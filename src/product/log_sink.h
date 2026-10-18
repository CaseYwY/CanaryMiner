// Rotating log file sink with a hard size cap, plus a tee that routes the process's stdout/stderr through the redaction
// filter into that sink (and back to the original console).
//
//   LogSink     canaryminer.log, canaryminer.log.1 ... canaryminer.log.<max_files-1>; rotation when the current file would
//               exceed max_file_bytes; total disk use is bounded by max_files * max_file_bytes. Every line goes through the
//               Redactor before it is written.
//   OutputTee   dup2()s stdout and stderr onto pipes, reads them on one thread line by line, redacts each line, writes it
//               to the original descriptor and to the LogSink. After it starts, stdout is not a terminal (the miner then
//               prints its plain, line-oriented format). The Windows branch uses the CRT equivalents and has not been built
//               or run in this repository (Gate 1b-1 had no Windows toolchain).

#pragma once

#include <stdint.h>
#include <stdio.h>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "redact.h"

namespace om {

struct LogSinkConfig {
    std::string dir;
    uint64_t maxFileBytes = 10ull * 1024 * 1024;
    unsigned maxFiles = 5;
};

class LogSink {
   public:
    LogSink(const LogSinkConfig &cfg, const Redactor *redactor) : cfg_(cfg), redactor_(redactor) {}
    ~LogSink() { close(); }
    LogSink(const LogSink &) = delete;
    LogSink &operator=(const LogSink &) = delete;

    bool open(std::string *error) {
        std::lock_guard<std::mutex> lock(mu_);
        if (cfg_.dir.empty() || cfg_.maxFiles < 1 || cfg_.maxFileBytes < 256) {
            if (error) *error = "log sink needs a directory, max_files >= 1 and max_file_bytes >= 256";
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(cfg_.dir, ec);
        if (ec) {
            if (error) *error = "cannot create log directory: " + ec.message();
            return false;
        }
        return openCurrent(error);
    }

    /** One line (no trailing newline needed). Redacted, truncated to the file cap, rotated as needed. */
    void write(const std::string &rawLine) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!file_) return;
        std::string line = redactor_ ? redactor_->apply(rawLine) : rawLine;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        const uint64_t room = cfg_.maxFileBytes - 1;
        if (line.size() > room) line.resize(static_cast<size_t>(room));
        line += '\n';
        if (size_ + line.size() > cfg_.maxFileBytes) rotate();
        if (!file_) return;
        if (fwrite(line.data(), 1, line.size(), file_) == line.size()) {
            size_ += line.size();
            fflush(file_);
        }
    }

    void close() {
        std::lock_guard<std::mutex> lock(mu_);
        if (file_) {
            fclose(file_);
            file_ = nullptr;
        }
    }

    std::string currentPath() const { return pathFor(0); }
    std::string pathFor(unsigned index) const {
        std::string p = (std::filesystem::path(cfg_.dir) / "canaryminer.log").string();
        if (index) p += "." + std::to_string(index);
        return p;
    }

   private:
    LogSinkConfig cfg_;
    const Redactor *redactor_;
    std::mutex mu_;
    FILE *file_ = nullptr;
    uint64_t size_ = 0;

    bool openCurrent(std::string *error) {
        file_ = fopen(pathFor(0).c_str(), "ab");
        if (!file_) {
            if (error) *error = "cannot open log file " + pathFor(0);
            return false;
        }
        std::error_code ec;
        const auto sz = std::filesystem::file_size(pathFor(0), ec);
        size_ = ec ? 0 : static_cast<uint64_t>(sz);
        return true;
    }

    void rotate() {
        if (file_) {
            fclose(file_);
            file_ = nullptr;
        }
        std::error_code ec;
        if (cfg_.maxFiles == 1) {
            std::filesystem::remove(pathFor(0), ec);
        } else {
            std::filesystem::remove(pathFor(cfg_.maxFiles - 1), ec);
            for (unsigned i = cfg_.maxFiles - 1; i >= 1; --i) {
                if (std::filesystem::exists(pathFor(i - 1), ec)) std::filesystem::rename(pathFor(i - 1), pathFor(i), ec);
            }
        }
        std::string ignored;
        openCurrent(&ignored);
        size_ = 0;
    }
};

