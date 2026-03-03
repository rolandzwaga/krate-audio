# Data Model: Chaos Attractor Oscillator

**Feature**: 026-chaos-attractor-oscillator
**Date**: 2026-02-05
**Status**: Complete

## Overview

This document defines the data model for the audio-rate chaos oscillator including enumerations, state structures, and configuration constants.

---

## Enumerations

### ChaosAttractor (FR-001 to FR-005)

```cpp
/// @brief Available chaos attractor models for audio-rate oscillation.
///
/// Each attractor has distinct mathematical character and timbral qualities:
/// - Lorenz: Smooth, flowing, three-lobe butterfly pattern
/// - Rossler: Asymmetric, single spiral, buzzy
/// - Chua: Harsh double-scroll with abrupt transitions
/// - Duffing: Driven nonlinear, harmonically rich
/// - VanDerPol: Relaxation oscillations, pulse-like
///
/// @note Distinct from ChaosModel enum (which includes Henon and excludes Duffing/VanDerPol)
enum class ChaosAttractor : uint8_t {
    Lorenz = 0,    ///< Lorenz attractor (sigma=10, rho=28, beta=8/3)
    Rossler = 1,   ///< Rossler attractor (a=0.2, b=0.2, c=5.7)
    Chua = 2,      ///< Chua circuit (alpha=15.6, beta=28, m0=-1.143, m1=-0.714)
    Duffing = 3,   ///< Duffing oscillator (gamma=0.1, A=0.35, omega=1.4)
    VanDerPol = 4  ///< Van der Pol oscillator (mu=1.0)
};

/// @brief Number of attractor types.
inline constexpr size_t kNumChaosAttractors = 5;
```

---

## Structures

### AttractorState (Internal)

```cpp
/// @brief Internal state variables for attractor dynamics.
///
/// For 3D attractors (Lorenz, Rossler, Chua):
///   - x, y, z represent the three state variables
///
/// For 2D oscillators (Duffing, VanDerPol):
///   - x represents position
///   - y represents velocity (v)
///   - z is unused (kept at 0.0f)
struct AttractorState {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
```

### AttractorConstants (Internal)

```cpp
/// @brief Per-attractor configuration constants.
///
/// These values are empirically tuned for audio-rate operation and are
/// stored in a constexpr array indexed by ChaosAttractor enum value.
struct AttractorConstants {
    // Numerical integration
    float dtMax;              ///< Maximum stable dt per RK4 substep
    float baseDt;             ///< Base dt for frequency scaling
    float referenceFrequency; ///< Reference frequency for dt scaling

    // Safety
    float safeBound;          ///< State bound for divergence detection

    // Normalization
    float xScale;             ///< Normalization divisor for x axis
    float yScale;             ///< Normalization divisor for y axis
    float zScale;             ///< Normalization divisor for z axis

    // Chaos parameter mapping
    float chaosMin;           ///< Minimum chaos parameter value
    float chaosMax;           ///< Maximum chaos parameter value
    float chaosDefault;       ///< Default chaos parameter value

    // Initial conditions
    AttractorState initialState; ///< Reset state for this attractor
};
```

---

## Constants Tables

### Per-Attractor Constants (from spec)

| Attractor | dtMax | baseDt | refFreq | safeBound | xScale | yScale | zScale |
|-----------|-------|--------|---------|-----------|--------|--------|--------|
| Lorenz | 0.001 | 0.01 | 100.0 | 500 | 20 | 20 | 30 |
| Rossler | 0.002 | 0.05 | 80.0 | 300 | 12 | 12 | 20 |
| Chua | 0.0005 | 0.02 | 120.0 | 50 | 2.5 | 1.5 | 1.5 |
| Duffing | 0.001 | 1.4 | 1.0 | 10 | 2 | 2 | N/A |
| VanDerPol | 0.001 | 1.0 | 1.0 | 10 | 2.5 | varies | N/A |

### Chaos Parameter Mapping (FR-019)

| Attractor | Parameter | Min | Max | Default |
|-----------|-----------|-----|-----|---------|
| Lorenz | rho | 20 | 28 | 28 |
| Rossler | c | 4 | 8 | 5.7 |
| Chua | alpha | 12 | 18 | 15.6 |
| Duffing | A (amplitude) | 0.2 | 0.5 | 0.35 |
| VanDerPol | mu | 0.5 | 5 | 1.0 |

### Initial States (FR-012)

| Attractor | x | y | z (or v) |
|-----------|---|---|----------|
| Lorenz | 1.0 | 1.0 | 1.0 |
| Rossler | 0.1 | 0.0 | 0.0 |
| Chua | 0.7 | 0.0 | 0.0 |
| Duffing | 0.5 | 0.0 | (phase=0) |
| VanDerPol | 0.5 | 0.0 | N/A |

---

## Class Definition (Overview)

