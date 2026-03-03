# Implementation Plan: Master Section Panel - Wire Voice & Output Controls

**Branch**: `054-master-section-panel` | **Date**: 2026-02-14 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/054-master-section-panel/spec.md`

## Summary

Wire the Voice & Output panel with functional controls: add a Voice Mode dropdown (Poly/Mono) bound to the already-registered `kVoiceModeId` parameter, add two new global parameter IDs (`kWidthId = 4`, `kSpreadId = 5`) for stereo Width and Spread, bind the existing placeholder knobs to these parameters, and forward them through the processor to the engine's `setStereoWidth()` / `setStereoSpread()` methods. This is a parameter pipeline wiring task with no DSP code changes -- only IDs, registration, handler, state persistence, processor forwarding, and uidesc bindings.

## Technical Context

**Language/Version**: C++20, VST3 SDK 3.7.x, VSTGUI 4.12+
**Primary Dependencies**: VST3 SDK, VSTGUI, RuinaeEngine (existing DSP)
**Storage**: IBStreamer binary state persistence (existing pattern)
**Testing**: Manual pluginval level 5 + visual verification (no unit tests -- this is pure parameter pipeline + UI wiring)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform via VSTGUI
**Project Type**: VST3 plugin monorepo
**Performance Goals**: Zero audio thread impact -- Width/Spread forwarded per-block via existing atomic reads
**Constraints**: 120x160px panel footprint, backward-compatible state loading
**Scale/Scope**: 5 files modified, ~80 lines of code added total

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Width/Spread are VST parameters registered in Controller, handled in Processor -- proper separation
- [x] No cross-inclusion between processor and controller
- [x] State flows Host -> Processor -> Controller via `setComponentState()`

**Principle II (Real-Time Safety):**
- [x] Width/Spread stored as `std::atomic<float>` -- lock-free reads on audio thread
- [x] `engine_.setStereoWidth()` / `setStereoSpread()` are noexcept, no allocation
- [x] No new allocations, locks, or exceptions on audio thread

**Principle III (Modern C++):**
- [x] Using `std::atomic`, `std::clamp`, no raw new/delete
- [x] All existing patterns followed

**Principle V (VSTGUI):**
- [x] Using COptionMenu for dropdown (VSTGUI cross-platform control)
- [x] Using existing ArcKnob custom view with control-tag binding
- [x] UI thread only accesses parameter values via IParameterChanges

**Principle VI (Cross-Platform):**
- [x] No platform-specific APIs used
- [x] All UI via VSTGUI controls (COptionMenu, ArcKnob, CTextLabel, ToggleButton)

**Principle VIII (Testing Discipline):**
- [x] Pluginval level 5 validation planned
- [x] Existing tests must pass (zero regressions)

**Principle XIII (Test-First):**
- Note: This is a UI wiring spec with no DSP logic changes. The "test" is pluginval + manual verification. No unit tests are applicable since we are only adding parameter IDs, uidesc bindings, and atomic forwarding calls.

**Principle XIV (ODR Prevention):**
- [x] No new classes or structs created
- [x] Only extending existing `GlobalParams` struct with 2 new fields
- [x] Only extending existing functions with new cases

**Principle XVI (Honest Completion):**
- [x] All FR/SC items will be individually verified against code and test output

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] No unit tests applicable (parameter wiring + UI binding only)
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None. Only extending the existing `GlobalParams` struct.

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| GlobalParams (extend) | `grep -r "struct GlobalParams" plugins/` | Yes - `plugins/ruinae/src/parameters/global_params.h:25` | Extend with `width` and `spread` fields |

**Utility Functions to be created**: None. Only extending existing functions with new switch cases.

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| handleGlobalParamChange (extend) | N/A | Yes | `global_params.h:36` | Add kWidthId, kSpreadId cases |
| registerGlobalParams (extend) | N/A | Yes | `global_params.h:72` | Add Width, Spread param registration |
| formatGlobalParam (extend) | N/A | Yes | `global_params.h:105` | Add Width, Spread display formatting |
| saveGlobalParams (extend) | N/A | Yes | `global_params.h:139` | Add Width, Spread float writes |
| loadGlobalParams (extend) | N/A | Yes | `global_params.h:146` | Add Width, Spread float reads (EOF-safe) |
| loadGlobalParamsToController (extend) | N/A | Yes | `global_params.h:170` | Add Width, Spread controller sync |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| GlobalParams struct | `plugins/ruinae/src/parameters/global_params.h:25` | Plugin | Extend with `width` and `spread` atomic fields |
| handleGlobalParamChange() | `plugins/ruinae/src/parameters/global_params.h:36` | Plugin | Add kWidthId/kSpreadId denormalization cases |
| registerGlobalParams() | `plugins/ruinae/src/parameters/global_params.h:72` | Plugin | Add Width/Spread Parameter registration |
| formatGlobalParam() | `plugins/ruinae/src/parameters/global_params.h:105` | Plugin | Add Width/Spread percentage display |
| saveGlobalParams() | `plugins/ruinae/src/parameters/global_params.h:139` | Plugin | Add Width/Spread float persistence |
| loadGlobalParams() | `plugins/ruinae/src/parameters/global_params.h:146` | Plugin | Add Width/Spread EOF-safe reading |
| loadGlobalParamsToController() | `plugins/ruinae/src/parameters/global_params.h:170` | Plugin | Add Width/Spread controller sync |
| RuinaeEngine::setStereoWidth() | `plugins/ruinae/src/engine/ruinae_engine.h:308` | Engine | Called from processor forwarding |
| RuinaeEngine::setStereoSpread() | `plugins/ruinae/src/engine/ruinae_engine.h:299` | Engine | Called from processor forwarding |
| createDropdownParameter() | `plugins/ruinae/src/controller/parameter_helpers.h:22` | Plugin | Already used for VoiceMode -- no changes needed |

### Files Checked for Conflicts

- [x] `plugins/ruinae/src/plugin_ids.h` - Confirmed IDs 4 and 5 are available
- [x] `plugins/ruinae/src/parameters/global_params.h` - No naming conflicts
- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - Engine methods already exist
- [x] `plugins/ruinae/resources/editor.uidesc` - Placeholder knobs ready for control-tag binding

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new types or functions are created. All changes are extensions to existing components (adding fields to a struct, adding cases to switch statements, adding control-tags to XML). The parameter IDs 4 and 5 are confirmed unused in the global range.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| RuinaeEngine | setStereoWidth | `void setStereoWidth(float width) noexcept` | Yes |
| RuinaeEngine | setStereoSpread | `void setStereoSpread(float spread) noexcept` | Yes |
| GlobalParams | masterGain | `std::atomic<float> masterGain{1.0f}` | Yes |
| GlobalParams | voiceMode | `std::atomic<int> voiceMode{0}` | Yes |
| GlobalParams | polyphony | `std::atomic<int> polyphony{8}` | Yes |
| GlobalParams | softLimit | `std::atomic<bool> softLimit{true}` | Yes |
| IBStreamer | writeFloat | `bool writeFloat(float val)` | Yes |
| IBStreamer | readFloat | `bool readFloat(float& val)` | Yes |

### Header Files Read

- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - setStereoWidth (line 308), setStereoSpread (line 299)
- [x] `plugins/ruinae/src/parameters/global_params.h` - GlobalParams struct (line 25), all functions
- [x] `plugins/ruinae/src/plugin_ids.h` - ParameterIDs enum (line 54)
- [x] `plugins/ruinae/src/controller/parameter_helpers.h` - createDropdownParameter (line 22)
- [x] `plugins/ruinae/src/processor/processor.cpp` - Global param forwarding (lines 627-632)
- [x] `plugins/ruinae/src/controller/controller.cpp` - registerGlobalParams call (line 94), setComponentState (line 143)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Width denormalization | norm * 2.0 = engine value (0.0-2.0), NOT percentage | `params.width.store(float(value * 2.0), ...)` |
| Width display | 0.0 norm = "0%", 0.5 norm = "100%", 1.0 norm = "200%" | `snprintf(text, sizeof(text), "%d%%", int(value * 200.0 + 0.5))` |
| Width controller sync | Controller receives engine value, must convert to normalized | `setParam(kWidthId, floatVal / 2.0)` |
| Spread | norm = engine value (0.0-1.0), 1:1 mapping | No conversion needed |
| loadGlobalParams EOF | Must NOT return false for missing Width/Spread (breaks old presets) | Read Width/Spread after existing fields; if read fails, keep defaults |
| VoiceMode dropdown items | Spec says "Polyphonic" and "Mono" but parameter was registered with "Poly"/"Mono" | The dropdown automatically uses the StringListParameter's registered strings ("Poly", "Mono"). The spec's "Polyphonic" refers to UI display text in the label, but the actual COptionMenu will show "Poly" and "Mono" as registered. |

## Layer 0 Candidate Analysis

Not applicable. This spec adds no DSP code -- only parameter pipeline wiring and UI bindings.

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| -- | -- |

**Decision**: No Layer 0 extraction needed. All changes are in the plugin layer.

## SIMD Optimization Analysis

Not applicable. This spec adds no DSP processing code. Width and Spread are forwarded to existing engine methods that already handle their own processing.

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This spec is a parameter pipeline + UI wiring task. No new DSP algorithms are introduced. The engine's existing `setStereoWidth()` and `setStereoSpread()` methods handle the actual audio processing and are unchanged.

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin parameter/UI layer

**Related features at same layer:**
- Phase 3 (Mono Mode Panel): Will use the Voice Mode dropdown added here to conditionally show mono controls
- Phase 5 (Settings Drawer): Will use the gear icon placeholder already in the panel

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| EOF-safe state loading pattern | HIGH | Any future spec adding global params | Document pattern in plan |
| "norm * 2.0" Width denormalization | LOW | Only Width uses this | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared utilities needed | All changes extend existing patterns -- no novel code |
| EOF-safe loading pattern documented | Future global params will follow the same read-or-default pattern |

### Review Trigger

After implementing **Phase 3 (Mono Mode Panel)**, review this section:
- [ ] Does Phase 3 need the Voice Mode dropdown? -> Already added by this spec
- [ ] Does Phase 3 add more global params? -> Follow the EOF-safe pattern from this spec

## Project Structure

### Documentation (this feature)

```text
specs/054-master-section-panel/
+-- plan.md              # This file
+-- research.md          # Phase 0 output (minimal -- straightforward wiring)
+-- data-model.md        # Phase 1 output (parameter model)
+-- quickstart.md        # Phase 1 output (implementation guide)
+-- contracts/           # Phase 1 output (parameter contracts)
+-- spec.md              # Feature specification
```

### Source Code (files to modify)

```text
plugins/ruinae/
+-- src/
|   +-- plugin_ids.h                    # Add kWidthId=4, kSpreadId=5
|   +-- parameters/
|   |   +-- global_params.h             # Extend GlobalParams + all 6 functions
|   +-- processor/
|   |   +-- processor.cpp               # Add engine_.setStereoWidth/Spread forwarding
+-- resources/
    +-- editor.uidesc                    # Add control-tags + wire knobs + add VoiceMode dropdown
