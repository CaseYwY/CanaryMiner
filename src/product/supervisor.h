// Multi-GPU supervisor (H-032). `canary-miner run` with N selected GPUs starts N child processes of itself, one per GPU, each with
// the hidden internal flags `--devices <uuid> --api-port <port> --api-bind 127.0.0.1 --worker-child --worker-id <index>`. A child
// runs the existing, tested single-GPU path unchanged: its own donation scheduler (1% per GPU), its own state and log folder
// (gpu-<index>), and its own stats endpoint on a loopback port.
//
// The supervisor:
//   - polls each child's stats endpoint and NVML (current clocks, power, temperature, fan only: never supported clocks);
//   - aggregates the children into the public stats document (devices[] per GPU, totals, the donation block);
//   - renders the dashboard (dashboard.h) on a TTY, or plain timestamped lines otherwise;
//   - restarts a child that exits unexpectedly after 10 s, at most 3 times per hour per GPU (GuardRestartBudget), and logs it;
//   - stops every child on Ctrl-C, SIGTERM, console close or [Q]: closing a child's stdin is the stop request (its stdin watcher
//     sets the same stop flag as a signal), and each child drains and flushes on its own path.
//
// Nothing here prints or stores the wallet or the donation account: child output passes through a Redactor that knows both.

#pragma once

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../core/platform.h"  // winsock2.h must come before windows.h
#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#endif

#include "../core/canary_version.h"
#include "../core/telemetry.h"
#include "dashboard.h"
#include "json_dom.h"
#include "product_cli.h"
#include "redact.h"
#include "restart_budget.h"
#include "settings.h"
#include "session_intent.h"
#include "accepted_history.h"
#include "stats.h"
#include "stats_server.h"

namespace om {

inline uint64_t supervisorNowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

/** Full path of this executable (the children are started from it). */
inline std::string selfExecutablePath() {
#if defined(_WIN32)
    char buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    return n == 0 || n >= sizeof(buf) ? std::string() : std::string(buf, n);
#else
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    return n <= 0 ? std::string() : std::string(buf, static_cast<size_t>(n));
#endif
}

/** Splits bytes into lines (LF or CRLF); a partial line waits for the rest. Long unterminated output is cut at 8 KiB. */
inline void feedLines(std::string *pending, const char *data, size_t n, const std::function<void(const std::string &)> &sink) {
    pending->append(data, n);
    size_t start = 0, nl;
    while ((nl = pending->find('\n', start)) != std::string::npos) {
        std::string line = pending->substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) sink(line);
        start = nl + 1;
    }
    pending->erase(0, start);
    if (pending->size() > 8192) {
        sink(pending->substr(0, 8192));
        pending->clear();
    }
}

/**
 * One child process. Its stdin is a pipe (closing it is the stop request); its stdout and stderr share one pipe, read line by line
 * on a thread. The child inherits this console, so Ctrl-C reaches it directly.
 */
class ChildProcess {
   public:
    using LineSink = std::function<void(const std::string &)>;

    ChildProcess() = default;
    ChildProcess(const ChildProcess &) = delete;
    ChildProcess &operator=(const ChildProcess &) = delete;
    ~ChildProcess() {
        if (started_ && !done_) kill();
        closeStdin();
        joinReader();
#if defined(_WIN32)
        if (hProcess_) CloseHandle(hProcess_);
#endif
    }

    /** Starts `exe` with `args` (argument 0 is `exe`). Returns false with *error set when the process cannot be created. */
    bool start(const std::string &exe, const std::vector<std::string> &args, LineSink sink, std::string *error) {
#if defined(_WIN32)
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
        if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0)) {
            if (error) *error = "cannot create pipes for the worker";
            return false;
        }
        SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);  // our ends are never inherited by the child
        SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
        std::string cmd = winQuote(exe);
        for (const std::string &a : args) cmd += " " + winQuote(a);
        std::vector<char> mutableCmd(cmd.begin(), cmd.end());
        mutableCmd.push_back('\0');
        // Only the two pipe ends are inherited: the attribute list names them, so no other inheritable handle of ours reaches the worker.
        SIZE_T attrBytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrBytes);
        std::vector<char> attrBuf(attrBytes);
        LPPROC_THREAD_ATTRIBUTE_LIST attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
        HANDLE inherit[2] = {inR, outW};
        const bool listReady = InitializeProcThreadAttributeList(attrs, 1, 0, &attrBytes) &&
                               UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr);
        STARTUPINFOEXA si{};
        si.StartupInfo.cb = sizeof(si);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = inR;
        si.StartupInfo.hStdOutput = outW;
        si.StartupInfo.hStdError = outW;
        si.lpAttributeList = attrs;
        PROCESS_INFORMATION pi{};
        const DWORD flags = listReady ? EXTENDED_STARTUPINFO_PRESENT : 0;
        const BOOL ok = listReady && CreateProcessA(exe.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si.StartupInfo, &pi);
        const DWORD createError = GetLastError();
        if (listReady) DeleteProcThreadAttributeList(attrs);
        CloseHandle(inR);
        CloseHandle(outW);
        if (!ok) {
            if (error) *error = "cannot start the worker (error " + std::to_string(createError) + ")";
            CloseHandle(inW);
            CloseHandle(outR);
            return false;
        }
        CloseHandle(pi.hThread);
        hProcess_ = pi.hProcess;
        hIn_ = inW;
        hOut_ = outR;
        started_ = true;
        reader_ = std::thread([this, sink] { readLoop(sink); });
        return true;
