#include "TcpClient.h"
#include "network/base/SocketOpt.h"

using namespace tmms::network;

tmms::network::TcpClient::TcpClient(EventLoop *loop, const InetAddress &server)
    : TcpConnection(loop, -1, InetAddress(), server), server_addr_(server)
{
}

tmms::network::TcpClient::~TcpClient()
{
    OnClose();
}

void tmms::network::TcpClient::Connect()
{
    loop_->RunInLoop([this]()
                     { ConnectInLoop(); });
}

void tmms::network::TcpClient::ConnectInLoop()
{
    loop_->AsserInLoopThread();
    if (status_ != kTcpConStatusInit && status_ != kTcpConStatusDisConnected)
    {
        return;
    }
    closed_ = false;
    fd_ = SocketOpt::CreateNonblockingTcpSocket(AF_INET);
    if (fd_ < 0)
    {
        OnClose();
        return;
    }
    status_ = kTcpConStatusConnecting;
    loop_->AddEvent(std::dynamic_pointer_cast<TcpClient>(shared_from_this()));
    EnableWriting(true);

    SocketOpt opt(fd_);
    auto ret = opt.Connect(server_addr_);
    if (ret == 0)
    {
        UpdateConnectionStatus();
        return;
    }
    else if (ret == -1)
    {
        if (errno != EINPROGRESS)
        {
            NETWORK_ERROR << "connect to server:" << server_addr_.ToIpPort() << "error:" << errno;
            OnClose();
            return;
        }
    }
}

void tmms::network::TcpClient::SetConnectCallback(const ConnectionCallback &cb)
{
    connected_cb_ = cb;
}

void tmms::network::TcpClient::SetConnectCallback(ConnectionCallback &&cb)
{
    connected_cb_ = std::move(cb);
}

void tmms::network::TcpClient::UpdateConnectionStatus()
{
    status_ = kTcpConStatusConnected;
    EnableWriting(false);
    if (connected_cb_)
    {
        connected_cb_(std::dynamic_pointer_cast<TcpClient>(shared_from_this()), true);
    }
}

void tmms::network::TcpClient::OnRead()
{
    if (status_ == kTcpConStatusConnecting)
    {
        if (CheckError())
        {
            NETWORK_ERROR << "connection to server:" << server_addr_.ToIpPort() << "error:" << errno;
            OnClose();
            return;
        }
        UpdateConnectionStatus();
        return;
    }
    else if (status_ == kTcpConStatusConnected)
    {
        TcpConnection::OnRead();
    }
}

void tmms::network::TcpClient::OnWrite()
{
    if (status_ == kTcpConStatusConnecting)
    {
        if (CheckError())
        {
            NETWORK_ERROR << "connection to server:" << server_addr_.ToIpPort() << "error:" << errno;
            OnClose();
            return;
        }
        UpdateConnectionStatus();
        return;
    }
    else if (status_ == kTcpConStatusConnected)
    {
        TcpConnection::OnWrite();
    }
}

void tmms::network::TcpClient::OnClose()
{
    if (status_ == kTcpConStatusConnecting || status_ == kTcpConStatusConnected)
    {
        connected_cb_(std::dynamic_pointer_cast<TcpClient>(shared_from_this()), false);
    }
    status_ = kTcpConStatusDisConnected;
    TcpConnection::OnClose();
}

void tmms::network::TcpClient::Send(std::list<BufferNodePtr> &list)
{
    if (status_ == kTcpConStatusConnected)
    {
        TcpConnection::Send(list);
    }
}

void tmms::network::TcpClient::Send(const char *buf, size_t size)
{
    if (status_ == kTcpConStatusConnected)
    {
        TcpConnection::Send(buf, size);
    }
}

bool tmms::network::TcpClient::CheckError()
{
    int error = 0;
    socklen_t len = sizeof(error);
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
    {
        return true;
    }
    return error != 0;
}
