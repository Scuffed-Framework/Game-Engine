#include <Rendering/Material/Color/Color.hpp>
#include "WindowManager.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#if _PLATFORM_WINDOWS
    #pragma comment(lib, "dwmapi.lib")
    #include <dwmapi.h>
#endif

#include <Engine/Engine.hpp>
#include <Gui/ImGui/ocornut/imgui.h>
#include <Gui/ImGui/ocornut/imgui_impl_sdl3.h>
#include <Rendering/RenderSystem.hpp>
#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>

namespace SF::Engine
{
    namespace
    {
        constexpr auto kWindowProperty = "SFE.Window";

        bool ImGuiReady() { return ImGui::GetCurrentContext() != nullptr; }

        struct KeyMapping
        {
            SDL_Scancode scancode;
            Key key;
        };

        constexpr KeyMapping kKeyMap[] = {
                {.scancode = SDL_SCANCODE_SPACE, .key = Key::Space},
                {.scancode = SDL_SCANCODE_APOSTROPHE, .key = Key::Apostrophe},
                {.scancode = SDL_SCANCODE_COMMA, .key = Key::Comma},
                {.scancode = SDL_SCANCODE_MINUS, .key = Key::Minus},
                {.scancode = SDL_SCANCODE_PERIOD, .key = Key::Period},
                {.scancode = SDL_SCANCODE_SLASH, .key = Key::Slash},
                {.scancode = SDL_SCANCODE_0, .key = Key::_0},
                {.scancode = SDL_SCANCODE_1, .key = Key::_1},
                {.scancode = SDL_SCANCODE_2, .key = Key::_2},
                {.scancode = SDL_SCANCODE_3, .key = Key::_3},
                {.scancode = SDL_SCANCODE_4, .key = Key::_4},
                {.scancode = SDL_SCANCODE_5, .key = Key::_5},
                {.scancode = SDL_SCANCODE_6, .key = Key::_6},
                {.scancode = SDL_SCANCODE_7, .key = Key::_7},
                {.scancode = SDL_SCANCODE_8, .key = Key::_8},
                {.scancode = SDL_SCANCODE_9, .key = Key::_9},
                {.scancode = SDL_SCANCODE_SEMICOLON, .key = Key::Semicolon},
                {.scancode = SDL_SCANCODE_EQUALS, .key = Key::Equal},
                {.scancode = SDL_SCANCODE_A, .key = Key::A},
                {.scancode = SDL_SCANCODE_B, .key = Key::B},
                {.scancode = SDL_SCANCODE_C, .key = Key::C},
                {.scancode = SDL_SCANCODE_D, .key = Key::D},
                {.scancode = SDL_SCANCODE_E, .key = Key::E},
                {.scancode = SDL_SCANCODE_F, .key = Key::F},
                {.scancode = SDL_SCANCODE_G, .key = Key::G},
                {.scancode = SDL_SCANCODE_H, .key = Key::H},
                {.scancode = SDL_SCANCODE_I, .key = Key::I},
                {.scancode = SDL_SCANCODE_J, .key = Key::J},
                {.scancode = SDL_SCANCODE_K, .key = Key::K},
                {.scancode = SDL_SCANCODE_L, .key = Key::L},
                {.scancode = SDL_SCANCODE_M, .key = Key::M},
                {.scancode = SDL_SCANCODE_N, .key = Key::N},
                {.scancode = SDL_SCANCODE_O, .key = Key::O},
                {.scancode = SDL_SCANCODE_P, .key = Key::P},
                {.scancode = SDL_SCANCODE_Q, .key = Key::Q},
                {.scancode = SDL_SCANCODE_R, .key = Key::R},
                {.scancode = SDL_SCANCODE_S, .key = Key::S},
                {.scancode = SDL_SCANCODE_T, .key = Key::T},
                {.scancode = SDL_SCANCODE_U, .key = Key::U},
                {.scancode = SDL_SCANCODE_V, .key = Key::V},
                {.scancode = SDL_SCANCODE_W, .key = Key::W},
                {.scancode = SDL_SCANCODE_X, .key = Key::X},
                {.scancode = SDL_SCANCODE_Y, .key = Key::Y},
                {.scancode = SDL_SCANCODE_Z, .key = Key::Z},
                {.scancode = SDL_SCANCODE_LEFTBRACKET, .key = Key::LeftBracket},
                {.scancode = SDL_SCANCODE_BACKSLASH, .key = Key::Backslash},
                {.scancode = SDL_SCANCODE_RIGHTBRACKET, .key = Key::RightBracket},
                {.scancode = SDL_SCANCODE_GRAVE, .key = Key::GraveAccent},
                {.scancode = SDL_SCANCODE_NONUSBACKSLASH, .key = Key::World1},
                {.scancode = SDL_SCANCODE_NONUSHASH, .key = Key::World2},
                {.scancode = SDL_SCANCODE_ESCAPE, .key = Key::Escape},
                {.scancode = SDL_SCANCODE_RETURN, .key = Key::Enter},
                {.scancode = SDL_SCANCODE_TAB, .key = Key::Tab},
                {.scancode = SDL_SCANCODE_BACKSPACE, .key = Key::Backspace},
                {.scancode = SDL_SCANCODE_INSERT, .key = Key::Insert},
                {.scancode = SDL_SCANCODE_DELETE, .key = Key::Delete},
                {.scancode = SDL_SCANCODE_RIGHT, .key = Key::Right},
                {.scancode = SDL_SCANCODE_LEFT, .key = Key::Left},
                {.scancode = SDL_SCANCODE_DOWN, .key = Key::Down},
                {.scancode = SDL_SCANCODE_UP, .key = Key::Up},
                {.scancode = SDL_SCANCODE_PAGEUP, .key = Key::PageUp},
                {.scancode = SDL_SCANCODE_PAGEDOWN, .key = Key::PageDown},
                {.scancode = SDL_SCANCODE_HOME, .key = Key::Home},
                {.scancode = SDL_SCANCODE_END, .key = Key::End},
                {.scancode = SDL_SCANCODE_CAPSLOCK, .key = Key::CapsLock},
                {.scancode = SDL_SCANCODE_SCROLLLOCK, .key = Key::ScrollLock},
                {.scancode = SDL_SCANCODE_NUMLOCKCLEAR, .key = Key::NumLock},
                {.scancode = SDL_SCANCODE_PRINTSCREEN, .key = Key::PrintScreen},
                {.scancode = SDL_SCANCODE_PAUSE, .key = Key::Pause},
                {.scancode = SDL_SCANCODE_F1, .key = Key::F1},
                {.scancode = SDL_SCANCODE_F2, .key = Key::F2},
                {.scancode = SDL_SCANCODE_F3, .key = Key::F3},
                {.scancode = SDL_SCANCODE_F4, .key = Key::F4},
                {.scancode = SDL_SCANCODE_F5, .key = Key::F5},
                {.scancode = SDL_SCANCODE_F6, .key = Key::F6},
                {.scancode = SDL_SCANCODE_F7, .key = Key::F7},
                {.scancode = SDL_SCANCODE_F8, .key = Key::F8},
                {.scancode = SDL_SCANCODE_F9, .key = Key::F9},
                {.scancode = SDL_SCANCODE_F10, .key = Key::F10},
                {.scancode = SDL_SCANCODE_F11, .key = Key::F11},
                {.scancode = SDL_SCANCODE_F12, .key = Key::F12},
                {.scancode = SDL_SCANCODE_F13, .key = Key::F13},
                {.scancode = SDL_SCANCODE_F14, .key = Key::F14},
                {.scancode = SDL_SCANCODE_F15, .key = Key::F15},
                {.scancode = SDL_SCANCODE_F16, .key = Key::F16},
                {.scancode = SDL_SCANCODE_F17, .key = Key::F17},
                {.scancode = SDL_SCANCODE_F18, .key = Key::F18},
                {.scancode = SDL_SCANCODE_F19, .key = Key::F19},
                {.scancode = SDL_SCANCODE_F20, .key = Key::F20},
                {.scancode = SDL_SCANCODE_F21, .key = Key::F21},
                {.scancode = SDL_SCANCODE_F22, .key = Key::F22},
                {.scancode = SDL_SCANCODE_F23, .key = Key::F23},
                {.scancode = SDL_SCANCODE_F24, .key = Key::F24},
                // Key::F25 has no SDL equivalent.
                {.scancode = SDL_SCANCODE_KP_0, .key = Key::Numpad0},
                {.scancode = SDL_SCANCODE_KP_1, .key = Key::Numpad1},
                {.scancode = SDL_SCANCODE_KP_2, .key = Key::Numpad2},
                {.scancode = SDL_SCANCODE_KP_3, .key = Key::Numpad3},
                {.scancode = SDL_SCANCODE_KP_4, .key = Key::Numpad4},
                {.scancode = SDL_SCANCODE_KP_5, .key = Key::Numpad5},
                {.scancode = SDL_SCANCODE_KP_6, .key = Key::Numpad6},
                {.scancode = SDL_SCANCODE_KP_7, .key = Key::Numpad7},
                {.scancode = SDL_SCANCODE_KP_8, .key = Key::Numpad8},
                {.scancode = SDL_SCANCODE_KP_9, .key = Key::Numpad9},
                {.scancode = SDL_SCANCODE_KP_PERIOD, .key = Key::NumpadDecimal},
                {.scancode = SDL_SCANCODE_KP_DIVIDE, .key = Key::NumpadDivide},
                {.scancode = SDL_SCANCODE_KP_MULTIPLY, .key = Key::NumpadMultiply},
                {.scancode = SDL_SCANCODE_KP_MINUS, .key = Key::NumpadSubtract},
                {.scancode = SDL_SCANCODE_KP_PLUS, .key = Key::NumpadAdd},
                {.scancode = SDL_SCANCODE_KP_ENTER, .key = Key::NumpadEnter},
                {.scancode = SDL_SCANCODE_KP_EQUALS, .key = Key::NumpadEqual},
                {.scancode = SDL_SCANCODE_LSHIFT, .key = Key::ShiftLeft},
                {.scancode = SDL_SCANCODE_LCTRL, .key = Key::ControlLeft},
                {.scancode = SDL_SCANCODE_LALT, .key = Key::AltLeft},
                {.scancode = SDL_SCANCODE_LGUI, .key = Key::SuperLeft},
                {.scancode = SDL_SCANCODE_RSHIFT, .key = Key::ShiftRight},
                {.scancode = SDL_SCANCODE_RCTRL, .key = Key::ControlRight},
                {.scancode = SDL_SCANCODE_RALT, .key = Key::AltRight},
                {.scancode = SDL_SCANCODE_RGUI, .key = Key::SuperRight},
                {.scancode = SDL_SCANCODE_APPLICATION, .key = Key::Menu},
        };

