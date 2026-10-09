# Feature Specification: Vorago Phase 10a — AtmosphereEngine Ghost Extension

**Spec slug:** `vorago-phase10a-ghost-extension`
**Roadmap source:** `specs/Vorago-roadmap.md` → Part A → **Phase 10a** (lines 491–511); reuse-inventory
row **L6 Granular Ghosts** (line 115), whose 🔶 clause names this phase by name; the dependency graph
(line 576, `Phase 10a (ghost) → Phase 12`); the cross-cutting constraints (lines 587–611), of which
**"Shared-component changes (AtmosphereEngine ghost extension in Phase 10a …) must keep Seraphis's
tests green"** is lines 609–611, the **no-bit-exact-goldens** rule is line 605 and the **ODR sweep**
rule is line 594. Every roadmap line number below was read and re-verified against
`specs/Vorago-roadmap.md` this session.
**Prior ruling this phase exists to execute:** Phase 10 spec `OQ-3`, ruled 2026-09-17 as
Clarifications **Q-B = (A) + (C)** (`specs/vorago-phase10-voice-engine/spec.md:2280-2310`,
`plan.md:2896-2916`): Phase 10 ships FR-017's ghost *configuration* only, and **a named later phase,
Phase 10a, owns reverse grains and event-triggered grain scheduling**.
**Layer:** **no new component and no new class.** One **append-only extension** to the shipped
Layer 3 `AtmosphereEngine` (`dsp/include/krate/dsp/systems/atmosphere_engine.h:179`), plus a
**default-off** wiring flag pair on the Layer 3 `VoragoEngineConfig`
(`dsp/include/krate/dsp/systems/vorago_engine.h:105`, the struct's opening line).
**Test target:** `dsp_systems_tests` — an **enumerated, not globbed** source list; the four shipped
Phase 5 TUs are registered at `dsp/tests/CMakeLists.txt:373-376`. A TU that is not listed there
silently drops out of the build and its cases never run. The `-fno-fast-math` opt-in list at
`:878-896` registers **only** `atmosphere_engine_nonfinite_test.cpp` of the four, deliberately
(`:888-895`); this phase's new TUs inherit that rule (FR-042).
**Depends on:** Phase 10 — shipped 2026-09-22 at commit
`374580d7d0f0631561413310bd3085e15ba7279c` ("feat(vorago): Phase 10 Voice and Engine"), which is
**this phase's base commit** and the revision every diff clause and every stored reference below is
taken against. Phase 10 contributes `VoragoEngine`, `kDefaultPolyphony = 4`, `kMaxVoices = 6` and two
CPU figures that this spec is careful to keep apart:
- **The checked-in global baseline** — `kEngineBaselineNsAtPoly4 = 2 694 479` ns/block
  (`dsp/tests/unit/systems/vorago_perf_test.cpp:257`), with its measured partner
  `kEngineMeasuredNsAtPoly4 = 2 566 170` (`:256`) and the FR-083 rule
  `baseline == ceil(measured × 1.05)` static-asserted at `:263-266` (and
  `kBaselineWithCavernNs <= kReferenceNs` at `:269`). This is the constant that
  carries roadmap line 506's "measured against Phase 10's **checked-in** global baseline" obligation,
  and SC-009 clause 5 is where that obligation is discharged.
- **The engine-column atmosphere stage figure — 28 285.5 ns/block.** This is **not** a checked-in
  constant. It exists only as a line of an artifact log
  (`specs/vorago-phase10-voice-engine/artifacts/perf.log:68`, "ENGINE COLUMN: atmosphere 28 285.5"),
  printed by the **stage probe** inside `VoragoVoice_StageCostProbe`
  (`vorago_perf_test.cpp:1264-1277`), which the TU itself labels a BREAKDOWN that "gates nothing"
  and deliberately measures at a reduced shape (`:294-302`). SC-009 may use it as a reference **only**
  by reproducing that probe's exact shape, which SC-009 now states and pins.
**Plugin work:** none. Parameter exposure is Phase 12; preset use is Phase 14 (roadmap line 495,
"sequenced before Phase 14 so presets can use it").

---

## Overview

`AtmosphereEngine` is Seraphis Phase 5's granular ghost layer, reused by Vorago as the engine-global
ghost tap. Roadmap line 115 asks Vorago's ghost tap for three things: **darker blur defaults**,
**reverse playback per grain**, and **event-triggered (not continuous-density) scheduling**. Phase 10
delivered the first outright (FR-017's seven numeric values, gated by SC-027) and *substituted*
event-gated `setLevel` for the second — because the shipped header has **no reverse control and no
event-trigger entry point** at all, so the other two are not configuration but a source change to a
Seraphis-shipped component. Phase 10 refused to absorb that change (it already carried four of them
and was tight against its budget) and named this phase instead.

Phase 10a adds exactly those two capabilities, append-only, on the same model Phase 10 used for
`ContinuousBody` and `AetherReverb`: a per-grain reverse flag drawn at grain birth from a **new,
dedicated RNG stream** so the existing `grainRng_` draw order is untouched, and a
`triggerGrain()` entry point beside the density scheduler so a `SlowEventScheduler` event **spawns a
grain** instead of only raising the level. Both are **inert at their defaults**: with reverse
probability 0 and no trigger call, the render, every RNG stream state and every counter are what they
were before the change, which is what keeps Seraphis's shipped suites and Phase 10's checked-in CPU
baseline green with no test edited.

**One roadmap fact did not survive verification and is corrected here.** Roadmap line 501 names
`primitives/reverse_buffer.h` as the substrate for reverse grains. It is not usable: `ReverseBuffer`
(`dsp/include/krate/dsp/primitives/reverse_buffer.h:27`) is a **mono, chunk-swapping double buffer**
with its own capture, its own `std::vector` allocation in `prepare()`, its own crossfade and a
latency equal to its chunk size (`:75`, `:107-118`, `:152-206`) — it captures audio itself rather than
reading someone else's ring, it is single-channel, and it has no notion of a per-grain read position.
`AtmosphereEngine` already reads its capture ring by *absolute index plus fraction*
(`atmosphere_engine.h:1182-1184`) through `RollingCaptureBuffer::LinearReader::indexAt`
(`rolling_capture_buffer.h:279-284`), so a reverse grain is **the same read with the position walking
backwards**, and needs no new substrate, no new include and no new allocation. FR-010 states this as
a requirement; ADR-1 records the correction.

---

## Scope

In scope, and nothing else:

1. An append-only per-grain **reverse** capability on `AtmosphereEngine`: one probability control, one
   dedicated RNG stream, one grain field, the birth-window arithmetic that a backwards read requires,
   a backwards advance in the span renderer, and three introspection accessors.
2. An append-only **event-trigger entry point** on `AtmosphereEngine`: `triggerGrain()`, its pending
   queue, its consumption point in the per-sample pass, and two introspection accessors.
3. A **default-off** wiring of both onto `VoragoEngine`'s global ghost tap, driven by the ghost
   request the engine already computes (`vorago_engine.h:1248-1254`).
4. The verification that the change is inert at its defaults, that Seraphis stays green, and that the
   global-stage CPU delta is measured and recorded against Phase 10's checked-in figures.

## Non-goals (what later phases own)

- **No parameter surface.** No `k{Section}{Parameter}Id`, no denormalisation, no state version.
  Phase 12 (`vorago-phase12-parameters`) owns that; roadmap line 495 sequences this phase before
  Phase 14 so *presets* can use it, not so this phase can author them.
- **No preset authoring and no listening trim.** Phase 14 (`vorago-phase14-presets-release`).
- **No change to Phase 10's shipped ghost behaviour.** FR-017's seven configuration values and
  SC-027's event-gated `setLevel` burst (`vorago_engine.h:1272`) stand exactly as shipped; this phase
  **adds** a spawn path, it does not replace the level gate (FR-030).
- **No change to any CPU ceiling.** Roadmap line 470's 30 % global ceiling (`kReferenceNs =
  3 200 000` ns/block, `vorago_perf_test.cpp:168`), `kRegressionFactor = 1.5` (`:165`),
  `kEngineBaselineNsAtPoly4 = 2 694 479` (`:257`) and the Cavern term `124 497` (`:215`) are read,
  never edited (FR-041, SC-009).
- **No refactor of `AtmosphereEngine`.** No blur, freeze, drift, envelope, pan, decorrelation,
  scheduler, latch or SIMD change. The `accumulateGrainSpanSIMD` kernel
  (`atmosphere_engine.h:1959-1966`) is not touched at all: reverse affects only the *scalar*
  index-generation pass and the advance recurrence.
- **No `ReverseBuffer` use, and no change to `ReverseBuffer`.** See ADR-1.
- **No new class.** See *New components* below.
- **No Seraphis behaviour change.** Seraphis never calls either new setter, so both defaults keep it
  on the pre-change code path (FR-040).

---

## Architecture decisions

These are determinations the shipped code forces, not open questions.

### ADR-1 — Reverse is a backwards walk of the existing ring read, not `ReverseBuffer`

Verified above. `AtmosphereGrain` holds `readIndexInt` (absolute source index, integer part) and
`readFrac` (`atmosphere_engine.h:1182-1183`); `renderGrainSpan`'s `advance` lambda (`:1876-1882`)
moves them **forward** by `ratio` per output sample while the write head also moves forward by one,
so the read age changes at `ratio − 1` per sample. Walking the pair **backwards** by `ratio` makes
the age change at `ratio + 1` per sample — strictly growing, never shrinking. Nothing else in the
read path changes: `LinearReader::indexAt` takes an age and yields ring indices plus a weight
(`rolling_capture_buffer.h:279-284`), and `i1` is one sample **older** than `i0` in both directions
(`:282`), so interpolation is correct unmodified.

### ADR-2 — The reverse draw comes from a NEW RNG stream, not `grainRng_`

`tryBirthGrain`'s comment at `atmosphere_engine.h:1613-1616` is explicit: the four birth draws
(`:1617-1620`) are in a **fixed order** and "inserting, removing or reordering one re-shuffles every
subsequent grain's parameters". A fifth draw on `grainRng_` would therefore change **every Seraphis
render** the moment the code landed, at any probability including 0. A dedicated `Xorshift32`
seeded through `deriveStreamSeed(seed, kReverseSalt)` — the same discipline `blurRng_` already uses
for exactly this reason (`:1013-1021`, `:2638`) — leaves `grainRng_`'s stream bit-identical at every
setting, which is what makes FR-040's inertness *structural* rather than merely tested.
`kReverseSalt` must sit outside every existing salt range: `kGrainSalt = 0x1000`,
`kBlurSalt = 0x2000`, `kSchedulerSalt = 0x3000`, `kDriftSaltBase = 0x4000` spanning
`kMaxGrains = 64` entries (`:331-334`, `:189`), and the header already static-asserts range
disjointness at `:360`.

### ADR-3 — The Vorago wiring ships OFF

Phase 10's SC-027 clause 2 and its checked-in `kEngineBaselineNsAtPoly4` were measured on the shipped
ghost path. Engaging either behaviour by default would move a Phase-10 criterion and a Phase-10
baseline inside a phase whose own success criteria say the ceiling is unchanged (roadmap line 510).
Both `VoragoEngineConfig` fields therefore default to their inert values (FR-030), and whether the
shipped Vorago default should engage them is **OQ-1**, below — the one decision the roadmap leaves to
a later phase (line 495 assigns the *use* to presets).

---

## Functional requirements

### FR-001 – FR-009 — Reverse: the control surface

- **FR-001** `AtmosphereEngine` gains a public setter `void setGrainReverseProbability(float) noexcept`
  and a getter `[[nodiscard]] float getGrainReverseProbability() const noexcept`. The stored value is
  clamped to `[0, 1]`. The naming follows the component's own `setGrain*` prefix
  (`setGrainSeconds` `:815`, `setGrainEnvelope` `:995`) and the repo's shipped
  `GranularEngine::setReverseProbability` semantics (`granular_engine.h:123-125`, `:283`).
- **FR-002** The **default is `0.0f`**, i.e. every grain is forward, i.e. the pre-change behaviour.
- **FR-003** The setter obeys the component's shipped setter contract: a **non-finite** argument is
  **substituted with the control's default, `0.0f`** — the `isFinite(x) ? x : <default>` shape used at
  `:816`, `:829`, `:852`, `:859`, `:903`, `:983`, identical to `setGrainSeconds`, `setDensity` and
  `setLevel`'s own substitution behaviour on this component — and an out-of-range argument is
  **clamped**, with the getter reporting the clamp. It is `noexcept`, allocation-free and safe to drive
  at block rate. **This differs by design from Phase 10's FR-069 macro-write rule, which retains the
  previous value on a non-finite macro write**: that rule is a `MacroMatrix` contract, not an
  `AtmosphereEngine` setter contract, and this control follows the component's own shipped setter shape
  instead (Clarifications 2026-09-22, Q2).
- **FR-004** A new private `Xorshift32 reverseRng_` is seeded from
  `deriveStreamSeed(seed_, kReverseSalt)` with a new `static constexpr std::size_t kReverseSalt`
  whose value lies **strictly above `kDriftSaltBase + kMaxGrains`** (`:334`, `:189`). A new
  `static_assert` in the shape of `:360` states the disjointness.
- **FR-005** `reverseRng_` is re-seeded in **exactly the three places** the other streams are:
  `prepare()` (beside `:552-556`), `reset()` (the same block) and `setSeed()` (`:1013-1021`). No
  other method re-seeds it.
- **FR-006** At each grain birth `tryBirthGrain()` takes **exactly one** `reverseRng_.nextUnipolar()`
  draw, **unconditionally** and at a fixed position relative to the four `grainRng_` draws, and the
  grain is reverse iff that draw is `< reverseProbability_`. Unconditional is load-bearing: a draw
  taken only when the probability is non-zero would make the stream's position depend on the control
  value, and `setGrainReverseProbability` would stop being a pure gain on a fixed stream.
- **FR-007** The four `grainRng_` draws at `:1617-1620` are **not moved, not reordered and not
  added to**. `getGrainRngState()` (`:1118`) after any fixed render is therefore identical
  pre- and post-change at every probability (SC-001 clause 2).
- **FR-008** `AtmosphereGrain` (`:1181-1201`) gains one field, `bool reversed = false`, snapshotted
  at birth beside `active` (`:1200`) and never re-read from the control surface during the grain's
  life — the same birth-snapshot rule the pitch envelope, pan and decorrelation already follow
  (`:1185-1193`). Changing the probability mid-render affects only grains born afterwards.
- **FR-009** Three introspection accessors are added beside the existing FR-072 block
  (`:1087-1118`): `[[nodiscard]] bool getLastBornGrainReversed() const noexcept`,
  `[[nodiscard]] std::uint64_t getTotalReverseGrainsBorn() const noexcept` (monotonic, reset by
  `reset()` with the other counters) and `[[nodiscard]] std::uint32_t getReverseRngState() const
  noexcept`. Without them FR-006's draw is unassertable and the Phase 2 FR-067/FR-056 failure mode
  (implemented but unassertable for want of an accessor — roadmap lines 186–189) repeats.

### FR-010 – FR-017 — Reverse: the read path

- **FR-010** A reverse grain reads the **same capture ring through the same `LinearReader`** as a
  forward grain (`rolling_capture_buffer.h:279-284`). No new buffer, no new allocation, no new
  include, and no use of `ReverseBuffer` (ADR-1).
- **FR-011** For a reverse grain the read position walks **backwards**: per output sample,
  `readIndexInt`/`readFrac` decrease by `ratio` while the write head advances by one, so the read age
  grows at `ratio + 1` samples per sample.
- **FR-012** The `readFrac` invariant is preserved in the form the code actually relies on
  **(amended 2026-09-23, R-9)**: `readFrac` is **non-negative and `<= 1`** for the grain's whole
  life. The backwards borrow's ceil-correction can round a value in `[−2.98e-8, 0)` up to exactly
  `1.0f`, so the half-open `[0, 1)` form the reviewed draft stated is *not* what the arithmetic gives
  and is not required: what the truncation-is-floor identity needs is non-negativity, and it applies
  to the **age** `LinearReader::index0` truncates (`rolling_capture_buffer.h:313-321`), not to
  `readFrac` itself, which is consumed only by the `ageAt` subtraction (`:1854-1857`) and never as an
  interpolation weight. `i1` is still one sample older than `i0`. The `:1183` field comment
  ("fraction in [0,1)") changes with this requirement (FR-045 site (vi)). The reference
  decomposition is the shipped **forward** `advance` lambda at
  `:1876-1882` — `readFrac += ratio`, truncate to `carryInt`, add the integer to `readIndexInt`,
  subtract it back off `readFrac` — and the backwards form is its mirror: subtract `ratio` from
  `readFrac`, take the **borrow** (`ceil` of the negative part, obtained by truncation on the
  negated value), subtract it from `readIndexInt` and add it back to `readFrac`. It is exact for
  every legal `ratio ∈ [0.125, 8]` — the clamp that guarantees that range is the birth-time pitch
  clamp at `:1622-1635` — and contains **no `std::floor` and no CRT call**, which is the codegen rule
  the lambda's own banner states at `:1871-1875` ("TRUNCATION, NOT std::floor … std::floor(float) is
  a CRT call on MSVC's default /arch") and `rolling_capture_buffer.h:302-318` measures.
- **FR-013** The direction is resolved **outside the per-sample loop**. `renderGrainSpan` (`:1829`)
  branches on `grain.reversed` once per span; no per-sample test on the flag appears in the inner
  loop. The Phase-2 SIMD pass (`:1959-1966`) is unchanged, because reverse changes only which indices
  the scalar pass writes into `idxL0/idxL1/fracL` (`:1917-1930`).
  **Verified by SC-006 clause 6**, a diff-anchored code-review gate. It is deliberately *not*
  delegated to SC-009: a per-sample compare on a `bool` is far below the resolution of any CPU arm
  this phase can run (see FR-025), so a timing criterion could not falsify it.
- **FR-014** `tryBirthGrain`'s liveness arithmetic (`:1644-1712`) is extended, **not rewritten**, by
  making the two rate terms direction-dependent and leaving every other line as it stands:
  | Term | Forward (shipped, `:1651-1652`) | Reverse (new) |
  |---|---|---|
  | `wUp` — rate at which the age **shrinks** | `max(ratioMax − 1, 0)` | `0` — a reverse read never catches up with the write head |
  | `wDown` — rate at which the age **grows** | `max(1 − ratioMin, 0)` | `1 + ratioMax` |
  `w = wUp + wDown` (`:1658`), `headroom` (`:1676`), `slack` (`:1691`), the truncation
  `lifetime = (w·requested > slack) ? floor(slack / w) : requested` (`:1692`), `ageLo` (`:1700`),
  `ageHi` (`:1705`), the FR-014 admission test and the two `kMinAgeSamples` guards are **unchanged in
  form and unchanged in code**. The `L' ≥ 2` rejection (`:1696-1699`) applies identically.
- **FR-015** The pass-B validity argument survives: `ageLo = guard = kMinAgeSamples = 64` for a
  reverse grain, and the age only grows thereafter, so **every read a reverse grain makes is at least
  `kMinAgeSamples` old** — which is the precondition `renderGrainChunk`'s banner states at
  `:1999-2004` for rendering a whole chunk after that chunk's writes. The `static_assert(kMinAgeSamples
  >= kControlChunkSamples)` at `:345` continues to carry it.
- **FR-016** Decorrelation is unchanged: the right channel still reads at `age + decorrAge`, i.e. the
  older point (`:1641-1642`), and `decorr` still appears in `headroom` and `ageHi`. The FR-014
  admission clip `min(birthAge + decorr, capacity − 2 − guard)` (`:1736`) is unchanged.
- **FR-017** Every other per-grain behaviour is direction-independent and untouched: the envelope
  phase multiplication and its endpoint conditioning (`:1936-1948`), the equal-power pan snapshot
  (`:1748-1751`), the per-grain pitch drift lane and its birth zeroing (`:1763-1765`), the age folds
  (`:1860-1869`), retirement, the round-robin slot sweep (`:1601-1612`) and the active-list
  maintenance (`:1795-1797`).

### FR-018 – FR-027 — Event-triggered grains

- **FR-018** `AtmosphereEngine` gains a public `void triggerGrain() noexcept`: a request that **one**
  grain be born as soon as the render reaches a sample, independent of the density scheduler. It is
  RT-safe, allocation-free and lock-free, and it is **AUDIO-THREAD-ONLY (amended 2026-09-23, R-6)**.
  Unlike every other mutator on this component, which is a pure store of a value the audio thread
  only reads (`setGrainSeconds` `:816`, `setDensity` `:829`, `setDecorrelation` `:903`, `setLevel`
  `:983`), `triggerGrain()` is a read-modify-write of `pendingTriggers_` and `droppedTriggers_` —
  counters pass A also read-modify-writes — so a caller on the UI or automation thread is a data
  race that can lose or double-count a trigger and break FR-022's "consumed exactly once". The
  banner's block-rate automation contract (`:28-35`) does **not** extend to it, and the banner's
  real-time contract gains a note saying so. No atomic is introduced because no caller is
  off-thread: the only call site in this phase is `VoragoEngine::runPreRenderControlStep()`, which
  runs inside `processStereoBlock`, between chunks, on the audio thread (FR-031).
- **FR-019** The request is held in a private saturating counter `pendingTriggers_` bounded by
  `kMaxGrains` (`:189`). A call when the counter is already at `kMaxGrains` is **dropped**, not
  queued, and increments a new `getDroppedTriggerCount()` (FR-026) — the same "skip, never steal"
  philosophy as `skipPoolFull_` (`:1609-1612`, banner `:60-63`).
- **FR-020** Pending triggers are consumed in `renderGrainChunk`'s **pass A** (`:2083-2137`), at most
  **one per sample**, in sample order, immediately **after** the density scheduler's own tick
  (`:2115`). The order is fixed and documented: scheduler first, trigger second. N calls therefore
  produce at most N grains, within the next N rendered samples, deterministically.
- **FR-021** A consumed trigger calls the **same** `tryBirthGrain()` the scheduler calls — same slot
  sweep, same draws, same admission tests, same counters, same in-chunk retirement bookkeeping
  (`:2116-2134`). A triggered grain is in every respect an ordinary grain; it is not exempt from the
  pool cap (`:1609-1612`) or the ring-cold rejection (`:1677-1680`, `:1696-1699`, `:1738-1741`).
- **FR-022** A trigger is consumed **exactly once** whether or not it produces a grain. A trigger
  that is consumed into a rejected birth increments the existing `skipPoolFull_`/`skipRingCold_`
  counter (`:1053`, `:1059`) and is gone; it is never retried on a later sample. Otherwise a
  cold-ring transient would bank an unbounded burst.
- **FR-023** `triggerGrain()` is a **no-op while `Latched`** (`runState_ == RunState::Latched`,
  `:1233`, `:653-658`): a latched engine returns from `processStereoBlock` before pass A
  (`:691-696`) and would never drain the queue. It is accepted while `Silencing` (that state still
  renders). It is a no-op before `prepare()`.
- **FR-024** `reset()` and `prepare()` clear `pendingTriggers_` and the dropped-trigger counter to
  zero, with the other counters (`:534`, `:1064-1073`). `silence()` does not clear them; the latch
  edge does, via FR-023's no-op plus the retirement `silence()` already performs.
- **FR-025** When `pendingTriggers_ == 0` the per-sample loop performs **no additional work beyond a
  predicate hoisted out of the loop**: the pass-A loop tests the pending count **once before the
  loop**, not once per sample. **Verified by SC-006 clause 6**, the same diff-anchored review gate
  FR-013 uses. SC-009 clause 1 is a *backstop*, not the measurement: 512 predicate evaluations per
  block are on the order of a hundred nanoseconds against a ~28 µs block figure measured best-of-12,
  whose own run-to-run drift the reference TU records at 0.6 %–7.4 % (`vorago_perf_test.cpp:239-247`).
  A ±10 % band cannot see it, and this spec does not claim it can.
- **FR-026** Two introspection accessors are added beside the FR-072 block:
  `[[nodiscard]] std::uint64_t getTotalTriggeredGrainsBorn() const noexcept` (grains that a consumed
  trigger actually produced) and `[[nodiscard]] std::uint64_t getDroppedTriggerCount() const noexcept`
  (FR-019). Both are monotonic and reset by `reset()`.
- **FR-027** `tryBirthGrain()`'s four `grainRng_` draws (`:1617-1620`) are a **fixed per-attempt
  contract**, unconditional on the age-admission test that follows them at `:1644-1712`: **every call
  that reaches the draw point — i.e. every call where the round-robin slot sweep finds a free slot, so
  `skipPoolFull_` is not incremented — consumes exactly four `grainRng_` draws**, whether the
  subsequent admission test then accepts the birth (`getTotalGrainsBorn()` increments) or rejects it as
  ring-cold (`skipRingCold_` increments). This is the contract SC-001 clause 2's trigger arm pins with
  a replica RNG (Clarifications 2026-09-22, Q3); the replica is valid there only because that arm's
  fixture keeps the ring warm across the measured span (no `skipRingCold_` increments), so "attempted"
  and "admitted" coincide and four draws per `getTotalGrainsBorn()` increment is the correct accounting
  for that specific render.

### FR-030 – FR-034 — Vorago wiring (default off)

- **FR-030** `VoragoEngineConfig` (`vorago_engine.h:105`; its atmos block is `:114-125`, with
  `atmosCaptureSeconds = 20.0f` at `:120`, `atmosBlurEnabled = true` at `:121` and
  `atmosFreezeEnabled = false` at `:123`) gains exactly two fields, both inert by
  default: `float atmosGhostReverseProbability = 0.0f;` and `bool atmosGhostEventTriggers = false;`.
  Phase 10's FR-017 block (`:290-307`) gains **one line** — the reverse probability written after
  `setDecorrelation` — and nothing else in that block moves.
- **FR-031** When `atmosGhostEventTriggers` is true, `VoragoEngine`'s control step calls
  `atmos_.triggerGrain()` **once on the rising edge of a hysteresis threshold crossing** of the ghost
  request it already computes — the `ghost` maximum over rendering voices at
  `vorago_engine.h:1248-1254`, whose value is written to `atmos_.setLevel(ghostPeak_ * ghost)` at
  `:1272`. **The latched quantity is the gated value `ghostPeak_ * ghost` — the very expression
  written to `setLevel` at `:1272`, not the pre-gate request** — and the edge is defined **exactly as
  Phase 10's own SC-027 burst detector defines it**, with the same two constants:
  | | rise | fall |
  |---|---|---|
  | Phase 10 SC-027, on `atmosphere().getLevel()` | `level >= 0.5f * kGhostBurstPeak` | `level <= 0.05f * kGhostBurstPeak` |
  | FR-031, on `ghostPeak_ * ghost` | `kGhostTriggerRise = 0.5f * kGhostBurstPeak` | `kGhostTriggerFall = 0.05f * kGhostBurstPeak` |

  (`vorago_engine_test.cpp:2666-2667` defines the thresholds, `:2699-2704` the two-state detector.)
  The engine and the shipped Phase-10 criterion therefore share **one** definition of "a ghost
  burst", rather than this phase inventing a second one. A new private `bool ghostTriggerHigh_` holds
  the state, in the shape `VoragoVoice::eventWasActive_` already uses for the same job
  (`vorago_voice.h:1775-1784`): `triggerGrain()` is called on the transition `false → true` only.
  **Gating on the gated value, not the request, is deliberate.** With `ghostPeak_ == 0` the ghost tap
  contributes nothing audible, so a grain spawned there would cost CPU for a sound no one can hear;
  latching on `ghostPeak_ * ghost` closes the spawn path with the level gate, in one place, and makes
  SC-010 (c) *exactly* SC-027 clause 2's closed-gate arm instead of a differently-shaped near-miss.
  FR-033 is unaffected: the level write is retained unconditionally and nothing is removed.
  **Why a hysteresis band and not "previous value was exactly `0.0f` and this one is `> 0.0f`":**
  the combined request is
  `combineWake(0.0f, lanes.eco[kGhost][0], lanes.sched[kGhost][0])`
  (`vorago_voice.h:1846`, `combineWake` = `max` at `:1011-1014`), and its ecosystem term is
  `ecosystemDepth_[k] * output[i]` (`:1734`) with `output` a **continuous** per-agent value in
  `[0, 1]` and `prepare()` installing `setEcosystemDepth(0.85f)` for every kind including Ghost
  (`:617`, setter at `:1309-1314`). At the shipped default the request therefore rarely returns to
  exactly `0.0f`, so an exact-zero predicate would fire at most **once per render** — the failure the
  reviewed draft would have shipped. Phase 10 already met this and answered it by counting
  **threshold** crossings, not exact-zero ones; FR-031 adopts that answer rather than inventing a
  second definition of the same event. A level-polling implementation that triggered every control
  step is still the natural bug, and is still what this clause forbids.
  Consequence, stated rather than left implicit: a ghost swell whose peak depth never reaches 0.5
  raises the level (FR-033) but spawns **no** grain. That is the same population SC-027's `>= 6`
  floor was measured on, which is why SC-010 (b) can reuse that floor.
- **FR-032** When `atmosGhostEventTriggers` is false, `triggerGrain()` is **never called** and the
  latch member is not advanced.
- **FR-033** FR-017's `setLevel` gating is **retained unconditionally** in both states
  (`vorago_engine.h:1272`). Phase 10's SC-027 clause 2 is a criterion of a shipped phase and reads
  `getLevel()`; this phase adds a spawn path beside it and removes nothing.
- **FR-034** Both fields are forwarded to the component at `VoragoEngine::prepare()` only, alongside
  the seven FR-017 values; there is no per-block setter and no macro row. Macro/parameter exposure is
  Phase 12.

### FR-040 – FR-051 — Inertness, portability and the shared-component obligation

- **FR-040** **Default-inert.** With `getGrainReverseProbability() == 0` and no `triggerGrain()` call,
  the component's rendered output, every RNG stream state, every counter, `getLatencySamples()`
  (`:1128`), `getActiveGrainCount()` (`:1048`) and every FR-072 accessor are **what they were before
  the change**. Every Seraphis consumer (`seraphis_voice.h`, `plugins/seraphis/src/parameters/`) is on
  that path by construction, because none of them can name the new setters.
- **FR-041** **No ceiling, baseline or threshold value is changed; four of them move house, taking
  the three definitions one of them derives from (amended 2026-09-23, R-3).**
  `vorago_perf_test.cpp`'s `kReferenceNs`, `kRegressionFactor`, `kEngineBaselineNsAtPoly4`,
  `kEngineMeasuredNsAtPoly4`, `kCavernMeasuredNsPerBlock` and `kCavernBaselineNsPerBlock`
  (`:165-258`) and `atmosphere_engine_perf_test.cpp`'s baselines are **read-only in value** to this
  phase: no number changes. **The one sanctioned structural exception (FR-047, Clarifications
  2026-09-22 Q7, R-3):** four of those constants — `kReferenceNs`, `kEngineMeasuredNsAtPoly4`,
  `kEngineBaselineNsAtPoly4` and `kCavernMeasuredNsPerBlock` — are extracted, unedited, into a new
  shared header `dsp/tests/unit/systems/vorago_perf_budget.h`, **together with `kSr48`, `kBlockSize`
  and `kBlockBudgetNs`, because `kReferenceNs` is derived from them** (`kReferenceNs = kBlockBudgetNs
  * 0.30`, `:160`, `:168`, with `kBlockBudgetNs = (kBlockSize / kSr48) * 1e9`) — seven names in all,
  no value changed — included by both
  `vorago_perf_test.cpp` (whose own `static_assert`s on them stay in place) and this phase's new
  CPU-delta TU (SC-009). Extraction is the only structural change FR-041 permits; the values
  themselves are unedited.
- **FR-046** **Stored-golden fingerprint protocol (Q4; execution ruled 2026-09-23, B-1: the
  three-toolchain probe is a main-loop step after the Windows gate, on standalone WSL g++ 13.3.0 /
  clang++ 18.1.3 builds of the fixture recipes, never from a push).** Every stored-golden `RenderFingerprint`
  comparison this phase adds (SC-001 clause 1, SC-010 (a)) uses the **`noise_organism` measured-bounds
  protocol** (`noise_organism_test.cpp:3288-3406`): per-comparison **MEASURED** checkpoint and metric
  bounds derived from a documented **three-toolchain probe** (MSVC, GNU, LLVM) with headroom over the
  worst observed deviation, never the default `kMetricTolerance`/`kSampleTolerance`; a **paste-ready
  literal printed on comparison failure**; and a **PROVENANCE block** (base commit, machine, compiler,
  date) beside every stored fingerprint literal. Bit-exact float goldens remain forbidden (roadmap
  line 605). Bounds are recorded in this spec once measured and may not be widened afterwards without
  a ruling.
- **FR-047** **Shared perf-budget header (Q7).** `dsp/tests/unit/systems/vorago_perf_budget.h` is a
  new, test-only header holding exactly **seven** extracted constant *definitions* **(amended
  2026-09-23, R-3)**: the four named in FR-041 — `kReferenceNs`, `kEngineMeasuredNsAtPoly4`,
  `kEngineBaselineNsAtPoly4`, `kCavernMeasuredNsPerBlock` — plus `kSr48`, `kBlockSize` and
  `kBlockBudgetNs`, because `kReferenceNs` derives from those three (`:160`, `:168`) and cannot move
  without them — no new value, no renamed constant, no new `static_assert` beyond what already exists
  in `vorago_perf_test.cpp`. The compliance table records the count explicitly ("seven definitions
  moved, four of them the FR-047 four, three of them `kReferenceNs`'s derivation inputs").
  `vorago_perf_test.cpp`'s only permitted modification anywhere in this phase
  is replacing those seven constant definitions with `#include "vorago_perf_budget.h"` (or
  equivalent); every other line of that file, and every other file under `dsp/tests/unit/systems/`, is
  unmodified (SC-006 clause 1, narrowed accordingly). `tools/check-seraphis-green.js` enforces this as
  a single named exception to its zero-modified-files check (SC-006 clause 2).
