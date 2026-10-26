// ==============================================================================
// Layer 1: Primitive Tests - PolyphaseHalfbandOversampler
//                                    (Profundum Phase 2, specs/profundum-phase2-edge-fold)
// ==============================================================================
// Reference: specs/profundum-phase2-edge-fold/plan.md (S3, S8.1, S8.2)
//            specs/profundum-phase2-edge-fold/tasks.md (T006 wires this TU,
//                                                       T007 fills it)
//
// SCOPE OF THIS TU (plan S8.2, the S3 row): design response, unity passband,
//   render-matches-design, zero-in-zero-out, copy continuation, reset
//   determinism, hook order, recorded group delay, noexcept / no heap.
//   plan S1 C-1: this primitive replaces the shared Oversampler for Phase 2.
//
// The shared test main sets FTZ/DAZ (dsp/tests/CMakeLists.txt, dsp_test_main.cpp),
// which is this primitive's documented precondition.
// ==============================================================================

#include <krate/dsp/primitives/polyphase_halfband_oversampler.h>

#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <vector>

using Krate::DSP::PolyphaseHalfbandOversampler;

namespace {

constexpr double kPi = 3.14159265358979323846;
using Cplx = std::complex<double>;

[[nodiscard]] double toDb(double mag) {
    return 20.0 * std::log10(std::max(mag, 1e-300));
}

/// Analytic decimation response of one halfband stage, in double, from the class's float
/// coefficients. Branch A = even-index, B = odd-index; each section
/// H(z) = (a + z^-2) / (1 + a z^-2) at the stage's input rate;
/// H(w) = 0.5 (A(e^jw) + e^-jw B(e^jw)). `w` is radians per sample at the stage input rate.
template <std::size_t N>
[[nodiscard]] double analyticMagnitude(const std::array<float, N>& c, double w) {
    const Cplx z2 = std::polar(1.0, -2.0 * w);
    Cplx branchA{1.0, 0.0};
    Cplx branchB{1.0, 0.0};
    for (std::size_t k = 0; k < N; ++k) {
        const double a = static_cast<double>(c[k]);
        const Cplx h = (a + z2) / (1.0 + a * z2);
        if (k % 2 == 0)
            branchA *= h;
        else
            branchB *= h;
    }
    return 0.5 * std::abs(branchA + std::polar(1.0, -w) * branchB);
}

/// Plain (rectangular) single-bin DFT in double at `hz`.
[[nodiscard]] Cplx dftBin(const float* x, std::size_t n, double fs, double hz) {
    const double w = 2.0 * kPi * hz / fs;
    Cplx acc{0.0, 0.0};
    for (std::size_t i = 0; i < n; ++i)
        acc += static_cast<double>(x[i]) * std::polar(1.0, -w * static_cast<double>(i));
    return acc;
}

/// Sinusoid amplitude at `hz` (rectangular window; callers use integer periods).
[[nodiscard]] double goertzelAmplitude(const float* x, std::size_t n, double fs, double hz) {
    return 2.0 * std::abs(dftBin(x, n, fs, hz)) / static_cast<double>(n);
}

[[nodiscard]] double rms(const float* x, std::size_t n) {
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        acc += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return std::sqrt(acc / static_cast<double>(n));
}

/// In-place iterative radix-2 FFT in double (size must be a power of two).
void fftDouble(std::vector<Cplx>& x) {
    const std::size_t n = x.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / static_cast<double>(len);
        const std::size_t half = len / 2;
        for (std::size_t k = 0; k < half; ++k) {
            const Cplx w = std::polar(1.0, ang * static_cast<double>(k));
            for (std::size_t i = 0; i < n; i += len) {
                const Cplx u = x[i + k];
                const Cplx v = x[i + k + half] * w;
                x[i + k] = u + v;
                x[i + k + half] = u - v;
            }
        }
    }
}

