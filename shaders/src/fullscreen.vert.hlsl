// Fullscreen triangle from SV_VertexID: no vertex or uniform buffers. Draw it
// with 3 vertices. uv (0,0) is the target's top-left, so sampling a texture
// with it is the identity mapping whichever backend flips NDC y.

struct Output {
    float2 Texcoord : TEXCOORD0;
    float4 Position : SV_Position;
};

Output main(uint id : SV_VertexID) {
    Output output;
    output.Texcoord = float2((id << 1) & 2, id & 2);
    output.Position = float4(output.Texcoord.x * 2.0f - 1.0f, 1.0f - output.Texcoord.y * 2.0f, 0.0f, 1.0f);
    return output;
}
