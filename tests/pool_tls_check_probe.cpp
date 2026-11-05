// Test-only parent driver. Request material arrives on stdin, never arguments/environment or public diagnostics.
#include "product/pool_tls_check.h"
#include <charconv>
#include <condition_variable>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif
namespace {
// Test-only cancellation trigger. Completion always wakes/joins the monitor, even on an exception
// or when no trigger file arrives; a missing trigger must not extend the production total budget.
class CancellationMonitor {
    std::mutex mutex_;std::condition_variable wake_;bool done_=false;std::thread thread_;
   public:
    CancellationMonitor(std::atomic<bool> &cancel,unsigned delay,const std::filesystem::path &file){
        if(delay||!file.empty())thread_=std::thread([&,delay,file]{std::unique_lock<std::mutex> lock(mutex_);
            if(delay){if(!wake_.wait_for(lock,std::chrono::milliseconds(delay),[&]{return done_;}))cancel=true;return;}
            while(!done_){std::error_code error;if(std::filesystem::exists(file,error)&&!error){cancel=true;return;}
                wake_.wait_for(lock,std::chrono::milliseconds(5),[&]{return done_;});}
        });
    }
    void stop(){{std::lock_guard<std::mutex> lock(mutex_);done_=true;}wake_.notify_all();if(thread_.joinable())thread_.join();}
    ~CancellationMonitor(){stop();}
};
}
int main(int argc,char **argv){
    using namespace om;
    if(argc!=2&&argc!=3&&argc!=4)return 2;
    try{
#if defined(_WIN32)
        if(_setmode(_fileno(stdin),_O_BINARY)<0)return 3;
#endif
        std::string wire;char bytes[4096];for(;;){const auto n=std::fread(bytes,1,sizeof(bytes),stdin);if(n>localTaskRequestLimit-wire.size())return 3;wire.append(bytes,n);if(n<sizeof(bytes))break;}
        PoolTlsCheckRequest request;uint64_t unused=0;if(!decodePoolTlsCheckRequest(wire,&request,&unused))return 4;
        unsigned delay=0;if(argc==3){const std::string word=argv[2];const auto r=std::from_chars(word.data(),word.data()+word.size(),delay);if(r.ec!=std::errc{}||r.ptr!=word.data()+word.size()||delay>60000)return 5;}
        std::filesystem::path cancelFile;if(argc==4){if(std::string(argv[2])!="--cancel-file"||!argv[3][0])return 5;cancelFile=std::filesystem::u8path(argv[3]);}
        std::atomic<bool> cancel{false};CancellationMonitor monitor(cancel,delay,cancelFile);
        const auto begin=std::chrono::steady_clock::now();PoolTlsCheckClient client(std::filesystem::u8path(argv[1]));const auto result=client.inspect(request,cancel);
        monitor.stop();
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
        std::cout<<"{\"transport\":"<<static_cast<unsigned>(result.transport)<<",\"started\":"<<(result.childStarted?1:0)
            <<",\"status\":"<<(result.reply?static_cast<int>(result.reply->status):-1)<<",\"stage\":"<<(result.reply?static_cast<int>(result.reply->stage):-1)
            <<",\"tls\":"<<(result.reply?static_cast<int>(result.reply->tlsError):-1)<<",\"elapsedMs\":"<<elapsed<<"}"<<std::endl;return 0;
    }catch(...){return 6;}
}
