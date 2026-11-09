// ==============================================================================
// Layer 2: DSP Processor - Linkwitz-Riley Crossover Filters
// ==============================================================================
// Phase-coherent multiband signal splitting using Linkwitz-Riley 4th-order
// (24dB/oct) crossover filters. Outputs sum to flat frequency response.
//
// Classes:
// - CrossoverLR4: 2-way band split (Low/High)
// - MultibandCrossover<MaxBands>: 1..MaxBands band split (cascaded CrossoverLR4)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, atomics)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle X: DSP Constraints (Butterworth Q for LR4, denormal prevention)
// - Principle XII: Test-First Development
//
// Reference: specs/076-crossover-filter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>


namespace Krate::DSP {

// =============================================================================
// TrackingMode Enumeration
// =============================================================================

/// @brief Coefficient recalculation strategy for frequency smoothing.
enum class TrackingMode : uint8_t {
    Efficient,      ///< Recalculate only when frequency changes by >=0.1Hz (default)
    HighAccuracy    ///< Recalculate every sample while smoothing is active
};

// =============================================================================
// Output Structures
// =============================================================================

/// @brief Output structure for 2-way crossover.
struct CrossoverLR4Outputs {
    float low = 0.0f;   ///< Lowpass output (content below crossover frequency)
    float high = 0.0f;  ///< Highpass output (content above crossover frequency)
};

// =============================================================================
// CrossoverLR4 Class
// =============================================================================

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
    static constexpr float kHysteresisThreshold = 0.1f;  ///< Hz threshold for Efficient mode

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor.
    CrossoverLR4() noexcept = default;

    /// @brief Destructor.
    ~CrossoverLR4() noexcept = default;

    // Non-copyable (contains filter state and atomics)
    CrossoverLR4(const CrossoverLR4&) = delete;
    CrossoverLR4& operator=(const CrossoverLR4&) = delete;

    // Move operations
    CrossoverLR4(CrossoverLR4&& other) noexcept
        : lpStage1_(other.lpStage1_)
        , lpStage2_(other.lpStage2_)
        , hpStage1_(other.hpStage1_)
        , hpStage2_(other.hpStage2_)
        , frequencySmoother_(other.frequencySmoother_)
        , sampleRate_(other.sampleRate_)
        , lastCoefficientFreq_(other.lastCoefficientFreq_)
        , prepared_(other.prepared_)
        , crossoverFrequency_(other.crossoverFrequency_.load(std::memory_order_relaxed))
        , smoothingTimeMs_(other.smoothingTimeMs_.load(std::memory_order_relaxed))
        , trackingMode_(other.trackingMode_.load(std::memory_order_relaxed)) {
    }

    CrossoverLR4& operator=(CrossoverLR4&& other) noexcept {
        if (this != &other) {
            lpStage1_ = other.lpStage1_;
            lpStage2_ = other.lpStage2_;
            hpStage1_ = other.hpStage1_;
            hpStage2_ = other.hpStage2_;
            frequencySmoother_ = other.frequencySmoother_;
            sampleRate_ = other.sampleRate_;
            lastCoefficientFreq_ = other.lastCoefficientFreq_;
            crossoverFrequency_.store(other.crossoverFrequency_.load(std::memory_order_relaxed),
                                      std::memory_order_relaxed);
            smoothingTimeMs_.store(other.smoothingTimeMs_.load(std::memory_order_relaxed),
                                   std::memory_order_relaxed);
            trackingMode_.store(other.trackingMode_.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
            prepared_ = other.prepared_;
        }
        return *this;
    }

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
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;

        // Configure frequency smoother
        const float srFloat = static_cast<float>(sampleRate);
        const float smoothMs = smoothingTimeMs_.load(std::memory_order_relaxed);
        frequencySmoother_.configure(smoothMs, srFloat);

        // Get clamped frequency and snap smoother to it
        const float freq = clampFrequency(crossoverFrequency_.load(std::memory_order_relaxed));
        frequencySmoother_.snapTo(freq);
        lastCoefficientFreq_ = freq;

        // Initialize all 4 biquads with Butterworth Q
        updateCoefficients(freq);

        // Reset filter states
        lpStage1_.reset();
        lpStage2_.reset();
        hpStage1_.reset();
        hpStage2_.reset();

        prepared_ = true;
    }

