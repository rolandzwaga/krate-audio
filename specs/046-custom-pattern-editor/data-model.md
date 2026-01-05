# Data Model: Custom Tap Pattern Editor

**Feature**: 046-custom-pattern-editor
**Date**: 2026-01-04

## Entities

### 1. CustomTapPattern

Represents the complete user-defined tap pattern with 16 taps maximum.

**Storage**: VST3 parameters (persisted in plugin state)

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| timeRatios[16] | float | 0.0-1.0 | Linear spread | Time position as ratio of max delay |
| levels[16] | float | 0.0-1.0 | 1.0 | Level as linear gain |
| tapCount | int | 2-16 | 4 | Number of active taps (existing parameter) |

**Relationships**:
- Referenced by: TapPatternEditor (UI), MultiTapDelay (DSP)
- Depends on: kMultiTapTapCountId (existing parameter)

### 2. TapPatternEditor (UI Component)

The VSTGUI CControl that provides visual editing.

**Runtime State** (not persisted):

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| selectedTap_ | int | -1 to 15 | -1 | Currently selected tap (-1 = none) |
| snapDivision_ | SnapDivision | enum | Off | Current grid snap setting |
| dragMode_ | DragMode | enum | None | Current drag state |
| dragStartPoint_ | CPoint | — | — | Mouse position at drag start |
| dragStartTime_ | float | 0.0-1.0 | — | Time ratio at drag start |
| dragStartLevel_ | float | 0.0-1.0 | — | Level at drag start |

**Enumerations**:

```cpp
enum class SnapDivision : uint8_t {
    Off = 0,
    Quarter = 4,
    Eighth = 8,
    Sixteenth = 16,
    ThirtySecond = 32,
    Triplet = 12
};

enum class DragMode : uint8_t {
    None = 0,
    Dragging
};
```

### 3. VST3 Parameter Definitions

New parameters to be added to `plugin_ids.h`:

```cpp
// Custom Pattern Time Ratios (950-965)
kMultiTapCustomTime0Id = 950,
kMultiTapCustomTime1Id = 951,
kMultiTapCustomTime2Id = 952,
kMultiTapCustomTime3Id = 953,
kMultiTapCustomTime4Id = 954,
kMultiTapCustomTime5Id = 955,
kMultiTapCustomTime6Id = 956,
kMultiTapCustomTime7Id = 957,
kMultiTapCustomTime8Id = 958,
kMultiTapCustomTime9Id = 959,
kMultiTapCustomTime10Id = 960,
kMultiTapCustomTime11Id = 961,
kMultiTapCustomTime12Id = 962,
kMultiTapCustomTime13Id = 963,
kMultiTapCustomTime14Id = 964,
kMultiTapCustomTime15Id = 965,

// Custom Pattern Levels (966-981)
kMultiTapCustomLevel0Id = 966,
kMultiTapCustomLevel1Id = 967,
kMultiTapCustomLevel2Id = 968,
kMultiTapCustomLevel3Id = 969,
kMultiTapCustomLevel4Id = 970,
kMultiTapCustomLevel5Id = 971,
kMultiTapCustomLevel6Id = 972,
kMultiTapCustomLevel7Id = 973,
kMultiTapCustomLevel8Id = 974,
kMultiTapCustomLevel9Id = 975,
kMultiTapCustomLevel10Id = 976,
kMultiTapCustomLevel11Id = 977,
kMultiTapCustomLevel12Id = 978,
kMultiTapCustomLevel13Id = 979,
kMultiTapCustomLevel14Id = 980,
kMultiTapCustomLevel15Id = 981,
```

### 4. DSP Storage Extension

Addition to `MultiTapDelay` class:

```cpp
// Existing
std::array<float, kMaxTaps> customTimeRatios_ = {};

// NEW
std::array<float, kMaxTaps> customLevels_ = {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
};
```

---

## State Transitions

### Pattern Selection

```
┌─────────────────┐      User selects         ┌─────────────────┐
│  Preset Pattern │  ──── Custom ────────────▶│  Custom Pattern │
│  (0-18)         │                           │  (19)           │
└─────────────────┘                           └─────────────────┘
        │                                             │
        │                                             │
        │   "Copy to Custom"                          │
        └─────────────────────────────────────────────┘
```

### Tap Editing

```
┌─────────┐    mouseDown     ┌──────────┐    mouseUp      ┌─────────┐
│  IDLE   │ ───on tap───────▶│ DRAGGING │ ───────────────▶│  IDLE   │
└─────────┘                  └──────────┘                 └─────────┘
     │                             │
     │                             │ mouseMove
     │                             ▼
     │                       Update time/level
     │                       Invalidate view
     │                       Notify parameter change
```

---

## Validation Rules

### Time Ratio
- Must be in range [0.0, 1.0]
- Applied: Clamped at UI and processor levels

### Level
- Must be in range [0.0, 1.0]
- Applied: Clamped at UI level
- 0.0 = silent tap (still visible in editor)

### Tap Count
- Range: 2-16 (existing constraint)
- Controlled by existing `kMultiTapTapCountId` parameter
- Editor shows/hides taps based on this value

### Grid Snap
- Applied only during drag operations
- Time ratio snapped to nearest grid division
- Level not snapped (free vertical movement)

---

## Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                         UI Thread                                │
├─────────────────────────────────────────────────────────────────┤
│  TapPatternEditor                                                │
│       │                                                          │
│       │ User drags tap                                           │
│       ▼                                                          │
│  beginEdit() ──▶ setParamNormalized() ──▶ performEdit()         │
│       │                                      │                   │
│       │                                      │ IParameterChanges │
│       │                                      ▼                   │
├───────┼──────────────────────────────────────┼───────────────────┤
│       │              Audio Thread            │                   │
│       │                                      ▼                   │
│       │         processParameterChanges()                        │
│       │                │                                         │
│       │                ▼                                         │
│       │         customTimeRatios_[i] = value                     │
│       │         customLevels_[i] = value                         │
│       │                │                                         │
│       │                ▼                                         │
│       │         applyCustomTimingPattern()                       │
│       │                │                                         │
│       │                ▼                                         │
│       │         TapManager.setTapTimeMs()                        │
│       │         TapManager.setTapLevelDb()                       │
│       │                                                          │
│       │                                                          │
│       ▼                                                          │
│  endEdit()                                                       │
└─────────────────────────────────────────────────────────────────┘
```

---

## State Persistence

### Save (Processor::getState)

```cpp
// In saveMultiTapParams():
for (int i = 0; i < 16; ++i) {
    streamer.writeFloat(customTimeRatios[i]);
}
for (int i = 0; i < 16; ++i) {
    streamer.writeFloat(customLevels[i]);
}
```

### Load (Processor::setState)

```cpp
// In loadMultiTapParams():
for (int i = 0; i < 16; ++i) {
    streamer.readFloat(customTimeRatios[i]);
}
for (int i = 0; i < 16; ++i) {
    streamer.readFloat(customLevels[i]);
}
```

### Compatibility

- Old presets without custom pattern data: Use default values (linear spread, full levels)
- Version detection: Check stream position after reading existing params
