// Pure policy/intent contracts and an isolated launcher fixture; no GPU or network.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "../src/product/session_intent.h"
#include "product_test_util.h"

using namespace om;

int main(int argc, char **argv) {
    for (int i = 1; i + 1 < argc; ++i) if (std::string(argv[i]) == "--launcher-fixture") {
        puts("fixture-child-started");
        fflush(stdout);
        if (std::string(argv[i + 1]) == "wait") for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return std::atoi(argv[i + 1]);
    }
    SessionIntent intent;
    PolicyConstraints limits;
    CHECK(arbitrateMining(intent, {}, limits) == MiningDecision::Stopped);
    const auto first = intent.explicitStart();
    CHECK(first.epoch != 0 && intent.permits(first));
    CHECK(!intent.permits(intent.explicitStart()));  // a second start cannot replace an active epoch
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::MiningNormal);
    limits.economicsEnabled = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::PausedEconomics);
    limits.economicDataValid = limits.positiveEligibleCandidate = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::MiningNormal);
    limits.game = GameIntent::Coexist;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::PausedGame);  // opted-in but detector missing
    limits.gameDetectionValid = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::MiningNormal);
    limits.gameDetected = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::PausedGame);
    limits.coexistQualified = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::MiningCoexist);
    limits.game = GameIntent::Pause;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::PausedGame);
    limits.criticalTelemetryInvalid = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::Cooling);
    limits.faultLatched = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::FaultLatched);
    // One explicit override covers only a known game/economic pause, never safety or unknown detection.
    limits = {};
    limits.game = GameIntent::Pause;
    limits.gameDetected = limits.gameDetectionValid = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::PausedGame);
    limits.forceResume = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::MiningForced);
    limits.thermalLimited = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::Cooling);
    limits.thermalLimited = false;
    limits.faultLatched = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::FaultLatched);
    limits = {};
    limits.game = GameIntent::Pause;
    limits.gameDetectionValid = false;
    limits.forceResume = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::PausedGame);
    limits = {};
    limits.economicsEnabled = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::PausedEconomics);
    limits.forceResume = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::MiningForced);
    limits.economicDataValid = limits.positiveEligibleCandidate = true;
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::MiningNormal);
    // The single-use control releases itself when the overridden pause disappears or changes source.
    ForceResumeControl control;
    limits = {};
    limits.game = GameIntent::Pause;
    limits.gameDetected = limits.gameDetectionValid = true;
    CHECK(control.press(limits) == ForceResumePress::Activated);
    limits.forceResume = control.observe(limits);
    CHECK(limits.forceResume && arbitrateMining(intent, first, limits) == MiningDecision::MiningForced);
    CHECK(control.press(limits) == ForceResumePress::Cancelled && !control.active());
    CHECK(control.press(limits) == ForceResumePress::Activated);
    CHECK(control.observe(limits));
    limits.gameDetected = false;
    CHECK(!control.observe(limits) && !control.active());  // game pause cleared
    limits = {};
    limits.economicsEnabled = true;
    CHECK(control.press(limits) == ForceResumePress::Activated);
    limits.economicDataValid = limits.positiveEligibleCandidate = true;
    CHECK(!control.observe(limits) && !control.active());  // economic pause cleared
    limits = {};
    limits.economicsEnabled = true;
    CHECK(control.press(limits) == ForceResumePress::Activated);
    limits.game = GameIntent::Pause;
    limits.gameDetected = limits.gameDetectionValid = true;
    CHECK(!control.observe(limits) && !control.active());  // a new pause source needs a new press
    limits.thermalLimited = true;
    CHECK(control.press(limits) == ForceResumePress::Ignored);
    limits = {};
    limits.game = GameIntent::Pause;
    limits.gameDetectionValid = false;
    CHECK(control.press(limits) == ForceResumePress::Ignored);
    intent.stop();
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::Stopped);
    limits = {};
    CHECK(arbitrateMining(intent, first, limits) == MiningDecision::Stopped);  // price/game recovery cannot start
    const auto second = intent.explicitStart();
    CHECK(second.epoch > first.epoch && !intent.permits(first));
    CHECK(arbitrateMining(intent, second, limits) == MiningDecision::MiningNormal);
    // Exhaust all combinations of safety/economic/game observations; none revive a stopped session.
    intent.stop();
    for (unsigned mask = 0; mask < 256; ++mask) {
        limits.faultLatched = (mask & 1) != 0;
        limits.thermalLimited = (mask & 2) != 0;
        limits.criticalTelemetryInvalid = (mask & 4) != 0;
        limits.gameDetected = (mask & 8) != 0;
        limits.gameDetectionValid = (mask & 16) != 0;
        limits.coexistQualified = (mask & 32) != 0;
        limits.economicsEnabled = true;
        limits.economicDataValid = (mask & 64) != 0;
        limits.positiveEligibleCandidate = (mask & 128) != 0;
        limits.forceResume = true;
        for (auto game : {GameIntent::Off, GameIntent::Pause, GameIntent::Coexist}) {
            limits.game = game;
            CHECK(arbitrateMining(intent, second, limits) == MiningDecision::Stopped);
        }
    }
    return finishTests("product_intent");
}
