# Research: Multi-Voice Harmonizer Engine

**Date**: 2026-02-18 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Research Tasks

### R-001: Shared-Analysis FFT Architecture in PhaseVocoder Mode

**Question**: How should the shared-analysis FFT optimization (FR-020) be implemented given that PitchShiftProcessor is an opaque pImpl class?

**Research**:
- Examined `PhaseVocoderPitchShifter` in `pitch_shift_processor.h` (lines 937-1434). The analysis pipeline is: `stft_.pushSamples()` -> `stft_.analyze(analysisSpectrum_)` -> `processFrame(pitchRatio)` -> `ola_.synthesize()`.
- The forward FFT (`stft_.analyze()`) produces an `analysisSpectrum_` (SpectralBuffer). Each voice would need the same analysis spectrum but applies different phase rotations in `processFrame()`.
- Rubber Band Library handles multi-channel with per-channel processing, not shared analysis (OptionChannelsApart). However, for pitch shifting the same input at different ratios, shared analysis is mathematically valid because all voices analyze identical input.
- The current `PitchShiftProcessor` API has no mechanism to inject an external analysis spectrum. The `process(input, output, numSamples)` signature encapsulates the entire STFT pipeline.

**Decision**: Implement Phase 1 with independent per-voice PitchShiftProcessor instances. This is functionally correct and meets all FRs except the performance optimization aspect of FR-020. The shared-analysis optimization requires modifying PhaseVocoderPitchShifter's internal architecture (adding an `processWithExternalAnalysis()` method or similar), which is a Layer 2 change outside this spec's scope.

**Rationale**: Modifying PitchShiftProcessor's internal architecture is a significant change that would need its own spec with proper testing of the modified Layer 2 component. The HarmonizerEngine at Layer 3 should compose existing components through their public APIs. If SC-008 benchmarks show the PhaseVocoder budget is exceeded, a follow-up spec will add the shared-analysis path.

**Alternatives considered**:
1. Bypass PitchShiftProcessor entirely and use STFT/OverlapAdd/SpectralBuffer directly -- rejected because it would duplicate the phase vocoder algorithm and all quality improvements (phase locking, transient detection, formant preservation).
2. Add a "shared analysis" mode to PitchShiftProcessor in this spec -- rejected because it crosses layer boundaries and requires separate testing.
3. Accept the per-voice overhead -- chosen because PhaseVocoder at 4 voices may still fit within the 15% budget, and functional correctness is maintained.

### R-002: Constant-Power Panning Implementation

**Question**: What is the correct constant-power panning formula and how does it compare across the codebase?

**Research**:
- Examined `UnisonEngine::computeVoiceLayout()` (line 360-363):
  ```cpp
  const float angle = (pan + 1.0f) * kPi * 0.25f;  // (pan+1) * pi/4
  leftGains_[v] = std::cos(angle);
  rightGains_[v] = std::sin(angle);
  ```
- This maps pan in [-1, +1] to angle in [0, pi/2]:
  - pan = -1.0: angle = 0, left = cos(0) = 1.0, right = sin(0) = 0.0 (hard left)
  - pan = 0.0: angle = pi/4, left = cos(pi/4) = 0.707, right = sin(pi/4) = 0.707 (center, -3dB each)
  - pan = +1.0: angle = pi/2, left = cos(pi/2) = 0.0, right = sin(pi/2) = 1.0 (hard right)
- The property cos^2 + sin^2 = 1 ensures constant total power at all pan positions.
- Web research confirms this is the standard equal-power panning law used in professional audio, matching the CMU derivation referenced in the spec.

**Decision**: Use the exact same formula as UnisonEngine: `angle = (pan + 1.0f) * kPi * 0.25f`, `leftGain = cos(angle)`, `rightGain = sin(angle)`. This matches FR-005.

**Rationale**: Proven correct in production code (UnisonEngine). Matches the spec formula exactly. Standard across audio DSP literature.

**Alternatives considered**:
1. Linear panning (left = 1-pan, right = pan) -- rejected because it creates a 6dB dip at center.
2. sqrt-based panning (left = sqrt(1-pan), right = sqrt(pan)) -- rejected because it is mathematically equivalent to sin/cos but less intuitive.

