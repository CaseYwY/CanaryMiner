// Owned CPU process only. stdout is an oracle-owned pipe; stderr carries non-private test barriers.
#include "product/console_output.h"
#include <cstdio>
#include <iostream>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char *name){++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL %s\n",name);}}
#define C(expr) check((expr),#expr)
template<class F> bool wait(F test,unsigned ms=3000){const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);do{if(test())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}while(std::chrono::steady_clock::now()<end);return false;}
void barrier(const char *value){std::fprintf(stderr,"%s\n",value);std::fflush(stderr);}
std::string payload(){std::string bytes(om::consoleOutputLimit,'x');for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<char>(i%256);return bytes;}
}
int main(int argc,char **argv){using namespace om;if(argc!=3)return 2;
    const auto helper=std::filesystem::absolute(std::filesystem::u8path(argv[1]));const std::string mode=argv[2];
    SessionIntent intent;const auto token=intent.explicitStart();C(intent.permits(token));
    ConsoleOutputPolicy policy;
    ConsoleOutput output(helper,intent,token,policy);
    if(mode=="stopped-before-start"){
        output.requestStop();C(!output.start());C(!output.submit("denied"));C(output.snapshot().state==ConsoleOutputState::Stopped);output.stop();
    }else{
#if !defined(_WIN32)
        const int flags=fcntl(STDOUT_FILENO,F_GETFL);
#endif
        C(output.start());C(!output.start());
        const bool ready=wait([&]{return output.snapshot().state==ConsoleOutputState::Ready;});C(ready);if(!ready){output.stop();return 1;}
        C(output.snapshot().issued==1&&output.snapshot().accepted==1);
        C(!output.snapshot().outputAccepted&&output.pollKey()==0);
        if(mode=="healthy"){
            C(output.submit(payload()));C(wait([&]{return output.snapshot().accepted==2;}));
            C(output.snapshot().outputAccepted);
            C(output.submit("\nORDERED-TAIL\n"));C(wait([&]{return output.snapshot().accepted==3;}));
            output.requestStop();output.stop();C(output.snapshot().settled);
        }else if(mode=="invalid"){
            C(!output.submit(std::string(consoleOutputLimit+1,'x')));C(output.snapshot().state==ConsoleOutputState::Faulted);C(output.snapshot().transport==LocalTaskStatus::Invalid);
            output.requestStop();output.stop();C(output.snapshot().transport==LocalTaskStatus::Invalid);
        }else{
            C(output.submit(payload()));C(wait([&]{return output.snapshot().inFlight&&output.snapshot().issued==2;}));barrier("ISSUED");
            if(mode=="timeout"){
                C(wait([&]{return output.snapshot().state==ConsoleOutputState::Faulted;}));
                const auto failed=output.snapshot();C(failed.transport==LocalTaskStatus::Timeout&&failed.accepted==1&&failed.issued==2);
                C(!output.start()&&!output.submit("not-replayed"));output.requestStop();output.stop();C(output.snapshot().transport==LocalTaskStatus::Timeout);
            }else{
                std::string command;std::getline(std::cin,command);C(command=="blocked");
                if(mode=="coalesce"||mode=="overflow"||mode=="foreign"){
                    for(unsigned i=0;i<64;++i)C(output.submit("unissued-screen-"+std::to_string(i),true));
                    C(output.snapshot().queued==1&&output.snapshot().queuedBytes==18&&output.snapshot().accepted==1&&output.snapshot().issued==2);
                    if(mode=="foreign"){
                        const auto original=output.snapshot();
                        {NativeProcessScope foreign(nativeProcessEpoch()+1);C(output.snapshot().state==ConsoleOutputState::Stopped);C(!output.submit("foreign"));output.requestStop();output.stop();}
                        C(output.snapshot().state==original.state&&output.snapshot().issued==original.issued&&output.snapshot().queuedBytes==original.queuedBytes);
                    }
                    if(mode=="overflow"){
                        for(unsigned i=0;i<31;++i)C(output.submit("ordered-line\n"));
                        C(!output.submit("overflow\n"));C(output.snapshot().state==ConsoleOutputState::Faulted&&output.snapshot().transport==LocalTaskStatus::Invalid&&output.snapshot().queued==0);
                    }
                }
                if(mode=="epoch"){
                    NativeProcessAdmission fault;C(fault.begin());fault.markUnresolved();fault.settled();
                    C(!nativeProcessUnresolved()&&!intent.permits(token));C(output.snapshot().state==ConsoleOutputState::Faulted&&output.snapshot().transport==LocalTaskStatus::Unreaped);C(!output.submit("old-epoch"));
                }
                const auto began=std::chrono::steady_clock::now();intent.stop();output.requestStop(ConsoleCleanup::Cursor);output.stop();
                C(std::chrono::steady_clock::now()-began<std::chrono::milliseconds(900));C(output.snapshot().settled&&output.snapshot().queued==0&&!output.snapshot().inFlight);
                if(mode=="overflow")C(output.snapshot().transport==LocalTaskStatus::Invalid);
                if(mode=="epoch")C(output.snapshot().transport==LocalTaskStatus::Unreaped);
                C(!output.start()&&!output.submit("stopped-replay"));
            }
        }
#if !defined(_WIN32)
        C(fcntl(STDOUT_FILENO,F_GETFL)==flags); // never mutate the parent's shared open-file-description flags
#endif
    }
    std::fprintf(stderr,"console owner %s: %u checks, %u failures\n",mode.c_str(),checks,failures);return failures?1:0;
}
