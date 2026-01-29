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

### UI Components (Spec 006-morph-ui)

| Component | Path | Purpose |
|-----------|------|---------|
| MorphPad | `plugins/disrumpo/src/controller/views/morph_pad.h` | 2D XY pad for morph position control |
| SpectrumDisplay | `plugins/disrumpo/src/controller/views/spectrum_display.h` | Crossover frequency visualization |
| VisibilityController | `plugins/disrumpo/src/controller/controller.cpp` | IDependent-based visibility toggle |
| ContainerVisibilityController | `plugins/disrumpo/src/controller/controller.cpp` | Show/hide containers by parameter |
| MorphSweepLinkController | `plugins/disrumpo/src/controller/controller.cpp` | Update morph from sweep frequency |

### MorphPad (Spec 006)
**Path:** [morph_pad.h](../../plugins/disrumpo/src/controller/views/morph_pad.h) | **Since:** 0.2.0

Custom VSTGUI control for 2D morph position control with node visualization. Extends `VSTGUI::CControl`.

```cpp
class MorphPad : public VSTGUI::CControl {
    void setActiveNodeCount(int count);         // 2, 3, or 4 nodes
    void setMorphMode(MorphMode mode);          // Linear1D, Planar2D, Radial2D
    void setMorphPosition(float x, float y);    // [0, 1] cursor position
    void setNodePosition(int idx, float x, float y);  // Custom node positions
    void setNodeType(int idx, DistortionType type);   // For color rendering
    void setNodeWeight(int idx, float weight);  // For connection line opacity
    void setSelectedNode(int idx);              // Node selection (-1 = none)
    void setMorphPadListener(MorphPadListener* listener);
};
```

**Features:**
- Node rendering: 12px filled circles with category-specific colors
- Cursor rendering: 16px open circle, 2px white stroke
- Connection lines: Cursor to nodes, opacity proportional to weight
- Interaction: Click, drag, Shift+drag (10x fine), Alt+drag (node move), double-click (reset)
- Scroll wheel: Vertical scroll adjusts X, horizontal scroll adjusts Y
- Mode visualization: 1D Linear (horizontal constraint), 2D Planar (default), 2D Radial (grid overlay)
- Position label: "X: 0.00 Y: 0.00" at bottom-left

**Category Colors:**
- Saturation: Orange (#FF6B35)
- Wavefold: Teal (#4ECDC4)
- Digital: Green (#95E86B)
- Rectify: Purple (#C792EA)
- Dynamic: Yellow (#FFCB6B)
- Hybrid: Red (#FF5370)
- Experimental: Light Blue (#89DDFF)

### MorphSweepLinkController
**Path:** Controller implementation in `controller.cpp` | **Since:** 0.2.0

IDependent-based controller that updates morph position when sweep frequency changes.

```cpp
class MorphSweepLinkController : public Steinberg::FObject {
    // Listens to sweep frequency parameter changes
    // For each band, applies link mode to compute morph position
    // Updates Band*MorphX/Y parameters
};
```

**Link Modes (MorphLinkMode enum):**
- None: Manual control only
- SweepFreq: Linear (low freq = 0, high freq = 1)
- InverseSweep: Inverted (high freq = 0, low freq = 1)
- EaseIn: sqrt(x) - more range in bass
- EaseOut: x^2 - more range in highs
- HoldRise: Hold at 0 until midpoint, then rise to 1
- Stepped: Quantized to 5 steps (0, 0.25, 0.5, 0.75, 1.0)

### IDependent Pattern for Visibility Controllers
Thread-safe UI visibility updates using VST3 parameter observation.

```cpp
class VisibilityController : public Steinberg::FObject {
    // In constructor:
    watchedParam_->addDependent(this);
    watchedParam_->deferUpdate();

    // In update():
    if (message == IDependent::kChanged) {
        // Update UI visibility (on UI thread)
    }

    // In deactivate() - CRITICAL before editor closes:
    watchedParam_->removeDependent(this);
};
```

**Usage:**
1. Create in `Controller::didOpen()`
2. Call `deactivate()` in `Controller::willClose()` BEFORE releasing
3. Store in `Steinberg::IPtr<>` for automatic reference counting

---

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
