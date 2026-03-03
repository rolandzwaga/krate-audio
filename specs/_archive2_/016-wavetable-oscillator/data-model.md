# Data Model: Wavetable Oscillator with Mipmapping

**Branch**: `016-wavetable-oscillator` | **Date**: 2026-02-04

## Terminology Glossary

| Term | Context | Meaning |
|------|---------|---------|
| **Mipmap** | Graphics term applied to audio | Multiple pre-computed versions of waveform data at progressively reduced bandwidth (fewer harmonics). |
| **Mipmap Level** | Waveform storage | One band-limited version of a waveform. Level 0 = full bandwidth; higher levels = fewer harmonics. |
| **Guard Samples** | Memory layout | Extra samples at table boundaries enabling branchless interpolation. 1 prepend + 3 append = 4 total per level. |
| **Table Size** | Data dimension | Number of actual waveform samples per level (excluding guards). Default: 2048. |
| **Fractional Mipmap Level** | Crossfade control | A float value where the integer part selects two adjacent levels and the fractional part determines the blend ratio. |
| **Phase** | Oscillator state | Position within the waveform cycle, normalized to [0, 1). |
| **Phase Increment** | Per-sample advance | `frequency / sampleRate`, how much phase advances per sample. |

## Entities

### WavetableData (Struct, Layer 0)

**Location**: `dsp/include/krate/dsp/core/wavetable_data.h`
**Namespace**: `Krate::DSP`
**Dependencies**: Standard library only (Layer 0)

#### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| `kDefaultTableSize` | `size_t` | 2048 | Samples per mipmap level (excluding guards) |
| `kMaxMipmapLevels` | `size_t` | 11 | Maximum number of mipmap levels (~11 octaves) |
| `kGuardSamples` | `size_t` | 4 | 1 prepend + 3 append guard samples |

#### Member Variables

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `levels_` | `std::array<std::array<float, kDefaultTableSize + kGuardSamples>, kMaxMipmapLevels>` | zero-initialized | Mipmap level data. Physical layout: [guard0][data0..dataN-1][guard1][guard2][guard3]. |
| `numLevels_` | `size_t` | 0 | Number of populated mipmap levels. 0 = empty/default. |
| `tableSize_` | `size_t` | `kDefaultTableSize` | Number of data samples per level (excluding guards). |

#### Memory Layout (per level)

```
Physical index:  [0]          [1]      [2]      ...  [N]        [N+1]      [N+2]      [N+3]
Logical index:   [-1]         [0]      [1]      ...  [N-1]      [N]        [N+1]      [N+2]
Role:            prepend      data     data     ...  data       append     append     append
                 guard        start    ...           end        guard 0    guard 1    guard 2
Content:         =data[N-1]   actual   actual   ...  actual     =data[0]   =data[1]   =data[2]
```

The `getLevel(i)` method returns a pointer to **logical index 0** (physical index 1, the first data sample), so:
- `p[-1]` = physical[0] = prepend guard = `data[N-1]` (wrap from end)
- `p[0]` through `p[N-1]` = actual waveform data
- `p[N]` = physical[N+1] = append guard 0 = `data[0]` (wrap from start)
- `p[N+1]` = physical[N+2] = append guard 1 = `data[1]`
- `p[N+2]` = physical[N+3] = append guard 2 = `data[2]`

**Key distinction**: "Logical index" is what the oscillator sees via `getLevel()` pointer; "physical index" is the offset within the `std::array` storage.

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| Default constructor | `WavetableData() = default` | Zero-initializes all data, numLevels = 0 |
| `getLevel()` | `const float* getLevel(size_t level) const noexcept` | Returns pointer to data start (index 0) of specified level, or nullptr if out of range |
| `getMutableLevel()` | `float* getMutableLevel(size_t level) noexcept` | Mutable version for generators |
| `tableSize()` | `size_t tableSize() const noexcept` | Returns kDefaultTableSize (2048) |
| `numLevels()` | `size_t numLevels() const noexcept` | Returns number of populated levels |
| `setNumLevels()` | `void setNumLevels(size_t n) noexcept` | Sets the populated level count |

#### Validation Rules

