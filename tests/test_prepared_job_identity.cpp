// Host-only checks for the run loop's prepared-job ownership snapshot.

#include <cstdio>
#include <cstring>
#include <string>

#include "../src/core/run.h"

using namespace om;

namespace {

bool check(bool condition, const char *message) {
    if (condition) return true;
    std::fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

Job baseJob() {
    Job job{};
    job.valid = true;
    job.epoch = 17;
    for (size_t i = 0; i < sizeof(job.msg); ++i)
        job.msg[i] = static_cast<uint8_t>(i ^ 0xa5U);
    for (size_t i = 0; i < 4; ++i)
        job.target[i] = 0x1020304050607080ULL + i;
    job.extra = std::string("certificate\0v3", 14);
    job.jobId = std::string("pool\0job-A", 10);
    return job;
}

bool sameJob(const Job &a, const Job &b) {
    return a.valid == b.valid && a.epoch == b.epoch &&
           std::memcmp(a.msg, b.msg, sizeof(a.msg)) == 0 &&
           std::memcmp(a.target, b.target, sizeof(a.target)) == 0 &&
           a.extra == b.extra && a.jobId == b.jobId;
}

bool samePrepared(const PreparedJob &a, const PreparedJob &b) {
    return a.valid == b.valid && a.epoch == b.epoch &&
           std::memcmp(a.msg, b.msg, sizeof(a.msg)) == 0 &&
           std::memcmp(a.target, b.target, sizeof(a.target)) == 0 &&
           a.extra == b.extra && a.jobId == b.jobId;
}

}  // namespace

int main() {
    bool ok = true;
    const Job original = baseJob();
    const Job inputBefore = original;

    PreparedJob prepared{};
    ok &= check(shouldPrepare(true, prepared, original),
                "an empty Pearl snapshot requires preparation");
    ok &= check(prepared.take(original), "a valid job can be adopted");
    ok &= check(sameJob(original, inputBefore),
                "take is transactional with respect to its input");
    ok &= check(prepared.jobId.size() == original.jobId.size() &&
                    prepared.jobId == original.jobId && prepared.jobId[4] == '\0',
                "take preserves the full embedded-NUL transport job id");
    ok &= check(!shouldPrepare(true, prepared, original),
                "an exactly unchanged Pearl job reuses preparation");
    ok &= check(sameRunLoopJobIdentity(original, original),
                "the live adoption gate retains an exactly unchanged job");

    const PreparedJob beforeProbe = prepared;
    Job changedId = original;
    changedId.jobId = std::string("pool\0job-B", 10);
    ok &= check(changedId.epoch == original.epoch &&
                    changedId.extra == original.extra &&
                    std::memcmp(changedId.msg, original.msg,
                                sizeof(original.msg)) == 0 &&
                    std::memcmp(changedId.target, original.target,
                                sizeof(original.target)) == 0,
                "the changed-id fixture changes no computational field");
    ok &= check(shouldPrepare(true, prepared, changedId),
                "a jobId-only Pearl change cannot reuse prepared ownership");
    ok &= check(!sameRunLoopJobIdentity(original, changedId),
                "the live adoption gate observes a jobId-only refresh");
    ok &= check(samePrepared(prepared, beforeProbe),
                "matches/shouldPrepare probes do not mutate the snapshot");
    ok &= check(!shouldPrepare(false, prepared, changedId),
                "non-Pearl epoch-only dataset preparation remains unchanged");

    ok &= check(prepared.take(changedId),
                "a valid changed-id job can be adopted");
    ok &= check(prepared.matches(changedId),
                "take transactionally adopts the changed transport id");
    ok &= check(!prepared.matches(original),
                "the prior transport id no longer owns the snapshot");
    ok &= check(sameRunLoopJobIdentity(changedId, changedId),
                "the adopted job remains stable on the next source poll");

    const PreparedJob beforeRepeat = prepared;
    ok &= check(prepared.take(changedId),
                "an unchanged valid job can be re-taken");
    ok &= check(samePrepared(prepared, beforeRepeat),
                "re-taking an unchanged job is value-idempotent");

    Job changedAfterTake = changedId;
    changedAfterTake.jobId[9] = 'C';
    changedAfterTake.extra[0] = 'X';
    ok &= check(prepared.matches(changedId) &&
                    !prepared.matches(changedAfterTake),
                "the snapshot owns independent string copies");

    Job invalid = changedId;
    invalid.valid = false;
    const PreparedJob beforeInvalid = prepared;
    ok &= check(!sameRunLoopJobIdentity(changedId, invalid),
                "the live adoption gate observes a validity change");
    ok &= check(!sameRunLoopJobIdentity(invalid, invalid),
                "two invalid jobs never form reusable live ownership");
    ok &= check(!prepared.matches(invalid),
                "an invalid job cannot match prepared ownership");
    ok &= check(!prepared.take(invalid) && samePrepared(prepared, beforeInvalid),
                "an invalid take fails closed without erasing prior ownership");

    if (!ok) return 1;
    std::puts("prepared-job identity tests passed");
    return 0;
}
