// One Jacobi relaxation iteration of unit-vs-unit collision.
//
// Every unit reads the positions of its neighbours from InUnits (found through
// the spatial grid: SortedKeys/CellRanges, see sim/GpuSpatialGrid) and writes
// its own corrected position to OutUnits. Reading one buffer and writing the
// other keeps an iteration independent of thread order; the caller ping-pongs
// the two buffers for as many iterations as it wants.
//
// Units are discs of diameter Diameter, so two overlap when their positions are
// closer than Diameter. Each pair pushes both units apart by half the overlap.
//
// SDL_GPU compute binding rules:
//   (t[n], space0) - read-only storage buffers
//   (u[n], space1) - read-write storage buffers
//   (b[n], space2) - uniform buffers

struct UnitData {
    float2 pos;
    float2 vel;
};

StructuredBuffer<UnitData> InUnits : register(t0, space0);
StructuredBuffer<uint2> SortedKeys : register(t1, space0); // (cellId, unitId), sorted by cell
StructuredBuffer<uint2> CellRanges : register(t2, space0); // [start, end) into SortedKeys, per cell

RWStructuredBuffer<UnitData> OutUnits : register(u0, space1);

// Matches GPUCollideUniforms in sim/GpuUnitCollisions.cpp
cbuffer CollideParams : register(b0, space2) {
    float2 WorldMin;
    float2 WorldMax;
    uint2 GridDim;
    float InvCellSize;
    uint UnitCount;
    float Diameter;
    float Relaxation;    // fraction of the summed correction applied per iteration
    float MaxCorrection; // per iteration, so dense clumps cannot explode
    float Padding;
};

// A stable direction for two units sitting exactly on top of each other, so
// they still separate. Opposite for the two members of the pair.
float2 separationFallback(uint self, uint other) {
    uint lo = min(self, other);
    uint hi = max(self, other);
    float angle = frac(sin(float(lo * 7919u + hi)) * 43758.5453f) * 6.2831853f;
    float2 dir = float2(cos(angle), sin(angle));
    return self < other ? dir : -dir;
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint index = dispatchThreadId.x;
    if (index >= UnitCount) {
        return;
    }

    UnitData unit = InUnits[index];

    int cellX = clamp((int)(unit.pos.x * InvCellSize), 0, (int)GridDim.x - 1);
    int cellY = clamp((int)(unit.pos.y * InvCellSize), 0, (int)GridDim.y - 1);

    float diameterSq = Diameter * Diameter;
    float2 correction = float2(0.0f, 0.0f);

    // Cells are one diameter wide, so every possible overlap is within the 3x3 block.
    for (int dy = -1; dy <= 1; ++dy) {
        int ny = cellY + dy;
        if (ny < 0 || ny >= (int)GridDim.y) {
            continue;
        }

        for (int dx = -1; dx <= 1; ++dx) {
            int nx = cellX + dx;
            if (nx < 0 || nx >= (int)GridDim.x) {
                continue;
            }

            uint2 range = CellRanges[(uint)nx + (uint)ny * GridDim.x];
            if (range.x == 0xFFFFFFFF) {
                continue;
            }

            for (uint s = range.x; s < range.y; ++s) {
                uint otherIndex = SortedKeys[s].y;
                if (otherIndex == index) {
                    continue;
                }

                float2 diff = unit.pos - InUnits[otherIndex].pos;
                float distSq = dot(diff, diff);
                if (distSq >= diameterSq) {
                    continue;
                }

                float dist = sqrt(distSq);
                float2 dir = dist > 0.0001f ? diff / dist : separationFallback(index, otherIndex);
                correction += dir * ((Diameter - dist) * 0.5f);
            }
        }
    }

    correction *= Relaxation;
    float correctionLength = length(correction);
    if (correctionLength > MaxCorrection) {
        correction *= MaxCorrection / correctionLength;
    }

    unit.pos = clamp(unit.pos + correction, WorldMin, WorldMax);
    OutUnits[index] = unit;
}
