// Prevalidated read-only native-terminal projection. Never carry raw provider strings into terminal output.
#pragma once
#include <string>
#include <vector>
namespace om {
struct PublicMarketView {
    bool enabled = false;
    std::vector<std::string> lines;
};
}  // namespace om
