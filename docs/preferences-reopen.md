# Preference save authority after a known reopen/conflict failure (P-14o)

M-CONFIG/M-PRIVACY; R-UI-004/007, R-PRIV-001/002; T-CONFIG/T-PRIVACY; D-172.

The native stores require a successfully loaded original before Save. A known failed reopen or failed current-original check must invalidate that authority. Another process restoring identical old bytes is not the successful reopen requested by the error message.

An actual synthetic regression reproduced **32 failures / 426 checks** in both Linux ordinary and own-code ASan+UBSan builds: four older stores kept their previous loaded flag after failed reopen, and all eight tested store types kept it after a detected external conflict/unreadable current target. Returning the previous bytes could make the stale instance save again without the required successful reopen. This is an observable return/admission bug, not a claimed physical-disk failure or general solution to every undetected ABA edit.

The bounded fix clears loaded authority before every reopen attempt and on failed current-original reads or mismatch. It preserves the caller's previous output on rejection, leaves stored originals unchanged, and admits Save again only after a successful explicit load. Null output requests also fail and revoke rather than dereferencing a pointer or retaining a prior writable observation. Structural draft-validation errors and ordinary lock contention remain separate; the existing lock/CAS and valid explicit-save behavior are retained.

Scope: UI presentation, runtime defaults, manual references, flat electricity, game defaults, pool-security declarations, language requests and combined-tariff declarations. No schema, setting/default, consent phrase, migration/backup, tariff/version identity, mining activation, process/driver/hardware action, deletion or automatic retry is added. The shared preference path is still synchronous; this repair is **not** latency isolation, a native hard bound, physical durability or broad runtime configuration qualification.

Only owned synthetic scratch files and the repository's public synthetic tariff example are used. Final post-fix/unit/native/CLI/PTY/cross-platform evidence will be recorded after execution, not inferred from the failing case.

Final **538** checks pass Windows/MSVC and Linux ordinary/own-code ASan+UBSan, with twenty additional repetitions in each applicable build. Windows default-off full CTest passes **427/427**. Twelve native CPU consumers/seven own units, fresh Language **6157**, actual inert diagnostics/language/pool/game/throttle CLI and eight native PTY regressions freshly rebuild/pass on Linux; discovery passes **166 / nineteen explicit skips**, with Windows portable **147 / twenty-three skips** (nineteen GCC-only cases excluded). Exact staged/source/history and scoped owned-child cleanup are recorded in the execution ledger. No real configuration, GPU/game, provider or installed miner is exercised.
