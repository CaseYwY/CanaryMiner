#pragma once

#include <csignal>

namespace om {

// Reads one explicit local file, independently of the installed HTTPS helper.
int commandTickerFile(int argc,const char *const *argv,const volatile sig_atomic_t *stop);

}  // namespace om
