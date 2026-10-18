#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "../src/core/pearl_pool.h"
#include "../src/algos/pearl-pow/job_publication.h"

namespace om {

struct PearlPoolSourceTestAccess {
    static bool connect(PearlPoolSource &source) { return source.connectAndLogin(); }
    static bool current(const PearlPoolSource &source, Job *job) {
        return source.currentJob(job);
    }
    static bool pending(const PearlPoolSource &source) {
        return source.havePendingTarget_;
    }
    static bool issued(const PearlPoolSource &source) { return source.issued_.valid; }
    static int requestId(const PearlPoolSource &source) { return source.id_; }
    static void buffered(PearlPoolSource &source, std::string bytes) {
        source.pending_ = std::move(bytes);
    }
    static FILE *attachTranscript(PearlPoolSource &source) {
        source.transcript_ = std::tmpfile();
        return source.transcript_;
    }
    static PearlPoolSubmitCounters &counters(PearlPoolSource &source) {
        return source.counters_;
    }
};

}  // namespace om

namespace {

int failures = 0;

om::PearlSourcePolicy sourcePolicy() {
    return {om::PearlNetwork::Mainnet,
            om::PearlTargetSemantics::BaseBeforePenalty};
}

bool canonicalJobsMatch(const om::Job &a, const om::Job &b) {
    bool same = false;
    return om::pearlPoolCanonicalJobsMatch(a, b, sourcePolicy(), &same) && same;
}

bool canonicalDigest(const om::Job &job, om::PearlJobIdentityDigest *out) {
    om::PearlPoolCanonicalIdentity identity;
    if (!om::pearlPoolCanonicalIdentityForJob(job, sourcePolicy(), &identity))
        return false;
    *out = identity.sourceDigest;
    return true;
}

void check(bool condition, const char *name) {
    std::printf("  %-68s %s\n", name, condition ? "ok" : "FAIL");
    if (!condition) ++failures;
}

class FakeClock final : public om::PearlSourceMonotonicClock {
   public:
    uint64_t nowMs() noexcept override { return now; }
    void advance(uint64_t amount) { now += amount; }
    uint64_t now = 1000;
};

struct ReceiveAction {
    om::PearlPoolTransferStatus status =
        om::PearlPoolTransferStatus::Bytes;
    std::string bytes;
};

struct SendAction {
    om::PearlPoolTransferStatus status =
        om::PearlPoolTransferStatus::Bytes;
    std::size_t maximum = std::numeric_limits<std::size_t>::max();
};

class FakeTransport final : public om::PearlPoolTransport {
   public:
    explicit FakeTransport(FakeClock &clock) : clock_(clock) {}

    om::PearlPoolConnectStatus open(const std::string &, int) override {
        ++openCalls;
        om::PearlPoolConnectStatus result = om::PearlPoolConnectStatus::Connected;
        if (!openResults.empty()) {
            result = openResults.front();
            openResults.pop_front();
        }
        open_ = result == om::PearlPoolConnectStatus::Connected;
        return result;
    }

    bool isOpen() const override { return open_; }
    void close() override {
        if (open_) ++closeCalls;
        open_ = false;
    }

    om::PearlPoolWaitStatus waitReadable(int milliseconds) override {
        maximumReadWait = std::max(maximumReadWait, milliseconds);
        if (failNextReadWait) {
            failNextReadWait = false;
            return om::PearlPoolWaitStatus::Error;
        }
        if (!open_) return om::PearlPoolWaitStatus::Error;
        if (!receives.empty()) return om::PearlPoolWaitStatus::Ready;
        if (milliseconds > 0) clock_.advance(static_cast<uint64_t>(milliseconds));
        return om::PearlPoolWaitStatus::Timeout;
    }

    om::PearlPoolWaitStatus waitWritable(int milliseconds) override {
        if (!open_) return om::PearlPoolWaitStatus::Error;
        if (forceWriteTimeout) {
            if (milliseconds > 0)
                clock_.advance(static_cast<uint64_t>(milliseconds));
            return om::PearlPoolWaitStatus::Timeout;
        }
        return om::PearlPoolWaitStatus::Ready;
    }

    om::PearlPoolTransfer receive(char *out, std::size_t maximum) override {
        ++receiveCalls;
        if (receives.empty())
            return {om::PearlPoolTransferStatus::WouldBlock, 0};
        ReceiveAction &action = receives.front();
        if (action.status != om::PearlPoolTransferStatus::Bytes) {
            const om::PearlPoolTransferStatus status = action.status;
            receives.pop_front();
            return {status, 0};
        }
        const std::size_t count = std::min(maximum, action.bytes.size());
        std::memcpy(out, action.bytes.data(), count);
        action.bytes.erase(0, count);
        if (action.bytes.empty()) receives.pop_front();
        return {om::PearlPoolTransferStatus::Bytes, count};
    }

    om::PearlPoolTransfer send(const char *data, std::size_t length) override {
        ++sendCalls;
        SendAction action;
        if (!sends.empty()) {
            action = sends.front();
            sends.pop_front();
        }
        if (action.status != om::PearlPoolTransferStatus::Bytes)
            return {action.status, 0};
        const std::size_t count = std::min(length, action.maximum);
        wire.append(data, count);
        sentFrame.append(data, count);
        if (!sentFrame.empty() && sentFrame.back() == '\n') {
            if (sentFrame.find("mining.submit") != std::string::npos) {
                // Reply/push becomes readable only after a complete submit.
                // Never preload future acknowledgments into preflight input.
                while (!afterSubmit.empty()) {
                    receives.push_back(std::move(afterSubmit.front()));
                    afterSubmit.pop_front();
                }
            }
            sentFrame.clear();
        }
        return {om::PearlPoolTransferStatus::Bytes, count};
    }

    bool setNonblocking(bool enabled) override {
        if (enabled) {
            ++sendOperations;
            if (failNextSetup) { failNextSetup = false; return false; }
            return open_;
        }
        if (failNextRestore) {
            failNextRestore = false;
            return false;
        }
        return open_;
    }

    void bytes(std::string data) {
        receives.push_back({om::PearlPoolTransferStatus::Bytes,
                            std::move(data)});
    }
    void afterSubmitBytes(std::string data) {
        afterSubmit.push_back({om::PearlPoolTransferStatus::Bytes, std::move(data)});
    }
    void afterSubmitEof() {
        afterSubmit.push_back({om::PearlPoolTransferStatus::EndOfStream, {}});
    }
    void afterSubmitError() {
        afterSubmit.push_back({om::PearlPoolTransferStatus::Error, {}});
    }
    void eof() {
        receives.push_back({om::PearlPoolTransferStatus::EndOfStream, {}});
    }
    void error() {
        receives.push_back({om::PearlPoolTransferStatus::Error, {}});
    }

