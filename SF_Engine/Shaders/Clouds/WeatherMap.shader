// R = coverage, G = cloud height/thickness, B = cloud type (0=stratus, 1=cumulus)
#include "Noise/PerlinWorleyNoise.si"
[[vk::binding(0, 0)]] [[vk::image_format("rgba8")]]
RWTexture2D<float4> outWeatherMap;

cbuffer WeatherParams : register(b1)
{
    uint  resolution;      // e.g. 512-1024; one tile of this map covers 1 / cloudWeatherUVScale km
    float coverageScale;   // number of coverage cells across the tile (rounded to an integer so it tiles)
    float heightScale;     // cells across the tile for height
    float typeScale;       // cells across the tile for type (min 1)
    float coverageBias;    // shifts overall coverage up/down
};

// Tileable 3-octave inverted-Worley FBM. `cells` must be an integer >= 1 so the pattern
// wraps seamlessly across the texture. Raw output is NOT [0,1]: inverted squared-distance
// Worley clusters around ~0.55..0.9, so callers must contrast-stretch (see stretch()).
float worleyFbm2D(float2 uv, float cells, float z)
{
    float c = max(1.0, round(cells));
    return worleyNoise(float3(uv * c, z), c) * 0.625 +
           worleyNoise(float3(uv * c * 2.0, z), c * 2.0) * 0.25 +
           worleyNoise(float3(uv * c * 4.0, z), c * 4.0) * 0.125;
}

// Map the compressed raw range to a usable 0..1 spread (mean ~0.5), then bias.
float stretch(float raw, float bias)
{
    return saturate((raw - 0.55) / 0.30 + bias);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= resolution)) return;

    float2 uv = (float2(id.xy) + 0.5) / float(resolution);

    // R: coverage. Sharpen so there are real gaps and real dense clusters.
    float coverage = stretch(worleyFbm2D(uv, coverageScale, 0.5), coverageBias);
    coverage = pow(coverage, 1.4);

    // G: height, lower frequency than coverage.
    float height = stretch(worleyFbm2D(uv, heightScale, 0.25), 0.0);

    // B: type, lowest frequency. >0.5 selects the cumulus-leaning gradient.
    float type = stretch(worleyFbm2D(uv, typeScale, 0.75), 0.0);

    outWeatherMap[id.xy] = float4(coverage, height, type, 1.0);
}
