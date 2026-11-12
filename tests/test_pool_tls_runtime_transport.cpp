// Ordinary TLS connection owner with real native IPC and synthetic TLS peers.
// Synthetic TLS peers plus an owned localhost TCP socket; no external resolver, trust store, account or GPU.
#include "product/pool_tls_runtime_transport.h"
#include "core/pearl_pool.h"
#include "product_test_util.h"
#include <csignal>
#include <fstream>
#include <thread>

using namespace om;
namespace {
using T = PearlPoolTransferStatus;
using W = PearlPoolWaitStatus;
using Security = PoolTransportSecurity;
using Error = PoolTransportError;

void checkObservation(const PearlPoolTransport &transport, Security security, unsigned port, Error error = Error::None) {
    const auto began = poolTransportNowMs();
    const auto value = transport.observation();
    CHECK(value.security == security && value.effectivePort == port && value.error == error);
    CHECK(value.observedMonoMs >= began && value.observedMonoMs <= poolTransportNowMs());
}

void plainSocketObservation() {
    const bool initialized = platformInit();
    CHECK(initialized);
    if (!initialized) return;
    const auto listener = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(listener != OM_INVALID_SOCKET);
    if (listener == OM_INVALID_SOCKET) { platformShutdown(); return; }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#if defined(_WIN32)
    int size = sizeof(address);
#else
    socklen_t size = sizeof(address);
#endif
    const bool ready = ::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0 &&
        ::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) == 0 && ::listen(listener, 1) == 0;
    CHECK(ready);
    if (ready) {
        PearlPoolSocketTransport transport;
        checkObservation(transport, Security::Disconnected, 0);
        const unsigned port = ntohs(address.sin_port);
        const bool opened = transport.open("127.0.0.1", static_cast<int>(port)) == PearlPoolConnectStatus::Connected;
        CHECK(opened);
        if (opened) {
            checkObservation(transport, Security::Plaintext, port);
            const auto peer = ::accept(listener, nullptr, nullptr);
            CHECK(peer != OM_INVALID_SOCKET);
            if (peer != OM_INVALID_SOCKET) {
                OM_CLOSESOCKET(peer);
                CHECK(transport.waitReadable(1000) == W::Ready);
                char byte = 0;
                CHECK(transport.receive(&byte, 1).status == T::EndOfStream);
                checkObservation(transport, Security::Disconnected, 0); // before explicit close
            }
        }
        transport.close();
        checkObservation(transport, Security::Disconnected, 0);
    }
    OM_CLOSESOCKET(listener);
    platformShutdown();
}

PoolTlsCheckRequest request(std::string host = "echo.test") {
    return {true, std::move(host), 1, 5000, {}, {}};
}

bool echo(PearlPoolTransport &transport, const std::string &original) {
    std::size_t sent = 0;
    for (unsigned turns = 0; sent < original.size() && turns < 256; ++turns) {
        const auto result = transport.send(original.data() + sent, original.size() - sent);
        if (result.status == T::Error) return false;
        if (result.status == T::Bytes) sent += result.bytes;
        else if (result.status == T::WouldBlock && transport.waitFor(result.interest, 1000) == W::Error) return false;
    }
    std::string received;
    char bytes[97];
    for (unsigned turns = 0; sent == original.size() && received.size() < original.size() && turns < 1024; ++turns) {
        const auto result = transport.receive(bytes, sizeof(bytes));
        if (result.status == T::Error || result.status == T::EndOfStream) return false;
        if (result.status == T::Bytes) received.append(bytes, result.bytes);
        else if (result.status == T::WouldBlock && transport.waitFor(result.interest, 1000) == W::Error) return false;
    }
    return sent == original.size() && received == original;
}

void reconnect(const std::filesystem::path &helper) {
    volatile sig_atomic_t stop = 0;
    PoolTlsRuntimeTransport transport(helper, request(), &stop);
    std::string original(32779, 'x');
    for (std::size_t i = 0; i < original.size(); ++i) original[i] = static_cast<char>(i % 256);
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const bool opened = transport.open("echo.test", 1) == PearlPoolConnectStatus::Connected;
        CHECK(opened && transport.isOpen() && transport.progressIO());
        if (!opened) break;
        checkObservation(transport, Security::VerifiedTls, 1);
        CHECK(echo(transport, original));
        transport.close();
        CHECK(!transport.isOpen() && !nativeProcessUnresolved());
        checkObservation(transport, Security::Disconnected, 0);
    }
}

