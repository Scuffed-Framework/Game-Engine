#pragma once
#include <Math/Crc32.hpp>
#include <Math/Vector.hpp>
#include <Physics/Ray.hpp>

namespace SF::Engine
{
    using namespace std;
    class AABB
    {
    public:
        Vec3 min{0}, max{1};

        AABB(const Vec3 min, const Vec3 max) : min(min), max(max) {}

        bool AabbIntersection(const AABB other) const
        {
            // clang-format off
            return min.x < other.max.x && max.x > min.x &&
                   min.y < other.max.y && max.y > min.y &&
                   min.z < other.max.z && max.z > min.z;
            // clang-format on
        }

        [[nodiscard]] std::optional<float> RayIntersectsAABB(const Ray &ray) const
        {
            const Vec3 &origin = ray.GetOrigin();
            const Vec3 &dir    = ray.GetCurrentRay();

            float tMin = 0.0f;
            float tMax = std::numeric_limits<float>::infinity();

            for (int i = 0; i < 3; ++i)
            {
                if (std::abs(dir[i]) < 1e-8f)
                {
                    if (origin[i] < min[i] || origin[i] > max[i])
                        return std::nullopt;

                    continue;
                }

                float t1 = (min[i] - origin[i]) / dir[i];
                float t2 = (max[i] - origin[i]) / dir[i];

                if (t1 > t2)
                    std::swap(t1, t2);

                tMin = std::max(tMin, t1);
                tMax = std::min(tMax, t2);

                if (tMin > tMax)
                    return std::nullopt;
            }

            return tMin;
        }

        bool Contains(const Vec3 p) const
        {
            // clang-format off
            return p.x >= min.x && p.x < max.x &&
                   p.y >= min.y && p.y < max.y &&
                   p.z >= min.z && p.z < max.z;
            // clang-format on
        }

        double GetVolume() const { return (max.x - min.x) * (max.y - min.y) * (max.z - min.z); }

        auto operator==(const AABB &a) const -> bool { return min == a.min && max == a.max; }
    };
} // namespace SF::Engine
