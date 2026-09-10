# Tasks: Vorago Phase 3 — Resonance Drift Network

**Spec:** `specs/vorago-phase3-resonance-drift/spec.md` (1830 lines)
**Plan:** `specs/vorago-phase3-resonance-drift/plan.md` (2017 lines)
**Deliverable:** one new Layer 3 header `dsp/include/krate/dsp/systems/resonance_drift_network.h`,
four new test TUs, one enumerated-list edit in `dsp/lint_all_headers.cpp`, two edits in
`dsp/tests/CMakeLists.txt`, and one bounded set of edits to `spec.md` (plan S14's corrections
C-1/C-6 … C-13). **Conditionally** (FR-013 Tier 1, probe-gated only): one purely additive method
(plus OQ-1's five-line companion) on `dsp/include/krate/dsp/processors/resonator_bank.h` with cases
in the already-registered `dsp/tests/unit/processors/resonator_bank_test.cpp`.
**Test targets:** `dsp_systems_tests` (all four new TUs); `dsp_processors_tests`,
`dsp_primitives_tests`, `dsp_core_tests`, `dsp_effects_tests`, `membrum_tests`, `innexus_tests`,
`seraphis_tests` (the SC-013 regression gate).
**Plugin work:** none. Vorago's plugin starts at Phase 11; phases 1–10 are KrateDSP-only.

---

## How to read this file

* Tasks are grouped into **ordered groups**. A group starts only when every task in the previous
  group is green.
* `[P]` marks tasks that are parallel-safe **within their group**: they create/edit files that are
  fully disjoint from every other task in the same group. **Every task that edits
  `resonance_drift_network.h` is unmarked and sits in its own group** — that header is the single
  shared file of this phase, and so are `dsp/tests/CMakeLists.txt`, `spec.md` and
  `resonator_bank.h`.
* Each task is self-contained: exact files, the failing test to write **first** (file, `TEST_CASE`
  name, the numeric assertions), then the implementation intent, then the verification command. An
  executor needs no other context beyond the spec/plan sections each task cites.
* Canonical order inside every task: **failing test → implement → zero warnings → tests pass.**
* No commit tasks. Commits happen outside this workflow.

**Deviation from the requested layout, called out deliberately.** CMake registration is **T001**,
not a final task. `dsp/tests/CMakeLists.txt`'s `dsp_systems_tests` source list is **enumerated, not
globbed** — verified this session: the list runs from `dsp/tests/CMakeLists.txt:306` (the
`add_executable(dsp_systems_tests` line) to the closing `)` after
`unit/systems/noise_organism_nonfinite_test.cpp`, and it carries its own comment saying so at the
Phase 2 block ("This list is ENUMERATED, not globbed - an unregistered TU silently drops out of the
build and its cases never run"). An unregistered TU compiles into nothing and its cases silently
never run, so registering the four TUs up front is the only ordering under which every later task's
"run the suite" step proves anything. The final group still carries a **registration-completeness
audit** (T021) that re-verifies the four entries plus the `-fno-fast-math` invariant, alongside the
full-suite run (T022) and the portability/lint gate (T023).

### Build and run commands (Windows; the full CMake path is mandatory)

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.perf]" 2>&1 | tail -40
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.calibration]" 2>&1 | tail -40
```

* Catch2 filters: test-case name as a **positional** argument (`"Name*"`), tags in `[brackets]`.
* `[long]` cases run locally by default and are excluded from per-push CI.
* **Timing runs alone**: `node tools/run-cpu-tests.js dsp_systems_tests`, nothing else executing.
* Capture slow runs to a log on the **first** run and read the log; never re-run a suite to grep it.

### Conventions every task inherits

* Namespace `Krate::DSP`; classes PascalCase, members trailing underscore, constants `kPascalCase`.
* Tag every new case `[resonance_drift_network]`, plus `[long]`, `[.perf]` or `[.calibration]`
  where the task says so. `TEST_CASE("ResonanceDriftNetwork_Xxx", "[resonance_drift_network]")`.
* Finiteness is `Krate::DSP::detail::isFinite` / `isNaN` / `isInf` (`core/db_utils.h:99`, `:118`,
  `:260`). **Never** `std::isnan`/`std::isinf`, and never
  `std::numeric_limits<float>::quiet_NaN()`/`infinity()` in a test (macOS CI is `-ffast-math`;
  build non-finite values from bit patterns through a volatile sink).
* No bit-exact float goldens anywhere (`tools/lint-float-bit-goldens.js` must stay green).
* Brace-initialised aggregates use **designated initialisers**
  (`ResonanceDriftNetwork::PrepareConfig{.numPeaks = 8}`) — Clang errors on narrowing where MSVC
  does not.
* Zero compiler warnings is part of every task's definition of done, not a later cleanup.

---

## Group A — Test-target registration (blocking, single shared-file task)

### T001 — Create the four TU stubs and register them (`dsp/tests/CMakeLists.txt`)

**Files to create** (stubs that compile and link, no `TEST_CASE` yet):

* `dsp/tests/unit/systems/resonance_drift_network_test.cpp`
* `dsp/tests/unit/systems/resonance_drift_network_spectral_test.cpp`
* `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp`
* `dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp`

Each stub is exactly:

```cpp
// Vorago Phase 3 (specs/vorago-phase3-resonance-drift): <role of this TU>
#include <catch2/catch_test_macros.hpp>
```

**File to edit:** `dsp/tests/CMakeLists.txt`

1. Append to the `add_executable(dsp_systems_tests ...)` source list (the list starts at `:306`;
   append after `unit/systems/noise_organism_nonfinite_test.cpp`, before the closing `)`):

```cmake
    # Vorago Phase 3 (specs/vorago-phase3-resonance-drift): ResonanceDriftNetwork.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   resonance_drift_network_test.cpp          SC-005, SC-006, SC-007, SC-008,
    #                                             SC-010, SC-011, SC-014, SC-017,
    #                                             SC-018, SC-019, SC-020, SC-021,
    #                                             SC-022 + the clamps/clamp-engages cases
    #   resonance_drift_network_spectral_test.cpp SC-001, SC-002, SC-003 [long],
    #                                             SC-015 [long], SC-016 [long]
    #   resonance_drift_network_perf_test.cpp     SC-004 + the FR-060 stage probe [.perf]
    #   resonance_drift_network_nonfinite_test.cpp SC-009 only
    unit/systems/resonance_drift_network_test.cpp
    unit/systems/resonance_drift_network_spectral_test.cpp
    unit/systems/resonance_drift_network_perf_test.cpp
    unit/systems/resonance_drift_network_nonfinite_test.cpp
```

2. Append **only** the non-finite TU to the `-fno-fast-math -fno-finite-math-only`
   `set_source_files_properties` block (the block ends with the
   `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` line at `:799-800`), after
   `unit/systems/noise_organism_nonfinite_test.cpp`, with this comment:

```cmake
        # Vorago Phase 3: SC-009 injects NaN/Inf via bit patterns in this TU and
        # needs IEEE semantics to assert on them. ONLY this one of the four Phase 3
        # TUs is listed; the other three must NOT be. resonance_drift_network_test.cpp
        # and resonance_drift_network_spectral_test.cpp stay out so the FR-008/FR-009
        # guards are proved in the /fp:fast + -ffast-math mode the header actually
        # ships in. The perf TU must stay out too: -fno-fast-math would change the
        # figures its baselines are pinned to.
        unit/systems/resonance_drift_network_nonfinite_test.cpp
```

**No** change to `dsp/CMakeLists.txt` (the component is header-only) and **no** plugin, CI,
clang-tidy-script or preset change anywhere in this phase.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
```

Builds warning-free; the suite still reports all tests passing (the four stubs add no cases yet).

---

## Group B — CPU measurement gate (blocking; may stop the phase)

### T002 — FR-060 stage-cost probe, written and run **before the component exists**

**File to edit:** `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp`

**Write the case first** — it is the only artefact of this task:
`TEST_CASE("ResonanceDriftNetwork_StageCostProbe", "[resonance_drift_network][.perf]")`.

Measurement basis, copied from `dsp/tests/unit/systems/noise_organism_perf_test.cpp:201-272`
(read it before writing): **ns per 512-sample block at 48 kHz**, best-of-25 trials × 500 blocks
after 400 warm-up blocks. One block period is 10 666 667 ns, so SC-004's 0.75 %/voice ceiling is
**80 000 ns/block**. Report percentages, assert none.

Five stages (plan S10.2), each timed independently and printed as a table via `WARN`:

| # | Stage | Shape to build with shipped components only |
|---|---|---|
| (a) | engine, **both** shapes | (a1) twelve `Krate::DSP::ResonatorBank` instances, each `prepare(48000.0)`, slot 0 only enabled at the FR-016 anchors 40, 55, 75, 103, 141, 193, 265, 363, 497, 681, 933, 1278 Hz, Q = 12, gain −6 dB, each `process(x)` called individually per sample and summed by the harness; (a2) **one** `ResonatorBank` with twelve enabled slots at the same twelve anchors, `processBlock(buf, 512)`. Report both, side by side |
| (b) | lanes | 48 `Krate::DSP::BrownianDrift`, each `prepare(48000.0)`, advanced with `processBlock(64)`; report at `decimation = 1` (advance every chunk) and at `decimation = 17` (advance every 17th chunk); report the 12 "pan" lanes **both** pooled with the other 36 **and isolated on their own** |
| (c) | control writes | the twelve-peak `setFrequency` → `setQ` → `setGain` triple on the (a1) shape, once per 64 samples: with change detection, without change detection, and with FR-015's mandatory Q-after-frequency write |
| (d) | per-sample tail | the FR-044 arithmetic with no banks: 12 `LinearRamp::process()` gates, 12 pan multiply-pairs, one wet-scale `LinearRamp`, one 20 ms `OnePoleSmoother` mix, two `std::clamp` + `detail::flushDenormal` |
| (e) | sleep edge | one `ResonatorBank::reset()` + FR-014-order re-apply + `setEnabled(0, true)`, reported as **ns per edge** |

**Assertions — this is a probe, not a gate.** For every figure: `REQUIRE(detail::isFinite(ns))` and
`REQUIRE(ns > 0.0)`. Nothing else is asserted; a zero or NaN is the one thing that would make the
table lie.

**Then evaluate two gates, in this order (plan S10.5) — do not substitute one number for the
other:**

* **Gate 1 (FR-013 verbatim, spec.md:292-296):** if **stage (a1)'s** figure — twelve single-resonator
  banks — **exceeds 48 000 ns/block**, FR-013 **Tier 1** is taken and Group C/D run before the
  component is written. At **48 000 ns or less, FR-011 stands exactly as written**, nothing in
  FR-013 is exercised, no shipped component is touched, **whatever the total says**, and Groups C
  and D are skipped entirely.
* **Gate 2 (SC-004 budget):** re-project the total using whichever engine figure gate 1 selected
  (engine + lanes at `decimation = 2` + control writes + per-sample tail; the sleep edge amortises
  to ≈ 0 against a 20–90 s event schedule). If the total exceeds **80 000 ns**: escalate to FR-013
  **Tier 2** (the `processSympatheticBankSIMD` kernel) when stage (a) dominates; to FR-038's
  **static-pan fallback** (drop the 12 pan lanes' advance; `setPeakPanWander` becomes an accepting
  no-op) when stage (b)-isolated dominates; otherwise **STOP AND SURFACE to the user with the
  measured table**.

