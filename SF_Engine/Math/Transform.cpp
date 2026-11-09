#include "Transform.hpp"
#include <EntityComponentSystem/Entity.hpp>
#include <Math/BasicMath.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#include <glm/gtc/matrix_transform.hpp>
#pragma GCC diagnostic pop

namespace SF::Engine
{
    Transform::Transform(const Vec3 &position, const Rotation &rotation, const Vec3 &scale) :
        position(position), rotation(rotation), scale(scale)
    {
    }

    Mat4 Transform::GetWorldMatrix() const { return ToWorldMatrix(); }

    Mat4 Transform::ToWorldMatrix() const
    {
        Mat4 m              = ToMatrix();
        const Entity *owner = GetOwner();
        for (const Entity *p = owner ? owner->GetParent() : nullptr; p; p = p->GetParent())
        {
            if (const auto *t = p->GetComponent<Transform>())
                m = t->ToMatrix() * m;
        }
        return m;
    }

    Vec3 Transform::GetPosition() const { return GetWorldTransform().position; }
    Rotation Transform::GetRotation() const { return GetWorldTransform().rotation; }
    Vec3 Transform::GetScale() const { return GetWorldTransform().scale; }

    bool Transform::operator==(const Transform &rhs) const
    {
        return position == rhs.position && rotation == rhs.rotation && scale == rhs.scale;
    }

    bool Transform::operator!=(const Transform &rhs) const { return !operator==(rhs); }

    Transform operator*(const Transform &lhs, const Transform &rhs)
    {
        return {lhs.position + lhs.rotation * (lhs.scale * rhs.position), lhs.rotation * rhs.rotation,
                lhs.scale * rhs.scale};
    }

    Transform &Transform::operator*=(const Transform &rhs) { return *this = *this * rhs; }

    Transform Transform::GetWorldTransform() const
    {
        Transform local(position, rotation, scale);

        if (const Entity *owner = GetOwner())
            if (const Entity *parent = owner->GetParent())
                if (const auto *parentTransform = parent->GetComponent<Transform>())
                    return parentTransform->GetWorldTransform() * local;

        return local;
    }

    Transform Transform::FromMatrix(const Mat4 &m)
    {
        const Vec3 position(m[3]);

        Vec3 axisX(m[0]), axisY(m[1]), axisZ(m[2]);
        Vec3 scale{glm::length(axisX), glm::length(axisY), glm::length(axisZ)};

        // Mirrored matrix: fold the reflection into one scale axis so the rotation stays proper
        if (glm::determinant(glm::mat3(axisX, axisY, axisZ)) < 0.0f)
            scale.x = -scale.x;

        // Degenerate axes (zero scale): fall back to identity axes instead of dividing by zero
        constexpr float eps = 1e-8f;
        const Vec3 nx       = std::abs(scale.x) > eps ? axisX / scale.x : Vec3(1, 0, 0);
        const Vec3 ny       = std::abs(scale.y) > eps ? axisY / scale.y : Vec3(0, 1, 0);
        const Vec3 nz       = std::abs(scale.z) > eps ? axisZ / scale.z : Vec3(0, 0, 1);

        const Quaternion q = glm::quat_cast(glm::mat3(nx, ny, nz));
        return {position, Rotation(q), scale};
    }
} // namespace SF::Engine
