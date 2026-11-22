#pragma once
#include "Aabb.hpp"
#include <vector>

namespace SF::Engine
{
    class BVH
    {
    public:
        struct Node
        {
            Vec3 min{}, max{};
            uint32_t leftFirst; // child index (if internal) OR first tri index (if leaf)
            uint32_t triCount;  // 0 for internal nodes, > 0 for leaf nodes
            bool isLeaf() const { return triCount > 0; }
        };

        struct Triangle
        {
            Vec3 v0, v1, v2;
        };

        struct Hit
        {
            float distance;
            uint32_t triangleIndex;
        };

        std::vector<Node> bvhNodes;
        std::vector<Triangle> triangles;
        std::vector<uint32_t> triIdx;

        [[nodiscard]] std::optional<Hit> IntersectBVH(const Ray &ray, uint32_t rootIdx = 0) const
        {
            if (rootIdx >= bvhNodes.size())
                return std::nullopt;

            std::optional<Hit> closestHit;

            struct StackEntry
            {
                uint32_t nodeIdx;
                float distance;
            };

            std::vector<StackEntry> stack;
            stack.push_back({rootIdx, 0.0f});

            while (!stack.empty())
            {
                const StackEntry entry = stack.back();
                stack.pop_back();

                // No need to visit nodes farther than our closest hit.
                if (closestHit && entry.distance >= closestHit->distance)
                    continue;

                const Node &node = bvhNodes[entry.nodeIdx];

                if (node.isLeaf())
                {
                    for (uint32_t i = 0; i < node.triCount; ++i)
                    {
                        const uint32_t triangleIndex = triIdx[node.leftFirst + i];
                        const Triangle &triangle     = triangles[triangleIndex];

                        if (auto distance = ray.IntersectTriangle(triangle.v0, triangle.v1, triangle.v2))
                        {
                            if (!closestHit || *distance < closestHit->distance)
                                closestHit = Hit{*distance, triangleIndex};
                        }
                    }

                    continue;
                }

                const uint32_t left  = node.leftFirst;
                const uint32_t right = left + 1;

                const auto leftDistance = AABB{bvhNodes[left].min, bvhNodes[left].max}.RayIntersectsAABB(ray);

                const auto rightDistance = AABB{bvhNodes[right].min, bvhNodes[right].max}.RayIntersectsAABB(ray);

                // Push farther child first, so the nearer child is processed first.
                if (leftDistance && rightDistance)
                {
                    if (*leftDistance < *rightDistance)
                    {
                        stack.push_back({right, *rightDistance});
                        stack.push_back({left, *leftDistance});
                    } else
                    {
                        stack.push_back({left, *leftDistance});
                        stack.push_back({right, *rightDistance});
                    }
                } else if (leftDistance)
                {
                    stack.push_back({left, *leftDistance});
                } else if (rightDistance)
                {
                    stack.push_back({right, *rightDistance});
                }
            }

            return closestHit;
        }
    };
} // namespace SF::Engine
