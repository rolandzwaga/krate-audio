# Custom Pattern Editor Implementation Plan

## Executive Summary

This document details the implementation of a custom tap pattern editor for the MultiTap delay mode, allowing users to visually define and manipulate delay tap timing and level patterns.

**Estimated Complexity: MEDIUM-HIGH**

The implementation requires:
- 1-2 new custom VSTGUI view classes
- 32 new VST3 parameters (16 time ratios + 16 levels)
- Significant drawing code for visualization
- Mouse interaction handling for draggable elements
- Real-time parameter synchronization
- Reuses existing `kMultiTapTapCountId` for tap count

---

## 1. Research Summary

### 1.1 VSTGUI Capabilities

Based on research from the [official VSTGUI documentation](https://steinbergmedia.github.io/vst3_doc/vstgui/html/):

**Custom View Creation:**
- Inherit from `CView` (display only) or `CControl` (interactive)
- Override `draw(CDrawContext*)` for rendering
- Override `onMouseDown`, `onMouseMoved`, `onMouseUp` for interaction
- Return `kMouseEventHandled` from mouse handlers to receive subsequent events

**Drawing API (CDrawContext):**
- `drawLine(CPoint start, CPoint end)` - Line drawing
- `drawRect(CRect, CDrawStyle)` - Rectangle (filled/stroked)
- `drawEllipse(CRect, CDrawStyle)` - Circles/ellipses
- `drawPolygon(PointList, CDrawStyle)` - Arbitrary polygons
- `setFillColor(CColor)`, `setFrameColor(CColor)` - Color settings
- `setLineWidth(CCoord)` - Line thickness

**Parameter Binding:**
- Use `CControl::setValue()` / `getValue()` for value management
- Call `beginEdit()` before changes, `endEdit()` after
- `valueChanged()` notifies listeners
- Tags link controls to VST3 parameter IDs

### 1.2 Commercial Reference Implementations

From [KVR Audio](https://www.kvraudio.com/) and plugin analysis:

**Cluster Delay (Minimal Audio):**
- Timeline with up to 8 taps
- Drag left/right for timing, up/down for volume
- Visual feedback bridges delay and sequencer concepts

**BEAM Taps (Lunacy):**
- 8 taps with individual pitch control
- Spacing and swing controls
- Multiple pitch modes

**Stepic (Devicemeister):**
- 16 patterns, pattern chaining
- Drag & drop MIDI export
- Fully scalable UI

### 1.3 Existing Project Patterns

Our codebase already implements custom views:
- `ModeTabBar` - Simple CView with click handling
- `PresetBrowserView` - Complex modal with list rendering
- `SavePresetDialogView` - Input dialog with validation

---

## 2. Feature Specification

### 2.1 Core Features

1. **Visual Tap Timeline**
   - Horizontal timeline showing tap positions
   - Vertical bars representing tap levels
   - Grid lines for timing reference (beat subdivisions)

2. **Draggable Tap Handles**
   - Horizontal drag adjusts tap time ratio
   - Vertical drag adjusts tap level
   - Visual feedback during drag (highlight, snap indicators)

3. **Tap Count**
   - Tap count controlled by existing `kMultiTapTapCountId` (2-16 taps)
   - Editor displays only the active number of taps
   - No add/remove in editor - use existing Tap Count slider

4. **Grid Snapping**
   - Optional snap-to-grid for timing
   - Snap values: 1/4, 1/8, 1/16, 1/32, triplets

5. **Preset Patterns**
   - Quick-select common patterns as starting points
   - Import pattern from current mathematical pattern

### 2.2 Data Model

Each tap requires:
- **Time Ratio**: 0.0 - 1.0 (relative position in delay window)
- **Level**: 0.0 - 1.0 (tap amplitude)
- **Pan** (optional): -1.0 to 1.0 (stereo position)

For 16 taps maximum:
- 16 time ratio parameters
- 16 level parameters
- (16 pan parameters if spatial editing is included)

Total: **32-48 new parameters**

### 2.3 Integration Points

1. **MultiTapDelay DSP** (`multi_tap_delay.h`)
   - Already has `setCustomPattern(const std::array<float, kMaxTaps>& timeRatios)`
   - Need to add level ratios to custom pattern

2. **Controller** (`controller.cpp`)
   - `createCustomView()` returns pattern editor
   - Visibility controlled by Pattern == Custom

3. **editor.uidesc**
   - Add custom-view-name attribute
   - Position in MultiTap panel

4. **State Persistence**
   - Save/load custom pattern with preset
   - Add to `multitap_params.h`

---

## 3. Architecture Design

### 3.1 Class Structure

```
┌─────────────────────────────────────────────────────────────┐
│                  TapPatternEditor                           │
│                  (CControl subclass)                        │
├─────────────────────────────────────────────────────────────┤
│ - tapCount_: int                                            │
│ - tapTimeRatios_[16]: float                                 │
│ - tapLevels_[16]: float                                     │
│ - selectedTap_: int (-1 if none)                            │
│ - dragMode_: enum {None, Time, Level, Both}                 │
│ - snapEnabled_: bool                                        │
│ - snapDivision_: int                                        │
├─────────────────────────────────────────────────────────────┤
│ + draw(CDrawContext*)                                       │
│ + onMouseDown(CPoint&, CButtonState&) -> CMouseEventResult  │
│ + onMouseMoved(CPoint&, CButtonState&) -> CMouseEventResult │
│ + onMouseUp(CPoint&, CButtonState&) -> CMouseEventResult    │
│ + setTapData(int tap, float time, float level)              │
│ + getTapData(int tap, float& time, float& level)            │
│ + setTapCount(int count)                                    │
│ + setSnapEnabled(bool enabled, int division)                │
├─────────────────────────────────────────────────────────────┤
│ - drawBackground(CDrawContext*)                             │
│ - drawGridLines(CDrawContext*)                              │
│ - drawTaps(CDrawContext*)                                   │
│ - drawSelectedHighlight(CDrawContext*)                      │
│ - hitTestTap(CPoint) -> int                                 │
│ - positionToTimeRatio(CCoord x) -> float                    │
│ - positionToLevel(CCoord y) -> float                        │
│ - snapToGrid(float timeRatio) -> float                      │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 Callback Interface

```cpp
// Listener for pattern changes
class ITapPatternListener {
public:
    virtual ~ITapPatternListener() = default;

    // Called when any tap is modified
    virtual void onTapChanged(int tapIndex, float timeRatio, float level) = 0;

    // Called when entire pattern is replaced (preset load, etc.)
    virtual void onPatternChanged() = 0;
};

// NOTE: Tap count is controlled by existing kMultiTapTapCountId parameter.
// The editor observes this parameter and displays/edits only the active taps.
// Users change tap count via the existing Tap Count slider, not via the editor.
```

### 3.3 Parameter Layout

New parameter IDs in `plugin_ids.h`:

```cpp
// Custom Pattern Parameters (ID range: 950-999)
// Time ratios: 950-965 (16 taps)
constexpr int32_t kMultiTapCustomTime0Id = 950;
constexpr int32_t kMultiTapCustomTime1Id = 951;
// ... through kMultiTapCustomTime15Id = 965

// Level ratios: 966-981 (16 taps)
constexpr int32_t kMultiTapCustomLevel0Id = 966;
constexpr int32_t kMultiTapCustomLevel1Id = 967;
// ... through kMultiTapCustomLevel15Id = 981

// NOTE: Tap count uses existing kMultiTapTapCountId (902)
// No separate custom tap count parameter needed
```

---

## 4. Visual Design

### 4.1 Layout Specification

```
┌────────────────────────────────────────────────────────────┐
│ CUSTOM PATTERN EDITOR                          [Snap: 1/8] │
├────────────────────────────────────────────────────────────┤
│ 100% ────────────────────────────────────────────────────  │
│  │                ▄                                        │
│  │       ▄        █        ▄                    ▄          │
│  │   ▄   █   ▄    █   ▄    █        ▄      ▄    █     ▄   │
│  │   █   █   █    █   █    █   ▄    █  ▄   █    █  ▄  █   │
│  │   █   █   █    █   █    █   █    █  █   █    █  █  █   │
│  0% ─┴───┴───┴────┴───┴────┴───┴────┴──┴───┴────┴──┴──┴── │
│      │   │   │    │   │    │   │    │  │   │    │  │  │    │
│      0  1/8 1/4  3/8 1/2  5/8 3/4  7/8  1   (time ratio)   │
├────────────────────────────────────────────────────────────┤
│ [Add Tap] [Clear All] [From Pattern ▼]          Taps: 8/16 │
└────────────────────────────────────────────────────────────┘
```

### 4.2 Visual Elements

| Element | Color (Dark Theme) | Notes |
|---------|-------------------|-------|
| Background | #2A2A2A | Panel background |
| Grid lines | #404040 | Beat divisions |
| Grid major | #505050 | Quarter notes |
| Tap bar fill | #4A90D9 | Normal state |
| Tap bar selected | #6AB0F9 | Selected tap |
| Tap handle | #FFFFFF | Drag handle circle |
| Time indicator | #F0A030 | Playback position |

### 4.3 Dimensions

- **Editor size**: 400 x 150 pixels (minimum)
- **Tap bar width**: 8 pixels
- **Handle radius**: 6 pixels
- **Grid line width**: 1 pixel
- **Margins**: 10 pixels all sides

---

## 5. Implementation Phases

### Phase 1: Core View Framework (3-4 hours)

**Files to create:**
- `plugins/iterum/src/ui/tap_pattern_editor.h`
- `plugins/iterum/src/ui/tap_pattern_editor.cpp`

**Tasks:**
1. Create `TapPatternEditor` class inheriting from `CControl`
2. Implement basic `draw()` with placeholder rendering
3. Add to `createCustomView()` in controller
4. Add view to `editor.uidesc` with `custom-view-name`
5. Verify view appears in UI

**Test:**
- [ ] View renders in MultiTap panel
- [ ] View has correct size and position

### Phase 2: Drawing Implementation (4-5 hours)

**Tasks:**
1. Implement `drawBackground()` with panel styling
2. Implement `drawGridLines()` with configurable divisions
3. Implement `drawTaps()` with bar rendering
4. Add tap count display
5. Add timing labels on x-axis
6. Add level labels on y-axis

**Test:**
- [ ] Grid renders correctly at different sizes
- [ ] Taps display with correct positions and heights
- [ ] Labels are readable and positioned correctly

### Phase 3: Mouse Interaction (4-5 hours)

**Tasks:**
1. Implement `hitTestTap()` for tap detection
2. Implement `onMouseDown()` for tap selection and drag start
3. Implement `onMouseMoved()` for dragging
4. Implement `onMouseUp()` for drag completion
5. Add snap-to-grid logic
6. Add visual feedback during drag (highlight, cursor change)
7. Listen for tap count parameter changes and redraw

**Test:**
- [ ] Clicking selects tap
- [ ] Dragging horizontally changes time
- [ ] Dragging vertically changes level
- [ ] Snap works when enabled
- [ ] Changing tap count slider updates editor display

### Phase 4: Parameter Integration (4-5 hours)

**Files to modify:**
- `plugins/iterum/src/plugin_ids.h`
- `plugins/iterum/src/parameters/multitap_params.h`
- `plugins/iterum/src/processor/processor.cpp`
- `plugins/iterum/src/controller/controller.cpp`

**Tasks:**
1. Add 32 new parameter IDs (16 time + 16 level)
2. Add atomics to `MultiTapParams` struct
3. Add parameter handlers in processor
4. Register parameters in controller
5. Add state save/load for custom pattern
6. Implement `beginEdit()`/`endEdit()` calls in editor
7. Sync editor display with parameter values (including existing tap count)

**Test:**
- [ ] Parameters appear in host automation
- [ ] Editor changes update parameters
- [ ] Parameter automation updates editor
- [ ] Pattern persists with preset save/load

### Phase 5: DSP Integration (3-4 hours)

**Files to modify:**
- `dsp/include/krate/dsp/effects/multi_tap_delay.h`

**Tasks:**
1. Add `setCustomPattern(timeRatios[], levels[], count)` overload
2. Update `applyTimingPattern()` to use level data
3. Apply custom levels when Custom pattern is selected
4. Test with various patterns

**Test:**
- [ ] Custom pattern produces expected tap delays
- [ ] Custom levels affect tap amplitudes
- [ ] Morphing works between patterns

### Phase 6: Polish & Refinement (3-4 hours)

**Tasks:**
1. Add "From Pattern" dropdown to copy mathematical patterns
2. Add "Reset" button (restore default evenly-spaced pattern)
3. Improve visual feedback (hover states, animations)
4. Add keyboard shortcuts (arrow keys nudge selected tap)
5. Add visibility controller (show only when Custom pattern selected)

**Test:**
- [ ] "From Pattern" copies current pattern to custom
- [ ] Reset restores default pattern
- [ ] View appears/disappears with pattern selection
- [ ] Keyboard navigation works

---

## 6. Risk Assessment

### 6.1 Technical Risks

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Parameter count exceeds host limits | Low | High | Some hosts have 1000+ limit; test in target DAWs |
| Mouse tracking issues on macOS | Medium | Medium | Use VSTGUI's cross-platform abstraction; test early |
| Performance with 16 taps redrawing | Low | Medium | Use dirty rects; only redraw changed areas |
| Preset compatibility breaking | Medium | High | Version the preset format; handle old presets |

### 6.2 UX Risks

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Users confused by time ratio vs ms | Medium | Medium | Show ms values based on current tempo |
| Too small for comfortable editing | Low | Medium | Make size configurable or use modal popup |
| Accidental tap deletion | Medium | Low | Require double-click; add undo later |

---

## 7. Alternatives Considered

### 7.1 Modal Editor Popup

**Pros:**
- Larger editing area
- Doesn't clutter main UI
- Can include more controls

**Cons:**
- Extra click to access
- Loses real-time preview feel
- More complex lifecycle management

**Decision:** Start with inline editor; add modal option later if needed.

### 7.2 Separate Level and Time Lanes

**Pros:**
- Clearer separation of concerns
- Easier to implement

**Cons:**
- Takes more vertical space
- Less intuitive than combined view

**Decision:** Use combined view with 2D dragging for intuitive UX.

### 7.3 Text-Based Pattern Entry

**Pros:**
- Simpler implementation
- Precise numeric entry

**Cons:**
- Not visual
- Harder to use for non-technical users

**Decision:** Use visual editor; text entry not suitable for target users.

---

## 8. Testing Strategy

### 8.1 Unit Tests

```cpp
// test: tap_pattern_editor_test.cpp

TEST_CASE("TapPatternEditor hit testing", "[ui][pattern-editor]") {
    // Verify hitTestTap returns correct tap index
}

TEST_CASE("TapPatternEditor position conversion", "[ui][pattern-editor]") {
    // Verify positionToTimeRatio and positionToLevel
}

TEST_CASE("TapPatternEditor snap to grid", "[ui][pattern-editor]") {
    // Verify snapToGrid with various divisions
}
```

### 8.2 Integration Tests

```cpp
// test: multitap_custom_pattern_test.cpp

TEST_CASE("Custom pattern parameter roundtrip", "[multitap][custom]") {
    // Set custom pattern parameters
    // Verify DSP receives correct tap configuration
}

TEST_CASE("Custom pattern preset save/load", "[multitap][custom][preset]") {
    // Save preset with custom pattern
    // Load preset
    // Verify pattern restored correctly
}
```

### 8.3 Manual Testing Checklist

- [ ] Add tap by clicking empty area
- [ ] Remove tap by double-clicking
- [ ] Drag tap horizontally (time changes)
- [ ] Drag tap vertically (level changes)
- [ ] Drag tap diagonally (both change)
- [ ] Snap to grid works
- [ ] Snap divisions change with dropdown
- [ ] "Clear All" removes all taps
- [ ] "From Pattern" copies current pattern
- [ ] Editor hidden when pattern != Custom
- [ ] Editor visible when pattern == Custom
- [ ] Pattern survives save/load
- [ ] Pattern works with different tempos
- [ ] Pattern works with tempo sync off

---

## 9. Dependencies

### 9.1 External Dependencies

None required. All functionality available in VSTGUI.

### 9.2 Internal Dependencies

| Dependency | Status | Notes |
|------------|--------|-------|
| `multi_tap_delay.h` | Exists | Need to add level support |
| `multitap_params.h` | Exists | Need to add custom pattern params |
| `controller.cpp` | Exists | Need to add createCustomView |
| `editor.uidesc` | Exists | Need to add view element |

---

## 10. Estimated Total Effort

| Phase | Estimated Hours |
|-------|-----------------|
| Phase 1: Core Framework | 3-4 |
| Phase 2: Drawing | 4-5 |
| Phase 3: Mouse Interaction | 4-5 |
| Phase 4: Parameter Integration | 4-5 |
| Phase 5: DSP Integration | 3-4 |
| Phase 6: Polish | 3-4 |
| **Total** | **21-27 hours** |

**Complexity Assessment: MEDIUM-HIGH**

This is a substantial feature requiring:
- Custom VSTGUI view with complex drawing
- 2D drag interaction with hit testing
- 32 new VST3 parameters (reuses existing tap count)
- DSP modifications for level support
- Extensive testing across platforms

---

## 11. References

### Official Documentation
- [VSTGUI View System](https://steinbergmedia.github.io/vst3_doc/vstgui/html/the_view_system.html)
- [VSTGUI CDrawContext API](https://steinbergmedia.github.io/vst3_doc/vstgui/html/class_v_s_t_g_u_i_1_1_c_draw_context.html)
- [VSTGUI CControl API](https://steinbergmedia.github.io/vst3_doc/vstgui/html/class_v_s_t_g_u_i_1_1_c_control.html)
- [VSTGUI Custom Views](https://steinbergmedia.github.io/vst3_doc/vstgui/html/create_your_own_view.html)

### Tutorials
- [Will Pirkle's Advanced GUI Tutorials](https://www.willpirkle.com/support/advanced-gui-tutorials/)
- [Writing Custom Views in VSTGUI 4.x](https://arne-scheffler.de/2015/09/13/vstgui_inherit_cview.html)

### Commercial References
- [Cluster Delay by Minimal Audio](https://www.pluginboutique.com/) - Timeline tap editor
- [BEAM Taps by Lunacy](https://www.kvraudio.com/product/beam-taps-by-lunacy) - 8-tap editor with pitch
- [Euclyd by Artists in DSP](https://www.kvraudio.com/product/euclyd-by-artists-in-dsp) - Euclidean pattern editor

---

## 12. Approval

- [ ] Technical approach reviewed
- [ ] Parameter count approved (32 new parameters)
- [ ] UI design approved
- [ ] Integration points identified
- [ ] Testing strategy approved
- [ ] Effort estimate acceptable

---

*Document created: 2026-01-04*
*Last updated: 2026-01-04*
