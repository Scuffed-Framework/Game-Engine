#include "Spinlock.hpp"
namespace SF::Engine
{
    void Spinlock::Lock(spinlock_t *lock) noexcept
    {
        int32_t expected = 0;
        // Use acquire semantics so hardware reads after the lock aren't reordered before it
        while (!atomic_compare_exchange_weak_explicit(lock, &expected, 1, STD memory_order_acquire,
                                                      STD memory_order_relaxed))
        {
            expected = 0;
            // Spin on a read-only load until the lock looks free
            while (atomic_load_explicit(lock, STD memory_order_relaxed) != 0)
            {
                PAUSE(); // asm volatile("pause")
            }
        }
    }
    
    void Spinlock::Unlock(spinlock_t *lock) noexcept
    {
        // Release semantics ensure all hardware writes finish before releasing the lock
        atomic_store_explicit(lock, 0, STD memory_order_release);
    }
} // namespace SF::Engine
