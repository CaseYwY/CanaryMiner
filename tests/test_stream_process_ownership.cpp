// Linux native-call interposition; real signals target only our creation-bound fixture pidfd.
// All numeric signals are suppressed. No storage, network, GPU, game or installed miner.
#include "product/duplex_task_process.h"
#include "product/worker_process.h"
#include "product_test_util.h"
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstring>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_TEST_STREAM_PIDFD 1
#endif
#endif
#if defined(OM_TEST_STREAM_PIDFD)
extern "C" int __real_posix_spawn(pid_t *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" pid_t __real_waitpid(pid_t,int *,int);
extern "C" int __real_waitid(idtype_t,id_t,siginfo_t *,int);
extern "C" long __real_syscall(long,...);
namespace {
enum class Fault { None, Stolen, Error, Pending, Race, SpawnUnavailable, SignalDenied, ReapedAtSpawn };
Fault fault=Fault::None;bool lost=false,cancelOnSpawn=false;
unsigned spawns=0,numericSpawns=0,numericSignals=0,lateProbes=0,signals=0;
int ownedFd=-1;pid_t ownedPid=-1;std::atomic<bool> cancel{false};
}
extern "C" int __wrap_posix_spawn(pid_t *pid,const char *path,const posix_spawn_file_actions_t *a,const posix_spawnattr_t *s,char *const argv[],char *const env[]){
    ++numericSpawns;const auto r=__real_posix_spawn(pid,path,a,s,argv,env);if(!r){ownedPid=*pid;++spawns;}return r;
}
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *a,const posix_spawnattr_t *s,char *const argv[],char *const env[]){
    if(fault==Fault::SpawnUnavailable)return ENOSYS;
    const auto r=__real_pidfd_spawn(fd,path,a,s,argv,env);
    if(!r){ownedFd=*fd;++spawns;if(cancelOnSpawn)cancel=true;
        if(fault==Fault::ReapedAtSpawn){siginfo_t st{};CHECK(__real_waitid(P_PIDFD,static_cast<id_t>(*fd),&st,WEXITED)==0);}}
    return r;
}
extern "C" pid_t __wrap_waitpid(pid_t pid,int *status,int options){
    if(lost)++lateProbes;
    const auto r=__real_waitpid(pid,status,options);
    if(fault==Fault::Race&&r==0){fault=Fault::None;int st=0;CHECK(__real_waitpid(pid,&st,0)==pid);return 0;}
    if(fault==Fault::Stolen&&r==pid){lost=true;errno=ECHILD;return -1;}
    if(r<0&&errno==ECHILD)lost=true;
    return r;
}
extern "C" int __wrap_waitid(idtype_t type,id_t id,siginfo_t *info,int options){
    if(lost){++lateProbes;errno=ECHILD;return -1;}
    if(fault==Fault::Error){lost=true;errno=EINVAL;return -1;}
    const auto r=__real_waitid(type,id,info,options|(fault==Fault::Pending?WNOWAIT:0));
    if(fault==Fault::Pending&&!r){std::memset(info,0,sizeof(*info));return 0;}
    if(fault==Fault::Race&&!r&&!info->si_pid){fault=Fault::None;siginfo_t st{};CHECK(__real_waitid(type,id,&st,WEXITED)==0);return 0;}
    if(fault==Fault::Stolen&&!r&&info->si_pid){lost=true;errno=ECHILD;return -1;}
    if(r<0&&errno==ECHILD)lost=true;
    return r;
}
extern "C" int __wrap_kill(pid_t,int){++numericSignals;errno=ESRCH;return -1;}
extern "C" long __wrap_syscall(long number,...){
    va_list args;va_start(args,number);
    if(number==SYS_pidfd_send_signal){const int fd=va_arg(args,int),signal=va_arg(args,int);auto *info=va_arg(args,siginfo_t *);const auto flags=va_arg(args,unsigned);va_end(args);
        if(signal)++signals;
        if(fault==Fault::SignalDenied){errno=EPERM;return -1;}return __real_syscall(number,fd,signal,info,flags);
    }
    va_end(args);errno=ENOSYS;return -1;
}
namespace {
#if defined(OM_TEST_STREAM_WORKER)
using Owner=om::WorkerControlProcess;
bool launch(Owner &p,const std::filesystem::path &exe){const auto now=om::workerControlNowMs();om::WorkerCommand c{{"11111111111111111111111111111111","22222222222222222222222222222222",1,1,0},1,now,now+3000,om::WorkerDirective::Pause};return p.start(exe,{},c,cancel);}
bool unknown(Owner &p){return p.poll().state==om::WorkerProcessState::Unreaped&&!p.reaped();}
#else
using Owner=om::DuplexTaskProcess;
bool launch(Owner &p,const std::filesystem::path &exe){return p.start(exe,cancel);}
bool unknown(Owner &p){return p.poll().state==om::DuplexTaskState::Unreaped;}
#endif
void cleanup(){
    if(ownedFd>=0){siginfo_t info{};const int r=__real_waitid(P_PIDFD,static_cast<id_t>(ownedFd),&info,WEXITED|WNOHANG|WNOWAIT);
        if(!r){if(!info.si_pid)CHECK(__real_syscall(SYS_pidfd_send_signal,ownedFd,SIGKILL,nullptr,0u)==0);CHECK(__real_waitid(P_PIDFD,static_cast<id_t>(ownedFd),&info,WEXITED)==0);}else CHECK(errno==ECHILD);
    }else if(ownedPid>0){int st=0;const auto r=__real_waitpid(ownedPid,&st,0);CHECK(r==ownedPid||(r<0&&errno==ECHILD));}
}
void handler(int){}
}
int main(int argc,char **argv){
    if(argc==5&&(!std::strcmp(argv[1],"--worker-control-v1")||!std::strcmp(argv[1],"--local-stream-v1"))){std::this_thread::sleep_for(std::chrono::milliseconds(60));return 0;}
    CHECK(argc==2);if(argc!=2)return finishTests("stream ownership");
    const std::string mode=argv[1];const auto exe=std::filesystem::absolute(argv[0]);
    if(mode=="race"||mode=="error"||mode=="stolen"||mode=="reaped-at-spawn"){
        fault=mode=="race"?Fault::Race:mode=="error"?Fault::Error:mode=="stolen"?Fault::Stolen:Fault::ReapedAtSpawn;
        {Owner p;CHECK(launch(p,exe));
            if(mode=="stolen"){std::this_thread::sleep_for(std::chrono::milliseconds(100));CHECK(unknown(p));}
            CHECK(!p.terminate(250)&&unknown(p));Owner other;CHECK(!launch(other,exe));}
        Owner next;CHECK(!launch(next,exe));CHECK(spawns==1&&lateProbes==0);cleanup();
    }else if(mode=="pending"){
        fault=Fault::Pending;{Owner p;CHECK(launch(p,exe)&&!p.terminate(0)&&unknown(p));Owner other;CHECK(!launch(other,exe));}
        fault=Fault::None;Owner next;CHECK(launch(next,exe)&&next.terminate(500));CHECK(spawns==2);
    }else if(mode=="unavailable"){
        fault=Fault::SpawnUnavailable;{Owner p;CHECK(!launch(p,exe)&&spawns==0);}fault=Fault::None;Owner next;CHECK(launch(next,exe)&&next.terminate(500));
    }else if(mode=="cancel-created"){
        cancelOnSpawn=true;{Owner p;CHECK(!launch(p,exe)&&spawns==1&&p.terminate(500));}
        cancelOnSpawn=false;cancel=false;Owner next;CHECK(launch(next,exe)&&next.terminate(500)&&spawns==2);
    }else if(mode=="policy"){
        struct sigaction original{};CHECK(sigaction(SIGCHLD,nullptr,&original)==0);
        for(unsigned i=0;i<3;++i){struct sigaction p{};sigemptyset(&p.sa_mask);p.sa_handler=i==0?SIG_IGN:i==1?SIG_DFL:handler;p.sa_flags=i==1?SA_NOCLDWAIT:0;
            CHECK(sigaction(SIGCHLD,&p,nullptr)==0);Owner owner;CHECK(!launch(owner,exe)&&spawns==0);}
        CHECK(sigaction(SIGCHLD,&original,nullptr)==0);Owner next;CHECK(launch(next,exe)&&next.terminate(500));
    }else CHECK(false);
    CHECK(numericSpawns==0&&numericSignals==0);
    return finishTests("stream native identity and retained uncertainty (CPU fixture only)");
}
#else
int main(){std::puts("SKIP: stream native ownership fixture requires glibc >= 2.40");return 77;}
#endif
