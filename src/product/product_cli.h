// Product command line: subcommands and the options that override the config file.
//
//   canary-miner [run] [options]      mine (default)
//   canary-miner list-devices         identity of every CUDA device and whether it is supported
//   canary-miner selftest             offline kernel + proof self-test on each selected device (no pool, no network)
//   canary-miner status [--json]      read the local stats endpoint of a running miner
//   canary-miner version              version, build architecture, fee-destination hash
//
//   --config FILE     config-v1 JSON file
//   --pool SPEC       repeatable and ordered: kryptex | herominers | HOST:PORT (replaces the config's pool list)
//   --wallet ADDR     payout address (alias --user)
//   --worker NAME
//   --devices LIST    comma separated: all | index | GPU-uuid | PCI bus id   (alias --device N)
//   --api-bind ADDR   loopback only        --api-port N  (0 disables the endpoint)
//   --log-dir DIR     rotating redacted log files
//   --json            (status) print the raw JSON
//
// Everything this parser does not recognise is returned in `rest` untouched for the legacy option loop in miner.cu
// (policy flags such as --pearl-network, benchmark flags, ...), so existing harness command lines keep working.
// CLI values override the config file; nothing here reads a file or the network.

#pragma once

#include <string>
#include <vector>

#include "config.h"

namespace om {

enum class Command { Run, ListDevices, Selftest, Status, Version };

/** Process exit codes of the product commands (the research option paths keep their historical 1). */
enum ExitCode : int {
    kExitOk = 0,
    kExitRuntime = 1,         // mining stopped on an error
    kExitUsage = 2,           // bad command line
    kExitConfig = 3,          // config file unreadable, invalid or incomplete
    kExitDeviceRefused = 4,   // no usable supported device, or a requested device is refused
    kExitSelftestFailed = 5,  // a selftest step failed
    kExitStatusDown = 6,      // `status` could not reach a running miner
    kExitShutdownForced = 7,  // the drain deadline passed; shares in flight were recorded as unknown
};

struct CliOverrides {
    std::string configPath;
    std::vector<std::string> pools;
    bool walletSet = false;
    std::string wallet;
    bool workerSet = false;
    std::string worker;
    bool devicesSet = false;
    std::vector<std::string> devices;
    bool apiBindSet = false;
    std::string apiBind;
    bool apiPortSet = false;
    int apiPort = 0;
    bool logDirSet = false;
    std::string logDir;
    bool json = false;
    bool noDonation = false;  // --no-donation: switch the developer donation off for this run
};

struct CliParse {
    Command command = Command::Run;
    bool commandExplicit = false;
    CliOverrides ov;
    std::vector<std::string> rest;  // untouched legacy arguments (argv[0] excluded)
};

inline std::vector<std::string> splitCommaList(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

/** Returns false with *error set on a malformed product option. Does not validate values beyond their shape. */
inline bool parseProductCli(int argc, const char *const *argv, CliParse *out, std::string *error) {
    CliParse r;
    int i = 1;
    if (i < argc && argv[i][0] != '-') {
        const std::string c = argv[i];
        if (c == "run") r.command = Command::Run;
        else if (c == "list-devices") r.command = Command::ListDevices;
        else if (c == "selftest") r.command = Command::Selftest;
        else if (c == "status") r.command = Command::Status;
        else if (c == "version") r.command = Command::Version;
        else {
            if (error) *error = "unknown command '" + c + "' (run, list-devices, selftest, status, version)";
            return false;
        }
        r.commandExplicit = true;
        ++i;
    }
    auto need = [&](int &k, const char *opt, std::string *value) {
        if (k + 1 >= argc) {
            if (error) *error = std::string(opt) + " needs a value";
            return false;
        }
        *value = argv[++k];
        return true;
    };
    for (; i < argc; ++i) {
        const std::string a = argv[i];
        std::string v;
        if (a == "--config") {
            if (!need(i, "--config", &v)) return false;
            if (!r.ov.configPath.empty()) {
                if (error) *error = "--config given twice";
                return false;
            }
            r.ov.configPath = v;
        } else if (a == "--pool") {
            if (!need(i, "--pool", &v)) return false;
            if (v.empty()) {
                if (error) *error = "--pool needs a preset name or HOST:PORT; an empty value in a launcher script is the usual cause";
                return false;
            }
            r.ov.pools.push_back(v);
        } else if (a == "--wallet" || a == "--user") {
            if (!need(i, a.c_str(), &v)) return false;
            r.ov.walletSet = true;
            r.ov.wallet = v;
        } else if (a == "--worker") {
            if (!need(i, "--worker", &v)) return false;
            r.ov.workerSet = true;
            r.ov.worker = v;
        } else if (a == "--devices" || a == "--device") {
            if (!need(i, a.c_str(), &v)) return false;
            for (const auto &d : splitCommaList(v)) r.ov.devices.push_back(d);
            if (r.ov.devices.empty()) {
                if (error) *error = a + " needs at least one device";
                return false;
            }
            r.ov.devicesSet = true;
        } else if (a == "--api-bind") {
            if (!need(i, "--api-bind", &v)) return false;
            r.ov.apiBindSet = true;
            r.ov.apiBind = v;
        } else if (a == "--api-port") {
            if (!need(i, "--api-port", &v)) return false;
            if (v.empty() || v.size() > 5) {
                if (error) *error = "--api-port needs a port number (0 disables the endpoint)";
                return false;
            }
            int p = 0;
            for (char c : v) {
                if (c < '0' || c > '9') {
                    if (error) *error = "--api-port needs a port number (0 disables the endpoint)";
                    return false;
                }
                p = p * 10 + (c - '0');
            }
            if (p > 65535) {
                if (error) *error = "--api-port must be 0..65535";
                return false;
            }
            r.ov.apiPortSet = true;
            r.ov.apiPort = p;
        } else if (a == "--log-dir") {
            if (!need(i, "--log-dir", &v)) return false;
            r.ov.logDirSet = true;
            r.ov.logDir = v;
        } else if (a == "--json") {
            r.ov.json = true;
        } else if (a == "--no-donation") {
            r.ov.noDonation = true;
        } else if (a == "--list-devices" && !r.commandExplicit) {
            r.command = Command::ListDevices;
            r.commandExplicit = true;
        } else if (a == "--version" && !r.commandExplicit) {
            r.command = Command::Version;
            r.commandExplicit = true;
        } else {
            r.rest.push_back(a);
        }
    }
    *out = r;
    return true;
}

/** Layer the CLI over a config (file or defaults). Pool specs are parsed here so errors name the offending value. */
inline bool applyOverrides(const CliOverrides &ov, ProductConfig *cfg, std::string *error) {
    if (!ov.pools.empty()) {
        std::vector<PoolEntry> pools;
        for (const auto &spec : ov.pools) {
            PoolEntry e;
            if (!parsePoolSpec(spec, &e, error)) return false;
            pools.push_back(e);
        }
        cfg->pools = pools;
    }
    if (ov.walletSet) cfg->wallet = ov.wallet;
    if (ov.workerSet) cfg->worker = ov.worker;
    if (ov.devicesSet) cfg->devices = ov.devices;
    if (ov.apiBindSet) cfg->api.bind = ov.apiBind;
    if (ov.apiPortSet) {
        cfg->api.enabled = ov.apiPort != 0;
        if (ov.apiPort != 0) cfg->api.port = ov.apiPort;
    }
    if (ov.logDirSet) cfg->log.dir = ov.logDir;
    if (ov.noDonation) cfg->donation = false;  // the flag only ever turns it off; it wins over "donation": true
    return true;
}

}  // namespace om
