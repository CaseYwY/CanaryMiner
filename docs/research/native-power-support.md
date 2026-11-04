# Native NVIDIA power support for P-09

**Evidence retrieved:** 2026-11-04. **Scope:** public NVIDIA NVML contracts, including possible GeForce/Blackwell capability; not qualification of a particular device, driver, platform or native adapter. The current NVML reference rendered as **615**, with inspected reference pages dated 2026-09-09. That is a documentation version, not an installed-driver observation.

Context7 was consulted first using its official-NVML documentation collection. NVIDIA pages were then located with Exa and read through Jina; original HTML was also inspected where the reader's Markdown omitted macro details. Only NVIDIA-owned material establishes external API claims below. No GPU/driver API, installed miner, private configuration, device identifier, account or live workload was inspected. This note changes no implementation or admission policy.

## 1. Decision: raw telemetry is possible; full timing support remains unproved

**None of the investigated APIs, on the cited generic contracts alone, qualifies a real `WorkProfileSample` or supplies the fully bounded source-support interval required by `alignWorkPower`.** Successful retrieval, nominal averaging duration, source timestamp, sensor update cadence, cache age and exact measurement endpoints are different facts. Knowing one does not establish the others.[^queries][^fields][^field-struct][^sample-struct][^repo]

Use separate capability states: **not checked**, **API unsupported**, **raw value available / support unknown**, and **independently qualified full support**. This research establishes candidate APIs and documentation gaps, not the last state. Unknown support must remain unavailable for qualification, not become zero power, a zero-width window, a synthesized timestamp or a permissive fallback.

### Supported/refused mapping

“Candidate” means a documented read mechanism, conditional on actual device/driver/API success and validated type/scope. “Refused” below means a product evidence claim is not justified; it does **not** mean the hardware necessarily lacks that API.

| API / selector | Documented useful evidence | Admission from this research alone |
| --- | --- | --- |
| `nvmlDeviceGetPowerUsage` | Unsigned integer milliwatts for GPU and associated circuitry, including memory. **Ampere except GA100 and newer:** one-second average. **GA100 and older:** instantaneous. Fermi-or-newer fully supported devices.[^queries] | Candidate raw display/diagnostic value with architecture-sensitive semantics. **Refuse full work alignment:** no returned source timestamp, exact window endpoints or cache-age bound. Do not label every result instantaneous. |
| `nvmlDeviceGetSamples(..., NVML_TOTAL_POWER_SAMPLES, ...)` | Driver-maintained recent power history; each sample has its original CPU timestamp in microseconds; power values use the unsigned-int union member. Kepler-or-newer fully supported devices.[^queries][^samples-enum][^sample-struct] | Candidate historical raw stream. **Refuse inferred averaging/support intervals or complete energy coverage:** selector does not document an instantaneous-versus-average mapping, sample cadence, retention duration or window endpoints. |
| `nvmlDeviceGetFieldValues` + `NVML_FI_DEV_POWER_AVERAGE` | Explicit one-second GPU-power average on Ampere except GA100 and newer; milliwatts; selected scope; per-field value/status/type and CPU timestamp.[^fields][^field-query][^field-struct] | Preferred explicit average selector for a future raw adapter, where supported. **Refuse automatic `[timestamp - 1 second, timestamp]` certification:** exact relation of CPU timestamp to physical window endpoint and cache latency is not specified. |
| Same call + `NVML_FI_DEV_POWER_INSTANT` | Explicit “Current GPU power, supported on all architectures”; field metadata/status. NVIDIA-SMI describes instantaneous board power as the **last measured** draw.[^fields][^field-struct][^smi] | Candidate explicit current-power channel, not a guaranteed new conversion at call time. **Refuse zero-width physical support, assumed refresh rate, and interval-wide power/energy.** Architecture wording is not a guarantee for every GeForce device/driver/scope. |
| `nvmlDeviceGetTotalEnergyConsumption` | C `unsigned long long` cumulative GPU energy in **millijoules**, since the driver was last reloaded; Volta-or-newer fully supported devices.[^queries] | Candidate cumulative-energy channel. **Refuse conversion to a point power reading or exact work-boundary energy:** no returned timestamp, endpoint-age bound, update cadence or accuracy contract. |
| Field query + `NVML_FI_DEV_TOTAL_ENERGY_CONSUMPTION` | GPU energy in mJ since driver reload, plus ordinary field metadata/status.[^fields][^field-struct] | Candidate timestamped cumulative-energy evidence, **not** a newly documented integration-window endpoint, exact read-boundary cut or lossless counter-epoch proof. |

