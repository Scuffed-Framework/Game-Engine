#pragma once
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

#include <3rdPartyLibs/Rocket.hpp>
#include <Engine/Module.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
#include "Time.hpp"

namespace SF::Engine
{
    /**
     * @brief Repeat count meaning "never stop". Pass it to Timers::Repeat, or simply use Timers::Every.
     */
    inline constexpr uint32_t InfiniteRepeat = std::numeric_limits<uint32_t>::max();

    /**
     * @brief A single scheduled timer owned by the Timers module.
     *
     * A Timer fires its OnTick() signal every @ref GetInterval "interval". It runs forever, a fixed number
     * of times, or until a stop condition returns true. Finished timers are removed by Timers.
     *
     * Timers are created through Timers::Once, Timers::Every, Timers::Repeat and Timers::RepeatUntil, which
     * return a shared TimerHandle. The handle stays valid (and Destroy() stays safe to call) even after the
     * timer has finished and been removed.
     *
     * @note OnTick() is emitted on the Timers worker thread, not the main thread.
     */
    class Timer
    {
        friend class Timers;

    public:
        /**
         * @brief Constructs a timer whose first tick happens one interval from now.
         * @param interval Time between ticks.
         * @param repeat   Number of ticks before the timer is removed. std::nullopt or #InfiniteRepeat run
         *                 forever; 0 creates an already-destroyed timer that never fires.
         */
        Timer(const DurationAsNanoseconds &interval, const std::optional<uint32_t> &repeat) :
            interval(interval), next(ClockNow() + interval), repeat(repeat)
        {
            if (this->repeat == InfiniteRepeat)
                this->repeat.reset();
            else if (this->repeat == 0u)
                destroyed = true;
        }

        /** @brief Returns the time between two consecutive ticks. */
        const DurationAsNanoseconds &GetInterval() const { return interval; }

        /**
         * @brief Returns the remaining tick count.
         * @return Ticks left, or std::nullopt if the timer runs forever.
         * @note Not synchronised with the worker thread; treat the value as a hint.
         */
        const std::optional<uint32_t> &GetRepeat() const { return repeat; }

        /** @brief Returns true once the timer has been destroyed or has finished. Thread-safe. */
        bool IsDestroyed() const { return destroyed; }

        /**
         * @brief Stops the timer. Thread-safe.
         *
         * No new tick starts after this call. A tick that is already executing on the worker thread
         * may still complete, so callbacks must not touch state that is freed right after Destroy().
         * Capture shared state (e.g. a std::shared_ptr) instead of raw pointers.
         */
        void Destroy() { destroyed = true; }

        /**
         * @brief Signal emitted every time the timer fires.
         * @return The tick signal; connect slots to it to receive ticks.
         * @warning Slots run on the Timers worker thread.
         */
        rocket::signal<void()> &OnTick() { return onTick; };

    private:
        DurationAsNanoseconds interval;      ///< Time between ticks.
        TimePoint next;                      ///< Time point of the next tick.
        std::optional<uint32_t> repeat;      ///< Remaining ticks, or nullopt for infinite.
        std::atomic_bool destroyed = false;  ///< Set by Destroy() or when the repeat count runs out.
        std::function<bool()> stopCondition; ///< Optional; checked before every tick, true destroys the timer.
        rocket::signal<void()> onTick;       ///< Emitted on every tick.
    };

    /** @brief Shared handle to a Timer. Keep it to call Timer::Destroy() later. */
    using TimerHandle = std::shared_ptr<Timer>;

    /**
     * @brief Module used for timed events.
     *
     * Owns a worker thread that sleeps until the earliest timer is due, then emits that timer's
     * Timer::OnTick() signal. Timers can fire once, a fixed number of times, forever, or until a
     * condition is met.
     *
     * All scheduling functions are thread-safe. The overloads taking an @p object connect the
     * callback to that instance (so it is disconnected automatically when the instance dies);
     * the overloads without it take a plain callable.
     *
     * @warning Callbacks and stop conditions execute on the worker thread. Anything they touch must be
     *          thread-safe (atomics, mutexes, or a queue drained on the main thread).
     */
    class Timers : public ModuleRegistrar<Timers>
    {
        REGISTER_MODULE_EX(Timers, Stage::Post, ModuleStartupStage::PreEngineInitComplete);

