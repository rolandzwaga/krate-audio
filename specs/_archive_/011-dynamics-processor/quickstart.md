# Quickstart: DynamicsProcessor

**Feature**: 011-dynamics-processor
**Date**: 2025-12-23

## Basic Usage

### Simple Compressor

```cpp
#include "dsp/processors/dynamics_processor.h"

using namespace Iterum::DSP;

// Create and configure
DynamicsProcessor comp;
comp.prepare(44100.0, 512);

// Standard compression settings
comp.setThreshold(-20.0f);  // -20 dB threshold
comp.setRatio(4.0f);        // 4:1 ratio
comp.setAttackTime(10.0f);  // 10ms attack
comp.setReleaseTime(100.0f); // 100ms release

// In audio callback
void processAudio(float* buffer, size_t numSamples) {
    comp.process(buffer, numSamples);
}
```

### Hard Limiter

```cpp
DynamicsProcessor limiter;
limiter.prepare(44100.0, 512);

// Limiter settings
limiter.setThreshold(-1.0f);      // -1 dB threshold (near ceiling)
limiter.setRatio(100.0f);         // Infinity:1 (limiter mode)
limiter.setAttackTime(0.1f);      // Fastest attack
limiter.setReleaseTime(50.0f);    // Fast release
limiter.setLookahead(5.0f);       // 5ms lookahead for transparent limiting
```

### Soft Knee Compression

```cpp
DynamicsProcessor softComp;
softComp.prepare(44100.0, 512);

// Transparent compression with soft knee
softComp.setThreshold(-18.0f);
softComp.setRatio(3.0f);
softComp.setKneeWidth(12.0f);     // 12 dB soft knee
softComp.setAttackTime(20.0f);
softComp.setReleaseTime(200.0f);
softComp.setAutoMakeup(true);     // Auto makeup gain
```

### Peak Limiting with Sidechain Filter

```cpp
DynamicsProcessor bassLimiter;
bassLimiter.prepare(44100.0, 512);

// Bass-friendly limiting (reduces pumping)
bassLimiter.setThreshold(-3.0f);
bassLimiter.setRatio(100.0f);
bassLimiter.setDetectionMode(DynamicsDetectionMode::Peak);
bassLimiter.setSidechainEnabled(true);
bassLimiter.setSidechainCutoff(100.0f);  // 100 Hz highpass on detection
bassLimiter.setLookahead(3.0f);
```

## Sample-by-Sample Processing

```cpp
// Per-sample processing for custom workflows
float processSample(float input) {
    float output = comp.processSample(input);

    // Read current gain reduction for metering
    float grDb = comp.getCurrentGainReduction();
    updateGRMeter(grDb);

    return output;
}
```

## Metering

```cpp
// After processing, read gain reduction for UI
void updateMeters() {
    float gainReduction = comp.getCurrentGainReduction();
    // gainReduction is in dB (0 = no reduction, -6 = 6dB reduction)
    meterWidget.setGainReduction(gainReduction);
}
```

## Latency Reporting

```cpp
// Report latency to host when lookahead is enabled
size_t getLatency() {
    return comp.getLatency();  // Returns 0 when lookahead disabled
}
```

## Detection Mode Selection

```cpp
// RMS mode: Average-responding, good for program material
comp.setDetectionMode(DynamicsDetectionMode::RMS);

// Peak mode: Transient-catching, good for limiting
comp.setDetectionMode(DynamicsDetectionMode::Peak);
```

## Typical Use Cases

### Vocal Compression

```cpp
DynamicsProcessor vocalComp;
vocalComp.prepare(sampleRate, blockSize);
vocalComp.setThreshold(-24.0f);   // Catch quiet parts
vocalComp.setRatio(3.0f);         // Gentle ratio
vocalComp.setKneeWidth(6.0f);     // Soft knee for transparency
vocalComp.setAttackTime(15.0f);   // Let transients through
vocalComp.setReleaseTime(150.0f); // Natural decay
vocalComp.setAutoMakeup(true);
```

### Drum Bus Compression

```cpp
DynamicsProcessor drumBus;
drumBus.prepare(sampleRate, blockSize);
drumBus.setThreshold(-12.0f);
drumBus.setRatio(4.0f);
drumBus.setAttackTime(30.0f);     // Slow attack preserves transients
drumBus.setReleaseTime(80.0f);    // Fast release for punch
drumBus.setSidechainEnabled(true);
drumBus.setSidechainCutoff(80.0f); // Reduce kick pumping
```

### Master Limiter

```cpp
DynamicsProcessor masterLimiter;
masterLimiter.prepare(sampleRate, blockSize);
masterLimiter.setThreshold(-0.3f);    // Just below 0 dBFS
masterLimiter.setRatio(100.0f);       // Hard limiting
masterLimiter.setAttackTime(0.1f);    // Instant
masterLimiter.setReleaseTime(100.0f); // Smooth release
masterLimiter.setLookahead(5.0f);     // Transparent limiting
masterLimiter.setDetectionMode(DynamicsDetectionMode::Peak);
```

## Test Scenarios

### TC-001: Basic Compression Verification

```cpp
TEST_CASE("Basic compression applies correct gain reduction") {
    DynamicsProcessor comp;
    comp.prepare(44100.0, 512);
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setKneeWidth(0.0f);  // Hard knee
    comp.setAttackTime(0.1f); // Fast attack for test

    // Feed constant signal at -10 dB (10 dB above threshold)
    // Expected: 7.5 dB gain reduction (10 * (1 - 1/4))
    // Output should be approximately -10 - 7.5 = -17.5 dB

    float input = dbToGain(-10.0f);  // -10 dB input

    // Process enough samples for attack to complete
    for (int i = 0; i < 4410; ++i) {  // 100ms
        comp.processSample(input);
    }

    float grDb = comp.getCurrentGainReduction();
    REQUIRE(grDb == Approx(-7.5f).margin(0.1f));
}
```

### TC-002: Limiter Mode Verification

```cpp
TEST_CASE("Limiter mode clamps output to threshold") {
    DynamicsProcessor limiter;
    limiter.prepare(44100.0, 512);
    limiter.setThreshold(-6.0f);
    limiter.setRatio(100.0f);  // Limiter mode
    limiter.setAttackTime(0.1f);
    limiter.setLookahead(5.0f);  // For accurate limiting

    // Process signal at 0 dB (6 dB above threshold)
    float input = 1.0f;  // 0 dB

    // After attack settles, output should not exceed threshold
    for (int i = 0; i < 4410; ++i) {
        float output = limiter.processSample(input);
        // Allow for attack transient
        if (i > 500) {
            float outputDb = gainToDb(std::abs(output));
            REQUIRE(outputDb <= -5.9f);  // -6 dB with margin
        }
    }
}
```

### TC-003: Soft Knee Smoothness

```cpp
TEST_CASE("Soft knee has no discontinuities") {
    DynamicsProcessor comp;
    comp.prepare(44100.0, 512);
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setKneeWidth(12.0f);

    // Sweep through knee region and check for smooth transitions
    std::vector<float> gains;
    for (float db = -30.0f; db <= -10.0f; db += 0.5f) {
        comp.reset();
        float input = dbToGain(db);

        // Let it settle
        for (int i = 0; i < 1000; ++i) {
            comp.processSample(input);
        }
        gains.push_back(comp.getCurrentGainReduction());
    }

    // Check that gain reduction changes smoothly (no jumps)
    for (size_t i = 1; i < gains.size(); ++i) {
        float delta = std::abs(gains[i] - gains[i-1]);
        REQUIRE(delta < 1.0f);  // No sudden jumps
    }
}
```
