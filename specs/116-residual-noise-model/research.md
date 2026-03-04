# Research: Innexus Milestone 2 -- Residual/Noise Model

## Research Questions

### RQ-1: How should harmonic subtraction be performed for accurate residual extraction?

**Decision**: Use frequency-domain spectral subtraction with phase-aligned harmonic resynthesis.

**Rationale**: The spec requires `residual = originalSignal - resynthesizedHarmonics` (FR-001). For accurate cancellation, the harmonic signal must be resynthesized with the exact tracked partial frequencies (FR-002), amplitudes, and phases (FR-003). The subtraction is performed in the time domain: for each analysis frame, the harmonic signal is resynthesized from the tracked partials using additive synthesis (sum of sinusoids at tracked frequencies/amplitudes/phases), then subtracted from the original windowed audio segment. The result is then analyzed via STFT to extract the residual spectral envelope.

**Implementation approach**:
1. For each analysis frame, take the original audio segment (one STFT hop window).
2. Resynthesize the harmonic content using the tracked partials from that frame: `harmonicSignal[n] = sum_k(amplitude_k * sin(phase_k + 2*pi*freq_k*n/sampleRate))` for samples n in the frame.
3. Subtract: `residualSignal = originalSignal - harmonicSignal`.
4. Apply STFT to the residual signal to get the spectral envelope.

**Note**: We do NOT need to use the `HarmonicOscillatorBank` for this offline subtraction. The oscillator bank is optimized for real-time streaming with amplitude smoothing and phase continuity. For offline frame-by-frame analysis, direct sinusoidal synthesis from the tracked Partial data is simpler and more accurate (no smoothing artifacts, exact phase alignment within each frame).

**Alternatives considered**:
- Spectral coring (zeroing harmonic bins): Simpler but less accurate for sample mode; deferred to M3 for live sidechain mode where latency matters.
- LPC-based residual extraction: More complex, requires solving Toeplitz system per frame, not necessary given good partial tracking.

### RQ-2: What is the optimal spectral envelope representation for 16 breakpoints?

**Decision**: Log-spaced frequency bands (approximately ERB/Bark scale) with piecewise-linear interpolation between breakpoints.

**Rationale**: The spec requires 8-16 breakpoints with logarithmic frequency distribution (FR-004, FR-005). Human auditory perception is approximately logarithmic in frequency. Using 16 bands with centers roughly following the ERB (Equivalent Rectangular Bandwidth) scale captures the perceptually important spectral shape without excessive data.

**Band center frequencies** (for 44.1 kHz, Nyquist = 22050 Hz):
Using a geometric series from ~50 Hz to ~20000 Hz with 16 points:

| Band | Center (Hz) | Approximate Range |
|------|-------------|-------------------|
| 0 | 50 | 0-75 |
| 1 | 100 | 75-150 |
| 2 | 175 | 150-225 |
| 3 | 275 | 225-350 |
| 4 | 425 | 350-530 |
| 5 | 650 | 530-800 |
| 6 | 1000 | 800-1250 |
| 7 | 1500 | 1250-1900 |
| 8 | 2300 | 1900-2800 |
| 9 | 3400 | 2800-4200 |
| 10 | 5000 | 4200-6200 |
| 11 | 7500 | 6200-9200 |
| 12 | 11000 | 9200-13500 |
| 13 | 16000 | 13500-19000 |
| 14 | 19500 | 19000-20500 |
| 15 | 21000 | 20500-22050 |

**Implementation**: Store breakpoint frequencies as normalized ratios (0.0 to 1.0 of Nyquist) for sample-rate independence (per edge case in spec). During analysis, compute the RMS energy in each band from the residual magnitude spectrum. During synthesis, interpolate the 16 breakpoints back to FFT-bin resolution using piecewise-linear interpolation in the log-frequency domain.

**Alternatives considered**:
- Uniform frequency spacing: Poor perceptual resolution at low frequencies, wastes resolution at high frequencies.
- Full FFT-resolution envelope: Excessive data per frame (513 floats vs 16), high serialization cost, unnecessary for noise character.
- LPC envelope: Parametric but harder to tilt (brightness control) and less intuitive for interpolation.

