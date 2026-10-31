// Last step of the gpu-collide pipeline: keep units out of walls and inside
// the level, set each unit's velocity from where it actually ended up (so
// collisions slow and deflect units rather than only moving them, and soak up
// fling velocity: position-based dynamics), and damage units that reached an
// exit and send them back to a spawn.
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
// Velocity from flinging explosions, which is free of the speed cap.
RWStructuredBuffer<float2> Fling : register(u2, space1);

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

// Most hops sweepTowards() takes. sim/GpuCollidePhysics.cpp keeps blasts
// from moving a unit further than this many radii in one step.
#define MAX_SWEEP_HOPS 64

// Where a unit moving from `start` to `end` this step (both centres) should
// end up, given walls. Walking moves less than a radius a step and is left
// alone: resolveWalls() alone keeps it out. A fast move, such as an explosion
// or fling, could skip clean through a thin wall that way, so it is walked
// in hops of at most a radius; at the first hop that runs further into a
// wall than the unit started out, it stops at the hop before, still on the
// near side. (Not at the hop itself: a unit already pressed into a thin wall
// could land past its middle, and be pushed out the far side.) Moving
// alongside a wall it is already touching doesn't count, so a unit pressed
// against a wall can still slide along it.
float2 sweepTowards(float2 start, float2 end) {
    float2 move = end - start;
    float distance = length(move);
    if (WallCount == 0 || distance <= Radius) {
        return end;
    }

    uint hops = min((uint)ceil(distance / Radius), MAX_SWEEP_HOPS);
    float2 hop = move / hops;
    float startClearance = wallClearance(start);
    for (uint i = 1; i <= hops; ++i) {
        float2 p = start + hop * i;
        float clearance = wallClearance(p);
        if (clearance < Radius && clearance < startClearance - 0.01f) {
            return start + hop * (i - 1);
        }
    }
    return end;
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
    float2 centre = sweepTowards(PrevPos[index] + Radius, unit.pos + Radius);
    centre = resolveWalls(centre, Radius);
    unit.pos = clamp(centre - Radius, WorldMin, WorldMax);

    float2 vel = (unit.pos - PrevPos[index]) * InvDt;

    // A flung unit keeps only as much of its fling as it actually managed:
    // hitting a wall or a crowd soaks the rest up. The remainder is walking,
    // capped as usual, so collisions can't fling anything.
    float2 fling = Fling[index];
    float flingSpeed = length(fling);
    if (flingSpeed > 0.0f) {
        float2 dir = fling / flingSpeed;
        fling = dir * clamp(dot(vel, dir), 0.0f, flingSpeed);
        vel -= fling;
    }

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
        fling = float2(0.0f, 0.0f);
    }

    Fling[index] = fling;

    Units[index] = unit;
}
