# API Contract: ArpeggiatorCore Scale Mode Extension

**File**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`
**Layer**: 2 (Processors)

## New Setters

```cpp
/// Set the scale type for pitch lane interpretation.
/// When non-Chromatic, pitch lane values are interpreted as scale degree offsets.
/// When Chromatic, pitch lane values remain as semitone offsets (backward compatible).
void setScaleType(ScaleType type) noexcept;

/// Set the root note for the scale (0=C through 11=B).
void setRootNote(int rootNote) noexcept;

/// Enable/disable input note quantization.
/// When enabled and scale is non-Chromatic, incoming noteOn pitches are
/// snapped to the nearest scale note before entering the note pool.
void setScaleQuantizeInput(bool enabled) noexcept;
```

## New Member

```cpp
private:
    ScaleHarmonizer scaleHarmonizer_;       ///< Scale calculator for pitch lane and input quantization
    bool scaleQuantizeInput_ = false;       ///< Whether to snap incoming notes to scale
```

## Modified Methods

### noteOn (input quantization)

```cpp
inline void noteOn(uint8_t note, uint8_t velocity) noexcept {
    // ... existing latch logic ...

    // Scale quantize input (FR-009): snap note to nearest scale note
    // before entering the held notes buffer.
    uint8_t effectiveNote = note;
    if (scaleQuantizeInput_ && scaleHarmonizer_.getScale() != ScaleType::Chromatic) {
        effectiveNote = static_cast<uint8_t>(
            std::clamp(scaleHarmonizer_.quantizeToScale(static_cast<int>(note)), 0, 127));
    }

    heldNotes_.noteOn(effectiveNote, velocity);
    // ... rest of existing logic ...
}
```

### fireStep (pitch offset conversion)

```cpp
// Current (lines 1567-1573):
for (size_t i = 0; i < result.count; ++i) {
    int offsetNote = static_cast<int>(result.notes[i]) +
                     static_cast<int>(pitchOffset);
    result.notes[i] = static_cast<uint8_t>(std::clamp(offsetNote, 0, 127));
}

// New:
for (size_t i = 0; i < result.count; ++i) {
    if (scaleHarmonizer_.getScale() != ScaleType::Chromatic && pitchOffset != 0) {
        auto interval = scaleHarmonizer_.calculate(
            static_cast<int>(result.notes[i]),
            static_cast<int>(pitchOffset));
        result.notes[i] = static_cast<uint8_t>(
            std::clamp(interval.targetNote, 0, 127));
    } else {
        int offsetNote = static_cast<int>(result.notes[i]) +
                         static_cast<int>(pitchOffset);
        result.notes[i] = static_cast<uint8_t>(std::clamp(offsetNote, 0, 127));
    }
}
```

## Behavioral Contract

- When ScaleType is Chromatic: ALL behavior is identical to current (FR-004).
- When ScaleType is non-Chromatic: pitch lane offset N means "move N scale degrees" (FR-005).
- Octave wrapping is handled by ScaleHarmonizer::calculate() (FR-006).
- MIDI note clamping to 0-127 is preserved (FR-008).
- Input quantization only active when scaleQuantizeInput_ is true AND scale is non-Chromatic (FR-009, FR-010).
