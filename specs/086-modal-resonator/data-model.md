# Data Model: Modal Resonator

**Feature**: 086-modal-resonator
**Date**: 2026-01-23
**Layer**: 2 (DSP Processor)

## Entity Definitions

### 1. ModalData

Data transfer structure for bulk mode configuration.

```cpp
/// @brief Mode configuration data for bulk import.
/// @note Used with setModes() to configure multiple modes at once.
struct ModalData {
    float frequency;   ///< Mode frequency in Hz [20, sampleRate * 0.45]
    float t60;         ///< Decay time in seconds (RT60) [0.001, 30.0]
    float amplitude;   ///< Mode amplitude [0.0, 1.0]
};
```

**Validation Rules**:
| Field | Valid Range | Clamping |
|-------|-------------|----------|
| frequency | [20 Hz, sampleRate * 0.45] | Yes |
| t60 | [0.001 s, 30.0 s] | Yes |
| amplitude | [0.0, 1.0] | Yes |

---

### 2. Material

Enumeration of material presets.

```cpp
/// @brief Material presets for frequency-dependent decay.
/// @note Each material has characteristic b_1 and b_3 coefficients.
enum class Material : uint8_t {
    Wood,     ///< Warm, quick HF decay (marimba-like)
    Metal,    ///< Bright, sustained (bell-like)
    Glass,    ///< Bright, ringing (glass bowl-like)
    Ceramic,  ///< Warm/bright, medium decay (tile-like)
    Nylon     ///< Dull, heavily damped (damped string-like)
};
```

---

### 3. MaterialCoefficients

Internal structure holding material-specific parameters.

```cpp
/// @brief Coefficients for frequency-dependent decay model.
/// @note R_k = b1 + b3 * f_k^2, T60_k = 6.91 / R_k
struct MaterialCoefficients {
    float b1;                           ///< Global damping (Hz)
    float b3;                           ///< Frequency-dependent damping (s)
    std::array<float, 8> ratios;        ///< Mode frequency multipliers
    int numModes;                       ///< Number of active modes for preset
};
```

**Preset Values**:

```cpp
inline constexpr MaterialCoefficients kMaterialPresets[] = {
    // Wood: warm, quick HF decay
    { 2.0f, 1.0e-7f, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}, 8 },
    // Metal: bright, sustained
    { 0.3f, 1.0e-9f, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}, 8 },
    // Glass: bright, ringing
    { 0.5f, 5.0e-8f, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}, 8 },
    // Ceramic: warm/bright, medium
    { 1.5f, 8.0e-8f, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}, 8 },
    // Nylon: dull, heavily damped
    { 4.0f, 2.0e-7f, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}, 8 }
};
```

---

### 4. ModeState

Internal structure holding per-mode oscillator state and coefficients.

```cpp
/// @brief Internal mode state (not exposed in public API).
/// @note State arrays use Structure of Arrays (SoA) for potential SIMD.
struct ModeState {
    // Oscillator state
    float y1;       ///< y[n-1] delay state
    float y2;       ///< y[n-2] delay state

    // Coefficients (updated when parameters change)
    float a1;       ///< 2 * R * cos(theta)
    float a2;       ///< R * R

    // Parameters (targets)
    float frequency;    ///< Mode frequency in Hz
    float t60;          ///< Decay time in seconds
    float amplitude;    ///< Mode amplitude [0, 1]

    // Status
    bool enabled;       ///< Mode active flag
};
```

---

## Constants

```cpp
namespace Krate::DSP {

/// Maximum number of modes in the resonator
inline constexpr size_t kMaxModes = 32;

/// Minimum mode frequency in Hz
inline constexpr float kMinModeFrequency = 20.0f;

/// Maximum mode frequency ratio (relative to sample rate)
inline constexpr float kMaxModeFrequencyRatio = 0.45f;

/// Minimum decay time in seconds
inline constexpr float kMinModeDecay = 0.001f;

/// Maximum decay time in seconds
inline constexpr float kMaxModeDecay = 30.0f;

/// Default decay time in seconds
inline constexpr float kDefaultModeDecay = 1.0f;

/// Minimum size scaling factor
inline constexpr float kMinSizeScale = 0.1f;

/// Maximum size scaling factor
inline constexpr float kMaxSizeScale = 10.0f;

/// Base frequency for material presets (A4)
inline constexpr float kBaseFrequency = 440.0f;

/// Default parameter smoothing time in milliseconds
inline constexpr float kDefaultSmoothingTimeMs = 20.0f;

/// ln(1000) - used for T60 to time constant conversion
/// T60 = 6.91 * tau where tau is the time constant
inline constexpr float kLn1000 = 6.907755278982137f;

} // namespace Krate::DSP
```