**Forbidden responses to a miss, at every stage of this phase:** lowering `kMaxPeaks`, raising
`kBudgetNs`, relaxing a threshold, shrinking a workload
(`noise_organism_perf_test.cpp:44-58`'s stop-and-surface rule, inherited verbatim).

**Record** the full table, both gate evaluations and the chosen tier in the scratchpad log and, later,
in `compliance.md` (T024).

**Verify (in isolation, nothing else running):**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
node tools/run-cpu-tests.js dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_StageCostProbe" 2>&1 | tee probe.log | tail -40
```

---

## Group C — FR-013 Tier 1 failing test (**conditional on T002 gate 1 only**)

> Skip Groups C and D entirely if T002 stage (a1) measured ≤ 48 000 ns/block. Taking either tier
> without that measurement is a spec violation, not an optimisation.

### T003 — Failing tests for `ResonatorBank::processIndividual` (+ OQ-1's state clear)

**File to edit:** `dsp/tests/unit/processors/resonator_bank_test.cpp` (already registered in
`dsp_processors_tests`; already in the `-fno-fast-math` block — **no CMake change**).

**Write these cases first; they must fail to compile/link before T004:**

`TEST_CASE("ResonatorBank_ProcessIndividual", "[resonator_bank]")`

1. **Sum identity.** Two identically-configured banks at 48 kHz, three slots enabled at 70/140/260 Hz,
   Q = 12, gains −6/−3/0 dB. Drive both with the same 4096-sample white-noise block (fixed seed).
   Bank A calls `process(x)`; bank B calls `processIndividual(x, out.data())` with
   `std::array<float, Krate::DSP::kMaxResonators> out`. `REQUIRE` that
   `std::accumulate(out.begin(), out.end(), 0.0f)` equals bank A's returned sample within
   `Approx(...).margin(1e-5f)` **for every sample** (`exciterMix_` is 0, so `process()`'s mix stage is
   the identity on the wet sum).
2. **Disabled slots write exact zeros.** With only slots 0–2 enabled, `REQUIRE(out[i] == 0.0f)` for
   every `i` in `[3, 16)`, on every sample — never stale data.
3. **Null and un-prepared guards.** `processIndividual(1.0f, nullptr)` is a no-op (the following
   `process()` output is unchanged from a twin bank that never saw the null call — `max|diff| == 0`
   over 512 samples). On a default-constructed (un-prepared) bank, `processIndividual(1.0f,
   out.data())` writes exactly `kMaxResonators` zeros and advances nothing (a subsequent
   `prepare` + render matches a twin that never called it, `max|diff| == 0`).
4. **Smoothers advance exactly once.** Call `processIndividual` 512 times on one bank and `process`
   512 times on a twin, both after `setDamping(0.5f)` (so a smoother is genuinely in flight);
   `REQUIRE` the two banks' summed outputs agree within `Approx(...).margin(1e-5f)` at sample 512 —
   a build that advanced the three global smoothers twice per call diverges here.

`TEST_CASE("ResonatorBank_ResetResonatorState", "[resonator_bank]")` (OQ-1's companion; the user ruled
option (i) on 2026-09-10 — see plan S17 OQ-1 — so this case is unconditional under Tier 1)

5. **State clear, configuration preserved.** Bank at 48 kHz, slot 0 at 40 Hz, `setQ(0, 100.0f)`
   (`RT60 = Q·ln1000/(π·f) ≈ 5.5 s`), driven to steady state by a 40 Hz sine for 1 s, then silence.
   Without the clear, the ring is still above `1.0e-3` after 100 ms. Call `resetResonatorState(0)`;
   `REQUIRE` the next 64 output samples are all `< 1.0e-6` in magnitude, **and** that
   `getFrequency(0) == Approx(40.0f)`, `getQ(0) == Approx(100.0f)` and `isEnabled(0) == true`
   afterwards (it is a state clear, **not** `reset()`'s configuration wipe).
6. **Out-of-range index is a silent no-op:** `resetResonatorState(kMaxResonators)` leaves a
   subsequent 512-sample render bit-identical to a twin that never called it.

**Verify:** the new cases fail (the methods do not exist yet).

---

## Group D — FR-013 Tier 1 implementation (**conditional**)

### T004 — Add `processIndividual` (+ `resetResonatorState`) to `ResonatorBank`, purely additively

**File to edit:** `dsp/include/krate/dsp/processors/resonator_bank.h`

**Mandatory conditions (FR-013):** every existing method's **body, signature and semantics stay
byte-for-byte unchanged**. The new render loop **duplicates** `process()`'s body
(`resonator_bank.h:470-517`) rather than extracting a shared helper out of it — extracting is the
instinctive move and it violates the condition. Carry a comment saying exactly that.

Implement, mirroring `process()` term for term:

```cpp
/// @brief Process one sample, writing each resonator's INDIVIDUAL contribution.
///
/// Additive companion to process() (:470), added for Vorago Phase 3 FR-013 Tier 1:
/// a caller that needs a per-resonator per-sample gate cannot get it from
/// process(), which returns only the summed wet output.
///
/// Writes exactly kMaxResonators floats; a disabled slot writes 0.0f. The
/// exciter-mix stage (:514) is deliberately NOT applied. Advances the three
/// global smoothers exactly once, exactly as process() does - a caller must use
/// EITHER process() OR processIndividual() for a given sample, never both.
///
/// The loop below intentionally DUPLICATES process()'s body instead of sharing a
/// helper with it: FR-013 requires every pre-existing method to stay
/// byte-for-byte unchanged, so process() is not refactored.
void processIndividual(float input, float* outPerResonator) noexcept;
```

Per slot: `enabled_[i] ? filters_[i].process(excitation) * dampingScale * gains_[i] *
calculateTiltGain(frequencies_[i], currentTilt) : 0.0f`. Null `outPerResonator` ⇒ no-op;
`!prepared_` ⇒ write `kMaxResonators` zeros and return without advancing.

`resetResonatorState(std::size_t index)` is five lines:
`if (index < kMaxResonators) filters_[index].reset();` — nothing else.

**Regression gate (SC-013), mandatory and non-negotiable:** run the consumer suites **before and
after** this edit and diff the results. `ResonatorBank` has real consumers — every Membrum body
(`plugins/membrum/src/dsp/bodies/*.h`, `body_bank.h`, `drum_voice.h`, `tone_shaper.h`,
`voice_pool.cpp`), Innexus (`physical_model_mixer.h`, `processor/innexus_voice.h`), plus
`ContinuousBody`, `NoiseOrganism`, `iresonator.h` and `modal_resonator_bank_simd.h`. **Any moved
result is a regression to investigate and surface, never a golden to update.**

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests \
  dsp_systems_tests dsp_effects_tests membrum_tests innexus_tests seraphis_tests
for t in dsp_processors_tests dsp_systems_tests dsp_effects_tests membrum_tests innexus_tests seraphis_tests; do \
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done
git diff --stat dsp/include/krate/dsp/processors/resonator_bank.h
```

All green with **no edits to existing cases**; the diff shows additions only.

---

## Group E — Spec write-back (single shared-file task)

### T005 — Write plan S14's corrections into `spec.md`

**File to edit:** `specs/vorago-phase3-resonance-drift/spec.md` — **and nothing else.**

This step exists so T024's compliance table cannot carry a row whose stated requirement contradicts
the shipped assertion. Apply exactly these, each already derived and justified in plan S14:

| # | Spec location | Edit |
|---|---|---|
| C-1 / C-7 | FR-062 (`spec.md:846-856`) and SC-011 (`:1139-1148`) | The declared prepare-time heap footprint is **zero bytes** and `prepare` performs **zero** allocations; `getAllocatedBytes()` returns `0`. `PrepareConfig::maxBlockSamples` is retained, clamped `[64, 8192]`, reported, and documented as **sizing nothing** in this component. SC-011's expected values are re-derived from that declaration |
| C-6 | FR-017 (`:402-406`), FR-044 step 1 (`:714-729`), FR-035 (`:554-561`) | FR-017 gains the per-sample **base-level `LinearRamp`** at `kGainRampMs` (the `noise_organism.h:1189` `Slot::levelRamp` shape); FR-044 step 1 gains the `levelGain` factor and states that the bank is written only `appliedGainDb − levelDb`; FR-035's citation of "the FR-017 base-level ramp" becomes true |
| C-8 | FR-035's exemption clause, FR-052's dormant wording | FR-035: the exemption becomes "**the FR-042 dormant interval, of which the wake-edge snap is the audible boundary**". FR-052: "exactly as an awake peak" becomes "as an awake peak does, **except that the FR-035 ceiling does not apply while the gate sits at exactly zero**" |
| C-9 | Edge Cases' sample-rate floor (`spec.md:1502`) | The floor is **`kMinUsableSampleRate = 8000.0 Hz`**, not 1 Hz, because at 1 Hz the pair `[kMinResonatorFrequency = 20, 0.45·fs = 0.45]` is **inverted** and `std::clamp` with `hi < lo` is undefined behaviour (MSVC fires `_STL_VERIFY("invalid bounds argument passed to std::clamp")`), including inside the shipped `ResonatorBank::clampFrequency` (`resonator_bank.h:542-545`) |
| C-10 | SC-001 (c) | Restated as a **derived relationship**: dormant peaks 4–11 in **both** arms so the acoustic content is identical, then require the level difference between `numPeaks = 4` and `numPeaks = 12` to equal `20·log10(sqrt(12/4)) = **4.77 dB ± 0.5 dB**`. This becomes the only assertion of FR-019's "`N` is `numPeaks`, not the awake count" |
| C-11 | SC-002's partition sentence | The boundary population is `n mod 64 ∈ **{0, 1, 2}**`, interior is the other 61/64 |
| C-12 | SC-002 (c)'s injection sentence | The injection is **systematic**: with `setSlewCeilings(24, 24)`, an alternating ±1-octave `setPeakAnchorHz` jump on **every** control chunk for the full pinned 60 s render |
| C-13 | SC-020 (b) | The wet path is isolated **directly**: compare a `mix = 1` render against a `mix = 0` render and require `\|RMS_wet_dB − RMS_dry_dB\| <= 6 dB`; the `mix = 0.5` render is kept only as a monotonicity check |

**Verify:** `git diff specs/vorago-phase3-resonance-drift/spec.md` shows **only** the C-numbered
clauses above; each edited FR/SC reads consistently with the assertion the later task writes for it.

---

## Group F — Component skeleton

### T006 — `resonance_drift_network.h` skeleton, constants, contract, lint registration

**Files to create:** `dsp/include/krate/dsp/systems/resonance_drift_network.h`
**Files to edit:** `dsp/lint_all_headers.cpp`, `dsp/tests/unit/systems/resonance_drift_network_test.cpp`

**ODR pre-check (mandatory, run and record):**
`grep -rn "class ResonanceDriftNetwork\|struct ResonanceDriftNetwork" dsp/ plugins/ tools/` must
return 0 hits (it did this session). Near-name hazards that **do** exist and must not be shadowed:
`ResonatorBank` (`processors/resonator_bank.h:174`), `ModalResonatorBank`
(`processors/modal_resonator_bank.h:71`), `SympatheticResonance` (`systems/sympathetic_resonance.h:96`),
and `TuningMode` (`resonator_bank.h:133`) — which is why the mode enum is nested and named
`AnchorMode`, never `TuningMode`.

**Failing test first** —
`TEST_CASE("ResonanceDriftNetwork_ControlSurfaceClamps", "[resonance_drift_network]")`, six arms:

* (i) `setNumPeaks(0)` ⇒ `getNumPeaks() == 1`; `setNumPeaks(99)` ⇒ `12`.
* (ii) every clamped setter driven past both ends and read back: `setPeakQ(i, 400)` ⇒ `100`,
  `setPeakQ(i, 0)` ⇒ `0.1`, `setPeakLevel(i, 99)` ⇒ `12`, `setPeakLevel(i, -99)` ⇒ `-60`,
  `setWanderRate(5)` ⇒ `1.0`, `setWanderRate(0)` ⇒ `0.002`, `setSlewCeilings(99, 0)` ⇒ `(24, 0.001)`,
  `setGravity(9)` ⇒ `1`, `setGravity(-9)` ⇒ `-1`, `setPeakRatio(i, 0)` ⇒ `0.25`,
  `setPeakRatio(i, 999)` ⇒ `64`, `setNoteFrequency(2)` ⇒ `8`, `setWetGain(99)` ⇒ `48`,
  `setWetGain(-99)` ⇒ `-24`, `setPeakPan(i, 9)` ⇒ `1`, `setPeakPan(i, -9)` ⇒ `-1`,
  `setFreqWander(i, 99)` ⇒ `24`, `setQWander(i, 99)` ⇒ `2`, `setGainWander(i, 99)` ⇒ `24`,
  **`setPeakWake(i, 9)` ⇒ `getPeakWakeAmount(i) == 1.0f`**, **`setPeakWake(i, -1)` ⇒ `0.0f`**,
  **`setPeakPanWander(i, 9)` ⇒ `1`**, **`setPeakPanWander(i, -1)` ⇒ `0`**. The last four are
  load-bearing, not cosmetic: an unclamped wake would scale one peak 50 % past unity into the wet
  trim or invert its polarity against the other eleven, and no other criterion looks.
* (iii) `getLaneDecimation()` returns **1** at `setWanderRate(1.0f)`, **2** at `0.03f`, **7** at
  `0.005f`, **17** at `0.002f` — the FR-037 mapping asserted directly.
* (iv) every setter called with `peak == kMaxPeaks` (12) is a **silent no-op** (no getter anywhere
  changes) and every getter with an out-of-range index returns its documented neutral: `0.0f` for
  floats, `0` for sizes, `false` for bools, `AnchorMode::Free` for the mode.
* (v) **the C-9 sample-rate ordering, asserted rather than assumed:** `prepare(0.0, cfg)` and
  `prepare(1.0, cfg)` (both floored to `kMinUsableSampleRate = 8000.0`), then
  `setPeakAnchorHz(0, 10.0f)`, `setPeakAnchorHz(1, 1.0e6f)` and one `processBlock` of 64 samples on
  each: no crash, no MSVC `_STL_VERIFY` abort from an inverted `std::clamp`, every output sample
  `detail::isFinite`, and `getPeakCurrentFrequency(i)` inside
  `[kMinResonatorFrequency, kMaxResonatorFrequencyRatio * kMinUsableSampleRate] = [20, 3600]`.
* (vi) **the log2-Q constants pinned to the shipped Q bounds** (the `entropy_processor.h:82`
  runtime-equivalence idiom, because `std::log2` is not `constexpr` in C++20 on Clang):
  `REQUIRE(ResonanceDriftNetwork::kMinLog2Q == Approx(std::log2(kMinResonatorQ)).margin(1e-5))`
  and the same for `kMaxLog2Q`/`kMaxResonatorQ`.

Plus `TEST_CASE("ResonanceDriftNetwork_PrepareFootprint", "[resonance_drift_network]")` (SC-011,
re-derived by C-1): `getAllocatedBytes() == 0` and **0** allocations recorded by an
`Krate::Test::AllocationScope` (`tests/test_helpers/allocation_detector.h:111`) around `prepare`, at
`maxBlockSamples` = 64, 2048 **and** 8192 — the relationship, not one value.

Plus the guard-ladder arms of
`TEST_CASE("ResonanceDriftNetwork_RenderPathBoundaries", "[resonance_drift_network]")`:
(b) **each** of the four pointers null independently — render a reference block, call with one null,
render again, `max|diff| == 0` against the reference continuation on **both** channels; (c)
`numSamples == 0` is a no-op consuming no control step, repeated 1000× so a one-sample drift would
show; (d) `processBlock` **before `prepare`** writes exactly `numSamples` zeros to both channels and
advances nothing.

**Implement** (plan S1.1–S1.5, S2.1–S2.4):

* Banner `// Layer: 3 (Systems)` matching `systems/timevar_comb_bank.h:8`; header-only;
  `namespace Krate::DSP`. Includes reach **downward only**: `core/db_utils.h`,
  `core/math_constants.h`, `core/random.h`, `primitives/smoother.h`,
  `processors/brownian_drift.h`, `processors/resonator_bank.h`, plus `<algorithm> <array> <cmath>
  <cstddef> <cstdint>`. **No `<vector>`, no `<memory>`** — there is no heap term (C-1).
* Constants exactly as plan S1.2: `kMaxPeaks = 12`, `kControlChunkSamples = 64`,
  `kMaxLaneDecimation = 17`, `kGainRampMs = 50.0f`, `kOutputClamp = 4.0f`,
  `kMixSmoothMs = kResonatorSmoothingTimeMs` (20 ms), `kDefaultWanderRateHz = 0.03f`,
  `kMinWanderRateHz = 0.002f`, `kMaxWanderRateHz = 1.0f`, `kDefaultFreqStepOctaves = 0.02f`,
  `kDefaultQStepOctaves = 0.05f`, `kMinSlewOctaves = 0.001f`, `kMaxSlewOctaves = 24.0f`,
  `kDefaultWetGainDb = 30.0f` (**provisional — T019 measures it**), `kMinWetGainDb = -24.0f`,
  `kMaxWetGainDb = 48.0f`, `kMinPeakLevelDb = -60.0f`, `kMaxPeakLevelDb = 12.0f`,
  `kMaxFreqWanderSemis = 24.0f`, `kMaxQWanderOctaves = 2.0f`, `kMaxGainWanderDb = 24.0f`,
  `kMinRatio = 0.25f`, `kMaxRatio = 64.0f`, `kMinNoteHz = 8.0f`, `kWakeSilenceEpsilon = 1.0e-6f`,
  `kMinUsableSampleRate = 8000.0`, and `kMinLog2Q`/`kMaxLog2Q` built from
  `detail::constexprLn(...) / detail::kLn2` (**never `std::log2`, which is not `constexpr` on
  Clang**). Carry every `static_assert` plan S1.2 lists, including
  `kMaxLaneDecimation * BrownianDrift::kTauMax >= 500.0f` and the `kMinUsableSampleRate` ordering
  assert.
* Nested `enum class AnchorMode : std::uint8_t { Free = 0, Keyed = 1, Hybrid = 2 }` (**append-only**),
  nested `struct PrepareConfig { std::size_t maxBlockSamples = 2048; std::size_t numPeaks = kMaxPeaks; }`,
  nested `struct Peak` (plan S1.5's field list verbatim).
* The complete public surface of plan S1.3, with real bodies for every setter/getter and the S1.4
  contract enforced: out-of-range `peak` ⇒ silent no-op / documented neutral; **non-finite float
  argument ⇒ no-op, previous value stands** (`if (!detail::isFinite(v)) return;` as the first line
  of every float setter — load-bearing, because `std::clamp` does **not** reject NaN and a NaN
  anchor propagates into `sin`/`cos` coefficients that `Biquad::process` never recovers from);
  out-of-range float ⇒ clamped into FR-016's range and the getter reports the clamped value.
* Salt table with the five `static_assert` overflow guards (plan S2.4): `kSaltFreqLane = 0`,
  `kSaltQLane = 16`, `kSaltGainLane = 32`, `kSaltPanLane = 48`, `kSaltPanPosition = 64`,
  `kSaltNextFree = 80`, **append-only**.
* `prepare` in plan S2.1's exact 13-step order (floor the rate at `kMinUsableSampleRate`; clamp the
  config; cache `minLog2Hz_`/`maxHz_`/`maxLog2Hz_`; `prepare` the twelve banks and pin
  `setDamping(0)`, `setExciterMix(0)`, `setSpectralTilt(0)`; prepare the 48 lanes; `applyDefaults()`;
  `prepared_ = true`; `setWanderRate(kDefaultWanderRateHz)`; configure/snap the mix smoother and the
  wet-scale ramp; configure/snap every gate and level ramp **and seed `lastGateTarget`**; zero the
  counters; `setSeed(seed_)` **last**; `snapControlState()`).
* `processBlock`'s guard ladder and **absolute** 64-sample control grid with `controlPhase_` carried
  **across calls** (plan S5.1) — deliberately not `HarmonicCloud`'s block-relative chunking, because
  a 36 + 28 split must run **one** control step, not two.
* `updateControl()` and `renderChunk()` may be silent at this task (no engine yet); everything above
  must be real.
* `applyDefaults()` writes FR-016's table: anchors `{40, 55, 75, 103, 141, 193, 265, 363, 497, 681,
  933, 1278}` Hz and ratios `{0.5, 1.0, 1.5, 2.0, 2.98, 4.0, 5.04, 6.0, 7.02, 8.0, 9.98, 12.0}`
  **verbatim, never regenerated**; `levelDb = -6`, `baseQ = 12`, `freqWanderSemis = 3`,
  `qWanderOct = 0.5`, `gainWanderDb = 6`, `panWanderDepth = 0.2`, `wakeAmount = 1`,
  `dormant = false`; network-wide `anchorMode_ = Free`, `noteHz_ = 55`, `gravity_ = 0`,
  `wanderEnabled_ = true`, `freqSlewOct_ = 0.02`, `qSlewOct_ = 0.05`, `mix_ = 1`,
  `wetGainDb_ = kDefaultWetGainDb`. `panPosition` is **not** written here — the seeded draw owns it.

**Also edit `dsp/lint_all_headers.cpp`:** add, after the Vorago Phase 2 line
`#include <krate/dsp/systems/noise_organism.h>` in the Layer 3 block,

```cpp
// Vorago Phase 3 (specs/vorago-phase3-resonance-drift), FR-001
#include <krate/dsp/systems/resonance_drift_network.h>
```

That file is an **enumerated** list and picks nothing up automatically; without the line the new
header gets zero strict-tidy coverage and SC-012's clang-tidy gate passes vacuously for it.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_*" 2>&1 | tail -5
node tools/lint-layers.js && node tools/lint-odr.js
```

Warning-free build; the clamps case (all six arms), `PrepareFootprint`, and
`RenderPathBoundaries` (b)(c)(d) pass; both lints exit 0.

---

## Group G — Anchors, modes, slew limiter

### T007 — `recomputeAnchors()`, the three modes, and the FR-035 slew ceiling

**File to edit:** `dsp/include/krate/dsp/systems/resonance_drift_network.h`
**Test file:** `dsp/tests/unit/systems/resonance_drift_network_test.cpp`

**Failing tests first.**

`TEST_CASE("ResonanceDriftNetwork_AnchorModes", "[resonance_drift_network]")` — FR-016 defaults,
48 kHz, `setWanderEnabled(false)`. **Settling clause applied to every arm: render ≥ 100 control
chunks (6 400 samples) after the last setter and before the first read**, because FR-035 caps a
one-octave move at 50 control steps.

* (a) `AnchorMode::Free`: `getPeakCurrentFrequency(i)` within **1 cent** of the FR-016 anchor, all 12.
* (b) `AnchorMode::Keyed` at `noteHz = 55`: within 1 cent of `55 × ratio[i]`, all 12. Then an
  **upward** `setNoteFrequency(110.0f)` moves every peak by **1200 ± 2 cents**. The downward arm is
  an explicit clamp check instead: at `setNoteFrequency(27.5f)`, peak 0 (`27.5 × 0.5 = 13.75 Hz`)
  clamps to **exactly 20 Hz** — name the clamped peaks and assert they sit at 20 Hz.
* (c) `Hybrid` at `setGravity(0.0f)` is **bit-equal** to Free: `getPeakCurrentFrequency(i)` compares
  `==` against the Free-mode value for all 12 (this passes only with an explicit short-circuit
  branch, not a log/exp round trip that happens to be exact).
* (d) `Hybrid` at `setGravity(1.0f)`: every peak within 1 cent of **its own** `noteHz × ratio[i]`,
  **and** the twelve results **pairwise distinct to 1 cent** (the assertion that catches a
  nearest-neighbour collapse). Also `g = 0.5` matches the closed form
  `log2 f = log2 f_free + g·(log2 f_keyed − log2 f_free)` within 1 cent.
* (e) `g = -1` within 1 cent of `exp2(2·log2 f_free − log2 f_keyed)`.
* (f) sweeping `g` from −1 to +1 in 0.05 steps **with settling at every step** moves each peak's
  `log2 f` monotonically — no reversal — and no single control step exceeds `freqSlewCeiling`.

`TEST_CASE("ResonanceDriftNetwork_SlewLimit", "[resonance_drift_network]")` — `Keyed`, defaults,
wander off, `freqSlewCeiling = 0.02`:

* (a) sampling `getPeakCurrentFrequency(i)` once per control step across an upward
  `setNoteFrequency(55 → 110)`, the per-step change in `log2 f` **never exceeds 0.02**, for any
  peak, at any step.
* (b) each peak reaches the new target within **50 ± 2 control steps** (`1.0 / 0.02`) — this fails
  if the limiter is absent (1 step), slower, or applied in the wrong domain.
* (d) with `setSlewCeilings(0.005f, 0.005f)` the same move takes **200 ± 5** control steps, proving
  the ceiling is a real control surface and not a constant.

(SC-018 (c), the `B/P` arm across the transition, belongs to T016 — it needs the same pinned 60 s
render machinery as SC-002 (a).)

**Implement** (plan S4, S6.5):

* `recomputeAnchors()` in the **log2 domain**, run on the control grid **only when `anchorsDirty_`**
  (set by `setAnchorMode`, `setPeakAnchorHz`, `setPeakRatio`, `setNoteFrequency`, `setGravity`):
  Free ⇒ `p.freeLog2Hz`; Keyed ⇒ `noteLog2 + p.ratioLog2`; Hybrid ⇒ an **explicit
  `if (gravity_ == 0.0f) log2Hz = p.freeLog2Hz;`** short-circuit else
  `p.freeLog2Hz + gravity_ * (keyedLog2 - p.freeLog2Hz)`. Finish with
  `p.anchorLog2Hz = std::clamp(log2Hz, minLog2Hz_, maxLog2Hz_)`.
* Cache `freeLog2Hz` in `setPeakAnchorHz` (sanitise → clamp to `[kMinResonatorFrequency, maxHz_]` →
  `std::log2`) and `ratioLog2` in `setPeakRatio`. Anchor clamping is **silent** — no engagement
  counter — because it is a legitimate configuration outcome at low sample rates (FR-025).
* Slew limiting on the applied values, in log2, per control step:
  `appliedLog2Hz = clamp(targetLog2Hz, appliedLog2Hz ∓ freqSlewOct_)` and the same for Q with
  `qSlewOct_`; then `appliedHz = std::exp2(appliedLog2Hz)`, `appliedQ = std::exp2(appliedLog2Q)`.
  **While `!engineActive` the applied values track target unslewed** (plan S6.5, correction C-8).
* `setSlewCeilings(float, float)` clamps each to `[kMinSlewOctaves, kMaxSlewOctaves] = [0.001, 24]`.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_AnchorModes" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_SlewLimit" 2>&1 | tail -5
```

---

## Group H — Wander lanes

### T008 — `setWanderRate` mapping, FR-037 decimation, the four lane maps

**File to edit:** `dsp/include/krate/dsp/systems/resonance_drift_network.h`
**Test file:** `dsp/tests/unit/systems/resonance_drift_network_test.cpp`

**Failing test first** —
`TEST_CASE("ResonanceDriftNetwork_LaneBounds", "[resonance_drift_network]")`:

* (a) **FR-030/FR-031 bounds, ≥ 10⁵ samplings.** 12 peaks, `setFreqWander(i, 24.0f)`,
  `setQWander(i, 2.0f)`, `setPeakQ(i, 12.0f)`, `setWanderRate(1.0f)`, wander on. Render 10 s at
  48 kHz sampling `getPeakCurrentFrequency(i)` and `getPeakCurrentQ(i)` **once per control step**
  (7 500 steps × 12 peaks = 90 000; extend to 12 s for ≥ 10⁵): **zero** excursions outside
  `[anchor·2^(-24/12), anchor·2^(+24/12)] ∩ [20, 0.45·fs]` and
  `[12·2^-2, 12·2^+2] = [3, 48] ∩ [0.1, 100]`.
* (b) **per-peak lane persistence:** each peak's `log2(getPeakCurrentFrequency(i))` trajectory,
  mean-removed, has lag-`T/8` normalised autocorrelation **≥ 0.20** and lag-`8T` **≤ 0.10**, where
  `T = 1/wanderRate`.
* (c) **the decimation mapping asserted directly** (also arm (iii) of the clamps case, repeated here
  against the realised behaviour): `getLaneDecimation()` is 1 / 2 / 7 / 17 at wander rates
  1.0 / 0.03 / 0.005 / 0.002 Hz, and a rate change mid-render neither forces an extra lane advance
  nor skips one (assert `getPeakCurrentFrequency(i)` moves by less than `freqSlewCeiling` across the
  control step that follows the rate change).
* (d) **`setWanderEnabled(false)` zeroes the depths without rewinding the lanes** (plan S6.1, C-3):
  with wander off, `getPeakCurrentFrequency(i)` equals the anchor **exactly** to within 1 cent for
  every peak; re-enabling does not jump — the first control step after re-enable moves each peak by
  less than `freqSlewCeiling`.

**Implement** (plan S6.1–S6.4):

* `setWanderRate(float hz)`: sanitise; clamp `[0.002, 1.0]`; `requestedTau = 1/rate` ∈ `[1, 500]` s;
  `newDecimation = clamp(ceil(requestedTau / BrownianDrift::kTauMax), 1, kMaxLaneDecimation)`;
  `tau = requestedTau / newDecimation` (lands in `[0.2, 30]`);
  `smoothness = clamp((tau - kTauMin) / (kTauMax - kTauMin), 0, 1)`; push `setSmoothness` into all
  48 lanes; **rebase** `laneCounter_ = laneCounter_ % newDecimation` then assign `laneDecimation_`.
  Worked values to check against: 1.0 Hz → dec 1, τ 1.0 s, s 0.0268; 0.0333 Hz → dec 1, τ 30 s,
  s 1.0; **0.03 Hz → dec 2, τ 16.67 s, s 0.552**; 0.005 Hz → dec 7, τ 28.57 s, s 0.952;
  0.002 Hz → dec 17, τ 29.41 s, s 0.980. **Without the decimation the whole sub-range
  `[0.002, 0.0333]` Hz — including this component's own 0.03 Hz default — is a dead zone.**
  `setWanderRate` is the **single owner** of `laneDecimation_` and of every lane's `setSmoothness`.
* Lane advance at the top of `updateControl()`, **before anything reads a lane**: when
  `laneCounter_ == 0`, call `processBlock(kControlChunkSamples)` on all four lanes of **every** peak
  — dormant peaks and zero-depth lanes included — then
  `laneCounter_ = (laneCounter_ + 1) % laneDecimation_`.
* `laneValue(lane) = std::clamp(sanitise(lane.getCurrentValue(), 0.0f), -1.0f, 1.0f)`, read through
  the **concrete** `BrownianDrift` type, never through a `ModulationSource&` (no virtual dispatch on
  the control path).
* `wanderScale() = wanderEnabled_ ? 1.0f : 0.0f`, multiplying every **depth**
  (`noise_organism.h:2200-2202` shape).
* The four maps for **every** peak, dormant included (plan S6.4), all in log2:
  `targetLog2Hz = clamp(anchorLog2Hz + ws·freqWanderSemis·lane/12, minLog2Hz_, maxLog2Hz_)`;
  `targetLog2Q = clamp(baseLog2Q + ws·qWanderOct·lane, kMinLog2Q, kMaxLog2Q)`;
  `targetGainDb = clamp(levelDb + ws·gainWanderDb·lane, -60, +12)`;
  `targetPan = clamp(panPosition + ws·panWanderDepth·lane, -1, +1)`.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_LaneBounds" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_ControlSurfaceClamps" 2>&1 | tail -5
```

---

## Group I — Engine wiring and the render tail

### T009 — The S9.1 bank seam, the FR-014/FR-015 write path, the FR-044 render tail

**File to edit:** `dsp/include/krate/dsp/systems/resonance_drift_network.h`
**Test file:** `dsp/tests/unit/systems/resonance_drift_network_test.cpp`

**Failing tests first.**

`TEST_CASE("ResonanceDriftNetwork_BlockSizeInvariance", "[resonance_drift_network]")` (SC-010): the
same 5 s total render, driven identically on both channels, produced by two **fresh identically
seeded** instances — one in 512-sample blocks, one in the irregular partition
`{1, 63, 64, 65, 200, 512, 1024, 3, 127, ...}` cycled to the same total — agree with
`max|diff| <= kSampleTolerance = 5.0e-4f` (`tests/test_helpers/render_fingerprint.h:58`) on **both**
channels. Same binary, same process, **no stored golden**.

Add to `ResonanceDriftNetwork_RenderPathBoundaries`:

* (a) **in-place equality:** `inL == outL, inR == outR` equals the out-of-place render from the same
  state with `max|diff| == 0` on both channels; the cross-aliased case `inL == outR, inR == outL`
  gives the same guarantee.
* (e) `numSamples = 65 536` with `PrepareConfig{.maxBlockSamples = 64}` renders correctly (every
  sample finite, RMS within 0.5 dB of the same content rendered in 512-blocks) and performs **0**
  allocations inside an `AllocationScope`.

And `TEST_CASE("ResonanceDriftNetwork_RendersNonSilent", "[resonance_drift_network]")`: the FR-016
default patch at `mix = 1`, driven by white noise at −12 dBFS on both channels for 2 s, has RMS
**> −60 dBFS** on both channels and every sample finite.

**Implement** (plan S5.2, S5.3, S8, S9.1, S9.2):

* Six private one-line seam inlines — `bankProcess`, `bankSetFrequency`, `bankSetQ`, `bankSetGain`,
  `bankSetEnabled`, `bankReset` — forwarding under **Tier 0** to `banks_[i]` slot 0, or under
  **Tier 1** (only if T002 said so) to one shared `bank_` slot `i`. **`bankReset(i)` is a pure state
  clear and nothing else**; the caller owns the FR-014-order re-apply and the `setEnabled` write.
* **Control-step writes (the silent-failure site):** frequency and Q are **one indivisible write
  pair** whose change detection is the **OR** of the two comparisons:

  ```cpp
  const bool freqOrQMoved = (p.appliedHz != p.lastWrittenHz) || (p.appliedQ != p.lastWrittenQ);
  if (freqOrQMoved) { bankSetFrequency(i, p.appliedHz); bankSetQ(i, p.appliedQ); /* store both */ }
  if (p.appliedBankGainDb != p.lastWrittenBankGainDb) { bankSetGain(i, p.appliedBankGainDb); /* store */ }
  ```

  **Why:** `ResonatorBank::setFrequency` re-derives `qValues_[index] = rt60ToQ(frequencies_[index],
  decays_[index])` (`resonator_bank.h:333`) off a decay table this network **never writes**, so
  `decays_[i]` stands at `kDefaultDecayTime = 1.0 s` forever. A frequency write after a Q write
  silently discards the Q; skipping an "unchanged" Q write after a frequency write leaves the filter
  at `rt60ToQ(f, 1.0) = 0.4548·f` — Q ≈ 18 at 40 Hz instead of the configured 12, clamped to 100
  above ≈ 220 Hz — permanently. **`ResonatorBank::setDecay` is NEVER called by this component.**
* `renderChunk` exactly as plan S5.3: per sample, capture **both** dry samples first
  (`dryL`/`dryR`, non-finite replaced by `0.0f` per channel independently), `x = 0.5·(dryL + dryR)`;
  per peak advance the gate and level ramps **for every peak, dormant and out-of-count included**,
  early-out on `!engineActive`, else `y = bankProcess(i, x) · gate · levelGain` and accumulate
  `wetL += y·panGainL`, `wetR += y·panGainR`; one `wetScaleRamp_.process()` scalar for both channels;
  `m = mixSmoother_.process()`; `out = (1-m)·dry + m·(wet·scale)`; then the FR-018 clamp and
  `detail::flushDenormal` per channel.
* `wetScaleRamp_`'s target is `(1.0f / std::sqrt(float(config_.numPeaks))) * dbToGain(wetGainDb_)` —
  FR-019's normalisation and FR-045's trim folded into **one** `LinearRamp` at `kGainRampMs`
  (correction C-4). **`N` is `numPeaks`, never the awake count** — an "awake" reading computes
  `1/sqrt(0) = inf` and `inf × 0.0f = NaN` when every peak sleeps.
* `setNumPeaks(n)` clamps `[1, kMaxPeaks]`, retargets the wet-scale ramp, and calls `refreshGates()`
  so dropped peaks are silenced through the ramp, never abruptly; it never reallocates.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_BlockSizeInvariance" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_RenderPathBoundaries" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_RendersNonSilent" 2>&1 | tail -5
```

