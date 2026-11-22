#include "ScriptEngine.hpp"

#include <ranges>

namespace SF::Engine
{
    bool ScriptModule::Build()
    {
        auto *eng = ScriptEngine::Get()->engine;
        module    = eng->GetModule(name.c_str(), asGM_ALWAYS_CREATE);
        if (!module)
            return false;

        for (const ScriptAsset *s: scripts)
        {
            if (module->AddScriptSection(s->name.c_str(), s->Source.c_str(), s->Source.size()) < 0)
            {
                Log::Error("Failed to add script section {} to module {}", s->name, name);
                return false;
            }
        }

        const int r = module->Build();
        dirty       = r < 0;
        if (r < 0)
            Log::Error("Failed to build script module {}", name);
        return r >= 0;
    }

    bool ScriptModule::SaveByteCodeToDisk(const bool stripDebug)
    {
        if (!Build())
            return false;

        File file(name + ".bytecode"); // must outlive the stream
        ByteCodeStream stream(&file);
        wasDebugInfoStripped = stripDebug;
        return module->SaveByteCode(&stream, stripDebug) >= 0;
    }

    bool ScriptModule::LoadByteCodeFromDisk()
    {
        auto *eng = ScriptEngine::Get()->engine;
        module    = eng->GetModule(name.c_str(), asGM_ALWAYS_CREATE);
        if (!module)
            return false;

        File file(name + ".bytecode");
        ByteCodeStream stream(&file);
        const bool ok = module->LoadByteCode(&stream, &wasDebugInfoStripped) >= 0;
        dirty         = !ok;
        return ok;
    }

    bool ScriptModule::LoadByteCodeFromSpan(const std::span<const uint8_t> span)
    {
        ByteCodeStream stream(span);
        return module->LoadByteCode(&stream, &wasDebugInfoStripped);
    }
    
    bool ScriptModule::LoadByteCodeFromMemory(const void *mem, const size_t size)
    {
        ByteCodeStream stream(mem, size);
        return module->LoadByteCode(&stream, &wasDebugInfoStripped);
    }

    bool ScriptEngine::Initialize()
    {
        engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
        if (!engine)
            throw std::runtime_error("CRITICAL ERROR: Failed to create script engine");

        engine->SetMessageCallback(
                asFUNCTION(+[](const asSMessageInfo *msg, void * /*param*/)
                           {
                               switch (msg->type)
                               {
                                   case asMSGTYPE_ERROR:
                                       Log::Error("{} ({}, {}) : {}", msg->section, msg->row, msg->col, msg->message);
                                       break;
                                   case asMSGTYPE_WARNING:
                                       Log::Warning("{} ({}, {}) : {}", msg->section, msg->row, msg->col, msg->message);
                                       break;
                                   default:
                                       Log::Info("{} ({}, {}) : {}", msg->section, msg->row, msg->col, msg->message);
                                       break;
                               }
                           }),
                nullptr, asCALL_CDECL);

        return true;
    }

    void ScriptEngine::Shutdown()
    {
        for (auto &m: modules | std::views::values)
            if (m.module)
                m.module->Discard();
        modules.clear();

        if (engine)
        {
            engine->GarbageCollect(asGC_FULL_CYCLE);
            engine->ShutDownAndRelease(); // preferred over Release() for the engine
            engine = nullptr;
        }
    }

    void ScriptEngine::Update()
    {
        if (engine)
            engine->GarbageCollect(asGC_ONE_STEP | asGC_DESTROY_GARBAGE);
    }

    ScriptModule &ScriptEngine::GetOrCreateModule(const std::string &moduleName)
    {
        return modules.try_emplace(moduleName, moduleName).first->second;
    }

    ScriptModule *ScriptEngine::FindScriptModule(const ScriptAsset &asset)
    {
        const auto it = modules.find(asset.ModuleName);
        return it != modules.end() ? &it->second : nullptr;
    }

    bool ScriptEngine::Execute(asIScriptFunction *func, asIScriptObject *object,
                               const std::function<void(asIScriptContext *)> &setArgs) const
    {
        if (!func)
            return false;

        asIScriptContext *ctx = engine->RequestContext(); // pooled
        bool ok               = false;

        if (ctx->Prepare(func) >= 0 && (!object || ctx->SetObject(object) >= 0))
        {
            if (setArgs)
                setArgs(ctx); // must come after Prepare and SetObject
            const int r = ctx->Execute();
            if (r == asEXECUTION_FINISHED)
                ok = true;
            else if (r == asEXECUTION_EXCEPTION)
                Log::Error("{}: script exception: {} (line {})", func->GetDeclaration(), ctx->GetExceptionString(),
                           ctx->GetExceptionLineNumber());
        }
        engine->ReturnContext(ctx);
        return ok;
    }

    asIScriptObject *ScriptEngine::Instantiate(ScriptModule &sm, const std::string &className) const
    {
        if (!sm.EnsureBuilt())
            return nullptr;

        asITypeInfo *type = sm.module->GetTypeInfoByName(className.c_str());
        if (!type)
        {
            Log::Error("Module {}: no class '{}'", sm.name, className);
            return nullptr;
        }

        asIScriptFunction *factory = type->GetFactoryByDecl((className + "@ " + className + "()").c_str());
        if (!factory)
        {
            Log::Error("Class '{}' has no default constructor", className);
            return nullptr;
        }

        asIScriptContext *ctx = engine->RequestContext();
        asIScriptObject *obj  = nullptr;
        if (ctx->Prepare(factory) >= 0 && ctx->Execute() == asEXECUTION_FINISHED)
        {
            obj = *static_cast<asIScriptObject **>(ctx->GetAddressOfReturnValue());
            if (obj)
                obj->AddRef(); // context drops its ref on ReturnContext
        }
        engine->ReturnContext(ctx);
        return obj;
    }
} // namespace SF::Engine
