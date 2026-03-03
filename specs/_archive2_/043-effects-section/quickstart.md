# Quickstart: Ruinae Effects Section

**Feature**: 043-effects-section | **Date**: 2026-02-08

## What Are We Building?

`RuinaeEffectsChain` -- a Layer 3 (systems) class that composes seven existing Layer 4 effects into a fixed-order stereo processing chain:

```
Input (L, R)
  -> [Freeze Slot]  (FreezeMode -- bypass when disabled)
  -> [Delay Slot]   (1 of 5 delay types with crossfade switching)
  -> [Reverb Slot]  (Dattorro Reverb)
  -> Output (L, R)
```

Plus a `RuinaeDelayType` enum added to the existing `ruinae_types.h`.

## New Files

| File | Type | Purpose |
|------|------|---------|
| `dsp/include/krate/dsp/systems/ruinae_effects_chain.h` | Header-only class | The effects chain itself |
| `dsp/tests/unit/systems/ruinae_effects_chain_test.cpp` | Catch2 tests | All FR/SC verification |

## Modified Files

| File | Change |
|------|--------|
| `dsp/include/krate/dsp/systems/ruinae_types.h` | Add `RuinaeDelayType` enum |
| `dsp/tests/CMakeLists.txt` | Add test source file |

## Key Design Decisions

1. **Switch dispatch, not polymorphism** -- The five delay types have genuinely different APIs (different prepare/process signatures, different parameter names). A switch statement in each forwarding method dispatches to the correct API. This matches the pattern used in `RuinaeVoice`.

2. **Linear crossfade at 30ms** -- Per-sample alpha ramp from 0 to 1 over 30ms (within the 25-50ms spec range). Uses `crossfadeIncrement()` from `core/crossfade_utils.h`.

3. **Fast-track crossfade** -- When a type switch is requested during an active crossfade, snap the current crossfade to completion and immediately start the new one. No triple-overlap.

4. **Per-delay latency compensation** -- Four pairs of `DelayLine` instances (one per non-spectral delay type) pad output to match the spectral delay's FFT latency. `getLatencySamples()` always returns the spectral delay's FFT size (typically 1024 samples).

5. **External freeze bypass** -- When `freezeEnabled_ == false`, skip the `FreezeMode::process()` call entirely. When enabled, dry/wet is set to 100% (full wet). The chain manages the enable/disable state.

## Critical API Gotchas

These are the most likely sources of bugs during implementation:

| Gotcha | Wrong | Right |
|--------|-------|-------|
| TapeDelay has no BlockContext | `tape_.process(L, R, n, ctx)` | `tape_.process(L, R, n)` |
| GranularDelay uses separate buffers | `granular_.process(L, R, n, ctx)` | `granular_.process(tempL, tempR, L, R, n, ctx)` |
| GranularDelay prepare takes only sampleRate | `granular_.prepare(sr, bs, maxMs)` | `granular_.prepare(sr)` |
| SpectralDelay mix is 0-1 normalized (refactored) | `spectral_.setDryWetMix(50.0f)` | `spectral_.setDryWetMix(0.5f)` |
| FreezeMode shimmer/decay/mix is 0-1 normalized (refactored) | `freeze_.setShimmerMix(50.0f)` | `freeze_.setShimmerMix(0.5f)` |
| TapeDelay time setter name | `tape_.setTime(ms)` | `tape_.setMotorSpeed(ms)` |
| PingPongDelay time setter name | `pingpong_.setTime(ms)` | `pingpong_.setDelayTimeMs(ms)` |
| SpectralDelay time setter name | `spectral_.setDelayTime(ms)` | `spectral_.setBaseDelayMs(ms)` |
| GranularDelay mix setter name | `granular_.setMix(mix)` | `granular_.setDryWet(mix)` |
| Reverb process method name | `reverb_.process(L, R, n)` | `reverb_.processBlock(L, R, n)` |

## Implementation Order

Tasks should be implemented in this order (each builds on the previous):

1. **RuinaeDelayType enum** -- Add to `ruinae_types.h`. Test: enum values, sentinel count.

2. **Skeleton chain (prepare/reset/processBlock pass-through)** -- Construct all effect instances, prepare them, process audio as dry pass-through. Test: SC-004 (output within -120 dBFS of input).