Power limits, requested limits, throttling flags and utilization are not substitutes for measured power or energy. GPU, module, memory-subsystem and wall-meter bases must not be interchanged.[^fields][^smi]

## 2. Direct power: averaging duration is not update cadence

`nvmlDeviceGetPowerUsage` explicitly distinguishes Ampere except GA100 and newer (average over a one-second interval) from GA100 and older (instantaneous). Its result covers the GPU and associated circuitry, not just the mining kernel or one process. The documentation's **+/-5%** accuracy statement is expressly for **Fermi and Kepler**; it is not a modern-GeForce/Blackwell calibration or uncertainty specification.[^queries]

NVIDIA-SMI provides useful first-party semantic corroboration: average power is entire-board power for the **last second**; instantaneous power is the **last measured** entire-board draw. Thus “instantaneous” does not promise that entering the API initiates a physical measurement, nor that all underlying circuitry is sampled simultaneously.[^smi]

The reviewed contracts do not specify a universal physical sensor-conversion frequency, NVML refresh frequency, maximum cached-value age, averaging-window phase, publication delay or complete filter impulse response. In particular:

* A one-second average is **not** a promise of one update per second, one independent sample per second, or a window ending at API completion.
* Faster application polling does not establish new source observations. Repeated numeric values can be legitimate; changed numeric values do not reveal the full averaging support.
* Host call-begin/call-end timestamps bound the API operation, not necessarily the underlying cached measurement.
* Do not borrow the documented **one second to one-sixth second utilization period** as a power cadence; NVIDIA documents that period for utilization, not these power channels.[^smi]

These are limits of the cited contracts, not measured assertions that a specific implementation always caches, filters or runs at any particular rate.

## 3. Historical power samples: preserve originals, do not invent windows

`nvmlDeviceGetSamples` retrieves samples already maintained in a driver buffer. It is not a request to integrate power over an arbitrary caller-selected interval.[^queries]

Documented mechanics:

1. Call with `samples == NULL` to obtain the number of sample entries to allocate; returned `sampleCount` on retrieval is the number actually returned.
2. `lastSeenTimeStamp == 0` requests all entries **currently maintained by the underlying buffer**, not all observations since boot or driver load.
3. A nonzero cursor returns only samples whose timestamps are **strictly greater**. NVIDIA recommends using a timestamp returned by an earlier query.
4. Power, utilization and clock sample payloads are documented as unsigned-int values in `nvmlValue_t`; preserve and validate `sampleValType`, rather than interpreting an arbitrary union member.
5. NVIDIA describes higher-frequency data at lower polling cost, but gives no numeric power-sample rate in this function's contract.[^queries]

`NVML_TOTAL_POWER_SAMPLES` is defined as total power drawn by the GPU. “TOTAL” identifies the power quantity; it does **not** turn the values into cumulative joules. The enum definition and query contract inspected do not explicitly bind this selector to the one-second average or the instantaneous field on each architecture. The sample-selector entries also do not independently state a power-unit conversion; a future adapter must establish and pin its unit/type contract rather than deriving one from the enum's name.[^samples-enum][^queries]

`nvmlSample_t::timeStamp` is documented as a **CPU timestamp in microseconds**. Unlike `nvmlFieldValue_t::timestamp`, this struct entry and the query paragraph do **not explicitly name the epoch**. Do not assert that it is a GPU device-cycle clock, a monotonic clock, or a documented Unix-epoch clock merely because its values appear plausible. Preserve the raw timestamp and clock-domain uncertainty until its exact platform contract is established.[^sample-struct][^queries]

No retention seconds/entry bound, cache-loss notification, fixed ordering guarantee, power averaging/filter window, or duplicate-timestamp disambiguator is established by the reviewed sample contract. Consequences for a future bounded collector:

* Treat initial history as historical, not as newly observed work-source evidence. Never restamp it at receipt.
* A caller sequence is delivery bookkeeping, not proof that every driver sample was retrieved.
* The size query and data query are separate calls; do not assume they freeze an immutable history or permit unbounded allocation/retry. Respect an application cap and returned counts/status.
* A strict-greater timestamp cursor cannot by itself prove absence of skipped equal-timestamp entries or buffer eviction. Lack of `NOT_FOUND` does not prove lossless capture.
* Adjacent source timestamps delimit returned observations, **not necessarily the support of their measurements**. Do not fill gaps, interpolate a work count, assign a previous-sample hold, or integrate a trapezoid and label the result exact source energy.

