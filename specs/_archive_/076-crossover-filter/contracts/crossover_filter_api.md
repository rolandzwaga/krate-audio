# API Contract: Crossover Filter

**Feature**: 076-crossover-filter | **Layer**: 2 (DSP Processors)

## File Location

```
dsp/include/krate/dsp/processors/crossover_filter.h
```

## Namespace

```cpp
namespace Krate {
namespace DSP {
```

---

## Enumerations

### TrackingMode

```cpp
/// @brief Coefficient recalculation strategy for frequency smoothing.
enum class TrackingMode : uint8_t {
    Efficient,      ///< Recalculate only when frequency changes by >=0.1Hz (default)
    HighAccuracy    ///< Recalculate every sample while smoothing is active
};
```

---

## Output Structures

### CrossoverLR4Outputs

```cpp
/// @brief Output structure for 2-way crossover.
struct CrossoverLR4Outputs {
    float low;   ///< Lowpass output (content below crossover frequency)
    float high;  ///< Highpass output (content above crossover frequency)
};
```

### Crossover3WayOutputs

```cpp
/// @brief Output structure for 3-way crossover.
struct Crossover3WayOutputs {
    float low;   ///< Low band (below lowMidFrequency)
    float mid;   ///< Mid band (lowMidFrequency to midHighFrequency)
    float high;  ///< High band (above midHighFrequency)
};
```

### Crossover4WayOutputs

```cpp
/// @brief Output structure for 4-way crossover.
struct Crossover4WayOutputs {
    float sub;   ///< Sub band (below subLowFrequency)
    float low;   ///< Low band (subLowFrequency to lowMidFrequency)
    float mid;   ///< Mid band (lowMidFrequency to midHighFrequency)
    float high;  ///< High band (above midHighFrequency)
};
```

---

## CrossoverLR4 Class

### Synopsis

