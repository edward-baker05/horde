// Exclusive prefix sum, in place, over blocks of 512 uints (two per thread),
// writing each block's total to BlockSums. Scanning BlockSums the same way and
// adding it back (scan_add.comp) completes a scan of any length; see
// sim/GpuSpatialGrid.cpp.

RWStructuredBuffer<uint> Data : register(u0, space1);
RWStructuredBuffer<uint> BlockSums : register(u1, space1);

cbuffer ScanParams : register(b0, space2) {
    uint Count;
    uint3 Padding;
};

groupshared uint Partial[2][256];

[numthreads(256, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID) {
    uint t = localId.x;
    uint i0 = groupId.x * 512 + t * 2;
    uint i1 = i0 + 1;

    uint a = i0 < Count ? Data[i0] : 0;
    uint b = i1 < Count ? Data[i1] : 0;
    uint pair = a + b;

    // Hillis-Steele inclusive scan of the 256 pair sums, ping-ponging between
    // two shared arrays so no thread reads a value being overwritten.
    uint src = 0;
    Partial[0][t] = pair;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint offset = 1; offset < 256; offset <<= 1) {
        uint v = Partial[src][t];
        if (t >= offset) {
            v += Partial[src][t - offset];
        }
        Partial[1 - src][t] = v;
        src = 1 - src;
        GroupMemoryBarrierWithGroupSync();
    }

    uint inclusive = Partial[src][t];
    uint exclusive = inclusive - pair;

    if (i0 < Count) {
        Data[i0] = exclusive;
    }
    if (i1 < Count) {
        Data[i1] = exclusive + a;
    }
    if (t == 255) {
        BlockSums[groupId.x] = inclusive;
    }
}
