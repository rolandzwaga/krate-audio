// ==============================================================================
// Layer 1: DSP Primitives - LowpassBiquadD
// ==============================================================================
// A second-order RBJ low-pass (the Biquad Lowpass design, biquad.h) evaluated in double precision:
// double coefficients and a double TDF2 state behind a float interface.
//
// Why (Profundum Phase 9 close, 2026-10-21, SC-021(a) attribution): a float32 TDF2 biquad at a very
// low normalised cutoff (500 Hz at 48 .. 192 kHz: fc / fs = 0.01 .. 0.0026) rounds its state
// coherently with the input, and a periodic input leaves a quasi-periodic error at -100 .. -118 dBFS
// (lines at the note's sample-phase beat), 25 dB above the rest of the chain's floor. ProfundumCore's
// excitation split and HarmonicFold's split use that filter at base and oversampled rate; their
// downstream stages (SHAPE 1, Character Tone 1) lift the error into the -65 .. -78 dBFS range at the
// voice sum. The same design in double returns the floor to -124 .. -132 dBFS (probe
// VoiceEngine_AliasingCoreProbe4, profundum_engine_aliasing_test.cpp).
//
// Real-time safe: no allocation, noexcept, no I/O. A non-finite input resets the state and returns
// 0.0f (the Biquad rule).
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Krate::DSP {

class LowpassBiquadD {
public:
    /// RBJ low-pass at cutoffHz / q (Biquad's FilterType::Lowpass formulas, in double). The state is
    /// kept. cutoffHz is clamped to (0, 0.49 fs]; a non-positive sample rate leaves the filter as an
    /// identity (b0 = 1).
    void configure(float cutoffHz, float q, double sampleRate) noexcept {
        if (!(sampleRate > 0.0)) {
            b0_ = 1.0; b1_ = 0.0; b2_ = 0.0; a1_ = 0.0; a2_ = 0.0;
            return;
        }
        const double fc = std::clamp(static_cast<double>(cutoffHz), 1.0e-3, 0.49 * sampleRate);
        const double qq = std::max(static_cast<double>(q), 1.0e-3);
        const double omega = 2.0 * std::numbers::pi * fc / sampleRate;
        const double cosOmega = std::cos(omega);
        const double alpha = std::sin(omega) / (2.0 * qq);
        const double a0 = 1.0 + alpha;
        b0_ = ((1.0 - cosOmega) / 2.0) / a0;
        b1_ = (1.0 - cosOmega) / a0;
        b2_ = b0_;
        a1_ = (-2.0 * cosOmega) / a0;
        a2_ = (1.0 - alpha) / a0;
    }

    void reset() noexcept {
        z1_ = 0.0;
        z2_ = 0.0;
    }

    [[nodiscard]] float process(float input) noexcept {
        if (!detail::isFinite(input)) {
            reset();
            return 0.0f;
        }
        const double x = static_cast<double>(input);
        const double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return static_cast<float>(y);
    }

    /// The state read through the float bit test (std::isfinite is unreliable under fast-math): a
    /// finite double beyond float range reads as non-finite here, which is the conservative verdict.
    [[nodiscard]] bool stateFinite() const noexcept {
        return detail::isFinite(static_cast<float>(z1_)) && detail::isFinite(static_cast<float>(z2_));
    }

private:
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
    double z1_ = 0.0, z2_ = 0.0;
};

} // namespace Krate::DSP
