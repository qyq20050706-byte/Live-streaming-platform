#include "TcpServer.h"
#include "network/base/Network.h"
using namespace tmms::network;

tmms::network::TcpServer::TcpServer(EventLoop *loop, const InetAddress &addr)
    : loop_(loop), addr_(addr)
{
    acceptor_ = std::make_shared<Acceptor>(loop_, addr);
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

void tmms::network::TcpServer::SetThreadNum(int num)
{
    io_thread_num_ = num;
}

void tmms::network::TcpServer::OnAccept(int fd, const InetAddress &addr)
{
    NETWORK_TRACE << "new connection fd:" << fd << " host:" << addr.ToIpPort();

    // 选择一个 IO loop
    EventLoop *io_loop = loop_;
    if (io_thread_pool_)
    {
        io_loop = io_thread_pool_->GetNextLoop();
    }

    TcpConnectionPtr con = std::make_shared<TcpConnection>(io_loop, fd, addr_, addr);
    con->SetCloseCallback(
        std::bind(&TcpServer::OnConnectionClose, this, std::placeholders::_1));

    if (write_complete_cb_)
    {
        con->SetWriteCompleteCallback(write_complete_cb_);
    }

    if (active_cb_)
    {
        con->SetActiveCallback(active_cb_);
    }

    con->SetRecvMsgCallback(message_cb_);

    // 主线程里先记录连接生命周期，避免 connection 提前析构
    connections_.insert(con);

    // 关键改动：
    // 在连接所属的 io_loop 线程里，先执行 new_connection_cb_
    // 让业务层完成上下文初始化，再 AddEvent 开始收数据
    io_loop->RunInLoop([this, io_loop, con]()
                       {
        if (new_connection_cb_)
        {
            new_connection_cb_(con);
        }

        // 如果业务层在 new_connection_cb_ 里拒绝了连接，就不要再加到 epoll
        if (!con->IsConnected())
        {
            return;
        }

        io_loop->AddEvent(con); });
}

void tmms::network::TcpServer::OnConnectionClose(const TcpConnectionPtr &con)
{
    NETWORK_TRACE << "host:" << con->PeerAddr().ToIpPort() << " closed.";

    // 关键：OnConnectionClose 是在 IO 线程里调用的（由 TcpConnection::OnClose 触发）
    // 但 connections_ 只在主线程维护
    // 所以需要切回主线程来 erase
    loop_->RunInLoop([this, con]()
                     {
        connections_.erase(con);
        if (destory_connection_cb_)
        {
            destory_connection_cb_(con);
        } });
}

void tmms::network::TcpServer::Start()
{
    // 创建 IO 线程池（如果配了 io_thread_num > 0）
    if (io_thread_num_ > 0)
    {
        io_thread_pool_ = std::make_shared<EventLoopThreadPool>(io_thread_num_, 0, 0);
        io_thread_pool_->Start();
        NETWORK_INFO << "TcpServer IO thread pool started with "
                     << io_thread_num_ << " threads.";
    }

    acceptor_->SetAcceptCallback(
        std::bind(&TcpServer::OnAccept, this, std::placeholders::_1, std::placeholders::_2));
    acceptor_->Start();
}

void tmms::network::TcpServer::Stop()
{
    acceptor_->Stop();
}