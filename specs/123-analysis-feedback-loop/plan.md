# Implementation Plan: Analysis Feedback Loop

**Branch**: `123-analysis-feedback-loop` | **Date**: 2026-03-06 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/123-analysis-feedback-loop/spec.md`

## Summary

Feed the Innexus synth's output back into its own analysis pipeline to create self-evolving timbral behavior. This adds a feedback buffer (mono, one-block delay) to the processor, two new parameters (FeedbackAmount, FeedbackDecay), a per-sample soft limiter (tanh) in the feedback path, and integration with existing freeze/sidechain/energy-budget safety mechanisms. The feedback mixing occurs between the sidechain stereo-to-mono downmix and the `pushSamples()` call. State version bumps from 7 to 8.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (shared DSP library)
**Storage**: Binary state stream (IBStreamer) -- version 8
**Testing**: Catch2 (unit + integration tests)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo VST3 plugin (Innexus)
**Performance Goals**: < 5% total plugin CPU @ 44.1kHz stereo; feedback mixing adds < 1 mul-add per sample (SC-008)
**Constraints**: Zero allocations on audio thread; feedback buffer pre-allocated in `setActive()`
**Scale/Scope**: Focused feature -- 2 parameters, 1 feedback buffer, signal flow modification in `process()`

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check -- Principle I (VST3 Architecture Separation):**
- [x] Parameters registered in both Processor (atomics) and Controller (RangeParameter)
- [x] No cross-includes between processor and controller
- [x] State flows: Host -> Processor -> Controller via setComponentState

**Required Check -- Principle II (Real-Time Audio Thread Safety):**
- [x] Feedback buffer allocated in `setActive()`, not in `process()`
- [x] No allocations, locks, exceptions, or I/O in the feedback mixing path
- [x] All operations are simple arithmetic (multiply, add, tanh, exp)

**Required Check -- Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) -- no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check -- Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check -- Principle XVI (Honest Completion):**
- [x] All FR/SC requirements have concrete, measurable test strategies

No constitution violations. No complexity tracking needed.

## Codebase Research (Principle XIV -- ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None. All changes are member additions to the existing `Processor` class.

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| (none) | N/A | N/A | N/A |

No new classes or structs are introduced by this feature. The feedback buffer is a `std::array<float, 8192>` member (same pattern as `sidechainBuffer_`), and the parameters are `std::atomic<float>` members.

**Utility Functions to be created**: None. The feedback mixing, soft limiting, and decay are simple inline arithmetic in `process()`.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `sidechainBuffer_` | `processor.h:504` | Plugin | Pattern reference for feedback buffer sizing (std::array<float, 8192>) |
| `std::tanh` | stdlib | N/A | Soft limiter in feedback path (FR-009) |
| `std::exp` | stdlib | N/A | Decay coefficient calculation (FR-013) |
| `HarmonicOscillatorBank::kOutputClamp` | `harmonic_oscillator_bank.h:86` | L2 | Existing safety clamp (2.0f) -- no changes needed (FR-011) |
| `HarmonicPhysics::applyDynamics` | `harmonic_physics.h:230` | Plugin | Energy budget normalization -- no changes needed (FR-010) |
| `LiveAnalysisPipeline::pushSamples` | `live_analysis_pipeline.cpp:158` | Plugin | Entry point for mixed feedback+sidechain signal (FR-003) |
| Confidence gate auto-freeze | `processor.cpp:861` | Plugin | Handles garbage analysis from feedback (FR-012) |
| `OnePoleSmoother` | `dsp/primitives/smoother.h` | L1 | Not needed -- feedback params are block-rate, no per-sample smoothing required |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` -- No feedback-related utilities exist
- [x] `dsp/include/krate/dsp/primitives/` -- No feedback buffer primitive exists
- [x] `plugins/innexus/src/` -- No existing feedback buffer or feedback parameter code
- [x] `specs/_architecture_/` -- No feedback mixing component registered

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new types are created. All changes are member additions to the existing `Processor` class. The feedback buffer is a plain `std::array`, and parameters follow the established `std::atomic<float>` pattern.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `LiveAnalysisPipeline` | `pushSamples` | `void pushSamples(const float* samples, size_t numSamples)` | Yes |
| `OnePoleSmoother` | `configure` | `void configure(float timeConstantMs, float sampleRate) noexcept` | Yes (NOT USED — see Decision Log; listed for reference only) |
| `OnePoleSmoother` | `snapTo` | `void snapTo(float value) noexcept` | Yes (NOT USED — see Decision Log; listed for reference only) |
| `sidechainBuffer_` | (member) | `std::array<float, 8192> sidechainBuffer_{}` | Yes |
| `HarmonicOscillatorBank` | `kOutputClamp` | `static constexpr float kOutputClamp = 2.0f` | Yes |

