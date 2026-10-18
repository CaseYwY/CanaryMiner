    // Test-only fold scheduling; every cumulative fold and update remains intact.
    static_assert(!kPhasedFold || (kWarpsM == 4 && kWarpsN == 4 &&
        kTilesM == 2 && kTilesN == 4 && kKBlock == 64 && kStagesP == 3 &&
        kGroupMP == 8 && !kFixedKRank && !kDirectFold &&
        kStaticCommonDim == 0 && !kStreamB && !kSwizzle128 && kMmaOrder == 0),
        "phased fold requires exact baseline tuple");
    if constexpr (kPhasedFold) {
        if (runtimeK != 8192 || runtimeRank != 128) return;
    }
// Intentionally no include guard: exact shared function-body text for the
// original global kernel and explicitly enabled inline-body experiments.
#if defined(CANARY_PEARL_TEST_WM8_REGCAP)
    static_assert(kWarpsM == 8 && kWarpsN == 2 && kTilesM == 2 && kTilesN == 4 &&
                  kKBlock == 64 && kStagesP == 3 && kGroupMP == 8 &&
                  !kFixedKRank && !kDirectFold,
                  "resource-only register cap is exact runtime WM8 only");
#if defined(__CUDA_ARCH__)
    static_assert(__CUDA_ARCH__ == 1200, "WM8 register resource probe is SM120 only");
