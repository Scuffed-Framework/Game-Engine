#pragma once
#include <LowLevel/Reflection/RTTISingle.hpp>
#include <Math/Matrix.hpp>
#include <UtilityClasses/StlExtensions.hpp>

namespace SF::Engine
{
    class Joint
    {
        SF_RTTI_BASE(Joint)
    public:
        // give no data cuz
        explicit Joint(std::uint32_t index = 0, std::string name = {}, std::uint32_t parent = InvalidIndex,
                       const Transform &bindLocalTransform = {}, const Transform &invBindLocal = {}) :
            index(index), parent(parent), name(std::move(name)), localBindTransform(bindLocalTransform),
            inverseBindTransform(invBindLocal)
        {
            CalculateInverseBindTransform(Transform{});
        }

        virtual ~Joint() = default;

        void CalculateInverseBindTransform(const Transform &parentBindTransform = {})
        {
            const auto bindTransform = parentBindTransform * localBindTransform;

            inverseBindTransform = Transform::FromMatrix(inverse(bindTransform.ToMatrix()));

            for (auto &child: children)
                child.CalculateInverseBindTransform(bindTransform);
        }

        [[nodiscard]] std::uint32_t GetIndex() const { return index; }
        [[nodiscard]] std::uint32_t GetParent() const { return parent; }
        [[nodiscard]] const std::string &GetName() const { return name; }
        [[nodiscard]] const std::vector<Joint> &GetChildren() const { return children; }
        [[nodiscard]] const Transform &GetLocalBindTransform() const { return localBindTransform; }
        [[nodiscard]] const Transform &GetInverseBindTransform() const { return inverseBindTransform; }

        void SetIndex(std::uint32_t newIndex) { this->index = newIndex; }
        void SetParent(const std::uint32_t newParent) { this->parent = newParent; }
        void SetName(const std::string &newName) { this->name = newName; }
        void AddChild(const Joint &child) { children.emplace_back(child); }
        void SetLocalBindTransform(const Transform &newLocalBindTransform)
        {
            this->localBindTransform = newLocalBindTransform;
        }
        void SetInverseBindTransform(const Transform &newInverseBindTransform)
        {
            this->inverseBindTransform = newInverseBindTransform;
        }

    private:
        std::uint32_t index  = 0;
        std::uint32_t parent = InvalidIndex;
        std::string name{};
        std::vector<Joint> children{};

        Transform localBindTransform{};
        Transform inverseBindTransform{};
    };
} // namespace SF::Engine
