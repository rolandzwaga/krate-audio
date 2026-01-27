# Pattern Freeze Mode - Quickstart Guide

**Feature Branch**: `069-pattern-freeze`
**Created**: 2026-01-16

## Overview

Pattern Freeze Mode extends the existing freeze functionality with pattern-triggered playback of captured audio slices. Instead of simply looping a frozen buffer, Pattern Freeze allows rhythmic, granular, and drone-based manipulation of captured audio.

## Prerequisites

Before implementing, ensure familiarity with:
- Existing `FreezeMode` in `dsp/effects/freeze_mode.h`
- `GranularEngine` patterns in `dsp/systems/granular_engine.h`
- `BlockContext` for tempo information
- Real-time safety requirements (no allocations in process())

## Quick Implementation Reference

### 1. Create EuclideanPattern (Layer 0)

```cpp
// dsp/include/krate/dsp/core/euclidean_pattern.h

namespace Krate::DSP {

class EuclideanPattern {
public:
    static constexpr int kMaxSteps = 32;

    [[nodiscard]] static uint32_t generate(int pulses, int steps, int rotation = 0) noexcept {
        uint32_t pattern = 0;
        for (int i = 0; i < steps; ++i) {
            // Accumulator method: distribute pulses evenly
            if (((pulses * ((i + rotation) % steps)) % steps) + pulses >= steps) {
                pattern |= (1u << i);
            }
        }
        return pattern;
    }

    [[nodiscard]] static bool isHit(uint32_t pattern, int position, int steps) noexcept {
        return (position >= 0 && position < steps) && ((pattern >> position) & 1);
    }
};

} // namespace Krate::DSP
```

### 2. Create RollingCaptureBuffer (Layer 1)

```cpp
// dsp/include/krate/dsp/primitives/rolling_capture_buffer.h

namespace Krate::DSP {

class RollingCaptureBuffer {
public:
    void prepare(double sampleRate, float maxSeconds = 5.0f) noexcept {
        bufferL_.prepare(sampleRate, maxSeconds);
        bufferR_.prepare(sampleRate, maxSeconds);
        maxSamples_ = bufferL_.maxDelaySamples();
        minReadySamples_ = static_cast<size_t>(sampleRate * 0.2);  // 200ms
        samplesRecorded_ = 0;
        sampleRate_ = sampleRate;
    }

    void write(float left, float right) noexcept {
        bufferL_.write(left);
        bufferR_.write(right);
        if (samplesRecorded_ < maxSamples_) ++samplesRecorded_;
    }

    std::pair<float, float> read(float delaySamples) const noexcept {
        return { bufferL_.readLinear(delaySamples), bufferR_.readLinear(delaySamples) };
    }

    [[nodiscard]] float getFillLevel() const noexcept {
        return 100.0f * static_cast<float>(samplesRecorded_) / static_cast<float>(maxSamples_);
    }

    [[nodiscard]] bool isReady() const noexcept {
        return samplesRecorded_ >= minReadySamples_;
    }

private:
    DelayLine bufferL_, bufferR_;
    size_t samplesRecorded_ = 0, maxSamples_ = 0, minReadySamples_ = 0;
    double sampleRate_ = 44100.0;
};

} // namespace Krate::DSP
```

### 3. Implement Voice Stealing in SlicePool

```cpp
Slice* SlicePool::stealShortestRemaining() noexcept {
    Slice* victim = nullptr;
    float highestPhase = -1.0f;

    for (auto& slice : slices_) {
        if (slice.active && slice.envelopePhase > highestPhase) {
            highestPhase = slice.envelopePhase;
            victim = &slice;
        }
    }

    if (victim) {
        // Apply 2ms micro-fade to prevent click
        victim->inRelease = true;
        victim->releaseIncrement = 1.0f / (0.002f * static_cast<float>(sampleRate_));
    }

    return victim;
}
```

### 4. Implement Poisson Triggering

