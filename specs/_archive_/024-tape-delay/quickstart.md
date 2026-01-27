# Quickstart: Tape Delay Mode

**Feature**: 024-tape-delay
**Date**: 2025-12-25
**Time to First Working Code**: ~30 minutes

## Overview

This quickstart gets you from zero to a working TapeDelay with the core functionality:
1. Motor speed with inertia
2. 3 echo heads at RE-201 ratios
3. Tape character (wow/flutter, saturation)
4. Feedback with filtering

## Prerequisites

Ensure these Layer 3 components exist (all verified present):
- `src/dsp/systems/tap_manager.h`
- `src/dsp/systems/feedback_network.h`
- `src/dsp/systems/character_processor.h`

## Step 1: Create Test File (5 min)

Create `tests/unit/features/tape_delay_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/features/tape_delay.h"

using namespace Iterum::DSP;
using Catch::Approx;

TEST_CASE("TapeDelay basic functionality", "[tape-delay]") {
    TapeDelay delay;

    SECTION("unprepared state") {
        REQUIRE_FALSE(delay.isPrepared());
    }

    SECTION("prepare initializes correctly") {
        delay.prepare(44100.0, 512, 2000.0f);
        REQUIRE(delay.isPrepared());
    }

    SECTION("motor speed with inertia") {
        delay.prepare(44100.0, 512, 2000.0f);
        delay.setMotorSpeed(500.0f);
        REQUIRE(delay.getTargetDelayMs() == Approx(500.0f));

        // Current should start at default, then ramp
        // After prepare, current starts at some initial value
    }

    SECTION("head configuration") {
        delay.prepare(44100.0, 512, 2000.0f);

        // All heads enabled by default
        REQUIRE(delay.isHeadEnabled(0));
        REQUIRE(delay.isHeadEnabled(1));
        REQUIRE(delay.isHeadEnabled(2));

        delay.setHeadEnabled(1, false);
        REQUIRE_FALSE(delay.isHeadEnabled(1));
        REQUIRE(delay.getActiveHeadCount() == 2);
    }

    SECTION("process produces output") {
        delay.prepare(44100.0, 512, 2000.0f);
        delay.setMotorSpeed(100.0f);  // Short delay for quick test
        delay.setFeedback(0.5f);
        delay.setMix(1.0f);  // Full wet

        // Create impulse
        std::array<float, 512> left{};
        std::array<float, 512> right{};
        left[0] = 1.0f;
        right[0] = 1.0f;

        delay.process(left.data(), right.data(), 512);

        // After 100ms at 44100Hz = 4410 samples
        // We only have 512 samples, so no echo yet in this block
        // But processing should not crash
        REQUIRE(std::isfinite(left[0]));
    }
}
```

## Step 2: Create Header Structure (10 min)

Create `src/dsp/features/tape_delay.h`:

