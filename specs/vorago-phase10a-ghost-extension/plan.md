# Implementation Plan: Vorago Phase 10a — AtmosphereEngine Ghost Extension

**Spec:** `specs/vorago-phase10a-ghost-extension/spec.md`
**Base commit:** `374580d7d0f0631561413310bd3085e15ba7279c` ("feat(vorago): Phase 10 Voice and Engine")
**Layer:** Layer 3 append-only extension of `dsp/include/krate/dsp/systems/atmosphere_engine.h`
(`class AtmosphereEngine`, `:179`) + two inert fields on `VoragoEngineConfig`
(`dsp/include/krate/dsp/systems/vorago_engine.h:105`). **No new production class, no new production
header, no new include on either header.**
**Test targets:** `dsp_systems_tests` (all new cases), plus `seraphis_tests` / `Seraphis` /
`pluginval` as the untouched-consumer gate (SC-006 clause 5).

Every file:line in this plan was opened and read during the planning session. Where the spec's own
citation disagreed with the file, the file wins and the divergence is recorded in
**§0 Plan-stage findings**.

---

## 0. Plan-stage findings

Six items that the spec states in a form the code cannot satisfy as written. None of them relaxes a
threshold: **P-1 adds a requirement, P-2 lengthens a fixture pre-roll so an existing assertion becomes
reachable, P-5 adds a sizing requirement the shipped audio thread needs before a second birth site
exists, P-6 fixes a self-contradictory threading contract, and P-3 / P-4 are citation/shape
corrections.** P-1, P-2, P-5 and P-6 needed a ruling before the build stage, because they change
spec text; all were ruled on 2026-09-23, each taking the recommended option (spec Clarifications
R-1, R-2, R-4 and R-6).

### P-1 (blocking, new requirement) — a reverse grain can outrun the ring **while the ring is still filling**, and the shipped FR-014 admission test cannot see it

The shipped admission test is capacity-based (`atmosphere_engine.h:1736-1742`):

```cpp
const double oldestAge = std::min(birthAge + decorr, capacity - 2.0 - guard);
const double needed = std::ceil(oldestAge) + guard;
if (static_cast<double>(capture_.getAvailableSamples()) < needed) { ++skipRingCold_; return; }
```

It tests the ring **at the birth sample only**. That is sufficient forward, and it is **not**
sufficient backwards:

| direction | age rate | `available` rate (unsaturated) | age vs available |
|---|---|---|---|
| forward, `r > 1` | `-(r-1)` | `+1` | age shrinks — always safe |
| forward, `r < 1` | `+(1-r) < 1` | `+1` | available grows faster — safe |
| **reverse** | **`+(1+r) > 1`** | `+1` | **age gains on available at `r` per sample** |

With `A = getAvailableSamples()` at birth, `C = captureCapacity_`, `L'` the truncated lifetime and
`r̄ = ratioMax`, the read age at grain-time `t` is `a(t) = birthAge + (1+r(t))·t ≤ birthAge + (1+r̄)·t`,
while `available(t) = min(A + t, C)`.

- **Saturated regime** (`available == C`): the birth window already guarantees safety.
  `ageHi = C - 2 - guard - ceil(wDown·L') - decorr` (`:1704-1705`) with the reverse `wDown = 1 + r̄`
  (FR-014) gives `a(L') + decorr ≤ C - 2 - guard`, and `LinearReader::maxAge_ = available - 2 = C - 2`
  (`rolling_capture_buffer.h:348`). Safe, nothing to add.
- **Filling regime**: `a(t) + decorr + guard - available(t)` grows at `r̄` per sample. Worked example at
  the Vorago ghost operating point (48 kHz, `captureSeconds = 20` → `C = 1 048 576`,
  `grainSeconds = 12` → `L' = 576 000`, `pitchSemitones = -12` with `driftRange 2` → `r̄ = 2^(-10/12) =
  0.5612`): a grain born after a 5 s pre-roll (`A = 240 000`) at the minimum birth age 64 has
  `a(t) = 64 + 1.5612·t` against `available(t) = 240 000 + t`; the two cross at `t ≈ 427 000` samples,
  i.e. 8.9 s into a 12 s grain. `LinearReader::index0` then **clamps** the age at `maxAge_`
  (`rolling_capture_buffer.h:313-321`) and the grain reads **one held sample** for its remaining ~3 s —
  a DC pedestal under the grain envelope. Not a crash, not a NaN, and **invisible to every criterion
  the spec currently writes**: `getMin/MaxObservedGrainAgeSamples()` fold the *computed* age
  (`:1800-1809`, `:1860-1869`), which stays inside `[64, C-2]`, so SC-003 (c) passes while the read is
  stale.

**Resolution — one additive admission clause, reverse-only, immediately after the shipped FR-014 test
(a pure insertion: it deletes nothing, so FR-045's deletion budget — six sites since ruling R-4, P-5's
sizing anchor (v) and the falsified-comment anchor (vi) included — is unaffected by *this* clause):**

```cpp
// (e2) FR-050 (Phase 10a): a REVERSE grain's read age grows at 1 + r per sample
//      while the ring fills at 1, so the shipped birth-sample test is only a
//      statement about t = 0. The deficit accumulates at ratioMax until the ring
//      saturates, after which step (c)'s window (wDown = 1 + ratioMax) already
//      bounds the whole life. So the ONE extra quantity is the deficit up to
//      saturation:  t* = min(lifetime, capacity - available).
//      VACUOUS ON A FULL RING (t* == 0) and never evaluated for a forward grain,
//      which is what keeps every shipped admission decision bit-identical.
if (reversed) {
    const double avail = static_cast<double>(capture_.getAvailableSamples());
    const double fillDeficit =
        std::ceil(static_cast<double>(ratioMax) * std::min(lifetime, capacity - avail));
    if (avail < needed + fillDeficit) { ++skipRingCold_; return; }
}
```

This is **FR-050** below (new). It is, with P-5's FR-051, one of two places in this phase where the
plan adds a requirement the spec does not state.

**FR-050 needs its own criterion, and the review found it had none.** Every reverse fixture in §3
pre-rolls to a full ring precisely so the shipped admission arithmetic is what is under test — which
means FR-050 is evaluated with `t* = 0` in all of them and a wrong-signed, wrong-termed or entirely
absent clause would pass every one. SC-003 (c) in particular is **not** the backstop it reads as: it
folds the *computed* age (`:1099-1103` via the fold sites at `:1800-1809`, `:1860-1869`), which P-1
itself shows stays inside `[64, C-2]` while the read is stale. This plan therefore adds **SC-011**
(§3.2, `AtmosphereGhost_ReverseFillDeficit`), the one case that renders the filling-ring regime
directly: arm **(a)** proves the clause rejects inside the regime where admission is impossible, and
arm **(a2)** proves the first admission arrives no earlier than the threshold the test recomputes.

**What no criterion can do, and why the reviewed draft's attempt was deleted.** The draft added an
arm asserting the *read* age against `available - 2` on a fixture that first REQUIREd a birth. That
cannot fail: FR-050 admits iff `A >= needed + ceil(rMax*t*)`, which is exactly the negation of the
crossing condition `A < needed_exact + rMax*t*`, so **admitted implies never stale** is a theorem and
any green-path fixture can only observe grains that provably satisfy the bound (§3.2 SC-011 (b),
§A-5). The protected quantity is therefore measured **with the clause compiled out**, in §6 step 4's
differential protocol. R-1's mitigation claim is restated against SC-011 (a)/(a2) and that
clause-disabled run, not against SC-003 (c).

### P-2 (blocking, consequence of P-1) — SC-003's 5 s pre-roll and its `coldStartSkips <= 3` bound are not reachable

Under FR-050 a reverse grain is admissible only once `A >= needed + ceil(rMax * t*)` with
`t* = min(L', C - A)`. **The `min` is load-bearing and the reviewed draft's formula dropped it**,
writing the saturation branch `A >= (birthAge + decorr + guard + rMax*C) / (1 + rMax)` as if `t*` were
always `C - A`. At the Vorago ghost point `L' = 576 000` and `C = 1 048 576`, so `C - A > L'` for every
`A < C - L' = 472 576` and the binding branch is `t* = L'`, which makes the deficit a **constant**
`ceil(0.561231 * 576 000) = 323 270`:

| birth age | `needed` | threshold `A = needed + 323 270` | at 48 kHz |
|---|---|---|---|
| minimum, `kMinAgeSamples = 64` (`:251`), `decorr -> 0` | `ceil(64) + 64 = 128` | **323 398** | **6.74 s** |
| worst legal, `ageHi + decorr = C - 2 - guard - ceil(wDown*L')` | `= 149 240 + 64 = 149 304` | **472 574** | **9.85 s** |

The second row lands within two samples of the branch crossover `C - L' = 472 576`, and that is
structural rather than luck: `ageHi` is *defined* so the saturated bound is tight, so the two branches
meet there. That near-coincidence is why the reviewed draft's saturation-branch figure of `472 073` =
9.83 s came out almost right at the worst birth age and badly wrong at the minimum one, where the same
formula gives 377 013 samples = 7.85 s against the true **323 398 = 6.74 s**. The reviewed draft's
**6.77 s** was approximately the right number reached without the formula that produces it; both the
formula and the figure are corrected here, and **every threshold in this phase is recomputed by the
test** from `getCaptureCapacitySamples()`, `kMinAgeSamples`, the configured `grainSeconds` and the
configured `rMax`, never transcribed from this table.

A 5 s pre-roll (`A = 240 000`) is below both thresholds, so it rejects *every* reverse birth and
`skipRingCold_` keeps climbing into the measured span - the delta assertion fails on correct code.

**Resolution (the assertion is unchanged; only the fixture is):** every reverse fixture pre-rolls
**until the ring is full**, and the plan states that as a *rendered sample count read from the engine*,
never as a hand-computed seconds figure:

> **Full-ring pre-roll, defined once.** From a fresh `prepare()`/`reset()`, render exactly
> `engine.getCaptureCapacitySamples()` samples (`atmosphere_engine.h:1133-1137`). The ring's own
> `getAvailableSamples()` is `std::min(samplesWritten_, capacity_)`
> (`rolling_capture_buffer.h:443-445`), so after that many rendered samples it is saturated at
> `capacity_` and stays there. `AtmosphereEngine` exposes **no** `getAvailableSamples()` of its own
> (grep: the name appears at `:247`, `:255`, `:950`, `:1663`, `:1738`, `:1995`, all internal uses of
> `capture_.`), so a test that needs the *available* count computes it as
> `std::min(samplesRendered, getCaptureCapacitySamples())` — exact by the line above, and requiring no
> new accessor.

**The seconds figures the reviewed draft carried were wrong and are deleted.**
`RollingCaptureBuffer::prepare()` sets `capacity_ = nextPowerOf2(sampleRate * maxDurationSeconds)`
(`rolling_capture_buffer.h:75-93`, the rounding stated at `:81-84` and echoed in the engine banner at
`:37-40`), so the ring is **not** `captureSeconds` of audio plus a chunk:

| fixture | rate | `captureSeconds` | `capacity_` | full ring |
|---|---|---|---|---|
| SC-003, SC-011, SC-001 | 48 kHz | 20 | `nextPowerOf2(960 000) = 1 048 576` | **21.845 s** |
| SC-010 (b) | 8 kHz | 20 | `nextPowerOf2(160 000) = 262 144` | **32.768 s** |
| SC-004 | 48 kHz | 1 | `nextPowerOf2(48 000) = 65 536` | **1.365 s** |

For SC-003 the pre-roll is therefore `getCaptureCapacitySamples() = 1 048 576` samples (21.845 s, not
the "21 s" the draft wrote), and the `coldStartSkips` ceiling recomputes from the arithmetic the spec
already uses: the scheduler's shortest interval at `density = 0.30`, `jitter = 0.5` is
`(1/0.30)·(1 - 0.5·0.5) = 2.5 s` (`grain_scheduler.h:78-88`, interval at `:102`), and the tick at
sample 0 is free (`samplesUntilNextGrain_ = 0.0f` in `reset()`, `:40`, decremented *before* the
`<= 0.0f` test in `process()`, `:73-76`), so `ticks <= 1 + floor(21.845 / 2.5) = 9` and the bound is
**`coldStartSkips <= 9`** — the same number, now derived from the real duration. The *measured-span*
assertion — `getSkippedTriggerCountRingCold()` does not advance **at all** over the following
10 minutes — is unchanged and is still what has teeth. FR-049's short twin takes the same full-ring
pre-roll with a ~30 s measured span. **SC-010 (b)'s "25 s pre-roll (> the 20 s ring at 8 kHz)" was
short by 7.8 s and is replaced by the same full-ring rule** (32.768 s of render at 8 kHz); the actual
pre-roll length of every fixture is recorded in the compliance table as a measured figure.

### P-3 (citation) — three spec references checked against the files; one resolves, two need restating

| Spec says | Actual |
|---|---|
| `VoragoVoice::getGhostRequest` at `vorago_voice.h:977` | correct (`:977`); it is the engine's own comment at `vorago_engine.h:1254` that carries the stale `:942` |
| "`prepare()` seeding beside `:552-556`" | `reset()` seeds at `:552` (scheduler), `:555` (grain), `:556` (blur); `prepare()` does **not** seed — it ends with `reset()` (`:527`). The new stream therefore needs **two** edit sites, not three: `reset()` and `setSeed()` (`:1015-1017`). FR-005's "exactly three places" still holds behaviourally, because `prepare()` reaches `reset()`. |
| `dsp/tests/CMakeLists.txt:373-376` for the Phase 5 TUs | **the spec is right and the reviewed draft's "correction" was wrong.** `atmosphere_engine_test.cpp`, `..._spectral_test.cpp`, `..._perf_test.cpp` and `..._nonfinite_test.cpp` are at `:373-376` (verified by `grep -n` this session); `:377-380` is the Phase 7 block comment plus `seraphis_voice_test.cpp`. The correction is withdrawn. The neighbouring citation *does* move: the `-fno-fast-math` opt-in for `atmosphere_engine_nonfinite_test.cpp` is at **`:896`**, inside the Phase-4/Phase-5 comment block **`:881-896`** (the draft wrote `:895` / `:883-896`). |

### P-4 (shape) — `compareFingerprints` already takes per-comparison bounds

`render_fingerprint.h:122-124` is
`compareFingerprints(actual, reference, double metricTolerance = kMetricTolerance, float sampleTolerance = kSampleTolerance)`,
and `FingerprintComparison` stores the bounds it was evaluated at (`:99-111`; verdict
`withinTolerance()` at `:108-110`, there is no `passes()`). FR-046's "measured per-comparison bounds"
therefore needs **no helper change** — the `noise_organism_test.cpp` protocol is a *usage* discipline
(`:3288-3406`: `kMeasured*Tolerance` constants, `static_assert`s that each per-comparison bound is
**looser** than the shared one and that the shared ones still hold their shipped values, a paste-ready
literal on failure, a PROVENANCE block). This plan reproduces that discipline; nothing under
`tests/test_helpers/` is modified.

### P-5 (blocking, new requirement) — a SECOND birth site per sample breaks the shipped pass-A scratch sizing proof

`prepare()` step 5b sizes the two pass-A scratch vectors from a stated one-birth-per-sample argument
(`atmosphere_engine.h:436-441`, verbatim):

```cpp
// 5b. Phase 11.5 pass-A/pass-B scratch (renderGrainChunk). Sized ONCE
//     here: <= kMaxGrains grains active at a chunk start plus
//     <= kControlChunkSamples births can retire inside one chunk, and
//     kControlChunkSamples == kMaxGrains, so 2 * kMaxGrains bounds both.
retiredScratch_.assign(kMaxGrains * 2, RetiredGrainSpan{});
dueScratch_.assign(kMaxGrains * 2, DueEntry{});
```

