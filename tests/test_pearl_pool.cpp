// Host-only Pearl pool protocol checks.

#include <cstdio>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "../src/core/pearl_pool.h"
#include "../src/core/run.h"

static const uint64_t kOneLimbs[4] = {1, 0, 0, 0};

using namespace om;

class SynchronousTestSource : public JobSource {
   public:
    const char *describe() const override { return "synchronous test source"; }
    bool fetch(Job *) override { return false; }
    bool submit(const Job &, const Solution &, std::string *) override {
        return true;
    }
};

class LegacyPoolTestSource : public SynchronousTestSource {
   public:
    bool poolCounters(uint64_t *accepted, uint64_t *rejected,
                      uint64_t *pending, std::string *lastError) const override {
        *accepted = 2;
        *rejected = 1;
        *pending = 3;
        lastError->clear();
        return true;
    }
};

class InvalidOutcomeTestSource : public SynchronousTestSource {
   public:
    OutcomeSnapshotStatus outcomeSnapshot(
        PoolOutcomeSnapshot *, std::string *) const override {
        return OutcomeSnapshotStatus::Invalid;
    }
};

class OverflowLegacyPoolTestSource : public SynchronousTestSource {
   public:
    bool poolCounters(uint64_t *accepted, uint64_t *rejected,
                      uint64_t *pending, std::string *lastError) const override {
        *accepted = UINT64_MAX;
        *rejected = 1;
        *pending = 0;
        lastError->clear();
        return true;
    }
};

static bool expect(const char *name, const std::string &actual,
                   const char *wanted) {
    if (actual == wanted) return true;
    fprintf(stderr, "%s\n  got:  %s\n  want: %s\n", name, actual.c_str(), wanted);
    return false;
}

static bool expectBool(const char *name, bool actual) {
    if (actual) return true;
    fprintf(stderr, "%s: failed\n", name);
    return false;
}

static std::string notify(const std::string &header, const std::string &target,
                          const char *jobId, int cert = 3) {
    return "{\"method\":\"mining.notify\",\"params\":{\"job_id\":\"" +
           std::string(jobId) + "\",\"header\":\"" + header +
           "\",\"target\":\"" + target + "\",\"cert_version\":" +
           std::to_string(cert) + "}}";
}

static std::string notifyRawCert(const std::string &header,
                                 const std::string &target,
                                 const char *jobId,
                                 const std::string &cert) {
    return "{\"method\":\"mining.notify\",\"params\":{\"job_id\":\"" +
           std::string(jobId) + "\",\"header\":\"" + header +
           "\",\"target\":\"" + target + "\",\"cert_version\":" + cert +
           "}}";
}

