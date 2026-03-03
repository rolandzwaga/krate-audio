# Research: Progressive Disclosure & Accessibility

**Feature Branch**: `012-progressive-disclosure`
**Date**: 2026-01-31
**Status**: Complete

## R1: VSTGUI Animation Framework for Panel Expand/Collapse

**Decision**: Use VSTGUI's built-in `Animation::Animator` with `ViewSizeAnimation` and `AlphaValueAnimation` plus `CubicBezierTimingFunction::easyInOut(250)` for panel transitions.

**Rationale**: VSTGUI 4.x includes a complete animation framework accessed via `CFrame::getAnimator()`. It supports:
- `ViewSizeAnimation` - animates CRect changes (perfect for height expand/collapse)
- `AlphaValueAnimation` - animates opacity (for fade-in/out effects)
- `FuncAnimation` - custom lambda-based animation target (new in 4.14)
- `CubicBezierTimingFunction::easyInOut()` - CSS-like easing with configurable duration
- Automatic cancellation when a new animation is added for the same view+name
- DoneFunction callback for cleanup after animation completes

The existing `ContainerVisibilityController` uses instant `setVisible()`. The animation approach wraps this: instead of instant show/hide, animate the container's height from 0 to target (or vice versa), then call `setVisible(false)` in the DoneFunction when collapsing.

**Implementation approach**:
- Create `AnimatedExpandController` that extends the existing `ContainerVisibilityController` pattern
- On expand: `setVisible(true)` first, then animate height from 0 to full height
- On collapse: animate height from full to 0, then `setVisible(false)` in done callback
- If reduced motion is active: skip animation, use instant show/hide (existing behavior)
- If user triggers reverse during animation: the animator automatically cancels the current animation and starts the new one from the current position (built-in VSTGUI behavior)

**Alternatives considered**:
- CVSTGUITimer-based manual animation: More work, reinvents what VSTGUI already provides
- Opacity-only fade: Simpler but doesn't reclaim screen space (height stays the same)
- No animation (instant): Already implemented, spec requires smooth transitions

**Key API signatures verified from source**:
```cpp
// cframe.h line 128
Animation::Animator* CFrame::getAnimator();

// animator.h line 40
void Animator::addAnimation(CView* view, IdStringPtr name, IAnimationTarget* target,
    ITimingFunction* timingFunction, DoneFunction notification = nullptr,
    bool notifyOnCancel = false);
void Animator::removeAnimation(CView* view, IdStringPtr name);

// animations.h
ViewSizeAnimation(const CRect& newRect, bool forceEndValueOnFinish = false);
AlphaValueAnimation(float endValue, bool forceEndValueOnFinish = false);
FuncAnimation(StartFunc&& start, TickFunc&& tick, FinishedFunc&& finished);

// timingfunctions.h line 102-106
static CubicBezierTimingFunction CubicBezierTimingFunction::easyInOut(uint32_t time);
static CubicBezierTimingFunction* CubicBezierTimingFunction::make(Style style, uint32_t time);
```

---

## R2: VSTGUI Window Resize with Aspect Ratio Constraint

**Decision**: Use `VST3Editor::setEditorSizeConstrains()` for min/max bounds, and implement the `checkSizeConstraint()` callback on CFrame (via the editor.uidesc `minSize`/`maxSize` attributes) to enforce the 5:3 aspect ratio. Use `VST3Editor::requestResize()` if the host supports `IPlugFrame`.

**Rationale**: VSTGUI's VST3Editor already provides:
- `setEditorSizeConstrains(CPoint minSize, CPoint maxSize)` - sets min/max editor dimensions
- `requestResize(CPoint newSize)` - requests host to resize the plugin window
- `CFrame::checkSizeConstraint(CPoint newSize)` - enforces constraints on proposed sizes
- The `editor.uidesc` already has `minSize="800, 700"` and `maxSize="1400, 1100"` on the editor template

The existing uidesc values need updating to match the spec's 5:3 ratio:
- Min: 800x500 (not 800x700)
- Max: 1400x900 (not 1400x1100) -- note: 1400/900 is not exactly 5:3 (which is 1400/840), but the spec says max=1400x900. We need to verify: 800/500 = 1.6 and 1400/840 = 1.667. Actually 5:3 = 1.667. But 800x500 = 8:5 = 1.6. The spec says "5:3 aspect ratio" but gives min 800x500 (8:5). This is a contradiction. Given the spec explicitly states 800x500 min and 1400x900 max, and both have approximately 1.5-1.6 ratio, the spec likely means the resize is constrained to snap to these proportions. The actual constraint ratio is: width/height = constant, where the constant is 5:3 (1.667).

