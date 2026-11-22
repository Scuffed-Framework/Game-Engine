#pragma once
#include <Assets/AssetPipeline.hpp>
#include <EntityComponentSystem/Component.hpp>
#include <LowLevel/Reflection/RTTI/SerializeContext.hpp>
#include "ScriptAsset.hpp"

namespace SF::Engine
{
    class ScriptComponent : public Component::Registrar<ScriptComponent>
    {
        static bool Reflect()
        {
            Reflection::SerializeContext::Instance().Class<ScriptComponent>()->Version(1)->Field(
                    "Script", &ScriptComponent::script);
            return true;
        }

        inline static const bool Registered = (Register("Script"), Reflect());

        ScriptAsset script{};
        std::span<const uint8_t> payload{}; // source bytes, set by the asset loader

        asIScriptObject *object          = nullptr;
        asIScriptFunction *startFn       = nullptr;
        asIScriptFunction *updateFn      = nullptr;
        asIScriptFunction *OnDisableFn   = nullptr;
        asIScriptFunction *OnEnableFn    = nullptr;
        asIScriptFunction *ShutdownFn    = nullptr;
        asIScriptFunction *ResetFn       = nullptr;
        asIScriptFunction *FixedUpdateFn = nullptr;

    public:
        ~ScriptComponent() override;

        void Start() override;
        void StartFixedTimer();
        void StopFixedTimer();
        void Update() override;
        void OnDisable() override;
        void OnEnable() override;
        void Shutdown() override;

        void FixedUpdate() override;
        void Reset() override;

        void Serialize(XMLNode &node) const override
        {
            Component::Serialize(node);
            script.Serialize(node);
        }
        void Deserialize(const XMLNode &node) override
        {
            Component::Deserialize(node);
            script.Deserialize(node);
        }
    };
} // namespace SF::Engine
