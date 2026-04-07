# Quickstart: Dual Reverb System

**Branch**: `125-dual-reverb` | **Spec**: specs/125-dual-reverb/spec.md

## What This Feature Does

Adds a dual reverb system to the Ruinae synthesizer:
1. Optimizes the existing Dattorro plate reverb (15%+ CPU reduction)
2. Adds a new SIMD-optimized 8-channel FDN reverb
3. Allows users to select between Plate and Hall reverb types with smooth crossfade switching

## Architecture Overview

```
                  +--------------------------+
                  |   RuinaeEffectsChain     |
                  |                          |
                  |  reverbType selector     |
                  |      |                   |
                  |  +---+---+               |
                  |  |       |               |
                  |  v       v               |
                  | Reverb  FDNReverb        |
                  | (Plate) (Hall)           |
                  |  |       |               |
                  |  +--xfade+               |
                  |      |                   |
                  |      v                   |
                  |   output                 |
                  +--------------------------+
```

## File Map

### New Files

| File | Description |
|------|-------------|
| `dsp/include/krate/dsp/effects/fdn_reverb.h` | FDN reverb class (Layer 4) |
| `dsp/include/krate/dsp/effects/fdn_reverb_simd.cpp` | Highway SIMD kernels for FDN |
| `dsp/tests/unit/effects/fdn_reverb_test.cpp` | FDN reverb unit tests |

### Modified Files

| File | Changes |
|------|---------|
| `dsp/include/krate/dsp/effects/reverb.h` | Gordon-Smith LFO, block-rate smoothing, contiguous buffer, denormal cleanup |
| `dsp/tests/unit/effects/reverb_test.cpp` | Updated tests for optimized reverb |
| `dsp/CMakeLists.txt` | Add fdn_reverb_simd.cpp source |
| `plugins/ruinae/src/plugin_ids.h` | Add kReverbTypeId, bump state version to 5 |
| `plugins/ruinae/src/parameters/reverb_params.h` | Add reverbType field + handlers |
| `plugins/ruinae/src/engine/ruinae_effects_chain.h` | Dual reverb instances + crossfade logic |
| `plugins/ruinae/src/processor/processor.cpp` | State save/load for reverb type |
| `plugins/ruinae/src/processor/processor.h` | Atomic for reverb type |
| `plugins/ruinae/src/controller/controller.cpp` | Register reverb type parameter |

## Key Implementation Patterns

### Gordon-Smith Phasor (replaces std::sin/cos in Dattorro LFO)
```cpp
// Init (in prepare or setParams):
lfoEpsilon_ = 2.0f * std::sin(kPi * modRate / sampleRate_);
sinState_ = 0.0f; cosState_ = 1.0f;  // phase=0 for Dattorro (single oscillator)

// Per-sample (in process):
float lfoA = sinState_ * modDepth * maxExcursion_;
float lfoB = cosState_ * modDepth * maxExcursion_;
sinState_ += lfoEpsilon_ * cosState_;
cosState_ -= lfoEpsilon_ * sinState_;  // uses updated sinState!
```

**Note on FDN LFO initialization**: The FDN reverb uses 4 independent LFO channels rather than one shared oscillator. Each channel is initialized at a quadrature phase offset to maximize decorrelation:
```cpp
// FDN: 4 independent channels, 90° apart (j = 0, 1, 2, 3)
for (int j = 0; j < 4; ++j) {
    lfoSinState_[j] = std::sin(j * kPi / 2.0f);  // 0°, 90°, 180°, 270°
    lfoCosState_[j] = std::cos(j * kPi / 2.0f);
}
// lfoEpsilon_ is shared (same modRate for all channels)
lfoEpsilon_ = 2.0f * std::sin(kPi * modRate / sampleRate_);
```

### Block-Rate Processing (16-sample sub-blocks)
```cpp
void processBlock(float* left, float* right, size_t numSamples) {
    size_t offset = 0;
    while (offset < numSamples) {
        size_t blockLen = std::min(size_t(16), numSamples - offset);
        // Update smoothers + coefficients once per sub-block
        updateBlockRateParams();
        // Process sub-block with held values
        for (size_t i = 0; i < blockLen; ++i) {
            processSample(left[offset+i], right[offset+i]);
        }
        offset += blockLen;
    }
}
```

### Householder Matrix (O(N) for N=8)
```cpp
// y[i] = x[i] - (2/N) * sum(x)
float sum = 0.0f;
for (int i = 0; i < 8; ++i) sum += channels[i];
float scaled = sum * 0.25f;  // 2/8
for (int i = 0; i < 8; ++i) channels[i] -= scaled;
```

### Reverb Type Crossfade (in effects chain)
```cpp
if (reverbCrossfading_) {
    // Process outgoing into temp buffers
    memcpy(tempL, left, n * sizeof(float));
    memcpy(tempR, right, n * sizeof(float));
    processReverb(activeReverbType_, tempL, tempR, n);
    // Process incoming in-place
    processReverb(incomingReverbType_, left, right, n);
    // Equal-power blend
    for (size_t i = 0; i < n; ++i) {
        auto [fadeOut, fadeIn] = equalPowerGains(reverbCrossfadeAlpha_);
        left[i]  = tempL[i] * fadeOut + left[i] * fadeIn;
        right[i] = tempR[i] * fadeOut + right[i] * fadeIn;
        reverbCrossfadeAlpha_ += reverbCrossfadeIncrement_;
        if (reverbCrossfadeAlpha_ >= 1.0f) {
            completReverbCrossfade();
            break;
        }
    }
}
```

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build DSP tests (includes reverb + FDN tests)
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/bin/Release/dsp_tests.exe

# Build Ruinae plugin tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Constitution Compliance Notes

- **Principle II** (Real-Time Safety): All processing methods noexcept, no allocations in process path
- **Principle IV** (SIMD): Scalar-first implementation, Highway SIMD as Phase 2
- **Principle IX** (Layers): FDNReverb at Layer 4, composes Layer 0-1 primitives
- **Principle XIII** (Test-First): Tests written before implementation
- **Principle XIV** (ODR): `FDNReverb` name verified unique in codebase
- **Principle XVI** (Honest Completion): All SC thresholds measured with real benchmarks
