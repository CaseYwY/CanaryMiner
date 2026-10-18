// Shutdown state machine: RUNNING -> DRAINING -> PERSIST -> CLOSE -> RELEASE, with an injected clock and hooks.
// ShutdownDriver (the watcher thread) is exercised on the normal, non-forced path only: its forced path terminates the process.

#include <atomic>
#include <chrono>
#include <thread>

#include "../src/product/product_main.h"
#include "../src/product/shutdown.h"
#include "product_test_util.h"

using namespace om;
using S = ShutdownState;

struct Probe {
    bool drained = false;
    uint64_t inFlight = 0, markedUnknown = 0;
    int persisted = 0, closed = 0, released = 0;
    bool persistForced = false;
    uint64_t persistUnknown = 99;
    std::string persistReason;
    bool persistOk = true;
    std::vector<std::string> order;

    ShutdownHooks hooks() {
        ShutdownHooks h;
        h.drained = [this] { return drained; };
        h.inFlight = [this] { return inFlight; };
        h.markUnknown = [this](uint64_t n) { markedUnknown += n; order.push_back("unknown"); };
        h.persist = [this](bool forced, uint64_t unknown, const std::string &reason) {
            ++persisted;
            persistForced = forced;
            persistUnknown = unknown;
            persistReason = reason;
            order.push_back("persist");
            return persistOk;
        };
        h.close = [this] { ++closed; order.push_back("close"); return true; };
        h.release = [this] { ++released; order.push_back("release"); return true; };
        return h;
    }
};

