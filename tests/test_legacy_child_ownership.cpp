// Native-call faults around actual self-owned CPU children. Numeric signals are always suppressed.
// No storage, network, process discovery, GPU, game or installed miner.
#include "product/legacy_child_process.h"
#include "product_test_util.h"
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstring>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <pthread.h>
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_TEST_LEGACY_PIDFD 1
#endif
#endif
#if defined(OM_TEST_LEGACY_PIDFD)
extern "C" pid_t __real_fork();
extern "C" pid_t __real_waitpid(pid_t,int *,int);
extern "C" int __real_waitid(idtype_t,id_t,siginfo_t *,int);
extern "C" int __real_pidfd_spawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]);
extern "C" int __real_sigaction(int,const struct sigaction *,struct sigaction *);
extern "C" long __real_syscall(long,...);
namespace {
enum class Fault { None, Stolen, Race, Error, Pending, Policy, Unavailable };
Fault fault=Fault::None;
pid_t owned=-1;int ownedFd=-1;unsigned spawns=0,numericSpawns=0,numericSignals=0,lateProbes=0;
bool consumed=false,lost=false;
std::atomic<bool> cancelled{false};bool cancelCreated=false;
std::atomic<bool> failReader{false};unsigned failedReaders=0;
void consume(){
    if(consumed)return;
    if(ownedFd>=0){siginfo_t info{};CHECK(__real_waitid(P_PIDFD,static_cast<id_t>(ownedFd),&info,WEXITED)==0);}
    else if(owned>0){int status=0;CHECK(__real_waitpid(owned,&status,0)==owned);}
    consumed=true;
}
}
extern "C" int pthread_create(pthread_t *thread,const pthread_attr_t *attributes,void *(*function)(void *),void *argument)noexcept{
    if(failReader.exchange(false)){++failedReaders;return EAGAIN;}
    using Create=int (*)(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
    static const auto real=reinterpret_cast<Create>(dlsym(RTLD_NEXT,"pthread_create"));
    return real?real(thread,attributes,function,argument):EAGAIN;
}
extern "C" pid_t __wrap_fork(){const auto pid=__real_fork();if(pid>0){owned=pid;ownedFd=-1;consumed=false;++spawns;++numericSpawns;}return pid;}
extern "C" int __wrap_pidfd_spawn(int *fd,const char *path,const posix_spawn_file_actions_t *a,const posix_spawnattr_t *s,char *const argv[],char *const env[]){
    if(fault==Fault::Unavailable)return ENOSYS;
    const int result=__real_pidfd_spawn(fd,path,a,s,argv,env);
    if(!result){ownedFd=*fd;owned=-1;consumed=false;++spawns;if(cancelCreated)cancelled=true;}return result;
}
extern "C" pid_t __wrap_waitpid(pid_t pid,int *status,int flags){
    if(lost){++lateProbes;errno=ECHILD;return -1;}
    if(fault==Fault::Error){lost=true;errno=EINVAL;return -1;}
    if(fault==Fault::Pending)return 0;
    const auto result=__real_waitpid(pid,status,flags);
    if(fault==Fault::Race&&!consumed&&!result)consume();
    if(result<0&&errno==ECHILD)lost=true;
    return result;
}
extern "C" int __wrap_waitid(idtype_t type,id_t id,siginfo_t *info,int flags){
    if(lost){++lateProbes;errno=ECHILD;return -1;}
    if(fault==Fault::Error){lost=true;errno=EINVAL;return -1;}
    if(fault==Fault::Pending){std::memset(info,0,sizeof(*info));return 0;}
    const int result=__real_waitid(type,id,info,flags);
    if(fault==Fault::Race&&!consumed&&!result&&!info->si_pid)consume();
    if(result<0&&errno==ECHILD)lost=true;
    return result;
}
extern "C" int __wrap_sigaction(int signal,const struct sigaction *action,struct sigaction *previous){
    const int result=__real_sigaction(signal,action,previous);
    if(!result&&signal==SIGCHLD&&!action&&previous&&fault==Fault::Policy)previous->sa_handler=SIG_IGN;
    return result;
}
extern "C" int __wrap_kill(pid_t,int){++numericSignals;errno=ESRCH;return -1;}
extern "C" long __wrap_syscall(long number,...){
    va_list args;va_start(args,number);
    if(number==SYS_pidfd_send_signal){const int fd=va_arg(args,int),signal=va_arg(args,int);auto *info=va_arg(args,siginfo_t *);const auto flags=va_arg(args,unsigned);va_end(args);
        CHECK(fd==ownedFd);if(fd!=ownedFd){errno=EBADF;return -1;}return __real_syscall(number,fd,signal,info,flags);}
    va_end(args);errno=ENOSYS;return -1;
}
int main(int argc,char **argv){
    if(argc==2&&std::string(argv[1])=="--synthetic-legacy-child"){std::this_thread::sleep_for(std::chrono::milliseconds(60));return 0;}
    if(argc==2&&std::string(argv[1])=="--synthetic-legacy-reader"){std::this_thread::sleep_for(std::chrono::milliseconds(1500));return 0;}
    CHECK(argc==2);if(argc!=2)return finishTests("legacy native ownership");
    const auto exe=std::filesystem::absolute(argv[0]).string();const std::string mode=argv[1];std::string error;int code=-1;
    const auto launch=[&](om::ChildProcess &child){return child.start(exe,{mode=="reader-cancel"?"--synthetic-legacy-reader":"--synthetic-legacy-child"},[](const std::string &){},&error,[]{return cancelled.load();});};
    if(mode=="stolen"||mode=="race"||mode=="error"){
        fault=mode=="stolen"?Fault::Stolen:mode=="race"?Fault::Race:Fault::Error;
        {om::ChildProcess child;CHECK(launch(child));if(mode=="stolen")consume();
            CHECK(!child.exited(&code));child.kill();CHECK(!child.exited(&code)&&!child.finished());
            CHECK(om::nativeProcessUnresolved());CHECK(numericSignals==0);
            om::ChildProcess other;CHECK(!launch(other));CHECK(spawns==1);}
        om::ChildProcess after;CHECK(!launch(after));CHECK(spawns==1&&lateProbes==0);consume();
    }else if(mode=="pending"){
        fault=Fault::Pending;{om::ChildProcess child;CHECK(launch(child));child.kill();
            CHECK(!child.exited(&code)&&om::nativeProcessUnresolved());om::ChildProcess other;CHECK(!launch(other));}
        fault=Fault::None;om::ChildProcess after;CHECK(launch(after));CHECK(after.waitFor(3000,&code)&&code==0);CHECK(spawns==2);
    }else if(mode=="foreign"){
        om::NativeProcessAdmission other;CHECK(other.begin());other.markUnresolved();
        {om::ChildProcess child;CHECK(!launch(child));CHECK(spawns==0);}other.settled();
        om::ChildProcess after;CHECK(launch(after));CHECK(after.waitFor(3000,&code)&&code==0);
    }else if(mode=="policy"||mode=="unavailable"){
        fault=mode=="policy"?Fault::Policy:Fault::Unavailable;
        {om::ChildProcess child;CHECK(!launch(child));CHECK(spawns==0&&!om::nativeProcessUnresolved());}
        fault=Fault::None;om::ChildProcess after;CHECK(launch(after));CHECK(after.waitFor(3000,&code)&&code==0);
    }else if(mode=="reader-cancel"){
        const auto began=std::chrono::steady_clock::now();fault=Fault::Error;
        {om::ChildProcess child;CHECK(launch(child));CHECK(!child.exited(&code));child.joinReader();}
        CHECK(std::chrono::steady_clock::now()-began<std::chrono::seconds(1));
        CHECK(om::nativeProcessUnresolved()&&lateProbes==0&&spawns==1);consume();
    }else if(mode=="thread-failure"){
        failReader=true;{om::ChildProcess child;CHECK(!launch(child));CHECK(child.started()&&child.finished());
            CHECK(failedReaders==1&&spawns==1&&!om::nativeProcessUnresolved());}
        om::ChildProcess after;CHECK(launch(after));CHECK(after.waitFor(3000,&code)&&code==0&&spawns==2);
    }else if(mode=="cancel-before"||mode=="cancel-created"){
        cancelCreated=mode=="cancel-created";cancelled=!cancelCreated;
        {om::ChildProcess child;CHECK(!launch(child));CHECK(child.started()==cancelCreated);
            if(cancelCreated)CHECK(child.finished()&&child.exited(&code));
            CHECK(spawns==(cancelCreated?1u:0u)&&!om::nativeProcessUnresolved());}
        cancelCreated=false;cancelled=false;om::ChildProcess after;CHECK(launch(after));CHECK(after.waitFor(3000,&code)&&code==0);
    }else if(mode=="exit"){
        {om::ChildProcess child;CHECK(launch(child));CHECK(child.waitFor(3000,&code)&&code==0);child.joinReader();
            CHECK(child.started()&&child.finished()&&!launch(child));}
        om::ChildProcess after;CHECK(launch(after));CHECK(after.waitFor(3000,&code)&&code==0);CHECK(spawns==2&&!om::nativeProcessUnresolved());
    }else CHECK(false);
    CHECK(numericSpawns==0&&numericSignals==0);
    return finishTests("legacy native identity/shared retained uncertainty (owned CPU fixture only)");
}
#else
int main(){std::puts("SKIP: legacy native ownership fixture requires glibc >= 2.40");return 77;}
#endif
