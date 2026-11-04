#include "pool_tls_rpc.h"
#include <array>
#include <charconv>
namespace om {
namespace {
using O=PoolTlsRpcOperation;using T=PearlPoolTransferStatus;using S=PearlTlsState;using E=PearlTlsError;using I=PearlPoolInterest;
void number(std::string &s,uint64_t n){s+=std::to_string(n);s+=' ';}
bool integer(std::string_view s,uint64_t maximum,uint64_t &out){
    if(s.empty()||s.size()>20||(s.size()>1&&s[0]=='0')||s[0]<'0'||s[0]>'9')return false;
    uint64_t n=0;const auto r=std::from_chars(s.data(),s.data()+s.size(),n);
    if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size()||n>maximum)return false;
    out=n;return true;
}
template<std::size_t N> bool fields(std::string_view wire,std::string_view magic,std::array<uint64_t,N> &values,std::string_view &payload){
    if(wire.size()>poolTlsRpcFrameLimit||wire.substr(0,magic.size())!=magic)return false;
    const auto end=wire.find('\n');if(end==wire.npos||end>256)return false;
    std::size_t at=magic.size();
    for(auto &v:values){const auto space=wire.find(' ',at);if(space==wire.npos||space>end||!integer(wire.substr(at,space-at),UINT64_MAX,v))return false;at=space+1;}
    if(at!=end)return false;
    payload=wire.substr(end+1);return true;
}
bool validReply(const PoolTlsRpcCommand &c,const PoolTlsRpcReply &r){
    if(!validPoolTlsRpcCommand(c)||r.sequence!=c.sequence||static_cast<unsigned>(r.state)>static_cast<unsigned>(S::Failed)||
        static_cast<unsigned>(r.error)>static_cast<unsigned>(E::Capacity)||static_cast<unsigned>(r.transfer.status)>static_cast<unsigned>(T::Progress))return false;
    const auto &t=r.transfer;
    if(static_cast<unsigned>(r.ready)>static_cast<unsigned>(PoolTlsRpcReady::Timeout)||
        (c.operation!=O::Wait&&r.ready!=PoolTlsRpcReady::NotWait))return false;
    if((r.state==S::Failed||r.state==S::Closed)&&(r.bufferedRead||r.ready!=PoolTlsRpcReady::NotWait))return false;
    if(r.state==S::Failed)return t.status==T::Error&&!t.bytes&&t.interest==I::None&&r.error!=E::None&&r.payload.empty();
    if(r.error!=E::None||r.state==S::Dormant||t.status==T::Error)return false;
    if(t.status==T::EndOfStream)return c.operation==O::Read&&r.state==S::Closed&&!t.bytes&&t.interest==I::None&&r.payload.empty();
    if(c.operation==O::Abort)return r.state==S::Closed&&t.status==T::Progress&&!t.bytes&&t.interest==I::None&&r.payload.empty();
    if(r.state!=S::VerifiedOpen&&r.state!=S::Handshaking)return false;
    if(c.operation==O::Open)return t.status==T::Progress&&!t.bytes&&t.interest==I::None&&r.state==S::VerifiedOpen&&r.payload.empty();
    if(c.operation==O::Wait)return r.ready!=PoolTlsRpcReady::NotWait&&t.status==T::Progress&&!t.bytes&&t.interest==I::None&&r.payload.empty();
    if(t.status==T::WouldBlock)return !t.bytes&&validPearlPoolInterest(t.interest)&&r.payload.empty();
    if(t.interest!=I::None)return false;
    if(t.status==T::Progress)return !t.bytes&&r.payload.empty();
    if(t.status!=T::Bytes||!t.bytes||r.state!=S::VerifiedOpen)return false;
    if(c.operation==O::Write)return t.bytes==c.payload.size()&&r.payload.empty();
    return c.operation==O::Read&&t.bytes<=c.maximum&&t.bytes==r.payload.size();
}
std::string wrap(const std::string &body){return std::to_string(body.size())+"\n"+body;}
}
bool validPoolTlsRpcCommand(const PoolTlsRpcCommand &c){
    if(!c.sequence||!c.issuedMonoMs||c.deadlineMonoMs<=c.issuedMonoMs||c.deadlineMonoMs-c.issuedMonoMs>60000||
        static_cast<unsigned>(c.operation)>static_cast<unsigned>(O::Abort)||c.payload.size()>localTaskRequestLimit)return false;
    if(c.operation==O::Open){PoolTlsCheckRequest request;uint64_t parent=0;
        return c.sequence==1&&!c.maximum&&c.interest==I::None&&!c.waitMs&&decodePoolTlsCheckRequest(c.payload,&request,&parent)&&request.enabled&&
            request.totalMs<=c.deadlineMonoMs-c.issuedMonoMs;}
    if(c.sequence==1)return false;
    if(c.operation==O::Write)return !c.payload.empty()&&c.payload.size()<=poolTlsRpcPayloadLimit&&!c.maximum&&c.interest==I::None&&!c.waitMs;
    if(!c.payload.empty())return false;
    if(c.operation==O::Read)return c.maximum&&c.maximum<=poolTlsRpcPayloadLimit&&c.interest==I::None&&!c.waitMs;
    if(c.operation==O::Wait)return !c.maximum&&validPearlPoolInterest(c.interest)&&c.waitMs<=25&&c.waitMs<c.deadlineMonoMs-c.issuedMonoMs;
    return !c.maximum&&c.interest==I::None&&!c.waitMs;
}
bool encodePoolTlsRpcCommand(const PoolTlsRpcCommand &c,std::string *out){
    if(!out||!validPoolTlsRpcCommand(c))return false;
    std::string s="CMTLSC1 ";for(uint64_t n:{c.sequence,c.issuedMonoMs,c.deadlineMonoMs,static_cast<uint64_t>(c.operation),static_cast<uint64_t>(c.maximum),static_cast<uint64_t>(c.interest),static_cast<uint64_t>(c.waitMs)})number(s,n);
    s+='\n';s+=c.payload;if(s.size()>poolTlsRpcFrameLimit)return false;*out=wrap(s);return true;
}
bool decodePoolTlsRpcCommand(std::string_view wire,PoolTlsRpcCommand *out){
    if(!out)return false;
    std::array<uint64_t,7> n{};std::string_view payload;if(!fields(wire,"CMTLSC1 ",n,payload)||n[3]>4||n[4]>poolTlsRpcPayloadLimit||n[5]>3||n[6]>25)return false;
    PoolTlsRpcCommand c{n[0],n[1],n[2],static_cast<O>(n[3]),static_cast<std::size_t>(n[4]),static_cast<I>(n[5]),static_cast<unsigned>(n[6]),std::string(payload)};
    if(!validPoolTlsRpcCommand(c))return false;
    *out=std::move(c);return true;
}
bool encodePoolTlsRpcReply(const PoolTlsRpcCommand &c,const PoolTlsRpcReply &r,std::string *out){
    if(!out||!validReply(c,r))return false;
    std::string s="CMTLSR1 ";
    for(uint64_t n:{r.sequence,static_cast<uint64_t>(r.transfer.status),static_cast<uint64_t>(r.transfer.bytes),static_cast<uint64_t>(r.transfer.interest),static_cast<uint64_t>(r.state),static_cast<uint64_t>(r.error),static_cast<uint64_t>(r.ready),r.bufferedRead?uint64_t{1}:uint64_t{0}})number(s,n);
    s+='\n';s+=r.payload;*out=wrap(s);return true;
}
bool decodePoolTlsRpcReply(const PoolTlsRpcCommand &c,std::string_view wire,PoolTlsRpcReply *out){
    if(!out)return false;
    std::array<uint64_t,8> n{};std::string_view payload;if(!fields(wire,"CMTLSR1 ",n,payload)||n[1]>4||n[2]>poolTlsRpcPayloadLimit||n[3]>3||n[4]>4||n[5]>static_cast<unsigned>(E::Capacity)||n[6]>2||n[7]>1)return false;
    PoolTlsRpcReply r{n[0],{static_cast<T>(n[1]),static_cast<std::size_t>(n[2]),static_cast<I>(n[3])},static_cast<S>(n[4]),static_cast<E>(n[5]),std::string(payload),static_cast<PoolTlsRpcReady>(n[6]),n[7]!=0};
    if(!validReply(c,r))return false;
    *out=std::move(r);return true;
}
bool PoolTlsRpcFramer::feed(std::string_view bytes,const std::function<bool(std::string_view)> &frame){
    if(failed_||!frame||bytes.size()>4096)return !(failed_=true);
    unsigned frames=0;
    for(char ch:bytes){if(buffer_.size()>=poolTlsRpcFrameLimit+8)return !(failed_=true);buffer_+=ch;
        if(!expected_){if(ch!='\n'){if(buffer_.size()>6||ch<'0'||ch>'9')return !(failed_=true);continue;}
            uint64_t n=0;if(!integer(std::string_view(buffer_).substr(0,buffer_.size()-1),poolTlsRpcFrameLimit,n)||!n)return !(failed_=true);
            expected_=static_cast<std::size_t>(n);buffer_.clear();continue;}
        if(buffer_.size()==expected_){if(++frames>16||!frame(buffer_))return !(failed_=true);buffer_.clear();expected_=0;}
    }
    return true;
}
bool PoolTlsRpcGate::time(uint64_t now){if(stopped_)return false;if(!now||now<last_||(pending_&&now>=pending_->deadlineMonoMs)){stop();return false;}last_=now;return true;}
bool PoolTlsRpcGate::issue(const PoolTlsRpcCommand &c,uint64_t now){
    if(!time(now))return false;
    if(pending_||!validPoolTlsRpcCommand(c)||c.issuedMonoMs>now||now>=c.deadlineMonoMs||sequence_==UINT64_MAX||c.sequence!=sequence_+1||
        (!attempted_&&c.operation!=O::Open)||(attempted_&&(!opened_||c.operation==O::Open))||
        (retryWrite_&&((c.operation==O::Write&&c.payload!=*retryWrite_)||c.operation==O::Read))){stop();return false;}
    attempted_=true;sequence_=c.sequence;pending_=c;settled_.reset();return true;
}
bool PoolTlsRpcGate::settle(const PoolTlsRpcReply &r,uint64_t now){
    if(!time(now))return false;
    if(!pending_||!validReply(*pending_,r)){stop();return false;}
    const bool open=pending_->operation==O::Open;
    if(pending_->operation==O::Write){if(r.transfer.status==T::Bytes)retryWrite_.reset();else if(r.state!=S::Failed)retryWrite_=pending_->payload;}
    settled_=r;pending_.reset();if(open)opened_=true;
    if(r.state==S::Failed||r.state==S::Closed)stopped_=true;
    return true;
}
void PoolTlsRpcGate::stop()noexcept{stopped_=true;opened_=false;settled_.reset();} // preserve the unresolved original; never replay it
} // namespace om
