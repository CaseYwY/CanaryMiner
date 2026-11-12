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
#include <array>
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
#include "preferences_settings.h"
#include "forecast_owner.h"
#include "market_reference_sink.h"
#include "energy_owner.h"
#include "energy_capture.h"
#include "session_intent.h"
#include "worker_fleet.h"
#include "accepted_history.h"
#include "stats.h"
#include "stats_server.h"
#include "supervisor_sampling.h"
#include "legacy_child_process.h"
#include "executable_path.h"
#include "console_output.h"

namespace om {

inline uint64_t supervisorNowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline GameGuardTime supervisorControlTime(){
    return {std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),supervisorNowMs()};
}

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
                                         std::optional<unsigned> resolvedWait = std::nullopt,const std::string &resolvedPoolTlsUrl = {},
                                         const std::string &resolvedPoolFallbackUrl = {}) {
    std::vector<std::string> out;
    for (size_t i = 0; i < user.size(); ++i) {
        const std::string &a = user[i];
        if (resolvedWait && a == "--work-wait-ms") { ++i; continue; }
        if (!resolvedPoolTlsUrl.empty() && (a == "--worker-pool-tls" || a == "--worker-pool-fallback")) { ++i; continue; }
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
    if (!resolvedPoolTlsUrl.empty()) { out.push_back("--worker-pool-tls"); out.push_back(resolvedPoolTlsUrl); out.push_back("--no-donation"); }
    if (!resolvedPoolTlsUrl.empty() && !resolvedPoolFallbackUrl.empty()) { out.push_back("--worker-pool-fallback"); out.push_back(resolvedPoolFallbackUrl); }
    return out;
}

struct SupervisorOptions {
    std::string exe;                       // this binary
    std::vector<std::string> userArgs;     // argv[1..] of this process
    std::vector<WorkerSeed> seeds;         // the selected GPUs, in device order
    std::string version, archLabel, poolEndpoint, poolPreset, worker;
    std::string startupNotice;              // memory-only ordinary banner; displayed by the same owned output role
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
    std::string manualMarketPreferencesPath;
    ManualMarketFactory manualMarketFactory;
    MarketReferenceFactory marketReferenceFactory; // default empty; explicit caller-owned source/consent/qualification
    bool noMarket = false;
    std::string electricityPreferencesPath;
    SessionEnergyFactory energyFactory;
    EnergyCaptureFactory energyCaptureFactory; // only an independently authorized, privately bound plan may supply this
    EnergyRuntimeFactory energyRuntimeFactory; // explicit development capability; owns both controls and fenced capture
    ForecastSinkFactory forecastFactory; // empty until a source-qualified caller supplies read/scenario ownership
    BillingReviewAccess billingReviewAccess; // default empty; borrowed owner must outlive this supervisor
    bool noEnergy = false;
    std::optional<unsigned> resolvedWorkWait;  // frozen for this session, including child restarts
    std::string resolvedPoolTlsUrl; // frozen user peer; no saved preference reload in workers
    std::string resolvedPoolFallbackUrl; // frozen consent for this exact TCP endpoint, or empty
    WorkerFleetFactory workerFleetFactory; // default empty; one fixed lifetime, never reconstructed on a worker retry
    WorkerFleetEvidenceSource workerFleetEvidence; // trusted memory-only source; missing evidence holds every controlled worker
    DisplaySampleFactory displaySampleFactory; // empty uses native display reader; constructed/read/destroyed only in the background
    std::string gamePreferencesPath;
    std::string poolSecurityPreferencesPath;
    std::string languagePreferencesPath;
    LanguageSettingsContext languageContext;
    GameSettingsContext gameContext; // frozen effective settings, not inferred from later saved preferences
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
    DisplaySampleTime telObserved;
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
            d.powerWatts = v.tel.powerValid ? v.tel.powerMilliwatts / 1000.0 : -1;
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

/** Owns worker control/views; line readers and display HTTP/NVML run on separate lifetime threads. */
class Supervisor {
   public:
    explicit Supervisor(SupervisorOptions opt) : opt_(std::move(opt)) {
        settings_.billingAccess=opt_.billingReviewAccess;
        for (const std::string &s : opt_.secrets) redactor_.addSecret(s);
    }
    Supervisor(const Supervisor &) = delete;
    Supervisor &operator=(const Supervisor &) = delete;

    /** Returns kExitOk when every worker stopped on request, kExitRuntime when a worker could not be kept running. */
    int run() {
        if (opt_.stop && *opt_.stop) {settings_.stopBillingReview();return kExitOk;}
        const auto nativeEpoch=nativeProcessEpoch();
        sessionToken_ = intent_.explicitStart();
        if (!intent_.permits(sessionToken_)) return kExitFaultLatched;
        if(opt_.workerFleetFactory&&(opt_.tuning||opt_.drainMs>60000)){intent_.stop();settings_.stopBillingReview();return kExitFaultLatched;}
        const bool tty=opt_.dashboard;
        if(tty)enableAnsiOnWindows();
        if(!startOutput()){intent_.stop();settings_.stopBillingReview();return opt_.stop&&*opt_.stop?kExitOk:kExitFaultLatched;}
        const uint64_t t0 = supervisorNowMs();
        startupUtc_=static_cast<uint64_t>(time(nullptr)); // paired original activation anchor; helper delay never renews it
        for (size_t i = 0; i < opt_.seeds.size(); ++i) {
            std::unique_ptr<Worker> w(new Worker());
            w->view.seed = opt_.seeds[i];
            w->port = opt_.workerPortBase + static_cast<int>(i);
            workers_.push_back(std::move(w));
        }
        startPreferences(); // finite asynchronous initial reads; no synchronous bootstrap/live fallback

        StatsServer server;
        if (opt_.apiEnabled) {
            std::string err;
            if (!server.start(opt_.apiBind, opt_.apiPort, [this] { return statsToJson(publisher_.snapshot()); },
                              versionToJson(opt_.version, opt_.archLabel), &err))
                addEvent("stats endpoint disabled: " + err);
        }

        KeyReader keys;
        bool stopping = false, forced = false, quit = false, controlFault = false;
        uint64_t stopDeadline = 0, nextSummary = 0, nextSample = 0;
        bool samplingFaultReported=false;
        const auto latchNativeFault=[&]{
            if(nativeProcessEpochCurrent(nativeEpoch))return;
            controlFault=true;
            if(!stopping){beginStop();stopping=true;stopDeadline=supervisorNowMs()+opt_.drainMs+5000;
                addEvent("Native process ownership changed; explicit new session required.");}
        };
        for (;;) {
            const uint64_t now = supervisorNowMs();
            latchNativeFault();
            if(!stopping&&!(opt_.stop&&*opt_.stop)){
                if(bootstrapPending_)updatePreferenceBootstrap();
                else if(sessionActivated_)updatePreferences();
            }
            if (tty) {
                for (int n = 0; n < 32; ++n) {
                    if(stopping||(opt_.stop&&*opt_.stop))break;
#if defined(_WIN32)
                    const int key=output_->pollKey();
#else
                    const int key=keys.poll();
#endif
                    if (!key) break;
                    view_.options = ui_;  // a saved page switch also governs the very next queued shortcut
                    if(!sessionActivated_){
                        if(key=='q'||key=='Q'||key==3)quit=true;
                        else if(key=='s'||key=='S'){settingsRequested_=true;nextFrame_=0;}
                        else handleDashKey(view_,key);
                    }else if (settings_.visible) {
                        settings_.updateCreditReview();
                        settings_.updateEnergyReview();
                        settings_.updateEnergyAuthorization();
                        settings_.updateBillingReview();
                        if(preferencesSettings_)preferencesSettings_->context=preferencesContext();
                        const auto result=preferencesSettings_?preferencesSettings_->key(key,static_cast<uint64_t>(time(nullptr)),now):settings_.key(key,{});
                        if(!preferencesSettings_)settings_.message="Background preferences unavailable; no synchronous file fallback or Save. Worker controls unchanged.";
                        if (result == SettingsResult::Quit) {
                            settings_.visible = false;
                            nextFrame_ = 0;
                            quit = true;
                        }
                    } else if (key == 'q' || key == 'Q' || key == 3) quit = true;
                    else if (key == 'r' || key == 'R') {
                        if (!stopping && fleet_) {
                            const bool changed = fleet_->requestForceResume();
                            addEvent(changed ? "force resume toggled for the current game/economic pause"
                                             : "force resume unavailable: no known game/economic pause");
                        } else if (!stopping) {
                            addEvent("force resume unavailable: no controlled worker fleet");
                        }
                    }
                    else if (key == 's' || key == 'S') {
                        openPreferencesSettings();
                    } else handleDashKey(view_, key);
                    if (quit) break;
                }
            }
            const bool stopRequested = quit || (opt_.stop && *opt_.stop);
            latchNativeFault();
            if (!stopping && stopRequested) {
                beginStop();
                stopping = true;
                stopDeadline = now + opt_.drainMs + 5000;
            }
            const auto display=output_->snapshot();
            // Input-only polls cannot stand in for acceptance of the original startup display bytes.
            const bool outputPending=display.state==ConsoleOutputState::Starting||
                (display.state==ConsoleOutputState::Ready&&!display.outputAccepted);
            if(!stopping&&!outputPending&&display.state!=ConsoleOutputState::Ready){
                controlFault=true;beginStop();stopping=true;stopDeadline=now+opt_.drainMs+5000;
            }
            if(!stopping&&!outputPending&&!bootstrapPending_&&!sessionActivated_){
                sessionActivated_=true; // one attempted activation, never a retry after a callback/factory failure
                if(!activateSession(t0,nativeEpoch)){
                    controlFault=controlFault||!(opt_.stop&&*opt_.stop);
                    beginStop();stopping=true;stopDeadline=supervisorNowMs()+opt_.drainMs+5000;
                }else{
                    nextFrame_=0; // a pending startup screen must not delay the first activated dashboard
                    if(settingsRequested_&&tty){settingsRequested_=false;openPreferencesSettings();}
                }
                latchNativeFault();
                if(!stopping&&opt_.stop&&*opt_.stop){beginStop();stopping=true;stopDeadline=supervisorNowMs()+opt_.drainMs+5000;}
            }
            if(fleet_){
                if(!stopping){WorkerFleetEvidence evidence;
                    try{if(opt_.workerFleetEvidence)evidence=opt_.workerFleetEvidence(sessionToken_.epoch);}catch(...){controlFault=true;beginStop();stopping=true;stopDeadline=supervisorNowMs()+opt_.drainMs+5000;}
                    latchNativeFault();
                    if(!stopping&&opt_.stop&&*opt_.stop){beginStop();stopping=true;stopDeadline=supervisorNowMs()+opt_.drainMs+5000;}
                    if(!stopping)fleet_->tick(supervisorControlTime(),evidence);
                    latchNativeFault();
                    if(!stopping&&fleet_->state()==WorkerFleetState::Faulted){controlFault=true;beginStop();stopping=true;stopDeadline=supervisorNowMs()+opt_.drainMs+5000;}}
                for(size_t i=0;i<workers_.size();++i)for(auto &line:fleet_->takeLines(static_cast<unsigned>(i)))inbox_.emplace_back(i,std::move(line));
            }
            drainInbox(tty);
            if (tuningStarted_) {
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
            const auto sampled=stopping?std::shared_ptr<const DisplaySampleBatch>{}:sampling_.take();
            if(sampled)nextSample=now+(tty?500:250);
            std::vector<uint64_t> sampleGenerations(workers_.size());
            for (size_t i = 0; i < workers_.size(); ++i) {
                Worker &w = *workers_[i];
                if(w.view.telValid&&!displaySampleInterval(w.view.telObserved,displaySampleTime(),2000))w.view.telValid=false;
                if(fleet_){
                    const auto control=fleet_->snapshot(static_cast<unsigned>(i));
                    if(control.ownershipUnresolved)controlFault=true;
                    if(control.requested&&!control.done){alive=true;
                        if(!stopping){sampleGenerations[i]=w.historyGeneration;
                            if(sampled&&sampled->generations[i]==w.historyGeneration)sample(i,sampled->readings[i]);}
                        if(control.pauseRequested){w.view.state=control.paused?"Paused":"Pausing";w.view.detail=control.paused?"controlled work boundary held":"waiting for drained work boundary";w.view.lastRateMs=0;}
                    }else if(control.done&&!w.controlledExitHandled){w.controlledExitHandled=true;
                        int code=control.exitCode;
                        if(control.ownershipUnresolved||(!stopping&&!stopRequested&&code!=0&&code!=7&&code!=8&&!control.retryable))code=kExitFaultLatched;
                        forced=forced||code==kExitShutdownForced;
                        onExit(i,code,now,stopping||stopRequested);}
                }else if (w.proc && w.proc->started() && !w.proc->finished()) {
                    if(!stopping){sampleGenerations[i]=w.historyGeneration;
                        if(sampled&&sampled->generations[i]==w.historyGeneration)sample(i,sampled->readings[i]);}
                    int code = 0;
                    if (w.proc->exited(&code)) {
                        w.proc->joinReader();
                        onExit(i, code, now, stopping || stopRequested);
                        sampleGenerations[i]=0;
                    } else {
                        alive = true;
                    }
                }
                pending = pending || w.restartPending;
            }
            latchNativeFault();
            if(energyCapture_&&intent_.permits(sessionToken_)){
                const auto utc=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                // A single ordered batch must be consumed before advancing the capture delivery clock.
                // Otherwise delayed original power timestamps would look like a clock rollback. Never restamp them.
                if(!sampling_.pending())energyCapture_->tick(utc,supervisorNowMs());
                settings_.updateEnergyReview();
                settings_.updateEnergyAuthorization();
                settings_.updateBillingReview();
            }
            latchNativeFault();
            if (stopping) {
                if (!alive) break;
                if (now >= stopDeadline) {
                    forced = true;
                    for (auto &w : workers_)
                        if (w->proc && w->proc->started() && !w->proc->finished()) w->proc->kill();
                    addEvent("drain deadline passed: remaining workers were stopped");
                    break;
                }
            } else if (!alive && !pending && !bootstrapPending_ && !outputPending) {
                addEvent("no GPU is mining: stopping");
                break;
            }
            if(sessionActivated_&&!stopping&&intent_.permits(sessionToken_)&&!(opt_.stop&&*opt_.stop)){
                if(sampling_.faulted()&&!samplingFaultReported){samplingFaultReported=true;
                    if(energyCapture_)energyCapture_->markGap(sessionToken_.epoch);
                    for(auto &w:workers_){w->view.telValid=false;w->view.lastRateMs=0;}
                    addEvent("Background display sampling stopped; prior observations retain their original age.");}
                if(now>=nextSample&&!sampling_.pending())sampling_.request(sampleGenerations);
            }
            publish(now, t0, stopping ? "draining" : sessionActivated_ ? "running" : "starting");
            if(!stopping)frame(now,t0,tty);
            if(!stopping&&!tty&&sessionActivated_&&now>=nextSummary){
                nextSummary = now + 15000;
                output_->submit(renderPlainSummary(buildModel(now,t0),time(nullptr))+"\n");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(tty||fleet_ ? 50 : 250));
        }
        if(!stopping)beginStop(); // natural/faulted worker exit revokes display before any optional join too
        settings_.stopBillingReview();
        if(energyCapture_)energyCapture_->requestStop();
        if(forecast_)forecast_->requestStop();
        if(marketReference_){marketReferenceStarted_=false;marketReference_->requestStop();}
        if(fleet_)fleet_->requestStop();
        sampling_.requestStop();
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
        if (tuningStarted_) {
            std::vector<std::string> events;
            opt_.tuning->stop(&events);
            for (const std::string &event : events) addEvent(event);
        }
        if (history_) history_->stop();  // worker drains/safety stop complete before optional persistence shutdown
        if (energy_) energy_->stop();
        if (energyCapture_) energyCapture_->stop();
        if (forecast_) forecast_->stop();
        if(fleet_){fleet_->stop();for(unsigned i=0;i<workers_.size();++i){const auto control=fleet_->snapshot(i);
            controlFault=controlFault||control.ownershipUnresolved;forced=forced||control.exitCode==kExitShutdownForced;}}
        if (marketReference_) marketReference_->stop(); // optional data joining cannot delay controlled-worker settlement
        if(preferences_)preferences_->stop(); // optional file/helper joining only after worker drain/settlement
        publish(supervisorNowMs(), t0, "stopped");
        server.stop();
        sampling_.stop(); // native reader teardown joins only after worker control/ordinary drains
        bool failed = false;
        for (auto &w : workers_) failed = failed || w->view.state == "Error";
        output_->stopSupervisor(workers_.size(),(supervisorNowMs()-t0)/1000,tty,controlFault||forced||failed);
        const auto finalOutput=output_->snapshot();
        controlFault=controlFault||finalOutput.state==ConsoleOutputState::Faulted||!finalOutput.settled;
        if (failed) intent_.stop();
        controlFault=controlFault||!nativeProcessEpochCurrent(nativeEpoch); // late optional-helper settlement cannot erase the original fault epoch
        return controlFault ? kExitFaultLatched : forced ? kExitShutdownForced : failed && !quit && !(opt_.stop && *opt_.stop) ? kExitFaultLatched : kExitOk;
    }

   private:
    struct Worker {
        WorkerView view;
        int port = 0;
        std::unique_ptr<ChildProcess> proc;
        GuardRestartBudget budget;
        bool restartPending = false;
        bool stopping = false;
        bool controlledExitHandled=false;
        uint64_t restartAt = 0;
        uint64_t nextEnergyAt = 0;
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
    ManualMarketPreferences manualEffective_;
    ManualMarketProjector manualProjection_;
    std::unique_ptr<MarketReferenceSink> marketReference_;
    bool marketReferenceStarted_=false;
    ElectricityPreferences electricityEffective_;
    std::unique_ptr<SessionEnergySink> energy_;
    std::unique_ptr<EnergyCaptureSink> energyCapture_;
    std::unique_ptr<ForecastSink> forecast_;
    SettingsPanel settings_;
    uint64_t nextFrame_ = 0;
    std::unique_ptr<AcceptedHistory> history_;
    SessionIntent intent_;
    SessionToken sessionToken_;
    SupervisorSampling sampling_; // declared before fleet_: exceptional destruction revokes workers before joining display I/O
    std::unique_ptr<ConsoleOutput> output_; // workers/fleet are destroyed before this owner joins; intent outlives it
    std::unique_ptr<PreferencesOwner> preferences_; // destroyed after fleet_, before borrowed intent
    std::unique_ptr<PreferencesSettings> preferencesSettings_;
    std::unique_ptr<WorkerFleet> fleet_; // destroyed before its borrowed intent; retained across child restarts/settings
    std::vector<std::unique_ptr<Worker>> workers_;  // declared after mu_ and inbox_: workers (and their readers) go first
    std::array<bool,3> bootstrapReads_{};
    unsigned bootstrapStep_=0;
    uint64_t bootstrapTicket_=0,startupUtc_=0;
    bool bootstrapPending_=false,sessionActivated_=false,settingsRequested_=false,tuningStarted_=false;
    bool manualKnown_=true,manualLoaded_=false,electricityKnown_=true;

    static std::string workerLabel(const Worker &w) { return "GPU " + std::to_string(w.view.seed.index); }

    bool startOutput(){
        try{
            const auto module=selfExecutablePath();if(module.empty())return false;
            const auto helper=std::filesystem::u8path(module).parent_path()/
#if defined(_WIN32)
                "canary_preferences_worker.exe";
#else
                "canary_preferences_worker";
#endif
            ConsoleOutputPolicy policy;
#if defined(_WIN32)
            policy.terminalInput=opt_.dashboard&&_isatty(_fileno(stdin))&&_isatty(_fileno(stdout));
#endif
            output_=std::make_unique<ConsoleOutput>(helper,intent_,sessionToken_,policy);
            if(!output_->start())return false;
            return output_->submit(opt_.dashboard?"\x1b[2J\x1b[?25l":opt_.startupNotice.empty()?"\n":redactor_.apply(opt_.startupNotice));
        }catch(...){return false;}
    }

    PreferencesSettingsContext preferencesContext()const{
        PreferencesSettingsContext context;context.runtime=opt_.runtimeContext;context.game=opt_.gameContext;context.game.running=true;
        context.pool.running=true;context.language=opt_.languageContext;context.language.running=true;
        context.manual.running=true;context.manual.cliDisabled=opt_.noMarket;context.manual.effective=manualEffective_;
        context.electricity.running=true;context.electricity.cliNoEnergy=opt_.noEnergy;context.electricity.effective=electricityEffective_;
        context.electricity.effective.collectEnergy=energy_&&energy_->snapshot()->state==SessionEnergyState::Running;
        context.manualProjectionAvailable=static_cast<bool>(opt_.manualMarketFactory);context.publicReferencesRequested=opt_.marketReferenceFactory&&!opt_.noMarket;
        context.manualFactory=opt_.manualMarketFactory;return context;
    }
    void startPreferences(){
        bootstrapReads_={{opt_.dashboard&&!opt_.uiPreferencesPath.empty(),opt_.dashboard&&!opt_.noMarket&&!opt_.manualMarketPreferencesPath.empty(),!opt_.noEnergy&&!opt_.electricityPreferencesPath.empty()}};
        manualKnown_=!bootstrapReads_[1];electricityKnown_=!bootstrapReads_[2];
        // Plain-mode electricity collection already existed; only its optional initial read needs this owner there.
        if((!opt_.dashboard&&!bootstrapReads_[2])||!intent_.permits(sessionToken_)||(opt_.stop&&*opt_.stop))return;
        try{
            std::array<std::filesystem::path,8> targets{};const std::string paths[]={opt_.uiPreferencesPath,opt_.runtimePreferencesPath,opt_.manualMarketPreferencesPath,opt_.electricityPreferencesPath,opt_.gamePreferencesPath,opt_.poolSecurityPreferencesPath,opt_.languagePreferencesPath};
            bool any=false;for(size_t i=0;i<7;++i)if(!paths[i].empty()){targets[i]=std::filesystem::u8path(paths[i]);any=true;}
            if(!opt_.electricityPreferencesPath.empty())targets[7]=std::filesystem::u8path(opt_.electricityPreferencesPath).parent_path()/"tariff-declaration.json";
            const auto module=selfExecutablePath();if(!any||module.empty())return;
            const auto helper=std::filesystem::u8path(module).parent_path()/
#if defined(_WIN32)
                "canary_preferences_worker.exe";
#else
                "canary_preferences_worker";
#endif
            preferences_.reset(new PreferencesOwner(helper,std::move(targets),intent_,sessionToken_));
            if(opt_.dashboard){preferencesSettings_.reset(new PreferencesSettings(*preferences_,settings_));preferencesSettings_->context=preferencesContext();}
            bootstrapPending_=preferences_->start();
            if(!bootstrapPending_)addEvent("Background preference admission unavailable; no synchronous startup/live fallback.");
        }catch(...){addEvent("Background preference setup unavailable; no synchronous live fallback.");}
    }
    void updatePreferenceBootstrap(){
        if(!preferences_||preferences_->foreignCaller())return;
        const PreferencesKind kinds[]={PreferencesKind::Ui,PreferencesKind::Manual,PreferencesKind::Electricity};
        try{
            auto state=preferences_->snapshot();
            if(state.state==PreferencesOwnerState::Faulted||state.state==PreferencesOwnerState::Stopped){
                bootstrapPending_=false;bootstrapTicket_=0;addEvent("Initial preferences unavailable; unread optional sources withheld. No file fallback or retry.");return;}
            if(bootstrapTicket_){
                const auto reply=preferences_->takeReply(bootstrapTicket_);if(!reply)return;
                bool loaded=false;
                if(reply->kind==kinds[bootstrapStep_]&&reply->status==PreferencesReplyStatus::Loaded){
                    if(reply->kind==PreferencesKind::Ui){UiPreferences value;loaded=parseUiPreferences(reply->document,&value,nullptr);if(loaded)ui_=std::move(value);}
                    else if(reply->kind==PreferencesKind::Manual){ManualMarketPreferences value;loaded=parseManualMarket(reply->document,&value,nullptr);if(loaded){manualEffective_=std::move(value);manualKnown_=manualLoaded_=true;}}
                    else{ElectricityPreferences value;loaded=parseElectricityPreferences(reply->document,&value,nullptr);if(loaded){electricityEffective_=std::move(value);electricityKnown_=true;}}
                }
                if(!loaded)addEvent("Initial preference read refused; associated optional sources withheld. File preserved.");
                bootstrapTicket_=0;++bootstrapStep_;nextFrame_=0;
                state=preferences_->snapshot();
            }
            while(bootstrapStep_<3&&!bootstrapReads_[bootstrapStep_])++bootstrapStep_;
            if(bootstrapStep_==3){bootstrapPending_=false;nextFrame_=0;return;}
            if(state.state==PreferencesOwnerState::Ready){
                const auto admitted=preferences_->load(kinds[bootstrapStep_]);
                if(admitted.status==PreferencesAdmission::Queued)bootstrapTicket_=admitted.ticket;
                else{bootstrapPending_=false;addEvent("Initial preference admission refused; unread optional sources withheld. No retry.");}
            }
        }catch(...){preferences_->requestStop();bootstrapPending_=false;bootstrapTicket_=0;addEvent("Initial preference completion unavailable; unread optional sources withheld.");}
    }
    void openPreferencesSettings(){
        // Unknown/back/reopen must not discard an unrelated root draft or the frozen original panel.
        if(preferences_&&preferences_->snapshot().uncertain)settings_.visible=true;
        else settings_.open(ui_);
        if(preferencesSettings_){preferencesSettings_->context=preferencesContext();preferencesSettings_->load(PreferencesKind::Ui);}
        else settings_.message="Background preferences unavailable; no synchronous file fallback or Save.";
    }
    bool activateSession(uint64_t t0,uint64_t nativeEpoch){
        const auto current=[&]{if(opt_.stop&&*opt_.stop)intent_.stop();return nativeProcessEpochCurrent(nativeEpoch)&&intent_.permits(sessionToken_);};
        if(!current())return false;
        try{
            if(opt_.workerFleetFactory){
                fleet_=opt_.workerFleetFactory(intent_,sessionToken_);
                if(!current()||!fleet_||!fleet_->start(static_cast<unsigned>(workers_.size()),supervisorControlTime())||!current())return false;
            }
            if(opt_.dashboard&&!opt_.noMarket){
                if(!manualKnown_)addEvent("Manual source state unavailable; manual and requested public references both withheld for this session.");
                else if(opt_.marketReferenceFactory&&manualEffective_.enabled)addEvent("Public and manual market sources both requested; references withheld. Choose one source for a new explicit session.");
                else if(opt_.marketReferenceFactory){
                    try{
                        marketReference_=opt_.marketReferenceFactory();
                        if(!current())return false;
                        const bool admitted=marketReference_&&marketReference_->start(sessionToken_.epoch);
                        if(!current())return false;
                        if(admitted)marketReferenceStarted_=true;
                        else{if(marketReference_)marketReference_->requestStop();addEvent("Public market references unavailable; no manual fallback, income or mining change.");}
                    }catch(...){if(marketReference_)marketReference_->requestStop();addEvent("Public market reference admission failed; no source fallback or mining change.");}
                }else if(manualLoaded_&&opt_.manualMarketFactory){manualProjection_=opt_.manualMarketFactory(manualEffective_,startupUtc_,t0);if(!current())return false;}
            }
            if(!current())return false;
            if(!opt_.historyPath.empty())history_.reset(new AcceptedHistory(std::filesystem::u8path(opt_.historyPath)));
            if(electricityKnown_&&!opt_.noEnergy&&electricityEffective_.collectEnergy&&opt_.energyFactory&&!workers_.empty()){
                energy_=opt_.energyFactory();if(!current())return false;
                const auto utc=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                const bool admitted=energy_&&energy_->start(sessionToken_.epoch,utc>0?static_cast<uint64_t>(utc):0,supervisorNowMs(),static_cast<unsigned>(workers_.size()),electricityEffective_,PowerOrigin::ObservedTelemetry);
                if(!current())return false;
                if(!admitted){energy_.reset();addEvent("Session board-energy collection unavailable; mining settings unchanged.");}
            }
            if(electricityKnown_&&opt_.energyRuntimeFactory){
                std::vector<std::string> ids;for(const auto &w:workers_)ids.push_back(w->view.seed.uuid);
                auto runtime=opt_.energyRuntimeFactory(true,opt_.noEnergy);if(!current()){if(runtime)runtime->requestStop();return false;}
                const bool admitted=runtime&&runtime->start(sessionToken_.epoch,ids);
                if(!current()){if(runtime)runtime->requestStop();return false;}
                if(admitted){settings_.energyAccess=energyRuntimeReview(*runtime);settings_.energyAuthorizationAccess=energyRuntimeAuthorization(*runtime);energyCapture_=std::move(runtime);}
                else{if(runtime)runtime->requestStop();addEvent("Durable board-energy runtime unavailable; preserve local history.");}
            }else if(electricityKnown_&&!opt_.noEnergy&&opt_.energyCaptureFactory){
                std::vector<std::string> ids;for(const auto &w:workers_)ids.push_back(w->view.seed.uuid);
                energyCapture_=opt_.energyCaptureFactory();if(!current())return false;
                const bool admitted=energyCapture_&&energyCapture_->start(sessionToken_.epoch,ids);if(!current())return false;
                if(!admitted){energyCapture_.reset();addEvent("Durable board-energy capture unavailable; preserve local history.");}
                else{settings_.energyAccess.snapshot=[this]{return energyCapture_->snapshot();};settings_.energyAccess.inspect=[this](uint64_t g,uint64_t r){return energyCapture_->inspect(g,r);};settings_.energyAccess.retry=[this](uint64_t g,uint64_t r){return energyCapture_->retryReviewed(g,r);};}
            }
            if(manualKnown_&&electricityKnown_&&opt_.forecastFactory){
                forecast_=opt_.forecastFactory(opt_.noMarket,opt_.noEnergy);if(!current())return false;
                const bool admitted=forecast_&&forecast_->start(sessionToken_.epoch);if(!current())return false;
                if(!admitted){if(forecast_)forecast_->requestStop();forecast_.reset();addEvent("Income forecast unavailable; preserve source records.");}
            }
            if(!current())return false;
            if(opt_.tuning){std::vector<std::string> events;tuningStarted_=true;opt_.tuning->start(t0,&events);for(const auto &event:events)addEvent(event);if(!current())return false;}
            for(size_t i=0;i<workers_.size();++i){if(!current())return false;startWorker(i,supervisorNowMs());}
            if(!current())return false;
            if(!workers_.empty()){
                std::vector<DisplaySampleTarget> targets;for(const auto &w:workers_)targets.push_back({w->view.seed.uuid,w->port});
                if(!sampling_.start(std::move(targets),opt_.displaySampleFactory))addEvent("Background display sampling unavailable.");
            }
            return current();
        }catch(...){addEvent("Session startup unavailable; stop retained, no automatic source or worker retry.");return false;}
    }
    void updatePreferences(){
        if(!preferencesSettings_)return;
        auto completed=preferencesSettings_->poll();if(!completed||!completed->reply)return;
        const auto &reply=*completed->reply;
        if(reply.kind==PreferencesKind::Ui){UiPreferences value;if(parseUiPreferences(reply.document,&value,nullptr))ui_=value;}
        if(reply.kind==PreferencesKind::Manual&&reply.status==PreferencesReplyStatus::Saved){manualEffective_=std::move(completed->manualValue);manualProjection_=std::move(completed->manualProjection);}
        nextFrame_=0;
    }

    void addEvent(const std::string &text) {
        events_.push_back({utcClock(time(nullptr)), text});
        if (events_.size() > 200) events_.erase(events_.begin());
        if (!opt_.dashboard) printLine(text);  // plain mode: restarts, connection changes and donation windows are log lines too
    }

    void printLine(const std::string &text) {
        if(!opt_.dashboard&&output_)output_->submit("["+utcClock(time(nullptr))+"] "+text+"\n");
    }

    /** Starts (or restarts) worker i. A spawn failure counts as an unexpected exit. */
    void startWorker(size_t i, uint64_t now) {
        // Recheck signals here as well: one can arrive between loop polling and a due retry/first spawn.
        if (opt_.stop && *opt_.stop) intent_.stop();
        if (!intent_.permits(sessionToken_)) return;
        Worker &w = *workers_[i];
        w.restartPending = false;
        w.view.telValid=false;w.view.lastRateMs=0;
        ++w.historyGeneration;
        w.historySeen = w.historyOutage = false;
        w.historyLastUtc = static_cast<int64_t>(time(nullptr));
        if(fleet_){
            w.controlledExitHandled=false;w.view.haveStats=false;w.view.state="Pausing";w.view.detail="waiting for controlled startup";
            if(!fleet_->launch(static_cast<unsigned>(i),std::filesystem::u8path(opt_.exe),workerArgs(opt_.userArgs,w.view.seed,w.port,opt_.resolvedWorkWait,opt_.resolvedPoolTlsUrl,opt_.resolvedPoolFallbackUrl),
                static_cast<unsigned>(opt_.drainMs+5000),supervisorControlTime())){
                w.controlledExitHandled=true;onExit(i,kExitFaultLatched,now,false,"controlled worker admission refused");
            }else addEvent(workerLabel(w)+": controlled worker requested");
            return;
        }
        w.proc.reset(new ChildProcess());
        const size_t slot = i;
        std::string err;
        const bool ok = w.proc->start(
            opt_.exe, workerArgs(opt_.userArgs, w.view.seed, w.port, opt_.resolvedWorkWait,opt_.resolvedPoolTlsUrl,opt_.resolvedPoolFallbackUrl),
            [this, slot](const std::string &line) {
                std::lock_guard<std::mutex> g(mu_);
                inbox_.push_back({slot, line});
            },
            &err,[this]{if(opt_.stop&&*opt_.stop)intent_.stop();return !intent_.permits(sessionToken_);});
        w.view.haveStats = false;
        w.view.state = "Connecting";
        w.view.detail.clear();
        if (!ok) {
            const bool requestedStop=opt_.stop&&*opt_.stop;
            int nativeCode=0;const bool settled=!w.proc->started()||w.proc->exited(&nativeCode);
            if(requestedStop)intent_.stop();
            if(requestedStop&&settled)onExit(i,nativeCode,now,true,err);
            else onExit(i,!settled||!intent_.permits(sessionToken_)?kExitFaultLatched:-1,now,false,err);
            return;
        }
        addEvent(workerLabel(w) + ": worker started" + (w.restarts ? " (restart " + std::to_string(w.restarts) + ")" : std::string()));
    }

    /** Asks every running worker to stop: closing stdin starts each child's own drain. */
    void beginStop() {
        intent_.stop();  // every pending result/retry from this epoch loses permission before draining
        if(output_)output_->requestStop(ConsoleCleanup::SupervisorStopped);
        if(fleet_)fleet_->requestStop();
        sampling_.requestStop();
        if(preferences_)preferences_->requestStop();
        settings_.stopBillingReview();
        if(forecast_)forecast_->requestStop();
        if(marketReference_){marketReferenceStarted_=false;marketReference_->requestStop();}
        if(energy_)energy_->requestStop();
        if(energyCapture_)energyCapture_->requestStop();
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
        if(energyCapture_)energyCapture_->markGap(sessionToken_.epoch);
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
        if(fleet_&&(code==kExitOk||code==kExitShutdownForced)){
            w.view.state="Stopped";addEvent(workerLabel(w)+": "+w.view.detail+"; explicit new start required");return;
        }
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

    /** Applies one original, generation-matched background observation; no HTTP or driver calls. */
    void sample(size_t i,const DisplaySampleReading &reading) {
        Worker &w = *workers_[i];
        bool historySample = false;
        const auto now=displaySampleTime();
        if(reading.stats&&displaySampleOrdered(reading.statsObserved,now)){
            const auto &snap=*reading.stats;
            if (history_ && reading.accepted) {
                const auto count=*reading.accepted;
                historySample = true;
                const auto utc = reading.statsObserved.utcMs/1000;
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
            w.view.lastSampleMs = reading.statsObserved.monotonicMs;
        }
        if (history_ && !historySample && w.historySeen && !w.historyOutage) {
            history_->markGap(static_cast<unsigned>(i), w.historyLastUtc);
            w.historyOutage = true;
        }
        const bool powerTime=displaySampleOrdered(reading.powerBegan,reading.powerFinished)&&displaySampleOrdered(reading.powerFinished,now);
        const bool powerValid=powerTime&&displaySampleInterval(reading.powerBegan,reading.powerFinished,1000)&&reading.telemetry.valid&&
            gpuUuidEqual(w.view.seed.uuid,reading.telemetry.deviceUuid);
        w.view.tel=reading.telemetry;
        w.view.telObserved=reading.powerBegan;
        w.view.telValid=powerValid&&displaySampleInterval(reading.powerBegan,now,2000);
        if((energy_||energyCapture_) && intent_.permits(sessionToken_) && powerTime) {
            const auto mono=reading.powerBegan.monotonicMs;
            const auto utc=reading.powerBegan.utcMs;
            SessionPowerInput input;input.channel=static_cast<unsigned>(i);input.monotonicMs=mono;input.utcMs=utc>0?static_cast<uint64_t>(utc):0;
            if(powerValid&&reading.telemetry.powerValid)input.milliwatts=reading.telemetry.powerMilliwatts;
            if(energyCapture_)energyCapture_->submit(sessionToken_.epoch,input);
            if(energy_&&(!input.milliwatts||mono>=w.nextEnergyAt)) {
                w.nextEnergyAt=mono+electricityEffective_.recordIntervalMs;
                energy_->submit(sessionToken_.epoch,input);
            }
        }else if(!powerTime){
            if(energyCapture_)energyCapture_->markGap(sessionToken_.epoch);
            if(energy_)energy_->requestStop(); // invalid original clocks cannot be replaced with delivery time
        }
        if (reading.stats&&w.view.lastSampleMs==reading.statsObserved.monotonicMs) {
            const bool donating = w.view.stats.fee.state == "donation";
            const uint64_t t = reading.statsObserved.monotonicMs;
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
        }else w.view.lastRateMs=0;
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
        if(forecast_&&intent_.permits(sessionToken_))model.finances=forecast_->snapshot()->view;
        if (history_) for (const auto &worker : workers_) if (!worker->historySeen || worker->historyOutage) model.history.gaps = true;
        if (opt_.dashboard && !opt_.noMarket && manualProjection_ && intent_.permits(sessionToken_)) model.publicMarket=manualProjection_(static_cast<uint64_t>(time(nullptr)),now);
        if(opt_.dashboard&&!opt_.noMarket&&marketReferenceStarted_&&intent_.permits(sessionToken_))model.publicMarket=marketReference_->view(static_cast<uint64_t>(time(nullptr)),now);
        if(energy_&&intent_.permits(sessionToken_)) {
            const auto energy=energy_->snapshot();model.energy=energy->view;
            if(model.energy.enabled&&now>=energy->throughMonoMs&&now-energy->throughMonoMs>electricityEffective_.maximumGapMs)
                model.energy.lines.push_back("No recent energy observation; known totals retained, current tail unavailable.");
        }
        if(fleet_) for(size_t i=0;i<workers_.size();++i){
            const auto control=fleet_->snapshot(static_cast<unsigned>(i));
            model.forceResumeActive=model.forceResumeActive||control.forceResumeActive;
            model.forceResumeAvailable=model.forceResumeAvailable||control.forceResumeAvailable;
        }
        return model;
    }

    void frame(uint64_t now, uint64_t t0, bool tty) {
        if (!tty) return;
        if (now < nextFrame_) return;
        nextFrame_ = now + (settings_.visible ? 50 : ui_.refreshMs);
        const DashModel m = buildModel(now, t0);
#if defined(_WIN32)
        const auto terminal=output_->snapshot();view_.columns=static_cast<int>(terminal.columns);view_.rows=static_cast<int>(terminal.rows);
#else
        view_.columns=terminalColumns(80);view_.rows=terminalRows(24);
#endif
        view_.color = ui_.color ? detectColorLevel() : ColorLevel::None;
        view_.showEvents = ui_.showEvents;
        view_.hideWorker = ui_.hideWorker;
        view_.options = ui_;
        dash_detail::g_dashBoxRules = !g_asciiOnly;
        // Erase to the end of every line: a shorter line must not leave the previous frame's characters behind.
        const std::string text = bootstrapPending_ ? std::string("Preferences PENDING\nInitial inspection\nWorkers not started\nQ/^C Stop\n[S] Settings later\n")+(settingsRequested_?"Settings requested\n":"") : settings_.visible ? (preferencesSettings_&&preferencesSettings_->overlay()?preferencesSettings_->render(view_.columns,view_.rows,view_.color):settings_.render(view_.columns, view_.rows, view_.color)) : renderDashboard(m, view_);
        std::string out;
        out.reserve(text.size() + 256);
        for (char ch : text) {
            if (ch == '\n') out += "\x1b[K";
            out += ch;
        }
        if(output_)output_->submit("\x1b[H"+out+"\x1b[K\x1b[J",true);
    }
};

}  // namespace om