### Header Files Read

- [x] `plugins/innexus/src/processor/processor.h` -- Processor class members
- [x] `plugins/innexus/src/plugin_ids.h` -- Parameter ID registry
- [x] `plugins/innexus/src/dsp/harmonic_physics.h` -- HarmonicPhysics class
- [x] `plugins/innexus/src/dsp/live_analysis_pipeline.h` -- pushSamples signature
- [x] `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` -- kOutputClamp
- [x] `dsp/include/krate/dsp/primitives/smoother.h` -- OnePoleSmoother API

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `sidechainBuffer_` | Used for downmix AND as pointer source for pushSamples | Cannot be overwritten by feedback mixing -- must use a separate mixed buffer OR modify in-place after pointer assignment |
| `pushSamples` | Takes `const float*` -- cannot modify the pointed-to data after passing | Must complete feedback mixing into a buffer before calling pushSamples |

**Critical Design Note**: The current code sets `sidechainMono = sidechainBuffer_.data()` and then passes `sidechainMono` to `pushSamples()`. For feedback mixing, we can either:
1. Mix feedback directly into `sidechainBuffer_` before the `pushSamples()` call (since the pointer already points there), or
2. Use a separate `feedbackMixBuffer_` and pass that to `pushSamples()`.

Option 1 is simpler and avoids a second buffer. The sidechain data is already copied into `sidechainBuffer_` at that point, so modifying it in-place is safe. When mono input is used directly (`sidechainMono = scBus.channelBuffers32[0]`), we need to copy to `sidechainBuffer_` first before mixing. This is the chosen approach.

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Feedback mixing loop | 3-line inline loop, single consumer, processor-specific |
| Decay coefficient calculation | One-liner `std::exp(...)`, single consumer |
| Soft limiter | Single `std::tanh(...)` expression, inline in per-sample loop |

**Decision**: No extractions. All operations are trivial arithmetic inlined in `process()`. No shared utility functions warranted.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | Output from block N feeds into analysis input of block N+1. Serial dependency across blocks. |
| **Data parallelism width** | 1 mono stream | Single channel feedback buffer -- no parallel streams |
| **Branch density in inner loop** | LOW | One conditional (freeze bypass) per block, not per sample |
| **Dominant operations** | arithmetic + tanh | Per-sample: 1 tanh, 2 muls, 1 add for mixing. Per-block: 1 exp for decay. |
| **Current CPU budget vs expected usage** | <5% budget vs negligible | Feedback adds <1 mul-add per output sample |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The feedback path processes a single mono stream with trivial per-sample operations (one tanh, two multiplies, one add). The working set is a single buffer of at most 8192 floats. SIMD vectorization of the decay application loop (`feedbackBuffer[s] *= decayCoeff`) could theoretically help, but at <8192 iterations per block with a uniform scalar multiply, the compiler's auto-vectorization will handle this adequately. The tanh soft limiter in the mixing loop has a serial dependency on the feedback buffer read, making manual SIMD unnecessary.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when feedbackAmount == 0 | Skip entire mixing loop | LOW | YES |
| Compiler auto-vectorization of decay loop | Automatic with -O2 | NONE | YES (free) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin-level (Innexus processor)

