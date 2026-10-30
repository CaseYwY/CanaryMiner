#include "worker_control.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <limits>
namespace om {
namespace {
using S=WorkerControlState;using D=WorkerDirective;using K=WorkerReceiptKind;
bool hexKey(const std::string &s){return s.size()==32&&s.find_first_not_of('0')!=std::string::npos&&
    std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
bool sameBinding(const WorkerControlBinding &a,const WorkerControlBinding &b){return a.epoch==b.epoch&&a.slot==b.slot&&
    a.workerGeneration==b.workerGeneration&&a.sessionKey==b.sessionKey&&a.workerKey==b.workerKey;}
bool validReceipt(const WorkerReceipt &r){return validWorkerCommand(r.command)&&(r.kind==K::Accepted||(r.kind==K::Paused&&r.command.directive==D::Pause))&&
    r.observedMonoMs>=r.command.issuedMonoMs&&r.observedMonoMs<r.command.deadlineMonoMs;}
bool sameReceipt(const WorkerReceipt &a,const WorkerReceipt &b){return sameWorkerCommand(a.command,b.command)&&a.kind==b.kind&&a.observedMonoMs==b.observedMonoMs;}
bool number(std::string_view s,uint64_t &n){
    if(s.empty()||s.size()>20||(s.size()>1&&s[0]=='0')||s[0]<'0'||s[0]>'9')return false;
    const auto r=std::from_chars(s.data(),s.data()+s.size(),n);return r.ec==std::errc{}&&r.ptr==s.data()+s.size();
}
bool fields(std::string_view wire,std::array<std::string_view,12> &out,size_t count){
    if(wire.empty()||wire.size()>workerControlFrameLimit||wire.back()!='\n')return false;
    wire.remove_suffix(1);size_t pos=0;
    for(size_t i=0;i<count;++i){const auto end=wire.find(' ',pos);
        if((i+1==count)!=(end==std::string_view::npos))return false;
        out[i]=wire.substr(pos,end==std::string_view::npos?wire.size()-pos:end-pos);
        if(out[i].empty()||!std::all_of(out[i].begin(),out[i].end(),[](unsigned char c){return c>=33&&c<=126;}))return false;
        pos=end==std::string_view::npos?wire.size():end+1;
    }return true;
}
bool commandFields(const std::array<std::string_view,12> &f,WorkerCommand &c){
    uint64_t slot=0;
    if(f[1].size()!=32||f[5].size()!=32||!number(f[2],c.binding.epoch)||!number(f[3],slot)||slot>255||
        !number(f[4],c.binding.workerGeneration)||!number(f[6],c.revision)||!number(f[7],c.issuedMonoMs)||!number(f[8],c.deadlineMonoMs))return false;
    c.binding.sessionKey=std::string(f[1]);c.binding.workerKey=std::string(f[5]);c.binding.slot=static_cast<unsigned>(slot);
    if(f[9]=="P")c.directive=D::Pause;else if(f[9]=="R")c.directive=D::Run;else return false;
    return validWorkerCommand(c);
}
std::string body(const WorkerCommand &c){return c.binding.sessionKey+" "+std::to_string(c.binding.epoch)+" "+std::to_string(c.binding.slot)+" "+
    std::to_string(c.binding.workerGeneration)+" "+c.binding.workerKey+" "+std::to_string(c.revision)+" "+std::to_string(c.issuedMonoMs)+" "+
    std::to_string(c.deadlineMonoMs)+(c.directive==D::Pause?" P":" R");}
bool terminal(S s){return s==S::Stopped||s==S::Expired||s==S::ClockFault||s==S::Faulted;}
}
bool validWorkerControlBinding(const WorkerControlBinding &b){return hexKey(b.sessionKey)&&hexKey(b.workerKey)&&b.epoch&&b.workerGeneration&&b.slot<=255;}
bool validWorkerCommand(const WorkerCommand &c){return validWorkerControlBinding(c.binding)&&c.revision&&c.issuedMonoMs&&
    c.deadlineMonoMs>c.issuedMonoMs&&c.deadlineMonoMs-c.issuedMonoMs<=workerControlMaxLeaseMs&&(c.directive==D::Pause||c.directive==D::Run);}
bool sameWorkerCommand(const WorkerCommand &a,const WorkerCommand &b){return sameBinding(a.binding,b.binding)&&a.revision==b.revision&&
    a.issuedMonoMs==b.issuedMonoMs&&a.deadlineMonoMs==b.deadlineMonoMs&&a.directive==b.directive;}
bool encodeWorkerCommand(const WorkerCommand &c,std::string *out){if(!out||!validWorkerCommand(c))return false;*out="CMWC1 "+body(c)+"\n";return true;}
bool decodeWorkerCommand(std::string_view wire,WorkerCommand *out){std::array<std::string_view,12> f;WorkerCommand c;
    if(!out||!fields(wire,f,10)||f[0]!="CMWC1"||!commandFields(f,c))return false;
    *out=std::move(c);return true;}
bool encodeWorkerReceipt(const WorkerReceipt &r,std::string *out){if(!out||!validReceipt(r))return false;
    *out="CMWA1 "+body(r.command)+(r.kind==K::Paused?" P ":" A ")+std::to_string(r.observedMonoMs)+"\n";return true;}
bool decodeWorkerReceipt(std::string_view wire,WorkerReceipt *out){std::array<std::string_view,12> f;WorkerReceipt r;
    if(!out||!fields(wire,f,12)||f[0]!="CMWA1"||!commandFields(f,r.command)||!number(f[11],r.observedMonoMs))return false;
    if(f[10]=="P")r.kind=K::Paused;else if(f[10]=="A")r.kind=K::Accepted;else return false;
    if(!validReceipt(r))return false;
    *out=std::move(r);return true;}
bool WorkerControlFramer::feed(std::string_view bytes,const std::function<bool(std::string_view)> &frame){
    if(failed_)return false;
    if(!frame||bytes.size()>workerControlChunkLimit||static_cast<size_t>(std::count(bytes.begin(),bytes.end(),'\n'))>workerControlFramesPerChunk){failed_=true;return false;}
    for(char c:bytes){if(used_==buffer_.size()||(c!='\n'&&(c<32||c>126))){failed_=true;return false;}
        buffer_[used_++]=c;if(c=='\n'){
            try{if(!frame(std::string_view(buffer_.data(),used_))){failed_=true;return false;}}
            catch(...){failed_=true;return false;}
            used_=0;}}
    return true;
}
uint64_t workerControlNowMs(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
WorkerControlGate::WorkerControlGate(WorkerControlBinding b,WorkerControlClock clock):binding_(std::move(b)),clock_(clock?std::move(clock):WorkerControlClock(workerControlNowMs)){
    if(!validWorkerControlBinding(binding_)){view_.state=S::Faulted;return;}
    try{lastMonoMs_=clock_();}catch(...){view_.state=S::ClockFault;return;}
    if(!lastMonoMs_||lastMonoMs_>std::numeric_limits<uint64_t>::max()-workerControlMaxLeaseMs){view_.state=S::ClockFault;return;}
    startupDeadline_=lastMonoMs_+workerControlMaxLeaseMs;
}
bool WorkerControlGate::time()const{
    if(terminal(view_.state))return false;
    uint64_t now=0;try{now=clock_();}catch(...){view_.state=S::ClockFault;return false;}
    if(!now||now<lastMonoMs_){view_.state=S::ClockFault;return false;}lastMonoMs_=now;
    if(now>=(view_.receipt?view_.receipt->command.deadlineMonoMs:startupDeadline_)){view_.state=S::Expired;return false;}
    return true;
}
WorkerCommandResult WorkerControlGate::receive(const WorkerCommand &c){
    std::lock_guard<std::mutex> lock(mutex_);if(!time())return WorkerCommandResult::Rejected;
    if(!validWorkerCommand(c)||!sameBinding(c.binding,binding_)||c.issuedMonoMs>lastMonoMs_||c.deadlineMonoMs<=lastMonoMs_){view_.state=S::Faulted;return WorkerCommandResult::Rejected;}
    if(view_.receipt&&sameWorkerCommand(c,view_.receipt->command))return WorkerCommandResult::Duplicate;
    const uint64_t previous=view_.receipt?view_.receipt->command.revision:0;
    if(previous==std::numeric_limits<uint64_t>::max()||c.revision!=previous+1||
        (view_.receipt&&c.issuedMonoMs<view_.receipt->command.issuedMonoMs)||(c.directive==D::Run&&!confirmedInitialPause_)){
        view_.state=S::Faulted;return WorkerCommandResult::Rejected;}
    view_.receipt=WorkerReceipt{c,K::Accepted,lastMonoMs_};view_.state=S::Active;return WorkerCommandResult::Accepted;
}
WorkerCommandResult WorkerControlGate::receiveFrame(std::string_view frame){WorkerCommand c;
    if(!decodeWorkerCommand(frame,&c)){fail();return WorkerCommandResult::Rejected;}return receive(c);}
void WorkerControlGate::requestStop(){std::lock_guard<std::mutex> lock(mutex_);view_.state=S::Stopped;}
void WorkerControlGate::fail(){std::lock_guard<std::mutex> lock(mutex_);if(!terminal(view_.state))view_.state=S::Faulted;}
WorkerControlSnapshot WorkerControlGate::snapshot()const{std::lock_guard<std::mutex> lock(mutex_);time();return view_;}
WorkControlRequest WorkerControlGate::requestState()const noexcept{std::lock_guard<std::mutex> lock(mutex_);time();
    if(view_.state==S::Stopped)return WorkControlRequest::Stop;
    if(terminal(view_.state))return WorkControlRequest::Fault;
    return view_.receipt&&view_.receipt->command.directive==D::Run?WorkControlRequest::Run:WorkControlRequest::Pause;
}
bool WorkerControlGate::pauseRequested()const noexcept{return requestState()!=WorkControlRequest::Run;}
void WorkerControlGate::confirmPause(){
    if(view_.atPausedBoundary&&view_.receipt&&view_.receipt->command.directive==D::Pause&&view_.receipt->kind!=K::Paused){
        view_.receipt->kind=K::Paused;view_.receipt->observedMonoMs=lastMonoMs_;confirmedInitialPause_=true;}
}
void WorkerControlGate::pauseBoundary(bool paused)noexcept{std::lock_guard<std::mutex> lock(mutex_);
    view_.atPausedBoundary=paused;if(time()&&paused)confirmPause();}
void WorkerControlGate::pauseHeld()noexcept{std::lock_guard<std::mutex> lock(mutex_);if(time())confirmPause();}
WorkerControlIssuer::WorkerControlIssuer(WorkerControlBinding b,const SessionIntent &i,SessionToken t):binding_(std::move(b)),intent_(i),token_(t){}
void WorkerControlIssuer::fault(){if(!terminal(view_.state))view_.state=S::Faulted;}
void WorkerControlIssuer::requestStop(){attempted_=true;view_.state=S::Stopped;}
bool WorkerControlIssuer::start(uint64_t now){
    if(attempted_||terminal(view_.state))return false;
    attempted_=true;
    if(!intent_.permits(token_)){requestStop();return false;}
    if(!validWorkerControlBinding(binding_)||binding_.epoch!=token_.epoch){fault();return false;}
    if(!now){view_.state=S::ClockFault;return false;}
    lastMonoMs_=now;view_.state=S::Active;return true;
}
bool WorkerControlIssuer::time(uint64_t now){
    if(!intent_.permits(token_)){requestStop();return false;}
    if(view_.state!=S::Active)return false;
    if(!now||now<lastMonoMs_){view_.state=S::ClockFault;return false;}lastMonoMs_=now;
    if(deadline_&&now>=deadline_){view_.state=S::Expired;return false;}return true;
}
std::optional<WorkerCommand> WorkerControlIssuer::issue(D directive,uint64_t now,uint64_t lease){
    if(!time(now)||view_.pending)return {};
    if((directive!=D::Pause&&directive!=D::Run)||(directive==D::Run&&!initialPauseConfirmed_)||!lease||lease>workerControlMaxLeaseMs||
        now>std::numeric_limits<uint64_t>::max()-lease||revision_==std::numeric_limits<uint64_t>::max()){fault();return {};}
    WorkerCommand c{binding_,++revision_,now,now+lease,directive};view_.pending=c;deadline_=c.deadlineMonoMs;return c;
}
WorkerReceiptResult WorkerControlIssuer::acknowledge(const WorkerReceipt &r,uint64_t now){
    time(now);
    if(view_.settled&&sameReceipt(r,*view_.settled))return terminal(view_.state)?WorkerReceiptResult::Terminal:WorkerReceiptResult::Duplicate;
    if(!validReceipt(r)||r.observedMonoMs>now||!view_.pending||!sameWorkerCommand(r.command,*view_.pending)){fault();return WorkerReceiptResult::Rejected;}
    if(r.command.directive==D::Pause&&r.kind==K::Accepted)return terminal(view_.state)?WorkerReceiptResult::Terminal:WorkerReceiptResult::Waiting;
    // A matching late receipt can settle the command slot, but cannot revive expired/stopped authority or prove process death.
    view_.settled=r;view_.pending.reset();if(r.kind==K::Paused)initialPauseConfirmed_=true;
    return terminal(view_.state)?WorkerReceiptResult::Terminal:WorkerReceiptResult::Settled;
}
WorkerControlIssuerSnapshot WorkerControlIssuer::snapshot(uint64_t now){time(now);return view_;}
bool WorkerControlIssuer::paused(uint64_t now){return time(now)&&!view_.pending&&view_.settled&&view_.settled->kind==K::Paused;}
} // namespace om