- **FR-048** **Tagging by measured cost (Q8).** Following the rule
  `bloom_engine_spectral_test.cpp:13-17` already applies in this repo: a case is tagged `[long]` **only
  when its measured runtime exceeds ~15 s AND its assertion is toolchain-independent** (a preset sweep,
  a MIDI/RNG golden, a click-free/bounded render — not a value whose pass/fail depends on compiler
  codegen). Candidates measured against this rule: SC-003's full render (the full-ring pre-roll,
  21.845 s, plus 10 min), SC-010 (b)'s 600 s render and SC-007 (d)'s 100 000-grain accelerated
  sweep. **SC-004 is not a candidate (amended 2026-09-23, R-8):** its assertions are NaN/Inf-guard and
  bounded-grid assertions — the phase's only reverse-path NaN/Inf and `kMaxLevel` check at the extreme
  ratio corner — which the closing sentence of this requirement forbids tagging at any measured cost.
  If SC-004 measures over 15 s it **splits** the way FR-049 splits SC-003: a short untagged arm
  (`AtmosphereGhost_ReverseTruncation`, the same corner configuration over fewer blocks, carrying the
  NaN/Inf, `kMaxLevel`, `lifetime >= 2` and lifetime-bound assertions on a handful of births) plus a
  `[long]` sweep sibling (`AtmosphereGhost_ReverseTruncation_Sweep`, the exhaustive per-block
  truncation sweep only), so the sentinel assertions stay in the per-push lane, which a tag would
  not keep. Every case's **measured runtime is recorded in
  the phase's compliance table**, tagged or not. Per the project's standing rule (`CLAUDE.md` Build
  Commands), a NaN/Inf-guard case, a bounded-grid case or a state-format case is **never** tagged
  `[long]` regardless of measured cost.
- **FR-049** **A reduced-duration per-push twin of SC-003 (Q8).** Alongside the full `[long]`
  `AtmosphereGhost_ReverseLiveness` case (SC-003), this phase ships a **second, per-push case**,
  `AtmosphereGhost_ReverseLiveness_Short` (untagged, runs every push), at the same Vorago ghost
  configuration and the same **full-ring pre-roll** (`getCaptureCapacitySamples()` = 1 048 576
  samples = 21.845 s at 48 kHz, `captureSeconds = 20`; amended 2026-09-23, R-2), but a **~30 s**
  measured span instead of 10 minutes. It keeps
  SC-003 (a)'s ring-cold-delta assertion (`coldStartSkips` unchanged across the measured span) so a
  genuine window-emptiness regression is still caught on every push, not only on the nightly `[long]`
  run; it does **not** repeat SC-003 (b)/(c) at full duration (those stay in the `[long]` case).
- **FR-042** **Test placement.** Every new case ships in TUs **added** to
  `dsp/tests/unit/systems/` and **enumerated** in `dsp/tests/CMakeLists.txt` beside `:373-376`. A TU
  that injects NaN/Inf bit patterns is registered in the `-fno-fast-math` list at `:878-896`; a TU
  that does not, is not — the rule `:888-895` states. No perf TU is ever added to that list.
- **FR-043** **Portability.** `node tools/check-portability.js` is clean. No new SIMD, so no
  aligned-load question arises. No `std::isnan`: non-finiteness is tested through the component's own
  `ITERUM_NOINLINE isFinite` (`:1269`) and, in tests, through `volatile` bit patterns.
- **FR-044** **Layer discipline.** `atmosphere_engine.h`'s include list (`:141-155`) gains **nothing**.
  `node tools/lint-layers.js` is clean, and the header banner's "deliberately ABSENT" list
  (`:17-25`) is unchanged and unextended.
- **FR-045** **Append-only bar.** The diff of `dsp/include/krate/dsp/systems/atmosphere_engine.h`
  deletes lines at **only** these six sites (amended 2026-09-23, R-4 and R-5: four in the reviewed
  spec, six now), each matched by anchor pattern:
  (i) the `wUp`/`wDown` pair at `:1651-1652` (they become direction-dependent, FR-014);
  (ii) the `advance` lambda **together with the whole of `renderGrainSpan`'s span body that calls
  it — pre-change `:1872-1880`, `:1884-1928`, `:1931-1941`, `:1943-1950` and `:1953-1966`, 87 lines,
  and no other range** (amended 2026-09-23, R-5 and **2026-09-24, B-4**). R-5 predicted
  `:1876-1882` / `:1888-1891` / `:1917-1930` (25 lines) on the argument that the direction hoist
  re-indents the two loops; the measured diff is **wider**, and the reason is the same one, carried
  further than R-5 carried it: the hoist wraps the *entire* span body — both loops, the cold-path
  branch, the Phase-1 stack arrays and index arithmetic, and the `accumulateGrainSpanSIMD` call — in
  a direction-templated lambda, so git reports every re-indented line as a deletion. `git diff HEAD
  -w` over the same range shows the only substantive change inside the loops is
  `advance();` → `advanceBy.template operator()<kBackwards>();`. B-4 therefore restates the range as
  measured rather than leaving a requirement whose stated site list its own implementation exceeds.
  The lambda gains the backwards form (FR-011), the span body the per-span direction selection
  (FR-013). **The codegen banner at `:1871-1875` is inside the amended range and 4 of its 5 lines are
  deleted** — R-5's "may be extended but not deleted" is superseded by B-4 for the same reason: the
  banner documents the `advance` lambda, which no longer exists in that form, and the replacement
  carries the same truncation argument at the templated lambda;
  (iii) the pass-A scheduler tick — the scheduling comment at `:2110-2114` **and** the
  `if (scheduler_.process())` block at `:2116-2135`, 25 lines (amended **2026-09-24, B-4**; the
  reviewed text named `:2115` alone, which is the `if` line itself and is *context*, not deleted).
  The block gains the trigger consumption beside it (FR-020) and the named-lambda form of FR-022's
  consumption that SC-006 clause 6 anchor 2's token rule requires;
  (iv) the `setSeed`/`prepare`/`reset` seeding lines at `:552-556` and `:1015-1019` (they gain the
  fifth stream, FR-005);
  (v) `prepare()` step 5b at `:436-441` — the two pass-A scratch `assign` lines and their derivation
  comment (they take `kMaxGrains * 3` and the two-births-per-sample derivation, FR-051; amended
  2026-09-23, R-4);
  (vi) the two shipped comments this phase falsifies (amended 2026-09-23, R-4): the pass-A scratch
  declaration comment at `:2595-2596` ("Sized once in prepare() (2 * kMaxGrains each) …", made false
  by FR-051) and the `readFrac` field comment at `:1183` ("fraction in [0,1)", made false by FR-012's
  amended invariant) — left as shipped, the header would document a real-time bound and a range the
  code no longer maintains.
  FR-050's admission clause is a pure insertion and consumes no site.
  A deletion anywhere else means the change stopped being append-only.
  **The diff range is stated once, here, and every clause that invokes it uses this one range.**
  `tools/check-seraphis-green.js` already fixes it: every check it runs is `git diff HEAD --numstat`
  (`check-seraphis-green.js:77`, implemented at `:152-155`), i.e. the **working tree against `HEAD`**,
  which is the pre-commit form. This phase's base commit is
  `374580d7d0f0631561413310bd3085e15ba7279c`, so:
  - **before the phase's work is committed**, `git diff HEAD --numstat -- <path>` is the range, and it
    is what the script runs and what SC-006 clauses 1, 2, 5 and 6 assert;
  - **after the work is committed**, the equivalent range is
    `git diff --numstat 374580d7d0f0631561413310bd3085e15ba7279c..HEAD -- <path>`, and the compliance
    record states the figures for that form.
  A bare `git diff --numstat <path>` (working tree vs index) is **never** the range: after a commit it
  reports nothing and would pass vacuously.
  `tools/check-seraphis-green.js` gains an `atmosphere_engine.h` entry in its `APPEND_ONLY_HEADERS`
  table (SC-006 clause 2) with the same `{file, what, expected: [{count, name, pattern}]}` shape its
  existing `multi_stage_envelope.h` / `growth_envelope.h` entries use — the real rule objects, at
  `check-seraphis-green.js:102-117` (the file's `:40-47` is the *prose* comment describing check 3,
  not the rule table). It inherits the script's `HEAD` range unchanged, so the script and this
  requirement cannot drift apart.