```

**Structure Decision**: VST3 plugin monorepo layout. All changes are within the existing `plugins/ruinae/` directory. No new files created.

## Concrete Implementation Changes

### File 1: `plugins/ruinae/src/plugin_ids.h`

**Change**: Add `kWidthId = 4` and `kSpreadId = 5` to the Global Parameters section.

**Location**: After `kSoftLimitId = 3` (line 58), before `kGlobalEndId = 99` (line 59).

```cpp
// Current (line 57-59):
kSoftLimitId = 3,          // on/off
kGlobalEndId = 99,

// New:
kSoftLimitId = 3,          // on/off
kWidthId = 4,              // 0-200% stereo width (norm * 2.0)
kSpreadId = 5,             // 0-100% voice spread
kGlobalEndId = 99,
```

### File 2: `plugins/ruinae/src/parameters/global_params.h`

**Change 1 - GlobalParams struct** (line 25-30): Add `width` and `spread` atomic fields.

```cpp
struct GlobalParams {
    std::atomic<float> masterGain{1.0f};    // 0-2 (linear gain)
    std::atomic<int> voiceMode{0};          // 0=Poly, 1=Mono
    std::atomic<int> polyphony{8};          // 1-16
    std::atomic<bool> softLimit{true};      // on/off
    std::atomic<float> width{1.0f};         // 0-2 (stereo width: 0=mono, 1=natural, 2=extra-wide)
    std::atomic<float> spread{0.0f};        // 0-1 (voice spread: 0=center, 1=full)
};
```

**Change 2 - handleGlobalParamChange()** (line 36-66): Add kWidthId and kSpreadId cases before default.

```cpp
case kWidthId:
    // 0-1 normalized -> 0-2 stereo width
    params.width.store(
        std::clamp(static_cast<float>(value * 2.0), 0.0f, 2.0f),
        std::memory_order_relaxed);
    break;
