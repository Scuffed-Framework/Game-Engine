#pragma once
#include <Assets/AssetTextures.hpp>
#include <string>
#include "SplashScreenRLEData.hpp"

namespace SF::Engine
{
    using namespace std;

    namespace SplashScreenInternal
    {
        struct Config
        {
            float DotSize = 3.0f;
            float Gap     = 1.25f;

            SvgColor Background = SvgColor::Rgb(31, 31, 31);
            SvgColor Water      = SvgColor::Rgb(41, 41, 41);
            SvgColor Land       = SvgColor::Rgb(255, 169, 31);

            bool Glow = true;
        };

        constexpr SvgDocument Generate(span<const uint8_t> mask, Ui32Vec2 maskSize, Ui32Vec2 viewportSize,
                                       const Config &config = {.DotSize    = 3.0f,
                                                               .Gap        = 1.25,
                                                               .Background = SvgColor::Rgb(31, 31, 31),
                                                               .Water      = SvgColor::Rgb(41, 41, 41),
                                                               .Land       = SvgColor::Rgb(255, 169, 31),
                                                               .Glow       = false});

        constexpr shared_ptr<Image2d> Construct(Ui32Vec2 viewportSize, Ui32Vec2 outputSize,
                                                span<const uint8_t> mask = LiteralData,
                                                Ui32Vec2 maskSize        = {DATA_WIDTH, DATA_HEIGHT},
                                                const Config &config     = {});
    } // namespace SplashScreenInternal
} // namespace SF::Engine
