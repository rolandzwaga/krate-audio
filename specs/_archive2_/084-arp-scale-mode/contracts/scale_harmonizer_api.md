# API Contract: ScaleHarmonizer (Refactored)

**File**: `dsp/include/krate/dsp/core/scale_harmonizer.h`
**Layer**: 0 (Core)

## ScaleData Struct

```cpp
struct ScaleData {
    std::array<int, 12> intervals{};  // Semitone offsets, zero-padded
    int degreeCount{0};               // Active degree count (5-12)
};
```

## ScaleType Enum (Extended)

```cpp
enum class ScaleType : uint8_t {
    Major = 0, NaturalMinor = 1, HarmonicMinor = 2, MelodicMinor = 3,
    Dorian = 4, Mixolydian = 5, Phrygian = 6, Lydian = 7,
    Chromatic = 8,
    Locrian = 9, MajorPentatonic = 10, MinorPentatonic = 11,
    Blues = 12, WholeTone = 13, DiminishedWH = 14, DiminishedHW = 15,
};
inline constexpr int kNumScaleTypes = 16;
```

## Constants (Updated)

```cpp
inline constexpr int kNumNonChromaticScales = 15;
inline constexpr int kNumScaleTypes = 16;
// kDegreesPerScale REMOVED -- degree count is now per-scale via ScaleData::degreeCount
```

## kScaleIntervals Table (Replaced)

```cpp
namespace detail {
inline constexpr std::array<ScaleData, 16> kScaleIntervals = {{ ... }};
// Indexed by static_cast<int>(ScaleType). Each entry has intervals and degreeCount.
}
```

## kReverseLookup Table (Extended)

```cpp
namespace detail {
inline constexpr std::array<std::array<int, 12>, 16> kReverseLookup = {{ ... }};
// Now 16 entries (one per scale type).
}
```

## buildReverseLookup (Generalized)

```cpp
// Old signature:
constexpr std::array<int, 12> buildReverseLookup(int scaleIndex) noexcept;
// New: uses kScaleIntervals[scaleIndex].degreeCount instead of hardcoded 7
```

## ScaleHarmonizer::calculate (Updated)

```cpp
[[nodiscard]] DiatonicInterval calculate(int inputMidiNote, int diatonicSteps) const noexcept;
// Changed: Uses scaleData.degreeCount for octave wrapping instead of hardcoded 7.
// The / 7 and % 7 become / degreeCount and % degreeCount.
```

## ScaleHarmonizer::getScaleDegree (Updated)

```cpp
[[nodiscard]] int getScaleDegree(int midiNote) const noexcept;
// Changed: Iterates degreeCount instead of hardcoded 7.
```

## ScaleHarmonizer::quantizeToScale (Unchanged)

```cpp
[[nodiscard]] int quantizeToScale(int midiNote) const noexcept;
// Already works correctly for variable-degree scales after kScaleIntervals/kReverseLookup refactoring.
```

## ScaleHarmonizer::getScaleIntervals (Updated)

```cpp
// Old: [[nodiscard]] static constexpr std::array<int, 7> getScaleIntervals(ScaleType type) noexcept;
// New:
[[nodiscard]] static constexpr ScaleData getScaleIntervals(ScaleType type) noexcept;
```

## Backward Compatibility

- All existing method signatures remain compatible (same names, same parameter types)
- `getScaleIntervals()` return type changes from `std::array<int, 7>` to `ScaleData` -- callers must update
- `DiatonicInterval.scaleDegree` range changes from 0-6 to 0-(degreeCount-1)
- `kDegreesPerScale` constant removed (was 7, now varies per scale)
