// Synthetic frozen public-reference owner adapter. No network, accounts, mining, source or release qualification.
#include "product/market_reference_runtime.h"
#include "product/market_ticker_runtime.h"
#include "product_test_util.h"
#include <chrono>
#include <condition_variable>
#include <fstream>
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

void launchPolicy(){
    const auto module=std::filesystem::absolute("synthetic-module");
    for(unsigned mask=0;mask<64;++mask){
        const PublicTickerLaunch launch{bool(mask&1),bool(mask&2),bool(mask&4),bool(mask&8),bool(mask&16),bool(mask&32)};
        CHECK(static_cast<bool>(installedPublicTickerFactory(module,launch))==((mask&31)==15));
    }
    const PublicTickerLaunch enabled{true,true,true,true,false};
    CHECK(!installedPublicTickerFactory("relative-module",enabled));
    CHECK(!installedPublicTickerFactory({},enabled));
    auto invalid=enabled;
    invalid.dnsServer="resolver.example";
    CHECK(!installedPublicTickerFactory(module,invalid));
    MarketPollingPolicy policy;
    policy.scheduleEconomicRequests=false;
    MarketPollingPlan plan;
    CHECK(plan.start(1,1000,epoch,policy,false));
    CHECK(!plan.takeEconomicRequest(1000,epoch));
    CHECK(!plan.takeEconomicRequest(3601000,epoch+3600));
    for (auto feed : {PublicFeed::GatePearlTicker,PublicFeed::KrakenUsdtTicker}) {
        const auto ticket=plan.next(3601000,epoch+3600);
        CHECK(ticket && ticket->request.feed==feed && !ticket->request.fromUtc && !ticket->request.toUtc);
        if(ticket)CHECK(plan.complete(*ticket,quote(feed,epoch+3600),MarketDataStatus::Ok,3601000,epoch+3600));
    }
    CHECK(!plan.next(3601000,epoch+3600));
}

