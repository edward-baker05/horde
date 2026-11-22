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
// Status effects (unit_status.hlsli) act here too: a stunned unit stops
// trying to walk, a slowed one walks slower, and a burning one loses hit
// points. Explosions and turrets that hit a unit set its status from the
// status table.
//
// SDL_GPU compute binding rules:
//   (t[n], space0) - read-only storage buffers
//   (u[n], space1) - read-write storage buffers
//   (b[n], space2) - uniform buffers

struct UnitData {
    float2 pos;
    float2 vel;
};

// Each unit's stable ID: its index changes whenever units are reordered by
// cell, so anything that has to stay with one unit is keyed on this instead.
StructuredBuffer<uint> Ids : register(t0, space0);
// The previous step's cell ranges from sim/GpuSpatialGrid (first key, one past
// the last, per cell; 0xFFFFFFFF first if empty), read as a density map.
StructuredBuffer<uint> CellRanges : register(t1, space0);
#include "unit_status.hlsli"
// What each source applies: [0] nothing, [1 + slot] that turret's effects.
StructuredBuffer<UnitStatus> StatusTable : register(t2, space0);
// This step's blasts and turret effects, uploaded each step: two float4s per
// explosion from 0 (ExplosionCount of them), then two per turret effect from
// EffectsTurretBase (TurretCount of them). Not in IntegrateParams: SDL_GPU
// binds only the first 4 KiB of a uniform push, and these don't fit in it.
StructuredBuffer<float4> Effects : register(t3, space0);
RWStructuredBuffer<UnitData> Units : register(u0, space1);
RWStructuredBuffer<float2> PrevPos : register(u1, space1);
// Velocity from flinging explosions, on top of the walking velocity in Units.
RWStructuredBuffer<float2> Fling : register(u2, space1);
// Hit points, 0 to the max. Explosions take some away; unit_finalize
// despawns a unit that reaches 0.
RWStructuredBuffer<float> Health : register(u3, space1);
// See sim/TurretEffects.hpp. Armed mines count the units that come near.
RWStructuredBuffer<uint> Feedback : register(u4, space1);
RWStructuredBuffer<UnitStatus> Status : register(u5, space1);

#include "unit_state.hlsli"

#define MAX_MARKERS 8
#define MAX_FIELDS 32
// The two parts of Effects. Match sim::kMaxExplosions and logic::kMaxTurrets.
#define MAX_EXPLOSIONS 256
#define MAX_TURRETS 64

// How a turret's effect is shaped: matches sim::TurretShape.
#define TURRET_CONE 0 // within range, and within a half-angle of its direction
#define TURRET_BEAM 1 // within a half-width of the segment along its direction
#define TURRET_MINE 2 // no damage: a unit within range counts into Feedback
// A turret effect's last float holds its shape in the low bits, its turret
// slot above them.
#define TURRET_SHAPE_BITS 4
#define TURRET_SHAPE_MASK 15u
#define FEEDBACK_HEADER 4
#define FEEDBACK_PER_TURRET 4
#define FEEDBACK_HITS 3

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
    uint EffectsTurretBase; // where the turret effects start in Effects
    // Explosions live in Effects, two per explosion: [2i] xy = centre, z =
    // radius, w = this step's push at the centre (negative pulls inward);
    // [2i+1] x = falloff (0 constant, 1 linear, 2 quadratic), y = 1 to fling
    // rather than shove, z = this step's damage at the centre, in hit points,
    // w = its index into StatusTable (0: none).
    float Time;        // seconds of simulation
    float WobbleAngle; // radians either side of the heading; 0 turns it off
    float WobbleRate;  // Hz
    uint CongestionEnabled; // nonzero once CellRanges holds a grid
    uint2 GridDim;
    float InvCellSize;
    // Units per cell around the point a cell ahead at which a unit starts
    // slowing down, and at which it stops: hexagonal packing is ~1.15.
    float CongestionStart;
    float CongestionStop;
    uint3 CongestionPadding;
    uint TurretCount;
    uint3 TurretPadding;
    // Turret effects live in Effects from EffectsTurretBase, two per effect
    // active this step: [2i] xy = centre, zw = direction (unit length);
    // [2i+1] x = range, y = the cone's cos half-angle or the beam's
    // half-width, z = this step's damage, w = TURRET_* shape | turret slot
    // << TURRET_SHAPE_BITS.
};