case kSpreadId:
    // 0-1 normalized -> 0-1 spread (1:1 mapping)
    params.spread.store(
        std::clamp(static_cast<float>(value), 0.0f, 1.0f),
        std::memory_order_relaxed);
    break;
```

**Change 3 - registerGlobalParams()** (line 72-99): Add Width and Spread parameter registration after Soft Limit.

```cpp
// Width (0-200%, default 100% = normalized 0.5)
parameters.addParameter(
    STR16("Width"), STR16("%"), 0, 0.5,
    ParameterInfo::kCanAutomate, kWidthId);

// Spread (0-100%, default 0%)
parameters.addParameter(
    STR16("Spread"), STR16("%"), 0, 0.0,
    ParameterInfo::kCanAutomate, kSpreadId);
```

**Change 4 - formatGlobalParam()** (line 105-133): Add Width and Spread percentage display before default.

```cpp
case kWidthId: {
    int pct = static_cast<int>(value * 200.0 + 0.5);
    char8 text[32];
    snprintf(text, sizeof(text), "%d%%", pct);
    UString(string, 128).fromAscii(text);
    return kResultOk;
}
case kSpreadId: {
    int pct = static_cast<int>(value * 100.0 + 0.5);
    char8 text[32];
    snprintf(text, sizeof(text), "%d%%", pct);
    UString(string, 128).fromAscii(text);
    return kResultOk;
}
```

**Change 5 - saveGlobalParams()** (line 139-144): Add Width and Spread float writes after softLimit.

```cpp
streamer.writeFloat(params.width.load(std::memory_order_relaxed));
streamer.writeFloat(params.spread.load(std::memory_order_relaxed));
```

**Change 6 - loadGlobalParams()** (line 146-163): Add EOF-safe Width and Spread reads after softLimit.

```cpp
// Width (new in this spec - EOF-safe for old presets)
if (streamer.readFloat(floatVal))
    params.width.store(floatVal, std::memory_order_relaxed);
