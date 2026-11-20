#pragma once
#include <Math/Transform.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <Assets/Animation/Joint/AnimJoint.hpp>

namespace SF::Engine
{
    // Flat joint array. Invariant: a joint's parent always has a LOWER index than the joint itself,
    // so forward kinematics is one linear pass with no recursion and no per-joint heap allocation.
    //
    // Bind data (global bind + inverse bind) is derived from the joints' local bind transforms.
    // It is marked dirty by AddJoint and rebuilt by RebuildInverseBind(). If you edit joints through
    // GetJoints()/GetJoint() (e.g. SetLocalBindTransform) call RebuildInverseBind() afterward.

    // TODO: Bones, head joint
    class Skeleton
    {
    public:
        static constexpr std::uint32_t NoParent = 0xFFFFFFFFu;

        // Parent must already exist (or be NoParent), which keeps the parent-first invariant.
        std::uint32_t AddJoint(std::string name, std::uint32_t parent, const Transform &bindLocal);
        void Clear();

        // Recomputes global bind, inverse bind (and the copy stored on each Joint) from local bind transforms.
        void RebuildInverseBind();

        [[nodiscard]] std::uint32_t Count() const { return static_cast<std::uint32_t>(joints.size()); }
        [[nodiscard]] bool Empty() const { return joints.empty(); }
        [[nodiscard]] bool HasBindData() const { return !bindDirty && inverseBindTransforms.size() == joints.size(); }

        // Checks index == position and parent < index for every joint.
        [[nodiscard]] bool IsValid() const;

        [[nodiscard]] std::vector<std::uint32_t> ChildrenOf(std::uint32_t joint) const;
        [[nodiscard]] std::vector<std::uint32_t> Roots() const;
        [[nodiscard]] bool IsDescendantOf(std::uint32_t joint, std::uint32_t ancestor) const;

        [[nodiscard]] std::optional<std::uint32_t> Find(const std::string &name) const
        {
            const auto it = lookup.find(name);
            return it == lookup.end() ? std::nullopt : std::optional(it->second);
        }

        // Change a joint's parent. Returns old->new index remap (joints get re-sorted to keep the parent-first
        // invariant), which you must apply to SkinVertex joint ids, clip channels and any stored Pose.
        // keepBindWorld=true keeps the joint where it is in bind space, so existing skinning stays valid.
        // keepBindWorld=false changes the bind pose, so bind data is always rebuilt in that case.
        std::vector<std::uint32_t> Reparent(std::uint32_t joint, std::uint32_t newParent, bool keepBindWorld = true,
                                            bool rebuildInverseBind = false);

        // The rest pose as local transforms, indexed like the joints.
        [[nodiscard]] std::vector<Transform> GetBindPose() const;

        void CalculateGlobalTransforms(std::span<const Transform> localTransforms,
                                       std::span<Mat4> globalTransforms) const;

        // out[i] = global[i] * inverseBind[i]; this is what gets uploaded to the skinning shader.
        void CalculateSkinningMatrices(std::span<const Mat4> globalTransforms, std::span<Mat4> out) const;

    private:
        void EnsureBind();

        std::vector<Joint> joints;
        std::vector<Mat4> globalBindTransforms;
        std::vector<Mat4> inverseBindTransforms;
        Mat4 correction{1.0f};
        std::unordered_map<std::string, std::uint32_t> lookup{};
        bool bindDirty = true;

    public:
        [[nodiscard]] std::span<Joint> GetJoints() { return joints; }
        [[nodiscard]] std::span<const Joint> GetJoints() const { return joints; }
        [[nodiscard]] Joint *GetJoint(std::uint32_t index) { return index < joints.size() ? &joints[index] : nullptr; }

        [[nodiscard]] const Joint *GetJoint(std::uint32_t index) const
        {
            return index < joints.size() ? &joints[index] : nullptr;
        }

        [[nodiscard]] std::span<const Mat4> GetGlobalBindTransforms() const { return globalBindTransforms; }
        [[nodiscard]] std::span<const Mat4> GetInverseBindTransforms() const { return inverseBindTransforms; }
    };
} // namespace SF::Engine
