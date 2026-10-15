# Tasks: Profundum Phase 4 — Drop & Note Behaviour

**Spec:** `specs/profundum-phase4-drop-note-behaviour/spec.md` (Clarified 2026-10-12) · **Plan:** `specs/profundum-phase4-drop-note-behaviour/plan.md`
(S0–S15 + review notes) · **Model:** `specs/profundum-phase4-drop-note-behaviour/drop-model.js` · **Phase base:** commit `3d8ca19f` on
`synth/profundum`
**Status:** Task list (2026-10-12). The build stage has not started. **Plan S1, O-1 and O-4 RATIFIED 2026-10-12 — see "Rulings applied" below; the precondition is met.**

## Read this first (every executor)

### Precondition: ratification of plan S1 and O-1 … O-4 (BLOCKING)

Plan S1 says its sixteen items must be ratified before `tasks.md` runs (`plan.md:38`, heading `## S1` at `:116`), and the plan ends with
four open questions (`plan.md:1175–1186`). On 2026-10-12 the spec's Clarifications log (`spec.md:840–851`) holds Q1–Q8 only: **no S1
item and no O-question is ratified.** The tasks below encode the plan's **default** for each. The main loop must ratify them with the
user and write each ruling into `spec.md` "## Clarifications" **before T006 starts** (Groups 0–2 are read-only, model or bench work and
may run before). Three items change a spec bar and must also be written into the spec row they change: C-5 (SC-013(c) gains
"+ 1 ulp(A)"), C-8 (E-4's bound becomes [−ρ_max − 2.7 st, A]; FR-007's bridge length becomes L_br + L_r) and C-6/O-3 (SC-013(a) gates
feasible cells only). Every test that relies on a C-item cites it in a comment (`// plan S1 C-8`). If the user rules differently,
change the listed tasks before executing them; never relax a bar on your own.

### Rulings applied (2026-10-12, after S1 ratification) — READ BEFORE YOUR TASK

The user ratified every S1 item at the plan default **except that O-1 was ruled the other way**: SC-015 measures the **rendered**
aliased power (Phase 1 SC-009's estimator, bar ≤ −96 dBFS per trajectory cell), not the index × `currentFrequency()` formula, so
**no Phase 4 trajectory carries an upward-slope bound**. O-4: no correction ceiling below 48 st. Spec rows SC-013, SC-015, FR-006,
FR-007, FR-053, FR-057, FR-062, E-4 and D-14 already carry the ratified wording; the spec's Clarifications session
"2026-10-12 — plan S1 ratification and open questions" is authoritative. Per-task consequences (each task section also carries a
pointer line):

| Task | Change under the rulings |
|---|---|
| T004 | **SKIP** — moot. No γ_cap, no `evidence/sc013-infeasible.json`, no `infeasible` model section. Return done with notes "skipped per O-1 (2026-10-12)". |
| T006 | The `drop_curve.h` stub defines **no** `kMaxUpwardSemitonesPerSample`. |
| T008 | No σ_up floor pin (the 160 / 96-sample case is removed); ConstantRate durations are exactly T_ms·\|Δ\|/12 samples with no floor. |
| T009 | No private σ_up floor on Quantized; step timing k·T/\|Δ\| only. |
| T011 | `DropCurve` constants per C-12 **without** `kMaxUpwardSemitonesPerSample`; `kBrakeSemitones` stays. |
| T012 | Bridge rise L_r = L_min always (no ⌈1.5·ΔA/σ_up⌉ branch): the branch assertion covers THREE branches (L_br = 1 steep; L_br > 1 brake ≤ 0.5 st; L_min rise), and the "σ_up-bound rise" cell / 480-sample figure is removed. |
| T015 | γ_lim = 48 (constant, no γ_cap formula); `landingFeasible()` = a root exists in [−48, 48] for at least one candidate; `DropPhaseLock_InfeasibleClamp` reads no json — it asserts `landingFeasible()` true on every SC-013 grid cell and covers the ±48 clamp branch with one synthetic trigger (f_res = `kMinF0Hz`, A = 48, T = 1 ms, exponential, R = 1; INFO its feasibility, no gate on it). |
| T016 | SC-013(c) gate Γ = γ_an + measured carry (no γ_cap term) + 1 ulp(A). |
| T019 | No static_asserts on σ_up aliases (they do not exist); the other static_asserts stand. |
| T021 | Every grid cell gated at \|θ\| ≤ 1°; no infeasible-set equality, no infeasible summary in evidence; \|γ\| recorded per Time (O-4). |
| T022 | SC-015 = rendered aliased power with Phase 1 SC-009's estimator (bar ≤ −96 dBFS) on the same trajectory set, block 1; the formula is neither computed nor gated; the evidence CSV holds the trajectory and the measured aliased power per cell. |
| T023 epoch 7 (user rulings, 2026-10-13) | Spec Clarifications "T023 epoch 6 readings": (D) SC-019 reading-changing levers allowed within the existing bars, verdict = median of 3 isolated lanes; (B) SC-010(b) is the click gate, (a) recorded; (A) SC-026 waveform gated with a1 divided out (≤ −120 dB), raw excursion recorded; (C) SC-014 Continue one-period RMS reference. No bar number changed. |
| T023 epoch 6 (main loop, 2026-10-13) | Rulings (A)–(D) of spec Clarifications "T023 epoch 5 readings" applied: SC-026 scoped (output diff ≤ −60 dB re peak, recorded per cell), SC-010(a)/SC-021 differential click count (positive controls must stay flagged), SC-010(b) D from delivered gains, SC-014 Continue RMS in the Reset dip's window, SC-019 per-hold mirror (T005 ruling (ii)); no bar number changed. |
| T023 epoch 3 (main loop, 2026-10-13) | The retry section had never been written (the epoch-3 executor stopped on it). Written: file list, build rights, items 1–8; SC-019 options (a) profile and (b) optimise the Phase 4 per-sample paths authorised by the main loop (measurement and optimisation under a fixed bar, no ratified design or bar touched); option (c) not needed: the timed region counts exactly SC-019's list. |
| T029 (main loop) | **FR-070 / SC-028 (2026-10-13):** `harmonic_oscillator_bank.h` opt-in `setContinuousFrequencyUpdates` + `reconcileContinuity` (append-only, default off), `profundum_core.h` enables it in `prepare`, `harmonic_oscillator_bank_tests.cpp` gains `HarmonicOscillatorBank_ContinuousFrequencyUpdates`, new TU `profundum_core_pitch_continuity_test.cpp` (registered) holds SC-028. Done by the main loop; see Run log. |
| T024 | S14 handoff: C-9 / C-10 / O-2 clauses become one recorded sentence ("no slope bound is needed under the rendered gate; the tail upswing figure 0.418 st/sample is recorded for Phase 9's own aliasing measurement"); drop the `landingFeasible()` capability line. |


| Item | What the tasks assume (plan default) | Tasks affected if ruled otherwise |
|---|---|---|
| C-1 | One new Layer 0 header `core/drop_curve.h` (`struct DropCurve`, `makeDropCurve`) owns the curve math and every curve-geometry constant | T006, T011, T012, T015 |
| C-2 ⚠ | FR-056/FR-057 "closed-form" = O(1) fixed-panel Gauss–Legendre 8 (2 panels per piece, 4 on pieces > 0.5·N) of the latched curve | T011, T012, T015 |
| C-3 ⚠ | Φ_x predicted on the core's 16-sample staircase and [8 Hz, cap] clamp: first 4 pitch updates exact + clamp-aware remainder with EM endpoint terms | T015 |
| C-4 ⚠ | Landing target = the neighbouring integer (⌊Φ̂⌋ or ⌈Φ̂⌉) with the smaller solved \|γ\| (\|r\| < 1 cycle) | T015, T021 |
| C-5 ⚠ | Correction c = γ·16x²(1−x)²; SC-013(c) gate = 1.05·Γ + 1 ulp(A), Γ = min(γ_an, γ_cap) + measured carry, γ_an solver-independent | T016, T021 |
| C-6 ⚠ / O-3 | Option (a): γ clamped to γ_cap = min(48, σ_up·D_s/3.0792…); `landingFeasible()` false; SC-013(a) gates feasible cells, the infeasible set must equal `evidence/sc013-infeasible.json` | T004, T015, T021 |
| C-7 ⚠ | With Rebound the fall runs A → −ρ_R along the family shape, d = (A + ρ_R)·p(u) − ρ_R | T011 |
| C-8 ⚠ | Bridge = brake → rise → onset fillet (S3.5); E-4 bound [−ρ_max − 2.7, A]; bridge length L_br + L_r exactly | T011, T012 |
| C-9 ⚠ | Drop tail upswings are not slope-bounded; Phase 9 handoff | T012, T022, T024 |
| C-10 / O-1 / O-2 | σ_up = 0.15 st/sample on bridge rise, correction, ConstantRate and Quantized (private floor), **not** ConstantTime; SC-015 keeps its formula (index × `currentFrequency()`) | T008, T009, T011, T015, T022 |
| C-11 | A Quantized retarget / entry restarts from the current integer output q | T009 |
| C-12 | k = 8, fillets 0.05, q = 0.45, elastic knot layout, landing slope 1.5, σ_up 0.15, brake 0.5 st, kDeclickMs 1.5 — defined once in `DropCurve` (kDeclickMs in `RetriggerDeclick`) | T011, T014 |
| C-13 | SC-024 second clause bar = 2e-5 cycles absolute | T015 |
| C-14 | `DropPhaseLock::trigger(curve, fResHz, gridPhase, minF0Hz, capHz, kind)` | T015, T019 |
| C-15 | Declick gain g[k] = ½(1 + cos(π(k+1)/L_d)), switch sample k = L_d − 1 written 0.0f, gain 1.0f afterwards | T014, T020 |
| C-16 | CPU worst case unknown until T005 measures it; R-1 levers in order if red | T005, T015, T018 |
| O-4 | No correction ceiling below 48 st (γ up to 47.9 st at 10 ms, 9.78 st at 60 ms on E0) | T015, T021 |

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **CMake registration is the first code task (T006), not the last.** Every later task is test-first and must build its TU; an
   unregistered TU cannot fail. This is the Phase 1–3 precedent (`specs/profundum-phase3-anchor/tasks.md:42–45`). T006 creates
   compiling stubs; the final group re-audits the registration as a single task (T025).
2. **`dsp/CMakeLists.txt` gains the five new headers** in `KRATE_DSP_CORE_HEADERS`, `KRATE_DSP_PRIMITIVES_HEADERS` and
   `KRATE_DSP_PROCESSORS_HEADERS` (plan S12.1 lists only the test CMake). Phases 1–3 registered their headers there
   (`dsp/CMakeLists.txt:158–161`, `:188–190`).
3. **No `[P]` implementation tasks.** Every implementation task rebuilds `dsp_processors_tests`, `dsp_primitives_tests` or
   `dsp_systems_tests` in the one build tree `build/windows-x64-release`; two concurrent builds of one tree corrupt MSBuild
   intermediates. Parallelism lives in Group 0 (Node scripts, read-only work) and in the integration lanes (T026 runs the suites
   concurrently).
4. **SC-013's `[long]` grid is split into one TEST_CASE per note** (`ProfundumNoteBehaviour_PostDropLanding_E0`, `_E1`, `_E2`, `_C3`,
   `_PunchGhostE1`) so the shards of plan R-10 ("shard by note across 4 processes") can be selected by name.
5. **The SC-013 infeasible set is defined by the component's own criterion**, not by `drop-model.js slope`. The `slope` section
   (`drop-model.js:452–473`) marks a cell infeasible when the whole corrected curve's max upward step exceeds 0.15
   (`renderExcess` `maxUp`, `:217`), which includes the Drop's tail upswings (up to 0.418 st/sample, C-9) that the correction does not
   cause. `DropPhaseLock` decides feasibility by the bracket [−γ_lim, γ_lim] (plan S5.4). T004 emits the set by the component criterion
   and records any difference from `slope`'s "23 of 384" figure; a difference goes to the main loop (it changes C-6's census).
6. **Harness h1 feed:** plan S8 says the Anchor's `setCoreH1Amplitude` is fed "`deliveredGains()[0]·kBankHeadroom`-scaled h1 as in
   Phase 3's chain test". Phase 3's chain test actually feeds `core->deliveredGains()[0] * SpectralShapeRecipe::kCenterPanGain`
   (`dsp/tests/unit/systems/profundum_anchor_chain_test.cpp:745`). The harness uses the Phase 3 form.
7. **Non-finite `prepare` and trigger arguments.** SC-017 requires every setter to keep its previous value on non-finite input. The
   plan does not say what `prepare(NaN)` or a non-finite `DropPhaseLock::trigger` argument does. The tasks rule: `prepare` with a
   non-finite rate is a no-op (state, including "unprepared", unchanged) on `DropEnvelope`, `DropPhaseLock` and `RetriggerDeclick`;
   `DropPhaseLock::trigger` with non-finite or non-positive `fResHz`, `minF0Hz` or `capHz` is ignored (no pending plan; the running
   correction continues); `DropPhaseLock::process` with a non-finite `plannedOffset` returns 0.0f and the staircase mirror adds
   (f_h − f_res)/fs with f_h held from the last finite sample.
8. **SC-024(a)'s brute-force reference uses "step ≤ 1e-3 of the shortest feature" (the spec bar, `spec.md:679`)**, i.e. per piece
   max(2000, 1000·len/shortestPieceLen) composite-Simpson panels (even), instead of plan S11.3's fixed 2·10⁵ panels per piece: the plan's
   figure costs ≈ 1.7e10 integrand evaluations over the 7 020-cell grid. The spec's step bound is met either way.
9. **Run logs go to one file per task** (`specs/profundum-phase4-drop-note-behaviour/evidence/run-log/<Tnnn>.txt`, created by that task)
   so that parallel tasks never edit a shared document.
10. **Catch2 filters.** A positional argument `"[note-behaviour]~[long]"` is one AND-term; separate arguments OR. Hidden `[.perf]` cases
    never run unless named. Never write `"[a],[b]~[long]"` (it parses as `[a]` or (`[b]` and not `[long]`), Phase 3 deviation 4).
11. **Early landing teeth at the processors level (T015):** `DropPhaseLock_LandingOnStaircaseModel` gates the spec's 1° bar on a
    test-side staircase model of the d_c trace before the full core composition exists (SC-013 remains the authority).
12. **`MonoHandler` cases go to a sibling TU** `mono_handler_glide_test.cpp` (FR-069 allows it); `mono_handler_test.cpp` stays unmodified.

### File map

| File | Kind | Layer / target | Created by / edited by |
|---|---|---|---|
| `dsp/include/krate/dsp/core/drop_curve.h` | new header, stdlib + `core/db_utils.h` only | L0 | T006 stub; T011, T012 |
| `dsp/include/krate/dsp/primitives/retrigger_declick.h` | new header, L0 only | L1 | T006 stub; T014 |
| `dsp/include/krate/dsp/processors/drop_envelope.h` | new header (`drop_curve.h`, `db_utils.h`) | L2 | T006 stub; T011, T012 |
| `dsp/include/krate/dsp/processors/drop_phase_lock.h` | new header (`drop_curve.h`, `db_utils.h`) | L2 | T006 stub; T015, T016 |
| `dsp/include/krate/dsp/processors/note_memory.h` | new header (`<cstdint>`, `<array>`) | L2 | T006 stub; T013 |
| `dsp/include/krate/dsp/processors/mono_handler.h` | **existing shared header, append-only** | L2 | T007, T008, T009 |
| `dsp/tests/unit/primitives/retrigger_declick_test.cpp` | new TU | `dsp_primitives_tests` | T006 stub; T014 |
| `dsp/tests/unit/processors/drop_c1_checker.h` | new test header (std + `drop_curve.h`) | — | T011 (append only after) |
| `dsp/tests/unit/processors/mono_handler_pre_phase4_ref.h` | generated frozen reference | — | T002 only |
| `dsp/tests/unit/processors/mono_handler_glide_test.cpp` | new TU | `dsp_processors_tests` | T006 stub; T007–T010 |
| `dsp/tests/unit/processors/drop_envelope_test.cpp` | new TU | `dsp_processors_tests` | T006 stub; T011, T012 |
| `dsp/tests/unit/processors/note_memory_test.cpp` | new TU | `dsp_processors_tests` | T006 stub; T013 |
| `dsp/tests/unit/processors/drop_phase_lock_test.cpp` | new TU | `dsp_processors_tests` | T006 stub; T015, T016 |
| `dsp/tests/unit/processors/drop_note_behaviour_nonfinite_test.cpp` | new TU, **`-fno-fast-math`** | `dsp_processors_tests` | T006 stub; T017 |
| `dsp/tests/unit/processors/drop_note_behaviour_perf_test.cpp` | new TU, `[.perf]`, **not** `-fno-fast-math` | `dsp_processors_tests` | T006 stub; T018 |
| `dsp/tests/unit/systems/profundum_note_behaviour_test_helpers.h` | new test header (`NoteBehaviourHarness`) | — | T019 (append only after) |
| `dsp/tests/unit/systems/profundum_note_behaviour_test.cpp` | new TU | `dsp_systems_tests` | T006 stub; T019, T020 |
| `dsp/tests/unit/systems/profundum_note_behaviour_spectral_test.cpp` | new TU (`[long]` grids + smokes) | `dsp_systems_tests` | T006 stub; T021, T022 |
| `dsp/CMakeLists.txt`, `dsp/tests/CMakeLists.txt` | existing | — | T006 only (T025 audits) |
| `specs/profundum-phase4-drop-note-behaviour/extract-mono-ref.js` | new Node script | SC-006 | T002 |
| `specs/profundum-phase4-drop-note-behaviour/check-append-only.js` | new Node script | SC-020 | T003 |
| `specs/profundum-phase4-drop-note-behaviour/drop-model.js` | existing model | C-6 evidence | T004 (new section only) |
| `specs/profundum-phase4-drop-note-behaviour/bench/drop-kernel-bench.cpp` | new bench | C-16 | T005 |
| `specs/profundum-phase4-drop-note-behaviour/evidence/` | evidence (JSON, CSV, WAV, run logs) | — | T001, T004, T005, T021, T022, ... |
| `specs/Profundum-roadmap.md` | existing roadmap | S14 handoffs | T024 |

