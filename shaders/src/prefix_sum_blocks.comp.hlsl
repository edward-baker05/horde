// Prefix Sum (Local Block Scan) Compute Shader.
//
// Computes local exclusive prefix sums for 512-element chunks of CellCounts
// using shared memory Blelloch scan and writes out BlockSums.
//
// SDL_GPU compute stage binding rules:
//   (t[n], space0) - sampled textures, read-only storage textures/buffers
//   (s[n], space0) - samplers
//   (u[n], space1) - read-write storage textures/buffers
//   (b[n], space2) - uniform buffers

StructuredBuffer<uint> CellCounts : register(t0, space0);
RWStructuredBuffer<uint> CellStarts : register(u0, space1);
RWStructuredBuffer<uint> BlockSums : register(u1, space1);

cbuffer ScanUniforms : register(b0, space2) {
    uint NumCells;
    uint3 Padding;
};

groupshared uint s_data[512];

[numthreads(256, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID) {
    uint tid = groupThreadId.x;
    uint blockIdx = groupId.x;

    uint idx0 = blockIdx * 512 + tid * 2;
    uint idx1 = idx0 + 1;

    s_data[tid * 2]     = (idx0 < NumCells) ? CellCounts[idx0] : 0;
    s_data[tid * 2 + 1] = (idx1 < NumCells) ? CellCounts[idx1] : 0;
    GroupMemoryBarrierWithGroupSync();

    // Up-sweep (Reduction) phase
    uint stride = 1;
    while (stride <= 256) {
        uint index = (tid + 1) * stride * 2 - 1;
        if (index < 512) {
            s_data[index] += s_data[index - stride];
        }
        stride *= 2;
        GroupMemoryBarrierWithGroupSync();
    }

    // Down-sweep phase for exclusive scan
    if (tid == 0) {
        BlockSums[blockIdx] = s_data[511]; // Total sum for this block
        s_data[511] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    stride = 256;
    while (stride >= 1) {
        uint index = (tid + 1) * stride * 2 - 1;
        if (index < 512) {
            uint temp = s_data[index - stride];
            s_data[index - stride] = s_data[index];
            s_data[index] += temp;
        }
        stride /= 2;
        GroupMemoryBarrierWithGroupSync();
    }

    if (idx0 < NumCells) {
        CellStarts[idx0] = s_data[tid * 2];
    }
    if (idx1 < NumCells) {
        CellStarts[idx1] = s_data[tid * 2 + 1];
    }
}
