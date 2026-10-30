#include "worker_identity.h"
#include "private_entropy.h"
#include <set>
int main(int argc,char **argv){
    try{std::string wire;uint64_t parent=0;unsigned count=0;
        if(!om::readLocalTaskInput(argc,argv,&wire)||!om::workerIdentityRequest(wire,&parent,&count)||!om::bindLocalTaskParent(parent))return 2;
        std::string result="CMWK1 "+std::to_string(count)+"\n";std::set<std::string> seen;
        for(unsigned i=0;i<count;++i){std::string key;if(!om::privateEnergyKey(&key)||!seen.insert(key).second)return 3;result+=key+"\n";}
        return om::writeLocalTaskOutput(result)?0:4;
    }catch(...){return 5;}
}
