// ==============================================================================
// Profundum_SpectrumProducerMath (Phase 12 T013; FR-071, FR-073 - FR-075;
// plan 4.2, 4.3, 5.2 - 5.4; tasks.md Deviation 2)
// ==============================================================================
// Drives ProfundumSpectrumProducer directly through its test seam
// (prepare / feedInterval / feedSilence / takeFrame) with synthetic 32-sample
// intervals. No engine, no processor. The SC-named arms (SC-010, SC-025, SC-030
// through the plugin) live in integration/spectrum_frame_test.cpp.
//
// Non-finite values are produced from bit patterns through a volatile read,
// and finiteness is checked with Krate::DSP::detail::isFinite (fast-math safe).
// ==============================================================================

#include "processor/goertzel_bank_simd.h"
#include "processor/spectrum_frame.h"
#include "processor/spectrum_producer.h"

#include <krate/dsp/core/db_utils.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <random>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using Profundum::ProfundumSpectrumFrame;
using Profundum::ProfundumSpectrumProducer;

constexpr std::size_t kInterval = 32;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr float kE1 = 41.20344f;
constexpr float kC3 = 130.8128f;
constexpr float kC6 = 1046.502f;

// The plan 4.2 layout asserts, repeated on the test side.
static_assert(sizeof(ProfundumSpectrumFrame) == 288);
static_assert(sizeof(ProfundumSpectrumFrame) % 32 == 0);
static_assert(std::is_trivially_copyable_v<ProfundumSpectrumFrame>);
static_assert(std::is_standard_layout_v<ProfundumSpectrumFrame>);
static_assert(offsetof(ProfundumSpectrumFrame, windowEndSample) == 24);
static_assert(offsetof(ProfundumSpectrumFrame, partialPower) == 32);

float makeNaN() noexcept {
    volatile std::uint32_t bits = 0x7FC00000u;
    const std::uint32_t b = bits;
    float f = 0.0f;
    std::memcpy(&f, &b, sizeof f);
    return f;
}

bool zeroOrNormal(float x) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(x);
    return (bits & 0x7FFFFFFFu) == 0u || (bits & 0x7F800000u) != 0u;
}

bool frameFinite(const ProfundumSpectrumFrame& f) noexcept {
    using Krate::DSP::detail::isFinite;
    if (!isFinite(f.sampleRate) || !isFinite(f.f0Hz) || !isFinite(f.anchorPower))
        return false;
    return std::ranges::all_of(f.partialPower, [](float p) { return isFinite(p); });
}

std::uint32_t expectedWindow(double fs, double f0) {
    const double n = std::round(8.0 * fs / f0);
    return static_cast<std::uint32_t>(std::clamp(n, 64.0, 1048576.0));
}

/// One synthetic interval source: character L = R = sum of (amp, harmonic) sines, anchor L = R = anchorAmp sin(f0).
struct Source {
    double fs = 48000.0;
    double f0 = static_cast<double>(kE1);
    std::vector<std::pair<double, int>> partials;  // (amplitude, harmonic number)
    double anchorAmp = 0.0;
    double constant = 0.0;  // added to every channel (TinyInputs)

    void fill(std::uint64_t start, std::array<float, kInterval>& c, std::array<float, kInterval>& a) const {
        for (std::size_t i = 0; i < kInterval; ++i) {
            const double t = static_cast<double>(start + i) / fs;
            double x = constant;
            for (const auto& [amp, h] : partials)
                x += amp * std::sin(kTwoPi * static_cast<double>(h) * f0 * t);
            c[i] = static_cast<float>(x);
            a[i] = static_cast<float>(constant + anchorAmp * std::sin(kTwoPi * f0 * t));
        }
    }
};

struct Feeder {
    ProfundumSpectrumProducer producer;
    std::uint64_t pos = 0;
    std::array<float, kInterval> c{};
    std::array<float, kInterval> a{};

    void feed(const Source& src, float f0Hz, std::uint8_t focus = 0, bool poly = false) {
        src.fill(pos, c, a);
        producer.feedInterval(std::span<const float>(c), std::span<const float>(c), std::span<const float>(a),
                              std::span<const float>(a), f0Hz, focus, poly, pos);
        pos += kInterval;
    }
};

