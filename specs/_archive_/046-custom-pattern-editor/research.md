# Research: Custom Tap Pattern Editor

**Feature**: 046-custom-pattern-editor
**Date**: 2026-01-04

## Phase 0 Research Results

This document consolidates research findings from the planning phase, resolving all technical unknowns identified in the Technical Context.

---

## 1. VSTGUI Custom View Implementation

### Decision
Inherit from `CControl` (not `CView`) for interactive pattern editing.

### Rationale
- `CControl` provides `beginEdit()`/`endEdit()` for proper VST3 automation recording
- Built-in tag-based parameter binding via `valueChanged()`
- Existing reference: `ModeTabBar` uses `CView` but doesn't need parameter binding; `TapPatternEditor` does

### Alternatives Considered
| Option | Pros | Cons |
|--------|------|------|
| CView subclass | Simpler | No built-in parameter binding |
| CControl subclass | Parameter binding, automation | Slightly more complex |
| Multiple CControl instances | One control per tap | Complex layout management, 32 controls |

### Implementation Details
```cpp
class TapPatternEditor : public VSTGUI::CControl {
    // Single control managing all 16 taps internally
    // Uses multiple tags internally for 32 parameters
};
```

---

## 2. Parameter ID Allocation

### Decision
Use ID range **950-981** for custom pattern parameters (32 total).

### Rationale
- MultiTap range is 900-999, currently using up to 912
- 950-965: Time ratio parameters (16)
- 966-981: Level parameters (16)
- Leaves 913-949 available for future MultiTap params

### Parameter Layout
```cpp
// Custom Pattern Time Ratios (950-965)
kMultiTapCustomTime0Id = 950,
kMultiTapCustomTime1Id = 951,
// ... through
kMultiTapCustomTime15Id = 965,

// Custom Pattern Levels (966-981)
kMultiTapCustomLevel0Id = 966,
kMultiTapCustomLevel1Id = 967,
// ... through
kMultiTapCustomLevel15Id = 981,
```

---

## 3. Time Ratio vs Absolute Time

### Decision
Store **time ratios (0.0-1.0)** as parameters, not absolute millisecond values.

### Rationale
- Parameters are normalized by VST3 convention
- Ratio of 1.0 = 100% of base time (max delay window)
- Base time controlled by Note Value + tempo (for mathematical patterns) or pattern selection
- Allows pattern to scale with tempo changes

### Conversion
```cpp
// In processor: convert ratio to absolute time
float absoluteTimeMs = ratio * baseTimeMs_;

// In editor: convert pixel position to ratio
float ratio = (xPosition - viewLeft) / viewWidth;
```

---

## 4. Level Storage Format

### Decision
Store levels as **linear gain (0.0-1.0)** in parameters, display as percentage.

### Rationale
- Simpler than dB storage for UI dragging
- Direct mapping: y-position → level
- 0.0 = silent, 1.0 = full level (0 dB)
- DSP already handles level as linear gain internally

### Alternatives Considered
| Format | Pros | Cons |
|--------|------|------|
| Linear 0-1 | Simple UI mapping | Not perceptually uniform |
| dB -96 to 0 | Perceptually uniform | Complex UI mapping |

---

## 5. Custom Pattern Visibility Control

### Decision
Use existing visibility controller pattern (pattern-based, like Note Value visibility).

### Rationale
- MultiTap already has pattern-based visibility for Note Value
- Show editor when `kMultiTapTimingPatternId == 19` (Custom)
- Consistent with existing UI patterns

### Implementation
```cpp
// In controller.cpp - create visibility controller
// Similar to multitapNoteValueVisibilityController_
// But triggers on pattern == Custom (index 19)
```

---

## 6. DSP Level Support

### Decision
Extend `MultiTapDelay::applyCustomTimingPattern()` to also apply custom levels.

### Rationale
- Currently `customTimeRatios_` exists but no `customLevels_`
- Need to add `customLevels_` array and apply via `TapManager::setTapLevelDb()`
- Minimal DSP change, most complexity is in UI

