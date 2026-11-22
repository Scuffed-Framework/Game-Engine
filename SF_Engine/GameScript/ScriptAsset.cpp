#include "ScriptAsset.hpp"
#include <GameScript/ScriptEngine.hpp>

namespace SF::Engine
{
    ScriptAsset::~ScriptAsset()
    {
        if (auto *SE = ScriptEngine::Get())
            if (auto *sm = SE->FindScriptModule(*this))
                sm->RemoveScript(*this);
    }

    bool ScriptAsset::Load(const std::span<const uint8_t> data)
    {
        Source.assign(reinterpret_cast<const char *>(data.data()), data.size());
        ScriptEngine::Get()->GetOrCreateModule(ModuleName).AddScript(*this);
        return true;
    }

    void ScriptAsset::ExecuteFunction(const std::string &decl) const
    {
        auto *SE = ScriptEngine::Get();
        auto *sm = SE->FindScriptModule(*this);
        if (!sm || !sm->EnsureBuilt())
        {
            Log::Error("Module not built for {}", name);
            return;
        }

        asIScriptFunction *func = sm->module->GetFunctionByDecl(decl.c_str());
        if (!func)
        {
            Log::Error("{}: no function '{}'", name, decl);
            return;
        }
        SE->Execute(func);
    }

    asIScriptObject *ScriptAsset::Instantiate() const
    {
        auto *SE = ScriptEngine::Get();
        auto *sm = SE->FindScriptModule(*this);
        if (!sm || ClassName.empty())
        {
            Log::Error("{}: cannot instantiate (module or ClassName missing)", name);
            return nullptr;
        }
        return SE->Instantiate(*sm, ClassName);
    }
} // namespace SF::Engine
