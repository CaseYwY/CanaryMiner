// The shared run loop. Backend-agnostic: it owns the nonce counter, job
// lifecycle, stop signal and reporting, and only ever asks the Algorithm to
// "test these nonces".

#include "run.h"
#include "host_work_wait.h"
#include "submit_outcome.h"
#include "pearl_build_profile.h"
#include "pearl_job_extra.h"
#include "pearl_pool.h"
#include "pearl_run_session.h"
#include "pearl_native_completion.h"
#include "pearl_accepted_quota.h"
#include "pearl_sustained_admission.h"
#include "pearl_integrated_pipeline.h"
#include "pearl_production_evidence.h"
#include <thread>

namespace om {
#if defined(CANARY_WORK_PAUSE_TESTING)
#if CANARY_WORK_PAUSE_TESTING != 1
#error "CANARY_WORK_PAUSE_TESTING must be exactly 1"
#endif
JobSource *workPauseTestSource(); // CPU fixture only; absent from ordinary artifacts
#endif
#if defined(CANARY_PEARL_TEST_RUN_SUSTAINED)
#if CANARY_PEARL_TEST_RUN_SUSTAINED != 1
#error "CANARY_PEARL_TEST_RUN_SUSTAINED must be exactly 1"
#endif
int64_t pearlTestSustainedNowNs() noexcept;
#endif
static int64_t sustainedNowNs() noexcept {
#if defined(CANARY_PEARL_TEST_RUN_SUSTAINED)
    return pearlTestSustainedNowNs();
#else
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}


// A validated-conservative memory bump (MHz over stock) for --mem-oc, keyed by
// GPU generation since a generation shares one memory spec. Only generations
// proven on real hardware get a value; anything else returns 0 and the card is
// left alone. RDNA3 measured stable at +100 on a 7900 XT (1350 MHz,
// correctness-verified) and crashes at +150, so ship +75. RDNA2 is the same
// GDDR6 class but untested here, so a smaller +50.
static int conservativeAmdMemBumpMhz(const char *gpuName) {
    std::string n;
    for (const char *p = gpuName ? gpuName : ""; *p; ++p)
        n += (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : *p;
    auto has = [&](const char *s) { return n.find(s) != std::string::npos; };
    if (has("navi 3") || has("navi3") || has("rx 79") || has("rx 78") ||
        has("rx 77") || has("rx 76") || has("7900") || has("7800") ||
        has("7700") || has("7600"))
        return 75;  // RDNA3
    if (has("navi 2") || has("navi2") || has("rx 69") || has("rx 68") ||
        has("rx 67") || has("rx 66") || has("6900") || has("6800") ||
        has("6700") || has("6600"))
        return 50;  // RDNA2
    return 0;
}

// A conservative memory-clock offset (MHz of transfer rate, applied via NVML)
// for --mem-oc on NVIDIA, keyed by MEMORY TYPE. GDDR6X cards force performance
// state P2 under load, which runs the memory below its rated speed; +500
// restores rated on a 4090 (validated, ~+2.6%) and the P2 downclock is the same
// across GDDR6X, so this is a restore-to-spec, not a real overclock. GDDR6 and
// GDDR7 have no such downclock, so they get nothing - a +offset there would be a
// genuine overclock. NVML clock control is Linux + root only (fails on Windows).
static int conservativeNvidiaMemOffsetMhz(const char *gpuName) {
    std::string n;
    for (const char *p = gpuName ? gpuName : ""; *p; ++p)
        n += (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : *p;
    auto has = [&](const char *s) { return n.find(s) != std::string::npos; };
    // GDDR6X: Ada 4090/4080/4070-family, Ampere 3090/3080 and the 3070 Ti.
    // Memory-class detector, not an allowlist: device admission is src/product/device_policy.h.
    // NOT 4060/4060 Ti, NOT the bare 3070/3060 (GDDR6), NOT 50-series (GDDR7).
    if (has("4090") || has("4080") || has("4070") || has("3090") ||
        has("3080") || has("3070 ti"))
        return 500;
    return 0;
}

// Opt-in conservative memory overclock (--mem-oc), keyed by memory type. AMD
// GDDR6 raises the clock over stock via overdrive sysfs; NVIDIA GDDR6X restores
// its P2 downclock via NVML. Both are Linux-only.
static void applyMemOc(GpuMonitor &gpu, const char *gpuName) {
    if (gpu.isAmd()) {
        const int bump = conservativeAmdMemBumpMhz(gpuName);
        if (bump == 0) {
            fprintf(stderr,
                    "--mem-oc: no conservative profile for this GPU, memory clock "
                    "left alone.\n");
            return;
        }
        int stock = 0;
        const int got = gpu.amdApplyMemBump(bump, &stock);
        if (stock <= 0) {
            fprintf(stderr,
                    "--mem-oc: AMD overdrive unavailable. Add "
                    "amdgpu.ppfeaturemask=0xffffffff to the kernel command line, "
                    "reboot, and run as root.\n");
            return;
        }
        if (got <= 0)
            fprintf(stderr, "--mem-oc: could not set the memory clock (need root).\n");
        else
            fprintf(stderr, "mem-oc: memory %d -> %d MHz (+%d, conservative)\n",
                    stock, got, got - stock);
    } else if (gpu.isNvidia()) {
        const int off = conservativeNvidiaMemOffsetMhz(gpuName);
        if (off == 0) {
            fprintf(stderr,
                    "--mem-oc: no conservative profile for this NVIDIA GPU (only "
                    "GDDR6X cards have a P2 downclock to restore). Use --mclk-offset "
                    "N by hand if you know your card.\n");
            return;
        }
        const int got = gpu.applyMemOffsetMhz(off);
        if (got < 0)
            fprintf(stderr,
                    "--mem-oc: could not set the memory clock offset - NVML clock "
                    "control needs Linux and root, and is unavailable on Windows.\n");
        else
            fprintf(stderr,
                    "mem-oc: memory clock offset +%d (GDDR6X, restore P2 to rated)\n",
                    off);
    }
}

int runMiner(Algorithm *algo, const RunOptions &opt, const char *gpuName,
             double gpuMemGB, const char *archLabel, volatile sig_atomic_t *stop,
             const char *gpuUuid, unsigned telemetryDeviceIndex) {
    if (algo) algo->setPreparationStopFlag(stop);
    // Covers every early return; normal completion also checks drain explicitly.
    struct LookaheadExit {
        Algorithm *algorithm;
        ~LookaheadExit() { if (algorithm) algorithm->discardPearlLookahead(); }
    } lookaheadExit{algo};
    if (!workWaitEnvironmentCompatible(opt.workWaitMs)) {
        fprintf(stderr, "host work wait is unavailable in research/pipeline/evidence modes\n");
        return 1;
    }
    if(opt.completedWork&&(!opt.workPause||(opt.workLane!=WorkLane::User&&opt.workLane!=WorkLane::Donation))){
        fprintf(stderr,"completed-work capture requires explicit ordinary cooperative control and lane\n");return kRunPauseFault;
    }
    if(opt.workPause&&(opt.bench||opt.formalBenchmark.enabled||opt.pearlAcceptedQuota||opt.pearlSustainedSeconds||opt.pearlProduction||
        opt.pearlWorkRange.enabled||pearlPrivateEnabled(opt.pearlPrivateExecution)||pearlPublicEnabled(opt.pearlPublicExecution)||
        opt.memOffsetMhz||opt.memOc||!workWaitEnvironmentCompatible(1)
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        ||opt.pearlGuardRetirementSession
#endif
        )){
        fprintf(stderr,"cooperative pause requires an ordinary run without research or hardware-tuning controls\n");return kRunPauseFault;
    }
    // Direct run-loop callers must honor the same profile boundary as the CLI.
    // Removed transports must never fall through to another source family.
    if (!algo || !algo->name() || !buildAllowsAlgorithm(algo->name())) {
        fprintf(stderr, "this CanaryMiner build supports only pearl-pow\n");
        return 1;
    }
    if (!pearlWorkRangePolicy(opt.pearlWorkRange,
            algo && algo->name() && !std::strcmp(algo->name(),"pearl-pow"),
            opt.bench || opt.formalBenchmark.enabled, !opt.poolHost.empty())) {
        fprintf(stderr,"invalid production Pearl work range\n"); return 1;
    }
    const char *localRetirementOptIn = getenv("CANARY_PEARL_LOCAL_RETIREMENTS");
    const char *evidenceRunId=getenv("CANARY_PEARL_EVIDENCE_RUN_ID");
    const unsigned acceptedQuota = opt.pearlAcceptedQuota;
    const bool genericNativeEnabled = pearlGenericNativeEvidenceMode(getenv("CANARY_PEARL_NATIVE_EVIDENCE"));
    PearlGenericNativeBinding genericNativeBinding(getenv("CANARY_PEARL_NATIVE_EVIDENCE"));
    const bool publicExecution=pearlPublicEnabled(opt.pearlPublicExecution);
    const PearlPrivateExecution accountingMode=publicExecution?pearlPublicAccountingMode(opt.pearlPublicExecution):opt.pearlPrivateExecution;
    const bool privateExecution=pearlPrivateEnabled(opt.pearlPrivateExecution);
    const bool executionAccounting=privateExecution || publicExecution;
    PearlNativeSelection privateSelection;
    PearlSm120NativeBinding privateGenericBinding;
    PearlPrivateExecutionStats privateFinalStats;
    auto executionSelectionValid=[&](const PearlNativeSelection &v) noexcept {
        return publicExecution?pearlPublicSelectionValid(v,opt.pearlPublicExecution):pearlPrivateSelectionValid(v,accountingMode);
    };
    auto privateAdmission=[&]() noexcept {
        if (!pearlPublicEnvironment(opt.pearlPublicExecution) || !pearlPrivateEnvironment(opt.pearlPrivateExecution)) return false;
        if(publicExecution) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
            return pearlPublicPolicy(opt.pearlPublicExecution,opt.pearlGuardRetirementSession && opt.poolHost=="127.0.0.1",
                opt.pearlGuardRetirementSession?opt.pearlGuardRetirementSession->cap():0,opt.batch,opt.pearlAcceptedQuota,opt.pearlSustainedSeconds,opt.pearlSustainedPublic);
#else
            return false;
#endif
        }
        if (!privateExecution) return true;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        return opt.pearlGuardRetirementSession && opt.pearlGuardRetirementSession->cap()==20 &&
            opt.poolHost=="127.0.0.1" && !opt.pearlSustainedPublic && opt.batch==pearlPrivateBatch(opt.pearlPrivateExecution) &&
            (opt.pearlAcceptedQuota==2 || (!opt.pearlAcceptedQuota && opt.pearlSustainedSeconds));
#else
        return false;
#endif
    };
    if (!privateAdmission()) return 1;
    PearlShapeExperimentBinding shapeBinding(
        getenv("CANARY_PEARL_SHAPE_EXPERIMENT"), getenv("CANARY_PEARL_SHAPE"), getenv("CANARY_PEARL_TILE"));
    const bool productFixedSelection = opt.productFixedSelection && !opt.autotune;
    const bool shapeOptionsMatch = productFixedSelection
        ? pearlProductFixedSelectionMatches(getenv("CANARY_PEARL_SHAPE"), getenv("CANARY_PEARL_TILE"))
        : opt.pearlShapeExperiment == shapeBinding.value();
    bool shapeOwnedGuard = false;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    shapeOwnedGuard = opt.pearlGuardRetirementSession && opt.poolHost == "127.0.0.1";
#endif
    if (opt.pearlProduction) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        if (!shapeOwnedGuard || !opt.pearlGuardRetirementSession->production() ||
            opt.pearlGuardRetirementSession->deadline()!=0) return 1;
#else
        return 1;
#endif
    }
    PearlIntegratedPipelineBinding integratedPipelineBinding(getenv("CANARY_PEARL_INTEGRATED_PIPELINE"));
    auto shapeAdmission = [&]() noexcept {
        if (!integratedPipelineBinding.revalidate(getenv("CANARY_PEARL_INTEGRATED_PIPELINE"),
            pearlIntegratedPipelineContext(opt, algo ? algo->name() : nullptr,
                algo && algo->pearlIntegratedPipelineCompiled(), shapeOwnedGuard))) return false;
        if (productFixedSelection)
            return privateAdmission() && shapeOptionsMatch && !getenv("CANARY_PEARL_SHAPE_EXPERIMENT");
        return privateAdmission() && shapeOptionsMatch && shapeBinding.revalidate(
            getenv("CANARY_PEARL_SHAPE_EXPERIMENT"), getenv("CANARY_PEARL_SHAPE"),
            genericNativeEnabled && genericNativeBinding.modeMatches(getenv("CANARY_PEARL_NATIVE_EVIDENCE")),
            opt.pearlAcceptedQuota || opt.pearlSustainedSeconds || opt.pearlProduction, shapeOwnedGuard,
            getenv("CANARY_PEARL_EXPERIMENT") || !shapeBinding.tileAllowed(getenv("CANARY_PEARL_TILE")) ||
            getenv("CANARY_PEARL_PROFILE_INTERVAL") || getenv("CANARY_PEARL_A_ROOT_FUSION") ||
            (getenv("CANARY_PEARL_ATTEMPT_OVERLAP") && !executionAccounting) || getenv("CANARY_PEARL_HARD_TARGET") ||
            getenv("CANARY_PEARL_BENCH_ALL_HIT"), getenv("CANARY_PEARL_TILE"));
    };
    if (!shapeAdmission() || !pearlAcceptedQuotaAdmission(opt, algo ? algo->name() : nullptr,
            getenv("CANARY_PEARL_ACCEPTED_QUOTA"), localRetirementOptIn, evidenceRunId,
            getenv("CANARY_PEARL_EXPERIMENT"),
            (getenv("CANARY_PEARL_SHAPE") && !shapeBinding.enabled()) || !shapeBinding.tileAllowed(getenv("CANARY_PEARL_TILE")) ||
            getenv("CANARY_PEARL_PROFILE_INTERVAL") || getenv("CANARY_PEARL_A_ROOT_FUSION") ||
            (getenv("CANARY_PEARL_ATTEMPT_OVERLAP") && !executionAccounting) || getenv("CANARY_PEARL_HARD_TARGET") ||
            getenv("CANARY_PEARL_BENCH_ALL_HIT"), getenv("CANARY_PEARL_NATIVE_EVIDENCE"))) {
        fprintf(stderr,"invalid accepted quota; refusing before device/source setup\n"); return 1;
    }
    PearlAcceptedQuotaTracker quotaTracker(acceptedQuota);
    const bool sustainedEnabled = opt.pearlSustainedSeconds != 0;
    auto sustainedAdmission = [&]() noexcept {
        return shapeAdmission() && pearlSustainedAdmission(opt, algo ? algo->name() : nullptr,
            getenv("CANARY_PEARL_SUSTAINED_MODE"), getenv("CANARY_PEARL_SUSTAINED_SECONDS"),
            getenv("CANARY_PEARL_ACCEPTED_QUOTA"), getenv("CANARY_PEARL_LOCAL_RETIREMENTS"),
            getenv("CANARY_PEARL_EVIDENCE_RUN_ID"), getenv("CANARY_PEARL_EXPERIMENT"),
            (getenv("CANARY_PEARL_SHAPE") && !shapeBinding.enabled()) || !shapeBinding.tileAllowed(getenv("CANARY_PEARL_TILE")) ||
            getenv("CANARY_PEARL_PROFILE_INTERVAL") || getenv("CANARY_PEARL_A_ROOT_FUSION") ||
            (getenv("CANARY_PEARL_ATTEMPT_OVERLAP") && !executionAccounting) || getenv("CANARY_PEARL_HARD_TARGET") ||
            getenv("CANARY_PEARL_BENCH_ALL_HIT"), getenv("CANARY_PEARL_NATIVE_EVIDENCE"));
    };
    PearlSustainedPolicy sustainedPolicy;
    if (!sustainedAdmission() || !pearlParseSustained(getenv("CANARY_PEARL_SUSTAINED_MODE"),
            getenv("CANARY_PEARL_SUSTAINED_SECONDS"), &sustainedPolicy)) return 1;
    PearlSustainedTracker sustainedTracker(sustainedPolicy);
    auto genericNativeAdmission = [&]() noexcept {
        const char *mode=getenv("CANARY_PEARL_NATIVE_EVIDENCE");
        if (!shapeAdmission()) return false;
        if (!genericNativeEnabled) return mode==nullptr;
        return genericNativeBinding.modeMatches(mode) && sustainedAdmission() &&
            pearlAcceptedQuotaAdmission(opt,algo ? algo->name() : nullptr,
                getenv("CANARY_PEARL_ACCEPTED_QUOTA"),getenv("CANARY_PEARL_LOCAL_RETIREMENTS"),
                getenv("CANARY_PEARL_EVIDENCE_RUN_ID"),getenv("CANARY_PEARL_EXPERIMENT"),
                (getenv("CANARY_PEARL_SHAPE") && !shapeBinding.enabled()) || !shapeBinding.tileAllowed(getenv("CANARY_PEARL_TILE")) ||
                getenv("CANARY_PEARL_PROFILE_INTERVAL") || getenv("CANARY_PEARL_A_ROOT_FUSION") ||
                (getenv("CANARY_PEARL_ATTEMPT_OVERLAP") && !executionAccounting) || getenv("CANARY_PEARL_HARD_TARGET") ||
                getenv("CANARY_PEARL_BENCH_ALL_HIT"),mode);
    };

    if (!pearlLocalRetirementAdmission(localRetirementOptIn,
            algo && algo->name() && !std::strcmp(algo->name(), "pearl-pow") &&
                !opt.bench && !opt.formalBenchmark.enabled && !opt.poolHost.empty(),
            !opt.pearlTranscript.empty() && opt.pearlTranscript.size() <= 4096 &&
                opt.pearlTranscript.find('\0') == std::string::npos) ||
        (localRetirementOptIn && !pearlEvidenceRunIdValid(evidenceRunId))) {
        fprintf(stderr, "invalid local-retirement evidence opt-in; requires value 1 and canonical pool transcript\n");
        return 1;
    }
    if (algo && !algo->pearlExperimentConfigurationValid(
                    opt, getenv("CANARY_PEARL_EXPERIMENT"))) {
        fprintf(stderr, "invalid or non-isolated Pearl experiment; refusing before device telemetry/source setup\n");
        return 1;
    }
    const char *admission = pearlRuntimeAdmissionProblem(
        opt, algo ? algo->name() : nullptr,
        algo ? algo->pearlRuntimeCapability() : PearlRuntimeCapability::Unsupported,
        getenv("CANARY_PEARL_BENCH_ALL_HIT") != nullptr,
        getenv("CANARY_PEARL_HARD_TARGET") != nullptr);
    if (admission) {
        fprintf(stderr, "%s; refusing before device telemetry/source setup\n", admission);
        return 1;
    }
    if (!opt.bench && !std::strcmp(algo->name(), "pearl-pow") && !pearlLoginByAccount(opt)) {
        const std::string problem = pearlWalletProblem(opt.wallet);
        if (!problem.empty()) {
            fprintf(stderr, "invalid Pearl wallet: %s; refusing before telemetry/source setup\n", problem.c_str());
            return 1;
        }
    }
    // CLI presence is checked in miner.cu. Recheck the resulting canonical
    // values here before telemetry or clock control so non-CLI callers cannot
    // construct a looser "formal" run.
    if (opt.formalBenchmark.enabled && getenv("CANARY_PEARL_PROFILE_INTERVAL")) {
        fprintf(stderr,
                "formal benchmark rejects inherited environment variable "
                "CANARY_PEARL_PROFILE_INTERVAL; refusing before telemetry or clock control\n");
        return 1;
    }
    if (opt.formalBenchmark.enabled &&
        (!algo || !canonicalFormalBenchmarkRunOptions(opt, algo->name()))) {
        fprintf(stderr,
                "formal benchmark runtime options are not canonical; "
                "refusing before telemetry or clock control\n");
        return 1;
    }

    CompletedWorkRun workCapture(opt.completedWork);
    if(!workCapture.begin(opt.workLane))return kRunPauseFault;
    unsigned appliedWorkWaitMs=opt.workWaitMs;
    const auto pauseBoundary=[&](){
        if(!opt.workPause)return WorkPauseResult::Ready;
        for(;;){const auto paused=pauseAtWorkBoundary(*opt.workPause,[&]{return *stop!=0;},[&]{return algo->discardPearlLookahead();},
            [&](bool paused){if(opt.loopHook)opt.loopHook->pauseChanged(paused);},[](){return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());},
            [](unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));});
            if(paused!=WorkPauseResult::Ready)return paused;
            if(*stop)return WorkPauseResult::Stopped;
            const auto state=opt.workPause->applyWorkWait(appliedWorkWaitMs);
            if(*stop||state==WorkControlRequest::Stop)return WorkPauseResult::Stopped;
            if(state==WorkControlRequest::Fault||appliedWorkWaitMs>kMaxWorkWaitMs)return WorkPauseResult::Failed;
            if(state==WorkControlRequest::Run)return WorkPauseResult::Ready;
        }
    };
    // An initial pause also precedes configuration, telemetry, preparation and source construction.
    const auto initialPause=pauseBoundary();
    if(initialPause==WorkPauseResult::Stopped)return workCapture.finish()?0:kRunPauseFault;
    if(initialPause==WorkPauseResult::Failed)return kRunPauseFault;

    // Open opted-in evidence before telemetry/source setup. Sinks outlive the
    // borrowed session and never share or overwrite the wire transcript.
    PearlLocalRetirementFile localRetirementFile;
    PearlProductionEvidenceFile productionLocalFile,productionWireFile;
    PearlLocalRetirementFile nativeCompletionFile;
    PearlNativeCompletionSink nativeCompletion(nativeCompletionFile);
    std::unique_ptr<PearlLocalRetirementJsonSink> localRetirementSink;
    if (localRetirementOptIn) {
        try {
            const std::string path = opt.pearlTranscript + ".local-retirements.jsonl";
            if (!(opt.pearlProduction ? productionLocalFile.open(path) : localRetirementFile.open(path.c_str()))) {
                fprintf(stderr, "local-retirement evidence exclusive open failed; stopping\n");
                return 1;
            }
            localRetirementSink.reset(new PearlLocalRetirementJsonSink(opt.pearlProduction ?
                static_cast<PearlLocalRetirementWriter&>(productionLocalFile) : localRetirementFile));
            if (opt.pearlProduction && !productionWireFile.open(opt.pearlTranscript)) return 1;
            const std::string completionPath=opt.pearlTranscript+".completion.json";
            if (!nativeCompletionFile.open(completionPath.c_str()) || !localRetirementSink->beginRun(evidenceRunId)) {
                fprintf(stderr,"local-retirement completion reservation/start failed; stopping\n");
                return 1;
            }
        } catch (...) {
            fprintf(stderr, "local-retirement evidence setup failed; stopping\n");
            return 1;
        }
    }
    // Programmatic callers receive the same immutable configuration as CLI
    // callers; admission precedes any algorithm configuration side effects.
    algo->configure(opt);
    Job pearlBenchmark;
    if (opt.bench && !std::strcmp(algo->name(), "pearl-pow") &&
        !initializePearlBenchmarkJob(algo, &pearlBenchmark)) {
        fprintf(stderr, "canonical Pearl synthetic fixture construction failed\n");
        return 1;
    }

    GpuMonitor gpu;
