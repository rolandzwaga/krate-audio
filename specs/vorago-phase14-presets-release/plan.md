# Implementation Plan: Vorago Phase 14 — Factory Presets & Release Readiness

**Spec:** `specs/vorago-phase14-presets-release/spec.md` (reviewed; Clarifications session 2026-09-27, Q1–Q8)
**Roadmap:** `specs/Vorago-roadmap.md` Part B, Phase 14 (the variety paragraph and the 2026-09-27 Q2 ruling)
**Branch:** `feat/vorago-phase1-events-modulation` (one branch per roadmap)
**Date:** 2026-09-27
**Status:** PLAN — no code written. The execution order has two stop-and-surface gates: G1, roster
ratification (FR-071), and G2, the frozen F (FR-017a). Section 2 lists nine findings from this
session's reading. Each one constrains or amends the spec, and the user must acknowledge them at G1.

Every "file:line" below was read in this planning session. Anything quoted from the spec says so.

---

## 0. What was read (this session)

| Area | Files / lines |
|---|---|
| Roadmap | `specs/Vorago-roadmap.md`: Phase 10 success criteria through the end of the file (Phase 14 text, the Q2 ruling paragraph, the dependency graph, the cross-cutting constraints) |
| Spec | `spec.md` in full (C-1…C-11, FR-001…FR-076, SC-001…SC-030, edge cases, OQs, clarifications) |
| Plugin | `plugins/vorago/CLAUDE.md`; `src/plugin_ids.h:1-244`; `src/parameters/ecosystem_params.h:1-118`; `param_routes.h:36,63-122,175-230` + the static_asserts; `macro_params.h:1-70`; `envelope_params.h:36-80`; `space_params.h:42,58-63,200-217`; `body_params.h:41-60`; `sub_params.h:36-40`; `events_params.h:34`; `life_params.h:32-34`; `noise_params.h:79`; `global_params.h:43,111,120`; `param_mapping.h:29-62,82-115`; `src/preset/vorago_preset_config.h:1-46`; `src/processor/processor.h:80-230`; `src/processor/processor.cpp:293-330,560-745,796-870,996-1035,1095-1113`; `src/controller/controller.cpp:94-109,130-240`; `resources/editor.uidesc:111,370-410`; `version.json` |
| Plugin tests | `tests/CMakeLists.txt` (whole file); `tests/vorago_test_fixture.h:1-335`; `tests/phase11_reference_chain.h:1-80`; `tests/integration/soak_test.cpp:40-47`; `tests/integration/processor_cpu_test.cpp:78-95`; `tests/unit/controller/editor_layout_test.cpp:9-10,109,240-262,827-1127,1305-1348` (count anchors) |
| DSP | `systems/ecosystem_engine.h:143-300,440-740,2340-2440`; `systems/vorago_voice.h:80-100,155-172,310-340,1400-1445,1514,1530-1546,1702-1713`; `systems/vorago_engine.h:140-191,360-380,850-905,1280,1297,1302-1303`; `systems/vorago_macro_matrix.h:198-235,344-356,515-548,678-690,975-1015,1125-1145`; `systems/atmosphere_engine.h:305-315,855-859`; `systems/resonance_drift_network.h:293`; `systems/feedback_ecology.h:597`; `dsp/tests/unit/systems/vorago_perf_budget.h:70-90`; `dsp/tests/unit/systems/vorago_param_surface_test.cpp` (TEST_CASE list); `dsp/tests/CMakeLists.txt:498-551` |
| Test helpers | `tests/test_helpers/render_fingerprint.h:40-130`; `vorago_fixtures.h:100-212,232-350,425-560`; `low_frequency_metrics.h:478-499`; `allocation_detector.h:105-175`; `vst_param_changes.h` / `vst_event_list.h` (include lines, class list) |
| Shared | `plugins/shared/src/preset/preset_manager.h:50-125`; `preset_manager.cpp:90-105,240-290` |
| Seraphis precedent | root `CMakeLists.txt:590-675`; `tools/seraphis_preset_defs.h:1-120,1272-1291`; `tools/seraphis_preset_generator.cpp` (whole file, code lines); `plugins/seraphis/tests/preset_test_support.h` (outline, `:130-165`); `plugins/seraphis/tests/unit/preset/factory_preset_test.cpp` + `integration/preset_render_sweep_test.cpp` (TEST_CASE list) |
| Tooling / CI | `tools/check-preset-generator-determinism.js:1-120,206-209`; `.claude/workflows/release-readiness.js:1-40`; `.claude/skills/release/SKILL.md:10-40`; `.github/workflows/long-tests-nightly.yml` (whole file); `.github/workflows/ci.yml:263-376,540-608,655-660,800-808,1008-1020,1068-1121`; `.github/workflows/release.yml:145-181` |

---

## 1. Execution order (gated)

```
A  Probe (FR-070)              hidden [.probe] case; 2 friend lines + 1 forward declaration in dsp (P-1);
                               preceded by preset_test_support.h part 0 (descriptor + distance,
                               §5.4–§5.5) so the probe includes the shared math (ruling R-8)
   -- G1 STOP: surface the probe table, proposed roster R and P-1..P-9 -> user ratifies (FR-071) --
B  Surface (FR-072..074)       dsp forwarders + VoragoVoiceParams fields; IDs 901..; state v3;
                               controller; uidesc page-6 r0; every count-bearing test updated
   + getTailSamples (FR-060)   independent of R; done here because it touches processor.h/.cpp
C  Preset infrastructure       categories (FR-001), PresetHost, defs types + Capability enum,
                               generator + CMake targets, per-push harness (FR-018..035, 039, 040)
D  Sweep harness               records, sharding, thread pool, arms, descriptor, verification vector
E  Pilot (FR-017a)             6-8 presets through D; measure t_max and a near-variant pair; rule F
   -- G2: record F (and STOP if any seed twin > F/2, C-7.3(b)) --
F  Library                     matrix -> primaries -> secondaries -> categories -> author -> sweep loop
G  Deferred + docs             FR-061 record, FR-062 docs/index.html, plugins/vorago/CLAUDE.md
H  Tooling + CI                determinism --plugin, rosters, nightly sweep jobs, ci.yml filter
I  Release gate                version 1.0.0 + CHANGELOG, release-readiness, auval evidence,
                               SC-013 CI measurements, FR-042 listening checkpoint,
                               FR-026/FR-027 install-path + installer-text verification (§8)
```

Nothing in B–I that references `R` is written before G1 (FR-071). C's code that does not reference
`R` (host, generator skeleton, container tests) can be written before G1. No preset file exists
before B lands, because the pilot presets are v3.

---

## 2. Findings that constrain or amend the spec (surface at G1)

**P-1 — Wiring `R` needs an append-only `dsp/` edit, and the probe needs a friend.**
`VoragoVoice` owns `EcosystemEngine ecosystem_` privately (`vorago_voice.h:2305`). It exposes only a
const accessor (`[[nodiscard]] const EcosystemEngine& ecosystem() const noexcept`, `:1514`). The only
mutation the voice ever makes is `ecosystem_.setSeed(...)` (`:1546`). `VoragoEngine` exposes its voices
only as const (`getVoice(std::size_t) const`, `vorago_engine.h:1280`). Its per-voice broadcast is
`applyVoiceParams(const VoragoVoiceParams&)` (`:859-879`) over the 31-field `VoragoVoiceParams`
(`:160-191`, `kFieldCount = 31`). No macro row targets a rule knob: the Life row writes only
`EcosystemDepth` (`vorago_macro_matrix.h:624-635`). FR-072 says each knob is "wired to its existing
`EcosystemEngine::set*` setter", and that is impossible from `plugins/` alone. The smallest change
needed is below. All of it is append-only and adds no new class, so FR-076 still holds.
- Stage A: one forward declaration `struct VoragoEcosystemRuleProbe;` in the `detail` block of
  `vorago_voice.h` (`:155-170`), following the B-4 pattern ("DEFINED IN THE TEST TU"). Also one
  `friend struct detail::VoragoEcosystemRuleProbe;` in `VoragoVoice` (beside `:1530-1534`) and one in
  `VoragoEngine` (beside `:1302-1303`). Inert.
- Stage B: per ratified knob, one `VoragoVoice` forwarder, one `VoragoVoiceParams` field and one line
  in `applyVoiceParams`. `kFieldCount` goes from 31 to 31 + |R|.
Recommendation: accept this as the necessary consequence of Q2. FR-017's "never edit `dsp/` to pass"
does not apply: this wires a ratified surface and does not retune a preset to pass a test.

**P-2 — Factory presets will be v3, not "v2, 428 bytes".** FR-006, SC-005 and C-9 say "first int32 ==
2, length == 428". FR-072 sets `kCurrentStateVersion = 3` before any preset exists (FR-071), and the
generator writes whatever `Processor::getState()` writes (C-3). The plan therefore checks
`== kCurrentStateVersion` and `== kStateV3Bytes` (= 428 + 4·|R|, §4.2) wherever the spec names 2 and
428. The spec text needs an amendment.

**P-3 — With two cells per knob and a 4–6 knob roster, N cannot fall in [35, 45].** Primaries are
unique (FR-011). The required primaries are 10 S + 12 M + 5 E + |E-ext| + the non-default-state
materials + the non-default D8/D9 extremes. Facts about the default surface:
- it stores Material A StoneChamber and Material B SteelTank (`body_params.h:44-45`) with body mix 1.00
  (`:41`);
- its slots cover all four noise models (`vorago_engine.h:167-169`), with noise at −18 dB
  (`noise_params.h:79`);
- its stage times sum to 155 s (`vorago_voice.h:320-322`).

From these, the predicted default-state cells are D1.StoneChamber, D1.SteelTank, D3.1–D3.4, D8.1 and
D9.2. FR-012's default-surface run confirms or corrects this. FR-011 makes default-state cells
**ineligible** as primaries. The floor is therefore 10 + 12 + 5 + 9 materials + D8.2 + D9.1 =
**38 + |E-ext|**. With two cells per knob and |R| ≥ 4, that is at least 46, which exceeds 45.
Options:
- (a) raise SC-029's ceiling to 38 + |E-ext|;
- (b) prefer one-sided knobs (P-4), so that |E-ext| ≤ 7;
- (c) ratify |R| = 3.

Plan default: (a), N = 38 + |E-ext|, recorded as a ruling.

**P-4 — Knobs whose default sits at a range end have only one extreme.** `syncRate_ = 0.0f` in `[0, 0.5]`
and `feedRate_ = 0.0f` in `[0, 1.0]` (`ecosystem_engine.h:2386,2391`). For these knobs the `.lo`
extreme equals the registered default, so resetting the knob to its default changes nothing and the
cell can never verify. Plan: such a knob contributes one cell, `E{n}.hi`. A knob with its default
inside the range contributes `.lo` and `.hi`. This is ratified together with R at G1.

**P-5 — SC-029's list of required primaries collides with FR-011.** SC-029 (and Q4) list "the 11 body
materials, 4 noise models and D8/D9 envelope extremes" as primaries. FR-011/C-2.2 forbid default-state
primaries. The plan follows FR-011 (hard rule) and reads SC-029 as "each non-default-state member".
The default-state members are still covered as cells (FR-013), and only by **factory presets**: each
one is attached as a verified secondary of some preset (§6 step 4). The default surface marks cells
as ineligible for primary and nothing else (C-2.1: "a pseudo-preset for this purpose only"). It never
counts toward coverage (§5.10).

**P-6 — FR-036's "one process" is met by records, not by re-rendering.** The unsharded job loads every
preset's descriptor and verification vector from the shard jobs' record artifacts into one process.
It evaluates C-7.3, FR-011a and FR-013 there, and renders only the control twins. Re-rendering every
preset in one job would not fit in 60 % of 180 min on the CI runners (§9).

**P-7 — `Comment` is written unescaped.** `PresetManager::savePreset` writes
`value="<description>"` without XML escaping (`preset_manager.cpp:271-274`). The generator matches
that byte for byte (FR-003), so descriptions MUST NOT contain `"`, `&`, `<` or `>`. SC-002's test
asserts this.

