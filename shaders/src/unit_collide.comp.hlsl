// One Jacobi relaxation pass of unit-vs-unit and unit-vs-wall collision, over
// the counting-sort broadphase (sim/GpuSpatialGrid):
//
//   GridA = SortedKeys  (cellId, unitIndex), sorted by cell
//   GridB = CellRanges  per cell: first key, then one past the last, as two
//                       consecutive uints; 0xFFFFFFFF first means empty
//
// Every unit reads its neighbours from InUnits and writes only itself to
// OutUnits, so a pass is independent of thread order; the caller ping-pongs
// the two buffers for as many passes as it wants. See collide_common.hlsli.

#include "collide_common.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint index = dispatchThreadId.x;
    if (index >= UnitCount) {
        return;
    }

    UnitData unit = InUnits[index];
    // Carried through to the other buffer as it is: never clamped back into
    // the level, which would bring it back to life.
    if (isDead(unit.pos)) {
        OutUnits[index] = unit;
        return;
    }
    float2 correction = float2(0.0f, 0.0f);
    uint overlaps = 0;
    accumulateNeighbours(index, unit.pos, cellOf(unit.pos), correction, overlaps);

    writeResolved(index, unit, correction, overlaps);
}
