# Implementation Plan: VSTGUI Infrastructure and Basic UI

**Branch**: `004-vstgui-infrastructure` | **Date**: 2026-01-28 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/004-vstgui-infrastructure/spec.md`

## Summary

Implement the complete VSTGUI infrastructure and Level 1 UI for Disrumpo, enabling visual control of multiband distortion. This covers Weeks 4-5 from the roadmap (T4.1-T5b.9) and achieves Milestone M3: Level 1 UI Functional.

Key deliverables:
- Extended plugin_ids.h with hex bit-field parameter encoding per dsp-details.md
- Full Controller implementation with VST3EditorDelegate, ~450 parameter registration
- SpectrumDisplay custom control (static band regions only - no FFT)
- Complete editor.uidesc with 24-color palette, control-tags, templates
- VisibilityController pattern for progressive band disclosure

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK 3.7.8+, VSTGUI 4.11+
**Storage**: IBStreamer for preset serialization
**Testing**: Catch2 for unit tests; pluginval level 5 for integration (Constitution Principle VIII)
**Target Platform**: Windows 10/11 (primary), macOS 11+, Linux (validation)
**Project Type**: VST3 plugin (monorepo structure)
**Performance Goals**: UI frame time < 16ms (60fps), editor open < 500ms (SC-001)
**Constraints**: Cross-platform VSTGUI only (Constitution Principle VI); zero allocations in audio thread
**Scale/Scope**: ~450 parameters, 8 bands x 4 nodes, 26 distortion types

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Controller and Processor are separate classes
- [x] Controller uses setComponentState() to sync FROM processor
- [x] No direct pointer sharing between components

**Principle V (VSTGUI Development):**
- [x] Use UIDescription XML for layout
- [x] Implement VST3EditorDelegate for custom views
- [x] All parameter values normalized (0.0-1.0) at VST boundary

**Principle VI (Cross-Platform Compatibility):**
- [x] NEVER use Win32/Cocoa APIs for UI
- [x] Use VSTGUI cross-platform abstractions only
- [x] SpectrumDisplay uses CView, not native drawing

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

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SpectrumDisplay | `grep -r "class SpectrumDisplay" plugins/` | No | Create New |
| VisibilityController | `grep -r "class VisibilityController" plugins/` | Yes (Iterum) | Copy and adapt pattern |
| ContainerVisibilityController | `grep -r "class ContainerVisibilityController" plugins/` | Yes (Iterum) | Copy and adapt pattern |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| makeBandParamId | `grep -r "makeBandParamId" plugins/` | Yes (disrumpo) | plugin_ids.h | Extend existing |
| makeNodeParamId | `grep -r "makeNodeParamId" plugins/` | No | plugin_ids.h | Create New |
| freqToX / xToFreq | `grep -r "freqToX" plugins/` | No | spectrum_display.h | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| VisibilityController pattern | plugins/iterum/src/controller/controller.cpp:145 | UI | Copy IDependent-based visibility pattern for band show/hide |
| ContainerVisibilityController | plugins/iterum/src/controller/controller.cpp:295 | UI | Copy for band strip container visibility |
| Iterum editor.uidesc | plugins/iterum/resources/editor.uidesc | UI | Reference for uidesc structure, segment buttons, gradients |
| RangeParameter | VST3 SDK | VST3 | For continuous parameters (Gain, Pan, Tone) |
| StringListParameter | VST3 SDK | VST3 | For discrete selectors (Type, Morph Mode) |
| Krate::DSP::FFT | dsp/include/krate/dsp/primitives/fft.h | DSP | NOT used in this spec (deferred to Week 13) |
| Krate::DSP::Window | dsp/include/krate/dsp/core/window_functions.h | DSP | NOT used in this spec (deferred to Week 13) |

### Files Checked for Conflicts

- [x] `plugins/disrumpo/src/plugin_ids.h` - Existing, will extend
- [x] `plugins/disrumpo/src/controller/controller.h` - Existing skeleton, will extend
- [x] `plugins/iterum/src/controller/controller.cpp` - Reference for visibility patterns
- [x] `dsp/include/krate/dsp/primitives/fft.h` - FFT deferred to Week 13

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are plugin-specific (Disrumpo namespace). VisibilityController/ContainerVisibilityController patterns will be copied and adapted into Disrumpo controller.cpp, not shared across plugins. No risk of namespace collision since each plugin has its own namespace.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| EditControllerEx1 | initialize | `tresult PLUGIN_API initialize(FUnknown* context) override` | Yes |
| EditControllerEx1 | parameters | `ParameterContainer parameters` (protected member) | Yes |
| RangeParameter | constructor | `RangeParameter(const TChar* title, ParamID tag, const TChar* units, ParamValue minPlain, ParamValue maxPlain, ParamValue defaultValuePlain, int32 stepCount, int32 flags, UnitID unitID)` | Yes |
| StringListParameter | appendString | `virtual void appendString(const String128 string)` | Yes |
| VST3EditorDelegate | createCustomView | `virtual CView* createCustomView(UTF8StringPtr name, const UIAttributes& attributes, const IUIDescription* description, VST3Editor* editor)` | Yes |
| CView | setVisible | `virtual void setVisible(bool state)` | Yes |
| Parameter | addDependent | `void addDependent(IDependent* dep)` | Yes |
| Parameter | deferUpdate | `void deferUpdate(int32 message = kChanged)` | Yes |

### Header Files Read

- [x] `extern/vst3sdk/public.sdk/source/vst/vsteditcontroller.h` - EditControllerEx1
- [x] `extern/vst3sdk/public.sdk/source/vst/vstparameters.h` - RangeParameter, StringListParameter
- [x] `extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.h` - VST3EditorDelegate
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/cview.h` - CView base class
- [x] `extern/vst3sdk/base/source/fobject.h` - FObject, IDependent

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Parameter | Must call addRef() before addDependent() | `param->addRef(); param->addDependent(this);` |
| VisibilityController | Must deactivate() before willClose() to prevent use-after-free | Call `vc->deactivate()` in willClose(), not destructor |
| UIViewSwitchContainer | Destroys child views on switch - never cache control pointers | Look up by tag on each update |
| StringListParameter | Index-to-normalized: `index / (count - 1)` | 4 items: index 0=0.0, 1=0.333, 2=0.667, 3=1.0 |
| editor.uidesc control-tag | Must be decimal string, not hex | `tag="3840"` not `tag="0x0F00"` |

