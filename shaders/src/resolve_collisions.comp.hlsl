// Unit Collision Resolution Compute Shader with 100% Contiguous Sorted Memory Access.
//
// Resolves 2D inter-unit collisions and overlaps in parallel:
//  - 100% contiguous sequential VRAM burst reads across neighbor cells for maximum cache hits
//  - Successive Over-Relaxation (SOR) + Progressive Non-Linear Hard-Core Repulsion
//    to eliminate spongy stacking and aggressively reduce unit overlaps
//  - PBD Tangential Contact Friction to stabilize stacking arches and piles
//  - Deep interior sleeping units (state 2, Royal Blue) skip redundant collision math
//  - Light sleeping units (state 1, Sky Cyan) check boundaries and propagate collision impulses
//  - Active moving units (state 0, Neon Green) resolve collisions and wake up resting units
//  - Classifies settled units dynamically into Interior, Perimeter, and Active
//
// SDL_GPU compute stage binding rules:
//   (t[n], space0) - sampled textures, read-only storage textures/buffers
//   (s[n], space0) - samplers
//   (u[n], space1) - read-write storage textures/buffers
//   (b[n], space2) - uniform buffers

RWStructuredBuffer<float2> Positions : register(u0, space1);
RWStructuredBuffer<float2> Velocities : register(u1, space1);
RWStructuredBuffer<uint> UnitStates : register(u2, space1);

StructuredBuffer<uint> CellStarts : register(t0, space0);
StructuredBuffer<uint> CellEnds : register(t1, space0);

cbuffer CollisionUniforms : register(b0, space2) {
    uint UnitCount;
    float UnitSize;
    float DesiredDistSq;
    float CellSize;

    float InvCellSize;
    float RowHeight;
    float InvRowHeight;
    float Restitution;

    int GridCols;
    int GridRows;
    int MaxCol;
    int MaxRow;

    float2 WorldOrigin;
    float2 MaxPosition;

    float Damping;
    float MaxDisplacement;
    uint EnableSleeping;
    uint DeepSleepMinContacts;

    float SleepMaxRelSpeed;
    float OverRelaxation;
    float ContactFriction;
    float Padding;
};

static const int2 EvenNeighbors[6] = {
    int2( 1,  0), int2(-1,  0), int2(-1, -1),
    int2( 0, -1), int2(-1,  1), int2( 0,  1)
};

static const int2 OddNeighbors[6] = {
    int2( 1,  0), int2(-1,  0), int2( 0, -1),
    int2( 1, -1), int2( 0,  1), int2( 1,  1)
};

void TestCell(
    uint u,
    uint mySleepState,
    float2 myPos,
    float2 myVel,
    int cellIdx,
    inout float2 totalDisp,
    inout float2 totalImpulse,
    inout uint contactCount,
    inout float maxRelSpeed)
{
    uint start = CellStarts[cellIdx];
    uint end = CellEnds[cellIdx];
    if (start >= end) {
        return;
    }

    for (uint v = start; v < end; ++v) {
        if (v != u) {
            uint otherStateWord = UnitStates[v];
            uint otherSleepState = otherStateWord & 0xFF;

            // DEEP SLEEP OPTIMIZATION:
            // If sleeping is enabled and both units are deep interior sleeping, skip expensive collision & separation math!
            if (EnableSleeping != 0 && mySleepState == 2 && otherSleepState == 2) {
                contactCount++;
            } else {
                float2 otherPos = Positions[v];
                float2 delta = otherPos - myPos;

                if (abs(delta.x) <= UnitSize && abs(delta.y) <= UnitSize) {
                    float distSq = dot(delta, delta);
                    if (distSq < DesiredDistSq && distSq > 0.00001f) {
                        contactCount++;
                        float dist = sqrt(distSq);
                        float2 normal = delta / dist;

                        // Non-linear hard-core separation:
                        // Over-relaxation + non-linear repulsion for deeper penetration to eliminate overlap
                        float penetrRatio = max(0.0f, 1.0f - (dist / UnitSize));
                        float stiffness = OverRelaxation * (1.0f + 1.5f * penetrRatio * penetrRatio);
                        float overlap = 0.5f * stiffness * (UnitSize - dist);
                        totalDisp -= normal * overlap;

                        // PBD Velocity Coupling & Collision Damping:
                        float2 otherVel = Velocities[v];
                        float2 relVel = otherVel - myVel;
                        float relSpeed = length(relVel);
                        maxRelSpeed = max(maxRelSpeed, relSpeed);

                        // Normal collision response (inelastic momentum transfer)
                        float normalVel = dot(relVel, normal);
                        if (normalVel < 0.0f) {
                            float normalImpulse = -normalVel * Damping;
                            totalImpulse -= normal * normalImpulse;
                        }

                        // Tangential friction (Coulomb contact friction to stabilize stacking arches and piles)
                        float2 tangent = float2(-normal.y, normal.x);
                        float tangentVel = dot(relVel, tangent);
                        totalImpulse += tangent * (tangentVel * ContactFriction);
                    }
                }
            }
        }
    }
}

