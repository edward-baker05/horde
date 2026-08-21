// Amortized GPU Odd-Even Spatial Sorting Compute Shader.
//
// Performs parallel transposition sorting on adjacent unit pairs using temporal coherence:
//  - PassType 0: Compares & swaps even pairs (2i, 2i + 1)
//  - PassType 1: Compares & swaps odd pairs (2i + 1, 2i + 2)
//
// When keyA > keyB, physically swaps CellKeys, Positions, Velocities, and UnitStates
// to keep unit memory buffers contiguous and sorted in VRAM across frames.
//
// SDL_GPU compute stage binding rules:
//   (t[n], space0) - sampled textures, read-only storage textures/buffers
//   (s[n], space0) - samplers
//   (u[n], space1) - read-write storage textures/buffers
//   (b[n], space2) - uniform buffers

RWStructuredBuffer<uint> CellKeys : register(u0, space1);
RWStructuredBuffer<float2> Positions : register(u1, space1);
RWStructuredBuffer<float2> Velocities : register(u2, space1);
RWStructuredBuffer<uint> UnitStates : register(u3, space1);

cbuffer SortUniforms : register(b0, space2) {
    uint UnitCount;
    uint PassType; // 0 = Even, 1 = Odd
    uint2 Padding;
};

[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    uint i = threadId.x;
    uint idxA = (i * 2) + PassType;
    uint idxB = idxA + 1;

    if (idxB >= UnitCount) {
        return;
    }

    uint keyA = CellKeys[idxA];
    uint keyB = CellKeys[idxB];

    if (keyA > keyB) {
        // Swap CellKeys
        CellKeys[idxA] = keyB;
        CellKeys[idxB] = keyA;

        // Swap Positions
        float2 posA = Positions[idxA];
        Positions[idxA] = Positions[idxB];
        Positions[idxB] = posA;

        // Swap Velocities
        float2 velA = Velocities[idxA];
        Velocities[idxA] = Velocities[idxB];
        Velocities[idxB] = velA;

        // Swap UnitStates
        uint stateA = UnitStates[idxA];
        UnitStates[idxA] = UnitStates[idxB];
        UnitStates[idxB] = stateA;
    }
}