    FakeClock &clock_;
    bool open_ = false;
    bool failNextRestore = false;
    bool failNextSetup = false;
    bool failNextReadWait = false;
    bool forceWriteTimeout = false;
    unsigned openCalls = 0;
    unsigned closeCalls = 0;
    unsigned sendCalls = 0;
    unsigned sendOperations = 0;
    unsigned receiveCalls = 0;
    int maximumReadWait = 0;
    std::string wire;
    std::string sentFrame;
    std::deque<om::PearlPoolConnectStatus> openResults;
    std::deque<ReceiveAction> receives;
    std::deque<ReceiveAction> afterSubmit;
    std::deque<SendAction> sends;
};

std::string hexByte(unsigned value) {
    static const char *digits = "0123456789abcdef";
    std::string out(2, '0');
    out[0] = digits[(value >> 4) & 15];
    out[1] = digits[value & 15];
    return out;
}

template <std::size_t N>
std::string bytesHex(const std::array<uint8_t, N> &bytes) {
    std::string out;
    out.reserve(N * 2);
    for (uint8_t byte : bytes) out += hexByte(byte);
    return out;
}

std::string headerHex(unsigned tag) {
    std::string out;
    for (unsigned i = 0; i < 76; ++i) out += hexByte((tag + i) & 255);
    return out;
}

std::string notify(const std::string &jobId, unsigned tag,
                   const std::string &target =
                       "000000000000000000000000000000000000000000000000000000000000ffff",
                   const std::string &height = "139053",
                   const std::string &certificate = "3") {
    return "{\"id\":null,\"method\":\"mining.notify\",\"params\":{"
           "\"job_id\":\"" + jobId + "\",\"header\":\"" +
           headerHex(tag) + "\",\"target\":\"" + target +
           "\",\"height\":" + height + ",\"cert_version\":" +
           certificate + "}}";
}

bool sameJob(const om::Job &a, const om::Job &b) {
    return std::memcmp(a.msg, b.msg, sizeof(a.msg)) == 0 &&
           std::memcmp(a.target, b.target, sizeof(a.target)) == 0 &&
           a.epoch == b.epoch && a.extra == b.extra && a.jobId == b.jobId &&
           a.valid == b.valid;
}

bool sameCanonicalIdentity(const om::PearlPoolCanonicalIdentity &a,
                           const om::PearlPoolCanonicalIdentity &b) {
    return om::pearlJobContextEqual(a.context, b.context) &&
           std::memcmp(a.rawTarget.v, b.rawTarget.v,
                       sizeof(a.rawTarget.v)) == 0 &&
           a.sourceDigest.bytes == b.sourceDigest.bytes;
}

bool sameCanonicalView(const om::PearlPoolCanonicalView &a,
                       const om::PearlPoolCanonicalView &b) {
    return sameCanonicalIdentity(a.identity, b.identity) &&
           std::memcmp(a.resolvedTarget.v, b.resolvedTarget.v,
                       sizeof(a.resolvedTarget.v)) == 0;
}

bool sameCanonicalFinalization(
    const om::PearlPoolCanonicalTargetFinalization &a,
    const om::PearlPoolCanonicalTargetFinalization &b) {
    return sameJob(a.job, b.job) &&
           sameCanonicalView(a.view, b.view) &&
           a.consumesPendingTarget == b.consumesPendingTarget;
}

const std::string authReply = "{\"id\":1,\"error\":null,\"result\":true}";

std::vector<om::PearlSourceEvent> drainAll(
    om::PearlSourceObservabilityMailbox &mailbox, std::size_t bound = 7) {
    std::vector<om::PearlSourceEvent> events;
    om::PearlSourceEvent batch[16];
    for (;;) {
        const std::size_t count = mailbox.drain(batch, bound);
        events.insert(events.end(), batch, batch + count);
        if (count == 0) break;
    }
    return events;
}

std::size_t countKind(const std::vector<om::PearlSourceEvent> &events,
                      om::PearlSourceEventKind kind) {
    return static_cast<std::size_t>(std::count_if(
        events.begin(), events.end(),
        [kind](const om::PearlSourceEvent &event) { return event.kind == kind; }));
}

const om::PearlSourceEvent *lastKind(
    const std::vector<om::PearlSourceEvent> &events,
    om::PearlSourceEventKind kind) {
    for (auto it = events.rbegin(); it != events.rend(); ++it)
        if (it->kind == kind) return &*it;
    return nullptr;
}

bool exactKinds(
    const std::vector<om::PearlSourceEvent> &events,
    std::initializer_list<om::PearlSourceEventKind> expected) {
    if (events.size() != expected.size()) return false;
    return std::equal(events.begin(), events.end(), expected.begin(),
                      [](const om::PearlSourceEvent &event,
                         om::PearlSourceEventKind kind) {
                          return event.kind == kind;
                      });
}

void prime(FakeTransport &transport, const std::string &job = "job-1",
           unsigned tag = 1, bool fragmented = false) {
    const std::string first = notify(job, tag) + "\n" + authReply + "\n";
    if (fragmented) {
        transport.bytes(first.substr(0, 19));
        transport.bytes(first.substr(19, 37));
        transport.bytes(first.substr(56));
    } else {
        transport.bytes(first);
    }
}

om::Solution solution() {
    om::Solution out;
    out.nonce = 7;
    out.hit[0] = 11;
    out.extra = "proof";
    return out;
}

void canonicalContextParserTest() {
    std::puts("explicit-policy canonical pool parser:");
    const om::PearlSourcePolicy policy{
        om::PearlNetwork::Mainnet,
        om::PearlTargetSemantics::BaseBeforePenalty};
    const std::string target =
        "000000000000000000000000000000000000000000000000000000000000ffff";
    const std::string canonicalLine =
        notify("canonical-job", 19, target, "139053", "3");
    om::Job canonical;
    std::string error = "stale error";
    om::PearlJobContextError code = om::PearlJobContextError::InvalidJob;
    const bool parsed = om::pearlPoolCanonicalJobFromNotify(
        canonicalLine, policy, &canonical, &error, &code);
    om::PearlJobContext context;
    const bool validated = parsed && om::validatePearlJobContext(
                                        canonical, &policy, &context, &error,
                                        &code);
    check(validated && canonical.valid && canonical.epoch == 139053 &&
              canonical.jobId == "canonical-job" &&
              context.network == om::PearlNetwork::Mainnet &&
              context.targetSemantics ==
                  om::PearlTargetSemantics::BaseBeforePenalty &&
              context.height == canonical.epoch &&
              context.certificateVersion ==
                  om::PearlCertificateVersion::V3 &&
              context.transportJobId == canonical.jobId && error.empty() &&
              code == om::PearlJobContextError::None,
          "canonical parser binds policy, height, certificate, and exact ID");
    om::Job legacy;
    check(om::pearlPoolJobFromNotify(canonicalLine, &legacy) &&
              legacy.valid && legacy.epoch == 0 && legacy.jobId.empty() &&
              legacy.extra.find("pearl-job-v1:") != 0,
          "legacy fixture parser remains distinct from canonical source jobs");

    struct ScheduleCase {
        const char *height;
        const char *certificate;
        bool accepted;
    };
    static const ScheduleCase schedule[] = {
        {"71934", "2", false}, {"71935", "2", true},
        {"96250", "2", true},  {"96251", "2", true},
        {"98999", "2", true},  {"98999", "3", false},
        {"99000", "2", false}, {"99000", "3", true},
        {"18446744073709551615", "3", true},
    };
    bool scheduleExact = true;
    for (const ScheduleCase &item : schedule) {
        om::Job out = canonical;
        const om::Job before = out;
        error = "stale";
        code = om::PearlJobContextError::None;
        const bool accepted = om::pearlPoolCanonicalJobFromNotify(
            notify("boundary", 20, target, item.height, item.certificate),
            policy, &out, &error, &code);
        scheduleExact = scheduleExact && accepted == item.accepted;
        if (accepted) {
            scheduleExact = scheduleExact && out.valid && error.empty() &&
                            code == om::PearlJobContextError::None;
        } else {
            scheduleExact = scheduleExact && sameJob(out, before) &&
                            !error.empty();
        }
    }
    check(scheduleExact,
          "mainnet V2, rank-penalty, V3, and uint64-max boundaries are exact");

    struct PolicyCase {
        om::PearlNetwork network;
        const char *height;
        const char *certificate;
        bool accepted;
    };
    static const PolicyCase policyCases[] = {
        {om::PearlNetwork::Testnet, "1", "2", true},
        {om::PearlNetwork::Testnet, "38647", "2", true},
        {om::PearlNetwork::Testnet, "38647", "3", false},
        {om::PearlNetwork::Testnet, "38648", "2", false},
        {om::PearlNetwork::Testnet, "38648", "3", true},
        {om::PearlNetwork::Testnet2, "54868", "2", false},
        {om::PearlNetwork::Testnet2, "54869", "2", true},
        {om::PearlNetwork::Testnet2, "83108", "2", true},
        {om::PearlNetwork::Testnet2, "83108", "3", false},
        {om::PearlNetwork::Testnet2, "83109", "2", false},
        {om::PearlNetwork::Testnet2, "83109", "3", true},
        {om::PearlNetwork::Regtest, "1", "2", false},
        {om::PearlNetwork::Regtest, "1", "3", true},
    };
    static const om::PearlTargetSemantics semantics[] = {
        om::PearlTargetSemantics::BaseBeforePenalty,
        om::PearlTargetSemantics::FinalAdjustedBound};
    bool explicitPoliciesExact = true;
    for (om::PearlTargetSemantics targetSemantics : semantics) {
        for (const PolicyCase &item : policyCases) {
            const om::PearlSourcePolicy exactPolicy{item.network,
                                                     targetSemantics};
            om::Job out = canonical;
            const om::Job before = out;
            error = "stale";
            code = om::PearlJobContextError::None;
            const bool accepted = om::pearlPoolCanonicalJobFromNotify(
                notify("policy-boundary", 22, target, item.height,
                       item.certificate),
                exactPolicy, &out, &error, &code);
            explicitPoliciesExact =
                explicitPoliciesExact && accepted == item.accepted;
            if (accepted) {
                om::PearlJobContext decoded;
                explicitPoliciesExact =
                    explicitPoliciesExact &&
                    om::validatePearlJobContext(out, &exactPolicy, &decoded,
                                                &error, &code) &&
                    decoded.network == item.network &&
                    decoded.targetSemantics == targetSemantics;
            } else {
                explicitPoliciesExact =
                    explicitPoliciesExact && sameJob(out, before) &&
                    !error.empty();
            }
        }
    }
    check(explicitPoliciesExact,
          "explicit Testnet, Testnet2, Regtest, and target-semantics policies are honored");

    std::string escapedId = "opaque\\u0000tail:colon";
    om::Job escapedIdJob;
    error = "stale";
    code = om::PearlJobContextError::InvalidJob;
    om::PearlJobContext escapedContext;
    static const char expectedEscapedId[] = "opaque\0tail:colon";
    check(om::pearlPoolCanonicalJobFromNotify(
              notify(escapedId, 23), policy, &escapedIdJob, &error, &code) &&
              om::validatePearlJobContext(escapedIdJob, &policy,
                                          &escapedContext, &error, &code) &&
              escapedContext.transportJobId == std::string(
                  expectedEscapedId, sizeof(expectedEscapedId) - 1) &&
              escapedIdJob.jobId == escapedContext.transportJobId,
          "escaped embedded-NUL and colon transport ID round-trips byte-exactly");

    const std::string maximumId(om::kMaxPearlTransportJobIdBytes, 'm');
    om::Job maximumIdJob;
    error = "stale";
    code = om::PearlJobContextError::InvalidJob;
    const bool maximumAccepted = om::pearlPoolCanonicalJobFromNotify(
        notify(maximumId, 24), policy, &maximumIdJob, &error, &code);
    om::Job oversizedOutput = canonical;
    const om::Job oversizedBefore = oversizedOutput;
    error = "stale";
    code = om::PearlJobContextError::None;
    const bool oversizedRejected = !om::pearlPoolCanonicalJobFromNotify(
        notify(maximumId + "x", 24), policy, &oversizedOutput, &error, &code);
    check(maximumAccepted && maximumIdJob.jobId.size() ==
                                 om::kMaxPearlTransportJobIdBytes &&
              oversizedRejected &&
              code == om::PearlJobContextError::TransportJobIdTooLong &&
              sameJob(oversizedOutput, oversizedBefore),
          "4096-byte transport ID is accepted and 4097-byte ID fails transactionally");

    static const char *invalidHeights[] = {
        "0",          "00",         "0139053",      "+139053",
        "-139053",    "139053.0",   "1e5",          "\"139053\"",
        "18446744073709551616"};
    bool invalidTransactional = true;
    for (const char *height : invalidHeights) {
        om::Job out = canonical;
        const om::Job before = out;
        error = "stale";
        code = om::PearlJobContextError::None;
        invalidTransactional =
            invalidTransactional &&
            !om::pearlPoolCanonicalJobFromNotify(
                notify("bad-height", 21, target, height, "3"), policy, &out,
                &error, &code) &&
            sameJob(out, before) && !error.empty();
    }
    check(invalidTransactional,
          "noncanonical, signed, fractional, quoted, and overflowing heights fail transactionally");

    std::string missingHeight = canonicalLine;
    const std::string heightMember = ",\"height\":139053";
    const std::size_t heightAt = missingHeight.find(heightMember);
    if (heightAt != std::string::npos)
        missingHeight.erase(heightAt, heightMember.size());
    std::string duplicateHeight = canonicalLine;
    const std::size_t duplicateAt = duplicateHeight.find(heightMember);
    if (duplicateAt != std::string::npos)
        duplicateHeight.insert(duplicateAt + heightMember.size(), heightMember);
    std::string escapedDuplicateHeight = canonicalLine;
    const std::size_t escapedAt = escapedDuplicateHeight.find(heightMember);
    if (escapedAt != std::string::npos)
        escapedDuplicateHeight.insert(escapedAt + heightMember.size(),
                                      ",\"heigh\\u0074\":139053");
    bool structuralFailures = heightAt != std::string::npos &&
                              duplicateAt != std::string::npos &&
                              escapedAt != std::string::npos;
    for (const std::string *line :
         {&missingHeight, &duplicateHeight, &escapedDuplicateHeight}) {
        om::Job out = canonical;
        const om::Job before = out;
        error = "stale";
        code = om::PearlJobContextError::None;
        structuralFailures =
            structuralFailures &&
            !om::pearlPoolCanonicalJobFromNotify(*line, policy, &out, &error,
                                                  &code) &&
            sameJob(out, before) && !error.empty();
    }
    check(structuralFailures,
          "missing and duplicate direct height members fail transactionally");

    const om::PearlSourcePolicy invalidPolicy{
        static_cast<om::PearlNetwork>(0),
        om::PearlTargetSemantics::BaseBeforePenalty};
    om::Job policyOutput = canonical;
    const om::Job policyBefore = policyOutput;
    error = "stale";
    code = om::PearlJobContextError::None;
    check(!om::pearlPoolCanonicalJobFromNotify(
              canonicalLine, invalidPolicy, &policyOutput, &error, &code) &&
              code == om::PearlJobContextError::InvalidNetwork &&
              sameJob(policyOutput, policyBefore),
          "invalid explicit source policy fails without publishing output");

    om::Job aliasOutput = canonical;
    const om::Job aliasBefore = aliasOutput;
    code = om::PearlJobContextError::None;
    const bool extraAliasRejected = !om::pearlPoolCanonicalJobFromNotify(
        canonicalLine, policy, &aliasOutput, &aliasOutput.extra, &code);
    const bool extraAliasSafe =
        extraAliasRejected && code == om::PearlJobContextError::AliasedOutput &&
        sameJob(aliasOutput, aliasBefore);
    aliasOutput = aliasBefore;
    code = om::PearlJobContextError::None;
    const bool idAliasRejected = !om::pearlPoolCanonicalJobFromNotify(
        canonicalLine, policy, &aliasOutput, &aliasOutput.jobId, &code);
    const bool idAliasSafe =
        idAliasRejected && code == om::PearlJobContextError::AliasedOutput &&
        sameJob(aliasOutput, aliasBefore);
    std::string aliasedLine = canonicalLine;
    const std::string aliasedLineBefore = aliasedLine;
    aliasOutput = aliasBefore;
    code = om::PearlJobContextError::None;
    const bool lineAliasRejected = !om::pearlPoolCanonicalJobFromNotify(
        aliasedLine, policy, &aliasOutput, &aliasedLine, &code);
    std::string aliasedBadLine = "{}";
    const std::string aliasedBadLineBefore = aliasedBadLine;
    aliasOutput = aliasBefore;
    code = om::PearlJobContextError::None;
    const bool failingLineAliasRejected =
        !om::pearlPoolCanonicalJobFromNotify(
            aliasedBadLine, policy, &aliasOutput, &aliasedBadLine, &code);
    check(extraAliasSafe && idAliasSafe && lineAliasRejected &&
              failingLineAliasRejected &&
              code == om::PearlJobContextError::AliasedOutput &&
              aliasedLine == aliasedLineBefore &&
              aliasedBadLine == aliasedBadLineBefore &&
              sameJob(aliasOutput, aliasBefore),
          "success and failure error/input/output aliases reject before mutation");
}

void canonicalStageBHelpersTest() {
    std::puts("additive canonical identity, transcript, match, and target finalization:");
    const om::PearlSourcePolicy basePolicy{
        om::PearlNetwork::Mainnet,
        om::PearlTargetSemantics::BaseBeforePenalty};
    const om::PearlSourcePolicy finalPolicy{
        om::PearlNetwork::Mainnet,
        om::PearlTargetSemantics::FinalAdjustedBound};
    om::pearl::MiningConfig config;
    const std::string target =
        "000000000000000000000000000000000000000000000000000000000000ffff";
    const std::string escapedId = "stage-b\\u0000id:tail";
    static const char expectedId[] = "stage-b\0id:tail";
    const std::string exactId(expectedId, sizeof(expectedId) - 1);
    om::Job job;
    std::string error = "stale";
    om::PearlJobContextError code = om::PearlJobContextError::InvalidJob;
    check(om::pearlPoolCanonicalJobFromNotify(
              notify(escapedId, 31, target, "139053", "3"), basePolicy,
              &job, &error, &code),
          "Stage-B fixture parses through the Stage-A explicit-policy gate");

    om::PearlPoolCanonicalView view;
    const bool identified = om::pearlPoolCanonicalViewForJob(
        job, basePolicy, config, &view, &error, &code);
    const om::PearlPoolCanonicalIdentity &identity = view.identity;
    const om::pearl::U256 raw = om::pearl::U256::fromLimbs(job.target);
    om::pearl::U256 expectedResolved;
    const bool scaled = config.penalizedTarget(raw, &expectedResolved);
    check(identified && scaled && identity.context.transportJobId == exactId &&
              identity.context.network == om::PearlNetwork::Mainnet &&
              identity.context.height == 139053 &&
              identity.context.targetSemantics ==
                  om::PearlTargetSemantics::BaseBeforePenalty &&
              identity.context.certificateVersion ==
                  om::PearlCertificateVersion::V3 &&
              std::memcmp(identity.rawTarget.v, job.target,
                          sizeof(job.target)) == 0 &&
              std::memcmp(view.resolvedTarget.v, expectedResolved.v,
                          sizeof(expectedResolved.v)) == 0 &&
              error.empty() && code == om::PearlJobContextError::None,
          "identity decodes policy/context and resolves raw target exactly");
    check(bytesHex(identity.sourceDigest.bytes) ==
              "328d3347a210018727000ed555a5e905c2b949a7a3b5655330ac944a2f885df2",
          "source digest matches the pinned domain/framing golden vector");

    std::string transcript = "sentinel transcript";
    std::string parsedSchema;
    std::string parsedEvent;
    std::string parsedNetwork;
    std::string parsedHeight;
    std::string parsedTransport;
    check(om::pearlPoolCanonicalTranscriptNotify(
              job, basePolicy, config, &transcript, &error, &code) &&
              om::jsonObjectStringStrict(transcript, "schema",
                                         &parsedSchema) &&
              parsedSchema == "canary-pearl-canonical-diagnostic-v1" &&
              om::jsonObjectStringStrict(transcript, "event",
                                         &parsedEvent) &&
              parsedEvent == "mining.notify" &&
              om::jsonObjectStringStrict(transcript, "network",
                                         &parsedNetwork) &&
              parsedNetwork == "mainnet" &&
              om::jsonObjectRawValueStrict(transcript, "height",
                                           &parsedHeight) &&
              parsedHeight == "139053" &&
              om::jsonObjectStringStrict(transcript,
                                         "transport_job_id_hex",
                                         &parsedTransport) &&
              parsedTransport == "73746167652d620069643a7461696c" &&
              transcript.find("\"network\":\"mainnet\"") !=
                  std::string::npos &&
              transcript.find("\"height\":139053") != std::string::npos &&
              transcript.find(
                  "\"target_semantics\":\"base-before-penalty\"") !=
                  std::string::npos &&
              transcript.find("\"cert_version\":\"v3\"") !=
                  std::string::npos &&
              transcript.find(
                  "\"transport_job_id_hex\":\"73746167652d620069643a7461696c\"") !=
                  std::string::npos &&
              transcript.find("\"raw_target_be\":\"" +
                              om::pearlTargetHexBE(identity.rawTarget.v) +
                              "\"") != std::string::npos &&
              transcript.find("\"resolved_target_be\":\"" +
                              om::pearlTargetHexBE(view.resolvedTarget.v) +
                              "\"") != std::string::npos,
          "strictly parsed transcript exposes validated binary-safe identity fields");

    const std::string maximumId(om::kMaxPearlTransportJobIdBytes, 'm');
    om::Job maximumIdJob;
    om::PearlPoolCanonicalIdentity maximumIdentity;
    std::string maximumTranscript;
    std::string maximumTransportHex;
    std::string expectedMaximumHex;
    expectedMaximumHex.reserve(maximumId.size() * 2);
    for (std::size_t i = 0; i < maximumId.size(); ++i)
        expectedMaximumHex += "6d";
    check(om::pearlPoolCanonicalJobFromNotify(
              notify(maximumId, 31, target, "139053", "3"), basePolicy,
              &maximumIdJob, &error, &code) &&
              om::pearlPoolCanonicalIdentityForJob(
                  maximumIdJob, basePolicy, &maximumIdentity, &error,
                  &code) &&
              om::pearlPoolCanonicalTranscriptNotify(
                  maximumIdJob, basePolicy, config, &maximumTranscript,
                  &error, &code) &&
              om::jsonObjectStringStrict(maximumTranscript,
                                         "transport_job_id_hex",
                                         &maximumTransportHex) &&
              maximumIdentity.context.transportJobId == maximumId &&
              maximumTransportHex.size() == maximumId.size() * 2 &&
              maximumTransportHex == expectedMaximumHex,
          "maximum transport ID crosses hash chunks and remains strict JSON");

    om::Job differentId;
    check(om::pearlPoolCanonicalJobFromNotify(
              notify("stage-b\\u0000id:tail\\u0000", 31, target, "139053",
                     "3"),
              basePolicy, &differentId, &error, &code),
          "distinct binary transport-ID fixture parses");
    om::PearlPoolCanonicalIdentity differentIdIdentity;
    bool same = false;
    const bool differentIdMatched = om::pearlPoolCanonicalJobsMatch(
        job, differentId, basePolicy, &same, &error, &code);
    const bool differentIdIdentified = om::pearlPoolCanonicalIdentityForJob(
        differentId, basePolicy, &differentIdIdentity, &error, &code);
    check(differentIdMatched && !same && differentIdIdentified &&
              identity.sourceDigest.bytes !=
                  differentIdIdentity.sourceDigest.bytes,
          "exact ID length and embedded bytes participate in match and digest");

    om::Job finalJob;
    om::PearlPoolCanonicalView finalView;
    const bool finalParsed = om::pearlPoolCanonicalJobFromNotify(
        notify(escapedId, 31, target, "139053", "3"), finalPolicy,
        &finalJob, &error, &code);
    const bool finalIdentified =
        finalParsed && om::pearlPoolCanonicalViewForJob(
                           finalJob, finalPolicy, config, &finalView, &error,
                           &code);
    check(finalIdentified &&
              std::memcmp(finalView.identity.rawTarget.v,
                          finalView.resolvedTarget.v,
                          sizeof(finalView.identity.rawTarget.v)) == 0 &&
              identity.sourceDigest.bytes !=
                  finalView.identity.sourceDigest.bytes,
          "final-adjusted semantics preserve raw target and change policy identity");

    const om::PearlSourcePolicy testnetPolicy{
        om::PearlNetwork::Testnet,
        om::PearlTargetSemantics::BaseBeforePenalty};
    om::Job networkJob;
    om::Job scheduleJob;
    om::Job headerJob;
    om::Job rawTargetJob;
    om::PearlPoolCanonicalIdentity networkIdentity;
    om::PearlPoolCanonicalIdentity scheduleIdentity;
    om::PearlPoolCanonicalIdentity headerIdentity;
    om::PearlPoolCanonicalIdentity rawTargetIdentity;
    const bool networkCovered =
        om::pearlPoolCanonicalJobFromNotify(
            notify(escapedId, 31, target, "139053", "3"), testnetPolicy,
            &networkJob, &error, &code) &&
        om::pearlPoolCanonicalIdentityForJob(
            networkJob, testnetPolicy, &networkIdentity, &error, &code) &&
        identity.sourceDigest.bytes != networkIdentity.sourceDigest.bytes;
    const bool scheduleCovered =
        om::pearlPoolCanonicalJobFromNotify(
            notify(escapedId, 31, target, "98999", "2"), basePolicy,
            &scheduleJob, &error, &code) &&
        om::pearlPoolCanonicalIdentityForJob(
            scheduleJob, basePolicy, &scheduleIdentity, &error, &code) &&
        identity.sourceDigest.bytes != scheduleIdentity.sourceDigest.bytes;
    const bool headerCovered =
        om::pearlPoolCanonicalJobFromNotify(
            notify(escapedId, 32, target, "139053", "3"), basePolicy,
            &headerJob, &error, &code) &&
        om::pearlPoolCanonicalIdentityForJob(
            headerJob, basePolicy, &headerIdentity, &error, &code) &&
        identity.sourceDigest.bytes != headerIdentity.sourceDigest.bytes;
    const std::string smallerTarget =
        "000000000000000000000000000000000000000000000000000000000000fffe";
    const bool rawTargetCovered =
        om::pearlPoolCanonicalJobFromNotify(
            notify(escapedId, 31, smallerTarget, "139053", "3"), basePolicy,
            &rawTargetJob, &error, &code) &&
        om::pearlPoolCanonicalIdentityForJob(
            rawTargetJob, basePolicy, &rawTargetIdentity, &error, &code) &&
        identity.sourceDigest.bytes != rawTargetIdentity.sourceDigest.bytes;
    check(networkCovered && scheduleCovered && headerCovered &&
              rawTargetCovered &&
              networkIdentity.context.network == om::PearlNetwork::Testnet &&
              scheduleIdentity.context.height == 98999 &&
              scheduleIdentity.context.certificateVersion ==
                  om::PearlCertificateVersion::V2,
          "digest covers network, schedule/certificate, header, and raw target");

    om::pearl::MiningConfig widerConfig = config;
    widerConfig.commonDim = 4096;
    om::PearlPoolCanonicalView widerView;
    check(om::pearlPoolCanonicalViewForJob(
              job, basePolicy, widerConfig, &widerView, &error, &code) &&
              std::memcmp(identity.rawTarget.v,
                          widerView.identity.rawTarget.v,
                          sizeof(identity.rawTarget.v)) == 0 &&
              std::memcmp(view.resolvedTarget.v, widerView.resolvedTarget.v,
                          sizeof(view.resolvedTarget.v)) != 0 &&
              identity.sourceDigest.bytes ==
                  widerView.identity.sourceDigest.bytes,
          "resolved target changes without changing canonical source identity");

    om::pearl::MiningConfig invalidConfig = config;
    invalidConfig.rank = 0;
    om::PearlPoolCanonicalIdentity identityWithInvalidConfig;
    om::PearlPoolCanonicalView invalidConfigSentinel = view;
    invalidConfigSentinel.resolvedTarget.v[0] ^= 0x7788;
    const om::PearlPoolCanonicalView invalidConfigBefore =
        invalidConfigSentinel;
    bool matchWithInvalidConfig = false;
    code = om::PearlJobContextError::None;
    const bool sourceIgnoresInvalidConfig =
        om::pearlPoolCanonicalIdentityForJob(
            job, basePolicy, &identityWithInvalidConfig, &error, &code) &&
        om::pearlPoolCanonicalJobsMatch(
            job, job, basePolicy, &matchWithInvalidConfig, &error, &code) &&
        matchWithInvalidConfig &&
        sameCanonicalIdentity(identity, identityWithInvalidConfig);
    const bool invalidViewRejected = !om::pearlPoolCanonicalViewForJob(
        job, basePolicy, invalidConfig, &invalidConfigSentinel, &error,
        &code);
    check(sourceIgnoresInvalidConfig && invalidViewRejected &&
              code == om::PearlJobContextError::InvalidMiningConfig &&
              sameCanonicalView(invalidConfigSentinel,
                                invalidConfigBefore),
          "source identity and dedupe remain available when resolution config is invalid");

    const std::string maximumTarget(64, 'f');
    om::Job overflowJob;
    om::PearlPoolCanonicalIdentity overflowIdentity;
    om::PearlPoolCanonicalView overflowSentinel = view;
    overflowSentinel.resolvedTarget.v[1] ^= 0x99;
    const om::PearlPoolCanonicalView overflowBefore = overflowSentinel;
    code = om::PearlJobContextError::None;
    const bool overflowSourceAvailable =
        om::pearlPoolCanonicalJobFromNotify(
            notify("overflow", 33, maximumTarget, "139053", "3"),
            basePolicy, &overflowJob, &error, &code) &&
        om::pearlPoolCanonicalIdentityForJob(
            overflowJob, basePolicy, &overflowIdentity, &error, &code);
    const bool overflowViewRejected = !om::pearlPoolCanonicalViewForJob(
        overflowJob, basePolicy, config, &overflowSentinel, &error, &code);
    check(overflowSourceAvailable && overflowViewRejected &&
              code == om::PearlJobContextError::TargetOverflow &&
              sameCanonicalView(overflowSentinel, overflowBefore),
          "source identity survives target-resolution overflow transactionally");

    bool exactMatch = false;
    check(om::pearlPoolCanonicalJobsMatch(job, job, basePolicy, &exactMatch,
                                          &error, &code) &&
              exactMatch,
          "canonical jobsMatch accepts identical exact context and policy");
    bool mismatchSentinel = true;
    error = "stale";
    code = om::PearlJobContextError::None;
    check(!om::pearlPoolCanonicalJobsMatch(
              job, finalJob, basePolicy, &mismatchSentinel, &error, &code) &&
              mismatchSentinel &&
              code == om::PearlJobContextError::PolicyMismatch,
          "policy mismatch fails closed without publishing match output");

    om::PearlPoolCanonicalIdentity identitySentinel = identity;
    identitySentinel.context.transportJobId = "identity sentinel";
    identitySentinel.rawTarget.v[0] ^= 0x1234;
    identitySentinel.sourceDigest.bytes[0] ^= 0x55;
    const om::PearlPoolCanonicalIdentity identityBefore = identitySentinel;
    om::Job invalid = job;
    invalid.msg[0] ^= 1;
    error = "stale";
    code = om::PearlJobContextError::None;
    const bool invalidIdentityRejected =
        !om::pearlPoolCanonicalIdentityForJob(
            invalid, basePolicy, &identitySentinel, &error, &code);
    std::string transcriptSentinel = "unchanged transcript";
    const std::string transcriptBefore = transcriptSentinel;
    const bool invalidTranscriptRejected =
        !om::pearlPoolCanonicalTranscriptNotify(
            invalid, basePolicy, config, &transcriptSentinel, &error, &code);
    bool matchSentinel = true;
    const bool invalidMatchRejected = !om::pearlPoolCanonicalJobsMatch(
        job, invalid, basePolicy, &matchSentinel, &error, &code);
    check(invalidIdentityRejected && invalidTranscriptRejected &&
              invalidMatchRejected &&
              sameCanonicalIdentity(identitySentinel, identityBefore) &&
              transcriptSentinel == transcriptBefore && matchSentinel,
          "invalid canonical jobs preserve every identity/transcript/match output");

    om::Job aliasedInput = job;
    const om::Job aliasedInputBefore = aliasedInput;
    code = om::PearlJobContextError::None;
    const bool identityInputAlias =
        !om::pearlPoolCanonicalIdentityForJob(
            aliasedInput, basePolicy, &identitySentinel, &aliasedInput.extra,
            &code);
    code = om::PearlJobContextError::None;
    const bool identityOutputAlias =
        !om::pearlPoolCanonicalIdentityForJob(
            aliasedInput, basePolicy, &identitySentinel,
            &identitySentinel.context.transportJobId, &code);
    om::PearlPoolCanonicalView viewSentinel = view;
    viewSentinel.identity.context.transportJobId = "view alias sentinel";
    const om::PearlPoolCanonicalView viewBefore = viewSentinel;
    code = om::PearlJobContextError::None;
    const bool viewOutputAlias = !om::pearlPoolCanonicalViewForJob(
        aliasedInput, basePolicy, config, &viewSentinel,
        &viewSentinel.identity.context.transportJobId, &code);
    code = om::PearlJobContextError::None;
    const bool transcriptInputAlias =
        !om::pearlPoolCanonicalTranscriptNotify(
            aliasedInput, basePolicy, config, &aliasedInput.jobId, &error,
            &code);
    transcriptSentinel = transcriptBefore;
    code = om::PearlJobContextError::None;
    const bool transcriptOutputAlias =
        !om::pearlPoolCanonicalTranscriptNotify(
            aliasedInput, basePolicy, config, &transcriptSentinel,
            &transcriptSentinel, &code);
    code = om::PearlJobContextError::None;
    const bool matchInputAlias = !om::pearlPoolCanonicalJobsMatch(
        aliasedInput, job, basePolicy, &matchSentinel,
        &aliasedInput.jobId, &code);
    code = om::PearlJobContextError::None;
    const bool matchBoolAlias = !om::pearlPoolCanonicalJobsMatch(
        aliasedInput, job, basePolicy, &aliasedInput.valid, &error,
        &code);
    check(identityInputAlias && identityOutputAlias && viewOutputAlias &&
              transcriptInputAlias && transcriptOutputAlias &&
              matchInputAlias && matchBoolAlias &&
              sameJob(aliasedInput, aliasedInputBefore) &&
              sameCanonicalIdentity(identitySentinel, identityBefore) &&
              sameCanonicalView(viewSentinel, viewBefore) &&
              transcriptSentinel == transcriptBefore && matchSentinel &&
              code == om::PearlJobContextError::AliasedOutput,
          "all Stage-B string input/output aliases reject before mutation");

    const uint64_t pending[4] = {0x1234, 0, 0, 0};
    const uint64_t pendingBefore[4] = {pending[0], pending[1], pending[2],
                                       pending[3]};
    om::PearlPoolCanonicalTargetFinalization finalized;
    const bool finalizedPending = om::pearlPoolFinalizeCanonicalTarget(
        job, pending, true, basePolicy, config, &finalized, &error, &code);
    om::pearl::U256 expectedPendingResolved;
    const bool pendingScaled = config.penalizedTarget(
        om::pearl::U256::fromLimbs(pending), &expectedPendingResolved);
    check(finalizedPending && pendingScaled &&
              finalized.consumesPendingTarget &&
              std::memcmp(finalized.job.target, pending,
                          sizeof(finalized.job.target)) == 0 &&
              std::memcmp(finalized.view.identity.rawTarget.v, pending,
                          sizeof(pending)) == 0 &&
              std::memcmp(finalized.view.resolvedTarget.v,
                          expectedPendingResolved.v,
                          sizeof(expectedPendingResolved.v)) == 0 &&
              std::memcmp(pending, pendingBefore, sizeof(pending)) == 0,
          "non-live finalizer selects pending target without consuming caller state");

    om::PearlPoolCanonicalTargetFinalization noPending;
    check(om::pearlPoolFinalizeCanonicalTarget(
              job, nullptr, false, basePolicy, config, &noPending, &error,
              &code) &&
              !noPending.consumesPendingTarget &&
              sameJob(noPending.job, job),
          "no-pending finalization needs no dummy pointer and retains notify target");

    om::PearlPoolCanonicalTargetFinalization overlap = finalized;
    const uint64_t overlapTarget[4] = {0xabcdef, 1, 0, 0};
    std::memcpy(overlap.job.target, overlapTarget, sizeof(overlapTarget));
    check(om::pearlPoolFinalizeCanonicalTarget(
              overlap.job, overlap.job.target, true, basePolicy, config,
              &overlap, &error, &code) &&
              overlap.consumesPendingTarget &&
              std::memcmp(overlap.job.target, overlapTarget,
                          sizeof(overlapTarget)) == 0 &&
              std::memcmp(overlap.view.identity.rawTarget.v, overlapTarget,
                          sizeof(overlapTarget)) == 0,
          "finalizer stages safely when notify, pending target, and output overlap");

    om::PearlPoolCanonicalTargetFinalization finalizationSentinel = finalized;
    finalizationSentinel.job.jobId = "finalization sentinel";
    finalizationSentinel.view.identity.context.transportJobId =
        "view sentinel";
    finalizationSentinel.consumesPendingTarget = false;
    const om::PearlPoolCanonicalTargetFinalization finalizationBefore =
        finalizationSentinel;
    error = "stale";
    code = om::PearlJobContextError::None;
    const bool invalidFinalizationRejected =
        !om::pearlPoolFinalizeCanonicalTarget(
            invalid, pending, true, basePolicy, config,
            &finalizationSentinel, &error, &code);
    const uint64_t zeroPending[4] = {0, 0, 0, 0};
    const bool zeroPendingRejected = !om::pearlPoolFinalizeCanonicalTarget(
        job, zeroPending, true, basePolicy, config, &finalizationSentinel,
        &error, &code);
    const bool nullPendingRejected = !om::pearlPoolFinalizeCanonicalTarget(
        job, nullptr, true, basePolicy, config, &finalizationSentinel, &error,
        &code);
    code = om::PearlJobContextError::None;
    const bool finalizationAliasRejected =
        !om::pearlPoolFinalizeCanonicalTarget(
            job, pending, true, basePolicy, config, &finalizationSentinel,
            &finalizationSentinel.job.extra, &code);
    check(invalidFinalizationRejected && zeroPendingRejected &&
              nullPendingRejected &&
              finalizationAliasRejected &&
              sameCanonicalFinalization(finalizationSentinel,
                                        finalizationBefore) &&
              std::memcmp(pending, pendingBefore, sizeof(pending)) == 0 &&
              code == om::PearlJobContextError::AliasedOutput,
          "finalizer failures and aliases preserve output and staged target");
}

void canonicalSourceIntegrationTest() {
    std::puts("canonical-only source policy, exact identity, and staging:");
    {
        FakeClock clock;
        FakeTransport transport(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid", 1, "wallet", "rig", sourcePolicy(),
                                   "", &transport, &clock);
        unsigned callbacks = 0;
        source.setPublicationCallback([](void *context) noexcept {
            ++*static_cast<unsigned *>(context);
        }, &callbacks);
        om::Job first, rebound;
        bool ok = source.fetch(&first) && callbacks == 5;
        ok = ok && source.fetch(&rebound) && sameJob(first, rebound) && callbacks == 5;
        transport.afterSubmitBytes("{\"id\":101,\"result\":true,\"error\":null}\n");
        std::string error;
        ok = ok && source.submit(rebound, solution(), &error) && callbacks == 7;
        source.setPublicationCallback(nullptr, nullptr);
        transport.afterSubmitBytes("{\"id\":102,\"result\":true,\"error\":null}\n");
        ok = ok && source.submit(rebound, solution(), &error) && callbacks == 7;
        check(ok, "event callback excludes issued bind/release and detaches before teardown");
    }
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        auto policy = sourcePolicy();
        if (scenario == 0) policy = {};
        if (scenario == 1) policy.network = static_cast<om::PearlNetwork>(99);
        if (scenario == 2)
            policy.targetSemantics = static_cast<om::PearlTargetSemantics>(99);
        if (scenario == 3) policy.network = om::PearlNetwork::SyntheticBenchmark;
        om::PearlPoolSource source("example.invalid", 1, "wallet", "rig", policy,
                                   "", &transport, &clock, &mailbox);
        om::Job output;
        output.extra = "sentinel";
        const auto before = output;
        std::string error;
        check(!source.fetch(&output) &&
                  !om::PearlPoolSourceTestAccess::connect(source) &&
                  !source.submit(output, solution(), &error) &&
                  sameJob(output, before) && transport.openCalls == 0 &&
                  transport.sendCalls == 0 && mailbox.snapshot().publishedEvents == 0,
              "missing/invalid pinned policy rejects before transport and publication");
    }
    {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlPoolSource source("example.invalid", 1, "wallet", "rig", "",
                                   &transport, &clock);
        om::Job job;
        check(!source.fetch(&job) && transport.openCalls == 0,
              "legacy constructor cannot silently activate a default policy");
    }
    bool allPolicies = true;
    for (auto network : {om::PearlNetwork::Mainnet, om::PearlNetwork::Testnet,
                         om::PearlNetwork::Testnet2, om::PearlNetwork::Regtest}) {
        for (auto semantics : {om::PearlTargetSemantics::BaseBeforePenalty,
                               om::PearlTargetSemantics::FinalAdjustedBound}) {
            FakeClock clock;
            FakeTransport transport(clock);
            om::PearlSourcePolicy policy{network, semantics};
            om::PearlPoolSource source("example.invalid", 1, "wallet", "rig", policy,
                                       "", &transport, &clock);
            const std::string exactId("id\0:quote\"\\", 11);
            const std::string quoted = om::jsonQuoted(exactId);
            transport.bytes(authReply + "\n" +
                notify(quoted.substr(1, quoted.size() - 2), 4,
                       std::string(60, '0') + "ffff", "18446744073709551615") + "\n");
            FILE *transcript = om::PearlPoolSourceTestAccess::attachTranscript(source);
            om::Job job, current;
            om::PearlPoolCanonicalView view;
            bool ok = source.fetch(&job) &&
                om::PearlPoolSourceTestAccess::current(source, &current) &&
                sameJob(job, current) && job.jobId == exactId && job.epoch == UINT64_MAX &&
                om::pearlPoolCanonicalViewForJob(job, policy, {}, &view);
            om::Job changed = job;
            changed.jobId += "x";
            std::string error;
            const auto sends = transport.sendCalls;
            ok = ok && !source.submit(changed, solution(), &error) &&
                transport.sendCalls == sends &&
                !source.submit(job, solution(), &job.extra) && sameJob(job, current);
            om::Job legacy;
            om::pearlPoolJobFromNotify(notify("legacy", 4), &legacy);
            ok = ok && !source.submit(legacy, solution(), &error) &&
                transport.sendCalls == sends;
            transport.afterSubmitBytes("{\"id\":101,\"result\":true,\"error\":null}\n");
            auto proofFixture = solution();
            // A valid shape prefix only, not a cryptographically valid proof.
            // Deliberately unlike the pinned config to prove diagnostic fields
            // cannot alter the canonical resolved bound.
            const uint64_t dimensions[4] = {8192, 16384, 1024, 64};
            std::vector<uint8_t> prefix;
            for (uint64_t dimension : dimensions)
                for (int byte = 0; byte < 8; ++byte)
                    prefix.push_back(static_cast<uint8_t>(dimension >> (byte * 8)));
            const bool validShape = network == om::PearlNetwork::Mainnet ||
                                    network == om::PearlNetwork::Testnet;
            if (validShape) proofFixture.extra = om::pearl::base64(prefix);
            ok = ok && source.submit(job, proofFixture, &error) &&
                transport.wire.find(quoted) != std::string::npos;
            if (transcript) {
                std::rewind(transcript);
                char buffer[4096];
                const bool read = std::fgets(buffer, sizeof(buffer), transcript) != nullptr;
                std::string expected;
                ok = ok && read && om::pearlPoolCanonicalTranscriptNotify(job, policy,
                    {}, &expected) && std::string(buffer) == expected + "\n";
                const bool readSubmit = std::fgets(buffer, sizeof(buffer), transcript) != nullptr;
                std::string rawBound;
                const std::string submitLine = readSubmit ? buffer : "";
                ok = ok && readSubmit &&
                    om::jsonObjectStringStrict(submitLine, "resolved_target_be", &rawBound) &&
                    rawBound == om::pearlTargetHexBE(view.resolvedTarget.v) &&
                    submitLine.find("\"event\":\"mining.submit\"") != std::string::npos;
                const char *shapeNames[4] = {"m", "n", "k", "rank"};
                for (int i = 0; i < 4; ++i) {
                    std::string shape;
                    ok = ok && om::jsonObjectRawValueStrict(submitLine, shapeNames[i], &shape) &&
                        shape == (validShape ? std::to_string(dimensions[i]) : "null");
                }
            } else ok = false;
            allPolicies = allPolicies && ok;
        }
    }
    check(allPolicies,
          "all policies fetch/current/submit/transcript preserve binary ID and full height");
    {
        FakeClock clock;
        FakeTransport transport(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid", 1, "wallet", "rig", sourcePolicy(),
                                   "", &transport, &clock);
        om::Job job;
        bool ok = source.fetch(&job);
        const auto before = job;
        transport.bytes("{\"method\":\"mining.set_target\",\"params\":{\"target\":\"" +
                        std::string(64, 'f') + "\"}}\n" + notify("overflow", 2) + "\n");
        ok = ok && !source.fetch(&job) && sameJob(job, before) &&
            om::PearlPoolSourceTestAccess::pending(source);
        transport.bytes(notify(std::string(4097, 'x'), 2) + "\n");
        ok = ok && !source.fetch(&job) && sameJob(job, before) &&
            om::PearlPoolSourceTestAccess::pending(source);
        transport.bytes("{\"method\":\"mining.set_target\",\"params\":{\"target\":\"" +
                        std::string(63, '0') + "1\"}}\n" +
                        notify(std::string(4096, 'x'), 2) + "\n");
        ok = ok && source.fetch(&job) && job.jobId.size() == 4096 && job.target[0] == 1 &&
            !om::PearlPoolSourceTestAccess::pending(source);
        transport.bytes(notify("next-own-target", 3) + "\n");
        ok = ok && source.fetch(&job) && job.target[0] == 0xffff;
        check(ok, "overflow and oversize notify preserve staged target until valid publication");
    }
}

