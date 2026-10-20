// Linux-only supervisor GPU tuning. NVML is loaded dynamically; no NVML SDK headers are required.

#pragma once

#include <stdint.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "config.h"
#include "supervisor.h"
#include "../core/gpu_identity.h"

#if defined(_WIN32)
#include <io.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace om {

constexpr int kNvmlSuccess = 0;
constexpr int kNvmlErrorNoPermission = 4;
constexpr unsigned kNvmlClockGraphics = 0;
constexpr unsigned kNvmlClockMemory = 2;
constexpr unsigned kNvmlPstate0 = 0;
constexpr unsigned kNvmlFanAuto = 0;
constexpr unsigned kNvmlFanManual = 1;

struct NvmlClockOffsetV1 {
    unsigned version;
    int type;
    int pstate;
    int clockOffsetMHz;
    int minClockOffsetMHz;
    int maxClockOffsetMHz;
};

inline unsigned nvmlClockOffsetV1Version() {
    return static_cast<unsigned>(sizeof(NvmlClockOffsetV1)) | (1u << 24u);
}

/** NVML function table. Tests inject a fake table; production resolves only these named operations. */
struct NvmlTuningApi {
    int (*init)() = nullptr;
    int (*shutdown)() = nullptr;
    int (*getHandleByUuid)(const char *, void **) = nullptr;
    int (*getUuid)(void *, char *, unsigned) = nullptr;
    int (*getClockInfo)(void *, unsigned, unsigned *) = nullptr;
    int (*setGpuLockedClocks)(void *, unsigned, unsigned) = nullptr;
    int (*resetGpuLockedClocks)(void *) = nullptr;
    int (*setMemoryLockedClocks)(void *, unsigned, unsigned) = nullptr;
    int (*resetMemoryLockedClocks)(void *) = nullptr;
    int (*getClockOffsets)(void *, NvmlClockOffsetV1 *) = nullptr;
    int (*setClockOffsets)(void *, NvmlClockOffsetV1 *) = nullptr;
    int (*getPowerLimit)(void *, unsigned *) = nullptr;
    int (*getPowerConstraints)(void *, unsigned *, unsigned *) = nullptr;
    int (*getPowerDefaultLimit)(void *, unsigned *) = nullptr;
    int (*setPowerLimit)(void *, unsigned) = nullptr;
    int (*getNumFans)(void *, unsigned *) = nullptr;
    int (*getFanSpeed)(void *, unsigned, unsigned *) = nullptr;
    int (*getFanPolicy)(void *, unsigned, unsigned *) = nullptr;
    int (*setFanPolicy)(void *, unsigned, unsigned) = nullptr;
    int (*setFanSpeed)(void *, unsigned, unsigned) = nullptr;
    int (*setDefaultFanSpeed)(void *, unsigned) = nullptr;
};

/** Production NVML loader. It is never opened on Windows or WSL. */
class NvmlTuningLibrary {
   public:
    NvmlTuningLibrary() = default;
    NvmlTuningLibrary(const NvmlTuningLibrary &) = delete;
    NvmlTuningLibrary &operator=(const NvmlTuningLibrary &) = delete;
    ~NvmlTuningLibrary() { close(); }

    bool open(std::string *error) {
#if defined(_WIN32)
        if (error) *error = "NVML GPU tuning is Linux-only";
        return false;
#else
        if (lib_) return true;
        lib_ = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!lib_) lib_ = dlopen("libnvidia-ml.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib_) {
            if (error) *error = "NVML library is unavailable";
            return false;
        }
#define OM_NVML_LOAD(field, symbol) api_.field = reinterpret_cast<decltype(api_.field)>(dlsym(lib_, symbol))
        OM_NVML_LOAD(init, "nvmlInit_v2");
        OM_NVML_LOAD(shutdown, "nvmlShutdown");
        OM_NVML_LOAD(getHandleByUuid, "nvmlDeviceGetHandleByUUID");
        OM_NVML_LOAD(getUuid, "nvmlDeviceGetUUID");
        OM_NVML_LOAD(getClockInfo, "nvmlDeviceGetClockInfo");
        OM_NVML_LOAD(setGpuLockedClocks, "nvmlDeviceSetGpuLockedClocks");
        OM_NVML_LOAD(resetGpuLockedClocks, "nvmlDeviceResetGpuLockedClocks");
        OM_NVML_LOAD(setMemoryLockedClocks, "nvmlDeviceSetMemoryLockedClocks");
        OM_NVML_LOAD(resetMemoryLockedClocks, "nvmlDeviceResetMemoryLockedClocks");
        OM_NVML_LOAD(getClockOffsets, "nvmlDeviceGetClockOffsets");
        OM_NVML_LOAD(setClockOffsets, "nvmlDeviceSetClockOffsets");
        OM_NVML_LOAD(getPowerLimit, "nvmlDeviceGetPowerManagementLimit");
        OM_NVML_LOAD(getPowerConstraints, "nvmlDeviceGetPowerManagementLimitConstraints");
        OM_NVML_LOAD(getPowerDefaultLimit, "nvmlDeviceGetPowerManagementDefaultLimit");
        OM_NVML_LOAD(setPowerLimit, "nvmlDeviceSetPowerManagementLimit");
        OM_NVML_LOAD(getNumFans, "nvmlDeviceGetNumFans");
        OM_NVML_LOAD(getFanSpeed, "nvmlDeviceGetFanSpeed_v2");
        OM_NVML_LOAD(getFanPolicy, "nvmlDeviceGetFanControlPolicy_v2");
        OM_NVML_LOAD(setFanPolicy, "nvmlDeviceSetFanControlPolicy");
        OM_NVML_LOAD(setFanSpeed, "nvmlDeviceSetFanSpeed_v2");
        OM_NVML_LOAD(setDefaultFanSpeed, "nvmlDeviceSetDefaultFanSpeed_v2");
#undef OM_NVML_LOAD
        if (!api_.init || !api_.shutdown || !api_.getHandleByUuid || !api_.getUuid) {
            if (error) *error = "NVML library is missing required device functions";
            close();
            return false;
        }
        const int rc = api_.init();
        if (rc != kNvmlSuccess) {
            if (error) *error = rc == kNvmlErrorNoPermission ? "GPU tuning needs root: run with sudo, or tune with nvidia-smi"
                                                            : "NVML initialization failed (error " + std::to_string(rc) + ")";
            close();
            return false;
        }
        initialized_ = true;
        return true;
#endif
    }

    void close() {
#if !defined(_WIN32)
        if (initialized_ && api_.shutdown) api_.shutdown();
        initialized_ = false;
        if (lib_) dlclose(lib_);
        lib_ = nullptr;
        api_ = NvmlTuningApi{};
#endif
    }

    NvmlTuningApi *api() { return &api_; }

   private:
    NvmlTuningApi api_;
    void *lib_ = nullptr;
    bool initialized_ = false;
};

