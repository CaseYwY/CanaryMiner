// Host-only admission and actual runMiner early-refusal tests.
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "../src/core/run.h"
#include "../src/algos/pearl-pow/cuda_preparation_plan.h"

using namespace om;

// Link with --wrap=dlopen/socket/connect: touching telemetry or networking in
// any rejected runtime case is a hard test failure, never a real device probe.
static bool inertCompletion=false;
static unsigned inertDlopenCalls=0;
extern "C" void *__wrap_dlopen(const char *, int) {
    if (!inertCompletion) std::abort();
    ++inertDlopenCalls; return nullptr; // no real library or AMD fallback
}
extern "C" int __wrap_socket(int, int, int) { std::abort(); }
extern "C" int __wrap_connect(int, const void *, unsigned) { std::abort(); }

struct ConfigureReached {};
// Test-only environment isolation; preserve presence even for empty values.
class SavedEnvironment final {
 public:
    explicit SavedEnvironment(const char *name) : name_(name), present_(getenv(name)!=nullptr),
        value_(present_ ? getenv(name) : "") {
        if (unsetenv(name_) != 0) std::abort();
    }
    ~SavedEnvironment() {
        if ((present_ ? setenv(name_,value_.c_str(),1) : unsetenv(name_)) != 0) std::abort();
    }
    SavedEnvironment(const SavedEnvironment &) = delete;
    SavedEnvironment &operator=(const SavedEnvironment &) = delete;
 private:
    const char *name_;
    bool present_;
    std::string value_;
};

class FakeAlgorithm final : public Algorithm {
 public:
    const char *algorithmName = "pearl-pow";
    PearlRuntimeCapability capability = PearlRuntimeCapability::CanonicalCudaPoolV1;
    int configured = 0;
    mutable int fixtureCalls = 0;
    int fixtureMode = 0;
    bool experimentCompiled = false;
    bool stopAtConfigure = false;
    bool failStdoutAtRelease=false;
    pearl::CudaFixedExperimentSelection experimentSelection = pearl::CudaFixedExperimentSelection::Disabled;
    const char *name() const override { return algorithmName; }
    PearlRuntimeCapability pearlRuntimeCapability() const noexcept override { return capability; }
    bool pearlExperimentConfigurationValid(const RunOptions &opt, const char *selection) const noexcept override {
        return pearl::parseCudaFixedExperiment(selection) == experimentSelection &&
            pearl::cudaFixedExperimentConfigurationValid(experimentSelection, experimentCompiled,
                opt.formalBenchmark.enabled, opt.bench, opt.poolHost, opt.poolPort,
                getenv("CANARY_PEARL_SHAPE") != nullptr, getenv("CANARY_PEARL_TILE") != nullptr,
                getenv("CANARY_PEARL_ATTEMPT_OVERLAP") != nullptr,
                getenv("CANARY_PEARL_A_ROOT_FUSION") != nullptr,
                getenv("CANARY_PEARL_PROFILE_INTERVAL") != nullptr);
    }
    size_t memoryBytes(const Job &) const override { std::abort(); }
    bool prepare(const Job &) override { std::abort(); }
    bool search(const Job &, uint64_t, uint64_t, std::vector<Solution> *) override { std::abort(); }
    bool verify(const Job &, const Solution &) const override { std::abort(); }
    void configure(const RunOptions &) override {
        ++configured;
        if (stopAtConfigure) throw ConfigureReached{};
    }
    bool setPearlProgressSink(PearlAlgorithmProgressSink *) override { return inertCompletion; }
    bool setPearlProofBindingSink(PearlProofBindingSink *) override { return inertCompletion; }
    void release() override {
        if (failStdoutAtRelease) {
            const int fd=::open("/dev/full",O_WRONLY);
            if (fd<0 || dup2(fd,fileno(stdout))<0) std::abort();
            ::close(fd);
            std::fputs("injected stdout failure\n",stdout);
        }
    }
    void fillBenchJob(Job *job) const override {
        ++fixtureCalls;
        if (fixtureMode == 1) return;
        if (fixtureMode == 2) throw std::runtime_error("injected fixture failure");
        if (!buildPearlSyntheticBenchmarkJob(job)) std::abort();
        if (fixtureMode == 3) job->target[0] = 1;
        if (fixtureMode == 4) job->valid = false;
        if (fixtureMode == 5) job->epoch++;
    }
};

