# Tasks: Vorago Phase 12 — Full Parameter Surface & State

**Spec:** `specs/vorago-phase12-parameters/spec.md` · **Plan:** `specs/vorago-phase12-parameters/plan.md`
**Branch:** `feat/vorago-phase1-events-modulation` (base `f149cced`) · **Status:** TASKS — nothing executed.

## How to read this file

- Tasks `T001…` are grouped into **ordered groups**. A group starts only when the previous group is done.
- Inside a group, `[P]` tasks touch **fully disjoint files that are new in this phase** and may run in
  parallel. Every task that edits a shared file (a CMake list, an existing header/cpp, a test file another
  task in the same group also edits) sits in a sequential group.
- Every task follows the repo order: **failing test first → implement → zero warnings → tests pass**.
  "Failing" includes "does not compile because the API does not exist yet" — record the failure once.
- No task commits. Commits happen outside this workflow.
- **Gate P-0 (plan §2.6) is binding:** T004 → T005 must finish before anyone lands a Gravity or Pressure
  `kRows` row. The user ruling is **already taken (spec R-1, 2026-09-24)**: an axis the probe confirms
  unreachable gains one new macro target; T005 is now a mechanical read of the probe table, and T007's
  body is fixed for both outcomes. Plan-stage rulings R-2..R-7 (trackers, ±24 dB, block-rate SC-011
  remedies and scope, front-loaded CMake, 15 pack TUs, ASCII titles) are all confirmed as written here. Every other group is independent
  of that ruling and proceeds.

### Commands used by every task (repeated inline where a task needs them)

```
BUILD  : "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <target>
RUN    : build/windows-x64-release/bin/Release/<target>.exe "<TestCaseName>" 2>&1 | tail -5
LOG    : slow runs are redirected to specs/vorago-phase12-parameters/artifacts/<name>.log and read from the
         log; a slow suite is never re-run just to see output
```

- Catch2 filter is the positional test-name argument (not `-c`); `[long]` and `[.perf]` cases run alone.
- Zero warnings means the BUILD output for the touched target has no `warning` line from a touched file.
- Non-finite checks: `Krate::DSP::detail::isFinite` (`dsp/include/krate/dsp/core/db_utils.h:118,125`);
  NaN/Inf test values are built from bit patterns through a `volatile` (never `std::numeric_limits` in a
  fast-math TU, never `std::isnan`).
- ODR: before creating any new class/struct name, run `grep -rn "class <Name>\b\|struct <Name>\b" dsp/ plugins/`
  and record the result in the task's log line. Names already swept by the spec (*New components*) still
  get re-swept.

### Deviation from the workflow template, stated

The workflow template puts "CMake registration (single task)" in the last group. Here it is the **single**
task **T002, at the front**, because `vorago_tests` and `dsp_systems_tests` enumerate their sources
(`plugins/vorago/tests/CMakeLists.txt:5-6`: "ENUMERATED, not globbed … silently drops out") and a
test-first task cannot run a TU that is not registered. T002 registers every new file once, as a
compile-clean placeholder; later tasks fill the placeholders (disjoint files, so `[P]` stays legal). The
last group keeps a **registration audit** (T056) that edits nothing unless the audit finds a gap.

A second, additive deviation: plan §7.1 lists no per-pack test TU. To make the 14 pack headers genuinely
parallel **and** test-first, each pack task owns one new TU `plugins/vorago/tests/unit/params/<section>_params_test.cpp`.
These add coverage only; SC-008 / SC-018 still exercise the packs in aggregate.

---

## Group 1 — Pre-flight (sequential)

### T001 — Baseline, branch and artifacts directory

- **Files:** create `specs/vorago-phase12-parameters/artifacts/` (directory) and
  `specs/vorago-phase12-parameters/artifacts/baseline.log`.
- **Steps:**
  1. `git branch --show-current` must print `feat/vorago-phase1-events-modulation`; `git log -1 --format=%h`
     must be `f149cced` or a descendant. If not, STOP and report.
  2. BUILD targets `dsp_systems_tests dsp_effects_tests vorago_tests Vorago`.
  3. Run and append the last 5 lines of each to `baseline.log`:
     `dsp_systems_tests.exe "~[.perf]~[performance]~[long]~[.probe]"`,
     `dsp_effects_tests.exe "~[.perf]~[performance]~[long]"`, `vorago_tests.exe "~[.perf]~[performance]~[long]"`.
- **Pass condition:** all three report "All tests passed" (record counts). Any pre-existing red is
  recorded and owned by this phase (memory: "never pre-existing"), not ignored.
- **Note:** `VoragoMacro_SweepAxes` (`dsp/tests/unit/systems/vorago_macro_test.cpp:1211`, `[long]`) is
  known to FAIL today on the Gravity, Pressure and Mass rows (its own comment `:1301-1311`;
  `specs/vorago-phase10-voice-engine/compliance.md:20`). It is not run here; T004 measures it.

### T002 — Single CMake registration of every new file (placeholders)

- **Files edited:** `dsp/tests/CMakeLists.txt`, `plugins/vorago/tests/CMakeLists.txt`,
  `plugins/vorago/CMakeLists.txt`.
- **Files created as placeholders** (each test TU contains only a header comment naming its owning task
  and `#include <catch2/catch_test_macros.hpp>`; each header contains only a comment and `#pragma once`):
  - DSP tests: `dsp/tests/unit/systems/vorago_param_surface_test.cpp`,
    `dsp/tests/unit/systems/vorago_macro_retune_probe_test.cpp`.
  - Plugin tests: `plugins/vorago/tests/unit/state_v2_test.cpp`, `unit/param_table_test.cpp`,
    `unit/param_table_expected.h` (header; not a source-list entry), `integration/param_surface_test.cpp`,
    `integration/sustain_test.cpp`, `integration/channel_pressure_test.cpp`, `integration/seed_test.cpp`,
    `integration/automation_rt_test.cpp`, `integration/continuity_test.cpp`, `integration/soak_test.cpp`,
    `phase11_reference_chain.h` (header, tests root), and the 15 pack TUs
    `unit/params/{global_ext,cloud,noise,resonance,ecology,sub,smear,events,ecosystem,body,space,envelope,bloom,ghost,life}_params_test.cpp`.
  - Plugin headers: `plugins/vorago/src/parameters/{param_mapping,cloud_params,noise_params,resonance_params,ecology_params,sub_params,smear_params,events_params,ecosystem_params,body_params,space_params,envelope_params,bloom_params,ghost_params,life_params,param_routes}.h`,
    `plugins/vorago/src/processor/sustain_latch.h` (17 headers).
- **Edits:**
  1. `dsp/tests/CMakeLists.txt`: after `unit/systems/vorago_ghost_ext_test.cpp` (`:536`) add a
     `# Vorago Phase 12 (specs/vorago-phase12-parameters)` comment block in the `:492-536` style naming
     each TU's SCs, then the two TUs. Add **only** `unit/systems/vorago_param_surface_test.cpp` to the
     `-fno-fast-math -fno-finite-math-only` list (the block ending `:1008-1009`), with a one-line reason
     ("injects NaN/Inf bit patterns into the Phase 12 setters"). The probe TU stays out.
  2. `plugins/vorago/tests/CMakeLists.txt`: add the 9 SC TUs and 15 pack TUs to `add_executable(vorago_tests …)`
     (`:7-17` block). Add to the fast-math-off list (`:66-73`): `unit/state_v2_test.cpp`,
     `unit/param_table_test.cpp`, `integration/automation_rt_test.cpp`, `integration/continuity_test.cpp`,
     `integration/soak_test.cpp`, and all 15 `unit/params/*_params_test.cpp` (they inject NaN into loaders).
     `integration/processor_cpu_test.cpp` stays absent (its `:80-82` comment).
  3. `plugins/vorago/CMakeLists.txt`: under `# Parameter Packs` (`:38-40`) list the 16 parameter headers;
     under `# Processor` list `src/processor/sustain_latch.h`.
- **Verify:** reconfigure is implicit in BUILD; BUILD `dsp_systems_tests vorago_tests Vorago` → zero
  warnings; `dsp_systems_tests.exe "~[.perf]~[performance]~[long]~[.probe]"` and
  `vorago_tests.exe "~[.perf]~[performance]~[long]"` pass with the **same counts as T001**.

---

## Group 2 — D-1 matrix base override (sequential)

### T003 — `VoragoMacroMatrix::setTargetBase / resetTargetBases / getTargetBase` (FR-001, FR-002, SC-006)

- **Files:** `dsp/tests/unit/systems/vorago_param_surface_test.cpp` (fill placeholder),
  `dsp/include/krate/dsp/systems/vorago_macro_matrix.h`.
- **Failing test first** — `TEST_CASE("VoragoMacro_TargetBaseOverride", "[systems][vorago]")`, four SECTIONs:
  1. `"NoOverrideIsBitIdentical"`: 8 macro vectors drawn from `std::mt19937{12012}` (each field uniform
     in [0,1]). Two matrices A, B (B freshly constructed, never touched by `setTargetBase`; A had
     `setTargetBase(CloudRichness, 0.3f)` then `resetTargetBases()`). Two engines prepared identically via
     `makeEngine` (`tests/test_helpers/vorago_fixtures.h:734`); `A.setMacros(v); A.apply(e1)`,
     `B.setMacros(v); B.apply(e2)`. `REQUIRE` every Voice getter on every `i < getPolyphony()` and every
     Engine getter equal with `==` (exact), and every field of `A.computeCavernTargets()` `==` B's.
  2. `"OverrideComposesWithMacro"`: for each pair (target, override, macro) in
     `{CloudRichness, 0.45f, Density}`, `{NoiseLevelDb, -24.0f, Density}`, `{CavernSize, 0.30f, Depth}`,
     `{OutputSaturation, 0.05f, Pressure}`: for macro value `k/10`, k = 0..10: set only that macro, apply,
     read the owner getter (cavern: the `computeCavernTargets()` field). Expected value computed
     **independently in the test** as `clamp(override + Σ_rows amount·applyModCurve(curve, m))` over the
     `VoragoMacroMatrix::kRows` entries on that target (bipolar Gravity form if Gravity is the macro), clamp
     to the owner's documented range. `REQUIRE(getter == Approx(expected).epsilon(1e-6))` and the 11-point
     series strictly increasing.
  3. `"OverrideAtTravelClampSaturates"`: `setTargetBase(CloudRichness, 1.0f)`; Density 0..1 in 11 steps →
     series non-decreasing; `getTargetBase(CloudRichness) == 1.0f`.
  4. `"RejectsNonFiniteAndOutOfRange"`: for NaN, +Inf, −Inf built from bit patterns `0x7FC00000`,
     `0x7F800000`, `0xFF800000` through a `volatile std::uint32_t` + `std::memcpy`:
     `setTargetBase(CloudRichness, bad)` leaves `getTargetBase(CloudRichness) == 0.70f`;
     `setTargetBase(static_cast<VoragoMacroTarget>(39), 0.5f)` and `(255)` change no target's
     `getTargetBase`; after overriding all 39 targets to 0.123f, `resetTargetBases()` makes every
     `getTargetBase(t)` equal the first `kRows` base for `t`.
- **Run:** BUILD `dsp_systems_tests` — fails to compile (no API). Record.
- **Implement** (plan §2.1): replace the banner `vorago_macro_matrix.h:58-63` ("NOT PROVIDED, DELIBERATELY")
  with the new contract paragraph (why the override exists; `everyRowSharesOneBasePerTarget` still
  guarantees one literal per target; NO HEADROOM RESCALING; SC-009 clause 1 holds with no override).
  Add public `setTargetBase` (reject `i >= kNumTargets` or `!isFiniteBits(base)`), `resetTargetBases`,
  `getTargetBase` (override if set else `literalBaseFor(target)`), private `static constexpr literalBaseFor`,
  members `std::array<float, kNumTargets> baseOverride_{}` and `std::array<bool, kNumTargets> hasOverride_{}`.
  Change exactly one line in `evaluateAll()` (`:1074-1086`): seed `value[i] = hasOverride_[i] ? baseOverride_[i] : row.base;`.
  No clamp at the matrix. All six `static_assert`s (`:1097-1117`) stay. Everything `noexcept`, no allocation.
- **Verify:** BUILD `dsp_systems_tests` zero warnings; RUN `VoragoMacro_TargetBaseOverride` passes; RUN
  `"VoragoMacro_*"` excluding `[long]` (`dsp_systems_tests.exe "VoragoMacro_*" "~[long]"`) passes —
  `VoragoMacro_NeutralIsIdentity` (`vorago_macro_test.cpp:1419`) and `VoragoMacro_ApplyIsIdempotent`
  (`:1604`) unedited.

---

## Group 3 — Gate P-0 (sequential; T005 is a user gate)

### T004 — FR-060 retune probe: measure every candidate row without editing `kRows`

- **Files:** `dsp/tests/unit/systems/vorago_macro_retune_probe_test.cpp` (fill placeholder),
  log `specs/vorago-phase12-parameters/artifacts/fr060_probe.log`.
