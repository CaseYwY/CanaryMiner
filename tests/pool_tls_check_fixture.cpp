// Separate owned helper for phase stalls/malformed replies. Never linked into the native connector/helper.
#include "product/pool_tls_check.h"
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <thread>
int main(int argc,char **argv){
    using namespace om;
    try{std::string wire;PoolTlsCheckRequest request;uint64_t parent=0;
        if(!readLocalTaskInput(argc,argv,&wire)||!decodePoolTlsCheckRequest(wire,&request,&parent)||!bindLocalTaskParent(parent))return 2;
        if(std::getenv("CANARY_SYNTHETIC_SECRET")||std::getenv("SSLKEYLOGFILE")||std::getenv("HTTPS_PROXY"))return 3;
        if(request.host.rfind("stall-",0)==0){const auto marker=std::filesystem::absolute(std::filesystem::u8path(argv[0])).parent_path()/"fixture-tls-ready";
            {std::ofstream out(marker);out<<localTaskProcessId();}std::this_thread::sleep_for(std::chrono::seconds(30));}
        if(request.host=="malformed.test")return writeLocalTaskOutput("CMTLSA1\n9\n4\n4\n")?0:4;
        if(request.host=="trailing.test")return writeLocalTaskOutput("CMTLSA1\n9\n4\n0\njunk")?0:4;
        if(request.host=="overflow.test"){const std::string bytes(localTaskResponseLimit,'x');for(unsigned i=0;i<3;++i)std::fwrite(bytes.data(),1,bytes.size(),stdout);std::fflush(stdout);return 0;}
        PoolTlsCheckReply reply{PoolTlsCheckStatus::Verified,PoolTlsCheckStage::Complete,PearlTlsError::None};
        return encodePoolTlsCheckReply(reply,&wire)&&writeLocalTaskOutput(wire)?0:4;
    }catch(...){return 5;}
}
