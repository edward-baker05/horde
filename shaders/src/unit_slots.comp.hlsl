// Inverts the reorder's permutation for drawing (see sim/GpuUnitReorder):
// after units are put in grid order, records where each unit landed by its
// stable ID, so Slots[id] is that unit's index. unit.vert then draws in ID
// order, which doesn't change from step to step, so overlapping units don't
// swap which is on top every time the grid order does.
//
// IDs are a permutation of [0, UnitCount), so every slot is written once.
//
// SDL_GPU compute binding rules:
//   (t[n], space0) - read-only storage buffers
//   (u[n], space1) - read-write storage buffers
//   (b[n], space2) - uniform buffers

StructuredBuffer<uint> Ids : register(t0, space0);
RWStructuredBuffer<uint> Slots : register(u0, space1);

// Matches GPUSlotsUniforms in sim/GpuUnitReorder.cpp
cbuffer SlotsParams : register(b0, space2) {
    uint UnitCount;
    uint3 Padding;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint s = dispatchThreadId.x;
    if (s >= UnitCount) {
        return;
    }
    Slots[Ids[s]] = s;
}
