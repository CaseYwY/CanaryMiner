// Headless, oracle-owned ConPTY only. Does not attach to, activate or write to the user's console.
#define _WIN32_WINNT 0x0A00
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace {
struct Handle {HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct Attributes {
    std::vector<unsigned char> bytes;LPPROC_THREAD_ATTRIBUTE_LIST value=nullptr;
    bool init(){SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);bytes.resize(size);value=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(bytes.data());if(!InitializeProcThreadAttributeList(value,1,0,&size)){value=nullptr;return false;}return true;}
    ~Attributes(){if(value)DeleteProcThreadAttributeList(value);}
};
std::wstring self(){wchar_t path[32768]{};const DWORD n=GetModuleFileNameW(nullptr,path,32768);return n&&n<32768?std::wstring(path,n):std::wstring();}
bool spawn(const std::wstring &exe,const std::wstring &args,STARTUPINFOEXW &startup,BOOL inherit,DWORD flags,Handle &process,Handle &thread){
    const auto command=L"\""+exe+L"\" "+args;std::vector<wchar_t> words(command.begin(),command.end());words.push_back(0);PROCESS_INFORMATION info{};
    if(!CreateProcessW(exe.c_str(),words.data(),nullptr,nullptr,inherit,EXTENDED_STARTUPINFO_PRESENT|flags,nullptr,nullptr,&startup.StartupInfo,&info))return false;
    process.value=info.hProcess;thread.value=info.hThread;return true;
}
BOOL WINAPI ignoreHostControl(DWORD){return TRUE;}
int host(const std::wstring &binary,const std::wstring &mode,const std::wstring &path,const std::wstring &receipt){
    SetConsoleCtrlHandler(ignoreHostControl,TRUE); // ignore only in this owned host; child has its own handler list
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};Handle input,output;
    input.value=CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    output.value=CreateFileW(L"CONOUT$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    if(input.value==INVALID_HANDLE_VALUE||output.value==INVALID_HANDLE_VALUE)return 20;
    Attributes attributes;if(!attributes.init())return 21;HANDLE handles[]={input.value,output.value};
    if(!UpdateProcThreadAttribute(attributes.value,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr))return 22;
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=input.value;startup.StartupInfo.hStdOutput=startup.StartupInfo.hStdError=output.value;startup.lpAttributeList=attributes.value;
    // Only the inert CLI/fake-supervisor fixture understands this test-only receipt. Helpers have minimal environments.
    if(!SetEnvironmentVariableW(L"CANARY_CONSOLE_FIXTURE_COMPLETION_V1",(receipt+L".complete").c_str()))return 27;
    const auto args=mode==L"idle"?L"settings --config \""+path+L"\"":mode+L" \""+path+L"\"";
    Handle process,thread;if(!spawn(binary,args,startup,TRUE,0,process,thread))return 23;
    if(WaitForSingleObject(process.value,15000)!=WAIT_OBJECT_0){TerminateProcess(process.value,94);WaitForSingleObject(process.value,2000);return 24;}
    DWORD code=99;if(!GetExitCodeProcess(process.value,&code))return 25;
    // Prove the actual program's exit before this console host closes CONIN$/CONOUT$.
    // Closing those host handles can itself wait for ConPTY output: do not mistake that for a product hang.
    Handle proof;proof.value=CreateFileW(receipt.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    DWORD count=0;if(proof.value==INVALID_HANDLE_VALUE||!WriteFile(proof.value,&code,sizeof(code),&count,nullptr)||count!=sizeof(code))return 26;
    return static_cast<int>(code);
}
bool probe(const std::wstring &binary,const std::wstring &mode,const std::wstring &path,bool blocked,bool control){
    Handle inputRead,inputWrite,outputRead,outputWrite;
    if(!CreatePipe(&inputRead.value,&inputWrite.value,nullptr,8192)||!CreatePipe(&outputRead.value,&outputWrite.value,nullptr,8192))return false;
    HPCON console=nullptr;if(FAILED(CreatePseudoConsole({100,30},inputRead.value,outputWrite.value,0,&console)))return false;
    Attributes attributes;bool ready=attributes.init()&&UpdateProcThreadAttribute(attributes.value,0,PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,console,sizeof(console),nullptr,nullptr);
    Handle job;job.value=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION policy{};
    policy.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS;policy.BasicLimitInformation.ActiveProcessLimit=8;
    ready=ready&&job.value&&SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&policy,sizeof(policy));
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes.value;Handle process,thread;
    const auto receipt=std::filesystem::path(binary).parent_path()/(blocked?"retained-program-exit":"healthy-program-exit");
    const bool created=ready&&spawn(self(),L"--host \""+binary+L"\" "+mode+L" \""+path+L"\" \""+receipt.wstring()+L"\"",startup,FALSE,CREATE_SUSPENDED,process,thread);
    CloseHandle(inputRead.value);inputRead.value=nullptr;CloseHandle(outputWrite.value);outputWrite.value=nullptr;
    std::atomic<bool> pause{false};std::mutex mutex;std::string transcript;
    std::thread reader([&]{char bytes[4096];DWORD count=0;for(;;){while(pause.load())Sleep(2);if(!ReadFile(outputRead.value,bytes,sizeof(bytes),&count,nullptr)||!count)break;std::lock_guard<std::mutex> lock(mutex);if(transcript.size()+count<1048576)transcript.append(bytes,count);}});
    const auto has=[&](const char *text){std::lock_guard<std::mutex> lock(mutex);return transcript.find(text)!=std::string::npos;};
    bool screen=false,stopped=false,settled=false;DWORD code=99;
    if(created&&AssignProcessToJobObject(job.value,process.value)&&ResumeThread(thread.value)!=static_cast<DWORD>(-1)){
        const auto deadline=GetTickCount64()+10000;
        while(GetTickCount64()<deadline&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT){
            screen=mode==L"plain-delayed"?std::filesystem::exists(std::filesystem::path(path)/"worker-starts"):has(mode==L"idle"?"CanaryMiner / Settings":"[S]");
            if(screen)break;Sleep(5);
        }
        if(screen&&mode==L"dashboard"){
            DWORD count=0;const char key='s';screen=WriteFile(inputWrite.value,&key,1,&count,nullptr)&&count==1;
            while(screen&&!has("CanaryMiner / Settings")&&GetTickCount64()<deadline&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT)Sleep(5);
            screen=screen&&has("CanaryMiner / Settings");
        }
        if(screen){
            pause=blocked;
            if(mode==L"plain-delayed")std::ofstream(std::filesystem::path(path)/"go-output")<<"go\n";
            if(blocked)Sleep(1500);
            else if(mode==L"plain-delayed")while(!has("console-line-15")&&GetTickCount64()<deadline&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT)Sleep(5);
            DWORD count=0;const char key=control?3:'q';
            if(WriteFile(inputWrite.value,&key,1,&count,nullptr)&&count==1){
                const auto stopDeadline=GetTickCount64()+2000;
                while(GetTickCount64()<stopDeadline){DWORD completion[2]{};std::ifstream proof(std::filesystem::path(receipt.wstring()+L".complete"),std::ios::binary);proof.read(reinterpret_cast<char *>(completion),sizeof(completion));
                    if(proof.gcount()==sizeof(completion)&&completion[0]==0x434d494f&&completion[1]){
                        struct Processes {DWORD assigned=0,count=0;ULONG_PTR ids[8]{};} active;
                        if(QueryInformationJobObject(job.value,JobObjectBasicProcessIdList,&active,sizeof(active),nullptr)&&active.count<=8){
                            bool onlyHostAndCompletedProgram=true;
                            for(DWORD i=0;i<active.count;++i)if(active.ids[i]!=GetProcessId(process.value)&&active.ids[i]!=completion[1])onlyHostAndCompletedProgram=false;
                            if(onlyHostAndCompletedProgram){stopped=true;break;}
                        }
                    }Sleep(5);}
            }
        }
        pause=false;
        if(WaitForSingleObject(process.value,5000)==WAIT_OBJECT_0){DWORD hostCode=99;if(GetExitCodeProcess(process.value,&hostCode))code=hostCode;}
        else{TerminateJobObject(job.value,95);WaitForSingleObject(process.value,2000);}
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        settled=QueryInformationJobObject(job.value,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr)&&accounting.ActiveProcesses==0;
        if(!settled)TerminateJobObject(job.value,96);
    }else if(created){TerminateProcess(process.value,97);WaitForSingleObject(process.value,2000);}
    // Drain concurrently during ClosePseudoConsole; older Windows may otherwise block its final output flush.
    pause=false;ClosePseudoConsole(console);reader.join();
    const bool clean=!has("FORBIDDEN_DEVICE_SETUP")&&!has("synthetic private value");
    const bool footer=blocked||has(mode==L"idle"?"Settings closed.":"stopped: 1 GPU(s)");
    std::printf("owned ConPTY %s/%s: ready=%d; application-return-and-child-retirement-before-release=%d; OS-exit-after-drain=%lu; job-settled=%d; clean=%d; footer=%d\n",blocked?"retained":"healthy",control?"Ctrl+C":"Q",screen,stopped,code,settled,clean,footer);
    // An unresponsive console can withhold Q/Ctrl+C itself. Never fabricate receipt of that intent:
    // bounded terminal loss is configuration refusal (idle) or a latched fault (supervisor), not clean success.
    const bool expectedExit=code==0||(blocked&&code==(mode==L"idle"?3u:8u));
    return screen&&stopped&&expectedExit&&settled&&clean&&footer;
}
}
int wmain(int argc,wchar_t **argv){
    if(argc==6&&std::wstring(argv[1])==L"--host")return host(argv[2],argv[3],argv[4],argv[5]);
    if(argc!=5)return 2;
    const std::wstring mode=argv[2],stop=argv[4];
    if((mode!=L"idle"&&mode!=L"dashboard"&&mode!=L"plain-delayed")||(stop!=L"q"&&stop!=L"ctrl-c")||(mode==L"plain-delayed"&&stop!=L"ctrl-c"))return 2;
    std::wstring healthyPath=argv[3],blockedPath=argv[3];
    if(mode!=L"idle"){
        const auto state=std::filesystem::path(argv[3]);
        if(!std::filesystem::create_directory(state/"healthy")||!std::filesystem::create_directory(state/"retained"))return 3;
        healthyPath=(state/"healthy").wstring();blockedPath=(state/"retained").wstring();
    }
    const bool healthy=probe(argv[1],mode,healthyPath,false,stop==L"ctrl-c");
    const bool blocked=probe(argv[1],mode,blockedPath,true,stop==L"ctrl-c");
    return healthy&&blocked?0:1;
}