**Untouchable (zero diff lines vs `3d8ca19f`) — everything under `dsp/include/` except the five new headers and the append-only lines of
`mono_handler.h`.** In particular `systems/profundum_core.h`, `systems/anchor_oscillator.h`, `primitives/smoother.h`,
`primitives/adsr_envelope.h`, `processors/spectral_shape_recipe.h`, `core/db_utils.h`, `core/pitch_utils.h`, `core/midi_utils.h`; and every
existing test file, notably `dsp/tests/unit/processors/mono_handler_test.cpp`, `dsp/tests/unit/systems/anchor_oscillator_test_helpers.h`,
`profundum_core_test_helpers.h`, `profundum_anchor_chain_test.cpp`, `poly_synth_engine_test.cpp`.

### Verified facts these tasks depend on (each read on 2026-10-12 in this session)

Paths relative to `dsp/include/krate/dsp/` unless stated.

- **`MonoHandler`** (`processors/mono_handler.h`, 500 lines): banner "Layer 2" `:2`; includes `core/db_utils.h`, `core/midi_utils.h`,
  `core/pitch_utils.h`, `primitives/smoother.h` `:22–25`; `namespace Krate {  // NOLINT(...)` `:31`, `namespace DSP {` `:32`, closing
  `}  // namespace DSP` `:499`, `}  // namespace Krate` `:500`; `struct MonoNoteEvent` `:40`; `enum class MonoMode` `:52`;
  `enum class PortaMode : uint8_t { Always = 0, LegatoOnly = 1 }` `:63–66`; `struct NoteEntry` `:73`; `class MonoHandler {` `:91`;
  `kMaxPortamentoTimeMs = 10000.0f` `:100`; constructor configures the ramp at 0 ms `:109–112`; `prepare(double)` body `:121–125` (last
  statement `portamentoRamp_.setSampleRate(sampleRate_);` `:124`); `setPortamentoTime` `:280–286` (NaN/Inf ignored `:281–283`, clamp, then
  `portamentoRamp_.configure` `:285`); `processPortamento()` `:291–295`; `getCurrentFrequency()` follows; `hasActiveNote()` `:340–342`;
  `reset()` `:349–356` (last statement `portamentoRamp_.snapTo(portamentoRamp_.getCurrentValue());` `:355`); `updatePortamentoTarget`
  `:450–457` (`if (enableGlide && portamentoTimeMs_ > 0.0f)` `:451`); `semitoneToFrequency` `:461–463`; members end with
  `float sampleRate_{kDefaultSampleRate};` `:496`.
- **`ProfundumCore`** (`systems/profundum_core.h`): `enum class RetriggerPhase : std::uint8_t { Reset, FreeRunning }` `:94`;
  `kControlInterval = 32` `:97`; `kPitchUpdateInterval = 16` `:98`; `kMinF0Hz = 8.0f` `:99`; `kBankHeadroom = 4.0f` `:114`;
  `prepare(double, int = kDefaultPartials)` `:129` (clamps the rate `:130`); `reset()` `:157`; `setControls` `:169`; `setRetriggerPhase`
  `:197`; `noteOn(float)` `:204`; `setPartialPanOffsets` `:214`; `deliveredGains()` `:294`; `currentFrequency()` `:298`;
  `numPartials()` `:301`.
- **`AnchorOscillator`** (`systems/anchor_oscillator.h`): `enum class Mode { Lock, Follow, Punch, Ghost }` `:58`; `RetriggerPhase` `:59`;
  `kPitchUpdateInterval = 16` `:63`; `kMinF0Hz = 8.0f` `:64`; `struct Controls { waveform, mode, level, punchBoostDb, punchTimeMs,
  ghostTimeMs }` `:99–106`; `prepare(double)` `:123`; `setControls` `:224`; `setFollowRatio` `:255`; `setCoreH1Amplitude` `:262`;
  `setRetriggerPhase` `:269`; `noteOn(float, bool rearm = true)` `:276`; `processBlock(L, R, n, f0PerSample, followRatioPerSample)`
  `:285`; `modeGain()` `:357`, `phase()` `:358`, `phaseIncrement()` `:359`, `isSounding()` `:360`.
- **Recipe:** `kNyquistCapFraction = 0.8f` `processors/spectral_shape_recipe.h:72`; `capFrequency(double)` `:93–95`; `kSineAnchor`
  `:231`, `kSawAnchor` `:237`, `kGrowl` `:247`; `kCenterPanGain` (used at `profundum_anchor_chain_test.cpp:745`).
- **`ADSREnvelope`** (`primitives/adsr_envelope.h`): `prepare(float)` `:68`, `reset()` `:75`, `gate(bool)` `:86`, `setAttack(float ms)`
  `:115`, `setDecay` `:121`, `setSustain` `:127`, `setRelease` `:132`, `setRetriggerMode` `:209`, `process()` `:232`, `getStage()` `:265`,
  `isActive()` `:266`.
- **`VoiceAllocator`** (`systems/voice_allocator.h`): `noteOn(uint8_t, uint8_t)` `:228`, `noteOff(uint8_t)` `:258`, `voiceFinished(size_t)`
  `:288`, `setStealMode` `:317`, `setVoiceCount` `:326`.
- **L0:** `detail::isNaN(float)` `core/db_utils.h:99`, `detail::isFinite(float)` `:118`, `detail::isFinite(double)` `:125`,
  `detail::isInf(float)` `:136`; `detail::flushDenormal` `:263` flushes |x| < 1e-15 (**not** a subnormal bit test — the new setters need
  their own exponent-bit test); `kPi` `core/math_constants.h:28`; `kA4FrequencyHz` `core/midi_utils.h:33`, `kA4MidiNote` `:36`.
- **Test helpers:** repo-root `tests/test_helpers/` (included by bare name, e.g. `#include "artifact_detection.h"`,
  `dsp/tests/unit/systems/anchor_oscillator_test.cpp:19–20`): `allocation_detector.h` — `AllocationDetector::instance()` `:95`,
  `getAllocationCount()` `:70`, `class AllocationScope` `:111` (count latched in the destructor `:118`; read a live count through
  `AllocationDetector::instance().getAllocationCount()`); `artifact_detection.h` — `ClickDetectorConfig` `:38`, `ClickDetector` `:99`,
  `detect` `:130`; `render_fingerprint.h`. Phase 3 systems helper `anchor_oscillator_test_helpers.h`: `anchorHermitePeakD2` `:121`,
  `struct AnchorSegmentSpec { enum class Kind { Hold, Cosine, Hermite } ... }` `:131–132`, `struct C1Report` `:137–142`,
  `anchorSegmentD2Bound` `:146`, `anchorCheckC1` `:165`, `anchorFindMutableStatics` `:410`. Phase 1 helper
  `profundum_core_test_helpers.h`: `goertzelPhase(const float*, std::size_t, double fs, double hz)` `:157`.
- **WAV:** no shared writer exists; `profundum_core_spectral_test.cpp:1562` has a TU-local `writeWavFloatStereo` inside an anonymous
  namespace (`:1535`). The Phase 4 spectral TU writes its own, also in an anonymous namespace, under a distinct name.
- **CMake** (`dsp/tests/CMakeLists.txt`): `add_executable(dsp_primitives_tests` `:87`, last entry `unit/primitives/arp_lane_test.cpp`
  `:145`, `)` `:146`; `add_executable(dsp_processors_tests` `:158`, `mono_handler_test.cpp` `:232`, `harmonic_fold_nonfinite_test.cpp`
  `:259`; `add_executable(dsp_systems_tests` `:331`, `poly_synth_engine_test.cpp` `:362`, last Phase 3 entry
  `unit/systems/anchor_oscillator_perf_test.cpp` `:651`, `)` `:652`; `KRATE_DSP_TESTS_DIR` defined for processors and systems tests
  `:741–744`; `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")` `:758`, `-fno-fast-math` block opens `:759`, last entry
  `unit/systems/anchor_oscillator_nonfinite_test.cpp` `:1138`, `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` `:1139`.
  `dsp/CMakeLists.txt`: `KRATE_DSP_CORE_HEADERS` `:82`, last entry `hungarian_algorithm.h` `:100`, `)` `:101`; `..._PRIMITIVES_HEADERS`
  `:104`, last entry `polyphase_halfband_oversampler.h` `:126`, `)` `:127`; `..._PROCESSORS_HEADERS` `:130`, last entry `harmonic_fold.h`
  `:161`, `)` `:162`. **Re-derive every anchor from the live file before editing.**
- **Tools:** `tools/run-cpu-tests.js` filter `'[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]'` (`:56`) —
  it also runs every `[long]` case of the named exe; `tools/pin-perf-cores.ps1 -Exe <path> [-ExeArgs ...]` (`:17–21`);
  `tools/check-portability.js`, `tools/lint-apple-globals.js`, `tools/run-clang-tidy.ps1` exist. `clang++` is at
  `/c/Program Files/LLVM/bin/clang++.exe` (listed 2026-10-12). Pattern for the frozen-reference extractor:
  `specs/profundum-phase2-edge-fold/extract-phase1-ref.js` (output `profundum_phase1_core_ref.h`, namespace
  `Krate::DSP::ProfundumTest::Phase1Ref`).
- **ODR sweep (2026-10-12, this session):** `grep -rnE "(class|struct|enum class|using) (DropCurve|DropEnvelope|DropPhaseLock|NoteMemory|RetriggerDeclick|GlideMode|NoteBehaviourHarness|MonoRefPrePhase4|DropC1Segment)\b" dsp plugins tests tools`
  → 0 hits. No `drop_*`, `note_memory*` or `retrigger_declick*` file exists.

