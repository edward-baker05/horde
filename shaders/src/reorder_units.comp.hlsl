// Unit Buffer Reordering Compute Shader.
//
// Scatters unit attributes into physically sorted contiguous storage arrays in VRAM:
//  - SortedIndex = CellStarts[cell] + UnitLocalIndex
//  - Guarantees 100% contiguous memory layout per grid cell with zero data races
//
// SDL_GPU compute stage binding rules:
//   (t[n], space0) - sampled textures, read-only storage textures/buffers
//   (s[n], space0) - samplers
//   (u[n], space1) - read-write storage textures/buffers
//   (b[n], space2) - uniform buffers

StructuredBuffer<float2> Positions : register(t0, space0);
StructuredBuffer<float2> Velocities : register(t1, space0);
StructuredBuffer<uint> UnitStates : register(t2, space0);
StructuredBuffer<uint> CellKeys : register(t3, space0);
StructuredBuffer<uint> UnitLocalIndices : register(t4, space0);
StructuredBuffer<uint> CellStarts : register(t5, space0);

RWStructuredBuffer<float2> SortedPositions : register(u0, space1);
RWStructuredBuffer<float2> SortedVelocities : register(u1, space1);
RWStructuredBuffer<uint> SortedUnitStates : register(u2, space1);

cbuffer ReorderUniforms : register(b0, space2) {
    uint UnitCount;
    uint3 Padding;
};

[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    uint u = threadId.x;
    if (u >= UnitCount) {
        return;
    }

    uint cell = CellKeys[u];
    uint localSlot = UnitLocalIndices[u];
    uint sortedIdx = CellStarts[cell] + localSlot;

    if (sortedIdx < UnitCount) {
        SortedPositions[sortedIdx] = Positions[u];
        SortedVelocities[sortedIdx] = Velocities[u];
        SortedUnitStates[sortedIdx] = UnitStates[u];
    }
}
