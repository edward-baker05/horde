// Reorders one per-unit buffer into the broadphase's sorted order (see
// sim/GpuUnitReorder): sorted place s takes what unit SortedKeys[s].y held.
// Units sharing a cell end up next to each other in memory, so passes that
// read neighbours read nearby memory.
//
// Any per-unit buffer works: elements are copied as Stride 32-bit words. The
// last buffer reordered also renumbers the keys (RenumberKeys), so that every
// key names the unit's new index, which is then its sorted place.
//
// SDL_GPU compute binding rules:
//   (t[n], space0) - read-only storage buffers
//   (u[n], space1) - read-write storage buffers
//   (b[n], space2) - uniform buffers

StructuredBuffer<uint> Source : register(t0, space0);
RWStructuredBuffer<uint2> SortedKeys : register(u0, space1);
RWStructuredBuffer<uint> Target : register(u1, space1);

// Matches GPUReorderUniforms in sim/GpuUnitReorder.cpp
cbuffer ReorderParams : register(b0, space2) {
    uint UnitCount;
    uint Stride; // words per element
    uint RenumberKeys;
    uint Padding;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint s = dispatchThreadId.x;
    if (s >= UnitCount) {
        return;
    }

    uint2 key = SortedKeys[s];
    uint from = key.y * Stride;
    uint to = s * Stride;
    for (uint k = 0; k < Stride; ++k) {
        Target[to + k] = Source[from + k];
    }
    if (RenumberKeys != 0) {
        SortedKeys[s] = uint2(key.x, s);
    }
}