---

## Group J — Life cycle: gates, dormancy, the base-level ramp

### T010 — `refreshGates`, `lastGateTarget`, the sleep/wake edges, `Peak::levelRamp`

**File to edit:** `dsp/include/krate/dsp/systems/resonance_drift_network.h`
**Test files:** `dsp/tests/unit/systems/resonance_drift_network_test.cpp` (SC-002 (d) arms),
`dsp/tests/unit/systems/resonance_drift_network_spectral_test.cpp` (SC-015 (a)(c)(d)(e)(h))

**Failing tests first.**

`TEST_CASE("ResonanceDriftNetwork_GateRamp", "[resonance_drift_network]")` — SC-002 (d) and its
three extra arms. `fs = 48 000`, so the bound `1.05 / (kGainRampMs · 0.001 · fs) = 4.375e-4` and the
correct worst-case step is `1.0 / 2400 = 4.1667e-4`.

* (d) **primary, on `getPeakGate(i)` sampled at `numSamples == 1` granularity:** a `setPeakWake(i, 0
  → 1)` ramp reaches 1.0 in **50 ms ± 5 ms**, is **monotone non-decreasing**, and no per-sample step
  exceeds **4.375e-4**. Secondary audio arm: the per-cycle peak magnitude over
  `round(fs / f_peak)`-sample windows reaches 90 % within 50 ms ± 5 ms — **no per-sample bound is
  asserted on audio**.
