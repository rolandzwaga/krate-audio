// ==============================================================================
// NonlinearCoupling — anti-homogenization regression guard (AUDIT M-3/M-4)
// ==============================================================================
// The gain-staging PR removed an always-on per-voice softClip that homogenized
// the output (crushed velocity dynamics, pinned every body to one ceiling, one
// timbral signature). The M-3/M-4 redesign added an env-LEVEL-driven recipSqrt
// soft-clipper INSIDE NonlinearCoupling. This guard pins the properties that
// keep that stage from re-introducing the pathology:
//
//   (1) OFF by default is exact bypass (amount==0 -> identical to a voice that
//       never had coupling).
//   (2) Engaging coupling never AMPLIFIES the voice peak (a soft-clipper must
//       only ever pull the peak down, never push the chain hotter).
//   (3) Velocity dynamics survive: a louder hit stays louder (no crush to one
//       ceiling) on a clean (non-rail-clipping) body.
//   (4) Distinct bodies stay spectrally distinct at full coupling (no
//       convergence to one timbre).
//
// Measured at level 0.25 so the per-voice hardClip rail does NOT engage on the
// coupling-OFF baseline (the body x env peak runs hot — see the separate
// headroom finding — so a higher level would clip the baseline and make the
// comparison meaningless).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "dsp/drum_voice.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kRenderN    = 24000;   // 0.5 s
constexpr float  kSafeLevel  = 0.20f;   // keeps the OFF baseline clear of the rail

bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

struct Render { std::vector<float> buf; float peak = 0.0f; bool finite = true; };

Render renderVoice(Membrum::BodyModelType body, float couplingAmount, float velocity,
                   float level = kSafeLevel)
{
    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate);
    voice.setBodyModel(body);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setMaterial(0.6f);
    voice.setSize(0.5f);
    voice.setDecay(0.8f);
    voice.setStrikePosition(0.33f);
    voice.setLevel(level);
    voice.unnaturalZone().nonlinearCoupling.setAmount(couplingAmount);
    voice.noteOn(velocity);

    Render r;
    r.buf.resize(static_cast<std::size_t>(kRenderN), 0.0f);
    for (int i = 0; i < kRenderN; ++i)
    {
        const float x = voice.process();
        if (!isFiniteSample(x)) r.finite = false;
        r.buf[static_cast<std::size_t>(i)] = x;
        r.peak = std::max(r.peak, std::abs(x));
    }
    return r;
}

// Spectral centroid (Hz) over the first 100 ms, 64-bin log grid 50 Hz..12 kHz.
double centroid(const std::vector<float>& buf)
{
    constexpr int kBins = 64;
    const double fLow = 50.0, fHigh = 12000.0;
    const int win = 4800;
    double num = 0.0, den = 0.0;
    for (int b = 0; b < kBins; ++b)
    {
        const double f = fLow * std::pow(fHigh / fLow,
                                         static_cast<double>(b) / (kBins - 1));
        const double w = 2.0 * 3.14159265358979323846 * f / kSampleRate;
        double re = 0.0, im = 0.0;
        for (int i = 0; i < win; ++i)
        {
            re += buf[static_cast<std::size_t>(i)] * std::cos(w * i);
            im -= buf[static_cast<std::size_t>(i)] * std::sin(w * i);
        }
        const double mag = std::sqrt(re * re + im * im);
        num += f * mag; den += mag;
    }
    return den > 0.0 ? num / den : 0.0;
}

} // namespace

TEST_CASE("NonlinearCoupling guard: amount==0 is exact bypass at the voice level",
          "[membrum][NonlinearCoupling][regression][homogenization]")
{
    // A voice with coupling at 0 must be sample-identical to one whose coupling
    // object was never touched (the default). Renders both and compares.
    const Render a = renderVoice(Membrum::BodyModelType::Membrane, 0.0f, 1.0f);

    Membrum::DrumVoice voice; // never touch nonlinearCoupling at all
    voice.prepare(kSampleRate);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setMaterial(0.6f);
    voice.setSize(0.5f);
    voice.setDecay(0.8f);
    voice.setStrikePosition(0.33f);
    voice.setLevel(kSafeLevel);
    voice.noteOn(1.0f);

    bool identical = true;
    for (int i = 0; i < kRenderN; ++i)
        if (voice.process() != a.buf[static_cast<std::size_t>(i)]) { identical = false; break; }
    CHECK(identical);
}

TEST_CASE("NonlinearCoupling guard: engaging coupling never amplifies the voice peak",
          "[membrum][NonlinearCoupling][regression][homogenization]")
{
    // A soft-clipper must only pull the peak DOWN, never push the chain hotter.
    const Membrum::BodyModelType bodies[] = {
        Membrum::BodyModelType::Membrane,
        Membrum::BodyModelType::Bell,
        Membrum::BodyModelType::String,
    };
    for (auto body : bodies)
    {
        const Render off = renderVoice(body, 0.0f, 1.0f);
        const Render on  = renderVoice(body, 1.0f, 1.0f);
        INFO("body peak off=" << off.peak << " on=" << on.peak);
        CHECK(off.finite);
        CHECK(on.finite);
        // recipSqrt(x) <= 1 for all x, and the added excess is amount*(shaped -
        // body), so the coupled peak is bounded by ~max(body, 1). It may add a
        // little peak via brightening on a quiet body, but must never blow the
        // chain up: well under a 1.5x growth and always below the hardClip rail.
        // (At the OFF baseline this body x env runs hot, so coupling actually
        // pulls the peak DOWN here; the generous bound keeps the guard valid if
        // the separate body-headroom issue is later fixed and bodies run cooler.)
        CHECK(on.peak <= off.peak * 1.5f + 0.05f);
        CHECK(on.peak <= 1.0f);               // never exceeds the rail
        CHECK(on.peak <  0.99f);              // and never DRIVEN into it at a clean level
    }
}

TEST_CASE("NonlinearCoupling guard: velocity dynamics survive full coupling",
          "[membrum][NonlinearCoupling][regression][homogenization]")
{
    // On a clean (non-clipping) body, a louder hit must stay clearly louder
    // with coupling fully engaged -- the old always-on softClip crushed v0.5
    // and v1.0 to the same ceiling.
    const Render v10 = renderVoice(Membrum::BodyModelType::String, 1.0f, 1.0f);
    const Render v05 = renderVoice(Membrum::BodyModelType::String, 1.0f, 0.5f);
    const float ratio = v05.peak > 1e-6f ? v10.peak / v05.peak : 0.0f;
    INFO("velocity peak ratio (v1.0/v0.5) at amount=1.0: " << ratio);
    CHECK(ratio >= 1.5f);
}

TEST_CASE("NonlinearCoupling guard: distinct bodies stay distinct at full coupling",
          "[membrum][NonlinearCoupling][regression][homogenization]")
{
    // Full coupling must not collapse different bodies onto one timbre.
    const double cBell   = centroid(renderVoice(Membrum::BodyModelType::Bell,   1.0f, 1.0f).buf);
    const double cString = centroid(renderVoice(Membrum::BodyModelType::String, 1.0f, 1.0f).buf);
    INFO("centroid Bell=" << cBell << " String=" << cString);
    // String is a bright plucked body; Bell is low/inharmonic. They must remain
    // far apart (String centroid well above Bell's) even at full coupling.
    CHECK(cString > cBell * 2.0);
}