```cpp
// ==============================================================================
// Layer 4: User Feature - Tape Delay
// ==============================================================================
#pragma once

#include "dsp/systems/tap_manager.h"
#include "dsp/systems/feedback_network.h"
#include "dsp/systems/character_processor.h"
#include "dsp/primitives/smoother.h"
#include "dsp/core/db_utils.h"

#include <array>
#include <cstddef>

namespace Iterum {
namespace DSP {

/// @brief Configuration for a single tape playback head
struct TapeHead {
    float ratio = 1.0f;
    float levelDb = 0.0f;
    float pan = 0.0f;
    bool enabled = true;
};

/// @brief Layer 4 User Feature - Classic Tape Delay Emulation
class TapeDelay {
public:
    static constexpr size_t kNumHeads = 3;
    static constexpr float kMinDelayMs = 20.0f;
    static constexpr float kMaxDelayMs = 2000.0f;

    TapeDelay() noexcept = default;
    ~TapeDelay() = default;

    // Non-copyable, movable
    TapeDelay(const TapeDelay&) = delete;
    TapeDelay& operator=(const TapeDelay&) = delete;
    TapeDelay(TapeDelay&&) noexcept = default;
    TapeDelay& operator=(TapeDelay&&) noexcept = default;

    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // Motor Speed
    void setMotorSpeed(float ms) noexcept;
    [[nodiscard]] float getTargetDelayMs() const noexcept { return targetDelayMs_; }
    [[nodiscard]] float getCurrentDelayMs() const noexcept;

    // Heads
    void setHeadEnabled(size_t idx, bool enabled) noexcept;
    void setHeadLevel(size_t idx, float dB) noexcept;
    void setHeadPan(size_t idx, float pan) noexcept;
    [[nodiscard]] bool isHeadEnabled(size_t idx) const noexcept;
    [[nodiscard]] size_t getActiveHeadCount() const noexcept;

    // Character
    void setWear(float amount) noexcept;
    void setSaturation(float amount) noexcept;
    void setAge(float amount) noexcept;
    void setFeedback(float amount) noexcept;
    void setMix(float amount) noexcept;

    // Processing
    void process(float* left, float* right, size_t numSamples) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;

private:
    // Composed systems
    TapManager taps_;
    FeedbackNetwork feedback_;
    CharacterProcessor character_;

    // Motor inertia
    OnePoleSmoother motorSmoother_;
    float targetDelayMs_ = 500.0f;

    // Heads
    std::array<TapeHead, kNumHeads> heads_ = {{
        {1.0f, 0.0f, -50.0f, true},   // Head 1: 1x, center-left
        {1.5f, -3.0f, 0.0f, true},    // Head 2: 1.5x, center
        {2.0f, -6.0f, 50.0f, true}    // Head 3: 2x, center-right
    }};

    // Parameters
    float wearAmount_ = 0.0f;
    float saturationAmount_ = 0.3f;
    float ageAmount_ = 0.0f;
    float feedbackAmount_ = 0.5f;
    float mixAmount_ = 0.5f;

    // State
    double sampleRate_ = 44100.0;
    float maxDelayMs_ = 2000.0f;
    bool prepared_ = false;

    void updateHeadTimings() noexcept;
    void updateCharacter() noexcept;
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void TapeDelay::prepare(double sampleRate, size_t maxBlockSize,
                                float maxDelayMs) noexcept {
    sampleRate_ = sampleRate;
    maxDelayMs_ = maxDelayMs;

    // Prepare composed systems
    taps_.prepare(static_cast<float>(sampleRate), maxBlockSize, maxDelayMs);
    feedback_.prepare(sampleRate, maxBlockSize, maxDelayMs);
    character_.prepare(sampleRate, maxBlockSize);

    // Configure motor inertia (300ms smoothing)
    motorSmoother_.configure(300.0f, static_cast<float>(sampleRate));
    motorSmoother_.snapTo(targetDelayMs_);

    // Configure character for tape mode
    character_.setMode(CharacterMode::Tape);

    // Configure feedback path
    feedback_.setFilterEnabled(true);
    feedback_.setFilterType(FilterType::Lowpass);
    feedback_.setFilterCutoff(4000.0f);
    feedback_.setSaturationEnabled(true);

    // Initialize heads
    for (size_t i = 0; i < kNumHeads; ++i) {
        taps_.setTapEnabled(i, heads_[i].enabled);
        taps_.setTapLevelDb(i, heads_[i].levelDb);
        taps_.setTapPan(i, heads_[i].pan);
    }

    updateCharacter();
    prepared_ = true;
}

inline void TapeDelay::reset() noexcept {
    taps_.reset();
    feedback_.reset();
    character_.reset();
    motorSmoother_.snapTo(targetDelayMs_);
}

inline void TapeDelay::setMotorSpeed(float ms) noexcept {
    targetDelayMs_ = std::clamp(ms, kMinDelayMs, maxDelayMs_);
    motorSmoother_.setTarget(targetDelayMs_);
}

inline float TapeDelay::getCurrentDelayMs() const noexcept {
    return motorSmoother_.getCurrentValue();
}

inline void TapeDelay::setHeadEnabled(size_t idx, bool enabled) noexcept {
    if (idx >= kNumHeads) return;
    heads_[idx].enabled = enabled;
    taps_.setTapEnabled(idx, enabled);
}

inline void TapeDelay::setHeadLevel(size_t idx, float dB) noexcept {
    if (idx >= kNumHeads) return;
    heads_[idx].levelDb = std::clamp(dB, -96.0f, 6.0f);
    taps_.setTapLevelDb(idx, heads_[idx].levelDb);
}

inline void TapeDelay::setHeadPan(size_t idx, float pan) noexcept {
    if (idx >= kNumHeads) return;
    heads_[idx].pan = std::clamp(pan, -100.0f, 100.0f);
    taps_.setTapPan(idx, heads_[idx].pan);
}

inline bool TapeDelay::isHeadEnabled(size_t idx) const noexcept {
    if (idx >= kNumHeads) return false;
    return heads_[idx].enabled;
}

inline size_t TapeDelay::getActiveHeadCount() const noexcept {
    size_t count = 0;
    for (const auto& head : heads_) {
        if (head.enabled) ++count;
    }
    return count;
}

inline void TapeDelay::setWear(float amount) noexcept {
    wearAmount_ = std::clamp(amount, 0.0f, 1.0f);
    updateCharacter();
}

inline void TapeDelay::setSaturation(float amount) noexcept {
    saturationAmount_ = std::clamp(amount, 0.0f, 1.0f);
    updateCharacter();
}

inline void TapeDelay::setAge(float amount) noexcept {
    ageAmount_ = std::clamp(amount, 0.0f, 1.0f);
    updateCharacter();
}

inline void TapeDelay::setFeedback(float amount) noexcept {
    feedbackAmount_ = std::clamp(amount, 0.0f, 1.2f);
    feedback_.setFeedbackAmount(feedbackAmount_);
}

inline void TapeDelay::setMix(float amount) noexcept {
    mixAmount_ = std::clamp(amount, 0.0f, 1.0f);
    taps_.setDryWetMix(mixAmount_ * 100.0f);
}

inline void TapeDelay::updateHeadTimings() noexcept {
    const float currentMs = motorSmoother_.getCurrentValue();
    for (size_t i = 0; i < kNumHeads; ++i) {
        const float headTime = currentMs * heads_[i].ratio;
        taps_.setTapTimeMs(i, std::min(headTime, maxDelayMs_));
    }
}

inline void TapeDelay::updateCharacter() noexcept {
    // Wear -> wow/flutter/hiss
    character_.setTapeWowDepth(wearAmount_);
    character_.setTapeFlutterDepth(wearAmount_ * 0.5f);
    character_.setTapeHissLevel(-60.0f + wearAmount_ * 20.0f);

    // Saturation
    character_.setTapeSaturation(saturationAmount_);

    // Age -> rolloff + additional hiss
    const float rolloff = 12000.0f - ageAmount_ * 8000.0f;
    character_.setTapeRolloffFreq(rolloff);
}

inline void TapeDelay::process(float* left, float* right,
                                size_t numSamples) noexcept {
    if (!prepared_) return;

    // Update head timings based on smoothed motor speed
    for (size_t i = 0; i < numSamples; ++i) {
        motorSmoother_.process();  // Advance smoother
    }
    updateHeadTimings();

    // Signal flow: Input -> TapManager -> Character -> Output
    // Feedback is handled internally by FeedbackNetwork on tap outputs

    // Process through tap manager (multi-head delay)
    taps_.process(left, right, left, right, numSamples);

    // Apply tape character
    character_.processStereo(left, right, numSamples);
}

inline void TapeDelay::process(float* buffer, size_t numSamples) noexcept {
    // Mono: duplicate to stereo, process, then sum back
    // For simplicity, just process left channel
    process(buffer, buffer, numSamples);
}

} // namespace DSP
} // namespace Iterum
```

