// Synthetic frozen public-reference owner adapter. No network, accounts, mining, source or release qualification.
#include "product/market_reference_runtime.h"
#include "product_test_util.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
using namespace om;
namespace {
constexpr uint64_t epoch=1800000000;
bool waitUntil(const std::function<bool()> &ready){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!ready()){if(std::chrono::steady_clock::now()>=until)return false;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return true;}
std::string joined(const PublicMarketView &view){std::string text=view.heading+'\n';for(const auto &line:view.lines)text+=line+'\n';return text;}
PublicJsonResponse quote(PublicFeed feed,uint64_t utc){PublicJsonResponse r;r.status=PublicHttpsStatus::Ok;r.httpStatus=200;r.observedUtc=utc;
    r.body=feed==PublicFeed::GatePearlTicker?"[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"2.50\"}]":"{\"error\":[],\"result\":{\"USDTZUSD\":{\"c\":[\"0.98\",\"1\"]}}}";return r;}
void lifecycle(){
    std::atomic<unsigned> calls{0},feeds{0},badRequests{0};std::atomic<uint64_t> ms{1000};auto clock=[&]{const auto now=ms.load();return std::make_pair(now,epoch+now/1000);};
    auto fetch=[&](const PublicJsonRequest &request,const std::atomic<bool> &,PublicHttpsLimits){++calls;
        if((request.feed!=PublicFeed::GatePearlTicker&&request.feed!=PublicFeed::KrakenUsdtTicker)||request.fromUtc||request.toUtc)++badRequests;
        else feeds.fetch_or(1u<<static_cast<unsigned>(request.feed));
        return quote(request.feed,clock().second);};
    auto runtime=makeMarketReferenceRuntime(fetch,{},MarketOrigin::SyntheticFixture,false,clock);
    CHECK(!runtime->view(epoch+1,1000).enabled&&calls==0);CHECK(runtime->start(7)&&!runtime->start(8));
    CHECK(waitUntil([&]{return joined(runtime->view(epoch+1,1000)).find("Latest PRL/USD reference: 2.45")!=std::string::npos;}));
    const auto original=runtime->view(epoch+1,1000);const auto text=joined(original);CHECK(original.enabled&&calls==2&&text.find("SYNTHETIC FIXTURE - NOT LIVE")!=text.npos&&text.find("trade-event time NOT SUPPLIED")!=text.npos&&text.find("time convention unqualified/unavailable")!=text.npos);
    runtime->requestStop();CHECK(joined(runtime->view(epoch+1,1000)).find("Market references: stopped")!=std::string::npos);
    CHECK(joined(runtime->view(epoch+1,1000)).find("Latest PRL/USD reference: N/A")!=std::string::npos);
    CHECK(!runtime->start(9));runtime->stop();runtime->stop();CHECK(calls==2&&feeds==5&&badRequests==0&&!runtime->start(10)&&joined(original)==text);
    for(bool cancel:{false,true}){auto unstarted=makeMarketReferenceRuntime(fetch,{},MarketOrigin::SyntheticFixture,false,clock);if(cancel)unstarted->requestStop();else CHECK(!unstarted->start(0));CHECK(!unstarted->start(1)&&!unstarted->view(epoch,0).enabled);}
    auto invalidPolicy=MarketPollingPolicy{};invalidPolicy.quoteRefreshMs=999;auto invalid=makeMarketReferenceRuntime(fetch,invalidPolicy,MarketOrigin::SyntheticFixture,false,clock);CHECK(!invalid->start(1)&&!invalid->view(epoch,0).enabled);
    auto convention=makeMarketReferenceRuntime(fetch,{},MarketOrigin::ProviderResponse,true,clock);CHECK(!convention->start(1)&&!convention->view(epoch,0).enabled);
    auto noFetch=makeMarketReferenceRuntime({});CHECK(!noFetch->start(1)&&!noFetch->view(epoch,0).enabled&&calls==2);
}
void late(){
    std::mutex mutex;std::condition_variable wait;bool entered=false,release=false;std::atomic<unsigned> calls{0};
    auto runtime=makeMarketReferenceRuntime([&](const PublicJsonRequest &request,const std::atomic<bool> &,PublicHttpsLimits){
        ++calls;std::unique_lock<std::mutex> lock(mutex);entered=true;wait.notify_all();wait.wait(lock,[&]{return release;});return quote(request.feed,epoch);}, {},MarketOrigin::SyntheticFixture,false,[]{return std::make_pair(uint64_t(1000),epoch);});
    CHECK(runtime->start(1));{std::unique_lock<std::mutex> lock(mutex);CHECK(wait.wait_for(lock,std::chrono::seconds(3),[&]{return entered;}));}
    const auto before=std::chrono::steady_clock::now();runtime->requestStop();CHECK(std::chrono::steady_clock::now()-before<std::chrono::seconds(1));
    const auto stopped=joined(runtime->view(epoch,1000));CHECK(stopped.find("Market references: stopped")!=stopped.npos&&stopped.find("2.5")==stopped.npos&&!runtime->start(2));
    {std::lock_guard<std::mutex> lock(mutex);release=true;}wait.notify_all();runtime->stop();
    const auto settled=joined(runtime->view(epoch,1000));CHECK(calls==1&&settled.find("2.5")==settled.npos&&settled.find("Latest PRL/USD reference: N/A")!=settled.npos);
}
void staleAndClock(){
    std::atomic<uint64_t> ms{1000};std::atomic<unsigned> calls{0};auto clock=[&]{const auto now=ms.load();return std::make_pair(now,epoch+now/1000);};
    auto policy=MarketPollingPolicy{};policy.quoteRefreshMs=86400000;
    auto runtime=makeMarketReferenceRuntime([&](const PublicJsonRequest &request,const std::atomic<bool> &,PublicHttpsLimits){++calls;return quote(request.feed,clock().second);},policy,MarketOrigin::SyntheticFixture,false,clock);
    CHECK(runtime->start(1)&&waitUntil([&]{return calls==2&&joined(runtime->view(epoch+1,1000)).find("Latest PRL/USD reference: 2.45")!=std::string::npos;}));
    CHECK(joined(runtime->view(epoch+122,122000)).find("stale/invalid cached reference 2.5")!=std::string::npos);
    ms=999;CHECK(waitUntil([&]{return joined(runtime->view(epoch,999)).find("clock fault")!=std::string::npos;}));
    const auto fault=joined(runtime->view(epoch,999));CHECK(fault.find("Latest PRL/USD reference: N/A")!=fault.npos&&calls==2);runtime->stop();CHECK(!runtime->start(2));
}
void helperFault(){
    std::atomic<unsigned> calls{0};
    auto runtime=makeMarketReferenceRuntime([&](const PublicJsonRequest &,const std::atomic<bool> &,PublicHttpsLimits){
        ++calls;PublicJsonResponse r;r.status=PublicHttpsStatus::Unreaped;return r;
    },{},MarketOrigin::SyntheticFixture,false,[]{return std::make_pair(uint64_t(1000),epoch);});
    CHECK(runtime->start(1));CHECK(waitUntil([&]{return joined(runtime->view(epoch,1000)).find("helper ownership fault")!=std::string::npos;}));
    const auto text=joined(runtime->view(epoch,1000));CHECK(text.find("Latest PRL/USD reference: N/A")!=text.npos&&calls==1);
    runtime->stop();CHECK(!runtime->start(2)&&calls==1);
}
}
int main(){lifecycle();late();staleAndClock();helperFault();return finishTests("frozen native public-reference ownership with memory-only revocation and post-drain settlement");}
