# Explicit civil-clock timestamp presentation (P-13k)

M-I18N/M-UI; R-I18N-001/002, R-TAR-001/003; T-I18N/T-UI/T-TARIFF/T-PRIVACY; D-109.

**Independent display adapter, not ordinary native activation, a billing clock or a source timestamp.**
`canary_terminal_time` formats an explicitly supplied UTC-millisecond instant under an explicitly supplied `CivilClock`.
Missing is neither epoch nor “now.” The ordinary miner/settings/supervisor does not call or link the adapter, change its
existing timestamp output, or activate saved language requests. No calendar reset, interval arithmetic, forecast, tariff,
stored clock, protocol timestamp, source freshness or donation behavior is altered by display formatting.

## One authoritative civil conversion, independent display policies

`TerminalTimeRequest` supplies an optional required UTC instant, independent civil clock and formatting locale, Latin or
locale-default digits, DateTime/Date/Time fields, Numeric/AbbreviatedMonth date style, explicit 24/12-hour cycle, optional
millisecond display and native tone. There is **no message-language input** from which to infer a zone, calendar, currency or
jurisdiction. Defaults explicitly select UTC, Gregorian display and 24-hour time, but never supply a missing instant.
The same 31 formatting IDs as number presentation are supported; unsupported safe tags report independent `localeFallback`,
while malformed tags, invalid enums and invalid/missing instants refuse transactionally.

The existing [civil core](electricity-core.md#time-adr) performs the **only** UTC-to-wall conversion. UTC and declared fixed
standard offsets work without data packages; named IANA clocks require that core's independently configured app-private data
and exact version. Named data unavailable/invalid does not fall back to UTC, the system zone or ICU's bundled timezone rules,
even for ASCII recovery. The adapter never calls `configureCivilTimeData`, loads a caller's package itself or infers a zone.
The existing supported range is UTC/local years 1970–9999 and offset magnitude at most 86400 seconds; an offset that moves a
boundary instant outside that range refuses. Instants are POSIX UTC milliseconds, not a leap-second model.

The result retains the original UTC instant, full clock, `CivilStamp`/source database version and exact offset. Wall fields are
then formatted in ICU's **explicit fixed GMT** formatter with `calendar=gregorian`, using a maintained pattern generator for
the requested fields/date style/hour cycle. ICU calendar type is checked before formatting. Formatting the already-resolved
wall-millisecond representation in GMT prevents a second conversion or disagreement with the authoritative civil TZDB.
This representation is below 2^48, so each integer millisecond is exact in ICU's `UDate` double; no timestamp rounding or
double-derived source arithmetic is introduced. Suppressing millisecond display omits those digits without changing the
retained source instant, and 12-hour formatting does not change any wall field. Pattern choices and human month names come
from maintained formatting data, not own-code language rules.

References: [ICU date/time pattern generation](https://unicode-org.github.io/icu/userguide/format_parse/datetime/),
[C date formatter](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/udat_8h.html) and
[C pattern generator](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/udatpg_8h.html). The supplied ICU 79.1 headers
specify `UDAT_PATTERN` for both styles when using an explicit generated pattern; the timezone argument is never null.

## Visible zone and honest fallback

The ready-to-layout segment includes the **entire wall display plus an explicit bracketed ASCII zone/offset label**.
UTC is `UTC`; a nonzero offset is `UTC+HH:MM:SS` or its minus form, preserving second-level fixed offsets. Named clocks include
their exact IANA ID and the offset at that instant. Two occurrences of the same folded wall time therefore have different
complete segment labels. Raw `text`/`ascii` wall strings are separate presentation components, not unambiguous machine
timestamp encodings; consumers must retain the clock/zone metadata or use the complete segment.

Explicit `asciiOnly` skips native ICU formatting calls and records `asciiRecovery=true`, not backend failure. A disabled
Unicode backend records `backendFallback=true`. Both use unambiguous fixed Gregorian `YYYY-MM-DD` and/or 24-hour time,
optionally milliseconds, **not a claim that requested month names, local digits or 12-hour grammar took effect**. Actual
formatting-locale fallback and internal resource inheritance are separate metadata. ICU-generated bidi controls are removed
before layout; callers cannot inject controls through formatting tags. ICU library/data loading and initialization are still
external dependency-owner/deployment gates, not made hermetic by a bounded own-code path.

The segment is atomic-LTR **human presentation**, not invariant machine ASCII. Full dates/times and their zone stay together
under clipping and autonomous wrapping; impossible width refuses all rows instead of emitting a different timestamp or losing
its zone. Literal/argument tones and provenance survive typed message composition. Logical versus visual bidi remains an
explicit terminal capability choice; visual text containing month names may reorder and must never be parsed back into time.
Generated direction controls stay unstyled, painting follows layout and all existing input/output bounds apply. The caller's
whole previous `TerminalTime` is preserved on any failure.

## Validation scope and remaining work

Fixtures cross all 31 formats, both digit styles, all three field sets, both date styles/hour cycles/millisecond choices,
exact wall fields/source metadata, ASCII recovery and unchanged process locale. UTC/second-level/±24-hour offsets, epoch/upper
range, leap/century/year rollovers, missing/invalid instants/policies and named-data-unavailable preservation are explicit.
Native exact English, 12-hour period, Arabic-Egyptian digits and upper-bound millisecond tests check real formatted output,
not only retained metadata. Atomic clipping/wrapping and named argument provenance cross order/direction policies.
Separately enabled offline TZDB fixtures cover New York's repeated hour with different offset labels, Apia's skipped date and
the reviewed 2026e Winnipeg rule without delegating that conversion to ICU. Exact platform/sanitizer/privacy evidence belongs
to the [execution ledger](architecture/execution-plan.md). An initial strict compile caught an unused test helper before its
layout scenarios were added; that failed compilation is not counted as validation.

This supplies timestamp presentation only. Duration/relative-time domains, other UI extraction, actual native binding,
explicit ICU/civil data-owner deployment, human review and real terminal/font/shaping/accessibility remain unfinished.
Independent external ICU/date libraries and data are not sanitizer-instrumented by own-code tests. No real-source/financial,
physical GPU/game/pool/terminal or production qualification is inferred.