struct GpuTuningPlatform {
    bool windows = false;
    bool wsl = false;
    bool root = false;
};

inline GpuTuningPlatform currentGpuTuningPlatform() {
#if defined(_WIN32)
    return {true, false, false};
#else
    std::ifstream version("/proc/version");
    std::string text;
    std::getline(version, text);
    for (char &c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return {false, text.find("microsoft") != std::string::npos, geteuid() == 0};
#endif
}

/** v1.0.2: GPU tuning is off on every platform (two Xid 62 PMU halts on RTX 5070 during tests); the code stays for a later release. */
constexpr bool kGpuTuningShipped = false;

inline const char *gpuTuningUnsupportedMessage() {
    return "GPU tuning flags are not supported in this release and are ignored; set clocks, power and fans with your own tool "
           "(nvidia-smi, MSI Afterburner or similar). Mining continues untuned";
}

struct GpuTuningDevice {
    int index = -1;
    std::string uuid;
};

enum GpuTuningCategory : unsigned {
    kTuneCoreClock = 1u << 0,
    kTuneMemoryClock = 1u << 1,
    kTuneCoreOffset = 1u << 2,
    kTuneMemoryOffset = 1u << 3,
    kTunePower = 1u << 4,
    kTuneFan = 1u << 5,
    kTuneAll = kTuneCoreClock | kTuneMemoryClock | kTuneCoreOffset | kTuneMemoryOffset | kTunePower | kTuneFan
};

struct GpuTuningValue {
    bool coreClockSet = false, memoryClockSet = false, coreOffsetSet = false, memoryOffsetSet = false, powerSet = false, fanSet = false;
    unsigned coreMinMHz = 0, coreMaxMHz = 0, memoryMHz = 0, powerWatts = 0, fanPercent = 0;
    int coreOffsetMHz = 0, memoryOffsetMHz = 0;
};

struct GpuTuningPlan {
    std::vector<GpuTuningValue> devices;
    unsigned categories = 0;
    uint64_t delayMs = 0;
};

inline std::string trimGpuTuning(std::string value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

inline bool splitGpuTuningList(const std::string &text, std::vector<std::string> *values) {
    values->clear();
    size_t start = 0;
    for (;;) {
        const size_t comma = text.find(',', start);
        const std::string item = trimGpuTuning(text.substr(start, comma == std::string::npos ? comma : comma - start));
        if (item.empty()) return false;
        values->push_back(item);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return true;
}

inline bool parseGpuTuningUnsigned(const std::string &text, unsigned *value) {
    if (text.empty()) return false;
    uint64_t n = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const unsigned digit = static_cast<unsigned>(c - '0');
        if (n > (std::numeric_limits<unsigned>::max() - digit) / 10u) return false;
        n = n * 10u + digit;
    }
    *value = static_cast<unsigned>(n);
    return true;
}

inline bool parseGpuTuningSigned(const std::string &text, int *value) {
    if (text.empty()) return false;
    size_t i = (text[0] == '+' || text[0] == '-') ? 1 : 0;
    if (i == text.size()) return false;
    for (; i < text.size(); ++i) if (text[i] < '0' || text[i] > '9') return false;
    errno = 0;
    char *end = nullptr;
    const long n = std::strtol(text.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end || n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max()) return false;
    *value = static_cast<int>(n);
    return true;
}

template <typename T, typename Parse>
inline bool parseGpuTuningList(const std::string &name, const std::string &raw, size_t count, Parse parse, std::vector<T> *out,
                               std::string *error) {
    std::vector<std::string> items;
    if (!splitGpuTuningList(raw, &items)) {
        if (error) *error = name + " has an empty list value";
        return false;
    }
    if (items.size() != 1 && items.size() != count) {
        if (error) *error = name + " needs one value or exactly " + std::to_string(count) + " values in selected GPU order";
        return false;
    }
    std::vector<T> parsed;
    parsed.reserve(items.size());
    for (const std::string &item : items) {
        T value{};
        if (!parse(item, &value)) {
            if (error) *error = name + " contains an invalid or out-of-range value: " + item;
            return false;
        }
        parsed.push_back(value);
    }
    if (parsed.size() == 1) {
        const T broadcast = parsed[0];
        parsed.assign(count, broadcast);
    }
    *out = std::move(parsed);
    return true;
}

struct GpuTuningClockRange {
    unsigned minMHz = 0, maxMHz = 0;
};

inline bool parseGpuTuningClockRange(const std::string &text, GpuTuningClockRange *range) {
    const size_t colon = text.find(':');
    if (colon != std::string::npos && text.find(':', colon + 1) != std::string::npos) return false;
    const std::string loText = colon == std::string::npos ? text : text.substr(0, colon);
    const std::string hiText = colon == std::string::npos ? text : text.substr(colon + 1);
    unsigned lo = 0, hi = 0;
    if (!parseGpuTuningUnsigned(loText, &lo) || !parseGpuTuningUnsigned(hiText, &hi) || lo < 100 || hi > 5000 || lo > hi) return false;
    range->minMHz = lo;
    range->maxMHz = hi;
    return true;
}

inline bool parseGpuTuningPlan(const GpuTuningConfig &cfg, size_t selectedCount, GpuTuningPlan *plan, std::string *error) {
    GpuTuningPlan out;
    if (!selectedCount) { if (error) *error = "no selected GPUs"; return false; }
    out.devices.resize(selectedCount);
    out.delayMs = cfg.ocDelaySeconds * 1000u;
    std::vector<GpuTuningClockRange> core;
    std::vector<unsigned> memory, power, fan;
    std::vector<int> coreOffset, memoryOffset;
    if (cfg.gpuCclockSet) {
        if (!parseGpuTuningList("--gpu-cclock", cfg.gpuCclock, selectedCount, parseGpuTuningClockRange, &core, error)) return false;
        out.categories |= kTuneCoreClock;
    }
    if (cfg.gpuMclockSet) {
        if (!parseGpuTuningList("--gpu-mclock", cfg.gpuMclock, selectedCount,
                                [](const std::string &s, unsigned *v) {
                                    unsigned n = 0;
                                    // SM120 memory clock requests use the NVML MHz value (the owner's qualified profile is 7001 MHz).
                                    if (!parseGpuTuningUnsigned(s, &n) || n < 100 || n > 10000) return false;
                                    *v = n;
                                    return true;
                                }, &memory, error)) return false;
        out.categories |= kTuneMemoryClock;
    }
    if (cfg.gpuCoffsetSet) {
        if (!parseGpuTuningList("--gpu-coffset", cfg.gpuCoffset, selectedCount,
                                [](const std::string &s, int *v) { return parseGpuTuningSigned(s, v) && *v >= -1000 && *v <= 1500; },
                                &coreOffset, error)) return false;
        out.categories |= kTuneCoreOffset;
    }
    if (cfg.gpuMoffsetSet) {
        if (!parseGpuTuningList("--gpu-moffset", cfg.gpuMoffset, selectedCount,
                                [](const std::string &s, int *v) { return parseGpuTuningSigned(s, v) && *v >= -1000 && *v <= 1500; },
                                &memoryOffset, error)) return false;
        out.categories |= kTuneMemoryOffset;
    }
    if (cfg.gpuPlimitSet) {
        if (!parseGpuTuningList("--gpu-plimit", cfg.gpuPlimit, selectedCount,
                                [](const std::string &s, unsigned *v) { return parseGpuTuningUnsigned(s, v) && *v > 0 && *v <= UINT32_MAX / 1000u; },
                                &power, error)) return false;
        out.categories |= kTunePower;
    }
    if (cfg.gpuFanSet) {
        if (!parseGpuTuningList("--gpu-fan", cfg.gpuFan, selectedCount,
                                [](const std::string &s, unsigned *v) { return parseGpuTuningUnsigned(s, v) && *v >= 30 && *v <= 100; },
                                &fan, error)) return false;
        out.categories |= kTuneFan;
    }
    if (!out.categories) {
        if (cfg.ocDelaySet) { if (error) *error = "--oc-delay requires at least one GPU tuning value"; return false; }
        *plan = out;
        return true;
    }
    for (size_t i = 0; i < selectedCount; ++i) {
        GpuTuningValue &v = out.devices[i];
        if (cfg.gpuCclockSet) { v.coreClockSet = true; v.coreMinMHz = core[i].minMHz; v.coreMaxMHz = core[i].maxMHz; }
        if (cfg.gpuMclockSet) { v.memoryClockSet = true; v.memoryMHz = memory[i]; }
        if (cfg.gpuCoffsetSet) { v.coreOffsetSet = true; v.coreOffsetMHz = coreOffset[i]; }
        if (cfg.gpuMoffsetSet) { v.memoryOffsetSet = true; v.memoryOffsetMHz = memoryOffset[i]; }
        if (cfg.gpuPlimitSet) { v.powerSet = true; v.powerWatts = power[i]; }
        if (cfg.gpuFanSet) { v.fanSet = true; v.fanPercent = fan[i]; }
    }
    *plan = std::move(out);
    return true;
}

struct GpuTuningMarkerRecord {
    std::string uuid;
    unsigned categories = 0;
};

inline std::string gpuTuningMarkerPath(const std::string &stateDir) {
    return (stateDir.empty() ? std::filesystem::path(".") : std::filesystem::path(stateDir)).append("gpu-tuning-restore.marker").string();
}

inline bool gpuTuningMarkerExists(const std::string &path) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    return exists || static_cast<bool>(ec);
}

inline bool validGpuTuningUuid(const std::string &uuid) {
    if (uuid.empty() || uuid.size() > 95) return false;
    for (char c : uuid) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

inline bool readGpuTuningMarker(const std::string &path, bool *exists, std::vector<GpuTuningMarkerRecord> *records, std::string *error) {
    records->clear();
    std::error_code ec;
    *exists = std::filesystem::exists(path, ec);
    if (ec) { if (error) *error = "cannot inspect GPU tuning restore marker"; return false; }
    if (!*exists) return true;
    std::ifstream in(path);
    std::string line;
    if (!in || !std::getline(in, line) || line != "canaryminer-gpu-tuning-v1") {
        if (error) *error = "GPU tuning restore marker is unreadable or has an unknown format";
        return false;
    }
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const size_t sep = line.find(' ');
        if (sep == std::string::npos || line.find(' ', sep + 1) != std::string::npos) {
            if (error) *error = "GPU tuning restore marker has a malformed record";
            return false;
        }
        GpuTuningMarkerRecord record;
        record.uuid = line.substr(0, sep);
        if (!validGpuTuningUuid(record.uuid) || !parseGpuTuningUnsigned(line.substr(sep + 1), &record.categories) ||
            record.categories == 0 || (record.categories & ~kTuneAll) != 0 || records->size() >= 16) {
            if (error) *error = "GPU tuning restore marker has an invalid record";
            return false;
        }
        records->push_back(record);
    }
    if (in.bad() || records->empty()) {
        if (error) *error = "GPU tuning restore marker is incomplete";
        return false;
    }
    return true;
}

inline bool writeGpuTuningMarker(const std::string &path, const std::vector<GpuTuningMarkerRecord> &records, std::string *error) {
    namespace fs = std::filesystem;
    const fs::path target(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    if (ec) { if (error) *error = "cannot create GPU tuning marker directory"; return false; }
    const std::string tmp = path + ".tmp";
    FILE *file = std::fopen(tmp.c_str(), "wb");
    if (!file) { if (error) *error = "cannot write GPU tuning restore marker"; return false; }
    bool ok = std::fputs("canaryminer-gpu-tuning-v1\n", file) >= 0;
    for (const GpuTuningMarkerRecord &r : records) {
        if (!validGpuTuningUuid(r.uuid) || r.categories == 0 || (r.categories & ~kTuneAll) != 0) { ok = false; break; }
        if (std::fprintf(file, "%s %u\n", r.uuid.c_str(), r.categories) < 0) { ok = false; break; }
    }
    ok = ok && std::fflush(file) == 0;
#if defined(_WIN32)
    ok = ok && _commit(_fileno(file)) == 0;
#else
    ok = ok && fsync(fileno(file)) == 0;
#endif
    ok = (std::fclose(file) == 0) && ok;
    if (!ok) {
        fs::remove(tmp, ec);
        if (error) *error = "cannot flush GPU tuning restore marker";
        return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (error) *error = "cannot atomically replace GPU tuning restore marker";
        return false;
    }
    return true;
}

inline bool removeGpuTuningMarker(const std::string &path, std::string *error) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) { if (error) *error = "cannot remove GPU tuning restore marker"; return false; }
    return true;
}

