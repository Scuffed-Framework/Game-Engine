#include <Rendering/Material/Color/Color.hpp>
#include "WindowManager.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#if _PLATFORM_WINDOWS
    #pragma comment(lib, "dwmapi.lib")
    #include <dwmapi.h>
#endif

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

        inline bool ImGuiReady() { return ImGui::GetCurrentContext() != nullptr; }

        struct KeyMapping
        {
            SDL_Scancode scancode;
            Key key;
        };

        constexpr KeyMapping kKeyMap[] = {
                {SDL_SCANCODE_SPACE, Key::Space},
                {SDL_SCANCODE_APOSTROPHE, Key::Apostrophe},
                {SDL_SCANCODE_COMMA, Key::Comma},
                {SDL_SCANCODE_MINUS, Key::Minus},
                {SDL_SCANCODE_PERIOD, Key::Period},
                {SDL_SCANCODE_SLASH, Key::Slash},
                {SDL_SCANCODE_0, Key::_0},
                {SDL_SCANCODE_1, Key::_1},
                {SDL_SCANCODE_2, Key::_2},
                {SDL_SCANCODE_3, Key::_3},
                {SDL_SCANCODE_4, Key::_4},
                {SDL_SCANCODE_5, Key::_5},
                {SDL_SCANCODE_6, Key::_6},
                {SDL_SCANCODE_7, Key::_7},
                {SDL_SCANCODE_8, Key::_8},
                {SDL_SCANCODE_9, Key::_9},
                {SDL_SCANCODE_SEMICOLON, Key::Semicolon},
                {SDL_SCANCODE_EQUALS, Key::Equal},
                {SDL_SCANCODE_A, Key::A},
                {SDL_SCANCODE_B, Key::B},
                {SDL_SCANCODE_C, Key::C},
                {SDL_SCANCODE_D, Key::D},
                {SDL_SCANCODE_E, Key::E},
                {SDL_SCANCODE_F, Key::F},
                {SDL_SCANCODE_G, Key::G},
                {SDL_SCANCODE_H, Key::H},
                {SDL_SCANCODE_I, Key::I},
                {SDL_SCANCODE_J, Key::J},
                {SDL_SCANCODE_K, Key::K},
                {SDL_SCANCODE_L, Key::L},
                {SDL_SCANCODE_M, Key::M},
                {SDL_SCANCODE_N, Key::N},
                {SDL_SCANCODE_O, Key::O},
                {SDL_SCANCODE_P, Key::P},
                {SDL_SCANCODE_Q, Key::Q},
                {SDL_SCANCODE_R, Key::R},
                {SDL_SCANCODE_S, Key::S},
                {SDL_SCANCODE_T, Key::T},
                {SDL_SCANCODE_U, Key::U},
                {SDL_SCANCODE_V, Key::V},
                {SDL_SCANCODE_W, Key::W},
                {SDL_SCANCODE_X, Key::X},
                {SDL_SCANCODE_Y, Key::Y},
                {SDL_SCANCODE_Z, Key::Z},
                {SDL_SCANCODE_LEFTBRACKET, Key::LeftBracket},
                {SDL_SCANCODE_BACKSLASH, Key::Backslash},
                {SDL_SCANCODE_RIGHTBRACKET, Key::RightBracket},
                {SDL_SCANCODE_GRAVE, Key::GraveAccent},
                {SDL_SCANCODE_NONUSBACKSLASH, Key::World1},
                {SDL_SCANCODE_NONUSHASH, Key::World2},
                {SDL_SCANCODE_ESCAPE, Key::Escape},
                {SDL_SCANCODE_RETURN, Key::Enter},
                {SDL_SCANCODE_TAB, Key::Tab},
                {SDL_SCANCODE_BACKSPACE, Key::Backspace},
                {SDL_SCANCODE_INSERT, Key::Insert},
                {SDL_SCANCODE_DELETE, Key::Delete},
                {SDL_SCANCODE_RIGHT, Key::Right},
                {SDL_SCANCODE_LEFT, Key::Left},
                {SDL_SCANCODE_DOWN, Key::Down},
                {SDL_SCANCODE_UP, Key::Up},
                {SDL_SCANCODE_PAGEUP, Key::PageUp},
                {SDL_SCANCODE_PAGEDOWN, Key::PageDown},
                {SDL_SCANCODE_HOME, Key::Home},
                {SDL_SCANCODE_END, Key::End},
                {SDL_SCANCODE_CAPSLOCK, Key::CapsLock},
                {SDL_SCANCODE_SCROLLLOCK, Key::ScrollLock},
                {SDL_SCANCODE_NUMLOCKCLEAR, Key::NumLock},
                {SDL_SCANCODE_PRINTSCREEN, Key::PrintScreen},
                {SDL_SCANCODE_PAUSE, Key::Pause},
                {SDL_SCANCODE_F1, Key::F1},
                {SDL_SCANCODE_F2, Key::F2},
                {SDL_SCANCODE_F3, Key::F3},
                {SDL_SCANCODE_F4, Key::F4},
                {SDL_SCANCODE_F5, Key::F5},
                {SDL_SCANCODE_F6, Key::F6},
                {SDL_SCANCODE_F7, Key::F7},
                {SDL_SCANCODE_F8, Key::F8},
                {SDL_SCANCODE_F9, Key::F9},
                {SDL_SCANCODE_F10, Key::F10},
                {SDL_SCANCODE_F11, Key::F11},
                {SDL_SCANCODE_F12, Key::F12},
                {SDL_SCANCODE_F13, Key::F13},
                {SDL_SCANCODE_F14, Key::F14},
                {SDL_SCANCODE_F15, Key::F15},
                {SDL_SCANCODE_F16, Key::F16},
                {SDL_SCANCODE_F17, Key::F17},
                {SDL_SCANCODE_F18, Key::F18},
                {SDL_SCANCODE_F19, Key::F19},
                {SDL_SCANCODE_F20, Key::F20},
                {SDL_SCANCODE_F21, Key::F21},
                {SDL_SCANCODE_F22, Key::F22},
                {SDL_SCANCODE_F23, Key::F23},
                {SDL_SCANCODE_F24, Key::F24},
                // Key::F25 has no SDL equivalent.
                {SDL_SCANCODE_KP_0, Key::Numpad0},
                {SDL_SCANCODE_KP_1, Key::Numpad1},
                {SDL_SCANCODE_KP_2, Key::Numpad2},
                {SDL_SCANCODE_KP_3, Key::Numpad3},
                {SDL_SCANCODE_KP_4, Key::Numpad4},
                {SDL_SCANCODE_KP_5, Key::Numpad5},
                {SDL_SCANCODE_KP_6, Key::Numpad6},
                {SDL_SCANCODE_KP_7, Key::Numpad7},
                {SDL_SCANCODE_KP_8, Key::Numpad8},
                {SDL_SCANCODE_KP_9, Key::Numpad9},
                {SDL_SCANCODE_KP_PERIOD, Key::NumpadDecimal},
                {SDL_SCANCODE_KP_DIVIDE, Key::NumpadDivide},
                {SDL_SCANCODE_KP_MULTIPLY, Key::NumpadMultiply},
                {SDL_SCANCODE_KP_MINUS, Key::NumpadSubtract},
                {SDL_SCANCODE_KP_PLUS, Key::NumpadAdd},
                {SDL_SCANCODE_KP_ENTER, Key::NumpadEnter},
                {SDL_SCANCODE_KP_EQUALS, Key::NumpadEqual},
                {SDL_SCANCODE_LSHIFT, Key::ShiftLeft},
                {SDL_SCANCODE_LCTRL, Key::ControlLeft},
                {SDL_SCANCODE_LALT, Key::AltLeft},
                {SDL_SCANCODE_LGUI, Key::SuperLeft},
                {SDL_SCANCODE_RSHIFT, Key::ShiftRight},
                {SDL_SCANCODE_RCTRL, Key::ControlRight},
                {SDL_SCANCODE_RALT, Key::AltRight},
                {SDL_SCANCODE_RGUI, Key::SuperRight},
                {SDL_SCANCODE_APPLICATION, Key::Menu},
        };

        constexpr Key kUnknownKey = static_cast<Key>(-1);

        Key ToKey(SDL_Scancode scancode)
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
                for (const auto &m: kKeyMap)
                    t[static_cast<int32_t>(m.key)] = m.scancode;
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

        int32_t ToSdlButton(int32_t button)
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

        Bitmask::Bitmask<InputMod> ToMods(SDL_Keymod mod)
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
            return Bitmask::Bitmask<InputMod>(mods);
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
                // Guard against reentrance: OS paint messages can fire during Vulkan allocations (vmaCreateImage etc.),
                // re-entering this path before the first frame completes.
                static bool s_inRefresh = false;
                if (s_inRefresh)
                    break;
                s_inRefresh = true;

                if (auto *rs = RenderSystem::Get(); rs && rs->GetRenderer() && rs->GetRenderer()->IsStarted())
                {
                    Update();
                    rs->Update();
                }
                s_inRefresh = false;
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
