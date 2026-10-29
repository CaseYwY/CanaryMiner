// Helper-side only: OS entropy may block. No clock/PID/device or pseudo-random fallback.
#pragma once
#include <string>
namespace om {bool privateEnergyKey(std::string *out);}
