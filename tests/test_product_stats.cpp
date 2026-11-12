// stats-v1 JSON, the status renderer, the pure request router, and the loopback listener (127.0.0.1 only, ephemeral port).

#include <string>
#include <thread>

#include "../src/product/product_main.h"
#include "../src/product/stats.h"
#include "../src/product/stats_server.h"
#include "../src/product/status_view.h"
#include "../src/core/submit_outcome.h"
#include "../src/core/pearl_pool.h"
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

static std::string captureStatus(const ProductConfig &cfg) {
    std::fflush(stdout);
    FILE *file=std::tmpfile();CHECK(file!=nullptr);if(!file)return {};
#if defined(_WIN32)
    const int saved=_dup(_fileno(stdout));CHECK(saved>=0);
    CHECK(_dup2(_fileno(file),_fileno(stdout))==0);
#else
    const int saved=dup(fileno(stdout));CHECK(saved>=0);
    CHECK(dup2(fileno(file),fileno(stdout))>=0);
#endif
    const int code=commandStatus(cfg,false);std::fflush(stdout);
#if defined(_WIN32)
    CHECK(_dup2(saved,_fileno(stdout))==0);_close(saved);
#else
    CHECK(dup2(saved,fileno(stdout))>=0);close(saved);
#endif
    CHECK(code==kExitOk);std::rewind(file);std::string result;char buffer[512];
    for(std::size_t n;(n=std::fread(buffer,1,sizeof(buffer),file))!=0;)result.append(buffer,n);
    std::fclose(file);return result;
}