void canonicalReconnect(const std::filesystem::path &helper) {
    volatile sig_atomic_t stop = 0;
    PoolTlsRuntimeTransport transport(helper, request("fixture.test"), &stop);
    PearlPoolSource source("fixture.test", 1, "placeholder", "fixture",
        {PearlNetwork::Mainnet, PearlTargetSemantics::BaseBeforePenalty}, "", &transport);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        Job job;
        const bool fetched = source.fetch(&job);
        CHECK(fetched && job.valid && job.jobId == "rpc-job");
        if (!fetched) break;
        Solution solution;
        solution.nonce = 7 + attempt;
        solution.hit[0] = 11;
        solution.extra = "synthetic-proof-" + std::to_string(attempt);
        std::string error;
        CHECK(source.submit(job, solution, &error));
        CHECK(source.submitCounters().accepted == attempt + 1);
        // The canonical source retains its own lifetime and reauthorizes on the next fetch.
        transport.close();
    }
    CHECK(!nativeProcessUnresolved());
}

void noReplayAfterUnknown(const std::filesystem::path &helper) {
    volatile sig_atomic_t stop = 0;
    PoolTlsRuntimeTransport transport(helper, request("lost-submit.test"), &stop);
    PearlPoolSource source("lost-submit.test", 1, "placeholder", "fixture",
        {PearlNetwork::Mainnet, PearlTargetSemantics::BaseBeforePenalty}, "", &transport);
    Job job;
    const bool fetched = source.fetch(&job);
    CHECK(fetched && job.valid);
    if (!fetched) return;
    Solution solution;
    solution.nonce = 7;
    solution.hit[0] = 11;
    solution.extra = "synthetic-proof";
    std::string error;
    CHECK(!source.submit(job, solution, &error));
    PearlSourceEvent events[64];
    const auto count = source.pearlSourceObservability()->drain(events, 64);
    unsigned unknown = 0;
    for (std::size_t i = 0; i < count; ++i)
        if (events[i].delivery == PearlSourceDelivery::PossiblySent && events[i].terminal == PearlSourceTerminal::Unknown) ++unknown;
    CHECK(unknown == 1 && !transport.isOpen());
    CHECK(source.submitCounters().submitted == 0 && source.submitCounters().unknown == 0 && source.submitCounters().accepted == 0);
    Job fresh;
    CHECK(source.fetch(&fresh) && fresh.valid && transport.isOpen());
    const auto after = source.pearlSourceObservability()->drain(events, 64);
    unsigned replayed = 0;
    for (std::size_t i = 0; i < after; ++i)
        if (events[i].delivery == PearlSourceDelivery::PossiblySent || events[i].delivery == PearlSourceDelivery::Sent) ++replayed;
    CHECK(replayed == 0 && source.submitCounters().accepted == 0);
}

