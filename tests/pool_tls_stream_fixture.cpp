// Explicit synthetic connection implementation behind the REAL duplex service. Never linked into a native worker.
#include "product/pool_tls_stream_service.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <thread>
namespace {
using namespace om;using T=PearlPoolTransferStatus;using I=PearlPoolInterest;using S=PoolTlsRpcState;
std::filesystem::path fixtureDirectory;
void recordPlainOpen(const PoolTlsCheckRequest &request){
    const auto directory=fixtureDirectory/(".tls-runtime-"+request.host);
    if(!std::filesystem::is_directory(directory))throw std::runtime_error("owned stage-marker directory missing");
    const auto temporary=directory/"plain-open.tmp",published=directory/"plain-open";
    {std::ofstream marker(temporary,std::ios::binary|std::ios::trunc);marker<<request.totalMs<<'\n';
        marker.close();if(!marker)throw std::runtime_error("owned stage-marker publication failed");}
    std::filesystem::rename(temporary,published);
}
std::string notification(){std::string header;const char *digits="0123456789abcdef";for(unsigned i=0;i<76;++i){const auto b=(i+1)&255;header+=digits[b>>4];header+=digits[b&15];}
    return "{\"id\":null,\"method\":\"mining.notify\",\"params\":{\"job_id\":\"rpc-job\",\"header\":\""+header+"\",\"target\":\""+std::string(60,'0')+"ffff\",\"height\":139053,\"cert_version\":3}}\n";}
struct Connection final:PoolTlsConnection {
    std::string host,input,lines;const char *pending=nullptr;std::size_t length=0;bool control=false,closed=false,plain=false;
    explicit Connection(std::string name,bool plaintext=false):host(std::move(name)),plain(plaintext){}
    PearlPoolTransfer send(const char *p,std::size_t n)override{
        if(host=="lost-write.test")throw std::runtime_error("owned synthetic write response lost");
        if((host=="lost-submit.test"||host=="fallback-lost-submit.test")&&std::string_view(p,n).find("mining.submit")!=std::string_view::npos)throw std::runtime_error("owned synthetic submit response lost");
        if(!plain){if(!pending){pending=p;length=n;control=true;return {T::WouldBlock,0,I::Write};}
            if(p!=pending||n!=length)return {T::Error,0};
            pending=nullptr;length=0;control=false;
        }else n=std::min<std::size_t>(n,4093); // genuine positive partial writes exercise the service's retained offset
        if(host=="echo.test")input.append(p,n);else{lines.append(p,n);for(std::size_t end;(end=lines.find('\n'))!=lines.npos;){const auto line=lines.substr(0,end);lines.erase(0,end+1);
            if(line.find("mining.authorize")!=line.npos)input+="{\"id\":1,\"result\":true,\"error\":null}\n"+notification();
            if(line.find("mining.submit")!=line.npos){const auto begin=line.find("\"id\":")+5;const auto finish=line.find_first_not_of("0123456789",begin);input+="{\"id\":"+line.substr(begin,finish-begin)+",\"result\":true,\"error\":null}\n";}}}
        return {T::Bytes,n};
    }
    PearlPoolTransfer receive(char *p,std::size_t maximum)override{if(closed||host=="closed-read.test"){closed=true;return {T::EndOfStream,0};}if(input.empty())return {T::WouldBlock,0,I::Read};const auto n=std::min(maximum,input.size());std::memcpy(p,input.data(),n);input.erase(0,n);return {T::Bytes,n};}
    PearlPoolWaitStatus waitFor(I interest,unsigned ms)override{if(interest==I::Write||interest==I::ReadWrite||!input.empty())return PearlPoolWaitStatus::Ready;std::this_thread::sleep_for(std::chrono::milliseconds(ms));return PearlPoolWaitStatus::Timeout;}
    bool bufferedRead()const override{return !input.empty()||control;}
    S state()const override{return closed?S::Closed:plain?S::PlaintextOpen:control?S::Handshaking:S::VerifiedOpen;}
    PearlTlsError error()const override{return PearlTlsError::None;}
    void abort()override{closed=true;}
};
PoolTlsOpenResult open(const PoolTlsCheckRequest &r,bool plain){
    const bool budget=r.host.rfind("budget-fallback-",0)==0;
    const bool cancelFallback=r.host.rfind("cancel-fallback-",0)==0;
    const bool observeReconnect=r.host.rfind("observe-reconnect-",0)==0;
    if(plain&&observeReconnect)recordPlainOpen(r);
    if(plain&&(budget||cancelFallback)){
        recordPlainOpen(r); // publication proves this exact helper entered the plaintext factory
        std::this_thread::sleep_for(std::chrono::milliseconds(budget?3500:30000));
    }
    if(r.host=="stall-open.test")std::this_thread::sleep_for(std::chrono::seconds(30));
    PoolTlsOpenResult result;
    if(!plain){using C=PoolTlsCheckStatus;using G=PoolTlsCheckStage;using E=PearlTlsError;
        if(r.host=="lost-open.test")throw std::runtime_error("owned synthetic open response lost");
        if(r.host=="verify-fail.test")result.failure=PoolTlsCheckReply{C::Tls,G::Handshake,E::Verification};
        if(r.host=="dns-fail.test")result.failure=PoolTlsCheckReply{C::Dns,G::Resolve,E::None};
        if(budget)std::this_thread::sleep_for(std::chrono::milliseconds(3500));
        if(budget||cancelFallback||r.host=="connect-fail.test"||r.host=="fallback-lost-submit.test")result.failure=PoolTlsCheckReply{C::Connect,G::Connect,E::None};
        if(observeReconnect&&!std::filesystem::is_regular_file(fixtureDirectory/(".tls-runtime-"+r.host)/"plain-open"))
            result.failure=PoolTlsCheckReply{C::Connect,G::Connect,E::None};
        if(r.host=="network-timeout.test")result.failure=PoolTlsCheckReply{C::Timeout,G::Connect,E::None};
        if(r.host=="handshake-timeout.test")result.failure=PoolTlsCheckReply{C::Timeout,G::Handshake,E::None};
        if(r.host=="unavailable-open.test")result.failure=PoolTlsCheckReply{C::Unavailable,G::None,E::None};
        if(result.failure)return result;
    }
    result.connection=std::make_unique<Connection>(r.host,r.host=="wrong-mode.test"?!plain:plain);return result;
}
}
int main(int argc,char **argv){fixtureDirectory=std::filesystem::absolute(std::filesystem::u8path(argv[0])).parent_path();
    return om::runPoolTlsStreamService(argc,argv,
    [](const om::PoolTlsCheckRequest &r,const std::atomic<bool> &){return open(r,false);},
    [](const om::PoolTlsCheckRequest &r,const std::atomic<bool> &){return open(r,true);});}
