#pragma once
#include "network/base/InetAddress.h"
#include "Event.h"
#include "EventLoop.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <atomic>

namespace tmms
{
    namespace network
    {
        enum
        {
            kNormalContext = 0,
            kRtmpContext,
            kHttpContext,
            kUserContext,
            kFlvContext,
        };
        using ContextPtr = std::shared_ptr<void>;
        class Connection;
        using ConnectionPtr = std::shared_ptr<Connection>;
        using ActiveCallback = std::function<void(const ConnectionPtr &)>;
        class Connection : public Event
        {
        public:
            Connection(EventLoop *loop, int fd, const InetAddress &localAddr, const InetAddress &peerAddr);
            virtual ~Connection() = default;

            void SetLocalAddr(const InetAddress &local);
            void SetLocalAddr(InetAddress &&local);
            void SetPeerAddr(const InetAddress &peer);
            void SetPeerAddr(InetAddress &&peer);
            const InetAddress &LocalAddr() const;
            const InetAddress &PeerAddr() const;

            template <typename T>
            std::shared_ptr<T> GetContext(int type) const
            {
                auto iter = contexts_.find(type);
                if (iter != contexts_.end())
                {
                    return std::dynamic_pointer_cast<T>(iter->second);
                }
                return std::make_shared<T>();
            }
            void SetContext(int type, const std::shared_ptr<void> &context);
            void SetContext(int type, std::shared_ptr<void> &&context);
            void ClearContext(int type);
            void ClearContext();

            void SetActiveCallback(const ActiveCallback &cb);
            void SetActiveCallback(ActiveCallback &&cb);
            void Active();
            void Deactive();
            virtual void ForceClose() = 0;

        private:
            std::unordered_map<int, ContextPtr> contexts_;
            ActiveCallback active_cb_;
            std::atomic<bool> active_{false};

        protected:
            InetAddress local_addr_;
            InetAddress peer_addr_;
        };
    }
}