void lifecycleAndDuplicateTest() {
    std::puts("pool lifecycle, framing, duplicate, and destructor:");
    FakeClock clock;
    FakeTransport transport(clock);
    om::PearlSourceObservabilityMailbox mailbox(clock);
    prime(transport, "job-1", 1, true);
    om::Job first;
    {
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                                   &transport, &clock, &mailbox);
        check(source.fetch(&first), "fragmented pre-auth notify produces a job");
        om::PearlJobIdentityDigest digest1, digest2;
        const bool baseDigest =
            canonicalDigest(first, &digest1);
        om::Job changed = first;
        changed.msg[0] ^= 1;
        const bool rejectsMismatchedMessage =
            !canonicalDigest(changed, &digest2);
        changed = first;
        changed.target[0] ^= 1;
        const bool targetChanges =
            canonicalDigest(changed, &digest2) &&
            digest1.bytes != digest2.bytes;
        changed = first;
        ++changed.epoch;
        const bool epochChanges =
            canonicalDigest(changed, &digest2) && digest1.bytes != digest2.bytes;
        om::PearlJobContext exact;
        const bool unpacked = om::decodePearlJobContext(first, &exact, nullptr);
        changed = first;
        auto changedContext = exact;
        changedContext.transportJobId = "different-job-id";
        changed.jobId = changedContext.transportJobId;
        om::encodePearlJobContext(changedContext, &changed.extra, nullptr);
        const bool jobIdChanges =
            canonicalDigest(changed, &digest2) &&
            digest1.bytes != digest2.bytes;
        changed = first;
        changedContext = exact;
        changedContext.certificateVersion = om::PearlCertificateVersion::V2;
        const bool certificateChanges =
            !om::encodePearlJobContext(changedContext, &changed.extra, nullptr);
        changed = first;
        const std::size_t lowerHex = changed.extra.find_first_of("abcdef");
        if (lowerHex != std::string::npos)
            changed.extra[lowerHex] =
                static_cast<char>(changed.extra[lowerHex] - 'a' + 'A');
        const bool rejectsNoncanonicalExtra =
            lowerHex != std::string::npos &&
            !canonicalDigest(changed, &digest2);
        changed = first;
        changedContext = exact;
        changedContext.header[0] ^= 1;
        om::encodePearlJobContext(changedContext, &changed.extra, nullptr);
        om::pearl::b3::hash(nullptr, changedContext.header.data(), changedContext.header.size(),
                            changed.msg);
        const bool headerChanges =
            canonicalDigest(changed, &digest2) &&
            digest1.bytes != digest2.bytes;
        changed = first;
        changed.valid = false;
        const bool rejectsInvalid =
            !canonicalDigest(changed, &digest2);
        check(baseDigest && unpacked && rejectsMismatchedMessage,
              "canonical digest requires valid exact header/message binding");
        check(targetChanges && epochChanges && jobIdChanges && headerChanges,
              "canonical digest binds target, height, ID, and header");
        check(certificateChanges, "canonical digest rejects wrong scheduled certificate");
        check(rejectsNoncanonicalExtra && rejectsInvalid,
              "canonical digest rejects noncanonical envelope and invalid Job");
        const auto queued = mailbox.snapshot();
        auto events = drainAll(mailbox, 2);
        const auto drained = mailbox.snapshot();
        const om::PearlSourceEventKind expected[] = {
            om::PearlSourceEventKind::SessionStarted,
            om::PearlSourceEventKind::TransportConnected,
            om::PearlSourceEventKind::JobRetainedPreAuth,
            om::PearlSourceEventKind::AuthorizationSucceeded,
            om::PearlSourceEventKind::JobAvailable,
        };
        bool ordered = events.size() == 5;
        for (std::size_t i = 0; ordered && i < 5; ++i)
            ordered = events[i].kind == expected[i] &&
                      events[i].eventToken == i + 1;
        check(ordered && queued.queued == 5 && queued.accountingHolds() &&
                  drained.queued == 0 && drained.drainedEvents == 5 &&
                  drained.accountingHolds(),
              "pre-auth events retain exact lifecycle order across bounded drains");
        const auto before = mailbox.snapshot();
        transport.bytes(notify("job-1", 1) + "\n");
        om::Job duplicate;
        check(source.fetch(&duplicate) && canonicalJobsMatch(first, duplicate),
              "byte-identical notify remains the same issued work");
        const auto after = mailbox.snapshot();
        check(after.publishedEvents == before.publishedEvents &&
                  after.activeJobToken == before.activeJobToken,
              "duplicate work emits no retirement or availability event");
    }
    auto shutdown = drainAll(mailbox, 1);
    check(shutdown.size() == 2 &&
              shutdown[0].kind == om::PearlSourceEventKind::JobRetired &&
              shutdown[0].reason == om::PearlSourceReason::Shutdown &&
              shutdown[1].kind == om::PearlSourceEventKind::Disconnected &&
              shutdown[1].reason == om::PearlSourceReason::Shutdown,
          "destructor retires, releases, and disconnects exactly once");
    check(!mailbox.snapshot().issuedJobBound,
          "destructor leaves no issued ownership behind");
}

