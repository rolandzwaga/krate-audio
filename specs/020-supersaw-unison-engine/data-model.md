# Data Model: Supersaw / Unison Engine

**Feature**: 020-supersaw-unison-engine | **Date**: 2026-02-04

---

## Entities

### StereoOutput (Value Type)

**Location**: `dsp/include/krate/dsp/systems/unison_engine.h` (file scope, `Krate::DSP` namespace)
**Purpose**: Lightweight return type for stereo audio sample pairs.

```cpp
struct StereoOutput {
    float left = 0.0f;
    float right = 0.0f;
};
```

**Constraints**:
- Simple aggregate (no user-declared constructors)
- Brace-initializable: `StereoOutput{0.0f, 0.0f}`
- Value semantics, no heap allocation
- 8 bytes total

**Relationships**: Returned by `UnisonEngine::process()`.

---

### UnisonEngine (Layer 3 System)

**Location**: `dsp/include/krate/dsp/systems/unison_engine.h`
**Namespace**: `Krate::DSP`
**Purpose**: Multi-voice detuned oscillator with stereo spread, inspired by Roland JP-8000 supersaw.

#### State Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `oscillators_` | `std::array<PolyBlepOscillator, 16>` | default-constructed | All 16 pre-allocated voice oscillators |
| `initialPhases_` | `std::array<double, 16>` | `{0}` | Deterministic random phases from RNG |
| `detuneOffsets_` | `std::array<float, 16>` | `{0}` | Per-voice detune in semitones |
| `panPositions_` | `std::array<float, 16>` | `{0}` | Per-voice pan position [-1, +1] |
| `leftGains_` | `std::array<float, 16>` | `{0}` | Pre-computed left channel pan gain |
| `rightGains_` | `std::array<float, 16>` | `{0}` | Pre-computed right channel pan gain |
| `blendWeights_` | `std::array<float, 16>` | `{0}` | Pre-computed blend weight per voice |
| `numVoices_` | `size_t` | `1` | Active voice count [1, 16] |
| `detune_` | `float` | `0.0f` | Detune amount [0.0, 1.0] |
| `stereoSpread_` | `float` | `0.0f` | Stereo spread [0.0, 1.0] |
| `blend_` | `float` | `0.5f` | Center vs outer blend [0.0, 1.0] |
| `frequency_` | `float` | `440.0f` | Base frequency in Hz |
| `gainCompensation_` | `float` | `1.0f` | `1/sqrt(numVoices)` |
| `centerGain_` | `float` | `0.707f` | `cos(blend * pi/2)` |
| `outerGain_` | `float` | `0.707f` | `sin(blend * pi/2)` |
| `sampleRate_` | `double` | `0.0` | Stored sample rate |
| `rng_` | `Xorshift32` | seed `0x5EEDBA5E` | Deterministic PRNG for phases |

#### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `kMaxVoices` | `16` | Maximum simultaneous unison voices |
| `kMaxDetuneCents` | `50.0f` | Maximum detune per side in cents |
| `kDetuneExponent` | `1.7f` | Power curve exponent for detune distribution |
| `kPhaseSeed` | `0x5EEDBA5E` | Fixed RNG seed for deterministic phases |

#### Lifecycle Methods

| Method | Signature | RT-Safe | Description |
|--------|-----------|---------|-------------|
| Constructor | `UnisonEngine() noexcept = default` | N/A | Default-constructible |
| `prepare` | `void prepare(double sampleRate) noexcept` | No | Initialize all oscillators, seed RNG, assign phases, reset params |
| `reset` | `void reset() noexcept` | No | Re-seed RNG, regenerate same phases, apply to oscillators |

#### Parameter Setters

| Method | Signature | RT-Safe | Validation | Side Effects |
|--------|-----------|---------|------------|--------------|
| `setNumVoices` | `void setNumVoices(size_t count) noexcept` | Yes | Clamp [1, 16] | Recompute detune, pan, gains |
| `setDetune` | `void setDetune(float amount) noexcept` | Yes | Clamp [0, 1], reject NaN/Inf | Recompute voice frequencies |
| `setStereoSpread` | `void setStereoSpread(float spread) noexcept` | Yes | Clamp [0, 1], reject NaN/Inf | Recompute pan positions |
| `setWaveform` | `void setWaveform(OscWaveform waveform) noexcept` | Yes | N/A | Set all oscillators |
| `setFrequency` | `void setFrequency(float hz) noexcept` | Yes | Reject NaN/Inf | Set all oscillator frequencies |
| `setBlend` | `void setBlend(float blend) noexcept` | Yes | Clamp [0, 1], reject NaN/Inf | Recompute blend gains |

#### Processing Methods

