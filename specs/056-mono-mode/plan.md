# Implementation Plan: Mono Mode Conditional Panel

**Branch**: `056-mono-mode` | **Date**: 2026-02-15 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/056-mono-mode/spec.md`

## Summary

Add a conditional visibility panel in the Voice & Output section that swaps the Polyphony dropdown for four mono-specific controls (Legato toggle, Priority dropdown, Portamento Time knob, Portamento Mode dropdown) when Voice Mode is set to Mono. This is purely UI-layer work: uidesc control-tag additions, CViewContainer visibility groups, and controller wiring using the same pattern as the 6 existing sync toggle visibility groups (LFO1, LFO2, Chaos, Delay, Phaser, TranceGate). No DSP, parameter registration, or state persistence changes are needed.

## Technical Context

**Language/Version**: C++20, VSTGUI 4.12+, VST3 SDK 3.7.x+
**Primary Dependencies**: VSTGUI (CViewContainer, COptionMenu, ToggleButton, ArcKnob), VST3 SDK (EditControllerEx1, setParamNormalized)
**Storage**: N/A (all 4 mono parameters already persisted by saveMonoModeParams/loadMonoModeParams)
**Testing**: pluginval strictness level 5 + manual verification (no unit tests for UI visibility wiring -- this is consistent with all 6 existing visibility group implementations) *(Constitution Principle XII)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform VSTGUI only
**Project Type**: VST3 plugin monorepo
**Performance Goals**: N/A -- no DSP work, UI-only changes
**Constraints**: Cross-platform (Constitution Principle VI) -- VSTGUI abstractions only, no native APIs
**Scale/Scope**: 3 files modified (editor.uidesc, controller.h, controller.cpp), ~50 lines of new code

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle V (VSTGUI Development):**
- [x] All UI elements use UIDescription XML (uidesc file)
- [x] No direct audio processing data access from UI thread
- [x] All parameter values normalized (0.0-1.0) at VST boundary

**Required Check - Principle VI (Cross-Platform):**
- [x] No Windows-native or macOS-native APIs used
- [x] All controls use VSTGUI cross-platform abstractions (COptionMenu, ToggleButton, ArcKnob, CViewContainer)

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] UI visibility wiring has no unit test precedent in this codebase (all 6 existing sync groups are tested via pluginval + manual only). This spec follows the same testing approach.
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No new classes, structs, or functions will be created (only extending existing Controller class with 2 new fields and additional cases in existing methods)

**Required Check - Principle XVI (Honest Completion):**
- [x] Compliance table will be verified against actual code and test output before marking MET

**Required Check - Principle XVII (Framework Knowledge):**
- [x] vst-guide skill loaded -- visibility group pattern documented in THREAD-SAFETY.md and UI-COMPONENTS.md

**Post-Design Re-Check:**
- [x] All design decisions use VSTGUI cross-platform abstractions
- [x] Visibility wiring follows the IDependent-like pattern in setParamNormalized (thread-safe: view pointers null-checked before use, no blocking operations)
- [x] No new types introduced, ODR risk is zero

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None. This spec only adds member fields and case branches to the existing Controller class.

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| (none) | N/A | N/A | N/A |

**Utility Functions to be created**: None.

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none) | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Controller visibility pattern | `controller.cpp:510-538` | Plugin | Extend with kVoiceModeId case for PolyGroup/MonoGroup toggle |
| Controller verifyView pattern | `controller.cpp:824-894` | Plugin | Extend with PolyGroup/MonoGroup custom-view-name capture |
| Controller willClose pattern | `controller.cpp:592-614` | Plugin | Extend with polyGroup_/monoGroup_ null cleanup |
| MonoModeParams | `parameters/mono_mode_params.h` | Plugin | Already registered, no changes needed |
| Plugin IDs | `plugin_ids.h:537-540` | Plugin | Reference kMonoPriorityId (1800), kMonoLegatoId (1801), kMonoPortamentoTimeId (1802), kMonoPortaModeId (1803) |
| kVoiceModeId | `plugin_ids.h:60` | Plugin | Reference for visibility toggle condition (value < 0.5 = Poly, >= 0.5 = Mono) |

### Files Checked for Conflicts

- [x] `plugins/ruinae/src/controller/controller.h` - Existing view pointer fields at lines 215-235; adding after tranceGateNoteValueGroup_ (line 235)
- [x] `plugins/ruinae/src/controller/controller.cpp` - Existing setParamNormalized, verifyView, willClose methods
- [x] `plugins/ruinae/resources/editor.uidesc` - Existing control-tags section (lines 62-326), Voice & Output panel (lines 2697-2794)
- [x] `plugins/ruinae/src/plugin_ids.h` - Mono param IDs confirmed at lines 537-540, VoiceMode at line 60
- [x] `plugins/ruinae/src/parameters/mono_mode_params.h` - All 4 params already registered

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new classes, structs, or functions are created. All changes are additive extensions to existing types (2 new fields in Controller, additional case branches in existing methods, new XML elements in uidesc). Zero ODR risk.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| EditControllerEx1 | getParameterObject | `Steinberg::Vst::Parameter* getParameterObject(Steinberg::Vst::ParamID tag)` | Yes |
| Parameter | getNormalized | `Steinberg::Vst::ParamValue getNormalized() const` | Yes |
| CView | setVisible | `virtual void setVisible(bool state)` | Yes |
| UIAttributes | getAttributeValue | `const std::string* getAttributeValue(const std::string& name) const` | Yes |

### Header Files Read

- [x] `plugins/ruinae/src/controller/controller.h` - Controller class fields and methods
- [x] `plugins/ruinae/src/controller/controller.cpp` - setParamNormalized (line 478), verifyView (line 637), willClose (line 592)
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter IDs
- [x] `plugins/ruinae/src/parameters/mono_mode_params.h` - registerMonoModeParams function
- [x] `plugins/ruinae/resources/editor.uidesc` - Control-tags section, Voice & Output panel

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| CViewContainer visibility | Must use `custom-view-name` attribute (not `custom-id`) for view capture in verifyView() | `attributes.getAttributeValue("custom-view-name")` |
| StringListParameter dropdowns | COptionMenu items are auto-populated by framework when control-tag matches a StringListParameter -- do NOT manually add items | Just set `control-tag="MonoPriority"` in uidesc |
| Visibility initial state | Must set in verifyView() after capturing pointer, not in didOpen() | Read current param value via `getParameterObject(kVoiceModeId)->getNormalized()` |
| ToggleButton with title | The `title` attribute renders text on the button; `on-color` colors the toggle indicator | Use both `title="Legato"` and `on-color="master"` |

## Layer 0 Candidate Analysis

N/A -- This is a UI-only feature. No DSP utilities to extract.

## SIMD Optimization Analysis

N/A -- This is a UI-only feature. No DSP processing involved.

## Higher-Layer Reusability Analysis

**This feature's layer**: UI / Controller layer (not DSP)

**Related features at same layer** (from roadmap):
- Phase 4 (Macros & Rungler): Different panel area, no overlap
- Phase 5.1 (Settings Drawer): Shares Voice & Output panel (gear icon) but does not interact with Poly/Mono swap

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Visibility group pattern (polyGroup_/monoGroup_) | LOW | None identified | Keep local -- the pattern is already well-established with 6 prior implementations |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared visibility helper function | The visibility toggle is 2 lines of code per group; abstracting would add complexity without benefit. All 6 existing groups use inline if-statements. |
| No new reusable component | This spec follows an identical established pattern. No novel abstractions needed. |

## Project Structure

### Documentation (this feature)

```text
specs/056-mono-mode/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output (minimal -- no unknowns)
├── data-model.md        # Phase 1 output (uidesc XML structure)
├── quickstart.md        # Phase 1 output (implementation guide)
├── contracts/           # Phase 1 output (API contracts)
│   └── controller-api.md
├── checklists/
│   └── requirements.md  # Quality checklist
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (files to modify)

