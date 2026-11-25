// Protocol fixture for the actual one-shot CLI. Does not open any sockets.
#include "product/public_json_process.h"
#include "product/executable_path.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string_view>
#include <thread>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
    if (argc != 8 || std::string_view(argv[2]) != "0" || std::string_view(argv[3]) != "0") return 2;
    const std::string_view feed(argv[1]);
    if (feed != "0" && feed != "2") return 3;
    const auto dir = std::filesystem::u8path(om::selfExecutablePath()).parent_path();
    std::string mode;
    {
        std::ifstream input(dir / "ticker-scenario");
        input >> mode;
        std::ofstream output(dir / "ticker-requests", std::ios::app);
        output << feed << '\n';
        if (!output) return 4;
    }
    if (mode == "slow") std::this_thread::sleep_for(std::chrono::seconds(10));
    om::PublicJsonResponse response;
    response.status = om::PublicHttpsStatus::Ok;
    response.httpStatus = 200;
    response.observedUtc = static_cast<uint64_t>(std::time(nullptr));
    if (feed == "0") {
        response.body = R"([{"currency_pair":"PEARL_USDT","last":"2.50"}])";
        if (mode == "future") response.observedUtc += 3600;
        if (mode == "limited") {
            response = {};
            response.status = om::PublicHttpsStatus::HttpStatus;
            response.httpStatus = 429;
            response.retryAfterSeconds = 1;
        }
    } else {
        response.body = R"({"error":[],"result":{"USDTZUSD":{"c":["0.98","1"]}}})";
        if (mode == "foreign")
            response.body = R"({"error":[],"result":{"private-sentinel":{"c":["87213","1"]}}})";
    }
    std::string wire;
    if (!om::encodePublicJsonResponse(response, &wire)) return 5;
#if defined(_WIN32)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 6;
#endif
    if (std::fwrite(wire.data(), 1, wire.size(), stdout) != wire.size() || std::fflush(stdout) != 0) return 7;
    if (mode == "lagged" && feed == "0") std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
}
