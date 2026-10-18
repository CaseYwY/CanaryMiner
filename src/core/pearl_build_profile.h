// Build-time algorithm selection. No runtime environment toggle or GPU access.
#pragma once
#include <string>

#ifndef CANARY_PEARL_ONLY_BUILD
#define CANARY_PEARL_ONLY_BUILD 0
#endif
#define CANARY_PROFILE_TEXT_INNER(x) #x
#define CANARY_PROFILE_TEXT(x) CANARY_PROFILE_TEXT_INNER(x)
static_assert(sizeof(CANARY_PROFILE_TEXT(CANARY_PEARL_ONLY_BUILD)) == 2 &&
              (CANARY_PROFILE_TEXT(CANARY_PEARL_ONLY_BUILD)[0] == '0' ||
               CANARY_PROFILE_TEXT(CANARY_PEARL_ONLY_BUILD)[0] == '1'),
              "CANARY_PEARL_ONLY_BUILD must be the literal 0 or 1");
#undef CANARY_PROFILE_TEXT
#undef CANARY_PROFILE_TEXT_INNER

namespace om {
inline constexpr bool pearlOnlyBuild = CANARY_PEARL_ONLY_BUILD == 1;
inline constexpr const char *defaultAlgorithmName =
    pearlOnlyBuild ? "pearl-pow" : "autolykos2";
inline bool buildAllowsAlgorithm(const std::string &name) noexcept {
    return !pearlOnlyBuild || name == "pearl-pow";
}
} // namespace om
