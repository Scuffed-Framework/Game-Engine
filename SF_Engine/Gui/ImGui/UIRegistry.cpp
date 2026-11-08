#include "UIRegistry.hpp"
#include <Gui/ImGui/ocornut/imgui.h>
#include <tracy/Tracy.hpp>

namespace SF::Engine
{
    // Register a raw draw function

    // Remove a previously registered window or function by handle
    void UIRegistry::Unregister(Handle handle)
    {
        std::erase_if(drawFns_, [handle](const auto &entry) { return entry.first == handle; });

        // Also clean up the type-dedup map if this was a typed registration
        std::erase_if(typeHandles_, [handle](const auto &entry) { return entry.second == handle; });
    }

    void UIRegistry::DrawAll()
    {
        ZoneScopedN("UIRegistry::DrawAll");
        for (auto &[handle, fn]: drawFns_)
        {
            ZoneScopedN("UI draw fn");
            ZoneValue(static_cast<uint64_t>(handle));
            fn();
        }
    }
}