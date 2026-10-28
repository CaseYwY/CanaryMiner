// Supervisor (H-032), CPU only: stats round-trip and aggregation, worker arguments, the dashboard at 80 and 120 columns, the plain
// summary, real child processes over pipes, and supervised runs with fake workers: a graceful stop of two workers, and a worker that
// keeps crashing until its restart budget ends the run (about 30 s, the 10 s restart delay three times).

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../src/product/fee.h"
#include "../src/product/supervisor.h"
#include "product_test_util.h"

using namespace om;

namespace {

const char kFakeChild[] = "--fake-child";
const char kFakeCrash[] = "--fake-crash";
const char kFakeHang[] = "--fake-hang";
const char kFakeLatched[] = "--fake-latched";
const char kHandleProbe[] = "--handle-probe";

/** The fake worker: prints a line on each stream, then either waits for stdin to close and exits 3 (a graceful stop), or crashes. */
int fakeChild(const std::string &mode) {
    if (mode == kFakeLatched) return kExitFaultLatched;
    if (mode == kFakeCrash) {
        printf("crashing now\n");
        fflush(stdout);
        return 9;
    }
    if (mode == kFakeHang) {
        printf("ignoring stop until force-stopped\n");
        fflush(stdout);
        for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    printf("hello from child\n");
    fflush(stdout);
    fprintf(stderr, "err line\n");
    fflush(stderr);
    char buf[64];
    while (fread(buf, 1, sizeof(buf), stdin) > 0) {
    }
    return 3;
}

bool has(const std::string &text, const char *needle) { return text.find(needle) != std::string::npos; }

int fakeHistoryChild(int argc, char **argv) {
    int port = 0;
    for (int i = 2; i + 1 < argc; ++i) if (std::string(argv[i]) == "--api-port") port = std::atoi(argv[i + 1]);
    if (!platformInit() || port <= 0) return 81;
    StatsSnapshot stats;
    stats.state = "running";
    stats.poolState = "mining";
    stats.devices.push_back(StatsDevice{});
    stats.devices[0].outcomes.accepted = 2;
    stats.devices[0].outcomes.rejected = 3;
    stats.devices[0].outcomes.pending = 4;
    stats.fee.outcomes.accepted = 999;
    StatsServer server;
    std::string error;
    if (!server.start("127.0.0.1", port, [&] { return statsToJson(stats); }, "{}", &error)) return 82;
    char input[64];
    while (fread(input, 1, sizeof(input), stdin) > 0) {}
    server.stop();
    platformShutdown();
    return 0;
}

WorkerSeed seed(int index) {
    WorkerSeed s;
    s.index = index;
    s.uuid = "GPU-aaaaaaaa-0000-0000-0000-00000000000" + std::to_string(index + 1);
    s.name = "NVIDIA GeForce RTX 5080";
    s.pciBus = "0000:0" + std::to_string(index + 1) + ":00.0";
    s.family = "SM120";
    s.ccMajor = 12;
    s.ccMinor = 0;
    return s;
}

StatsSnapshot workerSnapshot(int index, uint64_t accepted, const std::string &feeState) {
    StatsSnapshot s;
    s.version = "test";
    s.state = "running";
    s.poolState = "mining";
    s.poolEndpoint = "pearl.example:5500";
    StatsDevice d;
    d.index = 0;
    d.uuid = seed(index).uuid;
    d.name = "NVIDIA GeForce RTX 5080";
    d.family = "SM120";
    d.ccMajor = 12;
    d.rateTmacs = 206.1;
    d.rateAvgTmacs = 204.9;
    d.powerWatts = 250.0 + index;
    d.temperatureC = 60 + index;
    d.outcomes.accepted = accepted;
    d.outcomes.rejected = 1;
    d.outcomes.localStale = 2;
    d.outcomes.pending = 3;
    s.devices.push_back(d);
    s.fee.enabled = true;
    s.fee.state = feeState;
    s.fee.destinationConfigured = true;
    s.fee.feeSeconds = 10;
    s.fee.userSeconds = 90;
    s.fee.cycle = 4;
    s.fee.outcomes.accepted = 2;
    s.fee.outcomes.rejected = 1;
    s.fee.pool = "pearl.f2pool.com:5500";
    s.fee.login = "developer";
    return s;
}

SupervisorOptions baseOptions() {
    SupervisorOptions o;
    o.version = "1.0.2-test";
    o.archLabel = "SM120";
    o.poolEndpoint = "pearl.example:5500";
    o.worker = "rig1";
    o.donationOn = true;
    o.donationDestinationConfigured = true;
    o.apiEnabled = false;
    return o;
}

void testFeedLines() {
    std::vector<std::string> got;
    auto sink = [&](const std::string &line) { got.push_back(line); };
    std::string pending;
    feedLines(&pending, "ab\r\ncd", 6, sink);
    CHECK(got.size() == 1 && got[0] == "ab");
    CHECK(pending == "cd");
    feedLines(&pending, "e\n\n", 3, sink);
    CHECK(got.size() == 2 && got[1] == "cde");  // the empty line between is dropped
    CHECK(pending.empty());
}

void testWorkerArgs() {
    const std::vector<std::string> user = {"run", "--pool", "pearl.example:5500", "--wallet", "prl1-example", "--devices", "all",
                                           "--api-port", "8085", "--no-dashboard", "--worker", "rig1"};
    const std::vector<std::string> args = workerArgs(user, seed(1), 8087);
    CHECK(args.size() >= 4 && args[0] == "run");
    CHECK(args[2] == "pearl.example:5500");
    bool sawAll = false, sawDashboard = false, sawApiPort = false, sawChild = false;
    int devices = 0;
    for (size_t i = 0; i < args.size(); ++i) {
        sawAll = sawAll || args[i] == "all";
        sawDashboard = sawDashboard || args[i] == "--no-dashboard";
        if (args[i] == "--devices") {
            devices++;
            CHECK(i + 1 < args.size() && args[i + 1] == seed(1).uuid);
        }
        if (args[i] == "--api-port") {
            sawApiPort = i + 1 < args.size() && args[i + 1] == "8087";
        }
        if (args[i] == "--worker-child") sawChild = true;
    }
    CHECK(!sawAll);
    CHECK(!sawDashboard);
    CHECK(devices == 1);
    CHECK(sawApiPort);
    CHECK(sawChild);
    CHECK(args[args.size() - 2] == "--worker-id" && args[args.size() - 1] == "1");
}

void testStatsRoundTrip() {
    StatsSnapshot a = workerSnapshot(0, 7, "user");
    a.devices[0].powerWatts = -1;  // written as null
    a.devices[0].temperatureC = -1;
    const std::string json = statsToJson(a);
    JsonValue root;
    std::string err;
    CHECK(parseJsonStrict(json, &root, &err));
    StatsSnapshot back;
    CHECK(statsFromJson(root, &back, &err));
    CHECK(back.devices.size() == 1);
    CHECK(back.devices[0].outcomes.accepted == 7);
    CHECK(back.devices[0].outcomes.localStale == 2);
    CHECK(back.devices[0].outcomes.pending == 3);
    CHECK(back.devices[0].powerWatts < 0);
    CHECK(back.devices[0].temperatureC < 0);
    CHECK(back.devices[0].ccMajor == 12 && back.devices[0].ccMinor == 0);
    CHECK(back.fee.feeSeconds == 10 && back.fee.outcomes.accepted == 2);
    CHECK(back.poolState == "mining" && back.poolEndpoint == "pearl.example:5500");
    CHECK(back.fee.login == "developer");
    JsonValue other;
    CHECK(parseJsonStrict("{\"schema\":\"other\"}", &other, &err));
    StatsSnapshot none;
    CHECK(!statsFromJson(other, &none, &err));
}

void testAggregate() {
    SupervisorOptions o = baseOptions();
    std::vector<WorkerView> views(2);
    views[0].seed = seed(0);
    views[0].haveStats = true;
    views[0].stats = workerSnapshot(0, 7, "user");
    views[1].seed = seed(1);  // not reporting yet: listed with zeros, power from NVML
    views[1].telValid = true;
    views[1].tel.powerMilliwatts = 300000;
    views[1].tel.temperatureC = 70;
    StatsSnapshot agg = aggregateSnapshot(o, views, 12.5, "running");
    CHECK(agg.devices.size() == 2);
    CHECK(agg.devices[0].index == 0 && agg.devices[1].index == 1);
    CHECK(agg.devices[1].outcomes.accepted == 0);
    CHECK(agg.devices[1].powerWatts == 300.0);
    CHECK(statsTotals(agg).accepted == 7);
    CHECK(agg.poolState == "mining");
    CHECK(agg.fee.state == "user");
    CHECK(agg.fee.feeSeconds == 10 && agg.fee.userSeconds == 90);

    // a second worker in a donation window: the donation block sums, and the state reads "donation"
    views[1].haveStats = true;
    views[1].stats = workerSnapshot(1, 3, "donation");
    views[1].stats.fee.outcomes.accepted = 4;
    agg = aggregateSnapshot(o, views, 13.0, "running");
    CHECK(agg.fee.state == "donation");
    CHECK(agg.fee.outcomes.accepted == 6);  // 2 + 4
    CHECK(agg.fee.feeSeconds == 20);
    CHECK(agg.fee.windowActive == false);
    CHECK(statsTotals(agg).accepted == 10);
    CHECK(agg.fee.login == "developer");

    // a restarted worker: its earlier run's shares stay in the session totals while the new run starts at zero
    views[1].carried.accepted = 5;
    views[1].carried.rejected = 1;
    views[1].carriedFee.accepted = 2;
    views[1].haveStats = false;  // restarting, not reporting yet
    agg = aggregateSnapshot(o, views, 13.0, "running");
    CHECK(agg.devices[1].outcomes.accepted == 5 && agg.devices[1].outcomes.rejected == 1);
    CHECK(statsTotals(agg).accepted == 12);  // 7 (GPU 0) + 5 carried (GPU 1)
    CHECK(agg.fee.outcomes.accepted == 4);   // 2 (GPU 0) + 2 carried (GPU 1)
    views[1].haveStats = true;               // the new run reports 3 more
    agg = aggregateSnapshot(o, views, 13.0, "running");
    CHECK(agg.devices[1].outcomes.accepted == 8);  // 5 carried + 3 new
    views[1].carried = StatsOutcomes{};
    views[1].carriedFee = StatsOutcomes{};

    o.donationOn = false;
    agg = aggregateSnapshot(o, views, 13.0, "running");
    CHECK(agg.fee.state == "off");
    CHECK(!agg.fee.enabled);

    // the public document carries neither the donation account nor the payout destination
    const std::string json = statsToJson(aggregateSnapshot(baseOptions(), views, 1.0, "running"));
    CHECK(!has(json, CANARY_FEE_POOL_ACCOUNT));
    CHECK(!has(json, CANARY_FEE_DESTINATION));
    CHECK(has(json, "\"login\":\"developer\""));
}

DashModel sampleModel() {
    DashModel m;
    m.version = "1.0.2-test";
    m.poolEndpoint = "pearl.example:5500";
    m.worker = "rig1";
    m.connection = "Connected";
    m.sessionSeconds = 3725;
    DashGpu g0;
    g0.index = 0;
    g0.name = "NVIDIA GeForce RTX 5080";
    g0.state = "Mining";
    g0.rate = 206.1;
    g0.rateAvg = 204.9;
    g0.powerW = 305;
    g0.tempC = 68;
    g0.fanPct = 61;
    g0.coreMhz = 2800;
    g0.memMhz = 10501;
    g0.outcomes.accepted = 12;
    g0.outcomes.localStale = 1;
    g0.outcomes.pending = 1;
    g0.detail = "pci 0000:01:00.0";
    DashGpu g1 = g0;
    g1.index = 1;
    g1.state = "Donation";
    g1.powerW = -1;
    g1.tempC = -1;
    g1.outcomes.accepted = 3;
    m.gpus = {g0, g1};
    m.donation.accepted = 2;
    m.donationOn = true;
    m.events = {{"12:00:01", "GPU 0: accepted +1 share(s)"}, {"12:00:09", "GPU 1: donation window start"}};
    m.log = {"GPU 0: worker output line"};
    return m;
}

// ---- dashboard (H-033): synthetic model, golden frames, colour map, keys, the row window, the model's connection and data age

DashGpu synthGpu(int index, const std::string &state) {
    DashGpu g;
    g.index = index;
    g.name = "NVIDIA GeForce RTX 5070";
    g.state = state;
    g.pciBus = "0000:0" + std::to_string(index + 1) + ":00.0";
    g.poolState = "mining";
    g.rate = 112.7 + index * 0.5;
    g.rateAvg = 110.2 + index * 0.25;
    g.powerW = 156.9;
    g.tempC = 56;
    g.fanPct = 80;
    g.coreMhz = 2542;
    g.memMhz = 7001;
    g.sampleAgeS = 9.3 + index * 0.1;
    g.outcomes.submitted = 4122 + index;
    g.outcomes.accepted = 4000 + index;
    g.outcomes.guardRetired = 3;
    g.outcomes.localStale = 1;
    g.outcomes.stale = 2;
    g.outcomes.rejected = 5;
    g.outcomes.pending = 1;
    return g;
}

/** Synthetic rig: one donation window on GPU 1 when there are two or more GPUs, one restarting GPU when there are eight. */
DashModel synthModel(int gpus) {
    DashModel m;
    m.version = "1.0.3";
    m.poolEndpoint = "pool.example:5500";
    m.worker = "rig1";
    m.connection = "Authorized";
    m.sessionSeconds = 3725;
    for (int i = 0; i < gpus; ++i) {
        std::string state = "Mining";
        if (gpus >= 2 && i == 1) state = "Donation";
        if (gpus == 8 && i == 7) state = "Restarting";
        DashGpu g = synthGpu(i, state);
        if (state == "Restarting") {
            g.restarts = 2;
            g.rate = 0;  // no sample while restarting
            g.poolState = "-";
            g.sampleAgeS = 0.4;
            g.detail = "exited with code 9; restart in 10 s (restart 2)";
        }
        m.gpus.push_back(g);
    }
    m.donation.submitted = 28;
    m.donation.accepted = 27;
    m.donation.rejected = 1;
    m.donationOn = true;
    m.events = {{"12:00:01", "GPU 0: worker started"},
                {"12:00:04", "GPU 0: accepted +2 share(s)"},
                {"12:00:09", "GPU 1: donation window start"},
                {"12:03:10", "GPU 1: donation window end (+27 accepted)"},
                {"12:05:00", "GPU 0: pool connection lost, reconnecting"},
                {"12:05:30", "GPU 0: connected to the pool"},
                {"12:06:00", "GPU 0: exited with code 9; restart in 10 s (restart 1)"}};
    m.log = {"GPU 0: worker output line", "GPU 1: donation window start"};
    return m;
}

void checkWidth(const std::string &frame, int columns) {
    size_t start = 0;
    bool fits = true;
    while (start < frame.size()) {
        const size_t nl = frame.find('\n', start);
        const size_t end = nl == std::string::npos ? frame.size() : nl;
        fits = fits && end - start <= static_cast<size_t>(columns);
        start = end + 1;
    }
    CHECK_MSG("every dashboard line fits the width", fits);
}

std::string stripAnsi(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] != '\x1b') {
            out += s[i++];
            continue;
        }
        size_t j = i + 1;
        if (j < s.size() && s[j] == '[') {
            ++j;
            while (j < s.size() && !std::isalpha(static_cast<unsigned char>(s[j]))) ++j;
            ++j;
        }
        i = j;
    }
    return out;
}

