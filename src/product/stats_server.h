// Loopback-only, read-only HTTP endpoint:  GET /api/v1/stats  and  GET /api/v1/version.
//
// There are no write routes: every other method gets 405 (Allow: GET), every other path 404. The listener refuses to bind
// anything but a loopback address, requests larger than 8 KiB get 431, and a Host header that is not a loopback name gets 403
// (DNS-rebinding guard). One connection is served at a time with a 2 s I/O timeout, so a stalled client cannot hold the thread.
// handleStatsRequest() is pure and unit-tested; StatsServer / httpGetLocal add the sockets (loopback tests only).

#pragma once

#include <stdint.h>
#include <stdlib.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "../core/platform.h"

#if !defined(_WIN32)
#include <sys/select.h>
#endif

namespace om {

struct HttpReply {
    int code = 200;
    std::string contentType = "application/json";
    std::string body;
    std::string allow;  // set for 405
};

inline const char *httpReason(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 431: return "Request Header Fields Too Large";
        default: return "Error";
    }
}

inline std::string httpWire(const HttpReply &r) {
    std::string h = "HTTP/1.1 " + std::to_string(r.code) + " " + httpReason(r.code) + "\r\nContent-Type: " + r.contentType +
                    "\r\nContent-Length: " + std::to_string(r.body.size()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n";
    if (!r.allow.empty()) h += "Allow: " + r.allow + "\r\n";
    return h + "\r\n" + r.body;
}

inline bool loopbackHostHeader(std::string host) {
    while (!host.empty() && (host.front() == ' ' || host.front() == '\t')) host.erase(host.begin());
    while (!host.empty() && (host.back() == ' ' || host.back() == '\t' || host.back() == '\r')) host.pop_back();
    std::string name = host;
    if (!name.empty() && name[0] == '[') {
        const size_t e = name.find(']');
        name = e == std::string::npos ? name : name.substr(0, e + 1);
    } else {
        const size_t c = name.rfind(':');
        if (c != std::string::npos) name = name.substr(0, c);
    }
    for (char &c : name) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return name == "127.0.0.1" || name == "localhost" || name == "[::1]";
}

/** Route one complete request head. `statsJson` is called only for /api/v1/stats. */
inline HttpReply handleStatsRequest(const std::string &head, const std::function<std::string()> &statsJson, const std::string &versionJson) {
    HttpReply r;
    auto err = [&](int code, const char *msg) {
        r.code = code;
        r.body = std::string("{\"error\":\"") + msg + "\"}";
        return r;
    };
    if (head.size() > 8192) return err(431, "request too large");
    const size_t eol = head.find("\r\n");
    const std::string line = head.substr(0, eol == std::string::npos ? head.size() : eol);
    const size_t s1 = line.find(' ');
    const size_t s2 = s1 == std::string::npos ? std::string::npos : line.find(' ', s1 + 1);
    if (s1 == std::string::npos || s2 == std::string::npos) return err(400, "malformed request line");
    const std::string method = line.substr(0, s1), target = line.substr(s1 + 1, s2 - s1 - 1), proto = line.substr(s2 + 1);
    if (proto.compare(0, 5, "HTTP/") != 0) return err(400, "malformed request line");
    // header scan: Host must be a loopback name when present
    size_t pos = eol == std::string::npos ? head.size() : eol + 2;
    while (pos < head.size()) {
        const size_t e = head.find("\r\n", pos);
        const std::string h = head.substr(pos, (e == std::string::npos ? head.size() : e) - pos);
        if (h.size() >= 5) {
            std::string lower = h.substr(0, 5);
            for (char &c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            if (lower == "host:" && !loopbackHostHeader(h.substr(5))) return err(403, "loopback host required");
        }
        if (e == std::string::npos) break;
        pos = e + 2;
    }
    if (method != "GET") {
        err(405, "read-only endpoint");
        r.allow = "GET";
        return r;
    }
    const size_t q = target.find('?');
    const std::string path = q == std::string::npos ? target : target.substr(0, q);
    if (path == "/api/v1/stats") { r.body = statsJson ? statsJson() : "{}"; return r; }
    if (path == "/api/v1/version") { r.body = versionJson; return r; }
    return err(404, "not found");
}

inline bool isLoopbackBind(const std::string &bind) { return bind == "127.0.0.1" || bind == "::1" || bind == "localhost"; }

class StatsServer {
   public:
    ~StatsServer() { stop(); }

    /** Bind and listen (port 0 = ephemeral, for tests). Returns false with a reason; never binds a non-loopback address. */
    bool start(const std::string &bind, int port, std::function<std::string()> statsJson, std::string versionJson, std::string *error) {
        if (!isLoopbackBind(bind)) { if (error) *error = "stats endpoint refuses to bind '" + bind + "': loopback only"; return false; }
        statsJson_ = std::move(statsJson);
        versionJson_ = std::move(versionJson);
        const bool v6 = bind == "::1";
        fd_ = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
        if (fd_ == OM_INVALID_SOCKET) { if (error) *error = "socket() failed"; return false; }
        int one = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof(one));
        bool bound = false;
        if (v6) {
            sockaddr_in6 a{};
            a.sin6_family = AF_INET6;
            a.sin6_addr = in6addr_loopback;
            a.sin6_port = htons(static_cast<uint16_t>(port));
            bound = ::bind(fd_, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
        } else {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons(static_cast<uint16_t>(port));
            bound = ::bind(fd_, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
        }
        if (!bound || ::listen(fd_, 8) != 0) {
            if (error) *error = "cannot listen on " + bind + ":" + std::to_string(port) + " (is another miner running?)";
            OM_CLOSESOCKET(fd_);
            fd_ = OM_INVALID_SOCKET;
            return false;
        }
        if (port == 0) {
            sockaddr_storage ss{};
            socklen_t len = sizeof(ss);
            if (getsockname(fd_, reinterpret_cast<sockaddr *>(&ss), &len) == 0)
                port_ = ntohs(ss.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6 *>(&ss)->sin6_port : reinterpret_cast<sockaddr_in *>(&ss)->sin_port);
        } else {
            port_ = port;
        }
        stop_ = false;
        thread_ = std::thread([this] { loop(); });
        return true;
    }

    int port() const { return port_; }

    void stop() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
        if (fd_ != OM_INVALID_SOCKET) { OM_CLOSESOCKET(fd_); fd_ = OM_INVALID_SOCKET; }
    }

   private:
    socket_t fd_ = OM_INVALID_SOCKET;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::function<std::string()> statsJson_;
    std::string versionJson_;

    void loop() {
        while (!stop_) {
            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(fd_, &rd);
            timeval tv{0, 200000};
            const int n = ::select(static_cast<int>(fd_) + 1, &rd, nullptr, nullptr, &tv);
            if (n <= 0) continue;
            socket_t c = ::accept(fd_, nullptr, nullptr);
            if (c == OM_INVALID_SOCKET) continue;
            socketTimeout(c, 2);
            std::string head;
            char buf[2048];
            while (head.size() <= 8192 && head.find("\r\n\r\n") == std::string::npos) {
                const int got = socketRecv(c, buf, sizeof(buf));
                if (got <= 0) break;
                head.append(buf, static_cast<size_t>(got));
            }
            if (head.find("\r\n\r\n") != std::string::npos || head.size() > 8192) {
                const std::string wire = httpWire(handleStatsRequest(head, statsJson_, versionJson_));
                socketSend(c, wire.data(), wire.size());
            }
            OM_CLOSESOCKET(c);
        }
    }
};

/** Minimal GET against a loopback listener (the `status` command and the tests). */
inline bool httpGetLocal(const std::string &host, int port, const std::string &path, int *status, std::string *body, std::string *error,
                         int timeoutSeconds = 3) {
    const bool v6 = host == "::1";
    socket_t c = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (c == OM_INVALID_SOCKET) { if (error) *error = "socket() failed"; return false; }
    socketTimeout(c, timeoutSeconds);
    bool ok;
    if (v6) {
        sockaddr_in6 a{};
        a.sin6_family = AF_INET6;
        a.sin6_addr = in6addr_loopback;
        a.sin6_port = htons(static_cast<uint16_t>(port));
        ok = ::connect(c, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
    } else {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(static_cast<uint16_t>(port));
        ok = ::connect(c, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
    }
    if (!ok) { OM_CLOSESOCKET(c); if (error) *error = "no miner is listening on " + host + ":" + std::to_string(port); return false; }
    const std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + (v6 ? std::string("[::1]") : std::string("127.0.0.1")) + ":" +
                            std::to_string(port) + "\r\nConnection: close\r\n\r\n";
    if (socketSend(c, req.data(), req.size()) < 0) { OM_CLOSESOCKET(c); if (error) *error = "send failed"; return false; }
    std::string resp;
    char buf[4096];
    int got;
    while ((got = socketRecv(c, buf, sizeof(buf))) > 0 && resp.size() < (4u << 20)) resp.append(buf, static_cast<size_t>(got));
    OM_CLOSESOCKET(c);
    if (resp.compare(0, 9, "HTTP/1.1 ") != 0 || resp.size() < 12) { if (error) *error = "not an HTTP reply"; return false; }
    *status = atoi(resp.c_str() + 9);
    const size_t split = resp.find("\r\n\r\n");
    *body = split == std::string::npos ? std::string() : resp.substr(split + 4);
    return true;
}

}  // namespace om
