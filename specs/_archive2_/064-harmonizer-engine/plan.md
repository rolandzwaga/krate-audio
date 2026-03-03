# Implementation Plan: Multi-Voice Harmonizer Engine

**Branch**: `064-harmonizer-engine` | **Date**: 2026-02-18 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/064-harmonizer-engine/spec.md`

## Summary

Implement a `HarmonizerEngine` class (Layer 3, Systems) that orchestrates existing DSP components into a complete multi-voice harmonizer system. The engine composes a shared `PitchTracker` (L1), shared `ScaleHarmonizer` (L0), up to 4 `PitchShiftProcessor` instances (L2), per-voice `DelayLine` (L1), and `OnePoleSmoother` instances (L1) for click-free transitions. It supports two harmony modes (Chromatic and Scalic), mono-to-stereo constant-power panning, per-voice level/pan/delay/detune, global dry/wet control, and a shared-analysis FFT architecture in PhaseVocoder mode. The implementation is header-only, noexcept throughout its process path, and performs zero heap allocations after `prepare()`.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: PitchShiftProcessor (L2), PitchTracker (L1), ScaleHarmonizer (L0), OnePoleSmoother (L1), DelayLine (L1), pitch_utils.h (L0), db_utils.h (L0), math_constants.h (L0)
**Storage**: N/A (in-memory state only, pre-allocated buffers)
**Testing**: Catch2 (dsp_tests target) *(Constitution Principle VIII: Testing Discipline)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Shared DSP library (KrateDSP)
**Performance Goals**: Per-mode CPU budget: Simple <1%, PitchSync <3%, Granular <5%, PhaseVocoder <15% (4 voices, 44.1kHz, block 256). Orchestration overhead <1% regardless of mode.
**Constraints**: Real-time safe process() (no alloc, no locks, no exceptions, no I/O), noexcept on all process-path methods. Max 4 voices. Pre-allocated in prepare().
**Scale/Scope**: Single header-only class (~400-600 lines) + test file (~800-1200 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Post-Design Re-Check (Phase 1 complete)**: All principles re-validated against finalized design artifacts (plan.md, research.md, data-model.md, contracts/harmonizer_engine_api.h, quickstart.md). No violations found. FR-020 (shared-analysis FFT) is transparently DEFERRED with documented rationale in research.md R-001.

**Principle I (VST3 Architecture Separation)**: N/A -- this is a DSP library component, not a plugin component.

**Principle II (Real-Time Audio Thread Safety)**: PASS -- all processing methods are noexcept. All buffers pre-allocated in `prepare()`. No locks, no I/O, no exceptions in process path. `PitchShiftProcessor` uses pImpl with `std::unique_ptr` (allocated in its own `prepare()`), but HarmonizerEngine's `prepare()` is called from setup, not audio thread.

**Principle III (Modern C++ Standards)**: PASS -- uses `std::array`, `std::vector` (pre-allocated), `constexpr`, `[[nodiscard]]`, `noexcept`, move semantics for PitchShiftProcessor. No raw new/delete.

**Principle IV (SIMD & DSP Optimization)**: PASS -- SIMD analysis performed, verdict is NOT BENEFICIAL for the orchestration layer (see SIMD section below). Inner components (FFT, spectral processing) already have SIMD via pffft/Highway.

**Principle VI (Cross-Platform Compatibility)**: PASS -- no platform-specific code. Uses standard C++20 and existing cross-platform components.

**Principle VII (Project Structure & Build System)**: PASS -- follows monorepo layout. Header at `dsp/include/krate/dsp/systems/harmonizer_engine.h`, tests at `dsp/tests/unit/systems/harmonizer_engine_test.cpp`.

**Principle VIII (Testing Discipline)**: PASS -- tests written before implementation per canonical todo list.

**Principle IX (Layered DSP Architecture)**: PASS -- Layer 3 depending on Layer 0 (ScaleHarmonizer, pitch_utils, db_utils, math_constants), Layer 1 (PitchTracker, OnePoleSmoother, DelayLine), and Layer 2 (PitchShiftProcessor). No Layer 4 dependencies.

**Principle XV (Pre-Implementation Research / ODR Prevention)**: PASS -- `grep -r "class HarmonizerEngine" dsp/ plugins/` and `grep -r "enum class HarmonyMode" dsp/ plugins/` returned no results. No existing classes with these names.

**Principle XVI (Honest Completion)**: Acknowledged -- compliance table will be filled with specific file paths, line numbers, and test output.

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) -- no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `HarmonizerEngine`, `HarmonyMode` (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `HarmonizerEngine` | `grep -r "class HarmonizerEngine" dsp/ plugins/` | No | Create New |
| `HarmonyMode` | `grep -r "HarmonyMode" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None -- all utilities already exist in the codebase.

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `PitchShiftProcessor` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` | 2 | Per-voice pitch shifting (4 modes). `prepare()`, `reset()`, `process()`, `setSemitones()`, `setMode()`, `setFormantPreserve()`, `getLatencySamples()` |
| `PitchTracker` | `dsp/include/krate/dsp/primitives/pitch_tracker.h` | 1 | Shared pitch detection. `prepare()`, `reset()`, `pushBlock()`, `getFrequency()`, `getMidiNote()`, `getConfidence()`, `isPitchValid()` |
| `ScaleHarmonizer` | `dsp/include/krate/dsp/core/scale_harmonizer.h` | 0 | Shared diatonic interval computation. `setKey()`, `setScale()`, `calculate()` |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Per-voice smoothers for pitch/level/pan; global smoothers for dry/wet. `configure()`, `setTarget()`, `process()`, `snapTo()`, `reset()` |
| `DelayLine` | `dsp/include/krate/dsp/primitives/delay_line.h` | 1 | Per-voice onset delay. `prepare()`, `reset()`, `write()`, `readLinear()` |
| `dbToGain()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Convert dB levels to linear gain |
| `kPi` | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Constant-power panning angle calculation |
| `PitchMode` (enum) | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` | 2 | Reuse directly for pitch shift mode selection |
| `ScaleType` (enum) | `dsp/include/krate/dsp/core/scale_harmonizer.h` | 0 | Reuse directly for scale type selection |
| `UnisonEngine` | `dsp/include/krate/dsp/systems/unison_engine.h` | 3 | **Reference pattern only** (not composed). Match constant-power pan formula and multi-voice structure |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` -- Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` -- Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` -- Layer 2 processors
- [x] `dsp/include/krate/dsp/systems/` -- Layer 3 systems (no harmonizer_engine.h exists)
- [x] `specs/_architecture_/` -- Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: Both new types (`HarmonizerEngine` class, `HarmonyMode` enum) do not exist anywhere in the codebase. All utility functions, enums, and component types already exist and will be reused, not duplicated. The constant-power panning formula is inlined in each consumer (UnisonEngine, TapManager) rather than being a shared function -- this is acceptable as it is a 2-line formula, not worth extracting until a 4th consumer appears.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `PitchShiftProcessor` | constructor | `PitchShiftProcessor() noexcept` | Yes |
| `PitchShiftProcessor` | prepare | `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` | Yes |
| `PitchShiftProcessor` | reset | `void reset() noexcept` | Yes |
| `PitchShiftProcessor` | process | `void process(const float* input, float* output, std::size_t numSamples) noexcept` | Yes |
| `PitchShiftProcessor` | setSemitones | `void setSemitones(float semitones) noexcept` | Yes |
| `PitchShiftProcessor` | setCents | `void setCents(float cents) noexcept` | Yes |
| `PitchShiftProcessor` | setMode | `void setMode(PitchMode mode) noexcept` | Yes |
| `PitchShiftProcessor` | setFormantPreserve | `void setFormantPreserve(bool enable) noexcept` | Yes |
| `PitchShiftProcessor` | getLatencySamples | `[[nodiscard]] std::size_t getLatencySamples() const noexcept` | Yes |
| `PitchShiftProcessor` | isPrepared | `[[nodiscard]] bool isPrepared() const noexcept` | Yes |
| `PitchTracker` | prepare | `void prepare(double sampleRate, std::size_t windowSize = kDefaultWindowSize) noexcept` | Yes |
| `PitchTracker` | reset | `void reset() noexcept` | Yes |
| `PitchTracker` | pushBlock | `void pushBlock(const float* samples, std::size_t numSamples) noexcept` | Yes |
| `PitchTracker` | getFrequency | `[[nodiscard]] float getFrequency() const noexcept` | Yes |
| `PitchTracker` | getMidiNote | `[[nodiscard]] int getMidiNote() const noexcept` | Yes |
| `PitchTracker` | getConfidence | `[[nodiscard]] float getConfidence() const noexcept` | Yes |
| `PitchTracker` | isPitchValid | `[[nodiscard]] bool isPitchValid() const noexcept` | Yes |
| `ScaleHarmonizer` | setKey | `void setKey(int rootNote) noexcept` | Yes |
| `ScaleHarmonizer` | setScale | `void setScale(ScaleType type) noexcept` | Yes |
| `ScaleHarmonizer` | calculate | `[[nodiscard]] DiatonicInterval calculate(int inputMidiNote, int diatonicSteps) const noexcept` | Yes |
| `DiatonicInterval` | semitones | `int semitones` (member) | Yes |
| `OnePoleSmoother` | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| `OnePoleSmoother` | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| `OnePoleSmoother` | process | `[[nodiscard]] float process() noexcept` | Yes |
| `OnePoleSmoother` | snapTo | `void snapTo(float value) noexcept` | Yes |
| `OnePoleSmoother` | reset | `void reset() noexcept` | Yes |
| `OnePoleSmoother` | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| `DelayLine` | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| `DelayLine` | reset | `void reset() noexcept` | Yes |
| `DelayLine` | write | `void write(float sample) noexcept` | Yes |
| `DelayLine` | readLinear | `[[nodiscard]] float readLinear(float delaySamples) const noexcept` | Yes |
| `dbToGain` | N/A | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |
| `kPi` | N/A | `inline constexpr float kPi = 3.14159265358979323846f` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/pitch_shift_processor.h` -- PitchShiftProcessor, PitchMode enum, all inner shifters
- [x] `dsp/include/krate/dsp/primitives/pitch_tracker.h` -- PitchTracker class
- [x] `dsp/include/krate/dsp/core/scale_harmonizer.h` -- ScaleHarmonizer, ScaleType enum, DiatonicInterval struct
- [x] `dsp/include/krate/dsp/primitives/smoother.h` -- OnePoleSmoother, LinearRamp, SlewLimiter
- [x] `dsp/include/krate/dsp/primitives/delay_line.h` -- DelayLine class
- [x] `dsp/include/krate/dsp/core/db_utils.h` -- dbToGain(), gainToDb()
- [x] `dsp/include/krate/dsp/core/math_constants.h` -- kPi, kTwoPi, kHalfPi
- [x] `dsp/include/krate/dsp/core/midi_utils.h` -- kMinMidiNote, kMaxMidiNote, kA4FrequencyHz
- [x] `dsp/include/krate/dsp/systems/unison_engine.h` -- Reference pattern for panning and multi-voice

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `PitchShiftProcessor` | Non-copyable, uses pImpl. Default-constructable but requires `prepare()` before `process()` | Store in `std::array<PitchShiftProcessor, kMaxVoices>` (default-constructed, prepared in `prepare()`) |
| `PitchShiftProcessor` | `setSemitones()` accepts float, range [-24, +24]. Internally has smoothing (10ms). Does NOT accept cents -- use `setCents()` separately or combine: `setSemitones(totalSemitones)` where totalSemitones includes fractional cents | Use `setSemitones(computedShift + detuneCents/100.0f)` to set total including detune |
| `PitchShiftProcessor` | `process()` signature is `process(input, output, numSamples)` -- no pitch ratio parameter. Pitch is set via `setSemitones()`/`setCents()` before calling `process()` | Set semitones before each block process call |
| `OnePoleSmoother` | `configure()` takes time in ms to reach 99% of target (internally divides by 5 for tau). Default smoothing time is 5ms at 44100Hz | Must call `configure(timeMs, sampleRate)` in `prepare()` for each smoother |
| `OnePoleSmoother` | `setTarget()` uses NOINLINE for NaN safety. `process()` returns current value after one step. `snapTo()` sets both current and target immediately | Use `snapTo()` for initial value, `setTarget()` for smooth transitions |
| `DelayLine` | Non-copyable, movable. `prepare()` takes maxDelaySeconds, not samples. `readLinear()` takes delaySamples (float) | `prepare(sampleRate, kMaxDelayMs / 1000.0f)` for 50ms max |
| `DelayLine` | `write()` must be called BEFORE `read*()`/`readLinear()` each sample | Process order: write input, then read delayed |
| `ScaleHarmonizer` | In Chromatic mode, `calculate()` treats diatonicSteps as raw semitones. Returns DiatonicInterval with scaleDegree=-1 | Check harmonyMode before calling calculate() |
| `PitchTracker` | `getMidiNote()` returns -1 if no note committed yet | Guard against -1 when using as input to ScaleHarmonizer |
| `dbToGain()` | Returns constexpr result. At kSilenceFloorDb (-144), returns approximately 0. Need explicit mute threshold | Use `dB <= kMinLevel` check: if level <= -60 dB, treat as mute (gain = 0) |
| Constant-power pan | UnisonEngine uses `(pan + 1.0f) * kPi * 0.25f` for angle. At pan=-1: angle=0, left=cos(0)=1, right=sin(0)=0. At pan=0: angle=pi/4, left=cos(pi/4)=0.707, right=0.707. At pan=+1: angle=pi/2, left=0, right=1 | Matches FR-005 formula exactly |

## Layer 0 Candidate Analysis

*For Layer 3 features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| `constantPowerPan(pan)` | Used by UnisonEngine, TapManager, and now HarmonizerEngine (3 consumers) | Could go in `stereo_utils.h` | UnisonEngine, TapManager, HarmonizerEngine |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `dBToLinearWithMute(dB, muteThreshold)` | One-liner combining dbToGain + mute check, only used in HarmonizerEngine |
| Per-voice interval computation | Specific to harmonizer logic, not reusable elsewhere |

**Decision**: The constant-power pan formula is a 2-line inline computation (`cos/sin` with angle calculation). While it now has 3 consumers, extracting it would require modifying UnisonEngine and TapManager (risk of breaking existing tests). Decision: keep inline in HarmonizerEngine for now. Revisit extraction when a 4th consumer appears, as a separate refactoring spec.

## SIMD Optimization Analysis

*GATE: Must complete during planning. Constitution Principle IV.*

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | HarmonizerEngine is feed-forward: input -> pitch shift -> sum. No voice output feeds back |
| **Data parallelism width** | 4 voices | 4 independent voices, each with own PitchShiftProcessor. However, PitchShiftProcessor is a complex opaque object, not a simple arithmetic operation |
| **Branch density in inner loop** | LOW | Per-sample: smooth level/pan, accumulate into stereo output. Conditional only on numActiveVoices |
| **Dominant operations** | Complex subcomponents | 95%+ of CPU is inside PitchShiftProcessor (already SIMD-optimized FFT via pffft). HarmonizerEngine's own work is trivial: parameter smoothing + multiply-accumulate |
| **Current CPU budget vs expected usage** | Orchestration < 1% | The engine's own overhead (panning, smoothing, mixing) is negligible compared to PitchShiftProcessor |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The HarmonizerEngine is an orchestration layer. Its inner loop performs: (1) advance smoothers (single multiply-add per smoother per sample), (2) multiply voice output by gain and pan gains, (3) accumulate into stereo buffers. This is ~10 FLOPs per voice per sample. The dominant cost is inside PitchShiftProcessor, which already uses SIMD-optimized FFT via pffft. Vectorizing the orchestration layer's trivial arithmetic across 4 voices (exactly one SSE register width) would save <0.01% CPU -- not worth the complexity. The 4-voice parallel spectral processing is addressed in the roadmap Phase 5 (SIMD Math Header), which is a separate spec.

### Implementation Workflow

**Verdict is NOT BENEFICIAL:** Skip SIMD Phase 2. The orchestration layer uses scalar code only.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip pitch tracking when numVoices=0 | ~0.1% CPU savings (eliminates PitchTracker work) | LOW | YES (FR-018) |
| Skip DelayLine when delay=0ms | Eliminates per-sample write+read overhead | LOW | YES (FR-011) |
| Skip voice processing when level <= -60dB | Eliminates PitchShiftProcessor work for muted voices | LOW | YES |
| Shared-analysis FFT in PhaseVocoder mode | 75% forward FFT savings for 4 voices | HIGH | DEFER to implementation -- requires modifying PhaseVocoderPitchShifter internals, which is beyond Layer 3 scope. Document as future optimization. |

**Note on FR-020 (Shared Analysis FFT)**: The spec requires shared-analysis architecture in PhaseVocoder mode. However, the current `PitchShiftProcessor` API treats each instance as fully independent -- there is no mechanism to share an analysis spectrum between instances. Implementing shared analysis would require modifying `PhaseVocoderPitchShifter` (Layer 2) to accept an external analysis spectrum. This is a significant cross-layer change that should be handled as follows:

1. **Phase 1 (this spec)**: Implement HarmonizerEngine with independent per-voice `PitchShiftProcessor` instances. This is functionally correct but does not achieve the shared-analysis optimization.
2. **Phase 2 (future spec)**: Modify `PhaseVocoderPitchShifter` to support an "external analysis" mode where it accepts a pre-computed analysis spectrum and only performs synthesis. Then update HarmonizerEngine to use this mode.

This approach satisfies all functional requirements while deferring the performance optimization to a focused spec that properly handles the Layer 2 API change. The SC-008 CPU budget for PhaseVocoder mode (<15%) should still be achievable without shared analysis at 44.1kHz/256 block size, but if benchmarks show it exceeds the budget, the shared-analysis spec becomes urgent.

### FR-020 Refactor Roadmap: Shared-Analysis FFT

This section documents exactly how the shared-analysis optimization will be implemented so the deferral doesn't become forgotten tech debt.

#### Why It's Deferred (Not Abandoned)

`PhaseVocoderPitchShifter::process()` (line 1093 of `pitch_shift_processor.h`) takes raw audio samples and always runs its own forward FFT internally:

```cpp
// Current API — no way to inject external spectrum
void process(const float* input, float* output, std::size_t numSamples,
             float pitchRatio) noexcept {
    stft_.pushSamples(input, numSamples);          // pushes raw audio
    while (stft_.canAnalyze()) {
        stft_.analyze(analysisSpectrum_);           // forward FFT — REDUNDANT across voices
        processFrame(pitchRatio);                   // phase modification (unique per voice)
        ola_.synthesize(synthesisSpectrum_);         // per-voice OLA
    }
    ola_.pullSamples(output, samplesToOutput);
}
```

With 4 harmony voices, `stft_.analyze()` runs 4× on the same input — 75% wasted forward FFT work.

#### What Changes (Layer 2)

Add ONE new method to `PhaseVocoderPitchShifter`:

```cpp
/// Process with pre-computed analysis spectrum (shared-analysis mode).
/// Caller is responsible for running the forward FFT once and passing the result.
/// This method only performs phase modification + synthesis iFFT + OLA.
void processWithSharedAnalysis(const SpectralBuffer& sharedSpectrum,
                               float* output, std::size_t numSamples,
                               float pitchRatio) noexcept {
    // Skip stft_.pushSamples() and stft_.analyze() entirely
    // Copy shared spectrum into analysisSpectrum_ (or take by const ref)
    analysisSpectrum_.copyFrom(sharedSpectrum);
    processFrame(pitchRatio);                       // phase modification (unique per voice)
    ola_.synthesize(synthesisSpectrum_);             // per-voice OLA (MUST remain per-voice)
    ola_.pullSamples(output, samplesToOutput);
}
```

**Key constraint**: `processFrame()` (line 1158) already reads from `analysisSpectrum_` member. The shared method just replaces the source of that data. `ola_` remains per-voice (FR-021).

Internal members that remain per-voice (unchanged):
- `synthPhase_[]` — accumulated synthesis phases (unique per pitch ratio)
- `prevPhase_[]` — previous frame phases (unique per voice timing)
- `ola_` — overlap-add buffer (FR-021: MUST be per-voice)
- `synthesisSpectrum_` — output of phase modification
- Phase locking state (`isPeak_`, `regionPeak_`, etc.)
- Transient detector state

Internal members that become shared (owned by HarmonizerEngine):
- `stft_` — only needed for the one shared analysis call
- `analysisSpectrum_` — the shared output, passed to all voices

#### What Changes (Layer 3 — HarmonizerEngine)

```cpp
// In HarmonizerEngine::process(), PhaseVocoder path becomes:
STFT sharedSTFT_;              // owned by engine, prepared once
SpectralBuffer sharedSpectrum_; // shared analysis result