## Layer 0 Candidate Analysis

**This feature's layer**: Presentation Layer (Controller + VSTGUI)

No Layer 0 DSP utilities needed for this UI-only spec.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Presentation Layer (VSTGUI Infrastructure)

**Related features at same layer** (from roadmap.md):
- 005-morph-system: Will add MorphPad custom control, expanded band strip
- 006-sweep-system: Will add SweepIndicator overlay to SpectrumDisplay
- 007-modulation-matrix: Will add modulation panel visibility control

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| VisibilityController | HIGH | 005 (morph visibility), 006 (sweep panel), 007 (mod panel) | Keep in controller.cpp, pattern already proven in Iterum |
| ContainerVisibilityController | HIGH | 005 (type-specific params), 007 (modulation sections) | Keep in controller.cpp |
| SpectrumDisplay base rendering | MEDIUM | 006 (adds sweep overlay), Week 13 (adds FFT) | Design for extension via virtual methods |
| Color palette (uidesc) | HIGH | All future specs | Define once in editor.uidesc |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep VisibilityController in controller.cpp | Plugin-specific patterns, not worth shared lib overhead |
| Static band regions first | FFT deferred to Week 13 per roadmap; simpler to extend later |
| Hex bit-field IDs | Spec clarification confirmed dsp-details.md is canonical |

## Project Structure

### Documentation (this feature)