- **FR-050** **Reverse fill-up admission clause (added 2026-09-23, R-1; plan P-1).** The shipped
  FR-014 admission test at `:1736-1742` — `oldestAge = min(birthAge + decorr, capacity − 2 − guard)`,
  `needed = ceil(oldestAge) + guard`, reject iff `getAvailableSamples() < needed` — tests the ring
  **at the birth sample only**. That is sufficient forward (an `r > 1` grain's age shrinks; an
  `r < 1` grain's age grows at `1 − r < 1` while `available` grows at 1) and **not** sufficient
  backwards: a reverse age grows at `1 + r` per sample while `available` grows at 1 until it
  saturates at `capacity`, so the deficit gains on the ring at `r` per sample. Worked at the Vorago
  ghost point (48 kHz, `captureSeconds = 20` → `C = 1 048 576`, `grainSeconds = 12` →
  `L' = 576 000`, `pitchSemitones = −12` with `driftRange 2` → `ratioMax = 2^(−10/12) = 0.5612`): a
  grain born after a 5 s pre-roll (`A = 240 000`) at the minimum birth age 64 has
  `a(t) = 64 + 1.5612·t` against `available(t) = 240 000 + t`; the two cross at `t ≈ 427 000`
  samples, **≈ 8.9 s into the 12 s grain**, after which `LinearReader::index0` clamps the age at
  `maxAge_` (`rolling_capture_buffer.h:313-321`) and the grain reads **one held sample for its
  remaining ~3 s** — a DC pedestal under the envelope. No NaN, no counter moves, and
  `getMin/MaxObservedGrainAgeSamples()` fold the *computed* age (`:1800-1809`, `:1860-1869`), which
  stays inside `[64, C − 2]`, so no criterion of the reviewed spec could see it. Requirement:
  `tryBirthGrain()` gains **one additive, reverse-only admission clause**, inserted immediately after
  the shipped test, of exactly this form:
  ```cpp
  if (reversed) {
      const double avail = static_cast<double>(capture_.getAvailableSamples());
      const double fillDeficit =
          std::ceil(static_cast<double>(ratioMax) * std::min(lifetime, capacity - avail));
      if (avail < needed + fillDeficit) { ++skipRingCold_; return; }
  }
  ```
  i.e. the one extra quantity is the deficit accumulated at `ratioMax` up to saturation,
  `t* = min(lifetime, capacity − available)`, after which the FR-014 window (`wDown = 1 + ratioMax`)
  already bounds the whole life. The clause is **vacuous on a full ring** (`t* = 0`), is **never
  evaluated for a forward grain**, keeps every shipped admission decision bit-identical, counts its
  rejection on the existing `skipRingCold_` (so FR-022's consumed-once accounting is unchanged), and
  is a pure insertion that deletes nothing (it consumes no FR-045 site). Its binding criterion is
  **SC-011**. **No green-path fixture can bound the stale read**: admission under this predicate is
  the negation of the crossing condition, so admitted-implies-safe is a theorem (plan §A-5), and the
  protected quantity is observed only in SC-011's clause-disabled measurement.
- **FR-051** **Pass-A scratch sizing for two birth sites per sample (added 2026-09-23, R-4; plan
  P-5).** `prepare()` step 5b (`:436-441`) sizes `retiredScratch_` and `dueScratch_` at
  `kMaxGrains * 2` from a stated one-birth-per-sample argument ("<= kMaxGrains grains active at a
  chunk start plus <= kControlChunkSamples births can retire inside one chunk"). FR-020 adds a
  **second** birth attempt at the same sample index (the scheduler tick, then the trigger
  consumption), so the newborn term becomes `<= 2 * kControlChunkSamples` and `2 * kMaxGrains` no
  longer bounds either vector. Both are written by **unchecked index on the audio thread** —
  `retiredScratch_[retiredCount]` in `bookkeepingRetire` (`:2064-2066`) and `dueScratch_[k]` in the
  newborn insert (`:2128-2134`), both `std::vector` at `:2597-2598` — so an overflow is an
  out-of-bounds write, not a rejected birth. It is reachable wherever the density scheduler can fire
  on every sample, i.e. `sampleRate <= kMaxDensity = 20` (`:304`; `prepare()` floors the rate at 1.0
  only, `:410`). Requirement: both `assign` calls take **`kMaxGrains * 3`** and the derivation comment
  is restated for two births per sample, with the tight bound stated so no later reader re-derives
  it: with `numSamples <= kControlChunkSamples = 64` and `lifetime >= 2` (`:1696-1699`) a newborn can
  insert a due entry only from positions `i <= numSamples − 2`, i.e. **63** positions × 2 births =
  126 newborn entries, plus `<= kMaxGrains = 64` grains already active at the chunk start, for
  **190 <= 3 * kMaxGrains = 192**; `retiredScratch_` is bounded by the same count (a retirement is a
  consumed due entry or an end-of-chunk drain of one). The declaration comment at `:2595-2596`
  changes with it (FR-045 site (vi)). **The alternative was considered and rejected:** consuming a
  pending trigger only on samples where `scheduler_.process()` did *not* fire would keep the shipped
  sizing proof intact but defers a trigger to a later sample and so breaks FR-020's deterministic
  "N calls produce at most N grains **within the next N rendered samples**" — a spec requirement
  traded for a diff budget. Its binding criterion is **SC-012**; SC-008 (a)'s `AllocationScope`
  cannot stand in for it, because writing past a `std::vector`'s size allocates nothing.

---

## Success criteria

Every criterion names the TU case it ships in. Figures are **measured and transcribed**, never
hand-edited; no threshold in this section may be relaxed and no workload shrunk to make one pass.

**Pre-rolls (amended 2026-09-23, R-2).** Every "warm ring" / "full ring" pre-roll in this section is
the **full-ring rule**: from a fresh `prepare()`/`reset()`, render exactly
`engine.getCaptureCapacitySamples()` samples (`atmosphere_engine.h:1133-1137`). The ring's own
`getAvailableSamples()` is `std::min(samplesWritten_, capacity_)` (`rolling_capture_buffer.h:443-445`),
so after that many rendered samples it is saturated and stays there; and because
`capacity_ = nextPowerOf2(sampleRate × captureSeconds)` (`rolling_capture_buffer.h:75-93`), the ring
is **not** `captureSeconds` of audio — a pre-roll stated in seconds is wrong by construction. No
fixture states a pre-roll in seconds; each is `getCaptureCapacitySamples()` samples with the seconds
recorded as measured: at 48 kHz, `captureSeconds = 20` (SC-001, SC-003, FR-049's twin, and SC-011's
configuration, whose (a) arm deliberately stops short of it) `nextPowerOf2(960 000) = 1 048 576`
samples = **21.845 s**; SC-010 (b) at 8 kHz, `captureSeconds = 20`: `nextPowerOf2(160 000) =
262 144` samples = **32.768 s**; SC-004 at 48 kHz, `captureSeconds = 1`: `nextPowerOf2(48 000) =
65 536` samples = **1.365 s**. A full ring makes FR-050 vacuous (`t* = 0`), so every fixture below
except SC-011 tests the shipped admission arithmetic; SC-011 is the one case that deliberately
renders the filling regime. Where a case needs the *available* count during filling it computes
`std::min(samplesRendered, getCaptureCapacitySamples())` — `AtmosphereEngine` exposes no
`getAvailableSamples()` and this phase adds none.

- **SC-001 — Default-inert, two independent ways.** `AtmosphereGhost_DefaultInert`.

  **How the base-commit reference reaches the test — decided, not left open.** A Catch2 case cannot
  build and run the base-commit code, so clauses 1 and 2 use **stored references transcribed into the
  new TU as named constants**, not an out-of-band two-worktree comparison. The mechanism is the one
  `vorago_perf_test.cpp:236-253` already applies to its own baselines: the values are **measured
  once, from a worktree checked out at base commit `374580d7d0f0631561413310bd3085e15ba7279c`, running
  the identical render fixture**, and transcribed with a provenance comment naming that SHA, the
  fixture function and the date. The comment states in the same words the perf TU uses that they are
  **stored references, re-measured under a documented rule and never hand-edited** — a failure is a
  finding, not an invitation to update the literal. An out-of-band comparison is rejected for this
  phase's purpose because no later CI run re-executes it, so it would protect nothing the moment the
  phase landed.
  1. **Render identity across the change — the `noise_organism` measured-bounds protocol (Q4).** A
     60 s deterministic render at the Vorago ghost configuration (FR-017's seven values,
     `captureSeconds = 20`, seed 1, a fixed pink-noise-plus-tone excitation), compared with
     `tests/test_helpers/render_fingerprint.h` against the **transcribed base-commit
     `RenderFingerprint`** (its `rms`, `peak`, `meanAbs`, `totalVariation` and 32 `checkpoints`,
     `render_fingerprint.h:63-69`, written out as a `constexpr` initialiser).
     **Per FR-046, this comparison uses MEASURED per-comparison bounds, not the default
     `kMetricTolerance`/`kSampleTolerance`**, following the protocol
     `noise_organism_test.cpp:3288-3406` already ships in this repo: a documented **three-toolchain
     probe** (MSVC measured directly; GNU and LLVM read from the CI legs or a local WSL run), whose
     **worst observed deviation across all three toolchains, with headroom,** sets the checkpoint
     bound and the metric bound this TU actually checks;
     `compareFingerprints(actual, kBaseCommitFingerprint).withinTolerance()` (`render_fingerprint.h:
     108-110` — the member is `withinTolerance()`; there is **no** `passes()`) is evaluated at those
     measured bounds; a **paste-ready literal is printed on failure**; and the stored fingerprint
     carries a **PROVENANCE block** — base commit `374580d7d0f0631561413310bd3085e15ba7279c`, the
     measuring machine, the compiler/toolchain, and the date — beside the `constexpr` initialiser.
     **No bit-exact float golden** (roadmap line 605): the comparison stays tolerance-based, at
     measured rather than default bounds. **The bounds are recorded in this spec once measured during
     the build stage; no bound may be widened afterwards without a ruling** (FR-046).
  2. **RNG-stream identity.** Two arms, both integer identities (never float goldens), because ADR-2's
     structural claim needs an exact check: an implementation that drew the reverse bit from
     `grainRng_` must fail here even if clause 1's tolerances happened to absorb it.
     - **No-trigger arm.** `getGrainRngState()` (`:1118`) after that same render is **equal as an
       integer** to `kBaseCommitGrainRngState`, a transcribed `std::uint32_t` under the same
       provenance rule, at probability 0 and at probability 1.
     - **Trigger arm — a computed-delta replica, not a base-commit literal (Q3)**, because the base
       commit has no `triggerGrain()` and so no matching reference render exists: fire 100 triggers on
       a **warm ring** (the pre-roll technique SC-003/SC-005 (b) use, so no `skipRingCold_` increments
       occur across the measured span). A replica `Xorshift32`, seeded with
       `deriveStreamSeed(seed, kGrainSalt)` and held only by the test, is advanced **exactly four
       draws per observed admitted birth** — the birth count read from `getTotalGrainsBorn()`
       (FR-027 pins this as `tryBirthGrain`'s per-attempt draw contract, valid here because the warm
       ring makes attempted and admitted birth counts equal). REQUIRE the replica's final state equals
       `getGrainRngState()` after the run. This catches a triggered birth that drew a different number
       of times from `grainRng_`, which the no-trigger arm cannot see.
  3. **Counter identity.** `getTotalGrainsBorn()`, `getTotalGrainsRetired()`,
     `getSkippedTriggerCountPoolFull()`, `getSkippedTriggerCountRingCold()` (`:1053-1070`) and
     `getLatencySamples()` (`:1128`) are unchanged across the change at probability 0 with no trigger.
- **SC-002 — Reverse grains are measurably time-reversed against the capture.**
  `AtmosphereGhost_ReverseIsTimeReversed`.

  **Configuration, stated completely because every clause below depends on it.** 48 kHz,
  `captureSeconds = 4`, seed 1, `setGrainReverseProbability(1.0)`, `setPitchSemitones(0)`,
  `setPitchSpread(0)`, `setDriftRangeSemitones(0)`, `setDriftDepth(0)` — so `ratio == 1.0` exactly for
  the grain's whole life — `setPositionSeconds(0)` and `setPositionSpread(0)` — so the birth age is
  the `kMinAgeSamples = 64` guard (`:251`) and is known, not drawn — `setDecorrelation(0)`,
  `setPanSpread(0)`, `blurEnabled = false`, `freezeEnabled = false`, `setLevel(1.0)`,
  `setJitter(0)`, `setDensity(kMinDensity = 0.1)` (`:303`), `grainSeconds = 0.5`.
  **Excitation:** a linear chirp **200 Hz → 4 kHz over exactly 1.0 s**, i.e. a sweep rate of
  **3800 Hz/s**, written into the ring so that its midpoint coincides with the trigger sample `T`:
  the chirp covers source times `[T − 0.5 s, T + 0.5 s]`. Outside that window the excitation is
  silence. One grain is born by `triggerGrain()` at `T`.

  **Why that window, derived here so the thresholds below are visibly reachable.** At `ratio == 1.0`
  the read position walks backwards by exactly one source sample per output sample (FR-011), so a
  reverse grain born at age 64 traverses source `[T − 0.5 s, T]` **backwards, 1:1** over its 0.5 s
  life. A forward grain at `ratio == 1.0` holds its age constant (`ratio − 1 == 0`, ADR-1) and
  traverses source `[T, T + 0.5 s]` **forwards, 1:1**. The chirp therefore covers exactly what each
  direction reads, at 1:1, with no resampling to model. Expected spectral-centroid slopes:
  **−3800 Hz/s** reverse and **+3800 Hz/s** forward — so clause (b)'s **500 Hz/s** floor carries a
  **7.6×** margin and is visibly a floor, not a number that might sit above the achievable value.

  **How the grain's output is isolated, and how the isolation is asserted.** `AtmosphereEngine`
  exposes no per-grain output: `processStereoBlock` emits the mixed grain bus after the FR-028
  population gain and the FR-061 level smoother. Isolation is therefore obtained by arranging that
  **exactly one grain is alive** across the measured span, and *asserted*, never assumed:
  1. render the pre-roll (the first half of the chirp plus whatever the density scheduler produces);
  2. **wait until `getActiveGrainCount() == 0`** before calling `triggerGrain()` — the density
     scheduler at `kMinDensity` fires roughly every 10 s and `samplesUntilNextGrain_` starts at
     `0.0f` (`grain_scheduler.h:40`, tested at `:73-76`), so the first render sample always births
     one and it must be allowed to retire;
  3. snapshot `getTotalGrainsBorn()`, call `triggerGrain()`, render the grain's life one
     64-sample block at a time;
  4. **REQUIRE `getActiveGrainCount() == 1` at every block boundary of the measured span** and
     `getTotalGrainsBorn()` to have advanced by **exactly 1** across it, and take the grain's span
     from `getLastBornGrainLifetimeSamples()` (`:1112-1114`).
  If a scheduler birth collides with the measured span the case **fails loudly** rather than
  measuring a sum. The fixture's seed is chosen so it does not; a failure here is a fixture defect to
  fix, never a reason to widen (a) or (b).

  Two clauses, both on that isolated span:
  (a) the normalised cross-correlation peak against the **Hann-windowed time-reversed** reference
  segment (source `[T − 0.5 s, T]`, reversed, windowed with the same Hann the grain envelope applies)
  is **≥ 0.90**, and against the identically-windowed **forward** segment (source `[T, T + 0.5 s]`) is
  **≤ 0.30**;
  (b) the STFT spectral-centroid trajectory over the grain's life has a **negative** least-squares
  slope for the reverse grain and a **positive** one for the same grain born at probability 0, with
  `|slope|` at least **500 Hz/s** in both (expected ±3800 Hz/s, above). Clause (b) exists because
  clause (a) alone is satisfiable by a symmetric artefact.
  Plus the **endpoint guarantee relocated here from SC-003 (b) (amended 2026-09-23, R-10)**: on the
  isolated reverse span, `REQUIRE(span.front() == 0.0f && span.back() == 0.0f)` — exactly and
  bit-wise, the envelope endpoint guarantee at banner `:96-101` — asserted here because this is the
  one protocol in the phase where a single grain reaches the output alone (`getActiveGrainCount() ==
  1` at every block boundary), which SC-003's ~3.6 concurrent grains on a summed bus never permit.
- **SC-003 — Reverse grains stay inside the ring and never click.**
  `AtmosphereGhost_ReverseLiveness`.
  (a) The configuration is the Vorago ghost operating point in full: probability 1,
  `density = 0.30` grains/s (the FR-017 value, `vorago_perf_test.cpp:620`), `grainSeconds = 12`,
  `captureSeconds = 20`, `pitchSemitones = −12`, `positionSpread = 0.9`, `decorrelation = 0.85`,
  48 kHz, seed 1.
  **The clause is a warm-ring clause, and says so.** A cold ring rejects births by construction and
  has nothing to do with reverse: `GrainScheduler` initialises `samplesUntilNextGrain_ = 0.0f`
  (`grain_scheduler.h:40`) and `process()` decrements then tests `<= 0.0f` (`:73-76`), so the **first
  sample of the first render** attempts a birth with `capture_.getAvailableSamples() == 1`, and the
  admission test at `atmosphere_engine.h:1736-1741` increments `skipRingCold_`. `skipRingCold_` is
  monotonic until `reset()`, and `reset()` also empties the ring, so it cannot be cleared into a warm
  state. The criterion is therefore stated as a **delta**:
  - render the **full-ring pre-roll** first (amended 2026-09-23, R-2): exactly
    `getCaptureCapacitySamples() = nextPowerOf2(20 × 48 000) = 1 048 576` samples = **21.845 s**, not
    the reviewed draft's 5 s. Two reasons, both load-bearing. The shipped test needs 3.12 s —
    `ageHi ≈ 148 034` samples plus the maximum `decorrAge` (`kMaxDecorrelationMs = 30` at `:315`,
    = 1 440 samples) plus the `kMinAgeSamples = 64` guard, = 149 538 samples = 3.12 s — but FR-050's
    fill clause needs more: at this operating point its deficit is the constant
    `ceil(0.561231 × 576 000) = 323 270` (the `t* = L'` branch binds for every `A < C − L' =
    472 576`), so the first reverse birth is admissible only from `A = 323 398` samples = **6.74 s**
    at the minimum birth age and `A = 472 574` = **9.85 s** at the worst legal one; a 5 s pre-roll
    (`A = 240 000`) rejects *every* reverse birth and the delta assertion below fails on correct
    code. A full ring makes FR-050 vacuous (`t* = 0`), so what this clause tests is the shipped
    window arithmetic, and after it no admission can fail for want of history;
  - snapshot `coldStartSkips = getSkippedTriggerCountRingCold()` at the end of the pre-roll and
    REQUIRE **`coldStartSkips ≤ 9`** (amended 2026-09-23, R-2: 3 → 9, recomputed from the real
    pre-roll duration). That bound is the number of scheduler ticks the pre-roll can contain and is
    what keeps a genuine window-emptiness regression detectable: the free tick at sample 0
    (`samplesUntilNextGrain_ = 0.0f` in `reset()`, `grain_scheduler.h:40`, decremented *before* the
    `<= 0.0f` test in `process()`, `:73-76`) plus `floor(21.845 / 2.5) = 8` more, 2.5 s being the
    shortest interval `density = 0.30`, `jitter = 0.5` permits — `(1/0.30) × (1 − 0.5 × 0.5) = 2.5 s`
    (`grain_scheduler.h:78-88`, interonset at `:102`) — i.e. `1 + floor(21.845 / 2.5) = 9`;
  - then render **10 minutes** and REQUIRE `getSkippedTriggerCountRingCold() == coldStartSkips`,
    i.e. the counter **does not advance at all** over the 10 minutes — the birth window is non-empty
    at the Vorago operating point.

  The supporting arithmetic, stated so a regression is diagnosable: at 48 kHz,
  `capacity = nextPowerOf2(20 × 48000) = 1 048 576` (`rolling_capture_buffer.h:76-88`),
  `ratioMax = ratio(−12 + 2 st) = 0.5612`, `w = 1 + ratioMax = 1.5612`,
  `w × requested = 1.5612 × 576 000 = 899 251 ≤ slack ≈ 1 047 220`, so **no truncation occurs** and
  `ageHi ≈ 148 034` samples ≈ 3.08 s (at maximum decorrelation) comfortably contains the `positionSeconds = 1.0` ± 0.9 birth
  window (`:2670`).
  (b) **Click freedom, as a relative comparison (amended 2026-09-23, R-10).** The reviewed draft's
  per-grain clause — "the first and last emitted sample of every reverse grain is exactly `0.0f`" —
  is **deleted from this criterion**: `processStereoBlock` emits only the summed grain bus after the
  FR-028 population gain (`:2171-2192`) and the FR-061 level smoother, and at `density = 0.30 ×
  grainSeconds = 12` there are ~3.6 concurrent grains, so no endpoint of any individual grain reaches
  the output here. The endpoint guarantee (banner `:96-101`) is asserted where a single grain *is*
  isolated — SC-002's one-grain protocol at `ratio = 1.0`, bit-wise on `span.front()` and
  `span.back()`. What remains here is the comparison: the Seraphis Phase 5 click detector at the
  sigma the forward liveness cell of `atmosphere_engine_test.cpp` uses (helper and sigma cited by
  file:line in the compliance record; copied with a citation if that cell's detector is local, never
  re-invented) reports **no additional detections at probability 1 versus probability 0** over the
  same render — a *relative* bound, so a detector-sigma mismatch cannot make it vacuously true.
  (c) Every read age observed over that render satisfies `kMinAgeSamples ≤ age ≤ capacity − 2`
  (`getMinObservedGrainAgeSamples()` / `getMaxObservedGrainAgeSamples()`, `:1099-1103`) — FR-015.

  **Tagging (Clarifications 2026-09-22 Q8; FR-048/FR-049; amended 2026-09-24, B-5).** This case
  (the full-ring pre-roll plus 10-minute render) was tagged `[long]` on the ESTIMATE. **Measured, it
  is 6.990 s** — the 10 minutes is audio, not wall-clock — so FR-048's own rule ("only when its
  measured runtime exceeds ~15 s") takes the tag off and the case runs on every push. Its measured
  runtime is recorded in the compliance table. A
  reduced-duration twin, `AtmosphereGhost_ReverseLiveness_Short` (FR-049), runs **untagged on every
  push**, at the same full-ring pre-roll (R-2), keeping clause (a)'s ring-cold-delta assertion over a
  **~30 s** measured span instead of 10 minutes.
- **SC-004 — Truncation is correct at the extremes.** `AtmosphereGhost_ReverseTruncation`. At
  `captureSeconds = 1` (`kMinCaptureSeconds`, `:317`), `grainSeconds = 30` (`kMaxGrainSeconds`,
  `:302`), `pitchSemitones = +24` with `pitchSpread = 1` and `driftRangeSemitones = 12`, probability
  1, `density = kMinDensity = 0.1` (`:303`), the ring pre-rolled full — `getCaptureCapacitySamples()
  = nextPowerOf2(48 000) = 65 536` samples = **1.365 s** (amended 2026-09-23, R-2), so FR-050 is
  vacuous and the shipped truncation alone sets the lifetime: no birth is ever admitted with
  `lifetime < 2`
  (`:1696-1699`), every admitted grain's `getLastBornGrainLifetimeSamples()` (`:1112-1114`) satisfies
  `lifetime ≤ floor(slack / (1 + ratioMax))` recomputed in the test from the same inputs, and the
  render contains **no NaN, no Inf** (bit-pattern check) and **no sample above** `kMaxLevel = 2.0`
  (`:316`) in magnitude.

  **The render granularity that makes "every admitted grain" true, stated rather than assumed.**
  `getLastBornGrainLifetimeSamples()` reports the **most recent** birth only, and pass-A births are
  per-sample (`:2115-2135`), so a block-driven render that sampled the accessor once per block would
  verify a subset while claiming to verify all of them. The case therefore:
  - drives the render in **64-sample blocks**, reading the accessor after every block;
  - drives births by `triggerGrain()` at **at most one call per block**, with the density scheduler at
    `kMinDensity` (one tick per ~10 s, i.e. per ~7 500 blocks);
  - REQUIREs `getTotalGrainsBorn()` to advance by **at most 1 per block**, so every birth is observed
    at the accessor exactly once. A block that sees two births (a scheduler tick landing in the same
    64 samples as an admitted trigger) **fails the case** as a fixture defect — it is not silently
    skipped;
  - REQUIREs, at the end, that the **number of accessor observations equals the total number of
    births** over the render, which is the assertion that closes "every admitted grain".

  **Tagging (Q8, FR-048; amended 2026-09-23, R-8).** **Never tagged `[long]`**: its assertions are
  NaN/Inf-guard and bounded-grid assertions, which FR-048's closing sentence and `CLAUDE.md`'s Build
  Commands rule keep in the per-push lane at any measured cost. If its measured runtime exceeds ~15 s
  it **splits** as FR-049 splits SC-003 — `AtmosphereGhost_ReverseTruncation` (untagged: the same
  corner over fewer blocks, carrying the NaN/Inf, `kMaxLevel`, `lifetime >= 2` and lifetime-bound
  assertions on a handful of births) plus `AtmosphereGhost_ReverseTruncation_Sweep` (`[long]`: the
  exhaustive per-block truncation sweep only). The measured runtime is recorded in the compliance
  table either way.
- **SC-005 — One grain per trigger call, bounded by the pool.**
  `AtmosphereGhost_TriggerAccounting`. All clauses at density `kMinDensity = 0.1`
  (`:303`) so the density scheduler contributes at most one grain over each window.
  (a) **Trigger accounting is asserted on the trigger-only counter.** N ∈ {1, 5, 64} calls followed by
  a render of ≥ N samples **on a warm ring** produce **exactly N** additional
  `getTotalTriggeredGrainsBorn()` increments — that counter is trigger-only by FR-026, so "exactly N"
  is a real equality there. `getTotalGrainsBorn()` is bounded rather than equated:
  **N ≤ Δ`getTotalGrainsBorn()` ≤ N + 1**, the slack being the one density-scheduler birth the
  preamble already admits is possible over the window. (The reviewed draft asserted "exactly N" on
  both counters, which contradicted its own preamble and made the verdict an accident of where the
  seeded scheduler happened to fall.)
  (b) **The pool bound, in two arms, because the cold arm cannot reach it.**
  - **Cold arm (no prior render).** `kMaxGrains + 10 = 74` calls before any render: 64 are queued and
    10 are dropped at the saturating counter, then the render consumes all 64 into an **empty** ring
    (`writeCounter_` starts at 0, so `getAvailableSamples()` cannot meet
    `ceil(oldestAge) + guard`, `:1736-1741`). The assertions are therefore **stated, not hidden
    behind an "at most"**: `getDroppedTriggerCount() == 10` (FR-019), `getTotalGrainsBorn() == 0`,
    and `getSkippedTriggerCountRingCold() == 64`. This arm tests the **pending-queue** saturation
    and the cold-ring path; it does **not** reach the pool cap, and does not claim to.
  - **Warm arm (the arm that actually tests the pool bound, roadmap line 509).** Render past
    `ageHi` first so the ring is warm, record `k0 = getActiveGrainCount()` and the three counters,
    then fire `kMaxGrains + 10` calls and render 64 samples. Assertions:
    Δ`getTotalGrainsBorn() == kMaxGrains − k0`; Δ`getSkippedTriggerCountPoolFull() == k0`;
    Δ`getDroppedTriggerCount() == 10`; `getActiveGrainCount() == kMaxGrains` afterwards. Then fire
    **10 further** calls and render: Δ`getTotalGrainsBorn() == 0`,
    Δ`getSkippedTriggerCountPoolFull() == 10`, `getActiveGrainCount()` still `kMaxGrains` — the pool
    cap holding under continued pressure, with **no** grain stolen (FR-021, `:1609-1612`).
  (c) calls made into a **cold ring** are consumed exactly once: `getTotalGrainsBorn()` does not move,
  `getSkippedTriggerCountRingCold()` advances by the number consumed, and a later render with a warm
  ring produces **no** deferred burst (FR-022);
  (d) `triggerGrain()` after `silence()` has latched (`getActiveGrainCount() == 0` and the output is
  exactly `0.0f`) produces **no** grain and moves **no** counter, and `reset()` clears the pending
  queue (FR-023, FR-024).
- **SC-006 — Seraphis stays green, with git-diff evidence.** `AtmosphereGhost_AppendOnly` plus full
  suite runs. **Every clause below uses FR-045's single stated range** — `git diff HEAD …` before the
  phase's work is committed (the form `tools/check-seraphis-green.js:77`, `:152-155` runs), and
  `git diff 374580d7d0f0631561413310bd3085e15ba7279c..HEAD …` after. A bare `git diff --numstat
  <path>` is not the range and would pass vacuously post-commit. Six clauses:
  1. `git diff HEAD --numstat --diff-filter=M -- dsp/tests/unit/systems/` names **zero** files
     **except `vorago_perf_test.cpp`** (FR-047, Clarifications 2026-09-22 Q7) — and that file's only
     permitted modification is replacing its seven constant definitions (the FR-047 four —
     `kReferenceNs`, `kEngineMeasuredNsAtPoly4`, `kEngineBaselineNsAtPoly4`,
     `kCavernMeasuredNsPerBlock` — plus `kReferenceNs`'s derivation inputs `kSr48`, `kBlockSize`,
     `kBlockBudgetNs`; amended 2026-09-23, R-3) with an
     include of the new `vorago_perf_budget.h`; every other line of it is unchanged, and its own
     `static_assert`s on those constants stay in place unedited. Every TU this phase adds in that
     directory, including `vorago_perf_budget.h` itself, is `--diff-filter=A`.
     `tools/check-seraphis-green.js` enforces the single named exception rather than the bare
     zero-files rule.
     (The Phase-10 form of this clause had to enumerate four modified TUs — and its script's check 1
     carries a conditional "all four must be present" clause, `check-seraphis-green.js:27-34`, which
     is why the range must be pinned; this phase modifies only the one file, at one documented site,
     which is a stronger bar than Phase 10's, not a looser one.)
  2. `git diff HEAD --numstat -- dsp/include/krate/dsp/systems/atmosphere_engine.h` deletes lines at
     **only** FR-045's six anchor sites (amended 2026-09-23, R-4 and R-5, and
     2026-09-24, B-4, to the MEASURED ranges — FR-045 carries them), verified by `git diff HEAD -U0` and by the new
     `tools/check-seraphis-green.js` `APPEND_ONLY_HEADERS` entry, which takes the script's own `HEAD`
     range so the two cannot drift apart.
  3. The full `dsp_systems_tests` suite passes, including **every** `atmosphere_engine_*` and
     `seraphis_*` case, with **no Seraphis or Phase-5 assertion, threshold, baseline or default
     modified**. `AtmosphereEngine_NonFiniteGuardSurvivesFastMath` (the `-fno-fast-math` exclusion at
     `dsp/tests/CMakeLists.txt:888-895`) is among them and is not edited.
  4. `dsp_effects_tests`, `dsp_processors_tests` and the Vorago cases in `dsp_systems_tests`
     (`vorago_*`) pass unchanged; Phase 10's `SC-027` case `VoragoEngine_GhostConfiguration` is green
     with **no edit** (FR-033).
  5. The shipped plugin: `git diff HEAD --numstat --diff-filter=M -- plugins/seraphis/` names **zero**
     files; the `Seraphis` target and `seraphis_tests` build with **zero warnings**, `seraphis_tests`
     is green, and `tools/pluginval.exe --strictness-level 5 --validate` passes on the rebuilt bundle.
  6. **The structural gate for FR-013 and FR-025**, which no timing criterion in this phase can
     resolve (FR-025 states why). `git diff HEAD -U0 -- dsp/include/krate/dsp/systems/atmosphere_engine.h`
     shows, at the two anchors named here:
     - at `renderGrainSpan` (`:1829`): the `grain.reversed` test appears **above** the per-sample
       loop — a direction-selected span form, or a hoisted direction variable — and **zero**
       occurrences of `reversed` appear between the loop's `for` and its closing brace;
     - at the pass-A loop (`:2115`), stated as a token rule (amended 2026-09-23, R-7 — "zero
       additional loads on the path taken when it is zero" is a semantic property no reviewer can
       discharge from a `-U0` excerpt): the loop-invariant `anyPending` (`= pendingTriggers_ > 0u`)
       is declared **above** the per-sample `for`, and `pendingTriggers_` appears in the per-sample
       body **exactly once**, as the **right operand of a short-circuited `&&` whose left operand is
       (Clarified 2026-09-24 at compliance: the FR-022 decrement `--pendingTriggers_` lives in the
       `birthAndTrack` lambda defined ABOVE the loop, called from the taken branch; it is not an
       occurrence in the per-sample body, so the token rule reads exactly one.)
       `anyPending`** — so the zero path performs no load of the counter, by construction.

     This is a code-review gate on a named diff, not a prose assurance: it names the command, the
     range, the two anchors and the thing counted, so a reviewer either can or cannot produce the
     evidence. It is recorded in the phase's compliance table with the `git diff HEAD -U0` excerpt
     quoted, not paraphrased.
- **SC-007 — Seeding and determinism.** `AtmosphereGhost_Determinism`.
  (a) Two engines at the same seed, same configuration and same trigger schedule produce renders whose
  `compareFingerprints(...).withinTolerance()` (`render_fingerprint.h:108-110`) is true at the default
  tolerances, at probability 0, 0.5 and 1.
  (b) Two engines at **different** seeds at probability 0.5 produce
  `worstMetricRelativeError > 100 × kMetricTolerance` (the Phase-10 SC-026 separation form).
  (c) `setSeed(s)` mid-render re-seeds `reverseRng_` and leaves every live grain's `reversed` flag
  standing (FR-008); `getReverseRngState()` after `reset()` equals its value after `prepare()` at the
  same seed.
  (d) Over a 100 000-grain accelerated run at probability `p ∈ {0.25, 0.5, 0.75}`, the measured
  reverse fraction `getTotalReverseGrainsBorn() / getTotalGrainsBorn()` is within **±0.02** of `p`
  (a 3σ band for that sample count is ±0.005, so this is a calibration check, not a tight RNG test).
  Tagged `[long]` on the estimate (Q8, FR-048) and **untagged 2026-09-24 (B-5) on the measurement:
  1.361 s**. Measured runtime recorded in the compliance table.
- **SC-008 — RT safety and setter contracts.** `AtmosphereGhost_RtSafety`.
  (a) `getAllocatedBytes()`-equivalent invariance: no allocation occurs during any render, at any
  probability, with any trigger pattern — asserted with the suite's `AllocationScope` detector, the
  same instrument Phase 10's SC-014 uses.
  (b) `setGrainReverseProbability` with NaN and ±Inf (bit patterns through a `volatile`)
  **substitutes `0.0f`** — a NaN argument to `setGrainReverseProbability(1.0f)` reads back `0.0f` via
  the getter, not the prior `1.0f` — matching `setGrainSeconds`/`setDensity`/`setLevel`'s shipped
  substitution shape; with `−1.0` and `+2.0` it clamps to `0.0`/`1.0` and the getter reports the clamp
  (FR-003).
  (c) `triggerGrain()` before `prepare()` is a no-op that moves no counter.
  (d) Sample-rate changes: `prepare()` at 44 100 / 48 000 / 96 000 / 192 000 Hz clears the pending
  queue, re-seeds `reverseRng_`, and a reverse render at each rate is free of NaN/Inf and bounded by
  `kMaxLevel`.
  (e) **Partition invariance**: the same reverse render driven in blocks of 512, 64, 37 and 1 sample
  compares within the `render_fingerprint.h` tolerances — the property `:2002-2012` and
  `rolling_capture_buffer.h:243-255` exist to protect, now exercised on the backwards path.
- **SC-009 — Global-stage CPU delta recorded; the ceiling is unchanged.**
  `AtmosphereGhost_CpuDelta`, tagged `[.perf]`, run **alone** under `node tools/run-cpu-tests.js`
  after a cool-down, P-core-pinned — the protocol Phase 10's SC-001b names
  (`vorago_perf_test.cpp:236-247`).

  **The measurement shape, pinned exactly, because the reference figure belongs to one specific
  protocol and to no other.** 28 285.5 ns/block was produced by the **stage probe** inside
  `VoragoVoice_StageCostProbe` (`vorago_perf_test.cpp:1264-1277`), which calls
  `warmThenMeasure(kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial)` = **300 warm-up blocks,
  best-of-12 trials × 200 blocks** (`:300-302`) and drives the subject as **8 × 64-sample chunks per
  512-sample block** (`:1268-1272`) — *not* the `kWarmupBlocks/kTrials/kBlocksPerTrial` = 400/25/500
  gating shape at `:291-293`, and *not* a single 512-sample call. The TU's own comment at `:294-299`
  says the stage probe is deliberately measured at the reduced shape because it is a BREAKDOWN that
  "gates nothing". **Every arm of SC-009 reproduces the stage-probe shape**: 300 warm-up blocks,
  best-of-12 × 200 blocks, each block driven as 8 × 64-sample `processStereoBlock` calls, at 48 kHz.
  The subject is one `AtmosphereEngine` built exactly as `buildAtmosphere()` builds it
  (`vorago_perf_test.cpp:609-621`): `captureSeconds = 20`, FR-017's seven values (`density 0.30`,
  `grainSeconds 12`, `pitchSemitones −12`, `positionSpread 0.90`, `blur 0.85`, `decorrelation 0.85`)
  and `setLevel(kGhostBurstPeak = 0.60)` (`vorago_engine.h:204-206`).
  A best-of-25 × 500 measurement reads systematically **lower** than a best-of-12 × 200 one, so
  describing the two as one arm would make every bound below of unknown tightness. That claim has been
  removed; the shape above is the claim.
  **28 285.5 is an artifact-log figure** (`specs/vorago-phase10-voice-engine/artifacts/perf.log:68`),
  not a checked-in constant. The checked-in constants this phase gates against — and never edits —
  are `kEngineMeasuredNsAtPoly4` (`:256`), `kEngineBaselineNsAtPoly4` (`:257`) and
  `kCavernMeasuredNsPerBlock` (`:215`); `kEngineBaselineNsAtPoly4` is the one that carries roadmap
  line 506's "checked-in global baseline" obligation, discharged in clause 5.

  **The REQUIREs are the in-run paired gates only; the absolute bounds are WARN-only, recorded into
  the compliance record, never REQUIREd (Clarifications 2026-09-22, Q6).** Arms 2, 3 and 4 each
  REQUIRE their ratio against **arm 1 measured in the same run** — the binding, machine-independent
  gate. The absolute bounds derived from the transcribed 28 285.5 ns/block (Phase 10's recorded
  stage-probe figure, one machine) are printed into the compliance record for every arm, including
  arm 1 itself, but **never gate a REQUIRE**: that figure's own documented run-to-run drift is
  0.6 %–7.4 % (`vorago_perf_test.cpp:239-247`), so a REQUIRE against it would go red on a different
  machine, or the same machine after thermal drift, on untouched code. **Arm 1 therefore carries no
  REQUIRE of its own** — it is purely the in-run reference the other three arms gate against,
  consistent with FR-025's own statement that this arm is a backstop, not a measurement (see FR-025
  and SC-006 clause 6). The composed clause (5, below) is likewise **computed and printed**, never
  REQUIREd — it is dominated by arm 4's own bound (clause 5 restates why) and its inputs include a
  figure (28 285.5) that is itself WARN-only here. The whole case is tagged `[.perf]`, run **alone**
  under `node tools/run-cpu-tests.js` after a cool-down, P-core-pinned, per the project's CPU-test
  protocol.

  | Arm | What it runs | In-run gate (REQUIRE) | Absolute bound (WARN, recorded) |
  |---|---|---|---|
  | 1 — **inert** | probability 0, no triggers | — (it *is* the reference) | 31 114 = `28 285.5 × 1.10` (recorded only) |
  | 2 — **reverse engaged** | probability 1.0, no triggers | ≤ `arm1 × 1.10` | 31 114 (recorded only) |
  | 3 — **triggers, realistic** | one `triggerGrain()` per **8.33 s** | ≤ `arm1 × 1.50` | 42 428 = `28 285.5 × 1.50` (recorded only) |
  | 4 — **triggers, stress** | one `triggerGrain()` per **0.833 s** | ≤ `arm1 × 2.50` | 70 714 = `28 285.5 × 2.50` (recorded only) |

  1. **Inert.** The FR-040 gate: the change must not cost anything when it is off. Arm 1 carries
     **no REQUIRE**: it is the in-run reference arms 2–4 gate against, and its own absolute figure
     (31 114 ns/block) is recorded into the compliance table as a WARN only — machine drift of
     0.6–7.4 % makes an absolute REQUIRE here a false-red risk, not a real gate (Q6). FR-025's
     structural claim is carried by SC-006 clause 6, not by this arm.
  2. **Reverse engaged.** Reverse is not more expensive per grain-sample (same gathers, same envelope,
     one extra borrow test in the scalar advance) and SC-003 (a) establishes that it does not truncate
     at this configuration, so concurrency is unchanged; a figure above the in-run bound is a real
     finding, not a tolerance to widen. (The absolute figure, 31 114, is recorded alongside it as a
     WARN, per Q6.)
  3. **Triggers at the realistic cadence — derived at ENGINE level, because polyphony is what sets
     it.** The reviewed draft derived one trigger per 20 s from
     `SlowEventScheduler::kDefaultMinInterval = 20.0f` (`slow_event_scheduler.h:164`, verified) and
     called it "the fastest cadence Phase 10's two schedulers can produce". That ignored polyphony and
     the family draw, and is corrected here — **upward**, because the old figure was wrong, not to
     dodge a bound. The engine-level worst case is:
     `2 schedulers per voice` (`VoragoVoice::kNumEventSchedulers = 2`, `vorago_voice.h:241`)
     `× 6 voices` (`VoragoEngine::kMaxVoices = 6`)
     `× 1 event / 20 s` (the min-interval floor)
     `× 1/5 ghost share` (`EventFamily::GhostBurst` is one of `kNumEventFamilies = 5`,
     `vorago_voice.h:259`, `:261`) **= 0.12 ghost events/s = one per 8.33 s**.
     The engine folds voices with `std::max` (`vorago_engine.h:1248-1254`) and FR-031 fires on a
     rising threshold crossing of that fold, so overlapping bursts **merge** — merging only
     *lowers* the edge count, which is why 0.12/s is an upper bound and not an estimate.
     Concurrency: the inert configuration runs `0.30 grains/s × 12 s = 3.6` concurrent grains; the
     triggers add `0.12 × 12 = 1.44`, i.e. `5.04 / 3.6 =` **+40.0 %**. The 1.50 bound is that with
     margin, and it is the in-run REQUIRE (Q6); the absolute figure (42 428) is recorded as a WARN.
  4. **Triggers at the stress cadence — gated in-run, not merely printed.** The stress arm is the
     polyphony-scaled worst case at the ceiling of `VoragoVoice::setEventRateScale`, which clamps to
     `[0.1, 10]` (`vorago_voice.h:1353-1358`) and whose only clock is the scheduler interval range
     (fast 20–90 s becomes 2–9 s, `vorago_engine_test.cpp:2715-2719`): `0.12 × 10 =` **1.2 ghost
     events/s, one per 0.833 s**. Concurrency rises to `3.6 + 1.2 × 12 = 18.0`, i.e. **5.0×**.
     The in-run bound is **not** a proportional scaling of that, because a large part of the inert
     block cost — the blur FFT, the capture write, the level smoother — is concurrency-**independent**,
     and the stage probe's breakdown does not resolve the split. **2.50 is this phase's own in-run
     regression bar for the arm**, and a measurement above it is reported as a finding for the plan
     stage to rule on; it is **not** widened in place. The absolute figure (70 714) is recorded as a
     WARN, per Q6 — the REQUIRE is the in-run ×2.50 ratio. The reviewed draft left this arm "printed
     for the record, gated only by clause 5's arithmetic", i.e. effectively ungated — that is what
     this clause fixes.
  5. **Composed arithmetic against the checked-in baseline — the roadmap's ceiling obligation, and
     explicitly the dominated clause.** With `Δ` = arm 3 minus arm 1 (and, separately, arm 4 minus
     arm 1), the bound is the one the repository actually enforces, **including FR-083's ×1.05 term**:
     `ceil((kEngineMeasuredNsAtPoly4 + Δ) × 1.05) + kCavernMeasuredNsPerBlock ≤ kReferenceNs`
     i.e. `ceil((2 566 170 + Δ) × 1.05) + 124 497 ≤ 3 200 000`, i.e. **`Δ ≤ 362 880 ns/block`**.
     That ×1.05 is not optional: `vorago_perf_test.cpp:263-266` static-asserts
     `kEngineBaselineNsAtPoly4 == ceil(kEngineMeasuredNsAtPoly4 × 1.05)` and `:269` static-asserts
     `kEngineBaselineNsAtPoly4 + kCavernMeasuredNsPerBlock ≤ kReferenceNs`, so a Δ that satisfied the
     reviewed draft's looser `Δ ≤ 509 333` would **break the checked-in static_assert** the moment
     OQ-1 were ruled the other way. This is the bound that would apply if OQ-1 ruled the wiring on
     (OQ-1 is now closed inert — Clarifications Q5 — so this clause is arithmetic-only, not a live
     wiring gate).
     The TU **includes** `dsp/tests/unit/systems/vorago_perf_budget.h` (FR-047, Q7) for
     `kEngineMeasuredNsAtPoly4`, `kEngineBaselineNsAtPoly4`, `kCavernMeasuredNsPerBlock` and
     `kReferenceNs` — the same header `vorago_perf_test.cpp` now includes for its own definitions —
     and edits none of their values (FR-041).
     **This clause is dominated and is labelled as such**: arm 4's own bound caps Δ at
     `70 714 − arm1 ≈ 42 429 ns/block`, roughly **8.6×** below 362 880, so clause 5 can never fail
     while clauses 1–4 hold. **The clauses that bind are 2, 3 and 4.** Clause 5 is the arithmetic that
     connects this phase's measurement to the roadmap's checked-in global baseline, and it is
     **computed and printed into the compliance record, never REQUIREd (Q6)** — it was never anything
     but the connective arithmetic, and clauses 2–4's in-run REQUIREs are what actually gate the phase.
- **SC-010 — Vorago wiring is inert by default and correct when engaged.**
  `VoragoEngine_GhostExtensionWiring`.
  The accessor is `VoragoEngine::atmosphere()` — `[[nodiscard]] const AtmosphereEngine& atmosphere()
  const noexcept` (`vorago_engine.h:1066`). There is **no** `atmos()` on `VoragoEngine`: `atmos_` is
  the private member (`:1498`) and a repo-wide `grep -rn "atmos()" dsp/` finds only
  `SeraphisVoice::atmos()` (`seraphis_voice.h:845`) and its Seraphis-side callers. The reviewed draft
  used `atmos()` here and would not have compiled.
  (a) With `VoragoEngineConfig` at its defaults **plus one held note-on (amended 2026-09-23, B-2:
  `setSeed(0x6057u)`, `setPolyphony(1u)`, `noteOn(33u, 100u)` at sample 0, nothing else written —
  the no-note-on render is digital silence and pins nothing)**, a 60 s Vorago render compares against a
  **transcribed base-commit `RenderFingerprint`** (`kBaseCommitVoragoFingerprint`) under **FR-046's
  `noise_organism` measured-bounds protocol (Q4)** —
  `compareFingerprints(actual, kBaseCommitVoragoFingerprint).withinTolerance()`
  (`render_fingerprint.h:108-110`), evaluated at the MEASURED per-comparison bounds that protocol
  derives (a three-toolchain probe, a paste-ready failure literal, a PROVENANCE block), not the
  defaults — under the same stored-reference mechanism SC-001 clause 1 states (base commit
  `374580d7d0f0631561413310bd3085e15ba7279c`); and
  `engine.atmosphere().getTotalTriggeredGrainsBorn() == 0` and
  `engine.atmosphere().getGrainReverseProbability() == 0.0f`.
  (b) **Wiring correct when engaged.** The fixture is Phase 10's own `makeGhostEngine()`
  (`vorago_engine_test.cpp:2730-2756`) shape, restated because every number below depends on it:
  8 kHz, polyphony 1, seed `0x6057`, `setEventRateScale(10.0f)`, **`setEcosystemDepth(0.0f)`** — the
  write that makes the ghost request the *scheduler* lane alone, and without which the ecosystem term
  `ecosystemDepth_[kGhost] * output[i]` (`vorago_voice.h:1734`) keeps the request continuously
  non-zero — plus `atmosGhostEventTriggers = true` and, **unlike Phase 10's fixture**,
  `atmosCaptureSeconds` left at the shipped `20.0f` (`vorago_engine.h:120`) so grains can actually be
  admitted. The **full-ring pre-roll** warms the capture ring — `getCaptureCapacitySamples() =
  nextPowerOf2(160 000) = 262 144` samples = **32.768 s** at 8 kHz (amended 2026-09-23, R-2: the
  reviewed draft's 25 s was short by 7.8 s, because the ring is power-of-two rounded,
  `rolling_capture_buffer.h:75-93`; FR-050 is vacuous after it) — then all counters are snapshotted
  and the render runs **600 s**, driven in **64-sample blocks** so the observation grid equals the control
  grid and no edge can be missed by aliasing. Tagged `[long]` on the estimate (Clarifications
  2026-09-22 Q8, FR-048) and **untagged 2026-09-24 (B-5) on the measurement: 9.192 s**, well under
  FR-048's ~15 s bar. That matters beyond tidiness — (b) and (d) are the only criteria in this phase
  that observe a rising edge actually spawning a grain, and while the tag stood they ran in no gate,
  because every gate command in this phase excludes `[long]`. Measured runtime recorded in the
  compliance table.
  The edge count is taken by running SC-027's two-state detector (`vorago_engine_test.cpp:2699-2704`)
  over `engine.atmosphere().getLevel()` with `kGhostRiseThreshold = 0.5 × kGhostBurstPeak` and
  `kGhostFallThreshold = 0.05 × kGhostBurstPeak` (`:2666-2667`).
  **This is stated plainly rather than dressed up as independence:** FR-031 latches on
  `ghostPeak_ * ghost`, which is exactly the value `setLevel` stores and `getLevel()` returns, so the
  two detectors see the same signal by construction, and **no independent public observable of the
  scheduler's own event edges exists** — `SlowEventScheduler::isEventActive()`
  (`slow_event_scheduler.h:361`) lives on `sched_`, a private member of `VoragoVoice`, unreachable
  from `VoragoEngine`. The clause's teeth are therefore the assertions below and the failure modes
  named after them, not a second signal source. Assertions:
  - **`edges ≤ Δ getTotalTriggeredGrainsBorn() ≤ edges + 1`.** The detector counts *completed*
    excursions (rise **then** fall) while FR-031 fires on the rise, so a burst still in flight at the
    end of the render has triggered but is not yet counted — that is the whole of the `+1`, and it is
    stated rather than absorbed;
  - **`Δ getDroppedTriggerCount() == 0`** and **`Δ getSkippedTriggerCountPoolFull() ==
    Δ getSkippedTriggerCountRingCold() == 0`**, so the equality above cannot be silently satisfied by
    rejections;
  - **`Δ getTotalTriggeredGrainsBorn() ≥ 6`** — the same burst-count floor Phase 10's SC-027 clause 2
    measured on this fixture (`vorago_engine_test.cpp:2819`).

  **What this clause can actually falsify**, stated so its teeth are visible rather than assumed: a
  level-polling implementation that triggered every control step gives `Δ ≫ edges`; a missing or
  mis-gated wire gives `Δ == 0`; a per-voice rather than per-fold latch gives `Δ > edges + 1`; a
  trigger consumed but never turned into a grain shows as `Δ < edges` with a skip counter moving; and
  the `≥ 6` floor fails outright if FR-031's predicate cannot see the burst population Phase 10
  measured. A latch that never re-arms gives `Δ == 1`.
  (c) **The closed-gate arm, which is now literally SC-027 clause 2's.** `setGhostPeakLevel(0.0f)`
  (Phase 10's `gated` arm, `vorago_engine_test.cpp:2793-2795`), everything else as in (b):
  `getLevel()` holds its `0.0` base for the whole render and `getTotalTriggeredGrainsBorn() == 0` —
  because FR-031 latches on `ghostPeak_ * ghost`, which is identically zero there. And, as SC-027
  already asserts, the events **did** fire (`maxGhostRequest >= kGhostRiseThreshold`,
  `vorago_engine_test.cpp:2828`), so the zero is the gate closing rather than a silent lane. Phase
  10's own case is unchanged and unedited (FR-033).
  (d) `engine.atmosphere().getLevel()` still shows **≥ 6 burst edges** in arm (b) by the same
  detector, i.e. the spawn path did not replace the level gate.
  (e) **The reverse-probability wire (added 2026-09-23, R-11).** No other clause moves
  `atmosGhostReverseProbability` off its default — (a) asserts `getGrainReverseProbability() == 0.0f`
  at config defaults and (b)–(d) engage `atmosGhostEventTriggers` alone — so an implementation that
  omitted `atmos_.setGrainReverseProbability(cfg.atmosGhostReverseProbability)` (the one line FR-030
  says the FR-017 block gains, and half of FR-034) would pass every other criterion in the phase,
  because the component default is already `0.0f`. Arm: (b)'s fixture with
  `atmosGhostEventTriggers = false` and `atmosGhostReverseProbability = 1.0f`; after `prepare()`
  REQUIRE `engine.atmosphere().getGrainReverseProbability() == 1.0f`; then the full-ring pre-roll and
  a further render long enough for several density-scheduler births, and REQUIRE
  `engine.atmosphere().getTotalGrainsBorn() > 0` and
  `engine.atmosphere().getTotalReverseGrainsBorn() == engine.atmosphere().getTotalGrainsBorn()` (at
  probability 1 the only forward outcome is the single exact draw `1.0f`, expected rate 2^-32). A
  zero birth count is a fixture defect to fix, never a reason to weaken the arm. No threshold moves
  and nothing else in SC-010 changes.
- **SC-011 — FR-050 rejects inside the filling regime, and admits no earlier than its threshold
  (added 2026-09-23, R-1).** `AtmosphereGhost_ReverseFillDeficit`, untagged.
  Every other reverse fixture pre-rolls to a full ring, which sets FR-050's `t* = 0` and makes the
  clause a no-op — so a wrong-signed, wrong-termed or entirely absent FR-050 passes all of them, and
  SC-003 (c) is **not** a backstop (it folds the *computed* age, which stays inside `[64, C − 2]`
  precisely while the read is clamped and stale). This is the one case that renders the filling-ring
  regime.
  **Configuration:** SC-003's Vorago ghost point (probability 1, `density = 0.30`,
  `grainSeconds = 12`, `captureSeconds = 20`, `pitchSemitones = −12`, `positionSpread = 0.9`,
  `decorrelation = 0.85`, 48 kHz, seed 1 — so `C = getCaptureCapacitySamples() = 1 048 576` and
  `L' = 576 000`), driven in 64-sample blocks, with **one declared deviation: `pitchSpread = 0` and
  `driftRangeSemitones = 0`**, which pins `ratioMax == ratioMin == 2^(−12/12) = 0.5` exactly so every
  threshold below is computable *by the test* from public accessors (with the Vorago point's drift
  range of 2 the per-grain `ratioMax` is a draw with no getter, and a test forced to use the
  configured upper bound would assert a threshold larger than the one FR-050 applied — red on
  correct code). The deviation changes nothing FR-050 does. With `rMax = 0.5` the filling-branch
  deficit is `ceil(0.5 × 576 000) = 288 000`, the minimum-birth-age admission threshold is
  `kAdmitThresholdSamples = 128 + 288 000 = 288 128` samples (6.003 s), the branch crossover is
  `C − L' = 472 576` so `t* = L'` throughout the region rendered, and the drawn birth age lies in the
  `positionSeconds = 1.0 ± 0.9` window, `[4 800, 91 200]` samples, so the *actual* first admission
  falls between `A = 292 864` (6.10 s) and `A = 380 488` (7.93 s). **Every one of those numbers is
  recomputed in the test from `getCaptureCapacitySamples()`, `kMinAgeSamples`, the configured
  `grainSeconds` and the configured `rMax`, never transcribed.**
  (a) **Rejection arm — measured only over the span in which rejection is provable.** Pre-roll
  **5 s** (240 000 samples — deliberately *not* the full ring), snapshot `born0`,
  `cold0 = getSkippedTriggerCountRingCold()` and `poolFull0 = getSkippedTriggerCountPoolFull()`, fire
  **one** `triggerGrain()`, and render `kRejectSpan = 40 000` samples (0.833 s) in 64-sample blocks,
  ending at `A = 280 000` — 8 128 samples clear of the threshold. The test computes
  `kAdmitThresholdSamples` itself as `kMinAgeSamples + kMinAgeSamples + ceil(rMax × min(L', C − A))`
  and REQUIREs `240 000 + kRejectSpan < kAdmitThresholdSamples`, so the arm cannot drift into the
  admitting regime if a constant moves. Over that span: `Δ getTotalGrainsBorn() == 0` (the teeth);
  `Δ getSkippedTriggerCountPoolFull() == 0` (nothing rejected for the wrong reason); and
  **`1 ≤ Δ getSkippedTriggerCountRingCold() ≤ 2`** — the fired trigger *was* attempted and rejected,
  plus at most one scheduler tick, because the shortest interonset `jitter = 0.5` permits is
  `160 000 × 0.75 = 120 000` samples and `kRejectSpan = 40 000 < 120 000`. Attempts are bounded,
  never predicted: `samplesUntilNextGrain_` is a random draw (`grain_scheduler.h:80-84`), so the tick
  count is not exactly computable, which is why SC-003 (a) uses `≤` too.
  (a2) **Admission arm — the threshold is reached, and not before.** Continue the same fixture past
  the rejecting span to the end of the 12 s grain life (17 s of render in total, 816 000 samples),
  reading the counters at every 64-sample boundary. At the first boundary on which
  `getTotalGrainsBorn()` advances, record `A_born = min(samplesRendered, getCaptureCapacitySamples())`
  and `birthAge = getLastBornGrainBirthAgeSamples()` (`:1105-1106`), and REQUIRE
  `A_born ≥ ceil(birthAge) + kMinAgeSamples + ceil(rMax × min(L', C − A_born))` with
  `L' = getLastBornGrainLifetimeSamples()` (`:1112-1114`) — a **necessary** condition for FR-050 to
  have admitted that grain: the drawn `decorrAge ≥ 0` is unobservable, so the decorr-free lower bound
  on `needed` is used; the `:1736` clip cannot bind (`birthAge ≤ 91 200` against
  `C − 2 − guard = 1 048 510`); and `A_born` is read at the block end so it is never smaller than the
  `available` the admission actually saw.
  (c) **Forward arm unaffected — "never evaluated for a forward grain", pinned.** Repeat (a)'s
  fixture at probability **0** and REQUIRE `getTotalGrainsBorn()`, `getTotalGrainsRetired()`,
  `getSkippedTriggerCountRingCold()`, `getSkippedTriggerCountPoolFull()` and `getGrainRngState()`
  equal their **transcribed base-commit values at this short pre-roll**, under SC-001's provenance
  rule (integers, so no tolerance is involved). A forward render at a *filling* ring is exactly where
  a misplaced clause would show; SC-001 clause 3 pins only the full-ring case.
  **The clause-disabled differential — three measurements, all recorded in the compliance table
  (plan §6 step 4).** (i) Arm (a) is **red with the FR-050 block commented out** — the shipped test
  needs only `needed ≤ 92 488` against `A = 240 000`, so the trigger's birth is admitted at once and
  `Δ getTotalGrainsBorn() ≥ 1` — and green with it in. (ii) Arm (a2) is green with the clause in and
  **red with it out** (the first birth then lands at `A ≈ 240 064` against a right-hand side of at
  least `292 864`), which proves the rejection arm is not green merely because the configuration
  never births at all. (iii) With the clause still commented out, the **stale-read measurement**: run
  the 5 s-pre-roll fixture to the end of the grain life and record that
  `getMaxObservedGrainAgeSamples()` exceeds `min(samplesRendered, getCaptureCapacitySamples()) − 2.0`
  at some block boundary — predicted at `t > 2 × (239 998 − birthAge − decorr)` grain-samples:
  ≈ 479 870 for a birth at the `kMinAgeSamples` clamp and ≈ 297 600 … 470 400 for the
  `[4 800, 91 200]` ages this fixture draws, all inside the 576 000-sample life.
  **There is no clause (b).** The reviewed plan draft's (b) — pre-roll, REQUIRE a birth, then bound
  `getMaxObservedGrainAgeSamples()` by `available − 2` — cannot fail: FR-050 admits iff
  `A ≥ needed + ceil(rMax × t*)`, which is exactly the negation of the crossing condition
  `A < needed_exact + rMax × t*`, so **admitted implies never stale** is a theorem and any green-path
  fixture can only observe grains that provably satisfy the bound. The protected quantity is
  observable only in measurement (iii), with the clause compiled out, and this criterion says so
  rather than shipping an assertion that cannot fail.
- **SC-012 — The pass-A scratch vectors hold two births per sample (added 2026-09-23, R-4).**
  `AtmosphereGhost_PassAScratchBound`, untagged; FR-051's binding criterion. Without it FR-051 could
  be omitted and every gate stays green, and `AllocationScope` cannot supply the check — writing past
  `retiredScratch_`/`dueScratch_`'s size performs no allocation. So the case pairs a portable
  assertion with a sanitizer run.
  **Fixture — the corrected reaching configuration (plan P-5):** `sampleRate = 20`,
  `density = kMaxDensity = 20.0f` (`:304`) so the scheduler's interonset is exactly `1.0` and it fires
  on **every** sample (`grain_scheduler.h:74-76`), `captureSeconds = 30` so
  `C = nextPowerOf2(600) = 1024`, blur and freeze off, reverse probability 0 (direction is irrelevant
  to the sizing), ring pre-rolled full (1 024 samples), and `kMaxGrains` `triggerGrain()` calls issued
  before each block so the FR-019 queue is at its cap at every chunk boundary. (At the reviewed
  draft's 512 Hz the scheduler fires only every 25.6 samples and pass A tops out near 67 entries
  against a capacity of 128 — that configuration would have been a green test against a real
  defect.)
  - **Chunk 1** at `setGrainSeconds(3.2f)` (`L = round(3.2 × 20) = 64`): render exactly 64 samples.
    Two births per sample fill the pool. **Amended 2026-09-24, B-6.** The reviewed text read
    "REQUIRE `getActiveGrainCount() == kMaxGrains` afterwards and `Δ getTotalGrainsRetired() ≤ 1`",
    written when pass A had ONE birth site. With FR-020's second site there are **two** births at
    `i = 0` — the scheduler's and the trigger's — both with `i + lifetime <= numSamples`, so both are
    due inside this chunk and both retire at the end-of-chunk drain, which no later birth can refill
    (the drain runs after the birth loop). The measured outcome is therefore `active == 62`,
    `Δ retired == 2`, and the reviewed pair would be **red on correct code**. The assertions are the
    same two facts in the form the arithmetic supports: REQUIRE
    `getActiveGrainCount() + Δ getTotalGrainsRetired() == kMaxGrains` (the pool DID reach its cap) and
    `Δ getTotalGrainsRetired() ≤ 2` (the ONLY retirements were the `i = 0` pair). Neither is weaker:
    the sum identity is strictly stronger than the original equality on a chunk where retirements can
    occur, and `≤ 2` is the tight bound, not a widened one.
  - **Chunk 2** at `setGrainSeconds(0.1f)` (`L = 2`, the smallest legal lifetime here —
    `kMinGrainSeconds × 20 = 1` is rejected by the `lifetime < 2` test at `:1696-1699`): render exactly
    64 more samples and REQUIRE **`Δ getTotalGrainsRetired() > kMaxGrains × 2 = 128`** — the
    assertion with teeth: every retirement inside a chunk is one `retiredScratch_` write and one
    consumed `dueScratch_` entry, and `dueCount` never decreases within a chunk (`:2041-2053`,
    `:2119-2134`, drain at `:2136-2141`), so a delta above 128 *is* a write past the old sizing.
    Expected **≈ 186**; the REQUIRE is the structural `> 128`, not the measured figure. Also REQUIRE
    **`Δ getTotalGrainsRetired() ≤ kMaxGrains × 3 = 192`** — FR-051's own bound, so a derivation error
    in the other direction is caught rather than absorbed.
  - **Sanitizer half:** the same case from an ASan build. At `kMaxGrains × 2` the chunk-2 write is a
    heap-buffer-overflow and ASan aborts; at `kMaxGrains × 3` it is in bounds. **Both outcomes are
    recorded in the compliance table — red at `kMaxGrains × 2`, green at `kMaxGrains × 3`** — exactly
    as SC-011 records its clause-disabled runs. **Amended 2026-09-24, B-7 — which ASan.** The green
    arm is the build this clause named (`-DENABLE_ASAN=ON`, `build-asan`, `dsp_systems_tests` at
    Debug, MSVC). The RED arm cannot be taken from that build: MSVC's ASan **hangs inside its own
    error-reporting path** on this fixture — three runs, 600 s / 180 s / 300 s, produced a truncated
    log and a process at 0.27 s of CPU, with `symbolize=0:print_stacktrace=0` and with the
    `AllocationScope` removed making no difference. A hang is not the required evidence, so the red
    arm is taken instead from **g++ 13.3.0 `-fsanitize=address` under WSL2 Ubuntu 24.04**, running the
    identical fixture as a standalone program, which reports the overflow immediately and names the
    write site. The green arm is recorded on BOTH toolchains so the pair is comparable.
  - `[[maybe_unused]] const TestHelpers::AllocationScope scope;` still wraps both renders, proving the
    one thing it *can* here: the FR-051 sizing moved no allocation onto the audio thread.
  Cheap — 1 152 rendered samples at a 20 Hz rate; measured runtime recorded like every other case.

---

## Edge cases

- **RT-safety boundaries (amended 2026-09-23, R-6).** `triggerGrain()` is **audio-thread-only**
  (FR-018): it read-modify-writes `pendingTriggers_`/`droppedTriggers_`, which pass A also
  read-modify-writes, so a call from the control thread while the audio thread is inside
  `processStereoBlock` is a data race, not a supported pattern — the component's block-rate setter
  contract (`:28-35`) covers pure stores and does not extend to it. `VoragoEngine` calls it from its
  own control step on the audio thread (`vorago_engine.h:1243-1272`), the only caller in this phase.
  No atomics are introduced because no caller is off-thread; the spec states the contract rather
  than silently assuming it.
- **Trigger flood.** More than `kMaxGrains` calls before a render: FR-019 drops the excess, SC-005 (b)
  asserts it. There is no unbounded queue and no allocation.
- **Cold ring.** Triggers fired in the first `kMinAgeSamples` of a render are consumed and rejected
  (FR-022, SC-005 (c)); nothing is banked.
- **Latched engine.** `silence()` → latched → `triggerGrain()` is a no-op (FR-023). `reset()` is the
  only re-entry (`:651-652`), and it clears the queue (FR-024).
- **Parameter extremes.** Probability exactly 0 (SC-001) and exactly 1 (SC-002, SC-003, SC-009 arm 2);
  probability non-finite and out-of-range (SC-008 (b)); `grainSeconds` at both ends
  (`kMinGrainSeconds = 0.05`, `kMaxGrainSeconds = 30`, `:301-302`) crossed with
  `captureSeconds` at both ends (`[1, 30]`, `:317-318`) and `pitchSemitones` at ±24 with full spread
  and drift (SC-004) — the corner where reverse truncates hardest is `captureSeconds = 1`,
  `grainSeconds = 30`, `+36` effective semitones, where `w = 1 + 8 = 9` and the lifetime truncates to
  roughly `slack / 9`: at `captureSeconds = 1` the ring is `nextPowerOf2(48 000) = 65 536` samples, so
  `slack ≈ 64 180` and the 30 s request truncates to **≈ 7 100 samples ≈ 0.15 s**.
- **Reverse × decorrelation at maximum.** `decorrAge` is subtracted from `headroom` and `ageHi`
  unchanged (FR-016), so the right channel's older read is inside the ring by the same proof; asserted
  by SC-003 (c)'s age bounds, which fold both channels (`:1807-1808`).
- **Reverse × freeze.** The freeze leg is delay-matched and independent of grain direction
  (banner `:87-92`); `atmosFreezeEnabled = false` at Vorago (`vorago_engine.h:123`), but the component's
  own freeze cases must stay green (SC-006 clause 3).
- **Reverse × blur.** Blur runs on the mixed grain bus after pass B, so direction is invisible to it;
  the latency `getLatencySamples()` is unchanged (SC-001 clause 3).
- **Sample-rate changes.** `prepare()` at each supported rate re-derives the ring capacity, clears the
  pending queue and re-seeds `reverseRng_` (FR-005, FR-024, SC-008 (d)). Capacity is a power of two
  of `sampleRate × captureSeconds` (`rolling_capture_buffer.h:76-88`), so the reverse birth window
  scales with the rate and SC-003 (a)'s arithmetic holds at 44.1–192 kHz.
- **Seed determinism.** Same seed → same reverse pattern (SC-007 (a)); different seed → materially
  different (SC-007 (b)); `setSeed` mid-render re-seeds the stream without disturbing live grains
  (SC-007 (c)), matching `setSeed`'s documented mid-render contract (`:1005-1012`).
- **Block partitioning.** The absolute control grid (`:698-708`) and the per-sample age formation
  (`:1853-1857`) are what make the render partition-invariant; a backwards advance must not break it
  (SC-008 (e)).
- **Non-finite input while reverse is engaged.** The pass-1 input sanitiser (`:2031-2035`) and the
  per-sample substitution (`:2096-2104`) are unchanged and direction-independent; SC-004 asserts no
  NaN/Inf escapes on the reverse path.

---

## Existing components (verified this session)

| Component | Header path | What is reused, with the real signature |
|---|---|---|
| `AtmosphereEngine` | `dsp/include/krate/dsp/systems/atmosphere_engine.h:179` | The whole component. Extended append-only. `void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft, float* outRight, std::size_t numSamples) noexcept` (`:674-675`); `void prepare(double sampleRate, const PrepareConfig& config) noexcept` (`:407`); `void setSeed(std::uint32_t) noexcept` (`:1013`); `[[nodiscard]] std::uint32_t getGrainRngState() const noexcept` (`:1118`); `kMaxGrains = 64` (`:189`); `kMinAgeSamples = 64` (`:251`); `kControlChunkSamples = 64` (`:271`); salts `kGrainSalt = 0x1000`, `kBlurSalt = 0x2000`, `kSchedulerSalt = 0x3000`, `kDriftSaltBase = 0x4000` (`:331-334`). |
| `AtmosphereEngine::AtmosphereGrain` | same, `:1181-1201` | Private per-grain record; gains one `bool reversed` field (FR-008). Holds `std::uint64_t readIndexInt`, `float readFrac`, `float ratio`, `float decorrAge`, `float panL/panR`, `std::uint32_t lifetime/ageSamples`, `bool active`. |
| `RollingCaptureBuffer::LinearReader` | `dsp/include/krate/dsp/primitives/rolling_capture_buffer.h:197` | Unmodified. `void indexAt(float ageSamples, size_t newerOffset, std::int32_t& outI0, std::int32_t& outI1, float& outFrac) const noexcept` (`:279-280`); `[[nodiscard]] const float* leftData() const noexcept` / `rightData()` (`:289-290`); `[[nodiscard]] bool isValid() const noexcept` (`:294`). `index0()` clamps the age into `[0, maxAge_]` and truncates (`:313-321`), so a backwards walk is safe by construction. |
| `GrainScheduler` | `dsp/include/krate/dsp/processors/grain_scheduler.h:29` | Unmodified. `[[nodiscard]] bool process() noexcept` (`:73`), which decrements at `:74` and tests `<= 0.0f` at `:76`; `reset()` sets `samplesUntilNextGrain_ = 0.0f` (`:40`), so the **first sample of any render attempts a birth** — the fact SC-003 (a) and SC-005 (b) are written around. One RNG draw on a trigger (`:82`); jitter bounds the shortest interval at `1 − jitter × 0.5` of the interonset (`:82-84`). The new entry point sits **beside** it, never inside it. |
| `accumulateGrainSpanSIMD` | `dsp/include/krate/dsp/processors/grain_span_simd.h` | Unmodified — reverse changes only which indices the scalar pass writes (FR-013). |
| `Xorshift32` / `deriveStreamSeed` | `dsp/include/krate/dsp/core/random.h:102-111` | Reused for the new stream. `deriveStreamSeed` never yields 0 (`:110`), which is why every stream here goes through it (`:1002-1008`). |
| `VoragoEngine` / `VoragoEngineConfig` | `dsp/include/krate/dsp/systems/vorago_engine.h:144`, `:105` | Owns the global ghost tap `AtmosphereEngine atmos_` (`:1498`), published read-only as `[[nodiscard]] const AtmosphereEngine& atmosphere() const noexcept` (`:1066`) — **`atmosphere()`, not `atmos()`**; `VoragoEngine` has no `atmos()`. Config gains two inert fields (FR-030); its atmos block is `:114-125` (`atmosCaptureSeconds = 20.0f` at `:120`, `atmosBlurEnabled = true` at `:121`, `atmosFreezeEnabled = false` at `:123`). The control step already computes the combined ghost request (`:1248-1254`) and writes `atmos_.setLevel(ghostPeak_ * ghost)` (`:1272`); `kGhostBurstPeak = 0.60f` (`:204-206`). |
| `VoragoVoice::getGhostRequest` | `dsp/include/krate/dsp/systems/vorago_voice.h:977` | `[[nodiscard]] float getGhostRequest() const noexcept`. Read unchanged; the rising-edge latch (FR-031) copies `eventWasActive_`'s shape (`:1775-1784`). The combined value is `combineWake(0.0f, eco, sched)` = `max` (`:1011-1014`, written at `:1846`), whose **ecosystem** term `ecosystemDepth_[k] * output[i]` (`:1734`) is continuous in `[0, 1]` with `prepare()` installing depth `0.85` for every kind (`:617`) — which is why FR-031 uses a hysteresis band and not an exact-zero test. |
| `SlowEventScheduler` | `dsp/include/krate/dsp/processors/slow_event_scheduler.h` | Unmodified. `kDefaultMinInterval = 20.0f` (`:164`), `kDefaultMaxInterval = 90.0f` (`:165`), `[[nodiscard]] bool isEventActive() const noexcept` (`:361`) — **per scheduler**, and `VoragoVoice` owns `kNumEventSchedulers = 2` of them (`vorago_voice.h:241`) across up to `kMaxVoices = 6` voices, each event addressing one of `kNumEventFamilies = 5` families (`:259`, `:261`). That product, not the bare 20 s floor, is what sets SC-009 arm 3's cadence. `sched_` is **private to `VoragoVoice`**, so `isEventActive()` is not reachable from `VoragoEngine` — see SC-010 (b). |
| `VoragoVoice::setEventRateScale` | `dsp/include/krate/dsp/systems/vorago_voice.h:1353` | Unmodified. `void setEventRateScale(float) noexcept`, clamped to `[0.1, 10]` (`:1357`); its only clock is the scheduler interval range. The ceiling `10` is SC-009 arm 4's stress cadence, and Phase 10's ghost fixture already drives it there (`vorago_engine_test.cpp:2715-2719`, `:2744`). |
| `render_fingerprint.h` | `tests/test_helpers/render_fingerprint.h` | `[[nodiscard]] RenderFingerprint fingerprintRender(std::span<const float>)` (`:73`); its fields `rms/peak/meanAbs/totalVariation/checkpoints` (`:63-69`); `compareFingerprints(...)` (`:122-124`) returning a `FingerprintComparison` (`:99-111`) whose verdict member is **`withinTolerance()`** (`:108-110`) — there is **no `passes()`**; `kMetricTolerance = 2.5e-4` (`:61`), `kSampleTolerance = 5.0e-4f` (`:58`), `kRenderCheckpoints = 32` (`:55`). |
| Phase 10's ghost fixture | `dsp/tests/unit/systems/vorago_engine_test.cpp:2660-2830` | Unmodified, and **reused by reference** rather than re-derived: `kGhostRiseThreshold = 0.5f * kGhostBurstPeak` / `kGhostFallThreshold = 0.05f * kGhostBurstPeak` (`:2666-2667`), the two-state burst detector `traceGhostLane` (`:2683-2706`, detector at `:2699-2704`), `makeGhostEngine()` (`:2730-2756`, whose `setEcosystemDepth(0.0f)` at `:2752` is what makes the ghost lane scheduler-only), and the `>= 6u` burst floor (`:2819`). FR-031 and SC-010 (b)–(d) adopt these definitions verbatim. |
| `tools/check-seraphis-green.js` | `tools/check-seraphis-green.js` | Phase 10's append-only diff gate; every check runs `git diff HEAD --numstat` (`:77`, implemented at `:152-155`). Extended with an `atmosphere_engine.h` entry in the `APPEND_ONLY_HEADERS` rule table (FR-045, SC-006 clause 2), in the `{file, what, expected: [{count, name, pattern}]}` shape of its existing entries at **`:102-117`** (`:40-47` is the prose comment describing check 3, not the rule objects). |
| `ReverseBuffer` | `dsp/include/krate/dsp/primitives/reverse_buffer.h:27` | **Read and rejected** — see ADR-1. Mono, allocates its own `bufferA_`/`bufferB_` in `prepare(double, float)` (`:107-118`), captures its own input in `float process(float input)` (`:152`), and reports a chunk-sized latency (`:75`). Its only consumer is `processors/reverse_feedback_processor.h`. Not used, not changed. |

## New components

**No new production class.** Phase 10a still creates no new production class or header — the
roadmap's "append-only extension of `systems/atmosphere_engine.h`" holds for all production code. It
does add **two new test-only headers**: `dsp/tests/unit/systems/vorago_perf_budget.h` (FR-047,
Clarifications 2026-09-22 Q7, R-3), holding the seven constant definitions extracted unedited from
`vorago_perf_test.cpp`, and `dsp/tests/unit/systems/atmosphere_ghost_fixtures.h` (accepted at the
2026-09-23 plan-stage rulings; tasks T002) — the shared fixture header holding the Vorago ghost
configuration, the full-ring pre-roll defined once, the deterministic excitations and the transcribed
base-commit references, written against shipped API only so that the base-commit worktree runs the
*identical* fixture function SC-001's stored-reference mechanism requires (the rejected alternative,
duplicating the fixtures into each of the five TUs, would make that claim uncheckable). The ODR sweep
below is over the **names this spec introduces**, production
and test, recorded so absence, where claimed, is evidence rather than an assumption. Commands run
this session from the repo root.