#else
        int inp[2], outp[2];
        if (pipe2(inp, O_CLOEXEC) != 0 || pipe2(outp, O_CLOEXEC) != 0) {
            if (error) *error = "cannot create pipes for the worker";
            return false;
        }
        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(exe.c_str()));
        for (const std::string &a : args) argv.push_back(const_cast<char *>(a.c_str()));
        argv.push_back(nullptr);
        const pid_t pid = fork();
        if (pid == 0) {
            dup2(inp[0], STDIN_FILENO);
            dup2(outp[1], STDOUT_FILENO);
            dup2(outp[1], STDERR_FILENO);
            execv(exe.c_str(), argv.data());
            _exit(127);
        }
        close(inp[0]);
        close(outp[1]);
        if (pid < 0) {
            if (error) *error = "cannot start the worker (fork failed)";
            close(inp[1]);
            close(outp[0]);
            return false;
        }
        pid_ = pid;
        inFd_ = inp[1];
        outFd_ = outp[0];
        started_ = true;
        reader_ = std::thread([this, sink] { readLoop(sink); });
        return true;
#endif
    }

    bool started() const { return started_; }
    bool finished() const { return done_; }

    /** Non-blocking: true once the process has ended (*code is its exit status). */
    bool exited(int *code) {
        if (!started_) return false;
        if (!done_) {
#if defined(_WIN32)
            if (WaitForSingleObject(hProcess_, 0) != WAIT_OBJECT_0) return false;
            DWORD c = 0;
            GetExitCodeProcess(hProcess_, &c);
            exitCode_ = static_cast<int>(c);
#else
            int st = 0;
            if (waitpid(pid_, &st, WNOHANG) != pid_) return false;
            exitCode_ = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
#endif
            done_ = true;
        }
        if (code) *code = exitCode_;
        return true;
    }

    /** Closing stdin is the graceful stop request: the child's stdin watcher sees end-of-file and drains. Idempotent. */
    void closeStdin() {
#if defined(_WIN32)
        if (hIn_) {
            CloseHandle(hIn_);
            hIn_ = nullptr;
        }
#else
        if (inFd_ >= 0) {
            close(inFd_);
            inFd_ = -1;
        }
#endif
    }

    /** Waits up to `ms` for the process to end. True when it has (*code is set). */
    bool waitFor(uint64_t ms, int *code) {
        const uint64_t until = supervisorNowMs() + ms;
        while (!exited(code)) {
            if (supervisorNowMs() >= until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return true;
    }

    void kill() {
        if (!started_ || done_) return;
#if defined(_WIN32)
        TerminateProcess(hProcess_, 1);
#else
        ::kill(pid_, SIGKILL);
#endif
    }

    void joinReader() {
        if (reader_.joinable()) reader_.join();
    }

   private:
    bool started_ = false, done_ = false;
    int exitCode_ = -1;
#if defined(_WIN32)
    HANDLE hProcess_ = nullptr, hIn_ = nullptr, hOut_ = nullptr;

    /** MSVCRT command-line quoting for one argument. */
    static std::string winQuote(const std::string &s) {
        std::string out = "\"";
        for (size_t i = 0; i < s.size(); ++i) {
            size_t bs = 0;
            while (i < s.size() && s[i] == '\\') {
                ++bs;
                ++i;
            }
            if (i == s.size()) {
                out.append(bs * 2, '\\');
                break;
            }
            if (s[i] == '"') {
                out.append(bs * 2 + 1, '\\');
                out += '"';
            } else {
                out.append(bs, '\\');
                out += s[i];
            }
        }
        return out + "\"";
    }

    void readLoop(const LineSink &sink) {
        std::string pending;
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(hOut_, buf, sizeof(buf), &n, nullptr) && n > 0) feedLines(&pending, buf, n, sink);
        if (!pending.empty()) sink(pending);
        CloseHandle(hOut_);
    }
#else
    pid_t pid_ = -1;
    int inFd_ = -1, outFd_ = -1;

    void readLoop(const LineSink &sink) {
        std::string pending;
        char buf[4096];
        ssize_t n = 0;
        while ((n = read(outFd_, buf, sizeof(buf))) > 0 || (n < 0 && errno == EINTR)) {
            if (n > 0) feedLines(&pending, buf, static_cast<size_t>(n), sink);
        }
        if (!pending.empty()) sink(pending);
        close(outFd_);
    }
#endif
    std::thread reader_;
};

/** One GPU as the supervisor knows it at start (from the device list). */
struct WorkerSeed {
    int index = -1;
    std::string uuid, name, pciBus, family;
    int ccMajor = 0, ccMinor = 0;
};

