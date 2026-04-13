#include "network/base/InetAddress.h"
#include <string>
#include <iostream>
using namespace tmms::network;

int main()
{

    std::string host;
    while (std::cin >> host)
    {
        InetAddress addr(host);
        std::cout << "host:" << host << std::endl
                  << "ip:" << addr.IP() << std::endl
                  << "port:" << addr.Port() << std::endl
                  << "is_lan:" << addr.IsLanIp() << std::endl
                  << "is_wan:" << addr.IsWanIp() << std::endl
                  << "is_loop:" << addr.IsLoopbackIp() << std::endl;
    }
    return 0;
}