void invalidWorkTest() {
    std::puts("invalid work retirement:");
    FakeClock clock;
    FakeTransport transport(clock);
    om::PearlSourceObservabilityMailbox mailbox(clock);
    prime(transport);
    om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                               &transport, &clock, &mailbox);
    om::Job job;
    check(source.fetch(&job), "initial valid work fetched");
    drainAll(mailbox);
    transport.bytes("{\"id\":null,\"method\":\"mining.notify\",\"params\":{}}\n");
    check(!source.fetch(&job), "malformed replacement removes live work");
    auto malformed = drainAll(mailbox);
    check(countKind(malformed, om::PearlSourceEventKind::JobRetired) == 1 &&
              countKind(malformed, om::PearlSourceEventKind::SourceFault) == 1,
          "malformed replacement records retirement then fault");
}

void submitEvidenceTest() {
    std::puts("submission delivery evidence:");
    auto runFailure = [](const char *name, const std::deque<SendAction> &actions,
                         bool restoreFailure, om::PearlSourceDelivery delivery,
                         om::PearlSourceTerminal terminal, bool expectSuccess) {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                                   &transport, &clock, &mailbox);
        om::Job job;
        source.fetch(&job);
        drainAll(mailbox);
        const std::size_t wireBefore = transport.wire.size();
        const unsigned operationsBefore = transport.sendOperations;
        const unsigned callsBefore = transport.sendCalls;
        const std::string expectedWire =
            om::pearlPoolSubmitRequest(101, "job-1", "proof") + "\n";
        transport.sends = actions;
        transport.failNextRestore = restoreFailure;
        std::string err;
        const bool result = source.submit(job, solution(), &err);
        const auto events = drainAll(mailbox);
        const auto *event = lastKind(events, om::PearlSourceEventKind::SubmitTerminal);
        const bool exactOrder = exactKinds(
            events,
            {om::PearlSourceEventKind::SubmitDispatched,
             om::PearlSourceEventKind::SubmitTerminal,
             om::PearlSourceEventKind::SourceFault,
             om::PearlSourceEventKind::JobRetired,
             om::PearlSourceEventKind::Disconnected});
        const std::size_t wireAdded = transport.wire.size() - wireBefore;
        const std::string submittedWire = transport.wire.substr(wireBefore);
        const std::size_t expectedBytes =
            delivery == om::PearlSourceDelivery::NotSent
                ? 0
                : delivery == om::PearlSourceDelivery::PossiblySent
                      ? 5
                      : expectedWire.size();
        check(result == expectSuccess && exactOrder && event &&
                   event->delivery == delivery &&
                   event->terminal == terminal && !event->acknowledged &&
                   event->reason == om::PearlSourceReason::Transport &&
                   events[2].reason == om::PearlSourceReason::Transport &&
                   events[3].reason == om::PearlSourceReason::Transport &&
                   events[4].reason == om::PearlSourceReason::Transport &&
                   transport.sendOperations == operationsBefore + 1 &&
                   transport.sendCalls == callsBefore +
                                              (actions.empty()
                                                   ? 1u
                                                   : static_cast<unsigned>(
                                                         actions.size())) &&
                   wireAdded == expectedBytes &&
                   submittedWire == expectedWire.substr(0, expectedBytes),
               name);
    };

    runFailure("zero-byte write is NotSent LocalFailure",
               {{om::PearlPoolTransferStatus::Error, 0}}, false,
               om::PearlSourceDelivery::NotSent,
               om::PearlSourceTerminal::LocalFailure, false);
    runFailure("partial write is PossiblySent Unknown",
               {{om::PearlPoolTransferStatus::Bytes, 5},
                {om::PearlPoolTransferStatus::Error, 0}},
               false, om::PearlSourceDelivery::PossiblySent,
               om::PearlSourceTerminal::Unknown, false);
    runFailure("full write plus restore failure remains Sent Unknown", {}, true,
               om::PearlSourceDelivery::Sent,
               om::PearlSourceTerminal::Unknown, true);
}

