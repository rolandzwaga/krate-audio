# Implementation Plan: Morph UI & Type-Specific Parameters

**Branch**: `006-morph-ui` | **Date**: 2026-01-28 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/006-morph-ui/spec.md`

---

## Summary

Implement the complete Morph UI system for Disrumpo including:
1. **MorphPad custom control** - A 2D XY pad for controlling morph position with node rendering, cursor interaction, and connection lines
2. **BandStripExpanded template** - Expanded band view with morph controls, type-specific parameters, and output section
3. **UIViewSwitchContainer** - 26 type-specific parameter templates that switch based on distortion type selection
4. **Morph-Sweep Linking** - UI controls for linking morph position to sweep frequency with 7 mapping modes

This spec covers Week 7 of the Disrumpo roadmap (tasks T7.1-T7.43).

---

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 12+, GCC 10+)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.11+, existing Disrumpo infrastructure
**Storage**: N/A (parameters persisted via VST3 state mechanism)
**Testing**: Catch2 for unit tests, manual UI verification, pluginval validation
**Target Platform**: Windows 10/11 (primary), macOS 11+, Linux
**Project Type**: VST3 plugin (monorepo structure)
**Performance Goals**: < 16ms frame time (60fps UI), < 12ms parameter latency
**Constraints**: Zero allocations after initialization, thread-safe parameter updates
**Scale/Scope**: 41 functional requirements, 26 type-specific templates, 3 morph modes

---

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle V (VSTGUI Development):**
- [x] Use UIDescription XML for layout (editor.uidesc)
- [x] Implement VST3EditorDelegate for custom view creation (MorphPad)
- [x] UI thread MUST NEVER directly access audio processing data
- [x] Use IParameterChanges for UI to Processor updates

**Required Check - Principle VI (Cross-Platform Compatibility):**
- [x] NEVER use Windows-native APIs or macOS-native APIs for UI
- [x] Use VSTGUI cross-platform abstractions (CControl, COptionMenu, etc.)
- [x] All UI components MUST work on Windows, macOS, and Linux

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVII (Framework Knowledge):**
- [x] The vst-guide skill auto-loads for VSTGUI/VST3 work
- [x] Document non-obvious framework behavior in incident log

---

## Codebase Research (Principle XIV - ODR Prevention)

### Mandatory Searches Performed

**Classes/Structs to be created**: MorphPad (custom control)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| MorphPad | `grep -r "class MorphPad" plugins/` | No (placeholder only) | Create New |
| BandStripExpanded | uidesc template search | No | Create New (XML template) |
| TypeParams_* | uidesc template search | No | Create New (26 XML templates) |
| MorphLinkMode | `grep -r "MorphLinkMode" plugins/` | No | Create New (enum) |

**Utility Functions to be created**: Node weight visualization, coordinate conversion

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| getCategoryColor | `grep -r "getCategoryColor" plugins/` | No | - | Create in MorphPad |
| positionToPixel | Already in CXYPad | Reference | vstgui/lib/controls/cxypad.h | Reference pattern |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| MorphEngine | plugins/disrumpo/src/dsp/morph_engine.h | Plugin DSP | Weight computation reference for UI |
| MorphNode | plugins/disrumpo/src/dsp/morph_node.h | Plugin DSP | Node data structure |
| MorphMode | plugins/disrumpo/src/dsp/morph_node.h | Plugin DSP | Mode enum (Linear1D, Planar2D, Radial2D) |
| DistortionFamily | plugins/disrumpo/src/dsp/distortion_types.h | Plugin DSP | Category colors |
| VisibilityController | plugins/disrumpo/src/controller/controller.cpp | Controller | IDependent pattern for band visibility |
| ContainerVisibilityController | plugins/disrumpo/src/controller/controller.cpp | Controller | Container show/hide pattern |
| CXYPad | vstgui4/lib/controls/cxypad.h | VSTGUI | Reference for MorphPad implementation |
| UIViewSwitchContainer | vstgui4/uidescription/uiviewswitchcontainer.h | VSTGUI | Type-specific panel switching |

### Files Checked for Conflicts

- [x] `plugins/disrumpo/src/controller/controller.h` - Existing Controller class
- [x] `plugins/disrumpo/src/controller/controller.cpp` - createCustomView() placeholder exists
- [x] `plugins/disrumpo/src/dsp/morph_engine.h` - MorphEngine already implemented
- [x] `plugins/disrumpo/src/dsp/morph_node.h` - MorphNode, MorphMode already defined
- [x] `plugins/disrumpo/src/dsp/distortion_types.h` - DistortionFamily enum exists
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/controls/cxypad.h` - CXYPad reference

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: MorphPad is a new custom control with unique name. All supporting enums (MorphMode, DistortionFamily) already exist in DSP layer. The controller already has createCustomView() with a placeholder for "MorphPad" returning nullptr - we implement this.

