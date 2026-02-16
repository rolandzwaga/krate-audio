# Implementation Plan: Settings Drawer

**Branch**: `058-settings-drawer` | **Date**: 2026-02-16 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/058-settings-drawer/spec.md`

## Summary

Expose 6 global synthesizer settings (pitch bend range, velocity curve, tuning reference, voice allocation mode, voice steal mode, gain compensation) as VST parameters with IDs 2200-2205. Add a settings drawer UI panel that slides in from the right edge of the 925x880 window when the existing gear icon is clicked. The drawer contains knobs for continuous parameters, dropdowns for discrete parameters, and a toggle for gain compensation. All 6 DSP engine methods already exist and are tested -- no DSP work needed. State version bumps from 13 to 14 with backward-compatible defaults for old presets. The hardcoded `setGainCompensationEnabled(false)` is removed and replaced by the parameter-driven value.

## Technical Context

**Language/Version**: C++20 (MSVC 2022, Clang, GCC)
**Primary Dependencies**: Steinberg VST3 SDK 3.7.x, VSTGUI 4.12+
**Storage**: Binary state stream (IBStreamer) for preset persistence
**Testing**: Catch2 (dsp_tests, ruinae_tests), pluginval strictness 5
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo -- shared DSP library + plugin
**Performance Goals**: Total plugin < 5% single core @ 44.1kHz stereo; settings params are "set and forget" with no per-sample processing impact beyond what the engine already does
**Constraints**: Zero allocations on audio thread; all buffers pre-allocated; cross-platform UI only (VSTGUI)
**Scale/Scope**: 6 new parameters, 1 new param file, processor wiring, controller registration, drawer UI with animation, state version 13 to 14

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check:**

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Processor handles param changes + engine forwarding; Controller handles registration + display formatting + UI drawer logic. No cross-inclusion. |
| II. Real-Time Audio Thread Safety | PASS | All param stores use `std::atomic` with `memory_order_relaxed`. Settings values forwarded to engine in `applyParamsToEngine()` (same pattern as all other params). No allocations in any new code. |
| III. Modern C++ Standards | PASS | Using `std::atomic`, `constexpr`, `std::clamp`, `static_cast`. No raw new/delete in new code. |
| V. VSTGUI Development | PASS | All controls via UIDescription XML (ArcKnob, COptionMenu, ToggleButton, CTextLabel). Drawer animation via CVSTGUITimer. All cross-platform. |
| VI. Cross-Platform Compatibility | PASS | No platform-specific code. VSTGUI abstractions only. CVSTGUITimer for animation. |
| VIII. Testing Discipline | PASS | Tests written before implementation. All existing tests must pass. |
| IX. Layered DSP Architecture | N/A | No new DSP components. All 6 engine methods already exist and are tested. |
| XII. Debugging Discipline | PASS | No framework pivots expected. |
| XIII. Test-First Development | PASS | Failing tests first for each task group. |
| XIV. Living Architecture Documentation | PASS | Architecture docs updated as final task. |
| XV. Pre-Implementation Research (ODR) | PASS | See Codebase Research section below. |
| XVI. Honest Completion | PASS | Compliance table filled only after code/test verification. |

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Result | Action |
|--------------|---------------|--------|
| `SettingsParams` | Not found in codebase | Create New in `plugins/ruinae/src/parameters/settings_params.h` |

**Utility Functions to be created**:

| Planned Function | Search Result | Action |
|------------------|---------------|--------|
| `registerSettingsParams()` | Not found | Create New in `settings_params.h` |
| `handleSettingsParamChange()` | Not found | Create New in `settings_params.h` |
| `formatSettingsParam()` | Not found | Create New in `settings_params.h` |
| `saveSettingsParams()` | Not found | Create New in `settings_params.h` |
| `loadSettingsParams()` | Not found | Create New in `settings_params.h` |
| `loadSettingsParamsToController()` | Not found | Create New in `settings_params.h` |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `setGainCompensationEnabled()` | `ruinae_engine.h:569-574` | Plugin | Called from `applyParamsToEngine()` driven by param value instead of hardcoded `false` |
| `setPitchBendRange()` | `ruinae_engine.h:1205-1207` | Plugin | Called from `applyParamsToEngine()` with denormalized semitone value |
| `setVelocityCurve()` | `ruinae_engine.h:1215-1216` | Plugin | Called from `applyParamsToEngine()` with cast `VelocityCurve` enum |
| `setTuningReference()` | `ruinae_engine.h:1210-1212` | Plugin | Called from `applyParamsToEngine()` with denormalized Hz value |
| `setAllocationMode()` | `ruinae_engine.h:1193-1194` | Plugin | Called from `applyParamsToEngine()` with cast `AllocationMode` enum |
| `setStealMode()` | `ruinae_engine.h:1197-1198` | Plugin | Called from `applyParamsToEngine()` with cast `StealMode` enum |
| `VelocityCurve` enum | `dsp/include/krate/dsp/core/midi_utils.h:122-127` | 0 | Linear=0, Soft=1, Hard=2, Fixed=3 |
| `AllocationMode` enum | `dsp/include/krate/dsp/systems/voice_allocator.h:55-60` | 3 | RoundRobin=0, Oldest=1, LowestVelocity=2, HighestNote=3 |
| `StealMode` enum | `dsp/include/krate/dsp/systems/voice_allocator.h:67-70` | 3 | Hard=0, Soft=1 |
| `createDropdownParameter()` | `controller/parameter_helpers.h:23-41` | Plugin | For Velocity Curve, Voice Allocation, Voice Steal dropdowns |
| `createDropdownParameterWithDefault()` | `controller/parameter_helpers.h:47-70` | Plugin | For Voice Allocation (default=1, Oldest) |
| `CVSTGUITimer` | `vstgui/lib/cvstguitimer.h` | VSTGUI | Timer-driven drawer animation (~60fps, 16ms interval) |
| Gear icon ToggleButton | `editor.uidesc:2813-2821` | UI | Currently inert; will get a control-tag to trigger drawer toggle |
| `mono_mode_params.h` | `plugins/ruinae/src/parameters/` | Plugin | Reference pattern for param file with register/handle/format/save/load |
| `global_filter_params.h` | `plugins/ruinae/src/parameters/` | Plugin | Reference pattern for RangeParameter registration |

### Files Checked for Conflicts

- [x] `plugins/ruinae/src/parameters/` - All 22 existing param files checked, no `settings_params` exists
- [x] `plugins/ruinae/src/plugin_ids.h` - No IDs in 2200-2299 range exist, `kNumParameters = 2200`
- [x] `plugins/ruinae/src/processor/processor.h` - Fields end with `runglerParams_` (line 172), ready for new field
- [x] `plugins/ruinae/src/processor/processor.cpp` - `kCurrentStateVersion = 13`, hardcoded `setGainCompensationEnabled(false)` at line 117
- [x] `plugins/ruinae/resources/editor.uidesc` - No settings control-tags, no drawer container
- [x] `specs/_architecture_/` - No conflicts with planned types

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: One new struct (`SettingsParams`) with a unique name not found anywhere in the codebase. All new functions follow the established naming pattern (`register*Params`, `handle*ParamChange`, etc.) with unique prefix (`Settings`). No new DSP classes created. The drawer UI is all in the controller and uidesc.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `RuinaeEngine` | `setPitchBendRange` | `void setPitchBendRange(float semitones) noexcept` (line 1205) | Yes |
| `RuinaeEngine` | `setVelocityCurve` | `void setVelocityCurve(VelocityCurve curve) noexcept` (line 1215) | Yes |
| `RuinaeEngine` | `setTuningReference` | `void setTuningReference(float a4Hz) noexcept` (line 1210) | Yes |
| `RuinaeEngine` | `setAllocationMode` | `void setAllocationMode(AllocationMode mode) noexcept` (line 1193) | Yes |
| `RuinaeEngine` | `setStealMode` | `void setStealMode(StealMode mode) noexcept` (line 1197) | Yes |
| `RuinaeEngine` | `setGainCompensationEnabled` | `void setGainCompensationEnabled(bool enabled) noexcept` (line 569) | Yes |
| `VelocityCurve` | enum values | `Linear=0, Soft=1, Hard=2, Fixed=3` (midi_utils.h:122-127) | Yes |
| `AllocationMode` | enum values | `RoundRobin=0, Oldest=1, LowestVelocity=2, HighestNote=3` (voice_allocator.h:55-60) | Yes |
| `StealMode` | enum values | `Hard=0, Soft=1` (voice_allocator.h:67-70) | Yes |

### Header Files Read

- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - All 6 setter methods verified (lines 569, 1193, 1197, 1205, 1210, 1215)
- [x] `dsp/include/krate/dsp/core/midi_utils.h` - VelocityCurve enum (lines 122-127)
- [x] `dsp/include/krate/dsp/systems/voice_allocator.h` - AllocationMode (55-60), StealMode (67-70)
- [x] `plugins/ruinae/src/parameters/mono_mode_params.h` - Full param file pattern
- [x] `plugins/ruinae/src/parameters/global_filter_params.h` - RangeParameter/dropdown pattern
- [x] `plugins/ruinae/src/controller/parameter_helpers.h` - createDropdownParameter, createDropdownParameterWithDefault
- [x] `plugins/ruinae/src/plugin_ids.h` - Current enum range, kNumParameters=2200
- [x] `plugins/ruinae/src/processor/processor.h` - Processor fields (line 147-172), kCurrentStateVersion=13 (line 69)
- [x] `plugins/ruinae/src/processor/processor.cpp` - processParameterChanges (565-647), applyParamsToEngine (653-993), getState (354-409), setState (412-559)
- [x] `plugins/ruinae/src/controller/controller.cpp` - initialize (115-117), setComponentState (147-296), getParamStringByValue (323-381), valueChanged (984-1033), verifyView (687-956), didOpen/willClose (636-683)
- [x] `plugins/ruinae/src/controller/controller.h` - Controller fields (196-274)
- [x] `plugins/ruinae/resources/editor.uidesc` - Control-tags (62-225), gear icon (2813-2821), colors (4-53), window size (925x880, line 1815-1817)
- [x] `plugins/shared/src/ui/toggle_button.h` - ToggleButton class, onMouseDown, drawGearIcon

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Window size | Spec says 900x866 but actual is 925x880 | Use 925x880 for all drawer calculations |
| Pitch Bend Range | 0-24 semitones, integer steps; normalized default = 2/24 = 0.0833 | `stepCount=24`, default `2.0/24.0` |
| Tuning Reference | 400-480 Hz range; normalized default = (440-400)/80 = 0.5 | Linear mapping: `400 + normalized * 80` |
| Voice Allocation Mode | Default is Oldest (index 1), NOT first item | Use `createDropdownParameterWithDefault()` with defaultIndex=1 |
| Gain Compensation | Default ON for new presets, OFF for old presets | New param default=1.0; backward compat default=0 (off) |
| Hardcoded gain comp | `processor.cpp:117` -- `engine_.setGainCompensationEnabled(false)` | MUST be removed; gain comp now driven by parameter |
| State version | Currently 13 (from spec 057 macros/rungler) | Bump to 14; new settings data appended at END of stream |
| Gear icon currently inert | No control-tag, no control-listener registration | Need to add a UI-action tag (kActionSettingsToggleTag) and register as control-listener |
| `kNumParameters` sentinel | Currently 2200, must become 2300 | Settings range is 2200-2299 |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Pitch bend range normalization (linear 0-24) | Simple inline formula in settings_params.h, only used there |
| Tuning reference normalization (linear 400-480) | Simple inline formula in settings_params.h, only used there |

**Decision**: No Layer 0 extractions needed. All new code is plugin-level parameter plumbing (param struct, handle/register/format/save/load functions) and UI drawer logic. No new DSP code whatsoever.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | N/A | No new DSP processing; settings are configuration parameters |
| **Data parallelism width** | N/A | Parameters are scalar values forwarded to engine |
| **Branch density in inner loop** | N/A | No inner loop; parameters applied once per block |
| **Dominant operations** | N/A | Simple denormalization arithmetic |
| **Current CPU budget vs expected usage** | 0% additional | Settings params are applied in `applyParamsToEngine()`, adding 6 `engine_.set*()` calls to an existing function with hundreds of such calls |

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This spec adds no DSP processing. It exposes 6 existing engine configuration methods as automatable VST parameters and adds a UI drawer. The parameter handling is 6 additional lines in `applyParamsToEngine()`. SIMD analysis is not relevant.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Plugin parameter layer (param file, processor wiring, controller, uidesc) + UI infrastructure (drawer)

**Related features at same layer** (from Roadmap):
- Phase 6 (Additional Mod Sources) - will add more param files but no drawers
- No other slide-out drawers are planned

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `settings_params.h` pattern | HIGH | All future param files | Keep local, use as template |
| Drawer animation infrastructure | LOW | No other drawers planned | Keep local in controller |
| Gear icon toggle pattern | LOW | Only one gear icon | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared drawer component | Only one drawer is planned; over-engineering for reuse is not warranted |
| Keep animation logic in Controller | Simple timer-based slide, ~30 lines of code, no reuse case |

### Review Trigger

After implementing **Phase 6 (Additional Mod Sources)**, review this section:
- [ ] Does Phase 6 need the same param file pattern? -> Copy template from settings_params.h
- [ ] Any shared UI infrastructure needed? -> Unlikely per roadmap

## Project Structure

### Documentation (this feature)

```text
specs/058-settings-drawer/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- parameter-ids.md # Parameter ID definitions and mappings
+-- tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (files to create/modify)

