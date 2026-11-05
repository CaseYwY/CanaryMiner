// Intentionally compiled without a UTF-8 switch, like low-level native helper consumers.
#include "product/runtime_preferences.h"
#include <cstdio>
int main(){
    using Phase=om::ChangeActivation;
    const Phase phases[]={Phase::Presentation,Phase::WorkBoundary,Phase::NextStart,Phase::SeparatePermission,static_cast<Phase>(999)};
    const char *expected[]={"live presentation","safe work boundary","next explicit start; no automatic restart","separate permission required","unsupported"};
    for(unsigned i=0;i<5;++i)if(std::string(om::activationName(phases[i]))!=expected[i])return 1;
    if(std::string(om::activationName(static_cast<Phase>(-1)))!="unsupported")return 1;
    std::puts("ASCII-only low-level runtime activation source consumer: exact descriptions and unknown-enum recovery passed");return 0;
}
