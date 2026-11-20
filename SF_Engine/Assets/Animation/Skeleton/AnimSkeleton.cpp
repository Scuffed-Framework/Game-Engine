#include "AnimSkeleton.hpp"

#include <Engine/Log/Log.hpp>

#include <numeric>
#include <ranges>

namespace SF::Engine
{
    uint32_t Skeleton::AddJoint(std::string name, const std::uint32_t parent, const Transform &bindLocal)
    {
        if (parent != NoParent && parent >= joints.size())
            throw std::out_of_range("Skeleton::AddJoint: parent must be added before its children");

        const auto index = static_cast<uint32_t>(joints.size());

        // emplace keeps the first joint with a given name.
        if (!lookup.emplace(name, index).second)
            Log::Warning("Duplicate joint name: {}", name);

        joints.emplace_back(index, std::move(name), parent, bindLocal);
        bindDirty = true;
        return index;
    }

    void Skeleton::Clear()
    {
        joints.clear();
        globalBindTransforms.clear();
        inverseBindTransforms.clear();
        lookup.clear();
        bindDirty = true;
    }

    bool Skeleton::IsValid() const
    {
        for (std::uint32_t i = 0; i < joints.size(); ++i)
        {
            const auto parent = joints[i].GetParent();

            if (joints[i].GetIndex() != i)
                return false;

            if (parent != NoParent && parent >= i)
                return false;
        }

        return true;
    }

    void Skeleton::EnsureBind()
    {
        if (!HasBindData())
            RebuildInverseBind();
    }

    void Skeleton::RebuildInverseBind()
    {
        globalBindTransforms.resize(joints.size());
        inverseBindTransforms.resize(joints.size());

        for (std::size_t i = 0; i < joints.size(); ++i)
        {
            const auto parent = joints[i].GetParent();

            const Mat4 local = joints[i].GetLocalBindTransform().ToMatrix();

            globalBindTransforms[i]  = parent == NoParent ? local : globalBindTransforms[parent] * local;
            inverseBindTransforms[i] = inverse(globalBindTransforms[i]);

            // Keep the copy stored on the Joint in sync.
            joints[i].SetInverseBindTransform(Transform::FromMatrix(inverseBindTransforms[i]));
        }

        bindDirty = false;
    }

    std::vector<std::uint32_t> Skeleton::ChildrenOf(const std::uint32_t joint) const
    {
        std::vector<std::uint32_t> out;

        if (joint >= joints.size())
            return out;

        // Children always come after their parent.
        for (std::uint32_t i = joint + 1; i < joints.size(); ++i)
        {
            if (joints[i].GetParent() == joint)
                out.push_back(i);
        }

        return out;
    }

    std::vector<std::uint32_t> Skeleton::Roots() const
    {
        std::vector<std::uint32_t> out;

        for (std::uint32_t i = 0; i < joints.size(); ++i)
        {
            if (joints[i].GetParent() == NoParent)
                out.push_back(i);
        }

        return out;
    }

    bool Skeleton::IsDescendantOf(const std::uint32_t joint, const std::uint32_t ancestor) const
    {
        if (joint >= joints.size() || ancestor >= joints.size())
            return false;

        for (auto p = joints[joint].GetParent(); p != NoParent; p = joints[p].GetParent())
        {
            if (p == ancestor)
                return true;
        }

        return false;
    }

    std::vector<Transform> Skeleton::GetBindPose() const
    {
        std::vector<Transform> pose;
        pose.reserve(joints.size());

        for (const auto &joint: joints)
            pose.push_back(joint.GetLocalBindTransform());

        return pose;
    }

    void Skeleton::CalculateGlobalTransforms(std::span<const Transform> localTransforms,
                                             std::span<Mat4> globalTransforms) const
    {
        if (localTransforms.size() != joints.size() || globalTransforms.size() != joints.size())
            throw std::invalid_argument("Skeleton::CalculateGlobalTransforms: invalid buffer size");

        for (std::size_t i = 0; i < joints.size(); ++i)
        {
            const Mat4 local  = localTransforms[i].ToMatrix();
            const auto parent = joints[i].GetParent();

            globalTransforms[i] = parent == NoParent ? local : globalTransforms[parent] * local;
        }
    }

