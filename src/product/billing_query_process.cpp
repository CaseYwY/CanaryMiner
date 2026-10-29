#include "billing_query_process.h"
#include "billing_model.h"
#include "json_dom.h"
#include "hash.h"
#include <algorithm>
#include <chrono>
namespace om {
namespace {
using S=BillingStatus;using R=BillingRecoveryState;using namespace billing_detail;
constexpr size_t requestLimit=32*1024;
bool hash(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool accepted(S s){return s==S::Ok||s==S::Replay;}
bool fields(const JsonValue &v,size_t n){return v.isArray()&&v.items.size()==n;}
template<class T>bool number(const JsonValue &v,T &out,uint64_t maximum){uint64_t n=0;if(!v.asUint64(&n)||n>maximum)return false;out=static_cast<T>(n);return true;}
bool valid(const BillingQueryPageRequest &r){const auto path=r.file.u8string();return r.generation&&r.ticket&&r.file.is_absolute()&&path.size()<=4096&&path.find('\0')==std::string::npos&&hash(r.bindingDigest)&&instant(r.nowUtcMs)&&
    r.offset<billingQueryViewLimit&&r.offset%billingQueryPageBytes==0&&(r.expectedView.empty()?r.offset==0:hash(r.expectedView));}
std::string frame(const std::string &s){return std::to_string(s.size())+"\n"+s+"\n";}
bool part(const std::string &s,size_t &offset,size_t cap,std::string &out){
    const auto end=s.find('\n',offset);if(end==std::string::npos||end==offset||end-offset>10||(end-offset>1&&s[offset]=='0'))return false;
    size_t n=0;for(size_t i=offset;i<end;++i){const auto c=s[i];if(c<'0'||c>'9'||n>(cap-static_cast<unsigned>(c-'0'))/10)return false;n=n*10+static_cast<unsigned>(c-'0');}
    offset=end+1;if(n>cap||n>=s.size()-offset||s[offset+n]!='\n')return false;out=s.substr(offset,n);offset+=n+1;return true;
}
std::string view(const BillingRecoverySnapshot &s){
    const auto metadata="["+std::to_string(static_cast<unsigned>(s.state))+","+(s.applied?std::to_string(static_cast<unsigned>(*s.applied)):"null")+","+std::to_string(s.priorSequence)+","+jsonQuote(s.priorDigest)+","+
        std::to_string(s.recordedUtcMs)+","+jsonQuote(s.revision)+","+jsonQuote(s.committed.digest)+"]";
    return "CMBV1\n"+frame(metadata)+frame(stateText(s.committed))+frame(s.original?chunkText(*s.original):"null");
}
bool decodeView(const BillingQueryRequest &r,const std::string &wire,BillingRecoverySnapshot &out){
    std::string metadata,state,original;size_t at=6;
    if(wire.size()>billingQueryViewLimit||wire.rfind("CMBV1\n",0)!=0||!part(wire,at,1024,metadata)||!part(wire,at,2048,state)||!part(wire,at,chunkLimit,original)||at!=wire.size())return false;
    JsonValue root;if(!parseJsonStrict(metadata,&root,nullptr)||!fields(root,7))return false;const auto &a=root.items;BillingRecoverySnapshot s;s.status=S::Ok;
    if(!number(a[0],s.state,static_cast<unsigned>(R::Diverged))||s.state==R::Unavailable||!number(a[2],s.priorSequence,r.limits.maxBatches)||!a[3].isString()||!hash(a[3].text)||
        !number(a[4],s.recordedUtcMs,lastUtc)||s.recordedUtcMs<r.binding.cycleBeginUtcMs||s.recordedUtcMs>r.nowUtcMs||!a[5].isString()||!hash(a[5].text)||!a[6].isString()||!hash(a[6].text))return false;
    s.priorDigest=a[3].text;s.revision=a[5].text;
    if(a[1].type!=JsonValue::Type::Null){S applied;if(!number(a[1],applied,static_cast<unsigned>(S::Calculation)))return false;s.applied=applied;}
    if(!parseState(state,r.binding,r.limits,r.nowUtcMs,s.committed))return false;
    s.committed.digest=a[6].text;
    if(original!="null"){BillingChunk c;if(!parseChunk(original,r.binding.evidence,c)||c.sequence!=s.priorSequence+1||c.observedUtcMs>s.recordedUtcMs||(c.receipt&&c.receipt->request.source.nowUtcMs>s.recordedUtcMs))return false;s.original=std::move(c);}
    const auto bound=bindingText(r.binding,r.limits),seed=sha256Hex(bound);
    if(!s.priorSequence&&s.priorDigest!=seed)return false;
    const auto persisted=!s.original?R::Empty:s.applied?(accepted(*s.applied)?R::Confirmed:R::Rejected):R::Prepared;
    if(persisted==R::Empty&&(s.applied||s.priorSequence||s.priorDigest!=seed))return false;
    // Reconstruct the complete private journal revision from the caller-approved binding and retained original.
    const auto journalMeta="["+std::to_string(static_cast<unsigned>(persisted))+","+(s.applied?std::to_string(static_cast<unsigned>(*s.applied)):"null")+","+jsonQuote(r.file.u8string())+","+
        std::to_string(s.recordedUtcMs)+","+std::to_string(s.priorSequence)+","+jsonQuote(s.priorDigest)+"]";
    if(sha256Hex("CMBJ1\n"+frame(journalMeta)+frame(bound)+frame(original))!=s.revision)return false;
    const auto &c=s.committed;const bool prior=c.sequence==s.priorSequence&&c.digest==s.priorDigest;
    const bool after=s.original&&c.sequence==s.original->sequence&&c.batchId==s.original->id&&c.digest==sha256Hex(s.priorDigest+"|"+original+"|"+state);
    const bool consistent=persisted==R::Prepared?(prior||after):persisted==R::Confirmed?after:prior;
    if(s.state!=(consistent?persisted:R::Diverged)||view(s)!=wire)return false;
    out=std::move(s);return true;
}
std::string replyHeader(const BillingQueryPageRequest &r,const BillingQueryPageReply &s){return "[\"CMBQA1\","+std::to_string(r.generation)+","+std::to_string(r.ticket)+","+std::to_string(static_cast<unsigned>(s.status))+","+
    jsonQuote(s.viewDigest)+","+std::to_string(r.offset)+","+std::to_string(s.total)+","+std::to_string(s.data.size())+"]\n";}
}
bool encodeBillingQueryRequest(const BillingQueryPageRequest &r,uint64_t parent,std::string *wire){
    if(!wire||!valid(r)||!parent||parent>UINT32_MAX)return false;
    *wire="[\"CMBQQ1\","+std::to_string(parent)+","+std::to_string(r.generation)+","+std::to_string(r.ticket)+","+jsonQuote(r.file.u8string())+","+jsonQuote(r.bindingDigest)+","+
        std::to_string(r.nowUtcMs)+","+jsonQuote(r.expectedView)+","+std::to_string(r.offset)+"]";return wire->size()<=requestLimit;
}
bool decodeBillingQueryRequest(const std::string &wire,BillingQueryPageRequest *out,uint64_t *parent){
    JsonValue root;if(!out||!parent||wire.size()>requestLimit||!parseJsonStrict(wire,&root,nullptr)||!fields(root,9))return false;
    const auto &a=root.items;BillingQueryPageRequest r;uint64_t pid=0;
    if(!a[0].isString()||a[0].text!="CMBQQ1"||!number(a[1],pid,UINT32_MAX)||!number(a[2],r.generation,UINT64_MAX)||!number(a[3],r.ticket,UINT64_MAX)||!a[4].isString()||!a[5].isString()||
        !number(a[6],r.nowUtcMs,lastUtc)||!a[7].isString()||!number(a[8],r.offset,billingQueryViewLimit))return false;
    r.file=std::filesystem::u8path(a[4].text);r.bindingDigest=a[5].text;r.expectedView=a[7].text;std::string canonical;
    if(!encodeBillingQueryRequest(r,pid,&canonical)||canonical!=wire)return false;
    *out=std::move(r);*parent=pid;return true;
}
bool encodeBillingQueryReply(const BillingQueryPageRequest &r,const BillingQueryPageReply &s,std::string *wire){
    if(!wire||!valid(r)||static_cast<unsigned>(s.status)>static_cast<unsigned>(S::Storage))return false;
    if(s.status==S::Ok){if(!hash(s.viewDigest)||(!r.expectedView.empty()&&r.expectedView!=s.viewDigest)||!s.total||s.total>billingQueryViewLimit||r.offset>=s.total||s.data.size()!=std::min(billingQueryPageBytes,s.total-r.offset))return false;}
    else if(!s.viewDigest.empty()||s.total||!s.data.empty())return false;
    *wire=replyHeader(r,s)+s.data;return wire->size()<=localTaskResponseLimit;
}
bool decodeBillingQueryReply(const BillingQueryPageRequest &r,const std::string &wire,BillingQueryPageReply *out){
    const auto end=wire.find('\n');if(!out||wire.size()>localTaskResponseLimit||end==std::string::npos||end>512)return false;
    JsonValue root;if(!parseJsonStrict(wire.substr(0,end),&root,nullptr)||!fields(root,8))return false;const auto &a=root.items;BillingQueryPageReply s;uint64_t generation=0,ticket=0;size_t offset=0,length=0;
    if(!a[0].isString()||a[0].text!="CMBQA1"||!number(a[1],generation,UINT64_MAX)||generation!=r.generation||!number(a[2],ticket,UINT64_MAX)||ticket!=r.ticket||!number(a[3],s.status,static_cast<unsigned>(S::Storage))||
        !a[4].isString()||!number(a[5],offset,billingQueryViewLimit)||offset!=r.offset||!number(a[6],s.total,billingQueryViewLimit)||!number(a[7],length,billingQueryPageBytes)||length!=wire.size()-end-1)return false;
    s.viewDigest=a[4].text;s.data=wire.substr(end+1);std::string canonical;if(!encodeBillingQueryReply(r,s,&canonical)||canonical!=wire)return false;*out=std::move(s);return true;
}
BillingQueryPageReply executeBillingQueryRequest(const BillingQueryPageRequest &r){
    BillingQueryPageReply reply;if(!valid(r)){reply.status=S::Invalid;return reply;}
    BillingRecoveryStore store;reply.status=store.openQuery(r.file,r.bindingDigest,r.nowUtcMs);if(reply.status!=S::Ok)return reply;
    const auto state=store.inspect(r.nowUtcMs);reply.status=state.status;if(reply.status!=S::Ok)return reply;
    const auto text=view(state);if(text.size()>billingQueryViewLimit){reply.status=S::Capacity;return reply;}const auto digest=sha256Hex(text);
    if((!r.expectedView.empty()&&r.expectedView!=digest)||r.offset>=text.size()){reply.status=S::Conflict;return reply;}
    reply.viewDigest=digest;reply.total=text.size();reply.data=text.substr(r.offset,billingQueryPageBytes);return reply;
}
BillingQueryProcessClient::BillingQueryProcessClient(std::filesystem::path helper):process_(std::move(helper)){}
BillingQueryResult BillingQueryProcessClient::transact(const BillingQueryRequest &r,const std::atomic<bool> &cancel,unsigned timeout){
    BillingQueryResult result;bool idle=false;if(!busy_.compare_exchange_strong(idle,true)){result.transport=LocalTaskStatus::Busy;return result;}
    struct Release {std::atomic<bool> &busy;~Release(){busy=false;}} release{busy_};
    const auto start=std::chrono::steady_clock::now();
    if(timeout<50||timeout>60000||validateBinding(r.binding,r.limits,r.nowUtcMs)!=S::Ok){result.transport=LocalTaskStatus::Invalid;return result;}
    BillingQueryPageRequest page{r.generation,r.ticket,r.file,sha256Hex(bindingText(r.binding,r.limits)),"",r.nowUtcMs,0};std::string assembled;size_t total=0;
    for(unsigned flight=0;flight<3;++flight){
        if(cancel.load()){result.transport=LocalTaskStatus::Cancelled;return result;}
        std::string input;if(!encodeBillingQueryRequest(page,localTaskProcessId(),&input)){result.transport=LocalTaskStatus::Invalid;return result;}
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        if(elapsed>static_cast<int64_t>(timeout)-50){result.transport=LocalTaskStatus::Timeout;return result;}
        const auto received=process_.run(input,cancel,timeout-static_cast<unsigned>(elapsed));result.transport=received.status;if(received.status!=LocalTaskStatus::Ok)return result;
        BillingQueryPageReply parsed;if(!decodeBillingQueryReply(page,received.response,&parsed)){result.transport=LocalTaskStatus::Unavailable;return result;}
        if(cancel.load()){result.transport=LocalTaskStatus::Cancelled;return result;}
        if(parsed.status!=S::Ok){BillingRecoverySnapshot failed;failed.status=parsed.status;result.reply=failed;return result;}
        if(!flight){total=parsed.total;page.expectedView=parsed.viewDigest;assembled.reserve(total);}else if(parsed.total!=total){result.transport=LocalTaskStatus::Unavailable;return result;}
        assembled+=parsed.data;
        if(assembled.size()==total){BillingRecoverySnapshot snapshot;
            if(sha256Hex(assembled)!=page.expectedView||!decodeView(r,assembled,snapshot)){result.transport=LocalTaskStatus::Unavailable;return result;}
            if(cancel.load()){result.transport=LocalTaskStatus::Cancelled;return result;}
            if(std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(timeout)){result.transport=LocalTaskStatus::Timeout;return result;}
            result.reply=std::move(snapshot);return result;}
        page.offset=assembled.size();
    }
    result.transport=LocalTaskStatus::Unavailable;return result;
}
} // namespace om
