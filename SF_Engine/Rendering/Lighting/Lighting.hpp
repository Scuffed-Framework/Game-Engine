#pragma once

#include "ClusterCullSFERenderpass.hpp"
#include "DeferredLightSFERenderpass.hpp"
#include "ForwardTransparentSFERenderpass.hpp"
#include "GBufferPass.hpp"
#include "LightManager.hpp"
#include "LitMeshSFERenderpass.hpp"

#include <Scene/Types.hpp>

namespace SF::Engine
{
    inline SceneLight &MakeLight(std::vector<SceneLight> &lights, const char *name, Lighting::LightType type,
                                 Vec3 color, float intensity, Vec3 pos, Vec3 rotDeg, float radius = 10.0f)
    {
        SceneLight &sl = lights.emplace_back(name);

        auto *light       = sl.GetComponent<Light>();
        light->name       = name;
        light->type       = type;
        light->color      = color;
        light->intensity  = intensity;
        light->radius     = radius;
        light->castShadow = true;

        auto *t     = sl.GetComponent<Transform>();
        t->position = pos;
        t->rotation = Rotation(EulerDeg(rotDeg));

        if (type == Lighting::LightType::Directional)
            light->direction = glm::normalize(t->rotation * Vec3(0.f, -1.f, 0.f));

        return sl;
    }
} // namespace SF::Engine
