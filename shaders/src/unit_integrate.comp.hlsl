// Step 1 of the gpu-collide pipeline: steer each unit toward the nearest exit
// and predict its new position. Collisions then correct these predicted
// positions, and unit_finalize.comp derives velocity from how far each unit
// really moved.
//
// Each unit heads for the closest point of the closest exit band, so a crowd
// spreads along an exit instead of converging on one point of it. A unit whose
// centre is inside a field region follows the region's direction instead,
// turning into it at the region's strength, until it leaves: that is how a
// level steers the horde around a wall. Debug explosions then shove the
// predicted positions directly, or fling units by adding to a separate fling
// velocity that is free of the speed cap and dies away under drag; either
// way collisions and walls still have the last word on where units end up.
//
// SDL_GPU compute binding rules:
//   (t[n], space0) - read-only storage buffers
//   (u[n], space1) - read-write storage buffers
//   (b[n], space2) - uniform buffers

struct UnitData {
    float2 pos;
    float2 vel;
};

RWStructuredBuffer<UnitData> Units : register(u0, space1);
RWStructuredBuffer<float2> PrevPos : register(u1, space1);
// Velocity from flinging explosions, on top of the walking velocity in Units.
RWStructuredBuffer<float2> Fling : register(u2, space1);

#define MAX_MARKERS 8
#define MAX_FIELDS 32
#define MAX_EXPLOSIONS 8

// Matches GPUIntegrateUniforms in sim/GpuCollidePhysics.cpp
cbuffer IntegrateParams : register(b0, space2) {
    float Dt;
    uint UnitCount;
    float MaxSpeed;
    float Steering;      // how quickly velocity turns toward the desired velocity, per second
    float2 FallbackGoal; // a centre point to head for when the level has no exit
    float Radius;
    uint ExitCount;
    float4 Exits[MAX_MARKERS]; // xy = centre, zw = half size
    uint FieldCount;
    uint3 FieldPadding;
    // Two per region: [2i] xy = centre, zw = half size; [2i+1] xy = flow
    // direction (the box's local +x axis), z = turn rate per second.
    float4 Fields[MAX_FIELDS * 2];
    uint ExplosionCount;
    float FlingDrag;     // per second: how quickly fling velocity dies away
    float FlingMaxSpeed; // fling speed cap, so no step can skip through a wall
    uint ExplosionPadding;
    // Two per debug explosion: [2i] xy = centre, z = radius, w = this step's
    // push at the centre (negative pulls inward); [2i+1] x = falloff
    // (0 constant, 1 linear, 2 quadratic), y = 1 to fling rather than shove.
    float4 Explosions[MAX_EXPLOSIONS * 2];
};

// What every active explosion does to a unit at `centre` this step: how far
// shoves move it (`push`) and how much flings add to its velocity (`launch`).
void explosionPush(float2 centre, uint index, out float2 push, out float2 launch) {
    push = float2(0.0f, 0.0f);
    launch = float2(0.0f, 0.0f);
    for (uint i = 0; i < ExplosionCount; ++i) {
        float4 blast = Explosions[i * 2];
        float falloff = Explosions[i * 2 + 1].x;
        bool fling = Explosions[i * 2 + 1].y > 0.5f;
        float2 d = centre - blast.xy;
        float distance = length(d);
        if (distance >= blast.z) {
            continue;
        }
        // A unit right on the centre still gets a direction: spread by index.
        float angle = float(index) * 2.39996323f;
        float2 dir = distance > 0.001f ? d / distance : float2(cos(angle), sin(angle));
        float t = distance / blast.z;
        float scale = falloff < 0.5f ? 1.0f : (falloff < 1.5f ? 1.0f - t : (1.0f - t) * (1.0f - t));
        float amount = blast.w * scale;
        if (fling) {
            // Not so fast a pull would carry it past the centre in one step.
            launch += dir * max(amount, -distance / Dt);
        } else {
            // A pull stops at the centre rather than overshooting past it.
            push += dir * max(amount, -distance);
        }
    }
}

// Every field region containing `centre`, summed as direction x turn rate.
// Zero outside all of them.
float2 fieldFlow(float2 centre) {
    float2 flow = float2(0.0f, 0.0f);
    for (uint i = 0; i < FieldCount; ++i) {
        float4 box = Fields[i * 2];
        float4 field = Fields[i * 2 + 1];
        float2 d = centre - box.xy;
        // Into the box's frame: x along the push direction, y across it.
        float2 local = float2(dot(d, field.xy), dot(d, float2(-field.y, field.x)));
        if (all(abs(local) <= box.zw)) {
            flow += field.xy * field.z;
        }
    }
    return flow;
}

// The point units steer toward: the nearest point of the nearest exit.
float2 goalFor(float2 centre) {
    if (ExitCount == 0) {
        return FallbackGoal;
    }
    float2 best = Exits[0].xy;
    float bestSq = 3.4e38f;
    for (uint i = 0; i < ExitCount; ++i) {
        float2 q = clamp(centre, Exits[i].xy - Exits[i].zw, Exits[i].xy + Exits[i].zw);
        float2 d = q - centre;
        float distSq = dot(d, d);
        if (distSq < bestSq) {
            bestSq = distSq;
            best = q;
        }
    }
    return best;
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint index = dispatchThreadId.x;
    if (index >= UnitCount) {
        return;
    }

    UnitData unit = Units[index];
    PrevPos[index] = unit.pos;

    float2 centre = unit.pos + Radius;
    float2 desired;
    float turnRate;

    // A field region outranks the exit: inside one, a unit turns to follow
    // its arrow at full speed rather than cutting diagonally toward the exit.
    float2 flow = fieldFlow(centre);
    float flowRate = length(flow);
    if (flowRate > 0.0001f) {
        desired = flow / flowRate * MaxSpeed;
        turnRate = flowRate;
    } else {
        float2 toGoal = goalFor(centre) - centre;
        float distance = length(toGoal);
        desired = distance > 0.001f ? toGoal / distance * MaxSpeed : float2(0.0f, 0.0f);
        turnRate = Steering;
    }

    unit.vel += (desired - unit.vel) * saturate(turnRate * Dt);
    float2 push;
    float2 launch;
    explosionPush(centre, index, push, launch);
    float2 fling = Fling[index] * saturate(1.0f - FlingDrag * Dt) + launch;
    float flingSpeed = length(fling);
    if (flingSpeed > FlingMaxSpeed) {
        fling *= FlingMaxSpeed / flingSpeed;
    }
    Fling[index] = fling;

    unit.pos += (unit.vel + fling) * Dt + push;

    Units[index] = unit;
}