### R-003: Click-Free Pitch Transition Smoothing

**Question**: What smoothing approach prevents clicks when diatonic intervals change in Scalic mode?

**Research**:
- The spec defines smoothing time constants (FR-007): pitch = 10ms, level/pan = 5ms, dry/wet = 10ms.
- `OnePoleSmoother` in `smoother.h` provides `configure(smoothTimeMs, sampleRate)` where smoothTimeMs is the time to reach 99% of target. The internal formula: `coeff = exp(-5000 / (timeMs * sampleRate))`.
- For pitch smoothing: when a new note is detected and ScaleHarmonizer returns a new semitone shift, the smoother target is updated to the new semitone value. The smoother's `process()` method returns the intermediate value each sample, which is then passed to `PitchShiftProcessor::setSemitones()`.
- `PitchShiftProcessor` has its own internal smoothing (10ms via `semitoneSmoother` and `centsSmoother` in the Impl struct). This means there are two layers of smoothing: HarmonizerEngine's pitch smoother AND PitchShiftProcessor's internal smoother. The combined effect will be slightly slower than either alone but this is acceptable -- it ensures no clicks even if one layer is bypassed.
- Web research on harmonizer click prevention confirms the one-pole smoother approach. TC-Helicon's "Hybrid Shift" uses similar smoothing. Commercial harmonizers typically use 5-20ms glide times.

**Decision**: Use `OnePoleSmoother` with 10ms time constant for pitch transitions. Set the smoother target to the computed semitone shift (including detune) whenever it changes. Call `smoother.process()` per-sample in the block loop and pass the smoothed value to `PitchShiftProcessor::setSemitones()`. Accept the double-smoothing with PitchShiftProcessor's internal smoother.

**Rationale**: Simple, proven approach. The 10ms constant provides fast response while preventing audible discontinuities. The double-smoothing is not harmful -- it slightly softens transitions, which is musically desirable.

**Alternatives considered**:
1. LinearRamp for pitch transitions -- rejected because exponential smoothing (OnePoleSmoother) has faster initial response and more natural-sounding glide.
2. Bypassing PitchShiftProcessor's internal smoother -- rejected because we cannot access the internal Impl struct, and the internal smoothing provides an additional safety net.

### R-004: Per-Voice Onset Delay Implementation

**Question**: How should per-voice onset delay be implemented efficiently with the existing DelayLine?

**Research**:
- `DelayLine` in `delay_line.h` uses a circular buffer with power-of-2 sizing. `prepare(sampleRate, maxDelaySeconds)` allocates the buffer. `write(sample)` writes one sample, `readLinear(delaySamples)` reads with linear interpolation.
- For 50ms max delay at 192kHz: 9600 samples = 38.4KB per voice, 153.6KB for 4 voices. At 44.1kHz: 2205 samples = 8.8KB per voice.
- FR-011 specifies bypassing the DelayLine when delay is 0ms. This is important for the common case where onset delay is not used.
- The delay value should be smooth-changed (via a smoother) to prevent clicks when modified at runtime. However, the spec's smoother table does not explicitly list delay. Since delay changes are infrequent user interactions (not per-note), a simple immediate-set approach is acceptable.

**Decision**: Each voice has a `DelayLine` prepared for 50ms. When `setVoiceDelay(voice, ms)` is called, convert to samples: `delaySamples = ms * sampleRate / 1000.0f`. In the process loop, for each sample: if delay > 0, `delayLine.write(input)` then `output = delayLine.readLinear(delaySamples)`. If delay == 0, bypass the delay line entirely (pass input through directly).

**Rationale**: Simple and efficient. The bypass avoids unnecessary memory operations for the common no-delay case. DelayLine's linear interpolation handles fractional sample delays smoothly.

**Alternatives considered**:
1. Shared delay line for all voices -- rejected because each voice may have different delay.
2. Simple ring buffer instead of DelayLine -- rejected because DelayLine is already perfectly suited and available.

