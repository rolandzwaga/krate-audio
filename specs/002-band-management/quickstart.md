# Quickstart: Band Management Implementation

**Feature**: 002-band-management
**Estimated Time**: 3-4 days (MVP: User Stories 1 & 2 only)

> **Note**: The 3-4 day timeline covers MVP scope (CrossoverNetwork + phase coherence verification). Full tasks.md scope (91 tasks across 14 phases) requires the complete implementation timeline. See tasks.md for full breakdown.

## Prerequisites

- 001-plugin-skeleton complete (Disrumpo loads in DAW)
- Build environment configured (CMake, MSVC/Clang)
- Catch2 test framework available

## Implementation Order

### Day 1: CrossoverNetwork Core

**Task 1.1: Create header files**

```bash
# Create DSP directory structure
mkdir -p plugins/disrumpo/src/dsp
touch plugins/disrumpo/src/dsp/band_state.h
touch plugins/disrumpo/src/dsp/crossover_network.h
touch plugins/disrumpo/src/dsp/band_processor.h
```

**Task 1.2: Implement BandState (15 min)**

Copy structure from data-model.md to `band_state.h`. This is a data-only struct.

**Task 1.3: Write CrossoverNetwork tests first (30 min)**

```cpp
// plugins/disrumpo/tests/unit/crossover_network_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "dsp/crossover_network.h"

TEST_CASE("CrossoverNetwork basic functionality", "[crossover]") {
    Disrumpo::CrossoverNetwork network;

    SECTION("1 band passes input through unchanged") {
        network.prepare(44100.0, 1);
        std::array<float, 8> bands{};
        network.process(1.0f, bands);
        REQUIRE(bands[0] == 1.0f);
    }

    SECTION("2 bands split signal") {
        network.prepare(44100.0, 2);
        // ... test that low + high = input
    }

    SECTION("4 bands sum to flat response") {
        // Pink noise FFT test
    }
}
```

**Task 1.4: Implement CrossoverNetwork (2 hours)**

Key implementation points:
- Use `std::array<CrossoverLR4, 7>` for fixed allocation
- `process()` cascades through active crossovers only
- For 1 band, skip crossover entirely

### Day 2: BandProcessor and Gain/Pan/Mute

**Task 2.1: Write BandProcessor tests first (30 min)**

```cpp
// plugins/disrumpo/tests/unit/band_processing_test.cpp
TEST_CASE("BandProcessor gain", "[band]") {
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);

    SECTION("+6dB doubles amplitude") {
        proc.setGainDb(6.0f);
        // Wait for smoothing or snap
        float left = 1.0f, right = 1.0f;
        // Process many samples to settle
        for (int i = 0; i < 1000; ++i) {
            float l = 1.0f, r = 1.0f;
            proc.process(l, r);
        }
        // Check final gain is ~2.0
    }
}

TEST_CASE("BandProcessor pan", "[band]") {
    // Test equal-power pan law values
}

TEST_CASE("BandProcessor mute", "[band]") {
    // Test mute transition is click-free
}
```

**Task 2.2: Implement BandProcessor (1.5 hours)**

Key implementation points:
- Three `OnePoleSmoother` instances
- dB to linear conversion in setGainDb()
- Equal-power pan law formula from spec

### Day 3: Processor Integration

**Task 3.1: Update plugin_ids.h (30 min)**

Add:
- `BandParamType` enum
- `makeBandParamId()` function
- `kBandCountId` constant (0x0F03)
- Crossover parameter IDs

**Task 3.2: Update Processor class (2 hours)**

Add to `processor.h`:
```cpp
// Band management
std::array<Disrumpo::CrossoverNetwork, 2> crossoverNetworks_;  // L, R
std::array<Disrumpo::BandState, 8> bandStates_;
std::array<Disrumpo::BandProcessor, 8> bandProcessors_;
std::atomic<int> bandCount_{4};
```

Update `process()`:
```cpp
// 1. Process parameter changes
// 2. Split via crossover networks (L and R separately)
// 3. Apply per-band processing
// 4. Sum active bands with solo/mute logic
// 5. Output to buffers
```

**Task 3.3: Update state serialization (1 hour)**

Update `getState()`/`setState()` to include:
- Band count
- Per-band parameters (all 8 for stability)
- Crossover frequencies

### Day 4: Controller and Validation

**Task 4.1: Register parameters in Controller (1 hour)**

```cpp
// controller.cpp
tresult Controller::initialize(FUnknown* context) {
    // ... existing code ...

    // Band count
    parameters->addParameter(new RangeParameter(
        USTRING("Band Count"), kBandCountId,
        USTRING(""), 1.0, 8.0, 4.0));

    // Per-band parameters
    for (int b = 0; b < 8; ++b) {
        parameters->addParameter(/* gain */);
        parameters->addParameter(/* pan */);
        parameters->addParameter(/* solo */);
        parameters->addParameter(/* bypass */);
        parameters->addParameter(/* mute */);
    }
}
```

**Task 4.2: Run validation (1 hour)**

```bash
# Build
cmake --build build/windows-x64-release --config Release

# Run unit tests
ctest --test-dir build/windows-x64-release -C Release --output-on-failure

# Run pluginval
tools/pluginval.exe --strictness-level 1 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"
```

**Task 4.3: Fix any issues and commit**

## Build Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build plugin
"$CMAKE" --build build/windows-x64-release --config Release --target disrumpo

# Build and run tests
"$CMAKE" --build build/windows-x64-release --config Release --target disrumpo_tests
./build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe
```

## Common Pitfalls

1. **Forgetting to prepare() before process()**
   - Add `isPrepared()` check or assert in debug builds

2. **Allocating in audio thread**
   - Use fixed-size `std::array`, not `std::vector`

3. **Smoothing time too short**
   - 10ms default prevents most clicks; if issues persist, increase

4. **Pan formula wrong direction**
   - Verify: pan=-1 should give left=1.0, right=0.0

5. **Solo logic inverted**
   - When ANY solo active, ONLY soloed bands play (unless also muted)

## Verification Checklist

- [ ] 1 band configuration passes input unchanged
- [ ] 4 band summation is flat within +/-0.1 dB
- [ ] Band count change produces no clicks
- [ ] Solo works correctly (soloed band plays, others silent)
- [ ] Multiple solos work (all soloed bands play)
- [ ] Mute overrides solo
- [ ] State saves and restores correctly
- [ ] pluginval passes level 1
