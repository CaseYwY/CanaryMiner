#include "game_detection.h"
int main(int argc,char **argv){
    try{
        std::string wire;if(!om::readLocalTaskInput(argc,argv,&wire))return 2;
        om::GameProbeRequest request;uint64_t parent=0;
        if(!om::decodeGameProbeRequest(wire,&request,&parent)||!om::bindLocalTaskParent(parent))return 3;
        const auto reply=om::executeGameProbe(request);
        if(!om::encodeGameProbeReply(request,reply,&wire))return 4;
        return om::writeLocalTaskOutput(wire)?0:5;
    }catch(...){return 6;}
}
