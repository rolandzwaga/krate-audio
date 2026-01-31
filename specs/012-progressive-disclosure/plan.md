# Implementation Plan: Progressive Disclosure & Accessibility

**Branch**: `012-progressive-disclosure` | **Date**: 2026-01-31 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/012-progressive-disclosure/spec.md`

## Summary

Add progressive disclosure UI patterns and accessibility support to the Disrumpo multiband distortion plugin. This covers 8 user stories and 40 functional requirements across: smooth panel expand/collapse animation (FR-001 to FR-006), modulation panel toggle (FR-007 to FR-009), keyboard shortcuts (FR-010 to FR-016), window resize with 5:3 aspect ratio (FR-017 to FR-023), high contrast mode detection (FR-024 to FR-026), reduced motion detection (FR-027 to FR-029), generalized MIDI CC mapping with MIDI Learn (FR-030 to FR-037), and 14-bit MIDI CC support (FR-038 to FR-040).

The implementation leverages substantial existing infrastructure: `ContainerVisibilityController` pattern, `kBandExpanded` parameters, `IMidiMapping` interface, and sweep-only MIDI Learn. Primary new work is: VSTGUI animation framework integration, `IKeyboardHook` implementation, `VST3Editor::setEditorSizeConstrains()` for resize, platform-specific accessibility detection (behind `#ifdef` guards), and a shared `MidiCCManager` class.

## Technical Context

**Language/Version**: C++20, MSVC 2022 / Clang 12+ / GCC 10+
**Primary Dependencies**: VST3 SDK 3.7.x+, VSTGUI 4.12+ (Animation, IKeyboardHook, CFrame focus drawing)
**Storage**: Controller state (IBStream) for window size, global MIDI mappings; Component state for per-preset MIDI mappings
**Testing**: Catch2 (unit tests for logic, integration tests for parameter state, pluginval for validation) *(Constitution Principle VIII)*
**Target Platform**: Windows 10/11, macOS 11+, Linux (optional)
**Project Type**: Monorepo VST3 plugin
**Performance Goals**: 60fps UI, animations <= 300ms, MIDI CC latency < 10ms
**Constraints**: Cross-platform (Constitution Principle VI), no raw new/delete (Principle III), UI thread only for VSTGUI (Principle V)
**Scale/Scope**: ~450 existing parameters, 8 bands x 4 nodes, up to 128 MIDI CC mappings

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle V (VSTGUI Development):**
- [x] All UI via UIDescription and VSTGUI APIs
- [x] Animation uses built-in VSTGUI::Animation framework (ViewSizeAnimation, CubicBezierTimingFunction)
- [x] Keyboard shortcuts via IKeyboardHook (VSTGUI cross-platform API)
- [x] Window resize via VST3Editor::setEditorSizeConstrains() (VSTGUI API)
- [x] Focus drawing via CFrame::setFocusDrawingEnabled() (VSTGUI API)

**Required Check - Principle VI (Cross-Platform):**
- [x] Platform-specific code ONLY in accessibility_helper.cpp behind #ifdef guards
- [x] Spec explicitly requires platform-specific APIs for accessibility (FR-025b/c/d)
- [x] Graceful fallback on unsupported platforms (default = no high contrast, no reduced motion)
- [x] All other code uses VSTGUI cross-platform abstractions exclusively

**Required Check - Principle VIII (Testing Discipline):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created (all grep searches clear)