bool check(bool value, const char *what) {
    if (!value) std::fprintf(stderr, "FAIL: %s\n", what);
    return value;
}

RunOptions live() {
    RunOptions opt;
    opt.poolHost = "offline.invalid";
    opt.poolPort = 1;
    opt.wallet = "prl1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq";
    opt.pearlPolicy = {PearlNetwork::Mainnet, PearlTargetSemantics::BaseBeforePenalty};
    return opt;
}

bool localRetirementRuntimeAdmission() {
    SavedEnvironment local("CANARY_PEARL_LOCAL_RETIREMENTS"), runId("CANARY_PEARL_EVIDENCE_RUN_ID"), experiment("CANARY_PEARL_EXPERIMENT"),
        shape("CANARY_PEARL_SHAPE"), tile("CANARY_PEARL_TILE"), overlap("CANARY_PEARL_ATTEMPT_OVERLAP"),
        fusion("CANARY_PEARL_A_ROOT_FUSION"), profile("CANARY_PEARL_PROFILE_INTERVAL"),
        allHit("CANARY_PEARL_BENCH_ALL_HIT"), hard("CANARY_PEARL_HARD_TARGET");
    struct Directory {
        char path[64] = "/tmp/pearl-retirement-admission-XXXXXX";
        bool created = false;
        Directory() { created = mkdtemp(path) != nullptr; }
        ~Directory() {
            if (!created) return;
            // Only exact test-created paths under this mkdtemp directory.
            char file[160];
            std::snprintf(file,sizeof(file),"%s/wire.local-retirements.jsonl",path);
            if (unlink(file)!=0 && errno!=ENOENT) std::abort();
            std::snprintf(file,sizeof(file),"%s/wire.completion.json",path);
            if (unlink(file)!=0 && errno!=ENOENT) std::abort();
            std::snprintf(file,sizeof(file),"%s/wire",path);
            if (unlink(file)!=0 && errno!=ENOENT) std::abort();
            if (rmdir(path)!=0) std::abort();
        }
    } directory;
    if (!check(directory.created,"private admission fixture directory created")) return false;
    bool ok = true;
    FakeAlgorithm algo;
    volatile sig_atomic_t stop = 1;
    auto request = live(); request.pearlTranscript = std::string(directory.path)+"/wire";
    const std::string sidecar = request.pearlTranscript+".local-retirements.jsonl";
    const std::string completion=request.pearlTranscript+".completion.json";
    const char *goodId="0123456789abcdef0123456789abcdef";
    auto missing = [&](const std::string &path) {
        struct stat status{};
        return lstat(path.c_str(),&status)==-1 && errno==ENOENT;
    };
    auto setFlag = [&](const char *value) {
        if ((value ? setenv("CANARY_PEARL_LOCAL_RETIREMENTS",value,1) :
                     unsetenv("CANARY_PEARL_LOCAL_RETIREMENTS")) != 0) std::abort();
    };
    auto reject = [&](const RunOptions &opt, const char *label) {
        const int before = algo.configured, fixtures = algo.fixtureCalls;
        bool returned = false;
        try { returned = runMiner(&algo,opt,"must-not-probe",0,"test",&stop)==1; }
        catch (...) { returned = false; }
        ok &= check(returned && algo.configured==before && algo.fixtureCalls==fixtures,label);
        ok &= check(missing(request.pearlTranscript),"rejected admission creates no wire transcript");
    };
    auto configureOnly = [&](const RunOptions &opt, const char *label) {
        const int before = algo.configured, fixtures = algo.fixtureCalls;
        algo.stopAtConfigure = true;
        bool reached = false;
        try { (void)runMiner(&algo,opt,"must-not-probe",0,"test",&stop); }
        catch (const ConfigureReached &) { reached = true; }
        catch (...) {}
        algo.stopAtConfigure = false;
        ok &= check(reached && algo.configured==before+1 && algo.fixtureCalls==fixtures,label);
        ok &= check(missing(request.pearlTranscript),"configure sentinel precedes source transcript setup");
    };
    // Prove absence is admitted without creating the companion, not merely a
    // rejection by a different condition. Throw at configure before telemetry.
    configureOnly(request,"absent retirement opt-in reaches actual configure sentinel");
    ok &= check(missing(sidecar) && missing(completion),"absent retirement opt-in creates no evidence files");
    if (setenv("CANARY_PEARL_EVIDENCE_RUN_ID",goodId,1)!=0) std::abort();
    for (const char *value : {"", "0", "true", "01", "1 ", " 1", "1\n", "2"}) {
        setFlag(value); reject(request,"invalid retirement opt-in rejected before configure");
        ok &= check(missing(sidecar),"invalid flag creates no companion file");
    }
    setFlag("1");
    for (const char *id : {static_cast<const char *>(nullptr),"","0","0123456789abcdef0123456789abcdeF",
                           "0123456789abcdef0123456789abcdef0","0123456789abcdef0123456789abcdeg"}) {
        if ((id ? setenv("CANARY_PEARL_EVIDENCE_RUN_ID",id,1) : unsetenv("CANARY_PEARL_EVIDENCE_RUN_ID"))!=0) std::abort();
        reject(request,"missing or malformed completion run ID refused before configure");
        ok &= check(missing(sidecar) && missing(completion),"bad run ID creates no evidence");
    }
    if (setenv("CANARY_PEARL_EVIDENCE_RUN_ID",goodId,1)!=0) std::abort();
    for (unsigned mode=0; mode<10; ++mode) {
        auto opt = request;
        switch (mode) {
            case 0: opt.pearlTranscript.clear(); break;
            case 1: opt.pearlTranscript.assign(4097,'x'); break;
            case 2: opt.pearlTranscript += std::string("\0hidden",7); break;
            case 3: opt.bench = true; break;
            case 4: opt.formalBenchmark.enabled = true; break;
            case 5: opt.poolHost.clear(); break;
            case 6: algo.algorithmName = "autolykos2"; break;
            case 7: algo.capability = PearlRuntimeCapability::Unsupported; break;
            case 8: opt.wallet = "invalid"; break;
            case 9: opt.pearlPolicy.network = PearlNetwork::Testnet; break;
        }
        reject(opt,"retirement opt-in does not broaden canonical admission or path validity");
        ok &= check(missing(sidecar),"invalid canonical request creates no companion file");
        algo.algorithmName = "pearl-pow";
        algo.capability = PearlRuntimeCapability::CanonicalCudaPoolV1;
    }
    auto missingParent = request;
    missingParent.pearlTranscript = std::string(directory.path)+"/missing/wire";
    reject(missingParent,"companion open failure stops before configure");
    ok &= check(missing(sidecar),"failed open does not create another companion");
    configureOnly(request,"exact retirement opt-in creates evidence then reaches configure sentinel");
    struct stat status{};
    ok &= check(lstat(sidecar.c_str(),&status)==0 && S_ISREG(status.st_mode) &&
                status.st_size>0 && (status.st_mode & 0777)==0600,
                "actual run loop creates private run-header sidecar before configure");
    ok &= check(lstat(completion.c_str(),&status)==0 && S_ISREG(status.st_mode) && status.st_size==0,
                "actual run loop reserves empty completion, never completes before configure");
    // Preserve a pre-existing artifact byte-for-byte when the exact opt-in is
    // repeated. The caller never reaches configure and never truncates it.
    const char retained[] = "retained admission evidence\n";
    FILE *file = std::fopen(sidecar.c_str(),"wb");
    if (!file) ok &= check(false,"open own companion test artifact");
    else {
        ok &= check(std::fwrite(retained,1,sizeof(retained)-1,file)==sizeof(retained)-1,
                    "seed existing companion artifact");
        ok &= check(std::fclose(file)==0,"close existing companion artifact");
    }
    reject(request,"existing companion is refused before configure without overwrite");
    file = std::fopen(sidecar.c_str(),"rb");
    char bytes[sizeof(retained)]{};
    if (!file) ok &= check(false,"read preserved companion artifact");
    else {
        const auto size = std::fread(bytes,1,sizeof(bytes),file);
        ok &= check(size==sizeof(retained)-1 && !std::memcmp(bytes,retained,sizeof(retained)-1),
                    "existing companion bytes preserved exactly");
        ok &= check(std::fclose(file)==0,"close preserved companion readback");
    }
    // Execute real zero-work normal shutdown with a null-only dlopen stub,
    // exact expected NVIDIA UUID (therefore no AMD/sysfs fallback), and the
    // stop flag already set. Network wrappers remain unconditional aborts.
    for (unsigned fault=0; fault<2; ++fault) {
        for (const auto &path : {sidecar,completion,request.pearlTranscript})
            if (unlink(path.c_str())!=0 && errno!=ENOENT) std::abort();
        struct RestoreStdout {
            int saved=-1;
            RestoreStdout() {
                if (std::fflush(stdout)!=0) std::abort();
                saved=dup(fileno(stdout)); if (saved<0) std::abort();
            }
            ~RestoreStdout() {
                (void)std::fflush(stdout);
                if (dup2(saved,fileno(stdout))<0) std::abort();
                ::close(saved); std::clearerr(stdout);
                inertCompletion=false;
            }
        } restore;
        inertCompletion=true; algo.failStdoutAtRelease=fault!=0;
        const unsigned probes=inertDlopenCalls;
        const int rc=runMiner(&algo,request,"inert-no-device",0,"test",&stop,
                             "GPU-00000000-0000-0000-0000-000000000000",0);
        ok &= check(rc==(fault?1:0) && inertDlopenCalls>probes,
                    "actual zero-work finish returns success only after checked outputs");
        struct stat finalStat{};
        ok &= check(lstat(completion.c_str(),&finalStat)==0 &&
                    (fault ? finalStat.st_size==0 : finalStat.st_size>0),
                    "stdout fault leaves reserved completion empty; successful finish writes final snapshot");
        algo.failStdoutAtRelease=false;
    }
    return ok;
}