§S4 adds a second birth attempt at the same sample index `i` (the scheduler tick, then FR-020's trigger
consumption), so the newborn term becomes `<= 2 * kControlChunkSamples` and the derivation's `2 *
kMaxGrains` no longer bounds either vector. Both are written by **unchecked index** on the audio
thread — `retiredScratch_[retiredCount]` in `bookkeepingRetire` (`:2064-2066`) and `dueScratch_[k] =
DueEntry{...}; ++dueCount;` in the newborn insert (`:2128-2134`), both declared
`std::vector` at `:2597-2598` under the now-stale comment at `:2595-2596` (§S7 anchor (vi)) — so overflowing them is an out-of-bounds write, not a rejected birth.

**It is reachable, not theoretical — but NOT at the sample rate the reviewed draft named, and the
corrected reaching configuration is what SC-012 drives.** Two births at the *same* sample need the
density scheduler to fire on a sample whose pending trigger is also consumed, and the scheduler's
interonset is `sampleRate / density >= sampleRate / kMaxDensity` with `kMaxDensity = 20.0f` (`:304`;
`GrainScheduler::calculateInteronset`, `grain_scheduler.h:100-103`). A *sustained* two-per-sample rate
therefore needs `sampleRate <= 20`. At the draft's `sampleRate = 512` the scheduler fires only every
25.6 samples, pass A tops out near 67 entries per chunk, and nothing overflows — so that configuration
would have been a green test against a real defect.

`prepare()` floors the rate at 1.0 only (`:410`), so `sampleRate = 20` is legal and there the bound is
real. Counting exactly: every due entry is a retirement inside the chunk, so with `a` grains active at
the chunk start, `B` births in the chunk and `a_end` active at the end,
`dueCount = a + B - a_end <= kMaxGrains + 2 * kControlChunkSamples = 192`, and `retiredScratch_` takes
the same count (every due entry is consumed at `:2086-2090` or drained at `:2136-2141`).

A concrete reaching run, two chunks long: `sampleRate = 20`, `density = kMaxDensity = 20` (interonset
exactly `1.0`, so `process()` decrements to `0.0f` and the `<= 0.0f` test fires on **every** sample,
`grain_scheduler.h:74-76`), `captureSeconds = 30` so `C = nextPowerOf2(600) = 1024`, ring pre-rolled
full, the pending queue held at its `kMaxGrains` cap.

- **Chunk 1** at `grainSeconds = 3.2` (`L = round(3.2 * 20) = 64`): two births per sample fill all 64
  slots, and no newborn inserts a due entry (`i + 64 <= 64` only at `i = 0`).
- **Chunk 2** at `grainSeconds = 0.1` (`L = 2` — the smallest legal lifetime here, since
  `kMinGrainSeconds * 20 = 1` is rejected by the `lifetime < 2` test at `:1696-1699`): all 64 carried
  grains have `remaining <= 63 <= numSamples` and so are due (`:2042-2053`), and the newborns add
  ~124 more at two per sample, i.e. **~186 entries against a capacity of 128**.

The count is directly observable without a sanitizer — `Δ getTotalGrainsRetired()` across chunk 2 *is*
that chunk's `dueCount` — which is what makes SC-012 a portable criterion rather than an ASan-only one.

**Resolution — FR-051 (new): raise the prepare-time sizing to `kMaxGrains * 3` and restate the
derivation.** The new bound is tight and stated so no later reader has to re-derive it: with
`numSamples <= kControlChunkSamples = 64` and `lifetime >= 2` (`:1696-1699`), a newborn can only insert
a due entry from sample positions `i <= numSamples - 2`, i.e. **63** positions; at two births per
position that is 126 newborn entries, plus `<= kMaxGrains = 64` grains already active at the chunk
start, for `190 <= 3 * kMaxGrains = 192`. `retiredScratch_` is bounded by the same count (a retirement
is a consumed due entry or an end-of-chunk drain of one).

This is the **fifth** sanctioned deletion anchor in §S7 — `prepare()` step 5b's two `assign` lines and
their derivation comment — and it is the reason FR-045's budget was widened from four sites to six
(§8 item 4, ruled 2026-09-23, spec R-4). The
alternative considered and **rejected**: consuming a pending trigger only on samples where
`scheduler_.process()` did *not* fire keeps the shipped sizing proof and the four-site budget intact,
but it defers a trigger to a later sample and so breaks FR-020's "N calls produce at most N grains
**within the next N rendered samples**, deterministically" — a requirement of the spec, traded away to
protect a plan-level diff budget. Correcting the sizing is the cheaper of the two.

### P-6 (blocking, contract correction) — `triggerGrain()`'s threading contract cannot be both

The reviewed draft's doxygen said `triggerGrain()` "is a CONTROL-THREAD, block-rate call (banner
`:30-35`)" *and* that `VoragoEngine` calls it "on the audio thread", *and* that "no atomic is
introduced and none is needed". Only the audio-thread-only reading makes the last clause true. The
banner's contract (`atmosphere_engine.h:28-35` — "everything else … is noexcept, allocation-free,
lock-free and I/O-free … safe to drive from an automation lane at block rate") is about *stores*: every
other mutator on this component writes a value the audio thread only reads (`setGrainSeconds` `:816`,
`setDensity` `:829`, `setDecorrelation` `:903`, `setLevel` `:983`). `triggerGrain()` is different in
kind — it is a **read-modify-write of `pendingTriggers_` and `droppedTriggers_`, counters pass A also
read-modify-writes** (`--pendingTriggers_`, §S4). A control-thread caller is therefore a data race:
undefined behaviour that can lose or double-count a trigger and break FR-022's "consumed exactly once"
and SC-005 (a)'s exact equality.

**Resolution — the contract is stated once, as AUDIO-THREAD-ONLY** (§S4's rewritten doxygen), which is
what the only caller in this phase actually does: `VoragoEngine::runPreRenderControlStep()` runs inside
`processStereoBlock`, between chunks, on the audio thread. Spec FR-018's "callable from the control
step between or inside blocks" reads as permitting a non-audio caller and needs the same narrowing;
that is the ruling in §8. Supporting a control-thread caller instead would mean
`std::atomic<std::uint32_t> pendingTriggers_` with a CAS saturation loop bounded by `kMaxGrains`, a
relaxed `fetch_add` on `droppedTriggers_`, and a recorded `is_lock_free()` check — cost this phase does
not need, since nothing in it calls from another thread.

---

## 1. Component-by-component design

### S1 — `AtmosphereEngine`: the reverse control surface (FR-001 – FR-005)

**Header:** `dsp/include/krate/dsp/systems/atmosphere_engine.h` (Layer 3). **Include list unchanged**
(`:141-155`), so `node tools/lint-layers.js` has nothing new to see and the banner's "deliberately
ABSENT" list (`:17-25`) is unextended (FR-044).

**Public API, added after `setDecorrelation`/`getDecorrelation` (`:902-905`)**, keeping the
birth-snapshot group together:

```cpp
/// Probability that a grain plays its capture segment BACKWARDS. Range [0, 1],
/// default 0.0 (= the pre-Phase-10a behaviour: every grain forward). Read at
/// birth and SNAPSHOTTED; changing it affects only grains born afterwards.
/// Drawn from a DEDICATED stream (kReverseSalt), never grainRng_ - see the
/// birth-draw banner at :1613-1616 for why a fifth draw there would re-shuffle
/// every Seraphis render.
void setGrainReverseProbability(float probability) noexcept {
    reverseProbability_ = std::clamp(isFinite(probability) ? probability : 0.0f, 0.0f, 1.0f);
}
[[nodiscard]] float getGrainReverseProbability() const noexcept { return reverseProbability_; }
```

This is the component's own shipped setter shape verbatim — `isFinite(x) ? x : <default>` then
`std::clamp` — identical to `setGrainSeconds` (`:816`), `setDensity` (`:829`), `setPositionSpread`
(`:852`), `setDecorrelation` (`:903`) and `setLevel` (`:983`), routed through the ONE
`[[nodiscard]] ITERUM_NOINLINE static bool isFinite(float) noexcept` at `:1269` (never `std::isnan`;
FR-043). A non-finite argument substitutes the control default `0.0f` (FR-003, Clarifications Q2),
which is what SC-008 (b) asserts and what differs by design from Phase 10's FR-069 `MacroMatrix` rule.

**Constant (FR-004)**, beside the salt block at `:331-334`:

```cpp
static constexpr std::size_t kReverseSalt = 0x5000;
```

and beside the existing disjointness assert at `:360`:

```cpp
static_assert(kReverseSalt > kDriftSaltBase + kMaxGrains, "salt ranges must not overlap");
```

`kDriftSaltBase + kMaxGrains = 0x4040`, so `0x5000` is strictly above every existing range
(`kGrainSalt 0x1000`, `kBlurSalt 0x2000`, `kSchedulerSalt 0x3000`, `kDriftSaltBase 0x4000` spanning
`kMaxGrains = 64` entries, `:189`).

**Members**, in the private block: the two control members beside `decorrelation_` (`:2678`) and
`grainRng_` (`:2683`), the counters beside `lastBirthPanR_` (`:2707`):

```cpp
float reverseProbability_ = 0.0f;
Xorshift32 reverseRng_{1};
std::uint32_t pendingTriggers_ = 0;   ///< bounded by kMaxGrains (FR-019)
std::uint64_t droppedTriggers_ = 0;
std::uint64_t totalTriggered_ = 0;
std::uint64_t totalReverseBorn_ = 0;
bool lastBirthReversed_ = false;
```

`std::uint64_t` for the monotonic counters matches `skipPoolFull_`/`totalBorn_` (`:2696-2699`).

**Seeding (FR-005), the two real sites (P-3):**

- `reset()` step 4 (`:555-556`), appended: `reverseRng_.seed(deriveStreamSeed(seed_, kReverseSalt));`
- `setSeed()` (`:1015-1017`), appended: `reverseRng_.seed(deriveStreamSeed(seedValue, kReverseSalt));`

`prepare()` inherits it because `prepare()` ends with `reset()` (`:527`). `deriveStreamSeed`
(`core/random.h:102-111`) never yields 0 — `Xorshift32::seed()` silently substitutes its own default
for 0 (`core/random.h:71-74`), so two streams hashing to 0 would collapse onto one. No other method
re-seeds the stream.

**Counter clearing (FR-024)**, appended to `reset()` step 10 (`:627-638`):

```cpp
pendingTriggers_ = 0;
droppedTriggers_ = 0;
totalTriggered_ = 0;
totalReverseBorn_ = 0;
lastBirthReversed_ = false;
```

`silence()` (`:653-658`) is **not** touched: FR-023 makes `triggerGrain()` a no-op once `Latched`, and
`processStereoBlock` returns before pass A in that state (`:691-696`), so nothing can drain a queue
that also cannot grow.

### S2 — `tryBirthGrain()`: the draw, the direction-dependent rate terms, the fill clause (FR-006–008, FR-014–016, FR-050)

**(a) The draw (FR-006).** One inserted line, immediately after the four `grainRng_` draws at
`:1617-1620` — i.e. **after** the slot sweep's `skipPoolFull_` early-out (`:1601-1612`) and **before**
the liveness arithmetic that consumes it:

```cpp
// --- The FIFTH draw, on its OWN stream (FR-006). UNCONDITIONAL: a draw taken
//     only when the probability is non-zero would make the stream position a
//     function of the control value, and setGrainReverseProbability would stop
//     being a pure gain on a fixed stream. Consumed even when the admission
//     tests below then reject the birth - exactly as the four draws above are
//     (the ring-cold asymmetry pinned at :1578-1589).
const bool reversed = reverseRng_.nextUnipolar() < reverseProbability_;
```

`nextUnipolar()` returns `[0, 1]` (`core/random.h:65-67`), so `< 0.0f` is never true at the default and
`< 1.0f` is true except for the single exact value `1.0f` — documented: at probability 1 the expected
forward-grain rate is 2^-32. The four `grainRng_` draws are **not moved, not reordered, not added to**
(FR-007), which is what makes `getGrainRngState()` (`:1118`) bit-identical pre/post change at every
probability.

**(b) Direction-dependent rate terms (FR-014)** — the one sanctioned deletion at `:1651-1652`:

```cpp
// wUp: the age SHRINKS at this rate. wDown: the age GROWS at this rate.
// A REVERSE grain's read walks backwards while the write head walks forwards,
// so its age can only GROW, at 1 + r per sample - it never catches up with the
// write head and wUp is identically 0 (Phase 10a FR-014).
const double wUp = reversed ? 0.0 : std::max(static_cast<double>(ratioMax) - 1.0, 0.0);
const double wDown = reversed ? (1.0 + static_cast<double>(ratioMax))
                              : std::max(1.0 - static_cast<double>(ratioMin), 0.0);
```

Everything downstream is **unchanged in form and unchanged in code**: `w = wUp + wDown` (`:1658`),
`headroom` (`:1676`), the `headroom <= 2.0` rejection (`:1677-1680`), `slack = headroom - 2.0`
(`:1691`), `lifetime = (w*requested > slack) ? floor(slack/w) : requested` (`:1692`), the
`lifetime < 2.0` rejection (`:1696-1699`), `ageLo = ceil(wUp*lifetime) + guard` (`:1700`),
`ageHi = capacity - 2 - guard - ceil(wDown*lifetime) - decorr` (`:1704-1705`), the birth-age clamp
(`:1708-1712`) and the FR-014 admission test (`:1736-1742`).

Two consequences the tests rely on:

- `wUp = 0` implies `ageLo = kMinAgeSamples = 64` for **every** reverse grain. SC-002's fixture uses
  this to know the birth age without reading a draw, and FR-015 follows immediately: the age starts at
  64 and only grows, so every read a reverse grain makes is at least `kMinAgeSamples` old — the
  precondition `renderGrainChunk`'s banner states at `:1999-2004`, carried by
  `static_assert(kMinAgeSamples >= kControlChunkSamples)` (`:345`).
- Window non-emptiness survives by the **same** one-step argument the shipped comment gives at
  `:1701-1703`: truncation gives `w·L' <= slack` and each `ceil()` adds `< 1`, so `ageHi - ageLo > 0`.
  That argument uses only `w = wUp + wDown` and the truncation, never the identity of either term, so
  it transfers verbatim.

**(c) The fill clause (FR-050, P-1)** — inserted immediately after `:1742`, in the exact form given in
§0. Purely additive, vacuous on a full ring, never evaluated on a forward grain.

**(d) Commit + introspection** — appended inside the existing commit block (`:1780-1806`):

```cpp
grain.reversed = reversed;        // beside grain.active = true (:1795)
lastBirthReversed_ = reversed;    // beside lastBirthPanR_ = panR (:1805)
if (reversed) { ++totalReverseBorn_; }
```

`AtmosphereGrain` (`:1181-1201`) gains exactly one field beside `bool active = false;` (`:1200`):

```cpp
bool reversed = false;  ///< snapshot at birth (FR-008); never re-read from the control surface
```

`grains_.fill(AtmosphereGrain{})` in `reset()` (`:539`) clears it with everything else.

**Untouched and direction-independent (FR-016, FR-017):** the decorrelation offset and its
`birthAge + decorr` use (`:1641-1642`, `:1736`), equal-power pan (`:1748-1751`), drift-lane birth
zeroing (`:1763-1765`), `refreshGrainRatio` (`:1571-1577`), the envelope phase increment (`:1793`),
the active-list append and round-robin cursor (`:1795-1797`), and the two birth age folds
(`:1807-1808`).

### S3 — `renderGrainSpan()`: the backwards walk (FR-010 – FR-013)

**No new buffer, no new allocation, no `ReverseBuffer`** (ADR-1 / FR-010). A reverse grain reads the
same ring through the same
`void LinearReader::indexAt(float ageSamples, size_t newerOffset, std::int32_t& outI0, std::int32_t& outI1, float& outFrac) const noexcept`
(`rolling_capture_buffer.h:279-284`), whose `i1` is one sample **older** than `i0` in both directions
(`:282`), so interpolation is correct unmodified. `accumulateGrainSpanSIMD`
(`processors/grain_span_simd.h`, called at `:1950-1966`) is **not touched** — reverse changes only
which indices the scalar pass writes into `idxL0/idxL1/fracL` (`:1917-1930`).

**The exact backwards decomposition (FR-011, FR-012).** The shipped forward `advance` lambda
(`:1876-1882`) is:

```cpp
const auto advance = [&]() noexcept {
    readFrac += ratio;
    const auto carryInt = static_cast<std::int32_t>(readFrac);
    readIndexInt += static_cast<std::uint64_t>(carryInt);
    readFrac -= static_cast<float>(carryInt);
    ++age;
};
```

The mirror must (i) keep `readFrac` **non-negative** (and `<= 1`) for the grain's whole life — the
property the shipped banner at `:1871-1875` actually relies on, so `LinearReader::index0`'s truncation
of the resulting *age* is still exactly the floor (`rolling_capture_buffer.h:313-321`) — and (ii)
contain **no
`std::floor` and no CRT call** — the codegen rule the lambda's own banner states at `:1871-1875` and
`rolling_capture_buffer.h:302-318` measures. It subtracts `ratio` and takes the **borrow**:

```cpp
const auto advanceBack = [&]() noexcept {
    readFrac -= ratio;                                               // in (-8, 1)
    auto borrow = static_cast<std::int32_t>(-readFrac);              // trunc toward 0
    if (readFrac + static_cast<float>(borrow) < 0.0f) { ++borrow; }  // ceil correction
    readIndexInt -= static_cast<std::uint64_t>(borrow);
    readFrac += static_cast<float>(borrow);
    ++age;
};
```

Exactness for every legal `ratio` in `[0.125, 8]` (the range the birth-time `kMaxAbsGrainSemitones =
36` clamp guarantees, `:1622-1635`, `:311`):

| `readFrac` after the subtract | `-readFrac` | trunc | correction | `borrow` | new `readFrac` |
|---|---|---|---|---|---|
| `0.3` | `-0.3` | `0` | `0.3 >= 0`, none | `0` | `0.3` ok |
| `-0.5` | `0.5` | `0` | `-0.5 < 0`, `+1` | `1` | `0.5` ok |
| `-1.0` exact | `1.0` | `1` | `0.0 >= 0`, none | `1` | `0.0` ok |
| `-7.9` (ratio 8) | `7.9` | `7` | `-0.9 < 0`, `+1` | `8` | `0.1` ok |
| `-2.98e-8` (see below) | `2.98e-8` | `0` | `< 0`, `+1` | `1` | **`1.0f`** exactly |

**The invariant is `readFrac` in `[0, 1]`, not `[0, 1)`, and the last row is why** — the reviewed draft
claimed the half-open form and it is false. `readFrac -= ratio` is *exact* by Sterbenz whenever
`readFrac` sits just below `ratio`, which needs `ratio < 1`, i.e. any downward-pitched grain (SC-003's
operating point is `pitchSemitones = -12`, `ratio = 0.5`). For `readFrac'` in `[-2.98e-8, 0)` the
truncation gives `borrow = 0`, the correction bumps it to 1, and `readFrac' + 1.0f` rounds to exactly
`1.0f` — the ULP at 1.0 is 5.96e-8, so the halfway case rounds to even.

**That is harmless here, and the reason is stated so nobody later leans on the stronger claim.** The
property the code needs is the one the shipped `advance` banner states (`:1871-1875`): *`readFrac` is
NON-NEGATIVE for the grain's whole life, so truncation toward zero IS the floor*. The birth commit
needs the same and no more — its `ceil()` form is justified at `:1767-1771` as "so `readFrac` stays
**non-negative**", not as `< 1`. And `readFrac` is consumed in exactly **one** place in the span
renderer: the `ageAt` subtraction at `:1854-1857`. It is never used as an interpolation weight —
`fracL`/`fracR` come from `reader.indexAt(ageNow, …)` (`:1922-1928`), which derives its own fraction
inside `LinearReader::index0` (`rolling_capture_buffer.h:313-321`) from the *clamped* age. A `readFrac`
of exactly `1.0f` therefore shifts the reported age by one sample-and-a-rounding at most and cannot
break the truncation-is-floor identity, which applies to `age`, not to `readFrac`.
If a future change ever wants the strict `[0, 1)` form, the cost is one compare on the backwards path
only: `if (readFrac >= 1.0f) { readFrac = 0.0f; --readIndexInt; }`. This plan does **not** add it, and
records the reason rather than silently asserting a bound the arithmetic does not give. **Spec FR-012
states the half-open form**, so choosing between the restatement and the clamp is a spec change and is
§8 ruling item 9 — not a divergence left to land silently.

With that correction, `readIndexInt + readFrac` decreases by **exactly**
`ratio` per output sample (to within the single rounding above). `readIndexInt` is `std::uint64_t` and **may wrap below 0** near the start of
a render; that is harmless and deliberate — the age is formed as
`static_cast<float>(static_cast<std::int64_t>((chunkBase + i) - readIndexInt)) - readFrac`
(`:1852-1857`), a modulo-2^64 subtraction cast to signed, which reproduces the true (small, positive,
`< 2^22`) difference exactly under wraparound.

**Direction resolved once per span (FR-013); zero `reversed` tokens inside the loops (SC-006
clause 6).** The scalar index-generation loop (`:1917-1930`) and the cold-path loop (`:1888-1891`) are
wrapped in a **C++20 templated lambda** instantiated twice, so the forward instantiation is the shipped
code instruction for instruction and the flag is never examined per sample:

```cpp
// Direction is resolved ONCE per span (FR-013). Two instantiations: the
// kBackwards == false one is the shipped code, instruction for instruction.
const auto advanceBy = [&]<bool kBackwards>() noexcept {
    if constexpr (!kBackwards) { /* the shipped five lines, verbatim */ }
    else                       { /* the borrow form above           */ }
};
const auto runSpan = [&]<bool kBackwards>() noexcept {
    /* the shipped cold path and scalar pass, verbatim, with every `advance();`
       replaced by `advanceBy.template operator()<kBackwards>();`, and the
       Phase-2 SIMD call unchanged */
};
if (grain.reversed) { runSpan.template operator()<true>(); }
else                { runSpan.template operator()<false>(); }
```

`grain.reversed` is then the **only** occurrence of the token in the function and it sits above both
loops — exactly what SC-006 clause 6's `git diff HEAD -U0` gate counts.
**Portability (risk R-5):** templated lambdas and `.template operator()<...>()` are C++20 and accepted
by MSVC 19.29+, GCC 10+ and Clang 12+, but the construct is unusual in this repo, so the WSL/GCC probe
in §5 is mandatory, not optional. Documented fallback if any leg objects: promote `runSpan` to a
private member function template `template <bool kBackwards> void renderGrainSpanDirected(...)` called
from `renderGrainSpan` — same two instantiations, same gate evidence.

**Unchanged in the span renderer:** `ageAt` (`:1852-1857`), `foldAt` (`:1859-1869`), the envelope phase
multiplication and its endpoint conditioning (`:1936-1948`), the six gathers / three lerps of the SIMD
pass (`:1950-1966`) and the single state store-back (`:1968-1970`).

### S4 — `triggerGrain()` and its consumption point (FR-018 – FR-027)

**Public API**, added after `setGrainEnvelope`/`getGrainEnvelope` (`:995-1000`):

```cpp
/// @brief Request ONE grain be born as soon as the render reaches a sample,
///        independently of the density scheduler (FR-018).
///
/// RT-safe, allocation-free, lock-free. The request is a SATURATING counter
/// bounded by kMaxGrains; a call made when the queue is already full is
/// DROPPED, never queued (FR-019) - the same "skip, never steal" philosophy as
/// skipPoolFull_ (:1609-1612, banner :60-63). A no-op before prepare() and
/// while Latched (FR-023): a latched engine returns from processStereoBlock
/// before pass A (:691-696) and would never drain the queue.
///
/// THREADING - AUDIO THREAD ONLY (P-6). Unlike every other mutator on this
/// component, which is a pure STORE of a value the audio thread only reads
/// (setGrainSeconds :816, setDensity :829, setDecorrelation :903,
/// setLevel :983), this is a READ-MODIFY-WRITE of pendingTriggers_ and
/// droppedTriggers_ - counters renderGrainChunk's pass A also
/// read-modify-writes. Calling it from the UI or automation thread is a DATA
/// RACE, not a benign one: a lost decrement breaks FR-022's "consumed exactly
/// once". The banner's block-rate automation contract at :28-35 does not
/// extend to it. VoragoEngine's only call site is inside
/// runPreRenderControlStep(), which runs within processStereoBlock, between
/// chunks, on the audio thread. No atomic is introduced BECAUSE no caller is
/// off-thread; a control-thread caller would need std::atomic<std::uint32_t>
/// with a CAS saturation loop, not this.
void triggerGrain() noexcept {
    if (!prepared_ || runState_ == RunState::Latched) { return; }
    if (pendingTriggers_ >= static_cast<std::uint32_t>(kMaxGrains)) {
        ++droppedTriggers_;
        return;
    }
    ++pendingTriggers_;
}
```

