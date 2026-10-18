// Shared N64 kernel body. Only the epilogue differs per entry point (N64_EPILOGUE_SCAN_OUTPUT).
// Preserve the runtime guards and post-loop accumulator use: both are part of the measured native codegen.
    using namespace n64;
    if (m <= 0 || n <= 0 || m % BM || n % BN || blockDim.x != Threads || blockDim.y != 1 || blockDim.z != 1 ||
        gridDim.x != unsigned(n / BN) || gridDim.y != unsigned(m / BM) || gridDim.z != 1) return;
    const int k = runtimeK, rank = runtimeRank;
    const int dotLen = k - k % rank;
    const int totalSteps = dotLen / 32, stepsPerRank = rank / 32;
    if (totalSteps <= 0 || stepsPerRank <= 0 || totalSteps % 2) return;
    const int T = totalSteps / 2;
    if (stepsPerRank % 2) return;
    const int blocksPerFold = stepsPerRank / 2;

    const int blocksN = gridDim.x, blocksM = gridDim.y;
    const int linear = blockIdx.x + blockIdx.y * blocksN;
    const int perGroup = GroupM * blocksN;
    const int mFirst = (linear / perGroup) * GroupM;
    const int inGroup = linear % perGroup;
    const int groupRows = min(GroupM, blocksM - mFirst);
    const int rowBase = (mFirst + inGroup % groupRows) * BM;
    const int colBase = (inGroup / groupRows) * BN;
    if (rowBase >= m || colBase >= n) return;

    extern __shared__ __align__(128) int8_t smem[];
    const uint32_t sBase = static_cast<uint32_t>(__cvta_generic_to_shared(smem));
    const int tid = threadIdx.x, warp = tid >> 5, lane = tid & 31;
    const int warpM = warp / WarpsN, warpN = warp % WarpsN;

    const int cpRow = tid >> 2, cpKc = tid & 3;
    const uint32_t cpDst = uint32_t(cpRow * BK + ((cpKc ^ ((cpRow >> 1) & 3)) << 4));
    const int8_t *gA = A + size_t(rowBase + cpRow) * k + cpKc * 16;
    const int8_t *gB = Bn + size_t(colBase + cpRow) * k + cpKc * 16;
    const size_t k64 = size_t(64) * k;
    auto issue = [&](int blk, int st) {
        const uint32_t dA = sBase + st * StageBytes + cpDst, dB = dA + StageA;
        const size_t ko = size_t(blk) * BK;
#if N64_BM == 128 && N64_BN == 256
        cpasync16(dA, gA + ko);            cpasync16(dA + 64 * BK, gA + k64 + ko);
        cpasync16(dB, gB + ko);            cpasync16(dB + 64 * BK, gB + k64 + ko);
        cpasync16(dB + 128 * BK, gB + 2 * k64 + ko); cpasync16(dB + 192 * BK, gB + 3 * k64 + ko);
#elif N64_BM == 256 && N64_BN == 128
        cpasync16(dA, gA + ko);            cpasync16(dA + 64 * BK, gA + k64 + ko);
        cpasync16(dA + 128 * BK, gA + 2 * k64 + ko); cpasync16(dA + 192 * BK, gA + 3 * k64 + ko);
        cpasync16(dB, gB + ko);            cpasync16(dB + 64 * BK, gB + k64 + ko);
#else
#error "N64 tile shape not supported"
#endif
    };

    const int ldGroup = lane >> 3;
    const int aRow = (lane & 7) + (ldGroup & 1) * 8, aKc = ldGroup >> 1;
    const int bRow = (lane & 7) + (ldGroup >> 1) * 8, bKc = ldGroup & 1;
    const int sw = (aRow >> 1) & 3;
    const int swb = (bRow >> 1) & 3;
    uint32_t aOff[2], bOff[2];
#pragma unroll
    for (int h = 0; h < 2; ++h) {
        aOff[h] = uint32_t((warpM * 64 + aRow) * BK + (((2 * h + aKc) ^ sw) << 4));
        bOff[h] = uint32_t(StageA + (warpN * 64 + bRow) * BK + (((2 * h + bKc) ^ swb) << 4));
    }
    uint32_t af[2][4][4], bf[2][4][4];
    auto loadFrag = [&](int buf, int st, int h) {
        const uint32_t base = sBase + st * StageBytes;
#pragma unroll
        for (int i = 0; i < 4; ++i) ldsm4(af[buf][i], base + aOff[h] + i * 16 * BK);
#pragma unroll
        for (int j = 0; j < 4; ++j) ldsm4(bf[buf][j], base + bOff[h] + j * 16 * BK);
    };
    uint32_t acc[4][4][2][4];