- **Test** `TEST_CASE("VoragoMacro_Phase12RetuneProbe", "[.probe][vorago]")` — hidden, prints only,
  asserts nothing except `REQUIRE(detail::isFinite(x))` on every figure.
  - Reproduce the SC-008 fixture of `vorago_macro_test.cpp` exactly (read `:544-649`, `:723-869`,
    `:1211-1330` first): seeds `{101, 202, 303}`, the 5 sweep points, 60 s at 48 kHz, window [10 s, 60 s],
    polyphony 1, `applyFastAttack` (`vorago_fixtures.h:712`), macros applied after `noteOn`, the row's
    own note (Gravity/Pressure at C1 = 36, Mass at C3), the same metric (`meanOctaveOffset` re-implemented
    locally — it is TU-local at `vorago_macro_test.cpp:728`; `crestFactorDb` `vorago_fixtures.h:316`;
    `bandEnergyDb` `:245` below 80 Hz), `spearmanRho` (`:555`), the same endpoint formula.
  - **Emulate** a candidate row at sweep point x by setting the macro itself to x (so shipped rows still
    apply) and calling `matrix.setTargetBase(target, base + amount·applyModCurve(curve, x))`; for Gravity
    rows use `g = (x − 0.5)·2`, contribution `amount·applyModCurve(curve, |g|)·sign(g)`.
  - Candidates (plan §2.6): **baseline** (no candidate) for Gravity, Pressure, Mass;
    **Mass** `→ SubToneLevelOffsetDb` base 0.0 Linear, amounts +3.0, +4.5, +6.0 dB;
    **G-a** `Gravity → BreathingDepth` base 0.30 amount −0.30 Linear; **G-b** `Gravity → ResonanceWanderRate`
    base 0.03 amount −0.028 Exponential; **G-a+G-b**;
    **P-a** `Pressure → OutputSaturation` amount 0.88 (replacing 0.35: emulate the delta +0.53) ;
    **P-b** `Pressure → SubToneLevelOffsetDb` base 0.0 amount −12 dB Linear; **P-c** `Pressure → EcologyLoopGain`
    amount 0.18 (delta +0.02); **P-d** `Pressure → CloudRichness` base 0.70 amount +0.28; and every non-empty
    subset of {P-a, P-b, P-c, P-d} (15).
  - Print per candidate: mean rho, mean endpoint, per-seed rho/endpoint, the 5 mean metric values, and
    PASS/FAIL against Phase 10's unchanged thresholds (Gravity rho ≤ −0.9 sign-adjusted and endpoint ≥ 0.30;
    Pressure rho ≥ 0.9 and endpoint ≥ 3 dB; Mass rho ≥ 0.9 and endpoint ≥ 0.25 dB).
- **Run:** BUILD `dsp_systems_tests` (zero warnings), then run **alone**, nothing else executing:
  `dsp_systems_tests.exe "VoragoMacro_Phase12RetuneProbe" > specs/vorago-phase12-parameters/artifacts/fr060_probe.log 2>&1`
  (expect tens of minutes). Read the table from the log.
- **Done when:** the log holds the full table and the baseline rows reproduce Phase 10's figures within
  the per-seed spread (Gravity endpoint ≈ 0.18014; Pressure rho ≈ −0.567, endpoint ≈ 0.0736 dB; Mass rho
  ≈ −1, endpoint ≈ −0.446 dB — `specs/vorago-phase10-voice-engine/compliance.md:20`). If the baseline does
  not reproduce, STOP: the fixture is wrong.

### T005 — Gate P-0 read-out: pick T007's path per axis from the probe (ruling R-1 already taken)

- **Action:** read `fr060_probe.log`'s table. Per axis (Gravity, Pressure): if an admissible candidate
  set PASSES Phase 10's unchanged thresholds, T007 lands exactly that set. Otherwise **spec R-1 applies**:
  T007 adds one new `VoragoMacroTarget` for that axis. No user question is asked here; the ruling is
  recorded (spec *Clarifications*, plan-stage session R-1).
- **Record** the probe figures and the chosen path per axis as a dated **Q9** entry in spec
  *Clarifications* (which candidates passed/failed, which path T007 takes). No threshold is ever moved.
- **STOP AND SURFACE only if** the baseline rows do not reproduce Phase 10's figures (fixture wrong), or
  if a new-target design cannot be predicted to reach the threshold from the probe data.
- **Does not block:** T006 (Mass) and every group from Group 5 on.

---

## Group 4 — FR-060 `kRows` retune (sequential; edits `vorago_macro_matrix.h`)

### T006 — Mass row: `Mass → SubToneLevelOffsetDb` (FR-060, SC-021 Mass)

- **Failing test first:** the existing, unedited `VoragoMacro_SweepAxes` Mass row is the failing test —
  T004's baseline row is the recorded failure (rho ≈ −1, endpoint ≈ −0.446 dB vs ≥ 0.9 / ≥ 0.25 dB).