### RQ-3: How should the OverlapAdd infrastructure be used for noise resynthesis?

**Decision**: Use the existing `OverlapAdd` class with `applySynthesisWindow = true` (Hann window, 75% overlap).

**Rationale**: The ResidualSynthesizer generates spectrally shaped noise in the FFT domain. The IFFT output of random-phase noise does NOT have the smooth taper of a windowed analysis frame, so a synthesis window is needed to prevent boundary discontinuities. The OverlapAdd class already supports this via the `applySynthesisWindow` parameter.

**Configuration**:
- FFT size: matches analysis short window (1024 at 44.1 kHz) -- passed via `prepare()` parameter
- Hop size: matches analysis short window (512 at 44.1 kHz) -- passed via `prepare()` parameter
- Window: Hann (default in OverlapAdd)
- `applySynthesisWindow = true` -- applies Hann window to IFFT output before accumulation

**Frame-by-frame synthesis flow**:
1. Generate one frame of white noise (fftSize samples) from Xorshift32 PRNG.
2. Forward FFT the noise to get the noise spectrum.
3. Multiply noise spectrum by interpolated spectral envelope (from ResidualFrame).
4. Scale by `sqrt(totalEnergy / noiseEnergy)` to match the frame's energy level.
5. Write the shaped spectrum to a SpectralBuffer.
6. Call `OverlapAdd::synthesize(spectralBuffer)` to accumulate into output.
7. Call `OverlapAdd::pullSamples()` to extract hopSize output samples.

**Key insight**: The noise spectrum from step 2 has random phases and approximately flat magnitude. Multiplying by the spectral envelope shapes it to match the residual's spectral character. Scaling by the energy ratio ensures the output level matches the original residual energy.

**Alternatives considered**:
- Filter bank approach: Apply parallel bandpass filters to white noise. Higher CPU cost (16 filters per sample vs 1 FFT pair per frame), harder to match exact spectral envelope shape.
- Direct time-domain noise shaping: Not practical for arbitrary spectral envelopes.

### RQ-4: How should the Harmonic/Residual Mix parameter be implemented?

**Decision**: Two independent gain parameters (harmonic level and residual level), both defaulting to 1.0, applied as simple multipliers to the respective outputs before summing.

**Rationale**: FR-024 explicitly requires independent gains (not a crossfade). This allows the user to boost both above unity or attenuate both independently. The output is: `output = harmonicOutput * harmonicLevel + residualOutput * residualLevel`.

**VST3 implementation**:
- `kHarmonicLevelId` (400): Range 0.0-1.0 normalized, maps to 0.0-2.0 plain value (allows 200% boost). Default 1.0 (= 0.5 normalized).
- `kResidualLevelId` (401): Range 0.0-1.0 normalized, maps to 0.0-2.0 plain value. Default 1.0 (= 0.5 normalized).

**Alternatively**: After reviewing FR-021 more carefully, it says "Harmonic/Residual Mix" as a single parameter. However FR-024 says "two independent gain controls (harmonic level and residual level), not a single crossfade." We implement FR-024's design with two separate parameter IDs.

### RQ-5: How should brightness (spectral tilt) be applied?

**Decision**: Apply a linear gain tilt across the spectral envelope before FFT-domain multiplication. The tilt function is: `tiltGain(bin) = (1.0 - brightness) + brightness * (2.0 * binFreq / nyquist)` for positive brightness, and the inverse for negative brightness.

**Rationale**: FR-022 requires a spectral tilt where positive values boost high frequencies relative to low, and negative values do the reverse. A linear tilt across the spectrum is the simplest approach that achieves this. The brightness parameter ranges from -1.0 to +1.0 (mapped from the VST -100% to +100%).

**Implementation**:
```
// brightness in [-1.0, 1.0]
for each FFT bin k:
    freq_ratio = k / (numBins - 1)  // 0.0 at DC, 1.0 at Nyquist
    tilt = 1.0 + brightness * (2.0 * freq_ratio - 1.0)
    tilt = max(tilt, 0.0)  // clamp to non-negative
    envelope[k] *= tilt
```

