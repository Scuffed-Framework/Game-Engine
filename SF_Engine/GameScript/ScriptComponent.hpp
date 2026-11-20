#pragma once
#include <EntityComponentSystem/Component.hpp>

#include <Assets/AssetPipeline.hpp>
#include <Assets/Scripts/ScriptAsset.hpp>

namespace SF::Engine
{
    class ScriptComponent : Component::Registrar<ScriptComponent>
    {
        ScriptAsset script{};
        std::span<uint8_t> payload{};

    public:
        void Start() override
        {
            script.Load(payload);
            // call function Start()
        }
        void Update() override;
    };
} // namespace SF::Engine
