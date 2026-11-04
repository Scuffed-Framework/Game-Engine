#pragma once

#include <Math/Vectors/Vector.hpp>
#include <SDL3/SDL_video.h>
#include <cstdint>
#include <string>
#include <vector>
#include "../Input/Cursor.hpp"

namespace SF::Engine
{
    using namespace std;

    /**
     * @brief Class used to describe the video mode for a monitor.
     */
    class VideoMode
    {
    public:
        /// The width, in screen coordinates, of the video mode.
        int32_t width = 0;
        /// The height, in screen coordinates, of the video mode.
        int32_t height = 0;
        /// The bit depth of the red channel of the video mode.
        int32_t redBits = 0;
        /// The bit depth of the green channel of the video mode.
        int32_t greenBits = 0;
        /// The bit depth of the blue channel of the video mode.
        int32_t blueBits = 0;
        /// The refresh rate, in Hz, of the video mode.
        int32_t refreshRate = 0;
        /// Ratio of pixels to screen coordinates (multiply width/height by this for the real pixel size).
        float pixelDensity = 1.0f;
    };

    /**
     * @brief Class used to represent a monitor.
     */
    class Monitor
    {
        friend class Window;

    public:
        /// A default-constructed Monitor (ID 0) is invalid.
        explicit Monitor(SDL_DisplayID monitor = 0);

        [[nodiscard]] SDL_DisplayID GetMonitor() const { return monitor; }
        [[nodiscard]] bool IsValid() const { return monitor != 0; }

        [[nodiscard]] bool IsPrimary() const;

        /**
         * Gets the size of the usable area of the monitor (excludes taskbars, docks, notches, etc.).
         * @return The size, in screen coordinates, of the usable area.
         */
        [[nodiscard]] UVec2 GetWorkareaSize() const;

        /**
         * Gets the position of the usable area of the monitor.
         * @return The position of the usable area on the virtual screen.
         */
        [[nodiscard]] UVec2 GetWorkareaPosition() const;

        /**
         * Gets the size of the monitor.
         * @return The size, in screen coordinates, of the display.
         * @note SDL does not expose the physical size in millimetres.
         */
        [[nodiscard]] UVec2 GetSize() const;

        /**
         * Gets the content scale of the monitor.
         * @return The content scale (SDL provides a single uniform value, so x == y).
         */
        [[nodiscard]] Vec2 GetContentScale() const;

        /**
         * Gets the position of the monitor's viewport on the virtual screen.
         * @return The position of the monitor's viewport.
         */
        [[nodiscard]] UVec2 GetPosition() const;

        /**
         * Gets the name of this monitor.
         * @return The name of the monitor.
         */
        [[nodiscard]] string GetName() const;

        /**
         * Gets the available fullscreen video modes for this monitor.
         * @return The video modes supported by this monitor.
         */
        [[nodiscard]] std::vector<VideoMode> GetVideoModes() const;

        /**
         * Gets the current mode of this monitor.
         * @return The current video mode of this monitor.
         */
        [[nodiscard]] VideoMode GetVideoMode() const;

    private:
        SDL_DisplayID monitor;
    };
} // namespace SF::Engine
