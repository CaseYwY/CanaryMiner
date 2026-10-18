// Injectable byte transport for Pearl pool protocol tests.
//
// Framing deliberately remains in PearlPoolSource: a fake transport feeds the
// same fragmented byte stream as a real socket, so tests cannot accidentally
// bypass pending-buffer and correlation behavior.

#pragma once

#include <cstddef>
#include <limits>
#include <string>

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#endif

#include "platform.h"

namespace om {

enum class PearlPoolConnectStatus {
    Connected,
    DnsFailure,
    SocketFailure,
    ConnectFailure,
};

enum class PearlPoolWaitStatus { Ready, Timeout, Error };
enum class PearlPoolTransferStatus { Bytes, WouldBlock, EndOfStream, Error };

struct PearlPoolTransfer {
    PearlPoolTransferStatus status = PearlPoolTransferStatus::Error;
    std::size_t bytes = 0;
};

class PearlPoolTransport {
   public:
    virtual ~PearlPoolTransport() = default;
    virtual PearlPoolConnectStatus open(const std::string &host, int port) = 0;
    virtual bool isOpen() const = 0;
    virtual void close() = 0;
    virtual PearlPoolWaitStatus waitReadable(int milliseconds) = 0;
    virtual PearlPoolWaitStatus waitWritable(int milliseconds) = 0;
    virtual PearlPoolTransfer receive(char *out, std::size_t maximum) = 0;
    virtual PearlPoolTransfer send(const char *data, std::size_t length) = 0;
    virtual bool setNonblocking(bool enabled) = 0;
};

class PearlPoolSocketTransport final : public PearlPoolTransport {
   public:
    ~PearlPoolSocketTransport() override { close(); }

    PearlPoolConnectStatus open(const std::string &host, int port) override {
        close();
        struct addrinfo hints {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo *res = nullptr;
        const std::string portText = std::to_string(port);
        if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &res) != 0)
            return PearlPoolConnectStatus::DnsFailure;
        fd_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd_ == OM_INVALID_SOCKET) {
            freeaddrinfo(res);
            return PearlPoolConnectStatus::SocketFailure;
        }
        socketTimeout(fd_, 20);
        if (::connect(fd_, res->ai_addr, (int)res->ai_addrlen) != 0) {
            OM_CLOSESOCKET(fd_);
            fd_ = OM_INVALID_SOCKET;
            freeaddrinfo(res);
            return PearlPoolConnectStatus::ConnectFailure;
        }
        freeaddrinfo(res);
        return PearlPoolConnectStatus::Connected;
    }

    bool isOpen() const override { return fd_ != OM_INVALID_SOCKET; }

    void close() override {
        if (fd_ != OM_INVALID_SOCKET) OM_CLOSESOCKET(fd_);
        fd_ = OM_INVALID_SOCKET;
        savedFlagsValid_ = false;
    }

    PearlPoolWaitStatus waitReadable(int milliseconds) override {
        if (fd_ == OM_INVALID_SOCKET) return PearlPoolWaitStatus::Error;
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(fd_, &readSet);
        struct timeval timeout { milliseconds / 1000,
                                 (milliseconds % 1000) * 1000 };
        const int result =
            select((int)fd_ + 1, &readSet, nullptr, nullptr, &timeout);
        return result > 0 ? PearlPoolWaitStatus::Ready
                          : result == 0 ? PearlPoolWaitStatus::Timeout
                                        : PearlPoolWaitStatus::Error;
    }

    PearlPoolWaitStatus waitWritable(int milliseconds) override {
        if (fd_ == OM_INVALID_SOCKET) return PearlPoolWaitStatus::Error;
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(fd_, &writeSet);
        struct timeval timeout { milliseconds / 1000,
                                 (milliseconds % 1000) * 1000 };
        const int result =
            select((int)fd_ + 1, nullptr, &writeSet, nullptr, &timeout);
        return result > 0 ? PearlPoolWaitStatus::Ready
                          : result == 0 ? PearlPoolWaitStatus::Timeout
                                        : PearlPoolWaitStatus::Error;
    }

    PearlPoolTransfer receive(char *out, std::size_t maximum) override {
        if (fd_ == OM_INVALID_SOCKET || !out || maximum == 0)
            return {PearlPoolTransferStatus::Error, 0};
        const int count = socketRecv(fd_, out, maximum);
        if (count > 0)
            return {PearlPoolTransferStatus::Bytes,
                    static_cast<std::size_t>(count)};
        if (count == 0) return {PearlPoolTransferStatus::EndOfStream, 0};
#if defined(_WIN32)
        const int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINTR)
            return {PearlPoolTransferStatus::WouldBlock, 0};
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return {PearlPoolTransferStatus::WouldBlock, 0};
#endif
        return {PearlPoolTransferStatus::Error, 0};
    }

    PearlPoolTransfer send(const char *data, std::size_t length) override {
        if (fd_ == OM_INVALID_SOCKET || !data || length == 0)
            return {PearlPoolTransferStatus::Error, 0};
        // Winsock takes an int length. Bound the individual syscall without
        // truncating the source's total-byte evidence; sendLineUntil loops over
        // any remaining bytes. The same bound also keeps the POSIX ssize_t
        // result representable in this transport's intentionally portable int.
        const std::size_t request =
            length > static_cast<std::size_t>(std::numeric_limits<int>::max())
                ? static_cast<std::size_t>(std::numeric_limits<int>::max())
                : length;
#if defined(_WIN32)
        const int count = ::send(fd_, data, static_cast<int>(request), 0);
#elif defined(MSG_NOSIGNAL)
        const int count =
            static_cast<int>(::send(fd_, data, request, MSG_NOSIGNAL));
#else
        const int count = static_cast<int>(::send(fd_, data, request, 0));
#endif
        if (count > 0)
            return {PearlPoolTransferStatus::Bytes,
                    static_cast<std::size_t>(count)};
        if (count == 0) return {PearlPoolTransferStatus::EndOfStream, 0};
#if defined(_WIN32)
        const int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINTR)
            return {PearlPoolTransferStatus::WouldBlock, 0};
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return {PearlPoolTransferStatus::WouldBlock, 0};
#endif
        return {PearlPoolTransferStatus::Error, 0};
    }

    bool setNonblocking(bool enabled) override {
        if (fd_ == OM_INVALID_SOCKET) return false;
#if defined(_WIN32)
        u_long value = enabled ? 1 : 0;
        return ioctlsocket(fd_, FIONBIO, &value) == 0;
#else
        if (enabled) {
            const int flags = fcntl(fd_, F_GETFL, 0);
            if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) != 0)
                return false;
            savedFlags_ = flags;
            savedFlagsValid_ = true;
            return true;
        }
        if (!savedFlagsValid_) return false;
        const bool restored = fcntl(fd_, F_SETFL, savedFlags_) == 0;
        if (restored) savedFlagsValid_ = false;
        return restored;
#endif
    }

   private:
    socket_t fd_ = OM_INVALID_SOCKET;
#if !defined(_WIN32)
    int savedFlags_ = 0;
    bool savedFlagsValid_ = false;
#else
    bool savedFlagsValid_ = false;
#endif
};

}  // namespace om
