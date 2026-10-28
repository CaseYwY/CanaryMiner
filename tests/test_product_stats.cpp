// stats-v1 JSON, the status renderer, the pure request router, and the loopback listener (127.0.0.1 only, ephemeral port).

#include <string>
#include <thread>

#include "../src/product/product_main.h"
#include "../src/product/stats.h"
#include "../src/product/stats_server.h"
#include "../src/product/status_view.h"
#include "../src/core/submit_outcome.h"
#include "product_test_util.h"

using namespace om;

static bool has(const std::string &h, const std::string &n) { return h.find(n) != std::string::npos; }

static StatsSnapshot sample() {
    StatsSnapshot s;
    s.version = "1.0.0-test";
    s.state = "running";
    s.uptimeSeconds = 3725.5;
    s.poolEndpoint = "prl-us.kryptex.network:7048";
    s.poolPreset = "kryptex";
    s.poolState = "mining";
    StatsDevice d;
    d.index = 0;
    d.uuid = "GPU-aaaaaaaa-0000-0000-0000-000000000001";
    d.pciBus = "0000:01:00.0";
    d.name = "NVIDIA GeForce RTX 5080";
    d.family = "SM120";
    d.ccMajor = 12;
    d.ccMinor = 0;
    d.rateTmacs = 187.91;
    d.rateAvgTmacs = 185.2;
    d.attemptsPerSecond = 41.5;
    d.powerWatts = 275.0;
    d.temperatureC = 65;
    d.outcomes.submitted = 10;
    d.outcomes.acknowledged = 9;
    d.outcomes.accepted = 7;
    d.outcomes.rejected = 1;
    d.outcomes.stale = 1;
    d.outcomes.localStale = 2;
    d.outcomes.unknown = 1;
    d.outcomes.pending = 0;
    s.devices.push_back(d);
    StatsDevice e = d;
    e.index = 1;
    e.name = "NVIDIA GeForce RTX 5080";
    e.family = "SM120";
    e.powerWatts = -1;
    e.temperatureC = -1;
    e.outcomes.accepted = 3;
    s.devices.push_back(e);
    return s;
}

