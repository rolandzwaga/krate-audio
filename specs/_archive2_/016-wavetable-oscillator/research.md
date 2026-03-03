# Research: Wavetable Oscillator with Mipmapping

**Branch**: `016-wavetable-oscillator` | **Date**: 2026-02-04

## Research Tasks and Findings

### R-001: Wavetable Data Storage Layout with Guard Samples

**Decision**: Use `std::array<std::array<float, kDefaultTableSize + 4>, kMaxMipmapLevels>` for fixed-size storage. Each level stores N+4 floats: 1 prepend guard at physical index 0 (logically [-1]), N data samples at indices [1..N], and 3 append guards at indices [N+1..N+3]. The `getLevel()` method returns a pointer to the first data sample (index 1), so that pointer[-1] accesses the prepend guard.

**Rationale**: Fixed-size storage makes `WavetableData` a value type (~90 KB) that can be stack-allocated or placed in pre-allocated memory. The guard sample layout enables branchless cubic Hermite interpolation: given `int_phase` in [0, N-1], the expression `float* p = &table[int_phase]; float y0 = p[-1]; float y1 = p[0]; float y2 = p[1]; float y3 = p[2];` always produces valid reads without any conditional logic or modulo operations.

The guard samples are:
- `table[-1] = table[N-1]` (look-behind at phase 0 wraps to end)
- `table[N] = table[0]` (look-ahead at phase N-1 wraps to start)
- `table[N+1] = table[1]` (cubic Hermite needs 2 ahead)
- `table[N+2] = table[2]` (cubic Hermite needs 3 ahead for the p[2] access when int_phase = N-1)

**Alternatives considered**:
- Dynamic allocation (`std::vector`): Rejected because it adds heap allocation complexity and prevents the struct from being a simple value type. The 90 KB size is well within acceptable limits for audio applications.
- Single flat array with offsets: Rejected because the 2D array provides clearer semantics and the compiler optimizes it identically.
- Larger guard region (e.g., 8 samples for Lagrange): Rejected because the spec specifies cubic Hermite only. Can be extended later if needed.

---

### R-002: Mipmap Level Selection Formula

**Decision**: Use `level = max(0, ceil(log2(frequency * tableSize / sampleRate)))`, clamped to [0, numLevels - 1]. For the fractional variant, use `max(0.0f, log2f(frequency * tableSize / sampleRate))`, clamped to [0.0f, numLevels - 1.0f]. The oscillator adds +1.0 to the fractional level before floor-based decomposition, ensuring both crossfade levels are alias-free.

**Rationale**: This formula computes how many octaves the playback frequency is above the table's fundamental frequency. At the fundamental frequency `sampleRate / tableSize` (~21.5 Hz for 2048 @ 44100), all harmonics fit below Nyquist, so level 0 (full bandwidth) is used. Each doubling of frequency requires halving the harmonic count, which corresponds to moving up one mipmap level.

**Derivation**:
- Table fundamental = sampleRate / tableSize (e.g., 44100 / 2048 = 21.53 Hz)
- At frequency `f`, the number of harmonics that fit below Nyquist = sampleRate / (2 * f)
- Level 0 has tableSize/2 harmonics, level 1 has tableSize/4, etc.
- Level N has tableSize / (2^(N+1)) harmonics
- For alias-free playback, the level's harmonic count must NOT exceed the safe count:
  tableSize / (2^(N+1)) <= sampleRate / (2 * f)
- Solving: 2^(N+1) >= 2 * f * tableSize / sampleRate = 2 * ratio
  N+1 >= log2(2 * ratio) = 1 + log2(ratio)
  N >= log2(ratio) = log2(f * tableSize / sampleRate)
- So level = ceil(log2(f * tableSize / sampleRate))