// How far this unit's heading sways from straight at this moment, in radians.
// Two sines at unrelated rates, each with a phase hashed from the unit's ID,
// so the sway never quite repeats and neighbours are out of step.
float wobble(uint id) {
    uint h = id * 2654435761u;
    uint g = (id ^ 0x9E3779B9u) * 0x85EBCA6Bu;
    float phaseA = float(h >> 16) * (6.2831853f / 65536.0f);
    float phaseB = float(g >> 16) * (6.2831853f / 65536.0f);
    // Each unit's own speed, 0.75x to 1.25x the rate.
    float rate = WobbleRate * 6.2831853f * (0.75f + 0.5f * float(g & 0xFFFFu) / 65535.0f);
    return WobbleAngle * (0.65f * sin(Time * rate + phaseA) + 0.35f * sin(Time * rate * 1.7f + phaseB));
}

// What every active explosion does to a unit at `centre` this step: how far
// shoves move it (`push`), how much flings add to its velocity (`launch`),
// and how many hit points it loses (`damage`). Damage fades with distance by
// the same falloff as the push. Sets any status effects the blasts carry.
void explosionPush(float2 centre, uint id, inout UnitStatus status, out float2 push, out float2 launch,
                   out float damage) {
    push = float2(0.0f, 0.0f);
    launch = float2(0.0f, 0.0f);
    damage = 0.0f;
    for (uint i = 0; i < ExplosionCount; ++i) {
        float4 blast = Effects[i * 2];
        float4 blastEffect = Effects[i * 2 + 1];
        float falloff = blastEffect.x;
        bool fling = blastEffect.y > 0.5f;
        float2 d = centre - blast.xy;
        float distance = length(d);
        if (distance >= blast.z) {
            continue;
        }
        // A unit right on the centre still gets a direction: spread by ID.
        float angle = float(id) * 2.39996323f;
        float2 dir = distance > 0.001f ? d / distance : float2(cos(angle), sin(angle));
        float t = distance / blast.z;
        float scale = falloff < 0.5f ? 1.0f : (falloff < 1.5f ? 1.0f - t : (1.0f - t) * (1.0f - t));
        float amount = blast.w * scale;
        damage += blastEffect.z * scale;
        uint effect = (uint)blastEffect.w;
        if (effect != 0) {
            applyStatus(status, StatusTable[effect]);
        }
        if (fling) {
            // Not so fast a pull would carry it past the centre in one step.
            launch += dir * max(amount, -distance / Dt);
        } else {
            // A pull stops at the centre rather than overshooting past it.
            push += dir * max(amount, -distance);
        }
    }
}

