#include "game_detection.h"
#include "hash.h"
#include "private_entropy.h"
#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__linux__)
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#endif
namespace om {
namespace {
using S=GameProbeStatus;
struct Scan {
    const GameProbeRequest &request;GameProbeReply &reply;
    unsigned examined=0;std::set<std::pair<uint64_t,uint64_t>> identities;
    bool budget(){if(gameDetectionTime().monotonicMs-reply.began.monotonicMs>request.scanBudgetMs){reply.status=S::Capacity;return false;}return true;}
    bool row(){if(!budget())return false;if(++examined>request.maxProcesses){reply.status=S::Capacity;return false;}return true;}
    bool match(uint64_t pid,uint64_t created){
        if(!pid||!created||!identities.emplace(pid,created).second){reply.status=S::Incomplete;return false;}
        if(identities.size()>64){reply.status=S::Capacity;return false;}return true;
    }
};
#if defined(_WIN32)
struct Handle {HANDLE value;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
bool equalName(const wchar_t *s,const std::string &target){
    size_t i=0;for(;s[i]&&i<target.size();++i){auto a=s[i],b=static_cast<wchar_t>(target[i]);if(a>=L'A'&&a<=L'Z')a+=L'a'-L'A';if(b>=L'A'&&b<=L'Z')b+=L'a'-L'A';if(a!=b)return false;}return i==target.size()&&!s[i];
}
void inspect(Scan &s){
    if(s.request.target.adapter!=GameProcessAdapter::WindowsSessionImage){s.reply.status=S::Unsupported;return;}
    DWORD session=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&session))return;
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};if(snapshot.value==INVALID_HANDLE_VALUE)return;
    PROCESSENTRY32W row{};row.dwSize=sizeof(row);if(!Process32FirstW(snapshot.value,&row))return;
    do{
        if(!s.row())return;
        if(!equalName(row.szExeFile,s.request.target.executable))continue;
        DWORD targetSession=0;if(!ProcessIdToSessionId(row.th32ProcessID,&targetSession))return;
        if(targetSession!=session)continue;
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,row.th32ProcessID)};if(!process.value)return;
        wchar_t image[32768];DWORD length=32768;
        if(!QueryFullProcessImageNameW(process.value,0,image,&length)||!length||length>=32768)return;
        image[length]=0;const wchar_t *base=image;for(DWORD i=0;i<length;++i)if(image[i]==L'\\'||image[i]==L'/')base=image+i+1;
        FILETIME created{},exit{},kernel{},user{};
        if(!equalName(base,s.request.target.executable)||!GetProcessTimes(process.value,&created,&exit,&kernel,&user)||WaitForSingleObject(process.value,0)!=WAIT_TIMEOUT)return;
        if(!s.match(row.th32ProcessID,(static_cast<uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime))return;
    }while(Process32NextW(snapshot.value,&row));
    if(GetLastError()!=ERROR_NO_MORE_FILES||!s.budget())return;
    s.reply.status=S::Ok;
}
#elif defined(__linux__)
struct Fd {int value=-1;~Fd(){if(value>=0)::close(value);}};
bool readFile(int parent,const char *path,size_t maximum,std::string &out){
    Fd f{openat(parent,path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW)};if(f.value<0)return false;out.clear();
    for(;;){char data[4096];const auto n=::read(f.value,data,sizeof(data));if(n<0){if(errno==EINTR)continue;return false;}if(!n)return true;
        if(static_cast<size_t>(n)>maximum-out.size())return false;
        out.append(data,static_cast<size_t>(n));}
}
bool number(const std::string &s,uint64_t &n){if(s.empty()||s.size()>20)return false;n=0;for(char c:s){if(c<'0'||c>'9'||n>(UINT64_MAX-static_cast<unsigned>(c-'0'))/10)return false;n=n*10+static_cast<unsigned>(c-'0');}return true;}
bool visibleMount(const std::string &text,uint64_t activeMount){
    std::istringstream lines(text);std::string line;bool found=false;
    while(std::getline(lines,line)){std::istringstream words(line);std::vector<std::string> fields;std::string word;while(words>>word)fields.push_back(word);
        uint64_t mount=0;if(fields.empty()||!number(fields[0],mount)||mount!=activeMount)continue;
        if(fields.size()<10||fields[4]!="/proc")return false;
        if(found)return false;
        found=true;const auto dash=std::find(fields.begin()+6,fields.end(),"-");if(dash==fields.end()||fields.end()-dash!=4||*(dash+1)!="proc"||fields[3]!="/")return false;
        // hidepid=4/invisible can hide same-UID non-ptraceable tasks. Unknown options containing hidepid fail closed.
        for(const auto &options:{fields[5],*(dash+3)}){std::istringstream list(options);std::string option;while(std::getline(list,option,','))if(option.rfind("hidepid",0)==0&&option!="hidepid=0"&&option!="hidepid=off")return false;}
    }
    return found;
}
bool visibleProc(){
    Fd root{open("/proc",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW)};struct statfs fs{};if(root.value<0||fstatfs(root.value,&fs)||fs.f_type!=0x9fa0)return false;
    std::string info;if(!readFile(AT_FDCWD,("/proc/self/fdinfo/"+std::to_string(root.value)).c_str(),4096,info))return false;
    uint64_t activeMount=0;std::istringstream lines(info);std::string line;bool found=false;
    while(std::getline(lines,line)){if(line.rfind("mnt_id:",0)!=0)continue;if(found)return false;found=true;std::istringstream words(line.substr(7));std::string value,extra;
        if(!(words>>value)||!number(value,activeMount)||(words>>extra)||!activeMount)return false;}
    std::string text;return found&&readFile(AT_FDCWD,"/proc/self/mountinfo",128*1024,text)&&visibleMount(text,activeMount);
}
bool user(const std::string &status,bool &selected){
    std::istringstream lines(status);std::string line;bool found=false;selected=false;
    while(std::getline(lines,line)){if(line.rfind("Uid:",0)!=0)continue;if(found)return false;found=true;std::istringstream fields(line.substr(4));
        std::string value;for(unsigned i=0;i<4;++i){uint64_t id=0;if(!(fields>>value)||!number(value,id)||id>UINT32_MAX)return false;if(id==geteuid())selected=true;}if(fields>>value)return false;}
    return found;
}
bool identity(const std::string &stat,uint64_t expected,uint64_t &created,bool &active){
    const auto open=stat.find(" ("),end=stat.rfind(") ");uint64_t pid=0;if(open==std::string::npos||end==std::string::npos||end<open||!number(stat.substr(0,open),pid)||pid!=expected)return false;
    std::istringstream words(stat.substr(end+2));std::string value;uint64_t flags=0;char state=0;
    for(unsigned field=3;field<=22;++field){if(!(words>>value))return false;if(field==3){if(value.size()!=1)return false;state=value[0];}
        if(field==9&&!number(value,flags))return false;
        if(field==22&&!number(value,created))return false;}
    active=state!='Z'&&state!='X'&&state!='x'&&!(flags&0x00200000ULL); // PF_KTHREAD: no userspace image
    return created!=0;
}
bool image(int dir,std::string &name){char buf[4097];const auto n=readlinkat(dir,"exe",buf,sizeof(buf));if(n<=0||static_cast<size_t>(n)>=sizeof(buf))return false;
    std::string path(buf,static_cast<size_t>(n));const std::string deleted=" (deleted)";if(path.size()>deleted.size()&&path.compare(path.size()-deleted.size(),deleted.size(),deleted)==0)path.resize(path.size()-deleted.size());
    const auto slash=path.rfind('/');if(slash==std::string::npos)return false;name=path.substr(slash+1);return !name.empty();
}
void inspect(Scan &s){
    if(s.request.target.adapter!=GameProcessAdapter::LinuxUidNativeImage||!visibleProc()){s.reply.status=S::Unsupported;return;}
    DIR *raw=opendir("/proc");if(!raw)return;struct Close {DIR *dir;~Close(){closedir(dir);}} close{raw};
    for(;;){errno=0;const auto *entry=readdir(raw);if(!entry){if(errno==0&&s.budget())s.reply.status=S::Ok;return;}
        uint64_t pid=0;if(!number(entry->d_name,pid)||!pid)continue;if(!s.row())return;
        Fd dir{openat(dirfd(raw),entry->d_name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW)};if(dir.value<0)return;
        std::string status;bool selected=false;if(!readFile(dir.value,"status",32768,status)||!user(status,selected))return;if(!selected)continue;
        uint64_t first=0,last=0;bool active=false;std::string stat,name,again;
        if(!readFile(dir.value,"stat",4096,stat)||!identity(stat,pid,first,active))return;
        if(!active)continue;
        if(!image(dir.value,name))return;
        // Recheck creation, credentials and executable for EVERY selected task; a racing exec cannot prove absence.
        if(!readFile(dir.value,"stat",4096,stat)||!identity(stat,pid,last,active)||!active||first!=last||!image(dir.value,again)||name!=again||
            !readFile(dir.value,"status",32768,status)||!user(status,selected)||!selected)return;
        if(name==s.request.target.executable&&!s.match(pid,first))return;
    }
}
#else
void inspect(Scan &s){s.reply.status=S::Unsupported;}
#endif
}
#if defined(CANARY_GAME_DETECTION_TESTING)
#if defined(__linux__)
bool gameDetectionTestIdentity(const std::string &s,uint64_t pid,uint64_t &created,bool &active){return identity(s,pid,created,active);}
bool gameDetectionTestUser(const std::string &s,bool &selected){return user(s,selected);}
bool gameDetectionTestMount(const std::string &s,uint64_t id){return visibleMount(s,id);}
#elif defined(_WIN32)
bool gameDetectionTestName(const std::wstring &s,const std::string &target){return equalName(s.c_str(),target);}
#endif
#endif
GameProbeReply executeGameProbe(const GameProbeRequest &r){
    GameProbeReply p;p.began=p.finished=gameDetectionTime();p.privateScope=r.privateScope;
    if(!validGameProbeRequest(r)){p.status=S::Invalid;return p;}
    if(!r.enabled){p.status=S::Disabled;return p;} // no entropy or native process query while disabled
    try{
        if(p.privateScope.empty()&&!privateEnergyKey(&p.privateScope)){p.status=S::Entropy;return p;}
        Scan s{r,p,0,{}};inspect(s);p.finished=gameDetectionTime();
        if(p.began.utcMs<r.issued.utcMs||p.began.monotonicMs<r.issued.monotonicMs||p.finished.utcMs<p.began.utcMs||p.finished.monotonicMs<p.began.monotonicMs||
            (p.status==S::Ok&&(p.finished.monotonicMs-p.began.monotonicMs>r.scanBudgetMs||p.finished.utcMs-p.began.utcMs>r.scanBudgetMs+50)))p.status=S::Clock;
        if(p.status==S::Ok&&!s.identities.empty()){
            std::string evidence="CMGDINSTANCE1\n"+p.privateScope+'\n'+std::to_string(r.generation)+'\n'+std::to_string(static_cast<unsigned>(r.target.adapter))+'\n'+r.target.gameKey+'\n'+r.target.executable+'\n';
            for(const auto &id:s.identities)evidence+=std::to_string(id.first)+':'+std::to_string(id.second)+'\n';
            p.matches=static_cast<unsigned>(s.identities.size());p.instanceSet=sha256Hex(evidence);
        }
    }catch(...){p.status=S::Capacity;p.matches=0;p.instanceSet.clear();p.finished=gameDetectionTime();}
    return p;
}
} // namespace om