```text
# NEW FILES
plugins/ruinae/src/parameters/settings_params.h   # SettingsParams struct + register/handle/format/save/load

# MODIFIED FILES - Plugin Layer
plugins/ruinae/src/plugin_ids.h                    # Add settings param IDs 2200-2205, kNumParameters 2200->2300
plugins/ruinae/src/processor/processor.h           # Add SettingsParams field, bump kCurrentStateVersion 13->14
plugins/ruinae/src/processor/processor.cpp         # processParameterChanges, applyParamsToEngine, getState, setState, remove hardcoded gain comp
plugins/ruinae/src/controller/controller.h         # Add settingsDrawer_, settingsAnimTimer_, gearButton_ fields, toggleSettingsDrawer() method
plugins/ruinae/src/controller/controller.cpp       # initialize, setComponentState, getParamStringByValue, valueChanged, verifyView, willClose, drawer animation

# MODIFIED FILES - UI
plugins/ruinae/resources/editor.uidesc             # Control-tags, gear icon tag, settings drawer container with controls, bg-drawer color

# MODIFIED FILES - Tests
plugins/ruinae/tests/                              # Integration tests for settings params and state persistence
```

**Structure Decision**: Monorepo structure -- plugin changes in `plugins/ruinae/src/`, UI resources in `plugins/ruinae/resources/`.