---

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| MorphEngine | getWeights() | `[[nodiscard]] const std::array<float, kMaxMorphNodes>& getWeights() const noexcept` | Yes |
| MorphEngine | getSmoothedX() | `[[nodiscard]] float getSmoothedX() const noexcept` | Yes |
| MorphEngine | getSmoothedY() | `[[nodiscard]] float getSmoothedY() const noexcept` | Yes |
| MorphNode | position | `CPoint position = {0.0f, 0.0f}` (per custom-controls.md) | Yes |
| MorphNode | type | `DistortionType type = DistortionType::SoftClip` | Yes |
| CControl | getValue() | `virtual float PLUGIN_API getValue() const` | Yes |
| CControl | setValue() | `virtual void PLUGIN_API setValue(float val)` | Yes |
| CControl | valueChanged() | `virtual void valueChanged()` | Yes |
| CView | invalid() | `void invalid()` | Yes |
| EditControllerEx1 | getParamNormalized() | `Steinberg::Vst::ParamValue getParamNormalized(Steinberg::Vst::ParamID tag)` | Yes |
| EditControllerEx1 | setParamNormalized() | `Steinberg::tresult setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value)` | Yes |

### Header Files Read

- [x] `plugins/disrumpo/src/dsp/morph_engine.h` - MorphEngine public API
- [x] `plugins/disrumpo/src/dsp/morph_node.h` - MorphNode structure
- [x] `plugins/disrumpo/src/controller/controller.h` - Controller class
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/controls/ccontrol.h` - CControl base
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/controls/cxypad.h` - CXYPad reference

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| CControl | Value is normalized 0-1 | Use getValue()/setValue() with 0.0-1.0 range |
| MouseDownEvent | New event API in VSTGUI 4.11+ | Use `event.mousePosition`, `event.modifiers.has()`, `event.consumed = true` |
| UIViewSwitchContainer | template-switch-control binding | Attribute `template-switch-control="tagName"` in uidesc |
| IDependent | Must deactivate before destruction | Call `controller->unregisterDependent()` in willClose() |

---

## Layer 0 Candidate Analysis

**Utilities to Extract to Layer 0**: None identified

This feature is entirely UI-layer (Controller + VSTGUI). No DSP utilities needed - morph weight calculation already exists in MorphEngine.

**Decision**: All new code stays in Controller layer. No Layer 0 extraction needed.

---

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin Controller (UI Layer)

**Related features at same layer**:
- 008 (Week 9-10 Modulation) - Will need modulation routing matrix UI
- 013 (Week 12-13 Spectrum Display) - SpectrumDisplay custom control

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| MorphPad | MEDIUM | Could inform XY pads in modulation | Keep local |
| VisibilityController pattern | HIGH | Already reused, established pattern | Already shared |
| UIViewSwitchContainer pattern | HIGH | Modulation routing, preset browser | Document pattern |
| Node editor panel | MEDIUM | Modulation source selectors | Keep local, review after 008 |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| MorphPad stays in Disrumpo | Plugin-specific control, not reusable across plugins |
| Document UIViewSwitchContainer pattern | Valuable for future multi-template UIs |