// Hit points a unit at `centre` loses this step to turrets, setting any
// status effects they carry. Also counts it against any armed mine it is near.
float turretDamage(float2 centre, inout UnitStatus status) {
    float damage = 0.0f;
    for (uint i = 0; i < TurretCount; ++i) {
        float4 at = Effects[EffectsTurretBase + i * 2];
        float4 effect = Effects[EffectsTurretBase + i * 2 + 1];
        float2 d = centre - at.xy;
        float along = dot(d, at.zw);
        uint packed = (uint)effect.w;
        uint shape = packed & TURRET_SHAPE_MASK;
        uint slot = packed >> TURRET_SHAPE_BITS;
        bool hit;
        if (shape == TURRET_MINE) {
            if (length(d) <= effect.x + Radius) {
                InterlockedAdd(Feedback[FEEDBACK_HEADER + slot * FEEDBACK_PER_TURRET + FEEDBACK_HITS], 1u);
            }
            continue;
        } else if (shape == TURRET_BEAM) {
            float across = abs(dot(d, float2(-at.w, at.z)));
            hit = along >= 0.0f && along <= effect.x && across <= effect.y + Radius;
        } else {
            float distance = length(d);
            hit = distance <= effect.x + Radius && along >= distance * effect.y;
        }
        if (hit) {
            damage += effect.z;
            applyStatus(status, StatusTable[1 + slot]);
        }
    }
    return damage;
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

// How fast a unit heading along `dir` from `pos` (its top-left corner) may go,
// 0 to 1 of full speed, given how crowded the 3x3 cells centred two cells
// ahead of it were last step: the space one to three cells in front, not the
// unit's own cell or the units beside it, which would read any packed crowd
// as blocked even when the way ahead is clear. A unit slows before it walks into a crowd, rather than
// walking in and leaving collisions to push it back out; the unit behind it
// then sees it in the way and slows too, so a jam backs up as a queue instead
// of compressing.
float congestionSpeed(float2 pos, float2 dir) {
    if (CongestionEnabled == 0 || CongestionStop <= CongestionStart) {
        return 1.0f;
    }
    float2 ahead = pos + dir * (2.0f / InvCellSize);
    int2 cell = clamp((int2)(ahead * InvCellSize), int2(0, 0), (int2)GridDim - 1);
    uint units = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        int y = cell.y + dy;
        if (y < 0 || y >= (int)GridDim.y) {
            continue;
        }
        for (int dx = -1; dx <= 1; ++dx) {
            int x = cell.x + dx;
            if (x < 0 || x >= (int)GridDim.x) {
                continue;
            }
            uint c = (uint)x + (uint)y * GridDim.x;
            uint start = CellRanges[c * 2];
            if (start != 0xFFFFFFFF) {
                units += CellRanges[c * 2 + 1] - start;
            }
        }
    }
    float density = (float)units / 9.0f;
    return saturate((CongestionStop - density) / (CongestionStop - CongestionStart));
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
    if (isDead(unit.pos)) {
        return;
    }

    float2 centre = unit.pos + Radius;
    float2 desired;
    float turnRate;

    // Status timers run down first; anything hitting the unit this step
    // then starts its effects afresh.
    UnitStatus status = Status[index];
    bool stunned = status.stun > 0.0f;
    bool slowed = status.slow > 0.0f;
    float burnDamage = status.burn > 0.0f ? status.burnDps * min(status.burn, Dt) : 0.0f;
    status.stun = max(status.stun - Dt, 0.0f);
    status.slow = max(status.slow - Dt, 0.0f);
    status.burn = max(status.burn - Dt, 0.0f);

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

    if (WobbleAngle > 0.0f) {
        float s;
        float c;
        sincos(wobble(Ids[index]), s, c);
        desired = float2(desired.x * c - desired.y * s, desired.x * s + desired.y * c);
    }
    float desiredSpeed = length(desired);
    if (desiredSpeed > 0.001f) {
        desired *= congestionSpeed(unit.pos, desired / desiredSpeed);
    }
    // Stunned, a unit stops trying to go anywhere, slowing to a halt at its
    // usual steering rate; the crowd and blasts can still move it.
    if (stunned) {
        desired = float2(0.0f, 0.0f);
        turnRate = Steering;
    } else if (slowed) {
        desired *= status.slowFactor;
    }

    unit.vel += (desired - unit.vel) * saturate(turnRate * Dt);
    float2 push;
    float2 launch;
    float damage;
    explosionPush(centre, Ids[index], status, push, launch, damage);
    damage += turretDamage(centre, status) + burnDamage;
    Status[index] = status;
    if (damage > 0.0f) {
        Health[index] = max(Health[index] - damage, 0.0f);
    }
    float2 fling = Fling[index] * saturate(1.0f - FlingDrag * Dt) + launch;
    float flingSpeed = length(fling);
    if (flingSpeed > FlingMaxSpeed) {
        fling *= FlingMaxSpeed / flingSpeed;
    }
    Fling[index] = fling;

    unit.pos += (unit.vel + fling) * Dt + push;

    Units[index] = unit;
}
