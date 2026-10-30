// Developer fee scheduler and its persisted state: the clock-injected policy plus durable state. fee_session.h drives it from the
// outer run loop (miner.cu): a window ends the user's runMiner at a loop top, a fee runMiner mines it, then the user run resumes.
//
// Contract (PRODUCT-CONTRACT section 6):
//   * a cycle is 100 minutes of PRODUCTIVE mining time: 99 minutes for the user and a 1-minute fee window (99:1);
//   * only time with a job present and the devices mining counts - stopped, connecting and idle time never accrues;
//   * the window opens at a random offset inside each cycle (a fresh offset per cycle);
//   * when the window cannot run (fee pool unreachable, the miner stopped mid-window) the unmet fee time is debt; debt is
//     capped at ONE window, so a cycle's window is never longer than two minutes;
//   * a fee-pool failure defers the window, it never blocks or slows the user's mining;
//   * the state (cycle, time used, offset, debt) is persisted with a checksum, written atomically and fsynced.
// The destination is a compile-time constant (CANARY_FEE_DESTINATION). It is public by owner decision and is set below; a
// build may override it with -DCANARY_FEE_DESTINATION. Documents quote its SHA-256 as well. A build without a destination
// never opens a window.

#pragma once

#include <stdint.h>
#include <stdio.h>

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "hash.h"
#include "json_dom.h"

namespace om {

#ifndef CANARY_FEE_DESTINATION
#define CANARY_FEE_DESTINATION "prl1p9cspyppry9vh38rs78k8s5n44qy9k9jn25pl7wwsvj4emegu9shssjs2jc"
#endif

// Donation traffic has its own pool identity and never inherits the user's pool configuration.
#ifndef CANARY_FEE_POOL_HOST
#define CANARY_FEE_POOL_HOST "pearl.f2pool.com"
#endif
#ifndef CANARY_FEE_POOL_PORT
#define CANARY_FEE_POOL_PORT 5500
#endif
#ifndef CANARY_FEE_POOL_PRESET
#define CANARY_FEE_POOL_PRESET "f2pool"
#endif
#ifndef CANARY_FEE_POOL_ACCOUNT
#define CANARY_FEE_POOL_ACCOUNT "bucnazty90"
#endif
#ifndef CANARY_FEE_POOL_WORKER
#define CANARY_FEE_POOL_WORKER "canaryfee"
#endif

inline std::string feePoolEndpoint() { return std::string(CANARY_FEE_POOL_HOST) + ":" + std::to_string(CANARY_FEE_POOL_PORT); }
inline std::string feePoolLogin() { return std::string(CANARY_FEE_POOL_ACCOUNT) + "." + CANARY_FEE_POOL_WORKER; }

inline const char *feePolicyBuildLabel() {
#ifdef CANARY_FEE_TEST_POLICY
    return "test";
#else
    return "release";
#endif
}

inline std::string feeDestination() { return CANARY_FEE_DESTINATION; }
inline bool feeDestinationConfigured() { return !feeDestination().empty(); }
/** SHA-256 of the compiled destination (empty string when the build has none) - what documents may quote. */
inline std::string feeDestinationDigest() { return feeDestinationConfigured() ? sha256Hex(feeDestination()) : std::string(); }

struct FeePolicy {
    uint64_t cycleMs = 100ull * 60 * 1000;  // 100 productive minutes
    uint64_t windowMs = 60ull * 1000;       // 1 minute of fee work per cycle
    uint64_t maxStepMs = 10ull * 1000;      // longest interval that can be credited in one advance() (suspend/stall guard)
    uint64_t retryBackoffMs = 30ull * 1000;  // after a fee-pool failure, wait this long before trying the window again
};

struct FeeState {
    uint32_t version = 1;
    bool initialized = false;
    uint64_t cycle = 0;
    uint64_t userMs = 0;    // productive user time used in this cycle
    uint64_t feeMs = 0;     // productive fee time used in this cycle
    uint64_t owedMs = 0;    // length of this cycle's fee window (one window plus carried debt)
    uint64_t offsetMs = 0;  // user time at which the window opens
    uint64_t debtMs = 0;    // the carried part of owedMs (reporting)
    bool operator==(const FeeState &o) const {
        return version == o.version && initialized == o.initialized && cycle == o.cycle && userMs == o.userMs && feeMs == o.feeMs &&
               owedMs == o.owedMs && offsetMs == o.offsetMs && debtMs == o.debtMs;
    }
};

class FeeRandom {
   public:
    virtual ~FeeRandom() = default;
    virtual uint64_t below(uint64_t n) = 0;  // uniform in [0, n); n >= 1
};

class SystemFeeRandom final : public FeeRandom {
   public:
    SystemFeeRandom() { std::random_device rd; rng_.seed((static_cast<uint64_t>(rd()) << 32) ^ rd()); }
    uint64_t below(uint64_t n) override { return n <= 1 ? 0 : std::uniform_int_distribution<uint64_t>(0, n - 1)(rng_); }