inline bool hasGpuTuningValues(const GpuTuningConfig &cfg) {
    return cfg.gpuCclockSet || cfg.gpuMclockSet || cfg.gpuCoffsetSet || cfg.gpuMoffsetSet || cfg.gpuPlimitSet || cfg.gpuFanSet;
}

inline bool gpuTuningNeedsSupervisor(const GpuTuningConfig &cfg, const std::string &stateDir, const GpuTuningPlatform &platform) {
    if (platform.windows || platform.wsl) return false;
    return hasGpuTuningValues(cfg) || cfg.ocDelaySet || gpuTuningMarkerExists(gpuTuningMarkerPath(stateDir));
}

/** Supervisor lifecycle implementation: apply, delayed apply, crash-marker recovery, and restore. */
class GpuTuning final : public SupervisorTuning {
   public:
    GpuTuning(GpuTuningConfig config, std::vector<GpuTuningDevice> devices, std::string stateDir,
              NvmlTuningApi *fakeApi = nullptr, GpuTuningPlatform platform = currentGpuTuningPlatform())
        : config_(std::move(config)), devices_(std::move(devices)), markerPath_(gpuTuningMarkerPath(stateDir)), fakeApi_(fakeApi), platform_(platform) {}

    void start(uint64_t nowMs, std::vector<std::string> *events) override {
        if (started_) return;
        started_ = true;
        if (!platformSupported()) {
            if (config_.requested()) emit(events, gpuTuningUnsupportedMessage());
            return;
        }
        bool markerExists = false;
        std::vector<GpuTuningMarkerRecord> stale;
        std::string error;
        if (!readGpuTuningMarker(markerPath_, &markerExists, &stale, &error)) {
            emit(events, error + "; mining untuned");
            markerBlocked_ = true;
            return;
        }
        if (markerExists) {
            if (!platform_.root) { emitRootMessage(events); markerBlocked_ = true; return; }
            if (!openApi(&error)) {
                if (error == rootMessage()) emitRootMessage(events);
                else emit(events, "GPU tuning restore unavailable: " + error + "; mining untuned");
                markerBlocked_ = true;
                return;
            }
            const bool restored = restoreRecords(stale, events);
            closeApi();
            if (!restored) { markerBlocked_ = true; activeRecords_ = stale; return; }
            if (!removeGpuTuningMarker(markerPath_, &error)) { emit(events, error); markerBlocked_ = true; return; }
            emit(events, "GPU tuning: recovered the previous run's restore marker");
        }
        if (config_.ocResetOnly) return;  // reset-only is handled in miner.cu before the supervisor starts.
        if (!parseGpuTuningPlan(config_, devices_.size(), &plan_, &error)) {
            emit(events, "GPU tuning refused: " + error + "; mining untuned");
            return;
        }
        if (!plan_.categories) return;
        if (!platform_.root) { emitRootMessage(events); return; }
        if (plan_.delayMs == 0) {
            applyPlan(events);
        } else {
            pending_ = true;
            deadlineMs_ = nowMs + plan_.delayMs;
            emit(events, "GPU tuning armed; applying after " + std::to_string(plan_.delayMs / 1000) + " seconds");
        }
    }

