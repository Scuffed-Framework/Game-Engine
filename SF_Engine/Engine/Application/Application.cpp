#include "Application.hpp"
#include <Engine/Engine.hpp>

#include <cassert>

#if _PLATFORM_WINDOWS
    // Must define NOMINMAX before windows.h to prevent min/max macro pollution
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif

    #define __INTRIN_H_ // Prevent intrinsics header inclusion
    #include <windows.h>
#elif defined(_PLATFORM_MACOS)
    #include <mach-o/dyld.h>
#elif defined(_PLATFORM_LINUX)
    #include <unistd.h>
    #include <x86intrin.h>
#endif

namespace SF::Engine
{
    std::filesystem::path GetExecutablePathImpl()
    {
#if _PLATFORM_WINDOWS
        std::vector<wchar_t> buffer(MAX_PATH);

        DWORD size = 0;
        while (true)
        {
            size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

            if (size == 0)
                return {};

            if (size < buffer.size())
                break;

            buffer.resize(buffer.size() * 2);
        }

        return std::filesystem::path(buffer.data());

#elif _PLATFORM_MACOS
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);

        std::vector<char> buffer(size);
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            return {};

        return std::filesystem::weakly_canonical(buffer.data());

#elif _PLATFORM_LINUX
        std::vector<char> buffer(1024);
        ssize_t size = 0;

        while (true)
        {
            size = readlink("/proc/self/exe", buffer.data(), buffer.size());
            if (size < 0)
                return {};

            if (size < static_cast<ssize_t>(buffer.size()))
                break;

            buffer.resize(buffer.size() * 2);
        }

        return {std::string(buffer.data(), size)};

#else
        return {};
#endif
    }

    // The application is constructed AFTER the engine is fully booted, but its constructor still
    // does nothing engine-related: everything that touches modules lives in OnInit().
    Application::Application(std::unique_ptr<AppInfo> info) : Info(std::move(info)) {}

    Application::InitializationReturn Application::OnInit(Engine &)
    {
        wndMgr   = WindowManager::Get();
        renderer = RenderSystem::Get();
        sceneMgr = SceneManager::Get();

        if (!wndMgr || !renderer || !sceneMgr)
        {
            Log::Error("Failed to get engine modules\n");
            return InitializationReturn::FailedToGetEngineModules;
        }

        if (!Info)
        {
            Log::Error("ApplicationInfo not initialized\n");
            return InitializationReturn::NoApplicationInfo;
        }

        window = wndMgr->AddWindow();
        if (!window)
        {
            Log::Error("Failed to create window\n");
            return InitializationReturn::FailedToCreateGameWindow;
        }
        window->SetTitle(static_cast<std::string>(Info->name));
        window->SetResizable(true);
        window->SetTitleColor(SF::Engine::Color());

        // TODO: viewports are editor-ish; once the game side exists, move this block into
        // EditorApplication::OnInit so plain games don't get an ImGui viewport.
        viewports.push_back(std::make_unique<SceneViewport>(UVec2{800, 600}));

        renderer->OnRecordViewports().Add(
                [this](VkCommandBuffer cmd, std::size_t frameIndex)
                {
                    for (auto &vp: viewports)
                    {
                        vp->Tick(frameIndex);
                        vp->PrepareForRender(cmd);
                        vp->BeginRendering(cmd);
                        // scene draws for this viewport go here

                        vp->EndRendering(cmd);
                        vp->PrepareForSample(cmd);
                    }
                });

        // Capture the raw viewport pointer, not a reference into the vector (it dangles if viewports grows).
        for (auto &vp: viewports)
            reg = UIRegistry::Get().Register([p = vp.get()] { DrawViewport(p); });

        RegisterDefaultComponentWidgets();
        return InitializationReturn::Success;
    }

    const std::string &Application::GetName() const noexcept
    {
        assert(Info);
        return Info->name;
    }

    void Application::SetName(const std::string &name) const
    {
        assert(Info);
        Info->name = name;
    }

    const Version &Application::GetVersion() const noexcept
    {
        assert(Info);
        return Info->version;
    }

    void Application::SetVersion(const Version &version) const noexcept
    {
        assert(Info);
        Info->version = version;
    }

    std::vector<File> Application::GetAllModules()
    {
        std::vector<File> result;

        const std::string exeDir = GetExecutablePath().parent_path().string();
        const char *pattern      = "*.module";
        for (const auto &file: File::GetFiles(exeDir, pattern, false))
        {
            result.emplace_back(file);
        }

        return result;
    }

    // Engine::Run() already drains the GPU and calls OnShutdown() before the application is
    // destroyed, and the engine is guaranteed to outlive it (see RunEngine), so there is nothing
    // left to do here. The only exception is a failed OnInit, which RunEngine handles itself.
    Application::~Application() = default;

    // ---------------------------------------------------------------------------------------------
    // Program entry
    // ---------------------------------------------------------------------------------------------
    namespace
    {
        void DrainGpu()
        {
            if (auto *rs = RenderSystem::Get(); rs && rs->GetLogicalDevice())
                vkDeviceWaitIdle(*rs->GetLogicalDevice());
        }
    } // namespace

    int RunEngine(int /*argc*/, char ** /*argv*/, ApplicationFactory factory)
    {
        if (!factory)
            return -1;

        // 1. Engine first: modules, RenderSystem + PostInit(). No game knowledge needed.
        const auto exeDir = Application::GetExecutablePath().parent_path();
        std::filesystem::current_path(exeDir);
        Engine engine(exeDir.string());

        // 2. Then the application. Declared AFTER the engine, so it is destroyed BEFORE it
        //    on every return path below.
        std::unique_ptr<Application> app = factory();
        if (!app)
        {
            Log::Error("Application factory returned null\n");
            return -2;
        }

        engine.SetGameInfo(app->Info.get());

        if (auto result = app->OnInit(engine); result != Application::InitializationReturn::Success)
        {
            Log::Error("Application::OnInit failed ({})\n", static_cast<int>(result));
            DrainGpu(); // OnInit may have created GPU resources before failing
            return -3;
        }

        // 3. The engine owns the loop. It calls back into app->OnUpdate() and app->OnShutdown().
        engine.SetApplication(app.get());
        const int32_t rc = engine.Run();

        // 4. Ordered teardown: application (viewports, panels) dies here, engine after.
        engine.SetApplication(nullptr);
        app.reset();
        return rc;
    }
} // namespace SF::Engine
