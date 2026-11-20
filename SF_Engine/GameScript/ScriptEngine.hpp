#pragma once
#include <Engine/Log/Log.hpp>
#include <Engine/Module.hpp>

#include <Assets/Scripts/ScriptAsset.hpp>
#include <angelscript.h>


#ifdef Always
    #undef Always
#endif

namespace SF::Engine
{
    class ScriptModule;

    class ScriptEngine : public ModuleRegistrar<ScriptEngine>
    {
        friend class ModuleRegistrar<ScriptEngine>;
        REGISTER_MODULE_EX(ScriptEngine, Stage::Always, ModuleStartupStage::PreEngineInitComplete);

        int State = 0;

    public:
        bool Initialize() override
        {
            if (!engine)
            {
                throw std::runtime_error("CRITICAL ERROR: Failed to create script engine");
            }
            State = engine->SetMessageCallback(asFUNCTION(+[](const asSMessageInfo *msg, void * /*param*/)
                                                          {
                                                              switch (msg->type)
                                                              {
                                                                  case asMSGTYPE_ERROR:
                                                                      Log::Error("{} ({}, {}) : {}", msg->section,
                                                                                 msg->row, msg->col, msg->message);
                                                                      break;
                                                                  case asMSGTYPE_WARNING:
                                                                      Log::Warning("{} ({}, {}) : {}", msg->section,
                                                                                   msg->row, msg->col, msg->message);
                                                                      break;
                                                                  case asMSGTYPE_INFORMATION:
                                                                  default:
                                                                      Log::Info("{} ({}, {}) : {}", msg->section,
                                                                                msg->row, msg->col, msg->message);
                                                                      break;
                                                              }
                                                          }),
                                               nullptr, asCALL_CDECL);
            return true;
        }

        void Shutdown() override { State = engine->Release(); }

        void Update() override {

        };

        ScriptModule &GetOrCreateModule(const std::string &moduleName);
        ScriptModule *FindScriptModule(const ScriptAsset &asset);

        std::unordered_map<std::string, ScriptModule> modules;
        asIScriptEngine *engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
    };

    class ScriptModule
    {
    public:
        using LoadingStage = ModuleStartupStage;

        ScriptModule() { ScriptEngine::Get()->engine->GetModule(name.c_str(), asGM_CREATE_IF_NOT_EXISTS); }

        void AddScript(ScriptAsset &asset)
        {
            if (!asset.ModuleName.empty() && asset.ModuleName != name)
                Log::Warning("Script {} assigned to module {}, requested {}", asset.name, name, asset.ModuleName);
            scripts.push_back(asset);
        }

        bool Build(asIScriptEngine *engine)
        {
            module = engine->GetModule(name.c_str(), asGM_ALWAYS_CREATE);
            for (const auto &s: scripts)
                module->AddScriptSection(s.name.c_str(), s.Source.c_str(), s.Source.size());
            return module->Build() >= 0;
        }

        std::string name;
        std::vector<ScriptAsset> scripts;
        asIScriptModule *module = nullptr;
    };
} // namespace SF::Engine
