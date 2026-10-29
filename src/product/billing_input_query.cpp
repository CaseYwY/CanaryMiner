#include "billing_input_query.h"
#include "billing_input_model.h"
#include "hash.h"
#include <algorithm>
#include <chrono>
namespace om {
namespace {
using namespace billing_input_detail;using namespace billing_detail;
using S=BillingStatus;using P=BillingInputPhase;using M=BillingInputMode;
template<class T>bool number(const JsonValue &v,T &out,uint64_t cap){uint64_t n=0;if(!v.asUint64(&n)||n>cap)return false;out=static_cast<T>(n);return true;}
bool decodeView(const BillingQueryRequest &r,const std::string &view,BillingInputSnapshot &out){
    BillingInputSnapshot s;s.status=S::Ok;
    if(view=="CMBIV1\nabsent\n0\n"||view=="CMBIV1\nabsent\n1\n"){s.replacementPending=view[14]=='1';out=std::move(s);return true;}
    std::string model,meta,prefix;size_t at=7;
    if(view.size()>billingInputViewLimit||view.rfind("CMBIV1\n",0)!=0||!part(view,at,manifestLimit,model)||!part(view,at,16*1024,meta)||!part(view,at,chunkLimit,prefix)||at!=view.size())return false;
    JsonValue m,v;if(!parseJsonStrict(model,&m,nullptr)||!parseManifest(m,s.manifest)||manifestText(s.manifest)!=model||!parseJsonStrict(meta,&v,nullptr)||!v.isArray()||v.items.size()!=6)return false;
    const auto &a=v.items;const auto bound=bindingText(r.binding,r.limits);const auto &manifest=s.manifest;const auto total=manifest.bindingBytes+manifest.chunkBytes;
    if(manifest.file!=r.file||manifest.bindingBytes!=bound.size()||manifest.bindingDigest!=sha256Hex(bound)||manifest.preparedUtcMs>r.nowUtcMs||
        validateBinding(r.binding,r.limits,manifest.preparedUtcMs)!=S::Ok||!number(a[0],s.phase,3)||s.phase==P::Unavailable||!parseCertificate(a[1],s.certificate)||
        !a[2].isString()||!hash(a[2].text)||!number(a[3],s.received,total)||!a[4].isString()||!hash(a[4].text)||!number(a[5],s.replacementPending,1)||(s.phase==P::Complete)!=s.certificate.has_value()||
        (s.received!=total&&(s.received%billingInputPageBytes||s.phase!=P::Receiving))||prefix.size()!=(s.received>bound.size()?s.received-bound.size():0))return false;
    s.present=true;s.revision=a[2].text;s.originalPrefix=prefix;
    const auto payload=bound.substr(0,std::min(bound.size(),s.received))+prefix;
    if(sha256Hex(payload)!=a[4].text||sha256Hex(stageBody(manifest,s.phase,s.certificate,payload))!=s.revision)return false;
    if(s.received==total){
        if(sha256Hex(prefix)!=manifest.chunkDigest)return false;
        if(manifest.mode==M::CreateNew){if(prefix!="null")return false;}
        else{BillingChunk chunk;
            if(!parseChunk(prefix,r.binding.evidence,chunk)||chunk.sequence!=manifest.priorSequence+1||chunk.observedUtcMs>manifest.preparedUtcMs||
                (chunk.receipt&&chunk.receipt->request.source.nowUtcMs>manifest.preparedUtcMs))return false;
            s.original=std::move(chunk);}
        if(s.certificate){BillingRecoverySnapshot outcome;
            if(!proof(manifest,r.binding,r.limits,s.original,*s.certificate,&outcome))return false;
            if(s.certificate->state!=BillingRecoveryState::Unavailable)s.outcome=std::move(outcome);}
    }
    const auto canonical="["+std::to_string(static_cast<unsigned>(s.phase))+","+certificateText(s.certificate)+","+jsonQuote(s.revision)+","+std::to_string(s.received)+","+jsonQuote(a[4].text)+","+(s.replacementPending?"1":"0")+"]";
    if(canonical!=meta)return false;
    out=std::move(s);return true;
}
}
bool validBillingInputQuery(const BillingQueryPageRequest &r){const auto p=r.file.u8string();return r.generation&&r.ticket&&r.file.is_absolute()&&p.size()<=4096&&p.find('\0')==std::string::npos&&hash(r.bindingDigest)&&instant(r.nowUtcMs)&&
    r.offset<billingInputViewLimit&&r.offset%billingQueryPageBytes==0&&(r.expectedView.empty()?r.offset==0:hash(r.expectedView));}
bool encodeBillingInputQueryRequest(const BillingQueryPageRequest &r,uint64_t parent,std::string *wire){
    if(!wire||!validBillingInputQuery(r)||!parent||parent>UINT32_MAX)return false;
    *wire="[\"CMBISQ1\","+std::to_string(parent)+","+std::to_string(r.generation)+","+std::to_string(r.ticket)+","+jsonQuote(r.file.u8string())+","+jsonQuote(r.bindingDigest)+","+
        std::to_string(r.nowUtcMs)+","+jsonQuote(r.expectedView)+","+std::to_string(r.offset)+"]";return wire->size()<=manifestLimit;
}
bool decodeBillingInputQueryRequest(const std::string &wire,BillingQueryPageRequest *out,uint64_t *parent){
    JsonValue root;if(!out||!parent||wire.size()>manifestLimit||!parseJsonStrict(wire,&root,nullptr)||!root.isArray()||root.items.size()!=9)return false;
    const auto &a=root.items;BillingQueryPageRequest r;uint64_t pid=0;
    if(!a[0].isString()||a[0].text!="CMBISQ1"||!number(a[1],pid,UINT32_MAX)||!number(a[2],r.generation,UINT64_MAX)||!number(a[3],r.ticket,UINT64_MAX)||!a[4].isString()||!a[5].isString()||
        !number(a[6],r.nowUtcMs,lastUtc)||!a[7].isString()||!number(a[8],r.offset,billingInputViewLimit))return false;
    r.file=std::filesystem::u8path(a[4].text);r.bindingDigest=a[5].text;r.expectedView=a[7].text;std::string canonical;
    if(!encodeBillingInputQueryRequest(r,pid,&canonical)||canonical!=wire)return false;
    *out=std::move(r);*parent=pid;return true;
}
bool encodeBillingInputQueryReply(const BillingQueryPageRequest &r,const BillingQueryPageReply &s,std::string *wire){
    if(!wire||!validBillingInputQuery(r)||static_cast<unsigned>(s.status)>static_cast<unsigned>(S::Storage))return false;
    if(s.status==S::Ok){if(!hash(s.viewDigest)||(!r.expectedView.empty()&&r.expectedView!=s.viewDigest)||!s.total||s.total>billingInputViewLimit||r.offset>=s.total||s.data.size()!=std::min(billingQueryPageBytes,s.total-r.offset))return false;}
    else if(!s.viewDigest.empty()||s.total||!s.data.empty())return false;
    *wire="[\"CMBISA1\","+std::to_string(r.generation)+","+std::to_string(r.ticket)+","+std::to_string(static_cast<unsigned>(s.status))+","+jsonQuote(s.viewDigest)+","+
        std::to_string(r.offset)+","+std::to_string(s.total)+","+std::to_string(s.data.size())+"]\n"+s.data;return wire->size()<=localTaskResponseLimit;
}
bool decodeBillingInputQueryReply(const BillingQueryPageRequest &r,const std::string &wire,BillingQueryPageReply *out){
    const auto end=wire.find('\n');JsonValue root;if(!out||wire.size()>localTaskResponseLimit||end==std::string::npos||end>512||!parseJsonStrict(wire.substr(0,end),&root,nullptr)||!root.isArray()||root.items.size()!=8)return false;
    const auto &a=root.items;BillingQueryPageReply s;uint64_t generation=0,ticket=0;size_t offset=0,count=0;
    if(!a[0].isString()||a[0].text!="CMBISA1"||!number(a[1],generation,UINT64_MAX)||generation!=r.generation||!number(a[2],ticket,UINT64_MAX)||ticket!=r.ticket||
        !number(a[3],s.status,static_cast<unsigned>(S::Storage))||!a[4].isString()||!number(a[5],offset,billingInputViewLimit)||offset!=r.offset||!number(a[6],s.total,billingInputViewLimit)||
        !number(a[7],count,billingQueryPageBytes)||count!=wire.size()-end-1)return false;
    s.viewDigest=a[4].text;s.data=wire.substr(end+1);std::string canonical;if(!encodeBillingInputQueryReply(r,s,&canonical)||canonical!=wire)return false;
    *out=std::move(s);return true;
}
BillingInputQueryClient::BillingInputQueryClient(std::filesystem::path helper):process_(std::move(helper)){}
BillingInputQueryResult BillingInputQueryClient::transact(const BillingQueryRequest &r,const std::atomic<bool> &cancel,unsigned timeout){
    BillingInputQueryResult result;bool idle=false;if(!busy_.compare_exchange_strong(idle,true)){result.transport=LocalTaskStatus::Busy;return result;}
    struct Release {std::atomic<bool> &busy;~Release(){busy=false;}} release{busy_};const auto start=std::chrono::steady_clock::now();
    if(timeout<50||timeout>60000||validateBinding(r.binding,r.limits,r.nowUtcMs)!=S::Ok){result.transport=LocalTaskStatus::Invalid;return result;}
    BillingQueryPageRequest page{r.generation,r.ticket,r.file,sha256Hex(bindingText(r.binding,r.limits)),"",r.nowUtcMs,0};std::string assembled;size_t total=0;
    for(unsigned flight=0;flight<4;++flight){
        if(cancel.load()){result.transport=LocalTaskStatus::Cancelled;return result;}
        std::string input;if(!encodeBillingInputQueryRequest(page,localTaskProcessId(),&input)){result.transport=LocalTaskStatus::Invalid;return result;}
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        if(elapsed>static_cast<int64_t>(timeout)-50){result.transport=LocalTaskStatus::Timeout;return result;}
        const auto received=process_.run(input,cancel,timeout-static_cast<unsigned>(elapsed));result.transport=received.status;if(received.status!=LocalTaskStatus::Ok)return result;
        BillingQueryPageReply parsed;if(!decodeBillingInputQueryReply(page,received.response,&parsed)){result.transport=LocalTaskStatus::Unavailable;return result;}
        if(cancel.load()){result.transport=LocalTaskStatus::Cancelled;return result;}
        if(parsed.status!=S::Ok){BillingInputSnapshot failed;failed.status=parsed.status;result.reply=failed;return result;}
        if(!flight){total=parsed.total;page.expectedView=parsed.viewDigest;assembled.reserve(total);}else if(total!=parsed.total){result.transport=LocalTaskStatus::Unavailable;return result;}
        assembled+=parsed.data;
        if(assembled.size()==total){BillingInputSnapshot snapshot;
            if(sha256Hex(assembled)!=page.expectedView||!decodeView(r,assembled,snapshot)){result.transport=LocalTaskStatus::Unavailable;return result;}
            if(cancel.load()){result.transport=LocalTaskStatus::Cancelled;return result;}
            if(std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(timeout)){result.transport=LocalTaskStatus::Timeout;return result;}
            result.reply=std::move(snapshot);return result;
        }
        page.offset=assembled.size();
    }
    result.transport=LocalTaskStatus::Unavailable;return result;
}
} // namespace om