// Per block:
sharedSTFT_.pushSamples(input, numSamples);
while (sharedSTFT_.canAnalyze()) {
    sharedSTFT_.analyze(sharedSpectrum_);  // ONE forward FFT
    for (auto& voice : activeVoices) {
        voice.pitchShifter.processWithSharedAnalysis(
            sharedSpectrum_, voiceScratch_, numSamples, voice.pitchRatio);
        // ... pan, level, accumulate as before
    }
}
```

#### What Does NOT Change

- HarmonizerEngine's public API (`process()`, `setVoiceInterval()`, etc.)
- All existing tests (same inputs → same outputs, just faster)
- Non-PhaseVocoder modes (Simple, PitchSync, Granular) — unaffected
- Per-voice OLA buffers (FR-021) — still per-voice
- The existing `PhaseVocoderPitchShifter::process()` — remains as-is for non-harmonizer consumers

#### Trigger Condition

Run this refactor when EITHER:
1. SC-008 PhaseVocoder benchmark exceeds 15% CPU (4 voices, 44.1kHz, block 256), OR
2. A new spec requires shared spectral analysis for another feature (e.g., spectral freeze across voices)

#### Estimated Effort

- Layer 2 change: ~0.5 days (add `processWithSharedAnalysis()` + tests)
- Layer 3 change: ~0.5 days (add shared STFT path in `process()` + update tests)
- Total: ~1 day, well-bounded, no public API changes

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from roadmap and codebase):
- `UnisonEngine` (L3) -- multi-voice detuned oscillator with stereo spread
- `GranularEngine` (L3) -- multi-voice grain scheduling
- `StereoField` (L3) -- stereo width processing

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `HarmonyMode` enum | MEDIUM | Future standalone harmonizer plugin, MIDI harmony mode | Keep in harmonizer_engine.h for now |
| Constant-power pan utility | HIGH | Already 3 consumers (UnisonEngine, TapManager, HarmonizerEngine) | Keep inline; extract after 4th consumer |
| Per-voice Voice struct pattern | LOW | Specific to harmonizer | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class for multi-voice systems | UnisonEngine and HarmonizerEngine have fundamentally different voice models (oscillators vs pitch shifters). Forced abstraction would add complexity without benefit |
| Keep HarmonyMode in harmonizer_engine.h | Only one consumer. Extract to separate header if a second L3/L4 system needs harmony mode selection |
| Defer shared-analysis FFT | Requires Layer 2 API changes; implement as separate focused spec |
| Add `getNumVoices()` query method | The roadmap draft included `getNumVoices()` but it was omitted from the initial API contract. Added to the contract (A4 remediation from spec analysis 2026-02-18): callers should be able to inspect the current active voice count without tracking state externally. Implementation: `return numActiveVoices_;` |

### Review Trigger

After implementing **Iterum harmonizer delay mode** (Layer 4), review this section:
- [ ] Does the delay mode need HarmonyMode or similar? -> Already uses it via HarmonizerEngine
- [ ] Does the delay mode use the same composition pattern? -> Document shared pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/064-harmonizer-engine/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
+-- tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- systems/
|       +-- harmonizer_engine.h    # NEW: HarmonizerEngine class + HarmonyMode enum
+-- tests/
    +-- unit/systems/
        +-- harmonizer_engine_test.cpp  # NEW: All unit tests
```

**Build system changes**:
- `dsp/CMakeLists.txt`: Add `include/krate/dsp/systems/harmonizer_engine.h` to `KRATE_DSP_SYSTEMS_HEADERS`
- `dsp/tests/CMakeLists.txt`: Add `unit/systems/harmonizer_engine_test.cpp` to `dsp_tests` target + `-fno-fast-math` list

**Structure Decision**: Single header-only file at Layer 3 (Systems), following the same pattern as `unison_engine.h`. All implementation is inline in the header. Tests in a single test file following existing Layer 3 test patterns.

## Complexity Tracking

No constitution violations identified. All design decisions align with project principles.

| Aspect | Assessment |
|--------|------------|
| Layer dependency | CLEAN: L3 -> L2, L1, L0 only |
| ODR risk | LOW: No name conflicts |
| Real-time safety | PASS: All buffers pre-allocated in prepare() |
| Cross-platform | PASS: No platform-specific code |
