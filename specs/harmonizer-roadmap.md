# Harmonizer Effect Development Roadmap

**Status**: In Progress (Phase 1, Phase 2A, Phase 2B complete) | **Created**: 2026-02-17 | **Source**: [DSP-HARMONIZER-RESEARCH.md](DSP-HARMONIZER-RESEARCH.md)

A comprehensive, dependency-ordered development roadmap for the Harmonizer effect in the KrateDSP shared library. Every phase maps directly to existing codebase building blocks, identifies gaps, and provides implementation-level detail.

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Existing Building Block Inventory](#existing-building-block-inventory)
3. [Gap Analysis](#gap-analysis)
4. [Phase 1: Scale & Interval Foundation](#phase-1-scale--interval-foundation)
5. [Phase 2: Phase Vocoder Quality Improvements](#phase-2-phase-vocoder-quality-improvements)
6. [Phase 3: Pitch Tracking Robustness](#phase-3-pitch-tracking-robustness)
7. [Phase 4: Multi-Voice Harmonizer Engine](#phase-4-multi-voice-harmonizer-engine)
8. [Phase 5: SIMD Optimization](#phase-5-simd-optimization)
9. [Dependency Graph](#dependency-graph)
10. [Risk Analysis](#risk-analysis)

---

## Executive Summary

A **harmonizer** generates one or more pitch-shifted copies of an input signal, blending them with the original to create musical harmonies (Eventide, 1975). Unlike a simple pitch shifter that applies a fixed chromatic interval (e.g., always +4 semitones), a harmonizer applies a *variable* interval that changes depending on the input note to maintain scale-correctness -- "a 3rd above" means +3 semitones on some notes and +4 on others.

The KrateDSP library already provides **~80% of the required DSP components** for a production-quality harmonizer. The remaining work falls into three categories:

1. **Musical intelligence** (diatonic interval computation, pitch tracking): ~25% of remaining effort
2. **Quality improvements** (phase locking, transient detection): ~25% of remaining effort
3. **Orchestration** (multi-voice engine with harmony modes): ~30% of remaining effort
4. **SIMD optimization** (vectorized spectral math): ~20% of remaining effort

### Critical Path

```
Phase 1: ScaleHarmonizer (L0) ✅ ────────┐
                                          │
Phase 2A: Identity Phase Locking (L2) ✅ ┤
                                          ├──► Phase 4: HarmonizerEngine (L3)
Phase 2B: Spectral Transient Det. (L1) ✅ ┤
                                          │
Phase 3: PitchTracker (L1) ──────────────┘
                                               Phase 5: SIMD (independent)
```

---

## Existing Building Block Inventory

### Verified Available Components (Cross-Referenced Against Codebase)

Every component below has been verified to exist in the codebase with its exact file path, public API, and layer.

#### Pitch Shifting (Core Pipeline)

| Component | File | Layer | Key API | Harmonizer Role |
|-----------|------|-------|---------|-----------------|
| **PitchShiftProcessor** | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` | 2 | `setSemitones()`, `setCents()`, `setMode()`, `process()` | **Core**: per-voice pitch shifting (4 modes: Simple, Granular, PitchSync, PhaseVocoder) |
| **FormantPreserver** | `dsp/include/krate/dsp/processors/formant_preserver.h` | 2 | `extractEnvelope()`, `applyFormantPreservation()` | Cepstral formant correction for vocal naturalness (quefrency-domain liftering) |
| **PitchDetector** | `dsp/include/krate/dsp/primitives/pitch_detector.h` | 1 | `pushBlock()`, `detect()`, `getDetectedFrequency()`, `getConfidence()` | Input pitch detection for diatonic harmony (autocorrelation, 256-sample window) |

#### Pitch Utilities

| Component | File | Layer | Key API | Harmonizer Role |
|-----------|------|-------|---------|-----------------|
| `semitonesToRatio()` | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | `2^(semitones/12)` | Semitone-to-ratio conversion for pitch shifter |
| `ratioToSemitones()` | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | `12 * log2(ratio)` | Ratio-to-semitone conversion |
| `frequencyToMidiNote()` | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | `12 * log2(hz/440) + 69` | Frequency-to-MIDI for interval calculation |
| `frequencyToNoteClass()` | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | `midiNote % 12` → 0-11 | Note class extraction (C=0, B=11) |
| `frequencyToCentsDeviation()` | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | `-50` to `+50` cents | Cents deviation for hysteresis |
| `quantizePitch()` | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | Off/Semitones/Octaves/Fifths/Scale | Quantization to scale degrees (**major only, hardcoded root=0**) |

#### Spectral Processing

| Component | File | Layer | Key API | Harmonizer Role |
|-----------|------|-------|---------|-----------------|
| **FFT** | `dsp/include/krate/dsp/primitives/fft.h` | 1 | `forward()`, `inverse()` | pffft-backed SIMD FFT/IFFT (already ~4x via SSE radix-4 butterflies) |
| **STFT + OverlapAdd** | `dsp/include/krate/dsp/primitives/stft.h` | 1 | `analyze()`, `synthesize()` | Complete STFT analysis/synthesis chain |
| **SpectralBuffer** | `dsp/include/krate/dsp/primitives/spectral_buffer.h` | 1 | `getMagnitude()`, `getPhase()`, `setCartesian()`, `data()` | Dual Cartesian/polar with lazy conversion (Highway SIMD) |
| `wrapPhase()` | `dsp/include/krate/dsp/primitives/spectral_utils.h` | 1 | Wraps to `[-pi, pi]` | Phase vocoder phase handling (scalar while-loop based) |
| `phaseDifference()` | `dsp/include/krate/dsp/primitives/spectral_utils.h` | 1 | `currentPhase - previousPhase` | Instantaneous frequency estimation |
| `binToFrequency()` | `dsp/include/krate/dsp/primitives/spectral_utils.h` | 1 | `bin * sampleRate / fftSize` | Bin-frequency conversion |

#### Multi-Voice & Mixing

| Component | File | Layer | Key API | Harmonizer Role |
|-----------|------|-------|---------|-----------------|
| **UnisonEngine** | `dsp/include/krate/dsp/systems/unison_engine.h` | 3 | `setNumVoices()`, `setDetune()`, `setStereoSpread()`, `processBlock()` | Reference pattern for multi-voice processing (16 voices, constant-power pan, gain comp) |
| **OnePoleSmoother** | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | `process()`, `setTargetValue()` | Click-free parameter automation (pitch shift smoothing) |
| **LinearRamp** | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | `process()`, `setTarget()` | Linear parameter transitions |
| **SlewLimiter** | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | `process()`, `setMaxRate()` | Rate-limited parameter changes |
| **DelayLine** | `dsp/include/krate/dsp/primitives/delay_line.h` | 1 | `write()`, `read()`, `setDelay()` | Per-voice onset delay offset |
| **StereoField** | `dsp/include/krate/dsp/systems/stereo_field.h` | 3 | `setWidth()`, `setPan()`, `process()` | Stereo width processing |

#### Existing Pitch-Shifting Effects

| Component | File | Layer | Key API | Harmonizer Role |
|-----------|------|-------|---------|-----------------|
| **ShimmerDelay** | `dsp/include/krate/dsp/effects/shimmer_delay.h` | 4 | `setPitchSemitones()`, `setPitchMode()`, `process()` | Reference for pitch-shift-in-feedback-loop pattern (uses PitchSync mode by default) |

#### Modulation & Transient Detection

| Component | File | Layer | Key API | Harmonizer Role |
|-----------|------|-------|---------|-----------------|
| **TransientDetector** | `dsp/include/krate/dsp/processors/transient_detector.h` | 2 | `process()`, `getCurrentValue()`, `setSensitivity()` | **Time-domain** transient detection (envelope derivative analysis, modulation source). NOT suitable for phase vocoder spectral transient detection. |
| **Window** | `dsp/include/krate/dsp/core/window_functions.h` | 0 | `generate()` (Hann, Hamming, Blackman, Kaiser) | STFT windowing, COLA-compatible |

---

## Gap Analysis

### Components Already Spec'd but Confirmed Missing

| # | Component | Layer | Complexity | Blocked By | Harmonizer Role | Status |
|---|-----------|-------|-----------|------------|-----------------|--------|
| 1 | **ScaleHarmonizer** | 0 | LOW | Nothing | Diatonic interval calculation (core musical intelligence) | **COMPLETE** (spec 060, `scale_harmonizer.h`) |
| 2 | **PitchTracker** | 1 | LOW | Nothing | Smoothed pitch detection with hysteresis & confidence gating | **MISSING** (`PitchDetector` exists but output is raw/jittery, no smoothing or note-hold logic) |
| 3 | **Identity Phase Locking** | 2 | LOW-MED | Nothing | Laroche-Dolson phase locking for `PhaseVocoderPitchShifter` | **COMPLETE** (spec 061, integrated into `PhaseVocoderPitchShifter`) |
| 4 | **SpectralTransientDetector** | 1 | LOW-MED | Nothing | Spectral flux transient detection + phase reset for phase vocoder | **COMPLETE** (spec 062, `spectral_transient_detector.h` + phase reset integrated into `PhaseVocoderPitchShifter`) |
| 5 | **HarmonizerEngine** | 3 | MODERATE | 1, 2, 3, 4 | Multi-voice orchestration with harmony modes | **MISSING** |
| 6 | **SIMD Math Header** | 0 | MODERATE | Nothing | Vectorized `atan2`, `sincos`, `log`, `exp` for spectral pipeline | **MISSING** |

### Components Already Available (No Gap)

| Spec'd Component | Existing Equivalent | Notes |
|-----------------|-------------------|-------|
| Pitch shifting (4 modes) | `PitchShiftProcessor` (L2) | Simple, Granular, PitchSync, PhaseVocoder all verified |
| Formant preservation | `FormantPreserver` (L2) | Cepstral method, configurable quefrency (0.5-5ms) |
| Pitch detection | `PitchDetector` (L1) | Autocorrelation, 256-sample window, confidence output |
| Semitone/ratio conversion | `pitch_utils.h` (L0) | Standard `2^(st/12)` formula |
| FFT/IFFT (SIMD) | `FFT` via pffft (L1) | Already ~4x speedup via SSE radix-4 |
| STFT analysis/synthesis | `STFT` + `OverlapAdd` (L1) | Complete chain with configurable hop/window |
| Spectral buffer | `SpectralBuffer` (L1) | Lazy dual Cartesian/polar (Highway SIMD) |
| Window functions | `Window` (L0) | Hann, Hamming, Blackman, Kaiser |
| Parameter smoothing | `OnePoleSmoother` et al. (L1) | Exponential, linear ramp, slew limiter |
| Delay lines | `DelayLine` (L1) | Circular, linear/allpass interpolation |
| Stereo processing | `StereoField` (L3) | Width, M/S processing |
| Shimmer effect | `ShimmerDelay` (L4) | Pitch shift in feedback loop, reference architecture |
| Diatonic intervals | `ScaleHarmonizer` (L0) | 8 diatonic scales + chromatic, any key, O(1) constexpr lookup (spec 060) |

### Components Partially Available

| Need | What Exists | Gap |
|------|------------|-----|
| ~~Diatonic interval lookup~~ | ~~`quantizePitch()` snaps to scale degrees~~ | **RESOLVED**: `ScaleHarmonizer` (spec 060) provides full diatonic interval computation for 8 scales + chromatic, any key. |
| Robust pitch tracking | `PitchDetector` provides raw pitch | No median filtering, hysteresis, confidence gating, or minimum note duration |
| ~~Phase-locked pitch shifting~~ | ~~`PhaseVocoderPitchShifter` does phase vocoder~~ | **RESOLVED**: Identity phase locking (Laroche-Dolson 1999) implemented in spec 061 with peak detection, region-of-influence assignment, and phase-locked propagation. |
| ~~Transient-aware pitch shifting~~ | ~~`TransientDetector` detects transients~~ | **RESOLVED**: `SpectralTransientDetector` (spec 062) provides spectral flux onset detection with phase reset integration in `PhaseVocoderPitchShifter`. |

---

## Phase 1: Scale & Interval Foundation -- COMPLETE

**Layer**: 0 (Core)
**Blocks**: Phase 4 (HarmonizerEngine depends on interval calculation)
**Effort**: ~1-2 days
**Depends On**: Nothing (can start immediately)
**Status**: Complete -- implemented in spec [060-scale-interval-foundation](060-scale-interval-foundation/spec.md), merged to main.

### Why This Exists

The `ScaleHarmonizer` is the **musical intelligence core** of the harmonizer. It answers the question: "Given input note D, key of C major, harmony = 3rd above, what is the output note?" Answer: F (+3 semitones, a minor 3rd). This variable-interval logic is what distinguishes a harmonizer from a simple pitch shifter (research doc Section 4.1).

The existing `quantizePitch()` in `pitch_utils.h` can snap a value to the nearest major scale degree, but it does NOT compute diatonic intervals. It only supports major scale with hardcoded root=C.

### Existing Building Blocks to Compose

| Component | Role in ScaleHarmonizer |
|-----------|------------------------|
| `frequencyToMidiNote()` (L0) | Convert detected Hz to continuous MIDI note |
| `frequencyToNoteClass()` (L0) | Extract pitch class (0-11) from frequency |
| `semitonesToRatio()` (L0) | Convert computed semitone shift to playback ratio |

### API Design

```cpp
namespace Krate::DSP {

/// Scale types for diatonic harmonization
enum class ScaleType : uint8_t {
    Major = 0,           ///< W-W-H-W-W-W-H  (Ionian)
    NaturalMinor = 1,    ///< W-H-W-W-H-W-W  (Aeolian)
    HarmonicMinor = 2,   ///< W-H-W-W-H-WH-H
    MelodicMinor = 3,    ///< W-H-W-W-W-W-H  (ascending)
    Dorian = 4,          ///< W-H-W-W-W-H-W
    Mixolydian = 5,      ///< W-W-H-W-W-H-W
    Phrygian = 6,        ///< H-W-W-W-H-W-W
    Lydian = 7,          ///< W-W-W-H-W-W-H
    Chromatic = 8,       ///< All 12 semitones (fixed shift, no diatonic logic)
};

/// Result of a diatonic interval calculation
struct DiatonicInterval {
    int semitones;       ///< Actual semitone shift (e.g., +3 or +4)
    int targetNote;      ///< Absolute MIDI note of target (0-127)
    int scaleDegree;     ///< Target scale degree (0-6)
    int octaveOffset;    ///< Number of complete octaves traversed by the diatonic interval
};

/// @brief Diatonic interval calculator for harmonizer intelligence (Layer 0).
///
/// Given a key, scale type, input note, and desired diatonic interval,
/// computes the correct semitone shift. The shift varies per input note
/// to maintain scale-correctness (e.g., "3rd above" = +3 or +4 semitones
/// depending on the scale degree).
///
/// @par Thread Safety
/// Immutable after set*(); safe to call from audio thread.
///
/// @par Real-Time Safety
/// All methods are noexcept, no allocations.
class ScaleHarmonizer {
public:
    void setKey(int rootNote) noexcept;           // 0=C, 1=C#, ..., 11=B
    void setScale(ScaleType type) noexcept;

    [[nodiscard]] int getKey() const noexcept;
    [[nodiscard]] ScaleType getScale() const noexcept;

    /// Core: compute diatonic interval for input MIDI note
    /// @param inputMidiNote MIDI note number (0-127)
    /// @param diatonicSteps Scale degrees to shift (+2 = "3rd above", -2 = "3rd below")
    [[nodiscard]] DiatonicInterval calculate(int inputMidiNote, int diatonicSteps) const noexcept;

    /// Convenience: compute semitone shift from frequency
    [[nodiscard]] float getSemitoneShift(float inputFrequencyHz, int diatonicSteps) const noexcept;

    /// Query: get scale degree of a MIDI note (-1 if not in scale)
    [[nodiscard]] int getScaleDegree(int midiNote) const noexcept;

    /// Query: quantize MIDI note to nearest scale degree
    [[nodiscard]] int quantizeToScale(int midiNote) const noexcept;

    /// Static: get semitone offsets for a scale type
    [[nodiscard]] static constexpr std::array<int, 7> getScaleIntervals(ScaleType type) noexcept;

private:
    int rootNote_ = 0;  // C
    ScaleType scale_ = ScaleType::Major;
};

} // namespace Krate::DSP
```

### Implementation Details

1. **Scale storage**: Each scale stored as `constexpr std::array<int, 7>` of semitone offsets from root:
   - Major: `{0, 2, 4, 5, 7, 9, 11}`
   - NaturalMinor: `{0, 2, 3, 5, 7, 8, 10}`
   - HarmonicMinor: `{0, 2, 3, 5, 7, 8, 11}`
   - MelodicMinor: `{0, 2, 3, 5, 7, 9, 11}`
   - Dorian: `{0, 2, 3, 5, 7, 9, 10}`
   - Mixolydian: `{0, 2, 4, 5, 7, 9, 10}`
   - Phrygian: `{0, 1, 3, 5, 7, 8, 10}`
   - Lydian: `{0, 2, 4, 6, 7, 9, 11}`

2. **Core algorithm** (research doc Section 4.1):
   - Compute input pitch class: `noteClass = midiNote % 12`
   - Compute offset from root: `offset = (noteClass - rootNote + 12) % 12`
   - Find nearest scale degree by searching the scale array
   - Add diatonic interval (e.g., +2 for "3rd above")
   - Look up target scale degree's semitone offset
   - Compute semitone shift accounting for octave wraps

3. **Non-scale input notes** (research doc Section 4.1): For notes not in the current scale (chromatic passing tones), use the interval for the nearest scale degree. E.g., in C major, C# → treat as C and apply C's interval.

4. **Chromatic mode**: When `ScaleType::Chromatic`, `diatonicSteps` is interpreted as raw semitones (no scale logic). This enables fixed-shift operation.

### Test Plan

Tests must cover the exact example from the research doc (Section 4.1 -- Key of C Major, Harmony = "3rd above"):

| Input | Scale Degree (0-based) | 3rd Above | Shift (semitones) |
|-------|----------------------|-----------|-------------------|
| C (60) | 0 | E | +4 (major 3rd) |
| D (62) | 1 | F | +3 (minor 3rd) |
| E (64) | 2 | G | +3 (minor 3rd) |
| F (65) | 3 | A | +4 (major 3rd) |
| G (67) | 4 | B | +4 (major 3rd) |
| A (69) | 5 | C | +3 (minor 3rd) |
| B (71) | 6 | D | +3 (minor 3rd) |

Additional test cases:
- All 8 scale types with 2nd, 3rd, 5th, octave intervals
- All 12 keys (not just C)
- Non-scale input notes (chromatic passing tones)
- Negative intervals (harmony below: -2 = "3rd below")
- Octave wrapping (7th above C in C major = B, crossing octave boundary)
- Edge cases: MIDI note 0, MIDI note 127
- Chromatic mode: diatonic steps treated as raw semitones

### File Locations

- Header: `dsp/include/krate/dsp/core/scale_harmonizer.h`
- Tests: `dsp/tests/unit/core/scale_harmonizer_test.cpp`

---

## Phase 2: Phase Vocoder Quality Improvements

**Layer**: 1-2 (Primitives + Processors)
**Blocks**: Phase 4 (quality improvements used by HarmonizerEngine)
**Effort**: ~3-5 days
**Depends On**: Nothing (can run in parallel with Phase 1 and Phase 3)

### Why This Exists

The existing `PhaseVocoderPitchShifter` has two confirmed quality issues:

1. **Phasiness** (no phase locking): The standard phase vocoder preserves horizontal phase coherence (continuity across time) but destroys vertical phase coherence (relationships between adjacent bins). This produces a reverberant, smeared quality (research doc Section 2.4-2.5).

2. **Transient smearing** (no phase reset): Windowed STFT spreads transient energy across multiple frames, producing pre-echo on drums and consonant onsets (research doc Section 3.2).

Both issues have well-understood solutions from the academic literature.

### Phase 2A: Identity Phase Locking (Laroche & Dolson, 1999) -- COMPLETE

**Status**: Complete -- implemented in spec [061-phase-locking](061-phase-locking/spec.md), merged to main.

**Files modified:**
- `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (modified `PhaseVocoderPitchShifter`)

**Files created:**
- `dsp/tests/unit/processors/phase_locking_test.cpp`

#### Algorithm (research doc Section 2.5)

**Step 1 -- Peak Detection**: Find local maxima in the magnitude spectrum:
```
For each bin k (1 to numBins-2):
    if magnitude[k] > magnitude[k-1] AND magnitude[k] > magnitude[k+1]:
        isPeak[k] = true
```

Typically 20-100 peaks for a 4096-pt FFT. Use pre-allocated `std::array<bool, kMaxBins>` and `std::array<size_t, kMaxPeaks>` (no runtime allocation).

**Step 2 -- Region-of-Influence Assignment**: Each non-peak bin assigned to its nearest peak. Linear scan: iterate forward marking each bin's region, then backward for right-side boundaries.

**Step 3 -- Phase Propagation**: Replace the current per-bin phase accumulation with:
- **Peak bins**: Standard horizontal phase propagation (same as current code)
- **Non-peak bins**: Lock phase relative to region peak:
  ```
  phi_out[k] = phi_out[regionPeak[k]] + (phi_in[k] - phi_in[regionPeak[k]])
  ```

This preserves vertical phase coherence within each harmonic's spectral lobe, dramatically reducing phasiness.

**Step 4 -- Sin/Cos Optimization**: With phase locking, sin/cos calls only needed for peak bins (20-100), not all 2049 bins. This reduces transcendental function cost by ~95% (research doc Section 2.5).

**SIMD note** (research doc Section 2.5): Peak detection vectorizes with `_mm_cmpgt_ps` and `_mm_movemask_ps` for ~2-3x speedup, but defer to Phase 5.

#### Member Variables to Add

```cpp
// Phase locking state (pre-allocated, zero runtime allocation)
std::array<bool, kMaxBins> isPeak_{};
std::array<std::size_t, kMaxPeaks> peakIndices_{};
std::size_t numPeaks_ = 0;
std::array<std::size_t, kMaxBins> regionPeak_{};
bool phaseLockingEnabled_ = true;

static constexpr std::size_t kMaxBins = 4097;   // 8192/2+1 (max supported FFT)
static constexpr std::size_t kMaxPeaks = 512;
```

#### Test Plan

- Compare output quality: phase-locked PV vs basic PV on tonal input (measure spectral energy spread)
- Verify peak count is reasonable for sinusoidal input (1 peak per harmonic)
- Verify region assignment covers all bins (no unassigned bins)
- Verify phase-locked output matches basic PV output when phase locking disabled
- Verify sin/cos only called for peak bins (count calls or measure CPU reduction)
- Toggle test: `setPhaseLocking(true/false)` doesn't introduce clicks

### Phase 2B: Spectral Transient Detection & Phase Reset -- COMPLETE

**Status**: Complete -- implemented in spec [062-spectral-transient-detector](062-spectral-transient-detector/spec.md), merged to main.

**Files created:**
- `dsp/include/krate/dsp/primitives/spectral_transient_detector.h`
- `dsp/tests/unit/primitives/spectral_transient_detector_test.cpp`
- `dsp/tests/unit/processors/phase_reset_test.cpp`

**Files modified:**
- `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (integrated phase reset)
- `dsp/CMakeLists.txt` (added to `KRATE_DSP_PRIMITIVES_HEADERS`)
- `dsp/tests/CMakeLists.txt` (added tests)

**Note**: Named `SpectralTransientDetector` to distinguish from the existing time-domain `TransientDetector` (L2), which is a modulation source based on envelope derivative analysis. This component operates on magnitude spectra, not audio samples.

#### Algorithm (research doc Section 3.2, Duxbury et al. 2002)

**Spectral flux computation**:
```
spectralFlux = sum(max(0, |X_curr[k]| - |X_prev[k]|))  for k = 0..numBins-1
```

Only positive differences (onset energy) contribute. Compare against running average:
```
if (spectralFlux > threshold * runningAverage):
    transient = true
runningAverage = smoothCoeff * runningAverage + (1 - smoothCoeff) * spectralFlux
```

#### API Design

```cpp
class SpectralTransientDetector {
public:
    void prepare(std::size_t numBins) noexcept;
    void reset() noexcept;

    /// Feed a magnitude spectrum frame, returns true if transient detected
    [[nodiscard]] bool detect(const float* magnitudes, std::size_t numBins) noexcept;

    // Configuration
    void setThreshold(float multiplier) noexcept;    // Default: 1.5x running average
    void setSmoothingCoeff(float coeff) noexcept;    // Default: 0.95

    // Query
    [[nodiscard]] float getSpectralFlux() const noexcept;
    [[nodiscard]] float getRunningAverage() const noexcept;
    [[nodiscard]] bool isTransient() const noexcept;

private:
    std::vector<float> prevMagnitudes_;  // allocated in prepare()
    float runningAverage_ = 0.0f;
    float threshold_ = 1.5f;
    float smoothingCoeff_ = 0.95f;
    bool transientDetected_ = false;
};
```

#### Phase Reset Integration

In `PhaseVocoderPitchShifter::processFrame()`:
1. After computing magnitudes, call `spectralTransientDetector_.detect(magnitudes, numBins)`
2. If transient detected: `synthPhase_[k] = analysisPhase[k]` (phase reset) for all bins
3. This preserves sharp attack transients instead of smearing them across frames

From research doc Section 3.2: "At detected transients, reset synthesis phase to match analysis phase directly rather than propagating."

#### Test Plan

- Feed impulse (all zeros then spike): verify transient detected
- Feed sustained sine: verify no false transient detection
- Feed drum pattern (alternating impulses and silence): verify each onset detected
- Verify spectral flux is proportional to energy change
- Verify threshold/sensitivity parameters affect detection rate
- Integration test: pitch-shifted drum loop with vs without phase reset -- compare peak-to-RMS ratio (transient sharpness)

### File Locations

| File | Layer |
|------|-------|
| `dsp/include/krate/dsp/primitives/spectral_transient_detector.h` | 1 |
| `dsp/tests/unit/primitives/spectral_transient_detector_test.cpp` | 1 |
| `dsp/tests/unit/processors/phase_locking_test.cpp` | 2 |

---

## Phase 3: Pitch Tracking Robustness

**Layer**: 1 (Primitives)
**Blocks**: Phase 4 (HarmonizerEngine depends on stable pitch tracking)
**Effort**: ~1-2 days
**Depends On**: Nothing (can run in parallel with Phase 1 and Phase 2)

### Why This Exists

From research doc Section 8, gap #6: "Raw pitch detector output is noisy and can oscillate between adjacent notes, causing the harmony voice to 'warble' or produce audible pitch jumps. Commercial harmonizers apply significant smoothing and confidence thresholds."

The existing `PitchDetector` provides raw autocorrelation-based pitch detection with confidence output. The `PitchTracker` wraps it with post-processing to produce stable, usable note decisions.

### Existing Building Blocks to Compose

| Component | Role in PitchTracker |
|-----------|---------------------|
| `PitchDetector` (L1) | Raw pitch detection (autocorrelation, 256-sample window) |
| `OnePoleSmoother` (L1) | Exponential smoothing for frequency output |
| `frequencyToMidiNote()` (L0) | Convert Hz to MIDI for note decision |
| `frequencyToCentsDeviation()` (L0) | Cents deviation for hysteresis threshold |

### API Design

```cpp
namespace Krate::DSP {

/// @brief Smoothed pitch tracker with hysteresis and confidence gating (Layer 1).
///
/// Wraps PitchDetector with median filtering, hysteresis, confidence gating,
/// and minimum note duration to produce stable note decisions suitable for
/// harmonizer interval calculation.
///
/// @par Real-Time Safety
/// All methods are noexcept, no allocations in process path.
class PitchTracker {
public:
    static constexpr std::size_t kDefaultWindowSize = 256;
    static constexpr std::size_t kMaxMedianSize = 11;
    static constexpr float kDefaultHysteresisThreshold = 50.0f;  // cents
    static constexpr float kDefaultConfidenceThreshold = 0.5f;
    static constexpr float kDefaultMinNoteDurationMs = 50.0f;

    PitchTracker() noexcept = default;

    void prepare(double sampleRate, std::size_t windowSize = kDefaultWindowSize) noexcept;
    void reset() noexcept;

    /// Feed audio samples (delegates to internal PitchDetector)
    void pushBlock(const float* samples, std::size_t numSamples) noexcept;

    // Smoothed, stable output
    [[nodiscard]] float getFrequency() const noexcept;
    [[nodiscard]] int getMidiNote() const noexcept;
    [[nodiscard]] float getConfidence() const noexcept;
    [[nodiscard]] bool isPitchValid() const noexcept;

    // Configuration
    void setMedianFilterSize(std::size_t size) noexcept;       // 1-11, default 5
    void setHysteresisThreshold(float cents) noexcept;          // default 50
    void setConfidenceThreshold(float threshold) noexcept;      // default 0.5
    void setMinNoteDuration(float ms) noexcept;                 // default 50ms

private:
    PitchDetector detector_;

    // Median filter (ring buffer of recent pitch values)
    std::array<float, kMaxMedianSize> pitchHistory_{};
    std::size_t medianSize_ = 5;
    std::size_t historyIndex_ = 0;
    std::size_t historyCount_ = 0;

    // Hysteresis state
    int currentNote_ = -1;
    float hysteresisThreshold_ = kDefaultHysteresisThreshold;

    // Confidence gating
    float confidenceThreshold_ = kDefaultConfidenceThreshold;

    // Note hold timer (minimum duration before switching)
    float minNoteDurationMs_ = kDefaultMinNoteDurationMs;
    float noteHoldTimer_ = 0.0f;
    double sampleRate_ = 44100.0;

    // Smoothed output
    OnePoleSmoother frequencySmoother_;
    float smoothedFrequency_ = 0.0f;
};

} // namespace Krate::DSP
```

### Implementation Details

1. **Median filter**: Ring buffer of last N pitch detections. Output the median value (sort + pick middle). Eliminates single-frame outliers that cause warbling.

2. **Hysteresis**: Only switch to a new note when detected pitch deviates from current note by more than `hysteresisThreshold` cents. Prevents oscillation at note boundaries. Uses `frequencyToCentsDeviation()` from `pitch_utils.h`.

3. **Confidence gating**: Only update pitch when `PitchDetector::getConfidence()` exceeds threshold. During unvoiced segments (low confidence), hold the last valid note. Prevents tracking noise.

4. **Minimum note duration**: Require a new note to be stable for `minNoteDurationMs` before switching. Sample counter: `noteHoldTimer_ += numSamples; if (noteHoldTimer_ >= minNoteDurationSamples) { commit new note }`.

5. **Frequency smoothing**: `OnePoleSmoother` on the output frequency for portamento-like transitions (optional, controlled by smoother time constant).

### Test Plan

- Feed stable 440Hz sine: verify single, stable A4 output
- Feed sine with +/- 10 cents jitter: verify no note switching (hysteresis)
- Feed A4 → B4 transition: verify clean switch after hold timer
- Feed silence/noise: verify pitch holds last valid note, `isPitchValid()` returns false
- Feed rapid note changes (5 changes/second): verify minimum duration gating prevents warble
- Verify median filter removes single-frame outlier pitch jumps
- Verify confidence threshold gates unvoiced segments

### File Locations

- Header: `dsp/include/krate/dsp/primitives/pitch_tracker.h`
- Tests: `dsp/tests/unit/primitives/pitch_tracker_test.cpp`

---

## Phase 4: Multi-Voice Harmonizer Engine

**Layer**: 3 (System)
**Blocks**: Plugin integration (Iterum harmonizer effect, or standalone)
**Effort**: ~4-6 days
**Depends On**: Phase 1, Phase 2, Phase 3

### Why This Exists

From research doc Section 4.3 and Section 8 gap #2: A harmonizer needs N independent pitch-shifted voices, each with its own diatonic interval, level, pan, and delay. The engine coordinates shared pitch detection, per-voice interval calculation, and per-voice pitch shifting.

The existing `UnisonEngine` (L3) provides multi-voice detuning but with fixed frequencies set externally. A harmonizer needs each voice to independently track input pitch, compute its own diatonic interval, and apply pitch shifting.

### Architecture (research doc Sections 4.3 & 8)

```
Input ──────────────────────────────────────────────────── Dry Path ──> Mix
  │                                                                      │
  ├──> PitchTracker (shared, Phase 3) ──> ScaleHarmonizer (Phase 1)     │
  │         │                                  │                         │
  │         v                                  v                         │
  ├──> Voice 0: [DelayLine] → [PitchShiftProcessor] → [Level/Pan] ──>  │
  ├──> Voice 1: [DelayLine] → [PitchShiftProcessor] → [Level/Pan] ──>  Sum
  ├──> Voice 2: [DelayLine] → [PitchShiftProcessor] → [Level/Pan] ──>  │
  └──> Voice 3: [DelayLine] → [PitchShiftProcessor] → [Level/Pan] ──>  │
```

**Internal components** (all reused from existing library):
- 1x shared `PitchTracker` (Phase 3) -- analyze input once
- 1x `ScaleHarmonizer` (Phase 1) -- compute intervals
- Nx `PitchShiftProcessor` (L2, existing) -- one per voice, most CPU-expensive part
- Nx `FormantPreserver` (L2, existing) -- optional, one per voice in PhaseVocoder mode
- Per-voice `OnePoleSmoother` (L1, existing) -- level/pan/pitch automation
- Per-voice `DelayLine` (L1, existing) -- onset offset

### Existing Building Blocks to Compose

| Component | Role in HarmonizerEngine |
|-----------|--------------------------|
| `PitchShiftProcessor` (L2) | Per-voice pitch shifting (4 modes) |
| `FormantPreserver` (L2) | Per-voice formant correction (PhaseVocoder mode) |
| `PitchTracker` (L1, Phase 3) | Shared input pitch detection |
| `ScaleHarmonizer` (L0, Phase 1) | Diatonic interval computation |
| `OnePoleSmoother` (L1) | Click-free parameter changes |
| `DelayLine` (L1) | Per-voice onset delay offset |
| `semitonesToRatio()` (L0) | Semitone-to-ratio for pitch shifter |

### Harmony Mode (research doc Section 4.2)

```cpp
/// Harmony intelligence mode
enum class HarmonyMode : uint8_t {
    Chromatic = 0,   ///< Fixed semitone shift (no scale awareness)
    Scalic = 1,      ///< Diatonic interval in a fixed key/scale
    // Future: Chordal = 2 (chord recognition), MIDI = 3 (external MIDI)
};
```

- **Chromatic mode**: `PitchTracker` and `ScaleHarmonizer` bypassed. Each voice interval = raw semitones. No key/scale needed. Lower CPU.
- **Scalic mode**: `PitchTracker` runs every block. `ScaleHarmonizer.calculate()` per voice per block. Semitone shift smoothed with `OnePoleSmoother` to avoid clicks on note changes.

### API Design

```cpp
namespace Krate::DSP {

class HarmonizerEngine {
public:
    // =========================================================================
    // Constants
    // =========================================================================
    static constexpr int kMaxVoices = 4;
    static constexpr float kMaxDelayMs = 50.0f;
    static constexpr float kMinLevel = -60.0f;   // dB
    static constexpr float kMaxLevel = 6.0f;     // dB

    // =========================================================================
    // Lifecycle
    // =========================================================================
    HarmonizerEngine() noexcept = default;
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Voice Configuration
    // =========================================================================
    void setNumVoices(int count) noexcept;              // [0, kMaxVoices]
    [[nodiscard]] int getNumVoices() const noexcept;

    void setVoiceInterval(int voice, int diatonicSteps) noexcept; // +2 = 3rd above
    void setVoiceLevel(int voice, float dB) noexcept;             // [-60, +6]
    void setVoicePan(int voice, float pan) noexcept;              // [-1, +1]
    void setVoiceDelay(int voice, float ms) noexcept;             // [0, 50]
    void setVoiceDetune(int voice, float cents) noexcept;         // [-100, +100]

    // =========================================================================
    // Global Configuration
    // =========================================================================
    void setKey(int rootNote) noexcept;                  // 0=C .. 11=B
    void setScale(ScaleType type) noexcept;
    void setHarmonyMode(HarmonyMode mode) noexcept;      // Chromatic, Scalic
    void setPitchShiftMode(PitchMode mode) noexcept;     // Simple/Granular/PitchSync/PhaseVocoder
    void setFormantPreserve(bool enabled) noexcept;
    void setDryLevel(float dB) noexcept;
    void setWetLevel(float dB) noexcept;

    // =========================================================================
    // Processing (Mono in, Stereo out)
    // =========================================================================
    void process(const float* input, float* outputL, float* outputR,
                 std::size_t numSamples) noexcept;

    // =========================================================================
    // Latency
    // =========================================================================
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;

    // =========================================================================
    // Query (for UI feedback)
    // =========================================================================
    [[nodiscard]] float getDetectedPitch() const noexcept;
    [[nodiscard]] int getDetectedNote() const noexcept;
    [[nodiscard]] float getPitchConfidence() const noexcept;

private:
    // Shared analysis
    PitchTracker pitchTracker_;
    ScaleHarmonizer scaleHarmonizer_;

    // Per-voice processing
    struct Voice {
        PitchShiftProcessor pitchShifter;
        DelayLine delayLine;
        OnePoleSmoother levelSmoother;
        OnePoleSmoother panSmoother;
        OnePoleSmoother pitchSmoother;  // smooths diatonic shift changes
        int interval = 0;
        float level = 0.0f;
        float pan = 0.0f;
        float delaySamples = 0.0f;
        float detuneCents = 0.0f;
        bool active = false;
    };
    std::array<Voice, kMaxVoices> voices_;

    // Global settings
    HarmonyMode harmonyMode_ = HarmonyMode::Scalic;
    int numActiveVoices_ = 0;
    float dryLevel_ = 0.0f;  // linear
    float wetLevel_ = 1.0f;  // linear

    // Scratch buffers
    std::vector<float> voiceScratch_;
    std::vector<float> delayScratch_;
};

} // namespace Krate::DSP
```

### Processing Flow (Per Block)

```
1. Push input block to PitchTracker (shared across all voices)
2. Get detected frequency, MIDI note, confidence
3. For each active voice (0 to numActiveVoices_-1):
   a. Compute semitone shift:
      - Scalic mode: ScaleHarmonizer.calculate(detectedNote, voiceInterval) → semitones
                     Smooth shift with pitchSmoother (avoid clicks on note changes)
      - Chromatic mode: semitones = voiceInterval (fixed, no pitch detection)
   b. Add voice micro-detune (cents / 100)
   c. Set PitchShiftProcessor.setSemitones(totalShift)
   d. Copy input through voice's DelayLine (onset offset) into delayScratch_
   e. PitchShiftProcessor.process(delayScratch_, voiceScratch_, numSamples)
   f. Apply level gain (dB to linear)
   g. Apply constant-power pan:
      leftGain  = cos(pan * pi/4 + pi/4)
      rightGain = sin(pan * pi/4 + pi/4)
   h. Accumulate into stereo output: outputL += voiceScratch_ * leftGain
                                      outputR += voiceScratch_ * rightGain
4. Apply dry signal: outputL += input * dryLevel, outputR += input * dryLevel
5. Apply wet level to accumulated harmony output
```

### Per-Voice Micro-Detuning (research doc Section 4.3)

"Micro-pitch detuning between voices (a few cents) creates natural 'ensemble' width." Each voice has an independent `detuneCents` parameter (+/- 100 cents) added on top of the computed diatonic shift. This creates natural beating and width, especially useful when multiple voices share the same interval.

### Stereo Panning (following UnisonEngine pattern)

Each voice has independent pan position. Use constant-power panning (same formula as `UnisonEngine`):
```cpp
float leftGain  = std::cos(pan * kPi * 0.25f + kPi * 0.25f);
float rightGain = std::sin(pan * kPi * 0.25f + kPi * 0.25f);
```

### Mode Switching (research doc Section 4.2)

When switching between Chromatic and Scalic modes at runtime:
- Smoothly transition the pitch shift using `pitchSmoother` (don't jump)
- Reset `PitchTracker` state when entering Scalic mode
- Preserve voice configuration (levels, pans, delays)

### Memory Considerations

| Component | Per Voice | 4 Voices |
|-----------|-----------|----------|
| PitchShiftProcessor (PhaseVocoder) | ~32 KB (FFT buffers) | ~128 KB |
| PitchShiftProcessor (PitchSync) | ~5 KB (delay line) | ~20 KB |
| DelayLine (50ms @ 48kHz) | ~10 KB | ~40 KB |
| Smoothers (3x) | ~0.1 KB | ~0.4 KB |
| **Total (PhaseVocoder mode)** | **~42 KB** | **~168 KB** |
| **Total (PitchSync mode)** | **~15 KB** | **~60 KB** |

All pre-allocated in `prepare()`. Under 200KB total, acceptable for real-time.

### SoA Layout Consideration (research doc Section 4.3, SIMD Priority Matrix)

"For a 4-voice harmonizer with phase-vocoder mode, each voice has a 4096-bin spectral buffer (~16KB per voice, ~64KB total), which exceeds L1 and makes SoA more impactful."

However, since each voice uses its own `PitchShiftProcessor` instance (which internally manages its own spectral buffers), SoA optimization applies at the *per-bin processing level within the phase vocoder*, not at the voice orchestration level. Defer SoA to Phase 5.

### Test Plan

- **Chromatic mode**: Feed 440Hz sine, voice at +7 semitones → verify output ~659Hz
- **Scalic mode (C major, 3rd above)**: Feed A4 (440Hz) → verify output C5 (+3 semitones, ~523Hz)
- **Multi-voice**: 2 voices (3rd + 5th above) → verify both present in output spectrum
- **Pan**: Verify left/right channel balance matches pan setting
- **Delay**: Verify onset offset between dry and wet signals
- **Formant preserve**: Verify spectral envelope preservation (compare centroid before/after)
- **Note transition**: Feed A4 → B4 glide → verify smooth harmony tracking (no clicks)
- **Silence handling**: Feed silence → verify no artifacts, no NaN
- **All pitch shift modes**: Verify Simple, Granular, PitchSync, PhaseVocoder all produce output
- **Latency reporting**: Verify `getLatencySamples()` matches selected pitch shift mode
- **Micro-detune**: Set +5 cents on voice → verify slight frequency offset (beating)

### File Locations

- Header: `dsp/include/krate/dsp/systems/harmonizer_engine.h`
- Tests: `dsp/tests/unit/systems/harmonizer_engine_test.cpp`

---

## Phase 5: SIMD Optimization

**Layer**: 0-1 (Core + Primitives)
**Blocks**: Nothing (independent optimization pass)
**Effort**: ~3-5 days
**Depends On**: Nothing (can run in parallel with all other phases)

### Why This Exists

The phase vocoder pipeline has ~10 distinct stages. The research doc (Section 2.4, Section 10) provides a detailed per-step SIMD analysis. Three operations dominate non-FFT CPU cost and have massive SIMD speedup potential:

| Operation | Current | SIMD Speedup | % of PV CPU |
|-----------|---------|-------------|-------------|
| Cart-to-Polar (`atan2`, `sqrt`) | Scalar `std::atan2`, `std::sqrt` | 10-50x | 15-25% |
| Polar-to-Cart (`sin`, `cos`) | Scalar `std::sin`, `std::cos` | 3-8x | 10-15% |
| `log`/`exp` (formant cepstrum) | Scalar `std::log10`, `std::pow` | 4-40x | 5-10% |

The research doc explicitly recommends (Section 10): "Add a vectorized math header wrapping Pommier's sse_mathfun. pffft already demonstrates this library is compatible with our build."

### Phase 5A: Vectorized Math Header

**Files to create:**
- `dsp/include/krate/dsp/core/simd_math.h`

**Algorithm sources** (from research doc Section 10):
- **Pommier sse_mathfun** ([source](https://github.com/RJVB/sse_mathfun)): SSE `sin_ps`, `cos_ps`, `sincos_ps`, `log_ps`, `exp_ps`. Max error 2.38e-7 vs scalar. Already proven compatible via pffft.
- **sse_mathfun_extension** ([source](https://github.com/to-miz/sse_mathfun_extension)): SSE `atan2_ps`. Used by Rubber Band via bqvec's `VectorOpsComplex.h`.
- **Mazzo vectorized atan2** ([source](https://mazzo.li/posts/vectorized-atan2.html)): 50x speedup, 2.04 cycles/element, 6-term Remez polynomial.
- **IEEE 754 bit tricks** ([source](http://gallium.inria.fr/blog/fast-vectorizable-math-approx/)): Fast `log2` via `float_as_int(x)` manipulation. ~40x throughput for ~3% error (acceptable for audio).

**Functions to provide:**
```cpp
// Process 4 floats simultaneously via SSE
__m128 atan2_ps(__m128 y, __m128 x);
void sincos_ps(__m128 x, __m128* sin_out, __m128* cos_out);
__m128 log_ps(__m128 x);
__m128 exp_ps(__m128 x);
__m128 sqrt_ps_fast(__m128 x);  // rsqrt + Newton-Raphson, ~22-bit accuracy
```

**Cross-platform** (research doc Section 10, Alignment):
- SSE: 16-byte alignment, 4 floats/op (target)
- NEON: 16-byte alignment, 4 floats/op (pffft already supports)
- Scalar fallback for unsupported platforms
- Use `pffft_aligned_malloc` or `alignas(16)` for aligned allocations

### Phase 5B: Batch Spectral Conversions

**Files to modify:**
- `dsp/include/krate/dsp/primitives/spectral_buffer.h`

Add batch SIMD functions using the vectorized math header:
```cpp
void batchCartesianToPolar(const float* reals, const float* imags,
                           float* magnitudes, float* phases,
                           std::size_t count) noexcept;

void batchPolarToCartesian(const float* magnitudes, const float* phases,
                           float* reals, float* imags,
                           std::size_t count) noexcept;
```

Process 4 bins at a time. This replaces the current scalar `Complex::magnitude()` (`std::sqrt`) and `Complex::phase()` (`std::atan2`) -- the single highest-impact optimization (research doc Section 10).

### Phase 5C: Vectorized Phase Wrapping

**Files to modify:**
- `dsp/include/krate/dsp/primitives/spectral_utils.h`

From research doc Section 2.4 -- phase wrapping in 4 SSE instructions:
```cpp
__m128 wrapPhaseSSE(__m128 delta) {
    __m128 inv_twopi = _mm_set1_ps(1.0f / (2.0f * M_PI));
    __m128 twopi = _mm_set1_ps(2.0f * M_PI);
    return _mm_sub_ps(delta, _mm_mul_ps(twopi,
        _mm_round_ps(_mm_mul_ps(delta, inv_twopi), _MM_FROUND_TO_NEAREST_INT)));
}
```

Replaces the current scalar while-loop `wrapPhase()` for batch processing. The scalar version remains for single-value use.

### Phase 5D: Vectorized Formant Preserver

**Files to modify:**
- `dsp/include/krate/dsp/processors/formant_preserver.h`

From research doc Section 3.1: The cepstral pipeline `log(|X[k]|) → IFFT → lifter → FFT → exp()` has `log`/`exp` as dominant non-FFT cost. Replace scalar `std::log10`/`std::pow` with batched `log_ps`/`exp_ps` from `simd_math.h`.

### Test Plan

- Verify SIMD results match scalar within tolerance (max error 2.38e-7 for atan2 per research)
- Benchmark `batchCartesianToPolar` vs scalar loop (target: 4x+ speedup)
- Benchmark `batchPolarToCartesian` vs scalar loop (target: 3x+ speedup)
- Benchmark formant preserver before/after SIMD log/exp
- Test on all supported FFT sizes (1024, 2048, 4096)
- Verify alignment requirements (16-byte for SSE)
- Cross-platform: verify scalar fallback compiles and produces correct results

### SIMD Priority Matrix (from research doc Section 10)

**Tier 1: Already Done**
| Operation | Status |
|-----------|--------|
| FFT/IFFT (pffft) | ~4x via SSE radix-4 butterflies. 30-40% of total CPU |

**Tier 2: High Impact (This Phase)**
| Priority | Operation | Speedup | Key Intrinsics |
|----------|-----------|---------|----------------|
| 1 | Cart-to-Polar (`atan2`, `sqrt`) | 10-50x | Pommier `atan2_ps`, `_mm_rsqrt_ps` + NR |
| 2 | Polar-to-Cart (`sin`, `cos`) | 3-8x | Pommier `sincos_ps` |
| 3 | `log`/`exp` (formant cepstrum) | 4-40x | Pommier `log_ps`/`exp_ps` |
| 4 | Multi-voice parallel processing | 3-4x | SoA layout across 4 voices in `__m128` |

**Tier 3: Moderate (auto-vectorize first, explicit SIMD only if needed)**
| Priority | Operation | Speedup |
|----------|-----------|---------|
| 5 | Windowing (`input[i] * window[i]`) | ~4x |
| 6 | Phase diff + unwrapping | ~4x |
| 7 | Overlap-add accumulation | ~4x |
| 8 | Phase accumulation, freq estimation | ~4x |
| 9 | Spectral multiply/divide (formant) | ~4x |

**Tier 4: Low Impact (don't hand-vectorize)**
| Priority | Operation | Why Low |
|----------|-----------|---------|
| 10 | Peak detection (phase locking) | Small fraction of CPU |
| 11 | Bin shifting / scatter | No efficient SSE/AVX scatter |
| 12 | Delay-line reads | Sequential, no parallelism |

### File Locations

- Header: `dsp/include/krate/dsp/core/simd_math.h`

---

## Dependency Graph

```
Phase 1: ScaleHarmonizer (L0) ✅                       Phase 5: SIMD (L0-1)
   │  [COMPLETE]                                         [3-5 days, independent]
   │
   │  Phase 2A: Identity Phase Locking (L2) ✅
   │     │  [COMPLETE]
   │     │
   │  Phase 2B: SpectralTransientDetector (L1) ✅
   │     │  [COMPLETE]
   │     │
   │  Phase 3: PitchTracker (L1)
   │     │  [1-2 days, parallel with P1]
   │     │
   └──┬──┴──┬──┘
      │     │
      ▼     ▼
   Phase 4: HarmonizerEngine (L3)
      │  [4-6 days]
      ▼
     DONE
```

### Parallelization Opportunities

| Parallel Track A | Parallel Track B | Parallel Track C | Parallel Track D |
|-----------------|-----------------|-----------------|-----------------|
| Phase 1: ScaleHarmonizer | Phase 2A: Phase Locking | Phase 2B: Transient Det. | Phase 3: PitchTracker |
| (1-2 days) | (2-3 days) | (1-2 days) | (1-2 days) |
| ↓ | ↓ | ↓ | ↓ |
| **Merge point: Phase 4 starts (all complete)** | | | |

Phase 5 (SIMD) is fully independent and can run at any time.

### Estimated Total Timeline

| Phase | Duration | Cumulative (Serial) | Cumulative (Parallel) |
|-------|----------|--------------------|-----------------------|
| Phase 1 | 1-2 days | 1-2 days | 2-3 days (P1+P2+P3 parallel) |
| Phase 2A | 2-3 days | 3-5 days | (included above) |
| Phase 2B | 1-2 days | 4-7 days | (included above) |
| Phase 3 | 1-2 days | 5-9 days | (included above) |
| Phase 4 | 4-6 days | 9-15 days | 6-9 days |
| Phase 5 | 3-5 days | 12-20 days | 9-14 days (parallel with P4 or after) |

**Estimated total: 9-14 working days (2-3 weeks) with parallelization.**

---

## Risk Analysis

### High Risk

| Risk | Impact | Mitigation |
|------|--------|-----------|
| **PhaseVocoder CPU cost per voice** | 4 voices in PhaseVocoder mode = 4x FFT/IFFT per block | Offer PitchSync as default (5-10ms latency, much lower CPU). PhaseVocoder as "quality mode". ShimmerDelay already uses PitchSync for this reason. |
| **Pitch detection latency vs accuracy** | `PitchDetector` uses 256-sample window (~5.8ms). Low-frequency detection (50Hz) needs at least 20ms (research doc Section 3.3) | For low notes, PitchTracker median filter smooths over detection window. Accept 1-2 frame latency for sub-100Hz input. |
| **Phase locking complexity in existing code** | Modifying `PhaseVocoderPitchShifter::processFrame()` risks regressions | Add `setPhaseLocking(bool)` toggle. When disabled, behavior is identical to current code. Extensive before/after comparison tests. |

### Medium Risk

| Risk | Impact | Mitigation |
|------|--------|-----------|
| **Note-change clicks in Scalic mode** | Diatonic shift jumps discretely when PitchTracker detects new note | `OnePoleSmoother` on the pitch shift with 5-10ms glide time. PitchTracker minimum note duration prevents rapid switching. |
| **SIMD cross-platform portability** | SSE intrinsics don't compile on ARM | Scalar fallback path. pffft already demonstrates the pattern (SSE/NEON/scalar auto-detection). |
| **Spectral transient detection sensitivity** | Too sensitive = false positives (warbles). Too insensitive = smeared transients. | Configurable threshold with sensible default (1.5x running average). Expose sensitivity parameter. |

### Low Risk

| Risk | Impact | Mitigation |
|------|--------|-----------|
| **ScaleHarmonizer correctness** | Wrong diatonic intervals | Pure lookup table logic, exhaustively testable with known music theory. Reference table from research doc Section 4.1. |
| **PitchTracker stability** | Already-proven PitchDetector as foundation | Wraps existing component; adds only post-processing (median, hysteresis, gating). |
| **Multi-voice mixing** | Sum + pan is well-understood | Follow UnisonEngine pattern (constant-power pan, gain compensation). |
| **Memory footprint** | 4 voices x 42KB = ~168KB (PhaseVocoder mode) | Under 200KB, well within acceptable limits. All pre-allocated in `prepare()`. |

---

## Summary: What Exists vs What's New

```
EXISTING (reuse directly):          ~80% of DSP functionality
├── PitchShiftProcessor (4 modes: Simple, Granular, PitchSync, PhaseVocoder)
├── FormantPreserver (cepstral envelope correction)
├── PitchDetector (autocorrelation, 256-sample window)
├── FFT / STFT / OverlapAdd (pffft SIMD)
├── SpectralBuffer (dual Cartesian/polar, Highway SIMD)
├── spectral_utils (wrapPhase, phaseDifference, binToFrequency)
├── pitch_utils (semitonesToRatio, frequencyToMidiNote, frequencyToNoteClass)
├── DelayLine (circular, interpolated)
├── OnePoleSmoother / LinearRamp / SlewLimiter
├── StereoField (width, M/S)
├── Window functions (Hann, Hamming, Blackman, Kaiser)
└── ShimmerDelay (pitch-shift-in-feedback reference architecture)

NEW (must build):                   ~20% of DSP functionality
├── ScaleHarmonizer (L0) -- COMPLETE (spec 060, scale_harmonizer.h)
├── PitchTracker (L1) -- median filter + hysteresis + confidence gate
├── SpectralTransientDetector (L1) -- COMPLETE (spec 062, spectral_transient_detector.h + phase reset)
├── Identity Phase Locking (L2) -- COMPLETE (spec 061, integrated into PhaseVocoderPitchShifter)
├── HarmonizerEngine (L3) -- orchestration of existing components
└── SIMD Math Header (L0) -- vectorized atan2/sincos/log/exp
```

The majority of "new" code is either **musical intelligence** (ScaleHarmonizer, PitchTracker -- pure math/logic), **quality improvements** to existing algorithms (phase locking, transient detection), or **orchestration** (HarmonizerEngine composing existing components). The only genuinely new DSP algorithm is the spectral flux transient detector, which is a well-documented ~20-line computation.
