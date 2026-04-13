#include "network/base/SocketOpt.h"
#include "network/base/InetAddress.h"
#include <iostream>

using namespace tmms::network;

void TestClient()
{
    int sock = SocketOpt::CreateNonblockingTcpSocket(AF_INET);
    if (sock < 0)
    {
        std::cerr << "socket failed.sock:" << sock << "errno:" << errno << std::endl;
        return;
    }
    InetAddress server("192.168.47.136:3444");
    SocketOpt opt(sock);
    opt.SetNonBlocking(false);
    auto ret = opt.Connect(server);

    std::cout << "connect ret:" << ret << std::endl
              << "local:" << opt.GetLocalAddr()->ToIpPort() << std::endl
              << "peer:" << opt.GetPeerAddr()->ToIpPort() << std::endl;
}

void TestSever()
{
    int sock = SocketOpt::CreateNonblockingTcpSocket(AF_INET);
    if (sock < 0)
    {
        std::cerr << "socket failed.sock:" << sock << "errno:" << errno << std::endl;
        return;
    }
    InetAddress server("0.0.0.0:3444");
    SocketOpt opt(sock);
    opt.SetNonBlocking(false);
    if (opt.BindAddress(server) < 0)
    {
        std::cerr << "bind failed: " << errno << std::endl;
        return;
    }
    if (opt.Listen() < 0)
    {
        std::cerr << "listen failed: " << errno << std::endl;
        return;
    }
    InetAddress addr;
    auto ns = opt.Accept(&addr);
    if (ns < 0)
    {
        std::cerr << "accept failed: " << errno << std::endl;
        return;
    }

    std::cout << "Accept ret:" << ns << std::endl
              << "addr:" << addr.ToIpPort() << std::endl;
}

int main()
{
    TestSever();
    return 0;
}