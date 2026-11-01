#pragma once
#include <filesystem>

#include <SDL3/SDL.h>
#include <UtilityClasses/NoCopy.hpp>

namespace SF::Engine
{
    enum class CursorHotspot : uint8_t
    {
        UpperLeft,
        UpperRight,
        BottomLeft,
        BottomRight,
        Centered
    };

    enum class CursorStandard : uint8_t
    {
        Arrow,
        IBeam,
        Crosshair,
        Hand,
        ResizeX,
        ResizeY
    };

    class Cursor : NoCopy
    {
        friend class Window;

    public:
        /**
         * Sets the cursor to a image file.
         * @param filename The new custom mouse file.
         * @param hotspot The hotspot to display the cursor image at.
         */
        Cursor(const std::filesystem::path &filename, CursorHotspot hotspot);
        /**
         * Sets the cursor to a system style.
         * @param standard The standard shape.
         */
        Cursor(CursorStandard standard);
        ~Cursor() override;

    private:
        SDL_Cursor *cursor = nullptr;
    };
} // namespace SF::Engine
