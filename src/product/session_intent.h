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
enum class MiningDecision { Stopped, FaultLatched, Cooling, PausedGame, PausedEconomics, MiningForced, MiningCoexist, MiningNormal };

struct PolicyConstraints {
    bool faultLatched = false;
    bool thermalLimited = false, criticalTelemetryInvalid = false;
    GameIntent game = GameIntent::Off;
    bool gameDetected = false, gameDetectionValid = false, coexistQualified = false;
    bool economicsEnabled = false, economicDataValid = false, positiveEligibleCandidate = false;
    // One explicit operator request. It only covers the game/economic pause actually present; it never
    // covers stop, fault, cooling, failed telemetry, or unknown game detection.
    bool forceResume = false;
};

constexpr unsigned kForceResumeGame = 1u << 0, kForceResumeEconomics = 1u << 1;

// A game pause exists for an opted-in mode while detection is missing, or while a detected game
// cannot coexist. Unknown detection still pauses; it just cannot be overridden.
inline bool gamePauseActive(const PolicyConstraints &limits) {
    return limits.game != GameIntent::Off &&
        (!limits.gameDetectionValid ||
         (limits.gameDetected && (limits.game == GameIntent::Pause || !limits.coexistQualified)));
}

inline bool economicsPauseActive(const PolicyConstraints &limits) {
    return limits.economicsEnabled && (!limits.economicDataValid || !limits.positiveEligibleCandidate);
}

// Pause sources the current explicit override may cover. Zero means the request is inapplicable;
// unknown game detection is deliberately excluded because the operator cannot see which game it is.
inline unsigned forceResumePauseMask(const PolicyConstraints &limits) {
    if (limits.faultLatched || limits.thermalLimited || limits.criticalTelemetryInvalid) return 0;
    unsigned mask = 0;
    if (limits.game != GameIntent::Off && limits.gameDetectionValid && limits.gameDetected &&
        (limits.game == GameIntent::Pause || !limits.coexistQualified))
        mask |= kForceResumeGame;
    if (economicsPauseActive(limits)) mask |= kForceResumeEconomics;
    return mask;
}

inline MiningDecision arbitrateMining(const SessionIntent &intent, SessionToken token, const PolicyConstraints &limits) {
    if (!intent.permits(token)) return MiningDecision::Stopped;
    if (limits.faultLatched) return MiningDecision::FaultLatched;
    if (limits.thermalLimited || limits.criticalTelemetryInvalid) return MiningDecision::Cooling;
    if (limits.forceResume && forceResumePauseMask(limits) != 0) return MiningDecision::MiningForced;
    if (gamePauseActive(limits)) return MiningDecision::PausedGame;
    if (economicsPauseActive(limits)) return MiningDecision::PausedEconomics;
    return limits.game == GameIntent::Coexist && limits.gameDetected ? MiningDecision::MiningCoexist : MiningDecision::MiningNormal;
}

enum class ForceResumePress { Ignored, Activated, Cancelled };

// Memory-only, single-use override for the pause present at the key press. It is not persisted and it
// clears as soon as that exact set of pause sources disappears or changes, or a higher-priority
// safety condition appears. A later pause needs a fresh press.
class ForceResumeControl {
   public:
    ForceResumePress press(const PolicyConstraints &limits) {
        if (active_) {
            cancel();
            return ForceResumePress::Cancelled;
        }
        const unsigned mask = forceResumePauseMask(limits);
        if (!mask) return ForceResumePress::Ignored;
        active_ = true;
        pauseMask_ = mask;
        return ForceResumePress::Activated;
    }
    // Effective flag for the next arbitration. Also performs automatic one-shot release.
    bool observe(const PolicyConstraints &limits) {
        if (!active_) return false;
        if (forceResumePauseMask(limits) != pauseMask_) {
            cancel();
            return false;
        }
        return true;
    }
    bool active() const { return active_; }
    unsigned pauseMask() const { return pauseMask_; }
    void cancel() {
        active_ = false;
        pauseMask_ = 0;
    }

   private:
    bool active_ = false;
    unsigned pauseMask_ = 0;
};

}  // namespace om
