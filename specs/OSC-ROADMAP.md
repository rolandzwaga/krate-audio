# Oscillator & Waveform Generation DSP Roadmap

A phased roadmap for adding oscillator and waveform generation capabilities to the KrateDSP library. All components follow the layered architecture and build on existing infrastructure.

Research source: `specs/DSP-OSCILLATOR-TECHNIQUES.md`

## Design Principles

### 1. Layer Architecture

| Layer | Purpose | Oscillator Role |
|-------|---------|-----------------|
| **Layer 0 (core/)** | Math utilities, constants | PolyBLEP math, phase accumulator utils, wavetable types |
| **Layer 1 (primitives/)** | Stateless/simple stateful DSP | PolyBLEP oscillator, wavetable oscillator, minBLEP table, noise generators |
| **Layer 2 (processors/)** | Configurable processors | FM operator, sync oscillator, formant osc, chaos osc, particle cloud |
| **Layer 3 (systems/)** | Complete systems | Supersaw/Unison engine, FM voice, vector mixer |

### 2. Anti-Aliasing Strategy

**External Oversampling (Composable):**
- Oscillator primitives are "pure" - process at whatever sample rate they receive
- Use existing `Oversampler<Factor, NumChannels>` externally for 2x/4x
- PolyBLEP + 2x oversampling = excellent quality (per research recommendation)

**Built-in Anti-Aliasing for Oscillators:**
- PolyBLEP correction applied directly in the oscillator (no table needed)
- minBLEP tables precomputed in `prepare()` for hard sync scenarios
- Wavetable mipmapping handles anti-aliasing for wavetable playback

### 3. Code Reuse

**Existing Components to Leverage:**

| Component | Location | Reuse For |
|-----------|----------|-----------|
| `Xorshift32` | `core/random.h` | Noise generation, particle spawn |
| `math_constants.h` | `core/` | kPi, kTwoPi for phase calculations |
| `pitch_utils.h` | `core/` | semitonesToRatio, ratioToSemitones |
| `interpolation.h` | `core/` | Linear, cubic Hermite, Lagrange for wavetable reading |
| `window_functions.h` | `core/` | Grain envelopes, wavetable generation |
| `grain_envelope.h` | `core/` | Grain window shapes |
| `note_value.h` | `core/` | Tempo sync for LFO/sequenced oscillators |
| `modulation_source.h` | `core/` | Interface for oscillators as mod sources |
| `fast_math.h` | `core/` | fastTanh for feedback FM limiting |
| `LFO` | `primitives/lfo.h` | Reference pattern, potential LFO upgrade |
| `Oversampler<>` | `primitives/oversampler.h` | Anti-aliasing wrapper |
| `FFT` | `primitives/fft.h` | Wavetable mipmap generation, additive IFFT |
| `STFT` | `primitives/stft.h` | Spectral freeze oscillator |
| `SpectralBuffer` | `primitives/spectral_buffer.h` | Spectral freeze storage |
| `DCBlocker` | `primitives/dc_blocker.h` | DC offset from asymmetric waveforms |
| `OnePoleSmoother` | `primitives/smoother.h` | Parameter smoothing |

---

## Current State: What Already Exists

| Component | File | Notes |
|-----------|------|-------|
| LFO (wavetable, 6 shapes) | `primitives/lfo.h` | Sub-audio only (0.01-20Hz), 2048-sample tables, no anti-aliasing |
| AudioRateFilterFM oscillator | `processors/audio_rate_filter_fm.h` | 4 waveforms (sine/tri/saw/square), 2048 tables, no anti-aliasing |
| ChaosModSource | `processors/chaos_mod_source.h` | Lorenz/Rossler/Chua/Henon, control-rate update (every 32 samples) |
| NoiseGenerator | `processors/noise_generator.h` | 13 noise types (white through radio static), complete |
| ModalResonator | `processors/modal_resonator.h` | Two-pole sinusoidal oscillator, up to 32 modes |
| FrequencyShifter | `processors/frequency_shifter.h` | Quadrature oscillator (sine/cosine pair) |

### Gap Analysis

**What's missing for audio-rate oscillators:**
1. No band-limited waveforms (PolyBLEP, minBLEP, DPW)
2. No wavetable oscillator with mipmap anti-aliasing
3. No FM/PM synthesis operators or algorithms
4. No oscillator sync (hard or soft)
5. No sub-oscillator infrastructure
6. No supersaw/unison detuning
7. No phase distortion synthesis
8. No additive synthesis (partial control)
9. No vector/morphing synthesis

---

## Tier 1: Core Oscillator Infrastructure

### Phase 1: PolyBLEP Math Foundations (Layer 0)

**Goal:** Add the mathematical building blocks for polynomial band-limited step/ramp corrections, shared by all PolyBLEP-based oscillators.

**Layer:** 0 (core/)
**Dependencies:** `math_constants.h` only
**Enables:** Phase 2 (PolyBLEP Oscillator), Phase 5 (Sync)

