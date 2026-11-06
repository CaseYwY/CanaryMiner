// Single-control-thread lifetime binding of one detector to one episode guard. No miner/worker actuator.
#pragma once
#include "evidence_deadline.h"
#include "game_detection_owner.h"
namespace om {
enum class GameRuntimeState { Dormant, Off, Active, Stopped, Faulted };
class GameRuntime {
   public:
    GameRuntime(std::filesystem::path helper,GameProcessTarget target,GameDetectionPolicy detection,GameGuardPolicy policy,
        GameProfitBinding binding,const SessionIntent &intent,SessionToken token);
    ~GameRuntime();
    GameRuntime(const GameRuntime &)=delete;
    GameRuntime &operator=(const GameRuntime &)=delete;
    bool start(GameGuardTime now); // single-use; no implicit SessionIntent::explicitStart
    const GameGuardSnapshot &tick(GameGuardTime now,const GameSafety &safety,std::shared_ptr<const GameProfitWindow> profit={});
    // Restricts a caller's already bounded deadline to the exact evidence consumed by the immediately preceding tick.
    uint64_t runDeadline(GameGuardTime now,uint64_t upperMonoMs)const;
    // One explicit operator press over the current known game pause. No persistence; the guard
    // releases it automatically when that pause or its safety basis changes.
    bool requestForceResume();
    void cancelForceResume();
    bool forceResumeActive()const;
    void requestStop(); // revoke guard and detector before joining
    void stop();
    GameRuntimeState state()const;
    const GameGuardSnapshot &snapshot()const;
    // Current helper ownership, including revocation and late Unreaped. Never a mining permission.
    std::shared_ptr<const GameDetectionSnapshot> detection()const;
   private:
    const SessionIntent &intent_;SessionToken token_;
    GameProcessTarget target_;GameDetectionPolicy detectionPolicy_;GameGuardPolicy policy_;
    bool bindingValid_=false;mutable bool attempted_=false;
    mutable GameRuntimeState state_=GameRuntimeState::Dormant;
    GameGuardTime lastTick_;mutable uint64_t runUntil_=0;
    EvidenceDeadline detectionDeadline_,profitDeadline_;std::string profitEpisode_;
    mutable GameSessionGuard guard_;
    mutable GameDetectionOwner detector_; // fixed lifetime: no replacement/reset setter
    GameGuardSnapshot denied_;
    std::shared_ptr<const GameDetectionSnapshot> deniedDetection_;
    void fault(GameGuardTime now);
    void revoke()const;
    void observe()const; // control-thread memory revocation; never joins or invokes the detector clock
};
} // namespace om
