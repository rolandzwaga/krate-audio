# Implementation Plan: Innexus Plugin UI

**Branch**: `121-plugin-ui` | **Date**: 2026-03-06 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/121-plugin-ui/spec.md`

## Summary

Implement the full VSTGUI interface for the Innexus harmonic resynthesis instrument: 800x600 fixed editor with 48 parameter controls using vector-drawn shared components (ArcKnob, ToggleButton, ActionButton, BipolarSlider, FieldsetContainer), 5 custom CView subclasses for real-time display (spectral, confidence, memory status, evolution position, modulator activity), a reusable modulator template with DelegationController-based sub-controller for tag remapping, and an IMessage-based processor-to-controller data pipeline with 30ms CVSTGUITimer polling.

## Technical Context

**Language/Version**: C++20 (MSVC 2022, Clang, GCC)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, shared plugin UI components (`plugins/shared/src/ui/`)
**Storage**: N/A (all state in VST3 parameter system and IMessage)
**Testing**: Catch2 via `innexus_tests` target; VSTGUI test stubs already present at `plugins/innexus/tests/vstgui_test_stubs.cpp`
**Target Platform**: Windows 10/11, macOS 11+, Linux
**Project Type**: Plugin UI (controller-side)
**Performance Goals**: Spectral display >= 10 fps (SC-003), zero audio thread stalls (SC-008)
**Constraints**: Fixed 800x600, cross-platform VSTGUI only (no native APIs), vector-drawn (no bitmap assets)
**Scale/Scope**: 48 parameters, 5 custom views, 1 reusable template, ~15 source files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- [x] **Principle I (VST3 Architecture Separation)**: Processor and Controller remain separate. IMessage for display data transfer. Controller never touches audio data directly.
- [x] **Principle II (Real-Time Safety)**: Custom views use lock-free atomic buffers. 30ms timer on UI thread. `allocateMessage()` is called on the audio thread as part of the VST3 IMessage pattern — this is the project-wide convention (also used in Disrumpo); if profiling reveals jitter a lock-free SPSC queue should replace it.
- [x] **Principle III (Modern C++)**: Smart pointers for custom views, RAII for timer lifecycle.
- [x] **Principle V (VSTGUI)**: UIDescription XML, VST3EditorDelegate for custom views, IParameterChanges for UI->Processor.
- [x] **Principle VI (Cross-Platform)**: VSTGUI-only controls. No Win32/Cocoa/AppKit. Vector-drawn shared components.
- [x] **Principle VIII (Testing)**: Tests written before implementation code.
- [x] **Principle XII (Debug Before Pivot)**: N/A at planning stage.
- [x] **Principle XIII (Test-First)**: Skills auto-load. Tests before implementation.
- [x] **Principle XIV (ODR Prevention)**: Codebase research section below is complete. No duplicate classes.
- [x] **Principle XVI (Honest Completion)**: Compliance table filled from actual code/test evidence at completion.
- [x] **Principle XVII (Framework Knowledge)**: vst-guide skill loaded. Sub-controller and custom view patterns documented.

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Existing? | Action |
|--------------|-----------|--------|
| `HarmonicDisplayView` | No (only `Disrumpo::SpectrumDisplay` exists, different domain) | Create New in `Innexus` namespace |
| `ConfidenceIndicatorView` | No | Create New in `Innexus` namespace |
| `MemorySlotStatusView` | No | Create New in `Innexus` namespace |
| `EvolutionPositionView` | No | Create New in `Innexus` namespace |
| `ModulatorActivityView` | No | Create New in `Innexus` namespace |
| `ModulatorSubController` | No (only `Disrumpo::BandSubController` exists) | Create New in `Innexus` namespace |
| `DisplayData` (struct for IMessage payload) | No | Create New in `Innexus` namespace |

**Utility Functions to be created**: None. All drawing utilities use VSTGUI primitives and `Krate::Plugins` color utils.

### Existing Components to Reuse

| Component | Location | How It Will Be Used |
|-----------|----------|---------------------|
| `ArcKnob` | `plugins/shared/src/ui/arc_knob.h` | Primary knob for all continuous parameters (Gain, Release, Rate, Depth, etc.) |
| `ToggleButton` | `plugins/shared/src/ui/toggle_button.h` | Bypass, Freeze, Evolution Enable, Mod Enable, Blend Enable |
| `ActionButton` | `plugins/shared/src/ui/action_button.h` | Memory Capture and Recall momentary buttons |
| `BipolarSlider` | `plugins/shared/src/ui/bipolar_slider.h` | Residual Brightness (-1.0 to +1.0) |
| `FieldsetContainer` | `plugins/shared/src/ui/fieldset_container.h` | Section grouping with labeled borders (FR-003) |
| `lerpColor`, `darkenColor` | `plugins/shared/src/ui/color_utils.h` | Confidence indicator color interpolation, dimmed partial colors |
| `DelegationController` | VSTGUI SDK | Base class for `ModulatorSubController` |
| `CVSTGUITimer` | VSTGUI SDK | 30ms UI update timer for custom views |
| `CSegmentButton` | VSTGUI SDK | Input Source, Latency Mode, Harmonic Filter, Evolution Mode, Mod Target selectors |
| `COptionMenu` | VSTGUI SDK | Memory Slot selector, Mod Waveform selector |
| `CTextLabel` | VSTGUI SDK | Section headers, value displays |
| `CParamDisplay` | VSTGUI SDK | Parameter value readouts |

### Files Checked for Conflicts

- [x] `plugins/shared/src/ui/` - All shared UI components reviewed
- [x] `plugins/disrumpo/src/controller/views/` - SpectrumDisplay, MorphPad (different namespace, no conflict)
- [x] `plugins/innexus/src/controller/` - Currently only controller.h/cpp with no custom views
- [x] `plugins/innexus/src/dsp/` - DSP types checked (HarmonicFrame, MemorySlot)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are new and unique to the `Innexus` namespace. No name collisions with existing types. The only structural similarity is with `Disrumpo::SpectrumDisplay`, which is in a different namespace and serves a different purpose (frequency spectrum analysis vs harmonic partial bars).

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `HarmonicFrame` | `partials` | `std::array<Partial, kMaxPartials> partials{}` | Yes |
| `HarmonicFrame` | `f0` | `float f0 = 0.0f` | Yes |
| `HarmonicFrame` | `f0Confidence` | `float f0Confidence = 0.0f` | Yes |
| `HarmonicFrame` | `numPartials` | `int numPartials = 0` | Yes |
| `Partial` | `amplitude` | `float amplitude = 0.0f` | Yes |
| `Partial` | `harmonicIndex` | `int harmonicIndex = 0` | Yes |
| `MemorySlot` | `occupied` | `bool occupied = false` | Yes |
| `EvolutionEngine` | `getPosition()` | `[[nodiscard]] float getPosition() const noexcept` | Yes |
| `Processor` | `getMorphedFrame()` | `const Krate::DSP::HarmonicFrame& getMorphedFrame() const` | Yes |
| `Processor` | `getMemorySlot(int)` | `const Krate::DSP::MemorySlot& getMemorySlot(int index) const` | Yes |
| `ArcKnob` | constructor | `ArcKnob(const CRect& size, IControlListener* listener, int32_t tag)` | Yes |
| `ToggleButton` | constructor | `ToggleButton(const CRect& size, IControlListener* listener, int32_t tag)` | Yes |
| `ActionButton` | constructor | `ActionButton(const CRect& size, IControlListener* listener, int32_t tag)` | Yes |
| `BipolarSlider` | constructor | `BipolarSlider(const CRect& size, IControlListener* listener, int32_t tag)` | Yes |
| `FieldsetContainer` | constructor | `explicit FieldsetContainer(const CRect& size)` | Yes |
| `FieldsetContainer` | `setTitle` | setter for `title_` (std::string) | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/harmonic_types.h` - HarmonicFrame, Partial
- [x] `dsp/include/krate/dsp/processors/harmonic_snapshot.h` - MemorySlot
- [x] `plugins/innexus/src/dsp/evolution_engine.h` - EvolutionEngine::getPosition()
- [x] `plugins/innexus/src/processor/processor.h` - Processor API
- [x] `plugins/innexus/src/controller/controller.h` - Current controller state
- [x] `plugins/shared/src/ui/arc_knob.h` - ArcKnob
- [x] `plugins/shared/src/ui/toggle_button.h` - ToggleButton
- [x] `plugins/shared/src/ui/action_button.h` - ActionButton
- [x] `plugins/shared/src/ui/bipolar_slider.h` - BipolarSlider
- [x] `plugins/shared/src/ui/fieldset_container.h` - FieldsetContainer
- [x] `plugins/shared/src/ui/color_utils.h` - Color utilities
- [x] `plugins/disrumpo/src/controller/sub_controllers.h` - BandSubController (reference pattern)
- [x] `plugins/disrumpo/src/controller/controller.h` - Disrumpo Controller (reference for createCustomView/createSubController)
- [x] `plugins/disrumpo/src/controller/views/spectrum_display.h` - Reference for real-time display custom view

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `HarmonicFrame` | `numPartials` is `int`, not `size_t` | Cast when indexing: `static_cast<size_t>(frame.numPartials)` |
| `Partial.amplitude` | Linear amplitude, not dB | Convert: `20 * log10(amplitude)` for dB display |
| `FieldsetContainer` | Takes `CRect` only (no listener/tag) | It's a `CViewContainer` subclass, not a `CControl` |
| `ActionButton` | Momentary (triggers on click-release) | Bound to `kMemoryCaptureId`/`kMemoryRecallId` (stepCount=1) |
| `CSegmentButton` | Bound to `StringListParameter` | Ensure stepCount matches segment count in XML |
| `createSubController` | Called once per template instance in XML order | Use counter + reset in `willClose()` for modulator index |

