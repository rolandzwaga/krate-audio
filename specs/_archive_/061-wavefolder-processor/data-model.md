# Data Model: WavefolderProcessor

**Feature**: 061-wavefolder-processor
**Date**: 2026-01-14

## Enumerations

### WavefolderModel (FR-001, FR-002)

```cpp
/// @brief Available wavefolder model types.
///
/// Each model has distinct harmonic characteristics:
/// - Simple: Dense odd harmonics, smooth rolloff (triangle fold)
/// - Serge: FM-like sparse spectrum (sine fold)
/// - Buchla259: Parallel stages, rich timbre
/// - Lockhart: Even/odd harmonics with spectral nulls (Lambert-W)
enum class WavefolderModel : uint8_t {
    Simple = 0,    ///< Triangle fold - basic symmetric folding
    Serge = 1,     ///< Sine fold - characteristic Serge wavefolder
    Buchla259 = 2, ///< 5-stage parallel - Buchla 259 style
    Lockhart = 3   ///< Lambert-W based - circuit-derived
};
```

### BuchlaMode (FR-002a)

```cpp
/// @brief Sub-modes for Buchla259 model.
enum class BuchlaMode : uint8_t {
    Classic = 0,   ///< Fixed authentic thresholds/gains
    Custom = 1     ///< User-configurable thresholds/gains
};
```

## WavefolderProcessor Class

### Public Interface

```cpp
class WavefolderProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Minimum fold amount to prevent degeneracy
    static constexpr float kMinFoldAmount = 0.1f;

    /// Maximum fold amount
    static constexpr float kMaxFoldAmount = 10.0f;

    /// Default smoothing time in milliseconds
    static constexpr float kDefaultSmoothingMs = 5.0f;

    /// DC blocker cutoff frequency in Hz
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    /// Number of stages in Buchla259 model
    static constexpr size_t kBuchlaStages = 5;

    // =========================================================================
    // Lifecycle (FR-003, FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Default constructor with safe defaults.
    ///
    /// Initializes with:
    /// - Model: Simple
    /// - foldAmount: 1.0
    /// - symmetry: 0.0 (centered)
    /// - mix: 1.0 (100% wet)
    /// - buchlaMode: Classic
    WavefolderProcessor() noexcept = default;

    /// @brief Configure the processor for the given sample rate.
    ///
    /// Configures internal components (Wavefolder, DCBlocker, smoothers)
    /// for the specified sample rate. Must be called before process().
    ///
    /// @param sampleRate Sample rate in Hz (e.g., 44100.0)
    /// @param maxBlockSize Maximum block size in samples (unused, for future use)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state without reallocation.
    ///
    /// Clears DC blocker state and snaps smoothers to current target values.
    /// Call when starting a new audio stream or after discontinuity.
    void reset() noexcept;

    // =========================================================================
    // Model Selection (FR-007, FR-014, FR-023, FR-023a)
    // =========================================================================

    /// @brief Set the wavefolder model.
    ///
    /// @param model WavefolderModel to use
    /// @note Change is immediate (no smoothing)
    void setModel(WavefolderModel model) noexcept;

    /// @brief Get the current wavefolder model.
    [[nodiscard]] WavefolderModel getModel() const noexcept;

    /// @brief Set the Buchla259 sub-mode.
    ///
    /// @param mode BuchlaMode (Classic or Custom)
    /// @note Only affects processing when model == Buchla259
    void setBuchlaMode(BuchlaMode mode) noexcept;

    /// @brief Get the current Buchla259 sub-mode.
    [[nodiscard]] BuchlaMode getBuchlaMode() const noexcept;

    // =========================================================================
    // Buchla259 Custom Configuration (FR-022b, FR-022c)
    // =========================================================================

    /// @brief Set custom thresholds for Buchla259 Custom mode.
    ///
    /// @param thresholds Array of 5 threshold values
    /// @note Only affects processing when buchlaMode == Custom
    void setBuchlaThresholds(const std::array<float, kBuchlaStages>& thresholds) noexcept;

    /// @brief Set custom gains for Buchla259 Custom mode.
    ///
    /// @param gains Array of 5 gain values
    /// @note Only affects processing when buchlaMode == Custom
    void setBuchlaGains(const std::array<float, kBuchlaStages>& gains) noexcept;

    // =========================================================================
    // Parameter Setters (FR-008, FR-009, FR-010, FR-011, FR-012, FR-013)
    // =========================================================================

    /// @brief Set the fold amount (intensity).
    ///
    /// Controls how aggressively the signal is folded.
    /// Value is clamped to [0.1, 10.0].
    ///
    /// @param amount Fold amount
    void setFoldAmount(float amount) noexcept;

    /// @brief Set the symmetry (asymmetric folding amount).
    ///
    /// Controls even harmonic content.
    /// - 0.0: Symmetric folding (odd harmonics only)
    /// - +/-1.0: Maximum asymmetry (even harmonics added)
    /// Value is clamped to [-1.0, +1.0].
    ///
    /// @param symmetry Symmetry value [-1.0, +1.0]
    void setSymmetry(float symmetry) noexcept;

    /// @brief Set the dry/wet mix.
    ///
    /// - 0.0: Full bypass (output equals input)
    /// - 1.0: 100% folded signal
    /// Value is clamped to [0.0, 1.0].
    ///
    /// @param mix Mix amount [0.0, 1.0]
    void setMix(float mix) noexcept;

    // =========================================================================
    // Parameter Getters (FR-015, FR-016, FR-017)
    // =========================================================================

    /// @brief Get the current fold amount.
    [[nodiscard]] float getFoldAmount() const noexcept;

    /// @brief Get the current symmetry value.
    [[nodiscard]] float getSymmetry() const noexcept;

    /// @brief Get the current mix value.
    [[nodiscard]] float getMix() const noexcept;

    // =========================================================================
    // Processing (FR-024, FR-025, FR-026, FR-027, FR-028)
    // =========================================================================

    /// @brief Process a block of audio samples in-place.
    ///
    /// Applies the wavefolder effect with the current parameter settings.
    /// Signal chain: symmetry offset -> wavefolder -> DC blocker -> mix blend
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    ///
    /// @note No memory allocation occurs during this call (FR-026)
    /// @note n=0 is handled gracefully (FR-027)
    /// @note mix=0 produces exact input (FR-028)
    void process(float* buffer, size_t numSamples) noexcept;

private:
    // ... implementation details
};
```

