// Continuous-run streams: bounded segment files, no resume/truncate, no in-memory inventory.
#pragma once
#include "pearl_evidence_io.h"
#include <string>
#ifndef _WIN32
#include <sys/statvfs.h>
#endif
namespace om {
class PearlProductionEvidenceFile final : public PearlLocalRetirementWriter {
 public:
    static constexpr uint64_t segmentBytes=16ULL*1024*1024;
    static constexpr uint64_t minimumFreeBytes=512ULL*1024*1024;
    ~PearlProductionEvidenceFile() { if (file_) std::fclose(file_); }
    bool open(const std::string &path) noexcept {
        if (attempted_ || path.empty() || path.size()>4096) return false;
        attempted_=true;
        try { path_=path; return next(); } catch (...) { failed_=true; return false; }
    }
    bool segmented() const noexcept override { return true; }
    std::size_t write(const char *p,std::size_t n) noexcept override {
        if (failed_ || !file_ || (!p && n) || n>segmentBytes) return 0;
        try {
            if (n>segmentBytes-used_) {
                if (!finishSegment()) return 0;
                if (part_==UINT32_MAX) { failed_=true; return 0; }
                ++part_; if (!next()) return 0;
            }
            const auto written=std::fwrite(p,1,n,file_); used_+=written;
            if (written!=n) failed_=true;
            return written;
        } catch (...) { failed_=true; return 0; }
    }
    bool flush() noexcept override {
        if (failed_ || !file_ || std::fflush(file_)!=0 || std::ferror(file_)) { failed_=true; return false; }
        return true;
    }
    bool close() noexcept override { return file_ ? finishSegment() : attempted_ && !failed_; }
 private:
    bool finishSegment() noexcept {
        bool ok=flush();
#ifndef _WIN32
        if (ok) ok=::fsync(fileno(file_))==0;
#endif
        FILE *file=file_; file_=nullptr;
        if (file && std::fclose(file)!=0) ok=false;
        if (!ok) failed_=true;
        return ok;
    }
    bool next() {
#ifdef _WIN32
        failed_=true; return false; // This private production owner is Linux-only.
#else
        const auto slash=path_.find_last_of('/');
        const std::string parent=slash==std::string::npos ? "." : path_.substr(0,slash);
        struct statvfs space{};
        if (::statvfs(parent.c_str(),&space)!=0 || !space.f_frsize ||
            space.f_bavail<minimumFreeBytes/space.f_frsize+1) { failed_=true; return false; }
        char suffix[32]; std::snprintf(suffix,sizeof(suffix),".part-%08u",part_);
        const std::string path=part_ ? path_+suffix : path_;
        file_=pearlOpenExclusiveEvidenceFile(path.c_str()); used_=0;
        if (!file_) { failed_=true; return false; }
        const int directory=::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        bool ok=directory>=0 && ::fsync(directory)==0;
        if (directory>=0 && ::close(directory)!=0) ok=false;
        if (!ok) failed_=true;
        return ok;
#endif
    }
    std::string path_;
    FILE *file_=nullptr; uint64_t used_=0; uint32_t part_=0;
    bool attempted_=false,failed_=false;
};
} // namespace om