    void tick(uint64_t nowMs, std::vector<std::string> *events) override {
        if (!pending_ || nowMs < deadlineMs_) return;
        pending_ = false;
        applyPlan(events);
    }

    void stop(std::vector<std::string> *events) override {
        pending_ = false;
        if ((!applied_ && !markerBlocked_) || activeRecords_.empty()) return;
        if (!platformSupported()) return;
        if (!platform_.root) { emitRootMessage(events); return; }
        std::string error;
        if (!openApi(&error)) {
            if (error == rootMessage()) emitRootMessage(events);
            else emit(events, "GPU tuning restore unavailable: " + error);
            return;
        }
        const bool restored = restoreRecords(activeRecords_, events);
        closeApi();
        if (!restored) { emit(events, "GPU tuning restore incomplete; marker retained for the next start"); return; }
        if (!removeGpuTuningMarker(markerPath_, &error)) { emit(events, error); return; }
        activeRecords_.clear();
        applied_ = false;
        markerBlocked_ = false;
        emit(events, "GPU tuning: stock settings restored");
    }

    /** Direct reset-only path, called by miner.cu before any worker or pool setup. */
    bool resetOnly(std::vector<std::string> *events) {
        if (!platformSupported()) { emit(events, gpuTuningUnsupportedMessage()); return false; }
        if (!platform_.root) { emitRootMessage(events); return false; }
        bool markerExists = false;
        std::vector<GpuTuningMarkerRecord> records;
        std::string error;
        if (!readGpuTuningMarker(markerPath_, &markerExists, &records, &error)) {
            emit(events, error);
            return false;
        }
        for (const GpuTuningDevice &device : devices_) {
            auto it = std::find_if(records.begin(), records.end(), [&](const GpuTuningMarkerRecord &r) { return gpuUuidEqual(r.uuid, device.uuid); });
            if (it == records.end()) records.push_back({device.uuid, kTuneAll});
            else it->categories |= kTuneAll;
        }
        if (records.empty()) { emit(events, "GPU tuning reset refused: no selected GPUs"); return false; }
        if (!writeGpuTuningMarker(markerPath_, records, &error)) { emit(events, error); return false; }
        if (!openApi(&error)) {
            if (error == rootMessage()) emitRootMessage(events);
            else emit(events, "GPU tuning reset unavailable: " + error);
            return false;
        }
        const bool restored = restoreRecords(records, events);
        closeApi();
        if (!restored) { emit(events, "GPU tuning reset incomplete; marker retained"); return false; }
        if (!removeGpuTuningMarker(markerPath_, &error)) { emit(events, error); return false; }
        emit(events, "GPU tuning: stock settings restored; exiting");
        return true;
    }