| Proposed name | Kind | ODR / collision sweep | Result |
|---|---|---|---|
| — (no new class) | class | `grep -rn "class GhostExtension\|class AtmosphereGhostExtension\|class GrainTrigger" dsp/ plugins/` | **0 hits** — and none is proposed; recorded to close the roadmap line 594 obligation explicitly. |
| `setGrainReverseProbability` | member fn | `grep -rn "setGrainReverseProbability" dsp/ plugins/ tools/` | **0 hits.** (`setReverseProbability` has 18 hits on `GranularEngine`/`GranularFilter`/`GranularDelay` and their consumers — different classes, no collision, and the shipped semantic precedent, `granular_engine.h:123-125`.) |
| `getGrainReverseProbability` | member fn | same command | **0 hits.** |
| `getLastBornGrainReversed` | member fn | same command | **0 hits.** |
| `getTotalReverseGrainsBorn` | member fn | same command | **0 hits.** |
| `getReverseRngState` | member fn | `grep -rn "getReverseRngState" dsp/ plugins/ tools/` | **0 hits.** |
| `triggerGrain` | member fn | `grep -rn "triggerGrain" dsp/ plugins/` | **5 hits**, all inside `processors/granular_distortion.h` (`:368`, `:390`, `:542`) and `processors/formant_oscillator.h`'s `triggerGrains()` (`:422`, `:469`) — **private members of unrelated classes**, no `AtmosphereEngine` hit, no free function. No collision. |
| `getTotalTriggeredGrainsBorn` | member fn | `grep -rn "getTotalTriggeredGrainsBorn" dsp/ plugins/ tools/` | **0 hits.** |
| `getDroppedTriggerCount` | member fn | `grep -rn "getDroppedTriggerCount" dsp/ plugins/ tools/` | **0 hits.** (`getSkippedTriggerCountPoolFull` / `...RingCold` exist on this class, `:1053`/`:1059`; the new counter is a third, distinct name.) |
| `kReverseSalt` | constant | `grep -rn "kReverseSalt\|0x5000" dsp/include/krate/dsp/systems/atmosphere_engine.h` | **0 hits** — the salt space above `kDriftSaltBase + kMaxGrains = 0x4040` is free (`:331-336`, `:360`). |
| `AtmosphereGrain::reversed` | member | new field on an existing private nested struct (`:1181-1201`); `lint-odr.js` qualifies nested types by their enclosing class (`tools/lint-odr.js:19-21`) | No collision. |
| `atmosGhostReverseProbability`, `atmosGhostEventTriggers` | config fields | `grep -rn "atmosGhost" dsp/ plugins/` | **0 hits**; they sit in the existing `atmos*` block (`vorago_engine.h:114-125`). |
| `kGhostTriggerRise`, `kGhostTriggerFall` | constants | `grep -rn "kGhostTriggerRise\|kGhostTriggerFall" dsp/ plugins/ tools/` | **0 hits.** Private `VoragoEngine` constants, defined as `0.5f * kGhostBurstPeak` / `0.05f * kGhostBurstPeak` so they cannot drift from SC-027's detector (`vorago_engine_test.cpp:2666-2667`). |
| `ghostTriggerHigh_` | member | `grep -rn "ghostTriggerHigh_" dsp/ plugins/ tools/` | **0 hits.** One `bool` on `VoragoEngine`, FR-031's latch. |
| `dsp/tests/unit/systems/vorago_perf_budget.h` | header (test-only) | `grep -rn "vorago_perf_budget" dsp/ plugins/ tools/` (Q7) | **0 hits** at spec time — new file; the seven constants it holds (R-3) are moved, not renamed, from `vorago_perf_test.cpp`. |
| `dsp/tests/unit/systems/atmosphere_ghost_fixtures.h`, namespace `VoragoGhostFix` | header (test-only) | `grep -rn "atmosphere_ghost_fixtures" dsp/ plugins/ tools/` and `grep -rn "VoragoGhostFix" dsp/ plugins/ tools/` (run 2026-09-23) | **0 hits** each — new file, new namespace; everything in it is `inline`, it includes no production header this phase adds, and it is not registered in CMake (a header). |

