// Actual shared run loop, CPU algorithm and bounded in-memory source only. No telemetry, socket, device or Pearl proof qualification.
#include "core/run.h"
#include "product_test_util.h"
#include <algorithm>
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
    unsigned searches=0,submits=0,drains=0,resumes=0,fetches=0,version=1;
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
struct Source:JobSource {
    const char *describe()const override{return "fixture.workPauseTestSource";}
    bool fetch(Job *j)override{
        auto &r=*active;++r.fetches;r.events.push_back("fetch"+std::to_string(r.version));
        j->valid=true;j->epoch=r.version;j->jobId="fixture.job-"+std::to_string(r.version);j->msg[0]=static_cast<uint8_t>(r.version);
        if(r.pauseInFetch&&r.fetches==1)r.requested=true;
        return true;
    }
    bool submit(const Job &,const Solution &,std::string *)override{++active->submits;active->events.push_back("submit");return true;}
};
struct Algo:Algorithm {
    const char *name()const override{return "pearl-pow";}
    PearlRuntimeCapability pearlRuntimeCapability()const noexcept override{return PearlRuntimeCapability::CanonicalCudaPoolV1;}
    void configure(const RunOptions &)override{active->configured=true;active->events.push_back("configure");}
    size_t memoryBytes(const Job &)const override{return 0;}
    bool prepare(const Job &j)override{active->events.push_back("prepare"+std::to_string(j.epoch));if(active->pauseInPrepare&&j.epoch==1)active->requested=true;return true;}
    bool search(const Job &j,uint64_t nonce,uint64_t,std::vector<Solution> *out)override{
        auto &r=*active;++r.searches;r.events.push_back("search"+std::to_string(j.epoch));Solution s;s.nonce=nonce;out->push_back(s);
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
    void pauseChanged(bool paused)override{active->events.push_back(paused?"idle":"active");}
};
RunOptions options(Rig &r){RunOptions o;o.workPause=&r.control;o.plain=true;o.batch=1;o.reportSeconds=3600;
    o.poolHost="fixture.invalid";o.poolPort=1;o.poolAccount="fixture.account";o.poolProofCompression=true;o.pearlPolicy={PearlNetwork::Mainnet,PearlTargetSemantics::BaseBeforePenalty};return o;}
size_t event(const Rig &r,const std::string &s){return static_cast<size_t>(std::find(r.events.begin(),r.events.end(),s)-r.events.begin());}
}
namespace om {JobSource *workPauseTestSource(){active->sourceMade=true;return new Source;}}
int main(){
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
    return finishTests("shared run-loop cooperative pause");
}
