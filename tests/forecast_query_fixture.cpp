// Separate controlled read-helper stalls; never compiled into the installed query helper or library.
#include "product/forecast_query_process.h"
#include <chrono>
#include <fstream>
#include <thread>
int main(int argc,char **argv){
    std::string wire;if(!om::readLocalTaskInput(argc,argv,&wire))return 2;
    om::CreditProcessRequest r;uint64_t pid=0;if(!om::decodeForecastQuery(wire,&r,&pid)||!om::bindLocalTaskParent(pid))return 3;
    const auto name=r.file.filename();
    if(name=="fixture-query-hang.db"||(name=="fixture-query-between.db"&&r.ticket%32==1)){
        auto marker=r.file;marker+=".fixture-ready";std::ofstream(marker)<<om::localTaskProcessId();
        auto release=r.file;release+=".fixture-release";
        for(unsigned i=0;i<3000&&!std::filesystem::exists(release);++i)std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const auto reply=om::executeExistingCreditQuery(r);
    if(!om::encodeCreditReply(r,reply,&wire))return 4;
    return om::writeLocalTaskOutput("CMFA1\n"+wire)?0:5;
}
