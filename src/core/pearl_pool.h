#include "../algos/pearl-pow/cuda_common_dim.h"
// Pearl pool mining.
//
// Pearl's own tooling has no pool path at all: pearl-gateway talks to a node
// you run yourself, holds the payout address, and turns a submitted PlainProof
// into the zero-knowledge certificate a block needs. That is fine if you run a
// node. Most people will not.
//
// Pools exist anyway, and they speak a stratum of their own. This is the wire,
// established by probing a live pool because nothing documents it:
//
//   -> {"id":1,"method":"mining.authorize","params":{"wallet":"prl1...",
//         "worker":"rig1","pass":"x","agent":"CanaryMiner/1.0.0"}}
//   <- {"id":1,"error":null,"result":true}
//   <- {"id":null,"method":"mining.notify","params":{
//         "job_id":"00000000_2097152",
//         "header":"<152 hex chars, the 76-byte incomplete header>",
//         "target":"<64 hex chars, big endian>",
//         "height":101009,"cert_version":3}}
//   -> {"id":9,"method":"mining.submit","params":{
//         "job_id":"...","plain_proof":"<base64 bincode PlainProof>"}}
//
// Three things are worth knowing before changing any of it:
//
//   * PARAMS ARE OBJECTS, not arrays. Sending stratum's usual positional array
//     gets {"code":20,"msg":"params must be an object"}.
//   * THE WALLET FIELD IS `wallet`. Not `login`, not `user` - both of those
//     authorize "successfully" and then fail with {"code":24,"Wallet is
//     missing"}, which is a confusing way to find out.
//   * The proof we already build for the gateway is EXACTLY what the pool
//     wants. `plain_proof` is the same base64 bincode PlainProof. Nothing
//     about the mining changes, only the transport.
//
// The pool PUSHES work rather than answering polls, so fetch() drains whatever
// has arrived and reports the newest job it has seen, instead of asking.

#pragma once

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#endif

#include "algo.h"
#include "canary_version.h"
#include "http.h"
#include "json_lite.h"   // jsonRawValue/jsonUnquote: read the reply fields, not punctuation
#include "platform.h"
#include "pearl_job_extra.h"   // packPearlExtra, unpackPearlExtra, pearlDecimalU256
#include "pearl_job_context.h"
#include "pearl_gzip.h"          // H-019 task 3: gzip for the v2 submit dialect
#include "pearl_pool_transport.h"
#include "pearl_source_observability.h"
#include "pearl_evidence_io.h"
#include "pearl_proof_timing.h"
#include "submit_outcome.h"
#include "run.h"
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
#ifdef _WIN32
#error "Guard retirement experiment requires owned Linux anonymous pipes"
#endif
#include "pearl_guard_retirement_v2.h"
#endif

namespace om {

/** Big-endian hex, as the pool sends targets, into the little-endian limbs
 *  Job::target carries. Getting this backwards yields a target that is either
 *  absurdly easy or unreachable, and both look like "the pool hates us". */
inline bool hexToLimbs(const std::string &hex, uint64_t out[4]) {
    uint8_t b[32];
    if (!hexToBytes(hex, b, 32)) return false;
    for (int limb = 0; limb < 4; limb++) {
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | b[(3 - limb) * 8 + i];
        out[limb] = v;
    }
    return true;
}

// ------------------------------------------------------- 256-bit division
//
// Only needed to turn a difficulty into a target. Bitwise long division rather
// than a limb algorithm on purpose: 256 iterations is free at the rate a pool
// changes difficulty, and it needs no `__int128`, which MSVC does not have and
// which `U256::mul` is already written in 32-bit limbs to avoid.

inline bool u256IsZero(const uint64_t a[4]) {
    return (a[0] | a[1] | a[2] | a[3]) == 0;
}

inline bool u256Less(const uint64_t a[4], const uint64_t b[4]) {
    for (int i = 3; i >= 0; i--)
        if (a[i] != b[i]) return a[i] < b[i];
    return false;
}

inline void u256Sub(uint64_t a[4], const uint64_t b[4]) {
    uint64_t borrow = 0;
    for (int i = 0; i < 4; i++) {
        const uint64_t bi = b[i] + borrow;
        // `b[i] + borrow` can wrap to 0 when b[i] is all ones; that is still a
        // borrow out, so test the addition itself rather than only a[i] < bi.
        borrow = (borrow && bi == 0) ? 1 : (a[i] < bi ? 1 : 0);
        a[i] -= bi;
    }
}

inline void u256Shl1(uint64_t a[4], uint64_t in) {
    for (int i = 0; i < 4; i++) {
        const uint64_t carry = a[i] >> 63;
        a[i] = (a[i] << 1) | in;
        in = carry;
    }
}

/** q = num / den. False when den is zero, which no pool difficulty can be. */
inline bool u256Div(const uint64_t num[4], const uint64_t den[4], uint64_t q[4]) {
    if (u256IsZero(den)) return false;
    uint64_t rem[4] = {0, 0, 0, 0};
    uint64_t quo[4] = {0, 0, 0, 0};
    for (int bit = 255; bit >= 0; bit--) {
        const uint64_t b = (num[bit / 64] >> (bit % 64)) & 1ULL;
        u256Shl1(rem, b);
        if (!u256Less(rem, den)) {
            u256Sub(rem, den);
            quo[bit / 64] |= 1ULL << (bit % 64);
        }
    }
    memcpy(q, quo, sizeof(quo));
    return true;
}

/**
 * Pearl Stratum's difficulty, as a share target.
 *
 * `target = 0xFFFF * 2^208 / difficulty`, the ordinary Bitcoin-style mapping.
 * That is not an assumption: HeroMiners names the difficulty in its job id
 * (`00000000_2097152`) and the `target` it sends alongside decodes to exactly
 * `0xFFFF * 2^187`, which is `0xFFFF * 2^208 / 2097152`. Measured against the
 * live pool on 2026-08-20.
 *
 * The result is the same LITTLE-endian limb layout `Job::target` carries, so it
 * is interchangeable with what hexToLimbs() produces from a notify.
 */
inline bool pearlDifficultyToTarget(const std::string &decDifficulty,
                                    uint64_t out[4]) {
    uint64_t den[4] = {0, 0, 0, 0};
    if (!pearlDecimalU256(decDifficulty, den)) return false;
    if (u256IsZero(den)) return false;
    // 0xFFFF * 2^208: bit 208 is limb 3, bit 16.
    const uint64_t num[4] = {0, 0, 0, 0xFFFFULL << 16};
    uint64_t parsed[4];
    if (!u256Div(num, den, parsed) || u256IsZero(parsed)) return false;
    memcpy(out, parsed, sizeof(parsed));
    return true;
}

/** The job id rides in `extra` behind the header and cert version, so submit()
 *  can name the job the proof belongs to. unpackPearlExtra still parses the
 *  first two fields unchanged, so a gateway job and a pool job share a format. */
inline std::string packPoolExtra(const uint8_t header[76], int cert,
                                 const std::string &jobId) {
    return packPearlExtra(header, cert) + ":" + jobId;
}
inline std::string poolJobId(const std::string &extra) {
    const size_t first = extra.find(':');
    if (first == std::string::npos) return "";
    const size_t second = extra.find(':', first + 1);
    return second == std::string::npos ? "" : extra.substr(second + 1);
}

/** The JSON-RPC method this line names, or empty.
 *
 * Every dispatch here reads this rather than searching the line for the method
 * name, because a substring search matches the name wherever it appears -
 * inside a job id, inside a pool's error text, or as the prefix of a longer
 * method. handlePush() ate any line containing "mining.notify" and returned
 * true, which in the submit reply loop means the share's own verdict can be
 * swallowed as if it were a pushed job. Same rule as the reply parsing below:
 * parse the field, never match the bytes. */
inline std::string pearlPoolMethod(const std::string &line) {
    std::string m;
    if (!jsonObjectStringStrict(line, "method", &m)) return std::string();
    return m;
}

/** Exact canonical JSON uint64 token; no sign, fraction, exponent or padding. */
inline bool pearlPoolCanonicalUint64(const std::string &text,
                                     uint64_t *value) {
    if (!value || text.empty() ||
        (text.size() > 1 && text.front() == '0'))
        return false;
    uint64_t parsed = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (parsed > (UINT64_MAX - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    *value = parsed;
    return true;
}

/** Require an exact ASCII hex token before the legacy sscanf-based decoder.
 * `%2x` accepts a valid one-digit prefix (for example `0g`), and the shared
 * decoder otherwise treats that malformed pair as success. */
inline bool pearlPoolExactHex(const std::string &s, size_t chars) {
    if (s.size() != chars) return false;
    for (unsigned char ch : s)
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
              (ch >= 'A' && ch <= 'F')))
            return false;
    return true;
}

/**
 * A pushed share-target change, from either notification a stratum can use.
 *
 * Pearl Stratum V1 has NO difficulty negotiation of its own: every
 * `mining.notify` carries the final `target`, and HeroMiners sends nothing
 * else. Ergo (`stratum.cpp`) handles `mining.set_difficulty` and
 * `mining.set_target`, and BC3 (`stratum_btc`) handles `set_difficulty`, so a
 * Pearl pool that borrows either idiom is plausible and used to be silently
 * ignored here - the miner would have kept mining the last notify's target
 * while the pool graded against a new one, which reads as
 * "hash does not meet difficulty target" and nothing else.
 *
 * Accepts the object form (`{"difficulty":N}` / `{"target":"<hex>"}`) and the
 * single-element positional difficulty array. Difficulty is an exact positive
 * canonical integer: accepting or truncating fractions could make the local
 * target easier than the pool's and produce rejected shares.
 *
 * Returns false for any other line, and for a value that cannot be used.
 */
inline bool pearlPoolTargetFromPush(const std::string &line, uint64_t out[4]) {
    const std::string method = pearlPoolMethod(line);
    if (method == "mining.set_target") {
        std::string params, hex;
        uint64_t parsed[4];
        if (!jsonObjectRawValueStrict(line, "params", &params) ||
            !jsonObjectStringStrict(params, "target", &hex) ||
            !pearlPoolExactHex(hex, 64) || !hexToLimbs(hex, parsed) ||
            u256IsZero(parsed))
            return false;
        memcpy(out, parsed, sizeof(parsed));
        return true;
    }
    if (method == "mining.set_difficulty") {
        std::string params, dec;
        if (!jsonObjectRawValueStrict(line, "params", &params)) return false;
        if (params.empty()) return false;
        if (params.front() == '{') {
            if (!jsonObjectRawValueStrict(params, "difficulty", &dec)) return false;
        } else if (!jsonArraySingleRawValueStrict(params, &dec)) {
            return false;
        }
        return !dec.empty() && pearlDifficultyToTarget(dec, out);
    }
    return false;
}

enum class PearlPoolTargetPushStatus { NotPush, Applied, Invalid };

/** Apply a target push to the exact state handlePush owns.
 *
 * An unrelated reply leaves state alone. A syntactically identified target
 * push is authoritative for the next job, so a valid value is held pending
 * while the current job is retired. An invalid value retires both the pending
 * target and live work rather than letting the miner continue on obsolete
 * state.
 */
inline PearlPoolTargetPushStatus pearlPoolApplyTargetPush(
        const std::string &line, uint64_t pendingTarget[4],
        bool *havePending, uint64_t liveTarget[4], bool *haveJob) {
    if (!pendingTarget || !havePending || !liveTarget || !haveJob)
        return PearlPoolTargetPushStatus::Invalid;
    const std::string method = pearlPoolMethod(line);
    if (method != "mining.set_target" && method != "mining.set_difficulty")
        return PearlPoolTargetPushStatus::NotPush;
    uint64_t parsed[4];
    if (!pearlPoolTargetFromPush(line, parsed)) {
        memset(pendingTarget, 0, sizeof(parsed));
        memset(liveTarget, 0, sizeof(parsed));
        *havePending = false;
        *haveJob = false;
        return PearlPoolTargetPushStatus::Invalid;
    }
    memcpy(pendingTarget, parsed, sizeof(parsed));
    *havePending = true;
    // Standard Stratum semantics apply a pushed difficulty to the next job.
    // Retire the current one now so no share can be graded under a target whose
    // association with that header is unknown.
    memset(liveTarget, 0, sizeof(parsed));
    *haveJob = false;
    return PearlPoolTargetPushStatus::Applied;
}

/** Select the target for one already-validated notify.
 *
 * A separately pushed target belongs to the next valid job only. Consume it
 * exactly once; later notifies use their own explicit target unless another
 * push arrives. Validation happens before this helper is called, so malformed
 * notifies cannot spend the pending value.
 */
inline bool pearlPoolTargetForNotify(const uint64_t notifyTarget[4],
                                     uint64_t pendingTarget[4],
                                     bool *havePending,
                                     uint64_t selectedTarget[4]) {
    if (!notifyTarget || !pendingTarget || !havePending || !selectedTarget)
        return false;
    const uint64_t *source = *havePending ? pendingTarget : notifyTarget;
    if (u256IsZero(source)) return false;
    memcpy(selectedTarget, source, 4 * sizeof(uint64_t));
    if (*havePending) {
        memset(pendingTarget, 0, 4 * sizeof(uint64_t));
        *havePending = false;
    }
    return true;
}

/** A proof is valid only for the exact pushed job that generated it.  Compare
 * all of the fields that affect proof verification, rather than treating an
 * unchanged header as permission to keep using an earlier share target. */
inline bool pearlPoolJobsMatch(const Job &a, const Job &b) {
    return a.valid && b.valid && a.epoch == b.epoch &&
           a.extra == b.extra &&
           memcmp(a.msg, b.msg, sizeof(a.msg)) == 0 &&
           memcmp(a.target, b.target, sizeof(a.target)) == 0;
}

/** Stable, secret-free identity for exactly the fields pearlPoolJobsMatch()
 * treats as proof ownership. The domain includes its terminating NUL. Target
 * limbs are encoded in logical limb order 0..3, each as eight big-endian
 * bytes; every other integer is big-endian too. No host representation or
 * padding enters the digest. */
inline bool pearlPoolJobIdentityDigest(const Job &job,
                                       PearlJobIdentityDigest *out) {
    if (!out || !job.valid) return false;
    uint8_t header[76];
    int certificateVersion = 0;
    if (!unpackPearlExtra(job.extra, header, &certificateVersion)) return false;
    const std::string jobId = poolJobId(job.extra);
    if (certificateVersion < 1 || certificateVersion > 3 || jobId.empty() ||
        jobId.size() > UINT32_MAX ||
        job.extra != packPoolExtra(header, certificateVersion, jobId))
        return false;
    uint8_t expectedMessage[32];
    pearl::b3::hash(nullptr, header, sizeof(header), expectedMessage);
    if (memcmp(expectedMessage, job.msg, sizeof(expectedMessage)) != 0)
        return false;

    static const uint8_t domain[] = "canary-pearl-pool-job-v1";
    std::vector<uint8_t> encoded;
    encoded.reserve(sizeof(domain) + sizeof(header) + 1 + 32 + 8 + 4 +
                    jobId.size());
    encoded.insert(encoded.end(), domain, domain + sizeof(domain));
    encoded.insert(encoded.end(), header, header + sizeof(header));
    encoded.push_back(static_cast<uint8_t>(certificateVersion));
    const auto append64 = [&encoded](uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8)
            encoded.push_back(static_cast<uint8_t>(value >> shift));
    };
    for (int i = 0; i < 4; ++i) append64(job.target[i]);
    append64(job.epoch);
    const uint32_t length = static_cast<uint32_t>(jobId.size());
    for (int shift = 24; shift >= 0; shift -= 8)
        encoded.push_back(static_cast<uint8_t>(length >> shift));
    encoded.insert(encoded.end(), jobId.begin(), jobId.end());
    pearl::b3::hash(nullptr, encoded.data(), encoded.size(), out->bytes.data());
    return true;
}

