#include "safety_telemetry.h"
int main(int argc,char **argv){
    try{std::string wire;om::SafetyTelemetryRequest request;uint64_t parent=0;
        if(!om::readLocalTaskInput(argc,argv,&wire)||!om::decodeSafetyTelemetryRequest(wire,&request,&parent)||!om::bindLocalTaskParent(parent))return 2;
        const auto reply=om::collectSafetyTelemetry(request);
        if(!om::encodeSafetyTelemetryReply(request,reply,&wire))return 3;
        return om::writeLocalTaskOutput(wire)?0:4;
    }catch(...){return 5;}
}