// else: keep default 1.0f (natural stereo width)

// Spread (new in this spec - EOF-safe for old presets)
if (streamer.readFloat(floatVal))
    params.spread.store(floatVal, std::memory_order_relaxed);
// else: keep default 0.0f (all voices centered)
```

Note: Unlike the existing fields which return false on read failure (aborting the entire load), Width and Spread reads do NOT return false on failure. This preserves backward compatibility -- old presets that lack these fields will simply use defaults.

**Change 7 - loadGlobalParamsToController()** (line 170-184): Add Width and Spread controller sync after softLimit.

```cpp
if (streamer.readFloat(floatVal))
    setParam(kWidthId, static_cast<double>(floatVal / 2.0f));
if (streamer.readInt32(intVal))  // Note: this should be readFloat for consistency
    setParam(kSpreadId, static_cast<double>(floatVal));
```

Wait -- let me re-examine this. The `loadGlobalParamsToController` reads the same stream format as `loadGlobalParams`. Width is stored as the engine value (0-2), and the controller needs it as normalized (0-1). Spread is stored as 0-1 which is also the normalized value.

```cpp
// Width: engine value (0-2) -> normalized (0-1)
if (streamer.readFloat(floatVal))
    setParam(kWidthId, static_cast<double>(floatVal / 2.0f));
// Spread: stored value (0-1) = normalized (0-1)
if (streamer.readFloat(floatVal))
    setParam(kSpreadId, static_cast<double>(floatVal));
