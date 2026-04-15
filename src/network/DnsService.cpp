#include "DnsService.h"
#include <functional>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <cstring>
#include "network/base/Network.h"

using namespace tmms::network;
namespace
{
    static InetAddressPtr inet_address_null;
}
tmms::network::DnsService::~DnsService()
{
    Stop();
}

void tmms::network::DnsService::AddHost(const std::string &host, int index)
{
    std::lock_guard<std::mutex> lk(lock_);
    auto iter = hosts_info_.find(host);
    if (iter != hosts_info_.end())
    {
        return;
    }

    hosts_info_[host] = std::vector<InetAddressPtr>();
}

InetAddressPtr tmms::network::DnsService::GetHostAddress(const std::string &host, int index)
{
    std::lock_guard<std::mutex> lk(lock_);
    auto iter = hosts_info_.find(host);
    if (iter != hosts_info_.end())
    {
        auto &list = iter->second;
        if (!list.empty())
        {
            return list[index % (list.size())];
        }
    }
    return inet_address_null;
}

std::vector<InetAddressPtr> tmms::network::DnsService::GetHostAddresses(const std::string &host)
{
    std::lock_guard<std::mutex> lk(lock_);
    auto iter = hosts_info_.find(host);
    if (iter != hosts_info_.end())
    {
        return iter->second;
    }
    else
    {
        return std::vector<InetAddressPtr>();
    };
}

void tmms::network::DnsService::UpdateHost(const std::string &host, std::vector<InetAddressPtr> &list)
{
    std::lock_guard<std::mutex> lk(lock_);
    hosts_info_[host].swap(list);
}

std::unordered_map<std::string, std::vector<InetAddressPtr>> tmms::network::DnsService::GetHosts()
{
    std::lock_guard<std::mutex> lk(lock_);
    return hosts_info_;
}

void tmms::network::DnsService::SetDnsServiceParam(int32_t interval, int32_t sleep, int32_t retry)
{
    interval_ = interval;
    sleep_ = sleep;
    retry_ = retry;
}

void tmms::network::DnsService::Start()
{
    if(running_) return;
    running_ = true;
    thread_ = std::thread(std::bind(&DnsService::OnWork, this));
}

void tmms::network::DnsService::Stop()
{
    if(!running_) return;
    running_ = false;
    if (thread_.joinable())
    {
        thread_.join();
    }
}

void tmms::network::DnsService::OnWork()
{
    while (running_)
    {
        auto host_infos = GetHosts();
        for (auto &host : host_infos)
        {
            for(int i=0;i<retry_;++i){
                std::vector<InetAddressPtr> list;
                GetHostinfo(host.first,list);
                if(!list.empty()){
                    UpdateHost(host.first,list);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_));
    }
}

void tmms::network::DnsService::GetHostinfo(const std::string &host, std::vector<InetAddressPtr> &list)
{
    struct addrinfo ainfo, *res = nullptr;
    std::memset(&ainfo, 0x00, sizeof(addrinfo));
    ainfo.ai_family = AF_UNSPEC;
    ainfo.ai_flags = 0;
    ainfo.ai_socktype = SOCK_STREAM;
    auto ret = ::getaddrinfo(host.c_str(), nullptr, &ainfo, &res);
    if (ret != 0)
    {
        NETWORK_ERROR << "getaddrinfo failed for " << host
                      << ": " << gai_strerror(ret);
        return;
    }
    if (res == nullptr)
    {
        return;
    }
    struct addrinfo *rp = res;
    for (; rp != nullptr; rp = rp->ai_next)
    {
        InetAddressPtr peeraddr = std::make_shared<InetAddress>();
        if (rp->ai_family == AF_INET)
        {
            char ip[16] = {0};
            struct sockaddr_in *saddr = (struct sockaddr_in *)rp->ai_addr;
            ::inet_ntop(AF_INET, &(saddr->sin_addr.s_addr), ip, sizeof(ip));
            peeraddr->SetAddr(ip);
            peeraddr->SetPort(ntohs(saddr->sin_port));
        }
        else if (rp->ai_family == AF_INET6)
        {
            char ip[INET6_ADDRSTRLEN] = {0};
            struct sockaddr_in6 *saddr = (struct sockaddr_in6 *)rp->ai_addr;
            ::inet_ntop(AF_INET6, &(saddr->sin6_addr), ip, sizeof(ip));
            peeraddr->SetAddr(ip);
            peeraddr->SetPort(ntohs(saddr->sin6_port));
            peeraddr->SetIsIPV6(true);
        }
        list.push_back(peeraddr);
    }
    ::freeaddrinfo(res);
}