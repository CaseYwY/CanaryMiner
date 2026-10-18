// Host-only coherence and invariant checks for PearlProgressBus.

#include <atomic>
#include <cstdio>
#include <limits>
#include <thread>

#include "../src/core/algo.h"
#include "../src/core/pearl_progress.h"

using namespace om;

static int failures = 0;

static void check(const char *name, bool condition) {
    if (condition) {
        std::printf("  ok   %s\n", name);
    } else {
        std::printf("  FAIL %s\n", name);
        ++failures;
    }
}

struct FakeClock {
    uint64_t value = 1000;
    static uint64_t tick(void *opaque) noexcept {
        return ++static_cast<FakeClock *>(opaque)->value;
    }
};

class NonPearlAlgorithm final : public Algorithm {
   public:
    const char *name() const override { return "test"; }
    size_t memoryBytes(const Job &) const override { return 0; }
    bool prepare(const Job &) override { return true; }
    bool search(const Job &, uint64_t, uint64_t,
                std::vector<Solution> *) override { return true; }
    bool verify(const Job &, const Solution &) const override { return true; }
    void release() override {}
};

class RecordingProgressSink final : public PearlAlgorithmProgressSink {
   public:
    void onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent event, uint64_t amount,
        PearlProgressReason reason) noexcept override {
        ++calls;
        lastEvent = event;
        lastAmount = amount;
        lastReason = reason;
    }

    uint64_t calls = 0;
    PearlAlgorithmProgressEvent lastEvent =
        PearlAlgorithmProgressEvent::AttemptStarted;
    uint64_t lastAmount = 0;
    PearlProgressReason lastReason = PearlProgressReason::None;
};

static PearlProgressSnapshot tagged(uint64_t tag) {
    PearlProgressSnapshot s;
    s.phase = PearlProgressPhase::Searching;
    s.lastReason = PearlProgressReason::SearchFailed;
    s.lastOutcome = PearlSubmitOutcome::Accepted;
    s.sessionGeneration = tag;
    s.authorizationGeneration = tag;
    s.jobGeneration = tag;
    s.prepareGeneration = tag;
    s.searchGeneration = tag;
    s.finalizerGeneration = tag;
    s.candidateGeneration = tag;
    s.submitGeneration = tag;
    s.sessionStartedMs = tag * 100 + 1;
    s.authorizedMs = tag * 100 + 2;
    s.jobAdoptedMs = tag * 100 + 3;
    s.jobRetiredMs = tag * 100 + 4;
    s.prepareStartedMs = tag * 100 + 5;
    s.prepareCompletedMs = tag * 100 + 6;
    s.searchStartedMs = tag * 100 + 7;
    s.searchCompletedMs = tag * 100 + 8;
    s.lastAttemptStartedMs = tag * 100 + 9;
    s.lastAttemptCompletedMs = tag * 100 + 10;
    s.lastFinalizerStartedMs = tag * 100 + 11;
    s.lastFinalizerProgressMs = tag * 100 + 12;
    s.lastProofSerializedMs = tag * 100 + 13;
    s.lastSubmitAttemptMs = tag * 100 + 14;
    s.lastAcknowledgedMs = tag * 100 + 15;
    s.lastOutcomeMs = tag * 100 + 16;
    s.publishedAtMs = tag * 100 + 17;
    s.searchesStarted = tag * 10;
    s.searchesCompleted = tag * 4;
    s.searchesFailed = tag * 3;
    s.attemptsStarted = tag * 20;
    s.attemptsCompleted = tag * 19;
    s.hitsObserved = tag * 18;
    s.hitsRetained = tag * 17;
    s.finalizersStarted = tag * 16;
    s.hitSaturationEvents = tag * 2;
    s.hitsDroppedBySaturation = tag;
    s.candidatesSelected = tag * 16;
    s.hostChecked = tag * 15;
    s.hostValid = tag * 9;
    s.hostInvalid = tag * 6;
    s.proofsSerialized = tag * 8;
    s.submitAttempts = tag * 7;
    s.submissionFailures = tag;
    s.submitted = tag * 6;
    s.acknowledged = tag * 5;
    s.accepted = tag * 2;
    s.rejected = tag;
    s.stale = tag;
    s.unknown = tag;
    s.pending = tag;
    s.acceptedSuperseded = tag;
    for (std::size_t i = 1; i < kPearlProgressReasonCount; ++i)
        s.localDrops[i] = tag * 1000 + i;
    return s;
}