/// Up then down (identity hook) for every sample of `in`; returns the base-rate output and
/// appends every oversampled sample to `upOut` when non-null.
std::vector<float> roundTrip(PolyphaseHalfbandOversampler& os, const std::vector<float>& in,
                             std::vector<float>* upOut = nullptr) {
    std::vector<float> out(in.size());
    std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> buf{};
    for (std::size_t i = 0; i < in.size(); ++i) {
        os.upsample(in[i], buf.data());
        if (upOut != nullptr)
            upOut->insert(upOut->end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(os.factor()));
        out[i] = os.downsample(buf.data());
    }
    return out;
}

[[nodiscard]] std::vector<float> sineInput(std::size_t n, double fs, double hz, double peak,
                                           std::size_t phaseOffset = 0) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = static_cast<float>(peak * std::sin(2.0 * kPi * hz * static_cast<double>(i + phaseOffset) / fs));
    return v;
}

[[nodiscard]] bool sameBits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

struct IdentityHook {
    float operator()(float v) const noexcept { return v; }
};

}  // namespace

// ------------------------------------------------------------------------------
// Analytic design response (plan S3.2; tasks.md T007, deviation 3: normalised to |H(0)|)
// ------------------------------------------------------------------------------
TEST_CASE("PolyphaseHalfband_DesignResponse", "[primitives][polyphase-halfband]") {
    constexpr std::size_t kPoints = 8192;

    SECTION("outer stage (input rate 2fs): w = pi f / fs") {
        const auto& c = PolyphaseHalfbandOversampler::kOuterCoefficients;
        const double h0 = analyticMagnitude(c, 0.0);
        REQUIRE(h0 > 0.0);
        double passWorst = 0.0;
        double stopWorst = -1e300;
        for (std::size_t k = 0; k < kPoints; ++k) {
            const double w = kPi * static_cast<double>(k) / static_cast<double>(kPoints - 1);
            const double f = w / kPi;  // in units of the base fs
            const double g = toDb(analyticMagnitude(c, w) / h0);
            if (f <= 0.40)
                passWorst = std::max(passWorst, std::abs(g));
            if (f >= 0.55 && f <= 1.0)
                stopWorst = std::max(stopWorst, g);
        }
        const double halfDb = toDb(analyticMagnitude(c, kPi * 0.5) / h0);
        WARN("outer: passband worst |dev| " << passWorst << " dB (f <= 0.40 fs), at fs/2 " << halfDb
                                            << " dB, worst stopband " << stopWorst
                                            << " dB in [0.55 fs, fs] (plan -106.6)");
        CHECK(passWorst <= 0.01);
        CHECK(std::abs(halfDb - (-3.01)) <= 0.05);
        CHECK(stopWorst <= -100.0);
    }

    SECTION("inner stage (input rate 4fs): w = pi f / (2 fs)") {
        const auto& c = PolyphaseHalfbandOversampler::kInnerCoefficients;
        const double h0 = analyticMagnitude(c, 0.0);
        REQUIRE(h0 > 0.0);
        double passWorst = 0.0;
        double stopWorst = -1e300;
        for (std::size_t k = 0; k < kPoints; ++k) {
            const double w = kPi * static_cast<double>(k) / static_cast<double>(kPoints - 1);
            const double f = 2.0 * w / kPi;  // in units of the base fs
            const double g = toDb(analyticMagnitude(c, w) / h0);
            if (f <= 0.5)
                passWorst = std::max(passWorst, std::abs(g));
            if (f >= 1.5 && f <= 2.0)
                stopWorst = std::max(stopWorst, g);
        }
        WARN("inner: passband worst |dev| " << passWorst << " dB (f <= 0.5 fs), worst stopband "
                                            << stopWorst << " dB in [1.5 fs, 2 fs] (plan -116.8)");
        CHECK(passWorst <= 0.01);
        CHECK(stopWorst <= -100.0);
    }
}

