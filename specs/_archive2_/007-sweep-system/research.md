# Research: Sweep System

**Feature**: 007-sweep-system
**Date**: 2026-01-29
**Status**: Complete

---

## Research Tasks

### 1. Gaussian Intensity Distribution Implementation

**Question**: What is the optimal implementation for Gaussian intensity calculation in real-time audio?

**Decision**: Use direct exp() calculation with octave-space distance

**Rationale**:
- Formula: `intensity = intensityParam * exp(-0.5 * (distanceOctaves / sigma)^2)`
- Direct calculation is fast enough for per-band computation (8 bands max)
- Lookup table not needed at this scale
- `std::exp()` is well-optimized on modern CPUs

**Alternatives Considered**:
- Lookup table with interpolation: Rejected (complexity not justified for 8 bands)
- Polynomial approximation: Rejected (accuracy concerns at tails)

**Implementation**:
```cpp
float calculateGaussianIntensity(float bandFreqHz, float sweepCenterHz,
                                  float widthOctaves, float intensityParam) {
    float distanceOctaves = std::abs(std::log2(bandFreqHz) - std::log2(sweepCenterHz));
    float sigma = widthOctaves / 2.0f;
    float exponent = -0.5f * (distanceOctaves / sigma) * (distanceOctaves / sigma);
    return intensityParam * std::exp(exponent);
}
```

---

### 2. Lock-Free Buffer for Audio-UI Sync

**Question**: What pattern should be used for audio-thread to UI-thread communication?

**Decision**: Single-Producer Single-Consumer (SPSC) ring buffer with atomic indices

**Rationale**:
- Audio thread is single producer, UI thread is single consumer
- Steinberg SDK provides `OneReaderOneWriter::RingBuffer` template
- Lock-free guarantees real-time safety
- 8-entry buffer provides ~100ms of data at typical block sizes

**Alternatives Considered**:
- `std::mutex` protected queue: Rejected (priority inversion risk on audio thread)
- `std::atomic<SweepPositionData>`: Rejected (struct too large for atomic)
- Double buffer: Acceptable but ring buffer is more flexible

**Implementation Reference**:
```cpp
// From extern/vst3sdk/public.sdk/source/vst/utility/ringbuffer.h
template <typename ItemT>
class RingBuffer {
    bool push(const ItemT& item) noexcept;  // Producer
    bool pop(ItemT& item) noexcept;          // Consumer
};
```

---

### 3. Parameter Smoothing Strategy

**Question**: What smoothing approach prevents zipper noise on sweep frequency automation?

**Decision**: OnePoleSmoother with 10-50ms time constant (user configurable)

**Rationale**:
- OnePoleSmoother already exists in `dsp/include/krate/dsp/primitives/smoother.h`
- 10ms provides responsive feel, 50ms provides smoother transitions
- Exponential smoothing naturally handles rapid parameter changes
- Default: 20ms (balance between responsiveness and smoothness)

**API Usage**:
```cpp
Krate::DSP::OnePoleSmoother frequencySmoother_;

// In prepare():
frequencySmoother_.configure(20.0f, sampleRate);  // 20ms smoothing

// When parameter changes:
frequencySmoother_.setTarget(newFrequency);

// In process():
float smoothedFreq = frequencySmoother_.process();
```

**Alternatives Considered**:
- LinearRamp: Better for delay time, but exponential is more natural for frequency
- SlewLimiter: Could work, but OnePoleSmoother is simpler

---

### 4. Sweep-Morph Linking Curves

**Question**: How should the 8 morph link curves be implemented?

**Decision**: Pure functions in switch statement, add Custom mode to enum

**Rationale**:
- MorphLinkMode enum already exists in plugin_ids.h with 7 modes
- Need to add Custom (mode 7) to enum
- Pure functions are testable and real-time safe
- Custom mode requires breakpoint interpolation class

**Curve Formulas** (from dsp-details.md Section 8):

