#include "../Clouds/CloudCommon.si"
#include "../Atmosphere/Atmosphere.si"

// Writes a PREMULTIPLIED cloud layer into a private RT (binding 0, imageHdrSceneColor).
// CloudSFERenderpass::Render() then blends that RT into "hdr" with a fullscreen draw:
//   finalRGB = srcRGB + dstRGB * (1 - srcA)
// so we output rgb = in-scattered light, a = 1 - transmittance.
// Never touch "hdr" from here: stage 1's render pass clears it after PreRender.
// (Old per-pixel god-ray loop removed: it marched along camPos/|camPos| rather than the view
//  ray, mixed km and m, and contributed ~nothing. Fog is already evaluated in the raymarch.)

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 texSize;
    imageHdrSceneColor.GetDimensions(texSize.x, texSize.y);
    int2 workPos = int2(dispatchThreadID.xy);

    if (workPos.x >= texSize.x || workPos.y >= texSize.y)
    {
        return;
    }

    const float2 uv = (float2(workPos) + float2(0.5f)) / float2(texSize);

    float sceneZ = inDepth.SampleLevel(pointClampEdgeSampler, uv, 0).x;

    float4 cloudColor = inCloudReconstructionTexture.SampleLevel(linearClampEdgeSampler, uv, 0);
    float4 fogColor = inCloudFogReconstructionTexture.SampleLevel(linearClampEdgeSampler, uv, 0);

    if (anyBadFloat(cloudColor))
    {
        cloudColor = float4(0.0, 0.0, 0.0, 1.0);
    }
    if (anyBadFloat(fogColor))
    {
        fogColor = float4(0.0, 0.0, 0.0, 1.0);
    }

    float4 outColor = float4(0.0, 0.0, 0.0, 0.0);

    if (sceneZ <= 0.0f) // reverse z: sky only
    {
        float3 rgb = cloudColor.rgb;
        float trans = cloudColor.a;

        if (fogColor.a >= 0.0f)
        {
            rgb = rgb * fogColor.a + max(float3(0.0f), fogColor.rgb);
            trans *= fogColor.a;
        }

        outColor = float4(rgb, saturate(1.0 - trans));
    }

    imageHdrSceneColor[workPos] = outColor;
}
