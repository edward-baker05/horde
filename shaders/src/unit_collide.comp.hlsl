// One Jacobi relaxation pass of unit-vs-unit and unit-vs-wall collision, for
// the sorted broadphases (bitonic and counting sort, sim/GpuSpatialGrid):
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
    int2 cell = cellOf(unit.pos);
    float2 correction = float2(0.0f, 0.0f);
    uint overlaps = 0;

    // Cells are one diameter wide, so every possible overlap is within the 3x3 block.
    for (int dy = -1; dy <= 1; ++dy) {
        int ny = cell.y + dy;
        if (ny < 0 || ny >= (int)GridDim.y) {
            continue;
        }
        for (int dx = -1; dx <= 1; ++dx) {
            int nx = cell.x + dx;
            if (nx < 0 || nx >= (int)GridDim.x) {
                continue;
            }

            uint c = (uint)nx + (uint)ny * GridDim.x;
            uint start = GridB[c * 2];
            if (start == 0xFFFFFFFF) {
                continue;
            }
            uint end = GridB[c * 2 + 1];
            for (uint s = start; s < end; ++s) {
                accumulatePair(index, unit.pos, GridA[s].y, correction, overlaps);
            }
        }
    }

    writeResolved(index, unit, correction, overlaps);
}
