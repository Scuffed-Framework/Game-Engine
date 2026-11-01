#include "ImGuiSFERenderpass.hpp"

#include <Platform/Windowing/WindowManager.hpp>
#include <Rendering/FrameGraph/Stage.hpp>
#include <Rendering/RHI/Commands/CommandBuffer.hpp>
#include <Rendering/RHI/Renderpass/RhiRenderpass.hpp>
#include <Rendering/RenderSystem.hpp>

#include <SDL3/SDL.h>

#include <Configuration/Default/ImGuiDefault.hpp>
#include <stdexcept>
#include "CedricGuillemet/ImGuizmo.h"
#include "epezent/ImPlot.hpp"

namespace SF::Engine
{
    ImGuiSFERenderpass::ImGuiSFERenderpass(const Pipeline::Stage &stage) : EngineRenderpass(stage) { Init(); }

    ImGuiSFERenderpass::~ImGuiSFERenderpass() { Shutdown(); }

    void ImGuiSFERenderpass::CreateDescriptorPool()
    {
        auto *logDevice = RenderSystem::Get()->GetLogicalDevice();

        constexpr VkDescriptorPoolSize poolSize{.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                .descriptorCount = 1000};

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets       = 1000;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = &poolSize;

        RenderSystem::CheckVkResult(vkCreateDescriptorPool(*logDevice, &poolInfo, nullptr, &imguiPool_));
    }

    void ImGuiSFERenderpass::Init()
    {
        if (s_backendInitialized)
        {
            // Another instance already owns the context; this instance is a duplicate and should not re-initialize
            // anything. Log an error because this isn't supposed to happen
            Log::Error("[ImGui Pass Backend] ImGui Backend tried initializing, but was previously initialized!");
            return;
        }

        const auto *renderSystem = RenderSystem::Get();
        auto *physDevice         = renderSystem->GetPhysicalDevice();
        auto *logDevice          = renderSystem->GetLogicalDevice();
        const auto *window       = WindowManager::Get()->GetWindow(0);

        if (!window)
            throw std::runtime_error("ImGuiSFERenderpass: no window available");

        SDL_Window *wnd = window->GetWindow();
        if (!wnd)
            throw std::runtime_error("ImGuiSFERenderpass: null wnd");

        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplSDL3_InitForVulkan(wnd))
            throw std::runtime_error("ImGuiSFERenderpass: ImGui_ImplSDL3_InitForVulkan failed");

        CreateDescriptorPool();

        const auto *renderStage = renderSystem->GetRenderStage(GetStage().first);
        if (!renderStage || !renderStage->GetRenderpass())
            throw std::runtime_error("ImGuiSFERenderpass: render stage / renderpass not ready");

        VkRenderPass vkRenderPass = renderStage->GetRenderpass()->GetRenderpass();

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion      = VK_API_VERSION_1_2;
        initInfo.Instance        = renderSystem->GetInstance()->GetInstance();
        initInfo.PhysicalDevice  = physDevice->GetPhysicalDevice();
        initInfo.Device          = logDevice->GetLogicalDevice();
        initInfo.QueueFamily     = logDevice->GetGraphicsFamily();
        initInfo.Queue           = logDevice->GetGraphicsQueue();
        initInfo.PipelineCache   = renderSystem->GetPipelineCache();
        initInfo.DescriptorPool  = imguiPool_;
        initInfo.MinImageCount   = 2;
        initInfo.ImageCount      = renderSystem->GetSwapchain(0)->GetImageCount();
        initInfo.Allocator       = nullptr;
        initInfo.CheckVkResultFn = [](const VkResult r) { RenderSystem::CheckVkResult(r); };

        initInfo.PipelineInfoMain.RenderPass = vkRenderPass;
        initInfo.PipelineInfoMain.Subpass    = GetStage().second;
        // ImGui renders into the swapchain subpass which is always 1x (non-multisampled).
        // Passing physDevice->GetMsaaSamples() here would cause a spec violation because
        // the swapchain attachment sample count is 1x regardless of device MSAA capability.
        initInfo.PipelineInfoMain.MSAASamples =
                renderStage->IsMultisampled(GetStage().second) ? physDevice->GetMsaaSamples() : VK_SAMPLE_COUNT_1_BIT;

        if (!ImGui_ImplVulkan_Init(&initInfo))
            throw std::runtime_error("ImGuiSFERenderpass: ImGui_ImplVulkan_Init failed");

        initialized_         = true;
        s_backendInitialized = true;

        ImGuiDefaultStyle::SetStyle();
    }

    void ImGuiSFERenderpass::Shutdown()
    {
        if (!initialized_)
            return;

        vkDeviceWaitIdle(*RenderSystem::Get()->GetLogicalDevice());

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        if (imguiPool_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(*RenderSystem::Get()->GetLogicalDevice(), imguiPool_, nullptr);
            imguiPool_ = VK_NULL_HANDLE;
        }

        initialized_         = false;
        s_backendInitialized = false;
    }

    void ImGuiSFERenderpass::BuildUI()
    {
        if (drawCallback_)
            drawCallback_();
    }

    void ImGuiSFERenderpass::Render(const CommandBuffer &commandBuffer)
    {
        if (!initialized_)
            return;

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        ImGui::DockSpaceOverViewport(0u, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
        BuildUI();

        ImGui::Render();
        if (ImDrawData *drawData = ImGui::GetDrawData(); drawData && drawData->TotalVtxCount > 0)
            ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
    }
} // namespace SF::Engine