Actually: 1000/600 = 1.667 = 5:3 exactly. So default size defines the ratio. Min=800x480 would be 5:3 at 800 width. Max=1400x840 would be 5:3 at 1400 width. But spec says 800x500 and 1400x900. Let me re-read: "minimum window size MUST be 800 pixels wide by 500 pixels tall" and "maximum window size MUST be 1400 pixels wide by 900 pixels tall". These are bounds, not the actual aspect ratio. The aspect ratio is 5:3 and the resize snaps to that ratio. So at 5:3: min would be 834x500 (rounding up width to maintain ratio), and max would be 1400x840. Or we constrain to the 5:3 line and clamp both width and height. Implementation: `checkSizeConstraint` computes the 5:3 constrained size within the min/max bounds.

**Implementation approach**:
- In `createView()`, call `setEditorSizeConstrains(CPoint(800,480), CPoint(1500,900))` to set liberal bounds
- Override aspect ratio enforcement in a custom approach: when the host requests a resize, compute the nearest 5:3 size within bounds
- Store the last window size in controller state (getState/setState) for session persistence (FR-023)
- Use zoom factor approach: default is 1.0 at 1000x600, zoom range 0.8-1.4

**Alternatives considered**:
- Free resize with letterboxing: Spec explicitly says constrained proportional
- Zoom factor only (no resize): Does not satisfy FR-017 "resizable by user"
- Host-only resize: Some hosts don't support IPlugFrame resize, so we also need internal zoom support

**Key API signatures verified**:
```cpp
// vst3editor.h lines 123-125
bool VST3Editor::setEditorSizeConstrains(const CPoint& newMinimumSize, const CPoint& newMaximumSize);
void VST3Editor::getEditorSizeConstrains(CPoint& minimumSize, CPoint& maximumSize) const;
bool VST3Editor::requestResize(const CPoint& newSize);

// vst3editor.h lines 127-128
void VST3Editor::setZoomFactor(double factor);
double VST3Editor::getZoomFactor() const;
```

---

## R3: VSTGUI Keyboard Hook for Shortcuts

**Decision**: Implement `IKeyboardHook` interface and register with `CFrame::registerKeyboardHook()` in `didOpen()`, unregister in `willClose()`.

**Rationale**: VSTGUI provides the `IKeyboardHook` interface (since 4.0, updated in 4.11 for new event API) that allows intercepting all keyboard events before they reach individual views. This is exactly what we need for global shortcuts (Tab, Space, Arrow keys). The hook receives a `KeyboardEvent` with:
- `character` (UTF-32)
- `virt` (VirtualKey enum with Tab, Space, Up, Down, Left, Right, etc.)
- `isRepeat` flag
- Modifier keys via `ModifierEvent` base class

The hook can consume events by calling `event.consumed = true`, preventing further dispatch.

VSTGUI 4.x also has built-in focus drawing support:
- `CFrame::setFocusDrawingEnabled(true)` - enables focus rings
- `CFrame::setFocusColor(CColor)` - sets the focus ring color
- `CFrame::setFocusWidth(CCoord)` - sets the focus ring width (FR-010a: 2px)
- `CFrame::advanceNextFocusView(oldFocus, reverse)` - cycles focus (Tab/Shift+Tab)

**Implementation approach**:
- Create `KeyboardShortcutHandler` implementing `IKeyboardHook`
- Register in `didOpen()`, unregister in `willClose()` (matches existing controller lifecycle)
- Tab/Shift+Tab: Use `CFrame::advanceNextFocusView()` with custom focus chain (band strips only)
- Space: Toggle bypass on focused band (find band index from focused view, toggle kBandBypass parameter)
- Arrow keys: Fine adjust (1/100th range), Shift+Arrow: Coarse adjust (1/10th range) on focused CControl
- Enable focus drawing with accent color and 2px width in `didOpen()`

**Key API signatures verified**:
```cpp
// cframe.h lines 347-354
class IKeyboardHook {
public:
    virtual ~IKeyboardHook() noexcept = default;
    virtual void onKeyboardEvent(KeyboardEvent& event, CFrame* frame) = 0;
};

// cframe.h lines 139-141
void CFrame::registerKeyboardHook(IKeyboardHook* hook);
void CFrame::unregisterKeyboardHook(IKeyboardHook* hook);

// cframe.h lines 109-111
void CFrame::setFocusView(CView* pView);
CView* CFrame::getFocusView() const;
bool CFrame::advanceNextFocusView(CView* oldFocus, bool reverse = false) override;

// cframe.h lines 174-187
void CFrame::setFocusDrawingEnabled(bool state);
bool CFrame::focusDrawingEnabled() const;
void CFrame::setFocusColor(const CColor& color);
CColor CFrame::getFocusColor() const;
void CFrame::setFocusWidth(CCoord width);
CCoord CFrame::getFocusWidth() const;

// events.h lines 420-489, 514-524
enum class VirtualKey : uint32_t { Tab, Space, Left, Up, Right, Down, ... };
enum class ModifierKey : uint32_t { Shift = 1 << 0, Alt = 1 << 1, Control = 1 << 2, ... };
struct KeyboardEvent : ModifierEvent {
    char32_t character {0};
    VirtualKey virt {VirtualKey::None};
    bool isRepeat {false};
};
```

