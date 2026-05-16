#pragma once

#include "NonCopyable.h"
#include "LogStream.h"
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace tmms
{
    namespace base
    {

        class ThreadPool : public NonCopyable
        {
        public:
            ThreadPool() = default;
            ~ThreadPool();

            void Start(int thread_num);

            void AddTask(std::function<void()> task);

            void Stop();

            size_t Size() const { return workers_.size(); }

            size_t PendingTasks();

        private:
            void WorkerFunc();

            std::vector<std::thread> workers_;
            std::queue<std::function<void()>> tasks_;
            std::mutex mutex_;
            std::condition_variable cv_;
            bool stop_{false};
            bool started_{false};
        };

    } // namespace base
} // namespace tmms