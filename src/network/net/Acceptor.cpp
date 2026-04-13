#include "Acceptor.h"
#include "network/base/Network.h"
using namespace tmms::network;

tmms::network::Acceptor::Acceptor(EventLoop *loop, const InetAddress &addr)
    : Event(loop), addr_(addr)
{
}

tmms::network::Acceptor::~Acceptor()
{
    Stop();
    if (socket_opt_)
    {
        delete socket_opt_;
        socket_opt_ = nullptr;
    }
    if (fd_)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

void tmms::network::Acceptor::SetAcceptCallback(const AcceptCallback &cb)
{
    accept_cb_ = cb;
}

void tmms::network::Acceptor::SetAcceptCallback(AcceptCallback &&cb)
{
    accept_cb_ = std::move(cb);
}

void tmms::network::Acceptor::Start()
{
    loop_->RunInLoop([this]()
                     { Open(); });
}

void tmms::network::Acceptor::Stop()
{
    loop_->DelEvent(std::dynamic_pointer_cast<Event>(shared_from_this()));
}

void tmms::network::Acceptor::OnRead()
{
    if (!socket_opt_)
    {
        return;
    }
    while (true)
    {
        InetAddress addr;
        auto sock = socket_opt_->Accept(&addr);
        if (sock > 0)
        {
            if (accept_cb_)
            {
                accept_cb_(sock, addr);
            }
        }
        else
        {
            if (errno != EINTR && errno != EAGAIN)
            {
                NETWORK_ERROR << "acceptor error.errno:" << errno;
                OnClose();
            }
            break;
        }
    }
}

void tmms::network::Acceptor::OnClose()
{
    Stop();
    Open();
}

void tmms::network::Acceptor::OnError(const std::string &msg)
{
    NETWORK_ERROR << "acceptor error:" << msg;
    OnClose();
}

void tmms::network::Acceptor::Open()
{
    if (fd_ > 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
    if (addr_.IsIpv6())
    {
        fd_ = SocketOpt::CreateNonblockingTcpSocket(AF_INET6);
    }
    else
    {
        fd_ = SocketOpt::CreateNonblockingTcpSocket(AF_INET);
    }
    if (fd_ < 0)
    {
        NETWORK_ERROR << "socket failed.erron:" << errno;
        exit(-1);
    }
    if (socket_opt_)
    {
        delete socket_opt_;
        socket_opt_ = nullptr;
    }
    loop_->AddEvent(std::dynamic_pointer_cast<Event>(shared_from_this()));
    socket_opt_ = new SocketOpt(fd_);
    socket_opt_->SetReuseAddr(true);
    socket_opt_->SetReusePort(true);
    socket_opt_->BindAddress(addr_);
    socket_opt_->Listen();
}