**Edge cases**:
- f = 0 Hz: log2(0) = -inf, clamped to 0 (correct: no aliasing risk)
- f = 20 Hz: log2(20 * 2048 / 44100) = log2(0.93) = -0.11, clamped to 0 (correct: all harmonics fit)
- f = 100 Hz: log2(4.64) = 2.22, ceil = 3 (level 3, 128 max harmonics, max freq 12800 Hz < Nyquist)
- f = 440 Hz: log2(20.42) = 4.35, ceil = 5 (level 5, 32 max harmonics, max freq 14080 Hz < Nyquist)
- f < fundamental (~21.5 Hz): log2(value < 1) = negative, clamped to 0 (correct: full harmonics)
- f = Nyquist: log2(22050 * 2048 / 44100) = log2(1024) = 10 = highest level (correct)
- f > Nyquist: clamped to highest level

**Implementation note**: Use `std::log2f()` for the float version. For the constexpr integer version, a loop-based approach is used: increment level while `threshold < frequency` (ceil semantics), since `std::log2` is not constexpr in all compilers. The oscillator adds +1.0 to the fractional level before floor-based crossfade decomposition, ensuring both adjacent levels used in the crossfade have all harmonics below Nyquist.

**Alternatives considered**:
- Lookup table for level selection: Rejected because log2 is fast enough and the lookup table would need to cover a wide frequency range.
- Per-semitone mipmap spacing: Rejected because per-octave spacing with crossfading provides sufficient quality per research literature.

---

### R-003: Mipmap Generation via FFT/IFFT

**Decision**: Use the existing `FFT` class for all mipmap generation. For standard waveforms (saw, square, triangle) and custom harmonics: set harmonic amplitudes in the frequency domain as `Complex` bins, then use `FFT::inverse()` to produce the time-domain table. For raw samples: use `FFT::forward()` to analyze, zero bins above Nyquist limit per level, then `FFT::inverse()` to resynthesize.

**Rationale**: The FFT approach is O(N log N) vs O(N*H) for direct additive synthesis, which is significant when H (number of harmonics) is large (up to 1024 for a 2048-sample table). The existing FFT class supports sizes in [256, 8192], which covers the default 2048-sample table size. Using FFT for ALL generation paths (including standard waveforms) ensures consistency and leverages the existing tested infrastructure.

**Implementation pattern for standard waveforms**:
```
1. Allocate temporary Complex spectrum[tableSize/2 + 1]
2. Set DC bin (bin 0) to {0, 0}
3. For each harmonic n = 1 to maxHarmonicForLevel:
   a. Compute amplitude based on waveform type (1/n for saw, 1/n odd-only for square, etc.)
   b. Set spectrum[n] = {0.0f, -amplitude} for sine-phased harmonics
      (negative imaginary = sine wave starting at 0, matching standard convention)
4. Zero all bins above maxHarmonicForLevel
5. Call FFT::inverse(spectrum, tableData) to produce time-domain samples
6. Normalize: find peak, scale all samples so peak = 0.95-0.97
7. Set guard samples
```

**Harmonic series for standard waveforms**:
- Sawtooth: harmonics 1..N with amplitude 1/n, alternating sign for correct phase
  - Frequency domain: `spectrum[n] = {0.0f, -1.0f/n}` for each harmonic n
- Square: odd harmonics only (1, 3, 5, ...) with amplitude 1/n
  - Frequency domain: `spectrum[n] = {0.0f, -1.0f/n}` for n = 1, 3, 5, ...
- Triangle: odd harmonics only with amplitude 1/n^2, alternating sign
  - Frequency domain: `spectrum[n] = {0.0f, sign * 1.0f/(n*n)}` for n = 1, 3, 5, ...
  - sign alternates: +1 for n=1, -1 for n=3, +1 for n=5, etc.

**Max harmonics per level**:
- Level 0: tableSize / 2 (all harmonics, e.g., 1024 for 2048-sample table)
- Level 1: tableSize / 4 (512 harmonics)
- Level N: tableSize / (2^(N+1))
- Highest level (10 for 11 levels): tableSize / 2048 = 1 (fundamental only = sine)

