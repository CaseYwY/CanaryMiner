#include "billing_source_fixture.h"
#include "product/billing_query_process.h"
#include "product/billing_model.h"
#include "product/tariff_declaration.h"
#include "product_test_util.h"
#include <chrono>
#include <fstream>
#include <thread>
#include <iostream>
using namespace billing_source_fixture;
namespace {
using S=BillingStatus;using R=BillingRecoveryState;using O=BillingRecoveryOpen;
constexpr int64_t at=T+2*electricity_fixture::hour;
std::filesystem::path side(std::filesystem::path p,const char *s=".billing-recovery"){p+=s;return p;}
std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
BillingQueryRequest request(const std::filesystem::path &file){BillingQueryRequest r;r.generation=123;r.ticket=456;r.file=file;r.binding=billing_fixture::binding();r.nowUtcMs=at;return r;}
BillingQueryPageRequest page(const BillingQueryRequest &r){return {r.generation,r.ticket,r.file,sha256Hex(billing_detail::bindingText(r.binding,r.limits)),"",r.nowUtcMs,0};}
BillingChunk large(){auto c=billing_fixture::chunk();c.observedUtcMs=T+512;c.trace.intervals.clear();
    for(unsigned i=0;i<512;++i)c.trace.intervals.push_back({T+i,T+i+1,d(std::string(88,i%2?'2':'1'))});
    return c;}
void create(const BillingQueryRequest &r,bool big=false){BillingRecoveryStore store;CHECK(store.open(r.file,r.binding,r.nowUtcMs,O::CreateNew)==S::Ok);
    CHECK(store.submit(big?large():billing_fixture::chunk(),r.nowUtcMs)==S::Ok);}
template<class F>bool until(F f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return f();}
}
int main(int argc,char **argv){
    if(argc!=4&&argc!=6)return 2;
    const auto reader=std::filesystem::absolute(argv[1]),fixture=std::filesystem::absolute(argv[2]);BillingQueryProcessClient client(reader);std::atomic<bool> cancel{false};
    if(argc==6){const std::string mode=argv[4];const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[5]));
        if(mode=="source-inspect"){BillingQueryRequest r;r.file=root;r.binding=binding(side(root,".source.db"));r.generation=r.ticket=1;r.nowUtcMs=now;
            const auto result=client.transact(r,cancel,5000);if(!result.reply)return 3;
            std::cout<<static_cast<unsigned>(result.reply->status)<<' '<<static_cast<unsigned>(result.reply->state)<<' '<<result.reply->committed.sequence<<'\n';return 0;}
        auto r=request(root/"fixture.billing-query-hang.db");if(mode=="parent-probe"){create(r);BillingQueryProcessClient stalled(fixture);stalled.transact(r,cancel,60000);return 4;}
        const auto result=client.transact(r,cancel,5000);if(result.reply&&result.reply->state==R::Confirmed){std::cout<<"synthetic committed monetary view ready\n";return 0;}return 5;}
    const auto root=std::filesystem::path(makeScratchDir("billing-query"));auto r=request(root/"bill.db");create(r,true);
    const auto journal=bytes(side(r.file)),database=bytes(r.file);const auto result=client.transact(r,cancel,5000);
    CHECK(result.transport==LocalTaskStatus::Ok&&result.reply&&result.reply->state==R::Confirmed&&result.reply->original&&result.reply->original->trace.intervals.size()==512&&result.reply->committed.sequence==1);
    CHECK(bytes(side(r.file))==journal&&bytes(r.file)==database);
    auto p=page(r);const auto first=executeBillingQueryRequest(p);CHECK(first.status==S::Ok&&first.total>billingQueryPageBytes&&first.data.size()==billingQueryPageBytes);
    std::string wire;BillingQueryPageRequest decoded;uint64_t parent=0;CHECK(encodeBillingQueryRequest(p,localTaskProcessId(),&wire)&&decodeBillingQueryRequest(wire,&decoded,&parent)&&parent==localTaskProcessId());
    CHECK(!decodeBillingQueryRequest(wire+" ",&decoded,&parent));
    BillingQueryPageReply parsed;CHECK(encodeBillingQueryReply(p,first,&wire)&&decodeBillingQueryReply(p,wire,&parsed));
    CHECK(!decodeBillingQueryReply(p,wire+"x",&parsed)&&!decodeBillingQueryReply(p,wire.substr(0,wire.size()-1),&parsed));
    for(unsigned k=0;k<5;++k){auto bad=p;if(k==0)bad.ticket++;if(k==1)bad.offset=billingQueryPageBytes;if(k==2)bad.expectedView=std::string(64,'f');if(k==3)bad.generation++;if(k==4)bad.offset=1;
        CHECK(!decodeBillingQueryReply(bad,wire,&parsed));}
    p.expectedView=first.viewDigest;p.offset=billingQueryPageBytes;const auto tail=executeBillingQueryRequest(p);CHECK(tail.status==S::Ok&&tail.total==first.total&&tail.data.size()==first.total-p.offset);
    // A complete second revision between pages must reject the old view rather than mix its prefixes.
    {BillingRecoveryStore store;CHECK(store.open(r.file,r.binding,at)==S::Ok);auto c=billing_fixture::chunk(2);c.observedUtcMs=T+1000;c.trace=electricity_fixture::load("1",T+512,T+1000);CHECK(store.submit(c,at)==S::Ok);}
    CHECK(executeBillingQueryRequest(p).status==S::Conflict);const auto changed=client.transact(r,cancel,5000);CHECK(changed.reply&&changed.reply->committed.sequence==2);
    {auto bad=r;bad.binding.plan.revisions[0].baseRatePerKwh=d("0.7");const auto mismatch=client.transact(bad,cancel,5000);CHECK(mismatch.reply&&mismatch.reply->status==S::Attribution&&!mismatch.reply->original);}
    {auto missing=request(root/"missing"/"bill.db");const auto read=client.transact(missing,cancel,5000);CHECK(read.reply&&read.reply->status==S::Storage&&!std::filesystem::exists(missing.file.parent_path()));}
    {auto empty=request(root/"empty.db");{BillingRecoveryStore store;CHECK(store.open(empty.file,empty.binding,at,O::CreateNew)==S::Ok);}
        const auto read=client.transact(empty,cancel,5000);CHECK(read.reply&&read.reply->state==R::Empty&&!read.reply->original&&!read.reply->committed.provisionalTotal);}
    {auto rejected=request(root/"rejected.db");{BillingRecoveryStore store;CHECK(store.open(rejected.file,rejected.binding,at,O::CreateNew)==S::Ok);auto c=billing_fixture::chunk();c.trace.intervals[0].beginUtcMs++;CHECK(store.submit(c,at)==S::Calculation);}
        const auto read=client.transact(rejected,cancel,5000);CHECK(read.reply&&read.reply->state==R::Rejected&&read.reply->committed.sequence==0&&read.reply->original);}
    {BillingLedger bypass;CHECK(bypass.open(r.file,r.binding,at)==S::Ok);auto c=billing_fixture::chunk(3);c.observedUtcMs=T+2000;c.trace=electricity_fixture::load("1",T+1000,T+2000);CHECK(bypass.apply(c,at)==S::Ok);}
    {const auto read=client.transact(r,cancel,5000);CHECK(read.reply&&read.reply->state==R::Diverged&&read.reply->committed.sequence==3&&read.reply->original->sequence==2);}
    for(unsigned k=0;k<6;++k){auto bad=page(r);if(k==0)bad.generation=0;if(k==1)bad.ticket=0;if(k==2)bad.bindingDigest="bad";if(k==3)bad.file="relative";if(k==4)bad.offset=1;if(k==5)bad.offset=billingQueryPageBytes;
        CHECK(!encodeBillingQueryRequest(bad,localTaskProcessId(),&wire));}
    {LocalTaskProcess wrong(std::filesystem::absolute(argv[3]));CHECK(encodeBillingQueryRequest(page(r),localTaskProcessId(),&wire)&&wrong.run(wire,cancel,5000).status==LocalTaskStatus::Unavailable);}
