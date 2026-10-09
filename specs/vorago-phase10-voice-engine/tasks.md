# Tasks: Vorago Phase 10 — Voice & Engine

**Spec:** `specs/vorago-phase10-voice-engine/spec.md`
**Plan:** `specs/vorago-phase10-voice-engine/plan.md`
**Roadmap:** `specs/Vorago-roadmap.md` → Part A → Phase 10 (lines 446–474), Open Questions 5 and 6
(lines 588–589).
**Deliverables:** three new Layer 3 headers (`vorago_voice.h`, `vorago_engine.h`,
`vorago_macro_matrix.h`); **two** append-only edits to shipped Layer 3 headers (`continuous_body.h` —
six dark materials, AR-4; `feedback_ecology.h` — one RT-safe `silenceAudio()`, B-7); one new test-helper
header; two new Node tools; **nine** new test TUs (eight in `dsp_systems_tests`, one in
`dsp_effects_tests`); edits to three Seraphis-owned test TUs (the `kNumMaterials`-sized sites, FR-038).
**Plugin work:** none — Phase 11 owns all of it.

All paths are repo-relative to `f:/projects/iterum`.

---

## How to execute these tasks

**Canonical order inside every task, no exceptions:**

1. Write the failing test **first**, in the named file, with the named `TEST_CASE` and the exact
   assertions given. Build, run it, confirm it **fails** (or fails to compile, where the task says so).
2. Implement the minimum that makes it pass.
3. Fix **all** compiler warnings the change introduces — zero warnings is the gate, not a goal.
   "Pre-existing" is not a category.
4. Run the named verification command and confirm green.

**Build and run (Windows, always the full CMake path):**

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_effects_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "VoragoVoice_PartitionInvariance" 2>&1 | tail -5   # one case
```

Catch2 filters are **positional** (`<exe> "Vorago*"`); tags go in brackets (`"[long]"`). Never
`ctest -R <exe>` — `catch_discover_tests` registers Catch2 case names, not executable names.

**Parallelism.** Tasks marked **[P]** inside the same GROUP touch fully disjoint **new** files and may
run concurrently. Every task that edits a shared file (`dsp/tests/CMakeLists.txt`, `continuous_body.h`,
`feedback_ecology.h`, `vorago_voice.h`, `vorago_engine.h`, a TU another task also touches) is **alone in
its group** — which is why most groups hold exactly one task.

**No commit tasks.** Commits happen outside this workflow.

**Binding rulings** (plan S0.4 and S14; if the user overrules one, only the named task changes):

| Ruling | What it fixes | Where |
|---|---|---|
| **B-1** | `bloom_.setCapacity(clamp(cloud.getActivePartialCount(), 14, 64))`; the parent count handed to `processChunk` is `bloom_.reserveBase()`, **never** `getActivePartialCount()` | plan B-1, T011 |
| **B-2** | `amplitudes_[i] = std::exp2(-p * detail::kHarmonicCloudLog2N[i])` — **never** `std::pow`; `ratios_[i] = float(i + 1)` exactly | plan B-2, T011 |
| **B-3** | Two 2nd-order all-passes `{b0=a, b1=0, b2=1, a1=0, a2=a}` + a **one-sample delay on R**; the claim is per-channel flatness and a *bounded* mono sum, not a flat mono sum | plan B-3, T012 |
| **B-4** | The non-finite probe is a `detail` friend struct **defined in the test TU** — no `KRATE_DSP_VORAGO_TEST_HOOKS`, no `target_compile_definitions` line | plan B-4 / Q-C, T006, T020 |
| **B-5** | The atmosphere wet return is summed into the voice-sum bus **before** the subharmonic stage | plan B-5, T015 |
| **B-6** | The CPU solve is `N·V + (kMaxVoices − N)·L + G ≤ budget`; `kMaxVoices` is a **budget number**, not a free ceiling | plan B-6 / S12, T021, T031 |
| **B-7** | `FeedbackEcology::reset()` is control-thread only; the voice gets `reset()` / `resetForRecovery()` / `resetForSteal()` / `silence()`, and the last three route through `silenceAudio()` | plan B-7, T007, T010 |

**Stop-and-surface (binding on every task):** no baseline may be raised, no threshold relaxed, no
workload shrunk, no material excluded from a loop, no test tagged `[long]` to dodge a per-push failure.
Reduce cost, re-voice, or stop and surface the measurement to the user (roadmap line 558).

---

## Group and task map

| Group | Tasks | Theme |
|---|---|---|
| G1 | T001 | SC-016 clause 3 pre-change capture — **on the clean tree, before any header is touched** |
| G2 | T002 | Write the thirteen plan amendments back into `spec.md` |
| G3 | T003 [P], T004 [P], T005 [P] | Two Node tools + the analysis half of the fixtures header (three new files) |
| G4 | T006 | Build wiring: 3 header stubs, 9 TU stubs, every registration point |
| G5 | T007 | `FeedbackEcology::silenceAudio()` — the second shared-component change (B-7) |
| G6 | T008 | `ContinuousBody`: six dark materials, append-only (AR-4) |
| G7 | T009 | FR-038: the seven `kNumMaterials`-sized sites across three Seraphis-owned TUs + SC-025 |
| G8 | T010 | `VoragoVoice`: constants, config, `prepare`, the four clearing paths, seeds, envelope |
| G9 | T011 | `VoragoVoice`: the bloom → cloud spectrum handoff (B-1, B-2, FR-011, FR-012) |
| G10 | T012 | `VoragoVoice`: the render chain, noise decorrelation (B-3), two-body blend, carry FIFO, retirement |
| G11 | T013 | `VoragoVoice`: the identity layer — `publishIdentity()`, FR-020…FR-026 |
| G12 | T014 | `VoragoEngine`: constants, config, `prepare`, polyphony, seeds, notes, stealing |
| G13 | T015 | `VoragoEngine`: the render loop, the two control steps, output stage, latency, containment |
| G14 | T016 | The Vorago-typed half of `vorago_fixtures.h` (`makeEngine`, `kFastAttackEnvelopeConfig`, …) |
| G15 | T017 | `VoragoMacroMatrix`: enums, PODs, `kRows`, the six predicates, `apply` / `computeCavernTargets` |
| G16 | T018 [P] … T022 [P] | Five disjoint new test TUs: voice-longrun, engine-longrun, non-finite, perf, composed |
| G17 | T023 | Engine TU: determinism, steal, allocation, latency, slot seeds, life parity, held sub |
| G18 | T024 | Engine TU: the per-push sentinels and setter contracts |
| G19 | T025 | Macro TU: SC-008 sweeps, SC-009, SC-010, SC-023 (matrix), SC-028, FR-067 |
| G20 | T026 | Dark-materials TU: SC-015 spectral separation (`[long]`) |
| **G21** | T027 … T031 | **Integration:** registration audit, full-suite run, portability/lint, Seraphis-green + clang-tidy, perf-alone + the Q-A ruling |

---

# GROUP 1 — Pre-change capture

## T001 — SC-016 clause 3: capture the Seraphis material survey **before** `continuous_body.h` is touched

**Files created:** `specs/vorago-phase10-voice-engine/artifacts/seraphis-material-survey-prechange.log`.
**Files edited:** none. **The working tree must be clean of Phase 10 header edits when this runs.**

SC-016 clause 3 requires that `seraphis_perf_test.cpp`'s material survey reports **the same worst
material** after the append as before it. With no pre-change capture the clause is unexecutable, and the
build-time resolution is to quietly weaken it to "the worst material is one of the five" — which is not
what SC-016 asks. The survey case is `[.perf]` and a default run skips it, so it must be requested.

> **Superseded for the per-material comparison (Q-K, Q-S).** This capture was taken on a heat-soaked
> machine; Q-K re-captured both trees in one thermal state from a worktree at `ad7481f6`, and Q-S
> (2026-09-21) moved the per-material 10 % comparison to `ContinuousBody_CpuBudget`'s paired tables
> because the Seraphis survey's best-of-16 × 200 shape scatters ±20 % on identical code. The argmax
> clause still reads this survey. The seven Seraphis pairs and three ContinuousBody pairs are in
> `artifacts/sc016-c3-paired/`.

```bash
mkdir -p specs/vorago-phase10-voice-engine/artifacts
node tools/run-cpu-tests.js dsp_systems_tests 2>&1 \
  | tee specs/vorago-phase10-voice-engine/artifacts/seraphis-material-survey-prechange.log
grep -i "worst material" specs/vorago-phase10-voice-engine/artifacts/seraphis-material-survey-prechange.log
```

Run it **alone**, nothing else executing, after the machine has idled — it is a CPU-budget suite
(`CLAUDE.md`: a test that flips verdicts between runs is measuring the machine, not the code).

**Verify.** The log exists, the `grep` prints a material name, and that name is quoted verbatim in
`compliance.md` when the phase reports. T030 diffs the post-change capture against this file.

---

# GROUP 2 — Spec amendments

## T002 — Write the thirteen amendments and the two clarifications back into `spec.md`

**Files edited:** `specs/vorago-phase10-voice-engine/spec.md` only.

The plan resolved thirteen places where the spec's own text disagrees with the shipped code or with the
spec's other text (plan S14.1), plus two of the four user questions that are clarifications rather than
choices. Write them in **now**, before implementation, so the compliance table is not arguing with
itself at the end. **No threshold is relaxed by any of them.**

| # | Edit |
|---|---|
| A-1 | SC-008's `Depth` row metric becomes: *the dB ratio of the composed render's RMS to the RMS of the same render with `CavernVerb::setMix(0)`, both over `[10 s, 60 s]`.* **≥ 6 dB endpoint threshold unchanged.** State the reason inline: `kDefaultMix = 1.00f` and `setMix`'s doxygen make the wet/total share saturated at both endpoints. |
| A-2 | Add a note under SC-008's `Mass` row: the metric and its ≥ 4 dB threshold stand **unchanged**; the plan deleted the `Mass → BodyBlend` row (which opposed the metric), and the row's absence is deliberate. |
| A-3 | Add to FR-070 / SC-014 a division-of-labour note: SC-014 is the per-push `AllocationScope` arm at 10-minute-equivalent; SC-004b carries FR-070's 8 h `getAllocatedBytes()`-invariance clause unaccelerated. Neither is narrowed. |
| A-4 | FR-014: "the 4-stage `MultiStageEnvelope`" means **four pre-sustain stages**. The shipped `kEnvelopeStages = 6` adds a 0 ms sustain-hold at index 4 and a 0 ms post-sustain at 5, both required by `advanceToNextStage()`'s contract. |
| A-5 | FR-081's lever list gains **L-7 — lower `kMaxVoices`** as the ladder's first step, bounded below at the shipped polyphony. |
| A-6 | SC-002's stage list moves `atmosphere` from the **voice** column to the **engine** column (OQ-1 ruling (b) moved `AtmosphereEngine` to `VoragoEngine`). |
| A-7 | SC-016's opening: "FR-030 – FR-039b" → "FR-030 – FR-039". There is no FR-039b. |
| A-8 | SC-023 gains (i) the **matrix** arm (`VoragoMacro_UnpreparedAndDegenerate`) its own text already demands, and (ii) a degenerate sample-rate arm — NaN (bit pattern through a `volatile`), `0.0`, and `4000.0` — which is the only assertion of FR-076's second sentence anywhere. |
| A-9 | SC-019 clauses 1 and 3 gain the `Partial` family read-backs `HarmonicCloud::getMutation()` and `BloomEngine::getDepth()`; clause 2's arm for that family reduces to "never below base", because no scheduler family writes those two targets. |
| A-10 | SC-022 clause 2: the smear-**enabled** arm is configured `setSmearAmount(0)` + `setSmearDecoherence(0)` before the render, because only at 0/0 is `SpectralSmear` an exact identity delayed by `fftSize`. |
| A-11 | FR-087: record that the probe shape is carried as Q-C (see below). |
| A-12 | FR-026: neither life-modulator lane carries a voice-side depth factor (both components scale internally); the tidal lane is published as a **net**, `max(0, ·)`. Both lanes become observable via `getBreathingGravityLane()` and `getTidalFogDepth()`. |
| A-13 | The spec's deliverable list and AR-4's neighbourhood: this phase makes **two** append-only shared-component changes — `continuous_body.h` **and** `feedback_ecology.h` (one public `silenceAudio()`). SC-016's Seraphis gate does not widen; `FeedbackEcology` has no Seraphis consumer. |
| Q-C | FR-087's second sentence becomes: *"It is a `detail`-namespace friend struct forward-declared in `vorago_engine.h` and defined in `vorago_nonfinite_test.cpp`, in the shape `seraphis_engine.h:193-195` uses — no compile definition is added, because the class definition does not change."* |
| Q-D | Add one sentence to FR-011/FR-012: the count handed to `BloomEngine::processChunk` is `reserveBase()`, and `count == getActivePartialCount()` holds whenever richness keeps the active count at or above `kMinCloudCapacity = 14`. |

**Q-A (shipped polyphony and `kMaxVoices`) is NOT written here** — it is a user ruling, surfaced in
T031 after the measurement. Q-B (the two unshipped `AtmosphereEngine` behaviours) was ruled
2026-09-17 before the build (spec.md Clarifications, Q-B: configuration ships now, the behaviours go
to roadmap Phase 10a) and needs no surfacing. Until Q-A is ruled, **no SC-001b baseline may be checked in** (FR-083).

**Verify.** `grep -n "FR-039b" specs/vorago-phase10-voice-engine/spec.md` prints nothing; each amended
criterion reads coherently on its own; no numeric threshold in the file has moved (diff-review every
changed line).

---

# GROUP 3 — Standalone new files (parallel)

## T003 [P] — `tools/gen-vorago-material-tables.js` (S4.1)

**Files created:** `tools/gen-vorago-material-tables.js`. **Node, not Python** (project rule).

**Test first.** The script is its own test: it must **exit non-zero** if any generated table fails
strict ascent or the room-mode thinning rule. Write the three assertion blocks before the generators,
and confirm the script fails when a deliberately un-thinned room table is fed through them.

**Implement.** Prints three paste-ready `constexpr` initialiser blocks, 4-decimal fixed formatting,
explicit `f` suffixes, 32 entries each:

| Table | Law |
|---|---|
| `kRoomModeRatios` | `f(nx,ny,nz) = (c/2)·sqrt((nx/Lx)² + (ny/Ly)² + (nz/Lz)²)` over `nx,ny,nz ∈ [0,4]`, excluding `(0,0,0)`; room proportions **1 : 1.26 : 1.59** (Sepmeyer); sorted ascending, normalised by the first so `r[0] = 1.0` exactly, thinned to a **minimum spacing of 1.5 %**, first 32 kept |
| `kClampedPlateRatios` | The eight published clamped-circular-plate values verbatim — **1.000, 2.080, 3.410, 3.890, 5.000, 5.950, 6.820, 8.280** (Leissa NASA SP-160 Table 4.4) — then a constant-modal-density **linear continuation** of the LSQ slope over `k = 4..8`, anchored at `k = 8`. **Print the LSQ slope**, so the number in the header comment is the number the script computed |
| `kBarRatios` | `r[n] = (β_n / β_1)²`, `β_1..β_5 = 4.73004, 7.85320, 10.99561, 14.13717, 17.27876`, `β_n = (2n+1)π/2` for `n ≥ 6`. First eight must print as **1.0000, 2.7565, 5.4039, 8.9330, 13.3443, 18.6379, 24.8138, 31.8719** (corrected at T003; a six-digit π gave 24.8137 / 31.8718) |

Assertions inside the script: every table strictly ascending over all 32 entries; `r[0] == 1.0000`;
the room table's consecutive ratio gaps all `>= 1.015`.

**Verify.** `node tools/gen-vorago-material-tables.js` exits 0 and prints three blocks; the eight
`kBarRatios` values above match exactly.

---

## T004 [P] — `tools/check-seraphis-green.js` (SC-016 clauses 1–2)

**Files created:** `tools/check-seraphis-green.js`. Node. Exits non-zero with the offending paths.

**Test first.** Run it on the clean tree: it must exit **0**. Then stage a throwaway one-line deletion
in `continuous_body.h` and confirm it exits non-zero naming that file; revert.

**Implement.** Three mechanical checks:

1. `git diff --numstat --diff-filter=M dsp/tests/unit/systems/` names **exactly**
   `continuous_body_perf_test.cpp`, `continuous_body_test.cpp`, `seraphis_perf_test.cpp` — no more, no
   fewer.
2. `git diff --numstat dsp/include/krate/dsp/systems/continuous_body.h` shows **exactly two deleted
   lines**, and `git diff -U0` on the same file confirms they are the `BodyMaterial` enum line and the
   `kNumMaterials` line.
3. `git diff --numstat dsp/include/krate/dsp/systems/feedback_ecology.h` shows **zero** deleted lines.

The header comment must state why `feedback_ecology.h` is in scope for the zero-deletion bar but **out**
of scope for the Seraphis gate: the component has no Seraphis consumer (verified by a repo-wide grep —
only Vorago Layer 3 peers, `lint_all_headers.cpp` and its own five test TUs include it), so its green
gate is its own suites (T030 step 2), not this script.

**Verify.** `node tools/check-seraphis-green.js` exits 0 on the clean tree.

---

## T005 [P] — `tests/test_helpers/vorago_fixtures.h`, the analysis half (S10.2)

**Files created:** `tests/test_helpers/vorago_fixtures.h`. **No CMake edit** — `tests/test_helpers` is
an INTERFACE target exposing the whole directory (`tests/test_helpers/CMakeLists.txt:7-12`), which is
also what makes this header reachable from `dsp_effects_tests` (where T022's composed TU lives).

**Test first.** A temporary `TEST_CASE("VoragoFixtures_AnalysisHelpers", "[systems][vorago]")` in
`dsp/tests/unit/systems/vorago_voice_test.cpp` (the stub from T006) that pins each helper on a synthetic
signal, deleted by nothing — it stays as a self-check of the helpers every later case depends on:

- `spectralCentroidHz`: a 1 kHz sine at 48 kHz → within **±2 %** of 1000 Hz.
- `bandEnergyDb(span, sr, 900, 1100)` on the same sine → at least **20 dB** above
  `bandEnergyDb(span, sr, 4000, 8000)`.
- `spectralFlatness`: white noise → **> 0.5**; the sine → **< 0.05**.
- `crestFactorDb`: a full-scale sine → within **±0.5 dB** of 3.01 dB.
- `maxDeltaInWindow(span, windowSamples)`: on a 1 kHz sine at 48 kHz, the 20 ms-window maximum
  sample-to-sample delta equals `2π·1000/48000 ≈ 0.1309` within **±5 %**.
- `spearmanRho({1,2,3,4,5}, {1,2,3,4,5}) == 1.0` and `spearmanRho({1,2,3,4,5}, {5,4,3,2,1}) == -1.0`.

**Implement.** Header-only, `#pragma once`, no Vorago type named yet (T016 adds that half). Provide
exactly: `spectralCentroidHz(span, sr)`, `bandEnergyDb(span, sr, lo, hi)`, `spectralFlatness(span, sr)`,
`crestFactorDb(span)`, `perBandTotalVariation(span, sr)`, `perBinMagnitudeFlux(span, sr)`,
`blockRmsDb(span, blockLen)`, `spearmanRho(const std::array<double,5>&, const std::array<double,5>&)`,
`maxDeltaInWindow(span, windowSamples)`. One implementation of `maxDeltaInWindow` shared by SC-010,
SC-011, SC-017a and SC-018a — four criteria, one statistic, no per-case re-derivation.

