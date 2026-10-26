# Implementation Plan: Profundum Phase 10 — Plugin Scaffold

**Spec:** `specs/profundum-phase10-plugin-scaffold/spec.md` (reviewed; Clarifications Q1–Q7, 2026-10-22)
**Roadmap:** `specs/Profundum-roadmap.md` Part B (lines 748–774) and the Phase 9 handoff (line 682 ff.)
**Template:** the shipped `plugins/vorago/` tree and `specs/vorago-phase11-plugin-scaffold/plan.md` (same section layout)
**Branch:** `synth/profundum`, base `285613c5`
**Status:** PLAN — ready for tasks. Nothing implemented.
**Date:** 2026-10-22

---

## 0. How to read this plan

- Every claim about existing code cites a `file:line` read in this planning session at `285613c5`.
  "engine.h" is `dsp/include/krate/dsp/systems/profundum_engine.h`. "V-proc" is
  `plugins/vorago/src/processor/processor.cpp`. "V-ctl" is `plugins/vorago/src/controller/controller.cpp`.
- §1.3 lists the places where the plan corrects or tightens a spec premise. None of them relaxes a threshold.
  Two items need a ruling from the user (§8).
- **No DSP is written and no file under `dsp/` is touched** (FR-090). Every component below is plugin-local, in the new
  top-level namespace `Profundum`, and composes the Phase 9 `ProfundumEngine` unchanged.
- Layer: none. Plugin code sits above KrateDSP Layer 4 and includes only public `<krate/dsp/...>` headers (engine.h,
  `profundum_macro_matrix.h`, `smoother.h`, `scoped_denormal_mode.h`, `db_utils.h`).

---

## 1. Architecture

### 1.1 Object graph

```
Profundum::Processor : Steinberg::Vst::AudioEffect          (audio thread + host control calls)
 ├─ std::unique_ptr<Krate::DSP::ProfundumEngine> engine_    heap (engine.h:92–93: "Heavy (eight voices,
 │                                                           each about 100 KB): construct it with make_unique")
 ├─ GlobalParams globalParams_                                4 atomics (IDs 0–3)
 ├─ MacroParams  macroParams_                                 4 atomics (IDs 100–103)
 ├─ Krate::DSP::OnePoleSmoother outputGain_{1.0f}             wrapper-side gain, after the limiter
 ├─ std::array<EventSlot, 1024> eventOrder_                   allocation-free stable insertion sort (8 KiB)
 └─ bool prepared_, snapGainPending_; uint32 lastSliceCount_

Profundum::Controller : Steinberg::Vst::EditControllerEx1    (UI thread; NO IMidiMapping, NO INoteExpressionController)
 ├─ 8 registered parameters (registerGlobalParams + registerMacroParams)
 ├─ std::unique_ptr<Krate::Plugins::PresetManager> presetManager_   from makeProfundumPresetConfig()
 └─ createView("editor") -> new VSTGUI::VST3Editor(this, "editor", "editor.uidesc")
```

The processor and the controller share only `plugin_ids.h` and the two `parameters/*.h` packs. Neither includes the
other's header (Constitution I).

### 1.2 Signal path inside `process()`

```
ScopedDenormalMode                                   (FR-024.1; scoped_denormal_mode.h:60)
processParameterChanges(last point per queue)        (FR-024.2, FR-042, FR-043) -> atomics
shape guards 1–7                                      (FR-030)  -> early return, engine untouched
pushParamsToEngine()  once per call                   (FR-024.4) setVoiceMode / setPolyphony / setOutputCeilingDb /
                                                                 setMacros  (+ setTempoBpm if kTempoValid, FR-027)
outputGain_.setTarget(atomic); snap if pending        (FR-028)
buildEventOrder()                                     (FR-025)
for each slice [cursor, sliceEnd):
    dispatch every event with effectiveOffset <= cursor  -> engine_->noteOn / noteOff (FR-026)
    engine_->processBlock(outL+cursor, outR+cursor, len)   OVERWRITES both buffers (engine.h:420–422)
    applyOutputGain(outL+cursor, outR+cursor, len)          per-sample smoother, multiplier <= 1
silenceFlags = 0                                      (FR-024.6)
```

The engine's whole chain sits inside `processBlock`: voice sum → MassDynamics → BassStereoField → DCBlocker × 2 →
TruePeakLimiter with a 64-sample look-ahead (engine.h:12–14, :107, :443–444). The wrapper adds exactly one operation
after it, a multiply by a gain in `[0, 1]`. That is why the Phase 9 mono-sum, Anchor L == R and aliasing results carry
over unchanged. SC-008 proves the carry-over with a 0-mismatch comparison.

### 1.3 Spec premise corrections and tightenings (none relaxes a threshold)

| # | Spec item | Finding (cited) | Plan decision |
|---|---|---|---|
| P-1 | SC-015 absolute bars (MONO ≤ 8 %, POLY4 ≤ 25 %) | Phase 9 measured 7.5798 % / 24.005 % on its "everything on" showcase setting: excitation mix 1 / drive 1, Fold 100, Character Amount 1, Anchor FOLLOW, Drop retriggered every 250 ms, the morph ramping, host block 64 (`dsp/tests/unit/systems/profundum_engine_perf_test.cpp:9–16`, bars `:109–110`). The Phase 10 processor runs the `MorphState{}` defaults, where `foldAmount = 0` and `characterAmount = 0` (`morph_state.h:105, :108`). The parameters that reach the showcase setting are Phase 11's | The gated arms stay as the spec defines them, at the registered defaults. `compliance.md` says that their percentages are **not** comparable to Phase 9's. The plan adds a third arm, **"macros-max"**: all four macros at 1.0, the heaviest state the eight shipped parameters can reach (Clean↔Violent drives excitation, Fold and Character amount; `profundum_macro_matrix.h:44–45`). **Ruled Q-A (user, 2026-10-22): gated** — MONO-MAX ≤ 8 % and POLY-MAX ≤ 25 % beside the default-state arms (spec SC-015 row) |
| P-2 | FR-032 / SC-014 ("the only latency") | The file header at engine.h:15 still reads *"TruePeakLimiter (FR-025). Latency 0."*. That is stale: engine.h:105–107 and :442–444 give 64 | The wrapper follows :107 / :444. The stale comment lives in `dsp/`, which FR-090 forbids touching, so it is only **recorded** in `compliance.md` as a note for a later dsp change |
| P-3 | FR-031 "`setActive(false)` MUST call `engine_->reset()`" | `reset()` drives every component's reset (engine.h:201–254). No Phase 9 case calls it on a **never-prepared** engine, but the pluginval out-of-order path (`setActive(false)` before `setupProcessing`) would reach that untested state | The processor calls `engine_->reset()` when `engine_ && prepared_`. A prepared processor always resets, and SC-017.4 runs on a prepared one. The only skipped case is a never-prepared or refused engine, which the next successful `prepare()` resets anyway (engine.h:195–196). This tightens FR-031; it does not change it |
| P-4 | "exactly 0.0f" in SC-017.1 / .3 / .4 / .9 and SC-016 | The engine fills its PostSum buses with `-0.0f` (engine.h:508–516). That path is inactive under PerVoice, but a `-0.0f` sample is a legal silent output | Comparisons use **value** equality `x == 0.0f`, which admits `-0.0f`, and never bit identity. The sentinel-overwrite check (SC-017.3) compares bits against the sentinel `0.123f`, so an unwritten sample is still caught |
| P-5 | SC-017.1 / .4 and SC-009(a) assume an idle engine renders exact zero | **Phase 9 does not prove this.** `VoiceEngine_RendersNonSilent` checks that the buffers are written (`profundum_engine_test.cpp:366–374`), not that idle output is exactly 0, and no other engine case asserts it | Build task 1 is a 30-line probe in `lifecycle_test.cpp`: a prepared engine, no notes, 4096 samples, every sample `== 0.0f`, and the same 1 s after a released note. If it fails, the build **stops and reports**, because the cause would be in `dsp/` (FR-090). No bar is widened. **D-1 (user ruling 2026-10-22):** the 1 s clause read a wrong premise — the voices are exactly silent 0.100 s after the note-off, and the output then carries the global 5 Hz DC blocker's own decay (τ 31.8 ms) of the last voice sample, flushed to exact zero by its denormal guard at 1.011 s (+ 64 look-ahead); Phase 9's `VoiceEngine_ModeSwitch` read the same tail at 1.3 s. The clause now reads: Sum tap exactly zero by 0.125 s, \|out\| ≤ 1e-6 from 0.5 s, exact zero from 1.25 s after the note-off (tasks T006, hidden `Profundum_IdleTailProbe`). No `dsp/` change |
| P-6 | FR-070–FR-080 roster list and the SC-019 allow-list | One plugin roster outside `plugins/` is in neither list: `.claude/workflows/release-readiness.js:13–22` (`PLUGIN_MAP`), read by the `release-readiness` skill that Phase 13 uses as its release gate | **Ruled Q-B (user, 2026-10-22): edited in Phase 10** — `profundum: { testTarget: 'profundum_tests', bundle: 'Profundum.vst3' }` added to `PLUGIN_MAP` (spec FR-080), the file joins the SC-019 allow-list |
| P-7 | Roster completeness | `tools/ci-test-times.json:2–23` has no `profundum_tests` entry. `tools/ci-test-shard.js:136` treats an unknown suite as 60 s (*"unknown suite: assume a minute"*), so nothing fails | No edit, because the file is not on the SC-019 allow-list. `compliance.md` records the measured per-push wall clock for a later refresh |
| P-8 | SC-008 / SC-016.4: 0 mismatches against an independently constructed engine | The processor pushes all five setters on every block (FR-024.4). That is a no-op only if each pushed value equals the engine's pending default. Every setter is change-only (engine.h:25; `applySettings` compares `p` with `a`, :678–736), and the matrix stores plain values (`profundum_macro_matrix.h:166–181`) | Each pack default is **derived** from the engine constant, never re-typed (`kDefaultPolyphony`, `kDefaultCeilingDb`, `ProfundumMacroValues{}`). `static_assert`s tie them together (§2.3, §2.4) |
| P-9 | FR-060 test-target shape | `plugins/vorago/tests/CMakeLists.txt:171–172` ends with `krate_test_pch(vorago_tests)` after the per-source flag blocks. FR-060 does not mention it | The plan adds `krate_test_pch(profundum_tests)` in the same position |
| P-10 | FR-043 maps voice mode with `setVoiceMode(VoiceMode{index})` | Brace-initialising `enum class VoiceMode : std::uint8_t` (engine.h:97) from an `int` is narrowing: Clang rejects it and MSVC accepts it | The plan uses `idx == 1 ? VoiceMode::Poly : VoiceMode::Mono` and never brace-casts the enum |
| P-11 | SC-007 zero allocations | `Krate::Test::EventList::addEvent` and `addNoteOn` push into a `std::vector` (`tests/test_helpers/vst_event_list.h:57–87`), and so does `ParameterChanges::addChange` (`vst_param_changes.h:105–118`). `AllocationScope::getAllocationCount()` holds a value only after the scope's destructor (`allocation_detector.h:111–124`) | Every event list and parameter container is built **before** the scope. Inside the scope the test reads `TestHelpers::AllocationDetector::instance().getAllocationCount()` (the Vorago pattern, `plugins/vorago/tests/unit/lifecycle_test.cpp:252–254`) and calls `proc->process(data)` directly, with no Catch2 macro. It asserts after the scope |
| P-12 | FR-023 step 6, "return the base-class result" | `AudioEffect::setupProcessing` first overwrites `processSetup.maxSamplesPerBlock`, `processMode` and `sampleRate` (`extern/vst3sdk/public.sdk/source/vst/vstaudioeffect.cpp:151–153`). Only then does it refuse any size other than `kSample32` (`:155–156`; `canProcessSampleSize` at `:170–172`). Under a literal reading of FR-023, `engine_->prepare` (step 3) and `prepared_ = true` (step 6) have already run by then, so a refused 64-bit setup leaves the processor armed. Guard 3 does **not** catch a 64-bit block. `channelBuffers32` and `channelBuffers64` are members of one anonymous union (`extern/vst3sdk/pluginterfaces/vst/ivstaudioprocessor.h:209–213`), so in a `kSample64` block `channelBuffers32` is the non-null `channelBuffers64` pointer, reinterpreted. If a non-conforming host processed 64-bit buffers anyway, it would get `numSamples` float32 values written into `double` buffers. The writes stay in bounds, but the output is garbage | **This tightens FR-023 step 2: it refuses more, never less.** `setupProcessing` checks `canProcessSampleSize(setup.symbolicSampleSize) != kResultTrue` next to the rate check, **before** `engine_->prepare`. On a refusal it sets `prepared_ = false` and returns `kResultFalse` without calling the base class, so `processSetup` keeps the last accepted setup (§2.5.4 step 2b). `process()` then takes the not-ready path. FR-030's binding guard list does not change. If a host still sends a 64-bit block after the refusal, guard 7's `std::fill_n` writes `numSamples` float zeros. That is in bounds (half the bytes of the double buffer) and never touches the engine. SC-017 `SampleSize64` proves the refusal (§4.3) |
| P-13 | Macro titles | The spec prose writes "Dark ↔ Bright". `STR16` has a non-C++11 fallback that goes through a narrow-string table (`extern/vst3sdk/pluginterfaces/base/fstrdefs.h:31`), so non-ASCII titles are a portability risk | The registered titles are ASCII: `"Weight"`, `"Dark/Bright"`, `"Tight/Loose"` and `"Clean/Violent"`. Titles are display strings, not part of the permanent ID / type surface (roadmap line 872), and Phase 12 owns labels (OQ-10) |

