#include "network/net/EventLoopThread.h"
#include "network/UdpServer.h"
#include <iostream>
#include <cstring>

using namespace tmms::network;

static const char *g_http_response =
    "HTTP/1.0 200 OK\r\n"
    "Content-Length: 2\r\n"
    "\r\n"
    "OK";

int main()
{
    EventLoopThread eventloop_thread;
    eventloop_thread.Run();
    EventLoop *loop = eventloop_thread.Loop();

    if (!loop)
    {
        std::cerr << "EventLoop init failed" << std::endl;
        return -1;
    }

    InetAddress listen("0.0.0.0:34444");
    auto server = std::make_shared<UdpServer>(loop, listen);

    server->SetRecvMsgCallback([&server](const InetAddress &addr, MsgBuffer &buf)
                               {
         std::cout << "Received " << buf.readableBytes() << " bytes from " << addr.ToIpPort()
              << ", data: " << std::string(buf.peek(), buf.readableBytes()) << std::endl;

    if (addr.IsIpv6()) {
        struct sockaddr_in6 addr6;
        addr.GetSockAddr((struct sockaddr*)&addr6);
        server->Send(buf.peek(), buf.readableBytes(), (struct sockaddr*)&addr6, sizeof(addr6));
    } else {
        struct sockaddr_in addr4;
        addr.GetSockAddr((struct sockaddr*)&addr4);
        server->Send(buf.peek(), buf.readableBytes(), (struct sockaddr*)&addr4, sizeof(addr4));
    }

    buf.retrieveAll(); });

    server->SetCloseCallback([](const UdpSocketPtr &con)
                             {
        if(con)
        {
            std::cout<<"host:"<<con->PeerAddr().ToIpPort()<<"closed."<<std::endl;
        } });

    server->Start();
    std::cout << "UDP server started on " << listen.ToIpPort() << std::endl;

    while (1)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}