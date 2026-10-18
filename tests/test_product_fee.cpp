// Fee scheduler (injected clock and RNG) and its persisted state.
// Windows, ratio, restart mid-window, outage mid-window, debt cap, no accrual while stopped, corrupt state, atomic persistence.

#include <fstream>

#include "../src/product/fee.h"
#include "product_test_util.h"

using namespace om;

namespace {

constexpr uint64_t kSec = 1000;
constexpr uint64_t kMin = 60 * kSec;

class FixedRandom final : public FeeRandom {
   public:
    explicit FixedRandom(uint64_t v) : v_(v) {}
    uint64_t below(uint64_t n) override { return std::min(v_, n ? n - 1 : 0); }
    void set(uint64_t v) { v_ = v; }

   private:
    uint64_t v_;
};

}  // namespace

int main() {
    FeePolicy pol;
    CHECK(pol.cycleMs == 100 * kMin && pol.windowMs == kMin);

    // ---- 99:1 over several cycles, fresh random offset per cycle
    {
        FixedRandom rng(10 * kMin);
        FeeScheduler s(pol, &rng);
        uint64_t feeSeen = 0, userSeen = 0;
        FeeMode prev = FeeMode::User;
        uint64_t windows = 0;
        std::vector<uint64_t> windowStarts;
        for (uint64_t t = 0; t < 3 * 6000 + 5; ++t) {
            const FeeMode m = s.advance(t * kSec, true, true);
            (prev == FeeMode::Fee ? feeSeen : userSeen) += (t > 0);
            if (m == FeeMode::Fee && prev == FeeMode::User) { ++windows; windowStarts.push_back(s.state().userMs); }
            prev = m;
            rng.set((t * 7919) % (90 * kMin));  // a different offset for each cycle
        }
        CHECK(windows == 3);
        CHECK(windowStarts.size() == 3 && windowStarts[0] != windowStarts[1] && windowStarts[1] != windowStarts[2]);  // a fresh offset per cycle
        // one fee minute per 100 productive minutes (within one tick per cycle)
        CHECK(feeSeen >= 3 * 60 - 3 && feeSeen <= 3 * 60 + 3);
        CHECK(userSeen + feeSeen >= 3 * 6000);
        const double share = static_cast<double>(feeSeen) / static_cast<double>(feeSeen + userSeen);
        CHECK(share > 0.0095 && share < 0.0105);
        CHECK(s.state().cycle >= 3);
    }

    // ---- the window opens exactly at its random offset, lasts exactly one minute, then user mining resumes
    {
        FixedRandom rng(30 * kMin);
        FeeScheduler s(pol, &rng);
        FeeMode m = FeeMode::User;
        uint64_t openedAt = 0, closedAt = 0;
        for (uint64_t t = 0; t < 40 * 60; ++t) {
            const FeeMode next = s.advance(t * kSec, true, true);
            if (next == FeeMode::Fee && m == FeeMode::User) openedAt = t;
            if (next == FeeMode::User && m == FeeMode::Fee) closedAt = t;
            m = next;
        }
        CHECK(openedAt >= 30 * 60 && openedAt <= 30 * 60 + 2);
        CHECK(closedAt - openedAt >= 59 && closedAt - openedAt <= 61);
        CHECK(s.state().feeMs >= kMin && s.state().debtMs == 0);
    }

    // ---- offset is bounded by the user budget: a huge random value still leaves room for a full window in the cycle
    {
        FixedRandom rng(~0ull);
        FeeScheduler s(pol, &rng);
        s.advance(0, true, true);
        CHECK(s.state().offsetMs == pol.cycleMs - pol.windowMs);
    }

    // ---- not productive: no accrual at all (stopped, connecting, no job)
    {
        FixedRandom rng(0);
        FeeScheduler s(pol, &rng);
        for (uint64_t t = 0; t < 5000; ++t) s.advance(t * kSec, false, true);
        CHECK(s.state().userMs == 0 && s.state().feeMs == 0 && s.mode() == FeeMode::User);
        // productive for 5 s only
        for (uint64_t t = 5000; t < 5006; ++t) s.advance(t * kSec, true, true);
        CHECK(s.state().userMs + s.state().feeMs <= 6 * kSec && s.state().userMs + s.state().feeMs >= 4 * kSec);
        const uint64_t before = s.state().userMs;
        for (uint64_t t = 5006; t < 9000; ++t) s.advance(t * kSec, false, true);
        CHECK(s.state().userMs - before <= kSec);  // the last productive interval ends at the stop; nothing after
    }

    // ---- a stall/suspend gap is capped, not credited
    {
        FixedRandom rng(50 * kMin);
        FeeScheduler s(pol, &rng);
        s.advance(0, true, true);
        s.advance(1 * kSec, true, true);
        s.advance(3600 * kSec, true, true);  // one hour later
        CHECK(s.state().userMs <= kSec + pol.maxStepMs);
    }

    // ---- restart in the middle of a window: the remaining time is still owed, and nothing is double counted
    {
        FixedRandom rng(1 * kMin);
        FeeScheduler a(pol, &rng);
        FeeMode m = FeeMode::User;
        uint64_t t = 0;
        for (; t < 5000; ++t) {
            m = a.advance(t * kSec, true, true);
            if (m == FeeMode::Fee && a.state().feeMs >= 20 * kSec) break;
        }
        CHECK(m == FeeMode::Fee);
        const FeeState saved = a.state();
        CHECK(saved.feeMs >= 20 * kSec && saved.feeMs < 22 * kSec);
        FeeScheduler b(pol, &rng);
        b.restore(saved);
        CHECK(b.state() == saved && b.mode() == FeeMode::User);
        CHECK(b.windowRemainingMs() == saved.owedMs - saved.feeMs);
        // the window reopens on the first productive tick and finishes the remainder
        uint64_t feeSecondsAfter = 0;
        FeeMode prev = FeeMode::User;
        uint64_t t0 = 100000;
        for (uint64_t i = 0; i < 120; ++i) {
            const FeeMode nm = b.advance((t0 + i) * kSec, true, true);
            if (prev == FeeMode::Fee) ++feeSecondsAfter;
            prev = nm;
        }
        CHECK(feeSecondsAfter >= 38 && feeSecondsAfter <= 41);  // ~40 s remained
        CHECK(b.state().feeMs >= b.state().owedMs);
    }

    // ---- fee-pool outage in the middle of a window: user mining continues, the window finishes later
    {
        FixedRandom rng(1 * kMin);
        FeeScheduler s(pol, &rng);
        uint64_t t = 0;
        FeeMode m = FeeMode::User;
        while (m != FeeMode::Fee) m = s.advance((t++) * kSec, true, true);
        for (int i = 0; i < 30; ++i) m = s.advance((t++) * kSec, true, true);  // 30 s of fee work
        CHECK(m == FeeMode::Fee);
        s.noteFeePoolFailure(t * kSec);
        CHECK(s.mode() == FeeMode::User);
        // 90 s outage (pool reported unusable): never in fee mode, user time keeps accruing
        const uint64_t userBefore = s.state().userMs, feeBefore = s.state().feeMs;
        for (int i = 0; i < 90; ++i) {
            m = s.advance((t++) * kSec, true, false);
            CHECK(m == FeeMode::User);
        }
        CHECK(s.state().feeMs == feeBefore);
        CHECK(s.state().userMs >= userBefore + 85 * kSec);
        // pool back: the remaining ~30 s are served
        uint64_t feeAfter = 0;
        FeeMode prev = FeeMode::User;
        for (int i = 0; i < 80; ++i) {
            const FeeMode nm = s.advance((t++) * kSec, true, true);
            if (prev == FeeMode::Fee) ++feeAfter;
            prev = nm;
        }
        CHECK(feeAfter >= 28 && feeAfter <= 32);
        CHECK(s.state().feeMs >= s.state().owedMs && s.state().debtMs == 0);
    }

    // ---- retry backoff after a failure, but the user is never blocked
    {
        FixedRandom rng(0);
        FeeScheduler s(pol, &rng);
        s.advance(0, true, true);  // offset 0: the window is due at once
        s.advance(1 * kSec, true, true);
        CHECK(s.mode() == FeeMode::Fee);
        s.noteFeePoolFailure(2 * kSec);
        CHECK(s.mode() == FeeMode::User);
        for (uint64_t t = 2; t < 31; ++t) CHECK(s.advance(t * kSec, true, true) == FeeMode::User);  // inside the 30 s backoff
        CHECK(s.advance(33 * kSec, true, true) == FeeMode::Fee);
    }

    // ---- debt: a cycle in which the window never ran carries ONE window, capped, into the next
    {
        FixedRandom rng(0);
        FeeScheduler s(pol, &rng);
        uint64_t t = 0;
        // an entire cycle with the fee pool unusable
        while (s.state().cycle == 0) { s.advance((t++) * kSec, true, false); CHECK(s.mode() == FeeMode::User); if (t > 7000) break; }
        CHECK(s.state().cycle == 1);
        CHECK(s.state().debtMs == pol.windowMs && s.state().owedMs == 2 * pol.windowMs);
        // a second outage cycle: the debt stays capped at one window (it does not grow to two)
        while (s.state().cycle == 1) { s.advance((t++) * kSec, true, false); if (t > 14000) break; }
        CHECK(s.state().cycle == 2);
        CHECK(s.state().debtMs == pol.windowMs && s.state().owedMs == 2 * pol.windowMs);
        // pool recovers: the two-minute window is served in full, then the debt is gone
        uint64_t feeSeconds = 0;
        FeeMode prev = FeeMode::User;
        while (s.state().cycle == 2 && t < 21000) {
            const FeeMode m = s.advance((t++) * kSec, true, true);
            if (prev == FeeMode::Fee) ++feeSeconds;
            prev = m;
        }
        CHECK(feeSeconds >= 118 && feeSeconds <= 122);
        CHECK(s.state().cycle == 3 && s.state().debtMs == 0 && s.state().owedMs == pol.windowMs);
    }

    // ---- a cycle never exceeds its length: user + fee productive time per cycle == cycleMs
    {
        FixedRandom rng(5 * kMin);
        FeeScheduler s(pol, &rng);
        uint64_t t = 0;
        uint64_t cycleTotal = 0;
        while (s.state().cycle == 0 && t < 7000) {
            s.advance((t++) * kSec, true, true);
            if (s.state().cycle == 0) cycleTotal = s.state().userMs + s.state().feeMs;  // the last value before the roll-over
        }
        CHECK(cycleTotal >= pol.cycleMs - 2 * kSec && cycleTotal <= pol.cycleMs + 2 * kSec);
    }

    // ---- restore rejects insane state (tampering, version skew) and starts a clean cycle
    {
        FixedRandom rng(0);
        FeeScheduler s(pol, &rng);
        FeeState bad;
        bad.initialized = true;
        bad.owedMs = 10 * pol.windowMs;  // far more than one window of debt
        bad.debtMs = 9 * pol.windowMs;
        s.restore(bad);
        CHECK(s.state().owedMs == pol.windowMs && s.state().debtMs == 0 && s.state().cycle == 0);
        FeeState skew;
        skew.initialized = true;
        skew.version = 2;
        skew.owedMs = pol.windowMs;
        s.restore(skew);
        CHECK(s.state().version == 1 && s.state().owedMs == pol.windowMs);
    }

    // ---- state text: round trip, checksum, corruption
    {
        FeeState s;
        s.initialized = true;
        s.cycle = 7;
        s.userMs = 123456;
        s.feeMs = 20000;
        s.owedMs = 120000;
        s.offsetMs = 99999;
        s.debtMs = 60000;
        const std::string text = serializeFeeState(s);
        FeeState back;
        CHECK(parseFeeState(text, &back) && back == s);
        std::string flipped = text;
        flipped[text.find("123456") + 2] = '9';
        CHECK(!parseFeeState(flipped, &back));
        CHECK(!parseFeeState(text.substr(0, text.size() / 2), &back));
        CHECK(!parseFeeState("", &back) && !parseFeeState("{}", &back) && !parseFeeState("not json", &back));
        std::string badCrc = text;
        badCrc.replace(badCrc.rfind("\"crc32\":\"") + 9, 8, "00000000");
        CHECK(!parseFeeState(badCrc, &back));
        // a valid checksum over different content cannot be forged by editing a field
        std::string edited = text;
        edited.replace(edited.find("\"cycle\":7"), 9, "\"cycle\":8");
        CHECK(!parseFeeState(edited, &back));
    }

    // ---- persistence: atomic, fsynced file with a backup; corrupt primary falls back to the backup; both corrupt starts clean
    {
        const std::string dir = makeScratchDir("fee");
        FeeStateFile f(dir);
        FeeState loaded;
        CHECK(f.load(&loaded) == FeeStateFile::LoadResult::Fresh && !loaded.initialized);
        FeeState one;
        one.initialized = true;
        one.cycle = 1;
        one.owedMs = pol.windowMs;
        std::string err;
        CHECK(f.save(one, &err));
        CHECK(f.load(&loaded) == FeeStateFile::LoadResult::Loaded && loaded == one);
        FeeState two = one;
        two.cycle = 2;
        two.userMs = 5000;
        CHECK(f.save(two, &err));
        CHECK(f.load(&loaded) == FeeStateFile::LoadResult::Loaded && loaded == two);
        CHECK(std::filesystem::exists(f.backupPath()));
        CHECK(!std::filesystem::exists(f.path() + ".tmp"));
        // corrupt the primary: the previous good copy is used
        {
            std::ofstream o(f.path(), std::ios::binary | std::ios::trunc);
            o << "{\"version\":1,\"torn write";
        }
        CHECK(f.load(&loaded) == FeeStateFile::LoadResult::LoadedBackup && loaded == one);
        // corrupt both: clean start, flagged so the caller can log it; never an error that blocks mining
        {
            std::ofstream o(f.backupPath(), std::ios::binary | std::ios::trunc);
            o << "garbage";
        }
        CHECK(f.load(&loaded) == FeeStateFile::LoadResult::Corrupt && !loaded.initialized);
        FixedRandom rng(0);
        FeeScheduler s(pol, &rng);
        s.restore(loaded);
        CHECK(s.state().initialized && s.state().debtMs == 0 && s.state().cycle == 0);
        // an unwritable location reports an error instead of crashing
        FeeStateFile bad((std::filesystem::path(dir) / "donation-state.json" / "nested").string());
        CHECK(!bad.save(one, &err) && !err.empty());
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    // ---- the destination is a build-time constant, referenced by hash (public by owner decision, H-018b); the digest is
    //      empty exactly when no destination is compiled in
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(crc32("123456789", 9) == 0xCBF43926u);
    CHECK(feeDestinationConfigured() == !feeDestination().empty());
    CHECK(feeDestinationConfigured() ? feeDestinationDigest() == sha256Hex(feeDestination()) : feeDestinationDigest().empty());
    return finishTests("test_product_fee");
}
