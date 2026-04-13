#include "TcpConnection.h"
#include "network/base/Network.h"
#include <unistd.h>
#include <cstdlib>
#include <cstring>

using namespace tmms::network;

tmms::network::TcpConnection::TcpConnection(EventLoop *loop, int socketfd, const InetAddress &localAddr, const InetAddress &peerAddr)
    : Connection(loop, socketfd, localAddr, peerAddr)
{
}

tmms::network::TcpConnection::~TcpConnection()
{
}

void tmms::network::TcpConnection::SetCloseCallback(const CloseConnectionCallback &cb)
{
    close_cb_ = cb;
}

void tmms::network::TcpConnection::SetCloseCallback(CloseConnectionCallback &&cb)
{
    close_cb_ = std::move(cb);
}

void tmms::network::TcpConnection::OnClose()
{
    loop_->AsserInLoopThread();
    if (closed_)
    {
        return;
    }

    closed_ = true;
    loop_->DelEvent(shared_from_this());
    Event::Close();
    io_vec_list_.clear();
    pending_buffers_.clear();
    write_index_ = 0;
    EnableWriting(false);

    if (close_cb_)
    {
        close_cb_(std::dynamic_pointer_cast<TcpConnection>(shared_from_this()));
    }
}

void tmms::network::TcpConnection::ForceClose()
{
    auto self = shared_from_this();
    loop_->RunInLoop([self]()
                     { self->OnClose(); });
}

void tmms::network::TcpConnection::OnRead()
{
    if (closed_)
    {
        NETWORK_TRACE << "host:" << peer_addr_.ToIpPort() << "had closed.";
        return;
    }
    ExtenLife();
    while (true)
    {
        int err = 0;
        auto ret = message_buffer_.readFd(fd_, &err);
        if (ret > 0)
        {
            if (message_cb_)
            {
                message_cb_(std::dynamic_pointer_cast<TcpConnection>(shared_from_this()), message_buffer_);
                if (closed_)
                    return;
            }
        }
        else if (ret == 0)
        {
            OnClose();
            break;
        }
        else
        {
            if (err != EINTR && err != EAGAIN && err != EWOULDBLOCK)
            {
                NETWORK_ERROR << "read err." << err;
                OnClose();
            }
            break;
        }
    }
}

void tmms::network::TcpConnection::SetRecvMsgCallback(const MessageCallback &cb)
{
    message_cb_ = cb;
}

void tmms::network::TcpConnection::SetRecvMsgCallback(MessageCallback &&cb)
{
    message_cb_ = std::move(cb);
}

void tmms::network::TcpConnection::OnError(const std::string &msg)
{
    NETWORK_TRACE << "host:" << peer_addr_.ToIpPort() << "error msg" << msg;
    OnClose();
}

