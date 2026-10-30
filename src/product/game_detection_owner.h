// One frozen target and one isolated flight. Start/stop never grant mining permission or replace a game guard.
#pragma once
#include "game_detection.h"
namespace om {
enum class GameDetectionOwnerState { Dormant, Disabled, Waiting, Ready, Stopped, ClockFault, Faulted };
struct GameDetectionPolicy {
    bool enabled=false;
    unsigned pollMs=500,helperTimeoutMs=1000,scanBudgetMs=250,maxProcesses=32768;
};
struct GameDetectionSnapshot {
    GameDetectionOwnerState state=GameDetectionOwnerState::Dormant;
    LocalTaskStatus transport=LocalTaskStatus::Unavailable;
    GameProbeStatus status=GameProbeStatus::Incomplete;
    GameDetection detection;
    bool pending=false,unreaped=false;
    uint64_t completed=0;
};
using GameDetectionClock=std::function<GameGuardTime()>;
class GameDetectionOwner {
   public:
    GameDetectionOwner(std::filesystem::path helper,GameProcessTarget target,GameDetectionPolicy policy={},GameDetectionClock clock={});
    ~GameDetectionOwner();
    GameDetectionOwner(const GameDetectionOwner &)=delete;
    GameDetectionOwner &operator=(const GameDetectionOwner &)=delete;
    bool start(uint64_t explicitSessionGeneration); // single-use; frozen until a new explicit parent session
    void requestStop(); // memory-only revocation before joins
    void stop();
    std::shared_ptr<const GameDetectionSnapshot> snapshot()const;
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
