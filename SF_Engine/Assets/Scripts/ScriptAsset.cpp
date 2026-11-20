#include "ScriptAsset.hpp"
#include <GameScript/ScriptEngine.hpp>

namespace SF::Engine
{
    // ScriptAsset
    bool ScriptAsset::Load(std::span<const uint8_t> data)
    {
        Source.assign(reinterpret_cast<const char *>(data.data()), data.size());
        return true;
    }

    void ScriptAsset::ExecuteFunction(const std::string &exec) const
    {
        auto *SE               = ScriptEngine::Get();
        const ScriptModule *sm = SE->FindScriptModule(*this);
        if (!sm || !sm->module)
        {
            Log::Error("Module not built for {}", name);
            return;
        }

        asIScriptFunction *func = sm->module->GetFunctionByDecl(exec.c_str());
        if (!func)
        {
            Log::Error("{}: no '{}'", exec.c_str(), name);
            return;
        }

        asIScriptContext *ctx = SE->engine->RequestContext(); // pooled
        ctx->Prepare(func);
        if (ctx->Execute() == asEXECUTION_EXCEPTION)
            Log::Error("{}: exception: {}", name, ctx->GetExceptionString());
        SE->engine->ReturnContext(ctx);
    }
} // namespace SF::Engine
