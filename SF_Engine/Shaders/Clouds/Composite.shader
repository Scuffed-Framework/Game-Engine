#include "Clouds/CloudCommon.si"
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

    // Sky: always. Geometry: only where the cloud's opacity-weighted hit is NEARER than the surface (reverse-z: larger = nearer).
    // Before this, clouds were skipped on every non-sky pixel, so a plane seen through a cloud layer was never obscured.
    bool drawCloud = (sceneZ <= 0.0f);
    if (!drawCloud)
    {
        const float cloudZ = inCloudDepthReconstructionTexture.SampleLevel(pointClampEdgeSampler, uv, 0).x;
        drawCloud = cloudZ > sceneZ;
    }

    if (drawCloud)
    {
        float3 rgb = cloudColor.rgb;
        float trans = cloudColor.a;

        if (fogColor.a >= 0.0f)
        {
            rgb = rgb * fogColor.a + max(float3(0.0f), fogColor.rgb);
            trans *= fogColor.a;
        }

        rgb = 1.0 - exp(-rgb * kCloudExposure); // same tonemap the sky uses (1-exp), so clouds don't blow out against it
        outColor = float4(rgb, saturate(1.0 - trans));
    }

    imageHdrSceneColor[workPos] = outColor;
}