```text
plugins/ruinae/
├── resources/
│   └── editor.uidesc            # Add 4 control-tags, wrap Polyphony in PolyGroup,
│                                 # add MonoGroup container with 4 controls
└── src/
    └── controller/
        ├── controller.h          # Add polyGroup_ and monoGroup_ view pointer fields
        └── controller.cpp        # Add visibility toggle in setParamNormalized(),
                                  # view capture in verifyView(), cleanup in willClose()
```

**Structure Decision**: This is a modification-only change to 3 existing files in the Ruinae plugin. No new source files are created.

## Implementation Design

### Change 1: Control-Tags (editor.uidesc, lines 62-76)

Add 4 control-tag entries after the Global Filter section (line 75), before the OSC A section (line 77):

```xml
        <!-- Mono Mode -->
        <control-tag name="MonoPriority" tag="1800"/>
        <control-tag name="MonoLegato" tag="1801"/>
        <control-tag name="MonoPortamentoTime" tag="1802"/>
        <control-tag name="MonoPortaMode" tag="1803"/>
```

These tag values match the parameter IDs in `plugin_ids.h:537-540`.

### Change 2: PolyGroup Container (editor.uidesc, lines 2736-2744)

Wrap the existing Polyphony COptionMenu in a CViewContainer:

**Before:**
```xml
            <!-- Row 2: Polyphony dropdown -->
            <view class="COptionMenu" origin="8, 36" size="60, 18"
                  control-tag="Polyphony"
                  .../>
```

