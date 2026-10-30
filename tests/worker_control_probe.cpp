// Owned CPU-only pipe fixture. Its stdin/stdout are private test channels, never miner output or a production transport.
#include "product/worker_control.h"
#include <atomic>
#include <cstdio>
#include <thread>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <cerrno>
#include <unistd.h>
#endif
using namespace om;
namespace {
struct Probe final:RunPauseControl {
    WorkerControlGate gate{{"11111111111111111111111111111111","22222222222222222222222222222222",1,1,0}};
    std::mutex output;std::string last;
    void emit(){std::lock_guard<std::mutex> lock(output);const auto view=gate.snapshot();std::string wire;
        if(view.state==WorkerControlState::Active&&view.receipt&&encodeWorkerReceipt(*view.receipt,&wire)&&wire!=last){
            if(std::fwrite(wire.data(),1,wire.size(),stdout)!=wire.size()||std::fflush(stdout)!=0)gate.fail();
            last=std::move(wire);}}
    bool pauseRequested()const noexcept override{return gate.pauseRequested();}
    WorkControlRequest requestState()const noexcept override{return gate.requestState();}
    void pauseBoundary(bool paused)noexcept override{gate.pauseBoundary(paused);emit();}
    void pauseHeld()noexcept override{gate.pauseHeld();emit();}
};
}
int main(){
#if defined(_WIN32)
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    Probe probe;std::printf("CMREADY1 %llu\n",static_cast<unsigned long long>(workerControlNowMs()));std::fflush(stdout);
    std::thread receiver([&]{WorkerControlFramer frames;char bytes[64];for(;;){
#if defined(_WIN32)
        const int n=_read(_fileno(stdin),bytes,sizeof(bytes));
#else
        const auto n=::read(STDIN_FILENO,bytes,sizeof(bytes));if(n<0&&errno==EINTR)continue;
#endif
        if(n==0){probe.gate.requestStop();return;}if(n<0){probe.gate.fail();return;}
        if(!frames.feed(std::string_view(bytes,static_cast<size_t>(n)),[&](std::string_view line){
            const bool ok=probe.gate.receiveFrame(line)!=WorkerCommandResult::Rejected;probe.emit();return ok;})){probe.gate.fail();return;}
    }});
    unsigned batches=0,drains=0;WorkPauseResult result;
    for(;;){result=pauseAtWorkBoundary(probe,[]{return false;},[&]{++drains;return true;},[](bool){},workerControlNowMs,
        [](unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));});
        if(result!=WorkPauseResult::Ready)break;
        ++batches;probe.emit();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    {std::lock_guard<std::mutex> lock(probe.output);std::printf("CMDONE1 %s %u %u\n",result==WorkPauseResult::Stopped?"stopped":"fault",batches,drains);std::fflush(stdout);}
    // The fixture parent closes stdin after CMDONE1, so even a deliberately half-written frame can join without unsafe detach.
    receiver.join();return result==WorkPauseResult::Stopped?0:8;
}