/** Lines in a frame: every line but the last ends with a newline. */
size_t countLines(const std::string &frame) {
    return static_cast<size_t>(std::count(frame.begin(), frame.end(), '\n')) + 1;
}

DashView plainView(int columns, int rows = 0) {
    DashView v;
    v.columns = columns;
    v.rows = rows;
    v.color = ColorLevel::None;
    return v;
}

std::string goldenPath(int columns, int gpus) {
    return "tests/fixtures/dashboard_" + std::to_string(columns) + "_" + std::to_string(gpus) + "gpu.txt";
}

bool readFile(const std::string &path, std::string *out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    *out = buf.str();
    return true;
}

void testDashboardGoldens() {
    // CANARY_WRITE_GOLDENS=1 rewrites the fixtures from the renderer (run from the repository root); review then reads the diff.
    const bool write = std::getenv("CANARY_WRITE_GOLDENS") != nullptr;
    const std::string secrets[] = {std::string(CANARY_FEE_POOL_ACCOUNT), std::string(CANARY_FEE_DESTINATION)};
    for (int cols : {80, 100, 120}) {
        for (int gpus : {1, 2, 8}) {
            DashModel m = synthModel(gpus);
            DashView v = plainView(cols);
            const std::string frame = renderDashboard(m, v);
            checkWidth(frame, cols);
            CHECK(!has(frame, "\x1b"));
            CHECK(!has(frame, "prl1"));
            for (const std::string &s : secrets) CHECK(!s.empty() && !has(frame, s.c_str()));
            const std::string path = goldenPath(cols, gpus);
            if (write) {
                FILE *f = std::fopen(path.c_str(), "wb");
                CHECK_MSG("golden fixture writable", f != nullptr);
                if (f) {
                    std::fwrite(frame.data(), 1, frame.size(), f);
                    std::fclose(f);
                }
                continue;
            }
            std::string golden;
            const bool same = readFile(path, &golden) && golden == frame;
            if (!same) std::printf("  fixture %s differs from the renderer\n", path.c_str());
            CHECK_MSG("golden frame matches its fixture", same);
        }
    }
}

