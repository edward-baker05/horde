// Clear Spatial Grid Compute Shader.
//
// Initializes CellCounts, CellStarts, and CellEnds to 0 across all grid cells.
//
// SDL_GPU compute stage binding rules:
//   (t[n], space0) - sampled textures, read-only storage textures/buffers
//   (s[n], space0) - samplers
//   (u[n], space1) - read-write storage textures/buffers
//   (b[n], space2) - uniform buffers

RWStructuredBuffer<uint> CellCounts : register(u0, space1);
RWStructuredBuffer<uint> CellStarts : register(u1, space1);
RWStructuredBuffer<uint> CellEnds : register(u2, space1);

cbuffer ClearGridUniforms : register(b0, space2) {
    uint NumCells;
    uint3 Padding;
};

[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    uint cellIdx = threadId.x;
    if (cellIdx < NumCells) {
        CellCounts[cellIdx] = 0;
        CellStarts[cellIdx] = 0;
        CellEnds[cellIdx] = 0;
    }
}