---

## Project Structure

### Documentation (this feature)

```text
specs/006-morph-ui/
├── plan.md              # This file
├── research.md          # Phase 0 output (created during planning)
├── spec.md              # Feature specification
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
plugins/disrumpo/
├── src/
│   ├── controller/
│   │   ├── controller.h           # Add MorphPad support
│   │   ├── controller.cpp         # Update createCustomView(), didOpen/willClose
│   │   └── views/
│   │       └── morph_pad.h/.cpp   # NEW: MorphPad custom control
│   └── dsp/
│       ├── morph_engine.h         # EXISTS: Reference for weight visualization
│       └── morph_node.h           # EXISTS: Node data structures
├── resources/
│   └── editor.uidesc              # Add BandStripExpanded, 26 TypeParams_* templates
└── tests/
    └── unit/
        └── morph_pad_test.cpp     # NEW: MorphPad coordinate/hit testing
```

**Structure Decision**: Follow existing Disrumpo plugin structure. MorphPad goes in `src/controller/views/` alongside existing SpectrumDisplay. Templates added to existing `editor.uidesc`.

---

## Architecture Decisions

### AD-001: MorphPad Inherits from CControl

**Decision**: MorphPad will inherit from `VSTGUI::CControl` rather than `CView` or `CXYPad`.

**Rationale**:
- CControl provides `value`/`getValue()`/`setValue()` for parameter binding
- Unlike CXYPad which packs X/Y into single float, we need separate X/Y parameters
- CControl integrates with VST3 parameter system via control-tags

**Implementation**:
- MorphPad internally tracks two values (morphX_, morphY_)
- Uses two control-tags: `band*-morph-x` and `band*-morph-y`
- Listener interface notifies controller of position changes

### AD-002: UIViewSwitchContainer for Type Parameters

**Decision**: Use VSTGUI's built-in UIViewSwitchContainer bound to Band*Node*Type parameter.

**Rationale**:
- UIViewSwitchContainer is designed exactly for this use case
- `template-switch-control` attribute binds directly to parameter value
- No custom code needed for template switching

**Implementation**:
- 26 templates named `TypeParams_SoftClip` through `TypeParams_AllpassRes`
- Each template contains only type-specific controls (Drive/Mix/Tone are outside)
- Container bound to Band*Node*Type parameter via control-tag

### AD-003: Expand/Collapse via Visibility Controllers

**Decision**: Reuse existing VisibilityController pattern from band count visibility.

**Rationale**:
- Pattern already implemented and tested in controller.cpp
- IDependent mechanism provides thread-safe parameter observation
- Matches existing codebase patterns

**Implementation**:
- Add per-band expand state as Boolean parameter (Band*Expanded)
- Create ExpandedVisibilityController observing this parameter
- Controller manages lifecycle in didOpen()/willClose()

### AD-004: Cross-Family Morph Visualization

**Decision**: For cross-family morphing, display both type panels side-by-side with 50/50 split and opacity fading.

**Rationale**:
- Spec FR-038 requires side-by-side equal split layout
- Opacity proportional to morph weight provides visual feedback
- Below 10% weight, panel can collapse entirely (FR-039)

**Implementation**:
- When nodes are different families, show both TypeParams panels
- Use CLayeredViewContainer or custom container
- Opacity controlled by morph weights from MorphEngine

### AD-005: Morph Link Mode Enum

**Decision**: Create MorphLinkMode enum in plugin_ids.h alongside parameter definitions.

**Rationale**:
- 7 link modes: None, Sweep Freq, Inverse, EaseIn, EaseOut, Hold-Rise, Stepped
- Enum provides type safety and documentation
- Matches existing DistortionType enum pattern

---

## Technical Specifications

### Parameter Naming Convention

**Spec/Plan references**: Use `Band*Node*ParamName` with asterisk wildcards for general references
**Task descriptions**: Use `Band{b}Node{n}ParamName` with curly brace placeholders when describing iteration patterns
**Code implementation**: Use `Band0Node0ParamName` for actual parameter IDs in plugin_ids.h

