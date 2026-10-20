// config-v1 parsing, strict JSON, CLI subcommands/overrides, effective-config redaction.
//   argv[1] (optional): path of config/config.example.json, which must parse and validate for a run.

#include <string>
#include <vector>

#include "../src/product/config.h"
#include "../src/product/product_cli.h"
#include "../src/product/product_main.h"
#include "product_test_util.h"

using namespace om;

static bool parses(const std::string &text, ProductConfig *cfg = nullptr, std::string *err = nullptr) {
    ProductConfig tmp;
    std::string e;
    const bool ok = parseConfig(text, cfg ? cfg : &tmp, err ? err : &e);
    return ok;
}

static std::string errorOf(const std::string &text) {
    ProductConfig c;
    std::string e;
    return parseConfig(text, &c, &e) ? std::string() : e;
}

static bool contains(const std::string &hay, const char *needle) { return hay.find(needle) != std::string::npos; }

static CliParse parseArgs(std::vector<std::string> args, std::string *err = nullptr) {
    std::vector<const char *> argv{"canary-miner"};
    for (const auto &a : args) argv.push_back(a.c_str());
    CliParse out;
    std::string e;
    const bool ok = parseProductCli(static_cast<int>(argv.size()), argv.data(), &out, &e);
    if (err) *err = ok ? std::string() : e;
    if (!ok) out.command = static_cast<Command>(99);
    return out;
}

