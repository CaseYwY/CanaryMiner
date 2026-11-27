#include "product/executable_path.h"
#include "product/public_json_process.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <thread>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
    if (argc != 11 && argc != 12) return 2;
#if defined(_WIN32)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 2;
#endif
    const auto root = std::filesystem::u8path(om::selfExecutablePath()).parent_path();
    {
        std::ofstream log(root / "requests", std::ios::app);
        for (int i : {1, 2, 3, 8, 9, 10}) log << argv[i] << ' ';
        log << (argc == 12 ? argv[11] : "-") << '\n';
        if (!log) return 2;
    }
    const auto name = std::string(argv[1]) + '-' + argv[10] + ".json";
    const auto pause = root / "pause";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (std::filesystem::exists(pause)) {
        if (std::chrono::steady_clock::now() >= deadline) return 2;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto window = root / (std::string(argv[2]) + "-" + name);
    std::ifstream source(std::filesystem::exists(window) ? window : root / name, std::ios::binary);
    if (!source) return 2;
    om::PublicJsonResponse reply;
    reply.status = om::PublicHttpsStatus::Ok;
    reply.httpStatus = 200;
    reply.observedUtc = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    reply.body.assign(std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>());
    std::string wire;
    if (!om::encodePublicJsonResponse(reply, &wire)) return 2;
    return std::fwrite(wire.data(), 1, wire.size(), stdout) == wire.size() && std::fflush(stdout) == 0 ? 0 : 2;
}