    void Skeleton::CalculateSkinningMatrices(std::span<const Mat4> globalTransforms, std::span<Mat4> out) const
    {
        if (globalTransforms.size() != joints.size() || out.size() != joints.size())
            throw std::invalid_argument("Skeleton::CalculateSkinningMatrices: invalid buffer size");

        if (!HasBindData())
            throw std::logic_error("Skeleton::CalculateSkinningMatrices: call RebuildInverseBind() first");

        for (std::size_t i = 0; i < joints.size(); ++i)
            out[i] = globalTransforms[i] * inverseBindTransforms[i];
    }

    std::vector<std::uint32_t> Skeleton::Reparent(const std::uint32_t joint, const std::uint32_t newParent,
                                                  const bool keepBindWorld, const bool rebuildInverseBind)
    {
        const std::size_t n = joints.size();

        if (joint >= n)
            throw std::out_of_range("Skeleton::Reparent: invalid joint");

        if (newParent != NoParent && newParent >= n)
            throw std::out_of_range("Skeleton::Reparent: invalid parent");

        if (joint == newParent)
            throw std::invalid_argument("Skeleton::Reparent: cannot parent to itself");

        // Same parent: nothing changes, identity remap.
        if (joints[joint].GetParent() == newParent)
        {
            std::vector<std::uint32_t> identity(n);
            std::iota(identity.begin(), identity.end(), 0u);
            return identity;
        }

        // Check for cycles.
        for (std::uint32_t p = newParent; p != NoParent; p = joints[p].GetParent())
        {
            if (p == joint)
                throw std::invalid_argument("Skeleton::Reparent: would create a cycle");
        }

        // Global bind transforms must be valid before we read them (AddJoint leaves them dirty).
        EnsureBind();

        // Preserve bind-space position.
        if (keepBindWorld)
        {
            const Mat4 oldGlobal    = globalBindTransforms[joint];
            const Mat4 parentGlobal = newParent == NoParent ? Mat4(1.0f) : globalBindTransforms[newParent];

            joints[joint].SetLocalBindTransform(Transform::FromMatrix(inverse(parentGlobal) * oldGlobal));
        }

        joints[joint].SetParent(newParent);

        // Build adjacency.
        std::vector<std::vector<std::uint32_t>> kids(n);
        std::vector<std::uint32_t> roots;

        for (std::uint32_t i = 0; i < n; ++i)
        {
            const auto parent = joints[i].GetParent();

            if (parent == NoParent)
                roots.push_back(i);
            else
                kids[parent].push_back(i);
        }

        // Generate parent-first ordering (iterative DFS, keeps sibling order).
        std::vector<std::uint32_t> order;
        std::vector<std::uint32_t> stack(roots.rbegin(), roots.rend());

        order.reserve(n);

        while (!stack.empty())
        {
            const auto j = stack.back();
            stack.pop_back();

            order.push_back(j);

            for (const auto k: std::views::reverse(kids[j]))
                stack.push_back(k);
        }

        if (order.size() != n)
            throw std::logic_error("Skeleton::Reparent: invalid hierarchy");

        // Build old -> new mapping.
        std::vector<std::uint32_t> remap(n);

        for (std::uint32_t i = 0; i < n; ++i)
            remap[order[i]] = i;

        // Reorder joints.
        std::vector<Joint> sorted;
        sorted.reserve(n);

        for (const auto oldIndex: order)
        {
            Joint j = std::move(joints[oldIndex]);

            if (j.GetParent() != NoParent)
                j.SetParent(remap[j.GetParent()]);

            j.SetIndex(remap[oldIndex]);
            sorted.push_back(std::move(j));
        }

        joints = std::move(sorted);

        // Rebuild name lookup.
        lookup.clear();

        for (std::uint32_t i = 0; i < n; ++i)
            lookup.emplace(joints[i].GetName(), i);

        // Carry the existing bind data over to the new order so inverse binds stay valid.
        std::vector<Mat4> newGlobal(n);
        std::vector<Mat4> newInverse(n);

        for (std::uint32_t i = 0; i < n; ++i)
        {
            newGlobal[remap[i]]  = globalBindTransforms[i];
            newInverse[remap[i]] = inverseBindTransforms[i];
        }

        globalBindTransforms  = std::move(newGlobal);
        inverseBindTransforms = std::move(newInverse);

        // If the bind pose moved, the cached globals are stale, so rebuild regardless.
        if (rebuildInverseBind || !keepBindWorld)
            RebuildInverseBind();

        return remap;
    }
} // namespace SF::Engine