```cpp
float PatternScheduler::generatePoissonInterval(float density) noexcept {
    float u = rng_.nextUnipolar();
    u = std::max(u, 1e-7f);  // Prevent log(0)
    return -std::log(u) / density * static_cast<float>(sampleRate_);
}

PatternScheduler::TriggerResult PatternScheduler::processGranular() noexcept {
    TriggerResult result{};

    granular_.samplesUntilNext -= 1.0f;
    if (granular_.samplesUntilNext <= 0.0f) {
        result.triggered = true;
        result.positionJitter = rng_.nextUnipolar() * granular_.positionJitter;
        result.sizeMultiplier = 1.0f + (rng_.nextFloat() * granular_.sizeJitter * 0.5f);
        granular_.samplesUntilNext += generatePoissonInterval(granular_.density);
    }

    return result;
}
```

### 5. Process Pattern Type

```cpp
void PatternFreezeMode::process(float* left, float* right, size_t numSamples,
                                 const BlockContext& ctx) noexcept {
    // Always record to capture buffer
    for (size_t i = 0; i < numSamples; ++i) {
        captureBuffer_.write(left[i], right[i]);
    }

    if (!freezeEnabled_) {
        // Pass through to normal delay processing
        return;
    }

    // Clear output for accumulation
    std::fill_n(mixBufferL_.data(), numSamples, 0.0f);
    std::fill_n(mixBufferR_.data(), numSamples, 0.0f);

    switch (patternType_) {
        case PatternType::Euclidean:
        case PatternType::GranularScatter:
            processSlicePlayback(left, right, numSamples, ctx);
            break;
        case PatternType::HarmonicDrones:
            processHarmonicDrones(left, right, numSamples, ctx);
            break;
        case PatternType::NoiseBursts:
            processNoiseBursts(left, right, numSamples, ctx);
            break;
        case PatternType::Legacy:
            processLegacy(left, right, numSamples, ctx);
            break;
    }

    // Apply processing chain (pitch, diffusion, filter, decay)
    feedbackProcessor_.process(mixBufferL_.data(), mixBufferR_.data(), numSamples, ctx);

    // Copy to output
    std::copy_n(mixBufferL_.data(), numSamples, left);
    std::copy_n(mixBufferR_.data(), numSamples, right);
}
```

## Key Algorithms

### Euclidean Rhythm Formula

```
isHit(i) = (((pulses * (i + rotation)) % steps) + pulses) >= steps
```

This distributes `pulses` hits as evenly as possible across `steps` total positions.

### Exponential Distribution for Poisson

```
interval = -ln(U) / lambda
```

Where U is uniform random [0,1] and lambda is target density (grains/second).

### Gain Compensation for Multi-Voice

```
voiceGain = 1.0f / std::sqrt(static_cast<float>(voiceCount))
```

Prevents level explosion with overlapping voices.

## Testing Checklist

- [ ] EuclideanPattern generates correct patterns (E(3,8) = tresillo)
- [ ] RollingCaptureBuffer reports fill level correctly
- [ ] SlicePool steals shortest-remaining voice
- [ ] Granular density matches target +/- 20%
- [ ] No clicks at slice boundaries with 10ms+ envelope
- [ ] Legacy pattern matches existing FreezeMode output
- [ ] CPU < 5% with 8 simultaneous grains
- [ ] Pattern crossfade is click-free

## Common Pitfalls

1. **Forgetting to always record**: Buffer must record even when frozen
2. **Division by zero in Poisson**: Clamp random value away from 0
3. **Envelope clicks**: Ensure minimum 10ms attack/release
4. **Voice stealing clicks**: Apply micro-fade before reusing slice
5. **Tempo sync errors**: Check BlockContext.isPlaying before using tempo

## File Locations

| Component | Location |
|-----------|----------|
| EuclideanPattern | `dsp/include/krate/dsp/core/euclidean_pattern.h` |
| RollingCaptureBuffer | `dsp/include/krate/dsp/primitives/rolling_capture_buffer.h` |
| SlicePool | `dsp/include/krate/dsp/primitives/slice_pool.h` |
| PatternScheduler | `dsp/include/krate/dsp/processors/pattern_scheduler.h` |
| PatternFreezeMode | `dsp/include/krate/dsp/effects/pattern_freeze_mode.h` |

---

*Quickstart created: 2026-01-16*