void refusals(const std::filesystem::path &helper) {
    volatile sig_atomic_t stop = 0;
    {
        PoolTlsRuntimeTransport transport(helper, request(), &stop);
        CHECK(transport.open("other.test", 1) != PearlPoolConnectStatus::Connected && !transport.isOpen());
    }
    {
        PoolTlsRuntimeTransport transport(helper.parent_path() / "missing-tls-runtime-helper", request(), &stop);
        CHECK(transport.open("echo.test", 1) != PearlPoolConnectStatus::Connected && !transport.isOpen());
        const std::string login = "synthetic-login-never-sent";
        CHECK(transport.send(login.data(), login.size()).status == T::Error);
    }
    {
        PoolTlsRuntimeTransport transport(helper, request("verify-fail.test"), &stop);
        CHECK(transport.open("verify-fail.test", 1) != PearlPoolConnectStatus::Connected);
        CHECK(!transport.isOpen() && transport.error() == PearlTlsError::Verification);
        checkObservation(transport, Security::Disconnected, 0, Error::Verification);
        transport.close();
        checkObservation(transport, Security::Disconnected, 0, Error::Verification);
        const std::string login = "synthetic-login-never-sent";
        CHECK(transport.send(login.data(), login.size()).status == T::Error);
        CHECK(!nativeProcessUnresolved());
    }
    {
        auto privateRequest = request();
        privateRequest.caPem = "synthetic-private-anchor-not-a-certificate";
        PoolTlsRuntimeTransport transport(helper, std::move(privateRequest), &stop);
        CHECK(transport.open("echo.test", 1) != PearlPoolConnectStatus::Connected);
        CHECK(transport.error() == PearlTlsError::InvalidPolicy && !transport.isOpen());
    }
    {
        auto privateRequest = request();
        privateRequest.windowsPrivateTrust = std::make_shared<PearlTlsPrivateTrustInput>();
        PoolTlsRuntimeTransport transport(helper, std::move(privateRequest), &stop);
        CHECK(transport.open("echo.test", 1) != PearlPoolConnectStatus::Connected);
        CHECK(transport.error() == PearlTlsError::InvalidPolicy && !transport.isOpen());
    }
    {
        stop = 1;
        PoolTlsRuntimeTransport transport(helper, request(), &stop);
        CHECK(transport.open("echo.test", 1) != PearlPoolConnectStatus::Connected && !transport.isOpen());
        stop = 0;
        CHECK(transport.open("echo.test", 1) != PearlPoolConnectStatus::Connected);
    }
    {
        PoolTlsRuntimeTransport transport(helper, request(), &stop);
        CHECK(transport.open("echo.test", 1) == PearlPoolConnectStatus::Connected);
        stop = 1;
        CHECK(!transport.isOpen());
        const std::string body = "cancelled";
        CHECK(transport.send(body.data(), body.size()).status == T::Error);
        transport.close();
        stop = 0;
        CHECK(transport.open("echo.test", 1) != PearlPoolConnectStatus::Connected);
    }
    {
        PoolTlsRuntimeTransport transport(helper, request(), &stop);
        CHECK(transport.open("echo.test", 1) == PearlPoolConnectStatus::Connected);
        stop = 1;
        const PearlPoolTransport &observation = transport;
        checkObservation(observation, Security::Disconnected, 0, Error::Cancelled); // only observation before clearing input
        stop = 0;
        CHECK(!observation.isOpen() && transport.error() == PearlTlsError::Cancelled);
        CHECK(transport.open("echo.test", 1) != PearlPoolConnectStatus::Connected);
    }
}

volatile sig_atomic_t signalStop = 0;
void stopSignal(int) { signalStop = 1; }
void cancelOpen(const std::filesystem::path &helper) {
    signalStop = 0;
    const auto previous = std::signal(SIGINT, stopSignal);
    PoolTlsRuntimeTransport transport(helper, request("stall-open.test"), &signalStop);
    std::thread signal([] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); std::raise(SIGINT); });
    const auto began = std::chrono::steady_clock::now();
    const auto opened = transport.open("stall-open.test", 1);
    signal.join();
    CHECK(opened != PearlPoolConnectStatus::Connected && !transport.isOpen());
    CHECK(transport.error() == PearlTlsError::Cancelled);
    CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds(2));
    transport.close();
    signalStop = 0;
    CHECK(transport.open("stall-open.test", 1) != PearlPoolConnectStatus::Connected);
    std::signal(SIGINT, previous);
    CHECK(!nativeProcessUnresolved());
}

