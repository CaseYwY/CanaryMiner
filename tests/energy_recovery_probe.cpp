// Separate parent invocations exercise disk-only reconstruction; synthetic paths/identities only.
#include "energy_history_fixture.h"
#include "product/energy_history_owner.h"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc,char **argv){
    using namespace om;using namespace energy_history_fixture;
    if(argc!=4)return 2;
    const auto helper=std::filesystem::absolute(argv[1]),file=std::filesystem::absolute(argv[2]);const std::string action=argv[3];
    if(action=="owner"||action=="retry"){
        EnergyHistoryOwner owner(helper,file,binding(),{},3000,[]{return T+20000;});if(!owner.start(1))return 3;
        const auto wait=[&]{const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){const auto s=owner.snapshot()->state;
            if(s==EnergyHistoryOwnerState::Ready||s==EnergyHistoryOwnerState::NeedsReview||s==EnergyHistoryOwnerState::Faulted)return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));}return false;};
        if(!wait())return 4;
        if(action=="retry"&&(owner.retryReviewed(1,owner.snapshot()->revision)!=EnergyHistoryAdmission::Queued||!wait()))return 5;
        const auto s=owner.snapshot();std::cout<<"owner="<<static_cast<unsigned>(s->state)<<" recovery="<<static_cast<unsigned>(s->recoveryState)<<" pending="<<static_cast<bool>(s->uncertainBatch);
        if(s->reply)std::cout<<" sequence="<<s->reply->checkpoint.sequence;
        std::cout<<'\n';owner.stop();return 0;
    }
    auto r=request(file);r.queries.clear();r.recovery=EnergyRecoveryMode::Inspect;
    if(action=="init")r.mode=EnergyHistoryOpen::CreateNew;
    else if(action=="prepared"||action=="committed"||action=="acklost"){
        r.batch=first();r.batch->id=action=="prepared"?"fixture.exit-prepared":action=="committed"?"fixture.exit-committed":"fixture.bad-ack";r.recovery=EnergyRecoveryMode::Submit;
    }else if(action!="inspect")return 2;
    EnergyHistoryProcessClient client(helper);std::atomic<bool> cancel{false};const auto result=client.transact(r,cancel,3000);
    std::cout<<"unknown="<<result.writeOutcomeUnknown;
    if(result.reply){const auto &a=*result.reply;std::cout<<" opened="<<static_cast<unsigned>(a.opened)<<" sequence="<<a.checkpoint.sequence;
        if(a.recovery)std::cout<<" recovery="<<static_cast<unsigned>(a.recovery->state);}
    std::cout<<'\n';return 0;
}
