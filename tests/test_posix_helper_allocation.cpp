// Test-only POSIX interposition. Arm a single C++ allocation failure after both native setup objects exist.
// No helper is ever spawned; only descriptors created by this test can be cleaned up here.
#include "product_test_util.h"
#include "product/public_json_process.h"
#include "product/local_task_process.h"
#include "product/duplex_task_process.h"
#include "product/worker_process.h"
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstdarg>
#include <filesystem>
#include <fcntl.h>
#include <new>
#include <spawn.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(OM_NATIVE_FAULT_LEGACY)
#include "../src/core/platform.h"
#endif
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2,40)
#define OM_TEST_POSIX_ALLOCATION 1
#endif
#endif
#if defined(OM_TEST_POSIX_ALLOCATION)
namespace {
struct Descriptor {int fd;bool closed;};Descriptor descriptors[64]{};
unsigned descriptorCount=0,actions=0,attributes=0,spawnAttempts=0,trackingFault=0;
std::atomic<bool> failNew{false};
void track(int fd){if(descriptorCount>=64){++trackingFault;return;}descriptors[descriptorCount++]={fd,false};}
unsigned active(){unsigned n=0;for(unsigned i=0;i<descriptorCount;++i)if(!descriptors[i].closed)++n;return n;}
[[maybe_unused]] int faultPipe2(int *pair,int flags){const int r=::pipe2(pair,flags);if(!r){track(pair[0]);track(pair[1]);}return r;}
[[maybe_unused]] int faultSocketpair(int domain,int type,int protocol,int *pair){const int r=::socketpair(domain,type,protocol,pair);if(!r){track(pair[0]);track(pair[1]);}return r;}
int faultClose(int fd){const int r=::close(fd);if(!r){for(unsigned i=descriptorCount;i;--i)if(descriptors[i-1].fd==fd&&!descriptors[i-1].closed){descriptors[i-1].closed=true;break;}}else ++trackingFault;return r;}
int faultFcntl(int fd,int command,...){
    if(command==F_GETFL)return ::fcntl(fd,command);
    va_list args;va_start(args,command);const int argument=va_arg(args,int);va_end(args);
    const int r=::fcntl(fd,command,argument);if(command==F_DUPFD_CLOEXEC&&r>=0)track(r);return r;
}
int faultActionsInit(posix_spawn_file_actions_t *p){const int r=::posix_spawn_file_actions_init(p);if(!r)++actions;return r;}
int faultActionsDestroy(posix_spawn_file_actions_t *p){const int r=::posix_spawn_file_actions_destroy(p);if(!r)--actions;return r;}
int faultAttributesInit(posix_spawnattr_t *p){const int r=::posix_spawnattr_init(p);if(!r){++attributes;failNew=true;}return r;}
int faultAttributesDestroy(posix_spawnattr_t *p){const int r=::posix_spawnattr_destroy(p);if(!r)--attributes;return r;}
int refuseSpawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]){++spawnAttempts;return ENOSYS;}
}
bool canaryNativeAllocationFault(){return failNew.exchange(false);}
#define pipe2 faultPipe2
#define socketpair faultSocketpair
#define close faultClose
#define fcntl faultFcntl
#define posix_spawn_file_actions_init faultActionsInit
#define posix_spawn_file_actions_destroy faultActionsDestroy
#define posix_spawnattr_init faultAttributesInit
#define posix_spawnattr_destroy faultAttributesDestroy
#define pidfd_spawn refuseSpawn
#if defined(OM_NATIVE_FAULT_DUPLEX)
#include "../src/product/duplex_task_process.cpp"
#elif defined(OM_NATIVE_FAULT_WORKER)
#include "../src/product/worker_process.cpp"
#elif defined(OM_NATIVE_FAULT_LOCAL)
#include "../src/product/local_task_process.cpp"
#elif defined(OM_NATIVE_FAULT_LEGACY)
#include "../src/product/legacy_child_process.h"
#else
#include "../src/product/public_json_process.cpp"
#endif
#undef pipe2
#undef socketpair
#undef close
#undef fcntl
#undef posix_spawn_file_actions_init
#undef posix_spawn_file_actions_destroy
#undef posix_spawnattr_init
#undef posix_spawnattr_destroy
#undef pidfd_spawn
int main(int argc,char **argv){
    (void)argc;const auto helper=std::filesystem::absolute(argv[0]);std::atomic<bool> cancel{false};
    for(unsigned repetition=0;repetition<3;++repetition){
#if defined(OM_NATIVE_FAULT_DUPLEX)
        {om::DuplexTaskProcess client;CHECK(!client.start(helper,cancel));}
#elif defined(OM_NATIVE_FAULT_WORKER)
        {om::WorkerControlProcess client;const auto now=om::workerControlNowMs();
            om::WorkerCommand initial{{"11111111111111111111111111111111","22222222222222222222222222222222",1,1,0},1,now,now+3000,om::WorkerDirective::Pause};
            CHECK(!client.start(helper,{},initial,cancel));}
#elif defined(OM_NATIVE_FAULT_LOCAL)
        {om::LocalTaskProcess client(helper);const auto r=client.run("SYNTHETIC\n",cancel,600);CHECK(r.status==om::LocalTaskStatus::Unavailable&&!r.childStarted&&r.response.empty());}
#elif defined(OM_NATIVE_FAULT_LEGACY)
        {om::ChildProcess client;std::string error;CHECK(!client.start(helper.string(),{},[](const std::string &){},&error)&&!client.started());}
#else
        {om::PublicJsonProcessClient client(helper);const auto r=client.get({om::PublicFeed::GatePearlCandles,60,120},cancel,{300,600,8192,2048});
            CHECK(r.status==om::PublicHttpsStatus::Unavailable&&r.body.empty()&&!r.observedUtc&&!r.httpStatus&&!r.retryAfterSeconds);}
#endif
        CHECK(!failNew.load()&&spawnAttempts==0);CHECK(active()==0);CHECK(actions==0&&attributes==0);
        failNew=false;
        for(unsigned i=0;i<descriptorCount;++i)if(!descriptors[i].closed){::close(descriptors[i].fd);descriptors[i].closed=true;}
    }
    CHECK(trackingFault==0);
    return finishTests("POSIX helper allocation/setup exception ownership (no child spawn)");
}
#else
bool canaryNativeAllocationFault(){return false;}
int main(){std::puts("SKIP: POSIX allocation fixture requires glibc >= 2.40");return 77;}
#endif
