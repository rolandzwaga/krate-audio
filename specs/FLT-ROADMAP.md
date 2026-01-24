# Filter Implementation Roadmap

A step-by-step plan for implementing comprehensive filter support using the existing layered DSP architecture.

## Executive Summary

This roadmap covers implementing all filter types from `DSP-FILTER-TECHNIQUES.md` using the Krate::DSP layered architecture. The plan maximizes reuse of existing components and follows strict layer dependencies.

**Estimated Components:**
- Layer 0 (Core): 0 new files (all exist)
- Layer 1 (Primitives): 1 new file (`hilbert_transform.h`)
- Layer 2 (Processors): 14 new files
- Layer 3 (Systems): 4 new files (spectral_delay, granular, feedback_network exist)

**Roadmap Sections:**
- **Phases 1-11**: Foundation filters (COMPLETE - specs 070-078, 079)
- **Phases 12-18**: Advanced sound design & special FX filters

**Existing Infrastructure (reusable for Phases 12-18):**
| Component | Location | Reuse For |
|-----------|----------|-----------|
| `random.h` | `core/` | Stochastic, S&H filters |
| `window_functions.h` | `core/` | Spectral/granular processing |
| `fft.h`, `stft.h` | `primitives/` | All spectral filters |
| `spectral_buffer.h` | `primitives/` | Spectral morph, gate, tilt |
| `pitch_detector.h` | `primitives/` | Pitch-tracking filter |
| `noise_generator.h` | `processors/` | Karplus-Strong excitation |
| `envelope_follower.h` | `processors/` | Sidechain, transient filters |
| `spectral_delay.h` | `effects/` | **Already implements Phase 18.2** |
| `granular_engine.h` | `systems/` | Granular filter foundation |
| `feedback_network.h` | `systems/` | Filter matrix routing |
| `modulation_matrix.h` | `systems/` | Complex modulation routing |

---

## Current State: What Already Exists

### Layer 0 (Core) - Utilities
| Component | File | Reusable For |
|-----------|------|--------------|
| dB/gain conversion | `db_utils.h` | All filters with gain parameters |
| Math constants (Pi, Sqrt2) | `math_constants.h` | Coefficient calculations |
| Denormal handling | `db_utils.h` | All IIR filters |
| NaN/Inf detection | `db_utils.h` | Stability checks |

### Layer 1 (Primitives) - Building Blocks
| Component | File | Status | Notes |
|-----------|------|--------|-------|
| Biquad (TDF2) | `biquad.h` | Complete | LP, HP, BP, Notch, AP, Peak, Shelves |
| BiquadCascade | `biquad.h` | Complete | 2-8 stages, Butterworth/LR Q |
| SmoothedBiquad | `biquad.h` | Complete | Per-coefficient smoothing |
| DCBlocker (1st order) | `dc_blocker.h` | Complete | 6dB/oct HP at ~10Hz |
| DCBlocker2 (2nd order) | `dc_blocker.h` | Complete | Bessel, faster settling |
| OnePoleSmoother | `smoother.h` | Complete | Parameter smoothing |
| LinearRamp | `smoother.h` | Complete | Constant-rate changes |
| SlewLimiter | `smoother.h` | Complete | Asymmetric rate limiting |
| DelayLine | `delay_line.h` | Complete | Linear + allpass interpolation |
| LFO | `lfo.h` | Complete | Modulation source |
| Oversampler | `oversampler.h` | Complete | 2x/4x, IIR/FIR modes |

### Layer 2 (Processors) - Composed Modules
| Component | File | Status | Notes |
|-----------|------|--------|-------|
| MultimodeFilter | `multimode_filter.h` | Complete | 8 types, 4 slopes, smoothing, drive |
| EnvelopeFollower | `envelope_follower.h` | Complete | Amplitude/RMS/Peak + sidechain HP |
| DiffusionNetwork | `diffusion_network.h` | Complete | Allpass chains for reverb |

---

## Implementation Phases

### Phase 1: Core Coefficient Utilities (Layer 0)

**Goal:** Add missing coefficient calculation utilities needed by new filter types.

#### 1.1 Filter Coefficient Tables (`filter_tables.h`)
```
Location: dsp/include/krate/dsp/core/filter_tables.h
Layer: 0
Dependencies: None
```

**Contains:**
- Formant frequency/bandwidth tables for vowels (a, e, i, o, u)
- Chebyshev Type I/II pole-zero tables (orders 2-8)
- Bessel filter coefficients (orders 2-8)
- Elliptic filter tables (if needed later)

**Formant Data (from research):**
```cpp
struct FormantData {
    float f1, f2, f3;      // Frequencies (Hz)
    float bw1, bw2, bw3;   // Bandwidths (Hz)
};

constexpr FormantData kVowelFormants[] = {
    // Vowel   F1     F2     F3    BW1   BW2   BW3
    /* a */  { 700,  1220,  2600,  130,   70,  160 },  // father
    /* e */  { 530,  1850,  2500,  100,   80,  150 },  // bed
    /* i */  { 270,  2300,  3000,   60,   90,  150 },  // see
    /* o */  { 570,   840,  2400,   80,   80,  140 },  // go
    /* u */  { 300,   870,  2250,   70,  100,  140 },  // boot
};
```

