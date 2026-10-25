// Counting-sort broadphase, last step: with each cell's first index known
// (the prefix sum of counts), write every unit's key to its sorted place and
// record each cell's [start, end) range, in the same layout the bitonic
// broadphase produces.
//
// Dispatched over max(units, cells) threads: thread i places unit i and fills
// range i.

StructuredBuffer<uint2> UnitCell : register(t0, space0); // per unit: (cell, slot)
StructuredBuffer<uint> Starts : register(t1, space0);    // per cell: exclusive prefix sum of counts
StructuredBuffer<uint> Counts : register(t2, space0);    // per cell
RWStructuredBuffer<uint2> SortedKeys : register(u0, space1);
RWStructuredBuffer<uint2> CellRanges : register(u1, space1);

cbuffer ScatterParams : register(b0, space2) {
    uint UnitCount;
    uint CellCount;
    uint2 Padding;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint i = dispatchThreadId.x;

    if (i < UnitCount) {
        uint2 cellSlot = UnitCell[i];
        SortedKeys[Starts[cellSlot.x] + cellSlot.y] = uint2(cellSlot.x, i);
    }

    if (i < CellCount) {
        uint count = Counts[i];
        CellRanges[i] = count == 0 ? uint2(0xFFFFFFFF, 0xFFFFFFFF) : uint2(Starts[i], Starts[i] + count);
    }
}
