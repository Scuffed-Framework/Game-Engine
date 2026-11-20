#include <Gui/ImGui/ocornut/imgui.h>

#include <Rendering/RHI/Descriptors/DescriptorSet.hpp>
#include <Rendering/RHI/Descriptors/DescriptorSetBuilder.hpp>
#include <Rendering/RenderSystem.hpp>
#include <Rendering/SharedFunctions.hpp>
#include <Rendering/SharedSamplers.hpp>
#include <Rendering/Visuals/Sky/Atmosphere/LUT/AtmoLUTs.hpp>
#include "CloudSFERenderpass.hpp"

namespace SF::Engine
{
    bool CloudSFERenderpass::isWindowOpen = true;

    CloudSFERenderpass::CloudSFERenderpass(const Pipeline::Stage &stage, const std::shared_ptr<AtmosphereData> &data) :
        EngineRenderpass(stage), data_(std::make_shared<AtmosphereData>(*data))
    {
        SetOrder(100);
        uiHandle = UIRegistry::Get().Register([this] { DrawImGuiPanel(); });

        atmoUBO_   = std::make_unique<UniformBuffer>(sizeof(AtmosphereFrameUBO));
        CloudData_ = std::make_unique<UniformBuffer>(sizeof(CloudData));

        cloudNoise_ = std::make_unique<CloudNoiseLUTs>(128, 128, 128);
        {
            CommandBuffer cmd(true);
            cloudNoise_->Bake(cmd);
            cmd.SubmitIdle();
        }

        auto fullRes = UVec2(GetScreenSize());
        UVec2 quarterRes{fullRes.x / 4, fullRes.y / 4};

        raymarchPipeline_    = std::make_unique<ComputePipeline>("Shaders/Clouds/RaymarchClouds.shader");
        reconstructPipeline_ = std::make_unique<ComputePipeline>("Shaders/Clouds/Reconstruct.shader");
        compositePipeline_   = std::make_unique<ComputePipeline>("Shaders/Clouds/Composite.shader");

        raymarchSet_ = std::make_unique<DescriptorSet>(*raymarchPipeline_);
        for (uint32_t i = 0; i < kFramesInFlight; ++i)
        {
            reconstructSet_[i] = std::make_unique<DescriptorSet>(*reconstructPipeline_);
            compositeSet_[i]   = std::make_unique<DescriptorSet>(*compositePipeline_);
        }

        // Create a 1x1 dummy texture for frame 0 history fallback
        dummyTexture_ = std::make_unique<Image2d>(UVec2{1, 1}, VK_FORMAT_R16G16B16A16_SFLOAT,
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        {
            CommandBuffer cmd(true);

            Image::InsertImageMemoryBarrier(cmd, dummyTexture_->GetImage(), 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1, 0);

            VkClearColorValue clearColor{};
            clearColor.float32[0] = 0.0f;
            clearColor.float32[1] = 0.0f;
            clearColor.float32[2] = 0.0f;
            clearColor.float32[3] = 1.0f;
            constexpr VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            vkCmdClearColorImage(cmd, dummyTexture_->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1,
                                 &range);

            Image::InsertImageMemoryBarrier(cmd, dummyTexture_->GetImage(), VK_ACCESS_TRANSFER_WRITE_BIT,
                                            VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1,
                                            0);

            cmd.SubmitIdle();
        }
        dummyTexture_->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Create images - constructor transitions UNDEFINED -> GENERAL
        cloudRenderRT_ = std::make_unique<Image2d>(quarterRes, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_GENERAL,
                                                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        cloudDepthRT_  = std::make_unique<Image2d>(quarterRes, VK_FORMAT_R32_SFLOAT, VK_IMAGE_LAYOUT_GENERAL,
                                                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        cloudFogRT_    = std::make_unique<Image2d>(quarterRes, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_GENERAL,
                                                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        for (uint32_t i = 0; i < kFramesInFlight; ++i)
        {
            // TRANSFER_DST: the history ring is cleared once below so every descriptor set can point at real history
            // from frame 0.
            constexpr VkImageUsageFlags kReconUsage =
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            reconColor_[i] = std::make_unique<Image2d>(fullRes, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_GENERAL,
                                                       kReconUsage);
            reconDepth_[i] =
                    std::make_unique<Image2d>(fullRes, VK_FORMAT_R32_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, kReconUsage);
            reconFog_[i] = std::make_unique<Image2d>(fullRes, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_GENERAL,
                                                     kReconUsage);
        }

        // Transition all images to SHADER_READ_ONLY_OPTIMAL before BindDescriptors()
        {
            CommandBuffer cmd(true);

            TransitionToReadOnly(cmd, cloudRenderRT_.get());
            TransitionToReadOnly(cmd, cloudDepthRT_.get());
            TransitionToReadOnly(cmd, cloudFogRT_.get());

            for (uint32_t i = 0; i < kFramesInFlight; ++i)
            {
                // History ring starts as "no cloud": colour/fog (0,0,0,1) = no light, transmittance 1; depth r = 0
                // (far). Cleared so every reconstruct descriptor set can point at real history from frame 0 (no dummy
                // swap).
                VkClearColorValue clearVal{};
                clearVal.float32[3] = 1.0f;
                constexpr VkImageSubresourceRange clearRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                for (Image2d *img: {reconColor_[i].get(), reconDepth_[i].get(), reconFog_[i].get()})
                {
                    vkCmdClearColorImage(cmd, img->GetImage(), VK_IMAGE_LAYOUT_GENERAL, &clearVal, 1, &clearRange);
                    Image::InsertImageMemoryBarrier(
                            cmd, img->GetImage(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1, 0);
                }
            }

            cmd.SubmitIdle();
        }

        // Update CPU-side layout tracking
        cloudRenderRT_->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cloudDepthRT_->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cloudFogRT_->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        for (uint32_t i = 0; i < kFramesInFlight; ++i)
        {
            reconColor_[i]->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            reconDepth_[i]->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            reconFog_[i]->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // Private composite target (premultiplied cloud colour). Render() blends it into "hdr".
        // Compositing straight onto "hdr" from PreRender is wiped by stage 1's render-pass clear.
        cloudCompositeRT_ = std::make_unique<Image2d>(fullRes, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_GENERAL,
                                                      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        {
            CommandBuffer cmd(true);
            Image::InsertImageMemoryBarrier(cmd, cloudCompositeRT_->GetImage(), 0, VK_ACCESS_SHADER_READ_BIT,
                                            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                            VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1, 0);
            cmd.SubmitIdle();
        }
        cloudCompositeRT_->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        blitPipeline_ = std::make_unique<RhiRenderPipeline>(
                stage, "Shaders/Clouds/CloudBlit.shader", std::vector<Shader::VertexInput>{},
                std::vector<Shader::Define>{}, RhiRenderPipeline::Mode::Polygon, RhiRenderPipeline::Depth::None,
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
                VK_FRONT_FACE_COUNTER_CLOCKWISE, false);
        blitSet_ = std::make_unique<DescriptorSet>(*blitPipeline_);
        DescriptorSetWriteBuilder(*blitSet_, *blitPipeline_)
                .CombinedImageSampler(1, cloudCompositeRT_->GetView(), cloudCompositeRT_->GetSampler(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                .Build()
                .Apply();

        // NOW call BindDescriptors() since images are already in SHADER_READ_ONLY_OPTIMAL
        BindDescriptors();

        isWindowOpen = true;
    }

    void CloudSFERenderpass::BindDescriptors() const
    {
        // --- Raymarch set ---
        const auto *apLut = AtmoLUTs::Get().GetAerialPerspectiveLUT();

        DescriptorSetWriteBuilder(*raymarchSet_, *raymarchPipeline_)
                .Image(2, cloudRenderRT_->GetView(), VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .Image(6, cloudNoise_->GetBaseTexture()->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                .Image(7, cloudNoise_->GetDetailTexture()->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                .Image(8, cloudNoise_->GetWeatherTexture()->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                .Image(9, cloudNoise_->GetCurlTexture()->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                // Atmosphere LUTs: were missing -> inTransmittanceLut/inSkyViewLut/inMultiScatterLUT unbound -> all
                // cloud lighting zero
                .Image(10, AtmoLUTs::Get().GetTransmittanceLUT()->GetTexture()->GetView(),
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                .CombinedImageSampler(20, AtmoLUTs::Get().GetSkyViewLUT()->GetTexture()->GetView(),
                                      SharedSamplers::GetLinearClampSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                .CombinedImageSampler(27, AtmoLUTs::Get().GetMultiScatterLUT()->GetTexture()->GetView(),
                                      SharedSamplers::GetLinearClampSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                // Aerial perspective froxel volume + range: the same three resources Atmosphere.shader samples for
                // geometry. Baked in AtmosphereSFERenderpass::PreRender (order 1) before this pass (order 100), and
                // left in SHADER_READ_ONLY_OPTIMAL by AerialPerspectiveLUT::Bake. Own samplers, as the atmosphere pass
                // does.
                .CombinedImageSampler(30, apLut->GetAerialPerspectiveColorRGBTransR()->GetView(),
                                      apLut->GetAerialPerspectiveColorRGBTransR()->GetSampler(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                .CombinedImageSampler(32, apLut->GetAerialPerspectiveTransGB()->GetView(),
                                      apLut->GetAerialPerspectiveTransGB()->GetSampler(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                .CombinedImageSampler(33, apLut->GetAerialPerspectiveRange()->GetView(),
                                      apLut->GetAerialPerspectiveRange()->GetSampler(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                .Image(14, cloudDepthRT_->GetView(), VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .Image(22, cloudFogRT_->GetView(), VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .Buffer(21, CloudData_->GetBuffer())
                .Buffer(29, atmoUBO_->GetBuffer())
                .Build()
                .Apply();

        BindSharedCameraData(31, 1, raymarchSet_.get(), raymarchPipeline_.get());

        for (uint32_t i = 0; i < kFramesInFlight; ++i)
        {
            // --- Reconstruct set: fully static. Slot i writes recon[i] and reads recon[i-1] as history, both fixed per
            // slot, and layouts are fixed too (write target GENERAL after the pre-dispatch barrier, history READ_ONLY).
            // Never re-write these per frame: without UPDATE_AFTER_BIND that is invalid while a submit using the set is
            // pending.
            const uint32_t hist = (i + kFramesInFlight - 1) % kFramesInFlight;

            DescriptorSetWriteBuilder(*reconstructSet_[i], *reconstructPipeline_)
                    .Image(3, cloudRenderRT_->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    .Image(15, cloudDepthRT_->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    .Image(23, cloudFogRT_->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    .Buffer(21, CloudData_->GetBuffer())
                    .Buffer(29, atmoUBO_->GetBuffer())
                    // Initialize write targets with GENERAL layout
                    .Image(12, reconColor_[i]->GetView(), VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                    .Image(16, reconDepth_[i]->GetView(), VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                    .Image(24, reconFog_[i]->GetView(), VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                    // History = previous slot's reconstruction (cleared in the ctor, so valid from frame 0)
                    .Image(18, reconColor_[hist]->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    .Image(19, reconDepth_[hist]->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    .Image(26, reconFog_[hist]->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    .Build()
                    .Apply();
            BindSharedCameraData(31, 1, reconstructSet_[i].get(), reconstructPipeline_.get());

            // --- Composite set: static too, except scene depth (binding 4), which PreRender rewrites only when the
            // attachment changes (after a vkDeviceWaitIdle). The simplified Composite.shader no longer reads the
            // transmittance/multiscatter LUTs, CloudData, AtmosphereFrameUBO or camera UBO; the builder skips bindings
            // that aren't in its layout.
            DescriptorSetWriteBuilder(*compositeSet_[i], *compositePipeline_)
                    .Image(0, cloudCompositeRT_->GetView(), VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                    .Image(13, reconColor_[i]->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    .Image(17, reconDepth_[i]->GetView(),
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) // cloud hit depth, for occluding geometry
                    .Image(25, reconFog_[i]->GetView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    .Build()
                    .Apply();
        }
    }

    void CloudSFERenderpass::SetFrameData(const Mat4 &invProj, const Mat4 &invView, const Vec3 &cameraPos,
                                          const Vec3 &planetPos, const Vec3 &sunDir, const Vec2 screenSize)
    {
        const Vec3 sd = length(sunDir) > 1e-6f ? normalize(sunDir) : Vec3(0.577f, 0.577f, 0.577f);
        cachedSunDir_ = sd;

        const float ruToM = data_->params.bottomRadius / data_->params.renderUnitRadius;
        const Vec3 pos    = (cameraPos - planetPos) * ruToM;

        data_->ubo.invProj          = invProj;
        data_->ubo.invView          = invView;
        data_->ubo.cameraPos        = Vec4(pos, 0.0f);
        data_->ubo.planetPos        = Vec4(0.0f);
        data_->ubo.sunDir           = Vec4(sd, data_->params.sunIntensity);
        data_->ubo.bottomRadius     = data_->params.bottomRadius;
        data_->ubo.topRadius        = data_->params.topRadius;
        data_->ubo.renderUnitRadius = data_->params.renderUnitRadius;
        data_->ubo.screenSize       = screenSize;
        data_->ubo.sunCol           = Vec4(1);
    }

    void CloudSFERenderpass::PreRender(const CommandBuffer &cmd)
    {
        if (!enabled)
            return;

        const auto *rs  = RenderSystem::Get();
        auto *depthDesc = rs->GetAttachment("gbuf_depth");
        auto *colorDesc = rs->GetAttachment("hdr");
        auto *depthImg  = dynamic_cast<const ImageDepth *>(depthDesc);
        auto *colorImg  = dynamic_cast<const Image2d *>(colorDesc);
        if (!depthImg || !colorImg)
            return;

        const uint32_t cur = frameSlot_ % kFramesInFlight;
        drawThisFrame_     = false;

        // Composite writes a private RT (never "hdr"): READ_ONLY -> GENERAL for the compute write.
        Image::InsertImageMemoryBarrier(cmd, cloudCompositeRT_->GetImage(), VK_ACCESS_SHADER_READ_BIT,
                                        VK_ACCESS_SHADER_WRITE_BIT, cloudCompositeRT_->GetLayout(),
                                        VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1, 0);
        cloudCompositeRT_->SetLayout(VK_IMAGE_LAYOUT_GENERAL);

        // Descriptor sets are written once in BindDescriptors(). Only the scene depth view can change (resize);
        // rewriting a set a pending submit still uses is invalid (no UPDATE_AFTER_BIND), so idle first and only on
        // change.
        {
            const uint64_t generation = RenderSystem::Get()->GetAttachmentGeneration();
            if (generation != lastAttachmentGeneration_ || depthImg != lastDepthImg_)
            {
                vkDeviceWaitIdle(RenderSystem::Get()->GetLogicalDevice()->GetLogicalDevice());
                for (const auto &i: compositeSet_)
                {
                    DescriptorSetWriteBuilder(*i, *compositePipeline_)
                            .Image(4, depthImg->GetView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
                            .Build()
                            .Apply();
                }
                // The raymarch clips against scene depth too (geometry inside/in front of the layer).
                DescriptorSetWriteBuilder(*raymarchSet_, *raymarchPipeline_)
                        .Image(4, depthImg->GetView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
                        .Build()
                        .Apply();
                lastAttachmentGeneration_ = generation;
                lastDepthImg_             = depthImg;
            }
        }

        totalTime_ += 0.016f;
        atmoUBO_->Update(data_->ubo);
        UpdateCloudData();

        auto qext = cloudRenderRT_->GetExtent();
        auto fext = colorImg->GetExtent();

        {
            Image2d *quarterImages[3] = {cloudRenderRT_.get(), cloudDepthRT_.get(), cloudFogRT_.get()};

            // Pre-write barriers: transition to GENERAL
            for (auto &quarterImage: quarterImages)
            {

                Image::InsertImageMemoryBarrier(
                        cmd, quarterImage->GetImage(), VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                        quarterImage->GetLayout(), VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1, 0);
                quarterImage->SetLayout(VK_IMAGE_LAYOUT_GENERAL);
            }

            // Dispatch raymarch
            raymarchPipeline_->BindPipeline(cmd);
            raymarchSet_->BindDescriptor(cmd);
            SharedSamplers::BindSharedSamplerSet(cmd, raymarchPipeline_->GetPipelineLayout(),
                                                 VK_PIPELINE_BIND_POINT_COMPUTE);
            raymarchPipeline_->Dispatch(cmd, UVec3(qext.x, qext.y, 1), {/*LOCAL_X=*/8, /*LOCAL_Y=*/8, /*LOCAL_Z=*/1});

            // Post-write barriers: transition to SHADER_READ_ONLY_OPTIMAL
            for (auto &quarterImage: quarterImages)
            {
                Image::InsertImageMemoryBarrier(
                        cmd, quarterImage->GetImage(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1, 0);
                quarterImage->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }

            // Global memory barrier to ensure writes are visible
            VkMemoryBarrier memoryBarrier{};
            memoryBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                                 &memoryBarrier, 0, nullptr, 0, nullptr);
        }

        {
            Image2d *reconTargets[3] = {reconColor_[cur].get(), reconDepth_[cur].get(), reconFog_[cur].get()};
            for (auto &reconTarget: reconTargets)
            {
                const VkImageLayout currentLayout = reconTarget->GetLayout();

                Image::InsertImageMemoryBarrier(
                        cmd, reconTarget->GetImage(), VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                        currentLayout, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1, 0);
                reconTarget->SetLayout(VK_IMAGE_LAYOUT_GENERAL);
            }
        }

        reconstructPipeline_->BindPipeline(cmd);
        reconstructSet_[cur]->BindDescriptor(cmd);
        SharedSamplers::BindSharedSamplerSet(cmd, reconstructPipeline_->GetPipelineLayout(),
                                             VK_PIPELINE_BIND_POINT_COMPUTE);
        reconstructPipeline_->Dispatch(cmd, UVec3(fext),
                                       {/*LOCAL_X=*/8, /*LOCAL_Y=*/8,
                                        /*LOCAL_Z=*/1});

        // Transition reconstruction outputs to SHADER_READ_ONLY_OPTIMAL
        {
            Image2d *reconImages[3] = {reconColor_[cur].get(), reconDepth_[cur].get(), reconFog_[cur].get()};
            for (auto &reconImage: reconImages)
            {
                /*
                Image::InsertImageMemoryBarrier(
                        cmd, reconImage->GetImage(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1, 0);
                        */

                // TransitionToReadOnly(cmd, reconImage);
                reconImage->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }

        compositePipeline_->BindPipeline(cmd);
        compositeSet_[cur]->BindDescriptor(cmd);
        SharedSamplers::BindSharedSamplerSet(cmd, compositePipeline_->GetPipelineLayout(),
                                             VK_PIPELINE_BIND_POINT_COMPUTE);
        {
            const auto cext = cloudCompositeRT_->GetExtent();
            compositePipeline_->Dispatch(cmd, UVec3(cext.x, cext.y, 1), {/*LOCAL_X=*/8, /*LOCAL_Y=*/8, /*LOCAL_Z=*/1});
        }

        // Composite RT: GENERAL -> READ_ONLY for Render()'s fragment-shader blit.
        Image::InsertImageMemoryBarrier(cmd, cloudCompositeRT_->GetImage(), VK_ACCESS_SHADER_WRITE_BIT,
                                        VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1, 0);
        cloudCompositeRT_->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        drawThisFrame_ = true;

        ++frameSlot_;
        ++framesSinceStart_;
    }

    void CloudSFERenderpass::Render(const CommandBuffer &cmd)
    {
        if (!enabled || !drawThisFrame_)
            return;

        blitPipeline_->BindPipeline(cmd);
        blitSet_->BindDescriptor(cmd);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    void CloudSFERenderpass::UpdateCloudData()
    {
        CloudData ubo{};

        ubo.cloudBottomRadius = data_->params.bottomRadius + minAlt;
        ubo.cloudTopRadius    = data_->params.bottomRadius + maxAlt;
        ubo.stepCount         = static_cast<float>(marchSteps);
        ubo.lightStepCount    = static_cast<float>(lightMarchSteps);

        ubo.cloudDensityScale   = densityScale;
        ubo.cloudCoverage       = coverage;
        ubo.cloudDetailScale    = cloudDetailScale;
        ubo.cloudCurlNoiseScale = cloudCurlNoiseScale;
        ubo.cloudBaseNoiseScale = cloudBaseNoiseScale;
        ubo.cloudWeatherUVScale = cloudWeatherUVScale;

        ubo.time   = totalTime_;
        ubo.Wind   = Wind;
        ubo.Speed  = Speed;
        ubo.unused = unused;

        ubo.msAttenuation      = msAttenuation;
        ubo.msContribution     = msContribution;
        ubo.msPhaseAttenuation = msPhaseAttenuation;
        ubo.msOctaves          = static_cast<float>(msOctaves);

        ubo.transitionStartAltitude = transitionStartAltitude;
        ubo.transitionEndAltitude   = transitionEndAltitude;
        ubo.twoDColumnSamples       = static_cast<float>(twoDColumnSamples);

        frameCounter_  = (frameCounter_ + 1) % 256;
        ubo.frameIndex = static_cast<int>(frameCounter_);

        CloudData_->Update(ubo);
    }

    void CloudSFERenderpass::DrawImGuiPanel()
    {
        if (!isWindowOpen)
            return;

        ImGui::Begin("Cloud Debug", &isWindowOpen);
        ImGui::Checkbox("Enabled", &enabled);
        ImGui::SliderFloat("Min Altitude", &minAlt, 1000.0f, 50000.0f);
        ImGui::SliderFloat("Max Altitude", &maxAlt, 1000.0f, 50000.0f);
        ImGui::SliderFloat("Density Scale", &densityScale, 0.0f, 10.0f);
        ImGui::SliderFloat("Coverage", &coverage, 0.0f, 1.0f);
        ImGui::SliderInt("March Steps", &marchSteps, 4, 128);
        ImGui::SliderInt("Light March Steps", &lightMarchSteps, 2, 32);
        ImGui::SliderFloat("Base Noise Scale", &cloudBaseNoiseScale, 0.1f, 10.0f);
        ImGui::SliderFloat("Detail Scale", &cloudDetailScale, 0.1f, 10.0f);
        ImGui::SliderFloat("Curl Noise Scale", &cloudCurlNoiseScale, 0.0f, 5.0f);
        ImGui::SliderFloat("Weather UV Scale", &cloudWeatherUVScale, 0.1f, 10.0f);
        ImGui::Separator();
        ImGui::TextUnformatted("Multiple scattering (Oz octaves)");
        ImGui::SliderInt("MS Octaves", &msOctaves, 1, 8);
        ImGui::SliderFloat("MS Attenuation (a)", &msAttenuation, 0.01f, 1.0f);
        ImGui::SliderFloat("MS Contribution (b)", &msContribution, 0.0f, 1.0f);
        ImGui::SliderFloat("MS Phase Atten (c)", &msPhaseAttenuation, 0.0f, 1.0f);
        ImGui::Separator();
        ImGui::TextUnformatted("2D <-> volumetric transition (altitude, m)");
        ImGui::SliderFloat("Transition Start", &transitionStartAltitude, 1000.0f, 100000.0f);
        ImGui::SliderFloat("Transition End", &transitionEndAltitude, 2000.0f, 200000.0f);
        ImGui::SliderInt("2D Column Samples", &twoDColumnSamples, 2, 16);
        ImGui::End();
    }

} // namespace SF::Engine
