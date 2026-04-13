#pragma once
#include <vector>
#include <sys/epoll.h>
#include "Event.h"
#include <memory>
#include <unordered_map>
#include <queue>
#include <mutex>
#include "PipeEvent.h"
#include <functional>
#include "TimingWheel.h"

namespace tmms
{
    namespace network
    {
        class Event;
        using EventPtr = std::shared_ptr<Event>;
        using Func=std::function<void()>;
        class EventLoop
        {
        public:
            EventLoop();
            ~EventLoop();

            void Loop();
            void Quit();
            void AddEvent(const EventPtr &event);
            void DelEvent(const EventPtr &event);
            bool EnableEventWriting(const EventPtr &event, bool enable);
            bool EnableEventReading(const EventPtr &event, bool enable);
            void AsserInLoopThread();
            bool IsInLoopThread() const;
            void RunInLoop(const Func &f);
            void RunInLoop(Func &&f);

            void InsertEntry(uint32_t delay, EntryPtr enterptr);
            void RunAfter(double delay, const Func &cb);
            void RunAfter(double delay, Func &&cb);
            void RunEvery(double interval, const Func &cb);
            void RunEvery(double interval, Func &&cb);

        private:
            void RunFunctions();
            void WakeUp();
            bool looping_{false};
            int epoll_fd_{-1};
            std::vector<struct epoll_event> epoll_events_;
            std::unordered_map<int, EventPtr> events_;
            std::queue<Func> functions_;
            std::mutex lock_;
            PipeEventPtr pipe_event_;
            TimingWheel wheel_;
        };
    }
}