`node tools/lint-odr.js` and `node tools/lint-layers.js` must both be clean at the end of the phase
(FR-044); neither has anything new to find, because no type is added.

---

## Open questions

None remain open. **OQ-1** — whether the shipped Vorago default engages either behaviour, or leaves
that to presets — is **resolved** by the Clarifications session of 2026-09-22, Q5: **(A) both inert**.
`atmosGhostReverseProbability = 0.0f` and `atmosGhostEventTriggers = false` stand exactly as ADR-3 and
FR-030 already write them; Phase 10's `kEngineMeasuredNsAtPoly4`, `kEngineBaselineNsAtPoly4` and SC-027
are untouched; SC-010 (a)'s default-render fingerprint is a base-commit reference (now under FR-046's
measured-bounds protocol). Phase 14 presets engage the features. See **## Clarifications** below.

The plan stage raised eleven further items (`plan.md` §8: the fill-up admission clause, the full-ring
pre-rolls, the seven-name budget header, the pass-A scratch sizing and FR-045's six sites, site (ii)'s
range, `triggerGrain()`'s threading contract, SC-006 clause 6's token rule, SC-004's tagging, FR-012's
`readFrac` range, SC-003 (b)'s endpoint clause and SC-010's reverse-probability arm). **All eleven
were ruled on 2026-09-23, each taking the recommended option, and are closed** by Clarifications
R-1 … R-11 below, each enforced by the FR/SC named in its brackets.

