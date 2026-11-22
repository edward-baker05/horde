// RmlUi geometry vertex shader.
//
// Vertices come straight from Rml::Vertex (float2 position, ubyte4-normalised
// premultiplied colour, float2 uv). RmlUi positions are in the target's pixels,
// so the CPU folds the orthographic projection and any RmlUi transform into one
// matrix; the per-draw translation is added first, as RmlUi defines it.
//
// SDL_GPU vertex stage binding rules:
//   (b[n], space1) - uniform buffers

cbuffer RmlUniforms : register(b0, space1) {
    float4x4 Transform;   // projection * RmlUi transform
    float2 Translation;
    float2 padding;
};

struct Input {
    float2 Position : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float2 Texcoord : TEXCOORD2;
};

struct Output {
    float4 Color : TEXCOORD0;
    float2 Texcoord : TEXCOORD1;
    float4 Position : SV_Position;
};

Output main(Input input) {
    Output output;
    output.Position = mul(Transform, float4(input.Position + Translation, 0.0f, 1.0f));
    output.Color = input.Color;
    output.Texcoord = input.Texcoord;
    return output;
}
