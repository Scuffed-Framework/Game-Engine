#include "BlueNoiseLUT.hpp"
#include <Engine/Log/Log.hpp>
#include <Rendering/RenderSystem.hpp>

namespace SF::Engine
{
    BlueNoiseLUT::BlueNoiseLUT(uint32_t size) : size_(size)
    {
        texture_ = std::make_unique<Image2d>(UVec2{size_, size_}, VK_FORMAT_R16_SFLOAT, VK_IMAGE_LAYOUT_GENERAL,
                                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FILTER_LINEAR,
                                             VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLE_COUNT_1_BIT, false, false);
        createPipeline();
    }

    void BlueNoiseLUT::createPipeline()
    {
        pipeline_ = std::make_unique<ComputePipeline>("Shaders/Noise/BlueNoiseLUT.shader");
        descSet_  = std::make_unique<DescriptorSet>(*pipeline_);
        BindStorageImage(*descSet_, texture_.get());
    }

    void BlueNoiseLUT::Bake(const CommandBuffer &cmd)
    {
        if (baked_)
            TransitionToGeneral(cmd, texture_.get());

        pipeline_->BindPipeline(cmd);
        descSet_->BindDescriptor(cmd);
        vkCmdDispatch(cmd, (size_ + 7) / 8, (size_ + 7) / 8, 1);

        TransitionToReadOnly(cmd, texture_.get());
        texture_->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        baked_ = true;
    }

    PerlinWorleyNoiseLUT::PerlinWorleyNoiseLUT(uint32_t size) : size_(size)
    {
        texture_ =
                std::make_unique<Image3d>(UVec3{size_, size_, size_}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL,
                                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_FILTER_LINEAR,
                                          VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLE_COUNT_1_BIT, false, false);
        createPipeline();
    }

    void PerlinWorleyNoiseLUT::createPipeline()
    {
        pipeline_ = std::make_unique<ComputePipeline>("Shaders/Noise/PerlinWorleyNoiseLUT.shader");
        descSet_  = std::make_unique<DescriptorSet>(*pipeline_);
        BindStorageImage(*descSet_, texture_.get());
    }

    void PerlinWorleyNoiseLUT::Bake(const CommandBuffer &cmd)
    {
        if (baked_)
            TransitionToGeneral3d(cmd, texture_.get());

        pipeline_->BindPipeline(cmd);
        descSet_->BindDescriptor(cmd);
        const uint32_t g = (size_ + 3) / 4;
        vkCmdDispatch(cmd, g, g, g);

        TransitionToReadOnly3d(cmd, texture_.get());
        texture_->SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        baked_ = true;
    }
} // namespace SF::Engine
