#include "forecast_query_process.h"
int main(int argc,char **argv){
    try{
        std::string wire;if(!om::readLocalTaskInput(argc,argv,&wire))return 2;
        om::CreditProcessRequest request;uint64_t pid=0;
        if(!om::decodeForecastQuery(wire,&request,&pid)||!om::bindLocalTaskParent(pid))return 3;
        const auto reply=om::executeExistingCreditQuery(request);
        if(!om::encodeCreditReply(request,reply,&wire)||wire.size()+6>om::localTaskResponseLimit)return 4;
        return om::writeLocalTaskOutput("CMFA1\n"+wire)?0:5;
    }catch(...){return 6;}
}