#### 1.1 PolyBLEP Utilities (`core/polyblep.h`)

```
Location: dsp/include/krate/dsp/core/polyblep.h
Dependencies: math_constants.h
```

**Contains:**
- `polyBlep(t, dt)` - 2-point polynomial band-limited step correction
- `polyBlep4(t, dt)` - 4-point higher quality variant
- `polyBlamp(t, dt)` - Polynomial band-limited ramp (for triangle)
- `polyBlamp4(t, dt)` - 4-point higher quality ramp

All functions are `[[nodiscard]] constexpr float ... noexcept`.

**Rationale:** Pure math with no state. Layer 0 is correct because they depend only on arithmetic (like `interpolation.h` or `sigmoid.h`).

#### 1.2 Phase Accumulator Utilities (`core/phase_utils.h`)

```
Location: dsp/include/krate/dsp/core/phase_utils.h
Dependencies: None (stdlib only)
```

**Contains:**
- `PhaseAccumulator` - Struct with `phase`, `increment`, advance/wrap logic
- `calculatePhaseIncrement(frequency, sampleRate)` - Normalized increment
- `wrapPhase(phase)` - Wrap to [0, 1) with correct subtraction
- `detectPhaseWrap(currentPhase, previousPhase)` - Returns true on wrap
- `subsamplePhaseWrapOffset(phase, increment)` - Fractional position of wrap

**Rationale:** Phase accumulation code is duplicated in `lfo.h`, `audio_rate_filter_fm.h`, and `frequency_shifter.h`. Centralizing prevents further duplication and ensures consistent sub-sample accuracy.

**Status:** Not started

---

### Phase 2: PolyBLEP Oscillator (Layer 1)

**Goal:** Audio-rate band-limited oscillator with saw, square, pulse, and triangle waveforms using PolyBLEP anti-aliasing.

**Layer:** 1 (primitives/)
**Dependencies:** Phase 1 (`core/polyblep.h`, `core/phase_utils.h`), `core/math_constants.h`
**Enables:** Phase 5 (Sync), Phase 6 (Sub-oscillator), Phase 7 (Supersaw)

#### 2.1 PolyBLEP Oscillator (`primitives/polyblep_oscillator.h`)

```
Location: dsp/include/krate/dsp/primitives/polyblep_oscillator.h
Dependencies: core/polyblep.h, core/phase_utils.h, core/math_constants.h
```

```cpp
enum class OscWaveform : uint8_t {
    Sine = 0,     // No anti-aliasing needed (pure sine)
    Sawtooth,     // PolyBLEP at phase wrap
    Square,       // PolyBLEP at both edges
    Pulse,        // PolyBLEP with variable pulse width
    Triangle      // Integrated PolyBLEP square (leaky integrator)
};

class PolyBlepOscillator {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setFrequency(float hz) noexcept;
    void setWaveform(OscWaveform waveform) noexcept;
    void setPulseWidth(float width) noexcept;  // [0.01, 0.99]

    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;

    // For sync: access to phase state
    [[nodiscard]] double phase() const noexcept;
    [[nodiscard]] bool phaseWrapped() const noexcept;
    void resetPhase(double newPhase = 0.0) noexcept;

    // For FM/PM input
    void setPhaseModulation(float radians) noexcept;
    void setFrequencyModulation(float hz) noexcept;
};
```

**Design Notes:**
- Single-sample `process()` for sync/FM integration
- Block processing for efficiency when no modulation
- Phase wrap detection exposed for sync and sub-oscillator
- Triangle uses leaky integrator of antialiased square (standard approach)
- Frequency clamped to [0, sampleRate/2] internally

**Status:** Not started

---

### Phase 3: Wavetable Oscillator with Mipmapping (Layer 0 + Layer 1)

**Goal:** General-purpose wavetable oscillator with mipmap anti-aliasing, supporting user-loadable wavetables and standard waveforms.

**Layer:** 0 (core/) + 1 (primitives/)
**Dependencies:** `core/interpolation.h`, `core/math_constants.h`, `primitives/fft.h`
**Enables:** Phase 8 (FM Operator), Phase 10 (Phase Distortion), Phase 11 (Additive)

#### 3.1 Wavetable Data Structure (`core/wavetable_data.h`)

```
Location: dsp/include/krate/dsp/core/wavetable_data.h
Layer: 0
Dependencies: stdlib only
```

**Contains:**
- `WavetableData` - Struct holding multiple mipmap levels of a single waveform
- Table size constants (2048 samples default)
- Mipmap level count (10 levels for ~10 octaves of coverage)
- `selectMipmapLevel(frequency, sampleRate, tableSize)` - Level selection

#### 3.2 Wavetable Generator (`primitives/wavetable_generator.h`)

```
Location: dsp/include/krate/dsp/primitives/wavetable_generator.h
Layer: 1
Dependencies: core/wavetable_data.h, primitives/fft.h, core/window_functions.h
```

