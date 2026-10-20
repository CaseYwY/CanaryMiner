// H-034 GPU tuning parser and fake-NVML lifecycle tests. CPU-only: no NVML library or GPU calls.

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "../src/product/gpu_tuning.h"
#include "product_test_util.h"

using namespace om;

namespace {

struct FakeGpu {
    std::string uuid;
    unsigned core = 1000, memory = 5000;
    int coreOffset = 0, memoryOffset = 0;
    unsigned power = 300000, defaultPower = 300000;
    unsigned fanCount = 2;
    std::vector<unsigned> fanPolicy{0, 0}, fanSpeed{40, 45};
};

struct FakeNvml {
    std::vector<FakeGpu> gpus;
    std::vector<std::string> calls;
    int failSetPower = 0;
    static FakeNvml *active;

    FakeNvml() {
        gpus.resize(2);
        gpus[0].uuid = "GPU-aaaaaaaa-0000-0000-0000-000000000001";
        gpus[1].uuid = "GPU-aaaaaaaa-0000-0000-0000-000000000002";
        active = this;
    }

    static FakeGpu *gpu(void *handle) {
        const uintptr_t value = reinterpret_cast<uintptr_t>(handle);
        return value && value <= active->gpus.size() ? &active->gpus[value - 1] : nullptr;
    }
    static int idx(void *handle) { return static_cast<int>(reinterpret_cast<uintptr_t>(handle) - 1); }
    static int init() { active->calls.push_back("init"); return 0; }
    static int shutdown() { active->calls.push_back("shutdown"); return 0; }
    static int getHandle(const char *uuid, void **handle) {
        for (size_t i = 0; i < active->gpus.size(); ++i) {
            if (active->gpus[i].uuid == uuid) { *handle = reinterpret_cast<void *>(i + 1); return 0; }
        }
        return 6;
    }
    static int getUuid(void *handle, char *out, unsigned size) {
        FakeGpu *d = gpu(handle);
        if (!d || !size) return 2;
        std::snprintf(out, size, "%s", d->uuid.c_str());
        return 0;
    }
    static int getClock(void *handle, unsigned type, unsigned *out) {
        FakeGpu *d = gpu(handle);
        if (!d) return 2;
        active->calls.push_back("read_clock_" + std::to_string(type));
        *out = type == kNvmlClockGraphics ? d->core : d->memory;
        return 0;
    }
    static int setCore(void *handle, unsigned lo, unsigned hi) {
        FakeGpu *d = gpu(handle);
        if (!d) return 2;
        active->calls.push_back("set_core");
        d->core = lo == hi ? lo : lo;
        return 0;
    }
    static int resetCore(void *handle) {
        FakeGpu *d = gpu(handle);
        if (!d) return 2;
        active->calls.push_back("reset_core");
        d->core = 1000;
        return 0;
    }
    static int setMemory(void *handle, unsigned lo, unsigned) {
        FakeGpu *d = gpu(handle);
        if (!d) return 2;
        active->calls.push_back("set_memory_clock");
        d->memory = lo;
        return 0;
    }
    static int resetMemory(void *handle) {
        FakeGpu *d = gpu(handle);
        if (!d) return 2;
        active->calls.push_back("reset_memory_clock");
        d->memory = 5000;
        return 0;
    }
    static int getOffsets(void *handle, NvmlClockOffsetV1 *offset) {
        FakeGpu *d = gpu(handle);
        if (!d) return 2;
        offset->minClockOffsetMHz = -1000;
        offset->maxClockOffsetMHz = 1500;
        offset->clockOffsetMHz = offset->type == static_cast<int>(kNvmlClockGraphics) ? d->coreOffset : d->memoryOffset;
        return 0;
    }
    static int setOffsets(void *handle, NvmlClockOffsetV1 *offset) {
        FakeGpu *d = gpu(handle);
        if (!d) return 2;
        active->calls.push_back(offset->type == static_cast<int>(kNvmlClockGraphics) ? "set_core_offset" : "set_memory_offset");
        if (offset->type == static_cast<int>(kNvmlClockGraphics)) d->coreOffset = offset->clockOffsetMHz;
        else d->memoryOffset = offset->clockOffsetMHz;
        return 0;
    }
    static int getPower(void *handle, unsigned *out) {
        FakeGpu *d = gpu(handle);
        if (!d) return 2;
        *out = d->power;
        return 0;
    }
    static int getPowerConstraints(void *handle, unsigned *lo, unsigned *hi) {
        if (!gpu(handle)) return 2;
        *lo = 100000;
        *hi = 500000;
        return 0;
    }
    static int getPowerDefault(void *handle, unsigned *out) {
        FakeGpu *d = gpu(handle);
        if (!d) return 2;
        *out = d->defaultPower;
        return 0;
    }
    static int setPower(void *handle, unsigned value) {
        FakeGpu *d = gpu(handle);
        if (!d) return 2;
        active->calls.push_back("set_power");
        if (active->failSetPower) return active->failSetPower;
        d->power = value;
        return 0;
    }
    static int getNumFans(void *handle, unsigned *out) {
        FakeGpu *d = gpu(handle);
        if (!d) return 2;
        *out = d->fanCount;
        return 0;
    }
    static int getFanSpeed(void *handle, unsigned fan, unsigned *out) {
        FakeGpu *d = gpu(handle);
        if (!d || fan >= d->fanCount) return 2;
        *out = d->fanSpeed[fan];
        return 0;
    }
    static int getFanPolicy(void *handle, unsigned fan, unsigned *out) {
        FakeGpu *d = gpu(handle);
        if (!d || fan >= d->fanCount) return 2;
        *out = d->fanPolicy[fan];
        return 0;
    }
    static int setFanPolicy(void *handle, unsigned fan, unsigned policy) {
        FakeGpu *d = gpu(handle);
        if (!d || fan >= d->fanCount) return 2;
        active->calls.push_back(policy == kNvmlFanManual ? "set_fan_manual" : "set_fan_auto");
        d->fanPolicy[fan] = policy;
        return 0;
    }
    static int setFanSpeed(void *handle, unsigned fan, unsigned speed) {
        FakeGpu *d = gpu(handle);
        if (!d || fan >= d->fanCount) return 2;
        active->calls.push_back("set_fan_speed");
        d->fanSpeed[fan] = speed;
        return 0;
    }
    static int setDefaultFan(void *handle, unsigned fan) {
        FakeGpu *d = gpu(handle);
        if (!d || fan >= d->fanCount) return 2;
        active->calls.push_back("set_fan_default");
        d->fanPolicy[fan] = kNvmlFanAuto;
        d->fanSpeed[fan] = fan ? 45 : 40;
        return 0;
    }

