#include "credit_process.h"
#include <chrono>
#include <thread>
#include <cstdlib>
#include <fstream>
#if defined(_WIN32) && defined(CANARY_CREDIT_WORKER_TESTING)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#endif
int main(int argc, char **argv) {
    using namespace om;
    try {
#if defined(_WIN32) && defined(CANARY_CREDIT_WORKER_TESTING)
        if (argc == 4) { // prove the inherited request section cannot be remapped writable by the child
            auto h = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(std::strtoull(argv[2],nullptr,10)));
            void *writable = MapViewOfFile(h,FILE_MAP_WRITE,0,0,0);
            if (writable) { UnmapViewOfFile(writable); return 3; }
        }
#endif
        std::string wire; CreditProcessRequest request;
        if (!readLocalTaskInput(argc,argv,&wire) || !decodeCreditRequest(wire,&request)) return 2;
#ifdef CANARY_CREDIT_WORKER_TESTING
        auto marker = request.file; marker += ".fixture-ready";
        if (std::getenv("CANARY_SYNTHETIC_SECRET") || std::getenv("HTTPS_PROXY") || std::getenv("SSLKEYLOGFILE")) return 3;
#if !defined(_WIN32)
        for (int fd = 3; fd < 256; ++fd) { errno = 0; if (fcntl(fd,F_GETFD) >= 0 || errno != EBADF) return 3; }
#endif
        if (request.file.filename() == "fixture-stall-open.db") {
            std::ofstream(marker).put('1'); std::this_thread::sleep_for(std::chrono::seconds(60));
        }
        const bool stallBefore = request.batch && !request.batch->records.empty() && request.batch->records[0].id == "fixture.stall-before-apply";
        if (stallBefore) { std::ofstream(marker).put('1'); std::this_thread::sleep_for(std::chrono::seconds(60)); }
#endif
        const auto response = executeCreditRequest(request);
#ifdef CANARY_CREDIT_WORKER_TESTING
        if (request.batch && !request.batch->records.empty() && request.batch->records[0].id == "fixture.stall-after-apply" && response.applied && *response.applied == CreditLedgerStatus::Ok) {
            std::ofstream(marker).put('1'); std::this_thread::sleep_for(std::chrono::seconds(60));
        }
        if (request.batch && !request.batch->records.empty() && request.batch->records[0].id == "fixture.bad-ack") return writeLocalTaskOutput("[\"CMCA2\"]")?0:4;
        if (request.batch && !request.batch->records.empty() && request.batch->records[0].id == "fixture.overflow-ack") { for (unsigned i=0; i<100; ++i) std::fwrite(wire.data(),1,wire.size(),stdout); return 0; }
#endif
        wire.clear();
        return encodeCreditReply(request,response,&wire) && writeLocalTaskOutput(wire) ? 0 : 4;
    } catch (...) { return 5; } // never print paths, source bindings, record IDs or SQLite diagnostics
}
