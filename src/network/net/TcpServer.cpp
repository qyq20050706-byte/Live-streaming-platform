#include "TcpServer.h"
#include "network/base/Network.h"
using namespace tmms::network;

tmms::network::TcpServer::TcpServer(EventLoop *loop, const InetAddress &addr)
    : loop_(loop), addr_(addr)
{
    acceptor_=std::make_shared<Acceptor>(loop_,addr);
}

tmms::network::TcpServer::~TcpServer()
{
}

void tmms::network::TcpServer::SetNewConnectionCallback(const NewConnectionCallback &cb)
{
    new_connection_cb_ = cb;
}

void tmms::network::TcpServer::SetNewConnectionCallback(NewConnectionCallback &&cb)
{
    new_connection_cb_ = std::move(cb);
}

void tmms::network::TcpServer::SetDestoryConnectionCallback(const DestoryConnectionCallback &cb)
{
    destory_connection_cb_ = cb;
}

void tmms::network::TcpServer::SetDestoryConnectionCallback(DestoryConnectionCallback &&cb)
{
    destory_connection_cb_ = std::move(cb);
}

void tmms::network::TcpServer::SetActiveCallback(const ActiveCallback &cb)
{
    active_cb_ = cb;
}

void tmms::network::TcpServer::SetActiveCallback(ActiveCallback &&cb)
{
    active_cb_ = std::move(cb);
}

void tmms::network::TcpServer::SetWriteCompleteCallback(const WriteCompleteCallback &cb)
{
    write_complete_cb_ = cb;
}

void tmms::network::TcpServer::SetWriteCompleteCallback(WriteCompleteCallback &&cb)
{
    write_complete_cb_ = std::move(cb);
}

void tmms::network::TcpServer::SetMessageCallback(const MessageCallback &cb)
{
    message_cb_ = cb;
}

void tmms::network::TcpServer::SetMessageCallback(MessageCallback &&cb)
{
    message_cb_ = std::move(cb);
}

void tmms::network::TcpServer::OnAccept(int fd, const InetAddress &addr)
{
    NETWORK_TRACE << "new connection fd:" << fd << "host:" << addr.ToIpPort();
    TcpConnectionPtr con = std::make_shared<TcpConnection>(loop_, fd, addr_, addr);
    con->SetCloseCallback(std::bind(&TcpServer::OnConnectionClose, this, std::placeholders::_1));
    if (write_complete_cb_)
    {
        con->SetWriteCompleteCallback(write_complete_cb_);
    }
    if (active_cb_)
    {
        con->SetActiveCallback(active_cb_);
    }
    con->SetRecvMsgCallback(message_cb_);
    connections_.insert(con);
    loop_->AddEvent(con);
    con->EnableCheckIdleTimeout(30);
    if (new_connection_cb_)
    {
        new_connection_cb_(con);
    }
}

void tmms::network::TcpServer::OnConnectionClose(const TcpConnectionPtr &con)
{
    NETWORK_TRACE << "host:" << con->PeerAddr().ToIpPort() << "closed.";
    loop_->AsserInLoopThread();
    connections_.erase(con);
    loop_->DelEvent(con);
    if (destory_connection_cb_)
    {
        destory_connection_cb_(con);
    }
}

void tmms::network::TcpServer::Start()
{
    acceptor_->SetAcceptCallback(std::bind(&TcpServer::OnAccept, this, std::placeholders::_1, std::placeholders::_2));
    acceptor_->Start();
}

void tmms::network::TcpServer::Stop()
{
    acceptor_->Stop();
}
