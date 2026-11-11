#include "SplashScreenImage.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace SF::Engine::SplashScreenInternal
{
    namespace
    {
        struct DotLayout
        {
            std::size_t Cols;
            std::size_t Rows;
            float Pitch;
            float Radius;
        };

        std::string Number(const float value)
        {
            std::ostringstream stream;
            stream.precision(2);
            stream << std::fixed << value;

            std::string result = stream.str();

            while (!result.empty() && result.back() == '0')
                result.pop_back();

            if (!result.empty() && result.back() == '.')
                result.pop_back();

            return result;
        }

        bool GetBit(const std::span<const uint8_t> mask, const size_t index)
        {
            return (mask[index / 8] & (1u << (7 - (index % 8)))) != 0;
        }

        void AppendDot(std::string &path, const float x, const float y, const float radius)
        {
            const float diameter = radius * 2.0f;

            path += "M" + Number(x - radius) + " " + Number(y);
            path += "a" + Number(radius) + " " + Number(radius);
            path += " 0 1 0 " + Number(diameter) + " 0";
            path += "a" + Number(radius) + " " + Number(radius);
            path += " 0 1 0 -" + Number(diameter) + " 0";
        }

        bool IsExtraIsland(const size_t x, const size_t y, const DotLayout &layout)
        {
            static constexpr std::array<std::array<float, 2>, 4> islands = {
                    {{-155.5f, 19.6f}, {-156.3f, 20.8f}, {-157.97f, 21.45f}, {-159.5f, 22.05f}}};

            return std::ranges::any_of(
                    islands,
                    [&](const auto &island)
                    {
                        const auto ix = static_cast<size_t>(std::floor((island[0] + 180.0f) / 360.0f * layout.Cols));

                        const auto iy = static_cast<size_t>(std::floor((90.0f - island[1]) / 180.0f * layout.Rows));

                        return x == ix && y == iy;
                    });
        }

        float GetCoverage(const std::span<const uint8_t> mask, const Ui32Vec2 maskSize, const DotLayout &layout,
                          const size_t x, const size_t y)
        {
            const double sx0 = static_cast<double>(x) * maskSize.x / static_cast<double>(layout.Cols);
            const double sx1 = static_cast<double>(x + 1) * maskSize.x / static_cast<double>(layout.Cols);

            const double sy0 = static_cast<double>(y) * maskSize.y / static_cast<double>(layout.Rows);
            const double sy1 = static_cast<double>(y + 1) * maskSize.y / static_cast<double>(layout.Rows);

            const auto ix0 = static_cast<size_t>(sx0);
            const auto iy0 = static_cast<size_t>(sy0);

            const size_t ix1 = min(static_cast<size_t>(ceil(sx1)), static_cast<size_t>(maskSize.x));
            const size_t iy1 = min(static_cast<size_t>(ceil(sy1)), static_cast<size_t>(maskSize.y));

            double landArea        = 0.0;
            const double totalArea = (sx1 - sx0) * (sy1 - sy0);

            for (size_t sy = iy0; sy < iy1; ++sy)
            {
                const double overlapY =
                        max(0.0, min(sy1, static_cast<double>(sy + 1)) - max(sy0, static_cast<double>(sy)));

                for (size_t sx = ix0; sx < ix1; ++sx)
                {
                    if (!GetBit(mask, sy * maskSize.x + sx))
                        continue;

                    const double overlapX =
                            max(0.0, min(sx1, static_cast<double>(sx + 1)) - max(sx0, static_cast<double>(sx)));

                    landArea += overlapX * overlapY;
                }
            }

            return totalArea > 0.0 ? static_cast<float>(landArea / totalArea) : 0.0f;
        }
    } // namespace

    constexpr SvgDocument Generate(std::span<const uint8_t> mask, Ui32Vec2 maskSize, Ui32Vec2 viewportSize,
                                   const Config &config)
    {
        if (maskSize.x == 0 || maskSize.y == 0 || viewportSize.x == 0 || viewportSize.y == 0 ||
            config.DotSize <= 0.0f || config.Gap <= 0.0f)
            throw std::invalid_argument("Invalid dot matrix dimensions or configuration.");

        const size_t pixelCount = static_cast<size_t>(maskSize.x) * maskSize.y;

        if (const size_t requiredBytes = (pixelCount + 7) / 8; mask.size() < requiredBytes)
            throw std::invalid_argument("Incomplete bitmask.");

        DotLayout layout{};

        layout.Pitch = min(config.DotSize * config.Gap, static_cast<float>(viewportSize.x) / 60.0f);

        layout.Cols = static_cast<size_t>(static_cast<float>(viewportSize.x) / layout.Pitch);
        layout.Rows = layout.Cols / 2;

        if (static_cast<float>(layout.Rows) * layout.Pitch > static_cast<float>(viewportSize.y))
        {
            layout.Rows = static_cast<size_t>(static_cast<float>(viewportSize.y) / layout.Pitch);
            layout.Cols = layout.Rows * 2;
        }

        if (layout.Cols == 0 || layout.Rows == 0)
            throw std::invalid_argument("Viewport is too small.");

        layout.Radius = (layout.Pitch / config.Gap) * 0.5f;

        std::string landPath;
        std::string waterPath;

        for (size_t y = 0; y < layout.Rows; ++y)
        {
            for (size_t x = 0; x < layout.Cols; ++x)
            {
                const bool land =
                        GetCoverage(mask, maskSize, layout, x, y) * 255.0f > 120.0f || IsExtraIsland(x, y, layout);

                const float cx = (static_cast<float>(x) + 0.5f) * layout.Pitch;
                const float cy = (static_cast<float>(y) + 0.5f) * layout.Pitch;

                AppendDot(land ? landPath : waterPath, cx, cy, layout.Radius);
            }
        }

        SvgDocument document;

        document.width   = static_cast<float>(layout.Cols) * layout.Pitch;
        document.height  = static_cast<float>(layout.Rows) * layout.Pitch;
        document.viewBox = std::array{0.0f, 0.0f, document.width, document.height};

        // Background
        SvgRect background;
        background.size = {document.width, document.height};
        background.style.WithFill(config.Background);

        document.elements.emplace_back(std::move(background));

        // Water dots
        SvgPath water;
        water.data = std::move(waterPath);
        water.style.WithFill(config.Water);

        document.elements.emplace_back(std::move(water));

        // Optional land glow
        if (config.Glow)
        {
            // Requires SVG filter support in the serializer.
            // See note below.
        }

        // Land dots
        SvgPath land;
        land.data = std::move(landPath);
        land.style.WithFill(config.Land);

        document.elements.emplace_back(std::move(land));

        return document;
    }

    constexpr std::shared_ptr<Image2d> Construct(const Ui32Vec2 viewportSize, Ui32Vec2 outputSize,
                                                 const std::span<const uint8_t> mask, const Ui32Vec2 maskSize,
                                                 const Config &config)
    {
        const std::string svg = Generate(mask, maskSize, viewportSize, config).ToString();

        auto bitmap = std::make_unique<Bitmap>();
        BitmapSvg::LoadFromMemory(*bitmap, svg, {outputSize});

        return make_shared<Image2d>(std::move(bitmap));
    }
} // namespace SF::Engine::SplashScreenInternal
