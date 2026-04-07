# Research: Innexus Milestone 1 -- Core Playable Instrument

**Date**: 2026-03-03 | **Branch**: `115-innexus-m1-core-instrument`

## Research Questions & Findings

### R1: YIN Pitch Detection Algorithm

**Decision**: Implement YIN (de Cheveigne & Kawahara 2002) with FFT-accelerated difference function.

**Rationale**: YIN achieves ~34x lower gross pitch error rates than raw autocorrelation at similar computational cost when FFT-accelerated. The CMNDF + absolute threshold approach naturally handles octave errors. FFT acceleration via Wiener-Khinchin theorem reduces O(N^2) to O(N log N) per frame.

**Algorithm Steps**:
1. Compute difference function d(tau) = sum over i of (x[i] - x[i+tau])^2
2. Express d(tau) via autocorrelation: d(tau) = r(0) + r'(0, tau) - 2*r(tau)
   where r'(0, tau) = sum(x[i+tau]^2, i=0..W-tau-1)
3. Compute autocorrelation r(tau) via FFT (zero-pad to 2N, forward FFT, power spectrum, inverse FFT)
4. Compute CMNDF: d'(tau) = d(tau) / ((1/tau) * sum(d(j), j=1..tau))
5. Find first tau where d'(tau) < threshold (absolute threshold, default 0.3)
6. Apply parabolic interpolation on the 3 points around the minimum

**Alternatives Considered**:
- Raw autocorrelation: Higher error rate, no improvement over YIN
- SWIPE: More accurate for some sources but 5x more CPU; unsuitable for real-time constraint
- pYIN: HMM layer adds complexity; worth considering only if vanilla YIN proves insufficient
- McLeod Pitch Method (MPM): Good alternative, NSDF normalization, operates on fewer periods. Deferred as potential secondary algorithm.

**Key Implementation Detail**: The existing `FFTAutocorrelation` class in KrateDSP provides the pattern for FFT-accelerated correlation. YIN's difference function requires a slightly different normalization (CMNDF instead of standard normalization), so a new class is needed rather than direct reuse. However, the pffft buffer management and FFT call patterns can be directly followed.

### R2: dr_wav Library Integration

**Decision**: Use dr_wav from dr_libs (mackron/dr_libs on GitHub), MIT license, single-header.

**Rationale**: dr_wav is a well-established single-header C library for WAV and AIFF decoding. No build system changes needed. MIT license is compatible with the project. Cross-platform (Windows/macOS/Linux). Supports all common PCM formats.

**Integration Pattern**:
1. Download `dr_wav.h` to `extern/dr_libs/dr_wav.h`
2. In exactly one .cpp file (sample_analyzer.cpp):
   ```cpp
   #define DR_WAV_IMPLEMENTATION
   #include "dr_wav.h"
   ```
3. In all other files that need the API:
   ```cpp
   #include "dr_wav.h"
   ```
4. Use `drwav_open_file_and_read_pcm_frames_f32()` for one-call loading

**AIFF Support**: dr_wav supports AIFF container natively (read-only). Same API for both WAV and AIFF. Detected automatically from file header magic bytes.

**Alternatives Considered**:
- libsndfile: Full-featured but requires build system integration, not single-header
- Custom WAV parser: Reinventing the wheel, error-prone for edge cases
- dr_flac: Available from same library but out of scope for M1

### R3: Atomic Pointer Swap Mechanism

**Decision**: Use `std::atomic<SampleAnalysis*>` with release/acquire semantics for publishing immutable analysis results from background thread to audio thread.

**Rationale**: This is the simplest correct mechanism for single-producer/single-consumer immutable data publication. No mutex needed because the data is immutable after publication. Release semantics on the write side ensures all writes to the SampleAnalysis object are visible before the pointer becomes visible. Acquire semantics on the read side ensures the audio thread sees all the data correctly.

**Memory Management Pattern**:
```cpp
// Background thread (producer):
auto* newAnalysis = new SampleAnalysis(std::move(result));
auto* old = currentAnalysis_.exchange(newAnalysis, std::memory_order_release);
// Schedule old for deletion (NOT on audio thread)

// Audio thread (consumer):
auto* analysis = currentAnalysis_.load(std::memory_order_acquire);
// Use analysis->frames[...] -- guaranteed to see fully constructed data
```

**Deferred Deletion**: The old SampleAnalysis pointer returned by `exchange()` must be deleted, but NOT on the audio thread. Options:
1. Delete it at the start of the next background analysis (simplest)
2. Use a lock-free deletion queue polled by a cleanup thread
3. Delete in the processor's setActive(false) or terminate()

For M1, option 1 is sufficient since analyses are infrequent (user-triggered file loads).