    /// @brief Reset filter states without reinitialization.
    ///
    /// Clears all biquad state variables (z1, z2) to prevent clicks
    /// when restarting processing. Does not affect coefficients.
    ///
    /// @note Real-time safe
    void reset() noexcept {
        lpStage1_.reset();
        lpStage2_.reset();
        hpStage1_.reset();
        hpStage2_.reset();
    }

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
    void setCrossoverFrequency(float hz) noexcept {
        const float clamped = clampFrequency(hz);
        crossoverFrequency_.store(clamped, std::memory_order_relaxed);
        frequencySmoother_.setTarget(clamped);
    }

    /// @brief Set parameter smoothing time.
    ///
    /// Controls how quickly frequency changes take effect.
    /// Default is 5ms which prevents audible clicks.
    ///
    /// @param ms Smoothing time in milliseconds (default 5ms)
    /// @note Thread-safe
    void setSmoothingTime(float ms) noexcept {
        smoothingTimeMs_.store(ms, std::memory_order_relaxed);
        if (prepared_) {
            frequencySmoother_.configure(ms, static_cast<float>(sampleRate_));
        }
    }

    /// @brief Set coefficient recalculation strategy.
    ///
    /// - Efficient: Recalculate only when frequency changes by >=0.1Hz
    /// - HighAccuracy: Recalculate every sample during smoothing
    ///
    /// @param mode TrackingMode enum value
    /// @note Thread-safe (atomic write)
    void setTrackingMode(TrackingMode mode) noexcept {
        trackingMode_.store(static_cast<int>(mode), std::memory_order_relaxed);
    }

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get current crossover frequency target.
    /// @return Crossover frequency in Hz
    [[nodiscard]] float getCrossoverFrequency() const noexcept {
        return crossoverFrequency_.load(std::memory_order_relaxed);
    }

    /// @brief Get current smoothing time.
    /// @return Smoothing time in milliseconds
    [[nodiscard]] float getSmoothingTime() const noexcept {
        return smoothingTimeMs_.load(std::memory_order_relaxed);
    }

    /// @brief Get current tracking mode.
    /// @return TrackingMode enum value
    [[nodiscard]] TrackingMode getTrackingMode() const noexcept {
        return static_cast<TrackingMode>(trackingMode_.load(std::memory_order_relaxed));
    }

    /// @brief Check if prepare() has been called.
    /// @return true if crossover is ready for processing
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

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
    [[nodiscard]] CrossoverLR4Outputs process(float input) noexcept {
        if (!prepared_) {
            return CrossoverLR4Outputs{.low = 0.0f, .high = 0.0f};
        }

        // Update frequency from smoother
        const float currentFreq = frequencySmoother_.process();

        // Check if we need to update coefficients based on tracking mode
        const auto mode = static_cast<TrackingMode>(trackingMode_.load(std::memory_order_relaxed));
        if (mode == TrackingMode::HighAccuracy) {
            // Always update during smoothing
            if (!frequencySmoother_.isComplete()) {
                updateCoefficients(currentFreq);
                lastCoefficientFreq_ = currentFreq;
            }
        } else {
            // Efficient mode: only update if frequency changed by >= threshold
            const float freqDelta = std::abs(currentFreq - lastCoefficientFreq_);
            if (freqDelta >= kHysteresisThreshold) {
                updateCoefficients(currentFreq);
                lastCoefficientFreq_ = currentFreq;
            }
        }

        // A non-finite input resets every stage and outputs silence (the
        // Biquad rule the float stages used to apply)
        if (!detail::isFinite(input)) {
            reset();
            return CrossoverLR4Outputs{.low = 0.0f, .high = 0.0f};
        }
        const auto x = static_cast<double>(input);

        // LR4 lowpass: cascade two Butterworth LP stages
        const double low = lpStage2_.process(lpStage1_.process(x));

        // LR4 highpass: cascade two Butterworth HP stages
        const double high = hpStage2_.process(hpStage1_.process(x));

        return CrossoverLR4Outputs{.low = static_cast<float>(low), .high = static_cast<float>(high)};
    }

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
                      size_t numSamples) noexcept {
        if (input == nullptr || low == nullptr || high == nullptr || numSamples == 0) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            auto outputs = process(input[i]);
            low[i] = outputs.low;
            high[i] = outputs.high;
        }
    }

