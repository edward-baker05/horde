// Build Cell Ranges Compute Shader with Atomic Min/Max Resilience.
//
// Extracts robust contiguous unit index ranges [CellStarts[c], CellEnds[c]) for each grid cell
// using atomic min/max so that partially sorted or amortized frames are 100% resilient.
//
// SDL_GPU compute stage binding rules:
//   (t[n], space0) - sampled textures, read-only storage textures/buffers
//   (s[n], space0) - samplers
//   (u[n], space1) - read-write storage textures/buffers
//   (b[n], space2) - uniform buffers

StructuredBuffer<uint> CellKeys : register(t0, space0);
RWStructuredBuffer<int> CellStarts : register(u0, space1);
RWStructuredBuffer<int> CellEnds : register(u1, space1);

cbuffer RangeUniforms : register(b0, space2) {
    uint UnitCount;
    uint3 Padding;
};

[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    uint u = threadId.x;
    if (u >= UnitCount) {
        return;
    }

    uint key = CellKeys[u];
    InterlockedMin(CellStarts[key], (int)u);
    InterlockedMax(CellEnds[key], (int)(u + 1));
}
