# Recurring periods for stored energy

Run the following command in an [energy-enabled candidate](pool-tls-candidate.md#version-4-energy-runtime-layout) or a development build with `CANARY_ENABLE_ENERGY_RUNTIME=ON`. Use the state directory spelling from enrollment and a separate reset policy file. Resolve a relative state directory from the same working directory used at enrollment.

```text
canary-miner energy-periods --state-dir DIR --reset-policy RULE.json --cycles 2
```

```json
{
  "monthly_day": 31,
  "clock": {"kind": "utc", "fixed_offset_seconds": 0}
}
```

Choose an explicit UTC, fixed standard offset or reviewed named IANA clock. The command accepts the same
monthly, two-month and annual reset rules as the tariff declaration. Use `monthly_day`, `bimonthly_start_month`
(1 for odd or 2 for even months) with `bimonthly_day`, or `annual_month` with `annual_day`, plus the clock.
[Reset policy examples](tariff-declaration.md#derive-a-monthly-whole-meter-cycle)

Named clocks require the verified IANA data beside the miner. A reset day beyond the end of a month moves to that month's last day. Periods use half-open UTC ranges, including any daylight-saving change. At the exact reset instant the new period has no elapsed time. Use a different reset policy file to project the same history on an independent clock.

For an optional energy-only cost estimate, pass up to 24 saved `electricity-preferences.json` declarations
using a separate `--flat-rate FILE` for each file. The files must contain an enabled, reviewed flat board rate
in the same currency, with nonoverlapping effective UTC windows. The order of the files does not matter.
The reset policy's clock governs reporting periods. Each declaration's effective UTC bounds govern its price.
The report omits prices confirmed after the report cutoff. It uses previously saved copies to cover earlier versions.

```text
canary-miner energy-periods --state-dir DIR --reset-policy RULE.json --cycles 2 --flat-rate prior.json --flat-rate current.json
```

The worker audits one history snapshot. It reports energy on the stored model's UTC timeline and
leaves two seconds inside each price boundary unpriced because the saved measurements admit up to
two seconds of clock disagreement. Rate windows of four seconds or less have no priced span.
Each segment contributes exact observed watt-milliseconds times the signed unit price, with division by
3,600,000,000 after summing. The report shows priced GPU-time separately from observed GPU-time and displays
only the priced subtotal. Missing measurements and prices leave gaps. An explicit zero price or a measured zero
can produce a priced zero. The optional report estimates board energy cost and leaves utility accounts, tariff
fees, FX and economic controls outside the calculation.

The report includes one to twelve periods through now, or through the instant supplied with `--through-utc-ms`. It identifies the latest committed batch and shows covered versus requested GPU-milliseconds. An unavailable period has no energy value. A partial period shows the subtotal for covered time. A complete elapsed interval can show a measured zero. Each numeric energy result includes its exact watt-millisecond numerator. The cutoff clips coverage in the currently committed history, so it does not reconstruct what was already visible at that past time.

The command reads the existing enrollment with the authorization worker. Run the report while persistent energy collection is idle. The installed collector worker reserves the shared activity lock for the duration of the report. Collection holds the same lock for its lifetime. A live collector from the same build keeps its authority and the report exits before touching the journal. If a just-stopped collector still has an admitted write in flight, the report also exits before reading the journal. Stop older builds that use the same history before reporting. The report samples its audit clock after reserving the activity lock, then holds the journal lease before the ledger lease and audits a single existing read-only SQLite snapshot. A prepared journal blocks the report even if the batch reached the ledger. Missing or divergent originals also block it. The command creates no enrollment, observation, reset record or economic decision. Existing lock and SQLite sidecar housekeeping can occur in the selected directory. Output omits private paths, device keys and session identifiers.

The measurements describe the selected GPU boards. Whole-meter input, monetary settlement, automatic billing-ledger transitions and physical source qualification remain open.
