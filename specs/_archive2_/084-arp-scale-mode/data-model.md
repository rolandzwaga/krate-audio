# Data Model: Arpeggiator Scale Mode

**Feature**: 084-arp-scale-mode
**Date**: 2026-02-28

## E-001: ScaleData Struct (Layer 0 - Core)

**Location**: `dsp/include/krate/dsp/core/scale_harmonizer.h`

```cpp
/// Fixed-size scale interval data for variable-length scales.
/// Supports 5-note (pentatonic) through 12-note (chromatic) scales.
/// Zero-padded beyond degreeCount for unused slots.
struct ScaleData {
    std::array<int, 12> intervals{};  ///< Semitone offsets from root (e.g., {0,2,4,5,7,9,11} for Major)
    int degreeCount{0};               ///< Number of active degrees (5, 6, 7, 8, or 12)
};
```

**Validation rules**:
- `degreeCount` must be in range [5, 12]
- `intervals[0]` must always be 0 (root)
- `intervals[i]` must be strictly increasing for i < degreeCount
- `intervals[i]` must be in range [0, 11] for i < degreeCount
- `intervals[i]` must be 0 for i >= degreeCount (zero padding)

**State transitions**: N/A -- immutable constexpr data.

## E-002: Extended ScaleType Enum (Layer 0 - Core)

**Location**: `dsp/include/krate/dsp/core/scale_harmonizer.h`

```cpp
enum class ScaleType : uint8_t {
    // Existing values (stable, do not reorder)
    Major = 0,
    NaturalMinor = 1,
    HarmonicMinor = 2,
    MelodicMinor = 3,
    Dorian = 4,
    Mixolydian = 5,
    Phrygian = 6,
    Lydian = 7,
    Chromatic = 8,
    // New values (appended)
    Locrian = 9,
    MajorPentatonic = 10,
    MinorPentatonic = 11,
    Blues = 12,
    WholeTone = 13,
    DiminishedWH = 14,
    DiminishedHW = 15,
};

inline constexpr int kNumNonChromaticScales = 15;
inline constexpr int kNumScaleTypes = 16;
```

**Relationships**: Used by both `ScaleHarmonizer` (Layer 0) and `ArpeggiatorCore` (Layer 2).

## E-003: kScaleIntervals Table (Layer 0 - Core)

**Location**: `dsp/include/krate/dsp/core/scale_harmonizer.h`, namespace `detail`

```cpp
inline constexpr std::array<ScaleData, 16> kScaleIntervals = {{
    // Major (0):           7 degrees
    {{0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0}, 7},
    // NaturalMinor (1):    7 degrees
    {{0, 2, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}, 7},
    // HarmonicMinor (2):   7 degrees
    {{0, 2, 3, 5, 7, 8, 11, 0, 0, 0, 0, 0}, 7},
    // MelodicMinor (3):    7 degrees
    {{0, 2, 3, 5, 7, 9, 11, 0, 0, 0, 0, 0}, 7},
    // Dorian (4):          7 degrees
    {{0, 2, 3, 5, 7, 9, 10, 0, 0, 0, 0, 0}, 7},
    // Mixolydian (5):      7 degrees
    {{0, 2, 4, 5, 7, 9, 10, 0, 0, 0, 0, 0}, 7},
    // Phrygian (6):        7 degrees
    {{0, 1, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}, 7},
    // Lydian (7):          7 degrees
    {{0, 2, 4, 6, 7, 9, 11, 0, 0, 0, 0, 0}, 7},
    // Chromatic (8):      12 degrees
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 12},
    // Locrian (9):         7 degrees
    {{0, 1, 3, 5, 6, 8, 10, 0, 0, 0, 0, 0}, 7},
    // MajorPentatonic (10): 5 degrees
    {{0, 2, 4, 7, 9, 0, 0, 0, 0, 0, 0, 0}, 5},
    // MinorPentatonic (11): 5 degrees
    {{0, 3, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0}, 5},
    // Blues (12):           6 degrees
    {{0, 3, 5, 6, 7, 10, 0, 0, 0, 0, 0, 0}, 6},
    // WholeTone (13):      6 degrees
    {{0, 2, 4, 6, 8, 10, 0, 0, 0, 0, 0, 0}, 6},
    // DiminishedWH (14):   8 degrees
    {{0, 2, 3, 5, 6, 8, 9, 11, 0, 0, 0, 0}, 8},
    // DiminishedHW (15):   8 degrees
    {{0, 1, 3, 4, 6, 7, 9, 10, 0, 0, 0, 0}, 8},
}};
```

## E-004: ArpeggiatorParams Extension (Plugin Layer)

**Location**: `plugins/ruinae/src/parameters/arpeggiator_params.h`

