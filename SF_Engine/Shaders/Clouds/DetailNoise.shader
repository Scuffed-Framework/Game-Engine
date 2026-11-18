// Tileable Worley detail (erosion) noise, baked once.
// The raw sum of inverted worley octaves only spans roughly 0.5..0.85, so as stored it had almost no contrast and the erosion in
// cloudMap() was effectively a constant offset ("detail noise isn't doing anything"). It is stretched to the full 0..1 range here.
#include "Noise/PerlinWorleyNoise.si"

// Cells per tile for the three bands (integers so the volume wraps). worleyFbm() adds two more octaves on top of each band.
// Kept coarse on purpose: the view march steps 80-300 m, so features finer than that are undersampled and show up as per-pixel
// noise instead of wisps. At cloudDetailScale 3/km (333 m tile) these are ~170 m, 83 m and 42 m cells.
#define kDetailFreq0 2.0
#define kDetailFreq1 4.0
#define kDetailFreq2 8.0

// Expected raw range of the weighted sum; tune if the texture looks washed out or clipped (view it in RenderDoc).
#define kDetailRawLo 0.50
#define kDetailRawHi 0.85

[[vk::binding(0, 0)]]
[[vk::image_format("r8")]]
RWTexture3D<float> imageWorleyNoise;

[numthreads(8, 8, 1)]
void main(uint3 globalThreadID : SV_DispatchThreadID)
{
    int3 texSize;
    imageWorleyNoise.GetDimensions(texSize.x, texSize.y, texSize.z);
    int3 workPos = int3(globalThreadID.xyz);

    if (workPos.x >= texSize.x || workPos.y >= texSize.y || workPos.z >= texSize.z)
    {
        return;
    }

    const float3 uvw = (float3(workPos) + float3(0.5f)) / float3(texSize);

    float detailNoise =
        worleyFbm(uvw, kDetailFreq0) * 0.625 +
        worleyFbm(uvw, kDetailFreq1) * 0.250 +
        worleyFbm(uvw, kDetailFreq2) * 0.125;

    imageWorleyNoise[workPos] = saturate((detailNoise - kDetailRawLo) / (kDetailRawHi - kDetailRawLo));
}