struct SupervisorTuning {
    virtual ~SupervisorTuning() = default;
    virtual void start(uint64_t nowMs, std::vector<std::string> *events) = 0;
    virtual void tick(uint64_t nowMs, std::vector<std::string> *events) = 0;
    virtual void stop(std::vector<std::string> *events) = 0;
};

/** Arguments for one worker: the user's command line without the options the supervisor sets per worker, plus the internal ones. */
inline std::vector<std::string> workerArgs(const std::vector<std::string> &user, const WorkerSeed &w, int port,
                                         std::optional<unsigned> resolvedWait = std::nullopt) {
    std::vector<std::string> out;
    for (size_t i = 0; i < user.size(); ++i) {
        const std::string &a = user[i];
        if (resolvedWait && a == "--work-wait-ms") { ++i; continue; }
        if (a == "--devices" || a == "--device" || a == "--api-port" || a == "--api-bind" || a == "--worker-id") {
            ++i;  // drop the option and its value
            continue;
        }
        if (a == "--no-dashboard" || a == "--worker-child") continue;
        out.push_back(a);
    }
    out.push_back("--devices");
    out.push_back(w.uuid);
    out.push_back("--api-port");
    out.push_back(std::to_string(port));
    out.push_back("--api-bind");
    out.push_back("127.0.0.1");
    out.push_back("--worker-child");
    out.push_back("--worker-id");
    out.push_back(std::to_string(w.index));
    if (resolvedWait) { out.push_back("--work-wait-ms"); out.push_back(std::to_string(*resolvedWait)); }
    return out;
}

struct SupervisorOptions {
    std::string exe;                       // this binary
    std::vector<std::string> userArgs;     // argv[1..] of this process
    std::vector<WorkerSeed> seeds;         // the selected GPUs, in device order
    std::string version, archLabel, poolEndpoint, poolPreset, worker;
    std::string apiBind = "127.0.0.1";
    bool apiEnabled = true;
    int apiPort = 8085;
    int workerPortBase = 8086;             // worker i serves its stats on workerPortBase + i (loopback)
    bool donationOn = true;
    bool donationDestinationConfigured = false;
    uint64_t drainMs = 10000;              // the configured drain; the supervisor waits this long plus 5 s before forcing a stop
    std::vector<std::string> secrets;      // exact strings never printed (wallet, donation account and payout)
    bool dashboard = false;                // TTY dashboard; otherwise plain timestamped lines
    volatile sig_atomic_t *stop = nullptr; // set by the console handler or SIGTERM
    SupervisorTuning *tuning = nullptr;
    std::string uiPreferencesPath;         // optional presentation file; never the mining config
    std::string historyPath;               // optional local installation history; no wallet/device names stored
    std::string runtimePreferencesPath;
    RuntimeContext runtimeContext;
    std::optional<unsigned> resolvedWorkWait;  // frozen for this session, including child restarts
};

/** What the supervisor knows about one worker at one moment. */
struct WorkerView {
    WorkerSeed seed;
    bool haveStats = false;
    StatsSnapshot stats;
    std::string state = "Connecting";
    std::string detail;
    bool telValid = false;
    GpuTelemetry tel;
    // Shares from this GPU's earlier worker runs (before a restart), so session totals never go backwards.
    StatsOutcomes carried, carriedFee;
    unsigned restarts = 0;     // how many times this GPU's worker was restarted
    uint64_t lastSampleMs = 0; // when the stats endpoint last answered (0 = never); the dashboard's data age
    // Session average over mining time only (connecting, restarting and stopped time do not dilute it).
    double minedRateSum = 0;   // sum of rate * seconds while the pool state is mining
    double minedSeconds = 0;
    uint64_t lastRateMs = 0;
};

/** a += b for every counter. */
inline void addOutcomes(StatsOutcomes &a, const StatsOutcomes &b) {
    a.submitted += b.submitted;
    a.acknowledged += b.acknowledged;
    a.accepted += b.accepted;
    a.rejected += b.rejected;
    a.stale += b.stale;
    a.unknown += b.unknown;
    a.pending += b.pending;
    a.hostInvalid += b.hostInvalid;
    a.submitFailed += b.submitFailed;
    a.localStale += b.localStale;
}

