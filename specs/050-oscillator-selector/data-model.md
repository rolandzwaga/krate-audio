# Data Model: OscillatorTypeSelector

**Feature**: 050-oscillator-selector | **Date**: 2026-02-11

## Entities

### OscType (Existing -- Reuse)

**Location**: `dsp/include/krate/dsp/systems/ruinae_types.h`
**Namespace**: `Krate::DSP`

```cpp
enum class OscType : uint8_t {
    PolyBLEP = 0,        // Band-limited subtractive
    Wavetable,            // Mipmapped wavetable
    PhaseDistortion,      // Phase distortion synthesis
    Sync,                 // Hard-sync dual oscillator
    Additive,             // Additive harmonic synthesis
    Chaos,                // Chaos attractor oscillator
    Particle,             // Particle swarm oscillator
    Formant,              // Formant/vocal synthesis
    SpectralFreeze,       // Spectral freeze oscillator
    Noise,                // Multi-color noise
    NumTypes              // Sentinel: total number of types (= 10)
};
```

**Validation Rules**:
- Valid range: 0 to 9 (NumTypes - 1)
- No gaps in enum values (contiguous from 0)
- `uint8_t` backing type ensures compact storage

---

### OscillatorTypeSelector (New)

**Location**: `plugins/shared/src/ui/oscillator_type_selector.h`
**Namespace**: `Krate::Plugins`
**Base Class**: `VSTGUI::CControl`
**Interfaces**: `VSTGUI::IMouseObserver`, `VSTGUI::IKeyboardHook`

#### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `identityColor_` | `CColor` | `{100,180,255,255}` (blue) | Identity color for selected state highlighting |
| `identityId_` | `std::string` | `"a"` | Identity string: `"a"` (blue) or `"b"` (orange) |
| `popupOpen_` | `bool` | `false` | Whether the popup tile grid is currently visible |
| `popupView_` | `CViewContainer*` | `nullptr` | Raw pointer to the popup overlay (owned by CFrame while open) |
| `hoveredCell_` | `int` | `-1` | Index of hovered cell in popup (-1 = none) |
| `focusedCell_` | `int` | `-1` | Index of keyboard-focused cell in popup (-1 = none) |
| `isHovered_` | `bool` | `false` | Whether the collapsed control is hovered (for border highlight) |
| `sOpenInstance_` | `static OscillatorTypeSelector*` | `nullptr` | Singleton tracker for multi-instance popup exclusion |

#### Derived State (Computed)

| Property | Derivation |
|----------|------------|
| `currentIndex()` | `static_cast<int>(std::round(sanitizedValue * 9))` where `sanitizedValue` = clamped, NaN-safe normalized value |
| `currentType()` | `static_cast<OscType>(currentIndex())` |
| `displayName()` | Lookup table indexed by `currentIndex()` |
| `popupLabel()` | Abbreviated label lookup table indexed by `currentIndex()` |

---

### Value Mapping

#### Normalized Value <-> OscType Index

| OscType | Index | Normalized Value (`index / 9.0`) |
|---------|-------|----------------------------------|
| PolyBLEP | 0 | 0.000 |
| Wavetable | 1 | 0.111 |
| PhaseDistortion | 2 | 0.222 |
| Sync | 3 | 0.333 |
| Additive | 4 | 0.444 |
| Chaos | 5 | 0.556 |
| Particle | 6 | 0.667 |
| Formant | 7 | 0.778 |
| SpectralFreeze | 8 | 0.889 |
| Noise | 9 | 1.000 |

#### Conversion Functions

```cpp
// Normalized value (0.0 - 1.0) -> integer index (0-9)
// Handles NaN, infinity, and out-of-range values defensively
inline int oscTypeIndexFromNormalized(float value) {
    // FR-042: NaN/inf -> 0.5
    if (std::isnan(value) || std::isinf(value))
        value = 0.5f;
    // Clamp to valid range
    value = std::clamp(value, 0.0f, 1.0f);
    // Round to nearest integer index
    return static_cast<int>(std::round(value * 9.0f));
}

// Integer index (0-9) -> normalized value (0.0 - 1.0)
inline float normalizedFromOscTypeIndex(int index) {
    return static_cast<float>(std::clamp(index, 0, 9)) / 9.0f;
}
```

---

### Display Name Tables

```cpp
// Full display names (collapsed state + tooltip)
constexpr std::array<const char*, 10> kOscTypeDisplayNames = {
    "PolyBLEP",
    "Wavetable",
    "Phase Distortion",
    "Sync",
    "Additive",
    "Chaos",
    "Particle",
    "Formant",
    "Spectral Freeze",
    "Noise"
};

// Abbreviated labels (popup cell)
constexpr std::array<const char*, 10> kOscTypePopupLabels = {
    "BLEP",
    "WTbl",
    "PDst",
    "Sync",
    "Add",
    "Chaos",
    "Prtcl",
    "Fmnt",
    "SFrz",
    "Noise"
};
```

---

### Identity Color Mapping

| Identity | String | Color RGB | Usage |
|----------|--------|-----------|-------|
| OSC A | `"a"` | `rgb(100,180,255)` | Blue -- selected cell border, icon stroke, label |
| OSC B | `"b"` | `rgb(255,140,100)` | Orange -- selected cell border, icon stroke, label |

---

## State Transitions

### Control State Machine

