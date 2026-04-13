#include "network/net/Acceptor.h"
#include "network/net/EventLoopThread.h"
#include "network/net/TcpConnection.h"
#include <iostream>
#include <algorithm>

using namespace tmms::network;

// 完整的 HTTP 响应（注意结尾的 \r\n\r\n）
const char *http_response =
    "HTTP/1.0 200 OK\r\n"
    "Server: tmms\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: 0\r\n"
    "\r\n"; // 空行表示头部结束

int main()
{
    EventLoopThread eventloop_thread;
    eventloop_thread.Run();
    EventLoop *loop = eventloop_thread.Loop();

    if (!loop)
        return -1;

    std::vector<TcpConnectionPtr> connections; // 存储活跃连接
    InetAddress server("192.168.47.136:34444");
    auto acceptor = std::make_shared<Acceptor>(loop, server);

    acceptor->SetAcceptCallback([loop, &server, &connections](int fd, const InetAddress &addr)
                                {
        std::cout << "New connection from: " << addr.ToIpPort() << std::endl;

        auto conn = std::make_shared<TcpConnection>(loop, fd, server, addr);

        conn->EnableCheckIdleTimeout(3);

        // 1. 设置收到消息的回调
        conn->SetRecvMsgCallback([](const TcpConnectionPtr &con, MsgBuffer &buf) {
            // 安全地取出所有数据并转为字符串
            std::string msg = buf.retrieveAllAsString();
            std::cout << "Received: " << msg << std::endl;

            // 发送 HTTP 响应
            con->Send(http_response, strlen(http_response));
        });

        // 2. 设置写完成回调（发送完毕后关闭连接）
        conn->SetWriteCompleteCallback([loop, &connections](const TcpConnectionPtr &con) {
            std::cout << "Write complete, closing: " << con->PeerAddr().ToIpPort() << std::endl;
            loop->DelEvent(con);
            con->ForceClose();
            // 从连接列表中移除
            auto it = std::find(connections.begin(), connections.end(), con);
            if (it != connections.end()) {
                connections.erase(it);
            }
        });

        // 3. 设置关闭回调（用于清理）
        conn->SetCloseCallback([&connections](const TcpConnectionPtr &con) {
            std::cout << "Connection closed: " << con->PeerAddr().ToIpPort() << std::endl;
            // 确保从列表中移除（写完成回调中可能已经移除，这里再做一次安全删除）
            auto it = std::find(connections.begin(), connections.end(), con);
            if (it != connections.end()) {
                connections.erase(it);
            }
        });

        // 保存连接并添加到事件循环
        
        connections.push_back(conn);
        loop->AddEvent(conn); });

    acceptor->Start();
    std::cout << "Server listening on " << server.ToIpPort() << std::endl;

    // 主线程等待（更好的做法是等待 I/O 线程结束）
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}