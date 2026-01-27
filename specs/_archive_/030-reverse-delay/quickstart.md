# Quickstart: Reverse Delay Mode

**Feature**: 030-reverse-delay
**Date**: 2025-12-26

## Quick Verification Tests

These tests verify core functionality without exhaustive coverage.

### 1. Basic Reverse Playback (SC-001)

```cpp
TEST_CASE("Basic reverse playback is sample-accurate", "[reverse-delay][SC-001]") {
    ReverseBuffer buffer;
    buffer.prepare(44100.0, 100.0f);  // 100ms max chunk
    buffer.setChunkSizeMs(10.0f);     // 10ms = 441 samples
    buffer.setCrossfadeMs(0.0f);      // No crossfade for precise testing
    buffer.setReversed(true);

    // Feed known pattern: 0, 1, 2, 3... (ascending)
    std::vector<float> input(441);
    std::iota(input.begin(), input.end(), 0.0f);

    std::vector<float> output(441);
    for (size_t i = 0; i < 441; ++i) {
        output[i] = buffer.process(input[i]);
    }

    // First chunk output should be zeros (capturing)
    // After feeding one chunk, output should be input reversed

    // Feed another chunk to get reversed output
    std::vector<float> output2(441);
    for (size_t i = 0; i < 441; ++i) {
        output2[i] = buffer.process(0.0f);  // Silent input
    }

    // output2 should be: 440, 439, 438, ... 1, 0 (reversed first chunk)
    REQUIRE(output2[0] == Approx(440.0f));
    REQUIRE(output2[440] == Approx(0.0f));
}
```

### 2. Crossfade Prevents Clicks (SC-002)

```cpp
TEST_CASE("Crossfade prevents clicks at chunk boundaries", "[reverse-delay][SC-002]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);
    delay.setChunkSizeMs(100.0f);
    delay.setCrossfadePercent(50.0f);
    delay.setDryWetMix(100.0f);  // Wet only

    BlockContext ctx;
    ctx.sampleRate = 44100.0;

    // Process with constant signal
    std::vector<float> left(44100, 0.5f);   // 1 second
    std::vector<float> right(44100, 0.5f);

    delay.process(left.data(), right.data(), 44100, ctx);

    // Check for discontinuities around chunk boundaries
    // Chunk boundary at ~4410 samples (100ms)
    float maxDiff = 0.0f;
    for (size_t i = 4000; i < 4800; ++i) {
        float diff = std::abs(left[i] - left[i-1]);
        maxDiff = std::max(maxDiff, diff);
    }

    // With crossfade, max sample-to-sample difference should be small
    REQUIRE(maxDiff < 0.01f);  // SC-002 threshold
}
```

### 3. Playback Modes Work Correctly (SC-004)

```cpp
TEST_CASE("Playback modes produce correct patterns", "[reverse-delay][SC-004]") {
    SECTION("FullReverse mode reverses every chunk") {
        // Every chunk should be reversed
    }

    SECTION("Alternating mode alternates forward/reverse") {
        // Chunk 1: reverse, Chunk 2: forward, Chunk 3: reverse...
    }

    SECTION("Random mode randomizes per chunk") {
        // Statistical test: roughly 50% reversed over many chunks
    }
}
```

### 4. Feedback Stability (SC-005)

```cpp
TEST_CASE("Feedback at 100% sustains without runaway", "[reverse-delay][SC-005]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);
    delay.setChunkSizeMs(100.0f);
    delay.setFeedbackAmount(1.0f);  // 100%
    delay.setDryWetMix(100.0f);

    BlockContext ctx;
    ctx.sampleRate = 44100.0;

    // Feed impulse
    std::vector<float> left(512, 0.0f);
    std::vector<float> right(512, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;

    delay.process(left.data(), right.data(), 512, ctx);

    // Process many more blocks (10 seconds)
    for (int i = 0; i < 860; ++i) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        delay.process(left.data(), right.data(), 512, ctx);

        // Check output is bounded
        for (float sample : left) {
            REQUIRE(std::abs(sample) <= 1.5f);  // Reasonable bound
        }
    }
}
```

### 5. Latency Reporting (SC-007)

```cpp
TEST_CASE("Latency equals chunk size", "[reverse-delay][SC-007]") {
    ReverseDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    delay.setChunkSizeMs(500.0f);  // 500ms
    size_t expectedSamples = static_cast<size_t>(500.0f * 44100.0f / 1000.0f);

    REQUIRE(delay.getLatencySamples() == expectedSamples);

    delay.setChunkSizeMs(100.0f);  // Change chunk size
    expectedSamples = static_cast<size_t>(100.0f * 44100.0f / 1000.0f);

    REQUIRE(delay.getLatencySamples() == expectedSamples);
}
```

## Integration Test Flow

1. **Prepare** ReverseDelay with typical settings
2. **Feed** known audio pattern (sine wave, impulse, or recorded sample)
3. **Verify** output is time-reversed within chunks
4. **Verify** crossfade smooths chunk transitions
5. **Verify** feedback creates multiple reversed repeats
6. **Verify** filter affects feedback spectrum
7. **Verify** dry/wet mix works correctly

## Manual Verification Checklist

- [ ] Reverse playback sounds "backwards"
- [ ] No clicks at chunk boundaries with crossfade > 0%
- [ ] Mode changes take effect at next chunk (not mid-chunk)
- [ ] Tempo sync locks chunk to beat divisions
- [ ] High feedback creates cascading echoes
- [ ] Filter darkens feedback over time
- [ ] Output gain adjusts final level
- [ ] Reset clears all buffers (no residual audio)
