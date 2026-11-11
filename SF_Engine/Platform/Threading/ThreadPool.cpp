#include "ThreadPool.hpp"
namespace SF::Engine
{

    ThreadPool::ThreadPool(uint32_t threadCount)
    {
        // A pool with zero workers would accept jobs that can never execute.
        if (threadCount == 0)
            threadCount = 1;

        workers.reserve(threadCount);

        for (uint32_t i = 0; i < threadCount; ++i)
        {
            workers.emplace_back(
                    [this]
                    {
                        while (true)
                        {
                            std::function<void()> task;

                            {
                                std::unique_lock<std::mutex> lock(queueMutex);

                                condition.wait(lock, [this] { return stop || !tasks.empty(); });

                                // Finish processing queued work before exiting.
                                if (stop && tasks.empty())
                                    return;

                                task = std::move(tasks.front());
                                tasks.pop();

                                ++activeTasks;
                            }

                            // Do not hold queueMutex while executing user code.
                            task();

                            {
                                std::unique_lock<std::mutex> lock(queueMutex);

                                --activeTasks;

                                if (tasks.empty() && activeTasks == 0)
                                    completionCondition.notify_all();
                            }
                        }
                    });
        }
    }

    ThreadPool::~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }

        // Wake every worker so they can finish their remaining tasks.
        condition.notify_all();

        for (auto &worker: workers)
        {
            if (worker.joinable())
                worker.join();
        }
    }

    void ThreadPool::Wait()
    {
        std::unique_lock lock(queueMutex);

        completionCondition.wait(lock, [this] { return tasks.empty() && activeTasks == 0; });
    }
} // namespace SF::Engine