* (d-ii) **`refreshGates` re-target immunity.** Repeat the primary measurement while calling
  `setPeakWake` on a **different** peak once per 512-sample block for the whole ramp, exactly as
  FR-040's Phase-10 scheduler will. The measured peak must still reach 1.0 in **50 ms ± 5 ms**.
  **Run this arm red-first**: against a build whose `refreshGates()` re-targets every peak
  unconditionally it reads ≈ 107 ms to 90 % and ≈ 213 ms to 99 % — record both numbers — then green
  with the `lastGateTarget` guard. Every other assertion passes on the broken build, because
  re-targeting only *lowers* the per-sample step.
* (d-iii) **the C-6 base-level ramp.** A mid-render `setPeakLevel(i, -60 → +12)` on a steady sine at
  that peak's centre frequency: the per-cycle envelope rises over **50 ms ± 5 ms** and no per-sample
  step in that envelope exceeds `1.05 · dbToGain(12.0f) / (kGainRampMs · 0.001 · fs)`. A build with
  no `Peak::levelRamp` steps 72 dB in one sample and is red here.
* (d-iv) **FR-043's mix smoother.** Precondition, `REQUIRE`d before the arm runs: a 2 kHz sine sits
  outside every default peak's passband — the wet RMS at `mix = 1` is **≥ 40 dB below** the dry RMS.
  Render at `mix = 0`, then `setMix(1.0f)` mid-render: the per-cycle envelope (which tracks
  `1 − m`) falls to **1 % of its initial value in 20 ms ± 5 ms** (`OnePoleSmoother::configure`'s
  documented "time to reach 99 % of target", `smoother.h:158-159`) and is monotone. A build applying
  `setMix` instantaneously reads 0 ms and is red.