/** The public stats document for N workers: one devices[] entry per GPU, summed totals and one donation block. Pure. */
inline StatsSnapshot aggregateSnapshot(const SupervisorOptions &opt, const std::vector<WorkerView> &views, double uptimeSeconds,
                                       const std::string &state) {
    StatsSnapshot s;
    s.version = opt.version;
    s.state = state;
    s.uptimeSeconds = uptimeSeconds;
    s.poolEndpoint = opt.poolEndpoint;
    s.poolPreset = opt.poolPreset;
    s.poolState = "connecting";
    bool anyMining = false;
    StatsFee &f = s.fee;
    f.enabled = opt.donationOn;
    f.destinationConfigured = opt.donationDestinationConfigured;
    f.state = opt.donationOn ? "not_active" : "off";
    f.login = "developer";
    f.note = opt.donationOn ? "developer donation: 1% (99:1 per 100 productive minutes)" : "developer donation is off";
    for (const WorkerView &v : views) {
        StatsDevice d;
        if (v.haveStats && !v.stats.devices.empty()) d = v.stats.devices[0];
        d.index = v.seed.index;
        d.uuid = v.seed.uuid;
        d.pciBus = v.seed.pciBus;
        d.name = v.seed.name;
        d.family = v.seed.family;
        d.ccMajor = v.seed.ccMajor;
        d.ccMinor = v.seed.ccMinor;
        if (v.telValid) {
            d.powerWatts = v.tel.powerMilliwatts / 1000.0;
            d.temperatureC = static_cast<int>(v.tel.temperatureC);
        }
        d.outcomes.pending = v.haveStats ? d.outcomes.pending : 0;  // a stopped run has nothing in flight
        addOutcomes(d.outcomes, v.carried);
        addOutcomes(f.outcomes, v.carriedFee);
        s.devices.push_back(d);
        if (!v.haveStats) continue;
        const StatsSnapshot &c = v.stats;
        if (c.poolState == "mining") anyMining = true;
        if (f.pool.empty()) f.pool = c.fee.pool;
        if (c.fee.state == "donation") f.state = "donation";
        else if (c.fee.state == "user" && f.state != "donation") f.state = "user";
        f.windowActive = f.windowActive || c.fee.windowActive;
        f.cycle = c.fee.cycle > f.cycle ? c.fee.cycle : f.cycle;
        f.userSeconds += c.fee.userSeconds;
        f.feeSeconds += c.fee.feeSeconds;
        f.debtSeconds += c.fee.debtSeconds;
        f.poolFailures += c.fee.poolFailures;
        f.windowsCompleted += c.fee.windowsCompleted;
        addOutcomes(f.outcomes, c.fee.outcomes);
    }
    s.poolState = anyMining ? "mining" : "connecting";
    if (!opt.donationOn) f.state = "off";
    return s;
}

/** The supervisor's dashboard model, from the same views. Holds no wallet and no donation account. */
inline DashModel dashboardModel(const SupervisorOptions &opt, const std::vector<WorkerView> &views, const StatsSnapshot &agg, double uptimeSeconds,
                                uint64_t nowMs, const std::vector<DashEvent> &events, const std::deque<std::string> &log) {
    DashModel m;
    m.version = opt.version;
    m.poolEndpoint = opt.poolEndpoint;
    m.worker = opt.worker;
    m.sessionSeconds = static_cast<uint64_t>(uptimeSeconds);
    m.donationOn = opt.donationOn;
    m.donation = agg.fee.outcomes;
    m.events.assign(events.begin(), events.end());
    m.log.assign(log.begin(), log.end());
    bool anyUp = false, allStopped = !views.empty(), allError = !views.empty();
    for (size_t i = 0; i < views.size(); ++i) {
        const WorkerView &v = views[i];
        const StatsDevice &d = agg.devices[i];
        DashGpu g;
        g.index = v.seed.index;
        g.name = v.seed.name;
        g.state = v.state;
        g.pciBus = v.seed.pciBus;
        g.detail = v.detail;
        g.poolState = v.haveStats ? v.stats.poolState : std::string("-");
        g.restarts = v.restarts;
        // A sample can land after `nowMs` was read: clamp instead of wrapping around.
        if (v.lastSampleMs) g.sampleAgeS = nowMs > v.lastSampleMs ? static_cast<double>(nowMs - v.lastSampleMs) / 1000.0 : 0.0;
        anyUp = anyUp || v.state == "Mining" || v.state == "Donation";
        allStopped = allStopped && v.state == "Stopped";
        allError = allError && v.state == "Error";
        if (v.haveStats && !v.stats.devices.empty()) {
            g.rate = d.rateTmacs;
            g.rateAvg = v.minedSeconds > 0 ? v.minedRateSum / v.minedSeconds : 0.0;
        }
        g.outcomes = d.outcomes;  // includes earlier runs of this GPU's worker
        g.powerW = d.powerWatts;
        g.tempC = d.temperatureC;
        if (v.telValid) {
            g.fanPct = static_cast<int>(v.tel.fanPercent);
            g.coreMhz = v.tel.smClockMhz;
            g.memMhz = v.tel.memClockMhz;
        }
        m.gpus.push_back(g);
    }
    m.connection = anyUp ? "Authorized" : allStopped ? "Stopped" : allError ? "Error" : "Connecting";
    return m;
}

/** Runs the supervised workers until they all stop. Single-threaded except the per-child line readers. */
class Supervisor {
   public:
    explicit Supervisor(SupervisorOptions opt) : opt_(std::move(opt)) {
        for (const std::string &s : opt_.secrets) redactor_.addSecret(s);
    }
    Supervisor(const Supervisor &) = delete;
    Supervisor &operator=(const Supervisor &) = delete;

