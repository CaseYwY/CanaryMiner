# Quoted credentials and bounded output-line privacy (P-14m)

M-PRIVACY; R-PRIV-001/002; T-PRIVACY/T-RELEASE; D-169.

The existing log filter now covers known credential options with whitespace, surrounding/escaped/compound quotes and escaped spaces, plus string-valued known JSON keys with whitespace or a truncated closing quote. The output tee no longer starts a new redaction context in the middle of an oversized raw line. It omits that complete line, emits a fixed omission notice, keeps draining, and resumes only after an original newline or a later explicit new stream.

## Preserved boundaries

This is conservative text filtering, not a command interpreter, complete JSON parser, secret detector or comprehensive anonymization. Exact registered-secret replacement and deliberate address-tail presentation remain unchanged. Unknown formats/keys, arbitrary paths/device/account/game/financial details and a JSON value arriving in a different original line are not made safe for disclosure. Raw logs remain private; this repair grants no export, provider, collection or retention permission.

Known option tokens are filtered through their complete whitespace-delimited value, including embedded quotes and escaped whitespace. Simple surrounding quotes stay visible, but compound private tokens are replaced as a whole. Known JSON string values redact through the supplied text's end when unfinished. Adjacent public bytes remain for the covered formats; the filter does not validate those bytes or change any parsed mining/configuration/protocol input.

The tee's existing **65536-byte raw-line length boundary** now yields `[worker output omitted: oversized line]` rather than emitting independently filtered fragments. No raw prefix or credential suffix of that oversized line is published. Both stdout/stderr keep draining; subsequent ordinary lines survive. EOF finishes the current context so explicitly restarting the same tee does not discard the first new stderr line. The string's allocator capacity, synchronous sink/console write latency and broader log redaction/privacy remain separately unqualified.

No logs/configuration/credentials from a user are read by these tests. No new background/native process, network, GPU, game, mining command, donation/proof change or automatic export is supplied.

## Actual failures and checks

The initial permanent quoted/sink fixture fails **11/32** checks in both ordinary/instrumented builds. The actual tee reproduces stdout/stderr credential-suffix disclosure at its old fragmentation boundary and a missing omission marker; initial stream runs fail **4/13** and **3/13**, respectively (the extra ordinary failure is rotation-dependent loss of the following line). Existing log tests remain green.

After the first repair, added compound/escaped/punctuation cases fail **7/44** checks, and an oversized EOF followed by an explicit tee restart fails **1/17**, in both modes. The final implementation passes quoted/actual sink **44**, actual stdout/stderr/EOF/restart **17** and existing Linux log **42** checks under strict GCC and own-code ASan/UBSan. Unknown fixture modes refuse before file creation. Full cross-platform/native consumer/repetition/privacy evidence belongs in the execution ledger after completion.

The full collection/export/retention/credential-vault/disclosure controls, native write/stop hard bounds, deployment and physical/release qualification remain open. This slice does not complete R-PRIV-001 or the roadmap.

Final Windows default-off full CTest passes **420/420**, with twenty further repetitions of both new cases and the existing log case. Linux ordinary/instrumented final **44/17/42** and twenty additional new-case repetitions pass; twelve native suites and Language/CLI/PTY consumers freshly rebuild/pass. A separately recompiled native fleet/header consumer passes **134/70/12** against unchanged prior core support. All three redaction source hashes stay unchanged. Exact privacy scans and owned-child cleanup are recorded in the execution ledger; arbitrary raw-data disclosure and physical/native latency remain unqualified.