void testDashboardContent() {
    DashModel m = synthModel(2);
    DashView v = plainView(80);
    const std::string frame = renderDashboard(m, v);
    const std::string title = "(o> CanaryMiner 1.0.3  -  rig1";
    CHECK(has(frame, title.c_str()));  // the title, with the bird mark
    CHECK(has(frame, "mining | PRL | Pool pool.example:5500 | Worker rig1"));
    CHECK(has(frame, "Connection Authorized | Mining session 01:02:05"));
    CHECK(has(frame, "Current 225.90 TH/s"));
    CHECK(has(frame, "HARDWARE"));
    CHECK(has(frame, "RTX 5070 #1"));
    CHECK(has(frame, "112.70"));
    CHECK(has(frame, "156.9 W"));
    CHECK(has(frame, "SESSION SHARES"));
    CHECK(has(frame, "Stale local/pool"));
    CHECK(has(frame, "TOTAL"));
    CHECK(has(frame, "Donation"));
    CHECK(has(frame, "Local stale was not sent. Rejected = pool rejections only."));
    CHECK(has(frame, "RECENT EVENTS - UTC"));
    CHECK(has(frame, "12:03:10  GPU 1: donation window end (+27 accepted)"));
    CHECK(has(frame, "Data age 9.4s"));
    CHECK(has(frame, "[D] Details [L] Log [M]"));
    CHECK(has(frame, "[S] Settings"));
    CHECK(has(frame, "Quit"));
    CHECK(has(frame, "Rows 1-"));
    // the title closes the frame
    CHECK(frame.size() >= title.size() && frame.compare(frame.size() - title.size(), title.size(), title) == 0);

    // 80 columns drops Submitted and Retired; 100 and wider keeps them and the Core/Mem columns
    CHECK(!has(frame, "Retired"));
    CHECK(!has(frame, "Submitted"));
    CHECK(has(frame, "Core MHz"));
    DashView wideView = plainView(100);
    const std::string wide = renderDashboard(m, wideView);
    CHECK(has(wide, "Submitted"));
    CHECK(has(wide, "Retired"));

    // a fixed-width table never spills at any width, even with eight GPUs
    for (int cols : {60, 80, 100, 120}) {
        DashModel eight = synthModel(8);
        DashView vv = plainView(cols);
        checkWidth(renderDashboard(eight, vv), cols);
    }

    // the details page carries the PCI bus, restarts and the last error; the main page does not
    DashModel eight = synthModel(8);
    DashView details = plainView(80);
    details.page = DashPage::Details;
    const std::string det = renderDashboard(eight, details);
    CHECK(has(det, "DETAILS"));
    CHECK(has(det, "pci 0000:08:00.0"));
    CHECK(has(det, "Restarts 2"));
    CHECK(has(det, "Last error: exited with code 9; restart in 10 s (restart 2)"));
    CHECK(!has(frame, "pci 0000:01:00.0"));
    DashView logView = plainView(80);
    logView.page = DashPage::Log;
    const std::string logFrame = renderDashboard(m, logView);
    CHECK(has(logFrame, "WORKER OUTPUT (redacted)"));
    CHECK(has(logFrame, "GPU 0: worker output line"));
}