void timeoutAndEofTest() {
    std::puts("reply timeout versus EOF/error:");
    for (int mode = 0; mode < 3; ++mode) {
        const bool eof = mode == 1;
        const bool transportError = mode == 2;
        const bool closes = eof || transportError;
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                                   &transport, &clock, &mailbox);
        om::Job job;
        source.fetch(&job);
        drainAll(mailbox);
        if (eof) transport.afterSubmitEof();
        if (transportError) transport.afterSubmitError();
        std::string err;
        check(source.submit(job, solution(), &err),
              eof ? "fully sent share with EOF remains an unknown outcome"
                  : transportError
                        ? "fully sent share with read error remains unknown"
                        : "fully sent share timeout remains an unknown outcome");
        auto events = drainAll(mailbox);
        const auto *terminal =
            lastKind(events, om::PearlSourceEventKind::SubmitTerminal);
        const bool order = closes
                               ? exactKinds(
                                     events,
                                     {om::PearlSourceEventKind::SubmitDispatched,
                                      om::PearlSourceEventKind::SubmitTerminal,
                                      om::PearlSourceEventKind::SourceFault,
                                      om::PearlSourceEventKind::JobRetired,
                                      om::PearlSourceEventKind::Disconnected})
                               : exactKinds(
                                     events,
                                     {om::PearlSourceEventKind::SubmitDispatched,
                                      om::PearlSourceEventKind::SubmitTerminal});
        check(order && terminal &&
                   terminal->delivery == om::PearlSourceDelivery::Sent &&
                   terminal->terminal == om::PearlSourceTerminal::Unknown &&
                   !terminal->acknowledged &&
                   terminal->reason == (closes ? om::PearlSourceReason::Transport
                                               : om::PearlSourceReason::Timeout),
              eof ? "EOF terminal precedes close"
                  : transportError ? "read-error terminal precedes close"
                                   : "timeout has exact terminal evidence");
        check((closes && events.size() == 5 &&
                       events[2].reason == om::PearlSourceReason::Transport &&
                       events[3].reason == om::PearlSourceReason::Transport &&
                       events[4].reason == om::PearlSourceReason::Transport) ||
                  (!closes && transport.isOpen()),
              closes ? "EOF/error closes the session"
                     : "timeout leaves usable session open");
    }
}

void localValidationAndEmptyBatchLifecycleTest() {
    std::puts("local validation and empty-batch lifecycle:");
    FakeClock clock;
    FakeTransport transport(clock);
    om::PearlSourceObservabilityMailbox mailbox(clock);
    prime(transport);
    om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                               &transport, &clock, &mailbox);
    om::Job job;
    source.fetch(&job);
    drainAll(mailbox);
    const std::size_t wireBefore = transport.wire.size();
    std::string err;
    om::Solution empty;
    check(!source.submit(job, empty, &err) && drainAll(mailbox).empty() &&
              transport.wire.size() == wireBefore,
          "zero-proof candidate stays local and emits no network outcome");
    om::Job altered = job;
    altered.target[0] ^= 1;
    check(!source.submit(altered, solution(), &err) &&
              drainAll(mailbox).empty() &&
              transport.wire.size() == wireBefore,
          "host-invalid job identity stays local and emits no network outcome");
    om::Job next;
    check(source.fetch(&next) && canonicalJobsMatch(job, next) &&
              drainAll(mailbox).empty() && mailbox.snapshot().issuedJobBound,
          "next fetch releases and rebinds an unchanged zero-solution batch");
}

void correlationBudgetIsolationTest() {
    std::puts("correlation-budget isolation:");
    FakeClock clock;
    FakeTransport transport(clock);
    om::PearlSourceObservabilityMailbox mailbox(clock);
    prime(transport, "first-session", 10);
    om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                               &transport, &clock, &mailbox);
    om::Job first;
    source.fetch(&first);
    drainAll(mailbox);
    std::string replies;
    for (int id = 200; id < 212; ++id)
        replies += "{\"id\":" + std::to_string(id) +
                   ",\"error\":null,\"result\":true}\n";
    replies += "{\"id\":101,\"error\":null,\"result\":true}\n";
    transport.afterSubmitBytes(replies);
    std::string err;
    check(source.submit(first, solution(), &err),
          "fully sent share reaches the nonmatching-reply budget");
    const auto exhausted = drainAll(mailbox);
    check(exactKinds(exhausted,
                     {om::PearlSourceEventKind::SubmitDispatched,
                      om::PearlSourceEventKind::SubmitTerminal,
                      om::PearlSourceEventKind::SourceFault,
                      om::PearlSourceEventKind::JobRetired,
                      om::PearlSourceEventKind::Disconnected}) &&
              exhausted[1].delivery == om::PearlSourceDelivery::Sent &&
              exhausted[1].terminal == om::PearlSourceTerminal::Unknown &&
              !exhausted[1].acknowledged &&
              exhausted[1].reason == om::PearlSourceReason::Protocol &&
              exhausted[2].reason == om::PearlSourceReason::Protocol &&
              exhausted[3].reason == om::PearlSourceReason::Protocol &&
              exhausted[4].reason == om::PearlSourceReason::Protocol &&
              !transport.isOpen(),
          "budget exhaustion closes after exact terminal and protocol fault");

    prime(transport, "second-session", 11);
    om::Job second;
    check(source.fetch(&second) &&
              !canonicalJobsMatch(first, second) &&
              std::count(transport.wire.begin(), transport.wire.end(), '\n') == 3,
          "new session receives fresh work with no submit replay");
    drainAll(mailbox);
    transport.afterSubmitBytes("{\"id\":102,\"error\":null,\"result\":true}\n");
    check(source.submit(second, solution(), &err),
          "new session correlates only its own request id");
    const auto accepted = drainAll(mailbox);
    const auto *terminal =
        lastKind(accepted, om::PearlSourceEventKind::SubmitTerminal);
    check(exactKinds(accepted,
                     {om::PearlSourceEventKind::SubmitDispatched,
                      om::PearlSourceEventKind::SubmitTerminal}) &&
              terminal &&
              terminal->terminal == om::PearlSourceTerminal::Accepted &&
              terminal->acknowledged,
          "queued late reply cannot contaminate the replacement session");
}

void soloAuthorizationTest() {
    const std::string address = "prl1" + std::string(59, 'q');
    for (bool solo : {false, true}) {
        FakeClock clock; FakeTransport transport(clock);
        om::PearlPoolSource source("fixture.invalid", 1200, address, "unchanged-worker",
                                  sourcePolicy(), "", &transport, &clock, nullptr, false, solo);
        for (int attempt = 0; attempt < 2; ++attempt) {
            transport.bytes("{\"id\":1,\"result\":true,\"error\":null}\n");
            check(om::PearlPoolSourceTestAccess::connect(source), "standard/SOLO real source auth accepted");
            const std::string expected = om::pearlPoolAuthorizeRequest(address, "unchanged-worker", "x", "CanaryMiner/" CANARY_MINER_VERSION, solo) + "\n";
            check(transport.wire == (attempt == 0 ? expected : expected + expected),
                  "exact upstream prefix once on each connection; worker unchanged");
            transport.close();
        }
    }
    for (const std::string &bad : {"solo:" + address, "solo:solo:" + address}) {
        FakeClock clock; FakeTransport transport(clock);
        om::PearlPoolSource source("fixture.invalid", 1200, bad, "worker", sourcePolicy(),
                                  "", &transport, &clock, nullptr, false, true);
        check(!om::PearlPoolSourceTestAccess::connect(source) && transport.openCalls == 0,
              "prefixed address refused before transport open");
    }
}

void connectAndAuthorizationFailureTest() {
    std::puts("connect-stage and authorization failures:");
    const om::PearlPoolConnectStatus failures[] = {
        om::PearlPoolConnectStatus::DnsFailure,
        om::PearlPoolConnectStatus::SocketFailure,
        om::PearlPoolConnectStatus::ConnectFailure,
    };
    for (const auto failure : failures) {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        transport.openResults.push_back(failure);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                                   &transport, &clock, &mailbox);
        om::Job job;
        check(!source.fetch(&job), "connect-stage refusal returns no work");
        const auto events = drainAll(mailbox);
        check(exactKinds(events,
                         {om::PearlSourceEventKind::SessionStarted,
                          om::PearlSourceEventKind::SourceFault,
                          om::PearlSourceEventKind::Disconnected}) &&
                  events[1].reason == om::PearlSourceReason::Transport &&
                  events[2].reason == om::PearlSourceReason::Transport,
              "connect-stage failure has ordered fault and disconnect");
    }

    FakeClock clock;
    FakeTransport transport(clock);
    om::PearlSourceObservabilityMailbox mailbox(clock);
    transport.bytes(notify("preauth", 7) + "\n" +
                    "{\"id\":1,\"error\":{\"message\":\"denied\"},"
                    "\"result\":null}\n");
    om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                               &transport, &clock, &mailbox);
    om::Job job;
    check(!source.fetch(&job), "authorization refusal returns no work");
    const auto events = drainAll(mailbox);
    check(exactKinds(events,
                     {om::PearlSourceEventKind::SessionStarted,
                      om::PearlSourceEventKind::TransportConnected,
                      om::PearlSourceEventKind::JobRetainedPreAuth,
                      om::PearlSourceEventKind::SourceFault,
                      om::PearlSourceEventKind::JobRetired,
                      om::PearlSourceEventKind::Disconnected}) &&
              events[3].reason == om::PearlSourceReason::Authentication &&
              events[4].reason == om::PearlSourceReason::Authentication &&
              events[5].reason == om::PearlSourceReason::Authentication,
          "preauth work retires in strict order after authorization refusal");
}

void inboundBoundTest() {
    std::puts("bounded inbound framing:");
    {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        const std::string exact(
            om::PearlPoolSource::kMaxInboundBufferedBytes - 1, ' ');
        transport.bytes(exact + "\n" + authReply + "\n" +
                        notify("after-exact-bound", 8) + "\n");
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                                   &transport, &clock, &mailbox);
        om::Job job;
        check(source.fetch(&job) && transport.isOpen(),
              "line exactly at 64 KiB including newline is accepted");
    }
    {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        transport.bytes(std::string(
            om::PearlPoolSource::kMaxInboundBufferedBytes, 'x'));
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                                   &transport, &clock, &mailbox);
        om::Job job;
        check(!source.fetch(&job) && !transport.isOpen() &&
                  source.lastError().find("64 KiB") != std::string::npos,
              "unterminated line reaching the bound is rejected and closed");
        const auto events = drainAll(mailbox);
        check(exactKinds(events,
                         {om::PearlSourceEventKind::SessionStarted,
                          om::PearlSourceEventKind::TransportConnected,
                          om::PearlSourceEventKind::SourceFault,
                          om::PearlSourceEventKind::Disconnected}) &&
                  events[2].reason == om::PearlSourceReason::Protocol &&
                  events[3].reason == om::PearlSourceReason::Protocol,
              "oversize framing is an ordered protocol fault");
    }
}

