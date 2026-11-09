#pragma once
#include <Math/Transform.hpp>

#include <cstdint>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace SF::Engine::Animation
{
    using namespace std;
    // Flat joint array. Invariant: a joint's parent always has a LOWER index than the joint itself,
    // so forward kinematics is one linear pass with no recursion and no per-joint heap allocation.
    class Skeleton
    {
    public:
        static constexpr uint32_t NoParent = 0xFFFFFFFFu;

        struct Joint
        {
            string name;
            uint32_t parent = NoParent;
            Transform bindLocal;    // rest pose, relative to parent
            Mat4 inverseBind{1.0f}; // bind-space -> joint-space (what skinning needs)
        };

        vector<Joint> joints;

        // Applied on top of root joints: unit conversion (FBX cm), axis fix-ups, settings.scale ...
        Mat4 correction{1.0f};

        uint32_t AddJoint(string name, uint32_t parent, const Transform &bindLocal)
        {
            if (parent != NoParent && parent >= joints.size())
                throw out_of_range("Skeleton::AddJoint: parent must be added before its children");
            const auto index = static_cast<uint32_t>(joints.size());
            lookup.emplace(name, index);
            joints.push_back(
                    {.name = std::move(name), .parent = parent, .bindLocal = bindLocal, .inverseBind = Mat4(1.0f)});
            return index;
        }


        [[nodiscard]] uint32_t Count() const { return static_cast<uint32_t>(joints.size()); }

        [[nodiscard]] optional<uint32_t> Find(const string &name) const
        {
            const auto it = lookup.find(name);
            return it == lookup.end() ? nullopt : optional(it->second);
        }

        [[nodiscard]] vector<uint32_t> ChildrenOf(uint32_t joint) const
        {
            vector<uint32_t> out;
            for (uint32_t i = joint + 1; i < joints.size(); ++i)
                if (joints[i].parent == joint)
                    out.push_back(i);
            return out;
        }
        
        [[nodiscard]] vector<Mat4> BindGlobals() const
        {
            vector<Mat4> g(joints.size());
            for (size_t i = 0; i < joints.size(); ++i)
            {
                const Mat4 local = joints[i].bindLocal.ToMatrix();
                g[i]             = joints[i].parent == NoParent ? local : g[joints[i].parent] * local;
            }
            return g;
        }

        // Derive inverseBind from the bind pose. Importers may then override with the file's offset matrices.
        void RebuildInverseBind()
        {
            const auto g = BindGlobals();
            for (size_t i = 0; i < joints.size(); ++i)
                joints[i].inverseBind = glm::inverse(g[i]);
        }

        // Change a joint's parent. Returns old->new index remap (joints get re-sorted to keep the parent-first
        // invariant), which you must apply to SkinVertex joint ids, clip channels and any stored Pose.
        // keepBindWorld=true keeps the joint where it is in bind space, so existing skinning stays valid.
        vector<uint32_t> Reparent(uint32_t joint, uint32_t newParent, bool keepBindWorld = true,
                                  bool rebuildInverseBind = false)
        {
            for (uint32_t p = newParent; p != NoParent; p = joints[p].parent)
                if (p == joint)
                    throw invalid_argument("Skeleton::Reparent: would create a cycle");

            if (keepBindWorld)
            {
                const auto g            = BindGlobals();
                const Mat4 parentG      = newParent == NoParent ? Mat4(1.0f) : g[newParent];
                joints[joint].bindLocal = Transform::FromMatrix(inverse(parentG) * g[joint]);
            }
            joints[joint].parent = newParent;

            const size_t n = joints.size();
            vector<vector<uint32_t>> kids(n);
            vector<uint32_t> roots;
            for (uint32_t i = 0; i < n; ++i)
                (joints[i].parent == NoParent ? roots : kids[joints[i].parent]).push_back(i);

            vector<uint32_t> order, stack(roots.rbegin(), roots.rend());
            order.reserve(n);
            while (!stack.empty())
            {
                const uint32_t j = stack.back();
                stack.pop_back();
                order.push_back(j);
                for (const uint32_t k: views::reverse(kids[j]))
                    stack.push_back(k);
            }

            vector<uint32_t> remap(n);
            for (uint32_t i = 0; i < n; ++i)
                remap[order[i]] = i;

            vector<Joint> sorted;
            sorted.reserve(n);
            for (uint32_t oldIndex: order)
            {
                Joint j = std::move(joints[oldIndex]);
                if (j.parent != NoParent)
                    j.parent = remap[j.parent];
                sorted.push_back(std::move(j));
            }
            joints = std::move(sorted);
            lookup.clear();
            for (uint32_t i = 0; i < n; ++i)
                lookup.emplace(joints[i].name, i);

            if (rebuildInverseBind)
                RebuildInverseBind();

            return remap;
        }

    private:
        unordered_map<string, uint32_t> lookup;
    };
} // namespace SF::Engine::Animation
