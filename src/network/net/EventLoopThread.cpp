#include "EventLoopThread.h"

using namespace tmms::network;

tmms::network::EventLoopThread::EventLoopThread()
:thread_([this](){StartEventLoop();})
{
}

tmms::network::EventLoopThread::~EventLoopThread()
{
    Run();
    if(loop_){
        loop_->Quit();
    }
    if(thread_.joinable()){
        thread_.join();
    }
}

void tmms::network::EventLoopThread::Run()
{
    std::call_once(once_,[this]()
    {
        {
        std::lock_guard<std::mutex> lk(lock_);
        running_=true;
        condition_.notify_all();
        }
        auto f=promise_loop_.get_future();
        f.get();
    });
}

EventLoop *tmms::network::EventLoopThread::Loop() const
{
    return loop_;
}

std::thread &tmms::network::EventLoopThread::Thread()
{
    return thread_;
}

void tmms::network::EventLoopThread::StartEventLoop()
{
    EventLoop loop;

    std::unique_lock<std::mutex> lk(lock_);
    condition_.wait(lk,[this](){return running_;});
    loop_=&loop;
    promise_loop_.set_value(1);
    loop.Loop();
    loop_=nullptr;
}