    /** Returns kExitOk when every worker stopped on request, kExitRuntime when a worker could not be kept running. */
    int run() {
        if (opt_.stop && *opt_.stop) return kExitOk;
        sessionToken_ = intent_.explicitStart();
        if (!intent_.permits(sessionToken_)) return kExitFaultLatched;
        const uint64_t t0 = supervisorNowMs();
        if (!opt_.uiPreferencesPath.empty() && opt_.dashboard) {
            uiStore_.reset(new UiPreferencesStore(std::filesystem::u8path(opt_.uiPreferencesPath)));
            std::string error;
            if (!uiStore_->load(&ui_, &error)) addEvent("UI preferences unavailable: " + error);
        }
        if (!opt_.runtimePreferencesPath.empty()) runtimeStore_.reset(new RuntimePreferencesStore(std::filesystem::u8path(opt_.runtimePreferencesPath)));
        if (!opt_.historyPath.empty()) history_.reset(new AcceptedHistory(std::filesystem::u8path(opt_.historyPath)));
        for (size_t i = 0; i < opt_.seeds.size(); ++i) {
            std::unique_ptr<Worker> w(new Worker());
            w->view.seed = opt_.seeds[i];
            w->port = opt_.workerPortBase + static_cast<int>(i);
            w->nvmlOk = w->nvml.openByUuid(opt_.seeds[i].uuid);
            workers_.push_back(std::move(w));
        }
        if (opt_.tuning) {
            std::vector<std::string> events;
            opt_.tuning->start(t0, &events);
            for (const std::string &event : events) addEvent(event);
        }
        for (size_t i = 0; i < workers_.size(); ++i) startWorker(i, t0);

        StatsServer server;
        if (opt_.apiEnabled) {
            std::string err;
            if (!server.start(opt_.apiBind, opt_.apiPort, [this] { return statsToJson(publisher_.snapshot()); },
                              versionToJson(opt_.version, opt_.archLabel), &err))
                addEvent("stats endpoint disabled: " + err);
        }

        const bool tty = opt_.dashboard;
        if (tty) {
            enableAnsiOnWindows();
            printf("\x1b[2J\x1b[?25l");  // hide the cursor while the dashboard owns the screen
        }
        KeyReader keys;
        bool stopping = false, forced = false, quit = false;
        uint64_t stopDeadline = 0, nextSummary = 0;
        for (;;) {
            const uint64_t now = supervisorNowMs();
            if (tty) {
                for (int n = 0; n < 32; ++n) {
                    const int key = keys.poll();
                    if (!key) break;
                    view_.options = ui_;  // a saved page switch also governs the very next queued shortcut
                    if (settings_.visible) {
                        const auto result = settings_.key(key, [this](const UiPreferences &p, std::string *error) {
                            if (uiStore_ && !uiStore_->save(p, error)) return false;
                            ui_ = p;
                            return true;
                        }, [this](const RuntimePreferences &p, std::string *error) {
                            return runtimeStore_ && runtimeStore_->save(p, error);
                        });
                        if (result == SettingsResult::OpenRuntime) openRuntimeControls(&settings_, runtimeStore_.get(), opt_.runtimeContext);
                        if (result == SettingsResult::Quit) {
                            settings_.visible = false;
                            nextFrame_ = 0;
                            quit = true;
                        }
                    } else if (key == 'q' || key == 'Q' || key == 3) quit = true;
                    else if (key == 's' || key == 'S') {
                        std::string error;
                        UiPreferences loaded;
                        if (uiStore_ && uiStore_->load(&loaded, &error)) ui_ = loaded;
                        settings_.open(ui_);
                        if (!error.empty()) settings_.message = error;
                    } else handleDashKey(view_, key);
                    if (quit) break;
                }
            }
            const bool stopRequested = quit || (opt_.stop && *opt_.stop);
            if (!stopping && stopRequested) {
                beginStop();
                stopping = true;
                stopDeadline = now + opt_.drainMs + 5000;
            }
            drainInbox(tty);
            if (opt_.tuning) {
                std::vector<std::string> events;
                opt_.tuning->tick(now, &events);
                for (const std::string &event : events) addEvent(event);
            }
            if (!stopping) {
                for (size_t i = 0; i < workers_.size(); ++i) {
                    Worker &w = *workers_[i];
                    if (w.restartPending && now >= w.restartAt) startWorker(i, now);
                }
            }
            bool alive = false, pending = false;
            for (size_t i = 0; i < workers_.size(); ++i) {
                Worker &w = *workers_[i];
                if (w.proc && w.proc->started() && !w.proc->finished()) {
                    if (now >= w.nextSampleAt) {
                        sample(i);
                        w.nextSampleAt = now + (tty ? 500 : 250);
                    }
                    int code = 0;
                    if (w.proc->exited(&code)) {
                        w.proc->joinReader();
                        onExit(i, code, now, stopping || stopRequested);
                    } else {
                        alive = true;
                    }
                }
                pending = pending || w.restartPending;
            }
            if (stopping) {
                if (!alive) break;
                if (now >= stopDeadline) {
                    forced = true;
                    for (auto &w : workers_)
                        if (w->proc && w->proc->started() && !w->proc->finished()) w->proc->kill();
                    addEvent("drain deadline passed: remaining workers were stopped");
                    break;
                }
            } else if (!alive && !pending) {
                addEvent("no GPU is mining: stopping");
                break;
            }
            publish(now, t0, stopping ? "draining" : "running");
            frame(now, t0, tty);
            if (!tty && now >= nextSummary) {
                nextSummary = now + 15000;
                printf("%s\n", renderPlainSummary(buildModel(now, t0), time(nullptr)).c_str());
                fflush(stdout);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(tty ? 50 : 250));
        }
        for (auto &w : workers_) {
            if (!w->proc) continue;
            int code = 0;
            if (w->proc->started() && !w->proc->finished()) w->proc->waitFor(2000, &code);
            w->proc->joinReader();
        }
        // A poll cannot prove that no acknowledgement arrived after the last snapshot.
        // Persist an uncertain tail even for graceful exits instead of claiming complete history.
        if (history_) for (size_t i = 0; i < workers_.size(); ++i)
            history_->markGap(static_cast<unsigned>(i), workers_[i]->historyLastUtc);
        if (forced) addEvent("workers were stopped by force");
        if (opt_.tuning) {
            std::vector<std::string> events;
            opt_.tuning->stop(&events);
            for (const std::string &event : events) addEvent(event);
        }
        if (history_) history_->stop();  // worker drains/safety stop complete before optional persistence shutdown
        publish(supervisorNowMs(), t0, "stopped");
        if (tty) printf("\x1b[2J\x1b[H\x1b[?25h");  // and give the cursor back
        frame(supervisorNowMs(), t0, tty);
        server.stop();
        for (auto &w : workers_) w->nvml.close();
        bool failed = false;
        for (auto &w : workers_) failed = failed || w->view.state == "Error";
        const std::string summary = "stopped: " + std::to_string(workers_.size()) + " GPU(s), session " +
                                    std::to_string((supervisorNowMs() - t0) / 1000) + " s";
        if (tty) printf("%s\n", summary.c_str());
        else printLine(summary);
        if (failed) intent_.stop();
        return forced ? kExitShutdownForced : failed && !quit && !(opt_.stop && *opt_.stop) ? kExitFaultLatched : kExitOk;
    }

