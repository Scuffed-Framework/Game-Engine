#pragma once

#include <EntityComponentSystem/Component.hpp>
#include <LowLevel/XML/XMLModule.hpp>
#include <Math/Matrix/Matrix.hpp>
#include <Math/Vectors/Vector.hpp>
#include <Scene/SceneSerialization.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <LowLevel/Reflection/RTTI/SerializeContext.hpp>

namespace SF::Engine
{
    /**
     * @brief Holds position, rotation, and scale components.
     */
    class Transform : public Component::Registrar<Transform>
    {
        static bool Reflect()
        {
            Reflection::SerializeContext::Instance()
                    .Class<Transform>()
                    ->Version(1)
                    ->Field("Position", &Transform::position)
                    ->Speed(0.05f)
                    ->ResetTo(0, 0, 0)
                    ->Field("Rotation", &Transform::rotation)
                    ->Speed(0.5f)
                    ->ResetTo(0, 0, 0)
                    ->Field("Scale", &Transform::scale)
                    ->Speed(0.01f)
                    ->ResetTo(1, 1, 1);
            return true;
        }

        inline static const bool Registered = (Register("Transform"), Reflect());

    public:
        /**
         * Creates a new transform.
         * @param position The position.
         * @param rotation The rotation.
         * @param scale The scale.
         */
        Transform(const Vec3 &position = {}, const Vec3 &rotation = {}, const Vec3 &scale = Vec3(1.0f));
        ~Transform() override = default;

        std::string_view GetTypeName() const override { return "Transform"; }

        Mat4 GetWorldMatrix() const;
        Vec3 GetPosition() const;
        Vec3 GetRotation() const;
        Vec3 GetScale() const;

        const Vec3 &GetLocalPosition() const { return position; }
        void SetLocalPosition(const Vec3 &localPosition) { position = localPosition; }

        const Vec3 &GetLocalRotation() const { return rotation; }
        void SetLocalRotation(const Vec3 &localRotation) { rotation = localRotation; }

        const Vec3 &GetLocalScale() const { return scale; }
        void SetLocalScale(const Vec3 &localScale) { scale = localScale; }

        bool operator==(const Transform &rhs) const;
        bool operator!=(const Transform &rhs) const;

        friend Transform operator*(const Transform &lhs, const Transform &rhs);

        Transform &operator*=(const Transform &rhs);

        Transform GetWorldTransform() const;

        Vec3 position;
        Vec3 rotation;
        Vec3 scale;

        mutable Transform *worldTransform = nullptr;
        void Serialize(XMLNode &node) const override
        {
            Component::Serialize(node);
            SerializeVec3(node, "position", position);
            SerializeVec3(node, "rotation", rotation);
            SerializeVec3(node, "scale", scale);
        }

        void Deserialize(const XMLNode &node) override
        {
            Component::Deserialize(node);
            position = DeserializeVec3(node, "position");
            rotation = DeserializeVec3(node, "rotation");
            scale    = DeserializeVec3(node, "scale", {1.f, 1.f, 1.f});
        }

        // Local TRS composed up the parent chain, using the same degrees convention as ToMatrix().
        // (GetWorldMatrix() treats rotation as radians and sums parent/child angles, so it is not usable here.)
        Mat4 ToWorldMatrix() const;

        Mat4 ToMatrix() const
        {
            const Mat4 T = translate(Mat4(1.0f), position);
            const Mat4 R =
                    glm::yawPitchRoll(glm::radians(rotation.y), glm::radians(rotation.x), glm::radians(rotation.z));
            const Mat4 S = glm::scale(Mat4(1.0f), scale);
            return T * R * S;
        }

        void Reset() override
        {
            position = {0.0f, 0.0f, 0.0f};
            rotation = {0.0f, 0.0f, 0.0f};
            scale    = {1.0f, 1.0f, 1.0f};
        }

        unique_ptr<Component> Clone() const override { return std::make_unique<Transform>(*this); }
    };
} // namespace SF::Engine
