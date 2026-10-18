#pragma once
#include <cstring>
#include <string>
namespace om {
// The public payout address stays bare; authorization decoration is opt-in.
inline bool parsePearlAuthorizationModeOption(const std::string &value, bool *solo) {
    if (!solo || (value != "standard" && value != "solo")) return false;
    *solo = value == "solo";
    return true;
}
inline bool pearlSoloAuthorizationAllowed(bool solo, const char *algorithm,
                                          bool benchmark, bool hasPool,
                                          bool research) {
    return !solo || (algorithm && std::strcmp(algorithm, "pearl-pow") == 0 &&
                     !benchmark && hasPool && !research);
}
}  // namespace om
