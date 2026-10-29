// Last step of the gpu-collide pipeline: keep units out of walls and inside
// the level, set each unit's velocity from where it actually ended up (so
// collisions slow and deflect units rather than only moving them:
// position-based dynamics), and damage units that reached an exit and send
// them back to a spawn.
//
// SDL_GPU compute binding rules:
//   (t[n], space0) - read-only storage buffers
//   (u[n], space1) - read-write storage buffers
//   (b[n], space2) - uniform buffers

struct UnitData {
    float2 pos;
    float2 vel;
};

StructuredBuffer<float2> PrevPos : register(t0, space0);
// t1..t3: walls, declared by walls.hlsli
RWStructuredBuffer<UnitData> Units : register(u0, space1);
RWStructuredBuffer<float> Health : register(u1, space1); // hit points, 0 to the max

#define MAX_MARKERS 8

// Matches GPUFinalizeUniforms in sim/GpuCollidePhysics.cpp
cbuffer FinalizeParams : register(b0, space2) {
    float2 WorldMin;
    float2 WorldMax;
    float InvDt;
    uint UnitCount;
    float MaxSpeed; // velocity is clamped to this, so a big correction cannot fling a unit
    float Radius;
    uint2 WallBinDim;
    float WallBinSize;
    uint WallCount;
    uint ExitCount;
    uint SpawnCount;
    uint Frame;         // varies the respawn position from frame to frame
    uint RespawnOnExit; // 0: units that reach an exit stay there
    float ExitDamage;   // hit points lost on reaching an exit
    uint3 HealthPadding;
    float4 Exits[MAX_MARKERS];  // xy = centre, zw = half size
    float4 Spawns[MAX_MARKERS]; // xy = centre, zw = half size
};

#define WALL_PRIMS_SLOT t1
#define WALL_BINS_SLOT t2
#define WALL_BIN_INDICES_SLOT t3
#include "walls.hlsli"

// PCG hash: a well-mixed uint from a uint.
uint hash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float unitFloat(uint v) {
    return float(v & 0x00FFFFFFu) / 16777216.0f;
}

bool reachedExit(float2 centre) {
    for (uint i = 0; i < ExitCount; ++i) {
        if (all(abs(centre - Exits[i].xy) <= Exits[i].zw + Radius)) {
            return true;
        }
    }
    return false;
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint index = dispatchThreadId.x;
    if (index >= UnitCount) {
        return;
    }

    UnitData unit = Units[index];
    float2 centre = resolveWalls(unit.pos + Radius, Radius);
    unit.pos = clamp(centre - Radius, WorldMin, WorldMax);

    float2 vel = (unit.pos - PrevPos[index]) * InvDt;
    float speed = length(vel);
    if (speed > MaxSpeed) {
        vel *= MaxSpeed / speed;
    }
    unit.vel = vel;

    // Through the exit: take the damage, then back in at a random point of a
    // random spawn, at rest, keeping whatever health is left.
    if (RespawnOnExit != 0 && SpawnCount > 0 && reachedExit(unit.pos + Radius)) {
        Health[index] = max(Health[index] - ExitDamage, 0.0f);

        uint h = hash(index ^ hash(Frame));
        float4 spawn = Spawns[h % SpawnCount];
        float2 r = float2(unitFloat(hash(h)), unitFloat(hash(h ^ 0x9E3779B9u))) * 2.0f - 1.0f;
        unit.pos = clamp(spawn.xy + r * spawn.zw - Radius, WorldMin, WorldMax);
        unit.vel = float2(0.0f, 0.0f);
    }

    Units[index] = unit;
}