class PearlPoolSteadyClock final : public PearlSourceMonotonicClock {
   public:
    uint64_t nowMs() noexcept override {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }
};

enum class PearlPoolLineStatus {
    Line,
    Timeout,
    EndOfStream,
    TransportError,
    Oversize,
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    GuardRetired,
#endif
};

struct PearlPoolSendEvidence {
    std::size_t totalBytes = 0;
    std::size_t acceptedBytes = 0;
    bool socketUsable = true;
    bool blockingRestored = true;

    bool complete() const {
        return totalBytes != 0 && acceptedBytes == totalBytes;
    }
};

/** JSON-RPC success is explicit.  In particular, `result:false` and a
 * structured `error` must not be reported as a submitted share. */
/** Did the pool accept, and if not, why.
 *
 * Read the fields rather than searching for punctuation. Two bugs lived in the
 * substring version, both found by the mock-gateway test on its first run:
 *
 *   - it required the literal `"result":true` and `"error":null`, so a server
 *     that puts a space after the colon - which is ordinary, conforming JSON -
 *     had every reply read as malformed. The login then failed with a message
 *     blaming the pool for being malformed.
 *   - the top-level msg/message check ran first and unconditionally, so a
 *     pool that sent a greeting or any informational `message` alongside a
 *     perfectly good `"result":true` had the ACCEPT counted as a rejection.
 *     That is a share credited by the pool and not by us, which is invisible
 *     until the payout does not match the counter.
 *
 * Order now: an explicit non-null `error` decides, then `result`, and the
 * top-level msg/message is only a source of WORDING for a failure - never the
 * verdict itself. */
inline bool pearlPoolReplyAccepted(const std::string &line, std::string *reason) {
    auto fallbackReason = [&](const char *dflt) {
        std::string msg;
        if (jsonObjectStringStrict(line, "msg", &msg) && !msg.empty()) {
            *reason = msg;
            return;
        }
        if (jsonObjectStringStrict(line, "message", &msg) && !msg.empty()) {
            *reason = msg;
            return;
        }
        *reason = dflt;
    };

    std::string err;
    if (jsonObjectRawValueStrict(line, "error", &err) && err != "null" &&
        !err.empty()) {
        std::string msg;
        if (jsonObjectStringStrict(err, "message", &msg) && !msg.empty())
            *reason = msg;
        else if (jsonObjectStringStrict(err, "msg", &msg) && !msg.empty())
            *reason = msg;
        else *reason = jsonUnquote(err);
        return false;
    }

    std::string result;
    if (jsonObjectRawValueStrict(line, "result", &result)) {
        if (result == "true" || result == "1") return true;
        fallbackReason("pool answered result=false with no reason");
        return false;
    }

    fallbackReason("pool returned an unsuccessful or malformed JSON-RPC reply");
    return false;
}

/** Is this line the reply to request `id`?
 *
 * Read the field. Searching for the bytes `"id":N` misses `"id": N`, which is
 * ordinary JSON - and a missed reply is not a visible failure. submit() gives
 * up with "no reply from the pool", the share is not counted, and the pool
 * credits it anyway. The counter and the payout then disagree with no error
 * anywhere, which is the single hardest bug in this file to notice. */
inline bool pearlPoolReplyIsFor(const std::string &line, int id) {
    std::string raw;
    if (!jsonObjectRawValueStrict(line, "id", &raw)) return false;
    const std::string wanted = std::to_string(id);
    if (raw == wanted) return true;
    std::string stringId;
    return jsonObjectStringStrict(line, "id", &stringId) && stringId == wanted;
}

enum class PearlPoolSubmitVerdict {
    Accepted,
    Rejected,
    Stale,
    AcceptedSuperseded,
    Unknown,
};

inline bool pearlPoolErrorArray(const std::string &raw, std::string *code,
                               std::string *message) {
    if (code) code->clear();
    if (message) message->clear();
    size_t begin=0,end=raw.size();
    while (begin<end && std::isspace(static_cast<unsigned char>(raw[begin]))) begin++;
    while (end>begin && std::isspace(static_cast<unsigned char>(raw[end-1]))) end--;
    if (end-begin<2 || raw[begin]!='[' || raw[end-1]!=']') return false;
    const std::string array=raw.substr(begin,end-begin);
    const auto fields=jsonSplitArray(array);
    if (fields.size()!=3 || fields[2]!="null") return false;
    std::string decoded;
    if (!jsonObjectStringStrict("{\"message\":"+fields[1]+"}","message",&decoded))
        return false;
    if (fields[0].empty() || (fields[0][0]=='-' && fields[0].size()==1)) return false;
    for (size_t i=fields[0][0]=='-' ? 1 : 0;i<fields[0].size();++i)
        if (!std::isdigit(static_cast<unsigned char>(fields[0][i]))) return false;
    if (code) *code=fields[0];
    if (message) *message=decoded;
    return true;
}

inline bool pearlPoolTextSaysStale(const std::string &text) {
    // Match a small, explicit vocabulary rather than substrings. A successful
    // reply may carry informational text such as "stale shares: 0" or "not
    // stale"; neither is a verdict. Normalisation is deliberately limited to
    // ASCII case, repeated whitespace and harmless terminal punctuation.
    std::string normalized;
    normalized.reserve(text.size());
    bool space = true;
    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            if (!space) normalized += ' ';
            space = true;
        } else {
            normalized += (char)std::tolower(ch);
            space = false;
        }
    }
    while (!normalized.empty() && normalized.back() == ' ')
        normalized.pop_back();
    while (!normalized.empty() &&
           (normalized.back() == '.' || normalized.back() == '!' ||
            normalized.back() == ':'))
        normalized.pop_back();
    while (!normalized.empty() && normalized.back() == ' ')
        normalized.pop_back();
    return normalized == "stale" || normalized == "stale share" ||
           normalized == "stale job" || normalized == "stale work" ||
           normalized == "job not found";
}

/** Classify one correlated submit reply against the job state after all
 * preceding pushes have been consumed.
 *
 * Replacement-notify ordering alone does not reveal when the pool graded the
 * share. Public Pearl pool contracts still define an exact result:true reply as
 * accepted, including during their retarget grace windows. Preserve the timing
 * fact as AcceptedSuperseded without removing it from accepted accounting.
 * Only a structured stale error, or rejection-shaped result with exact stale
 * top-level wording, is classified Stale. */
inline PearlPoolSubmitVerdict pearlPoolClassifySubmitReply(
        const std::string &line, bool jobSuperseded, std::string *reason) {
    if (reason) reason->clear();
    std::string directMsg, directMessage;
    jsonObjectStringStrict(line, "msg", &directMsg);
    jsonObjectStringStrict(line, "message", &directMessage);
    const std::string &directReason =
        !directMsg.empty() ? directMsg : directMessage;

    std::string errorRaw;
    const bool haveError = jsonObjectRawValueStrict(line, "error", &errorRaw);
    if (haveError && errorRaw != "null" && !errorRaw.empty()) {
        std::string errorMsg, errorMessage, errorCode, errorCodeString;
        std::string resultRaw,arrayCode,arrayMessage;
        jsonObjectRawValueStrict(line,"result",&resultRaw);
        const bool errorArray=pearlPoolErrorArray(errorRaw,&arrayCode,&arrayMessage);
        if (errorArray && resultRaw=="false" && arrayCode=="-1" && arrayMessage=="Stale share") {
            if (reason) *reason=arrayMessage;
            return PearlPoolSubmitVerdict::Stale;
        }
        jsonObjectStringStrict(errorRaw, "msg", &errorMsg);
        jsonObjectStringStrict(errorRaw, "message", &errorMessage);
        jsonObjectRawValueStrict(errorRaw, "code", &errorCode);
        jsonObjectStringStrict(errorRaw, "code", &errorCodeString);
        std::string errorReason =
            !errorMessage.empty()
                ? errorMessage
                : (!errorMsg.empty() ? errorMsg : (errorArray ? arrayMessage : jsonUnquote(errorRaw)));
        // H-019 D4 (live v28s/v28t): F2Pool's stale reply is exactly
        // result null with error {"code":21,"msg":"stale-work"}. Code 22, and
        // code 21 with any other text, is a rejection.
        const bool f2poolStale = resultRaw == "null" && errorCode == "21" &&
                                 errorMsg == "stale-work";
        if (errorCode == "22" || errorCodeString == "22") {
            if (reason) *reason = errorReason;
            return PearlPoolSubmitVerdict::Rejected;
        }
        if (f2poolStale ||
            pearlPoolTextSaysStale(errorMsg) ||
            pearlPoolTextSaysStale(errorMessage) ||
            (!errorArray && pearlPoolTextSaysStale(errorReason))) {
            if (reason)
                *reason = pearlPoolTextSaysStale(errorMessage)
                              ? errorMessage
                              : (pearlPoolTextSaysStale(errorMsg) ? errorMsg
                                                                  : errorReason);
            return PearlPoolSubmitVerdict::Stale;
        }
        if (reason) *reason = errorReason;
        return PearlPoolSubmitVerdict::Rejected;
    }

    std::string result;
    if (!jsonObjectRawValueStrict(line, "result", &result)) {
        if (reason)
            *reason = "pool returned a malformed or unreadable submit reply";
        return PearlPoolSubmitVerdict::Unknown;
    }
    const bool directStale = pearlPoolTextSaysStale(directMsg) ||
                             pearlPoolTextSaysStale(directMessage);
    if (result == "true" || result == "1") {
        if (jobSuperseded) {
            if (reason)
                *reason = "pool acknowledged the share after replacing its job";
            return PearlPoolSubmitVerdict::AcceptedSuperseded;
        }
        return PearlPoolSubmitVerdict::Accepted;
    }
    if (result == "false" || result == "0" || result == "null") {
        if (directStale) {
            if (reason)
                *reason = pearlPoolTextSaysStale(directMessage)
                              ? directMessage
                              : directMsg;
            return PearlPoolSubmitVerdict::Stale;
        }
        if (result == "null") {
            if (reason)
                *reason = "pool returned null without a structured error";
            return PearlPoolSubmitVerdict::Unknown;
        }
        if (reason)
            *reason = directReason.empty()
                          ? "pool answered result=false with no reason"
                          : directReason;
        return PearlPoolSubmitVerdict::Rejected;
    }
    if (reason)
        *reason = "pool returned a submit result with unknown meaning";
    return PearlPoolSubmitVerdict::Unknown;
}

struct PearlPoolSubmitCounters {
    uint64_t submitted = 0;
    uint64_t acknowledged = 0;
    uint64_t accepted = 0;
    uint64_t rejected = 0;
    uint64_t stale = 0;
    uint64_t acceptedSuperseded = 0;  // diagnostic subset of accepted
    uint64_t unknown = 0;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    uint64_t guardRetired = 0; // full native->guard, confirmed zero guard->pool
#endif

    uint64_t classified() const {
        const auto ordinary = saturatingCounterAdd(
            saturatingCounterAdd(saturatingCounterAdd(accepted, rejected),
                                 stale),
            unknown);
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        return saturatingCounterAdd(ordinary,guardRetired);
#else
        return ordinary;
#endif
    }
    uint64_t pending() const {
        const uint64_t done = classified();
        return submitted > done ? submitted - done : 0;
    }
    bool conserved() const {
        uint64_t remaining = submitted;
        if (accepted > remaining) return false;
        remaining -= accepted;
        if (rejected > remaining) return false;
        remaining -= rejected;
        if (stale > remaining) return false;
        remaining -= stale;
        if (unknown > remaining) return false;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        remaining -= unknown;
        if (guardRetired > remaining || acknowledged > submitted-guardRetired) return false;
#endif
        uint64_t ackRemaining = acknowledged;
        if (accepted > ackRemaining) return false;
        ackRemaining -= accepted;
        if (rejected > ackRemaining) return false;
        ackRemaining -= rejected;
        if (stale > ackRemaining) return false;
        return acknowledged <= submitted && acceptedSuperseded <= accepted;
    }
    bool noteSubmitted() {
        if (submitted == UINT64_MAX) return false;
        ++submitted;
        return true;
    }
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    bool noteGuardRetired() {
        if (!conserved() || classified()>=submitted || guardRetired==UINT64_MAX) return false;
        ++guardRetired; return true; // Never increments acknowledged/accepted.
    }
#endif
    bool noteVerdict(PearlPoolSubmitVerdict verdict,
                     bool wasAcknowledged = true) {
        if (!conserved() || classified() >= submitted) return false;
        uint64_t *terminal = nullptr;
        switch (verdict) {
            case PearlPoolSubmitVerdict::Accepted: terminal = &accepted; break;
            case PearlPoolSubmitVerdict::Rejected: terminal = &rejected; break;
            case PearlPoolSubmitVerdict::Stale: terminal = &stale; break;
            case PearlPoolSubmitVerdict::AcceptedSuperseded:
                if (acceptedSuperseded == UINT64_MAX) return false;
                terminal = &accepted;
                break;
            case PearlPoolSubmitVerdict::Unknown: terminal = &unknown; break;
        }
        if (!terminal || *terminal == UINT64_MAX ||
            (wasAcknowledged && acknowledged == UINT64_MAX))
            return false;
        if (wasAcknowledged) ++acknowledged;
        ++*terminal;
        if (verdict == PearlPoolSubmitVerdict::AcceptedSuperseded)
            ++acceptedSuperseded;
        return true;
    }
};

/** Why this string cannot be a Pearl payout address, or empty if it might be.
 *
 * The pool is the authority and rejects a bad address with `Invalid Pearl
 * address`, which does not say which part is wrong or that a template was
 * never edited. The Ergo path has checked its own address locally since the
 * beginning; Pearl only checked for empty, so the shipped placeholder reached
 * the pool and came back as that message.
 *
 * Deliberately no bech32m checksum here. A checksum this code gets subtly
 * wrong would refuse a real address, which is worse than a pool round trip.
 * These are the failures a local check can be certain about. */
inline std::string pearlWalletProblem(const std::string &w) {
    static const char *kBech32 = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    if (w.empty()) return "no address given";
    if (w.find("YOUR") != std::string::npos)
        return "this is the placeholder from the launcher, not an address - "
               "edit WALLET in mine_pearl_herominers.sh or .bat";
    if (w.rfind("tprl1", 0) == 0)
        return "this is a Pearl TESTNET address (tprl1). A mainnet pool cannot "
               "pay it";
    if (w.rfind("prl1", 0) != 0) {
        // An address from another chain cannot be paid by a Pearl pool, and no
        // setting makes it work.
        return "a Pearl address starts with prl1";
    }
    if (w.find('.') != std::string::npos)
        return "the worker name belongs in --worker, not appended to the "
               "address after a dot";
    if (w.size() < 40 || w.size() > 90)
        return "wrong length for a Pearl address (" + std::to_string(w.size()) +
               " characters; a real one is about 63)";
    for (size_t i = 4; i < w.size(); i++) {
        if (!strchr(kBech32, w[i]))
            return std::string("'") + w[i] +
                   "' cannot appear in a Pearl address";
    }
    return std::string();
}

