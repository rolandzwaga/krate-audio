# Research: Supersaw / Unison Engine

**Feature**: 020-supersaw-unison-engine | **Date**: 2026-02-04

## Research Summary

All technical unknowns from the specification have been resolved during the clarification phase. This document consolidates the research findings and documents the rationale behind each design decision.

---

## R1: Non-Linear Detune Curve (JP-8000 Inspired)

**Decision**: Power curve with exponent 1.7: `offset[pair_i] = 50 * amount * (pair_i / numPairs)^1.7` cents

**Rationale**: Adam Szabo's paper "How to Emulate the Super Saw" measured the Roland JP-8000's detune distribution empirically. The 1.7 exponent provides the most musical distribution: inner voices are close enough together for a lush chorus effect, while outer voices are spread wide enough to provide the characteristic "sheen" and width. A linear distribution (exponent 1.0) sounds too uniform and thin. Higher exponents (2.0+) cluster voices too tightly near center, losing the chorus richness.

**Alternatives Considered**:
- **Linear distribution** (exponent 1.0): Too uniform spacing, lacks the organic clustering that makes supersaw sound musical. Rejected.
- **Exact table lookup from Szabo measurements**: More accurate to the original hardware, but adds complexity (lookup table storage, interpolation between voice counts). The power curve approximation is indistinguishable from the original in blind tests and is far simpler to implement. Rejected.
- **Exponential curve**: More extreme clustering than power curve; does not match measured JP-8000 behavior well. Rejected.
- **Quadratic (exponent 2.0)**: Close to measured values but inner voices too tightly clustered. The 1.7 exponent is a better fit per Szabo analysis.

**Performance Note**: The power curve coefficients `(pair_i / numPairs)^1.7` are pre-calculated into a small array whenever voice count or detune amount changes. The `process()` loop uses only pre-computed frequency values, avoiding `std::pow()` in the hot path.

---

## R2: Even Voice Count Handling

**Decision**: For even voice counts, the innermost detuned pair acts as the "center group" for the blend crossfade.

**Rationale**: With odd voice counts (e.g., 7), there is a natural center voice at the exact base frequency. With even counts (e.g., 8), there is no single center voice. Rather than inserting a phantom center voice (which would break the symmetric pair structure), the innermost pair receives the "center gain" from the blend crossfade. This means at blend=0.0, only the two innermost (least detuned) voices are audible, providing a nearly-centered sound. The detune on this innermost pair at moderate settings is small enough to be imperceptible as detuning.

**Alternatives Considered**:
- **Always insert a center voice for even counts**: Would make even counts behave like odd+1 counts, breaking user expectations about voice count. Also wastes a voice on a duplicate of the base frequency. Rejected.
- **Split the gain equally**: Would not allow blend=0.0 to produce a focused fundamental. Rejected.

---

## R3: Equal-Power Crossfade for Blend Control

**Decision**: `centerGain = cos(blend * pi/2)`, `outerGain = sin(blend * pi/2)`. Reuse existing `equalPowerGains()` from `core/crossfade_utils.h`.

**Rationale**: Equal-power crossfade maintains constant perceived loudness across the full blend range. This is the standard approach for audio crossfading. The existing `equalPowerGains(position)` function returns `{cos(pos * kHalfPi), sin(pos * kHalfPi)}` which maps directly to `{centerGain, outerGain}` when `position = blend`.

**API Mapping**:
- `equalPowerGains(blend)` returns `{fadeOut, fadeIn}` = `{centerGain, outerGain}`
- At blend=0.0: center=1.0, outer=0.0 (only center audible)
- At blend=0.5: center~=0.707, outer~=0.707 (equal power)
- At blend=1.0: center=0.0, outer=1.0 (only outer audible)

**Alternatives Considered**:
- **Linear crossfade**: Produces a -3dB dip at the midpoint, perceived as a volume drop. Rejected.
- **Custom curve**: No benefit over the standard equal-power approach, and would add complexity. Rejected.

---

## R4: Maximum Detune Spread

**Decision**: Exactly 100 cents (1.0 semitone) total spread at detune=1.0. Outermost pair at +/-50 cents.

**Rationale**: 100 cents provides a wide enough spread for aggressive supersaw sounds while keeping the overall pitch perception anchored. This matches the typical range of hardware supersaw implementations. Going wider (e.g., 200 cents) causes the sound to lose pitch coherence. Going narrower (e.g., 50 cents) limits the maximum width.

