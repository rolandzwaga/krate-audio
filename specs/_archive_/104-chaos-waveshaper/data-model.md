# Data Model: Chaos Attractor Waveshaper

**Feature**: 104-chaos-waveshaper
**Date**: 2026-01-26

## Overview

This document defines the data structures and class design for the ChaosWaveshaper Layer 1 primitive.

---

## 1. Enumeration: ChaosModel

```cpp
/// @brief Available chaos attractor models.
///
/// Each model has distinct mathematical character:
/// - Lorenz: Classic 3D continuous attractor with swirling, unpredictable behavior
/// - Rossler: Smoother 3D continuous attractor with spiraling patterns
/// - Chua: Double-scroll circuit attractor with bi-modal jumps
/// - Henon: 2D discrete map with sharp, rhythmic transitions
enum class ChaosModel : uint8_t {
    Lorenz = 0,   ///< Lorenz system (sigma=10, rho=28, beta=8/3)
    Rossler = 1,  ///< Rossler system (a=0.2, b=0.2, c=5.7)
    Chua = 2,     ///< Chua circuit (alpha=15.6, beta=28, m0=-1.143, m1=-0.714)
    Henon = 3     ///< Henon map (a=1.4, b=0.3)
};
```

---

## 2. Internal State Structure

```cpp
/// @brief Internal attractor state (not exposed publicly).
struct AttractorState {
    float x = 0.0f;  ///< X state variable (primary output for drive modulation)
    float y = 0.0f;  ///< Y state variable
    float z = 0.0f;  ///< Z state variable (unused for Henon)
};
```

---

## 3. Main Class: ChaosWaveshaper

### Class Declaration

```cpp
/// @brief Time-varying waveshaping using chaos attractor dynamics.
///
/// The attractor's normalized X component modulates the drive of a tanh-based
/// soft-clipper, producing distortion that evolves over time without external
/// modulation. Four chaos models provide different characters.
///
/// @par Features
/// - 4 chaos models: Lorenz, Rossler, Chua, Henon
/// - ChaosAmount parameter for dry/wet mixing
/// - AttractorSpeed for evolution rate control
/// - InputCoupling for signal-reactive behavior
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 1 (depends only on Layer 0)
/// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
/// - Principle XI: Performance Budget (< 0.1% CPU per instance)
///
/// @par Usage Example
/// @code
/// ChaosWaveshaper shaper;
/// shaper.prepare(44100.0);
/// shaper.setModel(ChaosModel::Lorenz);
/// shaper.setChaosAmount(0.5f);
/// shaper.setAttractorSpeed(1.0f);
/// shaper.setInputCoupling(0.3f);
///
/// float output = shaper.process(input);
/// @endcode
///
/// @see specs/104-chaos-waveshaper/spec.md
class ChaosWaveshaper {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinChaosAmount = 0.0f;
    static constexpr float kMaxChaosAmount = 1.0f;
    static constexpr float kDefaultChaosAmount = 0.5f;

    static constexpr float kMinAttractorSpeed = 0.01f;
    static constexpr float kMaxAttractorSpeed = 100.0f;
    static constexpr float kDefaultAttractorSpeed = 1.0f;

    static constexpr float kMinInputCoupling = 0.0f;
    static constexpr float kMaxInputCoupling = 1.0f;
    static constexpr float kDefaultInputCoupling = 0.0f;

    static constexpr float kMinDrive = 0.5f;   ///< Minimum waveshaping drive
    static constexpr float kMaxDrive = 4.0f;   ///< Maximum waveshaping drive

    static constexpr size_t kControlRateInterval = 32;  ///< Samples between attractor updates

    // =========================================================================
    // Lifecycle
    // =========================================================================

    ChaosWaveshaper() noexcept = default;
    ~ChaosWaveshaper() = default;

    // Copyable and movable (simple state)
    ChaosWaveshaper(const ChaosWaveshaper&) = default;
    ChaosWaveshaper& operator=(const ChaosWaveshaper&) = default;
    ChaosWaveshaper(ChaosWaveshaper&&) noexcept = default;
    ChaosWaveshaper& operator=(ChaosWaveshaper&&) noexcept = default;

    // =========================================================================
    // Initialization (FR-001, FR-002)
    // =========================================================================

    /// @brief Prepare for processing at given sample rate.
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @pre sampleRate >= 1000.0
    /// @post Attractor initialized to stable starting conditions
    /// @note NOT real-time safe (initializes state)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset attractor to stable initial conditions.
    /// @post State variables reset per current model
    /// @post Configuration (model, parameters) preserved
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (FR-009 to FR-012)
    // =========================================================================

    /// @brief Set the chaos attractor model.
    /// @param model Chaos algorithm (Lorenz, Rossler, Chua, Henon)
    /// @note Model change takes effect immediately; state reset recommended
    void setModel(ChaosModel model) noexcept;

    /// @brief Set the chaos amount (dry/wet mix).
    /// @param amount Amount [0.0, 1.0] where 0=bypass, 1=full chaos
    void setChaosAmount(float amount) noexcept;

    /// @brief Set the attractor evolution speed.
    /// @param speed Speed [0.01, 100.0] where 1.0=nominal rate
    void setAttractorSpeed(float speed) noexcept;

    /// @brief Set the input coupling amount.
    /// @param coupling Coupling [0.0, 1.0] where 0=no coupling, 1=full coupling
    void setInputCoupling(float coupling) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    [[nodiscard]] ChaosModel getModel() const noexcept;
    [[nodiscard]] float getChaosAmount() const noexcept;
    [[nodiscard]] float getAttractorSpeed() const noexcept;
    [[nodiscard]] float getInputCoupling() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] double getSampleRate() const noexcept;

    // =========================================================================
    // Processing (FR-003, FR-004)
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Input sample (assumed normalized to [-1, 1])
    /// @return Chaos-modulated waveshaped output
    /// @note Real-time safe (noexcept, no allocations)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    /// @note Real-time safe (noexcept, no allocations)
    void processBlock(float* buffer, size_t numSamples) noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    void updateAttractor(float inputEnvelope) noexcept;
    void updateLorenz() noexcept;
    void updateRossler() noexcept;
    void updateChua() noexcept;
    void updateHenon() noexcept;

    void resetModelState() noexcept;
    void checkAndResetIfDiverged() noexcept;

    [[nodiscard]] float sanitizeInput(float x) const noexcept;
    [[nodiscard]] float applyWaveshaping(float input) const noexcept;

    // Chua diode function
    [[nodiscard]] float chuaDiode(float x) const noexcept;

    // =========================================================================
    // State
    // =========================================================================

    AttractorState state_{};
    float normalizedX_ = 0.0f;      ///< Normalized attractor X output [-1, 1]
    float prevHenonX_ = 0.0f;       ///< Previous Henon X for interpolation
    float henonPhase_ = 0.0f;       ///< Fractional phase for Henon interpolation

    // =========================================================================
    // Configuration
    // =========================================================================

    ChaosModel model_ = ChaosModel::Lorenz;
    double sampleRate_ = 44100.0;
    float chaosAmount_ = kDefaultChaosAmount;
    float attractorSpeed_ = kDefaultAttractorSpeed;
    float inputCoupling_ = kDefaultInputCoupling;

    // =========================================================================
    // Per-Model Parameters (cached based on model_)
    // =========================================================================

    float baseDt_ = 0.005f;              ///< Base integration timestep
    float safeBound_ = 50.0f;            ///< State variable bound for divergence check
    float normalizationFactor_ = 20.0f;  ///< Scale X to [-1, 1]
    float perturbationScale_ = 0.1f;     ///< Input coupling scale

    // =========================================================================
    // Control Rate
    // =========================================================================

    int samplesUntilUpdate_ = 0;
    float inputEnvelopeAccum_ = 0.0f;    ///< Accumulated input envelope for coupling
    int envelopeSampleCount_ = 0;        ///< Samples in accumulator

    bool prepared_ = false;
};
```

