// Opt-in source transport experiment. Not a public mining profile.
#pragma once
#include "pearl_guard_retirement_channel.h"
#include "pearl_guard_retirement_v3.h"
namespace om { class PearlRunProgressCoordinator; namespace guard_retirement_v2 {
using namespace retirement_experiment;
// V1 is immutable: v2 explicitly adds peer-confirmed consumption (kind3).
inline bool encodeV2(const Frame &f,Bytes *out) {
    Frame wire=f; if(wire.kind==3) wire.kind=2;
    if(!encode(wire,out)) return false;
    (*out)[7]='2'; (*out)[8]=f.kind; return f.kind>=1 && f.kind<=3;
}
inline bool decodeV2(const void *raw,size_t size,Frame *out) {
    if(!raw || !out || size!=frameBytes) return false;
    Bytes wire; std::memcpy(wire.data(),raw,size);
    if(std::memcmp(wire.data(),"PRLGRET2",8) || wire[8]<1 || wire[8]>3) return false;
    const auto kind=wire[8]; wire[7]='1'; if(kind==3) wire[8]=2;
    if(!decode(wire.data(),wire.size(),out)) return false;
    out->kind=kind;
    return true;
}
// Capacity widening is explicit; the legacy session default remains 256.
enum class Profile { Legacy, Public24h, Production, PersonalProduction };
inline uint32_t profileCap(Profile p) { return p==Profile::Legacy ? 256U : p==Profile::Public24h ? 4096U :
    (p==Profile::Production || p==Profile::PersonalProduction) ? 2147483647U : 0U; }
enum class Step { Waiting, Confirmed, Failed };
enum class Ordinary { Accepted, Rejected, Stale, Unknown };
struct Totals {
    uint64_t fullProxy=0,accepted=0,rejected=0,stale=0,unknown=0,retired=0;
    uint64_t terminal() const { return accepted+rejected+stale+unknown+retired; }
    bool conserved() const { return fullProxy>=terminal(); }
};
struct NaturalFinal {
    Id run{}, session{};
    uint64_t reserved=0, retired=0, cap=0, stopMs=0, drainMs=0;
    bool complete=false,staleContinuation=false;
};
class Session {
public:
    using Retain=bool (*)(void*,const char*,const Bytes&);
    Session(Id run,Id session,uint32_t cap,uint64_t deadline,Profile profile=Profile::Legacy)
        :run_(run),session_(session),cap_(cap),deadline_(deadline),profile_(profile) {}
    bool admit(PipeBinding in,PipeBinding out,uint8_t network,uint8_t semantics,uint8_t cert,
               Retain retain,void *context) {
        if(admitted_ || !cap_ || cap_>profileCap(profile_) || (production() ? deadline_!=0 : (!deadline_ || deadline_>INT64_MAX)) || !retain ||
           !channel_.admit(in,out,network,semantics,cert)) return fail();
        in_=in; out_=out; retain_=retain; context_=context; admitted_=true; return true;
    }
    bool ready(uint64_t now) {
        return admitted_ && !failed_ && !finished_ && !productionSealed_ && !active_ && withinLifetime(now) &&
            totals_.fullProxy<cap_ && channel_.quiescent();
    }
    bool begin(uint32_t request,const Digest &job,const Digest &proof,uint64_t now) {
        if(!admitted_ || failed_ || finished_ || productionSealed_ || active_ || !withinLifetime(now) || totals_.fullProxy>=cap_ ||
           !request || request<=lastRequest_ || request>2147483647) return fail();
        // Called only after the actual source has recorded complete delivery.
        lastRequest_=request; job_=job; proof_=proof; active_=true; phase_=0; count_=0;
        ++totals_.fullProxy;
        if (totals_.fullProxy==cap_) capacityReachedMs_=now;
        return true;
    }
    bool observe(const Digest &job,uint64_t now) {
        if(!live(now)) return fail();
        if(job==(count_ ? history_[count_-1] : job_)) return true;
        if(count_==history_.size()) return fail();
        history_[count_++]=job;
        return true;
    }
    Step step(uint64_t now) {
#if defined(_WIN32)
        // No owned anonymous-pipe guard channel on Windows: fail closed (see pearl_guard_retirement_channel.h).
        (void)now; fail(); return Step::Failed;
#else
        if(!live(now) || !boundAnonymousPipe(in_) || !boundAnonymousPipe(out_)) { fail(); return Step::Failed; }
        pollfd p{in_.fd,POLLIN,0}; const int polled=poll(&p,1,0);
        if(polled<0 || (polled && p.revents!=POLLIN)) { fail(); return Step::Failed; }
        if(polled) {
            Bytes bytes; Frame frame;
            if(read(in_.fd,bytes.data(),bytes.size())!=ssize_t(bytes.size()) ||
               !decodeFrame(bytes.data(),bytes.size(),&frame) || !channel_.quiescent()) { fail(); return Step::Failed; }
            if(phase_==0) {
                if(frame.kind!=1 || frame.run!=run_ || frame.session!=session_ || frame.request!=lastRequest_ ||
                   frame.reservation!=totals_.fullProxy || frame.job!=job_ || frame.proof!=proof_ ||
                   frame.guardGeneration<lastGuardReplacement_) { fail(); return Step::Failed; }
                offer_=frame; phase_=1;
                if(!retain_(context_,"guard_retirement_offer",bytes)) { fail(); return Step::Failed; }
            } else if(phase_==2) {
                Frame expected=offer_; expected.kind=3; Bytes wanted;
                if(!encodeFrame(expected,&wanted) || bytes!=wanted ||
                   !retain_(context_,"guard_retirement_confirmed",bytes)) { fail(); return Step::Failed; }
                ++totals_.retired; lastGuardReplacement_=offer_.guardReplacementGeneration;
                active_=false; phase_=3; return Step::Confirmed;
            } else { fail(); return Step::Failed; }
        }
        if(phase_==1) {
            bool observed=false; for(size_t i=0;i<count_;++i) observed|=history_[i]==offer_.replacement;
            // Separate kernel pipes and TCP have no shared delivery ordering.
            // Keep the offer pending until real source intake observes its job.
            if(!observed) return Step::Waiting;
            Frame receipt=offer_; receipt.kind=2; Bytes bytes;
            if(!encodeFrame(receipt,&bytes) || !channel_.quiescent() ||
               write(out_.fd,bytes.data(),bytes.size())!=ssize_t(bytes.size()) ||
               !retain_(context_,"guard_retirement_consumed",bytes)) { fail(); return Step::Failed; }
            phase_=2; // This is NOT yet a terminal outcome.
        }
        return Step::Waiting;
#endif
    }
    bool ordinary(Ordinary verdict,uint64_t now) {
        // Unknown is recorded even after a malformed/missing control failure;
        // it must not be converted to retirement by subsequently arriving data.
        if(!active_ || !totals_.conserved()) return fail();
        if(verdict!=Ordinary::Unknown && (!live(now) || phase_!=0 || !channel_.quiescent())) return fail();
        switch(verdict) {
          case Ordinary::Accepted:++totals_.accepted;break;
          case Ordinary::Rejected:++totals_.rejected;break;
          case Ordinary::Stale:++totals_.stale;break;
          case Ordinary::Unknown:++totals_.unknown;failed_=true;break;
        }
        active_=false; return true;
    }
    // Capacity-only CPU fixture. Production completion uses the coordinator grant below.
    bool finish(uint64_t now) {
        if(!admitted_ || failed_ || finished_ || active_ || !withinLifetime(now) || totals_.fullProxy!=cap_ ||
           totals_.terminal()!=totals_.fullProxy || !channel_.quiescent()) return fail();
        finished_=true; return true;
    }
    // Owner-thread fence: after Stop only the existing reservation may drain.
    bool sealProduction(uint64_t now) {
        if (!production() || !admitted_ || failed_ || finished_ || productionSealed_ ||
            !withinLifetime(now) || !totals_.conserved()) return fail();
        productionSealed_=true; productionSealMs_=now;
        productionSealReserved_=totals_.fullProxy;
        return true;
    }
    uint64_t productionSealReserved() const { return productionSealReserved_; }
    bool active() const { return active_; }
    uint64_t capacityReachedMs() const { return capacityReachedMs_; }
    uint32_t cap() const { return cap_; }
    const Id &run() const { return run_; }
    const Id &session() const { return session_; }
    bool completed() const { return finished_; }
    const NaturalFinal &naturalFinal() const { return naturalFinal_; }
    bool failed() const { return failed_; }
    const Totals &totals() const { return totals_; }
    const Frame &confirmed() const { return offer_; }
    uint64_t deadline() const { return deadline_; }
    bool production() const { return profile_==Profile::Production || profile_==Profile::PersonalProduction; }
    bool personalProduction() const { return profile_==Profile::PersonalProduction; }
private:
    friend class om::PearlRunProgressCoordinator;
    bool finishNatural(const Id &run,const Id &session,const Totals &expected,uint64_t stop,uint64_t now) {
        if (!admitted_ || failed_ || finished_ || active_ || !withinLifetime(now) || stop>now ||
            (productionSealed_ && (stop!=productionSealMs_ || totals_.fullProxy!=productionSealReserved_)) ||
            run!=run_ || session!=session_ || totals_.fullProxy>cap_ ||
            totals_.terminal()!=totals_.fullProxy || expected.fullProxy!=totals_.fullProxy ||
            expected.accepted!=totals_.accepted || expected.retired!=totals_.retired ||
            expected.rejected || expected.unknown || totals_.rejected || totals_.unknown ||
            expected.stale!=totals_.stale ||
            ((expected.stale || totals_.stale) && !personalProduction()) || !channel_.quiescent()) return fail();
        naturalFinal_={run_,session_,totals_.fullProxy,totals_.retired,cap_,stop,now,true,personalProduction()};
        finished_=true; return true;
    }
    NaturalFinal naturalFinal_{};
    bool productionSealed_=false;
    uint64_t productionSealMs_=0,productionSealReserved_=0;
    uint64_t capacityReachedMs_=0;
    bool live(uint64_t now) const { return admitted_ && !failed_ && !finished_ && active_ && withinLifetime(now); }
    bool withinLifetime(uint64_t now) const { return production() ? now<=INT64_MAX : now<deadline_; }
    bool encodeFrame(const Frame &f,Bytes *out) const { return production() ? retirement_production::encodeV3(f,out) : encodeV2(f,out); }
    bool decodeFrame(const void *raw,size_t size,Frame *out) const { return production() ? retirement_production::decodeV3(raw,size,out) : decodeV2(raw,size,out); }
    bool fail() { failed_=true; return false; }
    Id run_,session_; uint32_t cap_,lastRequest_=0; uint64_t deadline_,lastGuardReplacement_=0; Profile profile_;
    Channel channel_; PipeBinding in_{},out_{}; Retain retain_=nullptr; void *context_=nullptr;
    Totals totals_{}; Digest job_{},proof_{}; std::array<Digest,32> history_{}; size_t count_=0;
    Frame offer_{}; unsigned phase_=0; bool admitted_=false,active_=false,failed_=false,finished_=false;
};
}} // namespace om::guard_retirement_v2