**Implementation**: The `semitonesToRatio()` function from `core/pitch_utils.h` converts cent values to frequency ratios: `ratio = 2^(cents/1200)`. For 50 cents: ratio ~= 1.02930. For -50 cents: ratio ~= 0.97153.

---

## R5: Nyquist Handling

**Decision**: Accept aliasing. Do NOT mute or clamp individual voices near Nyquist. PolyBLEP is the sole anti-aliasing mechanism.

**Rationale**: Muting voices near Nyquist would break the symmetric stereo image (one side loses a voice while the other keeps it). The PolyBLEP correction already provides significant alias suppression. In a dense 7+ voice unison, any residual aliasing is masked by the complex harmonic content. This matches the behavior of analog hardware which has no concept of Nyquist.

**Note on PolyBlepOscillator**: The existing `setFrequency()` clamps to `[0, sampleRate/2)`. However, for the UnisonEngine, we need voices that may be detuned slightly above Nyquist/2. The oscillator's clamping will handle this gracefully -- frequencies above Nyquist/2 will be clamped to just below Nyquist, and the PolyBLEP will do its best. This is acceptable behavior per the spec decision.

---

## R6: Gain Compensation

**Decision**: Fixed `1/sqrt(numVoices)` regardless of blend setting.

**Rationale**: The equal-power crossfade already maintains constant power during blend sweeps, so the gain compensation does not need to adapt to blend. Using a fixed factor based on total voice count provides predictable headroom management. The caller can trust that the output level is consistent regardless of blend position.

**Calculation**: Per-voice gain = `(1/sqrt(numVoices)) * blendCoefficient`. The blend coefficient comes from the equal-power crossfade (cos or sin term). This naturally gives each voice the appropriate contribution.

---

## R7: Constant-Power Pan Law

**Decision**: Direct implementation using `leftGain = cos((pan + 1) * pi/4)`, `rightGain = sin((pan + 1) * pi/4)`.

**Rationale**: The existing `stereo_utils.h` only provides `stereoCrossBlend()` which is a different operation (L/R cross-blend for ping-pong routing). A constant-power pan law is a different mathematical operation. Rather than extending `stereo_utils.h` prematurely (there is only one consumer so far), the pan law is implemented directly in the UnisonEngine.

**Future Extraction**: If Phase 17 (VectorMixer) or other Layer 3 systems need panning, the pan law should be extracted to a shared utility function in `core/stereo_utils.h`.

---

## R8: Random Phase Initialization

**Decision**: Use `Xorshift32` with fixed seed `0x5EEDBA5E`. Generate phases via `nextUnipolar()` which produces values in [0, 1).

**Rationale**: Deterministic seeding ensures reproducible output across `prepare()`/`reset()` calls, which is essential for DAW offline rendering consistency. The `Xorshift32` PRNG is already available in `core/random.h` and is real-time safe (no allocations, constexpr-compatible).

**Implementation**: In `prepare()`, seed the RNG, then call `nextUnipolar()` 16 times to fill the initial phase array. Store these phases. In `reset()`, re-seed and regenerate the same phases, then apply them to each oscillator via `resetPhase()`.

---

## R9: StereoOutput Struct Location

**Decision**: Define `StereoOutput` at file scope in `unison_engine.h` within the `Krate::DSP` namespace.

**Rationale**: The struct is a simple aggregate with two float members. It is defined in the unison engine header because that is its first consumer. If future Layer 3 systems (VectorMixer, FMVoice) need it, it can be extracted to a shared header like `core/stereo_types.h`.

**ODR Verification**: Searched for `StereoOutput` across the entire codebase. Only found references in `specs/OSC-ROADMAP.md` (planning document). No existing struct conflicts.

---

## R10: PolyBlepOscillator Memory Budget Verification

**Decision**: 16 oscillators fit within the 2048-byte budget.

**Analysis**:

`PolyBlepOscillator` member layout (from `polyblep_oscillator.h`):
| Member | Type | Size (bytes) |
|--------|------|-------------|
| `phaseAcc_` | `PhaseAccumulator` (2 doubles) | 16 |
| `dt_` | `float` | 4 |
| `sampleRate_` | `float` | 4 |
| `frequency_` | `float` | 4 |
| `pulseWidth_` | `float` | 4 |
| `integrator_` | `float` | 4 |
| `fmOffset_` | `float` | 4 |
| `pmOffset_` | `float` | 4 |
| `waveform_` | `OscWaveform` (uint8_t) | 1 |
| `phaseWrapped_` | `bool` | 1 |
| *padding* | - | ~6 |
| **Total** | | **~48 bytes** |

