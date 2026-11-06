// Actual statically linked clients, synthetic helpers only; no production injection API.
#include "product/local_task_process.h"
#include "product/public_json_process.h"
#include "product/duplex_task_process.h"
#include "product/worker_process.h"
#include "product/native_process_admission.h"
#include "product_test_util.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_TEST_CROSS_HELPER 1
#endif
#endif
#if defined(OM_TEST_CROSS_HELPER)
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" int __real_waitid(idtype_t,id_t,siginfo_t *,int);
namespace {
enum class Kind { Local, Public, Duplex, Worker };
enum class Fault { None, Stolen, Nested, Pending };
Fault fault=Fault::None;Kind inner=Kind::Local;
unsigned spawns=0;int pendingFd=-1;bool transient=false;
std::filesystem::path helper;std::atomic<bool> cancel{false};
struct Outcome {bool success=false,unknown=false;std::size_t bytes=0;};
Outcome operation(Kind);
void nested();
Kind kind(const std::string &name){
    if(name=="local")return Kind::Local;
    if(name=="public")return Kind::Public;
    if(name=="duplex")return Kind::Duplex;
    CHECK(name=="worker");return Kind::Worker;
}
}
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *a,const posix_spawnattr_t *s,char *const argv[],char *const env[]){
    const auto r=__real_pidfd_spawn(fd,path,a,s,argv,env);if(!r){++spawns;if(fault==Fault::Pending)pendingFd=*fd;}return r;
}
extern "C" int __wrap_waitid(idtype_t type,id_t id,siginfo_t *info,int options){
    const bool pending=fault==Fault::Pending&&static_cast<int>(id)==pendingFd;
    const int r=__real_waitid(type,id,info,options|(pending?WNOWAIT:0));
    if(pending&&!r){std::memset(info,0,sizeof(*info));return 0;}
    if(!r&&info->si_pid>0){
        if(fault==Fault::Stolen){fault=Fault::None;errno=ECHILD;return -1;}
        if(fault==Fault::Nested){fault=Fault::None;nested();}
    }
    return r;
}
namespace {
Outcome operation(Kind selected){
    if(selected==Kind::Local){om::LocalTaskProcess p(helper);const auto r=p.run("SYNTHETIC\n",cancel,600);return {r.status==om::LocalTaskStatus::Ok,r.status==om::LocalTaskStatus::Unreaped,r.response.size()};}
    if(selected==Kind::Public){om::PublicJsonProcessClient p(helper);const auto r=p.get({om::PublicFeed::GatePearlCandles,60,120},cancel,{300,600,8192,2048});
        return {r.status==om::PublicHttpsStatus::Ok,r.status==om::PublicHttpsStatus::Unreaped,r.body.size()};}
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);Outcome out;
    if(selected==Kind::Duplex){
        om::DuplexTaskProcess p;const bool started=p.start(helper,cancel);
        do{const auto v=p.poll();out.bytes+=v.bytes.size();out.unknown=v.state==om::DuplexTaskState::Unreaped||(v.channelFault&&om::nativeProcessUnresolved());
            out.success=v.state==om::DuplexTaskState::Exited&&v.exitCode==0&&!v.channelFault;
            if(!started||out.unknown||v.state==om::DuplexTaskState::Exited)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }while(std::chrono::steady_clock::now()<until);
        if(!p.terminate(250))out.unknown=true;
    }else{
        om::WorkerControlProcess p;const auto now=om::workerControlNowMs();
        om::WorkerCommand initial{{"11111111111111111111111111111111","22222222222222222222222222222222",1,1,0},1,now,now+3000,om::WorkerDirective::Pause};
        const bool started=p.start(helper,{},initial,cancel);
        do{const auto v=p.poll();CHECK(v.receipts.empty()&&v.work.empty());out.bytes+=v.output.size();
            out.unknown=v.state==om::WorkerProcessState::Unreaped||(v.controlFault&&om::nativeProcessUnresolved());
            out.success=p.reaped()&&v.exitCode==0&&!v.controlFault;
            if(!started||out.unknown||p.reaped())break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }while(std::chrono::steady_clock::now()<until);
        if(!p.terminate(250))out.unknown=true;
    }
    return out;
}
void nested(){
    if(!transient){fault=Fault::Stolen;const auto r=operation(inner);CHECK(r.unknown&&!r.bytes);return;}
    // Actual proof clears a withheld-wait fault before outer publication: only the original epoch catches this.
    CHECK(inner==Kind::Local);om::LocalTaskProcess p(helper);fault=Fault::Pending;
    const auto first=p.run("SYNTHETIC\n",cancel,600);CHECK(first.status==om::LocalTaskStatus::Unreaped);
    fault=Fault::None;const auto next=p.run("SYNTHETIC\n",cancel,600);CHECK(next.status==om::LocalTaskStatus::Ok);
}
}
int main(int argc,char **argv){
    if(argc==2&&!std::strcmp(argv[1],"--local-task-v1")){std::string input;if(!om::readLocalTaskInput(argc,argv,&input))return 2;return om::writeLocalTaskOutput("SYNTHETIC\n")?0:2;}
    if(argc==8){om::PublicJsonResponse r;r.status=om::PublicHttpsStatus::Ok;r.httpStatus=200;r.observedUtc=1;r.body="{\"fixture\":true}";
        std::string wire;if(!om::encodePublicJsonResponse(r,&wire))return 2;return std::fwrite(wire.data(),1,wire.size(),stdout)==wire.size()?0:2;}
    if(argc==5&&(!std::strcmp(argv[1],"--worker-control-v1")||!std::strcmp(argv[1],"--local-stream-v1"))){std::this_thread::sleep_for(std::chrono::milliseconds(60));return 0;}
    CHECK(argc==3);if(argc!=3)return finishTests("cross-helper admission");
    helper=std::filesystem::absolute(argv[0]);const std::string mode=argv[1],pair=argv[2];const auto split=pair.find('-');
    CHECK(split!=std::string::npos);if(split==std::string::npos)return finishTests("cross-helper admission");
    const auto first=kind(pair.substr(0,split)),second=kind(pair.substr(split+1));
    if(mode=="blocked"){
        fault=Fault::Stolen;auto r=operation(first);CHECK(r.unknown&&!r.bytes);r=operation(second);CHECK(r.unknown&&!r.bytes);CHECK(spawns==1);
    }else if(mode=="final"||mode=="transient"){
        transient=mode=="transient";inner=second;fault=Fault::Nested;
        const auto r=operation(first);CHECK(r.unknown&&!r.bytes);CHECK(spawns==(transient?3u:2u));
        if(!transient){const auto after=operation(first);CHECK(after.unknown&&!after.bytes);CHECK(spawns==2);}
        else CHECK(operation(Kind::Local).success);
    }else CHECK(false);
    return finishTests("cross-module native helper/worker admission and original fault epoch (CPU only)");
}
#else
int main(){std::puts("SKIP: native cross-module fixture requires glibc >= 2.40");return 77;}
#endif