#endif
#endif
    // Test-only constant-propagation experiment. No production launcher opts
    // in. Keep the runtime default and the complete pipeline/fold body shared;
    // do not silently reinterpret a mismatched caller's matrix stride.
    static_assert(!kFixedKRank || ptxFixedTupleAllowed(kWarpsM, kWarpsN,
                  kTilesM, kTilesN, kKBlock, kStagesP, kGroupMP),
                  "fixed experiment requires exact tile25 or WM8 tuple");
    static_assert(!kDirectFold || ptxDirectFoldTupleAllowed(kWarpsM, kWarpsN,
                  kTilesM, kTilesN, kKBlock, kStagesP, kGroupMP, kFixedKRank),
                  "direct fold requires the exact fixed K2048/rank128 tile25");
    static_assert(!kDirectFold || kTranscriptU32 == 16,
                  "direct fold requires exactly sixteen transcript slots");
    if constexpr (kFixedKRank) {
        if (runtimeK != 2048 || runtimeRank != 128) return;
    }
    // Default-off compile probe: preserve the pipeline and general fold, but
    // expose the current common dimension/rank to constant propagation.
    static_assert(kStaticCommonDim == 0 ||
                  (kStaticCommonDim == 8192 && !kFixedKRank && !kDirectFold &&
                   kWarpsM == 4 && kWarpsN == 4 && kTilesM == 2 && kTilesN == 4 &&
                   kKBlock == 64 && kStagesP == 3 && kGroupMP == 8),
                  "K8192 specialization is exact baseline tuple only");
    if constexpr (kStaticCommonDim != 0) {
        if (runtimeK != kStaticCommonDim || runtimeRank != 128) return;
    }
    // Test-only B-fragment lifetime experiment; never selected by production launchers.
    static_assert(!kStreamB || (kWarpsM == 4 && kWarpsN == 4 &&
                  kTilesM == 2 && kTilesN == 4 && kKBlock == 64 &&
                  kStagesP == 3 && kGroupMP == 8 && !kFixedKRank && !kDirectFold),
                  "streamed B probe requires the exact baseline tuple");
    if constexpr (kStreamB) {
        if (runtimeK != 8192 || runtimeRank != 128) return;
    }
    // Test-only unpadded BK128/two-stage layout. Preserve production defaults.
    static_assert(!kSwizzle128 || (kWarpsM == 4 && kWarpsN == 4 &&
                  kTilesM == 2 && kTilesN == 4 && kKBlock == 128 &&
                  kStagesP == 2 && kGroupMP == 8 && !kFixedKRank &&
                  !kDirectFold && !kStreamB && kStaticCommonDim == 0),
                  "swizzled BK128 probe requires exact tuple");
    if constexpr (kSwizzle128) {
        if (runtimeK != 8192 || runtimeRank != 128) return;
    }
    // Test-only: 0=unchanged, 1=same guard/original order, 2=half-major.
    // The guarded control separates constant propagation from instruction order.
    static_assert(kMmaOrder >= 0 && kMmaOrder <= 2, "unknown MMA order probe");
    static_assert(!kMmaOrder || (kWarpsM == 4 && kWarpsN == 4 &&
                  kTilesM == 2 && kTilesN == 4 && kKBlock == 64 &&
                  kStagesP == 3 && kGroupMP == 8 && !kFixedKRank &&
                  !kDirectFold && !kStreamB && !kSwizzle128 && kStaticCommonDim == 0),
                  "MMA half-major probe requires exact baseline tuple");
    if constexpr (kMmaOrder) {
        if (runtimeK != 8192 || runtimeRank != 128) return;
    }
    const int k = kStaticCommonDim ? kStaticCommonDim : (kFixedKRank ? 2048 : runtimeK);
    const int rank = (kStaticCommonDim || kFixedKRank) ? 128 : runtimeRank;
    constexpr int kK32 = 32;                       // the instruction's k
    constexpr int kBlockM = kWarpsM * kTilesM * kHashTile;
    constexpr int kBlockN = kWarpsN * kTilesN * kHashTile;
    constexpr int kThreads = kWarpsM * kWarpsN * 32;
    // Stages are a parameter because depth and stage count trade against one
    // shared-memory budget: 128 of k will not fit three stages of padded
    // shared, but it fits two. L2 throughput sits at ~91% while DRAM is 21%,
    // and that metric saturates on TRANSACTIONS rather than bytes - which is
    // why staging deeper keeps paying even though bytes per candidate are
    // unchanged.
    constexpr int kStages = kStagesP;
    // How much k one shared stage holds. Pearl's own reference GEMM uses a
    // k-block of 128; this kernel used 32, which is the mma's k and no more.
    // At 32 the loop takes a barrier every 32 of k, and each row's global read
    // is 32 contiguous bytes out of a 2048-byte row. Deeper stages mean fewer
    // barriers and longer contiguous reads for the same total traffic.
    constexpr int kSub = kKBlock / kK32;           // mma steps per stage
    // BOTH strides are padded. B was, A was not, and A at stride 32 put
    // ldmatrix's eight row addresses on banks 0,8,16,24,0,8,16,24 - a two-way
    // conflict that had been there since the fragment loads were written.
    constexpr int kAStride = kKBlock + (kSwizzle128 ? 0 : 16);
    constexpr int kBStride = kKBlock + (kSwizzle128 ? 0 : 16);

    const int warp = threadIdx.x >> 5;
    const int lane = threadIdx.x & 31;
    const int warpM = warp / kWarpsN;
    const int warpN = warp % kWarpsN;

    // Blocks are scheduled roughly in linear order, and the natural order is
    // the wrong one here. With grid (n/kBlockN, m/kBlockM), consecutive blocks
    // step along n, so a wave of concurrent blocks shares ONE A tile and pulls
    // a DIFFERENT B tile each - and B is the bigger of the two (kBlockN vs
    // kBlockM rows of k bytes).
    //
    // CORRECTION, measured later: this was justified at the time by L2
    // throughput reading 88-91%, which was read as L2 being the limiter. It is
    // not. The warp stall breakdown on the current kernel puts
    // long_scoreboard - the memory dependency stall - at 0.32 of 11.46 cycles
    // (2.8%), while math_pipe_throttle is 4.83 (42%). High L2 UTILISATION is
    // not the same as being L2 BOUND, and the swizzle measured +0.4%, ie.
    // nothing, which is what a non-bottleneck fix looks like. Kept because it
    // is harmless and costs one integer remap; do not cite it as a win.
    //
    // So walk down m in groups of kGroupM first. Within a group, kGroupM
    // consecutive blocks share one B tile, and a wave's distinct footprint
    // becomes one B tile plus kGroupM A tiles rather than the reverse.
    // Parameterised so the tuner can measure it: the right L2 reuse balance
    // depends on blockM, blockN, the mining shape and the card's L2 size, none
    // of which are fixed, so 8 was only ever a guess.
    constexpr int kGroupM = kGroupMP;
    const int blocksN = gridDim.x, blocksM = gridDim.y;
    const int linear = blockIdx.x + blockIdx.y * blocksN;
    const int perGroup = kGroupM * blocksN;
    const int mFirst = (linear / perGroup) * kGroupM;
    const int inGroup = linear % perGroup;
    // The last group is short whenever blocksM is not a multiple of kGroupM.
    // Dividing by kGroupM there would alias two blocks onto one tile and leave
    // another tile never computed - a wrong product, not a slow one.
    const int groupRows = min(kGroupM, blocksM - mFirst);
    const int bIdxM = mFirst + inGroup % groupRows;
    const int bIdxN = inGroup / groupRows;

    const int rowBase = bIdxM * kBlockM;
    const int colBase = bIdxN * kBlockN;
    if (rowBase >= m || colBase >= n) return;

    // Dynamic, because a padded A stride at any useful depth already exceeds
    // the 48 KB static limit. The launcher opts in via cudaFuncSetAttribute.
    extern __shared__ __align__(16) int8_t smem[];
    int8_t *const sAbase = smem;
    int8_t *const sBbase = smem + kStages * kBlockM * kAStride;
