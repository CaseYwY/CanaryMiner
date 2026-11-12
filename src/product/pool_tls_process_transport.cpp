#include "pool_tls_process_transport.h"
#include <algorithm>
#include <cstring>
#include <thread>
namespace om {
namespace {
using O=PoolTlsRpcOperation;using T=PearlPoolTransferStatus;using I=PearlPoolInterest;using S=PoolTlsRpcState;using E=PearlTlsError;using W=PearlPoolWaitStatus;
uint64_t nowMs(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
}
struct PoolTlsProcessTransport::Impl {
    std::filesystem::path helper;PoolTlsCheckRequest config;const std::atomic<bool> &cancel;const volatile sig_atomic_t *stop;PoolTlsTransportMode mode;DuplexTaskProcess process;
    PoolTlsRpcGate gate;PoolTlsRpcFramer frames;std::optional<PoolTlsRpcReply> result;O resultOperation=O::Abort;
    bool attempted=false,connected=false,terminal=false,nativeBuffered=false;uint64_t sequence=0,last=0,openDeadline=0;E failure=E::None;S state=S::Dormant;
    std::string input,writeCopy;const char *writePointer=nullptr;std::size_t writeLength=0;
    std::optional<PoolTlsCheckReply> openFailure;
    Impl(std::filesystem::path path,PoolTlsCheckRequest r,const std::atomic<bool> &c,const volatile sig_atomic_t *s,PoolTlsTransportMode m):helper(std::move(path)),config(std::move(r)),cancel(c),stop(s),mode(m){}
    void fail(E error,bool nativeOpen=false){if(!terminal){if(!nativeOpen)openFailure.reset();failure=error;terminal=true;connected=false;state=S::Failed;gate.stop();process.requestStop();}}
    bool check(){if(terminal)return false;if(cancel.load()||(stop&&*stop)){fail(E::Cancelled);return false;}const auto now=nowMs();
        if(!now||now<last){fail(E::Clock);return false;}last=now;
        if((openDeadline&&!connected&&now>=openDeadline)||(gate.pending()&&now>=gate.pending()->deadlineMonoMs)){fail(E::Timeout);return false;}return true;}
    bool issue(O operation,std::string payload={},std::size_t maximum=0,I interest=I::None,unsigned wait=0,uint64_t deadline=0,uint64_t issued=0){
        if(!check()||gate.pending()||result||sequence==UINT64_MAX)return false;
        const auto now=nowMs();
        if(!deadline){if(now>UINT64_MAX-5000){fail(E::Clock);return false;}deadline=now+5000;}
        if(now>=deadline){fail(E::Timeout);return false;}
        PoolTlsRpcCommand command{sequence+1,issued?issued:now,deadline,operation,maximum,interest,wait,std::move(payload)};std::string wire;
        if(!encodePoolTlsRpcCommand(command,&wire)||!gate.issue(command,now)||!process.send(wire)){fail(E::Protocol);return false;}
        ++sequence;return true;
    }
    void pump(){if(!check())return;const auto poll=process.poll();
        if(poll.channelFault||poll.state==DuplexTaskState::Failed||poll.state==DuplexTaskState::Unreaped){fail(E::Transport);return;}
        if(!poll.bytes.empty()&&!frames.feed(poll.bytes,[&](std::string_view wire){
            if(result||!gate.pending())return false;
            PoolTlsRpcReply reply;resultOperation=gate.pending()->operation;
            if(!decodePoolTlsRpcReply(*gate.pending(),wire,&reply)||!gate.settle(reply,nowMs()))return false;
            state=reply.state;nativeBuffered=reply.bufferedRead;result=std::move(reply);return true;})){fail(E::Protocol);return;}
        // No second reply is permitted while the first is retained, including a partial trailing frame.
        if(result&&!frames.complete()){fail(E::Protocol);return;}
        if(poll.eof&&(!frames.complete()||gate.pending())){fail(E::Transport);return;}
        if(poll.state==DuplexTaskState::Exited&&poll.exitCode!=0){fail(E::Transport);return;} // successful exit still has a bounded unread pipe tail
        if(result&&result->state==S::Failed){const auto error=result->error;
            if(resultOperation==O::Open||resultOperation==O::OpenPlaintext){
                // A failed native opener must finish its bounded reply and exit cleanly before its
                // phase can authorize a different transport. The original Open budget still applies.
                if(!poll.eof||poll.state!=DuplexTaskState::Exited)return;
                openFailure=result->openFailure;
            }
            fail(error,true);return;}
    }
    // A pre-submit nonblocking read can still have a pending RPC when the canonical source switches to writing.
    // Preserve any returned plaintext exactly; do not replace it with a readiness hint or discard pushed work.
    bool auxiliaries(){if(!result)return true;
        if(resultOperation==O::Wait){result.reset();return true;}
        if(resultOperation==O::Read){if(result->transfer.status==T::EndOfStream){fail(E::Transport);return false;}
            if(result->transfer.status==T::Bytes){if(result->payload.size()>65536-input.size()){fail(E::Capacity);return false;}input+=result->payload;}
            result.reset();return true;}
        return false;
    }
};
PoolTlsProcessTransport::PoolTlsProcessTransport(std::filesystem::path helper,PoolTlsCheckRequest r,const std::atomic<bool> &cancel,const volatile sig_atomic_t *stop,PoolTlsTransportMode mode):impl_(new Impl(std::move(helper),std::move(r),cancel,stop,mode)){}
PoolTlsProcessTransport::~PoolTlsProcessTransport(){close();}
PearlPoolConnectStatus PoolTlsProcessTransport::open(const std::string &host,int port){return openUntil(host,port,0);}
PearlPoolConnectStatus PoolTlsProcessTransport::openUntil(const std::string &host,int port,uint64_t originalDeadlineMonoMs){auto &p=*impl_;if(p.attempted||p.terminal)return PearlPoolConnectStatus::ConnectFailure;p.attempted=true;
    if(!p.check()||!p.config.enabled||host!=p.config.host||port<1||static_cast<unsigned>(port)!=p.config.port||!validPoolTlsCheckRequest(p.config)||
        (p.mode!=PoolTlsTransportMode::Tls&&p.mode!=PoolTlsTransportMode::Plaintext)||
        (p.mode==PoolTlsTransportMode::Plaintext&&(!p.config.caPem.empty()||p.config.windowsPrivateTrust))){p.fail(E::InvalidPolicy);return PearlPoolConnectStatus::ConnectFailure;}
    const auto now=nowMs();if(now>UINT64_MAX-p.config.totalMs){p.fail(E::Clock);return PearlPoolConnectStatus::ConnectFailure;}
    const auto deadline=originalDeadlineMonoMs?std::min(now+p.config.totalMs,originalDeadlineMonoMs):now+p.config.totalMs;
    if(deadline<=now||deadline-now<50){p.fail(E::Timeout);return PearlPoolConnectStatus::ConnectFailure;}
    p.openDeadline=deadline;
    auto request=p.config;request.totalMs=static_cast<unsigned>(deadline-now);
    const auto operation=p.mode==PoolTlsTransportMode::Tls?O::Open:O::OpenPlaintext;
    std::string payload;if(!encodePoolTlsCheckRequest(request,localTaskProcessId(),&payload)||!p.process.start(p.helper,p.cancel)||!p.issue(operation,std::move(payload),0,I::None,0,deadline,now)){p.fail(E::Transport);return PearlPoolConnectStatus::ConnectFailure;}
    while(!p.terminal&&(!p.result||p.result->state==S::Failed)){p.pump();if(!p.terminal&&(!p.result||p.result->state==S::Failed))std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    if(p.terminal)return PearlPoolConnectStatus::ConnectFailure;
    if(!p.result||p.result->state!=(p.mode==PoolTlsTransportMode::Tls?S::VerifiedOpen:S::PlaintextOpen)){p.fail(E::Protocol);return PearlPoolConnectStatus::ConnectFailure;}
    p.result.reset();p.connected=true;p.openDeadline=0;return PearlPoolConnectStatus::Connected;
}
bool PoolTlsProcessTransport::isOpen()const{const auto &p=*impl_;return p.connected&&!p.terminal&&!p.cancel.load()&&!(p.stop&&*p.stop);} // pending reauthentication still owns the connected socket
void PoolTlsProcessTransport::close(){auto &p=*impl_;p.attempted=true;if(!p.terminal){p.terminal=true;p.connected=false;p.state=S::Closed;p.gate.stop();p.process.requestStop();}p.process.terminate();}
bool PoolTlsProcessTransport::bufferedRead()const{const auto &p=*impl_;return isOpen()&&(p.nativeBuffered||!p.input.empty()||
    (p.result&&p.resultOperation==O::Read&&p.result->transfer.status==T::Bytes));} // an outstanding IPC poll is not buffered TLS/application work
bool PoolTlsProcessTransport::localReadPending()const{const auto &p=*impl_;return isOpen()&&p.gate.pending()&&(p.gate.pending()->operation==O::Read||p.gate.pending()->operation==O::Wait);}
PearlPoolWaitStatus PoolTlsProcessTransport::waitLocalRead(int ms){auto &p=*impl_;if(ms<0||ms>25||!p.check())return W::Error;const auto deadline=nowMs()+static_cast<unsigned>(ms);
    do{p.pump();if(p.terminal)return W::Error;if(p.result){if(p.resultOperation==O::Wait){const auto ready=p.result->ready;p.result.reset();return ready==PoolTlsRpcReady::Ready?W::Ready:W::Timeout;}return W::Ready;}
        if(!localReadPending())return W::Timeout;
        if(nowMs()>=deadline)return W::Timeout;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(true);
}
bool PoolTlsProcessTransport::setNonblocking(bool enabled){return enabled&&isOpen();}
PearlTlsError PoolTlsProcessTransport::error()const{return impl_->failure;}
std::optional<PoolTlsCheckReply> PoolTlsProcessTransport::openFailure()const{
    const auto &p=*impl_;return p.cancel.load()||(p.stop&&*p.stop)?std::nullopt:p.openFailure;
}
PearlPoolTransfer PoolTlsProcessTransport::send(const char *data,std::size_t length){auto &p=*impl_;if(!p.check()||!p.connected||!data||!length)return {T::Error,0};
    const auto size=std::min(length,poolTlsRpcPayloadLimit);
    if(p.writePointer&&(p.writePointer!=data||p.writeLength!=length||p.writeCopy.size()!=size||std::memcmp(data,p.writeCopy.data(),size))){p.fail(E::Protocol);return {T::Error,0};}
    p.pump();if(p.terminal)return {T::Error,0};
    if(p.result&&p.resultOperation==O::Write){auto result=p.result->transfer;p.result.reset();if(result.status==T::Bytes){p.writeCopy.clear();p.writePointer=nullptr;p.writeLength=0;}return result;}
    if(!p.auxiliaries()&&p.result){p.fail(E::Protocol);return {T::Error,0};}
    if(p.terminal)return {T::Error,0};
    if(p.gate.pending())return {T::WouldBlock,0,I::Read};
    if(!p.writePointer){p.writePointer=data;p.writeLength=length;p.writeCopy.assign(data,size);}
    if(!p.issue(O::Write,p.writeCopy))return {T::Error,0};
    return {T::WouldBlock,0,I::Read};
}
PearlPoolTransfer PoolTlsProcessTransport::receive(char *out,std::size_t maximum){auto &p=*impl_;if(!p.check()||!p.connected||!out||!maximum)return {T::Error,0};
    p.pump();if(p.terminal)return {T::Error,0};
    if(p.result&&p.resultOperation==O::Wait)p.result.reset();
    if(p.result&&p.resultOperation==O::Read){auto result=p.result->transfer;if(result.status==T::Bytes){p.input+=p.result->payload;p.result.reset();}
        else{p.result.reset();if(result.status==T::EndOfStream){p.connected=false;p.terminal=true;}return result;}}
    if(!p.input.empty()){const auto n=std::min(maximum,p.input.size());std::memcpy(out,p.input.data(),n);p.input.erase(0,n);return {T::Bytes,n};}
    if(p.writePointer||p.result){p.fail(E::Protocol);return {T::Error,0};}
    if(p.gate.pending())return {T::WouldBlock,0,I::Read};
    if(!p.issue(O::Read,{},std::min(maximum,poolTlsRpcPayloadLimit)))return {T::Error,0};
    return {T::WouldBlock,0,I::Read};
}
PearlPoolWaitStatus PoolTlsProcessTransport::waitFor(I interest,int ms){auto &p=*impl_;if(!validPearlPoolInterest(interest)||ms<0||ms>25000||!p.check()||!p.connected)return W::Error;
    const auto begin=nowMs();const auto deadline=begin+static_cast<unsigned>(ms);bool once=false;
    for(;;){p.pump();if(p.terminal)return W::Error;
        if(!p.input.empty())return W::Ready;
        if(p.result){if(p.resultOperation!=O::Wait)return W::Ready;const auto ready=p.result->ready;p.result.reset();if(ready==PoolTlsRpcReady::Ready)return W::Ready;}
        const auto current=nowMs();if(once&&current>=deadline)return W::Timeout;
        if(!p.gate.pending()){const auto wait=static_cast<unsigned>(current<deadline?std::min<uint64_t>(25,deadline-current):0);if(!p.issue(O::Wait,{},0,interest,wait))return W::Error;}
        once=true;if(!ms)return W::Timeout;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
} // namespace om