   private:
    struct Worker {
        WorkerView view;
        int port = 0;
        std::unique_ptr<ChildProcess> proc;
        GuardRestartBudget budget;
        Nvml nvml;
        bool nvmlOk = false;
        bool restartPending = false;
        bool stopping = false;
        uint64_t restartAt = 0;
        uint64_t nextSampleAt = 0;
        uint64_t historyGeneration = 0;
        uint64_t historyCounter = 0;
        int64_t historyLastUtc = 0;
        bool historySeen = false, historyOutage = false;
        unsigned restarts = 0;
        // last observed values, for the events
        bool seen = false;
        StatsOutcomes lastOutcomes, lastFeeOutcomes;
        std::string lastPool, lastFee;
        uint64_t windowAccepted = 0;
    };

    SupervisorOptions opt_;
    Redactor redactor_;
    StatsPublisher publisher_;
    std::mutex mu_;
    std::vector<std::pair<size_t, std::string>> inbox_;  // (worker, line) from the readers, guarded by mu_
    std::vector<DashEvent> events_;
    std::deque<std::string> log_;
    std::deque<DashLogEntry> logEntries_;
    DashView view_;  // page, scroll and terminal size of the dashboard
    UiPreferences ui_;
    std::unique_ptr<UiPreferencesStore> uiStore_;
    std::unique_ptr<RuntimePreferencesStore> runtimeStore_;
    SettingsPanel settings_;
    uint64_t nextFrame_ = 0;
    std::unique_ptr<AcceptedHistory> history_;
    SessionIntent intent_;
    SessionToken sessionToken_;
    std::vector<std::unique_ptr<Worker>> workers_;  // declared after mu_ and inbox_: workers (and their readers) go first

    static std::string workerLabel(const Worker &w) { return "GPU " + std::to_string(w.view.seed.index); }

    void addEvent(const std::string &text) {
        events_.push_back({utcClock(time(nullptr)), text});
        if (events_.size() > 200) events_.erase(events_.begin());
        if (!opt_.dashboard) printLine(text);  // plain mode: restarts, connection changes and donation windows are log lines too
    }

    void printLine(const std::string &text) {
        if (!opt_.dashboard) printf("[%s] %s\n", utcClock(time(nullptr)).c_str(), text.c_str());
        fflush(stdout);
    }

