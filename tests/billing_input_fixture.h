#pragma once
#include "billing_source_fixture.h"
#include "product/billing_input_model.h"
#include <fstream>
namespace billing_input_fixture {
using namespace billing_source_fixture;
using S=BillingStatus;using R=BillingRecoveryState;using P=BillingInputPhase;using A=BillingInputAction;
constexpr int64_t at=T+2*electricity_fixture::hour;
inline std::filesystem::path side(std::filesystem::path p,const char *suffix=".billing-input"){p+=suffix;return p;}
inline std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
inline BillingInputPlan plan(const std::filesystem::path &file){BillingInputPlan p;p.context.generation=123;p.context.ticket=456;p.context.file=file;p.context.binding=billing_fixture::binding();p.context.nowUtcMs=at;return p;}
inline BillingRecoverySnapshot inspect(const BillingInputPlan &p){BillingRecoveryStore store;if(store.open(p.context.file,p.context.binding,p.context.nowUtcMs,BillingRecoveryOpen::QueryOnly,p.context.limits)!=S::Ok)return {};return store.inspect(p.context.nowUtcMs);}
inline BillingInputRequest request(const BillingInputPlan &p,std::string *payload){BillingInputRequest r;r.generation=p.context.generation;r.ticket=p.context.ticket;r.action=A::Begin;
    if(!prepareBillingInput(p,&r.manifest,payload))r.generation=0;
    return r;}
inline bool readManifest(const std::filesystem::path &file,BillingInputManifest &out){const auto raw=bytes(side(file));size_t offset=6;std::string model;JsonValue value;
    return raw.rfind("CMBI1\n",0)==0&&billing_input_detail::part(raw,offset,billing_input_detail::manifestLimit,model)&&parseJsonStrict(model,&value,nullptr)&&billing_input_detail::parseManifest(value,out);}
inline BillingBinding largeBinding(){auto b=billing_fixture::binding();b.cycleEndUtcMs=T+64000;b.plan.revisions.clear();
    for(unsigned i=0;i<64;++i){auto revision=billing_fixture::binding().plan.revisions[0];revision.key="fixture.revision."+std::to_string(i);revision.beginUtcMs=T+i*1000;revision.endUtcMs=T+(i+1)*1000;revision.timeMode=TimeRateMode::Calendar;
        for(unsigned j=0;j<64;++j){CalendarRate c;c.key="fixture.calendar."+std::to_string(j)+std::string(80,'"');c.beginMinute=0;c.endMinute=1440;c.weekdayMask=127;c.monthMask=4095;c.priority=j;c.publishedUtcMs=T;c.ratePerKwh=d("0.3");revision.calendar.push_back(c);}
        b.plan.revisions.push_back(std::move(revision));}
    auto last=b;
    for(unsigned width=81;width<110;++width){for(auto &revision:b.plan.revisions)for(size_t j=0;j<revision.calendar.size();++j)revision.calendar[j].key="fixture.calendar."+std::to_string(j)+std::string(width,'"');
        if(billing_detail::bindingText(b,{}).size()>billing_detail::bindingLimit)break;
        last=b;}
    return last;
}
inline BillingChunk largeChunk(){auto c=billing_fixture::chunk();c.observedUtcMs=T+512;c.trace.intervals.clear();
    for(unsigned i=0;i<512;++i)c.trace.intervals.push_back({T+i,T+i+1,d("1."+std::string(46,'0')+(i%2?"1":"2"))});
    return c;}
} // namespace billing_input_fixture
