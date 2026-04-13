#include "network/net/EventLoopThread.h"
#include "network/TcpServer.h"
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
    TcpServer server(loop, listen);

    server.SetMessageCallback([](const TcpConnectionPtr &con, MsgBuffer &buf)
                              {
        buf.retrieveAll();  
        con->Send(g_http_response, strlen(g_http_response)); });

    server.SetNewConnectionCallback([](const TcpConnectionPtr &con)
                                    { con->SetWriteCompleteCallback([](const TcpConnectionPtr &con)
                                                                    {
                                                                        std::cout << "Write complete, closing: "
                                                                                  << con->PeerAddr().ToIpPort() << std::endl;
                                                                        con->ForceClose(); // 框架全包，无需手动操作
                                                                    }); });

    server.Start();

    while (1)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}