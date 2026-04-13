#pragma once
#include "base/NonCopyable.h"
#include <vector>
#include "EventLoop.h"
#include "EventLoopThread.h"
#include <memory>
#include <atomic>
namespace tmms
{
    namespace network{
        using EventLoopThreadPtr=std::shared_ptr<EventLoopThread>;
        class EventLoopThreadPool:public base::NonCopyable
        {
        public:
            EventLoopThreadPool(int thread_num,int start=0,int cpus=4);
            ~EventLoopThreadPool();

            std::vector<EventLoop *>GetLoops() const;
            EventLoop *GetNextLoop();
            size_t Size();
            void Start();

            private:
            std::vector<EventLoopThreadPtr> threads_;
            std::atomic_int_fast32_t loop_index_{0};
        };
        
    }
    
} // namespace tmms