   private:
    struct LiveDevice {
        GpuTuningMarkerRecord record;
        int index = -1;
        void *handle = nullptr;
        unsigned fans = 0;
    };

    GpuTuningConfig config_;
    std::vector<GpuTuningDevice> devices_;
    std::string markerPath_;
    NvmlTuningApi *fakeApi_ = nullptr;
    GpuTuningPlatform platform_;
    NvmlTuningLibrary library_;
    NvmlTuningApi *api_ = nullptr;
    bool initializedFake_ = false;
    bool started_ = false, pending_ = false, applied_ = false, markerBlocked_ = false, permissionMessageEmitted_ = false;
    uint64_t deadlineMs_ = 0;
    GpuTuningPlan plan_;
    std::vector<GpuTuningMarkerRecord> activeRecords_;

    static void emit(std::vector<std::string> *events, const std::string &line) { if (events) events->push_back(line); }
    static std::string rootMessage() { return "GPU tuning needs root: run with sudo, or tune with nvidia-smi"; }
    void emitRootMessage(std::vector<std::string> *events) {
        if (!permissionMessageEmitted_) { emit(events, rootMessage()); permissionMessageEmitted_ = true; }
    }
    bool platformSupported() const { return !platform_.windows && !platform_.wsl; }

    bool openApi(std::string *error) {
        if (api_) return true;
        if (fakeApi_) {
            api_ = fakeApi_;
            const int rc = api_->init ? api_->init() : -1;
            if (rc != kNvmlSuccess) {
                api_ = nullptr;
                if (error) *error = rc == kNvmlErrorNoPermission ? rootMessage() : "NVML initialization failed (error " + std::to_string(rc) + ")";
                return false;
            }
            initializedFake_ = true;
            return true;
        }
        if (!library_.open(error)) return false;
        api_ = library_.api();
        return true;
    }

    void closeApi() {
        if (initializedFake_ && api_ && api_->shutdown) api_->shutdown();
        initializedFake_ = false;
        if (!fakeApi_) library_.close();
        api_ = nullptr;
    }

    bool haveApplyFunctions(unsigned mask, std::string *error) const {
        bool ok = api_->getClockInfo != nullptr;
        if (mask & kTuneCoreClock) ok = ok && api_->setGpuLockedClocks && api_->resetGpuLockedClocks;
        if (mask & kTuneMemoryClock) ok = ok && api_->setMemoryLockedClocks && api_->resetMemoryLockedClocks;
        if (mask & (kTuneCoreOffset | kTuneMemoryOffset)) ok = ok && api_->getClockOffsets && api_->setClockOffsets;
        if (mask & kTunePower) ok = ok && api_->getPowerLimit && api_->getPowerConstraints && api_->getPowerDefaultLimit && api_->setPowerLimit;
        if (mask & kTuneFan) ok = ok && api_->getNumFans && api_->getFanSpeed && api_->getFanPolicy && api_->setFanPolicy && api_->setFanSpeed && api_->setDefaultFanSpeed;
        if (!ok && error) *error = "NVML library does not provide the APIs required by these tuning values";
        return ok;
    }

    bool openLiveDevices(const std::vector<GpuTuningMarkerRecord> &records, std::vector<LiveDevice> *live,
                         std::vector<std::string> *events) {
        live->clear();
        for (const GpuTuningMarkerRecord &record : records) {
            LiveDevice d;
            d.record = record;
            for (const GpuTuningDevice &candidate : devices_)
                if (gpuUuidEqual(candidate.uuid, record.uuid)) { d.index = candidate.index; break; }
            if (d.index < 0) d.index = static_cast<int>(live->size());
            const int rc = api_->getHandleByUuid(record.uuid.c_str(), &d.handle);
            if (rc != kNvmlSuccess || !d.handle) {
                if (rc == kNvmlErrorNoPermission) emitRootMessage(events);
                emit(events, "GPU " + std::to_string(d.index) + ": NVML could not open selected device (error " + std::to_string(rc) + ")");
                d.handle = nullptr;
                live->push_back(d);
                continue;
            }
            char uuid[96] = {};
            const int uuidRc = api_->getUuid(d.handle, uuid, sizeof(uuid));
            if (uuidRc != kNvmlSuccess || !gpuUuidEqual(uuid, record.uuid)) {
                emit(events, "GPU " + std::to_string(d.index) + ": NVML device identity check failed");
                d.handle = nullptr;
            }
            live->push_back(d);
        }
        return true;
    }

