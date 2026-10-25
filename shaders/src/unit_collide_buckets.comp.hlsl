// One Jacobi relaxation pass of unit-vs-unit and unit-vs-wall collision, for
// the fixed-capacity bucket broadphase (sim/GpuSpatialGrid, Buckets):
//
//   GridA = unused (bound to keep one pipeline layout for every variant)
//   GridB = per cell: count, then BucketCapacity unit indices, i.e. cell c's
//           count is GridB[c * (BucketCapacity + 1)]
//
// A cell holding more units than BucketCapacity only exposes the first
// BucketCapacity to its neighbours. Cells are one diameter wide, so at most
// four non-overlapping units fit; the extra room is for crowds mid-squeeze.

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
    uint stride = BucketCapacity + 1;

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

            uint base = ((uint)nx + (uint)ny * GridDim.x) * stride;
            uint count = min(GridB[base], BucketCapacity);
            for (uint s = 0; s < count; ++s) {
                accumulatePair(index, unit.pos, GridB[base + 1 + s], correction);
            }
        }
    }

    writeResolved(index, unit, correction);
}