void replacementAndOutcomesTest() {
    std::puts("replacement, sequential solutions, and exact outcomes:");
    FakeClock clock;
    FakeTransport transport(clock);
    om::PearlSourceObservabilityMailbox mailbox(clock);
    prime(transport);
    om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                               &transport, &clock, &mailbox);
    om::Job oldJob;
    source.fetch(&oldJob);
    drainAll(mailbox);
    transport.afterSubmitBytes(notify("job-2", 2) + "\n" +
                    "{\"id\":101,\"error\":null,\"result\":true}\n");
    std::string err;
    check(source.submit(oldJob, solution(), &err),
          "first old-job solution accepted across pushed replacement");
    const std::size_t wireAfterFirst = transport.wire.size();
    check(!source.submit(oldJob, solution(), &err) &&
              transport.wire.size() == wireAfterFirst && transport.isOpen(),
          "second already-known old-job solution is locally NotSent");
    auto events = drainAll(mailbox, 3);
    std::vector<om::PearlSourceEvent> terminals;
    for (const auto &event : events)
        if (event.kind == om::PearlSourceEventKind::SubmitTerminal)
            terminals.push_back(event);
    const bool replacementOrder = exactKinds(
        events,
        {om::PearlSourceEventKind::SubmitDispatched,
         om::PearlSourceEventKind::JobRetired,
         om::PearlSourceEventKind::JobAvailable,
         om::PearlSourceEventKind::SubmitTerminal,
         om::PearlSourceEventKind::SubmitDispatched,
         om::PearlSourceEventKind::SubmitTerminal});
    check(replacementOrder && terminals.size() == 2 &&
              events[0].jobToken == events[1].jobToken &&
              events[2].jobToken != events[1].jobToken &&
              terminals[0].jobToken == terminals[1].jobToken &&
              terminals[0].jobIdentity.bytes == terminals[1].jobIdentity.bytes &&
              terminals[0].operationToken != terminals[1].operationToken &&
              terminals[0].superseded && !terminals[1].superseded &&
              terminals[1].terminal == om::PearlSourceTerminal::LocalFailure &&
              terminals[1].delivery == om::PearlSourceDelivery::NotSent &&
              terminals[1].reason == om::PearlSourceReason::Stale &&
              !terminals[1].acknowledged && source.submitCounters().submitted == 1 &&
              source.submitCounters().acceptedSuperseded == 1 &&
              source.submitCounters().stale == 0,
          "inflight acceptance retained; later local drop never counts pool stale");
    om::Job replacement;
    check(source.fetch(&replacement) &&
              !canonicalJobsMatch(oldJob, replacement),
          "next fetch releases old binding and issues replacement work");

    const char *replies[] = {
        "{\"id\":102,\"error\":{\"message\":\"bad share\"},\"result\":null}\n",
        "{\"id\":103,\"error\":{\"message\":\"stale share\"},\"result\":null}\n",
        "{\"id\":104,\"error\":null,\"result\":null}\n",
    };
    const om::PearlSourceTerminal expected[] = {
        om::PearlSourceTerminal::Rejected,
        om::PearlSourceTerminal::Stale,
        om::PearlSourceTerminal::Unknown,
    };
    for (int i = 0; i < 3; ++i) {
        transport.afterSubmitBytes(replies[i]);
        source.submit(replacement, solution(), &err);
        const auto one = drainAll(mailbox);
        const auto *terminal =
            lastKind(one, om::PearlSourceEventKind::SubmitTerminal);
        check(terminal && terminal->terminal == expected[i] &&
                  terminal->acknowledged,
              i == 0 ? "correlated rejection maps exactly"
                     : i == 1 ? "correlated stale maps exactly"
                              : "correlated unknown remains acknowledged unknown");
    }
}

void preSubmitIntakeTest() {
    std::puts("bounded pre-submit intake and local-only retirement:");
    for (int scenario = 0; scenario < 4; ++scenario) {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(),
                                   "", &transport, &clock, &mailbox);
        om::Job job;
        check(source.fetch(&job), "pre-submit identity fixture starts");
        drainAll(mailbox);
        const std::size_t before = transport.wire.size();
        const int requestBefore = om::PearlPoolSourceTestAccess::requestId(source);
        // notify() inserts JSON source text; a binary NUL must be escaped on
        // the wire, then decoded into the exact six-byte canonical job ID.
        // Scenario 0 changes height, so the replaced job is not in the same-height
        // history (H-019 D1); the same-height bind has its own test.
        if (scenario == 0) transport.bytes(notify("new\\u0000id", 1, "000000000000000000000000000000000000000000000000000000000000ffff", "139054") + "\n");
        // Scenario 1 reuses the current job ID with a different job at the same
        // height: ambiguous under live v28f, refused locally as invalid (H-019 D2).
        if (scenario == 1) transport.bytes(notify("job-1", 2) + "\n");
        if (scenario == 2) transport.bytes(
            "{\"method\":\"mining.set_target\",\"params\":{\"target\":\"0000000000000000000000000000000000000000000000000000000000000fff\"}}\n");
        if (scenario == 3) transport.bytes(
            "{\"method\":\"mining.notify\",\"params\":{}}\n");
        transport.maximumReadWait = 0;
        std::string error;
        check(!source.submit(job, solution(), &error) && transport.wire.size() == before &&
                  transport.isOpen() && transport.maximumReadWait == 0 &&
                  om::PearlPoolSourceTestAccess::issued(source) &&
                  om::PearlPoolSourceTestAccess::requestId(source) == requestBefore,
              "queued identity/target/invalid replacement drops without send or issued release");
        const auto events = drainAll(mailbox);
        const auto *terminal = lastKind(events, om::PearlSourceEventKind::SubmitTerminal);
        check(!events.empty() && events.front().kind == om::PearlSourceEventKind::SubmitDispatched &&
                  terminal && terminal->terminal == om::PearlSourceTerminal::LocalFailure &&
                  terminal->delivery == om::PearlSourceDelivery::NotSent &&
                  !terminal->acknowledged && !terminal->superseded &&
                  terminal->reason == (scenario == 1 || scenario == 3 ? om::PearlSourceReason::InvalidJob :
                                                                        om::PearlSourceReason::Stale) &&
                  source.submitCounters().submitted == 0 && source.submitCounters().stale == 0 &&
                  source.submitCounters().unknown == 0 && source.submitCounters().acknowledged == 0,
              "local terminal is not a remote stale/unknown/acknowledgment");
        check(!source.submit(job, solution(), &error) && transport.wire.size() == before,
              "sequential old proof also stays local with retained issued binding");
        drainAll(mailbox);
        if (scenario >= 1) transport.bytes(notify("replacement", 3) + "\n");
        om::Job fresh;
        check(source.fetch(&fresh), "replacement can be fetched after local drop");
        if (scenario == 0)
            check(fresh.jobId == std::string("new\0id", 6),
                  "escaped replacement preserves exact embedded-NUL identity");
        transport.afterSubmitBytes("{\"id\":101,\"result\":true,\"error\":null}\n");
        check(source.submit(fresh, solution(), &error) && source.submitCounters().accepted == 1 &&
                  om::PearlPoolSourceTestAccess::requestId(source) == requestBefore + 1,
              "next fresh proof uses first unconsumed wire request ID");
    }
    for (int scenario = 0; scenario < 11; ++scenario) {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(),
                                   "", &transport, &clock, &mailbox);
        om::Job job;
        source.fetch(&job);
        drainAll(mailbox);
        const auto before = transport.wire.size();
        const unsigned receivesBefore = transport.receiveCalls;
        const uint64_t timeBefore = clock.now;
        if (scenario == 0) transport.bytes("{\"method\":\"mining.notify\"");
        if (scenario == 1) transport.eof();
        if (scenario == 2) transport.error();
        if (scenario == 3) transport.failNextReadWait = true;
        if (scenario == 4 || scenario == 5) {
            transport.bytes(notify("job-1", 1) + "\n");
            transport.failNextSetup = scenario == 4;
            transport.failNextRestore = scenario == 5;
        }
        if (scenario == 6) transport.bytes("{\"id\":101,\"result\":true,\"error\":null}\n");
        if (scenario == 7) {
            std::string burst;
            for (int i = 0; i < 65; ++i) burst += notify("job-1", 1) + "\n";
            transport.bytes(burst);
        }
        if (scenario == 8) for (int i = 0; i < 17; ++i) transport.bytes(" ");
        if (scenario == 9) transport.bytes(std::string(64 * 1024 + 1, 'x'));
        if (scenario == 10) {
            transport.bytes("{");
            transport.receives.push_back({om::PearlPoolTransferStatus::WouldBlock, {}});
        }
        transport.maximumReadWait = 0;
        std::string error;
        check(!source.submit(job, solution(), &error) && transport.wire.size() == before &&
                  !transport.isOpen() && clock.now == timeBefore && transport.maximumReadWait == 0 &&
                  transport.receiveCalls - receivesBefore <= 16,
              "partial/error/mode/future-reply/budget failure closes with zero send/wait");
        const auto events = drainAll(mailbox);
        const auto terminal = std::find_if(events.begin(), events.end(), [](const om::PearlSourceEvent &e) {
            return e.kind == om::PearlSourceEventKind::SubmitTerminal;
        });
        const auto disconnected = std::find_if(events.begin(), events.end(), [](const om::PearlSourceEvent &e) {
            return e.kind == om::PearlSourceEventKind::Disconnected;
        });
        check(terminal != events.end() && disconnected != events.end() && terminal < disconnected &&
                  terminal->delivery == om::PearlSourceDelivery::NotSent &&
                  terminal->terminal == om::PearlSourceTerminal::LocalFailure &&
                  !terminal->acknowledged && !terminal->superseded &&
                  source.submitCounters().submitted == 0 && source.submitCounters().unknown == 0 &&
                  source.submitCounters().stale == 0 && source.submitCounters().pending() == 0,
              "local terminal precedes disconnect, with no fabricated pool outcome");
    }
    for (bool extraReady : {false, true}) {
        FakeClock clock;
        FakeTransport transport(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(),
                                   "", &transport, &clock);
        om::Job job;
        source.fetch(&job);
        std::string line = notify("job-1", 1);
        line.append(om::PearlPoolSource::kMaxInboundBufferedBytes - line.size() - 1, ' ');
        line += '\n';
        om::PearlPoolSourceTestAccess::buffered(source, std::move(line));
        if (extraReady) transport.bytes(notify("job-1", 1) + "\n");
        else transport.afterSubmitBytes("{\"id\":101,\"result\":true,\"error\":null}\n");
        const auto bytesBefore = transport.wire.size();
        const auto receivesBefore = transport.receiveCalls;
        std::string error;
        const bool delivered = source.submit(job, solution(), &error);
        check(extraReady ? !delivered && !transport.isOpen() &&
                           transport.wire.size() == bytesBefore && transport.receiveCalls == receivesBefore :
                           delivered && source.submitCounters().accepted == 1,
              "already-buffered64KiB spends budget; further readable input cannot be bypassed");
    }
    {
        FakeClock clock;
        FakeTransport transport(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(),
                                   "", &transport, &clock);
        om::Job job;
        source.fetch(&job);
        std::string burst;
        for (int i = 0; i < 64; ++i) burst += notify("job-1", 1) + "\n";
        transport.bytes(burst);
        transport.afterSubmitBytes("{\"id\":101,\"result\":true,\"error\":null}\n");
        std::string error;
        check(source.submit(job, solution(), &error) && source.submitCounters().accepted == 1,
              "exactly64 identical canonical notifications retain dispatch eligibility");
    }
}

void reconnectAndCounterRefusalTest() {
    std::puts("reconnect identity and counter refusal:");
    FakeClock clock;
    FakeTransport transport(clock);
    om::PearlSourceObservabilityMailbox mailbox(clock);
    prime(transport, "same", 9);
    om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                               &transport, &clock, &mailbox);
    om::Job first;
    source.fetch(&first);
    auto firstEvents = drainAll(mailbox);
    const auto *firstAvailable =
        lastKind(firstEvents, om::PearlSourceEventKind::JobAvailable);
    const uint64_t firstSession = firstAvailable ? firstAvailable->sessionToken : 0;
    const auto firstDigest = firstAvailable ? firstAvailable->jobIdentity.bytes
                                            : om::PearlJobIdentityDigest().bytes;

    transport.eof();
    om::Job ignored;
    check(!source.fetch(&ignored), "EOF during fetch closes first session");
    drainAll(mailbox);
    prime(transport, "same", 9);
    om::Job second;
    check(source.fetch(&second), "source reconnects without replaying a submit");
    auto secondEvents = drainAll(mailbox);
    const auto *secondAvailable =
        lastKind(secondEvents, om::PearlSourceEventKind::JobAvailable);
    check(secondAvailable && secondAvailable->sessionToken != firstSession &&
              secondAvailable->jobIdentity.bytes == firstDigest,
          "identical work after reconnect gets new session token, same digest");
    check(std::count(transport.wire.begin(), transport.wire.end(), '\n') == 2,
          "reconnect wrote only two authorization requests and no submit replay");

    auto &counters = om::PearlPoolSourceTestAccess::counters(source);
    counters.submitted = UINT64_MAX;
    counters.acknowledged = UINT64_MAX;
    counters.accepted = UINT64_MAX;
    transport.afterSubmitBytes("{\"id\":101,\"error\":null,\"result\":true}\n");
    std::string err;
    source.submit(second, solution(), &err);
    auto refused = drainAll(mailbox);
    const auto terminal = std::find_if(
        refused.begin(), refused.end(), [](const om::PearlSourceEvent &event) {
            return event.kind == om::PearlSourceEventKind::SubmitTerminal;
        });
    const auto fault = std::find_if(
        refused.begin(), refused.end(), [](const om::PearlSourceEvent &event) {
            return event.kind == om::PearlSourceEventKind::SourceFault;
        });
    check(exactKinds(refused,
                     {om::PearlSourceEventKind::SubmitDispatched,
                      om::PearlSourceEventKind::SubmitTerminal,
                      om::PearlSourceEventKind::SourceFault}) &&
              terminal != refused.end() && fault != refused.end() &&
              terminal < fault &&
              terminal->delivery == om::PearlSourceDelivery::Sent &&
              terminal->terminal == om::PearlSourceTerminal::Accepted &&
              terminal->acknowledged && !terminal->superseded &&
              terminal->reason == om::PearlSourceReason::None &&
              fault->reason == om::PearlSourceReason::Protocol &&
              counters.submitted == UINT64_MAX &&
              counters.acknowledged == UINT64_MAX &&
              counters.accepted == UINT64_MAX,
          "counter refusal preserves remote terminal then records local fault");
}

