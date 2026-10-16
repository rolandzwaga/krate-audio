// ==============================================================================
// Layer 2: Processor Tests - Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// KeytrackedTilt (plan S1 C-1, C-13, S6.1; tasks T004 epoch 2, deviation 11).
// Holds: unity at the pivot, flat at Tone 0.5 (SC-028(a) part), keytracked
// response (SC-028(b) part), tone direction / pivot symmetry, clamp and reset.
//
// Responses are measured by driving a 1 s sine through a copy of the configured
// object and taking the RMS ratio over whole periods of the last 0.5 s.
// Epoch 2 tolerances (plan C-25, spec Clarifications "Group 2 task-level
// tolerances"): pivot unity +-0.05 dB, keytracking 0.1 dB per harmonic,
// symmetry over x = f / (2 f0) in [1/8, 8] centred on the pivot.
// Model table: specs/profundum-phase5-shape-character/evidence/run-log/T004.txt
// ==============================================================================

#include <krate/dsp/processors/keytracked_tilt.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using Krate::DSP::KeytrackedTilt;

namespace {

constexpr float kC1 = 32.703196f;
constexpr float kC2 = 65.406391f;
constexpr float kC3 = 130.81278f;
constexpr double kTwoPiD = 6.283185307179586;

/// xorshift32 white noise in [-1, 1).
struct XorShift32 {
    std::uint32_t s;
    float next() noexcept {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<float>(static_cast<double>(s) / 2147483648.0 - 1.0);
    }
};

KeytrackedTilt makeTilt(double rate, float k, float maxDb, float f0, float tone) {
    KeytrackedTilt t;
    t.prepare(rate);
    t.configure(k, maxDb);
    t.setFundamentalHz(f0);
    t.setTone(tone);
    return t;
}

/// Gain (dB) at freqHz: 1 s sine through a copy of `configured`, RMS ratio over
/// whole periods of the last 0.5 s.
double gainDb(const KeytrackedTilt& configured, double rate, double freqHz) {
    KeytrackedTilt t = configured;
    t.reset();
    const auto total = static_cast<std::size_t>(std::lround(rate));
    const double period = rate / freqHz;
    const double periods = std::floor(static_cast<double>(total / 2) / period);
    const auto window = static_cast<std::size_t>(std::lround(periods * period));
    const std::size_t start = total - window;
    double sumIn = 0.0;
    double sumOut = 0.0;
    for (std::size_t n = 0; n < total; ++n) {
        const auto x = static_cast<float>(std::sin(kTwoPiD * freqHz * static_cast<double>(n) / rate));
        const float y = t.process(x);
        if (n >= start) {
            sumIn += static_cast<double>(x) * static_cast<double>(x);
            sumOut += static_cast<double>(y) * static_cast<double>(y);
        }
    }
    return 10.0 * std::log10(sumOut / sumIn);
}

} // namespace

TEST_CASE("KeytrackedTilt_UnityAtPivot", "[processors][profundum][shape-character-parts]") {
    // plan S1 C-1, S6.1; epoch 2 ruling (C-25): +-0.05 dB, the matched-pole one-pole is not
    // power-complementary (0.017 dB at C2 fully tilted); SC-028(a) gates Tone 0.5, exact.
    constexpr double rate = 192000.0;
    for (const float tone : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const KeytrackedTilt t = makeTilt(rate, 2.0f, 12.0f, kC2, tone);
        const double g = gainDb(t, rate, 2.0 * static_cast<double>(kC2));
        INFO("tone " << tone << " gain at pivot " << g << " dB");
        CHECK(std::abs(g) <= 0.05);
        CHECK(t.pivotHz() == 2.0f * kC2);
        CHECK(t.tone() == tone);
    }

    KeytrackedTilt t = makeTilt(rate, 2.0f, 12.0f, kC2, 0.5f);
    CHECK(t.tone() == 0.5f);
    t.setTone(0.3f);
    CHECK(t.tone() == 0.3f);
    t.setFundamentalHz(kC3);
    CHECK(t.pivotHz() == 2.0f * kC3);
}

TEST_CASE("KeytrackedTilt_FlatAtNeutral", "[processors][profundum][shape-character-parts]") {
    // SC-028(a) part: at Tone 0.5 gL = gH = 1, y = lp + (x - lp).
    constexpr double rate = 192000.0;
    KeytrackedTilt t = makeTilt(rate, 2.0f, 12.0f, kC2, 0.5f);
    XorShift32 rng{0x1234u};
    const auto n = static_cast<std::size_t>(rate);
    std::vector<float> x(n);
    for (auto& v : x)
        v = rng.next();
    float maxX = 0.0f;
    for (const float v : x)
        maxX = std::max(maxX, std::abs(v));
    float maxErr = 0.0f;
    for (const float v : x)
        maxErr = std::max(maxErr, std::abs(t.process(v) - v));
    INFO("max |y - x| " << maxErr << " max |x| " << maxX);
    CHECK(maxErr <= 1.0e-6f * maxX);
}