static bool isCoherentTag(const PearlProgressSnapshot &s) {
    const uint64_t tag = s.sessionGeneration;
    if (tag == 0 || s.phase != PearlProgressPhase::Searching ||
        s.lastReason != PearlProgressReason::SearchFailed ||
        s.lastOutcome != PearlSubmitOutcome::Accepted ||
        s.authorizationGeneration != tag ||
        s.jobGeneration != tag || s.prepareGeneration != tag ||
        s.searchGeneration != tag || s.finalizerGeneration != tag ||
        s.candidateGeneration != tag || s.submitGeneration != tag)
        return false;
    if (s.sessionStartedMs != tag * 100 + 1 ||
        s.authorizedMs != tag * 100 + 2 ||
        s.jobAdoptedMs != tag * 100 + 3 ||
        s.jobRetiredMs != tag * 100 + 4 ||
        s.prepareStartedMs != tag * 100 + 5 ||
        s.prepareCompletedMs != tag * 100 + 6 ||
        s.searchStartedMs != tag * 100 + 7 ||
        s.searchCompletedMs != tag * 100 + 8 ||
        s.lastAttemptStartedMs != tag * 100 + 9 ||
        s.lastAttemptCompletedMs != tag * 100 + 10 ||
        s.lastFinalizerStartedMs != tag * 100 + 11 ||
        s.lastFinalizerProgressMs != tag * 100 + 12 ||
        s.lastProofSerializedMs != tag * 100 + 13 ||
        s.lastSubmitAttemptMs != tag * 100 + 14 ||
        s.lastAcknowledgedMs != tag * 100 + 15 ||
        s.lastOutcomeMs != tag * 100 + 16 ||
        s.publishedAtMs != tag * 100 + 17)
        return false;
    if (s.searchesStarted != tag * 10 ||
        s.searchesCompleted != tag * 4 || s.searchesFailed != tag * 3 ||
        s.attemptsStarted != tag * 20 ||
        s.attemptsCompleted != tag * 19 || s.hitsObserved != tag * 18 ||
        s.hitsRetained != tag * 17 || s.finalizersStarted != tag * 16 ||
        s.hitSaturationEvents != tag * 2 ||
        s.hitsDroppedBySaturation != tag ||
        s.candidatesSelected != tag * 16 || s.hostChecked != tag * 15 ||
        s.hostValid != tag * 9 || s.hostInvalid != tag * 6 ||
        s.proofsSerialized != tag * 8 || s.submitAttempts != tag * 7 ||
        s.submissionFailures != tag || s.submitted != tag * 6 ||
        s.acknowledged != tag * 5 || s.accepted != tag * 2 ||
        s.rejected != tag || s.stale != tag || s.unknown != tag ||
        s.pending != tag || s.acceptedSuperseded != tag)
        return false;
    if (s.localDrops[0] != 0) return false;
    for (std::size_t i = 1; i < kPearlProgressReasonCount; ++i)
        if (s.localDrops[i] != tag * 1000 + i) return false;
    return s.invariantsHold();
}

struct MidPublishPause {
    std::atomic<bool> entered{false};
    std::atomic<bool> readerFinished{false};
    std::atomic<bool> timedOut{false};
};

static void pauseMidPublish(void *opaque) {
    MidPublishPause *pause = static_cast<MidPublishPause *>(opaque);
    pause->entered.store(true, std::memory_order_release);
    for (uint64_t spin = 0; spin < 1000000; ++spin) {
        if (pause->readerFinished.load(std::memory_order_acquire)) return;
        std::this_thread::yield();
    }
    pause->timedOut.store(true, std::memory_order_release);
}

struct AfterFirstSequenceRace {
    std::atomic<bool> startWriter{false};
    std::atomic<bool> writerDone{false};
    std::atomic<bool> timedOut{false};
};

static void publishAfterFirstSequence(void *opaque) {
    AfterFirstSequenceRace *race =
        static_cast<AfterFirstSequenceRace *>(opaque);
    race->startWriter.store(true, std::memory_order_release);
    for (uint64_t spin = 0; spin < 1000000; ++spin) {
        if (race->writerDone.load(std::memory_order_acquire)) return;
        std::this_thread::yield();
    }
    race->timedOut.store(true, std::memory_order_release);
}

