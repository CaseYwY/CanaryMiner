// Installed sibling helper. Fixed public request identifiers/numbers only, never mining credentials or arbitrary URLs.
#include "public_json_process.h"
#include <cstdio>
#include <limits>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
    if (argc != 8) return 2;
    uint64_t n[7] = {};
    for (int i = 0; i < 7; ++i) {
        const std::string arg = argv[i + 1];
        if (arg.empty() || arg.size() > 20) return 2;
        for (char c : arg) {
            if (c < '0' || c > '9' || n[i] > (UINT64_MAX - static_cast<unsigned>(c - '0')) / 10) return 2;
            n[i] = n[i] * 10 + static_cast<unsigned>(c - '0');
        }
    }
    if (n[0] > 3 || n[1] > INT64_MAX || n[2] > INT64_MAX || n[3] > 60000 || n[4] > 60000 || n[5] > 1024 * 1024 || n[6] > 32768) return 2;
#if defined(_WIN32)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 2;
#endif
    std::atomic<bool> cancel{false};
    om::PublicJsonClient client;
    auto response = client.get({static_cast<om::PublicFeed>(n[0]), static_cast<int64_t>(n[1]), static_cast<int64_t>(n[2])}, cancel,
        {static_cast<unsigned>(n[3]), static_cast<unsigned>(n[4]), static_cast<size_t>(n[5]), static_cast<size_t>(n[6])});
    std::string wire;
    if (!om::encodePublicJsonResponse(response, &wire)) return 2;
    return std::fwrite(wire.data(), 1, wire.size(), stdout) == wire.size() && std::fflush(stdout) == 0 ? 0 : 2;
}
