#include "billing_input_query.h"
int main(int argc,char **argv){
    std::string input,output;om::BillingQueryPageRequest request;uint64_t parent=0;
    if(!om::readLocalTaskInput(argc,argv,&input)||!om::decodeBillingInputQueryRequest(input,&request,&parent)||!om::bindLocalTaskParent(parent))return 1;
    const auto reply=om::executeBillingInputQuery(request);
    return om::encodeBillingInputQueryReply(request,reply,&output)&&om::writeLocalTaskOutput(output)?0:1;
}
