// Prefix Sum (Top-Level Block Sums Scan) Compute Shader.
//
// Computes exclusive prefix scan across the BlockSums array in shared memory.
//
// SDL_GPU compute stage binding rules:
//   (t[n], space0) - sampled textures, read-only storage textures/buffers
//   (s[n], space0) - samplers
//   (u[n], space1) - read-write storage textures/buffers
//   (b[n], space2) - uniform buffers

RWStructuredBuffer<uint> BlockSums : register(u0, space1);

cbuffer TopScanUniforms : register(b0, space2) {
    uint NumBlocks;
    uint3 Padding;
};

groupshared uint s_blockData[2048];

[numthreads(1024, 1, 1)]
void main(uint3 groupThreadId : SV_GroupThreadID) {
    uint tid = groupThreadId.x;

    uint idx0 = tid * 2;
    uint idx1 = idx0 + 1;

    s_blockData[idx0] = (idx0 < NumBlocks) ? BlockSums[idx0] : 0;
    s_blockData[idx1] = (idx1 < NumBlocks) ? BlockSums[idx1] : 0;
    GroupMemoryBarrierWithGroupSync();

    // Up-sweep
    uint stride = 1;
    while (stride <= 1024) {
        uint index = (tid + 1) * stride * 2 - 1;
        if (index < 2048) {
            s_blockData[index] += s_blockData[index - stride];
        }
        stride *= 2;
        GroupMemoryBarrierWithGroupSync();
    }

    // Down-sweep
    if (tid == 0) {
        s_blockData[2047] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    stride = 1024;
    while (stride >= 1) {
        uint index = (tid + 1) * stride * 2 - 1;
        if (index < 2048) {
            uint temp = s_blockData[index - stride];
            s_blockData[index - stride] = s_blockData[index];
            s_blockData[index] += temp;
        }
        stride /= 2;
        GroupMemoryBarrierWithGroupSync();
    }

    if (idx0 < NumBlocks) {
        BlockSums[idx0] = s_blockData[idx0];
    }
    if (idx1 < NumBlocks) {
        BlockSums[idx1] = s_blockData[idx1];
    }
}
