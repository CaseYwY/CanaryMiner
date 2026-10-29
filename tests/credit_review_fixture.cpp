// Test-only native settings harness; fixed synthetic binding/filename, no account manifest/import or production entry.
#include "product/credit_recovery.h"
#include "product/credit_owner.h"
#include "product/settings.h"
#include "credit_fixture.h"
#include <csignal>
using namespace om;
using namespace credit_fixture;
static volatile sig_atomic_t stopped=0;
static void stop(int){stopped=1;}
int main(int argc,char **argv){
    if(argc<3)return 2;
    const std::string mode=argv[1];const auto root=std::filesystem::absolute(argv[argc-1]);const auto file=root/"fixture-credit-review.db";
    const bool cursored=mode=="--prepare-cursor"||mode=="--facts-cursor"||mode=="--settings-cursor";
    auto binding=cursored?cursorContract():contract();if(cursored)binding.cursor->initial=std::string(508,'i')+"INIT";
    if(argc==3&&(mode=="--prepare"||mode=="--prepare-cursor")){
        if(std::filesystem::exists(file))return 3;
        std::filesystem::create_directories(root);CreditProcessRequest r;r.generation=r.ticket=1;r.file=file;r.binding=binding;r.nowUtcMs=T;r.queries={{true,0,T}};
        if(executeCreditRequest(r).opened!=CreditLedgerStatus::Ok)return 4;
        r.recovery=CreditRecoveryMode::Submit;r.batch=batch(1,T,{record("fixture.private-original",1,T,"2.5")});
        if(cursored){r.batch->binding=binding;r.batch->cursor=CreditCursorStep{binding.cursor->initial,std::string(508,'x')+"TAIL"};}
        CreditRecoveryJournal j;if(j.open(r)!=CreditLedgerStatus::Ok||j.prepare(r)!=CreditLedgerStatus::Ok)return 5;
        puts("synthetic prepared; no income or mining");return 0;
    }
    if(argc==3&&(mode=="--facts"||mode=="--facts-cursor")){
        CreditProcessRequest r;r.generation=r.ticket=1;r.file=file;r.binding=binding;r.nowUtcMs=T+1000;r.recovery=CreditRecoveryMode::Inspect;r.queries={{true,0,T+1000}};
        const auto result=executeCreditRequest(r);if(result.opened!=CreditLedgerStatus::Ok||!result.recovery||result.totals.size()!=1)return 6;
        printf("%s %llu %s %lld",result.recovery->state==CreditRecoveryState::Prepared?"Prepared":result.recovery->state==CreditRecoveryState::Confirmed?"Confirmed":"Other",
            static_cast<unsigned long long>(result.totals[0].matchedRecords),result.totals[0].knownPostedNet.text().c_str(),static_cast<long long>(result.totals[0].observedAtMs));
        if(cursored){if(!result.cursor||result.cursor->status!=CreditLedgerStatus::Ok)return 9;printf(" cursor=%llu",static_cast<unsigned long long>(result.cursor->sequence));}
        puts("");return 0;
    }
    if(argc!=4||(mode!="--settings"&&mode!="--settings-cursor"))return 2;
#if defined(_WIN32)
    if(!_isatty(_fileno(stdin))||!_isatty(_fileno(stdout)))return 2;
#else
    if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO))return 2;
#endif
    if(!std::filesystem::exists(file))return 7;
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    CreditLedgerOwner owner(std::filesystem::absolute(argv[2]),file,binding,{},[]{return T+1000;});
    if(!owner.start(1))return 8;
    CreditReviewAccess access;access.snapshot=[&]{return owner.snapshot();};access.inspect=[&](uint64_t g){return owner.refresh(g);};access.retry=[&](uint64_t g,uint64_t revision){return owner.retryReviewed(g,revision);};
    const auto code=runSettingsCommand(root.u8string(),&stopped,false,false,access);owner.stop();return code;
}
