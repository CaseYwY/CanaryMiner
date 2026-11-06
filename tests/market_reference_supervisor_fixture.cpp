// Native terminal + actual owner/parser with synthetic responses and inert stdin-drained children only.
#include "cpu_telemetry_fixture.h"
#include "product/supervisor.h"
#include "product/manual_market.h"
#include "product/market_reference_runtime.h"
#include <csignal>
#include <fstream>
#include <iostream>
using namespace om;
namespace {
volatile sig_atomic_t stopped=0;
void stopSignal(int){stopped=1;}
struct Counts {unsigned factories=0,starts=0,views=0,joins=0;std::atomic<unsigned> fetches{0};bool revoked=false,late=false;};
class Counted final:public MarketReferenceSink {
    Counts &counts_;std::unique_ptr<MarketReferenceSink> runtime_;
   public:
    explicit Counted(Counts &counts):counts_(counts){
        runtime_=makeMarketReferenceRuntime([&counts](const PublicJsonRequest &request,const std::atomic<bool> &,PublicHttpsLimits){
            ++counts.fetches;PublicJsonResponse response;response.status=PublicHttpsStatus::Ok;response.httpStatus=200;response.observedUtc=static_cast<uint64_t>(time(nullptr));
            response.body=request.feed==PublicFeed::GatePearlTicker?"[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"2.50\"}]":"{\"error\":[],\"result\":{\"USDTZUSD\":{\"c\":[\"0.98\",\"1\"]}}}";return response;
        },{},MarketOrigin::SyntheticFixture,false);
    }
    bool start(uint64_t generation)override{++counts_.starts;return runtime_->start(generation);}
    PublicMarketView view(uint64_t utc,uint64_t mono)const override{++counts_.views;counts_.late=counts_.late||counts_.revoked;return runtime_->view(utc,mono);}
    void requestStop()override{counts_.revoked=true;runtime_->requestStop();}
    void stop()override{++counts_.joins;runtime_->stop();}
};
}
int main(int argc,char **argv){
    for(int i=1;i+1<argc;++i)if(std::string(argv[i])=="--fixture-child"){
        std::ofstream(std::filesystem::u8path(argv[i+1]),std::ios::app)<<"start\n";
        std::cout<<"Synthetic inert child ready\n"<<std::flush;while(std::cin.get()!=EOF){}return kExitOk;
    }
    if(argc<2||argc>3)return 2;
    const auto dir=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(dir);Counts counts;
    if(!platformInit())return 3;
    StatsServer local;std::string error;if(!local.start("127.0.0.1",0,[]{return "{}";},"{}",&error)){platformShutdown();return 4;}
    std::signal(SIGINT,stopSignal);std::signal(SIGTERM,stopSignal);
    SupervisorOptions options;options.exe=std::filesystem::absolute(std::filesystem::u8path(argv[0])).u8string();options.version="fixture";options.archLabel="CPU SYNTHETIC ONLY";
    WorkerSeed seed;seed.index=0;seed.uuid="SYNTHETIC-GPU-0";seed.name="inert fixture";options.seeds={seed};
    options.userArgs={"--fixture-child",(dir/"worker-starts").u8string()};options.workerPortBase=local.port();options.apiEnabled=false;options.dashboard=true;options.stop=&stopped;options.drainMs=1000;
    options.noMarket=argc==3&&std::string(argv[2])=="--no-market";
    options.manualMarketPreferencesPath=(dir/"manual-market-preferences.json").u8string();options.manualMarketFactory=makeManualMarketProjector;
    options.uiPreferencesPath=(dir/"ui-preferences.json").u8string();options.runtimePreferencesPath=(dir/"runtime-preferences.json").u8string();
    options.marketReferenceFactory=[&]()->std::unique_ptr<MarketReferenceSink>{++counts.factories;return std::make_unique<Counted>(counts);};
    Supervisor supervisor(std::move(options));const auto result=supervisor.run();local.stop();platformShutdown();
    std::ofstream(dir/"result.json")<<"{\"factories\":"<<counts.factories<<",\"starts\":"<<counts.starts<<",\"views\":"<<counts.views<<",\"joins\":"<<counts.joins<<",\"fetches\":"<<counts.fetches.load()<<",\"late\":"<<(counts.late?"true":"false")<<"}\n";
    return result;
}
