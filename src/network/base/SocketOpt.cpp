#include "SocketOpt.h"

using namespace tmms::network;

tmms::network::SocketOpt::SocketOpt(int sock, bool v6)
    : sock_(sock), is_v6_(v6)
{
}

int tmms::network::SocketOpt::CreateNonblockingTcpSocket(int family)
{
    int sock = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sock < 0)
    {
        NETWORK_ERROR << "socket failed.";
    }
    return sock;
}

int tmms::network::SocketOpt::CreateNonblockingUdpSocket(int family)
{
    int sock = ::socket(family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
    if (sock < 0)
    {
        NETWORK_ERROR << "socket failed.";
    }
    return sock;
}

int tmms::network::SocketOpt::BindAddress(const InetAddress &localaddr)
{
    if (localaddr.IsIpv6())
    {
        struct sockaddr_in6 addr;
        localaddr.GetSockAddr((struct sockaddr *)&addr);
        socklen_t size = sizeof(struct sockaddr_in6);
        return ::bind(sock_, (const sockaddr *)&addr, size);
    }
    else
    {
        struct sockaddr_in addr;
        localaddr.GetSockAddr((struct sockaddr *)&addr);
        socklen_t size = sizeof(struct sockaddr_in);
        return ::bind(sock_, (const sockaddr *)&addr, size);
    }
}

int tmms::network::SocketOpt::Listen()
{
    return ::listen(sock_, SOMAXCONN);
}

int tmms::network::SocketOpt::Accept(InetAddress *peeraddr)
{
    struct sockaddr_in6 addr;
    socklen_t len = sizeof(struct sockaddr_in6);
    int sock = ::accept4(sock_, (struct sockaddr *)&addr, &len, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (sock > 0)
    {
        if (addr.sin6_family == AF_INET)
        {
            char ip[16] = {0};
            struct sockaddr_in *saddr = (struct sockaddr_in *)&addr;
            ::inet_ntop(AF_INET, &(saddr->sin_addr.s_addr), ip, sizeof(ip));
            peeraddr->SetAddr(ip);
            peeraddr->SetPort(ntohs(saddr->sin_port));
        }
        else if (addr.sin6_family == AF_INET6)
        {
            char ip[INET6_ADDRSTRLEN] = {0};
            ::inet_ntop(AF_INET6, &(addr.sin6_addr), ip, sizeof(ip));
            peeraddr->SetAddr(ip);
            peeraddr->SetPort(ntohs(addr.sin6_port));
            peeraddr->SetIsIPV6(true);
        }
    }
    return sock;
}

int tmms::network::SocketOpt::Connect(const InetAddress &addr)
{
    if (addr.IsIpv6())
    {
        struct sockaddr_in6 addr_in6;
        addr.GetSockAddr((struct sockaddr *)&addr_in6);
        return ::connect(sock_, (struct sockaddr *)&addr_in6, sizeof(addr_in6));
    }
    else
    {
        struct sockaddr_in addr_in;
        addr.GetSockAddr((struct sockaddr *)&addr_in);
        return ::connect(sock_, (struct sockaddr *)&addr_in, sizeof(addr_in));
    }
}

InetAddressPtr tmms::network::SocketOpt::GetLocalAddr()
{
    struct sockaddr_in6 addr_in;
    socklen_t len = sizeof(struct sockaddr_in6);
    ::getsockname(sock_, (struct sockaddr *)&addr_in, &len);
    InetAddressPtr peeraddr = std::make_shared<InetAddress>();
    if (addr_in.sin6_family == AF_INET)
    {
        char ip[16] = {0};
        struct sockaddr_in *saddr = (struct sockaddr_in *)&addr_in;
        ::inet_ntop(AF_INET, &(saddr->sin_addr.s_addr), ip, sizeof(ip));
        peeraddr->SetAddr(ip);
        peeraddr->SetPort(ntohs(saddr->sin_port));
    }
    else if (addr_in.sin6_family == AF_INET6)
    {
        char ip[INET6_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET6, &(addr_in.sin6_addr), ip, sizeof(ip));
        peeraddr->SetAddr(ip);
        peeraddr->SetPort(ntohs(addr_in.sin6_port));
        peeraddr->SetIsIPV6(true);
    }
    return peeraddr;
}

InetAddressPtr tmms::network::SocketOpt::GetPeerAddr()
{
    struct sockaddr_in6 addr_in;
    socklen_t len = sizeof(struct sockaddr_in6);
    ::getpeername(sock_, (struct sockaddr *)&addr_in, &len);
    InetAddressPtr peeraddr = std::make_shared<InetAddress>();
    if (addr_in.sin6_family == AF_INET)
    {
        char ip[16] = {0};
        struct sockaddr_in *saddr = (struct sockaddr_in *)&addr_in;
        ::inet_ntop(AF_INET, &(saddr->sin_addr.s_addr), ip, sizeof(ip));
        peeraddr->SetAddr(ip);
        peeraddr->SetPort(ntohs(saddr->sin_port));
    }
    else if (addr_in.sin6_family == AF_INET6)
    {
        char ip[INET6_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET6, &(addr_in.sin6_addr), ip, sizeof(ip));
        peeraddr->SetAddr(ip);
        peeraddr->SetPort(ntohs(addr_in.sin6_port));
        peeraddr->SetIsIPV6(true);
    }
    return peeraddr;
}

void tmms::network::SocketOpt::SetTcpNoDelay(bool on)
{
    int optvalue = on ? 1 : 0;
    ::setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &optvalue, sizeof(optvalue));
}

void tmms::network::SocketOpt::SetReuseAddr(bool on)
{
    int optvalue = on ? 1 : 0;
    ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &optvalue, sizeof(optvalue));
}

void tmms::network::SocketOpt::SetReusePort(bool on)
{
    int optvalue = on ? 1 : 0;
    ::setsockopt(sock_, SOL_SOCKET, SO_REUSEPORT, &optvalue, sizeof(optvalue));
}

void tmms::network::SocketOpt::SetKeepAlive(bool on)
{
    int optvalue = on ? 1 : 0;
    ::setsockopt(sock_, SOL_SOCKET, SO_KEEPALIVE, &optvalue, sizeof(optvalue));
}

void tmms::network::SocketOpt::SetNonBlocking(bool on)
{
    int flags = ::fcntl(sock_, F_GETFL, 0);
    if (flags == -1)
    {
        NETWORK_ERROR << "fcntl F_GETFL failed.";
        return;
    }
    if (on)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    if (::fcntl(sock_, F_SETFL, flags) == -1)
    {
        NETWORK_ERROR << "fcntl F_SETFL failed.";
    }
}
