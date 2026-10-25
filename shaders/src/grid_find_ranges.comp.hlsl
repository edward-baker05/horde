// t0, space0 -> Read-only sorted grid keys buffer (uint2: cellId, unitId)
StructuredBuffer<uint2> SortedKeys : register(t0, space0);

// u0, space1 -> Read-write cell ranges buffer (uint2: start, end)
RWStructuredBuffer<uint2> CellRanges : register(u0, space1);

// b0, space2 -> Uniform buffer constants
cbuffer RangeParams : register(b0, space2) {
    uint unitCount;
    uint3 padding;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint i = dispatchThreadId.x;
    if (i >= unitCount) {
        return;
    }

    uint cell = SortedKeys[i].x;
    if (cell == 0xFFFFFFFF) {
        return;
    }

    bool isStart = (i == 0 || cell != SortedKeys[i - 1].x);
    bool isEnd = (i == unitCount - 1 || cell != SortedKeys[i + 1].x);

    if (isStart && isEnd) {
        CellRanges[cell] = uint2(i, i + 1);
    } else if (isStart) {
        CellRanges[cell].x = i;
    } else if (isEnd) {
        CellRanges[cell].y = i + 1;
    }
}