#if !defined(CANARY_WORK_PAUSE_TESTING)
    if (!gpu.open(telemetryDeviceIndex, gpuUuid ? gpuUuid : "") &&
        gpuUuid && *gpuUuid) {
        fprintf(stderr,
                "GPU telemetry unavailable: NVML could not bind selected UUID %s; "
                "power/clock controls and efficiency reporting are disabled.\n",
                gpuUuid);
    }
#else
    (void)telemetryDeviceIndex;(void)gpuUuid; // test binaries never discover a device
#endif

    // Optional memory transfer-rate offset (unqualified; rejected for qualification runs).
    if (opt.memOffsetMhz != 0) {
        const int got = gpu.applyMemOffsetMhz(opt.memOffsetMhz);
        if (got < 0)
            fprintf(stderr,
                    "could not set the memory clock offset. Needs an NVIDIA "
                    "card, a recent driver, and root on Linux.\n");
    }

    // Opt-in conservative memory OC (reset on exit via gpu.close()).
    if (opt.memOc) applyMemOc(gpu, gpuName);

    const bool tty = stdoutIsTty() && !opt.plain;

    MinerStats stats;
    stats.gpuName = gpuName ? gpuName : "unknown GPU";
    stats.algo = algo->name();
    stats.arch = archLabel ? archLabel : "";
    stats.backend = opt.backendLabel;
    stats.gpuMemGB = gpuMemGB;

    const bool isPearlBanner = algo && algo->name() && !strcmp(algo->name(), "pearl-pow");
    // Banner before any network or GPU work. Connecting to a pool and
    // building the dataset take ~15s combined, and printing nothing for that
    // long reads as a hang - it is the first thing a new user sees.
    // --pool wins over the gateway when both look set, because pearlHost has a
    // DEFAULT and an explicitly typed pool does not. Ordering this the other
    // way made the banner announce the gateway while the miner used the pool.
    stats.source =
        opt.bench ? "BENCHMARK ONLY - not mining, nothing submitted"
        : !opt.poolHost.empty()
            ? opt.poolHost + ":" + std::to_string(opt.poolPort) +
                  (isPearlBanner ? " (pearl pool)" : " (pool)")
        : "no pool configured";
    printBanner(stats, tty);

    if (opt.bench) {
        logLine(tty, "warn",
                "benchmark mode: measuring hashrate only. No pool, no wallet, "
                "no shares, no payouts.");
        // The algorithm names its own scripts. This used to be an if-chain
        // over algorithm names here, which told BC3 users to go and mine
        // Ergo; that was fixed by adding a branch, and it then told KawPow
        // users to go and mine Ergo. A list you have to remember to extend
        // fails the same way every time it is extended.
        const std::string script = algo ? algo->launcherGlob() : std::string();
        if (script.empty()) {
            logLine(tty, "info",
                    "to actually mine, pass --pool HOST:PORT --wallet <address>");
        } else {
            logLine(tty, "info",
                    "to actually mine, use a " + script +
                        " script (edit WALLET first) or pass --pool HOST:PORT "
                        "--wallet <address>");
        }
    }

    // Which transport a Pearl job comes over depends on whether the user has
    // a node. Both end in the same PlainProof.
    const bool isPearl = algo && algo->name() && !strcmp(algo->name(), "pearl-pow");
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    if (opt.pearlGuardRetirementSession && (!isPearl || opt.bench || !localRetirementOptIn ||
            (!acceptedQuota && !sustainedEnabled && !opt.pearlProduction))) return 1;