private:
    /// @brief Clamp frequency to valid range based on current sample rate.
    [[nodiscard]] float clampFrequency(float freq) const noexcept {
        const float maxFreq = static_cast<float>(sampleRate_) * kMaxFrequencyRatio;
        return std::clamp(freq, kMinFrequency, maxFreq);
    }

    /// @brief Update all filter coefficients for given frequency.
    /// RBJ Butterworth low/high-pass (Biquad's FilterType::Lowpass/Highpass
    /// design) computed in double. 1 - cos(w) and 1 + cos(w) are formed as
    /// 2 sin^2(w/2) and 2 cos^2(w/2), so no step cancels at low fc/fs.
    void updateCoefficients(float freq) noexcept {
        constexpr double kButterworthQD = 0.70710678118654752440;
        const double omega = 2.0 * std::numbers::pi * static_cast<double>(freq) / sampleRate_;
        const double sinHalf = std::sin(0.5 * omega);
        const double cosHalf = std::cos(0.5 * omega);
        const double alpha = std::sin(omega) / (2.0 * kButterworthQD);
        const double invA0 = 1.0 / (1.0 + alpha);
        const double a1 = -2.0 * std::cos(omega) * invA0;
        const double a2 = (1.0 - alpha) * invA0;
        const double lpB0 = sinHalf * sinHalf * invA0;  // (1 - cos w) / (2 a0)
        const double hpB0 = cosHalf * cosHalf * invA0;  // (1 + cos w) / (2 a0)

        const Section lp{.b0 = lpB0, .b1 = 2.0 * lpB0, .b2 = lpB0, .a1 = a1, .a2 = a2};
        const Section hp{.b0 = hpB0, .b1 = -2.0 * hpB0, .b2 = hpB0, .a1 = a1, .a2 = a2};
        lpStage1_.setCoefficients(lp);
        lpStage2_.setCoefficients(lp);
        hpStage1_.setCoefficients(hp);
        hpStage2_.setCoefficients(hp);
    }

    /// One second-order section in double precision: coefficients AND TDF2
    /// state. A float32 TDF2 state at fc/fs ~6e-4 (112 Hz at 192 kHz) rounds
    /// by up to ~1 % of the DC gain (1 + a1 + a2 ~ 1e-5 amplifies each state
    /// rounding); measured +-0.15 dB at 112 Hz and +-1 dB at 20 Hz / 192 kHz
    /// even with exact float coefficients.
    struct Section {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
        double z1 = 0.0;
        double z2 = 0.0;

        void setCoefficients(const Section& c) noexcept {
            b0 = c.b0;
            b1 = c.b1;
            b2 = c.b2;
            a1 = c.a1;
            a2 = c.a2;
        }

        [[nodiscard]] double process(double x) noexcept {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            // Flush tiny states (the float stages' denormal rule)
            if (z1 > -kStateFlush && z1 < kStateFlush) z1 = 0.0;
            if (z2 > -kStateFlush && z2 < kStateFlush) z2 = 0.0;
            return y;
        }

        void reset() noexcept {
            z1 = 0.0;
            z2 = 0.0;
        }

        static constexpr double kStateFlush = static_cast<double>(kDenormalThreshold);
    };

    // =========================================================================
    // Members
    // =========================================================================

    // Filter stages (4 biquads: 2 LP + 2 HP cascaded for LR4)
    Section lpStage1_;
    Section lpStage2_;
    Section hpStage1_;
    Section hpStage2_;

    // Parameter smoothing
    OnePoleSmoother frequencySmoother_;

    // State
    double sampleRate_ = 44100.0;
    float lastCoefficientFreq_ = kDefaultFrequency;
    bool prepared_ = false;

    // Atomic parameters for thread-safe UI/audio thread interaction
    std::atomic<float> crossoverFrequency_{kDefaultFrequency};
    std::atomic<float> smoothingTimeMs_{kDefaultSmoothingMs};
    std::atomic<int> trackingMode_{static_cast<int>(TrackingMode::Efficient)};
};