int main(int argc, char **argv) {
    // ---------------------------------------------------------------- strict JSON
    JsonValue v;
    std::string e;
    CHECK(parseJsonStrict("{\"a\":[1,2,{\"b\":null}],\"c\":\"x\\u00e9\\n\",\"d\":true}", &v, &e));
    CHECK(v.find("c") && v.find("c")->text == std::string("x\xc3\xa9\n"));
    CHECK(!parseJsonStrict("{\"a\":1,\"a\":2}", &v, &e) && contains(e, "duplicate key 'a'"));
    CHECK(!parseJsonStrict("{\"a\":1,}", &v, &e));
    CHECK(!parseJsonStrict("[1,2,]", &v, &e));
    CHECK(!parseJsonStrict("{\"a\":1} extra", &v, &e) && contains(e, "trailing"));
    CHECK(!parseJsonStrict("{'a':1}", &v, &e));
    CHECK(!parseJsonStrict("{\"a\":01}", &v, &e));
    CHECK(!parseJsonStrict("{\"a\":NaN}", &v, &e));
    CHECK(!parseJsonStrict("{\"a\":\"\\ud800\"}", &v, &e));
    CHECK(!parseJsonStrict("{\"a\":\"tab\there\"}", &v, &e));
    CHECK(!parseJsonStrict("// c\n{}", &v, &e));
    CHECK(parseJsonStrict("\xEF\xBB\xBF{\"a\":1}", &v, &e));  // BOM tolerated
    {
        std::string deep;
        for (int i = 0; i < 40; ++i) deep += "[";
        for (int i = 0; i < 40; ++i) deep += "]";
        CHECK(!parseJsonStrict(deep, &v, &e) && contains(e, "deep"));
    }
    uint64_t u = 0;
    JsonValue n;
    parseJsonStrict("18446744073709551615", &n, &e);
    CHECK(n.asUint64(&u) && u == UINT64_MAX);
    parseJsonStrict("18446744073709551616", &n, &e);
    CHECK(!n.asUint64(&u));
    parseJsonStrict("1.5", &n, &e);
    CHECK(!n.asUint64(&u));
    CHECK(jsonQuote("a\"b\\c\n\x01") == "\"a\\\"b\\\\c\\n\\u0001\"");

    // ---------------------------------------------------------------- config-v1
    ProductConfig c;
    CHECK(parses("{\"schema_version\":1}", &c));
    CHECK(c.worker == "canary" && c.api.port == 8085 && c.api.enabled && c.api.bind == "127.0.0.1" && c.pools.empty() && c.drainSeconds == 10 &&
          c.stateDir == "state" && c.log.dir.empty() && c.log.maxFiles == 5);
    CHECK(contains(errorOf("{}"), "no schema_version"));
    CHECK(contains(errorOf("{\"schema_version\":2}"), "schema_version 2 is not supported"));
    CHECK(contains(errorOf("{\"schema_version\":2}"), "no automatic migration"));
    CHECK(contains(errorOf("{\"schema_version\":0}"), "not supported"));
    CHECK(contains(errorOf("{\"schema_version\":\"1\"}"), "whole number"));
    CHECK(contains(errorOf("{\"schema_version\":1.0}"), "whole number"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"wallett\":\"x\"}"), "unknown key 'wallett'"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"api\":{\"prot\":1}}"), "unknown key 'prot' in api"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"log\":{\"size\":1}}"), "unknown key 'size' in log"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"shutdown\":{\"x\":1}}"), "unknown key 'x' in shutdown"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"tuning\":{\"gpu_fan\":80}}"), "must be a string"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"tuning\":{\"gpu_fann\":\"80\"}}"), "unknown key 'gpu_fann' in tuning"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"pools\":[{\"url\":\"kryptex\",\"fee\":1}]}"), "unknown key 'fee' in a pool entry"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"schema_version\":1}"), "duplicate key"));
    CHECK(contains(errorOf("[1]"), "must be a JSON object"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"worker\":5}"), "must be a string"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"pools\":[]}"), "non-empty"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"pools\":[\"nonsense\"]}"), "neither a preset"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"pools\":[\"host:0\"]}"), "neither a preset"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"pools\":[\"-h:1\"]}"), "neither a preset"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"pools\":[{\"url\":\"kryptex\",\"network\":\"testnet\"}]}"), "mainnet only"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"pools\":[{\"url\":\"kryptex\",\"target_semantics\":\"x\"}]}"), "target_semantics"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"api\":{\"port\":70000}}"), "between 1 and 65535"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"api\":{\"enabled\":\"yes\"}}"), "true or false"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"log\":{\"max_files\":0}}"), "between 1 and 100"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"log\":{\"max_file_bytes\":10}}"), "between 4096"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"shutdown\":{\"drain_seconds\":0}}"), "between 1 and 600"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"devices\":[-1]}"), "whole number"));
    CHECK(contains(errorOf("{\"schema_version\":1,\"devices\":[99]}"), "between 0 and 15"));

    CHECK(parses("{\"schema_version\":1,\"wallet\":\"W\",\"worker\":\"rig_1\",\"pools\":[\"kryptex\",\"herominers\",{\"url\":\"pool.example.net:3333\","
                 "\"target_semantics\":\"final-adjusted-bound\"}],\"devices\":[0,\"GPU-abc\"],\"api\":{\"port\":9000},\"log\":{\"dir\":\"l\","
                  "\"max_files\":3,\"max_file_bytes\":8192,\"redact_worker\":true},\"state_dir\":\"s\",\"shutdown\":{\"drain_seconds\":30},"
                  "\"tuning\":{\"gpu_cclock\":\"2550:2600,2400\",\"gpu_mclock\":\"7001\",\"gpu_coffset\":\"+599\","
                  "\"gpu_moffset\":\"0\",\"gpu_plimit\":\"175\",\"gpu_fan\":\"80\",\"oc_delay\":5,\"oc_reset_only\":false}}",
                  &c));
    CHECK(c.pools.size() == 3 && c.pools[0].preset == "kryptex" && c.pools[0].endpoint() == "prl-us.kryptex.network:7048" &&
          c.pools[1].endpoint() == "us2.pearl.herominers.com:1200" && c.pools[2].endpoint() == "pool.example.net:3333" &&
          c.pools[2].targetSemantics == "final-adjusted-bound" && c.pools[0].targetSemantics == "base-before-penalty" && c.pools[0].network == "mainnet");
    CHECK(c.devices.size() == 2 && c.devices[0] == "0" && c.devices[1] == "GPU-abc");
    CHECK(c.pools[0].policyKnown && c.pools[2].policyKnown);
    CHECK(c.api.port == 9000 && c.log.dir == "l" && c.log.maxFiles == 3 && c.log.maxFileBytes == 8192 && c.log.redactWorker && c.stateDir == "s" &&
          c.drainSeconds == 30);
    CHECK(c.tuning.gpuCclockSet && c.tuning.gpuCclock == "2550:2600,2400" && c.tuning.gpuMclockSet && c.tuning.gpuMclock == "7001");
    CHECK(c.tuning.gpuCoffsetSet && c.tuning.gpuCoffset == "+599" && c.tuning.gpuMoffsetSet && c.tuning.gpuMoffset == "0");
    CHECK(c.tuning.gpuPlimitSet && c.tuning.gpuPlimit == "175" && c.tuning.gpuFanSet && c.tuning.gpuFan == "80");
    CHECK(c.tuning.ocDelaySet && c.tuning.ocDelaySeconds == 5 && !c.tuning.ocResetOnly);
    CHECK(parses("{\"schema_version\":1,\"tuning\":{\"oc_reset_only\":true}}", &c) && c.tuning.ocResetOnly);
    CHECK(contains(errorOf("{\"schema_version\":1,\"tuning\":{\"oc_delay\":4294967296}}"), "between 0 and 4294967295"));

    // cross-field validation
    ProductConfig d;
    CHECK(validateConfig(d, false, &e));
    CHECK(!validateConfig(d, true, &e) && contains(e, "no wallet"));
    d.wallet = "W";
    CHECK(!validateConfig(d, true, &e) && contains(e, "no pool"));
    d.pools.resize(1);
    CHECK(validateConfig(d, true, &e));
    // F2Pool logs in with an account name: a payout address alone is refused with a hint
    d.pools[0].preset = "f2pool";
    CHECK(!validateConfig(d, true, &e) && contains(e, "F2Pool needs your F2Pool account name"));
    d.pools[0].account = "myaccount";
    CHECK(validateConfig(d, true, &e));
    d.pools[0] = PoolEntry{};
    d.api.bind = "0.0.0.0";
    CHECK(!validateConfig(d, false, &e) && contains(e, "loopback"));
    d.api.bind = "::1";
    CHECK(validateConfig(d, false, &e));
    d.worker = "bad worker";
    CHECK(!validateConfig(d, false, &e));
    d.worker = std::string(65, 'a');
    CHECK(!validateConfig(d, false, &e));

    // example file: parses, validates for a run, and carries no real address
    if (argc > 1) {
        ProductConfig ex;
        std::string err;
        CHECK_MSG("config.example.json loads", loadProductConfig(argv[1], &ex, &err));
        CHECK_MSG("config.example.json validates for a run", validateConfig(ex, true, &err));
        CHECK_MSG("config.example.json has a placeholder wallet", ex.wallet.find("REPLACE") != std::string::npos);
    }
    {
        ProductConfig ex;
        std::string err;
        CHECK(!loadProductConfig("definitely-missing-config.json", &ex, &err) && contains(err, "cannot read config file"));
    }

    // ---------------------------------------------------------------- CLI
    CliParse p = parseArgs({});
    CHECK(p.command == Command::Run && !p.commandExplicit && p.rest.empty());
    p = parseArgs({"run", "--pool", "kryptex", "--pool", "host:1234", "--wallet", "WALLETX", "--worker", "w1", "--devices", "0,GPU-x", "--api-port", "0", "--log-dir", "L"});
    CHECK(p.command == Command::Run && p.commandExplicit);
    CHECK(p.ov.pools.size() == 2 && p.ov.pools[0] == "kryptex" && p.ov.pools[1] == "host:1234");
    CHECK(p.ov.walletSet && p.ov.wallet == "WALLETX" && p.ov.workerSet && p.ov.worker == "w1");
    CHECK(p.ov.devicesSet && p.ov.devices.size() == 2 && p.ov.apiPortSet && p.ov.apiPort == 0 && p.ov.logDirSet && p.ov.logDir == "L");
    CHECK(parseArgs({"list-devices"}).command == Command::ListDevices);
    CHECK(parseArgs({"--list-devices"}).command == Command::ListDevices);
    CHECK(parseArgs({"selftest", "--devices", "1"}).command == Command::Selftest);
    CHECK(parseArgs({"status", "--json"}).command == Command::Status && parseArgs({"status", "--json"}).ov.json);
    CHECK(parseArgs({"version"}).command == Command::Version && parseArgs({"--version"}).command == Command::Version);
    CHECK(parseArgs({"--device", "1"}).ov.devices.size() == 1);
    {
        std::string err;
        parseArgs({"bogus"}, &err);
        CHECK(contains(err, "unknown command 'bogus'"));
        parseArgs({"--pool"}, &err);
        CHECK(contains(err, "--pool needs a value"));
        parseArgs({"--pool", ""}, &err);
        CHECK(contains(err, "empty value"));
        parseArgs({"--api-port", "70000"}, &err);
        CHECK(contains(err, "0..65535"));
        parseArgs({"--api-port", "x"}, &err);
        CHECK(contains(err, "port number"));
        parseArgs({"--config", "a", "--config", "b"}, &err);
        CHECK(contains(err, "twice"));
        parseArgs({"--devices", ","}, &err);
        CHECK(contains(err, "at least one device"));
    }
    // unrecognised (legacy/research) options pass through untouched and in order
    p = parseArgs({"--pearl-network", "mainnet", "--pool", "kryptex", "--batch", "256", "--bench"});
    CHECK(p.rest.size() == 5 && p.rest[0] == "--pearl-network" && p.rest[1] == "mainnet" && p.rest[2] == "--batch" && p.rest[4] == "--bench");

    // CLI overrides the file; pools are replaced as an ordered list
    {
        ProductConfig base;
        parseConfig("{\"schema_version\":1,\"wallet\":\"FILEWALLET\",\"worker\":\"fileworker\",\"pools\":[\"herominers\"],\"api\":{\"port\":9000},"
                    "\"log\":{\"dir\":\"filelogs\"},\"devices\":[\"0\"]}", &base, &e);
        CliParse o = parseArgs({"--pool", "kryptex", "--pool", "a.b:5", "--worker", "clirig", "--api-port", "9100", "--devices", "all",
                                "--gpu-fan", "80", "--oc-delay", "10", "--oc-reset-only"});
        CHECK(applyOverrides(o.ov, &base, &e));
        CHECK(base.pools[0].policyKnown && !base.pools[1].policyKnown);  // preset carries its policy; a bare HOST:PORT does not
        CHECK(base.pools.size() == 2 && base.pools[0].preset == "kryptex" && base.pools[1].endpoint() == "a.b:5");
        CHECK(base.wallet == "FILEWALLET" && base.worker == "clirig" && base.api.port == 9100 && base.log.dir == "filelogs");
        CHECK(base.devices.size() == 1 && base.devices[0] == "all");
        CHECK(base.tuning.gpuFanSet && base.tuning.gpuFan == "80" && base.tuning.ocDelaySeconds == 10 && base.tuning.ocResetOnly);
        CliParse off = parseArgs({"--api-port", "0"});
        CHECK(applyOverrides(off.ov, &base, &e) && !base.api.enabled);
        CliParse bad = parseArgs({"--pool", "no-port"});
        CHECK(!applyOverrides(bad.ov, &base, &e) && contains(e, "neither a preset"));
    }

    // effective configuration never shows the wallet or (optionally) the worker
    {
        ProductConfig s;
        s.wallet = "prl1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqSECRETTAILXYZ9";
        s.worker = "my-rig";
        s.pools.resize(2);
        s.pools[0].host = "a.example";
        s.pools[0].port = 1;
        s.pools[1].host = "b.example";
        s.pools[1].port = 2;
        const std::string t = effectiveConfigText(s, "unit test");
        CHECK(!contains(t, "SECRETTAIL") && !contains(t, "prl1qqqq"));
        CHECK(contains(t, "prl1...XYZ9") && contains(t, "my-rig") && contains(t, "a.example:1") && contains(t, "failover is not implemented"));
        s.log.redactWorker = true;
        CHECK(!contains(effectiveConfigText(s, "x"), "my-rig"));
    }

    // exit codes are stable (documented in DESIGN-1B1.md)
    CHECK(kExitOk == 0 && kExitRuntime == 1 && kExitUsage == 2 && kExitConfig == 3 && kExitDeviceRefused == 4 && kExitSelftestFailed == 5 &&
          kExitStatusDown == 6 && kExitShutdownForced == 7);
    return finishTests("test_product_config");
}
