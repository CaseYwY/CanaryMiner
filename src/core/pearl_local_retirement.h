// Opt-in local evidence only. Never a wire request or a pool outcome.
#pragma once
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif
#include "pearl_proof_ownership.h"
#include "pearl_run_progress_coordinator.h"
#include "pearl_evidence_io.h"

namespace om {
inline bool pearlLocalRetirementAdmission(const char *value, bool canonicalPool,
                                         bool transcript) noexcept {
    return !value || (!std::strcmp(value, "1") && canonicalPool && transcript);
}
struct PearlLocalRetirement {
    PearlRawTerminalEvidence terminal;
    PearlProofBinding binding;
    uint64_t candidate = 0, submitGeneration = 0, ordinal = 0;
    uint64_t expectedOperation = 0;
};
inline const char *pearlLocalRetirementReason(PearlSourceReason reason) noexcept {
    switch (reason) {
        case PearlSourceReason::Transport: return "transport";
        case PearlSourceReason::Protocol: return "protocol";
        case PearlSourceReason::Authentication: return "authentication";
        case PearlSourceReason::Timeout: return "timeout";
        case PearlSourceReason::RemoteRejected: return "remote_rejected";
        case PearlSourceReason::Stale: return "stale";
        case PearlSourceReason::InvalidJob: return "invalid_job";
        case PearlSourceReason::Shutdown: return "shutdown";
        case PearlSourceReason::None: return nullptr;
    }
    return nullptr;
}
inline bool pearlLocalRetirementValid(const PearlLocalRetirement &r) noexcept {
    const auto &e = r.terminal.event;
    return r.terminal.trust == PearlRawTerminalTrust::Trusted && r.terminal.observation &&
        e.kind == PearlSourceEventKind::SubmitTerminal && e.eventToken &&
        e.operationToken && e.operationToken == r.expectedOperation &&
        e.delivery == PearlSourceDelivery::NotSent &&
        e.terminal == PearlSourceTerminal::LocalFailure &&
        e.terminalEvidence == PearlSourceTerminalEvidence::LocalPreDeliveryFailure &&
        !e.acknowledged && !e.superseded && !e.reserved1 &&
        pearlLocalRetirementReason(e.reason) &&
        pearlProofJobIdentityValid(r.binding.job) &&
        pearlSerializedProofMetadataValid(r.binding.proof) &&
        r.binding.token && r.binding.searchGeneration && r.candidate &&
        r.submitGeneration && r.ordinal && r.ordinal <= kPearlProofOwnershipMaxPerSearch &&
        e.sessionToken == r.binding.job.session &&
        e.authorizationToken == r.binding.job.authorization &&
        e.jobToken == r.binding.job.job && e.jobIdentity.bytes == r.binding.job.digest.bytes;
}
class PearlLocalRetirementSink {
 public:
    virtual ~PearlLocalRetirementSink() = default;
    virtual bool retain(const PearlLocalRetirement &) noexcept = 0;
};
class PearlLocalRetirementJsonSink final : public PearlLocalRetirementSink {
 public:
    static constexpr uint64_t maxRecords = 4096;
    static constexpr std::size_t maxBytes = 8 * 1024 * 1024;
    explicit PearlLocalRetirementJsonSink(PearlLocalRetirementWriter &writer) : writer_(writer) {}
    PearlLocalRetirementJsonSink(const PearlLocalRetirementJsonSink &) = delete;
    PearlLocalRetirementJsonSink &operator=(const PearlLocalRetirementJsonSink &) = delete;
    const char *failure() const noexcept { return failure_; }
    uint64_t records() const noexcept { return records_; }
    std::size_t bytes() const noexcept { return bytes_; }
    const char *runId() const noexcept { return runId_; }
    bool beginRun(const char *id) noexcept {
        if (failure_ || started_ || records_ || bytes_ || retaining_ || finished_ || !pearlEvidenceRunIdValid(id))
            return fail("run_start");
        std::memcpy(runId_,id,33);
        char line[192];
        const int n=std::snprintf(line,sizeof(line),
            "{\"schema\":\"pearl-local-retirement-v1\",\"event\":\"local_retirement_run_start\",\"run_id\":\"%s\"}\n",runId_);
        if (n<=0 || static_cast<std::size_t>(n)>=sizeof(line) || !writeBytes(line,static_cast<std::size_t>(n)))
            return fail("run_start");
        started_=true; return true;
    }
    bool finish(PearlEvidenceDigest *out) noexcept {
        if (out) *out={};
        if (!out || retaining_ || finished_ || !started_) return fail("finish");
        retaining_=true;
        const bool flushed=writer_.flush();
        const bool closed=writer_.close();
        retaining_=false; finished_=true;
        if (failure_ || !flushed || !closed) return fail("close");
        *out={static_cast<uint64_t>(bytes_),hash_.digest(),true}; return true;
    }
    bool retain(const PearlLocalRetirement &r) noexcept override {
        if (failure_) return false;
        if (finished_) return fail("finished");
        if (retaining_) return fail("reentry");
        if (!pearlLocalRetirementValid(r)) return fail("invalid_binding");
        const auto &e = r.terminal.event;
        if (e.eventToken <= event_ || e.operationToken <= operation_ ||
            r.terminal.observation <= observation_ || r.binding.token <= proof_ ||
            r.candidate <= candidate_ || r.submitGeneration <= submit_)
            return fail("duplicate_or_reordered");
        if (records_ == (writer_.segmented() ? uint64_t(INT64_MAX) : maxRecords)) return fail("record_limit");
        char job[65], fingerprint[65], line[2048];
        hex(r.binding.job.digest.bytes.data(), job);
        hex(r.binding.proof.fingerprint.bytes.data(), fingerprint);
        const int length = std::snprintf(line, sizeof(line),
            "{\"schema\":\"pearl-local-retirement-v1\",\"event\":\"local_submit_terminal\","
            "\"sequence\":%llu,\"source_event_token\":%llu,\"source_timestamp_ms\":%llu,"
            "\"observation\":%llu,\"session_token\":%llu,\"authorization_token\":%llu,"
            "\"job_token\":%llu,\"job_identity\":\"%s\",\"operation_token\":%llu,"
            "\"search_generation\":%llu,\"proof_token\":%llu,\"candidate_token\":%llu,"
            "\"submit_generation\":%llu,\"proof_ordinal\":%llu,\"nonce\":\"%016llx\","
            "\"solution_fingerprint\":\"%s\",\"delivery\":\"not_sent\","
            "\"terminal\":\"local_failure\",\"terminal_evidence\":\"local_pre_delivery_failure\","
            "\"acknowledged\":false,\"superseded\":false,\"reason\":\"%s\"}\n",
            static_cast<unsigned long long>(records_ + 1),
            static_cast<unsigned long long>(e.eventToken),
            static_cast<unsigned long long>(e.timestampMs),
            static_cast<unsigned long long>(r.terminal.observation),
            static_cast<unsigned long long>(e.sessionToken),
            static_cast<unsigned long long>(e.authorizationToken),
            static_cast<unsigned long long>(e.jobToken), job,
            static_cast<unsigned long long>(e.operationToken),
            static_cast<unsigned long long>(r.binding.searchGeneration),
            static_cast<unsigned long long>(r.binding.token),
            static_cast<unsigned long long>(r.candidate),
            static_cast<unsigned long long>(r.submitGeneration),
            static_cast<unsigned long long>(r.ordinal),
            static_cast<unsigned long long>(r.binding.proof.nonce), fingerprint,
            pearlLocalRetirementReason(e.reason));
        if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(line)) return fail("encoding");
        const auto size = static_cast<std::size_t>(length);
        if (!writeBytes(line,size)) return false;
        ++records_;
        event_ = e.eventToken; operation_ = e.operationToken;
        observation_ = r.terminal.observation; proof_ = r.binding.token;
        candidate_ = r.candidate; submit_ = r.submitGeneration;
        return true;
    }
 private:
    bool writeBytes(const char *line,std::size_t size) noexcept {
        if (failure_ || retaining_ || finished_) return fail("reentry");
        if (size > (writer_.segmented() ? std::size_t(INT64_MAX) : maxBytes) - bytes_) return fail("byte_limit");
        // No retry after a partial write/flush: preserve the incomplete artifact.
        retaining_ = true;
        struct Reset { bool &active; ~Reset() { active = false; } } reset{retaining_};
        const auto written = writer_.write(line, size);
        if (failure_) return false;
        if (written != size) return fail("write");
        const bool flushed = writer_.flush();
        if (failure_) return false;
        if (!flushed) return fail("flush");
        hash_.update(line,size); bytes_ += size;
        return true;
    }
    bool fail(const char *reason) noexcept { if (!failure_) failure_ = reason; return false; }
    static void hex(const uint8_t *bytes, char *out) noexcept {
        const char digits[] = "0123456789abcdef";
        for (std::size_t i = 0; i < 32; ++i) {
            out[2 * i] = digits[bytes[i] >> 4]; out[2 * i + 1] = digits[bytes[i] & 15];
        }
        out[64] = '\0';
    }
    PearlLocalRetirementWriter &writer_;
    uint64_t records_ = 0, event_ = 0, operation_ = 0, observation_ = 0;
    uint64_t proof_ = 0, candidate_ = 0, submit_ = 0;
    std::size_t bytes_ = 0;
    const char *failure_ = nullptr;
    bool retaining_ = false;
    bool started_=false,finished_=false;
    char runId_[33]{};
    PearlEvidenceHash hash_;
};
} // namespace om
