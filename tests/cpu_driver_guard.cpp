// Linux CPU-fixture link guard: reject a driver probe before the real loader runs.
// Link the executable with --wrap=dlopen; never include in a production target.
#include <unistd.h>
#include <initializer_list>
namespace {
bool contains(const char *name,const char *needle){
    for(;*name;++name){const char *a=name,*b=needle;
        while(*a&&*b){const char lower=*a>='A'&&*a<='Z'?static_cast<char>(*a-'A'+'a'):*a;
            if(lower!=*b)break;
            ++a;++b;
        }
        if(!*b)return true;
    }
    return false;
}
}
extern "C" void *__real_dlopen(const char *,int);
extern "C" void *__wrap_dlopen(const char *name,int flags){
    if(name)for(const char *driver:{"cuda","nvidia","nvml","rocm","amdhip","opencl","hsa-runtime"})if(contains(name,driver)){
        const char message[]="CPU fixture refused driver library\n";
        const auto ignored=write(STDERR_FILENO,message,sizeof(message)-1);(void)ignored;_exit(95);
    }
    return __real_dlopen(name,flags);
}