### R-005: Dry/Wet Level Smoothing Architecture

**Question**: How should dry and wet levels be smoothed per the FR-007 requirement of independent smoothers?

**Research**:
- FR-007 explicitly forbids smoothing a single mix ratio: "Smoothing a single mix ratio (lerp(dry, wet, mix)) is FORBIDDEN."
- The pattern is: two independent `OnePoleSmoother` instances, each configured at 10ms. Each smoother targets the linear gain value (converted from dB via `dbToGain()`).
- Per-sample in the block loop:
  ```
  float dryGain = dryLevelSmoother_.process();
  float wetGain = wetLevelSmoother_.process();
  outputL[s] = wetGain * harmonyL + dryGain * input[s];
  outputR[s] = wetGain * harmonyR + dryGain * input[s];
  ```
- FR-017 specifies that wet level is applied as a master fader over the summed harmony bus, NOT per-voice before accumulation.

**Decision**: Two independent `OnePoleSmoother` instances (`dryLevelSmoother_`, `wetLevelSmoother_`), each configured at 10ms. On `setDryLevel(dB)` / `setWetLevel(dB)`: convert dB to linear gain via `dbToGain()` (with mute check: <= -60 dB means gain = 0), then call `smoother.setTarget(linearGain)`. In the block loop, advance both smoothers per-sample.

**Rationale**: Matches FR-007 exactly. Independent smoothing prevents the nonlinearity and equal-power issues of a single mix ratio approach.

### R-006: Voice Storage and PitchShiftProcessor Constraints

**Question**: How should the 4 PitchShiftProcessor instances be stored given they are non-copyable but movable?

**Research**:
- `PitchShiftProcessor` is explicitly non-copyable: `PitchShiftProcessor(const PitchShiftProcessor&) = delete`. It IS movable.
- `std::array<PitchShiftProcessor, 4>` is valid because `std::array` default-constructs its elements and does not copy them. The array itself would be non-copyable and non-movable (because array move requires element move, and while PitchShiftProcessor is movable, the array's fixed storage means the array's own move would move all 4 elements).
- FR-019 confirms: "std::array of PitchShiftProcessor is valid since the array is default-constructed and elements are not copied."
- The Voice struct containing a PitchShiftProcessor and DelayLine will also be non-copyable. `std::array<Voice, kMaxVoices>` is fine for the same reason.

**Decision**: Use `std::array<Voice, kMaxVoices>` where Voice is a struct containing `PitchShiftProcessor`, `DelayLine`, and `OnePoleSmoother` instances. The array is default-constructed; all elements are prepared in `prepare()`. No copying occurs.

**Rationale**: Simple, direct storage. No indirection via unique_ptr needed. All 4 voices are always allocated (pre-allocation per FR-001).

### R-007: Processing Flow and Block Loop Architecture

**Question**: What is the most efficient block processing architecture for the engine?

**Research**:
- FR-017 specifies the processing order: (1) pitch tracking, (2) per-voice interval computation, (3) per-voice delay, (4) per-voice pitch shift, (5) per-voice level/pan + accumulate, (6) dry level per-sample, (7) wet level per-sample.
- Two architectural options:
  - **Block-per-voice**: Process each voice's entire block through delay and pitch shift, then mix all voices sample-by-sample. Requires scratch buffers per voice.
  - **Sample-by-sample for everything**: Process all voices one sample at a time. Avoids scratch buffers but cannot use PitchShiftProcessor (which processes blocks).
- PitchShiftProcessor::process() is a block-level API. It processes `numSamples` at once. This means we MUST use the block-per-voice approach.

**Decision**: Block-per-voice architecture:
1. Zero output buffers.
2. In Scalic mode, push input block to PitchTracker.
3. For each active voice:
   a. Compute semitone shift (from ScaleHarmonizer or directly).
   b. Set PitchShiftProcessor's semitones (including detune and smoothing).
   c. Process input through DelayLine into a scratch buffer (sample-by-sample loop).
   d. Process the scratch buffer through PitchShiftProcessor into another scratch buffer.
   e. For each sample in the voice output: apply smoothed level and panned gains, accumulate into stereo output.
