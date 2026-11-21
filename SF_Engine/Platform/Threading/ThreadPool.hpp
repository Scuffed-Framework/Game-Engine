/******************************************************************************/
/* ThreadPool.hpp                                                             */
/******************************************************************************/
/*                            This file is part of                            */
/*                                SF Game Engine                              */
/******************************************************************************/
/* MIT License                                                                */
/*                                                                            */
/* Copyright (c) 2025-present Noah Lee                                        */
/*                                                                            */
/* May all those that this source may reach be blessed by the LORD and find   */
/* peace and joy in life.                                                     */
/* Everyone who drinks of this water will be thirsty again; but whoever       */
/* drinks of the water that I will give him shall never thirst; John 4:13-14  */
/*                                                                            */
/* Permission is hereby granted, free of charge, to any person obtaining a    */
/* copy of this software and associated documentation files (the "Software"), */
/* to deal in the Software without restriction, including without limitation */
/* the rights to use, copy, modify, merge, publish, distribute, sublicense,  */
/* and/or sell copies of the Software, and to permit persons to whom the      */
/* Software is furnished to do so, subject to the following conditions:       */
/*                                                                            */
/* The above copyright notice and this permission notice shall be included in */
/* all copies or substantial portions of the Software.                        */
/*                                                                            */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS    */
/* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF                 */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.     */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY       */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT  */
/* OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE      */
/*                                                                            */
/******************************************************************************/

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace SF::Engine
{
    inline namespace Threads
    {
        [[nodiscard]] inline uint32_t
        CalculateOptimalNumWorkerThreads(float ratio = 1.0f, uint32_t minThreads = 1,
                                         uint32_t maxThreads      = std::thread::hardware_concurrency(),
                                         uint32_t reservedThreads = 1)
        {
            if (maxThreads == 0)
                maxThreads = 1;

            const uint32_t availableThreads = maxThreads - std::min(maxThreads, reservedThreads);

            return std::max(minThreads, static_cast<uint32_t>(std::lround(std::clamp(ratio, 0.0f, 1.0f) *
                                                                          static_cast<float>(availableThreads))));
        }
    } // namespace Threads

    /**
     * @brief A fixed-size pool of worker threads.
     *
     * Enqueue() submits a task and returns a future containing its result.
     * Dispatch() submits a fire-and-forget task (an escaping exception terminates).
     * ParallelFor()/ParallelForEach() are the pool-backed equivalent of
     * std::for_each(std::execution::par, ...). The calling thread participates, so
     * they are safe to call from inside a pool task (no nested-wait deadlock) and
     * they do not depend on Wait(), which waits for *all* work in the pool.
     * Wait() blocks until all currently queued and executing tasks have completed.
     * Never call Wait() from a worker thread.
     */
    class ThreadPool
    {
    public:
        explicit ThreadPool(uint32_t threadCount = CalculateOptimalNumWorkerThreads());

        ~ThreadPool();

        ThreadPool(const ThreadPool &)            = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;

        ThreadPool(ThreadPool &&)            = delete;
        ThreadPool &operator=(ThreadPool &&) = delete;

        template<typename F, typename... Args>
        [[nodiscard]] auto Enqueue(F &&f, Args &&...args);

        template<typename F, typename... Args>
        void Dispatch(F &&f, Args &&...args);

        /**
         * @brief Run fn(i) for every i in [first, last) across the pool and the calling thread.
         *
         * Blocks until every index has been processed. The first exception thrown by fn is
         * rethrown here; remaining unstarted chunks are skipped. grain = indices per chunk
         * (0 = automatic). fn must be safe to call concurrently.
         */
        template<std::integral Index, typename Fn>
        void ParallelFor(Index first, Index last, Fn &&fn, Index grain = 0);

        /**
         * @brief Run fn(element) for every element of a random-access range in parallel.
         */
        template<std::ranges::random_access_range R, typename Fn>
        void ParallelForEach(R &&range, Fn &&fn, std::size_t grain = 0);

        void Wait();

        [[nodiscard]] const std::vector<std::thread> &GetWorkers() const noexcept { return workers; }

        [[nodiscard]] uint32_t GetThreadCount() const noexcept { return static_cast<uint32_t>(workers.size()); }

    private:
        void Submit(std::function<void()> task);

        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;

        std::mutex queueMutex;

        std::condition_variable condition;
        std::condition_variable completionCondition;

        uint32_t activeTasks = 0;
        bool stop            = false;
    };

    inline void ThreadPool::Submit(std::function<void()> task)
    {
        {
            std::unique_lock<std::mutex> lock(queueMutex);

            if (stop)
                throw std::runtime_error("Task submitted to a stopped ThreadPool");

            tasks.push(std::move(task));
        }

        condition.notify_one();
    }

    template<typename F, typename... Args>
    auto ThreadPool::Enqueue(F &&f, Args &&...args)
    {
        using ReturnType = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
                [fn = std::forward<F>(f), ... captured = std::forward<Args>(args)]() mutable -> ReturnType
                { return std::invoke(std::move(fn), std::move(captured)...); });

        auto result = task->get_future();

        Submit([task] { (*task)(); });

        return result;
    }

    template<typename F, typename... Args>
    void ThreadPool::Dispatch(F &&f, Args &&...args)
    {
        auto call = [fn = std::forward<F>(f), ... captured = std::forward<Args>(args)]() mutable
        { std::invoke(std::move(fn), std::move(captured)...); };

        // std::function needs a copyable callable; keep move-only captures behind a shared_ptr.
        Submit([task = std::make_shared<decltype(call)>(std::move(call))] { (*task)(); });
    }

    template<std::integral Index, typename Fn>
    void ThreadPool::ParallelFor(Index first, Index last, Fn &&fn, Index grain)
    {
        if (last <= first)
            return;

        const auto count = static_cast<std::size_t>(last - first);

        // Workers + the calling thread; ~4 chunks per lane balances load without much overhead.
        const std::size_t lanes = static_cast<std::size_t>(GetThreadCount()) + 1;
        const std::size_t chunk =
                grain > 0 ? static_cast<std::size_t>(grain) : std::max<std::size_t>(1, count / (lanes * 4));
        const std::size_t chunkCount = (count + chunk - 1) / chunk;

        struct State
        {
            std::atomic<std::size_t> next{0};
            std::atomic<std::size_t> done{0};
            std::atomic<bool> failed{false};
            std::mutex errorMutex;
            std::exception_ptr error;
        };

        // Helpers may start after we return (they then find no chunks left), so shared state
        // is kept alive by shared_ptr. fn is only touched while a claimed chunk is unfinished,
        // i.e. while the caller is still blocked below.
        auto state  = std::make_shared<State>();
        auto *fnPtr = std::addressof(fn);

        auto drain = [state, fnPtr, first, count, chunk, chunkCount]
        {
            for (;;)
            {
                const std::size_t c = state->next.fetch_add(1, std::memory_order_relaxed);
                if (c >= chunkCount)
                    return;

                if (!state->failed.load(std::memory_order_relaxed))
                {
                    const std::size_t begin = c * chunk;
                    const std::size_t end   = std::min(count, begin + chunk);

                    try
                    {
                        for (std::size_t i = begin; i < end; ++i)
                            (*fnPtr)(static_cast<Index>(first + static_cast<Index>(i)));
                    } catch (...)
                    {
                        std::lock_guard<std::mutex> guard(state->errorMutex);
                        if (!state->error)
                            state->error = std::current_exception();
                        state->failed.store(true, std::memory_order_relaxed);
                    }
                }

                if (state->done.fetch_add(1, std::memory_order_acq_rel) + 1 == chunkCount)
                    state->done.notify_all();
            }
        };

        const std::size_t helpers = std::min<std::size_t>(GetThreadCount(), chunkCount - 1);
        for (std::size_t h = 0; h < helpers; ++h)
            Dispatch(drain);

        drain(); // the caller works too

        for (std::size_t d = state->done.load(std::memory_order_acquire); d != chunkCount;
             d             = state->done.load(std::memory_order_acquire))
            state->done.wait(d);

        if (state->error)
            std::rethrow_exception(state->error);
    }

    template<std::ranges::random_access_range R, typename Fn>
    void ThreadPool::ParallelForEach(R &&range, Fn &&fn, std::size_t grain)
    {
        auto it      = std::ranges::begin(range);
        const auto n = static_cast<std::size_t>(std::ranges::distance(range));
        ParallelFor<std::size_t>(0, n, [&](std::size_t i) { fn(it[i]); }, grain);
    }
} // namespace SF::Engine
