# Filter Implementation Roadmap

A step-by-step plan for implementing comprehensive filter support using the existing layered DSP architecture.

## Executive Summary

This roadmap covers implementing all filter types from `DSP-FILTER-TECHNIQUES.md` using the Krate::DSP layered architecture. The plan maximizes reuse of existing components and follows strict layer dependencies.

**Estimated Components:**
- Layer 0 (Core): 2 new files
- Layer 1 (Primitives): 5 new files
- Layer 2 (Processors): 4 new files
- Layer 3 (Systems): 2 new files

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

---

## Dependency Graph

```
Layer 0 (Core)
├── math_constants.h (existing)
├── db_utils.h (existing)
├── filter_tables.h (new)
└── filter_design.h (new)

Layer 1 (Primitives)
├── biquad.h (existing)
├── dc_blocker.h (existing)
├── smoother.h (existing)
├── delay_line.h (existing)
├── oversampler.h (existing)
├── lfo.h (existing)
├── one_pole.h (new) ← math_constants
├── svf.h (new) ← math_constants, db_utils
├── allpass_1pole.h (new) ← math_constants
├── comb_filter.h (new) ← delay_line, dc_blocker
└── ladder_filter.h (new) ← oversampler, math_constants

Layer 2 (Processors)
├── multimode_filter.h (existing)
├── envelope_follower.h (existing)
├── crossover_filter.h (new) ← biquad, smoother
├── formant_filter.h (new) ← biquad, filter_tables, smoother
├── envelope_filter.h (new) ← envelope_follower, svf
└── phaser.h (new) ← allpass_1pole, lfo, smoother

Layer 3 (Systems) - Future extensions
├── multiband_processor.h ← crossover_filter
└── vocal_processor.h ← formant_filter, envelope_filter
```

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

---

## Appendix: Filter Selection Quick Reference

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