---

## 2. Component-by-component design

Every file below is new. The ODR sweep for each type is recorded in the spec's §New components. This session re-swept
the constant names: `grep -rn "kOutputGainSmoothMs\|makeProfundumEngineConfig" dsp/ plugins/ tests/` returns 0 hits.

### 2.1 `plugins/profundum/src/plugin_ids.h` (FR-011 – FR-014)

```cpp
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"
#include <cstddef>

namespace Profundum {

/// FR-012. Shared by processor and controller; neither includes the other.
constexpr Steinberg::int32 kCurrentStateVersion = 1;

/// FR-044: version (4) + global block (4 + 4 + 4 + 4) + macro block (4 x 4).
constexpr std::size_t kStateV1Bytes = 36;
static_assert(kStateV1Bytes == 4 + (4 + 4 + 4 + 4) + 4 * 4, "spec FR-044 byte layout (plan 3.4)");

/// FR-011. Fresh v4 GUIDs generated 2026-10-22 with Node crypto.randomUUID(); compared
/// against the sixteen registered FUIDs (spec New components): no match. NEVER change.
static const Steinberg::FUID kProcessorUID(0x94C64960, 0xF7E844BE, 0xB439A0F3, 0x95AF84C9);
static const Steinberg::FUID kControllerUID(0x449C9192, 0xE39F4DA0, 0xAD6E7BCA, 0x260F4CA0);

/// FR-014. `const`, not constexpr; GCC-13 rationale copied from plugins/vorago/src/plugin_ids.h:65-72.
static const char* const kSubCategories = "Instrument|Synth";

/// FR-013. Reserved band map (roadmap lines 762-765):
///   0-99 Global | 100-199 Macros + Mass + Morph position | 200-299 Core | 300-399 Anchor
///   400-499 Drop / note | 500-599 Fold / Shape | 600-699 Character | 700-799 Mass dynamics
///   800-899 Stereo field | 900-999 Motion | 1000-1099 Morph control. Phase 11 owns 104+ and 200+.
enum ParameterIDs : Steinberg::Vst::ParamID {
    kOutputGainId = 0,
    kVoiceModeId = 1,
    kPolyphonyId = 2,
    kOutputCeilingId = 3,
    kGlobalParamRangeEnd = 100,

    kMacroWeightId = 100,
    kMacroDarkBrightId = 101,
    kMacroTightLooseId = 102,
    kMacroCleanViolentId = 103,
    kMacroParamRangeEnd = 200,
};
}  // namespace Profundum
```

The GUIDs are pinned here so that `tasks.md` is deterministic:

- processor `94C64960-F7E8-44BE-B439-A0F395AF84C9`;
- controller `449C9192-E39F-4DA0-AD6E-7BCA260F4CA0`;
- installer `AppId` `{4DDA5FC2-4CD6-44E2-BF11-18887F0F6CD9}`, which differs from all eight `AppId`s at
  `plugins/*/installers/windows/setup.iss:18`.

### 2.2 `plugins/profundum/src/engine/profundum_engine_config.h` (FR-053)

```cpp
#pragma once
#include <krate/dsp/systems/profundum_engine.h>

namespace Profundum {
/// FR-053: ProfundumEngineConfig{} unchanged -- PerVoice, sidechain oversampling on at 4x
/// (profundum_engine.h:84-90). Phase 9 measured its CPU figures with exactly this configuration
/// (phase-handoffs 10.1, 10.6, 10.9); any departure must be justified here against them.
[[nodiscard]] inline Krate::DSP::ProfundumEngineConfig makeProfundumEngineConfig() noexcept {
    return Krate::DSP::ProfundumEngineConfig{};
}
/// FR-023 step 4, FR-028: the output-gain ramp (99 % settle; plan 3.1).
inline constexpr float kOutputGainSmoothMs = 20.0f;
}  // namespace Profundum
```

### 2.3 `plugins/profundum/src/parameters/global_params.h` (FR-040, FR-043, FR-045, FR-047)

The pack follows the contract of `plugins/vorago/src/parameters/global_params.h`: the struct at `:40` and the six
functions at `:62, :102, :145, :176, :186, :235`. Its includes are:

- `plugin_ids.h` and `ui/parameter_helpers.h` (shared);
- `base/source/fstreamer.h`, `pluginterfaces/base/ustring.h` and `public.sdk/source/vst/vstparameters.h`;
- `<krate/dsp/core/db_utils.h>` and `<krate/dsp/systems/profundum_engine.h>`.

```cpp
namespace Profundum {
using Engine = Krate::DSP::ProfundumEngine;

inline constexpr double kOutputGainFloorDb = -60.0;                 // Q1
inline constexpr int    kNumPolyphonyEntries = 8;                   // "1".."8"
static_assert(kNumPolyphonyEntries == static_cast<int>(Engine::kMaxVoices));        // FR-043; engine.h:99
static_assert(Engine::kMinCeilingDb == -12.0f && Engine::kMaxCeilingDb == 0.0f);   // engine.h:108-109

// Defaults DERIVED from the engine (P-8), so the per-block push is a no-op on a default engine.
inline constexpr float kDefaultOutputGain = 1.0f;
inline constexpr int   kDefaultVoiceModeIndex = 0;                                       // Mono (engine.h:461)
inline constexpr int   kDefaultPolyphony = static_cast<int>(Engine::kDefaultPolyphony);   // 4 (engine.h:100)
inline constexpr float kDefaultCeilingDb = Engine::kDefaultCeilingDb;                    // -1 (engine.h:110)

struct GlobalParams {
    std::atomic<float> outputGain{kDefaultOutputGain};       ///< linear [0, 1] (FR-028)
    std::atomic<int>   voiceMode{kDefaultVoiceModeIndex};    ///< 0 Mono, 1 Poly
    std::atomic<int>   polyphony{kDefaultPolyphony};         ///< [1, 8]
    std::atomic<float> outputCeilingDb{kDefaultCeilingDb};   ///< [-12, 0] dBTP
};

// ---- the ONE place each mapping is written (processor, controller and tests all call these) ----
[[nodiscard]] inline float  outputGainFromNormalized(double v) noexcept;   // plan 3.1
[[nodiscard]] inline double outputGainToNormalized(float g) noexcept;      // plan 3.1 inverse (FR-045)
[[nodiscard]] inline int polyphonyFromNormalized(double v) noexcept {
    return std::clamp(static_cast<int>(v * 7.0 + 1.0 + 0.5), 1, kNumPolyphonyEntries);
}
[[nodiscard]] inline int clampPolyphony(Steinberg::int32 n) noexcept {
    return std::clamp(static_cast<int>(n), 1, kNumPolyphonyEntries);
}
[[nodiscard]] inline float ceilingFromNormalized(double v) noexcept {
    return static_cast<float>(static_cast<double>(Engine::kMinCeilingDb)
                              + static_cast<double>(Engine::kMaxCeilingDb - Engine::kMinCeilingDb) * v);
}
[[nodiscard]] inline double ceilingToNormalized(float db) noexcept {
    return (static_cast<double>(db) + 12.0) / 12.0;
}

inline void handleGlobalParamChange(GlobalParams&, Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue v) noexcept;
inline void registerGlobalParams(Steinberg::Vst::ParameterContainer&);
inline Steinberg::tresult formatGlobalParam(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue,
                                            Steinberg::Vst::String128);
inline void saveGlobalParams(const GlobalParams&, Steinberg::IBStreamer&);
inline bool loadGlobalParams(GlobalParams&, Steinberg::IBStreamer&);   // EOF-safe; false at the first failed read
template <typename SetParamFunc>
inline void loadGlobalParamsToController(Steinberg::IBStreamer&, SetParamFunc);
}  // namespace Profundum
```

**`handleGlobalParamChange`.** `v` arrives already finite and clamped to `[0, 1]` (§2.5.8). Each ID stores with
`memory_order_relaxed`:

| ID | Stored value |
|---|---|
| `kOutputGainId` | `outputGainFromNormalized(v)` |
| `kVoiceModeId` | `v >= 0.5 ? 1 : 0` |
| `kPolyphonyId` | `polyphonyFromNormalized(v)` |
| `kOutputCeilingId` | `ceilingFromNormalized(v)` |
| any other ID | nothing (FR-042: IDs 4–99) |

**`registerGlobalParams`** (FR-047):

- `parameters.addParameter(STR16("Output Gain"), STR16("dB"), 0, 1.0, ParameterInfo::kCanAutomate, kOutputGainId)`.
- `createDropdownParameterWithDefault(STR16("Voice Mode"), kVoiceModeId, 0, {STR16("Mono"), STR16("Poly")})`, then
  `->getInfo().defaultNormalizedValue = 0.0`. The helper sets only the current value
  (`plugins/shared/src/ui/parameter_helpers.h:47–69`).
- `createDropdownParameterWithDefault(STR16("Polyphony"), kPolyphonyId, 3, {STR16("1") … STR16("8")})`, then
  `->getInfo().defaultNormalizedValue = 3.0 / 7.0`.
- `parameters.addParameter(STR16("Output Ceiling"), STR16("dBTP"), 0, 11.0 / 12.0, ParameterInfo::kCanAutomate,
  kOutputCeilingId)`.

**`formatGlobalParam`.**

- Output gain prints `"-inf dB"` when `v <= 0`, and otherwise `snprintf("%.1f dB", -60.0 + 60.0 * v)`, which gives
  "0.0 dB" at `v = 1`.
- The ceiling prints `"%.1f dBTP"` of `-12 + 12 v`.
- The two lists return `kResultFalse`, because they format themselves.

**`saveGlobalParams`** writes, in order: `writeFloat(outputGain)`, `writeInt32(voiceMode)`, `writeInt32(polyphony)`,
`writeFloat(outputCeilingDb)`.

**`loadGlobalParams`** reads in the same order. A failed read returns `false` and leaves every later field unchanged.
The FR-045 sanitising rules:

- a `float` field that fails `detail::isFinite` (`db_utils.h:118`) keeps the atomic;
- `outputGain` is clamped to `[0, 1]`;
- `voiceMode` is stored only if it is 0 or 1;
- `polyphony` goes through `clampPolyphony`;
- the ceiling is clamped to `[-12, 0]`.

**`loadGlobalParamsToController`** applies the same rules, then normalises:

- gain → `outputGainToNormalized(g)`;
- voice mode → `double(idx)`;
- polyphony → `(n − 1) / 7.0`;
- ceiling → `ceilingToNormalized(db)`.

An invalid `voiceMode` makes no call.

### 2.4 `plugins/profundum/src/parameters/macro_params.h` (FR-041)

```cpp
struct MacroParams {   // ProfundumMacro order (profundum_macro_matrix.h:38); explicit initialisers are load-bearing
    std::atomic<float> weight{Krate::DSP::ProfundumMacroMatrix::kNeutral};
    std::atomic<float> darkBright{Krate::DSP::ProfundumMacroMatrix::kNeutral};
    std::atomic<float> tightLoose{Krate::DSP::ProfundumMacroMatrix::kNeutral};
    std::atomic<float> cleanViolent{Krate::DSP::ProfundumMacroMatrix::kNeutral};
};
inline constexpr int kNumMacroParams = static_cast<int>(kMacroCleanViolentId - kMacroWeightId + 1);
static_assert(kNumMacroParams == static_cast<int>(Krate::DSP::ProfundumMacroMatrix::kNumMacros));   // :68
static_assert(static_cast<int>(kMacroDarkBrightId - kMacroWeightId)
              == static_cast<int>(Krate::DSP::ProfundumMacro::DarkBright));                          // :38
static_assert(Krate::DSP::ProfundumMacroValues{}.weight == Krate::DSP::ProfundumMacroMatrix::kNeutral);  // :58-67

inline std::atomic<float>& macroField(MacroParams&, int index) noexcept;              // switch 0..3; assert on default
inline const std::atomic<float>& macroField(const MacroParams&, int index) noexcept;
[[nodiscard]] inline Krate::DSP::ProfundumMacroValues macroValues(const MacroParams&) noexcept;  // relaxed loads
// + handleMacroParamChange / registerMacroParams / formatMacroParam / saveMacroParams /
//   loadMacroParams / loadMacroParamsToController -- the shape of
//   plugins/vorago/src/parameters/macro_params.h:108-211.
```