// ------------------------------------------------------------------------------
// Unity passband through up -> down (identity hook)
// ------------------------------------------------------------------------------
TEST_CASE("PolyphaseHalfband_UnityPassband", "[primitives][polyphase-halfband]") {
    constexpr double kFs = 48000.0;
    constexpr double kTone = 1000.0;
    constexpr std::size_t kN = 48000;    // 1 s
    constexpr std::size_t kSkip = 4800;  // 0.1 s; the remaining 43200 samples are 900 periods
    const double peak = std::pow(10.0, -12.0 / 20.0) * std::sqrt(2.0);  // -12 dBFS RMS

    for (const std::size_t factor : {std::size_t{4}, std::size_t{2}}) {
        INFO("factor " << factor);
        PolyphaseHalfbandOversampler os;
        os.setFactor(factor);
        REQUIRE(os.factor() == factor);
        const auto in = sineInput(kN, kFs, kTone, peak);
        const auto out = roundTrip(os, in);

        const std::size_t n = kN - kSkip;
        const double inRms = rms(in.data() + kSkip, n);
        const double outRms = rms(out.data() + kSkip, n);
        const double gainDb = toDb(outRms / inRms);
        INFO("output RMS re input RMS " << gainDb << " dB");
        CHECK(std::abs(gainDb) <= 0.01);

        // Local double DFT in place of the Phase 1 BH7 spectrum (not available in primitives).
        const double tone = goertzelAmplitude(out.data() + kSkip, n, kFs, kTone);
        REQUIRE(tone > 0.0);
        for (const double hz : {3000.0, 5000.0, 7000.0}) {
            const double reDb = toDb(goertzelAmplitude(out.data() + kSkip, n, kFs, hz) / tone);
            INFO(hz << " Hz: " << reDb << " dB re tone");
            CHECK(reDb <= -120.0);
        }
        CHECK(os.stateFinite());
    }
}

// ------------------------------------------------------------------------------
// Rendered response matches the analytic design (plan S3.2 branch order)
// ------------------------------------------------------------------------------
TEST_CASE("PolyphaseHalfband_RenderMatchesDesign", "[primitives][polyphase-halfband]") {
    const auto& c = PolyphaseHalfbandOversampler::kOuterCoefficients;
    const double h0 = analyticMagnitude(c, 0.0);

    SECTION("upsampler impulse response, F = 2") {
        constexpr std::size_t kLen = 8192;
        PolyphaseHalfbandOversampler os;
        os.setFactor(2);
        std::vector<Cplx> x(2 * kLen);
        std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> buf{};
        for (std::size_t i = 0; i < kLen; ++i) {
            os.upsample(i == 0 ? 1.0f : 0.0f, buf.data());
            x[2 * i] = Cplx{static_cast<double>(buf[0]), 0.0};
            x[2 * i + 1] = Cplx{static_cast<double>(buf[1]), 0.0};
        }
        fftDouble(x);
        const double dc = std::abs(x[0]);
        REQUIRE(dc > 0.0);

        // Bin k is k * 2fs / (2 kLen) = (k / kLen) * fs. Normalising to DC removes the x2 path gain.
        double passWorst = 0.0;
        double stopWorst = -1e300;
        for (std::size_t k = 0; k <= kLen; ++k) {
            const double f = static_cast<double>(k) / static_cast<double>(kLen);
            const double renderedDb = toDb(std::abs(x[k]) / dc);
            if (f <= 0.40) {
                const double designDb = toDb(analyticMagnitude(c, kPi * f) / h0);
                passWorst = std::max(passWorst, std::abs(renderedDb - designDb));
            }
            if (f >= 0.55 && f <= 1.0)
                stopWorst = std::max(stopWorst, renderedDb);
        }
        WARN("rendered upsampler: passband |render - design| worst " << passWorst
                                                                     << " dB, worst stopband " << stopWorst << " dB");
        CHECK(passWorst <= 0.01);
        CHECK(stopWorst <= -100.0);
    }

    SECTION("downsampler steady-state gain, F = 2 (in[1] -> branch A, in[0] -> branch B)") {
        // A 2fs-rate tone at base frequency f; output steady state over integer periods.
        // With the branch order swapped, the stopband rows fail by tens of dB.
        constexpr std::size_t kOut = 16384;
        constexpr std::size_t kSkip = 2048;
        constexpr double kPeak = 0.5;
        for (const double f : {0.3125, 0.625, 0.75, 0.875}) {
            INFO("f = " << f << " fs");
            PolyphaseHalfbandOversampler os;
            os.setFactor(2);
            std::vector<float> out(kOut);
            std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> buf{};
            for (std::size_t m = 0; m < kOut; ++m) {
                for (std::size_t j = 0; j < 2; ++j) {
                    const double t = static_cast<double>(2 * m + j);
                    buf[j] = static_cast<float>(kPeak * std::sin(2.0 * kPi * (f / 2.0) * t));
                }
                out[m] = os.downsample(buf.data());
            }
            const double gainDb = toDb(rms(out.data() + kSkip, kOut - kSkip) / (kPeak / std::sqrt(2.0)));
            const double designDb = toDb(analyticMagnitude(c, kPi * f) / h0);
            INFO("rendered " << gainDb << " dB, design " << designDb << " dB");
            if (f <= 0.40)
                CHECK(std::abs(gainDb - designDb) <= 0.01);
            else
                CHECK(gainDb <= -100.0);
        }
    }
}

