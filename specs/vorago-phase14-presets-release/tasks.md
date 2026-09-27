# Tasks: Vorago Phase 14 — Factory Presets & Release Readiness

**Spec:** `specs/vorago-phase14-presets-release/spec.md` · **Plan:** `specs/vorago-phase14-presets-release/plan.md`
(both normative; "§n" below means a plan section unless marked "spec")
**Branch:** `feat/vorago-phase1-events-modulation` (one branch per roadmap)
**Date:** 2026-09-27 · **Status:** TASKS — nothing implemented

---

### Plan decisions, ruled 2026-09-27 (spec Clarifications, plan stage R-1 … R-8)

- R-1 (P-1): the append-only `dsp/` edits are accepted — T001's inert friend now, T006/T007's forwarders,
  `VoragoVoiceParams` fields and `applyVoiceParams` lines after G1. No new class.
- R-2 (P-3): SC-029 reads **N == 38 + |E-ext|**; the [35, 45] band is superseded on N.
- R-3 (P-2, P-5): FR-006, FR-031, C-9, SC-005 say `kCurrentStateVersion` / `kStateV3Bytes`; SC-029's
  primaries are the **non-default-state** members. Already applied to `spec.md`; T005 does not re-apply.
- R-4 (P-6): the aggregate job loads shard record artifacts; only the control twins are rendered there.
- R-5 (P-8): D10.1 is a verified secondary of the S8 preset on its freeze-gesture render, never a primary.
- R-6: **T038 first edits `tools/run-cpu-tests.js`** so `FILTER` ends in `[long]~[vorago-sweep]`, then
  runs the standard `node tools/run-cpu-tests.js vorago_tests`; no name-filtered side path.
- R-7: the CMake registration split (T003 / T029 / T049 audit) and the separate
  `Vorago_FactoryPresets_LibraryShape` case are accepted as written.
- R-8: **new T001b** creates `preset_test_support.h` part 0 (descriptor + distance) before the probe;
  T002 includes it and keeps **no** local copy; T022 and T025 extend that header.
- Still open for G1 (T005): the roster `R`, P-4's one-sided cells, the §5.8 E-ext halfway margin.

## How to read this file

- Tasks are numbered `T001…` and grouped. **Groups run strictly in order.** Inside a group, tasks
  tagged **[P]** create only NEW files that no other task in that group touches, so they may run in
  parallel. A task that edits an existing or shared file (a CMake list, an existing header, a header
  another task also touches) sits alone in its own sequential group.
- Every task follows the canonical order: **write the failing test first → implement → zero warnings →
  tests pass.** Where a task creates only test code, the implementation it waits on is named.