    public:
        /** @brief Starts the worker thread. */
        Timers()
        {
            std::unique_lock lock(mutex);
            worker = std::thread([this] { ThreadRun(); });
        }

        /** @brief Stops and joins the worker thread. Pending timers are dropped without firing. */
        ~Timers() override
        {
            {
                std::lock_guard lock(mutex); // set under the mutex so the worker cannot miss the wake-up
                stop = true;
            }
            condition.notify_all();
            worker.join();
        }

        /** @brief Per-frame module update. */
        void Update() override;

        /**
         * @brief Schedules a callback bound to an object to fire once after a delay.
         * @tparam Instance Type of the object the callback is connected to.
         * @param object   Instance the slot is bound to.
         * @param function Callback to invoke.
         * @param delay    Time to wait before the callback fires.
         * @return Handle to the created timer.
         */
        template<class Instance>
        TimerHandle Once(Instance *object, std::function<void()> &&function, const DurationAsNanoseconds &delay)
        {
            auto instance = std::make_shared<Timer>(delay, 1);
            instance->onTick.connect(object, std::move(function));
            return Add(std::move(instance));
        }

        /**
         * @brief Schedules a callback bound to an object to fire repeatedly, forever.
         * @tparam Instance Type of the object the callback is connected to.
         * @param object   Instance the slot is bound to.
         * @param function Callback to invoke on every tick.
         * @param interval Time between ticks.
         * @return Handle to the created timer.
         */
        template<class Instance>
        TimerHandle Every(Instance *object, std::function<void()> &&function, const DurationAsNanoseconds &interval)
        {
            auto instance = std::make_shared<Timer>(interval, std::nullopt);
            instance->onTick.connect(object, std::move(function));
            return Add(std::move(instance));
        }

        /**
         * @brief Schedules a callback bound to an object to fire a fixed number of times.
         * @tparam Instance Type of the object the callback is connected to.
         * @param object   Instance the slot is bound to.
         * @param function Callback to invoke on every tick.
         * @param interval Time between ticks.
         * @param repeat   Number of ticks before the timer is removed. #InfiniteRepeat never stops;
         *                 0 never fires.
         * @return Handle to the created timer.
         */
        template<class Instance>
        TimerHandle Repeat(Instance *object, std::function<void()> &&function, const DurationAsNanoseconds &interval,
                           uint32_t repeat)
        {
            auto instance = std::make_shared<Timer>(interval, repeat);
            instance->onTick.connect(object, std::move(function));
            return Add(std::move(instance));
        }

        /**
         * @brief Schedules a callback bound to an object to fire until a condition becomes true.
         * @tparam Instance Type of the object the callback is connected to.
         * @param object        Instance the slot is bound to.
         * @param function      Callback to invoke on every tick.
         * @param interval      Time between ticks.
         * @param stopCondition Evaluated on the worker thread before every tick. When it returns true the
         *                      timer is destroyed without firing again.
         * @return Handle to the created timer.
         */
        template<class Instance>
        TimerHandle RepeatUntil(Instance *object, std::function<void()> &&function,
                                const DurationAsNanoseconds &interval, std::function<bool()> &&stopCondition)
        {
            auto instance           = std::make_shared<Timer>(interval, std::nullopt);
            instance->stopCondition = std::move(stopCondition);
            instance->onTick.connect(object, std::move(function));
            return Add(std::move(instance));
        }

        /**
         * @brief Schedules a callable to fire once after a delay.
         * @param function Callable to invoke.
         * @param delay    Time to wait before it fires.
         * @return Handle to the created timer.
         */
        TimerHandle Once(std::function<void()> &&function, const DurationAsNanoseconds &delay)
        {
            auto instance = std::make_shared<Timer>(delay, 1);
            instance->onTick.connect(std::move(function));
            return Add(std::move(instance));
        }

