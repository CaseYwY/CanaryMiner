// Selftest runner logic with a fake Algorithm (the real kernels run only on a GPU via `canary-miner selftest`),
// plus validation of the Regtest hit job the selftest builds.

#include <memory>

#include "../src/product/product_main.h"
#include "../src/product/selftest.h"
#include "../src/algos/pearl-pow/tma_fused_scan_policy.h"
#include "product_test_util.h"

#ifdef _WIN32
// An empty value deletes the variable in the MSVC CRT.
#define unsetenv(name) _putenv_s((name), "")
#endif

using namespace om;

namespace {

struct Behaviour {
    bool benchPrepareOk = true;
    bool benchSearchOk = true;
    bool benchReturnsSolution = false;  // a zero-target job must never yield one
    bool hitPrepareOk = true;
    bool hitSearchOk = true;
    bool hitReturnsSolution = true;
    bool verifyGenuine = true;
    bool verifyForged = false;  // true would be a broken verifier
    bool emptyProof = false;
    int created = 0;
    int released = 0;
    int searches = 0;
};

class FakeAlgo final : public Algorithm {
   public:
    explicit FakeAlgo(Behaviour *b) : b_(b) { ++b_->created; }
    const char *name() const override { return "pearl-pow"; }
    size_t memoryBytes(const Job &) const override { return 1; }
    void configure(const RunOptions &o) override { bench_ = o.bench; }
    void fillBenchJob(Job *job) const override { buildPearlSyntheticBenchmarkJob(job); }
    bool prepare(const Job &job) override { return job.valid && (bench_ ? b_->benchPrepareOk : b_->hitPrepareOk); }
    uint64_t workUnitsPerNonce() const override { return 4096; }
    PearlNativeSelection pearlNativeSelection() const noexcept override {
        PearlNativeSelection s;
        s.valid = true;
        s.m = 256; s.n = 256; s.k = 2048; s.rank = 128; s.sm = 120; s.preparationPlanId = 1; s.workUnitsPerAttempt = 256;
        s.kernelId = "fake-kernel";
        s.tileId = "fake-tile";
        return s;
    }
    bool search(const Job &, uint64_t, uint64_t count, std::vector<Solution> *out) override {
        ++b_->searches;
        if (count != workUnitsPerNonce()) return false;
        if (bench_) {
            if (!b_->benchSearchOk) return false;
            if (b_->benchReturnsSolution) out->push_back(makeSolution(1));
            return true;
        }
        if (!b_->hitSearchOk) return false;
        if (b_->hitReturnsSolution) out->push_back(makeSolution(0x1000 + static_cast<uint64_t>(b_->searches)));
        return true;
    }
    bool verify(const Job &, const Solution &s) const override { return s.nonce >= 0x1000 && s.nonce < 0x1000 + 100 ? b_->verifyGenuine : b_->verifyForged; }
    void release() override { ++b_->released; }

   private:
    Behaviour *b_;
    bool bench_ = false;
    Solution makeSolution(uint64_t nonce) const {
        Solution s;
        s.nonce = nonce;
        s.extra = b_->emptyProof ? std::string() : std::string(1234, 'p');
        return s;
    }
};

DeviceIdentity goodDevice() {
    DeviceIdentity d;
    d.index = 0;
    d.name = "NVIDIA GeForce RTX 5080";
    d.uuid = "GPU-aaaaaaaa-0000-0000-0000-000000000001";
    d.pciBus = "0000:01:00.0";
    d.ccMajor = 12;
    d.ccMinor = 0;
    return d;
}

bool has(const std::string &h, const char *n) { return h.find(n) != std::string::npos; }

SelftestReport run(Behaviour *b, const DeviceIdentity &d, int cc = 120, unsigned attempts = 2) {
    return runSelftest(d, cc, [b] { return std::unique_ptr<Algorithm>(new FakeAlgo(b)); }, attempts);
}

}  // namespace

