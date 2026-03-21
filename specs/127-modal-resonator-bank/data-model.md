# Data Model: Modal Resonator Bank

**Date**: 2026-03-21 | **Spec**: 127

## Entities

### ModalResonatorBank (NEW -- Layer 2 processor)

**Location**: `dsp/include/krate/dsp/processors/modal_resonator_bank.h`
**Namespace**: `Krate::DSP`

A bank of up to 96 parallel damped coupled-form resonators. Each mode is independently parameterized by frequency, amplitude, and decay rate. Uses SoA memory layout with 32-byte alignment for SIMD processing.

| Field | Type | Description |
|-------|------|-------------|
| `sinState_` | `alignas(32) float[kMaxModes]` | Coupled-form sin state per mode |
| `cosState_` | `alignas(32) float[kMaxModes]` | Coupled-form cos state per mode |
| `epsilon_` | `alignas(32) float[kMaxModes]` | Frequency coefficient: `2 * sin(pi * f / sr)` |
| `epsilonTarget_` | `alignas(32) float[kMaxModes]` | Target epsilon for smoothing |
| `radius_` | `alignas(32) float[kMaxModes]` | Decay coefficient R: `exp(-decayRate / sr)` |
| `radiusTarget_` | `alignas(32) float[kMaxModes]` | Target R for smoothing |
| `inputGain_` | `alignas(32) float[kMaxModes]` | Per-mode input gain: `amplitude * (1 - R)` |
| `inputGainTarget_` | `alignas(32) float[kMaxModes]` | Target input gain for smoothing |
| `active_` | `std::array<bool, kMaxModes>` | Per-mode active flag (Nyquist/amplitude cull) |
| `numActiveModes_` | `int` | Count of active (non-culled) modes |
| `numModes_` | `int` | Requested mode count (from Partial Count param) |
| `sampleRate_` | `float` | Current sample rate |
| `smoothCoeff_` | `float` | Smoothing coefficient for ~2ms one-pole |
| `envelopeState_` | `float` | Transient emphasis envelope follower state |
| `previousEnvelope_` | `float` | Previous envelope value (for derivative) |

**Constants (private, inside class)**:
| Constant | Value | Description |
|----------|-------|-------------|
| `kMaxModes` | `96` | Maximum resonator modes (matches `kMaxPartials`) |
| `kTransientEmphasisGain` | `4.0f` | Voicing: excitation transient boost factor |
| `kMaxB3` | `4.0e-5f` | Voicing: Chaigne-Lambourg max quadratic damping |
| `kSilenceThreshold` | `1e-12f` | Denormal protection threshold (~-120 dB) |
| `kNyquistGuard` | `0.49f` | Nyquist culling factor |
| `kAmplitudeThresholdLinear` | `0.0001f` | Amplitude culling threshold (linear, ~-80 dB) |
| `kEnvelopeAttackMs` | `5.0f` | Envelope follower attack time in ms (used at prepare-time to compute `envelopeAttackCoeff_`) |
| `kSmoothingTimeMs` | `2.0f` | Coefficient smoothing time constant |

**Key Methods**:

```cpp
void prepare(double sampleRate) noexcept;
void reset() noexcept;

// Configure modes from analyzed harmonic frame
void setModes(
    const float* frequencies,   // partial frequencies in Hz
    const float* amplitudes,    // linear amplitudes
    int numPartials,            // count [0, 96]
    float decayTime,            // base decay in seconds
    float brightness,           // 0-1, HF damping control
    float stretch,              // 0-1, stiff-string inharmonicity
    float scatter               // 0-1, irregular mode displacement
) noexcept;

// Update coefficients for frame transitions (no state clear)
void updateModes(
    const float* frequencies,
    const float* amplitudes,
    int numPartials,
    float decayTime,
    float brightness,
    float stretch,
    float scatter
) noexcept;

// Process a single sample through the resonator bank
float processSample(float excitation) noexcept;

// Process a block of samples
void processBlock(const float* input, float* output, int numSamples) noexcept;

// Check denormals and zero silent modes (called once per block)
void flushSilentModes() noexcept;

// Getters
int getNumActiveModes() const noexcept;
bool isPrepared() const noexcept;
```

**Validation Rules**:
- Frequencies must be > 0 and < 0.49 * sampleRate (Nyquist guard)
- Amplitudes below -80 dB (linear ~0.0001) are culled
- Mode count clamped to [0, kMaxModes]
- decayTime clamped to [0.01, 5.0] seconds
- brightness clamped to [0.0, 1.0]
- stretch clamped to [0.0, 1.0]
- scatter clamped to [0.0, 1.0]

