#pragma once
#include <Assets/Video/Video.hpp>
#include <Platform/Windowing/WindowManager.hpp>
#include <Rendering/RHI/Images/Image.hpp>
#include <atomic>
#include "SplashScreenImage.hpp"
#include "SplashScreenQuotes.hpp"

namespace SF::Engine
{

    // Editor & Engine have their splashscreens, Editor is outside of a window as a floating image, like Unreal or Unity
    // Engine is in the window, a video/gif, and more images, credits

    // show whilst shaders compile?
    class SplashScreenRenderer
    {
        [[nodiscard]] std::string RandomQuote() const
        {
            return std::string(Quotes[Mathematics::Rand(static_cast<std::size_t>(0), Quotes.size() - 1)]);
        }

    public:
        virtual ~SplashScreenRenderer() = default;

        /**
         * @breif call when we know
         */
        void Init()
        {
            auto *WndMgr = WindowManager::Get();
            if (IsEditor)
            {
                SplashScreenWindow = WndMgr->AddWindow();
                SplashScreenWindow->SetSize(WndMgr->GetPrimaryMonitor()->GetSize() / 2);
            } else
            {
                SplashScreenWindow = WndMgr->GetWindow(0);
            }

            const Ui32Vec2 size = SplashScreenWindow->GetSize();

            Images.emplace_back(SplashScreenInternal::Construct(size, size), std::chrono::seconds(5), RandomQuote());
            CurrentImage   = 0;
            ImageStartTime = std::chrono::steady_clock::now();
            Showing        = true;
        }

        void Show()
        {
            if (!Showing)
                return;

            const ImGuiViewport *vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->Pos);
            ImGui::SetNextWindowSize(vp->Size);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Vec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Vec2(0, 0));
            ImGui::Begin("###splashscreen", nullptr, Flags);

            if (CurrentImage < Images.size())
            {
                auto &[img, sec, quote] = Images[CurrentImage];

                const bool isLast   = CurrentImage + 1 == Images.size();
                const bool holdLast = IsEditor && isLast && !LoadingDone.load();

                if (!holdLast && std::chrono::steady_clock::now() - ImageStartTime >= sec)
                {
                    ++CurrentImage;
                    ImageStartTime = std::chrono::steady_clock::now();
                } else
                {
                    // Reserve room for the bar only when it is actually drawn.
                    const float barHeight = IsEditor ? ImGui::GetFrameHeight() : 0.0f;
                    const Vec2 avail      = ImGui::GetContentRegionAvail();

                    ImGui::Image(img->GetTexID(), Vec2(avail.x, avail.y - barHeight));

                    if (IsEditor)
                    {
                        const Vec2 origin      = ImGui::GetWindowPos();
                        const Vec2 extent      = ImGui::GetWindowSize();
                        constexpr float margin = 24.0f;

                        ImDrawList *draw = ImGui::GetWindowDrawList();
                        ImFont *font     = ImGui::GetFont();

                        const float quoteSize = ImGui::GetFontSize();
                        const float titleSize = quoteSize * 1.8f;
                        const float wrapWidth = extent.x - margin * 2.0f;

                        // Measure the wrapped quote so multi-line quotes grow upward.
                        const Vec2 quoteExtent = font->CalcTextSizeA(quoteSize, FLT_MAX, wrapWidth, quote.c_str());

                        const float quoteY = origin.y + extent.y - barHeight - margin - quoteExtent.y;
                        const float titleY = quoteY - titleSize - 8.0f;

                        draw->AddText(font, titleSize, Vec2(origin.x + margin, titleY), IM_COL32(255, 255, 255, 255),
                                      "Scuffed Framework Game Engine");
                        draw->AddText(font, quoteSize, Vec2(origin.x + margin, quoteY), IM_COL32(255, 255, 255, 220),
                                      quote.c_str(), nullptr, wrapWidth);

                        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), Vec2(-1.0f, barHeight),
                                           "Loading...");
                    }
                }
            }

            ImGui::End();
            ImGui::PopStyleVar(2); // WindowPadding + ItemSpacing

            if (CurrentImage >= Images.size())
                Showing = false;
        }

        void Cancel() { Showing = false; }

        void AddImage(const std::shared_ptr<Image2d> &img, std::chrono::seconds time)
        {
            Images.emplace_back(img, time, RandomQuote());
        }

        bool IsEditor = false;
        bool Showing  = false;

        std::string Quote;
        std::vector<
                std::tuple<std::shared_ptr<Image2d> /*Image*/, std::chrono::seconds /*Time*/, std::string /*Quote*/>>
                Images;
        Window *SplashScreenWindow = nullptr;
        ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoScrollWithMouse;
        std::atomic<bool> LoadingDone{false};

        std::size_t CurrentImage                             = 0;
        std::chrono::steady_clock::time_point ImageStartTime = {};
    };
} // namespace SF::Engine
