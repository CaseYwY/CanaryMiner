#include "billing_input_model.h"
#include "hash.h"
#include <algorithm>
#include <chrono>
namespace om {
namespace billing_input_detail {
using S=BillingStatus;using R=BillingRecoveryState;using P=BillingInputPhase;using A=BillingInputAction;using M=BillingInputMode;
template<class T>bool number(const JsonValue &v,T &out,uint64_t cap){uint64_t n=0;if(!v.asUint64(&n)||n>cap)return false;out=static_cast<T>(n);return true;}
bool hash(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool valid(const BillingInputManifest &m){const auto p=m.file.u8string();return m.file.is_absolute()&&p.size()<=4096&&p.find('\0')==std::string::npos&&
    m.bindingBytes>0&&m.bindingBytes<=billing_detail::bindingLimit&&m.chunkBytes>=4&&m.chunkBytes<=billing_detail::chunkLimit&&hash(m.bindingDigest)&&hash(m.chunkDigest)&&billing_detail::instant(m.preparedUtcMs)&&
    (m.mode==M::CreateNew?(m.chunkBytes==4&&m.chunkDigest==sha256Hex("null")&&m.priorRevision.empty()&&!m.priorSequence&&m.priorDigest==m.bindingDigest):
     m.mode==M::Submit&&hash(m.priorRevision)&&m.priorSequence<4096&&hash(m.priorDigest));}
bool valid(const BillingInputRequest &r){
    if(!r.generation||!r.ticket||!valid(r.manifest)||static_cast<unsigned>(r.action)>static_cast<unsigned>(A::Reconcile)||
        (!r.expectedRevision.empty()&&!hash(r.expectedRevision)))return false;
    const auto total=r.manifest.bindingBytes+r.manifest.chunkBytes;
    if(r.action==A::Begin)return r.offset==0&&r.data.empty();
    if(r.action==A::Inspect)return r.data.empty()&&r.offset<=total&&r.offset%billingInputPageBytes==0;
    if(!hash(r.expectedRevision))return false;
    if(r.action==A::Append)return r.offset<total&&r.offset%billingInputPageBytes==0&&r.data.size()==std::min(billingInputPageBytes,total-r.offset);
    return !r.offset&&r.data.empty();
}
std::string manifestText(const BillingInputManifest &m){return "["+std::to_string(static_cast<unsigned>(m.mode))+","+jsonQuote(m.file.u8string())+","+std::to_string(m.bindingBytes)+","+
    std::to_string(m.chunkBytes)+","+jsonQuote(m.bindingDigest)+","+jsonQuote(m.chunkDigest)+","+std::to_string(m.preparedUtcMs)+","+jsonQuote(m.priorRevision)+","+
    std::to_string(m.priorSequence)+","+jsonQuote(m.priorDigest)+"]";}
bool parseManifest(const JsonValue &v,BillingInputManifest &out){
    if(!v.isArray()||v.items.size()!=10)return false;
    const auto &a=v.items;BillingInputManifest m;
    if(!number(a[0],m.mode,1)||!a[1].isString()||!number(a[2],m.bindingBytes,billing_detail::bindingLimit)||!number(a[3],m.chunkBytes,billing_detail::chunkLimit)||
        !a[4].isString()||!a[5].isString()||!number(a[6],m.preparedUtcMs,billing_detail::lastUtc)||!a[7].isString()||!number(a[8],m.priorSequence,4096)||!a[9].isString())return false;
    m.file=std::filesystem::u8path(a[1].text);m.bindingDigest=a[4].text;m.chunkDigest=a[5].text;m.priorRevision=a[7].text;m.priorDigest=a[9].text;
    if(!valid(m))return false;
    out=std::move(m);return true;
}
std::string certificateText(const std::optional<BillingInputCertificate> &c){return c?"["+std::to_string(static_cast<unsigned>(c->state))+","+std::to_string(static_cast<unsigned>(c->result))+","+
    jsonQuote(c->revision)+","+jsonQuote(c->digest)+","+(c->applied?std::to_string(static_cast<unsigned>(*c->applied)):"null")+","+jsonQuote(c->committed)+"]":"null";}
bool parseCertificate(const JsonValue &v,std::optional<BillingInputCertificate> &out){
    if(v.type==JsonValue::Type::Null){out.reset();return true;}
    if(!v.isArray()||v.items.size()!=6)return false;
    const auto &a=v.items;BillingInputCertificate c;
    if(!number(a[0],c.state,static_cast<unsigned>(R::Rejected))||c.state==R::Prepared||!number(a[1],c.result,static_cast<unsigned>(S::Storage))||
        !a[2].isString()||!hash(a[2].text)||!a[3].isString()||!hash(a[3].text)||!a[5].isString()||a[5].text.size()>2048)return false;
    if(a[4].type!=JsonValue::Type::Null){S applied;if(!number(a[4],applied,static_cast<unsigned>(S::Calculation)))return false;c.applied=applied;}
    c.revision=a[2].text;c.digest=a[3].text;c.committed=a[5].text;out=std::move(c);return true;
}
std::string frame(const std::string &s){return std::to_string(s.size())+"\n"+s+"\n";}
bool part(const std::string &s,size_t &offset,size_t cap,std::string &out){
    const auto end=s.find('\n',offset);if(end==std::string::npos||end==offset||end-offset>10||(end-offset>1&&s[offset]=='0'))return false;
    size_t n=0;for(size_t i=offset;i<end;++i){const auto c=s[i];if(c<'0'||c>'9'||n>(cap-static_cast<unsigned>(c-'0'))/10)return false;n=n*10+static_cast<unsigned>(c-'0');}
    offset=end+1;if(n>cap||n>=s.size()-offset||s[offset+n]!='\n')return false;out=s.substr(offset,n);offset+=n+1;return true;
}
std::string stageBody(const BillingInputManifest &m,P phase,const std::optional<BillingInputCertificate> &c,const std::string &payload){
    return "CMBI1\n"+frame(manifestText(m))+frame("["+std::to_string(static_cast<unsigned>(phase))+","+certificateText(c)+"]")+frame(payload);
}
bool proof(const BillingInputManifest &m,const BillingBinding &b,const BillingLimits &limits,const std::optional<BillingChunk> &chunk,const BillingInputCertificate &c,BillingRecoverySnapshot *out){
    using namespace billing_detail;
    if(c.state==R::Unavailable)return m.mode==M::Submit&&c.result==S::Conflict&&!c.applied&&c.revision==m.priorRevision&&c.digest==m.priorDigest&&c.committed.empty();
    BillingRecoverySnapshot s;s.status=S::Ok;s.state=c.state;s.applied=c.applied;s.revision=c.revision;s.priorSequence=m.priorSequence;s.priorDigest=m.priorDigest;s.recordedUtcMs=m.preparedUtcMs;s.original=chunk;
    if(!parseState(c.committed,b,limits,m.preparedUtcMs,s.committed))return false;
    s.committed.digest=c.digest;
    if(m.mode==M::CreateNew){if(c.state!=R::Empty||c.result!=S::Ok||c.applied||chunk||s.committed.sequence||c.digest!=m.bindingDigest)return false;}
    else if(c.state==R::Confirmed){if(!chunk||!c.applied||(*c.applied!=S::Ok&&*c.applied!=S::Replay)||c.result!=*c.applied||s.committed.sequence!=chunk->sequence||
        s.committed.batchId!=chunk->id||c.digest!=sha256Hex(m.priorDigest+"|"+chunkText(*chunk)+"|"+c.committed))return false;}
    else if(c.state==R::Rejected){if(!chunk||!c.applied||*c.applied==S::Ok||*c.applied==S::Replay||c.result!=*c.applied||s.committed.sequence!=m.priorSequence||c.digest!=m.priorDigest)return false;}
    else return false;
    if(billingJournalRevision(m.file,bindingText(b,limits),s)!=c.revision)return false;
    if(out)*out=std::move(s);
    return true;
}
std::string identity(const BillingInputRequest &r){return sha256Hex(std::to_string(static_cast<unsigned>(r.action))+"|"+manifestText(r.manifest)+"|"+r.expectedRevision+"|"+std::to_string(r.offset)+"|"+sha256Hex(r.data));}
} // namespace billing_input_detail
using namespace billing_input_detail;
bool prepareBillingInput(const BillingInputPlan &p,BillingInputManifest *out,std::string *payload){
    using namespace billing_detail;
    const auto &r=p.context;
    if(!out||!payload||!r.generation||!r.ticket||validateBinding(r.binding,r.limits,r.nowUtcMs)!=S::Ok||p.chunk.has_value()!=p.prior.has_value())return false;
    const auto bound=bindingText(r.binding,r.limits);const auto original=p.chunk?chunkText(*p.chunk):"null";
    BillingInputManifest m;m.file=r.file;m.bindingBytes=bound.size();m.chunkBytes=original.size();m.bindingDigest=sha256Hex(bound);m.chunkDigest=sha256Hex(original);m.preparedUtcMs=r.nowUtcMs;m.priorDigest=m.bindingDigest;
    if(p.chunk){const auto &s=*p.prior;BillingSnapshot parsed;BillingChunk chunk;
        if(!boundedChunk(*p.chunk)||!parseChunk(original,r.binding.evidence,chunk)||s.status!=S::Ok||s.committed.status!=S::Ok||
            (s.state!=R::Empty&&s.state!=R::Confirmed&&s.state!=R::Rejected)||s.revision!=billingJournalRevision(r.file,bound,s)||!hash(s.committed.digest)||
            !parseState(stateText(s.committed),r.binding,r.limits,r.nowUtcMs,parsed)||s.recordedUtcMs>r.nowUtcMs||chunk.sequence!=s.committed.sequence+1||
            chunk.observedUtcMs>r.nowUtcMs||(chunk.receipt&&chunk.receipt->request.source.nowUtcMs>r.nowUtcMs))return false;
        m.mode=M::Submit;m.priorRevision=s.revision;m.priorSequence=s.committed.sequence;m.priorDigest=s.committed.digest;
    }
    if(!valid(m))return false;
    *out=std::move(m);*payload=bound+original;return true;
}
bool encodeBillingInputRequest(const BillingInputRequest &r,uint64_t parent,std::string *wire){
    if(!wire||!valid(r)||!parent||parent>UINT32_MAX)return false;
    const auto header="[\"CMBIQ1\","+std::to_string(parent)+","+std::to_string(r.generation)+","+std::to_string(r.ticket)+","+std::to_string(static_cast<unsigned>(r.action))+","+
        manifestText(r.manifest)+","+jsonQuote(r.expectedRevision)+","+std::to_string(r.offset)+","+std::to_string(r.data.size())+"]\n";
    if(header.size()>manifestLimit)return false;
    *wire=header+r.data;return wire->size()<=manifestLimit+billingInputPageBytes;
}
bool decodeBillingInputRequest(const std::string &wire,BillingInputRequest *out,uint64_t *parent){
    const auto end=wire.find('\n');JsonValue root;
    if(!out||!parent||wire.size()>manifestLimit+billingInputPageBytes||end==std::string::npos||end>manifestLimit||!parseJsonStrict(wire.substr(0,end),&root,nullptr)||!root.isArray()||root.items.size()!=9)return false;
    const auto &a=root.items;BillingInputRequest r;uint64_t pid=0;size_t count=0;
    if(!a[0].isString()||a[0].text!="CMBIQ1"||!number(a[1],pid,UINT32_MAX)||!number(a[2],r.generation,UINT64_MAX)||!number(a[3],r.ticket,UINT64_MAX)||
        !number(a[4],r.action,4)||!parseManifest(a[5],r.manifest)||!a[6].isString()||!number(a[7],r.offset,billingInputPayloadLimit)||!number(a[8],count,billingInputPageBytes)||count!=wire.size()-end-1)return false;
    r.expectedRevision=a[6].text;r.data=wire.substr(end+1);std::string canonical;
    if(!encodeBillingInputRequest(r,pid,&canonical)||canonical!=wire)return false;
    *out=std::move(r);*parent=pid;return true;
}
bool encodeBillingInputReply(const BillingInputRequest &r,const BillingInputReply &s,std::string *wire){
    if(!wire||!valid(r)||static_cast<unsigned>(s.status)>static_cast<unsigned>(S::Storage)||s.data.size()>billingInputPageBytes)return false;
    if(s.status==S::Ok){if(s.phase==P::Unavailable||static_cast<unsigned>(s.phase)>3||!hash(s.revision)||!hash(s.payloadDigest)||s.received>r.manifest.bindingBytes+r.manifest.chunkBytes||
        (s.phase==P::Complete)!=s.certificate.has_value()||(s.phase!=P::Receiving&&s.received!=r.manifest.bindingBytes+r.manifest.chunkBytes)||
        (r.action==A::Inspect?(r.offset>s.received||s.data.size()!=std::min(billingInputPageBytes,s.received-r.offset)):!s.data.empty()))return false;}
    else if(s.phase!=P::Unavailable||!s.revision.empty()||!s.payloadDigest.empty()||s.received||s.certificate||!s.data.empty())return false;
    const auto header="[\"CMBIA1\","+std::to_string(r.generation)+","+std::to_string(r.ticket)+","+jsonQuote(identity(r))+","+std::to_string(static_cast<unsigned>(s.status))+","+
        std::to_string(static_cast<unsigned>(s.phase))+","+jsonQuote(s.revision)+","+std::to_string(s.received)+","+jsonQuote(s.payloadDigest)+","+certificateText(s.certificate)+","+std::to_string(s.data.size())+"]\n";
    if(header.size()>8192)return false;
    *wire=header+s.data;return wire->size()<=localTaskResponseLimit;
}
bool decodeBillingInputReply(const BillingInputRequest &r,const std::string &wire,BillingInputReply *out){
    const auto end=wire.find('\n');JsonValue root;
    if(!out||wire.size()>localTaskResponseLimit||end==std::string::npos||end>8192||!parseJsonStrict(wire.substr(0,end),&root,nullptr)||!root.isArray()||root.items.size()!=11)return false;
    const auto &a=root.items;BillingInputReply s;uint64_t generation=0,ticket=0;size_t count=0;
    if(!a[0].isString()||a[0].text!="CMBIA1"||!number(a[1],generation,UINT64_MAX)||generation!=r.generation||!number(a[2],ticket,UINT64_MAX)||ticket!=r.ticket||
        !a[3].isString()||a[3].text!=identity(r)||!number(a[4],s.status,static_cast<unsigned>(S::Storage))||!number(a[5],s.phase,3)||!a[6].isString()||
        !number(a[7],s.received,billingInputPayloadLimit)||!a[8].isString()||!parseCertificate(a[9],s.certificate)||!number(a[10],count,billingInputPageBytes)||count!=wire.size()-end-1)return false;
    s.revision=a[6].text;s.payloadDigest=a[8].text;s.data=wire.substr(end+1);std::string canonical;
    if(!encodeBillingInputReply(r,s,&canonical)||canonical!=wire)return false;
    *out=std::move(s);return true;
}
namespace {
BillingInputResult flight(LocalTaskProcess &process,const BillingInputRequest &r,const std::atomic<bool> &cancel,unsigned timeout){
    BillingInputResult result;std::string wire;
    if(!encodeBillingInputRequest(r,localTaskProcessId(),&wire)){result.transport=LocalTaskStatus::Invalid;return result;}
    const auto received=process.run(wire,cancel,timeout);result.transport=received.status;result.childStarted=received.childStarted;result.commitIssued=received.childStarted&&r.action==A::Commit;
    if(received.status!=LocalTaskStatus::Ok)return result;
    BillingInputReply parsed;if(!decodeBillingInputReply(r,received.response,&parsed)){result.transport=LocalTaskStatus::Unavailable;return result;}
    if(cancel.load()){result.transport=LocalTaskStatus::Cancelled;return result;}
    result.reply=std::move(parsed);return result;
}
}
BillingInputProcessClient::BillingInputProcessClient(std::filesystem::path helper):process_(std::move(helper)){}
BillingInputResult BillingInputProcessClient::exchange(const BillingInputRequest &r,const std::atomic<bool> &cancel,unsigned timeout){
    BillingInputResult result;bool idle=false;if(!busy_.compare_exchange_strong(idle,true)){result.transport=LocalTaskStatus::Busy;return result;}
    struct Release {std::atomic<bool> &busy;~Release(){busy=false;}} release{busy_};
    const auto start=std::chrono::steady_clock::now();result=flight(process_,r,cancel,timeout);
    if(result.transport==LocalTaskStatus::Ok&&std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(timeout)){result.transport=LocalTaskStatus::Timeout;result.reply.reset();}
    return result;
}
BillingInputResult BillingInputProcessClient::transact(const BillingInputPlan &plan,const std::string &previous,const std::atomic<bool> &cancel,unsigned timeout){
    BillingInputResult result;bool idle=false;if(!busy_.compare_exchange_strong(idle,true)){result.transport=LocalTaskStatus::Busy;return result;}
    struct Release {std::atomic<bool> &busy;~Release(){busy=false;}} release{busy_};const auto start=std::chrono::steady_clock::now();
    BillingInputRequest r;r.generation=plan.context.generation;r.ticket=plan.context.ticket;r.action=A::Begin;r.expectedRevision=previous;std::string payload;
    if(timeout<50||timeout>60000||!prepareBillingInput(plan,&r.manifest,&payload)||!valid(r)){result.transport=LocalTaskStatus::Invalid;return result;}
    size_t received=0;
    for(unsigned count=0;count<26;++count){
        if(cancel.load()){result.transport=LocalTaskStatus::Cancelled;result.reply.reset();return result;}
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        if(elapsed>static_cast<int64_t>(timeout)-50){result.transport=LocalTaskStatus::Timeout;result.reply.reset();return result;}
        auto current=flight(process_,r,cancel,timeout-static_cast<unsigned>(elapsed));current.childStarted=current.childStarted||result.childStarted;current.commitIssued=current.commitIssued||result.commitIssued;result=std::move(current);
        if(!result.reply||result.reply->status!=S::Ok)return result;
        const auto &s=*result.reply;received+=r.data.size();
        const auto prefix=payload.substr(0,received);
        if(s.received!=received||s.payloadDigest!=sha256Hex(prefix)||s.revision!=sha256Hex(stageBody(r.manifest,s.phase,s.certificate,prefix))||
            (r.action!=A::Commit&&(s.phase!=P::Receiving||s.certificate))){result.transport=LocalTaskStatus::Unavailable;result.reply.reset();return result;}
        if(r.action==A::Commit){BillingRecoverySnapshot confirmed;
            if(s.phase!=P::Complete||!s.certificate||!proof(r.manifest,plan.context.binding,plan.context.limits,plan.chunk,*s.certificate,&confirmed)){
                result.transport=LocalTaskStatus::Unavailable;result.reply.reset();return result;}
            if(cancel.load()||std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(timeout)){
                result.transport=cancel.load()?LocalTaskStatus::Cancelled:LocalTaskStatus::Timeout;result.reply.reset();return result;}
            if(s.certificate->state!=R::Unavailable)result.confirmed=std::move(confirmed);
            return result;
        }
        r.expectedRevision=s.revision;r.data.clear();r.offset=0;
        if(received==payload.size())r.action=A::Commit;
        else{r.action=A::Append;r.offset=received;r.data=payload.substr(received,billingInputPageBytes);}
    }
    result.transport=LocalTaskStatus::Unavailable;result.reply.reset();return result;
}
} // namespace om
