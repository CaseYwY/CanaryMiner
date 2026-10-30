#include "game_worker_session.h"
#include "worker_identity.h"
#include "worker_process_owner.h"
#include <algorithm>
#include <thread>
namespace om {
namespace {
bool sameSafety(const WorkerSafetyEvidence &a,const WorkerSafetyEvidence &b){
    return a.epoch==b.epoch&&a.revision==b.revision&&a.observed.utcMs==b.observed.utcMs&&a.observed.monotonicMs==b.observed.monotonicMs&&
        a.safety.telemetryValid==b.safety.telemetryValid&&a.safety.thermalSafe==b.safety.thermalSafe&&a.safety.fault==b.safety.fault;
}
}
struct GameWorkerSession::Impl {
    struct Life {
        const uint64_t generation;
        WorkerControlIssuer issuer;WorkerProcessOwner owner;
        bool disconnected=false,settledBeforeDisconnect=false;
        Life(WorkerControlBinding b,const SessionIntent &i,SessionToken t):generation(b.workerGeneration),issuer(std::move(b),i,t){}
    };
    struct Slot {
        std::filesystem::path exe;std::vector<std::string> args;unsigned drain=0;
        bool configured=false,launchPending=false;
        WorkerFleetSnapshot view;
        std::vector<std::unique_ptr<Life>> lives; // retain retired owners until final join; no join/reconstruction inside tick
    };
    const SessionIntent &intent;SessionToken token;GameWorkerPolicy policy;
    GameRuntime game;WorkerIdentitySource identities;
    std::atomic<bool> cancel{false};std::thread identityThread;mutable std::mutex identityMutex;
    bool identityDone=false;WorkerIdentityResult identityResult;
    std::vector<std::string> keys;size_t nextKey=1;
    bool attempted=false,clockStarted=false;GameGuardTime anchor,last;
    WorkerFleetState state=WorkerFleetState::Dormant;std::vector<Slot> slots;
    std::optional<WorkerSafetyEvidence> safety;
    Impl(std::filesystem::path identity,std::filesystem::path detector,GameProcessTarget target,GameDetectionPolicy detection,
        GameGuardPolicy p,GameProfitBinding b,GameWorkerPolicy w,const SessionIntent &i,SessionToken t)
        :intent(i),token(t),policy(w),game(std::move(detector),std::move(target),detection,p,std::move(b),i,t),identities(std::move(identity)){}
    void halt(WorkerFleetState s){
        attempted=true;state=s;cancel=true;game.requestStop();
        for(auto &slot:slots){slot.view.revoked=true;slot.view.retryable=false;slot.view.paused=false;
            if(slot.launchPending){slot.launchPending=false;slot.view.done=true;slot.view.exitCode=s==WorkerFleetState::Faulted?8:0;}
            for(auto &life:slot.lives){life->issuer.requestStop();life->owner.requestStop();}}
    }
    bool time(GameGuardTime now){
        if(!intent.permits(token)){halt(WorkerFleetState::Stopped);return false;}
        if(now.utcMs<=0||now.utcMs>253402300799999LL||!now.monotonicMs||
            (clockStarted&&(now.utcMs<last.utcMs||now.monotonicMs<last.monotonicMs))){halt(WorkerFleetState::Faulted);return false;}
        if(!clockStarted){clockStarted=true;anchor=now;}
        const auto wall=static_cast<uint64_t>(now.utcMs-anchor.utcMs),mono=now.monotonicMs-anchor.monotonicMs;
        if((wall>mono?wall-mono:mono-wall)>2000){halt(WorkerFleetState::Faulted);return false;}
        last=now;return true;
    }
    bool unresolved()const{
        {std::lock_guard<std::mutex> lock(identityMutex);if(identityDone&&identityResult.status==LocalTaskStatus::Unreaped)return true;}
        for(const auto &slot:slots)if(!slot.lives.empty()&&slot.lives.back()->owner.snapshot().state==WorkerOwnerState::Unreaped)return true;
        return game.detection()->unreaped;
    }
    uint64_t safetyDeadline(GameGuardTime now,const WorkerSafetyEvidence &e){
        if(e.epoch!=token.epoch||!e.revision||e.observed.utcMs<=0||e.observed.utcMs>now.utcMs||!e.observed.monotonicMs||e.observed.monotonicMs>now.monotonicMs)return 0;
        if(safety){
            if(e.revision<safety->revision)return 0;
            if((e.revision==safety->revision&&!sameSafety(e,*safety))||e.observed.utcMs<safety->observed.utcMs||e.observed.monotonicMs<safety->observed.monotonicMs){halt(WorkerFleetState::Faulted);return 0;}
        }
        safety=e;
        const auto age=(std::max)(static_cast<uint64_t>(now.utcMs-e.observed.utcMs),now.monotonicMs-e.observed.monotonicMs);
        if(age>=policy.maximumSafetyAgeMs||now.monotonicMs>UINT64_MAX-(policy.maximumSafetyAgeMs-age))return 0;
        return now.monotonicMs+policy.maximumSafetyAgeMs-age;
    }
    void startSlot(unsigned index,GameGuardTime now){
        auto &slot=slots[index];if(nextKey>=keys.size()){halt(WorkerFleetState::Faulted);return;}
        WorkerControlBinding binding{keys[0],keys[nextKey++],token.epoch,slot.view.generation,index};
        auto life=std::make_unique<Life>(std::move(binding),intent,token);
        const bool started=life->issuer.start(now.monotonicMs);
        const auto initial=started?life->issuer.issue(WorkerDirective::Pause,now.monotonicMs,workerControlMaxLeaseMs):std::nullopt;
        slot.lives.push_back(std::move(life));slot.launchPending=false;
        if(!initial||!slot.lives.back()->owner.start(slot.exe,slot.args,*initial,slot.drain)){halt(WorkerFleetState::Faulted);return;}
        slot.view.revoked=false;slot.view.commandRevision=initial->revision;slot.view.commandDeadlineMonoMs=initial->deadlineMonoMs;
    }
    void tickSlot(Slot &slot,GameGuardTime now,uint64_t runUntil){
        if(slot.lives.empty()||slot.launchPending||slot.view.done)return;
        auto &life=*slot.lives.back();const auto owner=life.owner.snapshot();
        slot.view.game=game.snapshot().state;
        if(owner.state==WorkerOwnerState::Unreaped){halt(WorkerFleetState::Faulted);return;}
        if(owner.done){
            const auto issue=life.issuer.snapshot(now.monotonicMs);
            slot.view.done=true;slot.view.reaped=owner.reaped;slot.view.revoked=true;slot.view.paused=false;
            slot.view.exitCode=owner.exitCode;
            const bool settled=life.disconnected?life.settledBeforeDisconnect:issue.state==WorkerControlState::Active&&!issue.pending;
            slot.view.retryable=owner.state==WorkerOwnerState::Exited&&owner.reaped&&owner.exitCode==1&&settled;
            life.issuer.requestStop();return;
        }
        if(owner.state==WorkerOwnerState::Stopping){
            if(!life.disconnected){const auto issue=life.issuer.snapshot(now.monotonicMs);life.disconnected=true;
                life.settledBeforeDisconnect=issue.state==WorkerControlState::Active&&!issue.pending;life.issuer.requestStop();}
            slot.view.revoked=true;slot.view.paused=false;slot.view.pauseRequested=true;return;
        }
        if(owner.revoked){halt(WorkerFleetState::Faulted);return;}
        if(owner.receipt&&owner.receipt->observedMonoMs<=now.monotonicMs){const auto accepted=life.issuer.acknowledge(*owner.receipt,now.monotonicMs);
            if(accepted==WorkerReceiptResult::Rejected||accepted==WorkerReceiptResult::Terminal){halt(WorkerFleetState::Faulted);return;}}
        const auto issue=life.issuer.snapshot(now.monotonicMs);
        if(issue.state!=WorkerControlState::Active){halt(WorkerFleetState::Faulted);return;}
        const bool pause=!runUntil;slot.view.pauseRequested=pause;slot.view.paused=life.issuer.paused(now.monotonicMs);
        if(issue.pending||!issue.settled||owner.state!=WorkerOwnerState::Running)return;
        const auto &previous=issue.settled->command;
        const auto directive=pause?WorkerDirective::Pause:WorkerDirective::Run;
        // Renew before the existing lease expires, but never extend cached source validity by receipt/tick time.
        if(previous.directive==directive&&previous.deadlineMonoMs-now.monotonicMs>policy.commandLeaseMs/2&&
            (pause||runUntil>=previous.deadlineMonoMs))return;
        const uint64_t lease=pause?policy.commandLeaseMs:runUntil-now.monotonicMs;
        const auto next=life.issuer.issue(directive,now.monotonicMs,lease);
        if(!next||!life.owner.submit(*next)){halt(WorkerFleetState::Faulted);return;}
        slot.view.paused=false;slot.view.commandRevision=next->revision;slot.view.commandDeadlineMonoMs=next->deadlineMonoMs;
    }
};
GameWorkerSession::GameWorkerSession(std::filesystem::path identity,std::filesystem::path detector,GameProcessTarget target,
    GameDetectionPolicy detection,GameGuardPolicy game,GameProfitBinding binding,GameWorkerPolicy policy,const SessionIntent &intent,SessionToken token)
    :impl_(new Impl(std::move(identity),std::move(detector),std::move(target),detection,game,std::move(binding),policy,intent,token)){}
GameWorkerSession::~GameWorkerSession(){stop();}
bool GameWorkerSession::start(unsigned count,GameGuardTime now){
    auto &p=*impl_;if(p.attempted)return false;p.attempted=true;
    if(!p.time(now))return false;
    if(!count||count>256||p.policy.maximumIncarnations<count||p.policy.maximumIncarnations>256||p.policy.commandLeaseMs<100||p.policy.commandLeaseMs>workerControlMaxLeaseMs||
        p.policy.maximumSafetyAgeMs<100||p.policy.maximumSafetyAgeMs>workerControlMaxLeaseMs){p.halt(WorkerFleetState::Faulted);return false;}
    try{
        p.slots.resize(count);if(!p.game.start(now)){p.halt(WorkerFleetState::Faulted);return false;}
        p.state=WorkerFleetState::Starting;
        p.identityThread=std::thread([&p]{WorkerIdentityResult result;
            try{result=p.identities.request(p.policy.maximumIncarnations+1,p.cancel);}catch(...){result.status=LocalTaskStatus::Unavailable;}
            std::lock_guard<std::mutex> lock(p.identityMutex);p.identityResult=std::move(result);p.identityDone=true;});
    }catch(...){p.halt(WorkerFleetState::Faulted);return false;}
    return true;
}
bool GameWorkerSession::launch(unsigned index,std::filesystem::path exe,std::vector<std::string> args,unsigned drain,GameGuardTime now){
    auto &p=*impl_;if((p.state!=WorkerFleetState::Starting&&p.state!=WorkerFleetState::Active)||!p.time(now)||index>=p.slots.size())return false;
    auto &slot=p.slots[index];if(slot.launchPending||(slot.view.requested&&(!slot.view.done||!slot.view.reaped||!slot.view.retryable)))return false;
    size_t bytes=0;if(!localTaskPath(exe)||args.size()>256||drain>65000)return false;
    for(const auto &arg:args){if(arg.empty()||arg.size()>4096||arg.find('\0')!=arg.npos||arg=="--worker-control-v1")return false;bytes+=arg.size();}
    if(bytes>24000||slot.view.generation==UINT64_MAX)return false;
    if(slot.configured&&(slot.exe!=exe||slot.args!=args||slot.drain!=drain))return false;
    slot.exe=std::move(exe);slot.args=std::move(args);slot.drain=drain;slot.configured=true;slot.launchPending=true;
    const auto generation=slot.view.generation+1;slot.view={};slot.view.generation=generation;slot.view.requested=true;
    return true;
}
void GameWorkerSession::tick(GameGuardTime now,const WorkerFleetEvidence &e){
    auto &p=*impl_;if(p.state!=WorkerFleetState::Starting&&p.state!=WorkerFleetState::Active)return;
    if(!p.time(now)||p.unresolved()){if(p.state!=WorkerFleetState::Stopped)p.halt(WorkerFleetState::Faulted);return;}
    try{
        const auto safetyUntil=p.safetyDeadline(now,e.safety);
        if(p.state==WorkerFleetState::Faulted)return;
        const auto safe=safetyUntil?e.safety.safety:GameSafety{};
        const auto &guard=p.game.tick(now,safe,e.profit);
        if(p.game.state()==GameRuntimeState::Faulted||guard.state==GameGuardState::Stopped){p.halt(WorkerFleetState::Faulted);return;}
        if(p.state==WorkerFleetState::Starting){
            bool done=false;LocalTaskStatus status=LocalTaskStatus::Unavailable;
            {std::lock_guard<std::mutex> lock(p.identityMutex);done=p.identityDone;
                if(done){status=p.identityResult.status;if(status==LocalTaskStatus::Ok)p.keys=std::move(p.identityResult.keys);}}
            if(!done)return;
            if(status!=LocalTaskStatus::Ok||p.keys.size()!=p.policy.maximumIncarnations+1){p.halt(WorkerFleetState::Faulted);return;}
            p.state=WorkerFleetState::Active;
        }
        if(now.monotonicMs>UINT64_MAX-p.policy.commandLeaseMs){p.halt(WorkerFleetState::Faulted);return;}
        const auto upper=(std::min)(now.monotonicMs+p.policy.commandLeaseMs,safetyUntil);
        auto runUntil=safe.telemetryValid&&safe.thermalSafe&&!safe.fault?p.game.runDeadline(now,upper):0;
        // Leave bounded scheduling headroom to deliver Pause before source validity ends. This is not extra lease time.
        if(runUntil&&runUntil-now.monotonicMs<=(std::min)(100u,p.policy.commandLeaseMs/2))runUntil=0;
        for(unsigned i=0;i<p.slots.size()&&p.state==WorkerFleetState::Active;++i){
            if(p.slots[i].launchPending)p.startSlot(i,now);
            if(p.state==WorkerFleetState::Active)p.tickSlot(p.slots[i],now,runUntil);
        }
    }catch(...){p.halt(WorkerFleetState::Faulted);}
}
WorkerFleetState GameWorkerSession::state()const{return impl_->state;}
WorkerFleetSnapshot GameWorkerSession::snapshot(unsigned index)const{
    const auto &p=*impl_;if(index>=p.slots.size())return {};
    const auto &slot=p.slots[index];auto view=slot.view;view.game=p.game.snapshot().state;view.ownershipUnresolved=p.unresolved();
    if(!slot.launchPending&&!slot.lives.empty()&&slot.lives.back()->generation==view.generation){
        const auto current=slot.lives.back()->owner.snapshot();view.reaped=current.reaped;
        view.ownershipUnresolved=view.ownershipUnresolved||current.state==WorkerOwnerState::Unreaped;
        if(current.done&&(p.state==WorkerFleetState::Stopped||p.state==WorkerFleetState::Faulted)){view.done=true;if(!slot.view.done)view.exitCode=current.exitCode;}
        if(current.revoked){view.revoked=true;view.paused=false;}
    }
    if(p.state==WorkerFleetState::Stopped||p.state==WorkerFleetState::Faulted){view.revoked=true;view.retryable=false;view.paused=false;}
    if(view.ownershipUnresolved){view.exitCode=8;view.revoked=true;view.retryable=false;}
    return view;
}
std::vector<std::string> GameWorkerSession::takeLines(unsigned index){
    auto &p=*impl_;if(index>=p.slots.size()||p.slots[index].lives.empty())return {};
    return p.slots[index].lives.back()->owner.takeLines();
}
const GameGuardSnapshot &GameWorkerSession::gameSnapshot()const{return impl_->game.snapshot();}
void GameWorkerSession::requestStop(){impl_->halt(WorkerFleetState::Stopped);}
void GameWorkerSession::stop(){requestStop();auto &p=*impl_;if(p.identityThread.joinable())p.identityThread.join();
    for(auto &slot:p.slots)for(auto &life:slot.lives)life->owner.stop();
    p.game.stop();}
} // namespace om
