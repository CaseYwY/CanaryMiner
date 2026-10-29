#include "forecast_query_process.h"
namespace om {
bool validForecastQuery(const CreditProcessRequest &r){
    if(!r.binding.cursor||r.batch||r.recovery!=CreditRecoveryMode::Inspect||r.queries.empty()||r.queries.size()>4)return false;
    std::string checked;return encodeCreditRequest(r,&checked)&&checked.size()+24<=forecastQueryLimit;
}
bool encodeForecastQuery(const CreditProcessRequest &r,uint64_t pid,std::string *wire){
    if(!wire||!pid||pid>UINT32_MAX||!validForecastQuery(r))return false;
    std::string inner;if(!encodeCreditRequest(r,&inner))return false;
    *wire="CMFQ1\n"+std::to_string(pid)+"\n"+inner;return wire->size()<=forecastQueryLimit;
}
bool decodeForecastQuery(const std::string &wire,CreditProcessRequest *r,uint64_t *pid){
    if(!r||!pid||wire.size()>forecastQueryLimit||wire.rfind("CMFQ1\n",0)!=0)return false;
    const auto end=wire.find('\n',6);if(end==std::string::npos||end==6||end-6>10||wire[6]=='0')return false;
    uint64_t parent=0;for(size_t i=6;i<end;++i){if(wire[i]<'0'||wire[i]>'9')return false;parent=parent*10+static_cast<unsigned>(wire[i]-'0');}
    if(!parent||parent>UINT32_MAX)return false;
    CreditProcessRequest parsed;if(!decodeCreditRequest(wire.substr(end+1),&parsed)||!validForecastQuery(parsed))return false;
    *r=std::move(parsed);*pid=parent;return true;
}
CreditProcessResult ForecastQueryProcessClient::transact(const CreditProcessRequest &r,const std::atomic<bool> &cancel,unsigned timeout){
    CreditProcessResult result;if(cancel){result.transport=LocalTaskStatus::Cancelled;return result;}
    std::string wire;if(!encodeForecastQuery(r,localTaskProcessId(),&wire)){result.transport=LocalTaskStatus::Invalid;return result;}
    const auto task=process_.run(wire,cancel,timeout);result.transport=task.status;
    if(task.status==LocalTaskStatus::Ok){CreditProcessReply reply;
        if(task.response.rfind("CMFA1\n",0)==0&&decodeCreditReply(r,task.response.substr(6),&reply))result.reply=std::move(reply);
        else result.transport=LocalTaskStatus::Unavailable;}
    return result; // logical facts are never mutated by this executable; no write/retry authority is produced
}
} // namespace om
