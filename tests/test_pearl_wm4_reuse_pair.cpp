#include "pearl_search_energy_pair.h"
#include <array>
#include <cstdio>
#include <string>

namespace ep = pearl_search_energy_pair;
static unsigned failures = 0;
void check(bool okay, const char *message) {
    if (!okay) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
int main() {
    constexpr auto mode = ep::Mode::FixedWm8VsWm4Reuse;
    constexpr const char *uuid = "GPU-6e7558be-663d-16dd-0814-1ba4ec8ef253";
    std::array<const char *, 7> good{{"fixture", "--run", "--gpu-uuid", uuid, "--energy", "--attempts", "1"}};
    check(ep::plan(mode,false) == UINT64_C(0x435544410001002a) &&
          ep::plan(mode,true) == UINT64_C(0x435544410001002d), "actual distinct fixed role identities");
    check(std::string_view(ep::schema(mode)) == "pearl-wm4-reuse-whole-search-energy-v1", "distinct schema");
    for (const char *batch : {"1","4"}) {
        good[6] = batch;
        check(ep::batchAttempts(mode,7,good.data()) == unsigned(batch[0]-'0'), "closed exact batch count");
        for (bool a : {false,true}) for (bool b : {false,true}) for (bool conflict : {false,true})
            check(ep::admitted(mode,7,good.data(),ep::selector(mode,true),a,b,conflict) == (a&&b&&!conflict),
                  "both linked capabilities and clean environment before device");
        for (const char *bad : {static_cast<const char *>(nullptr), "", "invalid", ep::selector(mode,false),
                               ep::kCudaBk32FixedExperimentOptIn, ep::kCudaWm8ExperimentOptIn,
                               ep::kCudaFixedExperimentOptIn,
                               ep::kCudaWm8Cap112ExperimentOptIn})
            check(!ep::admitted(mode,7,good.data(),bad,true,true,false), "other selector refused");
        for (int argc : {-1,0,1,2,3,4,5,6,8,10})
            check(!ep::batchAttempts(mode,argc,good.data()), "closed argc, no implicit batch");
        for (size_t index=0;index<good.size();++index) {
            auto bad=good; bad[index]=nullptr;
            check(!ep::batchAttempts(mode,7,bad.data()), "null arguments refused");
        }
        for (size_t index : {size_t(1),size_t(2),size_t(4),size_t(5)}) {
            auto bad=good; bad[index]="--attempts";
            if (index==5) bad[index]="--energy";
            check(!ep::batchAttempts(mode,7,bad.data()), "unknown/repeated/reordered option refused");
        }
        for (auto old : {ep::Mode::RuntimeWm8VsFixedWm8,ep::Mode::FixedWm8VsBk32}) {
            check(!ep::batchAttempts(old,7,good.data()), "old entry cannot accept new CLI");
            check(ep::batchAttempts(old,5,good.data())==4, "old exact invocation remains four");
        }
    }
    check(!ep::batchAttempts(mode,7,nullptr), "null argv refused");
    for (const char *batch : {"", "0", "2", "3", "5", "01", "+1", "-1", "4 ", "1junk"}) {
        good[6]=batch; check(!ep::batchAttempts(mode,7,good.data()), "no numeric-prefix parsing");
    }
    good[6]="1";
    for (const char *badUuid : {"", "GPU-invalid", "gpu-6e7558be-663d-16dd-0814-1ba4ec8ef253",
                               "GPU-6E7558be-663d-16dd-0814-1ba4ec8ef253", "GPU-6e7558be-663d-16dd-0814-1ba4ec8ef253 "}) {
        auto bad=good; bad[3]=badUuid; check(!ep::batchAttempts(mode,7,bad.data()), "canonical UUID required");
    }
    const std::string spaced=std::string(ep::selector(mode,true))+" ";
    check(!ep::admitted(mode,7,good.data(),spaced.c_str(),true,true,false), "selector exact bytes");
    if (failures) return 1;
    std::puts("WM4 reuse pair admission: PASS (CPU only; no CUDA/NVML/source)");
}