#define OM_PTX_SOFFSET(row, off) (kSwizzle128 ? ((off) ^ (((row) & 7) * 16)) : (off))
#define OM_SA(st) (sAbase + (st) * kBlockM * kAStride)
#define OM_SB(st) (sBbase + (st) * kBlockN * kBStride)

    const int dotLen = k - k % rank;
    const int totalSteps = dotLen / kK32;
    const int stepsPerRank = rank / kK32;
    if (totalSteps <= 0 || stepsPerRank <= 0) return;
    const int totalBlocks = totalSteps / kSub;     // whole stages of k
    if (totalBlocks <= 0 || totalSteps % kSub) return;

    // Two accumulators per 16x16 tile: columns 0-7 and 8-15.
    uint32_t acc[kTilesM][kTilesN][2][4] = {};
    uint32_t slot[kTilesM][kTilesN] = {};
    int reduction = 0;

// One 16-byte cp.async per chunk; kKBlock/16 chunks cover a row's slice.
#define OM_PTX_ISSUE(blk, stage)                                               \
    {                                                                          \
        constexpr int kChunks = kKBlock / 16;                                  \
        const int kk_ = (blk) * kKBlock;                                       \
        for (int i = threadIdx.x; i < kBlockM * kChunks; i += kThreads) {      \
            const int row = i / kChunks;                                       \
            const int off = (i % kChunks) * 16;                                \
            __pipeline_memcpy_async(OM_SA(stage) + row * kAStride + OM_PTX_SOFFSET(row, off),       \
                                    &A[(size_t)(rowBase + row) * k + kk_ + off], 16); \
        }                                                                      \
        for (int i = threadIdx.x; i < kBlockN * kChunks; i += kThreads) {      \
            const int col = i / kChunks;                                       \
            const int off = (i % kChunks) * 16;                                \
            __pipeline_memcpy_async(OM_SB(stage) + col * kBStride + OM_PTX_SOFFSET(col, off),       \
                                    &Bn[(size_t)(colBase + col) * k + kk_ + off], 16); \
        }                                                                      \
        __pipeline_commit();                                                   \
    }