int main() {
    const uint64_t max = std::numeric_limits<uint64_t>::max();
    check("saturating add reaches UINT64_MAX without wrapping",
          pearlProgressSaturatingAdd(max - 2, 9) == max);

    RecordingProgressSink scopeSink;
    {
        PearlFinalizerProgressScope scope(&scopeSink);
        check("a new finalizer scope begins armed", scope.activeForTest());
    }
    check("an early finalizer exit emits one fail-closed drop",
          scopeSink.calls == 1 &&
              scopeSink.lastEvent ==
                  PearlAlgorithmProgressEvent::CandidateDropped &&
              scopeSink.lastAmount == 1 &&
              scopeSink.lastReason == PearlProgressReason::FinalizerFailed);
    {
        PearlFinalizerProgressScope scope(&scopeSink);
        scope.drop(PearlProgressReason::CandidateOwnershipLost);
        scope.drop(PearlProgressReason::ProofSerializationFailed);
    }
    check("an explicit finalizer drop is emitted exactly once",
          scopeSink.calls == 2 &&
              scopeSink.lastReason ==
                  PearlProgressReason::CandidateOwnershipLost);
    {
        PearlFinalizerProgressScope scope(&scopeSink);
        scope.close();
    }
    check("a completed finalizer scope emits no drop", scopeSink.calls == 2);
    try {
        PearlFinalizerProgressScope scope(&scopeSink);
        throw 7;
    } catch (int) {
    }
    check("exception unwinding emits exactly one fail-closed drop",
          scopeSink.calls == 3 &&
              scopeSink.lastReason == PearlProgressReason::FinalizerFailed);
    {
        PearlFinalizerProgressScope scope(nullptr);
    }
    check("a null-sink finalizer scope is inert", scopeSink.calls == 3);

    PearlProgressSnapshot saturated;
    saturated.searchesStarted = max - 1;
    saturated.add(&saturated.searchesStarted, 50);
    saturated.localDrops[static_cast<std::size_t>(
        PearlProgressReason::HostVerificationFailed)] = max - 1;
    saturated.noteDrop(PearlProgressReason::HostVerificationFailed, 50);
    check("funnel and per-reason counters saturate",
          saturated.searchesStarted == max &&
              saturated.dropsFor(
                  PearlProgressReason::HostVerificationFailed) == max);

    PearlProgressSnapshot invalid = tagged(1);
    invalid.attemptsCompleted = invalid.attemptsStarted + 1;
    check("completed attempts cannot exceed started attempts",
          !invalid.invariantsHold());
    invalid = tagged(1);
    invalid.hostInvalid = 1;
    check("host valid plus invalid must equal checked",
          !invalid.invariantsHold());
    invalid = tagged(1);
    invalid.acknowledged = 0;
    check("accepted outcomes require acknowledgements",
          !invalid.invariantsHold());
    invalid = tagged(1);
    invalid.submissionFailures = 2;
    check("submitted plus locally failed cannot exceed submit attempts",
          !invalid.invariantsHold());
    invalid = tagged(1);
    invalid.searchGeneration = 0;
    check("searching requires its exact ownership generation",
          !invalid.invariantsHold());
    invalid = tagged(1);
    invalid.phase = PearlProgressPhase::Preparing;
    invalid.prepareGeneration = 0;
    check("preparing requires its exact ownership generation",
          !invalid.invariantsHold());
    invalid = tagged(1);
    invalid.phase = PearlProgressPhase::Finalizing;
    invalid.finalizerGeneration = 0;
    check("finalizing requires its distinct finalizer generation",
          !invalid.invariantsHold());
    invalid = tagged(1);
    invalid.finalizersStarted = invalid.candidatesSelected - 1;
    check("selected candidates cannot exceed started finalizers",
          !invalid.invariantsHold());
    invalid = tagged(1);
    invalid.attemptsCompleted = invalid.finalizersStarted - 1;
    check("started finalizers cannot exceed completed attempts",
          !invalid.invariantsHold());
    invalid = tagged(1);
    invalid.phase = PearlProgressPhase::AwaitingOutcome;
    invalid.candidateGeneration = 0;
    check("submission phases retain candidate ownership",
          !invalid.invariantsHold());
    invalid = tagged(1);
    invalid.phase = PearlProgressPhase::AwaitingOutcome;
    invalid.submitGeneration = 0;
    check("submission phases require their exact submit generation",
          !invalid.invariantsHold());

    NonPearlAlgorithm nonPearl;
    check("non-Pearl algorithms reject the optional progress capability",
          !nonPearl.setPearlProgressSink(nullptr));

    PearlProgressBus algorithmBus;
    PearlProgressSnapshot initial;
    initial.phase = PearlProgressPhase::Searching;
    initial.sessionGeneration = 1;
    initial.authorizationGeneration = 1;
    initial.jobGeneration = 1;
    initial.prepareGeneration = 1;
    initial.searchGeneration = 1;
    initial.searchesStarted = 1;
    FakeClock fakeClock;

    PearlProgressBus scopedFailureBus;
    PearlAlgorithmProgressWriter scopedFailureWriter(
        &scopedFailureBus, initial, FakeClock::tick, &fakeClock);
    scopedFailureWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptStarted);
    scopedFailureWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptCompleted);
    scopedFailureWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsObserved);
    scopedFailureWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsRetained);
    scopedFailureWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::FinalizerStarted);
    {
        PearlFinalizerProgressScope scope(&scopedFailureWriter);
    }
    PearlProgressSnapshot scopedFailureRead;
    check("fail-closed scope returns the writer to Searching exactly once",
          scopedFailureBus.tryRead(&scopedFailureRead) &&
              scopedFailureRead.phase == PearlProgressPhase::Searching &&
              scopedFailureRead.finalizersStarted == 1 &&
              scopedFailureRead.dropsFor(
                  PearlProgressReason::FinalizerFailed) == 1 &&
              scopedFailureRead.invariantsHold());

    PearlAlgorithmProgressWriter algorithmWriter(
        &algorithmBus, initial, FakeClock::tick, &fakeClock);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptStarted);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptCompleted);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsObserved, 5);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsRetained, 3);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitBufferSaturated, 2);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::FinalizerStarted);
    PearlProgressSnapshot algorithmRead;
    check("finalizer starts before candidate selection or submission ownership",
          algorithmBus.tryRead(&algorithmRead) &&
              algorithmRead.phase == PearlProgressPhase::Finalizing &&
              algorithmRead.finalizerGeneration == 1 &&
              algorithmRead.finalizersStarted == 1 &&
              algorithmRead.candidatesSelected == 0 &&
              algorithmRead.candidateGeneration == 0 &&
              algorithmRead.lastFinalizerStartedMs ==
                  algorithmRead.lastFinalizerProgressMs &&
              algorithmRead.invariantsHold());

    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::CandidateSelected);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HostValid);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::FinalizerProgress);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::ProofSerialized);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptStarted);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptCompleted);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsObserved);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsRetained);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::FinalizerStarted);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::CandidateSelected);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HostValid);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::ProofSerialized);
    check("two attempts and two serialized proofs remain algorithm-owned",
          algorithmBus.tryRead(&algorithmRead) &&
              algorithmRead.attemptsStarted == 2 &&
              algorithmRead.attemptsCompleted == 2 &&
              algorithmRead.hitsObserved == 6 &&
              algorithmRead.hitsRetained == 4 &&
              algorithmRead.hitSaturationEvents == 1 &&
              algorithmRead.hitsDroppedBySaturation == 2 &&
              algorithmRead.finalizerGeneration == 2 &&
              algorithmRead.finalizersStarted == 2 &&
              algorithmRead.candidatesSelected == 2 &&
              algorithmRead.hostChecked == 2 &&
              algorithmRead.hostValid == 2 &&
              algorithmRead.proofsSerialized == 2 &&
              algorithmRead.candidateGeneration == 0 &&
              algorithmRead.phase == PearlProgressPhase::Searching &&
              algorithmRead.invariantsHold());
    check("algorithm progress timestamps use the injected monotonic clock",
          algorithmRead.lastAttemptStartedMs > 1000 &&
              algorithmRead.lastAttemptCompletedMs >
                  algorithmRead.lastAttemptStartedMs &&
              algorithmRead.lastFinalizerStartedMs >
                  algorithmRead.lastAttemptCompletedMs &&
              algorithmRead.lastFinalizerProgressMs >=
                  algorithmRead.lastFinalizerStartedMs &&
              algorithmRead.lastProofSerializedMs >
                  algorithmRead.lastFinalizerStartedMs &&
              algorithmRead.publishedAtMs ==
                  algorithmRead.lastProofSerializedMs);

    // A later retained hit may fail the host recheck. Aggregate funnel
    // counters remain coherent without pretending this is a pool rejection.
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptStarted);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptCompleted);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsObserved);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsRetained);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::FinalizerStarted);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::CandidateSelected);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HostInvalid);
    check("host-invalid candidate is a reasoned local drop",
          algorithmBus.tryRead(&algorithmRead) &&
              algorithmRead.finalizersStarted == 3 &&
              algorithmRead.candidatesSelected == 3 &&
              algorithmRead.hostChecked == 3 &&
              algorithmRead.hostValid == 2 &&
              algorithmRead.hostInvalid == 1 &&
              algorithmRead.dropsFor(
                  PearlProgressReason::HostVerificationFailed) == 1 &&
              algorithmRead.phase == PearlProgressPhase::Searching &&
              algorithmRead.invariantsHold());

    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptStarted);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptCompleted);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsObserved);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsRetained);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::FinalizerStarted);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::CandidateSelected);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HostValid);
    algorithmWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::CandidateDropped, 1,
        PearlProgressReason::ProofSerializationFailed);
    check("post-check finalizer failure retains its stable reason",
          algorithmBus.tryRead(&algorithmRead) &&
              algorithmRead.finalizersStarted == 4 &&
              algorithmRead.candidatesSelected == 4 &&
              algorithmRead.hostChecked == 4 &&
              algorithmRead.hostValid == 3 &&
              algorithmRead.hostInvalid == 1 &&
              algorithmRead.proofsSerialized == 2 &&
              algorithmRead.dropsFor(
                  PearlProgressReason::ProofSerializationFailed) == 1 &&
              algorithmRead.phase == PearlProgressPhase::Searching &&
              algorithmRead.invariantsHold());

    PearlProgressSnapshot exhaustedInitial = initial;
    exhaustedInitial.finalizerGeneration = max;
    PearlProgressBus exhaustedBus;
    PearlAlgorithmProgressWriter exhaustedWriter(
        &exhaustedBus, exhaustedInitial, FakeClock::tick, &fakeClock);
    exhaustedWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::FinalizerStarted);
    PearlProgressSnapshot exhaustedRead;
    check("finalizer generation exhaustion fails closed",
          exhaustedBus.tryRead(&exhaustedRead) &&
              exhaustedRead.phase == PearlProgressPhase::Failed &&
              exhaustedRead.finalizerGeneration == max &&
              exhaustedRead.finalizersStarted == 0 &&
              exhaustedRead.dropsFor(
                  PearlProgressReason::FinalizerGenerationExhausted) == 1 &&
              exhaustedRead.invariantsHold());
    const uint64_t exhaustedPublishedAt = exhaustedRead.publishedAtMs;
    exhaustedWriter.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptStarted);
    check("finalizer generation exhaustion remains latched",
          exhaustedBus.tryRead(&exhaustedRead) &&
              exhaustedRead.phase == PearlProgressPhase::Failed &&
              exhaustedRead.attemptsStarted == 0 &&
              exhaustedRead.publishedAtMs == exhaustedPublishedAt);

    PearlProgressBus bus;
    PearlProgressSnapshot clocked = tagged(7);
    clocked.phase = PearlProgressPhase::AwaitingOutcome;
    bus.publish(clocked);
    PearlProgressSnapshot read;
    check("fake-clock timestamps and phase publish coherently",
          bus.tryRead(&read) && read.phase == PearlProgressPhase::AwaitingOutcome &&
              read.sessionStartedMs == 701 && read.lastOutcomeMs == 716 &&
              read.publishedAtMs == 717 && read.invariantsHold());
    check("each completed publication leaves an even sequence",
          (bus.sequence() & 1) == 0);
    read.sessionGeneration = 123456;
    check("zero read attempts fail without touching the output",
          !bus.tryRead(&read, 0) && read.sessionGeneration == 123456);

    PearlProgressBus pausedBus;
    pausedBus.publish(tagged(1));
    MidPublishPause pause;
    std::thread pausedWriter([&] {
        pausedBus.publishWithMidpointHookForTest(tagged(2), pauseMidPublish,
                                                 &pause);
    });
    bool entered = false;
    for (uint64_t spin = 0; spin < 1000000; ++spin) {
        if (pause.entered.load(std::memory_order_acquire)) {
            entered = true;
            break;
        }
        std::this_thread::yield();
    }
    PearlProgressSnapshot untouched = tagged(999);
    const bool readWhileOdd =
        entered && pausedBus.tryRead(&untouched, 64);
    pause.readerFinished.store(true, std::memory_order_release);
    pausedWriter.join();
    check("the deterministic writer pause was reached without timeout",
          entered && !pause.timedOut.load(std::memory_order_acquire));
    check("an odd in-progress publication yields a bounded failed read",
          !readWhileOdd && untouched.sessionGeneration == 999);
    check("the paused publication becomes readable after completion",
          pausedBus.tryRead(&read) && isCoherentTag(read) &&
              read.sessionGeneration == 2);

    PearlProgressBus changedBus;
    changedBus.publish(tagged(2));
    AfterFirstSequenceRace race;
    std::thread changedWriter([&] {
        for (uint64_t spin = 0; spin < 1000000; ++spin) {
            if (race.startWriter.load(std::memory_order_acquire)) {
                changedBus.publish(tagged(3));
                race.writerDone.store(true, std::memory_order_release);
                return;
            }
            std::this_thread::yield();
        }
        race.timedOut.store(true, std::memory_order_release);
        race.writerDone.store(true, std::memory_order_release);
    });
    untouched = tagged(999);
    const bool acceptedChangedSequence =
        changedBus.tryReadWithAfterFirstSequenceHookForTest(
            &untouched, 1, publishAfterFirstSequence, &race);
    changedWriter.join();
    check("the after-first-sequence race completed without timeout",
          !race.timedOut.load(std::memory_order_acquire));
    check("a changed second sequence rejects the mixed read",
          !acceptedChangedSequence && untouched.sessionGeneration == 999);
    check("the replacement publication is readable after the rejected race",
          changedBus.tryRead(&read) && isCoherentTag(read) &&
              read.sessionGeneration == 3);

    // The concurrent oracle expects every field to carry the tagged schema;
    // do not let it mistake the deliberately different phase above for a tear.
    bus.publish(tagged(7));

    std::atomic<bool> readerReady{false};
    std::atomic<bool> writerDone{false};
    std::atomic<uint64_t> coherentReads{0};
    std::atomic<uint64_t> tornReads{0};
    std::thread reader([&] {
        readerReady.store(true, std::memory_order_release);
        while (!writerDone.load(std::memory_order_acquire)) {
            PearlProgressSnapshot sample;
            if (!bus.tryRead(&sample, 1024)) continue;
            if (isCoherentTag(sample)) {
                coherentReads.fetch_add(1, std::memory_order_relaxed);
            } else {
                tornReads.fetch_add(1, std::memory_order_relaxed);
            }
        }
        for (int i = 0; i < 100; ++i) {
            PearlProgressSnapshot sample;
            if (bus.tryRead(&sample, 1024) && isCoherentTag(sample))
                coherentReads.fetch_add(1, std::memory_order_relaxed);
            else
                tornReads.fetch_add(1, std::memory_order_relaxed);
        }
    });
    std::thread writer([&] {
        while (!readerReady.load(std::memory_order_acquire))
            std::this_thread::yield();
        for (uint64_t tag = 8; tag < 50008; ++tag) {
            bus.publish(tagged(tag));
            if ((tag & 63) == 0) std::this_thread::yield();
        }
        writerDone.store(true, std::memory_order_release);
    });
    writer.join();
    reader.join();

    check("threaded observer obtained coherent publications",
          coherentReads.load(std::memory_order_relaxed) >= 100);
    check("sequence validation rejects every torn publication",
          tornReads.load(std::memory_order_relaxed) == 0);

    if (failures) {
        std::printf("FAIL: %d Pearl progress-bus checks failed\n", failures);
        return 1;
    }
    std::puts("PASS: Pearl progress publication is coherent and bounded");
    return 0;
}