---

## 4. Per-Model Parameters

| Model | baseDt | safeBound | normFactor | perturbScale |
|-------|--------|-----------|------------|--------------|
| Lorenz | 0.005 | 50.0 | 20.0 | 0.1 |
| Rossler | 0.02 | 20.0 | 10.0 | 0.1 |
| Chua | 0.01 | 10.0 | 5.0 | 0.08 |
| Henon | 1.0 | 5.0 | 1.5 | 0.05 |

These are set when `setModel()` is called.

---

## 5. Initial Conditions (per model)

| Model | x | y | z |
|-------|---|---|---|
| Lorenz | 1.0 | 0.0 | 0.0 |
| Rossler | 0.1 | 0.0 | 0.0 |
| Chua | 0.1 | 0.0 | 0.0 |
| Henon | 0.0 | 0.0 | N/A |

---

## 6. Relationships

```
ChaosWaveshaper
    |
    +-- uses --> ChaosModel (enum)
    |
    +-- contains --> AttractorState (internal struct)
    |
    +-- depends on --> Layer 0
    |       |
    |       +-- detail::flushDenormal()
    |       +-- detail::isNaN()
    |       +-- detail::isInf()
    |       +-- Sigmoid::tanhVariable()
```

---

## 7. State Transitions

```
[Unprepared] --prepare()--> [Prepared/Idle]
     ^                            |
     |                            v
  reset()                  [Processing]
     |                            |
     +----------------------------+
```

Processing state is stateless in terms of allocation - all state is preallocated.

---

## 8. Memory Layout

```cpp
// Approximate sizes (64-bit system)
struct ChaosWaveshaper {
    // State (12 bytes)
    AttractorState state_;      // 12 bytes (3 floats)
    float normalizedX_;         // 4 bytes
    float prevHenonX_;          // 4 bytes
    float henonPhase_;          // 4 bytes

    // Configuration (28 bytes)
    ChaosModel model_;          // 1 byte + padding
    double sampleRate_;         // 8 bytes
    float chaosAmount_;         // 4 bytes
    float attractorSpeed_;      // 4 bytes
    float inputCoupling_;       // 4 bytes

    // Per-model params (16 bytes)
    float baseDt_;              // 4 bytes
    float safeBound_;           // 4 bytes
    float normalizationFactor_; // 4 bytes
    float perturbationScale_;   // 4 bytes

    // Control rate (12 bytes)
    int samplesUntilUpdate_;    // 4 bytes
    float inputEnvelopeAccum_;  // 4 bytes
    int envelopeSampleCount_;   // 4 bytes
    bool prepared_;             // 1 byte + padding
};
// Total: ~80-96 bytes (with padding)
```

No dynamic allocation. Entire class fits in cache line (typically 64 bytes) or two.