```cpp
/// @brief 2-way Linkwitz-Riley 4th-order (24dB/oct) crossover filter.
///
/// Provides phase-coherent band splitting where low + high outputs sum to flat.
/// Uses 4 cascaded Butterworth biquads (2 LP + 2 HP) for LR4 characteristic.
///
/// @par Thread Safety
/// Parameter setters are thread-safe (atomic). Processing methods are not
/// thread-safe and must only be called from the audio thread.
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
///
/// @par Usage
/// @code
/// CrossoverLR4 crossover;
/// crossover.prepare(44100.0);
/// crossover.setCrossoverFrequency(1000.0f);
///
/// // In audio callback
/// auto [low, high] = crossover.process(inputSample);
/// @endcode
class CrossoverLR4 {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinFrequency = 20.0f;
    static constexpr float kMaxFrequencyRatio = 0.45f;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr float kDefaultFrequency = 1000.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor.
    CrossoverLR4() noexcept = default;

    /// @brief Destructor.
    ~CrossoverLR4() noexcept = default;

    // Non-copyable (contains filter state)
    CrossoverLR4(const CrossoverLR4&) = delete;
    CrossoverLR4& operator=(const CrossoverLR4&) = delete;

    // Movable
    CrossoverLR4(CrossoverLR4&&) noexcept = default;
    CrossoverLR4& operator=(CrossoverLR4&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// @brief Initialize crossover for given sample rate.
    ///
    /// Resets all filter states and configures coefficients.
    /// Must be called before any processing.
    /// Safe to call multiple times (e.g., on sample rate change).
    ///
    /// @param sampleRate Sample rate in Hz (44100, 48000, 96000, 192000 typical)
    /// @note NOT real-time safe (may configure internal smoothers)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset filter states without reinitialization.
    ///
    /// Clears all biquad state variables (z1, z2) to prevent clicks
    /// when restarting processing. Does not affect coefficients.
    ///
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters (Thread-Safe)
    // =========================================================================

    /// @brief Set crossover frequency.
    ///
    /// The frequency is automatically clamped to [20Hz, sampleRate * 0.45].
    /// Changes are smoothed over the configured smoothing time.
    ///
    /// @param hz Crossover frequency in Hz
    /// @note Thread-safe (atomic write)
    void setCrossoverFrequency(float hz) noexcept;

    /// @brief Set parameter smoothing time.
    ///
    /// Controls how quickly frequency changes take effect.
    /// Default is 5ms which prevents audible clicks.
    ///
    /// @param ms Smoothing time in milliseconds (default 5ms)
    /// @note Thread-safe
    void setSmoothingTime(float ms) noexcept;

    /// @brief Set coefficient recalculation strategy.
    ///
    /// - Efficient: Recalculate only when frequency changes by >=0.1Hz
    /// - HighAccuracy: Recalculate every sample during smoothing
    ///
    /// @param mode TrackingMode enum value
    /// @note Thread-safe (atomic write)
    void setTrackingMode(TrackingMode mode) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get current crossover frequency target.
    /// @return Crossover frequency in Hz
    [[nodiscard]] float getCrossoverFrequency() const noexcept;

    /// @brief Get current smoothing time.
    /// @return Smoothing time in milliseconds
    [[nodiscard]] float getSmoothingTime() const noexcept;

    /// @brief Get current tracking mode.
    /// @return TrackingMode enum value
    [[nodiscard]] TrackingMode getTrackingMode() const noexcept;

    /// @brief Check if prepare() has been called.
    /// @return true if crossover is ready for processing
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process single sample through crossover.
    ///
    /// Returns low and high band outputs that sum to the input
    /// (flat frequency response).
    ///
    /// @param input Input sample
    /// @return CrossoverLR4Outputs with low and high band samples
    /// @note Real-time safe (noexcept, no allocation)
    [[nodiscard]] CrossoverLR4Outputs process(float input) noexcept;

    /// @brief Process block of samples through crossover.
    ///
    /// More efficient than calling process() per sample.
    /// Output buffers must be pre-allocated.
    ///
    /// @param input Input buffer (numSamples elements)
    /// @param low Output buffer for low band (numSamples elements)
    /// @param high Output buffer for high band (numSamples elements)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe (noexcept, no allocation)
    void processBlock(const float* input, float* low, float* high,
                      size_t numSamples) noexcept;
};
```

---

## Crossover3Way Class

### Synopsis

```cpp
/// @brief 3-way band splitter producing Low/Mid/High outputs.
///
/// Composes two CrossoverLR4 instances for phase-coherent 3-band splitting.
/// All three bands sum to the original signal.
///
/// @par Topology
/// Input -> CrossoverLR4#1 (lowMid) -> Low + HighFrom1
///          HighFrom1 -> CrossoverLR4#2 (midHigh) -> Mid + High
///
/// @par Frequency Ordering
/// The mid-high frequency is automatically clamped to >= low-mid frequency
/// to prevent invalid band configurations.
class Crossover3Way {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kDefaultLowMidFrequency = 300.0f;
    static constexpr float kDefaultMidHighFrequency = 3000.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    Crossover3Way() noexcept = default;
    ~Crossover3Way() noexcept = default;

    Crossover3Way(const Crossover3Way&) = delete;
    Crossover3Way& operator=(const Crossover3Way&) = delete;
    Crossover3Way(Crossover3Way&&) noexcept = default;
    Crossover3Way& operator=(Crossover3Way&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// @brief Initialize crossover for given sample rate.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all filter states.
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Set low-mid crossover frequency.
    /// @param hz Frequency in Hz (clamped to valid range)
    void setLowMidFrequency(float hz) noexcept;

    /// @brief Set mid-high crossover frequency.
    /// @param hz Frequency in Hz (clamped to >= lowMidFrequency)
    void setMidHighFrequency(float hz) noexcept;

    /// @brief Set parameter smoothing time for all internal crossovers.
    /// @param ms Smoothing time in milliseconds
    void setSmoothingTime(float ms) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    [[nodiscard]] float getLowMidFrequency() const noexcept;
    [[nodiscard]] float getMidHighFrequency() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process single sample through 3-way crossover.
    /// @param input Input sample
    /// @return Crossover3WayOutputs with low, mid, high band samples
    [[nodiscard]] Crossover3WayOutputs process(float input) noexcept;

    /// @brief Process block of samples through 3-way crossover.
    /// @param input Input buffer
    /// @param low Output buffer for low band
    /// @param mid Output buffer for mid band
    /// @param high Output buffer for high band
    /// @param numSamples Number of samples
    void processBlock(const float* input, float* low, float* mid, float* high,
                      size_t numSamples) noexcept;
};
```