**State Transitions**:
1. `Unprepared` -- initial state
2. `Prepared` -- after `prepare()`, ready for processing
3. `Active` -- after `setModes()`, modes configured and producing output
4. `Silent` -- all modes culled or decayed below threshold

---

### PhysicalModelMixer (NEW -- Innexus plugin-local DSP)

**Location**: `plugins/innexus/src/dsp/physical_model_mixer.h`
**Namespace**: `Innexus`

A simple crossfader that blends the existing path (harmonic + residual) with the physical path (harmonic + physical model).

| Field | Type | Description |
|-------|------|-------------|
| (stateless) | | All computation is inline per-sample |

**Key Methods**:
```cpp
// Mix harmonic, residual, and physical signals
// Returns: harmonicSignal + (1 - mix) * residualSignal + mix * physicalSignal
static float process(
    float harmonicSignal,
    float residualSignal,
    float physicalSignal,
    float mix
) noexcept;
```

**Note**: The mixer is stateless. It is a static utility function. The `dry = harmonic + residual`, `wet = harmonic + physical`, `output = lerp(dry, wet, mix)` formula simplifies to the above because the harmonic signal is present in both dry and wet paths.

---

### InnexusVoice (EXTEND -- add fields)

**Location**: `plugins/innexus/src/processor/innexus_voice.h`

New fields to add:

| Field | Type | Description |
|-------|------|-------------|
| `modalResonator` | `Krate::DSP::ModalResonatorBank` | Per-voice modal resonator bank |

The `prepare()` and `reset()` methods must be updated to include the new field.

---

### New Parameters

**Location**: `plugins/innexus/src/plugin_ids.h`

| Parameter ID | Name | Range | Default | Mapping | Storage |
|-------------|------|-------|---------|---------|---------|
| `kPhysModelMixId = 800` | Physical Model Mix | 0.0-1.0 | 0.0 | Linear | `std::atomic<float>` |
| `kResonanceDecayId = 801` | Decay | 0.01-5.0s | 0.5s (normalized default ≈ 0.5654; derived from `log(50)/log(500)` per mapping `0.01 * 500^n`) | Log | `std::atomic<float>` |
| `kResonanceBrightnessId = 802` | Brightness | 0.0-1.0 | 0.5 | Linear | `std::atomic<float>` |
| `kResonanceStretchId = 803` | Stretch | 0.0-1.0 | 0.0 | Linear | `std::atomic<float>` |
| `kResonanceScatterId = 804` | Scatter | 0.0-1.0 | 0.0 | Linear | `std::atomic<float>` |

---

## Relationships

```
HarmonicFrame (existing, read-only)
    |
    |--- frequencies[], amplitudes[] ---> ModalResonatorBank.setModes()/updateModes()
    |
    +--- used by OscillatorBank (unchanged)

ResidualSynthesizer (existing, read-only)
    |
    +--- output ---> transient emphasis ---> ModalResonatorBank.processSample(excitation)

ModalResonatorBank (new)
    |
    +--- output ---> PhysicalModelMixer.process()

PhysicalModelMixer (new)
    |
    +--- blends harmonic + residual + physical ---> voice output

InnexusVoice (extended)
    |
    +--- owns one ModalResonatorBank instance per voice
```

## Data Flow Per Sample (Inside Voice Render Loop)

```
1. vL, vR = oscillatorBank.processStereo()        // existing: harmonic signal
2. residualSample = residualSynth.process()        // existing: residual signal
3. physicalSample = modalResonator.processSample(residualSample)  // NEW: modal synthesis
   // processSample() internally: applies transient emphasis to residualSample,
   // runs the coupled-form resonator inner loop, then applies the soft-clip safety
   // limiter (scale by kSoftClipThreshold, clip, scale back).
   // The returned value is already transient-emphasized, resonated, and clipped.
4. mono = PhysicalModelMixer::process(
       (vL+vR)*0.5f * harmLevel,                   // harmonic mono component
       residualSample * resLevel,                    // residual component (unscaled raw residual)
       physicalSample,                               // physical model component (already clipped)
       physModelMix                                  // mix parameter
   )
5. Apply existing voice processing (freeze recovery, anti-click, velocity, expression, ADSR, release)
```

Note: The physical model replaces the residual path, not the harmonic path. The harmonic oscillator bank signal passes through unchanged at all mix levels.