**Consumption in pass A (FR-020 – FR-022).** The shipped per-sample block at `:2115-2135` is
`if (scheduler_.process()) { before = activeCount_; tryBirthGrain(); if (grew) { bornAt[slot] = i+1; if (i + lifetime <= numSamples) { sorted due insert } } }`.
The trigger path needs the same bookkeeping, so that body is extracted — the deletion FR-045 sanctions
at site (iii) — into a lambda declared beside the existing `bookkeepingRetire` lambda (`:2063-2081`),
with a hoisted predicate above the loop:

```cpp
/// One birth ATTEMPT at chunk sample `i`, with the in-chunk bookkeeping the
/// shipped scheduler path performed. Extracted verbatim so the density
/// scheduler and the FR-018 trigger path share ONE implementation instead of
/// two that can drift. Returns true iff a grain was actually born.
const auto birthAndTrack = [&](std::size_t i) noexcept -> bool {
    const std::size_t before = activeCount_;
    tryBirthGrain();
    if (activeCount_ <= before) { return false; }
    const std::size_t slot = activeIdx_[activeCount_ - 1];
    bornAt[slot] = static_cast<std::uint32_t>(i) + 1u;
    const auto lifetime = static_cast<std::size_t>(grains_[slot].lifetime);
    if (i + lifetime <= numSamples) { /* the shipped sorted insert, :2124-2133 */ }
    return true;
};

// FR-025: ONE test per chunk, not one per sample. On the inert path the loop
// body below loads nothing and tests one hoisted bool.
const bool anyPending = pendingTriggers_ > 0u;
```

and the loop body at `:2115` becomes:

```cpp
// --- Scheduling (FR-021), then FR-020's trigger. THE ORDER IS FIXED AND
//     DOCUMENTED: scheduler first, trigger second, at most one trigger per
//     sample, in sample order.
if (scheduler_.process()) {
    static_cast<void>(birthAndTrack(i));
}
if (anyPending && pendingTriggers_ > 0u) {
    --pendingTriggers_;                    // FR-022: consumed EXACTLY ONCE,
    if (birthAndTrack(i)) {                //         whether or not it births
        ++totalTriggered_;
    }
}
```

Point by point:

- **FR-020** — consumed in pass A, at most one per sample, in sample order, immediately after the
  scheduler tick. N calls produce at most N grains within the next N rendered samples,
  deterministically.
- **FR-021** — the *same* `tryBirthGrain()`: same slot sweep, same four `grainRng_` draws plus the
  fifth reverse draw, same admission tests, same `skipPoolFull_`/`skipRingCold_` counters, same
  in-chunk retirement insert. A triggered grain is an ordinary grain, exempt from nothing.
- **FR-022** — the decrement is unconditional on the outcome, so a trigger consumed into a rejected
  birth is gone and nothing is banked for a later sample.
- **FR-025 / SC-006 clause 6** — `pendingTriggers_` appears in the per-sample body only behind the
  short-circuited `anyPending &&`, so the zero path performs **no load** of the counter.
  **The gate is restated as a token rule, because "zero additional loads on the path taken when it is
  zero" is a semantic property no reviewer can discharge from a `git diff HEAD -U0` excerpt** — which
  is the whole point of making clause 6 diff-anchored rather than a prose assurance. The `-U0`-countable
  form, which §S4's code satisfies exactly: ***`pendingTriggers_` appears in the per-sample body
  exactly once, as the right operand of a short-circuited `&&` whose left operand is the
  loop-invariant `anyPending` declared above the `for`***. The `renderGrainSpan` half of clause 6 is
  already a token count ("zero occurrences of `reversed` between the loop's `for` and its closing
  brace") and is unchanged. This is a §8 ruling item: it restates SC-006 clause 6's pass-A half,
  tightening it from unverifiable prose to a countable rule, and weakens nothing.
- **FR-026** — `totalTriggered_` counts grains a consumed trigger actually produced (not attempts),
  which is what makes SC-005 (a)'s equality a real equality.
- **FR-051 (P-5), the sizing that this second birth site forces.** `prepare()` step 5b
  (`:436-441`) is the **fifth** sanctioned deletion anchor (§S7): both `assign` calls take
  `kMaxGrains * 3` and the derivation comment is restated for two births per sample:

  ```cpp
  // 5b. Phase 11.5 pass-A/pass-B scratch (renderGrainChunk). Sized ONCE here.
  //     Phase 10a FR-051: pass A now has TWO birth sites per sample (the
  //     density scheduler and FR-020's trigger consumption), so the newborn
  //     term is <= 2 * kControlChunkSamples, not one. Tight bound: a newborn
  //     only inserts a due entry from i <= numSamples - 2 (lifetime >= 2,
  //     :1696-1699), i.e. 63 positions x 2 births = 126, plus <= kMaxGrains
  //     grains already active at the chunk start = 190 <= 3 * kMaxGrains.
  //     retiredScratch_ is bounded by the same count (a retirement is a
  //     consumed due entry or an end-of-chunk drain of one).
  retiredScratch_.assign(kMaxGrains * 3, RetiredGrainSpan{});
  dueScratch_.assign(kMaxGrains * 3, DueEntry{});
  ```

  Both vectors are written by **unchecked index** on the audio thread
  (`retiredScratch_[retiredCount]` `:2064-2066`, `dueScratch_[k]` `:2128-2134`), so this is a
  correctness fix, not a headroom nicety. **FR-051's criterion is SC-012**
  (`AtmosphereGhost_PassAScratchBound`, §3.2), not a clause of SC-005: writing past a
  `std::vector`'s size allocates nothing, so `AllocationScope` **cannot** see the failure and the
  reviewed draft's "SC-005 gains the matching in-test guard" would have left FR-051 implementable-and-
  unassertable — the exact FR-009 failure mode this phase exists to avoid, and an unexplained
  asymmetry with FR-050, which did get SC-011. SC-012 pairs a portable assertion (a single 64-sample
  chunk retires more than `kMaxGrains * 2` grains) with an ASan run in which the old sizing is a
  heap-buffer-overflow; §4.4 carries the ASan configure/build/run lines, and §6 step 6 requires the
  arm to be recorded **red at `kMaxGrains * 2` and green at `kMaxGrains * 3`**.

### S5 — Introspection (FR-009, FR-026)

Five accessors appended to the FR-072 block after `getGrainRngState()` (`:1118`), all
`[[nodiscard]] ... const noexcept`, all allocation-free:

```cpp
/// True iff the most recent birth was a REVERSE grain (FR-006's draw, observed).
[[nodiscard]] bool getLastBornGrainReversed() const noexcept { return lastBirthReversed_; }

/// Reverse grains born since reset(). Monotonic; cleared by reset() with the
/// other counters. getTotalReverseGrainsBorn() / getTotalGrainsBorn() is the
/// measured probability SC-007 (d) calibrates.
[[nodiscard]] std::uint64_t getTotalReverseGrainsBorn() const noexcept { return totalReverseBorn_; }

/// Raw state of the REVERSE stream - the only way to prove the draw came from
/// its own stream rather than grainRng_ (ADR-2).
[[nodiscard]] std::uint32_t getReverseRngState() const noexcept { return reverseRng_.state(); }

/// Grains a consumed trigger actually produced (FR-026). Trigger-ONLY: a
/// density-scheduler birth never moves it.
[[nodiscard]] std::uint64_t getTotalTriggeredGrainsBorn() const noexcept { return totalTriggered_; }

/// triggerGrain() calls refused because the pending queue was already at
/// kMaxGrains (FR-019). Distinct from getSkippedTriggerCountPoolFull() (:1053)
/// and getSkippedTriggerCountRingCold() (:1059), which count BIRTH rejections.
[[nodiscard]] std::uint64_t getDroppedTriggerCount() const noexcept { return droppedTriggers_; }
```

Without these, FR-006's draw and FR-019/FR-022's accounting are unassertable and the Phase 2
FR-067/FR-056 failure mode (implemented but unobservable) repeats.

### S6 — `VoragoEngine` wiring, default off (FR-030 – FR-034)

**Config (FR-030).** Two fields appended to `VoragoEngineConfig`'s atmos block
(`vorago_engine.h:114-125`, after `atmosFreezeFftSize` at `:125`):

```cpp
/// Phase 10a. BOTH INERT BY DEFAULT (ADR-3, Clarifications 2026-09-22 Q5):
/// Phase 10's SC-027 and its checked-in kEngineBaselineNsAtPoly4 were measured
/// on the shipped ghost path, and a phase whose own criteria say the ceiling is
/// unchanged may not move either. Phase 14 presets engage them.
float atmosGhostReverseProbability = 0.0f;
bool atmosGhostEventTriggers = false;
```

**Prepare-time forwarding (FR-034).** `VoragoEngine::prepare()` step 5a (`:290-307`) gains **one line**
after `atmos_.setDecorrelation(0.85f);` (`:304`):

```cpp
atmos_.setGrainReverseProbability(cfg.atmosGhostReverseProbability);  // Phase 10a, default 0.0
```

**and the config shadow lands elsewhere, which the plan now states rather than leaving to the
implementer.** `ghostEventTriggers_ = cfg.atmosGhostEventTriggers;` goes in `prepare()`'s **step 2,
"clamp the config"** (`vorago_engine.h:266-271`, beside `maxBlock` and the `polyphony_` clamp) — the
section that already turns `cfg` fields into engine state — **not** in the FR-017 block. FR-030's
"gains one line" therefore stands literally: `:290-307` gains exactly the
`setGrainReverseProbability` call and nothing else, which is the one site FR-030 pins and the one site
SC-006's `-U0` diff evidence is quoted at. Both anchors (step 2 and the FR-017 block) are recorded
separately in the compliance table. Nothing else in the FR-017 block moves: the seven values
(`:299-307`) are untouched and `VoragoEngine_GhostConfiguration` clause 1
(`vorago_engine_test.cpp:2759-2777`) stays green with no edit.

**The rising-edge latch (FR-031).** `runPreRenderControlStep()` already folds the ghost request and
writes the gated level (`:1248-1272`):

```cpp
ghost = std::max(ghost, voices_[v].getGhostRequest());   // :1254, getter at vorago_voice.h:977
...
atmos_.setLevel(ghostPeak_ * ghost);                     // :1272, FR-033 - RETAINED UNCONDITIONALLY
```

Appended immediately after `:1272` (the level write is neither moved nor conditioned):

```cpp
// --- Phase 10a FR-031. The spawn path rides the SAME definition of "a ghost
//     burst" Phase 10's SC-027 detector uses (vorago_engine_test.cpp:2666-2667,
//     detector :2699-2704): a rising crossing of half the burst peak, re-armed
//     only below 5 % of it. A hysteresis band and NOT "was exactly 0, now > 0",
//     because the combined request is combineWake(0, eco, sched) = max
//     (vorago_voice.h:1011-1014, :1846) whose ecosystem term
//     ecosystemDepth_[k] * output[i] (:1734) is CONTINUOUS and prepare()
//     installs depth 0.85 for every kind (:617) - an exact-zero predicate would
//     fire at most once per render. THE LATCHED QUANTITY IS THE GATED VALUE -
//     the very expression written above - so the spawn path closes with the
//     level gate (SC-010 (c) is then literally SC-027 clause 2's closed arm) and
//     no grain is ever spawned for a ghost nobody can hear.
if (ghostEventTriggers_) {
    const float gated = ghostPeak_ * ghost;
    if (!ghostTriggerHigh_ && gated >= kGhostTriggerRise) {
        ghostTriggerHigh_ = true;
        atmos_.triggerGrain();                       // FR-018
    } else if (ghostTriggerHigh_ && gated <= kGhostTriggerFall) {
        ghostTriggerHigh_ = false;
    }
}
```

with, beside `kGhostBurstPeak = 0.60f` (`:204-206`):

```cpp
static constexpr float kGhostTriggerRise = 0.5f * kGhostBurstPeak;
static constexpr float kGhostTriggerFall = 0.05f * kGhostBurstPeak;
```

defined **from** `kGhostBurstPeak` so they cannot drift from `vorago_engine_test.cpp:2666-2667`; and
two members beside `ghostPeak_` (`:1515`):

```cpp
bool ghostEventTriggers_ = false;   ///< FR-030's config shadow
bool ghostTriggerHigh_ = false;     ///< FR-031's latch, the VoragoVoice::eventWasActive_ shape
                                    ///< (vorago_voice.h:1775-1784)
```

`ghostTriggerHigh_` is cleared in `prepare()` and in `reset()` beside `atmos_.reset()` (`:1162`).
**FR-032** follows: with the flag false the block is skipped entirely, `triggerGrain()` is never called
and the latch is not advanced. **FR-033**: the `setLevel` write at `:1272` is unconditional in both
states and nothing is removed.

### S7 — The append-only diff budget (FR-045)

`git diff` of `atmosphere_engine.h` may delete lines at **exactly** these six anchors; every other
change in §S1–S5 is an insertion:

| # | Anchor (base commit) | What changes | FR |
|---|---|---|---|
| (i) | `:1651-1652` `const double wUp` / `wDown` | become direction-dependent | FR-014 |
| (ii) | `:1876-1882` the `advance` lambda **together with the two loops in `renderGrainSpan` that call it — `:1888-1891` (cold path) and `:1917-1930` (scalar index generation) — and no other range** | becomes `advanceBy<kBackwards>` / `runSpan<kBackwards>` | FR-011, FR-013 |
| (iii) | `:2115` the `if (scheduler_.process())` block | its body moves into `birthAndTrack`; the trigger consumption is added beside it | FR-020 |
| (iv) | `:555-556` (reset) and `:1015-1017` (setSeed) | gain the fifth stream | FR-005 |
| (v) | `:436-441` `prepare()` step 5b's derivation comment and its two `assign` lines | `kMaxGrains * 2` → `kMaxGrains * 3`, comment restated for two births per sample | **FR-051 (P-5)** |
| (vi) | `:1183` `readFrac`'s field comment and `:2595-2596` the pass-A scratch declaration comment | both document contracts this phase falsifies (see below) | FR-012, FR-051 |

**Anchor (vi) — two shipped comments this phase makes false, which no other anchor covers.** Both sit
in `atmosphere_engine.h` outside anchors (i)–(v), so SC-006 clause 2 and the extended
`APPEND_ONLY_HEADERS` gate would otherwise **forbid correcting them** and the phase would ship a header
that documents the wrong real-time contract:

1. `:2595-2596`, directly above the `retiredScratch_`/`dueScratch_` declarations at `:2597-2598`, reads
   *"Sized once in prepare() (2 * kMaxGrains each); indexed by count, never pushed on the audio
   thread."* FR-051 changes the sizing at `:436-441`; left alone, the declaration comment still says
   `2 * kMaxGrains` on the two vectors P-5 proves can be overflowed by an unchecked audio-thread write,
   so a reader who trusts it re-derives the 128-entry bound P-5 showed is short. It becomes
   *"Sized once in prepare() (3 * kMaxGrains each — TWO birth sites per sample since Phase 10a FR-051;
   the derivation is at the assign site); indexed by count, never pushed on the audio thread."*
2. `:1183`, `float readFrac = 0.0f;  ///< absolute source index, fraction in [0,1)`. §S3 establishes
   that the backwards `advanceBack` can leave `readFrac` at exactly `1.0f` (the `-2.98e-8` row), so the
   field documents a half-open range the code no longer maintains. It becomes
   *"absolute source index, fraction in [0,1] — a reverse grain's borrow can land on exactly 1.0f; see
   the advanceBack table"*.

Anchor (vi) is a §8 ruling item beside item 4, and §4.3 carries a sixth `APPEND_ONLY_HEADERS` pattern
anchored on exactly these two lines (`Sized once in prepare\(\) \(2 \* kMaxGrains each\)` and
`fraction in \[0,1\)`), so a deletion anywhere else in the header still fails the gate.

**The range is `git diff HEAD ...` before the phase's work is committed** — the form
`tools/check-seraphis-green.js` runs (`:77`; `numstat()` at `:152-155`, `deletedLines()` at
`:171-177`) — and `git diff 374580d7d0f0631561413310bd3085e15ba7279c..HEAD ...` after. A bare
`git diff --numstat <path>` is never the range: post-commit it reports nothing and would pass
vacuously.

*Note on anchor (ii) — this is a FR-045 amendment, not a pattern-writing convenience.* Wrapping the
two loops in `runSpan<kBackwards>` re-indents them, which git reports as deletions at `:1888-1891` and
`:1917-1930` — **outside** the `:1876-1882` range FR-045 site (ii) names. The reviewed draft deferred
that to how `check-seraphis-green.js`'s patterns were later written, but SC-006 clause 2 is an
assertion about *where deletions appear in `git diff HEAD -U0`*, not about what a script tolerates, so
no pattern set can discharge it while the requirement names the narrower range. The documented fallback
(a private member function template) relocates the same two loops and has the identical problem, so it
is not an escape either.

**Resolution:** FR-045 site (ii) is amended **before the build stage** to read *"the `advance` lambda at
`:1876-1882` together with the two loops in `renderGrainSpan` that call it (`:1888-1891`,
`:1917-1930`)"*, and §4.3's `APPEND_ONLY_HEADERS` patterns are written against **those three ranges
only** — so a deletion anywhere else in the header still fails the gate. The measured per-anchor
deletion counts go in the compliance table (§4.3 already plans this). The amendment is a §8 ruling
item; it widens the named site to what the chosen design actually touches, and widens nothing else.

---

## 2. Algorithm notes (so no implementer has to derive them)

**A-1 Reverse read = the same ring read walking backwards.** `AtmosphereGrain` holds the read position
as `std::uint64_t readIndexInt` + `float readFrac` (`:1182-1183`), an *absolute source index*. Forward,
the pair advances by `ratio` while the write head advances by 1, so the age changes at `ratio - 1`.
Backwards, the pair retreats by `ratio` while the head still advances by 1, so the age changes at
`ratio + 1` — strictly growing. That is the whole feature: no second buffer, no capture of its own, no
latency, no crossfade. `ReverseBuffer` (`dsp/include/krate/dsp/primitives/reverse_buffer.h:27`) is
mono, allocates its own `bufferA_`/`bufferB_` in `prepare(double, float)` (`:107-118`), captures its own
input in `float process(float)` (`:152`) and reports a chunk-sized latency (`:75`) — unusable here,
unused, unchanged (ADR-1).

**A-2 The birth-window arithmetic is a substitution, not a rewrite.** The shipped derivation
(`:1644-1712`) is parameterised only by `wUp` (age-shrink rate) and `wDown` (age-grow rate). Reverse
substitutes `(0, 1 + r̄)` for `(max(r̄-1, 0), max(1-r_, 0))`. Every subsequent line — the double-guard
`headroom`, the two-sample ceiling slack, the `floor(slack/w)` truncation, the `L' >= 2` rejection,
both age bounds, the FR-014 admission clip — is unchanged in code.

**A-3 Why `w` is the SUM, and why reverse's is the largest.** The shipped comment at `:1653-1657`
explains that a straddling pitch envelope needs `wUp + wDown`, not `max`. Reverse is the extreme of
that: the age never shrinks, so the whole budget is spent on the old side, `w = 1 + r̄`. At the Vorago
ghost point `r̄ = 0.5612` gives `w = 1.5612` and `w·requested = 899 251 <= slack ~= 1 047 220`, so **no
truncation occurs** there, and `ageHi ~= 148 034` samples (~3.08 s at maximum decorrelation)
comfortably contains the `positionSeconds = 1.0 ± 0.9` birth window. At SC-004's corner
(`captureSeconds = 1` so `C = 65 536`, `grainSeconds = 30`, +36 effective semitones so `r̄ = 8`,
`w = 9`, `decorrelation = 0`): `slack = C - 2 - 2·64 - 0 - 2 = 65 404` and
`L' = floor(65 404 / 9) = 7 267` samples = **0.151 s**.

**A-4 The exact borrow (FR-012)** — §S3, with its four-case proof. The one added instruction on the
backwards path is a compare plus a conditional increment; the forward instantiation has neither.

