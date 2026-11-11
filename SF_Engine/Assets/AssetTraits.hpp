#pragma once
#include <filesystem>
#include <type_traits>
#include <utility>

namespace SF::Engine
{

    template<typename T, typename = void>
    struct HasWriteMember : std::false_type
    {
    };
    template<typename T>
    struct HasWriteMember<T, std::void_t<decltype(&T::Write)>> : std::true_type
    {
    };

    // True if T::Write is callable as Write(const Base&, const path&).
    template<typename T, typename Base, typename = void>
    struct HasValidWrite : std::false_type
    {
    };

    template<typename T, typename Base>
    struct HasValidWrite<T, Base,
                         std::void_t<decltype(T::Write(std::declval<const Base &>(),
                                                       std::declval<const std::filesystem::path &>()))>>
        : std::true_type
    {
    };

} // namespace SF::Engine
