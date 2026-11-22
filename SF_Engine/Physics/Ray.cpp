#include "Ray.hpp"

namespace SF::Engine
{
    Ray::Ray(bool useMouse, const Vec2 &screenStart) : useMouse(useMouse), screenStart(screenStart) {}

    void Ray::Update(const Vec3 &currentPosition, const Vec2 &mousePosition, const Mat4 &viewMatrix,
                     const Mat4 &projectionMatrix)
    {
        origin = currentPosition;

        if (useMouse)
            UpdateNormalizedDeviceCoordinates(mousePosition.x, mousePosition.y);
        else
            normalizedCoords = screenStart;

        this->viewMatrix       = viewMatrix;
        this->projectionMatrix = projectionMatrix;
        clipCoords             = Vec4(normalizedCoords.x, normalizedCoords.y, -1.0f, 1.0f);
        UpdateEyeCoords();
        UpdateWorldCoords();
    }

    Vec3 Ray::GetPointOnRay(float distance) const
    {
        auto vector = distance * currentRay;
        return origin + vector;
    }

    Vec3 Ray::ConvertToScreenSpace(const Vec3 &position) const
    {
        Vec4 coords(position, 1.0f);
        coords = viewMatrix * coords;
        coords = projectionMatrix * coords;

        if (coords.w < 0.0f)
            return Vec3(0.0f);

        return {(coords.x / coords.w + 1.0f) / 2.0f, 1.0f - (coords.y / coords.w + 1.0f) / 2.0f, coords.z};
    }

    std::optional<float> Ray::IntersectTriangle(const Vec3 &v0, const Vec3 &v1, const Vec3 &v2) const
    {
        constexpr float Epsilon = 1e-7f;

        const Vec3 edge1 = v1 - v0;
        const Vec3 edge2 = v2 - v0;

        const Vec3 p    = cross(currentRay, edge2);
        const float det = dot(edge1, p);

        // Parallel ray or degenerate triangle.
        if (std::abs(det) < Epsilon)
            return std::nullopt;

        const float invDet = 1.0f / det;

        const Vec3 t = origin - v0;

        const float u = dot(t, p) * invDet;

        if (u < 0.0f || u > 1.0f)
            return std::nullopt;

        const Vec3 q = cross(t, edge1);

        const float v = dot(currentRay, q) * invDet;

        if (v < 0.0f || u + v > 1.0f)
            return std::nullopt;

        const float distance = dot(edge2, q) * invDet;

        if (distance < 0.0f)
            return std::nullopt;

        return distance;
    }


    void Ray::UpdateNormalizedDeviceCoordinates(float mouseX, float mouseY)
    {
        normalizedCoords.x = (2.0f * mouseX) - 1.0f;
        normalizedCoords.y = (2.0f * mouseY) - 1.0f;
    }

    void Ray::UpdateEyeCoords()
    {
        invertedProjection = inverse(projectionMatrix);
        eyeCoords          = invertedProjection * clipCoords;
        eyeCoords          = Vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);
    }

    void Ray::UpdateWorldCoords()
    {
        invertedView = inverse(viewMatrix);
        rayWorld     = invertedView * eyeCoords;
        currentRay   = Vec3(rayWorld);
    }
} // namespace SF::Engine
