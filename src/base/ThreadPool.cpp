#include "ThreadPool.h"

using namespace tmms::base;

ThreadPool::~ThreadPool()
{
    Stop();
}

void ThreadPool::Start(int thread_num)
{
    if (started_)
        return;
    started_ = true;
    stop_ = false;

    if (thread_num <= 0)
        thread_num = 1;

    for (int i = 0; i < thread_num; ++i)
    {
        workers_.emplace_back(&ThreadPool::WorkerFunc, this);
    }

    LOG_INFO << "ThreadPool started with " << thread_num << " threads.";
}

void ThreadPool::Stop()
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!started_ || stop_)
            return;
        stop_ = true;
    }
    cv_.notify_all();

    for (auto &t : workers_)
    {
        if (t.joinable())
            t.join();
    }
    workers_.clear();

    LOG_INFO << "ThreadPool stopped.";
}

void ThreadPool::AddTask(std::function<void()> task)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stop_)
        {
            LOG_WARN << "ThreadPool::AddTask on stopped pool, task dropped.";
            return;
        }
        tasks_.emplace(std::move(task));
    }
    cv_.notify_one();
}

size_t ThreadPool::PendingTasks()
{
    std::unique_lock<std::mutex> lock(mutex_);
    return tasks_.size();
}

void ThreadPool::WorkerFunc()
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]
                     { return stop_ || !tasks_.empty(); });

            if (stop_ && tasks_.empty())
                return;

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        try
        {
            task();
        }
        catch (const std::exception &e)
        {
            LOG_ERROR << "ThreadPool: task exception: " << e.what();
        }
        catch (...)
        {
            LOG_ERROR << "ThreadPool: task threw unknown exception.";
        }
    }
}