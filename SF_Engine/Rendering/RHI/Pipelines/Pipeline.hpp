#pragma once

#include <Rendering/RHI/Commands/CommandBuffer.hpp>
#include <Rendering/RHI/Shaders/Shader.hpp>
#include <unordered_set>

namespace SF::Engine
{

    /**
     * @brief Class that is used to represent a pipeline.
     */
    class Pipeline
    {
    public:
        /**
         * @breif Represents position in the render structure, first value being the renderpass and second for subpass.
         */
        using Stage = std::pair<uint32_t, uint32_t>;

        Pipeline()          = default;
        virtual ~Pipeline() = default;

        void BindPipeline(const CommandBuffer &commandBuffer) const
        {
            vkCmdBindPipeline(commandBuffer, GetPipelineBindPoint(), GetPipeline());
        }

        [[nodiscard]] virtual const Shader *GetShader() const                             = 0;
        [[nodiscard]] virtual bool IsPushDescriptors() const                              = 0;
        [[nodiscard]] virtual const VkDescriptorSetLayout &GetDescriptorSetLayout() const = 0;
        [[nodiscard]] virtual const VkDescriptorPool &GetDescriptorPool() const           = 0;
        [[nodiscard]] virtual const VkPipeline &GetPipeline() const                       = 0;
        [[nodiscard]] virtual const VkPipelineLayout &GetPipelineLayout() const           = 0;
        [[nodiscard]] virtual const VkPipelineBindPoint &GetPipelineBindPoint() const     = 0;

        [[nodiscard]] bool HasBinding(uint32_t binding) const
        {
            return layoutBindings_.empty() || layoutBindings_.contains(binding);
        }

    protected:
        std::unordered_set<uint32_t> layoutBindings_;
    };
} // namespace SF::Engine