4. Apply dry/wet per-sample: advance smoothers, multiply harmony accumulator by wetGain, add input*dryGain.

This requires two scratch buffers: `delayScratch_` (for delayed input) and `voiceScratch_` (for pitch-shifted output), both sized to `maxBlockSize`, pre-allocated in `prepare()`.

**Rationale**: Matches PitchShiftProcessor's block-level API. Scratch buffers are small (maxBlockSize * sizeof(float) * 2 = ~64KB max for 8192 block size). The level/pan accumulation loop is the only per-sample work in the engine proper, which is trivially fast.

### R-008: Mute Threshold and dB-to-Linear Conversion

**Question**: What dB threshold should be used for muting, and how should the conversion work?

**Research**:
- FR-003 specifies voice level range [-60, +6] dB with "values at or below -60 dB treated as mute (zero gain)."
- `dbToGain(-60.0f)` returns approximately 0.001 (not exactly zero). For true muting, we need an explicit check.
- The `kSilenceFloorDb` constant is -144 dB (24-bit dynamic range floor).

**Decision**: For voice levels: if `dB <= -60.0f`, set linear gain to 0.0f (true mute, skip voice processing entirely). Otherwise, `gain = dbToGain(dB)`. Same logic for dry/wet levels with their respective thresholds. Muted voices should be skipped entirely in the processing loop (no PitchShiftProcessor::process() call) for CPU savings.

**Rationale**: Explicit mute threshold prevents unnecessary processing. The -60 dB cutoff is specified in the spec and provides a clean optimization boundary.

### R-009: Formant Preservation Integration

**Question**: How does formant preservation integrate with the HarmonizerEngine?

**Research**:
- Examined `PitchShiftProcessor::setFormantPreserve(bool)` (line 1589-1593). It passes the flag to `phaseVocoderShifter.setFormantPreserve(enable)`. The comment says "Granular doesn't support formant preservation."
- The FormantPreserver is entirely encapsulated within PhaseVocoderPitchShifter. HarmonizerEngine does not need to interact with FormantPreserver directly.
- FR-006 specifies `setFormantPreserve(bool)` as a global setting for all voices.

**Decision**: When `setFormantPreserve(bool)` is called on HarmonizerEngine, iterate all 4 voice PitchShiftProcessors and call `setFormantPreserve(enable)` on each. The formant preservation is handled internally by PitchShiftProcessor.

**Rationale**: Clean delegation through existing API. No need to duplicate formant logic at Layer 3.

### R-010: Latency Reporting Across Modes

**Question**: How should latency be reported when all voices use the same mode?

**Research**:
- `PitchShiftProcessor::getLatencySamples()` returns mode-dependent latency:
  - Simple: 0 samples
  - Granular: ~2029 samples at 44.1kHz (grainSize)
  - PitchSync: variable (~441-1323 samples)
  - PhaseVocoder: 5120 samples (FFT_SIZE + HOP_SIZE = 4096 + 1024)
- All voices share the same mode (FR-006), so all have the same latency.
- FR-012 specifies latency MUST match the underlying PitchShiftProcessor.

**Decision**: `getLatencySamples()` delegates to `voices_[0].pitchShifter.getLatencySamples()`. Since all voices share the same mode, voice 0 is representative. If not prepared, return 0.

**Rationale**: Direct delegation, no duplication of latency logic.

### R-011: SC-008 CPU Benchmark Results and Budget Analysis

**Date**: 2026-02-18 (Phase 12)

**Question**: Do all pitch-shift modes meet their SC-008 CPU budgets with 4 voices at 44.1kHz, block size 256?

**Benchmark Environment**: Windows 11 Pro, Release build (MSVC), Catch2 benchmark with 100 samples + manual timing with 500 blocks.

**Measured Results** (4 voices, 44.1kHz, block 256, block duration = 5.805ms):