int main() {
    // An inherited opt-in must not turn unrelated legacy tests into early
    // refusal false positives. Restore its exact original presence/value.
    SavedEnvironment inheritedRetirement("CANARY_PEARL_LOCAL_RETIREMENTS");
    SavedEnvironment inheritedIntegrated("CANARY_PEARL_INTEGRATED_PIPELINE");
    SavedEnvironment productFixed("CANARY_PEARL_PRODUCT_FIXED_SELECTION");
    // Actual run-loop refusal: inert wrappers abort on device/network access.
    for (const char *value : {"", "next-attempt-v1", "same-search-v1"}) {
        if (setenv("CANARY_PEARL_INTEGRATED_PIPELINE", value, 1) != 0) return 1;
        FakeAlgorithm missingCapability;
        volatile sig_atomic_t stopped = 1;
        if (runMiner(&missingCapability, live(), "must-not-probe", 0, "test", &stopped) != 1 ||
            missingCapability.configured != 0) return 1;
    }
    if (unsetenv("CANARY_PEARL_INTEGRATED_PIPELINE") != 0) return 1;
    bool ok = true;
    const auto cuda = PearlRuntimeCapability::CanonicalCudaPoolV1;
    FakeAlgorithm algo;
    volatile sig_atomic_t stop = 1;
    auto rejected = [&](const RunOptions &opt, const char *what) {
        const int before = algo.configured;
        ok &= check(pearlRuntimeAdmissionProblem(opt, algo.name(), algo.capability) != nullptr, what);
        ok &= check(runMiner(&algo, opt, "must-not-probe", 0, "test", &stop) == 1, what);
        ok &= check(algo.configured == before, "refusal must precede configure");
    };
    ok &= check(!pearlRuntimeAdmissionProblem(live(), "pearl-pow", cuda), "live canonical pool admitted");
    auto waiting = live();
    waiting.workWaitMs = 1001;
    rejected(waiting, "host wait range refused before device/source");
    waiting.workWaitMs = 1;
    waiting.bench = true;
    rejected(waiting, "host wait benchmark combination refused before device/source");
    waiting = live();
    waiting.workWaitMs = 200;
    for (const char *name : {"CANARY_PEARL_LOCAL_RETIREMENTS", "CANARY_PEARL_NATIVE_EVIDENCE", "CANARY_PEARL_INTEGRATED_PIPELINE",
            "CANARY_PEARL_SHAPE_EXPERIMENT", "CANARY_PEARL_ATTEMPT_OVERLAP", "CANARY_PEARL_A_ROOT_FUSION", "CANARY_PEARL_EXPERIMENT",
            "CANARY_PEARL_PROFILE_INTERVAL", "CANARY_PEARL_ASYNC_SUBMIT"}) {
        SavedEnvironment isolated(name);
        setenv(name, "1", 1);
        const int configuredBefore = algo.configured;
        ok &= check(runMiner(&algo, waiting, "must-not-probe", 0, "test", &stop) == 1 && algo.configured == configuredBefore,
                    "host wait/evidence selector combination refused before configure/device/source");
    }
    for (auto network : {PearlNetwork::Mainnet, PearlNetwork::Testnet,
                         PearlNetwork::Testnet2, PearlNetwork::Regtest}) {
        for (auto semantics : {PearlTargetSemantics::BaseBeforePenalty,
                               PearlTargetSemantics::FinalAdjustedBound}) {
            auto opt = live(); opt.pearlPolicy = {network, semantics};
            if (network == PearlNetwork::Mainnet)
                ok &= check(!pearlRuntimeAdmissionProblem(opt, "pearl-pow", cuda), "mainnet semantics matrix");
            else
                rejected(opt, "non-mainnet live policy fails before setup");
        }
    }
    for (unsigned invalid : {0U, 255U}) {
        auto opt = live(); opt.pearlPolicy.network = static_cast<PearlNetwork>(invalid);
        rejected(opt, "invalid network");
        opt = live(); opt.pearlPolicy.targetSemantics = static_cast<PearlTargetSemantics>(invalid);
        rejected(opt, "invalid semantics");
    }
    auto opt = live(); opt.pearlPolicy.network = PearlNetwork::SyntheticBenchmark;
    rejected(opt, "synthetic policy is not a live network");
    opt = live(); opt.poolHost.clear(); rejected(opt, "explicit pool required");
    opt = live(); opt.poolPort = 65536; rejected(opt, "port range");
    opt = live(); opt.wallet.clear(); rejected(opt, "wallet required");
    opt = live(); opt.wallet = "not-a-Pearl-address";
    ok &= check(runMiner(&algo, opt, "must-not-probe", 0, "test", &stop) == 1 &&
                algo.configured == 0, "invalid wallet fails before configure/telemetry/source");
    opt = live(); opt.backendLabel = "CUDA";
    algo.capability = PearlRuntimeCapability::Unsupported;
    rejected(opt, "forged CUDA display label is not a capability");
    algo.capability = static_cast<PearlRuntimeCapability>(255);
    rejected(opt, "unknown capability rejected");
    algo.capability = cuda;
    opt.backendLabel = "not-a-backend";
    ok &= check(!pearlRuntimeAdmissionProblem(opt, "pearl-pow", cuda), "display label not authority");
    RunOptions bench; bench.bench = true;
    ok &= check(!pearlRuntimeAdmissionProblem(bench, "pearl-pow", cuda), "synthetic benchmark admitted");
    ok &= check(!pearlRuntimeAdmissionProblem(bench, "pearl-pow", cuda, false, true), "hard zero synthetic remains valid");
    ok &= check(pearlRuntimeAdmissionProblem(bench, "pearl-pow", cuda, true), "all hit refused");
    ok &= check(pearlRuntimeAdmissionProblem(live(), "pearl-pow", cuda, false, true), "live hard target refused");
    opt = bench; opt.remoteOptionsSpecified = true; rejected(opt, "explicit default remote flags in bench");
    opt = bench; opt.wallet = "unused"; rejected(opt, "bench wallet rejected");
    opt = bench; opt.pearlPolicy = live().pearlPolicy; rejected(opt, "bench live policy rejected");
    opt = bench; opt.benchEpoch++; rejected(opt, "synthetic epoch frozen");
    opt = bench; opt.benchEpochSeconds = 1; rejected(opt, "synthetic height rotation rejected");
    opt = bench; opt.pearlTranscript = "not-written"; rejected(opt, "bench transcript rejected");
    opt = live(); opt.bench = true; rejected(opt, "bench plus pool rejected");
    algo.algorithmName = "autolykos2";
    opt = {}; ok &= check(!pearlRuntimeAdmissionProblem(opt, algo.name(), PearlRuntimeCapability::Unsupported), "non-Pearl unaffected");
    opt.pearlPolicy = live().pearlPolicy; rejected(opt, "Pearl policy on other algo rejected");
    algo.algorithmName = "pearl-pow";
    // Product selection is exact and cannot be used to smuggle a research or
    // non-fixed shape through the ordinary run admission.
    setenv("CANARY_PEARL_PRODUCT_FIXED_SELECTION", "1", 1);
    setenv("CANARY_PEARL_SHAPE", "4096x65536", 1);
    setenv("CANARY_PEARL_TILE", "ptx k64 4x4/2x4", 1);
    auto nonFixedProduct = live();
    nonFixedProduct.productFixedSelection = true;
    const int nonFixedConfigured = algo.configured;
    ok &= check(runMiner(&algo, nonFixedProduct, "must-not-probe", 0, "test", &stop) == 1 &&
                algo.configured == nonFixedConfigured,
                "non-fixed product shape refused without research admission");
    unsetenv("CANARY_PEARL_SHAPE");
    unsetenv("CANARY_PEARL_TILE");
    unsetenv("CANARY_PEARL_PRODUCT_FIXED_SELECTION");
    Job job;
    ok &= check(initializePearlBenchmarkJob(&algo, &job) && job.valid && pearlTargetIsZero(job.target), "complete canonical synthetic fixture");
    for (int mode = 1; mode <= 5; ++mode) {
        algo.fixtureMode = mode;
        job.valid = true; job.extra = "previous output";
        ok &= check(!initializePearlBenchmarkJob(&algo, &job) && !job.valid && job.extra.empty(), "failed fixture clears prior ownership");
    }
    algo.fixtureMode = 1;
    // Actual run-loop entry must refuse before configure, telemetry dlopen,
    // or any socket/connect. The wrappers above abort if those are reached.
    const char *priorExperiment = getenv("CANARY_PEARL_EXPERIMENT");
    const bool hadExperiment = priorExperiment != nullptr;
    const std::string savedExperiment = priorExperiment ? priorExperiment : "";
    auto rejectExperiment = [&](const RunOptions &request) {
        const int configuredBefore = algo.configured;
        const int fixturesBefore = algo.fixtureCalls;
        ok &= check(runMiner(&algo, request, "must-not-probe", 0, "test", &stop) == 1 &&
                    algo.configured == configuredBefore && algo.fixtureCalls == fixturesBefore,
                    "experiment refused before configure/fixture/telemetry/socket");
    };
    for (const char *selector : {"", "1", "malformed", pearl::kCudaFixedExperimentOptIn}) {
        setenv("CANARY_PEARL_EXPERIMENT", selector, 1);
        algo.experimentSelection = pearl::parseCudaFixedExperiment(selector);
        auto request = live(); request.poolHost = "127.0.0.1";
        algo.experimentCompiled = false;
        rejectExperiment(request);
    }
    algo.experimentCompiled = true;
    algo.experimentSelection = pearl::CudaFixedExperimentSelection::Enabled;
    for (const char *host : {"offline.invalid", "localhost", "::1", "127.0.0.2"}) {
        auto request = live(); request.poolHost = host;
        rejectExperiment(request);
    }
    rejectExperiment(bench);
    auto experimentFormal = live(); experimentFormal.poolHost = "127.0.0.1";
    experimentFormal.formalBenchmark.enabled = true;
    rejectExperiment(experimentFormal);
    auto experimentLoopback = live(); experimentLoopback.poolHost = "127.0.0.1";
    for (const char *variable : {"CANARY_PEARL_SHAPE", "CANARY_PEARL_TILE"}) {
        const char *prior = getenv(variable);
        const bool hadPrior = prior != nullptr;
        const std::string saved = prior ? prior : "";
        setenv(variable, "", 1);
        rejectExperiment(experimentLoopback);
        if (hadPrior) setenv(variable, saved.c_str(), 1); else unsetenv(variable);
    }
    unsetenv("CANARY_PEARL_EXPERIMENT");
    rejectExperiment(experimentLoopback);  // Captured enabled state cannot be unset away.
    algo.experimentSelection = pearl::CudaFixedExperimentSelection::Disabled;
    setenv("CANARY_PEARL_EXPERIMENT", pearl::kCudaFixedExperimentOptIn, 1);
    rejectExperiment(experimentLoopback);  // Nor may it be enabled after construction.
    unsetenv("CANARY_PEARL_EXPERIMENT");
    // The independent WM8 compile capability must not be borrowed from fixed-K.
    algo.experimentSelection = pearl::CudaFixedExperimentSelection::Wm8Runtime;
    setenv("CANARY_PEARL_EXPERIMENT", pearl::kCudaWm8ExperimentOptIn, 1);
    algo.experimentCompiled = false;
    rejectExperiment(experimentLoopback);
    algo.experimentCompiled = true;
    ok &= check(algo.pearlExperimentConfigurationValid(experimentLoopback, pearl::kCudaWm8ExperimentOptIn),
                "exact WM8 loopback configuration admitted by pure preflight");
    rejectExperiment(live());
    rejectExperiment(bench);
    rejectExperiment(experimentFormal);
    for (const char *variable : {"CANARY_PEARL_SHAPE", "CANARY_PEARL_TILE"}) {
        const char *prior = getenv(variable);
        const bool hadPrior = prior != nullptr;
        const std::string saved = prior ? prior : "";
        setenv(variable, "", 1);
        rejectExperiment(experimentLoopback);
        if (hadPrior) setenv(variable, saved.c_str(), 1); else unsetenv(variable);
    }
    setenv("CANARY_PEARL_EXPERIMENT", pearl::kCudaFixedExperimentOptIn, 1);
    rejectExperiment(experimentLoopback);
    unsetenv("CANARY_PEARL_EXPERIMENT");
    rejectExperiment(experimentLoopback);
    algo.experimentSelection = pearl::CudaFixedExperimentSelection::Wm8FixedK2048R128;
    setenv("CANARY_PEARL_EXPERIMENT", pearl::kCudaWm8FixedExperimentOptIn, 1);
    algo.experimentCompiled = false;
    rejectExperiment(experimentLoopback);
    algo.experimentCompiled = true;
    ok &= check(algo.pearlExperimentConfigurationValid(experimentLoopback, pearl::kCudaWm8FixedExperimentOptIn),
                "fixed WM8 exact isolated configuration accepted by pure preflight");
    rejectExperiment(live());
    rejectExperiment(bench);
    rejectExperiment(experimentFormal);
    for (const char *variable : {"CANARY_PEARL_SHAPE", "CANARY_PEARL_TILE", "CANARY_PEARL_ATTEMPT_OVERLAP",
                                  "CANARY_PEARL_A_ROOT_FUSION", "CANARY_PEARL_PROFILE_INTERVAL"}) {
        const char *prior = getenv(variable);
        const bool hadPrior = prior != nullptr;
        const std::string saved = prior ? prior : "";
        setenv(variable, "", 1);
        rejectExperiment(experimentLoopback);
        if (hadPrior) setenv(variable, saved.c_str(), 1); else unsetenv(variable);
    }
    setenv("CANARY_PEARL_EXPERIMENT", pearl::kCudaWm8ExperimentOptIn, 1);
    rejectExperiment(experimentLoopback);
    unsetenv("CANARY_PEARL_EXPERIMENT");
    rejectExperiment(experimentLoopback);
    algo.experimentSelection = pearl::CudaFixedExperimentSelection::Disabled;
    if (hadExperiment) setenv("CANARY_PEARL_EXPERIMENT", savedExperiment.c_str(), 1);
    const int before = algo.configured;
    ok &= check(runMiner(&algo, bench, "must-not-probe", 0, "test", &stop) == 1 &&
                algo.configured == before + 1, "fixture failure is before telemetry");
    // Exercise the actual runtime guard, not a parallel admission predicate.
    // Ordinary diagnostic benchmarks still reach configure/fixture creation;
    // formal benchmarks must reject even zero/invalid inherited settings first.
    RunOptions formal = bench;
    formal.formalBenchmark = {true, 0, "m4096-n65536", "cuda-ptx-mma-s8-v1",
                             "wm2-wn4-tm2-tn2-bk64-st3-gm8"};
    formal.plain = true;
    formal.backendLabel = "CUDA";
    formal.batch = kPearlFormalBenchmarkBatch;
    formal.reportSeconds = kPearlFormalBenchmarkReportSeconds;
    const char *priorProfile = std::getenv("CANARY_PEARL_PROFILE_INTERVAL");
    const bool hadProfile = priorProfile != nullptr;
    const std::string savedProfile = priorProfile ? priorProfile : "";
    for (const char *value : {"0", "100", "1000000000", "malformed"}) {
        setenv("CANARY_PEARL_PROFILE_INTERVAL", value, 1);
        const int configuredBefore = algo.configured;
        const int fixturesBefore = algo.fixtureCalls;
        ok &= check(runMiner(&algo, formal, "must-not-probe", 0, "test", &stop) == 1 &&
                    algo.configured == configuredBefore && algo.fixtureCalls == fixturesBefore,
                    "formal profile presence rejected before configure/fixture/telemetry");
        ok &= check(runMiner(&algo, bench, "must-not-probe", 0, "test", &stop) == 1 &&
                    algo.configured == configuredBefore + 1 && algo.fixtureCalls == fixturesBefore + 1,
                    "ordinary diagnostic benchmark remains admitted before fixture failure");
    }
    setenv("CANARY_PEARL_PROFILE_INTERVAL", "", 1);
    const int emptyBefore = algo.configured;
    ok &= check(runMiner(&algo, formal, "must-not-probe", 0, "test", &stop) == 1 &&
                algo.configured == emptyBefore, "empty present profile rejected");
    unsetenv("CANARY_PEARL_PROFILE_INTERVAL");
    ok &= check(runMiner(&algo, formal, "must-not-probe", 0, "test", &stop) == 1 &&
                algo.configured == emptyBefore + 1, "absent profile reaches formal fixture gate");
    if (hadProfile) setenv("CANARY_PEARL_PROFILE_INTERVAL", savedProfile.c_str(), 1);
    job.valid = true;
    ok &= check(!initializePearlBenchmarkJob(nullptr, &job) && !job.valid, "null algorithm clears output");
    ok &= check(!initializePearlBenchmarkJob(&algo, nullptr), "null output refused");
    PearlNetwork network = PearlNetwork::Mainnet;
    for (const char *text : {"", "MAINNET", "mainnet ", "synthetic-benchmark", "bogus"})
        ok &= check(!parsePearlNetworkOption(text, &network) && network == PearlNetwork::Mainnet, "invalid network token transactional");
    PearlTargetSemantics semantics = PearlTargetSemantics::BaseBeforePenalty;
    for (const char *text : {"", "final", "FinalAdjustedBound", "base-before-penalty "})
        ok &= check(!parsePearlTargetSemanticsOption(text, &semantics) && semantics == PearlTargetSemantics::BaseBeforePenalty, "invalid semantics transactional");
    ok &= check(!parsePearlNetworkOption("mainnet", nullptr) && !parsePearlTargetSemanticsOption("base-before-penalty", nullptr), "null parser outputs refused");
    ok &= localRetirementRuntimeAdmission();
    std::puts(ok ? "Pearl host admission PASS; no GPU/network/accepted-proof evidence" : "Pearl host admission FAIL");
    return ok ? 0 : 1;
}
