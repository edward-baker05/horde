// Calculate Spatial Cell Keys Compute Shader.
//
// Computes a 1D spatial hash / cell index for each unit based on its current 2D position
// and stores it in the CellKeys buffer on VRAM for amortized spatial sorting.
//
// SDL_GPU compute stage binding rules:
//   (t[n], space0) - sampled textures, read-only storage textures/buffers
//   (s[n], space0) - samplers
//   (u[n], space1) - read-write storage textures/buffers
//   (b[n], space2) - uniform buffers

StructuredBuffer<float2> Positions : register(t0, space0);
RWStructuredBuffer<uint> CellKeys : register(u0, space1);

cbuffer KeyUniforms : register(b0, space2) {
    uint UnitCount;
    float CellSize;
    float InvCellSize;
    float RowHeight;

    float InvRowHeight;
    int GridCols;
    int GridRows;
    int MaxCol;

    int MaxRow;
    float3 Padding;
};

[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    uint u = threadId.x;
    if (u >= UnitCount) {
        return;
    }

    float2 pos = Positions[u];

    int row = clamp((int)(pos.y * InvRowHeight), 0, MaxRow);
    float xOffset = (row % 2 == 1) ? (0.5f * CellSize) : 0.0f;
    int col = clamp((int)((pos.x - xOffset) * InvCellSize), 0, MaxCol);
    uint cellIdx = (uint)(col + row * GridCols);

    CellKeys[u] = cellIdx;
}