## Layer 0 Candidate Analysis

*Not applicable - this is a UI feature, not a DSP feature. No Layer 0 utilities to extract.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

**Decision**: No Layer 0 extraction needed. All utilities are UI-specific.

## SIMD Optimization Analysis

*Not applicable - this is a UI feature with no DSP processing.*

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This spec implements UI controls and display views. The only computation is dB conversion for spectral bar heights (`20 * log10f(amplitude)`), which runs on the UI thread at 33fps for 48 values -- trivially cheap. No SIMD analysis needed.

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin UI (controller-side)

**Related features at same layer**: Disrumpo UI, Iterum UI, Ruinae UI (all existing)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `HarmonicDisplayView` | LOW | Innexus only | Keep local |
| `ConfidenceIndicatorView` | MEDIUM | Any pitch-tracking plugin | Keep local, extract if 2nd use |
| `MemorySlotStatusView` | MEDIUM | Any slot-based plugin | Keep local, extract if 2nd use |
| `EvolutionPositionView` | LOW | Innexus only | Keep local |
| `ModulatorActivityView` | LOW | Innexus only | Keep local |
| `ModulatorSubController` | MEDIUM | Any plugin with repeated modulator sections | Keep local, extract if 2nd use |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep all custom views in `plugins/innexus/src/controller/views/` | First plugin needing these specific views; no confirmed 2nd consumer |
| Keep `ModulatorSubController` in `plugins/innexus/src/controller/` | Sub-controller is tightly bound to Innexus parameter IDs |