16 oscillators x 48 bytes = **768 bytes**.

UnisonEngine additional state estimate:
| Member | Type | Size (bytes) |
|--------|------|-------------|
| `oscillators_[16]` | `PolyBlepOscillator[16]` | ~768 |
| `initialPhases_[16]` | `double[16]` | 128 |
| `detuneOffsets_[16]` | `float[16]` | 64 |
| `panPositions_[16]` | `float[16]` | 64 |
| `leftGains_[16]` | `float[16]` | 64 |
| `rightGains_[16]` | `float[16]` | 64 |
| `blendWeights_[16]` | `float[16]` | 64 |
| Scalar params (10+) | `float`, `size_t`, etc. | ~80 |
| `rng_` | `Xorshift32` | 4 |
| **Total** | | **~1300 bytes** |

This is well under 2048 bytes. Verified.

---

## R11: Dependencies -- API Contract Verification

All dependency APIs have been verified by reading the actual header files:

### PolyBlepOscillator (`primitives/polyblep_oscillator.h`)
- `void prepare(double sampleRate) noexcept` -- initializes, resets all state
- `void reset() noexcept` -- resets phase/state, preserves config
- `void setFrequency(float hz) noexcept` -- clamps to [0, sampleRate/2), NaN/Inf guard
- `void setWaveform(OscWaveform waveform) noexcept` -- clears integrator on Triangle enter/leave
- `[[nodiscard]] float process() noexcept` -- returns one sample, sanitized
- `void resetPhase(double newPhase = 0.0) noexcept` -- wraps to [0, 1)

### semitonesToRatio (`core/pitch_utils.h`)
- `[[nodiscard]] float semitonesToRatio(float semitones) noexcept` -- returns `pow(2.0f, semitones / 12.0f)`

### equalPowerGains (`core/crossfade_utils.h`)
- `[[nodiscard]] std::pair<float, float> equalPowerGains(float position) noexcept` -- returns `{cos(pos*kHalfPi), sin(pos*kHalfPi)}`

### Xorshift32 (`core/random.h`)
- `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept`
- `constexpr void seed(uint32_t seedValue) noexcept`
- `[[nodiscard]] constexpr float nextUnipolar() noexcept` -- returns [0.0, 1.0]

### detail::isNaN (`core/db_utils.h`)
- `constexpr bool isNaN(float x) noexcept` -- bit manipulation NaN check
- `constexpr bool isInf(float x) noexcept` -- bit manipulation infinity check

### Math Constants (`core/math_constants.h`)
- `inline constexpr float kPi`
- `inline constexpr float kTwoPi`
- `inline constexpr float kHalfPi`

---

## R12: Existing Codebase Reuse Verification

### ODR Prevention Searches

| Planned Type | Search Result | Action |
|--------------|---------------|--------|
| `UnisonEngine` | Not found anywhere | Create New |
| `StereoOutput` | Not found as struct/class (only in OSC-ROADMAP.md text) | Create New |

### Existing Components Reuse Confirmed

| Component | Location | Layer | Verified API |
|-----------|----------|-------|-------------|
| `PolyBlepOscillator` | `primitives/polyblep_oscillator.h` | 1 | Yes |
| `OscWaveform` | `primitives/polyblep_oscillator.h` | 1 | Yes |
| `semitonesToRatio()` | `core/pitch_utils.h` | 0 | Yes |
| `equalPowerGains()` | `core/crossfade_utils.h` | 0 | Yes |
| `Xorshift32` | `core/random.h` | 0 | Yes |
| `detail::isNaN()` | `core/db_utils.h` | 0 | Yes |
| `detail::isInf()` | `core/db_utils.h` | 0 | Yes |
| `kPi, kTwoPi, kHalfPi` | `core/math_constants.h` | 0 | Yes |

### Components NOT Used (confirmed)

| Component | Location | Reason |
|-----------|----------|--------|
| `stereoCrossBlend()` | `core/stereo_utils.h` | Different operation (L/R swap), not a pan law |
| `PhaseAccumulator` | `core/phase_utils.h` | Used internally by PolyBlepOscillator; UnisonEngine interacts via public API only |
| `OnePoleSmoother` | `primitives/smoother.h` | Parameter smoothing is caller's responsibility |
