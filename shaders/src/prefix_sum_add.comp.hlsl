// Prefix Sum (Add Block Offsets & Compute Cell Ends) Compute Shader.
//
// Adds scanned block offsets to CellStarts and calculates exact CellEnds = CellStarts + CellCounts.
//
// SDL_GPU compute stage binding rules:
//   (t[n], space0) - sampled textures, read-only storage textures/buffers
//   (s[n], space0) - samplers
//   (u[n], space1) - read-write storage textures/buffers
//   (b[n], space2) - uniform buffers

StructuredBuffer<uint> CellCounts : register(t0, space0);
StructuredBuffer<uint> BlockSums : register(t1, space0);
RWStructuredBuffer<uint> CellStarts : register(u0, space1);
RWStructuredBuffer<uint> CellEnds : register(u1, space1);

cbuffer AddUniforms : register(b0, space2) {
    uint NumCells;
    uint3 Padding;
};

[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    uint cellIdx = threadId.x;
    if (cellIdx >= NumCells) {
        return;
    }

    uint blockIdx = cellIdx / 512;
    uint blockOffset = BlockSums[blockIdx];
    uint start = CellStarts[cellIdx] + blockOffset;
    uint count = CellCounts[cellIdx];

    CellStarts[cellIdx] = start;
    CellEnds[cellIdx] = start + count;
}