## Project Structure

### Documentation (this feature)

```text
specs/121-plugin-ui/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
\-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
plugins/innexus/
+-- src/
|   +-- controller/
|   |   +-- controller.h              # Extended with VST3EditorDelegate, custom views
|   |   +-- controller.cpp            # Extended with createView, createCustomView, createSubController, notify, didOpen, willClose
|   |   +-- modulator_sub_controller.h # DelegationController for Mod template remapping
|   |   \-- views/
|   |       +-- harmonic_display_view.h    # 48-bar spectral display (FR-009..FR-012)
|   |       +-- harmonic_display_view.cpp
|   |       +-- confidence_indicator_view.h # F0 confidence meter (FR-013..FR-015)
|   |       +-- confidence_indicator_view.cpp
|   |       +-- memory_slot_status_view.h   # 8-slot occupied indicator (FR-029)
|   |       +-- memory_slot_status_view.cpp
|   |       +-- evolution_position_view.h   # Horizontal track + playhead (FR-036)
|   |       +-- evolution_position_view.cpp
|   |       +-- modulator_activity_view.h   # Animated modulation indicator (FR-038, FR-040)
|   |       \-- modulator_activity_view.cpp
|   \-- processor/
|       +-- processor.h                # Extended: sendDisplayData() helper, display buffer
|       \-- processor.cpp              # Extended: IMessage sending in process()
+-- resources/
|   \-- editor.uidesc                  # Full UI definition (replaces placeholder)
\-- tests/
    \-- unit/
        \-- controller/
            +-- test_harmonic_display_view.cpp
            +-- test_confidence_indicator_view.cpp
            +-- test_memory_slot_status_view.cpp
            +-- test_evolution_position_view.cpp
            +-- test_modulator_activity_view.cpp
            +-- test_modulator_sub_controller.cpp
            \-- test_controller_ui.cpp
```

**Structure Decision**: Custom views go under `plugins/innexus/src/controller/views/` following the Disrumpo pattern. The sub-controller goes directly in `controller/` as it's a single file. Tests go in the existing `tests/unit/controller/` directory.