#endif


    std::unique_ptr<JobSource> source;
    uint64_t noncePrefix = 0;
    int nonceBits = 64;
#if defined(CANARY_WORK_PAUSE_TESTING)
    source.reset(workPauseTestSource());if(!source)return 1;
#else
    if (opt.bench) {
        stats.source = "benchmark (no pool/node)";
    } else if (isPearl && !opt.poolHost.empty()) {
        // Pearl to a pool. The pool runs the node and the prover, so this
        // needs nothing but a wallet - which is the whole point, since the
        // gateway path below requires running a Pearl node yourself.
        // Check the address here, not at the pool. The pool answers a wrong
        // one with "Invalid Pearl address", which does not say that the
        // launcher's placeholder was never edited - the single most likely
        // reason to see it.
        const std::string walletProblem = pearlLoginByAccount(opt) ? std::string() : pearlWalletProblem(opt.wallet);
        if (!walletProblem.empty()) {
            logLine(tty, "error",
                    opt.wallet.empty()
                        ? "mining Pearl to a pool needs --wallet with your PRL "
                          "address (it starts with prl1)."
                        : "the wallet ending '..." +
                              opt.wallet.substr(opt.wallet.size() > 4 ? opt.wallet.size() - 4 : 0) +
                              "' is not a Pearl payout address: " + walletProblem);
            return 1;
        }
#if defined(CANARY_PEARL_TEST_RUN_QUOTA)
        auto *pp = pearlTestQuotaSource(opt, localRetirementOptIn != nullptr);
        if (!pp) return 1;
#else
        auto *pp = new PearlPoolSource(opt.poolHost, opt.poolPort, opt.wallet,
                                       opt.worker, opt.pearlPolicy, opt.pearlProduction ? "" : opt.pearlTranscript,
                                       nullptr, nullptr, nullptr, localRetirementOptIn != nullptr, opt.pearlSoloAuthorization);
#endif
        source.reset(pp);
        // H-019 task 3: the v2 dialect is offered only from the product pool.compression setting.
        if (opt.poolProofCompression && !pp->setProofCompression(true, opt.poolAccount)) {
            fprintf(stderr, "%s\n", pp->lastError().c_str());
            return 1;
        }
        if (localRetirementOptIn && !(opt.pearlProduction ? pp->enableProductionDiagnosticCompletion(productionWireFile) : pp->enableDiagnosticCompletion())) {
            fprintf(stderr,"wire diagnostic completion setup failed; stopping\n"); return 1;
        }
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        if (opt.pearlGuardRetirementSession && (!localRetirementOptIn ||
            (!acceptedQuota && !sustainedEnabled && !opt.pearlProduction) || !pp->enableGuardRetirement(opt.pearlGuardRetirementSession))) {
            fprintf(stderr,"trusted guard retirement setup failed; stopping\n"); return 1;
        }
#endif
        stats.source = source->describe();
    } else {
        // Pool mining is the only mode: the benchmark has no source and everything else needs --pool.
        logLine(tty, "error", "no pool configured; pass --pool HOST:PORT --wallet <PRL address>");
        return 1;
    }
