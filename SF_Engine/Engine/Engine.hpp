#pragma once

// Macro Section
#define MEMORY_BARRIER() asm volatile("" ::: "memory")
#define PREFETCH(addr) asm volatile("prefetcht0 %0" ::"m"(*(const char *) (addr)))
#define PREFETCHW(addr) asm volatile("prefetcht0 %0" ::"m"(*(char *) (addr)))
#define PAUSE() asm volatile("pause")
#define CPU_RELAX() asm volatile("rep nop" ::: "memory")
#define NO_MANGLE __attribute__((visibility("default"))) extern "C"
#define NO_MANGLE_EXPORT __attribute__((visibility("default"))) __declspec(dllexport) extern "C"

#include <LowLevel/LayerStack/LayerStack.hpp>
#include "Application/Application.hpp"
#include "Log/Log.hpp"
#include "Module.hpp"

#include <3rdPartyLibs/Rocket.hpp>
#include <Math/Time/Time.hpp>
#include <Networking/URL.hpp>
#include <Scene/Scene.hpp>
#include <Scene/SceneLoading.hpp>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include <Platform/Threading/ThreadPool.hpp>

#include <Platform/PlatformIncludes.hpp>
#include "Configuration.hpp"
#include "InitGame/AppInfo.hpp"
#include "InitGame/GameInstance.hpp"
#include "VersionSemantic.hpp"


#ifdef major
    #undef major
#endif
#ifdef minor
    #undef minor
#endif

// Because windows.h is a stinking pile of garbage
#ifdef MAJOR
    #undef MAJOR
#endif
#ifdef MINOR
    #undef MINOR
#endif

namespace SF::Engine
{
    class Application;

    class Engine : NoCopy
    {
    public:
        using RegMap = ::SF::Engine::Module::RegistryMap;
        /**
         * Gets the engines instance.
         * @return The current engine instance.
         */
        static Engine *Get() { return Instance; }

        /**
         * Boots the engine on its own: creates and initializes every module (window manager,
         * render system, scene manager, ...). It knows nothing about any game or editor yet.
         * Afterwards attach an {@link Application} with SetApplication() and call
         * {@link Engine#Run @endlink. Normally you don't do this by hand, RunEngine() does it.
         * @param argv0 The first argument passed to main.
         * @param moduleFilter A filter for blacklisting/whitelisting modules.
         */
        explicit Engine(std::string argv0, ModuleFilter &&moduleFilter = {});

        ~Engine() override;

        /**
         * Runs the main loop until RequestClose() is called or the attached application asks to
         * close. Drains the GPU and calls Application::OnShutdown() before returning.
         */
        int32_t Run();

        /**
         * Attaches (or detaches with nullptr) the application the engine calls back into.
         * The engine does NOT own it.
         */
        void SetApplication(Application *application);
        [[nodiscard]] Application *GetApplication() const { return app; }

        /**
         * Creates the GameInstance and loads the startup scene, falling back to DefaultScene on
         * any failure. Call it from Application::OnInit if you want a specific scene; if nobody
         * did, Run() calls it with the default placeholder URL right before the loop starts.
         * Calling it more than once is ignored.
         * @param startUpURL URL of a local map scene, or a server that will send one.
         */
        void LoadStartupScene(const URL &startUpURL = URL("map://startup")); // TODO: Make startup look through scene
                                                                             // metadata to find
                                                                             // "<StartUpScene>true</StartupScene>"

        /**
         * Gets the first argument passed to main.
         * @return The first argument passed to main.
         */
        [[nodiscard]] const std::string &GetArgv0() const { return argv0; };

        /**
         * Gets the engine's version.
         * @return The engine's version.
         */
        [[nodiscard]] const Version &GetVersion() const { return version; }

        /**
         * Gets the fps limit.
         * @return The frame per second limit.
         */
        [[nodiscard]] float GetFpsLimit() const { return fpsLimit; }

        /**
         * Sets the fps limit. -1 disables limits.
         * @param fpsLimit The new frame per second limit.
         */
        void SetFpsLimit(float fpsLimit) { this->fpsLimit = fpsLimit; }

        /**
         * Gets if the engine is running.
         * @return If the engine is running.
         */
        [[nodiscard]] bool IsRunning() const { return running; }

        /**
         * Gets the delta (seconds) between updates.
         * @return The delta between updates.
         */
        [[nodiscard]] const DurationAsNanoseconds &GetDelta() const { return deltaUpdate.change; }

