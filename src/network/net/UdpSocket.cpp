#include "UdpSocket.h"
#include "network/base/Network.h"
#include <iostream>
tmms::network::UdpSocket::UdpSocket(EventLoop *loop, int socketfd, const InetAddress &localAddr, const InetAddress &peerAddr)
    : Connection(loop, socketfd, localAddr, peerAddr), message_buffer_(message_buffer_Size_)
{
}

tmms::network::UdpSocket::~UdpSocket()
{
    if (!closed_)
    {
        ForceClose();
    }
}

void tmms::network::UdpSocket::OnTimeout()
{
}

void tmms::network::UdpSocket::OnError(const std::string &msg)
{
}

void tmms::network::UdpSocket::OnRead()
{
    if (closed_)
    {
        NETWORK_TRACE << "host:" << peer_addr_.ToIpPort() << " had closed.";
        return;
    }
    ExtenLife();
    while (true)
    {
        struct sockaddr_storage ss;
        socklen_t len = sizeof(ss);
        message_buffer_.ensureWritableBytes(message_buffer_Size_);
        auto ret = ::recvfrom(fd_, message_buffer_.beginWrite(),
                              message_buffer_Size_, 0, (struct sockaddr *)&ss, &len);
        if (ret > 0)
        {
            message_buffer_.hasWritten(ret);
            InetAddress peeraddr;

            if (ss.ss_family == AF_INET)
            {
                struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
                char ip[INET_ADDRSTRLEN] = {0};
                ::inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
                peeraddr.SetAddr(ip);
                peeraddr.SetPort(ntohs(sin->sin_port));
            }
            else if (ss.ss_family == AF_INET6)
            {
                struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
                char ip[INET6_ADDRSTRLEN] = {0};
                ::inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
                peeraddr.SetAddr(ip);
                peeraddr.SetPort(ntohs(sin6->sin6_port));
                peeraddr.SetIsIPV6(true);
            }
            else
            {
                message_buffer_.retrieveAll();
                continue;
            }

            if (message_cb_)
            {
                message_cb_(peeraddr, message_buffer_);
                if (closed_)
                    return;
            }
            message_buffer_.retrieveAll();
        }
        else if (ret == 0)
        {
            continue;
        }
        else
        {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                NETWORK_ERROR << "host:" << peer_addr_.ToIpPort()
                              << " recvfrom err:" << errno;
                OnClose();
                return;
            }
            break;
        }
    }
}

void tmms::network::UdpSocket::OnWrite()
{
    if (closed_)
    {
        NETWORK_TRACE << "host:" << peer_addr_.ToIpPort() << "had closed.";
        return;
    }
    ExtenLife();
    while (true)
    {
        if (!buffer_list_.empty())
        {
            auto buf = buffer_list_.front();
            auto ret = ::sendto(fd_, buf->addr, buf->size, 0, buf->dest_addr, buf->sock_len);

            if (ret > 0)
            {

                buffer_list_.pop_front();
            }
            else if (ret < 0)
            {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    NETWORK_ERROR << "host:" << peer_addr_.ToIpPort()
                                  << "write err:" << errno;
                    OnClose();
                }
                break;
            }
        }
        if (buffer_list_.empty())
        {
            if (write_complete_cb_)
            {
                write_complete_cb_(std::dynamic_pointer_cast<UdpSocket>(shared_from_this()));
            }
            EnableWriting(false);
            break;
        }
    }
}

void tmms::network::UdpSocket::OnClose()
{
    if (closed_)
        return;
    else
    {
        closed_ = true;
        if (close_cb_)
        {
            close_cb_(std::dynamic_pointer_cast<UdpSocket>(shared_from_this()));
        }
        loop_->DelEvent(std::dynamic_pointer_cast<UdpSocket>(shared_from_this()));
        Event::Close();
    }
}

void tmms::network::UdpSocket::SetCloseCallback(const UdpSocketCloseCallback &cb)
{
    close_cb_ = cb;
}
void tmms::network::UdpSocket::SetCloseCallback(UdpSocketCloseCallback &&cb)
{
    close_cb_ = std::move(cb);
}
void tmms::network::UdpSocket::SetRecvMsgCallback(const UdpSocketMessageCallback &cb)
{
    message_cb_ = cb;
}
void tmms::network::UdpSocket::SetRecvMsgCallback(UdpSocketMessageCallback &&cb)
{
    message_cb_ = std::move(cb);
}
void tmms::network::UdpSocket::SetWriteCompleteCallback(const UdpSocketWriteCompleteCallback &cb)
{
    write_complete_cb_ = cb;
}
void tmms::network::UdpSocket::SetWriteCompleteCallback(UdpSocketWriteCompleteCallback &&cb)
{
    write_complete_cb_ = std::move(cb);
}

void tmms::network::UdpSocket::SetTimeoutCallback(int timeout, UdpSocketTimeoutCallback cb)
{
    auto cp = std::dynamic_pointer_cast<UdpSocket>(shared_from_this());
    loop_->RunAfter(timeout, [cb = std::move(cb), cp = std::move(cp)]()
                    { cb(cp); });
}

void tmms::network::UdpSocket::EnableCheckIdleTimeout(int32_t max_time)
{
    auto tp = std::make_shared<UdpTimeoutEntry>(std::dynamic_pointer_cast<UdpSocket>(shared_from_this()));
    max_idle_time_ = max_time;
    timeout_entry_ = tp;
    loop_->InsertEntry(max_time, tp);
}

void tmms::network::UdpSocket::Send(std::list<UdpBufferNodePtr> list)
{
    loop_->RunInLoop([this, list = std::move(list)]()
                     { SendInLoop(std::move(list)); });
}

void tmms::network::UdpSocket::Send(const char *buf, size_t size, sockaddr *addr, socklen_t len)
{
    if (!buf || size == 0)
        return;
    void *data_copy = std::malloc(size);
    if (!data_copy)
        return;
    std::memcpy(data_copy, buf, size);
    auto node = std::make_shared<UdpBufferNode>(data_copy, size, addr, len);

    Send({node});
}

void tmms::network::UdpSocket::ForceClose()
{
    auto self = shared_from_this();
    loop_->RunInLoop([self]()
                     { self->OnClose(); });
}

void tmms::network::UdpSocket::ExtenLife()
{
    auto tp = timeout_entry_.lock();
    if (tp)
    {
        loop_->InsertEntry(max_idle_time_, tp);
    }
}

void tmms::network::UdpSocket::SendInLoop(std::list<UdpBufferNodePtr> list)
{
    for (auto &i : list)
    {
        buffer_list_.emplace_back(i);
    }
    if (!buffer_list_.empty())
    {
        EnableWriting(true);
    }
}
