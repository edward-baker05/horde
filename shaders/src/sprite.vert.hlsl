// Instanced sprite vertex shader.
//
// There is no vertex buffer: the quad corners are generated from SV_VertexID
// and every sprite's transform comes from a storage buffer, so a whole batch is
// one SDL_DrawGPUPrimitives call. Draw with (spriteCount * 6) vertices.
//
// SDL_GPU vertex stage binding rules:
//   (t[n], space0) - sampled textures, storage textures, storage buffers
//   (s[n], space0) - samplers
//   (b[n], space1) - uniform buffers

// Must match gfx::Sprite in src/gfx/SpriteBatch.hpp byte for byte.
struct Sprite {
    float3 position; // world position; z is the depth/layer
    float rotation;  // radians, counter-clockwise
    float2 size;     // world-space width and height
    float2 padding;
    float4 uv;       // xy = top-left, zw = bottom-right, in atlas UV space
    float4 color;    // multiplied with the sampled texel
};

StructuredBuffer<Sprite> Sprites : register(t0, space0);

cbuffer CameraUniforms : register(b0, space1) {
    float4x4 ViewProjection;
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
    uint spriteIndex = id / 6;
    uint corner = TriangleIndices[id % 6];

    Sprite sprite = Sprites[spriteIndex];

    float2 texcoords[4] = {
        float2(sprite.uv.x, sprite.uv.y),
        float2(sprite.uv.z, sprite.uv.y),
        float2(sprite.uv.x, sprite.uv.w),
        float2(sprite.uv.z, sprite.uv.w),
    };

    float c = cos(sprite.rotation);
    float s = sin(sprite.rotation);

    float2 local = QuadCorners[corner] * sprite.size;
    float2 rotated = float2(local.x * c - local.y * s, local.x * s + local.y * c);
    float3 world = float3(rotated + sprite.position.xy, sprite.position.z);

    Output output;
    output.Position = mul(ViewProjection, float4(world, 1.0f));
    output.Texcoord = texcoords[corner];
    output.Color = sprite.color;
    return output;
}
