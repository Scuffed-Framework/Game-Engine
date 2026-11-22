#pragma once
#include <Globals.hpp>
#include <atomic>

namespace SF::Engine
{
    /**
     * @brief spinlock type, declare as @code spinlock_t spinlock{0}; @endcode
     */
    typedef /*volatile*/ STD atomic_int32_t spinlock_t;

    class Spinlock
    {
        static void Lock(spinlock_t *lock) noexcept;
        static void Unlock(spinlock_t *lock) noexcept;
    };
} // namespace SF::Engine