void foreignCaller(const std::filesystem::path &helper) {
    volatile sig_atomic_t stop = 0;
    PoolTlsRuntimeTransport transport(helper, request(), &stop);
    CHECK(transport.open("echo.test", 1) == PearlPoolConnectStatus::Connected);
    {
        NativeProcessScope foreign(nativeProcessEpoch() + 1);
        CHECK(!transport.isOpen() && !transport.bufferedRead() && !transport.localReadPending());
        checkObservation(transport, Security::Unknown, 0, Error::Unavailable);
        CHECK(transport.open("echo.test", 1) != PearlPoolConnectStatus::Connected);
        const std::string data = "foreign-data";
        char byte = 0;
        CHECK(transport.send(data.data(), data.size()).status == T::Error);
        CHECK(transport.receive(&byte, 1).status == T::Error);
        CHECK(transport.waitFor(PearlPoolInterest::Read, 0) == W::Error);
        CHECK(transport.waitLocalRead(0) == W::Error);
        CHECK(!transport.setNonblocking(true));
        transport.close();
    }
    CHECK(transport.isOpen() && transport.error() == PearlTlsError::None);
    checkObservation(transport, Security::VerifiedTls, 1);
    CHECK(echo(transport, "original-connection-still-owned"));
    transport.close();
    CHECK(!nativeProcessUnresolved());
}

void originalEpoch(const std::filesystem::path &helper) {
    volatile sig_atomic_t stop = 0;
    PoolTlsRuntimeTransport transport(helper, request(), &stop);
    CHECK(transport.open("echo.test", 1) == PearlPoolConnectStatus::Connected);
    NativeProcessAdmission fault;
    CHECK(fault.begin());
    fault.markUnresolved();
    fault.settled();
    CHECK(!nativeProcessUnresolved());
    checkObservation(transport, Security::Disconnected, 0, Error::Unavailable);
    CHECK(transport.error() == PearlTlsError::Unavailable);
    transport.close();
    CHECK(transport.open("echo.test", 1) != PearlPoolConnectStatus::Connected);
}

void authorizedFallback(const std::filesystem::path &helper) {
    for (const auto *host : {"dns-fail.test", "connect-fail.test", "network-timeout.test"}) {
        volatile sig_atomic_t stop = 0;
        PoolTlsRuntimeTransport strict(helper, request(host), &stop);
        CHECK(strict.open(host, 1) != PearlPoolConnectStatus::Connected && !strict.plaintextActive());
        checkObservation(strict, Security::Disconnected, 0, std::string(host) == "dns-fail.test" ? Error::Dns :
            std::string(host) == "connect-fail.test" ? Error::Connect : Error::Timeout);
        PoolTlsRuntimeTransport transport(helper, request(host), &stop,
            std::string("stratum+tcp://") + host + ":2");
        PearlPoolSource source(host, 1, "placeholder", "fixture",
            {PearlNetwork::Mainnet, PearlTargetSemantics::BaseBeforePenalty}, "", &transport);
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            Job job;
            const bool fetched = source.fetch(&job);
            CHECK(fetched && job.valid && transport.plaintextActive());
            if (!fetched) break;
            checkObservation(transport, Security::Plaintext, 2);
            CHECK(source.transportObservation().effectivePort == 2);
            CHECK(transport.plaintextConnections() == attempt + 1);
            Solution solution;
            solution.nonce = 7 + attempt; solution.hit[0] = 11; solution.extra = "synthetic-proof";
            std::string error;
            CHECK(source.submit(job, solution, &error));
            CHECK(source.submitCounters().accepted == attempt + 1);
            transport.close();
            CHECK(!transport.isOpen() && !nativeProcessUnresolved());
            checkObservation(transport, Security::Disconnected, 0);
        }
    }
}

void fallbackRefusals(const std::filesystem::path &helper) {
    volatile sig_atomic_t stop = 0;
    for (const auto *host : {"verify-fail.test", "handshake-timeout.test", "unavailable-open.test", "lost-open.test"}) {
        PoolTlsRuntimeTransport transport(helper, request(host), &stop, std::string("stratum+tcp://") + host + ":2");
        CHECK(transport.open(host, 1) != PearlPoolConnectStatus::Connected);
        CHECK(!transport.isOpen() && !transport.plaintextActive() && transport.plaintextConnections() == 0);
        CHECK(!nativeProcessUnresolved());
    }
    for (const auto *url : {"stratum+tcp://other.test:2", "stratum+ssl://echo.test:2",
                            "stratum+tcp://echo.test:02", "stratum+tcp://private@echo.test:2"}) {
        PoolTlsRuntimeTransport transport(helper, request(), &stop, url);
        CHECK(transport.open("echo.test", 1) != PearlPoolConnectStatus::Connected);
        CHECK(transport.error() == PearlTlsError::InvalidPolicy && transport.plaintextConnections() == 0);
    }
    PoolTlsRuntimeTransport transport(helper, request("connect-fail.test"), &stop, "stratum+tcp://connect-fail.test:2");
    CHECK(transport.open("connect-fail.test", 1) == PearlPoolConnectStatus::Connected && transport.plaintextActive());
    {
        NativeProcessScope foreign(nativeProcessEpoch() + 1);
        CHECK(!transport.plaintextActive() && transport.plaintextConnections() == 0);
        transport.close();
        CHECK(transport.open("connect-fail.test", 1) != PearlPoolConnectStatus::Connected);
    }
    CHECK(transport.isOpen() && transport.plaintextActive());
    stop = 1;
    const PearlPoolTransport &observation = transport;
    CHECK(!observation.isOpen());
    stop = 0;
    CHECK(transport.open("connect-fail.test", 1) != PearlPoolConnectStatus::Connected);
    CHECK(transport.error() == PearlTlsError::Cancelled);
}

