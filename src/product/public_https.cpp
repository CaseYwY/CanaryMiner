// Ordinary host C++, deliberately outside CUDA/protocol translation units.
#include "public_https_client.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <ctime>
#ifdef CANARY_HTTPS_TESTING
#include <cstdio>
#endif
#include <limits>
#include <tuple>
#include <utility>

#if defined(CANARY_PUBLIC_HTTPS)
#include "../core/platform.h"
#include <curl/curl.h>
#include <regex>
#if !defined(_WIN32)
#include <fcntl.h>
#endif
#endif

namespace om {

struct PublicJsonClient::Impl {
    bool usable = false;
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
#ifdef CANARY_HTTPS_TESTING
    unsigned port = 0;
    std::string path, caFile, dohPath;
    bool wrongName = false, permitLoopback = false;
#endif
};

#if defined(CANARY_PUBLIC_HTTPS)
namespace {
struct CurlLifetime {
    bool ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    ~CurlLifetime() { if (ok) curl_global_cleanup(); }
};
static bool curlReady() {
    static CurlLifetime lifetime;
    if (!lifetime.ok) return false;
    const auto *v = curl_version_info(CURLVERSION_NOW);
    if (!v || v->version_num < 0x080c00 || !(v->features & CURL_VERSION_SSL) || !(v->features & CURL_VERSION_ASYNCHDNS) ||
        !v->ares || !*v->ares || !v->ssl_version) return false;  // threaded DNS cannot meet the bounded-cleanup contract
#if defined(_WIN32)
    return std::string(v->ssl_version).find("Schannel") != std::string::npos;
#else
    return std::string(v->ssl_version).find("OpenSSL/") == 0;
#endif
}
constexpr char cloudflareDohUrl[] = "https://1.1.1.1/dns-query";
static bool dohTlsFloor() {
    const auto *version = curl_version_info(CURLVERSION_NOW);
    return version && version->version_num >= 0x081000;  // TLS 1.2 default since 8.16.0.
}
static bool cloudflareDohReady() {
    // libcurl's DoH subtransfer uses its own default TLS version and proxy options.
    // The installed parent starts this worker with a minimal, proxy-free environment.
    if (!dohTlsFloor()) return false;
    for (const auto *name : {"https_proxy", "HTTPS_PROXY", "all_proxy", "ALL_PROXY"}) {
#if defined(_WIN32)
        if (GetEnvironmentVariableA(name, nullptr, 0)) return false;
#else
        if (const auto *value = std::getenv(name); value && *value) return false;
#endif
    }
    return true;
}
static uint64_t utcNow() { return static_cast<uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())); }
static std::string trimLower(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n"), last = text.find_last_not_of(" \t\r\n");
    text = first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
    for (char &ch : text) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return text;
}
static bool decimalUnsigned(const std::string &s, uint64_t *out) {
    if (s.empty() || s.size() > 20) return false;
    uint64_t result = 0;
    for (char ch : s) {
        if (ch < '0' || ch > '9' || result > (UINT64_MAX - static_cast<unsigned>(ch - '0')) / 10) return false;
        result = result * 10 + static_cast<unsigned>(ch - '0');
    }
    *out = result; return true;
}

static bool utcParts(std::time_t time, std::tm &result) {
#if defined(_WIN32)
    return gmtime_s(&result, &time) == 0;
#else
    return gmtime_r(&time, &result) != nullptr;
#endif
}