The last points are engineering consequences of missing guarantees, not claims that NVIDIA promises those failure modes occur.

## 4. Field values: better metadata, still no full support contract

`nvmlDeviceGetFieldValues` can query several fields together. If fields share an underlying driver call, NVML populates them from a single such call. The documentation does **not** promise an atomic, physically simultaneous snapshot across every requested field.[^field-query]

For **each** returned `nvmlFieldValue_t`, retain:

* `fieldId`, requested `scopeId`, and `valueType`;
* `nvmlReturn`, which must be successful before reading `value`;
* `timestamp`: CPU time in **microseconds since 1970**;
* `latencyUsec`: time the field took to update **within NVML**, potentially averaged across fields serviced by the same driver call.[^field-struct]

The outer call returns `NVML_SUCCESS` when **any** fields were populated. It does not mean every requested power/energy field succeeded. A field's `value` is explicitly undefined if its individual `nvmlReturn` is not `NVML_SUCCESS`.[^field-query][^field-struct]

**`latencyUsec` is not a sensor averaging duration, source age or integration-window length.** Subtracting it from `timestamp` cannot construct measurement support. Likewise the field timestamp is not documented as an exact physical integration endpoint, a monotonic timestamp, a freshness guarantee or a calibrated maximum timing error.[^field-struct]

The average-power field block requires a deliberate `scopeId`: **0 GPU**, **1 module** (for example supported NVIDIA CPU plus GPU), **2 GPU base**. It says GPU-base scope supports only `_MIN_LIMIT`, `_MAX_LIMIT`, `_DEFAULT_LIMIT` and `_CURRENT_LIMIT`, not the average/instant usage fields. The CPU scope is NVIDIA CPU such as Grace, not arbitrary x86/non-NVIDIA ARM host power. Retain scope provenance and never present a module measurement as GPU-only or wall power.[^fields]

The average block explicitly states mW and the one-second architecture rule; the instant macro entry itself is much shorter (“Current GPU power, supported on all architectures”). A production adapter still needs a pinned header/API unit-and-value-type interpretation for each selected field; it must not use the storage type or a neighboring field's label as metrological calibration.[^fields][^field-struct]

## 5. Cumulative energy and read-begin/read-end uncertainty

`nvmlDeviceGetTotalEnergyConsumption` and its corresponding field describe cumulative GPU energy in **mJ since the driver was last reloaded**. The standalone API is documented for Volta-or-newer fully supported devices. The standalone call returns no source timestamp.[^queries][^fields]

### What a difference can and cannot establish

Two valid readings in the **same proven device/source/counter epoch** can support the reported counter difference. Exact integer subtraction preserves reported units; it does not establish exact physical energy or exact correspondence to work performed between two host boundary events. The reviewed entries provide no numeric energy accuracy, effective counter quantum, update period, maximum lag, integration algorithm, or relation between counter update and API read completion.[^queries][^fields]

Recording original dual-clock `readBegin` and `readEnd` is necessary evidence of the host read bracket. It does not solve missing source timing:

* **Conditional illustration, not an NVML guarantee:** if a separately qualified source guaranteed a fresh counter cut somewhere inside each read bracket, then its two true cut times would still be uncertain within those brackets. Their difference would not necessarily cover exactly the requested work interval.
* These NVML entries do **not** supply that fresh-cut guarantee. Unknown cached-counter age can put a counter's physical endpoint before call-begin. A short API call therefore cannot by itself bound all endpoint error.
* A field timestamp and `latencyUsec` add metadata but do not document when the counter's physical integration ended.
* Dividing a counter difference by host elapsed time produces an estimated average only under additional timing assumptions. It must not be relabeled an original point-power value or passed through `WorkPowerReading::milliwatts` as if it were one.

Even a fully timed GPU energy delta includes other GPU activity, display/game use and relevant circuitry within its basis. Work-count equality or a drained mining lane does not allocate that board energy to the miner. Idle-only observations cannot stand in for energy consumed during running work.[^queries][^smi][^repo]

### Precision and epochs

