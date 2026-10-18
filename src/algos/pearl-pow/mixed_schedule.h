// Deterministic whole-attempt schedules; no proof holding or transmission policy.
#pragma once
#include <cstdint>
#include <cstring>
namespace om::pearl {
class MixedSchedule {
public:
    explicit MixedSchedule(const char *pattern) noexcept {
        if(!pattern)return;
        auto n=std::strlen(pattern);if(!n || n>32)return;
        for(size_t i=0;i<n;++i)if(pattern[i]!='S' && pattern[i]!='L')return;
        std::memcpy(pattern_,pattern,n);length_=n;
    }
    bool valid() const noexcept{return length_!=0;}
    uint32_t m(uint64_t attempt) const noexcept{return valid()?(pattern_[attempt%length_]=='S'?4096u:16384u):0;}
    static constexpr uint32_t n=131072;
    uint64_t units(uint64_t attempt) const noexcept{return uint64_t(m(attempt)/16)*(n/16);}
    bool complete(uint64_t nonce) noexcept {
        if(!valid() || completed_==UINT64_MAX || (completed_ && nonce<=lastNonce_) ||
            units(completed_)>UINT64_MAX-work_)return false;
        work_+=units(completed_);lastNonce_=nonce;++completed_;return true;
    }
    uint64_t completed()const noexcept{return completed_;}
    uint64_t work()const noexcept{return work_;}
private:
    char pattern_[33]{};size_t length_=0;uint64_t completed_=0,work_=0,lastNonce_=0;
};
}