**Alternatives considered**:
- Direct additive synthesis (summing sines in time domain): Rejected because it is O(N*H) per level, much slower for high-harmonic levels. However, it would produce identical results.
- Pre-computed tables (compile-time generation): Rejected because the table data is too large for constexpr evaluation, and runtime generation during prepare() is fast enough.
- Using window functions during generation: Not needed for additive synthesis (we control exact harmonic content). Would only be needed for Gibbs phenomenon reduction, which is handled by normalization.

---

### R-004: Phase Alignment Between Mipmap Levels

**Decision**: All mipmap levels MUST be phase-aligned by using consistent phase conventions in the frequency-domain representation. Specifically, all harmonics are specified as sine components (imaginary part only in the FFT bins), ensuring all levels start at zero crossing with identical phase.

**Rationale**: Phase misalignment between mipmap levels causes destructive interference during crossfading, resulting in volume dips and timbral artifacts. When crossfading between level N and level N+1, the harmonics present in both levels must be in phase. By using pure imaginary FFT bins (`{0, -amplitude}` for sine phase), all levels share the same phase reference and the shared harmonics are identical in both levels.

**Verification**: For any harmonic `k` present in both level N and level N+1, the time-domain contribution is `A_k * sin(2*pi*k*n/tableSize)`. Since the amplitude `A_k` and phase are identical in both levels for shared harmonics, crossfading produces a smooth transition where only the higher harmonics (present in level N but absent in level N+1) are faded out.

---

### R-005: Independent Normalization per Mipmap Level

**Decision**: Normalize each mipmap level independently so that peak amplitude is approximately 0.95-0.97. This provides headroom for cubic Hermite inter-sample peaks while maintaining consistent perceived loudness across pitch range.

**Rationale**: Different mipmap levels have different numbers of harmonics, producing different peak amplitudes before normalization. Level 0 (all harmonics) of a sawtooth has higher peak amplitude than level 10 (sine only). Without per-level normalization, the oscillator would produce inconsistent volume when switching between mipmap levels during frequency sweeps.

**Normalization target**: 0.95 to 0.97 peak, not 1.0. This headroom accounts for:
1. Cubic Hermite interpolation can produce inter-sample peaks up to ~3% above the sample peaks
2. Gibbs phenomenon at level 0 for discontinuous waveforms (saw, square) causes ~9% overshoot before normalization
3. The normalization target of 0.95 ensures post-interpolation values stay within [-1.0, 1.0] for most cases

**Implementation**: After IFFT, scan all N samples to find the absolute peak, then multiply all samples by `0.96f / peak`.

---

### R-006: Mipmap Crossfading Strategy

**Decision**: Compute a fractional mipmap level using `selectMipmapLevelFractional()`. When the fractional part is within a threshold of an integer (< 0.05 from either side), use a single table lookup. Otherwise, read from two adjacent levels and linearly blend. Hysteresis (switch up at 0.6, down at 0.4) is explicitly deferred to future work; the initial implementation uses the simple 0.05 threshold.

**Rationale**: Per-sample crossfading provides the smoothest transitions during frequency sweeps. The single-lookup optimization avoids the cost of two table reads + interpolation when the fractional level is near an integer (which is the common case for stable frequencies). The threshold of 0.05 means that for 90% of the level range, only one lookup occurs.

**Implementation in `process()`**:
```
1. Compute fractional level: fracLevel = selectMipmapLevelFractional(effectiveFreq, sampleRate, tableSize)
2. Compute integer level: intLevel = floor(fracLevel)
3. Compute fraction: frac = fracLevel - intLevel
4. If frac < 0.05 or frac > 0.95 or intLevel >= numLevels - 1:
   - Single lookup from round(fracLevel) clamped to [0, numLevels-1]
   - sample = cubicHermiteRead(table, level, phase)
5. Else:
   - sample1 = cubicHermiteRead(table, intLevel, phase)
   - sample2 = cubicHermiteRead(table, intLevel + 1, phase)
   - sample = linearInterpolate(sample1, sample2, frac)
```