**P-8 — The D10.1 render is the freeze-gesture render.** D10.1 is a secondary of the S8 primary preset
(§6). That preset's single C-6 render sends Freeze On at `A + 65 s`. Arms 1–3 and its descriptor use
that render, and arm 4 uses the Freeze-On criteria (§5.3). Its `Sus` window ends when the gesture
arrives, so its ablation twins are unaffected. D10.1 cannot be a primary: its reversion (no gesture)
sounds identical over `Sus`, so there is nothing to measure against the primary bar.

**P-9 — The per-push cost of the short guard (FR-034) is unmeasured and may be large.** Per preset it
renders about 28 s of single-note-equivalent audio: 8 s at 48k, 4 s at 44.1k and 4 s at 96k (≈ 8 s
equivalent together), plus the 8 s chord with 4 voices. Across ~45 presets, §9 estimates 3–5 min
locally, and CI runners are 2–3× slower. The gate is SC-013's 80 % clause on the shared step. The
lever is a 2-thread pool inside the case (§4.8). The cost is measured before C merges.

---

## 3. Verified reuse inventory

| Reused | Where (read this session) | Real signature / fact used |
|---|---|---|
| Processor state I/O | `processor.cpp:569`, `:637-661` | `tresult PLUGIN_API setState(IBStream*)`; `tresult PLUGIN_API getState(IBStream*)` — version, v1 block, `saveGlobalParamsV2Ext`, 14 packs in band order |
| v1 default-tail pattern | `processor.cpp:595-627`; `controller.cpp:209-235` | a lower version loads the default-constructed packs' tail through a stack `MemoryStream`. The plan reuses this verbatim for "v2 loads R at defaults" |
| Param dispatch | `processor.cpp:667-734` | band dispatch `id < kEcosystemParamRangeEnd` → `handleEcosystemParamChange`, then `markDirty(id)` bumps `voiceParamGeneration_` for `Route::VP` (`:796-801`); parameter changes are latched first in `process()` (`:295-297`) |
| VP broadcast | `processor.cpp:826-863` | `pushVoiceParams()` builds `Krate::DSP::VoragoVoiceParams p{}` from pack atomics, then `engine_->applyVoiceParams(p)` |
| Output stage | `processor.cpp:1098-1113` | `renderSlice`: engine → cavern → `renderGainAndOutputStage` (multiply by gain, then `processOutputStage`) |
| Macro vector | `processor.cpp:1010-1030` | `VoragoMacroValues buildMacroVector() const noexcept` (knobs, plus channel pressure on Pressure) |
| Test seams | `processor.h:116-120` | `const Krate::DSP::VoragoEngine* engineForTest() const noexcept`, `const CavernVerb* cavernForTest()` |
| Tail default | `processor.h:89` | "getTailSamples(): NOT overridden -> SDK default kNoTail" |
| Route asserts | `param_routes.h:36`, asserts after `:231` | `std::array<ParamRouteEntry, 108> kParamRoutes`; `countRoute(Route::VP) == 31` |
| State constants | `plugin_ids.h:23`, `:28-47` | `kCurrentStateVersion = 2`, `kStateV2Bytes = 428` (static_assert sum) |
| Ecosystem band | `plugin_ids.h:172-173`, `:236` | only `kEcosystemDepthId = 900`; `kEcosystemParamRangeEnd = 1000` |
| Ecosystem pack | `ecosystem_params.h:32-116` | `struct EcosystemParams { std::atomic<float> depth{0.85f}; }`; `handle/register/format/save/load/loadToController` shape |
| Tapers | `param_mapping.h:29-62` | `linearFromNormalized/linearToNormalized(double, mn, mx)`; `offsetLogFromNormalized/ToNormalized(n, mn, mx, eps)` (log over `[mn+eps, mx+eps]` via `Krate::Plugins::logMapFromNormalized`); `indexFromNormalized(n, count)` = `round(n·(count−1))`; `indexToNormalized(i, count)` |
| Seeds | `param_mapping.h:82-87`, `global_params.h:43,120` | 16 seeds, default index 0 → normalized `i / 15` |
| Rule knobs | `ecosystem_engine.h:469-707`, defaults `:2372-2412` | 22 setters; each rejects non-finite values, clamps, then stores (FR-064); defaults include `predation_ = 0.55f` [0,1], `syncRate_ = 0.0f` [0,0.5], `kernelSigma_ = 0.03f` [0.01,0.35]; `setAffinity(Kind from, Kind to, float v)` is not symmetrised, range `[-2, 2]`, default −1 on the diagonal and +0.45 elsewhere |
| Engine voice params | `vorago_engine.h:160-191`, `:859-879` | `struct VoragoVoiceParams` (trivially copyable, `kFieldCount = 31`); "NO FIELD HERE MAY NAME A VoragoMacroTarget" (`:152-154`); every forwarder early-outs an unchanged value (`:852-855`) |
| Ghost grain | `vorago_engine.h:370`, `:1297`; `atmosphere_engine.h:311`, `:855-859` | `atmos_.setGrainSeconds(12.0f)` at prepare, never rewritten; `const AtmosphereEngine& atmosphere() const noexcept`; `float getGrainSeconds() const noexcept` |
| Macro matrix | `vorago_macro_matrix.h:979-994`, `:1006-1013`, `:1129-1140` | `void setMacros(const VoragoMacroValues&)`, `void setTargetBase(VoragoMacroTarget, float)`, `VoragoCavernTargets computeCavernTargets() const noexcept`; the Age decay row is −14 and the Depth decay row +25 (`:348-352`, `:682-686`) |
| Envelope | `vorago_voice.h:320-338`; `envelope_params.h:36-67` | stage defaults `{20000, 30000, 45000, 60000, 0, 0}` ms, release 45000 ms, growth 120 s; `enum class EnvelopeMode { Standard = 0, Growth = 1 }` |
| Cavern decay | `space_params.h:42`, `:58-59` | default 20 s, range [0.5, 60] s (`kSpaceDecayMinSeconds`, `kSpaceDecayMaxSeconds`) |
| Enum orders | `resonance_drift_network.h:293`; `feedback_ecology.h:597` | `AnchorMode { Free, Keyed, Hybrid }`; `FilterMode { Lowpass, Bandpass, Highpass }` |
| Preset manager | `preset_manager.h:55-61`, `:70`, `:75`, `:120`; `.cpp:95-103`, `:264-276` | 5-argument constructor with user/factory overrides; `scanPresets()`; `getPresetsForSubcategory(const std::string&)`; `static bool isValidPresetName(const std::string&)`; exact-match subcategory; the Info XML attributes plus an optional `Comment` |
| Test host pieces | `vst_param_changes.h:78-118`; `vst_event_list.h:36-80` | Catch2-free `Krate::Test::ParameterChanges::addChange(ParamID, double)`; `EventList::addNoteOn(pitch, velocity, offset)`, `addNoteOff(pitch, offset)` |
| Fixture (NOT reusable by the tool) | `vorago_test_fixture.h:38`, `:186-202` | includes Catch2 and calls `REQUIRE` in its constructor and `prepare` (spec finding 3) |
| Metrics | `vorago_fixtures.h:245`, `:316`, `:344`, `:441`, `:502`; `analysisFftSize` `:109-116` | `bandEnergyDb(span, sr, lo, hi)`; `crestFactorDb(span)`; `blockRmsDb(span, blockLen)` (floors at −240 dBFS); `perBandTotalVariation(span, sr)`; `perBinMagnitudeFlux(span, sr)`; FFT ≤ 8192 |
| Correlation | `low_frequency_metrics.h:478` | `float calculateCorrelation(const float* a, const float* b, std::size_t n)` |
| Fingerprint | `render_fingerprint.h` | `RenderFingerprint fingerprintRender(std::span<const float>)`; `FingerprintComparison compareFingerprints(actual, reference, kMetricTolerance = 2.5e-4, kSampleTolerance = 5e-4f)`; `.withinTolerance()` |
| Allocation | `allocation_detector.h:111-175` | `AllocationScope` (global); `ThreadScopedAllocationScope` (constructing thread only; the count is latched in the destructor) |
| Stimulus constants | `soak_test.cpp:44-45`; `processor_cpu_test.cpp:82-86` | `kVelocity100 = 100.0f / 127.0f`; `kOutputCeiling = 0.9661f`; `kCpuNotes{36, 40, 43, 47}`, `kCpuPolyphony = 4` |
| CPU budget | `vorago_perf_budget.h:78`, `:82` | `kBlockBudgetNs = 512/48000·1e9`; `kReferenceNs = kBlockBudgetNs * 0.30` (3 200 000) |
| Generator precedent | root `CMakeLists.txt:604-669`; `seraphis_preset_generator.cpp:86,122-126,224-265,280-349` | compiles processor.cpp + 5 SDK sources + the stub; `void* moduleHandle = nullptr;`; `kProcessorUID.toString(buf)`; 48-byte header (`VST3`, version 1, class id, list offset), then `Comp` + `Info` + `List` |
| Probe-friend precedent | `vorago_voice.h:155-170`, `:1530-1534`; `dsp/tests/unit/systems/vorago_nonfinite_test.cpp:74` | B-4: the friend struct is declared in the header and DEFINED in the test TU |
| DSP forwarder tests | `dsp/tests/unit/systems/vorago_param_surface_test.cpp:830`, `:1086-1096` | `VoragoVoice_Phase12Forwarders`, `VoragoEngine_ApplyVoiceParamsDefaultIsNoOp{_Short}`, `..._ReachesAllSlots` — extended, not copied |

Checked and not reused: `phase11_reference_chain.h` (`:6-80`) mirrors the **Phase 11** processor. The
probe renders through the shipped processor instead (§4.3).

---

## 4. Component design

### 4.1 DSP append (Layer 3, `dsp/include/krate/dsp/systems/`) — P-1

**Stage A (inert):**
```cpp
// vorago_voice.h, detail block (:155-170)
/// Phase 14 FR-070's rule-knob audibility probe. B-4: DEFINED IN THE TEST TU.
struct VoragoEcosystemRuleProbe;
// VoragoVoice, beside :1530-1534
friend struct detail::VoragoEcosystemRuleProbe;   // Phase 14 FR-070 (B-4)
// VoragoEngine, beside vorago_engine.h:1302-1303
friend struct detail::VoragoEcosystemRuleProbe;   // Phase 14 FR-070 (B-4)
```
ODR sweep (`grep -rn -E "(class|struct) VoragoEcosystemRuleProbe\b" dsp/ plugins/ tools/ tests/`): 0 hits.

**Stage B (after G1), per knob `k ∈ R`**, named after the engine setter. For predation:
```cpp
// VoragoVoice (public, beside setEcosystemDepthFor, vorago_voice.h:1430)
/// Phase 14 FR-072: forwards to EcosystemEngine::setPredation (ecosystem_engine.h:491).
/// Early-outs an unchanged value (the applyVoiceParams contract, vorago_engine.h:852-855).
void setEcosystemPredation(float v) noexcept {
    if (!detail::isFinite(v) || v == ecosystem_.getPredation()) { return; }
    ecosystem_.setPredation(v);
}
[[nodiscard]] float getEcosystemPredation() const noexcept { return ecosystem_.getPredation(); }
```
- Grouped affinity candidates, if ratified: `setEcosystemSelfAffinity(float)` writes
  `setAffinity(k, k, v)` for the five kinds, and `setEcosystemCrossAffinity(float)` writes the 20
  off-diagonal entries. Their getters read entry `(Partial, Partial)` and `(Partial, Resonator)`.
- `VoragoVoiceParams` gains `float ecosystemPredation = 0.55f;` (and so on), appended after
  `ecologyLoopFilterMode` (`vorago_engine.h:183-186`). Each default MUST equal the engine's member
  initializer (`ecosystem_engine.h:2372-2412`), so that broadcasting a default-constructed instance
  stays a no-op (`:156-158`). A test compares them against `EcosystemEngine{}.get*()` on a
  heap-constructed engine.
