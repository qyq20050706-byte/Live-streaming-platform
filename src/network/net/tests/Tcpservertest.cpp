#include "network/net/EventLoopThread.h"
#include "network/net/TcpServer.h"
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

    // 1. 设置消息回调：收到请求后发送响应
    server.SetMessageCallback([](const TcpConnectionPtr &con, MsgBuffer &buf)
                              {
        buf.retrieveAll();  // 忽略请求内容
        con->Send(g_http_response, strlen(g_http_response)); });

    // 2. 设置写完成回调：数据发送完毕后关闭连接
    server.SetNewConnectionCallback([&loop](const TcpConnectionPtr &con)
    { con->SetWriteCompleteCallback([loop](const TcpConnectionPtr &con) {
            std::cout << "Write complete, closing: " << con->PeerAddr().ToIpPort() << std::endl;
            loop->DelEvent(con);
            con->ForceClose();
});});
                                                                    

    // 3. （可选）设置连接关闭回调，用于日志或统计
    server.Start();

    while (1)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}