3. **Single delay type processing** -- Process one delay type (Digital as default). Test: impulse response shows delayed copies.

4. **All five delay types + parameter forwarding** -- Switch dispatch for all types, forward time/feedback/mix. Test: each type produces different output, parameters forwarded correctly.

5. **Crossfade state machine** -- Linear crossfade between delay types with fast-track. Test: SC-002 (no discontinuities > -60 dBFS), SC-008 (10 consecutive switches click-free).

6. **Latency compensation** -- Per-delay padding delays, constant `getLatencySamples()`. Test: SC-007 (latency constant across type switches).

7. **Freeze slot integration** -- FreezeMode as bypass-managed insert. Test: freeze captures spectrum, parameters forwarded.

8. **Reverb integration** -- Reverb as final chain stage. Test: processes delayed signal, independent freeze.

9. **Multi-sample-rate verification** -- All tests at 44.1kHz and 96kHz. Test: SC-006.

## Build and Test

```bash
# Set CMake alias (Windows)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run only effects chain tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "RuinaeEffectsChain*"
```

## Dependencies (All Existing)

```
RuinaeEffectsChain (Layer 3)
  |
  +-- FreezeMode (Layer 4) -- effects/freeze_mode.h
  +-- DigitalDelay (Layer 4) -- effects/digital_delay.h
  +-- TapeDelay (Layer 4) -- effects/tape_delay.h
  +-- PingPongDelay (Layer 4) -- effects/ping_pong_delay.h
  +-- GranularDelay (Layer 4) -- effects/granular_delay.h
  +-- SpectralDelay (Layer 4) -- effects/spectral_delay.h
  +-- Reverb (Layer 4) -- effects/reverb.h
  +-- DelayLine (Layer 1) -- primitives/delay_line.h
  +-- BlockContext (Layer 0) -- core/block_context.h
  +-- crossfadeIncrement (Layer 0) -- core/crossfade_utils.h
```

## Parameter Forwarding Quick Reference

### Delay Time

```cpp
void setDelayTime(float ms) noexcept {
    digitalDelay_.setTime(ms);
    tapeDelay_.setMotorSpeed(ms);
    pingPongDelay_.setDelayTimeMs(ms);
    granularDelay_.setDelayTime(ms);
    spectralDelay_.setBaseDelayMs(ms);
}
```

### Delay Feedback

```cpp
void setDelayFeedback(float amount) noexcept {
    digitalDelay_.setFeedback(amount);
    tapeDelay_.setFeedback(amount);
    pingPongDelay_.setFeedback(amount);
    granularDelay_.setFeedback(amount);
    spectralDelay_.setFeedback(amount);
}
```

### Delay Mix

```cpp
void setDelayMix(float mix) noexcept {
    digitalDelay_.setMix(mix);
    tapeDelay_.setMix(mix);
    pingPongDelay_.setMix(mix);
    granularDelay_.setDryWet(mix);       // Different name!
    spectralDelay_.setDryWetMix(mix);    // 0-1 normalized (refactored from 0-100)
}
```

### Freeze Parameters

```cpp
void setFreezePitchSemitones(float semitones) noexcept {
    freeze_.setPitchSemitones(semitones);      // Direct forwarding
}
void setFreezeShimmerMix(float mix) noexcept {
    freeze_.setShimmerMix(mix);                // 0-1 normalized (refactored from 0-100)
}
void setFreezeDecay(float decay) noexcept {
    freeze_.setDecay(decay);                   // 0-1 normalized (refactored from 0-100)
}
```

## Success Criteria Summary

| ID | Target | How to Verify |
|----|--------|---------------|
| SC-001 | < 3.0% CPU (Digital + reverb, 44.1kHz/512) | Benchmark test |
| SC-002 | Crossfade 25-50ms, no discontinuity > -60 dBFS | Type switch during continuous audio |
| SC-003 | Zero allocations in processBlock/setters | ASan or allocator instrumentation |
| SC-004 | Default state output within -120 dBFS of input | Process sine, measure deviation |
| SC-005 | All 29 FRs have at least one test | Test count audit |
| SC-006 | Tests pass at 44.1kHz and 96kHz | Parameterized sample rate tests |
| SC-007 | Constant latency across type switches | Query before/after switch, assert equal |
| SC-008 | 10 consecutive type switches click-free | Cycle all 5 types twice, measure output |
