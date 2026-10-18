#pragma once
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif
#include "../algos/pearl-pow/job.h"

namespace om {
inline bool pearlEvidenceRunIdValid(const char *id) noexcept {
    if (!id) return false;
    for (unsigned i=0; i<32; ++i)
        if (!((id[i]>='0' && id[i]<='9') || (id[i]>='a' && id[i]<='f'))) return false;
    return id[32]=='\0';
}
inline void pearlEvidenceHex(const uint8_t *bytes, char *out) noexcept {
    const char digits[]="0123456789abcdef";
    for (unsigned i=0; i<32; ++i) { out[2*i]=digits[bytes[i]>>4]; out[2*i+1]=digits[bytes[i]&15]; }
    out[64]='\0';
}
// Bounded-memory incremental unkeyed BLAKE3, using the existing exact chunk
// and parent helpers. Retain the final chunk (even when full) for ROOT handling.
class PearlEvidenceHash {
 public:
    void update(const char *bytes, std::size_t size) noexcept {
        while (size) {
            if (used_==sizeof(chunk_)) {
                uint8_t cv[32];
                pearl::b3::chunkCv(nullptr,chunk_,used_,chunks_,false,cv);
                uint64_t count=++chunks_;
                while ((count&1)==0) {
                    pearl::b3::parentCv(nullptr,stack_[--depth_].data(),cv,false,cv);
                    count >>= 1;
                }
                std::memcpy(stack_[depth_++].data(),cv,32); used_=0;
            }
            const std::size_t n=std::min(size,sizeof(chunk_)-used_);
            std::memcpy(chunk_+used_,bytes,n); used_+=n; bytes+=n; size-=n;
        }
    }
    std::array<uint8_t,32> digest() const noexcept {
        std::array<uint8_t,32> out{};
        pearl::b3::chunkCv(nullptr,chunk_,used_,chunks_,depth_==0,out.data());
        for (std::size_t i=depth_; i>0; --i)
            pearl::b3::parentCv(nullptr,stack_[i-1].data(),out.data(),i==1,out.data());
        return out;
    }
 private:
    uint8_t chunk_[1024]{};
    std::array<std::array<uint8_t,32>,64> stack_{};
    std::size_t used_=0, depth_=0;
    uint64_t chunks_=0;
};
struct PearlEvidenceDigest {
    uint64_t bytes=0;
    std::array<uint8_t,32> digest{};
    bool closed=false;
};
class PearlLocalRetirementWriter {
 public:
    virtual ~PearlLocalRetirementWriter() = default;
    virtual std::size_t write(const char *, std::size_t) noexcept = 0;
    virtual bool flush() noexcept = 0;
    virtual bool close() noexcept { return true; }
    virtual bool segmented() const noexcept { return false; }
};
// Explicit evidence artifacts are fresh, binary streams. Never fall back to a
// truncating open. O_EXCL also refuses an existing final-component symlink.
inline FILE *pearlOpenExclusiveEvidenceFile(const char *path) noexcept {
    if (!path || !*path) return nullptr;
#ifdef _WIN32
    const int fd=_open(path,_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
    if (fd<0) return nullptr;
    FILE *file=_fdopen(fd,"wb"); if (!file) _close(fd);
#else
    int flags=O_WRONLY|O_CREAT|O_EXCL;
#ifdef O_NOFOLLOW
    flags|=O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
    flags|=O_CLOEXEC;
#endif
    const int fd=::open(path,flags,0600);
    if (fd<0) return nullptr;
    FILE *file=fdopen(fd,"wb"); if (!file) ::close(fd);
#endif
    return file;
}
class PearlLocalRetirementFile final : public PearlLocalRetirementWriter {
 public:
    PearlLocalRetirementFile() = default;
    PearlLocalRetirementFile(const PearlLocalRetirementFile &) = delete;
    PearlLocalRetirementFile &operator=(const PearlLocalRetirementFile &) = delete;
    ~PearlLocalRetirementFile() { if (file_) std::fclose(file_); }
    bool close() noexcept override {
        if (!file_) return !closeFailed_;
        FILE *file=file_; file_=nullptr;
        closeFailed_=std::fclose(file)!=0; return !closeFailed_;
    }
    bool open(const char *path) noexcept {
        if (attempted_ || !path || !*path) return false;
        attempted_=true;
        file_=pearlOpenExclusiveEvidenceFile(path);
        return file_!=nullptr;
    }
    std::size_t write(const char *p,std::size_t n) noexcept override { return file_?std::fwrite(p,1,n,file_):0; }
    bool flush() noexcept override { return file_ && std::fflush(file_)==0; }
 private:
    FILE *file_=nullptr;
    bool attempted_=false,closeFailed_=false;
};
// Owns no FILE; close clears the source's pointer so its destructor is inert.
class PearlBorrowedEvidenceFile final : public PearlLocalRetirementWriter {
 public:
    explicit PearlBorrowedEvidenceFile(FILE **file) : file_(file) {}
    std::size_t write(const char *p,std::size_t n) noexcept override { return *file_?std::fwrite(p,1,n,*file_):0; }
    bool flush() noexcept override { return *file_ && std::fflush(*file_)==0; }
    bool close() noexcept override {
        if (!*file_) return !failed_;
        FILE *file=*file_; *file_=nullptr; failed_=std::fclose(file)!=0; return !failed_;
    }
 private:
    FILE **file_;
    bool failed_=false;
};
class PearlEvidenceStream {
 public:
    bool configure(PearlLocalRetirementWriter *writer,uint64_t limit) noexcept {
        if (writer_ || !writer || !limit || limit>128ULL*1024*1024) return fail();
        writer_=writer; limit_=limit; return !failed_;
    }
    bool configureProduction(PearlLocalRetirementWriter *writer) noexcept {
        if (writer_ || !writer || !writer->segmented()) return fail();
        writer_=writer; limit_=INT64_MAX; return !failed_;
    }
    bool append(const char *p,std::size_t n) noexcept {
        if (failed_ || closed_ || active_ || !writer_ || (!p && n) || n>limit_-bytes_) return fail();
        active_=true;
        const auto written=writer_->write(p,n);
        const bool flushed=!failed_ && written==n && writer_->flush();
        active_=false;
        if (failed_ || written!=n || !flushed) return fail();
        hash_.update(p,n); bytes_+=n; return true;
    }
    bool finish(PearlEvidenceDigest *out) noexcept {
        if (out) *out={};
        if (!out || active_ || closed_ || !writer_) return fail();
        active_=true;
        const bool flushed=writer_->flush();
        const bool closed=writer_->close();
        active_=false; closed_=true;
        if (failed_ || !flushed || !closed) return fail();
        *out={bytes_,hash_.digest(),true}; return true;
    }
    bool failed() const noexcept { return failed_; }
 private:
    bool fail() noexcept { failed_=true; return false; }
    PearlLocalRetirementWriter *writer_=nullptr;
    PearlEvidenceHash hash_;
    uint64_t bytes_=0,limit_=0;
    bool failed_=false,active_=false,closed_=false;
};
} // namespace om