**Contains:**
- `generateMipmappedSaw(WavetableData&)` - Additive synthesis method
- `generateMipmappedSquare(WavetableData&)` - Odd harmonics only
- `generateMipmappedTriangle(WavetableData&)` - Odd harmonics, alternating sign
- `generateMipmappedFromHarmonics(WavetableData&, const float* harmonicAmplitudes, size_t numHarmonics)` - Custom spectrum
- `generateMipmappedFromSamples(WavetableData&, const float* samples, size_t sampleCount)` - FFT -> zero bins -> IFFT per level

**Rationale:** Separate from the oscillator so wavetables can be precomputed once and shared across oscillator instances.

#### 3.3 Wavetable Oscillator (`primitives/wavetable_oscillator.h`)

```
Location: dsp/include/krate/dsp/primitives/wavetable_oscillator.h
Layer: 1
Dependencies: core/wavetable_data.h, core/interpolation.h, core/phase_utils.h
```

```cpp
class WavetableOscillator {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setWavetable(const WavetableData* table) noexcept;  // Non-owning pointer
    void setFrequency(float hz) noexcept;
    void setInterpolation(InterpolationMode mode) noexcept;  // Linear, CubicHermite

    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;

    // Phase control (same interface as PolyBlepOscillator)
    [[nodiscard]] double phase() const noexcept;
    void resetPhase(double newPhase = 0.0) noexcept;
    void setPhaseModulation(float radians) noexcept;
};
```

**Design Notes:**
- Non-owning pointer to WavetableData allows table sharing
- Automatic mipmap level selection based on frequency
- Cubic Hermite interpolation for high-quality playback
- Same phase interface as PolyBlepOscillator for interchangeability

**Status:** Not started

---

## Tier 2: Essential Synthesis Features

### Phase 4: minBLEP Table (Layer 1)

**Goal:** Precomputed minimum-phase band-limited step function table for high-quality discontinuity correction in sync oscillators and beyond.

**Layer:** 1 (primitives/)
**Dependencies:** `primitives/fft.h`, `core/window_functions.h`
**Enables:** Phase 5 (Hard Sync)

#### 4.1 minBLEP Table (`primitives/minblep_table.h`)

```
Location: dsp/include/krate/dsp/primitives/minblep_table.h
Dependencies: primitives/fft.h, core/window_functions.h
```

```cpp
class MinBlepTable {
public:
    // Generate table at prepare time (NOT real-time safe)
    void prepare(size_t oversamplingFactor = 64, size_t zeroCrossings = 8);

    // Query table (real-time safe)
    [[nodiscard]] float sample(float subsampleOffset, size_t index) const noexcept;
    [[nodiscard]] size_t length() const noexcept;

    // Residual buffer for mixing into output
    struct Residual {
        void addBlep(float subsampleOffset, float amplitude) noexcept;
        [[nodiscard]] float consume() noexcept;
        void reset() noexcept;
    };
};
```

**Algorithm:**
1. Generate windowed sinc (BLIT)
2. Integrate to get BLEP
3. FFT -> minimum-phase transform -> IFFT
4. Store as oversampled polyphase table

**Design Notes:**
- Generates once in `prepare()`, then table is read-only
- `Residual` is a small ring buffer mixed into oscillator output at each discontinuity
- subsampleOffset provides sub-sample accuracy for correct timing

**Status:** Not started

---

### Phase 5: Oscillator Sync (Layer 2)

**Goal:** Hard sync and soft sync variants, using minBLEP for clean discontinuity correction at the reset point.

**Layer:** 2 (processors/)
**Dependencies:** Phase 2 (`PolyBlepOscillator`), Phase 4 (`MinBlepTable`)
**Enables:** Used directly in synth voices

#### 5.1 Sync Oscillator (`processors/sync_oscillator.h`)

```
Location: dsp/include/krate/dsp/processors/sync_oscillator.h
Dependencies: primitives/polyblep_oscillator.h, primitives/minblep_table.h
```

```cpp
enum class SyncMode : uint8_t {
    Hard = 0,     // Reset slave phase on master wrap
    Reverse,      // Reverse slave direction on master wrap
    PhaseAdvance  // Advance slave phase by fixed amount on master wrap
};

class SyncOscillator {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setMasterFrequency(float hz) noexcept;
    void setSlaveFrequency(float hz) noexcept;
    void setSlaveWaveform(OscWaveform waveform) noexcept;
    void setSyncMode(SyncMode mode) noexcept;
    void setSyncAmount(float amount) noexcept;  // For soft sync blend

    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
};
```

**Design Notes:**
- Master oscillator is internal (always sine or saw for clean wrap detection)
- Slave oscillator is PolyBlepOscillator with minBLEP correction at reset
- Sub-sample accuracy: compute exact reset position, apply minBLEP residual
- Hard sync resets slave to master's fractional position (not zero) per research

**Status:** Not started

---

### Phase 6: Sub-Oscillator (Layer 2)

**Goal:** Frequency-divided sub-oscillator that tracks a master oscillator via flip-flop division, as in analog synthesizers.

