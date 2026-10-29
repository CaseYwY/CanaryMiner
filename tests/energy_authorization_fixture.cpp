// Test-only selected UUIDs and clock. No GPU discovery, mining, sensor calls or production-path selection.
#include "product/settings.h"
#include <csignal>
using namespace om;
static volatile sig_atomic_t stopped=0;
static void stop(int){stopped=1;}
int main(int argc,char **argv){
    if(argc!=3)return 2;
#if defined(_WIN32)
    if(!_isatty(_fileno(stdin))||!_isatty(_fileno(stdout)))return 2;
#else
    if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO))return 2;
#endif
    const auto root=std::filesystem::absolute(argv[2]);std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    EnergyAuthorizationOwner owner(std::filesystem::absolute(argv[1]),root/"energy-persistence.json",false,5000,[]{return 1800000100000LL;});
    if(!owner.start(1))return 3;
    EnergyAuthorizationAccess access;access.snapshot=[&]{return owner.snapshot();};access.submit=[&](uint64_t g,uint64_t r,const EnergyAuthorizationCommand &c){return owner.submit(g,r,c);};
    access.acknowledge=[&](uint64_t g,uint64_t r){return owner.acknowledgeInspection(g,r);};
    access.selectedDevices={"GPU-aaaaaaaa-0000-0000-0000-000000000001","GPU-bbbbbbbb-0000-0000-0000-000000000002"};
    const auto result=runSettingsCommand(root.u8string(),&stopped,false,false,{},{},access);owner.stop();return result;
}
