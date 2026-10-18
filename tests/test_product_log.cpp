// Redaction filter, rotating log sink (size cap), and the stdout/stderr tee.

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

#include "../src/product/log_sink.h"
#include "../src/product/redact.h"
#include "product_test_util.h"

using namespace om;
namespace fs = std::filesystem;

static std::string slurp(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool has(const std::string &h, const std::string &n) { return h.find(n) != std::string::npos; }

int main() {
    const std::string wallet = "prl1qaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaTAIL";  // 'prl1' + 44 chars (a test value, not an address)
    Redactor r;
    r.addSecret(wallet, true);
    r.addSecret("hunter22");
    r.addSecret("ab");  // too short: ignored

    // registered secrets and generic address shapes
    CHECK(r.apply("connecting as " + wallet + " now") == "connecting as <redacted:...TAIL> now");
    CHECK(!has(r.apply("password hunter22 used hunter22"), "hunter22"));
    const std::string generic = std::string("prl1") + "qbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbwxyz";  // not a literal address in the source
    CHECK(r.apply("other " + generic + " end") == "other prl1...wxyz end");
    CHECK(r.apply("prl1short stays") == "prl1short stays");
    CHECK(r.apply("xprl1qccccccccccccccccccccccccccccccccc") == "xprl1qccccccccccccccccccccccccccccccccc");  // not at a token boundary
    CHECK(r.apply("a ab b") == "a ab b");
    // option values and JSON members
    CHECK(r.apply("canary-miner --wallet SOMETHING --worker w --pass opensesame --pool x:1") ==
          "canary-miner --wallet <redacted> --worker w --pass <redacted> --pool x:1");
    CHECK(r.apply("--wallet=ADDR123 --user bob") == "--wallet=<redacted> --user <redacted>");
    CHECK(r.apply("--username keep") == "--username keep");
    CHECK(r.apply("{\"wallet\":\"A\",\"worker\":\"w\",\"pass\":\"x\",\"password\" : \"p q\"}") ==
          "{\"wallet\":\"<redacted>\",\"worker\":\"w\",\"pass\":\"<redacted>\",\"password\" : \"<redacted>\"}");
    CHECK(r.apply("{\"authorization\":\"Bearer abc\"}") == "{\"authorization\":\"<redacted>\"}");
    CHECK(r.apply("{\"wallet\":\"<redacted>\"}") == "{\"wallet\":\"<redacted>\"}");  // idempotent
    CHECK(r.apply(r.apply("--pass zzz")) == r.apply("--pass zzz"));
    Redactor withWorker;
    withWorker.addSecret("my-rig-7");
    CHECK(withWorker.apply("worker my-rig-7 up") == "worker <redacted> up");

    // ---------------------------------------------------------------- rotating sink
    const std::string dir = makeScratchDir("log");
    {
        LogSinkConfig cfg;
        cfg.dir = (fs::path(dir) / "a" / "b").string();  // nested: created
        cfg.maxFileBytes = 1024;
        cfg.maxFiles = 3;
        LogSink sink(cfg, &r);
        std::string err;
        CHECK(sink.open(&err));
        for (int i = 0; i < 200; ++i) sink.write("line " + std::to_string(i) + " " + std::string(40, 'x') + " " + wallet);
        sink.close();
        uint64_t total = 0;
        int files = 0;
        for (unsigned i = 0; i < 6; ++i) {
            const std::string p = sink.pathFor(i);
            if (!fs::exists(p)) continue;
            ++files;
            const auto sz = fs::file_size(p);
            CHECK_MSG("every log file respects the cap", sz <= 1024);
            total += sz;
            CHECK_MSG("no secret reaches a log file", !has(slurp(p), "aaaaaaaa") && has(slurp(p), "<redacted:...TAIL>"));
        }
        CHECK_MSG("exactly max_files files are kept", files == 3);
        CHECK_MSG("total disk use is bounded by max_files * max_file_bytes", total <= 3 * 1024);
        CHECK_MSG("the newest lines are in the current file", has(slurp(sink.pathFor(0)), "line 199"));
        CHECK_MSG("the oldest lines were dropped", !has(slurp(sink.pathFor(0)) + slurp(sink.pathFor(1)) + slurp(sink.pathFor(2)), "line 0 "));
        CHECK(!fs::exists(sink.pathFor(3)));
    }
    {
        // a line longer than the cap is truncated, not allowed to blow the cap
        LogSinkConfig cfg;
        cfg.dir = (fs::path(dir) / "long").string();
        cfg.maxFileBytes = 512;
        cfg.maxFiles = 1;
        LogSink sink(cfg, nullptr);
        std::string err;
        CHECK(sink.open(&err));
        sink.write(std::string(5000, 'y'));
        sink.write("tail");
        sink.close();
        CHECK(fs::file_size(sink.pathFor(0)) <= 512);
        CHECK(has(slurp(sink.pathFor(0)), "tail"));
    }
    {
        LogSinkConfig bad;
        std::string err;
        LogSink none(bad, nullptr);
        CHECK(!none.open(&err));
        bad.dir = dir;
        bad.maxFiles = 0;
        LogSink zero(bad, nullptr);
        CHECK(!zero.open(&err));
        // appends to an existing file and counts its size toward the cap
        LogSinkConfig cfg;
        cfg.dir = (fs::path(dir) / "append").string();
        cfg.maxFileBytes = 300;
        cfg.maxFiles = 2;
        {
            LogSink s1(cfg, nullptr);
            CHECK(s1.open(&err));
            s1.write(std::string(200, 'p'));
        }
        LogSink s2(cfg, nullptr);
        CHECK(s2.open(&err));
        s2.write(std::string(200, 'q'));  // would exceed 300: rotates first
        s2.close();
        CHECK(fs::exists(s2.pathFor(1)) && has(slurp(s2.pathFor(1)), "ppp") && has(slurp(s2.pathFor(0)), "qqq"));
    }

#if !defined(_WIN32)
    // ---------------------------------------------------------------- tee: stdout/stderr -> redacted file and console
    {
        LogSinkConfig cfg;
        cfg.dir = (fs::path(dir) / "tee").string();
        LogSink sink(cfg, &r);
        std::string err;
        CHECK(sink.open(&err));
        // capture the "console" the tee forwards to: a temp file standing in for the original stdout
        const std::string consolePath = (fs::path(dir) / "console.txt").string();
        fflush(stdout);
        const int savedOut = dup(1);
        FILE *cf = freopen(consolePath.c_str(), "wb", stdout);
        CHECK(cf != nullptr);
        {
            OutputTee tee(&sink, &r);
            const bool started = tee.start();
            std::printf("hello from stdout %s\n", wallet.c_str());
            std::fprintf(stderr, "problem on stderr --pass swordfish\n");
            std::printf("partial line without newline");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            tee.stop();
            CHECK(started);
        }
        fflush(stdout);
        dup2(savedOut, 1);
        close(savedOut);
        clearerr(stdout);
        const std::string logText = slurp(sink.pathFor(0));
        const std::string consoleText = slurp(consolePath);
        CHECK(has(logText, "hello from stdout <redacted:...TAIL>"));
        CHECK(has(logText, "problem on stderr --pass <redacted>"));
        CHECK(has(logText, "partial line without newline"));
        CHECK(has(consoleText, "hello from stdout <redacted:...TAIL>"));
        CHECK(!has(consoleText, "aaaaaaaaaaaaaaaa") && !has(logText, "swordfish"));
    }
#endif
    std::error_code ec;
    fs::remove_all(dir, ec);
    return finishTests("test_product_log");
}