## Detailed Implementation Design

### Task Group 1: Parameter IDs (FR-001, FR-002)

**Files modified**: `plugin_ids.h`

#### 1a. Add Settings parameter IDs to `plugin_ids.h`

Insert after `kRunglerEndId = 2199` and before `kNumParameters`:

```cpp
// ==========================================================================
// Settings Parameters (2200-2299)
// ==========================================================================
kSettingsBaseId = 2200,
kSettingsPitchBendRangeId = 2200, // Pitch bend range [0, 24] semitones (default 2)
kSettingsVelocityCurveId = 2201,  // Velocity curve (4 options: Linear/Soft/Hard/Fixed, default 0 = Linear)
kSettingsTuningReferenceId = 2202, // A4 tuning reference [400, 480] Hz (default 440)
kSettingsVoiceAllocModeId = 2203, // Voice allocation (4 options: RR/Oldest/LowVel/HighNote, default 1 = Oldest)
kSettingsVoiceStealModeId = 2204, // Voice steal (2 options: Hard/Soft, default 0 = Hard)
kSettingsGainCompensationId = 2205, // Gain compensation on/off (default 1 = enabled for new presets)
kSettingsEndId = 2299,

// ==========================================================================
kNumParameters = 2300,
```

#### 1b. Update the ID range allocation comment

Add at the top of the enum comment block:
```
//   2200-2299: Settings (Pitch Bend Range, Velocity Curve, Tuning Ref, Alloc Mode, Steal Mode, Gain Comp)
```

#### 1c. Add Settings gear toggle UI action tag

After `kModSourceViewModeTag = 10019`:
```cpp
// Settings Drawer Toggle (UI-only, gear icon click)
kActionSettingsToggleTag = 10020,
```

### Task Group 2: Parameter File (FR-003)

**File created**: `plugins/ruinae/src/parameters/settings_params.h`

Following the established pattern from `mono_mode_params.h` and `global_filter_params.h`:

#### 2a. `SettingsParams` struct

```cpp
struct SettingsParams {
    std::atomic<float> pitchBendRangeSemitones{2.0f};  // 0-24 semitones
    std::atomic<int> velocityCurve{0};                  // VelocityCurve index (0-3)
    std::atomic<float> tuningReferenceHz{440.0f};       // 400-480 Hz
    std::atomic<int> voiceAllocMode{1};                 // AllocationMode index (0-3), default=Oldest(1)
    std::atomic<int> voiceStealMode{0};                 // StealMode index (0-1), default=Hard(0)
    std::atomic<bool> gainCompensation{true};           // default=ON for new presets
};
```

#### 2b. `handleSettingsParamChange()`