int main() {
    // ---- JSON document
    const StatsSnapshot snap = sample();
    const std::string json = statsToJson(snap);
    JsonValue root;
    std::string err;
    CHECK(parseJsonStrict(json, &root, &err));
    CHECK(root.find("schema") && root.find("schema")->text == "canaryminer-stats-v1");
    CHECK(root.find("devices") && root.find("devices")->items.size() == 2);
    const JsonValue &d0 = root.find("devices")->items[0];
    CHECK(d0.find("uuid")->text == snap.devices[0].uuid && d0.find("pci_bus")->text == "0000:01:00.0" && d0.find("compute_capability")->text == "12.0");
    CHECK(d0.find("rate")->find("unit")->text == "T MAC/s" && d0.find("rate")->find("basis")->text == "interval_native");
    CHECK(d0.find("rate_average")->find("basis")->text == "since_start_native");
    CHECK(d0.find("power_watts")->isNumber() && root.find("devices")->items[1].find("power_watts")->type == JsonValue::Type::Null);
    CHECK(root.find("devices")->items[1].find("temperature_c")->type == JsonValue::Type::Null);
    const JsonValue *out = root.find("outcomes");
    CHECK(out && out->find("accepted")->text == "10" && out->find("unknown")->text == "2" &&
          out->find("submitted")->text == "20" && out->find("local_stale")->text == "4");  // totals
    for (const char *k : {"submitted", "acknowledged", "accepted", "rejected", "stale", "unknown", "pending", "guard_retired", "host_invalid", "submit_failed", "local_stale"})
        CHECK_MSG(k, d0.find("outcomes")->find(k) != nullptr);
    CHECK(om::pearlSubmitFailureKind(om::kPearlLocalStaleBeforeSendError) == PearlSubmitFailureKind::LocalStale);
    CHECK(om::pearlSubmitFailureKind("could not send the share") == PearlSubmitFailureKind::DeliveryFailure);
    CHECK(om::pearlSubmitFailureKind("pre-submit readiness failed; proof not sent") == PearlSubmitFailureKind::OtherLocalFailure);
    {
        StatsOutcomes semantics;
        // Local retirement is neither a pool rejection nor a delivery failure.
        switch (om::pearlSubmitFailureKind(om::kPearlLocalStaleBeforeSendError)) {
            case PearlSubmitFailureKind::LocalStale: ++semantics.localStale; break;
            case PearlSubmitFailureKind::DeliveryFailure: ++semantics.submitFailed; break;
            case PearlSubmitFailureKind::OtherLocalFailure: break;
        }
        CHECK(semantics.localStale == 1 && semantics.rejected == 0 && semantics.submitFailed == 0);
        // A real socket send failure remains a submit failure only.
        const std::string socketError = "could not send the share";
        switch (om::pearlSubmitFailureKind(socketError)) {
            case PearlSubmitFailureKind::LocalStale: ++semantics.localStale; break;
            case PearlSubmitFailureKind::DeliveryFailure: ++semantics.submitFailed; break;
            case PearlSubmitFailureKind::OtherLocalFailure: break;
        }
        CHECK(semantics.localStale == 1 && semantics.rejected == 0 && semantics.submitFailed == 1);
        // The run-to-stats adapter exposes only the pool's rejection count.
        MinerStats runStats;
        runStats.poolRejected = 1;
        runStats.hostInvalid = 1;
        runStats.submitFailed = semantics.submitFailed;
        runStats.localStale = semantics.localStale;
        const StatsOutcomes mapped = StatsObserver::statsOutcomesFromRun(runStats);
        CHECK(mapped.rejected == 1 && mapped.localStale == 1 && mapped.submitFailed == 1 && mapped.hostInvalid == 1);
    }
    CHECK(root.find("donation")->find("state")->text == "not_active" && root.find("donation")->find("destination_configured")->boolean == false);
    CHECK(root.find("donation")->find("enabled")->boolean == true && root.find("fee") == nullptr);  // keys renamed fee.* -> donation.*
    CHECK(root.find("donation")->find("pool") && root.find("donation")->find("login"));
    CHECK(root.find("pool")->find("endpoint")->text == "prl-us.kryptex.network:7048");
    // nothing secret can be in the document: there is no wallet/worker/pass field at all
    CHECK(!has(json, "wallet") && !has(json, "worker") && !has(json, "pass") && !has(json, "prl1"));
    CHECK(has(statsToJson(StatsSnapshot{}), "\"devices\":[]"));
    // saturating totals
    {
        StatsSnapshot big;
        big.devices.resize(2);
        big.devices[0].outcomes.accepted = UINT64_MAX;
        big.devices[1].outcomes.accepted = 5;
        CHECK(statsTotals(big).accepted == UINT64_MAX);
    }
    // special characters in strings stay valid JSON
    {
        StatsSnapshot odd;
        odd.poolLastError = "bad \"quote\" \\ and\nnewline";
        JsonValue o;
        CHECK(parseJsonStrict(statsToJson(odd), &o, &err) && o.find("pool")->find("last_error")->text == odd.poolLastError);
    }
    CHECK(has(versionToJson("1.2.3", "SM120"), "\"version\":\"1.2.3\"") && parseJsonStrict(versionToJson("1.2.3", "SM120"), &root, &err));

    // ---- status renderer
    {
        std::string text;
        CHECK(parseJsonStrict(json, &root, &err) && renderStatusText(root, &text, &err));
        CHECK(has(text, "CanaryMiner 1.0.0-test") && has(text, "state: running") && has(text, "uptime 01:02:05"));
        CHECK(has(text, "prl-us.kryptex.network:7048 (kryptex)") && has(text, "GPU 0  NVIDIA GeForce RTX 5080 [SM120]  187.9 T MAC/s (avg 185.2)"));
        CHECK(has(text, "275.0 W") && has(text, "65 C") && has(text, "accepted 7") && has(text, "unknown 1") &&
              has(text, "local stale 4") && has(text, "total shares: submitted 20"));
        CHECK(has(text, "donation: not_active"));
        JsonValue wrong;
        parseJsonStrict("{\"schema\":\"other\"}", &wrong, &err);
        CHECK(!renderStatusText(wrong, &text, &err) && has(err, "canaryminer-stats-v1"));
    }

    // ---- request router (pure)
    auto stats = [&] { return std::string("{\"ok\":true}"); };
    const std::string ver = "{\"v\":1}";
    auto route = [&](const std::string &head) { return handleStatsRequest(head, stats, ver); };
    CHECK(route("GET /api/v1/stats HTTP/1.1\r\nHost: 127.0.0.1:8085\r\n\r\n").code == 200);
    CHECK(route("GET /api/v1/stats HTTP/1.1\r\nHost: 127.0.0.1:8085\r\n\r\n").body == "{\"ok\":true}");
    CHECK(route("GET /api/v1/version HTTP/1.1\r\n\r\n").body == ver);
    CHECK(route("GET /api/v1/stats?x=1 HTTP/1.0\r\n\r\n").code == 200);
    CHECK(route("GET /api/v1/stats HTTP/1.1\r\nHost: localhost:9\r\n\r\n").code == 200);
    CHECK(route("GET /api/v1/stats HTTP/1.1\r\nhost: [::1]:9\r\n\r\n").code == 200);
    CHECK(route("GET /api/v1/stats HTTP/1.1\r\nHost: evil.example.com\r\n\r\n").code == 403);           // DNS rebinding
    CHECK(route("GET /api/v1/stats HTTP/1.1\r\nHost: 127.0.0.1.evil.com\r\n\r\n").code == 403);
    CHECK(route("GET /nope HTTP/1.1\r\n\r\n").code == 404 && route("GET / HTTP/1.1\r\n\r\n").code == 404 && route("GET /api/v1/stats/ HTTP/1.1\r\n\r\n").code == 404);
    for (const char *m : {"POST", "PUT", "DELETE", "PATCH", "OPTIONS", "HEAD", "get"}) {
        const HttpReply r = route(std::string(m) + " /api/v1/stats HTTP/1.1\r\n\r\n");
        CHECK_MSG(m, r.code == 405 && r.allow == "GET");
    }
    CHECK(route("garbage\r\n\r\n").code == 400 && route("GET /x\r\n\r\n").code == 400 && route("GET /api/v1/stats FTP/1.1\r\n\r\n").code == 400);
    CHECK(route(std::string("GET /api/v1/stats HTTP/1.1\r\nX: ") + std::string(9000, 'a') + "\r\n\r\n").code == 431);
    {
        const std::string wire = httpWire(route("GET /api/v1/version HTTP/1.1\r\n\r\n"));
        CHECK(has(wire, "HTTP/1.1 200 OK") && has(wire, "Content-Type: application/json") && has(wire, "Cache-Control: no-store") &&
              has(wire, "Content-Length: 7") && wire.substr(wire.size() - 7) == ver);
        CHECK(has(httpWire(route("POST / HTTP/1.1\r\n\r\n")), "Allow: GET"));
    }

    // ---- loopback listener
    {
        std::string e2;
        StatsServer refused;
        CHECK(!refused.start("0.0.0.0", 0, stats, ver, &e2) && has(e2, "loopback only"));
        CHECK(!refused.start("192.168.1.2", 0, stats, ver, &e2));
        CHECK(!refused.start("", 0, stats, ver, &e2));
        platformInit();
        StatsServer srv;
        StatsPublisher pub;
        pub.update([&](StatsSnapshot *s) { *s = snap; });
        CHECK(srv.start("127.0.0.1", 0, [&pub] { return statsToJson(pub.snapshot()); }, ver, &e2));
        const int port = srv.port();
        CHECK(port > 0);
        int code = 0;
        std::string body;
        CHECK(httpGetLocal("127.0.0.1", port, "/api/v1/stats", &code, &body, &e2) && code == 200 && parseJsonStrict(body, &root, &e2) &&
              root.find("schema")->text == "canaryminer-stats-v1");
        CHECK(httpGetLocal("127.0.0.1", port, "/api/v1/version", &code, &body, &e2) && code == 200 && body == ver);
        CHECK(httpGetLocal("127.0.0.1", port, "/other", &code, &body, &e2) && code == 404);
        // a publisher update is visible on the next request
        pub.update([](StatsSnapshot *s) { s->state = "draining"; });
        CHECK(httpGetLocal("127.0.0.1", port, "/api/v1/stats", &code, &body, &e2) && has(body, "\"state\":\"draining\""));
        // status command path end to end against the live listener
        ProductConfig cfg;
        cfg.api.port = port;
        CHECK(commandStatus(cfg, true) == kExitOk && commandStatus(cfg, false) == kExitOk);
        // many sequential connections do not leak or wedge the single-threaded server
        for (int i = 0; i < 50; ++i) CHECK(httpGetLocal("127.0.0.1", port, "/api/v1/version", &code, &body, &e2) && code == 200);
        // a stalled client (connects, sends nothing) cannot block the next request for long
        {
            socket_t stall = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons(static_cast<uint16_t>(port));
            const bool connected = ::connect(stall, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
            CHECK(connected);
            CHECK(httpGetLocal("127.0.0.1", port, "/api/v1/version", &code, &body, &e2, 6) && code == 200);  // served after the 2 s I/O timeout
            OM_CLOSESOCKET(stall);
        }
        srv.stop();
        CHECK(!httpGetLocal("127.0.0.1", port, "/api/v1/version", &code, &body, &e2) && has(e2, "no miner is listening"));
        // status against nothing: a clear error and the documented exit code
        CHECK(commandStatus(cfg, false) == kExitStatusDown);
        ProductConfig off;
        off.api.enabled = false;
        CHECK(commandStatus(off, false) == kExitStatusDown);
        platformShutdown();
    }

    // ---- StatsObserver: MinerStats -> snapshot
    {
        StatsPublisher pub;
        StatsDevice base = snap.devices[0];
        StatsObserver obs(&pub, base, "pool.example:1", "");
        MinerStats ms;
        ms.macsPerUnit = 2048.0 * 16 * 16;
        ms.hashrate = 3.5e6 / 1e6 * 0 + 100.0;  // millions of work units per second
        ms.hashrateAvg = 90.0;
        ms.attemptsPerSecond = 12.0;
        ms.submitted = 4; ms.acknowledged = 4; ms.accepted = 3; ms.poolRejected = 1; ms.stale = 0; ms.unknown = 0; ms.pending = 0;
        ms.uptimeSeconds = 42;
        GpuTelemetry g;
        g.valid = true;
        g.powerMilliwatts = 200500;
        g.powerValid = true;
        g.temperatureC = 61;
        obs.onReport(ms, g);
        const StatsSnapshot s = pub.snapshot();
        CHECK(s.state == "running" && s.poolState == "mining" && s.poolEndpoint == "pool.example:1" && s.devices.size() == 1);
        CHECK(s.devices[0].outcomes.accepted == 3 && s.devices[0].outcomes.rejected == 1 && s.uptimeSeconds == 42);
        CHECK(s.devices[0].powerWatts > 200.4 && s.devices[0].powerWatts < 200.6 && s.devices[0].temperatureC == 61);
        const double expectT = 100.0 * 1e6 * ms.macsPerUnit / 1e12;
        CHECK(s.devices[0].rateTmacs > expectT * 0.999 && s.devices[0].rateTmacs < expectT * 1.001);
        GpuTelemetry none;
        obs.onReport(ms, none);
        CHECK(pub.snapshot().devices[0].powerWatts < 0 && pub.snapshot().devices[0].temperatureC < 0);
    }
    return finishTests("test_product_stats");
}
