// Owned negative probe: the link guard must reject these names before dlopen.
#include <dlfcn.h>
#include <cstring>
int main(int argc,char **argv){
    if(argc!=2)return 2;
    const bool self=std::strcmp(argv[1],"self")==0;
    void *module=dlopen(self?nullptr:argv[1],RTLD_NOW);
    if(module)dlclose(module);
    return self&&module?0:3;
}
