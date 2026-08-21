// Unit Position Update & Spatial Counting Compute Shader.
//
// Updates 2D unit positions and velocities in parallel and counts cell occupancy:
//  - Applies gravity to all units
//  - Integrates velocity and position
//  - Resolves world edge & boundary collisions
//  - Atomically increments CellCounts and records local slot for deterministic counting sort
//
// SDL_GPU compute stage binding rules:
//   (t[n], space0) - sampled textures, read-only storage textures/buffers
//   (s[n], space0) - samplers
//   (u[n], space1) - read-write storage textures/buffers
//   (b[n], space2) - uniform buffers

RWStructuredBuffer<float2> Positions : register(u0, space1);
RWStructuredBuffer<float2> Velocities : register(u1, space1);
RWStructuredBuffer<uint> CellCounts : register(u2, space1);
RWStructuredBuffer<uint> CellKeys : register(u3, space1);
RWStructuredBuffer<uint> UnitLocalIndices : register(u4, space1);

cbuffer SimulationUniforms : register(b0, space2) {
    float DeltaTime;
    uint UnitCount;
    float Gravity;
    float Restitution;

    float2 WorldOrigin;
    float2 MaxPosition;

    float2 MinVelocity;
    float2 MaxVelocity;

    float FloorFriction;
    float UnitSize;
    float CellSize;
    float InvCellSize;

    float RowHeight;
    float InvRowHeight;
    int GridCols;
    int GridRows;

    int MaxCol;
    int MaxRow;
    float2 Padding;
};

[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    uint u = threadId.x;
    if (u >= UnitCount) {
        return;
    }

    float2 pos = Positions[u];
    float2 vel = Velocities[u];

    // 1. Universal gravity
    vel.y += Gravity * DeltaTime;

    // 2. Clamp velocity
    vel = clamp(vel, MinVelocity, MaxVelocity);

    // 3. Integrate position
    pos += vel * DeltaTime;

    // 4. Resolve boundary collisions
    if (pos.x <= WorldOrigin.x || pos.x >= MaxPosition.x) {
        vel.x *= Restitution;
    }

    if (pos.y <= WorldOrigin.y) {
        vel.y *= Restitution;
    }

    if (pos.y >= MaxPosition.y) {
        vel.y = 0.0f;
        vel.x *= FloorFriction;
    }

    pos = clamp(pos, WorldOrigin, MaxPosition);

    Positions[u] = pos;
    Velocities[u] = vel;

    // 5. Compute grid cell coordinates
    int row = clamp((int)(pos.y * InvRowHeight), 0, MaxRow);
    float xOffset = (row % 2 == 1) ? (0.5f * CellSize) : 0.0f;
    int col = clamp((int)((pos.x - xOffset) * InvCellSize), 0, MaxCol);
    uint cellIdx = (uint)(col + row * GridCols);

    // 6. Atomically reserve slot in cell
    uint localSlot;
    InterlockedAdd(CellCounts[cellIdx], 1, localSlot);

    CellKeys[u] = cellIdx;
    UnitLocalIndices[u] = localSlot;
}
