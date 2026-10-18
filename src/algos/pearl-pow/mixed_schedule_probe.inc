
#if defined(CANARY_PEARL_MIXED_SCHEDULE_PROBE) && CANARY_PEARL_MIXED_SCHEDULE_PROBE == 1
public:
    static int mixedScheduleProbe() {
        PearlPow p;RunOptions options;options.pearlPolicy={PearlNetwork::Mainnet,PearlTargetSemantics::BaseBeforePenalty};p.configure(options);
        if(!p.experimentConfigurationValid_ || p.overlapMode_!=AttemptOverlapMode::Disabled)return 2;
        cudaDeviceProp prop{};int dev=0;size_t freeB=0,totalB=0;
        if(cudaGetDevice(&dev)!=cudaSuccess || cudaGetDeviceProperties(&prop,dev)!=cudaSuccess)return 2;
        p.arch10_=prop.major*10+prop.minor;
        if(p.arch10_!=89 && p.arch10_!=120)return 2;
        // Same production kernel row, same K/rank, one largest allocation.
        p.tile_=25;p.shape_=*findCudaPreparationShape(16384,131072);p.compactReadback_=true;
        if(cudaMemGetInfo(&freeB,&totalB)!=cudaSuccess || p.bytesFor(p.shape_)+(512ULL<<20)>freeB ||
            cudaStreamCreate(&p.stream_)!=cudaSuccess || !p.allocate())return 2;
        p.allocated_=true;
        if(!p.compactReadback_)return 2;
        Job job;PearlJobContext context;context.network=PearlNetwork::Mainnet;
        context.targetSemantics=PearlTargetSemantics::BaseBeforePenalty;context.height=99000;
        context.certificateVersion=PearlCertificateVersion::V3;context.transportJobId="offline-combined-v3";
        for(size_t i=0;i<context.header.size();++i)context.header[i]=uint8_t(0xab^i);
        job.epoch=context.height;job.jobId=context.transportJobId;job.target[0]=1;
        if(!encodePearlJobContext(context,&job.extra,nullptr,nullptr))return 2;
        b3::hash(nullptr,context.header.data(),context.header.size(),job.msg);job.valid=true;
        if(!p.beginJob(job))return 2;
        uint64_t nonce=1;unsigned proofChecks=0;
        auto switchShape=[&](uint32_t m){
            if(p.shape_.m==m)return true;
            const auto *shape=findCudaPreparationShape(m,131072);if(!shape)return false;
            PreparedJobRequest request;PreparedJobHost next;
            if(!buildCudaPreparedJobRequest(tileConfig(p.tile_),m,131072,p.arch10_,PreparedJobMode::Normal,&request) ||
                !buildPreparedJobHost(job,nullptr,request,&next))return false;
            // Reuse B only when every derived preparation field except M is identical.
            // M changes A commitment/noise; prepareA receives the actual M below.
            auto previous=p.staging_;previous.m=m;
            if(previous!=next || !p.publication_.ready() || !p.discardPreparedLookahead() ||
                !p.publication_.beginRewrite())return false;
            p.shape_=*shape;p.staging_=next;p.wins_.beginSearch();
            return p.publication_.publish(next);
        };
        auto attempt=[&](){
            p.hitReadback_=0;
            return p.runAttempt(nonce++) && cudaMemcpyAsync(&p.hitReadback_,p.dHitCount_,4,cudaMemcpyDeviceToHost,p.stream_)==cudaSuccess &&
                cudaStreamSynchronize(p.stream_)==cudaSuccess && p.hitReadback_==0;
        };
        auto proof=[&](uint32_t row,uint32_t col){
            MerkleProof a,b;
            if(!p.readCompactOpening(reinterpret_cast<const uint8_t*>(p.dA_),size_t(p.shape_.m)*kK,row,p.dProofTreeA_,false,&a,nonce-1) ||
                !p.readCompactOpening(reinterpret_cast<const uint8_t*>(p.dBt_),size_t(p.shape_.n)*kK,col,p.dProofTreeB_,p.bCompactTreeValid_,&b,nonce-1))return false;
            uint8_t roots[64],ca[32],cb[32],expectedA[32],expectedB[32];
            if(cudaMemcpy(roots,p.dRoots_,64,cudaMemcpyDeviceToHost)!=cudaSuccess ||
                cudaMemcpy(ca,p.dCommitA_,32,cudaMemcpyDeviceToHost)!=cudaSuccess ||
                cudaMemcpy(cb,p.dCommitB_,32,cudaMemcpyDeviceToHost)!=cudaSuccess ||
                memcmp(roots,a.root,32) || memcmp(roots+32,b.root,32))return false;
            commitments(a.root,b.root,p.jobKey_,p.shape_.m,p.shape_.n,p.cert_>=3,expectedA,expectedB);
            if(memcmp(ca,expectedA,32)||memcmp(cb,expectedB,32))return false;
            std::vector<int8_t> ar,br;
            ar.assign(a.leafData.begin(),a.leafData.end());
            br.assign(b.leafData.begin(),b.leafData.end());
            uint32_t host[kTranscriptWords]{},gpu[kTranscriptWords]{};U256 digest,opened;
            // Offline tiny-target fixture must fail eligibility while still computing exact host transcript.
            if(p.recheck(ar,br,ca,cb,row,col,host,&digest,true))return false;
            size_t flat=size_t(row/kTileSide)*(p.shape_.n/kTileSide)+col/kTileSide;
            if(cudaMemcpy(gpu,p.dTranscripts_+flat*kTranscriptWords,sizeof(gpu),cudaMemcpyDeviceToHost)!=cudaSuccess ||
                memcmp(host,gpu,sizeof(host)) || !digestFromProof(a,row,b,col,ca,cb,p.shape_.m,p.shape_.n,kK,kRank,kTileSide,&opened) ||
                memcmp(digest.v,opened.v,sizeof(digest.v)))return false;
            auto encoded=encodePlainProofChecked(p.shape_.m,p.shape_.n,kK,kRank,a,row,kTileSide,b,col,kTileSide);
            if(!encoded.ok())return false;
            std::printf("{\"event\":\"mixed_offline_proof\",\"m\":%u,\"n\":%u,\"nonce\":%llu,\"row\":%u,\"col\":%u,\"target_eligible\":false,\"cert_version\":3,\"proof\":\"%s\"}\n",
                p.shape_.m,p.shape_.n,(unsigned long long)(nonce-1),row,col,base64(encoded.bytes).c_str());
            if(std::fflush(stdout))return false;p.bCompactTreeValid_=true;++proofChecks;return true;
        };
        // Exercise both boundary openings after repeated size changes before timing.
        for(uint32_t m:{4096u,16384u,4096u,16384u}){
            if(!switchShape(m) || !attempt() || !proof(0,0) || !proof(m-16,131072-16))return 2;
        }
        const char *patterns[]={"L","SLS","SSL","LSL","S","S","LSL","SSL","SLS","L"};
        const proof_timing::SystemAdapter clock;
        for(unsigned arm=0;arm<10;++arm){
            MixedSchedule schedule(patterns[arm]);
            if(!switchShape(schedule.m(0)) || !attempt())return 2;
            uint64_t elapsed=0;const auto start=clock.now();
            for(unsigned i=0;i<96;++i){
                if(!switchShape(schedule.m(i)) || !attempt() || !schedule.complete(nonce-1))return 2;
            }
            elapsed=clock.now()-start;
            if(!elapsed || schedule.completed()!=96)return 2;
            std::printf("{\"event\":\"mixed_schedule_arm\",\"arm\":%u,\"pattern\":\"%s\",\"attempts\":96,\"work_units\":%llu,\"elapsed_ns\":%llu,\"profile\":\"%s\",\"b_rebuilds\":0,\"public_pool\":false}\n",
                arm,patterns[arm],(unsigned long long)schedule.work(),(unsigned long long)elapsed,matrix_input::profile);
            if(std::fflush(stdout))return 2;
        }
        std::printf("PASS combined mixed-shape probe: %u proof checks and 10 mirrored schedule arms\n",proofChecks);
        return 0;
    }
private:
#endif
