#include "pearl_search_energy_pair.h"
#include <array>
#include <cstdio>
#include <string>
#include <string_view>

namespace ep = pearl_search_energy_pair;
static unsigned failures = 0;
void check(bool result, const char *message) {
    if (!result) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
int main() {
    constexpr const char *uuid = "GPU-6e7558be-663d-16dd-0814-1ba4ec8ef253";
    const std::array<const char *, 5> good{{"fixture", "--run", "--gpu-uuid", uuid, "--energy"}};
    check(ep::plan(false,false) == UINT64_C(0x4355444100010028) &&
          ep::plan(false,true) == UINT64_C(0x435544410001002a) &&
          ep::plan(true,false) == UINT64_C(0x435544410001002a) &&
          ep::plan(true,true) == UINT64_C(0x435544410001002c), "exact old/new role identities");
    check(std::string_view(ep::schema(false)) == "pearl-wm8-whole-search-energy-v1" &&
          std::string_view(ep::schema(true)) == "pearl-bk32-whole-search-energy-v1", "closed distinct schemas");
    for (bool bk32 : {false,true}) {
        check(ep::admitted(bk32,5,good.data(),ep::selector(bk32,true),true,true,false), "clean explicit pair admitted");
        for (bool a : {false,true}) for (bool b : {false,true}) for (bool conflict : {false,true})
            check(ep::admitted(bk32,5,good.data(),ep::selector(bk32,true),a,b,conflict) ==
                  (a && b && !conflict), "both linked capabilities and no override required");
        for (const char *bad : {static_cast<const char *>(nullptr), "", "invalid",
                               ep::selector(bk32,false), ep::selector(!bk32,true),
                               ep::kCudaFixedExperimentOptIn,
                               ep::kCudaWm8Cap112ExperimentOptIn})
            check(!ep::admitted(bk32,5,good.data(),bad,true,true,false), "missing/malformed/other selector refuses before setup");
        const std::string spaced = std::string(ep::selector(bk32,true)) + " ";
        check(!ep::admitted(bk32,5,good.data(),spaced.c_str(),true,true,false), "selector trailing bytes refused");
        for (int argc : {-1,0,1,2,3,4,6,10})
            check(!ep::admitted(bk32,argc,good.data(),ep::selector(bk32,true),true,true,false), "closed argument count");
        check(!ep::validCli(5,nullptr), "null argv refused");
        for (size_t index=0;index<good.size();++index) {
            auto bad=good; bad[index]=nullptr;
            check(!ep::validCli(5,bad.data()), "null argument refused");
        }
        for (size_t index : {size_t(1),size_t(2),size_t(4)}) {
            auto bad=good; bad[index]="--invalid";
            check(!ep::admitted(bk32,5,bad.data(),ep::selector(bk32,true),true,true,false), "unknown/reordered option refused");
        }
        for (const char *badUuid : {"", "GPU-invalid", "gpu-6e7558be-663d-16dd-0814-1ba4ec8ef253",
                                    "GPU-6E7558be-663d-16dd-0814-1ba4ec8ef253",
                                    "GPU-6e7558be-663d-16dd-0814-1ba4ec8ef253 "}) {
            auto bad=good; bad[3]=badUuid;
            check(!ep::admitted(bk32,5,bad.data(),ep::selector(bk32,true),true,true,false), "noncanonical UUID refused");
        }
    }
    if (failures) return 1;
    std::puts("Pearl search energy pair admission: PASS (CPU only, no CUDA/NVML/source)");
    return 0;
}
