#pragma once

#ifdef _PLATFORM_LINUX
    #include <csignal>
#endif

#include <cstdarg>
#include <cstddef>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <3rdPartyLibs/Rocket.hpp>
#include <LowLevel/FileSystem/File.hpp>

// Undefine problematic macros (microslop)
#ifdef major
    #undef major
#endif
#ifdef minor
    #undef minor
#endif

#include <Engine/VersionSemantic.hpp>
#include <Platform/PlatformIncludes.hpp>
#include <UtilityClasses/NoCopy.hpp>

#include <Configuration/Default/ImGuiDefaultWidgets.hpp>
#include <Engine/InitGame/AppInfo.hpp>
#include <Engine/Log/Log.hpp>
#include <Platform/Windowing/WindowManager.hpp>
#include <Rendering/RHI/Viewport/Viewport.hpp>
#include <Rendering/RenderSystem.hpp>
#include <Scene/SceneManager.hpp>

#ifdef Started
    #undef Started
#endif
#ifdef Success
    #undef Success
#endif

namespace SF::Engine
{
    class Engine;

    std::filesystem::path GetExecutablePathImpl();

    /**
     * A guest of the engine (game, editor, tool...). The engine boots first, then calls
     * OnInit() once, drives the main loop (calling OnUpdate() every update tick), and
     * finally calls OnShutdown() while every engine module is still alive.
     *
     * The application never owns the engine and never runs its own loop.
     */
    class Application : NoCopy
    {
        friend class Engine;

    protected:
        // Filled in by OnInit(); valid for the whole lifetime of the engine.
        WindowManager *wndMgr  = nullptr;
        RenderSystem *renderer = nullptr;
        SceneManager *sceneMgr = nullptr;

    private:
        bool started_ = false;
        std::string path_;
        std::vector<File> modules_;
        std::vector<std::unique_ptr<SceneViewport>> viewports;
        size_t reg{};

    public:
        enum InitializationReturn
        {
            Success,
            FailedToGetEngineModules,
            FailedToCreateGameWindow,
            NoApplicationInfo,
            Failure
        };

        enum class ShutdownReturn
        {
            Success,
            Failed
        };

        // Owned polymorphically; pass a derived AppInfo via std::make_unique<MyInfo>(...).
        std::unique_ptr<AppInfo> Info;

        Window *window{};

        explicit Application(std::unique_ptr<AppInfo> info);
        virtual ~Application();

        /**
         * Called once by the engine after all modules are up, before the main loop starts.
         * The default implementation grabs the engine modules, creates the main window and the
         * default viewport. Derived classes: call Application::OnInit(engine) FIRST, then add
         * your own panels/layers/scene setup. Return anything other than Success to abort startup.
         *
         * Use engine.LoadStartupScene(url) here if you want a specific startup scene; otherwise
         * the engine loads the default one right before the loop starts.
         */
        virtual InitializationReturn OnInit(Engine &engine);

        /**
         * Called by the engine once per update tick, after the Pre stage and the layer stack,
         * before the Normal stage (SceneManager). Window polling, scene update and rendering
         * are driven by the engine's module stages, NOT from here.
         */
        virtual void OnUpdate() {}

        /**
         * Called once after the main loop exits and the GPU has been drained, while every engine
         * module is still alive. Release GPU resources (viewport images, panel textures) here.
         */
        virtual void OnShutdown() {}

        /**
         * Polled by the engine every iteration. Return true to stop the main loop.
         * Default: the main window has been closed.
         */
        [[nodiscard]] virtual bool ShouldClose() const { return window && window->IsClosed(); }

        /// Typed access to the info object for derived applications.
        template<class TInfo>
        [[nodiscard]] TInfo *InfoAs() const noexcept
        {
            static_assert(std::is_base_of_v<AppInfo, TInfo>, "TInfo must derive from AppInfo");
            return static_cast<TInfo *>(Info.get());
        }

        /**
         * @brief Gets the Application's name.
         */
        [[nodiscard]] const std::string &GetName() const noexcept;

        /**
         * @brief Sets the Application's name for driver support.
         */
        void SetName(const std::string &name) const;

        /**
         * @brief Gets the Application's version.
         */
        [[nodiscard]] const Version &GetVersion() const noexcept;

        /**
         * @brief Sets the Application's version for driver support.
         */
        void SetVersion(const Version &version) const noexcept;

        /**
         * @brief Checks if the engine has started running this application's loop.
         */
        [[nodiscard]] bool IsStarted() const noexcept { return started_; }

        static std::vector<File> GetAllModules();
        static std::filesystem::path GetExecutablePath() { return GetExecutablePathImpl(); }
    };

    using ApplicationFactory = std::unique_ptr<Application> (*)();

    /**
     * The real program entry. Boots the engine, then creates and runs the application the
     * factory returns, then tears everything down in order (application first, engine last).
     */
    int RunEngine(int argc, char **argv, ApplicationFactory factory);

} // namespace SF::Engine

/**
 *     SF_IMPLEMENT_APPLICATION(SF::Engine::EditorApplication)
 *
 * AppType must be default constructible and derive from SF::Engine::Application.
 * Platform entry points (android_main, SDL_main, WinMain) can be swapped in here later
 * without touching any game or editor code.
 */
#define SF_IMPLEMENT_APPLICATION(AppType)                                                                              \
    int main(int argc, char **argv)                                                                                    \
    {                                                                                                                  \
        return ::SF::Engine::RunEngine(argc, argv, []() -> std::unique_ptr<::SF::Engine::Application>                  \
                                       { return std::make_unique<AppType>(); });                                       \
    }
