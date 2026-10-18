# DRAFT build container for the CanaryMiner natives - NOT BUILT, NOTHING PULLED (Gate 1b-1 Part B4).
#
# Purpose: one pinned, reproducible place to run tools/test_all.sh (CPU tests + the kernel SASS invariant) and to build the
# SM120 native. It pins the CUDA 13.1 redistributable components by SHA-256 (the values in
# LICENSES/NVIDIA-CUDA-13.1.1/provenance.json, the same files the qualified natives were built with), g++ 13.3, and CMake 3.31.
#
# Before the first real build (owner decision: the worker was told not to pull or build images):
#   1. replace every <PIN> below with a value recorded from a verified download (base image digest, CMake tarball SHA-256);
#   2. build with:  docker build -t canaryminer-build packaging/build-container   (from the repository root: -f is this file)
#   3. run:         docker run --rm -v "$PWD":/src canaryminer-build   with the verifier build mounted and
#                   CANARY_VERIFIER_PYTHONPATH / .secrets.local provided (they are never baked into the image).
#
# Not part of this image: the NVIDIA driver (kernels are only compiled and inspected, never run), any wallet, pool or credential.

ARG UBUNTU_IMAGE=ubuntu:24.04@sha256:<PIN>
FROM ${UBUNTU_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++-13 gcc-13 make python3 ca-certificates curl xz-utils git \
 && rm -rf /var/lib/apt/lists/* \
 && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100 \
 && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 \
 && g++ --version | head -1 | grep -E ' 13\.3\.[0-9]+' >/dev/null   # fail the build if the compiler is not 13.3

# CMake 3.31.x (architecture-specific "a" CUDA targets need >= 3.31).
ARG CMAKE_VERSION=3.31.6
ARG CMAKE_SHA256=<PIN>
RUN curl -fsSL -o /tmp/cmake.tgz "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
 && echo "${CMAKE_SHA256}  /tmp/cmake.tgz" | sha256sum -c - \
 && tar -xzf /tmp/cmake.tgz -C /opt && ln -s /opt/cmake-${CMAKE_VERSION}-linux-x86_64/bin/cmake /usr/local/bin/cmake \
 && ln -s /opt/cmake-${CMAKE_VERSION}-linux-x86_64/bin/ctest /usr/local/bin/ctest && rm /tmp/cmake.tgz

# CUDA 13.1 components, each verified against the SHA-256 recorded in LICENSES/NVIDIA-CUDA-13.1.1/provenance.json.
ARG CUDA_BASE=https://developer.download.nvidia.com/compute/cuda/redist
RUN set -eu; mkdir -p /tmp/cuda && cd /tmp/cuda; \
    fetch() { curl -fsSL -o "$2" "${CUDA_BASE}/$1/linux-x86_64/$2" && echo "$3  $2" | sha256sum -c -; tar -xJf "$2"; rm "$2"; }; \
    fetch cuda_nvcc      cuda_nvcc-linux-x86_64-13.1.115-archive.tar.xz      858a74e6caa36fc9b4fa1ec5e8d38f568ab8046ef93dca9ba8b3f01dee09f30d; \
    fetch libnvvm        libnvvm-linux-x86_64-13.1.115-archive.tar.xz        9038a2bf1237d9decdf99d90c4b43639536c9dbe4b3a40d1e6add5413a02096f; \
    fetch cuda_crt       cuda_crt-linux-x86_64-13.1.115-archive.tar.xz       e2861bbc6400815be6689cdc84afddfedc51103189888c8e51a2e9543e55a5b3; \
    fetch cuda_cudart    cuda_cudart-linux-x86_64-13.1.80-archive.tar.xz     b626f4790f46bc9324a1047f2fbcc9a42bc4a722b053056e61cc00da54ad6f32; \
    fetch cuda_cccl      cuda_cccl-linux-x86_64-13.1.115-archive.tar.xz      b0fff2704e59d4e6f6f4b7b5b64f96a7bf86760a5696fd6cb6936f8f9cd92d4c; \
    fetch cuda_cuobjdump cuda_cuobjdump-linux-x86_64-13.1.115-archive.tar.xz ead244f805ce4616bcc9ecb5b6d993926522b825cfc1dcfff0b1113b6ac656d9
COPY assemble_toolkit.py /usr/local/bin/assemble_toolkit.py
RUN python3 /usr/local/bin/assemble_toolkit.py /tmp/cuda /opt/cuda-13.1 && rm -rf /tmp/cuda \
 && /opt/cuda-13.1/bin/nvcc --version | grep -E 'release 13\.1, V13\.1\.115'

ENV CANARY_TOOLKIT=/opt/cuda-13.1 PATH=/opt/cuda-13.1/bin:$PATH
WORKDIR /src
# The default command is the full gate; mount the source tree at /src. CANARY_VERIFIER_PYTHONPATH and .secrets.local are mandatory
# inputs of tools/test_all.sh and must be supplied at run time.
CMD ["sh", "tools/test_all.sh"]