// ------------------------------------------------------------------------------
// Zero in, zero out (bit test)
// ------------------------------------------------------------------------------
TEST_CASE("PolyphaseHalfband_ZeroInZeroOut", "[primitives][polyphase-halfband]") {
    for (const std::size_t factor : {std::size_t{2}, std::size_t{4}}) {
        INFO("factor " << factor);
        PolyphaseHalfbandOversampler os;
        os.setFactor(factor);
        os.reset();
        std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> buf{};
        bool allZero = true;
        for (std::size_t i = 0; i < 4096; ++i) {
            os.upsample(0.0f, buf.data());
            for (std::size_t j = 0; j < factor; ++j)
                allZero = allZero && std::bit_cast<std::uint32_t>(buf[j]) == 0u;
            const float y = os.downsample(buf.data());
            allZero = allZero && std::bit_cast<std::uint32_t>(y) == 0u;
        }
        CHECK(allZero);
    }
}

// ------------------------------------------------------------------------------
// Copying continues the state exactly
// ------------------------------------------------------------------------------
TEST_CASE("PolyphaseHalfband_CopyContinuesIdentically", "[primitives][polyphase-halfband]") {
    for (const std::size_t factor : {std::size_t{4}, std::size_t{2}}) {
        INFO("factor " << factor);
        PolyphaseHalfbandOversampler a;
        a.setFactor(factor);
        const auto first = sineInput(1000, 48000.0, 437.0, 0.7);
        (void)roundTrip(a, first);

        PolyphaseHalfbandOversampler b = a;
        const auto second = sineInput(1000, 48000.0, 437.0, 0.7, 1000);
        std::vector<float> upA;
        std::vector<float> upB;
        const auto outA = roundTrip(a, second, &upA);
        const auto outB = roundTrip(b, second, &upB);
        CHECK(sameBits(outA, outB));
        CHECK(sameBits(upA, upB));
    }
}