**References:**
- [Stanford CCRMA Formant Filtering](https://ccrma.stanford.edu/~jos/filters/Formant_Filtering_Example.html)
- [ResearchGate Formant Frequencies Table](https://www.researchgate.net/figure/Formant-Frequencies-Hz-F1-F2-F3-for-Typical-Vowels_tbl1_332054208)

---

### Phase 2: SVF Implementation (Layer 1)

**Goal:** Add the Trapezoidal/TPT State Variable Filter for superior modulation behavior.

#### 2.1 State Variable Filter (`svf.h`)
```
Location: dsp/include/krate/dsp/primitives/svf.h
Layer: 1
Dependencies: math_constants.h, db_utils.h
Reuses: None (self-contained topology)
```

**Why SVF over Biquad:**
- Simultaneous LP/HP/BP/Notch outputs from single computation
- Independent cutoff/Q control (no interdependency)
- Excellent modulation stability (designed for it)
- Better low-frequency precision than biquad

**Implementation (Cytomic TPT/Trapezoidal):**
```cpp
class SVF {
public:
    enum class Mode { Lowpass, Highpass, Bandpass, Notch, Allpass, Peak, LowShelf, HighShelf };

    void prepare(double sampleRate);
    void setMode(Mode mode);
    void setCutoff(float hz);
    void setResonance(float q);  // 0.5 to ~20
    void setGain(float dB);      // For shelf/peak modes

    float process(float input);
    void processBlock(float* buffer, int numSamples);

    // Simultaneous outputs (for multi-mode use)
    struct Outputs { float low, high, band, notch; };
    Outputs processMulti(float input);

private:
    // TPT coefficients
    float g_;   // tan(pi * cutoff / sampleRate)
    float k_;   // 1/Q (damping)
    float a1_, a2_, a3_;  // Derived coefficients

    // Integrator states (trapezoidal)
    float ic1eq_ = 0.0f;
    float ic2eq_ = 0.0f;

    // Mode mixing coefficients
    float m0_, m1_, m2_;  // high, band, low mix
};
```

**Key Formulas (from Cytomic papers):**
```cpp
g = tan(pi * cutoff / sampleRate);
k = 1.0 / Q;
a1 = 1.0 / (1.0 + g * (g + k));
a2 = g * a1;
a3 = g * a2;

// Per-sample processing
v3 = input - ic2eq;
v1 = a1 * ic1eq + a2 * v3;
v2 = ic2eq + a2 * ic1eq + a3 * v3;
ic1eq = 2 * v1 - ic1eq;
ic2eq = 2 * v2 - ic2eq;

low = v2;
band = v1;
high = v3 - k * v1 - v2;
```

**Mode Mixing (output = m0*high + m1*band + m2*low):**
| Mode | m0 | m1 | m2 |
|------|----|----|-----|
| Lowpass | 0 | 0 | 1 |
| Highpass | 1 | 0 | 0 |
| Bandpass | 0 | 1 | 0 |
| Notch | 1 | 0 | 1 |
| Allpass | 1 | -k | 1 |
| Peak | 1 | 0 | -1 |
| LowShelf | 1 | k*(A-1) | A² |
| HighShelf | A² | k*(A-1) | 1 |

**References:**
- [Cytomic Technical Papers](https://cytomic.com/technical-papers/)
- [SvfLinearTrapOptimised2.pdf](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf)
- [GitHub C++ Implementation](https://gist.github.com/hollance/2891d89c57adc71d9560bcf0e1e55c4b)

---

### Phase 3: Comb Filters (Layer 1)

**Goal:** Add feedforward and feedback comb filters for flangers, choruses, and reverb components.

#### 3.1 Comb Filters (`comb_filter.h`)
```
Location: dsp/include/krate/dsp/primitives/comb_filter.h
Layer: 1
Dependencies: delay_line.h, dc_blocker.h
Reuses: DelayLine (for delay buffer)
```

**Contains:**

```cpp
// Feedforward comb (FIR) - creates notches
// y[n] = x[n] + g * x[n-D]
class FeedforwardComb {
public:
    void prepare(double sampleRate, float maxDelayMs);
    void setDelay(float delayMs);    // Also accepts samples
    void setGain(float g);           // -1 to +1 (depth of notches)
    float process(float input);
private:
    DelayLine delay_;
    float gain_ = 0.5f;
};

// Feedback comb (IIR) - creates peaks, can resonate
// y[n] = x[n] + g * y[n-D]
class FeedbackComb {
public:
    void prepare(double sampleRate, float maxDelayMs);
    void setDelay(float delayMs);
    void setFeedback(float g);       // |g| < 1 for stability
    void setDamping(float amount);   // Optional LP in feedback
    float process(float input);
private:
    DelayLine delay_;
    float feedback_ = 0.5f;
    float dampingCoeff_ = 0.0f;      // One-pole LP coefficient
    float dampState_ = 0.0f;
};

// Schroeder allpass comb - flat magnitude, phase dispersion
// y[n] = -g*x[n] + x[n-D] + g*y[n-D]
class SchroederAllpass {
public:
    void prepare(double sampleRate, float maxDelayMs);
    void setDelay(float delayMs);
    void setGain(float g);           // Diffusion amount
    float process(float input);
private:
    DelayLine delay_;
    float gain_ = 0.5f;
};
```

**Use Cases:**
- `FeedforwardComb`: Flanger, chorus (with LFO modulation of delay)
- `FeedbackComb`: Karplus-Strong plucked strings, reverb comb banks
- `SchroederAllpass`: Reverb diffusion networks, impulse spreading

**References:**
- [Stanford CCRMA Schroeder Allpass](https://ccrma.stanford.edu/~jos/pasp/Schroeder_Allpass_Sections.html)
- [Valhalla DSP - Reverb Diffusion](https://valhalladsp.com/2011/01/21/reverbs-diffusion-allpass-delays-and-metallic-artifacts/)

---

### Phase 4: First-Order Allpass (Layer 1)

**Goal:** Add first-order allpass for phasers and phase correction.

#### 4.1 First-Order Allpass (`allpass_1pole.h`)
```
Location: dsp/include/krate/dsp/primitives/allpass_1pole.h
Layer: 1
Dependencies: math_constants.h
Reuses: None
```

```cpp
// First-order allpass: y[n] = a*x[n] + x[n-1] - a*y[n-1]
// Phase shift: 0° at DC, -180° at Nyquist, -90° at break frequency
class Allpass1Pole {
public:
    void prepare(double sampleRate);
    void setFrequency(float hz);     // Break frequency (90° phase shift)
    void setCoefficient(float a);    // Direct coefficient (-1 to +1)
    float process(float input);
    void reset();

    // For calculating break frequency from coefficient
    static float frequencyFromCoeff(float a, double sampleRate);
    static float coeffFromFrequency(float hz, double sampleRate);

private:
    float a_ = 0.0f;
    float z1_ = 0.0f;   // x[n-1]
    float y1_ = 0.0f;   // y[n-1]
};
```

**Coefficient Calculation:**
```cpp
// From frequency to coefficient
a = (tan(pi * freq / sampleRate) - 1) / (tan(pi * freq / sampleRate) + 1);

// Or simplified for stability
a = (1 - tan(pi * freq / sampleRate)) / (1 + tan(pi * freq / sampleRate));
```

**Use in Phaser:** Cascade 2-12 stages, modulate break frequencies with LFO.

---

### Phase 5: Ladder Filter (Layer 1)

**Goal:** Implement the classic Moog 24dB/octave resonant lowpass.

#### 5.1 Ladder Filter (`ladder_filter.h`)
```
Location: dsp/include/krate/dsp/primitives/ladder_filter.h
Layer: 1
Dependencies: oversampler.h, math_constants.h, db_utils.h
Reuses: Oversampler (2x minimum for nonlinear version)
```

```cpp
class LadderFilter {
public:
    enum class Model {
        Linear,         // Fast, no saturation (Stilson/Smith)
        Nonlinear       // Classic analog character (Huovilainen)
    };

    void prepare(double sampleRate, int maxBlockSize);
    void setModel(Model model);
    void setCutoff(float hz);
    void setResonance(float k);      // 0-4, self-oscillation ~3.9
    void setDrive(float dB);         // Pre-filter saturation
    void setSlope(int poles);        // 1-4 (6-24 dB/oct)

    float process(float input);
    void processBlock(float* buffer, int numSamples);

private:
    // 4 cascaded one-pole stages
    float stage_[4] = {0};
    float stageTanh_[4] = {0};   // For Huovilainen nonlinear

    // Coefficients
    float g_;        // tan(pi * cutoff / sampleRate)
    float resonance_;
    float drive_;
    int numPoles_ = 4;
    Model model_ = Model::Nonlinear;

    Oversampler<2, 1> oversampler_;

    // Compensation for resonance volume loss
    float resonanceCompensation(float k) const;
};
```

**Huovilainen Model (nonlinear):**
```cpp
// Requires 2x oversampling minimum
float processNonlinear(float input) {
    float feedback = resonance_ * stage_[3];
    float inputWithFeedback = tanh(drive_ * (input - feedback));

    for (int i = 0; i < numPoles_; ++i) {
        float prev = (i == 0) ? inputWithFeedback : stage_[i-1];
        stage_[i] = stage_[i] + g_ * (tanh(prev) - stageTanh_[i]);
        stageTanh_[i] = tanh(stage_[i]);
    }

    return stage_[numPoles_ - 1];
}
```

**References:**
- [Huovilainen DAFX 2004 Paper](https://dafx.de/paper-archive/2004/P_061.PDF)
- [MoogLadders GitHub Collection](https://github.com/ddiakopoulos/MoogLadders)
- [Välimäki Improvements](https://www.researchgate.net/publication/220386519_Oscillator_and_Filter_Algorithms_for_Virtual_Analog_Synthesis)

---

### Phase 6: One-Pole Filter Variants (Layer 1)

**Goal:** Add explicit one-pole lowpass/highpass for smoothing and simple tone control.

#### 6.1 One-Pole Filters (`one_pole.h`)
```
Location: dsp/include/krate/dsp/primitives/one_pole.h
Layer: 1
Dependencies: math_constants.h
Reuses: None (distinct from OnePoleSmoother which is for parameters)
```

```cpp
// One-pole lowpass: y[n] = (1-a)*x[n] + a*y[n-1]
// 6dB/octave slope
class OnePoleLP {
public:
    void prepare(double sampleRate);
    void setCutoff(float hz);
    float process(float input);
    void processBlock(float* buffer, int numSamples);
    void reset();
private:
    float coeff_ = 0.0f;   // exp(-2*pi*cutoff/sampleRate)
    float state_ = 0.0f;
};

// One-pole highpass: y[n] = (1+a)/2 * (x[n] - x[n-1]) + a*y[n-1]
// 6dB/octave slope
class OnePoleHP {
public:
    void prepare(double sampleRate);
    void setCutoff(float hz);
    float process(float input);
    void processBlock(float* buffer, int numSamples);
    void reset();
private:
    float coeff_ = 0.0f;
    float prevInput_ = 0.0f;
    float state_ = 0.0f;
};

// Leaky integrator: y[n] = x[n] + a*y[n-1]
// Used for envelope detection, DC restoration
class LeakyIntegrator {
public:
    void setLeak(float a);   // 0.99-0.9999 typical
    float process(float input);
    void reset();
private:
    float leak_ = 0.995f;
    float state_ = 0.0f;
};
```

---

### Phase 7: Crossover Filter (Layer 2)

**Goal:** Add Linkwitz-Riley crossover for multi-band processing.

#### 7.1 Crossover Filter (`crossover_filter.h`)
```
Location: dsp/include/krate/dsp/processors/crossover_filter.h
Layer: 2
Dependencies: biquad.h, smoother.h
Reuses: BiquadCascade, OnePoleSmoother
```

```cpp
// Linkwitz-Riley crossover (2-way, 3-way, or 4-way)
// LR4 = 24dB/oct, phase coherent, sums to flat
class CrossoverLR4 {
public:
    void prepare(double sampleRate);
    void setCrossoverFrequency(float hz);
    void setSmoothingTime(float ms);

    struct Outputs { float low, high; };
    Outputs process(float input);
    void processBlock(const float* input, float* low, float* high, int numSamples);

private:
    // LR4 = two cascaded Butterworth
    Biquad lpStage1_, lpStage2_;
    Biquad hpStage1_, hpStage2_;
    OnePoleSmoother freqSmoother_;
};

// 3-way crossover (Low/Mid/High)
class Crossover3Way {
public:
    void prepare(double sampleRate);
    void setLowMidFrequency(float hz);
    void setMidHighFrequency(float hz);

    struct Outputs { float low, mid, high; };
    Outputs process(float input);

private:
    CrossoverLR4 lowMidSplit_;
    CrossoverLR4 midHighSplit_;
};

// 4-way crossover (Sub/Low/Mid/High)
class Crossover4Way {
    // ... similar pattern
};
```

**LR4 Implementation:**
```cpp
// Both LP and HP use Q = 0.7071 (Butterworth)
// Cascade two stages for LR4 (24dB/oct)
lpStage1_.configure(FilterType::Lowpass, freq, 0.7071f, 0.0f, sampleRate);
lpStage2_.configure(FilterType::Lowpass, freq, 0.7071f, 0.0f, sampleRate);

hpStage1_.configure(FilterType::Highpass, freq, 0.7071f, 0.0f, sampleRate);
hpStage2_.configure(FilterType::Highpass, freq, 0.7071f, 0.0f, sampleRate);

// Both outputs are -6dB at crossover, sum to 0dB flat
```

---

### Phase 8: Formant Filter (Layer 2)

**Goal:** Add vowel/formant filtering for vocal effects.

#### 8.1 Formant Filter (`formant_filter.h`)
```
Location: dsp/include/krate/dsp/processors/formant_filter.h
Layer: 2
Dependencies: biquad.h, filter_tables.h, smoother.h
Reuses: Biquad (3-5 parallel bandpass), OnePoleSmoother
```

```cpp
enum class Vowel { A, E, I, O, U };

class FormantFilter {
public:
    void prepare(double sampleRate);
    void setVowel(Vowel vowel);
    void setVowelMorph(float position);  // 0-4 morphs between vowels
    void setFormantShift(float semitones); // Shift all formants
    void setGender(float amount);        // -1=male, +1=female (formant scaling)

    float process(float input);
    void processBlock(float* buffer, int numSamples);

private:
    static constexpr int kNumFormants = 3;
    Biquad formants_[kNumFormants];  // Parallel bandpass
    OnePoleSmoother freqSmooth_[kNumFormants];
    OnePoleSmoother bwSmooth_[kNumFormants];

    float calculateFormantFreq(int formant, float vowelPos, float shift, float gender);
    float calculateFormantBW(int formant, float vowelPos);
};
```

**Formant Data (from Phase 1):**
```cpp
// Q = frequency / bandwidth for bandpass
// Parallel sum of bandpass outputs
float output = 0.0f;
for (int i = 0; i < kNumFormants; ++i) {
    output += formants_[i].process(input);
}
return output;
```

---

### Phase 9: Envelope Filter / Auto-Wah (Layer 2)

**Goal:** Combine envelope follower with resonant filter for wah effects.

#### 9.1 Envelope Filter (`envelope_filter.h`)
```
Location: dsp/include/krate/dsp/processors/envelope_filter.h
Layer: 2
Dependencies: envelope_follower.h, svf.h (or multimode_filter.h)
Reuses: EnvelopeFollower, SVF (preferred for modulation stability)
```

```cpp
class EnvelopeFilter {
public:
    enum class Direction { Up, Down };
    enum class FilterMode { Lowpass, Bandpass, Highpass };

    void prepare(double sampleRate);

    // Envelope parameters
    void setSensitivity(float dB);       // Input gain before detection
    void setAttack(float ms);
    void setRelease(float ms);
    void setDirection(Direction dir);

    // Filter parameters
    void setFilterMode(FilterMode mode);
    void setMinFrequency(float hz);      // Sweep range low
    void setMaxFrequency(float hz);      // Sweep range high
    void setResonance(float q);          // 0.5-20
    void setDepth(float amount);         // 0-1, envelope modulation depth
    void setMix(float dryWet);           // 0-1

    float process(float input);
    void processBlock(float* buffer, int numSamples);

private:
    EnvelopeFollower envelope_;
    SVF filter_;
    float minFreq_ = 200.0f;
    float maxFreq_ = 2000.0f;
    float depth_ = 1.0f;
    Direction direction_ = Direction::Up;
};
```

**Classic Wah Ranges:**
- Cry-Baby: 350Hz - 2.2kHz, Q ≈ 8
- Full-range: 100Hz - 8kHz
- Bass wah: 80Hz - 800Hz

---

### Phase 10: Phaser (Layer 2/3)

**Goal:** Cascaded allpass stages with LFO modulation.

#### 10.1 Phaser (`phaser.h`)
```
Location: dsp/include/krate/dsp/processors/phaser.h
Layer: 2
Dependencies: allpass_1pole.h, lfo.h, smoother.h
Reuses: Allpass1Pole, LFO, OnePoleSmoother
```

```cpp
class Phaser {
public:
    void prepare(double sampleRate);

    void setNumStages(int stages);       // 2, 4, 6, 8, 10, or 12
    void setRate(float hz);              // LFO rate
    void setDepth(float amount);         // 0-1, sweep range
    void setFeedback(float amount);      // -1 to +1, resonance
    void setMinFrequency(float hz);      // Sweep range low
    void setMaxFrequency(float hz);      // Sweep range high
    void setCenterFrequency(float hz);   // Alternative: center + depth
    void setStereoSpread(float degrees); // LFO phase offset for stereo
    void setMix(float dryWet);

    float process(float input);
    void processStereo(float& left, float& right);

    // Tempo sync
    void setTempoSync(bool enabled);
    void setNoteValue(NoteValue value);
    void setTempo(float bpm);

private:
    static constexpr int kMaxStages = 12;
    Allpass1Pole stages_[kMaxStages];
    LFO lfo_;
    int numStages_ = 4;
    float feedback_ = 0.0f;
    float feedbackState_ = 0.0f;
};
```

---

### Phase 11: Filter Design Utilities (Layer 0)

**Goal:** Add utilities for advanced filter designs.

#### 11.1 Filter Design (`filter_design.h`)
```
Location: dsp/include/krate/dsp/core/filter_design.h
Layer: 0
Dependencies: math_constants.h
```

```cpp
namespace FilterDesign {

// Butterworth pole angles for N-th order filter
constexpr float butterworthPoleAngle(int k, int N) {
    return kPi * (2*k + N - 1) / (2*N);
}

// Butterworth Q values for cascaded biquads
constexpr float butterworthQ(int stage, int numStages);  // Already exists

// Chebyshev Type I Q values (with ripple)
float chebyshevQ(int stage, int numStages, float rippleDb);

// Bessel Q values (maximally flat delay)
constexpr float besselQ(int stage, int numStages);

// Linkwitz-Riley Q (always 0.5 for each section)
constexpr float linkwitzRileyQ(int stage, int numStages);  // Already exists

// Calculate RT60 for feedback comb
constexpr float combFeedbackForRT60(float delayMs, float rt60Seconds) {
    return std::pow(10.0f, -3.0f * delayMs / (rt60Seconds * 1000.0f));
}

// Prewarp for bilinear transform
constexpr float prewarpFrequency(float freq, double sampleRate) {
    return std::tan(kPi * freq / sampleRate);
}

} // namespace FilterDesign
```

---

### Phase 12: Spectral & FFT-Based Filters (Layer 2/3)

**Goal:** Frequency-domain processing for advanced spectral manipulation beyond what time-domain filters can achieve.

**Existing Infrastructure (no new implementation needed):**
- `primitives/fft.h` - Radix-2 FFT/IFFT (256-8192 sizes)
- `primitives/stft.h` - STFT analysis + OverlapAdd synthesis
- `primitives/spectral_buffer.h` - Magnitude/phase storage
- `core/window_functions.h` - Hann, Hamming, Blackman, Kaiser with COLA verification

#### 12.1 Spectral Morph Filter (`spectral_morph_filter.h`)
```
Location: dsp/include/krate/dsp/processors/spectral_morph_filter.h
Layer: 2
Dependencies: stft.h (EXISTING), spectral_buffer.h (EXISTING)
Status: NEW (uses existing FFT infrastructure)
```

**Description:** Morph between two audio signals by interpolating their magnitude spectra while preserving phase from one source.

```cpp
class SpectralMorphFilter {
public:
    void prepare(double sampleRate, int fftSize = 2048);

    void setMorphAmount(float amount);     // 0 = source A, 1 = source B
    void setPreservePhaseFrom(Source src); // A, B, or Blend
    void setSpectralShift(float semitones); // Pitch shift via bin rotation
    void setSpectralTilt(float dB);        // Tilt spectral balance

    // Process stereo pair or two mono signals for morphing
    void process(const float* inputA, const float* inputB,
                 float* output, int numSamples);

    // Single input mode - morph with internal spectral snapshot
    void captureSnapshot();               // Freeze current spectrum
    float process(float input);           // Morph with snapshot

private:
    int fftSize_;
    std::vector<float> windowFunction_;
    std::vector<std::complex<float>> spectrumA_, spectrumB_;
    // COLA overlap-add for continuous processing
    std::vector<float> overlapBuffer_;
};
```

**Use Cases:**
- Vocal-to-synth morphing
- Evolving pad textures
- Spectral freeze effects
- Cross-synthesis between instruments

#### 12.2 Spectral Gate (`spectral_gate.h`)
```
Location: dsp/include/krate/dsp/processors/spectral_gate.h
Layer: 2
Dependencies: fft.h, envelope_follower.h
```

**Description:** Per-bin noise gate that only passes frequency components above a threshold, creating spectral "holes" in the sound.

```cpp
class SpectralGate {
public:
    void prepare(double sampleRate, int fftSize = 1024);

    void setThreshold(float dB);           // Gate threshold
    void setAttack(float ms);              // Per-bin attack
    void setRelease(float ms);             // Per-bin release
    void setRatio(float ratio);            // Expansion ratio below threshold
    void setFrequencyRange(float lowHz, float highHz); // Affected range
    void setSmearing(float amount);        // Spectral smoothing (0-1)

    float process(float input);
    void processBlock(float* buffer, int numSamples);

private:
    std::vector<float> binEnvelopes_;     // Per-bin envelope states
    std::vector<float> binGains_;         // Per-bin gain reduction
};
```

**Use Cases:**
- Extreme noise reduction
- Spectral "skeletonization" effects
- Creating sparse, pointillist textures
- Isolating tonal from noise components

#### 12.3 Spectral Tilt Filter (`spectral_tilt.h`)
```
Location: dsp/include/krate/dsp/processors/spectral_tilt.h
Layer: 2
Dependencies: fft.h (or IIR approximation)
```

**Description:** Apply a linear dB/octave tilt across the entire spectrum - simpler than EQ but powerful for tonal shaping.

```cpp
class SpectralTilt {
public:
    void prepare(double sampleRate);

    void setTilt(float dBPerOctave);      // -12 to +12 typical
    void setPivotFrequency(float hz);      // Frequency with 0dB change
    void setSmoothing(float ms);           // Transition smoothing

    float process(float input);
    void processBlock(float* buffer, int numSamples);

private:
    // Can be implemented as FFT or approximated with shelving EQ cascade
    // IIR approximation is more efficient for real-time
    Biquad lowShelf_, highShelf_;
};
```

---

### Phase 13: Physical Modeling Resonators (Layer 2)

**Goal:** Resonant structures that model physical vibrating systems for natural, musical filtering.

**Existing Infrastructure:**
- `processors/noise_generator.h` - 13 noise types (white, pink, brown, etc.) for excitation
- `primitives/delay_line.h` - With linear + allpass interpolation
- `primitives/one_pole.h`, `allpass_1pole.h` - For damping and dispersion
- `primitives/biquad.h` - For resonant bandpass filters

#### 13.1 Resonator Bank (`resonator_bank.h`)
```
Location: dsp/include/krate/dsp/processors/resonator_bank.h
Layer: 2
Dependencies: biquad.h, smoother.h
```

**Description:** Bank of tuned resonant filters that can model marimba bars, bells, strings, or arbitrary tunings.

```cpp
class ResonatorBank {
public:
    static constexpr int kMaxResonators = 16;

    void prepare(double sampleRate);

    // Tuning modes
    void setHarmonicSeries(float fundamentalHz, int numPartials);
    void setInharmonicSeries(float baseHz, float inharmonicity);
    void setCustomFrequencies(const float* frequencies, int count);

    // Per-resonator control
    void setFrequency(int index, float hz);
    void setDecay(int index, float seconds);  // RT60
    void setGain(int index, float dB);
    void setQ(int index, float q);            // Alternative to decay

    // Global
    void setDamping(float amount);            // Reduce all decays
    void setExciterMix(float amount);         // Blend dry input
    void setSpectralTilt(float dB);           // High-freq rolloff

    float process(float input);
    void processBlock(float* buffer, int numSamples);

    // Trigger for percussive use (optional impulse excitation)
    void trigger(float velocity = 1.0f);

private:
    struct Resonator {
        Biquad filter;                        // Bandpass at resonant freq
        float decay;
        float gain;
    };
    std::array<Resonator, kMaxResonators> resonators_;
    int activeCount_ = 8;
};
```

**Preset Tunings:**
```cpp
// Marimba: f, 4f, 10f, 20f (approximately)
// Bell: f, 2.2f, 3.4f, 4.9f, 6.3f (inharmonic)
// Guitar string: f, 2f, 3f... (harmonic with slight inharmonicity)
```

#### 13.2 Karplus-Strong String (`karplus_strong.h`)
```
Location: dsp/include/krate/dsp/processors/karplus_strong.h
Layer: 2
Dependencies: delay_line.h, one_pole.h, noise_generator.h (new)
```

**Description:** Classic plucked string synthesis using filtered delay line feedback.

```cpp
class KarplusStrong {
public:
    void prepare(double sampleRate, float maxFrequency = 20.0f);

    void setFrequency(float hz);              // Pitch (determines delay)
    void setDecay(float seconds);             // String decay time
    void setDamping(float amount);            // High-freq loss (0-1)
    void setBrightness(float amount);         // Excitation spectrum
    void setPickPosition(float position);     // 0-1, affects harmonics
    void setStretch(float amount);            // Inharmonicity (piano-like)

    // Excitation
    void pluck(float velocity = 1.0f);        // Noise burst
    void bow(float pressure);                 // Continuous excitation
    void excite(const float* signal, int len); // Custom excitation

    float process(float input = 0.0f);        // External excitation input

private:
    DelayLine delay_;
    OnePoleLP dampingFilter_;
    float feedback_;
    bool isExcited_ = false;
};
```

**Extensions:**
- Two delay lines for bowed strings
- Allpass in loop for inharmonicity
- Nonlinear elements for distortion

#### 13.3 Waveguide Resonator (`waveguide_resonator.h`)
```
Location: dsp/include/krate/dsp/processors/waveguide_resonator.h
Layer: 2
Dependencies: delay_line.h, allpass_1pole.h, dc_blocker.h
```

**Description:** Digital waveguide implementing bidirectional wave propagation for flute/pipe-like resonances.

```cpp
class WaveguideResonator {
public:
    void prepare(double sampleRate);

    void setLength(float hz);                 // Resonant frequency
    void setEndReflection(float left, float right); // -1 to +1
    void setLoss(float amount);               // Per-round-trip loss
    void setDispersion(float amount);         // Frequency-dependent delay
    void setExcitationPoint(float position);  // 0-1 along waveguide

    float process(float input);

private:
    DelayLine rightGoing_, leftGoing_;
    Allpass1Pole dispersionAP_;
    float reflectL_, reflectR_;
};
```

#### 13.4 Modal Resonator (`modal_resonator.h`)
```
Location: dsp/include/krate/dsp/processors/modal_resonator.h
Layer: 2
Dependencies: biquad.h, smoother.h
```

**Description:** Models vibrating bodies as sum of decaying sinusoidal modes. More physically accurate than resonator bank for complex bodies.

```cpp
class ModalResonator {
public:
    static constexpr int kMaxModes = 32;

    void prepare(double sampleRate);

    // Load modal data (from analysis or synthesis)
    void setModes(const ModalData* modes, int count);
    void setModeFrequency(int index, float hz);
    void setModeDecay(int index, float t60Seconds);
    void setModeAmplitude(int index, float amplitude);

    // Material modeling
    void setMaterial(Material mat);           // Preset: Wood, Metal, Glass, etc.
    void setSize(float scale);                // Frequency scaling
    void setDamping(float amount);            // Global decay modifier

    float process(float input);
    void processBlock(float* buffer, int numSamples);

    // Excite all modes simultaneously
    void strike(float velocity);

private:
    struct Mode {
        float freq;
        float decay;      // Exponential decay rate
        float amplitude;
        float phase;
        // State for modal oscillator
        float cosW, sinW; // Precomputed
        float y1, y2;     // State
    };
    std::array<Mode, kMaxModes> modes_;
};
```

---

### Phase 14: Chaos & Randomization Filters (Layer 2)

**Goal:** Unpredictable, evolving filter behaviors for experimental sound design.

**Existing Infrastructure:**
- `core/random.h` - `Xorshift32` PRNG (real-time safe, deterministic seeding)
- `primitives/svf.h` - TPT filter with excellent modulation stability
- `primitives/lfo.h` - Multiple waveforms for S&H clock

#### 14.1 Stochastic Filter (`stochastic_filter.h`)
```
Location: dsp/include/krate/dsp/processors/stochastic_filter.h
Layer: 2
Dependencies: svf.h (or multimode_filter.h), random_generator.h (new)
```

**Description:** Filter with randomly varying parameters - cutoff, Q, or type can drift or jump stochastically.

```cpp
class StochasticFilter {
public:
    enum class RandomMode {
        Walk,           // Brownian motion (smooth drift)
        Jump,           // Discrete random jumps
        Lorenz,         // Chaotic attractor
        Perlin          // Smooth coherent noise
    };

    void prepare(double sampleRate);

    // What to randomize
    void setRandomizeCutoff(bool enabled, float range); // Range in octaves
    void setRandomizeQ(bool enabled, float range);
    void setRandomizeType(bool enabled);      // Random filter type switching

    // How to randomize
    void setRandomMode(RandomMode mode);
    void setChangeRate(float hz);             // Rate of parameter changes
    void setSmoothing(float ms);              // Transition smoothing
    void setSeed(uint32_t seed);              // For reproducibility

    // Base values (center of random range)
    void setCutoff(float hz);
    void setResonance(float q);
    void setFilterType(FilterType type);

    float process(float input);

private:
    SVF filter_;
    RandomGenerator rng_;
    // Chaos state (for Lorenz mode)
    float x_, y_, z_;
};
```

**Lorenz Attractor:**
```cpp
// Classic chaotic system
dx/dt = σ(y - x)
dy/dt = x(ρ - z) - y
dz/dt = xy - βz
// Where σ=10, ρ=28, β=8/3
```

#### 14.2 Self-Oscillating Feedback (`self_osc_filter.h`)
```
Location: dsp/include/krate/dsp/processors/self_osc_filter.h
Layer: 2
Dependencies: ladder_filter.h (or svf.h), dc_blocker.h
```

**Description:** Push resonant filters into self-oscillation for sine-wave generation that can be played melodically.

```cpp
class SelfOscillatingFilter {
public:
    void prepare(double sampleRate);

    void setFrequency(float hz);              // Oscillation pitch
    void setResonance(float amount);          // 0-1, >0.95 for oscillation
    void setOscillationLevel(float dB);       // Output level limiter
    void setExternalInput(float mix);         // Blend external signal
    void setWaveShape(float amount);          // Soft clip shaping

    // Pitch control (for melodic use)
    void setGlide(float ms);
    void noteOn(int midiNote, float velocity);
    void noteOff();

    float process(float input = 0.0f);

private:
    LadderFilter filter_;
    DCBlocker2 dcBlock_;
    float targetFreq_, currentFreq_;
    float glideRate_;
};
```

#### 14.3 Sample & Hold Filter (`sample_hold_filter.h`)
```
Location: dsp/include/krate/dsp/processors/sample_hold_filter.h
Layer: 2
Dependencies: svf.h, random_generator.h, lfo.h
```

**Description:** Filter parameters are sampled and held at regular intervals, creating stepped modulation.

```cpp
class SampleHoldFilter {
public:
    void prepare(double sampleRate);

    // S&H timing
    void setHoldTime(float ms);               // Time between samples
    void setTriggerSource(TriggerSource src); // Clock, Audio, Random
    void setRandomTriggerProbability(float p); // For Random mode

    // What to sample
    void setSampleCutoff(bool enabled, float range);
    void setSampleQ(bool enabled, float range);
    void setSamplePan(bool enabled);          // For stereo

    // Sample source
    void setSourceLFO(float rate);            // Internal LFO
    void setSourceRandom(float range);        // Random values
    void setSourceEnvelope();                 // Follow input amplitude
    void setSourceExternal(float value);      // External CV

    void setSlew(float ms);                   // Smoothing between steps

    float process(float input);
    void processStereo(float& left, float& right);

private:
    SVF filter_;
    LFO lfo_;
    float holdCounter_ = 0.0f;
    float heldCutoff_, heldQ_;
};
```

---

### Phase 15: Sidechain & Reactive Filters (Layer 2/3)

**Goal:** Filters that respond to external signals or audio analysis.

**Existing Infrastructure:**
- `processors/envelope_follower.h` - Amplitude/RMS/Peak detection with sidechain HP
- `primitives/pitch_detector.h` - Autocorrelation-based (50-1000Hz range, ~6ms latency)
- `primitives/svf.h` - For cutoff modulation
- `primitives/delay_line.h` - For lookahead

#### 15.1 Sidechain Filter (`sidechain_filter.h`)
```
Location: dsp/include/krate/dsp/processors/sidechain_filter.h
Layer: 2
Dependencies: envelope_follower.h, svf.h (or multimode_filter.h)
```

**Description:** Filter cutoff controlled by a sidechain signal's envelope - classic ducking and pumping effects.

```cpp
class SidechainFilter {
public:
    void prepare(double sampleRate);

    // Sidechain detection
    void setSidechainInput(bool external);    // External vs. self-sidechain
    void setAttack(float ms);
    void setRelease(float ms);
    void setThreshold(float dB);
    void setSensitivity(float amount);

    // Filter response
    void setDirection(Direction dir);         // Up or Down
    void setMinCutoff(float hz);
    void setMaxCutoff(float hz);
    void setResonance(float q);
    void setFilterType(FilterType type);

    // Timing
    void setLookahead(float ms);              // Anticipate transients
    void setHold(float ms);                   // Hold before release

    float process(float input, float sidechain);
    float process(float input);               // Self-sidechain mode

private:
    EnvelopeFollower envelope_;
    SVF filter_;
    DelayLine lookahead_;
};
```

#### 15.2 Transient-Aware Filter (`transient_filter.h`)
```
Location: dsp/include/krate/dsp/processors/transient_filter.h
Layer: 2
Dependencies: envelope_follower.h, svf.h
```

**Description:** Detects transients and momentarily opens/closes filter for dynamic tonal shaping.

```cpp
class TransientAwareFilter {
public:
    void prepare(double sampleRate);

    // Transient detection
    void setTransientSensitivity(float amount);
    void setTransientAttack(float ms);        // Detection speed
    void setTransientDecay(float ms);         // Return to normal

    // Filter behavior
    void setIdleCutoff(float hz);             // Cutoff when no transient
    void setTransientCutoff(float hz);        // Cutoff during transient
    void setResonance(float q);
    void setTransientQBoost(float amount);    // Extra Q during transient

    float process(float input);

private:
    EnvelopeFollower fastEnv_, slowEnv_;      // For transient detection
    SVF filter_;
};
```

#### 15.3 Pitch-Tracking Filter (`pitch_tracking_filter.h`)
```
Location: dsp/include/krate/dsp/processors/pitch_tracking_filter.h
Layer: 2
Dependencies: pitch_detector.h (new), svf.h, smoother.h
```

**Description:** Filter cutoff follows the detected pitch of the input - creates harmonic filtering.

```cpp
class PitchTrackingFilter {
public:
    void prepare(double sampleRate, int maxBlockSize);

    // Pitch detection
    void setDetectionRange(float minHz, float maxHz);
    void setConfidenceThreshold(float threshold); // Ignore uncertain pitches
    void setTrackingSpeed(float ms);          // Smoothing

    // Filter relationship to pitch
    void setHarmonicRatio(float ratio);       // cutoff = pitch * ratio
    void setOffset(float semitones);          // Additional offset
    void setResonance(float q);
    void setFilterType(FilterType type);

    // Behavior when pitch uncertain
    void setFallbackCutoff(float hz);
    void setFallbackSmoothing(float ms);

    float process(float input);
    void processBlock(float* buffer, int numSamples);

private:
    PitchDetector pitchDetector_;
    SVF filter_;
    OnePoleSmoother cutoffSmoother_;
    float lastValidPitch_ = 440.0f;
};
```

---

### Phase 16: Exotic Modulation Filters (Layer 2/3)

**Goal:** Unusual modulation sources and routing for experimental effects.

#### 16.1 Audio-Rate Filter FM (`audio_rate_filter_fm.h`)
```
Location: dsp/include/krate/dsp/processors/audio_rate_filter_fm.h
Layer: 2
Dependencies: svf.h, oversampler.h
```

**Description:** Modulate filter cutoff at audio rates for metallic, bell-like, or aggressive tones.

```cpp
class AudioRateFilterFM {
public:
    void prepare(double sampleRate, int maxBlockSize);

    // Carrier filter
    void setCarrierCutoff(float hz);          // Center frequency
    void setCarrierQ(float q);
    void setFilterType(FilterType type);

    // Modulator
    void setModulatorSource(ModSource src);   // Internal, External, Self
    void setModulatorFrequency(float hz);     // Internal oscillator freq
    void setModulatorWaveform(Waveform wave); // Sine, Saw, Square, etc.

    // FM depth
    void setFMDepth(float octaves);           // Modulation range
    void setFMIndex(float index);             // Alternative: FM synthesis style

    // Requires oversampling for stability
    void setOversamplingFactor(int factor);   // 2x or 4x recommended

    float process(float input, float modulator = 0.0f);

private:
    SVF filter_;
    Oversampler<4, 1> oversampler_;
    LFO internalModulator_;
    float fmDepth_;
};
```

**Warning:** Audio-rate modulation can cause aliasing - oversample or use carefully!

#### 16.2 Filter Feedback Matrix (`filter_matrix.h`)
```
Location: dsp/include/krate/dsp/systems/filter_matrix.h
Layer: 3
Dependencies: svf.h, delay_line.h
```

**Description:** Multiple filters with configurable feedback routing between them - creates complex resonant networks.

```cpp
class FilterFeedbackMatrix {
public:
    static constexpr int kMaxFilters = 4;

    void prepare(double sampleRate);

    // Per-filter settings
    void setFilterCutoff(int index, float hz);
    void setFilterQ(int index, float q);
    void setFilterType(int index, FilterType type);

    // Matrix routing: feedbackMatrix[from][to] = amount
    void setFeedbackAmount(int from, int to, float amount);
    void setFeedbackDelay(int from, int to, float ms);
    void setFeedbackMatrix(const float matrix[kMaxFilters][kMaxFilters]);

    // Global
    void setInputRouting(const float* gains);  // Input to each filter
    void setOutputMix(const float* gains);     // Output from each filter
    void setGlobalFeedback(float amount);      // Scale all feedback

    float process(float input);
    void processStereo(float& left, float& right);

private:
    SVF filters_[kMaxFilters];
    DelayLine delays_[kMaxFilters][kMaxFilters];
    float matrix_[kMaxFilters][kMaxFilters];
};
```

#### 16.3 Frequency Shifter (`frequency_shifter.h`)
```
Location: dsp/include/krate/dsp/processors/frequency_shifter.h
Layer: 2
Dependencies: hilbert_transform.h (new), lfo.h
```

**Description:** Shifts all frequencies by a constant Hz amount (not pitch shifting!) - creates inharmonic, metallic effects.

```cpp
class FrequencyShifter {
public:
    void prepare(double sampleRate);

    void setShiftAmount(float hz);            // -1000 to +1000 typical
    void setFeedback(float amount);           // Creates spiraling effects
    void setMix(float dryWet);
    void setDirection(Direction dir);         // Up, Down, or Both (ring mod)

    // LFO modulation of shift
    void setModRate(float hz);
    void setModDepth(float hz);               // Shift range

    float process(float input);
    void processStereo(float& left, float& right); // Opposite shifts

private:
    // Hilbert transform for analytic signal
    Biquad hilbertAP_[8];                     // Allpass approximation
    float quadOscPhase_ = 0.0f;               // Quadrature oscillator
    float shiftFreq_;
    LFO modLFO_;
};
```

**Hilbert Transform:** Creates 90° phase shift across spectrum, enabling single-sideband modulation.

---

### Phase 17: Sequenced & Patterned Filters (Layer 2/3)

**Goal:** Rhythmic, pattern-based filter movements synchronized to tempo.

#### 17.1 Filter Step Sequencer (`filter_sequencer.h`)
```
Location: dsp/include/krate/dsp/systems/filter_sequencer.h
Layer: 3
Dependencies: svf.h, smoother.h
```

**Description:** Step sequencer controlling filter parameters - cutoff, Q, type per step.

```cpp
class FilterStepSequencer {
public:
    static constexpr int kMaxSteps = 16;

    void prepare(double sampleRate);

    // Sequence setup
    void setNumSteps(int steps);
    void setStepCutoff(int step, float hz);
    void setStepQ(int step, float q);
    void setStepType(int step, FilterType type);
    void setStepGain(int step, float dB);     // Per-step volume

    // Timing
    void setTempo(float bpm);
    void setStepDivision(NoteValue division); // 1/4, 1/8, 1/16, etc.
    void setSwing(float amount);              // Shuffle timing
    void setGlide(float ms);                  // Transition smoothing

    // Playback
    void setDirection(Direction dir);         // Forward, Backward, PingPong, Random
    void setGateLength(float percent);        // 0-100% of step

    // Sync
    void sync(double ppqPosition);
    void trigger();                           // Manual step advance

    float process(float input);

private:
    struct Step {
        float cutoff, q, gain;
        FilterType type;
    };
    std::array<Step, kMaxSteps> steps_;
    SVF filter_;
    int currentStep_ = 0;
    float stepProgress_ = 0.0f;
};
```

#### 17.2 Vowel Sequencer (`vowel_sequencer.h`)
```
Location: dsp/include/krate/dsp/systems/vowel_sequencer.h
Layer: 3
Dependencies: formant_filter.h, smoother.h
```

**Description:** Sequence through vowel sounds rhythmically - "talking" filter effect.

```cpp
class VowelSequencer {
public:
    static constexpr int kMaxSteps = 8;

    void prepare(double sampleRate);

    // Sequence setup
    void setNumSteps(int steps);
    void setStepVowel(int step, Vowel vowel);
    void setStepFormantShift(int step, float semitones);
    void setPattern(const Vowel* pattern, int length); // Bulk set

    // Timing
    void setTempo(float bpm);
    void setStepDivision(NoteValue division);
    void setMorphTime(float ms);              // Transition between vowels

    // Presets
    void setTalkingPreset(const char* word);  // "aeiou", "wow", etc.

    float process(float input);

private:
    FormantFilter formant_;
    std::array<Vowel, kMaxSteps> pattern_;
    int currentStep_ = 0;
};
```

#### 17.3 Multi-Stage Envelope Filter (`multistage_env_filter.h`)
```
Location: dsp/include/krate/dsp/processors/multistage_env_filter.h
Layer: 2
Dependencies: svf.h, smoother.h
```

**Description:** Complex envelope shapes (not just ADSR) driving filter movement - for evolving pads and textures.

```cpp
class MultiStageEnvelopeFilter {
public:
    static constexpr int kMaxStages = 8;

    void prepare(double sampleRate);

    // Envelope shape
    void setNumStages(int stages);
    void setStageTarget(int stage, float cutoffHz);
    void setStageTime(int stage, float ms);
    void setStageCurve(int stage, float curve); // -1 to +1 (log/lin/exp)
    void setLoop(bool enabled);
    void setLoopStart(int stage);
    void setLoopEnd(int stage);

    // Filter settings
    void setResonance(float q);
    void setFilterType(FilterType type);

    // Trigger
    void trigger();
    void release();                           // Jump to release stage
    void setVelocitySensitivity(float amount);

    float process(float input);

private:
    struct Stage {
        float targetCutoff;
        float timeMs;
        float curve;
    };
    std::array<Stage, kMaxStages> stages_;
    SVF filter_;
    float envelopeValue_ = 0.0f;
    int currentStage_ = 0;
};
```

---

### Phase 18: Granular & Time-Domain Filters (Layer 3)

**Goal:** Granular and time-based spectral processing for otherworldly textures.

**Existing Infrastructure (substantial reuse possible):**
- `systems/granular_engine.h` - Complete granular synthesis engine
- `effects/spectral_delay.h` - **Already implements per-bin spectral delay with freeze**
- `primitives/comb_filter.h` - FeedforwardComb, FeedbackComb, SchroederAllpass
- `primitives/lfo.h` - For modulating comb delays

#### 18.1 Granular Filter (`granular_filter.h`)
```
Location: dsp/include/krate/dsp/systems/granular_filter.h
Layer: 3
Dependencies: granular_engine.h (EXISTING), svf.h
Status: NEW (extends existing granular infrastructure)
```

**Description:** Extends existing `GranularEngine` with per-grain filtering. The base granular synthesis (grain pool, scheduler, processor) already exists.

**Existing Infrastructure:**
- `systems/granular_engine.h` - Core engine with grain pool, scheduler
- `processors/grain_processor.h`, `grain_scheduler.h` - Grain lifecycle
- `primitives/grain_pool.h` - Grain allocation
- `core/grain_envelope.h` - Window functions for grains

**New Addition:** Per-grain SVF filter instance with randomized cutoff.

```cpp
class GranularFilter {
public:
    void prepare(double sampleRate, float maxGrainMs = 100.0f);

    // Grain parameters
    void setGrainSize(float ms);              // 10-100ms typical
    void setGrainDensity(float grainsPerSec); // Overlap amount
    void setGrainShape(WindowType window);    // Hann, Gaussian, etc.
    void setGrainPitch(float semitones);      // Per-grain pitch shift
    void setGrainPitchRandom(float range);    // Random pitch variation

    // Filter per grain
    void setFilterCutoff(float hz);
    void setFilterCutoffRandom(float octaves); // Per-grain randomization
    void setFilterQ(float q);
    void setFilterType(FilterType type);

    // Grain playback
    void setPlaybackSpeed(float speed);       // Time stretch
    void setPlaybackPosition(float position); // Scrub position (0-1)
    void setPlaybackRandom(float amount);     // Position jitter
    void setReverse(float probability);       // Chance of reverse grains

    float process(float input);

private:
    struct Grain {
        int startSample;
        int length;
        float pitch;
        float filterCutoff;
        float phase;
        bool reverse;
        SVF filter;
    };
    std::vector<Grain> activeGrains_;
    DelayLine buffer_;
};
```

#### 18.2 Spectral Delay Filter - **ALREADY EXISTS**
```
Location: dsp/include/krate/dsp/effects/spectral_delay.h
Layer: 4 (User Feature)
Dependencies: stft.h, spectral_buffer.h, delay_line.h
Status: COMPLETE (spec 033-spectral-delay)
```

**Already Implemented Features:**
- Per-bin delay lines with linear interpolation
- Spread control (LowToHigh, HighToLow, CenterOut)
- Linear and logarithmic spread curves
- Feedback with frequency-dependent tilt
- Spectral freeze with crossfade
- Diffusion blur
- Stereo decorrelation
- Tempo sync

*No new implementation needed - see `effects/spectral_delay.h`*

**Existing API (reference):**
```cpp
// Already in effects/spectral_delay.h
class SpectralDelay {
    void prepare(double sampleRate, std::size_t maxBlockSize);
    void setBaseDelayMs(float ms);            // 0-2000ms
    void setSpreadMs(float ms);               // Per-bin spread
    void setSpreadDirection(SpreadDirection); // LowToHigh, HighToLow, CenterOut
    void setSpreadCurve(SpreadCurve);         // Linear or Logarithmic
    void setFeedback(float amount);           // 0-120%
    void setFeedbackTilt(float tilt);         // -1 to +1
    void setDiffusion(float amount);          // Spectral blur
    void setFreezeEnabled(bool enabled);
    void setStereoWidth(float amount);
    void setTimeMode(int mode);               // Free/Synced
    void setNoteValue(int index);             // Tempo sync
    void process(float* left, float* right, std::size_t numSamples, const BlockContext& ctx);
};
```

#### 18.3 Time-Varying Comb Bank (`timevar_comb_bank.h`)
```
Location: dsp/include/krate/dsp/systems/timevar_comb_bank.h
Layer: 3
Dependencies: comb_filter.h, lfo.h, smoother.h
```

**Description:** Bank of comb filters with independently modulated delay times - creates evolving metallic/resonant textures.

```cpp
class TimeVaryingCombBank {
public:
    static constexpr int kMaxCombs = 8;

    void prepare(double sampleRate, float maxDelayMs = 50.0f);

    // Comb configuration
    void setNumCombs(int count);
    void setCombDelay(int index, float ms);
    void setCombFeedback(int index, float amount);
    void setCombDamping(int index, float amount);
    void setCombGain(int index, float dB);

    // Global settings
    void setTuning(Tuning tuning);            // Harmonic, Inharmonic, Custom
    void setFundamental(float hz);            // Base frequency
    void setSpread(float amount);             // Detune between combs

    // Time variation
    void setModRate(float hz);                // Global LFO rate
    void setModDepth(float percent);          // Delay modulation depth
    void setModPhaseSpread(float degrees);    // LFO phase between combs
    void setRandomModulation(float amount);   // Per-comb random drift

    // Stereo
    void setStereoSpread(float amount);       // Pan distribution

    float process(float input);
    void processStereo(float& left, float& right);

private:
    struct CombChannel {
        FeedbackComb comb;
        LFO modLfo;
        float baseDelay;
        float pan;
    };
    std::array<CombChannel, kMaxCombs> combs_;
};
```

---

## Implementation Order

### Sprint 1: Foundation (Estimated: 2-3 days)
1. **`filter_tables.h`** (Layer 0) - Formant data, design tables
2. **`filter_design.h`** (Layer 0) - Design utilities
3. **`one_pole.h`** (Layer 1) - Simple LP/HP/Leaky

### Sprint 2: Core Filters (Estimated: 3-4 days)
4. **`svf.h`** (Layer 1) - TPT State Variable Filter
5. **`allpass_1pole.h`** (Layer 1) - First-order allpass
6. **`comb_filter.h`** (Layer 1) - FF/FB/Schroeder combs

### Sprint 3: Advanced Filters (Estimated: 3-4 days)
7. **`ladder_filter.h`** (Layer 1) - Moog ladder
8. **`crossover_filter.h`** (Layer 2) - LR crossovers

### Sprint 4: Effect Filters (Estimated: 3-4 days)
9. **`formant_filter.h`** (Layer 2) - Vowel filtering
10. **`envelope_filter.h`** (Layer 2) - Auto-wah
11. **`phaser.h`** (Layer 2) - Phaser effect

### Sprint 5: Spectral Processing (Estimated: 2-3 days)
*Note: `fft.h`, `stft.h`, `window_functions.h`, `spectral_buffer.h` already exist*

12. **`spectral_morph_filter.h`** (Layer 2) - Spectral morphing ← uses existing STFT
13. **`spectral_gate.h`** (Layer 2) - Per-bin gating ← uses existing FFT + envelope_follower
14. **`spectral_tilt.h`** (Layer 2) - Tilt filter (IIR approximation or FFT)

### Sprint 6: Physical Modeling (Estimated: 3-4 days)
*Note: `noise_generator.h` already exists with 13 noise types*

15. **`resonator_bank.h`** (Layer 2) - Modal resonator bank ← uses existing biquad
16. **`karplus_strong.h`** (Layer 2) - Plucked string ← uses existing delay_line, one_pole
17. **`waveguide_resonator.h`** (Layer 2) - Waveguide pipe ← uses existing delay_line, allpass_1pole
18. **`modal_resonator.h`** (Layer 2) - Modal synthesis ← uses existing biquad

### Sprint 7: Chaos & Randomization (Estimated: 2-3 days)
*Note: `random.h` (Xorshift32) already exists*

19. **`stochastic_filter.h`** (Layer 2) - Random parameter drift ← uses existing svf, random
20. **`self_osc_filter.h`** (Layer 2) - Self-oscillating filter ← uses existing ladder_filter
21. **`sample_hold_filter.h`** (Layer 2) - S&H modulation ← uses existing svf, random, lfo

### Sprint 8: Reactive Filters (Estimated: 2-3 days)
*Note: `pitch_detector.h`, `envelope_follower.h` already exist*

22. **`sidechain_filter.h`** (Layer 2) - Envelope-controlled filter ← uses existing components
23. **`transient_filter.h`** (Layer 2) - Transient-aware filter ← uses existing envelope_follower
24. **`pitch_tracking_filter.h`** (Layer 2) - Pitch-following filter ← uses existing pitch_detector

### Sprint 9: Exotic Modulation (Estimated: 3-4 days)
25. **`hilbert_transform.h`** (Layer 1) - Analytic signal via allpass cascade (NEW primitive)
26. **`audio_rate_filter_fm.h`** (Layer 2) - Audio-rate modulation ← uses existing svf, oversampler
27. **`frequency_shifter.h`** (Layer 2) - Single-sideband shifting ← uses NEW hilbert_transform
28. **`filter_matrix.h`** (Layer 3) - Feedback matrix ← extends existing feedback_network concepts

### Sprint 10: Sequenced Filters (Estimated: 2-3 days)
29. **`filter_sequencer.h`** (Layer 3) - Step sequencer ← uses existing svf, smoother
30. **`vowel_sequencer.h`** (Layer 3) - Vowel pattern sequencer ← uses existing formant_filter
31. **`multistage_env_filter.h`** (Layer 2) - Complex envelopes ← uses existing svf, smoother

### Sprint 11: Granular & Time-Domain (Estimated: 2-3 days)
*Note: `spectral_delay.h` (Layer 4) and `granular_engine.h` already exist*

32. **`granular_filter.h`** (Layer 3) - Per-grain filtering ← extends existing granular_engine
33. **`timevar_comb_bank.h`** (Layer 3) - Modulated comb bank ← uses existing comb_filter, lfo
    *(spectral_delay_filter.h removed - already implemented as `effects/spectral_delay.h`)*

---

## Dependency Graph

```
Layer 0 (Core) - ALL EXISTING
├── math_constants.h
├── db_utils.h
├── filter_tables.h (Phase 1)
├── filter_design.h (Phase 11)
├── window_functions.h ← used by spectral processing
└── random.h (Xorshift32) ← used by stochastic/S&H filters

Layer 1 (Primitives) - MOSTLY EXISTING
├── biquad.h
├── dc_blocker.h
├── smoother.h
├── delay_line.h
├── oversampler.h
├── lfo.h
├── fft.h ← used by all spectral filters
├── stft.h ← STFT + OverlapAdd
├── spectral_buffer.h
├── one_pole.h (Phase 6)
├── svf.h (Phase 2)
├── allpass_1pole.h (Phase 4)
├── comb_filter.h (Phase 3)
├── ladder_filter.h (Phase 5)
├── pitch_detector.h ← autocorrelation-based
└── hilbert_transform.h (NEW) ← allpass approximation for freq shifter

Layer 2 (Processors) - MIXED
├── multimode_filter.h
├── envelope_follower.h
├── noise_generator.h ← 13 noise types (white, pink, brown, etc.)
├── crossover_filter.h (Phase 7)
├── formant_filter.h (Phase 8)
├── envelope_filter.h (Phase 9)
├── phaser.h (Phase 10)
├── spectral_morph_filter.h (NEW) ← stft, spectral_buffer
├── spectral_gate.h (NEW) ← fft, envelope_follower
├── spectral_tilt.h (NEW) ← biquad cascade or fft
├── resonator_bank.h (NEW) ← biquad, smoother
├── karplus_strong.h (NEW) ← delay_line, one_pole, noise_generator
├── waveguide_resonator.h (NEW) ← delay_line, allpass_1pole, dc_blocker
├── modal_resonator.h (NEW) ← biquad, smoother
├── stochastic_filter.h (NEW) ← svf, random
├── self_osc_filter.h (NEW) ← ladder_filter, dc_blocker
├── sample_hold_filter.h (NEW) ← svf, random, lfo
├── sidechain_filter.h (NEW) ← envelope_follower, svf, delay_line
├── transient_filter.h (NEW) ← envelope_follower, svf
├── pitch_tracking_filter.h (NEW) ← pitch_detector, svf, smoother
├── audio_rate_filter_fm.h (NEW) ← svf, oversampler, lfo
├── frequency_shifter.h (NEW) ← hilbert_transform, lfo
└── multistage_env_filter.h (NEW) ← svf, smoother

Layer 3 (Systems) - MOSTLY EXISTING
├── granular_engine.h ← complete granular synthesis
├── feedback_network.h ← feedback routing with filters/saturation
├── modulation_matrix.h ← source→dest routing
├── filter_matrix.h (NEW) ← extends feedback_network for multi-filter routing
├── filter_sequencer.h (NEW) ← svf, smoother
├── vowel_sequencer.h (NEW) ← formant_filter, smoother
├── granular_filter.h (NEW) ← extends granular_engine with per-grain filtering
└── timevar_comb_bank.h (NEW) ← comb_filter, lfo, smoother

Layer 4 (Effects) - MOSTLY EXISTING
└── spectral_delay.h ← ALREADY IMPLEMENTS per-bin delays, freeze, tilt, diffusion
```

**Summary:** Of 39 components in the original roadmap, approximately 22 already exist.
The remaining 17 NEW components can all leverage existing primitives.

---

## Testing Strategy

Each new component needs:

1. **Unit Tests** (`dsp/tests/primitives/` or `dsp/tests/processors/`)
   - Coefficient calculation verification
   - Frequency response spot checks
   - Stability under extreme parameters
   - Reset/state clearing

2. **Approval Tests** (for complex filters)
   - Impulse response snapshots
   - Frequency sweep response
   - Modulation stability (SVF, ladder)

3. **Performance Benchmarks**
   - ops/sample measurement
   - Comparison with existing `MultimodeFilter`

---

## References

### Primary Sources
- [Cytomic Technical Papers](https://cytomic.com/technical-papers/)
- [RBJ Audio EQ Cookbook](https://webaudio.github.io/Audio-EQ-Cookbook/)
- [Stanford CCRMA Filters](https://ccrma.stanford.edu/~jos/filters/)
- [Vadim Zavalishin - The Art of VA Filter Design](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf)

### Moog Ladder
- [Huovilainen DAFX 2004](https://dafx.de/paper-archive/2004/P_061.PDF)
- [MoogLadders Collection](https://github.com/ddiakopoulos/MoogLadders)
- [D'Angelo/Välimäki Improved Model](https://www.researchgate.net/publication/261193653_An_improved_virtual_analog_model_of_the_Moog_ladder_filter)

### Reverb/Diffusion
- [Schroeder Allpass (CCRMA)](https://ccrma.stanford.edu/~jos/pasp/Schroeder_Allpass_Sections.html)
- [Valhalla DSP - Diffusion](https://valhalladsp.com/2011/01/21/reverbs-diffusion-allpass-delays-and-metallic-artifacts/)

### Formants
- [CCRMA Formant Filtering](https://ccrma.stanford.edu/~jos/filters/Formant_Filtering_Example.html)
- [Formant Frequency Tables](https://www.researchgate.net/figure/Formant-Frequencies-Hz-F1-F2-F3-for-Typical-Vowels_tbl1_332054208)

### Spectral Processing (Phases 12+)
- [DAFX Book - Spectral Processing](http://dafx.de/)
- [Phase Vocoder Tutorial (CCRMA)](https://ccrma.stanford.edu/~jos/sasp/Phase_Vocoder.html)
- [Spectral Morphing (Miller Puckette)](http://msp.ucsd.edu/techniques/v0.11/book-html/node115.html)
- [STFT/ISTFT Implementation](https://www.dsprelated.com/freebooks/sasp/STFT_Processing.html)

### Physical Modeling (Phase 13)
- [Karplus-Strong (CCRMA)](https://ccrma.stanford.edu/~jos/pasp/Karplus_Strong_Synthesis.html)
- [Digital Waveguides (Smith)](https://ccrma.stanford.edu/~jos/waveguide/)
- [Modal Synthesis Tutorial](https://ccrma.stanford.edu/~bilbao/booktop/node14.html)
- [Physical Audio Signal Processing](https://ccrma.stanford.edu/~jos/pasp/)

### Chaos & Randomization (Phase 14)
- [Lorenz Attractor](https://en.wikipedia.org/wiki/Lorenz_system)
- [Perlin Noise for Audio](https://www.musicdsp.org/en/latest/Synthesis/216-perlin-noise.html)
- [Chaotic Oscillators in Sound Synthesis](https://www.researchgate.net/publication/228981389_Chaotic_oscillators_for_sound_synthesis)

### Pitch Detection (Phase 15)
- [YIN Algorithm Paper](https://audition.ens.fr/adc/pdf/2002_JASA_YIN.pdf)
- [Autocorrelation Pitch Detection (CCRMA)](https://ccrma.stanford.edu/~jos/sasp/Autocorrelation_Pitch_Detector.html)
- [pYIN Probabilistic YIN](https://www.eecs.qmul.ac.uk/~siMDirrty/papers/pyin.pdf)

### Frequency Shifting (Phase 16)
- [Hilbert Transform for SSB (CCRMA)](https://ccrma.stanford.edu/~jos/st/Hilbert_Transform.html)
- [Frequency Shifting vs Pitch Shifting](https://www.soundonsound.com/techniques/frequency-shifting-vs-pitch-shifting)
- [Bode Frequency Shifter](https://www.muffwiggler.com/forum/viewtopic.php?t=15289)

### Granular Processing (Phase 18)
- [Granular Synthesis (Roads)](https://www.mitpress.mit.edu/books/computer-music-tutorial)
- [Real-time Granular Synthesis (CCRMA)](https://ccrma.stanford.edu/~jos/parshl/Granular_Synthesis.html)
- [Spectral Delay Networks](https://www.dafx.de/paper-archive/2009/papers/paper_45.pdf)

---

## Appendix: Filter Selection Quick Reference

### Foundation Filters (Phases 1-11)

| Use Case | Recommended Component | Layer |
|----------|----------------------|-------|
| General EQ | `Biquad` / `MultimodeFilter` | 1/2 |
| Synth filter (modulated) | `SVF` | 1 |
| Classic analog LP | `LadderFilter` | 1 |
| Crossover/multiband | `CrossoverLR4` | 2 |
| Parameter smoothing | `OnePoleSmoother` | 1 |
| Tone control (simple) | `OnePoleLP` / `OnePoleHP` | 1 |
| DC removal | `DCBlocker` / `DCBlocker2` | 1 |
| Phaser | `Phaser` (cascaded `Allpass1Pole`) | 2 |
| Flanger/chorus | `FeedforwardComb` + `LFO` | 1 |
| Reverb diffusion | `SchroederAllpass` / `DiffusionNetwork` | 1/2 |
| Vowel effects | `FormantFilter` | 2 |
| Auto-wah | `EnvelopeFilter` | 2 |
| Feedback tone | `Biquad` cascade + `DCBlocker2` | 1 |

### Advanced Sound Design (Phases 12-18)

| Use Case | Recommended Component | Layer | Status |
|----------|----------------------|-------|--------|
| Spectral morphing | `SpectralMorphFilter` | 2 | COMPLETE (spec-080) |
| Spectral gating | `SpectralGate` | 2 | COMPLETE (spec-081) |
| Tilt EQ | `SpectralTilt` | 2 | COMPLETE (spec-082) |
| Marimba/bell resonance | `ResonatorBank` | 2 | COMPLETE (spec-083) |
| Plucked strings | `KarplusStrong` | 2 | COMPLETE (spec-084) |
| Flute/pipe resonance | `WaveguideResonator` | 2 | COMPLETE (spec-085) |
| Complex body resonance | `ModalResonator` | 2 | COMPLETE (spec-086) |
| Random filter drift | `StochasticFilter` | 2 | COMPLETE (spec-087) |
| Sine generator from filter | `SelfOscillatingFilter` | 2 | COMPLETE (spec-088) |
| Stepped modulation | `SampleHoldFilter` | 2 | COMPLETE (spec-089) |
| Ducking/pumping filter | `SidechainFilter` | 2 | COMPLETE (spec-090) |
| Transient shaping | `TransientAwareFilter` | 2 | COMPLETE (spec-091) |
| Harmonic tracking | `PitchTrackingFilter` | 2 | COMPLETE (spec-092) |
| Metallic FM tones | `AudioRateFilterFM` | 2 | NEW |
| Complex resonant networks | `FilterFeedbackMatrix` | 3 | NEW |
| Inharmonic shifting | `FrequencyShifter` | 2 | NEW |
| Rhythmic filter patterns | `FilterStepSequencer` | 3 | NEW |
| Talking filter | `VowelSequencer` | 3 | NEW |
| Complex envelope shapes | `MultiStageEnvelopeFilter` | 2 | NEW |
| Per-grain processing | `GranularFilter` | 3 | NEW (extends existing) |
| Spectral smearing/freeze | `SpectralDelay` | 4 | **EXISTS** |
| Evolving metallic textures | `TimeVaryingCombBank` | 3 | NEW |

**Note:** `SpectralDelay` (effects/spectral_delay.h) already provides per-bin delays with freeze, tilt, and diffusion. 13 of 22 advanced filter components are now complete (specs 080-092). The remaining 9 components are planned for future sprints.