    NvmlTuningApi table() {
        NvmlTuningApi a;
        a.init = init; a.shutdown = shutdown; a.getHandleByUuid = getHandle; a.getUuid = getUuid;
        a.getClockInfo = getClock; a.setGpuLockedClocks = setCore; a.resetGpuLockedClocks = resetCore;
        a.setMemoryLockedClocks = setMemory; a.resetMemoryLockedClocks = resetMemory; a.getClockOffsets = getOffsets; a.setClockOffsets = setOffsets;
        a.getPowerLimit = getPower; a.getPowerConstraints = getPowerConstraints; a.getPowerDefaultLimit = getPowerDefault; a.setPowerLimit = setPower;
        a.getNumFans = getNumFans; a.getFanSpeed = getFanSpeed; a.getFanPolicy = getFanPolicy; a.setFanPolicy = setFanPolicy;
        a.setFanSpeed = setFanSpeed; a.setDefaultFanSpeed = setDefaultFan;
        return a;
    }
};

FakeNvml *FakeNvml::active = nullptr;

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("canary-h034-" + std::to_string(tick));
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    std::string stateDir() const { return (path / "state").string(); }
};

std::vector<GpuTuningDevice> devices(FakeNvml &fake, size_t count = 2) {
    std::vector<GpuTuningDevice> out;
    for (size_t i = 0; i < count; ++i) out.push_back({static_cast<int>(i), fake.gpus[i].uuid});
    return out;
}

bool hasLine(const std::vector<std::string> &lines, const std::string &needle) {
    for (const std::string &line : lines) if (line.find(needle) != std::string::npos) return true;
    return false;
}

bool hasGpuTuningMarker(const std::string &path) { return gpuTuningMarkerExists(path); }

bool hasCall(const std::vector<std::string> &calls, const std::string &needle) {
    for (const std::string &line : calls) if (line == needle) return true;
    return false;
}

