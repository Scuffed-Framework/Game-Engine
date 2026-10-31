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
    public:
        virtual ~SplashScreenRenderer() = default;
        void Init()
        {
            const uint8_t rand  = Mathematics::Rand(static_cast<size_t>(0), Quotes.size());
            const Ui32Vec2 size = SplashScreenWindow->GetSize();
            Images.emplace_back(SplashScreenInternal::Construct(size, size), 5.0f, Quotes[rand]);
        }

        virtual void Show();
        void Stop();
        void AddImage(Image2d, float);

        string Quote;
        vector<tuple<shared_ptr<Image2d> /*img*/, float /*time*/, string /*Quote*/>> Images;
        unique_ptr<Window> SplashScreenWindow;
    };
} // namespace SF::Engine