## Complexity Tracking

No constitution violations. No complexity tracking needed.

---

# Phase 0: Research

## Research Tasks

### R1: IMessage Protocol for Display Data

**Question**: What is the best structure for sending display data from Processor to Controller via IMessage?

**Decision**: Use a flat POD struct (`DisplayData`) copied atomically via a double-buffer. The processor writes display data at the end of each `process()` call into a lock-free atomic buffer. The controller's `notify()` handler receives IMessage and copies the struct to a local buffer. The 30ms timer reads from this buffer.

**Rationale**: The Disrumpo spectrum display uses raw FIFO pointers passed via IMessage (shared memory), which is efficient but couples processor lifetime to controller. For Innexus, the display data is small enough (48 amplitudes + metadata = ~250 bytes) that copying via IMessage is simpler and safer. The processor already has `notify()` for JSON import; extending it for display data is natural.

**Alternatives considered**:
- Shared FIFO (Disrumpo pattern): More complex, requires careful lifetime management. Overkill for 250 bytes.
- Raw pointer via IMessage: Fragile, requires synchronization. Disrumpo does this but it's a known pain point.
- Parameter-based (abuse parameters for display): Would require 50+ display-only parameters. Host would record them in automation. Rejected.

**Implementation Detail**: The `DisplayData` struct contains:
```cpp
struct DisplayData {
    float partialAmplitudes[48];     // Linear amplitudes for spectral display
    uint8_t partialActive[48];       // Which partials are active (for filtering dim)
    float f0;                        // Fundamental frequency (Hz)
    float f0Confidence;              // [0.0, 1.0]
    uint8_t slotOccupied[8];         // Memory slot status
    float evolutionPosition;         // Combined morph position [0.0, 1.0]
    float manualMorphPosition;       // Manual offset for ghost marker
    float mod1Phase;                 // Modulator 1 current phase [0.0, 1.0]
    float mod2Phase;                 // Modulator 2 current phase [0.0, 1.0]
    bool mod1Active;                 // Modulator 1 enabled & running
    bool mod2Active;                 // Modulator 2 enabled & running
    uint32_t frameCounter;           // Monotonic counter to detect new data
};
```

The processor populates this at the end of each `process()` call and sends it via `IMessage` with message ID `"DisplayData"`. The controller's `notify()` copies it to an `std::atomic`-guarded buffer. The 30ms timer reads it and calls `invalid()` on views that need updating.

### R2: CVSTGUITimer Pattern

**Decision**: Create a single shared `CVSTGUITimer` in `didOpen()`, stopped in `willClose()`. The timer callback iterates all custom views and calls `invalid()` on any that have new data (checked via `frameCounter`).

**Rationale**: One timer is simpler than per-view timers. Disrumpo uses a similar pattern for its sweep visualization timer. 30ms interval provides ~33fps which exceeds SC-003 (>=10fps) with headroom.

**Implementation**:
```cpp
// In didOpen():
displayTimer_ = VSTGUI::makeOwned<CVSTGUITimer>(
    [this](CVSTGUITimer*) { onDisplayTimerFired(); }, 30);

// In willClose():
if (displayTimer_) { displayTimer_->stop(); displayTimer_ = nullptr; }
```

### R3: Modulator Sub-Controller Pattern

**Decision**: Use `DelegationController` subclass with counter-based instantiation, following the Disrumpo `BandSubController` pattern exactly. The template is named `"modulator_panel"` with `sub-controller="ModulatorController"`. The counter (0 or 1) determines Mod1 vs Mod2 parameter mapping.

**Counter Reset Policy**: `modInstanceCounter_` is reset to 0 in `didOpen()` — this is the **primary** reset, because `didOpen()` fires just before VSTGUI begins instantiating views from the uidesc, so resetting here guarantees Mod 1 gets index 0 and Mod 2 gets index 1 on every editor open. A **defensive** reset also occurs in `willClose()` as a cleanup guard. The previously documented "reset in willClose" was incomplete — both locations are needed, with `didOpen()` being the authoritative one.

**Rationale**: This is the proven pattern in the codebase. Disrumpo's `BandSubController` handles 4 identical bands; our case is simpler (2 modulators).

**Tag Mapping**:

