// Test-only synthetic native recovery entry; no device discovery, sensors, mining or production state binding.
#include "energy_capture_fixture.h"
#include "energy_collector_fixture.h"
#include "product/energy_leased_capture.h"
#include "product/energy_recovery.h"
#include "product/settings.h"
#include <csignal>
using namespace om;
using namespace energy_capture_fixture;
static volatile sig_atomic_t stopped=0;
static void stop(int){stopped=1;}
int main(int argc,char **argv){
    if(argc<3)return 2;
    const std::string mode=argv[1];const auto root=std::filesystem::absolute(argv[argc-1]),file=root/"fixture-energy-review.db";
    const auto authorizationFile=root/"fixture-authorization.json";
    if(argc==4&&mode=="--leased-prepare"){
        namespace e=energy_collector_fixture;
        std::filesystem::create_directories(root);EnergyAuthorizationProcessClient c(std::filesystem::absolute(argv[2]));
        const auto d=e::enroll(c,authorizationFile);if(d.status!=EnergyAuthorizationStatus::Ok)return 3;
        const auto issued=e::call(c,e::auth(authorizationFile,EnergyAuthorizationAction::Provision,d.token));if(!issued.sessionIssued)return 3;
        EnergyCollectorLeaseReply s;s.provision=issued;auto r=e::history(authorizationFile,s,true);r.batch->readings[0].milliwatts=123456;r.batch->readings[1].milliwatts.reset();
        EnergyRecoveryJournal journal;EnergyHistoryLedger db;
        if(journal.open(r)!=EnergyHistoryStatus::Ok||db.open(r.file,r.binding,r.nowUtcMs,EnergyHistoryOpen::Existing,r.limits)!=EnergyHistoryStatus::Ok||
            journal.prepare(r,db.checkpoint(r.nowUtcMs))!=EnergyHistoryStatus::Ok)return 4;
        puts("synthetic prepared; no mining or sensor calls");return 0;
    }
    if(argc==3&&mode=="--prepare"){
        std::filesystem::create_directories(root);auto r=request(file);r.mode=EnergyHistoryOpen::CreateNew;
        const auto created=executeEnergyHistoryRequest(r);if(created.opened!=EnergyHistoryStatus::Ok)return 3;
        EnergyHistoryBatch b;b.sequence=1;b.id="energy.1";b.sessionId="fixture.private-original";b.start=EnergyHistorySession{b.sessionId,T,0,5000};b.observedAtMs=T+1000;
        b.readings={{0,T,0,0,123456},{0,T+1000,1000,0,std::nullopt}};r.batch=b;r.mode=EnergyHistoryOpen::Existing;r.recovery=EnergyRecoveryMode::Submit;
        EnergyRecoveryJournal journal;if(journal.open(r)!=EnergyHistoryStatus::Ok||journal.prepare(r,created.checkpoint)!=EnergyHistoryStatus::Ok)return 4;
        puts("synthetic prepared; no mining or sensor calls");return 0;
    }
    if(argc==3&&(mode=="--facts"||mode=="--leased-facts")){
        auto r=request(file);if(mode=="--leased-facts"){
            EnergyAuthorizationDocument d;if(!decodeEnergyAuthorizationDocument(energy_collector_fixture::bytes(authorizationFile),&d))return 5;
            EnergyCollectorLeaseReply s;s.provision.document=d;r=energy_collector_fixture::history(authorizationFile,s);}
        r.queries={{T,T+5000,{},{}}};const auto a=executeEnergyHistoryRequest(r);
        if(a.opened!=EnergyHistoryStatus::Ok||!a.recovery||a.totals.size()!=1)return 5;
        printf("%s %llu %s %llu %lld\n",a.recovery->state==EnergyRecoveryState::Prepared?"Prepared":a.recovery->state==EnergyRecoveryState::Confirmed?"Confirmed":"Other",
            static_cast<unsigned long long>(a.checkpoint.sequence),a.totals[0].knownKwh.numerator.text().c_str(),
            static_cast<unsigned long long>(a.totals[0].coveredDeviceMs),static_cast<long long>(a.checkpoint.observedAtMs));return 0;
    }
    const bool leased=argc==5&&mode=="--leased-settings";
    if(!leased&&(argc!=4||mode!="--settings"))return 2;
#if defined(_WIN32)
    if(!_isatty(_fileno(stdin))||!_isatty(_fileno(stdout)))return 2;
#else
    if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO))return 2;
#endif
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    auto capture=leased?makeLeasedEnergyCapture(std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),authorizationFile,false,5000,[]{return T+10000;}):
        makeEnergyCapture(std::filesystem::absolute(argv[2]),file,plan(),5000,[]{return T+10000;});
    if(!capture->start(1,leased?energy_collector_fixture::devices:devices()))return 6;
    EnergyReviewAccess access;access.snapshot=[&]{capture->tick(T+5000,5000);return capture->snapshot();};
    access.inspect=[&](uint64_t g,uint64_t r){return capture->inspect(g,r);};access.retry=[&](uint64_t g,uint64_t r){return capture->retryReviewed(g,r);};
    const auto result=runSettingsCommand(root.u8string(),&stopped,false,false,{},access);capture->stop();return result;
}
