#include "EngineRenderpassManager.hpp"

#include <cstdlib>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <tracy/Tracy.hpp>
#if defined(__GNUC__) || defined(__clang__)
    #include <cxxabi.h>
#endif

namespace SF::Engine
{
    namespace
    {
        struct PassZoneNames
        {
            std::string pre;
            std::string render;
        };

        // Demangled once per pass type; Tracy copies transient zone names, so these are only read.
        const PassZoneNames &GetPassZoneNames(const EngineRenderpass &pass)
        {
            static std::unordered_map<std::type_index, PassZoneNames> cache;
            const std::type_index key(typeid(pass));
            if (const auto it = cache.find(key); it != cache.end())
                return it->second;

            std::string name = key.name();
#if defined(__GNUC__) || defined(__clang__)
            int status = 0;
            if (char *demangled = abi::__cxa_demangle(key.name(), nullptr, nullptr, &status))
            {
                if (status == 0)
                    name = demangled;
                std::free(demangled);
            }
#endif
            constexpr std::string_view kPrefix = "SF::Engine::";
            if (name.starts_with(kPrefix))
                name.erase(0, kPrefix.size());

            return cache.emplace(key, PassZoneNames{name + " [Pre]", name + " [Render]"}).first->second;
        }
    } // namespace

    void EngineRenderpassManager::Clear() { stages.clear(); }

    void EngineRenderpassManager::RemoveSFERenderpassStage(const TypeId &id)
    {
        for (auto it = stages.begin(); it != stages.end();)
        {
            if (it->second == id)
            {
                it = stages.erase(it);
            } else
            {
                ++it;
            }
        }
    }

    void EngineRenderpassManager::PreRenderStage(const Pipeline::Stage &stage, const CommandBuffer &commandBuffer)
    {
        for (const auto &[stageIndex, typeId]: stages)
        {
            if (stageIndex.first != stage)
                continue;

            if (auto &SFERenderpass = SFERenderpasses[typeId])
                if (SFERenderpass->IsEnabled())
                {
                    ZoneTransientN(passZone, GetPassZoneNames(*SFERenderpass).pre.c_str(), true);
                    SFERenderpass->PreRender(commandBuffer);
                }
        }
    }

    void EngineRenderpassManager::RenderStage(const Pipeline::Stage &stage, const CommandBuffer &commandBuffer)
    {
        for (const auto &[stageIndex, typeId]: stages)
        {
            if (stageIndex.first != stage)
            {
                continue;
            }

            if (auto &SFERenderpass = SFERenderpasses[typeId])
            {
                if (SFERenderpass->IsEnabled())
                {
                    ZoneTransientN(passZone, GetPassZoneNames(*SFERenderpass).render.c_str(), true);
                    SFERenderpass->Render(commandBuffer);
                }
            }
        }
    }
} // namespace SF::Engine
