# Research: Formant Filter

**Feature**: 077-formant-filter
**Date**: 2026-01-21

## Overview

This document captures research findings for the FormantFilter implementation, a Layer 2 DSP processor for vocal/formant filtering effects.

---

## R1: Formant Filter Topology

### Decision
Parallel bandpass filters (3 filters for F1, F2, F3)

### Rationale
- Industry standard for formant synthesis (Csound, Max/MSP, synth plugins)
- Each formant is independent and can be individually tuned
- Simple to implement with existing Biquad configured as Bandpass
- Sum of outputs produces formant spectral shape

### Alternatives Considered

| Alternative | Reason Rejected |
|-------------|-----------------|
| Series resonant filters | Harder to control, formants interact unpredictably |
| FIR formant filters | Much higher CPU cost, less modulation-friendly |
| FFT-based formant | Latency, complexity, overkill for 3 formants |

### Topology Diagram

```
                 +-----------------+
                 | Bandpass F1     |
Input -------+-->| (freq, Q=f/bw)  |---+
             |   +-----------------+   |
             |                         |
             |   +-----------------+   |
             +-->| Bandpass F2     |---+--> Sum --> Output
             |   | (freq, Q=f/bw)  |   |
             |   +-----------------+   |
             |                         |
             |   +-----------------+   |
             +-->| Bandpass F3     |---+
                 | (freq, Q=f/bw)  |
                 +-----------------+
```

---

## R2: Q Calculation from Bandwidth

### Decision
Q = frequency / bandwidth (clamped to [0.5, 20.0])

### Rationale
- Standard formula: Q = f0 / BW where BW is the 3dB bandwidth
- Clamping ensures filter stability and useful range
- Q < 0.5 is essentially flat (wasteful)
- Q > 20 risks instability and excessive resonance

### Formula

```cpp
float Q = formantFrequency / bandwidth;
Q = std::clamp(Q, 0.5f, 20.0f);
```

### Example Q Values

| Vowel | Formant | Frequency | Bandwidth | Q |
|-------|---------|-----------|-----------|---|
| A | F1 | 600 Hz | 60 Hz | 10.0 |
| A | F2 | 1040 Hz | 70 Hz | 14.9 |
| A | F3 | 2250 Hz | 110 Hz | 20.0 (clamped) |
| I | F1 | 250 Hz | 60 Hz | 4.2 |
| I | F2 | 1750 Hz | 90 Hz | 19.4 |

---

## R3: Vowel Morphing Implementation

### Decision
Linear interpolation between adjacent vowels using morph position 0-4

### Rationale
- Position 0.0 = A, 1.0 = E, 2.0 = I, 3.0 = O, 4.0 = U
- Intermediate values interpolate between adjacent vowels
- std::lerp (C++20) provides efficient implementation
- Matches user expectation of smooth vowel transitions

### Implementation Pattern

```cpp
int lowerIdx = static_cast<int>(position);
int upperIdx = std::min(lowerIdx + 1, 4);
float fraction = position - static_cast<float>(lowerIdx);

float f1 = std::lerp(kVowelFormants[lowerIdx].f1,
                     kVowelFormants[upperIdx].f1,
                     fraction);
// Repeat for f2, f3, bw1, bw2, bw3
```

### Morph Position Mapping

| Position | Lower Vowel | Upper Vowel | Interpretation |
|----------|-------------|-------------|----------------|
| 0.0 | A | A | Pure A |
| 0.5 | A | E | 50% A, 50% E |
| 1.0 | E | E | Pure E |
| 1.5 | E | I | 50% E, 50% I |
| 2.0 | I | I | Pure I |
| 2.5 | I | O | 50% I, 50% O |
| 3.0 | O | O | Pure O |
| 3.5 | O | U | 50% O, 50% U |
| 4.0 | U | U | Pure U |

---

## R4: Formant Shift Formula

### Decision
Exponential pitch scaling with semitones

### Rationale
- Standard pitch shifting formula: multiplier = pow(2, semitones/12)
- Preserves musical intervals
- +12 semitones = 2x frequency (one octave up)
- -12 semitones = 0.5x frequency (one octave down)

### Formula

```cpp
float shiftMultiplier = std::pow(2.0f, semitones / 12.0f);
float shiftedFreq = baseFreq * shiftMultiplier;
```

### Shift Examples

| Semitones | Multiplier | Effect |
|-----------|------------|--------|
| -24 | 0.25 | Two octaves down |
| -12 | 0.5 | One octave down |
| -7 | 0.67 | Perfect fifth down |
| 0 | 1.0 | No change |
| +7 | 1.5 | Perfect fifth up |
| +12 | 2.0 | One octave up |
| +24 | 4.0 | Two octaves up |