**It must NOT include `<allocation_operator_overrides.h>`.** The single owner in `dsp_systems_tests` is
`unit/systems/selectable_oscillator_test.cpp:388`; in `dsp_effects_tests` it is
`unit/effects/aether_reverb_test.cpp:38`. A second include is a duplicate-symbol link error. Cases that
need allocation detection include `<allocation_detector.h>` only.

**Verify.** `dsp_systems_tests.exe "VoragoFixtures_AnalysisHelpers" 2>&1 | tail -5` green.

---

# GROUP 4 — Build wiring

## T006 — Three header stubs, nine TU stubs, and every registration point

**Files created**

- `dsp/include/krate/dsp/systems/vorago_voice.h`
- `dsp/include/krate/dsp/systems/vorago_engine.h`
- `dsp/include/krate/dsp/systems/vorago_macro_matrix.h`
- `dsp/tests/unit/systems/vorago_voice_test.cpp`
- `dsp/tests/unit/systems/vorago_voice_longrun_test.cpp`
- `dsp/tests/unit/systems/vorago_engine_test.cpp`
- `dsp/tests/unit/systems/vorago_engine_longrun_test.cpp`
- `dsp/tests/unit/systems/vorago_macro_test.cpp`
- `dsp/tests/unit/systems/vorago_dark_materials_test.cpp`
- `dsp/tests/unit/systems/vorago_nonfinite_test.cpp`
- `dsp/tests/unit/systems/vorago_perf_test.cpp`
- `dsp/tests/unit/effects/vorago_composed_chain_test.cpp`

**Files edited**

- `dsp/tests/CMakeLists.txt` — inside the **enumerated** `add_executable(dsp_systems_tests …)` list
  (opens at `:324`, ends at `:493` after the Phase 8 block), append the eight systems TUs with a
  comment block in the file's own house style naming which criteria each carries (plan S11.1 gives the
  block verbatim). Then, inside `add_executable(dsp_effects_tests …)` (opens at `:504`, last Phase 9
  entry is `unit/effects/aether_reverb_damper_offset_test.cpp`), append **only**:
  ```cmake
      # Vorago Phase 10 (specs/vorago-phase10-voice-engine): the composed
      # engine -> CavernVerb -> processOutputStage chain. It lives HERE, not in
      # dsp_systems_tests, so it compiles under this target's KRATE_DSP_AETHER_TEST_HOOKS
      # consistently with every other TU in this executable (FR-084a / OQ3).
      unit/effects/vorago_composed_chain_test.cpp
  ```
  **No `target_compile_definitions` line is added anywhere** (B-4 / Q-C). A TU not in these lists
  silently drops out of the build and its cases never run.
- `dsp/lint_all_headers.cpp` — in the Layer 3 block, after
  `#include <krate/dsp/systems/ecosystem_engine.h>` (verified at `:194`), append:
  ```cpp
  // Vorago Phase 10 (specs/vorago-phase10-voice-engine), FR-001
  #include <krate/dsp/systems/vorago_voice.h>
  #include <krate/dsp/systems/vorago_engine.h>
  #include <krate/dsp/systems/vorago_macro_matrix.h>
  ```
  A header absent from this TU is never strict-lint compiled.
- `dsp/CMakeLists.txt` — add the three headers to `KRATE_DSP_SYSTEMS_HEADERS` (IDE/source-group list,
  kept complete).

**Test first.** Each new TU carries exactly one scaffold case, deleted by the first real task on that
TU:

```cpp
TEST_CASE("VoragoVoice_Scaffold", "[systems][vorago]") { SUCCEED("wiring only - deleted by T010"); }
```

(`vorago_composed_chain_test.cpp` uses `VoragoComposed_Scaffold`, `[effects][vorago]`.) The assertion
in this task is that the image still builds and both suites are still green with nine new TUs linked.

**Implement.** Each header: `#pragma once`, the house banner (layer, spec slug
`vorago-phase10-voice-engine`, roadmap lines 446–474, the AR rulings that shape it), then
`namespace Krate { namespace DSP { class VoragoVoice { public: VoragoVoice() noexcept = default; }; }}`.
`vorago_engine.h` includes `vorago_voice.h`; `vorago_macro_matrix.h` includes `vorago_engine.h`.
**No `effects/` header in any of the three, ever** (AR-1; `tools/lint-layers.js:74` flags a
`systems/` → `effects/` include).

Each systems TU includes `<catch2/catch_all.hpp>` and the header it exercises; the composed TU
additionally includes `<krate/dsp/effects/cavern_verb.h>` and is the **only** Phase 10 TU that names a
Layer 4 type. **No new TU includes `<allocation_operator_overrides.h>`** (see T005).

**Verify.** Both targets build with **zero warnings**; both per-push runs are green and the case count
has risen by 9.

---

# GROUP 5 — The `FeedbackEcology` append (B-7)

## T007 — `void silenceAudio() noexcept`: the RT-safe half of `reset()`

**Files edited:** `dsp/include/krate/dsp/systems/feedback_ecology.h` (append only, **zero deleted
lines**); `dsp/tests/unit/systems/vorago_voice_test.cpp` (the behavioural case lives with the voice's
cases, because the voice is its only consumer).

**Why this exists.** `FeedbackEcology::reset()` (`:854`) runs `L.delay.reset()` per loop and its own
comment states the contract: *"reset() is a control-thread call"* (`:861-862`). The component quantifies
it — a `std::fill` over the whole power-of-two buffer, **131 072 B per loop at 48 kHz and 524 288 B at
192 kHz**, and *"a single 131 KB fill already exceeds"* the 8 889 ns control-chunk budget
(`:2401-2412`). Six loops at `kMaxDelaySeconds = 0.52` make **one** voice clear ~786 KB at 48 kHz and
~3.1 MB at 192 kHz. The voice's steal path (`silence()` → `resetForSteal()` → `noteOn()`) and FR-072's
deferred non-finite recovery are **audio-thread** paths and must not reach it.

**Test first.** In `vorago_voice_test.cpp`:

`TEST_CASE("FeedbackEcology_SilenceAudioIsRtSafeClear", "[systems][vorago]")` — written against
`FeedbackEcology` directly, before `VoragoVoice` exists:
- Prepare six loops at 48 kHz, `setMix(0.5)`, drive a loud steady state (white noise at −6 dBFS for
  2 s), then call `silenceAudio()` and render **silence in** for a window of
  `FeedbackEcology::kMaxDelayMs` worth of samples: peak output over that window **< −80 dBFS**. That
  bound and shape are the component's own — its sleep-edge clause is measured at −80 dBFS over the
  first 500 ms after a silent wake (`:1890-1896`), and *"a build that only skips the chain fails by
  60 dB or more"*.
- `silenceAudio()` on an **unprepared** instance is a silent no-op (no fault, no state change).
- Accounting is **untouched**: `crossfadeCount`, `clampEngagements`, `nonFiniteResets` and the control
  phase read back **identical** across the call. An RT path that zeroed a monotone counter would make
  `FeedbackEcology`'s own SC clauses unreadable; `clearLoopAudio` already documents that rule at
  `:2420-2424`.
- It is bounded by `config_.numLoops`, not `kMaxLoops`: prepare with **2** loops, call `silenceAudio()`,
  and assert loops 2…5 are untouched.

**Implement.** Append one public method beside `reset()`:

```cpp
/// @brief The RT-SAFE half of reset(): clear every loop's audio with NO
/// O(buffer) wipe. Calls clearLoopAudio(i) for i < config_.numLoops and
/// nothing else - no delay.reset(), no counter clear, no configuration
/// touched, no smoother snapped. Safe on the audio thread, which reset()
/// (:861-862) deliberately is not. Added for Vorago Phase 10's steal and
/// non-finite-recovery paths (plan B-7).
void silenceAudio() noexcept {
    if (!prepared_) { return; }
    for (std::size_t i = 0; i < config_.numLoops; ++i) { clearLoopAudio(i); }
}
```

The doxygen must also record **why dropping the wipe is sound**: `clearLoopAudio` does not zero the
ring, it installs a **read-mute window of exactly one delay length** with the position frozen while the
window runs (`:2412-2425`), so during the window the read contributes 0 and after it every sample the
read tap reaches was written **after** the clear. A NaN in the ring is therefore never read — which is
the entire property the O(buffer) wipe was buying.

No new class, member, enumerator or constant, so `tools/lint-odr.js` sees nothing new.

**Verify.**
```bash
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology*" 2>&1 | tail -5
git diff --numstat dsp/include/krate/dsp/systems/feedback_ecology.h   # deletions column must read 0
```
The full `FeedbackEcology*` set is this change's green gate (it has no Seraphis consumer, so SC-016's
gate does not widen).

---

# GROUP 6 — The `ContinuousBody` material append (AR-4)

## T008 — Six dark materials: enum, count, `kNumSeraphisMaterials`, three ratio tables, six profiles

**Files edited:** `dsp/include/krate/dsp/systems/continuous_body.h`;
`dsp/tests/unit/systems/vorago_dark_materials_test.cpp`.

**Ruling that shapes the whole task (plan S4.0): all six new materials are `Engine::Modal`** with
`defaultModeCount <= kModeCountCeiling (32)`. `configureNonModalSlot` (`:2226`) reaches only
`referenceHz`, `t60AtMaxResonanceSec` and `hfDampingParam` — the waveguide's stiffness/pick position and
the comb bank's spread/count are **class constants, not profile fields** — so two non-modal materials
could differ only in pitch, T60 and damping, which cannot satisfy SC-015's 5 % separation, and a
crossfade between them would take the FR-024a engine collapse (a **different workload** from the one the
shipped crossfade baseline was measured against). `configureModalSlot` (`:2151`) passes **six** profile
fields into `ModalResonatorBank::setModes`, and modal bank *i* is bound to slot *i*, so any two modal
materials crossfade properly.

**Test first.** In `vorago_dark_materials_test.cpp` (delete the scaffold case):

`TEST_CASE("ContinuousBody_AppendOnlyEnumerators", "[systems][vorago]")` — FR-039, all `static_assert`s
so a regression is a build break, not a run-time failure:
- `Glass == 0, Strings == 1, MetalPlate == 2, Chamber == 3, Ice == 4` — **no stored Seraphis material
  index moved**;
- the six new enumerators are exactly `5, 6, 7, 8, 9, 10` in the order `StoneChamber, SteelTank,
  WoodenHull, CathedralColumn, CavernWall, GlassSphere`;
- `kNumMaterials == 11`, `kNumSeraphisMaterials == 5`, `kMaterialProfiles.size() == kNumMaterials`;
- every one of the six new profiles has `engine == Engine::Modal` and
  `defaultModeCount <= kModeCountCeiling`.

`TEST_CASE("ContinuousBody_MaterialTableIsAppendOnly", "[systems][vorago]")` — SC-016 clause 4, as a
`static_assert`-able table so it cannot silently widen: `crossfadePartner` maps
Glass→Strings→MetalPlate→Chamber→Ice→Glass (the five-cycle, **unchanged**) and the six new materials
cycle **among themselves** (StoneChamber→SteelTank→…→GlassSphere→StoneChamber). A plain
`(idx + 1) % kNumMaterials` widening would re-point Ice's partner from Glass to StoneChamber and change
a Seraphis-**measured** crossfade pairing.

Both cases must fail to compile before the header change (the enumerators do not exist).

**Implement.**

1. **`kNumSeraphisMaterials`**, one appended line:
   ```cpp
   /// The five materials shipped with Seraphis Phase 4; the prefix seraphis_perf_test.cpp
   /// surveys, so appending materials cannot re-point Seraphis's measured SC-001/SC-002
   /// subject (Vorago Phase 10 FR-038a).
   static constexpr std::size_t kNumSeraphisMaterials = 5;
   ```
2. **The enum** (`:81`) gains six enumerators **after `Ice`**; **`kNumMaterials`** (`:84`) becomes `11`.
   These two lines are the **only** deletions the whole header change may contain (T004 checks it).
3. **Three ratio tables**, `static constexpr std::array<float, kModeCountCeiling>` at class scope,
   appended below `kPlateRatios` (`:705`), each pasted from `node tools/gen-vorago-material-tables.js`
   (T003) — generated, **never typed** — each carrying the sourcing block `:673-704` uses for Glass and
   Plate, and each guarded:
   ```cpp
   [[nodiscard]] static constexpr bool ratioTableStrictlyAscending(
       const std::array<float, kModeCountCeiling>& t) noexcept {
       for (std::size_t i = 1; i < t.size(); ++i) { if (!(t[i] > t[i - 1])) { return false; } }
       return true;
   }
   static_assert(ratioTableStrictlyAscending(kRoomModeRatios),     "FR-033 / :669-671");
   static_assert(ratioTableStrictlyAscending(kClampedPlateRatios), "FR-033 / :669-671");
   static_assert(ratioTableStrictlyAscending(kBarRatios),          "FR-033 / :669-671");
   ```
   Strict ascent over all 32 entries is what makes the Nyquist **prefix** truncation exact.
