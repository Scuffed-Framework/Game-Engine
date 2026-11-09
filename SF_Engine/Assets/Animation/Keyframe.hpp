#pragma once
#include <Math/Time/Time.hpp>
#include <Math/Transform.hpp>
#include <map>
#include <string>
#include <utility>
#include "Joint/AnimJoint.hpp"

namespace SF::Engine::Animation
{
    using namespace std;
    struct Keyframe
    {
        Keyframe() = default;
        Keyframe(const TimePoint TimeStamp, map<string, Transform> Pose) : TimeStamp(TimeStamp), Pose(std::move(Pose))
        {
        }

        void AddJointTransform(const string &jointNameId, const Mat4 &jointLocalTransform)
        {
            Pose.emplace(jointNameId, Transform::FromMatrix(jointLocalTransform));
        }

        [[nodiscard]] const TimePoint &GetTimeStamp() const { return TimeStamp; }

        [[nodiscard]] const map<string, Transform> &GetPose() const { return Pose; }

        bool operator==(const Keyframe &other) const { return TimeStamp == other.TimeStamp && Pose == other.Pose; }

    private:
        TimePoint TimeStamp;
        map<string, Transform> Pose;
    };
} // namespace SF::Engine::Animation
