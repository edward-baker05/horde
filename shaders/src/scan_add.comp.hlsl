// Finishes a multi-block prefix sum: adds each 512-element block's scanned
// offset (from the level above) to every element of the block.

StructuredBuffer<uint> BlockOffsets : register(t0, space0);
RWStructuredBuffer<uint> Data : register(u0, space1);

cbuffer ScanParams : register(b0, space2) {
    uint Count;
    uint3 Padding;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint i = dispatchThreadId.x;
    if (i < Count) {
        Data[i] += BlockOffsets[i / 512];
    }
}