**Post-Design Re-Check:**
- [x] No constitution violations found in design
- [x] Platform-specific code is limited and justified (FR-025b/c/d)
- [x] All new classes are unique (grep verified)
- [x] Existing ContainerVisibilityController pattern extended, not duplicated
- [x] MidiCCManager in plugins/shared/ for reuse (forward-looking)

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| AnimatedExpandController | `grep -r "class AnimatedExpandController\|class PanelAnimator\|class ExpandAnimator" dsp/ plugins/` | No | Create New |
| KeyboardShortcutHandler | `grep -r "class KeyboardShortcutHandler\|class KeyboardHookHandler" dsp/ plugins/` | No | Create New |
| AccessibilityPreferences (struct) | `grep -r "struct AccessibilityPreferences\|class AccessibilityDetector" dsp/ plugins/` | No (only in specs/Disrumpo/dsp-details.md as planned) | Create New |
| MidiCCMapping (struct) | `grep -r "struct MidiCCMapping\|class MidiCCMapping" dsp/ plugins/` | No (only in specs/Disrumpo/dsp-details.md as planned) | Create New |
| MidiCCManager | `grep -r "class MidiCCManager" dsp/ plugins/` | No (only in specs/Disrumpo/dsp-details.md as planned) | Create New |
| HighContrastColors (struct) | `grep -r "struct HighContrastColors" dsp/ plugins/` | No | Create New |
| WindowResizeHandler | `grep -r "class WindowResizeHandler\|class ResizeConstraint" dsp/ plugins/` | No | Not needed (use VST3Editor built-in) |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| queryAccessibilityPreferences | `grep -r "queryAccessibilityPreferences\|isHighContrastEnabled\|isReducedMotionPreferred" plugins/` | No (only in specs/) | NEW: shared/platform/ | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| ContainerVisibilityController | controller.cpp:162 | UI | Base pattern for AnimatedExpandController |
| VisibilityController | controller.cpp:60 | UI | Reference pattern for IDependent + deactivate() |
| BandCountDisplayController | controller.cpp:266 | UI | Reference pattern for parameter observation |
| expandedVisibilityControllers_ | controller.h:213 | UI | Replace with AnimatedExpandControllers |
| bandVisibilityControllers_ | controller.h:207 | UI | Keep as-is (band count visibility) |
| sweepVisualizationTimer_ | controller.h:243 | UI | Reference for timer-based UI updates |
| IMidiMapping | controller.h:46 | VST3 | Extend getMidiControllerAssignment() |
| assignedMidiCC_ | controller.h:263 | UI | Replace with MidiCCManager |
| kBandExpanded | plugin_ids.h | Param | Existing parameter, no change |
| kSweepMidiLearnActive | plugin_ids.h | Param | Keep for backwards compat |
| kSweepMidiCCNumber | plugin_ids.h | Param | Keep for backwards compat |
| PresetManager | shared/preset/preset_manager.h | Shared | State persistence patterns |

### Files Checked for Conflicts