**Layer:** 2 (processors/)
**Dependencies:** Phase 2 (`PolyBlepOscillator`)
**Enables:** Used directly in synth voices

#### 6.1 Sub-Oscillator (`processors/sub_oscillator.h`)

```
Location: dsp/include/krate/dsp/processors/sub_oscillator.h
Dependencies: primitives/polyblep_oscillator.h
```

```cpp
enum class SubOctave : uint8_t {
    OneOctave = 0,  // Divide by 2
    TwoOctaves = 1  // Divide by 4
};

enum class SubWaveform : uint8_t {
    Square = 0,   // Classic analog flip-flop output
    Sine,         // Digital: sine at sub frequency
    Triangle      // Digital: triangle at sub frequency
};

class SubOscillator {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setOctave(SubOctave octave) noexcept;
    void setWaveform(SubWaveform waveform) noexcept;
    void setMix(float mix) noexcept;  // 0 = main only, 1 = sub only

    // Process: takes master phase wrap signal, returns sub output
    [[nodiscard]] float process(bool masterPhaseWrapped) noexcept;

    // Convenience: process mixed with main oscillator output
    [[nodiscard]] float processMixed(float mainOutput, bool masterPhaseWrapped) noexcept;
};
```

**Design Notes:**
- Flip-flop state toggled on master phase wrap (authentic analog behavior)
- Square sub uses minBLEP at toggle points for clean output
- Sine/triangle sub calculated from derived frequency (digital approach)
- Mix control uses equal-power crossfade

**Status:** Not started

---

### Phase 7: Supersaw / Unison Engine (Layer 3)

**Goal:** Multi-voice detuned oscillator with stereo spread, modeled on the Roland JP-8000 supersaw analysis.

**Layer:** 3 (systems/)
**Dependencies:** Phase 2 (`PolyBlepOscillator`), `core/stereo_utils.h`, `core/pitch_utils.h`
**Enables:** Used directly in synth voices

#### 7.1 Unison Engine (`systems/unison_engine.h`)

```
Location: dsp/include/krate/dsp/systems/unison_engine.h
Dependencies: primitives/polyblep_oscillator.h, core/stereo_utils.h, core/pitch_utils.h
```

```cpp
class UnisonEngine {
public:
    static constexpr size_t kMaxVoices = 16;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setNumVoices(size_t count) noexcept;     // 1-16
    void setDetune(float amount) noexcept;          // 0-1 (non-linear curve)
    void setStereoSpread(float spread) noexcept;    // 0-1
    void setWaveform(OscWaveform waveform) noexcept;
    void setFrequency(float hz) noexcept;
    void setBlend(float blend) noexcept;            // Center vs detuned mix

    struct StereoOutput { float left; float right; };
    [[nodiscard]] StereoOutput process() noexcept;
    void processBlock(float* left, float* right, size_t numSamples) noexcept;
};
```

**Design Notes:**
- Non-linear detune curve per Adam Szabo's JP-8000 analysis
- Detune amounts NOT evenly spaced (3 pairs with different spreads)
- Each voice is a PolyBlepOscillator instance
- Stereo spread pans voices across the stereo field
- Random initial phase per voice for natural thickness

**Status:** Not started

---

### Phase 8: FM/PM Synthesis Operator (Layer 2)

**Goal:** Single FM operator (oscillator + ratio + feedback + level), the fundamental building block for FM/PM synthesis.

**Layer:** 2 (processors/)
**Dependencies:** Phase 3 (`WavetableOscillator`) or Phase 2 (`PolyBlepOscillator`)
**Enables:** Phase 9 (FM Voice)

#### 8.1 FM Operator (`processors/fm_operator.h`)

```
Location: dsp/include/krate/dsp/processors/fm_operator.h
Dependencies: primitives/wavetable_oscillator.h, core/fast_math.h, core/pitch_utils.h
```

```cpp
class FMOperator {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setFrequency(float hz) noexcept;       // Base frequency
    void setRatio(float ratio) noexcept;          // Frequency ratio (e.g., 1.0, 2.0, 3.5)
    void setFeedback(float amount) noexcept;      // Self-modulation [0, 1]
    void setLevel(float level) noexcept;          // Output level [0, 1]

    // Process with optional phase modulation input
    [[nodiscard]] float process(float phaseModInput = 0.0f) noexcept;

    // Get raw output (before level) for use as modulator
    [[nodiscard]] float lastRawOutput() const noexcept;
};
```

**Design Notes:**
- Uses phase modulation (Yamaha-style), NOT frequency modulation
- Feedback FM: output[n-1] fed back to own phase input, with tanh limiting
- Ratio is frequency multiplier (integer = harmonic, non-integer = inharmonic)
- Sine wave for classic FM (wavetable for extended FM)
- Level controls modulation depth when used as modulator

#### 8.2 FM Voice (Layer 3) (`systems/fm_voice.h`)

A system-level composition of 4-6 FMOperators with configurable algorithm routing. The FM operator alone (Phase 8) is independently useful for effects and modulation.

