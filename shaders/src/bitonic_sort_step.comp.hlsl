// u0, space1 -> Read-write grid keys buffer (uint2: cellId, unitId)
RWStructuredBuffer<uint2> GridKeys : register(u0, space1);

// b0, space2 -> Uniform buffer constants
cbuffer SortParams : register(b0, space2) {
    uint k;
    uint j;
    uint totalEntries;
    uint padding;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint i = dispatchThreadId.x;
    if (i >= totalEntries / 2) {
        return;
    }

    uint i1 = 2 * i - (i & (j - 1));
    uint i2 = i1 + j;

    uint2 a = GridKeys[i1];
    uint2 b = GridKeys[i2];

    bool dir = ((i1 & k) == 0);
    // Primary key: cellId (a.x vs b.x), Secondary key: unitId (a.y vs b.y)
    bool greater = (a.x > b.x) || (a.x == b.x && a.y > b.y);
    bool less = (a.x < b.x) || (a.x == b.x && a.y < b.y);

    bool shouldSwap = dir ? greater : less;

    if (shouldSwap) {
        GridKeys[i1] = b;
        GridKeys[i2] = a;
    }
}
