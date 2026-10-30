// Isolated synthetic replies/stalls only. Never linked into the installed helper or ordinary library.
#include "product/game_detection.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif
using namespace om;
std::filesystem::path self(){
#if defined(_WIN32)
    wchar_t buf[32768];const auto n=GetModuleFileNameW(nullptr,buf,32768);return n&&n<32768?std::filesystem::path(std::wstring(buf,n)):std::filesystem::path();
#else
    char buf[4096];const auto n=readlink("/proc/self/exe",buf,sizeof(buf));return n>0&&static_cast<size_t>(n)<sizeof(buf)?std::filesystem::path(std::string(buf,static_cast<size_t>(n))):std::filesystem::path();
#endif
}
int main(int argc,char **argv){
    if(argc==2&&std::string(argv[1])=="owned-target"){std::puts("ready");std::fflush(stdout);std::this_thread::sleep_for(std::chrono::seconds(60));return 0;}
    std::string wire;GameProbeRequest r;uint64_t pid=0;
    if(!readLocalTaskInput(argc,argv,&wire)||!decodeGameProbeRequest(wire,&r,&pid)||!bindLocalTaskParent(pid))return 2;
    const auto &mode=r.target.executable;
    if(mode=="fixture-hang")std::this_thread::sleep_for(std::chrono::seconds(60));
    if(mode=="fixture-life"){
        std::ofstream marker(self().parent_path()/"fixture-game-ready",std::ios::binary);marker<<localTaskProcessId();marker.close();
        std::this_thread::sleep_for(std::chrono::seconds(60));return 3;
    }
    if(mode=="fixture-malformed")return writeLocalTaskOutput("CMGDA1\ngarbage\n")?0:3;
    GameProbeReply p;p.privateScope=r.privateScope.empty()?std::string(32,'a'):r.privateScope;p.began=p.finished=gameDetectionTime();p.status=GameProbeStatus::Ok;
    if(mode=="fixture-present"){p.matches=1;p.instanceSet=std::string(64,'b');}
    if(mode=="fixture-incomplete")p.status=GameProbeStatus::Incomplete;
    if(mode=="fixture-clock")p.status=GameProbeStatus::Clock;
    if(mode=="fixture-wrong"){++r.revision;}
    if(mode=="fixture-delayed"){if(!encodeGameProbeReply(r,p,&wire))return 4;std::this_thread::sleep_for(std::chrono::milliseconds(150));return writeLocalTaskOutput(wire)?0:5;}
    if(!encodeGameProbeReply(r,p,&wire))return 4;
    return writeLocalTaskOutput(wire)?0:5;
}
