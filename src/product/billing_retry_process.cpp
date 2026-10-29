#include "billing_retry_process.h"
#include "billing_model.h"
#include "json_dom.h"
#include "hash.h"
#include <chrono>
namespace om {
namespace {
using S=BillingStatus;using R=BillingRecoveryState;using namespace billing_detail;
bool hash(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool accepted(S s){return s==S::Ok||s==S::Replay;}
bool valid(const BillingRetryRequest &r){const auto p=r.file.u8string();return r.generation&&r.ticket&&r.file.is_absolute()&&p.size()<=4096&&p.find('\0')==std::string::npos&&instant(r.nowUtcMs)&&hash(r.bindingDigest)&&hash(r.revision)&&hash(r.originalDigest);}
template<class T>bool number(const JsonValue &v,T &out,uint64_t cap){uint64_t n=0;if(!v.asUint64(&n)||n>cap)return false;out=static_cast<T>(n);return true;}
std::string frame(const std::string &s){return std::to_string(s.size())+"\n"+s+"\n";}
std::string revision(const BillingQueryRequest &r,const BillingRecoverySnapshot &s){const auto metadata="["+std::to_string(static_cast<unsigned>(s.state))+","+(s.applied?std::to_string(static_cast<unsigned>(*s.applied)):"null")+","+
    jsonQuote(r.file.u8string())+","+std::to_string(s.recordedUtcMs)+","+std::to_string(s.priorSequence)+","+jsonQuote(s.priorDigest)+"]";
    return sha256Hex("CMBJ1\n"+frame(metadata)+frame(bindingText(r.binding,r.limits))+frame(s.original?chunkText(*s.original):"null"));}
}
bool encodeBillingRetryRequest(const BillingRetryRequest &r,uint64_t parent,std::string *wire){
    if(!wire||!valid(r)||!parent||parent>UINT32_MAX)return false;
    *wire="[\"CMBRQ1\","+std::to_string(parent)+","+std::to_string(r.generation)+","+std::to_string(r.ticket)+","+jsonQuote(r.file.u8string())+","+jsonQuote(r.bindingDigest)+","+jsonQuote(r.revision)+","+
        jsonQuote(r.originalDigest)+","+std::to_string(r.nowUtcMs)+"]";return wire->size()<=32*1024;
}
bool decodeBillingRetryRequest(const std::string &wire,BillingRetryRequest *out,uint64_t *parent){
    JsonValue root;if(!out||!parent||wire.size()>32*1024||!parseJsonStrict(wire,&root,nullptr)||!root.isArray()||root.items.size()!=9)return false;const auto &a=root.items;BillingRetryRequest r;uint64_t pid=0;
    if(!a[0].isString()||a[0].text!="CMBRQ1"||!number(a[1],pid,UINT32_MAX)||!number(a[2],r.generation,UINT64_MAX)||!number(a[3],r.ticket,UINT64_MAX)||!a[4].isString()||!a[5].isString()||!a[6].isString()||!a[7].isString()||!number(a[8],r.nowUtcMs,lastUtc))return false;
    r.file=std::filesystem::u8path(a[4].text);r.bindingDigest=a[5].text;r.revision=a[6].text;r.originalDigest=a[7].text;std::string canonical;
    if(!encodeBillingRetryRequest(r,pid,&canonical)||canonical!=wire)return false;
    *out=std::move(r);*parent=pid;return true;
}
bool encodeBillingRetryReply(const BillingRetryRequest &r,const BillingRetryReply &s,std::string *wire){
    if(!wire||!valid(r)||static_cast<unsigned>(s.status)>static_cast<unsigned>(S::Storage)||s.state.size()>2048)return false;
    if(accepted(s.status)){if(!hash(s.confirmedRevision)||!hash(s.committedDigest)||!s.recordedResult||!accepted(*s.recordedResult)||s.state.empty())return false;}
    else if(!s.confirmedRevision.empty()||!s.committedDigest.empty()||!s.state.empty()||s.recordedResult)return false;
    *wire="[\"CMBRA1\","+std::to_string(r.generation)+","+std::to_string(r.ticket)+","+std::to_string(static_cast<unsigned>(s.status))+","+jsonQuote(s.confirmedRevision)+","+jsonQuote(s.committedDigest)+","+
        (s.recordedResult?std::to_string(static_cast<unsigned>(*s.recordedResult)):"null")+","+(s.state.empty()?"null":s.state)+"]";return wire->size()<=4096;
}
bool decodeBillingRetryReply(const BillingRetryRequest &r,const std::string &wire,BillingRetryReply *out){
    JsonValue root;if(!out||wire.size()>4096||!parseJsonStrict(wire,&root,nullptr)||!root.isArray()||root.items.size()!=8)return false;const auto &a=root.items;BillingRetryReply s;uint64_t generation=0,ticket=0;
    if(!a[0].isString()||a[0].text!="CMBRA1"||!number(a[1],generation,UINT64_MAX)||generation!=r.generation||!number(a[2],ticket,UINT64_MAX)||ticket!=r.ticket||!number(a[3],s.status,static_cast<unsigned>(S::Storage))||!a[4].isString()||!a[5].isString())return false;
    s.confirmedRevision=a[4].text;s.committedDigest=a[5].text;if(a[6].type!=JsonValue::Type::Null){S result;if(!number(a[6],result,1))return false;s.recordedResult=result;}
    if(a[7].type!=JsonValue::Type::Null){const auto start=wire.find(",[",wire.find("CMBRA1"));if(start==std::string::npos||wire.back()!=']')return false;s.state=wire.substr(start+1,wire.size()-start-2);}
    std::string canonical;if(!encodeBillingRetryReply(r,s,&canonical)||canonical!=wire)return false;
    *out=std::move(s);return true;
}
BillingRetryReply executeBillingRetryRequest(const BillingRetryRequest &r){
    BillingRetryReply reply;if(!valid(r)){reply.status=S::Invalid;return reply;}
    BillingRecoveryStore store;
#ifdef CANARY_BILLING_TESTING
    const auto name=r.file.filename().u8string();
    if(name=="fixture.billing-retry-precommit.db")BillingRecoveryTestPeer::interrupt(store,BillingRecoveryTestPoint::BeforeCommit);
    if(name=="fixture.billing-retry-committed.db")BillingRecoveryTestPeer::interrupt(store,BillingRecoveryTestPoint::AfterCommit);
#endif
    reply.status=store.retryReviewed(r.file,r.bindingDigest,r.revision,r.originalDigest,r.nowUtcMs);
    if(accepted(reply.status)){const auto confirmed=store.inspect(r.nowUtcMs);if(confirmed.status!=S::Ok||confirmed.state!=R::Confirmed){reply.status=S::Storage;return reply;}
        reply.confirmedRevision=confirmed.revision;reply.recordedResult=confirmed.applied;reply.committedDigest=confirmed.committed.digest;reply.state=stateText(confirmed.committed);}
    return reply;
}
BillingRetryProcessClient::BillingRetryProcessClient(std::filesystem::path helper):process_(std::move(helper)){}
BillingRetryResult BillingRetryProcessClient::transact(const BillingQueryRequest &r,const BillingRecoverySnapshot &reviewed,const std::atomic<bool> &cancel,unsigned timeout){
    BillingRetryResult result;bool idle=false;if(!busy_.compare_exchange_strong(idle,true)){result.transport=LocalTaskStatus::Busy;return result;}
    struct Release {std::atomic<bool> &busy;~Release(){busy=false;}} release{busy_};const auto start=std::chrono::steady_clock::now();
    if(timeout<50||timeout>60000||validateBinding(r.binding,r.limits,r.nowUtcMs)!=S::Ok||reviewed.status!=S::Ok||(reviewed.state!=R::Prepared&&reviewed.state!=R::Confirmed)||!reviewed.original||
        !boundedChunk(*reviewed.original)||!hash(reviewed.revision)||revision(r,reviewed)!=reviewed.revision||reviewed.recordedUtcMs>r.nowUtcMs){result.transport=LocalTaskStatus::Invalid;return result;}
    BillingRetryRequest request{r.generation,r.ticket,r.file,sha256Hex(bindingText(r.binding,r.limits)),reviewed.revision,sha256Hex(chunkText(*reviewed.original)),r.nowUtcMs};std::string wire;
    if(!encodeBillingRetryRequest(request,localTaskProcessId(),&wire)){result.transport=LocalTaskStatus::Invalid;return result;}
    if(cancel.load()){result.transport=LocalTaskStatus::Cancelled;return result;}
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();if(elapsed>static_cast<int64_t>(timeout)-50){result.transport=LocalTaskStatus::Timeout;return result;}
    const auto outcome=process_.run(wire,cancel,timeout-static_cast<unsigned>(elapsed));result.transport=outcome.status;if(outcome.status!=LocalTaskStatus::Ok)return result;
    BillingRetryReply decoded;if(!decodeBillingRetryReply(request,outcome.response,&decoded)){result.transport=LocalTaskStatus::Unavailable;return result;}
    if(cancel.load()){result.transport=LocalTaskStatus::Cancelled;return result;}
    if(accepted(decoded.status)){auto confirmed=reviewed;confirmed.state=R::Confirmed;confirmed.applied=decoded.recordedResult;confirmed.revision=decoded.confirmedRevision;
        if(!parseState(decoded.state,r.binding,r.limits,r.nowUtcMs,confirmed.committed)){result.transport=LocalTaskStatus::Unavailable;return result;}confirmed.committed.digest=decoded.committedDigest;
        if(confirmed.committed.sequence!=reviewed.original->sequence||confirmed.committed.batchId!=reviewed.original->id||confirmed.committed.digest!=sha256Hex(reviewed.priorDigest+"|"+chunkText(*reviewed.original)+"|"+decoded.state)||revision(r,confirmed)!=confirmed.revision){result.transport=LocalTaskStatus::Unavailable;return result;}
        if(cancel.load()){result.transport=LocalTaskStatus::Cancelled;return result;}
        if(std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(timeout)){result.transport=LocalTaskStatus::Timeout;return result;}result.confirmed=std::move(confirmed);}
    result.applied=decoded.status;return result;
}
} // namespace om
