#include "network/net/EventLoopThread.h"
#include "network/TcpClient.h"
#include <iostream>
#include <cstring>
#include <memory>

using namespace tmms::network;

static const char *g_http_response =
    "HTTP/1.0 200 OK\r\n"
    "Content-Length: 2\r\n"
    "\r\n"
    "OK";

static const char *g_http_request =
    "GET / HTTP/1.0\r\n"
    "Host: 192.168.47.136\r\n"
    "\r\n";

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

    InetAddress server("192.168.47.136:34444");
    std::shared_ptr<TcpClient> client=std::make_shared<TcpClient>(loop, server);

    client->SetRecvMsgCallback([](const TcpConnectionPtr &con, MsgBuffer &buf)
                              {
        std::cout << "Recv from server: " << buf.peek() << std::endl;
        buf.retrieveAll(); 
        con->ForceClose();
                              });

    client->SetCloseCallback([](const TcpConnectionPtr &con)
                            {
        if(con){
            std::cout<<"host:"<<con->PeerAddr().ToIpPort()<<"closed."<<std::endl;
        } });
    client->SetWriteCompleteCallback([](const TcpConnectionPtr &con)
                                    {
        if(con){
            std::cout<<"host:"<<con->PeerAddr().ToIpPort()<<"write complete."<<std::endl;
        } });
    client->SetConnectCallback([](const TcpConnectionPtr &con, bool connected)
                              {
        if(connected){
            // con->Send(g_http_request, strlen(g_http_request));
        } 
        else{
            std::cout << "connection closed normally" << std::endl;
        }
        });

    client->Connect();

    while (1)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}