[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    uint u = threadId.x;
    if (u >= UnitCount) {
        return;
    }

    float2 myPos = Positions[u];
    float2 myVel = Velocities[u];
    uint stateWord = UnitStates[u];
    uint mySleepState = stateWord & 0xFF;

    // Compute unit's hexagonal grid coordinates
    int row = clamp((int)(myPos.y * InvRowHeight), 0, MaxRow);
    float xOffset = (row % 2 == 1) ? (0.5f * CellSize) : 0.0f;
    int col = clamp((int)((myPos.x - xOffset) * InvCellSize), 0, MaxCol);

    float2 totalDisp = float2(0.0f, 0.0f);
    float2 totalImpulse = float2(0.0f, 0.0f);
    uint contactCount = 0;
    float maxRelSpeed = 0.0f;

    // 1. Check own cell
    int myCellIdx = col + row * GridCols;
    TestCell(u, mySleepState, myPos, myVel, myCellIdx, totalDisp, totalImpulse, contactCount, maxRelSpeed);

    // 2. Check all 6 surrounding hexagonal neighbors
    for (int k = 0; k < 6; ++k) {
        int2 offset = (row % 2 == 0) ? EvenNeighbors[k] : OddNeighbors[k];
        int nx = col + offset.x;
        int ny = row + offset.y;

        if (nx >= 0 && nx <= MaxCol && ny >= 0 && ny <= MaxRow) {
            int nIdx = nx + ny * GridCols;
            TestCell(u, mySleepState, myPos, myVel, nIdx, totalDisp, totalImpulse, contactCount, maxRelSpeed);
        }
    }

    // Apply accumulated displacement and impulse
    if (dot(totalDisp, totalDisp) > 0.0f || dot(totalImpulse, totalImpulse) > 0.0f) {
        float dispLen = length(totalDisp);
        if (dispLen > MaxDisplacement && dispLen > 0.0001f) {
            totalDisp = (totalDisp / dispLen) * MaxDisplacement;
        }

        myPos += totalDisp;
        myVel += totalImpulse;

        // Clamp to world bounds with restitution
        if (myPos.x <= WorldOrigin.x || myPos.x >= MaxPosition.x) {
            myVel.x *= Restitution;
        }
        if (myPos.y <= WorldOrigin.y) {
            myVel.y *= Restitution;
        }
        if (myPos.y >= MaxPosition.y) {
            myVel.y = 0.0f;
        }
        myPos = clamp(myPos, WorldOrigin, MaxPosition);

        Positions[u] = myPos;
        Velocities[u] = myVel;
    }

    // Dynamic Classification:
    //  - Deep Sleep (2, Royal Blue): Surrounded by >= DeepSleepMinContacts contacts with low relative shear (< SleepMaxRelSpeed)
    //  - Light Sleep (1, Sky Cyan): Perimeter unit with 1..(DeepSleepMinContacts-1) contacts
    //  - Active (0, Neon Green): Freely moving, colliding with high relative velocity (>= SleepMaxRelSpeed), or 0 contacts
    uint newSleepState = 0;
    if (EnableSleeping != 0) {
        if (maxRelSpeed < SleepMaxRelSpeed) {
            if (contactCount >= DeepSleepMinContacts) {
                newSleepState = 2; // Deep interior sleeping
            } else if (contactCount >= 1) {
                newSleepState = 1; // Light perimeter sleeping
            }
        }
    }

    UnitStates[u] = (newSleepState & 0xFF) | ((contactCount & 0xFF) << 16);
}