Examples:
- Spec: "Wire to `Band*MorphX` parameter"
- Task: "Create parameters `Band{b}MorphX` for all bands (b=0-3)"
- Code: `kBand0MorphXId`, `kBand1MorphXId`, etc.

### Radial Grid Visual Specification (FR-009)

When MorphPad is in 2D Radial mode, display a radial grid overlay:

```
Radial Grid Properties:
- Concentric circles: 4 rings at r = 0.25, 0.5, 0.75, 1.0 (normalized)
- Radial lines: 8 lines at 45° intervals (0°, 45°, 90°, 135°, 180°, 225°, 270°, 315°)
- Line color: RGBA(255, 255, 255, 64) - white with 25% opacity
- Line width: 1px
- Center point: marked with 4px filled circle
```

### Morph Smoothing Algorithm (FR-031)

**Algorithm**: First-order exponential smoothing (target-chase)

```cpp
// Per-sample update (called at UI refresh rate ~60Hz)
void updateSmoothedPosition(float targetX, float targetY, float smoothingMs) {
    if (smoothingMs <= 0.0f) {
        // Instant - no smoothing
        smoothedX_ = targetX;
        smoothedY_ = targetY;
        return;
    }

    // Time constant: smoothingMs represents time to reach ~63% of target
    const float refreshRate = 60.0f; // Hz
    const float alpha = 1.0f - std::exp(-1000.0f / (smoothingMs * refreshRate));

    smoothedX_ += alpha * (targetX - smoothedX_);
    smoothedY_ += alpha * (targetY - smoothedY_);
}
```

**Behavior**:
- 0ms: Instant response (alpha = 1.0)
- 100ms: Fast chase (~6 frames to 63%)
- 500ms: Slow glide (~30 frames to 63%)

### Morph Link Mode Equations (FR-034a-e)

**Input**: `sweepNorm` = normalized sweep frequency position [0, 1] where 0 = 20Hz, 1 = 20kHz (log scale)

```cpp
float applyLinkMode(MorphLinkMode mode, float sweepNorm) {
    switch (mode) {
        case MorphLinkMode::None:
            return manualPosition_;  // No change

        case MorphLinkMode::SweepFreq:
            // Linear: low freq → 0, high freq → 1
            return sweepNorm;

        case MorphLinkMode::InverseSweep:
            // Inverted: high freq → 0, low freq → 1
            return 1.0f - sweepNorm;

        case MorphLinkMode::EaseIn:
            // Exponential emphasizing low frequencies
            // More range in bass (0-0.3 of sweep → 0-0.6 of morph)
            return std::pow(sweepNorm, 0.5f);

        case MorphLinkMode::EaseOut:
            // Exponential emphasizing high frequencies
            // More range in highs (0.7-1.0 of sweep → 0.4-1.0 of morph)
            return std::pow(sweepNorm, 2.0f);

        case MorphLinkMode::HoldRise:
            // Hold at 0 until midpoint, then rise linearly to 1
            return sweepNorm < 0.5f ? 0.0f : (sweepNorm - 0.5f) * 2.0f;

        case MorphLinkMode::Stepped:
            // Quantize to 5 discrete steps
            return std::floor(sweepNorm * 5.0f) / 4.0f;  // 0, 0.25, 0.5, 0.75, 1.0
    }
}
```

### Cross-Family Opacity Fading Mechanism (AD-004 Detail)

**Implementation approach**: Use `CView::setAlphaValue()` on the UIViewSwitchContainer instances

```cpp
// In BandStripExpanded update logic:
void updateCrossFamilyVisualization(float weightA, float weightB, bool isCrossFamily) {
    if (!isCrossFamily) {
        // Same-family: show only primary panel at full opacity
        primaryTypeContainer_->setAlphaValue(1.0f);
        secondaryTypeContainer_->setVisible(false);
        return;
    }

    // Cross-family: side-by-side with opacity fading
    secondaryTypeContainer_->setVisible(weightB >= 0.1f);  // FR-039: collapse below 10%

    primaryTypeContainer_->setAlphaValue(weightA);
    secondaryTypeContainer_->setAlphaValue(weightB);
}
```