#pragma unroll
    for (int i = 0; i < 4; ++i)
#pragma unroll
        for (int j = 0; j < 4; ++j)
#pragma unroll
            for (int h = 0; h < 2; ++h)
#pragma unroll
                for (int e = 0; e < 4; ++e) acc[i][j][h][e] = 0;
    auto mmaAll = [&](int buf) {
#pragma unroll
        for (int i = 0; i < 4; ++i)
#pragma unroll
            for (int j = 0; j < 4; ++j)
#pragma unroll
                for (int h = 0; h < 2; ++h)
                    n64::mma(acc[i][j][h], af[buf][i], bf[buf][j][h * 2], bf[buf][j][h * 2 + 1]);
    };
    uint32_t slot[8];
#pragma unroll
    for (int q = 0; q < 8; ++q) slot[q] = 0;
    int reduction = 0, sinceFold = 0;

#pragma unroll
    for (int s = 0; s < S; ++s) { if (s < T) issue(s, s); commit(); }
    waitPrior<S - 1>();
    __syncthreads();
    loadFrag(0, 0, 0);

#ifndef N64_FOLD
#define N64_FOLD 1
#endif
    const int myHalf = lane >> 4, myWord = lane & 15;
    (void)myHalf; (void)myWord;
    int st = 0;
    for (int kt = 0; kt < T; ++kt) {
        loadFrag(1, st, 1);
        mmaAll(0);
        waitPrior<S - 2>();
        __syncthreads();
        if (kt + S < T) issue(kt + S, st);
        commit();
        const int nst = (st + 1 == S) ? 0 : st + 1;
        if (kt + 1 < T) loadFrag(0, nst, 0);
        mmaAll(1);
#ifdef N64_NOFOLD
        if (false) {
#else
        if (++sinceFold == blocksPerFold) {
#endif
            sinceFold = 0;
            const int rw = reduction & 15;
            uint32_t p[16];
#pragma unroll
            for (int i = 0; i < 4; ++i)
#pragma unroll
                for (int j = 0; j < 4; ++j)
                    p[i * 4 + j] = xor3(xor3(acc[i][j][0][0], acc[i][j][0][1], acc[i][j][0][2]),
                                        xor3(acc[i][j][0][3], acc[i][j][1][0], acc[i][j][1][1]),
                                        acc[i][j][1][2] ^ acc[i][j][1][3]);
#if N64_FOLD == 2
            {
                const bool b16 = lane & 16;
#pragma unroll
                for (int q = 0; q < 8; ++q) {
                    const uint32_t send = b16 ? p[q] : p[q + 8], keep = b16 ? p[q + 8] : p[q];
                    p[q] = keep ^ __shfl_xor_sync(0xffffffffu, send, 16);
                }
                const bool b8 = lane & 8;
#pragma unroll
                for (int q = 0; q < 4; ++q) {
                    const uint32_t send = b8 ? p[q] : p[q + 4], keep = b8 ? p[q + 4] : p[q];
                    p[q] = keep ^ __shfl_xor_sync(0xffffffffu, send, 8);
                }
                const bool b4 = lane & 4;
#pragma unroll
                for (int q = 0; q < 2; ++q) {
                    const uint32_t send = b4 ? p[q] : p[q + 2], keep = b4 ? p[q + 2] : p[q];
                    p[q] = keep ^ __shfl_xor_sync(0xffffffffu, send, 4);
                }
                const bool b2 = lane & 2;
                {
                    const uint32_t send = b2 ? p[0] : p[1], keep = b2 ? p[1] : p[0];
                    p[0] = keep ^ __shfl_xor_sync(0xffffffffu, send, 2);
                }
                p[0] ^= __shfl_xor_sync(0xffffffffu, p[0], 1);
                const bool mine = (lane & 1) == (rw >> 3);
                const int q8 = rw & 7;
#pragma unroll
                for (int q = 0; q < 8; ++q)
                    if (mine && q == q8) slot[q] = rotl32(slot[q], kRotation) ^ p[0];
            }
#else
            uint32_t v[16];
#pragma unroll
            for (int t = 0; t < 16; ++t) v[t] = warpXor(p[t]);
#pragma unroll
            for (int t = 0; t < 16; ++t)
                if (myHalf == (t >> 3) && myWord == rw) slot[t & 7] = rotl32(slot[t & 7], kRotation) ^ v[t];
#endif
            ++reduction;
        }
        st = nst;
    }
    waitPrior<0>();
    __syncthreads();
    uint32_t *const scanScratch = reinterpret_cast<uint32_t *>(smem);

#pragma unroll
    for (int i = 0; i < 4; ++i)
#pragma unroll
        for (int j = 0; j < 4; ++j) {
            const int row = rowBase + warpM * 64 + i * 16;
            const int col = colBase + warpN * 64 + j * 16;
            if (writeC) {
#pragma unroll
                for (int h = 0; h < 2; ++h) {
                    const int r = row + (lane >> 2), c = col + h * 8 + (lane & 3) * 2;
                    cNoised[(size_t)r * n + c] = (int32_t)acc[i][j][h][0];
                    cNoised[(size_t)r * n + c + 1] = (int32_t)acc[i][j][h][1];
                    cNoised[(size_t)(r + 8) * n + c] = (int32_t)acc[i][j][h][2];
                    cNoised[(size_t)(r + 8) * n + c + 1] = (int32_t)acc[i][j][h][3];
                }
            }
#if N64_FOLD == 2
            const int tt = i * 4 + j;
            const int ownerBase = ((tt >> 3) & 1) * 16 + ((tt >> 2) & 1) * 8 + ((tt >> 1) & 1) * 4 + (tt & 1) * 2;
            uint32_t vals[8];
#pragma unroll
            for (int q = 0; q < 8; ++q) {
                vals[q] = __shfl_sync(0xffffffffu, slot[q], ownerBase + ((lane >> 3) & 1));
            }
            if (lane < 16) {
                const int w = lane & 15;
                uint32_t sv = vals[0];
#pragma unroll
                for (int q = 1; q < 8; ++q) if ((w & 7) == q) sv = vals[q];
#else
            if ((lane >> 4) == ((i * 4 + j) >> 3)) {
                const int w = lane & 15;
                const uint32_t sv = slot[(i * 4 + j) & 7];
#endif
                const int tilesPerSide = rank / kHashTile;
                const int flat = (((row / rank) * (n / rank) + col / rank) * tilesPerSide + (row % rank) / kHashTile) *
                                     tilesPerSide + (col % rank) / kHashTile;
#if !defined(N64_EPILOGUE_SCAN_OUTPUT)
                transcripts[flat * kTranscriptU32 + w] = sv;
#endif
                scanScratch[((warp * 4 + i) * 4 + j) * ScanStride + w] = sv;
            }
        }
    __syncthreads();
#if defined(N64_EPILOGUE_SCAN_OUTPUT)
    if (tid < ScanTiles) {
        const int lt = tid, ow = lt >> 4, ti = (lt >> 2) & 3, tj = lt & 3;
        const int row = rowBase + (ow / WarpsN) * 64 + ti * 16;
        const int col = colBase + (ow % WarpsN) * 64 + tj * 16;
        const int tilesPerSide = rank / kHashTile;
        const uint32_t flat = (((row / rank) * (n / rank) + col / rank) * tilesPerSide + (row % rank) / kHashTile) *
                                  tilesPerSide + (col % rank) / kHashTile;
        tma_delivery::fusedScanTile(scanScratch + lt * ScanStride, flat, scanOut);
    }
#else
    if (tid < ScanTiles) {
        const int lt = tid, ow = lt >> 4, ti = (lt >> 2) & 3, tj = lt & 3;
        const int row = rowBase + (ow / WarpsN) * 64 + ti * 16;
        const int col = colBase + (ow % WarpsN) * 64 + tj * 16;
        const int tilesPerSide = rank / kHashTile;
        const uint32_t flat = (((row / rank) * (n / rank) + col / rank) * tilesPerSide + (row % rank) / kHashTile) *
                                  tilesPerSide + (col % rank) / kHashTile;
        uint32_t keyWords[8], blockWords[16], digest[8];
#pragma unroll
        for (int j = 0; j < 8; ++j) keyWords[j] = scanKey[j];
#pragma unroll
        for (int j = 0; j < 16; ++j) blockWords[j] = scanScratch[lt * ScanStride + j];
        blake3KeyedBlock(keyWords, blockWords, 64u, digest);
        bool wins = true;
        for (int j = 7; j >= 0; --j) {
            if (digest[j] < scanTarget[j]) break;
            if (digest[j] > scanTarget[j]) { wins = false; break; }
        }
        if (wins) {
            const uint32_t hitSlot = atomicAdd(scanHitCount, 1u);
            if (hitSlot < scanMaxHits) {
                scanHitIndex[hitSlot] = flat;
#pragma unroll
                for (int j = 0; j < 8; ++j) scanHitDigest[hitSlot * 8 + j] = digest[j];
            }
        }
    }
#endif
