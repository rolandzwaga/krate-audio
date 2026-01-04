# Implementation Plan: Custom Tap Pattern Editor

**Branch**: `046-custom-pattern-editor` | **Date**: 2026-01-04 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/046-custom-pattern-editor/spec.md`
**Reference**: Detailed design at `specs/custom-pattern-editor-plan.md`

## Summary

Visual tap pattern editor for MultiTap delay mode allowing users to create custom timing and level patterns via drag interactions. Requires 32 new VST3 parameters (16 time ratios + 16 levels), a custom VSTGUI CControl subclass with 2D drag interaction, and DSP extension to support custom levels.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 12+, GCC 10+)
**Primary Dependencies**: VSTGUI 4.12+, VST3 SDK 3.7.x+
**Storage**: VST3 state persistence (IBStreamer)
**Testing**: Catch2 (plugin_tests, dsp_tests), pluginval level 5
**Target Platform**: Windows 10/11, macOS 11+, Linux (optional)
**Project Type**: VST3 plugin (monorepo: dsp/ + plugins/iterum/)
**Performance Goals**: 60fps UI responsiveness (16ms draw/interaction budget)
**Constraints**: Real-time audio thread safety, cross-platform VSTGUI only (no native APIs)
**Scale/Scope**: 32 new parameters, 1 new UI class, DSP modifications for level support

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No allocations in audio thread - parameters use atomics
- [x] No locks in audio path - UI/DSP communicate via parameters
- [x] Pre-allocate all buffers - custom pattern arrays already pre-allocated

**Required Check - Principle V (VSTGUI Development):**
- [x] Using UIDescription XML for layout placement
- [x] Custom view via VST3EditorDelegate::createCustomView()
- [x] UI thread never directly accesses audio data (uses IParameterChanges)

**Required Check - Principle VI (Cross-Platform):**
- [x] No Win32/Cocoa APIs - pure VSTGUI CControl subclass
- [x] Uses VSTGUI drawing primitives (CDrawContext)

**Required Check - Principle XII (Debugging Discipline):**
- [x] Read specs/VST-GUIDE.md before implementation
- [x] Framework documentation reviewed

**Required Check - Principle XIII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| TapPatternEditor | `grep -r "class TapPatternEditor" dsp/ plugins/` | No | Create New |

**Note**: Originally planned ITapPatternListener interface is NOT needed - implementation uses CControl's built-in listener pattern via `listener_` member (see quickstart.md pattern).

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| hitTestTap | `grep -r "hitTestTap" dsp/ plugins/` | No | N/A | Create New |
| positionToTimeRatio | `grep -r "positionToTimeRatio" dsp/ plugins/` | No | N/A | Create New |
| snapToGrid | `grep -r "snapToGrid" dsp/ plugins/` | No | N/A | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| ModeTabBar | plugins/iterum/src/ui/mode_tab_bar.h | UI | Reference for CView drawing patterns |
| PresetBrowserView | plugins/iterum/src/ui/preset_browser_view.h | UI | Reference for complex custom views |
| MultiTapDelay | dsp/include/krate/dsp/effects/multi_tap_delay.h | 4 | DSP to receive custom pattern data |
| TapManager | dsp/include/krate/dsp/systems/tap_manager.h | 3 | Underlying tap control via MultiTapDelay |
| multitap_params.h | plugins/iterum/src/parameters/multitap_params.h | Plugin | Parameter handling patterns |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/effects/multi_tap_delay.h` - Existing custom pattern support
- [x] `plugins/iterum/src/ui/` - No existing TapPatternEditor
- [x] `plugins/iterum/src/plugin_ids.h` - ID range 913-999 available

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: TapPatternEditor is unique and not found in codebase. No existing similar implementations to conflict with.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| MultiTapDelay | setCustomTimingPattern | `void setCustomTimingPattern(std::span<float> timeRatios) noexcept` | ✓ |
| MultiTapDelay | customTimeRatios_ | `std::array<float, kMaxTaps> customTimeRatios_ = {}` | ✓ |
| MultiTapDelay | loadTimingPattern | `void loadTimingPattern(TimingPattern pattern, size_t tapCount) noexcept` | ✓ |
| TapManager | setTapLevelDb | `void setTapLevelDb(size_t tapIndex, float levelDb) noexcept` | ✓ |
| TapManager | getTapLevelDb | `[[nodiscard]] float getTapLevelDb(size_t tapIndex) const noexcept` | ✓ |
| TapManager | setTapTimeMs | `void setTapTimeMs(size_t tapIndex, float timeMs) noexcept` | ✓ |
| TapManager | getTapTimeMs | `[[nodiscard]] float getTapTimeMs(size_t tapIndex) const noexcept` | ✓ |
| CControl | beginEdit | inherited from CControl | ✓ |
| CControl | endEdit | inherited from CControl | ✓ |
| CDrawContext | drawRect | `void drawRect(const CRect& rect, CDrawStyle style = kDrawStroked)` | ✓ |
| CDrawContext | setFillColor | `void setFillColor(const CColor& color)` | ✓ |

