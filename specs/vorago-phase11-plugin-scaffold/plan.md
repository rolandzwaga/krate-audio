# Implementation Plan: Vorago Phase 11 — Plugin Scaffold

**Spec:** `specs/vorago-phase11-plugin-scaffold/spec.md` (reviewed, clarified 2026-09-24)
**Roadmap:** `specs/Vorago-roadmap.md` → Part B (lines 519–522), Phase 11 (lines 524–542)
**Template:** `specs/seraphis-phase8-plugin-scaffold/plan.md` (the shipped Part B plan; section numbering mirrors it)
**Depends on:** Phase 10 (`VoragoEngine`, `VoragoVoice`, `VoragoMacroMatrix`) ✅, Phase 10a ✅, Phase 9 (`CavernVerb`) ✅
**Status:** PLAN — no implementation
**Date:** 2026-09-24

---

## 0. How to read this plan

Every signature quoted below was read **from the header this session**, on
`feat/vorago-phase1-events-modulation` at `ddd3c476`. `file:line` citations are the files as they stand
at that commit. Where this plan contradicts or tightens the spec, §1.3 says so and cites the line that
proves it. No threshold is relaxed anywhere.

Phase 11 writes **no DSP** and touches **no file under `dsp/`, `plugins/seraphis/` or
`plugins/shared/`** (spec Scope; SC-027). It is a VST3 wrapper around a finished chain. The hard parts
are: (a) reproducing the composed chain of `dsp/tests/unit/effects/vorago_composed_chain_test.cpp:243–245`
exactly, with a sample-accurate event slicer in front of it; (b) proving the two shipped global
parameters reach that chain, while the twelve macros provably do not; (c) adding the plugin to every
roster outside `plugins/` so CI can actually fail on it.

**Algorithm content is small and fully pinned in §3.** There is no stochastic or feedback model in this
phase. The pinned algorithms are the master-gain one-pole recurrence, the stable event sort and slice
partition, the velocity and polyphony quantisers, and the state byte layout.

---

## 1. Architecture

### 1.1 Object graph

```
Vorago::Processor  (Steinberg::Vst::AudioEffect; audio thread for process())
 ├─ std::unique_ptr<Krate::DSP::VoragoEngine>  engine_   heap; sizeof <= kEngineSizeBound = 808 576 B
 │                                                        (vorago_engine.h:231, static_assert :1609)
 ├─ std::unique_ptr<Krate::DSP::CavernVerb>    cavern_   heap; allocates at prepare (cavern_verb.h:358)
 ├─ Krate::DSP::VoragoMacroMatrix              macros_   by value, default-constructed = FR-066 identity
 │                                                        (vorago_macro_matrix.h:193-197), NEVER written in Phase 11
 ├─ Vorago::GlobalParams                       globalParams_  (2 atomics)
 ├─ Vorago::MacroParams                        macroParams_   (12 atomics, INERT)
 ├─ Krate::DSP::OnePoleSmoother                masterGain_{1.0f}   (smoother.h:148)
 ├─ std::array<EventSlot, kMaxEventsPerBlock>  eventOrder_   (8 B x 1024 = 8 KiB, §3.2)
 └─ plain counters: setPolyphonyCalls_, lastSliceCount_ (test seams, §2.5.1)

Vorago::Controller  (EditControllerEx1 + VSTGUI::VST3EditorDelegate; UI thread)
 └─ std::unique_ptr<Krate::Plugins::PresetManager> presetManager_   (preset_manager.h:55-61)
```

