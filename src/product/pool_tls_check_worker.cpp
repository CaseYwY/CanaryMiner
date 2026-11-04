#include "pool_tls_check.h"
int main(int argc,char **argv){
    try{std::string wire;om::PoolTlsCheckRequest request;uint64_t parent=0;
        if(!om::readLocalTaskInput(argc,argv,&wire)||!om::decodePoolTlsCheckRequest(wire,&request,&parent)||!om::bindLocalTaskParent(parent))return 2;
        const std::atomic<bool> cancel{false};const auto reply=om::executePoolTlsCheck(request,cancel);
        return om::encodePoolTlsCheckReply(reply,&wire)&&om::writeLocalTaskOutput(wire)?0:3;
    }catch(...){return 4;}
}