```

### File 3: `plugins/ruinae/src/processor/processor.cpp`

**Change**: Add Width and Spread forwarding to engine after existing global param forwarding (after line 632).

**Location**: After `engine_.setSoftLimitEnabled(...)` on line 632.

```cpp
engine_.setStereoWidth(globalParams_.width.load(std::memory_order_relaxed));
engine_.setStereoSpread(globalParams_.spread.load(std::memory_order_relaxed));
```

### File 4: `plugins/ruinae/resources/editor.uidesc`

**Change 1 - Control Tags** (after line 65): Add VoiceMode, Width, and Spread control-tags.

```xml
<!-- Master/Global -->
<control-tag name="MasterGain" tag="0"/>
<control-tag name="VoiceMode" tag="1"/>
<control-tag name="Polyphony" tag="2"/>
<control-tag name="SoftLimit" tag="3"/>
<control-tag name="Width" tag="4"/>
<control-tag name="Spread" tag="5"/>
```

**Change 2 - Voice & Output Panel** (lines 2607-2689): Reorganize panel to add Voice Mode dropdown on row 1, push Polyphony to row 2, and wire Width/Spread knobs.

The complete revised panel content (between the FieldsetContainer open and close tags):

```xml
<!-- Voice & Output (silver accent) -- Row 1, right of OSC B -->
<view
    class="FieldsetContainer"
    origin="772, 32"
    size="120, 160"
    fieldset-title="Voice &amp; Output"
    fieldset-color="master"
    fieldset-radius="4"
    fieldset-line-width="1"
    fieldset-font-size="10"
    transparent="true"
>
    <!-- Row 1: Voice Mode dropdown with "Mode" label + gear icon -->
    <view class="CTextLabel" origin="4, 14" size="28, 18"
          title="Mode"
          font="~ NormalFontSmaller"
          font-color="text-secondary"
          text-alignment="left"
          transparent="true"/>
    <view class="COptionMenu" origin="32, 14" size="56, 18"
          control-tag="VoiceMode"
          default-value="0"
          font="~ NormalFontSmaller"
          font-color="master"
          back-color="bg-dropdown"
          frame-color="frame-dropdown-dim"
          tooltip="Voice Mode"
          transparent="false"/>

    <!-- Gear icon (inert placeholder for future settings drawer) -->
    <view class="ToggleButton" origin="92, 14" size="18, 18"
          icon-style="gear"
          on-color="master"
          off-color="text-secondary"
          icon-size="0.65"
          stroke-width="1.5"
          tooltip="Settings"
          transparent="true"/>

    <!-- Row 2: Polyphony dropdown with "Poly" label -->
    <view class="COptionMenu" origin="8, 36" size="60, 18"
          control-tag="Polyphony"
          font="~ NormalFontSmaller"
          font-color="master"
          back-color="bg-dropdown"
          frame-color="frame-dropdown-dim"
          tooltip="Polyphony"
          transparent="false"/>
    <view class="CTextLabel" origin="8, 54" size="60, 10"
          title="Poly"
          font="~ NormalFontSmaller"
          font-color="text-secondary"
          text-alignment="center"
          transparent="true"/>

    <!-- Output knob (centered) -->
    <view class="ArcKnob" origin="42, 66" size="36, 36"
          control-tag="MasterGain"
          default-value="0.5"
          arc-color="master"
          guide-color="knob-guide"/>
    <view class="CTextLabel" origin="34, 102" size="52, 10"
          title="Output"
          font="~ NormalFontSmaller"
          font-color="text-secondary"
          text-alignment="center"
          transparent="true"/>

    <!-- Width knob (functional, wired to parameter) -->
    <view class="ArcKnob" origin="14, 114" size="28, 28"
          control-tag="Width"
          default-value="0.5"
          arc-color="master"
          guide-color="knob-guide"/>
    <view class="CTextLabel" origin="10, 142" size="36, 10"
          title="Width"
          font="~ NormalFontSmaller"
          font-color="text-secondary"
          text-alignment="center"
          transparent="true"/>

    <!-- Spread knob (functional, wired to parameter) -->
    <view class="ArcKnob" origin="62, 114" size="28, 28"
          control-tag="Spread"
          default-value="0"
          arc-color="master"
          guide-color="knob-guide"/>
    <view class="CTextLabel" origin="58, 142" size="40, 10"
          title="Spread"
          font="~ NormalFontSmaller"
          font-color="text-secondary"
          text-alignment="center"
          transparent="true"/>

    <!-- Soft Limit toggle (bottom) -->
    <view class="ToggleButton" origin="20, 154" size="80, 16"
          control-tag="SoftLimit"
          title="Soft Limit"
          font="~ NormalFontSmaller"
          font-color="master"
          text-color="master"
          tooltip="Limit output level"
          transparent="true"/>