#pragma unroll
    for (int pre = 0; pre < kStages - 1; pre++)
        if (pre < totalBlocks) OM_PTX_ISSUE(pre, pre)

    // Counters, not modulo. stepsPerRank is a runtime value, so `step %
    // stepsPerRank` compiles to a full signed integer division - IMAD.HI,
    // IABS, SHF, ISETP, SEL, about twenty instructions - and it sat in the
    // hot loop executing once per k-slice. The SASS mix was dominated by
    // division sequences rather than by the fold or the mma.
    int stage = 0;                       // step % kStages
    int issueStage = (kStages - 1) % kStages;
    int sinceFold = 0;                   // counts up to stepsPerRank
    for (int blk = 0; blk < totalBlocks; blk++) {
        // The pipeline DRAINS at the tail, and the steady-state wait is wrong
        // there. No new commit is issued for the last kStages-1 steps, so by
        // the final step the only commit still outstanding is the one holding
        // that step's own data - and wait_prior(kStages-2) permits exactly
        // that many to remain, so it returns without waiting for it.
        //
        // This cost 7% of found candidates: they failed the host re-check and
        // were correctly not submitted, but the work was thrown away. Only
        // transcript word 15 ever differed, because only the last fold reads
        // the racing buffer. The reference vectors never caught it - their k
        // is small enough that the copy has always landed - which is why the
        // gate now includes a differential run at the k and rank we mine at.
        //
        // Fixed by keeping the pipeline DEPTH constant instead of varying the
        // wait: the tail commits an empty group, which still counts as a
        // group, so one compile-time-constant wait stays correct to the end.
        // Branching on the wait instead pushed the kernel over the register
        // budget and it stopped launching at 512 threads.
        __pipeline_wait_prior(kStages - 2);
        __syncthreads();

        // One ldmatrix per fragment instead of four scalar loads. The lane
        // splits into which row-of-eight it addresses (r) and which of the
        // four 8x8 tiles it feeds (q); the tile index chooses the row half and
        // the k half, in exactly the order the mma wants its four registers.
        const int ldGroup = lane >> 3;
        const int ldRow = (lane & 7) + (ldGroup & 1) * 8;
        const int ldOff = (ldGroup >> 1) * 16;
        // B's mma pairs are (low-cols low/high-k), then high-cols. Swap the
        // two group bits so ldmatrix returns each pair in adjacent registers.
        const int bLdRow = (lane & 7) + (ldGroup >> 1) * 8;
        const int bLdOff = (ldGroup & 1) * 16;

        // One shared stage now holds kSub of the mma's 32-deep k steps, so the
        // barrier and the global copies happen once per kKBlock instead of
        // once per 32. Everything inside is per-32 exactly as before.
#pragma unroll
        for (int sub = 0; sub < kSub; sub++) {
        const int kOffSub = sub * kK32;
        if constexpr (kStreamB) {
            uint32_t aReg[kTilesM][4];
#pragma unroll
            for (int i = 0; i < kTilesM; ++i) {
                const int row = (warpM * kTilesM + i) * kHashTile + ldRow;
                ldmatrixX4(aReg[i], OM_SA(stage) + row * kAStride + OM_PTX_SOFFSET(row, kOffSub + ldOff));
            }
#pragma unroll
            for (int j = 0; j < kTilesN; ++j) {
                uint32_t t[4], fragment[2][2];
                const int col = (warpN * kTilesN + j) * kHashTile + ldRow;
                ldmatrixX4(t, OM_SB(stage) + col * kBStride + OM_PTX_SOFFSET(col, kOffSub + ldOff));
                fragment[0][0] = t[0]; fragment[1][0] = t[1];
                fragment[0][1] = t[2]; fragment[1][1] = t[3];
#pragma unroll
                for (int i = 0; i < kTilesM; ++i)
#pragma unroll
                    for (int h = 0; h < 2; ++h)
                        mmaS8(acc[i][j][h], aReg[i], fragment[h], acc[i][j][h]);
            }
        } else {
        uint32_t aReg[kTilesM][4], bReg[kTilesN][4];
#pragma unroll
        for (int i = 0; i < kTilesM; i++) {
            const int row = (warpM * kTilesM + i) * kHashTile + ldRow;
            ldmatrixX4(aReg[i], OM_SA(stage) + row * kAStride + OM_PTX_SOFFSET(row, kOffSub + ldOff));
        }
#pragma unroll
        for (int j = 0; j < kTilesN; j++) {
            // B is n-major, so its "rows" are columns of B and the same
            // ldmatrix serves it. The four tiles come back as (cols 0-7, low
            // k), (cols 8-15, low k), (cols 0-7, high k), (cols 8-15, high k),
            // which is bReg[j][h][e] with h the column half and e the k half -
            // hence the deinterleave rather than a straight copy.
            //
            // For the record of what the layout is, since the loads no longer
            // spell it out: groupID picks the COLUMN and threadID_in_group
            // picks k, the opposite of A. Established by running a single mma
            // against a CPU product (scratchpad/mmaprobe.cu), not by reading
            // the operand order - three plausible readings were all wrong.
            const int col = (warpN * kTilesN + j) * kHashTile + bLdRow;
            ldmatrixX4(bReg[j], OM_SB(stage) + col * kBStride +
                       OM_PTX_SOFFSET(col, kOffSub + bLdOff));
        }
        if constexpr (kMmaOrder == 2) {
            // Preserve each accumulator's k-order; interleave column halves
            // across tiles rather than issuing adjacent halves of one tile.
#pragma unroll
            for (int h = 0; h < 2; h++)
#pragma unroll
                for (int i = 0; i < kTilesM; i++)
#pragma unroll
                    for (int j = 0; j < kTilesN; j++)
                        mmaS8(acc[i][j][h], aReg[i], &bReg[j][h * 2],
                              acc[i][j][h]);
        } else {
#pragma unroll
        for (int i = 0; i < kTilesM; i++)
#pragma unroll
            for (int j = 0; j < kTilesN; j++)
#pragma unroll
                for (int h = 0; h < 2; h++)
                    mmaS8(acc[i][j][h], aReg[i], &bReg[j][h * 2],
                          acc[i][j][h]);
        }

        }

        if (++sinceFold == stepsPerRank) {
            sinceFold = 0;
            if constexpr (kPhasedFold) {
                uint32_t partial[kTilesM][kTilesN];
#pragma unroll
                for (int i = 0; i < kTilesM; ++i)
#pragma unroll
                    for (int j = 0; j < kTilesN; ++j)
                        partial[i][j] = xor3(
                            xor3(acc[i][j][0][0], acc[i][j][0][1], acc[i][j][0][2]),
                            xor3(acc[i][j][0][3], acc[i][j][1][0], acc[i][j][1][1]),
                            acc[i][j][1][2] ^ acc[i][j][1][3]);
#pragma unroll
                for (int i = 0; i < kTilesM; ++i)
#pragma unroll
                    for (int j = 0; j < kTilesN; ++j) {
                        uint32_t v = warpXor(partial[i][j]);
                        if (lane == reduction % kTranscriptU32)
                            slot[i][j] = rotl32(slot[i][j], kRotation) ^ v;
                    }
            } else {
#pragma unroll
            for (int i = 0; i < kTilesM; i++) {
#pragma unroll
                for (int j = 0; j < kTilesN; j++) {
                    // Four instructions, not seven. The shape is fixed - two
                    // n-halves of four accumulator registers - so this is
                    // written out rather than looped.
                    uint32_t v = warpXor(
                        xor3(xor3(acc[i][j][0][0], acc[i][j][0][1], acc[i][j][0][2]),
                             xor3(acc[i][j][0][3], acc[i][j][1][0], acc[i][j][1][1]),
                             acc[i][j][1][2] ^ acc[i][j][1][3]));
                    // The rotate and XOR here are algebraically DEAD when the
                    // fold count does not exceed the sixteen lane slots: each
                    // lane is then written exactly once with slot still zero,
                    // so rotl32(0,13) ^ v is just v. k=2048 at rank 128 is
                    // exactly 16 folds. TRIED AND REVERTED - expressing the
                    // special case as a ternary cost 128 MORE SASS
                    // instructions (+4.7%) and introduced local-memory spills,
                    // because it forces both paths to be materialised inside a
                    // fully unrolled double loop. ptxas already handles the
                    // general form better than the "optimised" one. Left as
                    // the general expression on purpose.
                    if constexpr (kDirectFold) {
                        // Exactly16 reductions, numbered0..15, and zero-initialized
                        // slots: each destination lane is written exactly once.
                        // This is test-only compile-time selection, not a runtime
                        // ternary materializing both update paths.
                        if (lane == reduction) slot[i][j] = v;
                    } else {
                        if (lane == reduction % kTranscriptU32)
                            slot[i][j] = rotl32(slot[i][j], kRotation) ^ v;
                    }
                }
            }
            }
            ++reduction;
        }
        }   // sub

        if (blk + kStages - 1 < totalBlocks)
            OM_PTX_ISSUE(blk + kStages - 1, issueStage)
        else
            __pipeline_commit();   // empty group, purely to hold the depth

        if (++stage == kStages) stage = 0;
        if (++issueStage == kStages) issueStage = 0;
    }
