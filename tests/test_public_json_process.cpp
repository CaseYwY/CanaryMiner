#include "product/public_json_process.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>
#include <cstdlib>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace om;
int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (argc != 2) return finishTests("public_json_process");
    PublicJsonResponse input, output;
    std::string wire;
    CHECK(encodePublicJsonResponse(input, &wire));
    CHECK(decodePublicJsonResponse(wire, 1024, &output));
    CHECK(output.status == PublicHttpsStatus::Unavailable);
    input.status = PublicHttpsStatus::Ok; input.httpStatus = 200; input.observedUtc = 1; input.body = "{\"fixture\":true}";
    CHECK(encodePublicJsonResponse(input, &wire));
    CHECK(decodePublicJsonResponse(wire, 1024, &output));
    CHECK(output.body == input.body && output.httpStatus == 200 && output.observedUtc == 1);
    for (const auto &bad : {"", "CMJSON2 0 200 1 0 2\n{}", "CMJSON1 0 200 1 0 3\n{}", "CMJSON1 0 200 1 0 2\n{}extra", "CMJSON1 0 200 1 0 2\n{",
                           "CMJSON1 0 200 1 0 02\n{}", "CMJSON1 99 200 1 0 2\n{}", "CMJSON1 0 201 1 0 2\n{}", "CMJSON1 0 200 0 0 2\n{}",
                           "CMJSON1 0 200 1 3601 2\n{}", "CMJSON1 0 200 1 0 2\n[]x", "CMJSON1 1 0 0 0 2\n{}", "CMJSON1 0 200 -1 0 2\n{}",
                           "CMJSON1 0 200 18446744073709551616 0 2\n{}", "CMJSON1 0 200 1 0 2 extra\n{}"}) CHECK(!decodePublicJsonResponse(bad, 1024, &output));
    CHECK(!decodePublicJsonResponse(wire, 1, &output));
    CHECK(!decodePublicJsonResponse(wire, 1024, nullptr));
    input.status = PublicHttpsStatus::Network;
    CHECK(!encodePublicJsonResponse(input, &wire));
    std::atomic<bool> cancel{true};
    PublicJsonProcessClient missing(std::filesystem::path(argv[1]).parent_path() / "canary-missing-synthetic-helper");
    CHECK(missing.get({}, cancel).status == PublicHttpsStatus::Cancelled);
    cancel = false;
    CHECK(missing.get({}, cancel).status == PublicHttpsStatus::Unavailable);
    PublicJsonProcessClient relative("relative-helper");
    CHECK(relative.get({}, cancel).status == PublicHttpsStatus::Unavailable);
    PublicJsonProcessClient client(std::filesystem::absolute(argv[1]));
#if defined(_WIN32)
    _putenv_s("CANARY_SYNTHETIC_SECRET", "must-not-inherit");
    _putenv_s("SSLKEYLOGFILE", "must-not-inherit");
    _putenv_s("HTTPS_PROXY", "must-not-inherit");
#else
    setenv("CANARY_SYNTHETIC_SECRET", "must-not-inherit", 1);
    setenv("SSLKEYLOGFILE", "must-not-inherit", 1);
    setenv("HTTPS_PROXY", "must-not-inherit", 1);
    const int descriptor = open("/dev/null", O_RDONLY);  // intentionally not CLOEXEC
    CHECK(descriptor >= 0);
#endif
    auto request = [](unsigned scenario) { return PublicJsonRequest{PublicFeed::GatePearlCandles, scenario * 60, (scenario + 1) * 60}; };
    const PublicHttpsLimits limits{300, 600, 8192, 2048};
    CHECK(client.get(request(1), cancel, limits).status == PublicHttpsStatus::Ok);
    CHECK(client.get({}, cancel, {0, 600}).status == PublicHttpsStatus::InvalidRequest);
    auto start = std::chrono::steady_clock::now();
    CHECK(client.get(request(2), cancel, limits).status == PublicHttpsStatus::Timeout);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    for (unsigned scenario : {3u, 4u, 5u, 6u}) {
        const auto result = client.get(request(scenario), cancel, limits);
        CHECK(result.status == PublicHttpsStatus::Unavailable);
        CHECK(result.body.empty() && !result.observedUtc);
    }
    PublicJsonResponse stopped;
    start = std::chrono::steady_clock::now();
    std::thread running([&] { stopped = client.get(request(2), cancel, {1000, 15000, 8192, 2048}); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(client.get(request(1), cancel, limits).status == PublicHttpsStatus::Busy);
    cancel = true;
    running.join();
    CHECK(stopped.status == PublicHttpsStatus::Cancelled);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    cancel = false;
    CHECK(client.get(request(1), cancel, limits).status == PublicHttpsStatus::Ok);  // no stale bytes or previous child after cancellation
    const auto directory = std::filesystem::temp_directory_path() /
        (std::filesystem::path(L"canary-synthetic helper \u6d4b\u8bd5-") += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(directory));
    const auto copy = directory / std::filesystem::path(argv[1]).filename();
    CHECK(std::filesystem::copy_file(std::filesystem::absolute(argv[1]), copy));
    {
        PublicJsonProcessClient unicode(copy);
        CHECK(unicode.get(request(1), cancel, limits).status == PublicHttpsStatus::Ok);
    }
    CHECK(std::filesystem::remove(copy));
    CHECK(std::filesystem::remove(directory));
#if defined(_WIN32)
    _putenv_s("CANARY_SYNTHETIC_SECRET", ""); _putenv_s("SSLKEYLOGFILE", ""); _putenv_s("HTTPS_PROXY", "");
#else
    close(descriptor);
    unsetenv("CANARY_SYNTHETIC_SECRET"); unsetenv("SSLKEYLOGFILE"); unsetenv("HTTPS_PROXY");
#endif
    return finishTests("public_json_process");
}