// =============================================================================
// MultibandCrossover Class
// =============================================================================

/// @brief Multiband Linkwitz-Riley crossover: 1..MaxBands bands from cascaded
/// CrossoverLR4 splits, with optional D'Appolito allpass compensation.
///
/// @par Topology
/// Input -> LR4(f0) -> band 0 + rest; rest -> LR4(f1) -> band 1 + rest; ...
/// The last active band takes the final high output. With one active band the
/// input passes through unchanged.
///
/// @par Allpass Compensation
/// When enabled, band k of n active bands (k < n - 2) runs through
/// AP(f_{k+1}) ... AP(f_{n-2}) in ascending order, so every band carries the
/// same phase and the band sum is the allpass chain AP(f0) ... AP(f_{n-2}):
/// flat magnitude. The 2nd-order allpass Q is exactly 1/sqrt(2), because the
/// LR4 LP + HP sum is (s^2 - sqrt2*s + 1)/(s^2 + sqrt2*s + 1).
/// Reference: D'Appolito, "Active Realization of Multiway All-Pass Crossover
/// Systems", JAES Vol. 35, No. 4, April 1987.
///
/// @par Dry Compensation
/// processDryCompensation() runs a dry (unsplit) signal through
/// AP(f0) ... AP(f_{n-2}), the phase of the compensated band sum, so a dry/wet
/// mix does not notch at the crossovers.
///
/// @par Frequencies
/// setCrossoverFrequency() stores the value for every index, active or not, so
/// a later band-count increase finds it. Values are not ordered or clamped
/// against their neighbours: callers keep them ascending. Each crossover is
/// clamped to [20 Hz, 0.45 * sampleRate] where it is applied.
///
/// @par Lifecycle
/// prepare() sets every crossover to its stored frequency and snaps there (no
/// glide), clears every filter state and keeps the stored frequencies. A
/// crossover or allpass that joins on a band-count increase starts with clean
/// state at its frequency.
///
/// @par Thread Safety
/// Plain members: call every method from the audio thread, or while
/// processing is stopped.
///
/// @tparam MaxBands Maximum number of bands (>= 2)
template <int MaxBands>
class MultibandCrossover {
    static_assert(MaxBands >= 2, "MultibandCrossover needs at least 2 bands");

public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxBands = MaxBands;
    static constexpr int kNumCrossovers = MaxBands - 1;
    /// Q of the 2nd-order compensation allpasses: 1/sqrt(2)
    static constexpr float kAllpassQ = std::numbers::sqrt2_v<float> / 2.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    MultibandCrossover() noexcept { frequencies_.fill(CrossoverLR4::kDefaultFrequency); }
    ~MultibandCrossover() noexcept = default;

    MultibandCrossover(const MultibandCrossover&) = delete;
    MultibandCrossover& operator=(const MultibandCrossover&) = delete;
    MultibandCrossover(MultibandCrossover&&) noexcept = default;
    MultibandCrossover& operator=(MultibandCrossover&&) noexcept = default;

