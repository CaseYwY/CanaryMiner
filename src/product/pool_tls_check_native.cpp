#include "pool_tls_connection.h"
#if defined(CANARY_POOL_TLS_CHECK)
#include <algorithm>
#include <memory>
#include <vector>
#if !defined(_WIN32)
#include <poll.h>
#endif
#endif
namespace om {
#if defined(CANARY_POOL_TLS_CHECK)
namespace {
using S=PoolTlsCheckStatus;using G=PoolTlsCheckStage;using E=PearlTlsError;using I=PearlPoolInterest;using W=PearlPoolWaitStatus;
uint64_t nowMs(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Socket final:PearlPoolTransport {
    socket_t fd=OM_INVALID_SOCKET;
    ~Socket()override{close();}
    PearlPoolConnectStatus open(const std::string &,int)override{return PearlPoolConnectStatus::ConnectFailure;}
    bool isOpen()const override{return fd!=OM_INVALID_SOCKET;}
    void close()override{if(isOpen())OM_CLOSESOCKET(fd);fd=OM_INVALID_SOCKET;}
    bool setNonblocking(bool on)override{
        if(!on||!isOpen())return false;
#if defined(_WIN32)
        u_long value=1;return ioctlsocket(fd,FIONBIO,&value)==0;
#else
        const int flags=fcntl(fd,F_GETFL,0);return flags>=0&&fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0;
#endif
    }
    W waitReadable(int ms)override{return waitFor(I::Read,ms);}
    W waitWritable(int ms)override{return waitFor(I::Write,ms);}
    W waitFor(I interest,int ms)override{
        if(!isOpen()||!validPearlPoolInterest(interest)||ms<0||ms>25)return W::Error;
#if defined(_WIN32)
        fd_set reads,writes,errors;FD_ZERO(&reads);FD_ZERO(&writes);FD_ZERO(&errors);FD_SET(fd,&errors);
        if((static_cast<unsigned>(interest)&1u)!=0)FD_SET(fd,&reads);
        if((static_cast<unsigned>(interest)&2u)!=0)FD_SET(fd,&writes);
        timeval timeout{0,ms*1000};const int r=select(0,&reads,&writes,&errors,&timeout);
        if(r==SOCKET_ERROR&&WSAGetLastError()==WSAEINTR)return W::Timeout;
#else
        pollfd poller{fd,static_cast<short>(((static_cast<unsigned>(interest)&1u)?POLLIN:0)|((static_cast<unsigned>(interest)&2u)?POLLOUT:0)),0};
        const int r=poll(&poller,1,ms);if(r<0&&errno==EINTR)return W::Timeout;
#endif
        return r>0?W::Ready:r==0?W::Timeout:W::Error; // connect observes SO_ERROR; TLS observes EOF/error via its next step
    }
    PearlPoolTransfer transfer(char *out,const char *in,std::size_t n){
        using T=PearlPoolTransferStatus;if(!isOpen()||!n||n>65536)return {T::Error,0};
#if defined(_WIN32)
        const int r=out ? ::recv(fd,out,static_cast<int>(n),0) : ::send(fd,in,static_cast<int>(n),0);
        if(r<0&&(WSAGetLastError()==WSAEWOULDBLOCK||WSAGetLastError()==WSAEINTR))return {T::WouldBlock,0};
#else
        const auto r=out ? ::recv(fd,out,n,0) : ::send(fd,in,n,MSG_NOSIGNAL);
        if(r<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return {T::WouldBlock,0};
#endif
        return r>0?PearlPoolTransfer{T::Bytes,static_cast<std::size_t>(r)}:PearlPoolTransfer{r==0?T::EndOfStream:T::Error,0};
    }
    PearlPoolTransfer receive(char *p,std::size_t n)override{return p?transfer(p,nullptr,n):PearlPoolTransfer{};}
    PearlPoolTransfer send(const char *p,std::size_t n)override{return p?transfer(nullptr,p,n):PearlPoolTransfer{};}
};
struct Platform {bool ok=platformInit();~Platform(){if(ok)platformShutdown();}};
struct Connected final:PoolTlsConnection {
    std::shared_ptr<Platform> platform;std::unique_ptr<Socket> socket;PearlTlsStream tls;
    Connected(std::shared_ptr<Platform> api,std::unique_ptr<Socket> raw,const std::atomic<bool> &cancel):platform(std::move(api)),socket(std::move(raw)),tls(*socket,cancel){}
    PearlPoolTransfer send(const char *p,std::size_t n)override{return tls.send(p,n,nowMs());}
    PearlPoolTransfer receive(char *p,std::size_t n)override{return tls.receive(p,n,nowMs());}
    PearlPoolWaitStatus waitFor(PearlPoolInterest i,unsigned ms)override{
        if(ms>25)return W::Error;
        if((i==I::Read||i==I::ReadWrite)&&tls.readProgressReady())return W::Ready;
        return socket->waitFor(i,static_cast<int>(ms));
    }
    bool bufferedRead()const override{return tls.bufferedRead();}
    PearlTlsState state()const override{return tls.state();}
    PearlTlsError error()const override{return tls.error();}
    void abort()override{tls.abort();}
};
bool pendingConnect(){
#if defined(_WIN32)
    const int e=WSAGetLastError();return e==WSAEWOULDBLOCK||e==WSAEINPROGRESS||e==WSAEALREADY;
#else
    return errno==EINPROGRESS||errno==EALREADY||errno==EINTR;
#endif
}
bool connected(socket_t fd){int error=0;
#if defined(_WIN32)
    int length=sizeof(error);return getsockopt(fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char *>(&error),&length)==0&&length==sizeof(error)&&!error;
#else
    socklen_t length=sizeof(error);return getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)==0&&length==sizeof(error)&&!error;
#endif
}
}
#endif
static PoolTlsCheckReply nativeOpen(const PoolTlsCheckRequest &r,const std::atomic<bool> &cancel,std::unique_ptr<PoolTlsConnection> &owned){
    using S=PoolTlsCheckStatus;
    PoolTlsCheckReply out;const auto result=[&](S s){out.status=s;return out;};
    if(!r.enabled)return result(S::Disabled);
    if(cancel.load())return result(S::Cancelled);
    if(!validPoolTlsCheckRequest(r))return result(S::Invalid);
#if !defined(CANARY_POOL_TLS_CHECK)
    (void)owned;
    return result(S::Unavailable);
#else
    using G=PoolTlsCheckStage;using E=PearlTlsError;
    const auto begin=nowMs();if(!begin||begin>UINT64_MAX-r.totalMs)return result(S::Invalid);const auto deadline=begin+r.totalMs;
    const auto expired=[&]{if(cancel.load()){out.status=S::Cancelled;return true;}if(nowMs()>=deadline){out.status=S::Timeout;return true;}return false;};
    auto platform=std::make_shared<Platform>();
    if(!platform->ok)return result(S::Unavailable);
    out.stage=G::Resolve;
    addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_TCP;hints.ai_flags=AI_NUMERICSERV;
    addrinfo *addresses=nullptr;const auto port=std::to_string(r.port);
    const int resolved=getaddrinfo(r.host.c_str(),port.c_str(),&hints,&addresses);
    std::unique_ptr<addrinfo,decltype(&freeaddrinfo)> list(addresses,freeaddrinfo);
    if(expired())return out;
    if(resolved||!addresses)return result(S::Dns);
    out.stage=G::Connect;std::unique_ptr<Socket> socket;std::vector<std::unique_ptr<Socket>> pending;
    bool madeSocket=false;unsigned attempts=0;auto *next=addresses;uint64_t nextAttempt=0;
    while(!socket){
        if(expired())return out;
        // A quiet first-family connect must not consume the entire shared deadline before another
        // address is attempted. Keep at most sixteen owned candidates; stagger starts, never renew the budget.
        for(auto i=pending.begin();i!=pending.end();){const auto ready=(*i)->waitWritable(0);
            if(ready==W::Timeout){++i;continue;}
            if(ready==W::Ready&&connected((*i)->fd)){socket=std::move(*i);pending.erase(i);break;}
            i=pending.erase(i);
        }
        if(socket)break;
        while(next&&((next->ai_family!=AF_INET&&next->ai_family!=AF_INET6)||next->ai_socktype!=SOCK_STREAM||!next->ai_addr||next->ai_addrlen>sizeof(sockaddr_storage)))next=next->ai_next;
        const auto current=nowMs();
        if(next&&attempts<16&&(pending.empty()||current>=nextAttempt)){
            auto *address=next;next=next->ai_next;++attempts;auto candidate=std::make_unique<Socket>();
#if defined(_WIN32)
            candidate->fd=WSASocketW(address->ai_family,SOCK_STREAM,IPPROTO_TCP,nullptr,0,WSA_FLAG_NO_HANDLE_INHERIT);
#else
            candidate->fd=::socket(address->ai_family,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,IPPROTO_TCP);
#endif
            if(!candidate->isOpen())continue;
            madeSocket=true;if(!candidate->setNonblocking(true))continue;
            if(expired())return out;
            if(::connect(candidate->fd,address->ai_addr,static_cast<int>(address->ai_addrlen))==0){socket=std::move(candidate);break;}
            if(!pendingConnect())continue;
            pending.push_back(std::move(candidate));nextAttempt=current+250;continue;
        }
        if(pending.empty()&&(!next||attempts>=16))break;
        const auto observed=nowMs();if(observed>=deadline)continue;
        uint64_t wait=std::min<uint64_t>(25,deadline-observed);
        if(next&&attempts<16&&nextAttempt>observed)wait=std::min(wait,nextAttempt-observed);
        std::this_thread::sleep_for(std::chrono::milliseconds(wait));
    }
    if(expired())return out;
    if(!socket)return result(madeSocket?S::Connect:S::Socket);
    pending.clear(); // losers never receive a ClientHello or application data
    out.stage=G::Handshake;auto retained=std::make_unique<Connected>(platform,std::move(socket),cancel);auto &tls=retained->tls;PearlTlsPolicy policy;
    policy.enabled=true;policy.serverName=r.host;policy.caPem=r.caPem;policy.windowsPrivateTrust=r.windowsPrivateTrust;policy.handshakeDeadlineMonoMs=deadline;
    const auto failed=[&]{if(expired())return out;out.tlsError=tls.error();if(out.tlsError==E::None)out.tlsError=E::Protocol;return result(S::Tls);};
    if(!tls.begin(policy,nowMs()))return failed();
    while(tls.state()==PearlTlsState::Handshaking){if(expired())return out;const auto step=tls.handshake(nowMs());
        if(step.status==PearlPoolTransferStatus::Error)return failed();
        if(step.status==PearlPoolTransferStatus::WouldBlock){if(expired())return out;const auto current=nowMs();if(current>=deadline)continue;
            if(retained->waitFor(step.interest,static_cast<unsigned>(std::min<uint64_t>(deadline-current,25)))==W::Error){out.tlsError=E::Transport;return result(S::Tls);}}
    }
    if(expired())return out;
    if(tls.state()!=PearlTlsState::VerifiedOpen)return failed();
    out.stage=G::Complete;owned=std::move(retained);return result(S::Verified);
#endif
}
PoolTlsOpenResult openPoolTlsConnection(const PoolTlsCheckRequest &r,const std::atomic<bool> &cancel){PoolTlsOpenResult result;result.reply=nativeOpen(r,cancel,result.connection);return result;}
PoolTlsCheckReply executePoolTlsCheck(const PoolTlsCheckRequest &r,const std::atomic<bool> &cancel){return openPoolTlsConnection(r,cancel).reply;} // temporary owner closes before the one-shot helper can publish
} // namespace om
