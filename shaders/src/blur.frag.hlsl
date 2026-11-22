// One direction of a separable 9-tap gaussian. Run twice: direction (1,0) then
// (0,1). The centre weight is counted once and the other four on both sides.
//
// SDL_GPU fragment stage binding rules:
//   (t[n], space2) / (s[n], space2) - sampled textures / samplers
//   (b[n], space3)                  - uniform buffers

Texture2D<float4> Source : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);

cbuffer BlurUniforms : register(b0, space3) {
    float2 Direction;
    float2 TexelSize;
};

struct Input {
    float2 Texcoord : TEXCOORD0;
};

static const float kWeights[5] = {0.227027f, 0.1945946f, 0.1216216f, 0.054054f, 0.016216f};

float4 main(Input input) : SV_Target0 {
    const float2 step = Direction * TexelSize;
    float4 sum = Source.Sample(SourceSampler, input.Texcoord) * kWeights[0];
    for (int i = 1; i < 5; ++i) {
        sum += Source.Sample(SourceSampler, input.Texcoord + step * i) * kWeights[i];
        sum += Source.Sample(SourceSampler, input.Texcoord - step * i) * kWeights[i];
    }
    return sum;
}
