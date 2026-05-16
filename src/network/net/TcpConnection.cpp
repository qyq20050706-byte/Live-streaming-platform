#include "TcpConnection.h"
#include "network/base/Network.h"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <limits.h>

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

    // 1. 先清理发送缓冲区
    io_vec_list_.clear();
    pending_buffers_.clear();
    write_index_ = 0;

    // 2. 从 epoll 删除，关闭 fd
    loop_->DelEvent(shared_from_this());
    Event::Close();

    // 3. 最后触发回调（回调里可能会 erase 导致析构）
    // 用局部变量保存 cb，防止 close_cb_ 被清空后悬空
    auto cb = close_cb_;
    auto self = std::dynamic_pointer_cast<TcpConnection>(shared_from_this());

    // 先清空成员，再调用回调
    close_cb_ = nullptr;

    if (cb)
    {
        cb(self);
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
            message_buffer_.retrieveAll();
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

        auto self = std::dynamic_pointer_cast<TcpConnection>(shared_from_this());

        if (write_complete_cb_)
        {
            write_complete_cb_(self);
        }

        if (closed_)
        {
            return;
        }

        if (close_after_write_ &&
            (io_vec_list_.empty() || write_index_ >= io_vec_list_.size()))
        {
            close_after_write_ = false;
            OnClose();
        }
        return;
    }

    while (write_index_ < io_vec_list_.size())
    {
        size_t total_iov = io_vec_list_.size() - write_index_;
        int iovcnt = static_cast<int>(std::min(total_iov, (size_t)IOV_MAX));
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

                auto self = std::dynamic_pointer_cast<TcpConnection>(shared_from_this());

                if (write_complete_cb_)
                {
                    write_complete_cb_(self);
                }

                if (closed_)
                {
                    return;
                }

                // 发完再关
                if (close_after_write_ && io_vec_list_.empty())
                {
                    close_after_write_ = false;
                    OnClose();
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
    auto list_ptr = std::make_shared<std::list<BufferNodePtr>>(std::move(list));
    loop_->RunInLoop([self, list_ptr]()
                     { self->SendInLoop(*list_ptr); });
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

void tmms::network::TcpConnection::CloseAfterWrite()
{
    auto self = std::dynamic_pointer_cast<TcpConnection>(shared_from_this());
    loop_->RunInLoop([self]()
                     {
        if (self->closed_)
        {
            return;
        }

        self->close_after_write_ = true;

        // 如果当前没有任何待发送数据，直接关闭
        if (self->io_vec_list_.empty() ||
            self->write_index_ >= self->io_vec_list_.size())
        {
            self->close_after_write_ = false;
            self->OnClose();
        } });
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
    size_t total_iov = io_vec_list_.size() - write_index_;
    int iovcnt = static_cast<int>(std::min(total_iov, (size_t)IOV_MAX));
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
            if (write_index_ == io_vec_list_.size())
            {
                io_vec_list_.clear();
                pending_buffers_.clear();
                write_index_ = 0;

                auto self = std::dynamic_pointer_cast<TcpConnection>(shared_from_this());

                if (write_complete_cb_)
                {
                    write_complete_cb_(self);
                }

                if (closed_)
                {
                    return;
                }

                // 立即写完的情况，也要支持“发完再关”
                if (close_after_write_ && io_vec_list_.empty())
                {
                    close_after_write_ = false;
                    OnClose();
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

void tmms::network::TcpConnection::ExtenLife()
{
    auto tp = timeout_entry_.lock();
    if (tp)
    {
        loop_->InsertEntry(max_idle_time_, tp);
    }
}