void testDashboardColours() {
    DashModel m = synthModel(2);
    DashView truecolour = plainView(100);
    truecolour.color = ColorLevel::TrueColor;
    const std::string painted = renderDashboard(m, truecolour);
    CHECK(has(painted, "\x1b[38;2;255;210;31m"));   // canary yellow: the live telemetry numbers
    CHECK(has(painted, "\x1b[38;2;255;243;176m"));  // soft yellow-white: the donation window
    CHECK(has(painted, "\x1b[92m"));                // private-viewer green: Mining, Authorized, accepted events
    CHECK(has(painted, "\xe2\x94\x80"));            // box-drawing rules on a colour terminal
    DashView plain = plainView(100);
    std::string unpainted = stripAnsi(painted);     // colour changes nothing but the paint (and '-' rules become box lines)
    for (size_t at; (at = unpainted.find("\xe2\x94\x80")) != std::string::npos;) unpainted.replace(at, 3, "-");
    CHECK(unpainted == renderDashboard(m, plain));
    dash_detail::g_dashBoxRules = false;            // a console without UTF-8 keeps '-' rules
    CHECK(!has(renderDashboard(m, truecolour), "\xe2\x94\x80"));
    dash_detail::g_dashBoxRules = true;

    DashView palette = plainView(100);
    palette.color = ColorLevel::Palette256;
    CHECK(has(renderDashboard(m, palette), "\x1b[38;5;220m"));
    DashView basic = plainView(100);
    basic.color = ColorLevel::Basic16;
    CHECK(has(renderDashboard(m, basic), "\x1b[93m"));

    // the warm-up note shows for the first 5 minutes of a session only
    DashModel early = synthModel(1);
    early.sessionSeconds = 30;
    DashView v80 = plainView(100);
    CHECK(has(renderDashboard(early, v80), "Allow about 5 minutes for the hashrate to stabilize."));
    early.sessionSeconds = 300;
    CHECK(!has(renderDashboard(early, v80), "Allow about 5 minutes"));

    // the state to tone map: colour is never the only signal, so each state also has its label in the frame
    CHECK(dash_detail::stateTone("Mining") == Tone::Green);  // as in the private viewer
    CHECK(dash_detail::connectionTone("Authorized") == Tone::Green);
    CHECK(dash_detail::eventTone("GPU 0: accepted +1 share(s)") == Tone::Green);
    CHECK(dash_detail::stateTone("Donation") == Tone::Donation);
    CHECK(dash_detail::stateTone("Connecting") == Tone::Amber);
    CHECK(dash_detail::stateTone("Restarting") == Tone::Amber);
    CHECK(dash_detail::stateTone("Error") == Tone::Red);
    CHECK(dash_detail::stateTone("Stopped") == Tone::Red);
    CHECK(dash_detail::birdTone(synthModel(1)) == Tone::Canary);
    DashModel failing = synthModel(2);
    failing.gpus[0].state = "Error";
    CHECK(dash_detail::birdTone(failing) == Tone::Red);
}

