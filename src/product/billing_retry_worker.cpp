#include "billing_retry_process.h"
int main(int argc,char **argv){
    std::string input,output;om::BillingRetryRequest request;uint64_t parent=0;
    if(!om::readLocalTaskInput(argc,argv,&input)||!om::decodeBillingRetryRequest(input,&request,&parent)||!om::bindLocalTaskParent(parent))return 1;
    const auto reply=om::executeBillingRetryRequest(request);
    return om::encodeBillingRetryReply(request,reply,&output)&&om::writeLocalTaskOutput(output)?0:1;
}