**Status:** Not started

---

## Tier 3: Extended Synthesis

### Phase 9: Noise Oscillator (Layer 1)

**Goal:** Lightweight pitched noise generators for use as oscillator sources, distinct from the effects-oriented NoiseGenerator at Layer 2.

**Layer:** 1 (primitives/)
**Dependencies:** `core/random.h`, `core/math_constants.h`
**Enables:** Used as oscillator component, excitation source

#### 9.1 Noise Oscillator (`primitives/noise_oscillator.h`)

```
Location: dsp/include/krate/dsp/primitives/noise_oscillator.h
Dependencies: core/random.h, core/math_constants.h
```

```cpp
enum class NoiseColor : uint8_t {
    White = 0,
    Pink,       // -3dB/octave (Paul Kellet filter)
    Brown,      // -6dB/octave (leaky integrator)
    Blue,       // +3dB/octave (differentiated pink)
    Violet      // +6dB/octave (differentiated white)
};

class NoiseOscillator {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setColor(NoiseColor color) noexcept;
    void setSeed(uint32_t seed) noexcept;

    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
};
```

**Rationale:** The existing `NoiseGenerator` (Layer 2) has 13 types with levels, envelopes, and sidechain input. This lightweight primitive provides core noise algorithms for composition into oscillator-level components (e.g., excitation for Karplus-Strong, breathy texture layers, noise-based oscillators).

**Status:** Not started

---

### Phase 10: Phase Distortion Oscillator (Layer 2)

**Goal:** Casio CZ-style phase distortion synthesis where a sine wave is read at variable speed to produce classic analog-like waveforms.

**Layer:** 2 (processors/)
**Dependencies:** Phase 3 (`WavetableOscillator`), `core/phase_utils.h`
**Enables:** Used directly for synthesis

#### 10.1 Phase Distortion Oscillator (`processors/phase_distortion_oscillator.h`)

```
Location: dsp/include/krate/dsp/processors/phase_distortion_oscillator.h
Dependencies: primitives/wavetable_oscillator.h, core/phase_utils.h
```

```cpp
enum class PDWaveform : uint8_t {
    Saw = 0,          // Accelerated phase -> saw shape
    Square,           // Step phase change
    Pulse,            // Narrow step
    DoubleSine,       // Two half-cycles compressed
    HalfSine,         // One half-cycle stretched
    ResonantSaw,      // Windowed fast phase (resonant peak)
    ResonantTriangle, // Windowed triangle phase
    ResonantTrapezoid // Windowed trapezoid phase
};

class PhaseDistortionOscillator {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setFrequency(float hz) noexcept;
    void setWaveform(PDWaveform waveform) noexcept;
    void setDistortion(float amount) noexcept;  // 0=sine, 1=full shape (DCW)

    [[nodiscard]] float process() noexcept;
};
```

**Design Notes:**
- DCW (Digitally Controlled Wave) parameter = distortion amount
- At distortion=0, all shapes produce a sine wave
- At distortion=1, full characteristic shape
- Resonant types use windowed sync technique (phase runs faster, resets)
- Resonant types may need PolyBLEP correction at reset points

**Status:** Not started

---

### Phase 11: Additive Oscillator (Layer 2)

**Goal:** Partial-based additive synthesis with harmonic/inharmonic control and efficient IFFT resynthesis.

**Layer:** 2 (processors/)
**Dependencies:** `primitives/fft.h`, `core/phase_utils.h`, `core/window_functions.h`
**Enables:** Used for synthesis and spectral sound design

#### 11.1 Additive Oscillator (`processors/additive_oscillator.h`)

```
Location: dsp/include/krate/dsp/processors/additive_oscillator.h
Dependencies: primitives/fft.h, core/phase_utils.h, core/window_functions.h
```

```cpp
class AdditiveOscillator {
public:
    static constexpr size_t kMaxPartials = 128;

    void prepare(double sampleRate, size_t fftSize = 2048) noexcept;
    void reset() noexcept;

    void setFundamental(float hz) noexcept;

    // Per-partial control
    void setPartialAmplitude(size_t index, float amplitude) noexcept;
    void setPartialFrequencyRatio(size_t index, float ratio) noexcept;
    void setPartialPhase(size_t index, float phase) noexcept;

    // Macro controls
    void setNumPartials(size_t count) noexcept;
    void setSpectralTilt(float tiltDb) noexcept;    // dB/octave rolloff
    void setInharmonicity(float amount) noexcept;    // Stretch ratio

    // Uses IFFT for efficient resynthesis
    void processBlock(float* output, size_t numSamples) noexcept;
};
```

**Design Notes:**
- Uses overlap-add IFFT for efficient many-partial resynthesis
- Automatically removes partials above Nyquist (inherent anti-aliasing)
- Spectral tilt macro scales partial amplitudes by frequency
- Inharmonicity stretches partial ratios (partial_n = n * (1 + B * n^2))
- Block-only processing (IFFT is inherently block-based)

