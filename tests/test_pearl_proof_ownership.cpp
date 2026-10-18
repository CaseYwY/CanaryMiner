#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <type_traits>

#define PEARL_PROOF_OWNERSHIP_ENABLE_TEST_SEAMS 1
#include "core/pearl_proof_ownership.h"

namespace {

using namespace om;

int failures = 0;

void check(const char *name, bool value) {
    if (value) {
        std::printf("  %-68s ok\n", name);
        return;
    }
    std::fprintf(stderr, "FAIL: %s\n", name);
    ++failures;
}

PearlProofJobIdentity jobIdentity(uint8_t marker = 1) {
    PearlProofJobIdentity job;
    job.session = marker;
    job.authorization = static_cast<uint64_t>(marker) + 10;
    job.job = static_cast<uint64_t>(marker) + 20;
    for (std::size_t i = 0; i < job.digest.bytes.size(); ++i)
        job.digest.bytes[i] = static_cast<uint8_t>(marker + i);
    return job;
}

Solution solution(uint64_t nonce, char proofMarker) {
    Solution out;
    out.nonce = nonce;
    for (std::size_t i = 0; i < 4; ++i)
        out.hit[i] = nonce + static_cast<uint64_t>(i * 17);
    const char proof[] = {proofMarker, '\0', static_cast<char>(proofMarker + 1)};
    out.extra.assign(proof, sizeof(proof));
    return out;
}

PearlSerializedProofMetadata metadata(const Solution &solution) {
    PearlSerializedProofMetadata out;
    check("exact solution metadata is constructed",
          pearlSerializedProofMetadataFromSolution(solution, &out));
    return out;
}

void openSearch(PearlProofOwnershipLedger *ledger,
                const PearlProofJobIdentity &job, uint64_t generation,
                uint64_t maximumProofs) {
    check("one exact proof-ownership search opens",
          ledger && ledger->beginSearch(job, generation, maximumProofs) &&
              ledger->searchOpen() &&
              ledger->maximumProofs() == maximumProofs);
}

void testExactSingleUseOwnership() {
    std::puts("exact, order-independent, single-use ownership:");
    PearlProofOwnershipLedger ledger;
    const PearlProofJobIdentity job = jobIdentity();
    const PearlSerializedProofMetadata a = metadata(solution(100, 'a'));
    const PearlSerializedProofMetadata b = metadata(solution(200, 'b'));
    openSearch(&ledger, job, 7, 2);
    uint64_t tokenA = 99;
    uint64_t tokenB = 99;
    check("first serialized proof receives a nonzero token",
          ledger.bind(job, 7, a, &tokenA) && tokenA == 1);
    check("second serialized proof receives the next token",
          ledger.bind(job, 7, b, &tokenB) && tokenB == 2);
    check("both exact records remain active", ledger.activeCount() == 2);

    PearlProofBinding consumed;
    check("second vector result may consume its exact record first",
          ledger.consume(tokenB, job, 7, b, &consumed));
    check("consumed binding retains exact proof ownership",
          consumed.token == tokenB && consumed.searchGeneration == 7 &&
              pearlProofJobIdentityEqual(consumed.job, job) &&
              pearlSerializedProofMetadataEqual(consumed.proof, b));
    check("first exact record remains independently consumable",
          ledger.consume(tokenA, job, 7, a, &consumed));
    check("successful consumption removes every active record",
          ledger.activeCount() == 0 && ledger.reconcileSearch(job, 7));
}

void testMetadataCoversEverySubmittedField() {
    std::puts("submitted representation coverage:");
    const Solution base = solution(300, 'q');
    const PearlSerializedProofMetadata expected = metadata(base);
    Solution changed = base;
    changed.nonce ^= UINT64_C(1) << 63;
    check("nonce mutation changes metadata",
          !pearlSerializedProofMetadataEqual(expected, metadata(changed)));
    for (std::size_t i = 0; i < 4; ++i) {
        changed = base;
        changed.hit[i] ^= UINT64_C(1) << (i + 3);
        check("each hit limb mutation changes metadata",
              !pearlSerializedProofMetadataEqual(expected, metadata(changed)));
    }
    for (std::size_t i = 0; i < base.extra.size(); ++i) {
        changed = base;
        changed.extra[i] = static_cast<char>(changed.extra[i] ^ 1);
        check("each exact proof byte changes metadata",
              !pearlSerializedProofMetadataEqual(expected, metadata(changed)));
    }
    changed = base;
    changed.extra.push_back('\0');
    check("proof length and trailing NUL are identity",
          !pearlSerializedProofMetadataEqual(expected, metadata(changed)));
    changed = base;
    changed.pearlProofToken = 123;
    check("opaque token is not part of submitted proof identity",
          pearlSerializedProofMetadataEqual(expected, metadata(changed)));

    PearlSerializedProofMetadata sentinel = expected;
    Solution empty = base;
    empty.extra.clear();
    check("empty host-invalid diagnostic owns no serialized metadata",
          !pearlSerializedProofMetadataFromSolution(empty, &sentinel) &&
              !pearlSerializedProofMetadataValid(sentinel));
    check("null metadata output is refused safely",
          !pearlSerializedProofMetadataFromSolution(base, nullptr));
}

void testMismatchQuarantinesTransactionally() {
    std::puts("mismatch and replay quarantine:");
    PearlProofOwnershipLedger ledger;
    const PearlProofJobIdentity job = jobIdentity();
    const PearlSerializedProofMetadata proof = metadata(solution(400, 'm'));
    openSearch(&ledger, job, 9, 1);
    uint64_t token = 0;
    check("baseline binding succeeds", ledger.bind(job, 9, proof, &token));
    PearlSerializedProofMetadata wrong = proof;
    ++wrong.nonce;
    PearlProofBinding out;
    out.token = 777;
    check("wrong nonce cannot consume a valid token",
          !ledger.consume(token, job, 9, wrong, &out));
    check("failed consume zeroes output and quarantines ledger",
          out.token == 0 && ledger.quarantined() &&
              ledger.activeCount() == 1);
    uint64_t refused = 777;
    check("quarantine refuses later bindings with zero output",
          !ledger.bind(job, 9, proof, &refused) && refused == 0);

    PearlProofOwnershipLedger wrongJobLedger;
    openSearch(&wrongJobLedger, job, 10, 1);
    check("second baseline binding succeeds",
          wrongJobLedger.bind(job, 10, proof, &token));
    check("wrong source job identity is rejected",
          !wrongJobLedger.consume(token, jobIdentity(2), 10, proof, &out) &&
              wrongJobLedger.quarantined());

    PearlProofOwnershipLedger replayLedger;
    openSearch(&replayLedger, job, 11, 1);
    check("replay baseline binding succeeds",
          replayLedger.bind(job, 11, proof, &token));
    check("exact record is consumed once",
          replayLedger.consume(token, job, 11, proof, &out));
    check("the same token cannot be consumed twice",
          !replayLedger.consume(token, job, 11, proof, &out) &&
              replayLedger.quarantined());
}

void testAliasedOutputsRejectBeforeMutation() {
    std::puts("aliased output rejection:");
    const PearlProofJobIdentity job = jobIdentity();
    PearlSerializedProofMetadata proof = metadata(solution(450, 'x'));
    const PearlSerializedProofMetadata originalProof = proof;
    PearlProofOwnershipLedger bindLedger;
    openSearch(&bindLedger, job, 10, 1);
    check("token output aliasing proof metadata is rejected",
          !bindLedger.bind(job, 10, proof, &proof.nonce));
    check("aliased bind leaves caller metadata byte-exact and quarantines",
          pearlSerializedProofMetadataEqual(proof, originalProof) &&
              bindLedger.activeCount() == 0 && bindLedger.quarantined());

    PearlProofOwnershipLedger consumeLedger;
    openSearch(&consumeLedger, job, 10, 1);
    uint64_t token = 0;
    check("consume-alias baseline binding succeeds",
          consumeLedger.bind(job, 10, proof, &token));
    PearlProofJobIdentity oversizedJobStorage[2] = {job, jobIdentity(3)};
    const PearlProofJobIdentity originalJob = oversizedJobStorage[0];
    PearlProofBinding *aliased =
        reinterpret_cast<PearlProofBinding *>(&oversizedJobStorage[0]);
    check("binding output aliasing job identity is rejected",
          !consumeLedger.consume(token, oversizedJobStorage[0], 10, proof,
                                 aliased));
    check("aliased consume leaves caller job unchanged and record owned",
          pearlProofJobIdentityEqual(oversizedJobStorage[0], originalJob) &&
              consumeLedger.activeCount() == 1 &&
              consumeLedger.quarantined());

    PearlProofOwnershipLedger selfBindLedger;
    openSearch(&selfBindLedger, job, 11, 1);
    uint64_t *selfToken = reinterpret_cast<uint64_t *>(&selfBindLedger);
    check("token output overlapping the ledger itself is rejected",
          !selfBindLedger.bind(job, 11, proof, selfToken) &&
              selfBindLedger.activeCount() == 0 &&
              selfBindLedger.quarantined());

    PearlProofOwnershipLedger selfConsumeLedger;
    openSearch(&selfConsumeLedger, job, 12, 1);
    check("self-consume baseline binding succeeds",
          selfConsumeLedger.bind(job, 12, proof, &token));
    PearlProofBinding *selfOutput =
        reinterpret_cast<PearlProofBinding *>(&selfConsumeLedger);
    check("binding output overlapping the ledger itself is rejected",
          !selfConsumeLedger.consume(token, job, 12, proof, selfOutput) &&
              selfConsumeLedger.activeCount() == 1 &&
              selfConsumeLedger.quarantined());

    PearlProofOwnershipLedger recordConsumeLedger;
    openSearch(&recordConsumeLedger, job, 13, 1);
    check("record-alias baseline binding succeeds",
          recordConsumeLedger.bind(job, 13, proof, &token));
    PearlProofBinding *recordOutput =
        recordConsumeLedger.bindingStorageForTest();
    check("binding output overlapping reserved record storage is rejected",
          recordOutput != nullptr &&
              !recordConsumeLedger.consume(token, job, 13, proof,
                                            recordOutput) &&
              recordConsumeLedger.activeCount() == 1 &&
              recordConsumeLedger.quarantined());
}

void testDuplicateCapacityAndExhaustion() {
    std::puts("duplicate, capacity, and token exhaustion:");
    const PearlProofJobIdentity job = jobIdentity();
    const PearlSerializedProofMetadata proof = metadata(solution(500, 'd'));
    PearlProofOwnershipLedger duplicate;
    openSearch(&duplicate, job, 12, 2);
    uint64_t token = 0;
    check("duplicate baseline binding succeeds",
          duplicate.bind(job, 12, proof, &token));
    uint64_t refused = 44;
    check("same active proof cannot receive a second token",
          !duplicate.bind(job, 12, proof, &refused) && refused == 0 &&
              duplicate.quarantined());

    PearlProofOwnershipLedger full;
    openSearch(&full, job, 13, 2);
    PearlSerializedProofMetadata first = metadata(solution(1000, 'A'));
    PearlSerializedProofMetadata second = metadata(solution(1001, 'B'));
    uint64_t assigned = 0;
    const bool filled = full.bind(job, 13, first, &assigned) && assigned == 1 &&
                        full.bind(job, 13, second, &assigned) && assigned == 2;
    check("search admits its exact pre-reserved result budget",
          filled && full.activeCount() == 2);
    PearlSerializedProofMetadata overflow = metadata(solution(9999, 'z'));
    check("capacity overflow fails closed and quarantines",
          !full.bind(job, 13, overflow, &refused) && refused == 0 &&
              full.quarantined());

    PearlProofOwnershipLedger exhausted;
    openSearch(&exhausted, job, 14, 1);
    exhausted.setLastTokenForTest(std::numeric_limits<uint64_t>::max());
    check("token exhaustion fails closed without wraparound",
          !exhausted.bind(job, 14, proof, &refused) && refused == 0 &&
              exhausted.lastToken() ==
                  std::numeric_limits<uint64_t>::max() &&
              exhausted.quarantined());
}

void testSearchReconciliationAndInvalidation() {
    std::puts("search reconciliation and explicit invalidation:");
    const PearlProofJobIdentity job = jobIdentity();
    const PearlSerializedProofMetadata a = metadata(solution(600, 'r'));
    const PearlSerializedProofMetadata b = metadata(solution(601, 's'));
    PearlProofOwnershipLedger orphaned;
    openSearch(&orphaned, job, 15, 1);
    uint64_t token = 0;
    check("orphan baseline binding succeeds",
          orphaned.bind(job, 15, a, &token));
    check("unconsumed record makes successful reconciliation fail closed",
          !orphaned.reconcileSearch(job, 15) && orphaned.quarantined());

    PearlProofOwnershipLedger failedSearch;
    openSearch(&failedSearch, job, 16, 2);
    check("first failed-search record binds",
          failedSearch.bind(job, 16, a, &token));
    check("second failed-search record binds",
          failedSearch.bind(job, 16, b, &token));
    check("explicit search failure invalidates every owned result",
          failedSearch.invalidateSearch(job, 16) == 2 &&
              failedSearch.activeCount() == 0 &&
              !failedSearch.searchOpen() &&
              !failedSearch.quarantined());

    uint64_t newToken = 0;
    openSearch(&failedSearch, job, 17, 1);
    check("later search token remains monotonic after invalidation",
          failedSearch.bind(job, 17, a, &newToken) && newToken == 3);
    check("exact job retirement clears active bindings but not token history",
          failedSearch.invalidateSearch(job, 17) == 1 &&
              failedSearch.activeCount() == 0 &&
              failedSearch.lastToken() == 3 && !failedSearch.searchOpen() &&
              !failedSearch.quarantined());
}

void testSearchAuthorityAndLargeResultSet() {
    std::puts("single-search authority and result cardinality:");
    const PearlProofJobIdentity job = jobIdentity();
    const PearlProofJobIdentity wrongJob = jobIdentity(9);

    PearlProofOwnershipLedger simultaneous;
    openSearch(&simultaneous, job, 20, 1);
    check("a second generation cannot coexist with an open search",
          !simultaneous.beginSearch(job, 21, 1) &&
              simultaneous.searchOpen() && simultaneous.quarantined());

    PearlProofOwnershipLedger reused;
    openSearch(&reused, job, 30, 1);
    check("first generation closes without results",
          reused.reconcileSearch(job, 30));
    check("a generation cannot be reused after normal close",
          !reused.beginSearch(job, 30, 1) && reused.quarantined());

    PearlProofOwnershipLedger decreasing;
    openSearch(&decreasing, job, 32, 1);
    check("higher generation closes without results",
          decreasing.reconcileSearch(job, 32));
    check("a generation cannot regress after normal close",
          !decreasing.beginSearch(job, 31, 1) &&
              decreasing.quarantined());

    PearlProofOwnershipLedger wrongReconcile;
    openSearch(&wrongReconcile, job, 22, 1);
    uint64_t token = 0;
    const PearlSerializedProofMetadata proof = metadata(solution(700, 'u'));
    check("wrong-reconcile baseline binding succeeds",
          wrongReconcile.bind(job, 22, proof, &token));
    check("wrong generation cannot reconcile an owned result",
          !wrongReconcile.reconcileSearch(job, 23) &&
              wrongReconcile.activeCount() == 1 &&
              wrongReconcile.quarantined());

    PearlProofOwnershipLedger wrongInvalidate;
    openSearch(&wrongInvalidate, job, 24, 1);
    check("wrong-invalidate baseline binding succeeds",
          wrongInvalidate.bind(job, 24, proof, &token));
    check("wrong job cannot invalidate an owned result",
          wrongInvalidate.invalidateSearch(wrongJob, 24) == 0 &&
              wrongInvalidate.activeCount() == 1 &&
              wrongInvalidate.quarantined());

    PearlProofOwnershipLedger empty;
    openSearch(&empty, job, 25, 1);
    check("a zero-result search reconciles and closes exactly",
          empty.reconcileSearch(job, 25) && !empty.searchOpen() &&
              empty.activeCount() == 0 && !empty.quarantined());

    PearlProofOwnershipLedger excessive;
    check("an excessive declared result budget fails before opening",
          !excessive.beginSearch(job, 26,
                                 kPearlProofOwnershipMaxPerSearch + 1) &&
              !excessive.searchOpen() && excessive.activeCount() == 0 &&
              excessive.quarantined());

    PearlProofOwnershipLedger large;
    constexpr uint64_t kResults = 65;
    openSearch(&large, job, 27, kResults);
    bool allBound = true;
    for (uint64_t i = 0; i < kResults; ++i) {
        const PearlSerializedProofMetadata item =
            metadata(solution(10000 + i, static_cast<char>('A' + (i % 26))));
        uint64_t assigned = 0;
        allBound = allBound && large.bind(job, 27, item, &assigned) &&
                   assigned == i + 1;
    }
    check("one search may own more than the per-attempt 64-hit queue",
          allBound && large.activeCount() == kResults &&
              !large.quarantined());
    check("explicit failure retires the full large result set",
          large.invalidateSearch(job, 27) == kResults &&
              large.activeCount() == 0 && !large.searchOpen());

    PearlProofOwnershipLedger stale;
    openSearch(&stale, job, 28, 1);
    uint64_t oldToken = 0;
    check("old-search proof binds", stale.bind(job, 28, proof, &oldToken));
    PearlProofBinding consumed;
    check("old-search proof consumes",
          stale.consume(oldToken, job, 28, proof, &consumed) &&
              stale.reconcileSearch(job, 28));
    openSearch(&stale, job, 29, 1);
    check("an old token cannot be replayed into a new search",
          !stale.consume(oldToken, job, 29, proof, &consumed) &&
              stale.quarantined());
}

void testInvalidOpenAliasingAndAllocationFailure() {
    std::puts("invalid open, input alias, and allocation failure:");
    const PearlProofJobIdentity job = jobIdentity();
    PearlProofJobIdentity invalidJob = job;
    invalidJob.digest = PearlJobIdentityDigest();

    PearlProofOwnershipLedger zeroGeneration;
    check("zero generation cannot open a search",
          !zeroGeneration.beginSearch(job, 0, 1) &&
              zeroGeneration.quarantined());
    PearlProofOwnershipLedger zeroBudget;
    check("zero result budget cannot open a search",
          !zeroBudget.beginSearch(job, 1, 0) && zeroBudget.quarantined());
    PearlProofOwnershipLedger invalidIdentity;
    check("invalid source identity cannot open a search",
          !invalidIdentity.beginSearch(invalidJob, 1, 1) &&
              invalidIdentity.quarantined());

    PearlProofOwnershipLedger allocation;
    allocation.failNextReserveForTest();
    check("reserve allocation failure is contained and quarantined",
          !allocation.beginSearch(job, 1, 1) && !allocation.searchOpen() &&
              allocation.activeCount() == 0 && allocation.quarantined());

    PearlProofOwnershipLedger jobAlias;
    openSearch(&jobAlias, job, 40, 1);
    const PearlProofJobIdentity *ownedJob = jobAlias.activeJobForTest();
    const PearlSerializedProofMetadata proof = metadata(solution(800, 'v'));
    uint64_t token = 99;
    check("job input referencing ledger storage is rejected before bind",
          ownedJob != nullptr && !jobAlias.bind(*ownedJob, 40, proof, &token) &&
              token == 0 && jobAlias.activeCount() == 0 &&
              jobAlias.quarantined());

    PearlProofOwnershipLedger proofAlias;
    openSearch(&proofAlias, job, 41, 2);
    check("proof-alias baseline binding succeeds",
          proofAlias.bind(job, 41, proof, &token));
    const PearlProofBinding *ownedRecord =
        proofAlias.bindingStorageForTest();
    const PearlSerializedProofMetadata *ownedProof =
        ownedRecord ? &ownedRecord->proof : &proof;
    uint64_t secondToken = 99;
    check("proof input referencing ledger storage is rejected before bind",
          ownedRecord != nullptr &&
              !proofAlias.bind(job, 41, *ownedProof, &secondToken) &&
              secondToken == 0 && proofAlias.activeCount() == 1 &&
              proofAlias.quarantined());
}

}  // namespace

int main() {
    static_assert(!std::is_copy_constructible<PearlProofOwnershipLedger>::value,
                  "live proof ownership must never be copied");
    static_assert(!std::is_copy_assignable<PearlProofOwnershipLedger>::value,
                  "live proof ownership must never be copy-assigned");
    static_assert(!std::is_move_constructible<PearlProofOwnershipLedger>::value,
                  "live proof ownership must never be moved implicitly");
    static_assert(!std::is_move_assignable<PearlProofOwnershipLedger>::value,
                  "live proof ownership must never be move-assigned");
    testExactSingleUseOwnership();
    testMetadataCoversEverySubmittedField();
    testMismatchQuarantinesTransactionally();
    testAliasedOutputsRejectBeforeMutation();
    testDuplicateCapacityAndExhaustion();
    testSearchReconciliationAndInvalidation();
    testSearchAuthorityAndLargeResultSet();
    testInvalidOpenAliasingAndAllocationFailure();
    if (failures != 0) {
        std::fprintf(stderr, "%d Pearl proof ownership checks failed\n",
                     failures);
        return 1;
    }
    std::puts("\nPearl exact proof ownership: ok");
    return 0;
}
