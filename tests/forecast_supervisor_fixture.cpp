// Native supervisor/terminal with synthetic workers and explicit fixture-only credit setup. No CUDA or pool traffic.
#include "income_forecast_fixture.h"
#include "product/supervisor.h"
#include <csignal>
#include <fstream>
using namespace om;
namespace {
volatile sig_atomic_t stopping=0;
void signalStop(int){stopping=1;}
int child(int argc,char **argv){
    int port=0;for(int i=1;i+1<argc;++i)if(std::string(argv[i])=="--api-port")port=std::atoi(argv[i+1]);
    if(!port||!platformInit())return 3;
    StatsSnapshot data;data.state="running";data.poolState="mining";data.devices.push_back(StatsDevice{});data.devices[0].outcomes.accepted=2;
    StatsServer server;std::string error;if(!server.start("127.0.0.1",port,[&]{return statsToJson(data);},"{}",&error))return 4;
    char input[8];while(std::fread(input,1,sizeof(input),stdin)>0){}server.stop();platformShutdown();return 0;
}
class FixtureSink final:public ForecastSink {
    IncomeForecastOwner owner_;
    ForecastScenario scenario_;
   public:
    FixtureSink(std::filesystem::path helper,std::filesystem::path file,CreditSourceContract binding,ForecastOwnerPolicy policy,ForecastScenario scenario)
        :owner_(std::move(helper),std::move(file),std::move(binding),policy,[time=scenario.reportUtcMs]{return ForecastTime{time,10000};}),scenario_(std::move(scenario)){}
    bool start(uint64_t generation)override{return owner_.start(generation)&&owner_.submit(generation,scenario_)==ForecastAdmission::Queued;}
    void requestStop()override{owner_.requestStop();}
    void stop()override{owner_.stop();}
    std::shared_ptr<const ForecastOwnerSnapshot> snapshot()const override{return owner_.snapshot();}
};
}
int main(int argc,char **argv){
    if(argc>1&&std::string(argv[1])=="--fixture-child")return child(argc,argv);
    if(argc!=5)return 2;
    const auto writer=std::filesystem::absolute(std::filesystem::u8path(argv[1])),reader=std::filesystem::absolute(std::filesystem::u8path(argv[2])),root=std::filesystem::absolute(std::filesystem::u8path(argv[3]));
    const std::string mode=argv[4];if(mode!="complete"&&mode!="no-market"&&mode!="no-energy"&&mode!="hang"&&mode!="parent-probe"&&mode!="read-probe")return 2;
#if defined(_WIN32)
    const bool tty=_isatty(_fileno(stdout))&&_isatty(_fileno(stdin));
#else
    const bool tty=isatty(STDOUT_FILENO)&&isatty(STDIN_FILENO);
#endif
    if(!tty&&mode!="parent-probe"&&mode!="read-probe")return 2;
    std::filesystem::create_directories(root);const auto source=root/((mode=="hang"||mode=="parent-probe"||mode=="read-probe")?"fixture-query-hang.db":"source.db");
    CreditSourceContract binding;ForecastScenario scenario;
    {
        income_fixture::Fixture seed(root/"synthetic-model-seed.db");binding=seed.input.binding;binding.cursor=CreditCursorContract{"fixture.exclusive-acyclic-v1","fixture:initial"};
        scenario.clock=seed.input.clock;scenario.reportUtcMs=seed.now;scenario.modelValidUntilUtcMs=seed.now+income_fixture::day;scenario.qualification=ForecastQualification::SyntheticFixture;
        scenario.modelKey=seed.calibration.modelKey;scenario.profileKey=seed.calibration.profileKey;scenario.earningAlignmentContract=seed.calibration.earningAlignmentContract;
        scenario.calibration={{seed.begin-income_fixture::day,seed.begin,income_fixture::work(binding,seed.begin-income_fixture::day,seed.begin)}};
        scenario.remaining=seed.input.remaining;scenario.remaining.work.binding=binding;scenario.tokenQuote=seed.input.tokenQuote;scenario.fxQuote=seed.input.fxQuote;scenario.electricity=seed.input.electricity;scenario.electricity.attributedProduction=binding;
        CreditBatch b;b.binding=binding;b.sequence=1;b.snapshotId="synthetic-snapshot-1";b.sourceAsOfMs=b.observedAtMs=seed.now;b.coverage={{binding.historyStartMs-1,seed.now}};b.cursor=CreditCursorStep{binding.cursor->initial,"fixture:page-1"};
        b.records={credit_fixture::record("cutoff",1,seed.now-income_fixture::day,"100"),credit_fixture::record("prior-day",1,seed.begin-1,"20"),credit_fixture::record("midnight",1,seed.begin,"1"),credit_fixture::record("current",1,seed.now,"2")};
        CreditProcessRequest r;r.generation=r.ticket=1;r.file=source;r.binding=binding;r.nowUtcMs=seed.now;r.recovery=CreditRecoveryMode::Submit;r.batch=b;r.queries={{true,0,seed.now}};
        if(!std::filesystem::exists(source)){CreditProcessClient importer(writer);std::atomic<bool> cancel{false};const auto receipt=importer.transact(r,cancel,5000);if(!receipt.reply||receipt.reply->applied!=CreditLedgerStatus::Ok)return 5;}
    }
    std::filesystem::remove(root/"synthetic-model-seed.db");
    ForecastOwnerPolicy policy;policy.enabled=true;policy.helperTimeoutMs=mode=="parent-probe"?10000:3000;policy.readBudgetMs=15000;
    if(mode=="parent-probe"||mode=="read-probe"){
        FixtureSink sink(reader,source,binding,policy,scenario);if(!sink.start(1))return 6;
        if(mode=="read-probe"){
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
            while(!sink.snapshot()->completedTicket&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            const auto state=sink.snapshot();if(!state->result||!state->result->todayTokens)return 9;
            std::puts("synthetic committed read ready");return 0;
        }
        std::puts("synthetic forecast parent ready");std::fflush(stdout);
        for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if(!platformInit())return 7;
    StatsServer workerProbe,apiProbe;std::string error;
    if(!workerProbe.start("127.0.0.1",0,[]{return "{}";},"{}",&error)||!apiProbe.start("127.0.0.1",0,[]{return "{}";},"{}",&error))return 8;
    SupervisorOptions options;options.exe=selfExecutablePath();options.userArgs={"--fixture-child"};options.workerPortBase=workerProbe.port();options.apiPort=apiProbe.port();workerProbe.stop();apiProbe.stop();
    options.apiEnabled=true;options.dashboard=true;options.drainMs=500;options.version="SYNTHETIC FORECAST OWNER";options.poolEndpoint="fixture.example:1";options.worker="fixture";
    options.stop=&stopping;std::signal(SIGINT,signalStop);std::signal(SIGTERM,signalStop);options.noMarket=mode=="no-market";options.noEnergy=mode=="no-energy";
    WorkerSeed gpu;gpu.index=0;gpu.uuid="GPU-aaaaaaaa-0000-0000-0000-000000000001";gpu.name="SYNTHETIC GPU";options.seeds={gpu};
    options.forecastFactory=[=](bool noMarket,bool noEnergy)->std::unique_ptr<ForecastSink>{auto settings=policy;settings.suppressMarket=noMarket;settings.suppressEnergyCosts=noEnergy;return std::make_unique<FixtureSink>(reader,source,binding,settings,scenario);};
    std::ofstream(root/"fixture-api-port")<<options.apiPort;
    Supervisor supervisor(std::move(options));const auto result=supervisor.run();platformShutdown();return result;
}
