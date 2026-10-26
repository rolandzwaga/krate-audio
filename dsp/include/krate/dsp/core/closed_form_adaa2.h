// ==============================================================================
// Layer 0: Core Utility - Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// ClosedFormAdaa2<Law>, DiodeAdaaLaw, diodeH1Coefficient: second-order ADAA with
// closed-form antiderivatives in double (plan S1 C-1, C-11, S4.1).
//   Carries: FR-021 (Bite/BITE/Rectify ADAA), SC-010, SC-011 (through Rectifier,
//            BITE and Bite).
//   Spec:  specs/profundum-phase5-shape-character/spec.md
//   Plan:  specs/profundum-phase5-shape-character/plan.md
//
// Algorithm: Bilbao, Esqueda, Parker, Valimaki (2017), second-order
// antiderivative antialiasing. Computed in double because float cancellation in
// the F2 difference quotients is the known ADAA2 failure (C-11). kEpsilon is in
// argument units and matches TanhADAA / HardClipADAA (1e-5).
//
// Non-finite input never reaches process(): the owning stages sanitise first.
// stateFinite() lets them detect and recover from a pathological state.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>

#include <cmath>
#include <cstddef>

namespace Krate::DSP {

/// Second-order ADAA over a closed-form law.
/// Law: static double f(double), F1(double), F2(double); F1' = f, F2' = F1,
/// F1(0) = F2(0) = 0. Output is the band-limited f(x[n-1]) (one-sample delay).
template <class Law>
class ClosedFormAdaa2 {
public:
    static constexpr double kEpsilon = 1.0e-5; // C-11; argument units

    /// x1 = x2 = 0, F2(x1) = 0, d1 = Law::F1(0) = 0.
    void reset() noexcept {
        x1_ = 0.0;
        x2_ = 0.0;
        f2x1_ = 0.0;
        d1_ = 0.0;
    }

    /// Band-limited f(x[n-1]); one-sample delay. x must be finite.
    [[nodiscard]] double process(double x) noexcept {
        const double f2x = Law::F2(x);

        // D(x, x1) = |x - x1| > eps ? (F2(x) - F2(x1)) / (x - x1) : F1((x + x1)/2)
        const double dx1 = x - x1_;
        const double d0 = (std::abs(dx1) > kEpsilon) ? (f2x - f2x1_) / dx1
                                                     : Law::F1(0.5 * (x + x1_));

        double y;
        const double dx2 = x - x2_;
        if (std::abs(dx2) > kEpsilon) {
            y = 2.0 * (d0 - d1_) / dx2;
        } else {
            const double xBar = 0.5 * (x + x2_);
            const double delta = xBar - x1_;
            if (std::abs(delta) > kEpsilon) {
                y = (2.0 / delta) * (Law::F1(xBar) + (f2x1_ - Law::F2(xBar)) / delta);
            } else {
                y = Law::f(0.5 * (xBar + x1_));
            }
        }

        x2_ = x1_;
        x1_ = x;
        d1_ = d0;
        f2x1_ = f2x; // F2 cached: one F2 evaluation per sample on the main path
        return y;
    }

    /// True when every state member is finite (fast-math safe bit test).
    [[nodiscard]] bool stateFinite() const noexcept {
        return detail::isFinite(x1_) && detail::isFinite(x2_) && detail::isFinite(f2x1_) &&
               detail::isFinite(d1_);
    }

private:
    double x1_ = 0.0;
    double x2_ = 0.0;
    double f2x1_ = 0.0;
    double d1_ = 0.0;
};

/// Double-precision mirror of Asymmetric::diode (core/sigmoid.h:358-366), C-1,
/// with its first and second antiderivatives (zero at 0, continuous at the kink).
struct DiodeAdaaLaw {
    /// x >= 0: 1 - e^{-1.5x};  x < 0: x / (1 - 0.5x)
    [[nodiscard]] static double f(double x) noexcept {
        if (x >= 0.0)
            return 1.0 - std::exp(-1.5 * x);
        return x / (1.0 - 0.5 * x);
    }

    /// x >= 0: x + e^{-1.5x}/1.5 - 1/1.5;  x < 0: -4 ln(1 - 0.5x) - 2x
    [[nodiscard]] static double F1(double x) noexcept {
        if (x >= 0.0)
            return x + std::exp(-1.5 * x) / 1.5 - 1.0 / 1.5;
        return -4.0 * std::log(1.0 - 0.5 * x) - 2.0 * x;
    }

    /// x >= 0: x^2/2 - e^{-1.5x}/2.25 - x/1.5 + 1/2.25
    /// x < 0:  8((1 - 0.5x) ln(1 - 0.5x) - (1 - 0.5x)) - x^2 + 8
    /// (1 - 0.5x > 0 holds for every x < 0.)
    [[nodiscard]] static double F2(double x) noexcept {
        if (x >= 0.0)
            return x * x / 2.0 - std::exp(-1.5 * x) / 2.25 - x / 1.5 + 1.0 / 2.25;
        const double u = 1.0 - 0.5 * x;
        return 8.0 * (u * std::log(u) - u) - x * x + 8.0;
    }
};

/// PREPARE-TIME ONLY (never on the audio thread): the h1 Fourier coefficient of
/// DiodeAdaaLaw::f(gain * sin(phi)) over 256 midpoint phase points,
///   (2/256) * sum_{k=0}^{255} f(gain * sin phi_k) * sin phi_k,  phi_k = 2 pi (k + 0.5) / 256.
/// BassCharacterStage (c_bite over Amount, S5.2) and ShapeMorph (cBiteShape_ over
/// t2, S6.4) each fill their own 65-point table with it in prepare(); neither L3
/// class includes the other.
[[nodiscard]] inline double diodeH1Coefficient(double gain) noexcept {
    constexpr std::size_t kPoints = 256;
    constexpr double kTwoPiD = 6.283185307179586476925286766559;   // double; the Layer-0 kTwoPi is float (C4459, Phase 10 D-3)
    double sum = 0.0;
    for (std::size_t k = 0; k < kPoints; ++k) {
        const double phi = kTwoPiD * (static_cast<double>(k) + 0.5) / static_cast<double>(kPoints);
        const double s = std::sin(phi);
        sum += DiodeAdaaLaw::f(gain * s) * s;
    }
    return (2.0 / static_cast<double>(kPoints)) * sum;
}

} // namespace Krate::DSP
