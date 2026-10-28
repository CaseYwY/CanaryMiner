// Test-only helper: numeric scenario -> explicit loopback fixture. Never linked into the shipping helper.
#include "product/public_json_process.h"
#include <cstdio>
#include <filesystem>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
    if (argc != 8) return 2;
#if defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const auto from = std::stoull(argv[2]), to = std::stoull(argv[3]);
    if (to <= from || from % 60 || to % 60 || from / 60 > 65535) return 2;
    const auto scenario = (to - from) / 60;
    const char *paths[] = {"/ok", "/chunked", "/redirect", "/http-error", "/html", "/compressed", "/conflict-header", "/large-header",
        "/large-body", "/chunked-large", "/bad-json", "/invalid-utf8", "/deep-json", "/truncated", "/slow-header", "/slow-body", "/rate-limit"};
    const std::string path = scenario >= 1 && scenario <= 17 ? paths[scenario - 1] : "/ok";
    const auto ca = std::filesystem::absolute(argv[0]).parent_path() / (scenario == 20 ? "untrusted.pem" : "root.pem");
    auto client = om::PublicHttpsTestPeer::create(static_cast<unsigned>(from / 60), path, ca.string(), scenario == 18, scenario != 19);
    std::atomic<bool> cancel{false};
    const auto response = client->get({}, cancel, {500, 800, 8192, 2048});
    std::string wire;
    if (!om::encodePublicJsonResponse(response, &wire)) return 2;
    return std::fwrite(wire.data(), 1, wire.size(), stdout) == wire.size() && std::fflush(stdout) == 0 ? 0 : 2;
}