**Status:** Not started

---

## Tier 4: Novel / Experimental Oscillators

Each of these phases is independently implementable and targets creative sound design.

### Phase 12: Chaos Attractor Oscillator (Layer 2) - COMPLETE

**Goal:** Audio-rate chaos oscillator generating waveforms from Lorenz, Rossler, and other attractor systems. Extends the existing control-rate ChaosModSource to audio rate with pitch control.

**Layer:** 2 (processors/)
**Dependencies:** `core/math_constants.h`, `core/fast_math.h`, `primitives/dc_blocker.h`
**Enables:** Used directly for sound design

**Specification:** [specs/026-chaos-attractor-oscillator/](026-chaos-attractor-oscillator/)

**Implementation:** [dsp/include/krate/dsp/processors/chaos_oscillator.h](../dsp/include/krate/dsp/processors/chaos_oscillator.h)

**Completion Date:** 2026-02-05

#### 12.1 Chaos Oscillator (`processors/chaos_oscillator.h`)

```
Location: dsp/include/krate/dsp/processors/chaos_oscillator.h
Dependencies: core/math_constants.h, core/fast_math.h, primitives/dc_blocker.h
```

```cpp
enum class ChaosAttractor : uint8_t {
    Lorenz = 0,
    Rossler,
    Chua,
    Duffing,
    VanDerPol
};

class ChaosOscillator {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setAttractor(ChaosAttractor type) noexcept;
    void setFrequency(float hz) noexcept;     // Approximate pitch via dt scaling
    void setChaos(float amount) noexcept;       // Control parameter (e.g., rho for Lorenz)
    void setCoupling(float amount) noexcept;    // External input coupling
    void setOutput(size_t axis) noexcept;       // 0=x, 1=y, 2=z

    [[nodiscard]] float process(float externalInput = 0.0f) noexcept;
    void processBlock(float* output, size_t numSamples,
                      const float* extInput = nullptr) noexcept;
};
```

**Design Notes:**
- Audio-rate update (every sample), unlike ChaosModSource's 32-sample interval
- Frequency control via dt scaling (approximate pitch, not exact)
- DC blocker on output (chaotic signals often have DC offset)
- Divergence detection with automatic state reset
- tanh normalization to [-1, 1] output range
- Duffing and Van der Pol added for audio-suitable timbres
- RK4 integration with adaptive substepping for numerical stability
- baseDt values scaled 100x from original spec for audible output

**Status:** COMPLETE

---

### Phase 13: Formant Oscillator (Layer 2) - COMPLETE

