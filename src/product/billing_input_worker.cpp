#include "billing_input_process.h"
int main(int argc,char **argv){
    std::string input,output;om::BillingInputRequest request;uint64_t parent=0;
    if(!om::readLocalTaskInput(argc,argv,&input)||!om::decodeBillingInputRequest(input,&request,&parent)||!om::bindLocalTaskParent(parent))return 1;
    const auto reply=om::executeBillingInputRequest(request);
    return om::encodeBillingInputReply(request,reply,&output)&&om::writeLocalTaskOutput(output)?0:1;
}
