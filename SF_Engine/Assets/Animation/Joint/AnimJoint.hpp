#pragma once
#include <LowLevel/Reflection/RTTISingle.hpp>
#include <Math/BasicMath.hpp>
#include <Math/Matrix/Matrix.hpp>
#include <Math/Quaternion/Quaternion.hpp>


namespace SF::Engine::Animation
{
    using namespace std;
    class Joint
    {
        SF_RTTI_BASE(Joint)
    public:
        // give no data cuz
        explicit Joint(uint32_t index = 0, string name = "", const Mat4 &bindLocalTransform = Mat4(0)) :
            index(index), name(std::move(name)), localBindTransform(bindLocalTransform)
        {
        }

        virtual ~Joint() = default;

        void CalculateInverseBindTransform(const Mat4 &parentBindTransform)
        {
            auto bindTransform   = parentBindTransform * localBindTransform;
            inverseBindTransform = inverse(bindTransform);

            for (auto &child: children)
                child.CalculateInverseBindTransform(bindTransform);
        }

        [[nodiscard]] uint32_t GetIndex() const { return index; }
        void SetIndex(uint32_t index) { this->index = index; }

        [[nodiscard]] const string &GetName() const { return name; }
        void SetName(const string &name) { this->name = name; }

        [[nodiscard]] const vector<Joint> &GetChildren() const { return children; }

        void AddChild(const Joint &child) { children.emplace_back(child); }

        [[nodiscard]] const Mat4 &GetLocalBindTransform() const { return localBindTransform; }
        void SetLocalBindTransform(const Mat4 &localBindTransform) { this->localBindTransform = localBindTransform; }
        [[nodiscard]] const Mat4 &GetInverseBindTransform() const { return inverseBindTransform; }
        void SetInverseBindTransform(const Mat4 &inverseBindTransform)
        {
            this->inverseBindTransform = inverseBindTransform;
        }

    private:
        uint32_t index = 0;
        string name;
        vector<Joint> children;

        Mat4 localBindTransform;
        Mat4 inverseBindTransform{};
    };
} // namespace SF::Engine::Animation
