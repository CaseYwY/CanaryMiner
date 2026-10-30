#include "game_detection.h"
#include "hash.h"
#include <algorithm>
#include <chrono>
namespace om {
namespace {
constexpr size_t wireLimit=2048;
constexpr uint64_t lastUtc=253402300799999ULL;
bool hex(const std::string &s,size_t n){return s.size()==n&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
bool name(const std::string &s){return !s.empty()&&s.size()<=128&&s!="."&&s!=".."&&std::all_of(s.begin(),s.end(),[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';});}
bool time(GameGuardTime t){return t.utcMs>0&&static_cast<uint64_t>(t.utcMs)<=lastUtc;}
bool ordered(GameGuardTime a,GameGuardTime b){return a.utcMs<=b.utcMs&&a.monotonicMs<=b.monotonicMs;}
struct Reader {
    const std::string &s;size_t at=0;
    bool line(std::string &out,size_t maximum=128){const auto end=s.find('\n',at);if(end==std::string::npos||end-at>maximum)return false;out=s.substr(at,end-at);at=end+1;return true;}
    bool number(uint64_t &n,uint64_t maximum=UINT64_MAX){std::string v;if(!line(v,20)||v.empty()||(v.size()>1&&v[0]=='0'))return false;n=0;
        for(char c:v){if(c<'0'||c>'9'||static_cast<unsigned>(c-'0')>maximum||n>(maximum-static_cast<unsigned>(c-'0'))/10)return false;n=n*10+static_cast<unsigned>(c-'0');}return true;}
    bool clock(GameGuardTime &t){uint64_t utc=0;if(!number(utc,lastUtc)||!number(t.monotonicMs))return false;t.utcMs=static_cast<int64_t>(utc);return true;}
};
void addTime(std::string &s,GameGuardTime t){s+=std::to_string(t.utcMs)+'\n'+std::to_string(t.monotonicMs)+'\n';}
bool replyValid(const GameProbeRequest &r,const GameProbeReply &p){
    const bool noScope=p.privateScope.empty()&&r.privateScope.empty()&&(p.status==GameProbeStatus::Disabled||p.status==GameProbeStatus::Entropy);
    if(!validGameProbeRequest(r)||(!noScope&&!hex(p.privateScope,32))||(!r.privateScope.empty()&&p.privateScope!=r.privateScope)||
        !time(p.began)||!time(p.finished)||!ordered(r.issued,p.began)||!ordered(p.began,p.finished)||p.matches>64)return false;
    const auto status=static_cast<unsigned>(p.status);if(status>static_cast<unsigned>(GameProbeStatus::Entropy))return false;
    if(p.status!=GameProbeStatus::Ok)return p.matches==0&&p.instanceSet.empty();
    if(!r.enabled||p.finished.monotonicMs-p.began.monotonicMs>r.scanBudgetMs||static_cast<uint64_t>(p.finished.utcMs-p.began.utcMs)>r.scanBudgetMs+50)return false;
    return p.matches?hex(p.instanceSet,64):p.instanceSet.empty();
}
std::string binding(const GameProbeRequest &r){std::string s;return encodeGameProbeRequest(r,1,&s)?sha256Hex(s):std::string();}
}
GameGuardTime gameDetectionTime(){return {std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())};}
bool validGameProcessTarget(const GameProcessTarget &t){return name(t.gameKey)&&name(t.executable)&&
    (t.adapter==GameProcessAdapter::WindowsSessionImage||t.adapter==GameProcessAdapter::LinuxUidNativeImage);}
bool validGameProbeRequest(const GameProbeRequest &r){return r.generation&&r.revision&&validGameProcessTarget(r.target)&&time(r.issued)&&
    (r.privateScope.empty()||hex(r.privateScope,32))&&r.scanBudgetMs>=25&&r.scanBudgetMs<=1000&&r.maxProcesses>=1&&r.maxProcesses<=32768;}
bool encodeGameProbeRequest(const GameProbeRequest &r,uint64_t pid,std::string *out){
    if(!out||!pid||pid>UINT32_MAX||!validGameProbeRequest(r))return false;
    std::string s="CMGDQ1\n"+std::to_string(pid)+'\n'+std::to_string(r.enabled)+'\n'+std::to_string(r.generation)+'\n'+std::to_string(r.revision)+'\n'+
        std::to_string(static_cast<unsigned>(r.target.adapter))+'\n'+r.target.gameKey+'\n'+r.target.executable+'\n'+r.privateScope+'\n';
    addTime(s,r.issued);s+=std::to_string(r.scanBudgetMs)+'\n'+std::to_string(r.maxProcesses)+'\n';*out=std::move(s);return true;
}
bool decodeGameProbeRequest(const std::string &s,GameProbeRequest *out,uint64_t *pid){
    if(!out||!pid||s.size()>wireLimit)return false;
    Reader v{s};GameProbeRequest r;std::string magic;uint64_t p=0,enabled=0,adapter=0,budget=0,maximum=0;
    if(!v.line(magic)||magic!="CMGDQ1"||!v.number(p,UINT32_MAX)||!p||!v.number(enabled,1)||!v.number(r.generation)||!v.number(r.revision)||!v.number(adapter,1)||
        !v.line(r.target.gameKey)||!v.line(r.target.executable)||!v.line(r.privateScope)||!v.clock(r.issued)||!v.number(budget,1000)||!v.number(maximum,32768)||v.at!=s.size())return false;
    r.enabled=enabled!=0;r.target.adapter=static_cast<GameProcessAdapter>(adapter);r.scanBudgetMs=static_cast<unsigned>(budget);r.maxProcesses=static_cast<unsigned>(maximum);
    if(!validGameProbeRequest(r))return false;
    *pid=p;*out=std::move(r);return true;
}
bool encodeGameProbeReply(const GameProbeRequest &r,const GameProbeReply &p,std::string *out){
    if(!out||!replyValid(r,p))return false;
    std::string s="CMGDA1\n"+binding(r)+'\n'+std::to_string(static_cast<unsigned>(p.status))+'\n'+p.privateScope+'\n';addTime(s,p.began);addTime(s,p.finished);
    s+=std::to_string(p.matches)+'\n'+p.instanceSet+'\n';*out=std::move(s);return true;
}
bool decodeGameProbeReply(const GameProbeRequest &r,const std::string &s,GameProbeReply *out){
    if(!out||s.size()>wireLimit||!validGameProbeRequest(r))return false;
    Reader v{s};std::string magic,digest;GameProbeReply p;uint64_t status=0,count=0;
    if(!v.line(magic)||magic!="CMGDA1"||!v.line(digest)||digest!=binding(r)||!v.number(status,static_cast<unsigned>(GameProbeStatus::Entropy))||!v.line(p.privateScope)||
        !v.clock(p.began)||!v.clock(p.finished)||!v.number(count,64)||!v.line(p.instanceSet)||v.at!=s.size())return false;
    p.status=static_cast<GameProbeStatus>(status);p.matches=static_cast<unsigned>(count);if(!replyValid(r,p))return false;*out=std::move(p);return true;
}
GameDetection gameProbeDetection(const GameProbeRequest &r,const GameProbeReply &p){
    GameDetection d;d.gameKey=r.target.gameKey;d.generation=r.generation;d.revision=r.revision;d.observed=r.issued;
    if(!replyValid(r,p))return d;
    d.observed=p.began;
    if(p.status==GameProbeStatus::Ok){d.state=p.matches?GamePresence::Present:GamePresence::Absent;if(p.matches)d.episodeKey="process."+p.instanceSet;}
    return d;
}
GameProbeResult GameProbeClient::transact(const GameProbeRequest &r,const std::atomic<bool> &cancel,unsigned timeout){
    GameProbeResult result;if(cancel){result.transport=LocalTaskStatus::Cancelled;return result;}
    if(timeout<50||timeout>2000||timeout<r.scanBudgetMs){result.transport=LocalTaskStatus::Invalid;return result;}
    const auto started=gameDetectionTime();std::string wire;
    if(!encodeGameProbeRequest(r,localTaskProcessId(),&wire)||!ordered(r.issued,started)){result.transport=LocalTaskStatus::Invalid;return result;}
    if(!r.enabled){GameProbeReply p;p.status=GameProbeStatus::Disabled;p.privateScope=r.privateScope;p.began=p.finished=started;result.transport=LocalTaskStatus::Ok;result.reply=std::move(p);return result;}
    const auto task=process_.run(wire,cancel,timeout);result.transport=task.status;
    if(task.status==LocalTaskStatus::Ok){GameProbeReply p;const bool decoded=decodeGameProbeReply(r,task.response,&p);const auto now=gameDetectionTime();
        if(cancel)result.transport=LocalTaskStatus::Cancelled;
        else if(!ordered(started,now)||!ordered(r.issued,now))result.transport=LocalTaskStatus::Unavailable;
        else if(now.monotonicMs-started.monotonicMs>=timeout)result.transport=LocalTaskStatus::Timeout;
        else if(!decoded||!ordered(started,p.began)||!ordered(p.finished,now)||now.monotonicMs-p.began.monotonicMs>2000||now.utcMs-p.began.utcMs>2000)result.transport=LocalTaskStatus::Unavailable;
        else result.reply=std::move(p);
    }
    return result;
}
} // namespace om