void testParser() {
    GpuTuningConfig cfg;
    cfg.gpuCclockSet = true; cfg.gpuCclock = "2550:2600,2400";
    cfg.gpuMclockSet = true; cfg.gpuMclock = "7001";
    cfg.gpuCoffsetSet = true; cfg.gpuCoffset = "+599,0";
    cfg.gpuMoffsetSet = true; cfg.gpuMoffset = "0";
    cfg.gpuPlimitSet = true; cfg.gpuPlimit = "175";
    cfg.gpuFanSet = true; cfg.gpuFan = "80,75";
    GpuTuningPlan plan;
    std::string error;
    const bool validPlan = parseGpuTuningPlan(cfg, 2, &plan, &error);
    CHECK(validPlan);
    if (!validPlan) return;
    CHECK(plan.devices.size() == 2 && plan.categories == kTuneAll);
    CHECK(plan.devices[0].coreMinMHz == 2550 && plan.devices[0].coreMaxMHz == 2600);
    CHECK(plan.devices[1].coreMinMHz == 2400 && plan.devices[1].coreMaxMHz == 2400);
    CHECK(plan.devices[0].memoryMHz == 7001 && plan.devices[1].memoryMHz == 7001);
    CHECK(plan.devices[0].coreOffsetMHz == 599 && plan.devices[1].coreOffsetMHz == 0);
    CHECK(plan.devices[0].fanPercent == 80 && plan.devices[1].fanPercent == 75);
    CHECK(plan.devices[0].powerWatts == 175 && plan.devices[1].powerWatts == 175);

    GpuTuningConfig broadcast;
    broadcast.gpuCclockSet = true; broadcast.gpuCclock = "2500";
    broadcast.gpuMoffsetSet = true; broadcast.gpuMoffset = "-1000";
    const bool validBroadcast = parseGpuTuningPlan(broadcast, 3, &plan, &error);
    CHECK(validBroadcast);
    if (!validBroadcast) return;
    CHECK(plan.devices.size() == 3 && plan.devices[2].coreMinMHz == 2500 && plan.devices[2].coreMaxMHz == 2500);
    CHECK(plan.devices[0].memoryOffsetMHz == -1000 && plan.devices[2].memoryOffsetMHz == -1000);

    for (const auto &bad : {
             std::make_pair("clock", "99"), {"clock", "5001"}, {"clock", "2600:2500"}, {"clock", "2000:"},
             {"mclock", "7001:7001"}, {"offset", "+1501"}, {"offset", "-1001"}, {"power", "0"}, {"fan", "29"},
             {"fan", "101"}, {"list", "2500,,2600"}, {"list", "2500,2600,2700"}}) {
        GpuTuningConfig invalid;
        if (std::string_view(bad.first) == "clock") { invalid.gpuCclockSet = true; invalid.gpuCclock = bad.second; }
        else if (std::string_view(bad.first) == "mclock") { invalid.gpuMclockSet = true; invalid.gpuMclock = bad.second; }
        else if (std::string_view(bad.first) == "offset") { invalid.gpuCoffsetSet = true; invalid.gpuCoffset = bad.second; }
        else if (std::string_view(bad.first) == "power") { invalid.gpuPlimitSet = true; invalid.gpuPlimit = bad.second; }
        else if (std::string_view(bad.first) == "fan") { invalid.gpuFanSet = true; invalid.gpuFan = bad.second; }
        else { invalid.gpuCclockSet = true; invalid.gpuCclock = bad.second; }
        CHECK(!parseGpuTuningPlan(invalid, 2, &plan, &error));
    }
    GpuTuningConfig delayOnly;
    delayOnly.ocDelaySet = true; delayOnly.ocDelaySeconds = 1;
    CHECK(!parseGpuTuningPlan(delayOnly, 2, &plan, &error));
}