### Commands (Git Bash, full CMake path, always)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
LOG=<scratchpad>/<task>-build.log
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests 2>&1 > "$LOG"; tail -5 "$LOG"; grep -c "warning" "$LOG"
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "DropEnvelope_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "[profundum]~[long]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[note-behaviour]~[long]" 2>&1 | tail -5
```

Never `ctest -R`. Capture any run longer than about 30 s to a scratchpad log and read the log; never re-run a slow suite for its output.
One build of this tree at a time.

### Rules for every task

1. Failing test **first**: write it, build, watch it fail (a compile error on a missing symbol counts). Then implement, rebuild with
   **zero warnings** (`grep -c warning` on the build log = 0 for the touched files), and run the named cases until they pass.
2. Header-only, `namespace Krate::DSP` (C++17 nested form `namespace Krate::DSP {` for new headers). Includes point downward only:
   `drop_curve.h` → stdlib + `core/db_utils.h`; `retrigger_declick.h` → L0; `drop_envelope.h`, `drop_phase_lock.h` → `core/drop_curve.h`,
   `core/db_utils.h`; `note_memory.h` → stdlib only. **No new header includes a Layer 3 header.** Every helper type is nested in its class;
   no new namespace-scope name except `DropCurve`, `makeDropCurve`, the five classes and `GlideMode`.
3. PascalCase classes, camelCase functions, `trailing_` members, `kPascalCase` constants. Designated initialisers; every double→float
   via `static_cast`; no narrowing in brace init. Finiteness via `detail::isFinite` only, never `std::isnan`/`std::isfinite`. Bit
   comparisons via `std::bit_cast` / `std::memcmp`.
4. Audio-thread methods `noexcept`, no allocation, lock, exception, I/O; fixed-size members only; no `static` mutable data; no SIMD.
5. Test helpers are `inline` functions; processors helpers in `Krate::DSP::DropTest` (prefix `drop`), systems helpers in
   `Krate::DSP::ProfundumTest` (prefix `noteBehaviour`). Never an anonymous namespace in a header.
6. Tags: `[primitives][profundum][declick]`; `[processors][profundum][drop]`, `[drop-lock]`, `[note-memory]`, `[mono-glide]`,
   `[drop-nonfinite]`; perf `[processors][profundum][drop][.perf]`; systems `[systems][profundum][note-behaviour]`. `[long]` only for a case
   measured > 15 s and toolchain-independent, each with a per-push `…Smoke` sibling. **Never `[long]`:** NaN/Inf guards, L == R identity,
   RT safety, the resolver table, append-only checks.
7. No bit-exact float goldens. `memcmp` only for in-process identity between two renders of the same build.
8. Notes: E0 = MIDI 16 = 20.6017 Hz, E1 = 28 = 41.2034 Hz, E2 = 40 = 82.4069 Hz, C2 = 36 = 65.4064 Hz, C3 = 48 = 130.8128 Hz,
   C4 = 60 = 261.6256 Hz; compute `440·2^((m − 69)/12)` in double, cast once to float. Rates 44.1 / 48 / 96 kHz unless stated.
9. Sample counts from header constants in double: N = T·fs/1000 (not rounded), t_L = round(N). σ_up = `DropCurve::kMaxUpwardSemitonesPerSample`.
10. Evidence files go under `specs/profundum-phase4-drop-note-behaviour/evidence/`, addressed in tests as
    `std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase4-drop-note-behaviour/evidence/..."`; create directories with
    `std::filesystem::create_directories` outside any `AllocationScope`.
11. If a test exposes a defect in a file another task owns, do not edit that file: record the case, its output and your diagnosis under
    "Defects found" at the bottom of this file **only in a sequential task**, otherwise in your run-log file. T023 fixes it.
12. Never relax a threshold or shrink a workload to pass (CLAUDE.md, FR-066). A bar proven unreachable goes to the main loop with the
    measurement.

---

## Group 0 — pre-flight (parallel; read-only work or new script files only)

### T001 [P] ODR re-sweep and model evidence reproduction (read-only; plan R-8, S10)

- **ODR:** run `grep -rnE "(class|struct|enum class|enum|using) (DropCurve|DropEnvelope|DropPhaseLock|NoteMemory|RetriggerDeclick|GlideMode|NoteBehaviourHarness|MonoRefPrePhase4|DropC1Segment|PieceKind|TriggerKind|PhasePolicy)\b" dsp plugins tests tools`.
  Expected: hits only for nested names that already exist elsewhere **inside other classes** (record each; nested names do not collide).
  Any namespace-scope hit for the first nine names is a stop: report it. Confirm none of the roadmap hazard names
  (`CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`, `MorphNode`, `TransientDetector`, `HarmonicCloud`,
  `SpectralState`, `MacroMapper`) and none of `RetriggerMode` (`primitives/envelope_utils.h:64`) / `PortaMode` is reused.
- **Evidence:** run concurrently, each to its own scratchpad log, then `wait`, recording each wall clock:
  `node specs/profundum-phase4-drop-note-behaviour/drop-model.js shape`, `… integral`, `… lock2all`, `GLN=8 GLS=2 GLS_LONG=4 node … lock3 4`,
  `… perSample`, `… slope`, `… nyquist`, `… glide`, `… bound`.
- Compare against plan S10 (`plan.md:942–954`) to the printed precision: α = 0.12495 / 0.49979 / 0.87338; R² = 1.000000000; integral
  GL8×4 8.7e-9; lock2all corrected θ ≤ 0.71°; lock3 4 prediction 0.218°; perSample 1.41e-5 cycles; slope 0.418 st/sample and the 23-cell
  census at 10 ms; nyquist 3.8631 st; glide 0.000 / ≤ 1 sample; bound 0 cells over, worst 1.0192, 5 unbounded. Any mismatch: stop and report.
- **Write** `specs/profundum-phase4-drop-note-behaviour/evidence/run-log/T001.txt` with the sweep command, its output, every section's
  key lines and wall clocks.

### T002 [P] Frozen pre-Phase-4 `MonoHandler` reference (SC-006 / FR-021; plan S11.1, S12.3)

- **Create** `specs/profundum-phase4-drop-note-behaviour/extract-mono-ref.js` (Node, CommonJS, no dependencies), modelled on
  `specs/profundum-phase2-edge-fold/extract-phase1-ref.js` (its `execFileSync('git', ['show', …])`, `fail()` and exactly-once anchor
  checks). It reads `git show 3d8ca19f:dsp/include/krate/dsp/processors/mono_handler.h` and writes
  `dsp/tests/unit/processors/mono_handler_pre_phase4_ref.h` with exactly:
  - a 4-line `// GENERATED by specs/profundum-phase4-drop-note-behaviour/extract-mono-ref.js` / `// from 3d8ca19f:… - DO NOT EDIT.` /
    `// SC-006 frozen ConstantTime reference;` / `// only the enclosing namespace differs (Krate::DSP::MonoRefPrePhase4).` comment;
  - the line `namespace DSP {` (base `:32`) → `namespace DSP { namespace MonoRefPrePhase4 {  // NOLINT(modernize-concat-nested-namespaces)`;
  - the line `}  // namespace DSP` (base `:499`) → two lines `}  // namespace MonoRefPrePhase4` and `}  // namespace DSP`;
  - every other byte unchanged. Fail (exit 1) unless each anchor occurs exactly once. Print a short diff summary.
- Inside the new namespace, `LinearRamp`, `detail::isNaN`, `semitonesToRatio`, `kA4FrequencyHz` resolve through the enclosing
  `Krate::DSP`; `MonoNoteEvent`, `MonoMode`, `PortaMode`, `NoteEntry`, `MonoHandler` are distinct types (no ODR clash).
- **Verify now (no build tree):** run the script; then compile a scratchpad driver that includes both
  `<krate/dsp/processors/mono_handler.h>` and the generated header and instantiates `Krate::DSP::MonoHandler` and
  `Krate::DSP::MonoRefPrePhase4::MonoHandler`:
  `"/c/Program Files/LLVM/bin/clang++.exe" -std=c++20 -fsyntax-only -Wall -Wextra -Idsp/include -Idsp/tests/unit/processors <driver.cpp>` →
  no diagnostics. Run the script twice: the output is byte-identical (`cmp`). Record in `evidence/run-log/T002.txt`.

### T003 [P] SC-020 append-only + public-surface checker `check-append-only.js` (plan S12.3)

- **Create** `specs/profundum-phase4-drop-note-behaviour/check-append-only.js` (Node, CommonJS). Usage:
  `node …/check-append-only.js [baseRef=3d8ca19f]` and `node …/check-append-only.js --self-test`.
- Checks on `dsp/include/krate/dsp/processors/mono_handler.h`:
  1. **No removed line:** `git diff -U0 <base> -- <file>` contains no line starting with `-` other than the `--- ` header. Print each
     offender with its hunk header.
  2. **Public surface:** for both the base text (`git show <base>:<file>`) and the working-tree text, walk the body of `class MonoHandler`
     (braces counted, comments and string literals skipped), tracking the access section (`public:`/`protected:`/`private:`, `class`
     defaults private). Extract the declared name of each member function/variable declaration in a public section (the identifier before
     the first `(` of a function declarator, or before `{`/`=`/`;` of a data member). Added public names = working set − base set. PASS iff
     the added set is **exactly** `{setGlideMode, glideMode}`.
  3. List namespace-scope additions (e.g. `enum class GlideMode`) as INFO lines.
  One PASS/FAIL line per check; exit non-zero on any FAIL.
- `--self-test` builds synthetic (base, new, diff) triples in memory and asserts: (i) a diff with one removed line → check 1 FAIL;
  (ii) a new text adding a public `void preciseRetime()` besides the two allowed → check 2 FAIL; (iii) a new text adding only
  `setGlideMode` → check 2 FAIL (missing `glideMode`); (iv) a new text adding exactly the two in `public:` and a private
  `processPreciseGlide` → PASS. Exit 0 iff all four behave as stated.
- **Verify now:** `--self-test` exits 0; on today's tree the plain run prints check 1 PASS and check 2 FAIL ("missing setGlideMode,
  glideMode" — expected before T007). Record both outputs in `evidence/run-log/T003.txt`.

---

## Group 1 — model emitter for the SC-013 infeasible set (sequential; `drop-model.js` only)

### T004 Emit `evidence/sc013-infeasible.json` by the component's feasibility criterion (C-6, O-3; deviation 5)

**RULING 2026-10-12 (O-1): SKIP this task entirely — see "Rulings applied". Return done, notes "skipped per O-1".**

- **Edit** `specs/profundum-phase4-drop-note-behaviour/drop-model.js`: append a new section after `secBound` (after `:540`) and its
  dispatch line `if (arg === 'infeasible') secInfeasible();`. Do not change any existing section.
- `secInfeasible()`: over fs ∈ {44100, 48000, 96000} × note ∈ {E0, E1, E2, C3} × A ∈ {6, 12, 24, 48} × T ∈ {0.010, 0.060, 0.200, 1.0} s ×
  s ∈ {0, 1/3, 2/3, 1} × R ∈ {0, 1} (1 536 cells, the `secBound` loop `:520–522`):
  - c = `makeCurve(A, s, R)`, D = c.end·T, D_s = D·fs (samples, double), fn(g) = `predictHybrid(c, T, fres, fs, g, D, cap, 4)` (run with
    env `GLN=8 GLS=2 GLS_LONG=4`, the component's quadrature);
  - p0 = fn(0); candidates n* ∈ {⌊p0⌋, ⌈p0⌉}; γ_lim = min(48, 0.15·D_s/3.0792014356780038);
  - a candidate is feasible iff (fn(−γ_lim) − n*)·(fn(γ_lim) − n*) ≤ 0; the cell is infeasible iff neither candidate is feasible.
- Write `specs/profundum-phase4-drop-note-behaviour/evidence/sc013-infeasible.json`:
  `{ "generatedBy": "drop-model.js infeasible", "criterion": "no root of predictHybrid(g)-n* in [-gLim,gLim] for n* in {floor,ceil}", "gammaLimFormula": "min(48, 0.15*D_s/3.0792014356780038)", "cells": [ {"note":"E0","fs":44100,"A":48,"Tms":10,"s":0,"R":0}, … ] }`
  with s written as one of the strings `"0"`, `"1/3"`, `"2/3"`, `"1"`. Print the count per T.
- Run `GLN=8 GLS=2 GLS_LONG=4 node …/drop-model.js infeasible` and `node …/drop-model.js slope`. Record both outputs in
  `evidence/run-log/T004.txt`, with the infeasible count per T beside `slope`'s census (23 at 10 ms, 0 elsewhere). **If the sets differ,
  record the cells in each set and add a line under "Defects found" for the main loop (C-6 census)** — do not change the criterion.
- Verify: the JSON parses (`node -e "JSON.parse(require('fs').readFileSync(…))"`); no cell has T ≥ 60 ms unless the run log explains it.

---

## Group 2 — CPU feasibility bench (alone; nothing else running)

### T005 Microbench the S3.4 kernel and the S5.4 worst-case solve before any header is written (C-16, R-1)

- Precondition: Groups 0–1 finished; no build, suite, model or agent running on the machine.
- **Create** `specs/profundum-phase4-drop-note-behaviour/bench/drop-kernel-bench.cpp`: a self-contained prototype (stdlib only, no repo
  header) of
  - the S3.3 `DropCurve` data and `makeDropCurve` (snap and bridged, S3.1–S3.5 equations and the C-12 constants) and `integrate` (GL8 on
    fixed panels, 2 per piece, 4 on pieces > 0.5·N, node constants in a `constexpr std::array`);
  - the S3.4 per-sample kernel (counter-based u, one geometric recurrence per exponential region seeded with `std::exp`, Horner elsewhere,
    float output);
  - the S5.2 per-sample correction (Horner b(x), one add, float cast) and the S5.6 mirror (one `std::exp2` per 16 samples, one add per
    sample);
  - the S4.2 quantized glide kernel (counter-based, `pow` only when the step index changes) and the C-15 declick (double rotation);
  - the S5.3 predictor and S5.4 solve with **exactly 17 evaluations forced** (1 shared + 2 × (2 bracket + 6 Newton/bisection)).
- Configurations (48 kHz, host block 64): **(A)** per-sample load: Drop elastic s = 1, R = 1, A = 24, T = 200 ms, snap-retriggered every
  100 ms + correction + mirror + quantized glide 24 st over 500 ms restarted every 1 s + declick armed every 100 ms; **(B)** one worst-case
  solve: bridged trigger at T = 1 ms, A = 48, s = 1, R = 1, from mid-fall of a previous T = 20 ms drop (most pieces, longest rise),
  17 evaluations; **(C)** a typical solve: snap, A = 24, T = 200 ms, s = 1/3, R = 0.
- Report ns/sample for (A), µs/solve for (B) and (C), and the budget fraction
  `F = (ns_A·48000 + 10·µs_B·1000) / 1e9` (one worst-case trigger per 100 ms, SC-019's rate).
- Compile: `"/c/Program Files/LLVM/bin/clang++.exe" -std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG <bench.cpp> -o <scratchpad>/drop-bench.exe`.
  Run 5 times, 20 s apart, through `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <scratchpad>/drop-bench.exe`.
- **Decision (recorded in `evidence/run-log/T005.txt` and as a new line "Measured 2026-…, T005, pinned, isolated, median of 5" in plan
  S10.7, `plan.md:953`):** if the median F ≤ **0.033 %** (two-thirds of the 0.05 % bar, leaving room for the MSVC factor — the Phase 3
  T005 precedent), T015 implements the plain S5.4 solve. Otherwise apply plan R-1's levers to the bench **in order** ((2) hoisted Hermite
  coefficients, (3) cached 2^{d/12} per node × 2^{γb/12}, (4) linearised start) and re-measure after each until F ≤ 0.033 %; record which
  levers T011/T015 must implement. If all levers leave F > 0.05 %, stop and report to the main loop. The bar is never relaxed.
- **RE-EXECUTION 2026-10-12 (epoch 2):** **RULING 2026-10-12 (main loop, T005 outcome — bar unchanged):** the bench's F = 0.0399 % with levers (2)+(3) meets the SC-019 bar (0.05 %) but not the task's advisory 0.033 % headroom target; the per-sample load, not the solve, is the cost. Decision: (i) levers (2) and (3) are REQUIRED in T011 / T015; lever (4) only if T015 proves residual < 1e-9 cycles on every SC-013 cell; (ii) the two per-sample reductions the bench proposed are REQUIRED where they keep the output bit-identical or within the existing bars: Quantized step boundaries as integer sample indices precomputed at retarget (ceil(k·N/|Δ|), one array of ≤ 128 entries, no per-sample division) in T009, and the S5.6 mirror accumulating (f_h − f_res) once per 16-sample hold in double in T015 (SC-024 is the judge); (iii) T005 is DONE without a re-run — the real gates are T018 (components alone, pinned) and T028 (lane alone) at the 0.05 % bar, with 0.033 % recorded as the advisory target. Rejected: re-measuring on another machine (the gate runs on this one). Executor: do NOT re-run the bench; mark T005 done quoting `evidence/run-log/T005.txt` and plan S10.7's "measured" paragraph (stages 0–3: 0.0722 / 0.0708 / 0.0399 / 0.0433 %; A 5.74 ns/sample at stage 2; B 12.32 µs; C 2.99 µs).

---

## Group 3 — CMake registration with compiling stubs (sequential, single task)

### T006 Register every Phase 4 file in CMake (deviations 1, 2)

**RULING 2026-10-12 (O-1): the `drop_curve.h` stub defines no `kMaxUpwardSemitonesPerSample` — see "Rulings applied".**

**Precondition: the main loop has recorded the S1/O-question rulings (see top).**

- **Create stubs**, each with a file comment naming the layer, "Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)" and the
  SCs it will carry (plan S11.1 table):
  - headers `core/drop_curve.h` ("Layer 0: Core Utility", includes stdlib + `core/db_utils.h` only), `primitives/retrigger_declick.h`
    ("Layer 1: DSP Primitive", L0 only), `processors/drop_envelope.h`, `processors/drop_phase_lock.h` ("Layer 2", include
    `core/drop_curve.h` and `core/db_utils.h`), `processors/note_memory.h` ("Layer 2", stdlib only): `#pragma once`, banner,
    `namespace Krate::DSP {}`. **Exception:** the `drop_curve.h` stub already declares
    `struct DropCurve { static constexpr double kMaxUpwardSemitonesPerSample = 0.15; };` (C-10, C-12) so T008's σ_up pin can name the
    single definition before T011 fills the struct.
  - TUs (file comment, the header(s) they test, `#include <catch2/catch_test_macros.hpp>`, no test case):
    `dsp/tests/unit/primitives/retrigger_declick_test.cpp`; `dsp/tests/unit/processors/{drop_envelope_test, drop_phase_lock_test,
    note_memory_test, mono_handler_glide_test, drop_note_behaviour_nonfinite_test, drop_note_behaviour_perf_test}.cpp`;
    `dsp/tests/unit/systems/{profundum_note_behaviour_test, profundum_note_behaviour_spectral_test}.cpp` (the systems stubs also include
    `<krate/dsp/systems/profundum_core.h>`, `<krate/dsp/systems/anchor_oscillator.h>` and all five new headers, proving they coexist).
- **Edit `dsp/CMakeLists.txt`** (deviation 2): after `include/krate/dsp/core/hungarian_algorithm.h` (`:100`) add
  `# Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)` and `include/krate/dsp/core/drop_curve.h`; after
  `include/krate/dsp/primitives/polyphase_halfband_oversampler.h` (`:126`) the same comment and `include/krate/dsp/primitives/retrigger_declick.h`;
  after `include/krate/dsp/processors/harmonic_fold.h` (`:161`) the comment and `drop_envelope.h`, `drop_phase_lock.h`, `note_memory.h`.
- **Edit `dsp/tests/CMakeLists.txt`** (plan S12.1), each with a Phase 4 comment block mapping TU → SCs ("ENUMERATED, not globbed"):
  - `dsp_primitives_tests`: after `unit/primitives/arp_lane_test.cpp` (`:145`), before `)` (`:146`): `unit/primitives/retrigger_declick_test.cpp`.
  - `dsp_processors_tests`: after `unit/processors/harmonic_fold_nonfinite_test.cpp` (`:259`): `drop_envelope_test.cpp`,
    `drop_phase_lock_test.cpp`, `note_memory_test.cpp`, `mono_handler_glide_test.cpp`, `drop_note_behaviour_nonfinite_test.cpp`,
    `drop_note_behaviour_perf_test.cpp` (all under `unit/processors/`).
  - `dsp_systems_tests`: after `unit/systems/anchor_oscillator_perf_test.cpp` (`:651`), before `)` (`:652`):
    `unit/systems/profundum_note_behaviour_test.cpp`, `unit/systems/profundum_note_behaviour_spectral_test.cpp`.
  - `-fno-fast-math` block: after `unit/systems/anchor_oscillator_nonfinite_test.cpp` (`:1138`), before `PROPERTIES` (`:1139`):
    `# Profundum Phase 4: injects NaN/Inf/subnormal bit patterns (SC-017). The perf TU stays out (shipping FP mode).` and
    `unit/processors/drop_note_behaviour_nonfinite_test.cpp`. Nothing else.
- **Verify:** reconfigure (`"$CMAKE" --preset windows-x64-release`); build `dsp_primitives_tests dsp_processors_tests dsp_systems_tests`
  in **one** invocation with zero warnings, recording the wall clock; run `dsp_processors_tests.exe "[profundum]~[long]"`,
  `dsp_systems_tests.exe "[profundum]~[long]"` and `dsp_primitives_tests.exe "[profundum]"` → green, Phase 1–3 counts unchanged.
  `git diff --stat` shows only the two CMake files plus the new files.

---

## Group 4 — `MonoHandler` glide modes (sequential; shared header, append-only)

All four tasks edit `processors/mono_handler.h` by **pure line insertion** (plan S4.1) and the TU `mono_handler_glide_test.cpp`
(tag `[processors][profundum][mono-glide]`). After each task run `node specs/profundum-phase4-drop-note-behaviour/check-append-only.js`:
check 1 must PASS every time; check 2 PASSes from T007 on.

### T007 `GlideMode` API + SC-006 ConstantTime bit identity (FR-020, FR-021, FR-028; SC-006, SC-008 enum clause)

**Failing tests first** (`mono_handler_glide_test.cpp`; include `<krate/dsp/processors/mono_handler.h>` and
`"mono_handler_pre_phase4_ref.h"`):
- `MonoHandler_GlideModeEnum`: `static_assert(static_cast<int>(GlideMode::ConstantTime) == 0 && …ConstantRate == 1 && …Quantized == 2)`;
  `static_assert(std::is_same_v<std::underlying_type_t<GlideMode>, uint8_t>)`; a default-constructed `MonoHandler` reports
  `glideMode() == GlideMode::ConstantTime`; `setGlideMode(static_cast<GlideMode>(3))` and `(255)` leave it unchanged (E-14).
- `MonoHandler_GlideModeConstantTimeUnchanged` (SC-006): a step-script engine `monoReplay(live, ref, script)` that applies each step to a
  live `MonoHandler` (ConstantTime) and to `MonoRefPrePhase4::MonoHandler` and asserts, **bitwise** (`std::bit_cast<uint32_t>`), every
  `MonoNoteEvent` field (`frequency`, `velocity`, `retrigger`, `isNoteOn`) and every `processPortamento()` output and
  `getCurrentFrequency()`. Scripts:
  (a) the call sequence of **every** `TEST_CASE` in `dsp/tests/unit/processors/mono_handler_test.cpp` (51 cases), transcribed into
  `MonoStep` records `{op, a, b, samples}` (ops: prepare, noteOn, noteOff, setMode, setLegato, setPortamentoTime, setPortamentoMode, reset,
  process-n) — read that TU and transcribe; do not edit it;
  (b) a 10 000-event script from a 64-bit xorshift seeded `0x5EED0004`: noteOn (note 24–96, velocity 1–127), noteOff (a held note 70 %,
  random 30 %), setMode (0–2), setLegato, setPortamentoTime ∈ {0, 1, 50, 300, 2000, 10000, NaN, +Inf, −5, 20000}, setPortamentoMode,
  prepare ∈ {44100, 48000, 96000}, reset, each followed by 0–64 `processPortamento()` calls.
  **Additionally** call `setGlideMode(GlideMode::ConstantTime)` on the live handler before every 100th step (a same-mode set must be a no-op).
- Run `mono_handler_test.cpp`'s cases too: `dsp_processors_tests.exe "*MonoHandler*" "*mono*"` — every pre-existing case passes unmodified.

**Implement** (plan S4.1, insertion only): after `PortaMode` (`:66`) add the `GlideMode` enum with doc comment; in the public section add
`void setGlideMode(GlideMode mode) noexcept` (ignore out-of-enum; return if equal; otherwise store — the switch actions of S4.3 come in
T009) and `[[nodiscard]] GlideMode glideMode() const noexcept`; after `float sampleRate_{kDefaultSampleRate};` (`:496`) append the private
state of S4.1 (`glideMode_`, `gpStart_`, `gpTarget_`, `gpLen_`, `gpN_`, `gpActive_`, `qOut_`, `qFreq_`) and
`static constexpr double kMaxUpwardGlideSemitonesPerSample = 0.15;  // == DropCurve::kMaxUpwardSemitonesPerSample (C-10; pinned by SC-007)`.
No hook yet. Build `dsp_processors_tests`, zero warnings; `check-append-only.js` → both checks PASS.

### T008 ConstantRate law (FR-022, FR-026, D-13, C-10; SC-007)

**RULING 2026-10-12 (O-1): no σ_up floor, no 160 / 96-sample pin — see "Rulings applied".**

**Failing tests first:**
- `MonoHandler_GlideModeConstantRate` (SC-007): for fs ∈ {44100, 48000, 96000}, `setGlideMode(ConstantRate)`, `setPortamentoTime(120)`
  (ms per octave), start note 60 (snapped by a first noteOn), then a staccato noteOn (PortaMode::Always) to 60 + Δ for
  Δ ∈ {±1, ±7, ±12, ±24}; plus Δ = +24 at T = 10 000 ms per octave. Per run:
  - **Duration:** n_end = the first sample index (from the noteOn, first `processPortamento()` = sample 1) at which the output equals,
    bitwise, the settled target frequency (a second handler snapped to the target with T = 0 provides it); assert
    |n_end − T·|Δ|·fs/12000| ≤ 1.
  - **Slope:** least-squares slope of 69 + 12·log2(f/440) (double, from the float output) over the interior samples [0.05·n_end, 0.95·n_end]
    equals 12 000/(T·fs) within 1e-6 relative.
  - **Retarget:** at n_end/2 a noteOn to 60 − Δ (overlap with the first key held) keeps the slope within the same 1e-6 bar and lasts
    T·|Δ_remaining|/12 within ±1 sample, Δ_remaining measured from the pitch at the retarget sample.
- **σ_up floor pin (C-10, plan SC-007 row):** at 48 kHz, T = 1 ms per octave (r = 0.25 st/sample): an upward Δ = +24 glide lasts exactly
  ⌈24/`DropCurve::kMaxUpwardSemitonesPerSample`⌉ = 160 samples (the first n with 0.15·n ≥ 24 in double — 160, checked in the test);
  a downward Δ = −24 glide lasts exactly ⌈24/0.25⌉ = 96 samples. (Include `<krate/dsp/core/drop_curve.h>`; the T006 stub already
  defines the constant. `MonoHandler` must not include it — its private copy is pinned by this test, C-10.)

**Implement** (S4.1 hooks and S4.2 ConstantRate): insert the five hook lines of the S4.1 table (top of `processPortamento()` after `:291`;
top of `updatePortamentoTarget` after `:450`; end of `setPortamentoTime` after `:285`; end of `prepare` after `:124`; end of `reset()` after
`:355`) and the private helpers `processPreciseGlide()`, `preciseRetarget(float targetSemitones, bool glide)`, `preciseRetime()`,
`currentPreciseSemitones()` appended in the private section. ConstantRate: r = 12·1000/(T·fs); upward r_eff = min(r, 0.15), downward
r_eff = r; p(n) = start + sign(Δ)·min(r_eff·n, |Δ|) from the `uint64_t` counter; output
`semitoneToFrequency(static_cast<float>(p))`; set `currentFrequency_`. Snap (glide false or T = 0) sets position, output and
`currentFrequency_` to the target (E-7). Re-run T007's cases: still green.

### T009 Quantized law + mode switch (FR-023, FR-024, FR-028, C-11, D-5; SC-008; E-7, E-8, E-9)

**RULING 2026-10-12 (O-1): no private σ_up floor — see "Rulings applied".** **T005 ruling:** Quantized step boundaries are integer sample indices precomputed at retarget (ceil(k·N/|Δ|), ≤ 128 entries), no per-sample division; SC-008's ±1-sample timing is unchanged.

**Failing tests first:**
- `MonoHandler_GlideModeQuantizedSteps` (SC-008): Δ ∈ {±1, ±5, ±12, ±24}, T ∈ {50, 500} ms, plus Δ = ±24 at T = 10 000 ms; all three
  rates; start notes {28, 40, 60, 76, 100} where 0 ≤ start + Δ ≤ 127. Per run: the output changes exactly |Δ| times; each change is exactly
  one semitone (69 + 12·log2(f/440) differs by 1 ± 1e-4); every output is within 1e-4 relative of `midiNoteToFrequency(q)` for an integer q;
  step k occurs at round(k·T·fs/(1000·|Δ|)) ± 1 samples (k = 1 … |Δ|); the last step lands on the target.
- `MonoHandler_GlideModeQuantizedRetarget` (C-11): mid-glide (after step 3 of 60 → 72, T = 500 ms, 48 kHz) a retarget to 48: every output
  stays on the integer grid, the first change after the retarget is **downward**, and the glide makes exactly |48 − q| steps at
  ⌈k·N/|Δ|⌉ ± 1 from the retarget, with q the output note at the retarget.
- `MonoHandler_GlideModeSwitch` (FR-024, E-9): for every ordered pair of the three modes, start a 60 → 84 glide (T = 500 ms, 48 kHz) in the
  first mode, switch at 37 % of the glide: every per-sample output change is ≤ 1 semitone (|Δ semitone| ≤ 1 + 1e-4); entering ConstantTime
  the remainder lasts T ± 1 sample (FR-024: snap + retarget); entering Quantized from a fractional pitch the output is ⌊p⌋ (up-glide) and
  stays on the grid; entering ConstantRate continues at the rate 12 000/(T·fs) within 1e-6 relative. The final settled frequency equals the
  target bitwise.
- `MonoHandler_GlideModeEdgeCases`: E-7 — T = 0 in each mode snaps (first `processPortamento()` already equals the target bitwise);
  E-8 — `prepare(96000)` at 50 % of a ConstantRate and of a Quantized glide (started at 48 kHz) keeps the current output (bitwise) and the
  remainder lasts the new-rate duration ± 1 sample; `reset()` mid-glide in each new mode: the next noteOn snaps (first note after reset).
- `MonoHandler_GlideModeNoAllocation` (SC-018 part): inside one `AllocationScope`, every ordered pair of `setGlideMode` mid-glide, 10 000
  `processPortamento()` in each mode, retargets, `setPortamentoTime`, `prepare`, `reset`; read
  `AllocationDetector::instance().getAllocationCount()` **before** the scope closes → 0. Positive control: one `new int` inside an
  identical scope reads ≥ 1 (then `delete`).

**Implement:** Quantized per S4.2 (start q₀ integer per C-11; N = max(T·fs/1000, |Δ|/0.15); k(n) = min(|Δ|, ⌊n·|Δ|/N⌋);
`qFreq_ = semitoneToFrequency(static_cast<float>(q))` recomputed only when k changes); `preciseRetime` per S4.2; the full
`setGlideMode` switch table of S4.3. Re-run T007/T008 cases and `check-append-only.js`.

### T010 FR-025 unchanged surfaces + early shared-header gate (FR-025, FR-063; SC-020 early read)

**Failing test first:** `MonoHandler_GlideModeUnchangedSurfaces`: replay T007's scripts (a) and (b) with T ∈ {0, 50 ms} (scripts
re-seeded with portamento times restricted to {0, 50}) under **ConstantRate and Quantized** on the live handler against the frozen reference
running ConstantTime. Assert bitwise equality of every `MonoNoteEvent` field at every event, and at every noteOn / noteOff / setMode event
the same glide-vs-snap decision: "the first `processPortamento()` after the event equals the settled target frequency" is true on the live
handler iff it is true on the reference. Explicit cells: first note ever snaps; staccato under `PortaMode::LegatoOnly` snaps
(`mono_handler.h:186–199`); staccato under `Always` glides; overlap glides; after `reset()` the next note snaps. Every settled pitch equals
the reference's settled frequency bitwise.

**Then** (no code change expected; any red is a defect of T007–T009): build `dsp_processors_tests dsp_systems_tests ruinae_tests
seraphis_tests vorago_tests innexus_tests` in **one** invocation (`-j`), log the wall clock; run concurrently, each to its own log:
`dsp_processors_tests.exe "*MonoHandler*"`, `dsp_systems_tests.exe "*PolySynth*"`, `ruinae_tests.exe`, `seraphis_tests.exe`,
`vorago_tests.exe`, `innexus_tests.exe` (each with `"~[long]~[.perf]"`; the full `[long]` lanes run in T026). All green. Run
`check-append-only.js` → PASS. Record outputs in `evidence/run-log/T010.txt`.

---

## Group 5 — `DropCurve` + `DropEnvelope` (sequential; `drop_curve.h`, `drop_envelope.h`, `drop_c1_checker.h`, `drop_envelope_test.cpp`)

### T011 `DropCurve` (L0), `dropCheckC1`, `DropEnvelope` snap path: shapes, amount/time, rebound, lifecycle (FR-001–FR-006, FR-008, FR-010; C-1, C-2, C-7, C-12; SC-001–SC-003, `DropEnvelope_Lifecycle`)

**RULING 2026-10-12 (O-1): `DropCurve` defines no `kMaxUpwardSemitonesPerSample` — see "Rulings applied".** **T005 ruling:** lever (2) (Hermite coefficients hoisted to the piece entry) is required.

Files: `core/drop_curve.h`, `processors/drop_envelope.h`, new `dsp/tests/unit/processors/drop_c1_checker.h`, `drop_envelope_test.cpp`
(tag `[processors][profundum][drop]`).

**Failing tests first:**
- `drop_c1_checker.h` (namespace `Krate::DSP::DropTest`): `struct DropC1Segment { std::size_t start, len; double bound; };`
  `struct DropC1Report { double worstRatio, worstBoundaryJump; bool inRange, pass; };`
  `inline DropC1Report dropCheckC1(std::span<const double> g, std::span<const DropC1Segment> segs, double lo, double hi)` with the
  `anchorCheckC1` rules (plan S11.2): interior |Δ²g| ≤ 1.05·bound; at each boundary |Δg[b+1] − Δg[b]| ≤ 1.05 × the larger adjacent bound;
  a zero bound with non-zero Δ² fails; every sample in [lo, hi]. `inline double dropHermitePeakD2(double g0, double gp0, double g1,
  std::size_t len)` = max(|6Δ − 4m₀|, |6Δ − 2m₀|)/L² with Δ = g1 − g0, m₀ = gp0·L (the `anchorHermitePeakD2` formula,
  `anchor_oscillator_test_helpers.h:121`). `inline bool dropCurveBitEqual(const DropCurve&, const DropCurve&)` comparing every field by
  `std::bit_cast` (no struct memcmp: padding).
- `DropEnvelope_C1CheckerPositiveControl`: a ramp 0 → 1 over 100 samples then held (segments {0,100,0}, {100,100,0}) → `pass == false`;
  a sampled raised cosine with its analytic bound → `pass == true`.
- `DropEnvelope_ShapeFamilies` (SC-001): A = 24, T ∈ {10, 100, 1000} ms, three rates, R = 0, `trigger(true)`; p[n] = d[n]/A, u = n/N.
  α = mean of p[n] over n = 0 … ⌊N⌋. **Exponential** (s = 0): α ≤ 0.2; least-squares fit of ln p on u over u ∈ [0.05, 0.8] has R² ≥ 0.999
  (`INFO` the fitted rate; expect 8.000). **Linear** (s = 1/3): |α − 0.5| ≤ 0.005; max |p − (1 − u)| ≤ 0.01 over u ∈ [0.05, 0.95].
  **Logarithmic** (s = 2/3): α ≥ 0.8; ln(1 − p) on (1 − u) over u ∈ [0.2, 0.95], R² ≥ 0.999. **Elastic** (s = 1): ≥ 2 sign changes of d after
  the first landing; ε = −min d after landing within ±2 % of 0.15·A (∈ [3.528, 3.672]); each later peak magnitude ≤ 0.5 × the previous;
  |d| ≤ 0.01 st for every n ≥ ⌈2N⌉. `INFO` α per family.
- `DropEnvelope_ShapeContinuity` (SC-001): T = 100 ms, 48 kHz, A = 24, R = 0. 65 values s_k = k·(2/3)/64: α strictly increasing,
  |α_{k+1} − α_k| ≤ 0.02. 65 values s_k = 2/3 + k/192: ε strictly increasing for k ≥ 1, ε(s_0) = 0, |ε − (3s − 2)·0.15·A| ≤ 0.02·(3s − 2)·0.15·A
  for k ≥ 1, |ε_{k+1} − ε_k| ≤ 0.005·A. 193 values s_k = k/192: max over n ≤ 2N of |p(n; s_k) − p(n; s_{k+1})| ≤ 0.025.
- `DropEnvelope_AmountTime` (SC-002): s ∈ {0, 1/6, 1/3, 1/2, 2/3}, A ∈ {1, 12, 48}, T ∈ {1, 20, 200, 4000} ms, three rates, R = 0, snap:
  |d[0] − A| ≤ 1e-5; (a) for every n ≥ t_L + 1: d[n] is exactly 0.0f (bit test) and `isActive()` is false after processing it; (b) the
  first n with |d[n]| ≤ 0.01 equals n_1c ± 1, where n_1c = the smallest integer n with |`plannedCurve().valueAt(n)`| ≤ 0.01 (double scan);
  `INFO` n_1c and the bisected continuous crossing per cell.
- `DropEnvelope_Rebound` (SC-003): R ∈ {0, 0.25, 0.5, 1}, s ∈ {0, 1/3, 2/3, 5/6, 1}, A ∈ {12, 48}, T ∈ {20, 200} ms, three rates. For R > 0
  or s > 2/3: ρ = −min d within ±2 % of w·0.15·A + R·0.25·A (w = max(0, 3s − 2)). Settle sample (last n with |d| > 0.01, plus 1) ≤
  t_L + 0.5·N + 1 for s ≤ 2/3, ≤ 2N for s > 2/3. R = 0 and s ≤ 2/3: d[n] ≥ 0 at every n. ρ strictly increasing over R ∈ {0, 0.25, 0.5, 1}
  for every (s, A, T, fs).
- `DropEnvelope_RecurrenceMatchesClosedForm` (R-5): for every family and R ∈ {0, 1}, A = 48, T ∈ {1, 4000} ms, 96 kHz:
  max_n |d[n] − float(valueAt(n))| ≤ 1e-9 + ½ ulp(float(valueAt(n))) — i.e. d[n] equals `static_cast<float>(valueAt(n))` or its float
  neighbour; record the worst double difference of the recurrence (expect < 1e-10 st).
- `DropEnvelope_Lifecycle` (FR-002, FR-008): before `prepare`, `process()` returns 0.0f and `trigger`, every setter and
  `processBlock(p, 64)` have no effect (after `prepare(48000)` the next 10 000 samples of a trigger equal those of a never-touched instance,
  memcmp); `prepare` clamps 10 000 → 22 050 and 400 000 → 192 000 (observable via N = T·fs/1000 in `plannedCurve().fallSamples`);
  `reset()` keeps parameters (a trigger after reset renders bit-identically to an instance given the same setters once);
  `processBlock(nullptr, 64)` and `processBlock(p, 0)` leave the next `process()` bit-identical to an instance that never got the call;
  `currentOffset()` equals the last returned sample after every `process()` and after `processBlock` of {1, 37, 512}; `isActive()` is
  false before any trigger.

**Implement:** `core/drop_curve.h` per plan S3.1–S3.3 and S3.5 (the full struct including `PieceKind`, the C-12 constants as
`static constexpr` members — the only definitions — `valueAt`, `slopeAt`, `template <class F> integrate`, `pieceD2Bound(i)`, a
`pieceCount()`/`pieceStart(i)`/`pieceEnd(i)` accessor set for tests, and `makeDropCurve(A, timeMs, shape, rebound, sampleRate, bridged,
dNow, slopeNow)`; bridged pieces may be implemented now and are gated in T012). `processors/drop_envelope.h` per S3.4 and S3.6: setter
rule = non-finite keeps previous; subnormal (float exponent bits 0, mantissa ≠ 0, via `std::bit_cast<uint32_t>`) → +0.0f; then clamp;
`trigger(true)` snap; per-sample kernel per S3.4 (counter-based u, geometric recurrence seeded per region, Horner, float out, exactly 0.0f
and idle from `endSample`); `plannedCurve()`; `phaseIntegralSeconds()` lazily via `integrate`; aliases of every `DropCurve` constant. Apply
any T005 lever marked for the kernel. Build, zero warnings, all T011 cases green.

### T012 Bridged retrigger, C¹, invariance, latching, phase integral (FR-006, FR-007, FR-010, FR-056, FR-057; C-2, C-8; SC-004, SC-005, SC-024(a); E-1, E-2, E-4)

**RULING 2026-10-12 (O-1): bridge rise L_r = L_min always; three bridge branches, no σ_up branch — see "Rulings applied".**

**Failing tests first** (`drop_envelope_test.cpp`):
- `DropEnvelope_C1` (SC-004): `dropCheckC1` on d (double copy of the float trace) with per-piece bounds `pieceD2Bound(i)`, for every
  anchor family s ∈ {0, 1/3, 2/3, 1} and blends s ∈ {1/6, 1/2, 5/6} × R ∈ {0, 1}, A ∈ {1, 12, 48}, T ∈ {1, 20, 200, 4000} ms, 44.1 and
  96 kHz; range [−ρ_max − 2.7, A] (C-8). Snap trigger is the only allowed step (checked from sample 0 on).
- `DropEnvelope_RetriggerContinuation` (SC-004, FR-007, C-8): bridged `trigger(false)` at mid-fall (u = 0.3), mid-rebound (R = 1, halfway
  between t_L and the tail's first knot), mid-elastic (s = 1, at t = 1.3·T) and at idle; over T ∈ {1, 20, 200, 4000} ms × A ∈ {1, 12, 48} at
  44.1 and 96 kHz. The checker covers the previous drop, the brake, the rise, the onset fillet and the new drop as separate segments
  (bounds from `pieceD2Bound`) → pass. Assert the bridge length (samples from the trigger to the new fall start, `plannedCurve().fallStart`)
  equals L_br + L_r of S3.5 exactly (computed in the test from the pre-trigger `valueAt`/`slopeAt` at n + 1). Assert the grid exercises all
  four branches and `INFO` the first cell hitting each: L_br = 1 (T = 1 ms steep slope); L_br > 1 with brake excursion ≤ 0.5 st (mid-fall,
  T ≥ 20 ms); L_min-bound rise (A = 1); σ_up-bound rise (ΔA = 48, rise = ⌈1.5·ΔA/0.15⌉ samples, e.g. 480 for 0 → 48). During every rise the
  per-sample upward step ≤ 0.15 st.
- `DropEnvelope_EveryCellRetrigger` (E-4): `trigger(false)` before every sample for 20 000 samples (A = 48, T = 1 ms, s = 1, R = 1, 44.1 kHz):
  every d ∈ [−ρ_max − 2.7, 48] (ρ_max = 0.15·48 + 0.25·48 = 19.2) and finite.
- `DropEnvelope_AmountZero` (E-1): `setAmount(0)` then snap and bridged triggers, every family/R: d is exactly 0.0f on every sample.
- `DropEnvelope_BlockInvariance` (SC-005): every family × R ∈ {0, 1}, A = 24, T = 200 ms, 48 kHz, with a bridged retrigger at sample 3 001:
  traces rendered through `processBlock` with host blocks {1, 37, 512} and through `process()` are memcmp-equal.
- `DropEnvelope_RateInvariance` (SC-005, FR-010): A = 24, T = 100 ms, every family, R ∈ {0, 1}: p(u) at the 44.1 kHz sample positions vs the
  96 kHz render linearly interpolated at the same u, |Δp| ≤ 1e-4; landing sample, settle sample and elastic-period knots match
  ms·fs/1000 within 1 sample at each rate.
- `DropEnvelope_Latching` (SC-005, FR-003, D-4): mid-drop calls to all four setters leave the remaining trace memcmp-equal to an untouched
  render and `dropCurveBitEqual(plannedCurve(), before)` true; the next trigger uses the new values (its curve equals a fresh instance's).
- `DropEnvelope_PhaseIntegral` (SC-024(a), C-2; deviation 8): s ∈ {k/64, k = 0 … 64}, R ∈ {0, 0.5, 1}, A ∈ {1, 12, 48},
  T ∈ {1, 20, 200, 4000} ms, three rates (7 020 cells): `phaseIntegralSeconds()` vs the test's composite Simpson of
  (2^{valueAt(n)/12} − 1)/fs over each piece (panels per piece = max(2000, ⌈1000·len/shortestLen⌉), even) within 1e-6 relative (absolute
  1e-12 s when the reference is below 1e-6). Also on bridged curves (one bridged trigger per (A, T) at u = 0.3). Measure the case's wall
  clock; if > 15 s, tag it `[long]` and add `DropEnvelope_PhaseIntegralSmoke` (the four anchors only).

**Implement:** the bridged `trigger(false)` (evaluate the running curve at n + 1, `makeDropCurve(..., bridged = true, d, d')`, reset
the counter), brake → rise → onset fillet per S3.5, `pieceD2Bound` for every `PieceKind` (cubic Hermite via the `dropHermitePeakD2` form;
exponential (A + ρ_R)·k²/N² at the piece start, mirrored at its end; blended pieces as the blend of family bounds). Build, zero warnings,
T011 + T012 cases green.

---

## Group 6 — `NoteMemory` (sequential; `note_memory.h`, `note_memory_test.cpp`)

### T013 `NoteMemory` resolver (FR-030–FR-036, D-7, D-12; SC-009(a))

**Failing tests first** (`note_memory_test.cpp`, tag `[processors][profundum][note-memory]`):
- `NoteMemory_ResolverTable` (SC-009(a)): for all 4 modes × 2 Retrigger × 5 transitions × 2 audible = 80 cells, `resolve` (both the
  member and the `static constexpr` form) equals the expected `Actions` built in the test from the FR-034 table (`spec.md:435–440`) and
  FR-033's derivation: phase = Reset iff oscillatorRestart ∧ retrigger == Reset; dropTrigger = anchorRearm = envelopeRestart;
  declick = (phase == Reset) ∧ audible; dropSnap = ¬audible ∨ declick. Assert the 80 cells at compile time too (`static_assert` over a
  `constexpr` loop). **Continue collapse:** under Continue, {AlwaysRetrigger, EnvelopeRetriggerOnly} and {Legato, OscillatorContinuous}
  return `Actions` equal in every acted-on field — `phase`, `envelopeRestart`, `dropTrigger`, `dropSnap`, `anchorRearm`, `declick` — while `oscillatorRestart` keeps the raw FR-034 table bit (it is the input to the `phase` derivation and has no effect of its own under Continue, D-12; wording ruled by the main loop 2026-10-12 on T013, no bar change) in all 10 (transition, audible) cells; under Reset every pair of modes differs in ≥ 1 cell.
  **RE-EXECUTION 2026-10-12 (epoch 2):** the executor's option A is ruled; the files it wrote stand — re-read them, confirm the collapse checks compare the six acted-on fields (compile time and runtime) and that no test compares whole structs under Continue, then return done. No build (the gate builds).
  Default `mode() == AlwaysRetrigger`, `retrigger() == Reset`; `setMode(static_cast<Mode>(4))` and `setRetrigger(static_cast<Retrigger>(2))`
  are ignored; enum underlying values 0…3 / 0…1 / 0…4 / 0…1 `static_assert`ed.

**Implement** plan S6: the nested enums and `Actions` aggregate (bools + `PhasePolicy`), the `static constexpr std::array<std::array<std::uint8_t, 5>, 4>`
table of FR-034 cells, `constexpr` derivation. Build, zero warnings, green.

---

## Group 7 — `RetriggerDeclick` (sequential; `retrigger_declick.h`, `retrigger_declick_test.cpp`)

### T014 `RetriggerDeclick` (FR-041–FR-044, D-8, C-15; SC-011 unit parts)

**Failing tests first** (`retrigger_declick_test.cpp`, tag `[primitives][profundum][declick]`):
- `RetriggerDeclick_Timing` (SC-011, C-15): at 44.1, 48, 96 kHz: L_d = ⌈1.5·fs/1000⌉ (67 at 44.1 kHz — 66.15 → 67; 72; 144); after `arm()`,
  `samplesUntilSwitch()` = L_d − 1 and decrements by 1 per `process()`; the gain sequence g[k] equals ½(1 + cos(π(k+1)/L_d)) (double
  reference) within 1e-7 for k < L_d − 1; g[L_d − 1] is exactly 0.0f (bit test) and `samplesUntilSwitch()` = 0 there; the following sample
  is exactly 1.0f and `isActive()` false. Restart latency L_d − 1 ≤ ⌈1.5·fs/1000⌉ + 1 (recorded). Peak |Δg| ≤ π/(2L_d)·1.0001. C¹: max
  |Δ²g| over the dip ≤ 1.05·π²/(2L_d²) (raised-cosine bound, plan S11.2), excluding the deliberate return step at the switch.
- `RetriggerDeclick_RearmNoRestart` (FR-043, E-5): `arm()` at k = 0 and again at k = 20: the gain trace is bit-identical to a single arm.
- `RetriggerDeclick_Idle` (FR-044): never armed, `process()` returns exactly 1.0f for 10 000 samples; `processBlock` of {1, 37, 512}
  equals the per-sample trace (memcmp); `processBlock(nullptr, n)` and `(p, 0)` are no-ops; before `prepare`, `arm()` is a no-op and
  `process()` returns 1.0f; `prepare(NaN)` is a no-op (deviation 7).

**Implement** plan S7 (double rotation recurrence seeded at `arm()`, switch sample forced to 0.0f, `kDeclickMs = 1.5f`, rate clamp
[22050, 192000]). Build `dsp_primitives_tests`, zero warnings, green.

---

## Group 8 — `DropPhaseLock` (sequential; `drop_phase_lock.h`, `drop_phase_lock_test.cpp`)

### T015 Predictor, lazy solve, bypass, coalescing (FR-053–FR-059; C-3, C-4, C-6, C-13, C-14; SC-024(b), SC-026 component part; deviation 11)

**RULING 2026-10-12 (O-1, O-4): γ_lim = 48 constant, no γ_cap, no json; see "Rulings applied" for the `DropPhaseLock_InfeasibleClamp` replacement.** **T005 ruling:** lever (3) (per-node cache of 2^{(d+carry)/12} and b, one exp2(γb/12) per node per pass) is required; lever (4) only with a proven residual < 1e-9 cycles on every SC-013 cell; the S5.6 mirror accumulates (f_h − f_res) once per 16-sample hold in double (SC-024 judges).

**Failing tests first** (`drop_phase_lock_test.cpp`, tag `[processors][profundum][drop-lock]`; include `drop_envelope.h` in the test only):
- `DropPhaseLock_PredictedExcessPhase` (SC-024(b), C-13): over fs ∈ {44.1, 48, 96} kHz × {E0, C3} × A ∈ {1, 12, 48} × T ∈ {1, 20, 200, 4000} ms
  × s ∈ {0, 1/3, 2/3, 5/6, 1} × R ∈ {0, 0.5, 1}: `DropPhaseLock::predictExcess(curve, fRes, fs, 1, 0, 0.0, 1e12, 0.0, D_s, 0.0, 0.0, 0.0)`
  (U = 1, no clamp, γ = 0, no carry) vs the per-sample double sum Σ_n f_res·(2^{d[n]/12} − 1)/fs over n ∈ [0, ⌈endSample⌉ + 2] with d[n]
  the `DropEnvelope` float output: |Δ| ≤ 2e-5 cycles; `INFO` the worst cell (expect ≈ 1.41e-5 at C3, 44.1 kHz, A = 48, T = 1 ms).
- `DropPhaseLock_LandingOnStaircaseModel` (deviation 11): E1 and C3, 48 kHz, A ∈ {12, 48}, T ∈ {60, 200} ms, four anchors, R ∈ {0, 1},
  gridPhase 0, `SnapReset`, minF0 8, cap = 0.8·24 000: render d through `DropEnvelope`, d_c through `process()`; the test's staircase model
  Φ = Σ_n (f_h(n) − f_res)/fs, f_h held from samples n ≡ 0 (mod 16) as clamp(f_res·2^{d_c[n]/12}, 8, cap), summed over [0, ⌈D_s⌉]:
  |Φ − `targetCycles()`| ≤ 1/360 cycle; `landingFeasible()` true; |`correctionGamma()`| ≤ min(48, 0.15·D_s/`kBumpSlopePeak`);
  d_c is exactly 0.0f for n ≥ ⌈D_s⌉; `solveResidualCycles()` recorded.
- `DropPhaseLock_InfeasibleClamp` (C-6): the first cell of `evidence/sc013-infeasible.json` (read in the test via `KRATE_DSP_TESTS_DIR`):
  `landingFeasible()` false and |γ| = γ_lim exactly (double compare after the same formula).
- `DropPhaseLock_FollowBypass` (SC-026, FR-058): `setBypass(true)`; after triggers of every kind, `process(x)` returns x bitwise for
  10 000 values covering ±48, ±0.0f, 1e-30f and the planned d of an elastic R = 1 drop; `processBlock` likewise.
- `DropPhaseLock_TriggerCoalescing` (S5.4, SC-019 functional half): 64 `trigger` calls with different curves at one sample offset, then
  `processBlock` 4 096 samples: memcmp-equal to an instance that received only the 64th trigger.
- `DropPhaseLock_Lifecycle`: before `prepare`, `process(x)` returns x and `trigger` is a no-op; `reset()` clears plan, pending flag and
  accumulator (`accumulatedExcessCycles() == 0.0`); deviation-7 inputs (non-finite / ≤ 0 `fResHz`, `minF0Hz`, `capHz`) are ignored.

**Implement** plan S5.1–S5.4 (+ T005's chosen levers): `predictExcess` static (4 exact updates mirroring `static_cast<float>` of f_core,
clamp-aware `DropCurve::integrate` of the remainder, EM terms, analytic g′); `trigger` latches into a pending slot (last call wins);
`process`/`processBlock` run a pending solve once (1 shared evaluation, per candidate 2 bracket + ≤ 6 Newton/bisection, early stop at
1e-9 cycles, infeasible → ±γ_lim, choose smaller |γ|), then emit d + γ·b(n/D_s) (Horner, counter-based x) and exactly d from D_s on.
`kPitchUpdateInterval = 16` restated; aliases of the `DropCurve` constants. Build, zero warnings, green.

### T016 Trigger kinds, staircase mirror, bridged carry, C¹ and RT (S5.5, S5.6; FR-054, FR-059, FR-065; SC-013(c) component part, SC-025 allocation)

**RULING 2026-10-12 (O-1): SC-013(c) gate Γ = γ_an + carry + 1 ulp(A), no γ_cap term — see "Rulings applied".**

**Failing tests first:**
- `DropPhaseLock_Mirror` (S5.6): over an E1 48 kHz drop with gridPhase 5, `accumulatedExcessCycles()` equals the test's double staircase
  sum (held from samples with (n − 5) % 16 == 0) within 1e-12 relative at every sample, including 2 s past landing.
- `DropPhaseLock_TriggerKinds` (FR-059): (i) `SnapReset` after 1 s of a drop: after the first post-trigger `process()`,
  `accumulatedExcessCycles()` equals exactly (f_h(0) − f_res)/fs of the new trajectory (zeroed); (ii) `SnapFreeRunning` after an
  interrupted A = 48, T = 4000 ms drop (stopped at 300 ms): the accumulator equals its pre-trigger value plus the first sample's
  contribution (not zeroed), and `targetCycles()` is an integer within 1 of (pre-trigger accumulator + Φ̂_new); (iii) `Bridged`
  mid-fall, mid-rebound, mid-elastic: |d_c[n] − d_c[n−1]| at the trigger sample is within the `dropCheckC1` boundary rule (no step) and
  the staircase model of the whole trace lands within 1/360 cycle of `targetCycles()` (feasible cells, E1 and C3, 48 kHz, T = 200 ms).
- `DropPhaseLock_C1` (SC-013(c) component part, FR-054): `dropCheckC1` on d_c with the S11.2 segment bounds: pieceD2Bound(i) +
  |γ|·32/D_s² + the carry Hermite's `dropHermitePeakD2(c₀, c₀′, 0, L)` over its span; snap and bridged triggers, every family, R ∈ {0, 1},
  A ∈ {12, 48}, T ∈ {10, 60, 200, 1000} ms, E0 and C3, 48 kHz → pass; cornered-ramp positive control fails.
- `DropPhaseLock_NoAllocation` (SC-025): one `AllocationScope` around `setBypass`, 1 000 triggers of every kind with solving `process()`
  and `processBlock` calls; count read live through `AllocationDetector::instance().getAllocationCount()` before the scope closes → 0;
  positive control (`new int` in an identical scope) ≥ 1.

**Implement** S5.5 (kind handling, Hermite carry from (c₀, c₀′) to (0, 0) over brake + rise, included in the integrand) and S5.6 (mirror on
every processed sample, φ tracked from the trigger). Build, zero warnings; T015 + T016 green.

---

## Group 9 — non-finite and subnormal inputs (sequential; `drop_note_behaviour_nonfinite_test.cpp`, `-fno-fast-math`)

### T017 SC-017 robustness (FR-003, FR-009, E-14; deviation 7)

**Failing tests first** (tag `[processors][profundum][drop-nonfinite]`; NaN/±Inf/subnormals built with `std::bit_cast<float>(0x7FC00000u)`,
`0x7F800000u`, `0xFF800000u`, `1u`, `0x007FFFFFu`, `0x807FFFFFu`):
- `DropEnvelope_NonFinite`: each setter given NaN / ±Inf keeps its previous value (observable through the next trigger's
  `plannedCurve()` fields equal to a reference instance's, `dropCurveBitEqual`); subnormal input → flushed to 0 then clamped:
  `setAmount(subnormal)` → d ≡ 0.0f on every sample after a trigger (E-1), `setShape`/`setRebound` → exactly 0, `setTime` → 1 ms;
  `prepare(NaN)` is a no-op (deviation 7).
- `DropPhaseLock_NonFinite`: deviation 7 — ignored triggers; `process(NaN)` returns 0.0f and the accumulator stays finite.
- `RetriggerDeclick_NonFinite`: `prepare(NaN)`, `prepare(+Inf)` no-ops.
- `MonoHandler_GlideModeNonFinite`: `setPortamentoTime(NaN/±Inf)` keeps the previous time in ConstantRate and Quantized (glide duration
  unchanged); out-of-enum `setGlideMode` ignored.
- `DropNoteBehaviour_CornerRenders`: 10 s renders at 48 kHz of `DropEnvelope` → `DropPhaseLock` at every corner A ∈ {0, 48},
  T ∈ {1, 4000}, s ∈ {0, 1}, R ∈ {0, 1}, f_res ∈ {E0, 2 kHz}, retriggered (alternating snap/bridged) every 37 ms, plus `RetriggerDeclick`
  armed every 50 ms: every d, d_c and gain sample is finite (`detail::isFinite`) and **not subnormal** (exponent bits 0 ⇒ value is ±0).

**Implement:** only fixes the tests expose in the headers (record each). Build `dsp_processors_tests`, zero warnings, green.

---

## Group 10 — CPU budget TU, measured early (alone)

### T018 SC-019 / SC-025 cost cases, then one isolated CPU run (FR-066, D-9, C-16)

- **Write** `drop_note_behaviour_perf_test.cpp` (tag `[processors][profundum][drop][.perf]`):
  - `ProfundumNoteBehaviour_CpuBudget` (SC-019): 48 kHz, host block 64, 60 s of audio, per voice: `DropEnvelope` (elastic, R = 1, A = 24,
    T = 200 ms) retriggered every 100 ms alternating snap and bridged, `DropPhaseLock` trigger + solving process, a `NoteMemory::resolve`
    per note, a `RetriggerDeclick` armed every 100 ms, a `MonoHandler` Quantized glide (24 st, 500 ms, restarted every 1 s). Median of 5 of
    wall time / audio time ≤ **0.05 %**. `INFO` ns/sample and the worst single solve (µs).
  - `DropPhaseLock_TriggerCost` (SC-025): median of 5 (trigger + the solving `process()`) at T = 4000 ms ≤ 2 × the same at T = 1 ms
    (A = 48, s = 1, R = 1, bridged).
  - `DropPhaseLock_TriggerBurst` (S5.4, gated): 64 triggers at one offset + one `process()` ≤ 1.5 × one trigger + `process()` (median of 5).
  - Recorded only: the worst-case solve of S5.4 (bridged T = 1 ms, A = 48, s = 1, R = 1) in µs, and µs per sample of an every-sample
    retrigger render (E-4).
- Build `dsp_processors_tests` (zero warnings). Precondition for the run: nothing else running. `node tools/run-cpu-tests.js dsp_processors_tests`
  → log to scratchpad; quote the four figures in `evidence/run-log/T018.txt`. A red gate: re-run alone after the machine idles; if still
  red, apply the next unused R-1 lever in the headers (this task may edit `drop_envelope.h` / `drop_phase_lock.h` for that purpose only),
  re-run T011–T017's cases, re-measure. Never relax the bar.

---

## Group 11 — test composition (sequential; `profundum_note_behaviour_test_helpers.h`, `profundum_note_behaviour_test.cpp`)

### T019 `NoteBehaviourHarness`, classification self-test, checker cross-check, pitch estimator, enum mirror (plan S8, S11.2; SC-023, SC-012(a); D-15)

**RULING 2026-10-12 (O-1): no σ_up alias static_asserts — see "Rulings applied".**

**Failing tests first** (`profundum_note_behaviour_test.cpp`, tag `[systems][profundum][note-behaviour]`):
- `ProfundumNoteBehaviour_HarnessClassification` (plan S8): each scripted sequence used later (SC-009(b), SC-010, SC-011, SC-014) carries
  its expected outcome list (one of the five `Transition`s, `Release` or `NoAction` per event); the harness's recorded list equals it
  element by element. Positive controls: a script whose last noteOff yields `Release` then a `FromSilence` after ≥ 1 s of silence;
  releasing a non-active held key yields `NoAction` (and the harness asserts `ev.frequency` = active note's frequency, `ev.retrigger`
  false); an expected list altered in one element must **fail** the comparison (assert the comparator returns false).
- `ProfundumNoteBehaviour_C1CheckerCrossCheck`: on Hold, Cosine and Hermite segments built as `AnchorSegmentSpec`, `dropCheckC1` (bounds
  from `anchorSegmentD2Bound`) and `anchorCheckC1` give identical `pass` and ratios within 1e-12; `dropHermitePeakD2 == anchorHermitePeakD2`
  on 1 000 random (g0, gp0, g1, len).
- `ProfundumNoteBehaviour_PitchEstimatorSelfTest` (D-15): `noteBehaviourZeroCrossingPitch` (half-period f_zc estimator, linear
  interpolation of crossings in double) on chirps sin(2π·Σ f_ref/fs) generated in double from the analytic `DropCurve::valueAt` trajectory,
  stored as float: four anchors × A ∈ {12, 48} × T ∈ {10, 1000} ms × {E1, C2} at 48 kHz → every f_zc within 1 cent of the analytic
  half-period mean of f_ref.
- `NoteMemory_PhasePolicyMirror` (SC-023): `static_assert` equal underlying values of `NoteMemory::PhasePolicy::{Reset, FreeRunning}`,
  `ProfundumCore::RetriggerPhase::…`, `AnchorOscillator::RetriggerPhase::…`; `static_assert(DropPhaseLock::kPitchUpdateInterval ==
  ProfundumCore::kPitchUpdateInterval && … == AnchorOscillator::kPitchUpdateInterval)`; `static_assert` that
  `DropEnvelope::kMaxUpwardSemitonesPerSample` and `DropPhaseLock::kMaxUpwardSemitonesPerSample` equal
  `DropCurve::kMaxUpwardSemitonesPerSample`; `static_assert(ProfundumCore::kMinF0Hz == 8.0f)` (the value the harness passes as minF0Hz,
  C-14).
- `ProfundumNoteBehaviour_ResetReference` (SC-012(a)): for every Reset noteOn in a 4-note script at E1/C3, 48 kHz: core and Anchor got
  the same policy on the same boundary (recorded call log), core output at the effective sample is exactly 0.0f, Anchor `phase()` is 0
  there; with A = 0, θ = goertzelPhase(core h1) − goertzelPhase(Anchor) over 4 periods after the first second is 0 within 1°.

**Implement** `profundum_note_behaviour_test_helpers.h` (namespace `Krate::DSP::ProfundumTest`; includes `profundum_core.h`,
`anchor_oscillator.h`, `adsr_envelope.h`, `mono_handler.h`, the four Phase 4 headers, `"profundum_core_test_helpers.h"`,
`"anchor_oscillator_test_helpers.h"`, `"../processors/drop_c1_checker.h"`): `NoteBehaviourHarness` per plan S8 exactly — members
(core prepared with 64 partials, excitation off; Anchor; ADSR Hard A 2 ms D 200 ms S 0.3 R 300 ms; MonoHandler `setLegato(false)`;
NoteMemory; DropEnvelope; DropPhaseLock; RetriggerDeclick), fixed-size arrays for 512-sample host blocks, `noteOn(note, vel, offset)`,
`noteOff(note, offset)`, `render(nSamples, hostBlock, Traces&)` with preallocated traces (L, R, core L/R, Anchor L/R, d, d_c, f_core,
`currentFrequency()`, Anchor `phase()`, `modeGain()`, ADSR stage, gain, event marks, outcome list); the per-event classification and
action order of S8 steps 1–7 (declick split at `samplesUntilSwitch()`, last call wins; grid phase counter (16 − count % 16) % 16, reset at
each Reset noteOn); per-sample routing of S8 (f_core = f_res·exp2(d_c/12) in float; Anchor fed f_res or, in FOLLOW with `lock.setBypass(true)`,
f_core); `setCoreH1Amplitude(core.deliveredGains()[0] * SpectralShapeRecipe::kCenterPanGain)` per block (deviation 6); out =
(core + anchor)·gain·env with the gain multiply skipped when the declick is inactive; a `VoiceReuse` entry point fed by a 2-voice
`VoiceAllocator` Hard-steal event list. Build `dsp_systems_tests`, zero warnings, green.

### T020 Matrix, POLY reuse, declick transparency, routing, RT, Anchor untouched (SC-009(b)(c), SC-011, SC-012(b)(c)(d), SC-018, SC-026)

**Failing tests first** (same TU):
- `ProfundumNoteBehaviour_Matrix` (SC-009(b)): runs only after the classification self-test's scripts pass (call the same comparator first,
  `REQUIRE`). One script per mode × Retrigger (8 scripts) covering FromSilence, Overlap, Tail, ReturnToHeld, at E1 48 kHz, host block 37,
  Anchor LOCK level 1, `kDefaultControls`. Per transition, per the resolved `Actions`: **Anchor phase** — FreeRunning: `phase()` increments
  equal the expected per-sample increment within 1e-9 cycles at every sample across the event; Reset: `phase() == 0` at the effective sample.
  **Core** — Reset: output exactly 0.0f at the effective sample, and with A = 0 the h1 `goertzelPhase` over the next 4 periods equals that of a
  fresh prepared instance given the same Reset noteOn within 1°; FreeRunning same-note with A = 0: output memcmp-equal to an uninterrupted
  render; FreeRunning different note: h1 phase advance over the 4 periods before/after equals the analytic Σ f/fs within 1°. **ADSR** —
  `getStage() == Attack` right after the event iff envelopeRestart. **Drop** — `isActive()` and d at the effective sample = A iff
  dropTrigger (snap) or the bridge start value (bridged). **Re-arm** — `modeGain()` restarts iff anchorRearm, otherwise bit-identical to the
  uninterrupted trace.
- `ProfundumNoteBehaviour_PolyVoiceReuse` (SC-009(c)): 2-voice `VoiceAllocator` (`setVoiceCount(2)`, Hard steal); a third note steals an
  audible voice → `VoiceReuse`; the per-voice actions on both core and Anchor match the resolver for each mode under Reset and Continue.
- `ProfundumNoteBehaviour_DeclickTransparency` (SC-011): onset latency recorded per cell (≤ ⌈1.5·fs/1000⌉ + 1); core and Anchor multiplied by
  the bit-identical gain sample (trace check: out == (coreL + anchorL)·gain·env bitwise); both exactly 0.0f at the switch sample; the dip
  passes `dropCheckC1` with the Cosine bound from `anchorSegmentD2Bound`; a new note during the dip re-targets without a gain step (gain
  trace memcmp-equal to a single arm); every non-declick transition is memcmp-identical with and without the `RetriggerDeclick` in the
  chain; Anchor L == R bitwise in every cell.
- `ProfundumNoteBehaviour_Routing` (SC-012(b)(c)(d)): (b) LOCK under a Drop (A ∈ {12, 48}, T ∈ {60, 200} ms, E1 and C3, 48 kHz): Anchor
  instantaneous frequency from `phase()` increments equals f_res within 1 cent at every sample; `currentFrequency()` at every pitch update
  equals clamp(f_core at that sample, 8, cap) within 0.1 cent; `INFO` the per-sample staircase error (≈ 27 cents at 48 st / 60 ms). (c) FOLLOW
  (r = 1): the Anchor's accumulated `phase()` advance equals the integral of the trajectory it is fed (core post-Drop f0 on the Phase 3
  cadence) within 1e-6 cycles at every sample; record Σ(f_Anchor − `currentFrequency()`)/fs. (d) Glide in each of the three modes, no Drop:
  |Δθ(1 s)| ≤ 1° after the glide settles (Phase 3 SC-003(b) bound); the 60 s window, |Δθ(60 s) − Δθ(1 s)| ≤ 0.5°, is a separate
  `[long]` case `ProfundumNoteBehaviour_GlideLock60s` in this TU (E1 and C3, 48 kHz, the three modes, 200 ms ±7 st glides every 5 s).
- `ProfundumNoteBehaviour_NoAllocation` (SC-018): one `AllocationScope` over harness event + render calls (traces preallocated outside),
  `MonoHandler::setGlideMode` for every ordered pair mid-glide, every `NoteMemory` setter and `resolve`, every `DropEnvelope` setter,
  `trigger(true/false)`, `process`/`processBlock`, `DropPhaseLock::trigger` + solving `process`, `setBypass`, `RetriggerDeclick::arm`/
  `process`/`processBlock`; live count read before the scope closes → 0; positive control ≥ 1.
- `ProfundumNoteBehaviour_AnchorUntouched` (SC-026): LOCK, PUNCH, GHOST × a Drop script (A = 48, T = 60 ms, elastic R = 1, bridged retrigger):
  Anchor `phase()` trace and Anchor output memcmp-equal between lock enabled and `setBypass(true)`.
- `ProfundumNoteBehaviour_NoMutableStatics`: `anchorFindMutableStatics` on the source text of the five new headers (read via
  `KRATE_DSP_TESTS_DIR + "/../include/krate/dsp/…"`) → empty.

**Implement:** only harness fixes the tests expose (helper header is this group's). Build, zero warnings, green; measure the TU's wall clock
and tag `[long]` (with a `…Smoke` sibling) any case > 15 s.

---

## Group 12 — post-Drop landing and FOLLOW grids (sequential; `profundum_note_behaviour_spectral_test.cpp`)

### T021 SC-013 smoke + R-2 probe, then the full grid; SC-026 FOLLOW grid (FR-053–FR-059; C-4, C-5, C-6; deviation 4)

**RULING 2026-10-12 (O-1, O-4): every cell gated, no infeasible set, |γ| recorded per Time — see "Rulings applied".**

**Failing tests first** (tag `[systems][profundum][note-behaviour]`):
- `ProfundumNoteBehaviour_PostDropLandingSmoke` (per push): E1, 48 kHz, A ∈ {12, 48}, T ∈ {60, 200} ms, four anchors, R ∈ {0, 1}, LOCK, block
  37. Per cell: (a) θ = wrap(goertzelPhase(core L, 4 periods of f_res from the landing sample ⌈D_s⌉) − goertzelPhase(Anchor L, same window))
  |θ| ≤ 1°, and |θ(10 s) − θ(landing)| ≤ 0.5° (Phase 3 SC-003 drift bar) over the following 10 s; (b) d_c exactly 0.0f from ⌈D_s⌉;
  `currentFrequency()` == clamp(f_res, 8, cap) at every pitch update after landing; Anchor `phase()` and output memcmp-equal to a bypass
  render; (c) measured peak |d_c − d| ≤ 1.05·Γ + 1 ulp(A), Γ = min(γ_an, γ_cap) + |c₀| + |c₀′|·L·4/27 (carry terms from the measured d_c − d
  trace at the retrigger; 0 on snaps), where γ_an is computed **in the test** per C-5 from r = n* − Φ_ref(0) — Φ_ref(0) the test's own double
  staircase sum over the **bypass** render's `currentFrequency()` trace, n* the nearest-feasible integer chosen by the C-4 rule on Φ_ref(0) —
  f_res, D and `DropCurve::valueAt` (bisection of the lower bound L(g) on [0, 48], 4 000 midpoints, β ∈ {¼, ½, ¾}); γ_cap =
  min(48, 0.15·D_s/3.0792014356780038). The test never reads `correctionGamma()`, `correctionPeakBound()` or `predictedExcessCycles()` for
  this gate. `dropCheckC1` on d_c passes (S11.2 bounds). `INFO` |γ|/Γ and `solveResidualCycles()`.
  (d) bridged retriggers at mid-fall, mid-rebound, mid-elastic (E1, A = 24, T = 200 ms) land at |θ| ≤ 1°; a Reset noteOn zeroes the
  accumulator (θ = 0 at the effective sample within 1°); **interrupted-Drop FreeRunning snap**: A = 48, T = 4000 ms, ADSR release 300 ms
  idles while the Drop is in flight, then an OscillatorContinuous / Continue noteOn with a second Drop (A = 12, T = 200 ms) at E1 and C3:
  |θ| ≤ 1° at the second landing and `accumulatedExcessCycles()` right before the second trigger equals the mirror value (not zeroed);
  a glide in flight during a Drop: θ recorded, not gated (E-15).
  **Negative control:** with `setBypass(true)` in LOCK, at least one cell has |θ| > 90°.
  **R-2 probe:** `INFO` the rendered θ against `drop-model.js`'s prediction for E1/C3 cells (≤ 0.22°); a gap > 0.3° is recorded under
  "Defects found" with the cell (plan R-2), not hidden.
- `ProfundumNoteBehaviour_PostDropLanding_E0`, `_E1`, `_E2`, `_C3` (`[long]`): the full SC-013 grid for that note: A ∈ {6, 12, 24, 48} ×
  T ∈ {10, 60, 200, 1000} ms × four anchors × R ∈ {0, 1} × three rates, LOCK, checks (a)–(c) as the smoke; infeasible cells (C-6): the set of
  cells with `landingFeasible() == false` equals the subset of `evidence/sc013-infeasible.json` for that note exactly; θ recorded, not gated,
  on those cells. Write a per-cell CSV `evidence/sc013-<note>.csv` (note, fs, A, T, s, R, θ, drift, |γ|/Γ, feasible, residual).
- `ProfundumNoteBehaviour_PostDropLanding_PunchGhostE1` (`[long]`): PUNCH and GHOST on the same grid at E1.
- `ProfundumNoteBehaviour_FollowAfterDrop` (`[long]`, SC-026) + `…Smoke` (per push: A ∈ {12, 48}, T = 200 ms, four anchors, R = 1):
  FOLLOW r = 1 over the SC-013 grid at E1, 48 kHz: d_c memcmp-equal to d; after t_s, |Δθ(1 s)| ≤ 1° and |Δθ(60 s) − Δθ(1 s)| ≤ 0.5°
  (Phase 3 SC-003; the 60 s window only in the `[long]` case, the smoke uses 10 s).

**Implement:** only fixes the tests expose; component defects go to "Defects found" for T023 if they are in another group's file.
Build; run the smoke per push; run the `[long]` shards **concurrently** as five processes (one per case name), each to its own log, and
record each wall clock (plan estimate ≈ 2 min per shard).

---

## Group 13 — clicks, audibility, Nyquist, use cases (sequential; `profundum_note_behaviour_spectral_test.cpp`)

### T022 SC-010, SC-014, SC-015, SC-016, SC-021 (FR-060–FR-062, FR-064, FR-068; C-9, C-10)

**RULING 2026-10-12 (O-1): SC-015 is the rendered aliased-power measurement (≤ −96 dBFS), not the formula — see "Rulings applied".**

**Failing tests first:**
- `ProfundumNoteBehaviour_RetriggerClickFree` (SC-010(a)(b)): every SC-009(b)(c) script × E0, E1, E2, C3 × `kSawAnchor`, `kGrowl` × Anchor
  level 1 in Lock/Follow/Punch/Ghost × three rates × host blocks {1, 37, 512}: `ClickDetector` (default `ClickDetectorConfig`, its
  `sampleRate` set to fs) reports zero clicks; at every transition max |y[n] − y[n−1]| ≤ 1.1·(G_max·D_max + S_max·P_max) with S_max =
  π/(2L_d) (C-15) for declicked onsets and the Drop's analytic peak slope term at the effective sample, all terms analytic.
- `ProfundumNoteBehaviour_GlideClickFree` (SC-010(c)): Quantized Δ ∈ {±1, ±12, ±24} at T ∈ {10, 500} ms; ConstantRate Δ = ±24 at T = 10 ms
  per octave; mid-glide switches between every ordered pair of modes → zero clicks and the sample-delta bound at every Quantized step and
  switch with S_max from the one-semitone frequency step on a phase-continuous oscillator.
- `ProfundumNoteBehaviour_DeclickPositiveControl`: a Reset noteOn on a sounding voice without the declick is flagged by (a) and (b); a
  declick scheduled one block late (not split) is flagged by (b); a Quantized step applied with a phase reset is flagged by (c).
- `ProfundumNoteBehaviour_MonoIdentity` (SC-016): in every SC-010 render, Anchor L == R and core L == R (pan offsets identity) bitwise.
  Measure the SC-010 grid's wall clock: if > 15 s, keep the grid `[long]` sharded by note (`…RetriggerClickFree_E0` … `_C3`) with a per-push
  `…RetriggerClickFreeSmoke` (E1, 48 kHz, `kSawAnchor`, LOCK, blocks {1, 512}, all 8 scripts) — MonoIdentity rides along in each.
- `ProfundumNoteBehaviour_Audibility` (SC-014; after `…PitchEstimatorSelfTest` passes): core at `kSineAnchor`, E1 and C2, 48 kHz; descriptors
  on d_core = 12·log2(`currentFrequency()`/f_res) (block 1) **and** on f_zc. Amount 0 → 48 (T = 1000, s = 1/3): start pitch moves
  monotonically by ≥ 47 st (f_zc on the first half-period). Time 10 → 1000 ms (A = 24, s = 0): t_s monotone, ratio ≥ 10. Shape 0 → 2/3
  (A = 24, T = 1000, R = 0): α moves ≥ 0.6. Shape 2/3 → 1: ε moves ≥ 0.1·A. Rebound 0 → 1 (A = 24, T = 1000, s = 1/3): ρ moves ≥ 0.2·A.
  f_zc agrees with d_core's half-period mean within 10 cents. **Retrigger** (AlwaysRetrigger, A = 0, E1, `kSawAnchor`, LOCK level 1, the
  spec script): at every audible onset D = 10·log10(Σ(y_R − y_C)²/Σ y_C²) ≥ −10 dB over [arm sample, effective + one period], and the Reset
  render's minimum 0.25 ms RMS in that window ≥ 20 dB below the Continue render's. **Note Memory** (Retrigger = Reset; script n1 from
  silence, n2 over n1, release n2, release n1 then n3 in its tail, release n3, ≥ 1 s silence, n4; sustain 0.3): every mode pair differs in
  ≥ 1 onset window by ≥ 3 dB RMS-envelope difference or D ≥ −10 dB; under Continue the collapsed pairs are memcmp-identical. **Glide** (C2 → C4,
  Δ = +24, T = 500 ms, no Drop): ConstantTime vs ConstantRate ≥ 500 cents apart at t = T/2; Quantized vs ConstantRate ≥ 500 cents at T/2;
  Quantized vs ConstantTime max deviation ≥ 50 cents; d_core shows exactly 24 one-semitone plateaus.
- `ProfundumNoteBehaviour_TrajectoryNyquist` (SC-015): 44.1/48/96 kHz × E0–E2, C3 × `kSawAnchor`, `kGrowl`, block 1: Drop A = 48, T = 1 ms,
  every anchor family, R = 1, without and with the correction; Quantized and ConstantRate up-glides of 24 st at the smallest positive T;
  bridged retriggers including upward bridges at T = 1 ms; upward rebound recovery. At every sample: (highest k with
  `deliveredGains()[k] != 0` + 1) × `currentFrequency()` < fs/2. Write trajectory + margin CSV to `evidence/sc015-<fs>-<note>-<recipe>.csv`.
  A violation is a Phase 4 defect (D-14): record under "Defects found" with the CSV row; do not change the core.
- `ProfundumNoteBehaviour_UseCaseRenders` (SC-021): the four plan S11.4 presets (808 attack A 12 T 120 s 0 R 0 LOCK; cinematic impact
  36 / 900 / 2/3 / 0.3 GHOST; techno pluck 7 / 25 / 1/3 / 0.5 PUNCH; kick/bass hybrid 48 / 45 / 1 / 1.0 LOCK) × {E1, C2}, 48 kHz, core +
  Anchor + Drop + ADSR: write stereo float WAV + descriptor CSV to `evidence/usecase-<preset>-<note>.{wav,csv}` (TU-local writer in an
  anonymous namespace, e.g. `noteBehaviourWriteWav`); peak finite and ≤ the core + Anchor ceiling (`ProfundumCore` output ceiling +
  `AnchorOscillator::kOutputCeiling`); zero clicks (SC-010 method); every preset pair differs in start pitch ≥ 6 st, or t_s ≥ 2×, or α ≥ 0.1,
  or ρ ≥ 0.05·max(A) on d_core.

**Implement:** only fixes the tests expose. Build; run per-push cases; run `[long]` shards concurrently; record wall clocks.

---

## Group 14 — defect fix-up (sequential, conditional)

### T023 Resolve every entry under "Defects found"

- For each entry: write or reuse the failing test, fix in the owning file (respect append-only for `mono_handler.h`; never edit
  `profundum_core.h` / `anchor_oscillator.h` — a D-14 core fallback needs a clarify ruling: stop and report), rebuild, re-run every case
  of the affected groups. A bar that cannot be met goes to the main loop with its measurement. If no entry exists, record "none".

**Epoch 3 (main loop, 2026-10-13, after the FR-070 bank fix and the "Defects found" item 8 harness fixes) — this block is the
authoritative instruction for the retry; the three lines above are epoch 1.**

- **You MAY and MUST build and run tests.** The generic "do not build" executor rule does not apply to this task: it is the
  fix-up, and every open item below needs a measurement to diagnose. Build with
  `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_systems_tests`
  (and `dsp_processors_tests` for item 6), run with the per-push filter
  `"[note-behaviour]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]"` to a log under F:/tmp/p4fix/, tail it; never
  re-run a suite only to re-read output; never `find /c` or any disk-wide walk.
- **Files you may edit:** `dsp/tests/unit/systems/profundum_note_behaviour_test.cpp`,
  `dsp/tests/unit/systems/profundum_note_behaviour_spectral_test.cpp`, `dsp/tests/unit/systems/profundum_note_behaviour_test_helpers.h`,
  `dsp/tests/unit/processors/drop_note_behaviour_perf_test.cpp`, `dsp/include/krate/dsp/core/drop_curve.h`,
  `dsp/include/krate/dsp/processors/drop_envelope.h`, `dsp/include/krate/dsp/processors/drop_phase_lock.h`,
  `dsp/include/krate/dsp/processors/note_memory.h`, `dsp/include/krate/dsp/primitives/retrigger_declick.h`,
  `dsp/include/krate/dsp/processors/mono_handler.h` (append-only against 3d8ca19f: only the lines Phase 4 added may change;
  `node specs/profundum-phase4-drop-note-behaviour/check-append-only.js` must pass), this file (record under "Defects found" and
  in `evidence/run-log/T023.txt`). **Never edit** `harmonic_oscillator_bank.h`, `profundum_core.h`, `anchor_oscillator.h`
  (FR-070 is in; the core alone is proven clean, `evidence/growl-probe.md`). No bar changes, no test deletions, zero warnings.
- **Items 1–3 and 5** ("Defects found", 2026-10-13 triage): item 8 is already in the tree — build on it. Re-measure first
  (the tree changed since the triage: FR-070 and item 8), then diagnose each remaining red exactly as its triage entry says
  (item 1: dump `tr.fCore` / `tr.coreFreq` and the per-partial spectrum of a red kGrowl cell; item 2: the click detector's
  event list; item 3: Anchor routing receives f_res never the corrected f, FR-049/FR-058; item 5 after 1–3). Fix in the owning
  file. Any reading that contradicts a ratified bar or design: STOP AND SURFACE with the measurement.
- **Item 6, SC-019 (0.0709 / 0.0843 % against 0.05 %, T028.txt section 4) — main-loop decision 2026-10-13: options (a) and (b)
  are authorised, (c) is not needed.** (a) Profile the test Voice in `drop_note_behaviour_perf_test.cpp` one component at a time
  against the T005 bench figures (envelope ≈ 2.9, lock + mirror ≈ 1.9, quantized glide ≈ 1.2, declick ≈ 0.07 ns/sample) and
  record the table in T023.txt. The timed region counts exactly what SC-019 lists (Drop + lock + NoteMemory resolve + declick +
  quantized glide); any harness bookkeeping inside the timed region is a measurement defect — move it out, do not change the
  list. (b) Reduce the Phase 4 per-sample paths in the component headers above (the triage suspects: per-sample `std::exp2` in
  the mirror where one per 16 samples is the plan, `DropCurve::valueAt` piece search per sample instead of the S3.4
  recurrence, double → float conversions, non-inlined layering) with every existing test unchanged and green: the outputs are
  pinned by the Phase 4 suites, so an optimisation that changes a reading is a defect, not a lever. Measure alone and pinned:
  `node tools/run-cpu-tests.js dsp_processors_tests` with nothing else running (this task runs sequentially; start no
  background work). Record the median of 5 before and after. The 0.05 % bar is never relaxed (R-1, C-16); if (a)+(b) cannot
  reach it, STOP AND SURFACE with the per-component table.
- **Item 7:** already corrected in T026's filter; verify the filter string once and note it.
- Return done only when the per-push run of the Phase 4 TUs is green and item 6 is either green alone-and-pinned or surfaced
  with its table. Record every reading in `evidence/run-log/T023.txt` and under "Defects found" (items 1–8 resolved, with
  the fix and the number).

**Epoch 6 (main loop, 2026-10-13) — the four epoch-5 decisions are ruled; apply them (spec Clarifications "Session 2026-10-13 —
T023 epoch 5 readings") on top of the epoch-5 tree. Same file list and build rights as epoch 3.**
- (A) SC-026 / spectral :502/:503 / `AnchorUntouched`: keep the memcmp on the Anchor's pitch, `phase()` trace and mode gain;
  replace the output memcmp by a recorded per-cell max |diff| re the Anchor's peak, gated at ≤ −60 dB (expect ≈ −70 dB).
- (B) SC-010(a) and SC-021 (`RetriggerClickFree`, `GlideClickFree`, `UseCaseRenders`): count a detection only if the
  uninterrupted reference render of the cell (phase-aligned where a Reset re-seeds) has none within ±N samples; N ≤ 1/8 of the
  sounding period, chosen and recorded by you in the test with the method; bar stays zero. The three positive controls in
  `DeclickPositiveControl` MUST still be flagged by (a) — if your N or alignment lets one through, tighten, do not relax.
- (B′) `t22DeltaBound`: D from the core's delivered gains over the window (the actual trajectory), P and the 1.1 factor as they
  are; positive controls still flagged.
- (C) `Audibility` Retrigger: the Continue render's 0.25 ms RMS in the same window as the Reset render's minimum (same centre
  sample); bar ≥ 20 dB unchanged.
- (D) SC-019: implement the S5.6 per-hold mirror accumulation ((f_h − f_res) once per 16-sample hold, in double) in
  `drop_phase_lock.h`; SC-024 (1e-6 relative) and the SC-013 shards judge. Do NOT add lazy bracket ends. Re-run the official
  lane `node tools/run-cpu-tests.js dsp_processors_tests` alone, once, after the machine has idled ≥ 3 min; record the figure.
  If it is still above 0.05 %, STOP AND SURFACE with the per-component table — no further lever is ruled.
- Finish with the per-push run of the Phase 4 TUs green, zero warnings, check-append-only PASS, T023.txt and "Defects found"
  updated with the epoch-6 readings.

**Epoch 7 (user rulings, 2026-10-13) — apply spec Clarifications "Session 2026-10-13 — T023 epoch 6 readings" on top of the
epoch-6 tree. Same file list and build rights as epoch 3. (B′) stays as it is.**
- (A) SC-026 / spectral :502/:503 / `AnchorUntouched`: replace the −60 dB gate by the per-sample assertion
  output_on == output_off × (a1_on / a1_off) within float rounding (≤ −120 dB re the Anchor's peak; a1 from the level feed the
  harness already records for both runs). Keep the memcmp on pitch, phase increment, `phase()` and mode gain. Record the raw
  per-cell max |diff| re peak in the CSV / log (worst expected −8.63 dB at E0 A 48 T 10 ms R 1) — recorded, not gated.
- (B) SC-010(a) and SC-021: remove the zero bar on the differential count; keep `t22CountClicks` and print / record its count
  per cell; (b) and (c)'s bound are the gates (already green); the positive controls are proven by (b) and (c) only. Update
  the case comments and the T023.txt table so the compliance reader sees which detector gates what.
- (C) `Audibility` Retrigger: reference = the Continue render's RMS over one period of the new note centred on the effective
  sample; the Reset render's minimum 0.25 ms RMS in the onset window must be ≥ 20 dB below it. Record both numbers per onset.
- (D) SC-019: apply reading-changing levers in `drop_envelope.h`, `drop_phase_lock.h`, `drop_curve.h`, the Phase 4 glide
  code in `mono_handler.h` (append-only rule still applies) — float arithmetic where double is not needed for the bars, the
  correction-curve recurrence instead of a piece search, hoisted per-hold work — until the pinned profile reads ≤ 9.5 ns/sample
  with margin; after each lever re-run the SC-024 cases, the Mirror test and the SC-013 shards: a lever that breaks any bar is
  reverted, not tuned. Then the protocol: three `node tools/run-cpu-tests.js dsp_processors_tests` runs, each alone after
  ≥ 3 min idle (nothing else running: this task is sequential, start no background work), all three figures recorded, the
  median is the verdict against 0.05 %. If the median is still above the bar after the levers, STOP AND SURFACE with the
  per-component table and the three figures.
- Finish with the per-push run of the Phase 4 TUs green (22 cases), zero warnings, check-append-only PASS, T023.txt and
  "Defects found" updated with the epoch-7 readings, then return done.

---

## Group 15 — roadmap handoff (sequential; shared doc)

### T024 Write the S14 handoff clauses into `specs/Profundum-roadmap.md` (FR-027, SC-027; C-9, C-10, O-2)

**RULING 2026-10-12 (O-1): C-9 / C-10 / O-2 collapse to one recorded sentence; no `landingFeasible()` line — see "Rulings applied".**

- In Phase 11's section (`### Phase 11:` heading, `:589`), append the FR-027 clause verbatim from plan S14 (`plan.md:1141–1143`).
- In Phase 9's section (`### Phase 9:` heading, `:509`), append the three C-9/C-10 bullets of plan S14 (`plan.md:1144–1148`), plus the
  measured infeasible-cell summary from `evidence/sc013-infeasible.json` (count per note and T).
- Locate sections by heading, never by line number; change nothing else. Verify with `grep -n` that each clause occurs exactly once.

---

## Group 16 — integration (sequential; last)

### T025 CMake registration audit (single task)

- `grep -c` each of the nine Phase 4 TU paths in `dsp/tests/CMakeLists.txt`: exactly 1 each, except
  `unit/processors/drop_note_behaviour_nonfinite_test.cpp` = 2 (list + `-fno-fast-math` block); the perf TU is not in any
  `set_source_files_properties` block. `dsp/CMakeLists.txt` lists each of the five new headers exactly once, in the list of its layer.
  No other CMake line differs from `3d8ca19f` (`git diff 3d8ca19f -- dsp/CMakeLists.txt dsp/tests/CMakeLists.txt` shows only additions).

### T026 Full-suite run (FR-063, SC-020; plan S12.2)

- One rebuild: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests ruinae_tests seraphis_tests vorago_tests innexus_tests -j` → zero warnings; record the wall clock.
- Per-push lanes **concurrently**, each to its own log: the five DSP exes with `"~[.perf]~[long]"` (filter form of deviation 10), plus
  `ruinae_tests`, `seraphis_tests`, `vorago_tests`, `innexus_tests`. `[long]` lane concurrently: the Phase 4 shards by name (T021, T022).
  All green; quote each exe's last lines in `evidence/run-log/T026.txt`. Any red, Phase 4 or not, is owned here: diagnose, fix (T023 rules),
  re-run only the affected exe.
- `node specs/profundum-phase4-drop-note-behaviour/check-append-only.js` → PASS (quote it).
- **RE-EXECUTION 2026-10-13 (epoch 2):** per-push filter is `"~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.perf]"`. Shard
  the systems per-push lane: `"[note-behaviour]~[long]"` and `"~[note-behaviour]~[long]"` as two processes. `check-append-only.js`
  is expected to report the FR-070 additions as public-surface additions on `harmonic_oscillator_bank.h` only if it scans that
  file (it scans `mono_handler.h`): quote it as is. Epoch-1 results and their triage are under "Defects found".

### T027 Portability, Apple globals, clang-tidy (FR-067, SC-022)

- `node tools/check-portability.js` and `node tools/lint-apple-globals.js` on the new and changed TUs/headers → 0 findings (quote output).
- `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` → zero warnings in Phase 4 files; fix any finding.
- After `check-portability.js` (it boots WSL), run `wsl --shutdown`.

### T028 CPU lane alone (FR-066, SC-019, SC-025; plan S12.2)

- Precondition: everything else finished; machine idle. `node tools/run-cpu-tests.js dsp_processors_tests` → `ProfundumNoteBehaviour_CpuBudget`
  ≤ 0.05 %, `DropPhaseLock_TriggerCost` ≤ 2×, `DropPhaseLock_TriggerBurst` ≤ 1.5×; quote the figures in `evidence/run-log/T028.txt`.
  A red reading: re-run that suite alone after the machine idles; only then treat it as a defect (T018's lever rule).
- **RE-EXECUTION 2026-10-13 (epoch 2):** the workflow's executors may not run suites; the main loop runs this lane at close (as
  Phase 3 T025) after the fixer has resolved "Defects found" item 6 and the tree is green. Executor: return done with notes
  "deferred to the main loop at close; epoch-1 figures 0.0843 / 0.0709 % are the open SC-019 defect (Defects found 6)".

---

**T029 (main loop, 2026-10-13) — FR-070 / SC-028 bank continuity. Outcome: PASS.**
- Probe `evidence/growl-probe.cpp` (output in `evidence/growl-probe.md`): core alone clean on every SC-015 trajectory (≤ −125 dBFS);
  partial amplitudes re-rolled after any Drop ≥ 12 st (energy +4.0 dB, worst partial +10.7 dB at C3 kGrowl A 48 T 60 ms).
- `harmonic_oscillator_bank.h`: `setContinuousFrequencyUpdates` / `continuousFrequencyUpdates` (public, append-only), private
  `reconcileContinuity` + `continuityEps_` (seeded at phase init, cleared in reset, kept across reset for the setting), called at the
  top of `processStereo` and `processStereoBlock` when enabled; `applyExternalFrequencyMultipliers` unchanged in effect. A first
  attempt rewrote the state inside `applyExternalFrequencyMultipliers` and failed on the core (errors growing with T: the core changes the
  base after applying multipliers, so the coefficient seen there was not final) — hence the render-time reconcile.
- `profundum_core.h`: one line in `prepare` enables it.
- Tests: `HarmonicOscillatorBank_ContinuousFrequencyUpdates` 'worst partial -0.0001 dB (bar 0.05); default path worst -5.82 dB
  (control > 1)'; `ProfundumCore_SpectrumPreservedAcrossDrop` 'SC-028 spectrum across Drop: 162 cells, worst partial -0.2460 dB (bar
  0.1 within 40 dB of h1, 0.5 dB below) at mid grid midi 48 fs 44100 A 24 T 1 ms, k63, worst |energy| 0.0007 dB (bar 0.05)', 'All
  tests passed (486 assertions in 1 test case)'. Registered in `dsp/tests/CMakeLists.txt` (systems list).
- Regression run 1 (bank consumers rebuilt, default off): processors, innexus, membrum, seraphis, vorago all green; Phase 1–3
  systems cases: 2 red — Phase 1 `ProfundumCore_NoZipperPitchBend` (−46.95 dB, 34 clicks; −59.8 dB / 0 with the opt-in off) and
  Phase 2 `ProfundumCore_ExcitationZeroMixBitIdentical` (differs from sample 17 on the vibrato fixture). Diagnosis (probes
  `static_probe` / `ref_probe` / `conv2_probe`, scratchpad): static notes bit-identical; the bank-level rewrite exact to 4e-7; the
  Phase 1 reference synthesis runs a keep-state MCF in double, and the first rewrite kept the state amplitude A fixed where the
  bank's convention is A ∝ 1/cos(ω/2) (unit-circle seed, antiAliasGain cos(ω/2)). Fix: r = cos(ω₁/2)/cos(ω₂/2), s′ = r·s,
  c′ = c + s·ε₁/2 − r·s·ε₂/2; the Phase 1 reference applies the same law in double; the Phase 2 null is re-scoped (static →
  pinned Phase 1 core, motion → live default core). Re-measured: bend **−67.60 dB, 0 clicks**; null 'All tests passed (1036
  assertions)'; SC-028 and the bank test unchanged. Spec records: Phase 4 Clarifications (FR-070 amplitude convention), Phase 1
  Clarifications 2026-10-13, Phase 2 Clarifications 2026-10-13.
- Regression run 2 (2026-10-13 11:05–11:12, after the convention fix; innexus / membrum / seraphis / vorago / processors / systems rebuilt, six lanes concurrent): processors 'All tests passed (10802834 assertions in 3409 test cases)'; Phase 1–3 systems cases `"[profundum]~[note-behaviour]~[long]~[.perf]"` 'All tests passed (4064965 assertions in 147 test cases)'; innexus (1111855 / 565), membrum (119419 / 630), seraphis (444660 / 109), vorago (4060395 / 119) all passed — bit-identical for the default-off consumers. Logs F:/tmp/bankfix2_*.log.
- Close (main loop, 2026-10-13 17:15 → 2026-10-14 09:07): the Group 16 executors were cut off by the 10-minute executor limit
  with their lanes still running; the lanes completed on their own. T026 epoch 3: 25 of 26 lanes green (every per-push suite,
  both systems shards' Phase 4 cases, all 16 [long] shards); the one red, `SeraphisEngine_VoiceStealIsClickless`'s wall-clock
  block-time clause (26963 µs vs 10667 under 26 concurrent lanes + clang-tidy), is green alone in 1 s ("All tests passed (20
  assertions in 1 test case)") and the suite passed in full in the Group 14 and 15 gates — machine-load artefact (T026.txt
  "EPOCH 3 COMPLETION"). T027: check-portability all clear (12 TUs), lint-apple-globals OK (24 files), wsl --shutdown; the full
  `-Target dsp` tidy sweep hung 15 h on `filter_step_sequencer_tests.cpp` (not a Phase 4 file; killed), so the Phase 4 TUs were
  tidied by file list — see T027.txt. T028 epoch 3 alone and pinned: **SC-019 0.0286 %** (bar 0.05), SC-025 ratio 1.058, burst
  1.169; Phase 1–3 Profundum budgets re-confirmed (worst Anchor cell 0.1293 % of 0.3) — T028.txt "EPOCH 3".

## Defects found

*(Sequential tasks append here: case, output, diagnosis, owning file. T023 resolves.)*

**T026 / T028 epoch 1 (2026-10-13) — triage by the main loop; T023 (epoch 2) and the Group 14 / 16 fixer resolve these.** All
first-run reds of the T018–T022 cases (the per-group gates never ran them: workflow defect, fixed). Rules: never weaken a bar; the
bank and `profundum_core.h` are NOT to be edited further (FR-070 is in; the core alone is proven clean on every SC-015 trajectory,
`evidence/growl-probe.md`); fix the composition, the harness or the Phase 4 components.
1. **SC-015 `ProfundumNoteBehaviour_TrajectoryNyquist` (25 cells, kGrowl only, e.g. E1 44.1 kHz bridge-midfall −29.98 dBFS, bypass
   −57.4; kSawAnchor −130 on the same cells).** The core alone reads −125 … −136 dBFS on these trajectories (probe). Suspects, in
   order: the harness's `f0PerSample` buffer beyond `head` (the second `render(need − head, 1, tr)` call: is the trajectory pointer
   valid and the Drop/lock offset exactly 0 there?); `tr.coreL` being written by something other than the core (Anchor sum,
   declick gain) in the kGrowl cells; the DropPhaseLock correction leaving a non-zero d_c after landing (SC-013(b) also red).
   Verify by dumping `tr.fCore` / `tr.coreFreq` over the frame and the per-partial spectrum of the red cell.
2. **SC-010 `RetriggerClickFree*` / `GlideClickFree*` / `UseCaseRenders`: thousands of clicks "nearest event at 0".** The click
   detector's event list is almost certainly empty or not aligned (every detection reports event 0 thousands of samples away):
   harness defect first (`PT::NoteBehaviourTraces` event bookkeeping / the bound lookup), then re-measure; any remaining click is a
   component defect (declick scheduling, bridge).
3. **SC-013 `PostDropLanding*` (θ > 1°, drift > 0.5°, Anchor memcmp vs bypass) and `_Routing` (|dθ| 1.24°), `FollowAfterDrop`
   (constant −1.6 … −1.8° offset, drift fine), `SC013-R2-DEFECT` gaps ≈ 0.54° vs the staircase prediction.** Re-measure AFTER
   FR-070 (the state rewrite also removes the per-update phase error the predictor could not model). Then: the Anchor memcmp
   (:482/:483, note_behaviour_test.cpp:1437/:1438) says the Anchor's rendered output differs with the correction on vs off —
   the Anchor must receive f_res, never the corrected f (FR-049/FR-058): check the harness routing; a remaining constant offset in
   FOLLOW is a routing-order (grid phase) question, plan S8 / C-14.
4. **`_NoAllocation`: 95 contract violations with 0 allocations.** `contractViolations()` counts frequency / retrigger mismatches
   between the harness's expectation and the components; with 0 allocations the RT bar itself passes — fix the harness's
   expectation (or the component's contract) and keep the allocation assertion.
5. **`_DeclickPositiveControl` (c) ratio 0.98 (needs > 1), `_Audibility` (`tsZc.front() == 0` at note 28; Reset vs Continue RMS gap).**
   Control (c) is a test-design issue (the control must demonstrably fail); the audibility reds re-measure after items 1–3.
6. **SC-019 `ProfundumNoteBehaviour_CpuBudget` 0.0709 / 0.0843 % (bar 0.05 %), alone and pinned twice.** The composed per-sample
   cost is 14.8–17.6 ns against the T005 kernel bench 5.7 ns (envelope ≈ 2.9, lock + mirror ≈ 1.9, quantized glide ≈ 1.2, declick
   ≈ 0.07). Profile the test Voice one component at a time against those targets; suspects: per-sample `std::exp2` in the
   mirror (one per 16 samples is the plan), `DropCurve::valueAt` piece search per sample instead of the S3.4 recurrence, double →
   float conversions and non-inlined layering in the per-sample path, the harness's own per-sample bookkeeping counted inside the
   timed region. The budget is never relaxed; FR-070's reconcile adds ≤ 64 compares per 16 samples to the core (not Phase 4 cost).
7. **ruinae_tests `RuinaeVoice: SC-002 ... CPU < 3%` 3.23 % under the 25-process load, green with the CI filter.** Not a defect; T026's
   per-push filter must add `~[performance]` (deviation 10 corrected).

8. **Harness fixes already in the tree (2026-10-13 11:38–11:49, a workflow fixer dispatched out of order at Group 1 after the
   gate-target fix invalidated every gate's cache; stopped by the main loop, edits kept after review — no bar changed).**
   (a) item 4: `_NoAllocation` traces were allocated for 2.5 s against 3.5 s of renders (`profundum_note_behaviour_test.cpp`); each
   overflowing sub-block counted one contract violation. Now 3.5 s. (b) item 3: the SC-013 10 s drift re-measured with a 4-period
   BH7 window read the core's h2 leakage (4 bins from h1, inside the main lobe) as ±0.45° of "drift"; the drift now uses Phase 3
   SC-003's own estimator (whole-period windows ≥ 1 s, `spDeltaThetaDeg`, k = 2..10 against k = 1) in
   `profundum_note_behaviour_spectral_test.cpp` (`spRunLandingCell`). (c) item 5: `t22DeltaBound` omitted the total gain
   G = declick g · ADSR e (Phase 3 SC-006(c) form), so a waveform edge at e = 1 masked an undeclicked restart at sustain 0.3 and the
   positive control could not fail; the bound is now evaluated per sample from the control traces and the worst |dy| / bound
   ratio is returned. (d) item 5: `t22ZcDescriptors` t_s was "the last unsettled stamp", which read 0 when the whole fall fits
   inside the first half-period (E1, T = 10 ms); it is now the first stamp from which every later estimate is within tsCents.
   The fixer's log after (a)–(d): F:/tmp/p4fix/nb2.log (remaining reds there are items 1, 2 and the θ cells of item 3). The
   fixer's backups: F:/tmp/p4fix/spectral_orig.cpp (before), clean.cpp (after). T023 epoch 2 builds on these, does not redo them.

**T023 epoch 1 (2026-10-13, superseded by epoch 3): none.** No entry existed here, and no run-log file (`evidence/run-log/T001, T002, T003, T005, T018, T021`)
records a defect against another task's file. T005 section 4's decision has already been ruled (T005 section 5, 2026-10-12), so it is
not a defect. T018 and T021 record their cases as written but NOT YET MEASURED. A red reading from them (including any
`SC013-R2-DEFECT` line) or from T026 goes here and is fixed under T023's rules.

**T023 epoch 5 (2026-10-13) — BLOCKED on four rulings; full readings in `evidence/run-log/T023.txt`.** Re-measured on the
FR-070 + item-8 tree first.
1. SC-015: **resolved** (FR-070 + item 8). 384 cells, worst −123.65 dBFS; the triage cell kGrowl E1 44.1 kHz bridge-midfall now reads −135.68 dBFS.
2. SC-010:
   - (a) **needs a ruling (B).** The event list is correct. ClickDetector with its documented defaults flags the
     *steady, transition-free* core at every note, rate and recipe (e.g. E1 44.1 kHz kSawAnchor 27 detections in 0.5 s;
     C3 96 kHz kGrowl 115), so zero clicks cannot be reached by any Phase 4 component. The same applies to SC-021 (UseCaseRenders).
   - (b) **harness fixed.** The steady D/P is now taken over every 1/8-semitone key in the window's pitch range: the f_top-only
     key was not an upper bound. C3/E2 windows over the bound dropped from 1011/351 to 543/324 (retrigger) and from 1068/576
     to 744/336 (glide). The positive controls still fail (1.98 / 1.98 / 2.10). The remaining breaches (worst ratio 1.67)
     sit 48–150 samples after a +12 st Drop snap and come from the core's shape slew. **Needs a ruling (B′).**
3. SC-013 / SC-026:
   - θ and drift: green on every shard except 2 E0 cells (A 48, T 10 ms, s 0, R 1: 3.83° / 23.5°). Their solve was
     unconverged at the Newton cap (residual 9.8e-3 / 6.6e-2 cycles). **Fixed in `drop_phase_lock.h`:** an Illinois
     continuation (≤ 24 evaluations) runs only when the residual is still > 1e-6 cycles. Those cells now read 0.328° / 0.329°;
     the shards' worst |θ| is 0.354 / 0.449 / 0.534 / 0.500 / 0.449° (E0 / E1 / E2 / C3 / PunchGhostE1). Every other solve is unchanged.
   - FOLLOW: green (worst 0.0102°, 60 s drift 0.081°).
   - R-2 gaps > 0.3° are recorded per shard. They are not gated.
   - Anchor output memcmp: **needs a ruling (A).** phase() is bit-identical. The output differs (~−70 dB) only because
     a1 = core.deliveredGains()[0] (deviation 6) follows the corrected pitch through applyMask's P0 renormalisation. With a1
     held constant the memcmp passes in LOCK, PUNCH and GHOST.
4. NoAllocation: **resolved** (item 8(a)).
5. Audibility and the positive control:
   - DeclickPositiveControl and Audibility t_s: **resolved** (item 8(c)(d)).
   - Audibility Retrigger onset 2: **needs a ruling (C).** It reads Reset −51.74 vs Continue −53.71 dB. The declick is
     correct, but the bar compares the dip with the Continue render's own zero crossing, so it depends on the waveform phase.
6. SC-019: **ruled (D), spec Clarifications 2026-10-13 "T023 epoch 5 readings"; (A)–(C) likewise — T023 epoch 6 applies all four.**
   - Bit-identical changes made: locals-based `run()` loops in `drop_phase_lock.h` / `drop_envelope.h`, the shared bracket
     ends, and no float sum in the perf harness. Every reading is unchanged.
   - Per-component profile (ns/sample, min of 15): env 2.4, lock 3.3, solve 2.3, glide 1.8, declick 0.5, full 10.1–10.5,
     against a bar of 10.42.
   - Official lane: 0.0470 % (PASS), 0.0530 %, then 0.0514 % (FAIL) on the final code; T028 read 0.0843 / 0.0709 %.
   - Further levers change readings: the per-hold mirror of T005 ruling (ii), or lazy bracket ends.
7. Verified: T026's epoch-2 filter excludes `[performance]`.
8. Built on, not redone.

**T023 epoch 6 (2026-10-13): BLOCKED on four readings. Full readings are in `evidence/run-log/T023.txt` (epoch 6).**
Rulings (A)–(D) are applied; no bar was changed.
- (D) per-hold mirror: in `drop_phase_lock.h`.
- (A) output-diff gate: in the spectral test and the note-behaviour test (via the helpers).
- (B) differential count: `t22CountClicks`.
- (B′) delivered-gain Dc: harness `coreDeltaBound`.
- (C) same-window Continue RMS.
- Other gates: processors per-push green; SC-013 θ / drift unchanged from epoch 5; SC-015, FOLLOW and NoAllocation green; zero warnings; check-append-only PASS.
1. (B′) **resolved**: 0 windows over the bound on every retrigger and glide shard (worst ratio 0.8536). The positive controls still fail (b): 1.99 / 1.99 / 2.11.
2. (A) **red on the SC-013 grid**:
   - AnchorUntouched reads −69.95 / −69.74 dB (pass).
   - Snap landing cells: 510 of 1536 are over −60 dB re peak (worst −8.63 dB E0, −9.58 E1, −12.91 E2, −38.77 C3; PunchGhost −9.54).
   - Mostly R = 1 / short T cells, where |γ| reaches several st and applyMask moves a1.
   - pitch, phase() and modeGain stay bit-identical. Needs a new ruling on the bar.
3. (B) **the guard fails**: all three positive controls go unflagged by (a) under the differential count.
   - The Reset step lands at h1 phase 0, the saw's own edge: 0.36 / 0.36 / 0.50 samples from a reference detection, so any N ≥ 1.
   - Residual counted detections remain: smoke 34 / 9234, glides 5 / 1421, kick-bass-hybrid 11 / 10, grids 22k–28k (retrigger) and 3.8k–9.2k (glide).
   - Needs a ruling: position-only counting cannot satisfy both the guard and the zero bar.
4. (C) **onset 2 red by 1.07 dB**: Reset −51.74 vs Continue −32.81 dB in the same window (gap 18.93 < 20). Onsets 1 and 4 pass.
5. (D) **SC-019 still over**:
   - Official lane, alone after ≥ 3 min idle: 0.0598 % (12.465 ns/sample, bar 0.05 % = 10.42).
   - Profile: env 2.5–3.2, env+lock 7.3 (was 8.2), declick 0.5–0.7, glide 2.2, full 9.6–11.2 ns/sample.
   - SURFACED: no further lever is ruled.

**T023 epoch 7 (2026-10-13): DONE. The user rulings of spec Clarifications "Session 2026-10-13 — T023 epoch 6 readings" are applied.**
Full readings are in `evidence/run-log/T023.txt` (epoch 7). No bar number changed, and (B′) is unchanged.
1. **(A) SC-026: resolved.**
   - The harness records the Anchor's smoothed a1 per sample (a replica of its a1 smoother) in `NoteBehaviourTraces::anchorA1`.
   - Gate: output_on = output_off × a1_on / a1_off per sample, at most −120 dB re the peak.
     - AnchorUntouched: −139.68 / −142.24 / −139.68 dB.
     - SC-013 shards, worst: −138.19 (E0), −139.60 (E1), −141.82 (E2), −142.12 (C3), −139.60 (PunchGhostE1) dB.
   - Pitch, increment, phase() and mode gain are still compared byte for byte (memcmp).
   - The raw difference is recorded only. Worst −8.63 dB at E0 A 48 T 10 ms R 1, which is the Phase 9 listening item.
2. **(B) SC-010 / SC-021: resolved.**
   - (a)'s differential count is recorded per cell, with no gate. Smoke 34 of 9234; grids 22.6k–28.0k (retrigger) and 3.8k–9.2k (glide).
   - The gates are (b) at every transition and (c)'s bound: 0 windows over the bound everywhere. Worst ratios are 0.8536 (retrigger) and 0.8254 (glide).
   - The positive controls are proven by (b) and (c): 1.99 / 1.99 / 2.11.
   - SC-021 (UseCaseRenders) gates (b) at every transition window: 0 over the bound, worst ratio 0.7495.
3. **(C) SC-014 Retrigger: resolved.**
   - The reference is the Continue render's one-period RMS centred on its effective sample.
   - Gaps: 57.75 / 38.95 / 72.85 dB, against a bar of 20 dB.
4. **(D) SC-019: resolved by three levers that leave the per-sample arithmetic unchanged.**
   - The levers: `DropEnvelope::run` spans per piece; `DropPhaseLock::run` split into carry, bump and pass-through segments; the Quantized glide hot path, with its step moved out of line (Phase 4 lines of `mono_handler.h` only, check-append-only PASS).
   - Pinned profile, full: 8.46–8.59 → 6.55–7.19 ns/sample.
   - SC-013 θ/drift, SC-024 and the Mirror test read exactly as in epochs 5 and 6.
   - Official lanes, each alone after 200 s idle: 0.0376 / 0.0382 / 0.0526 %. The median, **0.0382 % ≤ 0.05 %, PASS**.
   - Lane noise:
     - Lane 1's S5.4 burst (1.705, code unchanged) and lane 2's SpectralSmear_CpuBudget (not Phase 4) each flipped once.
     - Both passed when re-run alone after idle.
5. Final state:
   - Per-push Phase 4 TUs: "All tests passed (36365 assertions in 22 test cases)".
   - Processors per-push: 10802834 / 3409, green.
   - 16 [long] shards green.
   - Ruinae, Seraphis, Vorago and Innexus green.
   - Zero warnings; check-append-only PASS.
- SC-021 listening (user, 2026-10-14): 3 of 4 presets pass; kick-bass hybrid "thump is more like a squeak" (A 48 from E1 = 659 Hz
  start, four octaves in 45 ms). Re-authoring under FR-068: three candidates rendered for a re-listen (spec Clarifications
  2026-10-14); the adopted values replace plan S11.4's hybrid row and the test table, then the eight renders are regenerated.
- SC-021 re-listen (user, 2026-10-15): shipped hybrid and c3-C2 "lower"; c1 and c2 pass; c1 adopted (A 24 / 45 ms / elastic / R 1.0 /
  LOCK), plan S11.4 and the test table updated, renders regenerated ("All tests passed (2610 assertions in 1 test case)"), candidate
  files removed; SC-021 4 of 4. SC-020 closed by the Vorago test record (a7e5de5f / main 6ace4b04). Compliance 86 / 0 / 0.