- **Handle:** an ID outside `[100, 103]` returns before the index is computed (FR-042; Vorago `:111–112`). Otherwise the
  handler stores `clamp(float(v), 0, 1)`.
- **Register:** four plain `Vst::Parameter`s, each with unit `"%"`, default `0.5`, `kCanAutomate` and ID `100 + i`. The
  titles are the ASCII ones from P-13.
- **Format:** `"%.0f%%"`.
- **Save and load:** four floats. A non-finite value is skipped, and every value is clamped to `[0, 1]`. The controller
  mirror is `setParam(100 + i, clamp(fv))`.

### 2.5 `plugins/profundum/src/processor/processor.{h,cpp}`

#### 2.5.1 Header

```cpp
#pragma once
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "parameters/global_params.h"
#include "parameters/macro_params.h"
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/profundum_engine.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Profundum {

class Processor : public Steinberg::Vst::AudioEffect {
public:
    static constexpr std::size_t kMaxEventsPerBlock = 1024;   // FR-025; 1024 x 8 B = 8 KiB

    Processor();
    ~Processor() override;
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs,
                                                     Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    // getTailSamples(): NOT overridden (SDK kNoTail; Phase 11 owns it).

    // ---- FR-033 test seams: const, allocation-free, never called by process() ----
    [[nodiscard]] const Krate::DSP::ProfundumEngine* engineForTest() const noexcept { return engine_.get(); }
    [[nodiscard]] const GlobalParams& globalParamsForTest() const noexcept { return globalParams_; }
    [[nodiscard]] const MacroParams& macroParamsForTest() const noexcept { return macroParams_; }
    [[nodiscard]] float outputGainValueForTest() const noexcept { return outputGain_.getCurrentValue(); }  // smoother.h:191
    [[nodiscard]] std::uint32_t lastSliceCountForTest() const noexcept { return lastSliceCount_; }

private:
    struct EventSlot {
        std::int32_t offset;     // clamped to [0, numSamples - 1]
        std::int32_t listIndex;  // index into data.inputEvents
    };

    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
    void pushParamsToEngine() noexcept;                                        // FR-023.5, FR-024.4
    std::size_t buildEventOrder(Steinberg::Vst::IEventList* events, std::size_t total) noexcept;
    void dispatchEvent(const Steinberg::Vst::Event& e) noexcept;
    void applyOutputGain(float* outL, float* outR, std::size_t n) noexcept;

    std::unique_ptr<Krate::DSP::ProfundumEngine> engine_;     // FR-022: heap only
    GlobalParams globalParams_{};
    MacroParams macroParams_{};
    Krate::DSP::OnePoleSmoother outputGain_{1.0f};            // smoother.h:148-155 (explicit initial value)
    bool snapGainPending_ = true;
    bool prepared_ = false;
    std::uint32_t lastSliceCount_ = 0;
    std::array<EventSlot, kMaxEventsPerBlock> eventOrder_{};
};
static_assert(sizeof(Processor) < 64u * 1024u, "the ~800 KB engine must live on the heap (FR-022, FR-064)");
}  // namespace Profundum
```

**Threading.** `prepared_`, `snapGainPending_` and `lastSliceCount_` are plain members. They are written only in
`setupProcessing`, `setActive` and `process`, and the VST3 host contract never runs those concurrently. This is the
Vorago precedent (`plugins/vorago/src/processor/processor.h:256–260`). `setState` can run concurrently with `process`,
so it touches **atomics only**.

#### 2.5.2 Constructor, `initialize()`, `terminate()` (FR-020, FR-022)

- **Constructor:** `setControllerClass(kControllerUID)` (model V-proc:70).
- **`initialize`:**
  1. `AudioEffect::initialize(context)`; return early on failure.
  2. `addEventInput(STR16("Event In"))` and `addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo)`. Never call
     `addAudioInput` (V-proc:130–149; copy the `-10875` comment).
  3. `engine_ = std::make_unique<Krate::DSP::ProfundumEngine>()`.
  4. Never call `setObserver` (FR-022).
- **`terminate`:** `engine_ = nullptr`, then `prepared_ = false`, then `AudioEffect::terminate()`. Assign `nullptr`;
  never call `.reset()`: V-proc:151–158 explains the readability-ambiguous-smartptr-reset-call.

#### 2.5.3 `setBusArrangements()` (FR-021)

This is V-proc:167–181 verbatim. Each of the following returns `kResultFalse`:

- `numIns != 0`;
- `numOuts != 1`;
- `outputs == nullptr`;
- `outputs[0] != SpeakerArr::kStereo`.

Any other arrangement returns `kResultTrue`.

#### 2.5.4 `setupProcessing()` (FR-023, Q6)

```cpp
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    // 1. Out-of-order host call: no engine -> stay unprepared.
    if (engine_ == nullptr) { prepared_ = false; return AudioEffect::setupProcessing(setup); }
    // 2. Q6: refuse, never clamp. db_utils.h:125 (double overload) is fast-math immune.
    const double sr = setup.sampleRate;
    if (!Krate::DSP::detail::isFinite(sr) || sr < Krate::DSP::ProfundumEngine::kMinSampleRate
        || sr > Krate::DSP::ProfundumEngine::kMaxSampleRate) {          // engine.h:118-119
        prepared_ = false;
        return kResultFalse;                                             // the engine is NOT prepared by this call
    }
    // 2b. P-12: refuse a sample size the base class would refuse, and do it BEFORE prepare. The base class
    //     overwrites processSetup before it refuses (vstaudioeffect.cpp:151-156). Guard 3 cannot catch a
    //     64-bit block, because channelBuffers32/64 share a union (ivstaudioprocessor.h:209-213).
    if (canProcessSampleSize(setup.symbolicSampleSize) != kResultTrue) {   // vstaudioeffect.cpp:170-172
        prepared_ = false;
        return kResultFalse;
    }
    // 3.
    engine_->prepare(sr, makeProfundumEngineConfig());                   // engine.h:147; allocates (control thread)
    // 4.
    outputGain_.configure(kOutputGainSmoothMs, static_cast<float>(sr)); // smoother.h:160
    snapGainPending_ = true;
    // 5. setState() may have run first. The engine keeps pending values across prepare (engine.h:144-146),
    //    and these land at the first grid point (applySettings, engine.h:678).
    pushParamsToEngine();
    // 6.
    prepared_ = true;
    return AudioEffect::setupProcessing(setup);
}
```

- Neither refusal path (step 2 or step 2b) calls the base class, so `processSetup` keeps the last accepted setup.
- After step 2b, the base class's own size check in step 6 always passes, so step 6 returns `kResultOk`.
- The order of steps 3 and 5 is the spec's own. Pushing before `prepare` would be observably the same, because
  `reset()` hands every pending value at once (engine.h:201; `applySettings(true)` at :231).

#### 2.5.5 `setActive()` (FR-031)

```cpp
if (state != 0) { snapGainPending_ = true; }                         // no allocation (SC-017.7)
else if (engine_ != nullptr && prepared_) { engine_->reset(); }      // P-3; engine.h:199-201: control thread, bounded
return AudioEffect::setActive(state);
```

#### 2.5.6 `process()` (FR-024, FR-025, FR-029, FR-030)

The skeleton is V-proc:306–459 without the pedal, cavern and frame paths.

```cpp
tresult PLUGIN_API Processor::process(ProcessData& data) {
    const Krate::DSP::ScopedDenormalMode denormalGuard;            // FIRST statement (FR-024.1)
    processParameterChanges(data.inputParameterChanges);           // FR-024.2 (before the guards)
    if (data.numOutputs <= 0 || data.outputs == nullptr) return kResultOk;            // guard 2
    if (data.outputs[0].channelBuffers32 == nullptr) return kResultOk;                // guard 3
    if (data.outputs[0].numChannels < 2) return kResultOk;                            // guard 4
    if (data.numSamples <= 0) return kResultOk;                                       // guard 5
    const auto total = static_cast<std::size_t>(data.numSamples);
    float* outL = data.outputs[0].channelBuffers32[0];
    float* outR = data.outputs[0].channelBuffers32[1];
    if (outL == nullptr || outR == nullptr) return kResultOk;                         // guard 6
    if (!prepared_ || engine_ == nullptr) {                                           // guard 7
        std::fill_n(outL, total, 0.0f);
        std::fill_n(outR, total, 0.0f);
        data.outputs[0].silenceFlags = 3;
        return kResultOk;
    }
    pushParamsToEngine();                                          // FR-024.4
    if (data.processContext != nullptr
        && (data.processContext->state & ProcessContext::kTempoValid) != 0) {
        engine_->setTempoBpm(static_cast<float>(data.processContext->tempo));   // FR-027; engine.h:299-303 guards
    }
    outputGain_.setTarget(globalParams_.outputGain.load(std::memory_order_relaxed));   // smoother.h:170
    if (snapGainPending_) {
        outputGain_.snapTo(outputGain_.getTarget());                                    // smoother.h:263
        snapGainPending_ = false;
    }

    // Plan 3.2: the sorted queue, then the post-1024 overflow queue at non-decreasing effective offsets.
    // Slice loop: the shape of V-proc:417-454 minus the pedal points and the kMaxBlockSamples cap:
    //   sliceEnd = havePending ? pendingOffset : total
    //   engine_->processBlock(outL + cursor, outR + cursor, sliceEnd - cursor);    // engine.h:422
    //   applyOutputGain(outL + cursor, outR + cursor, sliceEnd - cursor);
    //   ++lastSliceCount_;
    data.outputs[0].silenceFlags = 0;                              // FR-024.6
    return kResultOk;
}
```

- **No slice cap.** The engine accepts any length and splits on its own 32-sample grid (engine.h:420–440). Vorago's
  `kMaxBlockSamples` existed for its cavern. An event-free 4096 block therefore renders as exactly 1 slice; SC-009
  asserts `lastSliceCountForTest() == 1`.
- **No scratch buffer** (FR-029). The engine writes the host buffers and the gain multiplies them in place.
- **`applyOutputGain`:** for each sample `s`, `const float g = outputGain_.process(); outL[s] *= g; outR[s] *= g;`
  (smoother.h:197). Because `g ∈ [0, 1]` by construction (§3.1), `|out| <= ceilingLin_` still holds after the multiply
  (`true_peak_limiter.h:242–243, :257–258`).

#### 2.5.7 `pushParamsToEngine()` (FR-023.5, FR-024.4)

```cpp
const int mode = globalParams_.voiceMode.load(std::memory_order_relaxed);
engine_->setVoiceMode(mode == 1 ? Engine::VoiceMode::Poly : Engine::VoiceMode::Mono);   // P-10; engine.h:326
engine_->setPolyphony(static_cast<std::size_t>(
    clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed))));          // engine.h:330
engine_->setOutputCeilingDb(globalParams_.outputCeilingDb.load(std::memory_order_relaxed));   // engine.h:364
engine_->setMacros(macroValues(macroParams_));                                           // engine.h:371
```

- Every call is an O(1) pending store, with no allocation.
- A value equal to the applied one changes nothing at the next grid point. Settings compare at engine.h:678–736; the
  macro matrix stores plain values at `profundum_macro_matrix.h:166–181`.
- That is the P-8 property SC-008 depends on.

#### 2.5.8 `processParameterChanges()` (FR-042, FR-043)

This is V-proc:728–800 reduced to two bands, without the pedal branch. For each queue with `count > 0`:

1. `getPoint(count − 1, offset, value)`, so the **last** point wins.
2. `if (!detail::isFinite(value)) continue;` (`db_utils.h:125`).
3. `value = clamp(value, 0, 1)`.
4. Dispatch:
   - `id < kGlobalParamRangeEnd` → `handleGlobalParamChange`;
   - `id < kMacroParamRangeEnd` → `handleMacroParamChange`;
   - any other ID is ignored.

#### 2.5.9 `getLatencySamples()` (FR-032, Q5)

```cpp
return engine_ != nullptr ? static_cast<uint32>(Krate::DSP::ProfundumEngine::kLimiterLookaheadSamples) : 0u;  // engine.h:107
```

- The value is constant regardless of `prepared_`.
- `TruePeakLimiter::lookaheadSamples()` reads 0 until `prepare` (`true_peak_limiter.h:76, :131, :280`), which is why the
  wrapper never calls `engine_->latencySamples()` here. SC-014 asserts that the two agree after every prepare.