// ------------------------------------------------------------------------------
// reset() and setFactor() are deterministic
// ------------------------------------------------------------------------------
TEST_CASE("PolyphaseHalfband_ResetDeterministic", "[primitives][polyphase-halfband]") {
    PolyphaseHalfbandOversampler fresh;
    CHECK(fresh.factor() == 4);

    SECTION("two different histories, reset, identical input") {
        PolyphaseHalfbandOversampler a;
        PolyphaseHalfbandOversampler b;
        (void)roundTrip(a, sineInput(500, 48000.0, 1234.0, 0.9));
        (void)roundTrip(b, sineInput(700, 48000.0, 77.0, 0.3));
        a.reset();
        b.reset();
        const auto in = sineInput(1000, 48000.0, 555.0, 0.5);
        std::vector<float> upA;
        std::vector<float> upB;
        const auto outA = roundTrip(a, in, &upA);
        const auto outB = roundTrip(b, in, &upB);
        CHECK(sameBits(outA, outB));
        CHECK(sameBits(upA, upB));
        CHECK(a.stateFinite());
    }

    SECTION("setFactor maps anything but 2 to 4") {
        PolyphaseHalfbandOversampler os;
        os.setFactor(3);
        CHECK(os.factor() == 4);
        os.setFactor(0);
        CHECK(os.factor() == 4);
        os.setFactor(8);
        CHECK(os.factor() == 4);
        os.setFactor(2);
        CHECK(os.factor() == 2);
        os.setFactor(4);
        CHECK(os.factor() == 4);
    }

    SECTION("setFactor(2) resets state") {
        PolyphaseHalfbandOversampler used;
        (void)roundTrip(used, sineInput(500, 48000.0, 1234.0, 0.9));
        used.setFactor(2);
        PolyphaseHalfbandOversampler clean;
        clean.setFactor(2);
        const auto in = sineInput(1000, 48000.0, 555.0, 0.5);
        std::vector<float> upUsed;
        std::vector<float> upClean;
        const auto outUsed = roundTrip(used, in, &upUsed);
        const auto outClean = roundTrip(clean, in, &upClean);
        CHECK(sameBits(outUsed, outClean));
        CHECK(sameBits(upUsed, upClean));
    }

    SECTION("resetDownsampler clears the decimator only; the interpolator continues") {
        for (const std::size_t factor : {std::size_t{2}, std::size_t{4}}) {
            INFO("factor " << factor);
            // `used` decimated a loud history; `twin` saw the same interpolator input but its
            // decimator was only ever fed zeros, so after resetDownsampler both must match bitwise.
            PolyphaseHalfbandOversampler used;
            PolyphaseHalfbandOversampler twin;
            used.setFactor(factor);
            twin.setFactor(factor);
            const auto history = sineInput(500, 48000.0, 1234.0, 0.9);
            std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> buf{};
            const std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> zeros{};
            for (const float x : history) {
                used.upsample(x, buf.data());
                (void)used.downsample(buf.data());
                twin.upsample(x, buf.data());
                (void)twin.downsample(zeros.data());
            }
            used.resetDownsampler();
            twin.resetDownsampler();
            const auto in = sineInput(1000, 48000.0, 555.0, 0.5, history.size());
            std::vector<float> upUsed;
            std::vector<float> upTwin;
            const auto outUsed = roundTrip(used, in, &upUsed);
            const auto outTwin = roundTrip(twin, in, &upTwin);
            CHECK(sameBits(outUsed, outTwin));
            CHECK(sameBits(upUsed, upTwin));

            // Teeth: the interpolator state was kept, so the output differs from a full reset.
            PolyphaseHalfbandOversampler cleared;
            cleared.setFactor(factor);
            std::vector<float> upCleared;
            (void)roundTrip(cleared, in, &upCleared);
            CHECK_FALSE(sameBits(upUsed, upCleared));
        }
    }
}

