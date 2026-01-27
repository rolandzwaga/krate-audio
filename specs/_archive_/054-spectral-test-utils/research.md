# Research: Spectral Test Utilities

**Date**: 2026-01-13
**Feature**: 054-spectral-test-utils
**Purpose**: Resolve technical questions for aliasing measurement implementation

## Research Tasks

### 1. Aliased Frequency Calculation

**Question**: How do we calculate where a harmonic will alias to?

**Research**: Reviewed DSPRelated, WolfSound, and Elektor sources from spec.

**Decision**: Use the folding formula from the spec:
```
f_aliased = |k * fs - n * f_fundamental|
```
where `k` is chosen so the result falls in `[0, fs/2]`.

**Rationale**: This is the standard DSP formula for aliasing due to sampling. When a harmonic frequency exceeds Nyquist (fs/2), it "folds back" into the representable range.

**Implementation**:
```cpp
float calculateAliasedFrequency(float fundamentalHz, int harmonicNumber, float sampleRate) {
    const float harmonicFreq = fundamentalHz * static_cast<float>(harmonicNumber);
    const float nyquist = sampleRate / 2.0f;

    // No aliasing if below Nyquist
    if (harmonicFreq <= nyquist) {
        return harmonicFreq;
    }

    // Fold back: reflect around Nyquist
    // f_aliased = |k * fs - f_harmonic| where k minimizes the result
    float aliased = std::fmod(harmonicFreq, sampleRate);
    if (aliased > nyquist) {
        aliased = sampleRate - aliased;
    }
    return aliased;
}
```

**Alternatives considered**:
- Using iterative k search: More complex, same result
- Modular arithmetic only: Doesn't handle the reflection correctly

---

### 2. FFT Bin Mapping

**Question**: How do we map a frequency to an FFT bin index?

**Research**: Standard FFT bin mapping formula.

**Decision**: Use `bin = round(freq * fftSize / sampleRate)`

**Rationale**:
- FFT bin `k` represents frequency `k * sampleRate / fftSize`
- Inverse: `k = freq * fftSize / sampleRate`
- Round to nearest bin for best accuracy

**Implementation**:
```cpp
size_t frequencyToBin(float freqHz, float sampleRate, size_t fftSize) {
    const float binFloat = freqHz * static_cast<float>(fftSize) / sampleRate;
    return static_cast<size_t>(std::round(binFloat));
}
```

**Bin resolution**: With fftSize=2048 at 44.1kHz: `44100/2048 = 21.5 Hz` per bin.

---

### 3. Power Measurement in dB

**Question**: How do we convert FFT magnitudes to dB and sum power from multiple bins?

**Research**: Standard audio engineering practice.

**Decision**:
- For single bin: `dB = 20 * log10(magnitude + epsilon)`
- For multiple bins: Sum powers (magnitude squared), then convert

**Rationale**:
- Power is proportional to magnitude squared
- dB = 10 * log10(power) = 20 * log10(magnitude)
- When combining multiple frequency components, sum their powers first

**Implementation**:
```cpp
// Sum power from multiple bins
float totalPower = 0.0f;
for (size_t bin : bins) {
    float mag = spectrum[bin].magnitude();
    totalPower += mag * mag;
}
float rms = std::sqrt(totalPower);
float dB = 20.0f * std::log10(rms + 1e-10f);
```

**Alternatives considered**:
- Average power instead of sum: Would penalize signals with more harmonics
- Peak magnitude only: Ignores energy distribution

---

### 4. Window Function Selection

**Question**: Which window function should we use for aliasing measurement?

**Research**: Reviewed window function trade-offs in DSP literature.

**Decision**: Use Hann window (already available in `window_functions.h`)

**Rationale**:
- Good sidelobe suppression (~-32 dB first sidelobe)
- Moderate main lobe width (4 bins)
- COLA-compliant for overlap-add (not needed here but good practice)
- Already implemented in project

**Implementation**: Use `Window::generateHann()` from existing header.

**Alternatives considered**:
- Blackman: Better sidelobes (-58 dB) but wider main lobe - overkill for aliasing measurement
- Kaiser: Best control but requires beta tuning
- Rectangular (no window): Would cause spectral leakage, false aliasing readings

---

### 5. Test Signal Parameters

**Question**: What test signal parameters should be used for aliasing measurement?

**Research**: Based on spec examples and practical considerations.

**Decision**: Default configuration:
```cpp
struct AliasingTestConfig {
    float testFrequencyHz = 5000.0f;   // High enough to alias with harmonics
    float sampleRate = 44100.0f;       // Standard sample rate
    float driveGain = 4.0f;            // Strong clipping for visible aliasing
    size_t fftSize = 2048;             // Good frequency resolution (~21 Hz)
    size_t numCycles = 20;             // Not currently used, fftSize determines length
    int maxHarmonic = 10;              // Sufficient for hard clip (odd harmonics)
};
```