4. **Six profile rows** appended to `kMaterialProfiles` (`:729`) in enumerator order, designated
   initialisers with explicit `f` suffixes throughout (`:717-725` states why: Clang errors on narrowing
   where MSVC does not). `hfDampingParam` equals `damping.b3` on every row (the modal convention the
   five shipped rows follow):

   | Material | `ratios` | modes | α | `damping {b1, b3}` | `stretch` | `scatter` | `referenceHz` | `t60` |
   |---|---|---:|---:|---|---:|---:|---:|---:|
   | `StoneChamber` | `kRoomModeRatios` | 32 | 1.80 | `{0.90, 2.0e-7}` | 0.00 | 0.15 | 82.0 | 9.0 |
   | `SteelTank` | `kClampedPlateRatios` | 32 | 1.20 | `{0.45, 8.0e-8}` | 0.08 | 0.05 | 98.0 | 16.0 |
   | `WoodenHull` | `kPlateRatios` *(shared)* | 24 | 2.20 | `{1.40, 6.0e-7}` | 0.20 | 0.30 | 130.0 | 3.5 |
   | `CathedralColumn` | `kBarRatios` | 32 | 1.50 | `{0.60, 1.2e-7}` | 0.00 | 0.05 | 65.0 | 20.0 |
   | `CavernWall` | `kRoomModeRatios` *(shared)* | 32 | 2.00 | `{1.10, 4.0e-7}` | 0.35 | 1.00 | 55.0 | 6.0 |
   | `GlassSphere` | `kGlassRatios` *(shared)* | 20 | 2.40 | `{1.60, 9.0e-7}` | 0.10 | 0.20 | 147.0 | 5.0 |

   The header records the darkness axis and its order: **α, then damping, then mode count**
   (`a_k = k^-α`, `:660`). Every shipped material sits at α ≤ 1.0; every new one at α ≥ 1.20.

**If SC-015 (T026) later measures a centroid that is not below the minimum of the five shipped, the
lever is to raise that material's α and `b1` and re-measure — never to relax SC-015.** If SC-025 (T009)
finds a material over budget, the ladder is: re-voice (fewer modes, gentler damping) → drop the material
and surface the drop to the user (FR-038b). **Never** raise a baseline.

**Verify.**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ContinuousBody*" 2>&1 | tail -5
git diff --numstat dsp/include/krate/dsp/systems/continuous_body.h   # EXACTLY 2 deleted lines
git diff -U0 dsp/include/krate/dsp/systems/continuous_body.h          # ...the enum line and kNumMaterials
```

---

# GROUP 7 — The count-sized consumers

## T009 — FR-038: all seven `kNumMaterials`-sized sites, plus SC-025's 44-measurement table

**Files edited:** `dsp/tests/unit/systems/continuous_body_perf_test.cpp`,
`dsp/tests/unit/systems/continuous_body_test.cpp`, `dsp/tests/unit/systems/seraphis_perf_test.cpp`.
**Exactly these three and nothing else** — T004's clause 1 checks it.

**Why this is a task and not a footnote.** Three sites **assert** the count and would break loudly.
Four more are **sized** by it and would rot silently: a `constexpr std::array<T, N>` with five
initialisers still compiles at `N = 11` and zero-fills, giving six `BodyMaterial{0}` (= `Glass`) entries
surveyed at `seraphis_perf_test.cpp:662` and six **null** `const char*` streamed at `:1196`
(`os << kMaterialNames[i]`) — undefined behaviour, not a build break.

**Test first.** There is no new `TEST_CASE` here; the failing test is the **build**, plus the existing
suites. Before editing, build `dsp_systems_tests` after T008 and confirm the three asserting sites fail
(`static_assert` at `continuous_body_perf_test.cpp:283`, `REQUIRE` at `continuous_body_test.cpp:1082`,
`static_assert` at `seraphis_perf_test.cpp:419`). Those three failures are this task's red.

**Implement**, site by site:

| # | Site | Now | Becomes |
|---|---|---|---|
| 1 | `continuous_body_perf_test.cpp:283` | `static_assert(kNumMaterials == 5, "SC-005 measures 5 materials x 4 configurations = 20 measurements")` | `== 11`, message "11 x 4 = 44" |
| 2 | `continuous_body_perf_test.cpp:352` | `kMaterials`, 5 initialisers | **eleven** initialisers, enumerator order |
| 3 | `continuous_body_perf_test.cpp:360` | `kMaterialNames`, 5 initialisers | **eleven**, same order |
| 4 | `continuous_body_perf_test.cpp:373-379` | `crossfadePartner(m) = (idx + 1) % kNumMaterials` | **wraps inside each block**: `(i + 1) % kNumSeraphisMaterials` for `i < 5`; `5 + ((i - 5 + 1) % 6)` for the six new. Plain widening re-points Ice's partner and changes a Seraphis-measured pairing (SC-016 clause 4 forbids it) |
| 5 | `continuous_body_test.cpp:1082` | `REQUIRE(CB::kNumMaterials == 5u)` | `== 11u` (its sibling `REQUIRE(kMaterialProfiles.size() == kNumMaterials)` at `:1081` needs no edit) |
| 6 | `seraphis_perf_test.cpp:419` | `static_assert(ContinuousBody::kNumMaterials == 5, "SC-001 measures all five materials and uses the worst")` | **re-pointed** at `kNumSeraphisMaterials`; message unchanged in substance |
| 7 | `seraphis_perf_test.cpp:571`, `:579`, `:654`, `:662`, `:1196` | `kMaterials` / `kMaterialNames` / `MaterialSurvey::nsPerBlock` sized by `kNumMaterials`; `surveyMaterials()` and the report loop bounded by it | **all re-sized / re-bounded to `kNumSeraphisMaterials`**; the five initialisers and their contents are **unchanged**, so `kMaterials[survey.worstIndex]` at `:1205` keeps selecting from exactly the five materials Seraphis's checked-in baselines were set against |

**SC-025** is the existing `ContinuousBody_DarkMaterialBudgets` perf case widened by sites 1–3 to
**eleven materials × four configurations = 44 measurements**, each against the shipped
`kSteadyBaselineNsPerBlock` / `kOperatingBaselineNsPerBlock` / `kCrossfadeBaselineNsPerBlock` /
`kCloudOnlyBaselineNsPerBlock` at `kRegressionFactor = 1.5`. **Print the full 44-row table before any
`REQUIRE` fires** — that file's own stated discipline (`:875-880`). Note the inherited headroom is
**29 %, not 50 %**: two of the four baselines are capped rather than measurement-pinned (the file records
operating 41 141 capped at 35 500, *"exceeds `kMaxAdmissibleHalfPctNs` by 15.7 %"*, `:160-180`,
`:220-229`).

**A material over budget is re-voiced or dropped (FR-038b). The baselines do not move.**

**Verify.**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ContinuousBody*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "Seraphis*" 2>&1 | tail -5
node tools/check-seraphis-green.js
```
SC-025 itself is `[.perf]` and runs in T031, alone.

---

# GROUP 8 — `VoragoVoice`, part 1

## T010 — Constants, `VoragoVoiceConfig`, `prepare()`, the four clearing paths (B-7), seeds, the envelope

**Files edited:** `dsp/include/krate/dsp/systems/vorago_voice.h`,
`dsp/tests/unit/systems/vorago_voice_test.cpp` (delete the scaffold case).

**Test first.** All in `vorago_voice_test.cpp`, `[systems][vorago]`:

`TEST_CASE("VoragoVoice_SizeAndOwnership")` — FR-002. Prints `sizeof(VoragoVoice)` (the figure
`kVoiceSizeBound` is derived from) and asserts it is `<= kVoiceSizeBound`. Record the measured figure in
the header comment and set the bound to `ceil(measured × 1.05)` rounded up to the next 64 B — the
`seraphis_voice.h:186-204` rule. The class must be non-copyable **and non-movable** by explicit
`= delete` on all four, *stated* rather than silently produced: `ContinuousBody` user-declares a deleted
copy ctor and no move members (`continuous_body.h:647-648`), so a `= default`ed move would be **defined
as deleted** while reading as if the type were movable.

`TEST_CASE("VoragoVoice_UnpreparedAndDegenerate")` — SC-023 (voice half) + FR-076:
- every public method on an **unprepared** voice returns its documented neutral and writes nothing out
  of bounds;
- `processStereoBlock(nullptr, r, 64)` and `(l, nullptr, 64)` each **write nothing and advance
  nothing**; `n == 0` consumes **no** control step; `!prepared_` writes `n` zeros on both channels and
  does not advance;
- `n = 65536` (far above `maxBlockSamples`) and `n = 37` repeated at a never-aligning phase → finite,
  and control steps occur **once per 64 elapsed samples, not once per call** (read
  `ecosystem().getControlStepCount()`);
- degenerate configs: zero agents, zero noise sources, zero ecology loops, zero resonance peaks — each
  **clamped, never rejected**; `isPrepared()` true;