```cpp
inline void handleSettingsParamChange(
    SettingsParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kSettingsPitchBendRangeId:
            // Linear: 0-1 -> 0-24 semitones, integer steps (stepCount=24)
            params.pitchBendRangeSemitones.store(
                std::clamp(static_cast<float>(std::round(value * 24.0)), 0.0f, 24.0f),
                std::memory_order_relaxed); break;
        case kSettingsVelocityCurveId:
            params.velocityCurve.store(
                std::clamp(static_cast<int>(value * 3.0 + 0.5), 0, 3),
                std::memory_order_relaxed); break;
        case kSettingsTuningReferenceId:
            // Linear: 0-1 -> 400-480 Hz
            params.tuningReferenceHz.store(
                std::clamp(400.0f + static_cast<float>(value) * 80.0f, 400.0f, 480.0f),
                std::memory_order_relaxed); break;
        case kSettingsVoiceAllocModeId:
            params.voiceAllocMode.store(
                std::clamp(static_cast<int>(value * 3.0 + 0.5), 0, 3),
                std::memory_order_relaxed); break;
        case kSettingsVoiceStealModeId:
            params.voiceStealMode.store(
                std::clamp(static_cast<int>(value * 1.0 + 0.5), 0, 1),
                std::memory_order_relaxed); break;
        case kSettingsGainCompensationId:
            params.gainCompensation.store(value >= 0.5, std::memory_order_relaxed); break;
        default: break;
    }
}
```

#### 2c. `registerSettingsParams()`

```cpp
inline void registerSettingsParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // Pitch Bend Range: 0-24 semitones, integer steps, default 2
    parameters.addParameter(STR16("Pitch Bend Range"), STR16("st"), 24,
        2.0 / 24.0,  // normalized default for 2 semitones
        ParameterInfo::kCanAutomate, kSettingsPitchBendRangeId);

    // Velocity Curve: 4 options, default Linear (0)
    parameters.addParameter(createDropdownParameter(
        STR16("Velocity Curve"), kSettingsVelocityCurveId,
        {STR16("Linear"), STR16("Soft"), STR16("Hard"), STR16("Fixed")}
    ));

    // Tuning Reference: 400-480 Hz, continuous, default 440 Hz
    parameters.addParameter(STR16("Tuning Reference"), STR16("Hz"), 0,
        0.5,  // normalized default: (440 - 400) / 80 = 0.5
        ParameterInfo::kCanAutomate, kSettingsTuningReferenceId);

    // Voice Allocation: 4 options, default Oldest (1)
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Voice Allocation"), kSettingsVoiceAllocModeId, 1,
        {STR16("Round Robin"), STR16("Oldest"), STR16("Lowest Velocity"), STR16("Highest Note")}
    ));

    // Voice Steal Mode: 2 options, default Hard (0)
    parameters.addParameter(createDropdownParameter(
        STR16("Voice Steal"), kSettingsVoiceStealModeId,
        {STR16("Hard"), STR16("Soft")}
    ));

    // Gain Compensation: on/off, default ON (1.0)
    parameters.addParameter(STR16("Gain Compensation"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kSettingsGainCompensationId);
}
```

#### 2d. `formatSettingsParam()`

```cpp
inline Steinberg::tresult formatSettingsParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kSettingsPitchBendRangeId: {
            int st = static_cast<int>(std::round(value * 24.0));
            char8 text[32];
            snprintf(text, sizeof(text), "%d st", st);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kSettingsTuningReferenceId: {
            float hz = 400.0f + static_cast<float>(value) * 80.0f;
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f Hz", hz);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}
```

Note: Velocity Curve, Voice Allocation, Voice Steal are `StringListParameter` -- they handle their own formatting via `toPlain()`. Gain Compensation is a boolean toggle with stepCount=1 -- the framework handles "On"/"Off" display.

#### 2e. `saveSettingsParams()` and `loadSettingsParams()`

```cpp
inline void saveSettingsParams(const SettingsParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.pitchBendRangeSemitones.load(std::memory_order_relaxed));
    streamer.writeInt32(params.velocityCurve.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tuningReferenceHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.voiceAllocMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.voiceStealMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.gainCompensation.load(std::memory_order_relaxed) ? 1 : 0);
}

inline bool loadSettingsParams(SettingsParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    if (!streamer.readFloat(fv)) { return false; } params.pitchBendRangeSemitones.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.velocityCurve.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.tuningReferenceHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.voiceAllocMode.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.voiceStealMode.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.gainCompensation.store(iv != 0, std::memory_order_relaxed);
    return true;
}
```

#### 2f. `loadSettingsParamsToController()`

```cpp
template<typename SetParamFunc>
inline void loadSettingsParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    // Pitch Bend Range: inverse of (normalized * 24)
    if (streamer.readFloat(fv)) setParam(kSettingsPitchBendRangeId, static_cast<double>(fv) / 24.0);
    // Velocity Curve: index / 3
    if (streamer.readInt32(iv)) setParam(kSettingsVelocityCurveId, static_cast<double>(iv) / 3.0);
    // Tuning Reference: inverse of (400 + normalized * 80)
    if (streamer.readFloat(fv)) setParam(kSettingsTuningReferenceId, static_cast<double>((fv - 400.0f) / 80.0f));
    // Voice Allocation: index / 3
    if (streamer.readInt32(iv)) setParam(kSettingsVoiceAllocModeId, static_cast<double>(iv) / 3.0);
    // Voice Steal: index / 1
    if (streamer.readInt32(iv)) setParam(kSettingsVoiceStealModeId, static_cast<double>(iv));
    // Gain Compensation: bool -> 0.0 or 1.0
    if (streamer.readInt32(iv)) setParam(kSettingsGainCompensationId, iv != 0 ? 1.0 : 0.0);
}
```

### Task Group 3: Processor Wiring (FR-004, FR-005, FR-006, FR-007)

**Files modified**: `processor.h`, `processor.cpp`

#### 3a. Add SettingsParams field to Processor

In `processor.h`, after `runglerParams_` (line 172):
```cpp
SettingsParams settingsParams_;
```

Add include for `settings_params.h`.

Bump `kCurrentStateVersion` from 13 to 14 (line 69):
```cpp
constexpr Steinberg::int32 kCurrentStateVersion = 14;
```

#### 3b. Remove hardcoded gain compensation

Remove line 117 in `processor.cpp`:
```cpp
engine_.setGainCompensationEnabled(false);  // DELETE THIS LINE
```