New fields appended to the `ArpeggiatorParams` struct:

```cpp
// --- Scale Mode (084-arp-scale-mode) ---
std::atomic<int>  scaleType{8};          // ScaleType enum value (0-15, default 8 = Chromatic)
std::atomic<int>  rootNote{0};           // 0=C, 1=C#, ..., 11=B (default 0 = C)
std::atomic<bool> scaleQuantizeInput{false};  // Snap incoming notes to scale (default OFF)
```

## E-005: Parameter IDs (Plugin Layer)

**Location**: `plugins/ruinae/src/plugin_ids.h`

```cpp
// --- Scale Mode (084-arp-scale-mode, 3300-3302) ---
kArpScaleTypeId           = 3300,   // discrete: 0-15 (StringListParameter, 16 entries)
kArpRootNoteId            = 3301,   // discrete: 0-11 (StringListParameter, 12 entries)
kArpScaleQuantizeInputId  = 3302,   // discrete: 0-1 (toggle, default off)
// 3303-3399: reserved

kArpEndId = 3302,
kNumParameters = 3303,
```

## E-006: Arp Scale Type UI Display Order Mapping

The Arp Scale Type dropdown presents scales in a user-facing order different from the enum order.

| UI Index | Display Name | ScaleType Enum Value |
|----------|---|---|
| 0 | Chromatic | 8 |
| 1 | Major | 0 |
| 2 | Natural Minor | 1 |
| 3 | Harmonic Minor | 2 |
| 4 | Melodic Minor | 3 |
| 5 | Dorian | 4 |
| 6 | Phrygian | 6 |
| 7 | Lydian | 7 |
| 8 | Mixolydian | 5 |
| 9 | Locrian | 9 |
| 10 | Major Pentatonic | 10 |
| 11 | Minor Pentatonic | 11 |
| 12 | Blues | 12 |
| 13 | Whole Tone | 13 |
| 14 | Diminished (W-H) | 14 |
| 15 | Diminished (H-W) | 15 |

**Implementation**: Use `StringListParameter` with custom string order. The `handleArpParamChange` function must map the normalized value (UI index) to the correct ScaleType enum value using a lookup table (`kArpScaleDisplayOrder`).

## E-007: Dropdown Mapping Constants

**Location**: `plugins/ruinae/src/parameters/dropdown_mappings.h`

```cpp
// Arp Scale Type: display order for arp dropdown (Chromatic first)
inline constexpr int kArpScaleTypeCount = 16;

// Maps UI dropdown index to ScaleType enum value
inline constexpr std::array<int, 16> kArpScaleDisplayOrder = {
    8,   // 0: Chromatic
    0,   // 1: Major
    1,   // 2: Natural Minor
    2,   // 3: Harmonic Minor
    3,   // 4: Melodic Minor
    4,   // 5: Dorian
    6,   // 6: Phrygian
    7,   // 7: Lydian
    5,   // 8: Mixolydian
    9,   // 9: Locrian
    10,  // 10: Major Pentatonic
    11,  // 11: Minor Pentatonic
    12,  // 12: Blues
    13,  // 13: Whole Tone
    14,  // 14: Diminished (W-H)
    15,  // 15: Diminished (H-W)
};

// Maps ScaleType enum value to UI dropdown index (inverse of above)
inline constexpr std::array<int, 16> kArpScaleEnumToDisplay = {
    1,   // Major(0) -> UI 1
    2,   // NaturalMinor(1) -> UI 2
    3,   // HarmonicMinor(2) -> UI 3
    4,   // MelodicMinor(3) -> UI 4
    5,   // Dorian(4) -> UI 5
    8,   // Mixolydian(5) -> UI 8
    6,   // Phrygian(6) -> UI 6
    7,   // Lydian(7) -> UI 7
    0,   // Chromatic(8) -> UI 0
    9,   // Locrian(9) -> UI 9
    10,  // MajorPentatonic(10) -> UI 10
    11,  // MinorPentatonic(11) -> UI 11
    12,  // Blues(12) -> UI 12
    13,  // WholeTone(13) -> UI 13
    14,  // DiminishedWH(14) -> UI 14
    15,  // DiminishedHW(15) -> UI 15
};

// Arp Root Note
inline constexpr int kArpRootNoteCount = 12;
```

## E-008: State Serialization (Appended Fields)

Save order for new fields (appended after ratchetSwing):

```
writeInt32(scaleType)          // ScaleType enum value (0-15)
writeInt32(rootNote)           // 0-11
writeInt32(scaleQuantizeInput) // 0 or 1
```

Load: If `readInt32` fails for any of these, keep defaults (8, 0, false) -- backward compatible with old presets.