| Mode | Formula | Musical Intent |
|------|---------|----------------|
| None | Return 0.5 (center) | Independent control |
| Linear (SweepFreq) | `y = x` | Baseline, predictable |
| Inverse | `y = 1 - x` | Opposite relationship |
| EaseIn | `y = x^2` | Slow start, fast end |
| EaseOut | `y = 1 - (1-x)^2` | Fast start, gentle landing |
| HoldRise | `y = 0 if x < 0.6, else (x - 0.6) / 0.4` | Flat until 60%, then rise |
| Stepped | `y = floor(x * 4) / 3` | Discrete 4-step quantization |
| Custom | Breakpoint interpolation | User-defined |

**Implementation**:
```cpp
// Add to MorphLinkMode enum in plugin_ids.h:
enum class MorphLinkMode : uint8_t {
    None = 0,
    SweepFreq,
    InverseSweep,
    EaseIn,
    EaseOut,
    HoldRise,
    Stepped,
    Custom,        // NEW - add this
    COUNT          // Update to 8
};
```

---

### 5. Custom Curve Breakpoint Editor

**Question**: How should the custom curve editor store and interpolate breakpoints?

**Decision**: std::vector of CPoint (x,y pairs), linear interpolation between points

**Rationale**:
- 2-8 breakpoints per spec FR-022
- First point must have x=0, last point must have x=1
- Linear interpolation is simple and predictable
- Spline interpolation adds complexity without clear benefit

**Data Structure**:
```cpp
class CustomCurve {
public:
    static constexpr int kMinBreakpoints = 2;
    static constexpr int kMaxBreakpoints = 8;

    // Add/remove/move breakpoints
    bool addBreakpoint(float x, float y);
    bool removeBreakpoint(int index);
    void setBreakpoint(int index, float x, float y);

    // Evaluate curve at normalized position
    float evaluate(float x) const;

    // Serialization
    void saveState(IBStream* stream) const;
    void loadState(IBStream* stream);

private:
    struct Breakpoint { float x; float y; };
    std::vector<Breakpoint> breakpoints_;
};
```

**Alternatives Considered**:
- Bezier curves: More complex UI, unclear musical benefit
- Catmull-Rom splines: Smoother but harder to control precisely

---

### 6. Internal LFO for Sweep Modulation

**Question**: Should sweep LFO reuse existing LFO class or create sweep-specific version?

**Decision**: Wrap existing Krate::DSP::LFO class with sweep-specific parameters

**Rationale**:
- LFO class in `dsp/include/krate/dsp/primitives/lfo.h` is well-tested
- Supports all required shapes (Sine, Triangle, Saw, Square, S&H, SmoothRandom)
- Already has tempo sync support
- Wrapper adds sweep-specific range mapping and depth parameter

**API**:
```cpp
class SweepLFO {
public:
    void prepare(double sampleRate);
    void setRate(float hz);
    void setTempoSync(bool enabled, float bpm, NoteValue value);
    void setWaveform(Krate::DSP::Waveform shape);
    void setDepth(float depth);  // 0-1, how much LFO affects sweep

    // Returns modulation offset in Hz (to add to sweep frequency)
    float process();

private:
    Krate::DSP::LFO lfo_;
    float depth_ = 1.0f;
    float minFreq_ = 20.0f;
    float maxFreq_ = 20000.0f;
};
```

---

### 7. Envelope Follower for Sweep Modulation

**Question**: Should sweep envelope follower reuse existing EnvelopeFollower class?

**Decision**: Wrap existing Krate::DSP::EnvelopeFollower with sweep-specific mapping

**Rationale**:
- EnvelopeFollower exists in `dsp/include/krate/dsp/processors/envelope_follower.h`
- Supports configurable attack/release (FR-027: 1-100ms attack, 10-500ms release)
- Wrapper adds sensitivity parameter and frequency range mapping

**API**:
```cpp
class SweepEnvelopeFollower {
public:
    void prepare(double sampleRate);
    void setAttack(float ms);   // 1-100ms
    void setRelease(float ms);  // 10-500ms
    void setSensitivity(float sens);  // 0-100%

    // Returns modulation offset in Hz based on input level
    float process(float inputSample);

private:
    Krate::DSP::EnvelopeFollower env_;
    float sensitivity_ = 0.5f;
    float minFreq_ = 20.0f;
    float maxFreq_ = 20000.0f;
};
```