Gain compensation is now exclusively controlled by the `kSettingsGainCompensationId` parameter. The `SettingsParams` default (`gainCompensation{true}`) means new instances start with gain comp enabled. Old presets (version < 14) will default to off per FR-007.

#### 3c. Extend `processParameterChanges()`

After the rungler block (line 643-644):
```cpp
} else if (paramId >= kSettingsBaseId && paramId <= kSettingsEndId) {
    handleSettingsParamChange(settingsParams_, paramId, value);
}
```

#### 3d. Extend `applyParamsToEngine()`

Add after the Rungler section (around line 984), before `// --- Mono Mode ---`:
```cpp
// --- Settings ---
engine_.setPitchBendRange(settingsParams_.pitchBendRangeSemitones.load(std::memory_order_relaxed));
engine_.setVelocityCurve(static_cast<Krate::DSP::VelocityCurve>(
    settingsParams_.velocityCurve.load(std::memory_order_relaxed)));
engine_.setTuningReference(settingsParams_.tuningReferenceHz.load(std::memory_order_relaxed));
engine_.setAllocationMode(static_cast<Krate::DSP::AllocationMode>(
    settingsParams_.voiceAllocMode.load(std::memory_order_relaxed)));
engine_.setStealMode(static_cast<Krate::DSP::StealMode>(
    settingsParams_.voiceStealMode.load(std::memory_order_relaxed)));
engine_.setGainCompensationEnabled(settingsParams_.gainCompensation.load(std::memory_order_relaxed));
```

#### 3e. Extend `getState()` (state version 14)

After the v13 macro/rungler section (line 407):
```cpp
// v14: Settings params
saveSettingsParams(settingsParams_, streamer);
```

#### 3f. Extend `setState()` with backward compatibility (FR-007)

In the `version >= 3` branch, after the v13 macro/rungler section (line 538):
```cpp
// v14: Settings params
if (version >= 14) {
    loadSettingsParams(settingsParams_, streamer);
} else {
    // Backward compatibility: old presets get these defaults
    // (matching hardcoded behavior before this spec)
    settingsParams_.pitchBendRangeSemitones.store(2.0f, std::memory_order_relaxed);
    settingsParams_.velocityCurve.store(0, std::memory_order_relaxed);    // Linear
    settingsParams_.tuningReferenceHz.store(440.0f, std::memory_order_relaxed);
    settingsParams_.voiceAllocMode.store(1, std::memory_order_relaxed);   // Oldest
    settingsParams_.voiceStealMode.store(0, std::memory_order_relaxed);   // Hard
    settingsParams_.gainCompensation.store(false, std::memory_order_relaxed); // OFF for old presets
}
```

Note: For `version < 3`, the defaults from the struct initializer apply (gain comp = true). But since version < 3 presets are extremely old and the gain comp was always hardcoded false, we should also handle this in the v1 and v2 branches by explicitly setting `settingsParams_.gainCompensation.store(false, ...)` after loading. However, looking at the existing pattern, the struct defaults are used for missing data in older versions. Since gain comp default is `true` in the struct but old presets expect `false`, the explicit backward-compat block above handles v3+ correctly. For v1 and v2 (which also end without reading settings params), add the same backward-compat defaults.

### Task Group 4: Controller Registration and Display (FR-003, FR-008)

**Files modified**: `controller.cpp`

#### 4a. Register params in `Controller::initialize()`

After `registerRunglerParams(parameters);` (line 117):
```cpp
registerSettingsParams(parameters);
```

Add include for `settings_params.h`.

#### 4b. Extend `setComponentState()`

In the `version >= 2` branch, after v13 macro/rungler loading (line 268):
```cpp
// v14: Settings params
if (version >= 14) {
    loadSettingsParamsToController(streamer, setParam);
}
// Note: For version < 14, settings params keep their registration defaults:
// pitchBendRange=2, velocityCurve=Linear, tuning=440, alloc=Oldest, steal=Hard, gainComp=ON
// However, gain compensation default must be OFF for old presets.
// The registration default is ON (1.0), but we override for old presets:
if (version < 14) {
    setParam(kSettingsGainCompensationId, 0.0); // OFF for pre-spec-058 presets
}
```

#### 4c. Extend `getParamStringByValue()`

After the rungler formatting block (line 372):
```cpp
} else if (id >= kSettingsBaseId && id <= kSettingsEndId) {
    result = formatSettingsParam(id, valueNormalized, string);
}
```

### Task Group 5: Settings Drawer UI (FR-008, FR-009, FR-010, FR-011, FR-012, FR-013)

**Files modified**: `editor.uidesc`, `controller.h`, `controller.cpp`

#### 5a. Add control-tags to uidesc (FR-008)

In the control-tags section, after the Rungler tags (line 95):
```xml
<!-- Settings -->
<control-tag name="SettingsPitchBendRange" tag="2200"/>
<control-tag name="SettingsVelocityCurve" tag="2201"/>
<control-tag name="SettingsTuningReference" tag="2202"/>
<control-tag name="SettingsVoiceAllocMode" tag="2203"/>
<control-tag name="SettingsVoiceStealMode" tag="2204"/>
<control-tag name="SettingsGainCompensation" tag="2205"/>
<control-tag name="ActionSettingsToggle" tag="10020"/>
```

#### 5b. Add bg-drawer color

In the colors section, after `bg-display`:
```xml
<color name="bg-drawer" rgba="#111114ff"/>
```

