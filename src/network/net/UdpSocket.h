#pragma once
#include "network/base/InetAddress.h"
#include "network/base/MsgBuffer.h"
#include "network/net/EventLoop.h"
#include "network/net/Connection.h"
#include <list>
#include <functional>
#include <memory>

namespace tmms
{
    namespace network
    {
        class UdpSocket;
        using UdpSocketPtr = std::shared_ptr<UdpSocket>;
        using UdpSocketMessageCallback = std::function<void(const InetAddress &addr, MsgBuffer &buf)>;
        using UdpSocketWriteCompleteCallback = std::function<void(const UdpSocketPtr &)>;
        using UdpSocketCloseCallback = std::function<void(const UdpSocketPtr &)>;
        using UdpSocketTimeoutCallback = std::function<void(const UdpSocketPtr &)>;
        struct UdpTimeoutEntry;
        struct UdpBufferNode : public BufferNode
        {
            UdpBufferNode(void *buf, size_t s, struct sockaddr *saddr, socklen_t len)
                : BufferNode(buf, s), dest_addr(nullptr), sock_len(0)
            {
                if (saddr && len > 0)
                {
                    dest_addr = (struct sockaddr *)std::malloc(len);
                    if (dest_addr)
                    {
                        std::memcpy(dest_addr, saddr, len);
                        sock_len = len;
                    }
                }
            }
            ~UdpBufferNode()
            {
                if (dest_addr)
                    std::free(dest_addr);
            }
            struct sockaddr *dest_addr{nullptr};
            socklen_t sock_len{0};
        };

        using UdpBufferNodePtr = std::shared_ptr<UdpBufferNode>;

        class UdpSocket : public Connection
        {
        public:
            UdpSocket(EventLoop *loop, int socketfd, const InetAddress &localAddr, const InetAddress &peerAddr);
            ~UdpSocket();

            void OnTimeout();
            void OnError(const std::string &msg) override;
            void OnRead() override;
            void OnWrite() override;
            void OnClose() override;
            void SetCloseCallback(const UdpSocketCloseCallback &cb);
            void SetCloseCallback(UdpSocketCloseCallback &&cb);
            void SetRecvMsgCallback(const UdpSocketMessageCallback &cb);
            void SetRecvMsgCallback(UdpSocketMessageCallback &&cb);
            void SetWriteCompleteCallback(const UdpSocketWriteCompleteCallback &cb);
            void SetWriteCompleteCallback(UdpSocketWriteCompleteCallback &&cb);
            void SetTimeoutCallback(int timeout, UdpSocketTimeoutCallback cb);
            void EnableCheckIdleTimeout(int32_t max_time);
            void Send(std::list<UdpBufferNodePtr> list);
            void Send(const char *buf, size_t size, struct sockaddr *addr, socklen_t len);
            void ForceClose() override;

        protected:
            bool closed_{false};

        private:
            void ExtenLife();
            void SendInLoop(std::list<UdpBufferNodePtr> list);

            std::list<UdpBufferNodePtr> buffer_list_;

            int32_t max_idle_time_{30};
            std::weak_ptr<UdpTimeoutEntry> timeout_entry_;
            MsgBuffer message_buffer_;
            UdpSocketMessageCallback message_cb_;
            int32_t message_buffer_Size_{65535};
            UdpSocketWriteCompleteCallback write_complete_cb_;
            UdpSocketCloseCallback close_cb_;
        };
        struct UdpTimeoutEntry
        {
        public:
            UdpTimeoutEntry(const UdpSocketPtr &c) : conn(c) {}
            UdpTimeoutEntry(UdpSocketPtr &&c) : conn(std::move(c)) {}
            ~UdpTimeoutEntry()
            {
                auto c = conn.lock();
                if (c)
                {
                    c->OnTimeout();
                }
            }
            std::weak_ptr<UdpSocket> conn;
        };
    }
}