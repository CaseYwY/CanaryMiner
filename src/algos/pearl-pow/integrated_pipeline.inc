// Included inside PearlPow. The CPU owner retains this search's proof authority;
// only the next attempt's preparation/compute may be speculative. No host worker
// accesses mutable job, ledger, CUDA buffers or the proof binding callback.
    static constexpr size_t kIntegratedOpeningBytes =
        size_t(kTileSide) * kCudaCommonDim + CompactProofPlan::maxSiblings * 32;
    static constexpr size_t kIntegratedMetaOffset = 2 * kIntegratedOpeningBytes;
    static constexpr size_t kIntegratedSiblingOffset = kIntegratedMetaOffset + 192;
    static constexpr size_t kIntegratedHostBytes = kIntegratedSiblingOffset +
        2 * CompactProofPlan::maxSiblings * sizeof(uint64_t);
    struct IntegratedCapsule {
        uint8_t *host = nullptr;
        uint8_t *device = nullptr;
        uint64_t *siblings = nullptr;
        cudaEvent_t ready = nullptr, copied = nullptr;
        CompactProofPlan plans[2];
        Job job;
        uint64_t nonce = 0, generation = 0;
        uint8_t key[32] = {};
        U256 bound;
        bool valid = false;
    };
    const IntegratedPipelineMode integratedMode_ =
        parseIntegratedPipeline(getenv("CANARY_PEARL_INTEGRATED_PIPELINE"));
    IntegratedCapsule integratedCapsules_[2];
    cudaStream_t copyStream_ = nullptr;
    cudaEvent_t integratedDone_ = nullptr;
    uint32_t *integratedHitCount_ = nullptr;
    PearlSpeculativeAttemptSlot integratedAttempt_;
    PreparedJobHost integratedPrepared_;
    struct IntegratedQueuedPreparation {
        Job job;
        PreparedJobHost prepared;
        uint64_t nonce = 0, generation = 0, plan = 0;
        unsigned slot = 0;
        bool pending = false;
    } integratedQueuedPreparation_;
    uint64_t integratedPreparationsQueued_ = 0, integratedPreparationsCancelled_ = 0;
    unsigned integratedSlot_ = 0, integratedCurrentSlot_ = 0;
    bool integratedSearchActive_ = false, integratedAnnounced_ = false;
    bool integratedDoneRecorded_ = false;
    IntegratedPipelineHostTimings integratedTimings_;
    uint64_t integratedCpuFinalizationStarted_ = 0;
    static uint64_t integratedNowNs() noexcept {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    static void integratedElapsed(uint64_t started, uint64_t *count, uint64_t *total) noexcept {
        const uint64_t elapsed = integratedNowNs()-started;
        if (*count != UINT64_MAX) ++*count;
        *total = elapsed > UINT64_MAX-*total ? UINT64_MAX : *total+elapsed;
    }
    bool waitIntegratedComputeStream() noexcept {
        const auto started = integratedNowNs();
        const auto status = cudaStreamSynchronize(stream_);
        integratedElapsed(started,&integratedTimings_.computeWaits,&integratedTimings_.computeWaitNs);
        return status == cudaSuccess;
    }

    bool integratedConserved() const noexcept {
        const uint64_t pending = integratedQueuedPreparation_.pending ? 1 : 0;
        return integratedAttempt_.conserved() &&
            integratedPreparationsQueued_ >= integratedPreparationsCancelled_ &&
            integratedPreparationsQueued_-integratedPreparationsCancelled_ >= pending &&
            integratedPreparationsQueued_-integratedPreparationsCancelled_-pending ==
                integratedAttempt_.counters().reserved;
    }
    bool integratedEnabled() const noexcept {
        return integratedPipelineEnabled(integratedMode_);
    }
    bool integratedEnvironmentValid() const noexcept {
        if (parseIntegratedPipeline(getenv("CANARY_PEARL_INTEGRATED_PIPELINE")) != integratedMode_ ||
            integratedMode_ == IntegratedPipelineMode::Invalid) return false;
        if (!integratedEnabled()) return true;
        const char *compact = getenv("CANARY_PEARL_COMPACT_READBACK");
        return kIntegratedPipelineCompiled && compact && !strcmp(compact,"1") &&
            !formalRequest_.enabled && !syntheticRun_ &&
            privateExecution_ == PearlPrivateExecution::Disabled &&
            publicExecution_ == PearlPublicExecution::Disabled &&
            !getenv("CANARY_PEARL_ATTEMPT_OVERLAP") && !getenv("CANARY_PEARL_A_ROOT_FUSION") &&
            !getenv("CANARY_PEARL_EXPERIMENT") && !getenv("CANARY_PEARL_PROFILE_INTERVAL") &&
            !getenv("CANARY_PEARL_STAGE_TIMING") && !getenv("CANARY_PEARL_HARD_TARGET") &&
            !getenv("CANARY_PEARL_BENCH_ALL_HIT");
    }
    bool integratedAdmission(const RunOptions &options) const noexcept {
        if (!integratedEnvironmentValid()) return false;
        if (!integratedEnabled()) return true;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        return pearlIntegratedPipelineAdmission(pearlParseIntegratedPipeline(
            getenv("CANARY_PEARL_INTEGRATED_PIPELINE")), pearlIntegratedPipelineContext(
            options,name(),kIntegratedPipelineCompiled,options.pearlGuardRetirementSession != nullptr));
#else
        return false;
#endif
    }
    bool integratedSelectionValid() const noexcept {
        return !integratedEnabled() || (integratedEnvironmentValid() &&
            tile_ < kCudaPreparationPlanCount && integratedPipelineShape(arch10_, shape_.m, shape_.n, kK, kRank,
                                    tileConfig(tile_).preparationPlanId));
    }
    bool allocateIntegrated() {
        if (!integratedEnabled()) return true;
        if (!integratedSelectionValid() || !compactReadback_) return false;
        if (cudaStreamCreateWithFlags(&copyStream_,cudaStreamNonBlocking) != cudaSuccess ||
            cudaEventCreateWithFlags(&integratedDone_,cudaEventDisableTiming) != cudaSuccess ||
            cudaHostAlloc(reinterpret_cast<void **>(&integratedHitCount_),sizeof(uint32_t),cudaHostAllocDefault) != cudaSuccess)
            return false;
        *integratedHitCount_ = 0;
        for (auto &c : integratedCapsules_) {
            if (!alloc(&c.device,2*kIntegratedOpeningBytes) ||
                !alloc(&c.siblings,2*CompactProofPlan::maxSiblings*sizeof(uint64_t)) ||
                cudaHostAlloc(reinterpret_cast<void **>(&c.host),kIntegratedHostBytes,cudaHostAllocDefault) != cudaSuccess ||
                cudaEventCreateWithFlags(&c.ready,cudaEventDisableTiming) != cudaSuccess ||
                cudaEventCreateWithFlags(&c.copied,cudaEventDisableTiming) != cudaSuccess)
                return false;
        }
        return true;
    }
    void releaseIntegrated() noexcept {
        // release() has drained every stream before touching host destinations.
        for (auto &c : integratedCapsules_) {
            if (c.ready && cudaEventDestroy(c.ready) != cudaSuccess) publication_.poison();
            if (c.copied && cudaEventDestroy(c.copied) != cudaSuccess) publication_.poison();
            if (c.host && cudaFreeHost(c.host) != cudaSuccess) publication_.poison();
            c.ready = c.copied = nullptr; c.host = nullptr;
            c.device = nullptr; c.siblings = nullptr; c.valid = false;
        }
        if (integratedDone_ && cudaEventDestroy(integratedDone_) != cudaSuccess) publication_.poison();
        integratedDone_ = nullptr;
        if (integratedHitCount_ && cudaFreeHost(integratedHitCount_) != cudaSuccess) publication_.poison();
        integratedHitCount_ = nullptr;
        if (copyStream_ && cudaStreamDestroy(copyStream_) != cudaSuccess) publication_.poison();
        copyStream_ = nullptr;
    }
    void integratedDiagnostic(const char *event) const noexcept {
        if (!integratedEnabled()) return;
        const auto &c = integratedAttempt_.counters();
        fprintf(stderr,"[pearl-integrated] v=1 event=%s launched=%llu physically_completed=%llu promoted=%llu "
            "discarded=%llu discarded_completed=%llu discarded_unknown=%llu observed_hits=%llu "
            "promoted_hits=%llu discarded_hits=%llu reserved=%llu cancelled=%llu pending=%u conserved=%u "
            "preparations_queued=%llu preparations_cancelled=%llu preparation_pending=%u\n",
            event,(unsigned long long)c.launched,(unsigned long long)c.physicallyCompleted,
            (unsigned long long)c.promoted,(unsigned long long)c.discarded,
            (unsigned long long)c.discardedCompleted,(unsigned long long)c.discardedCompletionUnknown,
            (unsigned long long)c.observedHits,(unsigned long long)c.promotedObservedHits,
            (unsigned long long)c.discardedObservedHits,(unsigned long long)c.reserved,
            (unsigned long long)c.cancelledBeforeLaunch,integratedAttempt_.pending()?1:0,
            integratedConserved()?1:0,(unsigned long long)integratedPreparationsQueued_,
            (unsigned long long)integratedPreparationsCancelled_,integratedQueuedPreparation_.pending?1:0);
    }
    bool finishIntegratedAttempt() noexcept {
        if (integratedAttempt_.state() == PearlSpeculativeAttemptSlot::State::Ready) return true;
        if (integratedAttempt_.state() != PearlSpeculativeAttemptSlot::State::Running ||
            !integratedDone_ || !integratedDoneRecorded_ || !integratedHitCount_) return false;
        const auto started = integratedNowNs();
        const auto status = cudaEventSynchronize(integratedDone_);
        integratedElapsed(started,&integratedTimings_.computeWaits,&integratedTimings_.computeWaitNs);
        if (status != cudaSuccess) { publication_.poison(); return false; }
        return integratedAttempt_.markCompleted(*integratedHitCount_);
    }
    bool discardIntegratedAttempt() noexcept {
        if (!integratedAttempt_.pending() && !integratedQueuedPreparation_.pending) return true;
        bool completed = true;
        if (integratedAttempt_.state() == PearlSpeculativeAttemptSlot::State::Running)
            completed = finishIntegratedAttempt();
        const bool drained = drainAttemptStreams();
        // A failed CUDA completion must remain explicitly unknown. Always
        // clear ownership after the attempted drain; poisoned instances die.
        const bool discarded = integratedAttempt_.discardDrained();
        if (integratedQueuedPreparation_.pending) {
            integratedQueuedPreparation_.pending = false;
            if (integratedPreparationsCancelled_ != UINT64_MAX) ++integratedPreparationsCancelled_;
            else publication_.poison();
        }
        integratedDiagnostic("discard");
        return completed && drained && discarded && integratedConserved();
    }
    bool integratedJobMatches(const Job &job, uint64_t nonce) const noexcept {
        return integratedEnvironmentValid() && publication_.matches(integratedPrepared_) &&
            staging_ == integratedPrepared_ && integratedAttempt_.matches(
                job,nonce,wins_.generation(),tileConfig(tile_).preparationPlanId);
    }
    bool queueIntegratedNext(const Job &job, bool hint, uint64_t nonce,
                             unsigned currentSlot, uint64_t currentNonce) {
        if (!hint || nonce == currentNonce) return true;
        auto &queued = integratedQueuedPreparation_;
        if (wins_.generation() == UINT64_MAX || queued.pending ||
            integratedPreparationsQueued_ == UINT64_MAX) return false;
        // This preparation may overlap a cached current GEMM. Keep a distinct
        // bounded owner until that GEMM promotes and its completion slot is
        // free. Copy every allocating identity before enqueueing preparation.
        queued.job = job;
        queued.prepared = staging_;
        queued.nonce = nonce; queued.generation = wins_.generation()+1;
        queued.plan = tileConfig(tile_).preparationPlanId;
        queued.slot = currentSlot ^ 1;
        queued.pending = true;
        ++integratedPreparationsQueued_;
#ifdef CANARY_PEARL_TEST_TRANSCRIPT_SNAPSHOT
        testPreparedThreads_[queued.slot] = 256;
#endif
        const auto started = integratedNowNs();
        const bool prepared = prepareA(aSlot(queued.slot),nonce,prepStream_,false) &&
            cudaEventRecord(aReady_[queued.slot],prepStream_) == cudaSuccess;
        integratedElapsed(started,&integratedTimings_.preparationEnqueues,&integratedTimings_.preparationEnqueueNs);
        return prepared;
    }
    bool adoptIntegratedPreparation() {
        auto &queued = integratedQueuedPreparation_;
        if (!queued.pending) return true;
        if (integratedAttempt_.pending() || !publication_.matches(queued.prepared) ||
            !(staging_ == queued.prepared)) return false;
        integratedPrepared_ = queued.prepared;
        if (!integratedAttempt_.reserve(queued.job,queued.nonce,queued.generation,queued.plan)) return false;
        integratedSlot_ = queued.slot;
        queued.pending = false; // Ownership transferred; do not prepare twice.
        return true;
    }
    bool launchIntegratedNext() {
        if (!integratedAttempt_.pending()) return true;
        if (integratedAttempt_.state() != PearlSpeculativeAttemptSlot::State::Reserved) return false;
        integratedDoneRecorded_ = false;
        if (cudaStreamWaitEvent(stream_,aReady_[integratedSlot_],0) != cudaSuccess ||
            cudaMemsetAsync(dHitCount_,0,sizeof(uint32_t),stream_) != cudaSuccess) return false;
        bool gemmEnqueued = false;
        const bool computeEnqueued = computeA(aSlot(integratedSlot_),false,&gemmEnqueued);
        // A successful GEMM followed by a failed scan/readback enqueue is
        // launched work with unknown completion, never a prelaunch cancel.
        if (gemmEnqueued && !integratedAttempt_.markLaunched()) return false;
        if (!computeEnqueued || !gemmEnqueued) return false;
        integratedDoneRecorded_ = cudaMemcpyAsync(integratedHitCount_,dHitCount_,sizeof(uint32_t),
                    cudaMemcpyDeviceToHost,stream_) == cudaSuccess &&
            cudaEventRecord(integratedDone_,stream_) == cudaSuccess;
        return integratedDoneRecorded_;
    }
    bool searchIntegrated(const Job &job, uint64_t nonce, uint64_t attempts,
                          std::vector<Solution> *out) noexcept {
        const size_t before = out->size();
        if (attempts != 1 || !integratedSelectionValid() || !prepStream_ ||
            !copyStream_ || !integratedHitCount_ || completedAttempts_ == UINT64_MAX)
            return failSearch();
        const bool hint = nextHintValid_;
        const uint64_t next = nextHintNonce_;
        nextHintValid_ = false;
        integratedSearchActive_ = true;
        integratedCpuFinalizationStarted_ = 0;
        bool ok = false;
        try {
            if (!integratedAnnounced_) { integratedAnnounced_=true; integratedDiagnostic("begin"); }
            const bool cached = integratedAttempt_.pending();
            unsigned slot = cached ? integratedSlot_ : 0;
            if (cached) {
                if (!integratedJobMatches(job,nonce)) throw 0;
                progress(PearlAlgorithmProgressEvent::AttemptStarted);
                // The current cached GEMM is still running. Its other A slot
                // was retired by the previous synchronous CPU finalizer.
                if (!queueIntegratedNext(job,hint,next,slot,nonce) ||
                    !finishIntegratedAttempt()) throw 0;
                hitReadback_ = static_cast<uint32_t>(integratedAttempt_.observedHits());
                if (!integratedAttempt_.promote(job,nonce,wins_.generation(),
                                                 tileConfig(tile_).preparationPlanId)) throw 0;
            } else {
                progress(PearlAlgorithmProgressEvent::AttemptStarted);
#ifdef CANARY_PEARL_TEST_TRANSCRIPT_SNAPSHOT
                testPreparedThreads_[slot] = 256;
#endif
                if (!prepareA(aSlot(slot),nonce,prepStream_,false) ||
                    cudaEventRecord(aReady_[slot],prepStream_) != cudaSuccess ||
                    cudaStreamWaitEvent(stream_,aReady_[slot],0) != cudaSuccess ||
                    cudaMemsetAsync(dHitCount_,0,sizeof(uint32_t),stream_) != cudaSuccess ||
                    !computeA(aSlot(slot),false) ||
                    cudaMemcpyAsync(integratedHitCount_,dHitCount_,sizeof(uint32_t),
                                    cudaMemcpyDeviceToHost,stream_) != cudaSuccess) throw 0;
            }
            // Separate raw/noised A slots; the serial prep stream alone owns
            // scratch/noise/permutation temporaries, while GEMM reads A/B only.
            if (!cached) {
                if (!queueIntegratedNext(job,hint,next,slot,nonce) ||
                    !waitIntegratedComputeStream()) throw 0;
                hitReadback_ = *integratedHitCount_;
            }
            if (!adoptIntegratedPreparation()) throw 0;
            ++completedAttempts_;
            progress(PearlAlgorithmProgressEvent::AttemptCompleted);
            const HitWindow window = hitWindow(hitReadback_);
            progress(PearlAlgorithmProgressEvent::HitsObserved,window.observed);
            progress(PearlAlgorithmProgressEvent::HitsRetained,window.retained);
            if (window.saturated) progress(PearlAlgorithmProgressEvent::HitBufferSaturated,
                                          window.observed-window.retained);
            if (!captureRetiredAttempt(nonce,aSlot(slot),true,slot)) throw 0;
            integratedCurrentSlot_ = slot;
            integratedCapsules_[slot].valid = false;
            if (window.retained) {
                progress(PearlAlgorithmProgressEvent::FinalizerStarted);
                ok = openWin(job,nonce,window,out,aSlot(slot));
            } else ok = launchIntegratedNext();
            if (ok && !integratedConserved()) ok = false;
        } catch (...) { ok = false; }
        if (integratedCpuFinalizationStarted_)
            integratedElapsed(integratedCpuFinalizationStarted_,&integratedTimings_.cpuFinalizers,
                              &integratedTimings_.cpuFinalizationNs);
        integratedSearchActive_ = false;
        if (!ok) { out->resize(before); return failDeviceSearch(); }
        if ((completedAttempts_ & 127) == 0) integratedDiagnostic("progress");
        return true;
    }

    bool gatherIntegratedOpening(IntegratedCapsule &c, unsigned side,
            const uint8_t *data, size_t bytes, uint32_t firstRow,
            uint8_t *tree, bool reuseTree) {
        if (side > 1) return false;
        auto &plan = c.plans[side];
        const auto leaves = leafIndicesFromRows(firstRow,kTileSide,kK);
        if (leaves.empty() ||
            !compactProofPlan(bytes/kChunkLen,leaves.front(),leaves.size(),&plan) ||
            plan.leafBytes() != size_t(kTileSide)*kK ||
            plan.openingBytes() > kIntegratedOpeningBytes ||
            plan.siblingNodes.size() > CompactProofPlan::maxSiblings) return false;
        auto *hostSiblings = reinterpret_cast<uint64_t *>(c.host+kIntegratedSiblingOffset) +
            side*CompactProofPlan::maxSiblings;
        std::memcpy(hostSiblings,plan.siblingNodes.data(),plan.siblingNodes.size()*sizeof(uint64_t));
        auto *deviceSiblings = c.siblings+side*CompactProofPlan::maxSiblings;
        const uint32_t chunks = static_cast<uint32_t>(plan.total);
        if (!reuseTree) {
            chunkCvs<<<(chunks+255)/256,256,0,stream_>>>(data,chunks,
                reinterpret_cast<const uint32_t *>(dJobKey_),tree);
            if (cudaGetLastError() != cudaSuccess) return false;
            size_t offset=0;
            for (uint32_t width=chunks; width>2; width/=2) {
                reduceProofLevel<<<((width/2)+255)/256,256,0,stream_>>>(tree+offset*32,width/2,
                    reinterpret_cast<const uint32_t *>(dJobKey_),tree+(offset+width)*32);
                if (cudaGetLastError() != cudaSuccess) return false;
                offset+=width;
            }
        }
        if (cudaMemcpyAsync(deviceSiblings,hostSiblings,plan.siblingNodes.size()*sizeof(uint64_t),
                             cudaMemcpyHostToDevice,stream_) != cudaSuccess) return false;
        gatherProofOpening<<<(plan.openingBytes()+255)/256,256,0,stream_>>>(data,tree,
            plan.first*kChunkLen,static_cast<uint32_t>(plan.leafBytes()),deviceSiblings,
            static_cast<uint32_t>(plan.siblingNodes.size()),c.device+side*kIntegratedOpeningBytes);
        return cudaGetLastError() == cudaSuccess;
    }
    bool readIntegratedOpenings(const Job &job, uint64_t nonce, const ASlot &a,
            uint32_t row, uint32_t col, uint32_t flat, MerkleProof *aProof, MerkleProof *bProof) {
        auto &c = integratedCapsules_[integratedCurrentSlot_];
        c.valid = false;
        c.job = job; c.nonce = nonce; c.generation = wins_.generation();
        std::memcpy(c.key,jobKey_,32); c.bound = bound_;
        if (!gatherIntegratedOpening(c,0,reinterpret_cast<const uint8_t *>(a.raw),
                size_t(shape_.m)*kK,row,dProofTreeA_,false) ||
            !gatherIntegratedOpening(c,1,reinterpret_cast<const uint8_t *>(dBt_),
                size_t(shape_.n)*kK,col,dProofTreeB_,bCompactTreeValid_)) return failDeviceSearch();
        // Only these 192 bytes depend on mutable roots/commit/transcript
        // buffers. Capture them on the compute stream before its successor
        // may overwrite those outputs. The large openings have separate,
        // stable device staging and can copy concurrently with that GEMM.
        auto *meta = c.host+kIntegratedMetaOffset;
        if (cudaMemcpyAsync(meta,a.commit,32,cudaMemcpyDeviceToHost,stream_) != cudaSuccess ||
            cudaMemcpyAsync(meta+32,dCommitB_,32,cudaMemcpyDeviceToHost,stream_) != cudaSuccess ||
            cudaMemcpyAsync(meta+64,a.root,32,cudaMemcpyDeviceToHost,stream_) != cudaSuccess ||
            cudaMemcpyAsync(meta+96,dRoots_+32,32,cudaMemcpyDeviceToHost,stream_) != cudaSuccess ||
            cudaMemcpyAsync(meta+128,dTranscripts_+size_t(flat)*kTranscriptWords,64,cudaMemcpyDeviceToHost,stream_) != cudaSuccess ||
            cudaEventRecord(c.ready,stream_) != cudaSuccess ||
            cudaStreamWaitEvent(copyStream_,c.ready,0) != cudaSuccess) return failDeviceSearch();
        for (unsigned side=0; side<2; ++side)
            if (cudaMemcpyAsync(c.host+side*kIntegratedOpeningBytes,c.device+side*kIntegratedOpeningBytes,
                    c.plans[side].openingBytes(),cudaMemcpyDeviceToHost,copyStream_) != cudaSuccess)
                return failDeviceSearch();
        if (cudaEventRecord(c.copied,copyStream_) != cudaSuccess ||
            !launchIntegratedNext()) return failDeviceSearch();
        // Copy destinations and device opening staging remain alive through
        // completion on every branch. The next GPU attempt can now run while
        // reconstruction, recheck, root checks and serialization use only host
        // data. This search owns job/key/bound/ledger until it returns.
        const auto copyWaitStarted = integratedNowNs();
        const auto copyStatus = cudaEventSynchronize(c.copied);
        integratedElapsed(copyWaitStarted,&integratedTimings_.compactCopyWaits,&integratedTimings_.compactCopyWaitNs);
        if (copyStatus != cudaSuccess) return failDeviceSearch();
        integratedCpuFinalizationStarted_ = integratedNowNs();
        if (c.generation != wins_.generation() || c.nonce != nonce ||
            std::memcmp(c.key,jobKey_,32) || std::memcmp(c.bound.v,bound_.v,sizeof(bound_.v)) ||
            !reconstructCompactProof(c.plans[0],c.host,c.plans[0].openingBytes(),c.key,aProof) ||
            !reconstructCompactProof(c.plans[1],c.host+kIntegratedOpeningBytes,
                c.plans[1].openingBytes(),c.key,bProof)) return failSearch();
        c.valid = true;
        return true;
    }
