// Ordinary strict runtime against a separately owned localhost peer.
// The request arrives on stdin; no private trust or production credentials are accepted.
#include "product/pool_tls_runtime_transport.h"
#include "core/pearl_pool.h"
#include <cstdio>
#include <iostream>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
    using namespace om;
    const bool fallbackMode = argc == 4 && std::string(argv[2]) == "fallback";
    if (!fallbackMode && (argc != 3 || std::string(argv[2]) != "canonical")) return 2;
    try {
#if defined(_WIN32)
        if (_setmode(_fileno(stdin), _O_BINARY) < 0) return 3;
#endif
        std::string wire;
        char bytes[4096];
        for (;;) {
            const auto count = std::fread(bytes, 1, sizeof(bytes), stdin);
            if (count > localTaskRequestLimit - wire.size()) return 3;
            wire.append(bytes, count);
            if (count < sizeof(bytes)) break;
        }
        PoolTlsCheckRequest request;
        uint64_t parent = 0;
        if (!decodePoolTlsCheckRequest(wire, &request, &parent) || request.host != "localhost" ||
            !request.caPem.empty() || request.windowsPrivateTrust) return 4;
        volatile sig_atomic_t stop = 0;
        PoolTlsRuntimeTransport transport(std::filesystem::u8path(argv[1]), request, &stop,
            fallbackMode ? argv[3] : "");
        PearlPoolSource source(request.host, static_cast<int>(request.port), "placeholder", "fixture",
            {PearlNetwork::Mainnet, PearlTargetSemantics::BaseBeforePenalty}, "", &transport);
        Job job;
        const bool fetched = source.fetch(&job);
        bool submitted = false;
        if (fallbackMode && fetched) {
            Solution solution; solution.nonce = 7; solution.hit[0] = 11; solution.extra = "synthetic-proof";
            std::string error;
            submitted = source.submit(job, solution, &error);
        }
        // A trust failure must happen before PearlPoolSource writes mining.authorize.
        // The independent peer test checks that its application-byte buffer stays empty.
        std::cout << "{\"ok\":" << ((fetched && (!fallbackMode || submitted)) ? 1 : 0)
                  << ",\"plaintext\":" << (transport.plaintextActive() ? 1 : 0)
                  << ",\"tls\":" << static_cast<unsigned>(transport.error()) << "}" << std::endl;
        return 0;
    } catch (...) {
        return 5;
    }
}
