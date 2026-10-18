// Offline self-test of one device: no pool, no wallet, no network, no files.
//
//   1. device identity is on the allowlist (and the build matches the architecture);
//   2. hit-free kernel pass: the fixed synthetic benchmark job (zero target) runs one complete attempt through the production
//      kernel path and must return NO solution;
//   3. hit path: a Regtest job with a very easy bound runs complete attempts; each must return a solution, the algorithm's own
//      host re-verification (it recomputes the winning tile on the CPU from A and B and rebuilds the Merkle opening) must accept
//      it, it must carry a non-empty proof, and a solution the algorithm never produced must NOT verify.
// The report names the kernel and tile the device actually ran. The Regtest job never leaves the process: it is not a pool job,
// it is not submittable, and the proofs are discarded.
//
// runSelftest() only talks to the Algorithm interface, so the logic is unit-tested with a fake algorithm on the CPU; the real
// kernels are exercised by `canary-miner selftest` on a GPU.

#pragma once

#include <stdint.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/algo.h"
#include "../core/pearl_job_context.h"
#include "../core/run.h"
#include "device_policy.h"
#include "json_dom.h"

namespace om {

/** Regtest job whose bound accepts roughly one candidate in 65,536. Built from the same pieces as a pool job. */
inline bool buildSelftestHitJob(Job *job, std::string *error) {
    if (!job) return false;
    try {
        PearlJobContext context;
        context.network = PearlNetwork::Regtest;
        context.targetSemantics = PearlTargetSemantics::FinalAdjustedBound;
        context.height = 1000;
        context.certificateVersion = PearlCertificateVersion::V3;
        for (size_t i = 0; i < context.header.size(); ++i) context.header[i] = static_cast<uint8_t>(i * 29u + 5u);
        context.transportJobId = "canaryminer-selftest";
        Job next{};
        next.epoch = context.height;
        next.jobId = context.transportJobId;
        std::string err;
        PearlJobContextError code = PearlJobContextError::None;
        if (!encodePearlJobContext(context, &next.extra, &err, &code)) {
            if (error) *error = "selftest job encoding failed: " + err;
            return false;
        }
        pearl::b3::hash(nullptr, context.header.data(), context.header.size(), next.msg);
        next.target[0] = next.target[1] = next.target[2] = ~0ull;
        next.target[3] = 0x0000FFFFFFFFFFFFull;
        next.valid = true;
        const PearlSourcePolicy policy{context.network, context.targetSemantics};
        PearlJobContext validated;
        if (!validatePearlJobContext(next, &policy, &validated, &err, &code)) {
            if (error) *error = "selftest job rejected by validation: " + err;
            return false;
        }
        *job = next;
        return true;
    } catch (...) {
        if (error) *error = "selftest job allocation failed";
        return false;
    }
}

struct SelftestStep {
    std::string name;
    bool ok = false;
    std::string detail;
};

struct SelftestReport {
    std::vector<SelftestStep> steps;
    std::string kernelId, tileId;
    uint32_t m = 0, n = 0, k = 0;
    uint64_t hitAttempts = 0;
    bool ok() const {
        if (steps.empty()) return false;
        for (const auto &s : steps)
            if (!s.ok) return false;
        return true;
    }
    void add(const std::string &name, bool good, const std::string &detail) { steps.push_back({name, good, detail}); }

    std::string text(const DeviceIdentity &d) const {
        std::string t = "selftest device " + std::to_string(d.index) + " (" + d.name + ", " + d.uuid + ")\n";
        for (const auto &s : steps) t += std::string("  [") + (s.ok ? "PASS" : "FAIL") + "] " + s.name + (s.detail.empty() ? "" : " - " + s.detail) + "\n";
        if (!kernelId.empty())
            t += "  kernel path: kernel=" + kernelId + " tile=" + tileId + " shape=" + std::to_string(m) + "x" + std::to_string(n) + "x" +
                 std::to_string(k) + "\n";
        t += std::string("  result: ") + (ok() ? "PASS" : "FAIL") + "\n";
        return t;
    }