    bool preflight(std::vector<LiveDevice> *live, std::string *error, std::vector<std::string> *events) {
        if (!haveApplyFunctions(plan_.categories, error)) return false;
        for (size_t i = 0; i < live->size(); ++i) {
            LiveDevice &d = (*live)[i];
            if (!d.handle) { if (error) *error = "could not open every selected GPU by UUID"; return false; }
            const GpuTuningValue &v = plan_.devices[i];
            if (v.powerSet) {
                unsigned lo = 0, hi = 0;
                const int rc = api_->getPowerConstraints(d.handle, &lo, &hi);
                const uint64_t mw = static_cast<uint64_t>(v.powerWatts) * 1000u;
                if (rc != kNvmlSuccess) {
                    if (rc == kNvmlErrorNoPermission) emitRootMessage(events);
                    if (error) *error = "power constraints read failed (NVML error " + std::to_string(rc) + ")";
                    return false;
                }
                if (mw < lo || mw > hi) {
                    if (error) *error = "GPU " + std::to_string(d.index) + " power " + std::to_string(v.powerWatts) +
                                        " W is outside NVML constraints " + std::to_string(lo / 1000u) + ".." + std::to_string(hi / 1000u) + " W";
                    return false;
                }
            }
            if (v.coreOffsetSet && !offsetInDeviceRange(d.handle, kNvmlClockGraphics, v.coreOffsetMHz, error, d.index, events)) return false;
            if (v.memoryOffsetSet && !offsetInDeviceRange(d.handle, kNvmlClockMemory, v.memoryOffsetMHz, error, d.index, events)) return false;
            if (v.fanSet) {
                const int rc = api_->getNumFans(d.handle, &d.fans);
                if (rc != kNvmlSuccess || d.fans == 0) {
                    if (rc == kNvmlErrorNoPermission) emitRootMessage(events);
                    if (error) *error = "GPU " + std::to_string(d.index) + " fan control is unavailable (NVML error " + std::to_string(rc) + ")";
                    return false;
                }
            }
        }
        return true;
    }

    bool offsetInDeviceRange(void *handle, unsigned type, int requested, std::string *error, int index, std::vector<std::string> *events) {
        NvmlClockOffsetV1 info{};
        info.version = nvmlClockOffsetV1Version();
        info.type = static_cast<int>(type);
        info.pstate = static_cast<int>(kNvmlPstate0);
        const int rc = api_->getClockOffsets(handle, &info);
        if (rc != kNvmlSuccess) {
            if (rc == kNvmlErrorNoPermission) emitRootMessage(events);
            if (error) *error = "GPU " + std::to_string(index) + " clock-offset read failed (NVML error " + std::to_string(rc) + ")";
            return false;
        }
        if (requested < info.minClockOffsetMHz || requested > info.maxClockOffsetMHz) {
            if (error) *error = "GPU " + std::to_string(index) + " clock offset " + std::to_string(requested) + " MHz is outside NVML offset constraints " +
                                std::to_string(info.minClockOffsetMHz) + ".." + std::to_string(info.maxClockOffsetMHz) + " MHz";
            return false;
        }
        return true;
    }

    std::vector<GpuTuningMarkerRecord> planRecords() const {
        std::vector<GpuTuningMarkerRecord> records;
        for (const GpuTuningDevice &device : devices_) records.push_back({device.uuid, plan_.categories});
        return records;
    }

    bool applyPlan(std::vector<std::string> *events) {
        if (!platform_.root) { emitRootMessage(events); return false; }
        std::string error;
        if (!openApi(&error)) {
            if (error == rootMessage()) emitRootMessage(events);
            else emit(events, "GPU tuning unavailable: " + error + "; mining untuned");
            return false;
        }
        const std::vector<GpuTuningMarkerRecord> records = planRecords();
        std::vector<LiveDevice> live;
        openLiveDevices(records, &live, events);
        if (!preflight(&live, &error, events)) {
            emit(events, "GPU tuning refused: " + error + "; mining untuned");
            closeApi();
            return false;
        }
        if (!writeGpuTuningMarker(markerPath_, records, &error)) {
            emit(events, error + "; mining untuned");
            closeApi();
            return false;
        }
        activeRecords_ = records;
        int failedRc = kNvmlSuccess;
        for (size_t i = 0; i < live.size() && failedRc == kNvmlSuccess; ++i) {
            if (!applyDevice(live[i], plan_.devices[i], events, &failedRc)) break;
        }
        if (failedRc != kNvmlSuccess) {
            if (failedRc == kNvmlErrorNoPermission) emitRootMessage(events);
            else emit(events, "GPU tuning apply failed (NVML error " + std::to_string(failedRc) + "); restoring stock settings");
            const bool restored = restoreRecords(activeRecords_, events);
            closeApi();
            if (restored) {
                removeGpuTuningMarker(markerPath_, &error);
                activeRecords_.clear();
            } else {
                markerBlocked_ = true;
                emit(events, "GPU tuning rollback incomplete; marker retained for the next start");
            }
            return false;
        }
        closeApi();
        applied_ = true;
        emit(events, "GPU tuning applied to " + std::to_string(live.size()) + " selected GPU(s)");
        return true;
    }