**Related features at same layer** (from roadmap):
- Spec C: Attractor State Detection (future)
- Spec D: Spectral Memory (future)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Feedback buffer + mixing pattern | MEDIUM | Timbral delay, harmonic echo (hypothetical) | Keep local -- only one consumer |
| Soft limiter (tanh bounding) | LOW | Already available as stdlib `std::tanh` | Keep inline |
| Decay coefficient formula | LOW | Simple `std::exp(...)` one-liner | Keep inline |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared feedback class | Single consumer; pattern is 10 lines of inline code |
| No smoother for feedback params | Block-rate reading is sufficient; no per-sample zipper artifacts possible since feedback mixing is per-sample but amount/decay change only at block boundaries |

### Review Trigger

After implementing **Spec C (Attractor State Detection)**, review:
- [ ] Does attractor detection need the feedback buffer contents? If so, expose a read accessor.
- [ ] Any shared feedback infrastructure? Unlikely -- attractor detection is analysis-side.

## Project Structure

### Documentation (this feature)

```text
specs/123-analysis-feedback-loop/
+-- plan.md              # This file
+-- spec.md              # Feature specification
+-- checklists/          # Existing
```

### Source Code Changes

```text
plugins/innexus/
+-- src/
|   +-- plugin_ids.h                    # MOD: Add kAnalysisFeedbackId=710, kAnalysisFeedbackDecayId=711
|   +-- processor/
|   |   +-- processor.h                 # MOD: Add feedback buffer, atomics, previousFreezeForFeedback_ flag
|   |   +-- processor.cpp               # MOD: Feedback mixing in process(), state v7->v8, setActive() reset
|   +-- controller/
|       +-- controller.cpp              # MOD: Register 2 new RangeParameters, setComponentState v8
+-- tests/
    +-- integration/
    |   +-- test_analysis_feedback.cpp   # NEW: Integration tests for all FR/SC
    +-- unit/
        +-- vst/
            +-- test_state_v8.cpp        # NEW: State version 8 round-trip tests
```

**Structure Decision**: No new directories. Two new test files. Four existing files modified.

## Architecture Overview

### Signal Flow (Sidechain Mode with Feedback)

```
Block N:
  Sidechain In (stereo)
       |
       v
  Stereo-to-Mono Downmix -> sidechainBuffer_
       |
       v
  +-- Feedback Mixing (FR-001, FR-003) ---------+
  |  For each sample s:                          |
  |    fbSample = tanh(feedbackBuffer_[s]        |
  |               * feedbackAmount * 2.0) * 0.5  |
  |    mixedInput[s] = sidechain[s]              |
  |               * (1 - feedbackAmount)         |
  |               + fbSample                     |
  +----------------------------------------------+
       |
       v
  pushSamples(mixedInput, numSamples)
       |
       v
  LiveAnalysisPipeline (STFT, f0 detection, etc.)
       |
       v
  HarmonicFrame + ResidualFrame
       |
       v
  [Morph -> Filter -> Modulation -> Physics -> Oscillator Bank]
       |
       v
  Stereo Output (sampleL, sampleR)
       |
       v
  +-- Feedback Capture (FR-002, FR-006) ---------+
  |  After all per-sample processing:             |
  |    feedbackBuffer_[s] = (sampleL + sampleR)   |
  |                         * 0.5                 |
  +-----------------------------------------------+
       |
       v
  +-- Feedback Decay (FR-013) -------------------+
  |  After capture (once per block):              |
  |    decayCoeff = exp(-decayAmount              |
  |                * blockSize / sampleRate)       |
  |    For each s: feedbackBuffer_[s] *= decayCoeff|
  +-----------------------------------------------+
       |
       v
  feedbackBuffer_ ready for Block N+1
```

### Safety Stack (5 layers, all existing except soft limiter)

1. **Soft limiter** (NEW): `tanh(fb * amount * 2.0) * 0.5` bounds feedback signal to [-0.5, +0.5]
2. **Energy budget** (existing): `HarmonicPhysics::applyDynamics` normalizes total harmonic energy
3. **Hard clamp** (existing): `HarmonicOscillatorBank::kOutputClamp = 2.0f`
4. **Confidence gate** (existing): Auto-freeze when analysis confidence drops below threshold
5. **Feedback decay** (NEW): Exponential energy leak prevents infinite sustain

## Detailed Component Design

### 1. Parameter IDs (plugin_ids.h)