        constexpr auto kUnknownKey = static_cast<Key>(-1);

        Key ToKey(const SDL_Scancode scancode)
        {
            static constexpr array<Key, SDL_SCANCODE_COUNT> table = []
            {
                array<Key, SDL_SCANCODE_COUNT> t{};
                t.fill(kUnknownKey);
                for (const auto &[code, key]: kKeyMap)
                    t[static_cast<size_t>(code)] = key;
                return t;
            }();
            return (scancode > 0 && scancode < SDL_SCANCODE_COUNT) ? table[static_cast<size_t>(scancode)] : kUnknownKey;
        }

        SDL_Scancode ToScancode(Key key)
        {
            static const unordered_map<int32_t, SDL_Scancode> table = []
            {
                unordered_map<int32_t, SDL_Scancode> t;
                for (const auto &[scancode, k]: kKeyMap)
                    t[static_cast<int32_t>(k)] = scancode;
                return t;
            }();
            const auto it = table.find(static_cast<int32_t>(key));
            return it != table.end() ? it->second : SDL_SCANCODE_UNKNOWN;
        }

        int32_t FromSdlButton(uint8_t button)
        {
            switch (button)
            {
                case SDL_BUTTON_LEFT:
                    return 0;
                case SDL_BUTTON_RIGHT:
                    return 1;
                case SDL_BUTTON_MIDDLE:
                    return 2;
                case SDL_BUTTON_X1:
                    return 3;
                case SDL_BUTTON_X2:
                    return 4;
                default:
                    return -1;
            }
        }

