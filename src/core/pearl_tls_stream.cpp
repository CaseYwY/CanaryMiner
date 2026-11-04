#include "pearl_tls_stream.h"
#include "pearl_tls_engine.h"
#include <algorithm>
#include <array>
#include <cstring>
namespace om {
namespace {
using S=PearlTlsState;using E=PearlTlsError;using T=PearlPoolTransferStatus;using I=PearlPoolInterest;using K=PearlTlsEngineStatus;
constexpr std::size_t recordLimit=16384,bufferLimit=65536;
}
struct PearlTlsStream::Impl {
    PearlPoolTransport &wire;const std::atomic<bool> &cancel;std::unique_ptr<PearlTlsEngine> native;
    S state=S::Dormant;E error=E::None;uint64_t last=0,deadline=0;bool attempted=false,adopted=false;
    bool handshakeDone=false,eof=false;unsigned handshakeSteps=0;std::size_t handshakeBytes=0;
    std::array<char,recordLimit> outgoing{},plain{},writeCopy{};
    std::size_t outputSize=0,outputOffset=0,plainSize=0,plainOffset=0,writeSize=0,writeAccepted=0;
    const char *writePointer=nullptr;std::size_t writeLength=0;
    explicit Impl(PearlPoolTransport &w,const std::atomic<bool> &c):wire(w),cancel(c){}
    ~Impl(){native.reset();} // release a pending native operation before its stable caller-owned arrays die
    PearlPoolTransfer fail(E reason)noexcept{
        if(state!=S::Failed){state=S::Failed;error=reason;}
        if(adopted){try{wire.close();}catch(...){}}return {T::Error,0};
    }
    bool check(uint64_t now)noexcept{
        if(state==S::Failed||state==S::Closed)return false;
        if(cancel.load()){fail(E::Cancelled);return false;}
        if(!now||now<last){fail(E::Clock);return false;}
        last=now;if(state==S::Handshaking&&now>=deadline){fail(E::Timeout);return false;}
        if(native&&!native->tick(now)){fail(native->error());return false;}return true;
    }
    bool outputPending()const{return outputSize!=outputOffset||(native&&native->outputSize()!=0);}
    PearlPoolTransfer flush(){
        if(cancel.load())return fail(E::Cancelled);
        if(outputSize==outputOffset){outputSize=outputOffset=0;
            if(!native->outputSize()){native->outputFlushed();return {T::Progress,0};}
            outputSize=native->takeOutput(outgoing.data(),outgoing.size());
            if(!outputSize)return fail(native->error());
            if(outputSize>outgoing.size())return fail(E::Protocol);
        }
        const auto remaining=state==S::Handshaking?8*bufferLimit-handshakeBytes:bufferLimit;
        if(!remaining)return fail(E::Capacity);
        const auto request=std::min(outputSize-outputOffset,remaining);
        const auto sent=wire.send(outgoing.data()+outputOffset,request);
        if(cancel.load())return fail(E::Cancelled);
        if(sent.status==T::Bytes){
            if(!sent.bytes||sent.bytes>request||sent.interest!=I::None)return fail(E::Transport);
            outputOffset+=sent.bytes;if(state==S::Handshaking)handshakeBytes+=sent.bytes;
            if(!outputPending())native->outputFlushed();
            return {T::Progress,0};
        }
        if(sent.bytes)return fail(E::Transport);
        if(sent.status==T::WouldBlock){const auto interest=sent.interest==I::None&&!wire.progressIO()?I::Write:sent.interest;
            if(!validPearlPoolInterest(interest))return fail(E::Transport);
            return {T::WouldBlock,0,interest};}
        if(sent.status==T::Progress&&sent.interest==I::None)return {T::Progress,0};
        return fail(E::Transport);
    }
    PearlPoolTransfer feed(){
        if(cancel.load())return fail(E::Cancelled);
        if(eof)return fail(E::Protocol);
        const auto space=native->inputCapacity();if(!space)return fail(E::Capacity);
        const auto remaining=state==S::Handshaking?8*bufferLimit-handshakeBytes:bufferLimit;if(!remaining)return fail(E::Capacity);
        std::array<char,4096> input;const auto maximum=std::min(input.size(),std::min(space,remaining));
        const auto got=wire.receive(input.data(),maximum);
        if(cancel.load())return fail(E::Cancelled);
        if(got.status==T::Bytes){
            if(!got.bytes||got.bytes>maximum||got.interest!=I::None)return fail(E::Transport);
            if(!native->input(input.data(),got.bytes))return fail(native->error());
            if(state==S::Handshaking)handshakeBytes+=got.bytes;
            return {T::Progress,0};
        }
        if(got.bytes)return fail(E::Transport);
        if(got.status==T::WouldBlock){const auto interest=got.interest==I::None&&!wire.progressIO()?I::Read:got.interest;
            if(!validPearlPoolInterest(interest))return fail(E::Transport);
            return {T::WouldBlock,0,interest};}
        if(got.status==T::Progress&&got.interest==I::None)return {T::Progress,0};
        if(got.status==T::EndOfStream&&got.interest==I::None){eof=true;if(!native->endInput())return fail(E::Protocol);return {T::Progress,0};}
        return fail(E::Transport);
    }
    PearlPoolTransfer retry(PearlTlsEngineStep step){
        if(cancel.load())return fail(E::Cancelled);
        if(step.bytes)return fail(E::Protocol);
        if(step.status==K::NeedRead){const auto flushed=flush();if(flushed.status==T::Error||flushed.status==T::WouldBlock||outputPending())return flushed;
            if(native->retryBufferedInput())return {T::Progress,0};
            return feed();}
        if(step.status==K::NeedWrite){if(!outputPending())return fail(E::Protocol);return flush();}
        if(step.status==K::Progress)return {T::Progress,0};
        if(step.status==K::Closed&&state==S::VerifiedOpen){state=S::Closed;wire.close();return {T::EndOfStream,0};}
        return fail(step.status==K::Error?native->error():E::Protocol);
    }
};
PearlTlsStream::PearlTlsStream(PearlPoolTransport &w,const std::atomic<bool> &cancel):impl_(new Impl(w,cancel)){}
PearlTlsStream::~PearlTlsStream(){abort();}
bool PearlTlsStream::begin(const PearlTlsPolicy &p,uint64_t now)noexcept{
    auto &s=*impl_;
    try{
        if(s.attempted){s.fail(E::InvalidPolicy);return false;}s.attempted=true;
        if(!p.enabled){s.fail(E::Disabled);return false;}
        if(p.serverName.empty()||p.serverName.size()>253||p.serverName.find('\0')!=p.serverName.npos||p.caPem.size()>bufferLimit||
            !now||p.handshakeDeadlineMonoMs<=now||p.handshakeDeadlineMonoMs-now>60000){s.fail(E::InvalidPolicy);return false;}
        s.adopted=true;s.state=S::Handshaking;s.deadline=p.handshakeDeadlineMonoMs;if(!s.check(now))return false;
        E error=E::Unavailable;
#if defined(_WIN32)
        s.native=createPearlSchannelEngine(p,s.cancel,&error);
#else
        s.native=createPearlOpenSslEngine(p,&error);
#endif
        if(s.cancel.load()){s.fail(E::Cancelled);return false;}
        if(!s.native){s.fail(error);return false;}
        if(!s.wire.isOpen()||!s.wire.setNonblocking(true)){s.fail(E::Transport);return false;}
        if(s.cancel.load()){s.fail(E::Cancelled);return false;}return true;
    }catch(...){s.fail(E::Protocol);return false;}
}
PearlPoolTransfer PearlTlsStream::handshake(uint64_t now)noexcept{
    auto &s=*impl_;
    try{
        if(!s.check(now))return {T::Error,0};
        if(s.state!=S::Handshaking)return s.fail(E::Protocol);
        if(++s.handshakeSteps>8192)return s.fail(E::Capacity);
        auto result=s.flush();if(result.status==T::Error||result.status==T::WouldBlock||s.outputPending())return result;
        if(!s.handshakeDone){const auto step=s.native->handshake();if(step.status!=K::Complete||step.bytes)return s.retry(step);
            s.handshakeDone=true;result=s.flush();if(result.status==T::Error||result.status==T::WouldBlock||s.outputPending())return result;}
        if(s.cancel.load())return s.fail(E::Cancelled);
        s.state=S::VerifiedOpen;return {T::Progress,0};
    }catch(...){return s.fail(E::Protocol);}
}
PearlPoolTransfer PearlTlsStream::send(const char *data,std::size_t length,uint64_t now)noexcept{
    auto &s=*impl_;
    try{
        if(!s.check(now))return {T::Error,0};
        if(s.state!=S::VerifiedOpen||!data||!length)return s.fail(E::Protocol);
        if(s.writePointer&&(data!=s.writePointer||length!=s.writeLength||std::memcmp(data,s.writeCopy.data(),s.writeSize)!=0))return s.fail(E::Protocol);
        auto flushed=s.flush();if(flushed.status==T::Error||flushed.status==T::WouldBlock||s.outputPending())return flushed;
        if(!s.writePointer){s.writePointer=data;s.writeLength=length;s.writeSize=std::min(length,std::min(recordLimit,s.native->maximumPlaintext()));
            if(!s.writeSize)return s.fail(E::Unavailable);
            std::memcpy(s.writeCopy.data(),data,s.writeSize);}
        if(!s.writeAccepted){const auto step=s.native->send(s.writeCopy.data(),s.writeSize);if(step.status!=K::Bytes)return s.retry(step);
            if(step.bytes!=s.writeSize)return s.fail(E::Protocol);
            s.writeAccepted=step.bytes;flushed=s.flush();
            if(flushed.status==T::Error||flushed.status==T::WouldBlock||s.outputPending())return flushed;}
        if(s.cancel.load())return s.fail(E::Cancelled);
        const auto accepted=s.writeAccepted;s.writePointer=nullptr;s.writeSize=s.writeLength=s.writeAccepted=0;return {T::Bytes,accepted};
    }catch(...){return s.fail(E::Protocol);}
}
PearlPoolTransfer PearlTlsStream::receive(char *out,std::size_t maximum,uint64_t now)noexcept{
    auto &s=*impl_;
    try{
        if(!s.check(now))return {T::Error,0};
        if(s.state!=S::VerifiedOpen||!out||!maximum)return s.fail(E::Protocol);
        if(s.writePointer)return s.fail(E::Protocol);
        auto flushed=s.flush();if(flushed.status==T::Error||flushed.status==T::WouldBlock||s.outputPending())return flushed;
        if(s.plainOffset==s.plainSize){const auto step=s.native->receive(s.plain.data(),s.plain.size());if(step.status!=K::Bytes)return s.retry(step);
            if(!step.bytes||step.bytes>s.plain.size())return s.fail(E::Protocol);
            s.plainOffset=0;s.plainSize=step.bytes;flushed=s.flush();
            if(flushed.status==T::Error||flushed.status==T::WouldBlock||s.outputPending())return flushed;}
        if(s.cancel.load())return s.fail(E::Cancelled);
        const auto count=std::min(maximum,s.plainSize-s.plainOffset);std::memcpy(out,s.plain.data()+s.plainOffset,count);s.plainOffset+=count;return {T::Bytes,count};
    }catch(...){return s.fail(E::Protocol);}
}
bool PearlTlsStream::bufferedRead()const noexcept{const auto &s=*impl_;return s.state==S::VerifiedOpen&&(s.plainOffset!=s.plainSize||s.outputPending()||s.eof||s.native->bufferedRead());}
bool PearlTlsStream::readProgressReady()const noexcept{const auto &s=*impl_;return s.state==S::VerifiedOpen&&
    (s.plainOffset!=s.plainSize||s.eof||s.native->retryBufferedInput());}
PearlTlsState PearlTlsStream::state()const noexcept{const auto &s=*impl_;return s.state==S::VerifiedOpen&&!s.native->verified()?S::Handshaking:s.state;}
PearlTlsError PearlTlsStream::error()const noexcept{return impl_->error;}
void PearlTlsStream::abort()noexcept{auto &s=*impl_;s.attempted=true;if(s.state!=S::Closed&&s.state!=S::Failed){s.state=S::Closed;if(s.adopted){try{s.wire.close();}catch(...){}}}}
} // namespace om