/** The two JSON-RPC payloads whose values can originate outside this process.
 * Kept as free functions so their escaping is testable without a pool socket. */
inline std::string pearlPoolAuthorizeRequest(const std::string &wallet,
                                             const std::string &worker,
                                             const std::string &pass = "x",
                                              const std::string &agent = "CanaryMiner/" CANARY_MINER_VERSION,
                                             bool soloAuthorization = false,
                                             bool offerV2 = false) {
    if (soloAuthorization && !pearlWalletProblem(wallet).empty())
        throw std::invalid_argument("SOLO authorization requires a bare public Pearl address");
    const std::string upstreamWallet = soloAuthorization ? "solo:" + wallet : wallet;
    std::string request =
        "{\"id\":1,\"method\":\"mining.authorize\",\"params\":{\"wallet\":" +
        jsonQuoted(upstreamWallet);
    if (!worker.empty()) request += ",\"worker\":" + jsonQuoted(worker);
    if (!pass.empty()) request += ",\"pass\":" + jsonQuoted(pass);
    if (!agent.empty()) request += ",\"agent\":" + jsonQuoted(agent);
    // H-019 task 3 (H-002 spec, Kryptex gist): offer the gzip dialect. The pool
    // enables it only by answering with \"type\":\"v2\".
    if (offerV2) request += ",\"type\":\"v2\"";
    return request + "}}";
}

inline std::string pearlPoolSubmitRequest(int requestId, const std::string &jobId,
                                          const std::string &proof) {
    return "{\"id\":" + std::to_string(requestId) +
           ",\"method\":\"mining.submit\",\"params\":{\"job_id\":" +
           jsonQuoted(jobId) + ",\"plain_proof\":" + jsonQuoted(proof) + "}}";
}

/** The v2 dialect: `plain_proof` is base64(gzip(bincode)) and the login is
 * carried in `worker` (live pearl_kryptex_v2.py: account-or-wallet + '.' + worker). */
inline std::string pearlPoolSubmitRequestV2(int requestId, const std::string &jobId,
                                           const std::string &gzipProofBase64,
                                           const std::string &workerLogin) {
    return "{\"id\":" + std::to_string(requestId) +
           ",\"method\":\"mining.submit\",\"params\":{\"job_id\":" +
           jsonQuoted(jobId) + ",\"plain_proof\":" + jsonQuoted(gzipProofBase64) +
           ",\"worker\":" + jsonQuoted(workerLogin) + "}}";
}

/** The diagnostic transcript deliberately has a smaller data surface than the
 * wire protocol: no authorize request, wallet, worker, raw header, or proof
 * bytes are ever emitted. Defend the remaining free-text fields against a
 * pool echoing a PRL address in an error message or opaque job id. */
inline std::string pearlTranscriptRedact(const std::string &in) {
    std::string out;
    for (size_t p = 0; p < in.size();) {
        if (p + 4 <= in.size() && in.compare(p, 4, "prl1") == 0) {
            size_t end = p + 4;
            while (end < in.size() && std::isalnum((unsigned char)in[end])) end++;
            out += "[REDACTED_PRL_ADDRESS]";
            p = end;
        } else {
            out += in[p++];
        }
    }
    return out;
}

inline std::string pearlTargetHexBE(const uint64_t limbs[4]) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int limb = 3; limb >= 0; limb--)
        for (int byte = 7; byte >= 0; byte--) {
            const uint8_t v = (uint8_t)(limbs[limb] >> (byte * 8));
            out += hex[v >> 4];
            out += hex[v & 15];
        }
    return out;
}

inline std::string pearlTranscriptProofDigest(const std::string &proof) {
    uint8_t digest[32];
    pearl::b3::hash(nullptr, (const uint8_t *)proof.data(), proof.size(), digest);
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint8_t b : digest) { out += hex[b >> 4]; out += hex[b & 15]; }
    return out;
}

/**
 * A short fingerprint of the 76-byte header, via the digest the job already
 * carries. The header itself is deliberately never written to a transcript;
 * this is enough to tell two notifies apart and to prove which one a submitted
 * proof was mined against, which the job id alone cannot do - HeroMiners
 * advances the job id AND rewrites the header's timestamp on every push.
 */
inline std::string pearlHeaderFingerprint(const uint8_t msg[32]) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 8; i++) { out += hex[msg[i] >> 4]; out += hex[msg[i] & 15]; }
    return out;
}

inline std::string pearlTranscriptNotify(const Job &job) {
    return "{\"schema\":\"canary-pearl8-diagnostic-v1\",\"event\":\"mining.notify\",\"job_id\":" +
           jsonQuoted(pearlTranscriptRedact(poolJobId(job.extra))) +
           ",\"header_b3\":" + jsonQuoted(pearlHeaderFingerprint(job.msg)) +
           ",\"target_be\":" + jsonQuoted(pearlTargetHexBE(job.target)) +
           ",\"cert_version\":" + std::to_string(atoi(job.extra.c_str() + 153)) + "}";
}

/** A pushed share-target change, recorded so a transcript shows the bound the
 *  miner was actually using rather than only the one the last notify named. */
inline std::string pearlTranscriptTarget(const uint64_t limbs[4]) {
    return "{\"schema\":\"canary-pearl8-diagnostic-v1\",\"event\":\"mining.set_target\",\"target_be\":" +
           jsonQuoted(pearlTargetHexBE(limbs)) + "}";
}

/**
 * What the pool is about to recompute, written down before it answers.
 *
 * A rejection says only "hash does not meet difficulty target", which is the
 * same sentence for a stale header, a mis-scaled bound and a proof that
 * describes the wrong tile. These four fields separate them without another
 * run: `header_b3` ties the proof to a notify in the same file, `m`/`n`/`k`/
 * `rank` are read back out of the serialised proof rather than from the
 * miner's own variables, and `digest_be` against `bound_be` is the arithmetic
 * the pool is about to redo. A digest above the bound is our bug; a digest
 * below it that is still refused is not.
 */
// Diagnostic-only bincode prefix decoder: these untrusted dimensions are not
// proof validation and must never select a configuration or resolved bound.
inline bool pearlTranscriptDecodeProofShape(const std::string &proof,
                                            uint64_t fields[4]) {
    std::vector<uint8_t> raw;
    if (!pearl::base64Decode(proof, &raw) || raw.size() < 32) return false;
    for (int i = 0; i < 4; ++i) {
        fields[i] = 0;
        for (int b = 7; b >= 0; --b)
            fields[i] = (fields[i] << 8) | raw[i * 8 + b];
    }
    return true;
}

inline std::string pearlTranscriptSubmit(int requestId, const std::string &jobId,
                                         const std::string &proof,
                                         const std::string &headerB3,
                                         const uint64_t digest[4],
                                         const uint64_t target[4]) {
    std::string shape = "null,\"n\":null,\"k\":null,\"rank\":null";
    std::string bound = "null";
    std::string within = "null";
    uint64_t f[4] = {};
    if (pearlTranscriptDecodeProofShape(proof, f)) {
        shape = std::to_string(f[0]) + ",\"n\":" + std::to_string(f[1]) +
                ",\"k\":" + std::to_string(f[2]) + ",\"rank\":" + std::to_string(f[3]);
        pearl::MiningConfig cfg;
        cfg.commonDim = (uint32_t)f[2];
        cfg.rank = (uint16_t)f[3];
        pearl::U256 scaled;
        if (f[2] && f[3] && cfg.penalizedTarget(pearl::U256::fromLimbs(target), &scaled)) {
            bound = "\"" + pearlTargetHexBE(scaled.v) + "\"";
            within = pearl::U256::fromLimbs(digest).le(scaled) ? "true" : "false";
        }
    }
    return "{\"schema\":\"canary-pearl8-diagnostic-v1\",\"event\":\"mining.submit\",\"request_id\":" +
           std::to_string(requestId) + ",\"job_id\":" +
           jsonQuoted(pearlTranscriptRedact(jobId)) +
           ",\"header_b3\":" + jsonQuoted(headerB3) +
           ",\"m\":" + shape +
           ",\"digest_be\":" + jsonQuoted(pearlTargetHexBE(digest)) +
           ",\"target_be\":" + jsonQuoted(pearlTargetHexBE(target)) +
           ",\"bound_be\":" + bound +
           ",\"digest_within_bound\":" + within +
           ",\"plain_proof_bytes\":" + std::to_string(proof.size()) +
           ",\"plain_proof_b3\":" + jsonQuoted(pearlTranscriptProofDigest(proof)) + "}";
}

inline std::string pearlTranscriptReply(int requestId, bool accepted,
                                        const std::string &reason) {
    return "{\"schema\":\"canary-pearl8-diagnostic-v1\",\"event\":\"mining.reply\",\"request_id\":" +
           std::to_string(requestId) + ",\"accepted\":" +
           (accepted ? "true" : "false") + ",\"reason\":" +
           jsonQuoted(pearlTranscriptRedact(reason)) + "}";
}

inline std::string pearlTranscriptReply(int requestId,
                                        PearlPoolSubmitVerdict verdict,
                                        const std::string &reason) {
    const bool accepted = verdict == PearlPoolSubmitVerdict::Accepted ||
                          verdict == PearlPoolSubmitVerdict::AcceptedSuperseded;
    const bool stale = verdict == PearlPoolSubmitVerdict::Stale;
    const bool acceptedSuperseded =
        verdict == PearlPoolSubmitVerdict::AcceptedSuperseded;
    const bool unknown = verdict == PearlPoolSubmitVerdict::Unknown;
    const char *outcome = acceptedSuperseded
                              ? "accepted_superseded"
                              : (accepted
                                     ? "accepted"
                                     : (stale ? "stale"
                                              : (unknown ? "unknown"
                                                         : "rejected")));
    return "{\"schema\":\"canary-pearl8-diagnostic-v1\",\"event\":\"mining.reply\",\"request_id\":" +
           std::to_string(requestId) + ",\"accepted\":" +
           (accepted ? "true" : "false") + ",\"stale\":" +
           (stale ? "true" : "false") +
           ",\"accepted_superseded\":" +
           (acceptedSuperseded ? "true" : "false") +
           ",\"unknown\":" + (unknown ? "true" : "false") +
           ",\"outcome\":" +
           jsonQuoted(outcome) + ",\"reason\":" +
           jsonQuoted(pearlTranscriptRedact(reason)) + "}";
}

/** Parse a pushed job, including its server-selected share target, without a
 * socket.  This keeps target byte order and target-only refreshes testable. */
inline bool pearlPoolJobFromNotify(const std::string &line, Job *job) {
    if (!job || pearlPoolMethod(line) != "mining.notify") return false;
    std::string params, hdr, tgt, jid, certStr;
    if (!jsonObjectRawValueStrict(line, "params", &params) ||
        !jsonObjectStringStrict(params, "header", &hdr) ||
        !jsonObjectStringStrict(params, "target", &tgt) ||
        !jsonObjectStringStrict(params, "job_id", &jid) || jid.empty() ||
        !pearlPoolExactHex(hdr, 152) || !pearlPoolExactHex(tgt, 64))
        return false;
    uint8_t header[76];
    uint64_t target[4];
    if (!hexToBytes(hdr, header, sizeof(header)) || !hexToLimbs(tgt, target) ||
        (target[0] == 0 && target[1] == 0 && target[2] == 0 && target[3] == 0))
        return false;
    if (!jsonObjectRawValueStrict(params, "cert_version", &certStr) ||
        (certStr != "1" && certStr != "2" && certStr != "3"))
        return false;
    const int cert = certStr[0] - '0';
    // The miner's proof encoder has explicit V1/V2/V3 paths.  Guessing V3
    // for an absent or unknown pool field makes a syntactically valid but
    // cryptographically different proof.
    pearl::b3::hash(nullptr, header, sizeof(header), job->msg);
    memcpy(job->target, target, sizeof(target));
    // Observed Pearl pools send a Bitcoin-style base share target as
    // big-endian hex. PearlPow applies the mining-configuration adjustment
    // exactly once before scanning. At the current rank 128 that factor is
    // tileSize * k; do not pre-adjust it here.
    job->extra = packPoolExtra(header, cert, jid);
    job->epoch = 0;
    job->valid = true;
    return true;
}

/**
 * Canonical-only pool parser. Legacy parsing remains a fixture helper above;
 * this path never constructs or decodes a legacy envelope.
 */
inline bool pearlPoolCanonicalJobFromNotify(
    const std::string &line, const PearlSourcePolicy &policy, Job *job,
    std::string *error = nullptr,
    PearlJobContextError *code = nullptr) {
    if (error == &line ||
        (job && error && (error == &job->extra || error == &job->jobId))) {
        if (code) *code = PearlJobContextError::AliasedOutput;
        return false;
    }
    if (!job)
        return pearlSetContextError(PearlJobContextError::NullOutput, code,
                                    error, "Pearl pool job output is null");

    if (!pearlPublicNetwork(policy.network) && policy.network != PearlNetwork::Regtest)
        return pearlSetContextError(PearlJobContextError::InvalidNetwork, code,
                                    error, "invalid Pearl source network");
    if (!pearlKnownTargetSemantics(policy.targetSemantics))
        return pearlSetContextError(PearlJobContextError::InvalidTargetSemantics,
                                    code, error, "invalid Pearl source target semantics");

    std::string params, hdr, tgt, transportJobId, certText;
    std::string heightText;
    uint64_t height = 0;
    if (pearlPoolMethod(line) != "mining.notify" ||
        !jsonObjectRawValueStrict(line, "params", &params) ||
        !jsonObjectStringStrict(params, "header", &hdr) ||
        !jsonObjectStringStrict(params, "target", &tgt) ||
        !jsonObjectStringStrict(params, "job_id", &transportJobId) ||
        transportJobId.empty() ||
        !pearlPoolExactHex(hdr, 152) || !pearlPoolExactHex(tgt, 64) ||
        !jsonObjectRawValueStrict(params, "cert_version", &certText) ||
        (certText != "1" && certText != "2" && certText != "3") ||
        !jsonObjectRawValueStrict(params, "height", &heightText) ||
        !pearlPoolCanonicalUint64(heightText, &height) || height == 0)
        return pearlSetContextError(
            PearlJobContextError::InvalidJob, code, error,
            "invalid canonical Pearl pool notify or source policy");

    uint8_t header[76];
    Job parsed;
    if (!hexToBytes(hdr, header, sizeof(header)) ||
        !hexToLimbs(tgt, parsed.target) || pearlTargetIsZero(parsed.target))
        return pearlSetContextError(PearlJobContextError::InvalidHeader, code,
                                    error, "invalid Pearl pool header");
    PearlJobContext context;
    context.network = policy.network;
    context.targetSemantics = policy.targetSemantics;
    context.height = height;
    context.certificateVersion =
        static_cast<PearlCertificateVersion>(certText[0] - '0');
    memcpy(context.header.data(), header, context.header.size());
    context.transportJobId = transportJobId;

    pearl::b3::hash(nullptr, header, sizeof(header), parsed.msg);
    parsed.valid = false;
    parsed.epoch = height;
    parsed.jobId = transportJobId;
    if (!encodePearlJobContext(context, &parsed.extra, error, code))
        return false;
    parsed.valid = true;
    PearlJobContext validated;
    if (!validatePearlJobContext(parsed, &policy, &validated, error, code))
        return false;
    *job = std::move(parsed);
    return true;
}

