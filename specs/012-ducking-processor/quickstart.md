# Quickstart: Ducking Processor

**Feature**: 012-ducking-processor
**Date**: 2025-12-23

## Basic Usage

### Setup and Configuration

```cpp
#include "dsp/processors/ducking_processor.h"

using namespace Iterum::DSP;

// Create processor
DuckingProcessor ducker;

// Initialize for sample rate and buffer size
ducker.prepare(44100.0, 512);

// Configure for voiceover ducking
ducker.setThreshold(-30.0f);   // Duck when sidechain exceeds -30 dB
ducker.setDepth(-12.0f);       // Reduce main by up to 12 dB
ducker.setAttackTime(10.0f);   // Fast attack (10ms)
ducker.setReleaseTime(200.0f); // Medium release (200ms)
ducker.setHoldTime(100.0f);    // Hold ducking for 100ms after voice stops
```

### Per-Sample Processing

```cpp
// In your audio callback
void processAudio(float* mainBuffer, float* sidechainBuffer,
                  float* outputBuffer, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        outputBuffer[i] = ducker.processSample(mainBuffer[i], sidechainBuffer[i]);
    }
}
```

### Block Processing

```cpp
// More efficient for larger buffers
void processAudio(float* mainBuffer, float* sidechainBuffer,
                  float* outputBuffer, size_t numSamples) {
    ducker.process(mainBuffer, sidechainBuffer, outputBuffer, numSamples);
}

// In-place processing (overwrites main buffer)
void processAudioInPlace(float* mainBuffer, float* sidechainBuffer, size_t numSamples) {
    ducker.process(mainBuffer, sidechainBuffer, numSamples);
}
```

## Common Use Cases

### Voiceover Ducking (Podcast/Radio)

Music ducks when voice is detected:

```cpp
DuckingProcessor ducker;
ducker.prepare(sampleRate, blockSize);

// Voice detection triggers ducking
ducker.setThreshold(-40.0f);   // Sensitive to voice
ducker.setDepth(-15.0f);       // Significant reduction
ducker.setAttackTime(5.0f);    // Quick response to voice
ducker.setReleaseTime(500.0f); // Slow return to avoid pumping
ducker.setHoldTime(200.0f);    // Hold during breath pauses

// Enable sidechain HPF to focus on voice frequencies
ducker.setSidechainFilterEnabled(true);
ducker.setSidechainFilterCutoff(150.0f);  // Ignore bass rumble
```

### DJ/EDM Sidechain Pumping

Rhythmic ducking for electronic music:

```cpp
DuckingProcessor ducker;
ducker.prepare(sampleRate, blockSize);

// Aggressive ducking on kick drum
ducker.setThreshold(-20.0f);   // Trigger on loud kicks
ducker.setDepth(-24.0f);       // Deep ducking
ducker.setAttackTime(1.0f);    // Instant attack
ducker.setReleaseTime(100.0f); // Quick release for pumping effect
ducker.setHoldTime(0.0f);      // No hold for rhythmic feel

// No sidechain filter - respond to kick frequencies
ducker.setSidechainFilterEnabled(false);
```

### Subtle Background Reduction

Gentle ducking for ambient audio:

```cpp
DuckingProcessor ducker;
ducker.prepare(sampleRate, blockSize);

// Gentle ducking that remains audible
ducker.setThreshold(-35.0f);
ducker.setDepth(-6.0f);        // Only 6 dB reduction
ducker.setRange(-6.0f);        // Same as depth (redundant but explicit)
ducker.setAttackTime(50.0f);   // Slow attack for transparency
ducker.setReleaseTime(1000.0f);// Very slow release
ducker.setHoldTime(500.0f);    // Long hold for continuity
```

### Range-Limited Ducking

Never completely silence the background:

```cpp
DuckingProcessor ducker;
ducker.prepare(sampleRate, blockSize);

ducker.setDepth(-48.0f);       // Would be very deep...
ducker.setRange(-12.0f);       // ...but limited to -12 dB max

// Background always remains at least -12 dB from original level
```

## Metering

```cpp
// Get current gain reduction for UI display
float grDb = ducker.getCurrentGainReduction();
// Returns negative value when ducking (e.g., -8.5 dB)
// Returns 0.0 when not ducking
```

## Lifecycle Management

```cpp
// When sample rate changes
void sampleRateChanged(double newRate) {
    ducker.prepare(newRate, blockSize);
}

// When playback stops
void onStop() {
    ducker.reset();  // Clear all state
}

// Latency reporting (always 0 for ducking)
size_t getLatency() {
    return ducker.getLatency();  // Returns 0
}
```

## Test Scenarios

### US1: Basic Ducking

```cpp
TEST_CASE("Ducking applies gain reduction when sidechain exceeds threshold") {
    DuckingProcessor ducker;
    ducker.prepare(44100.0, 512);
    ducker.setThreshold(-30.0f);
    ducker.setDepth(-12.0f);
    ducker.setAttackTime(0.1f);  // Near-instant for test

    // Feed sidechain above threshold (-20 dB > -30 dB threshold)
    float sidechainLevel = dbToGain(-20.0f);  // ~0.1

    // Process with unity main signal
    float output = ducker.processSample(1.0f, sidechainLevel);

    // Should be attenuated
    REQUIRE(output < 1.0f);
    REQUIRE(gainToDb(output) < 0.0f);
}
```

### US3: Hold Time

```cpp
TEST_CASE("Hold time delays release after sidechain drops") {
    DuckingProcessor ducker;
    ducker.prepare(44100.0, 512);
    ducker.setThreshold(-30.0f);
    ducker.setDepth(-12.0f);
    ducker.setHoldTime(50.0f);  // 50ms hold
    ducker.setAttackTime(0.1f);
    ducker.setReleaseTime(0.1f);

    // Trigger ducking
    float loudSidechain = dbToGain(-10.0f);
    for (int i = 0; i < 100; ++i) {
        ducker.processSample(1.0f, loudSidechain);
    }

    // Drop sidechain below threshold
    float quietSidechain = dbToGain(-50.0f);

    // During hold period, GR should remain applied
    float grDuringHold = ducker.getCurrentGainReduction();
    for (int i = 0; i < 10; ++i) {
        ducker.processSample(1.0f, quietSidechain);
    }

    // Should still be ducking (hold not expired)
    REQUIRE(ducker.getCurrentGainReduction() < 0.0f);
}
```

### US5: Sidechain Filter

```cpp
TEST_CASE("Sidechain HPF reduces triggering from bass content") {
    DuckingProcessor ducker;
    ducker.prepare(44100.0, 512);
    ducker.setThreshold(-30.0f);
    ducker.setDepth(-12.0f);
    ducker.setSidechainFilterEnabled(true);
    ducker.setSidechainFilterCutoff(200.0f);

    // Generate low-frequency sidechain (50 Hz)
    // With 200 Hz HPF, this should be attenuated before detection
    // Result: less ducking than without filter
}
```
