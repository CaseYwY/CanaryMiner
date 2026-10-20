// Standard CLI forms (H-031): -o/-u/-p aliases, stratum+tcp scheme, wallet split, F2Pool account, --api-enable,
// ignored foreign flags, overclock refusal, help text. Nothing here reads a file or the network.

#include <string>
#include <vector>

#include "../src/product/config.h"
#include "../src/product/fee.h"
#include "../src/product/product_cli.h"
#include "product_test_util.h"

using namespace om;

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

/** Parse the arguments, layer them over the defaults, and report whether both steps succeeded. */
static bool layered(std::vector<std::string> args, ProductConfig *cfg, std::string *err) {
    CliParse p = parseArgs(std::move(args), err);
    if (p.command == static_cast<Command>(99)) return false;
    *cfg = ProductConfig{};
    return applyOverrides(p.ov, cfg, err);
}

int main() {
    // ---- no subcommand is needed; the run command is the default
    CHECK(parseArgs({"--pool", "f2pool", "-u", "prl1abc.rig1"}).command == Command::Run);
    CHECK(parseArgs({"run", "-o", "f2pool"}).command == Command::Run);

    // ---- pool forms: preset, HOST:PORT, stratum+tcp://, known hosts map to their preset, TLS refused
    {
        PoolEntry e;
        std::string err;
        CHECK(parsePoolSpec("stratum+tcp://pearl.f2pool.com:5500", &e, &err) && e.preset == "f2pool" && e.host == "pearl.f2pool.com" &&
              e.port == 5500 && e.policyKnown);
        CHECK(parsePoolSpec("prl-us.kryptex.network:7048", &e, &err) && e.preset == "kryptex" && e.policyKnown);
        CHECK(parsePoolSpec("stratum+tcp://us2.pearl.herominers.com:1200", &e, &err) && e.preset == "herominers" && e.port == 1200);
        CHECK(parsePoolSpec("example.net:3333", &e, &err) && e.preset.empty() && !e.policyKnown && e.host == "example.net");
        CHECK(!parsePoolSpec("stratum+ssl://pearl.f2pool.com:5500", &e, &err) && contains(err, "TLS not supported yet"));        CHECK(!parsePoolSpec("stratum+tcp://", &e, &err) && contains(err, "neither a preset"));
        // a lookalike domain is not a known host
        CHECK(parsePoolSpec("evilf2pool.com:5500", &e, &err) && e.preset.empty());
    }

    // ---- aliases: -o, -u, -p, --gpu-id, --user
    {
        CliParse p = parseArgs({"-o", "f2pool", "-u", "prl1abc", "-p", "secret", "--gpu-id", "0,1", "--user", "prl1xyz"});
        CHECK(p.ov.pools.size() == 1 && p.ov.wallet == "prl1xyz" && p.ov.devices.size() == 2);
        // -p is handed to the legacy loop as --pass, unchanged
        bool passSeen = false;
        for (size_t i = 0; i + 1 < p.rest.size(); ++i) passSeen = passSeen || (p.rest[i] == "--pass" && p.rest[i + 1] == "secret");
        CHECK(passSeen);
    }

    // ---- wallet split: ADDRESS.WORKER, worker from the split, --worker wins
    {
        ProductConfig c;
        std::string e;
        CHECK(layered({"--pool", "example.net:1", "-u", "prl1abc.rig1"}, &c, &e) && c.wallet == "prl1abc" && c.worker == "rig1");
        CHECK(layered({"--pool", "example.net:1", "-u", "prl1abc.rig1", "--worker", "override"}, &c, &e) && c.worker == "override");
        CHECK(layered({"--pool", "example.net:1", "-u", "prl1abc"}, &c, &e) && c.wallet == "prl1abc" && c.worker == "canary");
        CHECK(!layered({"--pool", "example.net:1", "-u", "prl1abc.a.b"}, &c, &e) && contains(e, "one dot"));
        CHECK(!layered({"--pool", "example.net:1", "-u", ".rig1"}, &c, &e) && contains(e, "ADDRESS.WORKER"));
        // a non-prl1 value on a non-F2Pool pool passes through; miner.cu's address check refuses it later
        CHECK(layered({"--pool", "example.net:1", "-u", "notanaddress"}, &c, &e) && c.wallet == "notanaddress" && c.pools[0].account.empty());
        CliParse emptyWallet = parseArgs({"-u", ""}, &e);
        CHECK(emptyWallet.command == static_cast<Command>(99) && contains(e, "needs your PRL address"));
    }

    // ---- F2Pool account form: ACCOUNT.WORKER on F2Pool is the login, the wallet stays unset
    {
        ProductConfig c;
        std::string e;
        CHECK(layered({"-o", "f2pool", "-u", "myaccount.rig2"}, &c, &e) && c.pools[0].account == "myaccount" && c.worker == "rig2" &&
              c.wallet.empty());
        CHECK(layered({"-o", "stratum+tcp://pearl.f2pool.com:5500", "-u", "myaccount"}, &c, &e) && c.pools[0].account == "myaccount");
        CHECK(!layered({"-o", "f2pool", "-u", "bad-name.rig"}, &c, &e) && contains(e, "may contain only letters"));
        // an account name on a non-F2Pool pool is not an F2Pool login: it is passed on as the wallet and refused downstream
        CHECK(layered({"-o", "kryptex", "-u", "myaccount.rig"}, &c, &e) && c.wallet == "myaccount" && c.pools[0].account.empty());
        // a prl1 address on F2Pool still works as the payout address
        CHECK(layered({"-o", "f2pool", "-u", "prl1abc.rig3"}, &c, &e) && c.wallet == "prl1abc" && c.pools[0].account.empty());
    }

    // ---- stats endpoint: --api-enable defaults to 21550; the config default stays 8085 without it
    {
        ProductConfig c;
        std::string e;
        CHECK(layered({"--pool", "example.net:1"}, &c, &e) && c.api.port == 8085);
        CHECK(layered({"--pool", "example.net:1", "--api-enable"}, &c, &e) && c.api.enabled && c.api.port == kApiEnablePort);
        CHECK(layered({"--pool", "example.net:1", "--api-enable", "--api-port", "9100"}, &c, &e) && c.api.port == 9100);
        CHECK(!layered({"--pool", "example.net:1", "--api-enable", "--api-port", "0"}, &c, &e) && contains(e, "conflicts with --api-port 0"));
        CHECK(kApiEnablePort == 21550);
    }

    // ---- devices and workers: the comma list
    {
        CliParse p = parseArgs({"--gpu-id", "0,GPU-x"});
        CHECK(p.ov.devices.size() == 2 && p.ov.devicesSet);
    }

    // ---- foreign flags are ignored/refused; Linux GPU tuning flags are consumed by the product parser
    {
        CliParse p = parseArgs({"--algorithm", "pearl", "--disable-cpu", "--pool", "f2pool"});
        CHECK(p.rest.empty() && p.ov.pools.size() == 1);
        CliParse ph = parseArgs({"--algorithm", "pearlhash"}, nullptr);
        CHECK(ph.rest.empty());
        std::string e;
        parseArgs({"--algorithm", "ethash"}, &e);
        CHECK(contains(e, "supports pearl"));
        CliParse tuning = parseArgs({"--gpu-id", "2,0", "--gpu-cclock", "2550:2600,2400", "--gpu-mclock=7001,6800", "--gpu-coffset", "+599,0",
                                     "--gpu-moffset", "0", "--gpu-plimit", "175", "--gpu-fan", "80,75", "--oc-delay", "5"});
        CHECK(tuning.ov.tuning.gpuCclockSet && tuning.ov.tuning.gpuCclock == "2550:2600,2400");
        CHECK(tuning.ov.tuning.gpuMclockSet && tuning.ov.tuning.gpuMclock == "7001,6800");
        CHECK(tuning.ov.tuning.gpuCoffsetSet && tuning.ov.tuning.gpuCoffset == "+599,0");
        CHECK(tuning.ov.tuning.gpuMoffsetSet && tuning.ov.tuning.gpuMoffset == "0");
        CHECK(tuning.ov.tuning.gpuPlimitSet && tuning.ov.tuning.gpuPlimit == "175");
        CHECK(tuning.ov.tuning.gpuFanSet && tuning.ov.tuning.gpuFan == "80,75");
        CHECK(tuning.ov.tuning.ocDelaySet && tuning.ov.tuning.ocDelaySeconds == 5 && tuning.rest.empty());
        CHECK(parseArgs({"--gpu-cclock=2500", "--oc-reset-only"}).ov.tuning.ocResetOnly);
        for (const std::vector<std::string> &bad : {std::vector<std::string>{"--gpu-cclock"}, {"--gpu-cclock", ""}, {"--gpu-fan=",},
                                                     {"--oc-delay", "1x"}, {"--oc-delay", "4294967296"}, {"--gpu-moffset"}}) {
            std::string err;
            CHECK(parseArgs(bad, &err).command == static_cast<Command>(99) && !err.empty());
        }
        {
            std::string err;
            parseArgs({"--gpu-cclock", "100", "--gpu-cclock", "200"}, &err);
            CHECK(contains(err, "given twice"));
        }
        {
            std::string err;
            parseArgs({"--oc-core"}, &err);
            CHECK(contains(err, "unknown GPU tuning flag"));
        }
        // research flags still go to the legacy loop untouched
        CHECK(parseArgs({"--pearl-network", "mainnet"}).rest.size() == 2);
        CHECK(parseArgs({"--mem-oc"}).rest.size() == 1);
    }

    // ---- CLI over config: precedence and the file keeps what the CLI does not name
    {
        ProductConfig base;
        std::string e;
        CHECK(parseConfig("{\"schema_version\":1,\"wallet\":\"prl1file\",\"worker\":\"fileworker\",\"pools\":[\"herominers\"]}", &base, &e));
        CliParse o = parseArgs({"--worker", "clirig"});
        CHECK(applyOverrides(o.ov, &base, &e) && base.wallet == "prl1file" && base.worker == "clirig" && base.pools[0].preset == "herominers");
    }

    // ---- help: the grouped text, and it names neither the donation account nor the donation address
    {
        CHECK(parseArgs({"--help"}).help && parseArgs({"-h"}).help);
        CHECK(!parseArgs({"run"}).help);
        const std::string help = productHelpText();
        for (const char *word : {"Pool", "Devices", "API", "Donation", "Other", "--pool", "--wallet", "--api-enable", "--gpu-id",
                                 "--no-donation", "--gpu-cclock", "--gpu-mclock", "--gpu-coffset", "--gpu-moffset", "--gpu-plimit", "--gpu-fan",
                                 "--oc-delay", "--oc-reset-only", "tuning.gpu_cclock", "Windows:", "Linux:", "mined on F2Pool to the developer"})
            CHECK_MSG(word, contains(help, word));
        // Amendment 2: the rendered help must not show the donation account or payout address (values come from fee.h)
        CHECK_MSG("help hides donation account", !contains(help, CANARY_FEE_POOL_ACCOUNT));
        CHECK_MSG("help hides donation address", !contains(help, CANARY_FEE_DESTINATION));
    }

    return finishTests("test_product_cli");
}
