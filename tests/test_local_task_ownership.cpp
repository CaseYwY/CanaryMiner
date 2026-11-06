// Actual one-shot/holder client with Linux linker interposition; no storage, network or mining.
#include "product/local_task_process.h"
#include "product_test_util.h"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_TEST_LOCAL_PIDFD 1
#endif
#endif
#if defined(OM_TEST_LOCAL_PIDFD)
using namespace om;
namespace {
enum class Fault { None, Stolen, WaitError, Pending, SpawnUnavailable, SignalDenied, CrossClient };
Fault fault=Fault::None;
pid_t owned=-1;int ownedFd=-1;
unsigned spawns=0,numericSpawns=0,numericSignals=0,numericOpens=0,signals=0,afterUnknown=0,badArgs=0;
bool lost=false,cancelOnSpawn=false;
std::atomic<bool> cancel{false};std::filesystem::path helper;
constexpr unsigned timeout=600;
void unknown(const LocalTaskResult &r,bool started){CHECK(r.status==LocalTaskStatus::Unreaped);CHECK(r.childStarted==started&&r.response.empty());}
int child(int argc,char **argv){std::string request;
    if(!readLocalTaskInput(argc,argv,&request))return 2;
    if(std::getenv("CANARY_SYNTHETIC_SECRET")||std::getenv("SSLKEYLOGFILE"))return 3;
    if(request=="FAIL\n")return 1;
    if(request=="WAIT\n"){std::this_thread::sleep_for(std::chrono::seconds(10));return 0;}
    if(request=="OVERFLOW\n"){const std::string text(localTaskResponseLimit+4096,'x');std::fwrite(text.data(),1,text.size(),stdout);return 0;}
    if(!writeLocalTaskOutput(request=="HOLD\n"?"SYNTHETIC READY\n":"SYNTHETIC OK\n"))return 4;
    if(request=="HOLD\n")std::this_thread::sleep_for(std::chrono::seconds(10));
    return 0;
}
}
extern "C" int __real_posix_spawn(pid_t *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" pid_t __real_waitpid(pid_t,int *,int);
extern "C" int __real_waitid(idtype_t,id_t,siginfo_t *,int);
extern "C" long __real_syscall(long,...);
extern "C" int __wrap_posix_spawn(pid_t *pid,const char *path,const posix_spawn_file_actions_t *a,const posix_spawnattr_t *s,char *const argv[],char *const env[]){
    ++numericSpawns;const int result=__real_posix_spawn(pid,path,a,s,argv,env);if(!result){owned=*pid;++spawns;}return result;
}
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *a,const posix_spawnattr_t *s,char *const argv[],char *const env[]){
    if(fault==Fault::SpawnUnavailable)return ENOSYS;
    if(!argv[0]||!argv[1]||std::strcmp(argv[1],"--local-task-v1")||argv[2])++badArgs;
    const int result=__real_pidfd_spawn(fd,path,a,s,argv,env);
    if(!result){owned=1;ownedFd=*fd;++spawns;if(cancelOnSpawn)cancel=true;}
    return result;
}
extern "C" int __wrap_kill(pid_t,int){++numericSignals;errno=ESRCH;return -1;} // never forward numeric signals
extern "C" pid_t __wrap_waitpid(pid_t pid,int *status,int options){
    if(lost)++afterUnknown;
    const auto result=__real_waitpid(pid,status,options);
    if(fault==Fault::Stolen&&result==pid&&pid==owned){lost=true;errno=ECHILD;return -1;}return result;
}
extern "C" int __wrap_waitid(idtype_t type,id_t id,siginfo_t *info,int options){
    if(lost){++afterUnknown;errno=ECHILD;return -1;}
    if(fault==Fault::WaitError){lost=true;errno=EINVAL;return -1;}
    const int result=__real_waitid(type,id,info,options|(fault==Fault::Pending?WNOWAIT:0));
    if(fault==Fault::Pending&&!result){std::memset(info,0,sizeof(*info));return 0;}
    if(fault==Fault::Stolen&&!result&&info->si_pid>0){lost=true;errno=ECHILD;return -1;}
    if(fault==Fault::CrossClient&&!result&&info->si_pid>0){
        fault=Fault::WaitError;{LocalTaskProcess other(helper);unknown(other.run("OK\n",cancel,timeout),true);}fault=Fault::None;
    }
    return result;
}
extern "C" long __wrap_syscall(long number,...){
    va_list args;va_start(args,number);
    if(number==SYS_pidfd_send_signal){const int fd=va_arg(args,int),signal=va_arg(args,int);auto *info=va_arg(args,siginfo_t *);const auto flags=va_arg(args,unsigned);va_end(args);
        if(signal){++signals;if(lost)++afterUnknown;}
        if(fault==Fault::SignalDenied){errno=EPERM;return -1;}return __real_syscall(number,fd,signal,info,flags);
    }
    if(number==SYS_pidfd_open)++numericOpens;
    va_end(args);errno=ENOSYS;return -1;
}
namespace {
void cleanup(){bool okay=false;
    if(ownedFd>=0){siginfo_t info{};const auto r=__real_waitid(P_PIDFD,static_cast<id_t>(ownedFd),&info,WEXITED|WNOHANG|WNOWAIT);
        if(!r){bool killed=true;if(!info.si_pid)killed=__real_syscall(SYS_pidfd_send_signal,ownedFd,SIGKILL,nullptr,0u)==0;
            okay=killed&&__real_waitid(P_PIDFD,static_cast<id_t>(ownedFd),&info,WEXITED)==0;
        }else okay=errno==ECHILD;
    }else if(owned>0){int status=0;const auto r=__real_waitpid(owned,&status,0);okay=r==owned||(r<0&&errno==ECHILD);}
    CHECK(okay);
}
void handler(int){}
}
int main(int argc,char **argv){
    if(argc==2&&!std::strcmp(argv[1],"--local-task-v1"))return child(argc,argv);
    CHECK(argc==2);if(argc!=2)return finishTests("local task ownership");
    helper=std::filesystem::absolute(argv[0]);const std::string mode=argv[1];
    if(mode=="run-stolen"||mode=="run-error"||mode=="hold-error"||mode=="hold-after-ready"){
        fault=mode=="run-stolen"?Fault::Stolen:mode=="hold-after-ready"?Fault::None:Fault::WaitError;
        unsigned callbacks=0;
        {LocalTaskProcess client(helper);
            if(mode=="hold-error"||mode=="hold-after-ready"){
                unknown(client.hold("HOLD\n",cancel,timeout,[&](const std::string &line){++callbacks;CHECK(line=="SYNTHETIC READY");fault=Fault::WaitError;return true;}),true);
                CHECK(callbacks==(mode=="hold-after-ready"?1u:0u));
            }else unknown(client.run("OK\n",cancel,timeout),true);
            unknown(client.run("OK\n",cancel,timeout),false);CHECK(spawns==1&&numericSignals==0&&signals==0);
        }
        LocalTaskProcess next(helper);unknown(next.run("OK\n",cancel,timeout),false);CHECK(spawns==1&&afterUnknown==0);cleanup();
    }else if(mode=="pending"||mode=="quarantine"||mode=="cancel-cleanup"||mode=="overflow-cleanup"){
        fault=Fault::Pending;cancelOnSpawn=mode=="cancel-cleanup";
        {LocalTaskProcess client(helper);unknown(client.run(mode=="overflow-cleanup"?"OVERFLOW\n":"OK\n",cancel,timeout),true);
            cancelOnSpawn=false;cancel=false;CHECK(spawns==1&&signals>0&&numericSignals==0);
            LocalTaskProcess other(helper);unknown(other.run("OK\n",cancel,timeout),false);CHECK(spawns==1);
            if(mode=="pending"){fault=Fault::None;const auto r=client.run("OK\n",cancel,timeout);CHECK(r.status==LocalTaskStatus::Ok&&r.childStarted&&r.response=="SYNTHETIC OK\n");CHECK(spawns==2);}
        }
        fault=Fault::None;LocalTaskProcess after(helper);CHECK(after.run("OK\n",cancel,timeout).status==LocalTaskStatus::Ok);CHECK(spawns==(mode=="pending"?3u:2u));
    }else if(mode=="cross-client"){
        fault=Fault::CrossClient;{LocalTaskProcess client(helper);unknown(client.run("OK\n",cancel,timeout),true);}
        CHECK(spawns==2&&lost&&signals==0&&numericSignals==0);LocalTaskProcess after(helper);unknown(after.run("OK\n",cancel,timeout),false);CHECK(spawns==2);cleanup();
    }else if(mode=="policy"){
        struct sigaction original{};CHECK(sigaction(SIGCHLD,nullptr,&original)==0);
        for(unsigned i=0;i<3;++i){struct sigaction p{};sigemptyset(&p.sa_mask);p.sa_handler=i==0?SIG_IGN:i==1?SIG_DFL:handler;p.sa_flags=i==1?SA_NOCLDWAIT:0;
            CHECK(sigaction(SIGCHLD,&p,nullptr)==0);LocalTaskProcess client(helper);const auto r=client.run("OK\n",cancel,timeout);CHECK(r.status==LocalTaskStatus::Unavailable&&!r.childStarted&&spawns==0);}
        CHECK(sigaction(SIGCHLD,&original,nullptr)==0);LocalTaskProcess client(helper);CHECK(client.run("OK\n",cancel,timeout).status==LocalTaskStatus::Ok);
    }else if(mode=="spawn-unavailable"){
        fault=Fault::SpawnUnavailable;LocalTaskProcess client(helper);auto r=client.run("OK\n",cancel,timeout);CHECK(r.status==LocalTaskStatus::Unavailable&&!r.childStarted&&spawns==0);
        fault=Fault::None;r=client.run("OK\n",cancel,timeout);CHECK(r.status==LocalTaskStatus::Ok&&r.childStarted&&spawns==1);
    }else if(mode=="signal-denied"){
        fault=Fault::SignalDenied;LocalTaskProcess client(helper);unknown(client.run("WAIT\n",cancel,timeout),true);CHECK(spawns==1&&signals>0&&numericSignals==0);
        CHECK(__real_syscall(SYS_pidfd_send_signal,ownedFd,SIGKILL,nullptr,0u)==0);fault=Fault::None;CHECK(client.run("OK\n",cancel,timeout).status==LocalTaskStatus::Ok&&spawns==2);
    }else if(mode=="ordinary"){
        LocalTaskProcess client(helper);CHECK(client.run({},cancel,timeout).status==LocalTaskStatus::Invalid&&spawns==0);
        cancel=true;CHECK(client.run("OK\n",cancel,timeout).status==LocalTaskStatus::Cancelled&&spawns==0);cancel=false;
        auto r=client.run("OK\n",cancel,timeout);CHECK(r.status==LocalTaskStatus::Ok&&r.childStarted&&r.response=="SYNTHETIC OK\n");
        r=client.run("FAIL\n",cancel,timeout);CHECK(r.status==LocalTaskStatus::Unavailable&&r.childStarted&&r.response.empty());
        unsigned callbacks=0;r=client.hold("HOLD\n",cancel,timeout,[&](const std::string &line){++callbacks;CHECK(line=="SYNTHETIC READY");cancel=true;return true;});
        CHECK(r.status==LocalTaskStatus::Cancelled&&r.childStarted&&callbacks==1);cancel=false;
        r=client.hold("HOLD\n",cancel,timeout,[](const std::string &)->bool{throw std::runtime_error("synthetic");});CHECK(r.status==LocalTaskStatus::Unavailable&&r.childStarted);
        CHECK(client.run("OK\n",cancel,timeout).status==LocalTaskStatus::Ok);
    }else CHECK(false);
    CHECK(numericSpawns==0&&numericSignals==0&&numericOpens==0&&badArgs==0);
    return finishTests("local task ownership");
}
#else
int main(){std::puts("SKIP: atomic local-helper ownership fixture requires glibc >= 2.40");return 77;}
#endif