**Performance note**: The two-lookup case costs approximately 2x the single-lookup case. For typical usage (stable frequency), the single-lookup path is taken, keeping performance near optimal.

**Alternatives considered**:
- Block-based crossfade (pick one level per block, fade at transitions): Simpler but can produce audible clicks on rapid frequency changes within a block. Rejected for per-sample approach.
- Hysteresis (switch up at 0.6, down at 0.4): Adds complexity and state. Explicitly deferred to future work. Can be added later if chattering is observed in practice without modifying the public API.
- No crossfading (hard switch at level boundaries): Produces audible clicks during frequency sweeps. Rejected.

---

### R-007: Cubic Hermite Interpolation with Guard Samples

**Decision**: Use the existing `Interpolation::cubicHermiteInterpolate(ym1, y0, y1, y2, t)` from `core/interpolation.h` with the guard sample layout to enable fully branchless table reads.

**Rationale**: The guard samples (FR-006) eliminate ALL boundary checks from the inner loop. For any integer phase index `i` in [0, N-1]:
- `p[-1]` reads the prepend guard (or the previous sample for i > 0)
- `p[0]` reads the current sample
- `p[1]` reads the next sample (or first append guard for i = N-1)
- `p[2]` reads two ahead (or second append guard for i = N-1)

This maps directly to the `cubicHermiteInterpolate(ym1, y0, y1, y2, t)` signature:
```cpp
const float* p = &levelData[intPhase];
float sample = Interpolation::cubicHermiteInterpolate(p[-1], p[0], p[1], p[2], fracPhase);
```

No modulo operations, no if-statements, no boundary checks. The pointer arithmetic is valid for all phase positions because of the guard samples.

---

### R-008: Resampling in `generateMipmappedFromSamples`

**Decision**: When the input sample count differs from the table size, resample using FFT zero-padding or truncation. Specifically: FFT the input at its native size, then zero-pad or truncate the spectrum to match the table size, then IFFT at the table size.

**Rationale**: The FFT naturally handles resampling when the transform size changes. If the input has fewer samples than the table size, zero-padding the spectrum adds interpolated samples. If the input has more samples, truncating the spectrum discards high-frequency content (effective lowpass filter).

**Implementation**:
1. If `sampleCount == tableSize`: FFT directly, proceed normally
2. If `sampleCount != tableSize`:
   a. FFT the input at `sampleCount` (or nearest power-of-2)
   b. Copy bins 0..min(inputBins, tableBins) to a new spectrum of size tableBins
   c. Zero remaining bins
   d. IFFT at tableSize

**Edge case**: If `sampleCount` is not a power of 2, the existing FFT class requires power-of-2 sizes. The implementation should zero-pad the input to the next power of 2 before FFT, or document this as a precondition.

---

### R-009: `processBlock` with FM Buffer

**Decision**: The FM-buffer `processBlock(output, fmBuffer, numSamples)` overload computes per-sample effective frequency and mipmap level selection. For the constant-frequency `processBlock(output, numSamples)` overload, the mipmap level is computed once at the start.

**Rationale**: Per-sample mipmap selection in the FM path ensures correct anti-aliasing even with rapid frequency changes. The constant-frequency path is optimized by computing the fractional mipmap level once and using it for all samples in the block.

**Implementation pattern for FM path**:
```
for each sample i:
    effectiveFreq = clamp(baseFrequency + fmBuffer[i], 0, sampleRate/2)
    fracLevel = selectMipmapLevelFractional(effectiveFreq, sampleRate, tableSize)
    sample = readWithCrossfade(fracLevel, currentPhase)
    advance phase using effectiveFreq
    output[i] = sanitize(sample)
```

---

### R-010: WavetableData Memory Alignment

**Decision**: Align the start of each mipmap level's data (index 0, after the prepend guard) to at least 16 bytes (SSE alignment). Use `alignas(32)` on the outer array for AVX compatibility.