| Method | Signature | RT-Safe | Description |
|--------|-----------|---------|-------------|
| `process` | `[[nodiscard]] StereoOutput process() noexcept` | Yes | Generate one stereo sample |
| `processBlock` | `void processBlock(float* left, float* right, size_t numSamples) noexcept` | Yes | Generate numSamples into buffers |

---

## State Transitions

### Voice Layout Computation (triggered by `setNumVoices`, `setDetune`, `setStereoSpread`, `setBlend`)

```
setNumVoices(N) or setDetune(amount) or setStereoSpread(spread) or setBlend(b)
  |
  v
computeVoiceLayout()
  |
  +-- Determine numPairs = N/2 (floor division)
  +-- Determine hasCenter = (N is odd)
  |
  +-- For each pair i (1..numPairs):
  |     offset = kMaxDetuneCents * detune * pow(i / numPairs, 1.7) / 100.0  [semitones]
  |     voiceUp:   detuneOffsets_[idx] = +offset
  |     voiceDown:  detuneOffsets_[idx] = -offset
  |
  +-- For center voice (if odd): detuneOffsets_[centerIdx] = 0.0
  |
  +-- For each pair i:
  |     panAmount = spread * pow(i / numPairs, 1.0)  [linear pan spread]
  |     voiceUp:   panPositions_[idx] = +panAmount
  |     voiceDown:  panPositions_[idx] = -panAmount
  |
  +-- For center voice: panPositions_[centerIdx] = 0.0
  |
  +-- Compute pan gains from positions:
  |     leftGains_[v]  = cos((pan + 1) * pi/4)
  |     rightGains_[v] = sin((pan + 1) * pi/4)
  |
  +-- Compute blend weights:
  |     auto [cGain, oGain] = equalPowerGains(blend)
  |     Center voice(s): blendWeights_[v] = cGain
  |     Outer voices:    blendWeights_[v] = oGain
  |
  +-- gainCompensation_ = 1.0f / sqrt(numVoices)
  |
  +-- Update all oscillator frequencies:
       oscillators_[v].setFrequency(frequency * semitonesToRatio(detuneOffsets_[v]))
```

### Processing Pipeline (per sample)

```
process()
  |
  v
For each active voice v (0..numVoices_-1):
  |
  +-- sample = oscillators_[v].process()
  +-- weighted = sample * blendWeights_[v] * gainCompensation_
  +-- sumL += weighted * leftGains_[v]
  +-- sumR += weighted * rightGains_[v]
  |
  v
Sanitize output:
  +-- Replace NaN with 0.0 (bit manipulation)
  +-- Clamp to [-2.0, 2.0]
  |
  v
Return StereoOutput{sumL, sumR}
```

---

## Voice Index Layout

### Odd Voice Count (e.g., 7 voices)

```
Index:  0     1     2     3     4     5     6
Role:   P3-   P2-   P1-   C     P1+   P2+   P3+
Detune: -max  -mid  -small 0    +small +mid  +max
Pan:    -wide -mid  -narrow 0   +narrow +mid  +wide
Blend:  outer outer outer  ctr  outer  outer  outer
```

- C = Center voice (exact base frequency)
- P{n}+/- = Pair n, detuned up (+) or down (-)
- Center voice at index `numVoices / 2`

### Even Voice Count (e.g., 8 voices)

```
Index:  0     1     2     3     4     5     6     7
Role:   P4-   P3-   P2-   P1-   P1+   P2+   P3+   P4+
Detune: -max  -     -     -min  +min  +     +     +max
Pan:    -wide -     -     -nar  +nar  +     +     +wide
Blend:  outer outer outer ctr   ctr   outer outer  outer
```

- No single center voice
- Innermost pair (P1-, P1+) receives center blend gain
- P1 has the smallest detune offset (via power curve)

---

## Validation Rules

### Parameter Clamping

| Parameter | Range | Out-of-Range Behavior |
|-----------|-------|----------------------|
| `numVoices` | [1, 16] | Clamped silently |
| `detune` | [0.0, 1.0] | Clamped; NaN/Inf ignored |
| `stereoSpread` | [0.0, 1.0] | Clamped; NaN/Inf ignored |
| `blend` | [0.0, 1.0] | Clamped; NaN/Inf ignored |
| `frequency` | Any float | NaN/Inf ignored (prev retained) |
| `waveform` | `OscWaveform` enum | No validation needed (type-safe) |

### Output Sanitization

Applied to every output sample (both `process()` and `processBlock()`):
1. NaN check via `std::bit_cast<uint32_t>` (works with `-ffast-math`)
2. Replace NaN with 0.0f
3. Clamp to [-2.0f, 2.0f]