**Layout**: Use fixed 50/50 split (CRowColumnView with equal column weights) when both panels visible.

---

## Component Breakdown

### Component 1: MorphPad Custom Control (T7.1-T7.10)

**Purpose**: 2D XY pad for controlling morph position with node visualization

**Files**:
- `plugins/disrumpo/src/controller/views/morph_pad.h`
- `plugins/disrumpo/src/controller/views/morph_pad.cpp`

**Key Features**:
- Node rendering: 12px filled circles with category colors
- Cursor rendering: 16px open circle, 2px white stroke
- Connection lines: Gradient from white to node color, opacity by weight
- Interaction: Click, drag, Shift+drag (fine), Alt+drag (node move), double-click (reset)
- Mode visualization: 1D Linear (horizontal constraint), 2D Planar, 2D Radial (grid overlay)
- Position label: "X: 0.00 Y: 0.00" at bottom-left

**Category Colors** (from custom-controls.md):
```cpp
const std::map<DistortionFamily, CColor> kCategoryColors = {
    {DistortionFamily::Saturation, CColor{0xFF, 0x6B, 0x35, 0xFF}},  // Orange
    {DistortionFamily::Wavefold,   CColor{0x4E, 0xCD, 0xC4, 0xFF}},  // Teal
    {DistortionFamily::Digital,    CColor{0x95, 0xE8, 0x6B, 0xFF}},  // Green
    {DistortionFamily::Rectify,    CColor{0xC7, 0x92, 0xEA, 0xFF}},  // Purple
    {DistortionFamily::Dynamic,    CColor{0xFF, 0xCB, 0x6B, 0xFF}},  // Yellow
    {DistortionFamily::Hybrid,     CColor{0xFF, 0x53, 0x70, 0xFF}},  // Red
    {DistortionFamily::Experimental, CColor{0x89, 0xDD, 0xFF, 0xFF}}, // Light Blue
};
```

### Component 2: BandStripExpanded Template (T7.11-T7.14)

**Purpose**: Expanded band view with full morph controls and type-specific parameters

**Size**: 680x280 per ui-mockups.md

**Sections**:
1. **Header** (reuse BandStripCollapsed)
   - Band name, Type dropdown, Solo/Bypass/Mute
   - Collapse button (-)

2. **Morph Section** (left)
   - Mini MorphPad (180x120)
   - Morph Mode selector (1D/2D/Radial)
   - Active Nodes selector (2/3/4)
   - Morph Smoothing knob (0-500ms)
   - Morph X Link dropdown
   - Morph Y Link dropdown

3. **Type-Specific Section** (center)
   - UIViewSwitchContainer bound to node type
   - 26 TypeParams_* templates

4. **Output Section** (right)
   - Gain knob (-24dB to +24dB)
   - Pan knob (-100% to +100%)
   - Solo/Bypass/Mute toggles (duplicated for convenience)

### Component 3: TypeParams Templates (T7.15-T7.41)

**Purpose**: 26 templates for type-specific parameters

**Template List** (from spec):