- **FR-076's floor-not-reject arm**: `prepare()` at a **NaN** sample rate (built from a bit pattern
  through a `volatile`, **never** `std::numeric_limits`, which `-ffast-math` folds), at `0.0`, and at
  `4000.0` (below the components' shared `kMinUsableSampleRate = 8000.0`) → `isPrepared()` is **true**
  in all three and a subsequent render is finite and bounded.

`TEST_CASE("VoragoVoice_SilenceClearsEcologyAudio")` — B-7's behavioural half. Render a voice to a loud
steady state with the ecology mix up, call `silence()` then `resetForSteal()`, then render with the
excitation held off (bloom depth 0, noise level at its floor, no `noteOn`): output over the next
`FeedbackEcology::kMaxDelayMs`-long window stays **below −80 dBFS**. This is the case that fails if
`silenceAudio()` is a no-op or the read-mute window is not installed.

`TEST_CASE("VoragoVoice_EnvelopeShapeIsShipped")` — FR-014 / FR-090 / A-4. Stage times and release read
back **exactly** as the table below; `Growth` mode zeroes **every** pre-sustain stage time; a
`Standard → Growth → Standard` round trip restores all six.

`TEST_CASE("VoragoVoice_SeedsAreDistinctStreams")` — FR-025. Two voices at the same seed produce
identical first-chunk sub-component state; two differing only in seed differ in **every** seeded
sub-component. `static_assert`s that the twelve salts are pairwise distinct and that
`kSlotDrawSaltBase + kNumEventSchedulers < VoragoEngine::kVoiceSaltBase`.

**Implement** (plan S2.1–S2.5, S3.1, S3.2, S3.7, S3.8).

Includes: Layers 0–2 and Layer 3 peers only — `core/db_utils.h`, `core/env_curve.h`, `core/random.h`,
`primitives/biquad.h`, `primitives/smoother.h`, `processors/{breathing_modulator, growth_envelope,
multi_stage_envelope, slow_event_scheduler, tidal_modulator}.h`, `systems/{bloom_engine, continuous_body,
ecosystem_engine, feedback_ecology, harmonic_cloud, noise_organism, resonance_drift_network}.h`.
**No `effects/` header. No `atmosphere_engine.h`** — the ghost tap is engine-owned (OQ-1 (b)).

Public constants:
```cpp
static constexpr std::size_t kControlChunkSamples = 64;
static constexpr std::size_t kMaxBlockSamples     = 2048;
static constexpr std::size_t kNumBodies           = 2;
static constexpr std::size_t kNumEventSchedulers  = 2;
static constexpr float kTailSilenceThreshold = 3.1623e-5f;   // -90 dBFS, NOT Seraphis's -100
static constexpr float kLevelReleaseMs       = 100.0f;       // a TAU, not a time-to-99%
static constexpr float kSilenceRampMs        = 1.0f;
static constexpr float kQuiescentSeconds     = 10.0f;        // derived to a chunk count at prepare()
static constexpr EnvCurve kStageCurve           = EnvCurve::Exponential;
static constexpr int      kEnvelopeStages       = 6;
static constexpr int      kEnvelopeSustainPoint = 4;
static constexpr std::size_t kBloomChildSlots  = 6;
static constexpr std::size_t kMinParentSlots   = 8;
static constexpr std::size_t kMinCloudCapacity = 14;   // kBloomChildSlots + kMinParentSlots
static constexpr float kNoiseApCoeffL = 0.6923878f;
static constexpr float kNoiseApCoeffR = 0.4021921f;
static constexpr float kBlendRampMs   = 50.0f;
// salts: kCloudSalt 0x0100, kNoiseSalt 0x0200, kResonanceSalt 0x0300, kEcologySalt 0x0400,
// kBloomSalt 0x0500, kEcosystemSalt 0x0600, kBodyASalt 0x0700, kBodyBSalt 0x0800,
// kBreathSalt 0x0900, kTideSalt 0x0A00, kSchedSaltBase 0x0B00, kSlotDrawSaltBase 0x0C00
enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 };
```

`VoragoVoiceConfig` — designated-initialiser-only POD, **every field clamped, never rejected**:
`maxBlockSamples 2048`, `numNoiseSources 4`, `numResonancePeaks 12`, `numEcologyLoops 6`,
`ecosystemAgents 32`, `ecosystemCells 64`, `ecosystemStepChunks 8`, `bloomChildSlots 6` (**not** the
component's 8 — B-1's floor would rise to 16), `maxCombDelayMs 50.0f`. **No atmosphere field.**

`prepare()`, the numbered order of plan S3.1 (it is load-bearing): rate floor → clamp config →
sub-component `prepare` with **designated initialisers throughout** → `applySeeds()` (before the first
note; `ContinuousBody::setSeed` is configure-time only, `:1339-1344`) → the FR-090 default block of plan
S8.2 written in full, every row including the unchanged ones → the agent→slot deal (T013 fills its body;
call the hook here) → derived constants → `prepared_ = true; reset();`.

Derived constants, exactly:
```cpp
levelReleaseCoeff_ = std::exp(-64.0f / (0.001f * kLevelReleaseMs * float(sampleRate_)));
silenceRampSamples_ = std::max(1, int(std::lround(0.001f * kSilenceRampMs * float(sampleRate_))));
quiescentChunksToRetire_ = std::max(1, int(std::lround(kQuiescentSeconds * sampleRate_ / 64.0)));
```
`levelReleaseCoeff_` must **not** use `calculateOnePolCoefficient` — that helper treats its argument as a
time-to-99 % (`smoother.h:86-93`) and would give `tau = 20 ms`.

**The four clearing paths (B-7 part 3), one shared body `clearRunState(bool rtSafe)`:**

| Entry point | Thread | Ecology arm | Fade tail | Sole caller |
|---|---|---|---|---|
| `reset()` | **control** | `ecology_.reset()` — the O(buffer) wipe | cleared | `prepare()`, `VoragoEngine::reset()`/`silence()` |
| `resetForRecovery()` | RT-safe | `ecology_.silenceAudio()` | cleared (a poisoned tail must not survive) | FR-072's deferred recovery |
| `resetForSteal()` | RT-safe | `ecology_.silenceAudio()` | **preserved** | the engine's steal teardown |
| `silence()` | RT-safe | `ecology_.silenceAudio()` | armed | steal, `VoragoEngine::silence()` |

The three RT paths differ from `reset()` by **exactly one call**, in one place, so they cannot drift.
`clearRunState` seeds `quiescentChunks_ = quiescentChunksToRetire_` — **at the retire value, not 0**
(`seraphis_voice.h:1022-1027`): the counter only advances inside a render, so a 0 seed would make every
never-rendered slot report `isFinished() == false` and the engine would take the full render path on all
`kMaxVoices` slots for ten seconds.

Envelope (plan S3.8), through the **single write path** `applyStage()` with shadow discipline:

| Stage | Level | ms | Role |
|---:|---:|---:|---|
| 0 | 1.00 | 20 000 | attack (FR-014, reproduced not re-derived) |
| 1 | 0.80 | 30 000 | body 1 |
| 2 | 0.92 | 45 000 | body 2 |
| 3 | 0.85 | 60 000 | body 3 |
| 4 | 0.85 | 0 | sustain hold (`kEnvelopeSustainPoint`) |
| 5 | 0.00 | 0 | post-sustain |
| release | — | 45 000 | FR-014 |

`setRetriggerMode(RetriggerMode::Legato)` written explicitly at `prepare()` (the component default is
`Hard`, which would restart a 20 s attack on every re-articulation). `Growth` mode forces **every** stage
from 0 to `sustainPoint − 1` to 0 ms, preserving level and curve.

`noteOn(f, velocity)`: retune cloud (`setFundamentalHz`), both bodies (`setNoteFrequencyHz`) and the
resonance network (`setNoteFrequency`); `mse_.gate(true)`; `growth_.trigger()` in `Growth` mode; drop an
**idle** carry but **keep a live one** (dropping a live carry skips up to 63 rendered samples and creates
exactly the click SC-011 measures). `noteOff()` is `cloud_.noteOff(); mse_.gate(false);` and **nothing
else** (AR-5). Document — do **not** repair — that `ContinuousBody` clamps note frequency to `[20, 8000]`
and `HarmonicCloud` to `[20, 4000]`.

`GrowthEnvelope::setSeed` is a documented **no-op** (`growth_envelope.h:140`) and is deliberately not
called, so it carries no salt.

**Verify.** `dsp_systems_tests.exe "VoragoVoice_*" 2>&1 | tail -5`, zero warnings.

---

# GROUP 9 — `VoragoVoice`, part 2

## T011 — The bloom → cloud spectrum handoff (B-1, B-2, FR-011, FR-012)

**Files edited:** `vorago_voice.h`, `vorago_voice_test.cpp`.

**Test first.**

`TEST_CASE("VoragoVoice_BloomCapacityTracksCloud", "[systems][vorago]")` — FR-012 / B-1. Sweep richness
across `[0, 1]` in 0.02 steps; at **every** step:
- `bloom().capacity() == std::clamp(cloud().getActivePartialCount(), 14u, 64u)`;
- `bloom().reserveBase() >= kMinParentSlots` (= 8);
- `bloom().getConsumerTiltDb() == cloud().getSpectralTiltDb()` exactly;
- `bloom().getOverlapEngagementCount() == 0` — R-6: the component's "the caller overran my reserved
  region" signal must stay a real signal, which it only does if the parent count is `reserveBase()`.

`TEST_CASE("VoragoVoice_SpectralTargetIsNeutral", "[systems][vorago]")` — FR-011 / B-2, the case that
fails if `std::pow` is used. With the bloom held inert (`setDepth(0)`, `setWake(0)`), a render with the
voice's supplied target is **bit-identical** (`std::memcmp`) to the same render with
`clearSpectralTarget()` forced — at **four richness values × four tilts × four gravities × four
inharmonicities** (the "at every setting" claim, sampled at 256 points).

`TEST_CASE("VoragoVoice_SpectralTargetEdge", "[systems][vorago]")` — SC-018a. Force the spawn edge
(`triggerBloom()` from zero live children) and the retire edge (let the last child retire):
`maxDeltaInWindow(20 ms)` across each edge **≤ 1.5 ×** the pre-edge maximum.

**Implement** `updateSpectrumTarget()`, exactly (plan S3.5):

```cpp
const std::size_t active   = cloud_.getActivePartialCount();                 // :950
const std::size_t capacity = std::clamp(active, kMinCloudCapacity,
                                        HarmonicCloud::kMaxPartials);        // B-1's floor
if (capacity != bloom_.capacity()) { bloom_.setCapacity(capacity); }         // :628
parentCount_ = bloom_.reserveBase();                                        // :710 - B-1
const float p = HarmonicCloud::kRichnessMinExponent
              + (HarmonicCloud::kRichnessMaxExponent - HarmonicCloud::kRichnessMinExponent)
                * cloudRichness_;
for (std::size_t i = 0; i < parentCount_; ++i) {
    ratios_[i]     = static_cast<float>(i + 1);                              // FR-082 identity branch
    amplitudes_[i] = std::exp2(-p * detail::kHarmonicCloudLog2N[i]);         // :57-62, :1492 - NOT pow
}
const std::size_t count = bloom_.processChunk(ratios_.data(), amplitudes_.data(),
                                              parentCount_, kControlChunkSamples);
const bool wantTarget = (bloom_.getLiveChildCount() > 0);
if (wantTarget)            { cloud_.setSpectralTarget(ratios_.data(), amplitudes_.data(), count); }
else if (targetActive_)    { cloud_.clearSpectralTarget(); }
targetActive_ = wantTarget;
```

`ratios_` and `amplitudes_` are `std::array<float, BloomEngine::kMaxSlots>` — **exactly 64 entries
each**, `BloomEngine::processChunk`'s normative precondition (`bloom_engine.h:466-481`).

Two things the header must state, because they are what a later reader gets wrong:

1. **The voice mirrors the cloud's richness in `cloudRichness_`, and every path that writes richness
   goes through `VoragoVoice::setRichness`** (which updates the shadow *and* the cloud). One write path.
   `HarmonicCloud::getRichness()` does exist (`:490`) and reading it in `updateSpectrumTarget()` is the
   equivalent implementation — take either, but not both.
2. **Neutrality is a parent-region claim, not a whole-spectrum claim.** Slots
   `[reserveBase(), capacity)` carry amplitude 0 while no child is live, where the untargeted cloud would
   carry `n^-p`. That is exactly why `clearSpectralTarget()` is called whenever
   `getLiveChildCount() == 0`.

Also state B-1's degradation direction: below `activeCount_ < 14` the **floor** wins, the top
`numChildSlots()` slots sit above `activeCount_`, and **the bloom becomes inaudible at very low richness
while the parents are never displaced** — the deliberate direction, because `capacity = activeCount_`
with no floor drives `reserveBase()` to 0 at `activeCount_ <= 6` and deletes the parent spectrum.

`setSpectralTarget` can never be rejected here: `count ∈ [1, 64]`, every ratio `> 0`, every amplitude
`>= 0` and finite — the four rejection conditions at `harmonic_cloud.h:801-803`, checked off one by one
in a header comment.

**Verify.** `dsp_systems_tests.exe "VoragoVoice_Bloom*" "VoragoVoice_SpectralTarget*" 2>&1 | tail -5`.

---

# GROUP 10 — `VoragoVoice`, part 3

## T012 — `renderOneChunk()`, noise decorrelation (B-3), the two-body blend, the carry FIFO, retirement

**Files edited:** `vorago_voice.h`, `vorago_voice_test.cpp`.

**Test first.**

`TEST_CASE("VoragoVoice_PartitionInvariance", "[systems][vorago]")` — SC-007 (voice half). 4096 samples
rendered as `1 × 4096`, `8 × 512`, and the pathological split `{36, 28, 1, 2047, 1984}` — which sums to
**exactly 4096**, asserted by a `static_assert` over the split array's sum in the case itself. All three
buffers **bit-identical** (`std::memcmp` over the whole render); fingerprint tolerance is deliberately
**not** used, because FR-007 demands exactness and all three arms are the same build in the same process.
Plus **exactly equal** `ecosystem().getControlStepCount()` across the three arms.

`TEST_CASE("VoragoVoice_NoiseDecorrelationMonoSum", "[systems][vorago]")` — FR-015 / B-3. Sweep
20 Hz – 8 kHz through the two all-passes:
- **per-channel** magnitude flat within **0.01 dB** (the all-pass property, exact by construction);
- **mono-sum** magnitude deviation **≤ 4.0 dB** across the band (ruled 2026-09-18, spec Q-E; was
  3.0 dB, unsatisfiable by construction — measured 3.66 dB at 8 kHz);
- and at least **6 dB better** than a **1 ms fractional-delay** pair measured in the same case (the
  one-sample pair is kept in the WARN line for the record only).
Print the measured worst deviation — that figure is what the header quotes.

`TEST_CASE("VoragoVoice_BodyBlendEndpoints", "[systems][vorago]")` — SC-017. At `b = 0`: render with
body B set to material X vs material Y (X ≠ Y) → **bit-identical**; and across two different body-B
seeds → **bit-identical**. Symmetrically at `b = 1` for body A. Plus: a render at `b = 0` reached by
ramping down from `b = 1` is **within fingerprint tolerance** of a render at `b = 0` from the start.

`TEST_CASE("VoragoVoice_BodyBlendNoZipper", "[systems][vorago]")` — SC-017a. `b` automated 0 → 1 over
5 s: `maxDeltaInWindow(20 ms)` on the ramp **≤ 1.5 ×** the same statistic measured 64 ms clear of the
ramp. Without this a chunk-stepped blend passes SC-017 untouched.

`TEST_CASE("VoragoVoice_DormancyIsTheComponents", "[systems][vorago]")` — FR-024. Wake at exactly 0 with
`dormant == false` behaves as the component defines it; a wake edge during a bloom fade and during a
steal ramp leaves neither the 50 ms re-entry fade nor the 1 ms silence ramp truncated.

**Implement** `renderOneChunk()` — **exactly `kControlChunkSamples`, always** (plan S3.4). Order is
fixed and is the contract:

1. identity layer (T013 fills the body; call the hook);
2. `updateSpectrumTarget()` (T011);
3. `cloud_.processStereoBlock(excL_, excR_, 64)`;
4. `noise_.processBlock(noiseMono_, 64)` — **mono out** — then, per sample: `g = noiseGain_.process()`,
   `m = noiseMono_[s] * g`, `aL = noiseApL_.process(m)`, `aR = noiseApR_.process(noiseApDelayR_)`,
   `noiseApDelayR_ = m`, `excL_[s] += aL`, `excR_[s] += aR`. **The one-sample delay on the R branch is
   what makes the pair a phase-*difference* network** rather than two unrelated all-passes (B-3);
5. the **voice envelope, in place on the excitation bus** (AR-5) — `velocity_ * mse_.process()`, times
   `growth_.getCurrentValue()` held across the chunk in `Growth` mode. **Nothing downstream of this
   point is gated**, stated normatively in the header;
6. `resonance_.processBlock(excL_, excR_, excL_, excR_, 64)` — **in place**; inputs may alias the
   outputs, in either pairing (`resonance_drift_network.h:506`);
7. `ecology_.processBlock(excL_, excR_, excL_, excR_, 64)` — in place, same documented licence
   (`feedback_ecology.h:908`);
8. the **two-body blend**: both bodies run at **every** blend value, **not** in place
   (`ContinuousBody` forbids it), then `carry[s] = (1-b)·A[s] + b·B[s]` with `b` advanced **per sample**
   by a `LinearRamp` at `kBlendRampMs = 50 ms`;
9. the silence fade tail, **guarded so `fadeRemaining_` never runs negative** (which would add an
   inverted, magnitude-growing tail forever);
10. the level detector on the **post-blend** buffer:
    `level_ = (peak > level_) ? peak : peak + (level_ - peak) * levelReleaseCoeff_;`
    `quiescentChunks_ = (level_ < kTailSilenceThreshold) ? quiescentChunks_ + 1 : 0;`
    `isFinished()` is `quiescentChunks_ >= quiescentChunksToRetire_`.

`carryAvail_ = 64; carryRead_ = 0; carryIsLifeOnly_ = false;` — and `lastOut*` is **not** assigned here:
it is captured at **serve** time in `processStereoBlock`, because on a mid-chunk steal `carryL_[63]` is
up to 63 samples of program material away from the amplitude the output actually reached.

`advanceOneChunkLifeOnly()` runs **step 1 only**, plus `updateLevel(0.0f)`, a zero-filled carry, and
`lastOutL_ = lastOutR_ = 0.0f`. It deliberately does **not** run step 2: `BloomEngine`'s clock draw is a
pure function of elapsed control steps, so advancing it in both paths would run the bloom clock twice per
chunk for a voice alternating idle and rendering.

Header must state the exact endpoint argument SC-017 rests on: at `b = 0` the expression is
`1.0f * A + 0.0f * B`, and IEEE-754 multiplication of a finite `B` by exactly `0.0f` is `±0.0f`, so body
B contributes **bit-nothing** — contribution nullity, not "a single-body render", which is not a
configuration this product has.

**Verify.** `dsp_systems_tests.exe "VoragoVoice_*" 2>&1 | tail -5`, zero warnings.

---

# GROUP 11 — `VoragoVoice`, part 4

## T013 — The identity layer: `publishIdentity()`, FR-020 … FR-026

**Files edited:** `vorago_voice.h`, `vorago_voice_test.cpp`.

`publishIdentity()` is the **only** writer of `setSourceWake` / `setPeakWake` / `setLoopWake` /
`setMutation` / `setDepth` / `ghostRequest_`, which is what makes "at depth 0 every destination reads its
configured base" a property of one expression rather than of five call sites.

**Test first.**

`TEST_CASE("VoragoVoice_WakeCombineRule", "[systems][vorago]")` — SC-019a. No render. Enumerated
(scheduler, ecosystem) pairs — both orderings, both zeros, both ones, the equal case — each shared
destination reads back **exactly** `max(base, max(scheduler, ecosystem))` through
`noise().getSourceWakeAmount()`, `resonance().getPeakWakeAmount()`, `ecology().getLoopWakeAmount()`,
`getGhostRequest()`, `cloud().getMutation()` and `bloom().getDepth()`.

`TEST_CASE("VoragoVoice_AgentReductionRule", "[systems][vorago]")` — SC-019b / FR-020a. No render.
Enumerated per-slot agent-energy tuples (a clear winner, a tie, a three-way): the slot's routed
contribution equals **exactly** the argmax-energy agent's output — **never a blend or an average**. The
tie case asserts the **lower agent index** wins (the strict `>` in the scan).

`TEST_CASE("VoragoVoice_SlotDrawDeterminism", "[systems][vorago]")` — SC-020a. Two voices, same seed and
config → **identical** slot sequences over a 30-minute accelerated render; two differing **only** in seed
→ at least one differing draw.

`TEST_CASE("VoragoVoice_LifeModulatorLanes", "[systems][vorago]")` — **FR-026's only assertion
anywhere.** Without it, an implementation that advanced `breath_` and `tide_` once per chunk and then
discarded both outputs passes every criterion in the spec.
1. With `setBreathingDepth(0)`: `resonance().getGravity()` equals `getResonanceGravity()` (the base)
   **exactly**, at every control step over 2 000 steps, and `getBreathingGravityLane()` is exactly
   `0.0f`.
2. With `setBreathingDepth(0.30)` and `breath_.setRate` at 0.017 Hz: over one full breath period
   (≈ 59 s of control steps) `getGravity() − base` traverses a range of **≥ 0.20** and its extremes are
   **±0.30** within tolerance — i.e. the lane is *summed onto* the base, not ignored and not squared.
3. `getTidalFogDepth()` is exactly `0.0f` at `setTidalDepth(0)`, reaches **≥ 0.25** somewhere inside one
   tidal period at depth 0.40, and is **never negative** (the `max(0, ·)` net).

**Implement** (plan S3.6).

**(a) The deal, computed once at `prepare()`.** Agent kinds come from a seeded stratified deal followed
by a Fisher–Yates shuffle (`ecosystem_engine.h:2168-2216`), so kind is **not** `i % 5` and the deal must
be **read**, never assumed:
```cpp
for (std::size_t i = 0; i < ecosystem_.getAgentCount(); ++i) {
    const std::size_t k = std::size_t(ecosystem_.getAgentKind(i));
    const std::size_t slots = slotCountForKind(k);   // Partial 1, Resonator numPeaks,
                                                     // Noise numSources, Feedback numLoops, Ghost 1
    if (slots == 0) { continue; }
    agentSlot_[i] = std::uint8_t(cursor[k] % slots);  agentValid_[i] = 1;  ++cursor[k];
}
```

**(b) The many-to-one reduction (FR-020a).** One pass, no sort, no allocation, `5 × 12` of stack:
`bestE` initialised to `-1.0`, and `if (e > bestE[k][s]) { bestE[k][s] = e; bestV[k][s] = getAgentOutput(i); }`
with `e = ecosystem_.getAgentEnergy(i)`. Strict `>` keeps the lower agent index.

**(c) The scheduler contribution (FR-022).** `setTargetCount(5)` at `prepare()`; the five destination
families are fixed: `{BloomTrigger, NoiseWake, PeakWake, LoopWake, GhostBurst}`.
`family = sched_[k].getActiveTarget()` (or `kNoTarget = 0xFF` while idle);
`value = max(0.0f, sched_[k].getCurrentValue())` — a **net, not a fold**, since
`setBipolarProbability(0)` makes every event positive. The **slot within a multi-slot family** is drawn
once per event from `slotDrawRng_[k]` on the **rising edge** of `isEventActive()`, latched in
`eventWasActive_[k]`, and held for the whole event. `BloomTrigger` calls `bloom_.triggerBloom()` **on the
onset edge only** — `armed_` is edge-like and a level-polling caller is the natural bug the component
guards against (`bloom_engine.h:654-663`).

**(d) The combine rule (FR-023), stated once:**
```cpp
const float eco   = ecosystemDepth_ * reducedEcosystemValue(kind, slot);
const float sched = schedulerValueFor(kind, slot);
const float wake  = std::max(base(kind, slot), std::max(eco, sched));
```
Neither source can silence a slot the other woke. Written through the shipped setters and nothing else,
so dormancy semantics stay the components' own and the voice adds no second gate (FR-024).
`Partial` agents drive `cloud_.setMutation(clamp(mutationBase_ + eco, 0, 1))` and
`bloom_.setDepth(clamp(bloomDepthBase_ + eco, 0, 1))`. `Ghost` agents and the `GhostBurst` family drive
**only** `ghostRequest_ = std::max(ecoGhost, schedGhost)` (FR-020b) — **the voice never touches
`AtmosphereEngine`; it does not own one.**

**(e) The two life modulators (FR-026).**
`resonance_.setGravity(clamp(gravityBase_ + breath_.getCurrentValue(), -1, 1))` — two lanes summed onto
base 0. **No second depth factor on either lane**: `BreathingModulator::getCurrentValue()` is already
`clamp(depth_ * bipolar, -1, 1)` (`:286`) and `TidalModulator` scales by its own depth internally
(`:207-210`, `:317`); multiplying again would make the `Movement → BreathingDepth` row quadratic in its
own parameter. **`HarmonicCloud::setSpectralGravity` is driven by neither lane** and keeps its FR-090
value. `getTidalFogDepth()` publishes `std::max(0.0f, tide_.getCurrentValue())` for the **engine** to
fold onto its own smear base — the voice never reaches `SpectralSmear`.

Both lanes are published (`getBreathingGravityLane()`, `getTidalFogDepth()`) so they are observable
rather than inferred.

**Verify.** `dsp_systems_tests.exe "VoragoVoice_*" 2>&1 | tail -5`.

---

# GROUP 12 — `VoragoEngine`, part 1

## T014 — Constants, `VoragoEngineConfig`, `prepare()`, polyphony, seeds, notes, the three-pass steal

**Files edited:** `dsp/include/krate/dsp/systems/vorago_engine.h`,
`dsp/tests/unit/systems/vorago_engine_test.cpp` (delete the scaffold case).

**Test first.**

`TEST_CASE("VoragoEngine_UnpreparedAndDegenerate", "[systems][vorago]")` — SC-023 (engine half), the
same ladder as T010's voice arm plus polyphony 1 and `setPolyphony(0)`/`(999)` clamped to
`[1, kMaxVoices]`.

`TEST_CASE("VoragoEngine_StealPolicy", "[systems][vorago]")` — SC-012, an **enumerated victim table**
driving the three passes; levels set by rendering voices to known amplitudes and read through
`getVoiceLevel(i)`:
(a) an idle slot exists → it is taken, **no steal**;
(b) one `Releasing` slot below −30 dBFS → **it** is the victim;
(c) several `Releasing`, all below → the **lowest level** wins;
(d) all `Releasing` **above** the threshold → the quietest `Releasing` **still** wins (pass 1, not
pass 2);
(e) only `Active` slots → the quietest `Active`;
(f) exact level tie → the **lower `voiceSerial_`**.

`TEST_CASE("VoragoEngine_SeedIsPerSlotNotPerNote", "[systems][vorago]")` — FR-048. `getSeed()` and each
slot's derived seed are **unchanged** across `noteOn` / `noteOff` / retire / steal cycles.

**Implement** (plan S6.1–S6.4, S6.7).

Includes: `core/db_utils.h`, `core/random.h`, `primitives/smoother.h`, `processors/spectral_smear.h`,
`processors/tape_saturator.h`, `processors/true_peak_limiter.h`, `systems/atmosphere_engine.h`,
`systems/subharmonic_engine.h`, `systems/voice_allocator.h`, `systems/vorago_voice.h`. **Never
`effects/cavern_verb.h` or any `effects/` header** (AR-1).

Public constants:
```cpp
static constexpr std::size_t kMaxVoices           = 8;    // a BUDGET number, not a free ceiling (B-6)
static constexpr std::size_t kDefaultPolyphony    = 4;
static constexpr std::size_t kControlChunkSamples = 64;
static constexpr std::size_t kMaxBlockSamples     = 2048;
static constexpr std::size_t kVoiceSaltBase       = 0xA000;   // disjoint from every voice salt
static constexpr std::size_t kAtmosSalt           = 0xB000;
static constexpr float kSumGainSmoothMs       = 100.0f;   // NOT 20 ms - the value is HELD for a chunk
static constexpr float kAmnestyLevelThreshold = 0.0316f;  // -30 dBFS
static constexpr float kOutputSaturation      = 0.12f;
static constexpr float kOutputDriveDb         = 0.0f;
static constexpr float kOutputCeilingDb       = -0.3f;
static constexpr std::size_t kResetsPerControlChunk = 1;
static constexpr float kGhostBurstPeak        = 0.60f;
static constexpr std::size_t kEngineSizeBound = kMaxVoices * VoragoVoice::kVoiceSizeBound + 64u * 1024u;
```
`kMaxVoices` **stays at 8 while SC-001a's survey runs** so the whole {1, 2, 4, 6, 8} curve is measurable;
T031 lowers it if Q-A rules that way, in the same commit as the SC-001b baseline.

`VoragoEngineConfig`: `maxBlockSamples 2048`, `VoragoVoiceConfig voice{}` forwarded verbatim,
`atmosCaptureSeconds 20.0f` (**not** a component default — a ghost grain is 12 s, so a 4 s or 8 s ring
could never hold one; the cost is `2 × 20 × fs` floats once, globally, where per-voice at `kMaxVoices`
it would have been 58 MB), `atmosBlurEnabled true`, `atmosFreezeEnabled **false**`,
`atmosBlurFftSize 1024`, `atmosFreezeFftSize 2048`, `smearEnabled **true**` (the component defaults to
false, `spectral_smear.h:141-149`; `Fog` needs it and SC-022 (2) needs both states reachable),
`smearFftSize 2048`.

`setSmearAmount` **writes `smearBase_`, not `smear_`**, and `getSmearAmount()` **reports `smearBase_`**
— FR-026's tidal fold re-writes the component every control chunk, so a setter that wrote the component
directly would be silently overwritten on the next chunk and the `Fog` row would stop meaning anything;
and a getter reading the component would make SC-009 clause 1 compare against a number the matrix does
not own.

**The four envelope fan-out forwarders** (`setEnvelopeMode`, `setEnvelopeStageTimeMs`,
`setEnvelopeReleaseMs`, `setGrowthDurationSeconds`) are the **only** mutable route from outside to the
voices — `getVoice(i)` is const and the Voice-owned macro rows travel through
`friend class VoragoMacroMatrix`. Each fans out over **all `kMaxVoices`**, deliberately unlike
`VoragoMacroMatrix::apply` (which walks `polyphony_`): these are **configuration**, and a later
`setPolyphony()` growth must not admit a slot carrying a different envelope. **No non-const
`getVoice(i)`** — a caller able to desynchronise two slots would falsify exactly that sentence, and
SC-030's parity clause rests on it.

**Stealing (FR-044)** lives in the engine, because `VoiceAllocator` has **no `Quietest` mode**
(`voice_allocator.h:55-60`) and owns no DSP, so it cannot see a level. The chosen slot is **freed before**
the allocator call so the allocator's idle search has exactly one candidate. Three passes:
(0) `Releasing` **and below** `kAmnestyLevelThreshold`, (1) `Releasing` at any level, (2) `Active`;
lowest `getCurrentLevel()` within a pass, ties on the engine-owned `voiceSerial_`. Use
`!(level < kAmnestyLevelThreshold)` rather than `level >= …`, so a **non-finite** level is treated as
*not eligible* rather than as the quietest voice in the pool. Pass 1 is not redundant: it is the branch
that makes "every candidate is at or above the threshold" still steal the quietest `Releasing` voice
instead of falling through and stealing nothing. **This is FR-044's reading of the amnesty — it protects
the loud, it does not prefer them.**

A steal calls the victim's `silence()` → `resetForSteal()` → `noteOn()`. **Both clearing calls are on
B-7's RT-safe side.** With a 4-voice pool and minutes-long tails **stealing is the normal allocation
path**, so the pre-B-7 shape would have run ~1.6 MB of `std::fill` per steal at 48 kHz inside
`VoragoEngine::noteOn`, on the audio thread.

`setPolyphony(n)` clamps to `[1, kMaxVoices]`, walks `allocator_.setVoiceCount(n)`'s NoteOff events as
**musical releases, never retirements**, marks still-sounding excess slots as orphans, and moves the
sum-gain **target** (`1/sqrt(n)`), never the value. It allocates nothing — `prepare()` prepared all
`kMaxVoices` voices regardless of polyphony.

`VoragoEngine::reset()` and `silence()` carry the **not-an-audio-thread-operation** warning: both call
`VoragoVoice::reset()` on all `kMaxVoices` slots (B-7's control-thread path) plus a `std::fill` over the
atmosphere's whole 20 s capture ring.

**Every test that constructs an engine must heap-allocate it** — `std::array<VoragoVoice, 8>` is hundreds
of KB and MSVC's default main-thread stack is 1 MiB. T016's `makeEngine()` is the only construction path.

**Verify.** `dsp_systems_tests.exe "VoragoEngine_*" 2>&1 | tail -5`, zero warnings.

---

# GROUP 13 — `VoragoEngine`, part 2

## T015 — The render loop, the two control steps, the output stage, latency, non-finite containment

**Files edited:** `vorago_engine.h`, `vorago_engine_test.cpp`.

**Test first.**

`TEST_CASE("VoragoEngine_PartitionInvariance", "[systems][vorago]")` — SC-007 (engine half). Same three
arms as T012, **bit-identical** buffers, plus **exactly equal** `getActiveVoiceCount()`.

`TEST_CASE("VoragoEngine_ReportedLatency", "[systems][vorago]")` — SC-022:
1. `getLatencySamples() == smear().getLatencySamples()` at every prepare-time configuration
   **including `smearEnabled = false`, where both are 0**;
2. two engines identical but for the prepare-time smear flag, one note, `kFastAttackEnvelopeConfig` so
   the onset is sharp, **and the enabled arm configured `setSmearAmount(0)` + `setSmearDecoherence(0)`
   before the render** (A-10 — only at 0/0 is `SpectralSmear` an exact identity delayed by `fftSize`;
   at the S8.3 defaults it actively smears and a ±1-sample onset equality is not a property a correct
   implementation has): the difference in onset sample index equals `getLatencySamples()` **within
   ±1 sample**.

`TEST_CASE("VoragoEngine_HeldSubFundamental", "[systems][vorago]")` — SC-031. `noteOn` the lowest note,
render, `noteOff` **all**, render through a **≥ 10 s** silence gap, `noteOn` a different note:
`subharmonic().getFundamentalHz()` **unchanged across the whole gap**, and the render across the gap
contains no sample-to-sample delta above SC-011's click threshold.

`TEST_CASE("VoragoEngine_TidalFogFold", "[systems][vorago]")` — FR-026 (engine half), the precedence rule
no criterion covers. With every voice at tidal depth 0, `smear().getSmearAmount()` equals
`engine.getSmearAmount()` (= `smearBase_`) **exactly** at every control step, **including after an
`apply()` at a non-neutral `Fog`**; with tidal depth 0.40 on one voice it equals
`clamp(smearBase_ + max_v getTidalFogDepth(), 0, 1)` at every control step. This is the case that fails
if `setSmearAmount` writes the component instead of the base.

**Implement** `processStereoBlock` exactly (plan S6.5):

```cpp
if (outL == nullptr || outR == nullptr) { return; }
if (n == 0) { return; }                                   // consumes no control step
if (!prepared_) { std::fill_n(outL, n, 0.0f); std::fill_n(outR, n, 0.0f); return; }

while (done < n) {
    const auto phase = std::size_t(sampleCounter_ % 64);
    if (phase == 0u) { runPreRenderControlStep(); }
    const std::size_t slice = std::min(n - done, 64 - phase);

    // 1. voice sum. THE BOUND IS v < kMaxVoices UNCONDITIONALLY: a high-water bound would leave
    //    spare slots receiving neither render nor advanceLifeOnly, and FR-046/SC-030 are written
    //    against ALL of them. This is also where B-6's L term comes from.
    for (std::size_t v = 0; v < kMaxVoices; ++v) {
        if (!isRendering(v)) { voices_[v].advanceLifeOnly(slice); continue; }
        voices_[v].processStereoBlock(vL_, vR_, slice);
        for (std::size_t s = 0; s < slice; ++s) {
            if (!isFiniteBits(vL_[s]) || !isFiniteBits(vR_[s])) {   // FR-072, AT the accumulation point
                nonFinitePending_ |= voiceBit(v); break;            // reset DEFERRED to the next step
            }
            busL_[s] += vL_[s]; busR_[s] += vR_[s];
        }
    }
    const float g = sumGainHeld_;                          // read ONCE per control chunk
    // 2. the GLOBAL ghost tap (B-5): fed from the voice sum, BEFORE subharmonic and smear, and its
    //    wet return summed back into THE SAME bus at the SAME point. A PLAIN SUM, no second gain -
    //    setLevel's trim is already applied inside the component and multiplying by getLevel()
    //    again would square it.
    atmos_.processStereoBlock(busL_, busR_, atmosL_, atmosR_, slice);
    for (std::size_t s = 0; s < slice; ++s) { busL_[s] += atmosL_[s]; busR_[s] += atmosR_[s]; }
    // 3. the Layer-3 half of roadmap line 461's chain
    sub_.processBlock(busL_, busR_, busL_, busR_, slice);  // in place
    smear_.processBlock(busL_, busR_, slice);              // in place
    // copy out; sampleCounter_ += slice; if (sampleCounter_ % 64 == 0) runPostRenderControlStep();
}
```

`runPreRenderControlStep()`:
1. `sumGain_.advanceSamples(63); sumGainHeld_ = sumGain_.process();` — **the `- 1` is load-bearing**,
   because `OnePoleSmoother::process()` itself advances one sample;
2. non-finite recovery, at most `kResetsPerControlChunk` bits: `voices_[v].resetForRecovery()` and
   `++nonFiniteRecoveries_`. **Not** `resetForSteal()` (which preserves the fade tail, and a poisoned
   voice must not carry a poisoned tail into the next note); **not** `reset()` either (B-7's
   control-thread path — one of `FeedbackEcology::reset()`'s six fills alone exceeds the 8 889 ns
   control-chunk budget, in the same block as the non-finite event);
3. the ghost level: `ghost = max over rendering voices of getGhostRequest()`, then
   `atmos_.setLevel(ghostPeak_ * ghost)` — base 0.0, the same non-silencing maximum FR-023 uses;
4. the **held** sub-fundamental (FR-051): the lowest **sounding** voice's frequency; when no voice
   sounds the **last value is held** and `sub_.setFundamentalHz` is **not called at all** — never reset
   to a default, which would glissando the subs on every note;
5. the tidal fog fold: `smear_.setSmearAmount(clamp(smearBase_ + maxOverRenderingVoices(getTidalFogDepth()), 0, 1))`.
   **`smearBase_` is the engine's own field, written only by `setSmearAmount` and reported by
   `getSmearAmount()`; the component is written only here.**

`runPostRenderControlStep()`: deferred retirement on the **absolute** grid (`allocator_.voiceFinished(v)`
once `isFinished()`), then orphan-tail bookkeeping. Running retirement on the grid rather than "once per
block" is what makes timing partition-invariant, which is what SC-007's `getActiveVoiceCount()` clause
reads.

`processOutputStage(l, r, n)` — in place, **the caller runs it AFTER its reverb** (AR-1): guard
(`nullptr` / `n == 0` / `!prepared_` → return), then a 64-sample loop of `satL_.process` / `satR_.process`
(a **cadence choice, not a size constraint**), then `limiter_.processBlock(l, r, int(n))` **always last
over the whole block** — which is what makes FR-073's `|out| <= 1.0` a property of the stage rather than
of the caller. The doxygen carries the three-line composed-chain example verbatim.

`getLatencySamples()` returns `smear_.getLatencySamples()` and **nothing else**: the atmosphere's blur
latency is a parallel wet path summed in, not a through-path delay, and the cavern's latency belongs to
the caller. Both facts in the doxygen so a Phase 11 reader does not re-derive them.

Non-finite detection uses `detail::isFinite` (`core/db_utils.h:118`) through a private `isFiniteBits`
static — **never** `std::isnan` / `std::isinf` / `std::isfinite`, which `-ffast-math` licenses the
compiler to fold away. Forward-declare `namespace detail { struct VoragoEngineNonFiniteProbe; }` and
befriend it; **define it in the test TU** (T020), so the class definition is byte-identical in every
build and no `KRATE_DSP_*` define is needed (B-4).

**Verify.** `dsp_systems_tests.exe "VoragoEngine_*" 2>&1 | tail -5`, zero warnings.

---

# GROUP 14 — Fixtures completion

## T016 — The Vorago-typed half of `tests/test_helpers/vorago_fixtures.h`

**Files edited:** `tests/test_helpers/vorago_fixtures.h` (T005 created it). **No CMake edit.**

**Test first.** In `vorago_engine_test.cpp`,
`TEST_CASE("VoragoFixtures_FastAttackAndMakeEngine", "[systems][vorago]")`:
- `makeEngine(48000.0, VoragoEngineConfig{})` returns a non-null `std::unique_ptr<VoragoEngine>` whose
  `isPrepared()` is true;
- after `applyFastAttack(*engine)`, **every** slot reads back attack ≤ **100 ms** and every stage time
  ≤ **100 ms** (`getEnvelopeStageTimeMs(i)` through the engine's fan-out getter), and release
  **100 ms**;
- a 1 s render at polyphony 4 with one note is **non-silent** (broadband RMS > −60 dBFS) — which is the
  property SC-021a depends on and the reason the fixture exists.

**Implement.** Add, and nothing else:

- **`kFastAttackEnvelopeConfig` (FR-014a)** — a POD of six `{level, ms}` pairs plus a release, attack
  **50 ms**, every stage time **50 ms**, release **100 ms**, all ≤ FR-014a's 100 ms ceiling. Plus
  `applyFastAttack(VoragoVoice&)` and `applyFastAttack(VoragoEngine&)` (implementable without a
  `const_cast` only because T014 exposed the four envelope fan-out forwarders).
  **A comment must state that SC-004b may NOT use it** (FR-014a's second sentence): the 8 h soak renders
  the shipped slow envelope precisely because it is the property under test, and substituting the
  fixture there is exactly the forbidden "bend the shipped character to fit a test window".
- **`makeEngine(double sr, const VoragoEngineConfig&)`** → `std::unique_ptr<VoragoEngine>`. Every case
  obtains its engine this way; **a stack local is a defect** (MSVC's 1 MiB main-thread stack).
- **`renderEngine(VoragoEngine&, std::vector<float>& l, std::vector<float>& r, std::size_t samples,
  std::size_t blockSize)`** — the standard render loop, so no case rolls its own partition by accident.

**Verify.** `dsp_systems_tests.exe "VoragoFixtures_*" 2>&1 | tail -5`; both suites still build.

---

# GROUP 15 — `VoragoMacroMatrix`

## T017 — Enums, PODs, `kRows`, the six compile-time predicates, `apply` / `computeCavernTargets`

**Files edited:** `dsp/include/krate/dsp/systems/vorago_macro_matrix.h`,
`dsp/tests/unit/systems/vorago_macro_test.cpp` (delete the scaffold case).

**Test first.**

`TEST_CASE("VoragoMacro_UnpreparedAndDegenerate", "[systems][vorago]")` — SC-023's matrix arm (A-8),
no render: `apply()` against an **unprepared** engine writes nothing and does not fault;
`computeCavernTargets()` on a **default-constructed** matrix returns exactly the FR-063 defaults, **field
by field**; an out-of-range `VoragoMacro` is a no-op on `setMacro` and returns **that macro's neutral**
on `getMacro`.

`TEST_CASE("VoragoMacro_SetterContract", "[systems][vorago]")` — SC-028 (matrix). `setMacro` /
`getMacro` and `setMacros` / `getMacros` round-trip over an enumerated table including both endpoints,
every neutral and **non-finite inputs built from bit patterns through a `volatile`**; a non-finite
argument is substituted with **that macro's neutral**, then clamped to `[0, 1]`; `Count` and
out-of-range are silent no-ops.

Both cases must fail to compile before the implementation.

**Implement** (plan S7.1–S7.4). **AR-6: this is a `constexpr` row table, not "`ModulationEngine`
presets"** — the header names `ModulationEngine` nowhere.

`enum class VoragoMacro : std::uint8_t { Darkness = 0, Age, Density, Movement, Gravity, Entropy,
Pressure, Weight, Fog, Life, Depth, Mass, Count };`
`enum class VoragoMacroTargetOwner : std::uint8_t { Voice = 0, Engine, Cavern };`
`enum class VoragoMacroTarget : std::uint8_t { … }` — declared in **owner blocks**, the Cavern block's
order **is** `VoragoCavernTargets`' field order; the full roster is plan S7.1 and every enumerator is
float-valued on a shipped setter (there is **no discrete-target row**).

`struct VoragoCavernTargets` — the seven Layer-4 defaults **duplicated as literals with their source
lines** (`size 0.50f` `:253`, `darkness 0.80f` `:254`, `decaySeconds 20.0f` `:255`, `fog 0.30f` `:259`,
`damperDepth 0.35f` `:247`, `mix 1.00f` `:264`, `width 1.00f` `:263`), because a Layer 3 header may not
name a Layer 4 type. **A drifted literal is invisible to a literal-vs-literal comparison**, which is why
T022's composed TU re-checks each against the real `CavernVerb` constant.

`struct VoragoMacroValues` — the twelve neutrals as member initialisers: `gravity = 0.5f` (bipolar:
0 = air, 0.5 = neutral, 1 = stone), every other macro `0.0f`. A default-constructed matrix is therefore
already at the FR-066 identity.

`struct VoragoMacroRow { VoragoMacro macro; VoragoMacroTargetOwner owner; VoragoMacroTarget target;
float base; float amount; ModCurve curve; };` — **`base` for a Voice- or Engine-owned row is the FR-090
prepare-time value the voice or engine actually installs on that target, cited to its S8 row — NOT the
owning component's own shipped default.** Only Cavern rows use a component default. FR-017 sets
atmosphere density to 0.30 over the component's shipped 4.0, so a row basing on the component default
would have `apply()` write 4.0 back **at the neutral** and destroy the ghost configuration on the first
block.

**`kRows` is plan S7.3's table, transcribed row for row.** It is normative; do not re-derive it. Three
corrections are already applied in that table and must survive transcription:
- **`Fog → GhostPeakLevel` and `Mass → SubTrackingAmount` are `Engine`-owned**, not `Voice`-owned (their
  targets sit in the Engine block, and `everyRowOwnerIsValid` would otherwise fail at compile time);
- **`Mass → BodyBlend` is deleted** — it moved the blend toward body B while SC-008's `Mass` metric
  integrates body **A**'s mode bands, so it opposed the criterion and could fail a correct
  implementation. The metric and its ≥ 4 dB threshold stand unchanged;
- **`NoiseLevelDb`, `BreathingDepth` and `TidalDepth` each get a real, directional row** (on `Density`,
  `Movement`, `Fog` respectively), not an `amount = 0.0f` claim row.
Three rows **do** carry `amount = 0.0f` (`CavernMix`, `CavernWidth`, `BodyMix`); each carries a comment
saying it is a **claim, not a movement**, and why the target is already at its useful extreme.

`contributionOf(row)`: unipolar `amount × applyModCurve(curve, m)`; bipolar `Gravity`
`g = (m − 0.5f) × 2.0f`, `amount × applyModCurve(curve, fabs(g)) × sign(g)`.
`evaluateAll()`: `acc = base(target)` seeded once per target, then one contribution per row, and
**no `if (neutral) return;` fast path** — `applyModCurve(c, 0) == 0` for all three permitted curves, so
`acc == base` at every neutral follows from the arithmetic rather than from a shortcut a mis-signed row
could hide behind.

`apply(VoragoEngine&) const noexcept` evaluates once, then writes Voice-owned rows through
`VoragoVoice`'s forwarders for `i < engine.getPolyphony()` and Engine-owned rows through the engine's own
setters. **Cavern rows are never written by `apply()`**; they are returned by
`[[nodiscard]] VoragoCavernTargets computeCavernTargets() const noexcept`, carrying the **raw sum** —
range clamping for those seven belongs to the Layer-4 setter the caller pushes into, which is the only
place that knows the reverb's ranges.

**FR-067's idempotence is a property of the forwarders and must be stated in the header:** every Voice-
and Engine-owned forwarder `apply()` writes through is **a plain scalar store or a `setTarget`** on a
ramp the owning component already runs — **never a `snapTo`, never a smoother reset, never a re-arm of a
ramp already at its destination.** The matrix adds no smoother of its own.

**`setTargetBase` / `resetTargetBases` / `getTargetBase` are NOT provided** — Phase 12's surface; their
absence is what keeps `everyRowSharesOneBasePerTarget` a compile-time guarantee.

Six `constexpr` predicates, each with a `static_assert` at **namespace scope below the class** (a
member-specification `static_assert` cannot call a member whose body has not been parsed yet):

| Predicate | What it rules out |
|---|---|
| `kRows.size() == kNumRows` | the table and the constant drifting apart |
| `everyRowOwnerIsValid(kRows)` | a row whose `owner` disagrees with the block its `target` sits in |
| `everyCavernRowHasAPodField(kRows)` | a Cavern target with no `VoragoCavernTargets` field |
| `noRowUsesSteppedCurve(kRows)` | `ModCurve::Stepped`, which breaks SC-010's continuity bound by construction |
| `everyTargetIsClaimed(kRows)` | an enumerated target no row writes — the "fold that vanishes in the build" failure |
| `everyRowSharesOneBasePerTarget(kRows)` | two rows on one target disagreeing about `base` — catches an S8 default and a `kRows` literal drifting apart at **compile** time |

**Verify.** `dsp_systems_tests.exe "VoragoMacro_*" 2>&1 | tail -5`, zero warnings.

---

# GROUP 16 — Five disjoint new test TUs (parallel)

## T018 [P] — `vorago_voice_longrun_test.cpp`: SC-018, SC-019, SC-020 (the `[long]` voice set)

**Files edited:** `dsp/tests/unit/systems/vorago_voice_longrun_test.cpp` (delete the scaffold case).
**No production code changes.** Estimated wall clock ~20 min.

`TEST_CASE("VoragoVoice_BloomSlotAccounting", "[systems][vorago][long]")` — SC-018. A 30-minute render
accelerated per FR-086 (state `A` and the wall-clock cost in the test). **At every control step:** the
count handed to `setSpectralTarget` `<= HarmonicCloud::kMaxPartials`; `bloom().reserveBase() >= 8`;
`setSpectralTarget` never rejected — asserted **positively** via `cloud().hasSpectralTarget()` tracking
the voice's own `targetActive_` expectation.

`TEST_CASE("VoragoVoice_EcosystemRouting", "[systems][vorago][long]")` — SC-019, three clauses, all at
polyphony 1:
1. Ecosystem depth 0 **and every scheduler depth 0** → the routed destinations read **exactly** their
   configured bases over a 10-minute accelerated render, read back through `getSourceWakeAmount`,
   `getPeakWakeAmount`, `getLoopWakeAmount`, `getGhostRequest()` **and (A-9) `cloud().getMutation()` and
   `bloom().getDepth()`** — without those last two an implementation that never wired the `Partial`
   agents at all passes SC-019, SC-019a and SC-019b.
2. Schedulers at their S8 depths and ecosystem depth 1 → each destination's trace **never falls below**
   the scheduler-only trace on the same seed. **For the `Partial` family this reduces to "never below
   base"**, because the schedulers' five families are `BloomTrigger, NoiseWake, PeakWake, LoopWake,
   GhostBurst` and `BloomTrigger` writes neither `setMutation` nor `setDepth`. State that explicitly, so
   the reduction reads as a decision and not an omission.
3. Attribution: **≥ 3 distinct value changes per minute** per family attributable to the ecosystem —
   the loop runs over **all five** families including `Partial` — and setting a kind's agents dormant
   makes that family's ecosystem contribution static while the scheduler contribution continues.

`TEST_CASE("VoragoVoice_SlowEventRouting", "[systems][vorago][long]")` — SC-020. 30-minute accelerated
render: the measured inter-event interval distribution lies inside the configured range; **every**
target index in `[0, 5)` is selected at least once; each event produces a measurable change on its
destination's observable.

**Verify.** `dsp_systems_tests.exe "VoragoVoice_BloomSlotAccounting" "VoragoVoice_EcosystemRouting" "VoragoVoice_SlowEventRouting" 2>&1 | tail -5`.

---

## T019 [P] — `vorago_engine_longrun_test.cpp`: SC-004b, SC-005, SC-013b, SC-021b

**Files edited:** `dsp/tests/unit/systems/vorago_engine_longrun_test.cpp`. **No production code
changes.** This TU is the phase's dominant cost; it runs in the nightly lane only.

`TEST_CASE("VoragoEngine_OvernightSoak", "[systems][vorago][long]")` — SC-004b. **Unaccelerated**
(FR-086: a block-RMS trajectory does not survive clock scaling), full polyphony, one held note, and
**FR-014's real envelope — never `kFastAttackEnvelopeConfig`**.
`T_settle` = attack 20 s + stage 0 30 s + the longest bloom fade-in 45 s + the atmosphere capture fill
20 s = 115 s → **rounded up to 120 s and stated numerically in the test**.
- In `[0, T_settle)`: block RMS monotone non-decreasing on a **10 s moving average**.
- After: RMS within **[−60, −6] dBFS**, and the last 60 s within **±6 dB** of the 60 s window starting
  at `T_settle + 300 s`.
- Throughout: no non-finite sample; `getAllocatedBytes()` unchanged (this is the clause that carries
  FR-070's 8 h invariance, A-3); `getNonFiniteRecoveryCount() == 0`.

`TEST_CASE("VoragoEngine_OvernightEvolution", "[systems][vorago][long]")` — SC-005. The **same render**,
shared with the case above through a Catch2 fixture (R-9: it must be rendered **once**, and the
wall-clock figure recorded in `compliance.md`). **Three engine seeds**, measured after `T_settle`. Per
seed: centroid sampled every 30 s; **CV of that series ≥ 0.05**; autocorrelation at **every lag from
60 s to 30 min < 0.9**. The criterion gates the **mean across the three seeds**; each seed's own value is
printed. **Averaging the centroid *series* across seeds is forbidden and the test says so in a comment.**

`TEST_CASE("VoragoEngine_ConfigurationFuzz", "[systems][vorago][long]")` — SC-013b. **968**
configurations × 10 s, accelerated per FR-086 with `A` and the wall-clock cost stated in the test.
Assertions: `|out| <= 1.0`, no non-finite sample, `getNonFiniteRecoveryCount() == 0`.

`TEST_CASE("VoragoEngine_SampleRateSweep", "[systems][vorago][long]")` — SC-021b. 44.1 / 88.2 / 96 /
176.4 kHz, 10 s each, the same four assertions as SC-021a against the 48 kHz reference.

**Verify.** `dsp_systems_tests.exe "VoragoEngine_Overnight*" 2>&1 | tail -5` (nightly lane; a local
confirmation run is acceptable on the shorter arms first).

---

## T020 [P] — `vorago_nonfinite_test.cpp`: SC-029 and the probe definition

**Files edited:** `dsp/tests/unit/systems/vorago_nonfinite_test.cpp`. Estimated < 5 s.

**Define the probe here**, not in the header (B-4 / Q-C):

```cpp
namespace Krate { namespace DSP { namespace detail {
struct VoragoEngineNonFiniteProbe {
    // Writes `bits` into voice v's carry FIFO and marks it servable, so the ENGINE's
    // accumulation-point scan is what discovers it - exactly the path FR-072 specifies.
    // `bits` is a bit pattern (0x7FC00000 qNaN / 0x7F800000 +Inf / 0xFF800000 -Inf),
    // NEVER a std::numeric_limits value, which -ffast-math folds.
    static void poisonVoiceCarry(VoragoEngine& e, std::size_t v, std::uint32_t bits) noexcept;
    static std::uint32_t pendingMask(const VoragoEngine& e) noexcept;
};
}}}
```

`TEST_CASE("VoragoEngine_NonFiniteContainment", "[systems][vorago]")` — SC-029, **untagged**: it is a
cross-platform sentinel and stays in the per-push lane. With `k >= 2` voices sounding, inject mid-render
for **each** voice index `i` and **each** of the three bit patterns:
- the block output is **finite throughout**;
- `getNonFiniteRecoveryCount()` increments by **exactly 1**;
- the **other** voices' contribution is **bit-unchanged** against a reference render with no injection —
  the clause that catches a wrong-voice reset;
- voice `i` **resumes rendering** (non-silent within a stated number of chunks after its reset).

Injecting for every voice index is what catches a mis-indexed reset (R-11): every other criterion
asserts the counter is **zero**, so a dead or mis-indexed branch would otherwise pass the whole suite.

**Verify.** `dsp_systems_tests.exe "VoragoEngine_NonFiniteContainment" 2>&1 | tail -5`.

---

## T021 [P] — `vorago_perf_test.cpp`: SC-001a, SC-002, SC-003, `VoragoVoice_ClearingPathCost`

**Files edited:** `dsp/tests/unit/systems/vorago_perf_test.cpp`. **All cases `[.perf]`**; they run in
T031, alone. Estimated ~11 min. **SC-001b is NOT written in this task** — FR-083 forbids checking in a
baseline before the Q-A ruling exists as a spec amendment.

Measurement basis, inherited verbatim from `cavern_verb_perf_test.cpp:105-125`:
```cpp
constexpr double kBlockBudgetNs   = (512.0 / 48000.0) * 1.0e9;   // 10 666 666.67
constexpr double kRegressionFactor = 1.5;
constexpr double kReferenceNs     = kBlockBudgetNs * 0.30;       // 3 200 000.0  (roadmap line 470)
constexpr double kMaxAdmissibleNs = kReferenceNs / kRegressionFactor;  // 2 133 333.3
```
Best-of-25 × 500 blocks after 400 warm-up blocks, P-core-pinned via `node tools/run-cpu-tests.js`.

`TEST_CASE("VoragoEngine_CpuSurvey", "[systems][vorago][.perf]")` — SC-001a. Polyphony sweep
**{1, 2, 4, 6, 8}** at the S8 defaults with macros at neutral. The `CavernVerb` figure is taken from
`specs/vorago-phase9-cavern-space/compliance.md` and **added arithmetically** rather than instantiated
(this TU may not name a Layer 4 type); T022 cross-checks the sum once. Prints ns/block, % of one core,
the SC-002 breakdown, **the measured `L`** (read from SC-002's standalone `advanceLifeOnly` arm), and
FR-081's ladder recomputed from measured `V`, `L` and `G` against the corrected solve
**`N·V + (kMaxVoices − N)·L + G <= budget`**. `kMaxVoices` is a **compile-time constant and cannot be
swept at run time**, so its cost is evaluated arithmetically and the ladder printed for
`kMaxVoices ∈ {4, 6, 8}` at every swept polyphony — **that table is the artefact Q-A is ruled from.**
Assertions limited to finite-and-positive; everything else `WARN`. **Gates nothing.**

`TEST_CASE("VoragoVoice_StageCostProbe", "[systems][vorago][.perf]")` — SC-002. Each stage measured
**standalone in the same TU**: cloud, noise + decorrelation, resonance, ecology, bloom, ecosystem,
body A, body B, envelope/blend for the **voice**; voice sum, **atmosphere**, subharmonic, smear, output
stage for the **engine** (A-6: `atmosphere` is in the engine column, the spec's list predates OQ-1 (b)).
**Plus one arm that is not a stage: `VoragoVoice::advanceLifeOnly(512)` standalone — that is B-6's `L`,
and what SC-001a's ladder consumes.** Prints each figure's share of the directly measured whole;
assertions finite-and-positive only.

`TEST_CASE("VoragoVoice_CompositionOverhead", "[systems][vorago][.perf]")` — SC-003.
`measured(VoragoVoice) <= 1.15 × sum(standalone sub-components)`, both measured **in this TU in the same
run** so the machine state is shared.

`TEST_CASE("VoragoVoice_ClearingPathCost", "[systems][vorago][.perf]")` — B-7's cost half. Best-of-25,
P-core-pinned, at **48 kHz and 192 kHz**, printing all four figures **before any `REQUIRE`**: `reset()`,
`resetForRecovery()`, `resetForSteal()`, `silence()`. Asserts:
(a) each of the three RT-safe paths is **≥ 10 × cheaper than `reset()`** at both rates — the non-vacuous
clause, and the one that regresses the day someone puts `ecology_.reset()` back on the steal path;
(b) a `silence()` + `resetForSteal()` pair (what one steal actually costs) fits inside **one 64-sample
control chunk at the measured rate** — **1 333 333 ns at 48 kHz, 333 333 ns at 192 kHz**.
The figure from (b) is what `kResetsPerControlChunk`'s rationale is sized against; record it in
`compliance.md`.

**Verify.** Deferred to T031 (perf runs alone). Confirm here only that the TU **builds** with zero
warnings and that `dsp_systems_tests.exe --list-tests | grep Vorago` shows the four cases.

---

## T022 [P] — `vorago_composed_chain_test.cpp`: the Layer-4 seam (`dsp_effects_tests`)

**Files edited:** `dsp/tests/unit/effects/vorago_composed_chain_test.cpp`. **This is the only Phase 10
TU that names a Layer 4 type**, and it lives in `dsp_effects_tests` so it compiles under that target's
`KRATE_DSP_AETHER_TEST_HOOKS` consistently with every other TU in that executable (FR-084a / OQ3).

The composed chain, every case:
```cpp
engine->processStereoBlock(l, r, n);
cavern.processStereoBlock(l, r, l, r, n);   // Layer 4, owned by the CALLER (AR-1)
engine->processOutputStage(l, r, n);
```

`TEST_CASE("VoragoComposed_OutputIsBounded", "[effects][vorago]")` — FR-073. The full chain at **every
macro extreme** and **every polyphony in `[1, kMaxVoices]`**: `|out| <= 1.0` on **every** sample. This is
the only place FR-073 is asserted on the real chain, which is what the seam exists for.

`TEST_CASE("VoragoComposed_CavernDefaultsMatch", "[effects][vorago]")` — SC-009 clause 1's Cavern half.
Each of the seven `VoragoCavernTargets` literals is compared against the **real** `CavernVerb` constant
(`kDefaultSize`, `kDefaultDarkness`, `kDefaultDecaySeconds`, `kDefaultFog`, `kDefaultDamperDepth`,
`kDefaultMix`, `kDefaultWidth`). A drifted duplicate is invisible to a literal-vs-literal comparison, so
this case is the only thing standing between the POD and silent drift.

`TEST_CASE("VoragoComposed_DepthMacroAxis", "[effects][vorago][long]")` — SC-008's `Depth` row, as
amended (A-1). Five sweep points {0, 0.25, 0.5, 0.75, 1} × three engine seeds, 60 s renders with
`kFastAttackEnvelopeConfig`, every other macro at its neutral, `computeCavernTargets()` pushed into the
`CavernVerb` each control chunk. Metric: **the dB ratio of the composed render's RMS to the RMS of the
same render with `CavernVerb::setMix(0)`**, both over `[10 s, 60 s]` stated in samples. Spearman rho
**≥ 0.9** and positive; endpoint difference **≥ 6 dB**. If the measured arm misses, the lever is
`CavernSize`'s / `CavernDecaySeconds`' `amount` column (explicitly implementation tuning) or a
stop-and-surface — **never the threshold**.

Optional, and worth it: cross-check SC-001a's arithmetic `CavernVerb` addition once, by measuring the
composed chain's cost here and comparing against `engine + compliance.md`'s figure.

**Verify.**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_effects_tests
build/windows-x64-release/bin/Release/dsp_effects_tests.exe "VoragoComposed_*" "~[long]" 2>&1 | tail -5
```

---

# GROUP 17 — Engine TU, part 1

## T023 — Determinism, steal ramp, allocation, slot seeds, life parity

**Files edited:** `dsp/tests/unit/systems/vorago_engine_test.cpp`. **No production code changes** unless
a case exposes a defect — in which case fix the defect, never the assertion.

`TEST_CASE("VoragoEngine_DeterminismHarness", "[systems][vorago]")` — SC-006, **untagged** (a
cross-platform sentinel). Two engines, same seed / config / note sequence, 60 s: `compareFingerprints`
**within tolerance** (`kMetricTolerance = 2.5e-4`, `kSampleTolerance = 5.0e-4`). Two engines differing
**only** in seed: `worstMetricRelativeError > 100 × kMetricTolerance`. **No bit-exact float golden is
stored anywhere** (FR-074; `tools/lint-float-bit-goldens.js` enforces it).

`TEST_CASE("VoragoEngine_StealRamp", "[systems][vorago]")` — SC-011. Saturate the pool, steal mid-chunk:
no sample-to-sample delta above **1.5 ×** the pre-steal maximum inside the steal window, and the stolen
voice is silent within `kSilenceRampMs` — **read through `detail::VoragoVoiceSilenceRampProbe`
(`fadeRemaining_`, `silenceRampSamples_`) so the ramp is observed, not inferred.** Define that probe in
this TU (the same B-4 shape as T020's). `getLastStolenVoiceIndex()` identifies the victim.

`TEST_CASE("VoragoEngine_NoAllocationAfterPrepare", "[systems][vorago]")` — SC-014. `AllocationScope`
around a **10-minute-equivalent accelerated** render that exercises every wake/sleep edge, every bloom
spawn/retire, every steal and every macro extreme: **zero** allocations, and `getAllocatedBytes()`
identical before and after. Acceleration is legal here — allocation accounting is an
event-and-accounting property (FR-086). Include `<allocation_detector.h>` **only**; never
`<allocation_operator_overrides.h>` (the image's single owner is
`unit/systems/selectable_oscillator_test.cpp:388`). **Add a clause-0 self-check first**: a deliberate
`std::vector<float> v(1024)` inside an `AllocationScope` **must be counted**, or the criterion is vacuous.

`TEST_CASE("VoragoEngine_SlotSeedReproducibility", "[systems][vorago]")` — SC-026, untagged.
(a) note *n* on slot *s*, let it retire, play it again → **within fingerprint tolerance**;
(b) force a steal of slot *s*, then play note *n* on it → same;
(c) note *n* on slot *s* and on slot *s′* simultaneously → `worstMetricRelativeError > 100 × kMetricTolerance`.

`TEST_CASE("VoragoEngine_AdvanceLifeOnlyParity", "[systems][vorago]")` — SC-030. Two slots from the same
slot-seed derivation, one rendering and one only `advanceLifeOnly`-advanced over the **same sample
count** across a partitioned schedule: **exactly equal** `ecosystem().getControlStepCount()` and equal
scheduler event counts; then `noteOn` the idle one and compare its first-chunk agent outputs against the
rendering one **within fingerprint tolerance** — it must not start from a cold ecosystem.

**Verify.** `dsp_systems_tests.exe "VoragoEngine_DeterminismHarness" "VoragoEngine_StealRamp" "VoragoEngine_NoAllocationAfterPrepare" "VoragoEngine_SlotSeedReproducibility" "VoragoEngine_AdvanceLifeOnlyParity" 2>&1 | tail -5`.

---

# GROUP 18 — Engine TU, part 2

## T024 — The per-push sentinels and the setter contracts

**Files edited:** `dsp/tests/unit/systems/vorago_engine_test.cpp`. **All four cases untagged** — they
are the cross-platform sentinels, and a `[long]`-only boundedness case surfaces its Linux/macOS failure
a day late, which is exactly the failure mode the project rule names. **The build may not re-merge a
sentinel with its `[long]` partner.**

`TEST_CASE("VoragoEngine_SoakSentinel", "[systems][vorago]")` — SC-004a, ~15 s. A **60 s** full-polyphony
render, one held note, 512-sample blocks: every sample finite (**bit-pattern test**), `|out| <= 1.0`
after `processOutputStage`, `getNonFiniteRecoveryCount() == 0`, `getAllocatedBytes()` identical to the
value read immediately after `prepare()`.

`TEST_CASE("VoragoEngine_ConfigurationFuzzSentinel", "[systems][vorago]")` — SC-013a, ~25 s. **32** seeded
random configurations (every macro, every exposed setter, every polyphony in `[1, kMaxVoices]`) × **2 s**,
**unaccelerated**: `|out| <= 1.0`, no non-finite sample, `getNonFiniteRecoveryCount() == 0`.

`TEST_CASE("VoragoEngine_SampleRateSentinel", "[systems][vorago]")` — SC-021a. 48 kHz and 192 kHz,
**`kFastAttackEnvelopeConfig`**, 1 s full-poly render: non-silent, finite, bounded, and 192 kHz's
broadband RMS within **±3 dB** of 48 kHz's.

`TEST_CASE("VoragoEngine_GhostConfiguration", "[systems][vorago]")` — SC-027.
1. Read back on `engine->atmosphere()` **immediately after `prepare()`**, **exact** comparisons:
   `getDensity() == 0.30f`, `getGrainSeconds() == 12.0f`, `getPitchSemitones() == -12.0f`,
   `getPositionSpread() == 0.90f`, `getBlur() == 0.85f`, `getDecorrelation() == 0.85f`.
2. Event gating: polyphony 1, 10-minute accelerated render with the ghost-destined scheduler live →
   `getLevel()` shows **≥ 6 burst edges** (0.0 → ≥ half `kGhostBurstPeak` → back); with that scheduler's
   depth at 0 → **exactly 0** edges and `getLevel()` holds its base of 0.0.

`TEST_CASE("VoragoEngine_SetterContract", "[systems][vorago]")` and
`TEST_CASE("VoragoVoice_SetterContract", "[systems][vorago]")` (the latter appended to
`vorago_voice_test.cpp`) — SC-028. For **every** public float setter on both classes: a **non-finite
argument (bit pattern through a `volatile`)** leaves the previous value standing; an out-of-range
argument is clamped and the getter reports the clamp; an out-of-range **index** is a silent no-op that
writes nothing.

**Verify.** `dsp_systems_tests.exe "VoragoEngine_SoakSentinel" "VoragoEngine_ConfigurationFuzzSentinel" "VoragoEngine_SampleRateSentinel" "VoragoEngine_GhostConfiguration" "*SetterContract" 2>&1 | tail -5`.

---

# GROUP 19 — Macro TU

## T025 — SC-008 sweeps, SC-009 neutrality, SC-010 continuity, FR-067 idempotence

**Files edited:** `dsp/tests/unit/systems/vorago_macro_test.cpp`.

`TEST_CASE("VoragoMacro_SweepAxes", "[systems][vorago][long]")` — SC-008, ~45 min. **Eleven** macros here
(`Depth` lives in T022's composed TU). Fixture: five points {0, 0.25, 0.5, 0.75, 1} × **three engine
seeds**, each a **60 s** render using **`kFastAttackEnvelopeConfig`**, every other macro at its FR-061
neutral; `Gravity`'s five points straddle its 0.5 neutral. All metrics measured on the **engine's own
output** over `[10 s, 60 s]` **stated in samples**.

Per macro: **Spearman rank correlation** of the primary metric against macro value, computed per seed
and **averaged over the three**, **≥ 0.9 in magnitude and of the documented sign**, **and** the endpoint
difference meets its threshold:

| Macro | Primary metric | Direction | Endpoint threshold |
|---|---|---|---|
| Darkness | spectral centroid | lower | ≥ 20 % |
| Age | HF band energy (> 4 kHz) | lower | ≥ 3 dB |
| Density | active partial count + awake noise-source count, summed | higher | ≥ 50 % |
| Movement | per-band energy total variation | higher | ≥ 20 % |
| Gravity | mean \|log2(partial ratio) − nearest integer\| | lower | ≥ 30 % |
| Entropy | spectral flatness | higher | ≥ 25 % |
| Pressure | crest factor | lower | ≥ 3 dB |
| Weight | energy below 80 Hz | higher | ≥ 6 dB |
| Fog | per-bin magnitude flux | lower | ≥ 20 % |
| Life | wake/sleep edges per minute over the five routed families | higher | ≥ 2 × |
| Mass | energy within ±1 semitone of **body A's** first eight mode frequencies, relative to broadband | higher | ≥ 4 dB |

**Four folded-concept clauses** on the same fixture: **Age** — `decaySeconds` from
`computeCavernTargets()` shorter at 1 than at 0, and `bodyA().getDamping()` higher; **Depth** —
`decaySeconds` longer and `CavernSize` larger (assert here on the returned POD; the render half is
T022's); **Entropy** — `cloud().getMutation()` and the life-modulator depths higher at 1; **Fog** — the
distance-filtering target reads more filtered at 1.

**Non-silence clause, asserted nowhere else:** the render with **every macro at 0 simultaneously**
(`Gravity` at 0 — its air extreme, **not** its neutral) has broadband RMS **> −60 dBFS** over
`[10 s, 60 s]`. **A macro set of all zeros is not a mute.** Without this clause an all-zeros mute passes
every criterion in the spec.

`TEST_CASE("VoragoMacro_NeutralIsIdentity", "[systems][vorago]")` — SC-009, untagged, ~10 s:
1. **Per-row base check** — for **every** `Voice`/`Engine` row, `row.base` equals the value read back
   from that target's getter on a voice/engine **immediately after `prepare()`**, **exactly**. This is
   what catches an S8 default and a `kRows` literal drifting apart. For `Cavern` rows, `row.base` equals
   the corresponding `VoragoCavernTargets` field default (T022 re-checks those against the real
   `CavernVerb` constants).
2. **Arithmetic inertness** — at every neutral, `apply()` leaves every writable target at exactly
   `base` and `computeCavernTargets()` returns exactly the defaults, asserted **per target**, so a
   mis-signed row cannot hide behind a cancellation.
3. **Render identity** — a 10 s render with the matrix never applied vs applied at the neutral →
   **bit-identical**.

`TEST_CASE("VoragoMacro_NoZipper", "[systems][vorago][long]")` — SC-010. Each macro automated 0 → 1 over
5 s: `maxDeltaInWindow(20 ms)` on the ramp **≤ 1.5 ×** the same statistic measured **64 ms clear of the
ramp**.

`TEST_CASE("VoragoMacro_ApplyIsIdempotent", "[systems][vorago]")` — FR-067, untagged, ~6 s. Set all
twelve macros to a stated **non-neutral** vector (each at 0.75, `Gravity` at 0.85), then render 10 s
twice from identical prepared engines: once with `apply()` called **once** before the render, once with
`apply()` called at **every** 64-sample control chunk. The two renders must be **bit-identical**
(`std::memcmp`). SC-010 cannot see this — it measures a zipper *during* a ramp, where a forwarder that
re-arms a ramp on every unchanged write looks identical to a correct one — and SC-009 clause 3 covers
only the neutral. A `setBodyBlend` / `setSmearAmount` / `setNoiseLevelDb` forwarder that called `snapTo`
instead of `setTarget` would otherwise pass the entire suite while making the instrument step on **every
block** at any non-neutral macro setting.

**Verify.** `dsp_systems_tests.exe "VoragoMacro_*" "~[long]" 2>&1 | tail -5`, then the `[long]` arm
separately.

---

# GROUP 20 — Dark-materials spectral separation

## T026 — SC-015: the six new materials are darker than every shipped one, and distinct from each other

**Files edited:** `dsp/tests/unit/systems/vorago_dark_materials_test.cpp`.

`TEST_CASE("ContinuousBody_DarkMaterialsSpectral", "[systems][vorago][long]")` — SC-015. One
`ContinuousBody` per material, **identical excitation** (a fixed pink-noise burst from a fixed seed) and
**identical resonance/damping**, a 20 s steady-state window:
- each new material's spectral centroid is **below the minimum** of the five shipped;
- each new material's centroid is **≥ 5 % from every other new one**.

**Print all eleven centroids before any `REQUIRE` fires** — the per-file discipline
`continuous_body_perf_test.cpp:875-880` sets. Timbre only; CPU is SC-025's job (T009/T031).

**If a material misses**, the lever is that material's **α, then its `b1` damping, then its mode
count**, in that order — re-voice and re-measure. **Never relax SC-015.** Terminal step: drop the
material from the six and surface the drop to the user (FR-038b).

**Verify.** `dsp_systems_tests.exe "ContinuousBody_DarkMaterialsSpectral" 2>&1 | tail -5`.

---

# GROUP 21 — Integration

## T027 — Registration audit (single task)

**Files checked (edit only if something is missing):** `dsp/tests/CMakeLists.txt`,
`dsp/lint_all_headers.cpp`, `dsp/CMakeLists.txt`, `tests/test_helpers/`.

1. All **eight** systems TUs are in the enumerated `add_executable(dsp_systems_tests …)` list, and the
   **one** composed TU is in `add_executable(dsp_effects_tests …)`. A TU that is not listed silently
   drops out of the build and its cases never run.
2. **No `target_compile_definitions` line was added** for Phase 10 (B-4 / Q-C) and `dsp_lint_stub` is
   untouched.
3. All three new headers are in `dsp/lint_all_headers.cpp`'s Layer 3 block and in
   `KRATE_DSP_SYSTEMS_HEADERS`.
4. `tests/test_helpers/vorago_fixtures.h` needed **no** CMake edit (INTERFACE target), and **no** new TU
   includes `<allocation_operator_overrides.h>`:
   `grep -rn "allocation_operator_overrides" dsp/tests/unit/systems/vorago_* dsp/tests/unit/effects/vorago_* tests/test_helpers/vorago_fixtures.h`
   must print nothing.
5. `node tools/gen-specs-index.js` regenerates `specs/_architecture_/` (FR-084).

---

## T028 — Full-suite run

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests dsp_effects_tests
# the per-push lane (excludes the timing-sensitive and [long] sets)
build/windows-x64-release/bin/Release/dsp_systems_tests.exe \
    "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_effects_tests.exe \
    "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" 2>&1 | tail -5
# the [long] set, explicitly (per-push CI excludes it; it runs nightly on all three OS legs)
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[long]" 2>&1 | tee /tmp/vorago-long.log | tail -5
build/windows-x64-release/bin/Release/dsp_effects_tests.exe "[long]" 2>&1 | tail -5
```
All green, **zero warnings** in the build log. Capture the `[long]` output to a log — never re-run a
slow suite just to see its output again.

---

## T029 — Portability and lint gates

```bash
node tools/check-portability.js
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-nonfinite-symbols.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_lint_stub
```
All must pass. **A green MSVC build proves nothing for the Linux and macOS legs.** Reminders:
- **no `std::isnan` / `std::isinf` / `std::isfinite`** anywhere in the three new headers or the nine new
  TUs — `detail::isFinite` (`core/db_utils.h:118`) in production, bit patterns through a `volatile` in
  tests;
- **no positional brace init** in any config or table row — designated initialisers everywhere (Clang
  errors on narrowing where MSVC does not);
- every float literal carries an `f` suffix (C4244); every `size_t` → `int` conversion is an explicit
  cast (C4267); every unused parameter is `[[maybe_unused]]` (C4100);
- **no committed bit-exact float golden** — `render_fingerprint.h` and measured tolerances only;
- this phase introduces **no SIMD**, so no aligned load/store is added.

Then the libstdc++ syntax pass — the one cheap gate that catches the `std::` include the MSVC STL
provides transitively and libstdc++ does not:
```bash
# WSL: the three new headers, the two appended-to headers, and all nine new TUs
g++ -std=c++20 -fsyntax-only -Idsp/include -Itests/test_helpers <each file>
```

---

## T030 — SC-016 post-change gate, the `FeedbackEcology` gate, and clang-tidy

```bash
# 1. SC-016: the untouched consumers of the extended header, in full
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ContinuousBody*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "Seraphis*"      2>&1 | tail -5
node tools/check-seraphis-green.js          # clauses 1-2, the git-diff scope checks

# clause 3 (Q-K / Q-S): paired capture, pre-change worktree at ad7481f6 vs this tree, each alone
# and P-core-pinned, alternating order; argmax from the Seraphis survey, per-material 10 % from
# ContinuousBody_CpuBudget's tables. See plan.md's SC-016 block for the exact commands.
grep -i "worst material" specs/vorago-phase10-voice-engine/artifacts/sc016-c3-paired/pair*-*.log
grep -E "^\s+(Glass|Strings|MetalPlate|Chamber|Ice) :" specs/vorago-phase10-voice-engine/artifacts/sc016-c3-paired/cbpair*-*.log

# 2. B-7's second shared-component change: FeedbackEcology's own suites, in full.
#    silenceAudio() is append-only and has no Seraphis consumer, so THIS is its gate.
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology*" 2>&1 | tail -5

# 3. clang-tidy - on Windows the .ps1, never the .sh
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```
The `diff` must print **nothing** — the survey's worst material is unchanged. clang-tidy must be clean:
fix **all** warnings; no suppression added to dodge one.

**Note:** this run shares the machine with T031's perf lane. Run each **alone**, nothing else executing,
with the machine idled between them — sustained benchmarking heats the CPU and boost clocks drop.

---

## T031 — Perf alone, then the Q-A ruling, then SC-001b

**Step 1 — measure.** Nothing else executing, after the machine has idled:
```bash
node tools/run-cpu-tests.js dsp_systems_tests 2>&1 | tee specs/vorago-phase10-voice-engine/artifacts/perf.log
```
This covers SC-001a, SC-002 (including the standalone `advanceLifeOnly` arm that **measures `L`**),
SC-003, `VoragoVoice_ClearingPathCost` and SC-025's 44-measurement table. If a case fails, confirm
nothing else was running and re-run **that suite alone** after the machine has idled before treating it
as a defect — a test that flips verdicts between runs is measuring the machine, not the code.

**Step 2 — recompute the ladder from the measurement.** Take measured `V`, `L`, `G` and solve
`N·V + (kMaxVoices − N)·L + G <= budget` at `kReferenceNs = 3 200 000` and at
`kMaxAdmissibleNs = 2 133 333`, for `kMaxVoices ∈ {4, 6, 8}` and every swept polyphony. The projection
the plan carried (`V = 474 395`, `G = 411 330`, `L ≈ 31 000`) says polyphony 4 clears the gated line
**only with L-4 applied and `kMaxVoices` at 6** — at 8 the four spare slots cost ~124 000 ns/block and
the gated ×1.15 column falls from 4.30 to **3.99**, on a break-even margin of 645 ns per spare slot.
**That is inside the projection's error bars in either direction, which is why `L` is measured.**

**Step 3 — surface Q-A to the user** with the measured table, both halves:
- shipped polyphony (**recommendation: 4** — the roadmap's own floor, line 460);
- `kMaxVoices` (**recommendation: lower it to 6** — it narrows roadmap line 460's "4–8 voices" to 4–6,
  which is a roadmap-touching choice and therefore the user's);
- the lever ladder actually needed: **L-7** (lower `kMaxVoices`) → **L-4** (per-voice counts 4 → 2,
  6 → 4, 12 → 8, all `VoragoVoiceConfig` defaults) → cheaper material voicing → **L-3** (one body, only
  with a recorded ruling — it deletes roadmap line 122's deliverable) → **L-5** (reduce polyphony,
  bounded below at 4) → **L-6 stop and surface**.
**Never** relax `kReferenceNs`, shrink the measured workload, or raise a checked-in baseline.

**Q-B** (OQ-3: reverse grains and event-triggered grain scheduling in `AtmosphereEngine`) is already
ruled (spec.md Clarifications, Q-B, 2026-09-17): FR-017's configuration ships now and roadmap Phase 10a
owns the other two. Record it as ruled in the phase report; do not surface it.

**Step 4 — only after the ruling is written back into `spec.md` as SC-001b** (FR-083).
**RULED AND APPLIED 2026-09-21 — verification only.** Q-H (2026-09-19) ruled polyphony 4,
`kMaxVoices = 6`, no voicing levers, and reformulated clause (ii); the baseline was transcribed from
the cooled re-measurement on 2026-09-21:
- `kMaxVoices` lowered 8 → 6 in `vorago_engine.h` (done at Q-A/Q-H);
- `TEST_CASE("VoragoEngine_CpuBudget", "[systems][vorago][.perf]")` is in `vorago_perf_test.cpp`:
  (i) engine + Cavern `<= kReferenceNs` (3 200 000) and (ii) engine `<= kEngineBaselineNsAtPoly4 × 1.5`,
  baseline `2 694 479 = ceil(2 566 170 × 1.05)`. The original "baseline `<= kMaxAdmissibleNs`" clause
  is not met by ruling; the TU `static_assert`s baseline = ⌈measured × 1.05⌉, baseline + Cavern
  `<= kReferenceNs`, and records the available headroom (1.141×) next to the baseline.
- **No baseline may be checked in before that amendment exists.** A baseline transcribed against an
  unruled configuration pins the wrong workload and is the one way the ladder gets skipped in practice.

---

## Compliance ledger (fill from actual output, never from memory)

When the phase reports complete:

- **every FR row** cites a `file:line` in `vorago_voice.h` / `vorago_engine.h` /
  `vorago_macro_matrix.h` / `continuous_body.h` / `feedback_ecology.h` that was **opened and read**;
- **every SC row** cites the `TEST_CASE` name **and the measured figure from the run log** — the measured
  `sizeof(VoragoVoice)`, the mono-sum deviation in dB, the eleven material centroids, the 44 material
  ns/block figures, the measured `V` / `L` / `G`, the composition-overhead ratio, the four clearing-path
  figures at both rates, the soak's settled RMS and its ±dB drift, the eleven Spearman rhos and eleven
  endpoint deltas, the burst-edge count;
- **SC-016 clause 3** quotes the worst material from **both** capture logs by path, and the
  per-material pre/post figures from `ContinuousBody_CpuBudget`'s paired tables (Q-S);
- **SC-024** records the `check-portability.js` result, the three headers' presence in
  `lint_all_headers.cpp`, and the libstdc++ syntax pass;
- **Q-A and Q-B** are recorded as ruled or as still open, with SC-001b present or explicitly absent.

A table of ✅ without those numbers is worse than an honest ❌.
