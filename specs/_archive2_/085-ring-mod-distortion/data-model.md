# Data Model: Ring Modulator Distortion

**Feature**: 085-ring-mod-distortion
**Date**: 2026-03-01

## Entities

### 1. RingModCarrierWaveform (New Enum)

**Location**: `dsp/include/krate/dsp/processors/ring_modulator.h`
**Namespace**: `Krate::DSP`

```cpp
enum class RingModCarrierWaveform : uint8_t {
    Sine = 0,       // Gordon-Smith phasor (2 muls + 2 adds)
    Triangle = 1,   // PolyBlepOscillator
    Sawtooth = 2,   // PolyBlepOscillator
    Square = 3,     // PolyBlepOscillator
    Noise = 4       // NoiseOscillator (white, fixed)
};
```

**Validation**: Values 0-4 only.
**Relationship**: Maps to `OscWaveform` for PolyBLEP cases (Triangle=OscWaveform::Triangle, Sawtooth=OscWaveform::Sawtooth, Square=OscWaveform::Square).

### 2. RingModFreqMode (New Enum)

**Location**: `dsp/include/krate/dsp/processors/ring_modulator.h`
**Namespace**: `Krate::DSP`

```cpp
enum class RingModFreqMode : uint8_t {
    Free = 0,       // Carrier frequency set directly in Hz
    NoteTrack = 1   // Carrier frequency = noteFrequency * ratio
};
```

**Validation**: Values 0-1 only.

### 3. RingModulator (New Class)

**Location**: `dsp/include/krate/dsp/processors/ring_modulator.h`
**Namespace**: `Krate::DSP`
**Layer**: 2 (depends on Layer 0: math_constants.h, db_utils.h; Layer 1: polyblep_oscillator.h, noise_oscillator.h, smoother.h)

#### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `sampleRate_` | `double` | `44100.0` | Sample rate in Hz |
| `prepared_` | `bool` | `false` | Whether prepare() has been called |
| `carrierWaveform_` | `RingModCarrierWaveform` | `Sine` | Active carrier waveform |
| `freqMode_` | `RingModFreqMode` | `NoteTrack` | Frequency mode |
| `freqHz_` | `float` | `440.0f` | Carrier frequency in Free mode (Hz) |
| `noteFrequency_` | `float` | `440.0f` | Note frequency from voice (Hz) |
| `ratio_` | `float` | `2.0f` | Carrier-to-note ratio for NoteTrack |
| `amplitude_` | `float` | `1.0f` | Carrier amplitude (drive) 0-1 |
| `stereoSpread_` | `float` | `0.0f` | Stereo spread amount 0-1 |
| `sinState_` | `float` | `0.0f` | Gordon-Smith sine state |
| `cosState_` | `float` | `1.0f` | Gordon-Smith cosine state |
| `sinStateR_` | `float` | `0.0f` | Right channel sine state (stereo) |
| `cosStateR_` | `float` | `1.0f` | Right channel cosine state (stereo) |
| `renormCounter_` | `int` | `0` | Renormalization counter |
| `renormCounterR_` | `int` | `0` | Right channel renorm counter |
| `polyBlepOsc_` | `PolyBlepOscillator` | -- | PolyBLEP for Triangle/Saw/Square |
| `polyBlepOscR_` | `PolyBlepOscillator` | -- | Right channel PolyBLEP (stereo) |
| `noiseOsc_` | `NoiseOscillator` | -- | Noise carrier |
| `noiseOscR_` | `NoiseOscillator` | -- | Right channel noise (stereo) |
| `freqSmoother_` | `OnePoleSmoother` | -- | Center frequency smoother |
| `freqSmootherR_` | `OnePoleSmoother` | -- | Right channel freq smoother (stereo) |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `prepare` | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Initialize all sub-components |
| `reset` | `void reset() noexcept` | Reset all oscillator and smoother state |
| `setCarrierWaveform` | `void setCarrierWaveform(RingModCarrierWaveform wf) noexcept` | Set carrier waveform |
| `setFreqMode` | `void setFreqMode(RingModFreqMode mode) noexcept` | Set Free/NoteTrack mode |
| `setFrequency` | `void setFrequency(float hz) noexcept` | Set carrier freq for Free mode |
| `setNoteFrequency` | `void setNoteFrequency(float hz) noexcept` | Set voice note frequency |
| `setRatio` | `void setRatio(float ratio) noexcept` | Set carrier-to-note ratio |
| `setAmplitude` | `void setAmplitude(float amplitude) noexcept` | Set carrier amplitude (drive) |
| `setStereoSpread` | `void setStereoSpread(float spread) noexcept` | Set stereo spread 0-1 |
| `processBlock` (mono) | `void processBlock(float* buffer, size_t numSamples) noexcept` | In-place mono processing |
| `processBlock` (stereo) | `void processBlock(float* left, float* right, size_t numSamples) noexcept` | In-place stereo processing |

