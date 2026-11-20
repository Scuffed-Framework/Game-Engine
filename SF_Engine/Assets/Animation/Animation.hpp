#pragma once
#include <Math/Time.hpp>
#include "Keyframe.hpp"
#include "Skeleton/AnimSkeleton.hpp"

namespace SF::Engine
{
    class Animation
    {
        DurationAsMilliseconds length;
        std::vector<Keyframe> keyframes;
        Entity *actingObject = nullptr;

    public:
        Animation(const DurationAsMilliseconds &length, std::vector<Keyframe> keyframes) :
            length(length), keyframes(std::move(keyframes))
        {
        }

        [[nodiscard]] const DurationAsMilliseconds &GetLength() const { return length; }
        [[nodiscard]] const std::vector<Keyframe> &GetKeyframes() const { return keyframes; }

        void SetActiveObject(Entity &entity) { actingObject = &entity; }
        [[nodiscard]] const Entity &GetActingObject() const { return *actingObject; }
    };
} // namespace SF::Engine