**Alternatives Considered**:
- `std::shared_ptr<SampleAnalysis>`: Atomic reference counting (shared_ptr control block) may allocate. Not guaranteed lock-free on all platforms.
- Lock-free SPSC queue: Overly complex for publishing a single immutable object
- Double-buffering: Works but more complex; pointer swap is the minimal correct solution

### R4: Blackman-Harris Window Function

**Decision**: Add Blackman-Harris (4-term) window to KrateDSP's window_functions.h.

**Rationale**: Blackman-Harris provides ~92 dB sidelobe rejection vs ~58 dB for Blackman and ~43 dB for Hann. This is critical for peak detection in the partial tracker where weak upper partials near strong lower ones must be resolved without spectral leakage artifacts.

**Coefficients (4-term)**:
```
w[n] = a0 - a1*cos(2*pi*n/N) + a2*cos(4*pi*n/N) - a3*cos(6*pi*n/N)
a0 = 0.35875
a1 = 0.48829
a2 = 0.14128
a3 = 0.01168
```

**Main lobe width**: Approximately 4 bins (wider than Hann at 2 bins), but the superior sidelobe rejection is worth the frequency resolution tradeoff for peak detection purposes.

### R5: Parabolic Interpolation for Sub-Bin/Sub-Sample Precision

**Decision**: Add a general-purpose `parabolicInterpolation()` function to spectral_utils.h.

**Rationale**: Parabolic interpolation is used in three places:
1. YIN: Refine CMNDF minimum location for sub-sample pitch precision
2. Peak detection: Refine spectral peak location for sub-bin frequency precision
3. Existing PitchDetector: Already uses inline parabolic interpolation (could be extracted)

**Formula**:
```
Given three points (x-1, y0), (x0, y1), (x+1, y2):
delta = (y0 - y2) / (2 * (y0 - 2*y1 + y2))
interpolated_x = x0 + delta
interpolated_y = y1 - 0.25 * (y0 - y2) * delta
```

### R6: Gordon-Smith Modified Coupled Form (MCF) Oscillator

**Decision**: Reuse the exact MCF pattern from ParticleOscillator for the HarmonicOscillatorBank.

**Rationale**: The ParticleOscillator has been benchmarked at 0.38% CPU for 64 oscillators at 44.1kHz. The HarmonicOscillatorBank needs 48 oscillators, which will be well under the 0.5% budget. The MCF pattern is proven and its SoA layout is cache-efficient.

**MCF Update Equations**:
```
epsilon = 2 * sin(pi * freq / sampleRate)
s_new = s + epsilon * c
c_new = c - epsilon * s_new  // uses updated s
```
- Determinant = 1 (amplitude-stable, no drift)
- No wavetable needed (2 muls + 2 adds per oscillator per sample)
- Phase-continuous: changing epsilon smoothly changes frequency without discontinuity

**Key Difference from ParticleOscillator**: ParticleOscillator has per-particle envelopes and spawn/death cycles. HarmonicOscillatorBank has per-partial amplitude smoothing and harmonic frame loading. The inner processing loop is nearly identical.

### R7: Dual-Window STFT Configuration

**Decision**: Long window 4096 samples, short window 1024 samples, both at 50% overlap.

**Rationale**:
- Long window (4096 at 44.1kHz): Frequency resolution = 44100/4096 = 10.77 Hz. This resolves harmonics of F0 down to ~40 Hz (where the first few harmonics are ~40, 80, 120 Hz apart).
- Short window (1024 at 44.1kHz): Frequency resolution = 44100/1024 = 43.07 Hz. This provides faster temporal tracking for upper harmonics where frequency resolution is less critical.
- 50% overlap: Standard for analysis (provides good time-frequency tradeoff). The long window updates at 44100/2048 = ~21.5 Hz, the short window at 44100/512 = ~86.1 Hz.

**Alternatives Considered**:
- Single window: Misses either low-frequency resolution or temporal tracking
- 75% overlap: Better time resolution but higher CPU cost; 50% is sufficient for offline analysis
- Different FFT sizes from window sizes (zero-padding): Adds complexity for marginal benefit in peak detection

### R8: Existing Plugin Scaffold Status

**Decision**: Innexus plugin scaffold already exists and is buildable. Phase 1 is partially complete.

**Findings**:
- `plugins/innexus/` directory structure exists with all necessary subdirectories
- `CMakeLists.txt` is configured and the plugin is already added to the root CMakeLists.txt
- `entry.cpp`, `plugin_ids.h`, `version.h`, processor/controller files exist
- Plugin registers as "Instrument|Synth" with unique FUIDs
- Sidechain audio input bus already configured (for future M3 live sidechain)
- MIDI event input already configured
- Stereo audio output already configured
- Currently outputs silence (correct for empty processor)
- Tests directory exists with CMakeLists.txt and test_main.cpp

**Remaining Phase 1 work**: Add M1 parameter IDs, register parameters, wire parameter handling.
