#include "TimingWheel.h"
#include "network/base/Network.h"
using namespace tmms::network;

tmms::network::TimingWheel::TimingWheel()
    : wheels_(4)
{
    wheels_[kTimingTypeSecond].resize(60);
    wheels_[kTimingTypeMinute].resize(60);
    wheels_[kTimingTypeHour].resize(24);
    wheels_[kTimingTypeDay].resize(30);
}

tmms::network::TimingWheel::~TimingWheel()
{
}

void tmms::network::TimingWheel::InsertEntry(uint32_t delay, EntryPtr enterptr)
{
    if (delay <= 0)
    {
        enterptr.reset();
    }
    else if (delay < kTimingMinute)
    {
        InsertSecondEntry(delay, enterptr);
    }
    else if (delay < kTimingHour)
    {
        InsertMinuteEntry(delay, enterptr);
    }
    else if (delay < kTimingDay)
    {
        InsertHourEntry(delay, enterptr);
    }
    else
    {
        auto day = delay / kTimingDay;
        if (day > 30)
        {
            NETWORK_ERROR << "It is not suport > 30 days!!!";
            return;
        }
        InsertDayEntry(delay, enterptr);
    }
}

void tmms::network::TimingWheel::OnTimer(int64_t now)
{
    if (last_ts_ == 0)
    {
        last_ts_ = now;
    }
    if (now - last_ts_ < 1000)
    {
        return;
    }
    last_ts_ = now;
    ++tick_;
    PopUp(wheels_[kTimingTypeSecond]);
    if (tick_ % kTimingMinute == 0)
    {
        PopUp(wheels_[kTimingTypeMinute]);
        }
    if (tick_ % kTimingHour == 0)
    {
        PopUp(wheels_[kTimingTypeHour]);
    }
    if (tick_ % kTimingDay == 0)
    {
        PopUp(wheels_[kTimingTypeDay]);
    }
}

void tmms::network::TimingWheel::PopUp(Wheel &bq)
{
    WheelEntry tmp;
    bq.front().swap(tmp);
    bq.pop_front();
    bq.push_back(WheelEntry());
}

void tmms::network::TimingWheel::RunAfter(double delay, const Func &cb)
{
    CallbackEntryPtr cbEntry = std::make_shared<CallbackEntry>([cb]()
                                                               { cb(); });
    InsertEntry(delay, cbEntry);
}

void tmms::network::TimingWheel::RunAfter(double delay, Func &&cb)
{
    CallbackEntryPtr cbEntry = std::make_shared<CallbackEntry>([cb = std::move(cb)]()
                                                               { cb(); });
    InsertEntry(delay, cbEntry);
}

void tmms::network::TimingWheel::RunEvery(double interval, const Func &cb)
{
    CallbackEntryPtr cbEntry = std::make_shared<CallbackEntry>([this, interval, cb]()
                                                               {
        cb();
        RunEvery(interval,cb); });
    InsertEntry(interval, cbEntry);
}

void tmms::network::TimingWheel::RunEvery(double interval, Func &&cb)
{
    CallbackEntryPtr cbEntry = std::make_shared<CallbackEntry>([this, interval, cb = move(cb)]()
                                                               {
        cb();
        RunEvery(interval,std::move(cb)); });
    InsertEntry(interval, cbEntry);
}

void tmms::network::TimingWheel::InsertSecondEntry(uint32_t delay, EntryPtr enterpty)
{
    wheels_[kTimingTypeSecond][delay - 1].emplace(enterpty);
}

void tmms::network::TimingWheel::InsertMinuteEntry(uint32_t delay, EntryPtr enterpty)
{
    auto minute = delay / kTimingMinute;
    auto second = delay % kTimingMinute;
    CallbackEntryPtr newEntryPtr = std::make_shared<CallbackEntry>([this, second, enterpty]()
                                                                   { InsertEntry(second, enterpty); });
    wheels_[kTimingTypeMinute][minute - 1].emplace(newEntryPtr);
}

void tmms::network::TimingWheel::InsertHourEntry(uint32_t delay, EntryPtr enterpty)
{
    auto hour = delay / kTimingHour;
    auto second = delay % kTimingHour;

    CallbackEntryPtr newEntryPtr = std::make_shared<CallbackEntry>([this, second, enterpty]()
                                                                   { InsertEntry(second, enterpty); });
    wheels_[kTimingTypeHour][hour - 1].emplace(newEntryPtr);
}

void tmms::network::TimingWheel::InsertDayEntry(uint32_t delay, EntryPtr enterpty)
{
    auto day = delay / kTimingDay;
    auto second = delay % kTimingDay;

    CallbackEntryPtr newEntryPtr = std::make_shared<CallbackEntry>([this, second, enterpty]()
                                                                   { InsertEntry(second, enterpty); });
    wheels_[kTimingTypeDay][day - 1].emplace(newEntryPtr);
}
