// Counting-sort broadphase, step 1: count units per cell. Each unit claims a
// slot in its cell with an atomic add and remembers (cell, slot), so the
// scatter step can place it without another atomic. A dead unit claims no
// cell, so no other unit ever finds it as a neighbour; it takes a slot in the
// extra counter after the last cell's instead, so the sorted keys still hold
// every unit (live ones by cell, then the dead) for sim/GpuUnitReorder.

#include "unit_state.hlsli"

struct UnitData {
    float2 pos;
    float2 vel;
};

StructuredBuffer<UnitData> Units : register(t0, space0);
// Per cell, then one more for the dead; all cleared to 0 beforehand.
RWStructuredBuffer<uint> Counts : register(u0, space1);
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

    float2 pos = Units[index].pos;
    uint slot;
    if (isDead(pos)) {
        InterlockedAdd(Counts[GridDim.x * GridDim.y], 1, slot);
        UnitCell[index] = uint2(NO_CELL, slot);
        return;
    }

    int2 cell = clamp((int2)(pos * InvCellSize), int2(0, 0), (int2)GridDim - 1);
    uint cellId = (uint)cell.x + (uint)cell.y * GridDim.x;

    InterlockedAdd(Counts[cellId], 1, slot);
    UnitCell[index] = uint2(cellId, slot);
}