void testDashboardKeysAndWindow() {
    DashModel m = synthModel(8);
    DashView v = plainView(80, 20);
    CHECK(handleDashKey(v, 'd') && v.page == DashPage::Details);
    CHECK(handleDashKey(v, 'D') && v.page == DashPage::Main);
    CHECK(handleDashKey(v, 'l') && v.page == DashPage::Log);
    CHECK(handleDashKey(v, 'm') && v.page == DashPage::Main);
    CHECK(!handleDashKey(v, 'x'));

    // a 20-row terminal: the frame is one row short of it, the footer stays at the bottom, the page scrolls
    const std::string first = renderDashboard(m, v);
    CHECK(countLines(first) == 19);
    CHECK(has(first, "Rows 1-"));
    CHECK(has(first, "(o> CanaryMiner"));
    CHECK(handleDashKey(v, kKeyPageDown));
    const std::string paged = renderDashboard(m, v);
    CHECK(v.scroll > 0);
    CHECK(has(paged, ("Rows " + std::to_string(v.scroll + 1) + "-").c_str()));
    CHECK(countLines(paged) == 19);
    CHECK(!has(paged, "HARDWARE\n"));  // the header scrolled off the top

    // scrolling past the end stops at the last page: its row counter ends at the page length
    DashView full = plainView(80, 0);
    const std::string whole = renderDashboard(m, full);
    const size_t pageLength = std::stoul(whole.substr(whole.find('/', whole.find("Rows 1-")) + 1));
    for (int i = 0; i < 200; ++i) handleDashKey(v, kKeyDown);
    const std::string end = renderDashboard(m, v);
    CHECK(has(end, ("-" + std::to_string(pageLength) + "/" + std::to_string(pageLength)).c_str()));
    CHECK(v.scroll < 200);
    CHECK(countLines(end) == 19);
    DashView up = plainView(80, 20);
    up.scroll = -5;
    renderDashboard(m, up);
    CHECK(up.scroll == 0);  // never above the top
    DashView farView = plainView(80, 20);
    farView.scroll = 10000;
    renderDashboard(m, farView);
    CHECK(farView.scroll < 10000);  // clamped to the end of the page
}

