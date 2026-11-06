// Test-only POSIX interposition. Arm a single C++ allocation failure after both native setup objects exist.
// No helper is ever spawned; only descriptors created by this test can be cleaned up here.
#include "product_test_util.h"
#include "product/public_json_process.h"
#include "product/local_task_process.h"
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <new>
#include <spawn.h>
#include <sys/socket.h>
#include <unistd.h>
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
int faultPipe2(int *pair,int flags){const int r=::pipe2(pair,flags);if(!r){track(pair[0]);track(pair[1]);}return r;}
[[maybe_unused]] int faultSocketpair(int domain,int type,int protocol,int *pair){const int r=::socketpair(domain,type,protocol,pair);if(!r){track(pair[0]);track(pair[1]);}return r;}
int faultClose(int fd){const int r=::close(fd);if(!r){for(unsigned i=descriptorCount;i;--i)if(descriptors[i-1].fd==fd&&!descriptors[i-1].closed){descriptors[i-1].closed=true;break;}}else ++trackingFault;return r;}
int faultActionsInit(posix_spawn_file_actions_t *p){const int r=::posix_spawn_file_actions_init(p);if(!r)++actions;return r;}
int faultActionsDestroy(posix_spawn_file_actions_t *p){const int r=::posix_spawn_file_actions_destroy(p);if(!r)--actions;return r;}
int faultAttributesInit(posix_spawnattr_t *p){const int r=::posix_spawnattr_init(p);if(!r){++attributes;failNew=true;}return r;}
int faultAttributesDestroy(posix_spawnattr_t *p){const int r=::posix_spawnattr_destroy(p);if(!r)--attributes;return r;}
int refuseSpawn(int *,const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const[],char *const[]){++spawnAttempts;return ENOSYS;}
}
void *operator new(std::size_t n){if(failNew.exchange(false))throw std::bad_alloc();if(void *p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void *operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void *p)noexcept{std::free(p);}
void operator delete[](void *p)noexcept{std::free(p);}
void operator delete(void *p,std::size_t)noexcept{std::free(p);}
void operator delete[](void *p,std::size_t)noexcept{std::free(p);}
#define pipe2 faultPipe2
#define socketpair faultSocketpair
#define close faultClose
#define posix_spawn_file_actions_init faultActionsInit
#define posix_spawn_file_actions_destroy faultActionsDestroy
#define posix_spawnattr_init faultAttributesInit
#define posix_spawnattr_destroy faultAttributesDestroy
#define pidfd_spawn refuseSpawn
#if defined(OM_NATIVE_FAULT_LOCAL)
#include "../src/product/local_task_process.cpp"
#else
#include "../src/product/public_json_process.cpp"
#endif
#undef pipe2
#undef socketpair
#undef close
#undef posix_spawn_file_actions_init
#undef posix_spawn_file_actions_destroy
#undef posix_spawnattr_init
#undef posix_spawnattr_destroy
#undef pidfd_spawn
int main(int argc,char **argv){
    (void)argc;const auto helper=std::filesystem::absolute(argv[0]);std::atomic<bool> cancel{false};
    for(unsigned repetition=0;repetition<3;++repetition){
#if defined(OM_NATIVE_FAULT_LOCAL)
        {om::LocalTaskProcess client(helper);const auto r=client.run("SYNTHETIC\n",cancel,600);CHECK(r.status==om::LocalTaskStatus::Unavailable&&!r.childStarted&&r.response.empty());}
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
int main(){std::puts("SKIP: POSIX allocation fixture requires glibc >= 2.40");return 77;}
#endif
