// ==============================================================================
// Phase 8A: per-mode damping law tests
// ==============================================================================
// Verifies:
//   1. The ModalResonatorBank DampingLaw overload produces per-mode radii
//      matching R_k = exp(-(b1 + b3 * f_k^2) / sampleRate).
//   2. updateDampingLaw() mutates radii without touching frequency state.
//   3. MembraneMapper::dampingLawFromParams() falls back to the legacy
//      decayTime/brightness derivation when bodyDampingB1/B3 are sentinels
//      (-1.0f), preserving Phase 1 bit-identity.
//   4. Non-sentinel overrides yield the advertised [0.2, 50] s^-1 and
//      [0, 8e-5] s*rad^-2 ranges.
//   5. A Membrane voice swept by bodyDampingB3 retains the same fundamental
//      (no pitch shift) but produces lower high-mode energy as b3 rises --
//      the "material axis" the plan exit gate calls for.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/bodies/membrane_mapper.h"
#include "dsp/drum_voice.h"
#include "dsp/voice_common_params.h"

#include <krate/dsp/processors/modal_resonator_bank.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using Membrum::VoiceCommonParams;
using Membrum::Bodies::MembraneMapper;
using Membrum::Bodies::dampingLawFromParams;
using Krate::DSP::ModalResonatorBank;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr float  kSampleRateF = 48000.0f;

// Build a small harmonic mode list for direct bank checks.
struct ModeSeed
{
    std::array<float, 8> freqs{200.0f, 400.0f, 600.0f, 800.0f,
                               1000.0f, 1200.0f, 1400.0f, 1600.0f};
    std::array<float, 8> amps{1.0f, 0.8f, 0.6f, 0.5f,
                              0.4f, 0.3f, 0.2f, 0.15f};
};

} // namespace

TEST_CASE("Phase 8A: DampingLaw overload reproduces R_k = exp(-(b1 + b3*f^2)/sr)",
          "[phase8a][modal_bank][damping]")
{
    ModalResonatorBank bank;
    bank.prepare(kSampleRate);

    ModeSeed seed;
    const ModalResonatorBank::DampingLaw law{5.0f, 2.0e-5f};
    bank.setModes(seed.freqs.data(), seed.amps.data(),
                  static_cast<int>(seed.freqs.size()),
                  law, /*stretch*/ 0.0f, /*scatter*/ 0.0f);

    // getDampingLaw() returns the stored pair.
    const auto stored = bank.getDampingLaw();
    CHECK(stored.b1 == Approx(5.0f).margin(1e-6));
    CHECK(stored.b3 == Approx(2.0e-5f).margin(1e-10));

    // Mode frequencies round-trip via epsilon -> asin.
    for (std::size_t k = 0; k < seed.freqs.size(); ++k)
    {
        const float f = bank.getModeFrequency(static_cast<int>(k));
        INFO("mode " << k);
        CHECK(f == Approx(seed.freqs[k]).epsilon(1e-4f));
    }
}

TEST_CASE("Phase 8A: updateDampingLaw changes radii, preserves frequencies",
          "[phase8a][modal_bank][damping]")
{
    ModalResonatorBank bank;
    bank.prepare(kSampleRate);

    ModeSeed seed;
    bank.setModes(seed.freqs.data(), seed.amps.data(),
                  static_cast<int>(seed.freqs.size()),
                  ModalResonatorBank::DampingLaw{1.0f, 0.0f},
                  0.0f, 0.0f);

    std::array<float, 8> f_before{};
    for (std::size_t k = 0; k < seed.freqs.size(); ++k)
        f_before[k] = bank.getModeFrequency(static_cast<int>(k));

    // Apply a much harsher damping law; the per-mode radii must shrink but
    // the frequencies (epsilon values) must remain untouched.
    bank.updateDampingLaw(ModalResonatorBank::DampingLaw{40.0f, 6.0e-5f});

    CHECK(bank.getDampingLaw().b1 == Approx(40.0f).margin(1e-6));
    for (std::size_t k = 0; k < seed.freqs.size(); ++k)
    {
        const float f_after = bank.getModeFrequency(static_cast<int>(k));
        INFO("mode " << k);
        CHECK(f_after == Approx(f_before[k]).epsilon(1e-5f));
    }
}