| Type | Template Name | Controls |
|------|---------------|----------|
| D01 Soft Clip | TypeParams_SoftClip | Curve, Knee (2 knobs) |
| D02 Hard Clip | TypeParams_HardClip | Threshold, Ceiling (2 knobs) |
| D03 Tube | TypeParams_Tube | Bias, Sag, Stage (2 knobs, 1 dropdown) |
| D04 Tape | TypeParams_Tape | Bias, Sag, Speed, Model, HF Roll, Flutter (4 knobs, 1 dropdown) |
| D05 Fuzz | TypeParams_Fuzz | Bias, Gate, Transistor, Octave, Sustain (4 knobs, 1 dropdown) |
| D06 Asym Fuzz | TypeParams_AsymFuzz | Bias, Asymmetry, Transistor, Gate, Sustain, Body (5 knobs, 1 dropdown) |
| D07 Sine Fold | TypeParams_SineFold | Folds, Symmetry, Shape, Bias, Smooth (5 knobs) |
| D08 Triangle Fold | TypeParams_TriFold | Folds, Symmetry, Angle, Bias, Smooth (5 knobs) |
| D09 Serge Fold | TypeParams_SergeFold | Folds, Symmetry, Model, Bias, Shape, Smooth (5 knobs, 1 dropdown) |
| D10 Full Rectify | TypeParams_FullRectify | Smooth, DC Block (1 knob, 1 toggle) |
| D11 Half Rectify | TypeParams_HalfRectify | Threshold, Smooth, DC Block (2 knobs, 1 toggle) |
| D12 Bitcrush | TypeParams_Bitcrush | Bit Depth, Dither, Mode, Jitter (3 knobs, 1 dropdown) |
| D13 Sample Reduce | TypeParams_SampleReduce | Rate Ratio, Jitter, Mode, Smooth (3 knobs, 1 dropdown) |
| D14 Quantize | TypeParams_Quantize | Levels, Dither, Smooth, Offset (4 knobs) |
| D15 Temporal | TypeParams_Temporal | Mode, Sensitivity, Curve, Attack, Release, Depth, Lookahead, Hold (6 knobs, 2 dropdowns) |
| D16 Ring Sat | TypeParams_RingSat | Mod Depth, Stages, Curve, Carrier, Bias, Carrier Freq (4 knobs, 2 dropdowns) |
| D17 Feedback | TypeParams_Feedback | Feedback, Delay, Curve, Filter, Filter Freq, Stages, Limiter, Limit Thresh (5 knobs, 3 dropdowns, 1 toggle) |
| D18 Aliasing | TypeParams_Aliasing | Downsample, Freq Shift, Pre-Filter, Feedback, Resonance (4 knobs, 1 toggle) |
| D19 Bitwise | TypeParams_Bitwise | Operation, Intensity, Pattern, Bit Range, Smooth (3 knobs, 1 dropdown, 1 range slider) |
| D20 Chaos | TypeParams_Chaos | Attractor, Speed, Amount, Coupling, X-Drive, Y-Drive, Smooth, Seed (7 knobs, 1 dropdown, 1 button) |
| D21 Formant | TypeParams_Formant | Vowel, Shift, Curve, Resonance, Bandwidth, Formants, Gender, Blend (6 knobs, 2 dropdowns) |
| D22 Granular | TypeParams_Granular | Grain Size, Density, Pitch Var, Drive Var, Position, Curve, Envelope, Spread, Freeze (7 knobs, 2 dropdowns, 1 toggle) |
| D23 Spectral | TypeParams_Spectral | Mode, FFT Size, Curve, Tilt, Thresh, Mag Bits, Freq Range, Phase Mode (4 knobs, 3 dropdowns, 1 range slider) |
| D24 Fractal | TypeParams_Fractal | Mode, Iterations, Scale, Curve, Freq Decay, Feedback, Blend, Depth (6 knobs, 2 dropdowns) |
| D25 Stochastic | TypeParams_Stochastic | Base Curve, Jitter, Jitter Rate, Coeff Noise, Drift, Seed, Correlation, Smooth (7 knobs, 1 dropdown, 1 button) |
| D26 Allpass Res | TypeParams_AllpassRes | Topology, Frequency, Feedback, Decay, Curve, Stages, Pitch Track, Damping (5 knobs, 2 dropdowns, 1 toggle) |

### Component 4: Node Editor Panel (T7.43)

**Purpose**: Show all active nodes with type selection capability

**Features**:
- List of active nodes (2-4 based on Active Nodes setting)
- Each row shows: Node letter (A/B/C/D), Type name, Category color indicator
- Click to select node for editing
- Selected node's parameters shown in UIViewSwitchContainer

