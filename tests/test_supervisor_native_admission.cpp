// Actual supervisor with a memory-only synthetic fleet and unavailable telemetry; no child/device/socket is launched.
#include "cpu_telemetry_fixture.h"
#include "product/supervisor.h"
#include "product_test_util.h"
using namespace om;
namespace {
enum class Phase {Start,Tick,Join};
struct State {Phase phase;NativeProcessAdmission fault;unsigned factories=0,ticks=0,joins=0;bool raised=false,revoked=false;explicit State(Phase p):phase(p){}};
void transient(State &s){if(s.raised)return;s.raised=true;CHECK(s.fault.begin());s.fault.markUnresolved();s.fault.settled();}
class Reader final:public DisplaySampleReader {public:DisplaySampleReading read(size_t,const std::atomic<bool> &)override{return {};}};
class Fleet final:public WorkerFleet {
    State &s_;mutable unsigned snapshots_=0;
public:
    explicit Fleet(State &s):s_(s){}
    bool start(unsigned slots,GameGuardTime)override{if(s_.phase==Phase::Start)transient(s_);return slots==1;}
    bool launch(unsigned,std::filesystem::path,std::vector<std::string>,unsigned,GameGuardTime)override{return true;}
    void tick(GameGuardTime,const WorkerFleetEvidence &)override{++s_.ticks;if(s_.phase==Phase::Tick)transient(s_);}
    WorkerFleetState state()const override{return s_.revoked?WorkerFleetState::Stopped:WorkerFleetState::Active;}
    WorkerFleetSnapshot snapshot(unsigned)const override{WorkerFleetSnapshot v;v.requested=true;v.done=s_.revoked||++snapshots_>=3;v.reaped=v.done;v.exitCode=0;return v;}
    std::vector<std::string> takeLines(unsigned)override{return {};}
    void requestStop()override{s_.revoked=true;}
    void stop()override{CHECK(s_.revoked);++s_.joins;if(s_.phase==Phase::Join)transient(s_);}
};
SupervisorOptions options(State &s){
    SupervisorOptions o;o.exe="SYNTHETIC-NEVER-EXECUTED";o.dashboard=false;o.drainMs=0;o.apiPort=0;
    WorkerSeed seed;seed.index=0;seed.uuid="SYNTHETIC-DEVICE-NOT-DISCOVERED";seed.name="Synthetic CPU fleet";o.seeds={seed};
    o.displaySampleFactory=[](const std::vector<DisplaySampleTarget> &)->std::unique_ptr<DisplaySampleReader>{return std::make_unique<Reader>();};
    o.workerFleetFactory=[&s](const SessionIntent &,SessionToken)->std::unique_ptr<WorkerFleet>{++s.factories;return std::make_unique<Fleet>(s);};return o;
}
}
int main(){
    CHECK(platformInit());
    for(auto phase:{Phase::Start,Phase::Tick,Phase::Join}){
        State state{phase};Supervisor supervisor(options(state));CHECK(supervisor.run()==kExitFaultLatched);
        CHECK(state.factories==1&&state.joins==1&&state.revoked&&state.raised&&!nativeProcessUnresolved());
        if(phase==Phase::Tick)CHECK(state.ticks==1); // do not wait for another control turn to revoke the original fault epoch
    }
    {State state{Phase::Tick};CHECK(state.fault.begin());state.fault.markUnresolved();
        Supervisor supervisor(options(state));CHECK(supervisor.run()==kExitFaultLatched&&state.factories==0);state.fault.settled();}
    {State state{Phase::Tick};volatile sig_atomic_t stop=1;CHECK(state.fault.begin());state.fault.markUnresolved();
        auto o=options(state);o.stop=&stop;Supervisor supervisor(std::move(o));CHECK(supervisor.run()==kExitOk&&state.factories==0);state.fault.settled();}
    platformShutdown();return finishTests("actual supervisor native-fault epoch and stop priority (synthetic fleet only)");
}
