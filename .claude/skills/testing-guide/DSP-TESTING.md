# DSP Testing Strategies

## Test Signal Types

Use standard test signals to verify DSP behavior:

| Signal | Use Case | How to Generate |
|--------|----------|-----------------|
| **Impulse** | Measure impulse response, latency | Single 1.0f, rest zeros |
| **Step** | DC response, settling time | Zeros then ones |
| **Sine Wave** | Frequency response, THD | `sin(2*pi*f*t/sr)` |
| **White Noise** | Full-spectrum response | Random values [-1, 1] |
| **Sweep** | Time-varying frequency response | Chirp from 20Hz to 20kHz |
| **DC** | DC offset handling | Constant non-zero value |

### Signal Generator Examples

```cpp
inline void generateImpulse(float* buffer, size_t size) {
    std::fill(buffer, buffer + size, 0.0f);
    buffer[0] = 1.0f;
}

inline void generateSine(float* buffer, size_t size,
                         float frequency, float sampleRate) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = std::sin(2.0f * M_PI * frequency * i / sampleRate);
    }
}

inline void generateWhiteNoise(float* buffer, size_t size) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (size_t i = 0; i < size; ++i) {
        buffer[i] = dist(gen);
    }
}
```

---

## Frequency Domain Testing

Use FFT analysis for filters and frequency-dependent effects:

```cpp
TEST_CASE("Lowpass filter frequency response", "[dsp][filter][fft]") {
    BiquadFilter filter;
    filter.setLowpass(1000.0f, 44100.0f, 0.707f);

    // Generate and process impulse
    std::array<float, 4096> impulse{};
    impulse[0] = 1.0f;
    filter.process(impulse.data(), impulse.size());

    // Compute FFT
    auto spectrum = computeFFT(impulse.data(), impulse.size());

    // Check passband (below cutoff - should be ~0dB)
    float passbandGain = magnitudeToDb(spectrum[frequencyToBin(500.0f, 44100.0f, 4096)]);
    REQUIRE(passbandGain == Approx(0.0f).margin(1.0f));

    // Check stopband (above cutoff - should be attenuated)
    float stopbandGain = magnitudeToDb(spectrum[frequencyToBin(4000.0f, 44100.0f, 4096)]);
    REQUIRE(stopbandGain < -20.0f);
}
```

---

## THD (Total Harmonic Distortion) Measurement

Use FFT-based harmonic analysis to measure distortion from saturation and nonlinear processing.

**Why FFT-based THD?** Simple time-domain comparison (subtracting a reference sine) incorrectly measures phase shift and filter delay as distortion. FFT analysis properly isolates harmonic content.

### Implementation

```cpp
float measureTHDWithFFT(const float* buffer, size_t size,
                         float fundamentalFreq, float sampleRate) {
    // FFT size must be power of 2
    size_t fftSize = 1;
    while (fftSize < size && fftSize < kMaxFFTSize) {
        fftSize <<= 1;
    }
    if (fftSize > size) fftSize >>= 1;

    // Apply Hann window to reduce spectral leakage
    std::vector<float> windowed(fftSize);
    constexpr float kTwoPi = 6.28318530718f;
    for (size_t i = 0; i < fftSize; ++i) {
        float window = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i) /
                                                static_cast<float>(fftSize - 1)));
        windowed[i] = buffer[i] * window;
    }

    // Perform FFT
    FFT fft;
    fft.prepare(fftSize);
    std::vector<Complex> spectrum(fftSize / 2 + 1);
    fft.forward(windowed.data(), spectrum.data());

    // Find fundamental bin
    float binWidth = sampleRate / static_cast<float>(fftSize);
    size_t fundamentalBin = static_cast<size_t>(std::round(fundamentalFreq / binWidth));

    // Get fundamental magnitude (search nearby bins for peak)
    float fundamentalMag = 0.0f;
    size_t searchRange = 2;
    for (size_t i = fundamentalBin > searchRange ? fundamentalBin - searchRange : 0;
         i <= fundamentalBin + searchRange && i < spectrum.size(); ++i) {
        float mag = spectrum[i].magnitude();
        if (mag > fundamentalMag) {
            fundamentalMag = mag;
            fundamentalBin = i;
        }
    }

    if (fundamentalMag < 1e-10f) return 0.0f;

    // Sum harmonic magnitudes (2nd through 10th harmonics)
    float harmonicPowerSum = 0.0f;
    for (int harmonic = 2; harmonic <= 10; ++harmonic) {
        size_t harmonicBin = fundamentalBin * harmonic;
        if (harmonicBin >= spectrum.size()) break;

        float harmonicMag = 0.0f;
        for (size_t i = harmonicBin > searchRange ? harmonicBin - searchRange : 0;
             i <= harmonicBin + searchRange && i < spectrum.size(); ++i) {
            float mag = spectrum[i].magnitude();
            if (mag > harmonicMag) harmonicMag = mag;
        }
        harmonicPowerSum += harmonicMag * harmonicMag;
    }

    // THD = sqrt(sum of harmonic powers) / fundamental power * 100%
    return std::sqrt(harmonicPowerSum) / fundamentalMag * 100.0f;
}
```