void tradeOwner(){
    MarketPollingPolicy policy;
    policy.publicTrades=true;
    MarketPollingPlan plan;
    CHECK(!plan.start(1,1000,epoch*1000,policy,false));
    policy.scheduleEconomicRequests=false;
    CHECK(!plan.start(1,1000,epoch*1000,policy,true));
    CHECK(plan.start(1,1000,epoch*1000+100,policy,false));
    CHECK(!plan.takeEconomicRequest(1000,epoch*1000+100));
    const auto ticket=plan.next(1000,epoch*1000+100);
    CHECK(ticket && ticket->request.feed==PublicFeed::GatePearlTrades && ticket->startedUtc==epoch);
    CHECK(!plan.next(1100,epoch*1000+99) && plan.clockFault());

    enum class Refresh { OlderEvent, ConflictingPrice, ConflictingBatch, OlderConflictingBatch };
    for (const auto refresh:{Refresh::OlderEvent,Refresh::ConflictingPrice,Refresh::ConflictingBatch,Refresh::OlderConflictingBatch}) {
        std::atomic<uint64_t> mono{1000};
        std::atomic<unsigned> requests{0}, badRequests{0};
        const auto clock=[&]{const auto elapsed=mono.load();return std::make_pair(elapsed,epoch*1000+elapsed);};
        policy.quoteRefreshMs=1000;
        MarketDataOwner owner([&](const PublicJsonRequest &request,const std::atomic<bool> &,PublicHttpsLimits limits){
            const auto call=requests.fetch_add(1);
            const auto isGate=request.feed==PublicFeed::GatePearlTrades;
            if ((!isGate && request.feed!=PublicFeed::KrakenUsdtTrades) || request.fromUtc || request.toUtc ||
                limits.bodyBytes!=maxPublicTradeBodyBytes) ++badRequests;
            PublicJsonResponse r;r.status=PublicHttpsStatus::Ok;r.httpStatus=200;r.observedUtc=clock().second/1000;
            if (isGate) {
                const bool later=call>=2;
                const bool older=later && (refresh==Refresh::OlderEvent || refresh==Refresh::OlderConflictingBatch);
                const auto event=epoch-(older ? 3 : 2);
                const auto row=[event](const char *id,const char *price){
                    return std::string("{\"id\":\"")+id+"\",\"create_time_ms\":\""+std::to_string(event*1000)+
                        ".1001\",\"currency_pair\":\"PEARL_USDT\",\"price\":\""+price+"\"}";
                };
                r.body="["+row("1",later && refresh==Refresh::ConflictingPrice ? "3" : "2.50");
                if (later && (refresh==Refresh::ConflictingBatch || refresh==Refresh::OlderConflictingBatch)) r.body+=","+row("2","3");
                r.body+="]";
            } else {
                r.body="{\"error\":[],\"result\":{\"USDTZUSD\":[[\"0.98\",\"1\","+std::to_string(epoch-3)+
                    ".125,\"b\",\"m\",\"\",4]],\"last\":\"123\"}}";
            }
            return r;
        },policy,MarketOrigin::ProviderResponse,false,clock);
        CHECK(owner.start(1));
        CHECK(waitUntil([&]{const auto snapshot=owner.snapshot();return snapshot->pearlTrade && snapshot->fxTrade;}));
        const auto original=owner.snapshot();
        CHECK(original->pearlTrade && original->fxTrade && !original->pearlQuote && !original->fxQuote &&
              original->economicRequests==0 && requests==2 && badRequests==0);
        const auto text=joined(marketDashboardView(*original,policy,epoch+1,1000));
        CHECK(text.find("PRL/USD trade reference: 2.45")!=text.npos &&
              text.find("Event UTC "+std::to_string(epoch-2)+".000100100")!=text.npos &&
              text.find("Receipt UTC ms "+std::to_string(epoch*1000+1000))!=text.npos);
        CHECK(joined(marketDashboardView(*original,policy,epoch+121,121000)).find(
            "PRL/USD trade reference: N/A")!=text.npos);
        const auto rolledBack=joined(marketDashboardView(*original,policy,epoch+116,120000));
        CHECK(rolledBack.find("Gate PRL/USDT trade: N/A")!=text.npos &&
              rolledBack.find("PRL/USD trade reference: N/A")!=text.npos);
        mono=2000;
        CHECK(waitUntil([&]{return owner.snapshot()->health[0].latched;}));
        const auto updated=owner.snapshot();
        if (refresh==Refresh::ConflictingPrice || refresh==Refresh::ConflictingBatch) {
            CHECK(!updated->pearlTrade && updated->health[0].data==MarketDataStatus::Coverage);
            CHECK(joined(marketDashboardView(*updated,policy,epoch+2,2000)).find("PRL/USD trade reference: N/A")!=text.npos);
        }
        else if (original->pearlTrade && updated->pearlTrade)
            CHECK(updated->pearlTrade->receivedUtcMs==original->pearlTrade->receivedUtcMs &&
                  updated->health[0].data==(refresh==Refresh::OlderEvent ? MarketDataStatus::Timestamp : MarketDataStatus::Coverage));
        else CHECK(false);
        owner.stop();
        const auto stopped=joined(marketDashboardView(*owner.snapshot(),policy,epoch+2,2000));
        CHECK(stopped.find("PRL/USD trade reference: N/A")!=stopped.npos && stopped.find("Event UTC ")==stopped.npos);
    }

    std::atomic<bool> receiptJump{false};
    MarketDataOwner jumped([&](const PublicJsonRequest &,const std::atomic<bool> &,PublicHttpsLimits){
        PublicJsonResponse r;r.status=PublicHttpsStatus::Ok;r.httpStatus=200;r.observedUtc=epoch+1;
        r.body="[{\"id\":\"1\",\"create_time_ms\":\""+std::to_string(epoch*1000)+
            "\",\"currency_pair\":\"PEARL_USDT\",\"price\":\"2.50\"}]";
        receiptJump=true;
        return r;
    },policy,MarketOrigin::ProviderResponse,false,[&]{
        return std::make_pair(uint64_t{1000},epoch*1000+(receiptJump.exchange(false) ? 7000 : 1000));
    });
    CHECK(jumped.start(1));
    CHECK(waitUntil([&]{return jumped.snapshot()->clockFault;}));
    const auto failed=jumped.snapshot();
    CHECK(failed->clockFault && !failed->running && !failed->pearlTrade && !failed->fxTrade);
    jumped.stop();

    auto runtime=makeMarketReferenceRuntime([=](const PublicJsonRequest &request,const std::atomic<bool> &,PublicHttpsLimits){
        PublicJsonResponse r;r.status=PublicHttpsStatus::Ok;r.httpStatus=200;r.observedUtc=epoch+1;
        if (request.feed==PublicFeed::GatePearlTrades) {
            r.body="[{\"id\":\"1\",\"create_time_ms\":\""+std::to_string(epoch*1000)+
                "\",\"currency_pair\":\"PEARL_USDT\",\"price\":\"2.50\"}]";
        } else {
            r.body="{\"error\":[],\"result\":{\"USDTZUSD\":[[\"0.98\",\"1\","+std::to_string(epoch)+
                ",\"b\",\"m\",\"\",4]],\"last\":\"123\"}}";
        }
        return r;
    },policy,MarketOrigin::ProviderResponse,false,[=]{return std::make_pair(uint64_t{1000},epoch*1000+1000);});
    CHECK(runtime->start(1));
    CHECK(waitUntil([&]{return joined(runtime->view(epoch+1,1000)).find("PRL/USD trade reference: 2.45")!=std::string::npos;}));
    runtime->stop();
}

