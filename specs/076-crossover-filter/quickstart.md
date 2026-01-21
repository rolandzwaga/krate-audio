# Quickstart: Crossover Filter (Linkwitz-Riley)

**Feature**: 076-crossover-filter | **Layer**: 2 (DSP Processors)

## Installation

Include the header in your DSP code:

```cpp
#include <krate/dsp/processors/crossover_filter.h>

using namespace Krate::DSP;
```

---

## Basic Usage

### 2-Way Crossover (CrossoverLR4)

Split audio into low and high bands:

```cpp
class MyProcessor {
    CrossoverLR4 crossover_;

public:
    void prepare(double sampleRate) {
        crossover_.prepare(sampleRate);
        crossover_.setCrossoverFrequency(1000.0f);  // 1kHz split point
    }

    void processBlock(float* buffer, size_t numSamples) {
        std::vector<float> low(numSamples);
        std::vector<float> high(numSamples);

        crossover_.processBlock(buffer, low.data(), high.data(), numSamples);

        // Process bands independently
        processLowBand(low.data(), numSamples);
        processHighBand(high.data(), numSamples);

        // Recombine (optional - outputs sum to flat)
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = low[i] + high[i];
        }
    }
};
```

### Single-Sample Processing

For sample-by-sample processing (e.g., in feedback loops):

```cpp
float processSample(float input) {
    auto [low, high] = crossover_.process(input);

    // Apply different processing to each band
    low = saturate(low);
    high = excite(high);

    return low + high;  // Recombine
}
```

---

## 3-Way Multiband Processing

Split into Low/Mid/High for multiband compression:

```cpp
class MultibandCompressor {
    Crossover3Way crossover_;
    Compressor lowComp_, midComp_, highComp_;

public:
    void prepare(double sampleRate) {
        crossover_.prepare(sampleRate);
        crossover_.setLowMidFrequency(300.0f);   // 300Hz
        crossover_.setMidHighFrequency(3000.0f); // 3kHz

        lowComp_.prepare(sampleRate);
        midComp_.prepare(sampleRate);
        highComp_.prepare(sampleRate);
    }

    void processBlock(float* buffer, size_t numSamples) {
        std::vector<float> low(numSamples), mid(numSamples), high(numSamples);

        crossover_.processBlock(buffer, low.data(), mid.data(),
                                high.data(), numSamples);

        // Compress each band independently
        lowComp_.processBlock(low.data(), numSamples);
        midComp_.processBlock(mid.data(), numSamples);
        highComp_.processBlock(high.data(), numSamples);

        // Recombine
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = low[i] + mid[i] + high[i];
        }
    }
};
```

---

## 4-Way Bass Management

Split into Sub/Low/Mid/High for speaker management:

```cpp
class BassManager {
    Crossover4Way crossover_;

public:
    void prepare(double sampleRate) {
        crossover_.prepare(sampleRate);
        crossover_.setSubLowFrequency(80.0f);    // Sub < 80Hz
        crossover_.setLowMidFrequency(250.0f);   // Low 80-250Hz
        crossover_.setMidHighFrequency(4000.0f); // Mid 250Hz-4kHz, High > 4kHz
    }

    void processBlock(float* input, size_t numSamples,
                      float* subOut, float* mainOut) {
        std::vector<float> sub(numSamples), low(numSamples);
        std::vector<float> mid(numSamples), high(numSamples);

        crossover_.processBlock(input, sub.data(), low.data(),
                                mid.data(), high.data(), numSamples);

        // Route sub to subwoofer
        std::copy(sub.begin(), sub.end(), subOut);

        // Combine rest for mains
        for (size_t i = 0; i < numSamples; ++i) {
            mainOut[i] = low[i] + mid[i] + high[i];
        }
    }
};
```

---

## Real-Time Parameter Modulation

For smooth frequency sweeps (e.g., from automation):

