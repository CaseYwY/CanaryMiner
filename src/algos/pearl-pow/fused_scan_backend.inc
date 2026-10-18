// Included inside PearlPow. This mode does not alter job or proof ownership.
    const FusedScanMode fusedScanMode_=parseFusedScan(getenv("CANARY_PEARL_FUSED_SCAN"));
    static bool fusedScanConflicts() noexcept {
        for (const char *key:{"CANARY_PEARL_INTEGRATED_PIPELINE","CANARY_PEARL_ATTEMPT_OVERLAP",
                "CANARY_PEARL_A_ROOT_FUSION","CANARY_PEARL_EXPERIMENT","CANARY_PEARL_PROFILE_INTERVAL",
                "CANARY_PEARL_STAGE_TIMING","CANARY_PEARL_HARD_TARGET","CANARY_PEARL_BENCH_ALL_HIT",
                "CANARY_PEARL_PRIVATE_EXECUTION","CANARY_PEARL_PRIVATE_EXECUTION_MODE",
                "CANARY_PEARL_PUBLIC_EXECUTION"})
            if (getenv(key)) return true;
        return false;
    }
    const bool fusedScanConflictCaptured_=fusedScanConflicts();
    bool fusedScanAnnounced_=false;
#ifdef CANARY_PEARL_TEST_TRANSCRIPT_SNAPSHOT
    static constexpr size_t kFusedScanCanarySlots=1;
#else
    static constexpr size_t kFusedScanCanarySlots=0;
#endif
    bool productN64ScanSelection() const noexcept {
        return false;
    }
    bool pairedScanEnabled() const noexcept { return fusedScanMode_==FusedScanMode::StorePairedV1; }
    bool fusedScanEnabled() const noexcept { return productN64ScanSelection() ||
        fusedScanMode_==FusedScanMode::StoreV1 || pairedScanEnabled(); }
    bool fusedScanEnvironmentValid() const noexcept {
        if (parseFusedScan(getenv("CANARY_PEARL_FUSED_SCAN"))!=fusedScanMode_ ||
            fusedScanMode_==FusedScanMode::Invalid) return false;
        if (productN64ScanSelection()) return !fusedScanConflicts();
        if (!fusedScanEnabled()) return true;
        if (!kFusedScanCompiled || fusedScanConflictCaptured_ || formalRequest_.enabled || syntheticRun_ ||
            privateExecution_!=PearlPrivateExecution::Disabled ||
            publicExecution_!=PearlPublicExecution::Disabled) return false;
        if (fusedScanConflicts()) return false;
        return true;
    }
    bool fusedScanAdmission(const RunOptions &options) const noexcept {
        if (!fusedScanEnvironmentValid()) return false;
        if (productN64ScanSelection()) return !formalRequest_.enabled && !syntheticRun_;
        if (!fusedScanEnabled()) return true;
        if (pairedScanEnabled() && !pairedScanBounded(
                options.pearlGuardRetirementSession && options.poolHost=="127.0.0.1",
                options.pearlAcceptedQuota || options.pearlSustainedSeconds || options.pearlProduction,options.pearlProduction)) return false;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        const char *native=getenv("CANARY_PEARL_NATIVE_EVIDENCE");
        const char *compact=getenv("CANARY_PEARL_COMPACT_READBACK");
        return options.pearlGuardRetirementSession && options.poolHost=="127.0.0.1" &&
            options.poolPort>0 && options.poolPort<=65535 && options.batch==256 &&
            (options.pearlAcceptedQuota || options.pearlSustainedSeconds || options.pearlProduction) &&
            options.pearlShapeExperiment==PearlShapeExperiment::Exact16384x131072K8192V1 &&
            native && !strcmp(native,"sm120-generic-v1") &&
            compact && !strcmp(compact,"1") &&
            options.pearlPolicy.network==PearlNetwork::Mainnet &&
            options.pearlPolicy.targetSemantics==PearlTargetSemantics::BaseBeforePenalty;
#else
        return false;
#endif
    }
    bool fusedScanSelectionValid() const noexcept {
        return !fusedScanEnabled() || (fusedScanEnvironmentValid() &&
            tile_<kCudaPreparationPlanCount && fusedScanExactShape(arch10_,shape_.m,shape_.n,kK,kRank,
                tileConfig(tile_).preparationPlanId) &&
            (!pairedScanEnabled() || pairedScanAllowed(arch10_,shape_.m,shape_.n,kK,kRank,
                tileConfig(tile_).preparationPlanId)));
    }
    bool initializeFusedScanCanaries() noexcept {
#ifdef CANARY_PEARL_TEST_TRANSCRIPT_SNAPSHOT
        return cudaMemsetAsync(dHitIndex_+kMaxHits,0xa5,4,stream_)==cudaSuccess &&
            cudaMemsetAsync(dHitDigest_+kMaxHits*8,0xa5,32,stream_)==cudaSuccess;
#else
        return true;
#endif
    }