**A-5 The fill-up deficit (FR-050)** — `available` grows at 1/sample until it saturates at `C`, while a
reverse age grows at `1 + r̄`; the deficit therefore accumulates at `r̄` per sample for
`t* = min(L', C - available)` samples, after which the saturated-regime bound (already enforced by
`ageHi`) takes over. Hence the single added term `ceil(r̄ · t*)`. On a full ring `t* = 0` and the clause
is a no-op — which is why every *other* fixture in §3 pre-rolls to a full ring (so the shipped
admission arithmetic is what those cases test), and why production (Phase 14) pays the clause only
during the first `captureSeconds` of a fresh render.

**`t* = 0` everywhere is exactly why FR-050 needs a fixture of its own.** SC-011 is the single case
that deliberately does *not* pre-roll to a full ring: it renders the `t* > 0` regime P-1 derives, and
it is the only place in the phase where a wrong sign, a wrong term or a missing clause changes a
result. Every other reverse case is, with respect to FR-050, vacuous by construction — stated here so
no later reader mistakes their greenness for coverage.

**And admitted-implies-safe is a theorem, which bounds what any criterion can assert.** Substituting
the admission predicate `A >= needed + ceil(rMax * t*)` into the crossing condition
`A < needed_exact + rMax * t*` (worst case at `t = t*`; beyond `t*` the ring is saturated and `ageHi`
already bounds the life, §A-2) gives a contradiction, because `needed >= needed_exact` and
`ceil(x) >= x`. So **no fixture that requires a birth can ever observe a stale read** while the clause
is in. The consequences are load-bearing for §3: SC-011's green arms assert the *admission decision*
((a) rejection, (a2) the threshold), and the *stale read itself* is observable only in the
clause-disabled run of §6 step 4 (iii). A criterion claiming to bound the read age on a green path —
as the reviewed draft's SC-011 (b) did — is vacuous by construction, not merely weak.

**A-6 The hysteresis edge (FR-031)** — a two-state Schmitt latch on `ghostPeak_ * ghost` with
`rise = 0.5·kGhostBurstPeak`, `fall = 0.05·kGhostBurstPeak`, firing on the `false -> true` transition
only. Identical in constants and shape to Phase 10's SC-027 detector (`vorago_engine_test.cpp:2666-2667`,
`:2699-2704`) and to `VoragoVoice::gatherSchedulerLanes`'s own onset-edge rule for
`bloom_.triggerBloom()` (`vorago_voice.h:1772-1784`, "the ONSET EDGE, never the level"). Stated
consequence: a ghost swell whose peak never reaches `0.5·kGhostBurstPeak` raises the level (FR-033) but
spawns **no** grain — the same population SC-027's `>= 6` floor was measured on.