void noReplayOnFallback(const std::filesystem::path &helper) {
    volatile sig_atomic_t stop = 0;
    PoolTlsRuntimeTransport transport(helper, request("fallback-lost-submit.test"), &stop,
        "stratum+tcp://fallback-lost-submit.test:2");
    PearlPoolSource source("fallback-lost-submit.test", 1, "placeholder", "fixture",
        {PearlNetwork::Mainnet, PearlTargetSemantics::BaseBeforePenalty}, "", &transport);
    Job job;
    const bool fetched = source.fetch(&job);
    CHECK(fetched && job.valid && transport.plaintextActive());
    if (!fetched) return;
    Solution solution; solution.nonce = 7; solution.hit[0] = 11; solution.extra = "synthetic-proof";
    std::string error;
    CHECK(!source.submit(job, solution, &error));
    PearlSourceEvent events[64];
    const auto count = source.pearlSourceObservability()->drain(events, 64);
    unsigned unknown = 0;
    for (std::size_t i = 0; i < count; ++i)
        if (events[i].delivery == PearlSourceDelivery::PossiblySent && events[i].terminal == PearlSourceTerminal::Unknown) ++unknown;
    CHECK(unknown == 1 && !transport.isOpen() && source.submitCounters().accepted == 0);
    Job fresh;
    CHECK(source.fetch(&fresh) && fresh.valid && transport.plaintextActive());
    const auto after = source.pearlSourceObservability()->drain(events, 64);
    unsigned replayed = 0;
    for (std::size_t i = 0; i < after; ++i)
        if (events[i].delivery == PearlSourceDelivery::PossiblySent || events[i].delivery == PearlSourceDelivery::Sent) ++replayed;
    CHECK(replayed == 0 && source.submitCounters().accepted == 0);
}

struct StageMarker {
    std::string host;
    std::filesystem::path directory;
    bool ready = false;
    StageMarker(const std::filesystem::path &helper, const char *prefix) {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        host = std::string(prefix) + std::to_string(localTaskProcessId()) + "-" + std::to_string(unique) + ".test";
        const auto parent = helper.parent_path().lexically_normal();
        directory = (parent / (".tls-runtime-" + host)).lexically_normal();
        std::error_code error;
        ready = directory.parent_path() == parent && std::filesystem::create_directory(directory, error);
    }
    ~StageMarker() {
        if (!ready) return;
        std::error_code error;
        // Remove only this attempt's two known marker files and its now-empty directory.
        std::filesystem::remove(directory / "plain-open.tmp", error);
        std::filesystem::remove(directory / "plain-open", error);
        std::filesystem::remove(directory, error);
    }
    bool entered() const {
        std::error_code error;
        return std::filesystem::is_regular_file(directory / "plain-open", error);
    }
    unsigned remaining() const {
        std::ifstream marker(directory / "plain-open", std::ios::binary);
        unsigned value = 0;
        std::string extra;
        if (!(marker >> value) || (marker >> extra)) return 0;
        return value;
    }
};