### Private Members

```cpp
private:
    // =========================================================================
    // Parameters (stored in user units)
    // =========================================================================

    WavefolderModel model_ = WavefolderModel::Simple;
    BuchlaMode buchlaMode_ = BuchlaMode::Classic;
    float foldAmount_ = 1.0f;       ///< Fold intensity [0.1, 10.0]
    float symmetry_ = 0.0f;         ///< Asymmetry [-1.0, +1.0]
    float mix_ = 1.0f;              ///< Dry/wet [0.0, 1.0]

    // =========================================================================
    // Buchla259 Configuration
    // =========================================================================

    /// Classic thresholds per FR-022a
    std::array<float, kBuchlaStages> buchlaThresholds_ = {
        0.2f, 0.4f, 0.6f, 0.8f, 1.0f
    };

    /// Classic gains per FR-022a
    std::array<float, kBuchlaStages> buchlaGains_ = {
        1.0f, 0.8f, 0.6f, 0.4f, 0.2f
    };

    // =========================================================================
    // Parameter Smoothers (FR-029, FR-030, FR-031)
    // =========================================================================

    OnePoleSmoother foldAmountSmoother_;
    OnePoleSmoother symmetrySmoother_;
    OnePoleSmoother mixSmoother_;

    // =========================================================================
    // DSP Components (FR-037, FR-038)
    // =========================================================================

    Wavefolder wavefolder_;   ///< For Simple, Serge, Lockhart models
    DCBlocker dcBlocker_;     ///< DC offset removal after folding

    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Apply Buchla259 5-stage parallel folding.
    ///
    /// @param input Input sample (with symmetry offset already applied)
    /// @param foldAmount Current smoothed fold amount
    /// @return Folded output (sum of all stages, normalized)
    [[nodiscard]] float applyBuchla259(float input, float foldAmount) const noexcept;
```

## Entity Relationships

```
WavefolderProcessor
    |
    +-- WavefolderModel (enum)
    |       |-- Simple    -> Wavefolder(Triangle)
    |       |-- Serge     -> Wavefolder(Sine)
    |       |-- Buchla259 -> custom 5-stage
    |       \-- Lockhart  -> Wavefolder(Lockhart)
    |
    +-- BuchlaMode (enum)
    |       |-- Classic   -> fixed thresholds/gains
    |       \-- Custom    -> user thresholds/gains
    |
    +-- Parameters
    |       |-- foldAmount [0.1, 10.0]
    |       |-- symmetry [-1.0, +1.0]
    |       \-- mix [0.0, 1.0]
    |
    +-- Smoothers (OnePoleSmoother x3)
    |       |-- foldAmountSmoother_
    |       |-- symmetrySmoother_
    |       \-- mixSmoother_
    |
    +-- DSP Components
            |-- wavefolder_ (Wavefolder, Layer 1)
            \-- dcBlocker_ (DCBlocker, Layer 1)
```

## Validation Rules

| Parameter | Range | Clamping |
|-----------|-------|----------|
| foldAmount | [0.1, 10.0] | std::clamp |
| symmetry | [-1.0, +1.0] | std::clamp |
| mix | [0.0, 1.0] | std::clamp |
| buchlaThresholds | any float | stored as-is |
| buchlaGains | any float | stored as-is |

## State Transitions

```
Unprepared State:
    - process() returns input unchanged (FR-005)
    - All setters work normally (store values)

Prepared State (after prepare()):
    - process() applies full signal chain
    - reset() snaps smoothers, clears DC blocker
    - Can call prepare() again with new sample rate

Model Changes:
    - Immediate effect (FR-032)
    - No smoothing on model switch
    - User should use mix for crossfading if needed
```