```text
specs/004-vstgui-infrastructure/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output (parameter structure)
+-- quickstart.md        # Phase 1 output (dev setup)
+-- contracts/           # Phase 1 output (API contracts)
```

### Source Code (repository root)

```text
plugins/disrumpo/
+-- src/
|   +-- plugin_ids.h              # EXTEND: Add NodeParamType, makeNodeParamId(), sweep params
|   +-- controller/
|   |   +-- controller.h          # EXTEND: Add VST3EditorDelegate, visibility members
|   |   +-- controller.cpp        # EXTEND: Full parameter registration, createCustomView
|   |   +-- views/
|   |       +-- spectrum_display.h/.cpp  # NEW: Static band regions + crossover dividers
|   +-- processor/
|       +-- processor.h/.cpp      # NO CHANGES (already complete from 003)
+-- resources/
|   +-- editor.uidesc             # NEW: Complete UI definition
+-- tests/
    +-- unit/
        +-- parameter_encoding_test.cpp  # NEW: Test makeBandParamId, makeNodeParamId
        +-- spectrum_display_test.cpp    # NEW: Test coordinate conversions, hit testing
```

---

## Phase 0: Research

### Research Tasks

1. **R1: VSTGUI Visibility Pattern Verification**
   - Study Iterum VisibilityController implementation
   - Verify IDependent::update() thread safety
   - Document edge cases (editor close race condition)

2. **R2: Parameter ID Encoding Validation**
   - Verify hex bit-field scheme from dsp-details.md
   - Confirm no conflicts with existing Disrumpo IDs (0x0F00-0x0F03)
   - Calculate full ID space: Global (0x0Fxx), Sweep (0x0Exx), Band (0xFbpp), Node (0xNbpp)

3. **R3: editor.uidesc Best Practices**
   - Review Iterum editor.uidesc structure
   - Document control-tag decimal conversion
   - Review UIViewSwitchContainer for type-specific params (Week 7)

4. **R4: Color Palette Consolidation**
   - Canonical source: ui-mockups.md Section 3
   - Map all 24 colors to uidesc format
   - Verify band colors match spec exactly

**Output**: `specs/004-vstgui-infrastructure/research.md`

---

## Phase 1: Design

### 1.1 Data Model

**Parameter ID Encoding** (per dsp-details.md):

```cpp
// Bit Layout (16-bit ParamID):
// +--------+--------+--------+
// | 15..12 | 11..8  |  7..0  |
// |  node  |  band  | param  |
// +--------+--------+--------+
//
// Special Bands:
// - 0xF = Global parameters
// - 0xE = Sweep parameters
// - 0x0-0x7 = Per-band parameters (node nibble indicates band-level vs node-level)

// Global: 0x0Fxx (band=0xF, node=0x0)
enum GlobalParamType : uint8_t {
    kGlobalInputGain   = 0x00,  // 0x0F00 = 3840
    kGlobalOutputGain  = 0x01,  // 0x0F01 = 3841
    kGlobalMix         = 0x02,  // 0x0F02 = 3842
    kGlobalBandCount   = 0x03,  // 0x0F03 = 3843
    kGlobalOversample  = 0x04,  // 0x0F04 = 3844
};

// Sweep: 0x0Exx (band=0xE, node=0x0)
enum SweepParamType : uint8_t {
    kSweepEnable       = 0x00,  // 0x0E00 = 3584
    kSweepFrequency    = 0x01,  // 0x0E01 = 3585
    kSweepWidth        = 0x02,  // 0x0E02 = 3586
    kSweepIntensity    = 0x03,  // 0x0E03 = 3587
    kSweepMorphLink    = 0x04,  // 0x0E04 = 3588
    kSweepFalloff      = 0x05,  // 0x0E05 = 3589
};

// Per-band (node=0xF): 0xFbpp where b=band(0-7), pp=param
// NOTE: Values 0x05-0x07 reserved for future use
enum BandParamType : uint8_t {
    kBandGain         = 0x00,  // 0xF000 = 61440 (band 0)
    kBandPan          = 0x01,
    kBandSolo         = 0x02,
    kBandBypass       = 0x03,
    kBandMute         = 0x04,
    // 0x05-0x07 reserved
    kBandMorphX       = 0x08,
    kBandMorphY       = 0x09,
    kBandMorphMode    = 0x0A,
};

// Per-node: 0xNbpp where N=node(0-3), b=band(0-7), pp=param
enum NodeParamType : uint8_t {
    kNodeType         = 0x00,  // 0x0000 = node 0, band 0
    kNodeDrive        = 0x01,
    kNodeMix          = 0x02,
    kNodeTone         = 0x03,
    kNodeBias         = 0x04,
    kNodeFolds        = 0x05,
    kNodeBitDepth     = 0x06,
    // 0x07-0x08 reserved for future MorphPad node positioning (deferred to spec 005)
};

// Helper functions
constexpr ParamID makeGlobalParamId(GlobalParamType param) {
    return 0x0F00 | static_cast<uint32_t>(param);
}

constexpr ParamID makeSweepParamId(SweepParamType param) {
    return 0x0E00 | static_cast<uint32_t>(param);
}

constexpr ParamID makeBandParamId(uint8_t band, BandParamType param) {
    return (0xF << 12) | (static_cast<uint32_t>(band) << 8) | static_cast<uint32_t>(param);
}

constexpr ParamID makeNodeParamId(uint8_t band, uint8_t node, NodeParamType param) {
    return (static_cast<uint32_t>(node) << 12) | (static_cast<uint32_t>(band) << 8) | static_cast<uint32_t>(param);
}
```

