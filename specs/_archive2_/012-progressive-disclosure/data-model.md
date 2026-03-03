# Data Model: Progressive Disclosure & Accessibility

**Feature Branch**: `012-progressive-disclosure`
**Date**: 2026-01-31

## Entities

### 1. Band Expand/Collapse State (Existing - Extend)

**Location**: Already defined as `BandParamType::kBandExpanded = 0x05` in `plugin_ids.h`

| Field | Type | Range | Default | Notes |
|-------|------|-------|---------|-------|
| expanded | boolean (normalized 0.0/1.0) | 0 or 1 | 0 (collapsed) | Per-band, 8 instances |

**Relationships**: Each band (0-7) has one expanded state. Hidden bands (band index >= band count) ignore expand actions (FR-004).

**State transitions**:
```
Collapsed (0.0) --[click expand]--> Expanding (animation) --> Expanded (1.0)
Expanded (1.0) --[click collapse]--> Collapsing (animation) --> Collapsed (0.0)
Expanding (animation) --[click collapse]--> Collapsing (from current position)
Collapsing (animation) --[click expand]--> Expanding (from current position)
```

**Existing components**:
- Parameter: `makeBandParamId(band, BandParamType::kBandExpanded)`
- Controller: `expandedVisibilityControllers_[band]` in `controller.h:213`
- UI tag: `9100 + band` for expanded container

**New work**: Add animation to transitions (R1). The `AnimatedExpandController` replaces instant `setVisible()` with animated height/opacity changes.

---

### 2. Modulation Panel Visibility (New)

| Field | Type | Range | Default | Notes |
|-------|------|-------|---------|-------|
| modulationPanelVisible | boolean (normalized 0.0/1.0) | 0 or 1 | 0 (hidden) | Single global parameter |

**Parameter ID**: `makeGlobalParamId(GlobalParamType::kGlobalModPanelVisible)` -- requires adding `kGlobalModPanelVisible = 0x06` to `GlobalParamType` enum.

**UI tag**: `9300` for modulation panel container.

**Validation**: Hiding does NOT disable active modulation routings (FR-008). The parameter only controls UI visibility.

---

### 3. Window Size State (New - Controller State Only)

| Field | Type | Range | Default | Notes |
|-------|------|-------|---------|-------|
| windowWidth | double | 800.0-1400.0 | 1000.0 | Pixels |
| windowHeight | double | 480.0-840.0 | 600.0 | Pixels (5:3 ratio from width) |

**Persistence**: Stored in controller state (`getState`/`setState`), NOT as a parameter. The window size is not automatable and has no parameter ID.

**Validation rules**:
- Width clamped to [800, 1400]
- Height computed from width: `height = width / (5.0/3.0) = width * 0.6`
- Aspect ratio: 5:3 (1.6667)
- Actual min: 800x480, actual max: 1400x840 at 5:3 ratio
- Note: Spec says min 800x500 and max 1400x900. These are bounds, not aspect-locked sizes. The exact 5:3 sizes within those bounds are 800x480 (too small for 500 min height) and 834x500 (exact 5:3 at 500 height). We will use 834x500 as effective minimum and 1400x840 as effective maximum to maintain exact 5:3 ratio.

---

### 4. MIDI CC Mapping (New)

```cpp
struct MidiCCMapping {
    uint8_t ccNumber;       // 0-127 (MSB CC number)
    uint32_t paramId;       // Target VST3 parameter ID
    bool is14Bit;           // If true, ccNumber+32 is LSB
    bool isPerPreset;       // false = global, true = per-preset
};
```

| Field | Type | Range | Default | Notes |
|-------|------|-------|---------|-------|
| ccNumber | uint8_t | 0-127 | N/A | MIDI CC number (MSB for 14-bit) |
| paramId | uint32_t | valid ParamID | N/A | Target parameter |
| is14Bit | bool | true/false | false | Auto-detected for CC 0-31 |
| isPerPreset | bool | true/false | false | User opts in via context menu |

**Relationships**:
- One CC number maps to at most one parameter (FR-036: most recent wins)
- One parameter can be mapped to at most one CC number
- Global mappings persist across presets (stored in controller state)
- Per-preset mappings override global for the same parameter (stored in component state)

**State transitions for MIDI Learn**:
```
Idle --[right-click "MIDI Learn"]--> Learning (for paramId)
Learning --[CC received]--> Mapped (ccNumber -> paramId)
Learning --[right-click again / Escape]--> Idle (cancelled)
Mapped --[right-click "Clear MIDI Learn"]--> Idle
Mapped --[right-click "Save with Preset"]--> Mapped (isPerPreset = true)
```

