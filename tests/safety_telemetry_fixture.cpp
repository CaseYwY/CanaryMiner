// Controlled CPU helper. Synthetic modes occupy only this test executable; never load any driver.
#include "product/safety_telemetry.h"
#include <fstream>
#include <thread>
int main(int argc,char **argv){
    using namespace om;
    std::string wire;SafetyTelemetryRequest r;uint64_t parent=0;
    if(!readLocalTaskInput(argc,argv,&wire)||!decodeSafetyTelemetryRequest(wire,&r,&parent)||!bindLocalTaskParent(parent))return 2;
    const char mode=r.devices[0].back();
    if(mode=='d')std::ofstream(std::filesystem::absolute(argv[0]).parent_path()/"fixture-safety-ready")<<localTaskProcessId();
    if(mode=='3'||mode=='d')std::this_thread::sleep_for(std::chrono::seconds(60));
    if(mode=='4')return writeLocalTaskOutput("malformed\n")?0:3;
    SafetyTelemetryReply out;out.status=SafetyTelemetryStatus::Ok;out.began=out.finished=safetyTelemetryTime();
    if(mode=='2')std::this_thread::sleep_for(std::chrono::milliseconds(60));
    out.finished=safetyTelemetryTime();
    if(mode=='5'){out.began.utcMs+=5000;out.began.monotonicMs+=5000;out.finished=out.began;}
    if(mode=='6')out.status=SafetyTelemetryStatus::DeviceFault;
    if(mode=='7')out.status=SafetyTelemetryStatus::Identity;
    if(mode=='8')out.status=SafetyTelemetryStatus::Unsupported;
    if(mode=='9')out.status=SafetyTelemetryStatus::Clock;
    if(mode=='a'&&r.revision>=2)out.status=SafetyTelemetryStatus::Unavailable;
    if(!r.enabled)out.status=SafetyTelemetryStatus::Disabled;
    if(out.status==SafetyTelemetryStatus::Ok)for(size_t i=0;i<r.devices.size();++i)out.readings.push_back({40+static_cast<int>(i),0,511});
    if(mode=='c')++r.revision;
    if(!encodeSafetyTelemetryReply(r,out,&wire))return 4;
    return writeLocalTaskOutput(wire)?0:5;
}
