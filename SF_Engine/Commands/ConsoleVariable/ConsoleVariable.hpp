#pragma once
#include <charconv>
#include <ranges>
#include <string>
#include <string_view>

#include <Engine/Module.hpp>
#include <LowLevel/XML/XMLModule.hpp>
#include <UtilityClasses/StreamFactory.hpp>

#ifdef Always
    #undef Always
#endif

namespace SF::Engine
{

    inline constexpr std::string_view CommandWindowConsoleVariablePrefix{"CVar::", 6};

    class ConsoleVariableRegistry;
    struct IConsoleVariable : public Serializable
    {
    public:
        ~IConsoleVariable() override = default;

        [[nodiscard]] virtual std::string GetFullName() const = 0;
        [[nodiscard]] virtual bool DidValueChange() const     = 0;

        virtual bool SetValueFromString(std::string_view str)   = 0;
        [[nodiscard]] virtual std::string ValueToString() const = 0;
    };

    template<typename V>
    struct ConsoleVariable : public IConsoleVariable
    {
        SF_RTTI(IConsoleVariable, ConsoleVariable)

    public:
        std::string module;
        std::string name;
        V Value;
        V LastValue;

        ConsoleVariable(std::string module, std::string name, V defaultValue);
        ~ConsoleVariable() override = default;

        [[nodiscard]] bool DidValueChange() const override { return !(Value == LastValue); }

        [[nodiscard]] std::string GetFullName() const override { return module + "." + name; }

        void ChangeValue(const V &New)
        {
            LastValue = Value; // so DidValueChange() means something next frame
            Value     = New;
        }

        bool SetValueFromString(std::string_view str) override
        {
            if constexpr (std::is_same_v<V, bool>)
            {
                if (str == "true" || str == "1")
                {
                    ChangeValue(true);
                    return true;
                }
                if (str == "false" || str == "0")
                {
                    ChangeValue(false);
                    return true;
                }
                return false;
            } else if constexpr (std::is_arithmetic_v<V>)
            {
                V parsed{};
                auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), parsed);
                if (ec != std::errc{})
                    return false;
                ChangeValue(parsed);
                return true;
            } else if constexpr (std::is_constructible_v<std::string, std::string_view>)
            {
                ChangeValue(std::string(str.data()));
                return true;
            }
        }

        [[nodiscard]] std::string ValueToString() const override
        {
            if constexpr (std::is_same_v<V, bool>)
            {
                return Value ? "true" : "false";
            } else if constexpr (std::is_arithmetic_v<V>)
            {
                return string(std::to_string(Value));
            } else
            {
                return Value;
            }
        }

        void Serialize(XMLNode &node) const override
        {
            XMLNode CVar = node.AddChild("CVar");
            CVar.SetAttribute("Module", module);
            CVar.SetAttribute("Name", name);
            CVar.SetAttribute("Value", Value);
            CVar.SetAttribute("LastValue", LastValue);
        }

        void Deserialize(const XMLNode &node) override
        {
            XMLNode CVar = node.GetChild("CVar");
            CVar.GetAttribute("Module", module);
            CVar.GetAttribute("Name", name);
            CVar.GetAttribute("Value", Value);
            CVar.GetAttribute("LastValue", LastValue);
        }
    };

    class ConsoleVariableRegistry : public ModuleRegistrar<ConsoleVariableRegistry>
    {
        REGISTER_MODULE_EX(ConsoleVariableRegistry, ModuleStage::Always, ModuleStartupStage::OnEngineInit);
        SF_RTTI(Module, ConsoleVariableRegistry)

        inline static std::unordered_map<std::string, IConsoleVariable *> s_cvars;

    public:
        static void RegisterCVar(const std::string &fullName, IConsoleVariable *cvar) { s_cvars[fullName] = cvar; }

        [[nodiscard]] static IConsoleVariable *Find(std::string_view fullName)
        {
            const auto it = s_cvars.find(fullName.data());
            return it != s_cvars.end() ? it->second : nullptr;
        }

        bool Initialize() override { return true; }
        void Update() override {}

        [[nodiscard]] Stage GetStage() const override { return ModuleStage::Always; }
        [[nodiscard]] std::string_view GetName() const override { return RTTI_GetTypeName(); }

        // NOLINTBEGIN(readability-convert-member-functions-to-static)
        void Serialize(XMLNode &node) const
        {
            XMLNode registryNode = node.AddChild("ConsoleVariables");
            for (const auto &cvar: s_cvars | std::views::values)
                cvar->Serialize(registryNode);
        }

        void Deserialize(const XMLNode &node)
        {
            XMLNode registryNode = node.GetChild("ConsoleVariables");
            for (XMLNode child = registryNode.GetFirstChild(); child.IsValid(); child = child.GetNextSibling())
            {
                std::string mod, nm;
                child.GetAttribute("Module", mod);
                child.GetAttribute("Name", nm);
                if (IConsoleVariable *cvar = Find(std::string_view(mod + "." += nm)))
                    cvar->Deserialize(registryNode); // each cvar re-finds its own <CVar> child
            }
        }
        // NOLINTEND(readability-convert-member-functions-to-static)

        Define_TypeId_Function(Module, ConsoleVariableRegistry)
    };
} // namespace SF::Engine
