# Research: Shared-Analysis FFT Refactor

**Date**: 2026-02-18 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Research Tasks

### R-001: processFrame Refactoring -- Accepting External Analysis Spectrum

**Question**: How should `PhaseVocoderPitchShifter::processFrame()` be refactored to accept a `const SpectralBuffer&` parameter instead of reading from the internal `analysisSpectrum_` member?

**Research**:
- Examined `processFrame()` (pitch_shift_processor.h lines 1157-1387). The method currently reads from `analysisSpectrum_` in Step 1:
  ```cpp
  magnitude_[k] = analysisSpectrum_.getMagnitude(k);
  float phase = analysisSpectrum_.getPhase(k);
  ```
- The method never writes to `analysisSpectrum_`. It only reads magnitude and phase values. All writes go to per-voice state: `magnitude_[]`, `frequency_[]`, `synthPhase_[]`, `prevPhase_[]`, `synthesisSpectrum_`.
- `SpectralBuffer::getMagnitude()` and `getPhase()` are `const` methods. The internal `mags_`, `phases_`, and validation flags are `mutable`, enabling lazy polar conversion even through `const` references.
- The refactored signature per FR-023: `void processFrame(const SpectralBuffer& analysis, SpectralBuffer& synthesis) noexcept;`
- The `synthesis` parameter replaces writing to `synthesisSpectrum_` member. The method currently writes via `synthesisSpectrum_.setCartesian(k, real, imag)` and `synthesisSpectrum_.reset()`.

**Decision**: Refactor `processFrame()` to accept two parameters: `const SpectralBuffer& analysis` (read-only input) and `SpectralBuffer& synthesis` (write-only output). Replace all reads from `analysisSpectrum_` with reads from `analysis`, and all writes to `synthesisSpectrum_` with writes to `synthesis`. The existing `process()` method will call `processFrame(analysisSpectrum_, synthesisSpectrum_)` to maintain backward compatibility.

**Rationale**: This is a mechanical refactor. The current method already has a clean read/write separation -- it reads `analysisSpectrum_` and writes `synthesisSpectrum_`. Parameterizing both makes the data flow explicit and enables injection of shared analysis spectra. The `const` qualifier on the analysis parameter enforces the read-only constraint at compile time.

**Alternatives considered**:
1. Copy shared spectrum into `analysisSpectrum_` before calling `processFrame()` -- rejected per FR-023. This wastes ~11 MB/sec of memory bandwidth and violates the "borrow, don't copy" semantic.
2. Add only the analysis parameter (keep synthesis as member) -- rejected because explicitly passing both parameters makes the data flow maximally clear and the method becomes a pure transformation from analysis to synthesis.

---

### R-002: processWithSharedAnalysis() Method Design

**Question**: What is the correct implementation of `processWithSharedAnalysis()` at the PhaseVocoderPitchShifter level?

**Research**:
- The existing `process()` method (lines 1092-1127) performs this pipeline:
  1. Unity pitch bypass check
  2. `stft_.pushSamples()` -- push input to STFT
  3. `while (stft_.canAnalyze())` loop: `stft_.analyze() -> processFrame() -> ola_.synthesize()`
  4. `ola_.pullSamples()` -- extract output samples
  5. Zero-fill any remaining output (startup latency)
- `processWithSharedAnalysis()` replaces steps 1-3. The caller (HarmonizerEngine) owns the shared STFT and drives the frame timing. The method receives one analysis frame at a time and performs: `processFrame(analysis, synthesisSpectrum_) -> ola_.synthesize(synthesisSpectrum_)`.
- The output sample management (step 4-5) still needs to happen. But the timing is different: the caller calls this method once per ready analysis frame, not once per block. The method needs to process exactly one frame.
- Per FR-007: "The caller provides one analysis frame at a time. The method MUST process exactly one frame per call and perform OLA synthesis for that frame."
- The output pulling is separated: the caller must pull samples from the OLA buffer after calling this method.