</view>
```

**Layout Notes**: The panel height is 160px. With the added Voice Mode dropdown row, the vertical layout becomes tight. The key positions are:
- Row 1 (Voice Mode + gear): y=14, height=18 -> ends at y=32
- Row 2 (Polyphony): y=36, height=18 -> ends at y=54
- Output knob: y=66, height=36 -> ends at y=102
- Output label: y=102, height=10 -> ends at y=112
- Width/Spread knobs: y=114, height=28 -> ends at y=142
- Width/Spread labels: y=142, height=10 -> ends at y=152
- Soft Limit: y=154, height=16 -> ends at y=170 (10px outside panel)

The Soft Limit toggle at y=154 with height 16 ends at y=170, which is 10px outside the 160px panel. This may need adjustment. Alternative layouts:

**Layout Option A (reduce Output knob vertical spacing)**:
- Row 1 (Voice Mode): y=14
- Row 2 (Polyphony): y=34
- Output knob: y=56, size=36x36 -> ends at y=92
- Output label: y=92 -> ends at y=102
- Width/Spread: y=104, size=28x28 -> ends at y=132
- Width/Spread labels: y=132 -> ends at y=142
- Soft Limit: y=144 -> ends at y=160

**Layout Option B (remove Poly label, use tooltip)**:
- Row 1 (Voice Mode): y=14
- Row 2 (Polyphony): y=34 (no label, tooltip only)
- Output knob: y=56
- ...more room

**Recommended**: Option A -- tighter vertical spacing without removing any labels. All controls fit within 160px.

**FINAL LAYOUT (Option A adjusted)**:

```
y=14:  [Mode] [Poly/Mono dropdown] [gear]   (row 1, 18px high)
y=34:  [Polyphony dropdown]                   (row 2, 18px high)
y=56:  [Output knob 36x36]                    (centered, 36px high)
y=92:  [Output label]                          (10px high)
y=104: [Width 28x28] [Spread 28x28]           (side by side, 28px high)
y=132: [Width label] [Spread label]             (10px high)
y=144: [Soft Limit toggle]                     (16px high, ends at y=160)
```

Total: 14px top padding + 146px content = 160px. Fits exactly.

## Implementation Task Groups

### Task Group 1: Parameter IDs and Registration

**Files**: `plugin_ids.h`, `global_params.h`
**Steps**:
1. Add `kWidthId = 4` and `kSpreadId = 5` to `plugin_ids.h`
2. Add `width` and `spread` fields to `GlobalParams` struct
3. Add kWidthId/kSpreadId cases to `handleGlobalParamChange()`
4. Add Width/Spread registration to `registerGlobalParams()`
5. Add Width/Spread formatting to `formatGlobalParam()`
6. Build and verify zero warnings

### Task Group 2: State Persistence

**Files**: `global_params.h`
**Steps**:
1. Add Width/Spread writes to `saveGlobalParams()`
2. Add EOF-safe Width/Spread reads to `loadGlobalParams()`
3. Add Width/Spread sync to `loadGlobalParamsToController()`
4. Build and verify zero warnings

### Task Group 3: Processor Forwarding

**Files**: `processor.cpp`
**Steps**:
1. Add `engine_.setStereoWidth()` call after existing global param forwarding
2. Add `engine_.setStereoSpread()` call after setStereoWidth
3. Build and verify zero warnings

### Task Group 4: UI Wiring

**Files**: `editor.uidesc`
**Steps**:
1. Add VoiceMode (tag 1), Width (tag 4), Spread (tag 5) control-tags
2. Reorganize Voice & Output panel: add Voice Mode dropdown with "Mode" label on row 1, push Polyphony to row 2, adjust vertical positions
3. Wire Width knob: add `control-tag="Width"` and `default-value="0.5"`
4. Wire Spread knob: add `control-tag="Spread"` and `default-value="0"`
5. Verify all controls fit within 120x160px panel

### Task Group 5: Build, Pluginval, and Verification

**Steps**:
1. Full Release build with zero warnings
2. Run pluginval at strictness level 5
3. Visual verification: Voice Mode dropdown works, Width/Spread knobs respond
4. Manual test: load old preset, verify defaults applied
5. Fill compliance table with concrete evidence

## Complexity Tracking

No constitution violations. All changes follow established patterns.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| -- | -- | -- |