#if defined(__linux__)
    {LocalTaskProcess transport(reader);CHECK(encodeBillingQueryRequest(page(r),localTaskProcessId()==1?2:1,&wire));CHECK(transport.run(wire,cancel,5000).status==LocalTaskStatus::Unavailable);}
#endif
    for(const auto *name:{"fixture.billing-query-exit.db","fixture.billing-query-bad.db","fixture.billing-query-extra.db","fixture.billing-query-damaged.db"}){
        auto q=request(root/name);create(q);BillingQueryProcessClient fake(fixture);const auto read=fake.transact(q,cancel,5000);CHECK(read.transport==LocalTaskStatus::Unavailable&&!read.reply);}
    {auto q=request(root/"fixture.billing-query-change.db");create(q,true);BillingQueryProcessClient fake(fixture);const auto read=fake.transact(q,cancel,5000);CHECK(read.reply&&read.reply->status==S::Conflict&&!read.reply->original&&read.reply->committed.status==S::Storage);}
    {auto q=request(root/"fixture.billing-query-hang.db");create(q);BillingQueryProcessClient stalled(fixture);BillingQueryResult ended;
        std::thread pending([&]{ended=stalled.transact(q,cancel,5000);});const bool ready=until([&]{return std::filesystem::exists(side(q.file,".fixture-ready"));});CHECK(ready);
        CHECK(stalled.transact(q,cancel,5000).transport==LocalTaskStatus::Busy);cancel=true;pending.join();CHECK(ended.transport==LocalTaskStatus::Cancelled&&!ended.reply);cancel=false;
        CHECK(stalled.transact(q,cancel,100).transport==LocalTaskStatus::Timeout);cancel=true;CHECK(client.transact(r,cancel,5000).transport==LocalTaskStatus::Cancelled);cancel=false;}
    // Complete plan codecs cover signed rates, offsets, optional/date rules, dynamic rows and shared tiers without changing admission.
    for(unsigned kind=0;kind<4;++kind){auto b=billing_fixture::binding();b.plan.clock={CivilClockKind::FixedStandardOffset,"",-18000};b.plan.minimumCycleSubtotal=d("1");auto &rev=b.plan.revisions[0];
        if(kind==0){rev.timeMode=TimeRateMode::Calendar;CalendarRate c;c.key="fixture.calendar";c.date=CivilDate{2026,10,28};c.publishedUtcMs=T;c.ratePerKwh=d("-0.12");rev.calendar={c};}
        if(kind==1){rev.timeMode=TimeRateMode::Dynamic;rev.baseRatePerKwh=d("0");rev.dynamic={{T,T+2*electricity_fixture::hour,T,d("-0.3")}};}
        if(kind>=2){b.plan.tiers=kind==2?TierMode::Progressive:TierMode::AllUnits;rev.bands={{d("1"),d("0.3")},{std::nullopt,d("-0.1")}};}
        const auto text=billing_detail::bindingText(b,{});BillingBinding decodedBinding;BillingLimits limits;CHECK(billing_detail::parseBinding(text,decodedBinding,limits)&&billing_detail::bindingText(decodedBinding,limits)==text);
        CHECK(!billing_detail::parseBinding(text+" ",decodedBinding,limits));JsonValue tree;CHECK(parseJsonStrict(text,&tree,nullptr));tree.items[5].items[7].items[2].text="-64801";CHECK(!billing_detail::parseBinding(tariffJson(tree),decodedBinding,limits));}
    std::filesystem::remove_all(root);return finishTests("billing query");
}
