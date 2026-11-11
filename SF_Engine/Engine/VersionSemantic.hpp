#pragma once
#include "Version.hpp"

namespace SF::Engine
{

    class Version
    {
    public:
        constexpr Version(std::uint16_t major = Engine_VERSION_MAJOR, std::uint8_t minor = Engine_VERSION_MINOR,
                          std::uint16_t patch = Engine_VERSION_PATCH) noexcept :
            major(major), minor(minor), patch(patch)
        {
        }

        constexpr ~Version() = default;

        [[nodiscard]] constexpr bool operator==(const Version &other) const noexcept
        {
            return major == other.major && minor == other.minor && patch == other.patch;
        }

        [[nodiscard]] constexpr bool operator!=(const Version &other) const noexcept { return !(*this == other); }

        [[nodiscard]] constexpr bool operator<(const Version &other) const noexcept
        {
            if (major != other.major)
                return major < other.major;
            if (minor != other.minor)
                return minor < other.minor;
            return patch < other.patch;
        }

        [[nodiscard]] constexpr std::string_view ToString() const noexcept
        {
            return std::string_view(std::to_string(major) + "." + std::to_string((minor)) + "." +
                                    std::to_string(patch));
        }

        std::uint16_t major;
        std::uint8_t minor;
        std::uint16_t patch;
    };
} // namespace SF::Engine