/**
 * Canonical source identity used by the pool source and host fixtures.
 *
 * PearlPoolCanonicalIdentity contains source authority only: typed context,
 * raw transported target, and source digest. PearlPoolCanonicalView adds the
 * resolved computational bound under one MiningConfig. Keeping these separate
 * means source identity and deduplication remain available even when a tuning
 * configuration is invalid. The opaque transport id is length-delimited so
 * embedded NUL and ':' bytes remain significant.
 */
struct PearlPoolCanonicalIdentity {
    PearlJobContext context;
    pearl::U256 rawTarget;
    PearlJobIdentityDigest sourceDigest;
};

struct PearlPoolCanonicalView {
    PearlPoolCanonicalIdentity identity;
    pearl::U256 resolvedTarget;
};

inline bool pearlPoolCanonicalIdentityErrorAliases(
    const Job &job, const PearlPoolCanonicalIdentity *out,
    const std::string *error) noexcept {
    return error &&
           (error == &job.extra || error == &job.jobId ||
            (out && error == &out->context.transportJobId));
}

inline bool pearlPoolCanonicalIdentityForJob(
    const Job &job, const PearlSourcePolicy &policy,
    PearlPoolCanonicalIdentity *out,
    std::string *error = nullptr,
    PearlJobContextError *code = nullptr) noexcept {
    if (pearlPoolCanonicalIdentityErrorAliases(job, out, error)) {
        if (code) *code = PearlJobContextError::AliasedOutput;
        return false;
    }
    if (!out)
        return pearlSetContextError(PearlJobContextError::NullOutput, code,
                                    error,
                                    "Pearl canonical identity output is null");

    PearlJobContext context;
    if (!validatePearlJobContext(job, &policy, &context, error, code))
        return false;

    try {
        static constexpr uint8_t domain[] =
            "canary-pearl-pool-canonical-source-v1";
        static_assert(domain[sizeof(domain) - 1] == 0,
                      "source-identity domain includes its final NUL");
        std::vector<uint8_t> encoded;
        encoded.reserve(sizeof(domain) + context.header.size() + 2 + 1 + 8 +
                        32 + 4 + context.transportJobId.size());
        // The terminating NUL is intentionally part of the versioned domain.
        encoded.insert(encoded.end(), domain, domain + sizeof(domain));
        encoded.push_back(static_cast<uint8_t>(context.network));
        encoded.push_back(static_cast<uint8_t>(context.targetSemantics));
        encoded.push_back(static_cast<uint8_t>(context.certificateVersion));
        const auto append64 = [&encoded](uint64_t value) {
            for (int shift = 56; shift >= 0; shift -= 8)
                encoded.push_back(static_cast<uint8_t>(value >> shift));
        };
        append64(context.height);
        encoded.insert(encoded.end(), context.header.begin(),
                       context.header.end());
        // Numeric U256 big-endian: most-significant limb first, and each limb
        // most-significant byte first. This matches transcript target hex.
        for (int i = 3; i >= 0; --i) append64(job.target[i]);
        const uint32_t length =
            static_cast<uint32_t>(context.transportJobId.size());
        for (int shift = 24; shift >= 0; shift -= 8)
            encoded.push_back(static_cast<uint8_t>(length >> shift));
        encoded.insert(encoded.end(), context.transportJobId.begin(),
                       context.transportJobId.end());

        PearlPoolCanonicalIdentity next;
        next.context = std::move(context);
        next.rawTarget = pearl::U256::fromLimbs(job.target);
        pearl::b3::hash(nullptr, encoded.data(), encoded.size(),
                        next.sourceDigest.bytes.data());
        *out = std::move(next);
    } catch (const std::bad_alloc &) {
        return pearlSetContextError(PearlJobContextError::AllocationFailure,
                                    code, error,
                                    "Pearl canonical identity allocation failed");
    } catch (const std::length_error &) {
        return pearlSetContextError(PearlJobContextError::AllocationFailure,
                                    code, error,
                                    "Pearl canonical identity allocation failed");
    }
    if (code) *code = PearlJobContextError::None;
    if (error) error->clear();
    return true;
}

inline bool pearlPoolCanonicalViewErrorAliases(
    const Job &job, const PearlPoolCanonicalView *out,
    const std::string *error) noexcept {
    return error &&
           (error == &job.extra || error == &job.jobId ||
            (out && error == &out->identity.context.transportJobId));
}

inline bool pearlPoolCanonicalViewForJob(
    const Job &job, const PearlSourcePolicy &policy,
    const pearl::MiningConfig &config, PearlPoolCanonicalView *out,
    std::string *error = nullptr,
    PearlJobContextError *code = nullptr,
    bool compactFloor = false) noexcept {
    if (pearlPoolCanonicalViewErrorAliases(job, out, error)) {
        if (code) *code = PearlJobContextError::AliasedOutput;
        return false;
    }
    if (!out)
        return pearlSetContextError(PearlJobContextError::NullOutput, code,
                                    error,
                                    "Pearl canonical view output is null");

    PearlPoolCanonicalIdentity identity;
    if (!pearlPoolCanonicalIdentityForJob(job, policy, &identity, error,
                                           code))
        return false;
    // H-025 D6: a v2 session resolves the bound from the compact-floored raw
    // target. The identity above still carries the raw target (D2 sees raw).
    Job floored;
    const Job *resolveJob = &job;
    if (compactFloor) {
        floored = job;
        if (!pearlPoolCompactFloor(job.target, floored.target))
            return pearlSetContextError(PearlJobContextError::InvalidJob, code,
                                        error,
                                        "Pearl target cannot be compact-floored");
        resolveJob = &floored;
    }
    pearl::U256 resolved;
    if (!resolvePearlTarget(*resolveJob, identity.context, config, &resolved, error,
                            code))
        return false;

    PearlPoolCanonicalView next;
    next.identity = std::move(identity);
    next.resolvedTarget = resolved;
    *out = std::move(next);
    if (code) *code = PearlJobContextError::None;
    if (error) error->clear();
    return true;
}

inline bool pearlPoolCanonicalJobsMatch(
    const Job &a, const Job &b, const PearlSourcePolicy &policy, bool *same,
    std::string *error = nullptr,
    PearlJobContextError *code = nullptr) noexcept {
    if ((same && (same == &a.valid || same == &b.valid)) ||
        (error && (error == &a.extra || error == &a.jobId ||
                   error == &b.extra || error == &b.jobId))) {
        if (code) *code = PearlJobContextError::AliasedOutput;
        return false;
    }
    if (!same)
        return pearlSetContextError(PearlJobContextError::NullOutput, code,
                                    error,
                                    "Pearl canonical match output is null");
    PearlPoolCanonicalIdentity left;
    PearlPoolCanonicalIdentity right;
    if (!pearlPoolCanonicalIdentityForJob(a, policy, &left, error, code) ||
        !pearlPoolCanonicalIdentityForJob(b, policy, &right, error, code))
        return false;
    const bool equal =
        pearlJobContextEqual(left.context, right.context) &&
        memcmp(left.rawTarget.v, right.rawTarget.v,
               sizeof(left.rawTarget.v)) == 0;
    *same = equal;
    if (code) *code = PearlJobContextError::None;
    if (error) error->clear();
    return true;
}

inline bool pearlPoolCanonicalTranscriptNotify(
    const Job &job, const PearlSourcePolicy &policy,
    const pearl::MiningConfig &config, std::string *transcript,
    std::string *error = nullptr,
    PearlJobContextError *code = nullptr,
    bool compactFloor = false) noexcept {
    if (error && (error == &job.extra || error == &job.jobId ||
                  error == transcript)) {
        if (code) *code = PearlJobContextError::AliasedOutput;
        return false;
    }
    if (transcript == &job.extra || transcript == &job.jobId) {
        if (code) *code = PearlJobContextError::AliasedOutput;
        if (error) {
            try {
                *error = "Pearl canonical transcript aliases job input";
            } catch (...) {
                error->clear();
            }
        }
        return false;
    }
    if (!transcript)
        return pearlSetContextError(PearlJobContextError::NullOutput, code,
                                    error,
                                    "Pearl canonical transcript output is null");
    PearlPoolCanonicalView view;
    if (!pearlPoolCanonicalViewForJob(job, policy, config, &view, error,
                                       code, compactFloor))
        return false;
    try {
        std::string transportIdHex;
        transportIdHex.reserve(
            view.identity.context.transportJobId.size() * 2);
        pearlAppendLowerHex(
            reinterpret_cast<const uint8_t *>(
                view.identity.context.transportJobId.data()),
            view.identity.context.transportJobId.size(), &transportIdHex);
        std::string next =
            "{\"schema\":\"canary-pearl-canonical-diagnostic-v1\","
            "\"event\":\"mining.notify\",\"network\":" +
            jsonQuoted(pearlNetworkToken(view.identity.context.network)) +
            ",\"height\":" +
            std::to_string(view.identity.context.height) +
            ",\"target_semantics\":" +
            jsonQuoted(pearlTargetSemanticsToken(
                view.identity.context.targetSemantics)) +
            ",\"cert_version\":" +
            jsonQuoted(pearlCertificateToken(
                view.identity.context.certificateVersion)) +
            ",\"transport_job_id_hex\":" + jsonQuoted(transportIdHex) +
            ",\"header_b3\":" +
            jsonQuoted(pearlHeaderFingerprint(job.msg)) +
            ",\"raw_target_be\":" +
            jsonQuoted(pearlTargetHexBE(view.identity.rawTarget.v)) +
            ",\"resolved_target_be\":" +
            jsonQuoted(pearlTargetHexBE(view.resolvedTarget.v)) + "}";
        transcript->swap(next);
    } catch (const std::bad_alloc &) {
        return pearlSetContextError(PearlJobContextError::AllocationFailure,
                                    code, error,
                                    "Pearl canonical transcript allocation failed");
    } catch (const std::length_error &) {
        return pearlSetContextError(PearlJobContextError::AllocationFailure,
                                    code, error,
                                    "Pearl canonical transcript allocation failed");
    }
    if (code) *code = PearlJobContextError::None;
    if (error) error->clear();
    return true;
}

/**
 * Transactional target-push finalization. pendingTarget is borrowed and never
 * mutated; a caller may consume it only after this helper publishes a result
 * whose consumesPendingTarget flag is true. This makes malformed notifies
 * incapable of spending a staged push.
 */
struct PearlPoolCanonicalTargetFinalization {
    Job job;
    PearlPoolCanonicalView view;
    bool consumesPendingTarget = false;
};

inline bool pearlPoolFinalizeCanonicalTarget(
    const Job &notifyJob, const uint64_t pendingTarget[4], bool havePending,
    const PearlSourcePolicy &policy, const pearl::MiningConfig &config,
    PearlPoolCanonicalTargetFinalization *out,
    std::string *error = nullptr,
    PearlJobContextError *code = nullptr,
    bool compactFloor = false) noexcept {
    if (error &&
        (error == &notifyJob.extra || error == &notifyJob.jobId ||
         (out &&
          (error == &out->job.extra || error == &out->job.jobId ||
           error == &out->view.identity.context.transportJobId)))) {
        if (code) *code = PearlJobContextError::AliasedOutput;
        return false;
    }
    if (!out)
        return pearlSetContextError(
            PearlJobContextError::NullOutput, code, error,
            "Pearl canonical target-finalization output is null");
    if (havePending && !pendingTarget)
        return pearlSetContextError(PearlJobContextError::InvalidJob, code,
                                    error,
                                    "Pearl pending target input is null");

    PearlJobContext validated;
    if (!validatePearlJobContext(notifyJob, &policy, &validated, error, code))
        return false;
    const uint64_t *selected = havePending ? pendingTarget : notifyJob.target;
    if (pearlTargetIsZero(selected))
        return pearlSetContextError(PearlJobContextError::ZeroTarget, code,
                                    error,
                                    "Pearl selected target is zero");
    try {
        PearlPoolCanonicalTargetFinalization next;
        next.job = notifyJob;
        memcpy(next.job.target, selected, sizeof(next.job.target));
        if (!pearlPoolCanonicalViewForJob(next.job, policy, config,
                                           &next.view, error, code, compactFloor))
            return false;
        next.consumesPendingTarget = havePending;
        *out = std::move(next);
    } catch (const std::bad_alloc &) {
        return pearlSetContextError(PearlJobContextError::AllocationFailure,
                                    code, error,
                                    "Pearl canonical target allocation failed");
    } catch (const std::length_error &) {
        return pearlSetContextError(PearlJobContextError::AllocationFailure,
                                    code, error,
                                    "Pearl canonical target allocation failed");
    }
    if (code) *code = PearlJobContextError::None;
    if (error) error->clear();
    return true;
}

class PearlPoolSource : public JobSource {
    friend struct PearlPoolSourceTestAccess;

   public:
    // Includes the terminating newline. Normal Pearl work lines are only a
    // few hundred bytes; 64 KiB leaves ample extension room while bounding a
    // peer that streams an unterminated fragment forever.
    static constexpr std::size_t kMaxInboundBufferedBytes = 64 * 1024;

    // Injected transport/clock/mailbox objects are borrowed and must all
    // outlive this source. An external mailbox must borrow the same clock.
    PearlPoolSource(std::string host, int port, std::string wallet,
                    std::string worker, PearlSourcePolicy policy,
                    std::string transcriptPath = "",
                    PearlPoolTransport *transport = nullptr,
                    PearlSourceMonotonicClock *clock = nullptr,
                    PearlSourceObservabilityMailbox *mailbox = nullptr,
                    bool nativeEvidence = false, bool soloAuthorization = false)
        : host_(std::move(host)), wallet_(std::move(wallet)),
          worker_(std::move(worker)), transcriptPath_(std::move(transcriptPath)),
          policy_(policy), port_(port),
          transport_(transport ? transport : &defaultTransport_),
          clock_(clock ? clock : &defaultClock_),
          defaultObservability_(*clock_),
          observability_(mailbox ? mailbox : &defaultObservability_) {
        nativeEvidenceFile_=nativeEvidence;
        soloAuthorization_=soloAuthorization;
        desc_ = host_ + ":" + std::to_string(port_) + " (pearl pool)";
        if (!sourcePolicyValid()) return;
        if (!transcriptPath_.empty()) {
            transcript_ = nativeEvidence ? pearlOpenExclusiveEvidenceFile(transcriptPath_.c_str())
                                         : fopen(transcriptPath_.c_str(), "w");
            if (!transcript_)
                lastError_ = "could not open Pearl diagnostic transcript " + transcriptPath_;
        }
    }

    // Source compatibility only: omitted policy is deliberately unusable.
    // Neither fetch nor connect may guess network or target semantics.
    PearlPoolSource(std::string host, int port, std::string wallet,
                    std::string worker, std::string transcriptPath = "",
                    PearlPoolTransport *transport = nullptr,
                    PearlSourceMonotonicClock *clock = nullptr,
                    PearlSourceObservabilityMailbox *mailbox = nullptr)
        : PearlPoolSource(std::move(host), port, std::move(wallet),
                          std::move(worker), PearlSourcePolicy{},
                          std::move(transcriptPath), transport, clock, mailbox) {}

    ~PearlPoolSource() override {
        if (pendingReply_) {
            std::string error;
            pendingReply_->deadline=nowMs();
            advanceReply(*pendingReply_,&error,false);
            pendingReply_.reset();
        }
        closeConnection(PearlSourceReason::Shutdown, false);
        if (transcript_) fclose(transcript_);
    }

    const char *describe() const override { return desc_.c_str(); }
    const std::string &lastError() const { return lastError_; }

