#include "energy_history_process.h"
#include "energy_history_model.h"
#include "energy_recovery.h"
#include "energy_collector_process.h"
#include "hash.h"
#include "json_dom.h"
#include "public_https.h" // shared bounded JSON lexical guard only
#include <algorithm>
#include <cstdlib>

namespace om {
namespace {
using namespace energy_history_detail;
constexpr size_t requestLimit=energyHistoryRequestLimit;
using S=EnergyHistoryStatus;
using R=EnergyRecoveryState;
bool accepted(S s){return s==S::Ok||s==S::Replay;}
bool hash(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool array(const JsonValue &v,size_t n){return v.isArray()&&v.items.size()==n;}
bool u(const JsonValue &v,uint64_t &n,uint64_t max=INT64_MAX){return v.asUint64(&n)&&n<=max;}
bool time(const JsonValue &v,int64_t &n){uint64_t x=0;if(!u(v,x,lastUtcMs))return false;n=static_cast<int64_t>(x);return true;}
bool str(const JsonValue &v,std::string &s,size_t max=4096){if(!v.isString()||v.text.size()>max||v.text.find('\0')!=std::string::npos)return false;s=v.text;return true;}
bool null(const JsonValue &v){return v.type==JsonValue::Type::Null;}
bool status(const JsonValue &v,EnergyHistoryStatus &s){uint64_t n=0;if(!u(v,n,static_cast<unsigned>(EnergyHistoryStatus::DetailExpired)))return false;s=static_cast<EnergyHistoryStatus>(n);return true;}
bool decimal(const JsonValue &v,ExactDecimal &d){return v.isString()&&v.text.size()<=150&&ExactDecimal::parse(v.text,&d)&&d.text()==v.text&&!d.negative();}
bool valid(const EnergyHistoryRequest &r){
    if(!r.generation||!r.ticket||!localTaskPath(r.file)||!validEnergyHistoryBinding(r.binding)||!validEnergyHistoryLimits(r.limits)||!instant(r.nowUtcMs)||r.binding.historyBeginUtcMs>r.nowUtcMs||
        (r.mode!=EnergyHistoryOpen::Existing&&r.mode!=EnergyHistoryOpen::CreateNew)||r.queries.size()>4)return false;
    switch(r.recovery){
        case EnergyRecoveryMode::Volatile:break;
        case EnergyRecoveryMode::Inspect:if(r.batch)return false;break;
        case EnergyRecoveryMode::Submit:if(!r.batch)return false;break;
        case EnergyRecoveryMode::Retry:if(!r.batch||r.mode!=EnergyHistoryOpen::Existing)return false;break;
        default:return false;
    }
    for(const auto &q:r.queries)if(!instant(q.beginUtcMs)||!instant(q.endUtcMs)||q.beginUtcMs>=q.endUtcMs||q.endUtcMs>r.nowUtcMs||
        (q.registerId.empty()? !q.periodId.empty():!identifier(q.registerId)||!identifier(q.periodId)))return false;
    return !r.batch||boundedEnergyHistoryBatch(r.binding,*r.batch,r.nowUtcMs);
}
std::string headText(const EnergyHistoryCheckpoint &c,bool persistent,bool compact){
    return "["+std::to_string(static_cast<unsigned>(c.status))+","+std::to_string(c.sequence)+","+std::to_string(c.readingCount)+","+std::to_string(c.sessionCount)+","+
        std::to_string(c.windowCount)+","+jsonQuote(c.batchId)+","+jsonQuote(c.sessionId)+","+std::to_string(c.observedAtMs)+","+std::to_string(c.throughUtcMs)+
        ((persistent||compact)?","+jsonQuote(c.digest):"")+(compact?","+std::to_string(c.compactedSequence):"")+"]";
}
bool head(const JsonValue &v,const EnergyHistoryRequest &r,EnergyHistoryCheckpoint &c){
    const bool persistent=r.recovery!=EnergyRecoveryMode::Volatile;
    const bool compact=r.limits.compaction.has_value();
    if(!array(v,compact?11:persistent?10:9))return false;
    const auto &a=v.items;
    if(!status(a[0],c.status)||!u(a[1],c.sequence,compact?lifetimeBatchLimit:r.limits.maxBatches)||!u(a[2],c.readingCount,compact?128*c.sequence:r.limits.maxReadings)||!u(a[3],c.sessionCount,r.limits.maxSessions)||
        !u(a[4],c.windowCount,r.limits.maxWindows)||!str(a[5],c.batchId,128)||!str(a[6],c.sessionId,128)||!time(a[7],c.observedAtMs)||!time(a[8],c.throughUtcMs))return false;
    if((persistent||compact)&&(!str(a[9],c.digest,64)||(c.status==S::Ok?!hash(c.digest):!c.digest.empty())))return false;
    if((persistent||compact)&&c.status==S::Ok&&!c.sequence&&c.digest!=sha256Hex(bindingText(r.binding)))return false;
    if(compact&&(!u(a[10],c.compactedSequence,c.sequence)||c.sequence-c.compactedSequence>=r.limits.compaction->afterBatches))return false;
    if(c.status!=EnergyHistoryStatus::Ok||!c.sequence)return !c.sequence&&!c.readingCount&&!c.sessionCount&&!c.windowCount&&c.batchId.empty()&&c.sessionId.empty()&&!c.observedAtMs&&!c.throughUtcMs;
    return identifier(c.batchId)&&c.sessionCount<=c.sequence&&c.readingCount<=128*c.sequence&&c.windowCount<=32*c.sequence&&
        (c.sessionCount||c.windowCount)&&c.observedAtMs>=r.binding.historyBeginUtcMs&&c.observedAtMs<=r.nowUtcMs&&c.throughUtcMs<=c.observedAtMs&&
        (c.sessionCount?(identifier(c.sessionId)&&c.throughUtcMs>=r.binding.historyBeginUtcMs):(c.sessionId.empty()&&!c.throughUtcMs&&!c.readingCount));
}
std::string totalText(const EnergyHistoryTotals &t){
    return "["+std::to_string(static_cast<unsigned>(t.status))+","+std::to_string(static_cast<unsigned>(t.coverage))+","+std::to_string(t.beginUtcMs)+","+
        std::to_string(t.endUtcMs)+","+std::to_string(t.observedAtMs)+","+std::to_string(t.coveredDeviceMs)+","+std::to_string(t.requestedDeviceMs)+","+
        jsonQuote(t.knownKwh.numerator.text())+","+(t.completeKwh?jsonQuote(t.completeKwh->numerator.text()):"null")+"]";
}
bool total(const JsonValue &v,const EnergyHistoryRequest &r,const EnergyHistoryQuery &q,EnergyHistoryTotals &t){
    if(!array(v,9))return false;
    const auto &a=v.items;uint64_t coverage=0;
    if(!status(a[0],t.status)||!u(a[1],coverage,2)||!time(a[2],t.beginUtcMs)||!time(a[3],t.endUtcMs)||!time(a[4],t.observedAtMs)||
        !u(a[5],t.coveredDeviceMs)||!u(a[6],t.requestedDeviceMs)||!decimal(a[7],t.knownKwh.numerator)||t.beginUtcMs!=q.beginUtcMs||t.endUtcMs!=q.endUtcMs)return false;
    t.coverage=static_cast<EnergyHistoryCoverage>(coverage);
    if(!null(a[8])){ElectricityAmount n;if(!decimal(a[8],n.numerator))return false;t.completeKwh=n;}
    if(t.status!=EnergyHistoryStatus::Ok)return !coverage&&!t.observedAtMs&&!t.coveredDeviceMs&&!t.requestedDeviceMs&&t.knownKwh.numerator.zero()&&!t.completeKwh;
    if(t.observedAtMs>r.nowUtcMs||t.requestedDeviceMs!=static_cast<uint64_t>(q.endUtcMs-q.beginUtcMs)*r.binding.channels.size()||t.coveredDeviceMs>t.requestedDeviceMs||
        (t.coveredDeviceMs&&!t.observedAtMs)||(!t.coveredDeviceMs&&!t.knownKwh.numerator.zero()))return false;
    ExactDecimal duration,maximum,power;
    if(t.knownKwh.numerator.scale()>3||!ExactDecimal::parse(std::to_string(t.coveredDeviceMs),&duration)||!ExactDecimal::parse("4294967.295",&power)||
        !duration.multiply(power,&maximum)||t.knownKwh.numerator.compare(maximum)>0)return false;
    const auto expected=t.coveredDeviceMs==t.requestedDeviceMs?EnergyHistoryCoverage::Complete:t.coveredDeviceMs?EnergyHistoryCoverage::Partial:EnergyHistoryCoverage::Unavailable;
    return t.coverage==expected&&t.completeKwh.has_value()==(expected==EnergyHistoryCoverage::Complete)&&(!t.completeKwh||t.completeKwh->numerator.compare(t.knownKwh.numerator)==0);
}
std::string recoveryText(const EnergyRecoverySnapshot &s){
    return "["+std::to_string(static_cast<unsigned>(s.status))+","+std::to_string(static_cast<unsigned>(s.state))+","+std::to_string(s.priorSequence)+","+
        jsonQuote(s.priorDigest)+","+(s.batch?batchText(*s.batch):"null")+","+(s.applied?std::to_string(static_cast<unsigned>(*s.applied)):"null")+"]";
}
bool recovery(const JsonValue &v,const EnergyHistoryRequest &r,const EnergyHistoryReply &reply,EnergyRecoverySnapshot &s){
    uint64_t state=0;if(!array(v,6)||!status(v.items[0],s.status)||!u(v.items[1],state,static_cast<unsigned>(R::Unavailable))||
        !u(v.items[2],s.priorSequence,r.limits.compaction?lifetimeBatchLimit:r.limits.maxBatches)||!str(v.items[3],s.priorDigest,64))return false;
    s.state=static_cast<R>(state);
    if(!null(v.items[4])){EnergyHistoryBatch b;if(!parseBatchValue(v.items[4],r.binding,&b)||!boundedEnergyHistoryBatch(r.binding,b,r.nowUtcMs))return false;s.batch=std::move(b);}
    if(!null(v.items[5])){S applied;if(!status(v.items[5],applied))return false;s.applied=applied;}
    if(s.status!=S::Ok)return s.state==R::Unavailable&&!s.priorSequence&&s.priorDigest.empty()&&!s.batch&&!s.applied;
    if(reply.opened!=S::Ok||reply.checkpoint.status!=S::Ok||!hash(s.priorDigest)||s.state==R::Disabled||s.state==R::Unavailable)return false;
    if(!s.priorSequence&&s.priorDigest!=sha256Hex(bindingText(r.binding)))return false;
    if(s.state==R::Empty||(s.state==R::Diverged&&!s.batch)){if(s.batch||s.applied||s.priorSequence)return false;}
    else if(!s.batch||s.batch->sequence!=s.priorSequence+1)return false;
    if((s.state==R::Prepared&&s.applied)||(s.state==R::Confirmed&&(!s.applied||!accepted(*s.applied)))||
        (s.state==R::Rejected&&(!s.applied||accepted(*s.applied)||*s.applied==S::Storage)))return false;
    if(s.state==R::Diverged&&s.applied==S::Storage)return false;
    const auto &c=reply.checkpoint;
    const bool prior=c.sequence==s.priorSequence&&c.digest==s.priorDigest;
    const bool after=s.batch&&c.sequence==s.batch->sequence&&c.batchId==s.batch->id&&c.observedAtMs==s.batch->observedAtMs&&
        c.digest==sha256Hex(s.priorDigest+"|"+batchText(*s.batch));
    if(s.state!=R::Diverged&&!(s.state==R::Prepared?(prior||after):s.state==R::Confirmed?after:prior))return false;
    if(reply.applied&&accepted(*reply.applied))return s.state==R::Confirmed&&s.batch&&r.batch&&batchText(*s.batch)==batchText(*r.batch);
    return true;
}
}
bool encodeEnergyHistoryRequest(const EnergyHistoryRequest &r,std::string *wire){
    if(!wire||!valid(r))return false;
    try{
        std::string text=std::string(r.limits.compaction?"[\"CMEQ3\",":r.recovery==EnergyRecoveryMode::Volatile?"[\"CMEQ1\",":"[\"CMEQ2\",")+std::to_string(r.generation)+","+std::to_string(r.ticket)+","+jsonQuote(r.file.u8string())+","+bindingText(r.binding)+",["+
            std::to_string(r.limits.maxBatches)+","+std::to_string(r.limits.maxReadings)+","+std::to_string(r.limits.maxSessions)+","+std::to_string(r.limits.maxWindows)+","+
            std::to_string(r.limits.maxDatabaseBytes)+"],"+std::to_string(static_cast<unsigned>(r.mode))+","+std::to_string(r.nowUtcMs)+","+
            (r.batch?batchText(*r.batch):"null")+",[";
        for(size_t i=0;i<r.queries.size();++i){if(i)text+=',';const auto &q=r.queries[i];text+="["+std::to_string(q.beginUtcMs)+","+std::to_string(q.endUtcMs)+","+jsonQuote(q.registerId)+","+jsonQuote(q.periodId)+"]";}
        text+="]";if(r.recovery!=EnergyRecoveryMode::Volatile||r.limits.compaction)text+=","+std::to_string(static_cast<unsigned>(r.recovery));
        if(r.limits.compaction)text+=",["+std::to_string(r.limits.compaction->afterBatches)+","+std::to_string(r.limits.compaction->maxSegments)+"]";
        text+="]";if(text.size()>requestLimit||!boundedPublicJson(text))return false;*wire=std::move(text);return true;
    }catch(...){return false;}
}
bool decodeEnergyHistoryRequest(const std::string &wire,EnergyHistoryRequest *out){
    if(!out||wire.size()>requestLimit||!boundedPublicJson(wire))return false;
    JsonValue v;if(!parseJsonStrict(wire,&v,nullptr)||!v.isArray()||v.items.empty()||!v.items[0].isString())return false;
    const bool compact=v.items[0].text=="CMEQ3",persistent=v.items[0].text=="CMEQ2";
    if(!array(v,compact?12:persistent?11:10)||(!compact&&!persistent&&v.items[0].text!="CMEQ1"))return false;
    const auto &a=v.items;EnergyHistoryRequest r;std::string path;uint64_t mode=0,n[5]={};
    if(!u(a[1],r.generation,UINT64_MAX)||!u(a[2],r.ticket,UINT64_MAX)||!str(a[3],path)||
        !parseBindingValue(a[4],&r.binding)||!array(a[5],5)||!u(a[6],mode,1)||!time(a[7],r.nowUtcMs)||!a[9].isArray()||a[9].items.size()>4)return false;
    for(size_t i=0;i<5;++i)if(!u(a[5].items[i],n[i],128*1024*1024))return false;
    r.limits={static_cast<size_t>(n[0]),static_cast<size_t>(n[1]),static_cast<size_t>(n[2]),static_cast<size_t>(n[3]),n[4]};r.mode=static_cast<EnergyHistoryOpen>(mode);
    if(persistent||compact){uint64_t recoveryMode=0;if(!u(a[10],recoveryMode,3)||(!compact&&!recoveryMode))return false;r.recovery=static_cast<EnergyRecoveryMode>(recoveryMode);}
    if(compact){uint64_t after=0,segments=0;if(!array(a[11],2)||!u(a[11].items[0],after,4096)||!u(a[11].items[1],segments,32768))return false;
        r.limits.compaction=EnergyCompactionPolicy{static_cast<size_t>(after),static_cast<size_t>(segments)};}
    try{r.file=std::filesystem::u8path(path);}catch(...){return false;}
    if(!null(a[8])){EnergyHistoryBatch b;if(!parseBatchValue(a[8],r.binding,&b))return false;r.batch=std::move(b);}
    for(const auto &s:a[9].items){EnergyHistoryQuery q;if(!array(s,4)||!time(s.items[0],q.beginUtcMs)||!time(s.items[1],q.endUtcMs)||!str(s.items[2],q.registerId,128)||!str(s.items[3],q.periodId,128))return false;r.queries.push_back(std::move(q));}
    if(!valid(r))return false;
    *out=std::move(r);return true;
}
bool encodeEnergyHistoryReply(const EnergyHistoryRequest &r,const EnergyHistoryReply &reply,std::string *wire){
    if(!wire||!valid(r)||reply.totals.size()>4)return false;
    const bool persistent=r.recovery!=EnergyRecoveryMode::Volatile;if(persistent!=reply.recovery.has_value())return false;
    const bool compact=r.limits.compaction.has_value();
    std::string text=std::string(compact?"[\"CMEA3\",":persistent?"[\"CMEA2\",":"[\"CMEA1\",")+std::to_string(reply.generation)+","+std::to_string(reply.ticket)+","+std::to_string(static_cast<unsigned>(reply.opened))+","+
        (reply.applied?std::to_string(static_cast<unsigned>(*reply.applied)):"null")+","+headText(reply.checkpoint,persistent,compact)+",[";
    for(size_t i=0;i<reply.totals.size();++i){if(i)text+=',';text+=totalText(reply.totals[i]);}
    text+="]";if(persistent)text+=","+recoveryText(*reply.recovery);else if(compact)text+=",null";text+="]";
    EnergyHistoryReply checked;if(!decodeEnergyHistoryReply(r,text,&checked))return false;*wire=std::move(text);return true;
}
bool decodeEnergyHistoryReply(const EnergyHistoryRequest &r,const std::string &wire,EnergyHistoryReply *out){
    if(!out||!valid(r)||wire.size()>localTaskResponseLimit||!boundedPublicJson(wire))return false;
    const bool persistent=r.recovery!=EnergyRecoveryMode::Volatile;
    const bool compact=r.limits.compaction.has_value();
    JsonValue v;if(!parseJsonStrict(wire,&v,nullptr)||!array(v,(persistent||compact)?8:7))return false;
    const auto &a=v.items;EnergyHistoryReply reply;
    if(!a[0].isString()||a[0].text!=(compact?"CMEA3":persistent?"CMEA2":"CMEA1")||!u(a[1],reply.generation,UINT64_MAX)||!u(a[2],reply.ticket,UINT64_MAX)||reply.generation!=r.generation||reply.ticket!=r.ticket||
        !status(a[3],reply.opened)||!head(a[5],r,reply.checkpoint)||!a[6].isArray())return false;
    if(!null(a[4])){EnergyHistoryStatus s;if(!status(a[4],s))return false;reply.applied=s;}
    if(reply.opened==S::DetailExpired||reply.checkpoint.status==S::DetailExpired||(!compact&&reply.applied==S::DetailExpired))return false;
    if(compact&&!persistent&&!null(a[7]))return false;
    if(reply.opened!=EnergyHistoryStatus::Ok){if(reply.applied||!a[6].items.empty()||reply.checkpoint.status==EnergyHistoryStatus::Ok)return false;}
    else{
        if(reply.applied.has_value()!=r.batch.has_value()||a[6].items.size()!=r.queries.size())return false;
        for(size_t i=0;i<r.queries.size();++i){EnergyHistoryTotals t;if(!total(a[6].items[i],r,r.queries[i],t))return false;
            if(t.status==EnergyHistoryStatus::Ok&&(reply.checkpoint.status!=EnergyHistoryStatus::Ok||t.observedAtMs!=reply.checkpoint.observedAtMs))return false;
            const auto &c=reply.checkpoint;
            const auto start=(std::max)(t.beginUtcMs,r.binding.historyBeginUtcMs),end=(std::min)(t.endUtcMs,c.throughUtcMs);
            if(t.coveredDeviceMs>static_cast<uint64_t>((std::max)(int64_t{0},end-start))*r.binding.channels.size()||t.coveredDeviceMs>c.readingCount*60000)return false;
            reply.totals.push_back(std::move(t));}
        if(reply.applied==EnergyHistoryStatus::Ok||reply.applied==EnergyHistoryStatus::Replay){const auto &c=reply.checkpoint;
            if(c.status!=EnergyHistoryStatus::Ok||c.sequence<r.batch->sequence||c.observedAtMs<r.batch->observedAtMs)return false;
            if(reply.applied==EnergyHistoryStatus::Ok&&(c.sequence!=r.batch->sequence||c.batchId!=r.batch->id||c.observedAtMs!=r.batch->observedAtMs))return false;
            if(c.sequence==r.batch->sequence&&(c.batchId!=r.batch->id||c.observedAtMs!=r.batch->observedAtMs))return false;
        }
        if(reply.applied==S::DetailExpired&&(!r.batch||reply.checkpoint.status!=S::Ok||r.batch->sequence>=reply.checkpoint.compactedSequence))return false;
    }
    if(persistent){EnergyRecoverySnapshot s;if(!recovery(a[7],r,reply,s))return false;
        if(reply.applied&&accepted(*reply.applied)&&s.status!=S::Ok)return false;
        reply.recovery=std::move(s);}
    *out=std::move(reply);return true;
}
EnergyHistoryReply executeEnergyHistoryRequest(const EnergyHistoryRequest &r){
    EnergyHistoryReply reply;reply.generation=r.generation;reply.ticket=r.ticket;
    if(!valid(r)){reply.opened=EnergyHistoryStatus::Invalid;return reply;}
    const bool persistent=r.recovery!=EnergyRecoveryMode::Volatile;EnergyRecoveryJournal journal;
    if(persistent){reply.recovery=EnergyRecoverySnapshot{};reply.opened=journal.open(r);
        if(reply.opened!=S::Ok){reply.recovery->status=reply.opened;return reply;}
        if(r.mode==EnergyHistoryOpen::CreateNew&&journal.exists()){reply.opened=S::Conflict;reply.recovery->status=S::Conflict;return reply;}}
    EnergyHistoryLedger ledger;reply.opened=ledger.open(r.file,r.binding,r.nowUtcMs,r.mode,r.limits);
    if(reply.opened!=EnergyHistoryStatus::Ok){if(persistent)reply.recovery->status=reply.opened;return reply;}
    if(persistent&&!journal.exists()){
        reply.opened=journal.initialize(ledger.checkpoint(r.nowUtcMs));
        if(reply.opened!=S::Ok){reply.recovery->status=reply.opened;return reply;}
    }
    if(persistent){
        const auto before=journal.inspect(ledger,r.nowUtcMs);reply.recovery=before;
        if(r.batch){
            if(before.status!=S::Ok)reply.applied=before.status;
            else if(before.state==R::Diverged)reply.applied=S::Conflict;
            else if(r.recovery==EnergyRecoveryMode::Retry){
                if(!journal.matches(r))reply.applied=S::Conflict;
                else if(before.state==R::Confirmed)reply.applied=ledger.verifyCommitted(*r.batch,r.nowUtcMs);
                else if(before.state==R::Rejected)reply.applied=before.applied;
                else if(before.state!=R::Prepared)reply.applied=S::Conflict;
                else{
                    reply.applied=ledger.apply(*r.batch,r.nowUtcMs);
                    // Any failed retry preserves the old Prepared uncertainty, even a deterministic current rejection.
                    if(accepted(*reply.applied)&&journal.settle(*reply.applied)!=S::Ok)reply.applied=S::Storage;
                }
            }else if(before.state==R::Prepared)reply.applied=S::Conflict;
            else{
                reply.applied=journal.prepare(r,ledger.checkpoint(r.nowUtcMs));
                if(reply.applied==S::Ok){
#ifdef CANARY_ENERGY_RECOVERY_TESTING
                    if(r.batch->id=="fixture.exit-prepared"||(r.file.filename()=="fixture.compact-prepared.db"&&r.batch->sequence==2))std::_Exit(88);
#endif
                    reply.applied=ledger.apply(*r.batch,r.nowUtcMs);
#ifdef CANARY_ENERGY_RECOVERY_TESTING
                    if((r.batch->id=="fixture.exit-committed"||(r.file.filename()=="fixture.compact-committed.db"&&r.batch->sequence==2))&&accepted(*reply.applied))std::_Exit(89);
#endif
                    if(*reply.applied!=S::Storage&&journal.settle(*reply.applied)!=S::Ok)reply.applied=S::Storage;
                }
            }
        }
        reply.recovery=journal.inspect(ledger,r.nowUtcMs);
    }else if(r.batch)reply.applied=ledger.apply(*r.batch,r.nowUtcMs);
    reply.checkpoint=ledger.checkpoint(r.nowUtcMs);
    for(const auto &q:r.queries){
        auto t=q.registerId.empty()?ledger.query(q.beginUtcMs,q.endUtcMs,r.nowUtcMs):ledger.registerTotal(q.registerId,q.periodId,q.endUtcMs,r.nowUtcMs);
        if(t.beginUtcMs!=q.beginUtcMs||t.endUtcMs!=q.endUtcMs)t.status=EnergyHistoryStatus::Invalid;
        if(t.status!=EnergyHistoryStatus::Ok){const auto status=t.status;t={};t.status=status;t.beginUtcMs=q.beginUtcMs;t.endUtcMs=q.endUtcMs;}
        reply.totals.push_back(std::move(t));
    }return reply;
}
EnergyHistoryProcessResult EnergyHistoryProcessClient::transact(const EnergyHistoryRequest &r,const std::atomic<bool> &cancel,unsigned timeout){
    EnergyHistoryProcessResult result;std::string wire;
    if(cancel){result.transport=LocalTaskStatus::Cancelled;return result;}
    if(!encodeEnergyHistoryRequest(r,&wire)){result.transport=LocalTaskStatus::Invalid;return result;}
    if(fence_&&!encodeEnergyCollectorGuardRequest({localTaskProcessId(),*fence_,r},&wire)){result.transport=LocalTaskStatus::Invalid;return result;}
    const auto task=process_.run(wire,cancel,timeout);result.transport=task.status;
    if(task.status==LocalTaskStatus::Ok){EnergyHistoryReply reply;if(decodeEnergyHistoryReply(r,task.response,&reply))result.reply=std::move(reply);else result.transport=LocalTaskStatus::Unavailable;}
    result.writeOutcomeUnknown=(r.batch.has_value()||r.mode==EnergyHistoryOpen::CreateNew)&&task.childStarted&&(!result.reply||result.reply->opened==EnergyHistoryStatus::Storage||
        result.reply->applied==EnergyHistoryStatus::Storage||(result.reply->opened==EnergyHistoryStatus::Ok&&result.reply->checkpoint.status!=EnergyHistoryStatus::Ok)||
        (result.reply->recovery&&(result.reply->recovery->status!=S::Ok||result.reply->recovery->state==R::Prepared||result.reply->recovery->state==R::Diverged)));
    return result;
}
} // namespace om