**After:**
```xml
            <!-- Row 2: Polyphony dropdown (visible when Voice Mode = Poly) -->
            <view class="CViewContainer" origin="8, 36" size="112, 18"
                  custom-view-name="PolyGroup" transparent="true">
                <view class="COptionMenu" origin="0, 0" size="60, 18"
                      control-tag="Polyphony"
                      .../>
            </view>
```

Key details:
- The COptionMenu origin changes from `8, 36` to `0, 0` (relative to its new parent container)
- The container origin is `8, 36` (same position the dropdown was at)
- Container size is `112, 18` per FR-002
- `visible` attribute is omitted (defaults to true, matching Poly default)

### Change 3: MonoGroup Container (editor.uidesc, after PolyGroup)

Add a new CViewContainer immediately after PolyGroup, at the same origin `8, 36`:

```xml
            <!-- Mono controls (visible when Voice Mode = Mono) -->
            <view class="CViewContainer" origin="8, 36" size="112, 18"
                  custom-view-name="MonoGroup" transparent="true"
                  visible="false">
                <!-- Single row: Legato toggle | Priority dropdown | Porta knob | PortaMode dropdown -->
                <view class="ToggleButton" origin="0, 0" size="22, 18"
                      control-tag="MonoLegato"
                      default-value="0"
                      title="Leg"
                      title-position="right"
                      font="~ NormalFontSmaller"
                      font-color="master"
                      text-color="master"
                      on-color="master"
                      off-color="text-secondary"
                      tooltip="Legato mode"
                      transparent="true"/>
                <view class="COptionMenu" origin="24, 0" size="36, 18"
                      control-tag="MonoPriority"
                      default-value="0"
                      font="~ NormalFontSmaller"
                      font-color="master"
                      back-color="bg-dropdown"
                      frame-color="frame-dropdown-dim"
                      tooltip="Note priority"
                      transparent="false"/>
                <view class="ArcKnob" origin="62, 0" size="18, 18"
                      control-tag="MonoPortamentoTime"
                      default-value="0"
                      arc-color="master"
                      guide-color="knob-guide"
                      tooltip="Portamento time"/>
                <view class="COptionMenu" origin="82, 0" size="30, 18"
                      control-tag="MonoPortaMode"
                      default-value="0"
                      font="~ NormalFontSmaller"
                      font-color="master"
                      back-color="bg-dropdown"
                      frame-color="frame-dropdown-dim"
                      tooltip="Portamento mode"
                      transparent="false"/>
            </view>
```

Key details:
- Container starts `visible="false"` because default Voice Mode is Polyphonic (FR-003)
- Container size is `112, 18` -- single row matching PolyGroup height (FR-003)
- All 4 controls at y=0 with 2px gaps: 22+2+36+2+18+2+30 = 112px (FR-004)
- Mini ArcKnob 18x18 fits the 18px row height -- no overflow (FR-004)
- No separate "Porta" label -- knob identified by position and tooltip (FR-005)
- Legato uses `on-color="master"`, dropdowns use `font-color="master"`, ArcKnob uses `arc-color="master"` + `guide-color="knob-guide"` per FR-011