| Property | Constraint | Notes |
|----------|-----------|-------|
| `numLevels_` | [0, kMaxMipmapLevels] | 0 = empty (valid default state) |
| `level` parameter | [0, numLevels_) | getLevel returns nullptr if out of range |
| All sample values | [-1.1, 1.1] after normalization | Generator enforces ~0.96 peak (range [0.95, 0.97]) with small Gibbs overshoot |
| Guard samples | Must match corresponding data samples | Generator sets guards after filling data |

---

### selectMipmapLevel (Free Function, Layer 0)

**Location**: `dsp/include/krate/dsp/core/wavetable_data.h`
**Namespace**: `Krate::DSP`

```cpp
[[nodiscard]] constexpr size_t selectMipmapLevel(
    float frequency,
    float sampleRate,
    size_t tableSize
) noexcept;
```

**Formula**: `max(0, floor(log2(frequency * tableSize / sampleRate)))`, clamped to [0, kMaxMipmapLevels - 1].

**Equivalent form**: `max(0, floor(log2(frequency / fundamental)))` where `fundamental = sampleRate / tableSize`.

**Note**: Since `std::log2` is not constexpr on all compilers, the implementation uses a loop-based approach: compute `fundamental = sampleRate / tableSize`, then count octaves by doubling the fundamental until it exceeds `frequency / 2`.

| Input | Output | Reasoning |
|-------|--------|-----------|
| f=100, sr=44100, ts=2048 | 2 | 100 * 2048 / 44100 = 4.64, log2(4.64) = 2.21, floor = 2 |
| f=10000, sr=44100, ts=2048 | 8 | 10000 * 2048 / 44100 = 464.4, log2(464.4) = 8.86, floor = 8 |
| f=0, sr=44100, ts=2048 | 0 | Zero frequency, log2(0) = -inf, clamped to 0 |
| f=22050, sr=44100, ts=2048 | 10 | 22050 * 2048 / 44100 = 1024, log2(1024) = 10 |
| f<0, sr=44100, ts=2048 | 0 | Negative frequency treated as zero |

---

### selectMipmapLevelFractional (Free Function, Layer 0)

**Location**: `dsp/include/krate/dsp/core/wavetable_data.h`
**Namespace**: `Krate::DSP`

```cpp
[[nodiscard]] inline float selectMipmapLevelFractional(
    float frequency,
    float sampleRate,
    size_t tableSize
) noexcept;
```

**Formula**: `max(0.0f, log2f(frequency * tableSize / sampleRate))`, clamped to [0.0f, kMaxMipmapLevels - 1.0f].

**Equivalent form**: `max(0.0f, log2f(frequency / fundamental))` where `fundamental = sampleRate / tableSize`.

Returns a float for crossfade blending. The integer part selects two adjacent levels; the fractional part is the blend ratio.

---

### Wavetable Generator Functions (Layer 1)

**Location**: `dsp/include/krate/dsp/primitives/wavetable_generator.h`
**Namespace**: `Krate::DSP`
**Dependencies**: `core/wavetable_data.h`, `core/math_constants.h`, `primitives/fft.h`

All generator functions follow the same pattern:

1. Create a temporary FFT instance and prepare it for `tableSize`
2. For each mipmap level 0..kMaxMipmapLevels-1:
   a. Compute `maxHarmonic` for this level
   b. Set harmonic amplitudes in frequency domain (Complex spectrum)
   c. IFFT to produce time-domain samples
   d. Normalize the level independently (peak -> 0.96)
   e. Copy normalized samples to WavetableData level
   f. Set guard samples
3. Set `data.numLevels` to kMaxMipmapLevels

#### Function Signatures

| Function | Signature | Description |
|----------|-----------|-------------|
| `generateMipmappedSaw` | `void generateMipmappedSaw(WavetableData& data)` | Sawtooth: all harmonics, amplitude 1/n |
| `generateMipmappedSquare` | `void generateMipmappedSquare(WavetableData& data)` | Square: odd harmonics only, amplitude 1/n |
| `generateMipmappedTriangle` | `void generateMipmappedTriangle(WavetableData& data)` | Triangle: odd harmonics, amplitude 1/n^2, alternating sign |
| `generateMipmappedFromHarmonics` | `void generateMipmappedFromHarmonics(WavetableData& data, const float* harmonicAmplitudes, size_t numHarmonics)` | Custom spectrum |
| `generateMipmappedFromSamples` | `void generateMipmappedFromSamples(WavetableData& data, const float* samples, size_t sampleCount)` | From raw waveform via FFT analysis |

