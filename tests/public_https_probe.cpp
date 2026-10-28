// Separate fixture-only process. URLs/loopback injection are not part of the public client API/build.
#include "product/public_https.h"
#include "product/public_json_process.h"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char **argv) {
    if (argc != 7 && argc != 8) return 2;
    const auto port = static_cast<unsigned>(std::stoul(argv[1]));
    const std::string mode = argv[4];
    std::atomic<bool> cancel{false};
    const auto start = std::chrono::steady_clock::now();
    std::thread stop;
    if (mode == "cancel") stop = std::thread([&] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); cancel = true; });
    om::PublicJsonResponse result;
    if (argc == 7) {
        auto client = om::PublicHttpsTestPeer::create(port, argv[2], argv[3], mode == "wrong-name", mode != "blocked");
        if (!client->available()) { if (stop.joinable()) stop.join(); return 3; }
        result = client->get({}, cancel, {500, 800, 8192, 2048});
    } else {
        const char *paths[] = {"/ok", "/chunked", "/redirect", "/http-error", "/html", "/compressed", "/conflict-header", "/large-header",
            "/large-body", "/chunked-large", "/bad-json", "/invalid-utf8", "/deep-json", "/truncated", "/slow-header", "/slow-body", "/rate-limit"};
        unsigned scenario = 0;
        for (unsigned i = 0; i < 17; ++i) if (std::string(argv[2]) == paths[i]) scenario = i + 1;
        if (mode == "wrong-name") scenario = 18;
        if (mode == "blocked") scenario = 19;
        if (std::filesystem::path(argv[3]).filename() == "untrusted.pem") scenario = 20;
        if (!scenario) { if (stop.joinable()) stop.join(); return 2; }
        om::PublicJsonProcessClient client(std::filesystem::absolute(argv[7]));
        const int64_t from = port * 60;
        result = client.get({om::PublicFeed::GatePearlCandles, from, from + scenario * 60}, cancel, {500, 800, 8192, 2048});
    }
    if (stop.joinable()) stop.join();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    const auto expected = static_cast<om::PublicHttpsStatus>(std::stoi(argv[5]));
    std::cout << om::publicHttpsStatusName(result.status) << " http=" << result.httpStatus << " body=" << result.body.size() << " elapsed_ms=" << ms << '\n';
    if (result.status != expected || ms > 2000) return 1;
    if (expected == om::PublicHttpsStatus::Ok) {
        if (result.body != "{\"fixture\":true}" || !result.observedUtc) return 1;
    } else if (!result.body.empty() || result.observedUtc) return 1;
    if (result.retryAfterSeconds != static_cast<unsigned>(std::stoul(argv[6]))) return 1;
    return 0;
}
