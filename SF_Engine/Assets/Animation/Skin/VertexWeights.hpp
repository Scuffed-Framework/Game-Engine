#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace SF::Engine
{
    using namespace std;
    class VertexWeights
    {
    public:
        // Inserts keeping the list sorted by descending weight.
        void AddJointEffect(uint32_t jointId, float jointWeight);
        void LimitJointNumber(uint32_t max);
        void FillEmptyWeights(uint32_t max);
        float SaveTopWeights(std::vector<float> &topWeightsArray) const;
        void RefillWeightList(const std::vector<float> &topWeights, float total);
        void RemoveExcessJointIds(uint32_t max);

        // Maps joint ids from one joint ordering to another (e.g. skin joint order -> Skeleton index).
        // Ids outside the table are left untouched.
        void RemapJointIds(std::span<const uint32_t> table);

        const std::vector<uint32_t> &GetJointIds() const { return jointIds; }
        const std::vector<float> &GetWeights() const { return weights; }

    private:
        std::vector<uint32_t> jointIds;
        std::vector<float> weights;
    };
} // namespace SF::Engine
