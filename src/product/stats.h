// stats-v1: the read-only snapshot served at GET /api/v1/stats, its JSON writer, and the thread-safe holder the run loop
// publishes into. Draft schema: stats-v1.schema.json. No field here is a secret: the wallet, worker name and pool password
// never enter a snapshot (the pool is reported as host:port and preset name only).
//
// Rates carry their label: `unit` is "T MAC/s" (trillion multiply-accumulates per second, one counted work unit = tile x k MACs),
// `basis` says how it was measured ("interval_native": the last report interval as the native counted it, uncalibrated against
// wall-clock telemetry). Outcome counts keep every terminal class apart; `unknown` is a share whose outcome was never learned.

#pragma once

#include <stdint.h>
#include <stdio.h>

#include <mutex>
#include <string>
#include <vector>

#include "json_dom.h"

namespace om {

constexpr const char *kStatsSchemaId = "canaryminer-stats-v1";

struct StatsOutcomes {
    uint64_t submitted = 0, acknowledged = 0, accepted = 0, rejected = 0, stale = 0, unknown = 0, pending = 0, guardRetired = 0;
    uint64_t hostInvalid = 0, submitFailed = 0, localStale = 0;
};

struct StatsDevice {
    int index = 0;
    std::string uuid, pciBus, name, family;  // family: SM120
    int ccMajor = 0, ccMinor = 0;
    double rateTmacs = 0;     // last report interval
    double rateAvgTmacs = 0;  // since start
    double attemptsPerSecond = 0;
    double powerWatts = -1;  // negative = not available
    int temperatureC = -1;
    StatsOutcomes outcomes;
};

struct StatsFee {  // the developer donation (JSON key "donation")
    bool enabled = true;
    std::string state = "not_active";  // off | not_active | user | donation
    bool destinationConfigured = false;
    std::string destinationSha256;
    uint64_t cycleMinutes = 100, windowMinutes = 1;
    uint64_t cycle = 0, userSeconds = 0, feeSeconds = 0, debtSeconds = 0;  // this cycle (feeSeconds = fee time mined this cycle)
    bool windowActive = false;
    uint64_t poolFailures = 0, windowsCompleted = 0;
    std::string pool, login;
    StatsOutcomes outcomes;  // shares mined for the fee destination; never part of the device or total outcomes
    std::string note = "developer donation inactive in this build";
};

struct StatsSnapshot {
    std::string version;
    std::string state = "starting";  // starting | running | draining | stopped
    double uptimeSeconds = 0;
    std::string poolEndpoint, poolPreset, poolState = "none", poolLastError;
    std::vector<StatsDevice> devices;
    StatsFee fee;
};

inline std::string statsNumber(double v) {
    char b[48];
    snprintf(b, sizeof(b), "%.6g", v < 0 ? 0.0 : v);
    return b;
}

inline StatsOutcomes statsTotals(const StatsSnapshot &s) {
    StatsOutcomes t;
    auto add = [](uint64_t a, uint64_t b) { return b > UINT64_MAX - a ? UINT64_MAX : a + b; };
    for (const auto &d : s.devices) {
        t.submitted = add(t.submitted, d.outcomes.submitted);
        t.acknowledged = add(t.acknowledged, d.outcomes.acknowledged);
        t.accepted = add(t.accepted, d.outcomes.accepted);
        t.rejected = add(t.rejected, d.outcomes.rejected);
        t.stale = add(t.stale, d.outcomes.stale);
        t.unknown = add(t.unknown, d.outcomes.unknown);
        t.pending = add(t.pending, d.outcomes.pending);
        t.guardRetired = add(t.guardRetired, d.outcomes.guardRetired);
        t.hostInvalid = add(t.hostInvalid, d.outcomes.hostInvalid);
        t.submitFailed = add(t.submitFailed, d.outcomes.submitFailed);
        t.localStale = add(t.localStale, d.outcomes.localStale);
    }
    return t;
}

inline std::string outcomesJson(const StatsOutcomes &o) {
    return "{\"submitted\":" + std::to_string(o.submitted) + ",\"acknowledged\":" + std::to_string(o.acknowledged) +
           ",\"accepted\":" + std::to_string(o.accepted) + ",\"rejected\":" + std::to_string(o.rejected) +
           ",\"stale\":" + std::to_string(o.stale) + ",\"unknown\":" + std::to_string(o.unknown) +
           ",\"pending\":" + std::to_string(o.pending) + ",\"guard_retired\":" + std::to_string(o.guardRetired) +
           ",\"host_invalid\":" + std::to_string(o.hostInvalid) + ",\"submit_failed\":" + std::to_string(o.submitFailed) +
           ",\"local_stale\":" + std::to_string(o.localStale) + "}";
}

inline std::string statsToJson(const StatsSnapshot &s) {
    std::string j = "{\"schema\":" + jsonQuote(kStatsSchemaId) + ",\"version\":" + jsonQuote(s.version) + ",\"state\":" + jsonQuote(s.state) +
                    ",\"uptime_seconds\":" + statsNumber(s.uptimeSeconds);
    j += ",\"pool\":{\"endpoint\":" + jsonQuote(s.poolEndpoint) + ",\"preset\":" + jsonQuote(s.poolPreset) + ",\"state\":" +
         jsonQuote(s.poolState) + ",\"last_error\":" + jsonQuote(s.poolLastError) + "}";
    j += ",\"devices\":[";
    for (size_t i = 0; i < s.devices.size(); ++i) {
        const StatsDevice &d = s.devices[i];
        if (i) j += ",";
        j += "{\"index\":" + std::to_string(d.index) + ",\"uuid\":" + jsonQuote(d.uuid) + ",\"pci_bus\":" + jsonQuote(d.pciBus) +
             ",\"name\":" + jsonQuote(d.name) + ",\"family\":" + jsonQuote(d.family) + ",\"compute_capability\":" +
             jsonQuote(std::to_string(d.ccMajor) + "." + std::to_string(d.ccMinor));
        j += ",\"rate\":{\"value\":" + statsNumber(d.rateTmacs) + ",\"unit\":\"T MAC/s\",\"basis\":\"interval_native\"}";
        j += ",\"rate_average\":{\"value\":" + statsNumber(d.rateAvgTmacs) + ",\"unit\":\"T MAC/s\",\"basis\":\"since_start_native\"}";
        j += ",\"attempts_per_second\":" + statsNumber(d.attemptsPerSecond);
        j += ",\"power_watts\":" + (d.powerWatts < 0 ? std::string("null") : statsNumber(d.powerWatts));
        j += ",\"temperature_c\":" + (d.temperatureC < 0 ? std::string("null") : std::to_string(d.temperatureC));
        j += ",\"outcomes\":" + outcomesJson(d.outcomes) + "}";
    }
    j += "],\"outcomes\":" + outcomesJson(statsTotals(s));
    j += ",\"donation\":{\"enabled\":" + std::string(s.fee.enabled ? "true" : "false") + ",\"state\":" + jsonQuote(s.fee.state) +
         ",\"destination_configured\":" + (s.fee.destinationConfigured ? "true" : "false") +
         ",\"cycle_minutes\":" + std::to_string(s.fee.cycleMinutes) +
         ",\"window_minutes\":" + std::to_string(s.fee.windowMinutes) + ",\"cycle\":" + std::to_string(s.fee.cycle) +
         ",\"user_seconds\":" + std::to_string(s.fee.userSeconds) + ",\"donation_seconds\":" + std::to_string(s.fee.feeSeconds) +
         ",\"debt_seconds\":" + std::to_string(s.fee.debtSeconds) + ",\"window_active\":" + (s.fee.windowActive ? "true" : "false") +
         ",\"pool_failures\":" + std::to_string(s.fee.poolFailures) + ",\"windows_completed\":" + std::to_string(s.fee.windowsCompleted) +
         ",\"pool\":" + jsonQuote(s.fee.pool) + ",\"login\":" + jsonQuote(s.fee.login) +
         ",\"outcomes\":" + outcomesJson(s.fee.outcomes) + ",\"note\":" + jsonQuote(s.fee.note) + "}}";
    return j;
}

/** Reads one stats-v1 document back into a snapshot (the supervisor aggregates its workers' endpoints this way). Unknown members are
 *  ignored; numbers that are missing read as zero, and power/temperature `null` read as "not available" (-1). */
inline bool statsFromJson(const JsonValue &root, StatsSnapshot *out, std::string *error) {
    auto str = [](const JsonValue *v) { return v && v->isString() ? v->text : std::string(); };
    auto num = [](const JsonValue *v) { return v && v->isNumber() ? atof(v->text.c_str()) : 0.0; };
    auto u64 = [](const JsonValue *v) {
        uint64_t x = 0;
        return v && v->asUint64(&x) ? x : uint64_t{0};
    };
    auto outcomes = [&](const JsonValue *o) {
        StatsOutcomes r;
        if (!o || !o->isObject()) return r;
        r.submitted = u64(o->find("submitted"));
        r.acknowledged = u64(o->find("acknowledged"));
        r.accepted = u64(o->find("accepted"));
        r.rejected = u64(o->find("rejected"));
        r.stale = u64(o->find("stale"));
        r.unknown = u64(o->find("unknown"));
        r.pending = u64(o->find("pending"));
        r.guardRetired = u64(o->find("guard_retired"));
        r.hostInvalid = u64(o->find("host_invalid"));
        r.submitFailed = u64(o->find("submit_failed"));
        r.localStale = u64(o->find("local_stale"));
        return r;
    };
    if (!root.isObject() || str(root.find("schema")) != kStatsSchemaId) {
        if (error) *error = std::string("not a ") + kStatsSchemaId + " document";
        return false;
    }
    StatsSnapshot s;
    s.version = str(root.find("version"));
    s.state = str(root.find("state"));
    s.uptimeSeconds = num(root.find("uptime_seconds"));
    if (const JsonValue *pool = root.find("pool")) {
        s.poolEndpoint = str(pool->find("endpoint"));
        s.poolPreset = str(pool->find("preset"));
        s.poolState = str(pool->find("state"));
        s.poolLastError = str(pool->find("last_error"));
    }
    if (const JsonValue *devs = root.find("devices")) {
        for (const JsonValue &d : devs->items) {
            if (!d.isObject()) continue;
            StatsDevice dev;
            dev.index = static_cast<int>(u64(d.find("index")));
            dev.uuid = str(d.find("uuid"));
            dev.pciBus = str(d.find("pci_bus"));
            dev.name = str(d.find("name"));
            dev.family = str(d.find("family"));
            const std::string cc = str(d.find("compute_capability"));
            dev.ccMajor = atoi(cc.c_str());
            const size_t dot = cc.find('.');
            dev.ccMinor = dot == std::string::npos ? 0 : atoi(cc.c_str() + dot + 1);
            dev.rateTmacs = num(d.find("rate") ? d.find("rate")->find("value") : nullptr);
            dev.rateAvgTmacs = num(d.find("rate_average") ? d.find("rate_average")->find("value") : nullptr);
            dev.attemptsPerSecond = num(d.find("attempts_per_second"));
            const JsonValue *w = d.find("power_watts"), *t = d.find("temperature_c");
            dev.powerWatts = w && w->isNumber() ? num(w) : -1;
            dev.temperatureC = t && t->isNumber() ? static_cast<int>(num(t)) : -1;
            dev.outcomes = outcomes(d.find("outcomes"));
            s.devices.push_back(dev);
        }
    }
    if (const JsonValue *f = root.find("donation")) {
        s.fee.enabled = f->find("enabled") && f->find("enabled")->isBool() ? f->find("enabled")->boolean : true;
        s.fee.state = str(f->find("state"));
        s.fee.destinationConfigured = f->find("destination_configured") && f->find("destination_configured")->boolean;
        s.fee.cycle = u64(f->find("cycle"));
        s.fee.userSeconds = u64(f->find("user_seconds"));
        s.fee.feeSeconds = u64(f->find("donation_seconds"));
        s.fee.debtSeconds = u64(f->find("debt_seconds"));
        s.fee.windowActive = f->find("window_active") && f->find("window_active")->boolean;
        s.fee.poolFailures = u64(f->find("pool_failures"));
        s.fee.windowsCompleted = u64(f->find("windows_completed"));
        s.fee.pool = str(f->find("pool"));
        s.fee.login = str(f->find("login"));
        s.fee.outcomes = outcomes(f->find("outcomes"));
        s.fee.note = str(f->find("note"));
    }
    *out = s;
    return true;
}

inline std::string versionToJson(const std::string &version, const std::string &arch) {
    return "{\"schema\":\"canaryminer-version-v1\",\"name\":\"canaryminer\",\"version\":" + jsonQuote(version) + ",\"build_arch\":" +
           jsonQuote(arch) + ",\"stats_schema\":" + jsonQuote(kStatsSchemaId) + "}";
}

/** Latest snapshot, written by the run loop's observer thread and read by the HTTP thread. */
class StatsPublisher {
   public:
    template <typename F>
    void update(F &&mutate) {
        std::lock_guard<std::mutex> lock(mu_);
        mutate(&snap_);
    }
    StatsSnapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mu_);
        return snap_;
    }

   private:
    mutable std::mutex mu_;
    StatsSnapshot snap_;
};

}  // namespace om