**14-bit CC rules**:
- CC 0-31: MSB, paired with CC 32-63 as LSB automatically
- CC 32-63: cannot be directly mapped (reserved as LSB)
- CC 64-127: 7-bit only, no pairing
- Combined value: `(MSB << 7) | LSB` = 0-16383

---

### 5. MIDI CC Manager State (New)

```cpp
class MidiCCManager {
    std::unordered_map<uint8_t, MidiCCMapping> globalMappings_;   // CC -> mapping
    std::unordered_map<uint8_t, MidiCCMapping> presetMappings_;   // CC -> mapping (per-preset)
    std::unordered_map<uint32_t, uint8_t> paramToCC_;             // ParamID -> CC (reverse lookup)

    // MIDI Learn state
    bool learnModeActive_ = false;
    uint32_t learnTargetParamId_ = 0;

    // 14-bit state
    uint8_t lastMSB_[128] = {};  // Last received MSB value per CC
};
```

**Serialization format (controller state - global mappings)**:
```
uint32_t mappingCount;
for each mapping:
    uint8_t ccNumber;
    uint32_t paramId;
    uint8_t flags;  // bit 0: is14Bit, bit 1: isPerPreset
```

**Serialization format (component state - per-preset mappings)**:
Same format, appended after existing component state data.

---

### 6. Accessibility Preferences (New - Read-Only)

```cpp
struct AccessibilityPreferences {
    bool highContrastEnabled = false;
    bool reducedMotionPreferred = false;

    // High contrast color palette (if applicable)
    struct HighContrastColors {
        uint32_t foreground = 0xFFFFFFFF;   // Text color
        uint32_t background = 0xFF000000;   // Background color
        uint32_t accent = 0xFF00FF00;       // Accent/highlight color
        uint32_t border = 0xFFFFFFFF;       // Border color
    } colors;
};
```

| Field | Type | Source | Default |
|-------|------|--------|---------|
| highContrastEnabled | bool | OS query | false |
| reducedMotionPreferred | bool | OS query | false |
| colors.foreground | uint32_t (ARGB) | OS high-contrast palette | 0xFFFFFFFF |
| colors.background | uint32_t (ARGB) | OS high-contrast palette | 0xFF000000 |
| colors.accent | uint32_t (ARGB) | OS high-contrast palette | 0xFF00FF00 |
| colors.border | uint32_t (ARGB) | OS high-contrast palette | 0xFFFFFFFF |

**Not persisted** - queried from OS on each editor open.

---

### 7. Keyboard Focus State (Transient)

| Field | Type | Range | Default | Notes |
|-------|------|-------|---------|-------|
| focusedBandIndex | int | -1 to 7 | -1 (no focus) | Which band strip has focus |
| focusedControl | CView* | valid view or nullptr | nullptr | Which control has focus |

**Not persisted** - transient UI state managed by `CFrame::setFocusView()` and `CFrame::getFocusView()`.

**Focus drawing**:
- Enabled via `CFrame::setFocusDrawingEnabled(true)`
- Color: 2px accent color outline (FR-010a)
- Width: `CFrame::setFocusWidth(2.0)`

---

## Parameter ID Additions

### New Global Parameters

| Parameter | ID Encoding | Type | Steps | Default |
|-----------|-------------|------|-------|---------|
| Modulation Panel Visible | `kGlobalModPanelVisible = 0x06` | Boolean | 1 | 0.0 (hidden) |
| MIDI Learn Active | `kGlobalMidiLearnActive = 0x07` | Boolean | 1 | 0.0 (off) |
| MIDI Learn Target | `kGlobalMidiLearnTarget = 0x08` | Integer | 0 (continuous) | 0.0 |

### Existing Parameters (No Change Needed)

| Parameter | ID | Status |
|-----------|-----|--------|
| Band*Expanded | `makeBandParamId(b, kBandExpanded)` | Already registered |
| Sweep MIDI Learn Active | `kSweepMidiLearnActive` | Keep for backwards compat |
| Sweep MIDI CC Number | `kSweepMidiCCNumber` | Keep for backwards compat |

### Removed/Deprecated Parameters

None. All existing parameters remain for backwards compatibility.

---

## State Version Changes

| Version | Added Fields |
|---------|-------------|
| 6 (current) | Existing state format |
| 7 (this feature) | Controller state: window size (double w, double h), global MIDI CC mappings (count + array), modulation panel visible (bool). Component state: per-preset MIDI CC mappings (count + array). |

**Backwards compatibility**: Version 6 states load without new fields; defaults are used. Version 7 states include the additional data after the existing fields.