---

### 8. MIDI CC Mapping

**Question**: How should MIDI CC mapping be implemented for sweep frequency?

**Decision**: Use VST3 IMidiMapping interface with configurable CC number

**Rationale**:
- VST3 SDK provides IMidiMapping for CC-to-parameter binding
- Supports both 7-bit (0-127) and 14-bit (MSB+LSB) modes per FR-029
- Host handles the actual CC reception, plugin just maps to parameter

**Implementation**:
```cpp
// In Controller class:
class Controller : public EditControllerEx1, public IMidiMapping {
    // IMidiMapping interface
    tresult PLUGIN_API getMidiControllerAssignment(
        int32 busIndex, int16 channel, CtrlNumber midiCC,
        ParamID& outParamId) override
    {
        if (midiCC == sweepMidiCC_) {
            outParamId = makeSweepParamId(SweepParamType::kSweepFrequency);
            return kResultTrue;
        }
        return kResultFalse;
    }

private:
    CtrlNumber sweepMidiCC_ = 74;  // Default: Brightness (CC#74)
};
```

---

### 9. SweepIndicator Rendering

**Question**: How should the sweep indicator be rendered on SpectrumDisplay?

**Decision**: Overlay layer with Gaussian/triangular curve, audio-synchronized position

**Rationale**:
- Per custom-controls.md Section 1.3.4:
  - Semi-transparent (white at 40% opacity)
  - 2px solid vertical center line
  - Gaussian bell for Smooth mode, triangular for Sharp mode
  - Height proportional to intensity

**Visual Specification**:
```
                    Sweep Width (2sigma)
               <------------------------>
                       /\  <- Peak = Intensity
                      /  \
                     /    \
                    /      \
                   /        \
------------------/----------\------------------
                      |
                 Center Freq
```

**Implementation Approach**:
```cpp
class SweepIndicator : public CViewContainer {
public:
    void draw(CDrawContext* context) override {
        if (!enabled_) return;

        float centerX = freqToX(centerFreqHz_, getWidth());
        float halfWidthX = freqToX(centerFreqHz_ * std::pow(2.0f, widthOctaves_/2.0f), getWidth())
                         - centerX;

        // Draw curve (Gaussian or triangular based on falloff mode)
        CGraphicsPath* path = context->createGraphicsPath();
        if (falloffMode_ == SweepFalloff::Smooth) {
            drawGaussianCurve(path, centerX, halfWidthX, intensity_);
        } else {
            drawTriangularCurve(path, centerX, halfWidthX, intensity_);
        }

        // Draw center line
        context->setLineWidth(2.0);
        context->drawLine(CPoint(centerX, 0), CPoint(centerX, getHeight()));
    }
};
```

---

### 10. Modulation Combination

**Question**: How should LFO and envelope follower modulation combine?

**Decision**: Additive combination, clamp to parameter range

**Rationale**:
- Per clarification in spec: "Additive (sum both modulation amounts, clamp to parameter range)"
- Base frequency + LFO offset + envelope offset, then clamp to [20Hz, 20kHz]
- Order: base -> +LFO -> +envelope -> clamp

**Implementation**:
```cpp
float calculateModulatedFrequency(float baseFreq, float lfoOffset, float envOffset) {
    float modulated = baseFreq + lfoOffset + envOffset;
    return std::clamp(modulated, 20.0f, 20000.0f);
}
```

---

## Summary

All technical decisions have been made. Key findings:

1. **Direct Gaussian calculation** is fast enough for 8 bands
2. **OnePoleSmoother** (existing) handles parameter smoothing
3. **SPSC ring buffer** pattern from SDK for audio-UI sync
4. **Wrap existing LFO/EnvelopeFollower** classes for sweep modulation
5. **Linear breakpoint interpolation** for custom curves
6. **IMidiMapping** interface for MIDI CC
7. **Additive modulation** for LFO + envelope combination

No unresolved questions remain. Ready to proceed to Phase 1 design.
