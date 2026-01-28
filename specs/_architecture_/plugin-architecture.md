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

---

## Disrumpo Plugin Components

### VST3 Components

| Component | Path | Purpose |
|-----------|------|---------|
| Processor | `plugins/disrumpo/src/processor/` | Audio processing (real-time) |
| Controller | `plugins/disrumpo/src/controller/` | UI state management |
| Entry | `plugins/disrumpo/src/entry.cpp` | Factory registration |
| IDs | `plugins/disrumpo/src/plugin_ids.h` | Parameter and component IDs |

### DSP Components (Spec 002-band-management)

| Component | Path | Purpose |
|-----------|------|---------|
| CrossoverNetwork | `plugins/disrumpo/src/dsp/crossover_network.h` | N-band phase-coherent crossover (1-8 bands) |
| BandProcessor | `plugins/disrumpo/src/dsp/band_processor.h` | Per-band gain/pan/mute with smoothing |
| BandState | `plugins/disrumpo/src/dsp/band_state.h` | Per-band configuration structure |

### CrossoverNetwork
**Path:** [crossover_network.h](../../plugins/disrumpo/src/dsp/crossover_network.h) | **Since:** 0.1.0

Phase-coherent multiband crossover using cascaded LR4 filters with D'Appolito allpass compensation.

```cpp
class CrossoverNetwork {
    void prepare(double sampleRate, int numBands);   // Initialize (1-8 bands)
    void reset();                                     // Clear filter states
    void setBandCount(int numBands);                  // Dynamic band change
    void setCrossoverFrequency(int index, float hz); // Set crossover point
    float getCrossoverFrequency(int index) const;    // Get crossover point
    void process(float input, std::array<float, 8>& bands); // Split to bands
    int getBandCount() const;
    bool isPrepared() const;
};
```

**Features:**
- SC-001 compliant: +/-0.1dB flat frequency response when bands summed
- D'Appolito allpass compensation for phase coherence
- Logarithmic default frequency distribution (FR-009)
- Band count change preserves existing crossovers (FR-011a/b)
- Real-time safe: fixed-size arrays, no allocations

### BandProcessor
**Path:** [band_processor.h](../../plugins/disrumpo/src/dsp/band_processor.h) | **Since:** 0.1.0

Per-band gain, pan, and mute processing with click-free smoothing.

```cpp
class BandProcessor {
    void prepare(double sampleRate);           // Initialize smoothers
    void reset();                               // Clear smoother states
    void setGainDb(float db);                  // Set gain [-24, +24] dB
    void setPan(float pan);                    // Set pan [-1, +1]
    void setMute(bool muted);                  // Set mute state
    void process(float& left, float& right);  // Apply processing in-place
    bool isSmoothing() const;
};
```

**Features:**
- Equal-power pan law: `left = cos(pan * PI/4 + PI/4) * gain`
- 10ms default smoothing for click-free transitions (FR-027a)
- Mute uses smooth fade to prevent clicks (SC-005)

### Parameter ID Encoding (Disrumpo)

Band-level parameters use bit-encoded IDs:

```cpp
// Encoding: (0xF << 12) | (band << 8) | param
// Band index: 0-7, Param type: BandParamType enum

// Examples:
// Band 0 Gain  = 0xF000
// Band 0 Pan   = 0xF001
// Band 1 Mute  = 0xF104
// Band 7 Solo  = 0xF702

constexpr ParamID makeBandParamId(uint8_t band, BandParamType param);
constexpr ParamID makeCrossoverParamId(uint8_t index);  // 0x0F10 + index
```