static std::time_t httpDate(const std::string &value) {
    if (value.size() < 24 || value.size() > 40) return -1;
    // Accept the three HTTP-date layouts, excluding curl_getdate's other date and timezone patterns.
    static const std::regex layout(
        R"([a-z]{3}, [0-9]{2} [a-z]{3} [0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2} gmt|)"
        R"([a-z]{6,9}, [0-9]{2}-[a-z]{3}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} gmt|)"
        R"([a-z]{3} [a-z]{3} (?: [0-9]|[0-9]{2}) [0-9]{2}:[0-9]{2}:[0-9]{2} [0-9]{4})");
    if (!std::regex_match(value, layout)) return -1;
    const auto comma = value.find(',');
    const auto dayStart = comma == std::string::npos ? 8u : comma + 2;
    const auto day = (value[dayStart] == ' ' ? 0 : 10 * (value[dayStart] - '0')) + value[dayStart + 1] - '0';
    const auto parse = [day](const std::string &text) -> std::time_t {
        const auto parsed = curl_getdate(text.c_str(), nullptr);
        std::tm calendar{};
        // The date parser can normalize days beyond the end of a month.
        return parsed >= 0 && utcParts(parsed, calendar) && calendar.tm_mday == day ? parsed : -1;
    };
    const auto yearStart = value.rfind('-');
    std::string expanded = value;
    if (comma != std::string::npos) expanded.replace(expanded.size() - 3, 3, "GMT");
    if (yearStart == std::string::npos) return parse(expanded);

    // RFC 850 uses a rolling 50-year rule. libcurl's general date parser uses a fixed pivot.
    std::tm now{};
    if (!utcParts(std::time(nullptr), now)) return -1;
    const int lastFutureYear = now.tm_year + 1950;
    const int digits = 10 * (value[yearStart + 1] - '0') + value[yearStart + 2] - '0';
    int year = lastFutureYear / 100 * 100 + digits;
    if (year > lastFutureYear) year -= 100;
    expanded.replace(yearStart + 1, 2, std::to_string(year));
    auto parsed = parse(expanded);
    if (year == lastFutureYear) {
        std::tm later{};
        if (!utcParts(parsed, later) || parsed < 0) return -1;
        if (std::tie(later.tm_mon, later.tm_mday, later.tm_hour, later.tm_min, later.tm_sec) >
            std::tie(now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec)) {
            expanded.replace(yearStart + 1, 4, std::to_string(year - 100));
            parsed = parse(expanded);
        }
    }
    return parsed;
}