At brightness = 0: tilt = 1.0 for all bins (neutral).
At brightness = +1.0: tilt ranges from 0.0 (DC) to 2.0 (Nyquist) -- 6dB/octave treble boost.
At brightness = -1.0: tilt ranges from 2.0 (DC) to 0.0 (Nyquist) -- 6dB/octave bass boost.

### RQ-6: How should transient emphasis be applied?

**Decision**: Multiply the residual energy scaling by `(1.0 + transientEmphasis)` during frames flagged as transients, and by `1.0` during non-transient frames.

**Rationale**: FR-023 requires boosting residual energy during transient frames. The transient flag is already computed by `SpectralTransientDetector` (FR-007). The emphasis parameter ranges from 0.0 to 2.0 (0% to 200%), so the energy multiplier during transients ranges from 1.0 (no boost) to 3.0 (200% boost = triple energy).

**Implementation**:
```
energyScale = totalEnergy;
if (frame.transientFlag && transientEmphasis > 0.0f) {
    energyScale *= (1.0f + transientEmphasis);
}
```

### RQ-7: How should state persistence be versioned for M1/M2 compatibility?

**Decision**: Bump the state version integer from 1 to 2. Version 1 states (M1-only) load cleanly by defaulting to empty residual frames. Version 2 states include residual frames serialized after the harmonic frame sequence.

**Rationale**: FR-027 requires backward-compatible state loading. The current `Processor::getState()` writes `version = 1`. For M2, we write `version = 2` and append:
1. The three new parameter values (harmonic level, residual level, brightness, transient emphasis)
2. The residual frame count (int32)
3. For each residual frame: 16 floats (bandEnergies) + 1 float (totalEnergy) + 1 int8 (transientFlag)

When loading version 1 states, the new parameters get their defaults and the residual frame vector remains empty. The synthesizer produces silence when no residual frames are available, which is consistent with M1 behavior (FR-029).

**Serialization format per ResidualFrame**: 16 * 4 + 4 + 1 = 69 bytes per frame. For a 10-second sample at 44.1 kHz with 512-sample hops: ~861 frames * 69 bytes = ~59 KB of residual data. Acceptable for IBStream.

### RQ-8: How should frame advancement for the residual synthesizer be synchronized with the harmonic oscillator bank?

**Decision**: The Innexus `Processor::process()` loop already advances `currentFrameIndex_` based on `hopSizeInSamples` (computed from `analysis->hopTimeSec * sampleRate`). The residual synthesizer will use the same `currentFrameIndex_` to look up its current `ResidualFrame`, ensuring perfect frame alignment.

**Rationale**: FR-017 requires the residual synthesizer to advance "synchronized with the harmonic oscillator bank's frame advancement." Since both use the same frame index driven by the same hop counter in the processor's per-sample loop, alignment is guaranteed by construction.

**Implementation**: The processor feeds the current `ResidualFrame` to the synthesizer at each frame boundary (same location where it calls `oscillatorBank_.loadFrame()`). The synthesizer generates one hop's worth of shaped noise per frame and buffers it via OverlapAdd for per-sample output.

## Summary of All Decisions

| # | Decision | Rationale |
|---|----------|-----------|
| RQ-1 | Time-domain harmonic subtraction with direct sinusoidal resynthesis from tracked partials | Accurate phase alignment; simpler than streaming oscillator bank for offline use |
| RQ-2 | 16 log-spaced bands (geometric series ~50Hz-21kHz), stored as normalized frequency ratios | Matches human auditory perception; sample-rate independent |
| RQ-3 | OverlapAdd with Hann synthesis window, 75% overlap, FFT-domain noise shaping | Leverages existing infrastructure; synthesis window prevents boundary clicks |
| RQ-4 | Two independent gain parameters (harmonic level 0-2x, residual level 0-2x) | Matches FR-024; allows independent control beyond crossfade |
| RQ-5 | Linear spectral tilt: `1.0 + brightness * (2 * freq_ratio - 1)` | Simple, effective, smooth, no discontinuities |
| RQ-6 | Energy multiplier `(1.0 + transientEmphasis)` during transient frames | Direct, predictable, no artifacts |
| RQ-7 | State version 1->2, residual data appended after existing blob | Backward compatible; M1 states load cleanly |
| RQ-8 | Shared `currentFrameIndex_` between harmonic and residual | Frame alignment by construction |
