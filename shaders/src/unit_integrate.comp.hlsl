// Step 1 of the gpu-collide pipeline: steer each unit toward the nearest exit
// and predict its new position. Collisions then correct these predicted
// positions, and unit_finalize.comp derives velocity from how far each unit
// really moved.
//
// Each unit heads for the closest point of the closest exit band, so a crowd
// spreads along an exit instead of converging on one point of it.
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

#define MAX_MARKERS 8

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
};

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
    float2 toGoal = goalFor(centre) - centre;
    float distance = length(toGoal);
    float2 desired = distance > 0.001f ? toGoal / distance * MaxSpeed : float2(0.0f, 0.0f);

    unit.vel += (desired - unit.vel) * saturate(Steering * Dt);
    unit.pos += unit.vel * Dt;

    Units[index] = unit;
}