Add to `ParameterIds` enum after `kEntropyId = 703`:

```cpp
// Analysis Feedback Loop (710-711) -- Spec B
kAnalysisFeedbackId = 710,           // 0.0-1.0, default 0.0
kAnalysisFeedbackDecayId = 711,      // 0.0-1.0, default 0.2
```

### 2. Processor Members (processor.h)

Add parameter atomics (following Spec A pattern at line 399-403):

```cpp
// Analysis Feedback Loop (Spec B: 123-analysis-feedback-loop)
std::atomic<float> feedbackAmount_{0.0f};    // 0.0-1.0, default 0.0
std::atomic<float> feedbackDecay_{0.2f};     // 0.0-1.0, default 0.2
```

Add feedback buffer (following `sidechainBuffer_` pattern at line 504):

```cpp
// Feedback buffer for analysis feedback loop (Spec B: FR-005, FR-006)
// Pre-allocated mono buffer sized to max block size (same as sidechainBuffer_)
std::array<float, 8192> feedbackBuffer_{};

// Tracks previous freeze state for feedback buffer clear on disengage (FR-016)
bool previousFreezeForFeedback_ = false;
```

Add test accessors:

```cpp
float getFeedbackAmount() const { return feedbackAmount_.load(std::memory_order_relaxed); }
float getFeedbackDecay() const { return feedbackDecay_.load(std::memory_order_relaxed); }
```

### 3. Process Loop Modifications (processor.cpp)

**A. Feedback mixing -- between sidechain downmix and pushSamples() (line ~381-393)**

Note: The roadmap (Innexus-emergent-harmonics-roadmap.md §Spec B) presents the soft limiter formula split across two lines (`fbSample = feedbackBuffer[s] * feedbackAmount` then `fbSample = tanh(fbSample * 2.0) * 0.5`). This is algebraically equivalent to the spec/plan formula (`fbSample = tanh(feedbackBuffer_[s] * feedbackAmount * 2.0f) * 0.5f`) — feedbackAmount is a scalar so the multiplication order is irrelevant. The spec/plan formula is canonical; the roadmap is an informal sketch.

The current code at line 381-393:
```cpp
// Feed sidechain audio to live analysis pipeline (FR-003, FR-009)
{
    int currentSource = inputSource_.load(...) > 0.5f ? 1 : 0;
    if (currentSource == 1 && sidechainMono != nullptr)
    {
        ...
        liveAnalysis_.pushSamples(sidechainMono, ...);
```

Modified to include feedback mixing:
```cpp
// Feed sidechain audio to live analysis pipeline (FR-003, FR-009)
{
    int currentSource = inputSource_.load(...) > 0.5f ? 1 : 0;
    if (currentSource == 1 && sidechainMono != nullptr)
    {
        const float fbAmount = feedbackAmount_.load(std::memory_order_relaxed);
        const bool manualFrozen = freeze_.load(std::memory_order_relaxed) > 0.5f;

        // Spec B FR-001, FR-003, FR-014, FR-015: Mix feedback into sidechain
        // Bypass feedback when: amount is zero, or freeze is active, or not in sidechain mode
        if (fbAmount > 0.0f && !manualFrozen)
        {
            // If sidechainMono points to raw bus data (mono case), copy to buffer first
            if (sidechainMono != sidechainBuffer_.data())
            {
                auto count = std::min(data.numSamples,
                    static_cast<Steinberg::int32>(sidechainBuffer_.size()));
                std::memcpy(sidechainBuffer_.data(), sidechainMono,
                    static_cast<size_t>(count) * sizeof(float));
                sidechainMono = sidechainBuffer_.data();
            }

            // FR-001, FR-009: Per-sample soft-limited feedback mixing
            auto count = std::min(data.numSamples,
                static_cast<Steinberg::int32>(sidechainBuffer_.size()));
            for (Steinberg::int32 s = 0; s < count; ++s)
            {
                const float fbSample = std::tanh(
                    feedbackBuffer_[static_cast<size_t>(s)]
                    * fbAmount * 2.0f) * 0.5f;
                sidechainBuffer_[static_cast<size_t>(s)] =
                    sidechainBuffer_[static_cast<size_t>(s)]
                    * (1.0f - fbAmount) + fbSample;
            }
        }

        // ... existing pushSamples call unchanged
        liveAnalysis_.pushSamples(sidechainMono, ...);
    }
}
```