**Rationale**:
- 5kHz fundamental: At 44.1kHz, harmonics 5+ will alias (5*5kHz = 25kHz > 22.05kHz)
- 4x drive: Strong clipping generates rich harmonic content
- 2048 FFT: 21.5 Hz resolution, good for distinguishing harmonics
- 10 harmonics: Hard clip generates odd harmonics; 10 covers most energy

**Aliasing example at 5kHz/44.1kHz**:
| Harmonic | Frequency | Aliased To |
|----------|-----------|------------|
| 5 | 25,000 Hz | 19,100 Hz |
| 6 | 30,000 Hz | 14,100 Hz |
| 7 | 35,000 Hz | 9,100 Hz |
| 8 | 40,000 Hz | 4,100 Hz |
| 9 | 45,000 Hz | 900 Hz |

---

### 6. Bin Classification Strategy

**Question**: How do we determine which bins are "aliased" vs "intended harmonics"?

**Research**: Analyzed the aliasing behavior of waveshapers.

**Decision**:
- **Intended harmonics**: Bins at `n * fundamental` where `n * fundamental < nyquist`
- **Aliased components**: Bins at `calculateAliasedFrequency(fundamental, n)` where `n * fundamental >= nyquist`
- **Use small tolerance** (+-1 bin) around each target to catch spectral leakage

**Rationale**: Clean separation allows comparing aliasing power between algorithms.

**Implementation**:
```cpp
std::vector<size_t> getHarmonicBins(const AliasingTestConfig& config) {
    std::vector<size_t> bins;
    const float nyquist = config.sampleRate / 2.0f;
    for (int n = 2; n <= config.maxHarmonic; ++n) {
        float freq = config.testFrequencyHz * static_cast<float>(n);
        if (freq < nyquist) {
            bins.push_back(frequencyToBin(freq, config.sampleRate, config.fftSize));
        }
    }
    return bins;
}

std::vector<size_t> getAliasedBins(const AliasingTestConfig& config) {
    std::vector<size_t> bins;
    const float nyquist = config.sampleRate / 2.0f;
    for (int n = 2; n <= config.maxHarmonic; ++n) {
        float freq = config.testFrequencyHz * static_cast<float>(n);
        if (freq >= nyquist) {
            float aliasedFreq = calculateAliasedFrequency(
                config.testFrequencyHz, n, config.sampleRate);
            size_t bin = frequencyToBin(aliasedFreq, config.sampleRate, config.fftSize);
            bins.push_back(bin);
        }
    }
    return bins;
}
```

---

### 7. Handling Overlapping Bins

**Question**: What if an aliased frequency lands on/near an intended harmonic?

**Research**: This is a fundamental limitation of aliasing measurement.

**Decision**: Accept the limitation but document it. Use test frequencies that avoid overlap.

**Rationale**:
- At 5kHz/44.1kHz, aliased 9th harmonic (900 Hz) is close to no intended harmonic
- The default config avoids major overlaps
- If overlap occurs, the measurement may underreport aliasing

**Implementation**: No special handling in v1. The caller should choose test parameters wisely.

---

### 8. Expected dB Reduction Values

**Question**: What aliasing reduction should we expect from ADAA?

**Research**: Reviewed academic ADAA papers and practical measurements.

**Decision**: Based on theoretical and practical expectations:
- First-order ADAA: 12-20 dB reduction (spec requires >= 12 dB)
- Second-order ADAA: 6-12 dB additional reduction over first-order (spec requires >= 6 dB)

**Rationale**:
- ADAA theory predicts ~18 dB/octave improvement for first-order
- Practical measurements show 12-20 dB depending on signal
- Second-order adds another order of improvement

**Test margins**: Use conservative thresholds with margin for measurement noise.

---

## Summary of Decisions

| Topic | Decision |
|-------|----------|
| Alias calculation | Modular arithmetic with Nyquist reflection |
| Bin mapping | Round(freq * fftSize / sampleRate) |
| Power measurement | Sum magnitude squared, then convert to dB |
| Window function | Hann window (existing) |
| Default frequency | 5kHz at 44.1kHz |
| Default FFT size | 2048 |
| Default drive | 4.0 |
| Max harmonics | 10 |
| Bin classification | Separate intended vs aliased based on Nyquist |
| Overlap handling | Accept limitation, document |

## References

1. Spec: specs/054-spectral-test-utils/spec.md
2. Existing FFT: dsp/include/krate/dsp/primitives/fft.h
3. Window functions: dsp/include/krate/dsp/core/window_functions.h
4. ADAA paper: Parker, J. et al. "Reducing Aliasing Using Antiderivative Anti-Aliasing"