    /** Starts (or restarts) worker i. A spawn failure counts as an unexpected exit. */
    void startWorker(size_t i, uint64_t now) {
        // Recheck signals here as well: one can arrive between loop polling and a due retry/first spawn.
        if (opt_.stop && *opt_.stop) intent_.stop();
        if (!intent_.permits(sessionToken_)) return;
        Worker &w = *workers_[i];
        w.restartPending = false;
        ++w.historyGeneration;
        w.historySeen = w.historyOutage = false;
        w.historyLastUtc = static_cast<int64_t>(time(nullptr));
        w.proc.reset(new ChildProcess());
        const size_t slot = i;
        std::string err;
        const bool ok = w.proc->start(
            opt_.exe, workerArgs(opt_.userArgs, w.view.seed, w.port, opt_.resolvedWorkWait),
            [this, slot](const std::string &line) {
                std::lock_guard<std::mutex> g(mu_);
                inbox_.push_back({slot, line});
            },
            &err);
        w.view.haveStats = false;
        w.view.state = "Connecting";
        w.view.detail.clear();
        if (!ok) {
            onExit(i, -1, now, false, err);
            return;
        }
        addEvent(workerLabel(w) + ": worker started" + (w.restarts ? " (restart " + std::to_string(w.restarts) + ")" : std::string()));
    }

    /** Asks every running worker to stop: closing stdin starts each child's own drain. */
    void beginStop() {
        intent_.stop();  // every pending result/retry from this epoch loses permission before draining
        addEvent("stopping: draining every GPU");
        for (auto &w : workers_) {
            w->restartPending = false;
            w->stopping = true;
            if (w->proc && !w->proc->finished()) w->proc->closeStdin();
        }
    }

    /** A worker ended: a stop is recorded; anything else is restarted after 10 s while the 3-per-hour budget allows it. */
    void onExit(size_t i, int code, uint64_t now, bool stopping, const std::string &spawnError = std::string()) {
        Worker &w = *workers_[i];
        if (stopping || w.stopping) {
            w.view.state = "Stopped";
            w.view.detail = "stopped (exit " + std::to_string(code) + ")";
            addEvent(workerLabel(w) + ": stopped (exit " + std::to_string(code) + ")");
            return;
        }
        if (history_) history_->markGap(static_cast<unsigned>(i), w.historyLastUtc);
        // Keep what this run earned: the next run's counters start at zero.
        StatsOutcomes done = w.lastOutcomes;
        done.pending = 0;
        addOutcomes(w.view.carried, done);
        addOutcomes(w.view.carriedFee, w.lastFeeOutcomes);
        w.lastOutcomes = StatsOutcomes{};
        w.lastFeeOutcomes = StatsOutcomes{};
        w.seen = false;
        w.view.haveStats = false;
        w.view.detail = spawnError.empty() ? "exited with code " + std::to_string(code) : spawnError;
        if (code == kExitFaultLatched) {
            w.view.state = "Error";
            w.view.detail += "; worker fault is latched; explicit new start required";
            addEvent(workerLabel(w) + ": " + w.view.detail);
            return;
        }
        if (!w.budget.admit(now)) {
            w.view.state = "Error";
            w.view.detail += "; restart budget spent (3 per hour), this GPU is stopped";
            addEvent(workerLabel(w) + ": " + w.view.detail);
            return;
        }
        w.restarts++;
        w.view.restarts = w.restarts;
        w.restartPending = true;
        w.restartAt = now + GuardRestartBudget::kDelayMs;
        w.view.state = "Restarting";
        addEvent(workerLabel(w) + ": " + w.view.detail + "; restart in 10 s (restart " + std::to_string(w.restarts) + ")");
    }

    /** Polls the worker's stats endpoint and NVML, and records the events its counters imply. */
    void sample(size_t i) {
        Worker &w = *workers_[i];
        bool historySample = false;
        int status = 0;
        std::string body, err;
        if (httpGetLocal("127.0.0.1", w.port, "/api/v1/stats", &status, &body, &err, 1) && status == 200) {
            JsonValue root;
            StatsSnapshot snap;
            if (parseJsonStrict(body, &root, &err) && statsFromJson(root, &snap, &err)) {
                uint64_t count = 0;
                if (history_ && acceptedHistoryCounter(root, &count)) {
                    historySample = true;
                    const auto utc = static_cast<int64_t>(time(nullptr));
                    if (!w.historySeen || w.historyOutage || count != w.historyCounter)
                        history_->observe(static_cast<unsigned>(i), w.historyGeneration, count, utc);
                    w.historyCounter = count;
                    w.historySeen = true;
                    w.historyOutage = false;
                    w.historyLastUtc = utc;
                }
                observe(w, snap);
                w.view.stats = snap;
                w.view.haveStats = true;
                w.view.lastSampleMs = supervisorNowMs();
            }
        }
        if (history_ && !historySample && w.historySeen && !w.historyOutage) {
            history_->markGap(static_cast<unsigned>(i), w.historyLastUtc);
            w.historyOutage = true;
        }
        if (w.nvmlOk) {
            w.view.tel = w.nvml.sample();
            w.view.telValid = w.view.tel.valid;
        }
        if (w.view.haveStats) {
            const bool donating = w.view.stats.fee.state == "donation";
            const uint64_t t = supervisorNowMs();
            const bool producing = w.view.stats.poolState == "mining";
            // Kernel warm-up reports a zero rate while the pool is already connected: those samples do not count.
            if (producing && w.view.lastRateMs && t > w.view.lastRateMs && !w.view.stats.devices.empty() &&
                w.view.stats.devices[0].rateTmacs > 0) {
                const double dt = static_cast<double>(t - w.view.lastRateMs) / 1000.0;
                w.view.minedRateSum += w.view.stats.devices[0].rateTmacs * dt;
                w.view.minedSeconds += dt;
            }
            w.view.lastRateMs = producing ? t : 0;
            w.view.state = donating ? "Donation" : (w.view.stats.poolState == "mining" ? "Mining" : "Connecting");
            w.view.detail = "pci " + w.view.seed.pciBus + "  pool " + w.view.stats.poolState;
        }
    }

