// CloudBlit.shader
// Blends Composite.shader's premultiplied cloud RT into "hdr" as a real subpass draw
// (CloudSFERenderpass::Render). Same pipeline state/convention as AtmosphereComposite.shader:
//   finalRGB = srcRGB + dstRGB * (1 - srcA)

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv0 : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv0 = uv;
    return output;
}

[[vk::binding(1, 0)]]
Sampler2D<float4> cloudColorSampler;

[shader("fragment")]
float4 fsMain(VSOutput input) : SV_Target
{
    return cloudColorSampler.SampleLevel(input.uv0, 0.0);
}