### Header Files Read

- [x] `dsp/include/krate/dsp/effects/multi_tap_delay.h` - MultiTapDelay class
- [x] `plugins/iterum/src/ui/mode_tab_bar.h` - Reference CView implementation
- [x] `plugins/iterum/src/plugin_ids.h` - Parameter ID allocation
- [x] `plugins/iterum/src/parameters/multitap_params.h` - Parameter patterns

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| MultiTapDelay | Custom pattern is `TimingPattern::Custom` (index 19 in enum) | Check for `pattern == TimingPattern::Custom` |
| MultiTapDelay | Only stores time ratios, not levels | Need to add `customLevels_` array or use TapManager directly |
| Parameter IDs | MultiTap range ends at 999 | Use 950-981 for custom pattern params (32 params) |
| CControl | Must call beginEdit/endEdit for automation | Wrap all value changes |
| VSTGUI | Font must be set before drawString | `context->setFont(font)` before text drawing |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | No Layer 0 candidates | — | — |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| hitTestTap | UI-specific, only used by TapPatternEditor |
| positionToTimeRatio | View coordinate conversion, specific to editor geometry |
| snapToGrid | UI helper, specific to editor interaction |

**Decision**: All utilities are UI-specific and will remain as private methods in TapPatternEditor class.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 4 - User Features (UI component)

**Related features at same layer**:
- Potential step sequencer views for other modes
- Pattern visualization for rhythmic effects
- Envelope editors for modulation

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Grid drawing code | MEDIUM | Step sequencers, envelope editors | Keep local, extract after 2nd use |
| Hit-test logic | LOW | Specific to vertical bar representation | Keep local |
| Snap-to-grid | MEDIUM | Step sequencers | Keep local, extract after 2nd use |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep grid drawing local | First pattern editor - no established patterns yet |
| No shared base class | Only one pattern editor currently |

### Review Trigger

After implementing **step sequencer or envelope editor**, review this section:
- [ ] Does sibling need grid drawing? → Extract to shared utilities
- [ ] Does sibling use same snap-to-grid? → Extract to shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/046-custom-pattern-editor/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
plugins/iterum/src/
├── ui/
│   ├── tap_pattern_editor.h      # NEW: TapPatternEditor class
│   └── tap_pattern_editor.cpp    # NEW: Implementation
├── parameters/
│   └── multitap_params.h         # MODIFY: Add custom pattern params
├── controller/
│   └── controller.cpp            # MODIFY: createCustomView, visibility
└── plugin_ids.h                  # MODIFY: Add 32 parameter IDs

plugins/iterum/resources/
└── editor.uidesc                 # MODIFY: Add pattern editor view

dsp/include/krate/dsp/effects/
└── multi_tap_delay.h             # MODIFY: Add custom level support

plugins/iterum/tests/
└── tap_pattern_editor_test.cpp   # NEW: Unit tests
```

**Structure Decision**: Extends existing monorepo structure. UI component in plugins/iterum/src/ui/, parameters in multitap_params.h, DSP extension in multi_tap_delay.h.

## Complexity Tracking

No Constitution Check violations. Implementation follows standard patterns for custom VSTGUI views with VST3 parameter binding.