int main() {
    // ---- normal path: the run loop finishes before the deadline
    {
        Probe p;
        ShutdownMachine m(10000, p.hooks());
        CHECK(m.state() == S::Running && !m.stopRequested());
        CHECK(m.step(0) == S::Running && m.step(5000) == S::Running);  // nothing happens without a request
        CHECK(p.persisted == 0 && p.closed == 0 && p.released == 0);
        m.requestStop("signal");
        m.requestStop("second request is ignored");
        CHECK(m.stopRequested());
        CHECK(m.step(100) == S::Draining);
        CHECK(m.step(200) == S::Draining && p.persisted == 0);  // still draining: not drained yet
        p.drained = true;
        CHECK(m.step(300) == S::Persist);
        CHECK(m.step(300) == S::Close && p.persisted == 1);
        CHECK(m.step(300) == S::Release && p.closed == 1);
        CHECK(m.step(300) == S::Done && p.released == 1 && m.done());
        CHECK(m.step(400) == S::Done && p.persisted == 1 && p.closed == 1 && p.released == 1);  // done is terminal
        CHECK(!m.forced() && m.unknownRecorded() == 0 && p.markedUnknown == 0);
        CHECK(!p.persistForced && p.persistUnknown == 0 && p.persistReason == "signal");
        const std::vector<S> expect = {S::Running, S::Draining, S::Persist, S::Close, S::Release, S::Done};
        CHECK(m.trace() == expect);
        CHECK((p.order == std::vector<std::string>{"persist", "close", "release"}));
    }
    // ---- the drain deadline is exact: 10 s from the request, not from construction
    {
        Probe p;
        p.inFlight = 3;
        ShutdownMachine m(10000, p.hooks());
        m.requestStop("signal");
        CHECK(m.step(1000) == S::Draining);        // deadline = 11000
        CHECK(m.step(10999) == S::Draining);
        CHECK(p.markedUnknown == 0 && !m.forced());
        CHECK(m.step(11000) == S::Persist);        // the deadline passed
        CHECK(m.forced() && m.unknownRecorded() == 3 && p.markedUnknown == 3);
        m.step(11000);
        CHECK(p.persistForced && p.persistUnknown == 3);
        // unknown is recorded BEFORE persist so the persisted record includes it
        CHECK(p.order.size() >= 2 && p.order[0] == "unknown" && p.order[1] == "persist");
        while (!m.done()) m.step(11000);
        CHECK(p.released == 1);
    }
    // ---- in-flight work that completes in time is not marked unknown
    {
        Probe p;
        p.inFlight = 2;
        ShutdownMachine m(5000, p.hooks());
        m.requestStop("signal");
        m.step(0);
        p.drained = true;
        m.step(4999);
        CHECK(!m.forced() && p.markedUnknown == 0);
    }
    // ---- nothing in flight at the deadline: forced, but zero unknown
    {
        Probe p;
        ShutdownMachine m(1000, p.hooks());
        m.requestStop("signal");
        m.step(0);
        m.step(1000);
        CHECK(m.forced() && m.unknownRecorded() == 0 && p.markedUnknown == 0);
    }
    // ---- a failing persist does not stop the shutdown
    {
        Probe p;
        p.persistOk = false;
        p.drained = true;
        ShutdownMachine m(1000, p.hooks());
        m.requestStop("signal");
        while (!m.done()) m.step(0);
        CHECK(m.persistFailed() && p.closed == 1 && p.released == 1);
    }
    // ---- missing hooks are tolerated
    {
        ShutdownMachine m(1000, ShutdownHooks{});
        m.requestStop("signal");
        m.step(0);
        m.step(1000);  // no drained hook: only the deadline moves it on
        while (!m.done()) m.step(1000);
        CHECK(m.forced());
    }
    // ---- concurrent requests: the first reason wins and the machine is requested exactly once
    {
        Probe p;
        ShutdownMachine m(1000, p.hooks());
        std::vector<std::thread> ts;
        for (int i = 0; i < 8; ++i) ts.emplace_back([&m, i] { m.requestStop("r" + std::to_string(i)); });
        for (auto &t : ts) t.join();
        p.drained = true;
        while (!m.done()) m.step(0);
        CHECK(p.persistReason.size() == 2 && p.persistReason[0] == 'r');
    }
    CHECK(std::string(shutdownStateName(S::Persist)) == "PERSIST" && std::string(shutdownStateName(S::Release)) == "RELEASE");

    // ---- ShutdownDriver on its own thread (normal path)
    {
        Probe p;
        volatile sig_atomic_t stopFlag = 0;
        std::atomic<bool> loopDone{false};
        std::vector<std::string> states;
        std::mutex mu;
        ShutdownDriver driver(2000, p.hooks(), &stopFlag, &loopDone, [&](const char *s) { std::lock_guard<std::mutex> l(mu); states.push_back(s); });
        driver.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        CHECK(!driver.machine().stopRequested());
        stopFlag = 1;  // what the signal handler does
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(driver.machine().state() == S::Draining);
        loopDone = true;   // the run loop returned
        driver.runLoopFinished();
        driver.join();
        CHECK(driver.machine().done() && !driver.machine().forced() && p.persisted == 1 && p.closed == 1 && p.released == 1);
        std::lock_guard<std::mutex> l(mu);
        CHECK(!states.empty() && states.front() == "draining" && states.back() == "stopped");
    }

    // ---- the shutdown record is valid JSON with the documented fields
    {
        StatsSnapshot snap;
        snap.devices.resize(1);
        snap.devices[0].outcomes.accepted = 5;
        snap.devices[0].outcomes.unknown = 2;
        const std::string text = shutdownRecordJson(true, 2, "signal", snap);
        JsonValue v;
        std::string err;
        CHECK(parseJsonStrict(text, &v, &err) && v.find("forced") && v.find("forced")->boolean && v.find("replayed") && v.find("replayed")->text == "0");
        CHECK(v.find("in_flight_recorded_unknown") && v.find("in_flight_recorded_unknown")->text == "2" && v.find("final_outcomes")->find("accepted")->text == "5");
    }
    // ---- atomic write helper
    {
        const std::string dir = makeScratchDir("shutdown");
        const std::string path = (std::filesystem::path(dir) / "sub" / "shutdown-record.json").string();
        std::string err;
        CHECK(writeFileAtomic(path, "{\"a\":1}\n", &err));
        std::string back;
        CHECK(readTextFile(path, &back) && back == "{\"a\":1}\n");
        CHECK(writeFileAtomic(path, "{\"a\":2}\n", &err) && readTextFile(path, &back) && back == "{\"a\":2}\n");
        CHECK(!std::filesystem::exists(path + ".tmp"));
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    return finishTests("test_product_shutdown");
}