TEST_CASE("KeytrackedTilt_Keytracked", "[processors][profundum][shape-character-parts]") {
    // SC-028(b) part; epoch 2 ruling (C-25): 0.1 dB per n (model worst 0.0835 dB at n = 16, Tone 0).
    constexpr double rate = 192000.0;
    for (const float tone : {0.0f, 0.25f, 0.75f, 1.0f}) {
        const KeytrackedTilt lo = makeTilt(rate, 2.0f, 12.0f, kC1, tone);
        const KeytrackedTilt hi = makeTilt(rate, 2.0f, 12.0f, kC3, tone);
        for (int h = 1; h <= 16; ++h) {
            const double gLo = gainDb(lo, rate, h * static_cast<double>(kC1));
            const double gHi = gainDb(hi, rate, h * static_cast<double>(kC3));
            INFO("tone " << tone << " n " << h << " C1 " << gLo << " dB, C3 " << gHi << " dB");
            CHECK(std::abs(gLo - gHi) <= 0.1);
        }
    }
}

TEST_CASE("KeytrackedTilt_ToneDirection", "[processors][profundum][shape-character-parts]") {
    constexpr double rate = 192000.0;
    const double f0 = static_cast<double>(kC2);

    for (const float tone : {0.0f, 0.25f, 0.75f, 1.0f}) {
        const KeytrackedTilt t = makeTilt(rate, 2.0f, 12.0f, kC2, tone);
        std::array<double, 16> g{};
        for (int h = 1; h <= 16; ++h)
            g[static_cast<std::size_t>(h - 1)] = gainDb(t, rate, h * f0);
        for (std::size_t i = 1; i < g.size(); ++i) {
            INFO("tone " << tone << " n " << i << " -> " << (i + 1) << ": " << g[i - 1] << " -> " << g[i] << " dB");
            if (tone > 0.5f)
                CHECK(g[i] > g[i - 1]);
            else
                CHECK(g[i] < g[i - 1]);
        }
    }

    double previous = -1.0e9;
    for (const float tone : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const double g16 = gainDb(makeTilt(rate, 2.0f, 12.0f, kC2, tone), rate, 16.0 * f0);
        INFO("tone " << tone << " H(16 f0) " << g16 << " dB, previous " << previous);
        CHECK(g16 > previous);
        previous = g16;
    }

    // Epoch 2 ruling: H(x, t) = H(1/x, 1 - t) with x = f / (2 f0), so the span is taken over
    // x in [1/8, 8] (f = f0 / 4 ... 16 f0), centred on the pivot.
    const auto span = [&](float tone) {
        const KeytrackedTilt t = makeTilt(rate, 2.0f, 12.0f, kC2, tone);
        return gainDb(t, rate, 16.0 * f0) - gainDb(t, rate, f0 / 4.0);
    };
    const double span1 = span(1.0f);
    const double span0 = span(0.0f);
    INFO("span Tone 1 " << span1 << " dB, span Tone 0 " << span0 << " dB");
    // Bar 0.06 dB (Group 2 gate ruling, 2026-10-15): the matched-pole one-pole reads 0.0524 dB here; the analog
    // prototype is exactly symmetric. Recorded in plan C-25.
    CHECK(std::abs(span1 + span0) <= 0.06);
}

TEST_CASE("KeytrackedTilt_ClampAndReset", "[processors][profundum][shape-character-parts]") {
    constexpr double rate = 88200.0;
    KeytrackedTilt t = makeTilt(rate, 2.0f, 12.0f, kC2, 1.0f);
    XorShift32 rng{0x1234u};
    // 20 kHz (task cell): pivot 40 kHz, just under OnePoleLP's 0.495 * rate = 43659 Hz.
    // 22 kHz: pivot 44 kHz, above it, so the clamp itself is exercised.
    for (const float f0 : {20000.0f, 22000.0f}) {
        t.setFundamentalHz(f0);
        CHECK(t.pivotHz() == 2.0f * f0);
        bool finite = true;
        for (std::size_t n = 0; n < static_cast<std::size_t>(rate); ++n)
            finite = finite && Krate::DSP::detail::isFinite(t.process(rng.next()));
        INFO("f0 " << f0);
        CHECK(finite);
    }
    t.reset();
    CHECK(t.process(0.0f) == 0.0f);
}