static unsigned retryDelay(const std::string &value, std::time_t serverTime) {
    constexpr unsigned cap = maxPublicRetryAfterSeconds;
    if (!value.empty() && std::all_of(value.begin(), value.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) {
        unsigned seconds = 0;
        for (char ch : value) {
            seconds = seconds * 10 + static_cast<unsigned>(ch - '0');
            if (seconds >= cap) return cap;
        }
        return seconds;
    }
    const auto until = httpDate(value);
    auto baseline = std::time(nullptr);
    // Clock skew or response transit must not shorten the provider's advertised wait.
    if (serverTime >= 0 && (baseline < 0 || serverTime < baseline)) baseline = serverTime;
    if (until < 0 || baseline < 0) return cap;
    return until <= baseline ? 0u : static_cast<unsigned>((std::min)(until - baseline, std::time_t{cap}));
}

struct Transfer {
    PublicJsonResponse response;
    PublicHttpsLimits limits;
    size_t headerBytes = 0;
    std::string media, encoding, contentLength;
    std::optional<std::string> retryAfter;
    std::time_t serverTime = -1;
    bool conflictingRetryAfter = false;
    bool permitLoopback = false;
    unsigned port = 443;
    bool failed() const { return response.status != PublicHttpsStatus::Ok; }
    bool fail(PublicHttpsStatus status) { response.status = status; return false; }
};
static bool chunkSize(size_t size, size_t count, size_t *bytes) {
    if (size && count > (std::numeric_limits<size_t>::max)() / size) return false;
    *bytes = size * count; return true;
}
static size_t writeBody(char *data, size_t size, size_t count, void *opaque) noexcept {
    auto &t = *static_cast<Transfer *>(opaque);
    try {
        size_t bytes = 0;
        if (!chunkSize(size, count, &bytes) || bytes > t.limits.bodyBytes - t.response.body.size()) { t.fail(PublicHttpsStatus::BodyTooLarge); return 0; }
        if (t.failed()) return 0;
        if (t.response.httpStatus != 200) { t.fail(PublicHttpsStatus::HttpStatus); return 0; }
        t.response.body.append(data, bytes);
        return bytes;
    } catch (...) { t.fail(PublicHttpsStatus::Unavailable); return 0; }
}
static size_t receiveHeader(char *data, size_t size, size_t count, void *opaque) noexcept {
    auto &t = *static_cast<Transfer *>(opaque);
    try {
        size_t bytes = 0;
        if (!chunkSize(size, count, &bytes) || bytes > t.limits.headerBytes - t.headerBytes) { t.fail(PublicHttpsStatus::InvalidHeaders); return 0; }
        t.headerBytes += bytes;
        if (t.failed()) return 0;
        const std::string line(data, bytes);
        if (bytes < 2 || line.substr(bytes - 2) != "\r\n") { t.fail(PublicHttpsStatus::InvalidHeaders); return 0; }
        for (size_t i = 0; i + 2 < bytes; ++i) if ((static_cast<unsigned char>(line[i]) < 32 && line[i] != '\t') || line[i] == 127) {
            t.fail(PublicHttpsStatus::InvalidHeaders); return 0;
        }
        if (line.rfind("HTTP/", 0) == 0) {
            const auto space = line.find(' ');
            uint64_t code = 0;
            if (space == std::string::npos || line.size() < space + 4 || !decimalUnsigned(line.substr(space + 1, 3), &code)) {
                t.fail(PublicHttpsStatus::InvalidHeaders); return 0;
            }
            t.response.httpStatus = static_cast<long>(code);
            t.media.clear(); t.encoding.clear(); t.contentLength.clear();
            t.retryAfter.reset();
            t.serverTime = -1;
            t.conflictingRetryAfter = false;
        } else if (line == "\r\n" || line == "\n") {
            if (t.response.httpStatus >= 200) {
                if (t.retryAfter)
                    t.response.retryAfterSeconds = t.conflictingRetryAfter ?
                        maxPublicRetryAfterSeconds : retryDelay(*t.retryAfter, t.serverTime);
                if (t.response.httpStatus != 200) { t.fail(PublicHttpsStatus::HttpStatus); return 0; }
                if (trimLower(t.media.substr(0, t.media.find(';'))) != "application/json" || (!t.encoding.empty() && t.encoding != "identity")) {
                    t.fail(PublicHttpsStatus::InvalidHeaders); return 0;
                }
            }
        } else {
            const auto colon = line.find(':');
            if (colon == std::string::npos || !colon || line[0] == ' ' || line[0] == '\t') { t.fail(PublicHttpsStatus::InvalidHeaders); return 0; }
            const auto name = trimLower(line.substr(0, colon)), value = trimLower(line.substr(colon + 1));
            // Only successful bodies are eligible for delivery. Read error headers through
            // the blank line so their representation metadata cannot discard a retry hint.
            if (t.response.httpStatus == 200) {
                auto unique = [&](std::string *field) { if (!field->empty() && *field != value) return false; *field = value; return true; };
                if ((name == "content-type" && !unique(&t.media)) || (name == "content-encoding" && !unique(&t.encoding)) ||
                    (name == "content-length" && !unique(&t.contentLength))) { t.fail(PublicHttpsStatus::InvalidHeaders); return 0; }
                if (name == "content-length") {
                    uint64_t length = 0;
                    if (!decimalUnsigned(value, &length)) { t.fail(PublicHttpsStatus::InvalidHeaders); return 0; }
                    if (length > t.limits.bodyBytes) { t.fail(PublicHttpsStatus::BodyTooLarge); return 0; }
                }
            }
            if (name == "retry-after") {
                if (!t.retryAfter) t.retryAfter = value;
                else if (*t.retryAfter != value) t.conflictingRetryAfter = true;
            }
            if (name == "date") {
                const auto date = httpDate(value);
                if (date >= 0 && (t.serverTime < 0 || date < t.serverTime)) t.serverTime = date;
            }
        }
        return bytes;
    } catch (...) { t.fail(PublicHttpsStatus::Unavailable); return 0; }
}
static curl_socket_t openPublicSocket(void *opaque, curlsocktype purpose, struct curl_sockaddr *address) noexcept {
    auto &t = *static_cast<Transfer *>(opaque);
    int kind = address->socktype;
#if !defined(_WIN32)
    // Recent curl passes atomic creation flags through this callback on Linux.
    kind &= ~(SOCK_CLOEXEC | SOCK_NONBLOCK);
#endif
    if (t.failed() || purpose != CURLSOCKTYPE_IPCXN || kind != SOCK_STREAM || address->protocol != IPPROTO_TCP) return CURL_SOCKET_BAD;
    bool allowed = false;
    if (address->family == AF_INET && address->addrlen >= sizeof(sockaddr_in)) {
        const auto &a = *reinterpret_cast<const sockaddr_in *>(&address->addr);
        const auto *b = reinterpret_cast<const uint8_t *>(&a.sin_addr);
        allowed = ntohs(a.sin_port) == t.port && (publicIpv4(b) || (t.permitLoopback && b[0] == 127 && b[1] == 0 && b[2] == 0 && b[3] == 1));
    } else if (address->family == AF_INET6 && address->addrlen >= sizeof(sockaddr_in6)) {
        const auto &a = *reinterpret_cast<const sockaddr_in6 *>(&address->addr);
        allowed = ntohs(a.sin6_port) == t.port && a.sin6_scope_id == 0 && publicIpv6(reinterpret_cast<const uint8_t *>(&a.sin6_addr));
    }
    if (!allowed) { t.fail(PublicHttpsStatus::DestinationBlocked); return CURL_SOCKET_BAD; }
#if defined(_WIN32)
    return WSASocketW(address->family, address->socktype, address->protocol, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
#else
    return ::socket(address->family, address->socktype | SOCK_CLOEXEC, address->protocol);
#endif
}
struct EasyDelete { void operator()(CURL *value) const { if (value) curl_easy_cleanup(value); } };
struct MultiDelete { void operator()(CURLM *value) const { if (value) curl_multi_cleanup(value); } };
struct ListDelete { void operator()(curl_slist *value) const { if (value) curl_slist_free_all(value); } };
}  // namespace
#endif

PublicJsonClient::PublicJsonClient() : impl_(new Impl()) {
#if defined(CANARY_PUBLIC_HTTPS)
    impl_->usable = curlReady();
#endif
}
PublicJsonClient::~PublicJsonClient() = default;
bool PublicJsonClient::available() const { return impl_->usable; }

PublicJsonResponse PublicJsonClient::get(const PublicJsonRequest &request, const std::atomic<bool> &cancel, PublicHttpsLimits limits) {
    if (cancel.load()) { PublicJsonResponse r; r.status = PublicHttpsStatus::Cancelled; return r; }
    if (impl_->busy.test_and_set()) { PublicJsonResponse r; r.status = PublicHttpsStatus::Busy; return r; }
    struct Busy { Impl *value; ~Busy() { value->busy.clear(); } } busy{impl_.get()};
    PublicJsonResponse response;
    std::string url;
    if (!publicFeedUrl(request, &url) || (!limits.dnsServer.empty() && !validPublicDnsServer(limits.dnsServer)) ||
        limits.totalMs < 50 || limits.totalMs > 60000 || !limits.connectMs || limits.connectMs > limits.totalMs ||
        !limits.bodyBytes || limits.bodyBytes > 1024 * 1024 || limits.headerBytes < 64 || limits.headerBytes > 32 * 1024) {
        response.status = PublicHttpsStatus::InvalidRequest; return response;
    }
    if (!available()) return response;
#if defined(CANARY_PUBLIC_HTTPS)
    try {
        const bool dnsOverHttps = limits.dnsServer == publicCloudflareDoh;
        if (dnsOverHttps && !cloudflareDohReady()) return response;
        // Numeric bootstrap prevents the deployment resolver from choosing the DoH peer.
        const char *dohUrl = cloudflareDohUrl;
#ifdef CANARY_HTTPS_TESTING
        std::string fixtureDohUrl;
#endif
        Transfer t;
        t.response.status = PublicHttpsStatus::Ok; t.limits = limits;
        std::unique_ptr<curl_slist, ListDelete> headers, resolve, connectTo;
        std::unique_ptr<CURLM, MultiDelete> multi(curl_multi_init());
        std::unique_ptr<CURL, EasyDelete> easy(curl_easy_init());
        if (!easy || !multi) return response;
        auto append = [](std::unique_ptr<curl_slist, ListDelete> &list, const char *text) {
            auto *value = curl_slist_append(list.get(), text);
            if (!value) return false;
            list.release(); list.reset(value); return true;
        };
        if (!append(headers, "Accept: application/json") || !append(headers, "Accept-Encoding: identity")) return response;
#ifdef CANARY_HTTPS_TESTING
        if (impl_->port) {
            t.port = impl_->port; t.permitLoopback = impl_->permitLoopback;
            const std::string name = impl_->wrongName ? "wrong.test.invalid" : "fixture.test.invalid";
            url = "https://" + name + ":" + std::to_string(impl_->port) + impl_->path;
            if (dnsOverHttps) {
                if (impl_->dohPath.empty()) return response;
                fixtureDohUrl = "https://127.0.0.1:" + std::to_string(impl_->port) + impl_->dohPath;
                dohUrl = fixtureDohUrl.c_str();
            } else if (!append(resolve, (name + ":" + std::to_string(impl_->port) + ":127.0.0.1").c_str())) return response;
        }
#endif
        auto set = [&](CURLoption option, auto value) { return curl_easy_setopt(easy.get(), option, value) == CURLE_OK; };
#ifdef CANARY_HTTPS_TESTING
        char diagnostic[CURL_ERROR_SIZE] = {};
        if (!set(CURLOPT_ERRORBUFFER, diagnostic)) return response;
#endif
        if (!set(CURLOPT_URL, url.c_str()) || !set(CURLOPT_PROTOCOLS_STR, "https") || !set(CURLOPT_REDIR_PROTOCOLS_STR, "https") ||
            !set(CURLOPT_HTTPGET, 1L) || !set(CURLOPT_FOLLOWLOCATION, 0L) || !set(CURLOPT_MAXREDIRS, 0L) ||
            !set(CURLOPT_PROXY, "") || !set(CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED)) || !set(CURLOPT_UNRESTRICTED_AUTH, 0L) ||
            !set(CURLOPT_SSL_VERIFYPEER, 1L) || !set(CURLOPT_SSL_VERIFYHOST, 2L) || !set(CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2)) ||
            !set(CURLOPT_SSL_OPTIONS, 0L) || !set(CURLOPT_NOSIGNAL, 1L) || !set(CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(limits.connectMs)) ||
            !set(CURLOPT_TIMEOUT_MS, static_cast<long>(limits.totalMs)) || !set(CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1)) ||
            !set(CURLOPT_HTTPHEADER, headers.get()) || !set(CURLOPT_HTTP_CONTENT_DECODING, 0L) ||
            !set(CURLOPT_USERAGENT, "CanaryMiner-PublicData/1") || !set(CURLOPT_FORBID_REUSE, 1L) ||
            !set(CURLOPT_WRITEFUNCTION, &writeBody) || !set(CURLOPT_WRITEDATA, &t) ||
            !set(CURLOPT_HEADERFUNCTION, &receiveHeader) || !set(CURLOPT_HEADERDATA, &t) ||
            !set(CURLOPT_OPENSOCKETFUNCTION, &openPublicSocket) || !set(CURLOPT_OPENSOCKETDATA, &t) ||
            !set(CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(limits.bodyBytes))) return response;