#endif

    // Construct after source so borrowed callbacks are unbound before source
    // destruction on every return or exception. Synthetic runs have no source.
    std::unique_ptr<PearlRunSession> pearlSession;
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
    const char *deferredSetting=std::getenv("CANARY_PEARL_ASYNC_SUBMIT");
    const bool deferredReplies=deferredSetting && !std::strcmp(deferredSetting,"1");
    if(deferredSetting && !deferredReplies)return 1;
#endif
#if !defined(CANARY_WORK_PAUSE_TESTING)
    if (isPearl && !opt.bench) {
        auto *pool = dynamic_cast<PearlPoolSource *>(source.get());
        if (!pool) return 1;
        pearlSession.reset(new PearlRunSession(*algo, *pool, opt.pearlPolicy,
            [](void *) noexcept -> uint64_t {
                return static_cast<uint64_t>(std::chrono::duration_cast<
                    std::chrono::milliseconds>(std::chrono::steady_clock::now()
                        .time_since_epoch()).count());
            }, nullptr, localRetirementSink.get()));
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
        if(deferredReplies && !pearlSession->enableDeferredSearchOwnership())return 1;
#endif
        if (!pearlSession->attach() || !(opt.pearlProduction ? pearlSession->armGuardProduction(evidenceRunId) :
                pearlSession->armGuardNaturalStop(evidenceRunId,acceptedQuota,sustainedPolicy.seconds,sustainedPolicy.minimumAccepted))) {
            logLine(tty, "error", "Pearl proof ownership/progress binding failed");
            return 1;
        }
    }
#endif

