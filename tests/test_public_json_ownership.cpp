// Linux linker interposition only: the ordinary library has no injectable ownership APIs.
// All real operations target the child/pidfd returned by this test's own spawn; numeric kills never forward.
#include "product/public_json_process.h"
#include "product_test_util.h"
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 40)
#define OM_TEST_PUBLIC_PIDFD_SPAWN 1
#endif
#endif
#if defined(OM_TEST_PUBLIC_PIDFD_SPAWN)
using namespace om;
namespace {
enum class Fault { None, Stolen, WaitError, Pending, SpawnUnavailable, SignalDenied, ReapedAtSpawn, CrossClient };
Fault fault=Fault::None;
pid_t owned=-1;
int ownedFd=-1;
unsigned spawns=0,numericSignals=0,signals=0,afterUnknown=0;
unsigned numericSpawns=0,numericAcquisitions=0,badIdentity=0;
bool lost=false;
bool cancelOnSpawn=false;
std::filesystem::path fixtureHelper;
constexpr auto unreaped=PublicHttpsStatus::Unreaped;
const PublicJsonRequest request{PublicFeed::GatePearlCandles,60,120};
const PublicHttpsLimits limits{300,600,8192,2048};
std::atomic<bool> cancel{false};
void checkUnknown(const PublicJsonResponse &r){CHECK(r.status==unreaped);CHECK(r.body.empty()&&!r.httpStatus&&!r.observedUtc&&!r.retryAfterSeconds);}
void handler(int){}
}
extern "C" int __real_posix_spawn(pid_t *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" pid_t __real_waitpid(pid_t,int *,int);
extern "C" int __real_waitid(idtype_t,id_t,siginfo_t *,int);
extern "C" long __real_syscall(long,...);
extern "C" int __wrap_posix_spawn(pid_t *pid,const char *path,const posix_spawn_file_actions_t *actions,const posix_spawnattr_t *attributes,char *const argv[],char *const env[]){
    ++numericSpawns;
    const int result=__real_posix_spawn(pid,path,actions,attributes,argv,env);
    if(!result){owned=*pid;++spawns;if(cancelOnSpawn)cancel=true;}
    return result;
}
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *actions,const posix_spawnattr_t *attributes,char *const argv[],char *const env[]){
    if(fault==Fault::SpawnUnavailable)return ENOSYS;
    const int result=__real_pidfd_spawn(fd,path,actions,attributes,argv,env);
    if(!result){
        owned=1;ownedFd=*fd;++spawns;if(cancelOnSpawn)cancel=true;
        if(*fd<0||(fcntl(*fd,F_GETFD)&FD_CLOEXEC)==0)++badIdentity;
        if(fault==Fault::ReapedAtSpawn){siginfo_t info{};if(__real_waitid(P_PIDFD,static_cast<id_t>(*fd),&info,WEXITED))++badIdentity;}
    }
    return result;
}
extern "C" int __wrap_kill(pid_t,int){++numericSignals;errno=ESRCH;return -1;}
extern "C" pid_t __wrap_waitpid(pid_t pid,int *status,int options){
    if(lost)++afterUnknown;
    const auto result=__real_waitpid(pid,status,options);
    if(fault==Fault::Stolen&&pid==owned&&result==pid){lost=true;errno=ECHILD;return -1;}
    return result;
}
extern "C" int __wrap_waitid(idtype_t type,id_t id,siginfo_t *info,int options){
    if(owned<0)return __real_waitid(type,id,info,options);
    if(lost){++afterUnknown;errno=ECHILD;return -1;}
    if(fault==Fault::WaitError){lost=true;errno=EINVAL;return -1;}
    const int result=__real_waitid(type,id,info,options|(fault==Fault::Pending?WNOWAIT:0));
    if(fault==Fault::Pending&&!result){std::memset(info,0,sizeof(*info));return 0;}
    if(fault==Fault::ReapedAtSpawn&&result<0&&errno==ECHILD){lost=true;return result;}
    if(fault==Fault::Stolen&&!result&&info->si_pid>0){
        if(options&WNOWAIT)__real_waitid(type,id,info,WEXITED);
        lost=true;errno=ECHILD;return -1;
    }
    if(fault==Fault::CrossClient&&!result&&info->si_pid>0){
        // A second real client loses ownership exactly while the first receives its final exit proof.
        fault=Fault::WaitError;
        {PublicJsonProcessClient other(fixtureHelper);checkUnknown(other.get(request,cancel,limits));}
        fault=Fault::None;
    }
    return result;
}
extern "C" long __wrap_syscall(long number,...){
    va_list args;va_start(args,number);
    if(number==SYS_pidfd_open){
        const auto pid=va_arg(args,pid_t);const auto flags=va_arg(args,unsigned);va_end(args);
        ++numericAcquisitions;
        const long result=__real_syscall(number,pid,flags);
        if(pid==owned&&result>=0)ownedFd=static_cast<int>(result);
        return result;
    }
    if(number==SYS_pidfd_send_signal){
        const int fd=va_arg(args,int),signal=va_arg(args,int);auto *info=va_arg(args,siginfo_t *);const auto flags=va_arg(args,unsigned);va_end(args);
        if(signal){++signals;if(lost)++afterUnknown;}
        if(signal&&fault==Fault::SignalDenied){errno=EPERM;return -1;}
        return __real_syscall(number,fd,signal,info,flags);
    }
    va_end(args);errno=ENOSYS;return -1;
}
namespace {
void cleanupOwned(){
    // Lost ownership is injected only for our actual child. Never use a copied numeric group identifier.
    if(ownedFd>=0){
        siginfo_t info{};
        const int result=__real_waitid(P_PIDFD,static_cast<id_t>(ownedFd),&info,WEXITED|WNOHANG|WNOWAIT);
        if(!result&&!info.si_pid){
            CHECK(__real_syscall(SYS_pidfd_send_signal,ownedFd,SIGKILL,nullptr,0u)==0);
            CHECK(__real_waitid(P_PIDFD,static_cast<id_t>(ownedFd),&info,WEXITED)==0);
        }else if(!result)CHECK(__real_waitid(P_PIDFD,static_cast<id_t>(ownedFd),&info,WEXITED)==0);
        else CHECK(errno==ECHILD);
    }else if(owned>0){
        // The missing-pidfd fixture child exits normally and is the only child created in this test process.
        int status=0;const auto result=__real_waitpid(owned,&status,0);
        CHECK(result==owned||(result<0&&errno==ECHILD));if(result==owned)CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    }
}
}
int main(int argc,char **argv){
    CHECK(argc==3);if(argc!=3)return finishTests("public JSON native ownership");
    const auto helper=std::filesystem::absolute(argv[1]);const std::string mode=argv[2];
    if(mode=="stolen"||mode=="wait-error"||mode=="reaped-at-spawn"){
        fault=mode=="stolen"?Fault::Stolen:mode=="wait-error"?Fault::WaitError:Fault::ReapedAtSpawn;
        {
            PublicJsonProcessClient client(helper);checkUnknown(client.get(request,cancel,limits));
            CHECK(lost&&spawns==1&&signals==0&&numericSignals==0);
            checkUnknown(client.get(request,cancel,limits));CHECK(spawns==1);
        }
        PublicJsonProcessClient next(helper);checkUnknown(next.get(request,cancel,limits));
        CHECK(spawns==1&&signals==0&&numericSignals==0&&afterUnknown==0);cleanupOwned();
    }else if(mode=="pending"||mode=="quarantine"||mode=="cancel-cleanup"||mode=="overflow-cleanup"){
        fault=Fault::Pending;
        {
            cancelOnSpawn=mode=="cancel-cleanup";
            const auto input=mode=="overflow-cleanup"?PublicJsonRequest{PublicFeed::GatePearlCandles,180,240}:request;
            PublicJsonProcessClient client(helper);checkUnknown(client.get(input,cancel,limits));CHECK(spawns==1&&signals>0&&numericSignals==0);
            cancelOnSpawn=false;cancel=false;
            PublicJsonProcessClient other(helper);checkUnknown(other.get(request,cancel,limits));CHECK(spawns==1);
            if(mode=="pending"){
                fault=Fault::None;CHECK(client.get(request,cancel,limits).status==PublicHttpsStatus::Ok);CHECK(spawns==2);
                CHECK(other.get(request,cancel,limits).status==PublicHttpsStatus::Ok);CHECK(spawns==3);
            }
        }
        fault=Fault::None;
        PublicJsonProcessClient after(helper);CHECK(after.get(request,cancel,limits).status==PublicHttpsStatus::Ok);
        CHECK(spawns==(mode=="pending"?4u:2u));CHECK(numericSignals==0);
    }else if(mode=="policy"){
        struct sigaction original{};CHECK(sigaction(SIGCHLD,nullptr,&original)==0);
        for(unsigned variant=0;variant<3;++variant){
            struct sigaction policy{};sigemptyset(&policy.sa_mask);policy.sa_handler=variant==0?SIG_IGN:variant==1?SIG_DFL:handler;
            policy.sa_flags=variant==1?SA_NOCLDWAIT:0;CHECK(sigaction(SIGCHLD,&policy,nullptr)==0);
            PublicJsonProcessClient client(helper);CHECK(client.get(request,cancel,limits).status==PublicHttpsStatus::Unavailable);CHECK(spawns==0);
        }
        CHECK(sigaction(SIGCHLD,&original,nullptr)==0);
        PublicJsonProcessClient client(helper);CHECK(client.get(request,cancel,limits).status==PublicHttpsStatus::Ok);CHECK(spawns==1&&numericSignals==0);
    }else if(mode=="spawn-unavailable"){
        fault=Fault::SpawnUnavailable;
        PublicJsonProcessClient client(helper);const auto result=client.get(request,cancel,limits);
        CHECK(result.status==PublicHttpsStatus::Unavailable&&spawns==0);
        fault=Fault::None;CHECK(client.get(request,cancel,limits).status==PublicHttpsStatus::Ok);CHECK(spawns==1);
        CHECK(numericSignals==0);
    }else if(mode=="cross-client"){
        fault=Fault::CrossClient;fixtureHelper=helper;
        {PublicJsonProcessClient client(helper);checkUnknown(client.get(request,cancel,limits));}
        CHECK(lost&&spawns==2&&signals==0&&numericSignals==0&&afterUnknown==0);
        PublicJsonProcessClient next(helper);checkUnknown(next.get(request,cancel,limits));CHECK(spawns==2);
        cleanupOwned();
    }else if(mode=="signal-denied"){
        fault=Fault::SignalDenied;PublicJsonProcessClient client(helper);
        const PublicJsonRequest held{PublicFeed::GatePearlCandles,120,180};
        checkUnknown(client.get(held,cancel,limits));CHECK(spawns==1&&signals>0&&numericSignals==0);
        // The same pinned kernel identity, not a reused numeric PID, is the only cleanup target.
        CHECK(__real_syscall(SYS_pidfd_send_signal,ownedFd,SIGKILL,nullptr,0u)==0);
        fault=Fault::None;CHECK(client.get(request,cancel,limits).status==PublicHttpsStatus::Ok);CHECK(spawns==2);
    }else CHECK(false);
    CHECK(numericSpawns==0&&numericAcquisitions==0&&badIdentity==0);
    return finishTests("public JSON native ownership");
}
#else
int main(){std::puts("SKIP: public-helper atomic pidfd ownership fixtures require glibc >= 2.40");return 77;}
#endif