### Usage Example

```cpp
TEST_CASE("Saturation THD is controllable", "[dsp][saturation]") {
    SaturationProcessor sat;
    sat.prepare(44100.0, 512);

    std::array<float, 4096> buffer;

    auto measureTHDAtDrive = [&](float drive) {
        sat.setInputGain(drive);

        // Let processor settle
        for (int i = 0; i < 10; ++i) {
            generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.5f);
            sat.process(buffer.data(), buffer.size());
        }

        // Measure with fresh signal
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.5f);
        sat.process(buffer.data(), buffer.size());

        return measureTHDWithFFT(buffer.data(), buffer.size(), 1000.0f, 44100.0f);
    };

    float thdLow = measureTHDAtDrive(-10.0f);
    float thdHigh = measureTHDAtDrive(10.0f);

    REQUIRE(thdLow < 0.5f);      // Low drive = low THD
    REQUIRE(thdHigh >= 3.0f);    // High drive = significant THD
    REQUIRE(thdHigh > thdLow);   // THD increases with drive
}
```

### Key Considerations

| Factor | Recommendation |
|--------|----------------|
| **Window function** | Use Hann window to reduce spectral leakage |
| **FFT size** | Use 4096+ samples for good frequency resolution |
| **Test frequency** | Use 1kHz (well away from FFT bin boundaries) |
| **Test amplitude** | Use 0.5 (gives headroom for saturation) |
| **Settling time** | Process 5-10 blocks before measurement |
| **Harmonic count** | Sum harmonics 2-10 (covers audible distortion) |

---

## Frequency Estimation for Pitch Accuracy

When testing pitch shifters or frequency-altering DSP, you need accurate frequency measurement.

### Method 1: FFT-based Detection

Good for pure tones, but fooled by amplitude modulation:

```cpp
float estimateFrequencyFFT(const float* buffer, size_t size, float sampleRate,
                            float expectedFreqMin = 50.0f, float expectedFreqMax = 2000.0f) {
    // Apply Hann window, perform FFT, find peak bin in expected range
    // Use parabolic interpolation for sub-bin accuracy
    // ...
    return (peakBin + delta) * binWidth;
}
```

### Method 2: Autocorrelation-based Detection

More robust for pitch shifters with crossfading artifacts:

```cpp
float estimateFrequencyAutocorr(const float* buffer, size_t size, float sampleRate,
                                 float minFreq = 50.0f, float maxFreq = 2000.0f) {
    size_t minLag = static_cast<size_t>(sampleRate / maxFreq);
    size_t maxLag = static_cast<size_t>(sampleRate / minFreq);
    maxLag = std::min(maxLag, size / 2);

    float bestCorr = -1.0f;
    size_t bestLag = minLag;

    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        float corr = 0.0f;
        for (size_t i = 0; i < size - lag; ++i) {
            corr += buffer[i] * buffer[i + lag];
        }
        if (corr > bestCorr) {
            bestCorr = corr;
            bestLag = lag;
        }
    }

    return sampleRate / static_cast<float>(bestLag);
}
```

### When to Use Which

| Scenario | Recommended Method | Why |
|----------|-------------------|-----|
| Pure sine waves | FFT | Sub-bin accuracy via parabolic interpolation |
| Pitch shifters | Autocorrelation | Immune to AM sidebands from crossfading |
| Filters | FFT | Need frequency response at specific bins |
| Oscillators | Either | Both work well for clean sources |

---

## Latency and Delay Testing

```cpp
TEST_CASE("DelayLine has correct latency", "[dsp][delay]") {
    DelayLine delay(44100);  // 1 second max
    delay.setDelay(100);     // 100 samples

    // Write impulse
    delay.write(1.0f);
    for (int i = 0; i < 99; ++i) {
        delay.write(0.0f);
        REQUIRE(delay.read(100) == 0.0f);  // Not yet arrived
    }

    delay.write(0.0f);  // 100th sample
    REQUIRE(delay.read(100) == 1.0f);  // Impulse arrived
}
```