void testDashboardModel() {
    SupervisorOptions o = baseOptions();
    std::vector<WorkerView> views(2);
    views[0].seed.index = 0;
    views[1].seed.index = 1;
    views[0].state = "Mining";
    views[1].state = "Connecting";
    views[0].lastSampleMs = 1000;
    StatsSnapshot agg = aggregateSnapshot(o, views, 1.0, "running");
    DashModel m = dashboardModel(o, views, agg, 1.0, 10300, {}, {});
    CHECK(m.connection == "Authorized");
    CHECK(m.gpus[0].sampleAgeS > 9.29 && m.gpus[0].sampleAgeS < 9.31);  // 9.3 s since the last stats sample
    CHECK(m.gpus[1].sampleAgeS < 0);                                    // never sampled
    views[0].lastSampleMs = 10400;  // sampled just after nowMs was read: clamps to 0, never wraps to 1.8e16 s
    m = dashboardModel(o, views, agg, 1.0, 10300, {}, {});
    CHECK(m.gpus[0].sampleAgeS == 0.0);
    views[0].lastSampleMs = 1000;
    views[0].state = "Error";
    views[1].state = "Error";
    m = dashboardModel(o, views, aggregateSnapshot(o, views, 1.0, "running"), 1.0, 10300, {}, {});
    CHECK(m.connection == "Error");
    views[0].state = "Stopped";
    views[1].state = "Stopped";
    m = dashboardModel(o, views, aggregateSnapshot(o, views, 1.0, "running"), 1.0, 10300, {}, {});
    CHECK(m.connection == "Stopped");
    views[0].state = "Connecting";
    views[1].state = "Connecting";
    m = dashboardModel(o, views, aggregateSnapshot(o, views, 1.0, "running"), 1.0, 10300, {}, {});
    CHECK(m.connection == "Connecting");
    // no data at all: the footer says so instead of showing a number
    DashModel none;
    DashView noneView = plainView(80);
    CHECK(has(renderDashboard(none, noneView), "Data age N/A"));
}