void testApplyAndRestore() {
    FakeNvml fake;
    NvmlTuningApi table = fake.table();
    TempDir temp;
    GpuTuningConfig cfg;
    cfg.gpuCclockSet = true; cfg.gpuCclock = "2550";
    cfg.gpuMclockSet = true; cfg.gpuMclock = "7001";
    cfg.gpuCoffsetSet = true; cfg.gpuCoffset = "+599";
    cfg.gpuMoffsetSet = true; cfg.gpuMoffset = "0";
    cfg.gpuPlimitSet = true; cfg.gpuPlimit = "175";
    cfg.gpuFanSet = true; cfg.gpuFan = "80";
    std::vector<std::string> events;
    {
        GpuTuning tuning(cfg, devices(fake, 1), temp.stateDir(), &table, {false, false, true});
        tuning.start(100, &events);
        CHECK(fake.gpus[0].core == 2550 && fake.gpus[0].memory == 7001);
        CHECK(fake.gpus[0].coreOffset == 599 && fake.gpus[0].memoryOffset == 0);
        CHECK(fake.gpus[0].power == 175000 && fake.gpus[0].fanPolicy[0] == kNvmlFanManual && fake.gpus[0].fanSpeed[0] == 80);
        CHECK(hasLine(events, "core lock requested 2550:2550 MHz"));
        CHECK(hasLine(events, "memory lock requested 7001 MHz"));
        CHECK(hasLine(events, "core offset requested +599 MHz; read-back +599 MHz"));
        CHECK(hasLine(events, "power requested 175 W; read-back 175 W"));
        CHECK(hasLine(events, "fan 1 requested 80%; read-back 80%"));
        CHECK(hasGpuTuningMarker(gpuTuningMarkerPath(temp.stateDir())));
        std::vector<std::string> setters;
        for (const std::string &call : fake.calls)
            if (call.rfind("set_", 0) == 0) setters.push_back(call);
        const std::vector<std::string> expected = {"set_core", "set_memory_clock", "set_core_offset", "set_memory_offset", "set_power",
                                                   "set_fan_manual", "set_fan_speed", "set_fan_manual", "set_fan_speed"};
        CHECK(setters == expected);
        tuning.stop(&events);
    }
    CHECK(fake.gpus[0].core == 1000 && fake.gpus[0].memory == 5000);
    CHECK(fake.gpus[0].coreOffset == 0 && fake.gpus[0].memoryOffset == 0);
    CHECK(fake.gpus[0].power == 300000 && fake.gpus[0].fanPolicy[0] == kNvmlFanAuto && fake.gpus[0].fanSpeed[0] == 40);
    CHECK(!hasGpuTuningMarker(gpuTuningMarkerPath(temp.stateDir())));
    CHECK(hasLine(events, "stock settings restored"));
    CHECK(hasCall(fake.calls, "reset_core") && hasCall(fake.calls, "reset_memory_clock"));
}

void testDelayAndUnsupportedPlatforms() {
    FakeNvml fake;
    NvmlTuningApi table = fake.table();
    TempDir temp;
    GpuTuningConfig delayed;
    delayed.gpuFanSet = true; delayed.gpuFan = "80";
    delayed.ocDelaySet = true; delayed.ocDelaySeconds = 3;
    GpuTuning tuning(delayed, devices(fake, 1), temp.stateDir(), &table, {false, false, true});
    std::vector<std::string> events;
    tuning.start(1000, &events);
    CHECK(fake.gpus[0].fanPolicy[0] == kNvmlFanAuto && !hasCall(fake.calls, "set_fan_manual"));
    tuning.tick(3999, &events);
    CHECK(!hasCall(fake.calls, "set_fan_manual"));
    tuning.tick(4000, &events);
    CHECK(fake.gpus[0].fanPolicy[0] == kNvmlFanManual && hasCall(fake.calls, "set_fan_manual"));
    tuning.stop(&events);

    for (const GpuTuningPlatform platform : {GpuTuningPlatform{true, false, false}, GpuTuningPlatform{false, true, true}}) {
        FakeNvml unsupportedFake;
        NvmlTuningApi unsupportedTable = unsupportedFake.table();
        GpuTuning unsupported(delayed, devices(unsupportedFake, 1), temp.stateDir(), &unsupportedTable, platform);
        std::vector<std::string> unsupportedEvents;
        unsupported.start(0, &unsupportedEvents);
        CHECK(hasLine(unsupportedEvents, gpuTuningUnsupportedMessage()));
        CHECK(unsupportedFake.calls.empty());
    }

    FakeNvml unprivilegedFake;
    NvmlTuningApi unprivilegedTable = unprivilegedFake.table();
    GpuTuning unprivileged(delayed, devices(unprivilegedFake, 1), temp.stateDir(), &unprivilegedTable, {false, false, false});
    std::vector<std::string> unprivilegedEvents;
    unprivileged.start(0, &unprivilegedEvents);
    CHECK(hasLine(unprivilegedEvents, "GPU tuning needs root: run with sudo, or tune with nvidia-smi"));
    CHECK(unprivilegedFake.calls.empty());

    GpuTuningConfig resetConfig;
    resetConfig.ocResetOnly = true;
    GpuTuning unprivilegedReset(resetConfig, devices(unprivilegedFake, 1), temp.stateDir(), &unprivilegedTable, {false, false, false});
    std::vector<std::string> resetEvents;
    CHECK(!unprivilegedReset.resetOnly(&resetEvents));
    CHECK(hasLine(resetEvents, "GPU tuning needs root: run with sudo, or tune with nvidia-smi"));
    CHECK(unprivilegedFake.calls.empty());
}