    bool applyDevice(const LiveDevice &d, const GpuTuningValue &v, std::vector<std::string> *events, int *failedRc) {
        unsigned actual = 0;
        int rc = kNvmlSuccess;
        if (v.coreClockSet) {
            rc = api_->setGpuLockedClocks(d.handle, v.coreMinMHz, v.coreMaxMHz);
            if (rc != kNvmlSuccess) { *failedRc = rc; return false; }
            rc = api_->getClockInfo(d.handle, kNvmlClockGraphics, &actual);
            if (rc != kNvmlSuccess) { *failedRc = rc; return false; }
            emit(events, "GPU " + std::to_string(d.index) + ": core lock requested " + std::to_string(v.coreMinMHz) + ":" +
                             std::to_string(v.coreMaxMHz) + " MHz; current " + std::to_string(actual) + " MHz");
        }
        if (v.memoryClockSet) {
            rc = api_->setMemoryLockedClocks(d.handle, v.memoryMHz, v.memoryMHz);
            if (rc != kNvmlSuccess) { *failedRc = rc; return false; }
            rc = api_->getClockInfo(d.handle, kNvmlClockMemory, &actual);
            if (rc != kNvmlSuccess) { *failedRc = rc; return false; }
            emit(events, "GPU " + std::to_string(d.index) + ": memory lock requested " + std::to_string(v.memoryMHz) +
                             " MHz; current " + std::to_string(actual) + " MHz");
        }
        if (v.coreOffsetSet && !applyOffset(d, kNvmlClockGraphics, v.coreOffsetMHz, "core", events, failedRc)) return false;
        if (v.memoryOffsetSet && !applyOffset(d, kNvmlClockMemory, v.memoryOffsetMHz, "memory", events, failedRc)) return false;
        if (v.powerSet) {
            rc = api_->setPowerLimit(d.handle, v.powerWatts * 1000u);
            if (rc != kNvmlSuccess) { *failedRc = rc; return false; }
            rc = api_->getPowerLimit(d.handle, &actual);
            if (rc != kNvmlSuccess) { *failedRc = rc; return false; }
            emit(events, "GPU " + std::to_string(d.index) + ": power requested " + std::to_string(v.powerWatts) + " W; read-back " +
                             std::to_string(actual / 1000u) + " W");
        }
        if (v.fanSet) {
            for (unsigned fan = 0; fan < d.fans; ++fan) {
                rc = api_->setFanPolicy(d.handle, fan, kNvmlFanManual);
                if (rc != kNvmlSuccess) { *failedRc = rc; return false; }
                unsigned policy = 0;
                rc = api_->getFanPolicy(d.handle, fan, &policy);
                if (rc != kNvmlSuccess) { *failedRc = rc; return false; }
                emit(events, "GPU " + std::to_string(d.index) + ": fan " + std::to_string(fan) + " policy requested manual; read-back " +
                                 (policy == kNvmlFanManual ? "manual" : std::to_string(policy)));
                rc = api_->setFanSpeed(d.handle, fan, v.fanPercent);
                if (rc != kNvmlSuccess) { *failedRc = rc; return false; }
                unsigned speed = 0;
                rc = api_->getFanSpeed(d.handle, fan, &speed);
                if (rc != kNvmlSuccess) { *failedRc = rc; return false; }
                emit(events, "GPU " + std::to_string(d.index) + ": fan " + std::to_string(fan) + " requested " +
                                 std::to_string(v.fanPercent) + "%; read-back " + std::to_string(speed) + "%");
            }
        }
        return true;
    }

    bool applyOffset(const LiveDevice &d, unsigned type, int value, const char *label, std::vector<std::string> *events, int *failedRc) {
        NvmlClockOffsetV1 info{};
        info.version = nvmlClockOffsetV1Version();
        info.type = static_cast<int>(type);
        info.pstate = static_cast<int>(kNvmlPstate0);
        info.clockOffsetMHz = value;
        int rc = api_->setClockOffsets(d.handle, &info);
        if (rc != kNvmlSuccess) { *failedRc = rc; return false; }
        info = NvmlClockOffsetV1{};
        info.version = nvmlClockOffsetV1Version();
        info.type = static_cast<int>(type);
        info.pstate = static_cast<int>(kNvmlPstate0);
        rc = api_->getClockOffsets(d.handle, &info);
        if (rc != kNvmlSuccess) { *failedRc = rc; return false; }
        emit(events, "GPU " + std::to_string(d.index) + ": " + label + " offset requested " + signedMhz(value) +
                         " MHz; read-back " + signedMhz(info.clockOffsetMHz) + " MHz");
        return true;
    }

    static std::string signedMhz(int value) {
        return std::string(value >= 0 ? "+" : "") + std::to_string(value);
    }

    bool restoreRecords(const std::vector<GpuTuningMarkerRecord> &records, std::vector<std::string> *events) {
        std::vector<LiveDevice> live;
        openLiveDevices(records, &live, events);
        bool ok = true;
        for (LiveDevice &d : live) {
            if (!d.handle) { ok = false; continue; }
            if ((d.record.categories & kTuneCoreClock) && !restoreCoreClock(d, events)) ok = false;
            if ((d.record.categories & kTuneMemoryClock) && !restoreMemoryClock(d, events)) ok = false;
            if ((d.record.categories & kTuneCoreOffset) && !restoreOffset(d, kNvmlClockGraphics, "core", events)) ok = false;
            if ((d.record.categories & kTuneMemoryOffset) && !restoreOffset(d, kNvmlClockMemory, "memory", events)) ok = false;
            if ((d.record.categories & kTunePower) && !restorePower(d, events)) ok = false;
            if ((d.record.categories & kTuneFan) && !restoreFans(d, events)) ok = false;
        }
        return ok;
    }

    bool restoreCoreClock(const LiveDevice &d, std::vector<std::string> *events) {
        if (!api_->resetGpuLockedClocks || !api_->getClockInfo) return restoreMissing(d, "core clock", events);
        const int rc = api_->resetGpuLockedClocks(d.handle);
        if (rc != kNvmlSuccess) return restoreFailed(d, "core clock", rc, events);
        unsigned clock = 0;
        const int readRc = api_->getClockInfo(d.handle, kNvmlClockGraphics, &clock);
        if (readRc != kNvmlSuccess) return restoreFailed(d, "core clock read-back", readRc, events);
        emit(events, "GPU " + std::to_string(d.index) + ": core clock reset to stock; current " + std::to_string(clock) + " MHz");
        return true;
    }

