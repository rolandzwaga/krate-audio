# Tasks: Profundum Phase 8 — Bass Morph

**Spec:** `specs/profundum-phase8-bass-morph/spec.md` (Draft 2026-10-19, reviewed; Q1–Q8 ruled) · **Plan:**
`specs/profundum-phase8-bass-morph/plan.md` (S0–S12, C-1…C-16, R-1…R-14, review notes) · **Roadmap:** `specs/Profundum-roadmap.md`
Phase 8 (lines 573–607) · **Phase base:** commit `964045a4` on `synth/profundum`
**Status:** Task list (2026-10-19). The build stage has not started. Plan S1's C-1, C-2, C-4 and C-16 were **ratified 2026-10-19** (spec Clarifications, plan-stage session), as were deviations 2 and 10 below; C-9 is a
⚠ RISK; none has a recorded ruling yet (spec Clarifications holds only the specify-stage session). The tasks build every item at the plan's
reading (precondition table). No commit tasks: commits happen outside this workflow.

## Read this first (every executor)

### Precondition: plan S1 (C-1…C-16)

| Item | What the tasks build | If the ratification goes the other way | Spec row | Task(s) |
|---|---|---|---|---|
| C-1 (ratified 2026-10-19) | One field-descriptor table `detail::morph_state_io::kFieldTable` in `morph_state.h`; `Law`, `Axis`, `Kind`, `SelectionGroup`, `FieldInfo` nested in `MorphState`; `BassMorphEngine` re-exports `Law`, `Axis`, `SelectionGroup` and `kLawTable` | Only the table's location moves; every test reads it through `BassMorphEngine::kLawTable` or `detail::morph_state_io::kFieldTable` | FR-030 | T002, T003 |
| C-2 (ratified 2026-10-19) | One selection per governing-amount group (Character, ImageMode, FieldMotion, Motion, Anchor), indexed **only** through `MorphState::groupIndex(g)`; runtime main byte = 4-bit mask | A single shared main selection cannot satisfy FR-034 rule 3 (plan C-2); T004's `BassMorph_LawTable_GoverningAmount` and T005's runtime layout would be rewritten | FR-034, FR-062, SC-019(b) | T003, T004, T005 |
| C-3 | p_a's target = c_m(ramped p after this interval's step), recomputed every interval | — | FR-050, FR-062 | T003 |
| C-4 (ratified 2026-10-19; FR-031 amended) | Linear, equal-power and log laws evaluated in **double**, rounded to float once, clamped to [min, max] of the endpoints; log endpoints cached as `log2(x + o)` in double at `setEndpoint` | Float evaluation exceeds SC-019(b)'s 2-ulp bar for opposite-sign endpoints (plan C-4); a contrary ruling needs a bar ruling as well | FR-031–FR-033, SC-019(b), SC-024 | T004 |
| C-5 | `prepare` ends with `reset()` | — | FR-011 | T003 |
| C-6 | Resolution cached behind a `dirty_` flag | — | FR-015, FR-082, SC-018 | T004 |
| C-7 | `setPosition` maps every p < `kDenormalThreshold` (1e-15f, `core/db_utils.h:38`, namespace `Krate::DSP`) to +0; `curve()` returns through `detail::flushDenormal`; `restoreRuntime` rejects −0 and 0 < \|x\| < 1e-15 | — | FR-013, FR-022, FR-062 | T003, T005 |
| C-8 | `AmpEnvelopeFields` defaults = `ADSREnvelope` member defaults (10 / 50 / 0.5 / 100 ms, curves 0.7) | — | FR-004 | T002 |
| C-9 ⚠ RISK | Hidden probe `BassMorph_SumTapCoherenceProbe` measures c, θ at the character tap; the main loop runs it at the T008 checkpoint, **before any rendered gate is trusted**. A red prediction or reading → STOP AND ASK; remediation only in the showcase pair's B or the laws (spec Q7), never a relaxed bar | — | SC-008(d), SC-015 | T008 |
| C-10 | SC-004 base state (`characterAmount` 0.8, `anchor.level` 1.0 on both sides) and the direct-setter reference (p held at 0; both endpoints replaced by A′ at the switch grid sample g_sw) | — | SC-004 | T010 |
| C-11 | `anchorFoldFeed` ramped per sample across each interval in the fixture | — | FR-070 | T007 |
| C-12 | Fast engine clauses per-push; render-bound clauses in `…Rendered` / `…Render` cases | — | SC-005, SC-006, SC-024 | T004, T005, T009 |
| C-13 | A stream `bool` byte > 1 is rejected; enum and bool leaves are read as `std::uint8_t` | — | FR-006 | T002 |
| C-14 | `endpoint(e)` with an undeclared `e` returns A | — | FR-012 | T003 |
| C-15 | Drafted bars adopted: SC-008(a) ε = 0.25 dB / 0.02 oct / 2 %; SC-015 3 dB / 0.25 oct / ratio 2 / Lock − Blend ≥ 2 dB; D-9 10^(0.5/20); D-10 ≤ 0.05 % of one core = **≤ 333.3 ns per interval at 48 kHz**; D-11 crossover log; D-12 transient gains linear in dB | — | SC-004, SC-008, SC-015, SC-018 | T009–T014 |
| C-16 (ratified 2026-10-19) | SC-017's 21-point grid on `maxNonlinearPair`; the two crossing arms and the (b) window on `algorithmCrossingPair` (both governing amounts non-zero) | Dropping the arms is rejected by the plan (FR-086's live crossfade would go ungated) | SC-017, FR-086 | T002 (pair), T013 |

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **CMake registration is the first code task (T001), not the last.** Every later task is test-first and its TU must be in the build to fail;
   `dsp/tests/CMakeLists.txt` enumerates TUs, so an unregistered TU silently drops out. T001 creates compiling stubs for all eleven TUs (plan
   S11 item 1 says the same). The last group's single CMake task (T017) audits the registration. Phase 6 / 7 deviation 1 is the precedent.