#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
    // The single run-loop owner pumps replies at search boundaries. Wait only
    // for a second proof's submission slot or final outcome reconciliation.
    const auto pumpDeferred=[&](bool wait,bool cancel=false) {
        if(!deferredReplies || !pearlSession)return true;
        do {
            if(!pearlSession->pollDeferredSubmission(cancel))return false;
            const auto rejection=source->takeRejectReason();
            if(!rejection.empty())logLine(tty,"error","pool REJECTED the share: "+rejection);
            const auto warning=source->takeJobWarning();
            if(!warning.empty())logLine(tty,"warn",warning);
            if(sustainedEnabled) {
                PoolOutcomeSnapshot outcomes;std::string error;
                if(source->outcomeSnapshot(&outcomes,&error)!=OutcomeSnapshotStatus::Valid ||
                    !outcomes.conserved() || outcomes.rejected || outcomes.stale || outcomes.unknown)return false;
            }
            if(!wait || !pearlSession->hasDeferredSubmission())break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while(true);
        return true;
    };
#endif

    int64_t productionStartNs=0,productionStopNs=0;
    Job job;
    // What prepare() actually depends on. The epoch alone is not enough: a
    // Pearl pool pushes a new header every ~19 seconds with the epoch pinned
    // at 0, so gating on the epoch left the miner hashing the FIRST job's
    // matrices while quoting the newest job id back at the pool. The proof was
    // valid for a header nobody had asked about any more, and the pool answered
    // "Jackpot condition not satisfied: hash does not meet difficulty target".
    // It looked like a Blackwell fault only because a slower card is more
    // likely to have a rotation land between preparing and finding a share.
    PreparedJob prepared;
    uint64_t nonceMask =
        (nonceBits >= 64) ? ~0ULL : ((1ULL << nonceBits) - 1ULL);
    uint64_t nonceCounter = opt.formalBenchmark.enabled
                                ? opt.formalBenchmark.startingNonce
                                : ((uint64_t)time(nullptr) << 16) & nonceMask;
    if (opt.pearlWorkRange.enabled) {
        if(noncePrefix || nonceBits!=64) return 1;
        nonceCounter=opt.pearlWorkRange.first;
        std::printf("{\"event\":\"pearl_work_range\",\"schema\":\"pearl-work-range-v1\","
                    "\"run_id\":\"%s\",\"gpu_uuid\":\"%s\",\"first\":\"%016llx\",\"last\":\"%016llx\"}\n",
                    evidenceRunId?evidenceRunId:"",gpuUuid?gpuUuid:"",
                    static_cast<unsigned long long>(opt.pearlWorkRange.first),
                    static_cast<unsigned long long>(opt.pearlWorkRange.last));
        if (std::fflush(stdout)!=0) return 1;
    }
    uint64_t nonce = noncePrefix | nonceCounter;

    uint64_t intervalHashes = 0;
    uint64_t intervalAttempts = 0;
    uint64_t hostInvalid = 0;   // candidate did not pass our own verifier
    uint64_t submitFailed = 0;  // proof was not acknowledged as delivered
    uint64_t localStale = 0;    // candidate retired/superseded before any send
    bool outcomeInvariantWarned = false;
    const auto tStart = std::chrono::steady_clock::now();
    auto tReport = tStart;
    std::vector<Solution> sols;

    if (opt.bench) {
        job.epoch = opt.benchEpoch;
        if (isPearl) {
            // Pearl owns the complete synthetic fixture: msg and extra are
            // derived from the same exact header in one implementation.
            job = std::move(pearlBenchmark);
        } else {
            memset(job.msg, 0xab, 32);
            for (int i = 0; i < 4; i++) job.target[i] = 0;
            job.valid = true;
        }
    }

    auto tEpoch = tStart;
    std::string prefetchNote;
    bool searchDone = false;  // an iteration finished its search and reconciliation since the loop hook last looked
    bool workWaitDue = false;

    while (!*stop) {
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
        if(!pumpDeferred(false))return 1;
        // Stop can arrive while a deferred reply is being polled. Production
        // drains that owned reply below; do not fetch or launch another search.
        if (opt.pearlProduction && *stop) break;
#endif
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        // Unknown delivery permanently fails this trusted guard session. Stop
        // before fetching or computing more work that it cannot submit.
        if (opt.pearlGuardRetirementSession && opt.pearlGuardRetirementSession->failed()) {
            logLine(tty, "error", "Pearl guard session failed; stopping before further work");
            return 1;
        }
#endif
        if (!sustainedAdmission()) return 1;
        if (sustainedEnabled && sustainedTracker.started()) {
            if (!sustainedTracker.poll(sustainedNowNs(), *stop != 0)) return 1;
            if (sustainedTracker.reached()) break;
        }
        if (opt.bench && opt.benchEpochSeconds > 0) {
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - tEpoch).count() >=
                (double)opt.benchEpochSeconds) {
                job.epoch++;
                tEpoch = now;
            }
        }
        if (opt.loopHook && !opt.bench) {
            const int hookExit = opt.loopHook->atLoopTop(searchDone);
            searchDone = false;
            if (hookExit == kRunFeeWindow || hookExit == kRunFeeWindowDone) {
                if (!algo->discardPearlLookahead()) return 1;
                if(!workCapture.finish())return kRunPauseFault;
                return hookExit;
            }
        }
        const auto beforeWait=pauseBoundary(); // reconcile/apply before the complete-batch delay, never during a search
        if(beforeWait==WorkPauseResult::Stopped)break;
        if(beforeWait==WorkPauseResult::Failed)return kRunPauseFault;
        if (workWaitDue && appliedWorkWaitMs) {
            // No lookahead GPU work may overlap a host-idle interval. Hinting below is disabled in this mode.
            if (!algo->discardPearlLookahead()) return 1;
            const auto result = waitAfterWork(appliedWorkWaitMs, [&] { return *stop != 0||
                (opt.workPause&&opt.workPause->requestState()!=WorkControlRequest::Run); }, [] {
                return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            }, [](uint64_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); });
            if (*stop) break;
            if (result != WorkWaitResult::Ready&&result!=WorkWaitResult::Stopped) return 1;
        }
        workWaitDue = false;
        const auto paused=pauseBoundary(); // previous batch's proofs are reconciled; no fetch/search during the pause
        if(paused==WorkPauseResult::Stopped)break;
        if(paused==WorkPauseResult::Failed)return kRunPauseFault;
        // Fetch again AFTER waiting, so a newly arrived job supersedes old work before any new GPU search.
        if (!opt.bench) {
            Job fresh;
            bool fetched = false;
            if (pearlSession) {
                if (!pearlSession->fetch(&fresh, &fetched)) {
                    logLine(tty, "error", "Pearl source ownership integrity failed");
                    return 1;
                }
            } else fetched = source->fetch(&fresh);
            if (!fetched) {
                if (!algo->discardPearlLookahead()) return 1;
                auto *pp = dynamic_cast<PearlPoolSource *>(source.get());
                if (pp && !pp->lastError().empty()) {
                    logLine(tty, "warn", "Pearl pool " + pp->lastError() +
                                             " - retrying in 5s");
                    sleepSeconds(5);
                    continue;
                }
                logLine(tty, "warn", std::string("cannot reach ") +
                                         source->describe() + " - retrying in 5s");
                sleepSeconds(5);
                continue;
            }
            // Pools may refresh share difficulty without changing the block
            // header. Target and job id must therefore participate in identity;
            // keeping an older target can submit a now-invalid candidate.
            if (!sameRunLoopJobIdentity(fresh, job)) {
                if (!algo->discardPearlLookahead()) return 1;
                const bool newEpoch = fresh.epoch != job.epoch;
                job = fresh;
                if (!newEpoch) stats.jobs++;
            }
        }
        if (!job.valid) { sleepSeconds(1); continue; }
        if(opt.workPause){if(*stop)break;if(opt.workPause->pauseRequested())continue;} // request arrived while fetching
        if (!genericNativeAdmission() || (executionAccounting && !(publicExecution?pearlPublicJobValid(job,opt.pearlPolicy,opt.pearlPublicExecution):pearlSm120NativeJobValid(job,opt.pearlPolicy))) || (genericNativeEnabled &&
                !genericNativeBinding.jobValid(job,opt.pearlPolicy))) {
            logLine(tty,"error","generic native evidence requires unchanged admission and mainnet/base/cert3 job"); return 1;
        }

        // Pearl re-prepares on any material change; PearlPow::prepare() caches on
        // header, target and certificate version, so an unchanged job costs
        // nothing.
        const bool epochChanged = !prepared.valid || job.epoch != prepared.epoch;
        if (pearlSession || shouldPrepare(isPearl, prepared, job)) {
            // Only warn about a wait when there is going to be one. Building
            // ahead makes this instant, and "please wait" followed immediately
            // by "ready in 0.00s" is just noise.
            // Pearl allocates a couple of hundred megabytes in a few milliseconds and has no epoch.
            const double gb = algo->memoryBytes(job) / 1e9;
            if (epochChanged && !algo->prefetchReadyFor(job) && gb >= 1.0) {
                char pre[160];
                snprintf(pre, sizeof(pre),
                         "epoch %llu - building %.2f GB dataset, please wait...",
                         (unsigned long long)job.epoch, gb);
                logLine(tty, "info", pre);
            }
            const auto t0 = std::chrono::steady_clock::now();
            if (*stop || !(pearlSession ? pearlSession->prepare(job) : algo->prepare(job)) || *stop)
                return 1;
            const double secs =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                    .count();
            stats.datasetGB = algo->memoryBytes(job) / 1e9;
            // Only the algorithm knows what one counted unit costs, and it
            // only knows after prepare() has a job. Zero means the unit is
            // the hash and the readout says MH/s.
            stats.macsPerUnit = algo->macsPerUnit();
            stats.workUnitsPerAttempt = algo->workUnitsPerNonce();
            if (executionAccounting) {
                const auto observed=algo->pearlNativeSelection();
                if (!executionSelectionValid(observed) || (privateSelection.valid &&
                    !pearlNativeSelectionEqual(privateSelection,observed))) return 1;
                if (!publicExecution && pearlPrivateGeneric(accountingMode) &&
                    (!privateGenericBinding.observe(observed) || !privateGenericBinding.check(observed,job,opt.pearlPolicy))) return 1;
                if(publicExecution && (!genericNativeBinding.observe(observed) || !genericNativeBinding.check(observed,job,opt.pearlPolicy)))return 1;
                if (!privateSelection.valid) {
                    privateSelection=publicExecution?genericNativeBinding.selection():pearlPrivateGeneric(accountingMode)?privateGenericBinding.selection():observed;
                    std::printf("{\"event\":\"pearl_native_selection\",\"schema\":\"pearl-native-selection-v1\","
                        "\"run_id\":\"%s\",\"native_evidence_mode\":\"%s\",\"%s\":\"%s\","
                        "\"selection_sm\":%u,\"selection_m\":%u,\"selection_n\":%u,\"selection_k\":2048,\"selection_rank\":128,"
                        "\"preparation_plan_id\":%llu,\"work_units_per_attempt\":%llu,\"kernel_id\":\"%.*s\",\"tile_id\":\"%.*s\"}\n",
                        localRetirementSink->runId(),publicExecution?pearlPublicProfile(opt.pearlPublicExecution):pearlPrivateProfile(opt.pearlPrivateExecution),publicExecution?"public_execution":"private_execution",publicExecution?pearlPublicName(opt.pearlPublicExecution):pearlPrivateName(opt.pearlPrivateExecution),
                        observed.sm,observed.m,observed.n,static_cast<unsigned long long>(observed.preparationPlanId),
                        static_cast<unsigned long long>(observed.workUnitsPerAttempt),
                        static_cast<int>(observed.kernelId.size()),observed.kernelId.data(),
                        static_cast<int>(observed.tileId.size()),observed.tileId.data());
                    if (std::fflush(stdout)!=0) return 1;
                }
            } else if (genericNativeEnabled) {
                const bool first=!genericNativeBinding.bound();
                const auto observed=algo->pearlNativeSelection();
                if (!genericNativeBinding.observe(observed) ||
                    !genericNativeBinding.check(observed,job,opt.pearlPolicy) ||
                    stats.workUnitsPerAttempt!=observed.workUnitsPerAttempt) {
                    logLine(tty,"error","invalid or changed generic native prepared selection"); return 1;
                }
                if (first) {
                    std::printf("{\"event\":\"pearl_native_selection\",\"schema\":\"pearl-native-selection-v1\","
                        "\"run_id\":\"%s\",\"native_evidence_mode\":\"%s\","
                        "\"selection_sm\":%u,\"selection_m\":%u,\"selection_n\":%u,\"selection_k\":%u,\"selection_rank\":%u,"
                        "\"preparation_plan_id\":%llu,\"work_units_per_attempt\":%llu,\"kernel_id\":\"%.*s\",\"tile_id\":\"%.*s\"}\n",
                        localRetirementSink->runId(),genericNativeBinding.mode(),observed.sm,observed.m,observed.n,observed.k,observed.rank,
                        static_cast<unsigned long long>(observed.preparationPlanId),
                        static_cast<unsigned long long>(observed.workUnitsPerAttempt),
                        static_cast<int>(observed.kernelId.size()),observed.kernelId.data(),
                        static_cast<int>(observed.tileId.size()),observed.tileId.data());
                }
            } else if ((acceptedQuota || sustainedEnabled) && stats.workUnitsPerAttempt != kPearlAcceptedQuotaWork) {
                logLine(tty,"error","accepted quota requires the exact full-shape attempt"); return 1;
            }
            if (opt.formalBenchmark.enabled) {
                const BenchmarkIdentity observed = algo->benchmarkIdentity();
                const uint64_t expectedCandidates =
                    (uint64_t{observed.m} / 16) * (observed.n / 16);
                if (!benchmarkIdentityMatches(opt.formalBenchmark, observed) ||
                    expectedCandidates == 0 ||
                    stats.workUnitsPerAttempt != expectedCandidates) {
                    logLine(tty, "error",
                            "formal benchmark identity did not exactly match "
                            "the requested Pearl implementation and fixture");
                    return 1;
                }
                stats.benchmarkIdentity = observed;
            }
            char buf[160];
            if (stats.datasetGB >= 1.0)
                snprintf(buf, sizeof(buf), "dataset ready in %.2fs%s - mining",
                         secs, algo->servedFromPrefetch() ? " (built ahead)" : "");
            else
                snprintf(buf, sizeof(buf), "ready in %.2fs (%.0f MB) - mining",
                         secs, stats.datasetGB * 1e3);
            if (epochChanged) logLine(tty, "ok", buf);
            prepared.take(job);
            if (epochChanged) stats.epochs++;

            // Say once what build-ahead decided, and why if it declined.
            const std::string note = algo->prefetchNote();
            if (note != prefetchNote) {
                prefetchNote = note;
                if (!note.empty()) logLine(tty, "info", note);
            }
        }

        if(opt.workPause){if(*stop)break;if(opt.workPause->pauseRequested())continue;} // finish preparation, then pause before search
        sols.clear();
        // Never let a launch cross the owned-subspace boundary. The kernel adds
        // the thread index to `nonce` by plain addition, so a batch straddling
        // the boundary carries into the pool's extranonce prefix and mines
        // nonces we do not own (which the pool then rejects). Shrink the launch
        // that would cross so it lands exactly on the boundary; the next one
        // starts fresh at counter 0. `remaining` wraps correctly when the whole
        // 64-bit nonce is ours (nonceMask all ones), which is the case with no
        // pool prefix - so this is a no-op for solo and for Pearl.
        //
        // `batch` is counted work, not necessarily nonce count. Pearl produces
        // a whole tile grid per nonce, so plan whole attempts and retain both
        // quantities: work drives rate accounting, nonces drive the cursor.
        uint64_t remaining = opt.pearlWorkRange.enabled
            ? opt.pearlWorkRange.remaining(nonceCounter)
            : remainingNonceCount(nonceMask, nonceCounter);
        if (pearlSession)
            remaining = std::min(remaining, PearlRunSession::maximumAttempts);
        const WorkBatch batch = planWorkBatch(
            opt.batch, remaining, algo->workUnitsPerNonce());
        if (!batch.workUnits || !batch.nonces) return 1;
        if (executionAccounting && (!privateAdmission() ||
            !executionSelectionValid(algo->pearlNativeSelection()) ||
            !pearlNativeSelectionEqual(privateSelection,algo->pearlNativeSelection()))) return 1;
        if (!publicExecution && pearlPrivateGeneric(accountingMode)) {
            if (!privateGenericBinding.check(algo->pearlNativeSelection(),job,opt.pearlPolicy) ||
                !pearlGenericAttemptBatch(privateSelection.workUnitsPerAttempt,algo->workUnitsPerNonce(),batch.workUnits,batch.nonces)) return 1;
        } else if (genericNativeEnabled) {
            if (!genericNativeAdmission() ||
                !genericNativeBinding.check(algo->pearlNativeSelection(),job,opt.pearlPolicy) ||
                !pearlGenericAttemptBatch(genericNativeBinding.selection().workUnitsPerAttempt,
                    algo->workUnitsPerNonce(),batch.workUnits,batch.nonces)) {
                logLine(tty,"error","generic native evidence requires one bound full attempt"); return 1;
            }
        } else if ((acceptedQuota || sustainedEnabled) && !pearlAcceptedQuotaBatch(algo->workUnitsPerNonce(),batch.workUnits,batch.nonces)) {
            logLine(tty,"error","accepted quota requires exactly one full attempt per search"); return 1;
        }
        if (opt.formalBenchmark.enabled &&
            !formalBenchmarkNonceRangeFits(nonceCounter, batch.nonces)) {
            logLine(tty, "error",
                    "formal benchmark batch would cross the 64-bit nonce "
                    "boundary - stopping before reuse");
            return 1;
        }
        if (opt.pearlProduction && !productionStartNs) {
            if (!genericNativeAdmission() || *stop) return 1;
            productionStartNs=sustainedNowNs();
            std::printf("{\"event\":\"pearl_production_active_begin\",\"run_id\":\"%s\","
                "\"production_mode\":\"private-v1\",\"active_start_ns\":%lld}\n",
                localRetirementSink->runId(),static_cast<long long>(productionStartNs));
            if (std::fflush(stdout)!=0) return 1;
        }
        if (sustainedEnabled) {
            if (!sustainedAdmission() || *stop) return 1;
            if (!sustainedTracker.started()) {
                if (!sustainedTracker.start(sustainedNowNs()) ||
                    !pearlSession->startGuardDuration(sustainedTracker.activeStartNs())) return 1;
                std::printf("{\"event\":\"pearl_sustained_active_begin\",\"run_id\":\"%s\","
                    "\"sustained_mode\":\"%s\",\"requested_duration_seconds\":%u,"
                    "\"minimum_accepted\":%u,\"active_start_ns\":%lld,\"active_deadline_ns\":%lld}\n",
                    localRetirementSink->runId(), pearlSustainedMode(sustainedPolicy),
                    sustainedPolicy.seconds, sustainedPolicy.minimumAccepted,
                    static_cast<long long>(sustainedTracker.activeStartNs()),
                    static_cast<long long>(sustainedTracker.deadlineNs()));
                if (std::fflush(stdout) != 0) return 1;
            } else {
                if (!sustainedTracker.poll(sustainedNowNs(), *stop != 0)) return 1;
                if (sustainedTracker.reached()) break;
            }
        }
        // Predict only the unchanged, owned cursor. No formal exhaustion prediction; a subsequent actual nonce/job mismatch
        // discards prepared data in the backend. This does not advance work.
        uint64_t rangeNext=0;
        const bool rangeHasNext = !opt.pearlWorkRange.enabled ||
            opt.pearlWorkRange.next(nonceCounter,batch.nonces,&rangeNext);
        const bool hint = !opt.workPause && !appliedWorkWaitMs && rangeHasNext && batch.nonces == 1 && !opt.formalBenchmark.enabled &&
            !*stop && algo && !std::strcmp(algo->name(),"pearl-pow");
        const uint64_t nextHint = opt.pearlWorkRange.enabled ? rangeNext : noncePrefix | ((nonceCounter + batch.nonces) & nonceMask);
        algo->pearlNextAttemptHint(hint,nextHint);
        const uint64_t completedBefore=opt.completedWork?algo->completedSearchAttempts():0;
        const uint64_t exactMacs=opt.completedWork?algo->exactMacsPerUnit():0;
        const auto invalidBefore=hostInvalid;
        if(opt.completedWork&&(!exactMacs||!algo->workUnitsPerNonce()||batch.workUnits/batch.nonces!=algo->workUnitsPerNonce()||
            batch.workUnits%batch.nonces)){return kRunPauseFault;}
        if (!(pearlSession ? pearlSession->search(job, nonce, batch.workUnits, &sols)
                          : algo->search(job, nonce, batch.workUnits, &sols)))
            return 1;
        if (opt.pearlWorkRange.enabled) {
            for (const auto &solution:sols)
                if (!opt.pearlWorkRange.contains(solution.nonce)) return 1;
        }
        if (executionAccounting) {
            const auto execution=algo->pearlPrivateExecutionStats();
            if (!privateAdmission() || !executionSelectionValid(algo->pearlNativeSelection()) ||
                !pearlNativeSelectionEqual(privateSelection,algo->pearlNativeSelection()) ||
                algo->workUnitsPerNonce()!=privateSelection.workUnitsPerAttempt ||
                !execution.conserved() || !execution.lastValid || execution.nonce!=nonce ||
                (pearlPrivateSerial(accountingMode) && (execution.cacheHit || execution.slot))) return 1;
        }
        if (genericNativeEnabled && (!genericNativeAdmission() ||
                !genericNativeBinding.check(algo->pearlNativeSelection(),job,opt.pearlPolicy) ||
                algo->workUnitsPerNonce()!=genericNativeBinding.selection().workUnitsPerAttempt)) {
            logLine(tty,"error","generic native selection changed during search"); return 1;
        }
        if ((acceptedQuota || sustainedEnabled || opt.pearlProduction) && sols.size() > 1) {
            logLine(tty,"error","accepted quota backend emitted more than one solution"); return 1;
        }

        for (const auto &s : sols) {
            char buf[160];
            std::string err;
            bool delivered = false;
            bool hostValid = false;
            if (pearlSession) {
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
                if(!pumpDeferred(true))return 1;
                if(pearlSession->retiredAfterDisconnect()) {
                    if(!algo->discardPearlLookahead())return 1;
                    break;
                }
                const bool submitted=deferredReplies
                    ? pearlSession->submitDeferred(job,s,&err,&delivered,&hostValid)
                    : pearlSession->submit(job,s,&err,&delivered,&hostValid);
#else
                const bool submitted=pearlSession->submit(job,s,&err,&delivered,&hostValid);
#endif
                if (!submitted) {
                    if (pearlSession->localRetirementFailed())
                        fprintf(stderr, "local-retirement evidence failed: %s; stopping without retry\n",
                            localRetirementSink && localRetirementSink->failure()
                                ? localRetirementSink->failure() : "binding");
                    logLine(tty, "error", "Pearl proof/submit ownership integrity failed; stopping");
                    return 1;
                }
            } else hostValid = algo->verify(job, s);
            if (!hostValid) {
                hostInvalid++;
                snprintf(buf, sizeof(buf),
                         "candidate failed host verification (unstable clocks?) "
                         "nonce=%016llx - NOT submitted",
                         (unsigned long long)s.nonce);
                logLine(tty, "error", buf);
                continue;
            }
            if (opt.bench) continue;
            if (!pearlSession) delivered = source->submit(job, s, &err);
            if (executionAccounting && delivered) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
                const auto execution=algo->pearlPrivateExecutionStats();
                if (!privateAdmission() || !execution.conserved() || !s.pearlProofToken || !execution.lastValid || execution.nonce!=s.nonce) return 1;
                char jobHex[65],sourceJobHex[65],sessionHex[33];
                pearlEvidenceHex(job.msg,jobHex);
                PearlPoolCanonicalIdentity sourceIdentity;
                if (!pearlPoolCanonicalIdentityForJob(job,opt.pearlPolicy,&sourceIdentity)) return 1;
                pearlEvidenceHex(sourceIdentity.sourceDigest.bytes.data(),sourceJobHex);
                const char digits[]="0123456789abcdef";
                const auto &session=opt.pearlGuardRetirementSession->session();
                for (unsigned i=0;i<16;++i) {sessionHex[2*i]=digits[session[i]>>4];sessionHex[2*i+1]=digits[session[i]&15];}
                sessionHex[32]='\0';
                const auto proofHex=pearlTranscriptProofDigest(s.extra);
                std::printf("{\"event\":\"%s\",\"run_id\":\"%s\",\"session_id\":\"%s\","
                    "\"%s\":\"%s\",\"proof_token\":%llu,\"nonce\":%llu,\"a_slot\":%u,\"cache_hit\":%s,"
                    "\"job_b3\":\"%s\",\"source_job_b3\":\"%s\",\"proof_b3\":\"%s\"}\n",
                    publicExecution?"pearl_public_execution_proof":"pearl_private_execution_proof",localRetirementSink->runId(),sessionHex,publicExecution?"public_execution":"private_execution",publicExecution?pearlPublicName(opt.pearlPublicExecution):pearlPrivateName(opt.pearlPrivateExecution),
                    static_cast<unsigned long long>(s.pearlProofToken),static_cast<unsigned long long>(s.nonce),
                    execution.slot,execution.cacheHit?"true":"false",jobHex,sourceJobHex,proofHex.c_str());
                if (std::fflush(stdout)!=0) return 1;
#else
                return 1;
#endif
            }
            if (delivered) {
                PoolOutcomeSnapshot outcomes;
                std::string le;
                // Delivery, acknowledgement and acceptance are three
                // different facts.  A Pearl gateway returns "submitted" when
                // it queued the proof for asynchronous proving; that is an
                // acknowledgement with an unknown final outcome, never an
                // accepted share.  Only a source with no outcome channel at
                // all (the synchronous solo node path) can use true here as
                // the terminal acceptance result.
                const OutcomeSnapshotStatus postSubmitStatus =
                    source->outcomeSnapshot(&outcomes, &le);
                if (postSubmitStatus == OutcomeSnapshotStatus::Unsupported) {
                    recordSynchronousAcceptance(&stats);
                    snprintf(buf, sizeof(buf),
                             "SOLUTION ACCEPTED  nonce=%016llx",
                             (unsigned long long)s.nonce);
                    logLine(tty, "ok", buf);
                } else if (postSubmitStatus == OutcomeSnapshotStatus::Invalid) {
                    if (!outcomeInvariantWarned) {
                        logLine(tty, "error",
                                "submission returned success but remote share "
                                "accounting was invalid; NOT counted accepted");
                        outcomeInvariantWarned = true;
                    }
                } else {
                    // Deliberately silent. A share per second is normal on an
                    // easy pool target - BC3 at difficulty 0.01 submitted 537 in
                    // one minute - and a line each buries the readout, the
                    // rejections and the connection messages that actually need
                    // reading. The periodic report already carries accepted,
                    // rejected, hashrate and watts. Set CANARY_LOG_SHARES=1 to get
                    // the per-share line back for diagnosis.
                    static const bool logShares = getenv("CANARY_LOG_SHARES") != nullptr;
                    if (logShares) {
                        snprintf(buf, sizeof(buf),
                                 "share delivered; remote outcome tracked "
                                 "separately   nonce=%016llx",
                                 (unsigned long long)s.nonce);
                        logLine(tty, "info", buf);
                    }
                }
            } else {
                const PearlSubmitFailureKind failure = pearlSubmitFailureKind(err);
                if (failure == PearlSubmitFailureKind::LocalStale) localStale++;
                else if (failure == PearlSubmitFailureKind::DeliveryFailure) submitFailed++;
                const char *failureLabel = failure == PearlSubmitFailureKind::LocalStale
                    ? "candidate retired/superseded before sending (local stale)"
                    : failure == PearlSubmitFailureKind::DeliveryFailure
                        ? "share delivery failed before acknowledgement"
                        : "local submission refused";
                snprintf(buf, sizeof(buf),
                         "%s  nonce=%016llx  %s", failureLabel,
                         (unsigned long long)s.nonce, err.c_str());
                logLine(tty, "error", buf);
            }
            // Pearl submits synchronously and exposes one-shot diagnostics.
            // Drain after EACH terminal attempt: a following timeout in the
            // same batch must not overwrite a stale/rejection warning.
            if (pearlSession) {
                const std::string rejection = source->takeRejectReason();
                if (!rejection.empty())
                    logLine(tty, "error", "pool REJECTED the share: " + rejection);
                const std::string warning = source->takeJobWarning();
                if (!warning.empty()) logLine(tty, "warn", warning);
            }
            if (pearlSession && pearlSession->retiredAfterDisconnect()) {
                if (!algo->discardPearlLookahead()) return 1;
                logLine(tty, "warn", "Pearl connection closed; remaining proofs retired, reconnecting");
                break;
            }
        }

        if (pearlSession && !pearlSession->finishSearch()) {
            logLine(tty, "error", "Pearl proof ownership reconciliation failed");
            // H-019 D3: a non-transport stop that left the source integrity-clean is restartable; anything else is not.
            const bool restartable = !pearlSession->retiredAfterDisconnect() && !pearlSession->snapshot().sourceQuarantined;
            return restartable ? kRunGuardStop : 1;
        }
        if(opt.completedWork){const auto completedAfter=algo->completedSearchAttempts();
            if(completedAfter<completedBefore||completedAfter-completedBefore!=batch.nonces||algo->exactMacsPerUnit()!=exactMacs||
                batch.workUnits/batch.nonces!=algo->workUnitsPerNonce()||
                !opt.completedWork->completed({completedWorkNow(),batch.workUnits,batch.nonces,exactMacs,appliedWorkWaitMs,hostInvalid!=invalidBefore}))
                return kRunPauseFault;
        }
        searchDone = job.valid;
        workWaitDue = job.valid;
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
        // Accepted-quota qualification must settle the submitted proof before
        // deciding whether another search is allowed to start.
        if(acceptedQuota && !pumpDeferred(true))return 1;