**Control-Tags** (decimal for uidesc):

| Parameter | Hex ID | Decimal Tag | Control-Tag Name |
|-----------|--------|-------------|------------------|
| Input Gain | 0x0F00 | 3840 | InputGain |
| Output Gain | 0x0F01 | 3841 | OutputGain |
| Global Mix | 0x0F02 | 3842 | GlobalMix |
| Band Count | 0x0F03 | 3843 | BandCount |
| Band 0 Gain | 0xF000 | 61440 | Band0Gain |
| Band 0 Pan | 0xF001 | 61441 | Band0Pan |
| Band 0 Solo | 0xF002 | 61442 | Band0Solo |
| Band 0 Node 0 Type | 0x0000 | 0 | Band0Node0Type |
| Band 0 Node 0 Drive | 0x0001 | 1 | Band0Node0Drive |

**Output**: `specs/004-vstgui-infrastructure/data-model.md`

### 1.2 API Contracts

**SpectrumDisplay API**:

```cpp
class SpectrumDisplay : public VSTGUI::CView {
public:
    explicit SpectrumDisplay(const VSTGUI::CRect& size);

    // Configuration (called from Controller)
    void setNumBands(int numBands);           // 1-8
    void setCrossoverFrequency(int index, float freqHz);  // 0-6 crossovers
    void setSelectedBand(int bandIndex);      // -1 = none
    void setBandState(int bandIndex, bool solo, bool mute, bool bypass);

    // CView overrides
    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;

    // Listener for crossover changes
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void onCrossoverChanged(int dividerIndex, float frequencyHz) = 0;
        virtual void onBandSelected(int bandIndex) = 0;
    };
    void setListener(Listener* listener);

private:
    // Coordinate conversion (log scale 20Hz-20kHz)
    float freqToX(float freqHz) const;
    float xToFreq(float x) const;

    // Hit testing
    int hitTestDivider(const VSTGUI::CPoint& where) const;  // Returns divider index or -1
    int hitTestBand(const VSTGUI::CPoint& where) const;     // Returns band index or -1
};
```

**Output**: `specs/004-vstgui-infrastructure/contracts/spectrum_display.h`

### 1.3 Quickstart

Development environment setup:

