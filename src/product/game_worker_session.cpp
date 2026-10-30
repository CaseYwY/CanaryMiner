#include "game_worker_session.h"
#include "worker_identity.h"
#include "worker_process_owner.h"
#include <algorithm>
#include <thread>
namespace om {
namespace {
OptimizationTime optimizationTime(GameGuardTime t){return {t.utcMs,t.monotonicMs};}
bool sameSafety(const WorkerSafetyEvidence &a,const WorkerSafetyEvidence &b){
    return a.epoch==b.epoch&&a.revision==b.revision&&a.observed.utcMs==b.observed.utcMs&&a.observed.monotonicMs==b.observed.monotonicMs&&
        a.safety.telemetryValid==b.safety.telemetryValid&&a.safety.thermalSafe==b.safety.thermalSafe&&a.safety.fault==b.safety.fault;
}
}
struct GameWorkerSession::Impl {
    struct Life {
        const uint64_t generation;
        WorkerControlIssuer issuer;WorkerProcessOwner owner;
        std::unique_ptr<EconomicCommandOwner> commands;bool commandStarted=false;
        bool disconnected=false,settledBeforeDisconnect=false;
        Life(WorkerControlBinding b,const SessionIntent &i,SessionToken t):generation(b.workerGeneration),issuer(std::move(b),i,t){}
    };
    struct Slot {
        std::filesystem::path exe;std::vector<std::string> args;unsigned drain=0;
        bool configured=false,launchPending=false;
        WorkerFleetSnapshot view;
        uint64_t contextRevision=1;bool drift=false;WorkProfileConditions conditions;
        std::unique_ptr<EconomicEvaluationOwner> evaluation;
        std::unique_ptr<EconomicParentBudget> budget; // lives/guards are destroyed before these borrowed parent owners
        std::vector<std::unique_ptr<Life>> lives; // retain retired owners until final join; no join/reconstruction inside tick
    };
    const SessionIntent &intent;SessionToken token;GameWorkerPolicy policy;
    std::vector<GameEconomicPolicy> economics;
    GameRuntime game;WorkerIdentitySource identities;
    std::atomic<bool> cancel{false};std::thread identityThread;mutable std::mutex identityMutex;
    bool identityDone=false;WorkerIdentityResult identityResult;
    std::vector<std::string> keys;size_t nextKey=1;
    bool attempted=false,clockStarted=false;GameGuardTime anchor,last;
    WorkerFleetState state=WorkerFleetState::Dormant;std::vector<Slot> slots;
    std::optional<WorkerSafetyEvidence> safety;
    EvidenceDeadline safetyValidity;
    Impl(std::filesystem::path identity,std::filesystem::path detector,GameProcessTarget target,GameDetectionPolicy detection,
        GameGuardPolicy p,GameProfitBinding b,GameWorkerPolicy w,const SessionIntent &i,SessionToken t,std::vector<GameEconomicPolicy> e)
        :intent(i),token(t),policy(w),economics(std::move(e)),game(std::move(detector),std::move(target),detection,p,std::move(b),i,t),identities(std::move(identity)){}
    void halt(WorkerFleetState s){
        attempted=true;state=s;cancel=true;game.requestStop();
        for(auto &slot:slots){slot.view.revoked=true;slot.view.retryable=false;slot.view.paused=false;slot.view.pauseRequested=true;
            if(slot.launchPending){slot.launchPending=false;slot.view.done=true;slot.view.exitCode=s==WorkerFleetState::Faulted?8:0;}
            for(auto &life:slot.lives){if(life->commands)life->commands->requestStop();life->issuer.requestStop();life->owner.requestStop();}
            if(slot.evaluation)slot.evaluation->requestStop();
            if(slot.budget)slot.budget->requestStop();}
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
        for(const auto &slot:slots){
            if(slot.evaluation&&slot.evaluation->schedule().unreaped)return true;
            if(!slot.lives.empty()&&slot.lives.back()->owner.snapshot().state==WorkerOwnerState::Unreaped)return true;
        }
        return game.detection()->unreaped;
    }
    uint64_t safetyDeadline(GameGuardTime now,const WorkerSafetyEvidence &e){
        if(e.epoch!=token.epoch||!e.revision||e.observed.utcMs<=0||e.observed.utcMs>now.utcMs||!e.observed.monotonicMs||e.observed.monotonicMs>now.monotonicMs)return 0;
        if(safety){
            if(e.revision<safety->revision)return 0;
            if((e.revision==safety->revision&&!sameSafety(e,*safety))||e.observed.utcMs<safety->observed.utcMs||e.observed.monotonicMs<safety->observed.monotonicMs){halt(WorkerFleetState::Faulted);return 0;}
        }
        safety=e;
        return safetyValidity.clip(e.revision,now,e.observed,policy.maximumSafetyAgeMs);
    }
    void startSlot(unsigned index,GameGuardTime now){
        auto &slot=slots[index];if(nextKey>=keys.size()){halt(WorkerFleetState::Faulted);return;}
        WorkerControlBinding binding{keys[0],keys[nextKey++],token.epoch,slot.view.generation,index};
        auto life=std::make_unique<Life>(binding,intent,token);
        if(slot.evaluation)life->commands=std::make_unique<EconomicCommandOwner>(economics[index].commands,binding,*slot.evaluation,life->issuer,*slot.budget,intent,token);
        const bool started=life->issuer.start(now.monotonicMs);
        const auto initial=started?life->issuer.issue(WorkerDirective::Pause,now.monotonicMs,workerControlMaxLeaseMs):std::nullopt;
        slot.lives.push_back(std::move(life));slot.launchPending=false;
        slot.view.commandPending=initial.has_value();
        if(!initial||!slot.lives.back()->owner.start(slot.exe,slot.args,*initial,slot.drain)){halt(WorkerFleetState::Faulted);return;}
        slot.view.revoked=false;slot.view.commandRevision=initial->revision;slot.view.commandDeadlineMonoMs=initial->deadlineMonoMs;
    }
    void tickSlot(unsigned index,GameGuardTime now,uint64_t permittedUntil){
        auto &slot=slots[index];
        if(slot.lives.empty()||slot.launchPending||slot.view.done)return;
        auto &life=*slot.lives.back();const auto owner=life.owner.snapshot();
        slot.view.game=game.snapshot().state;
        if(owner.state==WorkerOwnerState::Unreaped){halt(WorkerFleetState::Faulted);return;}
        if(owner.done){
            const auto issue=life.issuer.snapshot(now.monotonicMs);
            slot.view.commandPending=issue.pending.has_value();
            slot.view.done=true;slot.view.reaped=owner.reaped;slot.view.revoked=true;slot.view.paused=false;
            slot.view.exitCode=owner.exitCode;
            const bool settled=life.disconnected?life.settledBeforeDisconnect:issue.state==WorkerControlState::Active&&!issue.pending;
            slot.view.retryable=owner.state==WorkerOwnerState::Exited&&owner.reaped&&owner.exitCode==1&&settled;
            life.issuer.requestStop();
            if(life.commands){
                // Only actual reaping plus settled control at disconnect/exit can release this incarnation's budget attachment.
                if(slot.view.retryable&&life.commandStarted){if(!life.commands->retire(optimizationTime(now))){halt(WorkerFleetState::Faulted);return;}}
                else{slot.view.retryable=false;life.commands->requestStop();}}
            return;
        }
        if(owner.state==WorkerOwnerState::Stopping){
            if(!life.disconnected){const auto issue=life.issuer.snapshot(now.monotonicMs);life.disconnected=true;
                life.settledBeforeDisconnect=issue.state==WorkerControlState::Active&&!issue.pending;slot.view.commandPending=issue.pending.has_value();life.issuer.requestStop();}
            slot.view.revoked=true;slot.view.paused=false;slot.view.pauseRequested=true;return;
        }
        if(owner.revoked){halt(WorkerFleetState::Faulted);return;}
        if(owner.receipt&&owner.receipt->observedMonoMs<=now.monotonicMs){const auto accepted=life.commandStarted?
            life.commands->acknowledge(*owner.receipt,optimizationTime(now)):life.issuer.acknowledge(*owner.receipt,now.monotonicMs);
            if(accepted==WorkerReceiptResult::Rejected||accepted==WorkerReceiptResult::Terminal){halt(WorkerFleetState::Faulted);return;}}
        const auto issue=life.issuer.snapshot(now.monotonicMs);
        slot.view.commandPending=issue.pending.has_value();
        if(issue.state!=WorkerControlState::Active){halt(WorkerFleetState::Faulted);return;}
        if(life.commands){
            if(!life.commandStarted){
                slot.view.pauseRequested=true;slot.view.paused=life.issuer.paused(now.monotonicMs);
                if(issue.pending||!issue.settled||owner.state!=WorkerOwnerState::Running)return;
                if(!life.commands->start(optimizationTime(now),economics[index].plan)){halt(WorkerFleetState::Faulted);return;}
                life.commandStarted=true;
            }
            const auto next=life.commands->poll(optimizationTime(now),slot.contextRevision,economics[index].plan,slot.conditions,permittedUntil);
            if(life.commands->snapshot().revokeRequired){halt(WorkerFleetState::Faulted);return;}
            if(next){slot.view.commandPending=true;slot.view.commandRevision=next->revision;slot.view.commandDeadlineMonoMs=next->deadlineMonoMs;
                if(!life.owner.submit(*next)){life.commands->transportFailed();halt(WorkerFleetState::Faulted);return;}}
            const auto current=life.issuer.snapshot(now.monotonicMs);
            slot.view.commandPending=current.pending.has_value();
            slot.view.pauseRequested=current.pending?current.pending->directive==WorkerDirective::Pause:
                !current.settled||current.settled->command.directive==WorkerDirective::Pause;
            slot.view.paused=life.issuer.paused(now.monotonicMs);return;
        }
        // Request Pause with bounded delivery headroom, without erasing the actual permission ceiling.
        const bool pause=!permittedUntil||permittedUntil-now.monotonicMs<=(std::min)(100u,policy.commandLeaseMs/2);
        slot.view.pauseRequested=pause;slot.view.paused=life.issuer.paused(now.monotonicMs);
        if(issue.pending){
            // A pending Run cannot be overwritten by Pause. Revoke the owned channel when its original lease
            // is no longer covered; waiting for its receipt would leave a withdrawn command usable.
            if(issue.pending->directive==WorkerDirective::Run&&(!permittedUntil||permittedUntil<issue.pending->deadlineMonoMs))halt(WorkerFleetState::Faulted);
            return;
        }
        if(!issue.settled||owner.state!=WorkerOwnerState::Running)return;
        const auto &previous=issue.settled->command;
        const auto directive=pause?WorkerDirective::Pause:WorkerDirective::Run;
        // Renew before the existing lease expires, but never extend cached source validity by receipt/tick time.
        if(previous.directive==directive&&previous.deadlineMonoMs-now.monotonicMs>policy.commandLeaseMs/2&&
            (pause||permittedUntil>=previous.deadlineMonoMs))return;
        const uint64_t lease=pause?policy.commandLeaseMs:permittedUntil-now.monotonicMs;
        const auto next=life.issuer.issue(directive,now.monotonicMs,lease);
        if(!next){halt(WorkerFleetState::Faulted);return;}
        slot.view.paused=false;slot.view.commandPending=true;slot.view.commandRevision=next->revision;slot.view.commandDeadlineMonoMs=next->deadlineMonoMs;
        if(!life.owner.submit(*next)){halt(WorkerFleetState::Faulted);return;}
    }
};
GameWorkerSession::GameWorkerSession(std::filesystem::path identity,std::filesystem::path detector,GameProcessTarget target,
    GameDetectionPolicy detection,GameGuardPolicy game,GameProfitBinding binding,GameWorkerPolicy policy,const SessionIntent &intent,SessionToken token,std::vector<GameEconomicPolicy> economics)
    :impl_(new Impl(std::move(identity),std::move(detector),std::move(target),detection,game,std::move(binding),policy,intent,token,std::move(economics))){}
GameWorkerSession::~GameWorkerSession(){stop();}
bool GameWorkerSession::start(unsigned count,GameGuardTime now){
    auto &p=*impl_;if(p.attempted)return false;p.attempted=true;
    if(!p.time(now))return false;
    if(!count||count>256||p.policy.maximumIncarnations<count||p.policy.maximumIncarnations>256||p.policy.commandLeaseMs<100||p.policy.commandLeaseMs>workerControlMaxLeaseMs||
        p.policy.maximumSafetyAgeMs<100||p.policy.maximumSafetyAgeMs>workerControlMaxLeaseMs||(!p.economics.empty()&&p.economics.size()!=count)){p.halt(WorkerFleetState::Faulted);return false;}
    try{
        p.slots.resize(count);
        for(unsigned i=0;i<p.economics.size();++i){const auto &e=p.economics[i];auto &slot=p.slots[i];const auto &g=e.commands.guard;
            if(!e.commands.enabled||!g.enabled||!validEconomicGuardPolicy(g)||e.commands.commandLeaseMs!=p.policy.commandLeaseMs||
                !e.evaluation.enabled||validateWorkProfilePlan(e.plan)!=WorkProfileStatus::Ok){p.halt(WorkerFleetState::Faulted);return false;}
            slot.budget=std::make_unique<EconomicParentBudget>(EconomicParentBudgetPolicy{true,g.cooldownMs,g.transitionWindowMs,g.maximumTransitions,p.policy.maximumIncarnations},p.intent,p.token);
            slot.evaluation=std::make_unique<EconomicEvaluationOwner>(e.evaluation,p.intent,p.token);
            if(!slot.budget->start(optimizationTime(now))||!slot.evaluation->start(optimizationTime(now))){p.halt(WorkerFleetState::Faulted);return false;}
        }
        if(!p.game.start(now)){p.halt(WorkerFleetState::Faulted);return false;}
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
        const auto permittedUntil=safe.telemetryValid&&safe.thermalSafe&&!safe.fault?p.game.runDeadline(now,upper):0;
        for(unsigned i=0;i<p.slots.size()&&p.state==WorkerFleetState::Active;++i){
            auto &slot=p.slots[i];
            if(slot.evaluation){const bool normal=guard.state==GameGuardState::Off||guard.state==GameGuardState::NormalEligible;
                slot.conditions={safe.telemetryValid,safe.thermalSafe,normal,!normal,safe.fault,slot.drift};
                slot.evaluation->poll(optimizationTime(now),slot.contextRevision,p.economics[i].plan,slot.conditions);
                if(slot.evaluation->snapshot().state!=EconomicEvaluationState::Running){p.halt(WorkerFleetState::Faulted);break;}
            }
            if(p.slots[i].launchPending)p.startSlot(i,now);
            if(p.state==WorkerFleetState::Active)p.tickSlot(i,now,permittedUntil);
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
bool GameWorkerSession::setEconomicContext(unsigned index,uint64_t revision,bool drift){auto &p=*impl_;
    if((p.state!=WorkerFleetState::Starting&&p.state!=WorkerFleetState::Active)||index>=p.slots.size()||!p.slots[index].evaluation)return false;
    if(!p.intent.permits(p.token)){p.halt(WorkerFleetState::Stopped);return false;}
    auto &slot=p.slots[index];if(!revision||revision<slot.contextRevision){p.halt(WorkerFleetState::Faulted);return false;}
    slot.contextRevision=revision;slot.drift=drift;return true;
}
OptimizationScheduleSnapshot GameWorkerSession::economicSchedule(unsigned index)const{const auto &p=*impl_;
    return index<p.slots.size()&&p.slots[index].evaluation?p.slots[index].evaluation->schedule():OptimizationScheduleSnapshot{};
}
EconomicSubmission GameWorkerSession::submitEconomic(unsigned index,const OptimizationTicket &ticket,uint64_t revision,EconomicInput input,GameGuardTime now){auto &p=*impl_;
    if((p.state!=WorkerFleetState::Starting&&p.state!=WorkerFleetState::Active)||index>=p.slots.size()||!p.slots[index].evaluation)return EconomicSubmission::Inactive;
    if(!p.time(now))return EconomicSubmission::Inactive;
    if(revision!=p.slots[index].contextRevision)return EconomicSubmission::WrongContext;
    const auto result=p.slots[index].evaluation->submit(ticket,revision,std::move(input),optimizationTime(now));
    if(p.slots[index].evaluation->snapshot().state!=EconomicEvaluationState::Running)p.halt(WorkerFleetState::Faulted);
    return result;
}
OptimizationCompletion GameWorkerSession::completeEconomic(unsigned index,const OptimizationTicket &ticket,OptimizationOutcome outcome,GameGuardTime now){auto &p=*impl_;
    if(index>=p.slots.size()||!p.slots[index].evaluation)return OptimizationCompletion::WrongTicket;
    if(p.state==WorkerFleetState::Starting||p.state==WorkerFleetState::Active)p.time(now);
    const auto result=p.slots[index].evaluation->completeExternal(ticket,outcome,optimizationTime(now));
    if((p.state==WorkerFleetState::Starting||p.state==WorkerFleetState::Active)&&
        p.slots[index].evaluation->snapshot().state!=EconomicEvaluationState::Running)p.halt(WorkerFleetState::Faulted);
    return result;
}
GameEconomicSnapshot GameWorkerSession::economicSnapshot(unsigned index)const{const auto &p=*impl_;GameEconomicSnapshot out;
    if(index>=p.slots.size()||!p.slots[index].evaluation)return out;
    const auto &slot=p.slots[index];out.configured=true;out.contextRevision=slot.contextRevision;out.conditions=slot.conditions;
    out.evaluation=slot.evaluation->snapshot();out.budget=slot.budget->snapshot();
    if(!slot.launchPending&&!slot.lives.empty()&&slot.lives.back()->generation==slot.view.generation&&slot.lives.back()->commands){
        out.commands=slot.lives.back()->commands->snapshot();out.commandStarted=slot.lives.back()->commandStarted;}
    return out;
}
void GameWorkerSession::requestStop(){impl_->halt(WorkerFleetState::Stopped);}
void GameWorkerSession::stop(){requestStop();auto &p=*impl_;if(p.identityThread.joinable())p.identityThread.join();
    for(auto &slot:p.slots)for(auto &life:slot.lives)life->owner.stop();
    for(auto &slot:p.slots)if(slot.evaluation)slot.evaluation->stop();
    p.game.stop();}
} // namespace om
