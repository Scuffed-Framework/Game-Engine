// CLOUD_DEBUG: 0 = normal.
// 9 = QUADRANT view (one run tells you everything):
//   top-left     : ray direction (rgb = dir*0.5+0.5)           -> black = dispatch/bindings/atmo UBO broken (NaN)
//   top-right    : weather texture rgb                         -> black = weather tex unbound/empty
//   bottom-left  : base noise slice (greyscale)                -> black = 3D noise unbound/empty
//   bottom-right : R = cloudMap density*50 at mid layer, G = coverage, B = densityScale/10
//                  (R=1,G=0,B=1 solid magenta = ray misses mid shell)
// 1..6 = single full-screen views of the same things (6 = camera/radii sanity).
#define CLOUD_DEBUG 0
#include "Clouds/CloudCommon.si"
#include "Noise/BlueNoise.si"

// Evaluate quarter resolution.
[shader("compute")]
[numthreads(8,8,1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 texSize;
    imageCloudRenderTexture.GetDimensions(texSize.x, texSize.y);
    int2 workPos = int2(dispatchThreadID.xy);

    if(workPos.x >= texSize.x || workPos.y >= texSize.y)
    {
        return;
    }

    uint bayerIndex = kCloud.frameIndex.x % 16;
    int bayerValue = kBayer4x4[bayerIndex];
    int2 bayerOffset = int2(bayerValue % 4, bayerValue / 4);

    int2 fullResSize = texSize * 4;
    int2 fullResWorkPos = workPos * 4 + int2(bayerOffset);

    // Get evaluate uv in full resolution.
    const float2 uv = (float2(fullResWorkPos) + float2(0.5f)) / float2(fullResSize);

    float blueNoise = BlueNoiseErrorDistrib(workPos.x, workPos.y, 0, 0u);

    // Offset retarget for new seeds each frame
    uint2 offset = uint2(float2(0.754877669, 0.569840296) * (kCloud.frameIndex.x) * uint2(texSize));
    uint2 offsetId = workPos.xy + offset;
    offsetId.x = offsetId.x % texSize.x;
    offsetId.y = offsetId.y % texSize.y;
    float blueNoise2 = BlueNoiseErrorDistrib(offsetId.x, offsetId.y, 0, 0u);

    // Same unprojection as Composite.shader (reverse-z far plane).
    float4 clipSpace = float4(uv.x * 2.0f - 1.0f, uv.y * 2.0f - 1.0f, 0.5, 1.0); // z=0.5: z=0 is w=0 (infinite far) with reverse-z -> NaN
    float4 viewPosH = mul(kAtmo.invProj, clipSpace);
    float3 viewSpaceDir = normalize(viewPosH.xyz / viewPosH.w);
    float3 rayDir = normalize(mul(kAtmo.invView, float4(viewSpaceDir, 0.0)).xyz);

#if CLOUD_DEBUG != 0
    {
        float3 camP = kAtmo.cameraPos.xyz;

        // --- candidate views ---
        float3 vRay = rayDir * 0.5 + 0.5;
        float3 vWeather = inWeatherTexture.SampleLevel(linearRepeatSampler, uv, 0).rgb;
        float3 vNoise = inBasicNoise.SampleLevel(linearRepeatSampler, float3(uv, 0.5), 0).xxx;

        float midR = 0.5 * (kCloud.cloudBottomRadius + kCloud.cloudTopRadius);
        float tMid = raySphereIntersectInside(camP, rayDir, float3(0.0, 0.0, 0.0), midR);
        float3 vDensity;
        if (tMid < 0.0)
            vDensity = float3(1.0, 0.0, 1.0);
        else
            vDensity = float3(cloudMap(camP + rayDir * tMid, 0.5) * 50.0, kCloud.cloudCoverage, kCloud.cloudDensityScale / 10.0);

        float3 vUbo = float3(kCloud.cloudCoverage, kCloud.cloudDensityScale, kCloud.stepCount / 64.0);
        float3 vCam = float3(camP.y / 6.4e6, length(camP) / 6.4e6, kCloud.cloudBottomRadius / 6.4e6);

        float3 dbg;
#if CLOUD_DEBUG == 1
        dbg = vRay;
#elif CLOUD_DEBUG == 2
        dbg = vWeather;
#elif CLOUD_DEBUG == 3
        dbg = vNoise;
#elif CLOUD_DEBUG == 4
        dbg = vDensity;
#elif CLOUD_DEBUG == 5
        dbg = vUbo;
#elif CLOUD_DEBUG == 6
        dbg = vCam;
#else
        bool right = uv.x >= 0.5;
        bool bottom = uv.y >= 0.5;
        if (!bottom && !right) dbg = vRay;
        else if (!bottom && right) dbg = vWeather;
        else if (bottom && !right) dbg = vNoise;
        else dbg = vDensity;
#endif
        // Alpha = 1 so "black" can only mean "value is zero/NaN", never "transparent".
        imageCloudRenderTexture[workPos] = float4(dbg, 1.0);
        imageCloudDepthTexture[workPos] = float4(0.0, 0.0, 0.0, 0.0);
        imageCloudFogRenderTexture[workPos] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
#endif

    float depth = 0.0; // reverse z.
    float4 fogLighting = float4(0.0, 0.0, 0.0, 0.0);

    float4 cloudColor = cloudColorCompute(kCloud, kAtmo, uv, blueNoise2, depth, workPos, rayDir, true, fogLighting, blueNoise2);

    if (anyBadFloat(depth))
    {
        depth = 0.0;
    }

    imageCloudRenderTexture[workPos] = cloudColor;
    imageCloudDepthTexture[workPos] = float4(depth, 0.0, 0.0, 0.0);
    imageCloudFogRenderTexture[workPos] = fogLighting;
}