```bash
# 1. Configure (Windows)
cmake --preset windows-x64-release

# 2. Build
cmake --build build/windows-x64-release --config Release --target Disrumpo

# 3. Test parameter encoding
cmake --build build/windows-x64-release --config Release --target disrumpo_tests
build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe

# 4. Validate plugin
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"

# 5. Load in DAW (Reaper recommended for testing)
```

**Output**: `specs/004-vstgui-infrastructure/quickstart.md`

---

## Phase 2: Implementation Tasks

*Note: Tasks are organized by roadmap task IDs (T4.1-T5b.9). Each task includes test-first development per Constitution Principle XIII.*

### Week 4a: Plugin IDs and Controller Foundation (T4.1-T4.13)

**T4.1-T4.7: Parameter ID System (16h)**

Files: `plugins/disrumpo/src/plugin_ids.h`

1. Write unit tests for parameter ID encoding:
   - Test `makeGlobalParamId()` returns correct hex values
   - Test `makeSweepParamId()` returns 0x0E00-range values
   - Test `makeBandParamId(band=3, kBandGain)` returns 0xF300
   - Test `makeNodeParamId(node=2, band=1, kNodeDrive)` returns 0x2101
   - Test extraction functions recover band/node/param indices

2. Implement parameter enums and helper functions:
   - Add `GlobalParamType`, `SweepParamType`, `NodeParamType` enums
   - Add `makeGlobalParamId()`, `makeSweepParamId()`, `makeNodeParamId()` helpers
   - Add extraction functions: `extractNode()`, `isNodeParam()`, `isBandParam()`

3. Verify tests pass, commit: "feat(disrumpo): add hex bit-field parameter ID encoding (T4.1-T4.7)"

**T4.8-T4.13: Controller Foundation (32h)**

Files: `plugins/disrumpo/src/controller/controller.h`, `controller.cpp`

1. Extend Controller class declaration:
   - Add `VST3EditorDelegate` inheritance
   - Add `createCustomView()`, `didOpen()`, `willClose()` declarations
   - Add visibility controller members (8x band visibility)

2. Implement `registerGlobalParams()`:
   - Input Gain: RangeParameter [-24, +24] dB, default 0
   - Output Gain: RangeParameter [-24, +24] dB, default 0
   - Mix: RangeParameter [0, 100]%, default 100%
   - Band Count: StringListParameter ["1","2"..."8"], default "4"
   - Oversample Max: StringListParameter ["1x","2x","4x","8x"], default "4x"

3. Implement `registerSweepParams()`:
   - Enable: boolean toggle
   - Frequency: RangeParameter [20, 20000] Hz, log scale
   - Width: RangeParameter [0.5, 4.0] octaves
   - Intensity: RangeParameter [0, 100]%
   - Morph Link: StringListParameter ["None","Linear","Inverse","EaseIn"...]
   - Falloff: StringListParameter ["Hard","Soft"]

4. Implement `registerBandParams()` loop for 8 bands:
   - Gain, Pan, Solo, Bypass, Mute
   - MorphX, MorphY (RangeParameter [0,1])
   - MorphMode (StringListParameter ["1D Linear","2D Planar","2D Radial"])
   - ActiveNodes (StringListParameter ["2","3","4"])

5. Implement `registerNodeParams()` loop for 4 nodes x 8 bands:
   - Type: StringListParameter with 26 distortion type names
   - Drive, Mix, Tone, Bias, Folds, BitDepth (RangeParameter with type-appropriate ranges)

6. Verify plugin loads in DAW, commit: "feat(disrumpo): register ~450 parameters (T4.8-T4.13)"

### Week 4b: editor.uidesc Foundation (T4.14-T4.24)

**T4.14-T4.17: uidesc Skeleton (6h)**

Files: `plugins/disrumpo/resources/editor.uidesc`

