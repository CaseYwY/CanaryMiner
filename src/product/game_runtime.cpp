#include "game_runtime.h"
namespace om {
GameRuntime::GameRuntime(std::filesystem::path helper,GameProcessTarget target,GameDetectionPolicy detection,GameGuardPolicy policy,
    GameProfitBinding binding,const SessionIntent &intent,SessionToken token)
    :intent_(intent),token_(token),target_(std::move(target)),detectionPolicy_(detection),policy_(policy),bindingValid_(binding.gameKey==target_.gameKey),
     guard_(policy,std::move(binding),intent,token),detector_(std::move(helper),target_,detection){}
GameRuntime::~GameRuntime(){stop();}
void GameRuntime::fault(GameGuardTime now){
    state_=GameRuntimeState::Faulted;
    // Guard retains its episode/profit evidence while the fault latches; target changes cannot clear it.
    guard_.update(now,{},GameSafety{false,false,true});detector_.requestStop();
}
bool GameRuntime::start(GameGuardTime now){
    if(attempted_)return false;
    attempted_=true;
    if(!intent_.permits(token_)){requestStop();return false;}
    // Validate the guard/clock before any detector launch, including when game policy is Off.
    const auto initial=guard_.update(now,{},{}).state;
    if(initial==GameGuardState::Faulted||initial==GameGuardState::ClockFault){fault(now);return false;}
    if(policy_.mode==GameIntent::Off){state_=GameRuntimeState::Off;return true;}
    if(!bindingValid_||!validGameProcessTarget(target_)||!detectionPolicy_.enabled||!detector_.start(token_.epoch)){fault(now);return false;}
    state_=GameRuntimeState::Active;
    // Startup is Unknown until a fresh detector result arrives. No worker may infer absence from an empty startup snapshot.
    return true;
}
const GameGuardSnapshot &GameRuntime::tick(GameGuardTime now,const GameSafety &safety,std::shared_ptr<const GameProfitWindow> profit){
    if(!intent_.permits(token_)){requestStop();return guard_.snapshot();}
    if(state_==GameRuntimeState::Dormant||state_==GameRuntimeState::Stopped||state_==GameRuntimeState::Faulted)return guard_.snapshot();
    std::shared_ptr<const GameDetectionSnapshot> source;
    if(state_==GameRuntimeState::Active){
        source=detector_.snapshot();
        if(!source||source->unreaped||(source->state!=GameDetectionOwnerState::Waiting&&source->state!=GameDetectionOwnerState::Ready)||
            source->detection.generation!=token_.epoch||source->detection.gameKey!=target_.gameKey){fault(now);return guard_.snapshot();}
    }
    const auto &result=guard_.update(now,source?source->detection:GameDetection{},safety,std::move(profit));
    if(result.state==GameGuardState::Stopped)requestStop();
    else if(result.state==GameGuardState::Faulted||result.state==GameGuardState::ClockFault){state_=GameRuntimeState::Faulted;detector_.requestStop();}
    return guard_.snapshot();
}
void GameRuntime::requestStop(){attempted_=true;state_=GameRuntimeState::Stopped;guard_.requestStop();detector_.requestStop();}
void GameRuntime::stop(){requestStop();detector_.stop();}
} // namespace om