#ifdef CANARY_HTTPS_TESTING
        if (impl_->port && (!set(CURLOPT_RESOLVE, resolve.get()) || !set(CURLOPT_CAINFO, impl_->caFile.c_str()))) return response;
#endif
        if (dnsOverHttps) {
            if (!set(CURLOPT_DOH_URL, dohUrl) || !set(CURLOPT_DOH_SSL_VERIFYPEER, 1L) ||
                !set(CURLOPT_DOH_SSL_VERIFYHOST, 2L)) return response;
        } else if (!limits.dnsServer.empty()) {
            const auto resolver = limits.dnsServer.find(':') == std::string::npos
                ? limits.dnsServer + ":53" : "[" + limits.dnsServer + "]:53";
            constexpr std::string_view httpsScheme = "https://";
            const auto host = url.substr(httpsScheme.size(), url.find_first_of(":/", httpsScheme.size()) - httpsScheme.size());
            // Resolve the absolute provider name so deployment search suffixes never reach the selected server.
            // CONNECT_TO leaves the URL's authority in use for HTTP and TLS.
            const auto route = host + "::" + host + ".:";
            if (!append(connectTo, route.c_str()) || !set(CURLOPT_CONNECT_TO, connectTo.get()) ||
                !set(CURLOPT_DNS_SERVERS, resolver.c_str())) return response;
        }
        if (curl_multi_setopt(multi.get(), CURLMOPT_MAX_TOTAL_CONNECTIONS, 2L) != CURLM_OK ||
            curl_multi_setopt(multi.get(), CURLMOPT_MAXCONNECTS, 2L) != CURLM_OK) return response;
        if (curl_multi_add_handle(multi.get(), easy.get()) != CURLM_OK) return response;
        struct Detach { CURLM *multi; CURL *easy; ~Detach() { curl_multi_remove_handle(multi, easy); } } detach{multi.get(), easy.get()};
        const uint64_t startedUtc = utcNow();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limits.totalMs);
        CURLcode code = CURLE_FAILED_INIT;
        for (;;) {
            if (cancel.load()) { t.response.status = PublicHttpsStatus::Cancelled; break; }
            if (std::chrono::steady_clock::now() >= deadline) { t.response.status = PublicHttpsStatus::Timeout; break; }
            int active = 0;
            if (curl_multi_perform(multi.get(), &active) != CURLM_OK) { t.response.status = PublicHttpsStatus::Network; break; }
            int queued = 0;
            bool done = false;
            while (auto *message = curl_multi_info_read(multi.get(), &queued)) if (message->msg == CURLMSG_DONE) { code = message->data.result; done = true; }
            if (done) break;
            int descriptors = 0;
            if (curl_multi_poll(multi.get(), nullptr, 0, 50, &descriptors) != CURLM_OK) { t.response.status = PublicHttpsStatus::Network; break; }
        }
        if (cancel.load()) t.response.status = PublicHttpsStatus::Cancelled;
        if (t.response.status == PublicHttpsStatus::Ok && code != CURLE_OK) {
            switch (code) {
                case CURLE_OPERATION_TIMEDOUT: t.response.status = PublicHttpsStatus::Timeout; break;
                case CURLE_PEER_FAILED_VERIFICATION: case CURLE_SSL_CACERT_BADFILE: case CURLE_SSL_CRL_BADFILE:
                case CURLE_SSL_ISSUER_ERROR: case CURLE_SSL_CERTPROBLEM: t.response.status = PublicHttpsStatus::TlsVerification; break;
                case CURLE_SSL_CONNECT_ERROR: t.response.status = PublicHttpsStatus::TlsProtocol; break;
                case CURLE_FILESIZE_EXCEEDED: t.response.status = PublicHttpsStatus::BodyTooLarge; break;
                default: t.response.status = PublicHttpsStatus::Network; break;
            }
        }
        long http = 0;
        if (curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &http) == CURLE_OK && http) t.response.httpStatus = http;
        if (t.response.status == PublicHttpsStatus::Ok && t.response.httpStatus != 200) t.response.status = PublicHttpsStatus::HttpStatus;
        if (t.response.status == PublicHttpsStatus::Ok && !boundedPublicJson(t.response.body)) t.response.status = PublicHttpsStatus::InvalidJson;
        if (cancel.load()) t.response.status = PublicHttpsStatus::Cancelled;
        else if (std::chrono::steady_clock::now() >= deadline) t.response.status = PublicHttpsStatus::Timeout;
        t.response.observedUtc = utcNow();
        if (t.response.status == PublicHttpsStatus::Ok && (!startedUtc || t.response.observedUtc < startedUtc || t.response.observedUtc > 253402300799ULL))
            t.response.status = PublicHttpsStatus::InvalidObservationTime;
        if (t.response.status != PublicHttpsStatus::Ok) { t.response.body.clear(); t.response.observedUtc = 0; }
