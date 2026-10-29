#include "billing_input_fixture.h"
#include "product/billing_owner.h"
#include <chrono>
#include <iostream>
#include <thread>
using namespace billing_input_fixture;
namespace {
bool completed(BillingOwner &owner){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(65);
    while(owner.snapshot()->pending&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return !owner.snapshot()->pending;}
}
int main(int argc,char **argv){
    if(argc!=8)return 2;
    BillingOwnerHelpers helpers{std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]),std::filesystem::absolute(argv[4])};
    const auto fixture=std::filesystem::absolute(argv[5]),file=std::filesystem::absolute(std::filesystem::u8path(argv[6]));const std::string action=argv[7];
    const auto p=plan(file);if(action=="read-parent")helpers.monetaryQuery=fixture;if(action=="write-parent")helpers.input=fixture;
    BillingOwner owner(helpers,{file,p.context.binding,p.context.limits},{true,60000},[]{return BillingOwnerTime{at,10000};});
    if(!owner.start(1)||!completed(owner))return 3;
    if(action=="write-parent"&&(owner.createReviewed(1,owner.snapshot()->revision)!=BillingOwnerAdmission::Queued||!completed(owner)))return 4;
    const auto s=owner.snapshot();
    std::cout<<"{\"state\":"<<static_cast<unsigned>(s->state)<<",\"stable\":"<<(s->view&&s->view->stable?"true":"false")
        <<",\"stage\":"<<(s->view&&s->view->stage&&s->view->stage->present?"true":"false")
        <<",\"money\":"<<(s->view&&s->view->monetary?std::to_string(static_cast<unsigned>(s->view->monetary->state)):"null")
        <<",\"ticket\":"<<s->lastTicket<<",\"sequence\":"<<(s->view&&s->view->monetary?s->view->monetary->committed.sequence:0)<<"}\n";
}