    /** H-019 task 3 (H-002 task 2): `offer` asks the pool for the gzip v2 dialect at login.
     * The dialect is used only when the pool's authorize reply says `"type":"v2"`.
     * `upstreamAccount` (may be empty) is the login sent as `worker`; empty means the payout address. */
    bool setProofCompression(bool offer, const std::string &upstreamAccount) {
        if (!offer) {
            offerV2_ = false;
            return true;
        }
        const bool accountOk = upstreamAccount.size() <= 64 &&
            std::all_of(upstreamAccount.begin(), upstreamAccount.end(), [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
            });
        const bool workerOk = !worker_.empty() && worker_.size() <= 32 &&
            std::all_of(worker_.begin(), worker_.end(), [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) != 0;
            });
        if (!accountOk || !workerOk) {
            lastError_ = "v2 pool login needs an alphanumeric worker (<=32) and an account of letters, digits or '_' (<=64)";
            return false;
        }
        // H-025 task 3: check the payout address before the login is composed.
        // A dot here would produce a login the pool reads as address + worker.
        if (wallet_.find('.') != std::string::npos) {
            lastError_ = "the worker name belongs in --worker, not appended to the address after a dot";
            return false;
        }
        // The v2 login is the account (F2Pool) or the payout address (Kryptex),
        // so it cannot be combined with the SOLO "solo:" prefix.
        if (soloAuthorization_) {
            lastError_ = "v2 pool login cannot be combined with SOLO authorization";
            return false;
        }
        v2Worker_ = (upstreamAccount.empty() ? wallet_ : upstreamAccount) + "." + worker_;
        offerV2_ = true;
        return true;
    }

    // Borrowed worker-thread-only hook. Bind before source operations and clear
    // after the final operation, before destroying context. The callback must
    // not reenter source operations; publication has already released its lock.
    // Non-event issued bind/release are mirrored explicitly by the run loop:
    // bind after fetch (before final drain), release before the next fetch.
    void setPublicationCallback(void (*callback)(void *) noexcept,
                                void *context) noexcept {
        publicationCallback_ = callback;
        publicationContext_ = context;
    }

    bool poolCounters(uint64_t *accepted, uint64_t *rejected,
                      uint64_t *pending, std::string *lastError) const override {
        *accepted = counters_.accepted;
        *rejected = counters_.rejected;
        *pending = counters_.pending();
        *lastError = lastSubmitError_;
        return true;
    }

    OutcomeSnapshotStatus outcomeSnapshot(
        PoolOutcomeSnapshot *out, std::string *lastError) const override {
        if (!out || !lastError) return OutcomeSnapshotStatus::Invalid;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        out->guardRetired = counters_.guardRetired;
#endif
        out->submitted = counters_.submitted;
        out->acknowledged = counters_.acknowledged;
        out->accepted = counters_.accepted;
        out->rejected = counters_.rejected;
        out->stale = counters_.stale;
        out->unknown = counters_.unknown;
        out->pending = counters_.pending();
        out->acceptedSuperseded = counters_.acceptedSuperseded;
        *lastError = lastSubmitError_;
        return out->conserved() ? OutcomeSnapshotStatus::Valid
                                : OutcomeSnapshotStatus::Invalid;
    }

    std::string takeRejectReason() override {
        std::string out;
        out.swap(rejectReason_);
        return out;
    }

    std::string takeJobWarning() override {
        std::string out;
        out.swap(submitWarning_);
        return out;
    }

    const PearlPoolSubmitCounters &submitCounters() const { return counters_; }
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    // Borrowed trusted supervisor channel. No environment/CLI or public route.
    // A separately retained control ledger and diagnostic stream are required.
    bool enableGuardRetirement(guard_retirement_v2::Session *session) {
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
        // Bind the trusted channel before admitting combined deferred ownership.
        // Adding one later would skip that admission's ledger/readiness checks.
        if (deferredSearchOwnership_) return false;
#endif
        if (!session || guardRetirement_ || id_!=100 || connectionGeneration_ || sessionToken_ ||
            !diagnosticEnabled_ || !diagnosticCompletionHealthy() || !sourcePolicyValid() ||
            policy_.network!=PearlNetwork::Mainnet || policy_.targetSemantics!=PearlTargetSemantics::BaseBeforePenalty ||
            !session->ready(static_cast<uint64_t>(nowMs()))) return false;
        guardRetirement_=session; return true;
    }
    guard_retirement_v2::Session *guardRetirementSession() const { return guardRetirement_; }
    bool finishGuardRetirement() {
        return guardRetirement_ && counters_.conserved() && !counters_.pending() &&
            guardRetirement_->finish(static_cast<uint64_t>(nowMs()));
    }
#endif

    // Explicit opt-in before any source operation. An injected writer is a
    // borrowed CPU-test seam and must outlive the source's diagnostic finish.
    bool enableDiagnosticCompletion(PearlLocalRetirementWriter *writer=nullptr) noexcept {
        try {
        if (diagnosticEnabled_ || diagnosticFinished_ || id_!=100 || connectionGeneration_ ||
            sessionToken_ || (!writer && (!nativeEvidenceFile_ || !transcript_)) || (writer && transcript_)) return false;
        if (observability_->snapshot().publishedEvents) return false;
        diagnosticEnabled_=true;
        return diagnosticStream_.configure(writer ? writer : &diagnosticFile_,128ULL*1024*1024);
        } catch (...) { return false; }
    }
    bool enableProductionDiagnosticCompletion(PearlLocalRetirementWriter &writer) noexcept {
        try {
        if (!writer.segmented() || diagnosticEnabled_ || diagnosticFinished_ || id_!=100 ||
            connectionGeneration_ || sessionToken_ || transcript_ ||
            observability_->snapshot().publishedEvents) return false;
        diagnosticEnabled_=true;
        return diagnosticStream_.configureProduction(&writer);
        } catch (...) { return false; }
    }
    bool finishDiagnosticRun(PearlEvidenceDigest *wire) noexcept {
        if (wire) *wire={};
        if (!wire || !diagnosticEnabled_ || diagnosticFinished_) return false;
        diagnosticFinished_=true;
        try {
            const auto state=observability_->snapshot();
            if (state.submitInFlight || !counters_.conserved() || counters_.pending()!=0) return false;
            // Owner finishSearch has released its issued slot. Align this
            // non-event binding before closeConnection's first publication,
            // so its callback does not observe a false ownership mismatch.
            // Legacy close ordering and final snapshot checks are unchanged.
            releaseIssuedJobBinding();
            closeConnection(PearlSourceReason::Shutdown,false);
            return diagnosticStream_.finish(wire);
        } catch (...) { return false; }
    }
    bool diagnosticCompletionHealthy() const noexcept {
        return !diagnosticEnabled_ || !diagnosticStream_.failed();
    }

    PearlSourceObservability *pearlSourceObservability() override {
        return observability_;
    }

    bool fetch(Job *job) override {
        if (pendingReply_) return false;
        if (diagnosticFinished_ || (diagnosticEnabled_ && diagnosticStream_.failed())) return false;
        if (!job || !sourcePolicyValid()) return false;
        // Under the current synchronous run-loop contract, the next fetch is
        // the exact point at which search and every sequential submit for the
        // previously returned Job have completed.
        releaseIssuedJobBinding();
        if (!transcriptPath_.empty() && !transcript_) return false;
        if (!transport_->isOpen() && !connectAndLogin()) return false;
        // ZERO, not 50 ms, once we have work. The core calls fetch() before
        // every search() batch, and a batch is about 10 ms of GPU time at
        // Pearl's rate - so a 50 ms wait here cost 83% of the hashrate and
        // showed up as 65 M candidates/s against 413 in the benchmark. Only
        // the FIRST job is worth blocking for.
        const bool enteredWithJob = haveJob_;
        drain(enteredWithJob ? 0 : 8000);
        // A standard set_difficulty/set_target commonly arrives immediately
        // before its notify. If the zero-wait drain consumed only that first
        // line, wait for the job it announces instead of returning a false
        // connection failure and sleeping five seconds with the notify already
        // queued behind it.
        if (enteredWithJob && !haveJob_ && havePendingTarget_ &&
            transport_->isOpen())
            drain(8000);
        if (!haveJob_) {
            if (lastError_.empty())
                lastError_ = "pool connected but sent no mining job within 8 seconds";
            return false;
        }

        try {
            Job next = activeJob_;
            if (!bindIssuedJob(next)) return false;
            *job = std::move(next);
            return true;
        } catch (const std::bad_alloc &) {
            return false;
        } catch (const std::length_error &) {
            return false;
        }
    }

   private:
    struct PendingReply {
        Job job;
        long long deadline=0;
        uint64_t operation=0;
        int requestId=0, replies=0;
        bool submitted=false, controlStarted=true;
        PearlPoolLineStatus lastRead=PearlPoolLineStatus::Timeout;
    };
    std::unique_ptr<PendingReply> pendingReply_;
    bool deferredSearchOwnership_=false;
#if defined(CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT) && defined(CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT)
    bool deferredGuardAdmitted_=false;
#endif

   public:
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
    bool enableDeferredSearchOwnership() {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        // One pending reply continues to own the guard's immutable request/job/
        // proof tuple while later searches use the separate issued-job binding.
        // Admit only the already-bound, unused, healthy supervisor channel.
        if (guardRetirement_ && (id_!=100 || pendingReply_ || counters_.submitted ||
            !counters_.conserved() || !diagnosticEnabled_ || !diagnosticCompletionHealthy() ||
            guardRetirement_->totals().fullProxy ||
            !guardRetirement_->ready(static_cast<uint64_t>(nowMs())))) return false;
#endif
        if (deferredSearchOwnership_ || connectionGeneration_ || sessionToken_ ||
            !observabilityEnabled_ || !observability_->enableDeferredIssuedOwnership()) return false;
        deferredSearchOwnership_=true;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        deferredGuardAdmitted_=guardRetirement_!=nullptr;
#endif
        return true;
    }
    // The owner must finish its previous search first. Only the reply pump may
    // read from the socket while a submission is outstanding.
    bool fetchDeferred(Job *job) {
        if (!deferredSearchOwnership_ || !job) return false;
        if (!pendingReply_) return fetch(job);
        if (diagnosticFinished_ || !diagnosticCompletionHealthy()) return false;
        releaseIssuedJobBinding();
        std::string error;
        pollDeferredSubmit(&error);
        if (!transport_->isOpen() || !haveJob_) return false;
        try {
            Job next=activeJob_;
            if (!bindIssuedJob(next)) return false;
            *job=std::move(next);
            return true;
        } catch (const std::bad_alloc &) { return false; }
          catch (const std::length_error &) { return false; }
    }
    bool releaseDeferredSearchBinding() {
        if (!deferredSearchOwnership_) return false;
        releaseIssuedJobBinding();
        return !observability_->snapshot().failClosed;
    }
    // Explicit experimental API: true means fully sent, never accepted.
    // One outstanding request only. Legacy fetch/submit are refused until drain.
    bool beginDeferredSubmit(const Job &job,const Solution &sol,std::string *err) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        if (guardRetirement_ && (!deferredGuardAdmitted_ || !deferredSearchOwnership_)) return false;
#endif
        // Shared submission still enforces guard readiness, certificate, cap and
        // deadline; shared reply advancement alone publishes its terminal fact.
        return submitImpl(job,sol,err,true);
    }
    bool hasDeferredSubmit() const noexcept { return bool(pendingReply_); }
    bool pollDeferredSubmit(std::string *err) {
        if (!err || !pendingReply_) return false;
        if (!advanceReply(*pendingReply_,err,false)) return false;
        pendingReply_.reset(); return true;
    }
    bool cancelDeferredSubmit(std::string *err) {
        if (!err || !pendingReply_) return false;
        pendingReply_->deadline=nowMs();
        const bool completed=advanceReply(*pendingReply_,err,false);
        if (completed) pendingReply_.reset();
        return completed;
    }