        /**
         * Gets the delta (seconds) between renders.
         * @return The delta between renders.
         */
        [[nodiscard]] const DurationAsNanoseconds &GetDeltaRender() const { return deltaRender.change; }

    private:
        UpdateClock<> ups, fps;
        AppInfo *gameInfo{};

    public:
        void SetGameInfo(AppInfo *info) { gameInfo = info; }
        [[nodiscard]] AppInfo *GetGameInfo() const { return gameInfo; }
        [[nodiscard]] const std::string &GetApplicationName() const noexcept { return GetGameInfo()->name; }
        [[nodiscard]] Version &GetApplicationVersion() const { return GetGameInfo()->version; }

        /**
         * Gets the average UPS over a short interval.
         * @return The updates per second.
         */
        [[nodiscard]] uint32_t GetUps() const { return ups.value_; }

        /**
         * Gets the average FPS over a short interval.
         * @return The frames per second.
         */
        [[nodiscard]] uint32_t GetFps() const { return fps.value_; }

        /**
         * Requests the engine to stop the game-loop.
         */
        void RequestClose() { running = false; }

        /**
         * Gets the layer stack.
         * @return Reference to the layer stack.
         */
        LayerStack &GetLayerStack() { return layerStack; }

        /**
         * Gets the layer stack (const version).
         * @return Const reference to the layer stack.
         */
        [[nodiscard]] const LayerStack &GetLayerStack() const { return layerStack; }

        /**
         * Push a layer onto the layer stack.
         * @param layer The layer to push.
         */
        void PushLayer(std::shared_ptr<Layer> layer) { layerStack.PushLayer(std::move(layer)); }

        /**
         * Push an overlay onto the layer stack.
         * @param overlay The overlay to push.
         */
        void PushOverlay(std::shared_ptr<Layer> overlay) { layerStack.PushOverlay(std::move(overlay)); }

        /**
         * Pop a layer from the layer stack.
         * @param layer The layer to pop.
         */
        void PopLayer(const std::shared_ptr<Layer> &layer) { layerStack.PopLayer(layer); }

        /**
         * Pop an overlay from the layer stack.
         * @param overlay The overlay to pop.
         */
        void PopOverlay(const std::shared_ptr<Layer> &overlay) { layerStack.PopOverlay(overlay); }

        /**
         * Dispatch an event to the layer stack.
         * @param event The event to dispatch.
         */
        template<typename EventType>
        void DispatchEventToLayers(EventType &event)
        {
            layerStack.DispatchEvent(event);
        }

    private:
        void CreateModule(RegMap::const_iterator it, const ModuleFilter &filter);
        void DestroyModule(TypeId id);
        void UpdateStage(ModuleStage stage);

        static Engine *Instance;

        std::string argv0;
        Version version;

        std::map<TypeId, std::unique_ptr<Module>> modules;
        std::map<ModuleStage, std::vector<TypeId>> moduleStages;

        LayerStack layerStack;

        float fpsLimit;
        bool running;

        // The guest we call back into. Not owned.
        Application *app        = nullptr;
        bool startupSceneLoaded = false;

        DeltaTime deltaUpdate, deltaRender;
        ElapsedTime elapsedUpdate, elapsedRender;

        SceneURLResolver resolver;

    public:
        const std::thread::id g_main_thread_id = std::this_thread::get_id();
        const std::thread::id g_render_thread_id =
                std::thread::id(); // Set to main thread by default, will be updated if a separate render thread is used

        [[nodiscard]] const std::thread::id &GetMainThreadId() const { return g_main_thread_id; }

        [[nodiscard]] const std::thread::id &GetRenderThreadId() const { return g_render_thread_id; }

        [[nodiscard]] bool IsMainThread() const { return std::this_thread::get_id() == g_main_thread_id; }

    private:
        ThreadPool threadPool{
                std::max(1u, std::thread::hardware_concurrency() - 1)}; // Leave one thread for the main loop

    public:
        ThreadPool &GetThreadPool() { return threadPool; }

    protected:
        std::unique_ptr<GameInstance> gameInstance;

    public:
        [[nodiscard]] GameInstance *GetGameInstance() const { return gameInstance.get(); }
    };

    inline std::filesystem::path GetEngineRootPath() { return GetExecutablePath(); }

    inline std::filesystem::path GetEngineAssetsPath()
    {
        return GetEngineRootPath() / "Assets"; // todo: build into .rsc
    }
} // namespace SF::Engine
