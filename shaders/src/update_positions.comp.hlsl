struct UnitData {
    float2 pos;
    float2 vel;
};

// u0, space1 -> Read-write output buffer
RWStructuredBuffer<UnitData> OutUnits : register(u0, space1);

// t0, space0 -> Read-only source unit buffer
StructuredBuffer<UnitData> InUnits : register(t0, space0);

// t1, space0 -> Read-only sorted grid keys buffer (uint2: cellId, unitId)
StructuredBuffer<uint2> SortedKeys : register(t1, space0);

// t2, space0 -> Read-only cell ranges buffer (uint2: start, end)
StructuredBuffer<uint2> CellRanges : register(t2, space0);

// b0, space2 -> Uniform buffer constants
cbuffer SimParams : register(b0, space2) {
    float2 worldOrigin;
    float2 worldBounds;
    float dt;
    uint UnitCount;
    float Restitution;
    float Gravity;
    float UnitSize;
    float CellSize;
    uint2 GridDim;
    float2 InvCellSize;
    float2 padding;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint index = dispatchThreadId.x;
    if (index >= UnitCount) {
        return;
    }

    UnitData unit = InUnits[index];

    // 1. Integration (gravity & velocity)
    unit.vel.y += Gravity * dt;
    unit.pos += unit.vel * dt;

    // 2. Find collisions using Uniform Grid
    int cellX = clamp((int)((unit.pos.x - worldOrigin.x) * InvCellSize.x), 0, (int)GridDim.x - 1);
    int cellY = clamp((int)((unit.pos.y - worldOrigin.y) * InvCellSize.y), 0, (int)GridDim.y - 1);

    float2 totalDisplacement = float2(0.0f, 0.0f);
    uint collisionCount = 0;
    float desiredDist = UnitSize;
    float desiredDistSq = desiredDist * desiredDist;

    // Search 3x3 neighboring cells
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

            uint neighborCell = (uint)nx + (uint)ny * GridDim.x;
            uint2 range = CellRanges[neighborCell];

            // If empty cell, range is (0xFFFFFFFF, 0xFFFFFFFF)
            if (range.x == 0xFFFFFFFF) {
                continue;
            }

            for (uint s = range.x; s < range.y; ++s) {
                uint otherIdx = SortedKeys[s].y;
                if (otherIdx == index) {
                    continue;
                }

                UnitData other = InUnits[otherIdx];
                float2 diff = unit.pos - other.pos;
                float distSq = dot(diff, diff);

                if (distSq < desiredDistSq) {
                    float dist = sqrt(max(distSq, 0.0001f));
                    float2 dir = (distSq > 0.0001f) ? (diff / dist) : float2(0.0f, 1.0f);
                    float overlap = desiredDist - dist;

                    // Push this unit away by half the penetration
                    totalDisplacement += dir * (overlap * 0.5f);
                    collisionCount++;
                }
            }
        }
    }

    // Move unit by average overlap vector to minimise overlap with all neighbours
    if (collisionCount > 0) {
        unit.pos += totalDisplacement / float(collisionCount);
    }

    // 3. Edge collision & bounds clamp (clamping max pos to worldBounds so unit stays inside map)
    if (unit.pos.x < worldOrigin.x) {
        unit.pos.x = worldOrigin.x;
        unit.vel.x = -unit.vel.x * Restitution;
    } else if (unit.pos.x > worldBounds.x) {
        unit.pos.x = worldBounds.x;
        unit.vel.x = -unit.vel.x * Restitution;
    }

    if (unit.pos.y < worldOrigin.y) {
        unit.pos.y = worldOrigin.y;
        unit.vel.y = -unit.vel.y * Restitution;
    } else if (unit.pos.y > worldBounds.y) {
        unit.pos.y = worldBounds.y;
        unit.vel.y = -unit.vel.y * Restitution;
    }

    OutUnits[index] = unit;
}
