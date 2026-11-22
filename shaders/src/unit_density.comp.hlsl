// A coarse map of where the crowd is, for towers to aim at: per cell, how
// many live units have their centre in it, and where one of them is and how
// fast it is walking, so a shot can lead it. Read
// back a few times a second by sim/GpuCollidePhysics.cpp (pollDensity).
//
// Which unit's position a cell ends up holding is whichever store lands
// last: any unit in the cell will do, as a point a small blast will hit.
// The stores aren't atomic together, so the fields can come from different
// units in the cell; that is still a point in the cell and a velocity of
// the crowd there.
//
// SDL_GPU compute binding rules:
//   (t[n], space0) - read-only storage buffers
//   (u[n], space1) - read-write storage buffers
//   (b[n], space2) - uniform buffers

struct UnitData {
    float2 pos;
    float2 vel;
};

StructuredBuffer<UnitData> Units : register(t0, space0);
// Per cell: x = units, yz = one unit's centre (asuint), w = its velocity as
// two halves (x low, y high; world units per second). Cleared
// to zero counts before each pass.
RWStructuredBuffer<uint4> Density : register(u0, space1);

#include "unit_state.hlsli"

// Matches GPUDensityUniforms in sim/GpuCollidePhysics.cpp
cbuffer DensityParams : register(b0, space2) {
    uint UnitCount;
    float Radius;
    float InvCellSize;
    uint DimX;
    uint DimY;
    uint3 Padding;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint index = dispatchThreadId.x;
    if (index >= UnitCount) {
        return;
    }
    float2 pos = Units[index].pos;
    if (isDead(pos)) {
        return;
    }
    float2 centre = pos + Radius;
    int2 cell = clamp((int2)floor(centre * InvCellSize), int2(0, 0), int2(DimX, DimY) - 1);
    uint c = (uint)cell.x + (uint)cell.y * DimX;
    InterlockedAdd(Density[c].x, 1);
    Density[c].y = asuint(centre.x);
    Density[c].z = asuint(centre.y);
    float2 vel = Units[index].vel;
    Density[c].w = f32tof16(vel.x) | (f32tof16(vel.y) << 16);
}
