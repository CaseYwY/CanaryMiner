// Ordinary host C++, deliberately outside CUDA/protocol translation units.
#include "public_https.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#ifdef CANARY_HTTPS_TESTING
#include <cstdio>
#endif
#include <limits>
#include <utility>

#include "json_dom.h"

#if defined(CANARY_PUBLIC_HTTPS)
#include "../core/platform.h"
#include <curl/curl.h>
#if !defined(_WIN32)
#include <fcntl.h>
#endif
#endif

namespace om {

const char *publicHttpsStatusName(PublicHttpsStatus status) {
    switch (status) {
        case PublicHttpsStatus::Ok: return "ok";
        case PublicHttpsStatus::Unavailable: return "verified HTTPS/c-ares backend unavailable";
        case PublicHttpsStatus::InvalidRequest: return "invalid public request or bounds";
        case PublicHttpsStatus::Busy: return "public client already has an owner";
        case PublicHttpsStatus::Cancelled: return "cancelled";
        case PublicHttpsStatus::Timeout: return "public request deadline exceeded";
        case PublicHttpsStatus::DestinationBlocked: return "non-public destination refused";
        case PublicHttpsStatus::TlsVerification: return "TLS certificate/name/trust verification failed";
        case PublicHttpsStatus::TlsProtocol: return "TLS handshake/protocol failed";
        case PublicHttpsStatus::Network: return "public request network failure";
        case PublicHttpsStatus::HttpStatus: return "public provider HTTP error";
        case PublicHttpsStatus::InvalidHeaders: return "invalid/unsupported public response headers";
        case PublicHttpsStatus::BodyTooLarge: return "public response body limit exceeded";
        case PublicHttpsStatus::InvalidJson: return "invalid/over-budget public JSON";
    }
    return "unsupported public response";
}

bool publicFeedUrl(const PublicJsonRequest &r, std::string *url) {
    if (!url) return false;
    std::string result;
    switch (r.feed) {
        case PublicFeed::GatePearlTicker:
            if (r.fromUtc || r.toUtc) return false;
            result = "https://api.gateio.ws/api/v4/spot/tickers?currency_pair=PEARL_USDT"; break;
        case PublicFeed::KrakenUsdtTicker:
            if (r.fromUtc || r.toUtc) return false;
            result = "https://api.kraken.com/0/public/Ticker?pair=USDTUSD"; break;
        case PublicFeed::GatePearlCandles:
        case PublicFeed::KrakenUsdtCandles: {
            const int64_t maximum = r.feed == PublicFeed::GatePearlCandles ? 900 * 60 : 719 * 60;
            if (r.fromUtc < 60 || r.toUtc <= r.fromUtc || r.toUtc > 253402300799LL ||
                r.fromUtc % 60 || r.toUtc % 60 || r.toUtc - r.fromUtc > maximum) return false;
            if (r.feed == PublicFeed::GatePearlCandles)
                result = "https://api.gateio.ws/api/v4/spot/candlesticks?currency_pair=PEARL_USDT&interval=1m&from=" +
                    std::to_string(r.fromUtc - 60) + "&to=" + std::to_string(r.toUtc);
            else result = "https://api.kraken.com/0/public/OHLC?pair=USDTUSD&interval=1&since=" + std::to_string(r.fromUtc);
            break;
        }
        default: return false;
    }
    *url = std::move(result);
    return true;
}

bool publicIpv4(const uint8_t *b) {
    if (!b || b[0] == 0 || b[0] == 10 || b[0] == 127 || b[0] >= 224 ||
        (b[0] == 100 && b[1] >= 64 && b[1] <= 127) || (b[0] == 169 && b[1] == 254) ||
        (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168) ||
        (b[0] == 192 && b[1] == 0 && (b[2] == 0 || b[2] == 2)) ||
        (b[0] == 192 && b[1] == 88 && b[2] == 99) || (b[0] == 198 && (b[1] == 18 || b[1] == 19)) ||
        (b[0] == 198 && b[1] == 51 && b[2] == 100) || (b[0] == 203 && b[1] == 0 && b[2] == 113)) return false;
    return true;
}
bool publicIpv6(const uint8_t *b) {
    if (!b || (b[0] & 0xe0) != 0x20) return false;  // global unicast only; no mapped/compatible IPv4, local or multicast
    if (b[0] == 0x20 && b[1] == 1 && (b[2] & 0xfe) == 0) return false;  // conservative 2001::/23 special-use exclusion
    if (b[0] == 0x20 && b[1] == 1 && b[2] == 0x0d && b[3] == 0xb8) return false;
    if (b[0] == 0x20 && b[1] == 2) return false;  // 6to4 embeds another endpoint
    if (b[0] == 0x3f && b[1] == 0xff && (b[2] & 0xf0) == 0) return false;  // documentation 3fff::/20
    return true;
}

static bool validUtf8(const std::string &s) {
    for (size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i++]);
        if (b < 0x80) continue;
        unsigned count = b >= 0xc2 && b <= 0xdf ? 1 : b >= 0xe0 && b <= 0xef ? 2 : b >= 0xf0 && b <= 0xf4 ? 3 : 0;
        if (!count || s.size() - i < count) return false;
        const auto second = static_cast<unsigned char>(s[i]);
        if ((b == 0xe0 && second < 0xa0) || (b == 0xed && second >= 0xa0) ||
            (b == 0xf0 && second < 0x90) || (b == 0xf4 && second >= 0x90)) return false;
        while (count--) if ((static_cast<unsigned char>(s[i++]) & 0xc0) != 0x80) return false;
    }
    return true;
}
bool boundedPublicJson(const std::string &text) {
    if (text.empty() || text.size() > 1024 * 1024 || !validUtf8(text)) return false;
    size_t nodes = 0, depth = 0, stringBytes = 0, literalBytes = 0;
    size_t members[17] = {};
    bool quoted = false, escaped = false;
    for (unsigned char ch : text) {
        if (quoted) {
            if (++stringBytes > 4096) return false;
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
            continue;
        }
        if (ch == '"') { quoted = true; stringBytes = 0; ++nodes; literalBytes = 0; }
        else if (ch == '{' || ch == '[') { if (++depth > 16) return false; members[depth] = 0; ++nodes; literalBytes = 0; }
        else if (ch == '}' || ch == ']') { if (!depth) return false; --depth; literalBytes = 0; }
        else if (ch == ':') { if (++members[depth] > 256) return false; literalBytes = 0; }
        else if (ch == ',' || ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t') literalBytes = 0;
        else { if (!literalBytes) ++nodes; if (++literalBytes > 128) return false; }
        if (nodes > 20000) return false;
    }
    if (quoted || depth) return false;
    JsonValue value;
    return parseJsonStrict(text, &value, nullptr);  // never echo provider keys/payload as a diagnostic
}

struct PublicJsonClient::Impl {
    bool usable = false;
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
#ifdef CANARY_HTTPS_TESTING
    unsigned port = 0;
    std::string path, caFile;
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
struct Transfer {
    PublicJsonResponse response;
    PublicHttpsLimits limits;
    size_t headerBytes = 0;
    std::string media, encoding, contentLength;
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
        } else if (line == "\r\n" || line == "\n") {
            if (t.response.httpStatus >= 200) {
                if (t.response.httpStatus != 200) { t.fail(PublicHttpsStatus::HttpStatus); return 0; }
                if (trimLower(t.media.substr(0, t.media.find(';'))) != "application/json" || (!t.encoding.empty() && t.encoding != "identity")) {
                    t.fail(PublicHttpsStatus::InvalidHeaders); return 0;
                }
            }
        } else {
            const auto colon = line.find(':');
            if (colon == std::string::npos || !colon || line[0] == ' ' || line[0] == '\t') { t.fail(PublicHttpsStatus::InvalidHeaders); return 0; }
            const auto name = trimLower(line.substr(0, colon)), value = trimLower(line.substr(colon + 1));
            auto unique = [&](std::string *field) { if (!field->empty() && *field != value) return false; *field = value; return true; };
            if ((name == "content-type" && !unique(&t.media)) || (name == "content-encoding" && !unique(&t.encoding)) ||
                (name == "content-length" && !unique(&t.contentLength))) { t.fail(PublicHttpsStatus::InvalidHeaders); return 0; }
            if (name == "content-length") {
                uint64_t length = 0;
                if (!decimalUnsigned(value, &length)) { t.fail(PublicHttpsStatus::InvalidHeaders); return 0; }
                if (length > t.limits.bodyBytes) { t.fail(PublicHttpsStatus::BodyTooLarge); return 0; }
            }
            if (name == "retry-after") { uint64_t seconds = 0; if (decimalUnsigned(value, &seconds)) t.response.retryAfterSeconds = static_cast<unsigned>(std::min<uint64_t>(seconds, 3600)); }
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
    if (!publicFeedUrl(request, &url) || limits.totalMs < 50 || limits.totalMs > 60000 || !limits.connectMs || limits.connectMs > limits.totalMs ||
        !limits.bodyBytes || limits.bodyBytes > 1024 * 1024 || limits.headerBytes < 64 || limits.headerBytes > 32 * 1024) {
        response.status = PublicHttpsStatus::InvalidRequest; return response;
    }
    if (!available()) return response;
#if defined(CANARY_PUBLIC_HTTPS)
    try {
        Transfer t;
        t.response.status = PublicHttpsStatus::Ok; t.limits = limits;
        std::unique_ptr<curl_slist, ListDelete> headers, resolve;
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
            if (!append(resolve, (name + ":" + std::to_string(impl_->port) + ":127.0.0.1").c_str())) return response;
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
            t.response.status = PublicHttpsStatus::InvalidJson;
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
std::unique_ptr<PublicJsonClient> PublicHttpsTestPeer::create(unsigned port, std::string path, std::string caFile, bool wrongName, bool permitLoopback) {
    std::unique_ptr<PublicJsonClient> client(new PublicJsonClient());
    if (!port || port > 65535 || path.empty() || path[0] != '/' || path.size() > 128 || path.find_first_not_of("/abcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos) {
        client->impl_->usable = false; return client;
    }
    client->impl_->port = port; client->impl_->path = std::move(path); client->impl_->caFile = std::move(caFile);
    client->impl_->wrongName = wrongName; client->impl_->permitLoopback = permitLoopback;
    return client;
}
#endif

}  // namespace om