double dbRe(double p, double ref) { return 10.0 * std::log10(p / ref); }

/// Feeds a steady source until a frame is staged; returns false if none arrives within maxIntervals.
bool runUntilFrame(Feeder& fd, const Source& src, float f0Hz, ProfundumSpectrumFrame& out,
                   std::size_t maxIntervals = 40000) {
    for (std::size_t i = 0; i < maxIntervals; ++i) {
        fd.feed(src, f0Hz);
        if (fd.producer.takeFrame(out))
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("Profundum_SpectrumProducerMath", "[profundum][ui][spectrum]") {
    SECTION("SineReadsAmplitudeSquared") {
        Feeder fd;
        fd.producer.prepare(48000.0);
        Source src;
        src.fs = 48000.0;
        src.f0 = static_cast<double>(kE1);
        src.partials = {{0.5, 3}, {0.25, 10}};
        src.anchorAmp = 0.3;

        ProfundumSpectrumFrame frame{};
        REQUIRE(runUntilFrame(fd, src, kE1, frame));
        CHECK(frame.windowSamples == 9320u);
        CHECK(frame.windowSamples == expectedWindow(48000.0, static_cast<double>(kE1)));
        CHECK(frame.numPartials == 64);
        CHECK(frame.sampleRate == 48000.0f);
        CHECK(std::abs(static_cast<double>(frame.f0Hz) / static_cast<double>(kE1) - 1.0) <= 1.0e-4);
        CHECK((frame.flags & Profundum::kFrameFlagSilent) == 0);

        CHECK(std::abs(dbRe(frame.partialPower[2], 0.25)) <= 0.05);
        CHECK(std::abs(dbRe(frame.partialPower[9], 0.0625)) <= 0.05);
        for (std::size_t i = 0; i < Profundum::kSpectrumMaxPartials; ++i) {
            if (i == 2 || i == 9)
                continue;
            INFO("partial index " << i << " power " << frame.partialPower[i]);
            CHECK(static_cast<double>(frame.partialPower[i]) <= 0.25 * 1.0e-8);  // <= -80 dB re 0.25
        }
        CHECK(std::abs(dbRe(frame.anchorPower, 0.09)) <= 0.05);
        CHECK(frameFinite(frame));
    }

    SECTION("PartialCountAndNyquist") {
        struct Case {
            double fs;
            float f0;
            int count;
        };
        const std::array<Case, 3> cases{{{.fs = 48000.0, .f0 = kE1, .count = 64},
                                         {.fs = 48000.0, .f0 = kC3, .count = 64},
                                         {.fs = 44100.0, .f0 = kC6, .count = 21}}};
        for (const auto& cs : cases) {
            INFO("fs " << cs.fs << " f0 " << cs.f0);
            Feeder fd;
            fd.producer.prepare(cs.fs);
            Source src;
            src.fs = cs.fs;
            src.f0 = static_cast<double>(cs.f0);
            src.partials = {{0.5, 1}};
            ProfundumSpectrumFrame frame{};
            REQUIRE(runUntilFrame(fd, src, cs.f0, frame));
            CHECK(std::cmp_equal(frame.numPartials, cs.count));
            CHECK(frame.windowSamples == expectedWindow(cs.fs, static_cast<double>(cs.f0)));
            for (auto i = static_cast<std::size_t>(cs.count); i < Profundum::kSpectrumMaxPartials; ++i)
                CHECK(frame.partialPower[i] == 0.0f);
        }

        const std::array<float, 3> unusable{24000.0f, 0.0f, makeNaN()};
        for (std::size_t u = 0; u < unusable.size(); ++u) {
            INFO("unusable case " << u);
            Feeder fd;
            fd.producer.prepare(48000.0);
            Source src;
            src.partials = {{0.5, 1}};
            fd.feed(src, unusable[u]);
            ProfundumSpectrumFrame frame{};
            REQUIRE(fd.producer.takeFrame(frame));
            CHECK(frame.numPartials == 0);
            CHECK((frame.flags & Profundum::kFrameFlagSilent) != 0);
            CHECK(frame.f0Hz == 0.0f);
            CHECK(frame.sampleRate == 48000.0f);
            CHECK(frameFinite(frame));
            CHECK(fd.producer.windowsCompleted() == 0u);
        }
    }

    SECTION("CadenceOneFramePerWindow") {
        struct Case {
            float f0;
            std::uint32_t expected;
        };
        // floor(96000 / N): E1 N = 9320 -> 10; C3 N = round(384000 / 130.8128) -> 32.
        const std::array<Case, 2> cases{{{.f0 = kE1, .expected = 10u}, {.f0 = kC3, .expected = 32u}}};
        for (const auto& cs : cases) {
            INFO("f0 " << cs.f0);
            Feeder fd;
            fd.producer.prepare(48000.0);
            Source src;
            src.f0 = static_cast<double>(cs.f0);
            src.partials = {{0.5, 1}, {0.25, 2}};
            src.anchorAmp = 0.3;

            const std::size_t totalIntervals = 96000 / kInterval;  // 2 s
            std::uint32_t framesTaken = 0;
            std::uint32_t lastWindows = 0;
            bool advanceOk = true;
            for (std::size_t i = 0; i < totalIntervals; ++i) {
                fd.feed(src, cs.f0);
                if ((i + 1) % 16 == 0) {  // one 512-sample process() block
                    ProfundumSpectrumFrame frame{};
                    if (fd.producer.takeFrame(frame)) {
                        ++framesTaken;
                        CHECK(frameFinite(frame));
                    }
                    const std::uint32_t w = fd.producer.windowsCompleted();
                    if (w - lastWindows > 1u)
                        advanceOk = false;
                    lastWindows = w;
                }
            }
            CHECK(advanceOk);
            CHECK(framesTaken == fd.producer.windowsCompleted());
            const auto w = static_cast<long long>(fd.producer.windowsCompleted());
            CHECK(std::llabs(w - static_cast<long long>(cs.expected)) <= 1);
        }
    }

    SECTION("NonFiniteNeverPublished") {
        Feeder fd;
        fd.producer.prepare(48000.0);
        Source src;
        src.partials = {{0.5, 1}};
        src.anchorAmp = 0.3;
        const std::uint64_t n = expectedWindow(48000.0, static_cast<double>(kE1));
        const std::uint64_t nanAt = n + n / 2;  // inside the second window [N, 2N)
        const float nan = makeNaN();

        std::vector<ProfundumSpectrumFrame> frames;
        std::uint32_t windowsBeforeNanWindowEnd = 0;
        bool nanWindowAdvanced = false;
        const std::uint64_t total = 3 * n + 2 * kInterval;
        while (fd.pos < total) {
            src.fill(fd.pos, fd.c, fd.a);
            if (nanAt >= fd.pos && nanAt < fd.pos + kInterval)
                fd.c[static_cast<std::size_t>(nanAt - fd.pos)] = nan;
            const std::uint32_t before = fd.producer.windowsCompleted();
            fd.producer.feedInterval(std::span<const float>(fd.c), std::span<const float>(fd.c),
                                     std::span<const float>(fd.a), std::span<const float>(fd.a), kE1, 0, false,
                                     fd.pos);
            const bool containsSecondEnd = (2 * n > fd.pos) && (2 * n <= fd.pos + kInterval);
            if (containsSecondEnd) {
                windowsBeforeNanWindowEnd = before;
                nanWindowAdvanced = fd.producer.windowsCompleted() != before;
            }
            fd.pos += kInterval;
            ProfundumSpectrumFrame f{};
            if (fd.producer.takeFrame(f))
                frames.push_back(f);
        }
        CHECK(windowsBeforeNanWindowEnd == 1u);
        CHECK_FALSE(nanWindowAdvanced);
        CHECK(fd.producer.windowsCompleted() == 2u);  // windows 1 and 3 only
        CHECK(fd.producer.invalidWindows() == 1u);
        REQUIRE(frames.size() == 2u);
        for (const auto& f : frames) {
            CHECK(frameFinite(f));
            CHECK(f.windowEndSample != 2 * n);
        }
        CHECK(frames[0].windowEndSample == n);
        CHECK(frames[1].windowEndSample == 3 * n);
    }

    SECTION("TinyInputsStayNormal") {
        Feeder fd;
        fd.producer.prepare(48000.0);
        Source src;
        src.constant = 1.0e-38;
        const std::uint64_t n = expectedWindow(48000.0, static_cast<double>(kE1));
        std::size_t framesSeen = 0;
        while (fd.pos < 5 * n + kInterval) {
            fd.feed(src, kE1);
            ProfundumSpectrumFrame f{};
            if (fd.producer.takeFrame(f)) {
                ++framesSeen;
                CHECK(frameFinite(f));
                CHECK(zeroOrNormal(f.anchorPower));
                for (float p : f.partialPower) {
                    INFO("power bits " << std::bit_cast<std::uint32_t>(p));
                    CHECK(zeroOrNormal(p));
                }
            }
        }
        CHECK(framesSeen == 5u);
    }

    SECTION("SilenceThenRepublish") {
        Feeder fd;
        fd.producer.prepare(48000.0);
        Source src;
        src.partials = {{0.5, 1}};
        ProfundumSpectrumFrame frame{};
        REQUIRE(runUntilFrame(fd, src, kE1, frame));
        const std::uint32_t lastSeq = frame.sequence;

        fd.producer.feedSilence(fd.pos + kInterval);
        fd.pos += kInterval;
        ProfundumSpectrumFrame silent{};
        REQUIRE(fd.producer.takeFrame(silent));
        CHECK((silent.flags & Profundum::kFrameFlagSilent) != 0);
        CHECK(silent.numPartials == 0);
        CHECK(silent.sequence > lastSeq);
        const std::uint64_t firstSilentEnd = fd.pos;

        // 12000 samples = 0.25 s at 48 kHz = 375 intervals.
        int staged = 0;
        std::uint64_t stagedAt = 0;
        for (int i = 0; i < 400; ++i) {
            fd.producer.feedSilence(fd.pos + kInterval);
            fd.pos += kInterval;
            ProfundumSpectrumFrame f{};
            if (fd.producer.takeFrame(f)) {
                ++staged;
                stagedAt = fd.pos;
                CHECK((f.flags & Profundum::kFrameFlagSilent) != 0);
            }
        }
        CHECK(staged == 1);
        CHECK(stagedAt - firstSilentEnd == 12000u);
    }

    SECTION("PitchUnstableFlag") {
        for (const double drift : {2.0e-3, 5.0e-4}) {
            INFO("drift " << drift);
            Feeder fd;
            fd.producer.prepare(48000.0);
            Source src;
            src.partials = {{0.5, 1}};
            const double n = static_cast<double>(expectedWindow(48000.0, static_cast<double>(kE1)));
            ProfundumSpectrumFrame frame{};
            bool got = false;
            for (std::size_t i = 0; i < 1000 && !got; ++i) {
                const double ramp = std::min(1.0, static_cast<double>(fd.pos) / (n - kInterval));
                const auto f0 = static_cast<float>(static_cast<double>(kE1) * (1.0 + drift * ramp));
                fd.feed(src, f0);
                got = fd.producer.takeFrame(frame);
            }
            REQUIRE(got);
            const bool unstable = (frame.flags & Profundum::kFrameFlagPitchUnstable) != 0;
            CHECK(unstable == (drift > 1.0e-3));
        }
    }

    SECTION("FocusChangeRestartsWindow") {
        Feeder fd;
        fd.producer.prepare(48000.0);
        Source srcA;
        srcA.partials = {{0.5, 3}};
        Source srcB;
        srcB.partials = {{0.5, 5}};
        const std::uint64_t n = expectedWindow(48000.0, static_cast<double>(kE1));

        // Half a window on focus 0: nothing is published.
        while (fd.pos < n / 2) {
            fd.feed(srcA, kE1, 0);
            ProfundumSpectrumFrame f{};
            CHECK_FALSE(fd.producer.takeFrame(f));
        }
        const std::uint64_t switchAt = fd.pos;
        const std::uint32_t windowsAtSwitch = fd.producer.windowsCompleted();

        ProfundumSpectrumFrame frame{};
        bool got = false;
        for (std::size_t i = 0; i < 1000 && !got; ++i) {
            fd.feed(srcB, kE1, 1);
            got = fd.producer.takeFrame(frame);
        }
        REQUIRE(got);
        CHECK(windowsAtSwitch == 0u);
        CHECK(fd.producer.windowsCompleted() == 1u);
        CHECK(frame.focusVoice == 1);
        CHECK(frame.windowSamples == n);
        CHECK(frame.windowEndSample - frame.windowSamples == switchAt);
        CHECK(std::abs(dbRe(frame.partialPower[4], 0.25)) <= 0.05);
        CHECK(static_cast<double>(frame.partialPower[2]) <= 0.25 * 1.0e-8);
    }
}

TEST_CASE("Profundum_GoertzelBankSimdMatchesScalar", "[profundum][spectrum]") {
    // FR-075 / SC-013 (T040, ruled 2026-10-24): the Highway kernel equals a scalar reference of the same recurrence,
    // s[k] = x[k] + c_n s[k-1] - s[k-2], within 1e-12 relative (1e-12 absolute floor), for every count 1..64 and
    // segment length 1..32, starting from a non-zero state.
    constexpr std::size_t kMax = Profundum::kSpectrumMaxPartials;
    constexpr std::size_t kSeg = ProfundumSpectrumProducer::kIntervalSamples;
    std::mt19937 rng(20261024u);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::array<double, kSeg> xl{};
    std::array<double, kSeg> xr{};
    for (std::size_t k = 0; k < kSeg; ++k) {
        xl[k] = u(rng);
        xr[k] = u(rng);
    }
    const double w1 = 2.0 * std::numbers::pi * 41.2034 / 48000.0;
    std::array<double, kMax> c{};
    for (std::size_t n = 0; n < kMax; ++n)
        c[n] = 2.0 * std::cos(static_cast<double>(n + 1) * w1);
    std::array<double, kMax> init1L{};
    std::array<double, kMax> init2L{};
    std::array<double, kMax> init1R{};
    std::array<double, kMax> init2R{};
    for (std::size_t n = 0; n < kMax; ++n) {
        init1L[n] = u(rng);
        init2L[n] = u(rng);
        init1R[n] = u(rng);
        init2R[n] = u(rng);
    }
    const auto close = [](double a, double b) { return std::abs(a - b) <= 1.0e-12 * std::max(1.0, std::abs(b)); };
    std::size_t mismatches = 0;
    for (std::size_t count = 1; count <= kMax; ++count) {
        for (std::size_t m = 1; m <= kSeg; ++m) {
            auto s1L = init1L;
            auto s2L = init2L;
            auto s1R = init1R;
            auto s2R = init2R;
            auto r1L = init1L;
            auto r2L = init2L;
            auto r1R = init1R;
            auto r2R = init2R;
            Profundum::goertzelBankRun(xl.data(), xr.data(), m, c.data(), s1L.data(), s2L.data(), s1R.data(),
                                       s2R.data(), count);
            for (std::size_t n = 0; n < count; ++n) {
                for (std::size_t k = 0; k < m; ++k) {
                    const double l0 = xl[k] + c[n] * r1L[n] - r2L[n];
                    r2L[n] = r1L[n];
                    r1L[n] = l0;
                    const double q0 = xr[k] + c[n] * r1R[n] - r2R[n];
                    r2R[n] = r1R[n];
                    r1R[n] = q0;
                }
            }
            for (std::size_t n = 0; n < kMax; ++n) {
                const bool inRange = n < count;
                // In range: equal to the reference. Out of range: untouched.
                const bool ok = inRange ? close(s1L[n], r1L[n]) && close(s2L[n], r2L[n]) && close(s1R[n], r1R[n]) &&
                                              close(s2R[n], r2R[n])
                                        : s1L[n] == init1L[n] && s2L[n] == init2L[n] && s1R[n] == init1R[n] &&
                                              s2R[n] == init2R[n];
                if (!ok) {
                    ++mismatches;
                    if (mismatches <= 5u) {
                        INFO("count " << count << " m " << m << " partial " << n << ": s1L " << s1L[n] << " vs "
                                      << r1L[n] << ", s2L " << s2L[n] << " vs " << r2L[n]);
                        CHECK(ok);
                    }
                }
            }
        }
    }
    CHECK(mismatches == 0u);
}