#ifdef CANARY_HTTPS_TESTING
        if (diagnostic[0]) std::fprintf(stderr, "fixture TLS diagnostic: %s\n", diagnostic);
#endif
        return std::move(t.response);
    } catch (...) { response.status = PublicHttpsStatus::Unavailable; return response; }
#else
    return response;
#endif
}

#ifdef CANARY_HTTPS_TESTING
bool PublicHttpsTestPeer::hasDohBackend() {
#if defined(CANARY_PUBLIC_HTTPS)
    if (!curlReady() || !dohTlsFloor()) return false;
    std::unique_ptr<CURL, EasyDelete> easy(curl_easy_init());
    return easy && curl_easy_setopt(easy.get(), CURLOPT_DOH_URL, cloudflareDohUrl) == CURLE_OK;
#else
    return false;
#endif
}

std::unique_ptr<PublicJsonClient> PublicHttpsTestPeer::create(unsigned port, std::string path, std::string caFile,
                                                           bool wrongName, bool permitLoopback, std::string dohPath) {
    std::unique_ptr<PublicJsonClient> client(new PublicJsonClient());
    if (!port || port > 65535 || path.empty() || path[0] != '/' || path.size() > 128 || path.find_first_not_of("/abcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos) {
        client->impl_->usable = false; return client;
    }
    client->impl_->port = port; client->impl_->path = std::move(path); client->impl_->caFile = std::move(caFile);
    client->impl_->wrongName = wrongName; client->impl_->permitLoopback = permitLoopback;
    client->impl_->dohPath = std::move(dohPath);
    return client;
}
#endif

}  // namespace om
