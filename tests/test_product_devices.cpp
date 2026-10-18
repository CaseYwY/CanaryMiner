// Device identity allowlist and selection.

#include "../src/product/device_policy.h"
#include "product_test_util.h"

using namespace om;

static DeviceIdentity dev(int index, const char *name, int major, int minor, const char *uuid = "GPU-11111111-2222-3333-4444-555555555555",
                          const char *pci = "0000:01:00.0") {
    DeviceIdentity d;
    d.index = index;
    d.name = name;
    d.ccMajor = major;
    d.ccMinor = minor;
    d.uuid = uuid;
    d.pciBus = pci;
    d.memBytes = 16ull << 30;
    return d;
}

static bool has(const std::string &h, const char *n) { return h.find(n) != std::string::npos; }

int main() {
    const DeviceIdentity g5080 = dev(0, "NVIDIA GeForce RTX 5080", 12, 0, "GPU-aaaaaaaa-0000-0000-0000-000000000001", "0000:01:00.0");
    const DeviceIdentity g4070 = dev(1, "NVIDIA GeForce RTX 4070", 8, 9, "GPU-bbbbbbbb-0000-0000-0000-000000000002", "0000:02:00.0");
    const DeviceIdentity g5070 = dev(2, "NVIDIA GeForce RTX 5070", 12, 0, "GPU-cccccccc-0000-0000-0000-000000000003", "0000:03:00.0");

    // allowlist: exactly the contract's SKUs
    size_t n = 0;
    deviceAllowlist(&n);
    CHECK(n == 2);
    CHECK(checkDevice(g5080).allowed && checkDevice(g5080).family == "SM120");
    CHECK(!checkDevice(g4070).allowed && has(checkDevice(g4070).reason, "CanaryMiner supports NVIDIA Blackwell (RTX 50-series, SM120) only"));
    CHECK(checkDevice(g5070).allowed && checkDevice(g5070).family == "SM120");

    // unlisted devices are refused with a reason that names the device
    DeviceVerdict v = checkDevice(dev(0, "NVIDIA GeForce RTX 4070 Ti", 8, 9));
    CHECK(!v.allowed && has(v.reason, "RTX 4070 Ti") && has(v.reason, "CanaryMiner supports NVIDIA Blackwell (RTX 50-series, SM120) only") && v.family.empty());
    CHECK(!checkDevice(dev(0, "NVIDIA GeForce RTX 5090", 12, 0)).allowed);
    CHECK(!checkDevice(dev(0, "NVIDIA GeForce RTX 3090", 8, 6)).allowed);
    CHECK(!checkDevice(dev(0, "", 0, 0)).allowed);
    CHECK(!checkDevice(dev(0, "nvidia geforce rtx 5080", 12, 0)).allowed);  // exact name
    // right name, wrong compute capability
    v = checkDevice(dev(0, "NVIDIA GeForce RTX 5080", 8, 9));
    CHECK(!v.allowed && has(v.reason, "expected 12.0"));
    // malformed UUID
    CHECK(!checkDevice(dev(0, "NVIDIA GeForce RTX 5080", 12, 0, "")).allowed);
    CHECK(!checkDevice(dev(0, "NVIDIA GeForce RTX 5080", 12, 0, "not-a-uuid")).allowed);
    // the build must match the architecture
    CHECK(checkDevice(g5080, 120).allowed && !checkDevice(g4070, 120).allowed);
    v = checkDevice(g5080, 89);
    CHECK(!v.allowed && has(v.reason, "SM89 kernels"));

    // spec matching
    CHECK(deviceSpecMatches("0", g5080) && !deviceSpecMatches("1", g5080));
    CHECK(deviceSpecMatches("GPU-aaaaaaaa-0000-0000-0000-000000000001", g5080) && !deviceSpecMatches("GPU-aaaaaaaa-0000-0000-0000-000000000002", g5080));
    CHECK(deviceSpecMatches("0000:02:00.0", g4070) && !deviceSpecMatches("0000:02:00.0", g5080));
    CHECK(!deviceSpecMatches("", g5080) && !deviceSpecMatches("zero", g5080));

    const std::vector<DeviceIdentity> both = {g5080, g4070};
    DeviceSelection sel;
    // 'all': every allowed device for this build; the others are reported, not fatal
    CHECK(selectDevices({}, both, 120, &sel) && sel.chosen.size() == 1 && sel.chosen[0].index == 0 && sel.refusals.size() == 1 &&
          has(sel.refusals[0].c_str(), "CanaryMiner supports NVIDIA Blackwell (RTX 50-series, SM120) only"));
    CHECK(selectDevices({"all"}, both, 0, &sel) && sel.chosen.size() == 1 && sel.refusals.size() == 1);
    // explicit requests
    CHECK(!selectDevices({"1"}, both, 89, &sel) && sel.chosen.empty() && has(sel.refusals[0].c_str(), "CanaryMiner supports NVIDIA Blackwell (RTX 50-series, SM120) only"));
    CHECK(!selectDevices({"1"}, both, 120, &sel) && sel.chosen.empty() && sel.refusals.size() == 1);  // refused device
    CHECK(!selectDevices({"7"}, both, 120, &sel) && has(sel.refusals[0].c_str(), "no device matches '7'"));
    CHECK(!selectDevices({"0", "7"}, both, 120, &sel) && sel.chosen.size() == 1);  // partial failure is a failure
    CHECK(selectDevices({"0", "GPU-aaaaaaaa-0000-0000-0000-000000000001"}, both, 120, &sel) && sel.chosen.size() == 1);  // duplicates collapse
    // an unsupported card is refused outright, and 'all' skips it
    const std::vector<DeviceIdentity> odd = {dev(0, "NVIDIA GeForce RTX 5090", 12, 0), g5070};
    CHECK(!selectDevices({"0"}, odd, 120, &sel) && has(sel.refusals[0].c_str(), "RTX 5090"));
    CHECK(selectDevices({"all"}, odd, 120, &sel) && sel.chosen.size() == 1 && sel.chosen[0].name == "NVIDIA GeForce RTX 5070");
    // nothing usable
    CHECK(!selectDevices({"all"}, {dev(0, "NVIDIA GeForce RTX 5090", 12, 0)}, 120, &sel) && sel.chosen.empty() && !sel.refusals.empty());
    CHECK(!selectDevices({"all"}, {}, 120, &sel) && has(sel.refusals[0].c_str(), "no CUDA device"));
    return finishTests("test_product_devices");
}