---

## Crossover4Way Class

### Synopsis

```cpp
/// @brief 4-way band splitter producing Sub/Low/Mid/High outputs.
///
/// Composes three CrossoverLR4 instances for phase-coherent 4-band splitting.
/// All four bands sum to the original signal.
///
/// @par Topology
/// Input -> CrossoverLR4#1 (subLow) -> Sub + HighFrom1
///          HighFrom1 -> CrossoverLR4#2 (lowMid) -> Low + HighFrom2
///          HighFrom2 -> CrossoverLR4#3 (midHigh) -> Mid + High
///
/// @par Frequency Ordering
/// Frequencies are automatically ordered: subLow <= lowMid <= midHigh
class Crossover4Way {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kDefaultSubLowFrequency = 80.0f;
    static constexpr float kDefaultLowMidFrequency = 300.0f;
    static constexpr float kDefaultMidHighFrequency = 3000.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    Crossover4Way() noexcept = default;
    ~Crossover4Way() noexcept = default;

    Crossover4Way(const Crossover4Way&) = delete;
    Crossover4Way& operator=(const Crossover4Way&) = delete;
    Crossover4Way(Crossover4Way&&) noexcept = default;
    Crossover4Way& operator=(Crossover4Way&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Set sub-low crossover frequency.
    /// @param hz Frequency in Hz (clamped to valid range and <= lowMidFrequency)
    void setSubLowFrequency(float hz) noexcept;

    /// @brief Set low-mid crossover frequency.
    /// @param hz Frequency in Hz (clamped to >= subLow and <= midHigh)
    void setLowMidFrequency(float hz) noexcept;

    /// @brief Set mid-high crossover frequency.
    /// @param hz Frequency in Hz (clamped to >= lowMidFrequency)
    void setMidHighFrequency(float hz) noexcept;

    void setSmoothingTime(float ms) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    [[nodiscard]] float getSubLowFrequency() const noexcept;
    [[nodiscard]] float getLowMidFrequency() const noexcept;
    [[nodiscard]] float getMidHighFrequency() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    [[nodiscard]] Crossover4WayOutputs process(float input) noexcept;

    void processBlock(const float* input, float* sub, float* low,
                      float* mid, float* high, size_t numSamples) noexcept;
};
```

---

## Error Handling

All methods are `noexcept`. Invalid parameters are handled as follows:

| Condition | Behavior |
|-----------|----------|
| Frequency < 20Hz | Clamped to 20Hz |
| Frequency > Nyquist*0.45 | Clamped to Nyquist*0.45 |
| process() before prepare() | Returns zero-initialized output |
| processBlock() with nullptr | No-op (early return) |
| numSamples = 0 | No-op (early return) |

---

## Performance Characteristics

| Operation | Expected Cost |
|-----------|---------------|
| CrossoverLR4::process() | ~40ns (4 biquads) |
| CrossoverLR4::processBlock() | ~35ns/sample (loop optimization) |
| Crossover3Way::process() | ~80ns (2x CrossoverLR4) |
| Crossover4Way::process() | ~120ns (3x CrossoverLR4) |
| Coefficient recalc (TrackingMode::Efficient) | ~200ns per frequency change |
| Coefficient recalc (TrackingMode::HighAccuracy) | ~200ns per sample during smoothing |

---

## Include Dependencies

```cpp
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>
#include <atomic>
#include <array>
#include <cstdint>
```