#endif
    bool submit(const Job &job, const Solution &sol, std::string *err) override {
        return submitImpl(job,sol,err,false);
    }

   private:
    bool submitImpl(const Job &job,const Solution &sol,std::string *err,bool deferred) {
        if (pendingReply_) { if(err) *err="outstanding pool reply must be drained"; return false; }

        pearl::proof_timing::SourceScope timing(getenv("CANARY_PEARL_PROOF_TIMING"), sol.nonce,
                                               sol.pearlProofToken);
        if (diagnosticFinished_ || (diagnosticEnabled_ && diagnosticStream_.failed())) return false;
        if (!err || err == &job.extra || err == &job.jobId ||
            err == &sol.extra) return false;
        if (!sourcePolicyValid()) {
            *err = lastError_;
            return false;
        }
        long long operationDeadline = nowMs() + 15000;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        if (guardRetirement_) {
            if (!guardRetirement_->ready(static_cast<uint64_t>(nowMs()))) {
                *err="guard retirement channel/cap/deadline not ready"; return false;
            }
            if (!guardRetirement_->production())
                operationDeadline=std::min(operationDeadline,static_cast<long long>(guardRetirement_->deadline()));
        }
#endif
        if (sol.extra.empty()) {
            *err = "no proof attached to the solution";
            return false;
        }
        const pearl::MiningConfig config{pearl::kCudaCommonDim, 128, 16, 16};
        PearlPoolCanonicalView view;
        if (!pearlPoolCanonicalViewForJob(job, policy_, config, &view, err, nullptr, offerV2_))
            return false;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        if (guardRetirement_ && view.identity.context.certificateVersion!=PearlCertificateVersion::V3) {
            *err="guard retirement requires certificate3"; return false;
        }
#endif
        const std::string &id = view.identity.context.transportJobId;
        if (id.empty()) {
            *err = "job is missing its pool job id";
            return false;
        }
        // Submission belongs to the exact Job returned by fetch(), not to
        // whichever pushed job happens to be active by the time a reply lands.
        if (!issued_.valid ||
            issued_.connectionGeneration != connectionGeneration_ ||
            !jobsMatch(job, issued_.job)) {
            *err = "job was not issued by this live Pearl pool session";
            return false;
        }
        if (!transport_->isOpen()) {
            *err = "pool session ended before submission; proof not sent";
            return false;
        }

        if (id_ == std::numeric_limits<int>::max()) {
            *err = "Pearl pool request id exhausted; proof not sent";
            return false;
        }
        // The diagnostic bound comes from the same validated policy/config as
        // admission, never from untrusted proof fields or unconditional scaling.
        std::string diagnostic;
        if (!pearlPoolCanonicalTranscriptNotify(job, policy_, config, &diagnostic, err, nullptr, offerV2_))
            return false;
        const std::string event = "\"event\":\"mining.notify\"";
        diagnostic.replace(diagnostic.find(event), event.size(),
                           "\"event\":\"mining.submit\"");
        diagnostic.pop_back();
        uint64_t shape[4] = {};
        const bool decodedShape = pearlTranscriptDecodeProofShape(sol.extra, shape);
        const char *shapeNames[4] = {"m", "n", "k", "rank"};
        for (int i = 0; i < 4; ++i)
            diagnostic += "," + jsonQuoted(shapeNames[i]) + ":" +
                (decodedShape ? std::to_string(shape[i]) : "null");
        diagnostic += ",\"request_id\":" + std::to_string(id_ + 1) +
            ",\"digest_be\":" + jsonQuoted(pearlTargetHexBE(sol.hit)) +
            ",\"digest_within_bound\":" +
            (pearl::U256::fromLimbs(sol.hit).le(view.resolvedTarget) ? "true" : "false") +
            ",\"plain_proof_bytes\":" + std::to_string(sol.extra.size()) +
            ",\"plain_proof_b3\":" + jsonQuoted(pearlTranscriptProofDigest(sol.extra)) + "}";
        // Diagnostic identity only; this does not authorize or acknowledge work.
        if (pearl::proof_timing::enabled(getenv("CANARY_PEARL_PROOF_TIMING"))) {
            const auto proofDigest = pearlTranscriptProofDigest(sol.extra);
            fprintf(stderr,
                    "[pearl-proof-binding] v=1 nonce=%016llx proof_token=%llu request_id=%d "
                    "plain_proof_b3=%s observed_ns=%llu\n",
                    static_cast<unsigned long long>(sol.nonce),
                    static_cast<unsigned long long>(sol.pearlProofToken), id_ + 1,
                    proofDigest.c_str(),
                    static_cast<unsigned long long>(pearl::proof_timing::SystemAdapter{}.now()));
        }
        // v2 is all-or-nothing: a proof that cannot be framed as gzip is refused, never sent plain.
        std::string body;
        if (v2Negotiated_) {
            std::vector<uint8_t> plain;
            if (!pearl::base64Decode(sol.extra, &plain) || plain.empty()) {
                *err = "proof is not base64; v2 submit refused";
                return false;
            }
            const std::string gz = pearl_gzip::gzipCompress(std::string(plain.begin(), plain.end()));
            body = pearlPoolSubmitRequestV2(id_ + 1, id,
                pearl::base64(std::vector<uint8_t>(gz.begin(), gz.end())), v2Worker_);
        } else {
            body = pearlPoolSubmitRequest(id_ + 1, id, sol.extra);
        }
        auto reply=std::make_unique<PendingReply>();
        reply->job=job; reply->deadline=operationDeadline; reply->requestId=id_+1;
        uint64_t operationToken = 0;
        if (issued_.observable && observabilityEnabled_) {
            operationToken = afterMailboxPublication(
                observability_->publishSubmitDispatched(
                    issued_.sessionToken, issued_.jobToken,
                    PearlSourceDelivery::NotSent));
        }

        // This is a local attempt, not evidence that any request was sent.
        // Publish its terminal before any intake failure closes the session:
        // the synchronous owner already has an outstanding submit generation.
        timing.boundary(); // canonical validation, framing and dispatch bookkeeping
        const PreSubmitIntake intake = intakeBeforeSubmit();
        timing.boundary(); // existing bounded zero-wait intake, unchanged
        timing.finish(); // excludes the following send and pool reply wait
        const bool currentJob = jobsMatch(job, activeJob_);
        const std::string proofDigest = pearlTranscriptProofDigest(sol.extra);
        // Only a superseded job is held to the one-send rule; a current job keeps its existing contract.
        const bool proofReused = !currentJob && usedProofs_.count(proofDigest) != 0;
        if (!intake.ready || !haveJob_ || proofReused || (!currentJob && !sameHeightBound(job))) {
            const PearlSourceReason reason = !intake.ready ? intake.reason :
                haveJob_ || havePendingTarget_ ? PearlSourceReason::Stale :
                                               PearlSourceReason::InvalidJob;
            *err = !intake.ready ? intake.error :
                kPearlLocalStaleBeforeSendError;
            publishSubmitTerminal(operationToken, PearlSourceDelivery::NotSent,
                PearlSourceTerminal::LocalFailure, false, false, reason);
            if (intake.close) closeConnection(reason, true);
            return false;
        }
        ++id_;
        // H-019 D1: a same-height replaced job is sent exactly once (live v28r).
        usedProofs_.insert(proofDigest);
        if (!currentJob) sameHeightSpent_.insert(job.jobId);
        writeTranscript(diagnostic);

        pearl::proof_timing::NetworkScope networkTiming(getenv("CANARY_PEARL_PROOF_TIMING"),
                                                        sol.nonce, sol.pearlProofToken);
        const PearlPoolSendEvidence sent = sendLineUntil(body, operationDeadline);
        networkTiming.boundary();
        if (!sent.complete() || !sent.blockingRestored) {
            PearlSourceDelivery delivery = PearlSourceDelivery::NotSent;
            PearlSourceTerminal terminal = PearlSourceTerminal::LocalFailure;
            bool counterFault = false;
            if (sent.acceptedBytes != 0 && sent.acceptedBytes < sent.totalBytes) {
                delivery = PearlSourceDelivery::PossiblySent;
                terminal = PearlSourceTerminal::Unknown;
            } else if (sent.complete()) {
                delivery = PearlSourceDelivery::Sent;
                terminal = PearlSourceTerminal::Unknown;
                const bool submittedCounted = counters_.noteSubmitted();
                const bool terminalCounted =
                    submittedCounted && counters_.noteVerdict(
                                            PearlPoolSubmitVerdict::Unknown,
                                            false);
                counterFault = !submittedCounted || !terminalCounted;
            }
            publishSubmitTerminal(operationToken, delivery, terminal, false,
                                  false, PearlSourceReason::Transport);
            if (counterFault) publishSourceFault(PearlSourceReason::Protocol);
            *err = sent.complete()
                       ? "share sent but socket mode restoration failed; outcome unknown"
                       : sent.acceptedBytes == 0
                             ? "could not send the share"
                             : "only part of the share request reached the socket; outcome unknown";
            closeConnection(PearlSourceReason::Transport, true);
            return sent.complete();
        }
        if (!deferred) networkTiming.completeOnExit(); // Deferred scope records send only, not completed reply timing.
        const bool submittedCounted = counters_.noteSubmitted();

#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        bool controlStarted=true;
        if (guardRetirement_) {
            guard_retirement_v2::Digest fingerprint{};
            controlStarted=hexToBytes(pearlTranscriptProofDigest(sol.extra),fingerprint.data(),fingerprint.size()) &&
                guardRetirement_->begin(static_cast<uint32_t>(id_),view.identity.sourceDigest.bytes,
                                        fingerprint,static_cast<uint64_t>(nowMs()));
        }
#endif

        reply->operation=operationToken; reply->submitted=submittedCounted;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        reply->controlStarted=controlStarted;
#endif
        if (deferred) { pendingReply_=std::move(reply); return true; }
        return advanceReply(*reply,err,true);
    }

    bool advanceReply(PendingReply &state,std::string *err,bool wait) {
        const Job &job=state.job;
        const long long operationDeadline=state.deadline;
        const uint64_t operationToken=state.operation;
        const bool submittedCounted=state.submitted;
        [[maybe_unused]] const bool controlStarted=state.controlStarted;
        auto &lastRead=state.lastRead; auto &replies=state.replies;
        unsigned turnLines=0;
        for (; replies < 12 && nowMs() < operationDeadline;) {
            if (!wait && turnLines++>=16) return false;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
            if (!controlStarted) { lastRead=PearlPoolLineStatus::TransportError; break; }
#endif
            std::string line;
            lastRead = readLineUntil(&line, operationDeadline,!wait);
            if (!wait && lastRead==PearlPoolLineStatus::Timeout && nowMs()<operationDeadline)
                return false;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
            if (lastRead==PearlPoolLineStatus::GuardRetired) {
                const bool counted=submittedCounted && counters_.noteGuardRetired();
                auto f=guardRetirement_->confirmed(); f.kind=3;
                guard_retirement_v2::Bytes raw; std::string encoded;
                if (!(guardRetirement_->production() ? retirement_production::encodeV3(f,&raw) : guard_retirement_v2::encodeV2(f,&raw))) { publishSourceFault(PearlSourceReason::Protocol); return true; }
                static const char digits[]="0123456789abcdef";
                for (auto byte:raw) { encoded+=digits[byte>>4]; encoded+=digits[byte&15]; }
                writeTranscript("{\"event\":\"guard_retired\",\"request_id\":"+std::to_string(state.requestId)+
                    ",\"native_to_guard\":\"full_frame\",\"guard_to_pool_bytes\":0,\"pool_acknowledged\":false,"+
                    jsonQuoted(guardRetirement_->production()?"control_v3_hex":"control_v2_hex")+":"+
                    jsonQuoted(encoded)+"}");
                publishSubmitTerminal(operationToken,PearlSourceDelivery::Sent,PearlSourceTerminal::GuardRetired,
                                      false,false,PearlSourceReason::Stale);
                if (!counted || !diagnosticCompletionHealthy()) publishSourceFault(PearlSourceReason::Protocol);
                return true;
            }
#endif
            if (lastRead != PearlPoolLineStatus::Line) break;
            if (handlePush(line)) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
                if (guardRetirement_ && guardRetirement_->active() && haveJob_ &&
                    !guardRetirement_->observe(activeJobIdentity_.bytes,static_cast<uint64_t>(nowMs()))) {
                    lastRead=PearlPoolLineStatus::TransportError; break;
                }
#endif
                continue;
            }
            ++replies;
            if (!pearlPoolReplyIsFor(line, state.requestId)) continue;
            Job afterReply;
            const bool superseded =
                !currentJob(&afterReply) || !jobsMatch(job, afterReply);
            const PearlPoolSubmitVerdict verdict =
                pearlPoolClassifySubmitReply(line, superseded, err);
            writeTranscript(pearlTranscriptReply(state.requestId, verdict, *err));
            const bool counted = submittedCounted && counters_.noteVerdict(verdict);
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
            if (guardRetirement_) {
                using O=guard_retirement_v2::Ordinary;
                const auto ordinary=verdict==PearlPoolSubmitVerdict::Rejected ? O::Rejected :
                    verdict==PearlPoolSubmitVerdict::Stale ? O::Stale :
                    verdict==PearlPoolSubmitVerdict::Unknown ? O::Unknown : O::Accepted;
                if (!guardRetirement_->ordinary(ordinary,static_cast<uint64_t>(nowMs())))
                    publishSourceFault(PearlSourceReason::Protocol);
            }
#endif
            if (verdict == PearlPoolSubmitVerdict::Rejected) {
                lastSubmitError_ = *err;
                rejectReason_ = *err;
            } else if (verdict == PearlPoolSubmitVerdict::Stale) {
                submitWarning_ = "pool marked a share STALE (stale=" +
                                 std::to_string(counters_.stale) + "): " + *err;
            } else if (verdict == PearlPoolSubmitVerdict::AcceptedSuperseded) {
                submitWarning_ =
                    "pool ACCEPTED a share after replacing its job "
                    "(accepted-superseded=" +
                    std::to_string(counters_.acceptedSuperseded) + ")";
            } else if (verdict == PearlPoolSubmitVerdict::Unknown) {
                submitWarning_ = "pool share outcome UNKNOWN (unknown=" +
                                 std::to_string(counters_.unknown) + "): " + *err;
            }

            PearlSourceTerminal terminal = PearlSourceTerminal::Unknown;
            bool terminalSuperseded = false;
            switch (verdict) {
                case PearlPoolSubmitVerdict::Accepted:
                    terminal = PearlSourceTerminal::Accepted;
                    break;
                case PearlPoolSubmitVerdict::AcceptedSuperseded:
                    terminal = PearlSourceTerminal::Accepted;
                    terminalSuperseded = true;
                    break;
                case PearlPoolSubmitVerdict::Rejected:
                    terminal = PearlSourceTerminal::Rejected;
                    break;
                case PearlPoolSubmitVerdict::Stale:
                    terminal = PearlSourceTerminal::Stale;
                    break;
                case PearlPoolSubmitVerdict::Unknown:
                    terminal = PearlSourceTerminal::Unknown;
                    break;
            }
            publishSubmitTerminal(operationToken, PearlSourceDelivery::Sent,
                                  terminal, true, terminalSuperseded);
            if (!counted) publishSourceFault(PearlSourceReason::Protocol);
            return true;
        }

        *err = "no reply from the pool";
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        if (guardRetirement_ && guardRetirement_->active())
            guardRetirement_->ordinary(guard_retirement_v2::Ordinary::Unknown,static_cast<uint64_t>(nowMs()));
