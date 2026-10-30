// Self-owned CPU workers and isolated synthetic detection replies. No real game, GPU work, provider or installed miner.
#include "cpu_telemetry_fixture.h"
#include "product/supervisor.h"
#include "product/game_worker_session.h"
#include "product/worker_process.h"
#include "product_test_util.h"
#include <thread>
using namespace om;
namespace {
void sleep(unsigned ms=5){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
GameGuardPolicy gamePolicy(GameIntent mode){GameGuardPolicy p;p.mode=mode;p.exitDebounceMs=100;return p;}
GameDetectionPolicy detectionPolicy(){GameDetectionPolicy p;p.enabled=true;p.pollMs=100;return p;}
GameProcessTarget target(const std::string &mode){return {"fixture.game",mode,GameProcessAdapter::WindowsSessionImage};}
GameProfitBinding binding(){GameProfitBinding b;b.gameKey="fixture.game";return b;}
GameWorkerPolicy workerPolicy(){GameWorkerPolicy p;p.maximumIncarnations=4;p.commandLeaseMs=600;p.maximumSafetyAgeMs=800;return p;}
std::filesystem::path identity,detector,exe;
int child(int argc,char **argv){
    const std::string mode=argv[2];WorkerChannelHandles handles;int port=0;bool found=false;
    for(int i=3;i<argc;++i){const std::string a=argv[i];
        if(a=="--api-port"&&i+1<argc)port=std::atoi(argv[i+1]);
        if(a=="--worker-control-v1"&&i+3<argc)found=parseWorkerChannelHandles(argv[i+1],argv[i+2],argv[i+3],&handles);}
    if(!found)return 90;
    if(mode=="bad-receipt"||mode=="partial-receipt"){
        const char *text=mode=="bad-receipt"?"bad\n":"CMWA1 partial";
#if defined(_WIN32)
        DWORD written=0;WriteFile(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handles.output)),text,static_cast<DWORD>(std::strlen(text)),&written,nullptr);
#else
        if(::write(static_cast<int>(handles.output),text,std::strlen(text))<0)return 93;
#endif
        return 1;
    }
    WorkerControlClient client;if(!client.adopt(handles))return 91;
    if(!platformInit())return 92;
    StatsServer server;std::string error;
    if(port&&!server.start("127.0.0.1",port,[]{StatsSnapshot s;s.state="running";s.poolState="connecting";return statsToJson(s);},"{}",&error))return 94;
    const auto began=workerControlNowMs();uint64_t firstRun=0;int code=8;bool held=false;
    while(workerControlNowMs()-began<10000){
        if(mode=="exit-paused"||mode=="exit-clean"||mode=="exit-fault"){
            const auto state=client.requestState();
            if(state==WorkControlRequest::Pause){client.pauseBoundary(true);client.pauseHeld();}
            if(state==WorkControlRequest::Stop){code=0;break;}
            if(state==WorkControlRequest::Fault)break;
            if(workerControlNowMs()-began>=200){code=mode=="exit-paused"?1:mode=="exit-clean"?0:8;break;}
        }else{
            const auto result=pauseAtWorkBoundary(client,[]{return false;},[]{return true;},[&](bool paused){held=paused;
                if(paused){std::puts("FLEETPAUSE");std::fflush(stdout);}},workerControlNowMs,[](unsigned ms){sleep(ms);});
            if(result!=WorkPauseResult::Ready){code=result==WorkPauseResult::Stopped?0:8;break;}
            if(held)return 95;
            if(!firstRun){firstRun=workerControlNowMs();std::puts("FLEETRUN");std::fflush(stdout);}
            const auto view=client.snapshot();
            const bool error=mode=="exit-error"||(mode=="restart-once"&&view.receipt&&view.receipt->command.binding.workerGeneration==1);
            if(error&&workerControlNowMs()-firstRun>=150){code=1;break;}
        }
        sleep();
    }
    server.stop();platformShutdown();client.stop();return code;
}
struct Rig {
    SessionIntent intent;SessionToken token=intent.explicitStart();GameWorkerPolicy policy=workerPolicy();
    uint64_t revision=0;std::unique_ptr<GameWorkerSession> session;std::string lines;
    Rig(GameIntent mode=GameIntent::Off,const std::string &detected="fixture-absent",unsigned capacity=4){
        policy.maximumIncarnations=capacity;
        session=std::make_unique<GameWorkerSession>(identity,detector,target(detected),detectionPolicy(),gamePolicy(mode),binding(),policy,intent,token);
    }
    WorkerFleetEvidence fresh(GameGuardTime now){WorkerFleetEvidence e;e.safety={token.epoch,++revision,now,{true,true,false}};return e;}
    bool start(const std::string &mode="normal"){
        const auto now=gameDetectionTime();return session->start(1,now)&&session->launch(0,exe,{"--fleet-child",mode},300,now);
    }
    void step(const WorkerFleetEvidence *frozen=nullptr){const auto now=gameDetectionTime();session->tick(now,frozen?*frozen:fresh(now));
        for(const auto &line:session->takeLines(0))lines+=line+'\n';}
    bool until(const std::function<bool()> &done,const WorkerFleetEvidence *frozen=nullptr,unsigned ms=4000){
        const auto end=workerControlNowMs()+ms;do{step(frozen);if(done())return true;sleep();}while(workerControlNowMs()<end);return false;
    }
    WorkerFleetSnapshot view()const{return session->snapshot(0);}
};
void lifecycle(){
    {Rig r;r.session->requestStop();CHECK(!r.start()&&r.session->state()==WorkerFleetState::Stopped);}
    {Rig r;CHECK(r.start());r.session->requestStop();r.session->stop();CHECK(r.view().done&&r.view().revoked&&!r.view().retryable);CHECK(!r.session->launch(0,exe,{"--fleet-child","normal"},300,gameDetectionTime()));}
    {Rig r;CHECK(r.start());WorkerFleetEvidence missing;
        CHECK(r.until([&]{return r.view().paused;},&missing));CHECK(r.lines.find("FLEETRUN")==r.lines.npos);
        CHECK(r.until([&]{return r.lines.find("FLEETRUN")!=r.lines.npos;}));
        CHECK(!r.view().pauseRequested&&!r.view().revoked&&r.view().commandRevision>=2);
        auto cool=r.fresh(gameDetectionTime());cool.safety.safety.thermalSafe=false;
        CHECK(r.until([&]{return r.view().paused;},&cool));CHECK(r.view().pauseRequested&&!r.view().done);
        CHECK(r.until([&]{return !r.view().pauseRequested&&r.view().commandRevision>=4;}));
        r.session->requestStop();CHECK(r.view().revoked);r.session->stop();CHECK(r.view().done&&r.view().reaped&&r.view().exitCode==0);
        const auto keyless=r.lines;CHECK(keyless.find("CMWC1")==keyless.npos&&keyless.find("CMWA1")==keyless.npos);
        r.intent.stop();r.intent.explicitStart();r.step();CHECK(r.session->state()==WorkerFleetState::Stopped&&!r.session->start(1,gameDetectionTime()));}
    {Rig r;CHECK(r.start());auto frozen=r.fresh(gameDetectionTime());
        CHECK(r.until([&]{return r.lines.find("FLEETRUN")!=r.lines.npos;},&frozen));
        CHECK(r.view().commandDeadlineMonoMs<=frozen.safety.observed.monotonicMs+r.policy.maximumSafetyAgeMs);
        CHECK(r.until([&]{return r.view().paused;},&frozen));CHECK(!r.view().done&&r.session->state()==WorkerFleetState::Active);
        const auto revision=r.view().commandRevision;
        CHECK(r.until([&]{return r.view().commandRevision>revision+1;},&frozen));CHECK(r.view().pauseRequested&&!r.view().done);}
    {Rig r;CHECK(r.start());CHECK(r.until([&]{return r.lines.find("FLEETRUN")!=r.lines.npos;}));
        sleep(r.policy.commandLeaseMs+100);CHECK(r.until([&]{return r.view().done;}));
        CHECK(r.view().revoked&&!r.view().retryable&&r.view().exitCode==8&&!r.session->launch(0,exe,{"--fleet-child","normal"},300,gameDetectionTime()));}
    {Rig r;CHECK(r.start());auto proof=r.fresh(gameDetectionTime());r.step(&proof);proof.safety.safety.thermalSafe=false;r.step(&proof);
        CHECK(r.session->state()==WorkerFleetState::Faulted&&r.view().revoked);r.session->stop();}
    {Rig r;CHECK(r.start());WorkerFleetEvidence missing;CHECK(r.until([&]{return r.view().paused;},&missing));
        for(unsigned invalid=0;invalid<4;++invalid){auto e=r.fresh(gameDetectionTime());
            if(invalid==0)++e.safety.epoch;
            else if(invalid==1)e.safety.observed.utcMs+=100000;
            else if(invalid==2)e.safety.observed.monotonicMs+=100000;
            else e.safety.revision=0;
            r.step(&e);CHECK(r.session->state()==WorkerFleetState::Active&&r.view().pauseRequested&&r.lines.find("FLEETRUN")==r.lines.npos);
        }
        auto proof=r.fresh(gameDetectionTime());r.step(&proof);
        ++proof.safety.revision;--proof.safety.observed.utcMs;--proof.safety.observed.monotonicMs;r.step(&proof);
        CHECK(r.session->state()==WorkerFleetState::Faulted&&r.view().revoked);
    }
    {Rig r;CHECK(r.start());CHECK(r.until([&]{return r.lines.find("FLEETRUN")!=r.lines.npos;}));r.intent.stop();r.step();
        CHECK(r.session->state()==WorkerFleetState::Stopped&&r.view().revoked);r.session->stop();}
}
void gameAndRestarts(){
    {Rig r(GameIntent::Pause,"fixture-present");CHECK(r.start());CHECK(r.until([&]{return r.view().paused&&r.session->gameSnapshot().state==GameGuardState::PausedPolicy;}));
        CHECK(r.lines.find("FLEETRUN")==r.lines.npos&&!r.session->gameSnapshot().episodeKey.empty());}
    {Rig r(GameIntent::Coexist,"fixture-present");CHECK(r.start());CHECK(r.until([&]{return r.view().paused&&r.session->gameSnapshot().state==GameGuardState::NeedsQualification;}));
        CHECK(r.lines.find("FLEETRUN")==r.lines.npos);}
    {Rig r(GameIntent::Pause,"fixture-incomplete");CHECK(r.start());CHECK(r.until([&]{return r.view().paused;}));CHECK(r.lines.find("FLEETRUN")==r.lines.npos);}
    {Rig r(GameIntent::Pause,"fixture-sequence");CHECK(r.start());CHECK(r.until([&]{return r.session->gameSnapshot().state==GameGuardState::PausedPolicy;}));
        CHECK(r.until([&]{return r.lines.find("FLEETRUN")!=r.lines.npos;}));CHECK(r.session->gameSnapshot().episodeKey.empty());}
    {Rig r(GameIntent::Pause,"fixture-present");CHECK(r.start("exit-paused"));
        CHECK(r.until([&]{return r.view().done;}));const auto episode=r.session->gameSnapshot().episodeKey;
        CHECK(r.view().exitCode==1&&r.view().reaped&&r.view().retryable&&!episode.empty());
        CHECK(!r.session->launch(0,exe,{"--fleet-child","normal"},300,gameDetectionTime())); // frozen launch settings
        CHECK(r.session->launch(0,exe,{"--fleet-child","exit-paused"},300,gameDetectionTime()));
        CHECK(r.until([&]{return r.view().done&&r.view().generation==2;}));
        CHECK(r.view().retryable&&r.session->gameSnapshot().episodeKey==episode&&r.lines.find("FLEETRUN")==r.lines.npos);}
    {Rig r(GameIntent::Off,"fixture-absent",1);CHECK(r.start("exit-error"));CHECK(r.until([&]{return r.view().done;}));
        CHECK(r.view().retryable&&r.view().exitCode==1);CHECK(r.session->launch(0,exe,{"--fleet-child","exit-error"},300,gameDetectionTime()));r.step();
        CHECK(r.session->state()==WorkerFleetState::Faulted&&r.view().revoked);}
}
void exitCases(){
    for(const auto *mode:{"exit-clean","exit-fault","bad-receipt","partial-receipt"}){Rig r;CHECK(r.start(mode));CHECK(r.until([&]{return r.view().done;}));
        CHECK(r.view().reaped&&r.view().revoked&&!r.view().retryable);
        CHECK_MSG(mode,r.view().exitCode==(std::string(mode)=="exit-clean"?0:8));}
    {Rig r;CHECK(r.start("exit-error"));CHECK(r.until([&]{return r.view().done&&r.view().reaped;}));
        CHECK(r.session->launch(0,exe,{"--fleet-child","exit-error"},300,gameDetectionTime()));
        r.session->requestStop();r.session->stop();const auto v=r.view();
        CHECK(v.generation==2&&v.done&&v.revoked&&!v.retryable&&v.exitCode==0);
        CHECK(!v.reaped); // a cancelled queued incarnation must not borrow its predecessor's reaping proof
    }
}
void deadlines(){
    SessionIntent intent;const auto token=intent.explicitStart();
    {GameRuntime runtime(detector,target("fixture-absent"),detectionPolicy(),gamePolicy(GameIntent::Off),binding(),intent,token);
        auto now=gameDetectionTime();CHECK(runtime.start(now)&&runtime.runDeadline(now,now.monotonicMs+1000)==0);
        runtime.tick(now,{true,true,false});CHECK(runtime.runDeadline(now,now.monotonicMs+250)==now.monotonicMs+250);
        auto other=now;++other.monotonicMs;CHECK(runtime.runDeadline(other,other.monotonicMs+1000)==0);runtime.requestStop();CHECK(runtime.runDeadline(now,now.monotonicMs+1000)==0);}
    {auto d=detectionPolicy();d.pollMs=3000;auto p=gamePolicy(GameIntent::Pause);p.maximumDetectionGapMs=400;
        GameRuntime runtime(detector,target("fixture-absent"),d,p,binding(),intent,token);CHECK(runtime.start(gameDetectionTime()));
        GameGuardTime now;const auto end=workerControlNowMs()+2000;
        do{now=gameDetectionTime();runtime.tick(now,{true,true,false});if(runtime.snapshot().state==GameGuardState::NormalEligible)break;sleep();}while(workerControlNowMs()<end);
        const auto observed=runtime.detection()->detection.observed;
        const auto remaining=400-(std::max)(static_cast<uint64_t>(now.utcMs-observed.utcMs),now.monotonicMs-observed.monotonicMs);
        CHECK(runtime.runDeadline(now,now.monotonicMs+2000)==now.monotonicMs+remaining);
        sleep(410);now=gameDetectionTime();runtime.tick(now,{true,true,false});CHECK(runtime.runDeadline(now,now.monotonicMs+2000)==0);}
}
void supervisor(){
    {volatile sig_atomic_t stop=1;unsigned calls=0;SupervisorOptions o;o.stop=&stop;o.workerFleetFactory=[&](const SessionIntent &,SessionToken)->std::unique_ptr<WorkerFleet>{++calls;return {};};
        Supervisor s(std::move(o));CHECK(s.run()==0&&calls==0);}
    for(bool retry:{false,true}){
        volatile sig_atomic_t stop=0;unsigned factories=0;uint64_t revision=0,finishAfter=0;GameWorkerSession *borrowed=nullptr;
        SupervisorOptions o;o.exe=exe.u8string();o.userArgs={"--fleet-child",retry?"restart-once":"normal"};o.apiEnabled=false;o.stop=&stop;o.drainMs=100;
        WorkerSeed seed;seed.index=0;seed.uuid="fixture.no-device";seed.name="fixture.cpu";o.seeds={seed};
        StatsServer port;std::string error;CHECK(port.start("127.0.0.1",0,[]{return "{}";},"{}",&error));o.workerPortBase=port.port();port.stop();
        o.workerFleetFactory=[&](const SessionIntent &i,SessionToken t){++factories;
            auto policy=workerPolicy();policy.commandLeaseMs=5000;policy.maximumSafetyAgeMs=5000; // explicit fixture validity; short-source bounds are tested above
            auto session=std::make_unique<GameWorkerSession>(identity,detector,target("fixture-absent"),detectionPolicy(),gamePolicy(GameIntent::Off),binding(),policy,i,t);
            borrowed=session.get();return session;};
        const auto began=workerControlNowMs();
        o.workerFleetEvidence=[&](uint64_t epoch){const auto now=gameDetectionTime();
            if(borrowed&&borrowed->snapshot(0).generation>=(retry?2u:1u)&&borrowed->snapshot(0).commandRevision>=2&&!finishAfter)finishAfter=now.monotonicMs+100;
            // Includes the real ten-second restart delay and platform-specific refused stats-connect latency.
            if((finishAfter&&now.monotonicMs>=finishAfter)||now.monotonicMs-began>25000)stop=1;
            WorkerFleetEvidence e;e.safety={epoch,++revision,now,{true,true,false}};return e;};
        Supervisor s(std::move(o));CHECK(s.run()==0);CHECK(factories==1&&finishAfter&&borrowed);
        if(borrowed){const auto result=borrowed->snapshot(0);CHECK(result.done&&result.reaped&&result.revoked&&result.generation==(retry?2u:1u));CHECK(result.exitCode==0);}
    }
}
struct FleetProbeState {unsigned starts=0,launches=0,ticks=0,joins=0;int exitCode=0;bool revoked=false,lateUnreaped=false;};
class FleetProbe final:public WorkerFleet {
    FleetProbeState &p;
   public:
    explicit FleetProbe(FleetProbeState &state):p(state){}
    bool start(unsigned,GameGuardTime)override{++p.starts;return true;}
    bool launch(unsigned,std::filesystem::path,std::vector<std::string>,unsigned,GameGuardTime)override{++p.launches;return !p.revoked;}
    void tick(GameGuardTime,const WorkerFleetEvidence &)override{++p.ticks;}
    WorkerFleetState state()const override{return p.revoked?WorkerFleetState::Stopped:WorkerFleetState::Active;}
    WorkerFleetSnapshot snapshot(unsigned)const override{WorkerFleetSnapshot v;v.requested=p.launches!=0;v.done=p.revoked;v.exitCode=p.revoked?p.exitCode:-1;
        v.revoked=p.revoked;v.ownershipUnresolved=p.joins&&p.lateUnreaped;return v;}
    std::vector<std::string> takeLines(unsigned)override{return {};}
    void requestStop()override{p.revoked=true;}
    void stop()override{requestStop();++p.joins;}
};
void supervisorRefusals(){
    for(unsigned mode=0;mode<6;++mode){volatile sig_atomic_t stop=0;FleetProbeState state;state.lateUnreaped=mode==2||mode==5;state.exitCode=mode==4?1:0;unsigned factories=0;
        SupervisorOptions o;o.exe="must-not-be-executed";o.apiEnabled=false;o.stop=&stop;WorkerSeed seed;seed.uuid="fixture.no-device";o.seeds={seed};
        o.workerFleetFactory=[&](const SessionIntent &,SessionToken)->std::unique_ptr<WorkerFleet>{++factories;
            if(mode==0)return {};
            if(mode==1||mode==5)stop=1;
            return std::make_unique<FleetProbe>(state);};
        o.workerFleetEvidence=[&](uint64_t)->WorkerFleetEvidence{if(mode==3)throw 1;stop=1;return {};};
        Supervisor s(std::move(o));CHECK(s.run()==(mode==1||mode==4?0:8));CHECK(factories==1&&state.ticks==0);
        if(mode!=0)CHECK(state.revoked&&state.joins==1&&state.launches==(mode==1||mode==5?0u:1u));
    }
}
}
int main(int argc,char **argv){
    if(argc>=3&&std::string(argv[1])=="--fleet-child")return child(argc,argv);
    if(argc!=3&&argc!=4)return 2;
    identity=std::filesystem::absolute(std::filesystem::u8path(argv[1]));detector=std::filesystem::absolute(std::filesystem::u8path(argv[2]));exe=std::filesystem::u8path(selfExecutablePath());
    if(!platformInit())return 3;
    if(argc==4&&std::string(argv[3])=="cases")exitCases();
    else if(argc==4&&std::string(argv[3])=="supervisor"){supervisor();supervisorRefusals();}
    else{deadlines();lifecycle();gameAndRestarts();exitCases();supervisor();supervisorRefusals();}
    platformShutdown();
    return finishTests("game-owned worker session and supervisor");
}