### Component 5: Morph Link Controls (FR-032, FR-033, FR-034)

**Purpose**: Link morph X/Y to sweep frequency

**Link Modes**:
1. **None** - Manual control only
2. **Sweep Freq** - Linear mapping (low freq = 0, high freq = 1)
3. **Inverse Sweep** - Inverted mapping (high freq = 0, low freq = 1)
4. **EaseIn** - Exponential curve emphasizing low frequencies
5. **EaseOut** - Exponential curve emphasizing high frequencies
6. **Hold-Rise** - Hold at 0 until mid-point, then rise to 1
7. **Stepped** - Quantize to discrete steps (0, 0.25, 0.5, 0.75, 1.0)

---

## Implementation Phases

### Phase 0: Research (Complete in this plan)

- [x] Extract unknowns from Technical Context
- [x] Research VSTGUI UIViewSwitchContainer usage
- [x] Verify existing MorphEngine API
- [x] Document CControl event handling pattern

### Phase 1: MorphPad Custom Control (T7.1-T7.10)

**Duration**: ~42 hours

**Tasks**:
1. T7.1: Create MorphPad class shell (8h)
2. T7.2: Register in createCustomView() (2h)
3. T7.3: Implement node rendering (4h)
4. T7.4: Implement cursor and drag (8h)
5. T7.5: Implement connection lines (4h)
6. T7.6: Implement Shift+drag fine adjustment (2h)
7. T7.7: Implement Alt+drag node repositioning (4h)
8. T7.8: Implement morph mode visuals (4h)
9. T7.9: Wire to Band*MorphX/Y parameters (4h)
10. T7.10: Add morph mode selector (2h)

**Tests**:
- Unit: Coordinate conversion (positionToPixel, pixelToPosition)
- Unit: Node hit testing
- Manual: Drag interaction in plugin

### Phase 2: BandStripExpanded & Visibility (T7.11-T7.14)

**Duration**: ~20 hours

**Tasks**:
1. T7.11: Create BandStripExpanded template (8h)
2. T7.12: Implement expand/collapse toggle (4h)
3. T7.13: Create expanded visibility controllers (4h)
4. T7.14: Add mini MorphPad (4h)

**Tests**:
- Manual: Expand/collapse works per band
- Manual: Mini MorphPad binds to correct band

### Phase 3: UIViewSwitchContainer Setup (T7.15-T7.42)

**Duration**: ~49 hours

**Tasks**:
1. T7.15: Setup UIViewSwitchContainer (4h)
2. T7.16-T7.41: Create 26 TypeParams templates (42h total)
3. T7.42: Wire to Band*Node*Type (4h)

**Tests**:
- Manual: Type dropdown switches visible template
- Manual: All 26 templates display correctly
- Manual: Type-specific controls update parameters

### Phase 4: Node Editor & Linking (T7.43, FR-032-034)

**Duration**: ~16 hours

**Tasks**:
1. T7.43: Create node editor panel (8h)
2. FR-032/033: Add Morph X/Y Link dropdowns (4h)
3. FR-034: Implement link mode parameter wiring (4h)

**Tests**:
- Manual: Node selection changes visible parameters
- Manual: Link mode dropdown updates correctly

### Phase 5: Polish & Cross-Family Visualization

**Duration**: ~8 hours

**Tasks**:
1. FR-037/038/039: Cross-family visualization (6h)
2. FR-040: Scroll wheel interaction (2h)

**Total Estimated Duration**: ~135 hours (~3.4 weeks)

---

## Risk Considerations

### Risk 1: UIViewSwitchContainer Performance

**Risk**: Switching between 26 templates may cause lag or flicker.

**Mitigation**:
- Pre-create all templates at load time
- Use `animation-time="0"` to disable transition animation
- Test with all 26 types rapidly switching

### Risk 2: Cross-Family Visualization Complexity

**Risk**: Showing two TypeParams panels side-by-side may require significant custom layout.

**Mitigation**:
- Start with simpler implementation: show dominant type only
- Add side-by-side as enhancement if time permits
- FR-039 allows collapsing below 10% weight