#### Harmonic Content per Level

| Level | Max Harmonic (tableSize=2048) | Bandwidth |
|-------|-------------------------------|-----------|
| 0 | 1024 | Full |
| 1 | 512 | Half |
| 2 | 256 | Quarter |
| 3 | 128 | 1/8 |
| 4 | 64 | 1/16 |
| 5 | 32 | 1/32 |
| 6 | 16 | 1/64 |
| 7 | 8 | 1/128 |
| 8 | 4 | 1/256 |
| 9 | 2 | 1/512 |
| 10 | 1 | Fundamental only (sine) |

---

### WavetableOscillator (Class, Layer 1)

**Location**: `dsp/include/krate/dsp/primitives/wavetable_oscillator.h`
**Layer**: 1 (primitives/)
**Namespace**: `Krate::DSP`
**Dependencies**: Layer 0 only (`core/wavetable_data.h`, `core/interpolation.h`, `core/phase_utils.h`, `core/math_constants.h`, `core/db_utils.h`)

#### Member Variables (Internal State)

Listed in cache-friendly order (hot-path data first):

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `phaseAcc_` | `PhaseAccumulator` | `{0.0, 0.0}` | Phase state (phase + increment). Direct modification of `phase`/`increment` members is discouraged; use `setFrequency()` and `resetPhase()` instead. |
| `sampleRate_` | `float` | `0.0f` | Current sample rate |
| `frequency_` | `float` | `440.0f` | Base frequency in Hz |
| `fmOffset_` | `float` | `0.0f` | FM offset in Hz (reset each sample) |
| `pmOffset_` | `float` | `0.0f` | PM offset in radians (reset each sample) |
| `table_` | `const WavetableData*` | `nullptr` | Non-owning pointer to wavetable data |
| `phaseWrapped_` | `bool` | `false` | Phase wrapped on last process() |

**Total size**: ~48 bytes (compact, fits in one cache line)

#### Methods

**Lifecycle**:

| Method | Signature | RT-Safe | Description |
|--------|-----------|---------|-------------|
| Constructor | `WavetableOscillator() noexcept = default` | Yes | Default state, sampleRate=0 |
| `prepare` | `void prepare(double sampleRate) noexcept` | No | Initialize for sample rate, reset all state |
| `reset` | `void reset() noexcept` | Yes | Reset phase/modulation, preserve config |

**Parameter Setters**:

| Method | Signature | RT-Safe | Description |
|--------|-----------|---------|-------------|
| `setWavetable` | `void setWavetable(const WavetableData* table) noexcept` | Yes | Set non-owning pointer (nullptr = silence) |
| `setFrequency` | `void setFrequency(float hz) noexcept` | Yes | Set frequency, clamped to [0, sr/2) |

**Processing**:

| Method | Signature | RT-Safe | Description |
|--------|-----------|---------|-------------|
| `process` | `[[nodiscard]] float process() noexcept` | Yes | Generate one sample |
| `processBlock` | `void processBlock(float* output, size_t numSamples) noexcept` | Yes | Constant frequency block |
| `processBlock` (FM) | `void processBlock(float* output, const float* fmBuffer, size_t numSamples) noexcept` | Yes | Per-sample FM block |

**Phase Interface** (matches PolyBlepOscillator):

| Method | Signature | RT-Safe | Description |
|--------|-----------|---------|-------------|
| `phase` | `[[nodiscard]] double phase() const noexcept` | Yes | Current phase [0, 1) |
| `phaseWrapped` | `[[nodiscard]] bool phaseWrapped() const noexcept` | Yes | Last process() wrapped? |
| `resetPhase` | `void resetPhase(double newPhase = 0.0) noexcept` | Yes | Force phase, wrapped to [0, 1) |

**Modulation**:

| Method | Signature | RT-Safe | Description |
|--------|-----------|---------|-------------|
| `setPhaseModulation` | `void setPhaseModulation(float radians) noexcept` | Yes | PM offset (per-sample, non-accumulating) |
| `setFrequencyModulation` | `void setFrequencyModulation(float hz) noexcept` | Yes | FM offset (per-sample, non-accumulating) |