---

## Real-Time Safety Testing

Verify allocation-free operation in process methods:

```cpp
TEST_CASE("Process method is allocation-free", "[dsp][realtime]") {
    Processor proc;
    proc.prepare(44100.0, 512);  // Allocations allowed here

    std::array<float, 512> buffer{};

    // Override global new to detect allocations
    AllocationDetector detector;
    detector.startTracking();

    proc.processBlock(buffer.data(), 512);

    REQUIRE(detector.getAllocationCount() == 0);
}
```

---

## Deterministic Testing of DSP with Randomness

Many DSP algorithms use RNG for diffusion, stereo width, noise, modulation. Tests can become flaky.

### The Problem

```cpp
// BAD: Flaky test - threshold may pass sometimes, fail others
TEST_CASE("Diffusion produces smooth output", "[dsp][diffusion]") {
    SpectralDelay delay;
    delay.prepare(44100.0, 512);
    delay.setDiffusion(0.8f);  // Uses random phase internally
    // ...
    REQUIRE(clickRatio < 25.0f);  // May fail with unlucky RNG seed!
}
```

### The Solution: Seeded RNG

Add a `seedRng()` method to DSP classes:

```cpp
class SpectralDelay {
public:
    void seedRng(uint32_t seed) noexcept {
        rng_.seed(seed);
        // Reinitialize derived random values
        for (std::size_t i = 0; i < phaseBuffer_.size(); ++i) {
            phaseBuffer_[i] = (rng_.nextFloat() - 0.5f) * kTwoPi;
        }
    }

private:
    Xorshift32 rng_;
};
```

Use in tests:

```cpp
// GOOD: Deterministic test
TEST_CASE("Diffusion produces smooth output", "[dsp][diffusion]") {
    SpectralDelay delay;
    delay.prepare(44100.0, 512);
    delay.seedRng(42);  // Fixed seed for reproducibility
    delay.setDiffusion(0.8f);
    // ...
    REQUIRE(clickRatio < 25.0f);  // Now deterministic!
}
```

### When to Seed vs Not Seed

| Test Purpose | Seed? | Reason |
|--------------|-------|--------|
| Behavior verification | Yes | Results must be consistent |
| Testing randomness itself | No | Would defeat the test |
| Threshold-based tests | Yes | Avoid sensitivity to RNG |
| Decorrelation tests | Usually | Test feature works, not RNG |

### Example: Testing That Randomness Works

```cpp
// DO NOT seed - we're testing that randomization works
TEST_CASE("Diffusion instances produce different outputs", "[dsp][diffusion]") {
    SpectralDelay delay1, delay2;
    delay1.prepare(44100.0, 512);
    delay2.prepare(44100.0, 512);
    // NO seedRng() call

    delay1.setDiffusion(1.0f);
    delay2.setDiffusion(1.0f);

    auto output1 = processThrough(delay1, testInput);
    auto output2 = processThrough(delay2, testInput);

    float correlation = measureCorrelation(output1, output2);
    REQUIRE(correlation < 0.9f);  // Should be different
}
```

---

## Guard Rail Tests

Ensure DSP doesn't produce invalid output:

```cpp
TEST_CASE("DSP output contains no NaN or Inf", "[dsp][safety]") {
    Saturator sat;
    sat.prepare(44100.0, 512);
    sat.setDrive(10.0f);

    std::array<float, 512> buffer;

    SECTION("normal input") {
        generateSine(buffer.data(), 512, 440.0f, 44100.0f);
        sat.process(buffer.data(), 512);

        for (float sample : buffer) {
            REQUIRE_FALSE(std::isnan(sample));
            REQUIRE_FALSE(std::isinf(sample));
        }
    }

    SECTION("extreme input") {
        std::fill(buffer.begin(), buffer.end(), 1000.0f);
        sat.process(buffer.data(), 512);

        for (float sample : buffer) {
            REQUIRE_FALSE(std::isnan(sample));
            REQUIRE(std::abs(sample) <= 2.0f);
        }
    }

    SECTION("denormal input") {
        std::fill(buffer.begin(), buffer.end(), 1e-38f);
        sat.process(buffer.data(), 512);

        for (float sample : buffer) {
            REQUIRE_FALSE(std::isnan(sample));
        }
    }
}
```
