// Session/episode-bound game pause state machine. No detector, game metrics, miner actuator or real coexistence permit.
#pragma once
#include "game_profit.h"
#include "session_intent.h"
namespace om {
struct GameGuardTime {int64_t utcMs=0;uint64_t monotonicMs=0;};
enum class GamePresence { Unknown, Absent, Present };
struct GameDetection {
    GamePresence state=GamePresence::Unknown;
    std::string gameKey,episodeKey;
    uint64_t generation=0,revision=0;
    GameGuardTime observed;
};
struct GameSafety {bool telemetryValid=false,thermalSafe=false,fault=false;};
struct GameGuardPolicy {
    GameIntent mode=GameIntent::Off;
    ForecastQualification coexistence=ForecastQualification::Unsupported; // only synthetic tests can grant it in this slice
    bool tenMinuteProfitRule=true;
    uint64_t exitDebounceMs=2000,maximumDetectionGapMs=2000,maximumProfitAgeMs=10000;
};
enum class GameGuardState { Off, NormalEligible, PausedUnknown, PausedPolicy, NeedsQualification, CoexistEligible, ForceResumeEligible, Cooling, ProfitUnavailable, ProfitLocked, Stopped, ClockFault, Faulted };
// Only an explicit, known game/economic pause can be forced. Unknown detection, cooling, faults,
// clock faults and stopped sessions never appear here.
inline bool gameForceResumeEligible(GameGuardState s){
    return s==GameGuardState::PausedPolicy||s==GameGuardState::NeedsQualification||
        s==GameGuardState::ProfitUnavailable||s==GameGuardState::ProfitLocked||
        s==GameGuardState::ForceResumeEligible;
}
struct GameGuardSnapshot {
    GameGuardState state=GameGuardState::Stopped;
    std::string episodeKey;
    bool profitLocked=false,profitCheckDue=false;
    bool forceResumeActive=false; // memory-only; cleared automatically when the original pause or safety condition changes
    std::optional<uint64_t> coexistBeginMonoMs;
    std::shared_ptr<const GameProfitWindow> lastProfit; // retained evidence is historical when unavailable/locked/stopped
};
class GameSessionGuard {
   public:
    GameSessionGuard(GameGuardPolicy policy,GameProfitBinding binding,const SessionIntent &intent,SessionToken token);
    GameSessionGuard(const GameSessionGuard &)=delete;
    GameSessionGuard &operator=(const GameSessionGuard &)=delete;
    const GameGuardSnapshot &update(GameGuardTime now,const GameDetection &detection,const GameSafety &safety,
        std::shared_ptr<const GameProfitWindow> profit={});
    void requestStop();
    // One explicit key press: arms the current eligible pause, or cancels an already active override.
    // False when there is no eligible pause or the session is terminal.
    bool requestForceResume();
    void cancelForceResume();
    bool forceResumeActive()const{return forceActive_;}
    const GameGuardSnapshot &snapshot()const{return view_;}
   private:
    GameGuardPolicy policy_;GameProfitBinding binding_;const SessionIntent &intent_;SessionToken token_;
    GameGuardSnapshot view_;bool terminal_=false,clockStarted_=false,qualified_=false;
    bool forceArmed_=false,forceActive_=false;
    GameGuardTime anchor_,last_;
    std::optional<GameDetection> detection_;
    std::optional<uint64_t> absentSince_;
    int64_t coexistBeginUtcMs_=0;
    uint64_t lastProfitRevision_=0,lastProfitEndMonoMs_=0;
    bool time(GameGuardTime now);
    bool acceptProfit(const std::shared_ptr<const GameProfitWindow> &profit,GameGuardTime now);
    void clearEpisode();
};
} // namespace om
