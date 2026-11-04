// Explicit synthetic connection implementation behind the REAL duplex service. Never linked into a native worker.
#include "product/pool_tls_stream_service.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <thread>
namespace {
using namespace om;using T=PearlPoolTransferStatus;using I=PearlPoolInterest;using S=PearlTlsState;
std::string notification(){std::string header;const char *digits="0123456789abcdef";for(unsigned i=0;i<76;++i){const auto b=(i+1)&255;header+=digits[b>>4];header+=digits[b&15];}
    return "{\"id\":null,\"method\":\"mining.notify\",\"params\":{\"job_id\":\"rpc-job\",\"header\":\""+header+"\",\"target\":\""+std::string(60,'0')+"ffff\",\"height\":139053,\"cert_version\":3}}\n";}
struct Connection final:PoolTlsConnection {
    std::string host,input,lines;const char *pending=nullptr;std::size_t length=0;bool control=false,closed=false;
    explicit Connection(std::string name):host(std::move(name)){}
    PearlPoolTransfer send(const char *p,std::size_t n)override{
        if(host=="lost-write.test")throw std::runtime_error("owned synthetic write response lost");
        if(host=="lost-submit.test"&&std::string_view(p,n).find("mining.submit")!=std::string_view::npos)throw std::runtime_error("owned synthetic submit response lost");
        if(!pending){pending=p;length=n;control=true;return {T::WouldBlock,0,I::Write};}
        if(p!=pending||n!=length)return {T::Error,0};
        pending=nullptr;length=0;control=false;
        if(host=="echo.test")input.append(p,n);else{lines.append(p,n);for(std::size_t end;(end=lines.find('\n'))!=lines.npos;){const auto line=lines.substr(0,end);lines.erase(0,end+1);
            if(line.find("mining.authorize")!=line.npos)input+="{\"id\":1,\"result\":true,\"error\":null}\n"+notification();
            if(line.find("mining.submit")!=line.npos){const auto begin=line.find("\"id\":")+5;const auto finish=line.find_first_not_of("0123456789",begin);input+="{\"id\":"+line.substr(begin,finish-begin)+",\"result\":true,\"error\":null}\n";}}}
        return {T::Bytes,n};
    }
    PearlPoolTransfer receive(char *p,std::size_t maximum)override{if(closed)return {T::EndOfStream,0};if(input.empty())return {T::WouldBlock,0,I::Read};const auto n=std::min(maximum,input.size());std::memcpy(p,input.data(),n);input.erase(0,n);return {T::Bytes,n};}
    PearlPoolWaitStatus waitFor(I interest,unsigned ms)override{if(interest==I::Write||interest==I::ReadWrite||!input.empty())return PearlPoolWaitStatus::Ready;std::this_thread::sleep_for(std::chrono::milliseconds(ms));return PearlPoolWaitStatus::Timeout;}
    bool bufferedRead()const override{return !input.empty()||control;}
    S state()const override{return closed?S::Closed:control?S::Handshaking:S::VerifiedOpen;}
    PearlTlsError error()const override{return PearlTlsError::None;}
    void abort()override{closed=true;}
};
}
int main(int argc,char **argv){return om::runPoolTlsStreamService(argc,argv,[](const om::PoolTlsCheckRequest &r,const std::atomic<bool> &){
    if(r.host=="stall-open.test")std::this_thread::sleep_for(std::chrono::seconds(30));
    om::PoolTlsOpenResult result;result.reply={om::PoolTlsCheckStatus::Verified,om::PoolTlsCheckStage::Complete,om::PearlTlsError::None};result.connection=std::make_unique<Connection>(r.host);return result;});}
