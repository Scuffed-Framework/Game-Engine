#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "VertexWeights.hpp"

namespace SF::Engine
{
    class Skeleton;

    // Per-vertex joint influences plus the joint names those influences refer to.
    // VertexWeights joint ids always index into jointOrder.
    class Skin
    {
    public:
        Skin() = default;

        Skin(std::vector<std::string> jointOrder, std::vector<VertexWeights> vertexWeights) :
            jointOrder(std::move(jointOrder)), vertexWeights(std::move(vertexWeights))
        {
        }

        const std::vector<std::string> &GetJointOrder() const { return jointOrder; }
        const std::vector<VertexWeights> &GetVertexWeights() const { return vertexWeights; }

        // Re-targets the skin at a skeleton: joint ids become skeleton indices (matched by name)
        // and jointOrder becomes the skeleton's joint order. Returns false if a joint name was not found
        // (those influences are pointed at joint 0).
        bool BindToSkeleton(const Skeleton &skeleton);

        // Apply the old->new remap returned by Skeleton::Reparent.
        void ApplyJointRemap(std::span<const std::uint32_t> oldToNew);

    private:
        std::vector<std::string> jointOrder;
        std::vector<VertexWeights> vertexWeights;
    };
} // namespace SF::Engine
