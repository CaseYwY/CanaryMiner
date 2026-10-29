#include "energy_trace_process.h"
#include "energy_recovery.h"
#include "json_dom.h"
#include "public_https.h"
#include <utility>
namespace om {
namespace {
using S=EnergyHistoryStatus;
bool numberLine(const std::string &wire,size_t &offset,uint64_t maximum,uint64_t &value){
    const auto end=wire.find('\n',offset);if(end==std::string::npos||end==offset||end-offset>20||(end-offset>1&&wire[offset]=='0'))return false;
    value=0;for(size_t i=offset;i<end;++i){const auto c=wire[i];if(c<'0'||c>'9'||static_cast<unsigned>(c-'0')>maximum||value>(maximum-static_cast<unsigned>(c-'0'))/10)return false;value=value*10+static_cast<unsigned>(c-'0');}
    if(value>maximum)return false;
    offset=end+1;return true;
}
S sourceStatus(const EnergyHistoryReply &r){
    if(r.opened!=S::Ok)return r.opened;
    if(r.checkpoint.status!=S::Ok)return r.checkpoint.status;
    if(!r.recovery)return S::Storage;
    if(r.recovery->status!=S::Ok)return r.recovery->status;
    if(r.recovery->state!=EnergyRecoveryState::Empty&&r.recovery->state!=EnergyRecoveryState::Confirmed&&r.recovery->state!=EnergyRecoveryState::Rejected)return S::Conflict;
    return r.totals.size()==1?r.totals[0].status:S::Storage;
}
std::string traceText(const std::optional<PowerTrace> &trace){
    if(!trace)return "null";
    std::string text="[";
    for(size_t i=0;i<trace->intervals.size();++i){const auto &row=trace->intervals[i];if(i)text+=',';
        text+="["+std::to_string(row.beginUtcMs)+","+std::to_string(row.endUtcMs)+","+jsonQuote(row.watts.text())+"]";}
    return text+"]";
}
}
bool validEnergyTraceRequest(const EnergyTraceRequest &r){
    const auto &s=r.source;
    if(!r.maxIntervals||r.maxIntervals>512||s.mode!=EnergyHistoryOpen::Existing||s.recovery!=EnergyRecoveryMode::Inspect||s.batch||s.queries.size()!=1||!s.queries[0].registerId.empty()||!s.queries[0].periodId.empty())return false;
    std::string inner;return encodeEnergyHistoryRequest(s,&inner)&&inner.size()+32<=energyTraceRequestLimit;
}
bool encodeEnergyTraceRequest(const EnergyTraceRequest &r,uint64_t parent,std::string *wire){
    if(!wire||!parent||parent>UINT32_MAX||!validEnergyTraceRequest(r))return false;
    std::string inner;if(!encodeEnergyHistoryRequest(r.source,&inner))return false;
    *wire="CMETQ1\n"+std::to_string(parent)+"\n"+std::to_string(r.maxIntervals)+"\n"+inner;return wire->size()<=energyTraceRequestLimit;
}
bool decodeEnergyTraceRequest(const std::string &wire,EnergyTraceRequest *out,uint64_t *parent){
    if(!out||!parent||wire.size()>energyTraceRequestLimit||wire.rfind("CMETQ1\n",0)!=0)return false;
    size_t offset=7;uint64_t pid=0,maximum=0;EnergyTraceRequest r;
    if(!numberLine(wire,offset,UINT32_MAX,pid)||!pid||!numberLine(wire,offset,512,maximum)||!maximum||!decodeEnergyHistoryRequest(wire.substr(offset),&r.source))return false;
    r.maxIntervals=static_cast<size_t>(maximum);if(!validEnergyTraceRequest(r))return false;*parent=pid;*out=std::move(r);return true;
}
bool encodeEnergyTraceReply(const EnergyTraceRequest &r,const EnergyTraceReply &reply,std::string *wire){
    if(!wire||!validEnergyTraceRequest(r)||(reply.completeTrace&&reply.completeTrace->intervals.size()>r.maxIntervals))return false;
    if(reply.completeTrace){const auto &a=reply.completeTrace->evidence,&b=r.source.binding.evidence;
        if(a.basis!=b.basis||a.origin!=b.origin||a.scope!=b.scope||a.adapter!=b.adapter)return false;}
    std::string inner;if(!encodeEnergyHistoryReply(r.source,reply.source,&inner))return false;
    const auto text="CMETA1\n"+std::to_string(inner.size())+"\n"+inner+"\n"+std::to_string(static_cast<unsigned>(reply.status))+"\n"+traceText(reply.completeTrace);
    EnergyTraceReply checked;if(!decodeEnergyTraceReply(r,text,&checked))return false;*wire=text;return true;
}
bool decodeEnergyTraceReply(const EnergyTraceRequest &r,const std::string &wire,EnergyTraceReply *out){
    if(!out||!validEnergyTraceRequest(r)||wire.size()>localTaskResponseLimit||wire.rfind("CMETA1\n",0)!=0)return false;
    EnergyTraceReply reply;size_t offset=7;uint64_t bytes=0,status=0;
    if(!numberLine(wire,offset,localTaskResponseLimit,bytes)||!bytes||bytes>=wire.size()-offset||wire[offset+static_cast<size_t>(bytes)]!='\n'||
        !decodeEnergyHistoryReply(r.source,wire.substr(offset,static_cast<size_t>(bytes)),&reply.source))return false;
    offset+=static_cast<size_t>(bytes)+1;
    if(!numberLine(wire,offset,static_cast<unsigned>(S::Storage),status))return false;
    reply.status=static_cast<S>(status);
    const auto text=wire.substr(offset);JsonValue trace;if(!boundedPublicJson(text)||!parseJsonStrict(text,&trace,nullptr))return false;
    const auto base=sourceStatus(reply.source);
    if(base!=S::Ok){if(reply.status!=base||trace.type!=JsonValue::Type::Null)return false;}
    else if(reply.status!=S::Ok){if((reply.status!=S::Capacity&&reply.status!=S::Storage)||trace.type!=JsonValue::Type::Null)return false;}
    else{
        const auto &totals=reply.source.totals[0];
        if(totals.coverage!=EnergyHistoryCoverage::Complete){if(trace.type!=JsonValue::Type::Null)return false;}
        else{
            if(!trace.isArray()||trace.items.empty()||trace.items.size()>r.maxIntervals)return false;
            PowerTrace power;power.evidence=r.source.binding.evidence;int64_t cursor=totals.beginUtcMs;ExactDecimal integral,maximum,count;
            ExactDecimal::parse("4294967.295",&maximum);ExactDecimal::parse(std::to_string(r.source.binding.channels.size()),&count);if(!maximum.multiply(count,&maximum))return false;
            for(const auto &row:trace.items){uint64_t begin=0,end=0;ExactDecimal watts,duration,part;
                if(!row.isArray()||row.items.size()!=3||!row.items[0].asUint64(&begin)||!row.items[1].asUint64(&end)||begin!=static_cast<uint64_t>(cursor)||end<=begin||end>static_cast<uint64_t>(totals.endUtcMs)||
                    !row.items[2].isString()||row.items[2].text.size()>32||!ExactDecimal::parse(row.items[2].text,&watts)||watts.text()!=row.items[2].text||watts.negative()||watts.scale()>3||watts.compare(maximum)>0||
                    (!power.intervals.empty()&&power.intervals.back().watts.compare(watts)==0))return false;
                ExactDecimal::parse(std::to_string(end-begin),&duration);if(!watts.multiply(duration,&part)||!integral.add(part,&integral))return false;
                power.intervals.push_back({static_cast<int64_t>(begin),static_cast<int64_t>(end),watts});cursor=static_cast<int64_t>(end);
            }
            if(cursor!=totals.endUtcMs||!totals.completeKwh||integral.compare(totals.completeKwh->numerator)!=0)return false;
            reply.completeTrace=std::move(power);
        }
    }
    if(text!=traceText(reply.completeTrace))return false;
    *out=std::move(reply);return true;
}
EnergyTraceReply executeEnergyTraceRequest(const EnergyTraceRequest &r){
    EnergyTraceReply reply;auto &source=reply.source;source.generation=r.source.generation;source.ticket=r.source.ticket;source.recovery=EnergyRecoverySnapshot{};
    if(!validEnergyTraceRequest(r)){reply.status=source.opened=source.recovery->status=S::Invalid;return reply;}
    EnergyRecoveryJournal journal;source.opened=journal.open(r.source,EnergyRecoveryOpen::QueryOnly);
    if(source.opened!=S::Ok){reply.status=source.recovery->status=source.opened;return reply;}
    EnergyHistoryLedger ledger;source.opened=ledger.open(r.source.file,r.source.binding,r.source.nowUtcMs,EnergyHistoryOpen::ExistingQuery,r.source.limits);
    if(source.opened!=S::Ok){reply.status=source.recovery->status=source.opened;return reply;}
    source.checkpoint=ledger.checkpoint(r.source.nowUtcMs);source.recovery=journal.inspect(ledger,r.source.nowUtcMs);
    const auto &q=r.source.queries[0];source.totals.push_back(ledger.query(q.beginUtcMs,q.endUtcMs,r.source.nowUtcMs));reply.status=sourceStatus(source);
    if(reply.status!=S::Ok)return reply;
    const auto trace=ledger.queryTrace(q.beginUtcMs,q.endUtcMs,r.source.nowUtcMs,r.maxIntervals);reply.status=trace.status;reply.completeTrace=trace.completeTrace;return reply;
}
EnergyTraceProcessResult EnergyTraceProcessClient::transact(const EnergyTraceRequest &r,const std::atomic<bool> &cancel,unsigned timeout){
    EnergyTraceProcessResult result;if(cancel){result.transport=LocalTaskStatus::Cancelled;return result;}
    std::string wire;if(!encodeEnergyTraceRequest(r,localTaskProcessId(),&wire)){result.transport=LocalTaskStatus::Invalid;return result;}
    const auto task=process_.run(wire,cancel,timeout);result.transport=task.status;
    if(task.status==LocalTaskStatus::Ok){EnergyTraceReply reply;if(decodeEnergyTraceReply(r,task.response,&reply))result.reply=std::move(reply);else result.transport=LocalTaskStatus::Unavailable;}
    return result;
}
} // namespace om
