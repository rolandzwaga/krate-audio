// ==============================================================================
// Output safety clipper (user ruling 2026-11-10)
// ==============================================================================
// Always on, at the very end of the output path (after Output Gain and the global mix).
// Below the knee (-6 dBFS) it is an exact passthrough; above it a soft knee,
//   f(u) = sign(u) (K + H tanh((|u| - K) / H)),  K = 0.5, H = C - K, C = 0.98,
// approaches the ceiling C (-0.18 dBFS). Only the residual g(u) = f(u) - u, zero below
// the knee, runs through first-order antiderivative anti-aliasing (ADAA1), so the clean
// signal is untouched and gains no delay; a hard clamp at +-0.999 holds the ceiling
// should the residual's half-sample lag let a peak through. No latency, no allocation.
//
// DC servo: clipping a signal whose positive and negative peaks differ removes more from
// one side, which leaves DC (Half Rectify at drive 10 read 0.0995). The clipper's input is
// u = x + b, and b follows the mean of what the stage adds, y - x, through a one-pole loop
// at kServoHz, so the output keeps the input's mean. Shifting the input, not the output,
// keeps every output sample inside the ceiling. Below the knee b decays to exactly 0
// (kServoFloor), and from then on the stage is bit-exact again.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Disrumpo {

class OutputSafetyClipper {
public:
    static constexpr double kKnee = 0.5;         ///< -6.02 dBFS: exact passthrough at and below
    static constexpr double kCeiling = 0.98;     ///< -0.18 dBFS: the soft knee's asymptote
    static constexpr double kHardLimit = 0.999;  ///< last-resort clamp
    static constexpr double kEpsilon = 1.0e-5;   ///< ADAA1 step below which the midpoint is used
    static constexpr double kServoHz = 5.0;      ///< corner of the DC servo loop
    static constexpr double kServoFloor = 1.0e-9;  ///< a servo offset below this is exactly 0

    /// @brief Set the DC servo's loop coefficient for @p sampleRate (allocation-free).
    void prepare(double sampleRate) noexcept {
        servoCoeff_ = 1.0 - std::exp(-2.0 * std::numbers::pi * kServoHz / std::max(sampleRate, 1.0));
    }

    void reset() noexcept {
        prev_ = 0.0;
        offset_ = 0.0;
    }

    [[nodiscard]] float process(float input) noexcept {
        if (!Krate::DSP::detail::isFinite(input)) {
            reset();
            return input;
        }
        const double x = input;
        const double u = x + offset_;
        const double p = prev_;
        prev_ = u;
        double y = u;
        if (std::abs(u) > kKnee || std::abs(p) > kKnee) {
            const double du = u - p;
            y += std::abs(du) > kEpsilon ? (residualIntegral(u) - residualIntegral(p)) / du : residual(0.5 * (u + p));
        } else if (offset_ == 0.0) {
            return input;  // the residual is 0 on the whole segment and there is no offset
        }
        // The servo: the offset moves against what the stage adds to the input, hard clamp included
        y = std::clamp(y, -kHardLimit, kHardLimit);
        offset_ -= servoCoeff_ * (y - x);
        if (std::abs(offset_) < kServoFloor) offset_ = 0.0;
        return static_cast<float>(y);
    }

private:
    static constexpr double kHeadroom = kCeiling - kKnee;

    /// g(x) = f(x) - x: 0 up to the knee, then K + H tanh((|x| - K) / H) - |x|, odd.
    static double residual(double x) noexcept {
        const double a = std::abs(x);
        if (a <= kKnee) return 0.0;
        const double r = kKnee + kHeadroom * std::tanh((a - kKnee) / kHeadroom) - a;
        return x >= 0.0 ? r : -r;
    }

    /// G(x) = F(x) - x^2 / 2, zero at 0 and even: F(x) = K^2/2 + K (|x| - K) + H^2 ln cosh(z),
    /// z = (|x| - K) / H, ln cosh(z) = z + ln(1 + e^-2z) - ln 2.
    static double residualIntegral(double x) noexcept {
        const double a = std::abs(x);
        if (a <= kKnee) return 0.0;
        const double z = (a - kKnee) / kHeadroom;
        const double logCosh = z + std::log1p(std::exp(-2.0 * z)) - std::numbers::ln2;
        return 0.5 * kKnee * kKnee + kKnee * (a - kKnee) + kHeadroom * kHeadroom * logCosh - 0.5 * a * a;
    }

    double prev_ = 0.0;
    double offset_ = 0.0;
    double servoCoeff_ = 6.5428e-4;  ///< 5 Hz at 48 kHz until prepare()
};

}  // namespace Disrumpo