- An `AudioEffect` has no route to `IComponentHandler::restartComponent` (V-proc:566–569), and the processor sends no
  `IMessage` (§4.3, SC-014).

#### 2.5.10 `getState()` / `setState()` (FR-044, FR-045)

- **`getState`:** a null `state` returns `kResultFalse`. Otherwise `IBStreamer s(state, kLittleEndian);
  s.writeInt32(kCurrentStateVersion); saveGlobalParams(...); saveMacroParams(...);`.
- **`setState`:**
  1. A null `state` returns `kResultFalse`.
  2. `readInt32(version)`; a failed read returns `kResultFalse`.
  3. `version > kCurrentStateVersion` returns `kResultFalse` before any field is read.
  4. `loadGlobalParams(...) && loadMacroParams(...)`. The short circuit leaves every later field unchanged.
  5. Return `kResultOk`.

  `setState` makes **no** engine call. The next `process()` pushes the values, or `setupProcessing` step 5 does if no
  `process()` comes first. The push in `process()` is **unconditional** (FR-024.4). It never waits for
  `processParameterChanges` to see a change, because a preset that `setState` loads on a running processor arrives with
  no parameter point at all. SC-016 `StateWhileRunning` proves the running path, and SC-017 `PushInSetup` proves the
  setup path (§4.3).

### 2.6 `plugins/profundum/src/controller/controller.{h,cpp}` (FR-019, FR-046, FR-047, FR-050, FR-052, FR-055)

```cpp
namespace Profundum {
class Controller : public Steinberg::Vst::EditControllerEx1 {   // FR-019: no IMidiMapping, no INoteExpressionController
public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
                                                        Steinberg::Vst::ParamValue valueNormalized,
                                                        Steinberg::Vst::String128 string) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    [[nodiscard]] Krate::Plugins::PresetManager* presetManagerForTest() const noexcept { return presetManager_.get(); }
private:
    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;
};
}  // namespace Profundum
```

- **`initialize`:**
  1. `registerGlobalParams(parameters)`, then `registerMacroParams(parameters)`. The total is 8, asserted in SC-010.
  2. `presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(makeProfundumPresetConfig(), nullptr, this)`.
     The constructor is at `plugins/shared/src/preset/preset_manager.h:55–61`; the model is V-ctl:113.
  3. Install no state provider and no load provider. The Phase 12 browser wires those.
- **`terminate`:** `presetManager_ = nullptr`, then the base class.
- **`setComponentState`:** the stream handling of V-ctl:171–190, reduced to the v1 block.
  1. A null stream returns `kResultFalse`.
  2. Read the version; a version above `kCurrentStateVersion` returns `kResultFalse`.
  3. `loadGlobalParamsToController(s, set)`, then `loadMacroParamsToController(s, set)`, where
     `set = [this](ParamID id, double v) { setParamNormalized(id, v); }`.
- **`getParamStringByValue`:** `formatGlobalParam`, then `formatMacroParam`, then the base class (V-ctl:252–268).
- **`createView`:** `FIDStringsEqual(name, ViewType::kEditor)` returns `new VSTGUI::VST3Editor(this, "editor",
  "editor.uidesc")`; any other name returns `nullptr` (V-ctl:271–275).