    /// @brief Initialize for a sample rate. Keeps the stored frequencies, snaps
    /// every crossover to its frequency (no glide) and clears all filter state.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        for (int i = 0; i < kNumCrossovers; ++i) {
            // CrossoverLR4::prepare() snaps its smoother to its stored frequency,
            // which it clamps at its own rate: set the frequency at the new rate
            // and prepare again
            auto& xo = crossovers_[idx(i)];
            xo.prepare(sampleRate);
            xo.setCrossoverFrequency(frequencies_[idx(i)]);
            xo.prepare(sampleRate);
            configureAllpasses(i);
        }
        resetAllpasses();
        prepared_ = true;
    }

    /// @brief Clear every filter state; frequencies and band count unchanged.
    void reset() noexcept {
        for (auto& xo : crossovers_) {
            xo.reset();
        }
        resetAllpasses();
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set the number of active bands.
    /// Crossovers and allpasses that join the chain start clean at their
    /// frequency: never the state they froze with when last used, nor a glide.
    /// @param numBands Number of bands, clamped to [1, MaxBands]
    void setActiveBands(int numBands) noexcept {
        const int newBands = std::clamp(numBands, 1, MaxBands);
        if (newBands == activeBands_) {
            return;
        }
        const int oldBands = activeBands_;
        activeBands_ = newBands;
        if (newBands < oldBands || !prepared_) {
            return;
        }
        // Crossovers oldBands-1 .. newBands-2 join the chain
        const int firstNew = oldBands - 1;
        for (int i = firstNew; i < newBands - 1; ++i) {
            crossovers_[idx(i)].prepare(sampleRate_);
        }
        // Every allpass at a joining crossover: the dry chain and the bands below it
        for (int i = firstNew; i < kNumCrossovers; ++i) {
            dryAllpasses_[idx(i)].reset();
            for (int band = 0; band < i; ++band) {
                bandAllpass(band, i).reset();
            }
        }
    }

    /// @brief Set the frequency of crossover `index`, active or not.
    /// The crossover glides there over the smoothing time; the allpasses at
    /// this frequency follow at once.
    /// @param index Crossover index (0 .. MaxBands-2); others are ignored
    /// @param hz Frequency in Hz (clamped to [20 Hz, 0.45 * sampleRate] where applied)
    void setCrossoverFrequency(int index, float hz) noexcept {
        if (index < 0 || index >= kNumCrossovers) {
            return;
        }
        frequencies_[idx(index)] = hz;
        crossovers_[idx(index)].setCrossoverFrequency(hz);
        if (prepared_) {
            configureAllpasses(index);
        }
    }

    /// @brief Enable or disable D'Appolito allpass compensation of the bands.
    /// Switching it on starts the band allpasses from clean state.
    void setAllpassCompensation(bool enabled) noexcept {
        if (enabled && !compensation_) {
            for (auto& row : bandAllpasses_) {
                for (auto& ap : row) {
                    ap.reset();
                }
            }
        }
        compensation_ = enabled;
    }

    /// @brief Set the frequency smoothing time of every crossover.
    /// @param ms Smoothing time in milliseconds
    void setSmoothingTime(float ms) noexcept {
        for (auto& xo : crossovers_) {
            xo.setSmoothingTime(ms);
        }
    }

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Stored frequency of crossover `index` (CrossoverLR4::kDefaultFrequency
    /// for an invalid index).
    [[nodiscard]] float getCrossoverFrequency(int index) const noexcept {
        if (index < 0 || index >= kNumCrossovers) {
            return CrossoverLR4::kDefaultFrequency;
        }
        return frequencies_[idx(index)];
    }

    [[nodiscard]] int getActiveBands() const noexcept { return activeBands_; }

    [[nodiscard]] bool isAllpassCompensationEnabled() const noexcept { return compensation_; }

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Split one sample into the active bands.
    /// Writes bands[0 .. activeBands-1]; the rest are left untouched (all
    /// zeros before prepare()).
    void process(float input, std::array<float, MaxBands>& bands) noexcept {
        if (!prepared_) {
            bands.fill(0.0f);
            return;
        }
        if (activeBands_ == 1) {
            bands[0] = input;
            return;
        }

        float rest = input;
        for (int i = 0; i < activeBands_ - 1; ++i) {
            const auto out = crossovers_[idx(i)].process(rest);
            bands[idx(i)] = out.low;
            rest = out.high;
        }
        bands[idx(activeBands_ - 1)] = rest;

        if (compensation_) {
            // Band k: AP(f_{k+1}) ... AP(f_{n-2}), ascending
            for (int band = 0; band < activeBands_ - 2; ++band) {
                for (int ap = band + 1; ap < activeBands_ - 1; ++ap) {
                    bands[idx(band)] = bandAllpass(band, ap).process(bands[idx(band)]);
                }
            }
        }
    }

    /// @brief Give a dry (unsplit) sample the phase of the compensated band sum:
    /// AP(f0) ... AP(f_{n-2}). Call once per sample alongside process().
    /// @param x Dry input sample
    /// @return x through the allpasses at every active crossover
    [[nodiscard]] float processDryCompensation(float x) noexcept {
        if (!prepared_ || activeBands_ == 1) {
            return x;
        }
        for (int i = 0; i < activeBands_ - 1; ++i) {
            x = dryAllpasses_[idx(i)].process(x);
        }
        return x;
    }

private:
    static constexpr int kBandAllpassSlots = MaxBands - 2;

    [[nodiscard]] static constexpr std::size_t idx(int i) noexcept {
        return static_cast<std::size_t>(i);
    }

    /// The allpass of band `band` at crossover `crossover` (crossover > band).
    [[nodiscard]] Biquad& bandAllpass(int band, int crossover) noexcept {
        return bandAllpasses_[idx(band)][idx(crossover - band - 1)];
    }

    [[nodiscard]] float clampFrequency(float hz) const noexcept {
        const float maxHz = static_cast<float>(sampleRate_) * CrossoverLR4::kMaxFrequencyRatio;
        return std::clamp(hz, CrossoverLR4::kMinFrequency, maxHz);
    }

    /// Configure every allpass at crossover `crossover`: the dry chain and the
    /// bands below it.
    void configureAllpasses(int crossover) noexcept {
        const float hz = clampFrequency(frequencies_[idx(crossover)]);
        const auto sr = static_cast<float>(sampleRate_);
        dryAllpasses_[idx(crossover)].configure(FilterType::Allpass, hz, kAllpassQ, 0.0f, sr);
        for (int band = 0; band < crossover; ++band) {
            bandAllpass(band, crossover).configure(FilterType::Allpass, hz, kAllpassQ, 0.0f, sr);
        }
    }

    void resetAllpasses() noexcept {
        for (auto& row : bandAllpasses_) {
            for (auto& ap : row) {
                ap.reset();
            }
        }
        for (auto& ap : dryAllpasses_) {
            ap.reset();
        }
    }

    // =========================================================================
    // Members
    // =========================================================================

    std::array<CrossoverLR4, kNumCrossovers> crossovers_;
    /// bandAllpasses_[band][j]: band `band` at crossover band + j + 1
    std::array<std::array<Biquad, kBandAllpassSlots>, kBandAllpassSlots> bandAllpasses_;
    /// dryAllpasses_[i]: the dry chain at crossover i
    std::array<Biquad, kNumCrossovers> dryAllpasses_;
    std::array<float, kNumCrossovers> frequencies_{};

    double sampleRate_ = 44100.0;
    int activeBands_ = MaxBands;
    bool compensation_ = false;
    bool prepared_ = false;
};

} // namespace Krate::DSP

