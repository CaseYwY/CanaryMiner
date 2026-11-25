#pragma once

#include <csignal>
#include <filesystem>

namespace om {

int commandMarketHour(bool help, int argc, const char *const *argv);
int commandPublicMarketHour(const std::filesystem::path &module, int argc, const char *const *argv,
                            const volatile sig_atomic_t *stop);

}  // namespace om
