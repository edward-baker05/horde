// RmlUi fragment shader. Textures are stored premultiplied and so is the vertex
// colour, so a plain multiply stays premultiplied for the ONE / ONE_MINUS_SRC_ALPHA
// blend. Untextured geometry samples a 1x1 white texture.
//
// SDL_GPU fragment stage binding rules:
//   (t[n], space2) / (s[n], space2) - sampled textures / samplers

Texture2D<float4> Tex : register(t0, space2);
SamplerState TexSampler : register(s0, space2);

struct Input {
    float4 Color : TEXCOORD0;
    float2 Texcoord : TEXCOORD1;
};

float4 main(Input input) : SV_Target0 {
    return Tex.Sample(TexSampler, input.Texcoord) * input.Color;
}
