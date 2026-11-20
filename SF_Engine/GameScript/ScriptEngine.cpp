#include "ScriptEngine.hpp"

namespace SF::Engine
{
    ScriptModule &ScriptEngine::GetOrCreateModule(const std::string &moduleName)
    {
        auto [it, inserted] = modules.try_emplace(moduleName);
        if (inserted)
            it->second.name = moduleName;
        return it->second;
    }

    ScriptModule *ScriptEngine::FindScriptModule(const ScriptAsset &asset)
    {
        const auto it = modules.find(asset.ModuleName);
        return it != modules.end() ? &it->second : nullptr;
    }
} // namespace SF::Engine
