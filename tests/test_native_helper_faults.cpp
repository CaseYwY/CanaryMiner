// Test-only Win32 call interposition around the verbatim production implementation.
// No injectable production API, DLL patching, process scanning, real stores, network or mining.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "product_test_util.h"
#include "product/public_json_process.h"
#include "product/local_task_process.h"
#include "product/duplex_task_process.h"
#include "product/worker_process.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <vector>
#include <fcntl.h>
#include <io.h>

namespace {
enum class Fault { None, Allocation, AllocationAttributes, WaitError, ExitError, Pending, Assign, Resume, CancelCreated, Cross };
enum class Kind { Pipe, File, Job, Process, Thread, Mapping };
struct Record {HANDLE handle;Kind kind;bool closed;};
Record records[512]{};unsigned recordCount=0,trackingFault=0,spawns=0,resumes=0,waitErrors=0,exitErrors=0;
struct Attribute {LPPROC_THREAD_ATTRIBUTE_LIST pointer;bool closed;};
Attribute attributes[64]{};unsigned attributeCount=0;
Fault fault=Fault::None;std::atomic<bool> failNew{false},cancel{false};
void (*crossCallback)()=nullptr;bool useHold=false;unsigned readyCalls=0;
bool liveProcess(HANDLE h){for(unsigned i=recordCount;i;--i)if(records[i-1].handle==h&&!records[i-1].closed)return records[i-1].kind==Kind::Process;return false;}
void track(HANDLE h,Kind k){if(!h||h==INVALID_HANDLE_VALUE)return;if(recordCount>=512){++trackingFault;return;}records[recordCount++]={h,k,false};}
unsigned active(){unsigned n=0;for(unsigned i=0;i<recordCount;++i)if(!records[i].closed)++n;return n;}
unsigned activeAttributes(){unsigned n=0;for(unsigned i=0;i<attributeCount;++i)if(!attributes[i].closed)++n;return n;}
BOOL WINAPI faultCreatePipe(PHANDLE read,PHANDLE write,LPSECURITY_ATTRIBUTES sa,DWORD bytes){
    const BOOL r=::CreatePipe(read,write,sa,bytes);if(r){track(*read,Kind::Pipe);track(*write,Kind::Pipe);if(fault==Fault::Allocation)failNew=true;}return r;
}
HANDLE WINAPI faultCreateFileW(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,DWORD disposition,DWORD flags,HANDLE parent){
    const auto h=::CreateFileW(name,access,share,sa,disposition,flags,parent);track(h,Kind::File);return h;
}
HANDLE WINAPI faultCreateJobObjectW(LPSECURITY_ATTRIBUTES sa,LPCWSTR name){const auto h=::CreateJobObjectW(sa,name);track(h,Kind::Job);return h;}
HANDLE WINAPI faultCreateFileMappingW(HANDLE file,LPSECURITY_ATTRIBUTES sa,DWORD protect,DWORD high,DWORD low,LPCWSTR name){
    const auto h=::CreateFileMappingW(file,sa,protect,high,low,name);track(h,Kind::Mapping);return h;
}
BOOL WINAPI faultDuplicateHandle(HANDLE sourceProcess,HANDLE source,HANDLE destinationProcess,LPHANDLE destination,DWORD access,BOOL inherit,DWORD options){
    const BOOL r=::DuplicateHandle(sourceProcess,source,destinationProcess,destination,access,inherit,options);if(r)track(*destination,Kind::Mapping);return r;
}
BOOL WINAPI faultCloseHandle(HANDLE h){
    const BOOL r=::CloseHandle(h);if(r){for(unsigned i=recordCount;i;--i)if(records[i-1].handle==h&&!records[i-1].closed){records[i-1].closed=true;break;}}
    else ++trackingFault;return r;
}
BOOL WINAPI faultInitializeAttributes(LPPROC_THREAD_ATTRIBUTE_LIST list,DWORD count,DWORD flags,PSIZE_T bytes){
    const BOOL r=::InitializeProcThreadAttributeList(list,count,flags,bytes);
    if(r&&list){if(attributeCount>=64)++trackingFault;else attributes[attributeCount++]={list,false};}return r;
}
BOOL WINAPI faultUpdateAttributes(LPPROC_THREAD_ATTRIBUTE_LIST list,DWORD flags,DWORD_PTR attribute,PVOID value,SIZE_T bytes,PVOID previous,PSIZE_T returned){
    const BOOL r=::UpdateProcThreadAttribute(list,flags,attribute,value,bytes,previous,returned);
    if(r&&fault==Fault::AllocationAttributes)failNew=true;return r;
}
VOID WINAPI faultDeleteAttributes(LPPROC_THREAD_ATTRIBUTE_LIST list){
    for(unsigned i=attributeCount;i;--i)if(attributes[i-1].pointer==list&&!attributes[i-1].closed){attributes[i-1].closed=true;break;}
    ::DeleteProcThreadAttributeList(list);
}
BOOL WINAPI faultCreateProcessW(LPCWSTR path,LPWSTR args,LPSECURITY_ATTRIBUTES process,LPSECURITY_ATTRIBUTES thread,BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,LPSTARTUPINFOW startup,LPPROCESS_INFORMATION info){
    const BOOL r=::CreateProcessW(path,args,process,thread,inherit,flags,environment,directory,startup,info);
    if(r){++spawns;track(info->hProcess,Kind::Process);track(info->hThread,Kind::Thread);if(fault==Fault::CancelCreated)cancel=true;}
    return r;
}
BOOL WINAPI faultAssignJob(HANDLE job,HANDLE process){if(fault==Fault::Assign){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::AssignProcessToJobObject(job,process);}
DWORD WINAPI faultResumeThread(HANDLE thread){++resumes;if(fault==Fault::Resume){SetLastError(ERROR_ACCESS_DENIED);return static_cast<DWORD>(-1);}return ::ResumeThread(thread);}
DWORD WINAPI faultWait(HANDLE h,DWORD ms){
    if(liveProcess(h)){
        if(fault==Fault::WaitError){++waitErrors;SetLastError(ERROR_INVALID_HANDLE);return WAIT_FAILED;}
        if(fault==Fault::Pending)return WAIT_TIMEOUT;
    }
    const auto r=::WaitForSingleObject(h,ms);
    if(liveProcess(h)&&r==WAIT_OBJECT_0&&fault==Fault::Cross&&crossCallback){auto callback=crossCallback;crossCallback=nullptr;callback();}
    return r;
}
BOOL WINAPI faultExitCode(HANDLE h,LPDWORD code){if(liveProcess(h)&&fault==Fault::ExitError){++exitErrors;SetLastError(ERROR_INVALID_HANDLE);return FALSE;}return ::GetExitCodeProcess(h,code);}
}
bool canaryNativeAllocationFault(){return failNew.exchange(false);}

#define CreatePipe faultCreatePipe
#define CreateFileW faultCreateFileW
#define CreateJobObjectW faultCreateJobObjectW
#define CreateFileMappingW faultCreateFileMappingW
#define DuplicateHandle faultDuplicateHandle
#define CloseHandle faultCloseHandle
#define InitializeProcThreadAttributeList faultInitializeAttributes
#define UpdateProcThreadAttribute faultUpdateAttributes
#define DeleteProcThreadAttributeList faultDeleteAttributes
#define CreateProcessW faultCreateProcessW
#define AssignProcessToJobObject faultAssignJob
#define ResumeThread faultResumeThread
#define WaitForSingleObject faultWait
#define GetExitCodeProcess faultExitCode
#if defined(OM_NATIVE_FAULT_DUPLEX)
#include "../src/product/duplex_task_process.cpp"
#elif defined(OM_NATIVE_FAULT_WORKER)
#include "../src/product/worker_process.cpp"
#elif defined(OM_NATIVE_FAULT_LOCAL)
#include "../src/product/local_task_process.cpp"
#else
#include "../src/product/public_json_process.cpp"
#endif
#undef CreatePipe
#undef CreateFileW
#undef CreateJobObjectW
#undef CreateFileMappingW
#undef DuplicateHandle
#undef CloseHandle
#undef InitializeProcThreadAttributeList
#undef UpdateProcThreadAttribute
#undef DeleteProcThreadAttributeList
#undef CreateProcessW
#undef AssignProcessToJobObject
#undef ResumeThread
#undef WaitForSingleObject
#undef GetExitCodeProcess

namespace {
std::filesystem::path helper;
#if defined(OM_NATIVE_FAULT_DUPLEX) || defined(OM_NATIVE_FAULT_WORKER)
// The stream APIs expose native state/proof, not LocalTaskResult. This test-only bridge keeps the same fault assertions.
enum class Status {Ok,Unavailable,Cancelled,Unreaped};
struct Result {Status status=Status::Unavailable;bool childStarted=false;std::string response;};
class Client {
    std::filesystem::path helper_;
public:
    explicit Client(std::filesystem::path path):helper_(std::move(path)){}
    Result run(){
        Result r;const auto before=spawns;bool started=false,exited=false,unknown=false;
#if defined(OM_NATIVE_FAULT_DUPLEX)
        om::DuplexTaskProcess p;started=p.start(helper_,cancel);
#else
        om::WorkerControlProcess p;const auto now=om::workerControlNowMs();
        om::WorkerCommand initial{{"11111111111111111111111111111111","22222222222222222222222222222222",1,1,0},1,now,now+3000,om::WorkerDirective::Pause};
        started=p.start(helper_,{},initial,cancel);
#endif
        const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(600);
        if(started)do{
            const auto v=p.poll();
#if defined(OM_NATIVE_FAULT_DUPLEX)
            exited=v.state==om::DuplexTaskState::Exited;unknown=v.state==om::DuplexTaskState::Unreaped;
#else
            exited=p.reaped();unknown=v.state==om::WorkerProcessState::Unreaped;
#endif
            if(exited||unknown||cancel.load())break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }while(std::chrono::steady_clock::now()<until);
        if(!p.terminate(250))unknown=true;
        if(om::nativeProcessUnresolved())unknown=true;
        r.childStarted=spawns!=before;r.status=unknown?Status::Unreaped:cancel.load()?Status::Cancelled:started&&exited?Status::Ok:Status::Unavailable;
        return r;
    }
};
Result run(Client &client){return client.run();}
void empty(const Result &r,bool started){CHECK(r.response.empty()&&r.childStarted==started);}
#elif defined(OM_NATIVE_FAULT_LOCAL)
using Client=om::LocalTaskProcess;using Result=om::LocalTaskResult;using Status=om::LocalTaskStatus;
Result run(Client &client){return useHold?client.hold("SYNTHETIC\n",cancel,600,[](const std::string &){++readyCalls;return true;}):client.run("SYNTHETIC\n",cancel,600);}
void empty(const Result &r,bool started){CHECK(r.response.empty()&&r.childStarted==started);}
#else
using Client=om::PublicJsonProcessClient;using Result=om::PublicJsonResponse;using Status=om::PublicHttpsStatus;
Result run(Client &client){return client.get({om::PublicFeed::GatePearlCandles,60,120},cancel,{300,600,8192,2048});}
void empty(const Result &r,bool){CHECK(r.body.empty()&&!r.httpStatus&&!r.observedUtc&&!r.retryAfterSeconds);}
#endif
void unknown(const Result &r,bool started){CHECK(r.status==Status::Unreaped);empty(r,started);}
void cross(){fault=Fault::WaitError;{Client other(helper);unknown(run(other),true);}fault=Fault::None;}
void settleOwned(){
    for(unsigned i=0;i<recordCount;++i){const auto &r=records[i];if(r.closed||r.kind!=Kind::Process)continue;
        const auto before=::WaitForSingleObject(r.handle,0);
        if(before!=WAIT_OBJECT_0)CHECK(::TerminateProcess(r.handle,1)!=0);
        CHECK(::WaitForSingleObject(r.handle,2000)==WAIT_OBJECT_0);
    }
}
int child(int argc,char **argv){
    _setmode(_fileno(stdout),_O_BINARY);
#if defined(OM_NATIVE_FAULT_DUPLEX) || defined(OM_NATIVE_FAULT_WORKER)
    (void)argc;(void)argv;return 0; // no protocol/driver/device setup; lifecycle only
#elif defined(OM_NATIVE_FAULT_LOCAL)
    std::string input;if(!om::readLocalTaskInput(argc,argv,&input)||input!="SYNTHETIC\n")return 2;
    return om::writeLocalTaskOutput("SYNTHETIC\n")?0:2;
#else
    (void)argc;(void)argv;om::PublicJsonResponse r;r.status=Status::Ok;r.httpStatus=200;r.observedUtc=1;r.body="{\"fixture\":true}";
    std::string wire;if(!om::encodePublicJsonResponse(r,&wire))return 2;
    return std::fwrite(wire.data(),1,wire.size(),stdout)==wire.size()&&std::fflush(stdout)==0?0:2;
#endif
}
}
int main(int argc,char **argv){
#if defined(OM_NATIVE_FAULT_DUPLEX) || defined(OM_NATIVE_FAULT_WORKER)
    if(argc==5)return child(argc,argv);
#elif defined(OM_NATIVE_FAULT_LOCAL)
    if(argc==4&&!std::strcmp(argv[1],"--local-task-v1"))return child(argc,argv);
#else
    if(argc==8)return child(argc,argv);
#endif
    CHECK(argc==2);if(argc!=2)return finishTests("native helper faults");
    wchar_t path[32768]{};const auto length=GetModuleFileNameW(nullptr,path,32768);CHECK(length&&length<32768);helper=std::filesystem::path(std::wstring(path,length));
    const std::string mode=argv[1];
    if(mode=="allocation"||mode=="allocation-attributes"){
        fault=mode=="allocation"?Fault::Allocation:Fault::AllocationAttributes;
        for(unsigned i=0;i<3;++i){
            {Client client(helper);const auto r=run(client);CHECK(r.status==Status::Unavailable);empty(r,false);}
            CHECK(!failNew.load()&&!spawns);CHECK(active()==0);CHECK(activeAttributes()==0);
            // Old-code red runs close only the leaked handles created by this fixture, never another application's objects.
            for(unsigned n=0;n<recordCount;++n)if(!records[n].closed){::CloseHandle(records[n].handle);records[n].closed=true;}
        }
    }else if(mode=="wait-error"||mode=="exit-error"||mode=="hold-error"){
        fault=mode=="exit-error"?Fault::ExitError:Fault::WaitError;useHold=mode=="hold-error";
        {Client client(helper);unknown(run(client),true);unknown(run(client),false);}
        Client next(helper);unknown(run(next),false);CHECK(spawns==1&&readyCalls==0);CHECK(waitErrors+exitErrors==1);settleOwned();
    }else if(mode=="pending"){
        fault=Fault::Pending;{Client client(helper);unknown(run(client),true);}
        {Client next(helper);unknown(run(next),false);CHECK(spawns==1);}
        fault=Fault::None;{Client after(helper);CHECK(run(after).status==Status::Ok);}CHECK(spawns==2&&active()==0);
    }else if(mode=="assign-denied"||mode=="resume-denied"||mode=="cancel-created"){
        fault=mode=="assign-denied"?Fault::Assign:mode=="resume-denied"?Fault::Resume:Fault::CancelCreated;
        {Client client(helper);const auto r=run(client);CHECK(r.status==(mode=="cancel-created"?Status::Cancelled:Status::Unavailable));empty(r,true);}
        CHECK(spawns==1&&active()==0&&activeAttributes()==0);CHECK(resumes==(mode=="resume-denied"?1u:0u));
        fault=Fault::None;cancel=false;{Client client(helper);CHECK(run(client).status==Status::Ok);}CHECK(spawns==2&&active()==0);
    }else if(mode=="cross-client"){
        fault=Fault::Cross;crossCallback=cross;{Client client(helper);unknown(run(client),true);}
        Client next(helper);unknown(run(next),false);CHECK(spawns==2&&waitErrors==1);settleOwned();
    }else CHECK(false);
    CHECK(trackingFault==0);
    return finishTests("native helper faults (owned Win32 objects only)");
}