static void testPoolTransport() {
    const uint64_t began=10000;
    StatsSnapshot value=sample();value.poolTransport={{PoolTransportSecurity::VerifiedTls,PoolTransportError::None,8048,began},false};
    CHECK(has(statsToJson(value,began+4999),"\"state\":\"verified_tls\""));
    CHECK(!has(statsToJson(value,began+5000),"verified_tls"));
    CHECK(!has(statsToJson(value,began-1),"verified_tls"));
    value.state="draining";CHECK(!has(statsToJson(value,began),"verified_tls"));value.state="running";
    JsonValue wire;std::string error;StatsSnapshot decoded;
    CHECK(parseJsonStrict(statsToJson(value,began+1000),&wire,&error)&&statsFromJson(wire,&decoded,&error,20000));
    CHECK(decoded.poolTransport.observation.observedMonoMs==19000&&decoded.poolTransport.observation.effectivePort==8048);
    CHECK(currentPoolTransport(decoded.poolTransport,23999).observation.security==PoolTransportSecurity::VerifiedTls);
    CHECK(currentPoolTransport(decoded.poolTransport,24000).observation.security==PoolTransportSecurity::Unknown);
    // Parsing older or malformed optional observations must retain unrelated counters, never invent encryption.
    for(const auto &transport:std::vector<std::string>{"",",\"transport\":null",",\"transport\":{}",
        ",\"transport\":{\"state\":\"verified_tls\",\"error\":\"none\",\"port\":8048,\"age_ms\":null}",
        ",\"transport\":{\"state\":\"verified_tls\",\"error\":\"none\",\"port\":8048,\"age_ms\":5000}",
        ",\"transport\":{\"state\":\"verified_tls\",\"error\":\"none\",\"port\":0,\"age_ms\":0}",
        ",\"transport\":{\"state\":\"verified_tls\",\"error\":\"verification\",\"port\":8048,\"age_ms\":0}",
        ",\"transport\":{\"state\":\"verified_tls\",\"error\":\"PRIVATE-secret\",\"port\":8048,\"age_ms\":0}",
        ",\"transport\":{\"state\":\"PRIVATE-secret\",\"error\":\"none\",\"port\":8048,\"age_ms\":0}",
        ",\"transport\":{\"state\":\"plaintext\",\"error\":\"none\",\"port\":65536,\"age_ms\":-1}"}){
        CHECK(parseJsonStrict("{\"schema\":\"canaryminer-stats-v1\",\"state\":\"running\",\"uptime_seconds\":17,\"pool\":{\"state\":\"mining\""+transport+"}}",&wire,&error));
        CHECK(statsFromJson(wire,&decoded,&error,began)&&decoded.uptimeSeconds==17&&decoded.poolTransport.observation.security==PoolTransportSecurity::Unknown&&!decoded.poolTransport.mixed);
        CHECK(!has(statsToJson(decoded,began),"PRIVATE-secret"));
    }
    value.poolTransport={{PoolTransportSecurity::Unknown,PoolTransportError::None,0,began},true};
    CHECK(parseJsonStrict(statsToJson(value,began+100),&wire,&error)&&statsFromJson(wire,&decoded,&error,began+200));
    CHECK(decoded.poolTransport.mixed&&has(statsToJson(decoded,began+300),"\"state\":\"mixed\""));
    StatsPublisher publisher;StatsObserver observer(&publisher,{},"pool-fixture.test:8048","");
    const auto now=poolTransportNowMs();observer.onPoolTransportObservation({PoolTransportSecurity::VerifiedTls,PoolTransportError::None,8048,now});
    CHECK(publisher.snapshot(now+4999).poolTransport.observation.security==PoolTransportSecurity::VerifiedTls);
    CHECK(publisher.snapshot(now+5000).poolTransport.observation.security==PoolTransportSecurity::Unknown);
    observer.foldRun();CHECK(publisher.snapshot().poolTransport.observation.security==PoolTransportSecurity::Disconnected);
    class Refused final:public PearlPoolTransport {
        bool attempted=false;
    public:
        unsigned writes=0;
        PearlPoolConnectStatus open(const std::string &,int)override{attempted=true;return PearlPoolConnectStatus::ConnectFailure;}
        bool isOpen()const override{return false;}void close()override{}
        PearlPoolWaitStatus waitReadable(int)override{return PearlPoolWaitStatus::Error;}
        PearlPoolWaitStatus waitWritable(int)override{return PearlPoolWaitStatus::Error;}
        PearlPoolTransfer receive(char *,std::size_t)override{return {};}
        PearlPoolTransfer send(const char *,std::size_t)override{++writes;return {};}
        bool setNonblocking(bool)override{return false;}
        PoolTransportObservation observation()const override{return {PoolTransportSecurity::Disconnected,attempted?PoolTransportError::Verification:PoolTransportError::None,0,poolTransportNowMs()};}
    } refused;
    {
        PearlPoolSource source("pool-fixture.test",8048,"PRIVATE-wallet","PRIVATE-worker",{PearlNetwork::Mainnet,PearlTargetSemantics::BaseBeforePenalty},"",&refused);
        source.setTransportObserver(&observer);Job job;CHECK(!source.fetch(&job)&&refused.writes==0);
        const auto failed=publisher.snapshot();CHECK(failed.poolTransport.observation.security==PoolTransportSecurity::Disconnected&&failed.poolTransport.observation.error==PoolTransportError::Verification);
        const auto document=statsToJson(failed);CHECK(has(document,"verification")&&!has(document,"PRIVATE-wallet")&&!has(document,"PRIVATE-worker"));
    }
    // Exercise commandStatus itself: a successful HTTP response must not renew an old producer observation.
    CHECK(platformInit());StatsServer server;std::atomic<bool> delayed{true};
    CHECK(server.start("127.0.0.1",0,[&]{auto s=sample();const auto at=poolTransportNowMs();
        s.poolTransport={{PoolTransportSecurity::VerifiedTls,PoolTransportError::None,8048,at-(delayed?4800:0)},false};
        auto text=statsToJson(s,at);if(delayed)std::this_thread::sleep_for(std::chrono::milliseconds(300));return text;},"{}",&error));
    ProductConfig cfg;cfg.api.port=server.port();const auto aged=captureStatus(cfg);
    CHECK(has(aged,"First user pool transport: Unknown")&&!has(aged,"Verified TLS"));
    delayed=false;const auto fresh=captureStatus(cfg);CHECK(has(fresh,"Verified TLS (port 8048)"));
    server.stop();platformShutdown();
}

int main(int argc, char **argv) {
    // A fresh process intentionally has no external platformInit: Python owns the listening socket.
    if(argc==3&&std::string(argv[1])=="--status-test"){
        char *end=nullptr;const auto port=std::strtoul(argv[2],&end,10);
        if(!end||*end||port==0||port>65535)return 2;
        ProductConfig cfg;cfg.api.port=static_cast<int>(port);return commandStatus(cfg,false);
    }
    testPoolTransport();
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
