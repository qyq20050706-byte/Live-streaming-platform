#include "UdpServer.h"

using namespace tmms::network;
tmms::network::UdpServer::UdpServer(EventLoop *loop, const InetAddress &server)
    : UdpSocket(loop, -1, server, InetAddress()), server_(server)
{
}

tmms::network::UdpServer::~UdpServer()
{
    Stop();
}

void tmms::network::UdpServer::Start()
{
    if (fd_ != -1)
        return;
    loop_->RunInLoop([this]()
                     { Open(); });
}

void tmms::network::UdpServer::Stop()
{
    loop_->RunInLoop([this]()
                     { OnClose(); });
}

void tmms::network::UdpServer::Open()
{
    loop_->AsserInLoopThread();
    closed_ = false;
    fd_ = SocketOpt::CreateNonblockingUdpSocket(AF_INET);
    if (fd_ < 0)
    {
        NETWORK_ERROR << "create socket failed, errno=" << errno;
        OnClose();
        return;
    }
    SocketOpt opt(fd_);
    opt.SetReuseAddr(true);
    if (opt.BindAddress(server_) < 0)
    {
        NETWORK_ERROR << "bind failed: " << server_.ToIpPort();
        OnClose();
        return;
    }
    loop_->AddEvent(std::dynamic_pointer_cast<UdpServer>(shared_from_this()));
    NETWORK_TRACE << "UdpServer started on " << server_.ToIpPort();
}
