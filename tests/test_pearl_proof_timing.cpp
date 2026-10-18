#include "../src/core/pearl_proof_timing.h"
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <type_traits>

using namespace om::pearl::proof_timing;

template<std::size_t N> struct State {
    uint64_t time = 100;
    unsigned clocks = 0, publications = 0;
    Sample<N> sample;
};
template<std::size_t N> struct Fake {
    State<N> *state;
    uint64_t now() noexcept { ++state->clocks; return state->time; }
    void publish(const Sample<N> &sample) noexcept { state->sample = sample; ++state->publications; }
};

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--emit") == 0) {
        {
            NativeScope sample(std::getenv("CANARY_PEARL_PROOF_TIMING"), 42);
            sample.shape(4096, 65536, 2048, 128);
            for (int i = 0; i < 6; ++i) sample.boundary();
            sample.proofToken(7);
            sample.completeOnExit();
        }
        {
            SourceScope sample(std::getenv("CANARY_PEARL_PROOF_TIMING"), 42, 7);
            sample.boundary(); sample.boundary(); sample.finish();
        }
        {
            HostVerifyScope sample(std::getenv("CANARY_PEARL_PROOF_TIMING"), 42, 7);
            sample.boundary(); sample.finish();
        }
        std::puts("unchanged ordinary output");
        return 0;
    }
    using Native = Scope<6, Fake<6>>;
    static_assert(!std::is_copy_constructible<Native>::value && !std::is_move_constructible<Native>::value);
    static_assert(std::is_nothrow_destructible<Native>::value);
    const char *disabled[] = {nullptr, "", "0", "01", "true", "1 ", " 1", "11", "-1"};
    for (const char *option : disabled) {
        State<6> state;
        { Native sample(option, 42, 0, Fake<6>{&state});
          sample.boundary(); sample.shape(1, 2, 3, 4); sample.proofToken(8);
          sample.finish(); sample.completeOnExit(); }
        assert(state.clocks == 0 && state.publications == 0);
    }
    {
        State<6> state;
        { Native sample("1", 42, 0, Fake<6>{&state});
          sample.shape(4096, 65536, 2048, 128);
          for (int i = 0; i < 6; ++i) { state.time += 10; sample.boundary(); }
          sample.proofToken(7); sample.completeOnExit();
          state.time += 15; } // simulated later buffer destruction belongs to total
        assert(state.publications == 1 && state.clocks == 8);
        assert(state.sample.complete && state.sample.valid && state.sample.stages == 6);
        assert(state.sample.nonce == 42 && state.sample.proofToken == 7);
        assert(state.sample.totalNs == 75 && state.sample.tailNs == 15);
        assert(state.sample.startNs == 100 && state.sample.endNs == 175);
        assert(state.sample.m == 4096 && state.sample.n == 65536 && state.sample.k == 2048 && state.sample.rank == 128);
        for (auto ns : state.sample.stageNs) assert(ns == 10);
    }
    {
        State<2> state;
        { Scope<2, Fake<2>> sample("1", 1, 9, Fake<2>{&state});
          state.time = 110; sample.boundary(); state.time = 140; sample.boundary();
          state.time = 145; sample.finish(); state.time = 999999; }
        assert(state.clocks == 4 && state.sample.complete && state.sample.valid);
        assert(state.sample.totalNs == 45 && state.sample.tailNs == 5);
        assert(state.sample.stageNs[0] == 10 && state.sample.stageNs[1] == 30);
    }
    {
        State<6> state;
        try { Native sample("1", 1, 0, Fake<6>{&state});
              state.time = 110; sample.boundary(); state.time = 125;
              throw std::runtime_error("simulated proof failure"); }
        catch (const std::runtime_error &) {}
        assert(state.publications == 1 && !state.sample.complete && state.sample.valid);
        assert(state.sample.stages == 1 && state.sample.totalNs == 25 && state.sample.tailNs == 15);
    }
    {
        State<2> state;
        { Scope<2, Fake<2>> sample("1", 1, 0, Fake<2>{&state});
          state.time = 90; sample.boundary(); state.time = 110; sample.boundary(); sample.finish(); }
        assert(!state.sample.valid && !state.sample.complete); // no unsigned wrap on clock regression
    }
    {
        State<2> state;
        { Scope<2, Fake<2>> sample("1", 1, 0, Fake<2>{&state}); sample.boundary(); sample.finish(); }
        assert(!state.sample.valid && !state.sample.complete); // missing stage is not complete
    }
    {
        State<2> state;
        { Scope<2, Fake<2>> sample("1", 1, 0, Fake<2>{&state});
          sample.boundary(); sample.boundary(); sample.boundary(); sample.finish(); }
        assert(!state.sample.valid && !state.sample.complete); // no out-of-bounds stage write
    }
    {
        State<1> state;
        { Scope<1, Fake<1>> sample("1", 7, 9, Fake<1>{&state});
          state.time += 20; sample.boundary(); sample.completeOnExit();
          state.time += 15000000000ULL; }
        assert(state.sample.valid && state.sample.complete);
        assert(state.sample.stageNs[0] == 20 && state.sample.tailNs == 15000000000ULL);
        assert(state.sample.totalNs == 15000000020ULL && state.publications == 1);
    }
    {
        State<1> state;
        { Scope<1, Fake<1>> sample("1", 7, 9, Fake<1>{&state});
          state.time += 20; sample.boundary(); } // send failed; no completed reply phase
        assert(state.sample.valid && !state.sample.complete && state.sample.totalNs == 20);
    }
    {
        State<1> state;
        { Scope<1, Fake<1>> sample(nullptr, 7, 9, Fake<1>{&state});
          sample.boundary(); sample.completeOnExit(); }
        assert(state.clocks == 0 && state.publications == 0);
        om::pearl::proof_timing::NetworkScope production(nullptr, 7, 9);
        production.boundary(); production.completeOnExit();
    }
    std::puts("PASS: proof and network timing, opt-in, monotonicity, partial exits and frozen source span");
}