void testPlainSummary() {
    const std::string line = renderPlainSummary(sampleModel(), 0);
    CHECK(line.size() > 2 && line[0] == '[');
    CHECK(has(line, "Current 412.2 TH/s"));
    CHECK(has(line, "accepted 15"));
    CHECK(has(line, "GPU0 Mining"));
    CHECK(has(line, "GPU1 Donation"));
    CHECK(!has(line, "prl1"));
}

void testChildPipes() {
    std::mutex mu;
    std::vector<std::string> lines;
    ChildProcess child;
    std::string err;
    const std::string exe = selfExecutablePath();
    CHECK(!exe.empty());
    const bool started = child.start(exe, {kFakeChild}, [&](const std::string &line) {
        std::lock_guard<std::mutex> g(mu);
        lines.push_back(line);
    }, &err);
    CHECK_MSG("child starts", started);
    if (!started) return;
    child.closeStdin();  // the stop request: the child exits 3 after its output
    int code = -1;
    CHECK(child.waitFor(15000, &code));
    CHECK(code == 3);
    child.joinReader();
    std::lock_guard<std::mutex> g(mu);
    bool hello = false, errLine = false;
    for (const std::string &l : lines) {
        hello = hello || l == "hello from child";
        errLine = errLine || l == "err line";
    }
    CHECK(hello);
    CHECK(errLine);

    ChildProcess crasher;
    CHECK(crasher.start(exe, {kFakeCrash}, [](const std::string &) {}, &err));
    int crashCode = -1;
    CHECK(crasher.waitFor(15000, &crashCode));
    CHECK(crashCode == 9);
    crasher.joinReader();
}

/** Runs the supervisor over `workers` fake workers and stops it after `stopAfterMs` unless it ends first. */
int supervisedRun(const std::vector<std::string> &userArgs, int workers, uint64_t stopAfterMs, SupervisorTuning *tuning = nullptr) {
    volatile sig_atomic_t stop = 0;
    SupervisorOptions o = baseOptions();
    o.exe = selfExecutablePath();
    o.userArgs = userArgs;
    for (int i = 0; i < workers; ++i) o.seeds.push_back(seed(i));
    o.workerPortBase = 18990;
    o.drainMs = 1000;
    o.stop = &stop;
    o.dashboard = false;
    o.tuning = tuning;
    std::atomic<bool> finished{false};
    std::thread timer([&] {
        for (uint64_t t = 0; t < stopAfterMs / 20 && !finished.load(); ++t) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        stop = 1;
    });
    Supervisor sup(std::move(o));
    const int rc = sup.run();
    finished = true;
    timer.join();
    return rc;
}

#if defined(_WIN32)
/** Try to signal the parent's event. A reused numeric handle in this process is NOT evidence of inheritance. */
int handleProbeChild(const char *value) {
    const HANDLE h = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(strtoull(value, nullptr, 10)));
    SetEvent(h);  // a missing/different handle cannot signal the exact object the parent observes
    return 0;
}

/** A pipe end and an unrelated inheritable event are both inheritable in the parent. Only the pipe ends may reach the worker. */
void testHandleListInheritance() {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE unrelated = CreateEventA(&sa, TRUE, FALSE, nullptr);
    CHECK(unrelated != nullptr);
    if (!unrelated) return;
    std::string err;
    ChildProcess child;
    const std::string value = std::to_string(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(unrelated)));
    CHECK(child.start(selfExecutablePath(), {kHandleProbe, value}, [](const std::string &) {}, &err));
    int code = -1;
    CHECK(child.waitFor(15000, &code));
    CHECK(code == 0);
    CHECK_MSG("unrelated event object is not inherited by the worker", WaitForSingleObject(unrelated, 0) == WAIT_TIMEOUT);
    child.joinReader();

    // Positive control: unrestricted inheritance really must signal this exact parent event.
    // This proves the oracle would detect removing the production handle allow-list.
    const std::string command = "\"" + selfExecutablePath() + "\" " + kHandleProbe + " " + value;
    std::vector<char> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back('\0');
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process);
    CHECK_MSG("positive control starts with unrestricted inheritance", created != FALSE);
    if (created) {
        CHECK(WaitForSingleObject(process.hProcess, 15000) == WAIT_OBJECT_0);
        CHECK_MSG("inherited event is observable in the parent", WaitForSingleObject(unrelated, 0) == WAIT_OBJECT_0);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    CloseHandle(unrelated);
}
#endif