// ------------------------------------------------------------------------------
// The 2fs hook: count and order per downsample()
// ------------------------------------------------------------------------------
TEST_CASE("PolyphaseHalfband_HookOrder", "[primitives][polyphase-halfband]") {
    std::array<float, 2> seen{};
    std::size_t calls = 0;
    const auto hook = [&](float v) noexcept {
        if (calls < seen.size())
            seen[calls] = v;
        ++calls;
        return v;
    };

    SECTION("F = 2: two calls, in[0] then in[1]") {
        PolyphaseHalfbandOversampler os;
        os.setFactor(2);
        const std::array<float, 4> in{0.25f, -0.5f, 0.0f, 0.0f};
        (void)os.downsample(in.data(), hook);
        CHECK(calls == 2);
        CHECK(std::bit_cast<std::uint32_t>(seen[0]) == std::bit_cast<std::uint32_t>(in[0]));
        CHECK(std::bit_cast<std::uint32_t>(seen[1]) == std::bit_cast<std::uint32_t>(in[1]));
    }

    SECTION("F = 4: two calls, the in[0..1] 2fs sample then the in[2..3] one") {
        PolyphaseHalfbandOversampler os;
        os.setFactor(4);
        // From reset, inner decimation of zeros is exactly zero; a non-zero second pair makes
        // the second 2fs sample non-zero, so the recorded order is observable.
        const std::array<float, 4> in{0.0f, 0.0f, 0.5f, 0.5f};
        (void)os.downsample(in.data(), hook);
        CHECK(calls == 2);
        CHECK(std::bit_cast<std::uint32_t>(seen[0]) == 0u);
        CHECK(seen[1] != 0.0f);

        // Every further downsample adds exactly two calls.
        for (std::size_t i = 0; i < 10; ++i)
            (void)os.downsample(in.data(), hook);
        CHECK(calls == 22);
    }
}

// ------------------------------------------------------------------------------
// Low-frequency group delay of the up -> down round trip (recorded, no threshold)
// ------------------------------------------------------------------------------
TEST_CASE("PolyphaseHalfband_GroupDelayRecorded", "[primitives][polyphase-halfband]") {
    constexpr double kFs = 48000.0;
    constexpr std::size_t kLen = 8192;
    for (const std::size_t factor : {std::size_t{2}, std::size_t{4}}) {
        PolyphaseHalfbandOversampler os;
        os.setFactor(factor);
        std::vector<float> impulse(kLen, 0.0f);
        impulse[0] = 1.0f;
        const auto h = roundTrip(os, impulse);

        const double w20 = 2.0 * kPi * 20.0 / kFs;
        const double w80 = 2.0 * kPi * 80.0 / kFs;
        const double ph20 = std::arg(dftBin(h.data(), kLen, kFs, 20.0));
        const double ph80 = std::arg(dftBin(h.data(), kLen, kFs, 80.0));
        const double slope = -(ph80 - ph20) / (w80 - w20);  // round-trip group delay, base samples
        const double delay20 = -ph20 / w20;
        const double delay80 = -ph80 / w80;
        WARN("F = " << factor << ": round-trip group delay (phase slope 20-80 Hz @ 48 kHz) " << slope
                    << " base samples (" << slope / 2.0 << " per direction; plan ~1.9); phase delay "
                    << delay20 << " @ 20 Hz, " << delay80 << " @ 80 Hz");
        REQUIRE(Krate::DSP::detail::isFinite(slope));
        REQUIRE(Krate::DSP::detail::isFinite(delay20));
        REQUIRE(Krate::DSP::detail::isFinite(delay80));
    }
}

// ------------------------------------------------------------------------------
// noexcept, trivially copyable (no heap: nothing to own)
// ------------------------------------------------------------------------------
TEST_CASE("PolyphaseHalfband_NoexceptAndNoHeap", "[primitives][polyphase-halfband]") {
    using P = PolyphaseHalfbandOversampler;
    static_assert(noexcept(std::declval<P&>().upsample(0.0f, std::declval<float*>())));
    static_assert(noexcept(std::declval<float&>() = std::declval<P&>().downsample(std::declval<const float*>())));
    static_assert(noexcept(std::declval<float&>() =
                               std::declval<P&>().downsample(std::declval<const float*>(), IdentityHook{})));
    static_assert(noexcept(std::declval<P&>().reset()));
    static_assert(noexcept(std::declval<P&>().setFactor(std::size_t{2})));
    static_assert(std::is_trivially_copyable_v<P>);
    static_assert(P::kMaxFactor == 4);
    SUCCEED("compile-time checks hold");
}
