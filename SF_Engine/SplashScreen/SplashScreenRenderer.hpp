#pragma once
#include <Assets/Video/Video.hpp>
#include <Platform/Windowing/WindowManager.hpp>
#include <Rendering/RHI/Images/Image.hpp>
#include "SplashScreenImage.hpp"
#include "SplashScreenQuotes.hpp"

namespace SF::Engine
{
    using namespace std;
    // Editor & Engine have their splashscreens, Editor is outside of a window as a floating image, like Unreal or Unity
    // Engine is in the window, a video/gif, and more images, credits

    class SplashScreenRenderer
    {
        [[nodiscard]] string RandomQuote() const
        {
            return string(Quotes[Mathematics::Rand(static_cast<size_t>(0), Quotes.size() - 1)]);
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

            Images.emplace_back(SplashScreenInternal::Construct(size, size), chrono::seconds(5), RandomQuote());
            CurrentImage   = 0;
            ImageStartTime = chrono::steady_clock::now();
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
            ImGui::Begin("###splashscreen", nullptr, Flags);

            if (CurrentImage < Images.size())
            {
                auto &[img, sec, quote] = Images[CurrentImage];

                if (chrono::steady_clock::now() - ImageStartTime >= sec)
                {
                    ++CurrentImage;
                    ImageStartTime = chrono::steady_clock::now();
                } else
                {
                    const float barHeight = ImGui::GetFrameHeight();
                    const Vec2 avail      = ImGui::GetContentRegionAvail();
                    ImGui::Image(img->GetTexID(), ImGui::GetContentRegionAvail());
                    if (IsEditor)
                    {
                        const Vec2 origin      = ImGui::GetWindowPos();
                        const Vec2 extent      = ImGui::GetWindowSize();
                        constexpr float margin = 24.0f;

                        ImDrawList *draw = ImGui::GetWindowDrawList();
                        ImFont *font     = ImGui::GetFont();

                        const float quoteSize = ImGui::GetFontSize();
                        const float titleSize = quoteSize * 1.8f;

                        const float quoteY = origin.y + extent.y - barHeight - margin - quoteSize;
                        const float titleY = quoteY - titleSize - 8.0f;

                        draw->AddText(font, titleSize, Vec2(origin.x + margin, titleY), IM_COL32(255, 255, 255, 255),
                                      "Scuffed Framework Game Engine");
                        draw->AddText(font, quoteSize, Vec2(origin.x + margin, quoteY), IM_COL32(255, 255, 255, 220),
                                      quote.c_str(), nullptr, extent.x - margin * 2.0f);

                        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), Vec2(-1.0f, barHeight),
                                           "Loading...");
                    }
                }
            }

            ImGui::End();
            ImGui::PopStyleVar();
            if (CurrentImage >= Images.size())
                Showing = false;
        }

        void Cancel() { Showing = false; }

        void AddImage(const shared_ptr<Image2d> &img, chrono::seconds time)
        {
            Images.emplace_back(img, time, RandomQuote());
        }

        bool IsEditor = false;
        bool Showing  = false;

        string Quote;
        vector<tuple<shared_ptr<Image2d> /*Image*/, chrono::seconds /*Time*/, string /*Quote*/>> Images;
        Window *SplashScreenWindow = nullptr;
        ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoScrollWithMouse;

        size_t CurrentImage                             = 0;
        chrono::steady_clock::time_point ImageStartTime = {};
    };
} // namespace SF::Engine
