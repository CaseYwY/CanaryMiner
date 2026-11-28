// Installed sibling helper. Fixed public requests with an optional validated DNS selector.
#include "public_https_client.h"
#include "public_json_process.h"
#include <charconv>
#include <cstdio>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
    if (argc < 8 || argc > 12) return 2;
    uint64_t n[7] = {};
    for (int i = 0; i < 7; ++i) {
        const std::string arg = argv[i + 1];
        if (arg.empty() || arg.size() > 20) return 2;
        for (char c : arg) {
            if (c < '0' || c > '9' || n[i] > (UINT64_MAX - static_cast<unsigned>(c - '0')) / 10) return 2;
            n[i] = n[i] * 10 + static_cast<unsigned>(c - '0');
        }
    }
    if (n[0] > static_cast<unsigned>(om::PublicFeed::MexcPearlPair) || n[1] > INT64_MAX ||
        n[2] > INT64_MAX || n[3] > 60000 || n[4] > 60000 || n[5] > 1024 * 1024 || n[6] > 32768) return 2;
    const auto feed = static_cast<om::PublicFeed>(n[0]);
    const bool octopus = feed == om::PublicFeed::OctopusUnitRates || feed == om::PublicFeed::OctopusStandingCharges;
    const int resolver = octopus ? 11 : 8;
    if (argc != resolver && argc != resolver + 1) return 2;
    if (argc == resolver + 1 && !om::validPublicDnsServer(argv[resolver])) return 2;
#if defined(_WIN32)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 2;
#endif
    std::atomic<bool> cancel{false};
    om::PublicJsonClient client;
    om::PublicJsonRequest request{feed, static_cast<int64_t>(n[1]), static_cast<int64_t>(n[2])};
    if (octopus) {
        const std::string_view region(argv[9]), number(argv[10]);
        if (region.size() != 1 || number.empty() || number.size() > 2 || number.front() == '0') return 2;
        unsigned page = 0;
        const auto end = number.data() + number.size();
        const auto converted = std::from_chars(number.data(), end, page);
        if (converted.ec != std::errc{} || converted.ptr != end) return 2;
        request.octopus = om::OctopusRatePage{argv[8], region.front(), page};
        std::string url;
        if (!om::publicFeedUrl(request, &url)) return 2;
    }
    if (request.feed == om::PublicFeed::PearlIndexedBlockHash) {
        if (n[1] > om::maxPearlRewardTipHeight || n[2]) return 2;
        request.blockHeight = static_cast<uint32_t>(n[1]);
        request.fromUtc = 0;
    }
    auto response = client.get(request, cancel,
        {static_cast<unsigned>(n[3]), static_cast<unsigned>(n[4]), static_cast<size_t>(n[5]), static_cast<size_t>(n[6]),
         argc == resolver + 1 ? argv[resolver] : ""});
    std::string wire;
    if (!om::encodePublicJsonResponse(response, &wire)) return 2;
    return std::fwrite(wire.data(), 1, wire.size(), stdout) == wire.size() && std::fflush(stdout) == 0 ? 0 : 2;
}
