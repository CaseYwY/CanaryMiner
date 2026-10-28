// Single-owner policy/intent contracts. No I/O, hardware access or implicit start.
#pragma once

#include <cstdint>
#include <limits>

namespace om {

struct SessionToken {
    uint64_t epoch = 0;
};

class SessionIntent {
   public:
    // Only an explicit run/start action calls this; price/game/config callbacks cannot.
    SessionToken explicitStart() {
        if (active_ || epoch_ == std::numeric_limits<uint64_t>::max()) return {};
        ++epoch_;
        active_ = true;
        return {epoch_};
    }
    void stop() { active_ = false; }
    bool permits(SessionToken token) const { return active_ && token.epoch != 0 && token.epoch == epoch_; }

   private:
    uint64_t epoch_ = 0;
    bool active_ = false;
};

enum class GameIntent { Off, Pause, Coexist };
enum class MiningDecision { Stopped, FaultLatched, Cooling, PausedGame, PausedEconomics, MiningCoexist, MiningNormal };

struct PolicyConstraints {
    bool faultLatched = false;
    bool thermalLimited = false, criticalTelemetryInvalid = false;
    GameIntent game = GameIntent::Off;
    bool gameDetected = false, gameDetectionValid = false, coexistQualified = false;
    bool economicsEnabled = false, economicDataValid = false, positiveEligibleCandidate = false;
};

inline MiningDecision arbitrateMining(const SessionIntent &intent, SessionToken token, const PolicyConstraints &limits) {
    if (!intent.permits(token)) return MiningDecision::Stopped;
    if (limits.faultLatched) return MiningDecision::FaultLatched;
    if (limits.thermalLimited || limits.criticalTelemetryInvalid) return MiningDecision::Cooling;
    if (limits.game != GameIntent::Off && (!limits.gameDetectionValid ||
        (limits.gameDetected && (limits.game == GameIntent::Pause || !limits.coexistQualified)))) return MiningDecision::PausedGame;
    if (limits.economicsEnabled && (!limits.economicDataValid || !limits.positiveEligibleCandidate)) return MiningDecision::PausedEconomics;
    return limits.game == GameIntent::Coexist && limits.gameDetected ? MiningDecision::MiningCoexist : MiningDecision::MiningNormal;
}

}  // namespace om