#endif

        uint64_t formalNextNonce = 0;
        const bool formalNonceExhausted =
            opt.formalBenchmark.enabled &&
            !advanceFormalBenchmarkNonce(nonceCounter, batch.nonces,
                                         &formalNextNonce);
        const bool workRangeExhausted = opt.pearlWorkRange.enabled && !rangeHasNext;
        if (!formalNonceExhausted && !workRangeExhausted) {
            nonceCounter = opt.pearlWorkRange.enabled ? rangeNext : opt.formalBenchmark.enabled
                               ? formalNextNonce
                               : (nonceCounter + batch.nonces) & nonceMask;
            nonce = noncePrefix | nonceCounter;
        }
        intervalHashes += batch.workUnits;
        intervalAttempts += batch.nonces;
        if ((genericNativeEnabled || privateExecution) && (stats.totalNonces >= UINT64_MAX-batch.workUnits ||
                stats.totalAttempts >= UINT64_MAX-batch.nonces)) {
            logLine(tty,"error","generic native work accounting overflow"); return 1;
        }
        stats.totalNonces += batch.workUnits;
        stats.totalAttempts += batch.nonces;

        // Surface each pool rejection once, with the pool's own words. Without
        // this a miner that is being refused every share looks identical to
        // one that is being paid for every share.
        //
        // Straight through the JobSource interface, no casts. This used to be
        // a dynamic_cast chain naming each stratum type, and adding a type
        // without extending the chain left its counters at zero while the pool
        // credited shares - which happened for BC3 (367 shares, readout said
        // zero) and then again for KawPow (263 shares, readout said zero). A
        // cast chain is a hand-maintained list of types; the interface is not.
        // Guarded because benchmark mode has no JobSource at all: `source` is
        // a unique_ptr that is simply never assigned. The dynamic_cast chain
        // this block replaced was null-safe by accident - casting a null
        // pointer yields null and the old code tested it - so removing the
        // chain removed a guard nobody had written down. Bench mode segfaulted
        // on the first stats interval, and the pool path masked it because
        // that was the only path retested after the change.
        if (source) {
            const std::string v = source->takeRejectReason();
            if (!v.empty())
                logLine(tty, "error", "pool REJECTED the share: " + v);
            const std::string jw = source->takeJobWarning();
            if (!jw.empty()) logLine(tty, "warn", jw);
            PoolOutcomeSnapshot outcomes;
            std::string le;
            const OutcomeSnapshotStatus outcomeStatus =
                source->outcomeSnapshot(&outcomes, &le);
            const bool sustainedObserved=!sustainedEnabled ||
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
                (deferredReplies ? sustainedTracker.observeDeferred(sustainedNowNs(),outcomeStatus,
                    outcomes,sols.size(),*stop!=0) :
#endif
                sustainedTracker.observe(sustainedNowNs(),outcomeStatus,outcomes,sols.size(),*stop!=0)
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
                )
#endif
                ;
            if (!sustainedObserved) {
                logLine(tty,"error","sustained duration requires conserved terminal outcomes"); return 1;
            }
            if (acceptedQuota && !quotaTracker.observe(outcomeStatus,outcomes,sols.size())) {
                logLine(tty,"error","accepted quota requires conserved exact terminal outcomes"); return 1;
            }
            if (outcomeStatus == OutcomeSnapshotStatus::Invalid) {
                if (!outcomeInvariantWarned) {
                    logLine(tty, "error",
                            "remote share counters violated conservation; "
                            "ignoring the inconsistent snapshot");
                    outcomeInvariantWarned = true;
                }
            } else if (outcomeStatus == OutcomeSnapshotStatus::Valid) {
                stats.submitted = outcomes.submitted;
                stats.acknowledged = outcomes.acknowledged;
                stats.accepted = outcomes.accepted;
                stats.poolRejected = outcomes.rejected;
                stats.stale = outcomes.stale;
                stats.unknown = outcomes.unknown;
                stats.pending = outcomes.pending;
                stats.acceptedSuperseded = outcomes.acceptedSuperseded;
            }
        }
        stats.hostInvalid = hostInvalid;
        stats.submitFailed = submitFailed;
        stats.localStale = localStale;
        stats.rejected = stats.poolRejected;
        // Every proof reconciled and the final batch counted. Do not fetch
        // again: the guard holds the socket until our diagnostic close.
        if ((acceptedQuota && quotaTracker.reached()) || (sustainedEnabled && sustainedTracker.reached())) {
            if (*stop) { logLine(tty,"error","external stop raced quota acceptance; completion incomplete"); return 1; }
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const double el = std::chrono::duration<double>(now - tReport).count();
        if (el >= (double)opt.reportSeconds) {
            stats.uptimeNanoseconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - tStart).count());
            stats.uptimeSeconds = static_cast<double>(stats.uptimeNanoseconds) / 1e9;
            stats.hashrate = intervalHashes / el / 1e6;
            stats.attemptsPerSecond = intervalAttempts / el;
            stats.hashrateAvg =
                stats.uptimeSeconds > 0
                    ? stats.totalNonces / stats.uptimeSeconds / 1e6
                    : 0.0;
            stats.attemptsPerSecondAvg =
                stats.uptimeSeconds > 0
                    ? stats.totalAttempts / stats.uptimeSeconds
                    : 0.0;
            stats.epoch = job.epoch;
            stats.pushSample(stats.hashrate);
            // Sampled ONCE and reused: calling gpu.sample() twice would
            // accumulate one reading and display a different one.
            const GpuTelemetry sample = gpu.sample();
            if (sample.valid)
                stats.addPowerSample(sample.powerMilliwatts / 1000.0, el);
            printReadout(stats, sample, tty);
            if (opt.observer) opt.observer->onReport(stats, sample);
            intervalHashes = 0;
            intervalAttempts = 0;
            tReport = now;
        }
        if (workRangeExhausted) {
            logLine(tty,"error","Pearl work range exhausted; stopping before reuse"); return 1;
        }
        if (formalNonceExhausted) {
            logLine(tty, "error",
                    "formal benchmark exhausted the 64-bit nonce stream - "
                    "stopping before reuse");
            return 1;
        }
    }

