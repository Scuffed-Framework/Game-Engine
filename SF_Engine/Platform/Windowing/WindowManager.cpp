#include "WindowManager.hpp"

#include <Gui/ImGui/ocornut/imgui.h>
#include <Gui/ImGui/ocornut/imgui_impl_sdl3.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>

namespace SF::Engine
{
    bool WindowManager::CheckSdl(const bool result)
    {
        if (!result)
            Log::Error("SDL error: ", SDL_GetError());
        return result;
    }

    WindowManager::WindowManager()
    {
        if (!SDL_Init(SDL_INIT_VIDEO
#ifdef SF_INPUT_ADDONS
                      | SDL_INIT_HAPTIC | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK
#elif SF_GAME_REQUIRES_CAMERA_AND_SENSOR_ACCESS
                      | SDL_INIT_CAMERA | SDL_INIT_SENSOR
#endif
                      ))
        {
            throw runtime_error(string("SDL failed to initialize: ") + SDL_GetError());
        }

        if (!SDL_Vulkan_LoadLibrary(nullptr))
            throw runtime_error(string("SDL failed to find Vulkan support: ") + SDL_GetError());

        SDL_AddEventWatch(&WindowManager::EventWatch, this);

        // Get connected monitors.
        int32_t displayCount = 0;
        if (SDL_DisplayID *displays = SDL_GetDisplays(&displayCount))
        {
            for (int32_t i = 0; i < displayCount; i++)
                monitors.emplace_back(make_unique<Monitor>(displays[i]));
            SDL_free(displays);
        }
    }

    WindowManager::~WindowManager()
    {
        windows.clear();
        monitors.clear();

        SDL_RemoveEventWatch(&WindowManager::EventWatch, this);
        SDL_Vulkan_UnloadLibrary();
        SDL_Quit();
    }

    void WindowManager::Update()
    {
        PollEvents();
        for (const auto &window: windows)
            window->Update();
    }

    void WindowManager::PollEvents()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            DispatchEvent(event);
    }

    void WindowManager::WaitEvents(int32_t timeoutMs)
    {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, timeoutMs))
        {
            DispatchEvent(event);
            PollEvents();
        }
    }

    void WindowManager::DispatchEvent(const SDL_Event &event)
    {
        switch (event.type)
        {
            case SDL_EVENT_QUIT:
                Engine::Get()->RequestClose();
                return;

            case SDL_EVENT_DISPLAY_ADDED:
                AddMonitor(event.display.displayID);
                return;

            case SDL_EVENT_DISPLAY_REMOVED:
                RemoveMonitor(event.display.displayID);
                return;

            case SDL_EVENT_WINDOW_EXPOSED: // handled immediately by EventWatch
                return;

            default:
                break;
        }

        if (SDL_Window *sdlWindow = SDL_GetWindowFromEvent(&event))
        {
            if (Window *window = Window::FromId(SDL_GetWindowID(sdlWindow)))
            {
                window->HandleEvent(event); // also feeds ImGui
                return;
            }
        }

        if (ImGui::GetCurrentContext())
            ImGui_ImplSDL3_ProcessEvent(&event);
    }

    bool WindowManager::EventWatch(void *userdata, SDL_Event *event)
    {
        auto *self = static_cast<WindowManager *>(userdata);

        switch (event->type)
        {
            case SDL_EVENT_WINDOW_EXPOSED:
                if (SDL_IsMainThread())
                {
                    if (Window *window = Window::FromId(event->window.windowID))
                        window->HandleEvent(*event);
                }
                break;

            case SDL_EVENT_WILL_ENTER_BACKGROUND:
                self->inBackground.store(true);
                self->onBackground(true);
                break;

            case SDL_EVENT_DID_ENTER_FOREGROUND:
                self->inBackground.store(false);
                self->onBackground(false);
                break;

            case SDL_EVENT_TERMINATING:
                Engine::Get()->RequestClose();
                break;

            default:
                break;
        }
        return true; // return value is ignored for watches
    }

    void WindowManager::AddMonitor(SDL_DisplayID display)
    {
        const bool known =
                ranges::any_of(monitors, [display](const auto &monitor) { return monitor->GetMonitor() == display; });
        if (known)
            return;

        auto *monitor = monitors.emplace_back(make_unique<Monitor>(display)).get();
        onMonitorConnect(monitor, true);
    }

    void WindowManager::RemoveMonitor(SDL_DisplayID display)
    {
        // Notify first, while the Monitor object still exists.
        for (const auto &monitor: monitors)
        {
            if (monitor->GetMonitor() == display)
                onMonitorConnect(monitor.get(), false);
        }

        monitors.erase(
                ranges::remove_if(monitors, [display](const auto &monitor) { return monitor->GetMonitor() == display; })
                        .begin(),
                monitors.end());
    }

    Window *WindowManager::AddWindow()
    {
        const auto window = windows.emplace_back(make_unique<Window>(windows.size())).get();
        onAddWindow(window, true);
        return window;
    }

    const Window *WindowManager::GetWindow(const WindowId id) const
    {
        if (id >= windows.size())
            return nullptr;
        return windows.at(id).get();
    }

    Window *WindowManager::GetWindow(const WindowId id)
    {
        if (id >= windows.size())
            return nullptr;
        return windows.at(id).get();
    }

    const Monitor *WindowManager::GetPrimaryMonitor() const
    {
        for (const auto &monitor: monitors)
        {
            if (monitor->IsPrimary())
                return monitor.get();
        }
        return nullptr;
    }

    pair<const char *const *, uint32_t> WindowManager::GetInstanceExtensions() const
    {
        Uint32 count                  = 0;
        const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&count);
        if (!extensions)
        {
            CheckSdl(false);
            return {nullptr, 0};
        }
        return {extensions, count};
    }
} // namespace SF::Engine