void testSupervisedStop() {
    // two workers mine; a stop request closes both stdins; both drain (exit 3) and the run ends cleanly
    CHECK(supervisedRun({kFakeChild}, 2, 1500) == kExitOk);
}

struct TuningHookProbe final : SupervisorTuning {
    std::vector<std::string> calls;
    void start(uint64_t, std::vector<std::string> *events) override {
        calls.push_back("start");
        events->push_back("tuning hook started");
    }
    void tick(uint64_t, std::vector<std::string> *) override { calls.push_back("tick"); }
    void stop(std::vector<std::string> *events) override {
        calls.push_back("stop");
        events->push_back("tuning hook stopped");
    }
};

void testTuningHookOnForcedStop() {
    TuningHookProbe probe;
    CHECK(supervisedRun({kFakeHang}, 1, 500, &probe) == kExitShutdownForced);
    CHECK(probe.calls.size() >= 3 && probe.calls.front() == "start" && probe.calls.back() == "stop");
    bool sawTick = false;
    for (const std::string &call : probe.calls) sawTick = sawTick || call == "tick";
    CHECK(sawTick);
}

void testSupervisedRestartBudget() {
    // a worker that crashes at once is restarted after 10 s, three times within the hour; the fourth crash ends the run with an error
    CHECK(supervisedRun({kFakeCrash}, 1, 120000) == kExitFaultLatched);
}

void testStopAlreadyRequested() {
    volatile sig_atomic_t stop = 1;
    auto options = baseOptions();
    options.exe = "must-not-be-executed";
    options.seeds = {seed(0)};
    options.stop = &stop;
    TuningHookProbe probe;
    options.tuning = &probe;
    Supervisor supervisor(std::move(options));
    CHECK(supervisor.run() == kExitOk);
    CHECK(probe.calls.empty());
}

}  // namespace

int main(int argc, char **argv) {
    if (argc >= 2 && std::string(argv[1]) == "--fake-history-child") return fakeHistoryChild(argc, argv);
#if defined(_WIN32)
    if (argc >= 3 && std::string(argv[1]) == kHandleProbe) return handleProbeChild(argv[2]);
    if (argc == 2 && std::string(argv[1]) == "--handle-isolation-test") {
        testHandleListInheritance();
        return finishTests("handle_isolation");
    }
#endif
    if (argc >= 2 && (std::string(argv[1]) == kFakeChild || std::string(argv[1]) == kFakeCrash || std::string(argv[1]) == kFakeHang || std::string(argv[1]) == kFakeLatched))
        return fakeChild(argv[1]);
    if (argc == 3 && std::string(argv[1]) == "--dashboard-session-test") {
        if (!platformInit()) return 83;
        StatsServer probe;
        std::string error;
        if (!probe.start("127.0.0.1", 0, [] { return "{}"; }, "{}", &error)) return 84;
        const int port = probe.port();
        probe.stop();
        SupervisorOptions options = baseOptions();
        options.exe = selfExecutablePath();
        options.userArgs = {"--fake-history-child"};
        options.seeds = {seed(0)};
        options.dashboard = true;
        options.uiPreferencesPath = (std::filesystem::path(argv[2]) / "ui-preferences.json").string();
        options.historyPath = (std::filesystem::path(argv[2]) / "accepted-history.sqlite3").u8string();
        options.workerPortBase = port;
        Supervisor supervisor(std::move(options));
        const int result = supervisor.run();
        platformShutdown();
        return result;
    }
    testFeedLines();
    testStopAlreadyRequested();
    CHECK(supervisedRun({kFakeLatched}, 1, 120000) == kExitFaultLatched);
    testWorkerArgs();
    testStatsRoundTrip();
    testAggregate();
    testDashboardGoldens();
    testDashboardContent();
    testDashboardColours();
    testDashboardKeysAndWindow();
    testDashboardModel();
    testPlainSummary();
    testChildPipes();
#if defined(_WIN32)
    testHandleListInheritance();
#endif
    testSupervisedStop();
    testTuningHookOnForcedStop();
    testSupervisedRestartBudget();
    return finishTests("product_supervisor");
}