        /**
         * @brief Schedules a callable to fire repeatedly, forever (until Timer::Destroy()).
         * @param function Callable to invoke on every tick.
         * @param interval Time between ticks.
         * @return Handle to the created timer.
         */
        TimerHandle Every(std::function<void()> &&function, const DurationAsNanoseconds &interval)
        {
            auto instance = std::make_shared<Timer>(interval, std::nullopt);
            instance->onTick.connect(std::move(function));
            return Add(std::move(instance));
        }

        /**
         * @brief Schedules a callable to fire a fixed number of times.
         * @param function Callable to invoke on every tick.
         * @param interval Time between ticks.
         * @param repeat   Number of ticks before the timer is removed. #InfiniteRepeat never stops;
         *                 0 never fires.
         * @return Handle to the created timer.
         */
        TimerHandle Repeat(std::function<void()> &&function, const DurationAsNanoseconds &interval, uint32_t repeat)
        {
            auto instance = std::make_shared<Timer>(interval, repeat);
            instance->onTick.connect(std::move(function));
            return Add(std::move(instance));
        }

        /**
         * @brief Schedules a callable to fire until a condition becomes true.
         *
         * @code
         * std::atomic_bool disabled = false;
         * Timers::Get()->RepeatUntil([] { Poll(); }, 100ms, [&] { return disabled.load(); });
         * @endcode
         *
         * @param function      Callable to invoke on every tick.
         * @param interval      Time between ticks.
         * @param stopCondition Evaluated on the worker thread before every tick. When it returns true the
         *                      timer is destroyed without firing again. It must be thread-safe and must
         *                      not outlive the state it reads.
         * @return Handle to the created timer.
         */
        TimerHandle RepeatUntil(std::function<void()> &&function, const DurationAsNanoseconds &interval,
                                std::function<bool()> &&stopCondition)
        {
            auto instance           = std::make_shared<Timer>(interval, std::nullopt);
            instance->stopCondition = std::move(stopCondition);
            instance->onTick.connect(std::move(function));
            return Add(std::move(instance));
        }

    private:
        /** @brief Registers a fully configured timer and wakes the worker. */
        TimerHandle Add(TimerHandle instance)
        {
            {
                std::lock_guard lock(mutex);
                timers.push_back(instance);
            }
            condition.notify_all();
            return instance;
        }

        /**
         * @brief Worker thread main loop.
         *
         * Reaps destroyed timers, sleeps until the earliest remaining one is due, evaluates its stop
         * condition and emits its tick with the mutex released, then advances it by one interval and
         * retires it once its repeat count reaches zero. Sleeps indefinitely while there are no timers.
         */
        void ThreadRun()
        {
            std::unique_lock lock(mutex);

            while (!stop)
            {
                std::erase_if(timers, [](const TimerHandle &t) { return t->IsDestroyed(); });

                if (timers.empty())
                {
                    condition.wait(lock);
                    continue;
                }

                const auto it        = std::ranges::min_element(timers, [](const TimerHandle &a, const TimerHandle &b)
                                                                { return a->next < b->next; });
                const TimerHandle& instance = *it; // keep alive while the mutex is released

                if (const auto time = ClockNow(); time < instance->next)
                {
                    condition.wait_for(lock, std::chrono::nanoseconds(instance->next - time));
                    continue;
                }

                lock.unlock();
                const bool shouldStop = instance->stopCondition && instance->stopCondition();
                if (!shouldStop && !instance->IsDestroyed())
                    instance->onTick();
                lock.lock();

                if (shouldStop)
                {
                    instance->Destroy();
                    continue;
                }

                instance->next += instance->interval;
                if (instance->repeat && --*instance->repeat == 0)
                    instance->Destroy(); // reaped at the top of the loop
            }
        }

        std::vector<TimerHandle> timers; ///< All active timers, guarded by #mutex.

        std::atomic_bool stop = false; ///< Set to request worker shutdown.
        std::thread worker;            ///< Thread running ThreadRun().

        std::mutex mutex;                  ///< Guards #timers and the non-atomic Timer fields.
        std::condition_variable condition; ///< Wakes the worker when timers change or on stop.
    };
} // namespace SF::Engine