#endif
        const bool counted = submittedCounted &&
                             counters_.noteVerdict(
                                 PearlPoolSubmitVerdict::Unknown, false);
        writeTranscript(pearlTranscriptReply(
            state.requestId, PearlPoolSubmitVerdict::Unknown, *err));
        submitWarning_ = "pool share outcome UNKNOWN (unknown=" +
                         std::to_string(counters_.unknown) + "): " + *err;
        const bool correlationBudgetExhausted = replies >= 12;
        const bool readFailureCloses =
            correlationBudgetExhausted ||
            lastRead == PearlPoolLineStatus::EndOfStream ||
            lastRead == PearlPoolLineStatus::TransportError ||
            lastRead == PearlPoolLineStatus::Oversize;
        const PearlSourceReason readFailureReason =
            correlationBudgetExhausted ||
                    lastRead == PearlPoolLineStatus::Oversize
                ? PearlSourceReason::Protocol
                : readFailureCloses ? PearlSourceReason::Transport
                                    : PearlSourceReason::Timeout;
        publishSubmitTerminal(operationToken, PearlSourceDelivery::Sent,
                              PearlSourceTerminal::Unknown, false, false,
                              readFailureReason);
        if (!counted) publishSourceFault(PearlSourceReason::Protocol);
        if (readFailureCloses)
            closeConnection(readFailureReason, true);
        return true;
    }

   private:
    struct IssuedJobBinding {
        bool valid = false;
        bool observable = false;
        uint64_t connectionGeneration = 0;
        uint64_t sessionToken = 0;
        uint64_t jobToken = 0;
        Job job;
    };

    bool connectAndLogin() {
        if (!sourcePolicyValid()) return false;
        // DNS resolution and TCP connect are synchronous in openSocket() and
        // are not covered by this deadline. Once connected, authorization
        // send plus all reply/push parsing share one hard 25-second window.
        if (!openSocket()) return false;
        const long long authDeadline = nowMs() + 25000;
        // H-025 task 1: a v2 offer logs in as account-or-address + '.' + worker.
        // Non-v2 sessions keep the bare payout address, byte-identical.
        const std::string &authWallet = offerV2_ ? v2Worker_ : wallet_;
        const std::string auth = pearlPoolAuthorizeRequest(authWallet, worker_, "x", "CanaryMiner/" CANARY_MINER_VERSION, soloAuthorization_, offerV2_);
        const PearlPoolSendEvidence authSent = sendLineUntil(auth, authDeadline);
        if (!authSent.complete() || !authSent.blockingRestored) {
            lastError_ = "TCP connection closed while sending mining.authorize";
            closeConnection(PearlSourceReason::Transport, true);
            return false;
        }
        std::string line;
        std::string replyError;
        // Pearl V1 may push the first notify before the authorize ack. Keep
        // that job instead of treating it as a malformed login response.
        bool authorized = false;
        while (nowMs() < authDeadline) {
            const PearlPoolLineStatus read = readLineUntil(&line, authDeadline);
            if (read != PearlPoolLineStatus::Line) {
                if (read == PearlPoolLineStatus::Timeout)
                    lastError_ = "pool sent no correlated mining.authorize reply within 25 seconds";
                else if (read == PearlPoolLineStatus::Oversize)
                    lastError_ = "pool exceeded the 64 KiB inbound message limit during mining.authorize";
                else
                    lastError_ = "pool closed the TCP connection during mining.authorize";
                closeConnection(
                    read == PearlPoolLineStatus::Timeout
                        ? PearlSourceReason::Timeout
                        : read == PearlPoolLineStatus::Oversize
                              ? PearlSourceReason::Protocol
                              : PearlSourceReason::Transport,
                    true);
                return false;
            }
            if (handlePush(line)) continue;
            if (!pearlPoolReplyIsFor(line, 1)) continue;
            if (!pearlPoolReplyAccepted(line, &replyError)) {
                fprintf(stderr, "[pearl-pow] pool refused the login: %s\n", replyError.c_str());
                lastError_ = "pool refused the login: " + replyError;
                closeConnection(PearlSourceReason::Authentication, true);
                return false;
            }
            authorized = true;
            break;
        }
        if (!authorized) {
            lastError_ = "pool sent no correlated mining.authorize reply within 25 seconds";
            closeConnection(PearlSourceReason::Timeout, true);
            return false;
        }
        // Gzip is negotiated only when this session offered it AND the pool answered type v2.
        std::string poolType;
        v2Negotiated_ = offerV2_ && jsonObjectStringStrict(line, "type", &poolType) && poolType == "v2";
        authorized_ = true;
        if (observabilityEnabled_ && sessionToken_ != 0) {
            authorizationToken_ = afterMailboxPublication(
                observability_->publishAuthorizationSucceeded(sessionToken_));
            if (observabilityEnabled_ && retainedJobToken_ != 0) {
                afterMailboxPublication(
                    observability_->publishRetainedJobAvailable(
                        sessionToken_, authorizationToken_, retainedJobToken_));
                if (observabilityEnabled_) {
                    activeJobToken_ = retainedJobToken_;
                    retainedJobToken_ = 0;
                }
            }
        }
        lastError_.clear();
        return true;
    }

    /** Read whatever has arrived, keeping the newest job. */
    void drain(int ms) {
        const long long deadline = nowMs() + ms;
        do {
            // Poll with no timeout when we already have work: take whatever
            // has arrived and get straight back to mining.
            const PearlPoolWaitStatus ready =
                pending_.find('\n') != std::string::npos
                    ? PearlPoolWaitStatus::Ready
                    : transport_->waitReadable(haveJob_ ? 0 : 50);
            if (ready == PearlPoolWaitStatus::Error) {
                lastError_ = "pool socket failed while waiting for work";
                closeConnection(PearlSourceReason::Transport, true);
                return;
            }
            if (ready != PearlPoolWaitStatus::Ready) {
                if (haveJob_) return;
                continue;
            }
            std::string line;
            const long long lineDeadline =
                haveJob_ ? nowMs() + 20000 : deadline;
            const PearlPoolLineStatus read =
                readLineUntil(&line, lineDeadline);
            if (read != PearlPoolLineStatus::Line) {
                if (read == PearlPoolLineStatus::Timeout)
                    lastError_ = "pool timed out in a partial work message";
                else if (read == PearlPoolLineStatus::Oversize)
                    lastError_ = "pool exceeded the 64 KiB inbound work-message limit";
                else
                    lastError_ = "pool closed the TCP connection while waiting for work";
                closeConnection(
                    read == PearlPoolLineStatus::Timeout
                        ? PearlSourceReason::Timeout
                        : read == PearlPoolLineStatus::Oversize
                              ? PearlSourceReason::Protocol
                              : PearlSourceReason::Transport,
                    true);
                return;
            }
            handlePush(line);
        } while (nowMs() < deadline);
    }

    struct PreSubmitIntake {
        bool ready = true;
        bool close = false;
        PearlSourceReason reason = PearlSourceReason::None;
        const char *error = "";
    };

    /** Bounded, zero-wait intake; never closes or releases the issued binding.
     * Complete pushes use the normal canonical path. Any incomplete/ambiguous
     * intake refuses this proof; a quiet observation cannot prevent a later
     * cross-socket replacement race. */
    PreSubmitIntake intakeBeforeSubmit() {
        const auto failure = [](PearlSourceReason reason, const char *error) {
            return PreSubmitIntake{false, true, reason, error};
        };
        const PearlPoolWaitStatus initial = transport_->waitReadable(0);
        if (initial == PearlPoolWaitStatus::Error)
            return failure(PearlSourceReason::Transport,
                           "pre-submit readiness failed; proof not sent");
        if (pending_.empty() && initial == PearlPoolWaitStatus::Timeout)
            return {};
        if (!transport_->setNonblocking(true))
            return failure(PearlSourceReason::Transport,
                           "pre-submit nonblocking setup failed; proof not sent");
        PreSubmitIntake result;
        try {
            // Include bytes already buffered by the prior read in this intake's
            // byte budget, not merely bytes newly received from the socket.
            std::size_t lines = 0, bytes = pending_.size(), calls = 0;
            for (;;) {
                const std::size_t nl = pending_.find('\n');
                if (nl != std::string::npos) {
                    if (lines == 64) {
                        result = failure(PearlSourceReason::Protocol,
                            "pre-submit line budget exhausted; proof not sent");
                        break;
                    }
                    std::string line = pending_.substr(0, nl);
                    pending_.erase(0, nl + 1);
                    ++lines;
                    if (!handlePush(line)) {
                        result = failure(PearlSourceReason::Protocol,
                            "unsolicited reply before submission; proof not sent");
                        break;
                    }
                    continue;
                }
                const PearlPoolWaitStatus ready = transport_->waitReadable(0);
                if (ready == PearlPoolWaitStatus::Error) {
                    result = failure(PearlSourceReason::Transport,
                        "pre-submit readiness failed; proof not sent");
                    break;
                }
                if (ready == PearlPoolWaitStatus::Timeout) {
                    if (!pending_.empty()) result = failure(PearlSourceReason::Protocol,
                        "partial pre-submit message; proof not sent");
                    break;
                }
                if (lines == 64 || bytes == kMaxInboundBufferedBytes || calls == 16 ||
                    pending_.size() >= kMaxInboundBufferedBytes) {
                    result = failure(PearlSourceReason::Protocol,
                        "pre-submit intake budget exhausted; proof not sent");
                    break;
                }
                char buffer[4096];
                const std::size_t maximum = std::min(sizeof(buffer), std::min(
                    kMaxInboundBufferedBytes - bytes,
                    kMaxInboundBufferedBytes - pending_.size()));
                ++calls;
                const PearlPoolTransfer read = transport_->receive(buffer, maximum);
                if (read.status == PearlPoolTransferStatus::WouldBlock) {
                    if (!pending_.empty()) result = failure(PearlSourceReason::Protocol,
                        "partial pre-submit message; proof not sent");
                    break;
                }
                if (read.status != PearlPoolTransferStatus::Bytes ||
                    read.bytes == 0 || read.bytes > maximum) {
                    result = failure(PearlSourceReason::Transport,
                        "pre-submit receive failed or disconnected; proof not sent");
                    break;
                }
                bytes += read.bytes;
                pending_.append(buffer, read.bytes);
            }
        } catch (...) {
            result = failure(PearlSourceReason::Transport,
                             "pre-submit intake failed; proof not sent");
        }
        if (!transport_->setNonblocking(false))
            return failure(PearlSourceReason::Transport,
                           "pre-submit socket restoration failed; proof not sent");
        return result;
    }

    /** Consume every line already queued by the socket without waiting. */
    void drainAvailable() {
        while (transport_->isOpen()) {
            const bool buffered = pending_.find('\n') != std::string::npos;
            const PearlPoolWaitStatus ready = buffered
                                                  ? PearlPoolWaitStatus::Ready
                                                  : transport_->waitReadable(0);
            if (ready == PearlPoolWaitStatus::Timeout) return;
            if (ready == PearlPoolWaitStatus::Error) {
                lastError_ = "pool socket failed while reading queued work";
                closeConnection(PearlSourceReason::Transport, true);
                return;
            }
            std::string line;
            const PearlPoolLineStatus read =
                readLineUntil(&line, nowMs() + 20000);
            if (read != PearlPoolLineStatus::Line) {
                if (read == PearlPoolLineStatus::Timeout)
                    lastError_ = "pool timed out in a queued partial message";
                else if (read == PearlPoolLineStatus::Oversize)
                    lastError_ = "pool exceeded the 64 KiB queued-message limit";
                else
                    lastError_ = "pool closed the TCP connection while reading queued work";
                closeConnection(
                    read == PearlPoolLineStatus::Timeout
                        ? PearlSourceReason::Timeout
                        : read == PearlPoolLineStatus::Oversize
                              ? PearlSourceReason::Protocol
                              : PearlSourceReason::Transport,
                    true);
                return;
            }
            handlePush(line);
        }
    }

    bool currentJob(Job *job) const {
        if (!job || !haveJob_) return false;
        try {
            Job next = activeJob_;
            *job = std::move(next);
            return true;
        } catch (const std::bad_alloc &) {
            return false;
        } catch (const std::length_error &) {
            return false;
        }
    }

    bool jobsMatch(const Job &a, const Job &b) const {
        bool same = false;
        return pearlPoolCanonicalJobsMatch(a, b, policy_, &same) && same;
    }

    bool sourcePolicyValid() {
        if (soloAuthorization_ && (nativeEvidenceFile_ ||
                policy_.network != PearlNetwork::Mainnet || !pearlWalletProblem(wallet_).empty())) {
            lastError_ = "SOLO authorization requires a bare mainnet address without research evidence";
            return false;
        }
        if ((pearlPublicNetwork(policy_.network) ||
             policy_.network == PearlNetwork::Regtest) &&
            pearlKnownTargetSemantics(policy_.targetSemantics)) return true;
        lastError_ = "Pearl pool requires an explicit valid network and target-semantics policy";
        return false;
    }

    /**
     * A line the SERVER started, consumed. False means it is a reply to one of
     * our own requests and the caller still has to look at it.
     *
     * Every read path funnels through here so a pushed share target can never
     * be seen by one of them and missed by another - the login loop, the two
     * drains and the submit reply loop each used to test for `mining.notify`
     * and drop everything else on the floor.
     */
    bool handlePush(const std::string &line) {
        if (pearlPoolMethod(line) == "mining.notify") {
            takeNotify(line);
            return true;
        }
        const PearlPoolTargetPushStatus targetStatus =
            pearlPoolApplyTargetPush(line, pendingTarget_, &havePendingTarget_,
                                     activeJob_.target, &haveJob_);
        if (targetStatus == PearlPoolTargetPushStatus::NotPush)
            return false;
        if (targetStatus == PearlPoolTargetPushStatus::Invalid) {
            retireCurrentJob(PearlSourceReason::InvalidJob);
            clearJob();
            clearSameHeight();
            memset(pendingTarget_, 0, sizeof(pendingTarget_));
            havePendingTarget_ = false;
            lastError_ = "pool sent an invalid target/difficulty push; live work retired";
            fprintf(stderr, "[pearl-pow] %s\n", lastError_.c_str());
            writeTranscript("{\"schema\":\"canary-pearl8-diagnostic-v1\","
                            "\"event\":\"mining.target_invalid\"}");
            publishSourceFault(PearlSourceReason::InvalidJob);
            return true;
        }
        // Standard Stratum applies a difficulty change to the next job, not to
        // an already-issued header. We have no Pearl protocol evidence that a
        // separate push should override every later per-job target either, so
        // hold it for exactly the next valid notify and retire current work.
        retireCurrentJob(PearlSourceReason::Stale);
        clearJob();
        clearSameHeight();
        lastError_ = "pool changed target; waiting for the next mining.notify";
        writeTranscript(pearlTranscriptTarget(pendingTarget_));
        return true;
    }

    void takeNotify(const std::string &line) {
        // A malformed replacement must retire the preceding job. Continuing
        // to mine it is worse than briefly waiting for a valid refresh.
        try {
        Job fresh;
        PearlPoolCanonicalTargetFinalization finalized;
        std::string transcript;
        const pearl::MiningConfig config{pearl::kCudaCommonDim, 128, 16, 16};
        if (!pearlPoolCanonicalJobFromNotify(line, policy_, &fresh, &lastError_) ||
            !pearlPoolFinalizeCanonicalTarget(fresh, pendingTarget_,
                havePendingTarget_, policy_, config, &finalized, &lastError_,
                nullptr, offerV2_) ||
            !pearlPoolCanonicalTranscriptNotify(finalized.job, policy_, config,
                                                &transcript, &lastError_, nullptr, offerV2_)) {
            retireCurrentJob(PearlSourceReason::InvalidJob);
            clearJob();
            clearSameHeight();
            publishSourceFault(PearlSourceReason::InvalidJob);
            writeTranscript("{\"schema\":\"canary-pearl8-diagnostic-v1\",\"event\":\"mining.notify_invalid\"}");
            return;
        }
        // H-025 D6: the GPU publication floors the bound for v2 sessions only.
        finalized.job.compactFloorTarget = offerV2_;
        const bool duplicate = haveJob_ && jobsMatch(activeJob_, finalized.job);
        if (duplicate) {
            if (finalized.consumesPendingTarget) havePendingTarget_ = false;
            lastError_.clear();
            writeTranscript(transcript);
            return;
        }
        // H-019 D2 (live v28f): a reused transport job ID may rebind only to a
        // job that can no longer receive a proof. Any other reuse is ambiguous.
        if (jobIdReusedAmbiguously(finalized.job) ||
            (!jobsById_.count(finalized.job.jobId) && jobsById_.size() >= kMaxSessionJobs)) {
            lastError_ = "ambiguous reused transport job ID; live work retired";
            retireCurrentJob(PearlSourceReason::InvalidJob);
            clearJob();
            clearSameHeight();
            publishSourceFault(PearlSourceReason::InvalidJob);
            writeTranscript("{\"schema\":\"canary-pearl8-diagnostic-v1\",\"event\":\"mining.notify_ambiguous\"}");
            return;
        }
        // H-019 D1 (live v28r): a job replaced at the current height and target
        // stays bindable for one submit. Keep it in the bounded history; the
        // observability retirement is still published, as for any replacement.
        if (haveJob_ && finalized.job.epoch == activeJob_.epoch &&
            memcmp(finalized.job.target, activeJob_.target, sizeof(activeJob_.target)) == 0) {
            sameHeightJobs_.push_back(activeJob_);
            if (sameHeightJobs_.size() > kSameHeightHistory)
                sameHeightJobs_.erase(sameHeightJobs_.begin());
        } else {
            clearSameHeight();
        }
        eraseSameHeight(finalized.job.jobId);
        retireCurrentJob(PearlSourceReason::Stale);
        clearJob();
        jobsById_[finalized.job.jobId] = finalized.job;
        activeJob_ = std::move(finalized.job);
        activeJobIdentity_ = finalized.view.identity.sourceDigest;
        if (finalized.consumesPendingTarget) havePendingTarget_ = false;
        haveJob_ = true;
        if (observabilityEnabled_ && sessionToken_ != 0) {
            if (authorized_) {
                activeJobToken_ = afterMailboxPublication(
                    observability_->publishJobAvailable(
                        sessionToken_, authorizationToken_, activeJobIdentity_));
            } else {
                retainedJobToken_ = afterMailboxPublication(
                    observability_->publishJobRetainedPreAuth(
                        sessionToken_, activeJobIdentity_));
            }
        }
        lastError_.clear();
        writeTranscript(transcript);
        } catch (const std::bad_alloc &) {
            retireCurrentJob(PearlSourceReason::InvalidJob);
            clearJob();
            clearSameHeight();
            publishSourceFault(PearlSourceReason::InvalidJob);
        } catch (const std::length_error &) {
            retireCurrentJob(PearlSourceReason::InvalidJob);
            clearJob();
            clearSameHeight();
            publishSourceFault(PearlSourceReason::InvalidJob);
        }
    }

    template <typename Result>
    Result afterMailboxPublication(Result result, bool notify = true) noexcept {
        // Event publications reach this unlocked seam, including refusals.
        // Non-event bind/release suppress the callback until the run-loop owner
        // has explicitly mirrored that transition into its coordinator.
        if (!result) observabilityEnabled_ = false;
        if (notify && publicationCallback_) publicationCallback_(publicationContext_);
        return result;
    }

    void publishSourceFault(PearlSourceReason reason) {
        if (observabilityEnabled_ && sessionToken_ != 0)
            afterMailboxPublication(
                observability_->publishSourceFault(sessionToken_, reason));
    }

    void publishSubmitTerminal(uint64_t operationToken,
                               PearlSourceDelivery delivery,
                               PearlSourceTerminal terminal,
                               bool acknowledged, bool superseded,
                               PearlSourceReason reason =
                                   PearlSourceReason::None) {
        if (observabilityEnabled_ && operationToken != 0)
            afterMailboxPublication(observability_->publishSubmitTerminal(
                sessionToken_, operationToken, delivery, terminal,
                acknowledged, superseded, reason));
    }

    void retireCurrentJob(PearlSourceReason reason) {
        if (!observabilityEnabled_ || sessionToken_ == 0) return;
        const uint64_t token = authorized_ ? activeJobToken_ : retainedJobToken_;
        if (token != 0)
            afterMailboxPublication(observability_->publishJobRetired(
                sessionToken_, token, reason));
        if (authorized_)
            activeJobToken_ = 0;
        else
            retainedJobToken_ = 0;
    }

    bool bindIssuedJob(const Job &job) {
        IssuedJobBinding next;
        next.job = job;
        next.valid = true;
        next.connectionGeneration = connectionGeneration_;
        next.sessionToken = sessionToken_;
        next.jobToken = activeJobToken_;
        PearlPoolCanonicalIdentity identity;
        const bool exactIdentity =
            pearlPoolCanonicalIdentityForJob(job, policy_, &identity) &&
            identity.sourceDigest.bytes == activeJobIdentity_.bytes;
        if (!exactIdentity) return false;
        issued_ = std::move(next);
        if (observabilityEnabled_ &&
            (!exactIdentity || sessionToken_ == 0 || activeJobToken_ == 0)) {
            afterMailboxPublication(false, false);
        } else if (observabilityEnabled_) {
            issued_.observable = afterMailboxPublication(
                observability_->bindIssuedJob(sessionToken_, activeJobToken_), false);
        }
        return true;
    }

    void releaseIssuedJobBinding() {
        if (!issued_.valid) return;
        if (issued_.observable && observabilityEnabled_)
            afterMailboxPublication(observability_->releaseIssuedJob(
                issued_.sessionToken, issued_.jobToken), false);
        issued_ = IssuedJobBinding();
    }

    void clearJob() {
        haveJob_ = false;
        activeJob_ = Job{};
        activeJobToken_ = 0;
        retainedJobToken_ = 0;
        activeJobIdentity_ = PearlJobIdentityDigest();
    }

    // H-019 D1/D2 (live v28r/v28f). Both tables are scoped to one TCP session.
    // kSameHeightHistory and kMaxSessionJobs match the live guard's bounds in
    // spirit: the history is small, and the job table is refused, not grown,
    // past its limit.
    static constexpr std::size_t kSameHeightHistory = 8;
    static constexpr std::size_t kMaxSessionJobs = std::size_t(1) << 20;
    std::vector<Job> sameHeightJobs_;        // replaced jobs at the current height, oldest first
    std::set<std::string> sameHeightSpent_;  // history jobs already bound for their one submit
    std::map<std::string, Job> jobsById_;    // latest job per transport job ID
    std::set<std::string> usedProofs_;       // proof digests already sent this run (live: reused proof bytes)
    bool offerV2_ = false;                   // authorize offers type v2 (pool.compression: auto)
    bool v2Negotiated_ = false;              // this session's pool answered type v2
    std::string v2Worker_;                   // v2 submit login: account-or-wallet + '.' + worker

    void clearSameHeight() {
        sameHeightJobs_.clear();
        sameHeightSpent_.clear();
    }

    void eraseSameHeight(const std::string &id) {
        sameHeightJobs_.erase(
            std::remove_if(sameHeightJobs_.begin(), sameHeightJobs_.end(),
                           [&id](const Job &kept) { return kept.jobId == id; }),
            sameHeightJobs_.end());
        sameHeightSpent_.erase(id);
    }

    /** v28f: a reused ID may rebind only when its stored job is strictly below
     * both the new and the current height, is not the current job, is not in
     * the same-height history, and no submit is bound to it. */
    bool retiredJobId(const std::string &id, uint64_t height) const {
        const auto old = jobsById_.find(id);
        if (old == jobsById_.end() || !haveJob_) return false;
        if (old->second.epoch >= height || old->second.epoch >= activeJob_.epoch)
            return false;
        if (jobsMatch(old->second, activeJob_)) return false;
        for (const Job &kept : sameHeightJobs_)
            if (kept.jobId == id) return false;
        if (issued_.valid && issued_.job.jobId == id) return false;
        if (pendingReply_ && pendingReply_->job.jobId == id) return false;
        return true;
    }

    bool jobIdReusedAmbiguously(const Job &incoming) const {
        const auto prior = jobsById_.find(incoming.jobId);
        if (prior == jobsById_.end() || jobsMatch(prior->second, incoming))
            return false;
        return !retiredJobId(incoming.jobId, incoming.epoch);
    }

    /** v28r/H-011: a same-height replaced job is bindable once, while it is
     * still in the history, with the same height and target, and unspent. */
    bool sameHeightBound(const Job &job) const {
        if (!haveJob_ || !job.valid || job.jobId.empty() ||
            job.jobId == activeJob_.jobId || sameHeightSpent_.count(job.jobId))
            return false;
        if (job.epoch != activeJob_.epoch ||
            memcmp(job.target, activeJob_.target, sizeof(job.target)) != 0)
            return false;
        for (const Job &kept : sameHeightJobs_)
            if (kept.jobId == job.jobId) return jobsMatch(kept, job);
        return false;
    }

    void writeTranscript(const std::string &line) {
        if (diagnosticEnabled_) {
            if (!diagnosticFinished_ && diagnosticStream_.append(line.data(),line.size()))
                diagnosticStream_.append("\n",1);
            return;
        }
        if (!transcript_) return;
        fprintf(transcript_, "%s\n", line.c_str());
        fflush(transcript_);
    }

    bool openSocket() {
        if (!sourcePolicyValid()) return false;
        if (transport_->isOpen() || sessionToken_ != 0)
            closeConnection(PearlSourceReason::Shutdown, false);
        const std::string port = std::to_string(port_);
        if (observabilityEnabled_) {
            sessionToken_ = afterMailboxPublication(
                observability_->publishSessionStarted());
        }
        const PearlPoolConnectStatus status = transport_->open(host_, port_);
        if (status != PearlPoolConnectStatus::Connected) {
            if (status == PearlPoolConnectStatus::DnsFailure)
                lastError_ = "DNS lookup failed for " + host_ + ":" + port;
            else if (status == PearlPoolConnectStatus::SocketFailure)
                lastError_ = "could not create a TCP socket";
            else
                lastError_ =
                    "TCP connection to " + host_ + ":" + port + " failed";
            closeConnection(PearlSourceReason::Transport, true);
            return false;
        }
        if (connectionGeneration_ != UINT64_MAX) ++connectionGeneration_;
        pending_.clear();
        if (observabilityEnabled_ && sessionToken_ != 0)
            afterMailboxPublication(
                observability_->publishTransportConnected(sessionToken_));
        return true;
    }

    void closeConnection(PearlSourceReason reason, bool faulted) {
        if (!transport_->isOpen() && sessionToken_ == 0 && !haveJob_ &&
            !issued_.valid)
            return;
        if (faulted) publishSourceFault(reason);
        retireCurrentJob(reason);
        releaseIssuedJobBinding();
        if (observabilityEnabled_ && sessionToken_ != 0)
            afterMailboxPublication(
                observability_->publishDisconnected(sessionToken_, reason));
        transport_->close();
        pending_.clear();
        // Jobs are scoped to a TCP session.  Reusing one after reconnect can
        // only produce a stale share, even if the server happens to repeat a
        // header.  So is a negotiated difficulty: the new session starts from
        // whatever the pool decides then, not from what the last one agreed.
        clearJob();
        clearSameHeight();
        jobsById_.clear();
        havePendingTarget_ = false;
        memset(pendingTarget_, 0, sizeof(pendingTarget_));
        authorized_ = false;
        v2Negotiated_ = false;
        sessionToken_ = 0;
        authorizationToken_ = 0;
    }

    /** Send a whole request while preserving zero/partial/full byte evidence. */
    PearlPoolSendEvidence sendLineUntil(const std::string &body,
                                        long long deadlineMs) {
        const std::string line = body + "\n";
        PearlPoolSendEvidence evidence;
        evidence.totalBytes = line.size();
        if (!transport_->setNonblocking(true)) {
            evidence.socketUsable = false;
            evidence.blockingRestored = false;
            return evidence;
        }
        while (evidence.acceptedBytes < line.size()) {
            const long long remaining = deadlineMs - nowMs();
            if (remaining <= 0) break;
            const PearlPoolWaitStatus ready = transport_->waitWritable(
                remaining > 25000 ? 25000 : static_cast<int>(remaining));
            if (ready == PearlPoolWaitStatus::Timeout) break;
            if (ready == PearlPoolWaitStatus::Error) {
                evidence.socketUsable = false;
                break;
            }
            const PearlPoolTransfer transfer = transport_->send(
                line.data() + evidence.acceptedBytes,
                line.size() - evidence.acceptedBytes);
            if (transfer.status == PearlPoolTransferStatus::WouldBlock)
                continue;
            if (transfer.status != PearlPoolTransferStatus::Bytes ||
                transfer.bytes == 0 ||
                transfer.bytes > line.size() - evidence.acceptedBytes) {
                evidence.socketUsable = false;
                break;
            }
            evidence.acceptedBytes += transfer.bytes;
        }
        evidence.blockingRestored = transport_->setNonblocking(false);
        if (!evidence.blockingRestored) evidence.socketUsable = false;
        return evidence;
    }

    /** Read one complete line without allowing partial traffic to restart the
     * caller's overall deadline. Socket receive timeouts apply per recv and can
     * otherwise turn the post-connect 25-second authorization reply window
     * into an unbounded wait under a peer that keeps sending fragments. */
    PearlPoolLineStatus readLineUntil(std::string *out, long long deadlineMs,bool nonBlocking=false) {
        unsigned reads=0;
        if (!out) return PearlPoolLineStatus::TransportError;
        for (;;) {
            const size_t nl = pending_.find('\n');
            if (nl != std::string::npos) {
                *out = pending_.substr(0, nl);
                pending_.erase(0, nl + 1);
                return PearlPoolLineStatus::Line;
            }
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
            if (guardRetirement_ && guardRetirement_->active()) {
                const auto status=guardRetirement_->step(static_cast<uint64_t>(nowMs()));
                if (status==guard_retirement_v2::Step::Confirmed) return PearlPoolLineStatus::GuardRetired;
                if (status==guard_retirement_v2::Step::Failed) return PearlPoolLineStatus::TransportError;
            }
#endif
            const long long remaining = deadlineMs - nowMs();
            if (remaining <= 0) return PearlPoolLineStatus::Timeout;
            if (nonBlocking && reads++>=1) return PearlPoolLineStatus::Timeout;
            int wait=nonBlocking ? 0 : remaining > 25000 ? 25000 : static_cast<int>(remaining);
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
            if (guardRetirement_ && guardRetirement_->active()) wait=std::min(wait,10);
#endif
            const PearlPoolWaitStatus ready = transport_->waitReadable(wait);
            if (ready == PearlPoolWaitStatus::Timeout) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
                if (guardRetirement_ && guardRetirement_->active()) continue;
#endif
                return PearlPoolLineStatus::Timeout;
            }
            if (ready == PearlPoolWaitStatus::Error)
                return PearlPoolLineStatus::TransportError;
            char buf[4096];
            if (pending_.size() >= kMaxInboundBufferedBytes)
                return PearlPoolLineStatus::Oversize;
            const std::size_t remainingCapacity =
                kMaxInboundBufferedBytes - pending_.size();
            const std::size_t receiveMaximum =
                remainingCapacity < sizeof(buf) ? remainingCapacity
                                                : sizeof(buf);
            const PearlPoolTransfer transfer =
                transport_->receive(buf, receiveMaximum);
            if (transfer.status == PearlPoolTransferStatus::WouldBlock)
                continue;
            if (transfer.status == PearlPoolTransferStatus::EndOfStream)
                return PearlPoolLineStatus::EndOfStream;
            if (transfer.status != PearlPoolTransferStatus::Bytes ||
                transfer.bytes == 0 || transfer.bytes > receiveMaximum)
                return PearlPoolLineStatus::TransportError;
            pending_.append(buf, transfer.bytes);
            if (pending_.size() == kMaxInboundBufferedBytes &&
                pending_.find('\n') == std::string::npos)
                return PearlPoolLineStatus::Oversize;
        }
    }

    /** Wall clock, deliberately. clock() measures CPU time, and this thread
     *  spends its wait blocked in select() consuming none of it, so a
     *  clock()-based deadline would never expire. */
    long long nowMs() const {
        const uint64_t value = clock_->nowMs();
        return value > static_cast<uint64_t>(std::numeric_limits<long long>::max())
                   ? std::numeric_limits<long long>::max()
                   : static_cast<long long>(value);
    }

    bool soloAuthorization_ = false;
    std::string host_, wallet_, worker_, desc_, pending_, lastError_,
        transcriptPath_, lastSubmitError_, rejectReason_, submitWarning_;
    const PearlSourcePolicy policy_;
    int port_ = 0;
    int id_ = 100;
    bool haveJob_ = false;
    Job activeJob_;
    uint64_t pendingTarget_[4] = {};
    bool havePendingTarget_ = false;
    FILE *transcript_ = nullptr;
    bool nativeEvidenceFile_ = false;
    PearlBorrowedEvidenceFile diagnosticFile_{&transcript_};
    PearlEvidenceStream diagnosticStream_;
    bool diagnosticEnabled_=false,diagnosticFinished_=false;
    PearlPoolSubmitCounters counters_;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    guard_retirement_v2::Session *guardRetirement_=nullptr;
#endif
    PearlPoolSteadyClock defaultClock_;
    PearlPoolSocketTransport defaultTransport_;
    PearlPoolTransport *transport_ = nullptr;
    PearlSourceMonotonicClock *clock_ = nullptr;
    PearlSourceObservabilityMailbox defaultObservability_;
    PearlSourceObservabilityMailbox *observability_ = nullptr;
    bool observabilityEnabled_ = true;
    void (*publicationCallback_)(void *) noexcept = nullptr;
    void *publicationContext_ = nullptr;
    bool authorized_ = false;
    uint64_t connectionGeneration_ = 0;
    uint64_t sessionToken_ = 0;
    uint64_t authorizationToken_ = 0;
    uint64_t retainedJobToken_ = 0;
    uint64_t activeJobToken_ = 0;
    PearlJobIdentityDigest activeJobIdentity_;
    IssuedJobBinding issued_;
};

}  // namespace om