#undef OM_PTX_ISSUE
#undef OM_PTX_SOFFSET
#undef OM_SA
#undef OM_SB

#if defined(OM_PEARL_FUSED_SCAN_V1_BODY)
    // Every cp.async issuer drains, then the whole CTA stops reading matrix
    // staging before reusing its storage. Empty tail groups are drained too.
    __pipeline_wait_prior(0);
    __syncthreads();
    constexpr int kScanTiles = kWarpsM*kWarpsN*kTilesM*kTilesN;
    // Padding one word removes the 16-word stride's bank conflicts when one
    // hash thread reads each tile. This fits inside the existing 92160 bytes.
    constexpr int kScanStride = kTranscriptU32+1;
    static_assert(kScanTiles*kScanStride*sizeof(uint32_t) <=
        kStages*(kBlockM*kAStride+kBlockN*kBStride), "scan scratch exceeds staging");
    uint32_t *const scanScratch = reinterpret_cast<uint32_t *>(smem);
#endif

#pragma unroll
    for (int i = 0; i < kTilesM; i++) {
#pragma unroll
        for (int j = 0; j < kTilesN; j++) {
            const int row = rowBase + (warpM * kTilesM + i) * kHashTile;
            const int col = colBase + (warpN * kTilesN + j) * kHashTile;
            if (writeC) {
#pragma unroll
                for (int h = 0; h < 2; h++) {
                    const int r = row + (lane >> 2);
                    const int c = col + h * 8 + (lane & 3) * 2;
                    cNoised[(size_t)r * n + c] = (int32_t)acc[i][j][h][0];
                    cNoised[(size_t)r * n + c + 1] = (int32_t)acc[i][j][h][1];
                    cNoised[(size_t)(r + 8) * n + c] = (int32_t)acc[i][j][h][2];
                    cNoised[(size_t)(r + 8) * n + c + 1] = (int32_t)acc[i][j][h][3];
                }
            }
            if (lane < kTranscriptU32) {
                const int blocksPerRow = n / rank;
                const int iIdx = row / rank;
                const int jIdx = col / rank;
                const int hi = (row % rank) / kHashTile;
                const int wi = (col % rank) / kHashTile;
                const int tilesPerSide = rank / kHashTile;
                const int flat =
                    ((iIdx * blocksPerRow + jIdx) * tilesPerSide + hi) * tilesPerSide + wi;
                transcripts[flat * kTranscriptU32 + lane] = slot[i][j];
#if defined(OM_PEARL_FUSED_SCAN_V1_BODY)
                const int localTile=(warp*kTilesM+i)*kTilesN+j;
                scanScratch[localTile*kScanStride+lane]=slot[i][j];
#endif
            }
        }
    }

