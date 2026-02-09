# Quickstart: Ruinae Engine Composition

**Feature Branch**: `044-engine-composition`
**Date**: 2026-02-09

## What This Feature Does

Composes all Ruinae synthesizer DSP components (16 RuinaeVoice instances, VoiceAllocator, MonoHandler, NoteProcessor, ModulationEngine, global filter, RuinaeEffectsChain, and master output) into the complete `RuinaeEngine` class at Layer 3. This is the top-level DSP system for the Ruinae synthesizer.

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/systems/ruinae_engine.h` | RuinaeEngine class + RuinaeModDest enum |
| `dsp/tests/unit/systems/ruinae_engine_test.cpp` | Unit tests for all FR/SC requirements |
| `dsp/tests/integration/ruinae_engine_integration_test.cpp` | End-to-end MIDI-to-output signal path tests |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/CMakeLists.txt` | Add test source files to test targets |
| `specs/_architecture_/layer3-systems.md` | Add RuinaeEngine component entry |

## Key Patterns

### Composition (following PolySynthEngine)

```cpp
class RuinaeEngine {
    // Sub-components (all pre-allocated)
    std::array<RuinaeVoice, kMaxPolyphony> voices_;
    VoiceAllocator allocator_;
    MonoHandler monoHandler_;
    NoteProcessor noteProcessor_;
    ModulationEngine globalModEngine_;
    SVF globalFilterL_, globalFilterR_;
    RuinaeEffectsChain effectsChain_;

    // Scratch buffers (allocated in prepare())
    std::vector<float> voiceScratchBuffer_;
    std::vector<float> mixBufferL_, mixBufferR_;
    std::vector<float> previousOutputL_, previousOutputR_;
};
```

### Stereo Voice Panning

```cpp
// Equal-power pan law
float panPosition = voicePanPositions_[voiceIndex];
float leftGain = std::cos(panPosition * kPi * 0.5f);
float rightGain = std::sin(panPosition * kPi * 0.5f);

// Sum into stereo mix
for (size_t s = 0; s < numSamples; ++s) {
    mixBufferL_[s] += voiceScratchBuffer_[s] * leftGain;
    mixBufferR_[s] += voiceScratchBuffer_[s] * rightGain;
}
```

### Stereo Width (Mid/Side)

```cpp
for (size_t s = 0; s < numSamples; ++s) {
    float mid = (mixBufferL_[s] + mixBufferR_[s]) * 0.5f;
    float side = (mixBufferL_[s] - mixBufferR_[s]) * 0.5f;
    mixBufferL_[s] = mid + side * stereoWidth_;
    mixBufferR_[s] = mid - side * stereoWidth_;
}
```

### Global Modulation Integration

```cpp
// Process global modulation with previous block's output
globalModEngine_.process(blockContext_, previousOutputL_.data(),
                         previousOutputR_.data(), numSamples);

// Read offsets for engine-level params
float cutoffOffset = globalModEngine_.getModulationOffset(
    static_cast<uint32_t>(RuinaeModDest::GlobalFilterCutoff));

// Forward "AllVoice" offsets to each active voice
float filterOffset = globalModEngine_.getModulationOffset(
    static_cast<uint32_t>(RuinaeModDest::AllVoiceFilterCutoff));

// Two-stage clamping formula for global-to-voice forwarding (FR-021):
// finalValue = clamp(clamp(baseValue + perVoiceOffset, min, max) + globalOffset, min, max)
```

### Master Output

```cpp
float effectiveGain = masterGain_ * gainCompensation_;
for (size_t s = 0; s < numSamples; ++s) {
    left[s] = mixBufferL_[s] * effectiveGain;
    right[s] = mixBufferR_[s] * effectiveGain;

    if (softLimitEnabled_) {
        left[s] = Sigmoid::tanh(left[s]);
        right[s] = Sigmoid::tanh(right[s]);
    }

    // NaN/Inf flush
    if (detail::isNaN(left[s]) || detail::isInf(left[s])) left[s] = 0.0f;
    if (detail::isNaN(right[s]) || detail::isInf(right[s])) right[s] = 0.0f;
}
```

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ruinae-engine]"
```

## Test Strategy

### Unit Tests (ruinae_engine_test.cpp)
- Construction and constants
- Lifecycle (prepare/reset)
- Note dispatch (poly and mono)
- Voice mode switching
- Stereo panning and width
- Global filter
- Master output (gain compensation, soft limiting)
- Parameter forwarding
- NaN/Inf handling
- Edge cases (0 samples, unprepared, etc.)

### Integration Tests (ruinae_engine_integration_test.cpp)
- **Full signal path**: MIDI noteOn -> voice activation -> oscillator -> filter -> distortion -> trance gate -> VCA -> stereo pan -> width -> global filter -> effects -> master output
- **Chord playback**: Multiple simultaneous notes through full chain
- **Release and silence**: noteOff -> release tail -> silence
- **Mono legato**: Overlapping notes, envelope behavior
- **Portamento**: Smooth pitch glide verification
- **Pitch bend**: All voices shift frequency
- **Aftertouch**: Per-voice modulation
- **Effects integration**: Reverb tail, delay echoes
- **Mode switching**: Poly to mono and back under load
- **Multi-sample-rate**: Verify at 44100, 48000, 96000, 192000
- **CPU performance**: 8 voices < 10% single core

## Dependencies (Verified APIs)

| Component | Key Methods Called |
|-----------|------------------|
| RuinaeVoice | `prepare(sampleRate, maxBlockSize)`, `noteOn(freq, vel)`, `noteOff()`, `setFrequency(hz)`, `isActive()`, `processBlock(buf, n)`, `setAftertouch(val)` |
| VoiceAllocator | `noteOn(note, vel)`, `noteOff(note)`, `voiceFinished(idx)`, `setVoiceCount(n)`, `getVoiceNote(idx)`, `getActiveVoiceCount()`, `setAllocationMode()`, `setStealMode()`, `reset()` |
| MonoHandler | `noteOn(note, vel)`, `noteOff(note)`, `processPortamento()`, `prepare(sr)`, `reset()`, `setMode()`, `setLegato()`, `setPortamentoTime()`, `setPortamentoMode()` |
| NoteProcessor | `prepare(sr)`, `reset()`, `setPitchBend(bipolar)`, `processPitchBend()`, `getFrequency(note)`, `mapVelocity(vel)` |
| ModulationEngine | `prepare(sr, maxBlock)`, `reset()`, `process(ctx, inL, inR, n)`, `setRouting(idx, routing)`, `clearRouting(idx)`, `getModulationOffset(destId)` |
| SVF | `prepare(sr)`, `reset()`, `process(sample)`, `processBlock(buf, n)`, `setCutoff(hz)`, `setResonance(q)`, `setMode(mode)` |
| RuinaeEffectsChain | `prepare(sr, maxBlock)`, `reset()`, `processBlock(L, R, n)`, all setters |
| Sigmoid::tanh | `float tanh(float x)` |
