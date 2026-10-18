// EXPERIMENT ONLY: not included by any production translation unit.
// A second-hop retirement is never a pool ACK or native-to-proxy NotSent.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

namespace om { namespace retirement_experiment {
using Digest = std::array<uint8_t,32>;
using Id = std::array<uint8_t,16>;
constexpr size_t frameBytes = 204;
using Bytes = std::array<uint8_t,frameBytes>;
struct Frame {
    uint8_t kind=1;
    Id run{},session{};
    uint32_t request=0,reservation=0;
    // Guard notification counters, NOT native published-job generations.
    uint64_t guardGeneration=0,guardReplacementGeneration=0;
    Digest job{},replacement{},proof{},verifierDocument{};
    uint64_t upstreamBytes=0;
};
// Wire representation only; each owning session still enforces its admitted profile cap.
inline bool valid(const Frame &f) {
    return (f.kind==1 || f.kind==2) && f.request && f.request<=2147483647 &&
        f.reservation && f.reservation<=4096 && f.guardGeneration &&
        f.guardReplacementGeneration>f.guardGeneration && f.job!=f.replacement && !f.upstreamBytes;
}
inline void put(uint8_t *p,uint64_t n,size_t size) {
    for(size_t i=0;i<size;++i) { p[size-1-i]=uint8_t(n); n>>=8; }
}
inline uint64_t get(const uint8_t *p,size_t size) {
    uint64_t n=0; for(size_t i=0;i<size;++i) n=(n<<8)|p[i]; return n;
}
inline bool encode(const Frame &f,Bytes *out) {
    if(!out || !valid(f)) return false;
    *out={}; auto p=out->data(); std::memcpy(p,"PRLGRET1",8); p[8]=f.kind;
    std::memcpy(p+12,f.run.data(),16); std::memcpy(p+28,f.session.data(),16);
    put(p+44,f.request,4); put(p+48,f.reservation,4);
    put(p+52,f.guardGeneration,8); put(p+60,f.guardReplacementGeneration,8);
    std::memcpy(p+68,f.job.data(),32); std::memcpy(p+100,f.replacement.data(),32);
    std::memcpy(p+132,f.proof.data(),32); std::memcpy(p+164,f.verifierDocument.data(),32);
    return true;
}
inline bool decode(const void *data,size_t size,Frame *out) {
    if(!data || !out || size!=frameBytes) return false;
    auto p=static_cast<const uint8_t*>(data);
    if(std::memcmp(p,"PRLGRET1",8) || p[9] || p[10] || p[11] || get(p+196,8)) return false;
    Frame f; f.kind=p[8]; std::memcpy(f.run.data(),p+12,16); std::memcpy(f.session.data(),p+28,16);
    f.request=uint32_t(get(p+44,4)); f.reservation=uint32_t(get(p+48,4));
    f.guardGeneration=get(p+52,8); f.guardReplacementGeneration=get(p+60,8);
    std::memcpy(f.job.data(),p+68,32); std::memcpy(f.replacement.data(),p+100,32);
    std::memcpy(f.proof.data(),p+132,32); std::memcpy(f.verifierDocument.data(),p+164,32);
    if(!valid(f)) return false;
    *out=f;
    return true;
}
struct PipeBinding { int fd=-1; dev_t device=0; ino_t inode=0; int direction=-1; };
// Expected binding must come from the trusted supervisor's own os.pipe/pipe2
// inventory. fstat or /proc alone does not prove which process created a pipe.
inline bool boundAnonymousPipe(const PipeBinding &b) {
    struct stat st{};
    if(b.fd<=2 || fstat(b.fd,&st) || !S_ISFIFO(st.st_mode) ||
       st.st_dev!=b.device || st.st_ino!=b.inode ||
       (b.direction!=O_RDONLY && b.direction!=O_WRONLY)) return false;
    const int flags=fcntl(b.fd,F_GETFL);
    if(flags<0 || (flags&O_ACCMODE)!=b.direction || !(flags&O_NONBLOCK)) return false;
    char path[64],actual[128]{},expected[128]{};
    std::snprintf(path,sizeof(path),"/proc/self/fd/%d",b.fd);
    const auto n=readlink(path,actual,sizeof(actual)-1);
    std::snprintf(expected,sizeof(expected),"pipe:[%llu]",static_cast<unsigned long long>(st.st_ino));
    return n>0 && n<static_cast<ssize_t>(sizeof(actual)-1) && !std::strcmp(actual,expected);
}
class Channel {
public:
    bool admit(PipeBinding incoming,PipeBinding outgoing,uint8_t network,uint8_t targetSemantics,uint8_t certificate) {
        if(admitted_ || failed_ || incoming.direction!=O_RDONLY || outgoing.direction!=O_WRONLY ||
           network!=1 || targetSemantics!=1 || certificate!=3 ||
           incoming.fd==outgoing.fd || (incoming.device==outgoing.device && incoming.inode==outgoing.inode) ||
           !boundAnonymousPipe(incoming) || !boundAnonymousPipe(outgoing)) return fail();
        // Intended ends were explicitly inherited. Do not leak them to later execs.
        if(fcntl(incoming.fd,F_SETFD,FD_CLOEXEC) || fcntl(outgoing.fd,F_SETFD,FD_CLOEXEC)) return fail();
        in_=incoming; out_=outgoing; admitted_=true; return true;
    }
    bool quiescent() {
        if(!admitted_ || failed_ || !boundAnonymousPipe(in_) || !boundAnonymousPipe(out_)) return fail();
        pollfd p{in_.fd,POLLIN,0};
        // Any queued bytes, EOF, HUP, invalid descriptor or error blocks clean completion.
        if(poll(&p,1,0)!=0) return fail();
        pollfd q{out_.fd,POLLOUT,0};
        if(poll(&q,1,0)!=1 || q.revents!=POLLOUT) return fail();
        return true;
    }
    bool offer(Frame *f) {
        if(!admitted_ || failed_ || !boundAnonymousPipe(in_)) return fail();
        Bytes b; const auto n=read(in_.fd,b.data(),b.size());
        if(n!=static_cast<ssize_t>(b.size()) || !decode(b.data(),b.size(),f) || f->kind!=1) return fail();
        return quiescent(); // Reject a second record/trailing byte already queued.
    }
    bool receipt(const Frame &offer) {
        if(!quiescent() || offer.kind!=1) return fail();
        Frame f=offer; f.kind=2; Bytes b; if(!encode(f,&b)) return fail();
        // <= PIPE_BUF; exactly one nonblocking write. EINTR/short/EAGAIN are fatal.
        // Caller must have normal process SIGPIPE handling; no retry is allowed.
        if(write(out_.fd,b.data(),b.size())!=static_cast<ssize_t>(b.size())) return fail();
        return quiescent();
    }
    bool failed() const { return failed_; }
private:
    bool fail() { failed_=true; return false; }
    PipeBinding in_{},out_{}; bool admitted_=false,failed_=false;
};

// Detached CPU prototype ledger; not the production source counters/auditor.
// All submitted proofs consume their quota permanently, including retirement.
class Pending {
public:
    Pending(Id run,Id session,uint32_t cap):run_(run),session_(session),cap_(cap) {}
    bool begin(uint32_t request,const Digest &job,const Digest &proof,uint64_t now,uint64_t deadline,
               bool nativeToGuardFullFrame) {
        if(failed_ || finished_ || active_ || !cap_ || cap_>256 || reserved_==cap_ || request<=lastRequest_ ||
           request>2147483647 || now>=deadline || (reserved_ && deadline!=deadline_) || !nativeToGuardFullFrame)
            return fail();
        active_=true; prepared_=false; request_=request; lastRequest_=request; ++reserved_;
        job_=job; proof_=proof; deadline_=deadline; count_=0; return true;
    }
    bool observe(const Digest &canonicalJob,uint64_t now) {
        if(!live(now) || prepared_) return fail();
        // Duplicate notifications do not require a matching native counter.
        if(canonicalJob==(count_ ? replacements_[count_-1] : job_)) return true;
        if(count_==replacements_.size()) return fail();
        replacements_[count_++]=canonicalJob; return true;
    }
    bool prepare(const Frame &f,uint64_t now) {
        if(!live(now) || prepared_ || !valid(f) || f.kind!=1 || f.run!=run_ || f.session!=session_ ||
           f.request!=request_ || f.reservation!=reserved_ || f.job!=job_ || f.proof!=proof_ ||
           f.guardGeneration<lastGuardReplacement_) return fail();
        bool seen=false; for(size_t i=0;i<count_;++i) seen|=replacements_[i]==f.replacement;
        if(!seen) return fail();
        // The document digest is guard-attested and echoed, not native-verified.
        offered_=f; prepared_=true; return true;
    }
    bool receiptWritten(const Frame &f,uint64_t now,bool exactAtomicWrite) {
        Bytes a,b;
        if(!live(now) || !prepared_ || !exactAtomicWrite || !encode(f,&a) || !encode(offered_,&b) || a!=b)
            return fail();
        ++retired_; lastGuardReplacement_=f.guardReplacementGeneration; active_=false; prepared_=false;
        return true;
    }
    // Fixture-only ordinary pool-ACK accounting; no control frame calls this.
    bool poolAccepted(uint64_t now) {
        if(!live(now) || prepared_) return fail();
        ++accepted_; active_=false; return true;
    }
    bool finish(uint64_t now,Channel &channel) {
        // Last slot can retire; there need not be another pool reply to finish.
        // Overall run still requires the separate guard's consumed receipt/audit.
        if(failed_ || active_ || reserved_!=cap_ || accepted_+retired_!=reserved_ || now>=deadline_ ||
           !channel.quiescent()) return fail();
        finished_=true; return true;
    }
    uint32_t reserved() const { return reserved_; }
    uint32_t accepted() const { return accepted_; }
    uint32_t retired() const { return retired_; }
    bool failed() const { return failed_; }
private:
    bool live(uint64_t now) const { return !failed_ && !finished_ && active_ && now<deadline_; }
    bool fail() { failed_=true; return false; }
    Id run_,session_; uint32_t cap_,reserved_=0,accepted_=0,retired_=0,request_=0,lastRequest_=0;
    Digest job_{},proof_{}; std::array<Digest,32> replacements_{}; size_t count_=0;
    uint64_t deadline_=0,lastGuardReplacement_=0; Frame offered_{};
    bool active_=false,prepared_=false,failed_=false,finished_=false;
};
}} // om::retirement_experiment
