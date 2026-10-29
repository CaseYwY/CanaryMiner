#include "billing_source_fixture.h"
#include "product/billing_retry_process.h"
#include "product/billing_model.h"
#include "product/local_store_lease.h"
#include "product_test_util.h"
#include <fstream>
#include <chrono>
#include <thread>
#include <iostream>
using namespace billing_source_fixture;
namespace {
using S=BillingStatus;using R=BillingRecoveryState;using O=BillingRecoveryOpen;
constexpr int64_t at=T+2*electricity_fixture::hour;
std::filesystem::path side(std::filesystem::path p,const char *s=".billing-recovery"){p+=s;return p;}
std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
BillingQueryRequest context(const std::filesystem::path &file){BillingQueryRequest r;r.generation=123;r.ticket=456;r.file=file;r.binding=billing_fixture::binding();r.nowUtcMs=at;return r;}
BillingRecoverySnapshot create(const BillingQueryRequest &r){BillingRecoveryStore store;CHECK(store.open(r.file,r.binding,r.nowUtcMs,O::CreateNew)==S::Ok&&store.submit(billing_fixture::chunk(),r.nowUtcMs)==S::Ok);return store.inspect(r.nowUtcMs);}
BillingRetryRequest request(const BillingQueryRequest &r,const BillingRecoverySnapshot &s){return {r.generation,r.ticket,r.file,sha256Hex(billing_detail::bindingText(r.binding,r.limits)),s.revision,sha256Hex(billing_detail::chunkText(*s.original)),r.nowUtcMs};}
template<class F>bool until(F f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return f();}
}
int main(int argc,char **argv){
    if(argc!=4&&argc!=5)return 2;
    const auto helper=std::filesystem::absolute(argv[1]);std::atomic<bool> cancel{false};
    if(argc==5){const std::string mode=argv[4];const auto file=std::filesystem::absolute(std::filesystem::u8path(argv[3]));
        if(mode=="parent-probe"){auto r=context(file);auto reviewed=create(r);BillingRetryProcessClient client(std::filesystem::absolute(argv[2]));client.transact(r,reviewed,cancel,60000);return 3;}
        BillingQueryRequest r;r.generation=r.ticket=1;r.file=file;r.binding=binding(side(file,".source.db"));r.nowUtcMs=now+1000;BillingRecoverySnapshot reviewed;
        {BillingRecoveryStore reader;if(reader.open(file,r.binding,r.nowUtcMs,O::QueryOnly)!=S::Ok)return 4;reviewed=reader.inspect(r.nowUtcMs);}
        BillingRetryProcessClient client(mode=="fixture"?std::filesystem::absolute(argv[2]):helper);const auto result=client.transact(r,reviewed,cancel,5000);
        std::cout<<"{\"transport\":"<<static_cast<unsigned>(result.transport)<<",\"applied\":"<<(result.applied?std::to_string(static_cast<unsigned>(*result.applied)):"null")<<",\"confirmed\":"<<(result.confirmed?"true":"false")<<",\"sequence\":"<<(result.confirmed?result.confirmed->committed.sequence:0)<<"}\n";return 0;}
    const auto fixture=std::filesystem::absolute(argv[2]),root=std::filesystem::path(makeScratchDir("billing-retry"));BillingRetryProcessClient client(helper);auto r=context(root/"bill.db");const auto reviewed=create(r);
    const auto original=bytes(side(r.file)),database=bytes(r.file);const auto first=client.transact(r,reviewed,cancel,5000);
    CHECK(first.transport==LocalTaskStatus::Ok&&first.applied==S::Replay&&first.confirmed&&first.confirmed->revision==reviewed.revision&&first.confirmed->committed.digest==reviewed.committed.digest);
    CHECK(bytes(side(r.file))==original&&bytes(r.file)==database);
    auto input=request(r,reviewed);std::string wire;BillingRetryRequest decoded;uint64_t parent=0;
    CHECK(encodeBillingRetryRequest(input,localTaskProcessId(),&wire)&&decodeBillingRetryRequest(wire,&decoded,&parent));CHECK(!decodeBillingRetryRequest(wire+" ",&decoded,&parent));
    const auto reply=executeBillingRetryRequest(input);BillingRetryReply parsed;CHECK(encodeBillingRetryReply(input,reply,&wire)&&decodeBillingRetryReply(input,wire,&parsed));CHECK(!decodeBillingRetryReply(input,wire+" ",&parsed));
    for(unsigned k=0;k<6;++k){auto bad=input;if(k==0)bad.revision=std::string(64,'f');if(k==1)bad.originalDigest=std::string(64,'e');if(k==2)bad.bindingDigest=std::string(64,'d');if(k==3)bad.nowUtcMs=T;if(k==4)bad.file=root/"missing.db";if(k==5)bad.file="relative";
        const auto denied=executeBillingRetryRequest(bad);CHECK(denied.status!=S::Ok&&denied.status!=S::Replay&&denied.state.empty()&&bytes(side(r.file))==original&&bytes(r.file)==database);}
    for(unsigned k=0;k<4;++k){auto bad=reviewed;if(k==0)bad.revision=std::string(64,'f');if(k==1)bad.original->id="fixture.changed";if(k==2)bad.state=R::Diverged;if(k==3)bad.recordedUtcMs++;
        CHECK(client.transact(r,bad,cancel,5000).transport==LocalTaskStatus::Invalid&&bytes(side(r.file))==original);}
    {LocalStoreLease lease;CHECK(lease.acquire(side(r.file,".billing-recovery.lock")));const auto locked=client.transact(r,reviewed,cancel,5000);CHECK(locked.applied==S::Storage&&!locked.confirmed);}
    {BillingRecoveryStore changed;CHECK(changed.open(r.file,r.binding,at)==S::Ok&&changed.submit(billing_fixture::chunk(2),at)==S::Ok);}
    const auto after=bytes(side(r.file));const auto stale=client.transact(r,reviewed,cancel,5000);CHECK(stale.applied==S::Conflict&&!stale.confirmed&&bytes(side(r.file))==after);
    {LocalTaskProcess wrong(std::filesystem::absolute(argv[3]));CHECK(encodeBillingRetryRequest(input,localTaskProcessId(),&wire)&&wrong.run(wire,cancel,5000).status==LocalTaskStatus::Unavailable);}
#if defined(__linux__)
    {LocalTaskProcess worker(helper);CHECK(encodeBillingRetryRequest(input,localTaskProcessId()==1?2:1,&wire));CHECK(worker.run(wire,cancel,5000).status==LocalTaskStatus::Unavailable);}
#endif
    for(const auto *name:{"fixture.billing-retry-lost.db","fixture.billing-retry-proof.db"}){auto q=context(root/name);const auto saved=create(q);BillingRetryProcessClient fake(fixture);const auto result=fake.transact(q,saved,cancel,5000);CHECK(result.transport==LocalTaskStatus::Unavailable&&!result.applied&&!result.confirmed);}
    {auto q=context(root/"fixture.billing-retry-hang.db");const auto saved=create(q);BillingRetryProcessClient stalled(fixture);BillingRetryResult ended;
        std::thread thread([&]{ended=stalled.transact(q,saved,cancel,5000);});CHECK(until([&]{return std::filesystem::exists(side(q.file,".fixture-ready"));}));
        CHECK(stalled.transact(q,saved,cancel,5000).transport==LocalTaskStatus::Busy);cancel=true;thread.join();CHECK(ended.transport==LocalTaskStatus::Cancelled&&!ended.confirmed);cancel=false;
        CHECK(stalled.transact(q,saved,cancel,100).transport==LocalTaskStatus::Timeout);cancel=true;CHECK(client.transact(q,saved,cancel,5000).transport==LocalTaskStatus::Cancelled);cancel=false;}
    std::filesystem::remove_all(root);return finishTests("billing retry");
}