1. Create XML skeleton with version header
2. Define `<colors>` section with 24 named colors per ui-mockups.md Section 3:
   ```xml
   <colors>
     <color name="background-primary" rgba="#1a1a1eff"/>
     <color name="background-secondary" rgba="#252529ff"/>
     <color name="accent-primary" rgba="#ff6b35ff"/>
     <color name="accent-secondary" rgba="#4ecdc4ff"/>
     <color name="text-primary" rgba="#ffffffff"/>
     <color name="text-secondary" rgba="#8888aaff"/>
     <color name="band-1" rgba="#ff6b35ff"/>
     <color name="band-2" rgba="#4ecdc4ff"/>
     <color name="band-3" rgba="#95e86bff"/>
     <color name="band-4" rgba="#c792eaff"/>
     <color name="band-5" rgba="#ffcb6bff"/>
     <color name="band-6" rgba="#ff5370ff"/>
     <color name="band-7" rgba="#89ddffff"/>
     <color name="band-8" rgba="#f78c6cff"/>
     <!-- ... remaining colors -->
   </colors>
   ```

3. Define `<fonts>` section (6 styles):
   ```xml
   <fonts>
     <font name="title-font" font-name="Segoe UI" size="18" bold="true"/>
     <font name="section-font" font-name="Segoe UI" size="14" bold="true"/>
     <font name="label-font" font-name="Segoe UI" size="10"/>
     <font name="value-font" font-name="Segoe UI" size="11"/>
     <font name="small-font" font-name="Segoe UI" size="9"/>
     <font name="band-font" font-name="Segoe UI" size="12" bold="true"/>
   </fonts>
   ```

4. Define `<gradients>` for buttons and panels

5. Commit: "feat(disrumpo): create editor.uidesc skeleton with colors/fonts (T4.14-T4.17)"

**T4.18-T4.23: Control Tags (14h)**

1. Define control-tags for global params (decimal values):
   ```xml
   <control-tags>
     <control-tag name="InputGain" tag="3840"/>       <!-- 0x0F00 -->
     <control-tag name="OutputGain" tag="3841"/>      <!-- 0x0F01 -->
     <control-tag name="GlobalMix" tag="3842"/>       <!-- 0x0F02 -->
     <control-tag name="BandCount" tag="3843"/>       <!-- 0x0F03 -->
     <control-tag name="OversampleMax" tag="3844"/>   <!-- 0x0F04 -->
   </control-tags>
   ```

2. Define control-tags for sweep params (0x0E00 range)

3. Define control-tags for band 0-7 params:
   ```xml
   <!-- Band 0: 0xF0xx = 61440+ -->
   <control-tag name="Band0Gain" tag="61440"/>
   <control-tag name="Band0Pan" tag="61441"/>
   <control-tag name="Band0Solo" tag="61442"/>
   <!-- ... -->
   ```

4. Define control-tags for node params (bands 0-7, nodes 0-3):
   ```xml
   <!-- Band 0, Node 0: 0x00xx = 0+ -->
   <control-tag name="Band0Node0Type" tag="0"/>
   <control-tag name="Band0Node0Drive" tag="1"/>
   <!-- ... -->
   ```

5. Define UI-only visibility tags (9000+ range):
   ```xml
   <control-tag name="Band0Container" tag="9000"/>
   <control-tag name="Band1Container" tag="9001"/>
   <!-- ... -->
   ```

6. Commit: "feat(disrumpo): define ~500 control-tags for parameter binding (T4.18-T4.23)"

**T4.24: Main Layout Template (6h)**

1. Create main `<template name="editor">` at 1000x600:
   - Header section (0,0,1000,50) with title, global knobs
   - Spectrum area (10,60,990,260) placeholder for SpectrumDisplay
   - Band strip container (10,270,700,590) with scroll view
   - Side panel (720,270,990,590) placeholder for morph pad

2. Commit: "feat(disrumpo): create main editor layout template (T4.24)"

### Week 5a: Custom Controls and Basic UI (T5a.1-T5a.12)

**T5a.1: createCustomView Implementation (4h)**

Files: `plugins/disrumpo/src/controller/controller.cpp`

