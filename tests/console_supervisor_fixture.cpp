// Actual supervisor with owned CPU workers and a no-I/O display source. Never opens a driver or endpoint.
#include "cpu_telemetry_fixture.h"
#include "console_completion_fixture.h"
#include "../src/product/supervisor.h"
#include <csignal>
#include <fstream>
namespace {
volatile sig_atomic_t stopped=0;
void stopSignal(int){stopped=1;}
class EmptyReader final:public om::DisplaySampleReader {
    om::DisplaySampleReading read(size_t,const std::atomic<bool> &)override{return {};}
};
}
int main(int argc,char **argv){
    using namespace om;
    if(argc>=4&&std::string(argv[1])=="--owned-output-worker"){
        console_completion_fixture::disableChild();
        const auto state=std::filesystem::u8path(argv[3]);
        std::ofstream(state/"worker-starts",std::ios::app)<<"start\n";
        const bool delayed=argc>4&&std::string(argv[4])=="--delay-output";
        if(delayed){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(!std::filesystem::exists(state/"go-output")){if(std::chrono::steady_clock::now()>=end)return 4;std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
        for(unsigned i=0;i<(delayed?16u:8u);++i){
            const std::string line="console-line-"+std::to_string(i)+" "+std::string(delayed?1800:768,'x')+" --password=\"synthetic private value\"\n";
            std::fwrite(line.data(),1,line.size(),stdout);
        }
        std::fflush(stdout);
        char input[32];while(std::fread(input,1,sizeof(input),stdin)>0){}
        return 0;
    }
    if(argc!=3)return 2;
    const std::string mode=argv[1];if(mode!="dashboard"&&mode!="plain"&&mode!="plain-delayed")return 2;
    if(!platformInit())return 3;
    std::signal(SIGINT,stopSignal);std::signal(SIGTERM,stopSignal);
    SupervisorOptions options;options.exe=selfExecutablePath();options.apiEnabled=false;
    options.userArgs={"--owned-output-worker","--fixture-state",argv[2]};
    if(mode=="plain-delayed")options.userArgs.push_back("--delay-output");
    options.seeds={WorkerSeed{0,"fixture-output-uuid","CPU fixture","","",0,0}};
    options.version="console-fixture";options.dashboard=mode=="dashboard";options.stop=&stopped;options.drainMs=0;
    options.startupNotice="console fixture startup\n";
    options.displaySampleFactory=[](const std::vector<DisplaySampleTarget> &){return std::make_unique<EmptyReader>();};
    const int result=Supervisor(std::move(options)).run();platformShutdown();console_completion_fixture::complete();return result;
}