#### State Transitions

```
[Constructed] --prepare()--> [Prepared] --reset()--> [Prepared]
                                  |
                                  v
                        processBlock() returns processed audio
                                  |
                  (parameter setters can be called any time after prepare)
```

#### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `kMinFreqHz` | `0.1f` | Minimum carrier frequency |
| `kMaxFreqHz` | `20000.0f` | Maximum carrier frequency |
| `kMinRatio` | `0.25f` | Minimum carrier-to-note ratio |
| `kMaxRatio` | `16.0f` | Maximum carrier-to-note ratio |
| `kMaxSpreadOffsetHz` | `50.0f` | Max stereo spread offset at spread=1.0 |
| `kSmoothingTimeMs` | `5.0f` | Frequency smoother time constant |
| `kRenormInterval` | `1024` | Gordon-Smith renormalization interval |

### 4. RuinaeDistortionParams Extension (Modified Struct)

**Location**: `plugins/ruinae/src/parameters/distortion_params.h`

#### New Fields (appended to existing struct)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `ringFreq` | `std::atomic<float>` | `~0.6882f` | Normalized freq (log-mapped to 0.1-20000 Hz; default maps to 440 Hz via `log(440/0.1)/log(200000)`) |
| `ringFreqMode` | `std::atomic<int>` | `1` | 0=Free, 1=NoteTrack |
| `ringRatio` | `std::atomic<float>` | `~0.1111f` | Normalized ratio (mapped to 0.25-16.0; default maps to 2.0 via `(2.0-0.25)/15.75`) |
| `ringWaveform` | `std::atomic<int>` | `0` | 0-4 carrier waveform |
| `ringStereoSpread` | `std::atomic<float>` | `0.0f` | 0-1 stereo spread |

### 5. Parameter IDs (Modified Enum)

**Location**: `plugins/ruinae/src/plugin_ids.h`

| ID | Value | Description |
|----|-------|-------------|
| `kDistortionRingFreqId` | `560` | Carrier frequency |
| `kDistortionRingFreqModeId` | `561` | Free / NoteTrack selector |
| `kDistortionRingRatioId` | `562` | Carrier-to-note ratio |
| `kDistortionRingWaveformId` | `563` | Carrier waveform selector |
| `kDistortionRingStereoSpreadId` | `564` | Stereo spread amount |

### 6. RuinaeDistortionType Extension (Modified Enum)

**Location**: `plugins/ruinae/src/ruinae_types.h`

```cpp
enum class RuinaeDistortionType : uint8_t {
    Clean = 0,
    ChaosWaveshaper,
    SpectralDistortion,
    GranularDistortion,
    Wavefolder,
    TapeSaturator,
    RingModulator,      // NEW (value 6)
    NumTypes            // Now 7
};
```

## Relationships

```
RuinaeVoice 1---1 RingModulator       (pre-allocated, owned via unique_ptr)
RingModulator 1---1 PolyBlepOscillator  (for Tri/Saw/Square carriers, 2 for stereo)
RingModulator 1---1 NoiseOscillator     (for Noise carrier, 2 for stereo)
RingModulator 1---N OnePoleSmoother     (frequency smoothers, 2 for stereo)
RuinaeDistortionParams 1---5 atomic     (ring mod parameter storage)
```

## Normalization Mappings

| Parameter | Normalized (0-1) | Denormalized | Formula |
|-----------|-------------------|--------------|---------|
| Ring Freq | 0.0 - 1.0 | 0.1 - 20000 Hz | `0.1 * pow(200000.0, normalized)` |
| Ring Freq Mode | discrete 0-1 | Free / NoteTrack | `round(value * 1)` |
| Ring Ratio | 0.0 - 1.0 | 0.25 - 16.0 | `0.25 + normalized * 15.75` (linear) |
| Ring Waveform | discrete 0-4 | Sine/Tri/Saw/Sq/Noise | `round(value * 4)` |
| Ring Stereo Spread | 0.0 - 1.0 | 0.0 - 1.0 | direct |

**Default normalized values:**
- Ring Freq: `log(440/0.1) / log(200000) ~= 0.6882` (maps to 440 Hz)
- Ring Freq Mode: `1.0` (NoteTrack)
- Ring Ratio: `(2.0 - 0.25) / 15.75 ~= 0.1111` (maps to 2.0)
- Ring Waveform: `0.0` (Sine)
- Ring Stereo Spread: `0.0`