### Change 4: Controller View Pointer Fields (controller.h)

Add two new view pointer fields after the existing tranceGateNoteValueGroup_ (line 235):

```cpp
    /// Poly/Mono visibility groups - toggled by voice mode
    VSTGUI::CView* polyGroup_ = nullptr;
    VSTGUI::CView* monoGroup_ = nullptr;
```

### Change 5: Visibility Toggle in setParamNormalized (controller.cpp)

Add a kVoiceModeId case after the existing kTranceGateTempoSyncId block (line 538):

```cpp
    // Toggle Poly/Mono visibility based on voice mode
    if (tag == kVoiceModeId) {
        if (polyGroup_) polyGroup_->setVisible(value < 0.5);
        if (monoGroup_) monoGroup_->setVisible(value >= 0.5);
    }
```

This follows the exact same 2-line pattern used for all 6 existing sync toggle groups.

### Change 6: View Capture in verifyView (controller.cpp)

Add PolyGroup/MonoGroup cases in the `custom-view-name` matching block (after the TranceGateNoteValueGroup case at line 894):

```cpp
            // Poly/Mono visibility groups (toggled by voice mode)
            else if (*name == "PolyGroup") {
                polyGroup_ = container;
                auto* voiceModeParam = getParameterObject(kVoiceModeId);
                bool isMono = (voiceModeParam != nullptr) && voiceModeParam->getNormalized() >= 0.5;
                container->setVisible(!isMono);
            } else if (*name == "MonoGroup") {
                monoGroup_ = container;
                auto* voiceModeParam = getParameterObject(kVoiceModeId);
                bool isMono = (voiceModeParam != nullptr) && voiceModeParam->getNormalized() >= 0.5;
                container->setVisible(isMono);
            }
```

This follows the exact same pattern as the sync toggle groups (e.g., LFO1RateGroup at line 824-828), reading the current parameter value and setting initial visibility accordingly (FR-009).

### Change 7: Pointer Cleanup in willClose (controller.cpp)

Add polyGroup_/monoGroup_ cleanup after the existing tranceGateNoteValueGroup_ cleanup (line 614):

```cpp
        polyGroup_ = nullptr;
        monoGroup_ = nullptr;
```

### Implementation Order

1. **editor.uidesc**: Add 4 control-tags (Change 1)
2. **editor.uidesc**: Wrap Polyphony in PolyGroup container (Change 2)
3. **editor.uidesc**: Add MonoGroup container with 4 controls (Change 3)
4. **controller.h**: Add polyGroup_/monoGroup_ fields (Change 4)
5. **controller.cpp**: Add visibility toggle in setParamNormalized (Change 5)
6. **controller.cpp**: Add view capture in verifyView (Change 6)
7. **controller.cpp**: Add pointer cleanup in willClose (Change 7)
8. **Build**: Verify zero warnings
9. **Test**: Run pluginval at strictness level 5
10. **Test**: Manual verification of visibility swap

### Verification Plan

| Check | Method | Expected Result |
|-------|--------|-----------------|
| Control-tags registered | Build succeeds, controls bind to parameters | ArcKnob, dropdowns, toggle respond to parameter changes |
| Poly mode (default) | Open plugin | Polyphony dropdown visible, mono controls hidden |
| Switch to Mono | Click Voice Mode dropdown, select Mono | Polyphony hidden, 4 mono controls appear |
| Switch back to Poly | Click Voice Mode dropdown, select Polyphonic | Mono controls hidden, Polyphony reappears |
| Preset persistence | Save preset with Mono active, reload | Mono panel visible on load |
| Automation | Automate kVoiceModeId | Panel swaps in response to automation |
| pluginval | `pluginval --strictness-level 5 --validate ...` | Pass at level 5 |
| Zero warnings | `cmake --build ... --config Release` | No warnings from changed files |

## Complexity Tracking

No constitution violations. All design decisions use established patterns with zero new abstractions.
