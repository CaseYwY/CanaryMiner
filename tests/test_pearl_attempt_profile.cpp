#include "../src/algos/pearl-pow/attempt_profile.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <type_traits>

using namespace om::pearl::profile;

struct FakeState {
    int creates = 0, records = 0, queries = 0, destroys = 0, clocks = 0;
    int publications = 0, disabled = 0;
    int failCreate = 0, failRecord = 0, failQuery = 0;
    bool failureReturnsHandle = false;
    std::array<int, kStages + 2> destroyed{};
    std::array<float, kStages> durations{{1, 2, 3, 4, 5, 6, 7}};
    Sample published;
};

struct FakeAdapter {
    using Event = int;
    using Stream = int;
    using TimePoint = double;
    FakeState *state = nullptr;
    bool create(Event *event) noexcept {
        ++state->creates;
        if (state->creates == state->failCreate) {
            if (state->failureReturnsHandle) *event = state->creates;
            return false;
        }
        *event = state->creates;
        return true;
    }
    bool record(Event event, Stream stream) noexcept {
        assert(event >= 1 && event <= 8 && stream == 42);
        ++state->records;
        return state->records != state->failRecord;
    }
    bool elapsed(float *ms, Event first, Event last) noexcept {
        assert(first >= 1 && first <= 7 && last == first + 1);
        ++state->queries;
        if (state->queries == state->failQuery) return false;
        *ms = state->durations[static_cast<size_t>(first - 1)];
        return true;
    }
    void destroy(Event event) noexcept {
        assert(event >= 1 && event <= 8);
        assert(++state->destroyed[static_cast<size_t>(event)] == 1);
        ++state->destroys;
    }
    TimePoint now() noexcept { return ++state->clocks; }
    double milliseconds(TimePoint first, TimePoint last) noexcept { return last - first; }
    void disabled(const char *) noexcept { ++state->disabled; }
    void publish(const Sample &sample, uint32_t interval, uint64_t nonce,
                 uint32_t m, uint32_t n, size_t tile, int arch,
                 std::string_view device) noexcept {
        assert(interval == 100 && nonce == 99 && m == 4096 && n == 65536);
        assert(tile == 3 && arch == 120 && device == "device");
        assert(sample.valid());
        state->published = sample;
        ++state->publications;
    }
};

using Profiler = AttemptProfiler<FakeAdapter>;
static_assert(!std::is_copy_constructible<Profiler>::value, "event ownership");
static_assert(!std::is_move_constructible<Profiler>::value, "event ownership");
static_assert(!std::is_copy_assignable<Profiler>::value, "event ownership");
static_assert(!std::is_move_assignable<Profiler>::value, "event ownership");

void arm(Profiler &profiler) {
    for (int i = 0; i < 100; ++i) profiler.begin();
}
void boundaries(Profiler &profiler) {
    for (size_t i = 0; i <= kStages; ++i) profiler.boundary(i, 42);
}
void complete(Profiler &profiler) {
    profiler.complete(99, 4096, 65536, 3, 120, "device");
}
void sampleAttempt(Profiler &profiler) {
    boundaries(profiler);
    profiler.enqueued();
    complete(profiler);
}
void assertDisabled(Profiler &profiler, FakeState &state) {
    const int creates = state.creates, records = state.records;
    const int queries = state.queries, clocks = state.clocks;
    const int publications = state.publications;
    arm(profiler);
    sampleAttempt(profiler);
    assert(state.creates == creates && state.records == records);
    assert(state.queries == queries && state.clocks == clocks);
    assert(state.publications == publications);
}