        int32_t ToSdlButton(const int32_t button)
        {
            switch (button)
            {
                case 0:
                    return SDL_BUTTON_LEFT;
                case 1:
                    return SDL_BUTTON_RIGHT;
                case 2:
                    return SDL_BUTTON_MIDDLE;
                case 3:
                    return SDL_BUTTON_X1;
                case 4:
                    return SDL_BUTTON_X2;
                default:
                    return 0;
            }
        }

        Bitmask<InputMod> ToMods(const SDL_Keymod mod)
        {
            int32_t mods = 0;
            if (mod & SDL_KMOD_SHIFT)
                mods |= 0x0001;
            if (mod & SDL_KMOD_CTRL)
                mods |= 0x0002;
            if (mod & SDL_KMOD_ALT)
                mods |= 0x0004;
            if (mod & SDL_KMOD_GUI)
                mods |= 0x0008;
            if (mod & SDL_KMOD_CAPS)
                mods |= 0x0010;
            if (mod & SDL_KMOD_NUM)
                mods |= 0x0020;
            return Bitmask<InputMod>(mods);
        }

        InputAction ToAction(bool down, bool repeat)
        {
            return static_cast<InputAction>(down ? (repeat ? 2 : 1) : 0); // Release / Press / Repeat
        }
    } // namespace