**B. Feedback capture -- after the per-sample output loop (after line ~1373)**

After the per-sample loop writes to `out[0][s]` and `out[1][s]`, capture the mono output into the feedback buffer. The `currentSource == 1` check corresponds to `InputSource::Sidechain` (enum value 1, defined in `plugin_ids.h:127-131`). The `inputSource_` atomic stores a normalized float; the `> 0.5f` comparison is the existing pattern throughout the processor for integer-valued parameters stored as floats.

```cpp
// Spec B FR-002, FR-006: Capture mono output into feedback buffer
{
    const int currentSource = inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;
    if (currentSource == 1) // Only capture in sidechain mode (InputSource::Sidechain, FR-014)
    {
        const auto count = std::min(numSamples,
            static_cast<Steinberg::int32>(feedbackBuffer_.size()));

        if (numOutputChannels >= 2)
        {
            for (Steinberg::int32 s = 0; s < count; ++s)
            {
                feedbackBuffer_[static_cast<size_t>(s)] =
                    (out[0][s] + out[1][s]) * 0.5f;
            }
        }
        else
        {
            for (Steinberg::int32 s = 0; s < count; ++s)
                feedbackBuffer_[static_cast<size_t>(s)] = out[0][s];
        }

        // FR-013: Apply per-block exponential decay
        const float decayAmount = feedbackDecay_.load(std::memory_order_relaxed);
        if (decayAmount > 0.0f)
        {
            const float decayCoeff = std::exp(
                -decayAmount * static_cast<float>(count)
                / static_cast<float>(sampleRate_));
            for (Steinberg::int32 s = 0; s < count; ++s)
                feedbackBuffer_[static_cast<size_t>(s)] *= decayCoeff;
        }
    }
}
```

**C. Freeze interaction -- in the manual freeze detection block (~line 665-717)**

Add feedback buffer clear when freeze is disengaged:

```cpp
// Detect freeze disengage transition (on -> off)
if (!currentFreezeState && previousFreezeState_)
{
    // ... existing crossfade code ...

    // Spec B FR-016: Clear feedback buffer on freeze disengage
    feedbackBuffer_.fill(0.0f);
}
```

**D. setActive() reset (~line 184)**

Add feedback buffer clear:

```cpp
// Spec B: Reset feedback buffer (FR-018)
feedbackBuffer_.fill(0.0f);
```

### 4. Parameter Registration (controller.cpp)

Add after existing Spec A parameters (line ~485):

```cpp
// Analysis Feedback Loop (Spec B)
auto* feedbackAmountParam = new Steinberg::Vst::RangeParameter(
    STR16("Feedback Amount"), kAnalysisFeedbackId,
    STR16("%"), 0.0, 1.0, 0.0, 0,
    Steinberg::Vst::ParameterInfo::kCanAutomate);
parameters.addParameter(feedbackAmountParam);

auto* feedbackDecayParam = new Steinberg::Vst::RangeParameter(
    STR16("Feedback Decay"), kAnalysisFeedbackDecayId,
    STR16("%"), 0.0, 1.0, 0.2, 0,
    Steinberg::Vst::ParameterInfo::kCanAutomate);
parameters.addParameter(feedbackDecayParam);
```

### 5. processParameterChanges (processor.cpp)

Add cases after kEntropyId handling (~line 1896):

```cpp
// Analysis Feedback Loop (Spec B)
case kAnalysisFeedbackId:
    feedbackAmount_.store(
        std::clamp(static_cast<float>(value), 0.0f, 1.0f));
    break;
case kAnalysisFeedbackDecayId:
    feedbackDecay_.store(
        std::clamp(static_cast<float>(value), 0.0f, 1.0f));
    break;
```

### 6. State Persistence (processor.cpp)

**getState**: Change version from 7 to 8. Append after Spec A physics parameters:

```cpp
// Write state version -- Spec B: version 8 (analysis feedback loop)
streamer.writeInt32(8);

// ... existing state writing ...

// --- Spec B: Analysis Feedback Loop parameters (v8) ---
streamer.writeFloat(feedbackAmount_.load(std::memory_order_relaxed));
streamer.writeFloat(feedbackDecay_.load(std::memory_order_relaxed));
```

**setState**: Add after v7 handling:

```cpp
// --- Spec B: Analysis Feedback Loop parameters (v8) ---
feedbackAmount_.store(0.0f);
feedbackDecay_.store(0.2f);

if (version >= 8)
{
    if (streamer.readFloat(floatVal))
        feedbackAmount_.store(std::clamp(floatVal, 0.0f, 1.0f));
    if (streamer.readFloat(floatVal))
        feedbackDecay_.store(std::clamp(floatVal, 0.0f, 1.0f));
}
```

**Controller setComponentState**: Mirror the same logic to sync controller params.

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Feedback divergence / runaway levels | LOW | HIGH | 5-layer safety stack: tanh limiter, energy budget, hard clamp, confidence gate, decay |
| DC offset buildup in feedback loop | LOW | MEDIUM | tanh limiter is symmetric around zero; analysis pipeline's STFT windowing removes DC |
| Audible glitch on freeze disengage (buffer clear) | LOW | LOW | The buffer is zeroed but feedback mixing is bypassed during freeze; when freeze disengages, fbAmount determines how quickly feedback re-establishes |
| State version backward compatibility | LOW | MEDIUM | Defaulting to fbAmount=0.0 means old presets behave identically to before |
| sidechainMono pointer invalidation | MEDIUM | HIGH | Addressed by copying raw bus data to sidechainBuffer_ before in-place modification |

## Smoother Strategy

**No per-sample smoothers needed for feedback parameters.** Both `feedbackAmount` and `feedbackDecay` are read once per process() call at block rate. Since the feedback mixing formula applies these values uniformly across the block, there are no per-sample discontinuities (zipper noise). This matches the spec: "feedbackAmount is read per-process-call, so rapid automation produces smooth transitions at block rate" (Edge Cases section).

## Phase Breakdown

### Phase 1: Parameters and State (Foundation)

**Files modified**: `plugin_ids.h`, `processor.h`, `processor.cpp`, `controller.cpp`
**New files**: `tests/unit/vst/test_state_v8.cpp`

Tasks:
1. Add parameter IDs (kAnalysisFeedbackId=710, kAnalysisFeedbackDecayId=711) to `plugin_ids.h`
2. Add atomics and feedback buffer to `processor.h`
3. Add test accessors for feedback params
4. Add processParameterChanges cases
5. Register parameters in controller.cpp
6. Implement state v7->v8 in getState/setState (processor + controller)
7. Write state round-trip test (test_state_v8.cpp)
8. Build and verify all tests pass

### Phase 2: Feedback Path (Core Feature)

**Files modified**: `processor.cpp`
**New files**: `tests/integration/test_analysis_feedback.cpp`

Tasks:
1. Write test: SC-001 (no-regression with fbAmount=0)
2. Add feedback buffer clear in setActive()
3. Add feedback mixing between sidechain downmix and pushSamples()
4. Add feedback capture after per-sample output loop
5. Add per-block decay application
6. Write test: SC-002 (convergence with fbAmount=1.0, silence input)
7. Write test: SC-004 (output never exceeds ceiling)
8. Write test: SC-008 (negligible CPU overhead)
9. Build and verify all tests pass

### Phase 3: Freeze and Mode Interaction

**Files modified**: `processor.cpp`

Tasks:
1. Write test: SC-005 (freeze bypasses feedback)
2. Write test: SC-006 (freeze disengage clears buffer)
3. Add feedback buffer clear on freeze disengage
4. Write test: SC-007 (sample mode ignores feedback)
5. Write test: SC-003 (decay to silence within time bounds)
6. Build and verify all tests pass

### Phase 4: Quality Gates

Tasks:
1. Run full test suite (dsp_tests, innexus_tests, shared_tests)
2. Build Release and run pluginval
3. Run clang-tidy
4. Fill compliance table in spec.md
5. Final commit