#if defined(OM_PEARL_FUSED_SCAN_V1_BODY)
    // The 16 lanes owning canonical words publish before any hash reads them.
    __syncthreads();
    if (threadIdx.x < kScanTiles) {
        const unsigned localTile=threadIdx.x;
        const unsigned ownerWarp=localTile/(kTilesM*kTilesN);
        const unsigned tileInWarp=localTile%(kTilesM*kTilesN);
        const int row=rowBase+((ownerWarp/kWarpsN)*kTilesM+tileInWarp/kTilesN)*kHashTile;
        const int col=colBase+((ownerWarp%kWarpsN)*kTilesN+tileInWarp%kTilesN)*kHashTile;
        const int tilesPerSide=rank/kHashTile;
        const uint32_t flat=(((row/rank)*(n/rank)+col/rank)*tilesPerSide+
            (row%rank)/kHashTile)*tilesPerSide+(col%rank)/kHashTile;
        uint32_t keyWords[8], blockWords[16], digest[8];
#pragma unroll
        for (int j=0;j<8;++j) keyWords[j]=scanKey[j];
#pragma unroll
        for (int j=0;j<16;++j) blockWords[j]=scanScratch[localTile*kScanStride+j];
        // Same single-block keyed BLAKE3 as powScan: counter 0, length 64,
        // KEYED_HASH|CHUNK_START|CHUNK_END|ROOT. Digest is unsigned little endian.
        blake3KeyedBlock(keyWords,blockWords,64u,digest);
        bool wins=true;
        for (int j=7;j>=0;--j) {
            if (digest[j] < scanTarget[j]) break;
            if (digest[j] > scanTarget[j]) { wins=false; break; }
        }
        if (wins) {
            // Observed hits include overflow; payload writes remain bounded.
            const uint32_t hitSlot=atomicAdd(scanHitCount,1u);
            if (hitSlot < scanMaxHits) {
                scanHitIndex[hitSlot]=flat;
#pragma unroll
                for (int j=0;j<8;++j) scanHitDigest[hitSlot*8+j]=digest[j];
            }
        }
    }
#endif
