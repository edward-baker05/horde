// Last step of the gpu-collide pipeline: keep units out of walls and inside
// the level, and set each unit's velocity from where it actually ended up, so
// collisions slow and deflect units rather than only moving them
// (position-based dynamics).
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
};

#define WALL_PRIMS_SLOT t1
#define WALL_BINS_SLOT t2
#define WALL_BIN_INDICES_SLOT t3
#include "walls.hlsli"

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

    Units[index] = unit;
}