**Goal:** Direct formant synthesis using FOF (Fonction d'Onde Formantique) technique for vowel-like tones without filter chains.

**Layer:** 2 (processors/)
**Dependencies:** `core/math_constants.h`, `core/phase_utils.h`, `core/filter_tables.h`
**Enables:** Used directly for vocal synthesis

**Specification:** [specs/027-formant-oscillator/](027-formant-oscillator/)

**Implementation:** [dsp/include/krate/dsp/processors/formant_oscillator.h](../dsp/include/krate/dsp/processors/formant_oscillator.h)

**Completion Date:** 2026-02-06

#### 13.1 Formant Oscillator (`processors/formant_oscillator.h`)

```
Location: dsp/include/krate/dsp/processors/formant_oscillator.h
Dependencies: core/math_constants.h, core/phase_utils.h, core/filter_tables.h
```

```cpp
class FormantOscillator {
public:
    static constexpr size_t kNumFormants = 5;
    static constexpr size_t kGrainsPerFormant = 8;
    static constexpr float kMasterGain = 0.4f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setFundamental(float hz) noexcept;  // [20, 2000] Hz

    // Per-formant control
    void setFormantFrequency(size_t index, float hz) noexcept;
    void setFormantBandwidth(size_t index, float hz) noexcept;
    void setFormantAmplitude(size_t index, float amp) noexcept;

    // Vowel presets and morphing
    void setVowel(Vowel vowel) noexcept;  // Reuses Vowel enum from filter_tables.h
    void morphVowels(Vowel from, Vowel to, float mix) noexcept;
    void setMorphPosition(float position) noexcept;  // [0, 4] for A-E-I-O-U

    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
};
```

**Design Notes:**
- FOF technique: 5 parallel formant generators with fixed 8-grain pools
- 3ms attack (half-cycle raised cosine), 20ms grain duration, exponential decay
- Bandwidth controls decay constant: decayConstant = pi * bandwidthHz
- Vowel presets from Csound tables (bass male voice)
- Position-based morphing: 0=A, 1=E, 2=I, 3=O, 4=U with linear interpolation
- Master gain 0.4 (theoretical max output ~1.12)
- Reuses existing Vowel enum for API consistency with FormantFilter

**Status:** COMPLETE

---

### Phase 14: Particle / Swarm Oscillator (Layer 2)

**Goal:** Many lightweight sine oscillators ("particles") with individual drift, lifetime, and spawn behavior, emerging into complex textural timbres.

**Layer:** 2 (processors/)
**Dependencies:** `core/random.h`, `core/math_constants.h`, `core/grain_envelope.h`
**Enables:** Used directly for sound design textures

#### 14.1 Particle Oscillator (`processors/particle_oscillator.h`)

```
Location: dsp/include/krate/dsp/processors/particle_oscillator.h
Dependencies: core/random.h, core/math_constants.h, core/grain_envelope.h
```

```cpp
class ParticleOscillator {
public:
    static constexpr size_t kMaxParticles = 64;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setFrequency(float centerHz) noexcept;
    void setDensity(float particles) noexcept;        // Active count [1, 64]
    void setFrequencyScatter(float semitones) noexcept; // Spread from center
    void setLifetime(float ms) noexcept;               // Particle duration
    void setSpawnMode(SpawnMode mode) noexcept;         // Regular, Random, Burst

    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
};
```

**Design Notes:**
- Each particle: phase, frequency (drifting around center), amplitude, lifetime
- Particles fade in/out using grain envelope shapes
- Spawn replaces expired particles
- Frequency scatter in semitones for musical control
- At low density/long lifetime: unison-like; at high density/short lifetime: granular cloud

**Status:** Not started

---

### Phase 15: Rungler / Shift Register Oscillator (Layer 2)

**Goal:** Rob Hordijk Benjolin-inspired rungler with two cross-modulating oscillators and a shift register creating chaotic stepped sequences.

**Layer:** 2 (processors/)
**Dependencies:** Phase 2 (`PolyBlepOscillator`), `core/random.h`
**Enables:** Used directly for generative/experimental synthesis

#### 15.1 Rungler (`processors/rungler.h`)

```
Location: dsp/include/krate/dsp/processors/rungler.h
Dependencies: primitives/polyblep_oscillator.h, core/random.h
```

```cpp
class Rungler {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setOsc1Frequency(float hz) noexcept;
    void setOsc2Frequency(float hz) noexcept;
    void setRunglerDepth(float amount) noexcept;  // How much SR modulates osc2
    void setRunglerBits(size_t bits) noexcept;     // Shift register length [4, 16]
    void setFilterAmount(float amount) noexcept;   // Built-in LP on rungler CV

    struct Output {
        float osc1;      // Raw oscillator 1
        float osc2;      // Raw oscillator 2
        float rungler;   // Shift register as stepped CV
        float mixed;     // Osc1 + Osc2 mix
    };

    [[nodiscard]] Output process() noexcept;
};
```

**Design Notes:**
- Osc1 clocks the shift register; osc2's output is sampled as the new bit
- Shift register value (interpreted as float) modulates osc2 frequency
- Creates locked pseudo-random patterns that evolve as frequencies change
- Multiple outputs available for routing flexibility
- Built-in low-pass smoothing on rungler CV to tame zipper artifacts

**Status:** Not started

---

### Phase 16: Spectral Freeze Oscillator (Layer 2)

**Goal:** Capture a single FFT frame and continuously resynthesize it, creating frozen spectral drones from any audio input.

**Layer:** 2 (processors/)
**Dependencies:** `primitives/stft.h`, `primitives/spectral_buffer.h`, `primitives/fft.h`
**Enables:** Used for spectral effects and drone generation

#### 16.1 Spectral Freeze Oscillator (`processors/spectral_freeze_oscillator.h`)

```
Location: dsp/include/krate/dsp/processors/spectral_freeze_oscillator.h
Dependencies: primitives/stft.h, primitives/spectral_buffer.h, primitives/fft.h
```

```cpp
class SpectralFreezeOscillator {
public:
    void prepare(double sampleRate, size_t fftSize = 2048) noexcept;
    void reset() noexcept;

    // Capture the current spectrum
    void freeze(const float* inputBlock, size_t blockSize) noexcept;
    void unfreeze() noexcept;

    // Modify frozen spectrum
    void setPitchShift(float semitones) noexcept;
    void setSpectralTilt(float dbPerOctave) noexcept;
    void setFormantShift(float semitones) noexcept;

    [[nodiscard]] bool isFrozen() const noexcept;

    // Resynthesize from frozen spectrum
    void processBlock(float* output, size_t numSamples) noexcept;
};
```

**Design Notes:**
- Stores magnitude spectrum; phase is advanced coherently per bin
- Overlap-add IFFT for continuous output
- Pitch shift via bin shifting
- Spectral tilt for brightness control of frozen sound
- Block-only processing (IFFT inherent)

**Status:** Not started

---

### Phase 17: Vector / Morph Mixer (Layer 3)

**Goal:** XY vector mixing between 4 oscillator sources with vector envelope automation, inspired by Prophet VS and Korg Wavestation.

**Layer:** 3 (systems/)
**Dependencies:** `core/crossfade_utils.h`, `core/math_constants.h`
**Enables:** Complete vector synthesis system

#### 17.1 Vector Mixer (`systems/vector_mixer.h`)

```
Location: dsp/include/krate/dsp/systems/vector_mixer.h
Dependencies: core/crossfade_utils.h, core/math_constants.h
```

```cpp
class VectorMixer {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setVectorX(float x) noexcept;  // [-1, 1]
    void setVectorY(float y) noexcept;  // [-1, 1]

    // Mix 4 input signals based on vector position
    [[nodiscard]] float process(float a, float b, float c, float d) noexcept;

    // Get current corner weights
    struct Weights { float a, b, c, d; };
    [[nodiscard]] Weights getWeights() const noexcept;
};
```

**Design Notes:**
- Agnostic to oscillator type (just mixes 4 float signals)
- Bilinear interpolation for smooth transitions
- Corner assignment: A=top-left, B=top-right, C=bottom-left, D=bottom-right
- Can be driven by LFO, envelope, or manual control
- Lightweight utility that composes with any oscillator phase

**Status:** Not started

---

## Dependency Graph

```
Phase 1 (L0: PolyBLEP Math)
    |
    v
Phase 2 (L1: PolyBLEP Osc) -----> Phase 5 (L2: Sync) --+
    |                                                      |
    +----> Phase 6 (L2: Sub Osc)                          |
    |                                                      |
    +----> Phase 7 (L3: Supersaw/Unison) <----------------+
    |
    +----> Phase 15 (L2: Rungler)
    |
Phase 3 (L1: Wavetable Osc) ----> Phase 8 (L2: FM Operator)
    |                                  |
    +----> Phase 10 (L2: PD Osc)      +----> FM Voice (L3, optional)
    |
    +----> Phase 17 (L3: Vector Mixer)

Phase 4 (L1: minBLEP Table) ----> Phase 5 (L2: Sync)

Phase 9  (L1: Noise Osc)               [independent]
Phase 11 (L2: Additive Osc)            [depends on FFT only]
Phase 12 (L2: Chaos Osc)               [independent]
Phase 13 (L2: Formant Osc)             [independent]
Phase 14 (L2: Particle Osc)            [independent]
Phase 16 (L2: Spectral Freeze Osc)     [depends on STFT/FFT only]
```

---

## Implementation Priority

### Tier 1: Core Oscillator Infrastructure (Phases 1-3)

Foundation for everything else. Must come first.

| Phase | Components | Layer | New Files |
|-------|-----------|-------|-----------|
| 1 | polyblep.h, phase_utils.h | L0 | 2 |
| 2 | polyblep_oscillator.h | L1 | 1 |
| 3 | wavetable_data.h, wavetable_generator.h, wavetable_oscillator.h | L0+L1 | 3 |

### Tier 2: Essential Synthesis Features (Phases 4-8)

Standard synth capabilities.

| Phase | Components | Layer | New Files |
|-------|-----------|-------|-----------|
| 4 | minblep_table.h | L1 | 1 |
| 5 | sync_oscillator.h | L2 | 1 |
| 6 | sub_oscillator.h | L2 | 1 |
| 7 | unison_engine.h | L3 | 1 |
| 8 | fm_operator.h (+optional fm_voice.h) | L2 (+L3) | 1-2 |

### Tier 3: Extended Synthesis (Phases 9-11)

Additional synthesis methods.

| Phase | Components | Layer | New Files |
|-------|-----------|-------|-----------|
| 9 | noise_oscillator.h | L1 | 1 |
| 10 | phase_distortion_oscillator.h | L2 | 1 |
| 11 | additive_oscillator.h | L2 | 1 |

### Tier 4: Novel / Experimental (Phases 12-17)

Creative sound design oscillators. Each is independently implementable.

| Phase | Components | Layer | New Files |
|-------|-----------|-------|-----------|
| 12 | chaos_oscillator.h | L2 | 1 |
| 13 | formant_oscillator.h | L2 | 1 |
| 14 | particle_oscillator.h | L2 | 1 |
| 15 | rungler.h | L2 | 1 |
| 16 | spectral_freeze_oscillator.h | L2 | 1 |
| 17 | vector_mixer.h | L3 | 1 |

---

## Summary

| Layer | New Files | Components |
|-------|-----------|------------|
| **Layer 0** | 3 | polyblep.h, phase_utils.h, wavetable_data.h |
| **Layer 1** | 5 | polyblep_oscillator.h, wavetable_generator.h, wavetable_oscillator.h, minblep_table.h, noise_oscillator.h |
| **Layer 2** | 10 | sync_oscillator.h, sub_oscillator.h, fm_operator.h, phase_distortion_oscillator.h, additive_oscillator.h, chaos_oscillator.h, formant_oscillator.h, particle_oscillator.h, rungler.h, spectral_freeze_oscillator.h |
| **Layer 3** | 3 | unison_engine.h, fm_voice.h (optional), vector_mixer.h |
| **Total** | **21** | 17 phases |

Each phase produces 1-3 new header files and is small enough for a single spec document. Layer dependencies are strictly maintained throughout.
