#include "worker_work_stream.h"
#include <charconv>
namespace om {
namespace {
using K=WorkerWorkKind;
constexpr char kinds[]={'S','B','C','P','R','L','E','F'};
bool number(std::string_view s,uint64_t &out){
    if(s.empty()||s.size()>20||(s.size()>1&&s.front()=='0')||s.front()<'0'||s.front()>'9')return false;
    const auto r=std::from_chars(s.data(),s.data()+s.size(),out);return r.ec==std::errc{}&&r.ptr==s.data()+s.size();
}
}
bool sameWorkerWorkBinding(const WorkerControlBinding &a,const WorkerControlBinding &b){
    return a.sessionKey==b.sessionKey&&a.workerKey==b.workerKey&&a.epoch==b.epoch&&a.workerGeneration==b.workerGeneration&&a.slot==b.slot;
}
bool validWorkerWorkEvent(const WorkerWorkEvent &e){
    if(!validWorkerControlBinding(e.binding)||!e.sequence||e.observed.utcMs<0||e.observed.utcMs>253402300799999LL||
        !e.observed.monotonicMs||e.appliedHostWaitMs>1000)return false;
    if(e.kind==K::Completed)return e.lane==WorkLane::User&&e.workUnits&&e.attempts&&e.macsPerUnit&&e.workUnits%e.attempts==0;
    if(e.workUnits||e.attempts||e.macsPerUnit)return false;
    if(e.kind==K::Begin)return (e.lane==WorkLane::User||e.lane==WorkLane::Donation)&&!e.appliedHostWaitMs&&!e.flag;
    if(e.lane!=WorkLane::User)return false;
    if(e.kind==K::Paused||e.kind==K::Runnable||e.kind==K::Leaving)return !e.flag;
    if(e.appliedHostWaitMs)return false;
    if(e.kind==K::End)return true;
    return (e.kind==K::Start||e.kind==K::Finish)&&!e.flag;
}
bool encodeWorkerWorkEvent(const WorkerWorkEvent &e,std::string *out){
    if(!out||!validWorkerWorkEvent(e))return false;
    const auto &b=e.binding;
    const auto wire=std::string("CMWW1 ")+b.sessionKey+" "+std::to_string(b.epoch)+" "+std::to_string(b.slot)+" "+
        std::to_string(b.workerGeneration)+" "+b.workerKey+" "+std::to_string(e.sequence)+" "+kinds[static_cast<unsigned>(e.kind)]+" "+
        std::to_string(e.observed.utcMs)+" "+std::to_string(e.observed.monotonicMs)+(e.lane==WorkLane::User?" U ":" D ")+
        std::to_string(e.workUnits)+" "+std::to_string(e.attempts)+" "+std::to_string(e.macsPerUnit)+" "+
        std::to_string(e.appliedHostWaitMs)+(e.flag?" 1\n":" 0\n");
    if(wire.size()>workerControlFrameLimit)return false;
    *out=wire;return true;
}
bool decodeWorkerWorkEvent(std::string_view wire,WorkerWorkEvent *out){
    if(!out||wire.empty()||wire.size()>workerControlFrameLimit||wire.back()!='\n')return false;
    wire.remove_suffix(1);std::array<std::string_view,16> f;size_t pos=0;
    for(size_t i=0;i<f.size();++i){const auto end=wire.find(' ',pos);
        if((i+1==f.size())!=(end==wire.npos))return false;
        f[i]=wire.substr(pos,end==wire.npos?wire.size()-pos:end-pos);if(f[i].empty())return false;
        pos=end==wire.npos?wire.size():end+1;}
    WorkerWorkEvent e;uint64_t slot=0,utc=0,wait=0,flag=0;
    if(f[0]!="CMWW1"||f[1].size()!=32||f[5].size()!=32||!number(f[2],e.binding.epoch)||!number(f[3],slot)||slot>255||
        !number(f[4],e.binding.workerGeneration)||!number(f[6],e.sequence)||f[7].size()!=1||!number(f[8],utc)||utc>253402300799999ULL||
        !number(f[9],e.observed.monotonicMs)||!number(f[11],e.workUnits)||!number(f[12],e.attempts)||!number(f[13],e.macsPerUnit)||
        !number(f[14],wait)||wait>1000||!number(f[15],flag)||flag>1)return false;
    bool found=false;for(unsigned i=0;i<sizeof(kinds);++i)if(f[7][0]==kinds[i]){e.kind=static_cast<K>(i);found=true;break;}
    if(!found||(f[10]!="U"&&f[10]!="D"))return false;
    e.binding.sessionKey=std::string(f[1]);e.binding.workerKey=std::string(f[5]);e.binding.slot=static_cast<unsigned>(slot);
    e.observed.utcMs=static_cast<int64_t>(utc);e.lane=f[10]=="U"?WorkLane::User:WorkLane::Donation;
    e.appliedHostWaitMs=static_cast<unsigned>(wait);e.flag=flag!=0;
    if(!validWorkerWorkEvent(e))return false;
    *out=std::move(e);return true;
}
WorkerWorkPublisher::WorkerWorkPublisher(WorkerControlBinding b,Publish p):binding_(std::move(b)),publish_(std::move(p)){}
bool WorkerWorkPublisher::emit(WorkerWorkEvent e)noexcept{
    try{if(failed_||finished_||sequence_==UINT64_MAX||!publish_){failed_=true;return false;}
        e.binding=binding_;e.sequence=sequence_+1;
        if(!validWorkerWorkEvent(e)||!publish_(e)){failed_=true;return false;}
        ++sequence_;return true;
    }catch(...){failed_=true;return false;}
}
bool WorkerWorkPublisher::start(CompletedWorkTime t)noexcept{
    if(started_){failed_=true;return false;}WorkerWorkEvent e;e.observed=t;
    if(!emit(e))return false;
    started_=true;return true;
}
bool WorkerWorkPublisher::begin(WorkLane lane,CompletedWorkTime t)noexcept{
    if(!started_||running_){failed_=true;return false;}WorkerWorkEvent e;e.kind=K::Begin;e.observed=t;e.lane=lane;
    if(!emit(e))return false;
    running_=true;return true;
}
bool WorkerWorkPublisher::completed(const CompletedWorkBatch &b)noexcept{
    if(!running_){failed_=true;return false;}WorkerWorkEvent e;e.kind=K::Completed;e.observed=b.observed;
    e.workUnits=b.workUnits;e.attempts=b.attempts;e.macsPerUnit=b.macsPerUnit;e.appliedHostWaitMs=b.appliedHostWaitMs;e.flag=b.hostVerificationFailed;
    const bool ok=emit(e);if(b.hostVerificationFailed)failed_=true;return ok&&!b.hostVerificationFailed;
}
bool WorkerWorkPublisher::boundary(const CompletedWorkBoundary &b)noexcept{
    if(!running_){failed_=true;return false;}WorkerWorkEvent e;e.observed=b.observed;e.appliedHostWaitMs=b.appliedHostWaitMs;
    if(b.kind==CompletedWorkBoundaryKind::Paused)e.kind=K::Paused;
    else if(b.kind==CompletedWorkBoundaryKind::Runnable)e.kind=K::Runnable;
    else if(b.kind==CompletedWorkBoundaryKind::Leaving)e.kind=K::Leaving;
    else{failed_=true;return false;}return emit(e);
}
bool WorkerWorkPublisher::end(bool reconciled,CompletedWorkTime t)noexcept{
    if(!running_){failed_=true;return false;}WorkerWorkEvent e;e.kind=K::End;e.observed=t;e.flag=reconciled;
    const bool ok=emit(e);running_=false;if(!reconciled)failed_=true;return ok&&reconciled;
}
bool WorkerWorkPublisher::finish(CompletedWorkTime t)noexcept{
    if(!started_||running_){failed_=true;return false;}WorkerWorkEvent e;e.kind=K::Finish;e.observed=t;
    if(!emit(e))return false;
    finished_=true;return true;
}
} // namespace om