class OutputTee {
   public:
    OutputTee(LogSink *sink, const Redactor *redactor) : sink_(sink), redactor_(redactor) {}
    ~OutputTee() { stop(); }
    OutputTee(const OutputTee &) = delete;
    OutputTee &operator=(const OutputTee &) = delete;

    bool start() {
        if (running_) return true;
        fflush(stdout);
        fflush(stderr);
        if (!hook(1, &out_)) return false;
        if (!hook(2, &err_)) {
            unhook(&out_);
            closeRead(&out_);
            return false;
        }
        running_ = true;
        outThread_ = std::thread([this] { pump(&out_); });
        errThread_ = std::thread([this] { pump(&err_); });
        return true;
    }

    void stop() {
        if (!running_) return;
        fflush(stdout);
        fflush(stderr);
        // Restoring the original descriptors closes the last write end of each pipe, which delivers EOF to its reader.
        unhook(&out_);
        unhook(&err_);
        if (outThread_.joinable()) outThread_.join();
        if (errThread_.joinable()) errThread_.join();
        closeRead(&out_);
        closeRead(&err_);
        running_ = false;
    }

   private:
    struct Channel {
        int target = -1;  // 1 or 2
        int saved = -1;   // duplicate of the original descriptor
        int rd = -1;      // read end of the pipe
        std::string partial;
    };

    LogSink *sink_;
    const Redactor *redactor_;
    Channel out_, err_;
    std::thread outThread_, errThread_;
    bool running_ = false;

#ifdef _WIN32
    static int pipeOpen(int fds[2]) { return _pipe(fds, 65536, _O_BINARY); }
    static int dupFd(int fd) { return _dup(fd); }
    static int dup2Fd(int a, int b) { return _dup2(a, b); }
    static int closeFd(int fd) { return _close(fd); }
    static long readFd(int fd, void *b, unsigned n) { return _read(fd, b, n); }
    static long writeFd(int fd, const void *b, unsigned n) { return _write(fd, b, n); }
#else
    static int pipeOpen(int fds[2]) { return ::pipe(fds); }
    static int dupFd(int fd) { return ::dup(fd); }
    static int dup2Fd(int a, int b) { return ::dup2(a, b); }
    static int closeFd(int fd) { return ::close(fd); }
    static long readFd(int fd, void *b, unsigned n) { return static_cast<long>(::read(fd, b, n)); }
    static long writeFd(int fd, const void *b, unsigned n) { return static_cast<long>(::write(fd, b, n)); }
#endif

    static bool hook(int target, Channel *c) {
        int fds[2];
        if (pipeOpen(fds) != 0) return false;
        c->target = target;
        c->saved = dupFd(target);
        if (c->saved < 0 || dup2Fd(fds[1], target) < 0) {
            closeFd(fds[0]);
            closeFd(fds[1]);
            if (c->saved >= 0) closeFd(c->saved);
            c->saved = -1;
            return false;
        }
        closeFd(fds[1]);  // the descriptor 'target' now holds the only write end
        c->rd = fds[0];
        return true;
    }

    static void unhook(Channel *c) {
        if (c->saved >= 0) dup2Fd(c->saved, c->target);
    }

    static void closeRead(Channel *c) {
        if (c->rd >= 0) closeFd(c->rd);
        if (c->saved >= 0) closeFd(c->saved);
        c->rd = c->saved = -1;
    }

    void emit(Channel *c, const std::string &line) {
        const std::string clean = redactor_ ? redactor_->apply(line) : line;
        const std::string withNl = clean + "\n";
        writeFd(c->saved, withNl.data(), static_cast<unsigned>(withNl.size()));
        if (sink_) sink_->write(clean);
    }

    void pump(Channel *c) {
        char buf[4096];
        long n;
        while ((n = readFd(c->rd, buf, sizeof(buf))) > 0) {
            for (long i = 0; i < n; ++i) {
                if (buf[i] == '\n') {
                    emit(c, c->partial);
                    c->partial.clear();
                } else if (buf[i] != '\r') {
                    c->partial += buf[i];
                    if (c->partial.size() >= 65536) {
                        emit(c, c->partial);
                        c->partial.clear();
                    }
                }
            }
        }
        if (!c->partial.empty()) emit(c, c->partial);
        c->partial.clear();
    }
};

}  // namespace om