```cpp
/// @brief Audio-rate chaos oscillator implementing 5 attractor types.
///
/// Generates complex, evolving waveforms by numerically integrating
/// chaotic attractor systems at audio rate using RK4 with adaptive
/// substepping for numerical stability.
///
/// @par Layer: 2 (processors/)
/// @par Dependencies: Layer 0 (fast_math.h, db_utils.h, math_constants.h),
///                    Layer 1 (dc_blocker.h)
///
/// @par Memory Model
/// All state is pre-allocated. No heap allocation during processing.
///
/// @par Thread Safety
/// Single-threaded. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// - prepare(): NOT real-time safe (prepares DC blocker)
/// - All other methods: Real-time safe (noexcept, no allocations)
class ChaosOscillator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxSubsteps = 100;
    static constexpr size_t kResetCooldownSamples = 100;
    static constexpr float kMinFrequency = 0.1f;
    static constexpr float kMaxFrequency = 20000.0f;
    static constexpr float kDefaultDCBlockerCutoff = 10.0f;

    // =========================================================================
    // Lifecycle (FR-015, FR-016)
    // =========================================================================

    ChaosOscillator() noexcept;
    ~ChaosOscillator() = default;

    // Non-copyable (contains DCBlocker)
    ChaosOscillator(const ChaosOscillator&) = delete;
    ChaosOscillator& operator=(const ChaosOscillator&) = delete;
    ChaosOscillator(ChaosOscillator&&) noexcept = default;
    ChaosOscillator& operator=(ChaosOscillator&&) noexcept = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-017 to FR-021)
    // =========================================================================

    void setAttractor(ChaosAttractor type) noexcept;
    void setFrequency(float hz) noexcept;
    void setChaos(float amount) noexcept;       // [0, 1] normalized
    void setCoupling(float amount) noexcept;    // [0, 1]
    void setOutput(size_t axis) noexcept;       // 0=x, 1=y, 2=z

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    [[nodiscard]] ChaosAttractor getAttractor() const noexcept;
    [[nodiscard]] float getFrequency() const noexcept;
    [[nodiscard]] float getChaos() const noexcept;
    [[nodiscard]] float getCoupling() const noexcept;
    [[nodiscard]] size_t getOutput() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Processing (FR-022, FR-023)
    // =========================================================================

    [[nodiscard]] float process(float externalInput = 0.0f) noexcept;
    void processBlock(float* output, size_t numSamples,
                      const float* extInput = nullptr) noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    void updateConstants() noexcept;
    void resetState() noexcept;

    // RK4 integration (FR-006)
    void integrateOneStep(float externalInput) noexcept;
    void rk4Step(float dt, float coupling) noexcept;

    // Per-attractor derivatives (FR-001 to FR-005)
    [[nodiscard]] AttractorState computeLorenzDerivatives(
        const AttractorState& s, float coupling) const noexcept;
    [[nodiscard]] AttractorState computeRosslerDerivatives(
        const AttractorState& s, float coupling) const noexcept;
    [[nodiscard]] AttractorState computeChuaDerivatives(
        const AttractorState& s, float coupling) const noexcept;
    [[nodiscard]] AttractorState computeDuffingDerivatives(
        const AttractorState& s, float coupling) const noexcept;
    [[nodiscard]] AttractorState computeVanDerPolDerivatives(
        const AttractorState& s, float coupling) const noexcept;

    // Chua diode nonlinearity (FR-003)
    [[nodiscard]] static float chuaDiode(float x) noexcept;

    // Safety (FR-011, FR-012, FR-013, FR-014)
    [[nodiscard]] bool checkDivergence() const noexcept;
    [[nodiscard]] float sanitizeInput(float input) const noexcept;

    // Output processing (FR-008, FR-009, FR-010)
    [[nodiscard]] float getAxisValue() const noexcept;
    [[nodiscard]] float normalizeOutput(float value) const noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    ChaosAttractor attractor_ = ChaosAttractor::Lorenz;
    float frequency_ = 220.0f;
    float chaosNormalized_ = 1.0f;  // [0, 1]
    float coupling_ = 0.0f;
    size_t outputAxis_ = 0;

    // Computed parameters (from configuration)
    float chaosParameter_ = 28.0f;  // Actual parameter value (e.g., rho)
    float dt_ = 0.0f;               // Integration timestep per sample
    float dtMax_ = 0.001f;          // Maximum stable substep dt
    float safeBound_ = 500.0f;      // Divergence threshold
    float xScale_ = 20.0f;          // Output normalization
    float yScale_ = 20.0f;
    float zScale_ = 30.0f;

    // State
    AttractorState state_;
    float duffingPhase_ = 0.0f;     // Duffing driving term phase
    size_t resetCooldown_ = 0;      // Samples until next reset allowed
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // DC Blocker (FR-009)
    DCBlocker dcBlocker_;
};
```

---

## Validation Rules

### Input Validation

| Parameter | Valid Range | Behavior if Invalid |
|-----------|-------------|---------------------|
| frequency | [0.1, sampleRate/2) | Clamp to range |
| chaos | [0.0, 1.0] | Clamp to range |
| coupling | [0.0, 1.0] | Clamp to range |
| axis | {0, 1, 2} | Clamp to 0-2 |
| attractor | {0, 1, 2, 3, 4} | Default to Lorenz if invalid |
| externalInput | any float | NaN treated as 0.0 (FR-014) |

### State Validation

| Condition | Detection | Action |
|-----------|-----------|--------|
| x > safeBound | Per-sample check | Reset to initial state |
| y > safeBound | Per-sample check | Reset to initial state |
| z > safeBound | Per-sample check | Reset to initial state |
| NaN in state | Per-sample check | Reset to initial state |
| Inf in state | Per-sample check | Reset to initial state |

---

## State Transitions

### Attractor Change

```
setAttractor(new_type)
    -> Update all constants from kAttractorConstants[new_type]
    -> Reset state to initialState
    -> Reset Duffing phase if applicable
    -> Reset cooldown counter
```

### Divergence Recovery

```
checkDivergence() returns true
    -> if resetCooldown_ == 0:
        -> resetState()
        -> resetCooldown_ = kResetCooldownSamples
    -> else:
        -> Continue (cooldown prevents rapid cycling)
```

### Processing Pipeline

```
process(externalInput)
    -> sanitizeInput(externalInput)
    -> integrateOneStep(sanitizedInput)
    -> checkDivergence() -> if true, resetState()
    -> value = getAxisValue()
    -> normalized = normalizeOutput(value)
    -> dcBlocked = dcBlocker_.process(normalized)
    -> return dcBlocked
```
