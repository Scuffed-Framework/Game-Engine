#pragma once

#include <Engine/Log/Log.hpp>
#include <UtilityClasses/Patterns.hpp>
#include <deque>
#include <vector>
#include "DescriptorSet.hpp"

namespace SF::Engine
{
    struct DescriptorSetWrites
    {
        std::vector<VkWriteDescriptorSet> writes;
        std::deque<VkDescriptorBufferInfo> bufferInfos; // deque: stable addrs on grow + on move
        std::deque<VkDescriptorImageInfo> imageInfos;

        void Apply() const
        {
            if (!writes.empty())
                DescriptorSet::Update(writes);
        }
    };


    struct SamplerDescriptorType
    {
        VkDescriptorType Combined    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        VkDescriptorType JustSampler = VK_DESCRIPTOR_TYPE_SAMPLER;
    };

    class DescriptorSetWriteBuilder : public BuilderPattern<DescriptorSetWriteBuilder, DescriptorSetWrites>
    {
    public:
        // Bind by reference: DescriptorSet is non-copyable/non-movable, and the builder
        // only needs the handle, not ownership. Caller must keep dstSet alive for the
        // builder's lifetime (true of any code that would've done this by hand too).
        explicit DescriptorSetWriteBuilder(const DescriptorSet &dstSet) noexcept : m_DstSet(dstSet) {}
        explicit DescriptorSetWriteBuilder(const DescriptorSet &dstSet, const Pipeline &pipeline) noexcept :
            m_Pipeline(&pipeline), m_DstSet(dstSet)
        {
        }

        DescriptorSetWriteBuilder &Buffer(uint32_t binding, VkBuffer buffer,
                                          VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                          VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE)
        {
            const auto &info = m_Product.bufferInfos.emplace_back(VkDescriptorBufferInfo{buffer, offset, range});

            VkWriteDescriptorSet w{};
            w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet          = m_DstSet.GetDescriptorSet();
            w.dstBinding      = binding;
            w.descriptorCount = 1;
            w.descriptorType  = type;
            w.pBufferInfo     = &info;
            Push(w);
            return Self();
        }

        // Sampled/storage image (no sampler).
        DescriptorSetWriteBuilder &Image(uint32_t binding, VkImageView view, VkImageLayout layout,
                                         VkDescriptorType type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
        {
            return ImageImpl(binding, view, layout, VK_NULL_HANDLE, type);
        }

        DescriptorSetWriteBuilder &CombinedImageSampler(uint32_t binding, VkImageView view, VkSampler sampler,
                                                        VkImageLayout layout)
        {
            return ImageImpl(binding, view, layout, sampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }

        DescriptorSetWriteBuilder &Sampler(uint32_t binding, VkImageView view, VkSampler sampler, VkImageLayout layout)
        {
            return ImageImpl(binding, view, layout, sampler, VK_DESCRIPTOR_TYPE_SAMPLER);
        }

        // Escape hatch for bindings that don't fit the helpers above. dstSet/sType are
        // stamped in for you; fill in the rest (binding, type, pBufferInfo/pImageInfo,
        // and if it points at an info struct, make sure that struct outlives Apply()).
        DescriptorSetWriteBuilder &Custom(VkWriteDescriptorSet w)
        {
            w.sType  = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = m_DstSet.GetDescriptorSet();
            Push(w);
            return Self();
        }

    private:
        DescriptorSetWriteBuilder &ImageImpl(uint32_t binding, VkImageView view, VkImageLayout layout,
                                             VkSampler sampler, VkDescriptorType type)
        {
            const auto &info = m_Product.imageInfos.emplace_back(VkDescriptorImageInfo{sampler, view, layout});

            VkWriteDescriptorSet w{};
            w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet          = m_DstSet.GetDescriptorSet();
            w.dstBinding      = binding;
            w.descriptorCount = 1;
            w.descriptorType  = type;
            w.pImageInfo      = &info;
            Push(w);
            return Self();
        }

        void Push(const VkWriteDescriptorSet &w)
        {
            if (m_Pipeline && !m_Pipeline->HasBinding(w.dstBinding))
            {
                // swap for your logger; add once-per-(pipeline,binding) dedupe if it spams
                Log::Warning("Skipping write to binding {}: not in pipeline layout", w.dstBinding);
                return;
            }
            m_Product.writes.push_back(w);
        }

        const Pipeline *m_Pipeline = nullptr;

        const DescriptorSet &m_DstSet;
    };
} // namespace SF::Engine