1. Implement `createCustomView()`:
   ```cpp
   CView* Controller::createCustomView(UTF8StringPtr name, ...) {
       if (strcmp(name, "SpectrumDisplay") == 0) {
           CRect size;
           if (attributes.getRect("size", size)) {
               auto* spectrum = new SpectrumDisplay(size);
               spectrum->setNumBands(static_cast<int>(
                   getParamNormalized(makeGlobalParamId(kGlobalBandCount)) * 7 + 1));
               return spectrum;
           }
       }
       return nullptr;
   }
   ```

2. Commit: "feat(disrumpo): implement createCustomView for custom controls (T5a.1)"

**T5a.2-T5a.5: SpectrumDisplay Control (24h)**

Files: `plugins/disrumpo/src/controller/views/spectrum_display.h`, `spectrum_display.cpp`

1. Write unit tests:
   - `freqToX(20)` returns 0, `freqToX(20000)` returns width
   - `xToFreq(0)` returns 20, `xToFreq(width)` returns 20000
   - `hitTestDivider()` returns correct index within 10px tolerance
   - `hitTestBand()` returns correct band for click position

2. Implement SpectrumDisplay class:
   - Band region rendering with colored rectangles
   - Crossover divider rendering (vertical lines)
   - Frequency scale labels (20, 50, 100, 200, 500, 1k, 2k, 5k, 10k, 20k Hz)
   - Mouse interaction for divider dragging
   - Minimum 0.5 octave spacing constraint

3. Wire to editor.uidesc:
   ```xml
   <view class="SpectrumDisplay" custom-view-name="SpectrumDisplay"
         origin="10, 60" size="980, 200"/>
   ```

4. Verify visual rendering in DAW, commit: "feat(disrumpo): implement SpectrumDisplay with band regions (T5a.2-T5a.5)"

**T5a.6-T5a.9: BandStripCollapsed Template (12h)**

1. Create `<template name="BandStripCollapsed">` in uidesc:
   - Band label (e.g., "BAND 1")
   - Type dropdown (COptionMenu) bound to Band*Node0Type
   - Drive knob (CKnob) bound to Band*Node0Drive
   - Mix knob (CKnob) bound to Band*Node0Mix
   - Solo/Bypass/Mute toggles (COnOffButton)
   - Level meter (CVuMeter) placeholder

2. Wire control-tags to parameters

3. Commit: "feat(disrumpo): create BandStripCollapsed template (T5a.6-T5a.9)"

**T5a.10-T5a.12: Global Controls (10h)**

1. Create header section in main template:
   - Input Gain knob + label
   - Output Gain knob + label
   - Mix knob + label
   - Band Count segment button (1-8)
   - Oversample selector

2. Wire to control-tags

3. Commit: "feat(disrumpo): wire global controls in header (T5a.10-T5a.12)"

### Week 5b: Visibility Controllers and State (T5b.1-T5b.9)

**T5b.1-T5b.4: VisibilityController Pattern (20h)**

Files: `plugins/disrumpo/src/controller/controller.cpp`

1. Copy VisibilityController class from Iterum (within controller.cpp)
2. Copy ContainerVisibilityController class
3. Adapt for Disrumpo namespace and parameters

4. Implement `didOpen()`:
   ```cpp
   void Controller::didOpen(VST3Editor* editor) {
       activeEditor_ = editor;

       // Create band visibility controllers
       for (int b = 0; b < 8; ++b) {
           auto* bandCountParam = getParameterObject(makeGlobalParamId(kGlobalBandCount));
           float threshold = static_cast<float>(b) / 7.0f;

           bandVisibilityControllers_[b] = new ContainerVisibilityController(
               &activeEditor_, bandCountParam,
               9000 + b,  // Band container tag
               threshold,
               false  // Show when value >= threshold
           );
       }
   }
   ```

