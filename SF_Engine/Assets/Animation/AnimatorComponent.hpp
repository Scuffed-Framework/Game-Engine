#pragma once
#include <EntityComponentSystem/Component.hpp>
#include <map>
#include "Animation.hpp"
#include "Joint/AnimJoint.hpp"

namespace SF::Engine
{
    struct Keyframe;

    class AnimatorComponent : public Component::Registrar<AnimatorComponent>
    {
        bool playing                         = false;
        std::unique_ptr<Animation> animation = nullptr;

    public:
        void Start() override
        {
            if (animation && GetOwner()) // Start() is now actually called; animation is null until one is assigned
                animation->SetActiveObject(*GetOwner());
        };
        void Reset() override;

        void Update() override
        {
            // if (playing)
            //     PlayAnimation();
        }

        [[nodiscard]] std::map<std::string, Mat4> CalculateCurrentAnimationPose() const;
        [[nodiscard]] std::pair<Keyframe, Keyframe> GetSurroundingKeyframes() const;
        [[nodiscard]] float CalculateProgression(const Keyframe &previousFrame, const Keyframe &nextFrame) const;

        // void PlayAnimation(const Joint &Root, vector<Mat4> &Matrices) {}
    };
} // namespace SF::Engine