#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
    if (opt.pearlProduction) {
        productionStopNs=sustainedNowNs();
        if (!opt.pearlGuardRetirementSession ||
            !opt.pearlGuardRetirementSession->sealProduction(static_cast<uint64_t>(productionStopNs/1000000))) return 1;
    }
    if(!pumpDeferred(true,*stop!=0 && !opt.pearlProduction))return 1;
    if(deferredReplies && sustainedEnabled) {
        PoolOutcomeSnapshot finalOutcomes;std::string error;
        const auto status=source->outcomeSnapshot(&finalOutcomes,&error);
        if(!sustainedTracker.settleDeferred(sustainedNowNs(),status,finalOutcomes,*stop!=0))return 1;
    }
#endif
    if (opt.pearlProduction) {
        if (!genericNativeAdmission() || !productionStartNs || !*stop) return 1;
        if (!productionStopNs) productionStopNs=sustainedNowNs();
        std::printf("{\"event\":\"pearl_production_stop_admitted\",\"run_id\":\"%s\","
            "\"stop_admitted_ns\":%lld,\"total_attempts\":%llu,\"total_work_units\":%llu,\"stop_reserved\":%llu}\n",
            localRetirementSink->runId(),static_cast<long long>(productionStopNs),
            static_cast<unsigned long long>(stats.totalAttempts),static_cast<unsigned long long>(stats.totalNonces),
            static_cast<unsigned long long>(opt.pearlGuardRetirementSession->productionSealReserved()));
        if (std::fflush(stdout)!=0) return 1;
    }
    PearlRunFinalEvidence finalEvidence;
    if (sustainedEnabled && (!sustainedAdmission() || !sustainedTracker.reached() || *stop)) {
        logLine(tty,"error","sustained duration incomplete"); return 1;
    }
    if (sustainedEnabled) {
        std::printf("{\"event\":\"pearl_sustained_stop_admitted\",\"run_id\":\"%s\","
            "\"stop_admitted_ns\":%lld,\"total_attempts\":%llu,\"total_work_units\":%llu}\n",
            localRetirementSink->runId(), static_cast<long long>(sustainedTracker.stopNs()),
            static_cast<unsigned long long>(stats.totalAttempts),
            static_cast<unsigned long long>(stats.totalNonces));
        if (std::fflush(stdout) != 0) return 1;
    }
    if (acceptedQuota && (!quotaTracker.reached() || *stop)) {
        logLine(tty,"error","accepted quota not reached; completion remains incomplete"); return 1;
    }
    if (!algo->discardPearlLookahead()) return 1;
    if (localRetirementOptIn) {
        if (!pearlSession || !localRetirementSink ||
            !pearlSession->finishEvidenceRun(localRetirementSink->records(),&finalEvidence,
                sustainedEnabled ? sustainedTracker.stopNs() : productionStopNs)) {
            fprintf(stderr,"native evidence quiescence/source close failed; qualification incomplete\n"); return 1;
        }
        // No borrowed callbacks or unchecked source close may outlive final evidence.
        pearlSession.reset(); source.reset();
    }
    // Shutdown output, ordered so it does not collide with whatever the shell
    // draws next. Ctrl+C is delivered to every process sharing the console, so
    // the shell starts redrawing its prompt - and cmd.exe starts asking
    // "Terminate batch job (Y/N)?" - while this is still printing. Without a
    // flush and a clear line break the two interleave and the prompt lands in
    // the middle of a word.
    //
    // On a TTY, \r plus erase-to-end-of-line clears the partially drawn
    // readout row first, so the last thing on screen is a whole line rather
    // than a stump with a prompt hanging off it.
    if (tty) {
        printf("\r\033[K\n");
        fflush(stdout);
    }
    logLine(tty, "info", "stopping");
    fflush(stdout);
    // Put the card back the way it was found.
    if (genericNativeEnabled && (!genericNativeAdmission() || !genericNativeBinding.bound() ||
            !genericNativeBinding.check(algo->pearlNativeSelection(),job,opt.pearlPolicy))) {
        logLine(tty,"error","generic native final selection/admission invalid"); return 1;
    }
    if (opt.memOffsetMhz != 0) gpu.applyMemOffsetMhz(0);
    if (executionAccounting) {
        privateFinalStats=algo->pearlPrivateExecutionStats();
        if (!privateAdmission() || !executionSelectionValid(algo->pearlNativeSelection()) ||
            !pearlNativeSelectionEqual(privateSelection,algo->pearlNativeSelection()) ||
            !privateFinalStats.terminal(accountingMode,stats.totalAttempts)) return 1;
    }
    algo->release();
    gpu.close();
    if (localRetirementOptIn) {
        PearlEvidenceDigest localDigest;
        PearlSustainedFinal sustainedFinal;
        if (!genericNativeAdmission() || ((acceptedQuota || sustainedEnabled) && *stop) || !localRetirementSink->finish(&localDigest) ||
            std::ferror(stdout) || std::ferror(stderr) || std::fflush(stdout)!=0 || std::fflush(stderr)!=0 ||
            ((acceptedQuota || sustainedEnabled) && *stop) ||
            (sustainedEnabled && (!sustainedAdmission() ||
                !sustainedTracker.finish(sustainedNowNs(), *stop != 0, &sustainedFinal))) ||
            !nativeCompletion.finish(localRetirementSink->runId(),finalEvidence,
                localDigest,localRetirementSink->records(),hostInvalid,submitFailed,stats.totalNonces,stats.totalAttempts,
                acceptedQuota, sustainedEnabled ? &sustainedFinal : nullptr,
                executionAccounting ? &privateSelection : genericNativeEnabled ? &genericNativeBinding.selection() : nullptr,
                opt.pearlPrivateExecution,privateExecution ? &privateFinalStats : nullptr,
                opt.pearlPublicExecution,publicExecution ? &privateFinalStats : nullptr,opt.pearlProduction)) {
            fprintf(stderr,"native final evidence flush/close failed; qualification incomplete\n"); return 1;
        }
    }
    if (!genericNativeAdmission() || (sustainedEnabled && (*stop || !sustainedAdmission()))) return 1;
    return workCapture.finish()?0:kRunPauseFault;
}

}  // namespace om
