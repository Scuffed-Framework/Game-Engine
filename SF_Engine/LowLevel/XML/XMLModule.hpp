#pragma once
#include <Engine/Module.hpp>
#include <functional>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlsave.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <LowLevel/Reflection/RTTI/RTTI.hpp>
#include <UtilityClasses/UUID.hpp>
#include <charconv>
#include <concepts>
#include <format>
#include <sstream>

namespace SF::Engine
{

    class XMLNode;
    class Serializable;

    struct XMLAttribute
    {
        std::string name;
        std::string value;
    };

    template<typename T>
    concept ComponentIndexable = requires(const T &v) {
        { T::length() } -> std::convertible_to<int>;
        { v[0] } -> std::convertible_to<float>;
    };

    class XMLNode
    {
    public:
        XMLNode() : node(nullptr), doc(nullptr) {}
        explicit XMLNode(xmlNodePtr ptr) : node(ptr), doc(nullptr) {}
        XMLNode(xmlNodePtr ptr, xmlDocPtr document) : node(ptr), doc(document) {}

        ~XMLNode() = default;

        [[nodiscard]] std::string GetName() const;
        [[nodiscard]] std::string GetContent() const;
        void SetContent(const std::string &content) const;

        [[nodiscard]] std::string GetAttribute(const std::string &name) const;

        template<typename T>
        bool GetAttribute(const std::string &name, T &out) const;
        template<typename T>
        void SetAttribute(const std::string &name, const T &value);

        template<typename T>
        bool GetChildContent(const std::string &childName, T &out) const;

        template<typename T>
        void SetChildContent(const std::string &childName, const T &value);

        // Bulk get attributes into a map
        [[nodiscard]] std::unordered_map<std::string, std::string>
        GetAttributes(const std::vector<std::string> &keys) const
        {
            std::unordered_map<std::string, std::string> result;
            for (const auto &key: keys)
            {
                std::string val;
                if (GetAttribute(key, val))
                    result[key] = val;
            }
            return result;
        }

        void SetAttributes(const std::unordered_map<std::string, std::string> &attrs)
        {
            for (const auto &[key, val]: attrs)
                SetAttribute(key, val);
        }

        // Child node operations
        [[nodiscard]] XMLNode GetFirstChild() const;
        [[nodiscard]] XMLNode GetNextSibling() const;
        [[nodiscard]] XMLNode GetChild(const std::string &name) const;
        [[nodiscard]] std::vector<XMLNode> GetChildren(const std::string &name = "") const;

        XMLNode AddChild(const std::string &name);
        XMLNode AddChild(const std::string &name, const std::string &content);

        // Validation
        [[nodiscard]] bool IsValid() const { return node != nullptr; }

        bool GetAttributeRaw(const std::string &name, std::string &out) const;
        void SetAttributeRaw(const std::string &name, const std::string &value) const;

    private:
        xmlNodePtr node;
        xmlDocPtr doc;
        friend class XMLModule;
    };

    class Serializable
    {
        SF_RTTI_BASE(Serializable)
    public:
        virtual ~Serializable() = default;
        virtual void Serialize(XMLNode &) const {}
        virtual void Deserialize(const XMLNode &) {}
    };

    class XMLModule : public ModuleRegistrar<XMLModule>
    {
        REGISTER_MODULE_EX(XMLModule, ModuleStage::Normal, ModuleStartupStage::PreEngineInitComplete, Requires<>{});

    public:
        XMLModule();
        ~XMLModule() override;

        XMLModule(const XMLModule &)            = delete;
        XMLModule &operator=(const XMLModule &) = delete;

        void Update() override {}

        bool LoadFromFile(const std::string &filename);
        bool SaveToFile(const std::string &filename) const;

        bool LoadFromString(const std::string &content);
        [[nodiscard]] std::string SaveToString() const;

        [[nodiscard]] XMLNode GetRootNode() const;
        void SetRootNode(const std::string &rootName);

        template<typename T>
        bool Serialize(const std::string &name, const T &object, const std::string &filename);

        template<typename T>
        bool Deserialize(const std::string &filename, T &object);

        // Type-specific serialization
        static std::string SerializeValue(int value);
        static std::string SerializeValue(float value);
        static std::string SerializeValue(bool value);
        static std::string SerializeValue(const std::string &value);
        static std::string SerializeValue(const UUID &value);

        static bool DeserializeValue(const std::string &str, int &out);
        static bool DeserializeValue(const std::string &str, float &out);
        static bool DeserializeValue(const std::string &str, bool &out);
        static bool DeserializeValue(const std::string &str, std::string &out);
        static bool DeserializeValue(const std::string &str, UUID &out);

        template<ComponentIndexable T>
        static std::string SerializeValue(const T &value)
        {
            std::string out;
            char buf[32];
            for (int i = 0; i < static_cast<int>(T::length()); ++i)
            {
                if (i != 0)
                    out += ' ';
                const auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<float>(value[i]));
                out.append(buf, end);
            }
            return out;
        }

        template<ComponentIndexable T>
        static bool DeserializeValue(const std::string &str, T &out)
        {
            std::istringstream ss(str);
            T tmp{};
            for (int i = 0; i < static_cast<int>(T::length()); ++i)
                if (!(ss >> tmp[i]))
                    return false;
            out = tmp;
            return true;
        }

        // Error handling
        [[nodiscard]] std::string GetLastError() const { return lastError; }

    private:
        xmlDocPtr document;
        xmlNodePtr rootNode;
        std::string lastError;

        void Clear();
        void SetError(const std::string &error);
        static void ErrorHandler(void *ctx, const char *msg, ...);
    };

    template<typename T>
    void XMLNode::SetAttribute(const std::string &name, const T &value)
    {
        if constexpr (std::is_convertible_v<const T &, std::string>) // std::string, const char*, char[N]
            SetAttributeRaw(name, std::string(value));
        else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
            SetAttributeRaw(name, std::to_string(value));
        else
            SetAttributeRaw(name, XMLModule::SerializeValue(value)); // bool, float, UUID, vec types
    }

    template<typename T>
    bool XMLNode::GetAttribute(const std::string &name, T &out) const
    {
        if constexpr (std::is_same_v<T, std::string>)
            return GetAttributeRaw(name, out);
        else
        {
            std::string raw;
            if (!GetAttributeRaw(name, raw))
                return false;

            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
            {
                T value{};
                const char *end      = raw.data() + raw.size();
                const auto [ptr, ec] = std::from_chars(raw.data(), end, value);
                if (ec != std::errc{} || ptr != end)
                    return false;
                out = value;
                return true;
            } else
                return XMLModule::DeserializeValue(raw, out);
        }
    }

    template<typename T>
    bool XMLNode::GetChildContent(const std::string &childName, T &out) const
    {
        XMLNode child = GetChild(childName);
        if (!child.IsValid())
            return false;
        return XMLModule::DeserializeValue(child.GetContent(), out);
    }

    template<typename T>
    void XMLNode::SetChildContent(const std::string &childName, const T &value)
    {
        std::string serialized = XMLModule::SerializeValue(value);
        XMLNode child          = GetChild(childName);
        if (child.IsValid())
            child.SetContent(serialized);
        else
            AddChild(childName, serialized);
    }
} // namespace SF::Engine

#include "XMLModule.inl"
