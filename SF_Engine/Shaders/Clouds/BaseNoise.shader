// Tileable Perlin-Worley base shape noise (Schneider, GPU Pro 7), baked once.
// Uses Noise/PerlinWorleyNoise.si: worley/gradient noise there wrap on an integer period and the worley is INVERTED (1 - d^2), so
// billows are bright blobs. The old WorleyNoise.si/PerlinNoise.si version was neither tileable (a visible seam every texture tile,
// i.e. every 1/cloudBaseNoiseScale km) nor inverted.
#include "Noise/PerlinWorleyNoise.si"

#define kBasicFrequency 4.0 // cells per tile; must be an integer so the volume wraps

[[vk::binding(0, 0)]]
[[vk::image_format("r8")]]
RWTexture3D<float4> imageBasicNoise;

float basicNoiseComposite(float4 v)
{
    float wfbm = v.y * 0.625 + v.z * 0.25 + v.w * 0.125;
    return remap(v.x, wfbm - 1.0, 1.0, 0.0, 1.0);
}

[numthreads(8, 8, 1)]
void main(uint3 globalThreadID : SV_DispatchThreadID)
{
    int3 texSize;
    imageBasicNoise.GetDimensions(texSize.x, texSize.y, texSize.z);
    int3 workPos = int3(globalThreadID.xyz);

    if (workPos.x >= texSize.x || workPos.y >= texSize.y || workPos.z >= texSize.z)
    {
        return;
    }

    const float3 uvw = (float3(workPos) + float3(0.5f)) / float3(texSize);

    // 5 octaves: octave k has kBasicFrequency * 2^k cells, 64 at k=4, which is the Nyquist limit for a 128^3 volume
    float pfbm = lerp(1.0, perlinfbm(uvw, kBasicFrequency, 5), 0.5);
    pfbm = abs(pfbm * 2.0 - 1.0); // billowy perlin noise

    float4 col = float4(0.0);
    col.g = worleyFbm(uvw, kBasicFrequency);
    col.b = worleyFbm(uvw, kBasicFrequency * 2.0);
    col.a = worleyFbm(uvw, kBasicFrequency * 4.0);

    col.r = remap(pfbm, 0.0, 1.0, col.g, 1.0); // perlin-worley

    imageBasicNoise[workPos] = float4(basicNoiseComposite(col));
}
