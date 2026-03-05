// ==============================================================================
// HarmonicModulator -- LFO-Driven Per-Partial Animation
// ==============================================================================
// Plugin-local DSP class for Innexus M6 Phase 20.
// Location: plugins/innexus/src/dsp/harmonic_modulator.h
//
// Spec: specs/120-creative-extensions/spec.md
// Covers: FR-024 to FR-029, FR-051
// ==============================================================================

#pragma once

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/core/math_constants.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Innexus {

/// LFO waveform types for harmonic modulation (FR-024).
enum class ModulatorWaveform : int {
    Sine = 0,
    Triangle = 1,
    Square = 2,
    Saw = 3,
    RandomSH = 4  ///< Random Sample & Hold
};

/// Modulation target types (FR-024).
enum class ModulatorTarget : int {
    Amplitude = 0,  ///< Multiplicative unipolar (FR-025)
    Frequency = 1,  ///< Additive cents bipolar (FR-026)
    Pan = 2         ///< Offset bipolar (FR-027)
};

/// @brief LFO-driven per-partial animation for harmonic models (FR-024).
///
/// Each modulator instance has an independent free-running LFO (FR-029)
/// that modulates a configurable range of partials. Two instances are
/// used in the Innexus processor (Modulator 1 and Modulator 2).
///
/// The LFO phase is initialized to 0.0 in prepare() and never resets
/// on MIDI note events (FR-051).
///
/// @par Modulation formulas:
/// - Amplitude (FR-025): effectiveAmp = modelAmp * (1 - depth + depth * lfoUnipolar)
/// - Frequency (FR-026): effectiveFreq = modelFreq * pow(2, depth * lfoBipolar * 50 / 1200)
/// - Pan (FR-027): effectivePan = basePan + depth * lfoBipolar * 0.5
///
/// @par LFO waveforms (all from phase accumulator, no heap):
/// - Sine: sin(2*pi*phase)
/// - Triangle: 4*|phase - 0.5| - 1
/// - Square: phase < 0.5 ? 1 : -1
/// - Saw: 2*phase - 1
/// - Random S&H: held random value, updated on phase wrap
///
/// @par Thread Safety: Single-threaded (audio thread only).
/// @par Real-Time Safety: All methods noexcept, no allocations.
class HarmonicModulator {
public:
    /// Maximum frequency modulation range in cents (FR-026).
    static constexpr float kModMaxCents = 50.0f;

    /// Maximum pan modulation range (FR-027).
    static constexpr float kModMaxPan = 0.5f;

    HarmonicModulator() noexcept = default;

    /// @brief Initialize for processing (FR-051).
    /// Resets LFO phase to 0.0.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept
    {
        inverseSampleRate_ = 1.0f / static_cast<float>(sampleRate);
        phase_ = 0.0f;
        shValue_ = 0.0f;
        cachedBipolar_ = computeWaveform();
    }

    /// @brief Reset LFO phase to 0.0.
    void reset() noexcept
    {
        phase_ = 0.0f;
        shValue_ = 0.0f;
        cachedBipolar_ = computeWaveform();
    }

    /// @brief Set LFO waveform.
    void setWaveform(ModulatorWaveform waveform) noexcept
    {
        waveform_ = waveform;
        cachedBipolar_ = computeWaveform();
    }

    /// @brief Set LFO rate in Hz (FR-024).
    /// @param rateHz [0.01, 20.0]
    void setRate(float rateHz) noexcept
    {
        rate_ = rateHz;
    }

    /// @brief Set modulation depth (FR-024).
    /// @param depth [0.0, 1.0]
    void setDepth(float depth) noexcept
    {
        depth_ = depth;
    }

    /// @brief Set target partial range (FR-024).
    /// @param start First partial (1-based) [1, 48]
    /// @param end Last partial (1-based) [1, 48]
    void setRange(int start, int end) noexcept
    {
        rangeStart_ = start;
        rangeEnd_ = end;
    }

    /// @brief Set modulation target type.
    void setTarget(ModulatorTarget target) noexcept
    {
        target_ = target;
    }

    /// @brief Advance the LFO phase by one sample (FR-029).
    ///
    /// Phase increments by rate * inverseSampleRate.
    /// Free-running, never resets on note events.
    ///
    /// @note Real-time safe
    void advance() noexcept
    {
        phase_ += rate_ * inverseSampleRate_;

        if (phase_ >= 1.0f)
        {
            phase_ -= 1.0f;
            // Update S&H value on wrap (for RandomSH waveform)
            shValue_ = rng_.nextFloat(); // bipolar [-1, 1]
        }

        // Cache the current bipolar waveform value
        cachedBipolar_ = computeWaveform();
    }

    /// @brief Apply amplitude modulation to a frame's partials (FR-025).
    ///
    /// Modifies amplitude of partials in [rangeStart, rangeEnd] range.
    /// effectiveAmp = modelAmp * (1.0 - depth + depth * lfoUnipolar)
    ///
    /// @param frame HarmonicFrame to modify in-place
    void applyAmplitudeModulation(Krate::DSP::HarmonicFrame& frame) const noexcept
    {
        if (rangeStart_ > rangeEnd_ || depth_ <= 0.0f)
            return;

        const float unipolar = getCurrentValueUnipolar();
        const float factor = 1.0f - depth_ + depth_ * unipolar;

        const int n = frame.numPartials;
        for (int i = 0; i < n; ++i)
        {
            int harmIdx = frame.partials[static_cast<size_t>(i)].harmonicIndex;
            if (harmIdx >= rangeStart_ && harmIdx <= rangeEnd_)
            {
                frame.partials[static_cast<size_t>(i)].amplitude *= factor;
            }
        }
    }

    /// @brief Get per-partial frequency modulation multipliers (FR-026).
    ///
    /// Returns array of frequency multipliers for partials in range.
    /// Partials outside range get multiplier 1.0 (no modulation).
    /// multiplier = pow(2.0, depth * lfoBipolar * kModMaxCents / 1200.0)
    ///
    /// @param[out] multipliers Array of 48 frequency multipliers
    void getFrequencyMultipliers(
        std::array<float, Krate::DSP::kMaxPartials>& multipliers) const noexcept
    {
        multipliers.fill(1.0f);

        if (rangeStart_ > rangeEnd_ || depth_ <= 0.0f)
            return;

        const float bipolar = getCurrentValue();
        const float cents = depth_ * bipolar * kModMaxCents;
        const float multiplier = std::pow(2.0f, cents / 1200.0f);

        for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
        {
            int harmIdx = static_cast<int>(i) + 1; // 1-based
            if (harmIdx >= rangeStart_ && harmIdx <= rangeEnd_)
            {
                multipliers[i] = multiplier;
            }
        }
    }

    /// @brief Get per-partial pan offsets (FR-027).
    ///
    /// Returns array of pan offsets for partials in range.
    /// Partials outside range get offset 0.0 (no modulation).
    /// offset = depth * lfoBipolar * kModMaxPan
    ///
    /// @param[out] offsets Array of 48 pan offsets
    void getPanOffsets(
        std::array<float, Krate::DSP::kMaxPartials>& offsets) const noexcept
    {
        offsets.fill(0.0f);

        if (rangeStart_ > rangeEnd_ || depth_ <= 0.0f)
            return;

        const float bipolar = getCurrentValue();
        const float offset = depth_ * bipolar * kModMaxPan;

        for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
        {
            int harmIdx = static_cast<int>(i) + 1; // 1-based
            if (harmIdx >= rangeStart_ && harmIdx <= rangeEnd_)
            {
                offsets[i] = offset;
            }
        }
    }

    /// @brief Get current LFO value (bipolar, -1 to +1).
    [[nodiscard]] float getCurrentValue() const noexcept
    {
        return cachedBipolar_;
    }

    /// @brief Get current LFO value (unipolar, 0 to 1).
    [[nodiscard]] float getCurrentValueUnipolar() const noexcept
    {
        return (cachedBipolar_ + 1.0f) * 0.5f;
    }

    /// @brief Get current LFO phase [0, 1).
    [[nodiscard]] float getPhase() const noexcept
    {
        return phase_;
    }

private:
    /// Compute current waveform value from phase (bipolar).
    [[nodiscard]] float computeWaveform() const noexcept
    {
        switch (waveform_)
        {
        case ModulatorWaveform::Sine:
            return std::sin(Krate::DSP::kTwoPi * phase_);

        case ModulatorWaveform::Triangle:
            // 4*|phase - 0.5| - 1: at phase=0 -> 1, phase=0.5 -> -1, phase=1 -> 1
            return 4.0f * std::abs(phase_ - 0.5f) - 1.0f;

        case ModulatorWaveform::Square:
            return phase_ < 0.5f ? 1.0f : -1.0f;

        case ModulatorWaveform::Saw:
            // 2*phase - 1: at phase=0 -> -1, phase=1 -> 1
            return 2.0f * phase_ - 1.0f;

        case ModulatorWaveform::RandomSH:
            return shValue_;
        }

        return 0.0f; // unreachable
    }

    float phase_ = 0.0f;
    float rate_ = 1.0f;
    float depth_ = 0.0f;
    int rangeStart_ = 1;   // 1-based
    int rangeEnd_ = 48;    // 1-based
    ModulatorWaveform waveform_ = ModulatorWaveform::Sine;
    ModulatorTarget target_ = ModulatorTarget::Amplitude;
    float inverseSampleRate_ = 1.0f / 44100.0f;

    // S&H state
    float shValue_ = 0.0f;
    Krate::DSP::Xorshift32 rng_{12345};

    // Cached waveform output (updated each advance())
    float cachedBipolar_ = 0.0f;
};

} // namespace Innexus