#### State Transitions

```
                 prepare(sr)
    [Uninitialized] ---------> [Ready]
                                  |
                    reset()       |  process() / processBlock()
                   <-----------  [Processing]
                                  |
                   setFrequency() | setWavetable() | setPhaseModulation()
                   setFrequencyModulation() | resetPhase()
                                  |
                              [Processing] (parameters take effect on next process())
```

#### Per-Sample Processing Flow (`process()`)

```
1. If table_ is nullptr: return 0.0f
2. Compute effective frequency: effectiveFreq = clamp(frequency_ + fmOffset_, 0, sr/2)
3. Guard NaN/Inf in effective frequency -> 0.0f
4. Compute effective phase with PM: effectivePhase = wrapPhase(phase + pmOffset / kTwoPi)
5. Compute fractional mipmap level: fracLevel = selectMipmapLevelFractional(effectiveFreq, sr, tableSize)
6. Determine if crossfade needed:
   a. intLevel = floor(fracLevel), frac = fracLevel - intLevel
   b. If frac < 0.05 or frac > 0.95 or intLevel >= numLevels-1:
      - Single lookup at round(fracLevel) clamped
   c. Else: two lookups, blend with linearInterpolate
7. For each lookup:
   a. Map effectivePhase [0,1) to table index: tablePhase = effectivePhase * tableSize
   b. intPhase = floor(tablePhase), fracPhase = tablePhase - intPhase
   c. Get level pointer: p = table_->getLevel(level) + intPhase
   d. sample = cubicHermiteInterpolate(p[-1], p[0], p[1], p[2], fracPhase)
8. Advance phase: phaseWrapped_ = phaseAcc_.advance()
9. Reset modulation offsets: fmOffset_ = 0, pmOffset_ = 0
10. Sanitize output (NaN/range check)
11. Return sanitized sample
```

#### Validation Rules

| Parameter | Range | Clamping | Notes |
|-----------|-------|----------|-------|
| `frequency` | [0, sampleRate/2) | Silent clamp | FR-033 |
| `table` | any pointer | nullptr produces silence | FR-032 |
| `pmOffset` | any float | No clamp, converted by /kTwoPi | FR-042 |
| `fmOffset` | any float | Effective freq clamped to [0, sr/2) | FR-043 |
| `newPhase` (resetPhase) | any double | Wrapped to [0, 1) | FR-041 |
| Output | [-2.0, 2.0] | Clamped, NaN -> 0.0 | FR-051 |

---

## Dependency Contracts

### From `core/interpolation.h` (Interpolation namespace)

```cpp
[[nodiscard]] constexpr float cubicHermiteInterpolate(
    float ym1, float y0, float y1, float y2, float t
) noexcept;
// ym1: sample before y0
// y0, y1: samples bracketing the interpolation point
// y2: sample after y1
// t: fractional position in [0, 1] between y0 and y1
// Returns: interpolated value

[[nodiscard]] constexpr float linearInterpolate(
    float y0, float y1, float t
) noexcept;
// y0, y1: two samples to interpolate between
// t: fractional position [0, 1]
// Returns: y0 + t * (y1 - y0)
```

### From `core/phase_utils.h`

```cpp
struct PhaseAccumulator {
    double phase = 0.0;
    double increment = 0.0;
    [[nodiscard]] bool advance() noexcept;  // Returns true on wrap
    void reset() noexcept;                   // Resets phase to 0
    void setFrequency(float frequency, float sampleRate) noexcept;
};

[[nodiscard]] constexpr double wrapPhase(double phase) noexcept;
```

### From `primitives/fft.h`

```cpp
struct Complex {
    float real = 0.0f;
    float imag = 0.0f;
    // arithmetic operators, magnitude(), phase()
};

class FFT {
    void prepare(size_t fftSize) noexcept;
    void forward(const float* input, Complex* output) noexcept;
    void inverse(const Complex* input, float* output) noexcept;
    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] size_t numBins() const noexcept;  // = size/2 + 1
    [[nodiscard]] bool isPrepared() const noexcept;
};
```
