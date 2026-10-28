// Deliberately misbehaving child for IPC/lifecycle tests. No DNS or TLS calls.
#include "product/public_json_process.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#endif

int main(int argc, char **argv) {
    if (argc != 8) return 2;
#if defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (std::getenv("CANARY_SYNTHETIC_SECRET") || std::getenv("SSLKEYLOGFILE") || std::getenv("HTTPS_PROXY")) return 3;
#if !defined(_WIN32)
    for (int fd = 3; fd < 1024; ++fd) if (fcntl(fd, F_GETFD) != -1) return 4;
#endif
    const unsigned scenario = static_cast<unsigned>(std::stoul(argv[2]) / 60);
    if (scenario == 2) { std::this_thread::sleep_for(std::chrono::seconds(10)); return 1; }
    if (scenario == 3) { const std::string text(16384, 'x'); std::fwrite(text.data(), 1, text.size(), stdout); return 0; }
    if (scenario == 4) { std::fputs("CMJSON1 0 200 1 0 2\n{}extra", stdout); return 0; }
    if (scenario == 5) { std::fputs("CMJSON1 0 200 1 0 2\n{", stdout); return 0; }
    om::PublicJsonResponse response;
    response.status = om::PublicHttpsStatus::Ok; response.httpStatus = 200; response.observedUtc = 1; response.body = "{\"fixture\":true}";
    std::string wire;
    if (!om::encodePublicJsonResponse(response, &wire)) return 5;
    std::fwrite(wire.data(), 1, wire.size(), stdout);
    return scenario == 6 ? 1 : 0;
}