   private:
    std::mt19937_64 rng_;
};

enum class FeeMode { User, Fee };

class FeeScheduler {
   public:
    FeeScheduler(const FeePolicy &policy, FeeRandom *rng) : policy_(policy), rng_(rng) {}

    void restore(const FeeState &s) {
        st_ = s;
        mode_ = FeeMode::User;  // a restart always resumes in user mode; an unfinished window reopens when due
        haveLast_ = false;
        if (!st_.initialized || !sane(st_)) { st_ = FeeState{}; startCycle(0); }
    }

    const FeeState &state() const { return st_; }
    FeeMode mode() const { return mode_; }
    bool productive() const { return haveLast_ && lastProductive_; } // accounting state for the interval following the last call
    /** Remaining fee time owed in the current cycle (0 when the window is complete or not due). */
    uint64_t windowRemainingMs() const { return st_.owedMs > st_.feeMs ? st_.owedMs - st_.feeMs : 0; }
    bool changedSinceSave() const { return dirty_; }
    void markSaved() { dirty_ = false; }

    /** The fee pool refused, timed out or failed to authorize: stop the window now and retry later. Never blocks the user. */
    void noteFeePoolFailure(uint64_t nowMs) {
        retryAfterMs_ = nowMs + policy_.retryBackoffMs;
        haveRetry_ = true;
        if (mode_ == FeeMode::Fee) { mode_ = FeeMode::User; dirty_ = true; }
    }

    /**
     * Advance to `nowMs` (monotonic milliseconds). `productive`: a valid job is present and the devices are mining.
     * `feePoolUsable`: false while the fee destination is unavailable (not configured, no route, authorization failed).
     * Returns the mode to mine in from now on. The interval since the previous call is credited to the mode that call returned.
     * `holdFeeWhenIdle`: while in a fee window, a non-productive call (the fee run is connecting or between jobs) keeps the
     * window open instead of ending it; nothing is credited either way. The caller bounds that wait itself.
     */
    FeeMode advance(uint64_t nowMs, bool productive, bool feePoolUsable, bool holdFeeWhenIdle = false) {
        if (!st_.initialized) { st_ = FeeState{}; startCycle(0); }
        if (haveLast_) {
            const uint64_t raw = nowMs > lastMs_ ? nowMs - lastMs_ : 0;
            const uint64_t dt = std::min(raw, policy_.maxStepMs);
            if (lastProductive_ && dt) {
                if (mode_ == FeeMode::Fee) st_.feeMs += dt; else st_.userMs += dt;
                dirty_ = true;
            }
        }
        haveLast_ = true;
        lastMs_ = nowMs;
        lastProductive_ = productive;

        const bool idleOk = productive || (holdFeeWhenIdle && mode_ == FeeMode::Fee);
        const bool usable = feePoolUsable && idleOk && !(haveRetry_ && nowMs < retryAfterMs_);
        if (mode_ == FeeMode::Fee && (!usable || st_.feeMs >= st_.owedMs)) { mode_ = FeeMode::User; dirty_ = true; }

        for (int guard = 0; guard < 4; ++guard) {
            const uint64_t userBudget = policy_.cycleMs > st_.owedMs ? policy_.cycleMs - st_.owedMs : 0;
            const bool windowDone = st_.feeMs >= st_.owedMs;
            if (mode_ == FeeMode::User && !windowDone && usable && st_.userMs >= st_.offsetMs) { mode_ = FeeMode::Fee; dirty_ = true; break; }
            if (mode_ == FeeMode::User && st_.userMs >= userBudget && (windowDone || !usable)) {
                const uint64_t unmet = windowDone ? 0 : st_.owedMs - st_.feeMs;
                startCycle(std::min(unmet, policy_.windowMs));
                dirty_ = true;
                continue;
            }
            break;
        }
        return mode_;
    }

   private:
    FeePolicy policy_;
    FeeRandom *rng_;
    FeeState st_;
    FeeMode mode_ = FeeMode::User;
    bool haveLast_ = false, lastProductive_ = false, dirty_ = false, haveRetry_ = false;
    uint64_t lastMs_ = 0, retryAfterMs_ = 0;

    bool sane(const FeeState &s) const {
        return s.version == 1 && s.owedMs >= policy_.windowMs && s.owedMs <= 2 * policy_.windowMs && s.debtMs <= policy_.windowMs &&
               s.owedMs == policy_.windowMs + s.debtMs && s.feeMs <= s.owedMs && s.userMs <= policy_.cycleMs &&
               s.offsetMs <= policy_.cycleMs - s.owedMs;
    }

