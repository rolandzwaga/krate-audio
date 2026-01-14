# Plugin Architecture

[← Back to Architecture Index](README.md)

---

## VST3 Components

| Component | Path | Purpose |
|-----------|------|---------|
| Processor | `plugins/iterum/src/processor/` | Audio processing (real-time) |
| Controller | `plugins/iterum/src/controller/` | UI state management |
| Entry | `plugins/iterum/src/entry.cpp` | Factory registration |
| IDs | `plugins/iterum/src/plugin_ids.h` | Parameter and component IDs |

---

## Parameter Flow

```
Host → Processor.processParameterChanges() → atomics → process()
                                                    ↓
Host ← Controller.setParamNormalized() ← IMessage ←┘
```

---

## State Flow

```
Save: Processor.getState() → IBStream (parameters + version)
Load: IBStream → Processor.setState() → Controller.setComponentState()
```

---

## UI Components

| Component | Path | Purpose |
|-----------|------|---------|
| TapPatternEditor | `plugins/iterum/src/ui/tap_pattern_editor.h` | Custom tap pattern visual editor |
| CopyPatternButton | `plugins/iterum/src/controller/controller.cpp` | Copy current pattern to custom |
| ResetPatternButton | `plugins/iterum/src/controller/controller.cpp` | Reset pattern to linear spread |

---

## TapPatternEditor (Spec 046)
**Path:** [tap_pattern_editor.h](../../plugins/iterum/src/ui/tap_pattern_editor.h) | **Since:** 0.9.6

Visual editor for creating custom delay tap patterns. Extends `VSTGUI::CControl`.

```cpp
class TapPatternEditor : public VSTGUI::CControl {
    void setTapTimeRatio(size_t tapIndex, float ratio);  // [0, 1]
    void setTapLevel(size_t tapIndex, float level);      // [0, 1]
    void setActiveTapCount(size_t count);                // 1-16
    void setSnapDivision(SnapDivision division);         // Grid snapping
    void resetToDefault();                                // Linear spread, full levels
    void onPatternChanged(int patternIndex);             // Cancel drag if not Custom
    void setParameterCallback(ParameterCallback cb);     // Notify on user drag
};
```

**Features:**
- Horizontal drag adjusts tap timing (X axis = time ratio 0-1)
- Vertical drag adjusts tap level (Y axis = level 0-1)
- Grid snapping: Off, 1/4, 1/8, 1/16, 1/32, Triplet
- Only editable when pattern == Custom (index 19)
- Copy from any pattern to use as starting point

**Parameter IDs (Custom Pattern):**
- Time ratios: `kMultiTapCustomTime0Id` - `kMultiTapCustomTime15Id` (950-965)
- Levels: `kMultiTapCustomLevel0Id` - `kMultiTapCustomLevel15Id` (966-981)
- UI tags: `kMultiTapPatternEditorTagId` (920), `kMultiTapCopyPatternButtonTagId` (921), `kMultiTapResetPatternButtonTagId` (923)

**Visibility:** Controlled by `patternEditorVisibilityController_` using IDependent pattern. Visible only when `kMultiTapTimingPatternId` == 19 (Custom).
