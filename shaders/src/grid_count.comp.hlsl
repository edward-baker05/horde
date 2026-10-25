// Counting-sort broadphase, step 1: count units per cell. Each unit claims a
// slot in its cell with an atomic add and remembers (cell, slot), so the
// scatter step can place it without another atomic.

struct UnitData {
    float2 pos;
    float2 vel;
};

StructuredBuffer<UnitData> Units : register(t0, space0);
RWStructuredBuffer<uint> Counts : register(u0, space1);    // per cell, cleared to 0 beforehand
RWStructuredBuffer<uint2> UnitCell : register(u1, space1); // per unit: (cell, slot within cell)

// Matches GPUGridCountUniforms in sim/GpuSpatialGrid.cpp
cbuffer CountParams : register(b0, space2) {
    uint2 GridDim;
    float InvCellSize;
    uint UnitCount;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint index = dispatchThreadId.x;
    if (index >= UnitCount) {
        return;
    }

    int2 cell = clamp((int2)(Units[index].pos * InvCellSize), int2(0, 0), (int2)GridDim - 1);
    uint cellId = (uint)cell.x + (uint)cell.y * GridDim.x;

    uint slot;
    InterlockedAdd(Counts[cellId], 1, slot);
    UnitCell[index] = uint2(cellId, slot);
}