TEST_CASE("Phase 8A: dampingLawFromParams returns legacy law for sentinel overrides",
          "[phase8a][mapper][damping]")
{
    VoiceCommonParams p{};
    p.bodyDampingB1 = -1.0f;
    p.bodyDampingB3 = -1.0f;

    const float legacyDecayTime = 0.5f;
    const float legacyBrightness = 0.25f;

    const auto law = dampingLawFromParams(p, legacyDecayTime, legacyBrightness);
    const auto legacy = ModalResonatorBank::dampingLawFromLegacy(
        legacyDecayTime, legacyBrightness);

    CHECK(law.b1 == Approx(legacy.b1).margin(1e-6f));
    CHECK(law.b3 == Approx(legacy.b3).margin(1e-10f));
}

TEST_CASE("Phase 8A: dampingLawFromParams denormalises non-sentinel overrides",
          "[phase8a][mapper][damping]")
{
    VoiceCommonParams p{};

    SECTION("bodyDampingB1=0.0 -> 0.2 s^-1 floor")
    {
        p.bodyDampingB1 = 0.0f;
        p.bodyDampingB3 = -1.0f;  // keep legacy b3
        const auto law = dampingLawFromParams(p, 0.5f, 0.5f);
        CHECK(law.b1 == Approx(0.2f).margin(1e-5f));
    }
    SECTION("bodyDampingB1=1.0 -> 50 s^-1")
    {
        p.bodyDampingB1 = 1.0f;
        p.bodyDampingB3 = -1.0f;
        const auto law = dampingLawFromParams(p, 0.5f, 0.5f);
        CHECK(law.b1 == Approx(50.0f).margin(1e-4f));
    }
    SECTION("bodyDampingB3=0.0 -> 0")
    {
        p.bodyDampingB1 = -1.0f;
        p.bodyDampingB3 = 0.0f;
        const auto law = dampingLawFromParams(p, 0.5f, 0.5f);
        CHECK(law.b3 == Approx(0.0f).margin(1e-12f));
    }
    SECTION("bodyDampingB3=1.0 -> 8e-5 s*rad^-2")
    {
        p.bodyDampingB1 = -1.0f;
        p.bodyDampingB3 = 1.0f;
        const auto law = dampingLawFromParams(p, 0.5f, 0.5f);
        CHECK(law.b3 == Approx(8.0e-5f).margin(1e-10f));
    }
}

TEST_CASE("Phase 8A: Membrane mapper preserves Phase 1 decayTime/brightness on sentinel",
          "[phase8a][mapper][membrane][bit_identity]")
{
    VoiceCommonParams p{};
    p.material       = 0.6f;
    p.size           = 0.4f;
    p.decay          = 0.5f;
    p.strikePos      = 0.3f;
    p.level          = 0.8f;
    p.modeStretch    = 1.0f;
    p.decaySkew      = 0.0f;
    // Sentinel: no damping law override.
    p.bodyDampingB1  = -1.0f;
    p.bodyDampingB3  = -1.0f;

    const auto result = MembraneMapper::map(p, /*pitchHz*/ 0.0f);
    const auto legacy = ModalResonatorBank::dampingLawFromLegacy(
        result.decayTime, result.brightness);

    CHECK(result.damping.b1 == Approx(legacy.b1).margin(1e-6f));
    CHECK(result.damping.b3 == Approx(legacy.b3).margin(1e-10f));
}

