#include "VertexWeights.hpp"

#include <algorithm>

namespace SF::Engine
{
    void VertexWeights::AddJointEffect(const uint32_t jointId, const float jointWeight)
    {
        // Keep both arrays sorted by descending weight so LimitJointNumber can simply drop the tail.
        const auto it  = std::ranges::find_if(weights, [&](const float w) { return jointWeight > w; });
        const auto idx = std::distance(weights.begin(), it);

        weights.insert(weights.begin() + idx, jointWeight);
        jointIds.insert(jointIds.begin() + idx, jointId);
    }

    void VertexWeights::LimitJointNumber(const uint32_t max)
    {
        if (jointIds.size() > max)
        {
            std::vector<float> topWeights(max);
            const auto total = SaveTopWeights(topWeights);
            RefillWeightList(topWeights, total);
            RemoveExcessJointIds(max);
        } else if (jointIds.size() < max)
        {
            FillEmptyWeights(max);
        }
    }

    void VertexWeights::FillEmptyWeights(const uint32_t max)
    {
        while (jointIds.size() < max)
        {
            jointIds.emplace_back(0);
            weights.emplace_back(0.0f);
        }
    }

    float VertexWeights::SaveTopWeights(std::vector<float> &topWeightsArray) const
    {
        float total = 0.0f;

        for (std::size_t i = 0; i < topWeightsArray.size(); ++i)
        {
            topWeightsArray[i] = weights.at(i);
            total += topWeightsArray[i];
        }

        return total;
    }

    void VertexWeights::RefillWeightList(const std::vector<float> &topWeights, const float total)
    {
        weights.clear();

        // All top weights zero: fall back to full influence on the first joint instead of dividing by zero.
        if (total <= 0.0f)
        {
            for (std::size_t i = 0; i < topWeights.size(); ++i)
                weights.emplace_back(i == 0 ? 1.0f : 0.0f);
            return;
        }

        for (const auto &topWeight: topWeights)
            weights.emplace_back(topWeight / total);
    }

    void VertexWeights::RemoveExcessJointIds(const uint32_t max)
    {
        while (jointIds.size() > max)
            jointIds.pop_back();
    }

    void VertexWeights::RemapJointIds(const std::span<const uint32_t> table)
    {
        for (auto &id: jointIds)
        {
            if (id < table.size())
                id = table[id];
        }
    }
} // namespace SF::Engine