void testMarkerRecoveryAndResetOnly() {
    FakeNvml fake;
    NvmlTuningApi table = fake.table();
    TempDir temp;
    const std::string marker = gpuTuningMarkerPath(temp.stateDir());
    const std::vector<GpuTuningMarkerRecord> records{{fake.gpus[0].uuid, kTunePower}};
    std::string error;
    CHECK(writeGpuTuningMarker(marker, records, &error));
    fake.gpus[0].power = 175000;
    GpuTuningConfig none;
    std::vector<std::string> events;
    GpuTuning recovery(none, devices(fake, 1), temp.stateDir(), &table, {false, false, true});
    recovery.start(0, &events);
    CHECK(fake.gpus[0].power == 300000 && !hasGpuTuningMarker(marker));
    CHECK(hasLine(events, "recovered the previous run's restore marker"));

    fake.gpus[0].core = 2550;
    fake.gpus[0].memory = 7001;
    fake.gpus[0].coreOffset = 599;
    fake.gpus[0].memoryOffset = 0;
    fake.gpus[0].power = 175000;
    fake.gpus[0].fanPolicy[0] = kNvmlFanManual;
    fake.gpus[0].fanSpeed[0] = 80;
    events.clear();
    GpuTuning reset(none, devices(fake, 1), temp.stateDir(), &table, {false, false, true});
    CHECK(reset.resetOnly(&events));
    CHECK(fake.gpus[0].core == 1000 && fake.gpus[0].memory == 5000);
    CHECK(fake.gpus[0].coreOffset == 0 && fake.gpus[0].memoryOffset == 0 && fake.gpus[0].power == 300000);
    CHECK(fake.gpus[0].fanPolicy[0] == kNvmlFanAuto && !hasGpuTuningMarker(marker));
    CHECK(hasLine(events, "stock settings restored; exiting"));
}

void testPartialFailureAndHardKillMarker() {
    FakeNvml fake;
    NvmlTuningApi table = fake.table();
    TempDir temp;
    GpuTuningConfig cfg;
    cfg.gpuCclockSet = true; cfg.gpuCclock = "2400";
    cfg.gpuPlimitSet = true; cfg.gpuPlimit = "175";
    fake.failSetPower = kNvmlErrorNoPermission;
    std::vector<std::string> events;
    GpuTuning partial(cfg, devices(fake, 1), temp.stateDir(), &table, {false, false, true});
    partial.start(0, &events);
    CHECK(hasLine(events, "GPU tuning needs root: run with sudo, or tune with nvidia-smi"));
    CHECK(fake.gpus[0].core == 1000 && fake.gpus[0].power == 300000);
    CHECK(hasGpuTuningMarker(gpuTuningMarkerPath(temp.stateDir())));
    fake.failSetPower = 0;
    partial.stop(&events);
    CHECK(!hasGpuTuningMarker(gpuTuningMarkerPath(temp.stateDir())));

    events.clear();
    {
        GpuTuning applied(cfg, devices(fake, 1), temp.stateDir(), &table, {false, false, true});
        applied.start(0, &events);
        CHECK(fake.gpus[0].core == 2400 && hasGpuTuningMarker(gpuTuningMarkerPath(temp.stateDir())));
    }  // Simulated process kill: no stop() call; the durable marker remains.
    CHECK(hasGpuTuningMarker(gpuTuningMarkerPath(temp.stateDir())));
    GpuTuning nextStart(GpuTuningConfig{}, devices(fake, 1), temp.stateDir(), &table, {false, false, true});
    nextStart.start(1, &events);
    CHECK(fake.gpus[0].core == 1000 && !hasGpuTuningMarker(gpuTuningMarkerPath(temp.stateDir())));
}

}  // namespace

int main() {
    testParser();
    testApplyAndRestore();
    testDelayAndUnsupportedPlatforms();
    testMarkerRecoveryAndResetOnly();
    testPartialFailureAndHardKillMarker();
    // This release ships with tuning off: miner.cu never constructs GpuTuning and prints this notice instead.
    CHECK_MSG("tuning off in this release", !kGpuTuningShipped);
    CHECK_MSG("notice says not supported", std::string(gpuTuningUnsupportedMessage()).find("not supported") != std::string::npos);
    return finishTests("test_product_gpu_tuning");
}
