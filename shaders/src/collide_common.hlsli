// Shared by the unit_collide*.comp variants: one Jacobi relaxation pass of
// unit-vs-unit collision followed by unit-vs-wall collision.
//
// Every variant binds the same resources in the same slots and differs only in
// how it finds neighbours (GridA/GridB), so they share one pipeline layout and
// one C++ caller (sim/GpuUnitCollisions.cpp).
//
// Units are discs of diameter Diameter. A unit's `pos` is its top-left corner,
// as the renderer draws it, so its centre is pos + Diameter / 2.
//
// SDL_GPU compute binding rules:
//   (t[n], space0) - read-only storage buffers
//   (u[n], space1) - read-write storage buffers
//   (b[n], space2) - uniform buffers

#ifndef HORDE_COLLIDE_COMMON_HLSLI
#define HORDE_COLLIDE_COMMON_HLSLI

struct UnitData {
    float2 pos;
    float2 vel;
};

StructuredBuffer<UnitData> InUnits : register(t0, space0);
// Broadphase-specific; see each variant.
StructuredBuffer<uint2> GridA : register(t1, space0);
StructuredBuffer<uint> GridB : register(t2, space0);
// t3..t5: walls, declared by walls.hlsli

RWStructuredBuffer<UnitData> OutUnits : register(u0, space1);

// Matches GPUCollideUniforms in sim/GpuUnitCollisions.cpp
cbuffer CollideParams : register(b0, space2) {
    float2 WorldMin;
    float2 WorldMax; // largest top-left position that keeps a unit inside the level
    uint2 GridDim;
    float InvCellSize;
    uint UnitCount;
    float Diameter;
    float Relaxation;    // relaxed solver: fraction of the summed correction applied per pass
    float MaxCorrection; // per pass, so dense clumps cannot explode
    // Nonzero once sim/GpuUnitReorder has put the units in grid order: key s
    // then names unit s, so GridA needn't be read.
    uint UnitsSorted;
    uint2 WallBinDim;
    float WallBinSize;
    uint WallCount;
    uint Solver;          // SOLVER_*
    float AverageOmega;   // average solver: over-relaxation factor on the mean, 1 = plain average
    float ChebyshevOmega; // chebyshev solver: this pass's weight, 1 = a plain relaxed pass
    float Smoothing;      // unit_smooth.comp: how far velocity moves toward the neighbours' mean, 0 to 1
};

// All are Jacobi: every unit reads the same state (InUnits) and writes the
// next (OutUnits). They differ in how a unit combines its pair corrections:
static const uint SOLVER_RELAXED = 0; // sum of corrections, scaled by Relaxation
static const uint SOLVER_AVERAGE = 1; // mean of corrections times AverageOmega
// Relaxed, then extrapolated from the iterate two passes back, which is what
// OutUnits still holds for this unit: next = w * (relaxed - older) + older.
static const uint SOLVER_CHEBYSHEV = 2;

#define WALL_PRIMS_SLOT t3
#define WALL_BINS_SLOT t4
#define WALL_BIN_INDICES_SLOT t5
#include "walls.hlsli"
#include "unit_state.hlsli"

// A stable direction for two units sitting exactly on top of each other, so
// they still separate. Opposite for the two members of the pair.
float2 separationFallback(uint self, uint other) {
    uint lo = min(self, other);
    uint hi = max(self, other);
    float angle = frac(sin(float(lo * 7919u + hi)) * 43758.5453f) * 6.2831853f;
    float2 dir = float2(cos(angle), sin(angle));
    return self < other ? dir : -dir;
}

// Adds to `correction` this unit's half of separating it from `otherIndex`,
// counting the overlap in `overlaps`.
void accumulatePair(uint index, float2 pos, uint otherIndex, inout float2 correction, inout uint overlaps) {
    if (otherIndex == index) {
        return;
    }
    float2 diff = pos - InUnits[otherIndex].pos;
    float distSq = dot(diff, diff);
    if (distSq >= Diameter * Diameter) {
        return;
    }
    float dist = sqrt(distSq);
    float2 dir = dist > 0.0001f ? diff / dist : separationFallback(index, otherIndex);
    correction += dir * ((Diameter - dist) * 0.5f);
    ++overlaps;
}

// The grid cell a unit's position falls in.
int2 cellOf(float2 pos) {
    return clamp((int2)(pos * InvCellSize), int2(0, 0), (int2)GridDim - 1);
}

// The keys of the three cells around `cell` in row `cell.y + dy`, as
// [start, end); empty (start >= end) if there are none or the row is off the
// grid.
//
// Cells in a row have consecutive ids and the keys are sorted by cell, so the
// three cells of a row are one contiguous run of keys: one loop per row
// instead of one per cell. Empty cells hold 0xFFFFFFFF at both ends, so the
// run starts at the smallest start, and ends at the largest end of the cells
// that aren't empty.
void rowKeys(int2 cell, int dy, out uint start, out uint end) {
    start = 0xFFFFFFFF;
    end = 0;
    int ny = cell.y + dy;
    if (ny < 0 || ny >= (int)GridDim.y) {
        return;
    }
    uint xLo = (uint)max(cell.x - 1, 0);
    uint xHi = (uint)min(cell.x + 1, (int)GridDim.x - 1);
    uint row = (uint)ny * GridDim.x;
    for (uint x = xLo; x <= xHi; ++x) {
        uint c = row + x;
        uint cellStart = GridB[c * 2];
        if (cellStart != 0xFFFFFFFF) {
            start = min(start, cellStart);
            end = max(end, GridB[c * 2 + 1]);
        }
    }
}

// The unit key s names.
uint keyUnit(uint s) {
    return UnitsSorted != 0 ? s : GridA[s].y;
}

// Sums this unit's pair corrections against every unit in the 3x3 cells
// around `cell`. Cells are one diameter wide, so every possible overlap is
// within that block.
void accumulateNeighbours(uint index, float2 pos, int2 cell, inout float2 correction, inout uint overlaps) {
    for (int dy = -1; dy <= 1; ++dy) {
        uint start;
        uint end;
        rowKeys(cell, dy, start, end);
        for (uint s = start; s < end; ++s) {
            accumulatePair(index, pos, keyUnit(s), correction, overlaps);
        }
    }
}

// Applies the combined unit correction, then walls, then the level bounds.
void writeResolved(uint index, UnitData unit, float2 correction, uint overlaps) {
    if (Solver == SOLVER_AVERAGE) {
        correction *= AverageOmega / (float)max(overlaps, 1u);
    } else {
        correction *= Relaxation;
    }
    float correctionLength = length(correction);
    if (correctionLength > MaxCorrection) {
        correction *= MaxCorrection / correctionLength;
    }

    // Before walls, so the extrapolated position is still kept out of them.
    // Read before this thread overwrites it, and no other thread reads it.
    // The whole move stays within MaxCorrection, as the stale grid requires.
    if (Solver == SOLVER_CHEBYSHEV && ChebyshevOmega != 1.0f) {
        float2 older = OutUnits[index].pos;
        float2 relaxed = unit.pos + correction;
        correction = ChebyshevOmega * (relaxed - older) + older - unit.pos;
        correctionLength = length(correction);
        if (correctionLength > MaxCorrection) {
            correction *= MaxCorrection / correctionLength;
        }
    }

    float radius = Diameter * 0.5f;
    float2 centre = resolveWalls(unit.pos + correction + radius, radius);
    unit.pos = clamp(centre - radius, WorldMin, WorldMax);
    OutUnits[index] = unit;
}

#endif
