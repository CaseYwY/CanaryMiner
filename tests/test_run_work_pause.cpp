// Actual shared run loop, CPU algorithm and bounded in-memory source only. No telemetry, socket, device or Pearl proof qualification.
#include "core/run.h"
#include "product/worker_control.h"
#include "product/worker_process.h"
#include "product_test_util.h"
#include <algorithm>
#include <atomic>
#include <thread>
using namespace om;
namespace {
struct Rig;
Rig *active=nullptr;
struct Control:RunPauseControl {
    Rig *rig;
    explicit Control(Rig *r):rig(r){}
    bool pauseRequested()const noexcept override;
    void pauseBoundary(bool paused)noexcept override;
};
struct Rig {
    volatile sig_atomic_t stop=0;
    mutable bool requested=false;
    mutable unsigned polls=0;
    bool acknowledged=false,stopInPause=false,failDrain=false,pauseInPrepare=false,pauseInFetch=false;
    bool enabledHint=false,configured=false,sourceMade=false,uncontrolled=false,pauseOrdering=true;
    bool waitOrdering=true;std::vector<unsigned> appliedWaits;
    bool rawBoundaryOpen=false,rawOrdering=true;
    WorkerControlGate *gate=nullptr;uint64_t *gateClock=nullptr;unsigned gateMode=0;
    WorkerControlClient *nativeClient=nullptr;
    unsigned searches=0,submits=0,drains=0,resumes=0,fetches=0,version=1;
    uint64_t completedAttempts=0;
    std::vector<std::string> events;
    Control control{this};
};
bool Control::pauseRequested()const noexcept {
    if(rig->requested&&rig->acknowledged&&++rig->polls>=3){
        if(rig->stopInPause)rig->stop=1;
        else{rig->requested=false;rig->version=2;}
    }
    return rig->requested;
}
void Control::pauseBoundary(bool paused)noexcept{
    rig->pauseOrdering=rig->pauseOrdering&&(!rig->events.empty()&&rig->events.back()==(paused?"idle":"active"));
    if(paused)rig->pauseOrdering=rig->pauseOrdering&&rig->events.size()>=2&&rig->events[rig->events.size()-2]=="drain";
    rig->events.push_back(paused?"paused":"resume");rig->acknowledged=paused;
    if(!paused)++rig->resumes;
}
WorkerControlBinding binding(){return {"11111111111111111111111111111111","22222222222222222222222222222222",1,1,0};}
struct Source:JobSource {
    const char *describe()const override{return "fixture.workPauseTestSource";}
    bool fetch(Job *j)override{
        auto &r=*active;
        if(r.gate){const auto view=r.gate->snapshot();if(view.receipt&&view.receipt->command.revision>=4)r.version=2;}
        if(r.nativeClient){const auto view=r.nativeClient->snapshot();if(view.receipt&&view.receipt->command.revision>=4)r.version=2;}
        ++r.fetches;r.events.push_back("fetch"+std::to_string(r.version));
        j->valid=true;j->epoch=r.version;j->jobId="fixture.job-"+std::to_string(r.version);j->msg[0]=static_cast<uint8_t>(r.version);
        if(r.pauseInFetch&&r.fetches==1)r.requested=true;
        return true;
    }
    bool submit(const Job &,const Solution &,std::string *)override{++active->submits;active->events.push_back("submit");
        if(active->gate&&active->gateMode==6&&active->submits==1)active->waitOrdering=active->waitOrdering&&active->gate->snapshot().receipt->kind==WorkerReceiptKind::Accepted;
        return true;}
};
struct Algo:Algorithm {
    const char *name()const override{return "pearl-pow";}
    PearlRuntimeCapability pearlRuntimeCapability()const noexcept override{return PearlRuntimeCapability::CanonicalCudaPoolV1;}
    void configure(const RunOptions &)override{active->rawOrdering=active->rawOrdering&&!active->rawBoundaryOpen;active->configured=true;active->events.push_back("configure");
        if(active->gate&&active->gateMode==6)active->waitOrdering=active->waitOrdering&&active->gate->snapshot().receipt->kind==WorkerReceiptKind::WaitApplied;}
    size_t memoryBytes(const Job &)const override{return 0;}
    bool prepare(const Job &j)override{active->rawOrdering=active->rawOrdering&&!active->rawBoundaryOpen;active->events.push_back("prepare"+std::to_string(j.epoch));if(active->pauseInPrepare&&j.epoch==1)active->requested=true;return true;}
    uint64_t exactMacsPerUnit()const noexcept override{return 524288;}
    uint64_t completedSearchAttempts()const override{return active->completedAttempts;}
    bool search(const Job &j,uint64_t nonce,uint64_t count,std::vector<Solution> *out)override{
        auto &r=*active;r.rawOrdering=r.rawOrdering&&!r.rawBoundaryOpen;++r.searches;r.completedAttempts+=count;r.events.push_back("search"+std::to_string(j.epoch));Solution s;s.nonce=nonce;out->push_back(s);
        if(r.nativeClient){std::this_thread::sleep_for(std::chrono::milliseconds(5));if(r.searches>=1000)r.stop=1;return true;}
        if(r.gate){if(r.gateMode==6){const auto view=r.gate->snapshot();r.waitOrdering=r.waitOrdering&&view.receipt&&view.receipt->kind==WorkerReceiptKind::WaitApplied;
                if(view.receipt&&view.receipt->command.hostWaitMs)r.appliedWaits.push_back(*view.receipt->command.hostWaitMs);
                if(r.searches==1)r.gate->receive({binding(),3,1000,2000,WorkerDirective::Run,0});else r.stop=1;
            }else if(r.searches==1)r.gate->receive({binding(),3,1000,2000,WorkerDirective::Pause});else r.stop=1;return true;}
        if(r.pauseInPrepare||r.pauseInFetch||r.version==2||(r.uncontrolled&&r.searches==2))r.stop=1;else r.requested=true;
        return true;
    }
    bool verify(const Job &,const Solution &)const override{return true;}
    void release()override{}
    bool discardPearlLookahead()noexcept override{++active->drains;active->events.push_back("drain");return !(active->failDrain&&active->submits);}
    void pearlNextAttemptHint(bool enabled,uint64_t)noexcept override{active->enabledHint=active->enabledHint||enabled;}
};
struct Hook:RunLoopHook {
    int atLoopTop(bool completed)override{active->events.push_back(completed?"complete":"top");return 0;}
    void pauseChanged(bool paused)override{active->events.push_back(paused?"idle":"active");
        if(active->nativeClient&&paused){std::printf("NATIVEPAUSE %u %u\n",active->searches,active->submits);std::fflush(stdout);}}
};
RunOptions options(Rig &r){RunOptions o;o.workPause=&r.control;o.plain=true;o.batch=1;o.reportSeconds=3600;
    o.poolHost="fixture.invalid";o.poolPort=1;o.poolAccount="fixture.account";o.poolProofCompression=true;o.pearlPolicy={PearlNetwork::Mainnet,PearlTargetSemantics::BaseBeforePenalty};return o;}
size_t event(const Rig &r,const std::string &s){return static_cast<size_t>(std::find(r.events.begin(),r.events.end(),s)-r.events.begin());}
struct GateHook:Hook {
    void pauseChanged(bool paused)override{
        Hook::pauseChanged(paused);
        if(paused&&active->gateMode==0)*active->gateClock=2000;
    }
};
struct WorkSink:CompletedWorkSink {
    bool began=false,ended=false,ordered=true;std::vector<unsigned> waits;
    unsigned paused=0;
    bool begin(WorkLane lane,CompletedWorkTime)noexcept override{began=lane==WorkLane::User;return began;}
    bool completed(const CompletedWorkBatch &b)noexcept override{
        ordered=ordered&&active->searches==active->submits&&active->events.back()=="submit"&&b.workUnits==1&&b.attempts==1&&b.macsPerUnit==524288;
        waits.push_back(b.appliedHostWaitMs);return true;
    }
    bool end(bool clean,CompletedWorkTime)noexcept override{ended=clean;return clean;}
    bool boundary(const CompletedWorkBoundary &b)noexcept override{
        if(b.kind==CompletedWorkBoundaryKind::Paused){++paused;ordered=ordered&&active->drains&&active->searches==active->submits;}
        active->rawBoundaryOpen=b.kind!=CompletedWorkBoundaryKind::Leaving;return true;
    }
};
}
namespace om {JobSource *workPauseTestSource(){active->sourceMade=true;return new Source;}}
int main(int argc,char **argv){
    if(argc==6&&std::string(argv[1])=="--channel-fixture"&&std::string(argv[2])=="--worker-control-v1"){
        WorkerChannelHandles handles;WorkerControlClient client;
        if(!parseWorkerChannelHandles(argv[3],argv[4],argv[5],&handles)||!client.adopt(handles))return 90;
        Rig r;active=&r;r.nativeClient=&client;Algo algo;Hook hook;WorkSink raw;auto o=options(r);o.workPause=&client;o.loopHook=&hook;o.completedWork=&raw;
        const auto result=runMiner(&algo,o,"fixture.cpu",0,"fixture",&r.stop);client.stop();
        std::printf("NATIVEFINAL %u %u %u\n",r.searches,r.submits,r.version);std::fflush(stdout);
        if(r.searches!=r.submits||r.enabledHint||!r.rawOrdering||!raw.ordered||!raw.began||(!raw.ended&&result!=kRunPauseFault))return 91;
        return result==kRunPauseFault?8:result;
    }
    for(unsigned mode=0;mode<9;++mode){Rig r;active=&r;Algo algo;Hook hook;auto o=options(r);o.loopHook=&hook;
        r.stopInPause=mode==1||mode==4;r.failDrain=mode==2;r.pauseInPrepare=mode==3;r.pauseInFetch=mode==5;r.requested=mode==4||mode==6;
        if(mode==7)o.workWaitMs=5;
        if(mode==8)r.stop=1;
        const int result=runMiner(&algo,o,"fixture.cpu",0,"fixture",&r.stop);
        CHECK(result==(mode==2?kRunPauseFault:0)&&!r.enabledHint&&r.pauseOrdering);
        if(mode==4||mode==8){CHECK(!r.configured&&!r.sourceMade&&!r.searches&&r.resumes==0);continue;}
        CHECK(r.configured&&r.sourceMade&&r.submits==r.searches);
        if(mode==0||mode==7){CHECK(r.searches==2&&r.resumes==1);CHECK(event(r,"submit")<event(r,"complete")&&event(r,"complete")<event(r,"paused")&&event(r,"resume")<event(r,"fetch2")&&event(r,"fetch2")<event(r,"search2"));}
        if(mode==1||mode==2){CHECK(r.searches==1&&r.resumes==0);if(mode==2)CHECK(event(r,"paused")==r.events.size());}
        if(mode==3||mode==5){CHECK(r.searches==1&&r.resumes==1&&event(r,"search1")==r.events.size());CHECK(event(r,"resume")<event(r,"fetch2")&&event(r,"fetch2")<event(r,"search2"));}
        if(mode==6)CHECK(r.searches==1&&r.resumes==1&&event(r,"resume")<event(r,"configure"));
    }
    {Rig r;active=&r;r.uncontrolled=true;Algo algo;Hook hook;auto o=options(r);o.workPause=nullptr;o.loopHook=&hook;
        CHECK(runMiner(&algo,o,"fixture.cpu",0,"fixture",&r.stop)==0);
        CHECK(r.searches==2&&r.submits==2&&!r.resumes&&r.enabledHint&&event(r,"idle")==r.events.size());}
    for(unsigned mode=0;mode<6;++mode){Rig r;active=&r;Algo algo;auto o=options(r);
        if(mode==0)o.bench=true;
        if(mode==1)o.formalBenchmark.enabled=true;
        if(mode==2)o.pearlAcceptedQuota=1;
        if(mode==3)o.pearlSustainedSeconds=60;
        if(mode==4)o.memOffsetMhz=1;
        if(mode==5)o.pearlProduction=true;
        CHECK(runMiner(&algo,o,"fixture.cpu",0,"fixture",&r.stop)==kRunPauseFault&&!r.configured&&!r.sourceMade&&!r.searches);}
    for(unsigned mode=0;mode<3;++mode){Rig r;active=&r;uint64_t now=1000;WorkerControlGate gate(binding(),[&]{return now;});
        r.gate=&gate;r.gateClock=&now;r.gateMode=mode;Algo algo;GateHook hook;auto o=options(r);o.workPause=&gate;o.loopHook=&hook;
        CHECK(gate.receive({binding(),1,1000,2000,WorkerDirective::Pause})==WorkerCommandResult::Accepted);
        if(mode==1)gate.requestStop();
        if(mode==2)gate.fail();
        CHECK(runMiner(&algo,o,"fixture.cpu",0,"fixture",&r.stop)==(mode==1?0:kRunPauseFault));
        CHECK(!r.configured&&!r.sourceMade&&!r.searches&&gate.snapshot().state==(mode==0?WorkerControlState::Expired:mode==1?WorkerControlState::Stopped:WorkerControlState::Faulted));}
    for(unsigned mode=3;mode<6;++mode){Rig r;active=&r;WorkerControlGate gate(binding(),[]{return 1000;});r.gate=&gate;r.gateMode=mode;
        Algo algo;GateHook hook;auto o=options(r);o.workPause=&gate;o.loopHook=&hook;std::atomic<bool> sent{false};
        CHECK(gate.receive({binding(),1,1000,2000,WorkerDirective::Pause})==WorkerCommandResult::Accepted);
        std::thread sender([&]{const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);unsigned stage=0;
            while(std::chrono::steady_clock::now()<until){const auto view=gate.snapshot();
                if(view.receipt&&view.receipt->kind==WorkerReceiptKind::Paused&&view.receipt->command.revision==(stage?3u:1u)){
                    if(!stage){if(gate.receive({binding(),2,1000,2000,WorkerDirective::Run})!=WorkerCommandResult::Accepted)break;++stage;}
                    else{if(mode==3)sent=gate.receive({binding(),4,1000,2000,WorkerDirective::Run})==WorkerCommandResult::Accepted;
                        else{if(mode==4)gate.requestStop();else gate.fail();sent=true;}return;}}
                std::this_thread::sleep_for(std::chrono::milliseconds(1));}gate.fail();});
        const auto result=runMiner(&algo,o,"fixture.cpu",0,"fixture",&r.stop);sender.join();
        CHECK(sent&&result==(mode==5?kRunPauseFault:0)&&r.configured&&r.sourceMade&&r.submits==r.searches&&!r.enabledHint);
        CHECK(r.searches==(mode==3?2u:1u)&&event(r,"submit")<event(r,"complete"));
        if(mode==3)CHECK(event(r,"fetch2")<event(r,"search2"));
    }
    {Rig r;active=&r;WorkerControlGate gate(binding(),[]{return 1000;});r.gate=&gate;r.gateMode=6;
        Algo algo;Hook hook;WorkSink work;auto o=options(r);o.workPause=&gate;o.loopHook=&hook;o.completedWork=&work;std::atomic<bool> sent{false};
        CHECK(gate.receive({binding(),1,1000,2000,WorkerDirective::Pause})==WorkerCommandResult::Accepted);
        std::thread sender([&]{const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(std::chrono::steady_clock::now()<until){const auto view=gate.snapshot();if(view.receipt&&view.receipt->kind==WorkerReceiptKind::Paused){
                sent=gate.receive({binding(),2,1000,2000,WorkerDirective::Run,10})==WorkerCommandResult::Accepted;return;}
                std::this_thread::sleep_for(std::chrono::milliseconds(1));}gate.fail();});
        const auto result=runMiner(&algo,o,"fixture.cpu",0,"fixture",&r.stop);sender.join();
        CHECK(sent&&result==0&&r.searches==2&&r.submits==2&&!r.enabledHint);
        CHECK(r.waitOrdering&&r.appliedWaits==std::vector<unsigned>({10,0}));
        CHECK(work.began&&work.ended&&work.ordered&&work.waits==r.appliedWaits);
        CHECK(work.paused>0&&r.rawOrdering);}
    return finishTests("shared run-loop cooperative pause");
}