### Implementation
```cpp
// Add to MultiTapDelay:
std::array<float, kMaxTaps> customLevels_ = {1.0f, 1.0f, ...}; // Default full level

void applyCustomTimingPattern() noexcept {
    for (size_t i = 0; i < activeTapCount_; ++i) {
        float timeMs = std::min(baseTimeMs_ * customTimeRatios_[i], maxDelayMs_);
        tapManager_.setTapEnabled(i, true);
        tapManager_.setTapTimeMs(i, timeMs);
        tapManager_.setTapLevelDb(i, gainToDb(customLevels_[i])); // NEW
    }
    // ...
}
```

---

## 7. Grid Snap Divisions

### Decision
Support divisions: Off, 1/4, 1/8, 1/16, 1/32, Triplet (1/12).

### Rationale
- Matches standard musical subdivisions
- 1/12 for triplets (3 per quarter note × 4 quarters)
- "Off" allows free positioning

### Implementation
```cpp
enum class SnapDivision : uint8_t {
    Off = 0,
    Quarter = 4,    // 4 divisions
    Eighth = 8,     // 8 divisions
    Sixteenth = 16, // 16 divisions
    ThirtySecond = 32,
    Triplet = 12    // 12 divisions (triplets)
};

float snapToGrid(float ratio, SnapDivision div) {
    if (div == SnapDivision::Off) return ratio;
    float step = 1.0f / static_cast<float>(div);
    return std::round(ratio / step) * step;
}
```

---

## 8. Copy from Mathematical Pattern

### Decision
Implement as button that reads current tap times/levels and copies to custom parameters.

### Rationale
- User selects a mathematical pattern (Golden Ratio, Fibonacci, etc.)
- Click "Copy to Custom" button
- Pattern times and levels are read from TapManager and written to custom parameters
- User can then switch to Custom pattern and modify

### Implementation Flow
1. User clicks "Copy to Custom" button
2. Read current tap times: `TapManager::getTapTimeMs(i)`
3. Convert to ratios: `ratio = timeMs / maxDelayMs`
4. Read current tap levels: `TapManager::getTapLevelDb(i)`
5. Convert to linear: `level = dbToGain(levelDb)`
6. Write to custom parameters via controller
7. Optionally switch to Custom pattern automatically

---

## 9. Editor Size and Layout

### Decision
Minimum size 400x150 pixels, scalable with UI.

### Rationale
- 400px width allows comfortable tap spacing for 16 taps (25px per tap)
- 150px height allows clear level visualization
- Matches reference in custom-pattern-editor-plan.md

### Layout
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
│      0                                              100%   │
├────────────────────────────────────────────────────────────┤
│ [Reset] [Copy from Pattern]                     Taps: 8/16 │
└────────────────────────────────────────────────────────────┘
```

---

## 10. Mouse Interaction Model

### Decision
Single click selects tap, drag modifies time (horizontal) and level (vertical) simultaneously.

### Rationale
- Intuitive 2D control
- Matches user expectation from similar editors (DAW automation, EQ curves)
- No mode switching needed

### State Machine
```
IDLE → (mouseDown on tap) → DRAGGING → (mouseUp) → IDLE
     ↘ (mouseDown on empty) → no action (could add tap in future)
```

### Implementation
```cpp
CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) {
    int tapIndex = hitTestTap(where);
    if (tapIndex >= 0) {
        selectedTap_ = tapIndex;
        dragStartPoint_ = where;
        dragStartTime_ = tapTimeRatios_[tapIndex];
        dragStartLevel_ = tapLevels_[tapIndex];
        beginEdit();  // Start automation recording
        return kMouseEventHandled;
    }
    return kMouseEventNotHandled;
}
```

---

## Summary of Decisions

| Topic | Decision |
|-------|----------|
| Base class | CControl (for parameter binding) |
| Parameter IDs | 950-981 (32 params) |
| Time storage | Ratios 0.0-1.0 |
| Level storage | Linear gain 0.0-1.0 |
| Visibility | Pattern-based (Custom = index 19) |
| DSP extension | Add customLevels_ array |
| Grid snapping | Off, 1/4, 1/8, 1/16, 1/32, Triplet |
| Copy pattern | Button reads current, writes to custom |
| Editor size | 400x150 minimum |
| Mouse model | 2D drag for time + level |