    std::string json(const DeviceIdentity &d) const {
        std::string j = "{\"device\":" + std::to_string(d.index) + ",\"uuid\":" + jsonQuote(d.uuid) + ",\"name\":" + jsonQuote(d.name) +
                        ",\"result\":" + jsonQuote(ok() ? "pass" : "fail") + ",\"kernel\":" + jsonQuote(kernelId) + ",\"tile\":" + jsonQuote(tileId) +
                        ",\"steps\":[";
        for (size_t i = 0; i < steps.size(); ++i)
            j += std::string(i ? "," : "") + "{\"name\":" + jsonQuote(steps[i].name) + ",\"ok\":" + (steps[i].ok ? "true" : "false") + ",\"detail\":" +
                 jsonQuote(steps[i].detail) + "}";
        return j + "]}";
    }
};

using AlgorithmFactory = std::function<std::unique_ptr<Algorithm>()>;

inline double selftestSecondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

inline bool selftestDeadlineExceeded(std::chrono::steady_clock::time_point t0, unsigned deadlineSeconds) {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count() >= deadlineSeconds;
}

/** `hitAttempts` complete attempts on the hit path (2 is enough to prove the path repeats). */
inline SelftestReport runSelftest(const DeviceIdentity &device, int compiledCc, const AlgorithmFactory &make,
                                  unsigned hitAttempts = 2, unsigned deadlineSeconds = 180) {
    SelftestReport r;
    const auto deadline = std::chrono::steady_clock::now();
    const DeviceVerdict verdict = checkDevice(device, compiledCc);
    r.add("device identity and allowlist", verdict.allowed,
          verdict.allowed ? device.name + " [" + verdict.family + "]" : verdict.reason);
    if (!verdict.allowed) return r;

    // ---- 2. hit-free pass on the synthetic benchmark job
    {
        std::unique_ptr<Algorithm> algo = make();
        if (!algo) { r.add("hit-free kernel pass", false, "algorithm could not be created"); return r; }
        RunOptions opt;
        opt.bench = true;
        algo->configure(opt);
        Job job;
        algo->fillBenchJob(&job);
        if (selftestDeadlineExceeded(deadline, deadlineSeconds)) { r.add("hit-free kernel pass", false, "deadline exceeded"); return r; }
        bool good = job.valid && algo->prepare(job);
        std::string detail;
        if (!good) {
            detail = "prepare failed";
        } else {
            const PearlNativeSelection sel = algo->pearlNativeSelection();
            if (sel.valid) {
                r.kernelId = std::string(sel.kernelId);
                r.tileId = std::string(sel.tileId);
                r.m = sel.m; r.n = sel.n; r.k = sel.k;
            }
            std::vector<Solution> sols;
            if (selftestDeadlineExceeded(deadline, deadlineSeconds)) { algo->release(); r.add("hit-free kernel pass", false, "deadline exceeded"); return r; }
            const auto t0 = std::chrono::steady_clock::now();
            good = algo->search(job, 0, algo->workUnitsPerNonce(), &sols);
            const double secs = selftestSecondsSince(t0);
            if (!good) detail = "search failed";
            else if (!sols.empty()) { good = false; detail = std::to_string(sols.size()) + " solution(s) at a zero target"; }
            else detail = "one complete attempt, no solution, " + std::to_string(static_cast<int>(secs * 1000)) + " ms";
        }
        algo->release();
        r.add("hit-free kernel pass (synthetic job)", good, detail);
        if (!good) return r;
    }

    // ---- 3. hit path on the easy Regtest job
    {
        std::unique_ptr<Algorithm> algo = make();
        if (!algo) { r.add("hit path", false, "algorithm could not be created"); return r; }
        RunOptions opt;
        opt.pearlPolicy.network = PearlNetwork::Regtest;
        opt.pearlPolicy.targetSemantics = PearlTargetSemantics::FinalAdjustedBound;
        algo->configure(opt);
        Job job;
        std::string err;
        bool good = buildSelftestHitJob(&job, &err);
        std::string detail = err;
        if (good && !algo->prepare(job)) { good = false; detail = "prepare failed"; }
        if (good && selftestDeadlineExceeded(deadline, deadlineSeconds)) { good = false; detail = "deadline exceeded"; }
        if (good) {
            const PearlNativeSelection sel = algo->pearlNativeSelection();
            if (sel.valid && r.kernelId.empty()) {
                r.kernelId = std::string(sel.kernelId);
                r.tileId = std::string(sel.tileId);
                r.m = sel.m; r.n = sel.n; r.k = sel.k;
            }
            for (unsigned i = 0; good && i < hitAttempts; ++i) {
                if (selftestDeadlineExceeded(deadline, deadlineSeconds)) { good = false; detail = "deadline exceeded"; break; }
                std::vector<Solution> sols;
                const auto t0 = std::chrono::steady_clock::now();
                const bool searched = algo->search(job, 0x1000ull + static_cast<uint64_t>(i) * algo->workUnitsPerNonce(), algo->workUnitsPerNonce(), &sols);
                const double secs = selftestSecondsSince(t0);
                if (!searched) { good = false; detail = "attempt " + std::to_string(i + 1) + ": search failed"; break; }
                if (sols.empty()) { good = false; detail = "attempt " + std::to_string(i + 1) + ": no solution at an easy bound"; break; }
                const Solution &s = sols.front();
                if (!algo->verify(job, s)) { good = false; detail = "attempt " + std::to_string(i + 1) + ": host re-verification rejected the solution"; break; }
                if (s.extra.empty()) { good = false; detail = "attempt " + std::to_string(i + 1) + ": solution carries no proof"; break; }
                Solution bogus = s;
                bogus.nonce += 999999;
                if (algo->verify(job, bogus)) { good = false; detail = "attempt " + std::to_string(i + 1) + ": a solution the algorithm never produced verified"; break; }
                ++r.hitAttempts;
                detail = std::to_string(r.hitAttempts) + " attempt(s): " + std::to_string(sols.size()) + " solution(s), proof " +
                         std::to_string(s.extra.size()) + " bytes, host re-verified, forged nonce refused, last " +
                         std::to_string(static_cast<int>(secs * 1000)) + " ms";
            }
        }
        algo->release();
        r.add("hit path (regtest job: host re-verification and proof)", good, detail);
    }
    return r;
}

}  // namespace om