The build stage raised eight more, all from measurements rather than from design doubt: **B-1 – B-3**
(2026-09-23, the fingerprint protocol's execution and its reference harvest) and **B-4 – B-8**
(2026-09-24, FR-045's measured site ranges, the `[long]` tags retired by measurement, SC-012's chunk-1
form, which ASan the red arm comes from, and how `kSaturatedDrainFactor` is frozen). All eight are
ruled and closed below. Nothing remains open.

---

## Traceability

| Roadmap statement | Line(s) | Requirements / criteria |
|---|---|---|
| Phase 10a goal: "the two ghost-tap behaviours Phase 10 could not configure" | 495–497 | FR-001–FR-034 |
| "the shipped `AtmosphereEngine` has no reverse control and no event-trigger entry point" | 497–498 | Overview; ADR-1, ADR-2 |
| "Append-only extension of `systems/atmosphere_engine.h` on the `ContinuousBody` model" | 500 | FR-045, SC-006 |
| "a per-grain reverse flag read at grain birth" | 500–501 | FR-006, FR-008, FR-012, FR-014, FR-050, SC-002, SC-011 |
| "substrate: `primitives/reverse_buffer.h`" | 501 | **Corrected** — ADR-1, FR-010; `ReverseBuffer` row in *Existing components* |
| "the component already snapshots pitch, position and drift at birth" | 501–502 | FR-008 (verified at `atmosphere_engine.h:1185-1193`) |
| "an event-trigger entry point beside the density scheduler so a `SlowEventScheduler` event spawns a grain instead of only raising the level" | 502–503 | FR-018–FR-026, FR-031, FR-033, FR-051, SC-005, SC-010, SC-012 |
| "Default-inert: with neither feature engaged the render is unchanged" | 504 | FR-002, FR-025, FR-040, SC-001 |
| "Seraphis's suites stay green with no test edited (the Phase 10 SC-016 gate shape)" | 504–505 | FR-042, FR-045, SC-006 |
| "The added CPU sits in Phase 10's **global** stage … measured against Phase 10's checked-in global baseline" | 506–507 | FR-041, SC-009 |
| SC: "reverse grains measurably time-reversed against the capture" | 509 | SC-002 |
| SC: "one grain per trigger call, bounded by the pool" | 509–510 | FR-019, FR-021, SC-005 |
| SC: "default-inert render identity under `render_fingerprint.h` tolerances" | 510 | SC-001 |
| SC: "Seraphis green with git-diff evidence" | 510 | SC-006 |
| SC: "global-stage CPU delta recorded, ceiling unchanged" | 510–511 | FR-041, SC-009 |
| Depends on Phase 10's polyphony ruling and global-stage CPU baseline | 494 | SC-009 — arms 1–4 against the stage-probe figure 28 285.5 ns/block at the stage-probe shape (`vorago_perf_test.cpp:1264-1277`, `:300-302`); clause 5 against the **checked-in** `kEngineBaselineNsAtPoly4` (`:257`) via `kEngineMeasuredNsAtPoly4` (`:256`), `kCavernMeasuredNsPerBlock` (`:215`), `kReferenceNs` (`:168`) and FR-083's ×1.05 rule (`:263-266`) |
| Cross-cutting: RT safety, no allocation after prepare | 588–589 | FR-018 (audio-thread-only), FR-019, FR-051 (no out-of-bounds audio-thread write), SC-008 (a), SC-012 |
| Cross-cutting: layer discipline + ODR sweep | 594 | FR-044; *New components* table |
| Cross-cutting: CPU budgets are FRs, measured in tests | 595 | FR-041, SC-009 |
| Cross-cutting: no bit-exact float goldens | 605 | SC-001 clause 1, SC-007, SC-010 (a) |
| Cross-cutting: portability, `check-portability.js` | 606–607 | FR-043 |
| Cross-cutting: shared-component changes keep Seraphis green | 609–611 | FR-040, FR-045, SC-006 |

---

## Review notes

The 2026-09-22 adversarial review is applied in full; **no issue is rejected**. Four resolutions
changed a number or a predicate rather than only a citation, and each is recorded here with the
evidence that forced it, because none of them may be read as a threshold relaxed to dodge a finding.

1. **SC-009's protocol (blocker, twice).** The draft claimed its reference figure came from
   "best-of-25 × 500 blocks … after 400 warm-up". It did not: 400/25/500 are
   `kWarmupBlocks/kTrials/kBlocksPerTrial` (`vorago_perf_test.cpp:291-293`), the **engine** arm.
   28 285.5 came from the **stage probe** at `kStageWarmupBlocks/kStageTrials/kStageBlocksPerTrial`
   = 300/12/200 (`:300-302`), driven as 8 × 64-sample chunks (`:1264-1277`), which the TU labels a
   breakdown that "gates nothing" (`:294-299`). The false sentence is deleted and **SC-009 now
   reproduces the stage-probe shape exactly**, so the reference and the arms are commensurable. Each
   arm additionally carries an **in-run** gate against the inert arm measured in the same run, which
   is the paired comparison the review asked for and removes machine-to-machine variance from the
   verdict. No bound was loosened by this change.
2. **SC-009 arm 3's cadence, raised from ×1.35 to ×1.50.** The draft's "one trigger per 20 s" was
   derived from `kDefaultMinInterval` alone and ignored polyphony and the family draw. The corrected
   engine-level figure is `2 schedulers × 6 voices × (1/20 s) × (1/5 ghost share)` = one per 8.33 s,
   i.e. **+40 % concurrency, not +16.7 %**. The threshold moved because the derivation behind it was
   wrong — the review proved it wrong — not to accommodate a measurement. The stress arm, previously
   "printed for the record, gated only by clause 4's arithmetic", now has its own bound (×2.50) with
   its cadence derived the same way at `setEventRateScale`'s ceiling of 10 (`vorago_voice.h:1357`).
3. **SC-009 clause 5 tightened, from `Δ ≤ 509 333` to `Δ ≤ 362 880`.** The draft omitted FR-083's
   ×1.05 term. The repository static-asserts `kEngineBaselineNsAtPoly4 == ceil(measured × 1.05)`
   (`vorago_perf_test.cpp:263-266`) and `baseline + Cavern ≤ kReferenceNs` (`:269`), so a Δ the draft
   would have passed could have broken the checked-in assert. The clause is also now **labelled as
   dominated** by clauses 2–4, with those named as the binding ones.
4. **FR-031's edge predicate changed from exact-zero to a hysteresis band, and its subject from the
   pre-gate request to the gated value.** The exact-zero test was unusable: the ghost request's
   ecosystem term is continuous and `prepare()` installs depth `0.85` for every kind
   (`vorago_voice.h:617`, `:1734`, `:1846`), so it would have fired at most once per render.
   Phase 10 had already met and answered this — SC-027 counts threshold crossings with
   `0.5 × kGhostBurstPeak` / `0.05 × kGhostBurstPeak` (`vorago_engine_test.cpp:2666-2667`) — and
   FR-031 now adopts those constants rather than defining a second notion of "a ghost burst".
   Latching on `ghostPeak_ * ghost` rather than the request is a **second** change the review
   surfaced indirectly: it is what makes SC-010 (c) literally SC-027 clause 2's closed-gate arm
   instead of a differently-shaped near-miss, and it stops the spawn path costing CPU while the ghost
   tap is inaudible.

Two review suggestions were adopted with a stated limit rather than verbatim:

- **SC-010 (b)'s "count edges from an independent source".** There is none:
  `SlowEventScheduler::isEventActive()` (`slow_event_scheduler.h:361`) is on `sched_`, private to
  `VoragoVoice` and unreachable from `VoragoEngine`. The clause therefore says so explicitly and
  carries its teeth in the accounting assertions (`Δ getDroppedTriggerCount() == 0`, both skip
  counters unmoved, the `edges ≤ Δ ≤ edges + 1` band) and in the enumerated failure modes, rather
  than claiming an independence it does not have.
- **FR-013 / FR-025's falsifiability.** Neither was demoted to an implementation note, because both
  are real requirements; instead both are now gated by **SC-006 clause 6**, a named
  `git diff HEAD -U0` review gate at two named anchors. FR-025's false claim that SC-009 clause 1
  measures it is deleted, and SC-009 clause 1 is relabelled a backstop.

---

## Clarifications

### Session 2026-09-22

- **Q1** — When event triggers are engaged, does the density scheduler keep running? →
  **(A) Additive, as written**: the density scheduler keeps running at `0.30` grains/s and triggered
  grains add on top; replacement at the wiring layer (density at `kMinDensity` when triggers are on) is
  deferred to Phase 12/14 as a preset choice and is not part of this phase. [FR-020, SC-009, SC-010]
- **Q2** — What does `setGrainReverseProbability(NaN)` do? → **(A) Shipped setter shape**: a
  non-finite argument substitutes the control default `0.0f` before the clamp, identical to
  `setGrainSeconds`/`setDensity`/`setLevel`; this differs by design from Phase 10's FR-069 macro-write
  rule (retains the previous value), which is a `MacroMatrix` contract, not an `AtmosphereEngine` one.
  [FR-003, SC-008]
- **Q3** — What is the RNG-identity invariant for the trigger arm of SC-001 clause 2? →
  **(B) Computed delta replica**: a replica `Xorshift32` seeded with
  `deriveStreamSeed(seed, kGrainSalt)`, advanced exactly four draws per observed admitted birth (grain
  births counted via `getTotalGrainsBorn()`), must equal `getGrainRngState()` after 100 triggers fired
  on a warm ring; pins four `grainRng_` draws per admitted birth attempt as a contract of
  `tryBirthGrain`; the probability-0/probability-1 no-trigger identity arms stay as written; the
  reverse draw stays on its own dedicated stream (`kReverseSalt`), never `grainRng_`. [FR-027, SC-001]
- **Q4** — What tolerances and cross-toolchain protocol do the stored base-commit fingerprints use? →
  **(B) The `noise_organism` protocol**: MEASURED per-comparison bounds (not the default
  `kMetricTolerance`/`kSampleTolerance`), a documented three-toolchain probe (MSVC here; GNU and LLVM
  via the CI legs or WSL) whose worst deviations set the bounds with headroom, a paste-ready literal
  printed on failure, and a PROVENANCE block naming base commit, machine, compiler and date. Bit-exact
  float digests remain forbidden. The bounds are recorded in the spec once measured; no bound may be
  widened afterwards without a ruling. [FR-046, SC-001, SC-010]
- **Q5** — OQ-1: does the shipped Vorago default engage either behaviour? → **(A) Both inert by
  default**: `atmosGhostReverseProbability = 0.0f` and `atmosGhostEventTriggers = false` in
  `VoragoEngineConfig`; Phase 10's `kEngineMeasuredNsAtPoly4`, `kEngineBaselineNsAtPoly4` and SC-027
  are not touched; SC-010 (a)'s default-render fingerprint is a base-commit reference; Phase 14
  presets engage the features. OQ-1 is closed by this ruling. [FR-030, SC-010]
- **Q6** — Is SC-009's absolute gate a REQUIRE or a recorded figure? → **(B) In-run paired gates are
  the REQUIREs**: arms 2, 3 and 4 gate against arm 1 measured in the same run (the paired ratios and
  factors derived in the spec); the absolute bounds derived from 28 285.5 ns/block are WARNed and
  printed into the compliance record, never REQUIREd, because that figure is one machine's and the
  inert arm's documented drift is 0.6 to 7.4 %. The composed clause (Phase 10 measured plus delta plus
  Cavern at most `kReferenceNs`) is also computed and printed. The test is `[.perf]`, run alone and
  P-core-pinned per the CPU-test protocol. [SC-009]
- **Q7** — How does the new perf TU obtain Phase 10's checked-in constants? → **(B) Extract the four
  Phase 10 constants** (`kReferenceNs`, `kEngineMeasuredNsAtPoly4`, `kEngineBaselineNsAtPoly4`,
  `kCavernMeasuredNsPerBlock`) into a new shared header `dsp/tests/unit/systems/vorago_perf_budget.h`,
  included by `vorago_perf_test.cpp` and the new CPU-delta TU. The values do not change;
  `vorago_perf_test.cpp`'s own `static_assert`s stay in place. SC-006 clause 1 is narrowed to: zero
  modified files under `dsp/tests/unit/systems/` **except** `vorago_perf_test.cpp`, whose only
  permitted modification is replacing the four constant definitions with the include;
  `tools/check-seraphis-green.js` enforces that single named exception. Zero modified files under
  `plugins/seraphis/` stands. [FR-041, FR-047, SC-006]
- **Q8** — Which new cases are tagged `[long]`, and which CI lane runs each? → **(C) Tag by measured
  cost** per the `bloom_engine_spectral_test.cpp` rule: `[long]` only for cases costing above roughly
  15 s whose assertions are toolchain-independent (SC-003's 10-minute render, SC-010 (b)'s 600 s
  render, SC-007 (d)'s 100 000-grain accelerated sweep, SC-004's block-by-block truncation corner if
  it exceeds 15 s); every measured runtime is recorded in the compliance table. Additionally ship a
  reduced-duration per-push twin of SC-003 (about 30 s of render) that keeps the ring-cold-delta
  assertion so reverse-liveness still guards a push. NaN/Inf guards, bounded-grid and state-format
  cases are never `[long]`. [FR-048, FR-049, SC-003]

### Session 2026-09-23 (plan stage)

Eleven items raised by `plan.md` §8 ("Decisions taken here, and what still needs a ruling"). Every
one was ruled on 2026-09-23 and every ruling took the recommended option. Each is enforced by the
FR/SC in brackets, not only recorded here.

- **R-1** — Plan P-1: a reverse grain born while the ring is still filling outruns `available` (its
  age grows at `1 + r` per sample against the ring's 1) and reads a clamped, stale position for
  seconds, invisibly to every reviewed criterion; adopt the reverse-only fill-up admission clause as a
  new requirement with its own criterion? → **Yes**: FR-050 (the additive term
  `ceil(ratioMax × min(lifetime, capacity − available))`, vacuous on a full ring, never evaluated
  forward), with SC-011 (a) and (a2) as its binding criteria and the clause-disabled three-measurement
  differential as the evidence for the quantity it protects. **No green-path fixture can assert the
  stale-read bound**: FR-050's admission predicate is the negation of the crossing condition, so
  admitted-implies-safe is a theorem; the reviewed plan draft's SC-011 (b) is deleted rather than
  repaired and **does not exist**. [FR-050, SC-011]
- **R-2** — Plan P-2: SC-003's 5 s pre-roll sits below FR-050's admission threshold at the Vorago
  point (6.74 s at the minimum birth age) so its `coldStartSkips ≤ 3` assertion fails on correct
  code; and the ring is `nextPowerOf2(sampleRate × captureSeconds)`, so no seconds figure equals a
  full ring. Rule every pre-roll as the full-ring rule? → **Yes**: every pre-roll in the phase is
  `getCaptureCapacitySamples()` samples, seconds recorded as measured — SC-003 5 s → **21.845 s**
  (1 048 576 samples at 48 kHz, `captureSeconds = 20`) with `coldStartSkips` 3 → **9**
  (`1 + floor(21.845 / 2.5)`), the measured-span assertion unchanged; FR-049's twin inherits the same
  pre-roll; SC-010 (b) 25 s → **32.768 s** (the 8 kHz ring is `nextPowerOf2(160 000) = 262 144`
  samples); SC-004 65 536 samples = **1.365 s**. [Success-criteria preamble, SC-003, FR-049, SC-010,
  SC-004]
- **R-3** — Plan §4.2: `kReferenceNs` is derived (`kBlockBudgetNs × 0.30`, with `kBlockBudgetNs =
  (kBlockSize / kSr48) × 1e9`), so the budget header must carry `kSr48`, `kBlockSize` and
  `kBlockBudgetNs`; does moving seven names still satisfy FR-047's "exactly the four"? → **Yes**: the
  header carries **seven** names (`kSr48`, `kBlockSize`, `kBlockBudgetNs`, `kReferenceNs`,
  `kEngineMeasuredNsAtPoly4`, `kEngineBaselineNsAtPoly4`, `kCavernMeasuredNsPerBlock`), no value
  changed, and the compliance table records the count explicitly. [FR-041, FR-047, SC-006]
- **R-4** — Plan P-5: FR-020's trigger consumption is a second birth site at the same sample, so
  `prepare()` step 5b's `kMaxGrains * 2` pass-A scratch sizing — proven from one birth per sample and
  written by unchecked index on the audio thread — can overflow; adopt the `kMaxGrains * 3` sizing as
  a requirement with a criterion, and widen FR-045's deletion budget? → **Yes**: FR-051 with SC-012;
  FR-045's budget four → **six** sites, adding (v) `prepare()` step 5b (`:436-441`) and (vi) the two
  shipped comments this phase falsifies (`:2595-2596`, `:1183`). The alternative — consuming a
  trigger only on samples the scheduler did not fire on — defers a trigger and breaks FR-020's
  deterministic "within the next N rendered samples". [FR-051, FR-045, SC-012, SC-006]
- **R-5** — FR-045 site (ii) named only the `advance` lambda (`:1876-1882`), but the direction hoist
  re-indents the two `renderGrainSpan` loops that call it, which git reports as deletions outside
  that range; widen the site? → **Yes**: site (ii) = the `advance` lambda at `:1876-1882` together
  with the two loops that call it (`:1888-1891`, `:1917-1930`), and `check-seraphis-green.js`'s
  patterns are written against those three ranges only. [FR-045, SC-006]
- **R-6** — Plan P-6: `triggerGrain()` is a read-modify-write of counters pass A also
  read-modify-writes, so "callable from the control step between or inside blocks" would license a
  data race; narrow the contract? → **Yes**: `triggerGrain()` is **AUDIO-THREAD-ONLY**; the phrase is
  dropped from FR-018 and the component banner's real-time contract gains the matching note. No
  atomic is introduced because no caller is off-thread. [FR-018, Edge cases]
- **R-7** — SC-006 clause 6's pass-A half ("zero additional loads of `pendingTriggers_` on the path
  taken when it is zero") is a semantic property undischargeable from a `-U0` excerpt; restate as a
  token rule? → **Yes**: `pendingTriggers_` appears in the per-sample body exactly once, as the right
  operand of a short-circuited `&&` whose left operand is the loop-invariant `anyPending` declared
  above the `for`. Tighter, weakens nothing. [SC-006]
- **R-8** — FR-048 listed SC-004 as a conditional `[long]` candidate, but SC-004 is a NaN/Inf-guard
  and bounded-grid case, which FR-048's own closing sentence forbids tagging; remove it? → **Yes**:
  SC-004 is removed from the candidate list and is never tagged; if it measures over 15 s it splits
  (short untagged arm plus a `[long]` sweep sibling) as FR-049 splits SC-003. [FR-048, SC-004]
- **R-9** — FR-012 stated `readFrac ∈ [0, 1)`, but the backwards borrow's ceil-correction can round a
  value in `[−2.98e-8, 0)` up to exactly `1.0f`; amend the text (option a) or add a one-compare clamp
  (option b)? → **(a) Amend FR-012**: `readFrac` is non-negative and `≤ 1` for the grain's whole
  life; truncation-is-floor applies to `age`, not to `readFrac`, which is consumed only by the
  `ageAt` subtraction and never as an interpolation weight; the `:1183` field comment changes with
  it. [FR-012, FR-045 site (vi)]
- **R-10** — SC-003 (b)'s "first and last emitted sample of every reverse grain is exactly `0.0f`" is
  unobservable on a summed bus with ~3.6 concurrent grains; relocate it? → **Yes**: the per-grain
  endpoint clause is deleted from SC-003 (b) and replaced by (i) the isolated-span endpoint assertion
  in SC-002 (`span.front() == 0.0f && span.back() == 0.0f`, bit-wise, on the one-grain protocol at
  `ratio = 1.0`) and (ii) SC-003 (b)'s relative click-detector comparison. [SC-002, SC-003]
- **R-11** — No criterion ever moved `atmosGhostReverseProbability` off its default, so the
  FR-030 / FR-034 forwarding line was unfalsifiable; add an arm? → **Yes**: SC-010 gains arm (e),
  which sets `atmosGhostReverseProbability = 1.0f` with triggers off and observes the forwarding
  through `getGrainReverseProbability()` and an all-reverse birth population. No threshold moves.
  [SC-010, FR-030, FR-034]

### Session 2026-09-23 (build stage, T025 stop-and-surface)

- **B-1 — Where FR-046's GNU/LLVM figures come from.** T025 cannot measure inside the workflow
  (its group forbids building; the Linux/macOS CI legs run on push only, and pushing an unfinished
  phase is barred). Local WSL probe, push for CI, or defer? → **Local WSL probe, after the Windows
  gate**: the three-toolchain probe is a **main-loop step** executed after the workflow's Windows
  full-suite gate (T026) is green — the two fixture recipes compiled standalone against
  `dsp/include` + `tests/test_helpers` under g++ 13.3.0 `-O3`, g++ 13.3.0 `-O3 -ffast-math` and
  clang++ 18.1.3 `-O2` on Ubuntu 24.04 (WSL2, build dirs on F:), diffed against the MSVC 19.44
  `/O2` fingerprints — exactly the `noise_organism_test.cpp:3217-3231` method. The workflow records
  T025 as deferred to the main loop with both SKIP guards in place; the bounds land, the guards
  come out and the two cases are re-run before the phase commit. Never from a push. [FR-046, SC-001
  clause 1, SC-010 (a)]
- **B-2 — SC-010 (a)'s reference was digital silence.** The harvest found that `VoragoEngineConfig`
  defaults with no note-on render 60 s of exact zeros, so `kBaseCommitVoragoFingerprint` was all
  zeros and FR-046's derivation rule was unsatisfiable (`compareFingerprints` divides by
  `max(|ref|, 1e-12)`, so any deviation is either exactly 0 or ~1e9×, and the static_asserts demand
  a bound strictly looser than the shared constants). Sounding fixture, or keep silence as a binary
  detector? → **Sounding fixture**: SC-010 (a)'s render is `VoragoEngineConfig` defaults **plus one
  held note-on** — `setSeed(0x6057u)`, `setPolyphony(1u)`, `noteOn(33u, 100u)` at sample 0, no
  macro write, no other setter — 60 s at 48 kHz in 512-sample blocks, left channel;
  `kBaseCommitVoragoFingerprint` is **re-harvested** from the `374580d7` worktree with that recipe
  and FR-046 applies to it unchanged. The default-inertness claim is then made on a render in which
  the ghost tap actually captures and plays. [SC-010 (a), FR-046, FR-030]
- **B-3 — Harvest context (main-loop finding, no threshold moved).** The B-2 re-harvest, taken from a
  standalone dump executable at `374580d7`, read 1.0797e-4 (peak) / 6.53e-5 (checkpoint) against the
  SAME commit rendered inside `dsp_systems_tests.exe`, MSVC 19.44, same flags. A base-vs-HEAD A/B of
  nine engine configurations found every render bit-identical between the trees; a link-order bisect
  and per-symbol COMDAT attribution localised the whole delta to two Seraphis-era header-inline
  functions — `ContinuousBody::prepare(double)` (`continuous_body.h:1146`) and
  `SubharmonicEngine::updateControl()` (`subharmonic_engine.h:1064`) — whose copies from
  `continuous_body_test.obj` / `subharmonic_engine_test.obj` win at link time and are compiled
  differently under `/fp:fast` than in a small TU. Neither file changes in this phase, and Fixture A is
  immune because the component alone calls neither. Rule: **every stored engine-level reference is
  harvested by running the recipe inside a `dsp_systems_tests` build of the base commit** (a
  TEST_CASE with the paste-ready printer, the `noise_organism_test.cpp` regeneration idiom), never from a
  separate dump target; the same-toolchain self-consistency figure is then exactly 0 and FR-046's bound
  covers GNU/LLVM spread only. `kBaseCommitVoragoFingerprint` is re-harvested that way. [SC-010 (a),
  FR-046]

### Session 2026-09-24 (build stage, compliance remediation)

Every ruling below was taken from a measurement made this session, and every one of them **tightens
or restates** — none widens a bound, removes a case or lowers a workload.

> **B-4 … B-8 below were drafted by the workflow's remediation pass on 2026-09-24 and RATIFIED by the
> user the same day, each with its recommended option; B-8 (a threshold determination) was taken to the
> user explicitly. Rulings B-1 … B-3 above were taken in the main loop.**

- **B-4 — FR-045's site (ii) and (iii) ranges were narrower than the implementation they describe.**
  Measured, the diff deletes 87 lines at site (ii) (predicted 25) and 25 at site (iii) (predicted 1,
  and that one line is context), including 4 of the 5 lines of the codegen banner R-5 said "may be
  extended but not deleted". Reduce the diff, or restate the sites? → **Restate the sites**, exactly as
  R-5 itself restated site (ii) once before, because the extra lines are **not new deletions of
  meaning**: `git diff HEAD -w` shows they are re-indentation of the span body into the
  direction-templated lambda, whose only substantive change inside the loops is
  `advance();` → `advanceBy.template operator()<kBackwards>();`. The alternative — keeping the
  predicted ranges — would require *not* wrapping the loops, i.e. resolving direction per sample,
  which FR-013 forbids. No public declaration, default, constant or Seraphis-observable value is
  among the deletions; `tools/check-seraphis-green.js` matches all 122 against frozen patterns.
  The prose in FR-045 (ii)/(iii) is amended to the measured ranges. [FR-045, SC-006 clause 2]
- **B-5 — the three `[long]` tags were estimates, and the measurement retires them.** FR-048 tags
  `[long]` **only** above ~15 s measured. The estimates that produced the three tags quoted AUDIO
  durations (a "10-minute" render, a "600 s" render, a 100 000-grain sweep). Measured with Catch2
  `-d yes`, Release, MSVC 19.44, nothing else running: `AtmosphereGhost_ReverseLiveness` **6.990 s**,
  `AtmosphereGhost_Determinism_ReverseFraction` **1.361 s**,
  `VoragoEngine_GhostExtensionWiring_Engaged` **9.192 s**. Keep the tags, or apply the rule? →
  **Apply the rule: all three tags come off.** The consequence is the point, not a side effect: every
  gate command in this phase excludes `[long]`, so SC-010 (b)/(d) — the phase's ONLY criteria that
  observe a rising edge actually spawning a grain — ran in no gate while the tag stood. Untagged, the
  three cost 17.5 s in the per-push lane and FR-031's positive path is gated on every push. FR-049's
  untagged twin `AtmosphereGhost_ReverseLiveness_Short` is now redundant but is **kept**: deleting a
  shipped case to tidy a tag decision is not a trade this phase makes. [FR-048, FR-049, SC-003,
  SC-007 (d), SC-010 (b)]
- **B-6 — SC-012's chunk-1 assertions were written for one birth site per sample.** They read
  `getActiveGrainCount() == kMaxGrains` and `Δ getTotalGrainsRetired() ≤ 1`; under FR-020's second
  birth site the `i = 0` pair (scheduler *and* trigger) are both due inside chunk 1 and both retire at
  the end-of-chunk drain, so the measurement is `active == 62`, `Δ retired == 2` and the reviewed pair
  is red on correct code. Weaken, or restate? → **Restate, into the two facts the original pair was
  reaching for**: `getActiveGrainCount() + Δ getTotalGrainsRetired() == kMaxGrains` (the pool DID reach
  its cap — strictly stronger than the original equality on a chunk where retirements occur) and
  `Δ getTotalGrainsRetired() ≤ 2` (the only retirements were the `i = 0` pair — the tight bound). The
  teeth of SC-012 are in chunk 2 and are untouched. [SC-012, FR-051]
