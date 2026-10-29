#pragma once
#include "energy_capture.h"
namespace om {
// An optional native factory seam: asynchronous Load, one lifetime-leased Provision, then fenced Existing capture.
// No store repair, re-enrollment, lease timeout/stealing, automatic restart or hardware discovery.
std::unique_ptr<EnergyCaptureSink> makeLeasedEnergyCapture(std::filesystem::path authorizationHelper,std::filesystem::path collectorHelper,
    std::filesystem::path authorizationFile,bool cliNoEnergy=false,unsigned timeoutMs=10000,EnergyHistoryClock clock={});
} // namespace om