```
                     +------------------+
                     |   COLLAPSED      |
                     | (default state)  |
                     +------------------+
                      |       |       ^
            click     |  scroll|      | click cell / click outside /
                      v       v      | Escape / toggle click
                     +------------------+
                     |   POPUP OPEN     |
                     +------------------+
                      |       |
              scroll  |  arrow|
              (stays  | keys  |
               open)  v       v
                     +------------------+
                     |   POPUP OPEN     |
                     | (selection/focus |
                     |   updated)       |
                     +------------------+
```

### Transitions Detail

| From | Event | To | Action |
|------|-------|----|--------|
| COLLAPSED | Click | POPUP_OPEN | Open popup, register hooks, set `sOpenInstance_` |
| COLLAPSED | Scroll | COLLAPSED | Change selection +/- 1 with wrap, beginEdit/performEdit/endEdit |
| COLLAPSED | Host automation | COLLAPSED | Update display (icon + name), invalidate() |
| POPUP_OPEN | Click cell | COLLAPSED | Select type, beginEdit/performEdit/endEdit, close popup |
| POPUP_OPEN | Click outside | COLLAPSED | Close popup, no selection change |
| POPUP_OPEN | Escape | COLLAPSED | Close popup, no selection change |
| POPUP_OPEN | Click collapsed (toggle) | COLLAPSED | Close popup, no selection change |
| POPUP_OPEN | Scroll | POPUP_OPEN | Change selection +/- 1 with wrap, update highlight |
| POPUP_OPEN | Arrow keys | POPUP_OPEN | Move focus indicator |
| POPUP_OPEN | Enter/Space | COLLAPSED | Select focused cell, close popup |
| POPUP_OPEN | Other instance clicked | COLLAPSED | Close this popup (via `sOpenInstance_` check), other opens |

---

## Geometry Model

### Collapsed Control (180 x 28 px)

```
+--8px--+--20x14--+--6px--+---------name---------+--8px--+--8x5--+--8px--+
|  pad  |  icon   | gap   |  display name (11px)  |  pad  | arrow | pad   |
+-------+---------+-------+-----------------------+-------+-------+-------+
         ^                                                   ^
     identity color                                    text color
```

### Popup Grid (260 x 94 px)

```
+--6px--+--48x40--+--2px--+--48x40--+--2px--+--48x40--+--2px--+--48x40--+--2px--+--48x40--+--6px--+
|  pad  | cell 0  |  gap  | cell 1  |  gap  | cell 2  |  gap  | cell 3  |  gap  | cell 4  |  pad  |
+-------+---------+-------+---------+-------+---------+-------+---------+-------+---------+-------+
|  pad  | cell 5  |  gap  | cell 6  |  gap  | cell 7  |  gap  | cell 8  |  gap  | cell 9  |  pad  |
+-------+---------+-------+---------+-------+---------+-------+---------+-------+---------+-------+
```

Each cell (48 x 40 px):
```
+----------48----------+
|   waveform icon       |  26px
|   (48 x 26)          |
+-----------------------+
|   label (9px, center) |  12px (but within 14px remaining)
+-----------------------+
```

### Grid Arithmetic (Hit Testing)

```cpp
// FR-026: Grid arithmetic for hit testing
int hitTestPopupCell(CCoord localX, CCoord localY) const {
    constexpr CCoord kPadding = 6.0;
    constexpr CCoord kCellW = 48.0;
    constexpr CCoord kCellH = 40.0;
    constexpr CCoord kGap = 2.0;

    CCoord gridX = localX - kPadding;
    CCoord gridY = localY - kPadding;

    if (gridX < 0 || gridY < 0) return -1;

    int col = static_cast<int>(gridX / (kCellW + kGap));
    int row = static_cast<int>(gridY / (kCellH + kGap));

    if (col < 0 || col >= 5 || row < 0 || row >= 2) return -1;

    // Check we're inside the cell, not in the gap
    CCoord cellLocalX = gridX - col * (kCellW + kGap);
    CCoord cellLocalY = gridY - row * (kCellH + kGap);
    if (cellLocalX > kCellW || cellLocalY > kCellH) return -1;

    return row * 5 + col;
}
```

---

## Waveform Icon Specifications (FR-008, FR-040)

Each icon is 5-10 normalized points in [0,1] x [0,1] space, scaled to target rect at draw time. All drawn with 1.5px anti-aliased stroke, no fill (FR-005).

| Index | Type | Point Count | Description |
|-------|------|-------------|-------------|
| 0 | PolyBLEP | 6 | Sawtooth: rise from bottom-left to top-right, then vertical drop, repeat |
| 1 | Wavetable | ~8 | 3 overlapping sine-like waves offset vertically |
| 2 | PhaseDistortion | 6 | Bent sine: starts gentle, sharp peak, asymmetric |
| 3 | Sync | 8 | Truncated burst: partial saw cycles getting shorter |
| 4 | Additive | 10 | 5 vertical bars descending in height (spectrum) |
| 5 | Chaos | 8 | Looping squiggle (Lorenz-like attractor) |
| 6 | Particle | 8 | Scattered dots + arc envelope curve |
| 7 | Formant | 7 | 2-3 resonant humps (vocal formant peaks) |
| 8 | SpectralFreeze | 10 | Vertical bars of varying height (frozen spectrum) |
| 9 | Noise | 8 | Jagged random-looking horizontal line |