- **B-7 — the `kMaxGrains × 2` ASan arm cannot be taken from the MSVC ASan build.** MSVC's ASan hangs
  inside its own error-reporting path on this fixture: three attempts (600 s, 180 s, 300 s) left a
  truncated log and a process holding 0.27 s of CPU, unchanged by
  `ASAN_OPTIONS=symbolize=0:print_stacktrace=0` and unchanged by removing the `AllocationScope` whose
  global `operator new` override was the first suspect. (The previous session hit the same wall and
  recorded a 2-byte artifact.) Accept the hang as "consistent with an abort", or measure elsewhere? →
  **Measure elsewhere.** A hang is not evidence of an overflow; it is evidence of a hang. The red arm
  is taken from **g++ 13.3.0 `-fsanitize=address`, WSL2 Ubuntu 24.04**, running the identical fixture
  as a standalone program, which names the write site and the allocation. The green arm is recorded
  on **both** toolchains so the pair is comparable. [SC-012, FR-051]
- **B-8 — `kSaturatedDrainFactor` is frozen from the WORST of three clean runs, not the first.**
  T022's rule says "the FIRST clean measurement plus the documented 0.6–7.4 % drift headroom". That
  headroom was quoted from `vorago_perf_test.cpp:236-247`, where it describes the drift of ONE figure;
  this bar is a RATIO of two, and they do not drift together. Three isolated, P-core-pinned runs with
  nothing else executing measured arm 5 stable to ±5 % (282 988 … 313 516 ns/block) and arm 1 —
  a ~30 µs figure at 3.6 concurrent grains — swinging 23 % (27 317.5 … 33 663.5), so the quotient read
  9.31322, 10.4541 and 8.49076. Freezing run 1's 10.012 was tried and **run 2 exceeded it on identical
  code**, which is precisely the false red the isolation rule exists to prevent. Freeze the first, or
  the worst? → **The worst**: `ceil(10.4541 × 1.075 × 1000) / 1000 = 11.239`. That is a 2.7×
  TIGHTENING of the 30.0 placeholder, a fourth pinned run holds it (ratio 10.1715), and the ratio's
  measured spread is recorded at the constant so no later reader re-derives it. [SC-009, FR-041]

---
## Compliance record

**Filled 2026-09-24 (T029), re-verified and completed 2026-09-24 (remediation pass).** Base commit
`374580d7d0f0631561413310bd3085e15ba7279c`; the phase's work is uncommitted, so **every diff range
below is `git diff HEAD …`**, which is FR-045's stated pre-commit form. Machine: CODEBOX, 13th Gen
Intel Core i9-13900HX, Windows 11 Pro 26200, x64. Toolchain: MSVC 19.44 (`/O2`, Release) unless a row
says otherwise; the MSVC ASan leg is VS 2022 `-DENABLE_ASAN=ON`, Debug; the GNU/LLVM legs are
g++ 13.3.0 and clang++ 18.1.3 under WSL2 Ubuntu 24.04.

**Every ❌ and ⚠️ of the T029 record is closed below.** Nothing was closed by relaxing a threshold,
shrinking a workload or deleting a case: four bounds were *tightened* (two fingerprint tolerances per
comparison, and `kSaturatedDrainFactor` 30.0 → 11.239), three `[long]` tags were *removed* so their
cases run on every push, one criterion gained an assertion arm it never had (SC-001 clause 2's trigger
replica), and the remaining rows are measurements that had not been taken.

**Evidence artifacts** this record cites by name. All are in
`specs/vorago-phase10a-ghost-extension/artifacts/`, produced 2026-09-24 on the tree this record
describes:

| Tag | Artifact | What it is |
|---|---|---|
| `[SUITE]` | `dsp_systems_tests_gate_final.log` | full `dsp_systems_tests` at the phase filter `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]` |
| `[GHOST]` | `t025_ghost_all_durations.log` | every `[ghost]` case except `[.perf]`, `-d yes -s` — the runtime table's source |
| `[EFFECTS]` | `dsp_effects_tests_gate_final.log` | |
| `[PROCESSORS]` | `dsp_processors_tests_gate_final.log` | |
| `[SERAPHIS]` | `seraphis_tests_gate_final.log` | |
| `[PLUGINVAL]` | `seraphis_pluginval_final.log` | `tools/pluginval.exe --strictness-level 5 --validate` on the rebuilt bundle |
| `[PROBE-GNU]`, `[PROBE-GNU-FM]`, `[PROBE-LLVM]` | `fr046_probe_gcc_O3.txt`, `fr046_probe_out_g++_O3ffastmath.txt`, `fr046_probe_out_clang++_O2.txt` | FR-046's three-toolchain probe, raw per-metric output |
| `[SC011-OFF]` | `sc011_clause_disabled_run.log` | the FR-050 clause commented out: arm (a) red, plus the (ii) and (iii) probes |
| `[ASAN-x3-MSVC]` | `sc012_asan_x3_run.log` | MSVC ASan Debug, `kMaxGrains * 3` |
| `[ASAN-x2-GNU]`, `[ASAN-x3-GNU]` | `sc012_asan_linux_x2_run.log`, `sc012_asan_linux_x3_run.log` | g++ 13.3.0 `-fsanitize=address`, the red and green arms |
| `[PERF1]`…`[PERF5]` | `sc009_isolated_run1.log`, `sc009_isolated_run2_frozen.log`, `sc009_isolated_run3.log`, `sc009_isolated_run4_frozen.log`, `sc009_isolated_run5_duration.log` | five P-core-pinned SC-009 runs, each alone after a 60–90 s idle |
| `[TIDY]` | `clang_tidy_dsp_final.log` | `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` |
| `[GATES]` | live re-runs of `check-seraphis-green.js` / `check-portability.js` / `lint-layers.js` / `lint-odr.js` | |

**Legend.** ✅ = verified against the cited file:line or artifact line. ⚠️ = partly satisfied, with the
unsatisfied part named. ❌ = not satisfied or not measured. No row says "implemented" or "test passes"
without a citation.

### Functional requirements

