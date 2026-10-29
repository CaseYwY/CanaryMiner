#include "energy_history_process.h"
#include "energy_history_model.h"
#include "json_dom.h"
#include "public_https.h" // shared bounded JSON lexical guard only
#include <algorithm>

namespace om {
namespace {
using namespace energy_history_detail;
constexpr size_t requestLimit=64*1024;
bool array(const JsonValue &v,size_t n){return v.isArray()&&v.items.size()==n;}
bool u(const JsonValue &v,uint64_t &n,uint64_t max=INT64_MAX){return v.asUint64(&n)&&n<=max;}
bool time(const JsonValue &v,int64_t &n){uint64_t x=0;if(!u(v,x,lastUtcMs))return false;n=static_cast<int64_t>(x);return true;}
bool str(const JsonValue &v,std::string &s,size_t max=4096){if(!v.isString()||v.text.size()>max||v.text.find('\0')!=std::string::npos)return false;s=v.text;return true;}
bool null(const JsonValue &v){return v.type==JsonValue::Type::Null;}
bool status(const JsonValue &v,EnergyHistoryStatus &s){uint64_t n=0;if(!u(v,n,static_cast<unsigned>(EnergyHistoryStatus::Storage)))return false;s=static_cast<EnergyHistoryStatus>(n);return true;}
bool decimal(const JsonValue &v,ExactDecimal &d){return v.isString()&&v.text.size()<=150&&ExactDecimal::parse(v.text,&d)&&d.text()==v.text&&!d.negative();}
bool valid(const EnergyHistoryRequest &r){
    if(!r.generation||!r.ticket||!localTaskPath(r.file)||!validEnergyHistoryBinding(r.binding)||!validEnergyHistoryLimits(r.limits)||!instant(r.nowUtcMs)||r.binding.historyBeginUtcMs>r.nowUtcMs||
        (r.mode!=EnergyHistoryOpen::Existing&&r.mode!=EnergyHistoryOpen::CreateNew)||r.queries.size()>4)return false;
    for(const auto &q:r.queries)if(!instant(q.beginUtcMs)||!instant(q.endUtcMs)||q.beginUtcMs>=q.endUtcMs||q.endUtcMs>r.nowUtcMs||
        (q.registerId.empty()? !q.periodId.empty():!identifier(q.registerId)||!identifier(q.periodId)))return false;
    return !r.batch||boundedEnergyHistoryBatch(r.binding,*r.batch,r.nowUtcMs);
}
std::string headText(const EnergyHistoryCheckpoint &c){
    return "["+std::to_string(static_cast<unsigned>(c.status))+","+std::to_string(c.sequence)+","+std::to_string(c.readingCount)+","+std::to_string(c.sessionCount)+","+
        std::to_string(c.windowCount)+","+jsonQuote(c.batchId)+","+jsonQuote(c.sessionId)+","+std::to_string(c.observedAtMs)+","+std::to_string(c.throughUtcMs)+"]";
}
bool head(const JsonValue &v,const EnergyHistoryRequest &r,EnergyHistoryCheckpoint &c){
    if(!array(v,9))return false;
    const auto &a=v.items;
    if(!status(a[0],c.status)||!u(a[1],c.sequence,r.limits.maxBatches)||!u(a[2],c.readingCount,r.limits.maxReadings)||!u(a[3],c.sessionCount,r.limits.maxSessions)||
        !u(a[4],c.windowCount,r.limits.maxWindows)||!str(a[5],c.batchId,128)||!str(a[6],c.sessionId,128)||!time(a[7],c.observedAtMs)||!time(a[8],c.throughUtcMs))return false;
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
}
bool encodeEnergyHistoryRequest(const EnergyHistoryRequest &r,std::string *wire){
    if(!wire||!valid(r))return false;
    try{
        std::string text="[\"CMEQ1\","+std::to_string(r.generation)+","+std::to_string(r.ticket)+","+jsonQuote(r.file.u8string())+","+bindingText(r.binding)+",["+
            std::to_string(r.limits.maxBatches)+","+std::to_string(r.limits.maxReadings)+","+std::to_string(r.limits.maxSessions)+","+std::to_string(r.limits.maxWindows)+","+
            std::to_string(r.limits.maxDatabaseBytes)+"],"+std::to_string(static_cast<unsigned>(r.mode))+","+std::to_string(r.nowUtcMs)+","+
            (r.batch?batchText(*r.batch):"null")+",[";
        for(size_t i=0;i<r.queries.size();++i){if(i)text+=',';const auto &q=r.queries[i];text+="["+std::to_string(q.beginUtcMs)+","+std::to_string(q.endUtcMs)+","+jsonQuote(q.registerId)+","+jsonQuote(q.periodId)+"]";}
        text+="]]";if(text.size()>requestLimit||!boundedPublicJson(text))return false;*wire=std::move(text);return true;
    }catch(...){return false;}
}
bool decodeEnergyHistoryRequest(const std::string &wire,EnergyHistoryRequest *out){
    if(!out||wire.size()>requestLimit||!boundedPublicJson(wire))return false;
    JsonValue v;if(!parseJsonStrict(wire,&v,nullptr)||!array(v,10))return false;
    const auto &a=v.items;EnergyHistoryRequest r;std::string path;uint64_t mode=0,n[5]={};
    if(!a[0].isString()||a[0].text!="CMEQ1"||!u(a[1],r.generation,UINT64_MAX)||!u(a[2],r.ticket,UINT64_MAX)||!str(a[3],path)||
        !parseBindingValue(a[4],&r.binding)||!array(a[5],5)||!u(a[6],mode,1)||!time(a[7],r.nowUtcMs)||!a[9].isArray()||a[9].items.size()>4)return false;
    for(size_t i=0;i<5;++i)if(!u(a[5].items[i],n[i],128*1024*1024))return false;
    r.limits={static_cast<size_t>(n[0]),static_cast<size_t>(n[1]),static_cast<size_t>(n[2]),static_cast<size_t>(n[3]),n[4]};r.mode=static_cast<EnergyHistoryOpen>(mode);
    try{r.file=std::filesystem::u8path(path);}catch(...){return false;}
    if(!null(a[8])){EnergyHistoryBatch b;if(!parseBatchValue(a[8],r.binding,&b))return false;r.batch=std::move(b);}
    for(const auto &s:a[9].items){EnergyHistoryQuery q;if(!array(s,4)||!time(s.items[0],q.beginUtcMs)||!time(s.items[1],q.endUtcMs)||!str(s.items[2],q.registerId,128)||!str(s.items[3],q.periodId,128))return false;r.queries.push_back(std::move(q));}
    if(!valid(r))return false;
    *out=std::move(r);return true;
}
bool encodeEnergyHistoryReply(const EnergyHistoryRequest &r,const EnergyHistoryReply &reply,std::string *wire){
    if(!wire||!valid(r)||reply.totals.size()>4)return false;
    std::string text="[\"CMEA1\","+std::to_string(reply.generation)+","+std::to_string(reply.ticket)+","+std::to_string(static_cast<unsigned>(reply.opened))+","+
        (reply.applied?std::to_string(static_cast<unsigned>(*reply.applied)):"null")+","+headText(reply.checkpoint)+",[";
    for(size_t i=0;i<reply.totals.size();++i){if(i)text+=',';text+=totalText(reply.totals[i]);}
    text+="]]";EnergyHistoryReply checked;if(!decodeEnergyHistoryReply(r,text,&checked))return false;*wire=std::move(text);return true;
}
bool decodeEnergyHistoryReply(const EnergyHistoryRequest &r,const std::string &wire,EnergyHistoryReply *out){
    if(!out||!valid(r)||wire.size()>localTaskResponseLimit||!boundedPublicJson(wire))return false;
    JsonValue v;if(!parseJsonStrict(wire,&v,nullptr)||!array(v,7))return false;
    const auto &a=v.items;EnergyHistoryReply reply;
    if(!a[0].isString()||a[0].text!="CMEA1"||!u(a[1],reply.generation,UINT64_MAX)||!u(a[2],reply.ticket,UINT64_MAX)||reply.generation!=r.generation||reply.ticket!=r.ticket||
        !status(a[3],reply.opened)||!head(a[5],r,reply.checkpoint)||!a[6].isArray())return false;
    if(!null(a[4])){EnergyHistoryStatus s;if(!status(a[4],s))return false;reply.applied=s;}
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
    }
    *out=std::move(reply);return true;
}
EnergyHistoryReply executeEnergyHistoryRequest(const EnergyHistoryRequest &r){
    EnergyHistoryReply reply;reply.generation=r.generation;reply.ticket=r.ticket;
    if(!valid(r)){reply.opened=EnergyHistoryStatus::Invalid;return reply;}
    EnergyHistoryLedger ledger;reply.opened=ledger.open(r.file,r.binding,r.nowUtcMs,r.mode,r.limits);
    if(reply.opened!=EnergyHistoryStatus::Ok)return reply;
    if(r.batch)reply.applied=ledger.apply(*r.batch,r.nowUtcMs);
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
    const auto task=process_.run(wire,cancel,timeout);result.transport=task.status;
    if(task.status==LocalTaskStatus::Ok){EnergyHistoryReply reply;if(decodeEnergyHistoryReply(r,task.response,&reply))result.reply=std::move(reply);else result.transport=LocalTaskStatus::Unavailable;}
    result.writeOutcomeUnknown=(r.batch.has_value()||r.mode==EnergyHistoryOpen::CreateNew)&&task.childStarted&&(!result.reply||result.reply->opened==EnergyHistoryStatus::Storage||
        result.reply->applied==EnergyHistoryStatus::Storage||(result.reply->opened==EnergyHistoryStatus::Ok&&result.reply->checkpoint.status!=EnergyHistoryStatus::Ok));
    return result;
}
} // namespace om