- [x] `plugins/disrumpo/src/plugin_ids.h` - Parameter ID encoding verified, no conflicts with new IDs
- [x] `plugins/disrumpo/src/controller/controller.h` - Existing members mapped, extension points identified
- [x] `plugins/disrumpo/src/controller/controller.cpp` - All existing controller classes documented
- [x] `plugins/disrumpo/src/controller/views/` - Custom views enumerated (spectrum, morph_pad, sweep, etc.)
- [x] `plugins/shared/src/` - Existing shared infrastructure (preset, UI, platform) checked
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/animation/` - Animation APIs verified from source

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All 5 planned new types (AnimatedExpandController, KeyboardShortcutHandler, AccessibilityPreferences, MidiCCMapping, MidiCCManager) do not exist in the codebase. The dsp-details.md specification references MidiCCMapping and accessibility helpers but only as design specs, not compiled code. The existing ContainerVisibilityController will be reused (not recreated), and the AnimatedExpandController is a distinct class with animation capabilities.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| CFrame | getAnimator | `Animation::Animator* getAnimator()` | Yes (cframe.h:128) |
| CFrame | registerKeyboardHook | `void registerKeyboardHook(IKeyboardHook* hook)` | Yes (cframe.h:139) |
| CFrame | unregisterKeyboardHook | `void unregisterKeyboardHook(IKeyboardHook* hook)` | Yes (cframe.h:141) |
| CFrame | setFocusDrawingEnabled | `void setFocusDrawingEnabled(bool state)` | Yes (cframe.h:174) |
| CFrame | setFocusColor | `void setFocusColor(const CColor& color)` | Yes (cframe.h:179) |
| CFrame | setFocusWidth | `void setFocusWidth(CCoord width)` | Yes (cframe.h:185) |
| CFrame | setFocusView | `void setFocusView(CView* pView)` | Yes (cframe.h:109) |
| CFrame | getFocusView | `CView* getFocusView() const` | Yes (cframe.h:110) |
| CFrame | advanceNextFocusView | `bool advanceNextFocusView(CView* oldFocus, bool reverse = false) override` | Yes (cframe.h:111) |
| Animator | addAnimation | `void addAnimation(CView* view, IdStringPtr name, IAnimationTarget* target, ITimingFunction* timingFunction, DoneFunction notification = nullptr, bool notifyOnCancel = false)` | Yes (animator.h:40) |
| Animator | removeAnimation | `void removeAnimation(CView* view, IdStringPtr name)` | Yes (animator.h:48) |
| ViewSizeAnimation | ctor | `ViewSizeAnimation(const CRect& newRect, bool forceEndValueOnFinish = false)` | Yes (animations.h:41) |
| AlphaValueAnimation | ctor | `AlphaValueAnimation(float endValue, bool forceEndValueOnFinish = false)` | Yes (animations.h:22) |
| FuncAnimation | ctor | `FuncAnimation(StartFunc&& start, TickFunc&& tick, FinishedFunc&& finished)` | Yes (animations.h:130) |
| CubicBezierTimingFunction | easyInOut | `static CubicBezierTimingFunction easyInOut(uint32_t time)` | Yes (timingfunctions.h:105) |
| CubicBezierTimingFunction | make | `static CubicBezierTimingFunction* make(Style style, uint32_t time)` | Yes (timingfunctions.h:114) |
| VST3Editor | setEditorSizeConstrains | `bool setEditorSizeConstrains(const CPoint& newMinimumSize, const CPoint& newMaximumSize)` | Yes (vst3editor.h:123) |
| VST3Editor | requestResize | `bool requestResize(const CPoint& newSize)` | Yes (vst3editor.h:125) |
| VST3Editor | setZoomFactor | `void setZoomFactor(double factor)` | Yes (vst3editor.h:127) |
| IKeyboardHook | onKeyboardEvent | `virtual void onKeyboardEvent(KeyboardEvent& event, CFrame* frame) = 0` | Yes (cframe.h:353) |
| KeyboardEvent | virt | `VirtualKey virt {VirtualKey::None}` | Yes (events.h:519) |
| KeyboardEvent | character | `char32_t character {0}` | Yes (events.h:517) |
| VirtualKey | Tab/Space/Arrow | `enum class VirtualKey : uint32_t { Tab, Space, Left, Up, Right, Down, ... }` | Yes (events.h:420-489) |
| ModifierKey | Shift | `enum class ModifierKey : uint32_t { Shift = 1 << 0, ... }` | Yes (events.h:495-508) |

### Header Files Read

- [x] `extern/vst3sdk/vstgui4/vstgui/lib/cframe.h` - CFrame class, IKeyboardHook, focus drawing
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/events.h` - KeyboardEvent, VirtualKey, ModifierKey
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/animation/animator.h` - Animator class
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/animation/animations.h` - ViewSizeAnimation, AlphaValueAnimation, FuncAnimation
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/animation/timingfunctions.h` - CubicBezierTimingFunction
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/animation/ianimationtarget.h` - IAnimationTarget interface
- [x] `extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.h` - VST3Editor, VST3EditorDelegate, IVST3EditorDelegate
- [x] `plugins/disrumpo/src/plugin_ids.h` - Parameter ID encoding
- [x] `plugins/disrumpo/src/controller/controller.h` - Controller class
- [x] `plugins/disrumpo/src/controller/controller.cpp` - Existing controller implementations (lines 1-3600+)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| CubicBezierTimingFunction | `easyInOut()` returns by value, `make()` returns pointer | Use `make(Style::EasyInOut, 250)` for `new`-allocated timing function (Animator takes ownership) |
| Animator::addAnimation | Animator takes ownership of target AND timingFunction | Do NOT delete them manually |
| Animator::addAnimation | Adding animation with same view+name cancels existing | This is the desired behavior for mid-animation reversal (FR-006) |
| IKeyboardHook | Event is consumed by setting `event.consumed = true` | Not a return value |
| setEditorSizeConstrains | Note spelling: "Constrains" not "Constraints" | Exact method name from SDK |
| CFrame::advanceNextFocusView | Does not wrap from last to first view | Must handle wrapping manually for band-only focus chain |
| editor.uidesc | Current minSize="800, 700" maxSize="1400, 1100" | Must update to reflect 5:3 ratio constraints |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| All animation/keyboard/accessibility logic | UI-thread-only code, not DSP, not layer 0 |

**Decision**: No Layer 0 extractions needed. This feature is entirely UI/controller-side with no DSP changes. All new code lives in plugin/shared layers.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: UI/Controller layer (not DSP)

**Related features at same layer** (from ROADMAP.md):
- Iterum plugin: Would benefit from same keyboard shortcuts, window resize, accessibility detection, MIDI CC mapping
- Future Krate Audio plugins: Would need same accessibility and MIDI CC infrastructure

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| AccessibilityHelper | HIGH | Iterum, all future plugins | Extract now to plugins/shared/src/platform/ |
| MidiCCManager | HIGH | Iterum, all future plugins | Extract now to plugins/shared/src/midi/ |
| KeyboardShortcutHandler | MEDIUM | Iterum (different shortcuts) | Keep in Disrumpo for now, extract after 2nd use |
| AnimatedExpandController | MEDIUM | Iterum (if it gets expand/collapse) | Keep in Disrumpo for now |