- `applyVoiceParams` gains `voice.setEcosystem<Knob>(p.ecosystem<Knob>);` inside the slot loop.
- `kFieldCount` goes from 31 to 31 + |R|, and its comment arithmetic is updated.

RT safety: every forwarder is O(1) except `setKernelSigma`, which calls `refreshKernelDerivatives()`
(`:473`). That is a few `exp` calls with no allocation, and it runs only on a changed value because of
the early-out. Knobs survive `prepare()` (see the `affinity_` comment at `:2398-2400` and FR-064's
"prepare() re-derives state, never configuration" at `:461-463`). `pushAllSurfaces(Scope::Reprepared)`
re-broadcasts them regardless.

Seraphis is unaffected: `VoragoVoice` and `VoragoEngine` are Vorago-only, and `EcosystemEngine` is not
edited.

### 4.2 Plugin surface (Stage B)

**IDs** (`plugin_ids.h`, ecosystem band): `kEcosystem<Knob>Id = 901, 902, …` in ratification order.
Names come from the setter (`kEcosystemPredationId`, `kEcosystemSyncRateId`, `kEcosystemKernelSigmaId`,
…). Each is a plain `Steinberg::Vst::Parameter` (continuous) with `kCanAutomate`. No list types.

**State v3:**
```cpp
constexpr Steinberg::int32 kCurrentStateVersion = 3;
constexpr std::size_t kStateV2Bytes = 428;                      // unchanged, still asserted
inline constexpr std::size_t kNumEcosystemRuleParams = /* |R| */;
constexpr std::size_t kStateV3Bytes = kStateV2Bytes + 4 * kNumEcosystemRuleParams;
```
The v3 block is **appended after the life pack**, following the plugin CLAUDE.md rule: "The next
format change appends after byte 428 — it never rewrites v2". It does NOT go inside the ecosystem
pack, whose 4-byte slot sits mid-stream.

**Pack** (`ecosystem_params.h`): `EcosystemParams` gains one `std::atomic<float> <knob>{<engine default>}`
per knob. The new functions follow the shape of `saveGlobalParamsV2Ext`, the existing precedent:
```cpp
inline void saveEcosystemParamsV3Ext(const EcosystemParams&, Steinberg::IBStreamer&);
inline bool loadEcosystemParamsV3Ext(EcosystemParams&, Steinberg::IBStreamer&);   // EOF-safe; isFinite + clamp
template <typename SetParamFunc>
inline void loadEcosystemParamsV3ExtToController(Steinberg::IBStreamer&, SetParamFunc);
```
`handleEcosystemParamChange`, `registerEcosystemParams` and `formatEcosystemParam` each gain one case
per knob.

Taper per knob:
- `linearFromNormalized` over the setter's clamp range by default;
- `Krate::Plugins::logMapFromNormalized` / `logMapToNormalized` (the plain log taper
  `param_mapping.h:37` names) for ranges that span at least a decade with a non-zero minimum
  (`kernelSigma` [0.01, 0.35], `maxSpeed` [0.001, 0.05]).

The registered range IS the setter's clamp range, so no stored value is one the engine would clamp
again. The registered default normalized value is the inverse taper of the engine default.

**Routes** (`param_routes.h`): one `{kEcosystem<Knob>Id, Route::VP}` row per knob, in ascending ID
order. `std::array<ParamRouteEntry, 108>` becomes `108 + |R|`, and `countRoute(Route::VP) == 31`
becomes `31 + |R|`. VP is the right route: the knobs are per-voice, broadcast, and not macro targets,
so the `VoragoVoiceParams` rule (`vorago_engine.h:152-154`) holds.

