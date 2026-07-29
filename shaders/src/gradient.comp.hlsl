// Compute smoke test.
//
// Writes an animated gradient into a storage texture, which LevelScene then
// draws as an ordinary sprite. It is deliberately trivial: its job is to prove
// the whole compute path end to end — device creation with a compute pipeline,
// a read-write storage texture, a dispatch, and the resulting texture being
// sampled by the graphics pipeline in the same frame.
//
// Keep it around. When the first real compute shader misbehaves it is much
// easier to debug against a known-good dispatch than against nothing.
//
// SDL_GPU compute stage binding rules:
//   (t[n], space0) - sampled textures, read-only storage textures/buffers
//   (s[n], space0) - samplers
//   (u[n], space1) - read-write storage textures/buffers
//   (b[n], space2) - uniform buffers

RWTexture2D<float4> OutputTexture : register(u0, space1);

cbuffer GradientUniforms : register(b0, space2) {
    float2 Resolution;
    float Time;
    float Padding;
};

[numthreads(8, 8, 1)]
void main(uint3 threadId : SV_DispatchThreadID) {
    if (threadId.x >= (uint)Resolution.x || threadId.y >= (uint)Resolution.y) {
        return;
    }

    float2 uv = float2(threadId.xy) / Resolution;

    float3 color = float3(
        0.5f + 0.5f * sin(Time + uv.x * 6.2831853f),
        0.5f + 0.5f * sin(Time * 0.7f + uv.y * 6.2831853f + 2.0943951f),
        0.5f + 0.5f * sin(Time * 1.3f + (uv.x + uv.y) * 3.1415927f + 4.1887902f));

    OutputTexture[threadId.xy] = float4(color, 1.0f);
}
