#include "network/net/EventLoopThread.h"
#include "network/UdpClient.h"
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
    std::shared_ptr<UdpClient> client = std::make_shared<UdpClient>(loop, server);

    client->SetRecvMsgCallback([](const InetAddress &addr, MsgBuffer &buf)
                               {
        std::cout<<"host:"<<addr.ToIpPort()<<"msg:"<<buf.peek()<<std::endl;
        buf.retrieveAll(); });

    client->SetCloseCallback([](const UdpSocketPtr &con)
                             {
        if(con){
            std::cout<<"host:"<<con->PeerAddr().ToIpPort()<<"closed."<<std::endl;
        } });
    client->SetWriteCompleteCallback([](const UdpSocketPtr &con)
                                     {
        if(con){
            std::cout<<"host:"<<con->PeerAddr().ToIpPort()<<"write complete."<<std::endl;
        } });
    client->SetConnectedCallback([&client](const UdpSocketPtr &con, bool connected)
                                 {
        if(connected){
            client->Send("1111", strlen("1111"));
        } 
        else{
            std::cout << "connection closed normally" << std::endl;
        } });

    client->Connect();
    std::string line;

    while (std::getline(std::cin, line))
    {
        if (!line.empty())
        {
            client->Send(line.c_str(), line.size());
        }
    }

    return 0;
}