---

## R5: Gender Scaling Formula

### Decision
Exponential scaling with +/-0.25 octave range (per spec clarification Q3)

### Rationale
- Exponential provides perceptually uniform changes
- +/-0.25 octave matches typical male-female formant differences (~19%)
- Simple formula: multiplier = pow(2, gender * 0.25)

### Formula

```cpp
float genderMultiplier = std::pow(2.0f, gender * 0.25f);
// gender = -1.0 -> 0.841 (~-17%)
// gender =  0.0 -> 1.000 (neutral)
// gender = +1.0 -> 1.189 (~+19%)
```

### Gender Values

| Gender | Multiplier | Character |
|--------|------------|-----------|
| -1.0 | 0.841 | Male (formants down ~17%) |
| -0.5 | 0.917 | Slightly male |
| 0.0 | 1.000 | Neutral |
| +0.5 | 1.091 | Slightly female |
| +1.0 | 1.189 | Female (formants up ~19%) |

---

## R6: Parameter Smoothing Strategy

### Decision
6 OnePoleSmoother instances (3 frequencies + 3 bandwidths), default 5ms

### Rationale
- Smoothing frequencies independently allows natural transitions
- Smoothing bandwidths maintains Q stability during formant changes
- 5ms default provides click-free modulation without noticeable lag
- Matches existing pattern in MultimodeFilter and CrossoverFilter

### Implementation Pattern

```cpp
std::array<OnePoleSmoother, 3> freqSmoothers_;  // F1, F2, F3
std::array<OnePoleSmoother, 3> bwSmoothers_;    // BW1, BW2, BW3
```

### Smoothing Time Guidelines

| Use Case | Recommended Time |
|----------|------------------|
| Fast modulation (LFO) | 1-2 ms |
| Standard parameter changes | 5 ms (default) |
| Slow morphing | 10-20 ms |

---

## R7: Frequency Clamping Strategy

### Decision
Clamp after all transformations (morph + shift + gender)

### Rationale
- Final frequency must be in valid range regardless of parameter combination
- Lower bound: 20Hz (below audible, filter instability)
- Upper bound: 0.45 * sampleRate (Nyquist margin for filter stability)
- Same pattern as existing filters (see biquad.h detail::clampFrequency)

### Formula

```cpp
float clampFormantFrequency(float freq, double sampleRate) {
    const float minFreq = 20.0f;
    const float maxFreq = static_cast<float>(sampleRate) * 0.45f;
    return std::clamp(freq, minFreq, maxFreq);
}
```

### Maximum Frequencies by Sample Rate

| Sample Rate | Max Formant Frequency |
|-------------|----------------------|
| 44100 Hz | 19845 Hz |
| 48000 Hz | 21600 Hz |
| 96000 Hz | 43200 Hz |
| 192000 Hz | 86400 Hz |

---

## R8: Existing Formant Data Review

### Decision
Use kVowelFormants from filter_tables.h as-is

### Rationale
- Already exists in codebase (spec 070-filter-foundations)
- Based on Csound bass male voice values (industry standard)
- Frequencies and bandwidths are well-tested values
- No need to introduce alternative formant tables

### Existing Data (from filter_tables.h)

| Vowel | F1 (Hz) | F2 (Hz) | F3 (Hz) | BW1 (Hz) | BW2 (Hz) | BW3 (Hz) |
|-------|---------|---------|---------|----------|----------|----------|
| A | 600 | 1040 | 2250 | 60 | 70 | 110 |
| E | 400 | 1620 | 2400 | 40 | 80 | 100 |
| I | 250 | 1750 | 2600 | 60 | 90 | 100 |
| O | 400 | 750 | 2400 | 40 | 80 | 100 |
| U | 350 | 600 | 2400 | 40 | 80 | 100 |

### Source Reference
Csound Manual, Appendix Table 3 (Bass voice)
Peterson & Barney (1952), Fant (1972)

---

## References

1. **Stanford CCRMA Formant Filtering**
   https://ccrma.stanford.edu/~jos/filters/Formant_Filtering_Example.html

2. **Peterson & Barney (1952)** - Original formant frequency research
   https://asa.scitation.org/doi/10.1121/1.1906875

3. **Csound Manual** - Source of kVowelFormants data
   https://csound.com/docs/manual/index.html

4. **ResearchGate Formant Frequencies**
   https://www.researchgate.net/figure/Formant-Frequencies-Hz-F1-F2-F3-for-Typical-Vowels_tbl1_332054208

5. **filter_tables.h** - Existing formant data in codebase
   `dsp/include/krate/dsp/core/filter_tables.h`