- **Update config:** compiled through a `static_assert` touch point, as in V-ctl:61–65 (*"the ONLY thing that compiles
  it"*). No `UpdateChecker` is created (FR-052).

FR-055 asks that the controller survive `willClose()` on a tree that was never attached. That holds trivially here: the
controller is not a `VST3EditorDelegate` and holds no view pointers. The harness proves it (SC-013).

### 2.7 `src/preset/profundum_preset_config.h` (FR-050, FR-051)

```cpp
inline Krate::Plugins::PresetManagerConfig makeProfundumPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID      =*/kProcessorUID,
        /*.pluginName        =*/"Profundum",
        /*.pluginCategoryDesc=*/"Synth",
        /*.subcategoryNames  =*/{"Sub"}};   // Q4: permanent; Phase 13 may only add
}
```

The field order follows `plugins/shared/src/preset/preset_manager_config.h:18–23`.

### 2.8 `src/update/profundum_update_config.h` (FR-052)

This file copies `plugins/vorago/src/update/vorago_update_config.h` with the namespace and function renamed. It uses
`stringPluginName` and `VERSION_STR` from the generated `version.h`, and the endpoint
`"https://rolandzwaga.github.io/krate-audio/versions.json"`.

### 2.9 `src/entry.cpp` (FR-018)

This file is `plugins/vorago/src/entry.cpp:23–76` with `Vorago` replaced by `Profundum` and
`#define stringPluginName "Profundum"`. The `#include <ui/arc_knob.h>` line (`:28`) is **removed**, because FR-018
forbids `ui/*.h` includes.

### 2.10 `resources/editor.uidesc` (FR-054)

- Template `"editor"`: a `CViewContainer` of 420 × 360, with no bitmaps and no custom classes.
- A leading comment: *"Phase 10 PLACEHOLDER — Phase 12 replaces this file wholesale; stock views only"*.
- `<control-tags>`: `OutputGain`=0, `VoiceMode`=1, `Polyphony`=2, `OutputCeiling`=3, `MacroWeight`=100,
  `MacroDarkBright`=101, `MacroTightLoose`=102, `MacroCleanViolent`=103.
- Six `CSlider`s (gain, ceiling and the four macros) and two `COptionMenu`s (voice mode and polyphony), each beside an
  untagged `CTextLabel`.
- The attribute sets are those of the Vorago Phase 11 placeholder (`specs/vorago-phase11-plugin-scaffold/plan.md:932–941`).

The file is non-empty, so `tools/check-bundle.js` accepts it.

### 2.11 AU identity (FR-015 – FR-017)

- **`auv3/audiounitconfig.h.in`:** a full copy of `plugins/vorago/resources/auv3/audiounitconfig.h.in:1–40` with:
  - `Vorago` → `Profundum` at `:1`, `:20` and `:27`;
  - `'Vrgo'` / `Vrgo` → `'Prfd'` / `Prfd` at `:12–13`.

  Everything else is kept: `kSupportedNumChannels 02`, the flags, the delegate, `@AU_COMPONENT_VERSION@` and
  `kAUcomponentTag Synthesizer`.
- **`au-info.plist`:** Vorago's file with `:29` → `Profundum Mass Bass Instrument`, `:33` → `Krate Audio: Profundum`
  and `:35` → `Prfd`. It keeps one `AudioComponents` dict and one `SupportedNumChannels` dict of `0 / 2`.
- **`auv3/macOS/Profundum.entitlements`:** a byte copy of `Vorago.entitlements`.

### 2.12 Non-source files

| File | Content |
|---|---|
| `CMakeLists.txt` | §5.1 |
| `version.json` | `{"version": "0.1.0", "name": "Profundum", "description": "VST3 Mass Bass Instrument", "publisher": "Krate Audio", "url": "https://krateaudio.com/profundum/", "copyright": "Copyright (c) 2026 Krate Audio"}`. Six keys, no `preset_subdir` (FR-002; Vorago's `version.json` is the six-key model) |
| `CHANGELOG.md` | `# Changelog`, then `## [0.1.0]` with the scaffold bullets (FR-010) |
| `README.md` | One paragraph, plus the build, test and pluginval commands |
| `CLAUDE.md` | A leaf in the shape of `plugins/vorago/CLAUDE.md`: identity, roadmap pointer, *"No DSP lives in this plugin"*, skeleton, generated files, buses, ID table, `profundum_tests`, the pluginval path and the immutable FUIDs. It ends with a **"Decisions that outlive Phase 10"** section holding FR-009's nine items verbatim |
| `installers/windows/setup.iss` | Vorago's file with `Vorago` → `Profundum` and `AppId={{4DDA5FC2-4CD6-44E2-BF11-18887F0F6CD9}` (§2.1) |
| `installers/linux/README.txt` | Vorago's file, retitled |
| `docs/.gitkeep`, `src/ui/.gitkeep`, `resources/presets/Sub/.gitkeep` | FR-008, FR-051, FR-056 |

---

## 3. Pinned algorithms and control flow

### 3.1 Output gain: taper, inverse, smoother (FR-028, FR-043, FR-045, Q1)

```cpp
inline float outputGainFromNormalized(double v) noexcept {        // v already finite and in [0, 1]
    if (v <= 0.0) return 0.0f;                                     // -inf dB, exact 0
    if (v >= 1.0) return 1.0f;                                     // unity, exact (never 1 + ulp)
    return static_cast<float>(std::pow(10.0, (kOutputGainFloorDb + 60.0 * v) / 20.0));   // (-60, 0) dB
}
inline double outputGainToNormalized(float g) noexcept {           // g already finite and in [0, 1]
    if (!(g > 0.0f)) return 0.0;
    return std::clamp(1.0 + 20.0 * std::log10(static_cast<double>(g)) / 60.0, 0.0, 1.0);
}
```

Spot values:

| v | Gain | Level |
|---|---|---|
| 0.25 | 0.0056234 | −45 dB |
| 0.5 | 0.0316228 | −30 dB |
| 0.75 | 0.1778279 | −15 dB |
| 0.9 | 0.5011872 | −6 dB (SC-016) |

The value is computed in double and rounded once to float, so the relative error is about 6e-8, far inside SC-010's
1e-5. `std::pow` and `std::log10` do not allocate. The forward map runs on the audio thread, inside
`processParameterChanges`.

The smoother is `OnePoleSmoother` (`smoother.h:134`), with the recurrence of `process()` (`:197–208`):

- if `|y − t| < 1e-4` (`kCompletionThreshold`, `:55`), then `y := t`, so a target of exactly 1.0 or 0.0 is **reached
  exactly**;
- otherwise `y := t + a (y − t)`.

The pinned choices:

- **Coefficient:** `a = calculateOnePolCoefficient(20 ms, fs)`.
- **Cadence:** one `process()` call per output sample.
- **Target:** set once per `process()` call.
- **Snap:** `snapTo` on the first rendered block after `setupProcessing()` or `setActive(true)`.

Two consequences follow:

- (a) At unity gain the multiplier is exactly `1.0f`, so SC-008 is bit-exact by construction.
- (b) At `v = 0`, every sample after the ramp is exactly `0.0f` (SC-016).

### 3.2 Event ordering and slices (FR-025, FR-026)

- `clampOffset` is V-proc:48–54, verbatim.
- `buildEventOrder` is V-proc:1099–1124, verbatim: a stable insertion sort on `offset`. The strict `>` shift keeps equal
  offsets in list order.
- The overflow queue is V-proc:375–415, verbatim:
  - events `[1024, count)` are read in list order after the sorted queue;
  - each event's effective offset is `max(clampOffset(off), previous effective)`, seeded with the last sorted offset;
  - the result is non-decreasing, with nothing dropped and nothing allocated.
- The slice loop runs `while cursor < total`:
  1. Dispatch **every** pending event whose effective offset is `<= cursor`. This is a `while`, so equal offsets never
     produce a zero-length slice.
  2. Set `sliceEnd = havePending ? pendingOffset : total`.
  3. Render the slice, apply the gain and count the slice.

`dispatchEvent` (FR-026) follows V-proc:1129–1160 without the latch:

- **`kNoteOnEvent`:**
  - a pitch outside `[0, 127]` is dropped;
  - `velocity > 0` → `engine_->noteOn(pitch, quantiseVelocity(velocity))`;
  - otherwise → `engine_->noteOff(pitch)`.
- **`kNoteOffEvent`** with an in-range pitch → `noteOff`.
- **Any other event type** is ignored.

`quantiseVelocity(v) = uint8(clamp(v·127 + 0.5, 1, 127))` (V-proc:58–60). A velocity of 0.003 therefore maps to 1, and
is never the engine's velocity-0 note-off (engine.h:264–270).

### 3.3 Partition and grid interaction: why SC-009 is exact

The engine latches every event at its current sample and applies it at the first 32-sample grid point at or after it
(engine.h:17–20). Its output does not depend on how the host block is cut: Phase 9 asserts `diff == 0` for host blocks
{1, 7, 32, 64, 511, 4096} (`profundum_engine_test.cpp:938–969`).

The wrapper keeps that invariance for three reasons:

- (i) it calls `noteOn` / `noteOff` at the exact sample;
- (ii) its per-block setter pushes are change-only no-ops when no parameter changed (P-8);
- (iii) the gain multiply at unity is the identity.

### 3.4 State stream (FR-044, SC-011)

| Offset | Bytes | Field | Default | Load rule |
|---|---|---|---|---|
| 0 | 4 | `int32 kCurrentStateVersion` | 1 | `> 1` → reject before any read |
| 4 | 4 | `float outputGain` (linear) | 1.0 | finite, else keep; clamp [0, 1] |
| 8 | 4 | `int32 voiceMode` | 0 | {0, 1} stored, else keep |
| 12 | 4 | `int32 polyphony` | 4 | clamp [1, 8] |
| 16 | 4 | `float outputCeilingDb` | −1.0 | finite, else keep; clamp [−12, 0] |
| 20 | 16 | `float` × 4 macros, `ProfundumMacro` order | 0.5 each | finite, else keep; clamp [0, 1] |

- The total is **36 bytes**; the `static_assert` sits in `plugin_ids.h` (§2.1).
- A cut at offset 20, after the global block, leaves the macros unchanged (SC-011.4).
- Byte-comparing two serialized **streams** is allowed: they are not float renders (FR-065).

### 3.5 Seed and determinism

- The engine seed stays at `kDefaultSeed = 0xB455u` (engine.h:114), and the plugin never calls `setSeed`.
- Phase 11 owns the seed surface. The engine applies a seed only at prepare / reset (engine.h:373, :201–206).
- Two processors and an independently constructed engine therefore render bit-identically from the same script.
  SC-008 and SC-016.4 depend on this.

---

## 4. Test plan

### 4.1 File → case → coverage

All cases are tagged `[profundum]`. Plugin types are always written `::Profundum::…`, so `Krate::DSP::ProfundumTest`
can never shadow them (spec ODR row).

| File | TEST_CASE (tags) | SECTIONs | SC / FR |
|---|---|---|---|
| `unit/processor_bus_test.cpp` | `Profundum_ProcessorBusSetup` `[profundum][processor]` | `BusCounts`, `Arrangements` | SC-012; FR-020, FR-021 |
| `unit/param_denorm_test.cpp` | `Profundum_ParamDenormRoundTrip` `[profundum][params]` | `GridValues`, `LastPointWins`, `UnregisteredIds`, `NaNIgnored`, `RegisteredInfo`, `Display` | SC-010; FR-042, FR-043, FR-047 |
| `unit/state_roundtrip_test.cpp` | `Profundum_StateRoundTrip` `[profundum][state]` | `ByteIdentical36`, `DefaultDecode`, `FutureVersion`, `Truncated`, `Hostile`, `HostileGainKeepsCeiling`, `ControllerMirror`, `ControllerMirrorHostile` | SC-011; FR-044–FR-046 (`ControllerMirrorHostile` covers FR-045 sanitising on the controller side) |
| `unit/midi_event_test.cpp` | `Profundum_MidiEventTranslation` `[profundum][processor][midi]` | `PartitionInvariance`, `EventOrder`, `Overflow`, `OffsetClamp`, `Translation`, `SliceCount` | SC-009; FR-025 (`OffsetClamp` covers the offset clamp, `Overflow` the overflow order), FR-026 |
| `unit/lifecycle_test.cpp` | `Profundum_ProcessorLifecycle` `[profundum][processor]` | `IdleIsExactZero` (P-5 probe), `Latency`, `LatencyIsReal`, `NoLatencyMessage`, `Guards`, `SampleSize64`, `NotReady`, `Reset`, `GainSnap`, `PushInSetup`, `ActivateNoAlloc`, `ProcessNoAlloc`, `UnsupportedRate`, `NoMidiInterfaces` | SC-007, SC-014, SC-017; FR-019, FR-023 (including the P-12 step 2b), FR-028–FR-032 |
| `unit/controller/editor_lifecycle_test.cpp` | `Profundum_EditorLifecycle` `[profundum][controller][ui][lifecycle]` | `PresetConfigIsLive`, `HarnessCycles`, `EditorBindsSurface`, `CreateViewNames` | SC-013; FR-050–FR-055 |
| `integration/processor_audio_test.cpp` | `Profundum_ProcessorRendersHeldNote` `[profundum][processor][audio]` | `HeldNote`, `Ceiling`, `WrapperTransparency` | SC-005, SC-006, SC-008; FR-024, FR-028 |
| `integration/param_flow_test.cpp` | `Profundum_ParamFlowReachesEngine` `[profundum][params][audio]` | `ReadBack`, `Audibility`, `Tempo`, `MacrosLive`, `StateWhileRunning`, `GainRamp` | SC-016; FR-024 (`StateWhileRunning` covers the unconditional push of FR-024.4), FR-027, FR-028 (`GainRamp` covers per-sample smoothing), FR-041, FR-043 |
| `integration/processor_cpu_test.cpp` | `Profundum_ProcessorCpu` `[profundum][.perf][performance]` | none; three arms in one trial loop | SC-015; FR-067 |

### 4.2 Fixture: `plugins/profundum/tests/profundum_test_fixture.h`, namespace `ProfundumPluginTest`

The fixture adds only what the shared doubles lack (FR-063). The model is `plugins/vorago/tests/vorago_test_fixture.h`.

- **`MultiPointParamValueQueue` / `MultiParamChanges`:** copied from the shape of that fixture's `:60–175`. Both keep
  their capacity across `clear()`.
- **`CountingConnectionPoint final : Steinberg::Vst::IConnectionPoint`:**
  - `connect` and `disconnect` return `kResultTrue`;
  - `notify` increments `notifications`;
  - `addRef` and `release` return 1, and `queryInterface` returns `kNoInterface`.

  Used by SC-014.
- **`struct ProcessorFixture`:**
  - Owns `std::unique_ptr<::Profundum::Processor> proc` (FR-064). The constructor takes an optional
    `Steinberg::FUnknown* hostContext = nullptr`, calls `initialize(hostContext)` and sizes the buffers to 4096 plus
    guard words.
  - Only SC-014 `NoLatencyMessage` passes a context. It is a stack `Steinberg::Vst::HostApplication` that outlives the
    fixture (`extern/vst3sdk/public.sdk/source/vst/hosting/hostclasses.h:32`). `hostclasses.cpp` is already linked
    (§5.2), and Vorago does the same at `plugins/vorago/tests/integration/ecosystem_frame_test.cpp:554`.
  - `prepare(sr = 48000, maxBlock = 4096)`: `setupProcessing({kRealtime, kSample32, maxBlock, sr})`, then
    `setActive(true)`.
  - `processBlock(n, IEventList* = nullptr, IParameterChanges* = nullptr, ProcessContext* = nullptr)`: checks the guard
    words, then appends to `capturedL/R`. The capacity is reserved up front and never grown.
  - `processNoOutputs(pc)`.
  - `renderScript(span<const ScriptedEvent>, total, hostBlock, pcAtBlock)`: per-block event lists, rebuilt outside any
    timed or allocation scope.
  - `sizeBuffers`, `writeGuards` and `guardsIntact` (Vorago `:340–365`).
- **`struct DirectEngine`:** the reference arm of SC-008, SC-015 and SC-016.4.
  - `std::unique_ptr<Krate::DSP::ProfundumEngine> e`, prepared with `ProfundumEngineConfig{}`.
  - `OnePoleSmoother g{1.0f}`, configured with `kOutputGainSmoothMs`.
  - `render(script, total)` splits **only** at event samples. At the exact sample it calls `noteOn`, `noteOff` or
    `setVoiceMode` (equal samples in script order), then `processBlock`, then the gain loop. The caller owns the script
    span, so the 1100-event SC-009 `Overflow` reference needs no capacity limit.
- **Helpers:**
  - `isFiniteBits(float)`: an exponent test on `std::bit_cast<std::uint32_t>`, immune to fast-math.
  - `makeNaN()` and `makePosInf()`: built from bit patterns through a `volatile std::uint32_t` (FR-062).
  - `rmsDb(span, from, to)`, `peakOf`, `maxAbsDiff`, and `mismatches(a, b)` (the count of `a[i] != b[i]`).
  - Note sets:
    - `kE1 = 28`;
    - `kChord4 = {28, 33, 40, 48}` (E1 A1 E2 C3, the Phase 9 POLY4 set; `profundum_engine_perf_test.cpp:118`);
    - `kChord8 = {28, 33, 40, 45, 52, 57, 64, 69}` (SC-006).

### 4.3 Assertion strategy per criterion

**SC-005 (`HeldNote`).**

- Setup: 48 kHz, 512-sample blocks, registered defaults, NoteOn(28, 100/127) at sample 0, 2 s rendered.
- `rmsDb(L or R, 0.5 s, 2 s) >= -40.0` on each channel. This is Phase 9's bar (`profundum_engine_test.cpp:389`).
- Every sample passes `isFiniteBits`.
- Peak and RMS are `WARN`-printed.

**SC-006 (`Ceiling`).**

- Setup: POLY, polyphony 8, `kChord8` held, all macros at 1.0, gain 1.0, 4 s rendered.
- The bar is `ceil = std::pow(10.0f, -1.0f * 0.05f)`. That is the **same float expression** the limiter uses
  (`true_peak_limiter.h:136`), so the check is `<=` with no tolerance.
- (i) `peak <= ceil` on both channels.
- (ii) Change the ceiling parameter to normalized 0 at a block start. From that block plus 64 samples onward,
  `peak <= std::pow(10.0f, -12.0f * 0.05f)`.
- (iii) Non-vacuity: the peak in (i) is at least `0.5f * ceil`. If it is not, the script is made louder; the bar never
  moves.

**SC-007 (`ProcessNoAlloc`).**

1. Liveness probe: `{ AllocationScope s; auto* p = new int(7); n = AllocationDetector::instance().getAllocationCount();
   delete p; }`, then `REQUIRE(n >= 1)`.
2. Prepare the processor and render one warm-up block.
3. Build every container: 188 blocks of 512, carrying note-ons and note-offs, voice-mode, polyphony, ceiling, gain and
   macro changes, and a `ProcessContext` tempo of 90 then 140.
4. Inside the scope, call `proc->process(data)` directly, with no Catch macro, and read the count.
5. After the scope, `REQUIRE(count == 0)`.

**SC-008 (`WrapperTransparency`).** The 3 s script:

- MONO legato pair: NoteOn(28) @ 1000, NoteOn(33) @ 9 001 (no NoteOff in between), NoteOff(28) @ 20 003,
  NoteOff(33) @ 40 017.
- At block 100 (sample 51 200, a block start): a parameter point `kVoiceModeId = 1.0` at offset 0.
- POLY `kChord4` at samples 60 011, 60 013, 61 007 and 70 001, released at 120 019.
- None of the offsets is a multiple of 32.

The two arms:

- Arm (a): `ProcessorFixture` at block 512.
- Arm (b): `DirectEngine`, which calls `setVoiceMode(Poly)` immediately before rendering sample 51 200 and **no other
  setter**.

The assertion is `mismatches == 0` on both channels.

**SC-009.**

- **`PartitionInvariance`:** one 2 s script with notes at {311, 4 097, 20 001, 47 999, 70 003} and no parameter
  changes, rendered with host blocks {1, 7, 32, 65, 512, 4096}. Each render is compared with the 512 render:
  `mismatches == 0` per channel.
- **`EventOrder`:** NoteOn(40) @ 300 listed before NoteOff(40) @ 100 renders with `mismatches == 0` against the same
  events listed in offset order.
- **`Overflow`:** the spec's script, with all 1024 filler `kNoteOffEvent`s on pitches 100–127 at **offset 0**.
  - The last sorted offset is then 0. The first overflow event has offset 450, and each later one has a smaller raw
    offset (`450 − 5k`). The FR-025 rule therefore gives all 76 events the effective offset 450.
  - (a) `max |x| < 1e-6` over [B_end + 1 s, + 0.5 s).
  - (b) Control: without the 76 events, `RMS >= -60 dBFS` over the same window.
  - (c) **Ordering and offset rule.** Check (a) alone also passes an implementation that does any of the following:
    - sorts the overflow events;
    - dispatches them at their raw, decreasing offsets;
    - defers them to the next block.

    All three release the notes long before the window. Two more checks close that gap:
    - Right after block B, `lastSliceCountForTest() == 2`, for the slices `[0, 450)` and `[450, 512)`. Sorting or raw
      offsets would give 77 slices, and deferral would give 1.
    - Block B plus the next 100 ms (4 800 samples) is compared with a `DirectEngine` reference that replays the same
      script. The reference sends the 1024 filler note-offs at sample 0 of block B and the 76 overflow note-offs at
      sample 450, each group in list order. The threshold is `mismatches == 0` per channel. The fillers are replayed
      too: the engine's 256-entry queue sends excess note-offs to `pendingRelease_` (engine.h:283–288), and both arms
      must take that path identically.
  - `droppedEvents()` is `WARN`-printed.
- **`OffsetClamp`** (FR-025, offsets clamped to `[0, numSamples − 1]`): a prepared POLY fixture with polyphony 8. One
  512-sample block holds NoteOn(28) at offset −5 and NoteOn(40) at offset 612 (`numSamples + 100`). Then 0.5 s of
  event-free blocks follows.
  - `fixture.guardsIntact()` holds after every block, and every sample passes `isFiniteBits`. Without the clamp,
    `sliceEnd = 612` would write past the host buffer, and an offset of −5 would wrap the `size_t` length.
  - Right after the block, `lastSliceCountForTest() == 2`. Offset −5 clamps to 0 and is dispatched at cursor 0, so it
    adds no slice boundary. Offset 612 clamps to 511, so the slices are `[0, 511)` and `[511, 512)`.
  - The whole render has `mismatches == 0` per channel against the same script with the offsets written as 0 and 511.
- **`Translation`** (FR-026):
  - velocity 0.003 → the note sounds (RMS ≥ −40 dBFS over [0.5 s, 1 s));
  - a `kNoteOnEvent` with velocity 0 at 0.5 s releases a held note: the SC-009(a) window 1 s later reads `< 1e-6`;
  - pitches −1, 128 and 300, and a `kDataEvent`, each render with `mismatches == 0` against an event-free render.
- **`SliceCount`:** an event-free 4096 block gives `lastSliceCountForTest() == 1`. A block with 3 distinct offsets,
  none at 0, gives 4.

**SC-010.** On an initialised, **unprepared** fixture, use `processNoOutputs`. For each ID and each
`v ∈ {0, 0.25, 0.5, 0.75, 1}`, send a one-point `MultiParamChanges` and read the atomics.

| Field | Expected |
|---|---|
| gain | `== 0.0f` at 0; `== 1.0f` at 1; otherwise `abs(g − expected) <= 1e-5 · expected` with `expected = std::pow(10.0, (-60 + 60v)/20)`; every value in `[0, 1]` |
| voice mode | `{0, 0, 1, 1, 1}` |
| polyphony | `{1, 3, 5, 6, 8}` |
| ceiling | `abs(c − (-12 + 12v)) <= 1e-5` |
| macros | `abs(m − v) <= 1e-6` |

Then:

- `{0.1 @ 0, 0.9 @ 100}` stores 0.9.
- IDs 4, 99, 104 and 199 change none of the 8 atomics (snapshot before and after).
- `makeNaN()` changes no atomic.
- On the controller:
  - `getParameterCount() == 8`;
  - each `getParameterInfo` default matches the table `1.0, 0.0, 3/7, 11/12, 0.5 × 4`;
  - polyphony has `stepCount == 7` and its 8 strings read "1".."8";
  - `getParamStringByValue(kOutputGainId, 0)` gives "-inf dB", and `(…, 1.0)` gives "0.0 dB".

**SC-011.**

1. Set non-default values (gain 0.25, Poly, polyphony 6, ceiling −3, macros {0.1, 0.2, 0.8, 0.9}) and `getState` into a
   `MemoryStream`.
2. A fresh processor runs `setState` and then `getState`. The two buffers are `memcmp`-equal, and `size == 36`.
3. The default stream decodes to unity, Mono, 4, −1 and 0.5 × 4.
4. Version 2 → `kResultFalse`, and every atomic is unchanged.
5. A stream truncated at 20 bytes leaves the macros unchanged.
6. Hostile streams are loaded on a processor pre-set to known non-default values and follow the §3.4 rule column. That
   includes NaN and +Inf, built from bit patterns via `volatile`, in each of the 6 float fields in turn.
7. After the gain-4.0 stream (stored as 1.0), a prepared render of the SC-006 chord gives `peak <= 10^(ceilingDb/20)`,
   using the same float expression as the limiter.
8. The controller's `setComponentState` on stream (1) sets every parameter within 1e-6, and the gain inverse hits its
   points: 1.0 → 1.0, 0.0 → 0.0, 10^(−30/20) → 0.5 within 1e-5.
9. **`ControllerMirrorHostile`** (FR-045 on the controller side). Each hostile stream of step 6 runs on a fresh,
   initialised `Controller`:
   - First, `setComponentState` on stream (1) gives it a known non-default state, so every normalized value is known:
     gain 0.5, voice mode 1.0, polyphony 5/7, ceiling 0.75, macros {0.1, 0.2, 0.8, 0.9}.
   - Then `setComponentState` loads the hostile stream, and the test reads `getParamNormalized` for all eight IDs.

   The assertions:
   - a NaN or +Inf in any float field leaves that ID's normalized value unchanged (`==`, within 1e-12);
   - `voiceMode = 7` (and `−1`) leaves `kVoiceModeId` unchanged;
   - `polyphony = 99` gives exactly `1.0`, and `polyphony = 0` gives exactly `0.0`;
   - ceiling `+20` gives exactly `1.0`, and ceiling `−50` gives exactly `0.0`;
   - gain `4.0` gives exactly `1.0`, and gain `−1.0` gives exactly `0.0`;
   - a macro of `5.0` gives exactly `1.0`;
   - every normalized value read back lies in `[0, 1]`.

   A helper that forwards NaN, `(99 − 1) / 7` or `7.0` to `setParamNormalized` fails at least one of these.
   `Parameter::setNormalized` clamps by itself (`extern/vst3sdk/public.sdk/source/vst/vstparameters.cpp:62–71`), but a NaN
   passes that clamp. Only the "unchanged" rows prove that the helper skips the call
   instead of relying on the SDK clamp.

**SC-012.**

- `getBusCount(kAudio, kInput) == 0`, `getBusCount(kEvent, kInput) == 1` and `getBusCount(kAudio, kOutput) == 1`.
- `getBusInfo(kAudio, kOutput, 0)` has `channelCount == 2`.
- The four arrangement pairs return the values the spec lists.

**SC-013.**

- **`PresetConfigIsLive`:** Vorago's section (`editor_lifecycle_test.cpp:115–170`) with the category `Sub` and the temp
  directories `profundum_sc013_user_` / `profundum_sc013_factory_`. It also checks
  `fs::is_directory(PROFUNDUM_RESOURCES_DIR "/presets/Sub")` and that the directory name equals `subcategoryNames[0]`.
- **`HarnessCycles`:** `exerciseEditorLifecycle(*controller, "editor", path, 3)`
  (`tests/test_helpers/editor_lifecycle_harness.h:102–106`), then
  `REQUIRE(controller->presetManagerForTest() != nullptr)` before `terminate()`, as Vorago does
  (`plugins/vorago/tests/unit/controller/editor_lifecycle_test.cpp:180`). This is the FR-050 evidence that the
  **controller** creates a `PresetManager`. `PresetConfigIsLive` builds its own manager, so it cannot show that.
- **`EditorBindsSurface`:** attach a `VST3Editor` with `nativePlatformType()` and walk the frame.
  - The bound tag set (`getTag() >= 0`) is exactly {0, 1, 2, 3, 100, 101, 102, 103}, a count of 8.
  - Tags 1 and 2 are `COptionMenu`s.
- **`CreateViewNames`:** `createView("nonsense") == nullptr`, and `kEditor` returns a `VST3Editor`.
- The ASan run is a command (§5.5), not a test case.

**SC-014.**

- **`Latency`:**
  - after `initialize`, before any setup: `getLatencySamples() == 64 == ProfundumEngine::kLimiterLookaheadSamples`;
  - before `initialize` (a raw `new Processor`) and after `terminate`: `== 0`;
  - at each rate in {22 050, 44 100, 48 000, 88 200, 96 000, 192 000}: `== 64`, and equal to
    `engineForTest()->latencySamples()`;
  - still 64 after a second setup, after `setActive(false)` / `setActive(true)`, after every parameter change, and after
    a refused rate.
- **`NoLatencyMessage`:** the fixture is built with a stack `Steinberg::Vst::HostApplication` as the host context
  (§4.2). Without one, the check could not fail:
  - `ComponentBase::allocateMessage()` returns `nullptr` unless the context is an `IHostApplication`
    (`extern/vst3sdk/public.sdk/source/vst/vstcomponentbase.cpp:111–116`);
  - `sendMessageID` and `sendTextMessage` then return `kResultFalse` without reaching the peer (`:128–150`).

  The steps:
  1. `proc->connect(&peer)`, then the whole `Latency` sequence: initialise, every rate, re-setup, `setActive` toggles,
     parameter changes and a refused rate. Then `peer.notifications == 0`.
  2. **Positive control:** `proc->sendTextMessage("probe")` returns `kResultOk` and leaves `peer.notifications == 1`.
     The method is public (`vstcomponentbase.h:36, :57`). This proves the peer would have counted a latency message.

  `compliance.md` also cites the static check
  `grep -n "restartComponent\|allocateMessage\|sendMessage\|sendTextMessage" plugins/profundum/src/processor/processor.cpp`,
  which must return 0 lines.
- **`LatencyIsReal`:** fresh prepare at 48 kHz, NoteOn(28) @ 0, 512 samples. The first index where either channel has
  `x != 0.0f` lies in `[64, 68]`. It is `WARN`-printed.

**SC-015.** See §4.4.

**SC-016.**

- **`ReadBack`:** after one block, `voiceMode()`, `polyphony()`, `outputCeilingDb()` and `macros()` equal the
  denormalised atomics. The comparison is exact `==`, because both sides are the same float.
- **`Audibility`:** each spec row against the default render. Each render is 2 s at 48 kHz with E1 held, or `kChord4`
  for the POLY rows.

  | Row | Assertion |
  |---|---|
  | gain 0.9 | `rms(x) / rms(default)` over [0.05 s, 2 s) is in `[0.491, 0.511]` |
  | gain 0 | every sample after 20 ms is `== 0.0f` |
  | gain 1 | `mismatches == 0` |
  | voice mode | two-note overlap (28 @ 0 held, 40 @ 0.5 s): `maxAbsDiff > 1e-3` |
  | polyphony 1 vs 4 | `kChord4` in Poly: `abs(rmsDb1 − rmsDb4) > 1.0` |
  | ceiling −12 | `kChord8`: `peak <= pow(10.0f, -0.6f)`, and the default render's peak is above it |
  | each macro at 0 and at 1 | against 0.5: `rmsDb(x − default) > -60` |

  Every number is `WARN`-printed.
- **`Tempo`:** the steps of SC-016.3, with a stack `ProcessContext`:
  - `state = kTempoValid`, `tempo = 90`;
  - then `state = 0`, `tempo = 140`;
  - then `nullptr`.
- **`MacrosLive`:**
  - after a block, `macros()` equals the atomics;
  - one atomic changed through a parameter point changes `macros()` after the next block;
  - the all-0.5 processor gives `mismatches == 0` against `DirectEngine`.
- **`StateWhileRunning`** (FR-024.4, FR-045): a prepared fixture at 48 kHz that has rendered one 512-sample block with
  E1 held.
  1. `setState` with a stream that carries voice mode Poly, polyphony 2, ceiling −6 dBTP, gain 1.0 and macros
     {0.1, 0.2, 0.3, 0.4}.
  2. Render one block with **no** parameter changes and no events (`inputParameterChanges = nullptr`).
  3. Assert `voiceMode() == VoiceMode::Poly`, `polyphony() == 2` and `outputCeilingDb() == -6.0f`, and that `macros()`
     equals {0.1f, 0.2f, 0.3f, 0.4f} exactly.

  The read-backs return the engine's pending store (`dsp/include/krate/dsp/systems/profundum_engine.h:387–398`). A push
  that runs only after `processParameterChanges` saw a change leaves the defaults there and fails.
- **`GainRamp`** (FR-028, per-sample smoothing): a processor arm and a unity-gain `DirectEngine` arm, both at 48 kHz,
  with E1 held from sample 0. Each renders one 512-sample block at gain 1.0, and the two are bit-identical (SC-008).
  1. The next processor block carries a `kOutputGainId = 0.9` point at offset 0. The DirectEngine arm renders the same
     block at unity.
  2. After that block, `outputGainValueForTest()` lies strictly inside `(0.5011872, 1.0)`.
  3. Prediction: a local `OnePoleSmoother` starts at 1.0, runs `configure(kOutputGainSmoothMs, 48000.0f)` and
     `setTarget(0.5011872f)`, then 512 `process()` calls. With τ = 20 ms / 5 = 192 samples (`smoother.h:86–88`), the
     value is about 0.5012 + 0.4988·e^(−512/192) ≈ 0.536. The assertion is
     `abs(outputGainValueForTest() − predicted) <= 1e-5`. A smoother advanced once per slice or once per block lands
     far outside that, and so does a hard step.
  4. Over that block, at every index where `|direct| > 1e-4`, take the ratio `r[i] = out[i] / direct[i]`:
     - the ratio is non-increasing within a 1e-6 tolerance (`r[i] <= r[i−1] + 1e-6`);
     - `r[first] − r[last] > 0.1`.

     A block-step multiplier gives a constant ratio, so it fails the second clause.

  Every value is `WARN`-printed.

**SC-017.**

1. **`Guards`:** a prepared fixture with no note sounding. Each guard shape 1–6 carries NoteOn(28) @ 0 and returns
   `kResultOk`. The next valid 512-sample block with no events reads `== 0.0f` everywhere, and the engine read-backs and
   `droppedEvents()` are unchanged.
   - **`SampleSize64`** (P-12, FR-023 step 2b). It runs on two fixtures: one never prepared, and one prepared at 48 kHz.
     1. `setupProcessing` with `symbolicSampleSize = kSample64` at 48 kHz returns `kResultFalse`.
     2. A valid 32-bit block follows, pre-filled with the `0.123f` sentinel and carrying NoteOn(28) @ 0. It takes the
        not-ready path: every sample is `== 0.0f`, no sentinel bits remain and `silenceFlags == 3`. This shows that
        `prepared_` is false.
     3. Latency stays 64.
     4. A `kSample32` setup at 48 kHz then recovers: with E1 held, RMS ≥ −40 dBFS over [0.5 s, 1 s).
2. **`NotReady`:** pre-fill the buffers with `0.123f`. After the call, no sample's `std::bit_cast` equals
   `bits(0.123f)`, every value is `== 0.0f`, and `silenceFlags == 3`.
3. **`Reset`:** hold E1 for 0.5 s, then `setActive(false)`, `setActive(true)` and one 512-sample block with no events.
   Every sample reads `== 0.0f`, and `stateFinite()` holds.
4. **`GainSnap`:** the spec's two steps.
5. **`PushInSetup`:** `setState`, then `setupProcessing`, then the read-backs, with **no** `process()` call.
6. **`ActivateNoAlloc`:** `setActive(true)` inside an allocation scope.
7. **`UnsupportedRate`:**
   - 384 000, 11 025 and `makeNaN()` (as a double bit pattern) each return `kResultFalse`.
   - On both a never-prepared processor and one prepared at 48 kHz: the sentinel-overwrite and silence checks hold,
     `silenceFlags == 3`, latency stays 64, and `droppedEvents()` is unchanged.
   - Recovery at 48 000 gives `RMS ≥ −40 dBFS`.
   - 22 050 and 192 000 return `kResultOk`.
8. **`NoMidiInterfaces`:** `queryInterface(IMidiMapping::iid)` and `queryInterface(INoteExpressionController::iid)` both
   return `kNoInterface`.

**P-5 probe (`IdleIsExactZero`).** This section runs first in the TU.

- A prepared fixture renders 4096 event-free samples, all `== 0.0f`.
- Then E1 is held 0.5 s, released, and 1.5 s more is rendered. **D-1 (user ruling 2026-10-22):** the Sum tap is exactly
  zero by 0.125 s after the note-off, \|out\| ≤ 1e-6 from 0.5 s after it, and the output is exactly zero from 1.25 s after it
  (the 5 Hz DC blocker's decay of the last voice sample flushes to zero at 1.011 s; measured by the hidden
  `Profundum_IdleTailProbe`).
- If either check fails, the build stops and reports (§1.3, P-5). SC-009(a)'s weaker `< 1e-6` bar does not depend on
  this probe.

### 4.4 SC-015: `Profundum_ProcessorCpu`

Constants: `kFs = 48000`, `kBlock = 512`, `kWarmupBlocks = 200`, `kTrials = 16`, `kBlocksPerTrial = 100`, timed with
`std::chrono::steady_clock`. All arms run in one interleaved trial loop.

| Arm | Driver | Notes and settings |
|---|---|---|
| P-MONO / P-POLY | `ProcessorFixture`, bare `proc->process(data)` calls | The first warm-up block carries NoteOn(28), or `kChord4` plus a `kVoiceModeId = 1` point for POLY. Every later block has `inputEvents = nullptr` and `inputParameterChanges = nullptr` |
| D-MONO / D-POLY | `DirectEngine`: `processBlock(L, R, 512)` plus the gain loop | The same notes, and `setVoiceMode(Poly)` for POLY |
| P-MAX-MONO / P-MAX-POLY (P-1, **gated**, user ruling Q-A 2026-10-22) | as P-MONO / P-POLY | All four macro points at 1.0 in the warm-up block |

The figures:

- `best_X` is the minimum over trials of the ns per 100 blocks;
- `pct_X = best_X / (100 × 512 / 48 000 × 1e9) × 100`.

The gate (`REQUIRE`):

- `best_P_MONO <= 1.05 × best_D_MONO` and `best_P_POLY <= 1.05 × best_D_POLY`;
- `pct_P_MONO <= 8.0` and `pct_P_POLY <= 25.0`;
- `pct_P_MAX_MONO <= 8.0` and `pct_P_MAX_POLY <= 25.0` (Q-A, user ruling 2026-10-22).

`WARN` lines carry every percentage, both ratios, both P-MAX arms, and the Phase 9 reference figures 7.5798 and 24.005 labelled
*"showcase setting, not comparable (P-1)"*.

Run conditions:

- The TU is **not** in the `-fno-fast-math` list, for the Vorago reason (`plugins/vorago/tests/CMakeLists.txt:159–160`).
- The case runs only through `node tools/run-cpu-tests.js profundum_tests`, whose filter names `[.perf]`
  (`tools/run-cpu-tests.js:58`), alone on an idle machine.
- A red result is reported to the user. It is never re-run in a loop.

### 4.5 Portability rules for every test TU

- Never call `std::isnan`, `std::isinf` or `std::isfinite`. Use `isFiniteBits` or `detail::isFinite`.
- No brace-init narrowing. Fill `ProcessSetup` and `ProcessData` fields by explicit assignment, as the Vorago fixture
  does.
- Never name a `kPlatformType*` constant. Use `Krate::TestSupport::nativePlatformType()`
  (`lint-platform-type-literals.js`).
- Include `<allocation_operator_overrides.h>` in `test_main.cpp` only (FR-061; `lint-allocation-operator-overrides.js`).
- Never write `using namespace Krate::DSP::ProfundumTest`. Always spell the plugin namespace `::Profundum`.

---

## 5. Build integration

### 5.1 `plugins/profundum/CMakeLists.txt` (FR-001 – FR-006)

Start from `plugins/vorago/CMakeLists.txt` and make these changes:

- `krate_plugin_read_version(PROFUNDUM)` and `set(PLUGIN_NAME "${PROFUNDUM_NAME}")`.
- An enumerated source list:
  - `src/entry.cpp`, `src/plugin_ids.h`, `src/version.h`;
  - `src/processor/processor.{h,cpp}` and `src/controller/controller.{h,cpp}`;
  - `src/parameters/{global,macro}_params.h`;
  - `src/engine/profundum_engine_config.h`, `src/preset/profundum_preset_config.h`,
    `src/update/profundum_update_config.h`;
  - the SDK `memorystream.cpp`.
- `krate_plugin_platform_setup(... TAG PROFUNDUM BUNDLE_BASE com.krateaudio.profundum ENTITLEMENTS
  Profundum.entitlements KIND instrument)`.
- **No C4459 block**: Vorago's `:130–141` is removed (FR-006).

Everything else keeps Vorago's order: link and include, `smtg_target_configure_version_file`, resources,
install-to-system, install-presets, warnings, and `add_subdirectory(tests)`.

### 5.2 `plugins/profundum/tests/CMakeLists.txt` (FR-060 – FR-062)

```cmake
add_executable(profundum_tests
    unit/test_main.cpp
    unit/processor_bus_test.cpp
    unit/param_denorm_test.cpp
    unit/state_roundtrip_test.cpp
    unit/midi_event_test.cpp
    unit/lifecycle_test.cpp
    unit/controller/editor_lifecycle_test.cpp
    integration/processor_audio_test.cpp
    integration/param_flow_test.cpp
    integration/processor_cpu_test.cpp
    # SECOND compilation of every plugin .cpp (it must appear here AND in ../CMakeLists.txt)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    vstgui_test_stubs.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp
)
# `sdk` AFTER `vstgui_support` (plugins/vorago/tests/CMakeLists.txt:84)
target_link_libraries(profundum_tests PRIVATE
    KrateDSP KratePluginsShared Catch2::Catch2 test_helpers vstgui_support sdk)
target_include_directories(profundum_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests
    ${vst3sdk_SOURCE_DIR} ${vst3sdk_SOURCE_DIR}/vstgui4)
target_compile_features(profundum_tests PRIVATE cxx_std_20)
target_compile_definitions(profundum_tests PRIVATE
    PROFUNDUM_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/param_denorm_test.cpp         # normalized NaN
        unit/state_roundtrip_test.cpp      # NaN / +Inf per float field
        unit/lifecycle_test.cpp            # NaN sample rate; bit-pattern finiteness
        unit/midi_event_test.cpp           # bit-pattern finiteness over renders
        integration/processor_audio_test.cpp
        integration/param_flow_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only")
    # integration/processor_cpu_test.cpp is DELIBERATELY ABSENT: the timed code must
    # run in the shipping FP mode (plugins/vorago/tests/CMakeLists.txt:159-160).
endif()
catch_discover_tests(profundum_tests REPORTER console)
krate_test_pch(profundum_tests)   # P-9; after the per-source flag blocks (Vorago :171-172)
```

- `vstgui_test_stubs.cpp` is a copy of Vorago's 12-line file, which names no plugin.
- `test_main.cpp` is a copy of Vorago's `:1–29`.

### 5.3 Registration outside `plugins/` (FR-007, FR-070 – FR-080)

Each edit is one line or one block, modelled on the Vorago twin at the cited line. **All of them land in one task
group.** `lint-plugin-roster.js` builds its roster from `readdir(plugins/)` (`tools/lint-plugin-roster.js:23, :35`), so
the lint fails from the moment `plugins/profundum/` exists until every checked site names the plugin.

| Site | Edit (model line) |
|---|---|
| `CMakeLists.txt` | `add_subdirectory(plugins/profundum)` after `:501` |
| `.gitignore` | 3 leading-slash lines after `:81` |
| `ci.yml` | See the list below this table |
| `release.yml` | `- profundum` at `:42`; `'plugins/profundum/CMakeLists.txt'` in `:143` |
| `valgrind-nightly.yml` | `profundum_tests` in `:284` and `:291` |
| `tools/run-clang-tidy.ps1` | `ValidateSet` at `:66`; a `"profundum"` case covering src and tests (`:218–225`); `all` (`:240–251`) |
| `tools/run-clang-tidy.sh` | usage text at `:67`; a `profundum)` case (`:160–161`); `all)` (`:177–178`) |
| `tools/check-changelog-coverage.js` | `'profundum'` in `PLUGINS` at `:50` |
| `tools/gen-specs-index.js` | `['profundum', 'Profundum']` **before** the DSP keywords (after `:27`), with a comment noting that Profundum slugs contain `harmonic`, `stereo-field` and `voice-engine` |
| `tools/run-cpu-tests.js` | `'profundum_tests'` in `DEFAULT_TARGETS` (`:45–51`) |
| `tools/ci-test-shard.js` | `['profundum', 'Profundum']` in `:171–174` |
| `tools/run-close-lanes.js` | `'profundum_tests'` in the lane list at `:38` |
| root `CLAUDE.md` | every roster of FR-079 |

The `ci.yml` edits:

- detect-changes `:68`, filter `:100–101`, `for p in` `:124`, output echo `:142`, `for area in` `:116`;
- the three `hashFiles` keys at `:266`, `:533` and `:1007`;
- the nine `for plugin_info` blocks and their `case` arms; the macOS entry at `:575` becomes
  `profundum:Profundum:Profundum_AU:Profundum_AUV3 profundum_tests`;
- the Windows artifact upload `:495–500`;
- `auval -v aumu Prfd KrAt`, after `:797–805`;
- the AUv3 bundle verification `:864–870`;
- the macOS artifact upload `:954–962`;
- the Linux artifact upload `:1239–1244`.

### 5.4 Regenerated artifacts (FR-078)

- Run `node tools/gen-repo-map.js` and `node tools/gen-specs-index.js`, and commit both outputs with the change.
- `gen-symbols.js` scans `dsp/include` only (`tools/gen-symbols.js:19`), so its artifact does not change.

### 5.5 Targets to build and run (Windows, full CMake path)

The steps are ordered so that independent work overlaps. Only step 6 needs an idle machine.

1. **Configure once.** The root `CMakeLists.txt` changed, so `"$CMAKE" --preset windows-x64-release` re-runs.
2. **Build both targets in one invocation.**
   - `"$CMAKE" --build build/windows-x64-release --config Release --target Profundum profundum_tests -- /nr:false`.
     The `/nr:false` flag works around MSBuild node reuse.
   - Grep the log for `warning C[0-9]|warning:`; the expected count is 0 lines (SC-001).
3. **Run in parallel.** None of these is timing-sensitive:
   - `build/windows-x64-release/bin/Release/profundum_tests.exe 2>&1 | tee <log> | tail -5` (SC-002);
   - the `--list-tests` and `"[.perf]" --list-tests` listings (SC-002.1);
   - `tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Profundum.vst3`
     (SC-003);
   - `node tools/check-bundle.js build/windows-x64-release/VST3/Release/Profundum.vst3` (SC-020);
   - `git status --porcelain plugins/profundum` (SC-021);
   - the nine `LINTS` and the three `GENERATORS --check` of `tools/hooks/guard-ci-gates.js:31–48` (SC-018.1, SC-022);
   - the SC-018.2 grep table;
   - `node tools/check-portability.js` on the **14** new `.cpp` files of the FR-008 skeleton, each passed explicitly by
     path:
     - `plugins/profundum/src/entry.cpp`, `plugins/profundum/src/processor/processor.cpp` and
       `plugins/profundum/src/controller/controller.cpp`;
     - `plugins/profundum/tests/vstgui_test_stubs.cpp` and `plugins/profundum/tests/unit/test_main.cpp`;
     - `plugins/profundum/tests/unit/{processor_bus,param_denorm,state_roundtrip,midi_event,lifecycle}_test.cpp`;
     - `plugins/profundum/tests/unit/controller/editor_lifecycle_test.cpp`;
     - `plugins/profundum/tests/integration/{processor_audio,param_flow,processor_cpu}_test.cpp`.

     The expected line is `check-portability: all clear -- 14 compiled.`, with no skip suffix (SC-001). The count is
     cross-checked with `git status --porcelain --untracked-files=all plugins/profundum | grep -c '\.cpp$'`, which must
     print `14`. Then run `wsl --shutdown`, because WSLg leaves traces behind.
   - **FR inspection checklist.** These FRs have no SC of their own. Each output line goes into `compliance.md`.

     | FR | Command | Expected |
     |---|---|---|
     | FR-002 | `node -e "const j=require('./plugins/profundum/version.json');console.log(Object.keys(j).length, 'preset_subdir' in j)"` | `6 false` |
     | FR-006 | `grep -rnE "/wd[0-9]+|pragma warning *\( *disable|-Wno-" plugins/profundum` | 0 lines |
     | FR-008 | a `node -e` existence check over every path of the FR-008 tree except the three GENERATED files | every path present, 0 missing |
     | FR-009 | `grep -c` in `plugins/profundum/CLAUDE.md` for each decision anchor: `64 samples`, `after the limiter`, `Polyphony`, `Sub`, `IMidiMapping`, `Mass and Morph`, `ProfundumPluginTest`, `22050`, `120 BPM` | each ≥ 1 |
     | FR-014 | `grep -n 'kSubCategories = "Instrument|Synth"' plugins/profundum/src/plugin_ids.h` | 1 line |
     | FR-018 | `grep -c DEF_CLASS2 plugins/profundum/src/entry.cpp`; `grep -n '"ui/' plugins/profundum/src/entry.cpp` | `2`; 0 lines |
     | FR-050 | SC-013 `HarnessCycles` (`presetManagerForTest() != nullptr`) | pass |
     | FR-053 | `grep -c 'return Krate::DSP::ProfundumEngineConfig{}' plugins/profundum/src/engine/profundum_engine_config.h`, plus a read of the header | `1`; no other function defined |
     | FR-056 | `ls -A plugins/profundum/src/ui` | `.gitkeep` only |
   - **SC-018 extra greps.** `lint-plugin-roster.js` does not check these sites.

     | Site | Command | Expected |
     |---|---|---|
     | FR-079, the Monorepo Structure prose | `tr '\n' ' ' < CLAUDE.md \| grep -c 'Plugins today:.*profundum'` (the sentence spans lines 90–91) | ≥ 1 |
     | SC-018.4, the `all` block in the `.ps1` | `awk '/"all" *\{/,/^ *\}/' tools/run-clang-tidy.ps1 \| grep -c 'plugins/profundum/\(src\|tests\)'` | ≥ 2, for src and tests (the `all` block is `:240–251` today) |
     | SC-018.4, the `all` block in the `.sh` | `awk '/^ *all\)/,/;;/' tools/run-clang-tidy.sh \| grep -c 'plugins/profundum/\(src\|tests\)'` | ≥ 2 (the `all)` block is `:170–180` today) |
     | SC-018.3, the INDEX heading | `grep -n '^## Profundum' specs/INDEX.md`, then `awk '/^## Profundum/,/^## [^P]/' specs/INDEX.md \| grep -c profundum-phase10-plugin-scaffold` | 1 line; ≥ 1 |
4. **clang-tidy.** The test CMake changed, so reconfigure `build/windows-ninja` first; a stale compile database
   produces phantom warnings. Then run `./tools/run-clang-tidy.ps1 -Target profundum -BuildDir build/windows-ninja`
   (SC-018.4). This runs alongside step 3. The step 3 greps prove that both scripts' `all` branch covers Profundum.
   The plan does not run `-Target all`, because that would re-tidy every untouched plugin.
5. **ASan (SC-013.5).** Run `"$CMAKE" --build build-asan --config Debug --target profundum_tests`, which reconfigures on
   the root change by itself. Then run `build-asan/bin/Debug/profundum_tests.exe "[lifecycle]"`. This runs alongside
   steps 3–4.
6. **Last and alone:** `node tools/run-cpu-tests.js profundum_tests` (SC-015).

The close lane is `profundum_tests` plus the repo-wide lints. No `dsp/` header is modified, so no consumer suite is
re-run (FR-090; CLAUDE.md "Run only the tests the change set can reach").

SC-004 (`auval`) and the macOS and Linux legs of SC-001 are proven by CI only after the user pushes. `compliance.md`
marks them PENDING-CI, never PASS, until a run ID exists.

---

## 6. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | Idle output is not exactly zero (P-5): a DC-blocker tail, a denormal-flushed residue or an idle voice still rendering — **it was the DC-blocker tail (D-1, ruled 2026-10-22: the clause reads the blocker's settle)** | The P-5 probe runs first. On failure the build stops and reports, because the fix is in `dsp/` (FR-090). Only the exact-zero clauses depend on this; SC-009(a) uses `< 1e-6` |
| R-2 | Denormals in the release tail on a host thread without FTZ | `ScopedDenormalMode` is the first statement of `process()` (phase-handoffs §10.7). The test main enables FTZ (`plugins/vorago/tests/unit/test_main.cpp:27`), which would mask a missing guard, so the gate is the inspection grep cited in `compliance.md` (spec Edge cases) |
| R-3 | Brace narrowing (`VoiceMode`, `int32` → `size_t`) that MSVC accepts and Clang rejects | P-10, explicit `static_cast`s, and `check-portability.js` on all 14 new TUs with 0 skipped. §5.5 lists them by path and cross-checks the count against `git status` |
| R-4 | `std::isnan` under `-ffast-math` in tests | The FR-062 TU list (§5.2). Only bit-pattern helpers are used, and every non-finite value is injected through a `volatile` |
| R-5 | A roster site that no lint checks is missed | The SC-018.2 grep table, plus the extra sweep this plan ran (§1.3 P-6, P-7), which found `release-readiness.js` (§8 Q-B) |
| R-6 | An allocation in `process()` is hidden by an allocation inside the test harness | The P-11 discipline: containers are built outside the scope, and a liveness probe runs first |
| R-7 | The CPU gate flakes under load | The case is hidden (`[.perf]`) and runs alone through `run-cpu-tests.js`, which pins the cores and settles for 60 s (`:68`). A red result gets one re-run after the machine idles. If it is still red, it is reported, never relaxed |
| R-8 | `lint-plugin-roster.js` goes red in the middle of the build, because the plugin directory exists before the rosters | `tasks.md` puts the skeleton and every roster edit in the same group, and gates the lint only at the end of that group |
| R-9 | The ceiling bar is off by one float ulp | Compare against the same float expression the limiter uses (`true_peak_limiter.h:136`), not a decimal literal |
| R-10 | AU error `-10875` from a bus / plist mismatch | Exactly one `SupportedNumChannels 0/2` dict and `kSupportedNumChannels 02`, and no `addAudioInput` call (§2.5.2) |
| R-11 | `setActive(false)` before any `setupProcessing` reaches `reset()` on an unprepared engine | The P-3 guard |
| R-12 | A future `kMaxVoices` raise silently desynchronises the polyphony list | `static_assert(kNumPolyphonyEntries == ProfundumEngine::kMaxVoices)`: a raise breaks the build and forces the new-ID decision (FR-009 item 3) |

---

## 7. Implementation order (input to tasks.md)

1. **Skeleton and identity.**
   - Files: `version.json`, `CHANGELOG.md`, `README.md`, the `CLAUDE.md` leaf, the AU resources, the installers,
     `plugin_ids.h`, both packs, the engine / preset / update configs, `entry.cpp`, stub processor and controller
     bodies, the plugin and test `CMakeLists.txt` files, `test_main.cpp` and the stubs.
   - Every §5.3 roster edit, `.gitignore` and the regenerated artifacts land **in the same group**.
   - Verify: configure, then build `Profundum profundum_tests` with 0 warnings; `lint-plugin-roster.js` exits 0.
2. **P-5 probe** (`IdleIsExactZero`).
   - Verify: green, or stop and report.
3. **Per area, a failing test first and then the body.**
   - Areas, in order: bus (SC-012); params and denormalisation (SC-010); state (SC-011); `process()` and events (SC-009,
     SC-017.1–.3); lifecycle, latency and rate (SC-014, SC-017.4–.9); audio and transparency (SC-005, -006, -008); param
     flow (SC-016); allocation (SC-007); the editor and the placeholder `editor.uidesc` (SC-013).
   - Verify: each area's test is red before its body is written and green after.
4. **CPU test** (SC-015).
   - Verify: run last and alone.
5. **Close.**
   - Run pluginval, check-bundle, the clean-tree check, portability, clang-tidy, the ASan lifecycle run, the lints and
     the generators, then the scope fence (SC-019).
   - Run the §5.5 FR inspection checklist and the SC-018 extra greps.
   - Write `compliance.md`, citing the log lines.
   - Verify: SC-001 – SC-022, plus the inspection rows for FR-002, FR-006, FR-008, FR-009, FR-014, FR-018, FR-050,
     FR-053 and FR-056.

---

## 8. Open questions for the user

Each question concerns something the spec leaves unruled.

- **Q-A (P-1) — RULED 2026-10-22: gated.** MONO-MAX ≤ 8 % and POLY-MAX ≤ 25 % (spec SC-015 row, §5 table and gate
  above).
- **Q-B (P-6).** `.claude/workflows/release-readiness.js:13–22` lists plugins one by one, and the Phase 13 release gate
  reads it. Should Phase 10 add `profundum: { testTarget: 'profundum_tests', bundle: 'Profundum.vst3' }` and extend the
  SC-019 allow-list to cover that file, or should the entry wait for Phase 13? **RULED 2026-10-22: added in Phase 10**
  (spec FR-080 fourth list, SC-019 allow-list; tasks T024 / T028).

---

## Review notes

- **Spec-coverage review: all 10 issues applied, none rejected.** The changes:
  - SC-016 gains `StateWhileRunning` and `GainRamp`;
  - SC-009 gains `OffsetClamp` and the `Overflow` ordering checks (c);
  - SC-011 gains `ControllerMirrorHostile`;
  - SC-013 `HarnessCycles` now asserts `presetManagerForTest()`;
  - SC-014 `NoLatencyMessage` now runs with a `HostApplication` context and a positive control;
  - portability now covers 14 TUs;
  - §5.5 now holds the FR inspection checklist and the SC-018 extra greps;
  - P-12 is rewritten as a tightening of FR-023 step 2.
- **P-12: the fix goes in `setupProcessing`, not in a new `process()` guard.** FR-030 makes its guard order binding,
  so the plan does not insert a guard into that list. Instead, the `symbolicSampleSize` refusal moves into
  `setupProcessing`, ahead of `engine_->prepare`. The processor stays unprepared, so a later 64-bit block reaches only
  guard 7's in-bounds zero fill. No threshold is relaxed.
- **`ControllerMirrorHostile` adds "unchanged" rows to the clamp rows.** The clamp rows (`polyphony 99 → 1.0`,
  `ceiling +20 → 1.0`, `gain 4.0 → 1.0`) are written exactly as the issue proposed. The plan also adds "unchanged" rows
  for NaN, +Inf and `voiceMode 7`. `Parameter::setNormalized` clamps by itself, so the clamp rows alone could pass a
  helper that forwards an out-of-range value.