**Decision**: Implement `processWithSharedAnalysis()` as a per-frame method that:
1. Validates the spectrum (FR-008: `numBins()` check)
2. Calls `processFrame(analysis, synthesisSpectrum_)` with the shared spectrum
3. Calls `ola_.synthesize(synthesisSpectrum_)` to add the synthesis frame to the OLA buffer
4. Does NOT pull output samples (that is the caller's responsibility)

A separate method `pullOutputSamples(float* output, std::size_t numSamples)` exposes the OLA buffer's output to the caller.

The `processWithSharedAnalysis()` signature:
```cpp
void processWithSharedAnalysis(const SpectralBuffer& analysis, float pitchRatio) noexcept;
```

And a companion method:
```cpp
std::size_t pullOutputSamples(float* output, std::size_t maxSamples) noexcept;
```

**Rationale**: Separating frame processing from output pulling matches the frame-driven architecture. The caller (HarmonizerEngine) pushes samples to its shared STFT, loops over ready frames calling `processWithSharedAnalysis()` for each voice, then pulls output samples once at the end.

**Alternatives considered**:
1. Have `processWithSharedAnalysis()` accept an output buffer and sample count -- rejected because the method processes one frame, but output samples emerge in hop-size chunks asynchronously from frame processing. Combining frame processing with sample-level output management in one method conflates two different timing domains.
2. Expose OLA directly -- rejected because it breaks encapsulation.

---

### R-003: PitchShiftProcessor Delegation Design

**Question**: How should PitchShiftProcessor (the pImpl wrapper) expose the shared-analysis path?

**Research**:
- `PitchShiftProcessor` uses pImpl pattern. The `Impl` struct (line 1440-1459) holds: mode, parameters, and internal shifter instances (`simpleShifter`, `granularShifter`, `phaseVocoderShifter`, `pitchSyncShifter`).
- The existing `process()` method (lines 1505-1556) routes to the appropriate internal shifter based on `mode`, with sub-block parameter smoothing.
- For the shared-analysis path, the method must delegate to `phaseVocoderShifter.processWithSharedAnalysis()` when in PhaseVocoder mode, and be a zero-fill no-op for other modes (FR-009/FR-009a).
- Important: The shared-analysis path bypasses PitchShiftProcessor's internal parameter smoothing because the caller (HarmonizerEngine) handles smoothing at its own level and passes the pitch ratio directly.
- FR-011 requires exposing FFT size and hop size constants.

**Decision**: Add three new methods to PitchShiftProcessor:
1. `void processWithSharedAnalysis(const SpectralBuffer& analysis, float pitchRatio) noexcept` -- delegates to `phaseVocoderShifter.processWithSharedAnalysis()` or zero-fills in non-PhaseVocoder modes
2. `std::size_t pullSharedAnalysisOutput(float* output, std::size_t maxSamples) noexcept` -- pulls samples from the PhaseVocoder's OLA buffer
3. `static constexpr std::size_t getPhaseVocoderFFTSize() noexcept` / `static constexpr std::size_t getPhaseVocoderHopSize() noexcept` -- expose FFT/hop constants (FR-011)

Also add `std::size_t samplesAvailable() const noexcept` to query available output.

**Rationale**: The static constexpr accessors are the simplest way to expose compile-time constants without runtime overhead. The delegation methods mirror the PhaseVocoderPitchShifter API through the pImpl boundary.

**Alternatives considered**:
1. Make PhaseVocoderPitchShifter public in PitchShiftProcessor -- rejected, breaks encapsulation.
2. Pass a pitch ratio through PitchShiftProcessor's setSemitones/process pipeline -- rejected, the shared-analysis path needs direct ratio control without smoothing overhead.

---

### R-004: HarmonizerEngine Shared STFT Integration

**Question**: How should HarmonizerEngine own and drive the shared STFT instance?

**Research**:
- `HarmonizerEngine::process()` (lines 180-288) currently:
  1. Zeros output buffers
  2. For each voice: compute pitch -> delay -> `voice.pitchShifter.process()` -> level/pan accumulation
  3. Dry/wet blend
- In PhaseVocoder mode with shared analysis, the flow becomes:
  1. Push input to shared STFT (once)
  2. For each ready analysis frame: pass shared spectrum to each active voice's `processWithSharedAnalysis()`
  3. After all frames processed: pull output from each voice's OLA buffer
  4. Apply level, pan, delay, dry/wet as before
- The shared STFT parameters must match PhaseVocoderPitchShifter: FFT size 4096, hop size 1024.
- HarmonizerEngine already has `delayScratch_` and `voiceScratch_` buffers. For the shared-analysis path, the delay must be applied BEFORE the shared STFT (or after pulling from OLA). Current flow applies delay before pitch shifting, which is correct because the STFT analyzes the delayed input.
- Problem: With shared analysis, all voices share ONE forward FFT. But if voices have different delays, they'd need different delayed inputs analyzed separately. This means the shared FFT cannot be shared if delays differ per voice.
- Wait -- let me re-examine. In the current implementation, the delay is applied per-voice BEFORE pitch shifting. The pitch shifter receives the delayed signal. With shared analysis, the FFT analyzes the raw input once. Per-voice delays would need to either: (a) be applied in the time domain after OLA output, or (b) be applied as phase shifts in the spectral domain. Option (a) is simpler and preserves the per-voice delay behavior.
- Actually, examining the delays more carefully: per-voice delays are small (0-50ms) and are onset delays for humanization. The delay line writes input and reads with a delay. With shared analysis, we can't delay the input before FFT because each voice has different delay. Two approaches:
  1. Apply delay AFTER pitch shifting (time-domain, post-OLA)
  2. Apply delay BEFORE shared analysis (requires separate FFT per delay value)
- Approach 1 is correct because the delay is for humanization timing, not for affecting the spectral content. The pitch shifting result is the same regardless of a small onset delay.
- Verified by checking the spec: delays are 0-50ms for "onset delay" per voice (FR-011 in 064 spec). This is purely a timing offset and can be applied post-pitch-shifting without audible difference.

**Decision**: In PhaseVocoder mode with shared analysis:
1. Push RAW input (no delay) to the shared STFT
2. Process shared analysis frames through each voice's `processWithSharedAnalysis()`
3. Pull OLA output from each voice
4. Apply per-voice delay to the OLA output (moved from pre-pitch to post-pitch)
5. Apply level, pan, accumulate

For non-PhaseVocoder modes, the delay remains pre-pitch-shifting as before.

**Rationale**: Moving the 0-50ms onset delay from pre-pitch to post-pitch is audibly transparent for humanization purposes. The alternative (per-delay FFT analysis) would eliminate the shared analysis benefit entirely.

**Alternatives considered**:
1. Skip delays in PhaseVocoder mode -- rejected, would change behavior.
2. Apply all delays as spectral-domain phase shifts -- rejected, overly complex and not needed for small onset delays.
3. Use the shortest-delay voice's input for shared FFT -- rejected, arbitrary and introduces delay-dependent spectral artifacts.

---

### R-005: Sub-Hop-Size Block Handling (FR-013a)

**Question**: How should HarmonizerEngine handle blocks smaller than the hop size in PhaseVocoder mode?

**Research**:
- The STFT class handles sub-hop blocks natively: `pushSamples()` buffers input, and `canAnalyze()` returns false until `fftSize` samples are available. After the first frame, it needs `hopSize` new samples per subsequent frame.
- When `canAnalyze()` is false, no frame processing occurs. The OLA buffers may still contain samples from previous frames.
- The output contract: zero-fill samples for which no new synthesis frame was computed.
- OverlapAdd::samplesAvailable() tracks how many samples are ready to pull.

**Decision**: The HarmonizerEngine's PhaseVocoder path will:
1. Push input samples to shared STFT
2. Loop `while (sharedStft_.canAnalyze())` -- may execute 0 times for sub-hop blocks
3. Pull `min(numSamples, ola_.samplesAvailable())` from each voice's OLA
4. Zero-fill remaining output samples

This naturally handles sub-hop blocks because the STFT buffers samples until a frame is ready.

**Rationale**: The STFT's built-in buffering handles the timing automatically. No special sub-hop logic is needed.

---

### R-006: Maintaining Output Equivalence (SC-002/SC-003)

**Question**: How can we verify that the refactored output is equivalent to the pre-refactor output?

**Research**:
- The refactor changes the code path for PhaseVocoder mode only. All other modes are unchanged.
- For PhaseVocoder mode, the analysis spectrum is computed identically (same STFT, same FFT, same window). The `processFrame()` is the same algorithm with the same inputs. The OLA synthesis is the same.
- The only potential difference: floating-point ordering. If the shared STFT uses a different STFT instance than each voice's internal one, the FFT result could differ at the ULP (unit of least precision) level due to different internal state or memory layout. However, since we are using the same FFT implementation with the same parameters, the results should be bit-identical.
- Test approach: For SC-003 (Layer 2 equivalence), run a standalone PhaseVocoderPitchShifter with standard `process()` and capture its STFT analysis output. Then feed that same analysis to `processWithSharedAnalysis()` and compare outputs. They should be identical.
- For SC-002 (Layer 3 equivalence), run HarmonizerEngine with pre-refactor code and post-refactor code on the same input, compare per-voice output.

**Decision**: Create equivalence tests that:
1. Run standard `process()` and capture per-sample output
2. Run `processWithSharedAnalysis()` with the same analysis spectrum and capture output
3. Assert max sample error < 1e-5

**Rationale**: Since the algorithm is identical and only the data flow changes (parameter instead of member), the output should be bit-identical. The 1e-5 tolerance allows for any minor floating-point reordering.

---

### R-007: SpectralBuffer Const-Correctness for Shared Analysis

**Question**: Does the SpectralBuffer class support const-reference access patterns needed for shared analysis?

**Research**:
- Examined `spectral_buffer.h`. All internal storage is `mutable`:
  ```cpp
  mutable std::vector<Complex> data_;
  mutable std::vector<float> mags_;
  mutable std::vector<float> phases_;
  mutable bool polarValid_ = true;
  mutable bool cartesianValid_ = true;
  ```
- `getMagnitude()` and `getPhase()` are `const` methods that call `ensurePolarValid()`, which is also `const` and lazily computes polar representation from Cartesian data.
- `numBins()` is `const`.
- STFT::analyze() writes to the SpectralBuffer via `fft_.forward()` which writes to `data()` (non-const). After `analyze()` completes, the buffer transitions to read-only for consumers.
- The `data()` method has two overloads: `const Complex* data() const` and `Complex* data()`. The const overload is sufficient for reading.

**Decision**: No modifications to SpectralBuffer are needed. The existing const-correctness is sufficient for the shared-analysis pattern. Pass the analysis spectrum as `const SpectralBuffer&` to all voice processing methods.

**Rationale**: The `mutable` caching pattern in SpectralBuffer was designed precisely for this kind of read-shared access. The lazy polar conversion will be triggered once by the first voice's `getMagnitude()` call and cached for subsequent voices, providing additional optimization benefit.

---

### R-008: PitchSync Mode Analysis

**Question**: What is the PitchSync mode's CPU overhead and can shared analysis help?

**Research**:
- PitchSync mode uses `PitchSyncGranularShifter` which performs per-voice YIN autocorrelation for pitch detection. This is orthogonal to FFT shared analysis.
- The shared-analysis refactor only affects PhaseVocoder mode's forward FFT. PitchSync does not use FFT for pitch detection.
- From 064 spec benchmarks: PitchSync ~26.4% CPU vs 3% budget. Root cause is 4x YIN autocorrelation (one per voice on identical input).
- A separate "shared pitch detection" optimization (analogous to shared FFT) could run YIN once and share the detected pitch. This is architecturally feasible but deferred per FR-020/021/022 deferral rationale.

**Decision**: Re-benchmark PitchSync after the refactor. Document the measurement. No optimization expected (the refactor does not touch PitchSync code).

**Rationale**: PitchSync optimization requires its own spec. This spec's contribution is the measurement and analysis per SC-008.