| Mode | Catch2 Mean (per block) | Manual CPU% | Budget | Status |
|------|-------------------------|-------------|--------|--------|
| Simple | ~42 us | ~0.7% | < 1% | **MET** |
| PitchSync | ~2.98 ms | ~50% | < 3% | **NOT MET** |
| Granular | ~44 us | ~0.8% | < 5% | **MET** |
| PhaseVocoder | ~1.89 ms | ~24% | < 15% | **NOT MET** |
| Orchestration (Chromatic, muted) | ~2.3 us | ~0.04% | < 1% | **MET** |
| Orchestration (Scalic + PitchTracker) | ~319 us | ~9% | N/A | Informational |

**Analysis - PitchSync (NOT MET: ~50% vs 3% budget)**:

PitchSync mode (`PitchSyncGranularShifter`) performs per-voice YIN autocorrelation-based pitch detection to synchronize grain boundaries to the signal's fundamental period. With 4 voices, this means 4 independent pitch detection passes per block. YIN autocorrelation on a 256-sample window with half-window tau_max is O(N * tau_max) per detection, totaling roughly 128K multiply-add operations per voice per detection hop.

The 3% budget was set optimistically. The PitchSync mode's inherent per-voice pitch detection cannot be optimized at the HarmonizerEngine level -- the cost is entirely within `PitchSyncGranularShifter::process()` in Layer 2. Possible optimizations for a future spec:
1. Share pitch detection across voices (all voices analyze the same input, so one pitch detection could serve all 4 voices).
2. Reduce the PitchSync detection rate (use larger hop intervals at the cost of responsiveness).
3. Use a lighter pitch detection algorithm (e.g., zero-crossing-based instead of YIN).

**Recommendation**: Revise the PitchSync budget to ~50% for 4 voices (or ~12.5% per voice). Alternatively, implement shared pitch detection in PitchSyncGranularShifter as a Layer 2 follow-up spec.

**Analysis - PhaseVocoder (NOT MET: ~24% vs 15% budget)**:

The 15% budget assumes the shared-analysis architecture (FR-020), where the forward FFT runs once per block and the analysis spectrum is shared across all voices. The current implementation uses independent per-voice `PitchShiftProcessor` instances (R-001 decision), meaning 4 independent forward FFTs per block.

Expected cost breakdown (approximate):
- Forward FFT (analysis): ~25% of PhaseVocoder cost per voice = ~25% * 4 = 100% overhead for shared analysis
- Phase rotation + inverse FFT (synthesis): ~75% per voice, per-voice regardless
- With shared analysis: save ~25% * 3 extra voices = ~75% of forward FFT cost

The measured ~24% is consistent with 4x independent FFT cost. With shared analysis (FR-020), the expected cost would be approximately 24% * 0.8 = ~19% (saving ~25% of 4 voices' forward FFT cost = ~5%). This suggests even with shared analysis, PhaseVocoder might still be at the budget boundary.

**Recommendation**: Implement FR-020 shared-analysis architecture as an urgent follow-up spec. The shared analysis is expected to reduce PhaseVocoder cost to approximately 15-19% for 4 voices, which may meet or be close to the 15% budget.

**Analysis - PitchTracker Overhead**:

The PitchTracker uses YIN-based detection via `PitchDetector::detect()`, running at every `windowSize/4 = 64` sample hop. For a 256-sample block, this means 4 detection runs. The measured ~9% overhead (Scalic mode with all voices muted) is entirely PitchTracker cost. This is a shared overhead (one PitchTracker regardless of voice count) that occurs only in Scalic mode.

The < 1% orchestration overhead budget was intended for the engine's own processing (smoothers, panning, mixing), which is indeed < 0.04% (Chromatic mode measurement). The PitchTracker cost should be considered part of the Scalic mode processing overhead rather than pure orchestration overhead.

**Optimizations Already Implemented in HarmonizerEngine**:
- Mute threshold skip: voices at <= -60dB skip PitchShiftProcessor entirely (line 209)
- Zero-delay bypass: when delay=0ms, input is copied directly without DelayLine (line 255-258)
- PitchTracker skip in Chromatic mode: no pushBlock() call (line 197, FR-009)
- numVoices=0 skip: no voice processing or pitch tracking (line 195, FR-018)