**Alternatives considered**:
- Override `onKeyDown`/`onKeyUp` on individual views: Would require modifying every band strip view. Less maintainable.
- Custom CViewContainer with keyboard handling: Over-engineered for global shortcuts
- No keyboard shortcuts: Spec requires them (FR-010 through FR-016)

---

## R4: Platform Accessibility Detection (High Contrast / Reduced Motion)

**Decision**: Create a shared `AccessibilityHelper` class in `plugins/shared/src/platform/` with platform-specific implementations behind `#ifdef` guards. Query on editor open and cache results.

**Rationale**: There is no cross-platform VSTGUI API for detecting OS accessibility settings. The spec (FR-024, FR-025b, FR-025c, FR-025d, FR-027) explicitly requires platform-specific detection and lists the exact APIs to use:

**Windows**:
- High contrast: `SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(HIGHCONTRAST), &hc, 0)` - returns `HCF_HIGHCONTRASTON` flag and color scheme name
- Reduced motion: `SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &animEnabled, 0)` - returns boolean

**macOS**:
- High contrast: `[[NSWorkspace sharedWorkspace] accessibilityDisplayShouldIncreaseContrast]`
- Reduced motion: `[[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion]`

**Linux**:
- Best-effort: Check `GTK_THEME` environment variable for "HighContrast", or read `org.gnome.desktop.a11y.interface high-contrast` via GSettings. Fallback to default if unavailable.
- Reduced motion: Check `org.gnome.desktop.interface enable-animations` GSettings key. Fallback to false (animations enabled).

The spec explicitly allows platform-specific code for accessibility detection (FR-025b/c/d), and the constitution (Principle VI) allows platform-specific code for documented bug workarounds and optimizations with fallbacks.

**Implementation approach**:
- `plugins/shared/src/platform/accessibility_helper.h` - public header with cross-platform interface
- `plugins/shared/src/platform/accessibility_helper.cpp` - implementation with `#ifdef _WIN32`, `#ifdef __APPLE__`, `#else` (Linux)
- Query once in `didOpen()`, cache the results
- Provide: `isHighContrastEnabled()`, `isReducedMotionPreferred()`, `getHighContrastColors()` (returns a struct with foreground, background, accent colors)
- Constitution compliance: Platform-specific code guarded by `#ifdef`, with documented rationale. Falls back gracefully on unsupported platforms.

**Alternatives considered**:
- VSTGUI-only detection: VSTGUI has no APIs for OS accessibility settings
- Runtime polling with timer: Unnecessary overhead. Detect on editor open per FR-025 ("When the plugin opens")
- No accessibility: Spec requires it (FR-024 through FR-029)

---

## R5: Generalized MIDI CC Mapping (Extending Sweep-Only to All Parameters)

**Decision**: Create a `MidiCCManager` class in `plugins/shared/src/midi/` that manages CC-to-parameter mappings for any parameter, replacing the current sweep-only implementation. Support both global and per-preset mappings with 14-bit CC pairs.

**Rationale**: The existing codebase has:
- `kSweepMidiLearnActive` / `kSweepMidiCCNumber` parameters (sweep-only)
- `getMidiControllerAssignment()` returning only sweep frequency
- `assignedMidiCC_` as a single int

This needs generalization to support:
- Any parameter can be mapped (FR-030)
- Multiple simultaneous mappings (up to 128 global + 128 per-preset)
- MIDI Learn workflow via right-click context menu (FR-031)
- 14-bit CC pairs (FR-038-040)
- Hybrid persistence: global + per-preset (FR-034)

**Implementation approach**:
- `MidiCCMapping` struct: `{ uint8_t ccNumber; ParamID paramId; bool is14Bit; bool isPerPreset; }`
- `MidiCCManager` class: manages a `std::unordered_map<uint8_t, MidiCCMapping>` for global and per-preset
- Override `getMidiControllerAssignment()` to iterate all active mappings
- MIDI Learn: use a "learn mode" state that captures the next CC and creates a mapping
- 14-bit: when mapping CC 0-31, automatically pair with CC 32-63 as LSB
- Context menu: Override `createContextMenu()` in VST3EditorDelegate to add MIDI Learn/Clear options
- Serialization: global mappings in controller state (`getState`/`setState`), per-preset mappings in component state

