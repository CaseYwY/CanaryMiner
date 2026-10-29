#include "energy_runtime.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif
namespace om {
std::filesystem::path energyRuntimeExecutable(){
#if defined(_WIN32)
    wchar_t path[4097]={};const auto n=GetModuleFileNameW(nullptr,path,4097);
    if(!n||n>=4097)return {};
    return std::filesystem::path(std::wstring(path,n));
#elif defined(__linux__)
    char path[4097];const auto n=readlink("/proc/self/exe",path,sizeof(path));
    if(n<=0||static_cast<size_t>(n)>=sizeof(path))return {};
    const std::string text(path,static_cast<size_t>(n));
    if(text.size()>=10&&text.compare(text.size()-10,10," (deleted)")==0)return {};
    return std::filesystem::u8path(text);
#else
    return {};
#endif
}
std::optional<EnergyRuntimePaths> energyRuntimePaths(const std::filesystem::path &executable,const std::filesystem::path &stateDir){
    if(!localTaskPath(executable)||!localTaskPath(stateDir)||executable.filename().empty())return {};
#if defined(_WIN32)
    const auto suffix=".exe";
#else
    const auto suffix="";
#endif
    EnergyRuntimePaths p;
    p.authorizationHelper=executable.parent_path()/(std::string("canary_energy_authorization_worker")+suffix);
    p.collectorHelper=executable.parent_path()/(std::string("canary_energy_collector_worker")+suffix);
    p.authorizationFile=stateDir/"energy-persistence.json";
    if(!localTaskPath(p.authorizationHelper)||!localTaskPath(p.collectorHelper)||!localTaskPath(p.authorizationFile))return {};
    return p;
}
std::unique_ptr<EnergyRuntime> makeNativeEnergyRuntime(const std::string &stateDir,bool mining,bool disabled){
    try{
        if(stateDir.empty()||stateDir.find('\0')!=std::string::npos)return {};
        // Preserve configured path semantics (including symlink/.. traversal); lexical normalization is not canonicalization.
        std::error_code error;const auto dir=std::filesystem::absolute(std::filesystem::u8path(stateDir),error);
        if(error)return {};
        const auto p=energyRuntimePaths(energyRuntimeExecutable(),dir);if(!p)return {};
        return makeEnergyRuntime(p->authorizationHelper,p->collectorHelper,p->authorizationFile,mining,disabled);
    }catch(...){return {};}
}
} // namespace om
