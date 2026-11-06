// Ordinary child behaviour using only this CPU executable. No miner, device, external endpoint or user file.
#include "product/legacy_child_process.h"
#include "product/session_intent.h"
#include "product_test_util.h"
#include <cstdio>
#include <stdexcept>
#if defined(_WIN32)
#include <shellapi.h>
#include <fcntl.h>
#include <io.h>
#endif
using namespace om;
namespace {
std::filesystem::path self(){
#if defined(_WIN32)
    wchar_t path[32768];const auto count=GetModuleFileNameW(nullptr,path,32768);
    return count&&count<32768?std::filesystem::path(std::wstring(path,count)):std::filesystem::path();
#else
    char path[4096];const auto count=readlink("/proc/self/exe",path,sizeof(path));
    return count>0&&static_cast<size_t>(count)<sizeof(path)?std::filesystem::path(std::string(path,static_cast<size_t>(count))):std::filesystem::path();
#endif
}
std::string hex(const std::string &text){
    constexpr char digits[]="0123456789abcdef";std::string result=":";
    for(const unsigned char c:text){result+=digits[c>>4];result+=digits[c&15];}return result;
}
int peer(int argc,char **argv){
#if defined(_WIN32)
    _setmode(_fileno(stdout),_O_BINARY);_setmode(_fileno(stderr),_O_BINARY);
#endif
    const std::string mode=argv[2];
    if(mode=="args"){
#if defined(_WIN32)
        int count=0;auto *args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return 90;
        for(int i=3;i<count;++i)std::puts(hex(std::filesystem::path(args[i]).u8string()).c_str());LocalFree(args);
#else
        for(int i=3;i<argc;++i)std::puts(hex(argv[i]).c_str());
#endif
        return 0;
    }
    if(mode=="group"){
#if !defined(_WIN32)
        return argc==4&&std::to_string(getpgrp())==argv[3]?0:91;
#else
        return 0; // actual console flags are separately checked by native interposition, not a physical console claim
#endif
    }
    if(mode=="fd"){
#if !defined(_WIN32)
        return argc==4&&fcntl(std::atoi(argv[3]),F_GETFD)<0&&errno==EBADF?0:92;
#else
        return 0;
#endif
    }
    if(mode=="long"){std::printf("%s",std::string(9000,'x').c_str());return 0;}
    std::puts(mode=="delayed"?"READY":mode=="throw"?"THROW":"hello\r\n\nsecond");std::fflush(stdout);
    while(std::getchar()!=EOF){}
    if(mode=="output"){std::fputs("stderr\n",stderr);std::fputs("tail",stdout);return 3;}
    if(mode=="delayed"){std::puts("AFTER");std::fflush(stdout);}
    return 0;
}
struct Output {
    std::mutex mutex;std::vector<std::string> lines;
    void add(const std::string &line){std::lock_guard<std::mutex> lock(mutex);lines.push_back(line);}
    bool has(const std::string &line){std::lock_guard<std::mutex> lock(mutex);return std::find(lines.begin(),lines.end(),line)!=lines.end();}
};
void ordinary(){
    std::string error;int code=-1;const auto exe=self().u8string();
    {Output out;ChildProcess child;CHECK(child.start(exe,{"--ordinary-child","output"},[&](const std::string &line){out.add(line);},&error));
        child.closeStdin();CHECK(child.waitFor(5000,&code)&&code==3);child.joinReader();
        CHECK(out.has("hello")&&out.has("second")&&out.has("stderr")&&out.has("tail"));
        CHECK(child.started()&&child.finished()&&!child.start(exe,{},[](const std::string &){},&error));}
    {Output out;const std::vector<std::string> values={"","plain","a b","\"","\\","end\\\\","back\\\"quote",u8"路徑",u8"العربية"};
        auto args=std::vector<std::string>{"--ordinary-child","args"};args.insert(args.end(),values.begin(),values.end());
        ChildProcess child;CHECK(child.start(exe,args,[&](const std::string &line){out.add(line);},&error));
        CHECK(child.waitFor(5000,&code)&&code==0);child.joinReader();
        CHECK(out.lines.size()==values.size());for(const auto &value:values)CHECK(out.has(hex(value)));}
    {Output out;ChildProcess child;CHECK(child.start(exe,{"--ordinary-child","long"},[&](const std::string &line){out.add(line);},&error));
        CHECK(child.waitFor(5000,&code)&&code==0);child.joinReader();CHECK(!out.lines.empty());
        for(const auto &line:out.lines)CHECK(!line.empty()&&line.size()<=8192);}
#if !defined(_WIN32)
    {ChildProcess child;CHECK(child.start(exe,{"--ordinary-child","group",std::to_string(getpgrp())},[](const std::string &){},&error));CHECK(child.waitFor(5000,&code)&&code==0);}
    {int descriptors[2];CHECK(pipe(descriptors)==0);ChildProcess child;
        CHECK(child.start(exe,{"--ordinary-child","fd",std::to_string(descriptors[1])},[](const std::string &){},&error));
        CHECK(child.waitFor(5000,&code)&&code==0);close(descriptors[0]);close(descriptors[1]);}
#endif
    const auto scratch=std::filesystem::path(makeScratchDir("ordinary-child"));const auto directory=scratch/u8"字元 路徑";
    std::filesystem::create_directory(directory);const auto copy=directory/self().filename();std::filesystem::copy_file(self(),copy);
    {ChildProcess child;CHECK(child.start(copy.u8string(),{"--ordinary-child","args","space value"},[](const std::string &){},&error));CHECK(child.waitFor(5000,&code)&&code==0);}
    std::filesystem::remove_all(scratch);
    {ChildProcess child;CHECK(!child.start("relative-path",{},[](const std::string &){},&error)&&!child.started());}
    {ChildProcess child;CHECK(!child.start(exe,{std::string("a\0b",3)},[](const std::string &){},&error)&&!child.started());}
    {ChildProcess child;CHECK(!child.start(exe,{std::string(4097,'a')},[](const std::string &){},&error)&&!child.started());}
    {ChildProcess child;CHECK(!child.start(exe,std::vector<std::string>(257,"x"),[](const std::string &){},&error)&&!child.started());}
    {ChildProcess child;CHECK(!child.start(exe,std::vector<std::string>(6,std::string(4096,'x')),[](const std::string &){},&error)&&!child.started());}
    {ChildProcess child;CHECK(!child.start(exe,{},[](const std::string &){},&error,[]{return true;})&&!child.started());}
    CHECK(!nativeProcessUnresolved());
}
void foreign(){
    Output out;std::string error;int code=-1;SessionIntent intent;const auto token=intent.explicitStart();ChildProcess child;
    CHECK(child.start(self().u8string(),{"--ordinary-child","delayed"},[&](const std::string &line){out.add(line);},&error));
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!out.has("READY")&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(out.has("READY"));NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();other.settled();
    CHECK(!intent.permits(token));child.closeStdin();CHECK(child.waitFor(5000,&code)&&child.finished());child.joinReader();
    CHECK(!out.has("AFTER")&&!nativeProcessUnresolved());
}
void callbackFault(){
    std::string error;int code=-1;std::atomic<bool> called{false};SessionIntent intent;const auto token=intent.explicitStart();ChildProcess child;
    CHECK(child.start(self().u8string(),{"--ordinary-child","throw"},[&](const std::string &){called=true;throw std::runtime_error("synthetic sink refusal");},&error));
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!called.load()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(called.load());
    while(intent.permits(token)&&std::chrono::steady_clock::now()<end){child.exited(&code);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    CHECK(!intent.permits(token)&&(nativeProcessUnresolved()||child.finished()));
    child.closeStdin();CHECK(child.waitFor(5000,&code)&&child.finished());child.joinReader();CHECK(!nativeProcessUnresolved()&&!intent.permits(token));
    ChildProcess next;CHECK(next.start(self().u8string(),{"--ordinary-child","args"},[](const std::string &){},&error));CHECK(next.waitFor(5000,&code)&&code==0);
}
}
int main(int argc,char **argv){
    if(argc>=3&&std::string(argv[1])=="--ordinary-child")return peer(argc,argv);
#if !defined(_WIN32) && !defined(OM_LEGACY_PIDFD_SPAWN)
    std::puts("SKIP: ordinary native child requires glibc >= 2.40");return 77;
#endif
    ordinary();foreign();callbackFault();return finishTests("ordinary child console/pipes/Unicode/exception/lifetime (CPU only)");
}