5. Implement `willClose()` with proper cleanup:
   ```cpp
   void Controller::willClose(VST3Editor* editor) {
       // CRITICAL: Deactivate all visibility controllers before editor closes
       for (auto& vc : bandVisibilityControllers_) {
           if (vc) {
               if (auto* cvc = dynamic_cast<ContainerVisibilityController*>(vc.get())) {
                   cvc->deactivate();
               }
           }
       }
       bandVisibilityControllers_.clear();
       activeEditor_ = nullptr;
   }
   ```

6. Commit: "feat(disrumpo): implement band visibility controllers (T5b.1-T5b.4)"

**T5b.5-T5b.6: Band Visibility Testing (6h)**

1. Test in DAW:
   - Set band count to 4, verify bands 5-8 are hidden
   - Change to 6, verify bands 5-6 appear
   - Change to 2, verify bands 3-8 hidden
   - Verify no visual glitches during transitions

2. Commit: "test(disrumpo): verify band visibility with band count changes (T5b.5-T5b.6)"

**T5b.7: setComponentState (8h)**

1. Implement `setComponentState()` to read processor state:
   - Read version, validate
   - Read global params (inputGain, outputGain, mix, bandCount)
   - Read per-band params
   - Read per-node params
   - Call `setParamNormalized()` for each

2. Commit: "feat(disrumpo): implement setComponentState for preset loading (T5b.7)"

**T5b.8: getParamStringByValue (6h)**

1. Implement display formatting per FR-027:
   - Drive: plain number, one decimal (e.g., "5.2")
   - Mix: percentage (e.g., "75%")
   - Gain: dB with one decimal (e.g., "4.5 dB")
   - Type: type name string (e.g., "Tube")
   - Pan: percentage with L/R suffix (e.g., "30% L")

2. Commit: "feat(disrumpo): implement parameter value display formatting (T5b.8)"

**T5b.9: Placeholder Preset Dropdown (4h)**

1. Add preset button in header (placeholder for future Week 12)
2. Commit: "feat(disrumpo): add placeholder preset button (T5b.9)"

---

## Milestone M3 Verification Checklist

Before claiming M3 complete, verify ALL:

- [ ] SC-001: Plugin editor opens within 500ms at 1000x600
- [ ] SC-002: All 6 global controls visible and interactive
- [ ] SC-003: Spectrum display shows N colored band regions for N bands
- [ ] SC-004: Band strips appear/disappear within 100ms on count change
- [ ] SC-005: Type dropdown shows 26 types, selection changes audio
- [ ] SC-006: Drive and Mix knobs respond to drag with visual feedback
- [ ] SC-007: Solo/Bypass/Mute toggles change state and affect audio
- [ ] SC-008: Crossover dividers draggable with frequency tooltip
- [ ] SC-009: All parameters persist through save/load cycle
- [ ] SC-010: UI frame time < 16ms during operation

**Deliverables**:
- `plugins/disrumpo/src/plugin_ids.h` (extended)
- `plugins/disrumpo/src/controller/controller.h` (extended)
- `plugins/disrumpo/src/controller/controller.cpp` (full implementation)
- `plugins/disrumpo/src/controller/views/spectrum_display.h/.cpp` (new)
- `plugins/disrumpo/resources/editor.uidesc` (new)
- `plugins/disrumpo/tests/unit/parameter_encoding_test.cpp` (new)

---

## Risk Mitigations

| Risk | Mitigation |
|------|------------|
| Parameter ID collisions | Unit tests verify no overlapping IDs; encoding scheme guarantees uniqueness |
| Visibility controller race conditions | IDependent::deactivate() called in willClose() before destruction |
| uidesc control-tag errors | All tags computed from hex IDs with documented decimal conversion |
| UIViewSwitchContainer pointer invalidation | Never cache control pointers; look up by tag on each update |
| Cross-platform color rendering | Use VSTGUI CColor with 8-bit RGBA; avoid platform-specific transparency |

---

## Complexity Tracking

No constitution violations requiring justification. All patterns follow established Iterum precedents.