void targetRetirementTest() {
    std::puts("target retirement:");
    FakeClock clock;
    FakeTransport transport(clock);
    om::PearlSourceObservabilityMailbox mailbox(clock);
    prime(transport);
    om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                               &transport, &clock, &mailbox);
    om::Job job;
    source.fetch(&job);
    drainAll(mailbox);
    transport.bytes(
        "{\"id\":null,\"method\":\"mining.set_target\",\"params\":{"
        "\"target\":\"0000000000000000000000000000000000000000000000000000000000000fff\"}}\n"
        "{\"id\":null,\"method\":\"mining.notify\",\"params\":{}}\n" +
        notify("job-after-target", 4) + "\n");
    check(source.fetch(&job) && job.target[0] == 0xfff,
          "malformed notify cannot spend target staged for next valid notify");
    auto valid = drainAll(mailbox);
    check(countKind(valid, om::PearlSourceEventKind::JobRetired) == 1 &&
              countKind(valid, om::PearlSourceEventKind::JobAvailable) == 1 &&
              countKind(valid, om::PearlSourceEventKind::SourceFault) == 1,
          "staged target refresh records retirement, malformed fault, and availability");
    transport.bytes(
        "{\"id\":null,\"method\":\"mining.set_target\",\"params\":{"
        "\"target\":\"zz\"}}\n");
    check(!source.fetch(&job), "invalid target push retires live work");
    auto events = drainAll(mailbox);
    check(events.size() >= 2 &&
              events[0].kind == om::PearlSourceEventKind::JobRetired &&
              events[1].kind == om::PearlSourceEventKind::SourceFault,
          "invalid target records retirement before source fault");
}

void observabilityFailureDoesNotChangeMiningTest() {
    std::puts("observability fail-closed isolation:");
    FakeClock clock;
    FakeTransport transport(clock);
    om::PearlSourceTokenSeeds seeds;
    seeds.session = UINT64_MAX;
    om::PearlSourceObservabilityMailbox mailbox(clock, seeds);
    prime(transport);
    om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(), "",
                               &transport, &clock, &mailbox);
    om::Job job;
    check(source.fetch(&job),
          "mailbox token exhaustion does not suppress valid fetched work");
    transport.afterSubmitBytes("{\"id\":101,\"error\":null,\"result\":true}\n");
    std::string err;
    check(source.submit(job, solution(), &err) &&
              source.submitCounters().accepted == 1,
          "mailbox failure does not fabricate or suppress network outcome");
    check(mailbox.snapshot().failClosed && drainAll(mailbox).empty(),
          "failed mailbox stays explicit and publishes no invented events");
}

void diagnosticCompletionSourceTest() {
    struct Writer final : om::PearlLocalRetirementWriter {
        bool partial=false,flushFailure=false,closeFailure=false;
        unsigned writes=0,closes=0;
        std::string bytes;
        std::size_t write(const char *p,std::size_t n) noexcept override {
            ++writes; const auto size=partial?n/2:n;
            try { bytes.append(p,size); } catch (...) { return 0; }
            return size;
        }
        bool flush() noexcept override { return !flushFailure; }
        bool close() noexcept override { ++closes; return !closeFailure; }
    };
    for (unsigned mode=0; mode<6; ++mode) {
        FakeClock clock; Writer writer; FakeTransport transport(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid",1,"prl1test","rig",sourcePolicy(),"",&transport,&clock);
        if (mode!=5) check(source.enableDiagnosticCompletion(&writer),"completion tracking enabled before source work");
        writer.partial=mode==1; writer.flushFailure=mode==2;
        om::Job job; check(source.fetch(&job),"diagnostic fixture receives canonical job via fake transport");
        if (mode==5) {
            check(!source.enableDiagnosticCompletion(&writer) && writer.writes==0,
                  "cannot enable tracking after unrecorded source history"); continue;
        }
        if (mode==3) writer.closeFailure=true;
        if (mode==4) om::PearlPoolSourceTestAccess::counters(source).submitted=1;
        om::PearlEvidenceDigest digest;
        const bool finished=source.finishDiagnosticRun(&digest);
        check(finished==(mode==0) && digest.closed==(mode==0),
              "source completion refuses historical write/flush/close faults and unresolved pool pending");
        if (mode==0) {
            uint8_t expected[32];
            om::pearl::b3::hash(nullptr,reinterpret_cast<const uint8_t *>(writer.bytes.data()),writer.bytes.size(),expected);
            check(digest.bytes==writer.bytes.size() && !std::memcmp(expected,digest.digest.data(),32) &&
                  !om::PearlPoolSourceTestAccess::issued(source) && !transport.isOpen(),
                  "checked source close releases issued binding and hashes exact diagnostics");
        }
        if (mode==4) check(source.submitCounters().pending()==1 && source.submitCounters().unknown==0 && writer.closes==0,
                          "unresolved submission is not fabricated as Unknown or cleared by completion");
        else check(writer.closes==1,"source stream close attempted even with historical I/O failure");
        const auto opens=transport.openCalls;
        check(!source.fetch(&job) && transport.openCalls==opens && !source.finishDiagnosticRun(&digest),
              "source diagnostic finish is one-shot and prevents reconnect/reuse");
    }
}
void diagnosticFileOwnershipTest() {
#ifndef _WIN32
    struct Directory {
        char path[64]="/tmp/pearl-evidence-file-XXXXXX";
        bool created=mkdtemp(path)!=nullptr;
        ~Directory() {
            if (!created) return;
            for (const char *name : {"wire","link","bytes"}) {
                const std::string file=std::string(path)+"/"+name;
                if (unlink(file.c_str())!=0 && errno!=ENOENT) std::abort();
            }
            if (rmdir(path)!=0) std::abort();
        }
    } directory;
    check(directory.created,"private evidence-file directory created");
    if (!directory.created) return;
    const std::string wire=std::string(directory.path)+"/wire", link=std::string(directory.path)+"/link";
    auto read = [](const std::string &path) {
        std::string bytes;
        FILE *file=std::fopen(path.c_str(),"rb");
        if (!file) std::abort();
        char buffer[4096]; std::size_t n;
        while ((n=std::fread(buffer,1,sizeof(buffer),file))!=0) bytes.append(buffer,n);
        if (std::ferror(file) || std::fclose(file)!=0) std::abort();
        return bytes;
    };
    const std::string sentinel="prior failed artifact\r\nkeep exact\n";
    {
        om::PearlLocalRetirementFile file;
        check(file.open(wire.c_str()) && file.write(sentinel.data(),sentinel.size())==sentinel.size() &&
              file.flush() && file.close(),"create previous wire artifact without text translation");
    }
    check(symlink(wire.c_str(),link.c_str())==0,"create isolated final-component symlink");
    for (const auto &path : {wire,link}) {
        FakeClock clock; FakeTransport transport(clock);
        om::PearlPoolSource source("example.invalid",1,"prl1test","rig",sourcePolicy(),path,
                                   &transport,&clock,nullptr,true);
        om::Job job;
        check(!source.enableDiagnosticCompletion() && !source.fetch(&job) && transport.openCalls==0,
              "exclusive evidence source refuses existing file/symlink with no transport fallback");
        check(read(wire)==sentinel,"existing wire bytes preserved exactly on refused open");
    }
    if (unlink(link.c_str())!=0 || unlink(wire.c_str())!=0) std::abort();
    {
        FakeClock clock; FakeTransport transport(clock); prime(transport);
        om::PearlPoolSource source("example.invalid",1,"prl1test","rig",sourcePolicy(),wire,
                                   &transport,&clock,nullptr,true);
        om::Job job; om::PearlEvidenceDigest digest;
        check(source.enableDiagnosticCompletion() && source.fetch(&job) && source.finishDiagnosticRun(&digest),
              "actual exclusive wire file completes through fake-source lifecycle");
        const auto bytes=read(wire); uint8_t expected[32];
        om::pearl::b3::hash(nullptr,reinterpret_cast<const uint8_t *>(bytes.data()),bytes.size(),expected);
        check(!bytes.empty() && bytes.back()=='\n' && bytes.find('\r')==std::string::npos &&
              digest.bytes==bytes.size() && !std::memcmp(expected,digest.digest.data(),32),
              "wire final hash and count describe actual binary file bytes including exact LF separators");
    }
    {
        const std::string path=std::string(directory.path)+"/bytes";
        om::PearlLocalRetirementFile file; om::PearlEvidenceStream stream; om::PearlEvidenceDigest digest;
        const char payload[]={'\n','\r','\n','\0','x','\n'};
        check(file.open(path.c_str()) && stream.configure(&file,1024) &&
              stream.append(payload,sizeof(payload)) && stream.finish(&digest),"binary evidence stream closes");
        const auto bytes=read(path); uint8_t expected[32];
        om::pearl::b3::hash(nullptr,reinterpret_cast<const uint8_t *>(bytes.data()),bytes.size(),expected);
        check(bytes==std::string(payload,sizeof(payload)) && digest.bytes==sizeof(payload) &&
              !std::memcmp(expected,digest.digest.data(),32),"literal LF/CRLF/NUL bytes match retained hash");
    }
#endif
}
}  // namespace

// H-019 task 3/4 (H-002): gzip v2 is used only when offered AND the pool answers type v2.
void h019ProofCompressionTest() {
    std::puts("H-019 v2 gzip negotiation and submit framing:");
    const std::string v2Reply = "{\"id\":1,\"result\":true,\"error\":null,\"type\":\"v2\"}\n";
    const std::string plainReply = "{\"id\":1,\"result\":true,\"error\":null}\n";
    const std::string jobLine = notify("job-1", 1) + "\n";
    {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        transport.bytes(jobLine + v2Reply);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(),
                                   "", &transport, &clock, &mailbox);
        check(source.setProofCompression(true, ""), "v2 offer accepted with payout login");
        om::Job job;
        check(source.fetch(&job) && transport.wire.find("\"type\":\"v2\"") != std::string::npos,
              "authorize offers type v2 when compression is auto");
        transport.afterSubmitBytes("{\"id\":101,\"error\":null,\"result\":true}\n");
        std::string err;
        check(source.submit(job, solution(), &err) &&
                  transport.wire.find("\"worker\":\"prl1test.rig\"") != std::string::npos &&
                  transport.wire.find("\"plain_proof\":\"H4sI") != std::string::npos,
              "v2 submit carries base64(gzip) proof and the login in worker");
    }
    {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        transport.bytes(jobLine + plainReply);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(),
                                   "", &transport, &clock, &mailbox);
        check(source.setProofCompression(true, "acct_1"), "v2 offer accepted with a named account");
        om::Job job;
        check(source.fetch(&job), "pool without type answers the plain dialect");
        transport.afterSubmitBytes("{\"id\":101,\"error\":null,\"result\":true}\n");
        std::string err;
        check(source.submit(job, solution(), &err) &&
                  transport.wire.find("\"plain_proof\":\"proof\"") != std::string::npos &&
                  transport.wire.find("\"worker\":\"prl1test.rig\"") == std::string::npos,
              "no type reply means uncompressed submit without worker (never gzip)");
    }
    {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        transport.bytes(jobLine + v2Reply);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(),
                                   "", &transport, &clock, &mailbox);
        check(source.setProofCompression(false, ""), "pool.compression off is accepted");
        om::Job job;
        check(source.fetch(&job) && transport.wire.find("\"type\"") == std::string::npos,
              "compression off never sends type");
    }
}

// H-019 D1/D2 (live v28r/v28f): a same-height replaced job is sent once, and a
// reused transport job ID rebinds only to a job that can no longer receive a proof.
void h019SameHeightAndJobIdTest() {
    std::puts("H-019 same-height bind and transport job-ID reuse:");
    const std::string target =
        "000000000000000000000000000000000000000000000000000000000000ffff";
    {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(),
                                   "", &transport, &clock, &mailbox);
        om::Job oldJob;
        check(source.fetch(&oldJob), "same-height fixture starts");
        drainAll(mailbox);
        transport.bytes(notify("job-2", 2, target, "139053") + "\n");
        transport.afterSubmitBytes("{\"id\":101,\"error\":null,\"result\":true}\n");
        std::string err;
        const std::size_t wireBefore = transport.wire.size();
        check(source.submit(oldJob, solution(), &err) && transport.wire.size() > wireBefore,
              "same-height replaced job is sent once (v28r)");
        const std::size_t wireAfter = transport.wire.size();
        check(!source.submit(oldJob, solution(), &err) && transport.wire.size() == wireAfter &&
                  transport.isOpen(),
              "same-height replaced job refuses a repeated proof locally");
        om::Solution other = solution();
        other.extra = "proof-2";
        check(!source.submit(oldJob, other, &err) && transport.wire.size() == wireAfter &&
                  transport.isOpen(),
              "same-height replaced job is spent after its one submit");
    }
    {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(),
                                   "", &transport, &clock, &mailbox);
        om::Job oldJob;
        check(source.fetch(&oldJob), "target-change fixture starts");
        drainAll(mailbox);
        transport.bytes(notify("job-2", 2, "0000000000000000000000000000000000000000000000000000000000000fff",
                               "139053") + "\n");
        const std::size_t before = transport.wire.size();
        std::string err;
        check(!source.submit(oldJob, solution(), &err) && transport.wire.size() == before,
              "a same-height job with a changed target is not bindable");
    }
    {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(),
                                   "", &transport, &clock, &mailbox);
        om::Job first, second, reused;
        check(source.fetch(&first), "reuse fixture starts");
        transport.bytes(notify("job-2", 2, target, "139054") + "\n");
        check(source.fetch(&second) && second.jobId == "job-2", "a higher job replaces job-1");
        transport.bytes(notify("job-1", 3, target, "139055") + "\n");
        check(source.fetch(&reused) && reused.jobId == "job-1" && reused.epoch == 139055 &&
                  source.lastError().empty(),
              "a reused job ID rebinds once its old job is below both heights (v28f)");
    }
    {
        FakeClock clock;
        FakeTransport transport(clock);
        om::PearlSourceObservabilityMailbox mailbox(clock);
        prime(transport);
        om::PearlPoolSource source("example.invalid", 1, "prl1test", "rig", sourcePolicy(),
                                   "", &transport, &clock, &mailbox);
        om::Job live;
        check(source.fetch(&live), "ambiguous fixture starts");
        transport.bytes(notify("job-1", 2, target, "139053") + "\n");
        std::string err;
        check(!source.submit(live, solution(), &err) &&
                  source.lastError().find("ambiguous reused transport job ID") != std::string::npos,
              "a reused job ID while its old job is live is ambiguous and refused (v28f)");
    }
}