* **mW, mJ and microseconds are representation units**, not claims of that measurement accuracy, true resolution or temporal precision. Do not generalize Fermi/Kepler's +/-5% power claim to newer power or energy counters.[^queries][^field-struct][^sample-struct]
* “Since driver reload” is not a promise of persistence across reboot, driver reload or device replacement, nor a definition of “since this process/NVML session started.” Treat reboot/reload and uncertain source continuity as a new counter epoch. No reload/reset is proposed or performed here.
* The entries do not establish all GPU-reset, suspend/resume, wraparound or discontinuity semantics. Refuse a decreasing counter or uncertain epoch; never repair it by unsigned wrap arithmetic or by stitching the new epoch onto the old one.
* Nondecreasing numbers alone do not prove continuity: a reset followed by new consumption can overtake a previous number. Independent source/epoch binding is still needed. A zero delta is the reported counter result, not proof of zero physical energy during an arbitrarily short interval.

## 6. Unsupported and error behavior

For both standalone power and standalone energy, the documented outcomes include `NVML_SUCCESS`, `NVML_ERROR_UNINITIALIZED`, `NVML_ERROR_INVALID_ARGUMENT`, `NVML_ERROR_NOT_SUPPORTED`, `NVML_ERROR_GPU_IS_LOST` and `NVML_ERROR_UNKNOWN`. `nvmlDeviceGetSamples` lists these and additionally `NVML_ERROR_NOT_FOUND` when sample entries are not found.[^queries]

Preserve the distinction:

| Outcome | Conservative meaning |
| --- | --- |
| Valid successful numeric read, support unknown | Raw telemetry only; not qualified coverage. A measured numeric zero is different from missing/error. |
| `NOT_SUPPORTED` | This attempted API/type/scope is unsupported; not evidence that another API has succeeded or that every telemetry channel is unavailable. |
| `NOT_FOUND` for samples | No matching entries were found; not zero power/energy and not proof that the query itself is permanently unsupported. |
| Outer field-query success, individual field failure | Failed field is unavailable; do not inspect its undefined payload. |
| Uninitialized, invalid arguments, lost device, unknown or unexpected failure | No new valid observation. Do not silently reuse an old value as fresh or change its timestamp. Invalidate continuity/authority as appropriate; classify unknown causes conservatively. |

The power documentation still refers to power-management-mode support. **Do not make the deprecated `nvmlDeviceGetPowerManagementMode` call the sole capability gate:** NVIDIA marks it deprecated from CUDA 13.0 and scheduled for removal in CUDA 14.0. The current deprecation policy says removed functions return `NVML_ERROR_DEPRECATED`; unavailable/removed symbols and any unexpected status remain unavailable, not success.[^queries][^deprecations]

## 7. GeForce / Blackwell is a capability question, not a machine claim

NVIDIA explicitly describes NVML support for newer GeForce/RTX non-data-center products as **limited**. NVIDIA also identifies GeForce RTX 50 Series as Blackwell. These facts justify investigating a future Blackwell-capable adapter; they do not prove that every board/driver/OS exposes average power, instantaneous fields, history or energy counters.[^overview][^blackwell]

The architecture rules (“Ampere except GA100 or newer,” “all architectures,” and “Volta or newer fully supported devices”) are conditional API documentation, not a per-model support matrix. No device capability was probed here. A documented API, an exported symbol, successful NVML initialization, a known GPU architecture and success of a different telemetry channel must not independently qualify the requested channel or its full physical support.[^queries][^fields][^overview]

## 8. Fit with the existing work-alignment contract

The checked-in contract retains the **original** observation, both host read endpoints and optional **entire** source support, including averaging/cache support. It requires read bracket, support and observation to lie **strictly inside** one original constant-counter interval on both UTC and monotonic clocks; missing power/support refuses. Historical alignment is not profile admission, sampler liveness, a command permit or device qualification.[^repo]

Accordingly, a future native adapter should retain raw results and explicit unavailable reasons without constructing `support` from a guessed one-second offset, call bracket, inter-sample spacing or `latencyUsec`. A UTC-like source timestamp cannot acquire an original monotonic partner by copying receipt time. An independently qualified cross-clock mapping must preserve uncertainty and reject clock discontinuity; these generic NVML entries do not provide that mapping.

Before promoting native telemetry beyond raw diagnostics, evidence is still required for **each exact API/type/scope/platform combination**:

1. Device/source/incarnation and counter-epoch binding, with privacy-safe public fixtures.
2. Supported symbol/version, per-field success, value type, units and physical power/energy basis.
3. Full physical measurement support or a justified conservative bound, including filter/average duration, publication/cache age and precise timestamp semantics.
4. Original dual-clock observations and uncertainty handling; no retiming of cached history or completed work.
5. Bounded collection, explicit gaps/eviction/discontinuities, source freshness/liveness and shutdown behavior.
6. For running-energy profiles, independently timed coverage over the running interval, valid counter endpoints or a justified integration/error model, and attribution limitations. No interpolation or idle-to-running substitution may manufacture that evidence.