    bool restoreMemoryClock(const LiveDevice &d, std::vector<std::string> *events) {
        if (!api_->resetMemoryLockedClocks || !api_->getClockInfo) return restoreMissing(d, "memory clock", events);
        const int rc = api_->resetMemoryLockedClocks(d.handle);
        if (rc != kNvmlSuccess) return restoreFailed(d, "memory clock", rc, events);
        unsigned clock = 0;
        const int readRc = api_->getClockInfo(d.handle, kNvmlClockMemory, &clock);
        if (readRc != kNvmlSuccess) return restoreFailed(d, "memory clock read-back", readRc, events);
        emit(events, "GPU " + std::to_string(d.index) + ": memory clock reset to stock; current " + std::to_string(clock) + " MHz");
        return true;
    }

    bool restoreOffset(const LiveDevice &d, unsigned type, const char *label, std::vector<std::string> *events) {
        if (!api_->setClockOffsets || !api_->getClockOffsets) return restoreMissing(d, (std::string(label) + " offset").c_str(), events);
        NvmlClockOffsetV1 info{};
        info.version = nvmlClockOffsetV1Version();
        info.type = static_cast<int>(type);
        info.pstate = static_cast<int>(kNvmlPstate0);
        info.clockOffsetMHz = 0;
        int rc = api_->setClockOffsets(d.handle, &info);
        if (rc != kNvmlSuccess) return restoreFailed(d, (std::string(label) + " offset").c_str(), rc, events);
        info = NvmlClockOffsetV1{};
        info.version = nvmlClockOffsetV1Version();
        info.type = static_cast<int>(type);
        info.pstate = static_cast<int>(kNvmlPstate0);
        rc = api_->getClockOffsets(d.handle, &info);
        if (rc != kNvmlSuccess) return restoreFailed(d, (std::string(label) + " offset read-back").c_str(), rc, events);
        emit(events, "GPU " + std::to_string(d.index) + ": " + label + " offset reset to 0 MHz; read-back " + signedMhz(info.clockOffsetMHz) + " MHz");
        return info.clockOffsetMHz == 0;
    }

    bool restorePower(const LiveDevice &d, std::vector<std::string> *events) {
        if (!api_->getPowerDefaultLimit || !api_->setPowerLimit || !api_->getPowerLimit) return restoreMissing(d, "power limit", events);
        unsigned stock = 0;
        int rc = api_->getPowerDefaultLimit(d.handle, &stock);
        if (rc != kNvmlSuccess) return restoreFailed(d, "default power read", rc, events);
        rc = api_->setPowerLimit(d.handle, stock);
        if (rc != kNvmlSuccess) return restoreFailed(d, "power limit", rc, events);
        unsigned actual = 0;
        rc = api_->getPowerLimit(d.handle, &actual);
        if (rc != kNvmlSuccess) return restoreFailed(d, "power read-back", rc, events);
        emit(events, "GPU " + std::to_string(d.index) + ": power reset to default " + std::to_string(stock / 1000u) +
                         " W; read-back " + std::to_string(actual / 1000u) + " W");
        return actual == stock;
    }

    bool restoreFans(const LiveDevice &d, std::vector<std::string> *events) {
        if (!api_->getNumFans || !api_->setDefaultFanSpeed || !api_->setFanPolicy || !api_->getFanPolicy || !api_->getFanSpeed)
            return restoreMissing(d, "fan control", events);
        unsigned count = 0;
        int rc = api_->getNumFans(d.handle, &count);
        if (rc != kNvmlSuccess || !count) return restoreFailed(d, "fan count", rc, events);
        bool ok = true;
        for (unsigned fan = 0; fan < count; ++fan) {
            rc = api_->setDefaultFanSpeed(d.handle, fan);
            if (rc != kNvmlSuccess) { restoreFailed(d, "fan default", rc, events, fan); ok = false; continue; }
            rc = api_->setFanPolicy(d.handle, fan, kNvmlFanAuto);
            if (rc != kNvmlSuccess) { restoreFailed(d, "fan auto policy", rc, events, fan); ok = false; continue; }
            unsigned policy = 99, speed = 0;
            rc = api_->getFanPolicy(d.handle, fan, &policy);
            if (rc != kNvmlSuccess) { restoreFailed(d, "fan policy read-back", rc, events, fan); ok = false; continue; }
            rc = api_->getFanSpeed(d.handle, fan, &speed);
            if (rc != kNvmlSuccess) { restoreFailed(d, "fan speed read-back", rc, events, fan); ok = false; continue; }
            emit(events, "GPU " + std::to_string(d.index) + ": fan " + std::to_string(fan) + " reset to auto; policy " +
                             (policy == kNvmlFanAuto ? "auto" : std::to_string(policy)) + ", speed " + std::to_string(speed) + "%");
            ok = ok && policy == kNvmlFanAuto;
        }
        return ok;
    }

    bool restoreMissing(const LiveDevice &d, const char *what, std::vector<std::string> *events) {
        emit(events, "GPU " + std::to_string(d.index) + ": " + what + " restore API is unavailable");
        return false;
    }

    bool restoreFailed(const LiveDevice &d, const char *what, int rc, std::vector<std::string> *events, int fan = -1) {
        if (rc == kNvmlErrorNoPermission) emitRootMessage(events);
        std::string line = "GPU " + std::to_string(d.index) + ": ";
        if (fan >= 0) line += "fan " + std::to_string(fan) + " ";
        line += what;
        line += " restore failed (NVML error " + std::to_string(rc) + ")";
        emit(events, line);
        return false;
    }
};

}  // namespace om
