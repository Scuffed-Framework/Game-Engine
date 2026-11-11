#pragma once
#include <Math/Time/Time.hpp>
#include <Math/Transform.hpp>
#include <map>
#include <string>
#include <utility>
#include "Joint/AnimJoint.hpp"

namespace SF::Engine::Animation
{

    struct Keyframe
    {
        Keyframe() = default;
        Keyframe(const TimePoint TimeStamp, std::map<std::string, Transform> Pose) :
            TimeStamp(TimeStamp), Pose(std::move(Pose))
        {
        }

        void AddJointTransform(const std::string &jointNameId, const Mat4 &jointLocalTransform)
        {
            Pose.emplace(jointNameId, Transform::FromMatrix(jointLocalTransform));
        }

        [[nodiscard]] const TimePoint &GetTimeStamp() const { return TimeStamp; }

        [[nodiscard]] const std::map<std::string, Transform> &GetPose() const { return Pose; }

        bool operator==(const Keyframe &other) const { return TimeStamp == other.TimeStamp && Pose == other.Pose; }

    private:
        TimePoint TimeStamp;
        std::map<std::string, Transform> Pose;
    };
} // namespace SF::Engine::Animation