2. **`dsp/CMakeLists.txt` gains the two new headers** (confirmed 2026-10-19). Plan S8.1 says "No other CMake file changes", but Phases 3, 5, 6 and 7 each listed their
   new systems headers in the systems header list (`dsp/CMakeLists.txt:206–216`, Phase 7's `bass_stereo_field.h` at `:215`). T001 appends
   `morph_state.h` and `bass_morph_engine.h` there. A build-list append, not a header change (FR-088 untouched).
3. **The light test helpers get their own header, `dsp/tests/unit/systems/bass_morph_state_helpers.h`.** Plan S5 puts `bitEqual`,
   `randomValidState`, `randomValidPair` and the named pairs in `bass_morph_test_helpers.h`, which includes the whole Phase 6 / 7 render stack.
   The engine-only TUs (T002–T006) need them before the fixture exists, and a parallel group cannot share a header another task in it edits.
   Split: `bass_morph_state_helpers.h` (T002 creates; T003 appends the `Resolved` overload of `bitEqual`; read-only afterwards) holds what
   needs only `MorphState` / `BassMorphEngine`; `bass_morph_test_helpers.h` (T007) includes it and adds the render fixture. Both are namespace
   `Krate::DSP::BassMorphTest`. File name swept 2026-10-19: 0 hits.
4. **The engine is built in three sequential tasks** (T003 lifecycle / ramp / curves / identity; T004 laws / selection machine / cache; T005
   runtime round trip / RT safety / idempotence), each test-first against the previous state. T003 ships an **identity-only resolver**
   (every field at an interior position resolves to A's value; identity at 0 / 1 and equal endpoints already exact) that T004 replaces; its
   banner line says so. It is the only temporary behaviour in the list.
5. **Every `[long]` TEST_CASE has a non-long `…Smoke` sibling** (one small cell, the same assertions), so each gate exercises the path (gates
   exclude `[long]`). Phase 6 deviation 10 and Phase 7 deviation 6 are the precedent. No bar changes; the `[long]` shards gate the full grid.
6. **The hidden probes carry `[profundum]`.** The gate filter ends in `~[.]` (`.claude/workflows/profundum-phase.js:551`, Phase 7 P-0), so
   `[profundum][bass-morph][.]` never runs inside a gate. Phase 7 deviation 4 (hidden cases without `[profundum]`) predates that fix.
7. **R-2's early CPU timing is a hidden case in the probe TU** (`BassMorph_CpuEarlyProbe`), run by the main loop at the T008 checkpoint with
   the C-9 probe, after T004 has the real resolver (timing the T003 placeholder would measure nothing). Above 333.3 ns per interval it is STOP
   AND SURFACE (R-2: escalate, never a budget change). It is not the SC-018 gate (T014 / T020 are).
8. **SC-008(a) "non-decreasing within ε" is checked against the running maximum** (D(p_j) ≥ max_{i<j} D(p_i) − ε), so a slow drift cannot
   accumulate across 21 points. The stricter of the two readings; it relaxes nothing.
9. **`MorphChainTaps` gains `std::array<std::size_t, 7> ampEnvSetterCalls`** (one counter per `ampEnv` leaf), the "probe counter" plan S7.3's
   `BassMorphHelpers_ChangeOnlyEnvelopeSetters` needs; no wrapper class.
10. **(Ratified 2026-10-19.) SC-004 transition end for `field.mode` and the `field.motion*` selectors = g_sw + one period + 334 intervals (10 688 samples at any
    fs, the full-range pan slew: `kMaxPanStepPerInterval` = 0.003 per interval, `bass_stereo_field.h:77, 90, 92`; Phase 7 C-15's "334th
    query").** Plan S7.4 writes "+ 10 ms (image mode)". Phase 7's G7-1 diagnosis (Phase 7 `tasks.md`, "Defects found") measured
    SOLID ↔ SPREAD as a ~220 ms vector slew, so a 10 ms window would end the window mid-transition. The (a) bar compares transition and
    reference over the **same** window, so the longer window is not a relaxation. Plan S7.4's "read from `field` slew completion" for the
    motion cases is realised the same way.
11. **Public `static float curve(AnchorMorphMode m, float p) noexcept`** on `BassMorphEngine` (plan S4.4 names the function; S4.1's API list
    omits it). `BassMorph_SubThresholdLanding` and `BassMorph_AnchorCurves` assert against it.
12. **`BassMorph_Idempotent` (SC-006(a)) moves from T004 to T005**, because its assertion includes `serializeRuntime` bytes, which T005
    builds.

### File map

| File | Kind | Layer / target | Created / edited by |
|---|---|---|---|
| `dsp/include/krate/dsp/systems/morph_state.h` | new header: `MorphState`, `kFieldTable`, `forEachMorphLeaf`, validation, serialisation (plan S3). Includes L0 `core/db_utils.h`, `core/note_value.h`; L1 `primitives/envelope_utils.h`, `primitives/lfo.h`; L2 `processors/spectral_shape_recipe.h`, `processors/drop_envelope.h`, `processors/transient_shaper.h`; L3 peers `systems/profundum_core.h`, `systems/bass_character_stage.h`, `systems/bass_stereo_field.h`, `systems/anchor_oscillator.h`, `systems/mass_law.h` (aggregates, enums, range constants only) | L3 | T001 stub; T002 |
| `dsp/include/krate/dsp/systems/bass_morph_engine.h` | new header: `BassMorphEngine` (plan S4). Includes `systems/morph_state.h`, `core/db_utils.h`, `primitives/smoother.h` | L3 | T001 stub; T003; T004; T005 |
| `dsp/tests/unit/systems/bass_morph_state_helpers.h` | new test header (`bitEqual`, `serializedBytes`, `differingLeaves`, `ulpOf`, `randomValidState`, `randomValidPair`, named pairs) | — | T002; T003 (appends the `Resolved` overload) |
| `dsp/tests/unit/systems/bass_morph_test_helpers.h` | new test header (`MorphEvent`, `MorphChainConfig`, `MorphChainTaps`, `renderMorphChain`, `renderMorphPoly`, descriptor readers; plan S5) | — | T007 (read-only after) |
| `dsp/tests/unit/systems/morph_state_test.cpp` | new TU | `dsp_systems_tests` | T001 stub; T002 |
| `dsp/tests/unit/systems/bass_morph_engine_test.cpp` | new TU | `dsp_systems_tests` | T001 stub; T003; T004; T005 (each appends) |
| `dsp/tests/unit/systems/bass_morph_nonfinite_test.cpp` | new TU, **`-fno-fast-math`** | `dsp_systems_tests` | T001 stub; T006 |
| `dsp/tests/unit/systems/bass_morph_helpers_test.cpp` | new TU (fixture self-tests) | `dsp_systems_tests` | T001 stub; T007 |
| `dsp/tests/unit/systems/bass_morph_probe_test.cpp` | new TU, hidden cases only | `dsp_systems_tests` | T001 stub; T008 |
| `dsp/tests/unit/systems/bass_morph_chain_test.cpp` | new TU (per-push cases + two `[long]` with smokes) | `dsp_systems_tests` | T001 stub; T009 |
| `dsp/tests/unit/systems/bass_morph_click_test.cpp` | new TU (shards `[long]`) | `dsp_systems_tests` | T001 stub; T010 |
| `dsp/tests/unit/systems/bass_morph_trajectory_test.cpp` | new TU (shards `[long]`) | `dsp_systems_tests` | T001 stub; T011 |
| `dsp/tests/unit/systems/bass_morph_monosum_test.cpp` | new TU (shards `[long]`) | `dsp_systems_tests` | T001 stub; T012 |
| `dsp/tests/unit/systems/bass_morph_aliasing_test.cpp` | new TU (shards `[long]`) | `dsp_systems_tests` | T001 stub; T013 |
| `dsp/tests/unit/systems/bass_morph_perf_test.cpp` | new TU, `[performance][.perf]`, **not** `-fno-fast-math` | `dsp_systems_tests` | T001 stub; T014 |
| `dsp/CMakeLists.txt`, `dsp/tests/CMakeLists.txt` | existing | — | T001 only (T017 audits) |
| `specs/profundum-phase8-bass-morph/plan.md` | existing | — | T005 (the S4.10 Anchor punch / ghost row only) |
| `specs/profundum-phase8-bass-morph/evidence/` | TXT / CSV written by tests; `close/` lane scripts; `run-log/<Tnnn>.txt` | — | tests; every task's run-log |

**Untouchable (zero diff lines vs `964045a4`, FR-088):** every existing file under `dsp/include/`, `dsp/src/`, `plugins/`, `tests/test_helpers/`,
and every existing test file and helper — in particular `processors/spectral_shape_recipe.h`, `systems/profundum_core.h`,
`processors/harmonic_fold.h`, `systems/shape_morph.h`, `systems/bass_character_stage.h`, `processors/drop_envelope.h`,
`primitives/adsr_envelope.h`, `systems/bass_stereo_field.h`, `systems/mass_law.h`, `systems/mass_dynamics.h`, `processors/transient_shaper.h`,
`systems/anchor_oscillator.h`, `primitives/smoother.h`, `primitives/lfo.h`, `core/db_utils.h`, and the helpers `mass_dynamics_test_helpers.h`,
`bass_stereo_field_test_helpers.h`, `profundum_core_test_helpers.h`, `anchor_oscillator_test_helpers.h`, `shape_character_test_helpers.h`,
`../processors/shape_character_metric_helpers.h`, `../processors/spectral_shape_recipe_test_helpers.h`. If a reused header looks unusable,
STOP AND SURFACE: FR-088 allows only an append-only change gated by every consumer suite, and that is the user's decision.

### Verified facts these tasks depend on (read 2026-10-19 in this session; plan S0 holds the full ledger)

Paths relative to `dsp/include/krate/dsp/` unless stated. Re-derive every line anchor from the live file before editing.

- **CMake.** `dsp/tests/CMakeLists.txt`: Phase 7's systems comment block `:743–753`, its TUs `:754–762` (`unit/systems/bass_stereo_field_perf_test.cpp`
  last, `:762`), closed by `)` at `:763`. `-fno-fast-math` block: `:1258` (`# Profundum Phase 7: injects NaN/Inf bit patterns …`), `:1259`
  (`unit/systems/bass_stereo_field_nonfinite_test.cpp`), then `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` at `:1260`.
  `KRATE_DSP_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}"` is a compile definition (`:853`). `dsp/CMakeLists.txt:214–216`:
  `# Profundum Phase 7 (specs/profundum-phase7-stereo-field)`, `include/krate/dsp/systems/bass_stereo_field.h`, `)`.
- **Gate filter:** `PHASE_TAG = '[profundum]'`, `PHASE_FILTER = '[profundum]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'`
  (`.claude/workflows/profundum-phase.js:550–551`).
- **Range constants:** `DropEnvelope::kDefaultAmountSemitones = 0`, `kDefaultTimeMs = 100`, `kDefaultShape = 0`, `kDefaultRebound = 0`
  (`processors/drop_envelope.h:36–39`); `trigger(bool snap)` `:103`; `process()` `:124`; `currentOffset()` `:148`.
  `AnchorOscillator::kMinPunchBoostDb / kMaxPunchBoostDb = 0 / 12`, `kMinPunchTimeMs / kMaxPunchTimeMs = 5 / 250`,
  `kMinGhostTimeMs / kMaxGhostTimeMs = 1 / 100` (`systems/anchor_oscillator.h:84–86`). `BassStereoField::kMinCrossoverHz / kMaxCrossoverHz /
  kDefaultCrossoverHz = 60 / 250 / 120` (`systems/bass_stereo_field.h:70–72`). `TransientShaper::kMinGainDb / kMaxGainDb = −12 / 12`,
  `kGainSmoothMs = 10` (`processors/transient_shaper.h:78–79, 93`). Named recipes `kDefaultControls`, `kSineAnchor`, `kTriangleAnchor`,
  `kSawAnchor`, `kHeavy`, `kHollow`, `kGrowl` (`processors/spectral_shape_recipe.h:82–84`, defined `inline constexpr` from `:227`).
  Everything else as in plan S0.
- **`MassLaw`** (`systems/mass_law.h`): `kRows` `:104`, the AnchorLevel row `:110` (lo 0, hi 1, amountDown 0.5, amountUp 0.5, Linear), target
  order Depth, Body, TransientDuration, FoldAmount, CompressionAmount, AnchorLevel, OutputTrim (`:68`); `static float evaluateRow(const Row&,
  float mass, float base)` `:132`; `currentTargets()` `:185`. Tests locate the row by `row.target == MassLaw::Target::AnchorLevel`, never by a
  literal index.
- **`AnchorOscillator`** controls: `setControls` writes `pending_` (`:224–245`); `latchControls()` copies `active_ = pending_`, re-targets the
  level smoother and requests the waveform (`:436–446`); the punch / ghost times are read only where a gain program is built (`:1019–1048`).
  T005 completes the plan S4.10 row from these leads.
- **Test helpers:** `MassDynamicsTest::kGrid` `:81`, `kSettleSeconds = 1.0` `:87`, `descriptorSeconds(fs)` `:90`, `renderLength` `:95`,
  `gridDriver(total, hostBlock, fn(start, len, atGrid))` `:103`, `ChainConfig` `:173`, `coreControlsFor` `:275`, `unbiasedH1Amplitude` `:288`,
  `processChainTail` `:302`, `makeChainDynamics` `:348`, `prepareChainLaw` `:364`, `SubBand` `:545`, `subBand(y, n, fs, f0)` `:641`
  (`dsp/tests/unit/systems/mass_dynamics_test_helpers.h`). `StereoFieldTest::kFloorDb = −120` `:90`, `kSubBandQBarDb = 1.0` `:95`,
  `kSubBandRhoBar = 0.99` `:97`, `bitEqual(vector, vector)` `:102`, `FieldChainConfig` `:114`, `FieldChainTaps` `:136`, `makeField` `:160`,
  `advanceFieldAt` `:177`, `allocateFieldTaps` `:188`, `processStageAndOutput` `:204`, `processFieldTail(taps, cfg, md, dcL, dcR, limiter, stage,
  start, len, total)` `:232`, `renderFieldChain` `:267–425`, `renderFieldPoly` `:463`, `bandReading` `:736`, `evaluateMonoSumCell` `:998`,
  `monoSumCellPasses` `:1051`, `wideAliasedDbfs(y, fs, f0, alg)` `:1120` (`bass_stereo_field_test_helpers.h`). `ShapeCharacterTest::ClickCount
  { counted, … }` `:44`, `maxDeltaAround(x, n, i, N)` `:53`, `differentialClicks(transition, refStart, refEnd, n, fs, f0)` `:76`
  (`dsp/tests/unit/processors/shape_character_metric_helpers.h`). `ProfundumTest::renderedHarmonicPowers(x, fs, f0, N)` `:235`,
  `describeRendered` `:251` (`profundum_core_test_helpers.h`); `ProfundumTest::shapeOf(c, N)` (`../processors/spectral_shape_recipe_test_helpers.h:168`);
  `anchorMidiHz(int)` (`anchor_oscillator_test_helpers.h:88`); `kWeightedLevelDb` (`tests/test_helpers/k_weighting.h:111`);
  `TestUtils::kLowFrequencyFftSize = 262144` (`tests/test_helpers/low_frequency_metrics.h:64`); `TestHelpers::AllocationScope` with
  `TestHelpers::AllocationDetector::instance().getAllocationCount()` read inside the scope (`tests/test_helpers/allocation_detector.h:70, 111`;
  the pattern of `dsp/tests/unit/systems/bass_stereo_field_test.cpp:3061–3089`).
- **Component APIs the fixture calls:** `BassStereoField::computePanVector(VoiceState&, float f0Hz, std::array<float, kMaxPartials>&, …)` `:349`,
  `wideOffsets(Algorithm)` `:413`, `processBlock` `:437`; `BassCharacterStage::setChannelOffsets(float, float)` `:273`, `setFundamentalHz` `:287`,
  `processBlock` `:334`; `AnchorOscillator::setCoreH1Amplitude(float)` `:262`, `noteOn(float, bool)` `:276`, `processBlock(left, right, n,
  f0PerSample = nullptr, …)` `:285`.

### Commands (main loop and gates; executors do not build or run the test tree)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests > <log> 2>&1; tail -5 <log>
B=build/windows-x64-release/bin/Release
$B/dsp_systems_tests.exe "[profundum]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]" 2>&1 | tail -5   # the gate
$B/dsp_systems_tests.exe "[bass-morph]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]" 2>&1 | tail -5  # this phase only
$B/dsp_systems_tests.exe "BassMorph_ShowcaseTrajectory_Blend_E1" 2>&1 | tail -5                                    # one [long] shard
```

Never `ctest -R`. A positional `"[a]~[long]"` is one AND-term; separate arguments OR. Hidden cases run only when a filter names them. Capture any
run longer than ~30 s to a log and read the log; never re-run a slow suite for its output. One build of a tree at a time. Only
`dsp_systems_tests` contains Phase 8 TUs.

### Rules for every task

1. **Failing test first:** write the test cases before the implementation in the same task. Do NOT build or run the test tree (the group's gate
   does). Re-read every file you wrote and fix anything that obviously would not compile. Zero warnings is a gate criterion (MSVC C4244 / C4267 /
   C4100: `f` suffixes, explicit casts, `[[maybe_unused]]`).
2. Header-only, `namespace Krate::DSP {` (nested form). Banner: "Layer 3: System", "Profundum Phase 8 (specs/profundum-phase8-bass-morph)", the
   FRs / SCs it serves and the plan C-items it encodes. Includes point **downward or to L3 peers only** (file map row); never an L4 header;
   neither header calls a component's processing method or setter. New namespace-scope names: exactly `MorphState`, `isValidMorphState`,
   `serializeMorphState`, `deserializeMorphState`, `kMorphStateFormatVersion`, `kMorphLeafFieldCount`, `kMorphStateBytes`, namespace
   `detail::morph_state_io` (contents: `kFieldTable`, `kLeafOffsets`, `Leaf`, `forEachMorphLeaf`, `leafBitsEqual`, `kindOf`, `leavesMatchTable`,
   `sumOfRowBytes`) and `BassMorphEngine`. Everything else is nested. **No namespace-scope `Waveform`**: `MotionConfig::cycleShape` is spelled
   `::Krate::DSP::Waveform` (the LFO's, `primitives/lfo.h:47`). No `static constexpr MorphState` inside any struct (plan R-3); named states live
   in test code only.
3. PascalCase classes, camelCase functions, `trailing_` members, `kPascalCase` constants. Designated initialisers in every aggregate brace init;
   every double → float via `static_cast`; no narrowing. Finiteness only via `detail::isFinite` / `detail::isNaN` (`core/db_utils.h:99–129`); never
   `std::isnan` / `std::isfinite`. Float bit comparisons via `std::bit_cast<std::uint32_t>`; never `memcmp` of `MorphState` or `Resolved`
   (padding, spec FR-005a). Never hand a non-finite value to `std::clamp` or `SlewLimiter::setTarget`.
4. Every engine method and free function is `noexcept`, allocation-, lock-, exception- and I/O-free; fixed-size members only; no `static` mutable
   data; no `std::function`; no virtuals.
5. Test helpers are `inline` functions in namespace `Krate::DSP::BassMorphTest`; never an anonymous namespace in a header. TU-local helpers go
   in an anonymous namespace with a `bm` prefix. **Qualify every call into `MassDynamicsTest::`, `StereoFieldTest::`, `ShapeCharacterTest::`,
   `ProfundumTest::` and `BassMorphTest::`** (same-named helpers exist, e.g. `bitEqual`); no `using namespace` in any Phase 8 file.
6. **Tags.** Every TEST_CASE carries `[systems][profundum][bass-morph]` plus its criterion tag(s) (`[sc-001]` …, `[fr-012]` …). The non-finite TU
   adds `[nonfinite]`; perf cases add `[performance][.perf]`; probe cases add `[.]`. `[long]` only on the cases named `[long]` below, each with
   its `…Smoke` sibling (deviation 5). **Never `[long]`:** NaN / Inf guards, bit-identity, bounded-grid, RT safety, lifecycle and state /
   serialisation cases.
7. No bit-exact float goldens. Bit comparisons only between two runs of the same build in the same process (engine twins; A-static, B-static,
   direct-setter and unmorphed-Drop references), or L against R of one render.
8. Notes: E0 = `anchorMidiHz(16)` (20.60172 Hz), E1 = 28 (41.20344), A1 = 33 (55.0), E2 = 40 (82.40689), C3 = 48 (130.81278). fs = 48 kHz and
   host block 512 unless a case says otherwise. Every measured value is printed with `std::setprecision(6)`.
9. **FTZ / DAZ:** every render test that reads the core or the character stage sets FTZ / DAZ for its scope (restore on exit); engine-only tests
   never set it (plan R-7).
10. Seeds are fixed literals, as named in each case. Random states come only from `BassMorphTest::randomValidState` (raw `std::mt19937` words,
    plan R-8); never `std::uniform_*_distribution`.
11. Evidence: tests write TXT / CSV under `specs/profundum-phase8-bass-morph/evidence/` through `KRATE_DSP_TESTS_DIR` (it points at `dsp/tests`,
    so the repo root is `KRATE_DSP_TESTS_DIR "/../.."`); create directories with `std::filesystem::create_directories`, outside any
    `AllocationScope`.
12. If a test you write exposes a defect in a file another task owns, do not edit that file: add a `DEFECT: <case> <expected/observed>
    <diagnosis> <owning file>` line to your run-log. T015 resolves them.
13. Never relax a threshold or shrink a workload to pass. A bar proven unreachable is STOP AND SURFACE (status "blocked", the measured table in
    notes).
14. Write `specs/profundum-phase8-bass-morph/evidence/run-log/<Tnnn>.txt` for your task: files written, every decision taken, any deviation, any
    `DEFECT:` line.
15. **Lane rule (2026-10-14):** suites, `[long]` shards, clang-tidy sweeps, CPU / perf lanes and probes longer than ~5 minutes are main-loop work.
    A task that owns one writes the lane script under `specs/profundum-phase8-bass-morph/evidence/close/` and returns done with notes "deferred
    to the main loop at close: run <script>" (or "blocked" where its section says STOP AND SURFACE).
16. Every test that relies on a plan C-item cites it in a comment (`// plan S1 C-7`); every SC test cites its SC id in a comment above the
    TEST_CASE. The spec's test-name sketches are the TEST_CASE names.
17. **"Advance until landed"** (used throughout): call `advanceInterval()` until `position() == targetPosition()` **and** `anchorPosition() ==
    BassMorphEngine::curve(anchorMorphMode(), position())` (bitwise), with a hard cap of `2·⌈rampMs·fs/32000⌉ + 8` intervals; hitting the cap is
    a test failure with the values printed, never a silent stop.

---

## Group 1 — CMake registration with compiling stubs (sequential, single task)

### T001 Register every Phase 8 file (deviations 1, 2)

**Files:** `dsp/include/krate/dsp/systems/morph_state.h` (stub), `dsp/include/krate/dsp/systems/bass_morph_engine.h` (stub), the eleven TUs of the
file map (stubs), `dsp/CMakeLists.txt`, `dsp/tests/CMakeLists.txt`, `specs/profundum-phase8-bass-morph/evidence/run-log/T001.txt`.

- **Re-run the ODR sweep** and record the output in the run-log: `grep -rnw -E "MorphState|BassMorphEngine|isValidMorphState|serializeMorphState|
  deserializeMorphState|kMorphStateBytes|kMorphStateFormatVersion|kMorphLeafFieldCount|kMorphRuntimeBytes|kMorphRuntimeFormatVersion|
  morph_state_io|forEachMorphLeaf|kFieldTable|kLeafOffsets|leafBitsEqual|BassMorphTest|MorphChainConfig|MorphChainTaps|MorphEvent|
  renderMorphChain|renderMorphPoly|randomValidState|AmpEnvelopeFields|MotionConfig|AnchorMorphMode" dsp plugins tests tools` (`.h`, `.cpp`,
  `.hpp`, `.js`), and `find dsp plugins tests tools -iname "*bass_morph*" -o -iname "morph_state*"`. Expected: 0 hits (the plan's S0 sweep). Any
  hit other than a file this phase creates → STOP AND SURFACE. Seraphis's `kMorphState0Id` … are different identifiers (spec "New components");
  `-w` keeps them out.
- **Stub headers:** `#pragma once`, the banner (rule 2, "stub — filled by T002" / "T003") and an empty `namespace Krate::DSP {}`.
- **Stub TUs** under `dsp/tests/unit/systems/` (a file comment naming the SCs it will hold and the task that fills it,
  `#include <catch2/catch_test_macros.hpp>`, `#include <krate/dsp/systems/morph_state.h>`, `#include <krate/dsp/systems/bass_morph_engine.h>`, no
  TEST_CASE): `morph_state_test.cpp`, `bass_morph_engine_test.cpp`, `bass_morph_nonfinite_test.cpp`, `bass_morph_helpers_test.cpp`,
  `bass_morph_probe_test.cpp`, `bass_morph_chain_test.cpp`, `bass_morph_click_test.cpp`, `bass_morph_trajectory_test.cpp`,
  `bass_morph_monosum_test.cpp`, `bass_morph_aliasing_test.cpp`, `bass_morph_perf_test.cpp`. `morph_state_test.cpp` additionally includes
  `<krate/dsp/systems/profundum_core.h>`, `<krate/dsp/systems/bass_stereo_field.h>`, `<krate/dsp/systems/anchor_oscillator.h>`,
  `<krate/dsp/systems/mass_law.h>`, `<krate/dsp/systems/spectral_morph_engine.h>` and `<krate/dsp/primitives/lfo.h>`, so the new headers are
  proven to coexist with their peers and with the name hazards (`Waveform`, `SpectralState`).
- **Edit `dsp/CMakeLists.txt`:** after `include/krate/dsp/systems/bass_stereo_field.h` (`:215`), before `)` (`:216`), add
  `# Profundum Phase 8 (specs/profundum-phase8-bass-morph)`, `include/krate/dsp/systems/morph_state.h`,
  `include/krate/dsp/systems/bass_morph_engine.h`.
- **Edit `dsp/tests/CMakeLists.txt`**, each insertion preceded by a comment in the Phase 7 style (`:743–753`):
  - `dsp_systems_tests`: after `unit/systems/bass_stereo_field_perf_test.cpp` (`:762`), before `)` (`:763`): the comment
    `# Profundum Phase 8 (specs/profundum-phase8-bass-morph) - ENUMERATED, not globbed:` with one row per TU — `morph_state_test.cpp` (FR-001–FR-007,
    SC-007(a)(b), SC-019(a)(c)), `bass_morph_engine_test.cpp` (SC-001–SC-003, SC-004(c)(d) engine, SC-005 engine, SC-006(a), SC-007(c)(d), SC-011,
    SC-013, SC-014, SC-019(b), SC-020–SC-022, SC-024 engine), `bass_morph_nonfinite_test.cpp` (SC-012 - in the -fno-fast-math block below),
    `bass_morph_helpers_test.cpp` (fixture self-tests), `bass_morph_probe_test.cpp` (C-9 / R-2 probes, hidden), `bass_morph_chain_test.cpp`
    (SC-005/006 rendered [long], SC-009, SC-010, SC-015, SC-023, SC-024 render, FR-039), `bass_morph_click_test.cpp` (SC-004(a)(b)(d) [long]),
    `bass_morph_trajectory_test.cpp` (SC-008 [long]), `bass_morph_monosum_test.cpp` (SC-016 [long]), `bass_morph_aliasing_test.cpp` (SC-017
    [long]), `bass_morph_perf_test.cpp` (SC-018, FR-083 record [.perf]) — then the eleven `unit/systems/…` lines in that order.
  - `-fno-fast-math` block: after `unit/systems/bass_stereo_field_nonfinite_test.cpp` (`:1259`), before `PROPERTIES` (`:1260`):
    `# Profundum Phase 8: injects NaN/Inf bit patterns (SC-012, FR-081). The perf TU stays out (shipping FP mode).` and
    `unit/systems/bass_morph_nonfinite_test.cpp`. Nothing else.
- **Create** `specs/profundum-phase8-bass-morph/evidence/run-log/` and `evidence/close/` (a `.gitkeep` in each).
- **Verify (read-only):** re-read both CMake files around each insertion; `git diff --stat` shows only the two CMake files plus new files. The
  Group 1 gate builds `dsp_systems_tests` (it reconfigures automatically) and runs the gate filter; the Phase 1–7 case count is unchanged.

---

## Group 2 — `MorphState` (sequential, single task)

### T002 `morph_state.h`, the light test helpers and the `MorphState` tests (FR-001–FR-007; SC-007(a)(b), SC-019(a)(c); plan S3, C-1, C-8, C-13; deviation 3)

**Files:** `dsp/include/krate/dsp/systems/morph_state.h` (replace the stub), `dsp/tests/unit/systems/bass_morph_state_helpers.h` (new),
`dsp/tests/unit/systems/morph_state_test.cpp` (fill the stub). Target `dsp_systems_tests`. Depends on T001.

**Helpers first** (`bass_morph_state_helpers.h`, namespace `Krate::DSP::BassMorphTest`, includes `<krate/dsp/systems/morph_state.h>`,
`<random>`, `<array>`, `<bit>`, `<cmath>`):

- `std::array<std::byte, kMorphStateBytes> serializedBytes(const MorphState&)` (REQUIRE the return is `kMorphStateBytes`).
- `bool bitEqual(const MorphState& a, const MorphState& b)` = the two `serializedBytes` arrays are equal (spec FR-005a; valid states only — the
  helper `REQUIRE`s both are valid).
- `std::vector<std::size_t> differingLeaves(const MorphState& a, const MorphState& b)`: indices i where leaf i differs (floats by
  `std::bit_cast<std::uint32_t>`, enums / bools by value), via `detail::morph_state_io::forEachMorphLeaf(f, a, b)`.
- `float ulpOf(float x)` = `std::nextafter(std::fabs(x), +inf) − std::fabs(x)`.
- `MorphState randomValidState(std::mt19937& rng)` (plan S5.1, portable): `u = (w >> 8) · 2^−24` from raw words; per table row: F32 uniform on
  [lo, hi] (log rows log-uniform on [lo + o, hi + o] − o, clamped into [lo, hi]; `motion.cyclePhaseDeg` on [0, 360) with 360 → 0); Enum8
  `w % (maxEnum + 1)`; Bool `w & 1`. With probability 0.05 per F32 leaf it is set to exactly lo or hi (hiOpen: lo only); then with probability 0.15
  per governing amount (`characterAmount`, `field.spread`, `field.motionDepth`, `motion.amount`, `anchor.level`) it is set to exactly 0. If the
  result fails `BassStereoField::isValidControls(s.field)` (it cannot by construction; the helper `REQUIRE`s it).
- `std::pair<MorphState, MorphState> randomValidPair(std::mt19937& rng, float pEqual = 0.1f)`: two states, then each leaf of B copied from A with
  probability pEqual (one draw per leaf, `forEachMorphLeaf`).
- **Named pairs** (plan S5.1, spec descriptor block; each returns `std::pair<MorphState, MorphState>` built from `MorphState{}` with designated
  member writes): `showcasePair()` (A: recipe `kSineAnchor`, mix 0, Fold 0, SHAPE 0, Tube / 0 / 0.5, Drop 0, ampEnv attack 1 / decay 150 /
  sustain 0 / release 50, field SOLID spread 0, Motion Off, Mass 50, Anchor Sine / Lock / 1.0, feed 0. B: recipe `kGrowl` with edge 1, mix 1 /
  drive 1 / asymmetry 0.5, Fold 80, SHAPE 1, Fold / 0.8 / 0.8, Drop 0, ampEnv 1 / 2000 / 0.8 / 800, field WIDE spread 1, Motion Off, Mass 50,
  Anchor Sine / Lock / 0.7; curves at the C-8 defaults on both sides); `massOnlyPair()` (A.mass 0, B.mass 100); `anchorCurvePair()` (A
  `anchor.level` 1.0, B 0.7); `maxNonlinearPair()` (A Fold 0, Tube, Amount 0; B Fold 100, Fracture, Amount 1, excitation mix 1, drive 1);
  `algorithmCrossingPair()` (both Fold 100, mix 1, drive 1, Amount 1, Tone 0.5; A Tube, B Fracture). Every pair `REQUIRE`s both states valid.

**Tests first** (`morph_state_test.cpp`; all `[systems][profundum][bass-morph]` + the tag shown):

1. `MorphState_TypeTraits` `[fr-001]`: `static_assert(std::is_trivially_copyable_v<MorphState>)`, `std::is_aggregate_v<MorphState>`,
   `!std::is_polymorphic_v<MorphState>`; `sizeof` of every nested enum and of `BassCharacterStage::Algorithm`, `BassStereoField::ImageMode`,
   `MotionSource`, `NoteValue`, `NoteModifier`, `::Krate::DSP::Waveform`, `AnchorOscillator::Waveform`, `Mode` == 1.
2. `MorphState_Defaults` `[fr-004]`: a default `MorphState` leaf-by-leaf (bit compare for floats): recipe == `kDefaultControls` (6 leaves);
   excitation == `ProfundumCore::kDefaultExcitation`; `foldAmount` 0; `shape` 0; Tube / 0 / 0.5; Drop 0 / 100 / 0 / 0; ampEnv 10 / 50 / 0.5 / 100 /
   0.7 / 0.7 / 0.7 (C-8); `field` == `BassStereoField::Controls{}` (11 leaves); motion Off, Mass, 0, 1, 1, Sine, false, Quarter, None, 0, true, 0,
   Bipolar, 0.5, 10, 300; mass 50; transient 6 / 0; `anchor` == `AnchorOscillator::Controls{}` (6 leaves); feed 0.
3. `MorphState_DefaultIsValid` `[fr-004][fr-005]`: `isValidMorphState(MorphState{})`.
4. `MorphState_TableMatchesComponentRanges` `[fr-002][fr-030]`: `kFieldTable.size() == 62`; for i = 0 … 61 the row's `name`, `kind`, `law`, `axis`,
   `group`, `anchorBlock`, `lo`, `hi`, `hiOpen`, `logOffset`, `maxEnum`, `bytes` equal plan S3.2's row i (a 62-row expected table literal in the
   TU, transcribed from plan S3.2); `kLeafOffsets[i]` equals the S3.2 offset column (1, 5, 9, … 197); the field rows' lo / hi equal
   `BassStereoField::kMinCrossoverHz` / `kMaxCrossoverHz` and the [0, 1] boxes; agreement with `BassStereoField::isValidControls` on 10 000 random
   field blocks (seed `0x8005`) whose floats are drawn on [lo − 0.1·span, hi + 0.1·span] (finite only) and enums on [0, maxEnum + 1]:
   `isValidControls(c) ==` "every field leaf inside its table box and every enum ≤ maxEnum", 0 disagreements.
5. `MorphState_Validation` `[fr-005]` (finite cases only; NaN / Inf rows are T006's): per F32 leaf, `nextafter(lo, −∞)` and `nextafter(hi, +∞)`
   (hiOpen row: `hi` itself, i.e. 360.0f) → `false`; `lo` and `hi` (hiOpen: `nextafter(360.0f, 0.0f)`) → `true`; `−0.0f` in every box with lo = 0 →
   `true`; `1e-40f` (denormal) in every box with lo = 0 → `true`; each Enum8 leaf at `maxEnum + 1` → `false`; each log row's lo + o > 0
   (`static_assert` over the table).
6. `MorphState_LeafArity` `[sc-019]` (SC-019(a)(c)): `auto& [m1, …, m19] = s;` for `MorphState`; 7 for `AmpEnvelopeFields`; 16 for `MotionConfig`;
   6 / 3 / 11 / 6 for `SpectralShapeRecipe::Controls`, `ProfundumCore::ExcitationControls`, `BassStereoField::Controls`, `AnchorOscillator::Controls`;
   `static_assert(13 + 7 + 16 + 6 + 3 + 11 + 6 == kMorphLeafFieldCount)`; a comment says "adding a member fails here until the binding, the
   table and the serializer change together". (c): no table name contains, case-insensitively, any of `outputtrim`, `glide`, `legato`, `priority`,
   `poly`, `voice`, `partial`, `guard`, `seed`, `transientduration`, `compression`, `wideoffset`, `memory`, `retriggerphase`.
7. `BassMorph_Serialisation_RoundTrip` `[sc-007]` (SC-007(a)): seed `0x8007`, 10 000 random states: `serializeMorphState(s, buf, 256) == 201`;
   `deserializeMorphState(buf, 201, out)` true and `BassMorphTest::bitEqual(out, s)`; the same with size 256 also true; byte 0 == 1.
8. `BassMorph_Serialisation_Rejects` `[sc-007]` (SC-007(b), finite rows): destination pre-filled with `0xA5` and `out` pre-set to a distinct valid
   state; serialise: `dest == nullptr` → 0; capacity 0 … 200 → 0 with every byte still `0xA5`; each F32 leaf at `nextafter(lo, −∞)` /
   `nextafter(hi, +∞)` and each Enum8 at `maxEnum + 1` → 0, bytes untouched. Deserialise from a valid stream: `src == nullptr`, size 0 … 200,
   version byte 0 / 2 / 255, each F32 leaf's 4 bytes replaced by an out-of-range finite value, each Enum8 byte at `maxEnum + 1` and at 255, each
   Bool byte at 2 and 255 (C-13) → `false` and `out` bitwise unchanged; a stream truncated to 200 bytes → `false`.
9. `MorphStateHelpers_BitEqualAgreesWithLeafBits` (plan S3.6): seed `0x8036`, 10 000 `randomValidPair(rng, 0.5f)` plus, per leaf, a pair that
   differs in that leaf only: `BassMorphTest::bitEqual(a, b) == detail::morph_state_io::leafBitsEqual(a, b)` every time; `+0.0f` vs `−0.0f` in one
   F32 leaf → both `false`.
10. `MorphStateHelpers_RandomValidStateIsValid`: seed `0x8099`, 10 000 states all `isValidMorphState`; each governing amount is exactly 0 in
    [1000, 2000] of them; every F32 leaf hits lo at least once and (closed rows) hi at least once; every enum value 0 … maxEnum appears.

**Implementation** (`morph_state.h`, plan S3 verbatim): the struct of S3.1 (nested `Law`, `Axis`, `Kind`, `SelectionGroup`, `kNumSelectionGroups =
5`, `groupIndex`, `FieldInfo` (S3.2), `AmpEnvelopeFields`, `MotionConfig` with `Source`, `Target`, `Polarity`; member order exactly S3.1, which is
the serialised order); the `static_assert`s after the struct; `kMorphStateFormatVersion = 1`, `kMorphLeafFieldCount = 62`, `kMorphStateBytes =
201`; `detail::morph_state_io::kFieldTable` (the 62 rows of S3.2, every range from its source constant where one exists, designated
initialisers); `Leaf`, `forEachMorphLeaf` (one line per row, S3.3), `kindOf`, `leavesMatchTable` (`consteval`, uses `decltype(leaf)::value`,
plan R-4), `sumOfRowBytes`, `kLeafOffsets` (`consteval`-built), and `static_assert(leavesMatchTable())`, `static_assert(kMorphStateBytes == 1 +
sumOfRowBytes())`, `static_assert(kMorphStateBytes == 201)`, `static_assert(kLeafOffsets[61] == 197)`, plus the S4.2 `consteval` sweep "every
Stepped row has `group != None`, every other row `group == None`, every `anchorBlock` row is leaves 55–61"; `isValidMorphState` (S3.4),
`serializeMorphState` / `deserializeMorphState` (S3.5, the `spectral_state.h:178–300` pattern, `static_assert(std::endian::native ==
std::endian::little, …)`, decode into a local and copy on success), `leafBitsEqual` (S3.6).

**Verify:** the Group 2 gate builds `dsp_systems_tests` and runs the gate filter; the ten cases above pass.

---

## Group 3 — engine skeleton: lifecycle, ramp, Anchor curve, identity (sequential, single task)

### T003 `BassMorphEngine` API, lifecycle, position ramp, Anchor curves and the identity resolver — **B3-1 ruled 2026-10-19:** SC-003(a) / SC-022 carry the 0.5·ulp(max(|p_old|, |p_new|)) term (spec Clarifications, build session); the test's two tolerance expressions were changed by the main loop, nothing else (FR-010–FR-016, FR-020–FR-022, FR-035, FR-050–FR-052; SC-001, SC-002, SC-003(a)(c), SC-014, SC-021(a), SC-022; plan S4.1–S4.4, C-3, C-5, C-7, C-14; deviations 4, 11)

**Files:** `dsp/include/krate/dsp/systems/bass_morph_engine.h` (replace the stub), `dsp/tests/unit/systems/bass_morph_state_helpers.h` (append the
`Resolved` overload), `dsp/tests/unit/systems/bass_morph_engine_test.cpp` (fill the stub). Target `dsp_systems_tests`. Depends on T002.

**Helper append:** `bool bitEqual(const BassMorphEngine::Resolved& a, const BassMorphEngine::Resolved& b)` = `bitEqual(a.state, b.state)` and
`std::bit_cast<std::uint32_t>` equality of `position`, `anchorPosition`, `anchorMass`. The helper header now also includes
`<krate/dsp/systems/bass_morph_engine.h>`.

**Tests first** (append to `bass_morph_engine_test.cpp`; all `[systems][profundum][bass-morph]`; engine-only, no FTZ; a TU-local
`bmLand(engine, rampMs, fs)` implements rule 17):

1. `BassMorph_InterfaceStatics` `[fr-010][fr-014]`: `static_assert`s: `kControlInterval == 32` and equal to `ProfundumCore::`, `MassLaw::`,
   `BassStereoField::kControlInterval`; `kMinPositionRampMs == 5`, `kMaxPositionRampMs == 2000`, `kDefaultPositionRampMs == 40`; `kThetaUp == 0.52f`,
   `kThetaDown == 0.48f`; `kPunchExponent == 3`; `kDefaultAnchorMorphMode == AnchorMorphMode::Blend`; `kMorphRuntimeFormatVersion == 1`,
   `kMorphRuntimeBytes == 20`; `&BassMorphEngine::kLawTable == &detail::morph_state_io::kFieldTable`; `std::is_copy_constructible_v` and
   `std::is_nothrow_move_constructible_v`; `sizeof(Endpoint) == 1`, `sizeof(AnchorMorphMode) == 1`.
2. `BassMorph_EndpointApi` `[fr-012]` (C-14): fresh engine: `endpoint(A)` and `endpoint(B)` `bitEqual` `MorphState{}`; seed `0x8012`:
   `setEndpoint(A, sa)` and `(B, sb)` return true and read back `bitEqual`; `setEndpoint(static_cast<Endpoint>(2), s)` returns false, both
   endpoints unchanged, `endpoint(static_cast<Endpoint>(2))` `bitEqual` `endpoint(A)`; an invalid state (one field at `nextafter(hi, +∞)`) → false,
   unchanged; at 48 kHz mid-ramp (target 1 from 0, 10 intervals in) re-setting `sb` bitwise-identical returns true and `resolved()` plus the next
   16 `advanceInterval` results are `BassMorphTest::bitEqual` to a twin engine that received no call.
3. `BassMorph_AnchorModeApi` `[fr-014]`: fresh engine `anchorMorphMode() == Blend`; mid-ramp, `setAnchorMorphMode(static_cast<AnchorMorphMode>(3))`
   and `(255)`: mode unchanged, and the next 64 intervals' `anchorPosition()` values bit-equal a twin's.
4. `BassMorph_SubThresholdLanding` `[fr-022][fr-013]` (C-7), each mode at 48 kHz: from p = 0.5 (landed), `setPosition(1e-20f)` and
   `setPosition(std::nextafter(1e-15f, 0.0f))` → `targetPosition()` bit pattern `0x00000000`; `setPosition(1e-15f)` → target `1e-15f` bitwise; after
   landing `position() == targetPosition()` bitwise and 8 further intervals leave it unchanged; Punch, `setPosition(5e-6f)`: after landing
   `anchorPosition()` is `+0` and equals `BassMorphEngine::curve(Punch, position())` bitwise, unchanged for 8 intervals; `reset()` at 5e-6 in Punch
   publishes `anchorPosition()` `+0`, and the next `advanceInterval` leaves `resolved()` `bitEqual`. `curve(m, 0) == 0`, `curve(m, 1) == 1` (Blend,
   Punch), `curve(Lock, p) == 0` for every p, `curve(Punch, 0.5f) == 0.125f` exactly.
5. `BassMorph_BoundedStep` `[sc-003]` — SECTIONs `(a)` and `(c)` here (T004 adds `(b)`): jumps 0 → 1, 1 → 0, 0.3 → 0.7 and 30 s of seeded random
   automation (seed `0x8003`, a new target every 25–400 ms, uniform on [0, 1] with 10 % exact 0 / 1), × fs {44 100, 48 000, 96 000, 192 000} ×
   ramp {5, 40, 2000} ms, every mode, with an Anchor-mode change every 0.5 s of the random run. (a) per interval \|Δp\| and \|Δp_a\| ≤
   Δp_max·(1 + 4·FLT_EPSILON), Δp_max = 32 000 / (rampMs·fs) in double. (c) from a landed p = 0, `setPosition(1)`: the number of intervals until
   `position() == 1` is within ⌈rampMs·fs / 32 000⌉ ± 1 (e.g. 60 at 40 ms / 48 kHz, 8 at 5 ms / 48 kHz, 3000 at 2000 ms / 48 kHz).
6. `BassMorph_RampTimeChange` `[sc-022]`: 48 kHz, 0 → 1 at 40 ms; after 10 intervals `setPositionRampMs(5 | 40 | 2000)`: `position()` immediately after
   the call bitwise equal to the value before it (and to a twin without the call); from the next interval |Δp| == the new Δp_max within (1 + 4ε)
   until landing; fresh traversals at 5, 40, 2000 ms: SC-003(c) counts; `setPositionRampMs(4.9f)` → `positionRampMs() == 5`; `2001` → 2000; a NaN
   bit pattern is **T006's** (this TU is fast-math) — here `+∞` is not used either; `positionRampMs()` after `setPositionRampMs` before `prepare`
   holds the stored value.
7. `BassMorph_SampleRates` `[sc-014]`: fs {22 050, 44 100, 48 000, 96 000, 192 000} × ramp {5, 40, 2000}: SC-001 identity (test 9's procedure) on
   1 000 pairs (seed `0x8014`); traversal ⌈rampMs·fs/32 000⌉ ± 1; `prepare(8000)` traverses in the same count as `prepare(22050)` and
   `prepare(384000)` as `prepare(192000)`.
8. `BassMorph_Lifecycle` `[sc-021]` — SECTION `(a)` here (T004 adds `(b)`): an unprepared engine with `setEndpoint` × 2 (seed `0x8021` pair) and
   `setPosition(0.7f)`: `advanceInterval()` `bitEqual` to a prepared (48 kHz) engine's `resolved()` after the same calls and `reset()`;
   `position() == 0.7f` exactly on both.
9. `BassMorph_EndpointIdentity` `[sc-001]`: seed `0x8001`, 10 000 random pairs + `showcasePair()` + the 42 ordered pairs of the seven named recipes
   (embedded in `MorphState{}`), × the three modes, 48 kHz, default ramp: `setPosition(0)`, `reset()` → `bitEqual(resolved().state, A)`;
   `setPosition(1)`, land → state `bitEqual` E, where E = B with `anchor` and `anchorFoldFeed` taken from A in Lock, E = B in Blend / Punch;
   `anchorMass` == A.mass in Lock, B.mass otherwise (bitwise); `setPosition(0)`, land → `bitEqual` A. Count mismatches; `REQUIRE(mismatches == 0)`.
10. `BassMorph_EqualEndpointsConstant` `[sc-002]`: seed `0x8002`, 1 000 random states, A == B, each mode: `reset()` at p ∈ {0, 0.01, …, 1} (101
    points) and every interval of a 0 → 1 → 0 ramp: `bitEqual(resolved().state, A)`, 0 mismatches.

**Implementation** (`bass_morph_engine.h`): plan S4.1 API (plus the public static `curve`, deviation 11), S4.2 members (`a_`, `b_`, `logA_`,
`logB_` zero-filled for now, `posSlew_`, `anchorSlew_`, `target_`, `rampMs_`, `intervalRate_`, `mode_`, `selections_` (all A, indexed only
through `MorphState::groupIndex`), `resolved_`, `prepared_`, `dirty_`; `static_assert(MorphState::groupIndex(SelectionGroup::Anchor) <
std::tuple_size_v<decltype(selections_)>)`), S4.3 lifecycle (C-5), S4.4 position / ramp / curve (C-7; `setPosition`: non-finite → return; `p <
kDenormalThreshold ? 0.0f : (p >= 1.0f ? 1.0f : p)`), `advanceInterval` per S4.4 (before `prepare`: snap and resolve, FR-011), `selection(g)`
(None → A). **Resolver (temporary, deviation 4):** per leaf with q = row.anchorBlock ? p_a : p — Stepped: q ≤ 0 → A, q ≥ 1 → B, else
`selections_[groupIndex(row.group)]`'s side (A at this stage); continuous: `q == 0 || bitsEqual(xa, xb)` → xa, `q == 1` → xb, else xa (banner:
"interior positions resolve to A until T004"); `anchorMass` likewise. `serializeRuntime` / `restoreRuntime` are **not** declared yet (T005).

**Verify:** the Group 3 gate; the ten cases pass. Write the run-log with the traversal counts the cases printed at 48 kHz.

---

## Group 4 — laws, selection machine and cache (sequential, single task)

### T004 Interpolation laws, the per-group selection machine, the resolution cache and their tests (FR-030–FR-037, FR-034, FR-040, FR-053–FR-055, FR-061, FR-082; SC-003(b), SC-004(c), SC-004(d) engine, SC-005 engine, SC-013, SC-019(b), SC-020, SC-021(b), SC-024 engine; plan S4.5–S4.7, S6, C-2, C-4, C-6)

**Files:** `dsp/include/krate/dsp/systems/bass_morph_engine.h` (edit), `dsp/tests/unit/systems/bass_morph_engine_test.cpp` (append). Target
`dsp_systems_tests`. Depends on T003.

**Tests first** (append; `[systems][profundum][bass-morph]` + tag; each must fail against T003's identity-only resolver, which is noted in the
run-log case by case):

1. `BassMorph_LawTable` `[sc-019]` (SC-019(b)): Blend, 48 kHz. For each of the 62 leaves: A = `MorphState{}`; B = A with that leaf set to a value
   drawn by the row's rule (seed `0x8019`), different from A's (a different enumerator for Enum8, the other bool for Bool). F32 rows: `reset()` at
   p = 0.25 → `differingLeaves(resolved().state, A)` == {that leaf}; the value is within 2·`ulpOf(max(|xA|, |xB|))` (Linear, EqualPower) or 8·ulp
   (Logarithmic) of the closed form in double: linear `xA + 0.25·(xB − xA)`; equal-power `sqrt(0.75·xA² + 0.25·xB²)`; log
   `exp2(log2(xA + o) + 0.25·(log2(xB + o) − log2(xA + o))) − o`. Stepped rows (all governing amounts are default here: rule 3 applies): from p = 0,
   `setPosition(kThetaUp − 0.005f)`, land → the leaf equals A's; `setPosition(kThetaUp)`, land → B's; no other leaf differs.
2. `BassMorph_LawTable_GoverningAmount` `[sc-019][fr-034]` (plan S7.4 row, C-2): all five groups — Character (`characterAmount`; field
   `characterAlgorithm` Tube vs Fold), ImageMode (`field.spread`; mode SOLID vs WIDE), FieldMotion (`field.motionDepth`; the four selectors all
   differ: Drift / Cycle, false / true, Quarter / Whole, None / Dotted), Motion (`motion.amount`; the eight selectors all differ; variants with the
   non-zero side −0.5 and +0.5, and one with the zero side `−0.0f`; `motion.depth` 1 on both sides), Anchor (`anchor.level`; waveform Sine vs
   Triangle and mode Lock vs Punch; Blend so p_a = p). For each group, four cases: zero on A only, zero on B only, both non-zero (0.8 / ±0.5),
   both zero; every other leaf equal. Fresh engine, `reset()` at 0, then p ∈ {0, 0.01, 0.25, θ_up − 0.005, 0.99, 1} visited ascending
   (`setPosition` + land), then descending {0.99, θ_up − 0.005, 0.25, 0.01, 0}. At each landed point assert `selection(g)` and every discrete leaf
   of the group bitwise: rule-2 cases → the non-silent side at every q ∈ (0, 1), A at 0, B at 1; both-non-zero and both-zero → A at 0.01, 0.25,
   θ_up − 0.005 ascending, B at 0.99 and 1, B kept at θ_up − 0.005 descending, A at 0.25, 0.01, 0. The other four groups follow the rule-3
   trajectory of their own (default, equal-on-both-sides) governing amounts — A at 0, 0.01, 0.25, θ_up − 0.005 ascending; B at 0.99 and 1 (rule 1
   selects B at q = 1 for every group, S4.5 / FR-034) and at θ_up − 0.005 descending; A at 0.25, 0.01, 0 — and their discrete leaves equal A's
   (equal on both sides) at every visit (T015-1: corrected from "stay A's throughout", which contradicts rule 1).
3. `BassMorph_MonotoneLaws` `[fr-037]`: seed `0x8037`, 100 random valid pairs × 3 modes; p = k / 10 000, k = 0 … 10 000 ascending (`setPosition` +
   `reset()` each), plus every interval of a 0 → 1 traversal at 2000 ms (3 000 intervals at 48 kHz). Every continuous leaf: xB ≥ xA → resolved
   non-decreasing in p, else non-increasing, **zero tolerance** (float compare); Anchor-block leaves and `anchorMass` against p_a. Any reversal
   fails with the leaf name, the p pair and both values printed.
4. `BassMorph_ValidityClosure` `[sc-020]`: seed `0x8020`, 100 000 random pairs × a random p (raw-word uniform, 5 % exact 0 / 1, 5 % within 1e-6 of
   θ) × a random mode, `reset()`: `isValidMorphState(resolved().state)` and every continuous leaf in [min(xA, xB), max(xA, xB)].
5. `BassMorph_BoundedStep` — append SECTION `(b)` `[sc-003]`: the same runs as (a) (T003's loops, extended to read the state): per continuous
   leaf and interval |Δx| ≤ bound·(1 + 4·FLT_EPSILON) + k·`ulpOf(max(|xA|, |xB|))`, k = 2 linear / equal-power, 8 log; bounds (plan S6, at
   Δp_max): linear |xB − xA|·Δp; log (max + o)·|((xB + o)/(xA + o))^Δp − 1|; equal-power √Δp·max(mA, mB). Anchor-block leaves at Δp_a.
6. `BassMorph_Hysteresis` `[sc-004]` (SC-004(c)): pair A Tube / Amount 0.8, B Fold / 0.8 (rule 3), 48 kHz, default ramp. From `reset()` at 0, land at
   0.51 (selection A), then for 2 s (3 000 intervals) set the target to θ_up + 0.01·sin(2π·5·t) each interval (t = i·32/fs) and record p and
   `selection(Character)`: switches ≤ 1. Positive control: a single-threshold model (switch on p ≥ 0.52 / p < 0.52) on the recorded p switches ≥ 10.
   Mirror: from 1, land at 0.49 (selection B), jitter θ_down ± 0.01: switches ≤ 1; the single-threshold model at 0.48 switches ≥ 10.
7. `BassMorph_GoverningAmountRule` `[sc-004]` (SC-004(d) engine part): rows Character (A Tube / Amount 0 → B Fold / 0.8, and the reverse pair with
   the zero on B), ImageMode (A SOLID / spread 0 → B WIDE / spread 1, and reverse), Anchor waveform (A Sine / level 0 → B Triangle / 0.7, Blend, and
   reverse), Anchor mode (A Lock / level 0 → B Punch / 0.7, Blend, and reverse); 48 kHz, default ramp, 0 → 1 → 0 by two landed jumps. Per
   interval: q ∈ (0, 1) → the non-silent side's value; q = 0 → A; q = 1 → B (bitwise). The resolved discrete value changes exactly once per
   direction: zero on A → at the first interval with q > 0 (rising) and at the landing on 0 (falling); zero on B → at the landing on 1 (rising)
   and at the first interval with q < 1 (falling). Positive control: both amounts non-zero (0.8 / 0.7) → the change happens at the first interval
   with q ≥ 0.52 rising and q ≤ 0.48 falling, not at the first interval.
8. `BassMorph_AnchorCurves` `[sc-005]` (engine part; C-12): `anchorCurvePair()`, `reset()` at p ∈ {0, 0.05, …, 1}: Lock level bit pattern of 1.0f at
   every p; Blend |level − (1 − 0.3p)| ≤ 1e-6; Punch |level − (1 − 0.3p³)| ≤ 1e-6 and level_Punch(p) ≥ level_Blend(p). (e) `massOnlyPair()`: Lock →
   `MassLaw::evaluateRow(anchorRow, resolved().anchorMass, resolved().state.anchor.level)` bitwise equal to its p = 0 value at every p; Blend → it is
   non-decreasing in p, equals base − 0.5 = 0.0 at p = 0 and min(base + 0.5, 1) = 1.0 at p = 1 within 1e-6 (default level 0.5), printed per p to
   `evidence/sc005e-mass-only.csv` (`p,mode,anchorMass,effectiveLevel`).
9. `BassMorph_Lifecycle` — append SECTION `(b)` `[sc-021]` (rule-3 pair Tube / 0.8 vs Fold / 0.8): swept 0 → 0.5 (landed) then `reset()` → selection
   A; swept 1 → 0.5 then `reset()` → B; `reset()` at 0.53 → B and at 0.47 → A from either side; a fresh engine `reset()` at 0.5 → A.
10. `BassMorph_EndpointEditMidMorph` `[sc-024]` (engine part): seed `0x8024`, Blend, 48 kHz, a random pair with every governing amount forced
    non-zero (0 → 0.5); landed at p = 0.3; B′ = a random valid state with every continuous leaf different from B's and every discrete leaf equal to
    B's. `setEndpoint(B, B′)`, one `advanceInterval()`: per continuous leaf the change equals law(0.3; A, B′) − law(0.3; A, B) in double within
    k·ulp(max(|xA|, |xB|)) + k·ulp(max(|xA|, |xB′|)) (k as in test 1); the following interval is `bitEqual` (complete in one interval). Discrete:
    from the landed p = 0.3 (selection A), `setEndpoint(A, A′)` with A′ = A with `characterAlgorithm` changed → `resolved().state.characterAlgorithm
    == A′.characterAlgorithm` after exactly one `advanceInterval()`.
11. `BassMorph_Partition` `[sc-013]`: seed `0x8013`, a 20 s sample-stamped event list at 48 kHz (Position every 20–200 ms with 10 % at θ ± 1e-3 and
    5 % exact 0 / 1; RampMs every 1 s on [5, 2000]; Mode every 0.7 s; EndpointA / EndpointB every 1.3 s from `randomValidState`). A TU-local driver
    walks host blocks of {1, 7, 32, 64, 511, 4096} samples and, at every absolute grid sample inside the block, applies every unapplied event with
    stamp ≤ that sample, then `advanceInterval()` once and records the `Resolved`. Two engines per partition; every sequence `bitEqual` to the
    block-1 sequence of the first engine; sequence length = ⌈total / 32⌉.

**Implementation** (plan S4.5–S4.6 verbatim): `updateSelections(p, pa)` per group (rule 1 identity, rule 2 forced, rule 3 hysteresis, keep inside
the band; governing amounts per S4.5; `!= 0.0f`), called from `reset()` and `advanceInterval()`; the resolver of S4.6 (C-4 double evaluation,
`std::clamp` to [min, max] of the endpoints after `static_cast<float>`, identity branches untouched); `setEndpoint` refreshes that endpoint's
`log2(x + o)` cache for the 11 Logarithmic rows and sets `dirty_`; C-6: `advanceInterval` resolves only when `dirty_` is set, which `setEndpoint`,
a change of p or p_a (bitwise) or any selection change sets; `anchorMass` per S4.6 / FR-054. Remove the "until T004" banner line.

**Verify:** the Group 4 gate; every case of T003 and T004 passes. The run-log records which T004 cases were red against T003's resolver
(failing-first evidence comes from reasoning, not a run: executors do not build).

---

## Group 5 — runtime round trip, RT safety, idempotence (sequential, single task)

### T005 `serializeRuntime` / `restoreRuntime`, RT safety, idempotence and the S4.10 Anchor row (FR-062, FR-080, FR-081 restore side; SC-006(a), SC-007(c)(d), SC-011; plan S4.8, S4.9, S4.10, C-2, C-7; deviation 12)

**Files:** `dsp/include/krate/dsp/systems/bass_morph_engine.h` (edit), `dsp/tests/unit/systems/bass_morph_engine_test.cpp` (append),
`specs/profundum-phase8-bass-morph/plan.md` (S4.10 Anchor punch / ghost row only), `specs/profundum-phase8-bass-morph/evidence/receiver-inventory.txt`
(new). Target `dsp_systems_tests`. Depends on T004.

**Tests first** (append; `[systems][profundum][bass-morph]` + tag):

1. `BassMorph_RuntimeRoundTrip` `[sc-007]` (SC-007(c)): live engines at 48 kHz, ramp 333 ms, each mode, under seeded automation (seed `0x8070`);
   stops: 200 random points; a band point reached from below (pair Tube / 0.8 vs Fold / 0.8: land at 0.5 from 0 → selections A) and from above
   (from 1 → B); mid-ramp (target 1 from 0, stop after 7 intervals, target ≠ p); an in-flight mode change (Blend → Punch at landed 0.6, stop 3
   intervals later); `showcasePair()` landed at 0.3 (rule 2: `selection(Character) == B`). Each stop: `serializeMorphState(A)`,
   `serializeMorphState(B)`, `serializeRuntime(buf, 20) == 20`; a fresh engine `prepare(48000)` + `setEndpoint` × 2 (from the deserialised bytes)
   + `restoreRuntime(buf, 20) == true` → `resolved()` `bitEqual` the live engine's, `positionRampMs() == 333`; then 4 096 further intervals under
   the same automation applied to both → every `Resolved` `bitEqual`. 0 mismatches.
2. `BassMorph_RuntimeRejects` `[sc-007]` (SC-007(d), finite rows; NaN / Inf / denormal rows are T006's): from a valid 20-byte blob, each of: `src ==
   nullptr`; size 0 … 19; version 0 / 2; target, p or p_a = 1.5f or −0.25f; target / p / p_a = `−0.0f` (C-7); ramp 4.9f and 2001.0f; mode byte 3 and
   255; main mask 0x10 and 0xFF; Anchor byte 2; a selection contradicting S4.5 at the stored position for the current endpoints (rule-3 pair: B at
   p = 0.40, A at p = 0.60; p = 0 with a B bit; p = 1 with an A bit; showcase at 0.3 with Character bit A, which rule 2 forbids) → `false`, and
   `resolved()` plus `serializeRuntime` bytes unchanged. `serializeRuntime(nullptr, 20)` and capacity 19 → 0.
3. `BassMorph_Idempotent` `[sc-006]` (SC-006(a), deviation 12): seed `0x8006`, 200 random pairs × 3 modes, 48 kHz: record `resolved()` and the
   runtime bytes after `reset()` at 0; (i) 0 → 1 (land) → 0 (land); (ii) 2 s of random automation (a new target every 30–300 ms, Mode changes not
   included) ending with `setPosition(0)` and land: final `resolved()` `bitEqual` the initial one and the runtime bytes equal (both selections A).
4. `BassMorph_RealTimeSafety` `[sc-011]`: `static_assert(noexcept(...))` on every public member and on `isValidMorphState`, `serializeMorphState`,
   `deserializeMorphState`; then inside one `TestHelpers::AllocationScope` (all buffers and states pre-built outside it): 10 s at 48 kHz (15 000
   intervals) of random automation (seed `0x8011`), `setEndpoint` every 100 intervals, `serializeRuntime` / `restoreRuntime` every 500,
   `serializeMorphState` / `deserializeMorphState` every 500, `reset()` every 1 000, `setPositionRampMs` / `setAnchorMorphMode` every 250;
   `getAllocationCount()` read inside the scope == 0.

**Implementation** (plan S4.8): the 20-byte layout as private `static constexpr` offsets; `serializeRuntime` (null or short → 0); `restoreRuntime`
decodes into locals and rejects per S4.8 (non-finite via `detail::isFinite`, range, `−0` and 0 < |x| < `kDenormalThreshold`, ramp range, mode,
mask > 0x0F, Anchor byte > 1, and the per-group S4.5 consistency check including rule 2 against the **current** endpoints); on success it installs
exactly as S4.8 writes (`posSlew_.snapTo(p); posSlew_.setTarget(target_); anchorSlew_.snapTo(pa)`, selections, `dirty_ = true`, `resolve()`; no
`advanceInterval`, no `reset()`).

**S4.10 Anchor row (plan instruction to "a group-2 task"):** read `systems/anchor_oscillator.h:224–245` (`setControls` → `pending_`), `:436–446`
(`latchControls`, where `pending_` becomes `active_`), find every caller of `latchControls` (`grep -n latchControls`), and `:1000–1060` (where
`punchTimeMs`, `ghostTimeMs` and `punchBoostDb` are read into a gain program). Write what you read (file:line quotes) to
`evidence/receiver-inventory.txt` and replace the plan S4.10 cell "consumer not read in this session: a group-2 task reads …" with the class
(latched at the control latch, consumed when a program is built: next note-on or mode bridge — **only if the code says so**) and the file:line
citations. Change nothing else in `plan.md`.

**Verify:** the Group 5 gate; every engine case passes.

---

## Group 6 — non-finite TU and the render fixture (parallel; disjoint new files)

### T006 [P] Non-finite safety (SC-012; FR-005 / FR-006 / FR-013 / FR-021 / FR-062 non-finite rows; FR-081; plan R-5, C-7)

**Files:** `dsp/tests/unit/systems/bass_morph_nonfinite_test.cpp` (fill the stub; it is in the `-fno-fast-math` block). Target
`dsp_systems_tests`. Depends on T005. Includes only `morph_state.h`, `bass_morph_engine.h` and `bass_morph_state_helpers.h`.

**Tests** (`[systems][profundum][bass-morph][nonfinite]` + `[sc-012]`; every special value made from a bit pattern through a `volatile
std::uint32_t` and `std::bit_cast<float>`: quiet NaN `0x7FC00000`, signalling NaN `0x7FA00000`, `−NaN` `0xFFC00000`, `+∞` `0x7F800000`, `−∞`
`0xFF800000`, denormals `0x00000001` and `0x80000001`, `−0` `0x80000000`):

1. `BassMorph_NonFinite` (SC-012), SECTIONs:
   - **position:** from a landed p = 0.4 (each mode), `setPosition(v)` for every special value: NaN / ±∞ → `targetPosition()` stays 0.4f bitwise and
     the next 64 intervals equal a twin without the call; ± denormal and −0 → target `+0` (C-7).
   - **ramp:** `setPositionRampMs(NaN | ±∞)` → `positionRampMs()` unchanged.
   - **endpoints:** per F32 leaf: NaN (both kinds), +∞, −∞, `nextafter(lo, −∞)`, `nextafter(hi, +∞)` → `setEndpoint` false, endpoint unchanged;
     per Enum8 leaf `maxEnum + 1` → false.
   - **validation:** `isValidMorphState` false for each of those states; a ± denormal in a [0, x] box → true for +denormal, and for −denormal true
     only where lo ≤ −denormal (i.e. false in [0, x] boxes, true in [−1, x] boxes).
   - **state bytes:** for each F32 leaf, the 4 stream bytes set to each NaN / ±∞ pattern → `deserializeMorphState` false, `out` unchanged;
     `serializeMorphState` of the in-memory invalid state → 0, destination bytes unchanged.
   - **runtime bytes:** target, p or p_a = NaN / ±∞ / `1e-20f` / `1e-16f` / `std::nextafter(1e-15f, 0.0f)` / ± denormal → `restoreRuntime` false;
     ramp = NaN / ±∞ → false; `resolved()` unchanged each time.
   - **after every call above:** every float of `resolved().state` passes `detail::isFinite` and `isValidMorphState(resolved().state)`.

### T007 [P] The render fixture `bass_morph_test_helpers.h` and its self-tests (FR-070, FR-071; plan S5, S7.3, C-11; deviations 9, 10)

**Files:** `dsp/tests/unit/systems/bass_morph_test_helpers.h` (new), `dsp/tests/unit/systems/bass_morph_helpers_test.cpp` (fill the stub). Target
`dsp_systems_tests`. Depends on T005.

**Header** (namespace `Krate::DSP::BassMorphTest`; includes `bass_morph_state_helpers.h`, `bass_stereo_field_test_helpers.h` (which brings the
Phase 6 helpers), `profundum_core_test_helpers.h`, `anchor_oscillator_test_helpers.h`, `../processors/shape_character_metric_helpers.h`,
`<krate/dsp/primitives/adsr_envelope.h>`, `<krate/dsp/processors/drop_envelope.h>`, `tests/test_helpers/k_weighting.h`, `allocation_detector.h`;
the Phase 6 / 7 helper headers stay read-only):

- `MorphEvent`, `MorphChainConfig`, `MorphChainTaps` exactly as plan S5.2, plus `std::array<std::size_t, 7> ampEnvSetterCalls{}` (deviation 9).
  `MorphChainConfig::muteCharacter` (SC-016) and `drop` included.
- `renderMorphChain(const MorphChainConfig&)`: construction S5.3 (engine prepared after its setters, `r0 = resolved()`; ShapeMorph,
  BassCharacterStage and HarmonicFold configured from `r0` **before** their `prepare(fs, 512)`; the field through a `FieldChainConfig` copy with
  `field = r0.state.field` passed to `StereoFieldTest::makeField`; core / Anchor / Drop / ADSR prepared then given `r0`; `MassLaw` prepared,
  `setBase(baseFor(r0))`, `setMass`, `reset()`; dynamics via `MassDynamicsTest::makeChainDynamics` with the shaper gains from `r0`), then
  `MassDynamicsTest::gridDriver(total, cfg.hostBlock, …)`: per grid point the eleven steps of plan S5.4 **in that order** (events, exactly one
  `advanceInterval()`; Mass split with `evaluateRow` of the AnchorLevel row at `anchorMass`; core controls + excitation; Fold + SHAPE;
  `setAlgorithm` only on change, recorded in `discreteSetterCalls`; Anchor `setControls` with the Mass-biased level, recorded on waveform / mode
  change; the fold-feed target; Drop setters; ADSR setters change-only per leaf by bit compare, each call counted in `ampEnvSetterCalls`, the first
  interval applies all seven; `field.setControls` + `advanceControl()` (mode change recorded), the pan vector → `core.setPartialPanOffsets`, the WIDE
  offsets → `character.setChannelOffsets`; MassDynamics setters and `a1Next = MassDynamicsTest::unbiasedH1Amplitude(r.state.recipe, fNow, fs)`;
  note-on / note-off). Per block S5.5 (Drop f0 track when `cfg.drop`; core → `ShapeMorph` → `+ feed[n]·anchor[n]` (feed ramped linearly across the
  interval from the previous value, C-11) → Fold → character; amp envelope multiplies character **and** anchor taps when `ampEnvelope`; with it off,
  no multiplication at all; `muteCharacter` zeroes the character contribution after the tap is recorded; `StereoFieldTest::processFieldTail(…)`).
  `REQUIRE(cfg.noteOnSample % 32 == 0)`. Record `resolvedPerInterval`, `anchorLevelHanded`, `advanceSamples`.
- `renderMorphPoly(std::span<const double> f0s, const MorphChainConfig& common)`: the Phase 7 poly structure (`bass_stereo_field_test_helpers.h:463`),
  one voice chain per f0, all reading the same `engine.resolved()`; `perVoiceResolved[v]` records what each voice applied; one shared tail.
- `baseFor(const MorphState&)` (S5.3) and the descriptor readers of S5.7: `dCxDb`, `dNlDb`, `dStDb`, `dBrOct`, `lKDb`, `eSubDb(L, R, f0, fs)`,
  `dTMs` (whole periods; powers floored at `StereoFieldTest::kFloorDb`; read window `[kSettleSeconds·fs, + kLowFrequencyFftSize)` trimmed to whole
  periods; `dTMs` `REQUIRE`s the 80 dB fall-off before the end of the render), and `gateRender(cfg)` which renders with `ampEnvelope = true`,
  `noteOffSample` = fs · 1.0 s rounded down to a multiple of 32, length `noteOff + 2 s`, extended in 1 s steps up to 12 s until the
  output mid's one-period RMS is ≥ 80 dB below its peak; reaching 12 s without it is `FAIL` with the reading printed.
- `transitionEndSamples(field kind, f0, fs)` (deviation 10): character algorithm → one period + 10 ms; Anchor waveform / mode → 5 ms; `field.mode`
  and the four `field.motion*` selectors → one period + 334·32 samples.

**Tests** (`bass_morph_helpers_test.cpp`, `[systems][profundum][bass-morph]` + `[fr-070]`; FTZ / DAZ set per case):

1. `BassMorphHelpers_StaticEqualsFieldChain`: A == B == the state `renderFieldChain` can express (read `FieldChainConfig`,
   `bass_stereo_field_test_helpers.h:114–135`, and map the showcase B's chain settings onto it: recipe `kGrowl` edge 1, mix 1 / drive 1 / asymmetry
   0.5, Fold 80, SHAPE 1, Fold / 0.8 / 0.8, field WIDE spread 1, Mass 50, Anchor Sine / Lock / 0.7; whatever `FieldChainConfig` cannot express is set
   to the value it hard-codes, and the mapping is written to the run-log); E1, 48 kHz, 1.5 s, `ampEnvelope = false`: max |Δ| between the two
   renders ≤ `TestUtils::kSampleTolerance` per sample on the character, sum and out taps (both channels).
2. `BassMorphHelpers_GridCalls` `[sc-010]`: 1.0 s render at host blocks 1, 7, 512: `advanceSamples == {0, 32, 64, …}` and its size is ⌈total / 32⌉.
3. `BassMorphHelpers_ChangeOnlyEnvelopeSetters`: A and B differ only in `ampEnv.decayCurve` (0.7 → −0.7); a 0 → 1 jump at 0.5 s with ramp 40 ms:
   `ampEnvSetterCalls` for the six unchanged leaves == 1 each; for `decayCurve` == 1 + the number of intervals in which the resolved decayCurve
   changed bitwise (counted from `resolvedPerInterval`).
4. `BassMorphHelpers_DescriptorsOnSines`: on synthetic vectors (no chain): `dCxDb` of a pure 41.2 Hz sine ≤ −100 dB; `eSubDb` of a sine of amplitude
   a ∈ {0.1, 0.5, 1.0} equals 20·log10(a/√2) within 0.05 dB; `dTMs` of a constant tone of length T ∈ {0.5, 1.0} s followed by silence equals T·1000
   within one period (24.3 ms at 41.2 Hz).
5. `BassMorphHelpers_MuteCharacterKeepsTail` `[sc-016]`: showcase B static, `muteCharacter = true`: the character tap is still recorded non-zero and
   the sum tap equals the Anchor tap bitwise.

---

## Group 7 — C-9 coherence probe and R-2 early timing (single task; STOP AND SURFACE checkpoint)

> **Checkpoint taken 2026-10-19 (main loop).** R2 EARLY GREEN (185 ns vs 333 ns). C9 was RED on the drafted B; rulings
> B7-1 (floor at the resolved Anchor level) and B7-2 (B harvested) were taken, three reading refinements made (E_sub on f₀,
> D_nl against a linear twin core, relative −60 dB floors) and B replaced by harvest candidate 59 (spec Clarifications,
> build session; evidence/harvest/, evidence/showcase-harvest-aggregate.txt). T009–T014 read the spec as amended.

### T008 The hidden probes; the main loop runs them before any rendered gate is trusted (plan C-9, R-1, R-2; deviations 6, 7)

**Files:** `dsp/tests/unit/systems/bass_morph_probe_test.cpp` (fill the stub), `specs/profundum-phase8-bass-morph/evidence/close/probe.sh` (new),
`evidence/run-log/T008.txt`. Target `dsp_systems_tests`. Depends on T007.

**Cases** (both `[systems][profundum][bass-morph][.]`):

1. `BassMorph_SumTapCoherenceProbe`: `showcasePair()`, 48 kHz, steady renders (`ampEnvelope = false`), notes E1 and C3, p ∈ {0, 0.5, 1}, modes Lock,
   Blend, Punch. Per (note, p, mode) read on the read window (whole periods): the complex h1 (single-bin DFT at f0 of the mid) of the character tap
   H_c and of the Anchor tap H_a; the Anchor level L in force (`anchorLevelHanded` of the last interval); c = |H_c| / (|H_a| / L), θ = arg H_c − arg
   H_a (degrees). Predicted margins (plan C-9): SC-015 Anchor clause 20·log10(|1 + c·e^{iθ}| / |0.7 + c·e^{iθ}|) − 2 dB at p = 1 (c, θ from the
   Blend render); SC-008(d) 20·log10(|L_mode(p) + c·e^{iθ}|) − 20·log10(1) + 1 dB at p ∈ {0.5, 1} for Blend (L = 1 − 0.3p) and Punch (L = 1 −
   0.3p³). Measured margins from the same renders: E_sub(sum, Lock, 1) − E_sub(sum, Blend, 1) − 2 dB; E_sub(sum, mode, p) − F_lock(p) + 1 dB for
   every mode and p, F_lock(p) = `eSubDb` of the Lock render's Anchor tap; E_sub(Punch, p) − E_sub(Blend, p) + 0.1 dB. Print one row per cell and
   write `evidence/coherence-probe.txt`; last line `C9 VERDICT GREEN` if every margin ≥ 0, else `C9 VERDICT RED <cells>`. No REQUIRE on the margins
   (the main loop rules); `REQUIRE` only that every render produced finite taps.
2. `BassMorph_CpuEarlyProbe`: a random pair with every leaf differing and no governing amount 0 (seed `0x8018`, redrawn until true), 48 kHz, ramp
   2000 ms, target re-set 0 ↔ 1 on landing so every interval moves; 2·10^5 `advanceInterval()` calls timed with `std::chrono::steady_clock`, 3
   repetitions, median; print and write `evidence/cpu-early.txt`: `ns_per_interval`, `% of one core` = ns / 666 667 · 100, last line `R2 EARLY
   <GREEN|RED> <ns> vs 333.3`.

**`probe.sh`** (Git Bash; the `cpu-lane.sh` structure of Phase 7, `specs/profundum-phase7-stereo-field/evidence/close/cpu-lane.sh`): refuse to start
while `tasklist | grep -ciE "cl.exe|dsp_.*_tests.exe|clang-tidy"` is non-zero; run `dsp_systems_tests.exe "BassMorph_SumTapCoherenceProbe"` to
`evidence/run-log/close/probe-coherence.log`, then `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <exe> -ExeArgs "BassMorph_CpuEarlyProbe"`
to `evidence/run-log/close/probe-cpu.log`; append both verdict lines with wall clocks to `evidence/run-log/T008.txt`.

**Executor:** write the TU and the script, `bash -n` the script, and return status **"blocked"** — STOP AND SURFACE: "main loop builds
`dsp_systems_tests` (the Group 7 gate), runs `bash specs/profundum-phase8-bass-morph/evidence/close/probe.sh` alone, applies the decision rule,
and resumes with `unblock: [T008]`".

**Decision rule (main loop):** `C9 VERDICT GREEN` **and** `R2 EARLY GREEN` → proceed to Group 8. `C9 VERDICT RED` → STOP AND ASK the user with the
probe table and the remediation space the spec leaves (Q7: the showcase pair's B, e.g. its `anchor.level`, Fold amount or SHAPE; or the laws); no
bar is relaxed, and SC-005's own pair (1.0 → 0.7) does not change. A ruled change to `showcasePair()` is made in `bass_morph_state_helpers.h` and
the spec descriptor block (ruling in the row first) before Group 8. `R2 EARLY RED` → STOP AND SURFACE with the reading (R-2: escalate; the budget
is not changed).

---

## Group 8 — rendered criteria and perf (parallel; each task owns exactly one new TU; no header edits)

Every task below: FTZ / DAZ per case; notes and fs per rule 8; the showcase pair and the other named pairs from `bass_morph_state_helpers.h`;
renders through `BassMorphTest::renderMorphChain` / `renderMorphPoly`; evidence files as named. Depends on T008 unblocked.

### T009 [P] Chain criteria (SC-005(a)(d)(e) rendered, SC-006(b), SC-009, SC-010, SC-015, SC-023, SC-024 render, FR-039; plan C-9, C-12, S4.10)

**File:** `dsp/tests/unit/systems/bass_morph_chain_test.cpp`.

1. `BassMorph_RecipeDomain` `[sc-009]`: (a) `static_assert(!requires(BassMorphEngine& e, float* x, std::size_t n) { e.processBlock(x, x, n); })`,
   and the same for `e.process(1.0f)` and `e.processBlock(x, n)`. (b) `showcasePair()` recipes, p ∈ {0.25, 0.5, 0.75}: a `ProfundumCore` prepared
   at 48 kHz with E1 noted on, `setControls(resolved().state.recipe)` and one 32-sample block per interval until `shapeGains()` is bitwise unchanged
   for 8 consecutive intervals (cap 4 000 intervals → FAIL); then `shapeGains()` bitwise equal to `ProfundumTest::shapeOf(resolved.recipe,
   core.numPartials())` element by element. Positive control: core-tap renders of A (p = 0), B (p = 1) and p = 0.5; harmonics 1–16
   (`renderedHarmonicPowers`) of 0.5·A + 0.5·B differ from the p = 0.5 render by ≥ 1 dB in at least one harmonic.
2. `BassMorph_Global` `[sc-010]`: `renderMorphPoly` with f0s {E1, A1, E2, C3}, 0 → 1 jump at 0.1 s, default ramp, total 0.5 s, host blocks {7, 512}:
   `advanceSamples.size() == ⌈total / 32⌉`, each `% 32 == 0`; `perVoiceResolved[v][i]` `bitEqual` `perVoiceResolved[0][i]` for every v, i.
3. `BassMorph_Audibility` `[sc-015]`: `showcasePair()`, E1 and C3, 48 kHz, Blend: steady renders at p ∈ {0, 1} → D_cx, D_nl, D_st each move ≥ 3 dB,
   D_br ≥ 0.25 oct; gate renders at p ∈ {0, 1} → D_t(1) / D_t(0) ≥ 2; Lock and Blend steady renders at p = 1 → E_sub(sum, Lock) ≥ E_sub(sum, Blend)
   + 2 dB (subject to C-9; T008's verdict precedes this case). Write `evidence/sc015-audibility.csv`.
4. `BassMorph_DropLatched` `[sc-023]`: a harness with the engine and one `DropEnvelope` at 48 kHz on the grid (no core): A Drop 12 st / 200 ms / Shape
   0 / Rebound 0, B 36 st / 2000 ms / 1 / 0; at every grid point apply the four resolved Drop leaves through the setters, then (at sample 0) trigger
   (`trigger(true)`); `setPosition(1)` at 50 ms; per sample `process()` and record `currentOffset()`: bitwise equal for all samples < 2.5 s to an
   unmorphed envelope with A's values triggered at sample 0; a second trigger at 2.5 s (p landed at 1): from there bitwise equal to a fresh
   envelope with B's values triggered at the same sample, for 3 s.
5. `BassMorph_AmpEnvelopeImmediateApply` `[fr-039]` (plan S7.4 FR-039 row): the engine + one `ADSREnvelope` with the fixture's change-only setters
   (no core), 48 kHz, ramp `kMinPositionRampMs`, gate on at 0, morph 0 → 1 at 200 ms; pairs differing in one `ampEnv` leaf (decay 2000 ms both):
   (i) sustain 0 → 0.8 during Decay: at each grid sample the output step equals Δs·(1 − table(phase)) within 1e-5; (ii) the same sustain morph after
   Sustain is reached (morph at 3 s): max per-sample |Δg| < the per-interval Δs; (iii) decayCurve 0.7 → −0.7 during Decay: the step equals
   range·Δtable(phase) within 1e-5 (read `adsr_envelope.h:311–322` for range and table phase); (iv) decayMs 50 → 2000 during Decay: output bitwise equal
   to the static-A reference until the Sustain entry. For (i)–(iii) print and write to `evidence/amp-envelope-immediate.txt` the max |Δg| and its
   ratio to 10^(0.5/20) × the static reference's max |Δg| in the same window (recorded for Phase 9, not gated).
6. `BassMorph_EndpointEditMidMorphRender` `[sc-024]` (discrete render): A = B = the C-10 base (Tube, Amount 0.8, Anchor level 1.0), p held at 0.3,
   E1 and C3; an `EndpointA` event at 1.0 s with A′ = A with `characterAlgorithm` Fold. Assert `discreteSetterCalls` gains exactly one
   `setAlgorithm` entry at the first grid sample ≥ 1.0 s (g_sw). Reference: the same config at p = 0 with `EndpointA` and `EndpointB` = A′ at the
   same sample; refStart = A static, refEnd = A′ static. Window [g_sw, g_sw + `transitionEndSamples(algorithm)` + one period]: (a)
   `maxDeltaAround` (centre and half-width covering the window) on outL and outR ≤ 10^(0.5/20) × the reference's; (b)
   `differentialClicks(transition, refStart, refEnd, n, fs, f0).counted == 0` on both channels over [g_sw − 50 ms, window end].
7. `BassMorph_AnchorCurvesRendered` `[sc-005][long]` + `BassMorph_AnchorCurvesRenderedSmoke` `[sc-005]` (smoke: E1, p ∈ {0, 1}, all modes): steady
   renders E1, 48 kHz, 21 p × 3 modes on `anchorCurvePair()`: (a) Lock: E_sub(Anchor tap) max − min ≤ 0.05 dB over p; (d) the Anchor tap's w₁
   (`renderedHarmonicPowers`, relative to the p = 0 Lock render) within 0.1 dB of 20·log10(level(p)) in every mode; (e) `massOnlyPair()` in Lock: D_nl
   moves by ≥ 3 dB between p = 0 and p = 1. Write `evidence/sc005-rendered.csv`.
8. `BassMorph_IdempotentRender` `[sc-006][long]` + `BassMorph_IdempotentRenderSmoke` `[sc-006]` (smoke: one pair, Blend): seed `0x8066`, 8 random pairs
   constrained to `field.motionDepth` 0, `anchor.mode` ∈ {Lock, Follow}, Drop Amount 0 on both sides, × 3 modes, steady render, E1: A → B → A
   (0 → 1 at 0.25 s, back to 0 at 0.75 s, default ramp) vs A alone, same note-on and host blocks; t_land = first sample with p back at 0; read both
   on the output mid over [t_land + 1 s, + `kLowFrequencyFftSize`): harmonics 1–16 within 60 dB of the strongest agree within 0.1 dB.

### T010 [P] Discrete transitions click-free (SC-004(a)(b)(d) render; FR-034, FR-055; plan C-10, R-12, R-14; deviation 10)

**File:** `dsp/tests/unit/systems/bass_morph_click_test.cpp`.

- **Base state (C-10):** `MorphState{}` with `characterAmount` 0.8 and `anchor.level` 1.0 on both sides; the cycle cases add `field.motionSource =
  Cycle`, `cycleSync = true`, `motionDepth = 1`; the motion-source and cycleSync cases `motionDepth = 1`. Anchor morph mode **Blend** always.
  E1 and C3, 48 kHz, steady render, ramp {5, 40, 2000} ms; the 0 → 1 position event at 0.25 s; render length 0.25 s + rampMs + the transition end +
  one period + 50 ms.
- **Per (pair, note):** A-static and B-static renders (shared across ramp times; cache by (state, note)); per ramp: the transition render and the
  **direct-setter reference** (p held at 0; `EndpointA` and `EndpointB` events = A′ (A with the discrete field set to B's value) at the transition
  render's switch grid sample g_sw, read from `discreteSetterCalls`, or for field-motion selectors from the first interval whose resolved leaf
  changed). Window W = [g_sw, g_sw + `transitionEndSamples(kind)` + one period]. **(a)** max |y[n] − y[n−1]| over W (via
  `ShapeCharacterTest::maxDeltaAround` with centre and half-width covering W) on outL and outR ≤ 10^(0.5/20) × the reference's maximum over W.
  **(b)** `differentialClicks(transition, Astatic, Bstatic, n, fs, f0).counted == 0` on outL and outR over [g_sw − 50 ms, end of W]. Each cell
  appends `family,pair,note,ramp_ms,g_sw,ratio_L,ratio_R,clicks_L,clicks_R` to `evidence/sc004-<family>.csv`.
- **Shards** (`[sc-004][long]` each): `BassMorph_DiscreteClickFree_Algorithm` (30 ordered pairs), `_ImageMode` (6), `_AnchorWaveform` (20),
  `_AnchorMode` (12), `_FieldMotionSource` (2), `_FieldCycleSync` (2), `_FieldCycleModifier` (6, cycleNote Quarter), `_FieldCycleNote` (6, among
  {SixtyFourth, Quarter, QuadrupleWhole}, modifier None), `_GoverningAmount` (SC-004(d) render: the T004 rows — Character A Tube / 0 → B Fold / 0.8 and
  reverse, ImageMode spread 0 one side, Anchor waveform and mode level 0 one side — ramp 0 → 1 → 0 at the default ramp: `discreteSetterCalls` exactly
  one per direction at the intervals T004 test 7 names; each switch meets (a) and (b) against its direct-setter reference).
- **Smoke** `BassMorph_DiscreteClickFreeSmoke` `[sc-004]`: Tube → Fold, E1, default ramp, (a) and (b).

### T011 [P] Showcase trajectory (SC-008; FR-040, FR-033, FR-051; plan C-9, C-15, R-13; deviation 8)

**File:** `dsp/tests/unit/systems/bass_morph_trajectory_test.cpp`.

- Per (mode, note) shard `BassMorph_ShowcaseTrajectory_<Lock|Blend|Punch>_<E1|C3>` `[sc-008][long]` (6 shards): `showcasePair()`, 48 kHz, 21 static p
  (`initialPosition` = p, no automation). Steady render per p → D_cx, D_nl, D_st (f_c = the resolved `field.crossoverHz`), D_br, L_K, E_sub (sum tap
  mid), E_sub,out (out tap mid). Gate render per p (`gateRender`) → D_t; no gate render may reach 12 s. F_lock(p) = `eSubDb` of a **Lock** render's
  Anchor tap at the same p (a function-local static cache keyed by note; the Lock shard fills it, other shards compute it themselves if empty).
  The Punch shards also compute Blend's E_sub(p) (same cache pattern).
- Assertions: (a) each of D_cx, D_nl, D_st, D_t, D_br satisfies D(p_j) ≥ max_{i<j} D(p_i) − ε (ε 0.25 dB; 0.02 oct for D_br; for D_t, D(p_j) ≥ 0.98 ×
  the running max); (b) net travel D(1) − D(0) ≥ 3 dB for D_cx, D_nl, D_st, ≥ 0.25 oct for D_br, D_t(1) / D_t(0) ≥ 2; (c) L_K(p) ≥ min(L_K(0), L_K(1))
  − 1.5 dB; (d) in Lock E_sub(p) ≥ F_lock(p) − 1.0 dB, in Blend / Punch E_sub(p) ≥ F_lock(p) + 20·log10(L_m(p) / L_A) − 1.0 dB, at every p (spec
  SC-008(d), ruling B7-1; T015-3: this text predated the ruling, T011 implemented the ruled row), and in the Punch shards E_sub_Punch(p) ≥ E_sub_Blend(p) − 0.1 dB. A red in (c) or (d) is
  never fixed in the engine's output (spec Q7): it goes to T015 as `DEFECT:` with the per-p table.
- Write `evidence/sc008-trajectory-<mode>-<note>.csv` (`p,D_cx,D_nl,D_st,D_br,D_t,L_K,E_sub,E_sub_out,dyn_gain,F_lock`).
- **Smoke** `BassMorph_ShowcaseTrajectorySmoke` `[sc-008]`: Blend, E1, p ∈ {0, 0.5, 1}, assertions (a)–(d) on those three points.

### T012 [P] Mono compatibility along the morph (SC-016; FR-085)

**File:** `dsp/tests/unit/systems/bass_morph_monosum_test.cpp`.

- `BassMorph_MonoSum_<E1|C3>` `[sc-016][long]`: `showcasePair()`, Blend, 48 kHz: 21 static p, plus one render with a 0 → 1 event at 0.25 s at the default
  ramp (through SOLID → WIDE). Per render: `StereoFieldTest::bitEqual(taps.anchorL, taps.anchorR)` (structural: the fixture writes the mono Anchor
  to both channels). Per static p: `StereoFieldTest::evaluateMonoSumCell(taps, fieldCfg)` with `fieldCfg.field` = the resolved field at p, and
  `REQUIRE(StereoFieldTest::monoSumCellPasses(cell))` (Q_sub within 1.0 dB, ρ ≥ 0.99). For the ramp render read `evaluateMonoSumCell`
  (`bass_stereo_field_test_helpers.h:998–1050`) for its window, evaluate it with the landed state's field, and record in the run-log which span it
  covers.
- `BassMorph_MonoSum_AnchorThroughTail_<E1|C3>` `[sc-016][long]`: the same 21 positions and the ramp with `muteCharacter = true`: the output side-to-mid
  (`StereoFieldTest::bandReading(outL, outR, n, fs, 20, fs/2).sideToMidDb`) over the read window ≤ −120 dB (the floor) at every position;
  `bitEqual(outL, outR)` printed. Positive control: the character unmuted at p = 1 (WIDE, Spread 1) reads ≥ −60 dB.
- **Smoke** `BassMorph_MonoSumSmoke` `[sc-016]`: E1, p ∈ {0, 0.5, 1}, both reads and the positive control.
- Write `evidence/sc016-monosum-<note>.csv`.

### T013 [P] Anti-aliasing while morphing (SC-017; FR-086; plan C-16)

**File:** `dsp/tests/unit/systems/bass_morph_aliasing_test.cpp`.

- `BassMorph_Aliasing_<44k|48k|96k>` `[sc-017][long]`: E0, E1, E2. (a) `maxNonlinearPair()`, 21 frozen points (`initialPosition` = p, 1 s settle,
  steady render): `StereoFieldTest::wideAliasedDbfs(characterL|R, fs, f0, selected algorithm)` ≤ −80 dBFS per channel (the selected algorithm is
  `resolved().state.characterAlgorithm`, Fracture for every p ∈ (0, 1) by rule 2). Crossing arms on `algorithmCrossingPair()`: a fresh engine
  `reset()` at 0, `setPosition(kThetaUp − 0.005f)`, land → `REQUIRE(selection(Character) == A)`; `setPosition(kThetaUp)`, land →
  `REQUIRE(selection(Character) == B)`; each arm then a frozen steady render with `initialPosition` = that arm's p (a fresh engine's
  `reset()` keeps its initial A inside the band at θ_up − 0.005 and selects B at θ_up, FR-011; the test `REQUIRE`s the fixture's first
  `resolvedPerInterval` entry has the `characterAlgorithm` the landed engine resolved) under the same −80 dBFS bar. (b) recorded: a 1 s 0 → 1 ramp render (rampMs 1000) of
  `algorithmCrossingPair()`; Hann window [g_sw, g_sw + one period + 10 ms] at the recorded switch g_sw; non-harmonic in-band energy excluding
  ±(2 / T_window) of every k·f0, printed per (fs, note) and appended to `evidence/aliasing-crossfade.txt`; the same record for `maxNonlinearPair()`
  (switch at its first interval with p > 0). Write `evidence/sc017-aliasing-<fs>.csv`.
- **Smoke** `BassMorph_AliasingSmoke` `[sc-017]`: 48 kHz, E1, (a) at p ∈ {0, 0.5, 1} plus both crossing arms.

### T014 [P] CPU (SC-018) and the FR-083 worst-interval record; CPU lane script — **lane deferred to the main loop**

**Files:** `dsp/tests/unit/systems/bass_morph_perf_test.cpp`, `specs/profundum-phase8-bass-morph/evidence/close/cpu-lane.sh`.

- `BassMorph_Cpu` `[sc-018][performance][.perf]`: the T008 workload (every leaf differing, no governing amount 0, seed `0x8018`, 2000 ms ramp
  re-targeted 0 ↔ 1 on landing); 10^6 `advanceInterval()` calls, median of 3; `ns_per_interval ≤ 333.3` (0.05 % of 666.67 µs) → `CHECK`; print
  `SC018 VERDICT <GREEN|RED> <ns>` and write `evidence/sc018-cpu.txt`.
- `BassMorph_WorstTransitionIntervalRecord` `[fr-083][performance][.perf]`: `renderMorphPoly` 4 voices (E1, A1, E2, C3), a pair where every
  rendered discrete group (Character, ImageMode, Anchor waveform and mode, FieldMotion) switches at the same interval (rule 3, both amounts
  non-zero); per-interval wall time of the whole chain; print the worst interval's % of one core against Phase 5's 2.65 % crossfade cell and
  write `evidence/worst-interval.txt`. Not gated.
- `cpu-lane.sh`: the Phase 7 `cpu-lane.sh` structure (busy-check refusal; `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <exe> -ExeArgs
  "BassMorph_Cpu"`, then the same for `"BassMorph_WorstTransitionIntervalRecord"`; log `evidence/run-log/close/cpu-lane.log`; the verdict line
  appended to `evidence/run-log/T020.txt`).
- **Executor:** write the TU and the script, `bash -n` the script, return done with notes "deferred to the main loop at close: run `bash
  specs/profundum-phase8-bass-morph/evidence/close/cpu-lane.sh` last, alone (T020)".

---

## Group 9 — defect resolution and the handoff record (sequential)

### T015 Resolve every `DEFECT:` line, gate-documented red and wall-clock re-tag

- `grep -rn DEFECT specs/profundum-phase8-bass-morph/evidence/run-log/`; for each: write the failing reproduction (it usually exists), diagnose
  against the owning file, fix the owner, record here under "Defects found". Never relax a bar; a bar proven unreachable is STOP AND SURFACE.
- **Wall-clock re-tags:** from the Group 8 gate log (the main loop runs the gate with `-D 15` captured to a log), every per-push Phase 8 case over
  15 s that is toolchain-independent and not in rule 6's "never `[long]`" list gets `[long]` and a `…Smoke` sibling (deviation 5). Record each with
  its measured seconds; the T018 shard list is generated, so it picks them up.
- SC-008(c) / (d) or SC-015 reds after a green C-9 probe: STOP AND ASK with the per-p table (Q7 remediation space), never an engine-side fix.

### T016 Phase 9 / 11 / 12 handoff record (FR-083)

**File:** `specs/profundum-phase8-bass-morph/evidence/phase-handoffs.txt`. From plan S12, with the measured values filled in and each cited
(evidence file + line): the `advanceInterval` call order; the FR-054 Mass split; the change-only ADSR curve setters and the C-8 neutral-curve
decision; the FR-039 immediate-apply set (from `evidence/amp-envelope-immediate.txt`) and the completed S4.10 inventory
(`evidence/receiver-inventory.txt`); the fold-feed smoother (C-11); `AmpEnvelopeFields` / `MotionConfig` final for version 1; the worst-interval
record (`evidence/worst-interval.txt`, after T020); the MotionConfig-selectors click-free handoff gate; Drop morphs act on the next note; Phase 11:
201 × 2 + 20 bytes, restore order, IDs 100–199 and 1000–1099; Phase 12: the law / axis table, the curves, the Lock dimming UX, the two distinct Anchor
controls.

---

## Group 10 — integration (sequential; last)

### T017 CMake registration audit (single task)

- `grep -c` each of the eleven Phase 8 TU paths in `dsp/tests/CMakeLists.txt`: exactly 1 each, except `unit/systems/bass_morph_nonfinite_test.cpp` =
  2 (the list + the `-fno-fast-math` block); `bass_morph_perf_test.cpp` in no `set_source_files_properties` block. `dsp/CMakeLists.txt` lists
  `morph_state.h` and `bass_morph_engine.h` exactly once each, in the systems list. `git diff 964045a4 -- dsp/CMakeLists.txt dsp/tests/CMakeLists.txt`
  shows only added lines. Record in `evidence/run-log/T017.txt`. A missing or duplicated line is fixed here (the only task allowed to edit the CMake
  files after T001).

### T018 Suites lane, the phase's `[long]` lane and clang-tidy (FR-088, FR-089) — **deferred to the main loop**

**Files:** `specs/profundum-phase8-bass-morph/evidence/close/suites.sh`, `specs/profundum-phase8-bass-morph/evidence/close/tidy.sh`.

- `suites.sh` (copy the structure of `specs/profundum-phase7-stereo-field/evidence/close/suites.sh`, **with the scope rule of 2026-10-18**, Phase 6
  ruling G9-4; runs **detached**, writing `evidence/run-log/close/status.txt` with one line per lane — name, exit code, wall clock — so it survives
  the 2 h background-task ceiling):
  1. One rebuild of `dsp_systems_tests` (wall clock recorded; the count of `warning C|warning:` lines must be 0).
  2. **Change-set scope** (`BASE_COMMIT=964045a4`): `git diff --name-only $BASE_COMMIT -- dsp/include dsp/src plugins/shared tests/test_helpers` must
     be **empty**, and `git ls-files --others --exclude-standard -- dsp/include` must name only `dsp/include/krate/dsp/systems/morph_state.h` and
     `dsp/include/krate/dsp/systems/bass_morph_engine.h` (FR-088). Any modified tracked shared file is RED, written to the status file **with the
     consumer executables whose full suites it requires** (Ruinae, Seraphis, Vorago, Innexus, other `dsp_*_tests` per the file), and the script then
     also runs those consumers' full per-push suites (the task-format rule: the full suites of every consumer of a shared header the phase modified).
     With the expected diff no consumer suite runs.
  3. Concurrently:
     (a) the per-push lane of the phase's executable: `dsp_systems_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]" -D 15` to
     `evidence/run-log/close/perpush-dsp_systems_tests.log`;
     (b) the `[long]` lane of the **phase's own TUs only**: one shard per TEST_CASE in the list generated by `dsp_systems_tests.exe --list-tests
     "[bass-morph][long]~[performance]~[perf]~[benchmark]~[!benchmark]" --verbosity quiet` (never hand-kept; about 23 shards: 9 SC-004, 6 SC-008, 4
     SC-016, 3 SC-017, 2 chain, plus T015 re-tags), through `xargs -P <min(shards, cores − 4)>`, each shard to `evidence/run-log/close/long/<case>.log`.
     **Never every `[long]` case of the executable.**
  4. Summary block: per-shard verdict and wall clock (for the next run's sharding), the SC-004 worst ratio per family, the SC-008 (c) / (d) minimum
     margins per shard, the SC-017 worst dBFS per fs.
  A timing-budget assertion of an untouched (non-Phase-8) case that fails under this concurrent lane is recorded as load, not re-run (root CLAUDE.md).
- `tidy.sh` (copy the structure of `specs/profundum-phase7-stereo-field/evidence/close/tidy.sh`): reconfigure `build/windows-ninja` inside vcvars64
  (the test CMake list changed — a stale compile database gives phantom warnings), check all eleven Phase 8 TUs are in `compile_commands.json`, then
  `"/c/Program Files/LLVM/bin/clang-tidy.exe" -p build/windows-ninja <TU>` per TU, concurrently, with a 600 s ceiling each (the headers are checked
  through the TUs), to `evidence/run-log/close/tidy.log`, last line `TIDY GREEN|RED …`. Not the whole-`dsp` sweep.
- **Executor:** write both scripts and `bash -n` them; return done with notes "deferred to the main loop at close: after T015, run `suites.sh`
  detached and `tidy.sh` concurrently; quote each lane's last lines into `evidence/run-log/T018.txt`; any Phase 8 red is owned (fix, re-run only the
  affected shard or case)".

### T019 Portability and Apple globals (FR-087)

**Files:** `specs/profundum-phase8-bass-morph/evidence/close/portability.sh`, `evidence/run-log/T019.txt`.

- `portability.sh` (copy `specs/profundum-phase7-stereo-field/evidence/close/portability.sh`): concurrently, each with a 300 s ceiling, `node
  tools/lint-apple-globals.js` over `systems/morph_state.h`, `systems/bass_morph_engine.h`, `bass_morph_state_helpers.h`, `bass_morph_test_helpers.h`
  and the eleven TUs, and `node tools/check-portability.js` over the eleven TUs; then `wsl --shutdown`. Output `evidence/run-log/close/portability.log`,
  last line `PORTABILITY GREEN|RED`.
- **Executor:** write it and run it once (minutes at most). Both 0 findings → quote the log in `evidence/run-log/T019.txt` and return done. A finding
  in a Phase 8 file is fixed here (the group gate rebuilds). If the run fails with `wsl: Failed to translate` (the sandbox does not mount the repo
  drive) or exceeds 5 minutes, stop it and return done with notes "deferred to the main loop at close: run `bash
  specs/profundum-phase8-bass-morph/evidence/close/portability.sh` unsandboxed".

### T020 CPU lane alone (SC-018, FR-083 record) — **deferred to the main loop**

- Precondition: T018 and T019 lanes finished, machine idle. Executor: return done with notes "deferred to the main loop at close: run `bash
  specs/profundum-phase8-bass-morph/evidence/close/cpu-lane.sh` (T014) last, alone; quote the figures in `evidence/run-log/T020.txt` and in
  `evidence/phase-handoffs.txt` (T016)". A red `SC018 VERDICT` is re-run once alone after the machine has idled; only then is it a defect. Never relax
  the bar.

### Close procedure (main loop, in this order)

1. `probe.sh` alone at the T008 checkpoint (Group 7). A `C9 VERDICT RED` or `R2 EARLY RED` goes to the user before anything else runs.
2. Group 8 gate with `-D 15` captured to a log (T015's re-tag input).
3. `suites.sh` detached + `tidy.sh` concurrently (T018); `portability.sh` if T019 deferred it.
4. Any red → triage into "Defects found", fix, re-run only the affected shard or case.
5. `cpu-lane.sh` alone (T014 / T020), after the machine has idled.
6. Comply (outside this task list) quotes the evidence files and run-log readings; `evidence/phase-handoffs.txt` (T016) goes into the roadmap's
   Phase 9 / 11 / 12 notes.

---

## Defects found

*(T015 and sequential tasks append here: case, output, diagnosis, owning file.)*

**Pre-triaged (predicted by the plan or by Phase 7's record; not code defects until measured — gate fixers leave these alone):**

- **P-1 (plan C-9 / R-1):** SC-015's Anchor clause and SC-008(d) depend on the core's h1 coherence at the character tap; predicted red for c > 0.46
  in phase (SC-015) or c ≈ 0.7 near anti-phase (SC-008(d) Blend). Decided at the T008 checkpoint by measurement; a red is STOP AND ASK.
- **P-2 (Phase 7 G7-1 precedent):** SC-004(b)'s endpoint-only differential count (refStart = A static, refEnd = B static) counted detections inside
  the ~220 ms pan slew on Phase 7's dirty arm for SOLID ↔ SPREAD (Phase 7 `tasks.md`, "Defects found", G7-1). `_ImageMode` and the `field.motion*`
  shards of T010 may reproduce it on the C-10 base (Tube at Amount 0.8). If they do, the diagnosis (per-tap counts, intermediate-vector steady
  slopes) goes to the user as a ruling, as G7-1 did; (a)'s direct-setter ratio is unaffected. No bar is changed by a gate fixer.
- **P-3 (plan R-13):** SC-008(c) loudness dip > 1.5 dB mid-morph; remediated only in the laws or the showcase pair (Q7), escalated with the per-p table.
- **P-4 (plan R-2):** `advanceInterval` near the 333.3 ns bar (plan S10 estimates 165–285 ns); the T008 early timing decides before Group 8.

**Group 8 gate (fixer, 2026-10-19):**

- **G8-1 `BassMorph_AmpEnvelopeImmediateApply` (i): `CHECK(moving > 0)` red, 0 moving intervals, worst 5.56e-8.** Test defect (vacuous
  configuration), owner `bass_morph_chain_test.cpp`. At the C-8 default decayCurve 0.7 the decay table is 1 − phase^4.29, so at the
  200 ms morph (phase ≈ 0.095) Δs·(1 − table) ≈ 4e-6 per interval, below the 1e-5 tolerance: the comparison could not fail. Case (i) now
  runs both sides at decayCurve −0.7 (still a sustain-only pair; tasks.md T009 case 5 fixes no curve). Re-run: 8 moving intervals, worst
  3.56e-8. No bar changed.
- **G8-2 `BassMorph_AnchorCurvesRenderedSmoke` SC-005(e): `dNl1 − dNl0 >= 3` red, D_nl 0 → −6.44 dB.** READING, FLAGGED FOR THE USER.
  The spec row says D_nl "moves by ≥ the SC-015 bar"; T009 read it as signed. Measured (temporary probe, E1, Lock): Mass 0 → Fold 0,
  depth 1, body 0, both taps floored (D_nl 0); Mass 100 → Fold 61.3, depth 0, body 0.9, linear-core n ≥ 9 at +16 dB re its h1, and the
  Fold stage (compression regime, HarmonicFold kSoftEnd 30 … kCompressionEnd 65, peak-normalised) lowers it by 6.4 dB. The check now reads
  |D_nl(1) − D_nl(0)| ≥ 3 dB (bar unchanged; signed value still in sc005-rendered*.csv). If the user wants "rises" (clean → nonlinear
  sign), it is red by measurement and the remedy is the Mass-only pair or the FoldAmount row, not the bar.
- **G8-3 `BassMorph_IdempotentRenderSmoke` SC-006(b): pair 0 Blend worst 12.86 dB (h3).** Fixture defect, owner
  `bass_morph_test_helpers.h`. A = Fracture, B = Fold. Core and Fold taps matched the A-alone reference within 1e-3 dB; the character tap did
  not. The fixture never called `BassCharacterStage::syncPhase` (FR-072; roadmap Phase 5 handoff (2): the voice syncs at note-on), so the
  switch back to Fracture restarted its ÷2 phasor at 0 at the switch sample, unrelated to the core's phase. The fixture now calls
  `syncPhase(0)` at the note-on (core restarts at phase 0). Re-run: worst 0.0016 dB.

**T015 (2026-10-19):** `grep -rn DEFECT evidence/run-log/` names one `DEFECT:` line (T004.txt:157); T011 and T013 surfaced two further
text / record issues without a `DEFECT:` line; the Group 8 gate's G8-2 is re-opened. Gate input read: `fx_gate.log` (the post-fixer Group 8 run,
404 cases green; `# (e) massOnlyPair Lock D_nl p0,0,p1,-6.44339,travel,-6.44339,bar,3`).

- **T015-1 `BassMorph_LawTable_GoverningAmount` (T004.txt:157): text defect, owners `tasks.md` (T004 test 2) and `plan.md` (S7.4 SC-019 row).**
  "The other four groups' selections … stay A's throughout" contradicts rule 1 (S4.5, FR-034: B at q = 1 for every group); the test already
  asserts the rule-3 trajectory (T004 D3). Fixed in tasks.md T004 test 2. **plan.md S7.4's SC-019 row carries the same sentence and is not in
  T015's file list: it still needs the same one-line correction** (main loop). No code change, no bar changed.
- **T015-2 `BassMorph_AnchorCurvesRendered(Smoke)` SC-005(e) (re-opens G8-2): RED BY MEASUREMENT, STOP AND SURFACE.** The gate fixer turned
  `dNl1 − dNl0 ≥ 3` into `|dNl1 − dNl0| ≥ 3`. That relaxes the bar: D_nl is the directed "clean → nonlinear" axis (spec descriptor block, where a
  negative travel was ruled "could never travel"), SC-015 gates it signed (`BassMorph_Audibility`, `row("D_nl_dB", …, dNl1 − dNl0, 3.0)`), and
  T011 (b) reads it as "net travel D(1) − D(0)". T015 restored the signed check in `bass_morph_chain_test.cpp`. Measured: Lock, E1, D_nl(0) = 0
  (both taps floored), D_nl(1) = −6.44339 dB, travel −6.44 vs bar +3 (fx_gate.log; G8-2's probe: Mass 100 → Fold 61.3 inside HarmonicFold's
  compression regime, depth 0, body 0.9). The bar cannot be met inside Phase 8's files: the Mass-only pair is fixed by the spec (A Mass 0 /
  B Mass 100, all else default) and the FoldAmount row lives in Phase 6's frozen `mass_law.h` (FR-088). **Decision for the user:** (i) rule the
  spec's "moves by" as unsigned (restore `std::fabs`), (ii) redefine the Mass-only pair's other leaves (e.g. a non-default excitation / body so
  Mass raises n ≥ 9 power), or (iii) gate Mass on a different descriptor. Until ruled, SC-005(e) stays red; no engine change is possible or proper.
- **T015-3 tasks.md T011 (d): text defect, owner `tasks.md`.** The clause predated ruling B7-1; T011 implemented the ruled spec row (T011.txt
  "DEVIATION"). Text corrected to the ruled formula. No code change.
- **T015-4 SC-017(b) record degenerate (T013.txt): recorded, needs a ruling, no bar at stake.** With T = 1/f₀ + 10 ms the ±2/T exclusion exceeds
  f₀/4 for every f₀ ≤ 300 Hz, so every in-band bin is excluded and the record reads the numeric floor (excluded_fraction 1). SC-017(b) is
  recorded-only (covered by composition per the spec); a meaningful record needs a ruled window or exclusion width. Not changed here.
- **Main loop, 2026-10-19 (after the T015 stop):** B9-1 ruled by the user (SC-005(e) on Phase 6's SC-011 bar; chain test
  changed accordingly, D_nl recorded); T015-4 resolved as a reading (SC-017(b) window ≥ 8 periods); T015-1's plan S7.4
  sentence corrected; the `-D 15` per-push run is evidence/run-log/close/perpush-durations.log (51 cases green; only
  `BassMorph_SampleRates` over 15 s, 32.3 s, kept per-push as a bounded-grid sentinel).
- **Wall-clock re-tags: DONE by the main loop (see above); originally NOT DONE, input missing.** The close procedure step 2 log (Group 8 gate with `-D 15`) does not exist: neither
  `g8_run.log` nor `fx_gate.log` carries Catch2 duration lines (0 matches of `^[0-9.]+ s: `). Executors may not run tests, so no case was
  re-tagged. The main loop runs `dsp_systems_tests.exe "[bass-morph]~[long]~[performance]~[perf]~[benchmark]~[!benchmark]~[.]" -D 15` to a log;
  every Phase 8 case over 15 s outside rule 6's never-`[long]` list then gets `[long]` and a `…Smoke` sibling (T015 re-run, re-tag only).
- `morph_state.h` and `bass_morph_engine.h`: no `DEFECT:` line or gate red names either; unchanged.
