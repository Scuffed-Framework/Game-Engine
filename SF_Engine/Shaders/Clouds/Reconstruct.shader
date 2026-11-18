#include "Clouds/CloudCommon.si"

// World position from uv + (reverse-z) depth.
// NO Y flip: must match Atmosphere.shader / RaymarchClouds.shader (ndc = uv*2-1).
// The old code flipped Y here AND when reprojecting, which only cancels for a static camera;
// under motion it reprojected the mirrored pixel -> stair-step streaks and ghosting.
float3 getWorldPosFromUV(float2 uv, float depth)
{
    // Keep w away from 0 for "no cloud" (depth==0 -> infinitely far).
    float safeDepth = max(depth, 1e-5);

    float4 clipSpace = float4(uv.x * 2.0f - 1.0f, uv.y * 2.0f - 1.0f, safeDepth, 1.0);
    float4 viewPosH = mul(kAtmo.invProj, clipSpace);
    float3 viewSpacePos = viewPosH.xyz / viewPosH.w;
    float4 worldPos = mul(kAtmo.invView, float4(viewSpacePos, 1.0));
    return worldPos.xyz;
}

bool isInRange(float2 value, float2 minVal, float2 maxVal)
{
    return all(value >= minVal) && all(value <= maxVal);
}

[shader("compute")]
[numthreads(8,8,1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 texSize;
    imageCloudReconstructionTexture.GetDimensions(texSize.x, texSize.y);
    int2 workPos = int2(dispatchThreadID.xy);

    if(workPos.x >= texSize.x || workPos.y >= texSize.y)
    {
        return;
    }

    const float2 uv = (float2(workPos) + float2(0.5f)) / float2(texSize);

    int2 renderTexSize;
    inCloudRenderTexture.GetDimensions(renderTexSize.x, renderTexSize.y);
    const int2 readPos = min(workPos / 4, renderTexSize - 1);

    float traceCloudDepth = inCloudDepthTexture.Load(int3(readPos, 0)).r;

    // Reproject to get prev uv (same convention both ways: no flip).
    float3 worldPosCur = getWorldPosFromUV(uv, traceCloudDepth);
    float4 projPosPrev = mul(kCam.prevViewProjection, float4(worldPosCur, 1.0));
    float3 projPosPrevH = projPosPrev.xyz / projPosPrev.w;
    float2 uvPrev = projPosPrevH.xy * 0.5 + 0.5;

    const bool bPrevUvValid = projPosPrev.w > 0.0 && isInRange(uvPrev, float2(0.0), float2(1.0));

    float4 color = float4(0.0);
    float4 fog = float4(0.0);
    float depthZ = 0.0;

    // Current low-res frame, bilinearly upsampled: always-available fallback.
    float4 curBilinear = inCloudRenderTexture.SampleLevel(linearClampEdgeSampler, uv, 0);
    float4 curFogBilinear = inCloudFogRenderTexture.SampleLevel(linearClampEdgeSampler, uv, 0);

    if(bPrevUvValid)
    {
        float4 curColor = inCloudRenderTexture.Load(int3(readPos, 0));
        float4 curFog = inCloudFogRenderTexture.Load(int3(readPos, 0));
        float curDepthZ = inCloudDepthTexture.Load(int3(readPos, 0)).r;

        uint bayerIndex = kCloud.frameIndex.x % 16;
        int bayerValue = kBayer4x4[bayerIndex];
        int2 bayerOffset = int2(bayerValue % 4, bayerValue / 4);
        int2 workDeltaPos = workPos % 4;
        const bool bUpdateEvaluate = (workDeltaPos.x == bayerOffset.x) && (workDeltaPos.y == bayerOffset.y);

        // The freshly evaluated pixel used to REPLACE its history with the raw (jittered) sample, so 1 pixel in 16 carried a full
        // single-frame noise value for 16 frames: the dither-dot speckle. It is now blended like the rest, with a higher current
        // weight, and takes the exact (nearest) sample instead of the bilinear one.
        {
            const float4 curSample    = bUpdateEvaluate ? curColor : curBilinear;
            const float4 curFogSample = bUpdateEvaluate ? curFog : curFogBilinear;
            float4 hist = inCloudReconstructionTextureHistory.SampleLevel(linearClampEdgeSampler, uvPrev, 0);
            float4 histFog = inCloudFogReconstructionTextureHistory.SampleLevel(linearClampEdgeSampler, uvPrev, 0);
            float histDepth = inCloudDepthReconstructionTextureHistory.SampleLevel(linearClampEdgeSampler, uvPrev, 0).r;

            // Neighbourhood clamp: pull history into the range of the current low-res 3x3 so stale
            // cloud (disoccluded / moved) can't smear. Standard TAA ghost rejection.
            float4 nMin = curColor;
            float4 nMax = curColor;
            float4 fMin = curFog;
            float4 fMax = curFog;
            [unroll]
            for (int y = -1; y <= 1; y++)
            {
                [unroll]
                for (int x = -1; x <= 1; x++)
                {
                    int2 p = clamp(readPos + int2(x, y), int2(0, 0), renderTexSize - 1);
                    float4 c = inCloudRenderTexture.Load(int3(p, 0));
                    float4 f = inCloudFogRenderTexture.Load(int3(p, 0));
                    nMin = min(nMin, c);
                    nMax = max(nMax, c);
                    fMin = min(fMin, f);
                    fMax = max(fMax, f);
                }
            }
            // Slight expansion keeps some temporal accumulation (denoising) alive.
            float4 nPad = (nMax - nMin) * 0.25 + 0.002;
            float4 fPad = (fMax - fMin) * 0.25 + 0.002;
            hist = clamp(hist, nMin - nPad, nMax + nPad);
            histFog = clamp(histFog, fMin - fPad, fMax + fPad);

            // Motion-adaptive blend: more current frame the faster the screen-space motion.
            float motionPx = length((uvPrev - uv) * float2(texSize));
            float curW = saturate(0.08 + motionPx * 0.15);
            if (bUpdateEvaluate)
                curW = max(curW, 0.35);

            color = lerp(hist, curSample, curW);
            fog = lerp(histFog, curFogSample, curW);
            depthZ = lerp(histDepth, curDepthZ, curW);
        }
    }
    else
    {
        // No valid history: just show the current frame.
        color = curBilinear;
        fog = curFogBilinear;
        depthZ = inCloudDepthTexture.SampleLevel(linearClampEdgeSampler, uv, 0).r;
    }

    if (any(anyBadFloat(color)))
    {
        color = float4(0.0, 0.0, 0.0, 1.0);
    }
    if (any(anyBadFloat(fog)))
    {
        fog = float4(0.0, 0.0, 0.0, 1.0);
    }
    if (anyBadFloat(depthZ))
    {
        depthZ = 0.0;
    }

    imageCloudReconstructionTexture[workPos] = color;
    imageCloudFogReconstructionTexture[workPos] = fog;
    imageCloudDepthReconstructionTexture[workPos] = float4(depthZ, 0.0, 0.0, 0.0);
}