void tmms::network::TcpConnection::OnWrite()
{
    if (closed_)
    {
        NETWORK_TRACE << "host:" << peer_addr_.ToIpPort() << "had closed.";
        return;
    }
    ExtenLife();
    if (write_index_ >= io_vec_list_.size())
    {
        EnableWriting(false);
        if (write_complete_cb_)
        {
            write_complete_cb_(std::dynamic_pointer_cast<TcpConnection>(shared_from_this()));
        }
        return;
    }

    while (true)
    {
        int iovcnt = static_cast<int>(io_vec_list_.size() - write_index_);
        auto ret = ::writev(fd_, io_vec_list_.data() + write_index_, iovcnt);

        if (ret > 0)
        {
            size_t remain = ret;

            while (remain > 0 && write_index_ < io_vec_list_.size())
            {
                auto &iov = io_vec_list_[write_index_];

                if (iov.iov_len > remain)
                {
                    iov.iov_base = static_cast<char *>(iov.iov_base) + remain;
                    iov.iov_len -= remain;
                    remain = 0;
                }
                else
                {
                    remain -= iov.iov_len;
                    write_index_++;
                }
            }

            if (write_index_ == io_vec_list_.size())
            {
                io_vec_list_.clear();
                pending_buffers_.clear();
                write_index_ = 0;

                EnableWriting(false);

                if (write_complete_cb_)
                {
                    write_complete_cb_(std::dynamic_pointer_cast<TcpConnection>(shared_from_this()));
                }
                return;
            }

            if (write_index_ > 0 && write_index_ > io_vec_list_.size() / 2)
            {
                io_vec_list_.erase(io_vec_list_.begin(),
                                   io_vec_list_.begin() + write_index_);
                pending_buffers_.erase(pending_buffers_.begin(),
                                       pending_buffers_.begin() + write_index_);
                write_index_ = 0;
            }
        }
        else
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
}

void tmms::network::TcpConnection::SetWriteCompleteCallback(const WriteCompleteCallback &cb)
{
    write_complete_cb_ = cb;
}

void tmms::network::TcpConnection::SetWriteCompleteCallback(WriteCompleteCallback &&cb)
{
    write_complete_cb_ = std::move(cb);
}

void tmms::network::TcpConnection::Send(std::list<BufferNodePtr> list)
{
    auto self = std::dynamic_pointer_cast<TcpConnection>(shared_from_this());
    loop_->RunInLoop([self, list = std::move(list)]() mutable
                     { self->SendInLoop(list); });
}

void tmms::network::TcpConnection::Send(const char *buf, size_t size)
{
    if (closed_ || buf == nullptr || size == 0)
    {
        return;
    }

    void *mem = std::malloc(size);
    if (!mem)
    {
        NETWORK_ERROR << "malloc failed in TcpConnection::Send";
        return;
    }

    auto node = std::make_shared<BufferNode>(mem, size);
    std::memcpy(node->addr, buf, size);

    Send({node});
}

void tmms::network::TcpConnection::OnTimeout()
{
    NETWORK_ERROR << "host" << peer_addr_.ToIpPort() << "timeout and close it.";
    OnClose();
}

void tmms::network::TcpConnection::SetTimeoutCallback(int timeout, TimeoutCallback cb)
{
    auto cp = std::dynamic_pointer_cast<TcpConnection>(shared_from_this());
    loop_->RunAfter(timeout, [cb = std::move(cb), cp = std::move(cp)]()
                    { cb(cp); });
}

void tmms::network::TcpConnection::EnableCheckIdleTimeout(int32_t max_time)
{
    auto tp = std::make_shared<TimeoutEntry>(std::dynamic_pointer_cast<TcpConnection>(shared_from_this()));
    max_idle_time_ = max_time;
    timeout_entry_ = tp;
    loop_->InsertEntry(max_time, tp);
}

void tmms::network::TcpConnection::SendInLoop(std::list<BufferNodePtr> &list)
{
    if (closed_)
    {
        NETWORK_TRACE << "host:" << peer_addr_.ToIpPort() << "had closed.";
        return;
    }
    bool need_enable = io_vec_list_.empty();
    for (auto &l : list)
    {
        io_vec_list_.push_back({l->addr, l->size});
        pending_buffers_.push_back(l);
    }
    int iovcnt = static_cast<int>(io_vec_list_.size() - write_index_);
    auto ret = ::writev(fd_, io_vec_list_.data() + write_index_, iovcnt);
    if (ret > 0)
    {
        size_t remain = ret;
        while (remain > 0 && write_index_ < io_vec_list_.size())
        {
            auto &iov = io_vec_list_[write_index_];
            if (iov.iov_len > remain)
            {
                iov.iov_base = static_cast<char *>(iov.iov_base) + remain;
                iov.iov_len -= remain;
                remain = 0;
            }
            else
            {
                remain -= iov.iov_len;
                write_index_++;
            }
        }
        if (write_index_ == io_vec_list_.size())
        {
            io_vec_list_.clear();
            pending_buffers_.clear();
            write_index_ = 0;

            if (write_complete_cb_)
            {
                write_complete_cb_(std::dynamic_pointer_cast<TcpConnection>(shared_from_this()));
            }
            return;
        }
    }
    else if (ret < 0)
    {
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            NETWORK_ERROR << "host:" << peer_addr_.ToIpPort() << "writev err:" << errno;
            OnClose();
            return;
        }
    }
    if (need_enable && !io_vec_list_.empty())
    {
        EnableWriting(true);
    }
}

void tmms::network::TcpConnection::SendInLoop(const char *buf, size_t size)
{
    if (closed_)
    {
        NETWORK_TRACE << "host:" << peer_addr_.ToIpPort() << "had closed.";
        return;
    }
    size_t send_len = 0;
    if (io_vec_list_.empty())
    {
        send_len = ::write(fd_, buf, size);
        if (send_len < 0)
        {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                NETWORK_ERROR << "host:" << peer_addr_.ToIpPort() << "write err:" << errno;
                OnClose();
                return;
            }
            send_len = 0;
        }
        size -= send_len;
        if (size == 0)
        {
            if (write_complete_cb_)
            {
                write_complete_cb_(std::dynamic_pointer_cast<TcpConnection>(shared_from_this()));
            }
            return;
        }
    }
    if (size > 0)
    {
        void *mem = std::malloc(size);
        if (!mem)
        {
            NETWORK_ERROR << "malloc failed in SendInLoop";
            return;
        }
        auto node = std::make_shared<BufferNode>(mem, size);
        std::memcpy(node->addr, static_cast<const char *>(buf) + send_len, size);

        pending_buffers_.push_back(node);

        struct iovec vec;
        vec.iov_base = static_cast<char *>(node->addr);
        vec.iov_len = node->size;

        io_vec_list_.push_back(vec);
        EnableWriting(true);
    }
}

void tmms::network::TcpConnection::ExtenLife()
{
    auto tp = timeout_entry_.lock();
    if (tp)
    {
        loop_->InsertEntry(max_idle_time_, tp);
    }
}
