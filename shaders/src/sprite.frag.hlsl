// Instanced sprite fragment shader. Samples the atlas and applies the per-sprite
// tint.
//
// SDL_GPU fragment stage binding rules:
//   (t[n], space2) - sampled textures, storage textures, storage buffers
//   (s[n], space2) - samplers
//   (b[n], space3) - uniform buffers

Texture2D<float4> Atlas : register(t0, space2);
SamplerState AtlasSampler : register(s0, space2);

struct Input {
    float4 Color : TEXCOORD0;
    float2 Texcoord : TEXCOORD1;
};

float4 main(Input input) : SV_Target0 {
    return Atlas.Sample(AtlasSampler, input.Texcoord) * input.Color;
}