```cpp
class SweepableCrossover {
    CrossoverLR4 crossover_;

public:
    void prepare(double sampleRate) {
        crossover_.prepare(sampleRate);
        crossover_.setSmoothingTime(5.0f);  // 5ms smoothing (default)

        // For critical modulation, use high-accuracy tracking
        crossover_.setTrackingMode(TrackingMode::HighAccuracy);
    }

    // Call from UI thread or automation
    void setFrequency(float hz) {
        // Thread-safe: atomic write
        crossover_.setCrossoverFrequency(hz);
    }

    // Call from audio thread
    void processBlock(float* buffer, size_t numSamples) {
        // Frequency changes are automatically smoothed
        // No clicks even during rapid automation
        // ...
    }
};
```

---

## Tracking Mode Selection

Choose coefficient recalculation strategy based on your use case:

```cpp
// Default: Efficient mode (minimal CPU overhead)
crossover_.setTrackingMode(TrackingMode::Efficient);
// - Recalculates coefficients only when frequency changes by >=0.1Hz
// - Best for: static crossovers, slow automation

// High-accuracy mode (maximum precision)
crossover_.setTrackingMode(TrackingMode::HighAccuracy);
// - Recalculates coefficients every sample during smoothing
// - Best for: LFO modulation, critical frequency-sweep effects
```

---

## Sample Rate Changes

Handle sample rate changes correctly:

```cpp
void setSampleRate(double newSampleRate) {
    // prepare() resets filter states and reinitializes coefficients
    crossover_.prepare(newSampleRate);

    // Note: crossover frequency target is preserved
    // but will be re-clamped to new Nyquist limit
}
```

---

## Integration with VST3 Plugin

Example integration in a VST3 processor:

```cpp
// In Processor.h
class Processor : public Steinberg::Vst::AudioEffect {
    Krate::DSP::Crossover3Way crossover_;
    std::atomic<float> lowMidFreq_{300.0f};
    std::atomic<float> midHighFreq_{3000.0f};
    // ...
};

// In Processor.cpp
tresult Processor::setupProcessing(ProcessSetup& setup) {
    crossover_.prepare(setup.sampleRate);
    return kResultOk;
}

tresult Processor::process(ProcessData& data) {
    // Update from automation (thread-safe)
    crossover_.setLowMidFrequency(lowMidFreq_.load());
    crossover_.setMidHighFrequency(midHighFreq_.load());

    // Process audio
    // ...
}
```

---

## Common Patterns

### Verify Flat Sum

The outputs always sum to the input (within floating-point precision):

```cpp
auto [low, high] = crossover.process(input);
float sum = low + high;
assert(std::abs(sum - input) < 1e-6f);  // Should pass
```

### Stereo Processing

Process left and right channels independently:

```cpp
class StereoCrossover {
    CrossoverLR4 crossoverL_, crossoverR_;

public:
    void prepare(double sampleRate) {
        crossoverL_.prepare(sampleRate);
        crossoverR_.prepare(sampleRate);
    }

    void setCrossoverFrequency(float hz) {
        crossoverL_.setCrossoverFrequency(hz);
        crossoverR_.setCrossoverFrequency(hz);
    }

    void processStereo(float* left, float* right, size_t numSamples,
                       float* lowL, float* lowR,
                       float* highL, float* highR) {
        crossoverL_.processBlock(left, lowL, highL, numSamples);
        crossoverR_.processBlock(right, lowR, highR, numSamples);
    }
};
```

---

## Frequency Response Characteristics

| Parameter | Value |
|-----------|-------|
| Filter order | 4th order (LR4) |
| Slope | 24 dB/octave |
| Level at crossover | -6 dB (both bands) |
| Phase relationship | In-phase (sum flat) |
| Group delay | Frequency-dependent |

---

## Performance Tips

1. **Use processBlock()** for batch processing (more efficient than per-sample)
2. **Use TrackingMode::Efficient** unless you need sub-Hz modulation accuracy
3. **Pre-allocate output buffers** - avoid allocations in audio callback
4. **Call prepare() only once** per sample rate change
