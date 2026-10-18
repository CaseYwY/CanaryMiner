# Third-party notices

CanaryMiner is MIT-licensed (see `LICENSE`). It contains and was built with the third-party material below. Each
third-party component keeps its own licence, and that licence applies to that component only.

## 1. SOAT Miner (MIT License)

Portions derived from SOAT Miner, Copyright (c) 2026 Son of a Tech, used under the MIT License. The copyright and
permission notice is reproduced below as the licence requires (SHA-256 of `LICENSES/MIT-UPSTREAM.txt`:
`3d1501ecb1df3e6fbbd6330ef0cdaf7194747ce81fe4cbd4dd682bc0ebc73386`). The upstream author does not endorse, sponsor or support CanaryMiner.

```text
MIT License

Copyright (c) 2026 Son of a Tech

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## 2. Pearl Research Labs (ISC License)

CanaryMiner includes ports of, and is validated against, Pearl reference material. This is the root licence of the
`pearl-research-labs/pearl` repository as retained in `LICENSES/Pearl-821c811d.txt` (revision
`821c811d3e05979721f61434e804ec5f5473b4c0`; SHA-256 of the LF bytes `f88bcb5ffff7a414674de43c9d38508fa55a8065ab13ec112417a1833a9de126`). The list of sub-projects inside the
notice does not mean CanaryMiner includes those sub-projects, and the exact revision the historical port was taken
from is not recorded. Keep this paragraph with the text.

```text
ISC License

Copyright (c) 2025-2026 Pearl Research Labs
Copyright (c) 2015-2016 The Decred developers

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

---

This monorepo contains multiple sub-projects, each with its own LICENSE
file. See the LICENSE in each sub-directory for details:

  node/             ISC                (Pearl Research Labs)
  wallet/           ISC                (Pearl Research Labs)
  spv/              MIT                (Lightning Labs)
  dnsseeder/        Apache-2.0
  plonky2/          MIT OR Apache-2.0  (The Plonky2 Authors, Pearl Research Labs)
  xmss/external/    CC0-1.0
  miner/pearl-gemm/third_party/cutlass/  BSD-3-Clause (NVIDIA)
```

## 3. NVIDIA CUDA Toolkit 13.1.1 components

The native executables are built with, and statically link parts of, the CUDA 13.1 toolkit (nvcc 13.1.115, libnvvm,
CRT, cudart, CCCL, cuobjdump). The exact licence files of those components are preserved, unmodified, in
`LICENSES/NVIDIA-CUDA-13.1.1/` (cuda_cccl.txt, cuda_crt.txt, cuda_cudart.txt, cuda_cuobjdump.txt, cuda_nvcc.txt, libnvvm.txt, provenance.json); `provenance.json` records each official download URL, version and
hash. The NVIDIA redistribution terms for a shipped binary have not been assessed in Gate 1a.

## 4. BLAKE3

The host and device BLAKE3 implementations are in `src/algos/pearl-pow/job.h` and `blake3.cuh`. Their external
derivation is **UNVERIFIED**: the code carries only internal port and test-vector comments, and testing an
implementation against a library does not show it was copied from that library. No authorship or licence is assigned
here. (The Python `blake3` 1.0.8 wheel used by the validation provider ships its own licence file and is a separate
component.)

## 5. Not yet inventoried

| item | status |
|---|---|
| Python `blake3` and `typing_extensions` wheels (provider package) | licence files ship with the wheels; Gate 6 inventories the final package |
| Pearl verifier build (`pearl_mining`, Cargo dependency set) | rc.1 collected a conservative Cargo notice set; regenerate at Gate 6 |
| Nuitka compiler and runtime-library exception (if the controller stays Nuitka-compiled) | re-evaluate at Gate 6 |
| NVML / CUDA driver libraries | system libraries, not redistributed |
