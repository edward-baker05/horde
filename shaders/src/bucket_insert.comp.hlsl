// Bucket broadphase: each unit appends itself to its cell's fixed-size bucket.
// One pass, no sort. Buckets is laid out per cell as
// [count, index 0 .. index Capacity-1], with counts cleared beforehand. A unit
// arriving at a full bucket is counted but not stored.

struct UnitData {
    float2 pos;
    float2 vel;
};

StructuredBuffer<UnitData> Units : register(t0, space0);
RWStructuredBuffer<uint> Buckets : register(u0, space1);

// Matches GPUBucketUniforms in sim/GpuSpatialGrid.cpp
cbuffer BucketParams : register(b0, space2) {
    uint2 GridDim;
    float InvCellSize;
    uint UnitCount;
    uint Capacity;
    uint3 Padding;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint index = dispatchThreadId.x;
    if (index >= UnitCount) {
        return;
    }

    int2 cell = clamp((int2)(Units[index].pos * InvCellSize), int2(0, 0), (int2)GridDim - 1);
    uint base = ((uint)cell.x + (uint)cell.y * GridDim.x) * (Capacity + 1);

    uint slot;
    InterlockedAdd(Buckets[base], 1, slot);
    if (slot < Capacity) {
        Buckets[base + 1 + slot] = index;
    }
}
