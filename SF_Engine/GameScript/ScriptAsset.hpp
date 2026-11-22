#pragma once
#include <Assets/AssetPipeline.hpp>
#include <LowLevel/FileSystem/File.hpp>

#include <angelscript.h>

#include <ctype.h>
#include <span>
#include <string>

#include "UtilityClasses/StlExtensions.hpp"

namespace SF::Engine
{
    class ScriptAsset : public AssetBase, public DragAndDropObjectTag
    {
        SF_RTTI(ScriptAsset, AssetBase)
    public:
        ~ScriptAsset() override;

        std::string ModuleName; // empty = default module
        std::string ClassName;  // script class instantiated by ScriptComponent
        std::string Source;

        void Save() override {}

        void Serialize(XMLNode &node) const override
        {
            AssetBase::Serialize(node);
            XMLNode script = node.AddChild("Script");
            script.SetAttribute("ModuleName", ModuleName);
            script.SetAttribute("ClassName", ClassName);
        }

        void Deserialize(const XMLNode &node) override
        {
            AssetBase::Deserialize(node);
            XMLNode script = node.GetChild("Script");
            script.GetAttribute("ModuleName", ModuleName);
            script.GetAttribute("ClassName", ClassName);
        }

        /// Stores the source and registers this asset with its ScriptModule (build is lazy).
        bool Load(std::span<const uint8_t> data) override;

        /// Runs a global function, e.g. "void Start()".
        void ExecuteFunction(const std::string &decl) const;

        /// Creates an instance of ClassName. Caller must Release() it.
        [[nodiscard]] asIScriptObject *Instantiate() const;
    };

    inline std::optional<std::string_view> GetDefaultArgText(const asIScriptFunction *fn, const asUINT index = 0)
    {
        if (!fn || index >= fn->GetParamCount())
            return std::nullopt;

        const char *def = nullptr;
        if (fn->GetParam(index, nullptr, nullptr, nullptr, &def) < 0 || !def || !*def)
            return std::nullopt;

        std::string_view text = def;
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
            text.remove_prefix(1);
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            text.remove_suffix(1);
        return text.empty() ? std::nullopt : std::optional{text};
    }
    namespace detail
    {
        template<typename T>
        constexpr const char *ScriptTypeName()
        {
            if constexpr (std::is_same_v<T, bool>)
                return "bool";
            else if constexpr (std::is_same_v<T, float>)
                return "float";
            else if constexpr (std::is_same_v<T, double>)
                return "double";
            else if constexpr (std::is_same_v<T, std::string>)
                return "string"; // needs scriptstdstring add-on
            else if constexpr (std::is_integral_v<T>)
            {
                if constexpr (std::is_signed_v<T>)
                    return sizeof(T) == 1 ? "int8" : sizeof(T) == 2 ? "int16" : sizeof(T) == 4 ? "int" : "int64";
                else
                    return sizeof(T) == 1 ? "uint8" : sizeof(T) == 2 ? "uint16" : sizeof(T) == 4 ? "uint" : "uint64";
            } else
                static_assert(!sizeof(T), "unsupported script argument type");
        }

        template<typename T>
        T ReadReturn(asIScriptContext *ctx)
        {
            if constexpr (std::is_same_v<T, bool>)
                return ctx->GetReturnByte() != 0;
            else if constexpr (std::is_same_v<T, float>)
                return ctx->GetReturnFloat();
            else if constexpr (std::is_same_v<T, double>)
                return ctx->GetReturnDouble();
            else if constexpr (std::is_same_v<T, std::string>)
                return *static_cast<std::string *>(ctx->GetReturnObject());
            else if constexpr (sizeof(T) == 1)
                return static_cast<T>(ctx->GetReturnByte());
            else if constexpr (sizeof(T) == 2)
                return static_cast<T>(ctx->GetReturnWord());
            else if constexpr (sizeof(T) == 4)
                return static_cast<T>(ctx->GetReturnDWord());
            else
                return static_cast<T>(ctx->GetReturnQWord());
        }
    } // namespace detail

    /// Evaluates the default-argument expression of parameter `index` as T.
    /// Any valid AngelScript expression is allowed. Returns nullopt if there is no default,
    /// the function has no module (application-registered), or the expression fails to compile.
    template<typename T>
    std::optional<T> GetDefaultArg(const asIScriptFunction *fn, const asUINT index = 0)
    {
        const auto text = GetDefaultArgText(fn, index);
        if (!text)
            return std::nullopt;

        asIScriptModule *mod = fn->GetModule();
        if (!mod)
            return std::nullopt;

        const std::string code =
                std::string(detail::ScriptTypeName<T>()) + " __SFDefaultArg() { return (" + std::string(*text) + "); }";

        asIScriptFunction *tmp = nullptr; // not added to the module (flags = 0)
        if (mod->CompileFunction("__SFDefaultArg", code.c_str(), 0, 0, &tmp) < 0 || !tmp)
            return std::nullopt;

        asIScriptEngine *engine = fn->GetEngine();
        asIScriptContext *ctx   = engine->RequestContext();

        std::optional<T> result;
        if (ctx->Prepare(tmp) >= 0 && ctx->Execute() == asEXECUTION_FINISHED)
            result = detail::ReadReturn<T>(ctx); // read before ReturnContext resets it

        engine->ReturnContext(ctx);
        tmp->Release();
        return result;
    }
} // namespace SF::Engine