- **Implement:** in `kRows` (`vorago_macro_matrix.h`, Mass block `:652-692`) add
  `{.macro = VoragoMacro::Mass, .owner = VoragoMacroTargetOwner::Engine, .target = VoragoMacroTarget::SubToneLevelOffsetDb, .base = 0.0f, .amount = A, .curve = ModCurve::Linear}`
  with the comment from plan §2.6 (FR-060, shares Weight's target and base 0.0 dB, contributes 0 at Mass = 0).
  **A = the smallest of {3.0, 4.5, 6.0} that T004 measured PASS**; if none passed, STOP and surface (never
  above +6 dB, never a threshold move). Update `kNumRows` 46 → 47 and its comment formula (`:254-257`,
  "4 Mass" → "5 Mass"). Base 0.0 equals Weight's base (`:487-492`) so `everyRowSharesOneBasePerTarget` holds.
- **Verify:** BUILD `dsp_systems_tests dsp_effects_tests` zero warnings; RUN `VoragoMacro_TargetBaseOverride`
  (SC-006 (1) holds against the retuned rows) and `"VoragoMacro_*" "~[long]"` pass; then **alone**:
  `dsp_systems_tests.exe "VoragoMacro_SweepAxes" > artifacts/sc021_sweepaxes_mass.log 2>&1` — the Mass
  row's `CHECK`s pass with rho ≥ 0.9 and endpoint ≥ 0.25 dB; every axis that passed in Phase 10 still
  passes; Gravity/Pressure still fail exactly as before (expected until T007). Record the Mass figures.

### T007 — Gravity / Pressure rows — two fixed paths per axis, chosen by T005

- **Precondition:** T005 has recorded Q9. **Path A (admissible set found):** land exactly that set as real
  `kRows` rows (plan §2.6 step 3), with `kNumRows` and its comment formula updated, curves
  Linear/Exponential/SCurve only, new rows carrying their target's existing base, and
  `VoragoMacroTarget::Count` untouched for that axis.
- **Path B (R-1, axis unreachable on the 39 targets):** add one new `VoragoMacroTarget` for the axis in
  its owner block (Pressure: an Engine-owned `OutputDriveDb` target whose setter replaces the
  `kOutputDriveDb` constant read, `vorago_engine.h:202`, default = that constant; Gravity: a Voice-owned
  resonance wander-depth target forwarded to the existing 1.5 st wander depth, `vorago_voice.h:575`,
  default = the shipped value — or another target the probe data predicts reaches the threshold, named in
  Q9). Requirements: base equals the shipped chain's value (SC-002 unchanged, SC-006 (1) holds); the new
  setter is `noexcept`, RT-safe, ramped/inert at default; exactly one new `kRows` row per new target,
  contributing zero at macro-neutral; `Count` 40 or 41, `kNumTargets` and every per-target table/array
  sized from `Count` (grep `kNumTargets` and the three static_asserts, `vorago_macro_matrix.h:1107`);
  **no parameter ID, no route entry** (`kMbRoutes[39]` and `kParamRoutes[108]` stay; any static_assert
  tying MB to `Count` is written against `kMbRoutes` size). Extend `VoragoMacro_TargetBaseOverride` to
  the new target(s). Failing test first: the unedited `VoragoMacro_SweepAxes` row for the axis.
- **Verify (when executable):** `VoragoMacro_SweepAxes` and `VoragoComposed_DepthMacroAxis`
  (`dsp/tests/unit/effects/vorago_composed_chain_test.cpp:623`) run alone to logs; all twelve axes pass at
  Phase 10's unchanged thresholds; SC-002 (T036) stays green.
- **Otherwise (either path fails the threshold after landing):** STOP AND SURFACE with the figures.
  Never move a threshold, never move a base.

---

## Group 5 — DSP D-2..D-5 (sequential; all edit `vorago_voice.h` / `vorago_engine.h` and the one test TU)

All new methods are `noexcept`, allocation-, lock-, exception- and I/O-free; no new include (the headers
are Layer 3 and already include what they need).

### T008 — Seven `VoragoVoice` forwarders (FR-004)

- **Files:** `dsp/include/krate/dsp/systems/vorago_voice.h`, `dsp/tests/unit/systems/vorago_param_surface_test.cpp`.
- **Failing test first** — `TEST_CASE("VoragoVoice_Phase12Forwarders", "[systems][vorago]")` on a prepared
  voice (48 kHz, default `VoragoVoiceConfig`):
  - `setCloudSpectralGravity(-0.5f)` → `cloud().getSpectralGravity() == -0.5f`; NaN (bit pattern) → still −0.5f.
  - `setNoiseSourceModel(2, NoiseOrganismModel::MetallicHiss)`, render ≥ 4 800 samples with a held note →
    `noise().getSourceModel(2) == MetallicHiss`. Slot 4 (out of range) → no crash, nothing changes.
  - `setNoiseSourceType(2, NoiseType::Pink)` on a slot whose model is `Direct` → after 4 800 samples
    `noise().getSourceNoiseType(2) == Pink`.
  - `setNoiseCombTuning(0, 440.0f, 0.6f)` → `getCombFundamental(0) == 440.0f`, `getCombSpread(0) == 0.6f`;
    then `setNoiseCombTuning(0, NaN, 0.2f)` and `(0, 200.0f, NaN)` → both getters unchanged (440 / 0.6,
    **not** the organism's 60 / 0.35 defaults).
  - `setNoiseCombFeedback(1, 0.3f)` → `getCombFeedback(1) == 0.3f`; NaN → still 0.3f.
  - **Latch (C-4):** fresh prepared voice; `setNoiseCombFeedback(3, 0.75f)` (the value slot 3 already runs);
    then `setNoiseSourceModel(3, Direct)`, render 4 800 samples → `getCombFeedback(3) == 0.75f`.
  - `setResonanceAnchorMode(Keyed)` → `resonance().getAnchorMode() == Keyed`.
  - `setEcologyLoopFilterMode(4, Highpass)` → `ecology().getLoopFilterMode(4) == Highpass`; loop 6 → no-op.
  - **Early-out render identity:** two identically prepared voices with a held note, 1 s; voice B calls
    each forwarder with the value the voice already holds (anchor `Hybrid`, loop modes `Lowpass`, comb
    tuning 60/0.35 twice, spectral gravity 0.10) every 512 samples; max-abs difference ≤ 1e-6 per channel.
- **Run:** BUILD `dsp_systems_tests` — fails to compile. Record.
- **Implement** (plan §2.3 table), after `setStereoSpread` (`vorago_voice.h:1144`), before the
  sub-component read-access block: bare forwards for spectral gravity, source model, source type; comb
  tuning rejects non-finite args and early-outs on `combTuningSet_[slot] && hz == combHzReq_[slot] && spread == combSpreadReq_[slot]`;
  comb feedback rejects non-finite and early-outs only if `combFeedbackLatched_[slot] && std::clamp(fb, 0.0f, NoiseOrganism::kCombFeedbackCap) == noise_.getCombFeedback(slot)`,
  then sets `combFeedbackLatched_[slot] = true`; anchor mode early-outs on `m == resonance_.getAnchorMode()`;
  loop filter mode rejects `loop >= FeedbackEcology::kMaxLoops` and early-outs on unchanged. Four private
  per-slot arrays (`combTuningSet_`, `combHzReq_`, `combSpreadReq_`, `combFeedbackLatched_`), all cleared in
  `prepare()` step 5 right after `noise_.setNumSources` (`:557`). No getter added.
- **Verify:** BUILD zero warnings (the `kVoiceSizeBound` static_assert must still hold); RUN
  `VoragoVoice_Phase12Forwarders` and `"VoragoVoice_*" "~[long]"` pass.

### T009 — `VoragoVoiceParams` POD + `VoragoEngine::applyVoiceParams` (FR-003, SC-019 part 1)

- **Files:** `dsp/include/krate/dsp/systems/vorago_engine.h`, `dsp/tests/unit/systems/vorago_param_surface_test.cpp`.
- **Failing test first:**
  - `TEST_CASE("VoragoEngine_ApplyVoiceParamsDefaultIsNoOp_Short", "[systems][vorago]")`: two engines
    from `makeEngine` (48 kHz), polyphony 4, seed 1, note 36 velocity 100 at sample 0; engine B calls
    `applyVoiceParams(VoragoVoiceParams{})` after prepare; render 4 s (`renderEngine`,
    `vorago_fixtures.h:750`) → max-abs ≤ 1e-6 per channel. Also `STATIC_REQUIRE(std::is_trivially_copyable_v<VoragoVoiceParams>)`,
    `VoragoVoiceParams::kFieldCount == 31`, and every default field equals the voice's prepare-step-5 value
    read from engine A's `getVoice(i)` for **every** `i < VoragoEngine::kMaxVoices` (spread 0.45, spectral
    gravity 0.10, materials StoneChamber/SteelTank, models FilteredWind/GranularDust/Direct/MetallicHiss,
    comb 60 Hz / 0.35, feedback 0.55/0.55/0.55/0.75, anchor Hybrid, six loops Lowpass).
  - `TEST_CASE("VoragoEngine_ApplyVoiceParamsDefaultIsNoOp", "[systems][vorago][long]")`: the same, 60 s.
  - `TEST_CASE("VoragoEngine_ApplyVoiceParamsReachesAllSlots", "[systems][vorago]")`: polyphony 2; a
    non-default `p` (every field changed) → every getter on all six slots (not only `i < 2`) equals `p`
    after 4 800 samples with two held notes.
- **Implement** (plan §2.2): `struct VoragoVoiceParams` after `VoragoEngineConfig` (`vorago_engine.h:141`),
  initializers from plan §2.2 (NoiseType defaults `Brown`; comb feedback via
  `NoiseOrganism::kDefaultCombFeedback` / `kMetallicCombFeedback`; loop modes six `Lowpass` written out
  explicitly — no narrowing brace init), no field named after a `VoragoMacroTarget` (doc-comment states the
  rule), `kFieldCount = 31`, two `static_assert`s. `applyVoiceParams` loops `v < kMaxVoices` (comment: the
  orphan-tail reason) calling the T008 forwarders plus `setStereoSpread`, `setBodyMaterialA/B`.
- **Verify:** BUILD zero warnings (`kEngineSizeBound` holds); RUN the two untagged cases; RUN the `[long]`
  case alone.

### T010 — Per-tone sub level base `setSubToneLevelDb` / `getSubToneLevelDb` (FR-005)

- **Files:** `vorago_engine.h`, `vorago_param_surface_test.cpp`.
- **Failing test first:**
  - `TEST_CASE("VoragoEngine_SubToneLevelBase", "[systems][vorago]")`: prepared engine;
    `setSubToneLevelDb(0, -12.0f)` → `getSubToneLevelDb(0) == -12.0f` and
    `subharmonic().getToneLevelDb(0) == -12.0f` (offset 0); tones 1, 2 still −24 / −30; with the matrix
    offset `setSubToneLevelOffsetDb(+3.0f)` → tone 0 reads −9.0f; `setSubToneLevelDb(3, …)` and NaN/±Inf
    (bit patterns) → no change; setting −12.0f again is an early-out (tone 1's in-flight ramp: call
    `setSubToneLevelDb(1, -20.0f)`, render 64 samples, record tone-1 current level, call
    `setSubToneLevelDb(0, -12.0f)` again, render 64 → tone-1 trajectory identical to a control engine
    without the repeat call, max-abs output diff ≤ 1e-6).
  - First half of `TEST_CASE("VoragoEngine_NewSettersDefaultInert_Short", "[systems][vorago]")`: 4 s,
    `setSubToneLevelDb(t, SubharmonicEngine::kDefaultToneLevelDb[t])` ×3 on B → max-abs ≤ 1e-6.
- **Implement** (plan §2.4): field `std::array<float, SubharmonicEngine::kNumTones> subToneBaseDb_ = SubharmonicEngine::kDefaultToneLevelDb;`;
  setter rejects `tone >= kNumTones` / non-finite, early-outs on equal, writes only tone t
  (`sub_.setToneLevelDb(tone, base + subToneOffsetDb_)`); `applySubToneLevels()` (`:1154-1158`) reads
  `subToneBaseDb_[t]` instead of the constant.
- **Verify:** BUILD zero warnings; RUN both cases; `"VoragoEngine_*" "~[long]~[.perf]"` passes (Phase 6/10
  sub tests unedited).

### T011 — Ghost setters that survive re-prepare (FR-006, SC-022, SC-019 part 2)

- **Files:** `vorago_engine.h`, `vorago_param_surface_test.cpp`.
- **Failing test first:**
  - Extend `VoragoEngine_NewSettersDefaultInert_Short` to also call `setGhostReverseProbability(0.0f)`,
    `setGhostEventTriggers(false)`; 4 s ≤ 1e-6. Add `TEST_CASE("VoragoEngine_NewSettersDefaultInert", "[systems][vorago][long]")`:
    the same, 60 s.
  - `TEST_CASE("VoragoEngine_NewSettersSurvivePrepare", "[systems][vorago]")`:
    - SECTION `"SurvivesPrepare"`: set tones −12 / −20 / −36 dB, `setGhostReverseProbability(0.4f)`,
      `setGhostEventTriggers(true)`; `prepare()` again with the same config; render one 512-sample block;
      `getSubToneLevelDb(t)` and `subharmonic().getToneLevelDb(t)` == set values (exact);
      `getGhostReverseProbability() == 0.4f` and `atmosphere().getGrainReverseProbability() == 0.4f`;
      `getGhostEventTriggers() == true`. NaN reverse probability → unchanged; 1.7f → 1.0f (clamped).
    - SECTION `"DisarmAndRearm"`: triggers on, `setGhostPeakLevel(1.0f)`, fast-attack held note
      (`applyFastAttack`), event rate raised through a matrix override (`VoragoMacroMatrix m;
      m.setTargetBase(VoragoMacroTarget::EventRateScale, 10.0f); m.apply(engine);`); render in 512 blocks
      up to 120 s until `isGhostTriggerLatchHigh()` — `REQUIRE` it became true; `setGhostEventTriggers(false)`,
      render one block → latch false; `setGhostEventTriggers(true)` with the request still high, render
      64 samples → latch true.
- **Implement** (plan §2.5): `setGhostReverseProbability` (reject non-finite, clamp [0,1], set
  `ghostReverseSet_`, forward to `atmos_.setGrainReverseProbability`), getter reading the atmosphere,
  `setGhostEventTriggers` (on→off clears `ghostTriggerHigh_`; sets `ghostTriggersSet_`), getter,
  `isGhostTriggerLatchHigh()`. `prepare()` edits only the two lines `:290` and `:323` to use the config
  while the set-flag is false; `ghostTriggerHigh_ = false` at `:291` stays. New fields beside `:1568`.
- **Verify:** BUILD zero warnings; RUN new untagged cases; `vorago_ghost_ext_test.cpp` cases
  (`"VoragoEngine_GhostExtensionWiring*"`, `:415`, `:652`) pass unedited; RUN the `[long]` case alone.

### T012 — SC-023 (1) repeated broadcast is inert

- **Files:** `vorago_param_surface_test.cpp` only (implementation exists after T008–T011).
- **Test:** `TEST_CASE("VoragoEngine_RepeatedBroadcastIsInert", "[systems][vorago][long]")`: polyphony 6,
  six held notes (36, 43, 48, 55, 60, 67; velocity 100), 10 s at 48 kHz. Arm A: non-default `p` (every
  field changed, comb fundamental 440 Hz) + `setSubToneLevelDb(t, v_t)` (−12/−20/−36) called once.
  Arm B: identical calls repeated every 512-sample block. `REQUIRE(maxAbsDiff ≤ 1e-5)` per channel.
  Positive control arm C: as A but slot 2's model written alternately `Direct` / `FilteredWind` every
  block → `rmsDiff(A, C) > 1e-3`. Add an untagged 2 s twin `VoragoEngine_RepeatedBroadcastIsInert_Short`
  with the same bounds.
- **Verify:** BUILD; RUN the short twin; RUN the `[long]` case alone. If arm B diverges, the defect is a
  missing early-out in T008/T010 — fix there, never loosen 1e-5.

### T013 — DSP closure (FR-007, SC-019 "unedited" clause)

- **Steps:** BUILD `dsp_systems_tests dsp_effects_tests` zero warnings. Run
  `dsp_systems_tests.exe "~[.perf]~[performance]~[long]~[.probe]"` and `dsp_effects_tests.exe "~[.perf]~[performance]~[long]"`
  → all pass. `git diff --stat f149cced -- dsp/` lists only `vorago_macro_matrix.h`, `vorago_engine.h`,
  `vorago_voice.h`, `dsp/tests/CMakeLists.txt` and the two new TUs; `git diff f149cced -- dsp/tests/unit/`
  shows no change to any pre-existing TU. Save both outputs to `artifacts/dsp_closure.log`.
  Grep `-rn "std::isnan" ` in the three edited headers and new TUs → 0 hits.

---

## Group 6 — Plugin foundations (sequential)

### T014 — Parameter ID map (FR-010, C-5)

- **Files:** `plugins/vorago/src/plugin_ids.h`, `plugins/vorago/tests/unit/param_table_test.cpp`.
- **ODR/near-name sweep:** for every new enumerator run `grep -rn "\b<name>\b" dsp/ plugins/`; hits allowed
  only in `namespace Seraphis` (record them).
- **Failing test first** — `TEST_CASE("Vorago_ParamIdMap", "[vorago][params]")`: `STATIC_REQUIRE` /
  `REQUIRE` every ID value of C-5: `kSeedId == 2`, `kOutputSaturationId == 3`, `kSustainPedalId == 4`,
  `kChannelPressureId == 5`; Cloud `kCloudRichnessId 200, kCloudTiltId 201, kCloudMutationId 202,
  kCloudInharmonicityId 203, kCloudDriftDepthId 204, kCloudStereoSpreadId 205, kCloudSpectralGravityId 206`;
  Noise `kNoiseLevelId 300, kNoiseWakeId 301, kNoiseWanderRateId 302, kNoiseSlot{0..3}ModelId 310..313,
  kNoiseSlot{0..3}TypeId 320..323, kNoiseSlot{0..3}CombFundamentalId 330..333,
  kNoiseSlot{0..3}CombSpreadId 340..343, kNoiseSlot{0..3}CombFeedbackId 350..353`; Resonance
  `kResonanceGravityId 400, kResonanceMixId 401, kResonanceWanderRateId 402, kResonanceAnchorModeId 403`;
  Ecology `kEcologyMixId 500, kEcologyLoopGainId 501, kEcologyLoop{0..5}FilterModeId 510..515`; Sub
  `kSubLevelOffsetId 600, kSubTrackingId 601, kSubDiv2LevelId 610, kSubDiv4LevelId 611, kSubFifthBelowLevelId 612`;
  Smear `kSmearAmountId 700, kSmearDecoherenceId 701, kSmearTiltId 702`; `kEventsRateScaleId 800`;
  `kEcosystemDepthId 900`; Body `kBodyBlendId 1000, kBodyDampingId 1001, kBodyResonanceId 1002,
  kBodyMixId 1003, kBodyMaterialAId 1004, kBodyMaterialBId 1005`; Space `kSpaceSizeId 1100,
  kSpaceDarknessId 1101, kSpaceDecayId 1102, kSpaceFogId 1103, kSpaceDamperDepthId 1104, kSpaceMixId 1105,
  kSpaceWidthId 1106, kSpaceDensityId 1107, kSpaceDimensionalityId 1108, kSpaceBreathId 1109,
  kSpaceEarlySizeId 1110, kSpaceEarlyLevelId 1111, kSpaceEarlyAbsorptionId 1112, kSpaceEarlySendId 1113,
  kSpaceDamperRateId 1114, kSpaceFreezeId 1115`; Envelope `kEnvelopeModeId 1200,
  kEnvelopeStage{0..3}TimeId 1201..1204, kEnvelopeReleaseId 1205, kEnvelopeGrowthDurationId 1206`; Bloom
  `kBloomDepthId 1300, kBloomSpawnRateId 1301`; Ghost `kGhostPeakLevelId 1400, kGhostBlurId 1401,
  kGhostReverseProbabilityId 1402, kGhostEventTriggersId 1403`; Life `kLifeBreathingDepthId 1500,
  kLifeBreathingIrregularityId 1501, kLifeTidalDepthId 1502`. Range ends: `kGlobalParamRangeEnd 100`,
  `kMacroParamRangeEnd 200`, `kCloudParamRangeEnd 300`, `kNoiseParamRangeEnd 400`, `kResonanceParamRangeEnd 500`,
  `kEcologyParamRangeEnd 600`, `kSubParamRangeEnd 700`, `kSmearParamRangeEnd 800`, `kEventsParamRangeEnd 900`,
  `kEcosystemParamRangeEnd 1000`, `kBodyParamRangeEnd 1100`, `kSpaceParamRangeEnd 1200`,
  `kEnvelopeParamRangeEnd 1300`, `kBloomParamRangeEnd 1400`, `kGhostParamRangeEnd 1500`,
  `kLifeParamRangeEnd 1600`. `kCurrentStateVersion` is **still 1** here (bumped by T042).
- **Implement:** add the enumerators and range-end constants to `plugin_ids.h`; rewrite the reserved-map
  comment (`:46-51`): bands 1200–1599 claimed (Envelope/Bloom/Ghost/Life), 1600+ unassigned, macros LIVE.
- **Verify:** BUILD `vorago_tests Vorago` zero warnings; RUN `Vorago_ParamIdMap`; full
  `vorago_tests.exe "~[.perf]~[performance]~[long]"` still green (the 14 Phase 11 IDs untouched).

### T015 — Mapping functions, seed table, noise-type list, cavern seed (plan §3.3, C-8)

- **Files:** `plugins/vorago/src/parameters/param_mapping.h` (fill placeholder),
  `plugins/vorago/src/engine/vorago_engine_config.h`, `plugins/vorago/tests/unit/param_table_test.cpp`.
- **Failing test first** — `TEST_CASE("Vorago_ParamMapping", "[vorago][params]")`:
  - Linear: `linearFromNormalized(0.5, -12, 12) == 0.0`; `linearToNormalized(-4, -12, 12) == Approx(1.0/3).epsilon(1e-12)`;
    inputs −0.5 / 1.5 clamp to the ends.
  - Offset-log (plan §3.3.1), relative 1e-6: `offsetLogFromNormalized(0.5, 0, 120000, 10) == 1085.49 ms`
    (±0.01), n = 0 → 0 exactly, n = 1 → 120000 exactly; `(0.5, 0, 0.05, 1e-4) == 0.002138` (±1e-6);
    `(0.5, 0, 1, 0.01) == 0.090499` (±1e-6). Inverses: `offsetLogToNormalized(20000, 0, 120000, 10) == 0.809284413`,
    30000 → 0.852434578, 45000 → 0.895590654, 60000 → 0.926212855 (1e-9);
    `offsetLogToNormalized(1.0/240, 0, 0.05, 1e-4) == 0.603772849`; `(0.15, 0, 1, 0.01) == 0.600761933`.
  - Log (`Krate::Plugins::logMapFromNormalized`): n = 0.5 midpoints 1.0 Hz on [0.01, 100]; 630.0 Hz on
    [20, 19845] (±0.05); 0.044721 Hz on [0.002, 1]; 1.0 on [0.1, 10]; 5.4772 s on [0.5, 60];
    154.919 ms on [80, 300]; 10.9545 s on [1, 120] (all ±1e-4 relative). Inverses: 60 Hz on [20, 19845]
    → 0.159219747; 0.03 on [0.01,100] → 0.119280314; 0.03 on [0.002,1] → 0.435755587; 20 s on [0.5,60]
    → 0.770524453; 220 ms on [80,300] → 0.765346277 (1e-9).
  - Discrete: `indexFromNormalized(5.0/11, 12) == 5`; `indexToNormalized(6, 11) == 0.6`; out-of-range
    clamps.
  - Seeds: `kVoragoSeedValues.size() == 16`, `[0] == 1u`, all 16 distinct, `kCavernSeedSalt == 0x43415645u`,
    `cavernSeedFor(0) == 1u`, `cavernSeedFor(i) == (kVoragoSeedValues[i] ^ 0x43415645u)` for i = 1..15.
  - Noise types: `kNoiseTypeByIndex.size() == 12`, `[5] == NoiseType::Brown`, `[11] == NoiseType::RadioStatic`,
    indices 0–10 equal enum values 0–10, no entry equals `ModulationNoise`; `noiseTypeToIndex(ModulationNoise) == 2`.
  - `makeVoragoCavernConfig(2048).seed == kCavernSeed` and `makeVoragoCavernConfig(2048, 77u).seed == 77u`.
- **Implement** (plan §3.3): `namespace Vorago`, `enum class Taper`, the six mapping functions (double,
  clamp in/out, log via `Krate::Plugins::logMapFromNormalized`/`logMapToNormalized`), `kVoragoSeedValues`
  exactly as plan §3.3.2 with the header comment recording the one-off Node splitmix64 generator
  (seed `0x5641524F474F3132`), `kCavernSeedSalt`, `constexpr cavernSeedFor`, `kNoiseTypeByIndex` +
  `noiseTypeToIndex` with `static_assert`s. In `vorago_engine_config.h` change
  `makeVoragoCavernConfig(maxBlock)` to `makeVoragoCavernConfig(maxBlock, std::uint32_t seed = kCavernSeed)`
  (existing callers unchanged).
- **Verify:** BUILD `vorago_tests Vorago` zero warnings; RUN `Vorago_ParamMapping`; suite still green.

---

## Group 7 — Parameter packs (all `[P]`; each task owns one new header + one new test TU)

**Shared contract for T016–T029** (plan §3.4, FR-011/FR-012/FR-013; shape of `plugins/vorago/src/parameters/global_params.h:31-159`):
struct `Vorago::<Section>Params` of `std::atomic<float>` (continuous, **plain units**) and `std::atomic<int>`
(discrete indices), explicit initializers equal to the default column; `handle<Section>ParamChange(Params&, ParamID, ParamValue)`
(`switch` with `default: break`; denormalize via T015's functions; relaxed store; caller has already
rejected non-finite and clamped [0,1]); `register<Section>Params(ParameterContainer&)` (continuous:
`addParameter(title, units, 0, n₀, kCanAutomate, id)`; discrete: `Krate::Plugins::createDropdownParameterWithDefault`
**and** `getInfo().defaultNormalizedValue = n₀` — the P-1 trap `global_params.h:82-84`);
`format<Section>Param(id, value, String128)` (continuous only; `kResultFalse` for unknown ids);
`save<Section>Params(const Params&, IBStreamer&)` in ascending ID order (float → `writeFloat`, index →
`writeInt32`); `load<Section>Params(Params&, IBStreamer&) → bool` (stop at the first failed read and return
false, later fields unchanged; skip non-finite floats via `Krate::DSP::detail::isFinite`; clamp finite
floats to the plain range; clamp indices to `[0, n−1]`); `template <typename F> load<Section>ParamsToController(IBStreamer&, F setParam)`
(same read order, calls `setParam(id, normalized)` via the inverse map). Plain-range clamps are the
destination setter clamps listed in plan §3.2. Titles are ASCII only (no `÷`; MSVC/Clang source-charset
portability). Units: `%` for [0,1] amounts, `dB`, `Hz`, `ms`, `s`, `x`, `ct`, `dB/oct`, empty otherwise.

**Shared test shape** — each task's `TEST_CASE("Vorago_<Section>ParamsContract", "[vorago][params]")` in
`plugins/vorago/tests/unit/params/<section>_params_test.cpp` (fast-math-off TU), SECTIONs:
1. `"Registration"`: `ParameterContainer pc; register…(pc);` → `pc.getParameterCount() == N`; per ID:
   title, units, `stepCount` (continuous 0; list n−1), flags (`kCanAutomate`, plus `kIsList` for lists),
   `defaultNormalizedValue == Approx(n₀).margin(1e-9)`.
2. `"DefaultsDenormalizeToPlain"`: fresh struct vs `handle…(p, id, n₀)` for every ID → atomic equals the
   default plain value (continuous: relative 1e-6; discrete: exact); n = 0 and n = 1 → range ends.
3. `"UnregisteredInBandIgnored"`: every unused ID in the band (e.g. band start + gaps + band end − 1) leaves
   every atomic bit-identical (snapshot `std::bit_cast<std::uint32_t>`).
4. `"SaveLoadRoundTrip"`: set every field to a non-default in-range value, save → byte count == B; load
   into a fresh struct → every atomic bit-identical.
5. `"TruncationEveryOffset"`: for every cut c in 0..B−1 load the first c bytes into a pre-dirtied struct →
   `load` returns false, fields fully contained in [0, c) restored, the rest unchanged; no crash.
6. `"NonFiniteAndClamp"`: NaN/+Inf/−Inf (bit patterns via `volatile` + `memcpy`) written into each float
   field of the stream in turn → that field unchanged, later fields loaded; finite out-of-range floats
   clamp to range; index −1 / n clamp to 0 / n−1.
7. `"ControllerMirror"`: `load…ParamsToController` on the SECTION 4 stream → every normalized value equals
   the inverse map of the saved plain value within 1e-9.
8. `"FormatNonEmpty"`: `format…` at 0, 0.5, 1 for every continuous ID → `kResultOk`, non-empty string.

Verify for every pack task: BUILD `vorago_tests` zero warnings; RUN `"Vorago_<Section>ParamsContract"`.
Failing-first: the TU is written against the not-yet-written header and fails to compile — record once.

### T016 [P] — Cloud pack (`cloud_params.h`, `unit/params/cloud_params_test.cpp`) — N = 7, B = 28

| ID | Title | Units | Range | Default | n₀ | Taper |
|---|---|---|---|---|---|---|
| 200 | Cloud Richness | % | [0, 1] | 0.70 | 0.70 | lin |
| 201 | Cloud Tilt | dB/oct | [−12, 12] | −4.0 | 0.333333333 | lin |
| 202 | Cloud Mutation | % | [0, 1] | 0.15 | 0.15 | lin |
| 203 | Cloud Inharmonicity | | [0, 0.1] | 0.015 | 0.15 | lin |
| 204 | Cloud Drift Depth | ct | [0, 50] | 8.0 | 0.16 | lin |
| 205 | Cloud Stereo Spread | % | [0, 1] | 0.45 | 0.45 | lin |
| 206 | Cloud Spectral Gravity | | [−1, 1] | 0.10 | 0.55 | lin |

ODR: `Vorago::CloudParams` near-name `Seraphis::CloudParams` (different namespace) — record; no TU may
`using namespace` both.

### T017 [P] — Noise pack (`noise_params.h`, `unit/params/noise_params_test.cpp`) — N = 23, B = 92

| ID | Title | Units | Type / range | Default | n₀ | Taper |
|---|---|---|---|---|---|---|
| 300 | Noise Level | dB | [−96, 12] | −18 | 0.722222222 | lin |
| 301 | Noise Wake | % | [0, 1] | 0.35 | 0.35 | lin |
| 302 | Noise Wander Rate | Hz | [0.01, 100] | 0.03 | 0.119280314 | log |
| 310–313 | Noise Slot 1..4 Model | | L(4) `Direct, Filtered Wind, Granular Dust, Metallic Hiss` (index == `NoiseOrganismModel`, `noise_organism.h:126-131`) | 1, 2, 0, 3 | 1/3, 2/3, 0, 1 | — |
| 320–323 | Noise Slot 1..4 Type | | L(12) `White, Pink, Tape Hiss, Vinyl Crackle, Asperity, Brown, Blue, Violet, Grey, Velvet, Vinyl Rumble, Radio Static` (via `kNoiseTypeByIndex`) | 5 (Brown) | 5/11 | — |
| 330–333 | Noise Slot 1..4 Comb Freq | Hz | [20, 19845] | 60 | 0.159219747 | log |
| 340–343 | Noise Slot 1..4 Comb Spread | % | [0, 1] | 0.35 | 0.35 | lin |
| 350–353 | Noise Slot 1..4 Comb Feedback | % | [0, 0.9] | 0.55, 0.55, 0.55, 0.75 | 0.611111111 ×3, 0.833333333 | lin |

Stream field order = ascending ID (3F, 4I model, 4I type, 4F fund, 4F spread, 4F feedback).

### T018 [P] — Resonance pack (`resonance_params.h`, `unit/params/resonance_params_test.cpp`) — N = 4, B = 16

400 Resonance Gravity, "", [−1, 1], 0.0, n₀ 0.5, lin · 401 Resonance Mix, %, [0, 1], 0.45, 0.45, lin ·
402 Resonance Wander Rate, Hz, [0.002, 1], 0.03, 0.435755587, log · 403 Resonance Anchor, L(3)
`Free, Keyed, Hybrid` (index == `AnchorMode`, `resonance_drift_network.h:293`), default 2, n₀ 1.0.

### T019 [P] — Ecology pack (`ecology_params.h`, `unit/params/ecology_params_test.cpp`) — N = 8, B = 32

500 Ecology Mix, %, [0, 1], 0.15, 0.15, lin · 501 Ecology Loop Gain, %, [0, 0.9], 0.72, 0.8, lin ·
510–515 Ecology Loop 1..6 Filter, L(3) `Lowpass, Bandpass, Highpass` (index == `FilterMode`,
`feedback_ecology.h:597`), default 0, n₀ 0.0.

### T020 [P] — Sub pack (`sub_params.h`, `unit/params/sub_params_test.cpp`) — N = 5, B = 20

600 Sub Level Offset, dB, [−24, 24] (plan decision D-P3; the engine setter has no clamp), 0.0, 0.5, lin ·
601 Sub Tracking, %, [0, 1], 1.0, 1.0, lin · 610 Sub Div2 Level, dB, [−60, 6], −18, 0.636363636, lin ·
611 Sub Div4 Level, dB, [−60, 6], −24, 0.545454545, lin · 612 Sub Fifth-Below Level, dB, [−60, 6], −30,
0.454545455, lin.

### T021 [P] — Smear pack (`smear_params.h`, `unit/params/smear_params_test.cpp`) — N = 3, B = 12

700 Smear Amount, %, [0, 1], 0.20, 0.20, lin · 701 Smear Decoherence, %, [0, 1], 0.20, 0.20, lin ·
702 Smear Tilt, "", [−1, 1], 0.0, 0.5, lin.

### T022 [P] — Events pack (`events_params.h`, `unit/params/events_params_test.cpp`) — N = 1, B = 4

800 Event Rate, x, [0.1, 10], 1.0, n₀ **0.5** (exact — log midpoint), log.

### T023 [P] — Ecosystem pack (`ecosystem_params.h`, `unit/params/ecosystem_params_test.cpp`) — N = 1, B = 4

900 Ecosystem Depth, %, [0, 1], 0.85, 0.85, lin.

### T024 [P] — Body pack (`body_params.h`, `unit/params/body_params_test.cpp`) — N = 6, B = 24

1000 Body Blend 0.35 · 1001 Body Damping 0.25 · 1002 Body Resonance 0.70 · 1003 Body Mix 1.00 — all %,
[0, 1], n₀ = default, lin · 1004 Body Material A, 1005 Body Material B: L(11) in `ContinuousBody::BodyMaterial`
declaration order (`continuous_body.h:84-97`: `Glass, Strings, Metal Plate, Chamber, Ice, Stone Chamber,
Steel Tank, Wooden Hull, Cathedral Column, Cavern Wall, Glass Sphere`), index == enum value, no mapping
table; defaults 5 (n₀ 0.5) and 6 (n₀ 0.6). `static_assert(ContinuousBody::kNumMaterials == 11)`,
`static_cast<int>(StoneChamber) == 5`, `SteelTank == 6` in the header. Extra test assertion (SC-018 Q5):
for n in 0..10, `handle…(p, kBodyMaterialAId, n/10.0)` stores index n and `static_cast<BodyMaterial>(n)`
round-trips. ODR near-name `Seraphis::BodyParams` — record.

### T025 [P] — Space pack (`space_params.h`, `unit/params/space_params_test.cpp`) — N = 16, B = 64

1100 Space Size 0.50 · 1101 Space Darkness 0.80 · 1103 Space Fog 0.30 · 1104 Space Damper Depth 0.35 ·
1105 Space Mix 1.00 · 1106 Space Width 1.00 · 1107 Space Density 0.75 · 1108 Space Dimensionality 0.50 ·
1109 Space Breath 0.50 · 1111 Space Early Level 0.80 · 1112 Space Early Absorption 0.60 · 1113 Space Early Send
0.70 — all %, [0, 1], n₀ = default, lin · 1102 Space Decay, s, [0.5, 60], 20.0, 0.770524453, log ·
1110 Space Early Size, ms, [80, 300] (plan D-P5), 220, 0.765346277, log · 1114 Space Damper Rate, "",
[0, 1], 0.15, 0.600761933, olog ε = 0.01 (plan D-P4) · 1115 Space Freeze, L(2) `Off, On`, 0, 0.0 (int32 0/1).
Test additionally pins 1107–1114 defaults to `CavernVerb::kDefault*` (`cavern_verb.h:221, 247-265`).

### T026 [P] — Envelope pack (`envelope_params.h`, `unit/params/envelope_params_test.cpp`) — N = 7, B = 28

1200 Envelope Mode, L(2) `Standard, Growth`, 0, 0.0 · 1201–1204 Envelope Stage 1..4 Time, ms, [0, 120000],
20000 / 30000 / 45000 / 60000, n₀ 0.809284413 / 0.852434578 / 0.895590654 / 0.926212855, olog ε = 10 ms ·
1205 Envelope Release, ms, [0, 120000], 45000, 0.895590654, olog ε = 10 ms · 1206 Envelope Growth Duration,
s, [1, 120], 120, 1.0, log. Test pins defaults to `VoragoVoice::kDefaultStageTimesMs[0..3]`,
`kDefaultReleaseMs`, `kDefaultGrowthDurationSeconds` (`vorago_voice.h:322-333`).

### T027 [P] — Bloom pack (`bloom_params.h`, `unit/params/bloom_params_test.cpp`) — N = 2, B = 8

1300 Bloom Depth, %, [0, 1], 0.60, 0.60, lin · 1301 Bloom Spawn Rate, Hz, [0, 0.05], 1/240,
0.603772849, olog ε = 1e-4 Hz. Test: n = 0 → exactly 0 Hz (no `log(0)` evaluated).

### T028 [P] — Ghost pack (`ghost_params.h`, `unit/params/ghost_params_test.cpp`) — N = 4, B = 16

1400 Ghost Peak Level, %, [0, 1], 0.60, 0.60, lin · 1401 Ghost Blur, %, [0, 1], 0.85, 0.85, lin ·
1402 Ghost Reverse Probability, %, [0, 1], 0.0, 0.0, lin · 1403 Ghost Event Triggers, L(2) `Off, On`, 0, 0.0.

### T029 [P] — Life pack (`life_params.h`, `unit/params/life_params_test.cpp`) — N = 3, B = 12

1500 Breathing Depth 0.30 · 1501 Breathing Irregularity 0.30 · 1502 Tidal Depth 0.40 — %, [0, 1],
n₀ = default, lin.

---

## Group 8 — Global pack extension (sequential; edits existing `global_params.h`)

### T030 — Global extension: seed, output saturation, sustain pedal, channel pressure

- **Files:** `plugins/vorago/src/parameters/global_params.h`, `plugins/vorago/tests/unit/params/global_ext_params_test.cpp`,
  `plugins/vorago/tests/unit/param_denorm_test.cpp` (count update only).
- **Failing test first** — `TEST_CASE("Vorago_GlobalExtParamsContract", "[vorago][params]")` with the
  Group 7 SECTION shape for the four new IDs: 2 Seed, L(16) `"Seed 1"…"Seed 16"`, default 0, n₀ 0.0,
  stepCount 15; 3 Output Saturation, %, [0, 1], 0.12, 0.12, lin; 4 Sustain Pedal, R, flags
  `kCanAutomate | kIsHidden`, [0, 1], 0, 0.0; 5 Channel Pressure, %, R, `kCanAutomate | kIsHidden`, [0, 1],
  0, 0.0. `registerGlobalParams` now yields 6 parameters. `saveGlobalParamsV2Ext` writes exactly 8 bytes
  (int32 seedIndex, float outputSaturation) — sustain and pressure are **never** written;
  `loadGlobalParamsV2Ext` is EOF-safe/NaN-safe/clamped per the shared shape; the v1 `saveGlobalParams`
  still writes exactly 8 bytes (masterGain, polyphony) — assert byte-for-byte equal to a stream built by
  hand with `writeFloat(1.0f); writeInt32(4)` at defaults.
- **Implement:** add the four atomics (`seedIndex{0}` int, `outputSaturation{0.12f}`, `sustainPedal{0.0f}`,
  `channelPressure{0.0f}`), extend handle/register/format; add `saveGlobalParamsV2Ext` /
  `loadGlobalParamsV2Ext` / `loadGlobalParamsV2ExtToController`; the v1 functions are unchanged.
- **Update Phase 11 test:** `unit/param_denorm_test.cpp:127` — `registerGlobalParams(pc); registerMacroParams(pc);`
  count `14` → `18` (the controller count at `:225` changes in T033, not here).
- **Verify:** BUILD `vorago_tests Vorago` zero warnings; RUN `Vorago_GlobalExtParamsContract`,
  `Vorago_ParamDenormRoundTrip`, `Vorago_StateRoundTrip` pass.

---

## Group 9 — Route table and sustain latch (`[P]`; disjoint files)

### T031 [P] — `param_routes.h`: `Route`, `kParamRoutes[108]`, `routeOf`, `kMbRoutes[39]` (C-2, plan §4.1)

- **Files:** `plugins/vorago/src/parameters/param_routes.h` (fill placeholder), `plugins/vorago/tests/unit/param_table_test.cpp`.
- **Failing test first** — `TEST_CASE("Vorago_RouteTable", "[vorago][params]")`: `kParamRoutes.size() == 108`;
  IDs strictly ascending; per-route counts MB 39, VP 31, ENG 14, CV 9, MAC 13, Local 2;
  `routeOf(kMasterGainId) == Route::Local`, `routeOf(kSustainPedalId) == Local`, `routeOf(kChannelPressureId) == MAC`,
  `routeOf(kOutputSaturationId) == MB`, `routeOf(kSeedId) == ENG`, `routeOf(kPolyphonyId) == ENG`,
  `routeOf(kCloudStereoSpreadId) == VP`, `routeOf(kSpaceFreezeId) == CV`, `routeOf(kGhostReverseProbabilityId) == ENG`,
  `routeOf(100..111) == MAC`; `routeOf(207)`, `routeOf(1600)` → `std::nullopt`; every
  `VoragoMacroTarget` 0..38 appears in `kMbRoutes` exactly once and each `kMbRoutes` ID has `Route::MB`;
  mapping spot checks: 3 → `OutputSaturation`, 600 → `SubToneLevelOffsetDb`, 1102 → `CavernDecaySeconds`,
  1301 → `BloomSpawnRateHz`, 1401 → `AtmosBlur`.
- **Implement:** route assignment exactly as plan §3.2 Route column (MB: 3, 200–204, 300–302, 400–402,
  500–501, 600–601, 700–702, 800, 900, 1000–1003, 1100–1106, 1300–1301, 1400–1401, 1500–1502; VP: 205, 206,
  310–353, 403, 510–515, 1004, 1005; ENG: 1, 2, 610–612, 1200–1206, 1402, 1403; CV: 1107–1115; MAC: 5,
  100–111; Local: 0, 4). `constexpr` predicates + `static_assert`s for ascending/unique, per-route counts,
  and MB completeness. Linear-scan `constexpr routeOf` returning `std::optional<Route>`.
- **Verify:** BUILD `vorago_tests` zero warnings; RUN `Vorago_RouteTable`.

### T032 [P] — `Vorago::SustainLatch` (C-9, FR-030, plan §4.7)

- **Files:** `plugins/vorago/src/processor/sustain_latch.h` (fill placeholder),
  `plugins/vorago/tests/integration/sustain_test.cpp`.
- **ODR:** `grep -rn "class SustainLatch\|struct SustainLatch" dsp/ plugins/` → 0 hits (record).
- **Failing test first** — `TEST_CASE("Vorago_SustainLatchUnit", "[vorago][midi]")`, releases collected
  into a fixed `std::array<std::uint8_t,128>` + count:
  - pedal up: `noteOff(60)` returns true.
  - `setPedal(true)`; `noteOn(60)`; `noteOff(60)` returns false; `setPedal(false, rel)` → released {60}.
  - re-strike: pedal down, on 60, off 60 (latched), on 60 (held again), `setPedal(false)` → releases
    nothing; `noteOff(60)` → true.
  - `releaseAll(rel)` with 60 and 64 latched → releases {60, 64}, `isDown() == false`.
  - `clearWithoutRelease()` → nothing released, later pedal-up releases nothing.
  - 10 000 random operations (seed 12012) inside `AllocationScope` (`tests/test_helpers/allocation_detector.h:48-180`)
    → 0 allocations.
- **Implement:** class per plan §4.7 (two `std::bitset<128>`, `bool down_`), all `noexcept`, header-only,
  audio-thread only; note numbers ≥ 128 ignored.
- **Verify:** BUILD `vorago_tests` zero warnings; RUN `Vorago_SustainLatchUnit`.

---

## Group 10 — Controller (sequential; `controller.h/.cpp`)

### T033 — Register, format and describe all 108 parameters (FR-041, SC-018)

- **Files:** `plugins/vorago/src/controller/controller.cpp`, `plugins/vorago/tests/unit/param_table_expected.h`,
  `plugins/vorago/tests/unit/param_table_test.cpp`, `plugins/vorago/tests/unit/param_denorm_test.cpp`.
- **Failing test first:**
  - `param_table_expected.h`: one `constexpr` row per ID (108): id, title, units, stepCount, flags,
    default normalized (double), taper kind, plain min/max, expected n = 0.5 value, and the SC-011 class
    (A / B / C per plan §6.4: A = 0, 3, 5, 100–111, 200–206, 300–302, 330–353, 400–402, 500–501, 600–601,
    610–612, 700–702, 800, 900, 1000–1003, 1100–1114, 1201–1206, 1300–1301, 1400–1402, 1500–1502 (85);
    B = 310–313, 320–323, 403, 1004–1005, 1115 (12); C = 1, 2, 4, 510–515, 1200, 1403 (11)).
    `static_assert` 108 rows, 85/12/11 split, and that the 14 Phase 11 rows (0, 1, 100–111) carry their
    Phase 11 title/units/stepCount/flags/default (Master Gain dB 0.5; Polyphony list 5 steps 0.6; macros).
  - `TEST_CASE("Vorago_ParameterInfoTable", "[vorago][params]")`: fresh `Controller`, `initialize(nullptr)`
    → `getParameterCount() == 108`; for each row: `getParameterInfo` title/units/stepCount/flags equal,
    default within 1e-9; `getParamStringByValue(id, v)` non-empty for v ∈ {0, 0.5, 1}; for every continuous
    row the pack's denormalize at 0.5 equals the table's n = 0.5 value (relative 1e-9: log/olog midpoints
    per T015, linear midpoint otherwise); for IDs 1004/1005 index n → `BodyMaterial` n for n < 11.
  - Update `param_denorm_test.cpp` SECTION `"ControllerRegistersFourteen"` (`:222-236`): expected count
    `14` → `108`, section renamed `"ControllerRegistersAll"`, expected-ID set built from `kParamRoutes`.
- **Implement:** `Controller::initialize` registers global, macros, then the 14 packs in band order;
  `getParamStringByValue` chains `formatGlobalParam`, macro formatting, then each pack's `format…`, falling
  back to the base class (StringLists format themselves).
- **Verify:** BUILD `vorago_tests Vorago` zero warnings; RUN `Vorago_ParameterInfoTable`,
  `Vorago_ParamDenormRoundTrip`; `"Vorago_EditorLifecycle"` (editor-lifecycle harness,
  `unit/controller/editor_lifecycle_test.cpp:179-198`) stays green with the untouched uidesc.

### T034 — `IMidiMapping` (FR-031, SC-013 (5))

- **Files:** `plugins/vorago/src/controller/controller.h`, `controller.cpp`,
  `plugins/vorago/tests/integration/channel_pressure_test.cpp`.
- **Failing test first** — `TEST_CASE("Vorago_ChannelPressure", "[vorago][midi]")` SECTION `"MidiMapping"`:
  `controller->queryInterface(Steinberg::Vst::IMidiMapping::iid, &ptr) == kResultOk` and non-null;
  `getMidiControllerAssignment(0, 0, kCtrlSustainOnOff, id)` → `kResultOk`, `id == kSustainPedalId` (4);
  channel 15 same; `kAfterTouch` → `kChannelPressureId` (5); CC 1 → `kResultFalse`; bus 1 with CC64 →
  `kResultFalse`.
- **Implement** (Disrumpo shape `plugins/disrumpo/src/controller/controller.h:218-224`, `controller.cpp:707-726`):
  add `public Steinberg::Vst::IMidiMapping` base, `DEFINE_INTERFACES DEF_INTERFACE(Steinberg::Vst::IMidiMapping) END_DEFINE_INTERFACES(EditControllerEx1)`
  and `DELEGATE_REFCOUNT(EditControllerEx1)`; rewrite the header comment `controller.h:9` ("NO IMidiMapping")
  to describe the CC64/aftertouch mapping; no `INoteExpressionController`.
- **Verify:** BUILD `vorago_tests Vorago` zero warnings; RUN `"Vorago_ChannelPressure"`; full
  `vorago_tests.exe "~[.perf]~[performance]~[long]"` green.

---

## Group 11 — Processor wiring (sequential; `processor.h/.cpp`, `param_surface_test.cpp`)

Shared test conventions (plan §6): 48 kHz, 512-sample blocks unless stated, note C2 (36) velocity 100,
both channels, output-domain latency 3072 samples; "same binary" = both arms in one test process;
render comparisons use `maxAbsDiff` / `rmsDiff` / `peakOf` / `allFinite` from
`plugins/vorago/tests/vorago_test_fixture.h:344-390`; no checked-in golden.

### T035 — Phase 11 reference chain helper + SC-002 negative control

- **Files:** `plugins/vorago/tests/phase11_reference_chain.h` (fill placeholder),
  `plugins/vorago/tests/integration/param_surface_test.cpp`.
- **Helper** `renderPhase11ReferenceChain(const RefChainSpec&) → {std::vector<float> L, R}` (plan §6.1):
  heap `VoragoEngine`, `setSeed(kEngineSeed)` then `prepare(sr, makeVoragoEngineConfig(kMaxBlockSamples))`,
  polyphony from the spec (default 4); heap `CavernVerb` prepared with `makeVoragoCavernConfig(kMaxBlockSamples)`;
  per block: optional hook (`std::function`-free: a template callable) then `VoragoMacroMatrix{}` (or the
  hook's matrix) `.apply(engine)` + `applyCavernTargets(cavern, computeCavernTargets())`, engine → cavern →
  gain 1.0 → the same output stage the processor uses; notes at scripted absolute positions split into the
  same block partition. (Read `processor.cpp:170-278` and `:433-460` first and mirror the chain exactly.)
- **Test** `TEST_CASE("Vorago_Phase12DefaultsMatchPhase11Chain", "[vorago][integration]")`: fresh processor
  at registered defaults (`ProcessorFixture`, `prepare(48000, 2048)`), note 36 vel 100 at 0, 8 s in 512
  blocks vs the helper's reference: `REQUIRE(peakOf(ref[3072:]) >= 1e-4)` then `maxAbsDiff ≤ 1e-5` per
  channel. This is a regression **guard**: it passes now (Phase 11 processor) and must stay green through
  every later task; if it ever fails, the last change broke C-4.
- **Verify:** BUILD `vorago_tests` zero warnings; RUN it.

### T036 — `processParameterChanges` dispatch to every pack + input hygiene (FR-013, SC-010)

- **Files:** `processor.h`, `processor.cpp`, `plugins/vorago/tests/unit/param_table_test.cpp`.
- **Failing test first** — `TEST_CASE("Vorago_ParamInputHygiene", "[vorago][params]")` using a new const
  seam `packsForTest()` (returns a struct of const refs to every pack) and `MultiParamChanges`:
  - unregistered IDs 6, 99, 112, 207, 399, 516, 613, 703, 801, 901, 1006, 1116, 1207, 1302, 1404, 1503,
    1599, 1600 at value 0.9 → every atomic in every pack bit-identical to a pre-snapshot.
  - For each of the 108 IDs and each value in {NaN, +Inf, −Inf (bit patterns, double), −0.5, 1.5}: after
    `process()` every atomic is finite and inside its plain range (NaN/Inf leave the atomic unchanged;
    −0.5 / 1.5 store the range min / max).
  - One valid change per pack (e.g. 201 at 0.25 → tilt −6.0) reaches that pack's atomic.
- **Implement:** in `processParameterChanges` (`processor.cpp:323-349`): per queue take the last point
  (sustain handled in T043), `if (!detail::isFinite(value)) continue;`, clamp [0, 1], dispatch by band
  (`< kGlobalParamRangeEnd` global … `< kLifeParamRangeEnd` life, ≥ 1600 ignored); add the 14 pack members
  to `Processor`; add `packsForTest()`. No push logic yet.
- **Verify:** BUILD `vorago_tests Vorago` zero warnings (`static_assert(sizeof(Processor) < 64 KiB)` holds);
  RUN `Vorago_ParamInputHygiene`, `Vorago_Phase12DefaultsMatchPhase11Chain`, full suite non-long green.

### T037 — MB and MAC routes: base overrides + live macros (FR-020, FR-021, SC-005, SC-007 counter, SC-004 MB rows)

- **Files:** `processor.h`, `processor.cpp`, `integration/param_surface_test.cpp`,
  `integration/param_flow_test.cpp` (existing SECTION inverted).
- **Failing test first:**
  - `TEST_CASE("Vorago_MacrosDriveTheMatrix", "[vorago][integration]")`: (1) 13 arms — each macro at 1.0
    alone, plus Gravity at 0.0 — every Voice getter on `i < getPolyphony()` and every Engine getter of the
    plugin's engine (`engineForTest()`) equals exactly the same getter on a second engine prepared
    identically and driven by a directly constructed `VoragoMacroMatrix` with that macro; (2) render 4 s
    vs `renderPhase11ReferenceChain` whose hook applies that matrix per block and
    `applyCavernTargets(computeCavernTargets())`: precondition RMS over [3072, end) ≥ 1e-4, max-abs ≤ 1e-6;
    (3) all-macros-at-1 vs defaults: `rmsDiff > 1e-3`.
  - `param_flow_test.cpp:162` SECTION `"MacrosAreInert"` → renamed `"MacrosAreLive"` and its assertion
    inverted (macro at 1.0 now changes the matrix-driven getters; FR-050).
  - `TEST_CASE("Vorago_MacroPushOncePerProcess", "[vorago][integration]")` SECTION `"Counter"`: new seam
    `macroPushCountForTest()` increases by exactly 1 per `process()` for blocks carrying 0, 1, 64 and 1024
    note events.
  - `TEST_CASE("Vorago_EveryRouteReachesTheChain", "[vorago][integration]")` SECTION `"MB"`: polyphony 6,
    six held notes (36, 43, 48, 55, 60, 67); for each of the 32 Voice/Engine MB IDs one change to 0.8 of
    its plain range (log/olog along the taper), render 4 800 samples, read back on every `i < getPolyphony()`
    (voice) or the engine getter: relative 1e-6. `macrosForTest().getTargetBase(t)` equals the plain value.
    Output saturation (3) via `engineForTest()->getOutputSaturation()` (`vorago_engine.h:858`).
- **Implement** (plan §4.3, §4.4, §4.8): `lastPushedMb_[39]`, `mbValid_`, a constructor-built
  `const std::atomic<float>*` source table over `kMbRoutes`; `pushMacroBases()`; `buildMacroVector()`
  (Pressure = `clamp(knob + channelPressure, 0, 1)`, others unmodified); in `process()` after
  `pushGlobalParams()`: `pushMacroBases(); macros_.setMacros(v); macros_.apply(*engine_);
  applyCavernTargets(*cavern_, macros_.computeCavernTargets()); ++macroPushCount_;` once, before the slice
  loop (the existing per-block apply at `processor.cpp` stays single). Seams `macroPushCountForTest()`,
  `macrosForTest()`. `macros_` stops being "never written" — update its comment (`processor.h:100`).
- **Verify:** BUILD zero warnings; RUN the four cases + `Vorago_Phase12DefaultsMatchPhase11Chain` +
  `Vorago_ParamFlowReachesEngine`.

### T038 — VP route: generation counter + `applyVoiceParams` (FR-020, SC-004 VP rows)

- **Files:** `processor.h`, `processor.cpp`, `integration/param_surface_test.cpp`.
- **Failing test first** — `Vorago_EveryRouteReachesTheChain` SECTION `"VP"`: same fixture; for each of
  the 31 VP IDs one change (comb fundamental **440 Hz**; continuous others at 0.8 of range; discrete to a
  non-default index), render 4 800 samples, read back on **every** `i < kMaxVoices`:
  `getVoice(i).cloud().getStereoSpread()/getSpectralGravity()`, `bodyA()/bodyB().getMaterial()`,
  `noise().getSourceModel/getCombFundamental/getCombSpread/getCombFeedback(slot)`,
  `resonance().getAnchorMode()`, `ecology().getLoopFilterMode(l)`; noise **type** rows via the Direct
  probe (same step also sets slot model ID to Direct; then `getSourceNoiseType(slot)` equals the set type).
  Relative 1e-6 / exact for discrete.
- **Implement:** `voiceParamGeneration_`, `lastAppliedVpGen_ = kGenerationSentinel`; `markDirty(id)` after
  the pack store bumps the generation for `Route::VP`; `pushVoiceParams()` builds a local
  `VoragoVoiceParams` from the atomics (index → enum via T015 tables; member writes, no positional brace
  init) and calls `engine_->applyVoiceParams(p)` on mismatch.
- **Verify:** BUILD zero warnings; RUN SECTION `"VP"` + SC-002 guard.

### T039 — ENG route + seed (FR-023, SC-004 ENG rows, SC-014 (2)(4))

- **Files:** `processor.h`, `processor.cpp`, `integration/param_surface_test.cpp`, `integration/seed_test.cpp`.
- **Failing test first:**
  - `Vorago_EveryRouteReachesTheChain` SECTION `"ENG"`: envelope mode → Growth, stage times / release
    at 0.8 normalized (olog), growth at 0.8 → slot-0 getters **and** `getVoice(i)` for every
    `i < kMaxVoices`; sub tones → `subharmonic().getToneLevelDb(t)` equals the parameter (matrix offset at
    0 dB); ghost reverse 0.6 → `atmosphere().getGrainReverseProbability() == 0.6f`; ghost triggers on →
    `getGhostEventTriggers()`; seed index 7 → `engineForTest()->getSeed() == kVoragoSeedValues[7]`.
  - `TEST_CASE("Vorago_SeedParameter", "[vorago][integration]")`: (2) same index (5) on two fresh
    processors, 8 s held note → `maxAbsDiff ≤ 1e-5`; (4) processor A: set index 9 while silent (one empty
    block), then note → vs processor B whose seed parameter was 9 before `setupProcessing` → ≤ 1e-5.
- **Implement:** trackers `lastSeedIndex_`, `lastEnvMode_`, `lastStageMs_[4]`, `lastReleaseMs_`,
  `lastGrowthS_`, `lastSubToneDb_[3]`, `lastGhostReverse_`, `lastGhostTriggers_`, `engValid_`;
  `pushEngParams()` (bit-exact float compare; seed push = `engine_->setSeed(kVoragoSeedValues[i]);
  cavern_->setSeed(cavernSeedFor(i));`). `setupProcessing` (`processor.cpp:118-146`): read seed index,
  `engine_->setSeed(kVoragoSeedValues[i])` **before** `prepare`, prepare the cavern with
  `makeVoragoCavernConfig(kMaxBlockSamples, cavernSeedFor(i))`, set `lastSeedIndex_ = i`.
- **Verify:** BUILD zero warnings; RUN both cases + SC-002 guard (index 0 must still equal Phase 11).

### T040 — CV route (SC-004 cavern rows)

- **Files:** `processor.h`, `processor.cpp`, `integration/param_surface_test.cpp`.
- **Failing test first** — SECTION `"Cavern"`: for each of the 16 cavern IDs (1100–1115): 4 s render, one
  held note C2 from 0, 512 blocks; reference = `renderPhase11ReferenceChain` with the cavern setter for this
  ID applied by hand before the first block (MB-cavern IDs: via a matrix `setTargetBase` in the hook);
  precondition reference RMS over [3072, end) ≥ 1e-4; plugin vs reference max-abs ≤ 1e-6 per channel;
  non-vacuity: reference vs default-cavern reference `rmsDiff > 1e-3` over [3072, end). Freeze also via
  `cavernForTest()->isFrozen()`.
- **Implement:** `lastCv_[8]`, `lastFreeze_`, `cvValid_`; `pushCavernParams()` calling `setDensity`,
  `setDimensionality`, `setBreath`, `setEarlySizeMs`, `setEarlyLevel`, `setEarlyAbsorption`, `setEarlySend`,
  `setDamperRate`, `setFreeze` on change.
- **Verify:** BUILD zero warnings; RUN SECTION `"Cavern"` + SC-002 guard. A failing non-vacuity check on an
  ID means the test value is inaudible in 4 s — lengthen the render, never loosen.

### T041 — `pushAllSurfaces`, re-prepare, default read-back, host re-send, latency (FR-022, SC-003, SC-004 re-prepare, SC-017, SC-023 (2))

- **Files:** `processor.h`, `processor.cpp`, `integration/param_surface_test.cpp`.
- **Failing test first:**
  - `Vorago_EveryRouteReachesTheChain` SECTION `"SurvivesReprepare"`: set **every** persisted ID to its
    per-ID non-default value from the MB/VP/ENG/Cavern sections, render one block, `setActive(false)` →
    `setupProcessing(44100)` → `setActive(true)`, re-strike the six notes, render 4 410 samples → full
    read-back (one shared helper used by all SECTIONs and by SC-009) holds; repeat at 96 kHz (9 600
    samples). **Mutation check (once, logged to `artifacts/reprepare_mutation.log`):** temporarily delete
    the `++voiceParamGeneration_` and `engValid_ = false` lines, observe this SECTION fail, restore.
  - `TEST_CASE("Vorago_RegisteredDefaultsMatchEngine", "[vorago][integration]")`: exactly per plan §6.3
    SC-003 — references built **without a Processor**: MB (39) → `VoragoMacroMatrix{}.getTargetBase(t)` and,
    for the 32 Voice/Engine targets, the owner getter on the §6.1 reference engine; VP (31) →
    `VoragoVoiceParams{}` field and the reference engine's voice getters on every `i < kMaxVoices` (types
    320–323 → literal `NoiseType::Brown` and the Direct probe on the reference engine); ENG (14) →
    `SubharmonicEngine::kDefaultToneLevelDb[t]`, `VoragoVoice::kDefaultStageTimesMs[0..3]`,
    `kDefaultReleaseMs`, `kDefaultGrowthDurationSeconds`, mode Standard, reverse 0, triggers off,
    `kVoragoSeedValues[0] == 1u`, polyphony 4; CV freeze → `!isFrozen()` on a reference `CavernVerb`;
    MAC (12) → `VoragoMacroValues{}`. Each registered default (controller `getParameterInfo`) denormalized by
    the pack function vs its reference: relative 1e-6, exact for discrete. `INFO` the row count and
    `REQUIRE(rows == 97)`. Second SECTION `"PushIntegrity"`: the same references read back from a processor
    after `setupProcessing` + one `process()`.
  - `TEST_CASE("Vorago_HostResendsEveryParameter", "[vorago][integration]")`: 10 s held note; arm B
    re-sends all 108 IDs at their current normalized value every block → `maxAbsDiff ≤ 1e-5` vs arm A.
  - `TEST_CASE("Vorago_LatencyIndependentOfParameters", "[vorago][integration]")`: at 44.1/48/96 kHz, every
    discrete ID at each value and every continuous ID at 0 and 1 → `getLatencySamples() == 3072`.
- **Implement** (plan §4.5, §4.6): `pushAllSurfaces(Scope)` invalidates `mbValid_`, bumps
  `voiceParamGeneration_` and sets `lastAppliedVpGen_` to the sentinel, clears `engValid_` **except the seed
  tracker**, clears `cvValid_`; polyphony tracker untouched (D-P2). `setupProcessing` step 6 calls
  `pushAllSurfaces(Scope::Reprepared)` directly after prepare. `forcePushPending_` (`std::atomic<bool>`)
  consumed in `process()` **after** the not-ready path and above `pushGlobalParams()` (D-P7).
- **Verify:** BUILD zero warnings; RUN the four cases + SC-002 guard + full suite non-long.

### T042 — State v2 (C-7, FR-040, FR-045, SC-008, SC-009)

- **Files:** `plugin_ids.h` (`kCurrentStateVersion = 2`), `processor.h/.cpp`, `controller.cpp`,
  `unit/state_v2_test.cpp`, `integration/param_surface_test.cpp`, `unit/state_roundtrip_test.cpp`.
- **Failing test first:**
  - `TEST_CASE("Vorago_StateRoundTripV2", "[vorago][state]")` (plan §6.3 SC-008): (1) all 106 persisted
    IDs to seeded normalized values (`std::mt19937{12008}`) → `getState` length == `kStateV2Bytes` (428) →
    fresh processor `setState` → every atomic bit-identical (`std::bit_cast`), controller
    `setComponentState` normalized within 1e-9, held-note 4 s render ≤ 1e-5; (2) a 60-byte v1 stream built
    by hand in the v1 writer order (version 1, masterGain, polyphony, 12 macros) → the 14 v1 fields
    restored, all 92 others at registered defaults; (3) truncation at every offset 0..427 into a fresh and a
    pre-dirtied instance → no crash, prefix fields restored, suffix unchanged; (4) version 3 →
    `kResultFalse`, no atomic changed; (5) NaN bit pattern in each float field in turn → that field
    unchanged, the rest loaded; (6) atomics with sustain down and pressure 0.7 serialize byte-identically to
    both 0; after `setState`, `sustainPedal == 0` and `channelPressure == 0`.
  - `TEST_CASE("Vorago_SetStateAfterPrepareReachesDsp", "[vorago][integration]")`: prepared, rendering
    processor; `setState` of a stream with every persisted ID non-default; one block; the shared read-back
    helper from T041 holds.
  - Phase 11 `unit/state_roundtrip_test.cpp`: expected stream length 60 → `kStateV2Bytes`; "future
    version" constant 2 → `kCurrentStateVersion + 1` (`:267` SECTION); the v1 decode expectation moves to
    SC-008 (2). No assertion is weakened.
- **Implement** (plan §4.9): `kCurrentStateVersion = 2`; `getState` writes version, v1 block, then
  `saveGlobalParamsV2Ext` and the 14 pack saves in band order; `setState`: version > 2 → `kResultFalse`
  with nothing changed; ≤ 1 → v1 block only; == 2 → v1 block then the short-circuit chain
  `loadGlobalParamsV2Ext && loadCloud… && … && loadLife…`; then store `sustainPedal = channelPressure = 0`,
  `forcePushPending_.store(true, release)`, `latchReleasePending_.store(true, release)` (consumed in T043);
  **no DSP call**. `constexpr std::size_t kStateV2Bytes = 428` with a `static_assert` of the per-pack sum.
  Controller `setComponentState` mirrors with the `…ToController` functions and the same version gate.
- **Verify:** BUILD `vorago_tests Vorago` zero warnings; RUN the three cases, `Vorago_StateRoundTrip`, SC-002
  guard, full suite non-long.

---

## Group 12 — MIDI, pressure, invariance, RT safety (sequential; processor + distinct test TUs)

### T043 — Sustain wiring (C-9, FR-030, SC-012)

- **Files:** `processor.h`, `processor.cpp`, `integration/sustain_test.cpp`.
- **Failing test first** — `TEST_CASE("Vorago_SustainLatch", "[vorago][midi]")`; every assertion reads
  `engineForTest()->getVoiceState(slot)` after the block carrying the event (slot = the one that left
  `Idle` on the note-on):
  1. pedal down, note-off → `Active`, still `Active` after 2 s more; pedal up → `Releasing`; control with
     the pedal up → `Releasing` after the note-off block.
  2. one 512 block: note-off @100, pedal-down @200 → `Releasing`; pedal-down @100, note-off @200 →
     `Active`; same-offset note-off and pedal-down @150 → `Releasing` (notes first, D-P6).
  3. latched note re-struck, pedal up while held → `Active`; later note-off → `Releasing`.
  4. `setActive(false)` with a latched note → `Releasing` or `Idle` immediately after the call returns;
     `setState(v2 stream)` with a latched note → still `Active` right after `setState()` returns; after the
     next `process()` → `Releasing` or `Idle`.
  5. 10 000 random pedal/note events across blocks inside `AllocationScope` → 0 allocations.
- **Implement** (plan §4.7): `SustainLatch latch_`; in `processParameterChanges` copy **every** point of
  the `kSustainPedalId` queue into `std::array<PedalPoint, 128> pedalPoints_` (surplus coalesced into the
  last slot); merge a pedal cursor into the slice loop (`sliceEnd = min(nextNote, nextPedal, cursor + kMaxBlockSamples, total)`;
  equal offsets: notes first); note-off (and note-on velocity 0) routed through `latch_.noteOff`;
  `latchReleasePending_` consumed after the not-ready guard and before `forcePushPending_`;
  `setActive(false)` calls `latch_.releaseAll(noteOff)` before `engine_->silence()`; `setupProcessing`
  calls `latch_.clearWithoutRelease()`; early-return `process()` paths apply pending pedal points to the
  latch only when `prepared_`; pedal plain value mirrored to `globalParams_.sustainPedal`. Seam `latchForTest()`.
- **Verify:** BUILD zero warnings; RUN `Vorago_SustainLatch`, `Vorago_SustainLatchUnit`, `Vorago_MidiEventTranslation`
  (`unit/midi_event_test.cpp:181`) and the SC-002 guard.

### T044 — Channel pressure composition (FR-021, FR-032, FR-045, SC-013 (1)–(4), (7))

- **Files:** `integration/channel_pressure_test.cpp` (processor code already exists from T037/T042; fix
  there only if a clause fails).
- **Test** — add SECTIONs to `Vorago_ChannelPressure`: (1) pressure 0 → `macrosForTest().getMacros()`
  equals the twelve atomics exactly; (2) pressure 1.0 with the Pressure knob at default → every
  Pressure-row target getter equals a knob = 1.0 / pressure 0 arm (≤ 1e-6); (3) knob 0.4 + pressure 0.3 ≡
  knob 0.7 + pressure 0 (getters and `getMacros().pressure` within 1e-6), every other macro's targets
  unaffected by pressure; (4) knob 0.8 + pressure 0.5 ≡ knob 1.0 + pressure 0; (7) pressure 0.7 and pedal
  down with one latched note, then `setState` of a v2 stream whose Pressure knob is 0.25 → immediately
  `packsForTest()` shows `sustainPedal == 0` and `channelPressure == 0`; after one `process()`
  `getMacros().pressure == 0.25f` exactly and the latched slot reads `Releasing`.
- **Verify:** BUILD `vorago_tests` zero warnings; RUN `Vorago_ChannelPressure`.

### T045 — Block-size invariance with pedal (SC-007 full)

- **Files:** `integration/param_surface_test.cpp`.
- **Test** — `Vorago_MacroPushOncePerProcess` SECTION `"BlockSizeInvariance"`: the Phase 11 SC-008 (1)
  4 s script (read `specs/vorago-phase11-plugin-scaffold/spec.md:810-822` and its test to copy the note
  offsets) plus sustain down/up points at offsets that are non-multiples of every partition, arranged so at
  least one note-off is latched and later released by the pedal-up; at sample 0 only: all twelve macros
  0.7 (Gravity 0.8), ID 201 → −6 dB/oct, ID 206 → 0.5. Partitions {1, 7, 64, 65, 512, 2048, 4096} each vs
  512: precondition 512-reference peak over [3072, end) ≥ 1e-4; max-abs ≤ 1e-5 per channel.
  `lastSliceCountForTest() == 2` for an event-free 4096 block. `compareFingerprints`
  (`tests/test_helpers/render_fingerprint.h`) as `WARN` only.
- **Verify:** BUILD; RUN `Vorago_MacroPushOncePerProcess`.

### T046 — RT safety under full automation + live reseed (SC-015, SC-014 (5))

- **Files:** `integration/automation_rt_test.cpp`, `integration/seed_test.cpp`.
- **Tests:**
  - `TEST_CASE("Vorago_FullSurfaceAutomationAllocFree", "[vorago][integration]")`: 2 000 blocks × 512;
    every registered ID gets a seeded random normalized value every block (`std::mt19937{12015}`), notes
    and pedal points too; `AllocationScope` around each `process()` → 0 allocations total; `allFinite`;
    `peakOf ≤ 10^(−0.3/20)`; `engineForTest()->getNonFiniteRecoveryCount() == 0`.
  - `Vorago_SeedParameter` SECTION `"LiveReseed"` (5): note sounding, seed index 3 → 11 mid-render inside
    `AllocationScope` → 0 allocations, finite, peak ≤ ceiling.
- **Verify:** BUILD zero warnings; RUN both. An allocation is a defect in the push path — fix it, never
  exclude the ID.

---

## Group 13 — `[long]` lane (sequential; each run alone, captured to a log)

### T047 — FR-053 probe seam + SC-011 continuity

- **Files:** `processor.h`, `processor.cpp` (seam), `integration/continuity_test.cpp`.
- **Seam (FR-053):** declare `namespace detail { struct VoragoMasterGainSmootherBypassProbe; }` in
  `processor.h` (ODR sweep: 0 hits, record), make it a `friend` of `Processor`, add private
  `bool masterGainSnapProbe_ = false;`, and in `pushGlobalParams` use
  `if (snapGainPending_ || masterGainSnapProbe_) masterGain_.snapTo(gain);`. The probe struct is defined
  only in `continuity_test.cpp`.
- **Test** `TEST_CASE("Vorago_ParameterStepsAreContinuous", "[vorago][integration][long]")` (plan §6.4):
  scope = SC-011 column of `param_table_expected.h` (class A 85 IDs: 64 equal normalized steps 0→1;
  class B 12 IDs: 64 indices from a checked-in sequence `kDiscreteStepSeq` generated from seed 12011 with
  no index equal to its predecessor, ID 403's sequence containing all six ordered pairs; for 320–323 the
  slot model is set to Direct at sample 0; class C 11 IDs: same stepping, clause 4 only). Per ID: 48 kHz,
  512 blocks, note C2 vel 100 from 0, 1 s warm-up, steps 125 ms apart. Clause 1: `maxDeltaInWindow`
  (`tests/test_helpers/vorago_fixtures.h:381`) over ±10 ms centred on `step + 3072`; clause 2: 64 reference
  windows midway between steps (same shift); clause 3: `max(test) ≤ 1.5 × max(ref)` for classes A and B;
  clause 4 (all 108): every sample finite, peak ≤ −0.3 dBFS. Positive controls: (a) a one-sample step of
  2 × a reference window's own statistic injected into that window must exceed the bound; (b) with the
  probe engaged, master gain's 64-step render must **fail** clause 3. Print a per-ID table.
- **Run:** BUILD zero warnings; alone: `vorago_tests.exe "Vorago_ParameterStepsAreContinuous" > artifacts/sc011_continuity.log 2>&1`.
- **Done when:** all IDs pass and both controls behave. If an ID fails → T048.

### T048 — CONDITIONAL R-5 remedy (only for IDs T047 failed)

- **Rule (plan R-5, D-P8):** add a processor-side `OnePoleSmoother` (20 ms) on the failing ID's **plain**
  value, stepped once per `process()` in the pre-slice push step with `advanceSamples(numSamples)`,
  `snapTo` whenever its tracker is invalid; MB → `setTargetBase(t, smoothed)` before the single `apply()`;
  CV/ENG → the ID's own setter; continuous VP IDs (205, 206, 330–353) → a per-field engine fan-out over all
  `kMaxVoices` added to `vorago_engine.h` for that field only — **never** via `applyVoiceParams`. No push
  inside the slice loop.
- **Stop and surface** (no code) if a class-B discrete ID fails, or block-rate stepping still fails clause 3.
  Never an exemption, never a looser bound.
- **Verify:** re-run T047 alone; SC-007 (`Vorago_MacroPushOncePerProcess`), SC-002 guard, SC-003 pass.
  If no ID failed, record "no R-5 remedy" in `artifacts/sc011_continuity.log` and skip.

### T049 — SC-014 (3) seed table spread

- **Files:** `integration/seed_test.cpp`.
- **Test** `TEST_CASE("Vorago_SeedTableSpread", "[vorago][integration][long]")`: 16 fresh processors, seed
  index i, 30 s held-note render each; all 120 pairs `rmsDiff > 1e-3` over [3072, end).
- **Run alone** to `artifacts/sc014_spread.log`. On a failing pair: replace the offending
  `kVoragoSeedValues` entry (never index 0) with the next splitmix64 stream value (update the header
  comment), re-run T015's test and this gate. The gate is never lowered.

### T050 — SC-020 random-surface soak

- **Files:** `integration/soak_test.cpp`.
- **Test** `TEST_CASE("Vorago_RandomSurfaceSoak", "[vorago][integration][long]")`: 5 seeds 12020..12024 ×
  60 s at 48 kHz, notes C2 + G2 vel 100 held; every registered ID except 0, 4, 5, 500, 1003, 1105 re-drawn
  every 5 s; every sample finite; peak ≤ −0.3 dBFS; last-10 s RMS ≥ `R_default − 60 dB`, where `R_default`
  is the same window of a default-surface render in the same test (precondition `R_default ≥ 1e-4`).
- **Run alone** to `artifacts/sc020_soak.log`.

### T051 — DSP `[long]` cases

- **Run alone**, each to its own log: `dsp_systems_tests.exe "VoragoEngine_ApplyVoiceParamsDefaultIsNoOp"`,
  `"VoragoEngine_NewSettersDefaultInert"`, `"VoragoEngine_RepeatedBroadcastIsInert"`, and
  `vorago_tests.exe "[long]"` minus the cases already logged. All pass; record figures.

---

## Group 14 — Perf lane (sequential; nothing else running; `node tools/run-cpu-tests.js` only)

### T052 — SC-016 wrapper overhead

- **Files:** `plugins/vorago/tests/integration/processor_cpu_test.cpp` (existing `Vorago_ProcessorCpu`).
- **Edit:** the gated quiescent arm now runs with every route wired at registered defaults (P/D ≤ 1.05);
  add a `WARN`-recorded arm automating IDs 201 and 206 every block; keep arm E and record it against
  Phase 11's `1.79691e+07 ns`; if T048 landed any remedy, add the remedy arm per plan SC-016 (i)/(ii),
  otherwise log "no R-5 remedy — arm N/A".
- **Run:** `node tools/run-cpu-tests.js vorago_tests > specs/vorago-phase12-parameters/artifacts/sc016_cpu.log 2>&1`,
  alone. A failure is re-run once after idle before being treated as a defect; the budget is never relaxed.

### T053 — SC-021 CPU gate and axis re-runs

- **Run alone:** `node tools/run-cpu-tests.js dsp_systems_tests > artifacts/sc021_cpu.log 2>&1` →
  `VoragoEngine_CpuBudget` (`dsp/tests/unit/systems/vorago_perf_test.cpp:971`) passes. Then
  `dsp_effects_tests.exe "VoragoComposed_DepthMacroAxis" > artifacts/sc021_depth_axis.log 2>&1`.
- **Record** in `artifacts/sc021_summary.log`: Mass figures (T006), the T007 Gravity/Pressure figures
  with the path taken (A or B, and the new target's name under B), the T004 probe figures, and Phase 10's
  figures (`specs/vorago-phase10-voice-engine/compliance.md:20`).

---

## Group 15 — Documentation (sequential)

### T054 — Leaf `plugins/vorago/CLAUDE.md` (FR-052)

- **Edit:** ID table with the new bands (Envelope 1200, Bloom 1300, Ghost 1400, Life 1500; 1600+
  unassigned) and macros LIVE; state v2 layout (428 bytes, v1 a strict prefix); the route table (MB 39,
  VP 31, ENG 14, CV 9, MAC 13, Local 2); decisions 1 and 5 marked **delivered** (`IMidiMapping`, sustain);
  decision 3's "must go through the matrix" replaced by the shipped MB route for output saturation (ID 3
  → `setTargetBase(OutputSaturation, …)`); the D-P2 pushAllSurfaces exclusions; the near-name hazard (no TU
  may `using namespace` both `Vorago` and `Seraphis`). No transient machine state.
- **Verify:** `node tools/lint-plugin-roster.js` exits 0.

---

## Group 16 — Integration (sequential, last)

### T055 — Full build, zero warnings

- BUILD `dsp_systems_tests dsp_effects_tests vorago_tests Vorago` from clean object state for the touched
  targets; grep the build output for `warning` → 0 lines from touched files. The post-build VST3 copy
  permission error is not a compile failure (root `CLAUDE.md`).

### T056 — CMake registration audit

- For every `.cpp` under `plugins/vorago/tests/{unit,integration}` and `dsp/tests/unit/systems/vorago_*`,
  confirm it appears in its enumerated list; every NaN-injecting Phase 12 TU is in its fast-math-off list
  and `processor_cpu_test.cpp` / the probe TU are not; the 17 headers are listed in
  `plugins/vorago/CMakeLists.txt`. Every placeholder from T002 has real content (grep for the placeholder
  comment → 0 hits). Edit only on a gap, then repeat T055.

### T057 — Full suite run (non-perf)

- Alone, each to a log under `artifacts/`: `dsp_systems_tests.exe "~[.perf]~[performance]~[.probe]"`,
  `dsp_effects_tests.exe "~[.perf]~[performance]"`, `vorago_tests.exe "~[.perf]~[performance]"`,
  `shared_tests.exe`. All pass, including `[long]` (VoragoMacro_SweepAxes passes on all twelve axes
  after T007; any row failing is a defect). Run the exes directly; `ctest -R <exe>` is not a substitute (it matches
  case names, not executables).

### T058 — pluginval, editor lifecycle, clang-tidy

- `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Vorago.vst3"`
  → exit 0 (log to `artifacts/pluginval.log`).
- `./tools/run-clang-tidy.ps1 -Target vorago -BuildDir build/windows-ninja` and `-Target dsp` → 0 errors,
  0 warnings (fix every finding; logs to `artifacts/`).

### T059 — Lints and portability

- `node tools/check-portability.js`, `node tools/lint-odr.js`, `node tools/lint-layers.js`,
  `node tools/lint-nonfinite-symbols.js`, `node tools/lint-float-bit-goldens.js`,
  `node tools/lint-plugin-roster.js` → each exits 0 (outputs to `artifacts/lints.log`). Any finding is
  fixed at its source and T055 + the affected suites re-run.

---

## Dependency summary

```
G1 T001 → T002
G2 T003
G3 T004 → T005 (probe read-out; ruling R-1 pre-taken; blocks T007 only)
G4 T006 → T007 (path A or B per T005)
G5 T008 → T009 → T010 → T011 → T012 → T013
G6 T014 → T015
G7 T016..T029 [P]
G8 T030
G9 T031 [P], T032 [P]
G10 T033 → T034
G11 T035 → T036 → T037 → T038 → T039 → T040 → T041 → T042
G12 T043 → T044 → T045 → T046
G13 T047 → T048 (conditional) → T049 → T050 → T051
G14 T052 → T053            (perf lane, alone)
G15 T054
G16 T055 → T056 → T057 → T058 → T059
```

Groups 5–16 do not depend on the T005 ruling. SC coverage: SC-001 T055–T059 · SC-002 T035 · SC-003 T041 ·
SC-004 T037–T041 · SC-005 T037 · SC-006 T003 · SC-007 T037, T045 · SC-008 T042 · SC-009 T042 · SC-010 T036 ·
SC-011 T047–T048 · SC-012 T032, T043 · SC-013 T034, T044 · SC-014 T039, T046, T049 · SC-015 T046 ·
SC-016 T052 · SC-017 T041 · SC-018 T033 (+ Group 7) · SC-019 T009–T011, T013, T051 · SC-020 T050 ·
SC-021 T004–T007, T053 · SC-022 T011 · SC-023 T012, T041. FR-052 T054; FR-053 T047; FR-060 T004–T007.
