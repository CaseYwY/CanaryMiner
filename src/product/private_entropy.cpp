#include "private_entropy.h"
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#else
#include <cerrno>
#include <sys/random.h>
#endif
namespace om {
bool privateEnergyKey(std::string *out){
    if(!out)return false;
    unsigned char bytes[16]={};
#if defined(_WIN32)
    if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)return false;
#else
    size_t done=0;while(done<sizeof(bytes)){const auto n=getrandom(bytes+done,sizeof(bytes)-done,0);if(n<0&&errno==EINTR)continue;if(n<=0)return false;done+=static_cast<size_t>(n);}
#endif
    const char *digits="0123456789abcdef";std::string result;result.reserve(32);
    for(const auto byte:bytes){result+=digits[byte>>4];result+=digits[byte&15];}*out=std::move(result);return true;
}
} // namespace om
