#include "network/DnsService.h"
#include <iostream>

using namespace tmms::network;

int main(){
    std::vector<InetAddressPtr> list;
    sDnsService->AddHost("www.baidu.com");
    sDnsService->Start();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    list=sDnsService->GetHostAddresses("www.baidu.com");
    for(auto &i:list){
        std::cout<<"ip:"<<i->IP()<<std::endl;
    }
    return 0;
}