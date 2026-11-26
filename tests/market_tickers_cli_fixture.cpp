// Protocol fixture for the actual one-shot CLI. Does not open any sockets.
#include "product/public_json_process.h"
#include "product/executable_path.h"
#include <charconv>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string_view>
#include <thread>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

bool readTime(std::string_view value, int64_t &time) {
    const auto end = value.data() + value.size();
    const auto result = std::from_chars(value.data(), end, time);
    return result.ec == std::errc() && result.ptr == end && time >= 0;
}

std::string candleBody(bool pearl, int64_t start, unsigned count) {
    std::string body = pearl ? "[" : "{\"candles\":[";
    // Reverse order, with a possibly open boundary row. The in-window data uses two price levels.
    for (int i = static_cast<int>(count); i >= 0; --i) {
        if (i != static_cast<int>(count)) body += ',';
        const auto at = start + i * 60;
        const std::string quoted = std::string("\"") + (i < 30 ? (pearl ? "2" : "0.5") : (pearl ? "4" : "1.5")) + '"';
        if (pearl) {
            body += '[' + std::to_string(at * 1000) + ',' + quoted + ',' + quoted + ',' + quoted + ',' + quoted +
                ",\"1\"," + std::to_string((at + 60) * 1000) + ",\"2\"]";
        } else {
            body += "{\"start\":\"" + std::to_string(at) + "\",\"open\":" + quoted + ",\"high\":" + quoted +
                ",\"low\":" + quoted + ",\"close\":" + quoted + ",\"volume\":\"1\"}";
        }
    }
    body += pearl ? "]" : "]}";
    return body;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 8 && argc != 9) return 2;
    if (argc == 9 && !om::validPublicDnsServer(argv[8])) return 2;
    const std::string_view feed(argv[1]);
    const bool candles = feed == "4" || feed == "5";
    if (!candles && feed != "0" && feed != "2") return 3;
    int64_t from = 0, to = 0;
    if (!readTime(argv[2], from) || !readTime(argv[3], to) ||
        (candles ? from < 60 || from % 60 || to - from != 3600 || to > 253402300799LL : from || to)) return 2;
    if (candles && (std::string_view(argv[4]) != "5000" || std::string_view(argv[5]) != "15000" ||
                    std::string_view(argv[6]) != "65536")) return 2;
    const auto dir = std::filesystem::u8path(om::selfExecutablePath()).parent_path();
    std::string mode;
    {
        std::ifstream input(dir / (candles ? "candle-scenario" : "ticker-scenario"));
        input >> mode;
        std::ofstream output(dir / (candles ? "candle-requests" : "ticker-requests"), std::ios::app);
        output << feed;
        if (candles) output << ' ' << from << ' ' << to;
        output << '\n';
        if (!output) return 4;
    }
    if (argc == 9) {
        std::ofstream output(dir / "resolver-requests", std::ios::app);
        output << argv[8] << '\n';
        if (!output) return 4;
    }
    if (mode == "slow") std::this_thread::sleep_for(std::chrono::seconds(10));
    om::PublicJsonResponse response;
    response.status = om::PublicHttpsStatus::Ok;
    response.httpStatus = 200;
    // Match the real HTTPS worker's clock so coarse time() cannot lag the request's start second.
    const auto observed = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    response.observedUtc = static_cast<uint64_t>(observed);
    if (candles) {
        const bool pearl = feed == "4";
        // Index 60 adds an end-boundary row. The gap fixtures end at index 58.
        response.body = candleBody(pearl, from, ((!pearl && mode == "gap-fx") || (pearl && mode == "gap-pearl")) ? 58 : 60);
        if (mode == "archive") {
            response.body += std::string(33000, ' ') + "\r\n";
            std::ofstream source(dir / (pearl ? "body-pearl" : "body-fx"), std::ios::binary);
            source << response.body;
            if (!source) return 4;
        }
        if (!pearl && mode == "malformed") response.body = R"({"private-sentinel":"untrusted"})";
        if (!pearl && mode == "future") response.observedUtc += 3600;
        if (!pearl && mode == "stale") response.observedUtc -= 120;
        if (!pearl && mode == "blocked") {
            response = {};
            response.status = om::PublicHttpsStatus::DestinationBlocked;
        }
        if (!pearl && mode == "limited") {
            response = {};
            response.status = om::PublicHttpsStatus::HttpStatus;
            response.httpStatus = 429;
            response.retryAfterSeconds = 1;
        }
    } else if (feed == "0") {
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
