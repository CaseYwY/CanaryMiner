// Human-readable rendering of a stats-v1 document for `canary-miner status`.

#pragma once

#include <stdint.h>
#include <stdio.h>

#include <string>

#include "json_dom.h"
#include "stats.h"

namespace om {

namespace status_detail {
inline std::string str(const JsonValue *v) { return v && v->isString() ? v->text : std::string(); }
inline double num(const JsonValue *v) { return v && v->isNumber() ? atof(v->text.c_str()) : 0.0; }
inline std::string u64(const JsonValue *o, const char *key) {
    const JsonValue *v = o ? o->find(key) : nullptr;
    return v && v->isNumber() ? v->text : std::string("0");
}
inline std::string outcomes(const JsonValue *o) {
    return "submitted " + u64(o, "submitted") + ", accepted " + u64(o, "accepted") + ", rejected " + u64(o, "rejected") +
           ", stale " + u64(o, "stale") + ", local stale " + u64(o, "local_stale") + ", unknown " + u64(o, "unknown") +
           ", pending " + u64(o, "pending");
}
inline std::string fixed1(double v) {
    char b[32];
    snprintf(b, sizeof(b), "%.1f", v);
    return b;
}
}  // namespace status_detail

/** False when `root` is not a stats-v1 document (the reason is in *error). */
inline bool renderStatusText(const JsonValue &root, std::string *text, std::string *error) {
    using namespace status_detail;
    if (!root.isObject() || str(root.find("schema")) != kStatsSchemaId) {
        if (error) *error = std::string("the endpoint did not return a ") + kStatsSchemaId + " document";
        return false;
    }
    std::string t;
    const long up = static_cast<long>(num(root.find("uptime_seconds")));
    char dur[48];
    snprintf(dur, sizeof(dur), "%02ld:%02ld:%02ld", up / 3600, (up / 60) % 60, up % 60);
    t += "CanaryMiner " + str(root.find("version")) + "  state: " + str(root.find("state")) + "  uptime " + dur + "\n";
    if (const JsonValue *pool = root.find("pool")) {
        t += "pool: " + (str(pool->find("endpoint")).empty() ? std::string("none") : str(pool->find("endpoint"))) +
             (str(pool->find("preset")).empty() ? "" : " (" + str(pool->find("preset")) + ")") + "  " + str(pool->find("state"));
        if (!str(pool->find("last_error")).empty()) t += "  last error: " + str(pool->find("last_error"));
        t += "\n";
    }
    if (const JsonValue *devs = root.find("devices")) {
        for (const JsonValue &d : devs->items) {
            const JsonValue *rate = d.find("rate"), *avg = d.find("rate_average");
            t += "GPU " + u64(&d, "index") + "  " + str(d.find("name")) + " [" + str(d.find("family")) + "]  " + fixed1(num(rate ? rate->find("value") : nullptr)) +
                 " T MAC/s (avg " + fixed1(num(avg ? avg->find("value") : nullptr)) + ")";
            const JsonValue *w = d.find("power_watts"), *c = d.find("temperature_c");
            if (w && w->isNumber()) t += "  " + fixed1(num(w)) + " W";
            if (c && c->isNumber()) t += "  " + c->text + " C";
            t += "\n    shares: " + outcomes(d.find("outcomes")) + "\n";
        }
    }
    t += "total shares: " + outcomes(root.find("outcomes")) + "\n";
    if (const JsonValue *fee = root.find("donation"))
        t += "donation: " + str(fee->find("state")) + " (" + str(fee->find("note")) + ")\n";
    *text = t;
    return true;
}

}  // namespace om