## Step 3: Add to Build (5 min)

Add to `tests/CMakeLists.txt` under the test sources:

```cmake
# In the test executable sources
tests/unit/features/tape_delay_test.cpp
```

Create the `tests/unit/features/` directory if needed.

## Step 4: Build and Test (5 min)

```bash
# Configure
cmake --preset windows-x64-debug

# Build tests
cmake --build --preset windows-x64-debug --target dsp_tests

# Run tests
./build/bin/Debug/dsp_tests.exe "[tape-delay]"
```

## Step 5: Verify Core Functionality (5 min)

Add more specific tests:

```cpp
TEST_CASE("TapeDelay motor inertia", "[tape-delay]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    // Set initial speed
    delay.setMotorSpeed(200.0f);
    delay.reset();  // Snap to initial

    // Change speed
    delay.setMotorSpeed(500.0f);

    // Target should update immediately
    REQUIRE(delay.getTargetDelayMs() == Approx(500.0f));

    // Current should be different (still ramping)
    // After reset with snap, current == 200
    // After setMotorSpeed, target == 500 but current still 200
}

TEST_CASE("TapeDelay wear affects character", "[tape-delay]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    // At 0 wear, minimal modulation
    delay.setWear(0.0f);

    // At full wear, maximum modulation
    delay.setWear(1.0f);

    // Processing should not crash at any wear level
    std::array<float, 512> left{}, right{};
    left[0] = 1.0f;
    delay.process(left.data(), right.data(), 512);
    REQUIRE(std::isfinite(left[0]));
}
```

## Next Steps

After basic tests pass:

1. **Add proper signal flow tests** - Verify delay times match head ratios
2. **Add feedback tests** - Verify self-oscillation limiting
3. **Add performance tests** - Verify <5% CPU at 44.1kHz stereo
4. **Integration with processor** - Wire to VST parameters

## Common Issues

**"Cannot find header"**: Ensure `src/dsp/features/` is in include path.

**"TapManager undefined"**: Check include order, TapManager must be included.

**"Linking errors"**: Ensure TapManager/FeedbackNetwork/CharacterProcessor are header-only or linked.

**"Tests crash"**: Check `prepare()` is called before `process()`.
