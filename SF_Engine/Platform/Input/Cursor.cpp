#include "Cursor.hpp"

#include <Assets/Bitmaps/Bitmap.hpp>
#include <Engine/Log/Log.hpp>
#include <memory>

namespace SF::Engine
{
    namespace
    {
        SDL_SystemCursor ToSdlCursor(const CursorStandard standard)
        {
            switch (standard)
            {
                case CursorStandard::Arrow:
                    return SDL_SYSTEM_CURSOR_DEFAULT;
                case CursorStandard::IBeam:
                    return SDL_SYSTEM_CURSOR_TEXT;
                case CursorStandard::Crosshair:
                    return SDL_SYSTEM_CURSOR_CROSSHAIR;
                case CursorStandard::Hand:
                    return SDL_SYSTEM_CURSOR_POINTER;
                case CursorStandard::ResizeX:
                    return SDL_SYSTEM_CURSOR_EW_RESIZE;
                case CursorStandard::ResizeY:
                    return SDL_SYSTEM_CURSOR_NS_RESIZE;
            }
            return SDL_SYSTEM_CURSOR_DEFAULT;
        }
    } // namespace

    Cursor::Cursor(const std::filesystem::path &filename, const CursorHotspot hotspot)
    {
        const Bitmap bitmap(filename);
        const int width  = static_cast<int>(bitmap.GetSize().x);
        const int height = static_cast<int>(bitmap.GetSize().y);

        int hotX = 0;
        int hotY = 0;
        switch (hotspot)
        {
            case CursorHotspot::UpperLeft:
                break;
            case CursorHotspot::UpperRight:
                hotX = width - 1;
                break;
            case CursorHotspot::BottomLeft:
                hotY = height - 1;
                break;
            case CursorHotspot::BottomRight:
                hotX = width - 1;
                hotY = height - 1;
                break;
            case CursorHotspot::Centered:
                hotX = width / 2;
                hotY = height / 2;
                break;
        }
        SDL_Surface *surface =
                SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, bitmap.GetData().get(), width * 4);
        if (!surface)
        {
            Log::Error("Cursor: failed to wrap '", filename.string(), "': ", SDL_GetError());
            return;
        }

        cursor = SDL_CreateColorCursor(surface, hotX, hotY);
        if (!cursor)
            Log::Error("Cursor: failed to create cursor from '", filename.string(), "': ", SDL_GetError());

        SDL_DestroySurface(surface);
    }

    Cursor::Cursor(const CursorStandard standard)
    {
        cursor = SDL_CreateSystemCursor(ToSdlCursor(standard));
        if (!cursor)
            Log::Error("Cursor: failed to create system cursor: ", SDL_GetError());
    }

    Cursor::~Cursor()
    {
        if (!cursor)
            return;

        if (SDL_GetCursor() == cursor)
            SDL_SetCursor(SDL_GetDefaultCursor());

        SDL_DestroyCursor(cursor);
        cursor = nullptr;
    }
} // namespace SF::Engine