`TEST_CASE("ResonanceDriftNetwork_PeakLifeCycle", "[resonance_drift_network][long]")` — the
non-`[long]`-dependent arms in this task:

* (a) **after ≥ 50 ms of rendering at `wake == 0`** the peak contributes exactly zero: sweeping its
  anchor across a sine changes both channels by `max|diff| == 0`, and `isPeakEngineActive(i) == false`.
* (c) every peak dormant + `mix = 1` ⇒ **digital silence** on both channels (`== 0.0f`, exactly);
  `mix = 0` ⇒ both channels equal their own inputs within `kSampleTolerance` for **any** pan
  configuration.
* (d) `setPeakDormant(i, true)` vs `setPeakWake(i, 0.0f)` ⇒ `max|diff| == 0` on both channels;
  distinguished only by `isPeakDormant` / `getPeakWakeAmount`.
* (e) a mid-render `setNumPeaks` reduction silences the dropped peaks over the ramp with no
  `getPeakGate` step above **4.375e-4**.
* (h) **dormancy through a vanishing wake:** `setPeakWake(i, 1e-8f)` — a legal `[0, 1]` argument and
  exactly the shape `getEnvelopeValue() × getActiveDepth()` produces — followed by ≥ 50 ms of
  rendering leaves `isPeakEngineActive(i) == false`. This is the **only** arm that exercises
  `kWakeSilenceEpsilon`; every other dormancy arm reaches dormancy through `setPeakDormant`.

**Implement** (plan S7.1–S7.4, S6.6):

* `gateSteady(i)`: `0.0f` for an out-of-range, dormant, or out-of-count peak; and
  `(p.wakeAmount <= kWakeSilenceEpsilon) ? 0.0f : p.wakeAmount` — the snap at the **source** that
  keeps the sleep edge's exact `== 0.0f` test valid by construction.
* **`gate.setTarget()` is called ONLY from setters** (`setPeakWake`, `setPeakDormant`,
  `setNumPeaks`, `prepare`, `reset`, `clearAudioState`) through one `refreshGates()` helper, and
  **never from `updateControl()`**: `LinearRamp::setTarget` recomputes
  `increment_ = (target − current)/(rampMs·0.001·fs)` on **every** call (`smoother.h:342-354`), and
  so does `configure` when a transition is in flight (`:328-336`).
* **`refreshGates()` re-targets a peak only when `target != p.lastGateTarget`** (exact compare — the
  target is either a literal `0.0f` or the value `setPeakWake` stored verbatim). The **wake-edge
  test stays outside** that guard: `if (!p.engineActive && target != 0.0f) { bankApply(i,
  appliedHz, appliedQ, appliedBankGainDb); bankSetEnabled(i, true); p.engineActive = true; }` — one
  bool and one float compare per peak, so a peak can never sit `engineActive == false` with a rising
  gate.
* **Sleep edge, in `updateControl()` before the bank writes:**
  `if (p.engineActive && p.gate.isComplete() && p.gate.getCurrentValue() == 0.0f) { bankReset(i);
  bankApply(i, appliedHz, appliedQ, appliedBankGainDb); bankSetEnabled(i, false); p.engineActive =
  false; }`. All three steps are load-bearing: the state clear stops seconds of stored ring
  (`RT60 = Q·ln1000/(π·f) = 5.5 s` at Q = 100 / 40 Hz) from being released after the gate closed;
  the re-apply is mandatory because `ResonatorBank::reset()` is a **configuration wipe** leaving
  every slot at 440 Hz, default Q and `enabled_[i] = false` (`resonator_bank.h:225-231`); and not
  calling `bank.process` at all is what earns SC-004 (c) — an entered-but-idle bank still runs its
  three global smoothers per sample (`:474-476`).
* **`Peak::levelRamp`** — a `LinearRamp` at `kGainRampMs`, target `dbToGain(p.levelDb)`, set **only
  from `setPeakLevel`** (plus `snapTo` in `prepare`/`reset`/`clearAudioState`), advanced **per
  sample for every peak, dormant and out-of-count included, before the `engineActive` early-out**
  (SC-010 depends on that). The bank is told only
  `appliedBankGainDb = appliedGainDb − levelDb`, and
  `dbToGain(appliedBankGainDb) · dbToGain(levelDb) == dbToGain(appliedGainDb)` exactly, so FR-017's
  composition and `getPeakCurrentGainDb`'s meaning are unchanged.
* A dormant peak: lanes advance, anchors recompute, targets compute, `applied` tracks target
  **unslewed**, FR-052 getters report live values, gate and level ramps advance per sample — but
  **no `setFrequency`/`setQ`/`setGain` reaches the bank and `bank.process` is not called**.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_GateRamp" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_PeakLifeCycle" 2>&1 | tail -5
```

Record the (d-ii) red-first numbers in the scratchpad log for T024.

---

## Group K — Per-peak pan

### T011 — Seeded pan positions and the equal-power split

**File to edit:** `dsp/include/krate/dsp/systems/resonance_drift_network.h`
**Test file:** `dsp/tests/unit/systems/resonance_drift_network_test.cpp`

**Failing test first** —
`TEST_CASE("ResonanceDriftNetwork_PeakPan", "[resonance_drift_network]")`. One peak isolated (all
others dormant), sine at its centre frequency, `mix = 1`, `AnchorMode::Free`:

* (a) **equal-power constancy:** sweeping `setPeakPan(i, p)` across `[-1, +1]` in **0.1 steps** with
  wander off and ≥ 50 ms of settling per step, `outL_rms² + outR_rms²` stays within **0.5 dB** of its
  `pan = 0` value across the whole sweep.
* (b) **determinism:** two instances with the same seed report identical `getPeakPan(i)` defaults for
  all twelve (exact `==`), and with `panWander > 0` identical `getPeakCurrentPan(i)` trajectories
  sampled per control step over 10 s.
* (c) **non-vacuity (threshold measured at T019, provisional 1 dB):** ≥ 60 s at the default
  `panWander = 0.2` and `wanderRate = 0.03`, the inter-channel level difference
  `20·log10(outL_rms / outR_rms)` over 1 s windows has a **range exceeding `kPanNonVacuityDb`** —
  proof the lane is live rather than a static split that would pass (a) and (b) vacuously.

**Implement** (plan S2.4, S6.5):

* In `setSeed`, after the four lane seedings, draw the default positions from a dedicated one-shot
  generator `Xorshift32 panRng{deriveStreamSeed(seed_, kSaltPanPosition)}`:
  `side = (i % 2 == 0) ? +1 : -1`; `step = ((i / 2) + 1) / (kMaxPeaks * 0.5f)`;
  `jitter = panRng.nextFloat() * (1.0f / kMaxPeaks)`;
  `panPosition = clamp(side * step + jitter, -1, +1)`. Lowest peaks near centre (mono-compatible
  bass), highest widest, adjacent peaks never on the same side.
* Each lane `setSeed` is followed by a **mandatory** `lane.reset()`: `BrownianDrift::setSeed`
  re-seeds the RNG but leaves `x_` and the output smoother where they were
  (`brownian_drift.h:145-148`).
* Equal-power split in the control step: `theta = (appliedPan + 1.0f) * (kPi * 0.25f)` ∈ `[0, π/2]`,
  `panGainL = cos(theta)`, `panGainR = sin(theta)` — `gainL² + gainR² == 1` for every pan.
* **Document in the header:** `setSeed` **overwrites** `setPeakPan`, so a caller wanting both calls
  `setSeed` first. `reset()` does **not** redraw the positions.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_PeakPan" 2>&1 | tail -5
```

