#pragma once
#include <Assets/AssetPipeline.hpp>
#include <LowLevel/FileSystem/File.hpp>

#include <angelscript.h>

namespace SF::Engine
{
    class ScriptAsset : public AssetBase
    {
        SF_RTTI(ScriptAsset, AssetBase)
    public:
        asIScriptObject *Script = nullptr;
        std::string ModuleName;
        std::string Source;

        void Save() override {}

        void Serialize(XMLNode &node) const override
        {
            XMLNode script = node.AddChild("Script");
            script.SetAttribute("ModuleName", ModuleName);
        }

        void Deserialize(const XMLNode &node) override
        {
            XMLNode script = node.GetChild("Script");
            script.GetAttribute("ModuleName", ModuleName);
        }

        bool Load(std::span<const uint8_t>) override;

        void ExecuteFunction(const std::string &exec) const;
    };
} // namespace SF::Engine
