// Host compiler only: no CUDA runtime or driver. Any device call exits loudly.
#pragma once
#include <cstddef>
#include <cstdio>
#include <cstdlib>
inline int forbiddenPearlCliSetup() {
    std::fputs("FORBIDDEN_DEVICE_SETUP (host CLI stub)\n", stderr);
    std::exit(97);
}
constexpr int cudaSuccess = 0;
struct cudaDeviceProp {
    char name[256]{};
    std::size_t totalGlobalMem = 0;
    int major = 0, minor = 0, multiProcessorCount = 0;
    int pciBusID = 0, pciDeviceID = 0, pciDomainID = 0;
    struct { unsigned char bytes[16]{}; } uuid;
};
inline int cudaGetDeviceCount(int *) { return forbiddenPearlCliSetup(); }
inline int cudaGetDeviceProperties(cudaDeviceProp *, int) { return forbiddenPearlCliSetup(); }
inline int cudaSetDevice(int) { return forbiddenPearlCliSetup(); }
