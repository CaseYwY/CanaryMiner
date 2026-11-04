// Test-only parent driver. Request material arrives on stdin, never arguments/environment or public diagnostics.
#include "product/pool_tls_check.h"
#include <charconv>
#include <cstdio>
#include <iostream>
#include <thread>
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif
int main(int argc,char **argv){
    using namespace om;
    if(argc!=2&&argc!=3)return 2;
    try{
#if defined(_WIN32)
        if(_setmode(_fileno(stdin),_O_BINARY)<0)return 3;
#endif
        std::string wire;char bytes[4096];for(;;){const auto n=std::fread(bytes,1,sizeof(bytes),stdin);if(n>localTaskRequestLimit-wire.size())return 3;wire.append(bytes,n);if(n<sizeof(bytes))break;}
        PoolTlsCheckRequest request;uint64_t unused=0;if(!decodePoolTlsCheckRequest(wire,&request,&unused))return 4;
        unsigned delay=0;if(argc==3){const std::string word=argv[2];const auto r=std::from_chars(word.data(),word.data()+word.size(),delay);if(r.ec!=std::errc{}||r.ptr!=word.data()+word.size()||delay>60000)return 5;}
        std::atomic<bool> cancel{false};std::thread timer;if(delay)timer=std::thread([&]{std::this_thread::sleep_for(std::chrono::milliseconds(delay));cancel=true;});
        const auto begin=std::chrono::steady_clock::now();PoolTlsCheckClient client(std::filesystem::u8path(argv[1]));const auto result=client.inspect(request,cancel);
        if(timer.joinable())timer.join();
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
        std::cout<<"{\"transport\":"<<static_cast<unsigned>(result.transport)<<",\"started\":"<<(result.childStarted?1:0)
            <<",\"status\":"<<(result.reply?static_cast<int>(result.reply->status):-1)<<",\"stage\":"<<(result.reply?static_cast<int>(result.reply->stage):-1)
            <<",\"tls\":"<<(result.reply?static_cast<int>(result.reply->tlsError):-1)<<",\"elapsedMs\":"<<elapsed<<"}"<<std::endl;return 0;
    }catch(...){return 6;}
}
