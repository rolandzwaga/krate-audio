# API Contract: AudioRateFilterFM

**Feature**: 095-audio-rate-filter-fm | **Date**: 2026-01-24
**Location**: `dsp/include/krate/dsp/processors/audio_rate_filter_fm.h`
**Layer**: 2 (Processor)
**Namespace**: `Krate::DSP`

---

## Class: AudioRateFilterFM

### Summary

Audio-rate filter frequency modulation processor that creates metallic, bell-like, ring modulation-style, and aggressive timbres by modulating SVF filter cutoff at audio rates.

---

## Enumerations

### FMModSource

```cpp
enum class FMModSource : uint8_t {
    Internal = 0,  // Built-in wavetable oscillator
    External = 1,  // External modulator input
    Self = 2       // Filter output feedback
};
```

### FMFilterType

```cpp
enum class FMFilterType : uint8_t {
    Lowpass = 0,   // 12 dB/oct lowpass
    Highpass = 1,  // 12 dB/oct highpass
    Bandpass = 2,  // Constant 0 dB peak bandpass
    Notch = 3      // Band-reject filter
};
```

### FMWaveform

```cpp
enum class FMWaveform : uint8_t {
    Sine = 0,      // Pure sine wave
    Triangle = 1,  // Triangle wave
    Sawtooth = 2,  // Sawtooth wave
    Square = 3     // Square wave
};
```

---

## Lifecycle Methods

### prepare

```cpp
void prepare(double sampleRate, size_t maxBlockSize);
```

**Purpose**: Initialize processor for audio processing.

**Parameters**:
- `sampleRate`: Audio sample rate in Hz (minimum 1000.0)
- `maxBlockSize`: Maximum samples per processBlock() call

**Behavior**:
- Allocates wavetable memory
- Prepares SVF and oversamplers
- Pre-allocates processing buffers
- Generates wavetables

**Thread Safety**: NOT real-time safe (allocates memory)

**Preconditions**: None

**Postconditions**: `isPrepared()` returns true

---

### reset

```cpp
void reset() noexcept;
```

**Purpose**: Clear all internal state without changing parameters.

**Behavior**:
- Resets SVF integrator states to zero
- Resets oscillator phase to zero
- Clears self-modulation feedback history
- Resets oversampler filter states

**Thread Safety**: Real-time safe

**Preconditions**: None (safe to call before prepare)

**Postconditions**: Internal state cleared

---

## Carrier Filter Configuration

### setCarrierCutoff

```cpp
void setCarrierCutoff(float hz);
```

**Purpose**: Set the base/center frequency of the carrier filter.

**Parameters**:
- `hz`: Cutoff frequency in Hz, clamped to [20, sampleRate * 0.495 * oversamplingFactor]

**Thread Safety**: Real-time safe

---

### setCarrierQ

```cpp
void setCarrierQ(float q);
```

**Purpose**: Set the resonance/Q factor of the carrier filter.

**Parameters**:
- `q`: Q factor, clamped to [0.5, 20.0]

**Thread Safety**: Real-time safe

---

### setFilterType

```cpp
void setFilterType(FMFilterType type);
```

**Purpose**: Select the carrier filter response type.

**Parameters**:
- `type`: FMFilterType enum value (Lowpass, Highpass, Bandpass, Notch)

**Thread Safety**: Real-time safe

---

## Modulator Configuration

### setModulatorSource

```cpp
void setModulatorSource(FMModSource source);
```

**Purpose**: Select the source of the modulation signal.

**Parameters**:
- `source`: FMModSource enum value (Internal, External, Self)

**Thread Safety**: Real-time safe

---

### setModulatorFrequency

```cpp
void setModulatorFrequency(float hz);
```

**Purpose**: Set the internal oscillator frequency.

**Parameters**:
- `hz`: Frequency in Hz, clamped to [0.1, 20000]

**Note**: Only affects Internal modulation source.

**Thread Safety**: Real-time safe

---

### setModulatorWaveform

```cpp
void setModulatorWaveform(FMWaveform waveform);
```

**Purpose**: Select the internal oscillator waveform.

**Parameters**:
- `waveform`: FMWaveform enum value (Sine, Triangle, Sawtooth, Square)

**Note**: Only affects Internal modulation source.

**Thread Safety**: Real-time safe

---

## FM Depth Control

### setFMDepth

```cpp
void setFMDepth(float octaves);
```

**Purpose**: Set the modulation depth in octaves.

**Parameters**:
- `octaves`: Modulation range, clamped to [0.0, 6.0]

**Formula**: `modulatedCutoff = carrierCutoff * 2^(modulator * fmDepth)`

**Examples**:
- At depth=1, modulator=+1.0: cutoff doubles (1 octave up)
- At depth=1, modulator=-1.0: cutoff halves (1 octave down)
- At depth=0: cutoff is constant (no modulation)

**Thread Safety**: Real-time safe

---

## Oversampling Configuration

### setOversamplingFactor

```cpp
void setOversamplingFactor(int factor);
```

**Purpose**: Set the oversampling factor for anti-aliasing.

**Parameters**:
- `factor`: Integer value, clamped to valid values (1, 2, or 4)

**Clamping Rules**:
- factor ≤ 1: uses 1 (no oversampling)
- factor 2-3: uses 2 (2x oversampling)
- factor ≥ 4: uses 4 (4x oversampling)

