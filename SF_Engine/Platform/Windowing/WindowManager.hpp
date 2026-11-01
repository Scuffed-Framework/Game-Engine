#pragma once

#include <Engine/Engine.hpp>
#include <SDL3/SDL.h>
#include <atomic>
#include "Window.hpp"

namespace SF::Engine
{
    using namespace std;
    /**
     * @brief Module used for managing a window.
     */
    class WindowManager : public ModuleRegistrar<WindowManager>
    {
        REGISTER_MODULE(WindowManager, ModuleStage::Pre);

    public:
        WindowManager();
        ~WindowManager() override;

        void Update() override;

        Window *AddWindow();
        const Window *GetWindow(WindowId id) const;
        Window *GetWindow(WindowId id);

        const vector<unique_ptr<Monitor>> &GetMonitors() const { return monitors; };

        const Monitor *GetPrimaryMonitor() const;

        /**
         * Called when a window has been added or closed.
         * @return The rocket::signal.
         */
        rocket::signal<void(Window *, bool)> &OnAddWindow() { return onAddWindow; }

        /**
         * Called when a monitor has been connected or disconnected.
         * @return The rocket::signal.
         */
        rocket::signal<void(Monitor *, bool)> &OnMonitorConnect() { return onMonitorConnect; }

        /**
         * Called when the app is sent to the background (true) or returns to the foreground (false).
         * Only fires on mobile (Android/iOS). The Vulkan surface is not valid while in the background:
         * stop rendering, and be ready to recreate the swapchain on return.
         * @return The rocket::signal.
         */
        rocket::signal<void(bool)> &OnBackground() { return onBackground; }

        /** True between SDL's WILL_ENTER_BACKGROUND and DID_ENTER_FOREGROUND. Safe to poll from the render loop. */
        bool IsInBackground() const { return inBackground.load(); }

        /** Logs SDL_GetError() and returns false if `result` is false. SDL3 returns bool for most calls. */
        static bool CheckSdl(bool result);

        /**
         * Gets the Vulkan instance extensions SDL needs to create window surfaces.
         * The returned array is owned by SDL; do not free it.
         */
        pair<const char *const *, uint32_t> GetInstanceExtensions() const;

        vector<unique_ptr<Window>> &GetWindows() { return windows; }

        vector<unique_ptr<Monitor>> &GetMonitors() { return monitors; }

        void WaitEvents(int32_t timeoutMs = 100);

    private:
        void DispatchEvent(const SDL_Event &event);
        void PollEvents();
        void AddMonitor(SDL_DisplayID display);
        void RemoveMonitor(SDL_DisplayID display);

        static bool EventWatch(void *userdata, SDL_Event *event);

        vector<unique_ptr<Window>> windows;

        vector<unique_ptr<Monitor>> monitors;

        atomic<bool> inBackground{false};

        rocket::signal<void(Window *, bool)> onAddWindow;
        rocket::signal<void(Monitor *, bool)> onMonitorConnect;
        rocket::signal<void(bool)> onBackground;
    };
} // namespace SF::Engine