int main() {
    // fixed selection of the live miner per architecture; none for a host-only build
    {
        std::string shape, tile;
        CHECK(fixedSelectionFor(120, &shape, &tile) && shape == "16384x131072" && tile == "n64 k64 4x4/2x4");
        CHECK(!fixedSelectionFor(89, &shape, &tile) && fixedSelectionFor(120, &shape, &tile) && shape == "16384x131072");
        CHECK(!fixedSelectionFor(0, &shape, &tile));
        CHECK(std::string(productTmaDataflowFor(120)) == "combined-v1");
        CHECK(productTmaDataflowFor(89) == nullptr && productTmaDataflowFor(0) == nullptr);
        CHECK(pearl::tma_fused::productModeAllowed(pearl::tma_fused::Mode::Combined,true,true,true));
        CHECK(!pearl::tma_fused::productModeAllowed(pearl::tma_fused::Mode::Ordinary,true,true,true));
        CHECK(!pearl::tma_fused::productModeAllowed(pearl::tma_fused::Mode::EarlyFused,true,true,true));
        CHECK(!pearl::tma_fused::productModeAllowed(pearl::tma_fused::Mode::Combined,false,true,true));
        CHECK(!pearl::tma_fused::productModeAllowed(pearl::tma_fused::Mode::Combined,true,false,true));
        CHECK(!pearl::tma_fused::productModeAllowed(pearl::tma_fused::Mode::Combined,true,true,false));
        // the admitted tile is the SM120 build tile
        const char *ownTile = "n64 k64 4x4/2x4", *otherTile = "ptx k64 4x4/2x4";
        CHECK(pearlProductFixedSelectionMatches("16384x131072", ownTile));
        CHECK(!pearlProductFixedSelectionMatches("4096x65536", ownTile));
        CHECK(!pearlProductFixedSelectionMatches("16384x131072", otherTile));
        unsetenv("CANARY_PEARL_SHAPE");
        unsetenv("CANARY_PEARL_TILE");
        unsetenv("CANARY_PEARL_TMA_DATAFLOW");
        unsetenv("CANARY_PEARL_PRODUCT_FIXED_SELECTION");
        applyProductFixedSelection();
        CHECK(std::string(getenv("CANARY_PEARL_SHAPE")) == "16384x131072");
        CHECK(std::string(getenv("CANARY_PEARL_TILE")) == ownTile);
        CHECK(std::string(getenv("CANARY_PEARL_TMA_DATAFLOW")) == "combined-v1");
        CHECK(std::string(getenv("CANARY_PEARL_PRODUCT_FIXED_SELECTION")) == "1");
    }
    // ---- the Regtest hit job is a valid canonical job and is not a mainnet job
    {
        Job job;
        std::string err;
        CHECK(buildSelftestHitJob(&job, &err) && job.valid && err.empty());
        PearlJobContext ctx;
        std::string e;
        const PearlSourcePolicy regtest{PearlNetwork::Regtest, PearlTargetSemantics::FinalAdjustedBound};
        CHECK(validatePearlJobContext(job, &regtest, &ctx, &e));
        CHECK(ctx.network == PearlNetwork::Regtest && ctx.certificateVersion == PearlCertificateVersion::V3);
        const PearlSourcePolicy mainnet{PearlNetwork::Mainnet, PearlTargetSemantics::BaseBeforePenalty};
        PearlJobContext other;
        CHECK(!validatePearlJobContext(job, &mainnet, &other, &e));  // can never be mistaken for pool work
        CHECK(job.target[0] == ~0ull && job.target[3] == 0x0000FFFFFFFFFFFFull);
        CHECK(!buildSelftestHitJob(nullptr, &err));
    }

    // ---- all good
    {
        Behaviour b;
        const SelftestReport r = run(&b, goodDevice());
        CHECK(r.ok() && r.steps.size() == 3 && r.hitAttempts == 2);
        CHECK(r.kernelId == "fake-kernel" && r.tileId == "fake-tile" && r.m == 256 && r.k == 2048);
        CHECK(b.created == 2 && b.released == 2);  // both algorithm instances were released
        const std::string text = r.text(goodDevice());
        CHECK(has(text, "[PASS] device identity") && has(text, "[PASS] hit-free") && has(text, "[PASS] hit path") && has(text, "kernel=fake-kernel") &&
              has(text, "result: PASS"));
        JsonValue v;
        std::string e;
        CHECK(parseJsonStrict(r.json(goodDevice()), &v, &e) && v.find("result")->text == "pass" && v.find("steps")->items.size() == 3 &&
              v.find("kernel")->text == "fake-kernel");
    }
    // ---- unsupported device: refused before any algorithm exists
    {
        Behaviour b;
        DeviceIdentity d = goodDevice();
        d.name = "NVIDIA GeForce RTX 5090";
        const SelftestReport r = run(&b, d);
        CHECK(!r.ok() && r.steps.size() == 1 && b.created == 0 && has(r.steps[0].detail.c_str(), "supports NVIDIA Blackwell (RTX 50-series, SM120) only"));
        CHECK(has(r.text(d), "result: FAIL"));
    }
    // ---- wrong build for the device
    {
        Behaviour b;
        const SelftestReport r = run(&b, goodDevice(), 89);
        CHECK(!r.ok() && b.created == 0 && has(r.steps[0].detail.c_str(), "SM89 kernels"));
    }
    // ---- failure injection: each broken behaviour fails the right step and stops there
    {
        Behaviour b;
        b.benchPrepareOk = false;
        SelftestReport r = run(&b, goodDevice());
        CHECK(!r.ok() && r.steps.size() == 2 && !r.steps[1].ok && has(r.steps[1].detail.c_str(), "prepare failed") && b.created == 1 && b.released == 1);
    }
    {
        Behaviour b;
        b.benchSearchOk = false;
        SelftestReport r = run(&b, goodDevice());
        CHECK(!r.ok() && has(r.steps[1].detail.c_str(), "search failed") && b.released == 1);
    }
    {
        Behaviour b;
        b.benchReturnsSolution = true;
        SelftestReport r = run(&b, goodDevice());
        CHECK(!r.ok() && has(r.steps[1].detail.c_str(), "at a zero target"));
    }
    {
        Behaviour b;
        b.hitPrepareOk = false;
        SelftestReport r = run(&b, goodDevice());
        CHECK(!r.ok() && r.steps.size() == 3 && has(r.steps[2].detail.c_str(), "prepare failed") && b.released == 2);
    }
    {
        Behaviour b;
        b.hitSearchOk = false;
        SelftestReport r = run(&b, goodDevice());
        CHECK(!r.ok() && has(r.steps[2].detail.c_str(), "attempt 1: search failed"));
    }
    {
        Behaviour b;
        b.hitReturnsSolution = false;
        SelftestReport r = run(&b, goodDevice());
        CHECK(!r.ok() && has(r.steps[2].detail.c_str(), "no solution at an easy bound"));
    }
    {
        Behaviour b;
        b.verifyGenuine = false;
        SelftestReport r = run(&b, goodDevice());
        CHECK(!r.ok() && has(r.steps[2].detail.c_str(), "host re-verification rejected"));
    }
    {
        Behaviour b;
        b.verifyForged = true;  // a verifier that accepts anything must be caught
        SelftestReport r = run(&b, goodDevice());
        CHECK(!r.ok() && has(r.steps[2].detail.c_str(), "never produced verified"));
    }
    {
        Behaviour b;
        b.emptyProof = true;
        SelftestReport r = run(&b, goodDevice());
        CHECK(!r.ok() && has(r.steps[2].detail.c_str(), "carries no proof"));
    }
    // a factory that cannot create the algorithm
    {
        const SelftestReport r = runSelftest(goodDevice(), 120, [] { return std::unique_ptr<Algorithm>(); });
        CHECK(!r.ok() && has(r.steps.back().detail.c_str(), "could not be created"));
    }
    // an empty report is never a pass
    CHECK(!SelftestReport{}.ok());
    // A zero-second deadline refuses preparation rather than entering a long-running operation.
    {
        Behaviour b;
        const SelftestReport r = runSelftest(goodDevice(), 120, [&b] { return std::unique_ptr<Algorithm>(new FakeAlgo(&b)); }, 2, 0);
        CHECK(!r.ok() && r.steps.size() == 2 && has(r.steps[1].detail.c_str(), "deadline exceeded") && b.created == 1 && b.released == 0);
    }
    return finishTests("test_product_selftest");
}