- **Two hard stops (the plan's gates), never skipped:**
  - **G1 (T005):** after the audibility probe, STOP and surface the probe table, the proposed roster
    `R` to the user (FR-071). **No task from T006 onward starts before the
    user ratifies `R`** (with P-4's one-sided cells and the E-ext margin; P-1, P-2, P-3, P-5, P-6 and
    P-8 are already ruled — see "Plan decisions" above).
  - **G2 (T033):** after the pilot, record the frozen `F` (never below 4.0). STOP if any seed twin
    exceeds `F/2` (spec C-7.3(b), FR-017).
  - Also **FR-017 stop-and-surface** anywhere: a cell no authorable preset can verify, a failed C-7.3
    control, a failed boundedness arm, or a CPU breach is reported with its measurements. Never relax a
    predicate, floor, control, window or budget; never drop a cell/arm/preset; never edit `dsp/` to pass.
- **`R` is unknown until G1.** Tasks T006+ are written per ratified knob: `<Knob>` stands for the setter
  name without `set` (e.g. `Predation`, `SyncRate`, `KernelSigma`), `|R|` for the roster size, and
  `kNumEcosystemRuleParams == |R|`.
- **CMake registration.** `plugins/vorago/tests/CMakeLists.txt` is ENUMERATED, never globbed
  (`plugins/vorago/tests/CMakeLists.txt:5-6`: "an unregistered TU silently drops out of the build").
  Because G1 and G2 need code that RUNS mid-phase, registration cannot wait for the last group as in
  Phase 13: **T003** registers the probe TU only (needed for G1), **T029** is the single registration of
  every other new TU plus the root generator targets (needed before the pilot), and **T049** in the
  final group is the registration audit. Until T029 lands, tasks whose "Verify" names a new TU are
  verified in T030.
- **Build (Windows, full path always):**
  `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <t>`
  Run a suite directly: `build/windows-x64-release/bin/Release/<t>.exe "<TestName>" 2>&1 | tail -5`
  (positional name filter; never `ctest -R`). Capture any run over ~1 min to a log under `f:/tmp/` and
  read the log; never re-run a slow suite just to see its output.
- **Per-push filter for local checks** (what CI runs per push, `.github/workflows/ci.yml:374`):
  `"~[performance]~[perf]~[benchmark]~[!benchmark]~[long]"`. **Do not run `vorago_tests.exe` with no
  filter** after T029 (registration) lands: `[long][vorago-sweep]` cases are not hidden, so an unfiltered run starts the
  multi-hour sweep.
- **Cross-cutting rules for every task:** no allocation/lock/exception/IO on the audio thread; finite
  checks by bit pattern only (`Krate::DSP::detail::isFinite`), never `std::isnan`/`std::isfinite`; no
  brace-init narrowing (designated initialisers, explicit `static_cast`); no bit-exact float goldens
  (fingerprints/tolerances only, spec C-8); `std::thread` not `std::jthread`; `_dupenv_s` under
  `_MSC_VER` instead of `std::getenv`; names per root `CLAUDE.md`; no TU does `using namespace` of both
  `Vorago` and `Seraphis`. Inside `namespace VoragoTest`, qualify the plugin namespace as `::Vorago::`
  (the metrics header also declares a nested `Krate::DSP::TestUtils::Vorago`,
  `tests/test_helpers/vorago_fixtures.h:70-73`).
- **Namespaces of reused helpers (read this session):** metrics `Krate::DSP::TestUtils::Vorago`
  (`vorago_fixtures.h:70-73`: `bandEnergyDb` :245, `crestFactorDb` :316, `blockRmsDb` :344,
  `perBandTotalVariation` :441, `perBinMagnitudeFlux` :502); fingerprints `Krate::DSP::TestUtils`
  (`render_fingerprint.h:50-61`, `fingerprintRender` :73); `calculateCorrelation`
  (`low_frequency_metrics.h:478`); `TestHelpers::AllocationScope` / `ThreadScopedAllocationScope`
  (`allocation_detector.h:20`, :111, :149); `Krate::Test::ParameterChanges::addChange(ParamID, double)`
  (`vst_param_changes.h:31`, :78, :110); `Krate::Test::EventList::addNoteOn(pitch, velocity, offset)` /
  `addNoteOff(pitch, offset)` (`vst_event_list.h:34-77`). None of these headers includes Catch2. Test
  TUs include them as `<vorago_fixtures.h>` etc. (precedent `integration/continuity_test.cpp:40`).
- **Out of scope for every task:** commits and pushes (outside this workflow; any task needing a CI
  run on pushed code says so and waits for the user's permission), any new DSP class, any `dsp/` edit
  other than T001 and T007, any Seraphis file, any parameter outside the ecosystem block.

---

## Stage A — Audibility probe (FR-070) and gate G1

## Group 1 — inert probe friend in `dsp/` (sequential; shared headers)

### T001 — Forward-declare and befriend `detail::VoragoEcosystemRuleProbe` (plan P-1 Stage A, §4.1)

- **Edit:** `dsp/include/krate/dsp/systems/vorago_voice.h`, `dsp/include/krate/dsp/systems/vorago_engine.h`.
- **ODR first:** `grep -rn -E "(class|struct) VoragoEcosystemRuleProbe\b" dsp/ plugins/ tools/ tests/` → 0 hits.
- **Test first:** none is possible for an inert friend (it is exercised by T002). The guard is that the
  existing systems suite stays green after the edit.
- **Implement (append-only, 3 lines + comments):**
  - in the `detail` block of `vorago_voice.h` (`:157-171`, beside `VoragoVoiceIdentityProbe`):
    `/// Phase 14 FR-070's rule-knob audibility probe. B-4: DEFINED IN THE TEST TU.`
    `struct VoragoEcosystemRuleProbe;`
  - in `VoragoVoice`'s private friend list (`vorago_voice.h:1531-1535`):
    `friend struct detail::VoragoEcosystemRuleProbe;   // Phase 14 FR-070 (B-4)`
  - in `VoragoEngine`'s private friend list (`vorago_engine.h:1302-1303`): the same line.
  - Nothing else. No function body changes.
- **Verify:** build `dsp_systems_tests` with zero warnings; run
  `build/windows-x64-release/bin/Release/dsp_systems_tests.exe "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" 2>&1 | tail -3`
  → all pass. Build `vorago_tests` (unchanged TU list) → zero warnings.

## Group 1b — descriptor header, before the probe (ruling R-8; single new file)

### T001b — `plugins/vorago/tests/preset_test_support.h` part 0: descriptor + distance (§4.8, §5.4–§5.5)

- **Create:** `plugins/vorago/tests/preset_test_support.h` (new; Catch2-free; namespace `VoragoTest`).
- **ODR first:** `grep -rn -E "(class|struct) PresetDescriptor\b" plugins tools tests dsp` → 0 hits.
- **Test first:** the descriptor's unit test is T023's `Vorago_PresetDescriptor_Invariants`
  (level-independence: `describe(0.5·x) == describe(x)` within 1e-9; `descriptorDistance(a, a) == 0`;
  `meanOf({a}) == a`); write that case's assertions now in T023's file when T023 runs, and here only the
  header. The probe TU (T002) is the first consumer and T003/T030 build it.
- **Implement** exactly §5.4/§5.5, using the metrics helpers in `Krate::DSP::TestUtils::Vorago`
  (`vorago_fixtures.h`: `bandEnergyDb` :245, `crestFactorDb` :316, `perBandTotalVariation` :441,
  `perBinMagnitudeFlux` :502) and `calculateCorrelation` (`low_frequency_metrics.h:478`):
  `struct PresetDescriptor { std::array<double, 9> band; double motion, flux, corr, energySpread, crest; };`
  `PresetDescriptor describe(std::span<const float> L, std::span<const float> R, double sr)` (9 bands
  relative to `E_hi = E(80, 20000)`, clamp −60 dB, /3; motion/flux log2 with 1e-6 floor; corr/0.25;
  energySpread/2; crest/3), `double descriptorDistance(const PresetDescriptor&, const PresetDescriptor&)`
  (Euclidean over the 14 components), `PresetDescriptor meanOf(std::span<const PresetDescriptor>)`.
  Nothing else goes in this file yet; T022 and T025 extend it.
- **Verify:** T002 compiles against it (T003 → build `vorago_tests`, zero warnings).

## Group 2 — probe TU (single new file)

### T002 — `Vorago_EcosystemRuleProbe` hidden probe (FR-070, SC-025, §4.3)

- **Create:** `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp` (new).
- **Test (this TU IS the measurement):**
  `TEST_CASE("Vorago_EcosystemRuleProbe", "[.probe][vorago]")` — hidden, run once by hand.
- **Implement:**
  - Define, in this TU, in `namespace Krate::DSP::detail`:
    ```cpp
    struct VoragoEcosystemRuleProbe {
        template <typename Fn> static void forEachEcosystem(VoragoEngine& e, Fn fn) {
            for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) { fn(e.voices_[v].ecosystem_); }
        }
    };
    ```
    (`voices_` is private in `VoragoEngine`, `vorago_engine.h:347`; `ecosystem_` is private in
    `VoragoVoice`, `vorago_voice.h:1514` exposes only a const accessor.)
  - Host: `::Vorago::Processor` via `std::make_unique`, `initialize(nullptr)` →
    `setupProcessing({kRealtime, kSample32, 512, 48000.0})` → `setActive(true)`; drive `process()` with
    `Krate::Test::EventList` / `Krate::Test::ParameterChanges` (this TU is Catch2, but keep the drive
    local; the shared Catch2-free host arrives in T020). Reach the engine with
    `const_cast<Krate::DSP::VoragoEngine&>(*proc->engineForTest())` (`processor.h:116-117`), single
    threaded, after prepare and before the note-on block; apply the knob to all 6 voices through
    `forEachEcosystem`.
  - Stimulus (spec C-6): NoteOn MIDI 36, velocity `100.0f / 127.0f`, at sample 0; block 512; 48 kHz;
    default surface (`A` = 155 s, `vorago_voice.h:320-322`); render to `A + 185` = 340 s. Capture the
    three minutes `M1 = [160, 220]`, `M2 = [220, 280]`, `M3 = [280, 340]` s, stereo.
  - Descriptor and distance: `#include "preset_test_support.h"` (T001b, ruling R-8) and use
    `VoragoTest::describe`, `descriptorDistance` and `meanOf` (§5.4/§5.5); `D(P)` = mean of the three
    minutes. This TU carries **no** copy of the descriptor math.
  - Renders (29): default; seed twin of the default (`kSeedId` 2 → normalized `1/15`); one per
    non-default extreme of each candidate (clamp ends; default in parentheses, all from
    `ecosystem_engine.h:2373-2396` read this session): predation [0,1] (0.55); syncRate [0,0.5] (0 → hi
    only); exchangeRate [0,3] (0.35); crowding [0,0.2] (0.05); forageRate [0,0.05] (0.01); feedRate
    [0,1] (0 → hi only); grazeRate [0,3] (0.75); leakRate [0,1] (0.06); moveRate [0,0.5] (0.20);
    maxSpeed [0.001,0.05] (0.03); kernelSigma [0.01,0.35] (0.03); freqDrift [0,2e-4] (4e-5);
    selfAffinity = `setAffinity(k,k,v)` for all 5 kinds, [−2,2] (−1); crossAffinity = the 20
    off-diagonal entries, [−2,2] (+0.45) (`setAffinity`, `ecosystem_engine.h:707`; range `:237-238`).
    `leakExponent` is excluded (§4.3).
  - Output (printed with `std::printf`, copied by T004 into `compliance.md`): `t0 = d(default, seed
    twin)`; a table sorted by audibility = max `d` over the knob's extremes, columns knob, range,
    default, extreme(s), `d`, `d/t0`, flag `INAUDIBLE` when best `d < 2·t0`, and the E cells the knob
    would contribute per P-4 (`.hi` only when the default is the low range end). Then the proposed
    roster: top 4–6 audible knobs and the P-3 arithmetic `N = 38 + |E-ext|`.
  - Assertions (only these): every render finite by bit pattern and peak ≤ 0.9661; the default render's
    `M1` stereo RMS ≥ −60 dBFS (so the table is meaningful).
- **Verify:** after T003 → build `vorago_tests`, zero warnings; run T004.

## Group 3 — probe registration (sequential; shared CMake list)

### T003 — Register the probe TU (Stage A registration; see "CMake registration" above)

- **Edit:** `plugins/vorago/tests/CMakeLists.txt`.
- **Test first:** before editing, run `vorago_tests.exe "Vorago_EcosystemRuleProbe"` → Catch2 reports no
  matching test (the failing state).
- **Implement:** add `integration/ecosystem_rule_probe_test.cpp` under a new comment
  `# Phase 14 (specs/vorago-phase14-presets-release)` after `integration/preset_browser_test.cpp`
  (`CMakeLists.txt:47`); add it to the `-fno-fast-math -fno-finite-math-only` list (`:99-129`) with the
  comment `# bit-pattern finiteness over renders`.
- **Verify:** build `vorago_tests`, zero warnings; `vorago_tests.exe "Vorago_EcosystemRuleProbe" --list-tests`
  lists the case.

## Group 4 — run the probe and STOP (G1)

### T004 — Run the probe, record it (SC-025)

- **Run (alone, logged):**
  `build/windows-x64-release/bin/Release/vorago_tests.exe "Vorago_EcosystemRuleProbe" > f:/tmp/vorago_probe.log 2>&1`
  (29 × 340 s renders; expect tens of minutes — run in the background and wait for completion).
- **Record:** create `specs/vorago-phase14-presets-release/compliance.md` (if absent) with a section
  "FR-070 / SC-025 — audibility probe" holding the full printed table, `t0`, the log path and the date.
- **Verify:** the case passed (its two assertions), every candidate has a row.

### T005 — GATE G1: STOP and surface for ratification (FR-071, SC-026)

- Present to the user, verbatim from the log: the probe table, the proposed roster `R` (4–6 knobs),
  with the P-3 arithmetic `N = 38 + |E-ext|` for that roster, P-4's one-sided cells, and the §5.8 E-ext
  halfway-margin ruling. P-1, P-2, P-3, P-5, P-6 and P-8 were ruled at the plan stage (R-1…R-5, "Plan
  decisions" above) and the spec amendments are already applied — do not re-surface them.
- Record the user's roster ruling in `compliance.md` ("G1 rulings", date) and write the ratified `R`
  (setter names, ranges, defaults, cells) into `spec.md` C-2.3 and `plan.md` §4.2 as the binding list.
- **Nothing below starts until the user has ratified `R`.** No new ID, no v3 format, no UI control, no
  preset referencing `R` exists before this point.

---

## Stage B — Ratified surface (FR-072…FR-074) + `getTailSamples()` (FR-060)

## Group 5 — DSP forwarders (sequential; `dsp/` headers + DSP test TU)

### T006 — DSP test first: rule-knob forwarders and the broadcast contract (FR-072 dsp, §4.1 Stage B)

- **Edit:** `dsp/tests/unit/systems/vorago_param_surface_test.cpp` (already registered,
  `dsp/tests/CMakeLists.txt:551`, fast-math exemption `:1029`).
- **Write failing tests:**
  - new `TEST_CASE("VoragoVoice_EcosystemRuleForwarders", "[systems][vorago]")`: for each `<Knob>` ∈ R,
    on a heap `VoragoVoice` prepared at 48 kHz: `setEcosystem<Knob>(v)` with `v` = the knob's range
    midpoint → `voice.ecosystem().get<Knob>() == Approx(v).margin(1e-7)`; a NaN built by bit pattern
    (`std::bit_cast<float>(0x7FC00000u)`) leaves the value unchanged; a value above the clamp range
    reads back as the range max; `getEcosystem<Knob>()` equals `ecosystem().get<Knob>()`. For the
    grouped affinities (only if ratified): `setEcosystemSelfAffinity(v)` sets all five diagonal entries,
    `setEcosystemCrossAffinity(v)` all 20 off-diagonal entries (read via `getAffinity(from, to)`,
    `ecosystem_engine.h:721`).
  - extend `VoragoEngine_ApplyVoiceParamsReachesAllSlots` (`:1096`): a `VoragoVoiceParams` with each new
    field set to its range midpoint reaches `getVoice(v).ecosystem().get<Knob>()` for all 6 slots.
  - extend `VoragoEngine_ApplyVoiceParamsDefaultIsNoOp_Short` (`:1086`) / `…DefaultIsNoOp` (`:1092`): no
    change in method; they now cover the new fields automatically once `VoragoVoiceParams{}` carries
    them. Add one assertion: for each new field, `VoragoVoiceParams{}.ecosystem<Knob> ==
    heapEngine->get<Knob>()` on a heap `EcosystemEngine{}` (defaults must equal
    `ecosystem_engine.h:2373-2396`).
- **Verify red:** build `dsp_systems_tests` → compile errors naming the missing forwarders/fields.

### T007 — DSP implement: forwarders, `VoragoVoiceParams` fields, broadcast lines (§4.1 Stage B)

- **Edit:** `dsp/include/krate/dsp/systems/vorago_voice.h`, `dsp/include/krate/dsp/systems/vorago_engine.h`.
- **Implement (append-only):**
  - `VoragoVoice` public, beside `setEcosystemDepthFor` (`vorago_voice.h:1429`), per `<Knob>`:
    ```cpp
    /// Phase 14 FR-072: forwards to EcosystemEngine::set<Knob> (ecosystem_engine.h:<line>).
    /// Early-outs an unchanged value (the applyVoiceParams contract, vorago_engine.h:852-855).
    void setEcosystem<Knob>(float v) noexcept {
        if (!detail::isFinite(v) || v == ecosystem_.get<Knob>()) { return; }
        ecosystem_.set<Knob>(v);
    }
    [[nodiscard]] float getEcosystem<Knob>() const noexcept { return ecosystem_.get<Knob>(); }
    ```
    Grouped affinities (if ratified): self writes `setAffinity(k, k, v)` for the 5 kinds, cross writes
    the 20 off-diagonal entries; getters read `(Partial, Partial)` / `(Partial, Resonator)`; early-out
    compares against those getter values.
  - `VoragoVoiceParams` (`vorago_engine.h:160-191`): append `float ecosystem<Knob> = <engine default>f;`
    per knob after `ecologyLoopFilterMode`, comment citing the `ecosystem_engine.h` default line; update
    `kFieldCount` from 31 to `31 + |R|` and its arithmetic comment (`:188-189`). Do NOT add any field
    naming a `VoragoMacroTarget` (`:152-154`).
  - `applyVoiceParams` (`:859-879`): append `voice.setEcosystem<Knob>(p.ecosystem<Knob>);` inside the
    slot loop.
- **Verify:** build `dsp_systems_tests` (zero warnings); run
  `dsp_systems_tests.exe "VoragoVoice_EcosystemRuleForwarders,VoragoEngine_ApplyVoiceParams*" 2>&1 | tail -3`
  → pass; then the whole per-push filter of `dsp_systems_tests` → pass. `seraphis_tests` is not rebuilt
  (no shared header touched; `EcosystemEngine` itself is not edited).

## Group 6 — new failing plugin tests + new tail helper ([P], new files only)

### T008 [P] — `unit/state_v3_test.cpp` (FR-072, FR-074, SC-027)

- **Create:** `plugins/vorago/tests/unit/state_v3_test.cpp` (new; registered in T029).
- **Write failing tests** (host: `ProcessorFixture` from `vorago_test_fixture.h:166-330`, 48 kHz/512):
  - `Vorago_StateRoundTripV3` `[vorago][state]`: set every `kEcosystem<Knob>Id` to normalized 0.8 via
    `IParameterChanges` in one `process()`; `getState` → stream length `== kStateV3Bytes`, first int32
    `== 3`; `setState` into a fresh processor then `getState` → byte-identical (`memcmp == 0`).
  - `Vorago_State_V2LoadsWithRosterDefaults` `[vorago][state]`: move every knob to normalized 0.9;
    build a v2 stream = int32 2 + the first 424 bytes after the version of a v3 `getState` (i.e. the
    v2 chain, `kStateV2Bytes = 428` total); `setState(v2)` → `kResultOk`; next `getState` decodes (via
    `loadEcosystemParamsV3Ext`) each knob `== Approx(engine default)` (not 0.9).
  - `Vorago_State_V3TruncatedKeepsPrefix` `[vorago][state]`: a v3 stream truncated after byte 428 →
    `kResultOk`, knobs keep their pre-load values.
  - `Vorago_State_V3NonFiniteKnobRejected` `[vorago][state]`: overwrite the first knob float (offset
    428) with NaN bits `0x7FC00000u` → `kResultOk`, that knob unchanged, the others loaded.
  - `Vorago_State_V4Rejected`: version 4 → `kResultFalse`.
- **Waits on:** T011 (IDs/constants), T012 (pack I/O), T014 (processor v3). Red until then.

### T009 [P] — `unit/tail_samples_test.cpp` (FR-060, SC-030)

- **Create:** `plugins/vorago/tests/unit/tail_samples_test.cpp` (new; registered in T029).
- **Write failing test** `TEST_CASE("Vorago_Processor_GetTailSamplesMatchesState", "[vorago][tail]")`
  on a prepared processor at 48 kHz:
  - default surface → `getTailSamples() == 3696000u` (`ceil((45 + 20 + 12) · 48000)`; release 45 s
    `vorago_voice.h:323-324`, decay 20 s `space_params.h:42`, ghost grain 12 s `vorago_engine.h:370`);
  - Depth macro (110) = 1.0 → effective RT60 45 s → `ceil((45 + 45 + 12)·48000)`;
  - Age macro (101) = 1.0 → RT60 6 s → `ceil((45 + 6 + 12)·48000)`;
  - Depth 1.0 + `kSpaceDecayId` at 60 s → RT60 clamped to 60 → `ceil((45 + 60 + 12)·48000)`;
  - `kSpaceFreezeId` = 1.0 → `== Steinberg::Vst::kInfiniteTail`;
  - `::Vorago::kGhostGrainTailSeconds == Approx(proc.engineForTest()->atmosphere().getGrainSeconds())`
    after prepare (`atmosphere_engine.h:855-859`, `vorago_engine.h:1297`);
  - every factory preset (loop over `Vorago::PresetDefs::allPresets()` through `buildComponentState`,
    T020; skip cleanly when empty): reported value equals the §4.9 formula computed from the
    independently decoded state (`decodePresetState`, T022) — exact integer equality.
- **Waits on:** T010 (helper) and T014 (override).

### T010 [P] — `src/processor/tail_estimate.h` (FR-060, §4.9)

- **ODR first:** `grep -rn "effectiveCavernDecaySeconds\|kGhostGrainTailSeconds" dsp/ plugins/ tools/ tests/` → 0 hits.
- **Create:** `plugins/vorago/src/processor/tail_estimate.h` (header-only, processor-side), exactly the
  §4.9 code: `inline float effectiveCavernDecaySeconds(const Krate::DSP::VoragoMacroValues&, float
  storedDecaySeconds) noexcept` (stack `VoragoMacroMatrix`, `setTargetBase(CavernDecaySeconds, …)`,
  `setMacros`, `computeCavernTargets().decaySeconds`, clamp to `[kSpaceDecayMinSeconds,
  kSpaceDecayMaxSeconds]`) and `inline constexpr double kGhostGrainTailSeconds = 12.0;` with the
  `vorago_engine.h:370` citation. The C-6 timeline (T025) uses this same helper, so "effective RT60"
  has one definition.
- **Verify:** through T009 in T030.

## Group 7 — IDs and state constants (sequential)

### T011 — `plugin_ids.h`: roster IDs and v3 constants (FR-072)

- **Edit:** `plugins/vorago/src/plugin_ids.h`.
- **Test first:** T008 is the failing test (does not compile yet).
- **Implement:**
  - in the ecosystem band (`:172-173`): `kEcosystem<Knob>Id = 901, 902, …` in ratification order, each
    commented with its setter and range; they stay `< kEcosystemParamRangeEnd = 1000` (`:236`).
  - `kCurrentStateVersion = 3` (`:23`) with a comment "Phase 14: v3 = v2 + the ecosystem rule block";
    keep `kStateV2Bytes = 428` and its static_assert unchanged (`:28-47`); add
    `inline constexpr std::size_t kNumEcosystemRuleParams = |R|;` and
    `constexpr std::size_t kStateV3Bytes = kStateV2Bytes + 4 * kNumEcosystemRuleParams;` with a
    static_assert on the sum.
- **Verify:** compiles in T012+.

## Group 8 — ecosystem pack (sequential)

### T012 — `ecosystem_params.h` knobs + v3 extension I/O; pack contract test (FR-072, FR-074)

- **Edit:** `plugins/vorago/tests/unit/params/ecosystem_params_test.cpp` (test first), then
  `plugins/vorago/src/parameters/ecosystem_params.h`.
- **Failing test** — extend `Vorago_EcosystemParamsContract` (`ecosystem_params_test.cpp:88`) per knob:
  registered default normalized → plain `== Approx(heap EcosystemEngine{}.get<Knob>())` (margin 1e-6);
  normalized ↔ plain round-trip within 1e-6 at 0, 0.25, 0.5, 1; plain above range clamps to max;
  `formatEcosystemParam` returns `kResultOk` and a non-empty string; `saveEcosystemParamsV3Ext` writes
  exactly `4·|R|` bytes; `loadEcosystemParamsV3Ext` on a NaN-bit field leaves it unchanged and returns
  true; on EOF returns false with fields unchanged.
- **Implement** (shape of `saveGlobalParamsV2Ext`, §4.2): `EcosystemParams` gains
  `std::atomic<float> <knob>{<engine default>f};` per knob; `handleEcosystemParamChange`,
  `registerEcosystemParams` (plain `Parameter`, `kCanAutomate`, default = inverse taper of the engine
  default), `formatEcosystemParam` gain one case each; taper = `linearFromNormalized` over the setter's
  clamp range, or `Krate::Plugins::logMapFromNormalized`/`logMapToNormalized` for `kernelSigma`
  [0.01, 0.35] and `maxSpeed` [0.001, 0.05]; new `saveEcosystemParamsV3Ext`,
  `loadEcosystemParamsV3Ext` (EOF-safe, `isFinite` + clamp), and
  `loadEcosystemParamsV3ExtToController<SetParamFunc>`. The existing 4-byte `saveEcosystemParams`
  slot is untouched (v2 layout unchanged).
- **Verify:** build `vorago_tests`; `vorago_tests.exe "Vorago_EcosystemParamsContract"` → pass.

## Group 9 — routes and param-table (sequential)

### T013 — `param_routes.h` + param-table expectations (FR-074)

- **Edit:** `plugins/vorago/tests/unit/param_table_expected.h` and `unit/param_table_test.cpp` (test
  first), then `plugins/vorago/src/parameters/param_routes.h`.
- **Failing test:** add one expected row per new ID (name, unit, default normalized, stepCount 0, flags
  `kCanAutomate`, route VP) so `Vorago_ParamIdMap` (`param_table_test.cpp:53`), `Vorago_RouteTable`
  (`:326`), `Vorago_ParameterInfoTable` (`:607`) and `Vorago_ParamInputHygiene` (`:859`) fail on the
  missing rows/count.
- **Implement:** `std::array<ParamRouteEntry, 108>` (`param_routes.h:36`) → `108 +
  kNumEcosystemRuleParams`; one `{kEcosystem<Knob>Id, Route::VP}` row per knob in ascending ID order
  after `{kEcosystemDepthId, Route::MB}` (`:115`); `countRoute(Route::VP) == 31` (`:281`) →
  `31 + kNumEcosystemRuleParams`; the header comment "108" (`:6`, `:162`) updated. VP is correct: the
  knobs are per-voice, broadcast, never macro targets.
- **Verify:** after T014, `vorago_tests.exe "Vorago_Param*,Vorago_RouteTable"` → pass.

## Group 10 — processor (sequential)

### T014 — Processor: VP push, state v3, `getTailSamples()` (FR-072, FR-060)

- **Edit:** `plugins/vorago/src/processor/processor.h`, `plugins/vorago/src/processor/processor.cpp`.
- **Failing tests:** T008 and T009 (registered in T029), and the updated count tests of T015/T016.
- **Implement:**
  - `pushVoiceParams()` (`processor.cpp:826-863`): copy each knob atomic into `p.ecosystem<Knob>`
    before `engine_->applyVoiceParams(p)` (`:862`).
  - `getState()` (`:637-661`): append `saveEcosystemParamsV3Ext(ecosystemParams_, s);` after
    `saveLifeParams` — the v3 block follows byte 428, never inside the mid-stream ecosystem slot.
  - `setState()` (`:569-635`), per §4.2: `version > 3` → `kResultFalse` (unchanged rule via
    `kCurrentStateVersion`); `version >= 3 && v2Complete` → `loadEcosystemParamsV3Ext`; `version < 3` →
    serialize `EcosystemParams{}`'s v3 block into a stack `std::array<char, 4 * kNumEcosystemRuleParams>`
    `MemoryStream` and load it (the `:603-626` pattern), so every knob is at its hard-coded default; the
    `version < 2` branch then also takes this default v3 block.
  - `getTailSamples()` override declared in `processor.h` (replace the comment at `:89`): freeze on →
    `kInfiniteTail`; else `ceil((releaseMs/1000 + effectiveCavernDecaySeconds(knob macros, decaySeconds)
    + kGhostGrainTailSeconds) · processSetup.sampleRate)`; reads relaxed atomics only; macro input is the
    twelve knobs (no channel pressure — no Pressure row targets the decay, `vorago_macro_matrix.h:492-548`).
- **Verify:** build `vorago_tests` and `Vorago`, zero warnings; T008/T009 verified in T030.

## Group 11 — controller (sequential)

### T015 — Controller v3 state mirror (FR-072, FR-074)

- **Edit:** `plugins/vorago/src/controller/controller.cpp` (`applyStateStream`, `:180-235`).
- **Test first:** the failing test is the controller arm T016 adds to `Vorago_StateRoundTrip`
  (`unit/state_roundtrip_test.cpp:110`); T008 owns `state_v3_test.cpp`.
- **Implement:** after `loadV2Tail(streamer)` for `version >= 3` call
  `loadEcosystemParamsV3ExtToController(streamer, setParam)`; for `version < 3` feed the
  default-constructed `EcosystemParams{}` v3 block through the same inverse map (mirror of the
  processor, like the `:212-233` v1 path). Registration needs no edit (it goes through
  `registerEcosystemParams`).
- **Verify:** build `vorago_tests`; `vorago_tests.exe "Vorago_StateRoundTrip*"` → pass after T016.

## Group 12 — count-bearing and state tests (sequential)

### T016 — Update every count/size-bearing test to named constants (FR-074)

- **Edit:** `plugins/vorago/tests/unit/state_roundtrip_test.cpp`, `unit/state_v2_test.cpp`, and each
  file the sweep finds.
- **Sweep first:** `grep -rn "108\b\|106\b\|\b90u\|428\|kStateV2Bytes\|kCurrentStateVersion" plugins/vorago/tests`
  (24 hits for the two constants this session) and treat every hit.
- **Implement:**
  - `state_roundtrip_test.cpp`: `getSize() == kStateV2Bytes` and `memcmp(…, kStateV2Bytes)` on the
    CURRENT stream → `kStateV3Bytes`; the version check reads 3; the `kCurrentStateVersion + 1`
    rejection cases stay and now reject 4; header comment names v3; add a controller arm asserting
    `setComponentState(v3)` sets each `kEcosystem<Knob>Id` to the stored normalized value (±1e-6).
  - `state_v2_test.cpp`: `Vorago_StateRoundTripV2` (`:388`) becomes the legacy-v2 load test (v2 streams
    still load, `kResultOk`, and the knobs read their defaults); its current-stream round trip moves to
    T008's `Vorago_StateRoundTripV3`.
  - `param_denorm_test.cpp`, `automation_rt_test.cpp`, `continuity_test.cpp`, `ecosystem_frame_test.cpp`,
    `param_surface_test.cpp`, `preset_browser_test.cpp`, `processor_cpu_test.cpp`, `soak_test.cpp`,
    `body_params_test.cpp`: every 108/106/428 literal tied to the surface becomes
    `108 + kNumEcosystemRuleParams` / `kStateV3Bytes` etc. — never a new literal.
- **Verify:** build `vorago_tests`, zero warnings; per-push filter run of `vorago_tests` → all pass
  except the UI counts T017 fixes (list any other failure and fix it — none is "pre-existing").

## Group 13 — ecosystem page UI (sequential)

### T017 — Controls for R on page 6 + layout tests (FR-073, SC-028)

- **Edit:** `plugins/vorago/tests/unit/controller/editor_layout_test.cpp` (test first), then
  `plugins/vorago/resources/editor.uidesc` via an XSLT stylesheet run with `xslt3` (project rule for
  uidesc changes; stylesheet in the scratchpad, not committed).
- **Failing test:**
  - `kIdNames` (`:109`) size → `108 + kNumEcosystemRuleParams`, one row per new ID;
  - `pages[6]` (`:253-256`) inserts each new ID;
  - counts `all108` (`:838`), `nonHidden`/`boundIds`/`tagMap`/`checked` 106 (`:839`, `:873`, `:922`,
    `:1008`), `expectedName` 108 (`:909`), `pageUnion 90u` (`:1127`), `labelledControls 12u + 90u`
    (`:1176`), `getParameterCount() == 108` (`:1305`, `:1307`, `:1348`) → named constants
    `+ kNumEcosystemRuleParams`;
  - new `TEST_CASE("Vorago_Ecosystem_PageBindsRosterIds", "[vorago][ui]")`: page 6's bound-ID set
    contains every `kEcosystem<Knob>Id`; the unreachable-parameter allowlist is still exactly `{4, 5}`.
- **Implement:** per knob `k` (0-based in ratification order) one `<control-tag name="Ecosystem<Knob>"
  tag="90x"/>` beside `:111`, and one 44×44 `ArcKnob` in page-6 row r0 at `origin = (154 + 68·k, 4)`
  with a label at `(142 + 68·k, 52)`, attributes copied from the EcosystemDepth knob (`:387`),
  tooltip describing the rule.
- **Verify:** build `vorago_tests`; `vorago_tests.exe "Vorago_Uidesc*,Vorago_Ecosystem_PageBindsRosterIds,Vorago_Controller*"`
  → pass; per-push filter of `vorago_tests` → all pass.

---

## Stage C — Preset infrastructure (FR-001, FR-018…FR-035, FR-039, FR-040)

## Group 14 — category set (sequential)

### T018 — Seven fixed categories (FR-001, SC-001)

- **Edit:** `plugins/vorago/tests/unit/controller/editor_lifecycle_test.cpp` (test first), then
  `plugins/vorago/src/preset/vorago_preset_config.h`.
- **Failing test:** `editor_lifecycle_test.cpp:117-119` expect
  `{"Drones","Abyss","Caverns","Organisms","Machines","Textures","Ghosts"}` and tab labels
  `{"All", …those seven}`; the `Drones` probe-dir lines (`:133-161`) stay.
- **Implement:** line `:29` → `/*.subcategoryNames  =*/{"Drones", "Abyss", "Caverns", "Organisms",
  "Machines", "Textures", "Ghosts"}`; banner notes the set is fixed and additive-only. Nothing else
  (`makeVoragoPresetTabLabels`, `:36-44`, derives from it). Do not create directories by hand; the
  generator creates them (T035).
- **Verify:** build `vorago_tests`; `vorago_tests.exe "*Editor*Lifecycle*"`-matching cases pass (use
  `--list-tests` to get the exact names in `editor_lifecycle_test.cpp`).

## Group 15 — definitions, host ([P], new files only)

### T019 [P] — `tools/vorago_preset_defs.h` (FR-010, FR-022, FR-075, §4.5)

- **ODR first:** `grep -rn -E "(class|struct|enum class) (VoragoPresetDef|CellSpec|CapabilityGroup|Capability|Verification)\b" dsp/ plugins/ tools/ tests/`
  → 0 hits (`ParamSetting` has one hit in `Seraphis::PresetDefs`, `tools/seraphis_preset_defs.h:59`;
  different namespace/target, never included together).
- **Create** (data only; rules of `seraphis_preset_defs.h:1-40`: every function `inline`, tables are
  function-local `static const`, only project include `plugin_ids.h` plus std), namespace
  `Vorago::PresetDefs`, exactly the §4.5 declarations: `ParamSetting{ParamID id; double normalized;}`,
  `CapabilityGroup {S, M, E, D}`, `Capability` with S1…S10, M1…M12, E1…E5, **then the ratified E-ext
  cells in ratification order (`E6Lo`, `E6Hi`, … ; one-sided knobs contribute `.Hi` only, P-4)**, then
  the D cells in §4.5 order, then `Count`; `Verification {Ablation, RouteIsolated, StateWithS,
  StateWithReversion, StateAlwaysAudible, FreezeFloor}`; `CellSpec` and `cellSpecs()` filled with every
  cell's group, label, verification, ablation override (§5.6 table; S5 = 610, 611, 612 → 0.0 and 600 →
  0.0; M m = macro `100+m−1` → 0.0, Gravity 104 → 0.5; E-ext = knob → its registered default
  normalized; D13 = 800 → 0.5; D14 = 1500/1502 → 0.0), `sConjunct` (§5.8) and `extSide`;
  `VoragoPresetDef{name, category, description, primary, secondaries, params}`;
  `allPresets()` returning an EMPTY vector for now (T031/T034 fill it);
  `kCategories` = the seven C-1 names; `buildVoragoInfoXml(name, category, description)` writing the
  exact bytes of `preset_manager.cpp:264-275` with `MusicalCategory == MusicalInstrument == category`
  and the `Comment` line only for a non-empty description.
- **Verify:** compiled by T020/T022/T023 in T030.

### T020 [P] — `plugins/vorago/tests/vorago_preset_host.h` — Catch2-free host (FR-021, §4.6)

- **ODR first:** `grep -rn -E "(class|struct) PresetHost\b" dsp/ plugins/ tools/ tests/` → 0 hits.
- **Create** header-only, NO Catch2 include (the generator does not link Catch2; any Catch2 dependency
  fails T029's generator build), namespace `VoragoTest`, the §4.6 `PresetHost` API verbatim:
  `PresetHost()` (`std::make_unique<::Vorago::Processor>()`), `~PresetHost()` (`setActive(false)` if
  active, `terminate()`), `prepare(double sr, int32 maxBlock)` (initialize → setupProcessing
  `{kRealtime, kSample32, maxBlock, sr}` → setActive; sizes output buffers once), `process(n, IEventList*,
  IParameterChanges*)` (n ≤ maxBlock), `loadState(span<const uint8_t>)`, `saveState(vector<uint8_t>&)`,
  `outL()`, `outR()`, `processor()`; and
  `bool buildComponentState(const Vorago::PresetDefs::VoragoPresetDef&, std::vector<uint8_t>& comp,
  std::string& why)` = prepare(48000, 512) → ONE `process(512)` carrying every `def.params` point at
  offset 0 → `saveState`. Reject (with `why`) any non-finite or out-of-[0, 1] value and any point for
  `kSustainPedalId` (4) or `kChannelPressureId` (5). Uses `Krate::Test::ParameterChanges`/`EventList`.
- **Verify:** in T030 (compiled by the generator AND the test TUs).

## Group 16 — generator and harness part 1 ([P], new files only)

### T021 [P] — `tools/vorago_preset_generator.cpp` (FR-018, FR-019, FR-021, FR-023, FR-025, §4.7)

- **Create**, following `tools/seraphis_preset_generator.cpp:86-349` minus its partials block:
  `void* moduleHandle = nullptr;` (`:86` precedent); class id from `Vorago::kProcessorUID.toString(buf)`,
  checked to be 32 chars; output base = `argv[1]` (default `plugins/vorago/resources/presets`);
  create the seven `kCategories` directories; iterate `allPresets()` in definition order; per def
  `VoragoTest::buildComponentState`, then `writeVstPreset` = the 48-byte header + `Comp` + `Info` +
  `List` layout of `seraphis_preset_generator.cpp:212-265`, duplicated in this TU's anonymous namespace
  (the Seraphis tool is not edited), `Info` from `buildVoragoInfoXml`; exit 1 on any failure; no
  timestamp, no directory iteration, no RNG. Includes: `vorago_preset_defs.h`, `vorago_preset_host.h`.
- **Verify:** T030 builds `vorago_preset_generator` on MSVC; T052 compiles it under g++ (WSL).

### T022 [P] — `plugins/vorago/tests/preset_test_support.h` part 1 (FR-028…FR-031, FR-035, FR-011a, §4.8)

- **ODR first:** `grep -rn -E "(class|struct) (PresetFile|DecodedPresetState|CellResult|VerificationVector)\b" plugins/vorago tools tests dsp`
  → 0 hits in Vorago (Seraphis's `DecodedPresetState` is `SeraphisTest::`, other target).
- **Edit** (the header exists since T001b, ruling R-8; no other Group 16 task touches it) — add,
  Catch2-free, namespace `VoragoTest`, the Stage-C rows of the §4.8 table:
  - `factoryPresetRoot()` = `VORAGO_RESOURCES_DIR "/presets"`; `allPresetFiles()` sorted;
  - `PresetFile parseVstPreset(const std::filesystem::path&)` (magic `VST3`, version, 32-char class id,
    list offset in bounds, `Comp` and `Info` present, all offsets/sizes in bounds — FR-028);
  - `parseInfoAttributes(std::string_view)` → map of the six attributes + `Comment`;
  - `DecodedPresetState` (non-copyable; out-param) and `bool decodePresetState(span, DecodedPresetState&)`
    calling the shipped `load*Params` in `getState()` order (`processor.cpp:643-659`) then
    `loadEcosystemParamsV3Ext`, returning false unless `bytesConsumed == kStateV3Bytes` and version == 3;
  - `CellResult`, `VerificationVector`, and the pure `findWitness(const VerificationVector& p,
    const VerificationVector& q)` implementing §5.10's one rule (Q verifies `c` iff Q's entry passes at
    the secondary bar 1.5, plus the primary bar F only when `c == Q.primary`; scan P's primary then
    secondaries in order; the first unverified claim is the witness; none → SUBSET);
  - `normalizedVector(comp)` for §5.12 via a single `::Vorago::Controller` instance
    (`setComponentState` then `getParamNormalized` for every registered ID except 4 and 5) and
    `countDifferingIds(a, b)` (list IDs: `stepCount > 0`, compare `round(n·stepCount)`; continuous:
    `|Δn| ≥ 0.10`);
  - `kRecordedDefaultStateCells` placeholder = the P-3 prediction {D1StoneChamber, D1SteelTank,
    D3Direct, D3FilteredWind, D3GranularDust, D3MetallicHiss, D8Standard, D9SlowAttack, D10FreezeOff,
    D12TriggersOff} (corrected from the measured set in T033).
- **Verify:** via T023/T024 in T030.

## Group 17 — per-push preset tests ([P], new files only)

### T023 [P] — `unit/preset/factory_preset_test.cpp` (SC-001…SC-006, SC-009, FR-011 static, FR-075)

- **Create** (registered in T029). All `[vorago][preset]` (per-push). Write every case failing first:
  - `Vorago_FactoryPresets_CategoriesMatchConfig`: `makeVoragoPresetConfig().subcategoryNames ==
    kCategories` in C-1 order, `"Drones"` first; directories under `factoryPresetRoot()` == that list
    both ways; no file other than `.vstpreset` directly in a category dir, and no file at the root.
  - `Vorago_FactoryPresets_ContainerAndInfo`: every file passes `parseVstPreset`; class id ==
    `kProcessorUID` string; `Info` bytes == `buildVoragoInfoXml(stem, dir, def.description)`; the six
    attributes + `Comment` parse; no `"` `&` `<` `>` in any `Comment` (P-7).
  - `Vorago_FactoryPresets_RoundTrip`: `setState(Comp)` → `kResultOk`; `getState` byte-identical.
  - `Vorago_FactoryPresets_BrowserScan`: `PresetManager(config, nullptr, nullptr, tempUserDir,
    factoryRoot)` (`preset_manager.h:55-61`) → `scanPresets()` count == `allPresets().size()`; all
    `isFactory`; none with an empty `subcategory`; `getPresetsForSubcategory(c)` == defs count per `c`;
    tab labels == `{"All"} ∪ names`.
  - `Vorago_FactoryPresets_StreamShape`: per file version == `kCurrentStateVersion` (3), length ==
    `kStateV3Bytes` (P-2 ruling); names unique, ASCII, `PresetManager::isValidPresetName` true;
    polyphony index ≤ 3; `A ≤ 180 s`, `Rel ≤ 60 s` (decoded); every stored float finite by bit pattern.
  - `Vorago_FactoryPresets_LibraryShape`: N within the band ruled at G1 (P-3); ≥ 3 presets per category;
    every S, M, E and E-ext cell, each non-default-state D1 material, D8.2 and D9.1 is some def's primary.
    *(Split from StreamShape so that only this case is red while the library is partial — T030 through T035.)*
  - `Vorago_PresetDefs_ClaimsWellFormed`: unique primaries; no primary ∈ `kRecordedDefaultStateCells`;
    every claim `< Capability::Count`; every E-ext `CellSpec` has `extSide ∈ {−1, +1}` and
    `StateWithReversion`; D10.1 never a primary (P-8).
  - `Vorago_PresetDefs_EExtSidesExclusive`: per knob with both sides, a hand-built decoded state at
    `n₀ + δ_hi` (δ_hi = 0.5·(1 − n₀)) passes `.hi`'s side predicate and fails `.lo`'s; the mirror
    `n₀ − δ_lo` (δ_lo = 0.5·n₀) passes only `.lo`; `n₀` passes neither; with a synthetic reversion
    `d = F + 1`, the evaluator verifies exactly one of the pair.
  - `Vorago_PresetMatrix_NonSubsetRule`: `findWitness` on hand-built vectors — P's only claim X (P's
    primary, d = F + 1); Q holds X at d = 2.5 (1.5 < d < F), `2·s = 1.0`, all conjuncts true → SUBSET;
    Q's d = 1.0 → witness X; Q's d = 2.5 with `2·s(Q) = 3.0` → witness X.
  - `Vorago_FactoryPresets_TreeMatchesGenerator`: per def, `buildComponentState` vs the committed file:
    same file set/paths; `Info` byte-identical; version and length equal; every int32 field equal; every
    float within `kTreeFloatRelTol` (placeholder `1.19e-7` until T037 pins the measured value); print the
    per-field worst relative difference.
  - `Vorago_FactoryPresets_TreeToleranceProbe` `[.measure]`: prints per float field the worst
    `|c − r| / max(|c|, 1e-30)` (§5.13); no assertion beyond parse success.
  - `Vorago_PresetMatrix_ParameterSpaceDistinct`: every pair and every preset vs the default surface
    has `countDifferingIds ≥ 8`; print the minimum count and its pair.
- **Waits on:** T019, T020, T022; files from T035.

### T024 [P] — `integration/preset_load_rt_test.cpp` (FR-039, FR-040, SC-016)

- **Create** (registered in T029), `[vorago][preset]`:
  - `Vorago_FactoryPresets_SequentialLoadNoAlloc`: warm `ProcessorFixture` (48 kHz/512, NoteOn 36
    held); prebuild every preset's `Comp` outside the scope; per preset `setState`, then
    `TestHelpers::AllocationScope` around the next 4 `process()` calls → count == 0.
  - `Vorago_FactoryPresets_ConcurrentLoadIsRtSafe`: streams prebuilt; a `std::thread` message thread
    loops `setState` over all presets until stopped (every call `== kResultOk`, counted in an atomic);
    the test thread renders 4 s (375 blocks) under `TestHelpers::ThreadScopedAllocationScope` → 0;
    output finite by bit pattern and peak ≤ 0.9661. The global `AllocationScope` is forbidden here.
- **Waits on:** T020, T022 and presets on disk.

## Group 18 — harness part 2 (sequential; edits T022's header)

### T025 — `preset_test_support.h` part 2: timeline, render, descriptor, verification, records (§4.8, §5.1–§5.11)

- **Edit:** `plugins/vorago/tests/preset_test_support.h`.
- **ODR first:** `grep -rn -E "(class|struct) (SweepTimeline|RenderSpec|SweepCapture|SweepRecord|Shard)\b" plugins/vorago tools tests dsp`
  → 0 hits in Vorago (Seraphis's `SweepTimeline` is `SeraphisTest::`).
- **Implement** exactly as §4.8 / §5:
  - `SweepTimeline makeTimeline(const DecodedPresetState&, bool freezeGesture)` (§5.1; RT60 via
    `effectiveCavernDecaySeconds`, T010; Freeze-Off `Tail = [H+Rel+RT60+5, H+Rel+RT60+15]`, Freeze-On
    `[H+Rel+10, H+Rel+70]`; seconds → samples `llround(t·sr)`);
  - `RenderSpec` / `SweepCapture` / `renderPreset(const RenderSpec&)`: streaming over `PresetHost`,
    NoteOn 36 @ `100/127` at 0, NoteOff at `round(H·sr)` with its in-block offset, overrides as
    offset-0 points in block 0, freeze gesture = `kSpaceFreezeId` → 1.0 at offset 0 of the block
    starting at `floor(round((A+65)·sr)/512)·512`; per-block bit-pattern finiteness, stereo peak and
    512-sample power sums; samples copied only inside `capture` windows (≤ ~69 MB);
  - windows/RMS (§5.2) and the four arms (§5.3) as pure functions returning their numbers;
  - `selfDistance` (the descriptor, `describe`, `descriptorDistance` and `meanOf` are already in this
    header from T001b — ruling R-8 — and are not redefined);
  - cell evaluators (§5.6–§5.9): ablation twins, route-isolated E (12 renders), D predicates on the
    decoded state, E-ext side predicates with the G1-ruled margin, and the exact skip rules, each skip
    recorded with its reason;
  - `SweepRecord`, `writeRecord`/`readRecord` (text `key value…` lines, doubles `%.17g`; transient CI
    artifacts, never committed — not goldens);
  - `Shard shardFromEnv()` (`VORAGO_SWEEP_SHARD=i/n`, default `0/1`, malformed → reported to caller),
    `inShard(defIndex, shard)` (index mod n; index N is the default-surface pseudo-preset);
    `readEnv` (`_dupenv_s` under `_MSC_VER`); `runJobs(jobs, threads)` (`std::thread`; threads =
    `VORAGO_SWEEP_THREADS` or `min(hardware_concurrency, 4)`; jobs never call Catch2 macros);
    `sweepRecordFor(defIndex)` memo (loads from `VORAGO_SWEEP_IN` if set, writes to `VORAGO_SWEEP_OUT`
    if set);
  - `inline constexpr double kDistinctFloor = 4.0;` (frozen at G2, T033), `kSecondaryBar = 1.5`.
- **Verify:** via T026/T027/T028 in T030.

## Group 19 — sweep, matrix, CPU test TUs ([P], new files only)

### T026 [P] — `integration/preset_sweep_test.cpp` (FR-033, FR-033a, FR-034, FR-037, FR-038)

- **Create** (registered in T029):
  - `Vorago_PresetSweep_ShortBounded` `[vorago][preset]` (per-push, SC-014): per preset 8 s @ 48 kHz,
    4 s @ 44.1 kHz, 4 s @ 96 kHz single note, and 8 s of `{36, 40, 43, 47}` @ 48 kHz with polyphony forced
    to 4 (`kPolyphonyId` → 3/5): finite by bit pattern and peak ≤ 0.9661f; 2-thread `runJobs`;
    assertions on the test thread.
  - `Vorago_PresetSweep_LongRender` `[vorago][preset][long][vorago-sweep]` (SC-012, SC-024): per shard
    preset, the C-6 render; arm 1 finite + peak ≤ 0.9661 + every 10 s window ≤ −6 dBFS over `[0, Total]`;
    arm 2 every 10 s window ≥ −60 dBFS over `[A, H]`; arm 3 `ΔdB ∈ [−18, +12]`; arm 4 Freeze-Off
    `RMS(Tail) ≤ RMS(Sus) − 40`, Freeze-On (the D10.1 gesture preset) `last ≤ loudest + 1.0`,
    `last ≥ loudest − 6.0`, `loudest ≥ RMS(Sus) − 20`; prints A, Rel, RT60 and every arm's number.
  - `Vorago_PresetSweep_SustainAtAllRates` `[…][long][vorago-sweep]` (SC-022): `[0, A+65]` at 44.1 and
    96 kHz, arm 1 only.
  - `Vorago_PresetSweep_AblationVerifiesClaims` `[…][long][vorago-sweep]` (SC-011): the full vector per
    preset (every S/M, every ablation-D, E1…E5 route sets + `R_∅`, every E-ext cell); every claim
    verified at its role's bar (primary `max(F, 2·s)`, secondary `max(1.5, 2·s)`; E also
    `≥ d(R_∅, R_∅⁰) + 1.5`); record written.
  - `Vorago_PresetSweep_RendersAreReproducible` `[…][long][vorago-sweep]` (SC-015): two fresh hosts on two
    threads over `[0, A+65]`, `compareFingerprints(...).withinTolerance()` per channel.
  - The default-surface pseudo-preset (index N) runs in its shard through the D predicates only.
- **Waits on:** T025.

### T027 [P] — `integration/preset_matrix_test.cpp` (FR-011a, FR-013, FR-015, FR-036, FR-017a)

- **Create** (registered in T029):
  - `Vorago_PresetMatrix_CoverageComplete` `[vorago][preset][long][vorago-sweep][vorago-aggregate]`
    (SC-008, SC-018, SC-028, SC-029): from records (§5.10): REQUIRE a record for every def index and N;
    every cell has a factory-preset verifier (index < N); E1…E5 each the verified primary of a distinct
    preset; no primary in the measured default-state set; measured set == `kRecordedDefaultStateCells`;
    ≥ 1 verified D11 and D12.1; prints the full preset × cell matrix (primary / secondary /
    claimed-failed / unclaimed, with skip reasons), the default-state cells.
  - `Vorago_PresetMatrix_NoShowcaseSubset` (same tags): `findWitness` for every ordered pair; prints
    `P vs Q: witness <cell>` or `SUBSET`; zero SUBSET.
  - `Vorago_PresetSweep_SoundSpaceDistinct` (same tags, SC-010): every pair `d ≥ max(F, 2·max(s),
    2·t_max)`; control set `C` (argmax/argmin `s(P)`, the D10.1 preset, highest stored Pressure, highest
    stored Weight; deduplicated, ≥ 3): (a) level twin `d < 0.05` for every preset (from records),
    (a′) gain twin on the highest-Pressure preset (`kMasterGainId` → 0.5·stored) `d ≤ F/2`, (b) seed
    twins (`(i+1) mod 16`, normalized `/15`) `d ≤ F/2`, `t_max` = max of these, (c) sub twins (600 at
    stored ± 0.125; infeasible side reported, the other required) `d < F`; prints min/median/max `d`,
    the minimum pair's names, every `s(P)`, `t_max`, every control's `d`, the effective floor.
  - `Vorago_PresetPilot_Calibrate` `[.probe][vorago]` (FR-017a): renders the pilot defs + a pilot-only
    near-variant of Tectonic Floor defined in THIS TU (sub offset +6 dB + one section tweak; never in
    `allPresets()`), measures seed-twin `t_max`, the near-variant pair's `d`, every pilot `s(P)`; prints
    the §5.14 ruling inputs; asserts only finiteness.
- **Waits on:** T025.

### T028 [P] — `integration/preset_cpu_test.cpp` (FR-041, SC-017)

- **Create** (registered in T029; NOT in the fast-math exemption list, like `processor_cpu_test.cpp`):
  `TEST_CASE("Vorago_PresetCpu", "[vorago][.perf][performance]")` per §5.15: per preset host at 48k/512,
  `setState`, `kPolyphonyId` → 3/5, NoteOn `{36, 40, 43, 47}` at 0, untimed pre-roll to `A + 5 s`; one
  default-surface host pre-rolled to 160 s (re-rolled before leaving its `Sus`); 16 trials × 100 blocks,
  preset and default blocks interleaved, `std::chrono::steady_clock`; REQUIRE `min-trial(preset) /
  min-trial(default) ≤ 1.15` for the worst preset; print absolute ns vs `kReferenceNs` (3 200 000,
  `#include VORAGO_PERF_BUDGET_HEADER`) and the stored-polyphony figure (not gated).
- **Waits on:** T025.

## Group 20 — registration (sequential; the single registration of Stages B–D)

### T029 — Register every new TU and the generator targets

- **Edit:** `plugins/vorago/tests/CMakeLists.txt`, root `CMakeLists.txt`.
- **Test first:** `vorago_tests.exe --list-tests` currently lacks every case of T008/T009/T023–T028
  (the failing state); `vorago_preset_generator` is not a target.
- **Implement:**
  - vorago_tests list, under the Phase 14 comment from T003: `unit/state_v3_test.cpp`,
    `unit/tail_samples_test.cpp`, `unit/preset/factory_preset_test.cpp`,
    `integration/preset_sweep_test.cpp`, `integration/preset_matrix_test.cpp`,
    `integration/preset_load_rt_test.cpp`, `integration/preset_cpu_test.cpp`.
  - `-fno-fast-math -fno-finite-math-only` list: all of those except `preset_cpu_test.cpp`.
  - `target_include_directories(vorago_tests …)` gains `${CMAKE_SOURCE_DIR}/tools` (the defs header).
  - root `CMakeLists.txt`, after `generate_seraphis_presets` (`:664-669`): a `vorago_preset_generator`
    block copied from `:604-662` with sources `tools/vorago_preset_generator.cpp`,
    `plugins/vorago/src/processor/processor.cpp`, the same five SDK sources, and
    `plugins/vorago/tests/vstgui_test_stubs.cpp` (defines only `GetPluginFactory`, read this session);
    `target_link_libraries(… PRIVATE KrateDSP KratePluginsShared sdk)` (no vstgui_support); include dirs
    `plugins/vorago/src`, `plugins/vorago/tests`, `tests/test_helpers`, `tools`, `${vst3sdk_SOURCE_DIR}`;
    `cxx_std_20`; `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"` (fixed by `release.yml`); MSVC
    `/W4 /permissive- /Zc:__cplusplus /wd4100 /wd4458` (add `/wd4459` only if the first build emits
    C4459), else `-Wall -Wextra -Wpedantic -Wno-unused-parameter`; and
    `add_custom_target(generate_vorago_presets COMMAND vorago_preset_generator
    "${CMAKE_SOURCE_DIR}/plugins/vorago/resources/presets" DEPENDS vorago_preset_generator COMMENT
    "Generating Vorago factory presets" VERBATIM)`. If the Linux link later needs `dataexchange.cpp`
    (processor `connect()`), add that SDK source explicitly (checked in T052).
- **Verify:** reconfigure (`--preset windows-x64-release`), then T030.

## Group 21 — build and verify Stages B–D (sequential)

### T030 — Clean build + per-push verification

- Build `dsp_systems_tests`, `vorago_tests`, `vorago_preset_generator`, `Vorago` — **zero warnings**
  (fix every one; none is "pre-existing").
- Run `vorago_tests.exe "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" > f:/tmp/v14_push.log 2>&1`.
  Expected: everything green **except** `Vorago_FactoryPresets_LibraryShape` (no defs yet). All
  Stage B cases (T008, T009, T012, T013–T017) green; the preset cases pass vacuously on an empty
  library only where that is meaningful (container/round-trip over zero files) — confirm each prints
  its count.
- `dsp_systems_tests` per-push filter → green. Record the results in `compliance.md`.

---

## Stage E — Pilot and gate G2 (FR-017a)

## Group 22 — pilot defs (sequential)

### T031 — Author the seven pilot presets (§5.14, §6)

- **Edit:** `tools/vorago_preset_defs.h` (`allPresets()`).
- **Test first:** `Vorago_PresetDefs_ClaimsWellFormed` and `…ParameterSpaceDistinct` are the gates for
  the new rows (run after T032's generation).
- **Implement:** Tectonic Floor (S5, Abyss), Cathedral Void (S8 + D10.1, Caverns; Freeze stored Off,
  `Comment` tells the player to engage Freeze once the drone has bloomed), Pressure Front (M7,
  Machines), Teeming (M10 + D13.1, Organisms), Singing Colony (E2 + D5.1, Organisms), Glass Well (D1
  Glass, Caverns), Growth Ring (D8.2, Organisms). Every row: polyphony index ≤ 3; `A ≤ 180`,
  `Rel ≤ 60`; Freeze stored Off; no point for ID 4 or 5; output saturation only via its MB base; no
  `" & < >` in descriptions; normalized values in [0, 1].

## Group 23 — generate pilot tree (sequential)

### T032 — Generate and verify the pilot tree

- Build and run `generate_vorago_presets`; delete `resources/presets/Drones/.gitkeep` only if `Drones`
  now holds a preset (it does not in the pilot — leave it).
- Run the per-push filter (log) → green except `Vorago_FactoryPresets_LibraryShape`.

## Group 24 — calibrate and STOP (G2)

### T033 — Pilot measurement, default-state set, freeze F (FR-017a, FR-012)

- Run alone, logged: `vorago_tests.exe "Vorago_PresetPilot_Calibrate" > f:/tmp/v14_pilot.log 2>&1`, and
  the default-surface pseudo-preset's D predicates (`VORAGO_SWEEP_SHARD` covering index N) through
  `Vorago_PresetSweep_AblationVerifiesClaims`.
- Rule `F = max(4.0, 2·t_max)`; if the near-variant pair's `d ≥ F`, raise F to just above it and record
  that ruling. **STOP if any seed twin `> F/2`** (C-7.3(b)) — surface the numbers; never lower F.
- Edit `preset_test_support.h`: `kDistinctFloor` = the ruled F; `kRecordedDefaultStateCells` = the
  measured default-state set (if it differs from the P-3 prediction, re-derive §6's primary list and
  default-state secondaries before T034).
- Record in `compliance.md`: `t_max`, near-variant `d`, every pilot `s(P)`, the frozen F, the measured
  default-state set, log paths. Report G2 to the user (a stop only on the C-7.3(b) breach).

---

## Stage F — Matrix-derived library (FR-016, FR-004, FR-042 prep)

## Group 25 — author the library (sequential)

### T034 — Derive and author the full library (§6)

- **Edit:** `tools/vorago_preset_defs.h`.
- **Derivation, in order (FR-016):** enumerate cells (C-2.1 + ratified E-ext); take the measured
  default-state set (T033); every S, M, E, E-ext cell = its own preset's primary, plus the
  non-default-state D1 materials, D8.2, D9.1; attach every remaining cell (D2, D4.x, non-default D5–D7,
  D10.1, D11, D12.1 — additive density+trigger behaviour per OQ-4/FR-061 — D13, D14) and **every
  default-state cell** as a secondary of the primary whose S conjunct it needs; only then file each
  preset by sound into the seven categories; check ≥ 3 per category. Start from the §6 table (names
  and categories provisional); E-ext presets are colony temperaments named now.
- Same authoring constraints as T031.

## Group 26 — generate and per-push check (sequential)

### T035 — Generate the tree, remove `.gitkeep`, per-push gate

- Build/run `generate_vorago_presets`; remove `plugins/vorago/resources/presets/Drones/.gitkeep`.
- Per-push filter (logged) → **all green**, including `Vorago_FactoryPresets_LibraryShape`,
  `…ParameterSpaceDistinct`, `…ClaimsWellFormed`, `Vorago_PresetSweep_ShortBounded`,
  `Vorago_FactoryPresets_SequentialLoadNoAlloc`/`ConcurrentLoadIsRtSafe`,
  `Vorago_Processor_GetTailSamplesMatchesState`. Record `ShortBounded`'s wall time (P-9).

## Group 27 — sweep loop (sequential)

### T036 — Local sharded sweep + aggregate, iterate to green (SC-008, SC-010…SC-012, SC-015, SC-022, SC-024)

- Run every shard locally in the background, logged, e.g. 4 shards × 2 threads:
  `VORAGO_SWEEP_SHARD=i/4 VORAGO_SWEEP_OUT=f:/tmp/v14_sweep vorago_tests.exe "[vorago-sweep]~[vorago-aggregate]" -d yes > f:/tmp/v14_shard_i.log 2>&1`,
  then `VORAGO_SWEEP_IN=f:/tmp/v14_sweep vorago_tests.exe "[vorago-aggregate]" -d yes > f:/tmp/v14_agg.log 2>&1`.
  Wait for completion; do not poll.
- On any failure: re-author the preset (T034's file) and re-run only the affected shard(s) + aggregate.
  A cell no authorable preset verifies, a failed control, or a boundedness failure that re-authoring
  cannot cure → **FR-017 stop-and-surface** with the measurements.
- Record per-case durations (`-d yes`) for T044 and the printed matrix, witnesses and distinctness
  numbers in `compliance.md`.

### T037 — Pin the committed-tree float tolerance (C-9, SC-006, §5.13)

- Run `Vorago_FactoryPresets_TreeToleranceProbe` on MSVC and on WSL/GCC (WSL recipe per project memory;
  `wsl --shutdown` afterwards) and record both worst figures.
- macOS (AppleClang, `-ffast-math`): needs one `workflow_dispatch` CI run with a temporary step
  `vorago_tests "Vorago_FactoryPresets_TreeToleranceProbe"` — **requires pushing: ask the user first**;
  remove the step afterwards; record the run URL.
- Set `kTreeFloatRelTol = max(10 × worst over all three, 1.19e-7)` in `preset_test_support.h`; record all
  three figures and the pin in `compliance.md`. `Vorago_FactoryPresets_TreeMatchesGenerator` → green.

### T038 — Preset CPU, alone (FR-041, SC-017)

- **Edit first (ruling R-6):** `tools/run-cpu-tests.js:54` — `FILTER` becomes
  `'[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]'`; nothing else in the
  runner changes. Verify: `vorago_tests.exe "[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]" --list-tests`
  lists `Vorago_PresetCpu` and no `[vorago-sweep]` case.
- **Run (alone, ≥ 15 min idle, nothing else executing, logged):**
  `node tools/run-cpu-tests.js vorago_tests` (writes `f:/tmp/cpu_vorago_tests.log`; P-core pinned by the
  runner). Breach → re-author the preset (never relax); a flip between runs → idle and
  re-run alone once before calling it a defect. Record worst ratio, absolute ns and stored-polyphony
  figures.

---

## Stage G — Deferred items and docs (FR-061, FR-062)

## Group 28 — docs page ([P], new files only)

### T039 [P] — `plugins/vorago/docs/index.html` + `docs/assets/style.css` (FR-062)

- **Create** on the structure of `plugins/seraphis/docs/index.html` and `assets/style.css`: what Vorago
  is; the twelve concept macros; the ecosystem view; the seven categories, one line each; the
  freeze-gesture instruction; system requirements; links. Remove `plugins/vorago/docs/.gitkeep`.
- **Verify:** open locally in a browser; all internal links resolve; no external script other than what
  the Seraphis page uses.

## Group 29 — records and plugin CLAUDE.md (sequential)

### T040 — FR-061 record + `plugins/vorago/CLAUDE.md`

- `compliance.md`: FR-061 — additive density + trigger ghost behaviour (OQ-4) is what the D12.1 preset
  uses; cite the preset name.
- `plugins/vorago/CLAUDE.md`: ecosystem-band ID rows; "108 + |R| registered IDs, 106 + |R| persisted";
  route totals (VP 31 + |R|); state table (v3 block row, `kStateV3Bytes`); page-6 row r0; the seven
  categories; `getTailSamples()` rule; 1.0.0 controller-interface freeze.

---

## Stage H — Tooling and CI (FR-024, FR-063, FR-066)

## Group 30 — determinism script (sequential)

### T041 — `--plugin` selector (FR-024, SC-007)

- **Edit:** `tools/check-preset-generator-determinism.js`.
- **Test first:** `node tools/check-preset-generator-determinism.js --plugin vorago` → fails today
  (unknown flag / seraphis binary).
- **Implement:** `--plugin <name>` (default `seraphis`); table `{seraphis: 'seraphis_preset_generator',
  vorago: 'vorago_preset_generator'}` drives `DEFAULT_BINARIES` (`:52-56`), the not-found message
  (`:111-113`) and the temp prefix (`:209`, `${plugin}-presets-`); USAGE updated; the no-flag path keeps
  today's behaviour byte-for-byte.
- **Verify:** `node tools/check-preset-generator-determinism.js --plugin vorago` → exit 0;
  `node tools/check-preset-generator-determinism.js` (Seraphis) → exit 0.

## Group 31 — release rosters (sequential)

### T042 — `release-readiness.js` + release `SKILL.md` (FR-063, SC-021)

- **Edit:** `.claude/workflows/release-readiness.js` (`PLUGIN_MAP`, `:14-22`: add
  `vorago: { testTarget: 'vorago_tests', bundle: 'Vorago.vst3' }`), `.claude/skills/release/SKILL.md`
  (plugin list `:15-16`, table `:21-31`: `| vorago | vorago_tests | Vorago.vst3 |`).
- **Verify:** `grep -n vorago` in both files; `node tools/lint-plugin-roster.js` → exit 0.

## Group 32 — `ci.yml` nightly filter (sequential)

### T043 — `[long]~[vorago-sweep]` on all three legs (FR-066, SC-023)

- **Edit:** `.github/workflows/ci.yml` — `FILTER='[long]'` at `:369`, `:655`, `:1116` →
  `FILTER='[long]~[vorago-sweep]'`. Nothing else.
- **Verify:** `grep -n "FILTER='\[long\]" .github/workflows/ci.yml` shows exactly three lines, all with
  `~[vorago-sweep]`.

## Group 33 — nightly sweep jobs (sequential)

### T044 — Measure, size, and add the sweep jobs (FR-066, SC-013, §4.11, §9)

- **Measure first:** from T036's `-d yes` logs compute per-preset local wall clock; derive shard count
  `n` so every job's estimated CI step ≤ 60 % of its `timeout-minutes` (≤ 180) using the §9 CI factor;
  record the arithmetic in `compliance.md`.
- **Edit:** `.github/workflows/long-tests-nightly.yml`: add `vorago-sweep` (needs `check-activity`,
  same `if`; matrix `os: [windows-2022, macos-latest, ubuntu-latest]` × `shard: [0 … n−1]`;
  `fail-fast: false`; `timeout-minutes ≤ 180`; checkout, configure with the leg's `ci.yml` flags minus
  AU/ccache, build `--target vorago_tests` only; run with `VORAGO_SWEEP_SHARD=${{ matrix.shard }}/n`,
  `VORAGO_SWEEP_OUT=sweep-out`, filter `"[vorago-sweep]~[vorago-aggregate]" -d yes`; upload
  `vorago-sweep-${{ matrix.os }}-${{ matrix.shard }}`) and `vorago-sweep-aggregate` (needs both; `if:
  ${{ !cancelled() && needs.check-activity.outputs.should_run == 'true' }}`; per OS; download
  `vorago-sweep-${{ matrix.os }}-*` merged; run `VORAGO_SWEEP_IN=sweep-in … "[vorago-aggregate]" -d yes`).
- **Verify:** YAML parses (`node -e` with a YAML-free structural check, or `gh workflow view` after the
  user permits a push); the SC-013/SC-023 CI measurement happens in T048.

---

## Stage I — Release (FR-064, FR-065, FR-026/FR-027, FR-042)

## Group 34 — version (sequential)

### T045 — `version.json` 1.0.0 + `CHANGELOG.md` `[1.0.0]` (FR-064)

- **Edit only:** `plugins/vorago/version.json` (`"version": "0.2.0"` → `"1.0.0"`) and
  `plugins/vorago/CHANGELOG.md` (new `## [1.0.0]` section above the latest: the factory library — N
  presets in seven categories, the ecosystem rule knobs and state v3 with v2 compatibility, the
  state-derived tail, the controller-interface freeze). Never `version.h`.
- **Verify:** `node tools/check-changelog-coverage.js` → exit 0.

## Group 35 — release evidence (sequential; several steps need the user)

### T046 — Install-path and installer-text verification (FR-026, FR-027)

- Build `Vorago`; list `%PROGRAMDATA%\Krate Audio\Vorago\` → exactly the seven category directories and
  N `.vstpreset` files; root == `Platform::getFactoryPresetDirectory("Vorago")`; re-read
  `plugins/vorago/installers/windows/setup.iss:66-68` and `installers/linux/README.txt:29-44` against the
  final set; cite lines and listing in `compliance.md`. (A post-build copy permission error is expected
  and harmless; the build target is the only install path — never hand-copy.)

### T047 — Human listening checkpoint (FR-042, SC-020) — **the user**

- The phase owner auditions every preset; `compliance.md` gets, per preset, category fit and a one-line
  character note, and the confirmation that no two read as variants. Any "variant" verdict sends the
  pair back to T034/T036.

### T048 — Release gate (FR-065, SC-013, SC-019, SC-023)

- Run the `release-readiness` workflow for `vorago` (build, `vorago_tests`, pluginval strictness 5 on
  `build/windows-x64-release/VST3/Release/Vorago.vst3`, version/CHANGELOG sync) → green row.
- **Needs the user's permission to push:** one green `long-tests-nightly.yml` run (sweep jobs' step
  wall clocks per OS ≤ 60 % of `timeout-minutes`; per-push "Run Tests" ≤ 80 % of 20 min per OS; the
  generic nightly step unchanged) and the release commit's `ci.yml` "Run Vorago AU Validation"
  (`ci.yml:800`) auval line. Record every run URL and number in `compliance.md`.

---

## Final group — integration

## Group 36 — registration audit (sequential)

### T049 — CMake registration audit (single task)

- Confirm each of the nine Phase 14 TUs appears exactly once in `plugins/vorago/tests/CMakeLists.txt`,
  that all except `preset_cpu_test.cpp` are in the `-fno-fast-math` list, that `tools` is on the
  include path, and that the root `CMakeLists.txt` holds `vorago_preset_generator` (output
  `${CMAKE_BINARY_DIR}/bin`) and `generate_vorago_presets`. `vorago_tests.exe --list-tests` shows every
  TEST_CASE named in T002, T008, T009, T012, T017, T023–T028.

## Group 37 — full-suite run (sequential)

### T050 — Full local suite

- Clean build of `dsp_systems_tests`, `vorago_tests`, `vorago_preset_generator`, `Vorago`, zero warnings.
- `dsp_systems_tests` per-push filter; `vorago_tests` per-push filter; `vorago_tests "[long]~[vorago-sweep]"`
  (the generic nightly roster); the full sharded sweep + aggregate as in T036 (logged, background) —
  all green. `node tools/check-seraphis-green.js` → green (Seraphis untouched).
- pluginval strictness 5 on `Vorago.vst3` → exit 0.
- `node tools/check-preset-generator-determinism.js --plugin vorago` and the Seraphis default → exit 0.

## Group 38 — static checks (sequential)

### T051 — clang-tidy

- `./tools/run-clang-tidy.ps1 -Target vorago -BuildDir build/windows-ninja` and `-Target dsp` → 0
  findings (fix every one). `tools/vorago_preset_generator.cpp` is outside the `vorago` source dirs
  (`tools/run-clang-tidy.ps1:211-218`), so run clang-tidy on it explicitly with the same config.

### T052 — Portability (single task, last)

- `node tools/check-portability.js` → all clear (it compiles the changed TUs, including the generator,
  with g++ under WSL); then `wsl --shutdown`. Also build `vorago_preset_generator` under WSL/GCC once
  (FR-025) and confirm it links without VSTGUI (add `dataexchange.cpp` per T029 if it does not).