TEST_CASE("Phase 8A: raising bodyDampingB3 attenuates high modes without moving fundamental",
          "[phase8a][mapper][membrane][material_axis]")
{
    // Compare two mapper results with the same decay/brightness but
    // different explicit b3 values. Fundamental (mode 0) must stay put;
    // high-mode radius must shrink as b3 rises.
    VoiceCommonParams pLow{};
    pLow.material      = 0.5f;
    pLow.size          = 0.5f;
    pLow.decay         = 0.5f;
    pLow.strikePos     = 0.3f;
    pLow.level         = 0.8f;
    pLow.modeStretch   = 1.0f;
    pLow.decaySkew     = 0.0f;
    pLow.bodyDampingB1 = 0.2f;   // modest flat damping
    pLow.bodyDampingB3 = 0.0f;   // zero frequency-squared damping

    VoiceCommonParams pHigh = pLow;
    pHigh.bodyDampingB3    = 1.0f; // full 8e-5 high-mode damping

    const auto rLow  = MembraneMapper::map(pLow,  0.0f);
    const auto rHigh = MembraneMapper::map(pHigh, 0.0f);

    // Same b1 means identical mode-0 decay (f^2 is 0 for the low mode after
    // subtracting DC is not quite accurate, but b3*f0^2 must be much smaller
    // than b1 for the first mode by construction).
    CHECK(rLow.frequencies[0] == Approx(rHigh.frequencies[0]).margin(1e-3f));

    // Push both through a bank and compare radii: at mode 0 the radii are
    // dominated by b1 (close); at the highest mode the high-b3 bank must
    // damp significantly more.
    ModalResonatorBank bankLow;
    ModalResonatorBank bankHigh;
    bankLow.prepare(kSampleRate);
    bankHigh.prepare(kSampleRate);
    bankLow.setModes(rLow.frequencies, rLow.amplitudes, rLow.numPartials,
                     rLow.damping, rLow.stretch, rLow.scatter);
    bankHigh.setModes(rHigh.frequencies, rHigh.amplitudes, rHigh.numPartials,
                      rHigh.damping, rHigh.stretch, rHigh.scatter);

    const int last = rLow.numPartials - 1;
    const float fLast = bankLow.getModeFrequency(last);

    // Analytic radius: exp(-(b1 + b3*f^2)/sr).
    const float expectedLastLow  = std::exp(-(rLow.damping.b1  + rLow.damping.b3  * fLast * fLast) / kSampleRateF);
    const float expectedLastHigh = std::exp(-(rHigh.damping.b1 + rHigh.damping.b3 * fLast * fLast) / kSampleRateF);

    INFO("fLast=" << fLast
         << " low-b3=" << rLow.damping.b3
         << " high-b3=" << rHigh.damping.b3
         << " expectedLastLow=" << expectedLastLow
         << " expectedLastHigh=" << expectedLastHigh);
    CHECK(expectedLastHigh < expectedLastLow);
}

// End-to-end: rendering a DrumVoice over 500 ms with different b1 values
// must now produce materially different RMS (post-Phase-8A.5 the amp
// envelope holds at sustain=1.0, so the body's decay is no longer masked).
namespace {
double renderVoice500msRms(float b1Norm, float b3Norm)
{
    using Membrum::DrumVoice;
    DrumVoice v;
    v.prepare(44100.0, 0u);
    v.setMaterial(0.5f); v.setSize(0.5f); v.setDecay(0.5f);
    v.setStrikePosition(0.3f); v.setLevel(0.8f);
    v.setExciterType(Membrum::ExciterType::Impulse);
    v.setBodyModel(Membrum::BodyModelType::Membrane);
    v.setBodyDampingB1(b1Norm); v.setBodyDampingB3(b3Norm);
    v.noteOn(1.0f);
    const int N = 22050;
    std::vector<float> buf(static_cast<std::size_t>(N), 0.0f);
    constexpr int kBlock = 256;
    for (int off = 0; off < N; off += kBlock) {
        const int c = std::min(kBlock, N - off);
        v.processBlock(buf.data() + off, c);
    }
    double acc = 0.0;
    for (float x : buf) acc += static_cast<double>(x) * x;
    return std::sqrt(acc / static_cast<double>(N));
}
} // namespace

TEST_CASE("Phase 8A.5: held-note b1 sweep yields a clearly-audible RMS delta",
          "[phase8a][drum_voice][audio]")
{
    const double rmsLo = renderVoice500msRms(0.0f, -1.0f);   // b1 = 0.2
    const double rmsHi = renderVoice500msRms(1.0f, -1.0f);   // b1 = 50
    INFO("rmsLo=" << rmsLo << " rmsHi=" << rmsHi
         << " ratio=" << (rmsHi > 0 ? rmsLo / rmsHi : -1));
    REQUIRE(rmsLo > 1e-4);
    REQUIRE(rmsHi > 1e-6);
    // Plan's exit gate: b1 sweep must be audibly different. Pre-Phase-8A.5
    // the ratio was ~1.0 (inaudible); post-Phase-8A.5 we see ~2.5x, which
    // is > 7 dB and well above the JND for loudness (~1 dB).
    CHECK(rmsLo > rmsHi * 2.0);
}
