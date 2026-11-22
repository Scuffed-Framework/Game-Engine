#pragma once
#include <Engine/Log/Log.hpp>
#include <Engine/Module.hpp>

#include <angelscript.h>
#include "ScriptAsset.hpp"

#include <algorithm>
#include <cstring>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef Always
    #undef Always
#endif

namespace SF::Engine
{
    // Binary stream for AngelScript bytecode. Backed by a File or by memory.
    class ByteCodeStream : public asIBinaryStream
    {
    public:
        // Non-owning: the File must outlive the stream.
        explicit ByteCodeStream(File *file) : in(file) {}
        explicit ByteCodeStream(std::span<const uint8_t> data) : in(data) {}

        // A bare void* has no length, so it can't be bounds-checked. Take a size and store a span instead.
        ByteCodeStream(const void *data, size_t size) :
            in(std::span<const uint8_t>(static_cast<const uint8_t *>(data), size))
        {
        }

        int Write(const void *ptr, asUINT size) override
        {
            if (size == 0)
                return 0;

            auto *file = std::get_if<File *>(&in);
            if (!file || !*file)
                return -1; // memory streams are read-only

            (*file)->Write(ptr, size);
            return 0; // AngelScript expects 0 on success, negative on failure
        }

        int Read(void *ptr, asUINT size) override
        {
            if (size == 0)
                return 0;

            return std::visit(
                    [&]<typename T0>(T0 &&src) -> int
                    {
                        using T = std::decay_t<T0>;

                        if constexpr (std::is_same_v<T, File *>)
                        {
                            if (!src)
                                return -1;
                            src->Read(ptr, size);
                            return 0;
                        } else if constexpr (std::is_same_v<T, std::span<const uint8_t>>)
                        {
                            if (cursor + size > src.size())
                                return -1;
                            std::memcpy(ptr, src.data() + cursor, size);
                            cursor += size;
                            return 0;
                        } else
                        {
                            return -1; // void*: unknown length, refuse rather than read out of bounds
                        }
                    },
                    in);
        }

    private:
        InputDataVariant in;
        size_t cursor = 0; // only used by the span alternative
    };

    class ScriptModule : public Serializable
    {
    public:
        using LoadingStage = ModuleStartupStage;

        explicit ScriptModule(std::string moduleName) : name(std::move(moduleName)) {}

        // Scripts are non-owning: assets live in the asset pipeline.
        void AddScript(ScriptAsset &asset)
        {
            if (!asset.ModuleName.empty() && asset.ModuleName != name)
                Log::Warning("Script {} assigned to module {}, requested {}", asset.name, name, asset.ModuleName);
            if (std::ranges::find(scripts, &asset) == scripts.end())
            {
                scripts.push_back(&asset);
                dirty = true;
            }
        }

        void RemoveScript(ScriptAsset &asset)
        {
            if (std::erase(scripts, &asset))
                dirty = true;
        }

        /// (Re)compiles every script section. NOTE: ALWAYS_CREATE discards the old module,
        /// so existing script objects become invalid -> release them before hot reload.
        bool Build();

        /// Builds only if scripts changed since the last successful build.
        bool EnsureBuilt() { return (module && !dirty) || Build(); }

        bool SaveByteCodeToDisk(bool stripDebug);
        bool LoadByteCodeFromDisk();
        bool LoadByteCodeFromSpan(std::span<const uint8_t> span);
        bool LoadByteCodeFromMemory(const void *mem, size_t size);

        std::string name;
        bool wasDebugInfoStripped = false;
        bool dirty                = true;
        std::vector<ScriptAsset *> scripts;
        asIScriptModule *module = nullptr;

        void Serialize(XMLNode &m) const override
        {
            XMLNode script = m.AddChild("ScriptModule");
            script.SetAttribute("Name", name);
            script.SetAttribute("Dirty", dirty);
            for (const auto s: scripts)
                script.SetAttributes({{"Script", s->uuid.ToString()}});
        }

        void Deserialize(const XMLNode &m) override
        {
            const XMLNode script = m.GetChild("ScriptModule");
            script.GetAttribute("Name", name);

            scripts.clear();
            for (const XMLNode &child: script.GetChildren("Script"))
            {
                std::string raw;
                if (!child.GetAttribute("UUID", raw))
                    continue;

                UUID id;
                try
                {
                    id = UUID::FromString(raw);
                } catch (const std::exception &)
                {
                    Log::Warning("ScriptModule '{}': malformed script id '{}'; ignored.", name, raw);
                    continue;
                }

                auto base  = AssetController::Get()->FindByUUID(id);
                auto asset = ::SF::Reflection::rtti_pointer_cast<ScriptAsset>(base);
                if (!asset)
                {
                    Log::Warning("ScriptModule '{}': script asset {} not found or not a ScriptAsset.", name, raw);
                    continue;
                }

                AddScript(*asset); // non-owning; AssetController keeps it alive. Deduplicates and sets dirty.
            }
        }
    };

    class ScriptEngine : public ModuleRegistrar<ScriptEngine>
    {
        friend class ModuleRegistrar<ScriptEngine>;
        REGISTER_MODULE_EX(ScriptEngine, Stage::Always, ModuleStartupStage::PreEngineInitComplete);

    public:
        bool Initialize() override;
        void Shutdown() override;
        void Update() override;

        ScriptModule &GetOrCreateModule(const std::string &moduleName);
        ScriptModule *FindScriptModule(const ScriptAsset &asset);

        /// Runs a function (optionally as a method on `object`) on a pooled context.
        bool Execute(asIScriptFunction *func, asIScriptObject *object = nullptr,
                     const std::function<void(asIScriptContext *)> &setArgs = {}) const;

        /// Creates an instance of a script class via its default factory. Caller owns one ref (Release()).
        asIScriptObject *Instantiate(ScriptModule &module, const std::string &className) const;

        std::unordered_map<std::string, ScriptModule> modules;
        asIScriptEngine *engine = nullptr;
    };
} // namespace SF::Engine
