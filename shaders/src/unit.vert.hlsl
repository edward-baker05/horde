// Direct GPU Unit Vertex Shader with Native Sleep State Visualization.
//
// Reads unit positions and sleep states directly from the GPU physics storage buffer on VRAM.
// Quad vertices and transforms are computed entirely on the GPU without any
// CPU loops or PCIe vertex buffer uploads.
//
// SDL_GPU vertex stage binding rules:
//   (t[n], space0) - storage buffers / textures
//   (s[n], space0) - samplers
//   (b[n], space1) - uniform buffers

StructuredBuffer<float2> Positions : register(t0, space0);
StructuredBuffer<float2> Velocities : register(t1, space0);
StructuredBuffer<uint> UnitStates : register(t2, space0);

cbuffer UnitRenderUniforms : register(b0, space1) {
    float4x4 ViewProjection;
    float2 UnitSize;
    float2 Padding0;
    float4 UnitUV; // xy = top-left, zw = bottom-right atlas UV
};

struct Output {
    float4 Color : TEXCOORD0;
    float2 Texcoord : TEXCOORD1;
    float4 Position : SV_Position;
};

static const uint TriangleIndices[6] = {0, 1, 2, 3, 2, 1};

static const float2 QuadCorners[4] = {
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 1.0f),
};

Output main(uint id : SV_VertexID) {
    uint unitIndex = id / 6;
    uint corner = TriangleIndices[id % 6];

    float2 pos = Positions[unitIndex];
    uint stateWord = UnitStates[unitIndex];
    uint sleepState = stateWord & 0xFF;

    float2 texcoords[4] = {
        float2(UnitUV.x, UnitUV.y),
        float2(UnitUV.z, UnitUV.y),
        float2(UnitUV.x, UnitUV.w),
        float2(UnitUV.z, UnitUV.w),
    };

    float2 local = QuadCorners[corner] * UnitSize;
    float2 worldPos = pos + local;

    // Unit Color Coding:
    //  - Unit 0: Red leader
    //  - State 2: Rich Royal Blue (Deep Interior Sleeping)
    //  - State 1: Vivid Sky Cyan (Perimeter / Boundary Sleeping)
    //  - State 0: Vivid Neon Green (Active / Moving)
    float4 color = float4(0.0f, 1.0f, 0.2f, 1.0f);
    if (unitIndex == 0) {
        color = float4(1.0f, 0.0f, 0.0f, 1.0f);
    } else if (sleepState == 2) {
        color = float4(0.10f, 0.40f, 1.0f, 1.0f); // Royal Blue
    } else if (sleepState == 1) {
        color = float4(0.20f, 0.80f, 1.0f, 1.0f); // Cyan
    }

    Output output;
    output.Position = mul(ViewProjection, float4(worldPos, 0.0f, 1.0f));
    output.Color = color;
    output.Texcoord = texcoords[corner];
    return output;
}