**Rationale**: Per Constitution Principle IV, audio buffers should be aligned for SIMD. While the initial implementation is scalar, the data layout should not prevent future SIMD optimization. Using `alignas(32)` on the entire WavetableData struct ensures the array is aligned. Individual level alignment depends on the element count: with tableSize=2048 and 4 guard samples, each level is 2052 floats = 8208 bytes, which is naturally aligned to any power-of-2 boundary.

**Implementation note**: Since each level array is `std::array<float, 2052>` (8208 bytes), and the outer array starts at a 32-byte boundary, the first level starts at offset 0 (aligned), the second at offset 8208 (which is 8208 = 256*32 + 16, so 16-byte aligned but not 32-byte aligned). For full 32-byte alignment of every level, padding would be needed. This is acceptable for the initial implementation since cubic Hermite does not use SIMD internally.

---

### R-011: Existing Component Reuse Verification

**Decision**: Reuse all identified Layer 0 and Layer 1 components. No modifications needed to existing code.

**Components verified**:

| Component | Header | Verified API | Status |
|-----------|--------|-------------|--------|
| `PhaseAccumulator` | `core/phase_utils.h` | struct with `phase`, `increment`, `advance()`, `reset()`, `setFrequency()` | Reuse |
| `calculatePhaseIncrement()` | `core/phase_utils.h` | `constexpr double calculatePhaseIncrement(float, float) noexcept` | Reuse |
| `wrapPhase()` | `core/phase_utils.h` | `constexpr double wrapPhase(double) noexcept` | Reuse |
| `cubicHermiteInterpolate()` | `core/interpolation.h` | `constexpr float cubicHermiteInterpolate(float, float, float, float, float) noexcept` | Reuse |
| `linearInterpolate()` | `core/interpolation.h` | `constexpr float linearInterpolate(float, float, float) noexcept` | Reuse |
| `kPi`, `kTwoPi` | `core/math_constants.h` | `inline constexpr float` | Reuse |
| `detail::isNaN()` | `core/db_utils.h` | `constexpr bool isNaN(float) noexcept` | Reuse |
| `detail::isInf()` | `core/db_utils.h` | `constexpr bool isInf(float) noexcept` | Reuse |
| `FFT` class | `primitives/fft.h` | `prepare()`, `forward()`, `inverse()`, `size()`, `numBins()` | Reuse |
| `Complex` struct | `primitives/fft.h` | POD with `real`, `imag`, arithmetic operators | Reuse |

**ODR Check Results**:
- `WavetableData` -- Not found in codebase. Safe to create.
- `WavetableOscillator` -- Not found in codebase. Safe to create.
- `selectMipmapLevel` -- Not found. Safe to create.
- `generateMipmapped*` -- Not found. Safe to create.
- `wavetable` (general search) -- Found as private member arrays in lfo.h and audio_rate_filter_fm.h. No conflict with named types.

---

### R-012: Test Infrastructure for Wavetable Verification

**Decision**: Reuse the existing `SpectralAnalysis` test helper for FFT-based harmonic content verification and alias measurement. Use the existing `FFT` class directly in tests for harmonic content analysis of generated tables.

**Rationale**: The spectral analysis test helper provides `frequencyToBin()`, `getAliasedBins()`, and related utilities. For wavetable-specific tests, we also need to verify harmonic content of generated tables (SC-005 through SC-008), which requires direct FFT analysis of the table data.

**Test patterns**:
1. **Mipmap level selection**: Direct unit tests with known frequency/sampleRate/tableSize -> expected level
2. **Generator harmonic content**: Generate table, FFT the table data, verify bin magnitudes match expected harmonic series
3. **Oscillator alias suppression**: Generate output at various frequencies, FFT the output, measure alias components
4. **Crossfade smoothness**: Sweep frequency, check for discontinuities in output
5. **Phase interface**: Same tests as PolyBlepOscillator (phase wrap counting, resetPhase, PM/FM)