---

## Group L — `reset()` and `clearAudioState()`

### T012 — The configuration-preserving reset and its lighter sibling

**File to edit:** `dsp/include/krate/dsp/systems/resonance_drift_network.h`
**Test file:** `dsp/tests/unit/systems/resonance_drift_network_test.cpp`

**Failing tests first.**

`TEST_CASE("ResonanceDriftNetwork_ResetPreservesConfiguration", "[resonance_drift_network]")`:

* (a) **the non-silence arm — the one that catches a forwarded `ResonatorBank::reset()` without the
  re-apply.** Apply a full non-default configuration (all three modes exercised, non-default
  anchors, `peakQ`, levels, wanders, pans, `wetGain`, `numPeaks = 9`), call `reset()`, then render
  2 s of white noise at −12 dBFS: RMS **> −60 dBFS** on both channels. A build that forwards
  `reset()` without re-applying renders **digital silence**, because `ResonatorBank::reset()` leaves
  every slot at 440 Hz with `enabled_[i] = false`.
* (b) that render equals a **fresh `prepare` + the same setter sequence** sample-exactly
  (`max|diff| == 0`, both channels).
* (c) **every configuration getter** returns its pre-`reset` value — `getPeakPan(i)` included.

`TEST_CASE("ResonanceDriftNetwork_ClearAudioState", "[resonance_drift_network]")`:

* (a) one peak driven to steady state at `peakQ = 100` (`RT60 ≈ 5.5 s` at 40 Hz), then
  `clearAudioState()`: that peak's contribution is **below `kSampleTolerance = 5.0e-4`** on the very
  next sample.
* (b) two identically-seeded instances run forward together; `clearAudioState()` on one — every
  peak's `getPeakCurrentFrequency` / `Q` / `GainDb` / `Pan` agrees with the untouched twin within
  `kMetricTolerance = 2.5e-4` (`render_fingerprint.h:61`) for **≥ 60 s** afterwards (the lanes were
  never rewound).
* (c) the same comparison with `reset()` instead shows the reset instance's trajectories
  **diverge** — mean absolute difference in `log2 f` over the same 60 s exceeding 10× the (b)
  tolerance — proving the two methods are behaviourally distinct, not two names for one effect.

**Implement** (plan S2.2, S2.3):

* `reset()`: zero `controlPhase_`/`laneCounter_`/`clampEngagements_`, set `anchorsDirty_ = true`;
  `b.reset()` on every bank; `reset()` on all 48 lanes; reconfigure and **`snapTo`** (not
  `setTarget`) the mix smoother, wet-scale ramp, every gate (seeding `lastGateTarget` from
  `gateSteady(i)` at the same moment) and every level ramp; then `snapControlState()` — the
  **mandatory** full re-apply in FR-014 order, which also re-pins `setDamping(0)`,
  `setExciterMix(0)`, `setSpectralTilt(0)` and writes `bankSetEnabled(0, engineActive)`.
* `clearAudioState()`: **exactly the same steps minus the lane reset.** The four lanes per peak keep
  freewheeling. That single omission is the whole difference; (b)/(c) above prove it is real.
* This phase adds `clearAudioState()` and calls it from nowhere (Phase 10 is its consumer).

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_ResetPreservesConfiguration" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_ClearAudioState" 2>&1 | tail -5
```

---

## Group M — The output clamp and its counter

### T013 — FR-018's clamp, the saturating counter, and the criterion that can fail on their absence

**File to edit:** `dsp/include/krate/dsp/systems/resonance_drift_network.h`
**Test file:** `dsp/tests/unit/systems/resonance_drift_network_test.cpp`

**Why this task exists:** every other criterion that reads `getClampEngagementCount()` requires it to
be **zero or unchanged** (SC-001 (b), SC-008 (d), SC-009 (c), SC-016 (d)), and nothing anywhere
drives the output past `±kOutputClamp`. **A build with no clamp at all and
`getClampEngagementCount()` hardcoded to `0` passes the entire rest of the spec.**

**Failing test first** —
`TEST_CASE("ResonanceDriftNetwork_OutputClampEngages", "[resonance_drift_network]")`, entirely
through in-spec settings:

1. One peak at `setPeakQ(i, 100.0f)` anchored at **265 Hz**, `setPeakLevel(i, +12.0f)` (FR-017's
   ceiling), `setWetGain(48.0f)` (FR-045's ceiling), `mix = 1`, all other peaks dormant, driven by a
   265 Hz sine at **0 dBFS** on both channels for 5 s.
2. Assert **both** halves: `max|outL| <= kOutputClamp` **and** `max|outR| <= kOutputClamp` sample by
   sample (the clamp works), **and** `getClampEngagementCount() > 0` (the counter can increment —
   the half that fails on a hardcoded zero).
3. Record the count. Then render 5 s of the **FR-016 default patch** on white noise at −12 dBFS and
   `REQUIRE` the count is **unchanged** — an in-spec configuration does not engage the clamp, which
   is what makes SC-001 (b)'s `== 0` a real assertion.

If the drive in (1) does not reach `±4.0` in practice, **raise the drive, never weaken the
assertion**, and record the measured peak for `compliance.md`.

**Implement:** `clampCount(v)` clamps to `±kOutputClamp` and increments a **local** engagement
counter folded into `clampEngagements_` with **saturation** at `0xFFFFFFFFu` at the end of the chunk
(`noise_organism.h:2611-2616` idiom) — never wrapping, so a pathological render cannot report zero.
One counter shared by both channels; applied per channel independently, followed by
`detail::flushDenormal`.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_OutputClampEngages" 2>&1 | tail -5
```

---

## Group N — Non-finite immunity

### T014 — SC-009 in the `-fno-fast-math` TU

**File to edit:** `dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp` (and the
header only if a guard is found missing)

**Failing test first** —
`TEST_CASE("ResonanceDriftNetwork_NonFiniteGuards", "[resonance_drift_network]")`. Non-finite values
are built **from bit patterns through a volatile sink** (`reference_fastmath_nan_in_tests.md`),
never from `std::numeric_limits`:

```cpp
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;   // opaque to the optimiser
    float out{}; const std::uint32_t v = sink; std::memcpy(&out, &v, sizeof(out)); return out;
}
// quiet NaN 0x7FC00000, +Inf 0x7F800000, -Inf 0xFF800000
```

* (a) a NaN or Inf **input sample on either channel** (inject on L only, on R only, and on both;
  single samples and a 64-sample burst) leaves **every** output sample on **both** channels
  `detail::isFinite`, and the **next** block of finite input renders normally — RMS within 0.5 dB of
  an uninjected twin instance driven with the same finite content.
* (b) NaN and ±Inf into **every** float setter in the S1.3 declaration order — including
  `prepare`'s `sampleRate` (substituted by 48 000, then floored at `kMinUsableSampleRate`) — is
  **rejected**, and the matching getter still reports the **previous** value. This is FR-008's
  rejection semantics, deliberately different from `NoiseOrganism::sanitise`'s neutral substitution
  (`noise_organism.h:1099-1101`).
* (c) `getClampEngagementCount()` is **unchanged** after every injection above.

**Implement (only what the test proves missing):** the one-line `if (!detail::isFinite(v)) return;`
at the top of every float setter, the per-channel input sanitisation before the mono sum/dry path,
and the guarded `laneValue`. **No `std::isnan`/`std::isinf` anywhere** — `tools/lint-nonfinite-symbols.js`
enforces it.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_NonFiniteGuards" 2>&1 | tail -5
node tools/lint-nonfinite-symbols.js
```

---

## Group O — Realised Q (the write-order trap)

### T015 — SC-017 with its mandatory injection arm

**Test file:** `dsp/tests/unit/systems/resonance_drift_network_test.cpp`

**Why this task is separate:** the FR-014/FR-015 write-order failure is **silent**. The audio still
sounds plausible and `getPeakCurrentQ(i)` still reports the intended number, because FR-052 makes it
the network's own applied value, not a read of the filter. **SC-017 is the only criterion that can
fail on it.**

**Failing test first** —
`TEST_CASE("ResonanceDriftNetwork_RealisedQ", "[resonance_drift_network]")`:

* Setup: one peak driven in isolation (all others dormant), white noise at −12 dBFS on both
  channels, `mix = 1`, wander off, ≥ 100 control chunks of settling before any read.
* **Estimator: the Phase 2 Welch-on-an-arbitrary-probe-grid band-ratio fit**, copied from
  `dsp/tests/unit/systems/noise_organism_test.cpp:1631-1728` — `welchPowerGrid` +
  `measuredBandSum` / `modelBandSum` + `fitQFromBandRatio` bisection. **Not** a −3 dB crossing
  search: ~1 dB of noise on a Lorentzian flank is ~23 % of the half-width and would consume the
  entire tolerance before any real defect.
* Coverage: `peakQ ∈ {2, 12, 100}` × anchors `{40, 265, 1278}` Hz, nine combinations. For each,
  require the fitted `f / BW` to agree with `getPeakCurrentQ(i)` within **25 %**.
* `getPeakEquivalentRt60(i)` is consistent with `Q·ln1000/(π·f)` within the same tolerance **and
  must change** when `peakQ` changes at a fixed anchor — which it cannot if Q is being derived from
  the stale decay table.
* **Injection arm, mandatory.** Temporarily mutate the header two ways, one at a time, and record
  the measured `f/BW` for each: (i) swap the FR-014 order to `setQ` **then** `setFrequency`;
  (ii) let change detection skip an "unchanged" Q write after a frequency write. **Both mutated
  builds must go red** (the expected reading is `rt60ToQ(f, 1.0) = 0.4548·f` — Q ≈ 18 at 40 Hz,
  clamped to 100 above ≈ 220 Hz). Revert and re-verify green. Record all three readings.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_RealisedQ" 2>&1 | tail -5
```

---

## Group P — Spectral and statistical criteria (two parallel-safe tasks)

> Both tasks below create cases in **different** files and touch no shared file. Neither runs timing
> assertions, so they may run in parallel — but not alongside T020.

### T016 [P] — SC-001, SC-002 (a)(b)(c) and SC-018 (c)

**File to edit:** `dsp/tests/unit/systems/resonance_drift_network_spectral_test.cpp`

`TEST_CASE("ResonanceDriftNetwork_MaxQRingOut", "[resonance_drift_network]")` (SC-001): 12 peaks,
every `peakQ = kMaxResonatorQ = 100`, `qWander = 0`, FR-016 anchors, `mix = 1`; white noise at
−12 dBFS **on both channels** for 60 s, then silence.