**Key existing code to extend**:
```cpp
// controller.h line 263
int assignedMidiCC_ = 128;  // Replace with MidiCCManager

// controller.cpp line 2685
getMidiControllerAssignment()  // Extend to query MidiCCManager

// plugin_ids.h line 131-163
kSweepMidiLearnActive, kSweepMidiCCNumber  // Keep for backwards compat, add general MIDI Learn param
```

**Alternatives considered**:
- Keep sweep-only, add per-parameter MIDI params: Would require 450+ new parameters (one learn + one CC per existing parameter). Impractical.
- External MIDI mapping file: Non-standard for VST3, doesn't integrate with host automation
- Host-provided MIDI CC mapping: Most hosts already provide this, but spec requires plugin-side implementation

---

## R6: Modulation Panel Toggle

**Decision**: Reuse the existing `ContainerVisibilityController` pattern with a new `kModulationPanelVisible` parameter. No new controller class needed.

**Rationale**: The modulation panel toggle (FR-007 through FR-009) is functionally identical to the existing band expand/collapse mechanism:
- A boolean parameter controls visibility
- A `ContainerVisibilityController` watches the parameter and shows/hides the container
- The parameter persists in controller state

The only addition is a new parameter ID and a new visibility controller instance in `didOpen()`.

**Implementation approach**:
- Add `kGlobalModulationPanelVisible` to `GlobalParamType` in `plugin_ids.h`
- Register as boolean parameter (stepCount=1) in `registerGlobalParams()`
- Add UI-only container tag (e.g., 9300) for the modulation panel container
- Create `ContainerVisibilityController` in `didOpen()`, deactivate in `willClose()`
- Persist in controller state (already handled by EditControllerEx1)

**Alternatives considered**:
- New custom controller class: Overkill, existing pattern is sufficient
- CViewContainer programmatic add/remove: Unnecessary, setVisible works fine
- Separate modulation panel window: Not consistent with plugin design

---

## R7: VSTGUI Context Menu for MIDI Learn

**Decision**: Override `createContextMenu()` in `VST3EditorDelegate` to add MIDI Learn/Clear/Save-with-Preset options when right-clicking any mappable control.

**Rationale**: The `VST3EditorDelegate` interface includes:
```cpp
COptionMenu* createContextMenu(const CPoint& pos, VST3Editor* editor) override;
```
This is called by VST3Editor when the user right-clicks in the editor. The method receives the click position and can create a COptionMenu with custom items. VST3Editor appends host-provided menu items after the delegate's items.

Currently, the Disrumpo controller does NOT override `createContextMenu()` (the search returned no matches), so it uses the default empty implementation from `VST3EditorDelegate`.

**Implementation approach**:
- Override `createContextMenu()` in Controller
- Determine which parameter is under the mouse click using `findParameter()` or hit-testing
- If a parameter is found:
  - Add "MIDI Learn" item (starts learn mode for that parameter)
  - If already mapped: add "Clear MIDI Learn" item
  - If mapped: add "Save Mapping with Preset" toggle checkbox item
- Use `COptionMenu::addEntry()` with callback lambdas

**Key API**:
```cpp
// VST3EditorDelegate (vst3editor.h line 56)
COptionMenu* createContextMenu(const CPoint& pos, VST3Editor* editor) override;

// VST3EditorDelegate (vst3editor.h line 47-48)
bool findParameter(const CPoint& pos, Steinberg::Vst::ParamID& paramID, VST3Editor* editor) override;
```

---

## R8: State Serialization Version Bump

**Decision**: Bump `kPresetVersion` from 6 to 7 to support new state fields (window size, MIDI CC mappings, modulation panel visibility).

**Rationale**: The controller state needs to persist:
- Window size (width, height) - FR-023
- MIDI CC global mappings (array of CC->ParamID pairs) - FR-034
- Modulation panel visibility - FR-009
- Expand/collapse states already persist (no change needed)

The processor state also needs to add:
- Per-preset MIDI CC mappings (optional, only when "Save with Preset" is checked) - FR-032b

Current `kPresetVersion = 6`. Adding new fields requires a version bump to maintain backwards compatibility: older versions load without the new fields (using defaults), newer versions write the additional data.

**Alternatives considered**:
- Separate state file for MIDI mappings: Non-standard, breaks preset portability
- Store everything in processor state: MIDI mappings are UI-side concerns; window size is controller-only
- No version bump, append to end: Fragile, no backwards compat guarantee