| Template Tag | Mod 0 (index 0) | Mod 1 (index 1) |
|---|---|---|
| `Mod.Enable` | `kMod1EnableId` (610) | `kMod2EnableId` (620) |
| `Mod.Waveform` | `kMod1WaveformId` (611) | `kMod2WaveformId` (621) |
| `Mod.Rate` | `kMod1RateId` (612) | `kMod2RateId` (622) |
| `Mod.Depth` | `kMod1DepthId` (613) | `kMod2DepthId` (623) |
| `Mod.RangeStart` | `kMod1RangeStartId` (614) | `kMod2RangeStartId` (624) |
| `Mod.RangeEnd` | `kMod1RangeEndId` (615) | `kMod2RangeEndId` (625) |
| `Mod.Target` | `kMod1TargetId` (616) | `kMod2TargetId` (626) |

The offset is simply `modIndex * 10` added to the Mod1 base IDs (610).

### R4: Custom View Registration Pattern

**Decision**: All 5 custom views are instantiated via `Controller::createCustomView()` using string name matching, following the Disrumpo pattern. View names used in `editor.uidesc`:

| uidesc `custom-view-name` | Class |
|---|---|
| `"HarmonicDisplay"` | `HarmonicDisplayView` |
| `"ConfidenceIndicator"` | `ConfidenceIndicatorView` |
| `"MemorySlotStatus"` | `MemorySlotStatusView` |
| `"EvolutionPosition"` | `EvolutionPositionView` |
| `"ModulatorActivity"` | `ModulatorActivityView` |

For the modulator activity views: the sub-controller's `verifyView()` will tag each instance with `modIndex_` so the controller knows which modulator's data to feed it.

### R5: Editor.uidesc Structure

**Decision**: The editor.uidesc will use:
- `FieldsetContainer` (custom class) for section borders with titles
- `CViewContainer` for internal layout grouping
- VSTGUI `<template>` for the modulator panel (instantiated twice with `sub-controller`)
- `<view class="..." custom-view-name="..."/>` for custom views
- All `control-tag` attributes match parameter names registered via `controller.cpp`

**Layout Zones** (800x600):

| Zone | Y Range | Content |
|------|---------|---------|
| Header | 0-50 | Title, Bypass, Master Gain, Input Source, Latency Mode |
| Display | 50-200 | Spectral Display (500px wide), F0 Confidence (150px) |
| Middle | 200-340 | Musical Control (left 380px), Oscillator+Residual (right 420px) |
| Bottom | 340-600 | Memory, Cross/Stereo, Evolution, Detune, Modulators, Blend |

### R6: Note Name Conversion for F0 Display (FR-015)

**Decision**: Implement a simple `freqToNoteName()` utility inline in the confidence view. Formula: `midiNote = 12 * log2(freq / 440) + 69`, then map to note name + octave.

**Rationale**: This is a display-only utility with no DSP implications. Too specialized for shared components.

---

# Phase 1: Design & Contracts

## Data Model

See [data-model.md](data-model.md) for the detailed entity/struct definitions.

### Key Entities

1. **DisplayData** - POD struct for processor-to-controller display data transfer via IMessage
2. **HarmonicDisplayView** - Custom CView for 48-bar spectral visualization
3. **ConfidenceIndicatorView** - Custom CView for F0 confidence meter with note name
4. **MemorySlotStatusView** - Custom CView for 8-slot occupied/empty indicators
5. **EvolutionPositionView** - Custom CView for horizontal playhead track
6. **ModulatorActivityView** - Custom CView for modulation waveform animation
7. **ModulatorSubController** - DelegationController for modulator template tag remapping

### Parameter-to-Control Mapping

All 48 parameters mapped to UI controls:

