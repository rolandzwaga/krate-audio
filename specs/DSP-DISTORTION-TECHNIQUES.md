# DSP Distortion Techniques Reference

A comprehensive reference guide for implementing various distortion effects in digital audio processing. This document covers algorithms, anti-aliasing strategies, and practical implementation considerations.

## Table of Contents

1. [Waveshaping Fundamentals](#waveshaping-fundamentals)
2. [Soft Clipping Transfer Functions](#soft-clipping-transfer-functions)
3. [Hard Clipping](#hard-clipping)
4. [Tube/Valve Amplifier Modeling](#tubevalve-amplifier-modeling)
5. [Transistor & Solid-State Distortion](#transistor--solid-state-distortion)
6. [Tape Saturation](#tape-saturation)
7. [Fuzz & Asymmetric Clipping](#fuzz--asymmetric-clipping)
8. [Wavefolding](#wavefolding)
9. [Bitcrusher & Digital Distortion](#bitcrusher--digital-distortion)
10. [Chebyshev Polynomial Waveshaping](#chebyshev-polynomial-waveshaping)
11. [Neural Network Amp Modeling](#neural-network-amp-modeling)
12. [Anti-Aliasing Techniques](#anti-aliasing-techniques)
13. [DC Blocking](#dc-blocking)
14. [Practical Implementation Guidelines](#practical-implementation-guidelines)

---

## Waveshaping Fundamentals

Waveshaping is the process of transforming a signal using a transfer function (also called a waveshaping curve). The input signal's amplitude determines which part of the transfer function is applied, creating harmonic distortion.

### Key Concepts

- **Memoryless nonlinearity**: Output depends only on current input (no state)
- **Static vs. dynamic**: Real analog circuits often have frequency-dependent behavior
- **Symmetric vs. asymmetric**: Symmetric curves produce odd harmonics; asymmetric produce even and odd

### Basic Implementation

```cpp
// Generic waveshaper
float waveshape(float input, float (*transferFunction)(float)) {
    return transferFunction(input);
}
```

### Harmonic Generation

| Transfer Function Symmetry | Harmonics Produced |
|---------------------------|-------------------|
| Odd symmetry (point-symmetric about origin) | Odd harmonics only (3rd, 5th, 7th...) |
| Even symmetry (symmetric about y-axis) | Even harmonics only (2nd, 4th, 6th...) |
| Asymmetric | Both odd and even harmonics |

**Sources:**
- [KVR Audio - Transparent Clipper Discussion](https://www.kvraudio.com/forum/viewtopic.php?t=122309)
- [Musicdsp.org - Variable Hardness Clipping](https://www.musicdsp.org/en/latest/Effects/104-variable-hardness-clipping-function.html)

---

## Soft Clipping Transfer Functions

### Hyperbolic Tangent (tanh)

The most commonly used soft clipping function. Provides smooth, musical saturation.

```cpp
// Basic tanh soft clipping
float softClipTanh(float x) {
    return std::tanh(x);
}

// Variable hardness tanh
float softClipTanhVariable(float x, float drive) {
    return std::tanh(drive * x) / std::tanh(drive);
}

// Threshold-based tanh (linear below threshold)
float softClipTanhThreshold(float x, float threshold, float a, float b) {
    if (std::abs(x) < threshold) {
        return x;  // Linear region
    }
    float sign = (x > 0) ? 1.0f : -1.0f;
    return sign * (threshold + a * std::tanh(b * (std::abs(x) - threshold)));
}
```

**Characteristics:**
- Smooth transition from linear to saturated
- Output asymptotically approaches ±1
- Good model for differential transistor pairs (e.g., Moog ladder filter)

### Arctangent (atan)

```cpp
float softClipAtan(float x) {
    return (2.0f / M_PI) * std::atan(x);  // Normalized to [-1, 1]
}

float softClipAtanVariable(float x, float drive) {
    return (2.0f / M_PI) * std::atan(drive * x);
}
```

**Characteristics:**
- Similar to tanh but slightly different harmonic content
- Explicit derivative makes it useful for calculus-based tuning

### Error Function (erf)

```cpp
float softClipErf(float x) {
    return std::erf(x);
}
```

**Characteristics:**
- Used in the DAFX book for tape saturation emulation
- Produces odd spectral nulls not present in tanh
- Good approximation: Abramowitz and Stegun formula

### Reciprocal Square Root (Fast Alternative)

```cpp
// ~13x faster than tanh, similar behavior
float softClipFast(float x) {
    return x / std::sqrt(x * x + 1.0f);
}

// SIMD-friendly version
float softClipFastApprox(float x) {
    return x * rsqrtf(x * x + 1.0f);  // Use fast rsqrt
}
```

**Characteristics:**
- Much faster than transcendental functions
- Vectorizes well for SIMD
- Behaves similarly to tanh

### Cubic Soft Clipper (Julius O. Smith)

```cpp
float softClipCubic(float x) {
    if (x <= -1.0f) return -2.0f / 3.0f;
    if (x >= 1.0f) return 2.0f / 3.0f;
    return x - (x * x * x) / 3.0f;
}

// Normalized version
float softClipCubicNorm(float x) {
    float clipped = std::clamp(x, -1.0f, 1.0f);
    return 1.5f * clipped * (1.0f - clipped * clipped / 3.0f);
}
```

**Characteristics:**
- f'(±1) = 0, ensuring smooth transition to clipping
- Polynomial form: `f(x) = 1.5x - 0.5x³`
- Simple to compute, good for low-CPU scenarios

### Performance Comparison

| Function | Relative Speed | Quality | Notes |
|----------|---------------|---------|-------|
| tanh | 1.0x (baseline) | Excellent | Best musical quality |
| atan | ~1.2x | Very Good | Slightly brighter |
| erf | ~0.9x | Very Good | Unique spectral character |
| recipSqrt | ~13x | Good | Best for real-time |
| cubic | ~10x | Good | Simple polynomial |

**Sources:**
- [Raph Levien - A Few of My Favorite Sigmoids](https://raphlinus.github.io/audio/2018/09/05/sigmoid.html)
- [KVR Audio - Soft Clipping Algorithm](https://www.kvraudio.com/forum/viewtopic.php?t=195315)
- [GitHub - SRPlugins DSP Saturation](https://github.com/johannesmenzel/SRPlugins/wiki/DSP-ALGORITHMS-Saturation)

---

## Hard Clipping

Hard clipping abruptly limits the signal at a threshold, creating a square-ish waveform.

```cpp
float hardClip(float x, float threshold = 1.0f) {
    return std::clamp(x, -threshold, threshold);
}

// Asymmetric hard clipping
float hardClipAsymmetric(float x, float posThreshold, float negThreshold) {
    return std::clamp(x, negThreshold, posThreshold);
}
```

**Characteristics:**
- Creates strong odd harmonics
- Introduces significant aliasing (discontinuities in waveform)
- Requires oversampling or ADAA/polyBLAMP for quality results

**Anti-Aliasing Requirement:** Hard clipping introduces discontinuities that cause severe aliasing. Always use:
1. Oversampling (4x minimum, 8x recommended), or
2. polyBLAMP correction, or
3. ADAA (Antiderivative Anti-Aliasing)

---

## Tube/Valve Amplifier Modeling

### Overview

Tube amplifier modeling requires simulating the nonlinear behavior of vacuum tubes (triodes, pentodes) and associated circuitry.

### Modeling Approaches

#### 1. Static Waveshaping

Simple approach using a transfer function approximation:

```cpp
// Simplified triode model
float triodeWaveshape(float x, float bias, float drive) {
    float biased = x + bias;
    if (biased <= 0.0f) return 0.0f;
    return std::tanh(drive * std::pow(biased, 1.5f));
}
```

#### 2. Wave Digital Filters (WDFs)

WDFs transform analog circuits into digital equivalents by modeling each component.

**Key concepts:**
- Triode modeled as memoryless nonlinear three-port element
- Uses Newton-Raphson or secant method for solving nonlinear equations
- Can achieve ~50% less CPU than older methods with richer harmonics

#### 3. Differential Equations

Model the tube's plate current using the Koren or Child-Langmuir equations:

```cpp
// Koren triode model (simplified)
struct KorenTriode {
    float mu = 100.0f;    // Amplification factor
    float ex = 1.4f;      // Exponent
    float kg1 = 1060.0f;  // Grid constant
    float kp = 600.0f;    // Plate constant
    float kvb = 300.0f;   // Knee voltage
    
    float plateCurrent(float vp, float vg) {
        float e1 = vp / kp * std::log(1.0f + std::exp(kp * (1.0f / mu + vg / std::sqrt(kvb + vp * vp))));
        return std::pow(e1, ex) / kg1 * (1.0f + sign(e1));
    }
};
```

#### 4. Neural Network Models

Modern approach using LSTM, GRU, or WaveNet (see [Neural Network Amp Modeling](#neural-network-amp-modeling)).

### Triode vs. Pentode Characteristics

| Characteristic | Triode | Pentode |
|---------------|--------|---------|
| Harmonics | Primarily 2nd, 3rd | Richer harmonic content |
| Compression | Gradual | More abrupt |
| Sound | Warm, smooth | Aggressive, edgy |
| Complexity | Simpler to model | Variable-μ behavior |

**Sources:**
- [ResearchGate - Wave Digital Triode Model](https://www.researchgate.net/publication/224597228_Enhanced_Wave_Digital_Triode_Model_for_Real-Time_Tube_Amplifier_Emulation)
- [DAFx Paper - Real-Time Guitar Tube Amplifier Simulation](https://www.dafx.de/paper-archive/2010/DAFx10/MacakSchimmel_DAFx10_P12.pdf)
- [Norman Koren - Improved SPICE Tube Models](https://www.normankoren.com/Audio/Tubemodspice_article.html)

---

## Transistor & Solid-State Distortion

### Diode Clipping

The foundation of most solid-state distortion effects.

#### Shockley Diode Equation

```cpp
// Diode current-voltage relationship
float diodeCurrent(float voltage, float Is, float Vt, float n = 1.0f) {
    // Is = saturation current (~1e-12 for silicon)
    // Vt = thermal voltage (~26mV at room temperature)
    // n = ideality factor
    return Is * (std::exp(voltage / (n * Vt)) - 1.0f);
}
```

#### Diode Clipping Configurations

```cpp
// Symmetric diode clipping (back-to-back diodes)
float symmetricDiodeClip(float x, float threshold = 0.7f) {
    // Silicon diodes clip at ~0.6-0.7V
    if (x > threshold) {
        return threshold + 0.1f * std::tanh((x - threshold) * 10.0f);
    } else if (x < -threshold) {
        return -threshold + 0.1f * std::tanh((x + threshold) * 10.0f);
    }
    return x;
}

// Asymmetric clipping (different diode counts per polarity)
float asymmetricDiodeClip(float x, float posThreshold, float negThreshold) {
    // e.g., 2 silicon diodes positive (1.4V), 1 germanium negative (0.3V)
    float posClip = posThreshold + 0.1f * std::tanh((x - posThreshold) * 10.0f);
    float negClip = negThreshold + 0.1f * std::tanh((x + negThreshold) * 10.0f);
    
    if (x > posThreshold) return posClip;
    if (x < negThreshold) return negClip;
    return x;
}
```

### Diode Types and Forward Voltages

| Diode Type | Forward Voltage (Vf) | Clipping Character |
|------------|---------------------|-------------------|
| Schottky | 0.15-0.45V | Very soft, early clipping |
| Germanium | 0.25-0.35V | Soft, warm, "tube-like" |
| Silicon | 0.6-0.7V | Medium, standard distortion |
| LED | 1.5-2.2V | Hard, bright, aggressive |

### Op-Amp Feedback Clipping

Many overdrive pedals use diodes in the feedback loop of an op-amp:

```cpp
// Simplified op-amp with feedback clipping
struct FeedbackClipper {
    float gain = 100.0f;
    float clipThreshold = 0.6f;
    
    float process(float input) {
        float amplified = input * gain;
        // Diodes in feedback limit effective gain
        return std::tanh(amplified / clipThreshold) * clipThreshold;
    }
};
```

**Sources:**
- [Baltic Lab - DSP Diode Clipping Algorithm](https://baltic-lab.com/2023/08/dsp-diode-clipping-algorithm-for-overdrive-and-distortion-effects/)
- [ElectroSmash - Fuzz Face Analysis](https://www.electrosmash.com/fuzz-face)
- [Gerlt Technologies - Clipping Configurations](https://www.gerlttechnologies.com/index.php/more-info/circuits/165-clipping-configurations)

---

## Tape Saturation

### Overview

Tape saturation involves magnetic hysteresis, frequency-dependent saturation, and various analog artifacts.

### Jiles-Atherton Hysteresis Model

The gold standard for tape saturation modeling:

```cpp
struct JilesAthertonHysteresis {
    // Hysteresis parameters
    float Ms = 350000.0f;  // Saturation magnetization
    float a = 22000.0f;    // Shape parameter
    float alpha = 1.6e-3f; // Inter-domain coupling
    float k = 27.0f;       // Pinning coefficient
    float c = 1.7e-1f;     // Reversibility
    
    float M = 0.0f;        // Current magnetization (state)
    float H_prev = 0.0f;   // Previous field
    
    float langevin(float x) {
        if (std::abs(x) < 1e-6f) return x / 3.0f;
        return 1.0f / std::tanh(x) - 1.0f / x;
    }
    
    float process(float H) {
        // Simplified discretization
        float He = H + alpha * M;
        float Man = Ms * langevin(He / a);
        float dMdt = (Man - M) / k;
        
        M += dMdt;  // Euler integration
        H_prev = H;
        return M / Ms;  // Normalized output
    }
};
```

### Solver Options

| Solver | Accuracy | CPU Cost | Notes |
|--------|----------|----------|-------|
| RK2 (Runge-Kutta 2nd) | Medium | Low | Good for real-time |
| RK4 (Runge-Kutta 4th) | High | Medium | Standard choice |
| Newton-Raphson (4 iter) | High | Medium-High | More accurate |
| Newton-Raphson (8 iter) | Very High | High | Best accuracy |

### Simplified Tape Saturation

For less CPU-intensive implementations:

```cpp
struct SimpleTapeSaturation {
    float preEmphasisFreq = 3000.0f;
    float deEmphasisFreq = 3000.0f;
    float saturationAmount = 0.5f;
    
    // Pre-emphasis: boost highs before saturation
    OnePoleFilter preEmphasis;
    // De-emphasis: cut highs after saturation
    OnePoleFilter deEmphasis;
    
    float process(float input) {
        // 1. Pre-emphasis (boost highs)
        float emphasized = preEmphasis.processHighShelf(input);
        
        // 2. Saturation (erf or tanh work well for tape character)
        float saturated = std::erf(emphasized * saturationAmount);
        
        // 3. De-emphasis (cut highs)
        return deEmphasis.processLowShelf(saturated);
    }
};
```

### Tape Characteristics to Model

- **Hysteresis**: Creates "memory" and soft, asymmetric saturation
- **Head bump**: Low-frequency resonance (typically 60-120 Hz)
- **High-frequency rolloff**: Natural filtering of highs
- **Bias effects**: AC bias (~100kHz) affects saturation behavior
- **Wow and flutter**: Speed variations (for vintage effect)

**Sources:**
- [DAFx 2019 - Real-time Physical Modelling for Analog Tape Machines](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf)
- [ChowTape User Manual](https://chowdsp.com/manuals/ChowTapeManual.pdf)
- [Medium - Tape Emulation with Neural Networks](https://medium.com/swlh/tape-emulation-with-neural-networks-699bb42b9394)

---

## Fuzz & Asymmetric Clipping

### Fuzz Face Style Distortion

The classic Fuzz Face creates asymmetric clipping through transistor biasing.

```cpp
struct FuzzFace {
    float inputBias = 0.2f;      // DC bias from transistor coupling
    float saturationGain = 10.0f;
    OnePoleFilter dcBlocker;     // ~20Hz highpass
    
    float process(float input) {
        // Apply bias (causes asymmetry)
        float biased = input + inputBias;
        
        // Saturation (asymmetric due to bias)
        float saturated = std::tanh(biased * saturationGain);
        
        // Remove DC offset created by asymmetric clipping
        return dcBlocker.processHighPass(saturated);
    }
};
```

### Germanium vs. Silicon Transistor Characteristics

| Characteristic | Germanium (e.g., AC128) | Silicon (e.g., 2N3906) |
|---------------|------------------------|----------------------|
| Sound | Warm, creamy, smooth | Harsher, more high-end |
| Clipping | Softer transition | Sharper transition |
| Temperature | Very sensitive | Stable |
| Consistency | Variable between units | Consistent |
| Gain | Lower (~70-100 hFE) | Higher (~100-300 hFE) |

### Creating Asymmetric Clipping

```cpp
// Method 1: DC offset before symmetric saturation
float asymmetricViaBias(float x, float bias, float gain) {
    float saturated = std::tanh((x + bias) * gain);
    return saturated;  // Must DC-block after
}

// Method 2: Different functions for positive/negative
float asymmetricViaFunction(float x, float posGain, float negGain) {
    if (x >= 0.0f) {
        return std::tanh(x * posGain);
    } else {
        return std::tanh(x * negGain);
    }
}

// Method 3: Different thresholds
float asymmetricViaThreshold(float x, float posThresh, float negThresh) {
    if (x > posThresh) {
        return posThresh + 0.1f * std::tanh((x - posThresh) * 10.0f);
    } else if (x < -negThresh) {
        return -negThresh + 0.1f * std::tanh((x + negThresh) * 10.0f);
    }
    return x;
}
```

**Sources:**
- [Z² DSP - Modelling Fuzz](https://z2dsp.com/2017/09/04/modelling-fuzz/)
- [Barbarach BC - Building a Fuzz Face Clone](https://barbarach.com/building-a-fuzz-face-clone-intro-analysis/)
- [Effectrode - All Flavours of Fuzz, Overdrive and Distortion](https://www.effectrode.com/knowledge-base/all-flavours-of-fuzz-overdrive-and-distortion/)

---

## Wavefolding

### Overview

Wavefolding "folds" the waveform back on itself when it exceeds a threshold, creating rich harmonic content.

### Basic Wavefolding

```cpp
// Simple triangle wavefolder
float wavefoldTriangle(float x, float threshold = 1.0f) {
    // Fold signal back when exceeding threshold
    float folded = x;
    while (std::abs(folded) > threshold) {
        if (folded > threshold) {
            folded = 2.0f * threshold - folded;
        } else if (folded < -threshold) {
            folded = -2.0f * threshold - folded;
        }
    }
    return folded;
}

// Sine wavefolder (Serge-style)
float wavefoldSine(float x, float gain = 1.0f) {
    return std::sin(x * gain);
}
```

### Buchla 259 Wavefolder

The Buchla 259 uses five parallel op-amp folding stages:

```cpp
struct Buchla259Wavefolder {
    // Simplified model of 5-stage parallel architecture
    float stages[5] = {0.5f, 0.7f, 0.9f, 1.1f, 1.3f};  // Thresholds
    float gains[5] = {1.0f, 0.8f, 0.6f, 0.4f, 0.2f};   // Stage gains
    
    float process(float input, float foldAmount) {
        float output = input;
        float scaled = input * foldAmount;
        
        for (int i = 0; i < 5; i++) {
            if (std::abs(scaled) > stages[i]) {
                float excess = std::abs(scaled) - stages[i];
                float folded = stages[i] - excess * gains[i];
                output = (scaled > 0) ? folded : -folded;
            }
        }
        return output;
    }
};
```

### Serge Wave Multiplier

Uses Norton amplifiers for a distinct folding character:

```cpp
// Lockhart wavefolder (Serge-inspired)
// Uses Lambert-W function for accurate modeling
float lambertW(float x) {
    // Approximation for x > 0
    float w = std::log(1.0f + x);
    for (int i = 0; i < 3; i++) {
        float ew = std::exp(w);
        w = w - (w * ew - x) / (ew * (w + 1.0f));
    }
    return w;
}

float lockhartWavefolder(float x, float gain = 1.0f) {
    const float a = 2.0f;  // Diode ideality factor × thermal voltage
    float scaled = x * gain;
    return a * lambertW(scaled / a);
}
```

### Anti-Aliasing for Wavefolders

Wavefolding creates significant high-frequency content. Use:
- **ADAA** (1st or 2nd order) for efficient anti-aliasing
- **Oversampling** (4x-8x) for highest quality
- **polyBLAMP** for correcting derivative discontinuities

**Sources:**
- [DAFx 2017 - Virtual Analog Buchla 259 Wavefolder](https://www.dafx17.eca.ed.ac.uk/papers/DAFx17_paper_82.pdf)
- [CCRMA - Wavefolder](https://ccrma.stanford.edu/~jatin/ComplexNonlinearities/Wavefolder.html)
- [Medium - Complex Nonlinearities Episode 6: Wavefolding](https://jatinchowdhury18.medium.com/complex-nonlinearities-episode-6-wavefolding-9529b5fe4102)
- [ResearchGate - Virtual Analog Models of Lockhart and Serge Wavefolders](https://www.researchgate.net/publication/321985944_Virtual_Analog_Models_of_the_Lockhart_and_Serge_Wavefolders)

---

## Bitcrusher & Digital Distortion

### Bit Depth Reduction

```cpp
float bitcrush(float input, int bits) {
    float levels = std::pow(2.0f, bits);
    float halfLevels = levels / 2.0f;
    
    // Quantize to discrete levels
    float quantized = std::round(input * halfLevels) / halfLevels;
    return std::clamp(quantized, -1.0f, 1.0f);
}

// With dithering to reduce quantization artifacts
float bitcrushDithered(float input, int bits, float ditherAmount = 1.0f) {
    float levels = std::pow(2.0f, bits);
    float halfLevels = levels / 2.0f;
    float lsb = 1.0f / halfLevels;
    
    // Triangular dither
    float dither = (randomFloat() - randomFloat()) * lsb * ditherAmount;
    
    float quantized = std::round((input + dither) * halfLevels) / halfLevels;
    return std::clamp(quantized, -1.0f, 1.0f);
}
```

### Sample Rate Reduction

```cpp
class SampleRateReducer {
    float heldSample = 0.0f;
    float phase = 0.0f;
    float targetRate;
    float hostRate;
    
public:
    SampleRateReducer(float target, float host) 
        : targetRate(target), hostRate(host) {}
    
    float process(float input) {
        float increment = targetRate / hostRate;
        phase += increment;
        
        if (phase >= 1.0f) {
            phase -= 1.0f;
            heldSample = input;  // Sample and hold
        }
        
        return heldSample;
    }
};
```

### Combined Bitcrusher

```cpp
class Bitcrusher {
    SampleRateReducer srReducer;
    int bits = 8;
    bool enableDither = true;
    
public:
    float process(float input) {
        // First reduce sample rate (creates aliasing)
        float reduced = srReducer.process(input);
        
        // Then reduce bit depth (creates quantization noise)
        if (enableDither) {
            return bitcrushDithered(reduced, bits);
        }
        return bitcrush(reduced, bits);
    }
};
```

### Sonic Characteristics by Bit Depth

| Bit Depth | Levels | Character |
|-----------|--------|-----------|
| 16 bit | 65,536 | CD quality, clean |
| 12 bit | 4,096 | Subtle grit |
| 8 bit | 256 | Retro, video game |
| 6 bit | 64 | Lo-fi, crunchy |
| 4 bit | 16 | Very harsh, extreme |
| 1 bit | 2 | Square wave/fuzz |

**Sources:**
- [Wikipedia - Bitcrusher](https://en.wikipedia.org/wiki/Bitcrusher)
- [ADSR - Building FX: Bitcrushing + Rate Reduction](https://www.adsrsounds.com/reaktor-tutorials/building-fx-part-vi-basic-bitcrushing/)
- [Perfect Circuit - Weird FX: Bitcrushers](https://www.perfectcircuit.com/signal/weird-fx-bitcrushers)
- [Pete Brown - Simple Bitcrusher in C++](http://10rem.net/blog/2013/01/13/a-simple-bitcrusher-and-sample-rate-reducer-in-cplusplus-for-a-windows-store-app)

---

## Chebyshev Polynomial Waveshaping

### Overview

Chebyshev polynomials allow precise control over harmonic content. The nth polynomial generates the nth harmonic from a sine wave input.

### Chebyshev Polynomials

```cpp
// First few Chebyshev polynomials
float chebyshev1(float x) { return x; }                           // Fundamental
float chebyshev2(float x) { return 2*x*x - 1; }                   // 2nd harmonic
float chebyshev3(float x) { return 4*x*x*x - 3*x; }               // 3rd harmonic
float chebyshev4(float x) { return 8*x*x*x*x - 8*x*x + 1; }       // 4th harmonic
float chebyshev5(float x) { return 16*x*x*x*x*x - 20*x*x*x + 5*x; } // 5th harmonic

// General recursive formula
float chebyshevN(float x, int n) {
    if (n == 0) return 1.0f;
    if (n == 1) return x;
    return 2.0f * x * chebyshevN(x, n-1) - chebyshevN(x, n-2);
}
```

### Harmonic Mixing with Chebyshev

```cpp
struct ChebyshevWaveshaper {
    std::array<float, 8> harmonicLevels = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    
    float process(float x) {
        float output = 0.0f;
        float Tn_2 = 1.0f;      // T_0(x)
        float Tn_1 = x;         // T_1(x)
        
        output += harmonicLevels[0] * Tn_2;
        output += harmonicLevels[1] * Tn_1;
        
        for (int n = 2; n < 8; n++) {
            float Tn = 2.0f * x * Tn_1 - Tn_2;
            output += harmonicLevels[n] * Tn;
            Tn_2 = Tn_1;
            Tn_1 = Tn;
        }
        
        return output;
    }
};
```

### Important Limitations

1. **Only works correctly for sine waves of amplitude 1.0**
2. **Non-sinusoidal input creates intermodulation products**
3. **High-order polynomials require oversampling** (at least Nx for Nth order)
4. **Input amplitude affects harmonic distribution**

### Practical Applications

- **Additive synthesis engine**: Control harmonics with one oscillator
- **Harmonic exciter**: Add specific harmonics to a signal
- **Odd/even distortion**: Use odd or even polynomials only

**Sources:**
- [KVR - Chebyshev Polynomial Waveshaper](https://www.kvraudio.com/forum/viewtopic.php?t=70372)
- [Kenny Peng - Investigating the Math of Waveshapers](https://kenny-peng.com/2022/06/18/chebyshev_harmonics.html)
- [Miller Puckette - Waveshaping using Chebyshev polynomials](http://msp.ucsd.edu/techniques/v0.08/book-html/node80.html)
- [UCSD Music - Waveshaping Synthesis](http://musicweb.ucsd.edu/~trsmyth/waveshaping/waveshaping_4up.pdf)

---

## Neural Network Amp Modeling

### Overview

Neural networks can learn the transfer characteristics of analog equipment from audio samples alone.

### Architecture Comparison

| Architecture | Strengths | Weaknesses | Real-Time Feasibility |
|--------------|-----------|------------|----------------------|
| LSTM | Good dynamics, moderate CPU | Harder to optimize | Medium networks |
| GRU | ~33% less CPU than LSTM | Similar to LSTM | Better for embedded |
| WaveNet | Best for complex nonlinearities | Higher CPU | Larger networks |
| Transformer | Excellent quality | Very high CPU | Challenging |

### LSTM/GRU Implementation

```cpp
// Conceptual structure (actual implementation requires ML library)
struct NeuralAmpModel {
    // Hidden state
    std::array<float, 32> hidden;
    std::array<float, 32> cell;  // For LSTM only
    
    // Weights (loaded from trained model)
    Matrix weightsInput;
    Matrix weightsHidden;
    Matrix weightsOutput;
    
    float process(float input) {
        // 1. Combine input with previous hidden state
        // 2. Apply gates (forget, input, output for LSTM)
        // 3. Update hidden state
        // 4. Output through dense layer
        return output;
    }
};
```

### Training Data Requirements

- **Minimum**: 3 minutes of audio (input/output pairs)
- **Better**: 10-30 minutes covering full dynamic range
- **Input**: DI guitar signal or test signals
- **Output**: Recorded through target amp/pedal

### Available Tools and Libraries

| Tool | Description | Platform |
|------|-------------|----------|
| [Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) | WaveNet-based, plugin included | Desktop |
| [GuitarLSTM](https://github.com/GuitarML/GuitarLSTM) | LSTM training with Keras | Training |
| [RTNeural](https://github.com/jatinchowdhury18/RTNeural) | Real-time inference library | C++ |
| NeuralSeed | GRU on Daisy Seed | Embedded |

### Embedded Considerations

For microcontrollers (e.g., Daisy Seed @ 48kHz):
- **LSTM size 8**: Maximum for real-time
- **GRU size 10**: Maximum for real-time (~33% more efficient)
- **Loss < 0.01**: Achievable for distortion pedals
- **Loss < 0.02**: Achievable for low-gain amps

**Sources:**
- [MDPI - Real-Time Guitar Amplifier Emulation with Deep Learning](https://www.mdpi.com/2076-3417/10/3/766)
- [GitHub - Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler)
- [GitHub - GuitarLSTM](https://github.com/GuitarML/GuitarLSTM)
- [Towards Data Science - Neural Networks for Real-Time Audio: WaveNet](https://towardsdatascience.com/neural-networks-for-real-time-audio-wavenet-2b5cdf791c4f/)

---

## Anti-Aliasing Techniques

### The Aliasing Problem

Nonlinear processing creates harmonics. When harmonics exceed the Nyquist frequency (half sample rate), they "fold back" into the audible spectrum as inharmonic artifacts.

### 1. Oversampling

Process at a higher sample rate, then downsample with anti-aliasing filter.

```cpp
class Oversampler {
    int factor;
    HalfbandFilter upsampleFilter;
    HalfbandFilter downsampleFilter;
    
public:
    Oversampler(int oversampleFactor) : factor(oversampleFactor) {}
    
    void process(float* input, float* output, int numSamples,
                 std::function<float(float)> nonlinearity) {
        // Upsample
        std::vector<float> upsampled(numSamples * factor);
        upsampleFilter.upsample(input, upsampled.data(), numSamples, factor);
        
        // Process at higher rate
        for (int i = 0; i < numSamples * factor; i++) {
            upsampled[i] = nonlinearity(upsampled[i]);
        }
        
        // Downsample
        downsampleFilter.downsample(upsampled.data(), output, numSamples, factor);
    }
};
```

#### Oversampling Factor Guidelines

| Factor | Aliasing Reduction | CPU Multiplier | Recommended Use |
|--------|-------------------|----------------|-----------------|
| 2x | ~18 dB | 2x | Mild saturation |
| 4x | ~36 dB | 4x | Standard distortion |
| 8x | ~54 dB (inaudible) | 8x | High-quality distortion |
| 16x | ~72 dB | 16x | Mastering/critical |

### 2. Antiderivative Anti-Aliasing (ADAA)

Analytically removes aliasing without oversampling.

```cpp
// First-order ADAA for hard clipping
class HardClipADAA {
    float x1 = 0.0f;
    
    // Antiderivative of hard clip
    float F(float x) {
        if (x < -1.0f) return -x - 0.5f;
        if (x > 1.0f) return x - 0.5f;
        return 0.5f * x * x;
    }
    
public:
    float process(float x0) {
        float result;
        
        if (std::abs(x0 - x1) < 1e-6f) {
            // Avoid division by zero - use L'Hôpital limit
            result = hardClip(0.5f * (x0 + x1));
        } else {
            result = (F(x0) - F(x1)) / (x0 - x1);
        }
        
        x1 = x0;
        return result;
    }
};

// Second-order ADAA (smoother, more CPU)
class HardClipADAA2 {
    float x1 = 0.0f, x2 = 0.0f;
    float ad1_x1 = 0.0f;
    
    // Second antiderivative
    float F2(float x) {
        if (x < -1.0f) return -0.5f*x*x - 0.5f*x + 1.0f/6.0f;
        if (x > 1.0f) return 0.5f*x*x - 0.5f*x + 1.0f/6.0f;
        return x*x*x / 6.0f;
    }
    
    // ... implementation details
};
```

### 3. polyBLAMP (Polynomial Bandlimited Ramp)

Corrects discontinuities in the first derivative (e.g., hard clipping corners).

```cpp
// Four-point polyBLAMP correction
float polyBLAMP4(float t) {
    // t is fractional position of discontinuity (0 to 1)
    float t2 = t * t;
    float t3 = t2 * t;
    float t4 = t3 * t;
    
    if (t < 1.0f) {
        return t4 / 24.0f;
    } else if (t < 2.0f) {
        float u = t - 1.0f;
        return (-3*u*u*u*u + 4*u*u*u + 6*u*u + 4*u + 1) / 24.0f;
    }
    // ... additional segments
    return 0.0f;
}
```

**Results:**
- Four-point polyBLAMP: ~20 dB average aliasing reduction
- Most prominent aliasing components: up to 43 dB reduction
- Minor high-frequency droop (~12 dB after 10 kHz, can be compensated)

### Technique Comparison

| Technique | CPU Cost | Quality | Implementation Complexity |
|-----------|----------|---------|--------------------------|
| No anti-aliasing | 1x | Poor | Trivial |
| 2x oversampling | ~2x | Medium | Low |
| 4x oversampling | ~4x | Good | Low |
| 8x oversampling | ~8x | Excellent | Low |
| ADAA 1st order | ~1.5x | Good | Medium |
| ADAA 2nd order | ~2x | Very Good | High |
| polyBLAMP | ~1.3x | Good | Medium |
| ADAA + 2x oversample | ~3x | Excellent | Medium |

**Sources:**
- [Nick's Blog - Introduction to Oversampling](https://www.nickwritesablog.com/introduction-to-oversampling-for-alias-reduction/)
- [FabFilter Pro-L 2 - Oversampling](https://www.fabfilter.com/help/pro-l/using/oversampling)
- [CCRMA - Practical Considerations for ADAA](https://ccrma.stanford.edu/~jatin/Notebooks/adaa.html)
- [GitHub - ADAA Experiments](https://github.com/jatinchowdhury18/ADAA)
- [ResearchGate - Aliasing Reduction in Soft-Clipping](https://www.researchgate.net/publication/282978216_Aliasing_reduction_in_soft-clipping_algorithms)

---

## DC Blocking

### Why DC Blocking is Necessary

Asymmetric distortion creates DC offset, which:
- Reduces headroom
- Can cause clicks when combined with other audio
- May damage speakers at extreme levels
- Accumulates through cascaded effects

### Standard DC Blocker

```cpp
class DCBlocker {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float R;  // Pole coefficient
    
public:
    DCBlocker(float cutoffHz, float sampleRate) {
        // R determines cutoff frequency
        // R = 1 - (250/sampleRate) gives -3dB @ 40Hz
        // R = 1 - (190/sampleRate) gives -3dB @ 30Hz
        // R = 1 - (126/sampleRate) gives -3dB @ 20Hz
        R = 1.0f - (2.0f * M_PI * cutoffHz / sampleRate);
    }
    
    float process(float x0) {
        // y[n] = x[n] - x[n-1] + R * y[n-1]
        float y0 = x0 - x1 + R * y1;
        x1 = x0;
        y1 = y0;
        return y0;
    }
};
```

### Recommended Cutoff Frequencies

| Application | Cutoff | Notes |
|-------------|--------|-------|
| General distortion | 10-20 Hz | Preserves low bass |
| Guitar effects | 20-30 Hz | Standard for pedals |
| Subtle saturation | 5-10 Hz | Minimal bass impact |
| In feedback loops | 30-50 Hz | Prevents DC accumulation |

### Limit Cycle Prevention

Simple IIR DC blockers can create their own DC offset due to limit cycles in fixed-point math:

```cpp
// DC blocker with noise shaping to prevent limit cycles
class DCBlockerNoiseShape {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float e1 = 0.0f;  // Quantization error
    float R;
    
public:
    float process(float x0) {
        float y0 = x0 - x1 + R * y1;
        
        // Noise shaping: add previous quantization error
        y0 += e1;
        float y0_quant = quantize(y0);  // Your quantization function
        e1 = y0 - y0_quant;
        
        x1 = x0;
        y1 = y0_quant;
        return y0_quant;
    }
};
```

### Placement in Signal Chain

```
Input → [Asymmetric Distortion] → [DC Blocker] → Output
                                        ↑
                              Place AFTER any DC-generating process
```

For cascaded distortion stages, you may need DC blocking after each stage.

**Sources:**
- [KVR - Most Musical High-Pass Corner Frequency](https://www.kvraudio.com/forum/viewtopic.php?t=535852)
- [Musicdsp.org - DC Filter](https://www.musicdsp.org/en/latest/Filters/135-dc-filter.html)
- [Peabody - DC Blocking Filter](https://peabody.sapp.org/class/dmp2/lab/dcblock/)

---

## Practical Implementation Guidelines

### Signal Chain Order

```
Input
  ↓
[Input Gain/Drive] → Determines distortion amount
  ↓
[Pre-EQ] → Shape what frequencies get distorted
  ↓
[Oversampling Upsample] → If using oversampling
  ↓
[Nonlinearity/Distortion] → Core distortion algorithm
  ↓
[Oversampling Downsample] → If using oversampling
  ↓
[DC Blocker] → Remove DC offset
  ↓
[Post-EQ/Tone] → Shape output character
  ↓
[Output Level] → Compensate for level changes
  ↓
Output
```

### CPU Budget Considerations

| Component | Relative CPU | Notes |
|-----------|-------------|-------|
| Simple waveshaping (tanh) | 1x | Baseline |
| Tanh + 4x oversampling | 4-5x | Good quality/CPU trade-off |
| ADAA 1st order | 1.5x | Good alternative to oversampling |
| Tube model (WDF) | 5-10x | Complex but accurate |
| Neural network (small) | 10-50x | Depends on architecture |
| Tape hysteresis (RK4) | 3-5x | State-space solving |

### Quality Checklist

- [ ] **Anti-aliasing**: Using oversampling, ADAA, or polyBLAMP
- [ ] **DC blocking**: Applied after asymmetric processing
- [ ] **No denormals**: Flush-to-zero enabled for performance
- [ ] **Smooth parameter changes**: Interpolate to avoid clicks
- [ ] **Gain staging**: Appropriate input/output levels
- [ ] **Oversampling filter quality**: Good stopband rejection

### Common Pitfalls

1. **Forgetting DC blocking** after asymmetric distortion
2. **Insufficient oversampling** for high-gain distortion
3. **Denormal numbers** causing CPU spikes (use FTZ/DAZ)
4. **Parameter zipper noise** when not smoothing
5. **Phase issues** with pre/post EQ and oversampling filters

### Testing Recommendations

1. **Sine sweep test**: Check for aliasing artifacts
2. **Null test**: Compare with reference (if modeling real hardware)
3. **Transient response**: Test with drum hits
4. **DC offset**: Verify DC blocker effectiveness
5. **CPU profiling**: Measure under various conditions

---

## References and Further Reading

### Academic Papers

- Parker, J., Zavalishin, V., Le Bivic, E. (2016). "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution." DAFx-16.
- Bilbao, S., Esqueda, F., Parker, J., Välimäki, V. (2017). "Antiderivative Antialiasing for Memoryless Nonlinearities." IEEE Signal Processing Letters.
- Chowdhury, J. (2019). "Real-time Physical Modelling for Analog Tape Machines." DAFx-19.
- Wright, A., et al. (2020). "Real-Time Guitar Amplifier Emulation with Deep Learning." Applied Sciences.

### Online Resources

- [Musicdsp.org](https://www.musicdsp.org/) - DSP code snippets
- [KVR DSP Forum](https://www.kvraudio.com/forum/viewforum.php?f=33) - Active community
- [CCRMA](https://ccrma.stanford.edu/) - Stanford research
- [DAFx Paper Archive](https://www.dafx.de/) - Conference papers

### Books

- Zölzer, U. (Ed.). "DAFX: Digital Audio Effects" (2nd Edition)
- Pirkle, W. "Designing Audio Effect Plugins in C++"
- Smith, J.O. "Physical Audio Signal Processing"

---

*Last updated: January 2026*