This is approximately 12% darker than `bg-main` (#1A1A1E). The bg-main RGB values are (26, 26, 30). 12% darker: (23, 23, 20) -> #171714. Actually let's compute: 26 * 0.87 = 22.6 -> 23. So #1717 is too dark. Let's use #141416 (about 15% darker). Actually, looking at the hex values: bg-main is #1A1A1E which is (26, 26, 30). 10-15% darker means multiplying by 0.85-0.90: 26*0.88=22.9 -> 23, 30*0.88=26.4 -> 26. So #17171A. Let's use `#131316ff` for clearly darker while still readable.

#### 5c. Activate the gear icon with a control-tag

Modify the gear icon ToggleButton (line 2814) to add the action tag:
```xml
<view class="ToggleButton" origin="92, 14" size="18, 18"
      control-tag="ActionSettingsToggle"
      icon-style="gear"
      on-color="master"
      off-color="text-secondary"
      icon-size="0.65"
      stroke-width="1.5"
      tooltip="Settings"
      transparent="true"/>
```

The key change is adding `control-tag="ActionSettingsToggle"`.

#### 5d. Add settings drawer container to uidesc (FR-009)

Add as the LAST child of the root `editor` template (so it draws on top of everything else due to z-ordering). Position at x=925 (off-screen right) when closed:

```xml
<!-- Settings Drawer (slides in from right edge) -->
<view
    class="CViewContainer"
    origin="925, 0"
    size="220, 880"
    background-color="bg-drawer"
    custom-view-name="SettingsDrawer"
    transparent="false"
>
    <!-- SETTINGS title -->
    <view class="CTextLabel" origin="16, 16" size="188, 20"
          title="SETTINGS"
          font="~ NormalFontBig"
          font-color="text-light"
          text-alignment="left"
          transparent="true"/>

    <!-- Pitch Bend Range (knob) -->
    <view class="CTextLabel" origin="16, 56" size="120, 14"
          title="Pitch Bend Range"
          font="~ NormalFontSmaller"
          font-color="text-secondary"
          text-alignment="left"
          transparent="true"/>
    <view class="ArcKnob" origin="16, 74" size="36, 36"
          control-tag="SettingsPitchBendRange"
          default-value="0.0833"
          arc-color="master"
          guide-color="knob-guide"/>

    <!-- Velocity Curve (dropdown) -->
    <view class="CTextLabel" origin="16, 126" size="120, 14"
          title="Velocity Curve"
          font="~ NormalFontSmaller"
          font-color="text-secondary"
          text-alignment="left"
          transparent="true"/>
    <view class="COptionMenu" origin="16, 144" size="140, 20"
          control-tag="SettingsVelocityCurve"
          default-value="0"
          font="~ NormalFontSmaller"
          font-color="master"
          back-color="bg-dropdown"
          frame-color="frame-dropdown-dim"
          transparent="false"/>

    <!-- Tuning Reference (knob) -->
    <view class="CTextLabel" origin="16, 182" size="120, 14"
          title="Tuning Reference"
          font="~ NormalFontSmaller"
          font-color="text-secondary"
          text-alignment="left"
          transparent="true"/>
    <view class="ArcKnob" origin="16, 200" size="36, 36"
          control-tag="SettingsTuningReference"
          default-value="0.5"
          arc-color="master"
          guide-color="knob-guide"/>

    <!-- Voice Allocation (dropdown) -->
    <view class="CTextLabel" origin="16, 252" size="120, 14"
          title="Voice Allocation"
          font="~ NormalFontSmaller"
          font-color="text-secondary"
          text-alignment="left"
          transparent="true"/>
    <view class="COptionMenu" origin="16, 270" size="140, 20"
          control-tag="SettingsVoiceAllocMode"
          font="~ NormalFontSmaller"
          font-color="master"
          back-color="bg-dropdown"
          frame-color="frame-dropdown-dim"
          transparent="false"/>

    <!-- Voice Steal Mode (dropdown) -->
    <view class="CTextLabel" origin="16, 308" size="120, 14"
          title="Voice Steal"
          font="~ NormalFontSmaller"
          font-color="text-secondary"
          text-alignment="left"
          transparent="true"/>
    <view class="COptionMenu" origin="16, 326" size="140, 20"
          control-tag="SettingsVoiceStealMode"
          font="~ NormalFontSmaller"
          font-color="master"
          back-color="bg-dropdown"
          frame-color="frame-dropdown-dim"
          transparent="false"/>

    <!-- Gain Compensation (toggle) -->
    <view class="CTextLabel" origin="16, 364" size="120, 14"
          title="Gain Compensation"
          font="~ NormalFontSmaller"
          font-color="text-secondary"
          text-alignment="left"
          transparent="true"/>
    <view class="ToggleButton" origin="16, 382" size="50, 20"
          control-tag="SettingsGainCompensation"
          default-value="1"
          on-color="master"
          off-color="toggle-off"
          title="On"
          transparent="true"/>
</view>
```

#### 5e. Add click-outside overlay for drawer dismiss (FR-011)

For "click outside drawer closes it", we need a transparent overlay that sits between the main UI and the drawer. When visible, clicking on it dismisses the drawer. This overlay is a `CViewContainer` spanning the full window, placed just before the drawer in z-order. It starts hidden and becomes visible when the drawer is open.

```xml
<!-- Settings Drawer Click-Outside Overlay (transparent, catches clicks to dismiss) -->
<view
    class="CViewContainer"
    origin="0, 0"
    size="925, 880"
    custom-view-name="SettingsOverlay"
    transparent="true"
    visible="false"
    mouse-enabled="true"
/>
```

In the controller, we wire this overlay with a mouse handler via `verifyView()`. When clicked, it calls `toggleSettingsDrawer()` to close.

Actually, VSTGUI `CViewContainer` with `transparent="true"` does not consume mouse events by default. We need a different approach. The simplest cross-platform solution is to register a `CMouseEventResult` handler. However, VSTGUI's architecture makes this tricky without subclassing.

**Alternative approach**: Instead of an overlay, we handle the click-outside behavior in the `verifyView` wiring. We intercept mouse events on the main UI containers. BUT this is complex and fragile.

**Simpler approach**: Use a custom transparent CControl subclass (like our ToggleButton but invisible) that spans the full window. When it receives a mouse down, it triggers the drawer close. However, this would require a new custom view class.

**Pragmatic approach**: Use the gear icon as the primary toggle (already planned). For click-outside-to-close, register the controller as a `IMouseObserver` (VSTGUI::IMouseObserver) on the editor frame. When a mouse click occurs and the drawer is open, check if the click is outside the drawer bounds and close it.

Wait -- VSTGUI has `CFrame::setMouseObserver()`. The controller can implement `IMouseObserver` and intercept `onMouseDown` to check if the click is outside the drawer when it's open. This is the cleanest approach:

```cpp
// In Controller, implement IMouseObserver
VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CFrame* frame, const VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons) override {
    if (settingsDrawerOpen_ && settingsDrawer_) {
        VSTGUI::CRect drawerRect = settingsDrawer_->getViewSize();
        if (!drawerRect.pointInside(where)) {
            toggleSettingsDrawer();
            return VSTGUI::kMouseEventHandled;
        }
    }
    return VSTGUI::kMouseEventNotHandled;
}
```

However, `IMouseObserver` is not a standard interface in the VST3 SDK. Let me reconsider.

**Best approach for VSTGUI**: Use a transparent `CControl` subclass that acts as a click catcher. But creating a new custom view class is heavy for this.

**Practical approach**: Use the existing `verifyView` + custom-view-name pattern to capture a `CViewContainer` that we make mouse-opaque when the drawer is open. A `CViewContainer` can be made to intercept mouse events if we subclass it. But we already have custom views.

**Final decision**: The simplest reliable VSTGUI pattern is:
1. Create a transparent overlay `CViewContainer` with `custom-view-name="SettingsOverlay"`.
2. In `verifyView`, store a pointer to it.
3. When the drawer opens, make the overlay visible. The overlay needs to be a class that consumes mouse events.
4. Since `CViewContainer` with no children and `transparent="true"` passes mouse events through, we need to use a `CView` subclass. The simplest is to use a `COnOffButton` (standard VSTGUI) with an invisible bitmap, or a simple `CControl` with no visual.

Actually, the ToggleButton class already handles mouse clicks and calls valueChanged. We can use a ToggleButton with no icon and transparent background as the overlay, with a specific tag:

```xml
<!-- Overlay uses ToggleButton with no icon, fully transparent -->
<view class="ToggleButton" origin="0, 0" size="925, 880"
      custom-view-name="SettingsOverlay"
      on-color="~ TransparentCColor"
      off-color="~ TransparentCColor"
      transparent="true"
      visible="false"/>
```

But ToggleButton draws icons, so a fully transparent one might still draw something. Let me check the ToggleButton draw method -- it draws an icon based on iconStyle_. If no icon-style is set, it defaults to... we need to check. But this is getting too complex.

**Simplest reliable approach**: Instead of an overlay, just handle "click outside" as part of the drawer's parent container. We add the drawer as a child of the root, and we use `CFrame::onMouseDown` override. Actually, since `VST3Editor` is a `CFrame`, we can't easily override its mouse handling.

**Final practical decision**: Skip the overlay approach. Instead:
1. When the drawer is open, register the controller as a `CControlListener` on the main content containers (the 5 row containers and the fieldset containers).
2. This is too fragile.

**Actually the simplest approach** that works reliably in VSTGUI is to NOT use an overlay at all. Instead, use a simple transparent `CControl` (our existing ToggleButton with `icon-style=""` and fully transparent colors) as a full-window hit area. When it gets a mouse click (value changes), close the drawer. This is placed between the main content and the drawer in z-order.

OR even simpler: since the ToggleButton class has an `onMouseDown` that toggles and calls `valueChanged`, and the controller's `valueChanged` can check the tag and close the drawer. We just need a ToggleButton covering the window with a unique tag:

```cpp
kActionSettingsOverlayTag = 10021,  // Click-outside overlay for drawer dismiss
```

In the uidesc, place it after all main content but before the drawer:
```xml
<view class="ToggleButton" origin="0, 0" size="925, 880"
      control-tag="ActionSettingsOverlay"
      icon-style=""
      on-color="~ TransparentCColor"
      off-color="~ TransparentCColor"
      transparent="true"
      visible="false"/>
```

When the drawer opens: overlay becomes visible and mouse-enabled. When it's clicked: `valueChanged` fires with the overlay tag, which closes the drawer. When the drawer closes: overlay becomes hidden.

The only issue: ToggleButton might draw something even with empty icon-style. We need to verify this. If icon-style is empty or unrecognized, the `drawIconAndTitle` method in `toggle_button.h` checks the icon style and only draws known icons (power, chevron, gear, funnel). If it's empty, it draws nothing -- just the title, which is also empty. So a ToggleButton with no icon-style and no title would be invisible. But it still has `onMouseDown` which toggles the value.

This approach works. Let me finalize it.

#### 5f. Controller fields and methods for drawer

In `controller.h`, add:
```cpp
// Settings drawer
VSTGUI::CViewContainer* settingsDrawer_ = nullptr;
VSTGUI::CView* settingsOverlay_ = nullptr;
VSTGUI::CControl* gearButton_ = nullptr;
VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> settingsAnimTimer_;
bool settingsDrawerOpen_ = false;
float settingsDrawerProgress_ = 0.0f;  // 0.0 = closed, 1.0 = open
bool settingsDrawerTargetOpen_ = false;

void toggleSettingsDrawer();
```

#### 5g. Drawer animation logic in controller.cpp

```cpp
void Controller::toggleSettingsDrawer() {
    settingsDrawerTargetOpen_ = !settingsDrawerOpen_;

    // If timer already running (animation in progress), it will naturally
    // reverse direction because we changed the target. No need to restart.
    if (settingsAnimTimer_) return;

    settingsAnimTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
        [this](VSTGUI::CVSTGUITimer*) {
            // Ease-out: progress moves toward target with deceleration
            constexpr float kAnimDuration = 0.16f;  // 160ms
            constexpr float kTimerInterval = 0.016f; // ~60fps
            constexpr float kStep = kTimerInterval / kAnimDuration;

            if (settingsDrawerTargetOpen_) {
                settingsDrawerProgress_ = std::min(settingsDrawerProgress_ + kStep, 1.0f);
            } else {
                settingsDrawerProgress_ = std::max(settingsDrawerProgress_ - kStep, 0.0f);
            }

            // Ease-out curve: 1 - (1-t)^2
            float eased = settingsDrawerTargetOpen_
                ? 1.0f - (1.0f - settingsDrawerProgress_) * (1.0f - settingsDrawerProgress_)
                : settingsDrawerProgress_ * (2.0f - settingsDrawerProgress_);

            // Map progress to x position: closed=925, open=705
            constexpr float kClosedX = 925.0f;
            constexpr float kOpenX = 705.0f;
            float x = kClosedX + (kOpenX - kClosedX) * eased;

            if (settingsDrawer_) {
                VSTGUI::CRect r = settingsDrawer_->getViewSize();
                r.moveTo(VSTGUI::CPoint(x, 0));
                settingsDrawer_->setViewSize(r);
                settingsDrawer_->invalid();
            }

            // Check if animation is complete
            bool done = settingsDrawerTargetOpen_
                ? (settingsDrawerProgress_ >= 1.0f)
                : (settingsDrawerProgress_ <= 0.0f);

            if (done) {
                settingsDrawerOpen_ = settingsDrawerTargetOpen_;
                settingsAnimTimer_ = nullptr;

                // Show/hide overlay
                if (settingsOverlay_) {
                    settingsOverlay_->setVisible(settingsDrawerOpen_);
                }

                // Update gear button state
                if (gearButton_) {
                    gearButton_->setValue(settingsDrawerOpen_ ? 1.0f : 0.0f);
                    gearButton_->invalid();
                }
            }
        }, 16); // ~60fps

    // Show overlay immediately when opening
    if (settingsDrawerTargetOpen_ && settingsOverlay_) {
        settingsOverlay_->setVisible(true);
    }
    // Hide overlay immediately when closing
    if (!settingsDrawerTargetOpen_ && settingsOverlay_) {
        settingsOverlay_->setVisible(false);
    }
}
```

**Correction to ease-out logic**: The animation progress is linear (kStep per frame), but the position mapping applies ease-out. The approach needs refinement:

For opening: progress goes 0->1. Eased position = 1 - (1-t)^2.
For closing (reverse): progress goes 1->0. We want the same deceleration feel, so the ease-out is applied symmetrically. Actually, "immediately reverse direction from current position" means:

When closing mid-animation, `settingsDrawerTargetOpen_` flips to false. The progress starts decreasing from wherever it is. The eased output should feel like ease-out in the reverse direction too.

A simpler implementation: track `settingsDrawerProgress_` as a raw 0-1 value (0=closed, 1=open). On each timer tick, move toward the target. Apply ease-out to convert progress to visual position.

#### 5h. Wire drawer in `verifyView()`

In the `custom-view-name` section of `verifyView()`:
```cpp
else if (*name == "SettingsDrawer") {
    settingsDrawer_ = container;
}
```

Also in `verifyView`, capture the overlay:
```cpp
else if (*name == "SettingsOverlay") {
    settingsOverlay_ = view;
    view->setVisible(false);
}
```

For the gear button, in the control tag registration section:
```cpp
if (tag == static_cast<int32_t>(kActionSettingsToggleTag)) {
    gearButton_ = control;
    control->registerControlListener(this);
}
```

#### 5i. Handle gear icon click in `valueChanged()`

In the toggle buttons section (around line 990):
```cpp
case kActionSettingsToggleTag: toggleSettingsDrawer(); return;
```

Handle overlay click:
```cpp
case kActionSettingsOverlayTag:
    if (settingsDrawerOpen_) {
        toggleSettingsDrawer();
    }
    return;
```

#### 5j. Clean up in `willClose()`

Add to the cleanup list:
```cpp
settingsDrawer_ = nullptr;
settingsOverlay_ = nullptr;
gearButton_ = nullptr;
settingsAnimTimer_ = nullptr;
settingsDrawerOpen_ = false;
settingsDrawerProgress_ = 0.0f;
settingsDrawerTargetOpen_ = false;
```

### Task Group 6: Architecture Documentation Update

Update `specs/_architecture_/` to document:
- Settings parameters (IDs 2200-2205, linking to engine methods)
- Settings drawer UI (animation pattern, gear icon toggle, click-outside dismiss)
- State version 14 format (appended settings param pack)

## Window Geometry Notes

**Actual window dimensions**: 925x880 (discovered from uidesc line 1815-1817, NOT 900x866 as spec approximated)

**Drawer calculations**:
- Drawer width: 220px
- Closed position: x=925 (fully off-screen right)
- Open position: x=925-220=705
- Main content area: 900px wide (rows are 900px), positioned at x=0
- Voice & Output panel: x=772, width=145 (right edge at 917)
- When drawer is open at x=705, it overlaps the rightmost ~195px of the 900px content area
- The left 705px of the main UI remains fully unobstructed

## Complexity Tracking

No constitution violations to justify. All design decisions follow established patterns exactly.

## Post-Design Constitution Re-Check

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Processor owns SettingsParams. Controller registers/formats/animates drawer. No cross-dependency. |
| II. Real-Time Thread Safety | PASS | All param stores use `std::atomic` with `memory_order_relaxed`. No allocations in new audio-thread code. Drawer animation is UI-thread only (CVSTGUITimer). |
| III. Modern C++ | PASS | `std::atomic`, `constexpr`, `std::clamp`, `static_cast`. No raw new/delete. |
| V. VSTGUI | PASS | All controls via UIDescription XML. ArcKnob, COptionMenu, ToggleButton, CTextLabel -- all cross-platform. CVSTGUITimer for animation. |
| VI. Cross-Platform | PASS | No platform-specific code. VSTGUI abstractions only. |
| VIII. Testing | PASS | Tests for param handling, state persistence, backward compatibility. |
| IX. Layered DSP | N/A | No new DSP components. |
| XIII. Test-First | PASS | Tests designed before implementation for each task group. |
| XV. ODR Prevention | PASS | No duplicate types. `SettingsParams` is unique. |
| XVI. Honest Completion | PASS | Compliance table will be filled from actual code/test verification only. |
