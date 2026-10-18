// CPU-only integration fixture. Real canonical source and runtime adapter;
// fake transport/clock/device/proofs. This is NOT official proof acceptance.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/core/pearl_run_session.h"
#include "../src/core/pearl_proof_binding.h"
#include "../src/core/pearl_native_completion.h"

namespace om {
// Read-only access to the existing friend seam: a local refusal must not even
// reserve a wire request ID. Wire-only assertions cannot establish this after
// a disconnect, so inspect the actual source counter as well.
struct PearlPoolSourceTestAccess {
    static int requestId(const PearlPoolSource &source) { return source.id_; }
};
}

namespace {
using namespace om;
int failures = 0;
void check(bool condition, const char *name) {
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
}
PearlSourcePolicy policy() {
    return {PearlNetwork::Mainnet, PearlTargetSemantics::BaseBeforePenalty};
}
class Clock final : public PearlSourceMonotonicClock {
 public:
    uint64_t now = 1000;
    uint64_t nowMs() noexcept override { return now; }
    static uint64_t read(void *context) noexcept {
        return static_cast<Clock *>(context)->now;
    }
};
std::string notification(const std::string &id = "session-job", char headerByte = '1',
                        const std::string &height = "139053") {
    return "{\"id\":null,\"method\":\"mining.notify\",\"params\":{"
           "\"job_id\":\"" + id + "\",\"header\":\"" + std::string(152, headerByte) +
           "\",\"target\":\"" + std::string(60, '0') + "ffff" +
           "\",\"height\":" + height + ",\"cert_version\":3}}\n";
}
class Transport final : public PearlPoolTransport {
 public:
    explicit Transport(Clock &clock) : clock(clock) {}
    Clock &clock;
    bool open_ = false, failSubmit = false, eofOnSubmit = false;
    bool replaceOnSubmit = false;
    bool partialSubmit=false,partialPending=false;
    bool failReadable = false, failReceive = false;
    bool failNonblocking = false, failBlockingRestore = false;
    bool watchFreshness = false;
    std::size_t maxReceiveBytes = static_cast<std::size_t>(-1);
    PearlSourceObservability *observability = nullptr;
    std::vector<PearlSourceEvent> zeroWaitEvents;
    std::vector<int> waits;
    std::vector<bool> modeChanges;
    std::vector<int> submitIds;
    unsigned opens = 0, submissions = 0;
    std::string pending, wire;
    std::deque<std::string> incoming;
    PearlPoolConnectStatus open(const std::string &, int) override {
        ++opens; open_ = true; return PearlPoolConnectStatus::Connected;
    }
    bool isOpen() const override { return open_; }
    void close() override {
        open_ = false;
        // A new TCP connection cannot inherit unread bytes or a partial write
        // from the discarded fake socket. Keep only the cumulative wire log.
        incoming.clear(); pending.clear();
    }
    PearlPoolWaitStatus waitReadable(int ms) override {
        if (watchFreshness) {
            waits.push_back(ms);
            if (ms == 0 && observability)
                zeroWaitEvents.push_back(observability->snapshot().lastPublishedEvent);
        }
        if (failReadable) return PearlPoolWaitStatus::Error;
        if (!open_) return PearlPoolWaitStatus::Error;
        if (!incoming.empty()) return PearlPoolWaitStatus::Ready;
        if (ms > 0) clock.now += static_cast<uint64_t>(ms);
        return PearlPoolWaitStatus::Timeout;
    }
    PearlPoolWaitStatus waitWritable(int) override {
        return open_ ? PearlPoolWaitStatus::Ready : PearlPoolWaitStatus::Error;
    }
    bool setNonblocking(bool enabled) override {
        modeChanges.push_back(enabled);
        return open_ && !(enabled ? failNonblocking : failBlockingRestore);
    }
    PearlPoolTransfer receive(char *out, std::size_t max) override {
        if (failReceive) return {PearlPoolTransferStatus::Error, 0};
        if (incoming.empty()) return {PearlPoolTransferStatus::WouldBlock, 0};
        auto &front = incoming.front();
        if (front.empty()) {
            incoming.pop_front();
            return {PearlPoolTransferStatus::EndOfStream, 0};
        }
        const auto size = std::min(std::min(max, front.size()), maxReceiveBytes);
        std::memcpy(out, front.data(), size);
        front.erase(0, size);
        if (front.empty()) incoming.pop_front();
        return {PearlPoolTransferStatus::Bytes, size};
    }
    PearlPoolTransfer send(const char *data, std::size_t length) override {
        const std::string piece(data, length);
        if (partialPending) return {PearlPoolTransferStatus::Error,0};
        if (partialSubmit && piece.find("mining.submit")!=std::string::npos) {
            const auto n=length/2; wire.append(data,n); partialPending=true;
            return {PearlPoolTransferStatus::Bytes,n};
        }
        if (failSubmit && piece.find("mining.submit") != std::string::npos)
            return {PearlPoolTransferStatus::Error, 0};
        wire += piece; pending += piece;
        for (std::size_t end; (end = pending.find('\n')) != std::string::npos;) {
            const auto line = pending.substr(0, end);
            pending.erase(0, end + 1);
            if (line.find("mining.authorize") != std::string::npos) {
                incoming.push_back("{\"id\":1,\"result\":true,\"error\":null}\n" + notification());
            } else if (line.find("mining.submit") != std::string::npos) {
                ++submissions;
                if (eofOnSubmit) { incoming.push_back(""); continue; }
                const auto key = line.find("\"id\":");
                const auto begin = key + 5;
                const auto finish = line.find_first_not_of("0123456789", begin);
                submitIds.push_back(std::stoi(line.substr(begin, finish - begin)));
                // These replies are deliberately generated only after the
                // corresponding complete request was sent, never prequeued.
                // A higher height: the replaced job is not in the same-height history (H-019 D1).
                incoming.push_back((replaceOnSubmit ? notification("replacement-job", '1', "139054") : "") +
                                   "{\"id\":" + line.substr(begin, finish - begin) +
                                   ",\"result\":true,\"error\":null}\n");
            }
        }
        return {PearlPoolTransferStatus::Bytes, length};
    }
};
class Backend final : public Algorithm {
 public:
    PearlAlgorithmProgressSink *progress = nullptr;
    PearlProofBindingSink *binding = nullptr;
    unsigned results = 1;
    bool prepareOk = true, searchOk = true, verifyOk = true;
    bool throwPrepare = false, throwSearch = false, throwVerify = false;
    bool refuseProgress = false, refuseBinding = false, omitBinding = false;
    bool wrongBindingJob = false, prebound = false;
    bool throwUnits = false, swapOutputs = false, duplicateOutputs = false;
    bool omitSerialized = false, extraSerialized = false;
    unsigned prepares = 0;
    mutable unsigned verifies = 0;
    const char *name() const override { return "pearl-pow"; }
    PearlRuntimeCapability pearlRuntimeCapability() const noexcept override {
        return PearlRuntimeCapability::CanonicalCudaPoolV1;
    }
    size_t memoryBytes(const Job &) const override { return 0; }
    void release() override {}
    bool setPearlProgressSink(PearlAlgorithmProgressSink *sink) override {
        progress = sink; return !sink || !refuseProgress;
    }
    bool setPearlProofBindingSink(PearlProofBindingSink *sink) override {
        binding = sink; return !sink || !refuseBinding;
    }
    bool prepare(const Job &) override {
        ++prepares;
        if (throwPrepare) throw std::runtime_error("fake prepare");
        return prepareOk;
    }
    uint64_t workUnitsPerNonce() const override {
        if (throwUnits) throw std::runtime_error("fake units");
        return 1;
    }
    void emit(PearlAlgorithmProgressEvent event, uint64_t amount = 1) {
        if (progress) progress->onPearlAlgorithmProgress(event, amount);
    }
    bool search(const Job &job, uint64_t nonce, uint64_t work,
                std::vector<Solution> *out) override {
        if (throwSearch) throw std::runtime_error("fake search");
        for (uint64_t i = 0; i < work; ++i) {
            emit(PearlAlgorithmProgressEvent::AttemptStarted);
            emit(PearlAlgorithmProgressEvent::AttemptCompleted);
            if (i >= results) {
                emit(PearlAlgorithmProgressEvent::HitsObserved, 0);
                emit(PearlAlgorithmProgressEvent::HitsRetained, 0);
                continue;
            }
            emit(PearlAlgorithmProgressEvent::HitsObserved);
            emit(PearlAlgorithmProgressEvent::HitsRetained);
            emit(PearlAlgorithmProgressEvent::FinalizerStarted);
            emit(PearlAlgorithmProgressEvent::CandidateSelected);
            emit(PearlAlgorithmProgressEvent::HostValid);
            Solution sol; sol.nonce = nonce + i; sol.hit[0] = i + 1;
            sol.extra = "fake-proof-" + std::to_string(nonce + i);
            if (prebound) sol.pearlProofToken = 999;
            Job boundJob = job;
            if (wrongBindingJob) boundJob.jobId += "-wrong";
            if (!omitBinding && (!binding || !binding->bindSerializedProof(boundJob, &sol)))
                return false;
            if (!omitSerialized) emit(PearlAlgorithmProgressEvent::ProofSerialized);
            if (extraSerialized) emit(PearlAlgorithmProgressEvent::ProofSerialized);
            out->push_back(sol);
        }
        if (out->size() > 1 && swapOutputs) std::swap((*out)[0], (*out)[1]);
        if (out->size() > 1 && duplicateOutputs) (*out)[1] = (*out)[0];
        return searchOk;
    }
    bool verify(const Job &, const Solution &) const override {
        ++verifies;
        if (throwVerify) throw std::runtime_error("fake verify");
        return verifyOk;
    }
};
struct Fixture {
    Clock clock;
    Transport transport{clock};
    Backend backend;
    PearlPoolSource source{"offline.invalid", 1,
        "prl1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq",
        "host-only", policy(), "", &transport, &clock};
    Job job;
    Fixture() { transport.observability = source.pearlSourceObservability(); }
    bool start(PearlRunSession &session) {
        bool fetched = false;
        if (!session.attach()) return false;
        const bool fetchResult = session.fetch(&job, &fetched);
        if (!fetchResult || !fetched || !job.valid) {
            const auto state = session.snapshot();
            std::fprintf(stderr, "fixture fetch=%d got=%d valid=%d flags=%llu drained=%llu wire=%s\n",
                fetchResult, fetched, job.valid,
                static_cast<unsigned long long>(state.sourceIntegrityFlags),
                static_cast<unsigned long long>(state.sourceEventsDrained), transport.wire.c_str());
            return false;
        }
        return session.prepare(job);
    }
};
void normal(unsigned results, uint64_t work) {
    Fixture f; f.backend.results = results;
    {
        PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
        check(f.start(session), "normal attach/fetch/prepare");
        std::vector<Solution> solutions;
        check(session.search(f.job, 10, work, &solutions), "normal search");
        check(solutions.size() == results, "result count exact");
        for (const auto &sol : solutions) {
            bool delivered = false, valid = false; std::string error;
            check(sol.pearlProofToken != 0, "serialized proof bound before return");
            check(session.submit(f.job, sol, &error, &delivered, &valid) && delivered && valid,
                  "exact bound proof delivered through real source");
        }
        check(session.finishSearch() && session.healthy(), "normal reconciliation");
        const auto s = session.snapshot();
        check(!s.sourceQuarantined && s.progress.accepted == results &&
              s.progress.proofsSerialized == results && s.progress.attemptsCompleted == work &&
              s.progress.pending == 0, "exact coordinator outcome accounting");
        check(f.transport.submissions == results && f.backend.verifies == results,
              "one verification and wire submission per bound result");
        const uint64_t generation = s.progress.searchGeneration;
        bool fetched = false;
        check(session.fetch(&f.job, &fetched) && fetched && session.prepare(f.job),
              "same-job next fetch mirrors released source ownership");
        f.backend.results = 0;
        check(session.search(f.job, 100, 1, &solutions) && solutions.empty() &&
              session.finishSearch() && session.healthy(), "same-job second search reconciles");
        check(session.snapshot().progress.searchGeneration == generation + 1,
              "each new search advances exact generation");
        check(f.backend.prepares == 1, "unchanged issued job reuses device preparation exactly once");
    }
    check(!f.backend.progress && !f.backend.binding, "normal destructor clears borrowed sinks");
}
void tamper(unsigned field) {
    Fixture f;
    PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
    check(f.start(session), "tamper fixture starts");
    std::vector<Solution> solutions;
    check(session.search(f.job, 20, 1, &solutions) && solutions.size() == 1, "tamper search");
    if (solutions.empty()) return;
    Job job = f.job; Solution sol = solutions[0];
    switch (field) {
        case 0: sol.extra += "mutation"; break;
        case 1: ++sol.nonce; break;
        case 2: ++sol.hit[0]; break;
        case 3: ++sol.pearlProofToken; break;
        case 4: job.jobId += "mutation"; break;
        case 5: job.extra += "mutation"; break;
        case 6: job.valid = false; break;
    }
    bool delivered = false; std::string error;
    check(!session.submit(job, sol, &error, &delivered) && !delivered,
          "mutated binding is fatal and not delivered");
    check(!session.healthy() && f.transport.submissions == 0 && f.backend.verifies == 0,
          "ownership mismatch fails before verifier/network");
}
void replayAndUnconsumed() {
    for (bool replay : {false, true}) {
        Fixture f;
        PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
        check(f.start(session), "replay fixture starts");
        std::vector<Solution> solutions;
        check(session.search(f.job, 30, 1, &solutions) && solutions.size() == 1, "replay search");
        if (solutions.empty()) continue;
        if (!replay) {
            check(!session.finishSearch() && !session.healthy(), "unconsumed proof cannot reconcile");
            continue;
        }
        bool delivered = false; std::string error;
        check(session.submit(f.job, solutions[0], &error, &delivered) && delivered,
              "first submission succeeds");
        delivered = false;
        check(!session.submit(f.job, solutions[0], &error, &delivered) && !delivered &&
              f.transport.submissions == 1, "replay cannot produce second wire submission");
    }
}
void failuresAndCleanup() {
    for (unsigned mode = 0; mode < 11; ++mode) {
        Fixture f;
        f.backend.refuseProgress = mode == 0;
        f.backend.refuseBinding = mode == 1;
        f.backend.prepareOk = mode != 2;
        f.backend.throwPrepare = mode == 3;
        f.backend.searchOk = mode != 4;
        f.backend.throwSearch = mode == 5;
        f.backend.omitBinding = mode == 6;
        f.backend.wrongBindingJob = mode == 7;
        f.backend.prebound = mode == 8;
        f.backend.verifyOk = mode != 9;
        f.backend.throwVerify = mode == 10;
        {
            PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
            if (mode < 2) check(!session.attach(), "unsupported sink binding fails attach");
            else if (mode < 4) check(!f.start(session), "prepare failure propagated");
            else {
                check(f.start(session), "failure fixture starts");
                std::vector<Solution> solutions;
                const bool searched = session.search(f.job, 40, 1, &solutions);
                if (mode < 9) check(!searched, "search/binding failure propagated");
                else {
                    check(searched && solutions.size() == 1, "verification fixture search");
                    if (!solutions.empty()) {
                        bool delivered = false, valid = true; std::string error;
                        const bool submitted = session.submit(f.job, solutions[0], &error, &delivered, &valid);
                        if (mode == 9)
                            check(submitted && !valid && !delivered && session.finishSearch(),
                                  "host invalid consumed locally without dispatch");
                        else check(!submitted && !delivered, "verifier exception fails closed");
                    }
                }
            }
            check(f.transport.submissions == 0, "failure makes no wire submission");
        }
        check(!f.backend.progress && !f.backend.binding, "failure destructor clears both borrowed sinks");
    }
}
void workBoundsAndSourceFailure() {
    for (const uint64_t work : {uint64_t(0), PearlRunSession::maximumAttempts + 1}) {
        Fixture f;
        PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
        check(f.start(session), "work bound fixture starts");
        std::vector<Solution> solutions;
        check(!session.search(f.job, 0, work, &solutions) &&
              f.transport.submissions == 0, "invalid work budget fails before fake device search");
    }
    Fixture f;
    PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
    check(f.start(session), "source failure fixture starts");
    std::vector<Solution> solutions;
    check(session.search(f.job, 50, 1, &solutions) && solutions.size() == 1,
          "source failure candidate prepared");
    if (solutions.empty()) return;
    f.transport.failSubmit = true;
    bool delivered = true, valid = false; std::string error;
    check(session.submit(f.job, solutions[0], &error, &delivered, &valid) &&
          !delivered && valid, "source pre-delivery failure is not invented acceptance");
    const auto state = session.snapshot();
    check(state.progress.accepted == 0 && state.progress.submissionFailures == 1 &&
          f.transport.submissions == 0, "source failure exact local terminal accounting");
}
void sustainedTerminalDrain() {
    Fixture f;
    PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
    check(session.attach(), "sustained attach");
    for (uint64_t i = 0; i < 300; ++i) {
        bool fetched = false, delivered = false; std::string error;
        std::vector<Solution> solutions;
        const bool good = session.fetch(&f.job, &fetched) && fetched &&
            session.prepare(f.job) && session.search(f.job, i, 1, &solutions) &&
            solutions.size() == 1 &&
            session.submit(f.job, solutions[0], &error, &delivered) && delivered &&
            session.finishSearch() && session.healthy();
        check(good, "sustained source/candidate/terminal lifecycle");
        if (!good) return;
    }
    const auto state = session.snapshot();
    check(state.progress.accepted == 300 && state.terminalEvidenceObserved == 300 &&
          state.terminalEvidenceCount == 0 && state.terminalEvidenceDropped == 0,
          "bounded terminal evidence drained beyond history capacity without dropping outcomes");
}
void outputSequenceAndUnits() {
    for (unsigned mode = 0; mode < 5; ++mode) {
        Fixture f; f.backend.results = 2;
        f.backend.throwUnits = mode == 0;
        f.backend.swapOutputs = mode == 1;
        f.backend.duplicateOutputs = mode == 2;
        f.backend.omitSerialized = mode == 3;
        f.backend.extraSerialized = mode == 4;
        PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
        check(f.start(session), "output sequence fixture starts");
        std::vector<Solution> solutions;
        check(!session.search(f.job, 60, 2, &solutions) && !session.healthy() &&
              f.transport.submissions == 0, "units exception or malformed serialized order fails before wire");
    }
}
void outputAliases() {
    {
        Fixture f;
        PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
        check(session.attach(), "alias attach");
        f.job.valid = true;
        check(!session.fetch(&f.job, &f.job.valid) && f.job.valid && f.transport.opens == 0,
              "fetch status cannot alias job validity or mutate it");
    }
    for (unsigned mode = 0; mode < 5; ++mode) {
        Fixture f;
        PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
        check(f.start(session), "submit alias fixture starts");
        std::vector<Solution> solutions;
        check(session.search(f.job, 70, 1, &solutions) && solutions.size() == 1, "alias search");
        if (solutions.empty()) continue;
        const Job originalJob = f.job;
        const Solution originalSolution = solutions[0];
        bool delivered = false, valid = false; std::string error;
        std::string *errorOut = &error;
        bool *deliveryOut = &delivered, *validOut = &valid;
        if (mode == 0) deliveryOut = &f.job.valid;
        if (mode == 1) validOut = deliveryOut;
        if (mode == 2) errorOut = &f.job.extra;
        if (mode == 3) errorOut = &f.job.jobId;
        if (mode == 4) errorOut = &solutions[0].extra;
        check(!session.submit(f.job, solutions[0], errorOut, deliveryOut, validOut) &&
              f.transport.submissions == 0 && f.backend.verifies == 0,
              "aliased submit output rejected before verification/wire");
        check(f.job.valid == originalJob.valid && f.job.extra == originalJob.extra &&
              f.job.jobId == originalJob.jobId && solutions[0].extra == originalSolution.extra,
              "aliased output cannot corrupt job/proof identity");
    }
}
void disconnectAndReplacement() {
    {
        Fixture f; f.backend.results = 0;
        PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
        check(f.start(session), "idle EOF fixture starts");
        std::vector<Solution> solutions;
        check(session.search(f.job, 1, 1, &solutions) && session.finishSearch(),
              "idle EOF prior batch completes");
        f.transport.incoming.push_back("");
        bool fetched = true;
        check(session.fetch(&f.job, &fetched) && !fetched && session.healthy(),
              "EOF after finished batch is retryable no-job");
        check(session.fetch(&f.job, &fetched) && fetched && session.prepare(f.job) &&
              session.search(f.job, 2, 1, &solutions) && session.finishSearch(),
              "next fetch reconnects with fresh ownership");
        check(f.transport.opens == 2, "EOF reconnect opens exactly one new session");
    }
    for (unsigned results : {1U, 2U}) {
        Fixture f; f.backend.results = results;
        PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
        check(f.start(session), "submit EOF fixture starts");
        std::vector<Solution> solutions;
        check(session.search(f.job, 10, results, &solutions) && solutions.size() == results,
              "submit EOF search serializes full batch");
        if (solutions.empty()) continue;
        f.transport.eofOnSubmit = true;
        bool delivered = true, valid = false; std::string error;
        check(session.submit(f.job, solutions[0], &error, &delivered, &valid) &&
              delivered && valid && session.retiredAfterDisconnect(),
              "submit EOF retires batch without inventing remote acceptance");
        check(session.finishSearch() && session.healthy(), "disconnected remaining ledger explicitly retired");
        const auto state = session.snapshot();
        check(state.progress.unknown == 1 && state.progress.accepted == 0 &&
              state.progress.pending == 0 &&
              state.progress.dropsFor(PearlProgressReason::SessionSuperseded) == results - 1,
              "EOF preserves Unknown and retires only undispatched batch proofs");
        check(f.transport.submissions == 1, "EOF remaining vector never replayed automatically");
        f.transport.eofOnSubmit = false; f.backend.results = 0;
        bool fetched = false;
        check(session.fetch(&f.job, &fetched) && fetched && session.prepare(f.job) &&
              session.search(f.job, 20, 1, &solutions) && session.finishSearch(),
              "submit EOF permits a new session and new search");
    }
    {
        Fixture f; f.backend.results = 2;
        PearlRunSession session(f.backend, f.source, policy(), Clock::read, &f.clock);
        check(f.start(session), "replacement fixture starts");
        std::vector<Solution> solutions;
        check(session.search(f.job, 30, 2, &solutions) && solutions.size() == 2, "replacement search");
        if (solutions.size() != 2) return;
        f.transport.replaceOnSubmit = true;
        bool delivered = false, valid = false; std::string error;
        check(session.submit(f.job, solutions[0], &error, &delivered, &valid) &&
              delivered && valid && !session.retiredAfterDisconnect(),
              "connected job replacement preserves exact old issued terminal");
        const int afterSent = PearlPoolSourceTestAccess::requestId(f.source);
        delivered = true; valid = false;
        check(session.submit(f.job, solutions[1], &error, &delivered, &valid) &&
              !delivered && valid && !session.retiredAfterDisconnect() && session.finishSearch() && session.healthy(),
              "next retained proof for now-superseded job drops locally after genuine in-flight acceptance");
        const auto state = session.snapshot();
        check(state.progress.accepted == 1 && state.progress.acceptedSuperseded == 1 &&
              state.progress.submissionFailures == 1 && state.progress.submitted == 1 && state.progress.pending == 0 &&
              f.transport.submissions == 1 && PearlPoolSourceTestAccess::requestId(f.source) == afterSent,
              "replacement terminal is attributed once as accepted superseded");
        bool fetched = false;
        check(session.fetch(&f.job, &fetched) && fetched && f.job.jobId == "replacement-job" &&
              session.prepare(f.job) && f.backend.prepares == 2,
              "new active job is adopted only on next fetch with new preparation");
    }
}
bool noPoolOutcomes(const PearlPoolSubmitCounters &counters) {
    return counters.conserved() && counters.submitted == 0 && counters.acknowledged == 0 &&
        counters.accepted == 0 && counters.rejected == 0 && counters.stale == 0 &&
        counters.unknown == 0 && counters.acceptedSuperseded == 0 && counters.pending() == 0;
}
void checkFreshnessDispatch(const Transport &transport) {
    check(!transport.zeroWaitEvents.empty(), "freshness intake uses zero-wait transport probe");
    if (!transport.zeroWaitEvents.empty()) {
        const auto &event = transport.zeroWaitEvents.front();
        check(event.kind == PearlSourceEventKind::SubmitDispatched &&
              event.delivery == PearlSourceDelivery::NotSent && event.operationToken != 0,
              "NotSent dispatch published before freshness intake");
    }
    check(std::all_of(transport.waits.begin(), transport.waits.end(),
                     [](int wait) { return wait == 0; }), "local freshness refusal never waits for future traffic");
}
void queuedReplacementBeforeSubmit() {
    // A new ID one height up, target-only invalidation and an invalid notify
    // must refuse old proofs locally. The same ID with a changed header at the
    // current height is ambiguous (H-019 D2), so it is refused as invalid work.
    for (unsigned mode = 0; mode < 4; ++mode) {
        Fixture f; f.backend.results = 2;
        PearlRunSession session(f.backend,f.source,policy(),Clock::read,&f.clock);
        check(f.start(session), "queued replacement fixture starts");
        const Job oldJob = f.job;
        std::vector<Solution> solutions;
        check(session.search(f.job,100,2,&solutions) && solutions.size() == 2,
              "queued replacement serializes two owned proofs");
        if (solutions.size() != 2) continue;
        const int requestBefore = PearlPoolSourceTestAccess::requestId(f.source);
        const auto wireBefore = f.transport.wire;
        const uint64_t clockBefore = f.clock.now;
        // mode 0: a new ID one height up, so the old proofs are not in the same-height history.
        // mode 1: the current ID with a changed header at the same height: ambiguous (H-019 D2).
        if (mode == 0) f.transport.incoming.push_back(notification("queued-new-job", '1', "139054"));
        if (mode == 1) f.transport.incoming.push_back(notification("session-job",'2'));
        if (mode == 2) f.transport.incoming.push_back(
            "{\"id\":null,\"method\":\"mining.set_target\",\"params\":{\"target\":\"" +
            std::string(60,'0') + "eeee\"}}\n");
        if (mode == 3) f.transport.incoming.push_back(
            "{\"id\":null,\"method\":\"mining.notify\",\"params\":{}}\n");
        f.transport.watchFreshness = true;
        for (std::size_t i=0; i<solutions.size(); ++i) {
            bool delivered = true, valid = false; std::string error;
            check(session.submit(oldJob,solutions[i],&error,&delivered,&valid) && !delivered && valid &&
                  !error.empty() && session.healthy() && !session.retiredAfterDisconnect(),
                  "queued stale proof is a healthy connected local terminal");
            check(f.transport.open_ && f.transport.opens == 1 && f.transport.submissions == 0 &&
                  f.transport.wire == wireBefore && PearlPoolSourceTestAccess::requestId(f.source) == requestBefore &&
                  noPoolOutcomes(f.source.submitCounters()), "stale refusal consumes no request ID/wire/pool outcome");
            const auto source = f.source.pearlSourceObservability()->snapshot();
            const auto &terminal = source.lastPublishedEvent;
            check(!source.failClosed && source.issuedJobBound && !source.submitInFlight &&
                  terminal.kind == PearlSourceEventKind::SubmitTerminal &&
                  terminal.delivery == PearlSourceDelivery::NotSent &&
                  terminal.terminal == PearlSourceTerminal::LocalFailure &&
                  terminal.terminalEvidence == PearlSourceTerminalEvidence::LocalPreDeliveryFailure &&
                  terminal.acknowledged == 0 && terminal.superseded == 0 &&
                  terminal.reason == (mode == 1 || mode == 3 ? PearlSourceReason::InvalidJob : PearlSourceReason::Stale),
                  "connected freshness terminal is canonical NotSent, never remote stale/ACK");
        }
        checkFreshnessDispatch(f.transport);
        check(f.clock.now == clockBefore && session.finishSearch() && session.healthy(),
              "connected local terminal batch reconciles without sleeping or ledger quarantine");
        const auto state = session.snapshot();
        check(!state.sourceQuarantined && state.sourceIntegrityFlags == 0 && state.sourceEventsInvalid == 0 &&
              state.progress.submitAttempts == 2 && state.progress.submissionFailures == 2 &&
              state.progress.submitted == 0 && state.progress.accepted == 0 &&
              state.progress.stale == 0 && state.progress.unknown == 0 && state.progress.pending == 0 &&
              state.terminalEvidenceObserved == 2 && state.terminalEvidenceConsumed == 2 &&
              state.terminalEvidenceDropped == 0 && state.terminalEvidenceCount == 0,
              "two local terminals reconcile exact source/coordinator counters");
        f.transport.watchFreshness = false;
        if (mode >= 1) f.transport.incoming.push_back(notification("recovered-job"));
        bool fetched = false;
        check(session.fetch(&f.job,&fetched) && fetched && session.prepare(f.job) && f.backend.prepares == 2,
              "next fetch adopts replacement canonical job without reconnect");
        check(f.job.jobId == (mode == 0 ? "queued-new-job" : "recovered-job") &&
              (mode != 1 || f.job.extra != oldJob.extra), "same-ID replacement at the current height is refused, then recovers (v28f)");
        f.backend.results = 1;
        check(session.search(f.job,200,1,&solutions) && solutions.size() == 1, "replacement search recovers");
        if (solutions.empty()) continue;
        bool delivered = false, valid = false; std::string error;
        check(session.submit(f.job,solutions[0],&error,&delivered,&valid) && delivered && valid &&
              session.finishSearch() && session.healthy(), "new owned proof submits successfully after local stale drops");
        check(f.transport.opens == 1 && f.transport.submissions == 1 && f.transport.submitIds.size() == 1 &&
              f.transport.submitIds[0] == requestBefore + 1 && f.source.submitCounters().accepted == 1 &&
              session.snapshot().progress.acceptedSuperseded == 0, "first actual request ID and acceptance remain exact");
    }
}
void freshnessDisconnectBeforeSubmit() {
    // Each injected failure is present BEFORE the first send. Distinguish these
    // local outcomes from existing after-send EOF/AcceptedSuperseded fixtures.
    for (unsigned mode=0; mode<10; ++mode) {
        Fixture f; f.backend.results = 3;
        PearlRunSession session(f.backend,f.source,policy(),Clock::read,&f.clock);
        check(f.start(session), "freshness failure fixture starts");
        std::vector<Solution> solutions;
        check(session.search(f.job,300,3,&solutions) && solutions.size() == 3,
              "freshness failure owns full three-proof batch");
        if (solutions.size() != 3) continue;
        const int requestBefore = PearlPoolSourceTestAccess::requestId(f.source);
        const auto wireBefore = f.transport.wire;
        const uint64_t clockBefore = f.clock.now;
        if (mode == 0) f.transport.incoming.push_back("{\"id\":null,\"method\":\"mining.notify\"");
        if (mode == 1) f.transport.incoming.push_back("");
        if (mode == 2) f.transport.failReadable = true;
        if (mode == 3) { f.transport.incoming.push_back(notification()); f.transport.failReceive = true; }
        if (mode == 4) { f.transport.incoming.push_back(notification()); f.transport.failNonblocking = true; }
        if (mode == 5) { f.transport.incoming.push_back(notification()); f.transport.failBlockingRestore = true; }
        if (mode == 6) f.transport.incoming.push_back("{\"id\":101,\"result\":true,\"error\":null}\n");
        if (mode == 7) {
            std::string flood;
            for (unsigned i=0;i<65;++i) flood += notification();
            f.transport.incoming.push_back(std::move(flood));
        }
        if (mode == 8) { f.transport.incoming.push_back(notification()); f.transport.maxReceiveBytes = 1; }
        if (mode == 9) f.transport.incoming.push_back(std::string(PearlPoolSource::kMaxInboundBufferedBytes,' '));
        f.transport.watchFreshness = true;
        bool delivered = true, valid = false; std::string error;
        check(session.submit(f.job,solutions[0],&error,&delivered,&valid) && !delivered && valid &&
              !error.empty() && session.retiredAfterDisconnect() && session.healthy(),
              "pre-send intake error is local and retires remaining batch without quarantine");
        checkFreshnessDispatch(f.transport);
        check(f.clock.now == clockBefore && !f.transport.open_ && f.transport.opens == 1 &&
              f.transport.submissions == 0 && f.transport.wire == wireBefore &&
              PearlPoolSourceTestAccess::requestId(f.source) == requestBefore &&
              noPoolOutcomes(f.source.submitCounters()), "bounded pre-send failure has no request/bytes/counter effect");
        check(session.finishSearch() && session.healthy(), "disconnected batch ledger reconciles after local terminal");
        const auto state = session.snapshot();
        check(!state.sourceQuarantined && state.sourceIntegrityFlags == 0 && state.sourceEventsInvalid == 0 &&
              state.progress.submitAttempts == 1 && state.progress.submissionFailures == 1 &&
              state.progress.submitted == 0 && state.progress.accepted == 0 && state.progress.stale == 0 &&
              state.progress.unknown == 0 && state.progress.pending == 0 &&
              state.progress.dropsFor(PearlProgressReason::SessionSuperseded) == 2 &&
              state.terminalEvidenceObserved == 1 && state.terminalEvidenceConsumed == 1 &&
              state.terminalEvidenceDropped == 0 && state.terminalEvidenceCount == 0 && f.backend.verifies == 1,
              "local terminal precedes disconnect, exactly two undispatched proofs retired, no fake ACK");
        const auto source = f.source.pearlSourceObservability()->snapshot();
        check(!source.failClosed && !source.submitInFlight && !source.issuedJobBound &&
              !source.transportConnected, "source releases in-flight/issued ownership on disconnect");
        f.transport.watchFreshness = false;
        f.transport.failReadable = f.transport.failReceive = false;
        f.transport.failNonblocking = f.transport.failBlockingRestore = false;
        f.transport.maxReceiveBytes = static_cast<std::size_t>(-1);
        f.backend.results = 1;
        bool fetched = false;
        check(session.fetch(&f.job,&fetched) && fetched && session.prepare(f.job) && f.backend.prepares == 2 &&
              session.search(f.job,400,1,&solutions) && solutions.size() == 1,
              "fresh connection and canonical search recover after bounded intake error");
        if (solutions.empty()) continue;
        delivered = false; valid = false;
        check(session.submit(f.job,solutions[0],&error,&delivered,&valid) && delivered && valid &&
              session.finishSearch() && session.healthy(), "reconnected fresh proof submits, old batch never replayed");
        check(f.transport.opens == 2 && f.transport.submissions == 1 && f.transport.submitIds.size() == 1 &&
              f.transport.submitIds[0] == requestBefore + 1 && f.source.submitCounters().accepted == 1,
              "recovery sends one fresh request with unconsumed ID");
    }
}
class RetirementWriter final : public PearlLocalRetirementWriter {
 public:
    bool partial = false, badFlush = false, badClose=false;
    unsigned writes = 0, flushes = 0, closes=0;
    std::string last,all;
    PearlLocalRetirementJsonSink *reenterSink = nullptr;
    const PearlLocalRetirement *reenterRecord = nullptr;
    bool reenterOnFlush = false, reentered = false, reenterResult = true;
    std::size_t write(const char *bytes, std::size_t size) noexcept override {
        ++writes;
        try { last.assign(bytes, partial ? size / 2 : size); all+=last; }
        catch (...) { return 0; }
        if (reenterSink && !reenterOnFlush) {
            reentered = true; reenterResult = reenterSink->retain(*reenterRecord);
        }
        return partial ? size / 2 : size;
    }
    bool flush() noexcept override {
        ++flushes;
        if (reenterSink && reenterOnFlush) {
            reentered = true; reenterResult = reenterSink->retain(*reenterRecord);
        }
        return !badFlush;
    }
    bool close() noexcept override { ++closes; return !badClose; }
};
PearlLocalRetirement retirementRecord(uint64_t token = 1) {
    PearlLocalRetirement r;
    auto &e = r.terminal.event;
    r.terminal.observation = token; r.terminal.trust = PearlRawTerminalTrust::Trusted;
    e.eventToken = e.operationToken = token; e.timestampMs = 1000;
    e.sessionToken = 11; e.authorizationToken = 12; e.jobToken = 13;
    e.jobIdentity.bytes[0] = 1; e.kind = PearlSourceEventKind::SubmitTerminal;
    e.delivery = PearlSourceDelivery::NotSent; e.terminal = PearlSourceTerminal::LocalFailure;
    e.terminalEvidence = PearlSourceTerminalEvidence::LocalPreDeliveryFailure;
    e.reason = PearlSourceReason::Stale;
    r.binding.token = token; r.binding.job = {11,12,13,e.jobIdentity};
    r.binding.searchGeneration = 20; r.binding.proof.nonce = 0;
    r.binding.proof.proofBytes = 3; r.binding.proof.fingerprint.bytes[0] = 2;
    r.candidate = r.submitGeneration = r.expectedOperation = token; r.ordinal = 1;
    return r;
}
void localRetirementSchemaAndFaults() {
    check(pearlLocalRetirementAdmission(nullptr,false,false), "absent diagnostic leaves default unchanged");
    check(pearlLocalRetirementAdmission("1",true,true), "explicit diagnostic admission");
    for (const char *value : {"", "0", "true", "1 "})
        check(!pearlLocalRetirementAdmission(value,true,true), "malformed diagnostic opt-in refused");
    check(!pearlLocalRetirementAdmission("1",false,true) &&
          !pearlLocalRetirementAdmission("1",true,false), "diagnostic requires canonical explicit transcript");
    const auto good = retirementRecord();
    check(pearlLocalRetirementValid(good), "known typed local retirement valid");
    for (unsigned mode=0; mode<18; ++mode) {
        auto bad = good; auto &e = bad.terminal.event;
        switch (mode) {
            case 0: bad.terminal.trust = PearlRawTerminalTrust::Untrusted; break;
            case 1: e.kind = PearlSourceEventKind::SubmitDispatched; break;
            case 2: e.sessionToken++; break;
            case 3: e.authorizationToken++; break;
            case 4: e.jobToken++; break;
            case 5: e.jobIdentity.bytes[1]++; break;
            case 6: e.operationToken++; break;
            case 7: e.delivery = PearlSourceDelivery::PossiblySent; break;
            case 8: e.terminal = PearlSourceTerminal::Unknown; break;
            case 9: e.terminalEvidence = PearlSourceTerminalEvidence::CorrelatedRemoteOutcome; break;
            case 10: e.acknowledged = 1; break;
            case 11: e.superseded = 1; break;
            case 12: e.reason = static_cast<PearlSourceReason>(255); break;
            case 13: e.reserved1 = 1; break;
            case 14: bad.binding.token = 0; break;
            case 15: bad.binding.searchGeneration = 0; break;
            case 16: bad.binding.proof.fingerprint = {}; break;
            case 17: bad.submitGeneration = 0; break;
        }
        RetirementWriter writer; PearlLocalRetirementJsonSink sink(writer);
        check(!sink.retain(bad) && sink.failure() && !sink.retain(good) && writer.writes == 0,
              "invalid local evidence fails closed without bytes or repair retry");
    }
    {
        RetirementWriter writer; PearlLocalRetirementJsonSink sink(writer);
        check(sink.retain(good) && sink.records() == 1 && writer.flushes == 1, "typed JSON retained once");
        check(writer.last.find("\"reason\":\"stale\"") != std::string::npos &&
              writer.last.find("\"delivery\":\"not_sent\"") != std::string::npos &&
              writer.last.find("\"nonce\":\"0000000000000000\"") != std::string::npos &&
              writer.last.find("request_id") == std::string::npos && writer.last.find("wallet") == std::string::npos &&
              writer.last.find("plain_proof") == std::string::npos, "record contains typed binding only, no wire/proof/payout");
        check(!sink.retain(good) && writer.writes == 1, "duplicate evidence is not written twice");
    }
    for (unsigned mode=0; mode<2; ++mode) {
        RetirementWriter writer; writer.partial = mode == 0; writer.badFlush = mode == 1;
        PearlLocalRetirementJsonSink sink(writer);
        check(!sink.retain(good) && sink.failure() && sink.records() == 0 &&
              !sink.retain(retirementRecord(2)) && writer.writes == 1,
              "partial write/flush failure remains sticky and preserves failed bytes without retry");
    }
    for (unsigned mode=0; mode<2; ++mode) {
        RetirementWriter writer; PearlLocalRetirementJsonSink sink(writer);
        writer.reenterSink = &sink; writer.reenterRecord = &good;
        writer.reenterOnFlush = mode == 1;
        check(!sink.retain(good) && writer.reentered && !writer.reenterResult &&
              sink.failure() && !std::strcmp(sink.failure(),"reentry") &&
              sink.records()==0 && sink.bytes()==0 && writer.writes==1 &&
              writer.flushes==mode && !sink.retain(retirementRecord(2)) && writer.writes==1,
              "writer write/flush reentry cannot duplicate evidence or report outer success");
    }
    {
        RetirementWriter writer; PearlLocalRetirementJsonSink sink(writer);
        for (uint64_t i=1; i<=PearlLocalRetirementJsonSink::maxRecords; ++i)
            check(sink.retain(retirementRecord(i)), "bounded diagnostic record fills");
        check(!sink.retain(retirementRecord(PearlLocalRetirementJsonSink::maxRecords+1)) &&
              writer.writes == PearlLocalRetirementJsonSink::maxRecords && sink.failure() &&
              sink.bytes() <= PearlLocalRetirementJsonSink::maxBytes, "diagnostic capacity fails without extra write");
    }
}
void localRetirementSessionIntegration() {
    for (unsigned mode=0; mode<4; ++mode) {
        Fixture f; RetirementWriter writer;
        writer.partial = mode == 1; writer.badFlush = mode == 2;
        PearlLocalRetirementJsonSink sink(writer);
        PearlRunSession session(f.backend,f.source,policy(),Clock::read,&f.clock,&sink);
        check(f.start(session), "typed-retirement session starts");
        const auto issued = f.source.pearlSourceObservability()->snapshot();
        std::vector<Solution> solutions;
        check(session.search(f.job,500,1,&solutions) && solutions.size()==1, "typed-retirement proof owned");
        if (solutions.empty()) continue;
        const int before = PearlPoolSourceTestAccess::requestId(f.source);
        if (mode == 3) f.transport.incoming.push_back("partial");
        else f.transport.incoming.push_back(notification("newer-job", '1', "139054"));
        bool delivered=false,valid=false; std::string error;
        const bool ok = session.submit(f.job,solutions[0],&error,&delivered,&valid);
        check(valid && !delivered && f.transport.submissions==0 &&
              PearlPoolSourceTestAccess::requestId(f.source)==before && noPoolOutcomes(f.source.submitCounters()),
              "typed sink never changes wire request or pool accounting");
        check(session.snapshot().progress.submissionFailures == 1,
              "typed evidence never suppresses existing local-failure accounting");
        if (mode == 1 || mode == 2) {
            check(!ok && !session.healthy() && session.localRetirementFailed() && writer.writes==1,
                  "sink failure explicitly stops owning session, never replays proof");
            check(!session.submit(f.job,solutions[0],&error,&delivered,&valid) && writer.writes==1,
                  "failed diagnostic cannot revive consumed proof");
        } else {
            check(ok && session.healthy() && session.finishSearch() && sink.records()==1 &&
                  !session.localRetirementFailed(), "typed local retirement preserves healthy reconciliation");
            check(writer.last.find("\"job_token\":"+std::to_string(issued.issuedJobToken)+",") != std::string::npos &&
                  writer.last.find("\"proof_token\":"+std::to_string(solutions[0].pearlProofToken)+",") != std::string::npos &&
                  writer.last.find("\"reason\":\""+std::string(mode==3?"protocol":"stale")+"\"") != std::string::npos,
                  "artifact binds original issued proof/job and trusted reason, including disconnect");
        }
    }
    {
        Fixture f; RetirementWriter writer; PearlLocalRetirementJsonSink sink(writer);
        PearlRunSession session(f.backend,f.source,policy(),Clock::read,&f.clock,&sink);
        check(f.start(session), "accepted nonlocal diagnostic fixture");
        std::vector<Solution> solutions;
        check(session.search(f.job,700,1,&solutions) && solutions.size()==1, "accepted proof search");
        if (solutions.empty()) return;
        bool delivered=false,valid=false; std::string error;
        check(session.submit(f.job,solutions[0],&error,&delivered,&valid) && delivered && valid &&
              session.finishSearch() && writer.writes==0, "remote accepted outcome is never local-retirement evidence");
    }
    {
        struct ReenterSink final : PearlLocalRetirementSink {
            PearlRunSession *session = nullptr;
            unsigned calls = 0;
            bool result = true;
            bool retain(const PearlLocalRetirement &) noexcept override {
                ++calls; result = session->finishSearch(); return true;
            }
        } sink;
        Fixture f;
        PearlRunSession session(f.backend,f.source,policy(),Clock::read,&f.clock,&sink);
        sink.session = &session;
        check(f.start(session), "reentrant sink session starts");
        std::vector<Solution> solutions;
        check(session.search(f.job,800,1,&solutions) && solutions.size()==1, "reentrant sink owns proof");
        if (solutions.empty()) return;
        f.transport.incoming.push_back(notification("reentry-new-job", '1', "139054"));
        bool delivered=false,valid=false; std::string error;
        check(!session.submit(f.job,solutions[0],&error,&delivered,&valid) &&
              sink.calls==1 && !sink.result && session.localRetirementFailed() &&
              !session.healthy() && valid && !delivered && f.transport.submissions==0 &&
              session.snapshot().progress.submissionFailures==1,
              "mutating sink reentry fails owning session despite callback claiming success");
        check(!session.submit(f.job,solutions[0],&error,&delivered,&valid) && sink.calls==1,
              "reentrant diagnostic failure cannot replay proof or callback");
    }
}
void localRetirementExclusiveFile() {
#ifndef _WIN32
    char directory[] = "/tmp/pearl-local-retirement-XXXXXX";
    if (!mkdtemp(directory)) { check(false,"create private diagnostic test directory"); return; }
    const std::string file = std::string(directory)+"/evidence.jsonl";
    const std::string link = std::string(directory)+"/symlink.jsonl";
    {
        PearlLocalRetirementFile writer;
        check(writer.open(file.c_str()), "exclusive diagnostic file opens once");
        const char text[] = "preserve failed evidence\n";
        check(writer.write(text,sizeof(text)-1)==sizeof(text)-1 && writer.flush() &&
              writer.close() && writer.close(), "diagnostic file flush/close idempotent");
        check(!writer.open(file.c_str()), "file object cannot be reused after close");
        PearlLocalRetirementFile existing;
        check(!existing.open(file.c_str()), "existing evidence is never overwritten");
        check(::symlink(file.c_str(),link.c_str())==0, "create private test symlink");
        PearlLocalRetirementFile linked;
        check(!linked.open(link.c_str()), "diagnostic final symlink refused");
        FILE *read = std::fopen(file.c_str(),"rb");
        char bytes[sizeof(text)] = {};
        check(read && std::fread(bytes,1,sizeof(text)-1,read)==sizeof(text)-1 &&
              !std::memcmp(bytes,text,sizeof(text)-1), "old artifact bytes preserved");
        if (read) std::fclose(read);
    }
    std::remove(link.c_str()); std::remove(file.c_str()); ::rmdir(directory);
#endif
}
void completionHashAndWriterFaults() {
    for (std::size_t size : {0U,1U,63U,64U,1023U,1024U,1025U,2048U,3073U,8192U,17411U}) {
        std::string bytes(size,'x');
        for (std::size_t i=0; i<size; ++i) bytes[i]=static_cast<char>((i*73+19)&255);
        uint8_t expected[32]; pearl::b3::hash(nullptr,reinterpret_cast<const uint8_t *>(bytes.data()),size,expected);
        for (std::size_t fragment : {1U,63U,1024U,1171U}) {
            RetirementWriter writer; PearlEvidenceStream stream;
            check(stream.configure(&writer,128ULL*1024*1024),"stream configured");
            for (std::size_t i=0; i<size; i+=fragment)
                check(stream.append(bytes.data()+i,std::min(fragment,size-i)),"fragmented stream append");
            PearlEvidenceDigest digest;
            check(stream.finish(&digest) && digest.closed && digest.bytes==size &&
                  !std::memcmp(digest.digest.data(),expected,32) && writer.all==bytes,
                  "stream BLAKE3 matches existing one-shot across chunk and tree boundaries");
            check(!stream.append("x",1) && !stream.finish(&digest),"sealed evidence stream cannot append or refinish");
        }
    }
    for (unsigned mode=0; mode<4; ++mode) {
        RetirementWriter writer; PearlEvidenceStream stream;
        check(stream.configure(&writer,4),"fault stream configured");
        writer.partial=mode==0; writer.badFlush=mode==1; writer.badClose=mode==2;
        const bool appended=stream.append("test",4);
        if (mode<2) check(!appended,"partial/flush stream append fails");
        else check(appended,"complete write before close/capacity fault");
        if (mode==3) check(!stream.append("x",1),"evidence byte cap is hard");
        PearlEvidenceDigest digest;
        check(!stream.finish(&digest) && !digest.closed && writer.closes==1,
              "historical append/flush/close/capacity failure cannot complete, close attempted");
    }
    for (unsigned mode=0; mode<3; ++mode) {
        struct ReenterWriter final : PearlLocalRetirementWriter {
            PearlEvidenceStream *stream=nullptr;
            unsigned mode=0,writes=0,closes=0;
            bool nested=true;
            void enter() noexcept { nested=stream->append("x",1); }
            std::size_t write(const char *,std::size_t size) noexcept override {
                ++writes; if (mode==0) enter(); return size;
            }
            bool flush() noexcept override { if (mode==1) enter(); return true; }
            bool close() noexcept override { ++closes; if (mode==2) enter(); return true; }
        } writer;
        PearlEvidenceStream stream; writer.stream=&stream; writer.mode=mode;
        check(stream.configure(&writer,128),"reentry stream configured");
        const bool appended=stream.append("test",4);
        PearlEvidenceDigest digest;
        check(appended==(mode==2) && !stream.finish(&digest) && !digest.closed &&
              !writer.nested && writer.writes==1 && writer.closes==1,
              "write/flush/close reentry cannot fabricate complete stream");
    }
}
void nativeCompletionIntegration() {
    const char *id="0123456789abcdef0123456789abcdef";
    for (unsigned mode=0; mode<4; ++mode) {
        RetirementWriter wire,local,completion;
        Fixture f;
        check(f.source.enableDiagnosticCompletion(&wire),"source evidence enabled before operations");
        PearlLocalRetirementJsonSink sink(local);
        check(sink.beginRun(id),"local stream starts with exact run ID");
        PearlRunSession session(f.backend,f.source,policy(),Clock::read,&f.clock,&sink);
        if (mode==0) f.backend.results=0;
        if (mode==3) f.backend.results=3;
        check(f.start(session),"completion session starts");
        const auto previousReport=session.snapshot();
        const uint64_t attempts=mode==3?3:1;
        std::vector<Solution> solutions;
        check(session.search(f.job,900,attempts,&solutions),"completion search");
        uint64_t failed=0;
        if (!solutions.empty()) {
            if (mode==2) f.transport.incoming.push_back(notification("completion-new-job", '1', "139054"));
            if (mode==3) f.transport.partialSubmit=true;
            bool delivered=false,valid=false; std::string error;
            check(session.submit(f.job,solutions[0],&error,&delivered,&valid) && valid,"terminal completes before finish");
            if (!delivered) ++failed;
        }
        check(session.finishSearch(),"ordinary proof ledger reconciles before completion");
        const auto before=session.snapshot();
        const auto sourceBefore=f.source.pearlSourceObservability()->snapshot();
        check(session.healthy() && sourceBefore.issuedJobBound==(mode!=3) &&
              !sourceBefore.submitInFlight && !before.sourceQuarantined &&
              before.progress.attemptsCompleted==attempts,
              "normal finished search releases owner while source retains issued binding until close");
        PearlRunFinalEvidence final;
        check(session.finishEvidenceRun(sink.records(),&final) && final.complete &&
              !final.source.transportConnected && !final.source.issuedJobBound &&
              !final.source.submitInFlight && !final.source.queued && !final.owner.sourceQuarantined &&
              final.owner.terminalEvidenceObserved==final.owner.terminalEvidenceConsumed &&
              final.owner.sourceEventsDrained>=before.sourceEventsDrained && wire.closes==1,
              "explicit shutdown drains source events while session remains attached");
        check(final.pool.accepted==(mode==1?1U:0U) && sink.records()==(mode==2?1U:0U) &&
              final.owner.liveness.possiblyDeliveredUnknowns==(mode==3?1U:0U),
              "accepted/local/partial Unknown accounting distinctions retained exactly");
        check(previousReport.progress.attemptsCompleted==0 && final.owner.progress.attemptsCompleted==attempts &&
              previousReport.progress.accepted==0 && (mode!=1 || final.pool.accepted==1),
              "final counters include completed work/outcomes after earlier report snapshot");
        if (mode==3) check(final.owner.progress.dropsFor(PearlProgressReason::SessionSuperseded)==2,
                          "disconnected search counts all attempts while retiring two unsubmitted proofs");
        if (mode!=3) check(final.owner.sourceEventsDrained>before.sourceEventsDrained,
                          "normal connection close contributes final drained events");
        PearlEvidenceDigest localDigest;
        check(sink.finish(&localDigest) && localDigest.closed && local.closes==1,"local writer flush and close checked");
        uint8_t expected[32];
        pearl::b3::hash(nullptr,reinterpret_cast<const uint8_t *>(local.all.data()),local.all.size(),expected);
        check(!std::memcmp(expected,localDigest.digest.data(),32),"local digest covers run header and exact record bytes");
        pearl::b3::hash(nullptr,reinterpret_cast<const uint8_t *>(wire.all.data()),wire.all.size(),expected);
        check(!std::memcmp(expected,final.wire.digest.data(),32),"wire digest covers exact existing diagnostics including newline");
        PearlNativeCompletionSink end(completion);
        check(end.finish(id,final,localDigest,sink.records(),0,failed,attempts,attempts) && completion.closes==1 &&
              completion.last.find("\"native_exit_required\":0")!=std::string::npos &&
              completion.last.find("\"native_exit_code\"")==std::string::npos,
              "complete final counters require future actual native exit, not preclaimed exit status");
        check(!end.finish(id,final,localDigest,sink.records(),0,failed,attempts,attempts) && completion.writes==1,
              "native completion writer is one-shot");
        for (unsigned fault=0; fault<6; ++fault) {
            RetirementWriter bad; bad.partial=fault==0; bad.badFlush=fault==1; bad.badClose=fault==2;
            PearlNativeCompletionSink broken(bad);
            auto altered=final; auto digest=localDigest;
            if (fault==3) altered.owner.progress.submissionFailures++;
            if (fault==4) digest.closed=false;
            if (fault==5) altered.owner.terminalEvidenceDropped=1;
            check(!broken.finish(id,altered,digest,sink.records(),0,failed,attempts,attempts) &&
                  !broken.finish(id,final,localDigest,sink.records(),0,failed,attempts,attempts),
                  "completion malformed evidence/partial write/flush/close cannot recover to success");
        }
        for (unsigned stage=0; stage<3; ++stage) {
            struct ReenterFinal final : PearlLocalRetirementWriter {
                PearlNativeCompletionSink *sink=nullptr;
                const PearlRunFinalEvidence *final=nullptr;
                const PearlEvidenceDigest *digest=nullptr;
                uint64_t records=0,failed=0,attempts=0;
                unsigned stage=0,writes=0,closes=0;
                bool nested=true;
                void enter() noexcept {
                    nested=sink->finish("0123456789abcdef0123456789abcdef",*final,*digest,
                                        records,0,failed,attempts,attempts);
                }
                std::size_t write(const char *,std::size_t size) noexcept override {
                    ++writes; if (stage==0) enter(); return size;
                }
                bool flush() noexcept override { if (stage==1) enter(); return true; }
                bool close() noexcept override { ++closes; if (stage==2) enter(); return true; }
            } writer;
            PearlNativeCompletionSink sealed(writer); writer.sink=&sealed;
            writer.final=&final; writer.digest=&localDigest; writer.records=sink.records();
            writer.failed=failed; writer.attempts=attempts; writer.stage=stage;
            check(!sealed.finish(id,final,localDigest,sink.records(),0,failed,attempts,attempts) &&
                  !writer.nested && writer.writes==1 && writer.closes==1,
                  "completion callback reentry is sticky despite claimed write/flush/close success");
        }
        check(!session.finishEvidenceRun(sink.records(),&final) && !session.healthy(),
              "completed session cannot be reused or finalized twice");
        Job later; check(!f.source.fetch(&later) && f.transport.opens==1,"completed source cannot reconnect");
    }
    for (unsigned mode=0; mode<5; ++mode) {
        RetirementWriter wire,local; Fixture f;
        check(f.source.enableDiagnosticCompletion(&wire),"fault source enabled");
        PearlLocalRetirementJsonSink sink(local); check(sink.beginRun(id),"fault local start");
        PearlRunSession session(f.backend,f.source,policy(),Clock::read,&f.clock,&sink);
        check(f.start(session),"fault completion starts");
        std::vector<Solution> solutions;
        check(session.search(f.job,1000,1,&solutions) && solutions.size()==1,"fault owned search");
        if (solutions.empty()) continue;
        if (mode==0) {
            PearlRunFinalEvidence final;
            check(!session.finishEvidenceRun(0,&final) && !final.complete,
                  "active proof/search cannot be completed by shutdown"); continue;
        }
        f.transport.incoming.push_back(notification("fault-new-job", '1', "139054"));
        bool delivered=false,valid=false; std::string error;
        check(session.submit(f.job,solutions[0],&error,&delivered,&valid) && !delivered && valid &&
              session.finishSearch(),"fault case local terminal retained first");
        if (mode==2) wire.badClose=true;
        if (mode==3) wire.badFlush=true;
        if (mode==4) local.badClose=true;
        PearlRunFinalEvidence final;
        const bool done=session.finishEvidenceRun(mode==1?0:sink.records(),&final);
        if (mode<4) check(!done && !final.complete,"missing local record or wire close/flush failure refuses completion");
        else {
            PearlEvidenceDigest digest;
            check(done && !sink.finish(&digest) && !digest.closed,"local final close failure remains incomplete");
        }
    }
    for (unsigned mode=0; mode<2; ++mode) {
        struct ClosingCallback final : PearlLocalRetirementWriter {
            RetirementWriter file;
            PearlRunSession *session=nullptr;
            unsigned mode=0,calls=0;
            bool nested=true;
            std::size_t write(const char *p,std::size_t n) noexcept override { return file.write(p,n); }
            bool flush() noexcept override { return file.flush(); }
            bool close() noexcept override {
                ++calls;
                if (mode==0) { Job job; bool fetched=false; nested=session->fetch(&job,&fetched); }
                else { PearlRunFinalEvidence final; nested=session->finishEvidenceRun(0,&final); }
                return file.close(); // claimed successful I/O must not mask reentry
            }
        } wire;
        RetirementWriter local; Fixture f;
        check(f.source.enableDiagnosticCompletion(&wire),"closing-callback source configured");
        PearlLocalRetirementJsonSink sink(local); check(sink.beginRun(id),"closing-callback run starts");
        PearlRunSession session(f.backend,f.source,policy(),Clock::read,&f.clock,&sink);
        wire.session=&session; wire.mode=mode; f.backend.results=0;
        check(f.start(session),"closing-callback session starts");
        std::vector<Solution> solutions;
        check(session.search(f.job,1100,1,&solutions) && session.finishSearch(),"closing-callback owner quiescent");
        PearlRunFinalEvidence final;
        check(!session.finishEvidenceRun(0,&final) && !final.complete && !wire.nested &&
              wire.calls==1 && !session.healthy() && f.transport.opens==1,
              "fetch or nested-finalize callback cannot mutate session during evidence finish or mask failure");
    }
}
}  // namespace
int main() {
    normal(1, 1); normal(2, 2); normal(0, 2);
    for (unsigned field = 0; field < 7; ++field) tamper(field);
    replayAndUnconsumed(); failuresAndCleanup(); workBoundsAndSourceFailure();
    sustainedTerminalDrain();
    outputSequenceAndUnits(); outputAliases();
    disconnectAndReplacement();
    queuedReplacementBeforeSubmit();
    freshnessDisconnectBeforeSubmit();
    localRetirementSchemaAndFaults();
    localRetirementSessionIntegration();
    localRetirementExclusiveFile();
    completionHashAndWriterFaults();
    nativeCompletionIntegration();
    if (failures) return 1;
    std::puts("Pearl run-session host integration PASS (fake proofs/device/transport; no official acceptance)");
    return 0;
}