void lifecycleTests() {
    // This is the exact gate used by CUDA prepare, including the lifetime
    // capture: a later unset must not hide an earlier profiling configuration.
    for (const char *setting : {static_cast<const char *>(nullptr), "", "0",
                               "100", "1000000000", "bad"}) {
        FakeState state;
        Profiler profiler(setting, FakeAdapter{&state});
        assert(profiler.formalCompatible(nullptr) == (setting == nullptr));
        for (const char *current : {"", "0", "100", "bad"})
            assert(!profiler.formalCompatible(current));
        assert(!state.creates && !state.records && !state.queries && !state.clocks);
    }
    for (const char *setting : {static_cast<const char *>(nullptr), "0", "bad"}) {
        FakeState state;
        {
            Profiler profiler(setting, FakeAdapter{&state});
            arm(profiler);
            sampleAttempt(profiler);
            profiler.cancel();
            profiler.close();
        }
        assert(!state.creates && !state.records && !state.queries && !state.clocks);
        assert(!state.destroys && !state.publications);
    }
    // Enabled but not armed: exactly what the tuner's runAttempt calls see.
    {
        FakeState state;
        Profiler profiler("100", FakeAdapter{&state});
        sampleAttempt(profiler);
        assert(!state.creates && !state.records && !state.queries && !state.clocks);
        assert(!state.publications);
    }
    // Every partial-create prefix, including an adapter that returns a handle
    // along with failure: retain and destroy all owned handles exactly once.
    for (int fail = 1; fail <= 8; ++fail) {
        for (bool returnedHandle : {false, true}) {
            FakeState state;
            state.failCreate = fail;
            state.failureReturnsHandle = returnedHandle;
            {
                Profiler profiler("100", FakeAdapter{&state});
                arm(profiler);
                assert(state.creates == fail && !state.destroys && !state.clocks);
                assert(state.disabled == 1 && !state.publications);
                assertDisabled(profiler, state);
                profiler.close();
                profiler.close();
                assert(state.destroys == fail - (returnedHandle ? 0 : 1));
            }
            assert(state.destroys == fail - (returnedHandle ? 0 : 1));
        }
    }
    for (int fail = 1; fail <= 8; ++fail) {
        FakeState state;
        state.failRecord = fail;
        {
            Profiler profiler("100", FakeAdapter{&state});
            arm(profiler);
            sampleAttempt(profiler);
            assert(state.records == fail && state.disabled == 1);
            assert(!state.queries && !state.publications && !state.destroys);
            assertDisabled(profiler, state);
        }
        assert(state.destroys == 8);
    }
    for (int fail = 1; fail <= 7; ++fail) {
        FakeState state;
        state.failQuery = fail;
        {
            Profiler profiler("100", FakeAdapter{&state});
            arm(profiler);
            sampleAttempt(profiler);
            assert(state.queries == fail && state.disabled == 1);
            assert(!state.publications);
            assertDisabled(profiler, state);
        }
        assert(state.destroys == 8);
    }
    // Successful complete: seven distinct bins, exact ordering, no extra
    // event/clock calls after completion; cancel/release-style reuse is safe.
    {
        FakeState state;
        {
            Profiler profiler("100", FakeAdapter{&state});
            arm(profiler);
            sampleAttempt(profiler);
            assert(state.publications == 1 && state.creates == 8);
            assert(state.records == 8 && state.queries == 7 && state.clocks == 4);
            assert(state.published.gpuMs == state.durations);
            assert(state.published.gpuTotalMs() == 28);
            assert(state.published.enqueueMs == 1 && state.published.readbackWaitMs == 1);
            complete(profiler);
            assert(state.publications == 1 && state.clocks == 4);
            arm(profiler);
            profiler.boundary(0, 42);
            profiler.cancel();
            sampleAttempt(profiler);
            assert(state.publications == 1 && state.creates == 8);
            arm(profiler);
            sampleAttempt(profiler);
            assert(state.publications == 2 && state.creates == 8);
            profiler.close();
            profiler.close();
            assertDisabled(profiler, state);
        }
        assert(state.destroys == 8);
    }
    // Bad call ordering cannot publish old or partly populated durations.
    for (int kind = 0; kind < 4; ++kind) {
        FakeState state;
        Profiler profiler("100", FakeAdapter{&state});
        arm(profiler);
        if (kind == 0) profiler.boundary(1, 42);
        if (kind == 1) profiler.enqueued();
        if (kind == 2) { boundaries(profiler); complete(profiler); }
        if (kind == 3) { boundaries(profiler); profiler.enqueued(); profiler.enqueued(); }
        assert(state.disabled == 1 && !state.publications);
        assertDisabled(profiler, state);
    }
    // Duration validation is exercised through the actual completion path.
    for (float bad : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN()}) {
        FakeState state;
        state.durations[3] = bad;
        Profiler profiler("100", FakeAdapter{&state});
        arm(profiler);
        sampleAttempt(profiler);
        assert(state.disabled == 1 && !state.publications);
        assertDisabled(profiler, state);
    }
}

int main() {
    lifecycleTests();
    uint32_t interval = 9;
    assert(parseInterval(nullptr, &interval) && interval == 0);
    assert(parseInterval("0", &interval) && interval == 0);
    assert(parseInterval("100", &interval) && interval == 100);
    assert(parseInterval("1000000000", &interval) && interval == 1000000000);
    assert(!parseInterval("100", nullptr));
    for (const char *bad : {"", "1", "99", "-100", "+100", " 100", "100 ",
                            "100x", "1e3", "1000000001", "4294967295",
                            "999999999999999999999999", "00000000000"}) {
        interval = 99;
        assert(!parseInterval(bad, &interval));
        assert(interval == 0);
    }
    Schedule off;
    for (int i = 0; i < 1000; ++i) assert(!off.next());
    Schedule sparse(100);
    for (int i = 1; i <= 1000; ++i) assert(sparse.next() == (i % 100 == 0));
    Schedule large(1000000000);
    for (int i = 0; i < 1000; ++i) assert(!large.next());

    Sample sample;
    assert(!sample.valid()); // never publish unpopulated/zero CUDA timings
    sample.gpuMs.fill(0.5f);
    assert(sample.valid());
    assert(sample.gpuTotalMs() == 3.5);
    for (size_t i = 0; i < kStages; ++i) {
        for (float bad : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
            sample.gpuMs[i] = bad;
            assert(!sample.valid());
        }
        sample.gpuMs[i] = 0.5f;
    }
    for (double bad : {-1.0, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
        sample.enqueueMs = bad;
        assert(!sample.valid());
        sample.enqueueMs = 1;
        sample.readbackWaitMs = bad;
        assert(!sample.valid());
        sample.readbackWaitMs = 1;
    }
    assert(sample.valid());
    puts("Pearl attempt profiling host configuration/timing/lifecycle validation passed");
}
