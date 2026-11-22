// Aims mortars: for each request, finds the thickest crowd within the mortar's
// range on this step's grid (sim/GpuSpatialGrid), and writes the target cell
// to the mortar's slot of the feedback buffer, for the CPU to read back a few
// frames later (see sim/TurretEffects).
//
// One thread group per request. Candidate cells are every other cell within
// range; each scores the units in the 5x5 cells around it, so a mortar aims
// at a crowd rather than at one crammed cell. The group then reduces to the
// best candidate.
//
// SDL_GPU compute binding rules:
//   (t[n], space0) - read-only storage buffers
//   (u[n], space1) - read-write storage buffers
//   (b[n], space2) - uniform buffers

// Per cell [first key, one past the last), or 0xFFFFFFFF first when empty.
StructuredBuffer<uint> CellRanges : register(t0, space0);
// See sim/TurretEffects.hpp: a header, then FEEDBACK_PER_TURRET per turret.
RWStructuredBuffer<uint> Feedback : register(u0, space1);

#define MAX_TURRETS 64
#define FEEDBACK_HEADER 4
#define FEEDBACK_PER_TURRET 4
#define CANDIDATE_STRIDE 2
#define SCORE_REACH 2 // cells either side of a candidate it counts

// Matches GPUAimUniforms in sim/GpuCollidePhysics.cpp
cbuffer AimParams : register(b0, space2) {
    uint2 GridDim;
    float CellSize;
    uint RequestCount;
    // Per request: xy = the mortar's centre, z = range, w unused.
    float4 Requests[MAX_TURRETS];
    // Per request: x = the mortar's turret slot, y = the shot's ID.
    uint4 RequestIds[MAX_TURRETS];
};

uint unitsIn(int2 cell) {
    if (any(cell < 0) || any(cell >= (int2)GridDim)) {
        return 0;
    }
    uint c = (uint)cell.x + (uint)cell.y * GridDim.x;
    uint start = CellRanges[c * 2];
    return start == 0xFFFFFFFFu ? 0 : CellRanges[c * 2 + 1] - start;
}

groupshared uint BestScore[256];
groupshared uint BestCell[256];

[numthreads(256, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint threadIndex : SV_GroupIndex) {
    uint request = groupId.x;
    if (request >= RequestCount) {
        return;
    }

    float2 centre = Requests[request].xy;
    float range = Requests[request].z;
    int2 lo = max((int2)floor((centre - range) / CellSize), int2(0, 0));
    int2 hi = min((int2)floor((centre + range) / CellSize), (int2)GridDim - 1);
    uint2 span = (uint2)max((hi - lo) / CANDIDATE_STRIDE + 1, int2(0, 0));
    uint candidates = span.x * span.y;

    uint bestScore = 0;
    uint bestCell = 0;
    for (uint i = threadIndex; i < candidates; i += 256) {
        int2 cell = lo + int2(i % span.x, i / span.x) * CANDIDATE_STRIDE;
        float2 cellCentre = ((float2)cell + 0.5f) * CellSize;
        if (length(cellCentre - centre) > range) {
            continue;
        }
        uint score = 0;
        for (int dy = -SCORE_REACH; dy <= SCORE_REACH; ++dy) {
            for (int dx = -SCORE_REACH; dx <= SCORE_REACH; ++dx) {
                score += unitsIn(cell + int2(dx, dy));
            }
        }
        if (score > bestScore) {
            bestScore = score;
            bestCell = (uint)cell.x + (uint)cell.y * GridDim.x;
        }
    }

    BestScore[threadIndex] = bestScore;
    BestCell[threadIndex] = bestCell;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 128; stride > 0; stride >>= 1) {
        if (threadIndex < stride && BestScore[threadIndex + stride] > BestScore[threadIndex]) {
            BestScore[threadIndex] = BestScore[threadIndex + stride];
            BestCell[threadIndex] = BestCell[threadIndex + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (threadIndex == 0) {
        uint base = FEEDBACK_HEADER + RequestIds[request].x * FEEDBACK_PER_TURRET;
        Feedback[base] = RequestIds[request].y;
        Feedback[base + 1] = BestCell[0];
        Feedback[base + 2] = BestScore[0];
    }
}
