// Test-owned parent for true process-death checks. Reports only its synthetic helper PID.
#include "product/duplex_task_process.h"
#include <iostream>
#include <thread>
int main(int argc,char **argv){using namespace om;if(argc!=2)return 2;std::atomic<bool> cancel{false};DuplexTaskProcess child;
    if(!child.start(std::filesystem::u8path(argv[1]),cancel)||!child.send("P"))return 3;
    std::string response;const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<until){const auto r=child.poll();if(r.channelFault||r.state!=DuplexTaskState::Running)return 4;response+=r.bytes;
        if(response.find('\n')!=response.npos){std::cout<<response<<std::flush;std::this_thread::sleep_for(std::chrono::seconds(30));return 0;}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    return 5;
}
