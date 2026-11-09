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
    using namespace std;
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
        float percipitationBias;
        float FadeDistance2d;
        float fadeSmoothDist;

        Vec2 Wind;
        float Speed;
        float unused;
    };

    static_assert(sizeof(CloudData) == 80, "CloudData size mismatch - check cpu/gpu side");
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
        float cloudDetailScale    = 0.00025f;
        float cloudBaseNoiseScale = 1.0f;
        float cloudCurlNoiseScale = 1.0f;

        float cloudWeatherUVScale = 0.020f; // 1/km: one weather tile = 50 km
        float percipitationBias   = 1.0f;
        float FadeDistance2d      = 1.0f;
        float fadeSmoothDist      = 1.0f;
        float time{};
        float sdfRangeMetres{};
        Vec2 Wind    = Vec2{0, 0};
        float Speed  = 0.0f;
        float unused = 0.0f;

        static bool isWindowOpen;

    private:
        void BindDescriptors() const;
        void UpdateCloudData();

        shared_ptr<AtmosphereData> data_;
        unique_ptr<UniformBuffer> atmoUBO_;   // binding=0
        unique_ptr<UniformBuffer> CloudData_; // binding=1

        unique_ptr<CloudNoiseLUTs> cloudNoise_;

        unique_ptr<ComputePipeline> raymarchPipeline_;
        unique_ptr<ComputePipeline> reconstructPipeline_;
        unique_ptr<ComputePipeline> compositePipeline_;

        unique_ptr<DescriptorSet> raymarchSet_; // written once in BindDescriptors(), never re-written per frame

        static constexpr uint32_t kFramesInFlight = 3;
        unique_ptr<DescriptorSet> reconstructSet_[kFramesInFlight];
        unique_ptr<DescriptorSet> compositeSet_[kFramesInFlight];

        Vec3 cachedSunDir_{0.577f, 0.577f, 0.577f};
        float totalTime_{0.0f};

        const Image2d *lastColorImg_    = nullptr;
        const ImageDepth *lastDepthImg_ = nullptr;

        uint64_t lastAttachmentGeneration_ = 0;
        uint32_t frameCounter_             = 0;

        unique_ptr<Image2d> cloudRenderRT_; // binding 2/3  rgba16f
        unique_ptr<Image2d> cloudDepthRT_;  // binding 14/15 r32f
        unique_ptr<Image2d> cloudFogRT_;    // binding 22/23 rgba16f

        unique_ptr<Image2d> reconColor_[kFramesInFlight]; // binding 12(write)/13(read)/18(history)
        unique_ptr<Image2d> reconDepth_[kFramesInFlight]; // binding 16(write)/17(read)/19(history)
        unique_ptr<Image2d> reconFog_[kFramesInFlight];   // binding 24(write)/25(read)/26(history)

        uint32_t framesSinceStart_ = 0;
        uint32_t frameSlot_        = 0; // monotonically increasing, index with % kFramesInFlight
        bool firstFrame_           = true;
        // fahh
        unique_ptr<Image2d> dummyTexture_;

        // Composite compute writes premultiplied cloud colour here; Render() blends it into "hdr".
        // (Writing "hdr" from PreRender gets wiped by stage 1's render-pass clear.)
        unique_ptr<Image2d> cloudCompositeRT_;
        unique_ptr<RhiRenderPipeline> blitPipeline_;
        unique_ptr<DescriptorSet> blitSet_;
        bool drawThisFrame_ = false;
        size_t uiHandle;
    };

} // namespace SF::Engine