**A-7 SC-002's reachability arithmetic.** At `ratio == 1.0` exactly (pitch 0, spread 0, drift 0):
forward holds its age constant (`ratio - 1 == 0`) and traverses the source forwards 1:1; reverse
retreats one source sample per output sample and traverses it **backwards 1:1**. `wUp = 0` in both arms
(forward's is `max(1-1, 0) = 0`), so `ageLo = 64` and, with `positionSeconds = positionSpread = 0`, the
birth age is the clamp value `64` — **known, not drawn**. A 200 Hz → 4 kHz chirp over 1.0 s sweeps at
3 800 Hz/s, so the expected centroid slopes are ∓3 800 Hz/s and clause (b)'s 500 Hz/s floor carries a
**7.6×** margin.

---

## 3. Test plan

### 3.1 New translation units

| TU (under `dsp/tests/unit/systems/`) | Holds | fast-math |
|---|---|---|
| `atmosphere_ghost_test.cpp` | SC-001, SC-002, SC-004 (short arm, **never** `[long]`), SC-005, SC-006 (`AtmosphereGhost_AppendOnly`), SC-007 (a)–(c), SC-008 (a)(c)(d)(e), **SC-011 (FR-050, untagged)**, **SC-012 (FR-051, untagged)**, FR-049's short twin | default (**not** in the opt-out list) |
| `atmosphere_ghost_longrun_test.cpp` | SC-003 `[long]`, SC-007 (d) `[long]`, SC-004's exhaustive sweep sibling `[long]` (only if measured > 15 s) | default |
| `atmosphere_ghost_nonfinite_test.cpp` | SC-008 (b) — the only case that **injects** NaN/±Inf bit patterns | **`-fno-fast-math` opt-in list** (FR-042) |
| `atmosphere_ghost_perf_test.cpp` | SC-009 `[.perf]` | default (**never** the opt-out list — it would move the figures) |
| `vorago_ghost_ext_test.cpp` | SC-010 (a)(c); (b)(d) in a `[long]` sibling case (decision D-4) | default |
| `vorago_perf_budget.h` (header, FR-047) | the extracted Phase 10 constants | n/a |

The split follows `dsp/tests/CMakeLists.txt:881-896`'s stated rule verbatim: only the TU that *injects*
bit patterns is registered for `-fno-fast-math`; detection-only checks (SC-004's "no NaN, no Inf in the
render") stay on the fast-math path and use the bit-pattern predicate, never `std::isnan`.

**Decision D-4 (tagging; FR-048/FR-049 precedent).** The spec names one case for SC-010. Phase 10's own
600 s ghost case is untagged (`vorago_engine_test.cpp:2759`), but this phase's arm (b) actually
*renders grains*, so its measured runtime may exceed 15 s. If it does, the case splits exactly as
FR-049 splits SC-003: `VoragoEngine_GhostExtensionWiring` (clauses (a) and (c), untagged, per push) and
`VoragoEngine_GhostExtensionWiring_Engaged` (clauses (b) and (d), `[long]`). Measured runtimes for both
go in the compliance table either way; nothing is dropped, only the lane changes.

**D-4 does NOT extend to SC-004.** SC-004's assertions are NaN/Inf-guard and bounded-grid assertions,
which `CLAUDE.md`'s Build Commands rule and FR-048's own closing sentence forbid tagging `[long]` at
any measured cost. It is removed from FR-048's candidate list (§8.8) and, if long, split into an
untagged short arm plus a `[long]` sweep sibling — the split keeps the sentinel assertions in the
per-push lane, which a tag would not.

### 3.2 Per-criterion design

Seeds are fixed. **"Warm ring" / "full ring" always means the P-2 full-ring pre-roll**: from a fresh
`prepare()`/`reset()`, render exactly `engine.getCaptureCapacitySamples()` samples
(`atmosphere_engine.h:1133-1137`), which saturates the ring because
`getAvailableSamples() == std::min(samplesWritten_, capacity_)`
(`rolling_capture_buffer.h:443-445`). No fixture states a pre-roll in seconds; each records its
measured sample count and the seconds it works out to in the compliance table. Where a case needs the
*available* count during filling it computes `std::min(samplesRendered, getCaptureCapacitySamples())`
itself — `AtmosphereEngine` exposes no `getAvailableSamples()` and this phase does not add one.

**SC-001 `AtmosphereGhost_DefaultInert` — `atmosphere_ghost_test.cpp`**

- *Clause 1, render identity.* 60 s at 48 kHz, `captureSeconds = 20`, seed 1, the Vorago ghost
  configuration as `buildAtmosphere()` writes it (`vorago_perf_test.cpp:609-630`), excitation a
  deterministic pink-noise-plus-tone built with a local `Xorshift32` (never `<random>`: not portable,
  `vorago_perf_test.cpp:376-379`). `fingerprintRender(std::span<const float>)`
  (`render_fingerprint.h:73`) over the left channel, compared by
  `compareFingerprints(actual, kBaseCommitFingerprint, kMeasuredMetricTolerance, kMeasuredSampleTolerance)`
  (`:122-124`), verdict `withinTolerance()` (`:108-110`).
  **Bounds protocol (FR-046 — the `noise_organism_test.cpp:3288-3406` shape, reproduced not
  reinvented):** two `constexpr` per-comparison bounds, with `static_assert`s that each is **looser**
  than the shared `kMetricTolerance = 2.5e-4` / `kSampleTolerance = 5.0e-4f` *and* that the shared
  constants still hold their shipped values (so a "fix" that edits `render_fingerprint.h` breaks the
  build rather than passing); a documented three-toolchain probe (MSVC measured here, GNU and LLVM from
  the CI legs or a local WSL run) whose **worst observed deviation with headroom** sets each bound; a
  paste-ready literal printed on failure; and a PROVENANCE block naming base commit
  `374580d7d0f0631561413310bd3085e15ba7279c`, the machine, the compiler and the date. The measured
  bounds are transcribed back into the spec once taken and may not be widened afterwards without a
  ruling.
  **How the reference is obtained:** a worktree checked out at the base commit runs the *identical*
  fixture function; the resulting `RenderFingerprint` is transcribed as a `constexpr` initialiser —
  the mechanism `vorago_perf_test.cpp:236-253` already uses for its own baselines. A failure is a
  finding, not an invitation to update the literal.
- *Clause 2, RNG-stream identity (integer equalities, never floats).*
  - **No-trigger arm:** `REQUIRE(engine.getGrainRngState() == kBaseCommitGrainRngState)` after the same
    render, at probability `0.0f` **and** `1.0f`. This is the check that fails if the reverse bit were
    drawn from `grainRng_`, whatever clause 1's tolerances happened to absorb.
  - **Trigger arm (Q3), computed-delta replica:** warm ring, then 100 `triggerGrain()` calls. A replica
    `Xorshift32 replica{deriveStreamSeed(seed, AtmosphereEngine::kGrainSalt)}` held only by the test is
    advanced **four `next()` calls per observed `getTotalGrainsBorn()` increment** (FR-027's
    per-attempt draw contract; valid here because a warm ring makes attempted and admitted counts
    equal — the case asserts `Δ getSkippedTriggerCountRingCold() == 0` to prove it), then
    `REQUIRE(replica.state() == engine.getGrainRngState())`.
- *Clause 3, counter identity.* `getTotalGrainsBorn()`, `getTotalGrainsRetired()`,
  `getSkippedTriggerCountPoolFull()`, `getSkippedTriggerCountRingCold()` (`:1053-1070`) and
  `getLatencySamples()` (`:1128`) equal their transcribed base-commit values at probability 0 with no
  trigger.

**SC-002 `AtmosphereGhost_ReverseIsTimeReversed` — `atmosphere_ghost_test.cpp`**

Fixture exactly as the spec states (48 kHz, `captureSeconds = 4`, seed 1, probability 1,
pitch/spread/drift all 0 so `ratio == 1.0` for the whole life, `positionSeconds = positionSpread = 0`
so the birth age is the `kMinAgeSamples = 64` clamp, `decorrelation = 0`, `panSpread = 0`, blur and
freeze off, `level = 1.0`, `jitter = 0`, `density = kMinDensity = 0.1`, `grainSeconds = 0.5`),
excitation a linear chirp 200 Hz → 4 kHz over exactly 1.0 s whose midpoint coincides with the trigger
sample `T`, silence outside.
**Isolation is asserted, never assumed:** render the pre-roll; wait for `getActiveGrainCount() == 0`
(the scheduler fires on the first render sample because `samplesUntilNextGrain_` starts at `0.0f`,
`grain_scheduler.h:40`, `:73-76`); snapshot `getTotalGrainsBorn()`; `triggerGrain()`; render the
grain's life in 64-sample blocks; `REQUIRE(getActiveGrainCount() == 1)` at **every** block boundary of
the measured span and `Δ getTotalGrainsBorn() == 1` across it; take the span from
`getLastBornGrainLifetimeSamples()` (`:1112-1114`). A colliding scheduler birth fails the case loudly —
a fixture defect to fix, never a reason to widen (a) or (b).
**The isolation preamble also pins the per-birth draw (FR-009, FR-006).** Immediately after the
`Δ getTotalGrainsBorn() == 1` check: `REQUIRE(engine.getLastBornGrainReversed())` in the
probability-1 arm and `REQUIRE_FALSE(...)` in the probability-0 comparison arm. Without this the
accessor FR-009 adds is read by no criterion in the phase — the milder form of the Phase 2
"implemented but unassertable" failure FR-009 exists to prevent — and FR-006's draw is observable only
as a long-run fraction in SC-007 (d).

- (a) normalised cross-correlation peak **>= 0.90** against the Hann-windowed time-reversed reference
  segment (source `[T-0.5 s, T]`, reversed, windowed with the same Hann the grain envelope applies) and
  **<= 0.30** against the identically windowed forward segment (`[T, T+0.5 s]`).
- (b) STFT spectral-centroid trajectory over the grain's life: least-squares slope **negative** for the
  reverse grain and **positive** for the same grain at probability 0, `|slope| >= 500 Hz/s` in both
  (expected ±3 800 Hz/s, A-7). Frames via `TestUtils::Vorago::frameMagnitudes`
  (`tests/test_helpers/vorago_fixtures.h:137`) and `TestUtils::spectralCentroidHz`
  (`tests/test_helpers/reverb_metrics.h:291`, re-exported at `vorago_fixtures.h:86`) — **reused, not
  re-implemented**. Clause (b) exists because clause (a) alone is satisfiable by a symmetric artefact.
- (c) **FRACTIONAL-RATIO ARM — the clause that makes FR-012 falsifiable.** Clauses (a) and (b) are both
  configured at `ratio == 1.0` exactly, which is precisely the case in which §S3's ceil-correction
  branch never fires (`readFrac` stays at 0 for the grain's whole life). SC-004 asserts only lifetime
  truncation, NaN/Inf absence and the `kMaxLevel` bound; SC-008 (e) asserts partition invariance. An
  off-by-one borrow, or a `readFrac` that drifts outside its range, renders deterministically, without
  NaN, bounded and partition-invariantly — so **every one of those passes while the read position is
  wrong at every fractional ratio, which is every production configuration** (the Vorago ghost point is
  `r̄ = 0.5612`). This arm closes that hole.

  Same fixture, one change: `setPitchSemitones(-5)` (`pitchSpread` and drift still 0), so every grain
  has the same known `ratio = 2^(-5/12) = 0.749154…`. The assertion is an exact identity on the read
  walk, not a spectral band:

  ```
  REQUIRE(getMaxObservedGrainAgeSamples() - getMinObservedGrainAgeSamples()
          == Approx(double(1.0 + ratio) * double(L' - 1)).margin(1.0));
  ```

  with `L' = getLastBornGrainLifetimeSamples()`. The age folds are `:1550-1554`, read through
  `:1099-1103`. **Why this is clean despite the folds being engine-lifetime, not per-grain:**
  `positionSeconds = positionSpread = 0` and `wUp = 0` put *every* grain's birth age at the
  `kMinAgeSamples = 64` clamp, so `min == 64` exactly; `pitchSpread = 0` and `decorrelation = 0` make
  every grain identical in `ratio`, lifetime (no truncation: `w·requested = 41 981` against
  `slack ≈ 262 000` at `captureSeconds = 4`) and fold sites, so whichever grain sets `max` sets it to
  the same `64 + (1+ratio)·(L'-1)`. The identity therefore holds regardless of how many pre-roll grains
  the scheduler contributed — stated here rather than left as an assumption the fixture silently makes.
  Secondary, from the same frames clause (b) already computes: the centroid slope at this ratio is
  `-(ratio × 3800) ≈ -2 847 Hz/s`, `REQUIRE`d negative with `|slope| >= 500 Hz/s`, which catches a
  sign-inverted walk that the age identity would also catch but from the audio side.
- (d) **FR-008 mid-life snapshot, asserted where it is observable.** FR-008 requires the `reversed`
  flag to be snapshotted at birth and never re-read from the control surface. The reviewed draft
  observed it in SC-007 (c) as "an unchanged `getTotalReverseGrainsBorn()`" — a **birth** counter,
  which is unchanged whenever no grain is born and says nothing about a live grain's direction; an
  implementation consulting `reverseProbability_` per sample would flip live grains mid-life and pass
  that check untouched. So it is asserted here instead, on the isolated span: birth one grain at
  probability 1, call `setGrainReverseProbability(0.0f)` at the span's midpoint, and REQUIRE clause
  (a)'s `>= 0.90` reversed-reference cross-correlation over the **whole** grain span (and clause (b)'s
  slope negative across the change point, not just before it). A per-sample re-read halves the
  correlation and flips the second half's slope.

**SC-003 `AtmosphereGhost_ReverseLiveness` `[long]` — `atmosphere_ghost_longrun_test.cpp`**

Vorago ghost operating point in full: probability 1, `density = 0.30` (`vorago_perf_test.cpp:620`),
`grainSeconds = 12`, `captureSeconds = 20`, `pitchSemitones = -12`, `positionSpread = 0.9`,
`decorrelation = 0.85`, 48 kHz, seed 1.

- (a) **full-ring pre-roll** (§3.2's definition: `getCaptureCapacitySamples() = 1 048 576` samples
  = **21.845 s** at 48 kHz / `captureSeconds = 20`, per P-2's table — *not* the 21 s the reviewed draft
  hand-computed), which makes FR-050 vacuous (`t* = 0`), then
  `coldStartSkips = getSkippedTriggerCountRingCold()` with `REQUIRE(coldStartSkips <= 9)` —
  `1 + floor(21.845 / 2.5)` scheduler ticks, 2.5 s being the shortest interval `jitter = 0.5` permits
  (`grain_scheduler.h:78-88`, interonset at `:102`), the free tick at sample 0 being the `1`
  (`:40`, `:73-76`). Then **10 minutes** and
  `REQUIRE(getSkippedTriggerCountRingCold() == coldStartSkips)`: the counter does not advance **at
  all**, i.e. the birth window is non-empty at the Vorago operating point.
- (b) **click freedom, restated so it has a mechanism.** The reviewed draft's per-grain endpoint
  clause ("the first and last emitted sample of *every* reverse grain is exactly `0.0f`") is **not
  observable here and is dropped from SC-003**: `processStereoBlock` emits only the summed grain bus
  after the FR-028 population gain (`:2171-2192`) and the FR-061 level smoother, and at
  `density = 0.30 × grainSeconds = 12` there are ~3.6 concurrent grains, so no endpoint of any
  individual grain reaches the output. The per-grain endpoint guarantee is asserted where a single
  grain *is* isolated — **SC-002**, which already runs the one-grain protocol
  (`getActiveGrainCount() == 0`, `triggerGrain()`, `== 1` at every block boundary): SC-002 gains
  `REQUIRE(span.front() == 0.0f && span.back() == 0.0f)` on the isolated reverse span, exactly and
  bit-wise, the envelope endpoint guarantee at banner `:96-101`.
  What remains in SC-003 (b) is the **comparison** clause, with its detector named: the Seraphis Phase 5
  click detector `TestUtils::countClicks(span, sigma)` — the helper and its sigma are cited by file:line
  in the compliance record from `atmosphere_engine_test.cpp`'s own forward liveness cell, read at
  implementation time and quoted, exactly as every other reused helper in this plan is
  (`frameMagnitudes` `vorago_fixtures.h:137`, `spectralCentroidHz` `reverb_metrics.h:291`). If that
  cell turns out to use a locally-defined detector rather than a shared helper, the detector is
  **copied with a citation** into the new TU rather than re-invented, and the sigma is transcribed
  unchanged. Assertion: **no additional detections at probability 1 versus probability 0** over the
  same render — a *relative* bound, so a detector-sigma mismatch cannot make it vacuously true.
- (c) `getMinObservedGrainAgeSamples() >= kMinAgeSamples` and
  `getMaxObservedGrainAgeSamples() <= capacity - 2` (`:1099-1103`) — FR-015. **This is a bound on the
  COMPUTED age and it is not FR-050's backstop**: P-1 shows the computed age stays inside
  `[64, C-2]` precisely while the *read* is clamped and stale, so on a full ring — which this fixture
  guarantees — (c) would pass with FR-050 absent, wrong-signed or wrong-termed. FR-050's binding
  criterion is **SC-011**, below.
- FR-049 twin `AtmosphereGhost_ReverseLiveness_Short` (untagged, per push): same configuration, same
  full-ring pre-roll, **~30 s** measured span, clause (a) only.

**SC-004 `AtmosphereGhost_ReverseTruncation` — `atmosphere_ghost_test.cpp`, NEVER `[long]`.**

**The reviewed draft's conditional tagging is withdrawn.** FR-048's own closing sentence restates the
project's standing rule (`CLAUDE.md`, Build Commands): *a NaN/Inf-guard case, a bounded-grid case or a
state-format case is **never** tagged `[long]` regardless of measured cost*. SC-004's assertion set is
exactly that class — no NaN, no Inf (bit-pattern check), no sample above `kMaxLevel = 2.0` — and it is
the **only** reverse-path NaN/Inf and bound check at the extreme ratio corner (`r̄ = 8`,
`captureSeconds = 1`). Tagging it would move the phase's cross-platform sentinel out of the per-push
lane into the nightly one, which is the lane the rule exists to protect. So: **SC-004 is removed from
FR-048's `[long]` candidate list.** If it measures over 15 s it is **split the way FR-049 splits
SC-003**, not tagged:

- `AtmosphereGhost_ReverseTruncation` (untagged, per push): the same corner configuration, fewer
  blocks, carrying the NaN/Inf + `kMaxLevel` + `lifetime >= 2` assertions and both arms' lifetime
  bounds on a handful of births;
- `AtmosphereGhost_ReverseTruncation_Sweep` (`[long]`, `atmosphere_ghost_longrun_test.cpp`): only the
  exhaustive per-block truncation sweep.

Measured runtime is recorded either way.

`captureSeconds = 1` (`kMinCaptureSeconds`, `:317`), `grainSeconds = 30` (`kMaxGrainSeconds`, `:302`),
probability 1, `density = kMinDensity`, and — added by this plan — **`decorrelation = 0`**, so `slack`
is exactly recomputable from public accessors (`decorrAge` is a per-grain draw with no getter). Ring
pre-rolled to full — `getCaptureCapacitySamples() = nextPowerOf2(48 000) = 65 536` samples =
**1.365 s** (P-2's table) — so FR-050 is vacuous. Two arms:

- *exact arm*: `pitchSpread = 0`, `pitchSemitones = +24`, `driftRangeSemitones = 12` gives
  `semisHi = 36` (the `kMaxAbsGrainSemitones` clamp) and `r̄ = 8` exactly, so
  `REQUIRE(getLastBornGrainLifetimeSamples() == floor(slack / 9))` with `slack` recomputed in the test
  from `getCaptureCapacitySamples()` and `kMinAgeSamples`;
- *corner arm*: `pitchSpread = 1` (the spec's corner; `r̄` is then draw-dependent in `[4, 8]`), so
  `REQUIRE(lifetime <= floor(slack / 5))` — the bound that holds for **every** admissible draw.

Both arms additionally: no birth with `lifetime < 2` (`:1696-1699`); no NaN and no Inf in the render
(bit-pattern predicate); no `|sample| > kMaxLevel = 2.0` (`:316`).
**Observation granularity (the spec's own requirement, kept):** drive the render in 64-sample blocks,
read `getLastBornGrainLifetimeSamples()` after every block, drive births with **at most one**
`triggerGrain()` per block, `REQUIRE(Δ getTotalGrainsBorn() <= 1)` per block (a block seeing two births
fails as a fixture defect rather than being silently skipped), and at the end
`REQUIRE(observations == Δ getTotalGrainsBorn())` — the assertion that closes "every admitted grain".

**SC-005 `AtmosphereGhost_TriggerAccounting` — `atmosphere_ghost_test.cpp`** (all arms at
`density = kMinDensity` so the scheduler contributes at most one grain per window).

- (a) N in {1, 5, 64} calls on a **warm ring** then a render of >= N samples.
  **The pool must be empty for the N = 64 arm's equality to be an equality, and the reviewed draft did
  not make it so.** At the shipped default `grainSeconds_ = 4.0f` (`:2667`) a ~21.8 s full-ring
  pre-roll at `density = kMinDensity` births two or three scheduler grains and one is very likely
  still alive, so with `k0 >= 1` the 64th trigger hits the slot sweep's pool-full early-out
  (`:1608-1612`) and **63** triggered births occur — the equality fails on correct code. The (b) warm
  arm already accounts for `k0` explicitly; (a) now inherits that discipline. Two fixes, both applied:
  - pin `setGrainSeconds(kMinGrainSeconds)` (0.05 s, `:301`) for this arm and, immediately before
    firing the N triggers, `REQUIRE(getActiveGrainCount() == 0)` — with a short idle render to let any
    pre-roll grain retire, which at 0.05 s it does within 2 400 samples;
  - state the equality in the `k0`-aware form so it stays true if a scheduler grain does survive:
    `Δ getTotalTriggeredGrainsBorn() == N - poolFullDelta` with
    `Δ getSkippedTriggerCountPoolFull() == poolFullDelta`, and `poolFullDelta == 0` REQUIREd for
    N in {1, 5} and for N = 64 given the `getActiveGrainCount() == 0` precondition.

  Plus `N <= Δ getTotalGrainsBorn() <= N + 1` (the one possible scheduler birth).
- (b) **cold arm** (no prior render): `kMaxGrains + 10 = 74` calls before any render, then a render of
  **exactly 128 samples** (>= 64, so all 64 pending triggers are consumed at one per sample, FR-020;
  the length is stated because the reviewed draft said only "then a render"):
  `getDroppedTriggerCount() == 10`, `getTotalGrainsBorn() == 0`, and
  **`getSkippedTriggerCountRingCold() == 64 + schedulerTicks`**.

  **The reviewed draft's bare `== 64` is wrong on correct code.** `GrainScheduler::reset()` sets
  `samplesUntilNextGrain_ = 0.0f` (`grain_scheduler.h:40`) and `process()` decrements *before* testing
  `<= 0.0f` (`:73-76`), so the density scheduler fires a birth attempt on the **very first rendered
  sample**; on a cold ring that attempt fails the admission test at `:1736-1742` and increments
  `skipRingCold_` too. The spec establishes this exact first-sample tick itself (spec.md:568-572), so
  `== 64` contradicts a fact the same document states. `schedulerTicks = 1 + floor(renderSeconds /
  interonsetSeconds)`, which at `kMinDensity` (10 s interonset) is **1** for any render under 10 s, so
  the concrete assertion is `== 65`. The test computes `schedulerTicks` from the rendered length rather
  than hard-coding 65. It then isolates the trigger accounting from the scheduler with a **rate**
  assertion, which is also the only place in the phase with any purchase on FR-020's "at most one per
  sample, in sample order":

  - snapshot the counter, render **exactly 1 sample**, REQUIRE the delta is **exactly 2** — the
    scheduler's free first tick (`samplesUntilNextGrain_ = 0.0f`, `grain_scheduler.h:44`, decremented
    at `:74` before the `<= 0.0f` test at `:76`) **plus exactly one** consumed trigger, both rejected
    ring-cold at `:1736-1741`;
  - render **1 more sample**, REQUIRE the delta advances by **exactly 1** (one trigger, no scheduler
    tick — the next interonset at `kMinDensity` is 10 s away);
  - over the remaining 126 samples of the 128-sample render the delta is therefore **62**, and the
    post-first-sample total is **63**, not the 64 the reviewed draft wrote. That off-by-one was red on
    correct code: the reviewed draft's own paragraph establishes the first-sample scheduler tick and
    then counted it out of the delta.

  Without the rate assertion an implementation that drains the **whole** pending queue on a single
  sample passes every other criterion in the phase — SC-005 (a) renders ">= N samples", (b)'s totals
  are direction-agnostic about rate, and SC-010 (b) delivers triggers one at a time — and it would also
  invalidate FR-051's two-births-per-sample derivation.

  This arm tests the **pending-queue** saturation and the cold-ring path; it does not reach the pool
  cap and does not claim to.
  **warm arm** (the pool bound, roadmap line 509): render to a full ring, record
  `k0 = getActiveGrainCount()` and the three counters, fire 74 calls, render 64 samples:
  `Δ getTotalGrainsBorn() == kMaxGrains - k0`, `Δ getSkippedTriggerCountPoolFull() == k0`,
  `Δ getDroppedTriggerCount() == 10`, `getActiveGrainCount() == kMaxGrains`. Then 10 further calls and
  a render: `Δ getTotalGrainsBorn() == 0`, `Δ getSkippedTriggerCountPoolFull() == 10`,
  `getActiveGrainCount()` still `kMaxGrains` — the cap holding under continued pressure with **no**
  grain stolen (FR-021, `:1609-1612`). The warm arm runs at probability 0 so all 64 births are admitted
  by the shipped test; the reverse warm case is SC-003's.
- (c) calls into a **cold ring** are consumed exactly once: `getTotalGrainsBorn()` unmoved,
  `getSkippedTriggerCountRingCold()` advances by the number consumed, and a later warm render shows
  **no** deferred burst (FR-022).
- (d) three sub-arms, because the reviewed draft's single one asserted nothing.
  - **Latched no-op (FR-023, clause 2).** After `silence()` has latched (`getActiveGrainCount() == 0`,
    output exactly `0.0f`), `triggerGrain()` produces no grain and moves no counter.
  - **`reset()` clears the queue and the counters (FR-024, FR-009, FR-026) — asserted on CONSUMPTION
    evidence, not on births.** The draft's shape ("trigger while latched, reset, render, require
    `getTotalTriggeredGrainsBorn() == 0`") is **vacuous**: FR-023 makes `triggerGrain()` a no-op while
    Latched, so nothing was ever queued. The reviewed draft's replacement — fire 74 on a cold ring,
    `reset()`, render, REQUIRE `Δ getTotalGrainsBorn() == 0` — is **vacuous in the same half**:
    `reset()` also empties the capture ring (spec.md:571-573), so after it no birth is admissible
    whether or not `pendingTriggers_` was cleared, and the births assertion holds identically either
    way. Only FR-024's dropped-counter half had teeth. Restated so both halves do, in two sub-arms:
    - **Cold-ring, consumption-counted.** On a non-latched, cold-ring engine fire
      `kMaxGrains + 10 = 74` calls, REQUIRE `getDroppedTriggerCount() == 10` (so the queue is provably
      at 64), then `reset()`, then render **>= 64 samples** and REQUIRE
      `getSkippedTriggerCountRingCold() == schedulerTicks` — **not** `Δ`, because `reset()` zeroes
      `skipRingCold_` at `:628`, and `schedulerTicks` is computed from the rendered length exactly as
      in (b). An uncleared 64-deep queue gives `64 + schedulerTicks`. Plus `getTotalGrainsBorn() == 0`
      and `getDroppedTriggerCount() == 0`.
    - **Warm-ring, counters provably advanced.** Full-ring pre-roll at reverse probability 1 and
      `kMinGrainSeconds`, fire and consume several triggers so that `getTotalTriggeredGrainsBorn() > 0`
      and `getTotalReverseGrainsBorn() > 0` are REQUIREd *before* the reset; then `reset()` and REQUIRE
      `getTotalTriggeredGrainsBorn() == 0`, `getTotalReverseGrainsBorn() == 0` and
      `getDroppedTriggerCount() == 0`. Without this sub-arm an implementation that forgot
      `totalReverseBorn_ = 0;` or `totalTriggered_ = 0;` in `reset()` step 10 passes every criterion in
      the phase — and those two are exactly the accessors FR-009 and FR-026 add to avoid the Phase 2
      "implemented but unassertable" failure.
  - **`Silencing` accepts triggers (FR-023, clause 3) — the clause no criterion exercised.** FR-023 has
    three clauses; an implementation that early-returned on `runState_ != RunState::Running` (the
    natural over-broad guard) satisfies the other two and silently drops every trigger during the
    FR-007 retirement ramp. Arm: warm ring, grains alive, `silence()`, then **while
    `getActiveGrainCount() > 0`** (i.e. still `Silencing`, not yet `Latched`) call `triggerGrain()` and
    REQUIRE `getDroppedTriggerCount()` is unmoved *and* that the trigger was **accepted**, observed as
    `Δ getTotalTriggeredGrainsBorn() >= 1` over the remaining Silencing render (the ring is warm and
    the pool has free slots, so an accepted trigger births). An over-broad guard gives
    `Δ getTotalTriggeredGrainsBorn() == 0` with `getDroppedTriggerCount()` also unmoved — which is why
    the birth, not the absence of a drop, is the assertion with teeth.

**SC-006 `AtmosphereGhost_AppendOnly` — `atmosphere_ghost_test.cpp` plus tool and suite runs.**

The TU case shells nothing; it asserts the compile-time half (the `static_assert`s on `kReverseSalt`
disjointness and on the unchanged `kMaxGrains` / `kMinAgeSamples` / `kControlChunkSamples`) and prints
the clause list. Clauses 1, 2, 5 and 6 are discharged by `node tools/check-seraphis-green.js`
(extended, §4.3) plus the quoted `git diff HEAD -U0` excerpt in the compliance record, and clauses 3–4
by the suite runs in §4.4. Clause 6's two anchors are satisfied by construction, and **both are stated
as token counts over the quoted `-U0` excerpt**: at `renderGrainSpan` the `grain.reversed` test sits
above both loops with **zero** `reversed` occurrences between each loop's `for` and its closing brace
(§S3); at pass A `pendingTriggers_` appears in the per-sample body **exactly once**, as the right
operand of a short-circuited `&&` whose left operand is the loop-invariant `anyPending` declared above
the `for` (§S4's restatement of the clause). Clause 2's deletion anchors are the **six** of §S7, anchor (ii) covering `:1876-1882`, `:1888-1891` and
`:1917-1930`, anchor (v) covering `:436-441` and anchor (vi) covering the two falsified comments at
`:1183` and `:2595-2596`.

**SC-007 `AtmosphereGhost_Determinism` — `atmosphere_ghost_test.cpp` (a)–(c); `..._longrun_test.cpp`
(d) `[long]`.**

- (a) two engines at the same seed, configuration and trigger schedule:
  `compareFingerprints(...).withinTolerance()` at the **default** tolerances, at probability 0, 0.5 and
  1 (a same-binary comparison, so the shared constants are the right bounds — the measured-bounds
  protocol is only for *stored* references).
- (b) different seeds at probability 0.5: `worstMetricRelativeError > 100 * kMetricTolerance` (the
  Phase-10 SC-026 separation form).
- (c) three parts.
  - `setSeed(s)` mid-render re-seeds `reverseRng_`, observed via `getReverseRngState()`; and
    `getReverseRngState()` after `reset()` equals its value after `prepare()` at the same seed.
  - **FR-006's unconditional draw, asserted (the clause that was asserted nowhere).** FR-006's
    load-bearing sentence is that the `reverseRng_` draw is taken *unconditionally*, so the stream
    position never depends on the control value; §S5 adds `getReverseRngState()` explicitly so it is
    observable, yet the reviewed draft never compared it across probabilities — SC-001 clause 2's
    probability-0/probability-1 equality is on `getGrainRngState()`, a **different** stream. An
    implementation that guarded the draw (`if (reverseProbability_ > 0.0f)`) would pass every criterion
    in §3 and `setGrainReverseProbability` would silently stop being a pure gain on a fixed stream.
    Assertion, using the **replica** technique SC-001 clause 2's trigger arm already uses for
    `grainRng_` rather than a bare cross-run equality: a test-held
    `Xorshift32 replica{deriveStreamSeed(seed, AtmosphereEngine::kReverseSalt)}` advanced **exactly one
    `nextUnipolar()` per observed birth ATTEMPT** on a warm-ring fixture — where attempts equal
    `Δ getTotalGrainsBorn()`, proven in the same case by `Δ getSkippedTriggerCountRingCold() == 0` and
    `Δ getSkippedTriggerCountPoolFull() == 0` — then `REQUIRE(replica.state() ==
    engine.getReverseRngState())`, run at probability `0.0f` **and** `1.0f` with the identical fixed
    render. The reviewed draft's weaker form (state equal between the two probability runs and
    different from `deriveStreamSeed(seed, kReverseSalt)`) catches a guarded draw and a wholly absent
    draw, but **not** FR-006's "exactly one": two draws per birth, or a draw taken at a different point
    in the birth sequence, produce identical state between the two runs and a value different from the
    seed, so both pass it. The replica pins the count and the position. The two weaker equalities are
    retained alongside it, since they cost nothing and localise a failure.
  - **FR-008's mid-life snapshot is asserted in SC-002 (d), not here.** The draft's observation — "an
    unchanged `getTotalReverseGrainsBorn()` across the remainder of the live grains' spans" — measures
    a *birth* counter, which is unchanged whenever no grain is born and says nothing about a live
    grain's direction. It is retained only as a weak consistency check (no birth ⇒ no counter move),
    never as FR-008's evidence.
- (d) 100 000-grain accelerated run at `p` in {0.25, 0.5, 0.75}:
  `|getTotalReverseGrainsBorn() / getTotalGrainsBorn() - p| <= 0.02` (3σ for that count is ±0.005, so
  this is a calibration check, not a tight RNG test). Acceleration = `kMinGrainSeconds` grains at high
  density on a small warm ring.

**SC-008 — `atmosphere_ghost_test.cpp` (a)(c)(d)(e); `atmosphere_ghost_nonfinite_test.cpp` (b).**

- (a) `AtmosphereGhost_RtSafety`: `#include <allocation_detector.h>` **only** — never
  `<allocation_operator_overrides.h>`. The single owner of the global `operator new`/`delete`
  replacements in `dsp_systems_tests` is `unit/systems/selectable_oscillator_test.cpp:388`
  (`tests/test_helpers/vorago_fixtures.h:27-32`); a second include anywhere in the image is a
  duplicate-symbol link error. `[[maybe_unused]] const TestHelpers::AllocationScope scope;` around
  every render, at probability 0/0.5/1 with and without triggers, read **inside** the open scope
  (`vorago_engine_test.cpp:1705-1710`'s idiom).
- (b) `AtmosphereGhost_NonFiniteSetter` (the `-fno-fast-math` TU): NaN and ±Inf built from bit patterns
  through a `volatile std::uint32_t`. `setGrainReverseProbability(NaN)` after a `1.0f` write reads back
  **`0.0f`** — substitution, not retention (FR-003, Q2); `-1.0f` clamps to `0.0f` and `+2.0f` to
  `1.0f`, with the getter reporting the clamp.
- (c) `triggerGrain()` before `prepare()` is a no-op that moves no counter
  (`getDroppedTriggerCount() == 0`, `getTotalTriggeredGrainsBorn() == 0`).
- (d) `prepare()` at 44 100 / 48 000 / 96 000 / 192 000 Hz re-seeds `reverseRng_`
  (`getReverseRngState()` equal across rates at the same seed) and a reverse render at each rate is
  NaN/Inf-free and bounded by `kMaxLevel`. **Plus the queue-clearing assertion the reviewed draft
  claimed but never wrote** — it stated `prepare()` "clears the pending queue" and then listed only the
  re-seed and NaN/Inf checks, so FR-024's `prepare()` half was untested. Same shape as SC-005 (d)'s
  corrected `reset()` arm, with `prepare()` in its place, and **asserted on consumption rather than on
  births for the same reason**: `prepare()` rebuilds the capture ring, so a post-`prepare()`
  `Δ getTotalGrainsBorn() == 0` holds whether or not the queue was cleared. On a non-latched,
  cold-ring engine fire `kMaxGrains + 10 = 74` calls, REQUIRE `getDroppedTriggerCount() == 10`, call
  `prepare()` at the next rate, render **>= 64 samples**, and REQUIRE
  `getSkippedTriggerCountRingCold() == schedulerTicks` (an uncleared queue gives
  `64 + schedulerTicks`; `prepare()` ends with `reset()` at `:527`, which zeroes the counter at
  `:628`), `getTotalGrainsBorn() == 0` and `getDroppedTriggerCount() == 0`.
- (e) partition invariance: the same reverse render driven in blocks of 512, 64, 37 and 1 compares
  within the **default** `render_fingerprint.h` tolerances — the property `:2002-2012` and
  `rolling_capture_buffer.h:243-255` exist to protect, now exercised on the backwards path.

**SC-009 `AtmosphereGhost_CpuDelta` `[.perf]` — `atmosphere_ghost_perf_test.cpp`.**

Shape pinned to the **stage probe**, not the gating arm: `kStageWarmupBlocks = 300`,
`kStageTrials = 12`, `kStageBlocksPerTrial = 200` (`vorago_perf_test.cpp:300-302`), each 512-sample
block driven as **8 × 64-sample** `processStereoBlock` calls (`:1264-1277`), 48 kHz, subject built
exactly as `buildAtmosphere()` builds it (`:609-630`): `captureSeconds = 20`, FR-017's seven values and
`setLevel(VoragoEngine::kGhostBurstPeak = 0.60f)`. The TU reimplements `bestNsPerBlock` /
`warmThenMeasure` locally (~20 lines, copied with a citation) because they are file-local in
`vorago_perf_test.cpp:330-353` and exporting them would modify a file FR-047 pins.

| Arm | Runs | REQUIRE (in-run) | WARN (recorded) |
|---|---|---|---|
| 1 inert | p = 0, no triggers | — (it *is* the reference) | 31 114 = 28 285.5 × 1.10 |
| 2 reverse | p = 1.0, no triggers | `<= arm1 × 1.10` | 31 114 |
| 3 triggers, realistic | one `triggerGrain()` per 8.33 s | `<= arm1 × 1.50` | 42 428 |
| 4 triggers, stress | one per 0.833 s | `<= arm1 × 2.50` | 70 714 |
| **5 triggers, saturated drain** | `setGrainSeconds(kMinGrainSeconds)`; `kMaxGrains` `triggerGrain()` calls issued per 512-sample stage block, so the queue is at the FR-019 cap at every chunk boundary | `<= arm1 × kSaturatedDrainFactor` | the measured figure |

**Arm 5 exists because arms 2–4 never measure the configuration the trigger API permits.** FR-019 caps
`pendingTriggers_` at `kMaxGrains = 64` and FR-020 drains it at one per sample, so pass A can run
**two** `tryBirthGrain()` calls per sample — 48 000 extra birth attempts per second at 48 kHz,
sustained. Arm 4's one trigger per 0.833 s is 1.2/s, roughly four orders of magnitude below that, so
arms 2–4 gate a path their own configuration leaves effectively idle. Each extra attempt is not free:
the round-robin slot sweep is up to 64 integer tests (`:1601-1608`), and an attempt that clears the
sweep also takes four `grainRng_` draws plus the fifth reverse draw, two `ratioAtPitch` calls
(`semitonesToRatio`, `:1536-1538`) and `std::round`/`std::floor`/`std::ceil` on doubles (`:1665`,
`:1692`, `:1700`, `:1705`, `:1737`) **before** it can be rejected at `:1740`. SC-005 (b) already drives
exactly this configuration (74 calls, queue saturated at 64, drained over a 128-sample render) but
carries only accounting assertions; without arm 5 nothing in the phase bounds the drain path's cost.
`kSaturatedDrainFactor` is taken from the **first** clean measurement (alone, cooled, P-core-pinned)
plus the documented 0.6–7.4 % drift headroom, transcribed into the TU as a `constexpr`, recorded in the
compliance table, and thereafter **frozen — never widened to make a later run pass**. Arm comment to
carry: Vorago itself issues at most one trigger per control chunk
(`vorago_engine.h:887-889` calls `runPreRenderControlStep()` only at `phase == 0`), so arm 5 bounds the
shared component's **public contract**, not the Vorago operating point, which arms 2–3 cover.

Clause 5 is **computed and printed, never REQUIREd**: with `Δ` = arm 3 (and separately arm 4) minus
arm 1, print `ceil((kEngineMeasuredNsAtPoly4 + Δ) × 1.05) + kCavernMeasuredNsPerBlock <= kReferenceNs`,
i.e. `Δ <= 362 880 ns/block` — dominated by arm 4's own bound (`~= 42 429`, about 8.6× tighter). The
four constants come from the new `vorago_perf_budget.h` (FR-047) and none is edited.
Protocol: `node tools/run-cpu-tests.js dsp_systems_tests`, **alone**, after a cool-down, P-core pinned —
`vorago_perf_test.cpp:236-247`, whose own documented drift is 0.6 %–7.4 %, which is exactly why arms
2–4 gate against arm 1 measured *in the same run* and the absolute figures only WARN.

**SC-010 `VoragoEngine_GhostExtensionWiring` — `vorago_ghost_ext_test.cpp`.**

The accessor is `engine.atmosphere()` —
`[[nodiscard]] const AtmosphereEngine& atmosphere() const noexcept` (`vorago_engine.h:1066`); there is
**no** `VoragoEngine::atmos()` (`atmos_` is the private member at `:1498`).

- (a) `VoragoEngineConfig` at its defaults plus one held note-on (B-2, 2026-09-23: `setSeed(0x6057u)`,
  `setPolyphony(1u)`, `noteOn(33u, 100u)` at sample 0 — the no-note-on render is silence), 60 s Vorago render (built with
  `TestUtils::Vorago::makeEngine` / `renderEngine`, `vorago_fixtures.h:734`, `:750`) compared against a
  transcribed base-commit `kBaseCommitVoragoFingerprint` under the same FR-046 measured-bounds protocol
  as SC-001 clause 1; plus `engine.atmosphere().getTotalTriggeredGrainsBorn() == 0` and
  `engine.atmosphere().getGrainReverseProbability() == 0.0f`.
- (b) engaged: Phase 10's `makeGhostEngine()` shape (`vorago_engine_test.cpp:2730-2756`) — 8 kHz,
  polyphony 1, seed `0x6057`, `setEventRateScale(10.0f)` (the `[0.1, 10]` ceiling,
  `vorago_voice.h:1353-1358`), **`setEcosystemDepth(0.0f)`** (the write without which the ecosystem
  term keeps the request continuously non-zero) — with `atmosGhostEventTriggers = true` and, **unlike**
  Phase 10's fixture, `atmosCaptureSeconds` left at the shipped `20.0f` (`vorago_engine.h:120`; Phase 10
  used `1.0f` at `:2742`) so grains can actually be admitted. **Full-ring pre-roll per §3.2** — at
  8 kHz / `captureSeconds = 20` that is `getCaptureCapacitySamples() = nextPowerOf2(160 000) =
  262 144` samples = **32.768 s**, so FR-050 is vacuous. (The reviewed draft's "25 s pre-roll (> the
  20 s ring at 8 kHz)" was **short by 7.8 s** and left the stated invariant unestablished; the ring is
  power-of-two rounded, `rolling_capture_buffer.h:75-93`.) Then snapshot every counter and render
  **600 s** driven in **64-sample** blocks so
  the observation grid equals the control grid and no edge is missed by aliasing. Edges counted with
  SC-027's two-state detector (`:2699-2704`) over `engine.atmosphere().getLevel()` at
  `kGhostRiseThreshold = 0.5 × kGhostBurstPeak` / `kGhostFallThreshold = 0.05 × kGhostBurstPeak`
  (`:2666-2667`). Assertions:
  `edges <= Δ getTotalTriggeredGrainsBorn() <= edges + 1` (the detector counts *completed* excursions
  while FR-031 fires on the rise, so a burst still in flight at the end is the whole of the `+1`);
  `Δ getDroppedTriggerCount() == 0`;
  `Δ getSkippedTriggerCountPoolFull() == Δ getSkippedTriggerCountRingCold() == 0` (so the band cannot
  be satisfied by rejections); and `Δ getTotalTriggeredGrainsBorn() >= 6` (SC-027's own floor,
  `:2819`).
  The clause is explicit that no independent edge source exists — `SlowEventScheduler::isEventActive()`
  (`slow_event_scheduler.h:361`) lives on `sched_`, private to `VoragoVoice` — so its teeth are the
  accounting assertions and the enumerated failure modes: level-polling gives `Δ >> edges`, a missing
  or mis-gated wire gives `Δ == 0`, a per-voice rather than per-fold latch gives `Δ > edges + 1`, a
  trigger consumed but never turned into a grain gives `Δ < edges` with a skip counter moving, and a
  latch that never re-arms gives `Δ == 1`.
- (c) `setGhostPeakLevel(0.0f)` (`vorago_engine_test.cpp:2793-2795`), everything else as (b):
  `getLevel()` holds its `0.0` base for the whole render and `getTotalTriggeredGrainsBorn() == 0` —
  because FR-031 latches on `ghostPeak_ * ghost`, identically zero there — while
  `maxGhostRequest >= kGhostRiseThreshold` (`:2828`) proves the events did fire, so the zero is the
  gate closing rather than a silent lane. Phase 10's own case is unchanged and unedited (FR-033).
- (d) arm (b)'s `getLevel()` still shows **>= 6** burst edges by the same detector: the spawn path did
  not replace the level gate.
- (e) **the reverse-probability wire, which no other clause ever moves off its default.** (a) asserts
  only `getGrainReverseProbability() == 0.0f` at config defaults, and (b)–(d) engage
  `atmosGhostEventTriggers` alone — so an implementation that omits
  `atmos_.setGrainReverseProbability(cfg.atmosGhostReverseProbability);` (§S6, the one line FR-030 says
  the FR-017 block gains, and half of FR-034) passes every other criterion in the phase, because the
  component default is already `0.0f`. Half the Vorago wiring this phase ships would be unfalsifiable.
  Arm: arm (b)'s fixture with `atmosGhostEventTriggers = false` and
  `atmosGhostReverseProbability = 1.0f`; after `prepare()` REQUIRE
  `engine.atmosphere().getGrainReverseProbability() == 1.0f`; then the **full-ring pre-roll per §3.2**
  and a further render long enough for several density-scheduler births, and REQUIRE
  `engine.atmosphere().getTotalGrainsBorn() > 0` and
  `engine.atmosphere().getTotalReverseGrainsBorn() == engine.atmosphere().getTotalGrainsBorn()` (at
  probability 1 the only forward outcome is the single exact draw `1.0f`, expected rate 2^-32, §S2 (a)).
  A zero birth count is a fixture defect to fix, never a reason to weaken the arm. Recorded in §7
  against FR-030 and FR-034.

**SC-011 `AtmosphereGhost_ReverseFillDeficit` — `atmosphere_ghost_test.cpp`, untagged, cheap (NEW, the
binding criteria for FR-050: arms (a), (a2) and (c)).**

Every other reverse fixture pre-rolls to a full ring, which sets FR-050's `t* = 0` and makes the clause
a no-op — so a wrong-signed, wrong-termed or entirely **absent** FR-050 passes all of them (§0 P-1,
§A-5). This is the one case that renders the filling-ring regime. It takes SC-003's Vorago ghost
configuration (probability 1, `density = 0.30`, `grainSeconds = 12`, `captureSeconds = 20`,
`pitchSemitones = -12`, `positionSpread = 0.9`, `decorrelation = 0.85`, 48 kHz, seed 1, so
`C = getCaptureCapacitySamples() = 1 048 576` and `L' = 576 000`), driven in 64-sample blocks, **with
one declared deviation: `pitchSpread = 0` and `driftRangeSemitones = 0`.** That pins
`ratioMax == ratioMin == 2^(-12/12) = 0.5` exactly, so every threshold below is computable *by the
test* from public accessors; with the Vorago point's drift range of 2 the per-grain `ratioMax` is a
draw with no getter, and a test forced to use the configured upper bound would assert a threshold
larger than the one FR-050 actually applied — red on correct code. The deviation strengthens the case
and changes nothing FR-050 does: the clause is evaluated identically, only with a known `rMax`.

With `rMax = 0.5`: the deficit on the filling branch is `ceil(0.5 * 576 000) = 288 000`, the
minimum-birth-age admission threshold is `kAdmitThresholdSamples = 128 + 288 000 = 288 128` samples
(6.003 s), and the branch crossover is `C - L' = 472 576`, so `t* = L'` throughout the region this case
renders. The drawn birth age lies in the `positionSeconds = 1.0 ± 0.9` window, i.e.
`[4 800, 91 200]` samples, so the *actual* first admission falls between `A = 292 864` (6.10 s) and
`A = 380 488` (7.93 s). Every one of those numbers is recomputed in the test, never transcribed.

- **(a) Rejection arm — FR-050 doing its job, measured ONLY over the span in which rejection is
  provable.** The reviewed draft pre-rolled 5 s and then measured across the **whole 12 s** grain life.
  That is red on correct code: `available` grows one sample per rendered sample, so the rejecting
  regime ends when it reaches `kAdmitThresholdSamples` — 48 128 samples (1.003 s) into the measured
  span — and the scheduler (interonset `48 000 / 0.30 = 160 000` samples, scaled by `1 ± 0.25` at
  `jitter = 0.5`, `grain_scheduler.h:78-88`, interonset at `:102`) lands most of its remaining ticks in
  the **admitting** regime, where they are admitted. `Δ getTotalGrainsBorn() > 0` and the ring-cold
  equality would both fail. The span is therefore bounded:

  Pre-roll **5 s** (240 000 samples), snapshot `born0`, `cold0 = getSkippedTriggerCountRingCold()` and
  `poolFull0 = getSkippedTriggerCountPoolFull()`, fire **one** `triggerGrain()`, and render
  `kRejectSpan = 40 000` samples (0.833 s) in 64-sample blocks, ending at `A = 280 000` — 8 128 samples
  clear of the threshold. The test computes `kAdmitThresholdSamples` itself as
  `kMinAgeSamples + kMinAgeSamples + ceil(rMax * min(L', C - A))` and REQUIREs
  `240 000 + kRejectSpan < kAdmitThresholdSamples`, so the arm cannot silently drift into the admitting
  regime if a constant moves. Assertions over that span:
  - `Δ getTotalGrainsBorn() == 0` — the teeth;
  - `Δ getSkippedTriggerCountPoolFull() == 0` — nothing was rejected for the wrong reason;
  - `1 <= Δ getSkippedTriggerCountRingCold() <= 2` — the fired trigger **was** attempted and rejected,
    plus at most one scheduler tick, because the shortest interonset `jitter = 0.5` permits is
    `160 000 * 0.75 = 120 000` samples and `kRejectSpan = 40 000 < 120 000`. **Attempts are bounded,
    never predicted**: `samplesUntilNextGrain_` is a random draw (`grain_scheduler.h:80-84`) so the
    tick count is not exactly computable, which is why SC-003 (a) uses `<=` too. The reviewed draft's
    `== attempts` equality against a jitter-derived tick formula was not satisfiable.

  **With the FR-050 block commented out the trigger's birth is admitted at once** — the shipped test
  needs only `needed <= 92 488` against `A = 240 000` — so `Δ getTotalGrainsBorn() >= 1` and the arm is
  **red**. That differential is the check that the phase's added requirement is exercised, and §6
  step 4 records both runs.
- **(a2) Admission arm — the threshold IS reached, and not before.** Continue the same fixture past the
  rejecting span to the end of the 12 s grain life (17 s of render in total, 816 000 samples), reading
  the counters at every 64-sample boundary. At the first boundary on which `getTotalGrainsBorn()`
  advances, record `A_born = min(samplesRendered, getCaptureCapacitySamples())` and
  `birthAge = getLastBornGrainBirthAgeSamples()` (`:1105-1106`), and REQUIRE

  ```
  A_born >= std::ceil(birthAge) + kMinAgeSamples
            + std::ceil(rMax * std::min(double(L'), double(C) - A_born))
  ```

  with `L' = getLastBornGrainLifetimeSamples()` (`:1112-1114`). This is a **necessary** condition for
  FR-050 to have admitted that grain: the drawn `decorrAge >= 0` is not observable, so the test uses
  the decorr-free lower bound on `needed` (the `min(birthAge + decorr, C - 2 - guard)` clip at `:1736`
  cannot bind here — `birthAge <= 91 200` against `C - 2 - guard = 1 048 510`), and `A_born` is read at
  the block end so it is never smaller than the `available` the admission actually saw. Without FR-050
  the first birth lands at `A ≈ 240 064` against a right-hand side of at least `292 864`, so this arm
  is red too — and it proves the rejection arm is not green merely because the configuration never
  births at all.
- **(b) — DELETED. The stale-read bound cannot be asserted on a green path, and that is a theorem, not
  a fixture problem.** The reviewed draft's (b) pre-rolled 10 s, REQUIREd `Δ getTotalGrainsBorn() >= 1`
  and then bounded `getMaxObservedGrainAgeSamples()` by `available - 2` at every block boundary. It
  cannot fail, for two independent reasons.
  1. **Admitted implies safe.** FR-050 admits iff `A >= needed + ceil(rMax * t*)`,
     `t* = min(L', C - A)`. On the filling branch the read age is
     `a(t) = birthAge + decorr + (1 + rMax)*t` and `available(t) = A + t`, so `a(t) > available(t) - 2`
     reduces to `needed_exact + rMax*t > A` with `needed_exact = birthAge + decorr + 2` — worst at
     `t = t*`, since for `t > t*` the ring is saturated and step (c)'s `ageHi`
     window (`wDown = 1 + rMax`) already bounds the whole remaining life (§A-2). So the crossing
     condition is `A < needed_exact + rMax*t*`. Since `needed >= needed_exact` and `ceil(x) >= x`,
     **admission implies the crossing condition is false**. Any clause that requires a birth first can
     therefore only ever observe grains that provably satisfy the bound: FR-050 present, wrong-signed
     or wrong-termed *in the conservative direction* all pass it, and FR-050 absent passes it too at
     the chosen pre-roll (below).
  2. **Numerically inert at the chosen pre-roll.** At 10 s (`A = 480 000`) the threshold for the
     largest birth age this fixture can draw (`<= 91 200` samples, plus
     `0.85 * kMaxDecorrelationMs (30.0f, :315) * 0.001 * 48 000 = 1 224` and the 64-sample guard, i.e.
     `needed <= 92 488`) is `92 488 + ceil(0.5 * (1 048 576 - 480 000)) = 376 776` — already passed.
     FR-050 rejects nothing there, and with the clause deleted no read goes stale there either.

  Moving the pre-roll into the band does not rescue it: inside the band FR-050 rejects **every** reverse
  birth, so `Δ getTotalGrainsBorn() >= 1` fails on correct code. There is no pre-roll at which (b) is
  both non-vacuous and green, and the plan's earlier claim (R-1, §A-5, §8.1) that (b) asserts "the
  quantity FR-050 actually protects" was false.

  **The measurement survives as a clause-disabled one**, folded into the differential protocol §6
  step 4 already mandates for (a): with the FR-050 block commented out, run the **5 s pre-roll**
  fixture to the end of the grain life and record that `getMaxObservedGrainAgeSamples()` exceeds
  `min(samplesRendered, getCaptureCapacitySamples()) - 2.0` at some block boundary. P-1's arithmetic
  puts the crossing at `t > 2 * (239 998 - birthAge - decorr)` grain-samples: ≈ 479 870 for a birth at
  the `kMinAgeSamples` clamp and ≈ 297 600 … 470 400 for the `[4 800, 91 200]` ages this fixture
  actually draws — all inside the 576 000-sample life. **Both outcomes, clause in and clause out, go in
  the compliance table**, exactly as step 4 already requires for (a). No green-path fixture can bound
  the stale read; only the clause-disabled run can, and the plan says so rather than shipping an
  assertion that cannot fail.
- **(c) Forward arm unaffected — "never evaluated for a forward grain", pinned.** Repeat (a)'s fixture
  at probability **0** and REQUIRE `getTotalGrainsBorn()`, `getTotalGrainsRetired()`,
  `getSkippedTriggerCountRingCold()`, `getSkippedTriggerCountPoolFull()` and `getGrainRngState()` equal
  their **transcribed base-commit values at this short pre-roll**, under SC-001's provenance rule
  (integers, so no tolerance is involved). FR-050 is reverse-only and a forward render at a *filling*
  ring is exactly where a misplaced clause would show up; SC-001 clause 3 only pins the full-ring case.

**SC-012 `AtmosphereGhost_PassAScratchBound` — `atmosphere_ghost_test.cpp`, untagged (NEW, the binding
criterion for FR-051).**

FR-051 raises the pass-A scratch sizing from `kMaxGrains * 2` to `kMaxGrains * 3` because §S4 adds a
second birth site per sample. Without a criterion FR-051 could be omitted entirely and every gate stays
green, and **`AllocationScope` cannot supply one**: writing past `retiredScratch_`/`dueScratch_`'s size
performs no allocation. So this case pairs a portable assertion with a sanitizer run.

Fixture, exactly P-5's corrected reaching configuration: `sampleRate = 20`, `density = kMaxDensity =
20.0f` (`:304`) so the scheduler's interonset is exactly `1.0` and it fires on **every** sample
(`grain_scheduler.h:74-76`), `captureSeconds = 30` so `C = nextPowerOf2(600) = 1024`, blur and freeze
off, reverse probability 0 (direction is irrelevant to the sizing), ring pre-rolled full per §3.2
(1 024 samples). Then, with `kMaxGrains` `triggerGrain()` calls issued before each block so the FR-019
queue is at its cap at every chunk boundary:

- **chunk 1** at `setGrainSeconds(3.2f)` (`L = round(3.2 * 20) = 64`): render exactly 64 samples. Two
  births per sample fill the pool; REQUIRE `getActiveGrainCount() == kMaxGrains` afterwards, and
  REQUIRE `Δ getTotalGrainsRetired() <= 1` (only a birth at `i = 0` can be due in this chunk).
- **chunk 2** at `setGrainSeconds(0.1f)` (`L = 2`; `kMinGrainSeconds * 20 = 1` would be rejected by the
  `lifetime < 2` test at `:1696-1699`): render exactly 64 more samples and REQUIRE

  ```
  Δ getTotalGrainsRetired() > kMaxGrains * 2
  ```

  which is the assertion with teeth. Every retirement inside a chunk is one `retiredScratch_` write and
  one consumed `dueScratch_` entry, and `dueCount` never decreases within a chunk (`:2041-2053`,
  `:2119-2134`, drain at `:2136-2141`), so a delta above 128 **is** a write past the old `kMaxGrains *
  2` sizing. Expected ≈ 186; the REQUIRE is the structural `> 128`, not the measured figure. Also
  REQUIRE `Δ getTotalGrainsRetired() <= kMaxGrains * 3` — the FR-051 bound itself, so a derivation
  error in the other direction is caught rather than absorbed.
- **sanitizer half**: the same case, run from the ASan build (§4.4). At `kMaxGrains * 2` the chunk-2
  write is a heap-buffer-overflow and ASan aborts; at `kMaxGrains * 3` it is in bounds. §6 step 6
  requires **both** outcomes to be recorded in the compliance table, exactly as step 4 does for FR-050.
- `[[maybe_unused]] const TestHelpers::AllocationScope scope;` still wraps the two renders, so the
  FR-051 sizing is also shown not to have moved any allocation onto the audio thread — the one thing
  `AllocationScope` *can* prove here, and the comment says so rather than implying it detects the
  overflow.

Cheap: 1 152 rendered samples at a 20 Hz rate. Measured runtime recorded like every other case.

### 3.3 Cross-criterion conventions

- **No bit-exact float goldens** anywhere (roadmap line 605). Every render comparison is
  `render_fingerprint.h` with either the shared tolerances (same-binary comparisons) or documented
  measured per-comparison bounds (stored base-commit references).
- **No `std::isnan` / `std::isinf` / `std::isfinite`** in the header or any new TU: the header uses its
  own `isFinite` (`:1269`), the tests use `volatile`-laundered bit patterns.
- **No `<random>`**: all excitation uses `Xorshift32` so the sequence is identical on every toolchain.
- **No narrowing in brace init**; designated initialisers for every `PrepareConfig`.
- Every new case's **measured runtime is recorded in the compliance table**, tagged or not (FR-048).

---

## 4. Build integration

### 4.1 `dsp/tests/CMakeLists.txt` — the enumerated list (an unlisted TU silently never runs)

Add to the `dsp_systems_tests` source list, in a new commented block after the Phase 10 Vorago block
(`:492-516`):

```cmake
# Vorago Phase 10a (specs/vorago-phase10a-ghost-extension): the AtmosphereEngine
# ghost extension (reverse grains + triggerGrain). ENUMERATED, not globbed.
unit/systems/atmosphere_ghost_test.cpp
unit/systems/atmosphere_ghost_longrun_test.cpp
unit/systems/atmosphere_ghost_nonfinite_test.cpp
unit/systems/atmosphere_ghost_perf_test.cpp
unit/systems/vorago_ghost_ext_test.cpp
```

Add to the `-fno-fast-math` opt-in list (the block at `:881-896`, beside
`unit/systems/atmosphere_engine_nonfinite_test.cpp` at `:896`) **exactly one** entry:

```cmake
# Vorago Phase 10a: SC-008 (b) injects NaN/Inf via bit patterns in this TU.
# ONLY this one of the five Phase 10a TUs is listed - the perf TU in particular
# must stay out, or -fno-fast-math would move the figures it measures.
unit/systems/atmosphere_ghost_nonfinite_test.cpp
```

`vorago_perf_budget.h` is a header in the same directory and needs no CMake entry.
`tests/test_helpers/` is an INTERFACE target exposing the whole directory
(`tests/test_helpers/CMakeLists.txt:7-12`), so `<render_fingerprint.h>`, `<allocation_detector.h>` and
`vorago_fixtures.h` are reachable with no CMake edit.

### 4.2 `vorago_perf_test.cpp` and the new budget header (FR-047, Q7)

`dsp/tests/unit/systems/vorago_perf_budget.h` (new, test-only) holds the Phase 10 constants **moved
unedited**, each with its provenance comment: `kEngineMeasuredNsAtPoly4` (`vorago_perf_test.cpp:256`),
`kEngineBaselineNsAtPoly4` (`:257`), `kCavernMeasuredNsPerBlock` (`:215`) and `kReferenceNs` (`:168`).
No new value, no rename, no new `static_assert`. `vorago_perf_test.cpp`'s **only** permitted
modification in the whole phase is replacing those definitions with
`#include "vorago_perf_budget.h"`; its own `static_assert`s (`:263-266`, `:269`, `:272-274`) stay in
place unedited, and `kBaselineWithCavernNs`, `kAvailableRegressionHeadroom`,
`kCavernBaselineNsPerBlock`, `kRegressionFactor` and `kMaxAdmissibleNs` stay in the TU.

*Implementation note and open item (§8.3):* `kReferenceNs` is **derived** — `kBlockBudgetNs * 0.30`
(`:160`, `:168`) with `kBlockBudgetNs = (kBlockSize / kSr48) * 1e9` — so the header must carry
`kSr48`, `kBlockSize` and `kBlockBudgetNs` with it. That is still "replacing constant definitions with
the include", but it moves seven names rather than four, and the compliance record states it
explicitly rather than letting the FR-047 wording drift.

### 4.3 `tools/check-seraphis-green.js` (SC-006 clauses 1–2)

Two additive edits to the existing rule tables:

1. `APPEND_ONLY_HEADERS` (`:102-117`) gains an `atmosphere_engine.h` entry in the shipped
   `{ file, what, expected: [{ count, name, pattern }] }` shape:

   ```js
   {
     file: 'dsp/include/krate/dsp/systems/atmosphere_engine.h',
     what: 'the Phase 10a ghost extension (reverse grains + triggerGrain)',
     expected: [
       { count: 2, name: 'the wUp/wDown rate lines',
         pattern: /const double w(Up|Down) = std::max\(/ },
       { count: /* measured at implementation, then frozen */ 0,
         name: 'the advance lambda AND the two renderGrainSpan loops that call it',
         pattern: /* anchored on the amended FR-045 site (ii) - and ONLY those three
                     ranges: `:1876-1882` (`const auto advance`, `readFrac +=`,
                     `carryInt`), `:1888-1891` (the cold-path `for` + `foldAt` +
                     `advance();`) and `:1917-1930` (the scalar `for`, `ageAt(i)`,
                     `newerOffset`, `reader.indexAt`). A deletion anywhere else in
                     renderGrainSpan matches nothing and fails the gate. */ },
       { count: /* measured, then frozen */ 0,
         name: 'the scheduler-tick block lines',
         pattern: /* anchored on `scheduler_\.process`, `before = activeCount_`, `tryBirthGrain`, `bornAt\[slot\]` */ },
       { count: 2, name: 'the reset()/setSeed() blur-seed lines',
         pattern: /blurRng_\.seed\(deriveStreamSeed\(/ },
       // FR-051 / P-5: prepare() step 5b's sizing, the FIFTH sanctioned anchor.
       { count: 2, name: 'the pass-A scratch assign lines',
         pattern: /(retired|due)Scratch_\.assign\(kMaxGrains \* 2,/ },
       // Anchor (vi): the TWO shipped comments this phase falsifies. Without
       // these patterns the gate FORBIDS correcting them and the header ships a
       // wrong real-time contract (see S7).
       { count: 1, name: "the pass-A scratch declaration comment (:2595-2596)",
         pattern: /Sized once in prepare\(\) \(2 \* kMaxGrains each\)/ },
       { count: 1, name: "AtmosphereGrain::readFrac's range comment (:1183)",
         pattern: /fraction in \[0,1\)/ },
     ],
   }
   ```

   The two `count`s marked *measured* are filled from the real diff at implementation time and then
   frozen; the patterns are written so a deletion anywhere else in the header cannot match any of them.
   The **per-anchor measured deletion counts** (all six) go in the compliance table, beside the quoted
   `git diff HEAD -U0` excerpt for each.
   The script inherits its own `git diff HEAD` range (`:77`, `:152-155`, `:171-177`), so the
   requirement and the tool cannot drift apart.
2. `ALLOWED_SYSTEMS_TUS` (`:129-134`) gains `'vorago_perf_test.cpp'` as **the single named exception**
   to check 1's zero-modified-files bar (FR-047 / Q7), with a comment naming the one permitted
   modification. Check 1's conditional "all four must be present" clause (`:27-34`, implemented at
   `:253-262`) is keyed on the `continuous_body.h` `widened` result and is unaffected.
   `ALLOWED_SERAPHIS_PLUGIN_FILES` (`:122-125`) is **not** touched: zero modified files under
   `plugins/seraphis/` stands.

### 4.4 Commands (Windows; always the full CMake path)

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "AtmosphereGhost_*" 2>&1 | tail -20
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "VoragoEngine_GhostExtensionWiring*" 2>&1 | tail -20
# the regression gate on the shipped component and its consumers
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "AtmosphereEngine_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "Seraphis*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "Vorago*" 2>&1 | tail -5
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_effects_tests dsp_processors_tests seraphis_tests Seraphis
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"
node tools/check-seraphis-green.js
node tools/lint-layers.js
node tools/lint-odr.js
node tools/check-portability.js
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
# SC-012 (FR-051) ONLY: a 190-entry write into a 128-entry std::vector allocates
# nothing, so AllocationScope cannot see it - ASan is the ONLY detector, and it
# is scheduled here rather than assumed. Run it twice: once with the sizing left
# at kMaxGrains * 2 (must ABORT with a heap-buffer-overflow) and once at * 3
# (must pass). Both outcomes go in the compliance table.
"$CMAKE" -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON
"$CMAKE" --build build-asan --config Debug --target dsp_systems_tests
build-asan/bin/Debug/dsp_systems_tests.exe "AtmosphereGhost_PassAScratchBound" 2>&1 | tail -30
# SC-009 LAST, alone, after a cool-down
node tools/run-cpu-tests.js dsp_systems_tests
```

ODR sweep before any new name (roadmap line 594), re-run at implementation time:

```bash
grep -rn "setGrainReverseProbability\|getGrainReverseProbability\|getLastBornGrainReversed\|\
getTotalReverseGrainsBorn\|getReverseRngState\|getTotalTriggeredGrainsBorn\|getDroppedTriggerCount\|\
kReverseSalt\|ghostTriggerHigh_\|atmosGhost\|vorago_perf_budget" dsp/ plugins/ tools/
```

The spec's *New components* table records 0 hits for each at spec time; `triggerGrain` has 5
pre-existing hits, all private members of `processors/granular_distortion.h` and
`processors/formant_oscillator.h` — unrelated classes, no collision.

---

## 5. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | **A reverse grain outruns the ring while it fills** and reads a clamped, stuck position (P-1). Silent: no NaN, no counter moves, and the age folds still look legal. | FR-050's additive admission clause makes it structurally impossible. Its binding criteria are **SC-011 (a)** (rejection inside the provably-rejecting span, red with the clause commented out) and **(a2)** (the first admission arrives no earlier than the threshold the test recomputes). **No green-path fixture can bound the stale read, and this is a theorem, not a gap**: admission implies `A >= needed + ceil(rMax*t*)`, which is exactly the negation of the crossing condition, so any clause requiring a birth first can only observe grains that provably satisfy the bound — which is why the reviewed draft's SC-011 (b) was deleted and its measurement moved into §6 step 4's **clause-disabled** run. **SC-003 (c) is likewise NOT the mitigation**: it bounds the *computed* age by `capacity - 2`, which P-1 shows stays legal while the read is stale, and on the full ring every other fixture pre-rolls to, FR-050 is vacuous. |
| R-2 | **The backwards advance breaks the `readFrac` non-negativity invariant** at some `ratio`, corrupting the truncation-is-floor identity `LinearReader::index0` and the shipped `advance` banner (`:1871-1875`) rely on. | The five-case table in §S3, which states the invariant the arithmetic actually gives (`[0, 1]`, non-negative — **not** the `[0, 1)` the reviewed draft claimed, see the `-2.98e-8 → 1.0f` row) and records that `readFrac` is consumed only by the `ageAt` subtraction (`:1854-1857`), never as an interpolation weight. Plus **SC-002 (c)'s fractional-ratio age identity** (the only clause that exercises the ceil-correction branch at all), SC-004's extremes (`ratio = 8`) and SC-008 (e)'s partition invariance at four block sizes. Never `std::floor` — a CRT call on MSVC's default `/arch`. |
| R-13 | **Two births at the same sample overflow the pass-A scratch vectors** sized at `2 * kMaxGrains` from a one-birth-per-sample proof (P-5) — an unchecked out-of-bounds `std::vector` write on the audio thread, reachable whenever the density scheduler can fire every sample (`sampleRate <= kMaxDensity = 20`, `:304`). | FR-051 raises both to `kMaxGrains * 3` with the tight `190 <= 192` derivation restated in the code comment; §S7 anchors (v) and (vi) and §4.3's fifth and sixth `APPEND_ONLY_HEADERS` patterns make the change *and* the stale declaration comment visible to the gate; **SC-012** drives the corrected reaching configuration, asserting portably that one chunk retires more than `kMaxGrains * 2` grains and separately under ASan, where the old sizing is a heap-buffer-overflow. |
| R-14 | **`triggerGrain()` called off the audio thread** — a read-modify-write race on `pendingTriggers_`/`droppedTriggers_` that silently loses or double-counts triggers (P-6). | The contract is stated once, AUDIO-THREAD-ONLY, in §S4's doxygen and in the spec amendment FR-018 needs (§8); the only caller in this phase is `VoragoEngine::runPreRenderControlStep()`, which runs inside `processStereoBlock`. |
| R-3 | **A fifth `grainRng_` draw** would change every Seraphis render at any probability, including 0 (banner `:1613-1616`). | ADR-2's dedicated `reverseRng_` + `kReverseSalt`; SC-001 clause 2's **integer** state equality, which no tolerance can absorb. |
| R-4 | **The trigger predicate leaks into the per-sample loop.** No timing arm in this phase can resolve a per-sample `bool` test against a ~28 µs block with 0.6–7.4 % drift. | Structural gate: SC-006 clause 6's `git diff HEAD -U0` at two named anchors; §S4's short-circuited hoisted `anyPending`. |
| R-5 | **Templated-lambda portability.** MSVC-green proves nothing for the GCC/Clang legs. | `node tools/check-portability.js` plus a WSL GCC build of `dsp_systems_tests` before commit; documented fallback to a private member function template (§S3). |
| R-6 | **Denormals on the backwards path.** | `readFrac` is re-normalised into `[0, 1]` every sample by construction (§S3 — **not** `[0, 1)`: the ceil-correction can land on exactly `1.0f`) and is never accumulated toward 0; no new denormal site is introduced and no `flushDenormal` call is added. |
| R-7 | **`readIndexInt` unsigned wraparound** when a reverse grain walks past absolute index 0 early in a render. | Harmless by construction: the age is a modulo-2^64 subtraction cast to `std::int64_t` (`:1852-1857`), exact for the true difference (`< 2^22`). Exercised by SC-002's short ring and SC-003 (c). |
| R-8 | **`vorago_perf_test.cpp` drifts beyond the one permitted edit** while extracting the budget header. | `check-seraphis-green.js` names it as the single exception; the compliance record quotes its `--numstat` row and its `-U0` excerpt. |
| R-9 | **Stored base-commit fingerprints regenerated to turn a red arm green.** | FR-046's protocol: PROVENANCE block, paste-ready literal on failure, `static_assert`s that the per-comparison bounds are looser than the shared ones *and* that the shared ones are unedited, and the spec rule that a measured bound may not be widened without a ruling. |
| R-10 | **`[long]` mis-tagging** hides a per-push regression — and the reviewed draft did exactly that, authorising a conditional `[long]` on SC-004, the phase's only reverse-path NaN/Inf and `kMaxLevel` check at the extreme ratio corner. | FR-048's rule (> ~15 s **and** toolchain-independent), FR-049's short twin for SC-003, D-4's split for SC-010; **SC-004 removed from the `[long]` candidate list (§8.8) and split, never tagged, if it measures long**; NaN/Inf-guard, bounded-grid and state-format cases are never `[long]`, per `CLAUDE.md`'s Build Commands rule. |
| R-11 | **SC-005 (b)'s cold arm reaching `kMaxGrains` births** would contradict its own cold-ring assertion. | The two arms are separated exactly as the spec writes them: the cold arm asserts queue saturation plus `skipRingCold_ == 64`; the warm arm asserts the pool cap. |
| R-12 | **`AtmosphereGrain` growing a cache line** from the new `bool`. | The struct currently ends on one `bool` with padding, so the field is free in practice; if a size change appears, it costs cache only, never correctness, and SC-009 arm 1 records it. |

---

## 6. Implementation order (every step ends in a verifiable state)

1. `vorago_perf_budget.h` + the `vorago_perf_test.cpp` include swap. Verify: `dsp_systems_tests`
   builds, the perf TU's `static_assert`s still hold, `node tools/check-seraphis-green.js` clean after
   its `ALLOWED_SYSTEMS_TUS` edit.
2. S1 (control surface, salt, seeding, counters) + S5 (accessors). Verify: zero warnings;
   `AtmosphereEngine_*` and `Seraphis*` green, unchanged.
3. SC-001 clauses 2–3 and SC-008 (b)(c) written **first and red**, then S2 (a) and (d). Verify they
   pass and `getGrainRngState()` is unchanged at both probabilities.
4. S2 (b) direction-dependent rate terms + S2 (c) FR-050 clause, with SC-003 (a)/(c), SC-004 and
   **SC-011 written first**. The FR-050 differential is a **three-measurement protocol**, and all three
   results go in the compliance table:
   (i) SC-011 (a) **red with the FR-050 block commented out** (the trigger's birth is admitted at
   `A = 240 000`) and green with it in;
   (ii) SC-011 (a2) green with the clause in — the first admission is no earlier than the recomputed
   threshold — and red with it out;
   (iii) with the clause still commented out, the **clause-disabled stale-read measurement** that
   replaces the deleted SC-011 (b): run the 5 s-pre-roll fixture to the end of the grain life and
   record that `getMaxObservedGrainAgeSamples()` exceeds
   `min(samplesRendered, getCaptureCapacitySamples()) - 2.0` at some block boundary (predicted
   `t > 2 * (239 998 - birthAge - decorr)` grain-samples). This is the only way the protected quantity
   can be observed at all — with the clause in, admitted-implies-safe is a theorem (§A-5).
5. S3 backwards advance and direction hoist, with SC-002 written first. Verify SC-002 (a)(b), **(c)'s
   fractional-ratio age identity and (d)'s mid-life snapshot**, and SC-008 (e).
6. S4 `triggerGrain()` + pass-A consumption **and FR-051's `kMaxGrains * 3` sizing in the same step**
   (the sizing must land with, not after, the second birth site), with SC-005 and **SC-012 written
   first**. SC-012's portable half (`Δ getTotalGrainsRetired() > kMaxGrains * 2` across one 64-sample
   chunk) must pass, and its ASan half must be recorded **red at `kMaxGrains * 2` and green at
   `kMaxGrains * 3`** — the check that FR-051 is actually exercised — before anything else in this step
   is called done.
7. Base-commit worktree measurement: transcribe SC-001 clause 1 and SC-010 (a) fingerprints, run the
   three-toolchain probe, record the measured bounds back into the spec.
8. S6 Vorago wiring + SC-010.
9. `check-seraphis-green.js` `APPEND_ONLY_HEADERS` entry with the measured deletion counts; SC-006
   evidence captured (`git diff HEAD -U0` excerpts quoted, not paraphrased).
10. SC-009 last, **alone**, after a cool-down, P-core-pinned.
11. Gates: portability, layers, ODR, clang-tidy, `seraphis_tests` + `Seraphis` + pluginval; compliance
    table filled with real file:line citations and real measured numbers.

---

## 7. Traceability

| FR | Where it is implemented |
|---|---|
| FR-001–003 | §S1 setter/getter, the shipped `isFinite ? x : default` + clamp shape |
| FR-004–005 | §S1 `kReverseSalt` + `static_assert`; `reset()` `:555-556`, `setSeed()` `:1015-1017` (P-3) |
| FR-006–008 | §S2 (a), (d); `AtmosphereGrain::reversed` |
| FR-009 | §S5 (five accessors); the reverse/triggered counters are read by SC-002 (d), SC-007 (c)/(d) and SC-010 (e), and their  clearing by SC-005 (d)'s warm sub-arm |
| FR-010–013 | §S3 (reader reuse, borrow form, direction hoist) |
| FR-014–017 | §S2 (b); decorrelation / pan / drift / envelope untouched |
| FR-018–027 | §S4 |
| FR-030–034 | §S6; the reverse-probability wire is asserted by **SC-010 (e)**, the event-trigger wire by SC-010 (b)–(d) |
| FR-040–045 | §S7 diff budget, §4 build integration, §3.3 conventions |
| FR-046–049 | §3.2 (fingerprint protocol, twin case, tagging), §3.1, §4.2 |
| **FR-050 (new)** | §0 P-1, §S2 (c), §A-5; **asserted by SC-011 (a) and (a2)**, plus the clause-disabled stale-read measurement in §6 step 4 (iii) — no green-path fixture can bound that quantity (§A-5) |
| **FR-051 (new, P-5)** | §S4's sizing block, §S7 anchors (v) and (vi), §4.3's fifth and sixth patterns; **asserted by SC-012** |

| SC | Case | File |
|---|---|---|
| SC-001 | `AtmosphereGhost_DefaultInert` | `atmosphere_ghost_test.cpp` |
| SC-002 | `AtmosphereGhost_ReverseIsTimeReversed` | `atmosphere_ghost_test.cpp` |
| SC-003 | `AtmosphereGhost_ReverseLiveness` `[long]` + `..._Short` | `atmosphere_ghost_longrun_test.cpp` / `atmosphere_ghost_test.cpp` |
| SC-004 | `AtmosphereGhost_ReverseTruncation` (**never `[long]`**; `..._Sweep` `[long]` sibling only if > 15 s) | `atmosphere_ghost_test.cpp` / `atmosphere_ghost_longrun_test.cpp` |
| SC-005 | `AtmosphereGhost_TriggerAccounting` | `atmosphere_ghost_test.cpp` |
| SC-006 | `AtmosphereGhost_AppendOnly` + tool/suite runs | `atmosphere_ghost_test.cpp`, `tools/check-seraphis-green.js` |
| SC-007 | `AtmosphereGhost_Determinism` (+ (d) `[long]`) | `atmosphere_ghost_test.cpp` / `atmosphere_ghost_longrun_test.cpp` |
| SC-008 | `AtmosphereGhost_RtSafety` / `AtmosphereGhost_NonFiniteSetter` | `atmosphere_ghost_test.cpp` / `atmosphere_ghost_nonfinite_test.cpp` |
| SC-009 | `AtmosphereGhost_CpuDelta` `[.perf]` | `atmosphere_ghost_perf_test.cpp` |
| SC-010 | `VoragoEngine_GhostExtensionWiring` (+ `_Engaged` `[long]`, D-4) | `vorago_ghost_ext_test.cpp` |
| **SC-011 (new)** | `AtmosphereGhost_ReverseFillDeficit` (untagged) — arms (a), (a2), (c); the binding criteria for FR-050 | `atmosphere_ghost_test.cpp` |
| **SC-012 (new)** | `AtmosphereGhost_PassAScratchBound` (untagged, plus an ASan run) — the binding criterion for FR-051 | `atmosphere_ghost_test.cpp` |

---

## 8. Decisions taken here, and what still needs a ruling

**Taken (no ruling needed):** the borrow-form backwards advance (§S3); the templated-lambda direction
hoist with a named fallback (§S3, R-5); `birthAndTrack` extraction as the sanctioned site-(iii)
deletion (§S4); the five-TU split and which single TU is `-fno-fast-math` (§3.1); `setDecorrelation(0)`
plus the exact/corner two-arm structure in SC-004 (§3.2); D-4's SC-010 split rule.

**Needed a ruling before the build stage — all eleven ruled on 2026-09-23, each taking the
recommended option; the spec records them as Clarifications R-1 … R-11, each with an enforcing FR/SC:**

1. **P-1 / FR-050** — adopt the reverse fill-up admission clause as a new functional requirement,
   **together with SC-011 (a) and (a2) as its binding criteria and the clause-disabled measurement in
   §6 step 4 (iii) as the evidence for the quantity it protects**. *Recommended: yes.* Without the
   clause a reverse grain born while the ring is filling reads a clamped, stale position for seconds.
   Note explicitly, because the reviewed draft claimed otherwise: **no green-path fixture can assert the
   stale-read bound**, since FR-050's admission predicate is the negation of the crossing condition and
   admitted-implies-safe is therefore a theorem (§3.2 SC-011 (b), §A-5). The reviewed draft's SC-011 (b)
   is deleted rather than repaired.
2. **P-2** — SC-003's pre-roll 5 s → the **full-ring rule** (`getCaptureCapacitySamples()` samples =
   **21.845 s** at 48 kHz / `captureSeconds = 20`) and its `coldStartSkips` ceiling 3 → **9**, with the
   measured-span assertion unchanged; FR-049's twin inherits the same pre-roll. **SC-010 (b)'s 25 s
   pre-roll does NOT satisfy the rule and is corrected to 32.768 s** — the 8 kHz ring is
   `nextPowerOf2(160 000) = 262 144` samples, not 20 s (`rolling_capture_buffer.h:75-93`). SC-004's
   pre-roll is likewise stated as 65 536 samples = 1.365 s. No pre-roll anywhere in the phase is stated
   in seconds; each is `getCaptureCapacitySamples()` samples with the seconds recorded as measured.
3. **§4.2's constant count** — whether moving `kReferenceNs`'s derivation inputs (`kSr48`,
   `kBlockSize`, `kBlockBudgetNs`) with it still satisfies FR-047's "exactly the four constant
   definitions". *Recommended: yes, recorded explicitly in the compliance table.*
4. **P-5 / FR-051 and FR-045's site count** — adopt the `kMaxGrains * 3` pass-A scratch sizing as a new
   functional requirement **with SC-012 as its criterion**, and amend FR-045's deletion budget from
   **four** sites to **six**: `prepare()` step 5b (`:436-441`) is the fifth, and the two shipped
   comments this phase falsifies (`:2595-2596`, `:1183`) are the sixth. *Recommended: yes.* The
   alternative to the sizing — deferring a trigger to a sample the scheduler did not fire on —
   preserves the four-site budget by breaking FR-020's "within the next N rendered samples,
   deterministically", i.e. it trades a spec requirement for a plan-level diff budget. The alternative
   to anchor (vi) is shipping a header whose declaration comment states a real-time bound P-5 proves
   wrong and whose `readFrac` field states a range §S3 proves the code no longer maintains.
5. **FR-045 site (ii)'s range** — amend to *"the `advance` lambda at `:1876-1882` together with the two
   loops in `renderGrainSpan` that call it (`:1888-1891`, `:1917-1930`)"*. *Recommended: yes.* §S3's
   design (and its documented member-template fallback) re-indents both loops, so the deletions land
   outside the range as written, and SC-006 clause 2 is an assertion about the `-U0` diff, not about
   what a later-written script tolerates. §4.3's patterns are written against those three ranges only,
   so a deletion anywhere else still fails.
6. **P-6 / FR-018's threading clause** — narrow "callable from the control step between or inside
   blocks" to **AUDIO-THREAD-ONLY**, and add the matching note to the component banner's real-time
   contract. *Recommended: yes.* `triggerGrain()` is the one mutator on this component that is a
   read-modify-write of state pass A also mutates; the alternative is an atomic counter with a CAS
   saturation loop and a recorded `is_lock_free()` check, for a caller this phase does not have.
7. **SC-006 clause 6's pass-A half** — restate from "zero additional loads of `pendingTriggers_` on the
   path taken when it is zero" (a semantic property, undischargeable from a `-U0` excerpt) to the
   token rule *"`pendingTriggers_` appears in the per-sample body exactly once, as the right operand of
   a short-circuited `&&` whose left operand is the loop-invariant `anyPending` declared above the
   `for`"*. *Recommended: yes* — it tightens the gate and weakens nothing.
8. **FR-048's `[long]` candidate list** — remove **SC-004**. *Recommended: yes.* FR-048's own closing
   sentence and `CLAUDE.md`'s Build Commands rule forbid tagging a NaN/Inf-guard or bounded-grid case,
   and SC-004 is the phase's only reverse-path NaN/Inf and `kMaxLevel` check at the extreme ratio
   corner. If it measures over 15 s it splits (short untagged arm + `[long]` sweep sibling), as FR-049
   splits SC-003.

9. **FR-012's `readFrac` range** — amend the spec text. FR-012 as written (spec.md:213-223) says
   "`readFrac ∈ [0, 1)` for the grain's whole life"; §S3 shows the backwards borrow can land on exactly
   `1.0f` (the `-2.98e-8` row), so the plan's design cannot satisfy the requirement as stated and no
   criterion asserts it either way. Two options, and one must be named here rather than left in §9:
   (a) **amend FR-012** to *"`readFrac` is non-negative and `<= 1` for the grain's whole life (the
   ceil-correction can round a value in `[-2.98e-8, 0)` up to exactly `1.0f`); truncation-is-floor
   applies to `age`, not to `readFrac`"*; or (b) adopt the one-compare clamp
   `if (readFrac >= 1.0f) { readFrac = 0.0f; --readIndexInt; }` on the backwards path so FR-012 stands
   verbatim. *Recommended: (a)*, because the properties the code relies on — the shipped `advance`
   banner's non-negativity at `:1871-1875` and the birth commit's at `:1767-1771` — are satisfied
   without the clamp, and `readFrac` is consumed only by the `ageAt` subtraction at `:1854-1857`, never
   as an interpolation weight. Whichever is chosen, `:1183`'s field comment changes with it (anchor
   (vi), item 4).
10. **SC-003 (b)'s per-grain endpoint clause** — spec SC-003 (b) requires "the first and last emitted
    sample of **every** reverse grain is exactly `0.0f`". It is unobservable where the spec puts it:
    `processStereoBlock` emits only the summed grain bus after the FR-028 population gain
    (`:2171-2192`), and at `density = 0.30 × grainSeconds = 12` there are ~3.6 concurrent grains.
    Amend: the clause is **deleted from SC-003 (b)** and replaced by (i) the isolated-span endpoint
    assertion in SC-002 (`span.front() == 0.0f && span.back() == 0.0f`, bit-wise, on the one-grain
    protocol) and (ii) SC-003 (b)'s relative click-detector comparison. *Recommended: yes.* This is a
    narrowing from "every reverse grain" to a single isolated grain at `ratio == 1.0`, of the same
    class as P-2's change, so it is ruled here rather than recorded only as a review note.
11. **SC-010 gains arm (e)** — a criterion that sets `atmosGhostReverseProbability` to a non-default
    value. No criterion in the reviewed draft ever did, so the FR-030/FR-034 forwarding line was
    unfalsifiable. *Recommended: yes*; no threshold moves and nothing else in SC-010 changes.

---

## 9. Review notes

### Round 1

Every issue raised in the first plan review was **accepted**; none was rejected, and no threshold was
relaxed to resolve one. Where an issue offered alternative resolutions, the choice and its reason are
recorded above rather than left implicit:

- **Pass-A scratch overflow (P-5):** took the *raise the sizing to `kMaxGrains * 3`* option over
  *forbid a second birth on the same sample*, because the latter defers a trigger and breaks FR-020's
  determinism bound — a spec requirement — to preserve a plan-level diff budget. Cost: a fifth
  sanctioned deletion anchor, declared.
- **`triggerGrain()` threading (P-6):** took the *declare AUDIO-THREAD-ONLY* option over the atomic +
  CAS option, because no caller in this phase is off-thread and the atomic would be unexercised
  machinery. The narrowing is a spec amendment, not a silent doc change (§8.6).
- **`readFrac` invariant (§S3):** took the *restate the invariant as `[0, 1]` and non-negative* option
  over adding the `if (readFrac >= 1.0f)` clamp, because the properties the code actually relies on —
  the shipped banner's non-negativity at `:1871-1875`, the birth commit's at `:1767-1771` — are
  satisfied without it, and `readFrac` is consumed only by the `ageAt` subtraction, never as an
  interpolation weight. The clamp is recorded as the one-compare fix should a future change need the
  strict form.
- **SC-003 (b):** took the *drop the per-grain endpoint claim from SC-003, keep it in SC-002* option,
  because the engine emits only the summed grain bus after the population gain (`:2171-2192`) and
  SC-003's ~3.6 concurrent grains make no individual endpoint observable. The endpoint assertion moves
  to SC-002's already-asserted one-grain isolation; SC-003 (b) keeps the relative click-detector
  comparison, with the detector to be cited by file:line at implementation time.
- **SC-002 / FR-012:** took the *isolated-grain age identity* option
  (`max - min == (1 + ratio)·(L' - 1)` within 1 sample) as the primary, with the centroid-slope arm as
  a secondary, because the identity is exact and directly detects a wrong-rate walk, and — as §SC-002
  (c) shows — the fold pollution that would normally make an engine-lifetime accessor unusable is
  eliminated by this fixture's zero position spread, zero pitch spread and zero decorrelation.
- **SC-005 (a):** applied *both* suggested fixes (pin `kMinGrainSeconds` with a
  `getActiveGrainCount() == 0` precondition, **and** restate the equality in the `k0`-aware form), so
  the arm is correct whether or not the idle render drains the pool on some future scheduler change.

### Round 2

Fourteen issues. Thirteen accepted and applied in full; one accepted in substance and **rejected in its
proposed correction**, with the reason below. No threshold was relaxed anywhere: the two changes that
*shorten* a measured span (SC-011 (a)) or *delete* a clause (SC-011 (b)) both replace an assertion that
was red-on-correct-code or could-not-fail with one that is neither, and SC-011 gains arm (a2) and the
clause-disabled step-4 measurement in exchange.

- **SC-011 (a) (blocker).** Accepted. The measured span is bounded to the provably-rejecting regime
  (5 s pre-roll + 40 000 samples, ending 8 128 samples below the recomputed
  `kAdmitThresholdSamples`), the jitter-derived `== attempts` equality is replaced by
  `1 <= Δ skipRingCold <= 2` with the interonset bound that justifies it, and the positive half the
  issue asked for is arm (a2).
- **SC-011 (b) (blocker).** Accepted, including the theorem. (b) is deleted rather than repaired, its
  measurement moved into §6 step 4 (iii) as a clause-disabled run, and R-1, §A-5 and §8.1 now state
  that admitted-implies-safe makes a green-path fixture impossible in principle.
- **FR-051 has no criterion (major).** Accepted — and resolving it exposed a second defect in P-5 the
  issue did not raise: the reaching configuration the plan named (512 Hz, `captureSeconds = 1`,
  `grainSeconds = kMinGrainSeconds`) **does not reach**. Two births at one sample need the density
  scheduler to fire every sample, which needs `sampleRate <= kMaxDensity = 20` (`:304`,
  `grain_scheduler.h:100-103`); at 512 Hz the scheduler fires every 25.6 samples and pass A tops out
  near 67 entries against a capacity of 128. P-5 is corrected and SC-012 drives a configuration that
  does reach (~186 entries), with a **portable** assertion — `Δ getTotalGrainsRetired()` across one
  64-sample chunk *is* that chunk's `dueCount` — as well as the ASan run the issue asked for. FR-051 is
  therefore not ASan-only, which matters because the issue is right that `AllocationScope` is blind to
  it.
- **SC-010 never moves `atmosGhostReverseProbability` (major).** Accepted; arm (e) added, §7 and §8
  item 11 record it.
- **SC-005 (b) off-by-one (major).** Accepted; the post-first-sample delta is 63, and the arm gains the
  explicit FR-020 rate assertion (delta exactly 2 on the first sample, exactly 1 on the second) the
  issue correctly notes is the phase's only purchase on "at most one per sample".
- **FR-012 divergence not ruled (major).** Accepted; §8 item 9 added with both options and a
  recommendation.
- **SC-005 (d) / SC-008 (d) vacuous halves (major).** Accepted; both arms now assert on consumption
  evidence (`getSkippedTriggerCountRingCold() == schedulerTicks`, absolute because `reset()`/`prepare()`
  zero the counter) instead of on births, and SC-005 (d) gains the warm-ring sub-arm that makes
  `getTotalTriggeredGrainsBorn() == 0` and `getTotalReverseGrainsBorn() == 0` real post-reset
  assertions (the separate minor issue about FR-009/FR-026 counter clearing, resolved in the same
  place).
- **SC-003 (b) narrowing unruled (minor).** Accepted; §8 item 10.
- **SC-007 (c) does not pin "exactly one" (minor).** Accepted; the replica technique replaces the bare
  cross-run equality, which is retained as a localising check.
- **Stale header comments outside every deletion anchor (major).** Accepted; §S7 gains anchor (vi),
  §4.3 a sixth pattern, §8 item 4 the site count **six**, and R-6's `[0,1)` wording is corrected to
  match §S3.
- **SC-009 never measures the saturated drain (minor).** Accepted; arm 5 added, with the factor taken
  from the first clean measurement and frozen, and the note that Vorago itself triggers at most once
  per control chunk.
- **P-3's own correction is wrong (minor).** Accepted. Verified this session with `grep -n`: the four
  Phase 5 TUs are at `dsp/tests/CMakeLists.txt:373-376`, exactly as the spec wrote them; `:377-380` is
  the Phase 7 block header plus `seraphis_voice_test.cpp`. The correction is withdrawn. While checking
  it, the neighbouring citation turned out to be one line out as well — the `-fno-fast-math` opt-in is
  at `:896` inside the block `:881-896`, not `:895` / `:883-896` — and that is corrected too, in P-3,
  §3.1 and §4.1.
- **P-2's "6.77 s" arithmetic (minor) — ACCEPTED IN SUBSTANCE, PROPOSED FIGURE REJECTED.** The issue is
  right that the figure does not follow from the formula the plan prints, and right that the formula's
  output at the minimum birth age is 377 013 samples = 7.85 s. But **the formula itself is the defect,
  not the figure**: it is `A >= (birthAge + decorr + guard + rMax*C) / (1 + rMax)`, which is FR-050's
  *saturation* branch, obtained by substituting `t* = C - A`. FR-050 as §0 P-1 writes it uses
  `t* = min(lifetime, capacity - avail)`, and at the Vorago ghost point `L' = 576 000` while
  `C - A = 808 576` at a 5 s pre-roll, so the binding branch is `t* = L'` and the threshold is the
  **constant** `needed + ceil(0.561231 * 576 000) = needed + 323 270`, i.e. **323 398 samples = 6.74 s**
  at the minimum birth age — not 7.85 s, and not the draft's 6.77 s either. Adopting the proposed
  correction would have written a *larger* wrong number into the document and, worse, into SC-011 (a)'s
  span bound, where an over-large threshold silently widens the measured span back toward the admitting
  regime the blocker above exists to keep it out of. P-2 now carries the corrected formula, both
  branch figures with their substitutions shown (6.74 s and ≈ 9.85 s, the latter within two samples of
  the branch crossover `C - L' = 472 576` by construction), and the rule that every threshold is
  recomputed by the test rather than transcribed. The companion 9.83 s figure the issue accepted is
  restated as 9.85 s from the correct branch.
