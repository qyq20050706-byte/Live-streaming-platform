#include "InetAddress.h"
#include <cstring>
#include "Network.h"

using namespace tmms::network;

tmms::network::InetAddress::InetAddress(const std::string &ip, uint16_t port, bool bv6)
    : addr_(ip), port_(std::to_string(port)), is_ipv6_(bv6)
{
}

tmms::network::InetAddress::InetAddress(const std::string &host, bool is_v6)
    : is_ipv6_(is_v6)
{
    GetIpAndPort(host, addr_, port_);
}

void tmms::network::InetAddress::SetHost(const std::string &host)
{
    GetIpAndPort(host, addr_, port_);
}

void tmms::network::InetAddress::SetAddr(const std::string &addr)
{
    addr_ = addr;
}

void tmms::network::InetAddress::SetPort(uint16_t port)
{
    port_ = std::to_string(port);
}

void tmms::network::InetAddress::SetIsIPV6(bool is_v6)
{
    is_ipv6_ = is_v6;
}

const std::string &tmms::network::InetAddress::IP() const
{
    return addr_;
}

uint32_t tmms::network::InetAddress::IPv4(const char *ip) const
{
    struct sockaddr_in addr_in;
    memset(&addr_in, 0x00, sizeof(struct sockaddr_in));
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = 0;
    if (::inet_pton(AF_INET, ip, &addr_in.sin_addr) <= 0)
    {
        NETWORK_ERROR << "IPv4 ip:" << ip << "convert failed.";
        return 0;
    }
    return ntohl(addr_in.sin_addr.s_addr);
}

std::string tmms::network::InetAddress::ToIpPort() const
{
    std::ostringstream ss;
    ss << addr_ << ":" << port_;
    return ss.str();
}

uint16_t tmms::network::InetAddress::Port() const
{
    return static_cast<uint16_t>(std::atoi(port_.c_str()));
}

void tmms::network::InetAddress::GetSockAddr(struct sockaddr *saddr) const
{
    if (is_ipv6_)
    {
        auto *addr_in6 = reinterpret_cast<struct sockaddr_in6 *>(saddr);
        memset(addr_in6, 0x00, sizeof(struct sockaddr_in6));
        addr_in6->sin6_family = AF_INET6;
        addr_in6->sin6_port = htons(std::atoi(port_.c_str()));
        if (::inet_pton(AF_INET6, addr_.c_str(), &addr_in6->sin6_addr) <= 0)
        {
            NETWORK_ERROR << "Invalid IPv6 address: " << addr_;
        }
    }
    else
    {
        auto *addr_in = reinterpret_cast<struct sockaddr_in *>(saddr);
        memset(addr_in, 0x00, sizeof(struct sockaddr_in));
        addr_in->sin_family = AF_INET;
        addr_in->sin_port = htons(std::atoi(port_.c_str()));
        if (::inet_pton(AF_INET, addr_.c_str(), &addr_in->sin_addr) <= 0)
        {
            NETWORK_ERROR << "Invalid IPv4 address: " << addr_;
        }
    }
}

bool tmms::network::InetAddress::IsIpv6() const
{
    return is_ipv6_;
}

bool tmms::network::InetAddress::IsWanIp() const
{
    return !IsLanIp() && !IsLoopbackIp() && addr_ != "0.0.0.0";
}

bool tmms::network::InetAddress::IsLanIp() const
{
    static const uint32_t a_start = IPv4("10.0.0.0");
    static const uint32_t a_end = IPv4("10.255.255.255");
    static const uint32_t b_start = IPv4("172.16.0.0");
    static const uint32_t b_end = IPv4("172.31.255.255");
    static const uint32_t c_start = IPv4("192.168.0.0");
    static const uint32_t c_end = IPv4("192.168.255.255");
    static const uint32_t ip = IPv4(addr_.c_str());

    bool is_a = ip >= a_start && ip <= a_end;
    bool is_b = ip >= b_start && ip <= b_end;
    bool is_c = ip >= c_start && ip <= c_end;

    return is_a || is_b || is_c;
}

bool tmms::network::InetAddress::IsLoopbackIp() const
{
    if (is_ipv6_)
        return addr_ == "::1";
    else
        return addr_.compare(0, 4, "127.") == 0;
}

void tmms::network::InetAddress::GetIpAndPort(const std::string &host, std::string &ip, std::string &port)
{
    auto pos = host.find_last_of(":");
    if (pos != std::string::npos)
    {
        ip = host.substr(0, pos);
        port = host.substr(pos + 1);
        return;
    }
    else
    {
        ip = host;
    }
}