### Detailed Analysis (for HIGH potential items)

**AccessibilityHelper** provides:
- OS high contrast detection (Win32, macOS, Linux)
- OS reduced motion detection
- High contrast color palette query

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Iterum | YES | Same accessibility needs, identical API |
| Future plugins | YES | Plugin-agnostic utility |

**Recommendation**: Extract now to `plugins/shared/src/platform/accessibility_helper.h/.cpp`

**MidiCCManager** provides:
- CC-to-parameter mapping management
- MIDI Learn workflow
- 14-bit CC support
- Global + per-preset hybrid persistence

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Iterum | YES | Same MIDI CC needs (delay time, feedback, etc.) |
| Future plugins | YES | Standard MIDI CC mapping is universal |

**Recommendation**: Extract now to `plugins/shared/src/midi/midi_cc_manager.h/.cpp`

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract AccessibilityHelper to shared/ | Plugin-agnostic, will be needed by Iterum (clear 2nd consumer) |
| Extract MidiCCManager to shared/ | Plugin-agnostic, same mapping needs across all plugins |
| Keep KeyboardShortcutHandler in Disrumpo | Shortcut mappings may differ per plugin; wait for 2nd consumer |
| Keep AnimatedExpandController in Disrumpo | Expand/collapse is Disrumpo-specific for now |

### Review Trigger

After implementing **Iterum Week 14** (if it gets progressive disclosure), review this section:
- [ ] Does Iterum need KeyboardShortcutHandler or similar? -> Extract to shared/
- [ ] Does Iterum need AnimatedExpandController or similar? -> Extract to shared/
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/012-progressive-disclosure/
  plan.md              # This file
  spec.md              # Feature specification (40 FRs, 11 SCs)
  research.md          # Phase 0: Research findings (R1-R8)
  data-model.md        # Phase 1: Entity definitions, state model
  quickstart.md        # Phase 1: Implementation guide
  contracts/           # Phase 1: API contracts
    animation-controller.h
    keyboard-shortcut-handler.h
    accessibility-helper.h
    midi-cc-manager.h
```

### Source Code (repository root)

```text
plugins/shared/src/
  platform/
    accessibility_helper.h       # NEW: AccessibilityPreferences, query functions
    accessibility_helper.cpp     # NEW: Win32/macOS/Linux implementations
  midi/
    midi_cc_manager.h            # NEW: MidiCCManager, MidiCCMapping
    midi_cc_manager.cpp          # NEW: Mapping, MIDI Learn, 14-bit, serialization

plugins/disrumpo/src/
  plugin_ids.h                   # MODIFY: Add kGlobalModPanelVisible, kGlobalMidiLearnActive, kGlobalMidiLearnTarget
  controller/
    controller.h                 # MODIFY: Add members (keyboard handler, MidiCCManager, resize state)
    controller.cpp               # MODIFY: AnimatedExpandController, context menu, resize, etc.
    keyboard_shortcut_handler.h  # NEW: IKeyboardHook for Tab/Space/Arrow
    keyboard_shortcut_handler.cpp
  resources/
    editor.uidesc                # MODIFY: Update minSize/maxSize, modulation panel container tag

plugins/disrumpo/tests/
  unit/
    midi_cc_manager_test.cpp     # NEW: MidiCCManager unit tests
    keyboard_shortcut_test.cpp   # NEW: Keyboard shortcut logic tests
    accessibility_test.cpp       # NEW: Accessibility detection tests (mock-based)
    animation_logic_test.cpp     # NEW: Animation state transition tests
  integration/
    expand_collapse_test.cpp     # MODIFY: Add animation timing tests
    resize_test.cpp              # NEW: Window resize constraint tests
    midi_learn_integration_test.cpp  # NEW: MIDI Learn workflow tests
```

**Structure Decision**: Extends the existing monorepo structure. New shared utilities go in `plugins/shared/src/`. Plugin-specific code stays in `plugins/disrumpo/src/controller/`. Tests follow existing conventions in `plugins/disrumpo/tests/`.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| Platform-specific code in accessibility_helper.cpp | FR-025b/c/d explicitly require Windows SystemParametersInfo, macOS NSWorkspace, Linux GSettings APIs | VSTGUI has no cross-platform accessibility detection API. The spec mandates OS-level queries. Code is isolated in one file with #ifdef guards and graceful fallbacks. |
