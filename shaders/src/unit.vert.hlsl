// Instanced unit vertex shader for direct GPU rendering.
//
// Reads unit positions directly from the compute-updated storage buffer.
//
// SDL_GPU vertex stage binding rules:
//   (t[n], space0) - sampled textures, storage textures, storage buffers
//   (s[n], space0) - samplers
//   (b[n], space1) - uniform buffers

struct UnitData {
    float2 pos;
    float2 vel;
};

StructuredBuffer<UnitData> Units : register(t0, space0);
// Where each unit was one physics step earlier, when MaxJump > 0. Physics runs
// at a fixed rate below the frame rate, so drawing blends the last two steps.
StructuredBuffer<float2> PrevPos : register(t1, space0);
// Each unit's hit points, when InvMaxHealth > 0.
StructuredBuffer<float> Health : register(t2, space0);

cbuffer UnitUniforms : register(b0, space1) {
    float4x4 ViewProjection;
    float4 UvRect;   // xy = top-left, zw = bottom-right
    float4 Color;    // tint color
    float2 UnitSize; // width, height
    float Alpha;     // 0 = draw at PrevPos, 1 = draw at Units[].pos
    float MaxJump;   // 0: no PrevPos. Moves longer than this (a respawn) are not blended.
    float InvMaxHealth; // 1 / max hit points; 0: no Health buffer, draw everyone at full health
    float3 Padding;
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

    UnitData unit = Units[unitIndex];

    float2 texcoords[4] = {
        float2(UvRect.x, UvRect.y),
        float2(UvRect.z, UvRect.y),
        float2(UvRect.x, UvRect.w),
        float2(UvRect.z, UvRect.w),
    };

    float2 local = QuadCorners[corner] * UnitSize;
    float2 pos = unit.pos;
    if (MaxJump > 0.0f) {
        float2 prev = PrevPos[unitIndex];
        if (distance(prev, pos) <= MaxJump) {
            pos = lerp(prev, pos, Alpha);
        }
    }
    float2 world = pos + local;

    Output output;
    output.Position = mul(ViewProjection, float4(world, 0.0f, 1.0f));
    output.Texcoord = texcoords[corner];
    // Full health draws in Color, fading linearly to black at 0.
    float health = InvMaxHealth > 0.0f ? saturate(Health[unitIndex] * InvMaxHealth) : 1.0f;
    float4 tint = float4(Color.rgb * health, Color.a);
    output.Color = (unitIndex == 0) ? float4(1.0f, 0.0f, 0.0f, 1.0f) : tint;
    return output;
}
