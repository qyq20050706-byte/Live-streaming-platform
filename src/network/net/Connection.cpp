#include "Connection.h"

using namespace tmms::network;
tmms::network::Connection::Connection(EventLoop *loop, int fd, const InetAddress &localAddr, const InetAddress &peerAddr)
:Event(loop,fd),local_addr_(localAddr),peer_addr_(peerAddr)
{
}

void tmms::network::Connection::SetLocalAddr(const InetAddress &local)
{
    local_addr_=local;
}

void tmms::network::Connection::SetLocalAddr(InetAddress &&local)
{
    local_addr_=std::move(local);
}

void tmms::network::Connection::SetPeerAddr(const InetAddress &peer)
{
    peer_addr_=peer;
}

void tmms::network::Connection::SetPeerAddr(InetAddress &&peer)
{
    peer_addr_=std::move(peer);
}

const InetAddress &tmms::network::Connection::LocalAddr() const
{
    return local_addr_;
}

const InetAddress &tmms::network::Connection::PeerAddr() const
{
    return peer_addr_;
}

void tmms::network::Connection::SetContext(int type, const std::shared_ptr<void> &context)
{
    contexts_[type]=context;
}

void tmms::network::Connection::SetContext(int type, std::shared_ptr<void> &&context)
{
    contexts_[type]=std::move(context);
}

void tmms::network::Connection::ClearContext(int type)
{
    contexts_.erase(type);
}

void tmms::network::Connection::ClearContext()
{
    contexts_.clear();
}

void tmms::network::Connection::SetActiveCallback(const ActiveCallback &cb)
{
    active_cb_=cb;
}

void tmms::network::Connection::SetActiveCallback(ActiveCallback &&cb)
{
    active_cb_=std::move(cb);

}

void tmms::network::Connection::Active()
{
    bool expected = false;
    if (active_.compare_exchange_strong(expected, true)) {
        auto self = std::dynamic_pointer_cast<Connection>(shared_from_this());
        loop_->RunInLoop([self]() {
            if (self->active_cb_) self->active_cb_(self);
        });
    }

}

void tmms::network::Connection::Deactive()
{
    active_.store(false);
}
