// Worker-only bridge from a verified backend proof to run-owned authority.
#pragma once

#include "algo.h"

namespace om {

/**
 * The run owner opens/reserves its proof ledger before synchronous search and
 * captures the exact source session, authorization, job digest and search
 * generation. This hook checks the supplied Job against that captured identity
 * and binds the exact serialized Solution. It must never infer authority from
 * a returned result after search, or from whichever source job is newest.
 *
 * Input pearlProofToken is zero. On success only pearlProofToken may change,
 * to a nonzero ledger token; nonce, hit and extra remain untouched. Failure
 * authorizes no submission and makes the backend fail the entire search. The
 * run owner then invalidates all bindings/backlog for that search, including
 * bindings created before a subsequent output-allocation failure. No reconnect
 * or retry may restore a consumed/invalidated token.
 *
 * Calls are synchronous, worker-thread-only and noexcept. The pointer is
 * borrowed: clear the backend binding before destroying this sink on every
 * normal/exceptional exit, never while search or a callback is active.
 */
class PearlProofBindingSink {
   public:
    virtual ~PearlProofBindingSink() = default;
    virtual bool bindSerializedProof(const Job &job,
                                     Solution *solution) noexcept = 0;
};

}  // namespace om