**Processor:** `pushVoiceParams()` copies each knob atomic into `p.ecosystem<Knob>`. `getState()`
appends `saveEcosystemParamsV3Ext(ecosystemParams_, s)` after `saveLifeParams`. `setState()`:
```
version > 3                 -> kResultFalse (unchanged rule)
v1Complete = loadGlobal && loadMacro
version >= 2 && v1Complete  -> v2Complete = loadV2Tail(s)
version >= 3 && v2Complete  -> loadEcosystemParamsV3Ext(ecosystemParams_, s)   // truncation-safe
version <  3                -> load the DEFAULT v3 block through a stack MemoryStream (the :603-626 pattern)
                               -> every knob in R at its hard-coded default (FR-072)
version <  2                -> the existing v2 default path, then the same default v3 block
```
Resetting R to defaults on a v2 or v1 load is what FR-072 asks for. It mirrors the existing v1
behaviour (`processor.cpp:597-600`: "leaves every Phase 12 field at its REGISTERED DEFAULT, whatever
the previous state held"). The **controller**'s `applyStateStream` mirrors it
(`controller.cpp:186-235`).

**UI** (`resources/editor.uidesc`, page-6, `:386-387`): row r0 holds EventsRateScale (x 18) and
EcosystemDepth (x 86). The page is 1100 px wide with 68-px cells, so knob `k` of R goes at
`origin = (154 + 68·k, 4)` with its label at `(142 + 68·k, 52)`. Each is a 44 × 44 `ArcKnob` with the
same attributes as `:387-388`, plus one `control-tag` row per ID beside `:111`. Row r0 remains one
functional group (Events + Ecosystem). The edit is made with an XSLT stylesheet via `xslt3`, the
project rule for changes to more than 3 controls.

**Count-bearing tests to update** (sweep: `grep -rn "108\b\|106\b\|\b90u\|428\|kStateV2Bytes\|kCurrentStateVersion" plugins/vorago/tests`):
- `editor_layout_test.cpp`:
  - `kIdNames` size 108 (`:109`);
  - the page map at `:252-256` adds each ID to `pages[6]`;
  - the counts `all108`, `nonHidden 106`, `boundIds 106`, `tagMap 106`, `checked 106`,
    `pageUnion 90` and `getParameterCount 108`.
- `param_table_expected.h` + `param_table_test.cpp`.
- `param_denorm_test.cpp`, `automation_rt_test.cpp`, `continuity_test.cpp`, `ecosystem_frame_test.cpp`,
  `param_surface_test.cpp`, `preset_browser_test.cpp`, `processor_cpu_test.cpp`, `soak_test.cpp`,
  `body_params_test.cpp`.
- `state_v2_test.cpp`: becomes the legacy-v2 load test, and its round-trip moves to v3.
- `unit/state_roundtrip_test.cpp`: its size and byte-compare assertions on the **current** stream
  (`getSize() == kStateV2Bytes` at `:242`, `:249`, `:258`, `:296`, `:376`; `memcmp(..., kStateV2Bytes)`
  at `:250`) become `kStateV3Bytes`. `:259` (`leInt32At(*st, 0) == kCurrentStateVersion`) stays and now
  reads 3. The `kCurrentStateVersion + 1` rejection cases (`:277`, `:398`) stay unchanged and now reject 4.
  The header comment at `:10` is updated to name v3.

Each count becomes a named constant (`108 + kNumEcosystemRuleParams`) rather than a new literal.

`plugins/vorago/CLAUDE.md` updates: the ID table (ecosystem band), "108 registered IDs, 106 persisted",
the route totals, the state table (a v3 block row and the `kStateV3Bytes` total), and the page-6 row.

### 4.3 Audibility probe (Stage A, FR-070) — `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp`

`TEST_CASE("Vorago_EcosystemRuleProbe", "[.probe][vorago]")` is hidden and run once by hand. Its output
is copied into `compliance.md` and into the G1 message.

- **Probe struct**, defined in this TU in `namespace Krate::DSP::detail`:
  ```cpp
  struct VoragoEcosystemRuleProbe {
      template <typename Fn> static void forEachEcosystem(VoragoEngine& e, Fn fn) {
          for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) { fn(e.voices_[v].ecosystem_); }
      }
  };
  ```
  The probe reaches the engine through `const_cast<Krate::DSP::VoragoEngine&>(*proc.engineForTest())`.
  The engine is a non-const heap object (`processor.h:228`), so the cast is well-defined. The probe
  runs single-threaded, between `process()` calls, after `prepare` and before the note-on block.
- **Render:** the §4.6 host at the default surface, with the C-6 stimulus (§5.1) and the default
  timeline (`A` = 155 s), rendered to the end of `M3` (`A + 185` = 340 s). This yields the full C-7.2
  descriptor.
- **Descriptor (ruling R-8):** `#include "preset_test_support.h"` and use `VoragoTest::describe`,
  `descriptorDistance` and `meanOf` (§4.8, part 0, which lands before this TU). The probe carries no
  copy of §5.4/§5.5, so its ranking and the sweep share one implementation.
- **Candidates**: the spec C-2.3 list, grounded in `ecosystem_engine.h:469-707`. Extremes are the
  setter's clamp ends; the default is in parentheses.

  | Knob | Range (default) |
  |---|---|
  | predation | [0, 1] (0.55) |
  | syncRate | [0, 0.5] (0) |
  | exchangeRate | [0, 3] (0.35) |
  | crowding | [0, 0.2] (0.05) |
  | forageRate | [0, 0.05] (0.01) |
  | feedRate | [0, 1] (0) |
  | grazeRate | [0, 3] (0.75) |
  | leakRate | [0, 1] (0.06) |
  | moveRate | [0, 0.5] (0.20) |
  | maxSpeed | [0.001, 0.05] (0.03) |
  | kernelSigma | [0.01, 0.35] (0.03) |
  | freqDrift | [0, 2e-4] (4e-5) |
  | selfAffinity | [−2, 2] (−1) |
  | crossAffinity | [−2, 2] (+0.45) |

  `leakExponent` is excluded: "leakExponent above ~1.3 kills the ecosystem … the upper half [is not]
  musically usable" (`:531-537`).
- **Renders:** 1 default, 1 seed twin of the default (next seed index), and one per non-default
  extreme (27). That is 29 renders of 340 s.
- **Metric per extreme:** `d(default, extreme)` (§5.5), and its ratio to `t0 = d(default, seed twin)`.
  Each knob's audibility is the maximum over its extremes. A knob whose best `d < 2·t0` is flagged as
  inaudible.
- **Output:** a table sorted by audibility with knob, range, default, extreme(s), d, d/t0, and the
  cells it would contribute per P-4. Then the proposed roster: the top 4–6 audible knobs, with the P-3
  arithmetic for N. **Then STOP (G1).**

### 4.4 Category config (FR-001) — `src/preset/vorago_preset_config.h`

Change line `:29` to
`/*.subcategoryNames =*/{"Drones", "Abyss", "Caverns", "Organisms", "Machines", "Textures", "Ghosts"}`.
Nothing else changes: `makeVoragoPresetTabLabels()` (`:36-44`) derives its list from this one. On
disk there are seven directories under `resources/presets/`. `Drones/.gitkeep` is removed once `Drones`
holds presets.

### 4.5 Definitions — `tools/vorago_preset_defs.h` (data only, namespace `Vorago::PresetDefs`)

Rules copied from `seraphis_preset_defs.h:1-40`: every function is `inline`, every table is a
function-local `static const`, and the only project include is `plugin_ids.h` (plus std). No state
layout.

```cpp
struct ParamSetting { Steinberg::Vst::ParamID id; double normalized; };

enum class CapabilityGroup : std::uint8_t { S, M, E, D };

enum class Capability : std::uint8_t {
    S1Noise, S2Resonance, S3Smear, S4Ecology, S5Sub, S6Bloom, S7Ecosystem, S8Cavern, S9Ghost, S10Body,
    M1Darkness, M2Age, M3Density, M4Movement, M5Gravity, M6Entropy,
    M7Pressure, M8Weight, M9Fog, M10Life, M11Depth, M12Mass,
    E1PartialBloom, E2ResonatorPeaks, E3NoiseWake, E4FeedbackLoopWake, E5GhostBursts,
    /* after G1: E6Lo, E6Hi, E7Hi, ... appended here in ratification order (P-4) */
    D1Glass, D1Strings, D1MetalPlate, D1Chamber, D1Ice, D1StoneChamber, D1SteelTank,
    D1WoodenHull, D1CathedralColumn, D1CavernWall, D1GlassSphere,        // BodyMaterial order
    D2BlendBoth,
    D3Direct, D3FilteredWind, D3GranularDust, D3MetallicHiss,             // NoiseOrganismModel order
    D4Type0, D4Type1, D4Type2, D4Type3, D4Type4, D4Type5,
    D4Type6, D4Type7, D4Type8, D4Type9, D4Type10, D4Type11,               // kNoiseTypeByIndex order
    D5Free, D5Keyed, D5Hybrid,
    D6Lowpass, D6Bandpass, D6Highpass,
    D7Div2, D7Div4, D7FifthBelow,
    D8Standard, D8Growth,
    D9FastAttack, D9SlowAttack,
    D10FreezeHolds, D10FreezeOff,
    D11GhostReverse,
    D12TriggersOn, D12TriggersOff,
    D13SlowEvents, D13FastEvents,
    D14Breathing, D14Tidal,
    Count
};

enum class Verification : std::uint8_t {
    Ablation,            // S, M: C-7.4 with `ablation`
    RouteIsolated,       // E1..E5: C-2.1 Group E
    StateWithS,          // D: state predicate AND the named S cell verified
    StateWithReversion,  // E-ext, D13/D14 and every D primary: state AND the reversion ablation passes
    StateAlwaysAudible,  // D8, D9 (and D10.2, D12.2 - default-state)
    FreezeFloor          // D10.1: C-6 arm 4 Freeze-On criteria including the -20 dB floor
};

struct CellSpec {
    Capability cell;
    CapabilityGroup group;
    std::string_view label;               // "S1 noise organism", printed in the matrix
    Verification verification;
    std::array<ParamSetting, 4> ablation; // the override (S/M/E-ext/D13/D14); unused slots {0, -1}
    std::uint8_t ablationCount;
    Capability sConjunct;                 // StateWithS: the S cell required; else Count
    std::int8_t extSide;                  // E-ext: -1 for E{n}.lo, +1 for E{n}.hi (§5.8); else 0
};
[[nodiscard]] inline const std::array<CellSpec, static_cast<std::size_t>(Capability::Count)>& cellSpecs();

struct VoragoPresetDef {
    std::string_view name;
    std::string_view category;
    std::string_view description;         // P-7: no " & < >
    Capability primary;
    std::vector<Capability> secondaries;
    std::vector<ParamSetting> params;     // normalized; untouched IDs keep registered defaults (C-4)
};
[[nodiscard]] inline const std::vector<VoragoPresetDef>& allPresets();
inline constexpr std::array<std::string_view, 7> kCategories{
    "Drones", "Abyss", "Caverns", "Organisms", "Machines", "Textures", "Ghosts"};
[[nodiscard]] inline std::string buildVoragoInfoXml(std::string_view name, std::string_view category,
                                                    std::string_view description);
```
`buildVoragoInfoXml` writes exactly the bytes of `preset_manager.cpp:266-276`, with
`MusicalCategory == MusicalInstrument == category`. The `Comment` line is present only when the
description is non-empty; factory presets always have one (FR-003).

ODR: `VoragoPresetDef`, `CellSpec` and `CapabilityGroup` had 0 hits as `class|struct|enum class` in
`dsp/ plugins/ tools/ tests/` in this session's sweep. The spec swept `Capability`. `Verification` is
re-swept when the header is created. `ParamSetting` shares its name with
`Seraphis::PresetDefs::ParamSetting` (`seraphis_preset_defs.h:59`). They live in different namespaces
and targets, and no TU includes both defs headers.

### 4.6 Catch2-free host — `plugins/vorago/tests/vorago_preset_host.h` (FR-021)

This host is shared by the generator and every preset test TU, and it includes no Catch2 header. The
generator target does not link Catch2, so any Catch2 dependency fails its build. The name is
`VoragoTest::PresetHost` (0 ODR hits this session).

```cpp
namespace VoragoTest {
class PresetHost {
public:
    PresetHost();                                           // make_unique<::Vorago::Processor>() (FR-064 heap rule)
    ~PresetHost();                                          // setActive(false) if active, terminate()
    [[nodiscard]] Steinberg::tresult prepare(double sampleRate, Steinberg::int32 maxBlock);
        // initialize(nullptr) -> setupProcessing({kRealtime, kSample32, maxBlock, sr}) -> setActive(true);
        // sizes outL_/outR_ to maxBlock once (never regrown)
    [[nodiscard]] Steinberg::tresult process(std::size_t n, Steinberg::Vst::IEventList* ev,
                                             Steinberg::Vst::IParameterChanges* pc);   // n <= maxBlock
    [[nodiscard]] Steinberg::tresult loadState(std::span<const std::uint8_t> comp);    // setState(MemoryStream)
    [[nodiscard]] bool saveState(std::vector<std::uint8_t>& out);                      // getState(MemoryStream)
    [[nodiscard]] const float* outL() const noexcept;
    [[nodiscard]] const float* outR() const noexcept;
    [[nodiscard]] ::Vorago::Processor& processor() noexcept;
private: /* processor, buffers, a reusable Krate::Test::EventList and ParameterChanges */
};

/// C-4, THE drive: prepare(48000, 512) -> ONE process(512) carrying every def.params point at
/// offset 0 -> getState. Used by the generator AND Vorago_FactoryPresets_TreeMatchesGenerator.
[[nodiscard]] bool buildComponentState(const Vorago::PresetDefs::VoragoPresetDef& def,
                                       std::vector<std::uint8_t>& comp, std::string& why);
}
```
`buildComponentState` rejects, with `why` set:
- any definition value outside [0, 1] or non-finite (the spec's edge case: "a definition outside
  [0, 1] is a generator error, not a clamp");
- any point for `kSustainPedalId` or `kChannelPressureId` (C-4).

Every scripted render in §4.8 drives this same class.

### 4.7 Generator + CMake (FR-018…FR-020, FR-023, FR-025)

`tools/vorago_preset_generator.cpp` follows `seraphis_preset_generator.cpp:86-349` without the partials
block:
- defines `void* moduleHandle = nullptr;`;
- takes the class id from `Vorago::kProcessorUID.toString(buf)` and checks it is 32 chars;
- takes the output base from `argv[1]`, defaulting to `plugins/vorago/resources/presets`;
- creates the seven `kCategories` directories;
- iterates `allPresets()` in definition order and calls `buildComponentState` for each;
- writes each file with `writeVstPreset`: the 48-byte header + `Comp` + `Info` + `List` layout of
  `:224-265`, duplicated into this TU's anonymous namespace so the Seraphis tool is not edited;
- exits 1 on any failure;
- uses no timestamp, no directory iteration and no RNG (FR-023).

In the root `CMakeLists.txt`, after `generate_seraphis_presets` (`:664-669`), add a block copied from
`:604-662` with these changes:
- sources: `tools/vorago_preset_generator.cpp`, `plugins/vorago/src/processor/processor.cpp`, the same
  five SDK sources, and `plugins/vorago/tests/vstgui_test_stubs.cpp` (it defines only
  `GetPluginFactory`, read this session);
- `target_link_libraries(... PRIVATE KrateDSP KratePluginsShared sdk)`;
- include dirs `plugins/vorago/src`, `plugins/vorago/tests`, `tests/test_helpers` (header-only use),
  `tools` and `${vst3sdk_SOURCE_DIR}`;
- `cxx_std_20` and `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"`;
- the same warning flags (`/W4 /permissive- /Zc:__cplusplus /wd4100 /wd4458`), with `/wd4459` added
  only if the first MSVC build emits C4459, as the vorago_tests comment rules.

```cmake
add_custom_target(generate_vorago_presets
    COMMAND vorago_preset_generator "${CMAKE_SOURCE_DIR}/plugins/vorago/resources/presets"
    DEPENDS vorago_preset_generator
    COMMENT "Generating Vorago factory presets" VERBATIM)
```
`processor.cpp` also constructs a `DataExchangeHandler` in `connect()`. Its symbols should come from
`sdk`, as they do for `vorago_tests`, whose source list has no `dataexchange.cpp`. If the Linux link
fails, add the SDK source explicitly. The WSL build verifies this before C merges.

### 4.8 Harness — `plugins/vorago/tests/preset_test_support.h` (Catch2-free)

Namespace `VoragoTest`. `factoryPresetRoot()` returns `VORAGO_RESOURCES_DIR "/presets"`;
`VORAGO_RESOURCES_DIR` is already defined for `vorago_tests` (`tests/CMakeLists.txt` compile
definitions). `allPresetFiles()` returns a sorted list, for the reason given in Seraphis
(`plugins/seraphis/tests/preset_test_support.h:141-160`).

| Piece | Signature sketch | Notes |
|---|---|---|
| Container | `struct PresetFile { std::string classId; std::vector<std::uint8_t> comp; std::string info; bool ok; std::string why; };` `PresetFile parseVstPreset(const std::filesystem::path&)` | magic, version, 32-char id, list offset in bounds, `Comp` and `Info` present, offsets and sizes in bounds (FR-028) |
| Info | `std::map<std::string, std::string> parseInfoAttributes(std::string_view)` | six attributes + `Comment` |
| Typed decode | `struct DecodedPresetState { ::Vorago::GlobalParams global; ::Vorago::MacroParams macros; ... ::Vorago::LifeParams life; std::size_t bytesConsumed; std::int32_t version; };` `bool decodePresetState(std::span<const std::uint8_t>, DecodedPresetState&)` | calls the shipped `load*Params` in `getState()` order (`processor.cpp:643-659`), then `loadEcosystemParamsV3Ext`; asserts `bytesConsumed == kStateV3Bytes` (FR-031). The packs hold atomics, so the struct is non-copyable and is filled through an out-param |
| Timeline | `struct SweepTimeline { double A, rel, rt60, M1b, M1e, M2b, M2e, M3b, M3e, H, tailB, tailE, total; bool freezeGesture; };` `SweepTimeline makeTimeline(const DecodedPresetState&, bool freezeGesture)` | §5.1; rt60 comes from `effectiveCavernDecaySeconds` (§4.9) |
| Render | `struct RenderSpec { std::span<const std::uint8_t> comp; std::vector<Vorago::PresetDefs::ParamSetting> overrides; double sr = 48000; std::vector<std::uint8_t> notes{36}; double noteOffAt = -1; double freezeAt = -1; double end; std::vector<std::pair<double, double>> capture; };` `struct SweepCapture { bool finite; float peak; std::vector<double> rms10sDb; std::vector<std::vector<float>> capL, capR; };` `SweepCapture renderPreset(const RenderSpec&)` | streaming: each block updates bit-pattern finiteness (`Krate::DSP::detail::isFinite`), the stereo peak and the 10-s power sums; samples are copied only inside the `capture` windows, so memory stays bounded (≤ 3 × 60 s × 2 ch × 4 B ≈ 69 MB) |
| Descriptor | `struct PresetDescriptor { std::array<double, 9> band; double motion, flux, corr, energySpread, crest; };` `PresetDescriptor describe(std::span<const float> L, std::span<const float> R, double sr)`; `double descriptorDistance(const PresetDescriptor&, const PresetDescriptor&)`; `PresetDescriptor meanOf(std::span<const PresetDescriptor>)` | §5.4–5.5; ODR 0 hits |
| Verification | `struct CellResult { bool stateOk; bool rendered; double d; double bar; bool verified; std::string note; };` `struct VerificationVector { std::array<CellResult, Capability::Count> cells; }` | §5.6–5.9; ODR 0 hits |
| Record | `struct SweepRecord { std::string name; SweepTimeline tl; arms…; std::array<PresetDescriptor, 3> minutes; PresetDescriptor mean; double selfDistance; double levelTwinD; VerificationVector vec; E-arm numbers; rate arms; repro; }` `void writeRecord(const SweepRecord&, const std::filesystem::path&)`; `bool readRecord(...)` | text `key value…` lines, doubles as `%.17g`. A record is a transient CI artifact: never committed and never compared across toolchains, so it is not a golden. ODR 0 hits |
| Sharding | `struct Shard { std::size_t index, count; }; Shard shardFromEnv();` `bool inShard(std::size_t defIndex, Shard)` | `VORAGO_SWEEP_SHARD=i/n`, default `0/1`; index `N` is the default-surface pseudo-preset; a malformed value fails the calling TEST_CASE |
| Env | `std::optional<std::string> readEnv(const char*)` | `_dupenv_s` under `_MSC_VER` (avoids C4996), `std::getenv` elsewhere (precedent: `ruinae_byte_identical_post_lane10_test.cpp:209`) |
| Pool | `void runJobs(std::vector<std::function<void()>>& jobs, unsigned threads)` | `std::thread`, not `std::jthread` (AppleClang libc++ availability); threads = `VORAGO_SWEEP_THREADS`, or `min(hardware_concurrency, 4)`; jobs never call Catch2 macros; results are asserted on the test thread |
| Memo | `const SweepRecord& sweepRecordFor(std::size_t defIndex)` | in-process cache. If `VORAGO_SWEEP_IN` is set, it loads the record file instead of rendering (aggregate job). If `VORAGO_SWEEP_OUT` is set, it writes each computed record |

`MultiParamChanges` and `MultiPointParamValueQueue` stay in `vorago_test_fixture.h` for the Catch2 TUs.
The host only ever needs one point per ID at offset 0, which `Krate::Test::ParameterChanges` provides.

### 4.9 `getTailSamples()` (FR-060) — `processor.h/.cpp` + `src/processor/tail_estimate.h`

```cpp
// src/processor/tail_estimate.h (processor-side, header-only; ODR: 0 hits for both names)
namespace Vorago {
/// C-6 / FR-060: the effective cavern decay = the shipped matrix's CavernDecaySeconds with the stored
/// decay installed as the target base, clamped to the cavern range (space_params.h:58-59).
[[nodiscard]] inline float effectiveCavernDecaySeconds(const Krate::DSP::VoragoMacroValues& macros,
                                                       float storedDecaySeconds) noexcept {
    Krate::DSP::VoragoMacroMatrix m{};
    m.setTargetBase(Krate::DSP::VoragoMacroTarget::CavernDecaySeconds, storedDecaySeconds);
    m.setMacros(macros);
    return std::clamp(m.computeCavernTargets().decaySeconds,
                      static_cast<float>(kSpaceDecayMinSeconds), static_cast<float>(kSpaceDecayMaxSeconds));
}
/// vorago_engine.h:370 sets the ghost grain to 12 s at prepare and never rewrites it.
inline constexpr double kGhostGrainTailSeconds = 12.0;
}
// Processor
Steinberg::uint32 PLUGIN_API getTailSamples() override;
```
Body:
- If `freeze == 1`, return `kInfiniteTail`.
- Otherwise, `tail = releaseMs/1000 + effectiveCavernDecaySeconds(knob macros, decaySeconds) +
  kGhostGrainTailSeconds`, and samples = `ceil(tail · processSetup.sampleRate)`. The largest result,
  ≤ 132 s × 192 kHz ≈ 2.5e7, is far below `kInfiniteTail`.

It reads only relaxed pack atomics, so it is safe on the host thread and allocation-free; the matrix
is a stack value. The macro input is the knobs, not channel pressure, because no Pressure row targets
the decay (`vorago_macro_matrix.h:492-548`). The C-6 timeline uses the same helper, so "effective
RT60" has a single definition. The comment at `processor.h:89` is replaced.

### 4.10 `plugins/vorago/docs/index.html` (FR-062)

Copy the structure of `plugins/seraphis/docs/index.html` and `assets/style.css` (directory listing
read). Content:
- what Vorago is;
- the twelve concept macros;
- the ecosystem view;
- the seven categories, one line each;
- the freeze-gesture instruction;
- system requirements;
- links.

`docs/.gitkeep` stays until the page lands, then it is removed. `docs.yml` needs no edit (spec C-11).

### 4.11 Tooling and CI (FR-024, FR-063, FR-066)

- **`check-preset-generator-determinism.js`:** add `--plugin <name>` (default `seraphis`). A table
  `{seraphis: 'seraphis_preset_generator', vorago: 'vorago_preset_generator'}` builds
  `DEFAULT_BINARIES` (`:52-56`), the temp prefix (`:209`, `${plugin}-presets-`) and the not-found
  message (`:111-113`). The default path keeps today's behaviour byte for byte (FR-024). USAGE is
  updated.
- **`run-cpu-tests.js`:** `FILTER` becomes
  `'[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]'` (ruling R-6); nothing
  else in the runner changes.
- **Rosters:** `PLUGIN_MAP` gains `vorago: { testTarget: 'vorago_tests', bundle: 'Vorago.vst3' }`
  (`release-readiness.js:14-22`). `SKILL.md`'s plugin list (`:15-16`) and table (`:21-31`) gain Vorago.
- **`ci.yml`:** the three nightly filters `FILTER='[long]'` (`:369`, `:655`, `:1116`) become
  `FILTER='[long]~[vorago-sweep]'`. Nothing else in `ci.yml` changes.
- **`long-tests-nightly.yml`:** two new jobs, gated on `check-activity` like `long-tests`:
  ```yaml
  vorago-sweep:
    needs: check-activity
    if: needs.check-activity.outputs.should_run == 'true'
    strategy: { fail-fast: false, matrix: { os: [windows-2022, macos-latest, ubuntu-latest], shard: [0 .. n-1] } }
    runs-on: ${{ matrix.os }}
    timeout-minutes: <=180
    steps: checkout; configure (the leg's ci.yml flags minus AU/ccache options); build --target vorago_tests;
           run: VORAGO_SWEEP_SHARD=${{ matrix.shard }}/n VORAGO_SWEEP_OUT=sweep-out
                <bin>/vorago_tests "[vorago-sweep]~[vorago-aggregate]" -d yes
           upload-artifact: vorago-sweep-${{ matrix.os }}-${{ matrix.shard }} (sweep-out/)
  vorago-sweep-aggregate:
    needs: [check-activity, vorago-sweep]
    if: ${{ !cancelled() && needs.check-activity.outputs.should_run == 'true' }}
    strategy: { fail-fast: false, matrix: { os: [...] } }
    timeout-minutes: <=180
    steps: build vorago_tests; download-artifact pattern vorago-sweep-${{ matrix.os }}-* merge-multiple;
           run: VORAGO_SWEEP_IN=sweep-in <bin>/vorago_tests "[vorago-aggregate]" -d yes
  ```
  The shard count `n` is ruled from the measured cost (§9) and recorded. `-d yes` puts every case's
  duration in the log for SC-013. The aggregate case REQUIREs a record for every definition index,
  so a missing shard fails loudly.

---

## 5. Algorithms (exact)

### 5.1 Timeline (C-6)

From the typed decode:
- `A` = `mode == Growth ? growthDurationSeconds : (stage0 + stage1 + stage2 + stage3) / 1000`.
- `Rel = releaseMs / 1000`.
- `RT60 = effectiveCavernDecaySeconds(decoded knob macros, decaySeconds)`.
- `M1 = [A+5, A+65]`, `M2 = [A+65, A+125]`, `M3 = [A+125, A+185]`, `Sus = M1`, `H = A + 185`.
- `Tail` = `[H+Rel+10, H+Rel+70]` for Freeze-On (stored On, or the gesture preset), otherwise
  `[H+Rel+RT60+5, H+Rel+RT60+15]`.
- `Total = Tail.end`.
- Seconds convert to samples as `llround(t · sr)`.

Stimulus:
- NoteOn 36 with `kVelocity100` at sample 0 (block 0, offset 0).
- NoteOff at `round(H·sr)`, with its exact in-block offset.
- Block size 512, sample rate 48 000 Hz.
- Freeze gesture: `kSpaceFreezeId` → 1.0 as an offset-0 point in the block that starts at
  `floor(round((A+65)·sr)/512)·512`. That is at most 10.7 ms early (documented).

### 5.2 Windows and RMS

- Stereo power of a span = `(ΣL² + ΣR²) / (2n)`, in dB as `10·log10(max(p, 1e-24))`. This matches the
  −240 dBFS floor of `blockRmsDb` (`vorago_fixtures.h:343-376`).
- "Every 10 s window over [a, b]" means `k = floor((b−a)/10)` full windows from `a`. If a remainder
  exists, one more window is right-aligned at `b`. Every sample is covered, and no window is shorter
  than 10 s.
- The streaming recorder keeps a power sum per 512-sample block, so any window is a sum of blocks.
  Windows snap inward to block edges, with at most 512 samples of slack.

### 5.3 Arms (C-6)

1. Bounded over `[0, Total]`: every sample finite by bit pattern; `peak ≤ 0.9661f`; every 10-s
   window ≤ −6 dBFS.
2. Non-silence over `[A, H]`: every 10-s window ≥ −60 dBFS.
3. `ΔdB = RMS([H−60, H]) − RMS(Sus)` ∈ `[−18, +12]`.
4. Tail:
   - Freeze Off: `RMS(Tail) ≤ RMS(Sus) − 40`.
   - Freeze On (gesture preset), over the six 10-s windows of `Tail`: `last ≤ loudest + 1.0`,
     `last ≥ loudest − 6.0`, and `loudest ≥ RMS(Sus) − 20`.

Authoring rule: no factory preset stores Freeze On. The spec's edge case says a stored-On load
freezes an empty tank (`aether_reverb.h:409-410`, per spec). If a preset ever stores On, it runs the
Freeze-On arm 4 unmodified.

### 5.4 Descriptor (C-7.2)

Per 60-s minute, each channel (L and R) is measured separately and the results are combined:
- Band power per channel is `E_c(lo, hi) = bandEnergyDb(x_c, 48000, lo, hi)`. The stereo band power is
  `E(lo, hi) = 10·log10((10^(E_L/10) + 10^(E_R/10)) / 2)`.
- `E_hi = E(80, 20000)`.
- Band edges: `k = 0` is `[20, 80]`; `k = 1..7` is `[80·2^(k−1), 80·2^k]`; `k = 8` is
  `[10240, 20000]`.
- `band[k] = max(E(band k) − E_hi, −60) / 3`.
- `motion = log2(max(mean(perBandTotalVariation(L), (R)), 1e-6)) / 1`.
- `flux = log2(max(mean(perBinMagnitudeFlux(L), (R)), 1e-6)) / 1`.
- `corr = calculateCorrelation(L, R, n) / 0.25`.
- `energySpread` = population stddev over the 60 one-second stereo dB values, divided by 2. Each
  one-second value is `10·log10` of the power mean of `blockRmsDb(L, 48000)` and
  `blockRmsDb(R, 48000)` for that block.
- `crest = mean(crestFactorDb(L), crestFactorDb(R)) / 3`.

`D(P)` is the component-wise mean of the three minutes. The 1e-6 floor keeps `log2` finite for a
static render, because both helpers return 0.0 when there are fewer than 2 frames. It sits orders of
magnitude below any drone's motion.

### 5.5 Distance and floors (C-7.3)

- `d(P, Q) = sqrt(Σ_i (D_i(P) − D_i(Q))²)` over the 14 scaled components (9 bands + 5 others).
- `s(P) = max(d(m1,m2), d(m1,m3), d(m2,m3))`. For a twin rendered only through `Sus`, `s` is
  `d(first 30 s, last 30 s)`.
- Pair floor: `d(P, Q) ≥ max(F, 2·max(s(P), s(Q)), 2·t_max)`, with F frozen at G2 (≥ 4.0).

### 5.6 Ablation twins (C-7.4) — overrides in normalized units

Overrides are sent as offset-0 points in block 0 on top of `setState(P)`. This is the same path a host
uses: the processor latches parameter changes before any slice (`processor.cpp:295-297`). Each twin
renders on its own timeline to `Sus.end`. `P_Sus` is P's `M1` descriptor.

| Cell | Override |
|---|---|
| S1 | 300 → 0.0 |
| S2 | 401 → 0.0 |
| S3 | 700 → 0.0, 701 → 0.0 |
| S4 | 500 → 0.0 |
| S5 | 610, 611, 612 → 0.0; 600 → 0.0 |
| S6 | 1300 → 0.0 |
| S7 | 900 → 0.0 |
| S8 | 1105 → 0.0 |
| S9 | 1400 → 0.0 |
| S10 | 1003 → 0.0 |
| M m | macro `100+m−1` → its registered default (0.0; Gravity 0.5) |
| E{n}.lo / E{n}.hi | knob ID → its registered default normalized value; scored only when the cell's §5.8 side predicate holds (`StateWithReversion`), so one stored value can verify at most one of the pair |
| D13.x | 800 → 0.5 |
| D14.1 / D14.2 | 1500 → 0.0 / 1502 → 0.0 |
| D primary (reversion) | the cell's IDs → registered defaults: D1 → 1004/1005 defaults (indices 5/6); D8.2 → 1200 → 0; D9.1 → 1201–1204 → defaults. A reversion that changes the envelope renders on its own timeline |

A cell is verified iff `d(P_Sus, twin_Sus) ≥ max(bar, 2·s(P))`, with bar = F for a primary and 1.5 for
a secondary. Unclaimed cells are recorded against the secondary bar; that is their value in the full
vector (FR-012).

### 5.7 Route-isolated E arms (C-2.1 Group E)

Destinations: E1↔S6, E2↔S2, E3↔S1, E4↔S4, E5↔S9.
- For each k: `R_k` = P + the S-overrides of the other four destinations; `R_k⁰` = `R_k` + `900 → 0.0`.
- `R_∅` = P + all five destination overrides; `R_∅⁰` = `R_∅` + `900 → 0.0`.
- All renders use P's timeline and run to `Sus.end`.

E_k is verified iff `d(R_k, R_k⁰) ≥ max(bar, 2·s(R_k))` AND `d(R_k, R_k⁰) ≥ d(R_∅, R_∅⁰) + 1.5`, where
`s(R_k)` is the half-window self-distance. That is 12 renders per preset (5 pairs + the `R_∅` pair).

### 5.8 D predicates (decoded state, FR-031)

| Cell | State predicate | Conjunct |
|---|---|---|
| D1.x | `materialA == x` or `materialB == x` | S10 |
| D2 | `0.35 ≤ blend ≤ 0.65` | S10 |
| D3.m | some slot has `model[s] == m` | S1 |
| D4.t | some slot has `model[s] == Direct` and `type[s] == t` | S1 |
| D5.a | `anchorMode == a` | S2 |
| D6.f | some loop has `loopFilterMode[l] == f` | S4 |
| D7.t | `argmax(div2LevelDb, div4LevelDb, fifthBelowLevelDb) == t` (strict; ties fail) | S5 |
| D8.s | `mode == s` | always audible |
| D9.1 / D9.2 | `A ≤ 10` / `A ≥ 90` | always audible |
| D10.1 | the preset's render is the gesture render | FreezeFloor (arm 4 Freeze-On) |
| D10.2 | `freeze == 0` | always audible (default-state) |
| D11 | `ghostReverseProbability ≥ 0.5` | S9 |
| D12.1 / D12.2 | `eventTriggers == 1 / 0` | S9 / default-state |
| D13.1 / D13.2 | `eventRateScale ≤ 0.3` / `≥ 3.0` | S7 AND reversion (800 → 0.5) |
| D14.1 / D14.2 | `breathingDepth ≥ 0.7` / `tidalDepth ≥ 0.7` | depth → 0 ablation |
| E{n}.lo / E{n}.hi | on the decoded normalized knob `n` with registered default `n₀`: `n ≤ n₀ − δ_lo` / `n ≥ n₀ + δ_hi`, where `δ_lo = 0.5·n₀` and `δ_hi = 0.5·(1 − n₀)` (the stored value sits at least halfway from the default toward that extreme) | reversion (knob → `n₀`, §5.6) at the cell's role bar |

The E-ext margin is a ruling recorded with R at G1 (FR-071). It is scale-free, so it is feasible on
every side that exists: a side exists only when `n₀` is not at that range end (P-4). Because
`δ_lo, δ_hi > 0` for any existing side, the two predicates are mutually exclusive: a preset displaced
above the default can verify `E{n}.hi` but never `E{n}.lo`, and vice versa. The two E-ext primaries
therefore cannot verify each other in the FR-011a vectors, and a missing low-extreme showcase leaves
`E{n}.lo` uncovered, which fails FR-013.

A D cell that is some preset's **primary** also needs its reversion to pass at the primary bar (C-2.2).

### 5.9 Full verification vector — exact skip rules (no approximation)

The vector is computed once per preset (FR-012). A render is skipped only when its outcome is known
exactly:
- **Override equals the stored value** for every overridden ID: the twin *is* P, so `d = 0` and the
  cell is not verified.
- **M cell with small displacement:** the stored macro displacement is below 0.5 (Gravity: 0.35), so
  its state conjunct fails.
- **E cell with `900` stored at 0.0:** `R_k ≡ R_k⁰`.
- **D cell with a false state predicate:** the conjunct is never evaluated.
- **E-ext cell with a false side predicate (§5.8):** the reversion twin is not rendered, and the cell
  is recorded as not verified.

Every skip is recorded with its reason in the printed matrix.

### 5.10 Non-subset (FR-011a) and coverage (FR-013)

Each record stores, per cell, the raw terms rather than a single flag: the measured `d`, `2·s`, the
state/side predicate result and the S-conjunct or E-route result. "Verified at bar b" is computed from
them (`d ≥ max(b, 2·s)` and every conjunct true).

**The one rule.** Q verifies cell `c` iff Q's entry for `c` passes at the **secondary bar** (1.5). For
`c == Q.primary` the entry must also pass at the primary bar F, which the shard case already requires
of Q's own primary. No other cell is judged at F in this check: whether Q verifies `c` never depends on
the bar P's claim carries.

For each ordered pair (P, Q), P ≠ Q, scan P's claims: primary first, then secondaries in definition
order. The witness is the first claim `c` that Q does not verify under the rule above. No witness is a
failure ("P's whole showcase sits inside Q's"). The result prints per P as `P vs Q: witness <cell>`,
or `P vs Q: SUBSET`.

The rule is a pure function, `findWitness(const VerificationVector& p, const VerificationVector& q)`, in
`preset_test_support.h`. It is unit-tested per push on hand-built vectors
(`Vorago_PresetMatrix_NonSubsetRule`, §7):
- P's only claim `X` (its primary, d = F + 1) is present in Q with `d` strictly between 1.5 and F and
  every conjunct true → Q verifies `X` → no witness → the check **fails** (reports SUBSET);
- the same with Q's `d` = 1.0 → witness `X`;
- the same with Q's `d` between 1.5 and F but `2·s(Q)` above `d` → witness `X`.

**Coverage** is counted over factory presets only (indices `0..N−1`). The default-surface
pseudo-preset (index N, §5.9) is used for one thing: it marks default-state cells as ineligible for
primary (FR-011, FR-012). `Vorago_PresetMatrix_CoverageComplete` asserts:
- every cell has at least one **factory-preset** verifier (index < N). The default surface is excluded
  from this count, so a default-state cell with no factory showcase fails;
- each of E1..E5 is the verified primary of a distinct preset;
- no factory preset's primary is in the default-state set **measured in this run** (the cells index N
  verifies);
- the measured default-state set equals `kRecordedDefaultStateCells`, the constant that the per-push
  `Vorago_PresetDefs_ClaimsWellFormed` checks primaries against. Any drift between the prediction and
  the measurement fails nightly, and the constant is corrected from the printed set.

### 5.11 Controls (C-7.3)

The control set `C` is printed, deduplicated, and holds at least 3 presets: the argmax and argmin of
`s(P)`, the D10.1 preset, the preset with the highest stored Pressure, and the one with the highest
stored Weight.
- (a) For every preset: the descriptor of its `M1..M3` buffers scaled by `10^(−6/20)`, compared with
  the unscaled descriptor, gives `d < 0.05`. This is computed in the shard.
- (a′) For the highest-Pressure preset: a full render with ID 0 → 0.5·stored gives `d ≤ F/2`.
- (b) For each C preset: a full render at seed index `(i+1) mod 16` (normalized `/15`) gives
  `d ≤ F/2`. `t_max` is the maximum of these.
- (c) For each C preset: renders with `600` at stored ± 0.125 give `d < F`. A side that would leave
  [0, 1] is reported as infeasible, and the other side is then required.

### 5.12 Parameter space (C-7.1)

The normalized vectors come from the shipped inverse maps:
- Initialize one `Vorago::Controller` instance.
- For each preset, call `setComponentState(comp)`, then `getParamNormalized(id)` for every persisted
  ID (all registered IDs except 4 and 5).
- The default surface is the freshly initialized controller.

List IDs are those with `ParameterInfo::stepCount > 0`; two values differ iff `round(n·stepCount)`
differs. Continuous IDs differ iff `|Δn| ≥ 0.10`. Every pair of presets, and every preset against the
default, needs at least 8 differing IDs.

### 5.13 Committed-tree tolerance (C-9)

`Vorago_FactoryPresets_TreeToleranceProbe` (`[.measure]`) regenerates each preset via
`buildComponentState` and prints, per float field, the worst `|c − r| / max(|c|, 1e-30)`.
- It runs on MSVC and on WSL/GCC, using the WSL recipe in memory.
- It runs on the macOS CI leg (AppleClang, `-ffast-math`) **before the tolerance is pinned**: one
  `workflow_dispatch` run with a temporary step that invokes
  `vorago_tests "Vorago_FactoryPresets_TreeToleranceProbe"` and prints its table. The step is removed
  once the figure is recorded, and the run URL goes in `compliance.md`.
- Pinned tolerance = `max(10 × worst across all three toolchains, 1 ULP relative (1.19e-7))`. All three
  worst figures (MSVC, WSL/GCC, macOS) are recorded in `compliance.md` next to the pinned value.
- The enforcing test still prints its per-field worst on every leg. A later macOS run over the pin
  triggers a re-measure on all three, recorded as a ruling (C-9), never a silent widening.
- Int fields and `Info` bytes must match exactly, and so must version and length.

### 5.14 Pilot (FR-017a)

Pilot set (7):

| Cell | Preset |
|---|---|
| S5 | Tectonic Floor |
| S8 | Cathedral Void (gesture, D10.1) |
| M7 | Pressure Front |
| M10 | Teeming |
| E2 | Singing Colony |
| D1 | Glass Well |
| D8.2 | Growth Ring |

Together these cover the subs, the space, drive, the colony, a material swap and the Growth envelope.
Add one deliberately authored near-variant of Tectonic Floor (sub offset +6 dB plus one section
tweak).

Measure `t_max` over the pilot's seed twins, `d` for the near-variant pair, and every pilot `s(P)`.

Rule `F = max(4.0, 2·t_max)` and record it in `compliance.md` with the numbers. If the near-variant
pair reads ≥ F, raise F to just above it (a near-variant must fail) and record that ruling. STOP if a
seed twin exceeds F/2 (C-7.3(b)). F is then frozen as `kDistinctFloor` in `preset_test_support.h`.

### 5.15 Preset CPU (FR-041)

`[.perf]`, run only via `node tools/run-cpu-tests.js vorago_tests`. The runner's `FILTER`
(`tools/run-cpu-tests.js:54`) gains `~[vorago-sweep]` (ruling R-6) so this command stays true for every
suite and the multi-hour sweep never shares a CPU run. For each preset:
1. Set up the host at 48k/512 and call `setState`.
2. Override `kPolyphonyId → 3/5` (4 voices).
3. Send NoteOn `kCpuNotes` at t = 0.
4. Pre-roll, untimed, to `A + 5 s`.
5. Time 16 trials × 100 blocks with `std::chrono::steady_clock`, **interleaving** a preset block with
   a default block (the Phase 9 `measureTrio` lesson).

One default-surface host is pre-rolled to 160 s. It is pre-rolled again whenever its next block would
leave its `Sus` window, which is about every 3 presets.

Gate: `min-trial(preset) / min-trial(default) ≤ 1.15` for the worst preset. The absolute figure vs
3 200 000 ns and the stored-polyphony figure are printed, not gated.

### 5.16 Probe audibility

See §4.3.

---

## 6. Matrix-derived library (FR-016)

Derivation:
1. Enumerate the cells (C-2.1 plus the ratified E-ext cells).
2. Run the default surface through the harness to get the default-state set.
3. Make every S, M, E and E-ext cell the primary of its own preset. Add the non-default-state D1
   materials, D8.2 and D9.1 as primaries (P-3/P-5).
4. Attach each remaining cell (D2, D4.x, the non-default D5–D7, D10.1, D11, D12.1, D13, D14) as a
   **secondary** to the primary whose section it needs (its S conjunct), so its conjunct is already
   verified. Attach **every default-state cell** the same way (the set measured in step 2; predicted:
   D1.StoneChamber, D1.SteelTank, D3.1–D3.4, D8.1, D9.2, D10.2, D12.2). Each one needs at least one
   factory preset that verifies it, because the default surface never counts toward coverage (§5.10).
5. Only then file each preset by how it sounds.
6. Check FR-004: at least 3 presets per category.

The list below is provisional: it comes before G1 and before any render, and names and categories
move if the harness or listening says so.

| Primary | Preset | Secondaries (plan) | Category |
|---|---|---|---|
| S1 | Wind Through Basalt | D4 ×4 (Direct slots), D3.1 Direct, D3.2 FilteredWind | Textures |
| S2 | Resonant Shaft | D5.2 Keyed | Caverns |
| S3 | Smeared Horizon | D8.1 Standard, D10.2 freeze off | Textures |
| S4 | Feedback Mire | D6.2 Bandpass, D6.3 Highpass | Machines |
| S5 | Tectonic Floor | D7.3 fifth-below | Abyss |
| S6 | Slow Bloom | D14.1 breathing, D9.2 slow attack | Drones |
| S7 | Colony Pulse | D13.2 fast events | Organisms |
| S8 | Cathedral Void | D10.1 freeze gesture | Caverns |
| S9 | Choir of Absence | D11 reverse, D12.1 triggers (additive, OQ-4) | Ghosts |
| S10 | Hull Resonance | D2 blend, D1 StoneChamber, D1 SteelTank (default materials kept, blended) | Machines |
| M1 | Lightless | D12.2 triggers off | Abyss |
| M2 | Erosion | D4 ×4, D3.3 GranularDust | Textures |
| M3 | Crowded Dark | — | Drones |
| M4 | Drifting Strata | D14.2 tidal | Drones |
| M5 | Stone Gravity | D5 Hybrid (Gravity is inert outside Hybrid) | Abyss |
| M6 | Entropic Hum | — | Machines |
| M7 | Pressure Front | — | Machines |
| M8 | Weighted Deep | D7.2 f/4 | Abyss |
| M9 | Fogbound | — | Ghosts |
| M10 | Teeming | D13.1 slow events | Organisms |
| M11 | Endless Descent | — | Caverns |
| M12 | Monolith | — | Drones |
| E1 | Bloom Colony | — | Organisms |
| E2 | Singing Colony | D5.1 Free | Organisms |
| E3 | Swarm Breath | D4 ×4, D3.4 MetallicHiss | Textures |
| E4 | Feeding Loops | — | Machines |
| E5 | Haunted Colony | — | Ghosts |
| D1 Glass | Glass Well | — | Caverns |
| D1 Strings | Strung Abyss | — | Drones |
| D1 MetalPlate | Iron Plate | — | Machines |
| D1 Chamber | Chamber Drone | — | Drones |
| D1 Ice | Ice Shelf | — | Textures |
| D1 WoodenHull | Hull Ark | — | Drones |
| D1 CathedralColumn | Column Hymn | — | Caverns |
| D1 CavernWall | Cavern Wall | — | Caverns |
| D1 GlassSphere | Glass Sphere | — | Ghosts |
| D8.2 | Growth Ring | — | Organisms |
| D9.1 | Sudden Chasm | — | Abyss |
| E-ext (each) | colony temperaments, named after G1 | — | Organisms (most) |

That is 38 + |E-ext| presets, with every category at 4 or more before the E-ext presets. Authoring
constraints on every row:
- polyphony index ≤ 3 (FR-007);
- `A ≤ 180` and `Rel ≤ 60` (FR-008);
- Freeze stored Off;
- no point for ID 4 or 5;
- output saturation only via its MB base (C-5);
- no `" & < >` in the description (P-7).

---

## 7. Test plan

All new TUs are listed in `plugins/vorago/tests/CMakeLists.txt`. Tags:
- per-push: `[vorago][preset]`;
- sharded nightly: `[vorago][preset][long][vorago-sweep]`;
- aggregate: the sharded tags plus `[vorago-aggregate]`.

| FR / SC | File | TEST_CASE | Assertion strategy |
|---|---|---|---|
| FR-070 / SC-025 | `integration/ecosystem_rule_probe_test.cpp` | `Vorago_EcosystemRuleProbe` `[.probe]` | prints the table; asserts only that every render is finite and the default render is non-silent (−60 dBFS over M1), so the table is meaningful |
| FR-072 / SC-027 | `unit/state_v3_test.cpp` (new) | `Vorago_StateRoundTripV3`, `Vorago_State_V2LoadsWithRosterDefaults`, `Vorago_State_V3TruncatedKeepsPrefix`, `Vorago_State_V3NonFiniteKnobRejected` | exact byte round-trip at length `kStateV3Bytes`; a v2 stream (version 2 + the v2 chain), loaded after the knobs were moved off default, leaves the knobs at defaults; a stream truncated after the life pack leaves the knobs unchanged; a NaN by bit pattern leaves its field unchanged |
| FR-072 | `unit/state_v2_test.cpp` (kept) | `Vorago_StateRoundTripV2` → legacy load only | v2 streams still load |
| FR-072 / FR-074 | `unit/state_roundtrip_test.cpp` (updated) | existing cases | current-stream size and `memcmp` at `kStateV3Bytes`; version field == `kCurrentStateVersion` (3); `kCurrentStateVersion + 1` (4) still rejected |
| FR-072 / FR-074 | `unit/params/ecosystem_params_test.cpp` | `Vorago_EcosystemParamsContract` extended | per knob: registered default == engine default (heap `EcosystemEngine{}` getter); normalized ↔ plain round-trip within 1e-6; clamp; format string |
| FR-074 | `unit/param_table_test.cpp` + `param_table_expected.h` | `Vorago_ParamIdMap`, `Vorago_RouteTable`, `Vorago_ParameterInfoTable`, `Vorago_ParamInputHygiene` | rows for the new IDs; VP count |
| FR-072 (dsp) | `dsp/tests/unit/systems/vorago_param_surface_test.cpp` | new `VoragoVoice_EcosystemRuleForwarders`; `VoragoEngine_ApplyVoiceParamsDefaultIsNoOp{_Short}` and `..._ReachesAllSlots` extended | each forwarder reaches `ecosystem().get*()`; non-finite values are rejected; an unchanged value early-outs; a default broadcast leaves the render within `compareFingerprints` of no broadcast |
| FR-073 / SC-028 | `unit/controller/editor_layout_test.cpp` | `Vorago_UidescBindsEverySurfaceId`, `Vorago_UidescTagTable`, `Vorago_UidescViewClassRule`, `Vorago_UidescLayout`, new `Vorago_Ecosystem_PageBindsRosterIds` | counts via constants; the page-6 set contains each knob ID; the allowlist is still `{4, 5}` |
| FR-060 / SC-030 | `unit/tail_samples_test.cpp` (new) | `Vorago_Processor_GetTailSamplesMatchesState` | default: `ceil((45+20+12)·48000) = 3 696 000`; Depth 1 → RT60 45 s; Age 1 → 6 s; Depth 1 + decay 60 → clamped to 60; Freeze On → `kInfiniteTail`; every factory preset equals the decoded-state computation exactly; `kGhostGrainTailSeconds == engineForTest()->atmosphere().getGrainSeconds()` after prepare |
| FR-001 / SC-001 | `unit/preset/factory_preset_test.cpp` (new) | `Vorago_FactoryPresets_CategoriesMatchConfig` | config list == `kCategories` == C-1 order; dirs == list both ways; no stray file |
| FR-003, FR-028 / SC-002 | same | `Vorago_FactoryPresets_ContainerAndInfo` | FR-028 fields; Info attributes byte-equal `buildVoragoInfoXml`; no `" & < >` in Comment |
| FR-029 / SC-003 | same | `Vorago_FactoryPresets_RoundTrip` | `setState` succeeds; `getState` is byte-identical |
| FR-030 / SC-004 | same | `Vorago_FactoryPresets_BrowserScan` | `PresetManager(config, nullptr, nullptr, tempUserDir, factoryRoot)`; count N; all `isFactory`; no empty subcategory; per-category counts == defs; tab labels == `{"All"} ∪ names` |
| FR-004/005/006/007/008/009 / SC-005 | same | `Vorago_FactoryPresets_StreamShape` | version == `kCurrentStateVersion` and length == `kStateV3Bytes` (P-2); names valid, unique and ASCII; polyphony ≤ 3; `A ≤ 180`, `Rel ≤ 60`; floats finite by bit pattern; ≥ 3 per category; N within the ruled band |
| FR-011 (static part) | same | `Vorago_PresetDefs_ClaimsWellFormed` | unique primaries; no primary in `kRecordedDefaultStateCells` (the constant the nightly aggregate asserts equal to the measured set, §5.10); every claim is a valid cell; every E-ext `CellSpec` has `extSide ∈ {−1, +1}` and `StateWithReversion` |
| FR-075 / C-2.3 | same | `Vorago_PresetDefs_EExtSidesExclusive` | for each ratified knob with both sides: a hand-built decoded state at `n₀ + δ_hi` passes `E{n}.hi`'s side predicate and fails `E{n}.lo`'s; the mirror displacement passes only `.lo`; `n₀` passes neither; with a synthetic reversion `d` above the bar, the harness's cell evaluator verifies exactly one of the pair |
| FR-011a | same | `Vorago_PresetMatrix_NonSubsetRule` | `findWitness` on hand-built vectors (§5.10): Q holding P's only claim at 1.5 < d < F reports SUBSET; d = 1.0, or `2·s(Q) > d`, yields witness `X` |
| FR-021/FR-032 / SC-006 | same | `Vorago_FactoryPresets_TreeMatchesGenerator` + `..._TreeToleranceProbe` `[.measure]` | §5.13; prints the per-field worst on every leg |
| FR-035 / SC-009 | same | `Vorago_PresetMatrix_ParameterSpaceDistinct` | §5.12; prints the minimum pair count |
| FR-034 / SC-014 | `integration/preset_sweep_test.cpp` | `Vorago_PresetSweep_ShortBounded` (per-push) | 8 s @48k, 4 s @44.1k, 4 s @96k, 8 s chord @48k at forced poly 4; finite and peak ≤ 0.9661; 2-thread pool |
| FR-033 / SC-012, SC-024 | same | `Vorago_PresetSweep_LongRender` `[long][vorago-sweep]` | §5.3 per shard; prints A, Rel, RT60 and every arm's number |
| FR-033a / SC-022 | same | `Vorago_PresetSweep_SustainAtAllRates` `[long][vorago-sweep]` | arm 1 over `[0, A+65]` at 44.1k and 96k |
| FR-037, FR-012 / SC-011 | same | `Vorago_PresetSweep_AblationVerifiesClaims` `[long][vorago-sweep]` | full vector (§5.6–5.9); every claim verified at its role's bar; vector written to the record |
| FR-038 / SC-015 | same | `Vorago_PresetSweep_RendersAreReproducible` `[long][vorago-sweep]` | two fresh hosts on parallel threads over `[0, A+65]`; `compareFingerprints(...).withinTolerance()` per channel |
| FR-011a, FR-013 / SC-008, SC-018, SC-029 | `integration/preset_matrix_test.cpp` | `Vorago_PresetMatrix_CoverageComplete`, `Vorago_PresetMatrix_NoShowcaseSubset` `[long][vorago-sweep][vorago-aggregate]` | computed from records (§5.10): every cell has a factory-preset verifier (index < N, default surface excluded); E1..E5 distinct primaries; no primary in the measured default-state set; measured set == `kRecordedDefaultStateCells`; `findWitness` for every ordered pair; prints the full matrix, the default-state cells and the witnesses |
| FR-015, FR-036 / SC-010 | same | `Vorago_PresetSweep_SoundSpaceDistinct` `[long][vorago-sweep][vorago-aggregate]` | §5.5, §5.11; prints min/median/max d, the minimum pair, every s(P), t_max, the controls and the floor |
| FR-017a | same | `Vorago_PresetPilot_Calibrate` `[.probe]` | §5.14; prints the inputs to the ruling |
| FR-039 / SC-016 | `integration/preset_load_rt_test.cpp` | `Vorago_FactoryPresets_SequentialLoadNoAlloc` | warm `ProcessorFixture` (48k/512, note held); for each preset, `setState`, then `AllocationScope` around the next 4 `process()` calls → 0 |
| FR-040 / SC-016 | same | `Vorago_FactoryPresets_ConcurrentLoadIsRtSafe` | streams prebuilt; a message thread loops `setState`; the audio thread renders 4 s under `ThreadScopedAllocationScope` → 0; every call returns `kResultOk`; output finite with peak ≤ 0.9661 |
| FR-041 / SC-017 | `integration/preset_cpu_test.cpp` | `Vorago_PresetCpu` `[.perf]` | §5.15 |
| FR-026 / FR-027 | compliance record (Stage I) | — | after building `Vorago`: list `%PROGRAMDATA%\Krate Audio\Vorago\`, confirm exactly the seven C-1 category directories and N `.vstpreset` files, and that the root equals `Platform::getFactoryPresetDirectory("Vorago")` (`preset_paths.h:27`, per spec); re-read `installers/windows/setup.iss:66-68` and `installers/linux/README.txt:29-44` against the final category set; cite the lines and the listing in `compliance.md` |
| FR-024 / SC-007 | CLI | `node tools/check-preset-generator-determinism.js --plugin vorago` | exit 0; the run without `--plugin` still passes for Seraphis |
| FR-063 / SC-021, FR-066 / SC-023, FR-065 / SC-019, FR-064, FR-042 / SC-020, SC-013 | compliance record | greps, CI run URLs, `-d yes` durations, the auval log line, listening notes | measured, not asserted in code |

`preset_sweep_test.cpp` and `preset_matrix_test.cpp` share `sweepRecordFor` through the header's
function-local static cache. When both run in one process (a local full run), the aggregate reuses
the records the shard cases produced.

---

## 8. Build integration

- `dsp/include/krate/dsp/systems/vorago_voice.h` and `vorago_engine.h` change (P-1). No DSP CMake
  change is needed, because `dsp/tests/unit/systems/vorago_param_surface_test.cpp` is already
  registered (`dsp/tests/CMakeLists.txt:551`, fast-math exemption `:1029`). Run `dsp_systems_tests`.
- Root `CMakeLists.txt`: add `vorago_preset_generator` and `generate_vorago_presets` (§4.7).
- `plugins/vorago/tests/CMakeLists.txt`:
  - Add `unit/state_v3_test.cpp`, `unit/tail_samples_test.cpp`, `unit/preset/factory_preset_test.cpp`,
    `integration/preset_sweep_test.cpp`, `integration/preset_matrix_test.cpp`,
    `integration/preset_load_rt_test.cpp`, `integration/preset_cpu_test.cpp` and
    `integration/ecosystem_rule_probe_test.cpp`.
  - Add all of them except `preset_cpu_test.cpp` to the `-fno-fast-math -fno-finite-math-only` list,
    because they do bit-pattern checks. The CPU TU stays out for the same reason
    `processor_cpu_test.cpp` does.
- `plugins/vorago/CMakeLists.txt`: no change. `krate_plugin_install_presets` is already called (spec
  FR-026), and `tail_estimate.h` is header-only.
- Stage I install verification (FR-026, FR-027): after the `Vorago` build, list the
  `%PROGRAMDATA%\Krate Audio\Vorago\` tree (seven category directories, N files). `setup.iss:66-68`
  installs `presets\*` recursively to `{commonappdata}\Krate Audio\Vorago` (read this session), and
  `installers/linux/README.txt:29-44` copies `presets/*` wholesale (read this session). Both are
  category-agnostic, so the check confirms they stay accurate with the final set. The evidence goes in
  `compliance.md` with line citations.
- Targets to build and run: `dsp_systems_tests`, `vorago_tests`, `vorago_preset_generator`,
  `generate_vorago_presets`, and `Vorago` (for pluginval). `seraphis_tests` is NOT affected, because
  no shared header is touched; `node tools/check-seraphis-green.js` staying in scope confirms it.
- Gates before commit:
  - zero warnings (MSVC);
  - `node tools/check-portability.js`, then `wsl --shutdown`;
  - clang-tidy `vorago` and `dsp`;
  - pluginval 5;
  - the determinism script.

---

## 9. Cost model and CI sizing (estimates, to be replaced by measurement)

Anchor: the default processor at polyphony 4 costs about 1.20 × `kReferenceNs` = 3.83e6 ns per
10.67 ms block (roadmap Phase 11 status), or about 0.36 × real time. A single held note is estimated at
about 0.15 × real time, because the global stages dominate. Per preset (average `A` ≈ 60 s):

| Render | Audio s | Count |
|---|---|---|
| main C-6 | ≈ 350 | 1 |
| ablation S/M/D/E-ext (after §5.9 skips) | ≈ 125 each | ≈ 17 |
| E routes | ≈ 125 each | 12 |
| 44.1k + 96k (≈ 2.2× cost) | 125 × 3.2 | 1 |
| reproducibility | 125 × 2 | 1 |
| **total** | **≈ 4 700 s** | ≈ 12 min local wall, ≈ 25–35 min CI |

- **Nightly load.** With N ≈ 45, that is about 20–26 CI-runner-hours per OS per night. Each job has
  60 % of 180 min (108 min) and runs 2 threads on a 2–4 vCPU runner. That needs about 10–14 shards
  per OS, or 30–42 jobs.
- **Aggregate job.** It renders the controls only: about 10 × 350 s, or 15–25 min on CI.
- **Per-push additions.**
  - Short guard: ≈ 45 × (~28 s single-note-equivalent × 0.15) ≈ 3 min locally, ≈ 6–9 min on CI (P-9).
  - Container, tree and parameter-space tests: < 30 s.
- **Measurement first.** The first task of Stage H measures the real per-preset wall clock, locally
  (`-d yes`) and on one `workflow_dispatch` nightly. Those numbers set `n`, and SC-013 is judged only
  on CI logs.

---

## 10. Risks and mitigations

| Risk | Mitigation |
|---|---|
| A material swap or a single macro may not move the descriptor by F (the primary bar) | the pilot includes one material preset (Glass Well) and one macro preset (Pressure Front); a failing primary is re-authored with stronger contrast against the other material and the stored settings, never given a relaxed bar; a persistent failure is an FR-017 finding |
| The default-state set differs from the P-3 prediction | FR-012's default-surface run is the first sweep executed; the primary list and the default-state secondaries (§6 step 4) are re-derived from it before authoring; nightly `CoverageComplete` asserts the measured set equals `kRecordedDefaultStateCells` |
| Knob at a range end (`predation 0.5` disables exchange exactly, `:487-489`) | the probe renders the true extremes; predation's E-ext `.lo` is 0.0, not 0.5 |
| A non-thread-safe static in KrateDSP breaks parallel renders | the reproducibility arm renders its two takes on two threads; the pilot compares a 1-thread and a 4-thread record within fingerprint tolerance |
| Memory on 7 GB runners | streaming capture (≤ 69 MB per main render) and ≤ 4 concurrent jobs |
| `-fno-fast-math` TUs instantiate inline DSP code with different flags (COMDAT; memory note "render goldens: harvest inside the consuming test binary") | no render is compared across binaries: the tree test compares state bytes against a measured tolerance, and reproducibility compares within one binary |
| NaN/Inf under macOS `-ffast-math` | every finite check uses `Krate::DSP::detail::isFinite` (bit pattern); the new TUs join the exemption list |
| Narrowing in brace init (Clang) | designated initializers for `ProcessSetup`, `RenderSpec` and the `CellSpec` tables; `static_cast` on every size_t→int32 |
| MSVC C4996 on `std::getenv` | `readEnv` uses `_dupenv_s` under `_MSC_VER` |
| `std::jthread` missing on AppleClang | plain `std::thread` + join |
| Denormals in long tails | the processor's `ScopedDenormalMode` (`processor.cpp:294`) covers renders; analysis is in double |
| `kernelSigma` setter cost on every broadcast | the forwarder early-outs an unchanged value |
| The v3 bump breaks hosts' saved v2 state | the v2 load path is tested (SC-027); version > 3 is still rejected |
| Controller interface freeze at 1.0.0 | this phase adds no interface; FR-064 note in CLAUDE.md |
| The pre-existing per-push roster may already exceed SC-013's 80 % | measured from the CI log before C merges; an existing overrun is an FR-017 finding, and nothing is trimmed to hide it |
| Stored-On freeze leaves an empty tank | authoring rule: Freeze stored Off; D10.1 verified by gesture (P-8) |
| The ghost density scheduler replays stale capture after release (spec edge case) | the Freeze-Off tail arm catches it; it is an FR-017 finding, and the window is never widened |

---

## 11. Open questions for the user (G1)

1. **R**: ratify the probe's proposed 4–6 knobs (FR-071), with P-4's one-sided cells for knobs whose
   default is a range end.
2. **P-1**: accept the append-only `vorago_voice.h` / `vorago_engine.h` edits (the friend probe now;
   forwarders and `VoragoVoiceParams` fields after G1).
3. **P-3**: for SC-029's band, raise the ceiling to `38 + |E-ext|` (recommended), prefer one-sided
   knobs, or cut |R|.
4. **P-2 / P-5**: amend FR-006/SC-005/C-9 to "current version, `kStateV3Bytes`", and SC-029's primary
   list to "non-default-state members".
5. **P-6**: accept the record-artifact design as satisfying FR-036's "one process".
6. **P-8**: make D10.1 a secondary of the S8 preset, verified on its gesture render.

**Ruled 2026-09-27, before the build stage (spec Clarifications, "Plan stage"):** items 2–6 as
recommended — R-1 (P-1 dsp edits), R-2 (N == 38 + |E-ext|), R-3 (P-2/P-5 amendments, applied to the
spec), R-4 (P-6 records), R-5 (P-8 secondary). Also R-6 (the CPU runner excludes `[vorago-sweep]`,
§4.11/§5.15), R-7 (the CMake registration split and the `LibraryShape` case) and R-8 (the descriptor
lands in `preset_test_support.h` before the probe, no duplicate copy — §1, §4.3). **Only item 1 (`R`,
with P-4's one-sided cells and the §5.8 E-ext margin) remains for G1.**

---

## Review notes

Review round (2026-09-27): all seven issues applied, none rejected.
- E-ext pairs (major): side predicates (§5.8), `CellSpec::extSide`, `StateWithReversion` for E-ext,
  a skip rule (§5.9) and `Vorago_PresetDefs_EExtSidesExclusive`. The halfway margin is a proposed
  ruling, ratified with R at G1.
- Coverage (major): coverage counts factory presets only. Default-state cells are attached as
  secondaries (§6 step 4 and its table), and P-5 is corrected.
- Non-subset (major): one rule in §5.10 (secondary bar; F only for Q's own primary). The "bar P's claim
  carries" wording is removed, and `findWitness` is unit-tested on hand-built vectors.
- C-9 macOS (minor): the probe is measured on the macOS leg before the pin, and the pin covers all
  three toolchains.
- `state_roundtrip_test.cpp` (minor): added to §4.2 and §7 (lines read this session).
- FR-026/FR-027 (minor): a Stage I verification step (§1, §7, §8).
- Default-state drift (minor): nightly asserts that no primary is in the measured set and that the
  measured set equals `kRecordedDefaultStateCells`.