* (a) every sample `detail::isFinite` — never `std::isnan`.
* (b) `getClampEngagementCount() == 0`.
* (c) **the derived `1/sqrt(N)` relationship (correction C-10), not agreement.** Render twice with
  **peaks 4–11 dormant in BOTH arms** so the acoustic content is identical, once at
  `setNumPeaks(4)` and once at `setNumPeaks(12)`; require the level difference to equal
  `20·log10(sqrt(12/4)) = **4.77 dB ± 0.5 dB**`. This is simultaneously the only assertion of
  FR-019's "`N` is `numPeaks`, not the awake count".
* (d) the tail falls below **`1.0e-4`** (−80 dBFS) within a bound **computed in the test from shipped
  constants** — `kMaxResonatorQ * kLn1000 / (kPi * kMinResonatorFrequency) + 5.0 ≈ 16.0 s` —
  `REQUIRE`d to sit in **[15.9, 16.1]** before being used (the
  `continuous_body_test.cpp:3361-3386` pattern).
* (e) repeat (a)–(d) with all four depths at maxima (24 st / 2 oct / 24 dB / pan 1.0) and
  `wanderRate = 1.0`.

`TEST_CASE("ResonanceDriftNetwork_NoZipperUnderDrift", "[resonance_drift_network]")` (SC-002): a
220 Hz sine at −12 dBFS on both channels, 12 peaks with anchors spread across it, **all four lanes
at max depth**, `wanderRate = 1.0` (so `decimation == 1`, the un-decimated path).

* **Exactly 60 s** — 2 880 000 samples, 45 000 control chunks. Pinned, because the boundary
  population size determines the statistic.
* `x[n] = outL[n]`; `d[n] = |x[n] − 2x[n−1] + x[n−2]|`; partition on **`n mod 64 ∈ {0, 1, 2}`**
  (3/64 boundary, 61/64 interior) — correction C-11. Leaving residue 2 in the interior puts
  contaminated samples (1.6 % of that population) into the 0.1 % tail `P` is drawn from and collapses
  `B/P` toward 1.
* `B` and `P` are **the same 99.9th percentile** of their own populations (a max-vs-percentile
  comparison sits at the 99.9989th quantile for this length and goes red from extreme-value
  statistics alone).
* (a) `B <= kBoundaryRatio · P`, with `kBoundaryRatio` **measured at T019 across ≥ 8 seeds under this
  partition** (provisional **1.5**; a figure measured under a `{0,1}` partition may not be carried
  over).
* (b) control arm with `setWanderEnabled(false)`: `B_off / P_off <= 1.05` **and**
  `|P_on − P_off| / P_off <= 0.10`.
* (c) **injection, mandatory, systematic, through the public surface** (correction C-12): with
  `setSlewCeilings(24.0f, 24.0f)` — an in-spec setting, **no `#ifdef` hook and no edit to the header
  under test** — apply an **alternating ±1-octave `setPeakAnchorHz` jump on every control chunk for
  the full pinned 60 s render**. Same duration, partition and statistic as (a); only the injection
  schedule changes. **`B / P` must exceed 10.** (One-shot injection is arithmetically unsatisfiable:
  three outliers cannot move a 135 000-sample 99.9th percentile.)

Add **SC-018 (c)** here, in the same TU and on the same machinery:
`ResonanceDriftNetwork_SlewLimit`'s (c) arm renders for the **same pinned 60 s**, repeating the
55 ↔ 110 Hz `setNoteFrequency` step on a fixed **1 s** schedule, and computes `B/P` over that whole
render against SC-002 (a)'s bound. Same duration, partition, population size and statistic, so the
bound transfers legitimately.

**Verify:**

```bash
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_MaxQRingOut" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_NoZipperUnderDrift" 2>&1 | tail -5
```

### T017 [P] — SC-006 seed determinism and SC-008 sample-rate independence

**File to edit:** `dsp/tests/unit/systems/resonance_drift_network_test.cpp`

`TEST_CASE("ResonanceDriftNetwork_SeedDeterminism", "[resonance_drift_network]")`:

* (a) same seed, config and rate ⇒ `max|diff| == 0` over 10 s on **both** channels.
* (b) **trajectory decorrelation over ≥ 20·τ_effective** — take the cheap option:
  `setWanderRate(1.0f)` ⇒ τ = 1 s ⇒ a **20 s** render; transcribe the duration and resulting τ into
  `compliance.md`. Pairing is **peak *i* of A against peak *i* of B**, 12 pairs, **not** 24
  cross-paired. `kSeedDecorrelationR` is measured at T019 across ≥ 12 seed pairs.
  **Anti-vacuity: the same estimator on a same-seed pair must read ≥ 0.95.**
* (c) the audio of the two differs: RMS of the difference ≥ **−30 dB** relative to either render.
* (d) `reset()` with no setter called since `prepare` reproduces the post-`prepare` stream **exactly**
  (`max|diff| == 0`).

`TEST_CASE("ResonanceDriftNetwork_SampleRateIndependence", "[resonance_drift_network]")`, at
44 100 / 48 000 / 96 000 Hz with identical config and seed:

* (a) wander off, `getPeakCurrentFrequency(i)` agrees within **0.1 %** across the three rates for
  every peak whose anchor is below `0.45 × 44 100 = 19 845 Hz` (all twelve defaults are).
* (b) the gate ramp measures **50 ms ± 5 ms** at every rate, via T010's named estimator.
* (c) **a rate-invariant quantity, not three independent draws:** over ≥ 50·τ at each rate (e.g. 20 s
  at `wanderRate = 1.0`), estimate the **1/e decorrelation lag in seconds** of a peak's `log2 f`
  trajectory and require the three to agree within a tolerance **measured at T019 across ≥ 8 seeds
  at one fixed rate** (provisional **±15 %**), with an **injection check** that a deliberately
  hardcoded sample rate in the lane mapping moves the statistic outside it.
* (d) at 44.1 kHz an anchor set above `0.45·fs` (e.g. `setPeakAnchorHz(11, 30000.0f)`) is **clamped
  silently**, reported clamped by `getPeakAnchorHz`/`getPeakCurrentFrequency`, produces no
  non-finite value, and causes **no** clamp-counter engagement.

**Verify:**

```bash
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_SeedDeterminism" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_SampleRateIndependence" 2>&1 | tail -5
```

---

## Group Q — The `[long]` set

### T018 — SC-003, the remaining SC-015 arms, and SC-016

**File to edit:** `dsp/tests/unit/systems/resonance_drift_network_spectral_test.cpp`

Tag all three cases `[resonance_drift_network][long]`: each is multi-minute and every assertion is a
property of the seeded walk, of exact zeros, or of a measured statistic — **toolchain-independent**,
so they belong in the nightly lane. **Do not tag** SC-001, SC-002, SC-009, SC-010, SC-011, SC-019 or
SC-021 (the cross-platform sentinels and the sub-second renders).

`TEST_CASE("ResonanceDriftNetwork_WanderRateSpectral", "[resonance_drift_network][long]")` (SC-003):
≥ `10·T` (**350 s** at the 0.03 Hz default), pink noise at −12 dBFS, `mix = 1`. Every **100 ms**
extract the five `Krate::Test::AudioFeatures::band` fractions
(`tests/test_helpers/audio_features.h:23-37`). **Band selection is fixed and recorded:** eligible =
mean fraction ≥ 0.01; among eligible, the highest CV **in the wander-on arm**; that index is used in
**every** arm and written into `compliance.md`.

* (a) lag-`T/8` normalised autocorrelation of the mean-removed trajectory **≥ 0.20**.
* (b) the `setWanderEnabled(false)` control arm's CV is **≥ 1.8×** below the wander-on CV.
* (c) sampling `getPeakCurrentFrequency` / `getPeakCurrentQ` every control step: **zero** excursions
  outside the FR-030/FR-031 bounds in **≥ 10⁵** samplings.
* (d) per-peak lag-`T/8` ACF **≥ 0.20** and lag-`8T` **≤ 0.10**.
* (e) **independence against a measured null:** mean `|r|` over the `C(12,2) = 66` pairs of
  mean-removed `log2(getPeakCurrentFrequency(i))` trajectories, bound `kLaneIndependenceR` derived at
  T019 from **≥ 12 network seeds**. **Phase 2's 0.05 is NOT transcribed** — Bartlett's formula gives
  `E|r| ≈ 0.23` here from estimator noise alone. **In-process anti-vacuity control:** twelve lanes
  built on the same seed **and salt** must read **≥ 0.95**.
* (f) **`setWanderRate` is not inert — the only arm that fails if FR-037 is missing.** 0.005 Hz
  (`decimation = 7`) vs 0.3 Hz (`decimation = 1`), each ≥ `10·T`, ACFs evaluated at the **same
  absolute lag of 1.67 s**; `ρ_slow − ρ_fast >= kRateSeparation`, measured at T019 across ≥ 8 seeds.

Extend `ResonanceDriftNetwork_PeakLifeCycle` (already `[long]`) with:

* (b) **lanes freewheel while dormant:** `getPeakCurrentFrequency(i)` across a **120 s** dormant
  interval differs from its value at sleep by **≥ 20 cents for ≥ 9 of the 12 peaks**.
* (f) **wake after 120 s dormancy at `peakQ = 100`** (own `RT60 = 5.5 s` at 40 Hz): the gate
  satisfies T010's 50 ms ± 5 ms bound, and the peak's contribution **never exceeds its own
  pre-dormancy steady-state peak magnitude** during the first 5 s — red without the sleep-edge state
  clear. **Extended:** holding the peak at `wake = 0` for one extra control step,
  `getPeakCurrentFrequency` / `Q` / `GainDb` already reflect the drifted value **before** the gate
  lifts. **C-8 arm:** a `setNoteFrequency` octave jump applied *during* the dormant interval is
  reflected by `getPeakCurrentFrequency(i)` within **one** control step, not fifty — this is what
  distinguishes unslewed dormant tracking from a slew-limited dormant peak.
* (g) over a scripted 20–90 s wake/sleep sequence at `numPeaks = 12`, 200 ms-window RMS moves by no
  more than a bound **measured at T019 across ≥ 8 seeds**; a mid-render `setNumPeaks(12 → 4)` moves
  the survivors by **4.77 dB ± 0.5 dB**.

`TEST_CASE("ResonanceDriftNetwork_LongRenderBoundedness", "[resonance_drift_network][long]")`
(SC-016): every depth and `wanderRate` at maximum, `peakQ = 100`, an external wake pattern on a
**20–90 s** stochastic schedule (`slow_event_scheduler.h:164-165` defaults, driven by the test, not
owned by the component), pink noise at −12 dBFS, **30 minutes at 48 kHz**.

* (a) every **10 s** window's RMS within `±kSoakWindowDb` of the median — **measured** at T019 across
  ≥ 8 seeds (provisional **±6 dB** and explicitly not trusted: Phase 2 had to widen ±3.0 → ±4.5 dB
  for a *less* extreme configuration).
* (b) least-squares slope within **±0.5 dB / 30 min**.
* (c) no window below `kSoakFloorDbfs` (measured; provisional **−60 dBFS**) nor above **−3 dBFS**.
* (d) **zero** non-finite samples and `getClampEngagementCount() == 0`.

**Verify (capture to a log on the first run; never re-run to grep):**

```bash
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[long]" 2>&1 | tee long.log | tail -10
```

---

## Group R — Calibration

### T019 — Measure the ten provisional thresholds and `kDefaultWetGainDb`

**Files to edit:** `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp` (the calibration
case), then the constants in `resonance_drift_network_test.cpp` /
`resonance_drift_network_spectral_test.cpp`, and the header comment beside `kDefaultWetGainDb` in
`dsp/include/krate/dsp/systems/resonance_drift_network.h`.

