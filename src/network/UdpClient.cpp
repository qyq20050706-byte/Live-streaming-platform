#include "UdpClient.h"
#include "network/base/Network.h"
#include "network/base/SocketOpt.h"

using namespace tmms::network;
tmms::network::UdpClient::UdpClient(EventLoop *loop, const InetAddress &server)
    : UdpSocket(loop, -1, InetAddress(), server), server_addr_(server)
{
}

tmms::network::UdpClient::~UdpClient()
{
    OnClose();
}

void tmms::network::UdpClient::Connect()
{
    loop_->RunInLoop([this]()
                     { ConnectInLoop(); });
}

void tmms::network::UdpClient::SetConnectedCallback(const ConnectedCallback &cb)
{
    connected_cb_ = cb;
}

void tmms::network::UdpClient::SetConnectedCallback(ConnectedCallback &&cb)
{
    connected_cb_ = std::move(cb);
}

void tmms::network::UdpClient::ConnectInLoop()
{
    loop_->AsserInLoopThread();
    fd_ = SocketOpt::CreateNonblockingUdpSocket(AF_INET);
    if (fd_ < 0)
    {
        OnClose();
        return;
    }
    connected_ = true;
    loop_->AddEvent(std::dynamic_pointer_cast<UdpClient>(shared_from_this()));
    SocketOpt opt(fd_);
    opt.Connect(server_addr_);
    server_addr_.GetSockAddr((struct sockaddr *)&sock_addr_);
    if (connected_cb_)
    {
        connected_cb_(std::dynamic_pointer_cast<UdpSocket>(shared_from_this()), true);
    }
}

void tmms::network::UdpClient::Send(std::list<BufferNodePtr> &list)
{
    std::list<UdpBufferNodePtr> udp_list;
    for (auto &node : list)
    {
        void *data_copy = std::malloc(node->size);
        if (!data_copy)
        {
            NETWORK_ERROR << "malloc failed in UdpClient::Send";
            continue;
        }
        std::memcpy(data_copy, node->addr, node->size);

        auto udp_node = std::make_shared<UdpBufferNode>(
            data_copy, 
            node->size, 
            (struct sockaddr *)&sock_addr_, 
            sock_len_
        );
        udp_list.emplace_back(std::move(udp_node));
    }

    if (!udp_list.empty())
    {
        UdpSocket::Send(udp_list);
    }
}

void tmms::network::UdpClient::Send(const char *buf, size_t size)
{
    UdpSocket::Send(buf,size,(struct sockaddr *)&sock_addr_,sock_len_);
}

void tmms::network::UdpClient::OnClose()
{
    if(connected_){
        connected_=false;
    }
    UdpSocket::OnClose();

}
