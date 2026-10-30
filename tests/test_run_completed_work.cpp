// Actual shared run loop and exact source with CPU algorithm/in-memory jobs. No telemetry, network or GPU.
#include "core/run.h"
#include "product/completed_work_capture.h"
#include "product_test_util.h"
#include <stdexcept>
using namespace om;
namespace {
struct Rig;Rig *active=nullptr;
struct Control:RunPauseControl {
    bool requested=false,held=false;
    bool pauseRequested()const noexcept override{return requested;}
    void pauseBoundary(bool p)noexcept override{held=p;}
};
struct Rig {
    volatile sig_atomic_t stop=0;unsigned searches=0,submitted=0,prepared=0,configured=0;
    bool failSearch=false,throwSearch=false,failPrepare=false,failDrain=false,invalid=false,shortCompletion=false,changedFactor=false,changedUnits=false;
    bool stoppedBatch=true,withSolution=true;uint64_t factor=524288,units=3,counter=0;Control control;
};
struct Source:JobSource {
    const char *describe()const override{return "fixture.completed-work";}
    bool fetch(Job *j)override{j->valid=true;j->epoch=1;j->jobId="fixture.job";return true;}
    bool submit(const Job &,const Solution &,std::string *)override{++active->submitted;return true;}
};
struct Algo:Algorithm {
    const char *name()const override{return "pearl-pow";}
    PearlRuntimeCapability pearlRuntimeCapability()const noexcept override{return PearlRuntimeCapability::CanonicalCudaPoolV1;}
    void configure(const RunOptions &)override{++active->configured;}
    size_t memoryBytes(const Job &)const override{return 0;}
    bool prepare(const Job &)override{++active->prepared;return !active->failPrepare;}
    uint64_t workUnitsPerNonce()const override{return active->units;}
    uint64_t exactMacsPerUnit()const noexcept override{return active->factor;}
    double macsPerUnit()const override{return 0.125;} // intentionally unrelated display value
    uint64_t completedSearchAttempts()const override{return active->counter;}
    bool search(const Job &,uint64_t,uint64_t work,std::vector<Solution> *out)override{auto &r=*active;++r.searches;
        if(r.throwSearch)throw std::runtime_error("fixture");
        if(r.failSearch)return false;
        r.counter+=r.shortCompletion?0:work/r.units;
        if(r.changedFactor)++r.factor;
        if(r.changedUnits)++r.units;
        if(r.withSolution)out->push_back({});
        if(r.stoppedBatch)r.stop=1;
        return true;
    }
    bool verify(const Job &,const Solution &)const override{return !active->invalid;}
    bool discardPearlLookahead()noexcept override{return !(active->failDrain&&active->searches);}
    void release()override{}
};
struct Sink:CompletedWorkSink {
    SessionIntent intent;SessionToken token=intent.explicitStart();CompletedWorkCapture capture{{true,8192,std::string(32,'1')},intent,token,1};
    unsigned completeCalls=0;bool refuse=false,ordered=true;
    unsigned pausedCalls=0;bool resumePause=false,refuseBoundary=false;
    std::optional<CompletedWorkInterval> originalPause;
    Sink(){CHECK(capture.start(completedWorkNow()));}
    bool begin(WorkLane lane,CompletedWorkTime t)noexcept override{return capture.begin(lane,t);}
    bool completed(const CompletedWorkBatch &b)noexcept override{++completeCalls;ordered=ordered&&active->searches==1&&(!active->withSolution||active->submitted==1||active->invalid);
        return !refuse&&capture.completed(b);}
    bool boundary(const CompletedWorkBoundary &b)noexcept override{
        if(refuseBoundary)return false;
        const bool ok=capture.boundary(b);
        if(b.kind==CompletedWorkBoundaryKind::Paused){++pausedCalls;ordered=ordered&&active->control.held&&!active->configured&&!active->searches;
            if(resumePause&&pausedCalls>=3)active->control.requested=false;}
        if(capture.snapshot().lastInterval&&capture.snapshot().lastInterval->kind==CompletedWorkBoundaryKind::Paused)originalPause=capture.snapshot().lastInterval;
        return ok;
    }
    bool end(bool clean,CompletedWorkTime t)noexcept override{return capture.end(clean,t);}
};
RunOptions options(Rig &r,Sink &sink){RunOptions o;o.workPause=&r.control;o.completedWork=&sink;o.plain=true;o.batch=6;o.reportSeconds=3600;
    o.poolHost="fixture.invalid";o.poolPort=1;o.poolAccount="fixture.account";o.poolProofCompression=true;
    o.pearlPolicy={PearlNetwork::Mainnet,PearlTargetSemantics::BaseBeforePenalty};return o;}
int run(Rig &r,RunOptions o){active=&r;Algo algo;return runMiner(&algo,o,"fixture.cpu",0,"fixture",&r.stop);}
struct FeeHook:RunLoopHook {int atLoopTop(bool complete)override{return complete?kRunFeeWindow:0;}};
void actualBoundary(){Sink sink;Rig user;auto o=options(user,sink);o.workWaitMs=10;CHECK(run(user,o)==0);
    const auto original=sink.capture.snapshot().lastBatch;CHECK(sink.ordered&&sink.completeCalls==1&&original&&original->batch.appliedHostWaitMs==10);
    CHECK(user.searches==1&&user.submitted==1&&sink.capture.snapshot().userTmacs.text()=="0.000003145728");
    Rig donation;o=options(donation,sink);o.workLane=WorkLane::Donation;CHECK(run(donation,o)==0);
    CHECK(sink.capture.snapshot().userTmacs.text()=="0.000003145728"&&sink.capture.snapshot().donationTmacs.text()=="0.000003145728");
    CHECK(original->lane==WorkLane::User&&sink.capture.snapshot().lastBatch->lane==WorkLane::Donation&&sink.capture.snapshot().batches==2);
    Rig feeSwitch;feeSwitch.stoppedBatch=false;FeeHook hook;o=options(feeSwitch,sink);o.loopHook=&hook;
    CHECK(run(feeSwitch,o)==kRunFeeWindow&&feeSwitch.searches==1&&sink.capture.snapshot().state==CompletedWorkState::Ready);
    Rig resumed;resumed.withSolution=false;CHECK(run(resumed,options(resumed,sink))==0&&resumed.submitted==0);
    CHECK(sink.capture.snapshot().batches==4&&sink.capture.snapshot().attempts==8&&sink.capture.snapshot().userTmacs.text()=="0.000009437184");
    {Sink initial;Rig r;r.control.requested=true;initial.resumePause=true;
        CHECK(run(r,options(r,initial))==0&&initial.ordered&&initial.pausedCalls>=3&&initial.originalPause);
        if(initial.originalPause){const auto &p=*initial.originalPause;CHECK(p.userTmacs.zero()&&p.donationTmacs.zero()&&p.batches==0&&p.run==1&&
            p.end.monotonicMs<=initial.capture.snapshot().lastBatch->batch.observed.monotonicMs);}}
    {Sink refused;Rig r;refused.refuseBoundary=true;CHECK(run(r,options(r,refused))==kRunPauseFault&&!r.configured&&!r.searches&&
        refused.capture.snapshot().state==CompletedWorkState::Faulted);}
}
void failures(){
    for(unsigned mode=0;mode<9;++mode){Sink sink;Rig r;
        if(mode==0)r.factor=0;
        if(mode==1)r.failSearch=true;
        if(mode==2)r.shortCompletion=true;
        if(mode==3)r.changedFactor=true;
        if(mode==4)r.changedUnits=true;
        if(mode==5)r.invalid=true;
        if(mode==6)r.failDrain=true;
        if(mode==7)r.failPrepare=true;
        if(mode==8)sink.refuse=true;
        CHECK(run(r,options(r,sink))==(mode==1||mode==6||mode==7?1:kRunPauseFault));
        CHECK(sink.capture.snapshot().state==CompletedWorkState::Faulted);
        CHECK(sink.capture.snapshot().batches==(mode==6?1u:0u));
        const auto searches=r.searches;r.stop=0;CHECK(run(r,options(r,sink))==kRunPauseFault&&r.searches==searches);
    }
    {Sink sink;Rig r;r.throwSearch=true;bool threw=false;try{run(r,options(r,sink));}catch(const std::runtime_error &){threw=true;}
        CHECK(threw&&sink.capture.snapshot().state==CompletedWorkState::Faulted&&sink.capture.snapshot().batches==0);}
    for(unsigned mode=0;mode<4;++mode){Sink sink;Rig r;auto o=options(r,sink);
        if(mode==0)o.workPause=nullptr;
        if(mode==1)o.workLane=static_cast<WorkLane>(99);
        if(mode==2)o.bench=true;
        if(mode==3)o.memOffsetMhz=1;
        CHECK(run(r,o)==kRunPauseFault&&!r.configured&&!r.searches&&sink.capture.snapshot().runs==0);}
    {Sink sink;Rig r;auto o=options(r,sink);o.completedWork=nullptr;r.factor=0;
        CHECK(run(r,o)==0&&r.searches==1&&sink.capture.snapshot().runs==0);} // source stays opt-in
}
}
namespace om {JobSource *workPauseTestSource(){return new Source;}}
int main(){actualBoundary();failures();return finishTests("shared run-loop completed work source");}
