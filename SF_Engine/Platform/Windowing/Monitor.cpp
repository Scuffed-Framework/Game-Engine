#include "Monitor.hpp"

#include <SDL3/SDL.h>
#include <cmath>

namespace SF::Engine
{
    namespace
    {
        VideoMode ToVideoMode(const SDL_DisplayMode &mode)
        {
            VideoMode out;
            out.width        = mode.w;
            out.height       = mode.h;
            out.refreshRate  = static_cast<int32_t>(std::lround(mode.refresh_rate));
            out.pixelDensity = mode.pixel_density;

            if (const SDL_PixelFormatDetails *details = SDL_GetPixelFormatDetails(mode.format))
            {
                out.redBits   = details->Rbits;
                out.greenBits = details->Gbits;
                out.blueBits  = details->Bbits;
            }
            return out;
        }
    } // namespace

    Monitor::Monitor(SDL_DisplayID monitor) : monitor(monitor) {}

    bool Monitor::IsPrimary() const { return monitor != 0 && monitor == SDL_GetPrimaryDisplay(); }

    UVec2 Monitor::GetWorkareaSize() const
    {
        SDL_Rect rect{};
        if (!SDL_GetDisplayUsableBounds(monitor, &rect))
            return {0u, 0u};
        return {static_cast<uint32_t>(rect.w), static_cast<uint32_t>(rect.h)};
    }

    UVec2 Monitor::GetWorkareaPosition() const
    {
        SDL_Rect rect{};
        if (!SDL_GetDisplayUsableBounds(monitor, &rect))
            return {0u, 0u};
        return {static_cast<uint32_t>(rect.x), static_cast<uint32_t>(rect.y)};
    }

    UVec2 Monitor::GetSize() const
    {
        SDL_Rect rect{};
        if (!SDL_GetDisplayBounds(monitor, &rect))
            return {0u, 0u};
        return {static_cast<uint32_t>(rect.w), static_cast<uint32_t>(rect.h)};
    }

    Vec2 Monitor::GetContentScale() const
    {
        const float scale = SDL_GetDisplayContentScale(monitor); // 0.0f on failure
        const float s     = scale > 0.0f ? scale : 1.0f;
        return {s, s};
    }

    UVec2 Monitor::GetPosition() const
    {
        SDL_Rect rect{};
        if (!SDL_GetDisplayBounds(monitor, &rect))
            return {0u, 0u};
        return {static_cast<uint32_t>(rect.x), static_cast<uint32_t>(rect.y)};
    }

    std::string Monitor::GetName() const
    {
        const char *name = SDL_GetDisplayName(monitor);
        return name ? name : "";
    }

    std::vector<VideoMode> Monitor::GetVideoModes() const
    {
        int count               = 0;
        SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(monitor, &count);
        if (!modes)
            return {};

        std::vector<VideoMode> result;
        result.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            result.push_back(ToVideoMode(*modes[i]));

        SDL_free(modes);
        return result;
    }

    VideoMode Monitor::GetVideoMode() const
    {
        const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(monitor);
        return mode ? ToVideoMode(*mode) : VideoMode{};
    }
} // namespace SF::Engine