**Write one hidden case** —
`TEST_CASE("ResonanceDriftNetwork_MeasureThresholds", "[resonance_drift_network][.calibration]")`,
following the `NoiseOrganism_MeasureSourceDrive` precedent
(`noise_organism_perf_test.cpp:19-20`). It renders each **null distribution** and prints mean,
spread and observed extreme for:

| Constant | Where it is used | Seeds |
|---|---|---|
| `kBoundaryRatio` (prov. 1.5) | SC-002 (a) — **measured under the `{0,1,2}` partition only** | ≥ 8 |
| `kLaneIndependenceR` | SC-003 (e) | ≥ 12 |
| `kRateSeparation` | SC-003 (f) | ≥ 8 |
| `kSeedDecorrelationR` | SC-006 (b) | ≥ 12 seed **pairs** |
| rate-invariance tolerance (prov. ±15 %) | SC-008 (c) | ≥ 8 |
| wake/sleep RMS bound | SC-015 (g) | ≥ 8 |
| `kSoakWindowDb` (prov. ±6 dB) | SC-016 (a) | ≥ 8 |
| `kSoakFloorDbfs` (prov. −60 dBFS) | SC-016 (c) | ≥ 8 |
| SC-020 (a)'s window | tied to `kDefaultWetGainDb` | ≥ 8 |
| `kPanNonVacuityDb` (prov. 1 dB) | SC-021 (c) | ≥ 8 |

**`kDefaultWetGainDb` is measured in the same case (FR-045):** render the FR-016 reference patch
(`numPeaks = 12`, all defaults, `AnchorMode::Free`) driven by white noise at −12 dBFS with
`mix = 1`; measure the wet RMS against the drive RMS; choose `kDefaultWetGainDb` so the wet RMS
lands within a few dB of the drive RMS. The independent estimate to sanity-check against: twelve
constant-0 dB-peak bandpasses at Q = 12 on the FR-016 anchors have a summed noise-equivalent
bandwidth of ≈ `(π/2)·Σ(f_i/Q) ≈ 605 Hz` against a 24 kHz white-noise bandwidth (≈ −16 dB), plus
`1/sqrt(12)` (−10.8 dB) plus the −6 dB default level ⇒ **≈ −33 dB of passive attenuation**, so the
provisional **+30 dB** is the right order. Record the measured wet-vs-drive gap, the chosen
constant, the method and the date in the header comment beside the constant.

**Then write `SC-020`:**
`TEST_CASE("ResonanceDriftNetwork_WetGainTrim", "[resonance_drift_network]")`:

* (a) the reference patch on SC-001's broadband drive at the measured `kDefaultWetGainDb` lands
  within the window recorded in `compliance.md` (a compliance row, not a re-measurement).
* (b) **isolated directly, never by differencing** (correction C-13): render the same patch and drive
  at **`mix = 1`** (wet only) and at **`mix = 0`** (dry only) and require
  `|RMS_wet_dB − RMS_dry_dB| <= 6 dB`; the `mix = 0.5` render is kept only as a **monotonicity**
  check, its RMS lying between the two.
* (c) `getWetGain()` echoes the last set value clamped to `[-24, +48]`, and a 0 dB trim reduces the
  wet RMS relative to (a) by the expected dB difference within **0.5 dB**.

**Bound provenance rule, binding.** Before a threshold is transcribed, the arm it belongs to is
**run against an injected defect and must go red**. Any threshold that cannot separate a correct
implementation from an injected defect is **re-derived from a measured distribution and the change
recorded** — never merely widened. Phase 2 had to rewrite five criteria after measurement, including
one that "passed on seed luck" (`specs/vorago-phase2-noise-organism/compliance.md:71-86`).

**Then re-run T016, T017 and T018 against the measured bounds.**

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.calibration]" 2>&1 | tee calib.log | tail -40
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ResonanceDriftNetwork_WetGainTrim" 2>&1 | tail -5
```

---

## Group S — CPU baselines (runs alone)

### T020 — SC-004, measured in isolation, with both compile-time clauses

**File to edit:** `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp`

`TEST_CASE("ResonanceDriftNetwork_CpuBudget", "[resonance_drift_network][.perf]")`. Reference
configuration: 12 peaks, `AnchorMode::Hybrid` at `gravity = 0.5`, all four lanes at FR-016 defaults,
wander on, `mix = 1`, all awake, drive identical on both channels. Trial shape and helpers copied
from `noise_organism_perf_test.cpp:195-300`: best-of-25 × 500 blocks after 400 warm-up blocks,
512-sample blocks at 48 kHz.

* (a) **≤ 80 000 ns per 512-sample block** (roadmap line 228's 0.75 %/voice).
* (b) the `setWanderEnabled(false)` arm is **≥ 10 %** below the reference — this is what proves the
  FR-015 change detection actually saves work; a change-detection path that saves nothing is not
  implemented.
* (c) the all-dormant arm is **≥ 40 %** below the reference, with `setPeakDormant(i, true)` on all
  twelve **then ≥ 50 ms of rendering** so every gate landed at exactly 0 and every sleep edge fired;
  `isPeakEngineActive(i) == false` for all twelve **asserted before timing starts**; and the output
  verified **exactly `0.0f`** on both channels throughout, so a "cheap" figure cannot come from an
  accidental early return.
* (d) each gated arm carries a checked-in baseline with **two different** `static_assert`s
  (`noise_organism_perf_test.cpp:1560-1575` idiom), evaluated on **every CI leg** even though the
  timing cases are `[.perf]`-hidden:

```cpp
static_assert(kBaselineX * kRegressionFactor <= kBudgetNs, "...exceeds the 0.75 % budget");
static_assert(kBaselineX >= kBudgetNs / 50.0,             "...looks like a no-op run");
```

with `kRegressionFactor = 1.5` and `kBudgetNs = 80000.0` (so the anti-no-op floor is 1 600 ns). The
floor is not a restatement of the ceiling: without it a baseline taken from an un-`prepare`d network
— which fills silence and advances nothing — would compile and pass forever.

**If a measurement is over budget: REDUCE COST, NEVER RAISE THE BASELINE.** The escalation is
FR-013's tiers, then FR-038's static-pan fallback, then **stop and surface to the user with the
measured table**. Never a threshold change, never a cap reduction.

**Isolation is mandatory and has two clauses** (CLAUDE.md, `feedback_cpu_tests_isolation_only.md`):
not concurrently with any other suite, build, tidy run or agent, **and not back-to-back either** —
`run-cpu-tests.js` settles 20 s between suites. A test that flips verdicts between runs is measuring
the machine, not the code.

**Verify:**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
node tools/run-cpu-tests.js dsp_systems_tests 2>&1 | tee cpu.log | tail -20
```

---

## Group T — Integration

### T021 [P] — Registration-completeness audit

**Files to read (edit only if something is missing):** `dsp/tests/CMakeLists.txt`,
`dsp/lint_all_headers.cpp`

1. `grep -n "resonance_drift_network" dsp/tests/CMakeLists.txt` shows **exactly five** hits: the four
   TUs in the `dsp_systems_tests` source list and **only** the non-finite TU in the
   `-fno-fast-math -fno-finite-math-only` block. Any other TU appearing in that block is a defect:
   `-fno-fast-math` would disable the very guards the other three prove and would move the perf
   baselines.
2. `grep -n "resonance_drift_network" dsp/lint_all_headers.cpp` shows **exactly one** hit, in the
   Layer 3 block.
3. `build/windows-x64-release/bin/Release/dsp_systems_tests.exe --list-tests | grep ResonanceDrift`
   lists every case this phase wrote — a case defined in an unregistered TU appears nowhere.
4. No plugin, CI, clang-tidy-script, preset or CMake-preset file was changed by this phase:
   `git status --short` shows changes only under `dsp/` and `specs/vorago-phase3-resonance-drift/`.

### T022 — Full-suite run and the SC-013 regression gate

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target \
  dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests \
  membrum_tests innexus_tests seraphis_tests 2>&1 | tee build.log | tail -20
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests \
         dsp_effects_tests membrum_tests innexus_tests seraphis_tests; do \
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done
git diff --stat dsp/include/
```

* **Default path (no FR-013 fallback taken):** every suite green, **no edits to existing cases**, and
  `git diff --stat dsp/include/` shows **no change outside the one new header**.
* **If Tier 1 or Tier 2 was taken (T004):** the same suites were run **before and after** and the
  results diffed. **Any moved result is a regression to investigate and surface, never a golden to
  update.**
* Zero compiler warnings on the whole build. "Pre-existing" is not an excuse — own every warning and
  every failure.

### T023 [P] — Lints, portability, clang-tidy (SC-012)

```bash
node tools/check-portability.js
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-nonfinite-symbols.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja 2>&1 | tee tidy.log | tail -30
```

All must exit 0; clang-tidy must show **zero new warnings** (and fix any it reports on the new
header — a warning anywhere in `dsp` is owned, not deferred). A green Windows build proves nothing
about the Linux/macOS legs: `check-portability.js` is the gate that catches MSVC leniency, and it
only **compiles**, so re-read the header for runtime platform gates (`std::isnan` under
`-ffast-math`, narrowing in brace init, `std::log2` in a `constexpr` initialiser) by eye as well.

### T024 — `compliance.md`

**File to create:** `specs/vorago-phase3-resonance-drift/compliance.md`

One row per **FR-001 … FR-064** and per **SC-001 … SC-022**, each carrying:

* the implementing `file:line` (read now, not remembered) or the `TEST_CASE` name;
* the **actual measured number** for every SC that measures one — the probe table and both gate
  evaluations from T002, the tier taken and why, the (d-ii) red-first ramp readings from T010, the
  three SC-017 injection readings from T015, SC-002 (c)'s injected `B/P`, every threshold measured at
  T019 with its distribution and seed count, the measured `kDefaultWetGainDb` and wet-vs-drive gap,
  the SC-004 (a)/(b)/(c) figures with their percentages, the SC-001 (d) computed ring bound, and the
  band index SC-003 selected;
* the lint/tidy/suite outputs from T022 and T023, verbatim;
* the T005 spec-correction list (C-1, C-6 … C-13) with the FR/SC each amended;
* any open question left to the user (plan S17's OQ-1 and OQ-2, and their resolutions).

**No ✅ without evidence recorded this session.** If a requirement is not met, say so and document
the gap: a table full of unverified ticks is worse than an honest ❌.

---

## Dependency summary

```
A (T001 registration)
  └─ B (T002 probe, gate 1 + gate 2)                     ← may stop the phase
       ├─ C (T003) → D (T004)   [conditional on gate 1 only]
       └─ E (T005 spec write-back)
            └─ F (T006 skeleton + lint registration)
                 └─ G (T007 anchors, modes, slew)
                      └─ H (T008 lanes, decimation, maps)
                           └─ I (T009 engine seam, writes, render tail)
                                └─ J (T010 gates, dormancy, level ramp)
                                     └─ K (T011 pan)
                                          └─ L (T012 reset / clearAudioState)
                                               └─ M (T013 output clamp + counter)
                                                    └─ N (T014 non-finite)
                                                         └─ O (T015 realised Q + injection)
                                                              └─ P (T016 [P], T017 [P])
                                                                   └─ Q (T018 [long])
                                                                        └─ R (T019 calibration)
                                                                             └─ S (T020 CPU, alone)
                                                                                  └─ T (T021 [P],
                                                                                       T022,
                                                                                       T023 [P],
                                                                                       T024)
```

Groups F through O each contain exactly one task because each edits
`dsp/include/krate/dsp/systems/resonance_drift_network.h` — the single shared file of this phase.
The only parallel-safe pairs are T016/T017 (disjoint test TUs, no timing) and T021/T023 (audit and
lints, neither building nor timing).
