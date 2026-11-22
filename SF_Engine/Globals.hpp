#pragma once
#include <type_traits>
#include <vector>

#ifdef STD
    #undef STD
    #define STD std::
#endif
#ifndef STD
    #define STD std::
#endif

#ifndef NODISCARD
    #define NODISCARD [[nodiscard]]
#endif

#ifndef DEPRECATED
    #define DEPRECATED(x) __attribute__((__deprecated__("use '" x "' instead")))
#endif

#ifndef CAPABILITY
    #define CAPABILITY(x) __attribute__((capability(x)))
#endif

#if defined(_MSC_VER)
    #define FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define FORCE_INLINE [[gnu::always_inline]] inline
#else
    #define FORCE_INLINE inline
#endif

#define MEMORY_BARRIER() std::atomic_signal_fence(std::memory_order_seq_cst)

#if defined(__GNUC__) || defined(__clang__)
    #define PREFETCH(addr) __builtin_prefetch((addr), 0, 3)  // 0 = read, 3 = high temporal locality
    #define PREFETCHW(addr) __builtin_prefetch((addr), 1, 3) // 1 = write intent
#elif defined(_MSC_VER)
    #include <intrin.h>
    #if defined(_M_X64) || defined(_M_IX86)
        #define PREFETCH(addr) _mm_prefetch((const char *) (addr), _MM_HINT_T0)
        #define PREFETCHW(addr) _m_prefetchw((volatile const void *) (addr))
    #elif defined(_M_ARM64)
        #define PREFETCH(addr) __prefetch((const void *) (addr))
        #define PREFETCHW(addr) __prefetchw((const void *) (addr))
    #endif
#else
    #define PREFETCH(addr) ((void) 0)
    #define PREFETCHW(addr) ((void) 0)
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #if defined(_MSC_VER)
        #include <intrin.h>
        #define PAUSE() _mm_pause()
    #else
        #define PAUSE() __asm__ __volatile__("pause")
    #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    #if defined(_MSC_VER)
        #include <intrin.h>
        #define PAUSE() __yield()
    #else
        #define PAUSE() __asm__ __volatile__("yield")
    #endif
#else
    #define PAUSE() ((void) 0)
#endif
#define CPU_RELAX() PAUSE()

#if defined(_WIN32) || defined(__CYGWIN__)
    #define NO_MANGLE extern "C"
    #define NO_MANGLE_EXPORT extern "C" __declspec(dllexport)
#else
    #define NO_MANGLE extern "C" __attribute__((visibility("default")))
    #define NO_MANGLE_EXPORT extern "C" __attribute__((visibility("default")))
#endif
namespace SF::Engine
{
    inline constexpr STD string EngineCVarNamespace       = "SFE";
    inline constexpr STD string RenderSystemCVarNamespace = "RS";
} // namespace SF::Engine
