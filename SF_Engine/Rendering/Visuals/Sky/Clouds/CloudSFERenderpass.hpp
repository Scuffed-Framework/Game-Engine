#pragma once

#include <Gui/ImGui/UIRegistry.hpp>
#include <Rendering/FrameGraph/EngineRenderpassManager.hpp>
#include <Rendering/LUT/AlligatorNoiseLUT.hpp>
#include <Rendering/LUT/BlueNoiseLUT.hpp>
#include <Rendering/RHI/Descriptors/DescriptorSet.hpp>
#include <Rendering/RHI/Memory/UniformBuffer.hpp>
#include <Rendering/RHI/Pipelines/ComputePipeline.hpp>
#include <Rendering/RHI/Pipelines/RhiRenderPipeline.hpp>
#include <Rendering/Visuals/Sky/Atmosphere/AtmosphereParams.hpp>
#include "../Atmosphere/LUT/AerialPerspectiveLUT.hpp"
#include "../Atmosphere/LUT/MultiScatterLUT.hpp"
#include "../Atmosphere/LUT/TransmittanceLUT.hpp"
#include "CloudNoise.hpp"

#include <Math/BasicMath.hpp>
#include <cstdint>
#include <memory>

namespace SF::Engine
{
    struct Std140 CloudData
    {
        float cloudBottomRadius;
        float cloudTopRadius;
        float stepCount;
        float lightStepCount;

        float cloudDensityScale;
        float cloudCoverage;

        float time;

        float sdfRangeMetres;
        int frameIndex;

        float cloudDetailScale;
        float cloudBaseNoiseScale;
        float cloudCurlNoiseScale;

        float cloudWeatherUVScale;
        float precipitationBias;
        float FadeDistance2d;
        float fadeSmoothDist;

        Vec2 Wind;
        float Speed;
        float unused;

        // Wrenninge "Oz" multiple-scattering octaves (see CloudCommon.si)
        float msAttenuation;      // a
        float msContribution;     // b
        float msPhaseAttenuation; // c
        float msOctaves;

        // 2D <-> volumetric crossfade (metres above sea level), 2D column sample count
        float transitionStartAltitude;
        float transitionEndAltitude;
        float twoDColumnSamples;
        float reserved0;
    };

    static_assert(sizeof(CloudData) == 112, "CloudData size mismatch - check cpu/gpu side");
    static_assert(sizeof(CloudData) % 16 == 0, "CloudData must satisfy std140 alignment");

    class CloudSFERenderpass : public EngineRenderpass
    {
    public:
        explicit CloudSFERenderpass(const Pipeline::Stage &stage, const std::shared_ptr<AtmosphereData> &data);
        ~CloudSFERenderpass() override = default;

        void SetFrameData(const Mat4 &invProj, const Mat4 &invView, const Vec3 &cameraPos, const Vec3 &planetPos,
                          const Vec3 &sunDir, Vec2 screenSize);

        void PreRender(const CommandBuffer &cmd) override;

        void Render(const CommandBuffer &) override;

        void DrawImGuiPanel();

        float minAlt = 1500.0f; // metres above planet surface
        float maxAlt = 6000.0f;

        int marchSteps      = 32; // primary ray
        int lightMarchSteps = 8;  // secondary (light accum compute)

        float densityScale        = 1.0f;
        float coverage            = 0.5;
        float cloudDetailScale    = 3.0f; // 1/km (posKm * scale): 333 m detail tile; see DetailNoise.shader for why it isn't finer
        float cloudBaseNoiseScale = 1.0f;
        float cloudCurlNoiseScale = 1.0f;

        float cloudWeatherUVScale = 0.020f; // 1/km: one weather tile = 50 km
        float precipitationBias   = 1.0f;
        float FadeDistance2d      = 1.0f;
        float fadeSmoothDist      = 1.0f;
        float time{};
        float sdfRangeMetres{};
        Vec2 Wind    = Vec2{0, 0};
        float Speed  = 0.0f;
        float unused = 0.0f;

        // Wrenninge "Oz" multiple scattering. Defaults are the reference snippet's values.
        // Energy conservation wants b <= a; b > a over-brightens thin regions (tune with the sliders).
        float msAttenuation      = 0.2f; // a: extinction falloff per octave
        float msContribution     = 0.4f; // b: energy per octave
        float msPhaseAttenuation = 0.5f; // c: eccentricity falloff per octave. 0.5 (was 0.1, tuned for HG g=0.3): the octave-0 phase is now
                                         // approximate-Mie with a very narrow forward peak, which must relax toward isotropic quickly
        int msOctaves            = 4;

        // Altitude crossfade between the volumetric march and the 2D (column-integrated) layer: volumetric only below
        // start, 2D only above end. The 2D layer is only drawn with the camera above maxAlt, so keep start >= maxAlt.
        float transitionStartAltitude = 7000.0f;
        float transitionEndAltitude   = 30000.0f;
        int twoDColumnSamples         = 6;

        static bool isWindowOpen;

    private:
        void BindDescriptors() const;
        void UpdateCloudData();

        std::shared_ptr<AtmosphereData> data_;
        std::unique_ptr<UniformBuffer> atmoUBO_;   // binding=0
        std::unique_ptr<UniformBuffer> CloudData_; // binding=1

        std::unique_ptr<CloudNoiseLUTs> cloudNoise_;

        std::unique_ptr<ComputePipeline> raymarchPipeline_;
        std::unique_ptr<ComputePipeline> reconstructPipeline_;
        std::unique_ptr<ComputePipeline> compositePipeline_;

        std::unique_ptr<DescriptorSet> raymarchSet_; // written once in BindDescriptors(), never re-written per frame

        static constexpr uint32_t kFramesInFlight = 3;
        std::unique_ptr<DescriptorSet> reconstructSet_[kFramesInFlight];
        std::unique_ptr<DescriptorSet> compositeSet_[kFramesInFlight];

        Vec3 cachedSunDir_{0.577f, 0.577f, 0.577f};
        float totalTime_{0.0f};

        const Image2d *lastColorImg_    = nullptr;
        const ImageDepth *lastDepthImg_ = nullptr;

        uint64_t lastAttachmentGeneration_ = 0;
        uint32_t frameCounter_             = 0;

        std::unique_ptr<Image2d> cloudRenderRT_; // binding 2/3  rgba16f
        std::unique_ptr<Image2d> cloudDepthRT_;  // binding 14/15 r32f
        std::unique_ptr<Image2d> cloudFogRT_;    // binding 22/23 rgba16f

        std::unique_ptr<Image2d> reconColor_[kFramesInFlight]; // binding 12(write)/13(read)/18(history)
        std::unique_ptr<Image2d> reconDepth_[kFramesInFlight]; // binding 16(write)/17(read)/19(history)
        std::unique_ptr<Image2d> reconFog_[kFramesInFlight];   // binding 24(write)/25(read)/26(history)

        uint32_t framesSinceStart_ = 0;
        uint32_t frameSlot_        = 0; // monotonically increasing, index with % kFramesInFlight
        bool firstFrame_           = true;
        // fahh
        std::unique_ptr<Image2d> dummyTexture_;

        // Composite compute writes premultiplied cloud colour here; Render() blends it into "hdr".
        // (Writing "hdr" from PreRender gets wiped by stage 1's render-pass clear.)
        std::unique_ptr<Image2d> cloudCompositeRT_;
        std::unique_ptr<RhiRenderPipeline> blitPipeline_;
        std::unique_ptr<DescriptorSet> blitSet_;
        bool drawThisFrame_ = false;
        size_t uiHandle;
    };

} // namespace SF::Engine