int main() {
    bool ok = true;

    PoolOutcomeSnapshot sourceOutcome;
    std::string sourceError;
    SynchronousTestSource synchronous;
    ok &= expectBool(
        "a legacy synchronous source keeps submit-return acceptance semantics",
        successfulSubmitIsTerminalAcceptance(&synchronous, &sourceOutcome,
                                             &sourceError));
    LegacyPoolTestSource legacyPool;
    ok &= expectBool(
        "legacy pool counters suppress the synchronous acceptance fallback",
        !successfulSubmitIsTerminalAcceptance(&legacyPool, &sourceOutcome,
                                              &sourceError) &&
            sourceOutcome.submitted == 6 &&
            sourceOutcome.acknowledged == 3 &&
            sourceOutcome.accepted == 2 && sourceOutcome.rejected == 1 &&
            sourceOutcome.pending == 3 && sourceOutcome.conserved());
    InvalidOutcomeTestSource invalidOutcome;
    ok &= expectBool(
        "invalid outcome accounting cannot fall back to solo acceptance",
        !successfulSubmitIsTerminalAcceptance(
            &invalidOutcome, &sourceOutcome, &sourceError));
    OverflowLegacyPoolTestSource overflowLegacy;
    ok &= expectBool(
        "overflowing legacy counters are invalid, never solo acceptance",
        overflowLegacy.outcomeSnapshot(&sourceOutcome, &sourceError) ==
                OutcomeSnapshotStatus::Invalid &&
            !successfulSubmitIsTerminalAcceptance(
                &overflowLegacy, &sourceOutcome, &sourceError));
    MinerStats synchronousStats;
    recordSynchronousAcceptance(&synchronousStats);
    ok &= expectBool(
        "synchronous acceptance conserves submitted and acknowledged counts",
        synchronousStats.submitted == 1 &&
            synchronousStats.acknowledged == 1 &&
            synchronousStats.accepted == 1);

    ok &= expect("JSON quotes every control character",
                 jsonQuoted("a\"\\\b\f\n\r\t\x01z"),
                 "\"a\\\"\\\\\\b\\f\\n\\r\\t\\u0001z\"");
    ok &= expect("authorization quotes wallet and worker",
                 pearlPoolAuthorizeRequest("prl1\"wallet", "rig\\one\n"),
                 "{\"id\":1,\"method\":\"mining.authorize\",\"params\":{\"wallet\":\"prl1\\\"wallet\",\"worker\":\"rig\\\\one\\n\",\"pass\":\"x\",\"agent\":\"CanaryMiner/" CANARY_MINER_VERSION "\"}}");
    ok &= expect("authorization has no subscribe method",
                 pearlPoolAuthorizeRequest("w", "r").find("subscribe") == std::string::npos ? "yes" : "no", "yes");
    const std::string bareAddress = "prl1" + std::string(59, 'q');
    bool soloMode = false;
    ok &= expectBool("parse opt-in SOLO", parsePearlAuthorizationModeOption("solo", &soloMode) && soloMode);
    ok &= expectBool("parse explicit standard", parsePearlAuthorizationModeOption("standard", &soloMode) && !soloMode);
    ok &= expectBool("reject malformed mode", !parsePearlAuthorizationModeOption("SOLO", &soloMode) && !parsePearlAuthorizationModeOption("", &soloMode));
    const std::string expectedSoloAuthorization = "{\"id\":1,\"method\":\"mining.authorize\",\"params\":{\"wallet\":\"solo:" + bareAddress + "\",\"worker\":\"rig\",\"pass\":\"x\",\"agent\":\"canaryminer\"}}";
    ok &= expect("SOLO only decorates upstream wallet",
        pearlPoolAuthorizeRequest(bareAddress, "rig", "x", "canaryminer", true),
        expectedSoloAuthorization.c_str());
    bool doublePrefixRejected = false;
    try { pearlPoolAuthorizeRequest("solo:" + bareAddress, "rig", "x", "canaryminer", true); }
    catch (const std::invalid_argument &) { doublePrefixRejected = true; }
    ok &= expectBool("double prefix refused", doublePrefixRejected);
    ok &= expectBool("SOLO context rejects guards and other modes",
        pearlSoloAuthorizationAllowed(true, "pearl-pow", false, true, false) &&
        !pearlSoloAuthorizationAllowed(true, "pearl-pow", false, true, true) &&
        !pearlSoloAuthorizationAllowed(true, "pearl-pow", true, true, false) &&
        !pearlSoloAuthorizationAllowed(true, "autolykos2", false, true, false));
    ok &= expect("submission quotes pool supplied job id",
                 pearlPoolSubmitRequest(101, "job\"1", "proof\\data"),
                 "{\"id\":101,\"method\":\"mining.submit\",\"params\":{\"job_id\":\"job\\\"1\",\"plain_proof\":\"proof\\\\data\"}}");
    std::string replyError;
    ok &= expectBool("only explicit JSON-RPC success is accepted",
                     pearlPoolReplyAccepted("{\"id\":101,\"error\":null,\"result\":true}",
                                            &replyError));
    ok &= expectBool("false submit result is not reported as a share",
                     !pearlPoolReplyAccepted("{\"id\":101,\"error\":null,\"result\":false}",
                                             &replyError));
    ok &= expectBool("pool rejection message is preserved",
                     !pearlPoolReplyAccepted("{\"id\":101,\"error\":{\"message\":\"bad share\"},\"result\":null}",
                                             &replyError) && replyError == "bad share");
    ok &= expectBool("an unrelated success cannot authorize request id 1",
                     !pearlPoolReplyIsFor(
                         "{\"id\":2,\"error\":null,\"result\":true}", 1));

    // Pearl8's optional transcript is metadata-only. In particular, logging
    // must neither leak an address echoed by a pool nor alter the exact submit
    // payload used for share construction.
    // Named "fakeAddress", not "secret": gitleaks' generic-api-key rule fires on
    // a string literal assigned to something called secret, and a fixture whose
    // entire job is to be an obviously-fake address should not be the thing that
    // trains people to run git commit --no-verify.
    const std::string fakeAddress = "prl1exampleaddressnotreal000000";
    ok &= expectBool("transcript redacts PRL addresses",
                     pearlTranscriptRedact("pool says " + fakeAddress + " rejected")
                         .find(fakeAddress) == std::string::npos);
    const std::string submitBefore = pearlPoolSubmitRequest(101, "job-1", "proof-data");
    const uint8_t hdrMsg[32] = {0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4};
    const uint64_t zeroDigest[4] = {};
    const uint64_t someTarget[4] = {0, 0, 0, 0x7fff8ULL << 40};
    const std::string submitTrace =
        pearlTranscriptSubmit(101, "job-1", "proof-data",
                              pearlHeaderFingerprint(hdrMsg), zeroDigest, someTarget);
    const std::string submitAfter = pearlPoolSubmitRequest(101, "job-1", "proof-data");
    ok &= expectBool("transcript does not change share construction",
                     submitBefore == submitAfter &&
                     submitTrace.find("proof-data") == std::string::npos &&
                     submitTrace.find("plain_proof_b3") != std::string::npos);
    // A synthetic address, not a real one: the redactor keys off the prl1
    // prefix and eats alphanumerics, so this exercises it exactly and puts no
    // real wallet in a test fixture.
    const std::string secret = "prl1qqqqexampleaddressnotarealwalletqqqq";
    const std::string replyTrace = pearlTranscriptReply(101, false,
                                                         "wallet " + secret + " rejected");
    ok &= expectBool("transcript redacts echoed pool errors",
                     replyTrace.find(secret) == std::string::npos &&
                     replyTrace.find("REDACTED_PRL_ADDRESS") != std::string::npos);
    const std::string acceptedSupersededTrace = pearlTranscriptReply(
        102, PearlPoolSubmitVerdict::AcceptedSuperseded, "new job arrived");
    ok &= expectBool("transcript preserves accepted-superseded distinctly",
                     acceptedSupersededTrace.find("\"accepted\":true") !=
                             std::string::npos &&
                         acceptedSupersededTrace.find("\"stale\":false") !=
                             std::string::npos &&
                         acceptedSupersededTrace.find(
                             "\"accepted_superseded\":true") !=
                             std::string::npos &&
                         acceptedSupersededTrace.find(
                             "\"outcome\":\"accepted_superseded\"") !=
                             std::string::npos);
    const std::string unknownTrace = pearlTranscriptReply(
        103, PearlPoolSubmitVerdict::Unknown, "no correlated reply");
    ok &= expectBool("transcript preserves unknown distinctly",
                     unknownTrace.find("\"unknown\":true") != std::string::npos &&
                         unknownTrace.find("\"outcome\":\"unknown\"") !=
                             std::string::npos);

    // Pearl Stratum targets are base share targets. They arrive as big-endian
    // hex while Job/CUDA use little-endian limbs; PearlPow applies the mining
    // configuration adjustment once. A target-only notify is a difficulty
    // refresh, not a stale duplicate.
    const std::string header(152, 'a');
    const std::string endianTarget =
        "0102030405060708111213141516171821222324252627283132333435363738";
    uint64_t limbs[4] = {};
    ok &= expectBool("big-endian pool target parses",
                     hexToLimbs(endianTarget, limbs));
    ok &= expectBool("pool target becomes little-endian Job limbs",
                     limbs[0] == 0x3132333435363738ULL &&
                     limbs[1] == 0x2122232425262728ULL &&
                     limbs[2] == 0x1112131415161718ULL &&
                     limbs[3] == 0x0102030405060708ULL);

    const std::string easyTarget =
        "0000000000000001000000000000000000000000000000000000000000000000";
    const std::string hardTarget =
        "0000000000000000800000000000000000000000000000000000000000000000";
    Job easy, hard;
    ok &= expectBool("parse initial mock Pearl job",
                     pearlPoolJobFromNotify(notify(header, easyTarget, "job-easy"), &easy));
    ok &= expectBool("parse target-only Pearl difficulty refresh",
                     pearlPoolJobFromNotify(notify(header, hardTarget, "job-hard"), &hard));
    ok &= expectBool("same header has stable job identity hash",
                     memcmp(easy.msg, hard.msg, sizeof(easy.msg)) == 0);
    ok &= expectBool("refreshed target and job id are retained",
                     memcmp(easy.target, hard.target, sizeof(easy.target)) != 0 &&
                     easy.extra != hard.extra && poolJobId(hard.extra) == "job-hard");
    const std::string notifyTrace = pearlTranscriptNotify(hard);
    ok &= expectBool("notify transcript contains only needed job metadata",
                     notifyTrace.find("job-hard") != std::string::npos &&
                     notifyTrace.find(hardTarget) != std::string::npos &&
                     notifyTrace.find(header) == std::string::npos);
    ok &= expectBool("target-only refresh is a different submit job",
                     !pearlPoolJobsMatch(easy, hard));
    ok &= expectBool("an exact synthetic job remains current",
                     pearlPoolJobsMatch(easy, easy));
    const std::string escapedJobId = "job\"quoted\\tail\nline";
    const std::string escapedNotify =
        "{\"method\":\"mining.notify\",\"params\":{"
        "\"job_id\":\"job\\\"quoted\\\\tail\\nline\",\"header\":\"" +
        header + "\",\"target\":\"" + easyTarget +
        "\",\"cert_version\":3}}";
    Job escapedJob;
    ok &= expectBool("escaped pool job id is decoded exactly",
                     pearlPoolJobFromNotify(escapedNotify, &escapedJob) &&
                         poolJobId(escapedJob.extra) == escapedJobId);
    ok &= expectBool("decoded pool job id is safely re-quoted on submit",
                     pearlPoolSubmitRequest(5, poolJobId(escapedJob.extra), "p") ==
                         "{\"id\":5,\"method\":\"mining.submit\",\"params\":{"
                         "\"job_id\":\"job\\\"quoted\\\\tail\\nline\","
                         "\"plain_proof\":\"p\"}}");

    // Pearl upstream #204 showed why prev-hash alone is not a template
    // identity: version, Merkle root, bits or certificate can change while the
    // tip stays put.  The pool gives us the already-serialized 76-byte header,
    // so compare that envelope byte-for-byte (through extra), along with the
    // share target, certificate and job id.  Parse each variant from a real
    // notify shape so this cannot pass on a hand-built Job that omits a field.
    // Hex offsets cover every same-tip header region: version, Merkle root,
    // timestamp and nBits. Bytes 4..35 (the previous-block hash) stay fixed.
    const size_t headerMutationOffsets[] = {0, 72, 136, 144};
    const char *headerMutationNames[] = {
        "version", "Merkle root", "timestamp", "header nBits"};
    PreparedJob parsedPrepared;
    parsedPrepared.take(easy);
    for (size_t i = 0; i < sizeof(headerMutationOffsets) /
                                    sizeof(headerMutationOffsets[0]); ++i) {
        std::string changedHeader = header;
        const size_t at = headerMutationOffsets[i];
        changedHeader[at] = changedHeader[at] == 'a' ? 'b' : 'a';
        Job changed;
        char parseName[128], identityName[128], preparedName[128];
        snprintf(parseName, sizeof(parseName), "parse same-tip %s mutation",
                 headerMutationNames[i]);
        snprintf(identityName, sizeof(identityName),
                 "%s change replaces same-tip work", headerMutationNames[i]);
        snprintf(preparedName, sizeof(preparedName),
                 "%s change invalidates prepared work", headerMutationNames[i]);
        ok &= expectBool(parseName, pearlPoolJobFromNotify(
                                        notify(changedHeader, easyTarget,
                                               "job-easy"),
                                        &changed));
        ok &= expectBool(identityName, !pearlPoolJobsMatch(easy, changed));
        ok &= expectBool(preparedName,
                         shouldPrepare(true, parsedPrepared, changed));
    }

    Job sameTipCert, sameTipTarget, sameTipId;
    ok &= expectBool("parse same-tip certificate mutation",
                     pearlPoolJobFromNotify(
                         notify(header, easyTarget, "job-easy", 2),
                         &sameTipCert));
    ok &= expectBool("parse same-tip target mutation",
                     pearlPoolJobFromNotify(
                         notify(header, hardTarget, "job-easy"),
                         &sameTipTarget));
    ok &= expectBool("parse same-tip job-id mutation",
                     pearlPoolJobFromNotify(
                         notify(header, easyTarget, "job-renamed"),
                         &sameTipId));
    ok &= expectBool("certificate-version change replaces same-tip work",
                     !pearlPoolJobsMatch(easy, sameTipCert));
    ok &= expectBool("target change replaces same-tip work",
                     !pearlPoolJobsMatch(easy, sameTipTarget));
    ok &= expectBool("job-id change replaces same-tip work",
                     !pearlPoolJobsMatch(easy, sameTipId));

    ok &= expectBool("certificate mutation invalidates prepared work",
                     shouldPrepare(true, parsedPrepared, sameTipCert));
    ok &= expectBool("target mutation invalidates prepared work",
                     shouldPrepare(true, parsedPrepared, sameTipTarget));
    ok &= expectBool("job-id mutation invalidates prepared work",
                     shouldPrepare(true, parsedPrepared, sameTipId));

    // Do not manufacture a certificate version. A missing V1/V2/V3 selector
    // can produce a perfectly encoded proof for the wrong verifier.
    const std::string noCert =
        "{\"method\":\"mining.notify\",\"params\":{\"job_id\":\"bad\",\"header\":\"" +
        header + "\",\"target\":\"" + easyTarget + "\"}}";
    const std::string zeroTarget(64, '0');
    auto rejectNotifyWithoutMutation = [&](const char *name,
                                           const std::string &line) {
        Job destination = easy;
        const bool rejected = !pearlPoolJobFromNotify(line, &destination);
        ok &= expectBool(name, rejected &&
                                   pearlPoolJobsMatch(destination, easy) &&
                                   destination.jobId == easy.jobId);
    };
    rejectNotifyWithoutMutation("missing certificate version is rejected",
                                noCert);
    rejectNotifyWithoutMutation(
        "unsupported certificate version is rejected",
        notifyRawCert(header, easyTarget, "bad", "4"));
    rejectNotifyWithoutMutation(
        "fractional certificate version is rejected",
        notifyRawCert(header, easyTarget, "bad", "3.5"));
    rejectNotifyWithoutMutation(
        "quoted certificate version is rejected",
        notifyRawCert(header, easyTarget, "bad", "\"3\""));
    rejectNotifyWithoutMutation(
        "zero target cannot become a locally accepted job",
        notify(header, zeroTarget, "bad"));
    rejectNotifyWithoutMutation(
        "short pool target is rejected",
        notify(header, easyTarget.substr(1), "bad"));
    rejectNotifyWithoutMutation(
        "long pool target is rejected",
        notify(header, easyTarget + "0", "bad"));
    std::string malformedTarget = easyTarget;
    malformedTarget[1] = 'g';  // `%2x` used to accept the pair `0g` as zero.
    rejectNotifyWithoutMutation(
        "malformed pool target pair is rejected",
        notify(header, malformedTarget, "bad"));
    std::string malformedHeader = header;
    malformedHeader[1] = 'g';
    rejectNotifyWithoutMutation(
        "malformed pool header pair is rejected",
        notify(malformedHeader, easyTarget, "bad"));
    rejectNotifyWithoutMutation(
        "empty pool job id is rejected",
        notify(header, easyTarget, ""));
    const std::string unicodeEscapedJob =
        "{\"method\":\"mining.notify\",\"params\":{"
        "\"job_id\":\"\\u006aob-id\",\"header\":\"" + header +
        "\",\"target\":\"" + easyTarget +
        "\",\"cert_version\":3}}";
    Job unicodeJob;
    ok &= expectBool("unicode-escaped pool job id is decoded",
                     pearlPoolJobFromNotify(unicodeEscapedJob, &unicodeJob) &&
                         poolJobId(unicodeJob.extra) == "job-id");
    rejectNotifyWithoutMutation(
        "wrong-typed notify header cannot borrow a nested string",
        "{\"method\":\"mining.notify\",\"params\":{\"job_id\":\"bad\","
        "\"header\":{\"header\":\"" + header + "\"},\"target\":\"" +
        easyTarget + "\",\"cert_version\":3}}" );
    rejectNotifyWithoutMutation(
        "nested notify fields are not direct params",
        "{\"method\":\"mining.notify\",\"params\":{\"metadata\":{"
        "\"job_id\":\"bad\",\"header\":\"" + header +
        "\",\"target\":\"" + easyTarget +
        "\",\"cert_version\":3}}}" );
    rejectNotifyWithoutMutation(
        "duplicate direct notify target is rejected",
        "{\"method\":\"mining.notify\",\"params\":{\"job_id\":\"bad\","
        "\"header\":\"" + header + "\",\"target\":\"" + easyTarget +
        "\",\"target\":\"" + hardTarget + "\",\"cert_version\":3}}" );
    rejectNotifyWithoutMutation(
        "unicode-escaped duplicate notify key is rejected",
        "{\"method\":\"mining.notify\",\"params\":{\"job_id\":\"bad\","
        "\"header\":\"" + header + "\",\"target\":\"" + easyTarget +
        "\",\"\\u0074arget\":\"" + hardTarget +
        "\",\"cert_version\":3}}");

    const pearl::U256 easyBound = pearl::U256::fromLimbs(easy.target);
    const pearl::U256 hardBound = pearl::U256::fromLimbs(hard.target);
    pearl::U256 candidate = hardBound;
    ++candidate.v[0];  // above current target, still below the prior one
    ok &= expectBool("candidate would pass only the stale easier share bound",
                     candidate.le(easyBound) && !candidate.le(hardBound));
    pearl::MiningConfig cfg;
    cfg.commonDim = 2048;
    cfg.rank = 128;
    // A pool target is a base target, exactly like any other pool's. HeroMiners
    // publishes 0xFFFF * 2^208 / difficulty and its verifier applies the same
    // rank/work penalty, so the miner scales it once and mines against that.
    // Using the advertised value directly is 2^19 too hard and submits nothing:
    // 12 accepted / 0 rejected on a 4090 with the scaled bound, 0 submits in
    // 71 s on a 5080 without it.
    pearl::U256 scaledHard, scaledEasy;
    ok &= expectBool("pool target scales once into the mining bound",
                     cfg.penalizedTarget(hardBound, &scaledHard) &&
                     cfg.penalizedTarget(easyBound, &scaledEasy));
    ok &= expectBool("scaling is the shipped k=2048/rank=128 factor",
                     scaledHard.le(scaledEasy) && hardBound.le(scaledHard));
    pearl::U256 oneFactor;
    ok &= expectBool("that factor is exactly 524,288",
                     pearl::U256::fromLimbs(kOneLimbs)
                             .mul(524288ull, &oneFactor) &&
                     cfg.penalizedTarget(
                         pearl::U256::fromLimbs(kOneLimbs),
                         &scaledEasy) &&
                     scaledEasy.le(oneFactor) && oneFactor.le(scaledEasy));
    // The stale-target regression still bites: a candidate that only passed
    // the previous, easier job must fail the current one after scaling.
    pearl::U256 staleCandidate = scaledHard;
    ++staleCandidate.v[0];
    ok &= expectBool("candidate above the fresh scaled bound is refused",
                     !staleCandidate.le(scaledHard));
    // The reported failure: "Pearl pool refused login: Invalid Pearl address".
    // Every one of these used to reach the pool and come back as that message.
    const std::string placeholder = "prl1YOUR_PEARL_ADDRESS_HERE";
    // Synthetic, not anyone's wallet: these fixtures only need the address
    // FORM, and a real payout address in a public repo is a privacy leak.
    const std::string fakeMainnet =
        "prl1examplefakeaddressmadeupnevertrueqqqqqqqqqqqqqqqqqqqqqqqqqq";
    const std::string fakeTestnet =
        "tprl1examplefaketestnetaddressneverusedqqqqqqqqqqqqqqqqqqqqqqqqq";
    ok &= expectBool("the launcher placeholder is refused locally",
                     !pearlWalletProblem(placeholder).empty());
    ok &= expectBool("the placeholder message names the launcher",
                     pearlWalletProblem(placeholder).find("WALLET") !=
                         std::string::npos);
    ok &= expectBool("an empty address is refused",
                     !pearlWalletProblem("").empty());
    ok &= expectBool("a testnet address is refused on a mainnet pool",
                     pearlWalletProblem(fakeTestnet).find("TESTNET") !=
                         std::string::npos);
    // An Ergo-shaped address works for Ergo and can never work here.
    const std::string fakeErgo =
        "9exampleErgoAddressNotARealWalletUsedInTests";
    ok &= expectBool("an Ergo address is refused",
                     !pearlWalletProblem(fakeErgo).empty());
    ok &= expectBool("and it says a Pearl address starts with prl1",
                     pearlWalletProblem(fakeErgo).find("starts with prl1") !=
                         std::string::npos);
    ok &= expectBool("wallet.worker concatenation is refused",
                     pearlWalletProblem(fakeMainnet + ".rig1").find("--worker") !=
                         std::string::npos);
    ok &= expectBool("surrounding whitespace is refused",
                     !pearlWalletProblem(fakeMainnet + " ").empty() &&
                     !pearlWalletProblem(" " + fakeMainnet).empty());
    ok &= expectBool("a truncated address is refused",
                     !pearlWalletProblem(fakeMainnet.substr(0, 20)).empty());
    ok &= expectBool("a well-formed mainnet address passes",
                     pearlWalletProblem(fakeMainnet).empty());

    // Two different 524,288s. The MACs one candidate costs is tileSize * k.
    // penalizedTarget's factor is tileSize * (k / rank) * kPenaltyBaseRank.
    // They are equal ONLY because the shipped rank is 128, which is also
    // kPenaltyBaseRank. Anyone who changes rank breaks that silently: the
    // reported TH/s stays right while the mining bound moves, or the reverse.
    {
        pearl::MiningConfig c;
        c.commonDim = 2048;
        c.rank = 128;
        const double macs = (double)c.tileSize() * c.commonDim;
        pearl::U256 one = pearl::U256::fromLimbs(kOneLimbs);
        pearl::U256 scaled;
        ok &= expectBool("a candidate costs tileSize * k MACs", macs == 524288.0);
        ok &= expectBool("at rank 128 the target factor is the same number",
                         c.penalizedTarget(one, &scaled) &&
                         scaled.v[0] == 524288ull && scaled.v[1] == 0);
        c.rank = 256;
        pearl::U256 scaled256;
        ok &= expectBool("at rank 256 they diverge, so they are not one constant",
                         c.penalizedTarget(one, &scaled256) &&
                         scaled256.v[0] == 262144ull &&
                         (double)c.tileSize() * c.commonDim == 524288.0);
    }

    // The regression for the rejections. A Pearl pool pushes a new header
    // every ~19 s with the epoch pinned at 0 (PearlPoolSource sets it to 0 on
    // every job). Gating prepare() on the epoch therefore prepared once, at
    // connect, and the miner spent the rest of the session hashing that first
    // header's matrices while quoting the newest job id back at the pool -
    // a proof that is valid for a job nobody asked about, answered with
    // "Jackpot condition not satisfied: hash does not meet difficulty target".
    {
        Job first;
        first.valid = true;
        first.epoch = 0;
        memset(first.msg, 0x11, sizeof(first.msg));
        for (int i = 0; i < 4; i++) first.target[i] = 0x0123456789abcdefull;
        first.extra = "job-0";

        PreparedJob prepared;
        ok &= expectBool("the first job always prepares",
                         shouldPrepare(true, prepared, first));
        prepared.take(first);
        ok &= expectBool("and the same job again does not",
                         !shouldPrepare(true, prepared, first));

        // The bug, in one assertion: same epoch, different header.
        Job second = first;
        memset(second.msg, 0x22, sizeof(second.msg));
        second.extra = "job-1";
        ok &= expectBool("the epochs are identical, which is what hid this",
                         second.epoch == first.epoch);
        ok &= expectBool("a new Pearl header re-prepares even at the same epoch",
                         shouldPrepare(true, prepared, second));

        // A pool may also refresh only the target (VarDiff).
        Job harder = first;
        harder.target[3] >>= 4;
        ok &= expectBool("a target-only refresh re-prepares too",
                         shouldPrepare(true, prepared, harder));

        // And the job id alone moving is still a different job to submit under.
        Job renamed = first;
        renamed.extra = "job-99";
        ok &= expectBool("a new job id re-prepares", shouldPrepare(true, prepared, renamed));

        // Autolykos must keep the epoch-only gate: re-preparing there rebuilds
        // a 7.27 GB dataset, so a per-job re-prepare would be a disaster.
        ok &= expectBool("Autolykos does NOT re-prepare on a same-epoch change",
                         !shouldPrepare(false, prepared, second));
        Job nextEpoch = second;
        nextEpoch.epoch = first.epoch + 1;
        ok &= expectBool("but Autolykos does re-prepare on a new epoch",
                         shouldPrepare(false, prepared, nextEpoch));
        ok &= expectBool("and so does Pearl", shouldPrepare(true, prepared, nextEpoch));
    }

    // Pearl maps one nonce to a complete transcript grid. The run loop must
    // account the grid it actually executes, not the arbitrary CLI budget,
    // and advance the nonce cursor by attempts rather than by candidates.
    {
        constexpr uint64_t tiles = 1048576;
        auto checkBatch = [&](const char *name, uint64_t requested,
                              uint64_t remaining, uint64_t wantWork,
                              uint64_t wantNonces) {
            const WorkBatch got = planWorkBatch(requested, remaining, tiles);
            char workName[160], nonceName[160];
            snprintf(workName, sizeof(workName), "%s: executed candidates", name);
            snprintf(nonceName, sizeof(nonceName), "%s: nonce advancement", name);
            ok &= expectBool(workName, got.workUnits == wantWork);
            ok &= expectBool(nonceName, got.nonces == wantNonces);
        };

        checkBatch("sub-grid batch", 256, 99, tiles, 1);
        checkBatch("one short of a grid", tiles - 1, 99, tiles, 1);
        checkBatch("nonmultiple above one grid", tiles + 256, 99, tiles, 1);
        checkBatch("nonmultiple above three grids", 3 * tiles + 256, 99,
                   3 * tiles, 3);
        checkBatch("nonce subspace clamps whole attempts", 3 * tiles + 256, 2,
                   2 * tiles, 2);

        const WorkBatch ordinary = planWorkBatch(777, 500, 1);
        ok &= expectBool("ordinary algorithms retain one-unit-per-nonce behavior",
                         ordinary.workUnits == 500 && ordinary.nonces == 500);
        const WorkBatch none = planWorkBatch(0, 99, tiles);
        ok &= expectBool("zero work request stays empty",
                         none.workUnits == 0 && none.nonces == 0);
        const WorkBatch huge = planWorkBatch(~0ull, ~0ull, tiles);
        ok &= expectBool("maximum request rounds down without overflowing",
                         huge.workUnits == (~0ull / tiles) * tiles &&
                         huge.nonces == ~0ull / tiles);

        // `UINT64_MAX - 0 + 1` wraps to zero in uint64_t. Full-width solo and
        // Pearl nonce spaces start in exactly that state after their counter
        // wraps, so the range helper must saturate rather than stop mining.
        const uint64_t full = remainingNonceCount(~0ull, 0);
        ok &= expectBool("full 64-bit nonce range at zero does not overflow",
                         full == ~0ull);
        ok &= expectBool("last full-width nonce is counted before wrap",
                         remainingNonceCount(~0ull, ~0ull) == 1);
        const WorkBatch afterWrap = planWorkBatch(3 * tiles + 256, full, tiles);
        ok &= expectBool("Pearl resumes exact work after full-range wrap",
                         afterWrap.workUnits == 3 * tiles &&
                         afterWrap.nonces == 3);

        // A prefixed stratum subspace has a smaller mask. Keep the inclusive
        // end count exact so a launch lands on the boundary but never crosses
        // into another miner's prefix.
        ok &= expectBool("bounded nonce subspace includes both endpoints",
                         remainingNonceCount(0xffff, 0xfff0) == 16);
        const WorkBatch edge = planWorkBatch(99, remainingNonceCount(0xffff, 0xfffe), 1);
        ok &= expectBool("bounded subspace clamps without crossing its prefix",
                         edge.workUnits == 2 && edge.nonces == 2);
        ok &= expectBool("counter outside its mask is refused",
                         remainingNonceCount(0xff, 0x100) == 0);
    }

    // ---------------------------------------------- difficulty negotiation
    //
    // Pearl Stratum V1 carries the target in every notify and HeroMiners sends
    // nothing else, so none of this fires against that pool. It exists because
    // a pool that DOES push a difficulty separately was previously ignored
    // outright, and the only symptom would have been every share refused with
    // "hash does not meet difficulty target".
    {
        printf("difficulty and target pushes\n");

        // The mapping, pinned against a measured live value rather than an
        // assumption: HeroMiners' job id names difficulty 2097152 and the
        // target it sends alongside is this.
        uint64_t t[4] = {};
        ok &= expectBool("difficulty 2097152 converts",
                         pearlDifficultyToTarget("2097152", t));
        ok &= expect("and it is exactly the target HeroMiners sends",
                     pearlTargetHexBE(t),
                     "00000000000007fff80000000000000000000000000000000000000000000000");

        uint64_t one[4] = {};
        ok &= expectBool("difficulty 1 converts", pearlDifficultyToTarget("1", one));
        ok &= expect("difficulty 1 is the whole 0xFFFF * 2^208 numerator",
                     pearlTargetHexBE(one),
                     "00000000ffff0000000000000000000000000000000000000000000000000000");

        uint64_t easy[4] = {};
        ok &= expectBool("AlphaPool's difficulty 50000 converts",
                         pearlDifficultyToTarget("50000", easy));
        ok &= expectBool("a 42x easier difficulty is a numerically larger target",
                         !u256Less(easy, t) && u256Less(t, easy));
        ok &= expectBool("difficulty 0 is refused rather than dividing by zero",
                         !pearlDifficultyToTarget("0", easy));

        // The two push shapes, and the lines that must NOT be mistaken for one.
        uint64_t got[4] = {};
        ok &= expectBool("object-form set_difficulty is understood",
                         pearlPoolTargetFromPush(
                             "{\"id\":null,\"method\":\"mining.set_difficulty\","
                             "\"params\":{\"difficulty\":2097152}}", got));
        ok &= expectBool("and yields the same target as the direct conversion",
                         memcmp(got, t, sizeof(t)) == 0);
        memset(got, 0, sizeof(got));
        ok &= expectBool("array-form set_difficulty is understood too",
                         pearlPoolTargetFromPush(
                             "{\"id\":null,\"method\":\"mining.set_difficulty\","
                             "\"params\":[2097152]}", got));
        ok &= expectBool("array form agrees with object form",
                         memcmp(got, t, sizeof(t)) == 0);
        memset(got, 0, sizeof(got));
        ok &= expectBool("set_target takes the hex target verbatim",
                         pearlPoolTargetFromPush(
                             "{\"id\":null,\"method\":\"mining.set_target\",\"params\":"
                             "{\"target\":\"00000000000007fff800000000000000000000000"
                             "00000000000000000000000\"}}", got));
        ok &= expectBool("and it lands in the same limbs a notify target would",
                         memcmp(got, t, sizeof(t)) == 0);
        const uint64_t targetSentinel[4] = {11, 22, 33, 44};
        memcpy(got, targetSentinel, sizeof(got));
        ok &= expectBool("malformed set_target preserves parser output",
                         !pearlPoolTargetFromPush(
                             "{\"method\":\"mining.set_target\",\"params\":{"
                             "\"target\":\"" + std::string(63, '0') + "g\"}}", got) &&
                         memcmp(got, targetSentinel, sizeof(got)) == 0);
        ok &= expectBool("wrong-typed set_target cannot borrow a nested decoy",
                         !pearlPoolTargetFromPush(
                             "{\"method\":\"mining.set_target\",\"params\":{"
                             "\"target\":{\"target\":\"" + std::string(63, '0') +
                             "1\"}}}", got) &&
                         memcmp(got, targetSentinel, sizeof(got)) == 0);
        ok &= expectBool("duplicate direct set_target is rejected",
                         !pearlPoolTargetFromPush(
                             "{\"method\":\"mining.set_target\",\"params\":{"
                             "\"target\":\"" + std::string(63, '0') +
                             "1\",\"target\":\"" + std::string(63, '0') +
                             "2\"}}", got) &&
                         memcmp(got, targetSentinel, sizeof(got)) == 0);
        uint64_t pendingState[4] = {101, 102, 103, 104};
        uint64_t liveState[4] = {201, 202, 203, 204};
        const uint64_t pendingBefore[4] = {101, 102, 103, 104};
        const uint64_t liveBefore[4] = {201, 202, 203, 204};
        bool havePending = true;
        bool haveLiveJob = true;
        ok &= expectBool("malformed recognized target push retires stale state",
                         pearlPoolApplyTargetPush(
                             "{\"method\":\"mining.set_target\",\"params\":{"
                             "\"target\":\"bad\"}}", pendingState,
                             &havePending, liveState, &haveLiveJob) ==
                             PearlPoolTargetPushStatus::Invalid &&
                         !havePending && !haveLiveJob &&
                         u256IsZero(pendingState) && u256IsZero(liveState));
        memcpy(pendingState, pendingBefore, sizeof(pendingState));
        memcpy(liveState, liveBefore, sizeof(liveState));
        havePending = true;
        haveLiveJob = true;
        ok &= expectBool("unrelated reply preserves target state",
                         pearlPoolApplyTargetPush(
                             "{\"id\":9,\"result\":true}", pendingState,
                             &havePending, liveState, &haveLiveJob) ==
                             PearlPoolTargetPushStatus::NotPush &&
                         havePending && haveLiveJob &&
                         memcmp(pendingState, pendingBefore,
                                sizeof(pendingState)) == 0 &&
                         memcmp(liveState, liveBefore, sizeof(liveState)) == 0);
        ok &= expectBool("invalid recognized difficulty also retires stale state",
                         pearlPoolApplyTargetPush(
                             "{\"method\":\"mining.set_difficulty\",\"params\":[8,9]}",
                             pendingState, &havePending, liveState,
                             &haveLiveJob) == PearlPoolTargetPushStatus::Invalid &&
                         !havePending && !haveLiveJob &&
                         u256IsZero(pendingState) && u256IsZero(liveState));

        memset(pendingState, 0, sizeof(pendingState));
        memcpy(liveState, liveBefore, sizeof(liveState));
        havePending = false;
        haveLiveJob = true;
        ok &= expectBool("valid target push is pending and retires current work",
                         pearlPoolApplyTargetPush(
                             "{\"method\":\"mining.set_target\",\"params\":{"
                             "\"target\":\"" + std::string(63, '0') + "1\"}}",
                             pendingState, &havePending, liveState,
                             &haveLiveJob) == PearlPoolTargetPushStatus::Applied &&
                         havePending && !haveLiveJob && u256IsZero(liveState) &&
                         pendingState[0] == 1 && pendingState[1] == 0 &&
                         pendingState[2] == 0 && pendingState[3] == 0);

        // A malformed notify is rejected before target selection, so it cannot
        // spend the pending target intended for the next valid job.
        Job malformedNext;
        ok &= expectBool("malformed notify does not consume pending target",
                         !pearlPoolJobFromNotify(
                             notify(header, "bad", "job-malformed"),
                             &malformedNext) &&
                         havePending && pendingState[0] == 1);

        Job firstValidNotify;
        ok &= expectBool("next notify is independently valid",
                         pearlPoolJobFromNotify(
                             notify(header, hardTarget, "job-next"),
                             &firstValidNotify));
        uint64_t selectedState[4] = {};
        ok &= expectBool("pending target applies to exactly the next valid notify",
                         pearlPoolTargetForNotify(
                             firstValidNotify.target, pendingState, &havePending,
                             selectedState) &&
                         selectedState[0] == 1 && selectedState[1] == 0 &&
                         selectedState[2] == 0 && selectedState[3] == 0 &&
                         !havePending && u256IsZero(pendingState));
        Job followingNotify;
        ok &= expectBool("following notify is independently valid",
                         pearlPoolJobFromNotify(
                             notify(header, easyTarget, "job-following"),
                             &followingNotify));
        memset(selectedState, 0, sizeof(selectedState));
        ok &= expectBool("following notify uses its own explicit target",
                         pearlPoolTargetForNotify(
                             followingNotify.target, pendingState, &havePending,
                             selectedState) &&
                         memcmp(selectedState, followingNotify.target,
                                sizeof(selectedState)) == 0 && !havePending);
        ok &= expectBool("a mining.notify is NOT treated as a difficulty push",
                         !pearlPoolTargetFromPush(
                             notify(std::string(152, 'a'),
                                    std::string(63, '0') + "1", "00000000_2097152"),
                             got));
        ok &= expectBool("a plain submit reply is not either",
                         !pearlPoolTargetFromPush(
                             "{\"id\":101,\"error\":null,\"result\":true}", got));
        ok &= expectBool("a zero difficulty on the wire is refused",
                         !pearlPoolTargetFromPush(
                             "{\"method\":\"mining.set_difficulty\",\"params\":[0]}", got));
        ok &= expectBool("a zero target on the wire is refused",
                         !pearlPoolTargetFromPush(
                             "{\"method\":\"mining.set_target\",\"params\":{\"target\":\"" +
                             std::string(64, '0') + "\"}}", got));

        const std::string difficultyOverflow =
            "115792089237316195423570985008687907853269984665640564039457584007"
            "913129639936";
        const std::vector<std::string> invalidDifficultyPushes = {
            "{\"method\":\"mining.set_difficulty\",\"params\":{\"difficulty\":0}}",
            "{\"method\":\"mining.set_difficulty\",\"params\":{\"difficulty\":01}}",
            "{\"method\":\"mining.set_difficulty\",\"params\":{\"difficulty\":1.5}}",
            "{\"method\":\"mining.set_difficulty\",\"params\":{\"difficulty\":-1}}",
            "{\"method\":\"mining.set_difficulty\",\"params\":{\"difficulty\":1e3}}",
            "{\"method\":\"mining.set_difficulty\",\"params\":{\"difficulty\":\"8\"}}",
            "{\"method\":\"mining.set_difficulty\",\"params\":{\"metadata\":{\"difficulty\":8}}}",
            "{\"method\":\"mining.set_difficulty\",\"params\":{\"difficulty\":8,\"difficulty\":9}}",
            "{\"method\":\"mining.set_difficulty\",\"params\":[8,9]}",
            "{\"method\":\"mining.set_difficulty\",\"params\":[]}",
            "{\"method\":\"mining.set_difficulty\",\"params\":[8] trailing}",
            "{\"method\":\"mining.set_difficulty\",\"params\":{\"difficulty\":" +
                difficultyOverflow + "}}"
        };
        for (size_t i = 0; i < invalidDifficultyPushes.size(); ++i) {
            memcpy(got, targetSentinel, sizeof(got));
            char name[128];
            snprintf(name, sizeof(name),
                     "invalid canonical difficulty case %zu preserves output", i);
            ok &= expectBool(name,
                             !pearlPoolTargetFromPush(invalidDifficultyPushes[i], got) &&
                             memcmp(got, targetSentinel, sizeof(got)) == 0);
        }

        // The division itself, at the boundary a 32-bit-limb implementation
        // would get wrong.
        uint64_t big[4] = {};
        ok &= expectBool("a difficulty above 2^32 still converts",
                         pearlDifficultyToTarget("4294967297", big));
        ok &= expectBool("and gives a harder target than 2^32 - 1 does",
                         u256Less(big, t));
    }

    // --- the accept/reject verdict ------------------------------------
    //
    // Both of these were real, and both were found by the mock-gateway test on
    // its first run rather than by anything here. A verdict read wrong is the
    // worst kind of bug this file can carry: the miner keeps running, the
    // panel keeps printing a hashrate, and the counter simply disagrees with
    // the payout.
    {
        std::string why;
        ok &= expectBool("a compact accept is accepted",
                         pearlPoolReplyAccepted(
                             "{\"id\":1,\"error\":null,\"result\":true}", &why));

        // Ordinary, conforming JSON. The substring version required the exact
        // bytes "result":true and refused this, then blamed the pool for
        // being malformed.
        ok &= expectBool("a space after the colon is still an accept",
                         pearlPoolReplyAccepted(
                             "{\"id\": 1, \"error\": null, \"result\": true}", &why));
        ok &= expectBool("and pretty-printed across lines too",
                         pearlPoolReplyAccepted(
                             "{\n  \"id\": 1,\n  \"error\": null,\n"
                             "  \"result\": true\n}", &why));
        ok &= expectBool("a nested success cannot override a direct rejection",
                         !pearlPoolReplyAccepted(
                             "{\"meta\":{\"result\":true},\"error\":null,"
                             "\"result\":false}", &why));
        ok &= expectBool("duplicate direct verdicts are rejected",
                         !pearlPoolReplyAccepted(
                             "{\"id\":1,\"error\":null,\"result\":true,"
                             "\"result\":false}", &why));
        ok &= expectBool("duplicate direct error members are rejected",
                         !pearlPoolReplyAccepted(
                             "{\"id\":1,\"error\":null,\"error\":{"
                             "\"message\":\"bad share\"},\"result\":true}",
                             &why));

        // A greeting alongside a good result used to be read as a rejection,
        // so the pool credited the share and we did not.
        ok &= expectBool("an accept carrying a message is still an accept",
                         pearlPoolReplyAccepted(
                             "{\"id\":1,\"error\":null,\"result\":true,"
                             "\"message\":\"welcome\"}", &why));

        // The negative half, which must keep working.
        ok &= expectBool("an error object is a rejection",
                         !pearlPoolReplyAccepted(
                             "{\"id\":2,\"result\":null,\"error\":"
                             "{\"code\":23,\"message\":\"Low difficulty share\"}}", &why));
        ok &= expectBool("and its message is the reason we report",
                         why == "Low difficulty share");
        ok &= expectBool("result=false is a rejection",
                         !pearlPoolReplyAccepted(
                             "{\"id\":2,\"result\":false,\"msg\":\"stale\"}", &why));
        ok &= expectBool("with the top-level msg as its wording",
                         why == "stale");
        ok &= expectBool("a reply with neither result nor error is a rejection",
                         !pearlPoolReplyAccepted("{\"id\":2}", &why));
        ok &= expectBool("and says so rather than staying silent", !why.empty());

        ok &= expectBool("an ordinary accepted submit classifies accepted",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"result\":true,\"error\":null}",
                             false, &why) == PearlPoolSubmitVerdict::Accepted);
        ok &= expectBool("a superseded result:true remains accepted with a diagnostic",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"result\":true,\"error\":null}",
                             true, &why) ==
                             PearlPoolSubmitVerdict::AcceptedSuperseded);
        ok &= expectBool("top-level stale text cannot override result true",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"result\":true,"
                             "\"message\":\"stale share\",\"error\":null}",
                             false, &why) == PearlPoolSubmitVerdict::Accepted);
        ok &= expectBool("top-level stale text on result false classifies stale",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"result\":false,"
                             "\"message\":\"stale share\",\"error\":null}",
                             false, &why) == PearlPoolSubmitVerdict::Stale);
        ok &= expectBool("Job not found rejection classifies stale",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"result\":false,\"error\":{"
                             "\"message\":\"Job not found\"}}",
                             false, &why) == PearlPoolSubmitVerdict::Stale);
        // H-019 D4 (live v28s): code 21 is stale only with msg "stale-work"
        // and result null; other text under code 21 is a rejection.
        ok &= expectBool("code 21 with other text is a rejection",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"result\":false,\"error\":{"
                             "\"code\":21,\"message\":\"old work\"}}",
                             false, &why) == PearlPoolSubmitVerdict::Rejected);
        ok &= expectBool("F2Pool stale-work reply classifies stale",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"error\":{\"code\":21,\"msg\":\"stale-work\"},"
                             "\"result\":null}",
                             false, &why) == PearlPoolSubmitVerdict::Stale);
        ok &= expectBool("F2Pool code 21 with another msg is a rejection",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"error\":{\"code\":21,\"msg\":\"other\"},"
                             "\"result\":null}",
                             false, &why) == PearlPoolSubmitVerdict::Rejected);
        ok &= expectBool("F2Pool code 22 with stale-work text is a rejection",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"error\":{\"code\":22,\"msg\":\"stale-work\"},"
                             "\"result\":null}",
                             false, &why) == PearlPoolSubmitVerdict::Rejected);
        ok &= expectBool("Kryptex [-1,\"Stale share\",null] classifies stale",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"result\":false,"
                             "\"error\":[-1,\"Stale share\",null]}",
                             false, &why) == PearlPoolSubmitVerdict::Stale);
        ok &= expectBool("an ordinary false submit classifies rejected",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"result\":false,"
                             "\"msg\":\"low difficulty\"}",
                             false, &why) == PearlPoolSubmitVerdict::Rejected);
        ok &= expectBool("informational stale text cannot override success",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"result\":true,"
                             "\"message\":\"stale shares: 0\"}",
                             false, &why) == PearlPoolSubmitVerdict::Accepted);
        ok &= expectBool("negated stale text cannot override success",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"result\":true,"
                             "\"message\":\"not stale\"}",
                             false, &why) == PearlPoolSubmitVerdict::Accepted);
        ok &= expectBool("a correlated reply without a verdict is unknown",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2}", false, &why) ==
                             PearlPoolSubmitVerdict::Unknown);
        ok &= expectBool("a null result without an error is unknown",
                         pearlPoolClassifySubmitReply(
                             "{\"id\":2,\"result\":null,\"error\":null}",
                             false, &why) == PearlPoolSubmitVerdict::Unknown);

        PearlPoolSubmitCounters counters;
        counters.noteSubmitted();
        ok &= expectBool("a fresh submission is pending",
                         counters.pending() == 1 && counters.conserved());
        ok &= expectBool("one accepted verdict is recorded once",
                         counters.noteVerdict(PearlPoolSubmitVerdict::Accepted) &&
                             counters.accepted == 1 &&
                             counters.acknowledged == 1 &&
                             counters.pending() == 0 &&
                             counters.conserved());
        ok &= expectBool("a second verdict cannot double-count one submission",
                         !counters.noteVerdict(PearlPoolSubmitVerdict::Rejected) &&
                             counters.accepted == 1 && counters.rejected == 0 &&
                             counters.conserved());
        const PearlPoolSubmitVerdict allVerdicts[] = {
            PearlPoolSubmitVerdict::Rejected,
            PearlPoolSubmitVerdict::Stale,
            PearlPoolSubmitVerdict::AcceptedSuperseded,
            PearlPoolSubmitVerdict::Unknown,
        };
        for (PearlPoolSubmitVerdict verdict : allVerdicts) {
            counters.noteSubmitted();
            ok &= expectBool("each Pearl submit outcome preserves conservation",
                             counters.noteVerdict(verdict) &&
                                 counters.pending() == 0 && counters.conserved());
        }
        ok &= expectBool("all Pearl submit outcome counters are distinct",
                         counters.submitted == 5 && counters.accepted == 2 &&
                             counters.rejected == 1 && counters.stale == 1 &&
                             counters.acceptedSuperseded == 1 &&
                             counters.unknown == 1 &&
                             counters.acknowledged == 5 &&
                             counters.classified() == 5);

        PearlPoolSubmitCounters noReply;
        noReply.noteSubmitted();
        ok &= expectBool("a timeout is terminal unknown but not acknowledged",
                         noReply.noteVerdict(PearlPoolSubmitVerdict::Unknown,
                                             false) &&
                             noReply.unknown == 1 &&
                             noReply.acknowledged == 0 &&
                             noReply.pending() == 0 && noReply.conserved());

        PoolOutcomeSnapshot impossible;
        impossible.submitted = 1;
        impossible.accepted = 1;
        ok &= expectBool("acceptance cannot exist without acknowledgement",
                         !impossible.conserved());

        PoolOutcomeSnapshot overflow;
        overflow.submitted = UINT64_MAX;
        overflow.acknowledged = UINT64_MAX;
        overflow.accepted = UINT64_MAX;
        overflow.rejected = 1;
        ok &= expectBool("classified counters saturate and conservation fails closed",
                         overflow.classified() == UINT64_MAX &&
                             !overflow.conserved());
    }

    // --- dispatching on the method ------------------------------------
    //
    // A substring search matches the method name wherever it appears. The
    // dangerous one is handlePush(): in submit()'s reply loop it swallows any
    // line it claims, so a rejection whose wording mentions mining.notify used
    // to eat the share's own verdict.
    {
        uint64_t g[4] = {};
        ok &= expectBool("a set_target push is still recognised",
                         pearlPoolTargetFromPush(
                             "{\"method\":\"mining.set_target\",\"params\":"
                             "{\"target\":\"" + std::string(63, '0') + "1\"}}", g));
        // Both of these carry the value the old code would then read, so they
        // discriminate: byte-matching returns true for each.
        ok &= expectBool("a reply that merely MENTIONS the method is not a push",
                         !pearlPoolTargetFromPush(
                             "{\"id\":9,\"result\":false,\"target\":\"" +
                             std::string(63, '0') + "1\",\"error\":{\"code\":21,"
                             "\"message\":\"stale, see mining.set_target\"}}", g));
        ok &= expectBool("nor is a job whose id contains it",
                         !pearlPoolTargetFromPush(
                             "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                             "{\"job_id\":\"mining.set_difficulty\",\"difficulty\":8}}", g));
        ok &= expectBool("the method field is read, not searched for",
                         pearlPoolMethod(
                             "{\"id\": null, \"method\": \"mining.notify\"}") ==
                             "mining.notify");
        ok &= expectBool("escaped direct keys decode to their JSON spelling",
                         pearlPoolMethod(
                             "{\"meth\\u006fd\":\"mining.notify\"}") ==
                             "mining.notify");
        ok &= expectBool("escaped duplicate direct keys are rejected",
                         pearlPoolMethod(
                             "{\"method\":\"mining.notify\","
                             "\"meth\\u006fd\":\"mining.set_target\"}").empty());
        ok &= expectBool("a line with no method has none",
                         pearlPoolMethod("{\"id\":9,\"result\":true}").empty());
    }

    // --- matching a reply to its request ------------------------------
    //
    // A missed reply is silent: submit() gives up with "no reply from the
    // pool", the share is not counted, and the pool credits it anyway.
    {
        ok &= expectBool("a compact reply matches its request",
                         pearlPoolReplyIsFor("{\"id\":101,\"result\":true}", 101));
        ok &= expectBool("a space after the colon still matches",
                         pearlPoolReplyIsFor("{\"id\": 101, \"result\": true}", 101));
        ok &= expectBool("a string id matches its number",
                         pearlPoolReplyIsFor("{\"id\":\"101\"}", 101));
        ok &= expectBool("somebody else's reply does not match",
                         !pearlPoolReplyIsFor("{\"id\":102,\"result\":true}", 101));
        ok &= expectBool("and a push, whose id is null, does not either",
                         !pearlPoolReplyIsFor(
                             "{\"id\":null,\"method\":\"mining.notify\"}", 101));
        // 101 must not match 1011 or 1: the old substring test matched both.
        ok &= expectBool("a longer id is not a prefix match",
                         !pearlPoolReplyIsFor("{\"id\":1011}", 101));
        ok &= expectBool("a malformed string id is not a prefix match",
                         !pearlPoolReplyIsFor("{\"id\":\"101x\"}", 101));
        ok &= expectBool("a fractional id is not truncated",
                         !pearlPoolReplyIsFor("{\"id\":101.5}", 101));
        ok &= expectBool("an exponent id is not coerced",
                         !pearlPoolReplyIsFor("{\"id\":101e1}", 101));
        ok &= expectBool("a nested id cannot impersonate the direct id",
                         !pearlPoolReplyIsFor(
                             "{\"meta\":{\"id\":101},\"id\":102}", 101));
        ok &= expectBool("duplicate direct ids are rejected",
                         !pearlPoolReplyIsFor("{\"id\":101,\"id\":102}",
                                              101));
    }

    if (!ok) return 1;
    puts("pearl pool JSON payloads: ok");
    return 0;
}
