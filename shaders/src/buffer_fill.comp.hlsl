// Sets every Stride-th uint of a buffer to Value: Data[i * Stride] = Value for
// i < Count. Clears the counters of the counting-sort and bucket broadphases.

RWStructuredBuffer<uint> Data : register(u0, space1);

cbuffer FillParams : register(b0, space2) {
    uint Count;
    uint Value;
    uint Stride;
    uint Padding;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint i = dispatchThreadId.x;
    if (i < Count) {
        Data[i * Stride] = Value;
    }
}