| Req | Verdict | Evidence |
|---|---|---|
| FR-001–FR-009 (reverse control surface) | ✅ | `setGrainReverseProbability` / `getGrainReverseProbability` / `getLastBornGrainReversed` / `getTotalReverseGrainsBorn` / `getReverseRngState` all present on `atmosphere_engine.h`; exercised by `AtmosphereGhost_RtSafety` (`atmosphere_ghost_test.cpp:482`) and `AtmosphereGhost_NonFiniteSetter` (`atmosphere_ghost_nonfinite_test.cpp:110`), both green in `[SUITE]`. |
| FR-010–FR-017 (reverse read path) | ✅ | Direction-templated span lambda `runSpan` at `atmosphere_engine.h:2091`, dispatched once at `:2178`; backwards walk `advanceBy` at `:2066`. Covered by `AtmosphereGhost_ReverseIsTimeReversed` (`atmosphere_ghost_test.cpp:3027`) and `AtmosphereGhost_ReverseTruncation` (`:2382`), green in `[SUITE]`. |
| FR-012 (amended, R-9: `readFrac ∈ [0,1]`) | ✅ | Field comment reads `fraction in [0,1] - a reverse grain's borrow can land on exactly 1.0f` (`atmosphere_engine.h:1309-1312`). |
| FR-013 (direction resolved once per span) | ✅ | Structural gate — see *SC-006 clause 6, anchor 1*: `grain.reversed` occurs **once** in `renderGrainSpan`, at `:2178`, outside both per-sample loops; **zero** occurrences inside either loop body. |
| FR-018–FR-027 (event-triggered grains) | ✅ | `triggerGrain()` at `atmosphere_engine.h:1087-1091` (FR-019's cap is the `pendingTriggers_ >= kMaxGrains` test at `:1087`); consumption at `:2394-2395` through the `consumePendingTrigger` lambda declared at `:2345-2351`, whose `--pendingTriggers_` at `:2346` is FR-022's consumed-exactly-once decrement. `AtmosphereGhost_TriggerAccounting` (`atmosphere_ghost_test.cpp:3190`) green in `[SUITE]`. FR-027's four-draws-per-attempt contract is now **asserted**, not only documented — see SC-001 clause 2's trigger arm. |
| FR-025 (pending predicate hoisted, zero cost when empty) | ✅ **closed** | Was ⚠️ at T029 because the binding gate (SC-006 clause 6 anchor 2) counts tokens and the count was 2. **The code was changed to meet the count, not the count to meet the code.** `anyPending` is declared above the `for` at `atmosphere_engine.h:2334`; FR-022's consumption moved into the `consumePendingTrigger` lambda at `:2345-2351`, also above the loop; the per-sample body now reads `if (anyPending && pendingTriggers_ > 0u) { consumePendingTrigger(i); }` (`:2394-2395`). Measured: `pendingTriggers_` occurs in the body of the `for` opened at `:2353` and closed at `:2397` **exactly once**, at `:2394`, as the right operand of the short-circuited `&&`. |
| FR-030–FR-034 (Vorago wiring, default OFF) | ✅ **closed** | `git diff HEAD --numstat -- dsp/include/krate/dsp/systems/vorago_engine.h` = **55 added / 0 deleted**, a pure append. All five `VoragoEngine_GhostExtensionWiring` clauses green in `[SUITE]`, clause (a)'s fingerprint arm included (it SKIPped at T029). |
| FR-040 (default-inert) | ✅ **closed** | SC-001 clauses 1, 2 and 3 all green in `[SUITE]`. Clause 1 (render identity) now runs at measured bounds and reports **worst metric relative error 0, worst sample error 0** against `kBaseCommitFingerprint` — an exact match with the base commit, not merely a within-tolerance one. |
| FR-041 (no ceiling/baseline/threshold value changed) | ✅ | `git diff HEAD --numstat -- dsp/tests/unit/systems/vorago_perf_test.cpp` = **9 added / 7 deleted**; each deletion is a `constexpr` definition replaced by a `using` of the identical value. The seven literals live at `vorago_perf_budget.h:72, 74, 78, 82, 99, 137, 138`. No number changed. `kSaturatedDrainFactor` is NOT one of them: it is this phase's own constant in this phase's own new TU (see SC-009). |
| FR-042 (test placement) | ✅ | Five TUs enumerated in `dsp/tests/CMakeLists.txt`'s `dsp_systems_tests` list; **only** `atmosphere_ghost_nonfinite_test.cpp` is added to the `-fno-fast-math` block; the perf TU is deliberately not in it. CMake diff **30 added / 0 deleted**. |
| FR-043 (portability) | ✅ | `[GATES]`: `check-portability: all clear -- 6 compiled, 2 skipped.` — `atmosphere_ghost_longrun_test.cpp`, `atmosphere_ghost_nonfinite_test.cpp`, `atmosphere_ghost_perf_test.cpp`, `atmosphere_ghost_test.cpp`, `vorago_ghost_ext_test.cpp`, `vorago_perf_test.cpp`, each `OK`. Independently, the FR-046 probe **compiled and ran the component and the whole engine** under g++ 13.3.0 (`-O3` and `-O3 -ffast-math`) and clang++ 18.1.3 (`-O2`) — a stronger portability statement than syntax-only: `advanceBy.template operator()<kBackwards>()` compiles and *renders correctly* on both. |
| FR-044 (layer discipline) | ✅ | `[GATES]`: `lint-layers: OK — no layer-dependency violations in 5-layer DSP tree.` `atmosphere_engine.h`'s include list gains nothing: the `-U0` hunk list jumps from `@@ -36,0 +37,9 @@` straight to `@@ -335,0 +345,6 @@`, so no line of the pre-change `:141-155` include block is touched. |
| FR-045 (append-only bar, six anchors) | ✅ **closed, with the sites restated (B-4)** | **122 deletions, every one matched by a frozen anchor pattern** — per-anchor table below. The T029 record marked this ✅ while its own anchor table said two sites came out "wider than predicted"; **B-4 amends FR-045 (ii) and (iii) to the measured ranges** and gives the reason (`git diff HEAD -w` shows the extra lines are re-indentation into the direction-templated lambda, the only substantive change inside the loops being `advance();` → `advanceBy.template operator()<kBackwards>();`). `[GATES]`: `[3] dsp/include/krate/dsp/systems/atmosphere_engine.h: modified, 122 deletion(s), all expected -- default-inert`. |
| FR-046 (measured-bounds fingerprint protocol) | ✅ **closed** | **The three-toolchain probe ran** (B-1's method, ruling honoured to the letter). Figures, bounds and method in *FR-046 — the three-toolchain probe* below; the bounds are recorded in this spec, in both TUs, and are **tighter** than the placeholders they replace. `kMeasuredBoundsLanded`, its accessor and both SKIP guards are deleted: `grep -n T025` over both TUs returns nothing. |
| FR-047 (shared perf-budget header, seven names) | ✅ | Seven definitions moved — the FR-047 four plus `kReferenceNs`'s three derivation inputs — at `vorago_perf_budget.h:72, 74, 78, 82, 99, 137, 138`, all `inline constexpr`, no renamed constant, no new `static_assert`. `[GATES]`: `[1] dsp/tests/unit/systems/: 1 modified TU(s), all in scope`. |
| FR-048 (tag by measured cost) | ✅ **closed** | **Every one of the sixteen cases now has a measured runtime** (`-d yes`, table below), and the rule was then *applied*, not assumed: three `[long]` tags were estimates quoting AUDIO durations and come off on the measurement (B-5). |
| FR-049 (per-push twin of SC-003) | ✅ | `AtmosphereGhost_ReverseLiveness_Short`, untagged, at `atmosphere_ghost_test.cpp:2511`, same full-ring pre-roll. Green in `[SUITE]`, 0.273 s. Now redundant (its parent is untagged too) and deliberately **kept**: B-5 does not delete a shipped case to tidy a tag decision. |
| FR-050 (reverse fill-up admission clause) | ✅ **closed** | Clause verbatim at `atmosphere_engine.h:1899-1907`, a pure insertion (hunk `@@ -1742,0 +1889,20 @@`), consuming no FR-045 site. **All three clause-disabled differential measurements are now recorded** — table below, from `[SC011-OFF]`. |
| FR-051 (pass-A scratch at `kMaxGrains * 3`) | ✅ **closed** | `retiredScratch_.assign(kMaxGrains * 3, …)` / `dueScratch_.assign(kMaxGrains * 3, …)` at `atmosphere_engine.h:470-471`. The measured chunk-2 delta is now in an artifact (a `WARN` at `atmosphere_ghost_test.cpp:3728`, printed unconditionally), and **both ASan outcomes are evidenced** — red at ×2, green at ×3 — see the FR-051 table. |

### Success criteria

| SC | Verdict | Evidence |
|---|---|---|
| SC-001 clause 1 (render identity at measured bounds) | ✅ **closed** | `AtmosphereGhost_DefaultInert` §"clause 1", `[GHOST]`: `worst metric relative error 0 (bound 0.001), worst sample error 0 (bound 0.001)`. Runs unconditionally; the SKIP guard is gone. |
| SC-001 clause 2 no-trigger arms | ✅ | `getGrainRngState() == kBaseCommitGrainRngState` at p = 0 and p = 1, integer equality, green in `[SUITE]`. |
| SC-001 clause 2 **trigger arm** | ✅ **newly implemented** | It did not exist at T029 — no `getGrainRngState()` replica arm was ever written for triggered births. Now at `atmosphere_ghost_test.cpp:1346`, `[GHOST]`: `triggeredDelta=100 coldDelta=0 poolFullDelta=0 droppedDelta=0 bornTotal=100`, then `attempts=101 replica=534194471 engine=534194471`. The replica is `Xorshift32{deriveStreamSeed(1u, AtmosphereEngine::kGrainSalt)}` advanced four draws per attempt that reached the draw site; the span's own `skipRingCold_` delta is asserted **zero**, so the collapsed "four per admitted birth" identity the spec states is what the arm exercises, while the replica itself also accounts for the cold pre-roll's one rejected attempt. |
| SC-001 clause 3 (counter identity) | ✅ | `kBaseCommitTotalBorn / TotalRetired / SkipPoolFull / SkipRingCold / LatencySamples` all equal, green in `[SUITE]`. |
| SC-002 (measurably time-reversed) | ✅ | `AtmosphereGhost_ReverseIsTimeReversed` (`:3027`) green, 0.078 s. |
| SC-003 (liveness, no click) | ✅ | `AtmosphereGhost_ReverseLiveness` (`atmosphere_ghost_longrun_test.cpp:623`) green, **7.184 s**, now **untagged** (B-5) and therefore in the per-push lane and in `[SUITE]`'s 1 389 cases. |
| SC-004 (truncation at the extremes) | ✅, never `[long]` | `AtmosphereGhost_ReverseTruncation` (`atmosphere_ghost_test.cpp:2382`) carries `[atmosphere][ghost]` and nothing else; measured 0.082 s, so R-8's forbidden tagging never arose and no `_Sweep` sibling was needed. |
| SC-005 (one grain per trigger, pool-bounded) | ✅ | `AtmosphereGhost_TriggerAccounting` (`:3190`) green, 0.019 s. |
| SC-006 clause 1 | ✅ | `git diff HEAD --numstat --diff-filter=M -- dsp/tests/unit/systems/` returns exactly one row: `9  7  dsp/tests/unit/systems/vorago_perf_test.cpp`. All seven new files untracked additions. The SC-011 probe TU and the temporary ASan edit used during this remediation were both **removed**; `git status --porcelain -- dsp/tests/unit/systems/` lists no `zz_*` file. |
| SC-006 clause 2 | ✅ | 122 deletions over six anchors, all matched — section below. |
| SC-006 clause 3 | ✅ | `[SUITE]`: `All tests passed (5952931 assertions in 1389 test cases)` — **zero skipped** (T029 had 2) and three cases more than T029's 1 386, the three B-5 untaggings. No `atmosphere_engine_*` or `seraphis_*` TU appears in `git status --porcelain -- dsp/tests/unit/systems/`. *(T029's clause-3 row named `AtmosphereEngine_NonFiniteGuardSurvivesFastMath`; no such case exists — the `-fno-fast-math` TU's only case is `AtmosphereEngine_NonFiniteHygiene` at `atmosphere_engine_nonfinite_test.cpp:321`, and that file is unmodified. The clause's substance — the exclusion list gains a line and deletes none — holds.)* |
| SC-006 clause 4 | ✅ | `[EFFECTS]`: `All tests passed (115009 assertions in 495 test cases)`. `[PROCESSORS]`: `All tests passed (10697080 assertions in 3311 test cases)`. `vorago_engine_test.cpp` is not in the modified list, so Phase 10's `VoragoEngine_GhostConfiguration` (SC-027) is green with no edit. |
| SC-006 clause 5 | ✅ **closed (4 of 4)** | `git diff HEAD --numstat --diff-filter=M -- plugins/seraphis/` = **zero rows**. Gate build `grep -c "warning C"` = **0** across `dsp_systems_tests`, `seraphis_tests`, `Seraphis`, `dsp_effects_tests`, `dsp_processors_tests`; the build's only errors are the documented benign post-build `MSB3073` preset-copy failure into `C:\ProgramData`. `[SERAPHIS]`: `All tests passed (444660 assertions in 109 test cases)`. **`[PLUGINVAL]`: exit 0, zero `FAIL` lines**, on the bundle rebuilt at 2026-09-24 07:31:08, later than every source this phase touches. |
| SC-006 clause 6 | ✅ **closed, both anchors literal** | Token counts quoted below. Anchor 1: `grain.reversed` once in `renderGrainSpan`, zero inside either loop. Anchor 2: `pendingTriggers_` **exactly once** in the per-sample body, as the right operand of the `anyPending &&` — the T029 measurement of 2 was closed by moving the decrement out of the body (FR-025 row), not by amending the clause. |
| SC-007 (seeding and determinism) | ✅ | `AtmosphereGhost_Determinism` (`atmosphere_ghost_test.cpp:1456`, 0.513 s) and `AtmosphereGhost_Determinism_ReverseFraction` (`atmosphere_ghost_longrun_test.cpp:705`, 1.397 s, now untagged) green in `[SUITE]`. |
| SC-008 (RT safety, setter contracts) | ✅ | `AtmosphereGhost_RtSafety` (`:482`, 0.107 s) and `AtmosphereGhost_NonFiniteSetter` (`atmosphere_ghost_nonfinite_test.cpp:110`, 0.001 s) green in `[SUITE]`. |
| SC-009 (global-stage CPU delta) | ✅ **closed** | Five isolated P-core-pinned runs, table below. **`kSaturatedDrainFactor` is transcribed and frozen at 11.239** (`atmosphere_ghost_perf_test.cpp:218`), a 2.7× tightening of the 30.0 placeholder, derived by the stated formula from the **worst** of three clean measurements rather than the first (B-8, with the reason measured). Two independent pinned runs hold it afterwards. |
| SC-010 (Vorago wiring) | ✅ **closed** | All five clauses green. (a) both halves: the config-default half, and the 60 s fingerprint half at measured bounds — `worst metric relative error 0 (bound 0.005), worst sample error 0 (bound 0.006)`. (b)/(d) — the phase's only observation of a rising edge spawning a grain — ran in `[SUITE]` for the first time: `burst edges: 20 / d triggeredBorn: 20 / d dropped: 0 / d skipPoolFull: 0 / d skipRingCold: 0 / max level: 0.5956 / max ghost req: 0.9926`, with `edges <= deltaTriggered` (20 ≤ 20), `deltaTriggered <= edges + 1` (20 ≤ 21) and `edges >= 6` (20 ≥ 6). (c) closed-gate arm: `burstEdges := 0, maxLevel := 0.0f, maxGhostRequest := 0.992646`. (e): 25 grains born, all reverse. |
| SC-011 (FR-050's binding criterion) | ✅ **closed** | `AtmosphereGhost_ReverseFillDeficit` (`atmosphere_ghost_test.cpp:1899`) green, 0.106 s, arms (a), (a2) and (c), no arm (b) by design. **All three clause-disabled differentials recorded** — table below. |
| SC-012 (pass-A scratch bound) | ✅ **closed** | Chunk-2 teeth green with the measured number now in an artifact: `[GHOST]`, `SC-012 measured: chunk 1 active=62 deltaRetired=2; chunk 2 deltaRetired=188 (teeth: > 128, FR-051 bound: <= 192)`. Chunk-1's assertions are the B-6 restatement, which the spec now carries. Both ASan arms evidenced (B-7). |

---

### FR-045 — the six anchors, measured

`git diff HEAD --numstat -- dsp/include/krate/dsp/systems/atmosphere_engine.h` → **394 added, 122
deleted**. Grouping `git diff HEAD -U0`'s hunks by anchor, using pre-change line numbers:

| Anchor | Pre-change range(s) | Deleted | Against FR-045 **as amended by B-4** |
|---|---|---:|---|
| (i) the `wUp`/`wDown` pair | `:1651-1652` | **2** | as stated |
| (ii) the `advance` lambda plus the whole span body that calls it | `:1872-1880`, `:1884-1928`, `:1931-1941`, `:1943-1950`, `:1953-1966` | 9 + 45 + 11 + 8 + 14 = **87** | as stated (B-4 restated the range to these five hunks; `git diff HEAD -w` shows the additional lines are re-indentation into the direction-templated lambda) |
| (iii) the pass-A scheduler tick | `:2110-2114`, `:2116-2135` | 5 + 20 = **25** | as stated (B-4 restated `:2115` alone — which is context, not a deletion — to these two hunks; the 20th line is the 16-space `}` closing `if (activeCount_ > before)`, which the FR-025 lambda extraction re-aligned) |
| (iv) the `setSeed` / `prepare` / `reset` seeding lines | `:555-556`, `:1015-1017` | **0** | narrower than stated — pure appends (`@@ -556,0 +587 @@`, `@@ -1016,0 +1111 @@`) |
| (v) `prepare()` step 5b | `:437-441` | **5** | within the stated `:436-441` |
| (vi) the two comments this phase falsifies | `:1183`, `:2595-2596` | 1 + 2 = **3** | as stated |
| | **total** | **122** | matches `--numstat` exactly |

Every count is frozen into `tools/check-seraphis-green.js`'s `APPEND_ONLY_HEADERS` entry (`:108`
onwards; the 16-space brace pattern, whose frozen count moved 1 → 2 with the FR-025 lambda
extraction, is at `:348` with the reason recorded beside it). The script fails both on a deletion
matching no pattern and on a pattern whose tally drifts. `[GATES]`:

```
  [3] dsp/include/krate/dsp/systems/atmosphere_engine.h: modified, 122 deletion(s), all expected -- default-inert
check-seraphis-green: in scope
```

**No public declaration, default, constant or Seraphis-observable value is among the 122.** The
strongest evidence for that is not the pattern table but SC-001 clause 1 and SC-010 (a): two 60 s
renders, one of the component alone and one of the whole engine, both now compared against
base-commit fingerprints and both reading **exactly 0** deviation on every metric and all 32
checkpoints.

### SC-006 clause 6 — the two token counts, quoted

**Anchor 1 — `renderGrainSpan`.** `grain.reversed` occurs **exactly once** in the whole function, at
`atmosphere_engine.h:2178`, as the dispatch over a direction-templated lambda, and **zero** times
inside either per-sample loop:

```cpp
        const auto runSpan = [&]<bool kBackwards>() noexcept {            // :2091
            ...
                for (std::size_t i = start; i < spanEnd; ++i) {           // :2098
                    foldAt(i, ageAt(i));
                    advanceBy.template operator()<kBackwards>();          // :2100
        ...
        if (grain.reversed) {                                             // :2178
            runSpan.template operator()<true>();                          // :2179
        } else {
            runSpan.template operator()<false>();                         // :2181
        }
```

Measured: occurrences of `reversed` in `renderGrainSpan` = **1**; inside the loop bodies = **0**. ✅

**Anchor 2 — the pass-A loop.** `anyPending` is declared **above** the `for`, and `pendingTriggers_`
occurs in the per-sample body **exactly once**:

```cpp
        const bool anyPending = pendingTriggers_ > 0u;                    // :2334  (above the for)

        // FR-022's consumption, lifted OUT of the per-sample body so that
        // `pendingTriggers_` occurs there EXACTLY ONCE ...
        const auto consumePendingTrigger = [this, &birthAndTrack](std::size_t sampleIndex) {
            --pendingTriggers_;                                           // :2346  (above the for)
            if (birthAndTrack(sampleIndex)) { ++totalTriggered_; }
        };

        for (std::size_t i = 0; i < numSamples; ++i) {                    // :2353
            ...
            if (anyPending && pendingTriggers_ > 0u) {                    // :2394  the ONLY occurrence
                consumePendingTrigger(i);                                 // :2395
            }
        }                                                                 // :2397
```

Measured by `awk 'NR>=2353 && NR<=2398' … | grep -c "pendingTriggers_"` → **1**, and that occurrence
is the right operand of a short-circuited `&&` whose left operand is the loop-invariant `anyPending`,
exactly as R-7 requires. ✅

**How this row changed, stated plainly.** At T029 the count was **2** (the `--pendingTriggers_`
decrement sat inline in the guarded branch) and the record proposed "either the clause amended to
'exactly once outside the guarded branch' or a ruling". Neither was taken. The semantics were already
correct — the decrement is inside the `&&`-guarded branch, so the zero path loaded nothing — but
clause 6 is deliberately a **token count a reviewer discharges from a `-U0` excerpt**, not a semantic
property they must re-derive, and a criterion of that shape is worth less every time it is reinterpreted
to fit. The code was shaped to the count instead. Cost: one named lambda, zero behaviour change
(`[SUITE]` is green and SC-001 clause 1's fingerprint reads exactly 0 against the base commit).

### FR-046 — the three-toolchain probe

**Method (B-1, followed as ruled).** Both fixture recipes — Fixture A (`atmosphere_ghost_fixtures.h`
section 6.1) and Fixture B (section 6.2, the B-2 sounding recipe) — re-implemented as a standalone
`main()` driving the identical render loops, compiled against `dsp/include` + `tests/test_helpers` +
the KrateDSP `.cpp` list + pffft + Highway, with `enableFTZDAZ()` called before every render, under

```
g++ 13.3.0      -std=c++20 -O3
g++ 13.3.0      -std=c++20 -O3 -ffast-math
clang++ 18.1.3  -std=c++20 -O2
```

on Ubuntu 24.04 (WSL2, build dirs on F:), and diffed against the MSVC 19.44 `/O2` **in-suite**
fingerprints stored as `kBaseCommitFingerprint` / `kBaseCommitVoragoFingerprint`. **The recipe is
confirmed, not assumed:** all three legs report `fixtureA.grainRngState 8918586` and
`fixtureA.totalBorn 17`, the same integers SC-001 clauses 2 and 3 pin against the base commit.

**Fixture A** — relative deviation from the stored MSVC reference, per metric:

| Leg | rms | peak | meanAbs | totalVariation | worst checkpoint (abs) |
|---|---:|---:|---:|---:|---:|
| g++ `-O3` | 2.5702e-7 | **2.7235e-4** | 1.9474e-6 | 9.7665e-6 | **2.9029e-4** (cp 20) |
| g++ `-O3 -ffast-math` | 2.9607e-8 | 1.5922e-6 | 1.3755e-7 | 3.9244e-7 | 8.3121e-6 |
| clang++ `-O2` | 2.5702e-7 | 2.7235e-4 | 1.9474e-6 | 9.7665e-6 | 2.9029e-4 |
| MSVC 19.44 `/O2`, in-suite | **0** | **0** | **0** | **0** | **0** |

**Fixture B** — same form:

| Leg | rms | peak | meanAbs | totalVariation | worst checkpoint (abs) |
|---|---:|---:|---:|---:|---:|
| g++ `-O3` | 7.7292e-4 | 1.4398e-3 | 5.4546e-4 | 1.0330e-5 | 2.0687e-3 |
| g++ `-O3 -ffast-math` | 7.1260e-4 | **1.5878e-3** | 4.8765e-4 | 6.7619e-5 | **2.1693e-3** (cp 18) |
| clang++ `-O2` | **8.4859e-4** | 1.2995e-3 | **6.2286e-4** | **6.8181e-5** | 2.1012e-3 |
| MSVC 19.44 `/O2`, in-suite | **0** | **0** | **0** | **0** | **0** |

The MSVC row is measured, not asserted: `[GHOST]` prints `worst metric relative error 0 … worst
sample error 0` for both comparisons, which is B-3's predicted exact self-consistency now observed.

**The bounds, recorded here as FR-046 requires and never to be widened without a ruling:**

| Comparison | Measured worst | Bound | Headroom | In the tree at |
|---|---:|---:|---:|---|
| Fixture A, aggregate metric | 2.7235e-4 | **1.0e-3** | 3.7× | `atmosphere_ghost_test.cpp:881` |
| Fixture A, checkpoint sample | 2.9029e-4 | **1.0e-3f** | 3.4× | `atmosphere_ghost_test.cpp:877` |
| Fixture B, aggregate metric | 1.5878e-3 | **5.0e-3** | 3.1× | `vorago_ghost_ext_test.cpp:134` |
| Fixture B, checkpoint sample | 2.1693e-3 | **6.0e-3f** | 2.8× | `vorago_ghost_ext_test.cpp:130` |

All four are **tighter than the placeholders they replace** (2.0e-3f / 1.0e-2 in both TUs): measuring
bought discrimination, not slack. Each clears the `static_assert` pair that forbids a bound tighter
than the shared `render_fingerprint.h` constants (5.0e-4f / 2.5e-4) and forbids editing those shared
constants for one caller's sake.

**Why Fixture B's spread is ~6× Fixture A's**, recorded rather than left as an oddity: Fixture B walks
the whole engine over 60 s, so a legal reassociation early in an OU-drifted trajectory moves where
every later sample lands, whereas Fixture A drives one component whose grain stream is integer-pinned.
Ruling B-3's standalone-vs-in-suite term (1.0797e-4 peak on MSVC) is **contained** in the GNU/LLVM
figures, since those are standalone builds compared against the in-suite reference; at ~7 % of a
1.6e-3 spread the headroom covers it several times over.

**Both SKIP guards are gone**, not left standing at `true`: `kMeasuredBoundsLanded`, its accessor and
the two `SKIP(...)` blocks are deleted from both TUs, and `grep -n T025` over both returns nothing.

### FR-050 — the three clause-disabled differential measurements

The clause, at `atmosphere_engine.h:1899-1907`, was commented out; `dsp_systems_tests` was rebuilt and
`AtmosphereGhost_ReverseFillDeficit` re-run alongside a throwaway probe TU that reports (ii) and (iii)
(`[SC011-OFF]`; the probe TU and the commented-out clause were both reverted afterwards, and
`check-seraphis-green.js` is green on the restored tree).

| Measurement (SC-011, plan §6 step 4) | Result with the clause OUT | Status |
|---|---|---|
| (i) arm (a) red (`Δ getTotalGrainsBorn() ≥ 1`), green with it in | **RED**, `atmosphere_ghost_test.cpp(2037): FAILED: REQUIRE( bornDelta == std::uint64_t{0} )` `with expansion: 1 == 0`, message `(a) bornDelta=1 coldDelta=0 poolFullDelta=0` | ✅ recorded |
| (ii) arm (a2) red with it out (first birth at `A ≈ 240 064` against an RHS of at least `292 864`) | **RED**: `A_born = 240064`, `birthAge = 29792.1`, `L' = 576000`, `rMax = 0.5`, `rhs = 317857`, `A_born >= rhs ? NO (a2 is RED)` — the predicted `A ≈ 240 064` exactly, against an RHS above the predicted floor | ✅ recorded |
| (iii) the stale read: `getMaxObservedGrainAgeSamples()` exceeds `min(samplesRendered, C) − 2.0` at some block boundary | **Observed**: first at **354 112** samples rendered (`maxObservedAge = 354 113` against `available − 2 = 354 110`), growing to a **worst excess of 178 358 samples** by the end of the grain's life (`maxObservedAge = 894 410`, total rendered 816 000, capacity 1 048 576) | ✅ recorded |

The prediction for (iii) was "`t > 2 × (239 998 − birthAge − decorr)` grain-samples, ≈ 297 600 … 470 400
for the `[4 800, 91 200]` birth ages this fixture draws". The observed crossing at 354 112 rendered
samples — i.e. 114 048 samples after the 240 064 birth, with the read age then running ~1.5× the
render — sits inside that window. And arm (c), the forward arm, stayed **green with the clause out**,
which is the "never evaluated for a forward grain" half of FR-050 observed rather than argued.

### FR-051 — SC-012's delta and both ASan outcomes

| Item | Expected (spec) | Measured | Verdict |
|---|---|---|---|
| Portable delta, chunk 2 | ≈ 186 | **188**, now in an artifact: `[GHOST]` / `[ASAN-x3-MSVC]` / `[ASAN-x3-GNU]` all print `chunk 2 deltaRetired=188`. The figure moved from an `INFO` (printed only on failure) to a `WARN` at `atmosphere_ghost_test.cpp:3728`, the convention the phase's other measured figures already use | ✅ |
| Structural lower bound | `> kMaxGrains * 2` = **> 128** | asserted at `:3718`, green everywhere | ✅ |
| Structural upper bound | `<= kMaxGrains * 3` = **≤ 192** | asserted at `:3719`, green everywhere | ✅ |
| Chunk 1 | B-6's restatement | `active=62`, `deltaRetired=2`; `deltaRetiredChunk1 <= 2` (`:3685`) and `activeAfterChunk1 + deltaRetiredChunk1 == kMaxGrains` → `64 == 64` (`:3686`) | ✅ |
| ASan at `kMaxGrains * 3` | passes | `[ASAN-x3-MSVC]` (VS 2022 `-DENABLE_ASAN=ON`, Debug): `All tests passed (8 assertions in 1 test case)`. `[ASAN-x3-GNU]` (g++ 13.3.0 `-fsanitize=address`): `chunk 2: deltaRetired = 188 … NO SANITIZER ERROR` | ✅ |
| ASan at `kMaxGrains * 2` | **aborts with heap-buffer-overflow** | `[ASAN-x2-GNU]`, verbatim: `==867==ERROR: AddressSanitizer: heap-buffer-overflow … WRITE of size 5 at 0x519000000480 thread T0` / `#0 … renderGrainChunk(…)::{lambda(unsigned long)#1}::operator()(unsigned long) atmosphere_engine.h:2323` / `0x519000000480 is located 0 bytes after 1024-byte region [0x519000000080,0x519000000480)` / `allocated by … std::vector<…DueEntry>::_M_fill_assign` — i.e. the `dueScratch_[k] = DueEntry{…}` write, one element past a 128-entry vector | ✅ |

`atmosphere_engine.h:2323` is the newborn due-entry insert; `DueEntry` is 5 bytes, which is the
`WRITE of size 5`. The overflow appears **only** in chunk 2 and only at ×2, exactly where FR-051's
derivation says it must.

**Why the red arm is a GNU ASan run and not the MSVC one (B-7).** MSVC's ASan hangs inside its own
error-reporting path on this fixture: three attempts (600 s, 180 s, 300 s wall) each left a truncated
log at the chunk-1 boundary and a process holding **0.27 s of CPU** — blocked, not computing. Neither
`ASAN_OPTIONS=symbolize=0:print_stacktrace=0:halt_on_error=1` nor removing the `AllocationScope` (whose
global `operator new` override was the obvious suspect for a deadlock in a report path that allocates)
changed it. The previous session hit the same wall and recorded a 2-byte artifact. A hang is not
evidence of an overflow, so the red arm was measured where the reporter works; the green arm is
recorded on **both** toolchains so the pair is comparable.

### SC-009 — five arms, the frozen factor, and why it took three runs

Every run below: `pwsh tools/pin-perf-cores.ps1 -Exe …/dsp_systems_tests.exe -ExeArgs
AtmosphereGhost_CpuDelta`, executed **alone** after a 60–90 s idle, `pin-perf-cores: performance-core
mask 0xFFFF`. Shape: 300 warm-up blocks, best-of-12 × 200 blocks, each block 8 × 64-sample
`processStereoBlock` calls — 2 700 blocks per arm = 28.8 s of audio. Case runtime **1.587 s**
(`[PERF5]`, `-d yes`).

`[PERF1]`, the reference run, in full:

| Arm | ns/block | Ratio vs arm 1 | WARN absolute | WARN verdict | Grains born (triggered) | dropped / poolFull / active |
|---|---:|---:|---:|---|---|---|
| 1 — inert (p = 0, no triggers) | **33 663.5** | 1× | 31 114 | **ABOVE** | 8 (0) | 0 / 0 / 3 |
| 2 — reverse engaged (p = 1.0) | **32 367.5** | 0.961501× | 31 114 | **ABOVE** | 6 (0) | 0 / 0 / 3 |
| 3 — triggers, realistic (1 / 8.33 s) | **38 427** | 1.1415× | 42 428 | within | 11 (3) | 0 / 0 / 4 |
| 4 — triggers, stress (1 / 0.833 s) | **58 035.5** | 1.72399× | 70 714 | within | 42 (34) | 0 / 0 / 17 |
| 5 — saturated drain | **313 516** | 9.31322× | none — arm 5's figure *is* the measurement | — | 36 689 (36 689) | 0 / 1 331 661 / 64 |

In-run paired gates (the only REQUIREs, per Q6; arm 1 carries none because it *is* the reference), all
passed in `[PERF1]`: `arm2 <= arm1 x 1.1 -> 32367.5 vs 37029.9`; `arm3 <= arm1 x 1.5 -> 38427 vs
50495.2`; `arm4 <= arm1 x 2.5 -> 58035.5 vs 84158.8`; `arm5 <= arm1 x 30 -> 313516 vs 1.0099e+06`.

Clause 5, computed and printed, never REQUIREd, and dominated: `delta(arm3 − arm1) = 4763.5 → lhs =
2.82398e+06 <= 3.2e+06 ? yes`; `delta(arm4 − arm1) = 24372 → lhs = 2.84457e+06 <= 3.2e+06 ? yes`,
against the derived `delta <= 362 880` and the checked-in, unedited `kEngineBaselineNsAtPoly4 =
2.69448e+06`.

**`kSaturatedDrainFactor` is transcribed and frozen at 11.239** (`atmosphere_ghost_perf_test.cpp:218`).
T022's rule says "the FIRST clean measurement × 1.075". Applied literally that gives **10.012**, and it
was tried — `[PERF2]`, an equally clean pinned run on **identical code**, then read
`arm5 <= arm1 x 10.012 -> 285568 vs 273493`: **red**. The five runs show why:

| Run | arm 1 ns/block | arm 5 ns/block | arm5/arm1 | `ceil(×1.075)` | Verdict at the factor then in the tree |
|---|---:|---:|---:|---:|---|
| `[PERF1]` | 33 663.5 | 313 516 | 9.31322 | 10.012 | green (factor 30.0) |
| `[PERF2]` | 27 317.5 | 285 568 | 10.4541 | **11.239** | **red** (factor 10.012) |
| `[PERF3]` | 33 329 | 282 988 | 8.49076 | 9.128 | green (factor 10.012 — restored) |
| `[PERF4]` | 27 928 | 284 218 | 10.1715 | 10.935 | green (factor **11.239**) |
| `[PERF5]` | — | 294 384 | 10.9 (printed bound 485 660) | — | green (factor 11.239) |

Arm 5 is stable to ±5 % (282 988 … 313 516); **arm 1 swings 23 %** (27 317.5 … 33 663.5), and the
quotient inherits that swing inverted. The 0.6–7.4 % headroom T022 cites describes the drift of ONE
measured figure (`vorago_perf_test.cpp:236-247`), not of a ratio of two. So B-8 applies the stated
formula to the **worst** clean observation: `ceil(10.4541 × 1.075 × 1000) / 1000 = 11.239`. That is a
**2.7× tightening** of the 30.0 placeholder, two later pinned runs hold it, and the ratio's measured
spread is recorded at the constant so no later reader re-derives it. No bound was widened; no workload
was shrunk.

Arm 1 and arm 2 reading **above** their WARN absolute (33 663.5 / 32 367.5 against 31 114) is the
machine-to-machine drift Q6 anticipated when it made the absolutes non-REQUIREs; the paired in-run
ratios, immune to it, all pass with margin.

### FR-048 — the tagging decision, and every case's measured runtime

Sixteen cases ship in this phase. Runtimes are Catch2 `-d yes`, Release, MSVC 19.44, summed over every
leaf-section run of the case, from `[GHOST]` (and `[PERF5]` for the `[.perf]` case). Nothing else was
running.

| Case | TU:line | Tag | Measured runtime |
|---|---|---|---:|
| `AtmosphereGhost_AppendOnly` | `atmosphere_ghost_test.cpp:1749` | untagged | **0.000 s** |
| `AtmosphereGhost_PassAScratchBound` | `atmosphere_ghost_test.cpp:3598` | untagged (bounded-grid sentinel — never `[long]`) | **0.000 s** |
| `AtmosphereGhost_NonFiniteSetter` | `atmosphere_ghost_nonfinite_test.cpp:110` | untagged (NaN/Inf guard — never `[long]`) | **0.001 s** |
| `AtmosphereGhost_TriggerAccounting` | `atmosphere_ghost_test.cpp:3190` | untagged | **0.019 s** |
| `AtmosphereGhost_ReverseIsTimeReversed` | `atmosphere_ghost_test.cpp:3027` | untagged | **0.078 s** |
| `AtmosphereGhost_ReverseTruncation` | `atmosphere_ghost_test.cpp:2382` | untagged — **never `[long]`** (R-8) | **0.082 s** |
| `AtmosphereGhost_ReverseFillDeficit` | `atmosphere_ghost_test.cpp:1899` | untagged | **0.106 s** |
| `AtmosphereGhost_RtSafety` | `atmosphere_ghost_test.cpp:482` | untagged | **0.107 s** |
| `AtmosphereGhost_ReverseLiveness_Short` | `atmosphere_ghost_test.cpp:2511` | untagged (FR-049's twin) | **0.273 s** |
| `AtmosphereGhost_Determinism` | `atmosphere_ghost_test.cpp:1456` | untagged | **0.513 s** |
| `AtmosphereGhost_DefaultInert` | `atmosphere_ghost_test.cpp:1177` | untagged | **1.001 s** |
| `AtmosphereGhost_Determinism_ReverseFraction` | `atmosphere_ghost_longrun_test.cpp:705` | untagged — **`[long]` removed, B-5** | **1.397 s** |
| `AtmosphereGhost_CpuDelta` | `atmosphere_ghost_perf_test.cpp:404` | **`[.perf]`** (hidden tag; runs alone, never in the per-push lane) | **1.587 s** |
| `AtmosphereGhost_ReverseLiveness` | `atmosphere_ghost_longrun_test.cpp:623` | untagged — **`[long]` removed, B-5** | **7.184 s** |
| `VoragoEngine_GhostExtensionWiring_Engaged` | `vorago_ghost_ext_test.cpp:652` | untagged — **`[long]` removed, B-5** | **9.575 s** |
| `VoragoEngine_GhostExtensionWiring` | `vorago_ghost_ext_test.cpp:415` | untagged — see below | **15.802 s** |

**The rule, applied to those numbers rather than to the estimates.**

- Three cases carried `[long]` on an ESTIMATE that quoted **audio** durations ("a 10-minute render",
  "600 s", "a 100 000-grain sweep"). Measured, they cost 7.184 s, 9.575 s and 1.397 s — all under
  FR-048's ~15 s bar — so B-5 takes the tags off. The consequence is the substance: every gate command
  in this phase excludes `[long]`, so SC-010 (b)/(d), the phase's **only** criteria that observe a
  rising edge actually spawning a grain, had never executed in any gate. They now run on every push,
  and did run in `[SUITE]`.
- `VoragoEngine_GhostExtensionWiring` is the one case **over** the bar, at 15.802 s, and it stays
  untagged because FR-048's test is a conjunction: over ~15 s **AND** toolchain-independent. Its
  clause (a) is a stored-golden `RenderFingerprint` comparison at cross-toolchain-measured bounds —
  the definition of an assertion whose pass/fail depends on compiler codegen — so the second conjunct
  fails and the tag is forbidden. (Its 15.802 s is dominated by that clause's 60 s render, 5.031 s,
  and by clause (c)'s 600 s-of-audio closed-gate arm, 9.470 s.)
- No NaN/Inf-guard, bounded-grid or state-format case is tagged: `AtmosphereGhost_NonFiniteSetter`,
  `AtmosphereGhost_ReverseTruncation` and `AtmosphereGhost_PassAScratchBound` are all untagged, which
  is R-8 and the `CLAUDE.md` standing rule satisfied.
- Per-push cost of the whole `[ghost]` set after B-5: **~36 s** for 15 cases (`[GHOST]`,
  `All tests passed (801 assertions in 15 test cases)`), up ~18 s from the tagged arrangement, in
  exchange for FR-031's positive path being gated at all.

### Pre-roll sample counts actually used by each fixture

| Fixture / criterion | Sample rate | `captureSeconds` | Pre-roll samples | Seconds |
|---|---:|---:|---:|---:|
| SC-001, SC-002, SC-003, SC-005, SC-007, SC-008 and FR-049's twin, via `VoragoGhostFix::preRollFullRing` | 48 000 | 20 | **1 048 576** (`nextPowerOf2(960 000)`) | **21.845 s** |
| SC-001 clause 2's trigger arm, via `preRollFullRing` on SC-005's fixture | 48 000 | 1 (`kMinCaptureSeconds`) | **65 536** | **1.365 s** |
| SC-004, the truncation corner | 48 000 | 1 (`kMinCaptureSeconds`) | **65 536** | **1.365 s** |
| SC-010 (b) | 8 000 | 20 | **262 144** | **32.768 s** |
| SC-012, the pass-A scratch bound | 20 | 30 | **1 024** | 51.2 s of audio at that rate |
| SC-011, the **deliberate partial fill** — the one fixture that is not a full ring | 48 000 | 20 | **240 000** | **5.000 s** |

### Static gates, ODR sweep and clang-tidy

| Gate | Result |
|---|---|
| `node tools/check-portability.js` | `check-portability: all clear -- 6 compiled, 2 skipped.` |
| `node tools/lint-layers.js` | `lint-layers: OK — no layer-dependency violations in 5-layer DSP tree.` |
| `node tools/lint-odr.js` | `lint-odr: OK — 771 definitions scanned, no cross-file name collisions.` |
| `node tools/check-seraphis-green.js` | `check-seraphis-green: in scope` — `[1] dsp/tests/unit/systems/: 1 modified TU(s), all in scope`; `[3] atmosphere_engine.h: modified, 122 deletion(s), all expected -- default-inert`; `[4] plugins/seraphis/: 0 modified file(s), all in scope` |
| `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` | `[TIDY]`: `Files analyzed: 366`, `[OK] Errors: 0`, `[OK] Warnings: 0` |
| Compiler warnings | `grep -c "warning C"` over the gate build log = **0**, across `dsp_systems_tests`, `seraphis_tests`, `Seraphis`, `dsp_effects_tests` and `dsp_processors_tests` |
| `tools/pluginval.exe --strictness-level 5 --validate …/Seraphis.vst3` | exit **0**, zero `FAIL` lines (`[PLUGINVAL]`) |
| ODR sweep (roadmap line 594) | No new production class or header; the *New components* table's per-name sweeps stand, and `lint-odr.js` is the machine confirmation |

### Outstanding before the phase can close

**Nothing.** Every ❌ and ⚠️ of the T029 record is closed above, and each closure is a measurement
taken or a bound tightened, never a threshold relaxed, a workload shrunk or a case removed:

| T029 item | How it closed |
|---|---|
| 1. FR-046 / SC-001 clause 1 / SC-010 (a) — probe not run | Probe run under B-1's exact method; four bounds measured and recorded, all **tighter** than the placeholders; both SKIP guards deleted; both comparisons green at exactly 0 deviation on MSVC |
| 2. SC-009 — `kSaturatedDrainFactor` still 30.0 | Frozen at **11.239** from the worst of three clean pinned runs (B-8), a 2.7× tightening, held by two further runs |
| 3. FR-048 — no case's runtime measured | All sixteen measured with `-d yes`; the rule then applied, retiring three estimate-era `[long]` tags (B-5) |
| 4. FR-051 — the ×2 ASan arm produced an empty artifact | Red arm captured under g++ ASan with the write site and allocation named (B-7); green arm recorded on both toolchains |
| 5. SC-012 — the chunk-2 delta rode an `INFO` | Moved to an unconditional `WARN`; **188** is now in three artifacts |
| 6. FR-050 — none of SC-011's three differentials captured | All three measured with the clause compiled out, then the clause restored and the tree re-verified green |
| 7. SC-006 clause 5 — pluginval not run | Run at strictness 5 on the rebuilt bundle: exit 0, zero FAIL lines |
| 8. SC-006 clause 6 anchor 2 — literal token rule not met | **Code changed to meet the count** (FR-025 row): measured 1, in the required `anyPending && …` position |

Two further defects found during this pass and fixed rather than filed:

- **SC-001 clause 2's trigger arm had never been implemented** — a `grep` for `getGrainRngState` over
  the ghost TUs found only the two no-trigger arms and SC-011 (c). It exists now
  (`atmosphere_ghost_test.cpp:1346`) and is green, which is the first executed evidence that a
  triggered birth draws from `grainRng_` exactly as a scheduled one does (FR-027).
- **`check-seraphis-green.js`'s 16-space brace count** had to move 1 → 2 when the FR-025 lambda
  extraction re-aligned the closing brace of `if (activeCount_ > before)`. The reason is recorded at
  the pattern (`:342-348`); the total moved 121 → 122 and every line is still matched by a frozen
  pattern.