| Parameter | Control Type | Section |
|-----------|-------------|---------|
| `kBypassId` | `ToggleButton` | Header |
| `kMasterGainId` | `ArcKnob` | Header |
| `kInputSourceId` | `CSegmentButton` (2) | Header |
| `kLatencyModeId` | `CSegmentButton` (2) | Header |
| `kReleaseTimeId` | `ArcKnob` | Oscillator |
| `kInharmonicityAmountId` | `ArcKnob` | Oscillator |
| `kHarmonicLevelId` | `ArcKnob` | Residual |
| `kResidualLevelId` | `ArcKnob` | Residual |
| `kResidualBrightnessId` | `BipolarSlider` | Residual |
| `kTransientEmphasisId` | `ArcKnob` | Residual |
| `kFreezeId` | `ToggleButton` (prominent) | Musical Control |
| `kMorphPositionId` | `ArcKnob` | Musical Control |
| `kHarmonicFilterTypeId` | `CSegmentButton` (5) | Musical Control |
| `kResponsivenessId` | `ArcKnob` | Musical Control |
| `kMemorySlotId` | `COptionMenu` (8) | Memory |
| `kMemoryCaptureId` | `ActionButton` | Memory |
| `kMemoryRecallId` | `ActionButton` | Memory |
| `kTimbralBlendId` | `ArcKnob` | Cross-Synthesis |
| `kStereoSpreadId` | `ArcKnob` | Stereo |
| `kEvolutionEnableId` | `ToggleButton` | Evolution |
| `kEvolutionSpeedId` | `ArcKnob` | Evolution |
| `kEvolutionDepthId` | `ArcKnob` | Evolution |
| `kEvolutionModeId` | `CSegmentButton` (3) | Evolution |
| `kMod1EnableId` | `ToggleButton` | Mod 1 (template) |
| `kMod1WaveformId` | `COptionMenu` (5) | Mod 1 (template) |
| `kMod1RateId` | `ArcKnob` | Mod 1 (template) |
| `kMod1DepthId` | `ArcKnob` | Mod 1 (template) |
| `kMod1RangeStartId` | `ArcKnob` | Mod 1 (template) |
| `kMod1RangeEndId` | `ArcKnob` | Mod 1 (template) |
| `kMod1TargetId` | `CSegmentButton` (3) | Mod 1 (template) |
| `kMod2EnableId` | `ToggleButton` | Mod 2 (template) |
| `kMod2WaveformId` | `COptionMenu` (5) | Mod 2 (template) |
| `kMod2RateId` | `ArcKnob` | Mod 2 (template) |
| `kMod2DepthId` | `ArcKnob` | Mod 2 (template) |
| `kMod2RangeStartId` | `ArcKnob` | Mod 2 (template) |
| `kMod2RangeEndId` | `ArcKnob` | Mod 2 (template) |
| `kMod2TargetId` | `CSegmentButton` (3) | Mod 2 (template) |
| `kDetuneSpreadId` | `ArcKnob` | Detune |
| `kBlendEnableId` | `ToggleButton` | Blend |
| `kBlendSlotWeight1Id`..`8` | `ArcKnob` x8 | Blend |
| `kBlendLiveWeightId` | `ArcKnob` | Blend |

**Total**: 48 parameters, 48 controls. SC-001 satisfied by design.

## Contracts

> **Authoritative Definitions**: The `contracts/` directory contains the authoritative versions of all protocol and interface definitions for this spec (`imessage-protocol.md`, `custom-views.md`, `modulator-template.md`). The summaries below are kept for quick reference; in case of any discrepancy, the contracts/ files take precedence over this plan.

### IMessage Protocol Contract

**Message ID**: `"DisplayData"`

**Direction**: Processor -> Controller

**Payload**: Binary blob containing `DisplayData` struct (see R1 above). Sent via `IAttributeList::setBinary("data", &displayData, sizeof(DisplayData))`.

**Frequency**: Once per `process()` call that produces new analysis output. Not every buffer -- only when the harmonic frame changes.

**Controller Reception**: `Controller::notify()` checks message ID, extracts binary, copies to local `DisplayData cachedDisplayData_`. Sets `displayDataReady_ = true`.

### Custom View Interface Contract

All custom views share a common pattern:

```cpp
class SomeView : public VSTGUI::CView {
public:
    explicit SomeView(const VSTGUI::CRect& size);
    void draw(VSTGUI::CDrawContext* context) override;
    // Data setter called from timer callback:
    void updateData(const DisplayData& data);
    CLASS_METHODS_NOCOPY(SomeView, CView)
};
```

Each view's `updateData()` copies relevant fields from `DisplayData` into local state and calls `invalid()` to trigger redraw.

### Controller Extension Contract

The `Innexus::Controller` class will be extended to implement `VSTGUI::VST3EditorDelegate`:

```cpp
class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate
{
public:
    // Existing methods unchanged...

    // New VST3EditorDelegate methods:
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    VSTGUI::CView* createCustomView(VSTGUI::UTF8StringPtr name, ...) override;
    VSTGUI::IController* createSubController(VSTGUI::UTF8StringPtr name, ...) override;
    void didOpen(VSTGUI::VST3Editor* editor) override;
    void willClose(VSTGUI::VST3Editor* editor) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;
};
```

### Processor Extension Contract

The `Innexus::Processor` will add display data sending:

```cpp
// New method in Processor:
void sendDisplayData(Steinberg::Vst::ProcessData& data);
// Called at end of process() when new frame data is available.
// Uses allocateMessage() + sendMessage() API.
```