### Risk 3: Parameter Binding for 26 Templates

**Risk**: Each template needs correct control-tag bindings; 450+ parameters.

**Mitigation**:
- Parameters already registered in Controller::initialize()
- Use systematic naming: Band{b}Node{n}{Param}
- Create template once, verify bindings, then clone

### Risk 4: MorphPad Event Handling Complexity

**Risk**: Multiple interaction modes (click, drag, Shift+drag, Alt+drag) may conflict.

**Mitigation**:
- Reference CXYPad implementation for patterns
- Test each interaction mode independently
- Use event.consumed to prevent propagation

---

## Testing Strategy

### Unit Tests

| Test | Location | Coverage |
|------|----------|----------|
| MorphPad coordinate conversion | `morph_pad_test.cpp` | positionToPixel(), pixelToPosition() |
| MorphPad hit testing | `morph_pad_test.cpp` | hitTestNode() |
| Link mode calculations | `morph_link_test.cpp` | All 7 link mode mappings |

### Integration Tests

| Test | Method |
|------|--------|
| MorphPad parameter binding | Load plugin, drag cursor, verify parameter values |
| UIViewSwitchContainer switching | Change type dropdown, verify template switches |
| Expand/collapse state | Save preset with expanded band, reload, verify state |
| IT-005: Morph automation | Record automation curve for Band1MorphX/Y in DAW, playback verifies cursor moves |

### Manual Tests

| Test | Acceptance |
|------|------------|
| All 26 type templates | Each template displays correct controls |
| Morph position persistence | Preset save/load preserves morph position |
| Mini MorphPad | Mini version (180x120) works same as full size |
| Link modes | Each link mode produces expected morph movement |
| DAW compatibility | MorphPad works correctly in Cubase, Ableton, Reaper (minimum) |

### Pluginval Validation

Run after all phases complete:
```bash
tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Disrumpo.vst3"
```

---

## Success Criteria Verification Plan

| Criteria | How to Verify |
|----------|---------------|
| SC-001: < 12ms parameter latency | Measure with profiler during drag |
| SC-002: No flicker on template switch | Visual inspection, rapid type changes |
| SC-003: < 100ms expand/collapse | Stopwatch or visual inspection |
| SC-004: All 26 templates correct | Manual comparison with ui-mockups.md |
| SC-005: MorphPad-only control | Use plugin without typing values |
| SC-006: Preset persistence | Save/load cycle verification |
| SC-007: Both sizes work | Test 250x200 and 180x120 |
| SC-008: Mode visuals correct | Visual inspection of all 3 modes |
| SC-009: Fine adjustment works | Shift+drag shows finer movement |
| SC-010: Node repositioning persists | Alt+drag, save preset, reload |
| SC-011: Smoothing affects audio | Test with 0ms vs 500ms, hear difference |
| SC-012: Link modes work | Set Sweep Freq, verify morph follows sweep |
| SC-013: Output controls work | Adjust Gain/Pan, verify audio change |
| SC-014: Position label updates | Real-time update during drag |

---

## Complexity Tracking

No constitution violations requiring justification.

---

## Appendix: Key File Locations

| File | Purpose |
|------|---------|
| `plugins/disrumpo/src/controller/views/morph_pad.h` | MorphPad header |
| `plugins/disrumpo/src/controller/views/morph_pad.cpp` | MorphPad implementation |
| `plugins/disrumpo/src/controller/controller.cpp` | createCustomView(), didOpen(), willClose() |
| `plugins/disrumpo/resources/editor.uidesc` | All XML templates |
| `plugins/disrumpo/src/plugin_ids.h` | Parameter IDs, MorphLinkMode enum |
| `specs/Disrumpo/custom-controls.md` | MorphPad specification reference |
| `specs/Disrumpo/ui-mockups.md` | Type-specific parameter layouts |
| `specs/Disrumpo/vstgui-implementation.md` | UIViewSwitchContainer patterns |