**Note**: When factor > 1, SVF is prepared at oversampled rate.

**Thread Safety**: Real-time safe (if prepared, reconfigures SVF)

---

### getLatency

```cpp
[[nodiscard]] size_t getLatency() const noexcept;
```

**Purpose**: Get processing latency introduced by oversampling.

**Returns**: Latency in samples (0 for factor=1 or Economy/ZeroLatency quality modes; varies for FIR modes)

**Thread Safety**: Real-time safe

---

## Processing Methods

### process (single sample)

```cpp
[[nodiscard]] float process(float input, float externalModulator = 0.0f) noexcept;
```

**Purpose**: Process a single audio sample.

**Parameters**:
- `input`: Input audio sample
- `externalModulator`: External modulator value [-1, +1] (used only when source is External)

**Returns**: Filtered output sample

**Behavior**:
- If not prepared: returns input unchanged
- If NaN/Inf input: returns 0.0f and resets state
- Updates internal oscillator phase (for Internal source)
- Stores output for feedback (for Self source)
- Flushes denormals in filter state

**Thread Safety**: Real-time safe (noexcept, no allocations)

---

### processBlock (with external modulator)

```cpp
void processBlock(float* buffer, const float* modulator, size_t numSamples) noexcept;
```

**Purpose**: Process a block of audio with external modulation.

**Parameters**:
- `buffer`: Input/output audio buffer (in-place processing)
- `modulator`: External modulator buffer (nullptr treated as 0.0 for no modulation)
- `numSamples`: Number of samples to process

**Thread Safety**: Real-time safe

---

### processBlock (no external modulator)

```cpp
void processBlock(float* buffer, size_t numSamples) noexcept;
```

**Purpose**: Process a block of audio (Internal or Self modulation only).

**Parameters**:
- `buffer`: Input/output audio buffer (in-place processing)
- `numSamples`: Number of samples to process

**Thread Safety**: Real-time safe

---

## Query Methods

### isPrepared

```cpp
[[nodiscard]] bool isPrepared() const noexcept;
```

**Returns**: true if prepare() has been called successfully

---

### getCarrierCutoff

```cpp
[[nodiscard]] float getCarrierCutoff() const noexcept;
```

**Returns**: Current carrier cutoff frequency in Hz

---

### getCarrierQ

```cpp
[[nodiscard]] float getCarrierQ() const noexcept;
```

**Returns**: Current Q factor

---

### getFilterType

```cpp
[[nodiscard]] FMFilterType getFilterType() const noexcept;
```

**Returns**: Current filter type

---

### getModulatorSource

```cpp
[[nodiscard]] FMModSource getModulatorSource() const noexcept;
```

**Returns**: Current modulation source

---

### getModulatorFrequency

```cpp
[[nodiscard]] float getModulatorFrequency() const noexcept;
```

**Returns**: Current internal oscillator frequency in Hz

---

### getModulatorWaveform

```cpp
[[nodiscard]] FMWaveform getModulatorWaveform() const noexcept;
```

**Returns**: Current internal oscillator waveform

---

### getFMDepth

```cpp
[[nodiscard]] float getFMDepth() const noexcept;
```

**Returns**: Current FM depth in octaves

---

### getOversamplingFactor

```cpp
[[nodiscard]] int getOversamplingFactor() const noexcept;
```

**Returns**: Current oversampling factor (1, 2, or 4)

---

## Edge Cases

| Condition | Behavior |
|-----------|----------|
| process() before prepare() | Returns input unchanged |
| NaN/Inf input | Returns 0.0f, resets internal state |
| FM depth = 0 | Output identical to static SVF |
| Modulated cutoff < 20Hz | Clamped to 20Hz |
| Modulated cutoff > Nyquist | Clamped to sampleRate * 0.495 * factor |
| Invalid oversampling factor | Clamped to nearest valid (1, 2, or 4) |
| Self-mod output > 1.0 | Hard-clipped to [-1, +1] |
| Denormal filter state | Flushed to zero |
| Modulator frequency changed during processing | Phase continuity maintained (no clicks) |
| External modulator buffer is nullptr | Treated as 0.0 (no modulation) |

---

## Dependencies

| Component | Header | Usage |
|-----------|--------|-------|
| SVF | `<krate/dsp/primitives/svf.h>` | Carrier filter |
| Oversampler | `<krate/dsp/primitives/oversampler.h>` | Anti-aliasing |
| kPi, kTwoPi | `<krate/dsp/core/math_constants.h>` | Oscillator phase |
| flushDenormal | `<krate/dsp/core/db_utils.h>` | Denormal prevention |
| isNaN, isInf | `<krate/dsp/core/db_utils.h>` | Input validation |

---

## Constants

```cpp
// Internal constants
static constexpr size_t kWavetableSize = 2048;  // Wavetable samples
static constexpr float kMinCutoff = 20.0f;       // Minimum cutoff Hz
static constexpr float kMinQ = 0.5f;             // Minimum Q
static constexpr float kMaxQ = 20.0f;            // Maximum Q
static constexpr float kMinModFreq = 0.1f;       // Minimum modulator Hz
static constexpr float kMaxModFreq = 20000.0f;   // Maximum modulator Hz
static constexpr float kMaxFMDepth = 6.0f;       // Maximum FM depth octaves
```