There is **no audio scratch**. The engine renders straight into the host channel buffers at the
slice offset, the cavern runs in place on them, the master gain multiplies in place, and the output
stage runs in place (FR-028: "the only audio scratch needed is whatever the master-gain stage
requires", which is none). This differs from the Seraphis template, which needed dry/wet scratch because
`AetherReverb` was driven out of place (Seraphis plan §2.5.1, four `std::vector<float>`).

No cross-include between `processor/` and `controller/`. Shared by both: `src/plugin_ids.h` and the
two header-only packs under `src/parameters/`.

### 1.2 Signal path inside `process()`, per slice

```
 process() top: ScopedDenormalMode ─► processParameterChanges (last point per queue, FR-043)
               ─► FR-030 shape guards ─► pushGlobalParams() (polyphony on change; gain target / snap; P-9)
               ─► 2. macros_.apply(*engine_)                       vorago_macro_matrix.h:954 (once per call, P-10)
                     applyCavernTargets(*cavern_, macros_.computeCavernTargets())   :1026 + FR-034a
               ─► build eventOrder_ (stable sort by clamped offset, §3.2)
 per slice [cursor, sliceEnd):
   1. dispatch every event with clampedOffset <= cursor            (FR-025, FR-031)
   3. engine_->processStereoBlock(outL+c, outR+c, n)                 vorago_engine.h:891
   4. cavern_->processStereoBlock(outL+c, outR+c, outL+c, outR+c, n) cavern_verb.h:568 (in place)
   5. x[s] *= masterGain_.process()  for s in [0,n), both channels   smoother.h:197
   6. engine_->processOutputStage(outL+c, outR+c, n)                 vorago_engine.h:1006 (limiter LAST)
 end: data.outputs[0].silenceFlags = 0
```

Steps 3, 4, 6 are the chain test's three calls in order (`vorago_composed_chain_test.cpp:243–245`;
also the engine's own banner at `vorago_engine.h:996–1000`: *"engine.processStereoBlock(l, r, n);
cavern.processStereoBlock(l, r, l, r, n); engine.processOutputStage(l, r, n);"*). Step 2 before step 3
is the chain test's `pushCavernTargets` before every block (`:236–241`) with the matrix apply added; it
runs once per `process()` rather than per slice (P-10), which is still once per host block.

### 1.3 Spec premise corrections and tightenings (none relaxes a threshold)

**P-1 — `createDropdownParameterWithDefault` does NOT set the registered default.**
FR-048 says polyphony is registered with `createDropdownParameterWithDefault(..., 3, {...})`, and SC-009
requires *"each registered parameter's `toPlain(getDefaultNormalizedValue())` matches the table"*. The
helper (`plugins/shared/src/ui/parameter_helpers.h:47–68`) calls `param->setNormalized(...)`, which moves
the **current** value only; `ParameterInfo::defaultNormalizedValue` stays 0, i.e. index 0, i.e. **one
voice**. Seraphis hit exactly this: `plugins/seraphis/src/parameters/global_params.h` registers through
a local `addDropdownParam` whose comment says *"the shared helper leaves info.defaultNormalizedValue
untouched, so a host 'reset to default' on this control used to yield ONE voice"*, and the fix is at
`plugins/seraphis/src/parameters/dropdown_mappings.h:383–397` (`param->getInfo().defaultNormalizedValue =
index / steps` before `addParameter`). Vorago cannot include a Seraphis header, and FR-056 / scope forbid
editing `plugins/shared/`. **Resolution:** `registerGlobalParams` pins the default inline, two lines,
after the helper call (§2.3). SC-009's default clause is what catches the omission.

**P-2 — SC-019 clause 1 cannot detect the missing first-block snap.**
SC-019.1 says the `< 1e-6` peak at master gain 0 is *"possible only because of the first-block snap"*.
It is not. The reported latency is 3072 samples (FR-033: smear 2048 + cavern 1024), so the first 64 ms
of output are silence regardless of gain, and a non-snapped one-pole starting at 1.0 falls below 1e-6
of its start in ≈ 3 × 20 ms (`ln 1e6 / ln 100 = 3` time constants of a 99 %-in-20 ms smoother, §3.1),
i.e. inside that silent window. The shipped attack is 20 s (`vorago_voice.h:322`), so the signal under
the tail of the ramp is tiny as well. **Resolution:** keep SC-019.1 as specified (it still proves gain 0
reaches the chain), add a **non-vacuity arm** (the same script at normalized 0.5 must peak `>= 1e-4`,
SC-005's floor), and observe the snap **directly** through a const seam
`float masterGainValueForTest() const noexcept` returning `masterGain_.getCurrentValue()`
(`smoother.h:191`): after the first 512-sample `process()` with target 0
it must read exactly `0.0f`. Without the snap, a smoother constructed at `1.0f` reads
`exp(-512 / 208.4) ≈ 0.086` there (§3.1's coefficient), so the arm discriminates. The smoother is
therefore constructed with `1.0f` on purpose (§2.5.1).

**P-3 — FR-025's "applied in `sampleOffset` order" requires a sort; the template loop does not sort.**
The Seraphis loop *assumed* a sorted list (Seraphis plan §3.2 property 2: *"a late event with an
earlier offset fires at the current cursor rather than rewinding it"*). SC-022 clause 8 feeds
`[NoteOn(55)@400, NoteOn(48)@100]` and requires it to render like the sorted list. With the template
loop, NoteOn(55)'s offset 400 ends the first slice search and NoteOn(48) would fire at 400, not 100.
**Resolution:** §3.2's allocation-free stable insertion sort of `(clampedOffset, listIndex)` pairs into
a fixed `std::array` sized `kMaxEventsPerBlock = 1024`, with a defined overflow fallback.

**P-4 — SC-012 clause 3's "exactly 1 preset" needs two distinct override directories.**
`PresetManager::scanPresets()` scans the user directory **and** the factory directory
(`plugins/shared/src/preset/preset_manager.cpp:37–51`). Pointing both overrides at the same temp tree
double-counts (the Seraphis test says so: *"both directory overrides point at the SAME directory, so
scanPresets() enumerates the tree twice"*, `plugins/seraphis/tests/unit/controller/editor_lifecycle_test.cpp`
Seraphis_PresetConfigIsLive), and leaving the user override empty makes the scan read the machine's real
user preset folder. **Resolution:** user override = an **empty** temp directory, factory override = a
second temp directory holding `Drones/Probe.vstpreset`. A zero-byte file suffices: the scan keys on
`extension() == ".vstpreset"` (`:63`), `readMetadata` is a stub returning true (`:512–515`), and
`PresetInfo::isValid()` needs only a name and a path (`preset_info.h:25–27`).

**P-5 — the spec's "silence() does not rewind the slots' trajectories" is unproven, and nothing may
depend on it.** `VoragoEngine::silence()` calls `voices_[v].silence(); voices_[v].reset();` for every
slot (`vorago_engine.h:447–456`), and the engine's own seed documentation names `reset()` as the call
that *"restores a slot's trajectory"* (`:526–543`). Whether a render after `setActive(false/true)`
matches a fresh instance is therefore not what the spec's edge case asserts. The plan keeps the spec's
operative rule — **no test may assume either way** — and flags the sentence for amendment (§8.2).

**P-6 — non-vacuity preconditions for the equality criteria.** SC-008 (1), SC-022 (5, 7, 8) and SC-023
compare two renders at `<= 1e-5`. With a 20 s attack and a 3072-sample latency, a short render can be
quiet enough that `1e-5` is a large fraction of the signal, or all of it. Every such comparison in §4.3
therefore first `REQUIRE`s the reference render's peak over the compared window `>= 1e-4` (the SC-005
floor). If a script does not reach it, **the script is lengthened** (longer render, velocity 127) —
never the threshold. SC-022 already states this precondition; SC-008 and SC-023 gain it.

**P-7 — SC-006 stays in the per-push lane.** It renders 2 × 30 s at polyphony 6 (~60 s of audio at up
to ~43 % of one core, roadmap Phase 10 note: polyphony 5–6 measured 37.99–43.41 %), so it will cost
more than the `[long]` threshold of ~15 s. It is a **bounded-output** criterion, and root `CLAUDE.md`
forbids tagging bounded / NaN-guard tests `[long]`. It stays untagged; its measured wall time is
recorded in `compliance.md`.

**P-8 — no "processor render == direct chain render" equality test.** It would be the obvious
strengthening of FR-024, and it is deliberately **not** added: the processor's engine code is inlined
into `processor.cpp`'s second compilation while a direct reference would be inlined into the test TU,
and under `/fp:fast` two TUs may contract the same inline code differently (memory note
*"Render goldens: harvest inside the consuming test binary"* measured 1e-4 same-commit drift). Every
equality criterion in §4.3 compares two renders **through the same `Processor::process()`**, or two
calls made from the **same** TU (SC-024).

**P-9 — FR-024 step 0 is hoisted to once per `process()`.** FR-024 lists "push the global parameters
(FR-024a)" as step 0 *of each slice* (spec.md:410–411). The plan calls `pushGlobalParams()` once per
`process()`, before the first slice (§2.5.7). This is observationally identical: the only writers of
`globalParams_` are `processParameterChanges` (run once, at the top of `process()`, taking each
queue's last point, FR-043) and `setState` (not concurrent with `process()`), so the atomics hold
one value for the whole call; a per-slice push would repeat the same edge-triggered `setPolyphony`
comparison and the same `setTarget` with an unchanged value on every later slice. The spec is amended
to match (§8.2 item 9) so the compliance table does not cite a spec/implementation disagreement.

**P-10 — FR-024 step 2 (macro + cavern-target push) is hoisted to once per `process()`.** FR-024
puts `macros_.apply(*engine_)` and `applyCavernTargets(*cavern_, macros_.computeCavernTargets())` in
every slice. Together they cost two `evaluateAll()` passes over the 46-row table
(`vorago_macro_matrix.h:257` `kNumRows = 46`; `apply` evaluates at `:958`, `computeCavernTargets`
at `:1026–1027`), 8 engine setters plus 24 voice setters per voice for `i < engine.getPolyphony()`
(`:961–1016`) and 7 cavern setters. A host block with ~1000 events at distinct offsets would repeat
all of it ~1000 times for no effect. The plan calls both once per `process()`, immediately after
`pushGlobalParams()` (§2.5.6). This is equivalent to the per-slice form in Phase 11 because every
input of both calls is fixed for the whole call: (a) `macros_` is never written after construction
(§2.4, INERT); (b) the set of slots `apply()` writes is `i < getPolyphony()`, and polyphony changes
only in `pushGlobalParams()`, which now runs before it; (c) the values it writes are plain scalar
stores or `setTarget()`s, never a snap (`vorago_macro_matrix.h:45–56`), and they survive a
mid-block steal or retrigger: `clearRunState()` re-installs the identity lanes *from the stored
bases* (`vorago_voice.h:1473–1482`; `installIdentityNeutral` at `:1873–1880` applies `kNoLanes`
over the bases plus `gravityBase_`), and `VoragoVoice::noteOn` (`:900–919`) writes only frequency,
velocity and gates, none of which is an `apply()` destination. **Phase 12 obligation**, recorded in
the leaf `CLAUDE.md` and §8.3: when the macros become live they are pushed **once per `process()`
(per host block), never per slice** — a macro change is block-granular like every other parameter
(SC-008 (2)). The spec is amended (§8.2 item 10).

---

## 2. Component-by-component design

Layer: every file is **plugin layer**. KrateDSP types consumed, with their layer: `VoragoEngine`,
`VoragoMacroMatrix` (L3, `systems/`), `CavernVerb` (L4, `effects/`), `OnePoleSmoother` (L1,
`primitives/smoother.h`), `ScopedDenormalMode` and `detail::isFinite` (L0, `core/`). The plugin may
include any layer.

### 2.1 `plugins/vorago/src/plugin_ids.h` (FR-011 – FR-014)

```cpp
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Vorago {

/// FR-012. Shared by processor and controller; neither includes the other.
constexpr Steinberg::int32 kCurrentStateVersion = 1;

/// FR-011. Freshly generated v4 GUIDs. NEVER reused, NEVER changed after release.
static const Steinberg::FUID kProcessorUID(0x........, 0x........, 0x........, 0x........);
static const Steinberg::FUID kControllerUID(0x........, 0x........, 0x........, 0x........);

/// FR-014. `static const char* const`, not constexpr, with the GCC-13
/// -Wunused-variable rationale copied from plugins/seraphis/src/plugin_ids.h:43-49.
static const char* const kSubCategories = "Instrument|Synth";

/// FR-013. Reserved map (roadmap line 533; bands in roadmap line 549's pack order):
///   0-99      Global     (Phase 11 - SHIPPED: master gain, polyphony)
///   100-199   Macros     (Phase 11 - SHIPPED, INERT; wired in Phase 12)
///   200-299 Cloud · 300-399 Noise · 400-499 Resonance · 500-599 Ecology · 600-699 Sub
///   700-799 Smear · 800-899 Events · 900-999 Ecosystem · 1000-1099 Body · 1100-1199 Space
///   1200+     UNASSIGNED - Phase 12 claims a whole band for any unnamed section.
/// REGISTERED TYPES ARE FROZEN (roadmap line 559): kMasterGainId + 12 macros are plain
/// Steinberg::Vst::Parameter; kPolyphonyId is a StringListParameter.
enum ParameterIDs : Steinberg::Vst::ParamID {
    kMasterGainId = 0,
    kPolyphonyId = 1,

    kMacroDarknessId = 100,  // id - 100 == static_cast<int>(VoragoMacro::X)
    kMacroAgeId = 101,       // (vorago_macro_matrix.h:95-109)
    kMacroDensityId = 102,
    kMacroMovementId = 103,
    kMacroGravityId = 104,
    kMacroEntropyId = 105,
    kMacroPressureId = 106,
    kMacroWeightId = 107,
    kMacroFogId = 108,
    kMacroLifeId = 109,
    kMacroDepthId = 110,
    kMacroMassId = 111,
};

/// FR-043 range dispatch.
constexpr Steinberg::Vst::ParamID kGlobalParamRangeEnd = 100;  // id <  100 -> global pack
constexpr Steinberg::Vst::ParamID kMacroParamRangeEnd = 200;   // id <  200 -> macro pack

}  // namespace Vorago
```

The `static_assert(kMacroMassId - kMacroDarknessId + 1 == Krate::DSP::VoragoMacroMatrix::kNumMacros)`
that ties the band to the matrix lives in `macro_params.h` (which already includes the matrix header),
not here, so `plugin_ids.h` stays SDK-only.

**FUID procedure.** `node -e "console.log(require('crypto').randomUUID())"` twice; split each into four
`0x`-prefixed 32-bit groups; verify non-collision with
`grep -rn "FUID k\(Processor\|Controller\)UID" plugins/*/src/plugin_ids.h` (the fourteen existing values
the spec lists). Record both in `plugins/vorago/CLAUDE.md` as immutable. The same procedure generates the
fresh `AppId` GUID for `installers/windows/setup.iss` (§2.12).

### 2.2 `plugins/vorago/src/engine/vorago_engine_config.h` (FR-053, FR-034a, FR-024a)

Thin, free functions only, **no new type**. Every value it returns is a DSP-owned struct.

```cpp
#pragma once
#include <krate/dsp/effects/cavern_verb.h>
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>
#include <cstddef>
#include <cstdint>

namespace Vorago {

/// One constant for the engine config, the cavern config, the slice bound (FR-026)
/// and any scratch (FR-028): == 2048 (vorago_engine.h:182).
inline constexpr std::size_t kMaxBlockSamples = Krate::DSP::VoragoEngine::kMaxBlockSamples;

/// Seeds (spec Conventions; Clarification SEED). Stated EXPLICITLY rather than
/// inherited from VoragoEngine::seed_ = 1u (vorago_engine.h:1605) and
/// PrepareConfig::seed = 1 (cavern_verb.h:321), so a dsp/ default change cannot
/// silently move Vorago's sound. Not parameters in Phase 11.
inline constexpr std::uint32_t kEngineSeed = 1u;
inline constexpr std::uint32_t kCavernSeed = 1u;

/// FR-024a.2. Same 20 ms family as the Seraphis template (seraphis_engine_config.h:38).
inline constexpr float kMasterGainSmoothMs = 20.0f;

/// FR-053. The shipped VoragoEngineConfig (vorago_engine.h:105-156) with ONLY
/// maxBlockSamples set: smearEnabled = true, smearFftSize = 2048 (-> 2048 latency),
/// atmosCaptureSeconds = 20, both Phase 10a ghost fields inert.
[[nodiscard]] inline Krate::DSP::VoragoEngineConfig
makeVoragoEngineConfig(std::size_t maxBlockSamples) noexcept {
    Krate::DSP::VoragoEngineConfig cfg{};
    cfg.maxBlockSamples = maxBlockSamples;  // engine clamps to [1, 2048] (:284-285)
    return cfg;
}

/// FR-053. CavernVerb::PrepareConfig's shipped defaults (cavern_verb.h:300-322) with
/// maxBlockSamples and seed set. spectralDiffusionEnabled MUST stay true and
/// diffusionFftSize MUST stay 1024 (-> 1024 latency, FR-033). These are the values the
/// Phase 10 composed chain and its SC-001b Cavern term were measured with
/// (vorago_composed_chain_test.cpp:303-313).
[[nodiscard]] inline Krate::DSP::CavernVerb::PrepareConfig
makeVoragoCavernConfig(std::size_t maxBlockSamples) noexcept {
    Krate::DSP::CavernVerb::PrepareConfig cfg{};
    cfg.maxBlockSamples = maxBlockSamples;  // cavern clamps to [64, 8192] (:381)
    cfg.seed = kCavernSeed;
    return cfg;
}

/// FR-034a. vorago_composed_chain_test.cpp:188-196 (pushCavernTargets, forceDry=false),
/// same seven setters in the same order.
inline void applyCavernTargets(Krate::DSP::CavernVerb& cavern,
                               const Krate::DSP::VoragoCavernTargets& t) noexcept {
    cavern.setSize(t.size);                  // cavern_verb.h:613
    cavern.setDarkness(t.darkness);          // :619
    cavern.setDecaySeconds(t.decaySeconds);  // :626
    cavern.setFog(t.fog);                    // :652
    cavern.setDamperDepth(t.damperDepth);    // :717
    cavern.setMix(t.mix);                    // :748
    cavern.setWidth(t.width);                // :741
}

}  // namespace Vorago
```

**Why per-slice pushes are identity writes.** Every `CavernVerb` setter is *"clamp(isFinite(x) ? x :
default, lo, hi), store the shadow copy, then forward or target a smoother"*, and smoothers snap while
`prepared_ && !anySamplesProcessed_` (`cavern_verb.h:600–611`); the chain test relies on the same
idempotence (`vorago_composed_chain_test.cpp:227–231`). The matrix's forwarders are *"A PLAIN SCALAR STORE
OR A setTarget() on a ramp the owning component already runs – NEVER a snapTo(), NEVER a smoother reset,
NEVER a re-arm of a ramp that is already at its destination"* (`vorago_macro_matrix.h:45–56`), pinned by
`VoragoMacro_ApplyIsIdempotent` (`dsp/tests/unit/systems/vorago_macro_test.cpp:1604`); the one forwarder
that re-arms a ramp early-outs on an unchanged value (`setSubToneLevelOffsetDb`,
`vorago_engine.h:764–780`). So calling both every slice is partition-invariant, which SC-008 measures.

**RT safety.** `applyCavernTargets` is `noexcept` and allocation-free. `make*Config` are prepare-time.

### 2.3 `plugins/vorago/src/parameters/global_params.h` (FR-040, FR-043, FR-044, FR-048)

The six-function pack contract of `plugins/seraphis/src/parameters/global_params.h:40, 85, 124, 169,
211, 218, 240–241`, reduced to two fields. Includes `"ui/parameter_helpers.h"` (shared), the SDK
`vstparameters.h`, `base/source/fstreamer.h`, `pluginterfaces/base/ustring.h`, and
`<krate/dsp/systems/vorago_engine.h>` (for `kMaxVoices`) plus `<krate/dsp/core/db_utils.h>`
(`detail::isFinite`, `db_utils.h:118`).

```cpp
namespace Vorago {

struct GlobalParams {
    std::atomic<float> masterGain{1.0f};  ///< linear [0, 2]; normalized 0.5 == unity
    std::atomic<int> polyphony{4};        ///< [1, 6]; == VoragoEngine::kDefaultPolyphony (:178)
};

/// The ONE conversion into the engine's polyphony domain (Seraphis global_params.h
/// clampPolyphony rationale): keeps the stored value and engine_->getPolyphony()
/// (clamped, vorago_engine.h:507-508, :523) in the same domain, so the change
/// detector in §2.5.7 converges after one push even on a corrupt stream.
[[nodiscard]] inline std::size_t clampPolyphony(int raw) noexcept {
    return std::clamp(static_cast<std::size_t>(std::max(raw, 1)), std::size_t{1},
                      Krate::DSP::VoragoEngine::kMaxVoices);  // == 6
}

inline void handleGlobalParamChange(GlobalParams&, Steinberg::Vst::ParamID,
                                    Steinberg::Vst::ParamValue) noexcept;
inline void registerGlobalParams(Steinberg::Vst::ParameterContainer&);
inline Steinberg::tresult formatGlobalParam(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue,
                                            Steinberg::Vst::String128);
inline void saveGlobalParams(const GlobalParams&, Steinberg::IBStreamer&);
inline bool loadGlobalParams(GlobalParams&, Steinberg::IBStreamer&);
template <typename SetParamFunc>
inline void loadGlobalParamsToController(Steinberg::IBStreamer&, SetParamFunc);

}  // namespace Vorago
```

**Denormalisation (FR-044), exact expressions:**

| ID | `handleGlobalParamChange` stores | Inverse (`…ToController`) |
|---|---|---|
| `kMasterGainId` | `std::clamp(static_cast<float>(value * 2.0), 0.0f, 2.0f)` | `double(gain) / 2.0` |
| `kPolyphonyId` | `std::clamp(static_cast<int>(value * 5.0 + 1.0 + 0.5), 1, 6)` | `(double(clampPolyphony(n)) - 1.0) / 5.0` |

Polyphony at `{0, 0.25, 0.5, 0.75, 1}` → `int(1.5)=1, int(2.75)=2, int(4.0)=4, int(5.25)=5, int(6.5)=6`,
exactly SC-009's `{1, 2, 4, 5, 6}`. The registered default index 3 is normalized `3/5 = 0.6` →
`int(4.5) = 4`.

**Registration (FR-048 + P-1):**

```cpp
parameters.addParameter(STR16("Master Gain"), STR16("dB"), 0, 0.5,
                        ParameterInfo::kCanAutomate, kMasterGainId);   // plain Vst::Parameter

auto* poly = Krate::Plugins::createDropdownParameterWithDefault(
    STR16("Polyphony"), kPolyphonyId, /*defaultIndex=*/3,
    {STR16("1"), STR16("2"), STR16("3"), STR16("4"), STR16("5"), STR16("6")});
// P-1: the helper sets only the CURRENT value (parameter_helpers.h:64-66); the REGISTERED
// default must be pinned here or a host "reset to default" yields one voice.
poly->getInfo().defaultNormalizedValue = 3.0 / 5.0;
parameters.addParameter(poly);
```

**Load path clamps and rejects non-finite** (a corrupt stream must neither poison the gain multiply nor
make the polyphony detector fire every block):

```cpp
inline bool loadGlobalParams(GlobalParams& p, Steinberg::IBStreamer& s) {
    float g = 1.0f;
    Steinberg::int32 n = 0;
    if (!s.readFloat(g)) { return false; }
    if (Krate::DSP::detail::isFinite(g)) {                     // fast-math-immune, db_utils.h:118
        p.masterGain.store(std::clamp(g, 0.0f, 2.0f), std::memory_order_relaxed);
    }
    if (!s.readInt32(n)) { return false; }
    p.polyphony.store(static_cast<int>(clampPolyphony(n)), std::memory_order_relaxed);
    return true;
}
```

A failed read returns `false` and leaves every later field at its **current** value (FR-046). Byte
round-trip (SC-010.1) is unaffected because a valid stream is already in range.

`formatGlobalParam`: master gain as `"%.1f dB"` of `20·log10(value·2)` with `-80 dB` below `1e-4`
(Seraphis `formatGlobalParam` shape); `kPolyphonyId` returns `kResultFalse` so the
`StringListParameter` formats itself.

**Stream:** `writeFloat(masterGain)`, `writeInt32(polyphony)` — 8 bytes (§3.4).

### 2.4 `plugins/vorago/src/parameters/macro_params.h` (FR-042)

```cpp
namespace Vorago {

/// FR-042. Explicit initializers are LOAD-BEARING: value-initialisation would put
/// gravity at 0.0f while the controller registers 0.5 and VoragoMacroValues::gravity
/// is 0.5f (vorago_macro_matrix.h:198-211). Caught by SC-010.2.
struct MacroParams {
    std::atomic<float> darkness{0.0f};
    std::atomic<float> age{0.0f};
    std::atomic<float> density{0.0f};
    std::atomic<float> movement{0.0f};
    std::atomic<float> gravity{0.5f};  ///< bipolar around 0.5
    std::atomic<float> entropy{0.0f};
    std::atomic<float> pressure{0.0f};
    std::atomic<float> weight{0.0f};
    std::atomic<float> fog{0.0f};
    std::atomic<float> life{0.0f};
    std::atomic<float> depth{0.0f};
    std::atomic<float> mass{0.0f};
};

static_assert(kMacroMassId - kMacroDarknessId + 1 == Krate::DSP::VoragoMacroMatrix::kNumMacros,
              "the macro band must hold exactly VoragoMacro's twelve (vorago_macro_matrix.h:251)");

}  // namespace Vorago
```

Implementation detail that keeps the six functions short and order-safe: one internal
`inline std::atomic<float>& macroField(MacroParams&, int index)` switch over the twelve fields in
`VoragoMacro` order (plus a `const` overload), used by the handler (`index = id - kMacroDarknessId`),
save, load and controller-load loops. **`macroField` is only ever called with `index` in
`[0, kNumMacros)`**: its other callers are fixed `for (int i = 0; i < 12; ++i)` loops, and
`handleMacroParamChange` **returns early, touching nothing, when `id > kMacroMassId`** — an
unregistered ID in the reserved band 112–199, which FR-043's `id < kMacroParamRangeEnd` routing still
sends to it — before computing the index. The switch's `default` arm is therefore unreachable; it is
written as `assert(false)` followed by returning the last field, so no path is undefined. Handler stores `std::clamp(float(value), 0.0f, 1.0f)`. Loader
stores a read float only if `detail::isFinite` and clamps to `[0, 1]`. Registration: twelve
`parameters.addParameter(title, STR16("%"), 0, default, kCanAutomate, id)` (plain `Vst::Parameter`,
Seraphis `macro_params.h` shape), titles `"Darkness" … "Mass"`, defaults `0.0` except Gravity `0.5`.
`formatMacroParam`: `"%.0f%%"` for ids in `[kMacroDarknessId, kMacroMassId]`.

**INERT.** No Phase 11 code reads `MacroParams` into `VoragoMacroMatrix::setMacro/setMacros`
(`vorago_macro_matrix.h:832, :915`). `macros_` is never written after construction. Verified as a
negative control by SC-023; Phase 12 inverts it. Stream: twelve `writeFloat` — 48 bytes.

### 2.5 `plugins/vorago/src/processor/processor.{h,cpp}`

Two TUs day one (FR-008; split only past ~1500 lines).

#### 2.5.1 Header

```cpp
#pragma once
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "parameters/global_params.h"
#include "parameters/macro_params.h"

#include <krate/dsp/effects/cavern_verb.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Vorago {

class Processor : public Steinberg::Vst::AudioEffect {
public:
    /// §3.2. 1024 x 8 B = 8 KiB; keeps sizeof(Processor) far below FR-064's 64 KiB.
    static constexpr std::size_t kMaxEventsPerBlock = 1024;

    Processor();
    ~Processor() override;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    // getTailSamples(): NOT overridden -> SDK default kNoTail (Clarification Q6).

    // ---- test-access seams: const, allocation-free, never called by process() ----
    [[nodiscard]] const Krate::DSP::VoragoEngine* engineForTest() const noexcept { return engine_.get(); }   // FR-026a
    [[nodiscard]] const Krate::DSP::CavernVerb* cavernForTest() const noexcept { return cavern_.get(); }     // SC-013
    [[nodiscard]] std::uint32_t setPolyphonyCallCountForTest() const noexcept { return setPolyphonyCalls_; } // FR-024a.1
    [[nodiscard]] std::uint32_t lastSliceCountForTest() const noexcept { return lastSliceCount_; }           // FR-026
    [[nodiscard]] float masterGainValueForTest() const noexcept { return masterGain_.getCurrentValue(); }    // P-2
    [[nodiscard]] const GlobalParams& globalParamsForTest() const noexcept { return globalParams_; }         // SC-009
    [[nodiscard]] const MacroParams& macroParamsForTest() const noexcept { return macroParams_; }            // SC-009

private:
    struct EventSlot {                 // §3.2
        std::int32_t offset;           // clamped to [0, numSamples - 1]
        std::int32_t listIndex;        // index into data.inputEvents
    };

    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
    void pushGlobalParams() noexcept;
    std::size_t buildEventOrder(Steinberg::Vst::IEventList* events, std::size_t total) noexcept;
    void dispatchEvent(const Steinberg::Vst::Event& e) noexcept;
    void renderSlice(float* outL, float* outR, std::size_t n) noexcept;

    std::unique_ptr<Krate::DSP::VoragoEngine> engine_;  // FR-022: NEVER by value / stack
    std::unique_ptr<Krate::DSP::CavernVerb> cavern_;    // FR-022
    Krate::DSP::VoragoMacroMatrix macros_{};            // FR-022: by value; never written (FR-042)

    GlobalParams globalParams_{};
    MacroParams macroParams_{};                         // INERT

    Krate::DSP::OnePoleSmoother masterGain_{1.0f};      // P-2: constructed at 1.0f on purpose
    bool snapGainPending_ = true;                       // FR-024a.2 first-block snap
    bool prepared_ = false;
    std::size_t lastPushedPolyphony_ = 0;
    std::uint32_t setPolyphonyCalls_ = 0;               // written only on the setup/process thread
    std::uint32_t lastSliceCount_ = 0;
    std::array<EventSlot, kMaxEventsPerBlock> eventOrder_{};
};

// FR-064: unique_ptr ownership keeps the object small; tests still heap-allocate it.
static_assert(sizeof(Processor) < 64u * 1024u, "FR-064: the 808 KB engine must live on the heap");

}  // namespace Vorago
```

The two counters and the smoother read are plain members, written only on the thread that calls
`setupProcessing`/`process` and read by tests between calls (FR-024a.1's wording); no atomics.

#### 2.5.2 `initialize()` / `terminate()` — FR-020, FR-022

`AudioEffect::initialize(context)`; on success `addEventInput(STR16("Event In"))`,
`addAudioOutput(STR16("Main Out"), Vst::SpeakerArr::kStereo)`, **no** `addAudioInput()` (model
`plugins/seraphis/src/processor/processor.cpp:560–561`; anti-model `plugins/ruinae/src/processor/processor.cpp:56`);
then `engine_ = std::make_unique<Krate::DSP::VoragoEngine>(); cavern_ = std::make_unique<Krate::DSP::CavernVerb>();`.
`terminate()`: `engine_ = nullptr; cavern_ = nullptr; prepared_ = false; return AudioEffect::terminate();`
(`= nullptr`, not `.reset()`, for the readability-ambiguous-smartptr-reset-call reason at
`plugins/seraphis/src/processor/processor.cpp:570–578`).

#### 2.5.3 `setBusArrangements()` — FR-021

Verbatim shape of `plugins/seraphis/src/processor/processor.cpp:594–608`:

```cpp
if (numIns != 0) return kResultFalse;
if (numOuts != 1) return kResultFalse;
if (outputs == nullptr || outputs[0] != Vst::SpeakerArr::kStereo) return kResultFalse;
return kResultTrue;
```

The rejection is not the guard; `process()` carries its own `numChannels < 2` early-out (FR-030).

#### 2.5.4 `setupProcessing()` — FR-023, FR-028

Exactly FR-023's order:

```cpp
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    // 0. Out-of-order host calls (pluginval strictness 5): stay unprepared.
    if (engine_ == nullptr || cavern_ == nullptr) { return AudioEffect::setupProcessing(setup); }
    const double sr = setup.sampleRate;           // engine floors a bad rate itself (vorago_engine.h:281)

    // 1. both configs from the CONSTANT 2048, never setup.maxSamplesPerBlock.
    // 2. seed BEFORE prepare: prepare derives every slot seed from seed_ and seeds each
    //    voice before its own prepare (vorago_engine.h:264-267, :295).
    engine_->setSeed(kEngineSeed);                // :544
    // 3.
    engine_->prepare(sr, makeVoragoEngineConfig(kMaxBlockSamples));   // :277
    cavern_->prepare(sr, makeVoragoCavernConfig(kMaxBlockSamples));   // cavern_verb.h:358

    // 4. polyphony FROM THE PARAMETER (setState may precede this); config has no field.
    const std::size_t poly = clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed));
    engine_->setPolyphony(poly);                  // :507, allocates nothing
    ++setPolyphonyCalls_;
    lastPushedPolyphony_ = engine_->getPolyphony();   // :523, the engine's clamped value

    // 5. master gain: configure, arm the first-block snap.
    masterGain_.configure(kMasterGainSmoothMs, static_cast<float>(sr));  // smoother.h:160
    snapGainPending_ = true;

    // 6. no scratch to size (§1.1). prepared_ last.
    prepared_ = true;
    return AudioEffect::setupProcessing(setup);
}
```

`engine_->setSeed` on the second and later `setupProcessing` calls hits an already-prepared engine and
therefore reseeds live (`vorago_engine.h:544–550` forwards to every slot; spec FR-023.2 correction). It is
immediately followed by `prepare()`, which re-derives every stream from the same `kEngineSeed` anyway, so
the net effect is identical to a first prepare. No other call site of `setSeed` exists.

`setupProcessing` is not the audio thread; `prepare` is the only allocating path of both components
(`vorago_engine.h:277` banner; `cavern_verb.h:358`). No MXCSR here (per-thread; set in `process()`).

#### 2.5.5 `setActive()` — FR-032

```cpp
tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state != 0) {
        snapGainPending_ = true;                  // re-arm the snap; NOTHING ELSE (SC-026: 0 allocations)
    } else {
        if (engine_) { engine_->silence(); }      // vorago_engine.h:447, allocation-free, not audio-thread
        if (cavern_) { cavern_->reset(); }        // cavern_verb.h:487, allocation-free, not audio-thread
    }
    return AudioEffect::setActive(state);
}
```

`silence()` and `reset()` are no-ops on unprepared components (`vorago_engine.h:446–450`; the cavern's
reset walks empty geometry), so `setActive(false)` before any `setupProcessing` is safe.

#### 2.5.6 `process()` — FR-024, FR-025, FR-026, FR-029, FR-030

```cpp
tresult PLUGIN_API Processor::process(ProcessData& data) {
    const Krate::DSP::ScopedDenormalMode denormalGuard;       // FR-029, core/scoped_denormal_mode.h:60

    processParameterChanges(data.inputParameterChanges);      // latched BEFORE the shape guards

    // FR-030 guard order (binding; plugins/seraphis/src/processor/processor.cpp:1325-1356).
    if (data.numOutputs <= 0 || data.outputs == nullptr) return kResultOk;
    if (data.outputs[0].channelBuffers32 == nullptr) return kResultOk;
    if (data.outputs[0].numChannels < 2) return kResultOk;
    if (data.numSamples <= 0) return kResultOk;
    const auto total = static_cast<std::size_t>(data.numSamples);
    float* outL = data.outputs[0].channelBuffers32[0];
    float* outR = data.outputs[0].channelBuffers32[1];
    if (outL == nullptr || outR == nullptr) return kResultOk;
    if (!prepared_ || engine_ == nullptr || cavern_ == nullptr) {
        std::fill_n(outL, total, 0.0f);
        std::fill_n(outR, total, 0.0f);
        data.outputs[0].silenceFlags = 3;
        return kResultOk;
    }

    pushGlobalParams();                                       // FR-024 step 0, once per call (P-9, §2.5.7)
    macros_.apply(*engine_);                                  // FR-024 step 2, once per call (P-10)
    applyCavernTargets(*cavern_, macros_.computeCavernTargets());   // FR-034a, once per call (P-10)

    const std::size_t numEvents = buildEventOrder(data.inputEvents, total);   // §3.2
    std::size_t next = 0;
    std::size_t cursor = 0;
    lastSliceCount_ = 0;

    while (cursor < total) {
        // 1. every event due at this slice start (a WHILE: same-offset events all fire).
        while (next < numEvents && static_cast<std::size_t>(eventOrder_[next].offset) <= cursor) {
            Steinberg::Vst::Event e{};
            if (data.inputEvents->getEvent(eventOrder_[next].listIndex, e) == kResultOk) {
                dispatchEvent(e);                             // FR-031
            }
            ++next;
        }
        // 2. slice end = next event offset, block end, or +2048 (FR-026), whichever first.
        std::size_t sliceEnd = std::min(total, cursor + kMaxBlockSamples);
        if (next < numEvents) {
            sliceEnd = std::min(sliceEnd, static_cast<std::size_t>(eventOrder_[next].offset));
        }
        // offsets are sorted and every one <= cursor was consumed, so sliceEnd > cursor.
        renderSlice(outL + cursor, outR + cursor, sliceEnd - cursor);
        ++lastSliceCount_;
        cursor = sliceEnd;
    }

    data.outputs[0].silenceFlags = 0;                         // FR-024
    return kResultOk;
}

void Processor::renderSlice(float* l, float* r, std::size_t n) noexcept {
    // step 2 (FR-034 / FR-034a) is NOT here: it runs once per process(), after
    // pushGlobalParams() (P-10). Phase 12 keeps it per block, never per slice.
    engine_->processStereoBlock(l, r, n);                              // step 3
    cavern_->processStereoBlock(l, r, l, r, n);                        // step 4, in place
    for (std::size_t s = 0; s < n; ++s) {                              // step 5, per sample
        const float g = masterGain_.process();
        l[s] *= g;
        r[s] *= g;
    }
    engine_->processOutputStage(l, r, n);                              // step 6, limiter last
}
```

For readability the loop above shows only the sorted queue. The implementation reads "next pending
event" through one small inline helper that yields the sorted queue first and then §3.2 step 3's
overflow queue, so both loops (dispatch and slice-end) stay exactly as written.

`dispatchEvent` (FR-031, §3.3): `kNoteOnEvent` → pitch guard `[0,127]`, then velocity `> 0.0f` ?
`engine_->noteOn(uint8(pitch), quantiseVelocity(v))` : `engine_->noteOff(uint8(pitch))`;
`kNoteOffEvent` → pitch guard, `engine_->noteOff(uint8(pitch))`; everything else ignored (CC64 included,
FR-009 item 5).

**Why step 2 is outside the slice loop (P-10).** Both calls read only `macros_` (never written after
construction in Phase 11) and `engine_->getPolyphony()` (changed only by `pushGlobalParams()`, which
precedes them), and every value they write is a stored base that survives a mid-block note-on, steal or
retrigger (P-10 (c)). Calling them once per `process()` therefore leaves the engine and cavern in the
same state at every slice as calling them per slice, and removes the only per-slice cost that scales
with the matrix size. The remaining per-slice cost (engine, cavern, gain loop, `processOutputStage`,
including the limiter's per-call upsample setup, `true_peak_limiter.h:104–127`) is inherent to
sample-accurate slicing and is measured for the event-dense worst case by SC-014 arm E.

**FR-027:** nothing copies `processOutputStage`'s 64-sample saturator loop; it is *"a CADENCE CHOICE,
NOT A SIZE CONSTRAINT"* (`vorago_engine.h:996–1000`).

**RT audit (SC-007).** No `new`/`delete`, no container growth (`eventOrder_` is a fixed `std::array`),
no lock, no `throw`, no I/O. `macros_.apply` and every engine/cavern call used are `noexcept` and
documented real-time safe (`vorago_engine.h:890`, `:1005`). `IEventList::getEvent` and the parameter
queue calls are C-ABI `tresult` methods.

#### 2.5.7 `pushGlobalParams()` — FR-024a

```cpp
void Processor::pushGlobalParams() noexcept {
    const std::size_t poly = clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed));
    if (poly != lastPushedPolyphony_) {                      // EDGE-TRIGGERED (FR-024a.1)
        engine_->setPolyphony(poly);                         // vorago_engine.h:507
        ++setPolyphonyCalls_;
        lastPushedPolyphony_ = engine_->getPolyphony();      // :523
    }
    const float gain = globalParams_.masterGain.load(std::memory_order_relaxed);
    if (snapGainPending_) {
        masterGain_.snapTo(gain);                            // smoother.h:263
        snapGainPending_ = false;
    } else {
        masterGain_.setTarget(gain);                         // smoother.h:170
    }
}
```

Hoisted to once per `process()` (P-9; spec amendment §8.2 item 9): the atomics cannot change within a call (parameter changes are latched
at the top and take the last point, FR-043), so a per-slice update would be observationally identical
and needlessly partition-shaped. `processParameterChanges` runs even on the zero-sample path (SC-021's
last clause), but `pushGlobalParams` does not; the stored value is pushed on the next rendered block.

#### 2.5.8 `processParameterChanges()` — FR-043

For each queue `i` in `[0, changes->getParameterCount())`: `id = queue->getParameterId()`,
`count = queue->getPointCount()`; skip if `count <= 0`; `queue->getPoint(count - 1, offset, value)`
(the **last** point); `if (id < kGlobalParamRangeEnd) handleGlobalParamChange(...)` else
`if (id < kMacroParamRangeEnd) handleMacroParamChange(...)`. Null `changes` → return.
`handleMacroParamChange` ignores `id > kMacroMassId` (§2.4); SC-009 sends IDs 150 and 199 to prove it.

#### 2.5.9 `getLatencySamples()` — FR-033

```cpp
Steinberg::uint32 PLUGIN_API Processor::getLatencySamples() {
    if (engine_ == nullptr || cavern_ == nullptr) { return 0u; }
    return static_cast<Steinberg::uint32>(engine_->getLatencySamples()     // vorago_engine.h:1031 (smear)
                                          + cavern_->getLatencySamples());  // cavern_verb.h:786
}
```

= `2048 + 1024 = 3072` after any prepare, at every rate: the smear's `fftSize_ =
bit_floor(clamp(fftSize, 512, 4096))` (`spectral_smear.h:200`) and the reverb's
`diffusionFftSize_ = clamp(bit_floor(requested), 256, 4096)` (`aether_reverb.h:1641`) take no rate term.
Before the first prepare: `0 + 1024` (`spectral_smear.h:472` needs `prepared_`; `aether_reverb.h:2717–2719`
with member default `:4672`). **No `restartComponent`** (FR-033; the Seraphis C-2 reasoning: an
`AudioEffect` has no route to `IComponentHandler`).

#### 2.5.10 `getState()` / `setState()` — FR-045, FR-046

```cpp
tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (state == nullptr) return kResultFalse;
    Steinberg::IBStreamer s(state, kLittleEndian);
    s.writeInt32(kCurrentStateVersion);
    saveGlobalParams(globalParams_, s);
    saveMacroParams(macroParams_, s);
    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    if (state == nullptr) return kResultFalse;
    Steinberg::IBStreamer s(state, kLittleEndian);
    Steinberg::int32 version = 0;
    if (!s.readInt32(version)) return kResultFalse;
    if (version > kCurrentStateVersion) return kResultFalse;   // FR-046: nothing loaded
    if (loadGlobalParams(globalParams_, s)) {                  // short stream: stop, keep the rest
        loadMacroParams(macroParams_, s);
    }
    return kResultOk;                                          // no prepare reachable from here
}
```

`setState` writes atomics only, so it is safe beside `process()`. A version `< 1` (0 or negative) is
accepted and read as v1; there has never been another version.

### 2.6 `plugins/vorago/src/controller/controller.{h,cpp}` — FR-047, FR-048, FR-050, FR-052, FR-055

```cpp
namespace Vorago {
class Controller : public Steinberg::Vst::EditControllerEx1, public VSTGUI::VST3EditorDelegate {
public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue, Steinberg::Vst::String128) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

    [[nodiscard]] Krate::Plugins::PresetManager* presetManagerForTest() const noexcept {
        return presetManager_.get();   // model plugins/seraphis/src/controller/controller.h:196
    }
private:
    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;
};
}  // namespace Vorago
```

- `initialize`: `EditControllerEx1::initialize`; `registerGlobalParams(parameters)`;
  `registerMacroParams(parameters)` (exactly 14); `presetManager_ =
  std::make_unique<Krate::Plugins::PresetManager>(makeVoragoPresetConfig(), nullptr, this);`
  (`preset_manager.h:55–61`; Seraphis `controller.cpp:157–158`). **No `UpdateChecker`** (FR-052).
  No `setStateProvider`/`setLoadProvider` wiring: there is no browser UI to call them until Phase 13, and
  the Seraphis Phase 12 hotfix that added them came with the browser.
- `terminate`: `presetManager_ = nullptr;` then `EditControllerEx1::terminate()`.
- `setComponentState`: null → `kResultFalse`; read version; `> kCurrentStateVersion` →
  `kResultFalse`; then `loadGlobalParamsToController(s, setParam)` and
  `loadMacroParamsToController(s, setParam)` with
  `setParam = [this](Steinberg::Vst::ParamID id, double v) { setParamNormalized(id, v); }`.
- `getParamStringByValue`: `formatGlobalParam`, then `formatMacroParam`, else
  `EditControllerEx1::getParamStringByValue` (polyphony formats itself).
- `createView`: `FIDStringsEqual(name, Vst::ViewType::kEditor)` →
  `new VSTGUI::VST3Editor(this, "editor", "editor.uidesc")` (Seraphis `controller.cpp:434–436`); else
  `nullptr`. No `createCustomView`/`verifyView`: no custom views, no raw view pointers, so `willClose()`
  on a never-attached tree touches only VSTGUI-owned state (FR-055).
- **No `INoteExpressionController`, no `IMidiMapping`** (FR-019, OQ-7 ruling). No
  `DEFINE_INTERFACES` block beyond what the two bases provide.
- **Compile touch point for the update config** (it is included by nothing else, and a `.h` in a CMake
  source list is `HEADER_FILE_ONLY`, never compiled — Seraphis plan §2.8):
  ```cpp
  #include "update/vorago_update_config.h"
  static_assert(std::is_same_v<decltype(Vorago::makeVoragoUpdateConfig()),
                               Krate::Plugins::UpdateCheckerConfig>);
  ```

### 2.7 `src/preset/vorago_preset_config.h` — FR-050, FR-051

```cpp
#pragma once
#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"

namespace Vorago {
inline Krate::Plugins::PresetManagerConfig makeVoragoPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID      =*/kProcessorUID,
        /*.pluginName        =*/"Vorago",
        /*.pluginCategoryDesc=*/"Synth",
        /*.subcategoryNames  =*/{"Drones"}};   // field order: preset_manager_config.h:19-24
}
}  // namespace Vorago
```

`resources/presets/Drones/.gitkeep` makes the filesystem half agree from day one (FR-051).
`krate_plugin_install_presets` must tolerate a category directory holding only `.gitkeep`; verify by the
Windows configure+build in SC-020 (Seraphis shipped Phase 8 the same way with `Textures/.gitkeep`).

### 2.8 `src/update/vorago_update_config.h` — FR-052

```cpp
#pragma once
#include "update/update_checker_config.h"
#include "../version.h"

namespace Vorago {
inline Krate::Plugins::UpdateCheckerConfig makeVoragoUpdateConfig() {
    return Krate::Plugins::UpdateCheckerConfig{
        /*.pluginName     =*/stringPluginName,
        /*.currentVersion =*/VERSION_STR,
        /*.endpointUrl    =*/"https://rolandzwaga.github.io/krate-audio/versions.json"};
}
}  // namespace Vorago
```

Model: `plugins/seraphis/src/update/seraphis_update_config.h:21–27`. Compiled only by §2.6's touch point.

### 2.9 `src/entry.cpp` — FR-018

`plugins/seraphis/src/entry.cpp:47–85` with `Seraphis` → `Vorago`: `#define stringPluginName "Vorago"`
(identical to the generated `version.h`'s definition from `version.json`'s `"name"`, so the redefinition
is warning-free), `BEGIN_FACTORY_DEF`, two `DEF_CLASS2` (processor: `kVstAudioEffectClass`,
`Vorago::kSubCategories`; controller: `kVstComponentControllerClass`), `END_FACTORY`. **No `ui/*.h`
include.**

### 2.10 `resources/editor.uidesc` — FR-054

Stock views only, template `"editor"` (`CViewContainer`, 420 × 520), 14 `<control-tag>` bindings
(`MasterGain`=0, `Polyphony`=1, `MacroDarkness`=100 … `MacroMass`=111). Thirteen `CSlider`s (master
gain + twelve macros; plain `Vst::Parameter`s) and one `COptionMenu` (polyphony, a
`StringListParameter`), each beside a decorative untagged `CTextLabel`. Attribute sets are the Seraphis
Phase 8 placeholder's (Seraphis plan §2.10: `orientation="horizontal" draw-frame draw-back draw-value
frame-color back-color value-color` for sliders; `font font-color frame-color back-color fill-color
frame-width style-3D-in/out` for the menu). A leading XML comment says *"Phase 11 PLACEHOLDER — Phase 13
replaces this file wholesale; stock views only"*. No bitmaps, no custom class names.

### 2.11 `resources/` — AU identity (FR-015, FR-016, FR-017)

- `auv3/audiounitconfig.h.in`: complete copy of `plugins/seraphis/resources/auv3/audiounitconfig.h.in:1–40`
  with `Seraphis`→`Vorago`, `Srph`→`Vrgo` in **both** quoted and unquoted (`…1`) forms;
  `kAUcomponentName Krate Audio: Vorago`, `kAUcomponentTag Synthesizer`,
  `kAUcomponentVersion @AU_COMPONENT_VERSION@`, the digit-pair comment, `kSupportedNumChannels 02`, the
  flags and the delegate define kept verbatim.
- `au-info.plist`: `plugins/seraphis/resources/au-info.plist:23–51` with the same substitutions — one
  `AudioComponents` dict (`AUWrapperFactory`, `KrAt`, `Vrgo`, `aumu`, `Krate Audio: Vorago`) and one
  `SupportedNumChannels` dict `Inputs 0 / Outputs 2`. Any disagreement with §2.5.2 is AU error `-10875`.
- `auv3/macOS/Vorago.entitlements`: byte copy of `Seraphis.entitlements`.

### 2.12 Non-source files

| File | Content |
|---|---|
| `CMakeLists.txt` | §5.1 |
| `version.json` | exactly `version "0.1.0"`, `name "Vorago"`, `description`, `publisher "Krate Audio"`, `url "https://krateaudio.com/vorago/"`, `copyright`. No `preset_subdir` (FR-002). |
| `CHANGELOG.md` | `## [0.1.0]` section describing the scaffold (FR-010; parsed by `check-changelog-coverage.js`). |
| `README.md` | one paragraph + build / test / pluginval commands. |
| `CLAUDE.md` | leaf in `plugins/seraphis/CLAUDE.md`'s shape (FR-009) **plus the five decisions**: (1) OQ-7 deferred to Phase 12 with the controller-FUID host-cache caveat; (2) preset categories grow only, `Drones` permanent; (3) the soft-limit omission (FR-041) and why; (4) polyphony "1".."6" with the 30 % ceiling gating only the default 4 (5–6 measured 37.99–43.41 % engine-only in Phase 10); (5) CC64 deferred to Phase 12 as a wrapper-side note-off latch. Also: the two FUIDs as immutable, the reserved ID map, `vorago_tests` invocation, pluginval path, "no DSP lives in this plugin", and the P-10 rule that live macros are pushed once per `process()`, never per slice. |
| `installers/windows/setup.iss` | `plugins/seraphis/installers/windows/setup.iss` with `Seraphis`→`Vorago` and a **fresh** `AppId` GUID (Seraphis's is `{13374501-…}` at `:18`). |
| `installers/linux/README.txt` | Seraphis's, retitled. |
| `docs/.gitkeep`, `src/ui/.gitkeep`, `resources/presets/Drones/.gitkeep` | FR-056, FR-080, FR-051. |

---

## 3. Pinned algorithms and control flow

### 3.1 Master-gain smoother (FR-024a.2)

`Krate::DSP::OnePoleSmoother` (`smoother.h:134`), recurrence from `process()` (`:197–208`):

```
if |y - t| < kCompletionThreshold:  y := t
else:                               y := t + a·(y - t);  y := flushDenormal(y)
a = calculateOnePolCoefficient(20 ms, fs)     // 99 % settle in 20 ms  ->  tau = 20/ln(100) = 4.343 ms
```

At 48 kHz `tau = 208.4` samples, so `a = exp(-1/208.4) = 0.995213`. Binding choices:

- **Cadence:** one `process()` per output sample, inside `renderSlice`'s step-5 loop. After `N` samples
  `y = t + a^N (y0 - t)` independent of partition (SC-008).
- **Target:** once per `process()`, in `pushGlobalParams` (§2.5.7).
- **Snap:** `snapTo(target)` on the first rendered block after `setupProcessing()` / `setActive(true)`.
- **Placement:** after the cavern, before `processOutputStage`, so `TruePeakLimiter` stays last and
  `|out| <= ceiling` holds by construction (`true_peak_limiter.h:12–14`: *"every output sample satisfies
  |out| <= ceiling exactly"*; ceiling installed from `kOutputCeilingDb = -0.3f`, `vorago_engine.h:203`,
  → `10^(-0.3/20) = 0.96605`).

### 3.2 Event ordering and slice partition (FR-025, FR-026)

```cpp
// clamp into [0, total-1]: past-the-end -> last sample, negative -> 0 (FR-025).
[[nodiscard]] inline std::int32_t clampOffset(Steinberg::int32 o, std::size_t total) noexcept {
    const auto last = static_cast<std::int32_t>(total - 1);   // total > 0 here
    return (o < 0) ? 0 : ((o > last) ? last : o);
}
```

`buildEventOrder(events, total)`:

1. `count = events ? events->getEventCount() : 0`; `n = min(count, kMaxEventsPerBlock)`.
2. For `i` in `[0, n)`: `getEvent(i, e)`; on success append `{clampOffset(e.sampleOffset, total), i}`
   by **stable insertion sort on `offset`** (shift right while `prev.offset > new.offset` — strict `>`
   preserves list order among equal offsets, so a same-offset NoteOff→NoteOn pair keeps its meaning).
   Cost: `O(n)` for an already-sorted list (the VST3-conformant case), `O(n²)` worst case, bounded by 1024.
3. **Overflow (`count > 1024`)**: events `[1024, count)` are **not stored**. They form a second,
   unsorted queue read straight from `data.inputEvents` in list order, after the sorted queue is
   exhausted. Each overflow event's effective offset is
   `max(clampOffset(e.sampleOffset), previous effective offset)`, where the first "previous" is the last
   sorted entry's offset. Effective offsets are therefore non-decreasing, the slice loop consumes them
   exactly like sorted entries (a plain `std::size_t overflowNext` index plus one `int32` running
   maximum, both locals of `process()`, no allocation), and the cursor never rewinds. Nothing is
   dropped, so no note can stick; only the ordering guarantee degrades, and only past 1024 events in
   one block.
4. Return the number of sorted entries.

Slice partition: slices end at the next pending event offset, the block end, or `cursor + 2048`,
whichever is first. Every event with `offset <= cursor` is dispatched before the slice renders (a
`while`, so equal offsets never produce a zero-length slice). An event-free 4096-sample block therefore
renders as exactly 2 slices (FR-026's seam).

### 3.3 Velocity and pitch quantisation (FR-031)

`uint8_t quantiseVelocity(float v) { return uint8_t(std::clamp(v * 127.0f + 0.5f, 1.0f, 127.0f)); }`
for `v > 0`. `v = 0.003` → `0.881` → clamped to `1` → a note-on, never the engine's velocity-0
note-off (`vorago_engine.h:568–571`). `v = 1.0` → `127`. Pitch (`int16`) outside `[0, 127]` is dropped
before the `uint8_t` cast. `VoragoEngine::noteOn/noteOff` take no offset (`:564`, `:601`), which is why
§3.2 slices at every event.

### 3.4 State stream byte layout (FR-045, FR-046, SC-010)

Little-endian `IBStreamer`:

| Offset | Bytes | Field | Default |
|---|---|---|---|
| 0 | 4 | `int32 kCurrentStateVersion` | 1 |
| 4 | 4 | `float masterGain` (linear, [0, 2]) | 1.0 |
| 8 | 4 | `int32 polyphony` ([1, 6]) | 4 |
| 12 | 48 | `float` × 12 macros, `VoragoMacro` order | 0.0 except gravity (offset 28) = **0.5** |

Total **60 bytes**. A cut at offset 12 (after the global block) leaves all twelve macros at their prior
values (SC-010.4). A digest over this **serialized stream** is allowed; a digest over rendered audio is
not (FR-065).

### 3.5 Seed policy

`kEngineSeed = kCavernSeed = 1u` (§2.2), engine seeded before every `prepare`. Per-slot seeds are fixed
and never advanced per note (`vorago_engine.h:526–543`), so a render is a pure function of
(seed, config, note sequence, partition-free): two instances with the same input render identically.
Every render comparison in §4.3 relies on this. Phase 12 owns a seed parameter and **must not assume a
post-prepare `setSeed` is a no-op** (spec FR-023.2 correction; `vorago_voice.h:961–966`).

---

## 4. Test plan

Target `vorago_tests`. Every case tagged `[vorago]` plus an area tag; FR-066 fixes the nine case names.
Requirements covered by a case appear as `SECTION`s inside it (section names are free; case names are
not). All renders use 48 kHz unless stated. All non-finite checks use bit patterns
(`Krate::DSP::detail::isFinite`, `db_utils.h:118`) — never `std::isnan/isinf/isfinite` (FR-062).

### 4.1 File → case → coverage

| File | `TEST_CASE` (tags) | SCs |
|---|---|---|
| `unit/processor_bus_test.cpp` | `Vorago_ProcessorBusSetup` `[vorago][processor]` | SC-011 |
| `unit/param_denorm_test.cpp` | `Vorago_ParamDenormRoundTrip` `[vorago][params]` | SC-009 |
| `unit/state_roundtrip_test.cpp` | `Vorago_StateRoundTrip` `[vorago][state]` | SC-010 |
| `unit/midi_event_test.cpp` | `Vorago_MidiEventTranslation` `[vorago][processor][midi]` | SC-008 (1), SC-022 |
| `unit/lifecycle_test.cpp` | `Vorago_ProcessorLifecycle` `[vorago][processor]` | SC-007, SC-013, SC-021, SC-026 |
| `unit/controller/editor_lifecycle_test.cpp` | `Vorago_EditorLifecycle` `[vorago][controller][ui][lifecycle]` | SC-012 |
| `integration/processor_audio_test.cpp` | `Vorago_ProcessorRendersHeldNote` `[vorago][integration]` | SC-005, SC-006, SC-024 |
| `integration/param_flow_test.cpp` | `Vorago_ParamFlowReachesEngine` `[vorago][integration]` | SC-008 (2), SC-019, SC-023 |
| `integration/processor_cpu_test.cpp` | `Vorago_ProcessorCpu` `[vorago][.perf][performance]` | SC-014 |

SC-008 (2) is a parameter-timing criterion and lives with the other parameter-flow checks; FR-066 lists
`param_flow_test.cpp` as covering FR-024a, which clause 2 exercises.

**Inspection-only requirement: FR-029's `ScopedDenormalMode` clause.** `Vorago_ProcessorLifecycle`
(SC-007) covers FR-029's no-allocation half only. The *"MUST construct a
`Krate::DSP::ScopedDenormalMode` at its top"* half cannot be tested here: `test_main.cpp` calls
`enableFTZDAZ()` on the test thread (§4.2), which masks a missing guard, and the guard restores MXCSR on
exit, so nothing is observable afterwards. It is verified **by inspection**: `grep -n
"ScopedDenormalMode" plugins/vorago/src/processor/processor.cpp` must show it as the first statement of
`Processor::process()`, and `compliance.md` cites that file:line. The compliance row for FR-029 does
not claim this clause through `Vorago_ProcessorLifecycle` (spec amendment §8.2 item 11).

### 4.2 Shared fixture — `plugins/vorago/tests/vorago_test_fixture.h`, `namespace VoragoTest`

Reuses `Krate::Test::EventList` (`tests/test_helpers/vst_event_list.h`; `addNoteOn(pitch, velocity,
offset)`, `addNoteOff`, `addEvent(Event&)` for arbitrary types and orders) and `Krate::Test::
ParameterChanges` / `ParamValueQueue` (`vst_param_changes.h:31–78`, single point at offset 0) wherever
one point suffices (FR-063). Defines **only** what they lack:

- `MultiPointParamValueQueue` — `addTestPoint(int32 offset, double value)`, reports its points in order
  (SC-009's `{0.1@0, 0.9@100}` and SC-008 (2)'s point at offset 300 need offsets other than 0).
- `MultiParamChanges` — `IParameterChanges` over a `std::vector<MultiPointParamValueQueue>`;
  `addQueue(id)`, `clear()`.
- `ProcessorFixture`:
  - `std::unique_ptr<::Vorago::Processor> proc` (heap, FR-064);
  - `prepare(double sr, int32 maxBlock)` → `initialize(nullptr)`, `setupProcessing`, `setActive(true)`;
  - `renderBlocks(std::span<const std::size_t> blockSizes, …)` builds a `ProcessData` with one stereo
    output bus over **pre-reserved** channel vectors, attaches the current `EventList` /
    `MultiParamChanges` for the next block only, calls `process`, appends to `capturedL/R`, clears the
    per-block inputs. A `perBlock` callback lets a test inject events/params before block `k`;
  - guard words either side of each channel buffer, checked after every call (the Seraphis fixture's
    canary pattern, `seraphis_test_fixture.h`: out-of-range writes are a test failure);
  - `reserveCapture(totalSamples)` so render sections under `AllocationScope` do not grow vectors.
- Stats helpers: `peakOf(span)`, `rmsOf(span)`, `maxAbsDiff(a, b, from, to)`, `allFinite(span)`.

**Namespace hazard:** tests name `::Vorago::…` fully qualified; no TU writes
`using namespace Krate::DSP::TestUtils;` (it would make `Vorago` ambiguous once
`vorago_perf_budget.h` or `vorago_fixtures.h` is visible). `vorago_fixtures.h` is not included at all:
`applyFastAttack` cannot reach a processor-owned engine and SC-005 forbids changing the envelope.

`test_main.cpp`: `enableFTZDAZ()` before `Catch::Session().run`, `void* moduleHandle = nullptr;`, and the
binary's **only** `#include <allocation_operator_overrides.h>` (FR-061; model
`plugins/seraphis/tests/unit/test_main.cpp:15, :24, :27`).

### 4.3 Assertion strategy, criterion by criterion

Tolerances: within-process comparisons of renders through the same `Processor::process()` use the
spec's max-abs thresholds; nothing is a bit-exact digest (`tools/lint-float-bit-goldens.js` clean).
`compareFingerprints` (`render_fingerprint.h:122`) appears only as a `WARN`-level secondary check.

**SC-005 — held note non-silent** (`Vorago_ProcessorRendersHeldNote`, SECTION `HeldNoteIsAudible`).
Fixture at 48 kHz / 512; defaults (no param changes); `NoteOn(48, 100/127.f)` at offset 0 of block 0;
8 s (750 blocks). `REQUIRE(allFinite)` over both channels; `REQUIRE(peak over [7 s, 8 s) >= 1.0e-4)`.
`WARN` the peak and RMS of that last second (recorded in `compliance.md`).

**SC-006 — ceiling** (same case, SECTION `OutputNeverExceedsCeiling`). Polyphony normalized 1.0
(= 6), master gain normalized 1.0 (linear 2.0), six notes `{36, 40, 43, 47, 50, 53}` at offset 0,
30 s. `REQUIRE(max|x| <= 0.9661f)` on both channels, all finite. The same script at master gain 0.5 is
rendered and both peaks `WARN`ed, **recorded not gated** (B-1: it peaks at ~0.24; no six-voice render
reaches the limiter). **Discrimination arm (B-1):** `renderProbeThroughGainAndOutputStage(gainNorm)` —
prepare, one silent `process()` block at `gainNorm` (snaps the smoother), then 40 blocks of a
0.6-amplitude 110 Hz tone through `Processor::renderGainAndOutputStage()` (steps 5–6 alone, public);
peak of the last block. `REQUIRE(unity peak >= 0.49f)`, `REQUIRE(gain-2.0 peak <= 0.9661f)` both
channels, `REQUIRE(gain-2.0 peak > unity peak)`. Never lower 0.49, never raise 0.6 above 0.72. Not
`[long]` (P-7).

**SC-024 — cavern targets pushed** (same case, SECTION `CavernTargetsArePushed`, all in this TU).
Three heap `CavernVerb`s prepared with `makeVoragoCavernConfig(512)` at 48 kHz. Input: a deterministic
stereo signal generated in the TU — 1 s of a 110 Hz sine at 0.25 then 2 s of zeros. Cavern A:
`applyCavernTargets(A, T)` with `T = {0.9, 0.2, 5, 0.8, 0.9, 0.5, 0.3}`. Cavern B: the seven setters
called by hand with the same values in FR-034a's order. Cavern C: untouched defaults. Render 3 s at
512 blocks, pushing before each block. `REQUIRE(maxAbsDiff(A, B) <= 1e-6)`;
`REQUIRE(rmsOf(A - C) > 1e-3)`.

**SC-009 — denorm round-trip** (`Vorago_ParamDenormRoundTrip`). Initialized, **unprepared** processor
(parameter latching precedes the shape guards, so a `process()` with `numOutputs = 0` applies them —
same path SC-021 checks). For each of the 14 IDs × `{0, .25, .5, .75, 1}`: one-point
`Krate::Test::ParameterChanges`, `process`, read `globalParamsForTest()` / `macroParamsForTest()`.
Master gain `Approx(2v).margin(1e-6)`; polyphony exactly `{1,2,4,5,6}`; macros `Approx(v).margin(1e-6)`.
Multi-point `{0.1@0, 0.9@100}` on `kMacroFogId` → `0.9`. **Unregistered in-band IDs:** set all
twelve macros to distinct non-defaults, snapshot them, then send one-point changes on IDs **150** and
**199** (value 0.9) through `process()` → `REQUIRE` all twelve macro atomics bit-equal to the
snapshot and both globals unchanged (§2.4's early return). Controller arm: `Controller::initialize`,
`getParameterCount() == 14`, each ID present; `toPlain(getDefaultNormalizedValue())` via
`getParameterObject(id)` equals the table (gain 1.0 as plain linear via `normalized·2`, polyphony index
3 → `(3+1) = 4` voices, macros `0/…/0.5/…`). **This is the P-1 detector.** Also `REQUIRE` that no ID
outside the 14 is registered (FR-041 soft-limit absence).

**SC-010 — state** (`Vorago_StateRoundTrip`, `Steinberg::MemoryStream`). (1) Set all 14 to
non-defaults, `getState` → fresh processor `setState` → `getState`; the two streams' bytes equal
(`memcmp` over 60 bytes — a stream digest, allowed). (2) Default processor's stream: bytes 28–31 decode
to `0.5f`, bytes 8–11 to `4`. (3) Hand-built stream with version 2 → `kResultFalse`; all atomics
unchanged. (4) Stream truncated to 12 bytes after setting macros to non-defaults on the target →
`kResultOk`, globals loaded, macros unchanged. (5) Controller: `setComponentState(stream 1)` → every
`getParamNormalized(id)` within `1e-6` of the streamed normalized value. Plus a corrupt-stream
sub-arm: polyphony `int32 = 99` and gain `+Inf` (bit pattern via volatile) → stored polyphony 6, gain
unchanged; the next `process()` calls `setPolyphony` once and a second `process()` does not (the
§2.3 convergence property). This TU is in the `-fno-fast-math` list (§5.2).

**SC-011 — buses** (`Vorago_ProcessorBusSetup`). After `initialize`: `getBusCount(kEvent, kInput) == 1`,
`getBusCount(kAudio, kInput) == 0`, `getBusCount(kAudio, kOutput) == 1`, output arrangement
`kStereo`. `setBusArrangements`: `(0 in, [stereo])` → `kResultTrue`; `(1 in stereo, [stereo])`,
`(0, [mono])`, `(0, [stereo, stereo])` → `kResultFalse`.

**SC-012 — editor** (`Vorago_EditorLifecycle`). (1) `Krate::TestSupport::exerciseEditorLifecycle(
controller, "editor", VORAGO_RESOURCES_DIR "/editor.uidesc")` (`editor_lifecycle_harness.h:102`), 3
cycles. (2) SECTION `EditorBindsFourteenControls`: own `VST3Editor`, `attached(nullptr,
nativePlatformType())`, recursive walk collecting `CControl`s with `getTag() >= 0` (the Seraphis
`collectControls` filter — `CTextLabel` IS-A `CControl` with tag −1), `REQUIRE(size == 14)`, tag set
== the 14 IDs, `dynamic_cast<COptionMenu*>(tag 1) != nullptr`; `removed()`, `release()`. (3) SECTION
`PresetConfigIsLive`: `makeVoragoPresetConfig()` fields; P-4's two temp dirs
(`std::filesystem::temp_directory_path() / "vorago_sc012_<pid-or-counter>"`, created and removed by the
section); `PresetManager pm(cfg, nullptr, nullptr, userEmpty, factory)`; `scanPresets()` size 1,
`subcategory == "Drones"`; `REQUIRE(is_directory(VORAGO_RESOURCES_DIR "/presets/Drones"))`;
`controller.presetManagerForTest() != nullptr`. (4) `vorago_tests.exe "[lifecycle]" --list-tests` lists
the case (command check in the build stage). (5) ASan run (§5.8).

**SC-007 — zero allocations** (`Vorago_ProcessorLifecycle`, SECTION `ProcessDoesNotAllocate`).
Liveness probe first: `TestHelpers::AllocationScope` around one `new int` → count `>= 1`. Then prepare,
one 512 warm-up block, pre-build every `EventList` / `MultiParamChanges` for the next 2 s (188 blocks:
note-ons/offs at varied offsets, polyphony 0.0/1.0 flips, gain and macro changes), reserve capture,
then render inside one `AllocationScope` and read `getAllocationCount()` **inside** the scope → `0`.

**SC-013 — latency** (same case). Before prepare: `WARN(getLatencySamples())` (expect 1024, recorded,
not gated). For each `{44100, 48000, 88200, 96000, 192000}`: `setupProcessing` →
`REQUIRE(getLatencySamples() == 3072)` and `== engineForTest()->getLatencySamples() +
cavernForTest()->getLatencySamples()`. Then a second rate, `setActive(false/true)`, and one block with
all 14 parameters changed → still 3072.

**SC-021 — degenerate shapes** (same case, one SECTION per shape): `numOutputs = 0`; `outputs` with
`channelBuffers32 = nullptr`; mono bus (`numChannels = 1`, a one-element pointer array); `numSamples =
0`; `channelBuffers32[1] = nullptr`; `process()` before `setupProcessing()` with buffers pre-filled
`0.5f` and `silenceFlags` pre-seeded `0` → fully zeroed and `silenceFlags == 3`. A rendered block
pre-seeded `silenceFlags = 3` reads `0` afterwards. Every call returns `kResultOk`. A master-gain change
sent with `numSamples = 0` is visible in `globalParamsForTest()` afterwards.

**SC-026 — setActive clears the tail** (same case). Hold `NoteOn(48, 127/127.f)` for **8 s** (the
SC-005 window, where a held note is known to reach `1e-4`); `setActive(false)`; `AllocationScope`
around `setActive(true)` → `0`; render 1 s with no events → `peak < 1e-6` over both channels.
**Precondition (P-6):** the peak over the **last 1 s before `setActive(false)`** (a window of the same
length) is `>= 1e-4` — 100× the pass threshold. **Negative control:** a second processor runs the
identical script **without** the `setActive(false/true)` pair and `REQUIRE`s the peak of its following
1 s `>= 1e-4`. The control proves the 1 s window would carry the held note and the cavern tail if
FR-032's `silence()` / `reset()` were missing, so the `< 1e-6` assertion can fail. If either
precondition misses `1e-4`, the hold is lengthened — never either threshold.

**SC-008 (1) — block-size invariance** (`Vorago_MidiEventTranslation`, SECTION
`BlockSizeInvariance`). Script over 4 s (192 000 samples): `NoteOn(48, 1.0)` @ 0, `NoteOn(55, 0.8)` @
37 111, `NoteOff(48)` @ 100 003, `NoteOn(60, 0.9)` @ 150 007 — absolute sample positions that are not
multiples of 7, 64, 65, 512, 2048 or 4096, and no parameter change. Rendered at host blocks
`{1, 7, 64, 65, 512, 2048, 4096}`; each event is converted to `(block, offset)` for that partition.
Precondition (P-6): the 512 reference's peak over `[3072, end)` `>= 1e-4` (lengthen the script if not).
Each partition vs 512: `REQUIRE(maxAbsDiff <= 1e-5)` per channel. `static_assert(65 % 64 != 0)` is the
"partition boundary inside a control chunk" proof. The 4096 run: an event-free 4096 block reports
`lastSliceCountForTest() == 2`. `compareFingerprints` `WARN`-only. Runtime note: the block-1 arm makes
192 000 `process()` calls; measured wall time is recorded, and the case is **not** `[long]` unless it
exceeds ~15 s (in which case only this SECTION's partition list is kept and the case tagged per the
root `CLAUDE.md` rule — the partition set is never reduced).

**SC-022 — MIDI translation** (same case). (1) NoteOn → `getActiveVoiceCount()` +1 and that slot's
`getVoiceState(i) == VoiceState::Active` (`vorago_engine.h:1065`). (2) NoteOff, and separately
NoteOn velocity 0 → slot `Releasing`, count unchanged. (3) pitch 128 / −1 → count unchanged, no slot
state changes. (4) a `kPolyPressureEvent` and a `kDataEvent` → nothing changes. (5) offset timing: A =
NoteOn(48,100) @300 of a 512 block; B = a 300-sample block then NoteOn @0 of the next 512 block; C =
NoteOn @0 of the first block. Render ≥ 1 s past 3072 (lengthen until the precondition holds). Window
`[3372, end)`: precondition peak(A) `>= 1e-4`; `maxAbsDiff(A, B) <= 1e-5`; `maxAbsDiff(A, C) > 1e-5`.
(6) velocity 0.003 → slot `Active`. (7) NoteOn @600 in a 512 block ≈ @511 (`<= 1e-5`), @−5 ≈ @0,
clause-5 window and precondition. (8) `[NoteOn(55)@400, NoteOn(48)@100]` ≈ sorted list (`<= 1e-5`),
same window and precondition — **the P-3 detector**. (9, plan-added, covers §3.2's overflow branch)
one 2048-sample block (fixture prepared with `maxBlock = 2048`, default polyphony 4) carrying 1100
events:
- **sorted segment**, list indices `[0, 1024)`: alternating `NoteOn(60, 1.0)` / `NoteOff(60)` at
  offset `= index` (index 1023 is a `NoteOff`);
- **overflow segment**, list indices `[1024, 1100)`: index 1024 = `NoteOn(62, 1.0)` @1100; index 1025 =
  `NoteOn(64, 1.0)` @**5**, an out-of-order offset below the running maximum, which §3.2 step 3 must
  clamp up to 1100 rather than rewind the cursor; indices `[1026, 1100)` = 74 alternating
  `NoteOn(60)` / `NoteOff(60)` at offsets 1101…1174 (index 1099 is a `NoteOff`).

`REQUIRE(process() == kResultOk)`, output finite, fixture canaries clean. Then count slots
`i < kMaxVoices` by `getVoiceState(i)`: **exactly 2 `VoiceState::Active`** (notes 62 and 64, the
only notes never released, both carried only by the overflow segment) and `getActiveVoiceCount() ==
3` (plus pitch 60 `Releasing`). An implementation that drops the overflow queue ends with **0**
Active slots (the last sorted event is a `NoteOff(60)`), and one that drops the out-of-order event
ends with 1, so both fail. This is the test of §3.2's *"Nothing is dropped, so no note can stick"*.

**SC-019 — globals reach the chain** (`Vorago_ParamFlowReachesEngine`). (1) Gain 0.0 in block 0's
parameter changes, `NoteOn(48)` @0, 4 s → `peak < 1e-6`; **non-vacuity (P-2):** same at 0.5 →
`peak >= 1e-4` (lengthen the render if not); **snap (P-2):** after block 0 at gain 0,
`masterGainValueForTest() == 0.0f` exactly. (2) Polyphony 0.0 → `engineForTest()->getPolyphony() == 1`;
1.0 → `6`. (3) seam first proves itself: a changed value raises `setPolyphonyCallCountForTest()` by
exactly 1; the same value sent again raises it by 0. (4) unprepared processor, `setState` with polyphony
2 (via a stream built by another processor), then `setupProcessing` → `getPolyphony() == 2`.

**SC-008 (2) — parameter timing is block-granular** (same case). 512 blocks, NoteOn @0, ≥ 1 s warm-up
(block `N = 94`), then render R1 with gain 0.0 at offset 300 of block N and R2 with gain 0.0 at offset
0 of block N → `maxAbsDiff(R1, R2) <= 1e-5` over the whole render. Non-vacuity: R0 (no change) vs R1
over `[N·512 + 3072, N·512 + 3072 + 48000)` → `rms(R0 - R1) > 1e-3` (lengthen the warm-up if the held
note is too quiet to reach it).

**SC-023 — macros inert** (same case). Render M1 with all 12 macros at 1.0 (block 0) and M0 at defaults,
same note script, 4 s → precondition peak(M0) `>= 1e-4` over `[3072, end)`; `maxAbsDiff <= 1e-5`.

**SC-014 — CPU** (`Vorago_ProcessorCpu`, hidden). Both arms built in this TU at 48 kHz, 512 blocks,
polyphony 4, notes `{36, 40, 43, 47}` velocity 100: arm P = a `::Vorago::Processor` prepared via the fixture's `prepare()`, but **timed through a bare
`proc->process(data)` call**: the `ProcessData`, its stereo output bus and the output buffers are built
once before timing starts; the four note-ons are in an `EventList` attached to the **first warm-up
block only** and detached afterwards (`inputEvents = nullptr`, `inputParameterChanges = nullptr` for
every timed block). The timed region contains no capture, no canary check, no allocation and no
fixture call, the same shape as arm D's bare chain calls, so the 1.05× gate measures only what the
wrapper adds (FR-067a);
arm D = a heap `VoragoEngine` (`setSeed(kEngineSeed)`, `prepare(48000, makeVoragoEngineConfig(2048))`,
`setPolyphony(4)`, four `noteOn`) + heap `CavernVerb` (`makeVoragoCavernConfig(2048)`) + local
`OnePoleSmoother` at unity, driven by the direct chain (engine → cavern in place → gain loop →
`processOutputStage`). Warm-up 100 blocks each, discarded. Then 16 trials, **interleaved** P, D, P, D …,
each timing 100 blocks with `std::chrono::steady_clock`; take best-of-16 per arm, divide by 100.
`REQUIRE(P_best <= 1.05 * D_best)` (FR-067a). `WARN` P_best and its ratio to `kReferenceNs` (FR-067,
recorded only), with `kReferenceNs` taken from `dsp/tests/unit/systems/vorago_perf_budget.h:82` (single
source, not re-typed; §5.2 adds the include path). A P_best above `kReferenceNs` is reported to the user
as a Phase 10 budget finding. **Arm E (event-dense worst case, `WARN`-recorded, not gated):** after
the P/D trials, a fresh processor prepared at 48 kHz / `maxBlock = 2048`, polyphony 4, one 2048-sample
warm-up block with the four held notes; then 16 trials, each timing **one** 2048-sample `process()`
whose pre-built `EventList` holds **1024 events in strictly reverse offset order** (offsets 2047 down to
1024, alternating `NoteOn(60)` / `NoteOff(60)` so the pool state stays bounded): the insertion sort's
O(n²) case and 1024 slices. Each trial's list is built before the timed region. `WARN` the best-of-16
ns and its ratio to the block's real-time duration (2048 / 48000 s); both go into `compliance.md`. No
threshold is invented here (the spec has none for this case); a ratio `>= 1.0` (slower than real
time) is reported to the user as a finding. Run only via `node tools/run-cpu-tests.js vorago_tests`, alone.

### 4.4 Portability rules for every test TU

- `static_assert`s and casts: no narrowing in brace init; `Steinberg::int32`/`std::size_t` conversions
  explicit.
- Non-finite payloads from bit patterns through `volatile std::uint32_t` + `std::memcpy`
  (`reference_fastmath_nan_in_tests`), in TUs listed in §5.2's `-fno-fast-math` block.
- Temp paths via `std::filesystem::temp_directory_path()`, never hard-coded; cleaned up.
- No platform headers; VSTGUI only through the harness helpers.

---

## 5. Build integration

### 5.1 `plugins/vorago/CMakeLists.txt` (FR-001 – FR-006)

Mirror of `plugins/seraphis/CMakeLists.txt` minus Seraphis-only sources:

```cmake
cmake_minimum_required(VERSION 3.20)
krate_plugin_read_version(VORAGO)
krate_plugin_configure_generated_files()
set(PLUGIN_NAME Vorago)
smtg_add_vst3plugin(${PLUGIN_NAME}
    src/entry.cpp            src/plugin_ids.h          src/version.h
    src/processor/processor.h  src/processor/processor.cpp
    src/controller/controller.h src/controller/controller.cpp
    src/parameters/global_params.h src/parameters/macro_params.h
    src/engine/vorago_engine_config.h
    src/preset/vorago_preset_config.h
    src/update/vorago_update_config.h)
target_link_libraries(${PLUGIN_NAME} PRIVATE sdk vstgui_support KrateDSP KratePluginsShared)
target_include_directories(${PLUGIN_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
smtg_target_configure_version_file(${PLUGIN_NAME})
krate_plugin_platform_setup(${PLUGIN_NAME} TAG VORAGO BUNDLE_BASE com.krateaudio.vorago
                            ENTITLEMENTS Vorago.entitlements KIND instrument)
smtg_target_add_plugin_resources(${PLUGIN_NAME} RESOURCES resources/editor.uidesc)
krate_plugin_install_to_system(${PLUGIN_NAME})
krate_plugin_install_presets(${PLUGIN_NAME})
krate_plugin_set_warnings(${PLUGIN_NAME})
# C4459 block copied from plugins/seraphis/CMakeLists.txt:120-135, pointing at
# dsp/include/krate/dsp/systems/timevar_comb_bank.h:931 (the local kPi) reached via
# vorago_voice.h:137 -> continuous_body.h. Delete when the dsp/ shadow is removed.
if(MSVC)
    target_compile_options(${PLUGIN_NAME} PRIVATE /wd4459)
endif()
if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
```

Exact argument spellings are copied from the Seraphis file at implementation time (the model lines are
`:10–11, :18–82, :85, :90–95, :100–103, :108, :113, :118, :133–135, :140–142`); the block above fixes
the content, not the whitespace. The Seraphis comment at `:120–132` cites `timevar_comb_bank.h:915`;
the line is `:931` today — the Vorago comment uses `:931`.

### 5.2 `plugins/vorago/tests/CMakeLists.txt` (FR-060 – FR-065)

```cmake
add_executable(vorago_tests
    unit/test_main.cpp
    unit/processor_bus_test.cpp unit/param_denorm_test.cpp unit/state_roundtrip_test.cpp
    unit/midi_event_test.cpp unit/lifecycle_test.cpp
    unit/controller/editor_lifecycle_test.cpp
    integration/processor_audio_test.cpp integration/param_flow_test.cpp
    integration/processor_cpu_test.cpp
    # SECOND compilation of every plugin .cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    vstgui_test_stubs.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp)
target_link_libraries(vorago_tests PRIVATE
    KrateDSP KratePluginsShared Catch2::Catch2 test_helpers vstgui_support sdk)   # sdk AFTER vstgui_support
target_include_directories(vorago_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tests ${vst3sdk_SOURCE_DIR} ${vst3sdk_SOURCE_DIR}/vstgui4)
target_compile_features(vorago_tests PRIVATE cxx_std_20)
target_compile_definitions(vorago_tests PRIVATE
    VORAGO_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources"
    VORAGO_PERF_BUDGET_HEADER="${CMAKE_SOURCE_DIR}/dsp/tests/unit/systems/vorago_perf_budget.h")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/state_roundtrip_test.cpp      # SC-010 corrupt-stream arm injects +Inf
        unit/lifecycle_test.cpp            # bit-pattern finiteness over renders
        integration/processor_audio_test.cpp
        integration/param_flow_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only")
    # processor_cpu_test.cpp deliberately ABSENT: the flag would move the timed code.
endif()
if(MSVC)   # only if the target emits C4459 (FR-006); same comment as §5.1
    target_compile_options(vorago_tests PRIVATE /wd4459)
endif()
catch_discover_tests(vorago_tests REPORTER console)
```

`processor_cpu_test.cpp` includes the Phase 10 budget header as `#include VORAGO_PERF_BUDGET_HEADER`
(a macro-expanded string-literal include, standard C++), so it reuses `kReferenceNs` without adding the
whole `dsp/tests/unit/systems` directory to the include path. That header includes only `<cstddef>` and
opens `Krate::DSP::TestUtils::Vorago` (`vorago_perf_budget.h:59, :61`); the TU therefore names plugin
types as `::Vorago::…` (§4.2). The `/wd4459` line is added only if the first MSVC build of
`vorago_tests` actually shows C4459 (Seraphis's test target needs none); SC-001 decides.

`vstgui_test_stubs.cpp`: copy of Seraphis's (`GetPluginFactory()` returning `nullptr`).

### 5.3 Root `CMakeLists.txt` (FR-070)

`add_subdirectory(plugins/vorago)` after `add_subdirectory(plugins/seraphis)` (`CMakeLists.txt:494`).

### 5.4 `.gitignore` (FR-007)

Three lines after `.gitignore:76–78`: `/plugins/vorago/resources/win32resource.rc`,
`/plugins/vorago/src/version.h`, `/plugins/vorago/resources/auv3/audiounitconfig.h`.

### 5.5 External rosters (FR-071 – FR-081)

| Site | Edit | Checked by |
|---|---|---|
| `.github/workflows/ci.yml` lint-checked sites | detect-changes output, paths-filter, `for p in`, `$GITHUB_OUTPUT` echo, three `hashFiles` keys, nine `for plugin_info in \` blocks + `case` arms (macOS entry `Vorago_AU:Vorago_AUV3`) | `lint-plugin-roster.js` |
| `ci.yml` non-lint sites | Windows artifact upload (model `:474–479`), macOS `auval -v aumu Vrgo KrAt` step (model `:764–773`), AUv3 bundle verification (`:824–830`), macOS artifact upload with `.vst3`/`.component`/`AUv3.app` (`:903–911`), Linux artifact upload (`:1176–1181`) | SC-025.2 greps |
| `.github/workflows/release.yml` | `- vorago` in the choice list (`:41`), `'plugins/vorago/CMakeLists.txt'` in `hashFiles` (`:138`) | lint |
| `.github/workflows/valgrind-nightly.yml` | `vorago_tests` in the build list (`:276`) and the `for bin in` list (`:283`) | lint |
| `tools/run-clang-tidy.ps1` | `"vorago"` in `ValidateSet` (`:60`), a `"vorago"` case with `plugins/vorago/src` + `plugins/vorago/tests`, both dirs in `all` | lint; SC-016 run |
| `tools/run-clang-tidy.sh` | `vorago)` case, `all)` paths, usage text (`:63`) | lint |
| `tools/check-changelog-coverage.js` | `'vorago'` in `PLUGINS` (`:50`) | lint |
| `tools/gen-specs-index.js` | `['vorago', 'Vorago']` in `SUBSYSTEMS` **before** `spectral` (with a comment like the Seraphis one at `:20–22`) | SC-025.4, `--check` |
| `tools/run-cpu-tests.js` | `'vorago_tests'` after `'seraphis_tests'` in `DEFAULT_TARGETS` (`:45–50`) | SC-025.6 |
| root `CLAUDE.md` | every roster (FR-079's eight sites) | SC-025.5 |
| `.github/workflows/docs.yml` | **no edit** (FR-080) | — |

Each `ci.yml` edit is a copy of its Seraphis twin with `seraphis`→`vorago`, `Seraphis`→`Vorago`,
`Srph`→`Vrgo`. After editing, `grep -ci vorago ci.yml` is compared with `grep -ci seraphis ci.yml`
(51 today) and any difference explained in `compliance.md`.

### 5.6 Regenerated artifacts (FR-078)

`node tools/gen-repo-map.js` (auto-discovers `plugins/vorago`) and `node tools/gen-specs-index.js`
(re-files all `vorago-*` slugs under "Vorago"), both committed. `gen-symbols.js` scans `dsp/include`
only and must report no change (`--check`).

### 5.7 Targets to build and run (Windows; full CMake path per root `CLAUDE.md`)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --preset windows-x64-release                       # new subdirectory -> reconfigure
"$CMAKE" --build build/windows-x64-release --config Release --target Vorago vorago_tests 2>&1 | tee <log>
grep -cE "warning C[0-9]|warning:" <log>                    # SC-001: must print 0
build/windows-x64-release/bin/Release/vorago_tests.exe --list-tests          # SC-002.1
build/windows-x64-release/bin/Release/vorago_tests.exe "[.perf]" --list-tests
build/windows-x64-release/bin/Release/vorago_tests.exe 2>&1 | tail -5       # SC-002.2
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Vorago.vst3"   # SC-003
node tools/check-bundle.js build/windows-x64-release/VST3/Release/Vorago.vst3                             # SC-018
node tools/check-portability.js                                                                         # SC-015
./tools/run-clang-tidy.ps1 -Target vorago -BuildDir build/windows-ninja                                 # SC-016
node tools/gen-repo-map.js --check; node tools/gen-specs-index.js --check; node tools/gen-symbols.js --check
# + the nine lints of tools/hooks/guard-ci-gates.js:38-47 (SC-017), incl. lint-plugin-roster.js (SC-025.1)
# Content FRs with no test case; each result cited in compliance.md:
node tools/check-changelog-coverage.js vorago                # FR-010: exit 0 (## [0.1.0] entry present)
node -e "const v=require('./plugins/vorago/version.json');const k=Object.keys(v).sort().join(',');
  if(k!=='copyright,description,name,publisher,url,version'||v.version!=='0.1.0'||v.name!=='Vorago'
  ||v.publisher!=='Krate Audio'||v.url!=='https://krateaudio.com/vorago/'){console.error(k);process.exit(1)}"  # FR-002: exit 0
grep -c "OQ-7" plugins/vorago/CLAUDE.md; grep -ciE "host-cache|host cache" plugins/vorago/CLAUDE.md     # FR-009.1: each >= 1
grep -c "Drones" plugins/vorago/CLAUDE.md; grep -ciE "grow only|only grow|never rename" plugins/vorago/CLAUDE.md   # FR-009.2: each >= 1
grep -ciE "soft-limit|soft limit" plugins/vorago/CLAUDE.md                                              # FR-009.3: >= 1
grep -c "37.99" plugins/vorago/CLAUDE.md                                                                 # FR-009.4: >= 1
grep -c "CC64" plugins/vorago/CLAUDE.md                                                                  # FR-009.5: >= 1
grep -o 'class="[^"]*"' plugins/vorago/resources/editor.uidesc | sort -u \
  | grep -vE 'class="(CViewContainer|CSlider|COptionMenu|CTextLabel)"'                                 # FR-054: prints nothing
git status --porcelain plugins/vorago                                                                   # SC-020: empty
git diff --stat <phase-base>..HEAD -- dsp/ plugins/seraphis/ plugins/shared/                            # SC-027: exactly resonator_bank.h (B-2)
node tools/run-cpu-tests.js vorago_tests                    # SC-014, ALONE, nothing else running
# SC-012.5: ASan Debug build (CMakeLists.txt:112 ENABLE_ASAN), separate dir:
"$CMAKE" -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON
"$CMAKE" --build build-asan --config Debug --target vorago_tests
build-asan/bin/Debug/vorago_tests.exe "[lifecycle]"
```

SC-004 (`auval`) and the Linux/macOS legs of SC-001 are verified by CI; locally the
`check-portability.js` gate is the stand-in (memory: *"Green Windows build proves nothing for
Linux/macOS"*). B-2: `dsp_processors_tests`, `dsp_systems_tests` and `seraphis_tests` run once in
the closure because `resonator_bank.h` lost one dead line; every other DSP suite needs no run.

---

## 6. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R1 | Polyphony "reset to default" lands on 1 voice (P-1) | Inline `defaultNormalizedValue` pin; SC-009 default clause detects it |
| R2 | Unsorted host event lists fire late notes early/late (P-3) | §3.2 stable sort; SC-022.8; overflow path SC-022.9 |
| R3 | Equality criteria vacuous because the drone is still near-silent (20 s attack + 3072 latency) (P-6) | Peak `>= 1e-4` preconditions; lengthen scripts, never thresholds |
| R4 | Macro / cavern push breaks partition invariance, or its cost explodes on event-dense blocks | Pushed once per `process()`, not per slice (P-10); forwarders are idempotent by contract (`vorago_macro_matrix.h:45–56`, `VoragoMacro_ApplyIsIdempotent`), so the block-to-block repeat steps nothing; SC-008 (1) measures invariance at `{1…4096}`; SC-014 arm E records the dense-event cost |
| R5 | Cross-TU `/fp:fast` contraction makes processor-vs-direct renders differ (P-8) | No such equality test; all comparisons go through one compiled path |
| R6 | FR-067a's 1.05× gate is noise-limited | Interleaved best-of-16 × 100 blocks, isolated lane, 20 s settle (`run-cpu-tests.js`); a flip on re-run alone is a machine finding, not a code finding (root `CLAUDE.md`); never relax the 1.05 |
| R7 | Heap/stack: `VoragoEngine` ~808 KB on the stack overflows MSVC's 1 MiB main stack | FR-022 `unique_ptr`, FR-064 heap fixture, `static_assert(sizeof(Processor) < 64 KiB)` |
| R8 | Denormals in the long tails (45 s release, 20 s cavern) | `ScopedDenormalMode` per `process()` (per-thread); `OnePoleSmoother` flushes itself (`smoother.h:208`); test main `enableFTZDAZ()` |
| R9 | Non-finite state from a corrupt stream poisons the gain multiply or spins the polyphony detector | `detail::isFinite` + clamp on load; `clampPolyphony` single conversion; SC-010 corrupt-stream arm |
| R10 | `std::isnan` under `-ffast-math` | Only `detail::isFinite` (bit pattern); `-fno-fast-math` on NaN-injecting TUs; `lint-nonfinite-symbols.js` |
| R11 | Narrowing in brace init on Clang (`EventSlot{offset, i}` with `size_t`) | Explicit `static_cast<std::int32_t>`; `check-portability.js` |
| R12 | A roster site the lint does not check is missed (artifact uploads, auval, AUv3, `.gitignore`, specs index, `run-cpu-tests.js`, `CLAUDE.md`) | SC-025 greps, one per site; §5.5 table is the checklist |
| R13 | 192 kHz prepare allocates ~29 MB of atmosphere ring and runs slower than real time under pluginval | Allowed (pluginval asserts correctness, not speed); allocation is in `setupProcessing` only |
| R14 | SC-006 / SC-008 wall time in the per-push lane | Recorded; SC-006 cannot be `[long]` (bounded test); SC-008's partition set is never trimmed |
| R15 | macOS AU `-10875` from bus / plist / config disagreement | FR-015/016 copies with substitutions only; SC-004 in CI |
| R16 | Layer discipline: plugin including L4 `cavern_verb.h` | Plugins may include any layer; `lint-layers.js` covers `dsp/` only; no `dsp/` file changes |

---

## 7. Implementation order

1. `plugin_ids.h` (FUIDs generated + swept), `version.json`, `CHANGELOG.md`, resources (AU files,
   entitlements, placeholder uidesc, `Drones/.gitkeep`), `.gitignore`, `CMakeLists.txt`, root
   `add_subdirectory`, `tests/CMakeLists.txt`, `test_main.cpp`, stubs, fixture. → configure + build
   green with empty test cases.
2. Write the nine `TEST_CASE`s (failing), per §4.3.
3. `global_params.h`, `macro_params.h`, `vorago_engine_config.h`, preset/update configs, `entry.cpp`.
4. `Processor` (§2.5), then `Controller` (§2.6). → build, zero warnings, all non-perf cases green.
5. pluginval, check-bundle, check-portability, clang-tidy (after step 6 adds the target).
6. External rosters (§5.5), regenerated artifacts (§5.6), root `CLAUDE.md`, leaf `CLAUDE.md`, README,
   installers. → all nine lints + three generator checks.
7. ASan lifecycle run; isolated CPU run; record SC-005/006/013/014 figures in `compliance.md`.

---

## 8. Decisions left open, spec amendments, residuals

### 8.1 Left open

None. Every choice the spec left to the plan is pinned above.

### 8.2 Spec amendments this plan requires (edit `spec.md` before the compliance table)

1. **FR-048 / P-1:** after `createDropdownParameterWithDefault`, set
   `getInfo().defaultNormalizedValue = 3.0 / 5.0` before `addParameter`.
2. **SC-019.1 / P-2:** delete *"possible only because of the first-block snap"*; add the gain-0.5
   non-vacuity arm and the `masterGainValueForTest() == 0.0f` snap arm; add the seam to FR-024a.2.
3. **FR-025 / P-3:** name the fixed-capacity stable sort and the >1024 overflow rule; SC-022 gains
   clause 9.
4. **SC-012.3 / P-4:** "factory override → temp dir with `Drones/Probe.vstpreset`; user override → a
   separate empty temp dir".
5. **Edge cases, Seed determinism / P-5:** replace *"It does not rewind the slots' life/ecosystem
   trajectories"* with *"Whether it rewinds them is not asserted"* (`silence()` calls
   `voices_[v].reset()`, `vorago_engine.h:447–456`).
6. **SC-008 (1), SC-023, SC-026 / P-6:** add the `>= 1e-4` reference-peak precondition. For SC-026:
   the hold becomes 8 s at velocity 127; the peak over the last 1 s before `setActive(false)` must be
   `>= 1e-4`, and a negative-control render without the `setActive(false/true)` pair must show a
   following-1 s peak `>= 1e-4` (replaces the *"last 0.5 s peak > 0"* pre-check).
7. **FR-066 table:** SC-008 (2) lives in `Vorago_ParamFlowReachesEngine`.
8. **SC-009:** the round-trip arm runs through `process()` on an unprepared processor with
   `numOutputs = 0` and reads `globalParamsForTest()` / `macroParamsForTest()` (name the seams). Add
   the unregistered in-band sub-arm (IDs 150 and 199 leave all twelve macros and both globals
   unchanged), and state in FR-043 that `handleMacroParamChange` ignores `id > kMacroMassId`.
9. **FR-024 step 0 / P-9:** reword to *"push the global parameters (FR-024a) once per `process()`,
   before the first slice"*.
10. **FR-024 step 2 / P-10:** reword to *"once per `process()`, after step 0 and before the first
    slice: `macros_.apply(*engine_)` and `applyCavernTargets(...)`"*, with the equivalence argument, and
    add the Phase 12 rule *"per block, never per slice"*. Rewrite SC-022 clause 9 per §4.3 (the
    overflow segment carries NoteOn(62) and an out-of-order NoteOn(64)). SC-014 gains the
    bare-`process()` timing rule for arm P and the `WARN`-recorded event-dense arm E.
11. **FR-066 table / FR-029:** `Vorago_ProcessorLifecycle` covers FR-029's no-allocation half only;
    the `ScopedDenormalMode` clause is marked *"verified by inspection (grep of processor.cpp)"*.
12. **FR-002 / FR-009 / FR-010 / FR-054:** name the §5.7 content checks as their verification.

### 8.3 Residuals, recorded rather than hidden

- `getTailSamples() == kNoTail` (Clarification Q6): offline bounce may truncate the 45 s release +
  20 s cavern decay. Phase 14 revisits.
- Two instances render identically (fixed seeds, Clarification SEED). Phase 12 adds the seed parameter.
- Phase 12 must keep the macro + cavern-target push **once per `process()`, never per slice** (P-10);
  the leaf `CLAUDE.md` records it. Arm E's event-dense figure is the baseline Phase 12 compares against.
- The Seraphis `CMakeLists.txt` C4459 comment cites `timevar_comb_bank.h:915`; the line is `:931`. Not
  fixed here (no Seraphis edits, SC-027); mentioned for whoever removes the shadow.
- First valgrind nightly run of `Vorago_EditorLifecycle` is a follow-up in `compliance.md`, not a gate
  (SC-012.5).