---

## State Diagram

```
                            ┌─────────────┐
                            │ Unprepared  │
                            │  (initial)  │
                            └──────┬──────┘
                                   │
                              prepare()
                                   │
                                   ▼
                            ┌─────────────┐
           ┌───────────────►│  Prepared   │◄───────────────┐
           │                │  (ready)    │                │
           │                └──────┬──────┘                │
           │                       │                       │
       reset()              setMode*()/                reset()
           │                setMaterial()                  │
           │                       │                       │
           │                       ▼                       │
           │                ┌─────────────┐                │
           └────────────────│ Configured  │────────────────┘
                            │  (modes set)│
                            └──────┬──────┘
                                   │
                            strike() or
                            process(input)
                                   │
                                   ▼
                            ┌─────────────┐
                            │  Resonating │◄──────┐
                            │  (active)   │       │
                            └──────┬──────┘       │
                                   │              │
                              process()    strike()/input
                            (until silent)        │
                                   │              │
                                   ▼              │
                            ┌─────────────┐       │
                            │   Silent    │───────┘
                            │(decayed out)│
                            └─────────────┘
```

---

## Relationships

```
┌──────────────────────────────────────────────────────────────────┐
│                       ModalResonator                              │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                 Mode State Arrays (x32)                    │  │
│  │  ┌─────────┬─────────┬─────────┬─────────┬─────────┐      │  │
│  │  │  y1_[]  │  y2_[]  │  a1_[]  │  a2_[]  │ gains_[]│      │  │
│  │  │ float32 │ float32 │ float32 │ float32 │ float32 │      │  │
│  │  └─────────┴─────────┴─────────┴─────────┴─────────┘      │  │
│  │  ┌─────────────┬─────────────┬─────────────────────┐      │  │
│  │  │frequencies_[]│  t60s_[]   │    enabled_[]       │      │  │
│  │  │   float32   │  float32   │       bool          │      │  │
│  │  └─────────────┴─────────────┴─────────────────────┘      │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │              Parameter Smoothers (x64)                     │  │
│  │  ┌──────────────────────┬──────────────────────┐          │  │
│  │  │ frequencySmooth_[32] │ amplitudeSmooth_[32] │          │  │
│  │  │   OnePoleSmoother    │   OnePoleSmoother    │          │  │
│  │  └──────────────────────┴──────────────────────┘          │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                  Global Parameters                         │  │
│  │  ┌──────────┬──────────┬──────────┬──────────────────┐    │  │
│  │  │sampleRate│  size_   │ damping_ │ smoothingTimeMs_ │    │  │
│  │  │ double   │  float   │  float   │      float       │    │  │
│  │  └──────────┴──────────┴──────────┴──────────────────┘    │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                    Static Data                             │  │
│  │  ┌──────────────────────────────────────────────────────┐ │  │
│  │  │  kMaterialPresets[5] : MaterialCoefficients          │ │  │
│  │  │  (Wood, Metal, Glass, Ceramic, Nylon)                │ │  │
│  │  └──────────────────────────────────────────────────────┘ │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘

External Dependencies:
┌─────────────────────────────────────────────────┐
│  Layer 0: Core                                  │
│  - kPi, kTwoPi (math_constants.h)              │
│  - flushDenormal, isNaN, isInf (db_utils.h)   │
└─────────────────────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────────────────┐
│  Layer 1: Primitives                            │
│  - OnePoleSmoother (smoother.h)                │
└─────────────────────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────────────────┐
│  Layer 2: Processors                            │
│  - ModalResonator (modal_resonator.h)          │
└─────────────────────────────────────────────────┘
```

---

## Memory Layout

**Total Memory per Instance** (approximate):

| Component | Size | Count | Total |
|-----------|------|-------|-------|
| y1_ array | 4 bytes | 32 | 128 bytes |
| y2_ array | 4 bytes | 32 | 128 bytes |
| a1_ array | 4 bytes | 32 | 128 bytes |
| a2_ array | 4 bytes | 32 | 128 bytes |
| gains_ array | 4 bytes | 32 | 128 bytes |
| frequencies_ array | 4 bytes | 32 | 128 bytes |
| t60s_ array | 4 bytes | 32 | 128 bytes |
| enabled_ array | 1 byte | 32 | 32 bytes |
| OnePoleSmoother (freq) | ~20 bytes | 32 | 640 bytes |
| OnePoleSmoother (amp) | ~20 bytes | 32 | 640 bytes |
| Global params | ~32 bytes | 1 | 32 bytes |
| **Total** | | | **~2,240 bytes** |

**Alignment**: Arrays should be `alignas(32)` for potential AVX optimization.
