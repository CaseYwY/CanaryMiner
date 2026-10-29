#include "billing_query_process.h"
int main(int argc,char **argv){
    std::string input,output;om::BillingQueryPageRequest request;uint64_t parent=0;
    if(!om::readLocalTaskInput(argc,argv,&input)||!om::decodeBillingQueryRequest(input,&request,&parent)||!om::bindLocalTaskParent(parent))return 1;
    const auto reply=om::executeBillingQueryRequest(request);
    return om::encodeBillingQueryReply(request,reply,&output)&&om::writeLocalTaskOutput(output)?0:1;
}