namespace {

std::string h025Address() { return "prl1" + std::string(59, 'q'); }

// Parses 64 big-endian hex digits into limbs (limb 0 least significant).
bool h025Limbs(const char *hex, uint64_t out[4]) {
    if (std::strlen(hex) != 64) return false;
    for (int i = 0; i < 4; ++i) out[i] = 0;
    for (int k = 0; k < 64; ++k) {
        const char c = hex[k];
        const int digit = c >= '0' && c <= '9'   ? c - '0'
                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                 : -1;
        if (digit < 0) return false;
        const int bit = 4 * (63 - k);
        out[bit / 64] |= static_cast<uint64_t>(digit) << (bit % 64);
    }
    return true;
}

bool h025SameLimbs(const uint64_t a[4], const uint64_t b[4]) {
    return std::memcmp(a, b, 4 * sizeof(uint64_t)) == 0;
}

}  // namespace

// H-025 tasks 1-4: a v2 login is account-or-address + '.' + worker; non-v2 stays bare.
void h025V2LoginTest() {
    std::puts("H-025 v2 login golden authorize lines:");
    const std::string address = h025Address();
    const std::string agent = "CanaryMiner/" CANARY_MINER_VERSION;
    const std::string v2Reply = "{\"id\":1,\"result\":true,\"error\":null,\"type\":\"v2\"}\n";
    const std::string plainReply = "{\"id\":1,\"result\":true,\"error\":null}\n";
    const std::string tail = "\",\"worker\":\"rig\",\"pass\":\"x\",\"agent\":\"" + agent + "\"";
    const std::string head = "{\"id\":1,\"method\":\"mining.authorize\",\"params\":{\"wallet\":\"";
    {
        FakeClock clock; FakeTransport transport(clock);
        transport.bytes(v2Reply);
        om::PearlPoolSource source("fixture.invalid", 1200, address, "rig", sourcePolicy(),
                                   "", &transport, &clock, nullptr);
        check(source.setProofCompression(true, ""), "Kryptex-style v2 offer with empty account");
        check(om::PearlPoolSourceTestAccess::connect(source) &&
                  transport.wire == head + address + ".rig" + tail + ",\"type\":\"v2\"}}\n",
              "Kryptex login is address.worker with type v2 (golden)");
    }
    {
        FakeClock clock; FakeTransport transport(clock);
        transport.bytes(v2Reply);
        om::PearlPoolSource source("fixture.invalid", 5500, address, "rig", sourcePolicy(),
                                   "", &transport, &clock, nullptr);
        check(source.setProofCompression(true, "acct_1"), "F2Pool-style v2 offer with account");
        check(om::PearlPoolSourceTestAccess::connect(source) &&
                  transport.wire == head + "acct_1.rig" + tail + ",\"type\":\"v2\"}}\n",
              "F2Pool login is account.worker, address never sent as wallet (golden)");
    }
    {
        FakeClock clock; FakeTransport transport(clock);
        transport.bytes(plainReply);
        om::PearlPoolSource source("fixture.invalid", 1200, address, "rig", sourcePolicy(),
                                   "", &transport, &clock, nullptr);
        check(om::PearlPoolSourceTestAccess::connect(source) &&
                  transport.wire == head + address + tail + "}}\n",
              "HeroMiners-style non-v2 login is byte-identical (golden)");
    }
    {
        FakeClock clock; FakeTransport transport(clock);
        transport.bytes(plainReply);
        om::PearlPoolSource source("fixture.invalid", 1200, address, "rig", sourcePolicy(),
                                   "", &transport, &clock, nullptr);
        check(source.setProofCompression(false, "") &&
                  om::PearlPoolSourceTestAccess::connect(source) &&
                  transport.wire == head + address + tail + "}}\n",
              "compression off on Kryptex keeps the legacy bare-address login");
    }
    {
        FakeClock clock; FakeTransport transport(clock);
        om::PearlPoolSource source("fixture.invalid", 1200, address + ".rig", "rig", sourcePolicy(),
                                   "", &transport, &clock, nullptr);
        check(!source.setProofCompression(true, "") && source.lastError().find("--worker") != std::string::npos &&
                  transport.openCalls == 0,
              "a dot in the payout address is refused before the login is composed");
    }
    {
        FakeClock clock; FakeTransport transport(clock);
        om::PearlPoolSource source("fixture.invalid", 1200, address, "rig", sourcePolicy(),
                                   "", &transport, &clock, nullptr, false, true);
        check(!source.setProofCompression(true, "") && transport.openCalls == 0,
              "v2 login is refused together with SOLO authorization");
    }
}

// H-025 tasks 6-7 (D6, H-023 digest §4): compact-floor the raw v2 target before the bound.
void h025CompactFloorTest() {
    std::puts("H-025 D6 compact-floor vectors (live pearl_pool_target.py):");
    struct Vector { const char *raw; const char *floored; };
    const Vector vectors[] = {
        // H-023 worked example (>> branch with a 26-byte value).
        {"0000000000000488c8c53a59f4ff03c663eb961fba36883b122a9385bd6c6396",
         "0000000000000488c80000000000000000000000000000000000000000000000"},
        // Differs only below the mantissa: same floored value.
        {"0000000000000488c8ffffffffffffffffffffffffffffffffffffffffffffff",
         "0000000000000488c80000000000000000000000000000000000000000000000"},
        // Already compact-exact: unchanged.
        {"000000000000000000000000000000000000000000000000000000000000ffff",
         "000000000000000000000000000000000000000000000000000000000000ffff"},
        // Top bit set in a 3-byte word: size becomes 4 and the mantissa shifts.
        {"0000000000000000000000000000000000000000000000000000000000800001",
         "0000000000000000000000000000000000000000000000000000000000800000"},
        {"0000000000000000000000000000000000000000000000000000008123456789",
         "0000000000000000000000000000000000000000000000000000008123000000"},
        {"0000000000000000000000000000000000000000000000000000000080123456",
         "0000000000000000000000000000000000000000000000000000000080120000"},
        // 32-byte value whose floor has 33 significant bytes after the shift.
        {"8000000000000000000000000000000000000000000000000000000000abcdef",
         "8000000000000000000000000000000000000000000000000000000000000000"},
        {"7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
         "7fffff0000000000000000000000000000000000000000000000000000000000"},
    };
    int vectorIndex = 0;
    for (const Vector &v : vectors) {
        uint64_t raw[4], expected[4], actual[4] = {0, 0, 0, 0};
        const bool parsed = h025Limbs(v.raw, raw) && h025Limbs(v.floored, expected);
        const std::string name = "compact floor vector " + std::to_string(++vectorIndex) +
                                 " matches live compact_floor";
        check(parsed && om::pearlPoolCompactFloor(raw, actual) && h025SameLimbs(actual, expected),
              name.c_str());
    }
    {
        const uint64_t zero[4] = {0, 0, 0, 0};
        uint64_t out[4] = {0, 0, 0, 0};
        check(!om::pearlPoolCompactFloor(zero, out),
              "zero target has no compact floor and is refused");
    }
    {
        // The penalty factor is the compact-floor adjustment (2^21). The live
        // K8192 profile is commonDim 8192; the default test build is 2048.
        om::pearl::MiningConfig config{8192, 128, 16, 16};
        om::pearl::U256 one, scaled;
        one.v[0] = 1;
        check(config.penalizedTarget(one, &scaled) && scaled.v[0] == 2097152 &&
                  scaled.v[1] == 0 && scaled.v[2] == 0 && scaled.v[3] == 0,
              "penalty factor 2097152 equals K8192_ADJUSTMENT");
    }
    {
        // Resolved bound uses the floored target; identity keeps the raw target.
        om::pearl::MiningConfig config{om::pearl::kCudaCommonDim, 128, 16, 16};
        const char *rawA = vectors[0].raw, *rawB = vectors[1].raw;
        om::Job jobA, jobB;
        std::string error;
        om::PearlJobContextError code = om::PearlJobContextError::InvalidJob;
        check(om::pearlPoolCanonicalJobFromNotify(notify("job-d6-a", 41, rawA, "139053"),
                                                  sourcePolicy(), &jobA, &error, &code) &&
                  om::pearlPoolCanonicalJobFromNotify(notify("job-d6-b", 42, rawB, "139053"),
                                                      sourcePolicy(), &jobB, &error, &code),
              "D6 fixtures parse through the canonical notify gate");
        om::PearlPoolCanonicalView viewA, viewB, rawView;
        uint64_t floorA[4] = {0, 0, 0, 0};
        om::pearl::U256 expectedA, expectedRawA;
        const bool floorOk = om::pearlPoolCompactFloor(jobA.target, floorA);
        check(floorOk && config.penalizedTarget(om::pearl::U256::fromLimbs(floorA), &expectedA) &&
                  config.penalizedTarget(om::pearl::U256::fromLimbs(jobA.target), &expectedRawA),
              "expected penalized bounds computed for the D6 fixture");
        check(om::pearlPoolCanonicalViewForJob(jobA, sourcePolicy(), config, &viewA, &error, &code, true) &&
                  om::pearlPoolCanonicalViewForJob(jobB, sourcePolicy(), config, &viewB, &error, &code, true) &&
                  om::pearlPoolCanonicalViewForJob(jobA, sourcePolicy(), config, &rawView, &error, &code, false),
              "D6 views resolve for v2 and non-v2 sessions");
        check(std::memcmp(viewA.resolvedTarget.v, expectedA.v, sizeof(expectedA.v)) == 0,
              "v2 resolved bound is floor(raw) * 2^21");
        check(std::memcmp(viewB.resolvedTarget.v, viewA.resolvedTarget.v, sizeof(viewA.resolvedTarget.v)) == 0,
              "raw targets differing below the mantissa share one effective bound");
        check(std::memcmp(rawView.resolvedTarget.v, expectedRawA.v, sizeof(expectedRawA.v)) == 0,
              "non-v2 resolved bound is unchanged raw * 2^21");
        check(std::memcmp(viewA.identity.rawTarget.v, jobA.target, sizeof(jobA.target)) == 0 &&
                  std::memcmp(viewB.identity.rawTarget.v, jobB.target, sizeof(jobB.target)) == 0,
              "identity keeps the raw target, not the floored value");
        check(!canonicalJobsMatch(jobA, jobB),
              "raw targets that differ stay distinct for jobsMatch (D2 sees raw)");
    }
}

// H-025 D6 on the GPU publication path: a v2 job's search bound is the same
// floored bound the CPU view resolves; raw stays in sourceTarget. Non-v2 is unchanged.
void h025GpuBoundTest() {
    std::puts("H-025 D6 GPU publication bound (buildPreparedJobHost):");
    const std::string raw = "0000000000000488c8c53a59f4ff03c663eb961fba36883b122a9385bd6c6396";
    om::pearl::MiningConfig config{8192, 128, 16, 16};
    om::pearl::PreparedJobRequest request;
    request.config = config;
    request.m = 16384;
    request.n = 131072;
    request.preparationPlanId = 1;
    request.mode = om::pearl::PreparedJobMode::Normal;
    const om::PearlSourcePolicy policy = sourcePolicy();
    om::Job v2Job, plainJob;
    std::string error;
    om::PearlJobContextError code = om::PearlJobContextError::InvalidJob;
    check(om::pearlPoolCanonicalJobFromNotify(notify("job-gpu-v2", 51, raw, "139053"),
                                              policy, &v2Job, &error, &code) &&
              om::pearlPoolCanonicalJobFromNotify(notify("job-gpu-plain", 51, raw, "139053"),
                                                  policy, &plainJob, &error, &code),
          "GPU fixtures parse through the canonical notify gate");
    v2Job.compactFloorTarget = true;

    uint64_t floorTarget[4] = {0, 0, 0, 0};
    uint64_t rawLimbs[4] = {0, 0, 0, 0};
    check(h025Limbs(raw.c_str(), rawLimbs) && om::pearlPoolCompactFloor(rawLimbs, floorTarget),
          "compact floor of the GPU fixture");
    om::pearl::U256 expectedV2, expectedPlain;
    check(config.penalizedTarget(om::pearl::U256::fromLimbs(floorTarget), &expectedV2) &&
              config.penalizedTarget(om::pearl::U256::fromLimbs(rawLimbs), &expectedPlain),
          "expected GPU bounds computed");

    om::pearl::PreparedJobHost v2Host, plainHost;
    check(om::pearl::buildPreparedJobHost(v2Job, &policy, request, &v2Host, &error, &code) &&
              om::pearl::buildPreparedJobHost(plainJob, &policy, request, &plainHost, &error, &code),
          "prepared host builds for v2 and non-v2 jobs");
    check(std::memcmp(v2Host.resolvedBound.v, expectedV2.v, sizeof(expectedV2.v)) == 0 &&
              v2Host.compactFloorTarget,
          "v2 GPU search bound is floor(raw) * 2^21");
    check(std::memcmp(plainHost.resolvedBound.v, expectedPlain.v, sizeof(expectedPlain.v)) == 0 &&
              !plainHost.compactFloorTarget,
          "non-v2 GPU search bound is unchanged raw * 2^21");
    om::PearlPoolCanonicalView cpuView;
    check(om::pearlPoolCanonicalViewForJob(v2Job, policy, config, &cpuView, &error, &code, true) &&
              std::memcmp(cpuView.resolvedTarget.v, v2Host.resolvedBound.v,
                          sizeof(v2Host.resolvedBound.v)) == 0,
          "v2 GPU bound equals the host CPU resolved bound");
    check(std::memcmp(v2Host.sourceTarget.data(), rawLimbs, sizeof(rawLimbs)) == 0,
          "v2 prepared host keeps the raw target as source identity");
    check(om::pearl::preparedJobHostConsistent(v2Host) &&
              om::pearl::preparedJobHostConsistent(plainHost),
          "both prepared hosts re-derive consistently (floor flag in identity)");
}

int main() {
    h019SameHeightAndJobIdTest();
    h019ProofCompressionTest();
    h025V2LoginTest();
    h025CompactFloorTest();
    h025GpuBoundTest();
    canonicalContextParserTest();
    canonicalStageBHelpersTest();
    canonicalSourceIntegrationTest();
    lifecycleAndDuplicateTest();
    invalidWorkTest();
    submitEvidenceTest();
    timeoutAndEofTest();
    localValidationAndEmptyBatchLifecycleTest();
    correlationBudgetIsolationTest();
    soloAuthorizationTest();
    connectAndAuthorizationFailureTest();
    inboundBoundTest();
    replacementAndOutcomesTest();
    preSubmitIntakeTest();
    reconnectAndCounterRefusalTest();
    targetRetirementTest();
    observabilityFailureDoesNotChangeMiningTest();
    diagnosticCompletionSourceTest();
    diagnosticFileOwnershipTest();
    std::printf("\nPearl pool source observability: %s\n",
                failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
