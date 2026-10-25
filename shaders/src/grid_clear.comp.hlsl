// u0, space1 -> Read-write cell ranges buffer (uint2: start, end)
RWStructuredBuffer<uint2> CellRanges : register(u0, space1);

// b0, space2 -> Uniform buffer constants
cbuffer ClearParams : register(b0, space2) {
    uint totalCells;
    uint3 padding;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint cellId = dispatchThreadId.x;
    if (cellId >= totalCells) {
        return;
    }

    CellRanges[cellId] = uint2(0xFFFFFFFF, 0xFFFFFFFF);
}