**Remaining blocker:** the cited public API contracts do not close items 3–6 merely by returning successful values. Keeping a native report explicitly unproved is a useful, truthful result. Do not enable native profile qualification or weaken the existing synthetic-only admission because this research located a telemetry API.[^repo]

## Retrieval limitations

The old NVIDIA field-enum URL `https://docs.nvidia.com/deploy/nvml-api/group__nvmlFieldValueEnums.html` returned **404** during full retrieval, despite still appearing in search results. It was not silently replaced by a secondary explanation. The current `/latest/api/` page was accessible; its initial Jina Markdown omitted detailed macro bodies, so the original HTML returned through the same public reader route was inspected to verify the averaging and scope text. The `/615/api/` route was also accessible. Context7 and search snippets were discovery aids, not sole evidence for those claims.

No first-party numeric cadence/cache-retention guarantee, modern accuracy certification, complete source-support endpoints or GeForce/Blackwell per-channel support matrix was established by the reviewed pages. This is a bounded research conclusion, **not** a claim that no other NVIDIA document or product-specific contract can ever supply one. No secondary source, authenticated page, forum anecdote or live-device result was used to fill these gaps.

## Sources

All external sources below are NVIDIA-owned and were retrieved on **2026-11-04**. Live `/latest/` URLs can change; the inspected rendering identified reference version 615.

[^queries]: NVIDIA, **Device Queries**, entries `nvmlDeviceGetPowerUsage`, `nvmlDeviceGetSamples`, `nvmlDeviceGetTotalEnergyConsumption` and their parameter/return contracts: <https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html>.
[^fields]: NVIDIA, **Field Value Enums**, detailed average/instant/energy macro entries: <https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlFieldValueEnums.html#c.NVML_FI_DEV_POWER_AVERAGE>, <https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlFieldValueEnums.html#c.NVML_FI_DEV_POWER_INSTANT>, <https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlFieldValueEnums.html#c.NVML_FI_DEV_TOTAL_ENERGY_CONSUMPTION>. Versioned page also retrieved: <https://docs.nvidia.com/deploy/nvml-api/615/api/group__nvmlFieldValueEnums.html>.
[^field-query]: NVIDIA, **Field Value Queries**, `nvmlDeviceGetFieldValues`, shared-driver-call behavior and individual field status requirement: <https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlFieldValueQueries.html>.
[^field-struct]: NVIDIA, **nvmlFieldValue_t**, timestamp epoch, latency meaning, scope/type and undefined-on-error payload: <https://docs.nvidia.com/deploy/nvml-api/latest/api/structnvmlFieldValue__t.html>.
[^sample-struct]: NVIDIA, **nvmlSample_t**, CPU timestamp in microseconds and sample payload: <https://docs.nvidia.com/deploy/nvml-api/latest/api/structnvmlSample__t.html>.
[^samples-enum]: NVIDIA, **Device Structs**, `nvmlSamplingType_t`, including `NVML_TOTAL_POWER_SAMPLES` and distinct module-power selector: <https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceStructs.html#_CPPv418nvmlSamplingType_t>.
[^smi]: NVIDIA, **NVIDIA System Management Interface**, “GPU Power Readings,” “Module Power Readings,” “GPU Memory Power Readings,” “Utilization” and polling options: <https://docs.nvidia.com/deploy/nvidia-smi/index.html>. Descriptive documentation only; the utility was not run.
[^overview]: NVIDIA, **NVML API Reference**, limited non-data-center GeForce/RTX support: <https://docs.nvidia.com/deploy/nvml-api/latest/nvml-api-reference.html>.
[^blackwell]: NVIDIA, **GeForce RTX 50 Series Graphics Cards**, Blackwell architecture identification only, not NVML capability evidence: <https://www.nvidia.com/en-us/geforce/graphics-cards/50-series/>.
[^deprecations]: NVIDIA, **Deprecation and/or Removal Notices for the NVML Library**, CUDA 13.0 list and current removal/error policy: <https://docs.nvidia.com/deploy/nvml-api/latest/deprecations.html>.
[^repo]: Repository evidence: [work/power alignment documentation](../work-power-alignment.md), [alignment types](../../src/product/work_power_alignment.h) and [alignment implementation](../../src/product/work_power_alignment.cpp), especially missing-support refusal, dual-clock ordering, strict containment and original preservation. These links identify checked-in source contracts, not physical sensor evidence.
