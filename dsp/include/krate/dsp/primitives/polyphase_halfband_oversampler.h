// ==============================================================================
// Layer 1: DSP Primitive - Profundum Phase 2 (specs/profundum-phase2-edge-fold)
// ==============================================================================
// PolyphaseHalfbandOversampler: zero-heap 2x/4x polyphase IIR halfband
// oversampler (plan S3; ratified C-1). Includes Layer 0 (core/) and the
// standard library only (C-1(b-i): the first-order allpass section is written
// inline, no one_pole_allpass.h include).
//   Spec:  specs/profundum-phase2-edge-fold/spec.md
//   Plan:  specs/profundum-phase2-edge-fold/plan.md (S3, S8.1, S8.2)
//   Tests: dsp/tests/unit/primitives/polyphase_halfband_oversampler_test.cpp
//          (tag [polyphase-halfband])
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace Krate::DSP {

/// One channel of a 2x/4x polyphase IIR halfband oversampler (Valenzuela-Constantinides). Zero
/// look-ahead (IIR, no FIR, no latency compensation); low-frequency group delay about 1.9 base-rate
/// samples per direction (measured in its test). Heap-free, copyable (copying continues the state),
/// all methods noexcept and allocation-free. Precondition: FTZ/DAZ on (sections are not flushed).
///
/// Each branch section is the first-order allpass y = a·(x − y1) + x1 (halfband-design.js `AP.p`).
/// There is deliberately no per-call NaN/Inf reset: resetting one branch alone would
/// desynchronise the polyphase pair (plan S1 C-1(b)). Callers poll stateFinite() instead.
class PolyphaseHalfbandOversampler {
public:
    static constexpr std::size_t kMaxFactor = 4;
    /// Outer stage (fs <-> 2fs): N = 8, transition 0.05 of the 2fs rate (halfband-design.js).
    static constexpr std::array<float, 8> kOuterCoefficients{
        0.0358327884f, 0.134090142f, 0.272040143f, 0.424324871f,
        0.572057197f, 0.706292142f, 0.827124762f, 0.941503094f};
    /// Inner stage (2fs <-> 4fs): N = 4, transition 0.25 of the 4fs rate.
    static constexpr std::array<float, 4> kInnerCoefficients{
        0.0424547099f, 0.170739850f, 0.393319893f, 0.745713589f};

    PolyphaseHalfbandOversampler() noexcept {
        outer_.setCoefficients(kOuterCoefficients);
        inner_.setCoefficients(kInnerCoefficients);
    }

    /// 2 or 4 (anything else -> 4); resets state.
    void setFactor(std::size_t requested) noexcept {
        factor_ = (requested == 2) ? std::size_t{2} : kMaxFactor;
        reset();
    }

    [[nodiscard]] std::size_t factor() const noexcept { return factor_; }

    /// Zero all section state.
    void reset() noexcept {
        outer_.reset();
        inner_.reset();
    }

    /// Zero the decimation sections only; the interpolation sections keep their state. For a
    /// caller that keeps upsampling while its oversampled-rate output is known to be zero.
    void resetDownsampler() noexcept {
        outer_.resetDown();
        inner_.resetDown();
    }

    /// Writes factor() samples at the oversampled rate.
    void upsample(float x, float* out) noexcept {
        if (factor_ == 2) {
            outer_.up(x, out);
            return;
        }
        std::array<float, 2> pq{};
        outer_.up(x, pq.data());
        inner_.up(pq[0], out);
        inner_.up(pq[1], out + 2);
    }

    /// Reads factor() samples; `atTwoX(float) -> float` is applied to each 2fs-rate sample
    /// before the outer decimation (for F = 2 that is the oversampled rate itself).
    template <class AtTwoX>
    [[nodiscard]] float downsample(const float* in, AtTwoX&& atTwoX) noexcept {
        float a = 0.0f;
        float b = 0.0f;
        if (factor_ == 2) {
            a = in[0];
            b = in[1];
        } else {
            a = inner_.down(in[0], in[1]);
            b = inner_.down(in[2], in[3]);
        }
        a = atTwoX(a);
        b = atTwoX(b);
        return outer_.down(a, b);
    }

    /// Identity hook.
    [[nodiscard]] float downsample(const float* in) noexcept {
        return downsample(in, [](float v) noexcept { return v; });
    }

    /// True when every section's x1 / y1 is finite.
    [[nodiscard]] bool stateFinite() const noexcept {
        return outer_.finite() && inner_.finite();
    }

private:
    struct Section {
        float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
        float process(float x) noexcept {
            const float y = a * (x - y1) + x1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    /// N/2 sections per branch, up and down separate. Branch A = even-index coefficients,
    /// branch B = odd-index (plan S3.2).
    template <std::size_t N>
    struct Stage2x {
        std::array<Section, N / 2> upA{}, upB{}, dnA{}, dnB{};

        void setCoefficients(const std::array<float, N>& c) noexcept {
            for (std::size_t i = 0; i < N / 2; ++i) {
                upA[i].a = c[2 * i];
                dnA[i].a = c[2 * i];
                upB[i].a = c[2 * i + 1];
                dnB[i].a = c[2 * i + 1];
            }
        }

        void reset() noexcept {
            const auto zero = [](Section& s) noexcept {
                s.x1 = 0.0f;
                s.y1 = 0.0f;
            };
            std::for_each(upA.begin(), upA.end(), zero);
            std::for_each(upB.begin(), upB.end(), zero);
            resetDown();
        }

        void resetDown() noexcept {
            const auto zero = [](Section& s) noexcept {
                s.x1 = 0.0f;
                s.y1 = 0.0f;
            };
            std::for_each(dnA.begin(), dnA.end(), zero);
            std::for_each(dnB.begin(), dnB.end(), zero);
        }

        /// outA = x through upA, outB = x through upB; emit {outA, outB}.
        void up(float x, float* out) noexcept {
            float pa = x;
            float pb = x;
            // Profundum Phase 9 T029 (bit-identical): the two branch chains are independent, so they
            // are advanced together, one section each per step, instead of one whole chain after the
            // other; each branch still sees exactly its own sequence of operations.
            for (std::size_t i = 0; i < N / 2; ++i) {
                pa = upA[i].process(pa);
                pb = upB[i].process(pb);
            }
            out[0] = pa;
            out[1] = pb;
        }

        /// a = x1 (the later sample) through dnA, b = x0 through dnB; return 0.5 (a + b).
        float down(float x0, float x1) noexcept {
            float pa = x1;
            float pb = x0;
            for (std::size_t i = 0; i < N / 2; ++i) {   // T029: branches interleaved (see up)
                pa = dnA[i].process(pa);
                pb = dnB[i].process(pb);
            }
            return 0.5f * (pa + pb);
        }

        [[nodiscard]] bool finite() const noexcept {
            const auto sectionFinite = [](const Section& s) noexcept {
                return detail::isFinite(s.x1) && detail::isFinite(s.y1);
            };
            return std::all_of(upA.begin(), upA.end(), sectionFinite) &&
                   std::all_of(upB.begin(), upB.end(), sectionFinite) &&
                   std::all_of(dnA.begin(), dnA.end(), sectionFinite) &&
                   std::all_of(dnB.begin(), dnB.end(), sectionFinite);
        }
    };

    Stage2x<8> outer_;
    Stage2x<4> inner_;
    std::size_t factor_ = kMaxFactor;
};

}  // namespace Krate::DSP
