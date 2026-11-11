#pragma once
#include <Math/KVP.hpp>
#include <vector>
#include "Keyframe.hpp"

namespace SF::Engine::Animation
{

    class AnimationCurve
    {
    public:
        // Properties
        std::vector<Keyframe> keyframes;
        const int length = static_cast<int>(keyframes.size());

        Keyframe AddKeyframe(const Keyframe &frame)
        {
            keyframes.push_back(frame);
            return frame;
        }

        void RemoveKeyframe(const Keyframe &frame)
        {
            if (const auto it = std::ranges::find(keyframes, frame); it != keyframes.end())
                keyframes.erase(it);
        }
    };
} // namespace SF::Engine::Animation