void fallbackThenVerifiedObservation(const std::filesystem::path &helper) {
    StageMarker marker(helper, "observe-reconnect-");
    CHECK(marker.ready);
    if (!marker.ready) return;
    volatile sig_atomic_t stop = 0;
    PoolTlsRuntimeTransport transport(helper, request(marker.host), &stop, "stratum+tcp://" + marker.host + ":2");
    CHECK(transport.open(marker.host, 1) == PearlPoolConnectStatus::Connected);
    checkObservation(transport, Security::Plaintext, 2);
    CHECK(marker.entered() && transport.plaintextConnections() == 1);
    transport.close();
    checkObservation(transport, Security::Disconnected, 0);
    CHECK(transport.open(marker.host, 1) == PearlPoolConnectStatus::Connected);
    checkObservation(transport, Security::VerifiedTls, 1);
    CHECK(transport.plaintextConnections() == 1 && !transport.plaintextActive());
    transport.close();
}

void fallbackSharesOriginalBudget(const std::filesystem::path &helper) {
    StageMarker marker(helper, "budget-fallback-");
    CHECK(marker.ready);
    if (!marker.ready) return;
    volatile sig_atomic_t stop = 0;
    auto config = request(marker.host);
    config.totalMs = 6000;
    PoolTlsRuntimeTransport transport(helper, config, &stop, "stratum+tcp://" + marker.host + ":2");
    const auto began = std::chrono::steady_clock::now();
    const auto opened = transport.open(marker.host, 1);
    const auto elapsed = std::chrono::steady_clock::now() - began;
    CHECK(marker.entered()); // TLS used 3500 ms; a second real helper must receive only the remainder.
    CHECK(marker.remaining() >= 50 && marker.remaining() <= 2550);
    CHECK(opened != PearlPoolConnectStatus::Connected && transport.error() == PearlTlsError::Timeout);
    CHECK(!transport.isOpen() && !transport.plaintextActive() && transport.plaintextConnections() == 0);
    CHECK(elapsed < std::chrono::milliseconds(7500) && !nativeProcessUnresolved());
}

void cancelDuringPlaintextOpen(const std::filesystem::path &helper) {
    StageMarker marker(helper, "cancel-fallback-");
    CHECK(marker.ready);
    if (!marker.ready) return;
    signalStop = 0;
    const auto previous = std::signal(SIGINT, stopSignal);
    auto config = request(marker.host);
    config.totalMs = 8000;
    PoolTlsRuntimeTransport transport(helper, config, &signalStop, "stratum+tcp://" + marker.host + ":2");
    bool entered = false;
    std::chrono::steady_clock::time_point observed;
    std::thread stopper([&] {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        while (std::chrono::steady_clock::now() < limit) {
            if (marker.entered()) { entered = true; observed = std::chrono::steady_clock::now(); break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        // The timeout only cleans up a failed fixture; success requires observing the actual plaintext stage.
        std::raise(SIGINT);
    });
    const auto opened = transport.open(marker.host, 1);
    const auto completed = std::chrono::steady_clock::now();
    stopper.join();
    CHECK(entered && marker.entered() && marker.remaining() >= 50);
    CHECK(entered && completed >= observed && completed - observed < std::chrono::seconds(2));
    CHECK(opened != PearlPoolConnectStatus::Connected && transport.error() == PearlTlsError::Cancelled);
    CHECK(!transport.isOpen() && transport.plaintextConnections() == 0 && !nativeProcessUnresolved());
    signalStop = 0;
    CHECK(transport.open(marker.host, 1) != PearlPoolConnectStatus::Connected && transport.error() == PearlTlsError::Cancelled);
    std::signal(SIGINT, previous);
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const auto helper = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    plainSocketObservation();
    reconnect(helper);
    canonicalReconnect(helper);
    noReplayAfterUnknown(helper);
    refusals(helper);
    cancelOpen(helper);
    foreignCaller(helper);
    authorizedFallback(helper);
    fallbackRefusals(helper);
    noReplayOnFallback(helper);
    fallbackThenVerifiedObservation(helper);
    fallbackSharesOriginalBudget(helper);
    cancelDuringPlaintextOpen(helper);
    originalEpoch(helper);
    return finishTests("ordinary TLS runtime owner (synthetic peers, real IPC)");
}
