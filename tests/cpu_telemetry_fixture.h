// Test-only unavailable-NVML adapter for the header-only supervisor. No driver library is loaded.
// Include before supervisor.h in CPU fixtures; never include from an installed binary or production library.
#pragma once
#include "../src/core/platform.h"
#if defined(_WIN32)
#include <windows.h>
inline HMODULE WINAPI cpuFixtureUnavailableTelemetry(LPCSTR){return nullptr;}
#define LoadLibraryA cpuFixtureUnavailableTelemetry
#else
#include <dlfcn.h>
inline void *cpuFixtureUnavailableTelemetry(const char *,int){return nullptr;}
#define dlopen cpuFixtureUnavailableTelemetry
#endif
#include "../src/core/telemetry.h"
#if defined(_WIN32)
#undef LoadLibraryA
#else
#undef dlopen
#endif
