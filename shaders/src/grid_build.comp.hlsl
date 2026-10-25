struct UnitData {
    float2 pos;
    float2 vel;
};

// t0, space0 -> Read-only source unit buffer
StructuredBuffer<UnitData> InUnits : register(t0, space0);

// u0, space1 -> Read-write grid keys buffer (uint2: cellId, unitId)
RWStructuredBuffer<uint2> GridKeys : register(u0, space1);

// b0, space2 -> Uniform buffer constants
cbuffer GridBuildParams : register(b0, space2) {
    float2 worldOrigin;
    float2 invCellSize;
    uint2 gridDim;
    uint unitCount;
    uint allocatedUnits;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint index = dispatchThreadId.x;
    if (index >= allocatedUnits) {
        return;
    }

    if (index >= unitCount) {
        // Inactive padding entries sort to the end of the array
        GridKeys[index] = uint2(0xFFFFFFFF, 0xFFFFFFFF);
        return;
    }

    UnitData unit = InUnits[index];
    int cellX = clamp((int)((unit.pos.x - worldOrigin.x) * invCellSize.x), 0, (int)gridDim.x - 1);
    int cellY = clamp((int)((unit.pos.y - worldOrigin.y) * invCellSize.y), 0, (int)gridDim.y - 1);
    uint cellId = (uint)cellX + (uint)cellY * gridDim.x;

    GridKeys[index] = uint2(cellId, index);
}