    Window *Window::FromId(SDL_WindowID id)
    {
        SDL_Window *sdlWindow = SDL_GetWindowFromID(id);
        if (!sdlWindow)
            return nullptr;
        return static_cast<Window *>(
                SDL_GetPointerProperty(SDL_GetWindowProperties(sdlWindow), kWindowProperty, nullptr));
    }

    void Window::HandleEvent(const SDL_Event &event)
    {
        if (ImGuiReady())
            ImGui_ImplSDL3_ProcessEvent(&event);

        switch (event.type)
        {
            case SDL_EVENT_WINDOW_MOVED:
            {
                if (fullscreen)
                    break;
                position = {event.window.data1, event.window.data2};
                onPosition(position);
                break;
            }
            case SDL_EVENT_WINDOW_RESIZED:
            { // logical (OS-scaled) size
                const int32_t width  = event.window.data1;
                const int32_t height = event.window.data2;
                if (width <= 0 || height <= 0)
                    break;
                if (fullscreen)
                {
                    fullscreenSize = {width, height};
                    onSize(fullscreenSize);
                } else
                {
                    size = {width, height};
                    onSize(size);
                }
                break;
            }
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            { // physical framebuffer size
                const int32_t width  = event.window.data1;
                const int32_t height = event.window.data2;
                if (width <= 0 || height <= 0)
                    break; // minimized: don't recreate the swapchain
                framebufferSize = {width, height};
                RenderSystem::Get()->SetFramebufferResized(this->id);
                break;
            }
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                closed = true;
                Engine::Get()->RequestClose();
                onClose();
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                focused = true;
                onFocus(true);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                focused = false;
                onFocus(false);
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                iconified = true;
                onIconify(true);
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                iconified = false;
                onIconify(false);
                break;
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
                windowSelected = true;
                onEnter(true);
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                windowSelected = false;
                onEnter(false);
                break;
            case SDL_EVENT_WINDOW_EXPOSED:
            {
                break;
            }

            case SDL_EVENT_DROP_BEGIN:
                pendingDrops.clear();
                break;
            case SDL_EVENT_DROP_FILE:
                if (event.drop.data)
                    pendingDrops.emplace_back(event.drop.data);
                break;
            case SDL_EVENT_DROP_COMPLETE:
                if (!pendingDrops.empty())
                    onDrop(pendingDrops);
                pendingDrops.clear();
                break;

            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                onKey(ToKey(event.key.scancode), ToAction(event.key.down, event.key.repeat), ToMods(event.key.mod));
                break;
            case SDL_EVENT_TEXT_INPUT:
            {
                const char *text = event.text.text;
                while (text && *text)
                {
                    const uint32_t codepoint = SDL_StepUTF8(&text, nullptr);
                    if (codepoint == 0)
                        break;
                    onChar(static_cast<char>(codepoint));
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                const int32_t button = FromSdlButton(event.button.button);
                if (button < 0)
                    break;
                onMouseButton(static_cast<MouseButton>(button), ToAction(event.button.down, false),
                              ToMods(SDL_GetModState()));
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
                if (SDL_GetWindowRelativeMouseMode(window))
                {
                    mousePosition += DVec2(event.motion.xrel, event.motion.yrel);
                } else
                {
                    mousePosition = {event.motion.x, event.motion.y};
                }
                onMousePosition(mousePosition);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                // Accumulate so GetMouseScrollDelta() (= scroll - lastScroll) works across consecutive notches.
                mouseScroll += DVec2(event.wheel.x, event.wheel.y);
                onMouseScroll(DVec2(event.wheel.x, event.wheel.y));
                break;

            default:
                break;
        }
    }

    double Window::SmoothScrollWheel(double value, float delta)
    {
        if (value != 0.0)
        {
            value -= static_cast<double>(delta) * copysign(3.0, value);
            value = Mathematics::Deadband(0.08, value);
            return value;
        }

        return 0.0;
    }

    Window::Window(size_t id) :
        id(id), window(nullptr), size(800, 600), fullscreenSize(0, 0), position(0, 0), title("Default Window"),
        borderless(false), resizable(false), floating(false), fullscreen(false), closed(false), focused(false),
        iconified(false), windowSelected(false), cursorHidden(false), mouseLastPosition(0.0, 0.0),
        mousePosition(0.0, 0.0), mousePositionDelta(0.0, 0.0), mouseLastScroll(0.0, 0.0), mouseScroll(0.0, 0.0),
        mouseScrollDelta(0.0, 0.0)
    {
        constexpr SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;

        window = SDL_CreateWindow(title.c_str(), static_cast<int>(size.x), static_cast<int>(size.y), flags);
        if (!window)
            throw runtime_error(string("SDL failed to create the window: ") + SDL_GetError());

        SDL_SetPointerProperty(SDL_GetWindowProperties(window), kWindowProperty, this);

        int width = 0, height = 0;
        SDL_GetWindowSize(window, &width, &height);
        size = UVec2(width, height);

        int fbWidth = 0, fbHeight = 0;
        SDL_GetWindowSizeInPixels(window, &fbWidth, &fbHeight);
        framebufferSize = UVec2(fbWidth, fbHeight);

        int xpos = 0, ypos = 0;
        SDL_GetWindowPosition(window, &xpos, &ypos);
        position = UVec2(xpos, ypos);

        // Window attributes that can change later.
        SDL_SetWindowBordered(window, !borderless);
        SDL_SetWindowResizable(window, resizable);
        SDL_SetWindowAlwaysOnTop(window, floating);

        if (fullscreen)
            SetFullscreen(true);

#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
        SDL_StartTextInput(window);
#endif

        SDL_ShowWindow(window);
    }

    Window::~Window()
    {
        if (window)
        {
            SDL_ClearProperty(SDL_GetWindowProperties(window), kWindowProperty);
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        closed = true;
    }

    void Window::Update()
    {
        mousePositionDelta = mousePosition - mouseLastPosition;
        mouseLastPosition  = mousePosition;

        mouseScrollDelta = mouseScroll - mouseLastScroll;
        mouseLastScroll  = mouseScroll;
    }

    void Window::SetSize(const IVec2 &size)
    {
        if (size.x != -1)
            this->size.x = size.x;
        if (size.y != -1)
            this->size.y = size.y;
        SDL_SetWindowSize(window, static_cast<int>(this->size.x), static_cast<int>(this->size.y));
    }

    void Window::SetPosition(const IVec2 &position)
    {
        if (position.x != -1)
            this->position.x = position.x;
        if (position.y != -1) // was `position.x != -1` (copy/paste bug)
            this->position.y = position.y;
        SDL_SetWindowPosition(window, static_cast<int>(this->position.x), static_cast<int>(this->position.y));
    }

    void Window::SetTitle(const string &title)
    {
        this->title = title;
        SDL_SetWindowTitle(window, title.c_str());
        onTitle(title);
    }

    void Window::SetIcons(const vector<filesystem::path> &filenames) const
    {
        vector<unique_ptr<Bitmap>> bitmaps;
        vector<SDL_Surface *> surfaces;

        for (const auto &filename: filenames)
        {
            auto bitmap = make_unique<Bitmap>(filename);
            if (!bitmap)
                continue;

            const int w = static_cast<int>(bitmap->GetSize().x);
            const int h = static_cast<int>(bitmap->GetSize().y);

            // Wraps the bitmap's RGBA8 pixels without copying; the bitmap must outlive the surface.
            SDL_Surface *surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, bitmap->GetData().get(), w * 4);
            if (!surface)
                continue;

            surfaces.push_back(surface);
            bitmaps.emplace_back(std::move(bitmap));
        }

        if (!surfaces.empty())
        {
            // SDL takes one icon plus "alternate images" for other resolutions.
            for (size_t i = 1; i < surfaces.size(); ++i)
                SDL_AddSurfaceAlternateImage(surfaces[0], surfaces[i]);
            SDL_SetWindowIcon(window, surfaces[0]); // SDL copies the surface(s)
        }

        for (SDL_Surface *surface: surfaces)
            SDL_DestroySurface(surface);
    }

    void Window::SetBorderless(bool borderless)
    {
        this->borderless = borderless;
        SDL_SetWindowBordered(window, !borderless);
        onBorderless(borderless);
    }

    void Window::SetResizable(bool resizable)
    {
        this->resizable = resizable;
        SDL_SetWindowResizable(window, resizable);
        onResizable(resizable);
    }

    void Window::SetFloating(bool floating)
    {
        this->floating = floating;
        SDL_SetWindowAlwaysOnTop(window, floating);
        onFloating(floating);
    }

    void Window::SetFullscreen(bool fullscreen, Monitor *monitor)
    {
        const Monitor *selected = monitor ? monitor : GetCurrentMonitor();

        this->fullscreen = fullscreen;

        if (fullscreen)
        {
            if (selected)
            {
                const auto videoMode = selected->GetVideoMode();
                fullscreenSize       = {videoMode.width, videoMode.height};

                const int pos = SDL_WINDOWPOS_CENTERED_DISPLAY(selected->GetMonitor());
                SDL_SetWindowPosition(window, pos, pos);
            }
            // nullptr mode = borderless fullscreen at the desktop resolution (the recommended default).
            SDL_SetWindowFullscreenMode(window, nullptr);
            SDL_SetWindowFullscreen(window, true);
        } else
        {
            SDL_SetWindowFullscreen(window, false);
            SDL_SetWindowSize(window, static_cast<int>(size.x), static_cast<int>(size.y));
            if (selected)
            {
                const int pos = SDL_WINDOWPOS_CENTERED_DISPLAY(selected->GetMonitor());
                SDL_SetWindowPosition(window, pos, pos);
            }
            SDL_SyncWindow(window); // SDL window changes are async; wait so position is valid
            int xpos = 0, ypos = 0;
            SDL_GetWindowPosition(window, &xpos, &ypos);
            position = UVec2(xpos, ypos);
        }

        onFullscreen(fullscreen);
    }

    void Window::SetIconified(bool iconify) const
    {
        if (!iconified && iconify)
            SDL_MinimizeWindow(window);
        else if (iconified && !iconify)
            SDL_RestoreWindow(window);
    }

    string Window::GetClipboard() const
    {
        char *text    = SDL_GetClipboardText();
        string result = text ? text : "";
        SDL_free(text);
        return result;
    }

    void Window::SetClipboard(const string &string) const { SDL_SetClipboardText(string.c_str()); }

    void Window::SetCursorHidden(bool hidden)
    {
        if (cursorHidden != hidden)
        {
            SDL_SetWindowRelativeMouseMode(window, hidden);

            if (!hidden && cursorHidden)
                SetMousePosition(mousePosition);
        }

        cursorHidden = hidden;
    }

    void Window::SetCursor(const Cursor *cursor) { SDL_SetCursor(cursor ? cursor->cursor : SDL_GetDefaultCursor()); }

    InputAction Window::GetKey(Key key) const
    {
        const SDL_Scancode scancode = ToScancode(key);
        if (scancode == SDL_SCANCODE_UNKNOWN)
            return static_cast<InputAction>(0);

        const bool *state = SDL_GetKeyboardState(nullptr);
        return static_cast<InputAction>(state[scancode] ? 1 : 0);
    }

    InputAction Window::GetMouseButton(MouseButton mouseButton) const
    {
        const int32_t sdlButton = ToSdlButton(static_cast<int32_t>(mouseButton));
        if (sdlButton == 0)
            return static_cast<InputAction>(0);

        const SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
        return static_cast<InputAction>((buttons & SDL_BUTTON_MASK(sdlButton)) ? 1 : 0);
    }

    void Window::SetMousePosition(const DVec2 &mousePosition)
    {
        this->mouseLastPosition = mousePosition;
        this->mousePosition     = mousePosition;
        SDL_WarpMouseInWindow(window, static_cast<float>(mousePosition.x), static_cast<float>(mousePosition.y));
    }

    void Window::SetMouseScroll(const DVec2 &mouseScroll)
    {
        this->mouseLastScroll = mouseScroll;
        this->mouseScroll     = mouseScroll;
    }

    std::string Window::ToString(Key key)
    {
        switch (key)
        {
            case Key::Space:
                return "Space";
            case Key::Apostrophe:
                return "Apostrophe";
            case Key::Comma:
                return "Comma";
            case Key::Minus:
                return "Minus";
            case Key::Period:
                return "Period";
            case Key::Slash:
                return "Slash";
            case Key::_0:
                return "0";
            case Key::_1:
                return "1";
            case Key::_2:
                return "2";
            case Key::_3:
                return "3";
            case Key::_4:
                return "4";
            case Key::_5:
                return "5";
            case Key::_6:
                return "6";
            case Key::_7:
                return "7";
            case Key::_8:
                return "8";
            case Key::_9:
                return "9";
            case Key::Semicolon:
                return "Semicolon";
            case Key::Equal:
                return "Equal";
            case Key::A:
                return "A";
            case Key::B:
                return "B";
            case Key::C:
                return "C";
            case Key::D:
                return "D";
            case Key::E:
                return "E";
            case Key::F:
                return "F";
            case Key::G:
                return "G";
            case Key::H:
                return "H";
            case Key::I:
                return "I";
            case Key::J:
                return "J";
            case Key::K:
                return "K";
            case Key::L:
                return "L";
            case Key::M:
                return "M";
            case Key::N:
                return "N";
            case Key::O:
                return "O";
            case Key::P:
                return "P";
            case Key::Q:
                return "Q";
            case Key::R:
                return "R";
            case Key::S:
                return "S";
            case Key::T:
                return "T";
            case Key::U:
                return "U";
            case Key::V:
                return "V";
            case Key::W:
                return "W";
            case Key::X:
                return "X";
            case Key::Y:
                return "Y";
            case Key::Z:
                return "Z";
            case Key::LeftBracket:
                return "Left Bracket";
            case Key::Backslash:
                return "Backslash";
            case Key::RightBracket:
                return "Right Bracket";
            case Key::GraveAccent:
                return "Grave Accent";
            case Key::World1:
                return "World1";
            case Key::World2:
                return "World2";
            case Key::Escape:
                return "Escape";
            case Key::Enter:
                return "Enter";
            case Key::Tab:
                return "Tab";
            case Key::Backspace:
                return "Backspace";
            case Key::Insert:
                return "Insert";
            case Key::Delete:
                return "Delete";
            case Key::Right:
                return "Right";
            case Key::Left:
                return "Left";
            case Key::Down:
                return "Down";
            case Key::Up:
                return "Up";
            case Key::PageUp:
                return "Page Up";
            case Key::PageDown:
                return "Page Down";
            case Key::Home:
                return "Home";
            case Key::End:
                return "End";
            case Key::CapsLock:
                return "Caps Lock";
            case Key::ScrollLock:
                return "Scroll Lock";
            case Key::NumLock:
                return "Num Lock";
            case Key::PrintScreen:
                return "Print Screen";
            case Key::Pause:
                return "Pause";
            case Key::F1:
                return "F1";
            case Key::F2:
                return "F2";
            case Key::F3:
                return "F3";
            case Key::F4:
                return "F4";
            case Key::F5:
                return "F5";
            case Key::F6:
                return "F6";
            case Key::F7:
                return "F7";
            case Key::F8:
                return "F8";
            case Key::F9:
                return "F9";
            case Key::F10:
                return "F10";
            case Key::F11:
                return "F11";
            case Key::F12:
                return "F12";
            case Key::F13:
                return "F13";
            case Key::F14:
                return "F14";
            case Key::F15:
                return "F15";
            case Key::F16:
                return "F16";
            case Key::F17:
                return "F17";
            case Key::F18:
                return "F18";
            case Key::F19:
                return "F19";
            case Key::F20:
                return "F20";
            case Key::F21:
                return "F21";
            case Key::F22:
                return "F22";
            case Key::F23:
                return "F23";
            case Key::F24:
                return "F24";
            case Key::F25:
                return "F25";
            case Key::Numpad0:
                return "Numpad 0";
            case Key::Numpad1:
                return "Numpad 1";
            case Key::Numpad2:
                return "Numpad 2";
            case Key::Numpad3:
                return "Numpad 3";
            case Key::Numpad4:
                return "Numpad 4";
            case Key::Numpad5:
                return "Numpad 5";
            case Key::Numpad6:
                return "Numpad 6";
            case Key::Numpad7:
                return "Numpad 7";
            case Key::Numpad8:
                return "Numpad 8";
            case Key::Numpad9:
                return "Numpad 9";
            case Key::NumpadDecimal:
                return "Numpad Decimal";
            case Key::NumpadDivide:
                return "Numpad Divide";
            case Key::NumpadMultiply:
                return "Numpad Multiply";
            case Key::NumpadSubtract:
                return "Numpad Subtract";
            case Key::NumpadAdd:
                return "Numpad Add";
            case Key::NumpadEnter:
                return "Numpad Enter";
            case Key::NumpadEqual:
                return "Numpad Equal";
            case Key::ShiftLeft:
                return "Shift Left";
            case Key::ControlLeft:
                return "Control Left";
            case Key::AltLeft:
                return "Alt Left";
            case Key::SuperLeft:
                return "Super Left";
            case Key::ShiftRight:
                return "Shift Right";
            case Key::ControlRight:
                return "Control Right";
            case Key::AltRight:
                return "Alt Right";
            case Key::SuperRight:
                return "Super Right";
            case Key::Menu:
                return "Menu";
            default:
                return "Undefined";
        }
    }

    const Monitor *Window::GetCurrentMonitor() const
    {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(window);

        for (const auto &monitor: WindowManager::Get()->GetMonitors())
        {
            if (monitor->monitor == display)
                return monitor.get();
        }
        return nullptr;
    }

    VkResult Window::CreateSurface(const VkInstance &instance, const VkAllocationCallbacks *allocator,
                                   VkSurfaceKHR *surface) const
    {
        return SDL_Vulkan_CreateSurface(window, instance, allocator, surface) ? VK_SUCCESS
                                                                              : VK_ERROR_INITIALIZATION_FAILED;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

    void Window::SetTitleColor(Color col)
    {
#if _PLATFORM_WINDOWS
        auto hwnd = static_cast<HWND>(
                SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!hwnd)
            return;

        DWORD color = (DWORD(col.b * 255.0f) << 16) | (DWORD(col.g * 255.0f) << 8) | DWORD(col.r * 255.0f);
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &color, sizeof(color));
#endif
    }

    void Window::SetBorderColor(Color col)
    {
#if _PLATFORM_WINDOWS
        auto hwnd = static_cast<HWND>(
                SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!hwnd)
            return;

        DWORD color = (DWORD(col.b * 255.0f) << 16) | (DWORD(col.g * 255.0f) << 8) | DWORD(col.r * 255.0f);
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &color, sizeof(color));
#endif
    }
#pragma GCC diagnostic pop
} // namespace SF::Engine
