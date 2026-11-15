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
    // Do the same thing with position so we can abstract over the like 40 different coordinate systems >:)
    struct RadiansTag
    {
        static constexpr float ToRadians = 1.0f;
    };
    struct DegreesTag
    {
        static constexpr float ToRadians = glm::pi<float>() / 180.0f;
    };

    template<typename Unit>
    struct Euler
    {
        Vec3 value{};

        constexpr Euler() = default;
        constexpr explicit Euler(const Vec3 &v) : value(v) {}
        constexpr Euler(float x, float y, float z) : value(x, y, z) {}

        // Implicit unit conversion: EulerRad r = EulerDeg(0, 90, 0);
        template<typename Other>
            requires(!std::is_same_v<Other, Unit>)
        constexpr Euler(const Euler<Other> &o) : value(o.value * (Other::ToRadians / Unit::ToRadians))
        {
        }
    };

    using EulerDeg = Euler<DegreesTag>;
    using EulerRad = Euler<RadiansTag>;

    // Canonical orientation: always a quaternion internally, viewable as any Euler unit.
    class Rotation
    {
    public:
        friend bool operator==(const Rotation &a, const Rotation &b) { return abs(glm::dot(a.q, b.q)) > 1.0f - 1e-6f; }

        Rotation() = default;
        Rotation(const Quaternion &q) : q(normalize(q)) {}

        template<typename Unit>
        Rotation(const Euler<Unit> &e) : q(Quaternion(e.value * Unit::ToRadians))
        {
        }

        template<typename Unit>
        [[nodiscard]] Euler<Unit> As() const
        {
            return Euler<Unit>(glm::eulerAngles(q) / Unit::ToRadians);
        }

        [[nodiscard]] Quaternion &Raw() { return q; }

        [[nodiscard]] EulerDeg Degrees() const { return As<DegreesTag>(); }
        [[nodiscard]] EulerRad Radians() const { return As<RadiansTag>(); }

        [[nodiscard]] const Quaternion &Quat() const { return q; }
        operator const Quaternion &() const { return q; }

        [[nodiscard]] Mat4 ToMatrix() const { return glm::mat4_cast(q); }

        [[nodiscard]] Rotation Inverse() const { return Rotation(inverse(q)); }

        friend Rotation operator*(const Rotation &a, const Rotation &b) { return Rotation(a.q * b.q); }
        Vec3 operator*(const Vec3 &v) const { return q * v; }

    private:
        Quaternion q{1.0f, 0.0f, 0.0f, 0.0f}; // identity (w, x, y, z)
    };

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
        Transform(const Vec3 &position = {}, const Rotation &rotation = {}, const Vec3 &scale = Vec3(1.0f));
        ~Transform() override = default;

        std::string_view GetTypeName() const override { return "Transform"; }

        Mat4 GetWorldMatrix() const;
        Vec3 GetPosition() const;
        Rotation GetRotation() const;
        Vec3 GetScale() const;

        const Vec3 &GetLocalPosition() const { return position; }
        void SetLocalPosition(const Vec3 &localPosition) { position = localPosition; }

        const Rotation &GetLocalRotation() const { return rotation; }
        void SetLocalRotation(const Rotation &localRotation) { rotation = localRotation; }

        const Vec3 &GetLocalScale() const { return scale; }
        void SetLocalScale(const Vec3 &localScale) { scale = localScale; }

        bool operator==(const Transform &rhs) const;
        bool operator!=(const Transform &rhs) const;

        friend Transform operator*(const Transform &lhs, const Transform &rhs);

        Transform &operator*=(const Transform &rhs);

        Transform GetWorldTransform() const;

        Vec3 position;
        Rotation rotation;
        Vec3 scale;

        mutable Transform *worldTransform = nullptr;

        void Serialize(XMLNode &node) const override
        {
            Component::Serialize(node);
            SerializeVec3(node, "position", position);
            node.SetAttribute("rotation", rotation.Quat());
            SerializeVec3(node, "scale", scale);
        }

        void Deserialize(const XMLNode &node) override
        {
            Component::Deserialize(node);
            position = DeserializeVec3(node, "position");

            Quaternion q;
            rotation = node.GetAttribute("rotation", q) ? Rotation(q) : Rotation{};

            scale = DeserializeVec3(node, "scale", {1.f, 1.f, 1.f});
        }

        // Local TRS composed up the parent chain, using the same degrees convention as ToMatrix().
        // (GetWorldMatrix() treats rotation as radians and sums parent/child angles, so it is not usable here.)
        Mat4 ToWorldMatrix() const;

        Mat4 ToMatrix() const
        {
            return translate(Mat4(1.0f), position) * rotation.ToMatrix() * glm::scale(Mat4(1.0f), scale);
        }

        static Transform FromMatrix(const Mat4 &m);

        void Reset() override
        {
            position = {0.0f, 0.0f, 0.0f};
            rotation = Rotation{};
            scale    = {1.0f, 1.0f, 1.0f};
        }

        std::unique_ptr<Component> Clone() const override { return std::make_unique<Transform>(*this); }
    };
} // namespace SF::Engine