int installedPath(const std::filesystem::path &fixture){
#if defined(__linux__)
#if !defined(__GLIBC__)
    return 77;
#elif !__GLIBC_PREREQ(2,40)
    return 77;
#endif
#endif
    const auto directory = std::filesystem::u8path(makeScratchDir("public-ticker-install"));
    const auto factory=installedPublicTickerFactory(directory/"synthetic-module",{true,true,true,true,false});
#if defined(_WIN32)
    const auto helper=directory/"canary_public_json_worker.exe";
#else
    const auto helper=directory/"canary_public_json_worker";
#endif
    CHECK(factory && !std::filesystem::exists(helper));
    auto now=[]{
        const auto [mono, utc] = marketSystemClock();
        return std::make_pair(utc, mono);
    };
    auto view=[&now](const MarketReferenceSink &runtime){
        const auto t=now();
        return joined(runtime.view(t.first,t.second));
    };
    const std::atomic<bool> cancelled{false};
    auto missingFetch=marketProcessFetch(helper);
    CHECK(missingFetch({PublicFeed::GatePearlTicker},cancelled,{}).status==PublicHttpsStatus::Unavailable);
    auto missing=factory();
    CHECK(missing->start(1));
    CHECK(view(*missing).find("N/A (missing/stale/skewed conversion)")!=std::string::npos);
    missing->stop();
    std::filesystem::copy_file(fixture,helper);
    auto runtime=factory();
    CHECK(!runtime->view(0,0).enabled && runtime->start(2));
    CHECK(waitUntil([&]{return view(*runtime).find("Latest PRL/USD reference: 2.45")!=std::string::npos;}));
    const auto displayed=view(*runtime);
    CHECK(displayed.find("observed UTC")!=std::string::npos);
    CHECK(displayed.find("trade-event time NOT SUPPLIED")!=std::string::npos);
    CHECK(displayed.find("time convention unqualified/unavailable")!=std::string::npos);
    runtime->requestStop();
    CHECK(view(*runtime).find("Latest PRL/USD reference: N/A")!=std::string::npos);
    runtime->stop();
    CHECK(!runtime->start(3));
    const auto requests = directory / "candle-requests";
    CHECK(!std::filesystem::exists(requests));
    const auto resolverRequests = directory / "resolver-requests";
    CHECK(!std::filesystem::exists(resolverRequests));
    const std::string resolver = "fd00::53";
    auto launch = PublicTickerLaunch{true,true,true,true,false,true,resolver};
    const auto withHour = installedPublicTickerFactory(directory/"synthetic-module",launch);
    launch.dnsServer = "192.168.50.53";  // an installed factory has already frozen its input
    for (const auto *mode : {"full", "gap-fx", "gap-pearl", "future"}) {
        std::ofstream(directory / "candle-scenario") << mode;
        auto session = withHour();
        CHECK(session->start(1));
        CHECK(waitUntil([&] {
            const auto text = view(*session);
            const auto fxAt = text.find("Coinbase USDT-USD: ");
            return fxAt != text.npos && (text.find("60/60", fxAt) != text.npos ||
                text.find("59/60", fxAt) != text.npos || text.find("ok/invalid market observation/minute time", fxAt) != text.npos);
        }));
        const std::string_view scenario(mode);
        const auto text = view(*session);
        CHECK(text.find(resolver) == text.npos);
        {
            std::ifstream reader(resolverRequests);
            std::string line;
            for (unsigned n = 0; n < 4; ++n)
                CHECK(std::getline(reader, line) && line == resolver);
            CHECK(!std::getline(reader, line));
        }
        std::filesystem::remove(resolverRequests);
        CHECK_MSG((std::string("Latest ticker with candle scenario ") + mode + "\n" + text).c_str(),
                  text.find("Latest PRL/USD reference: 2.45") != text.npos);
        CHECK(text.find("TWAP approximation | candidate data, finality unverified") != text.npos);
        const auto mean = "1h mean of minute closes: " +
            std::string(scenario == "gap-pearl" ? "N/A" : "3.000000") + " USDT/PRL | " +
            (scenario == "full" ? "3.500000" : "N/A") + " USD/PRL";
        CHECK(text.find(mean) != text.npos);
        CHECK(text.find(scenario == "gap-pearl" ? "MEXC PEARLUSDT: 59/60" : "MEXC PEARLUSDT: 60/60") != text.npos);
        if (scenario == "gap-fx") CHECK(text.find("Coinbase USDT-USD: 59/60") != text.npos);

        std::ifstream input(requests);
        unsigned pearlFeed=0, fxFeed=0;
        uint64_t pearlStart=0, pearlEnd=0, fxStart=0, fxEnd=0;
        CHECK(bool(input >> pearlFeed >> pearlStart >> pearlEnd >> fxFeed >> fxStart >> fxEnd));
        CHECK(pearlFeed == 4 && fxFeed == 5 && pearlStart == fxStart && pearlEnd == fxEnd && pearlEnd - pearlStart == 3600);
        unsigned unexpected=0;
        CHECK(!(input >> unexpected));
        input.close();
        std::filesystem::remove(requests);
        CHECK(text.find("1h UTC seconds [" + std::to_string(pearlStart) + ", " + std::to_string(pearlEnd) + ")") != text.npos);

        const auto t = now();
        const auto unavailable = "N/A USDT/PRL | N/A USD/PRL";
        CHECK(joined(session->view(t.first + 121, t.second)).find(unavailable) != text.npos);
        CHECK(joined(session->view(t.first, t.second + 121000)).find(unavailable) != text.npos);
        session->requestStop();
        CHECK(view(*session).find(unavailable) != text.npos);
        session->stop();
    }
    const auto event=marketSystemClock().second-3;
    std::ofstream(directory/"trade-response-7.json")<<"[{\"id\":\"4\",\"create_time_ms\":\""<<event*1000
        <<".25\",\"currency_pair\":\"PEARL_USDT\",\"price\":\"2.50\"}]";
    std::ofstream(directory/"trade-response-8.json")<<"{\"error\":[],\"result\":{\"USDTZUSD\":[[\"0.98\",\"1\","
        <<event<<".2499,\"b\",\"m\",\"\",5]],\"last\":\"123\"}}";
    const auto tickerRequests=directory/"ticker-requests";
    const auto originalTickerBytes=std::filesystem::file_size(tickerRequests);
    launch.hourPermitted=false;
    launch.tradesPermitted=true;
    launch.dnsServer=resolver;
    const auto withTrades=installedPublicTickerFactory(directory/"synthetic-module",launch);
    auto tradeRuntime=withTrades();
    CHECK(tradeRuntime && tradeRuntime->start(1));
    CHECK(waitUntil([&]{return view(*tradeRuntime).find("PRL/USD trade reference: 2.45")!=std::string::npos;}));
    const auto tradeDisplay=view(*tradeRuntime);
    CHECK(tradeDisplay.find("Event UTC ")!=tradeDisplay.npos && tradeDisplay.find("Receipt UTC ms ")!=tradeDisplay.npos);
    CHECK(tradeDisplay.find(resolver)==tradeDisplay.npos && tradeDisplay.find("Latest PRL/USD reference")==tradeDisplay.npos);
    tradeRuntime->stop();
    CHECK(view(*tradeRuntime).find("PRL/USD trade reference: N/A")!=tradeDisplay.npos);
    CHECK(std::filesystem::file_size(tickerRequests)==originalTickerBytes && !std::filesystem::exists(requests));
    {
        std::ifstream tradeRequests(directory/"trade-requests");
        unsigned gate=0, kraken=0, unexpected=0;
        CHECK(bool(tradeRequests>>gate>>kraken) && gate==7 && kraken==8 && !(tradeRequests>>unexpected));
        std::ifstream resolvers(resolverRequests);
        std::string first, second, extra;
        CHECK(bool(resolvers>>first>>second) && first==resolver && second==resolver && !(resolvers>>extra));
    }
    std::error_code ignored;
    std::filesystem::remove_all(directory,ignored); // only the scratch directory created above
    return finishTests("installed ticker helper path and native process");
}
}
int main(int argc,char **argv){
    if(argc==2)return installedPath(std::filesystem::u8path(argv[1]));
    lifecycle();late();staleAndClock();helperFault();launchPolicy();tradeOwner();
    return finishTests("frozen native public-reference ownership with memory-only revocation and post-drain settlement");
}