    void observe(Worker &w, const StatsSnapshot &snap) {
        const std::string label = workerLabel(w);
        const StatsOutcomes now = snap.devices.empty() ? StatsOutcomes{} : snap.devices[0].outcomes;
        if (w.seen) {
            if (now.accepted > w.lastOutcomes.accepted)
                addEvent(label + ": accepted +" + std::to_string(now.accepted - w.lastOutcomes.accepted) + " share(s)");
            if (w.lastPool == "mining" && snap.poolState != "mining") addEvent(label + ": pool connection lost, reconnecting");
            if (w.lastPool != "mining" && snap.poolState == "mining" && w.lastPool != "") addEvent(label + ": connected to the pool");
        }
        if (snap.fee.state == "donation" && w.lastFee != "donation") {
            addEvent(label + ": donation window start");
            w.windowAccepted = snap.fee.outcomes.accepted;
        }
        if (snap.fee.state != "donation" && w.lastFee == "donation") {
            const uint64_t got = snap.fee.outcomes.accepted >= w.windowAccepted ? snap.fee.outcomes.accepted - w.windowAccepted : 0;
            addEvent(label + ": donation window end (+" + std::to_string(got) + " accepted)");
        }
        w.lastFee = snap.fee.state;
        w.lastPool = snap.poolState;
        w.lastOutcomes = now;
        w.lastFeeOutcomes = snap.fee.outcomes;
        w.seen = true;
    }

    /** Child lines: redacted, kept for [L] and the plain log. Dashboard mode keeps them out of the event list. */
    void drainInbox(bool tty) {
        std::vector<std::pair<size_t, std::string>> lines;
        {
            std::lock_guard<std::mutex> g(mu_);
            lines.swap(inbox_);
        }
        for (const auto &entry : lines) {
            const std::string text = redactor_.apply(entry.second);
            const std::string tagged = "GPU " + std::to_string(workers_[entry.first]->view.seed.index) + ": " + text;
            log_.push_back(tagged);
            if (log_.size() > 200) log_.pop_front();
            const auto observedAt = time(nullptr);
            logEntries_.push_back({tagged, utcClock(observedAt), utcIsoClock(observedAt), DashLogSeverity::Unclassified});
            if (logEntries_.size() > 200) logEntries_.pop_front();
            if (!tty) printLine(tagged);
        }
    }

    void publish(uint64_t now, uint64_t t0, const std::string &state) {
        std::vector<WorkerView> views;
        for (auto &w : workers_) views.push_back(w->view);
        const StatsSnapshot agg = aggregateSnapshot(opt_, views, static_cast<double>(now - t0) / 1000.0, state);
        publisher_.update([&](StatsSnapshot *s) { *s = agg; });
    }

    DashModel buildModel(uint64_t now, uint64_t t0) {
        std::vector<WorkerView> views;
        for (auto &w : workers_) views.push_back(w->view);
        const double up = static_cast<double>(now - t0) / 1000.0;
        const StatsSnapshot agg = aggregateSnapshot(opt_, views, up, "running");
        DashModel model = dashboardModel(opt_, views, agg, up, now, events_, log_);
        model.logEntries.assign(logEntries_.begin(), logEntries_.end());
        if (history_) model.history = history_->snapshot();
        if (history_) for (const auto &worker : workers_) if (!worker->historySeen || worker->historyOutage) model.history.gaps = true;
        return model;
    }

    void frame(uint64_t now, uint64_t t0, bool tty) {
        if (!tty) return;
        if (now < nextFrame_) return;
        nextFrame_ = now + (settings_.visible ? 50 : ui_.refreshMs);
        const DashModel m = buildModel(now, t0);
        view_.columns = terminalColumns(80);
        view_.rows = terminalRows(24);
        view_.color = ui_.color ? detectColorLevel() : ColorLevel::None;
        view_.showEvents = ui_.showEvents;
        view_.hideWorker = ui_.hideWorker;
        view_.options = ui_;
        dash_detail::g_dashBoxRules = !g_asciiOnly;
        // Erase to the end of every line: a shorter line must not leave the previous frame's characters behind.
        const std::string text = settings_.visible ? settings_.render(view_.columns, view_.rows, view_.color) : renderDashboard(m, view_);
        std::string out;
        out.reserve(text.size() + 256);
        for (char ch : text) {
            if (ch == '\n') out += "\x1b[K";
            out += ch;
        }
        printf("\x1b[H%s\x1b[K\x1b[J", out.c_str());
        fflush(stdout);
    }
};

}  // namespace om
