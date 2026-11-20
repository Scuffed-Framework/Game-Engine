#include "AnimSkin.hpp"

#include "../Skeleton/AnimSkeleton.hpp"

#include <Engine/Log/Log.hpp>

namespace SF::Engine
{
    bool Skin::BindToSkeleton(const Skeleton &skeleton)
    {
        bool allFound = true;
        std::vector toSkeleton(jointOrder.size(), 0u);

        for (std::size_t i = 0; i < jointOrder.size(); ++i)
        {
            if (const auto found = skeleton.Find(jointOrder[i]))
                toSkeleton[i] = *found;
            else
            {
                Log::Error("Skin joint '{}' not found in skeleton.", jointOrder[i]);
                allFound = false;
            }
        }

        for (auto &vertex: vertexWeights)
            vertex.RemapJointIds(toSkeleton);

        jointOrder.clear();
        jointOrder.reserve(skeleton.Count());

        for (const auto &joint: skeleton.GetJoints())
            jointOrder.push_back(joint.GetName());

        return allFound;
    }

    void Skin::ApplyJointRemap(const std::span<const std::uint32_t> oldToNew)
    {
        for (auto &vertex: vertexWeights)
            vertex.RemapJointIds(oldToNew);

        if (oldToNew.size() != jointOrder.size())
            return;

        std::vector<std::string> reordered(jointOrder.size());

        for (std::size_t i = 0; i < jointOrder.size(); ++i)
            reordered[oldToNew[i]] = std::move(jointOrder[i]);

        jointOrder = std::move(reordered);
    }
} // namespace SF::Engine
