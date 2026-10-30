#include "product/game_detection_owner.h"
#include <iostream>
#include <thread>
using namespace om;
int main(int argc,char **argv){
    if(argc<3)return 2;
    const auto helper=std::filesystem::absolute(argv[1]);const std::string mode=argv[2];
    if(mode=="life"){
        GameDetectionPolicy policy;policy.enabled=true;policy.helperTimeoutMs=2000;
        GameDetectionOwner owner(helper,{"fixture.game","fixture-life",GameProcessAdapter::WindowsSessionImage},policy);if(!owner.start(1))return 3;
        std::this_thread::sleep_for(std::chrono::seconds(20));return 4;
    }
    if(mode!="once"||argc!=5)return 2;
    GameProbeRequest r;r.enabled=true;r.generation=r.revision=1;r.issued=gameDetectionTime();r.scanBudgetMs=1000;r.target={"fixture.game",argv[3],GameProcessAdapter::WindowsSessionImage};
#if defined(__linux__)
    r.target.adapter=GameProcessAdapter::LinuxUidNativeImage;
#endif
    if(std::string(argv[4])!="-")r.privateScope=argv[4];
    GameProbeClient client(helper);std::atomic<bool> cancel{false};const auto result=client.transact(r,cancel,2000);
    if(result.transport!=LocalTaskStatus::Ok||!result.reply)return 5;
    const auto &p=*result.reply;std::cout<<static_cast<unsigned>(p.status)<<' '<<p.matches<<' '<<p.privateScope<<' '<<(p.instanceSet.empty()?"-":p.instanceSet)<<'\n';return 0;
}
