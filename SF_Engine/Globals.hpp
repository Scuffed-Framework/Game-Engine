#pragma once
#include <type_traits>
#include <vector>
namespace SF::Engine
{


    constexpr std::string EngineCVarNamespace       = "SFE";
    constexpr std::string RenderSystemCVarNamespace = "RS";

} // namespace SF::Engine
#if not defined NODISCARD
    #define NODISCARD [[nodiscard]]
#endif

#if not defined DEPRECATED
    #define DEPRECATED(x) __attribute__((__deprecated__("use '" x "' instead")))
#endif