    void startCycle(uint64_t debtMs) {
        const bool first = !st_.initialized;
        st_.initialized = true;
        if (!first) ++st_.cycle;
        st_.userMs = 0;
        st_.feeMs = 0;
        st_.debtMs = std::min(debtMs, policy_.windowMs);
        st_.owedMs = policy_.windowMs + st_.debtMs;
        const uint64_t budget = policy_.cycleMs - st_.owedMs;
        st_.offsetMs = rng_ ? rng_->below(budget + 1) : 0;
    }
};

// ------------------------------------------------------------------ persistence

/** Canonical text of a FeeState with its CRC-32 ("crc32" member last). */
inline std::string serializeFeeState(const FeeState &s) {
    std::string body = "{\"version\":" + std::to_string(s.version) + ",\"initialized\":" + (s.initialized ? "true" : "false") +
                       ",\"cycle\":" + std::to_string(s.cycle) + ",\"user_ms\":" + std::to_string(s.userMs) +
                       ",\"fee_ms\":" + std::to_string(s.feeMs) + ",\"owed_ms\":" + std::to_string(s.owedMs) +
                       ",\"offset_ms\":" + std::to_string(s.offsetMs) + ",\"debt_ms\":" + std::to_string(s.debtMs);
    char crc[16];
    snprintf(crc, sizeof(crc), "%08x", crc32(body.data(), body.size()));
    return body + ",\"crc32\":\"" + crc + "\"}\n";
}

/** Parse and verify. False for any damage: bad JSON, missing/unknown members, wrong checksum, wrong version. */
inline bool parseFeeState(const std::string &text, FeeState *out) {
    JsonValue v;
    std::string err;
    if (!parseJsonStrict(text, &v, &err) || !v.isObject() || v.members.size() != 9) return false;
    const JsonValue *crc = v.find("crc32");
    if (!crc || !crc->isString() || crc->text.size() != 8) return false;
    const size_t at = text.rfind(",\"crc32\":");
    if (at == std::string::npos) return false;
    char want[16];
    snprintf(want, sizeof(want), "%08x", crc32(text.data(), at));
    if (crc->text != want) return false;
    FeeState s;
    uint64_t ver = 0;
    const char *names[] = {"version", "cycle", "user_ms", "fee_ms", "owed_ms", "offset_ms", "debt_ms"};
    uint64_t vals[7];
    for (int i = 0; i < 7; ++i) {
        const JsonValue *m = v.find(names[i]);
        if (!m || !m->asUint64(&vals[i])) return false;
    }
    ver = vals[0];
    const JsonValue *init = v.find("initialized");
    if (ver != 1 || !init || !init->isBool()) return false;
    s.version = static_cast<uint32_t>(ver);
    s.initialized = init->boolean;
    s.cycle = vals[1]; s.userMs = vals[2]; s.feeMs = vals[3]; s.owedMs = vals[4]; s.offsetMs = vals[5]; s.debtMs = vals[6];
    *out = s;
    return true;
}

/** donation-state.json plus donation-state.json.bak; save = write temp, fsync, keep the old copy as .bak, rename into place. */
class FeeStateFile {
   public:
    explicit FeeStateFile(const std::string &dir) : dir_(dir) {}
    std::string path() const { return (std::filesystem::path(dir_) / "donation-state.json").string(); }
    std::string backupPath() const { return path() + ".bak"; }

    enum class LoadResult { Fresh, Loaded, LoadedBackup, Corrupt };

    /** Fresh = no file (first run). Corrupt = files exist but none verifies (caller starts clean; the user is never blocked). */
    LoadResult load(FeeState *out) const {
        bool anyFile = false;
        const std::string candidates[] = {path(), backupPath()};
        for (int i = 0; i < 2; ++i) {
            std::string text;
            if (!readAll(candidates[i], &text)) continue;
            anyFile = true;
            FeeState s;
            if (parseFeeState(text, &s)) { *out = s; return i == 0 ? LoadResult::Loaded : LoadResult::LoadedBackup; }
        }
        *out = FeeState{};
        return anyFile ? LoadResult::Corrupt : LoadResult::Fresh;
    }

    bool save(const FeeState &s, std::string *error) const {
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        const std::string tmp = path() + ".tmp";
        const std::string text = serializeFeeState(s);
        FILE *f = fopen(tmp.c_str(), "wb");
        if (!f) { if (error) *error = "cannot write " + tmp; return false; }
        bool ok = fwrite(text.data(), 1, text.size(), f) == text.size() && fflush(f) == 0;
#ifdef _WIN32
        ok = ok && _commit(_fileno(f)) == 0;
#else
        ok = ok && fsync(fileno(f)) == 0;
#endif
        ok = (fclose(f) == 0) && ok;
        if (!ok) { std::filesystem::remove(tmp, ec); if (error) *error = "cannot flush " + tmp; return false; }
        std::filesystem::remove(backupPath(), ec);
        if (std::filesystem::exists(path(), ec)) std::filesystem::rename(path(), backupPath(), ec);
        std::filesystem::rename(tmp, path(), ec);
        if (ec) { if (error) *error = "cannot replace " + path() + ": " + ec.message(); return false; }
        return true;
    }

   private:
    std::string dir_;
    static bool readAll(const std::string &p, std::string *out) {
        FILE *f = fopen(p.c_str(), "rb");
        if (!f) return false;
        char buf[4096];
        size_t n;
        out->clear();
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && out->size() < (1u << 16)) out->append(buf, n);
        fclose(f);
        return true;
    }
};

}  // namespace om
