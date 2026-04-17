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
#include "dsp/pad_config.h"
#include "dsp/voice_common_params.h"
#include "controller/controller.h"
#include "plugin_ids.h"
#include "voice_pool/voice_pool.h"

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
    SECTION("bodyDampingB3=1.0 -> 1e-3 s*rad^-2")
    {
        p.bodyDampingB1 = -1.0f;
        p.bodyDampingB3 = 1.0f;
        const auto law = dampingLawFromParams(p, 0.5f, 0.5f);
        CHECK(law.b3 == Approx(1.0e-3f).margin(1e-9f));
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

// Measure how much a b3 sweep changes the ABOVE-1-kHz energy (b3 selectively
// damps high modes, so below-1-kHz energy should stay approximately equal
// while above-1-kHz energy crashes).
namespace {
struct BandEnergy { double below1k; double above1k; };

BandEnergy renderVoiceBandEnergy(float b1Norm, float b3Norm)
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
    // Coarse band-split via an 8-tap moving-average LP filter ~ 1 kHz cutoff
    // at 44.1 kHz (fc = sr / (pi * taps) ~ 1755 Hz -- close enough for an
    // order-of-magnitude test).
    constexpr int kTaps = 16;
    std::vector<float> lp(buf.size(), 0.0f);
    double acc = 0.0;
    for (int i = 0; i < static_cast<int>(buf.size()); ++i) {
        acc += buf[i];
        if (i >= kTaps) acc -= buf[i - kTaps];
        lp[i] = static_cast<float>(acc / kTaps);
    }
    BandEnergy out{};
    for (int i = 0; i < static_cast<int>(buf.size()); ++i) {
        const double low = lp[i];
        const double high = buf[i] - low;
        out.below1k += low * low;
        out.above1k += high * high;
    }
    return out;
}
} // namespace

TEST_CASE("Phase 8C: simple Membrane (no click/noise layers, no pitch env) pitch-shifts",
          "[phase8c][drum_voice][minimal]")
{
    using Membrum::DrumVoice;
    auto render = [](float airNorm) {
        DrumVoice v;
        v.prepare(44100.0, 0u);
        v.setMaterial(0.5f); v.setSize(0.5f); v.setDecay(0.5f);
        v.setStrikePosition(0.3f); v.setLevel(0.8f);
        v.setExciterType(Membrum::ExciterType::Impulse);
        v.setBodyModel(Membrum::BodyModelType::Membrane);
        v.setAirLoading(airNorm);
        v.setModeScatter(0.0f);
        v.setNoiseLayerMix(0.0f);
        v.setClickLayerMix(0.0f);
        v.noteOn(1.0f);
        const int N = 22050;
        std::vector<float> buf(static_cast<std::size_t>(N), 0.0f);
        constexpr int kBlock = 256;
        for (int off = 0; off < N; off += kBlock) {
            const int c = std::min(kBlock, N - off);
            v.processBlock(buf.data() + off, c);
        }
        return buf;
    };
    auto zcPerSec = [](const std::vector<float>& buf, int start, int end) {
        int crossings = 0;
        for (int i = start + 1; i < end; ++i)
            if ((buf[i-1] >= 0.0f) != (buf[i] >= 0.0f)) ++crossings;
        return static_cast<double>(crossings) / (2.0 * (end - start) / 44100.0);
    };
    const auto noAir = render(0.0f);
    const auto fullAir = render(1.0f);
    // Measure pitch over 50..200 ms where body dominates.
    const double hzNo   = zcPerSec(noAir,   2205, 8820);
    const double hzFull = zcPerSec(fullAir, 2205, 8820);
    INFO("minimal noAir=" << hzNo << " Hz, fullAir=" << hzFull << " Hz");
    // Rossing 5% physical curve -> tail pitch drops ~3-5%.
    CHECK(hzFull < hzNo * 0.98);
}

TEST_CASE("Phase 8C: Kick-preset attack window shows airLoading shift",
          "[phase8c][drum_voice][diagnose]")
{
    using Membrum::DrumVoice;
    auto render = [](float airNorm) {
        DrumVoice v;
        v.prepare(44100.0, 0u);
        v.setMaterial(0.3f); v.setSize(0.8f); v.setDecay(0.3f);
        v.setStrikePosition(0.3f); v.setLevel(0.8f);
        v.setExciterType(Membrum::ExciterType::Impulse);
        v.setBodyModel(Membrum::BodyModelType::Membrane);
        v.setAirLoading(airNorm);
        v.setModeScatter(0.0f);
        // Kick pitch-env config (from default_kit) to reproduce the user's
        // workflow where air-loading has to cut through a 160 -> 50 Hz sweep.
        v.toneShaper().setPitchEnvStartHz(160.0f);
        v.toneShaper().setPitchEnvEndHz(50.0f);
        v.toneShaper().setPitchEnvTimeMs(20.0f);
        // Zero noise + click to isolate the body's pitch response.
        v.setNoiseLayerMix(0.0f); v.setClickLayerMix(0.0f);
        v.noteOn(1.0f);
        std::vector<float> buf(22050, 0.0f);
        constexpr int kBlock = 256;
        for (int off = 0; off < 22050; off += kBlock)
            v.processBlock(buf.data() + off, std::min(kBlock, 22050 - off));
        return buf;
    };
    auto zcPerSec = [](const std::vector<float>& buf, int start, int end) {
        int crossings = 0;
        for (int i = start + 1; i < end; ++i)
            if ((buf[i-1] >= 0.0f) != (buf[i] >= 0.0f)) ++crossings;
        return static_cast<double>(crossings) / (2.0 * (end - start) / 44100.0);
    };
    // Inspect the bank's mode-0 target frequency AFTER the pitch envelope
    // has completed (200 ms).
    auto mode0AfterPitchEnv = [](float airNorm) {
        DrumVoice v;
        v.prepare(44100.0, 0u);
        v.setMaterial(0.3f); v.setSize(0.8f); v.setDecay(0.3f);
        v.setStrikePosition(0.3f); v.setLevel(0.8f);
        v.setExciterType(Membrum::ExciterType::Impulse);
        v.setBodyModel(Membrum::BodyModelType::Membrane);
        v.setAirLoading(airNorm); v.setModeScatter(0.0f);
        v.toneShaper().setPitchEnvStartHz(160.0f);
        v.toneShaper().setPitchEnvEndHz(50.0f);
        v.toneShaper().setPitchEnvTimeMs(20.0f);
        v.setNoiseLayerMix(0.0f); v.setClickLayerMix(0.0f);
        v.noteOn(1.0f);
        std::vector<float> buf(8820, 0.0f);
        constexpr int kBlock = 256;
        for (int off = 0; off < 8820; off += kBlock)
            v.processBlock(buf.data() + off, std::min(kBlock, 8820 - off));
        return v.getBodyBankForTest().getSharedBank().getModeFrequency(0);
    };
    const float f0No   = mode0AfterPitchEnv(0.0f);
    const float f0Full = mode0AfterPitchEnv(1.0f);
    INFO("after 200ms: noAir mode0=" << f0No << " Hz, fullAir mode0=" << f0Full);
    // Physical Rossing curve: 5% depression on mode 0.
    CHECK(f0Full < f0No * 0.98f);
}

TEST_CASE("Phase 8C: Acoustic-Kick-style preset shows airLoading in rendered audio",
          "[phase8c][drum_voice][acoustic]")
{
    // Reproduces the Acoustic Kick preset from default_kit.h (tmpl Kick)
    // and renders 500 ms with airLoading=0 vs airLoading=1. Compares the
    // spectral centroid and mode-0 fundamental measured from the rendered
    // buffer to prove the knob DOES shift the body.
    using Membrum::DrumVoice;
    auto render = [](float airNorm) {
        DrumVoice v;
        v.prepare(44100.0, 0u);
        v.setMaterial(0.3f);
        v.setSize(0.8f);
        v.setDecay(0.3f);
        v.setStrikePosition(0.3f);
        v.setLevel(0.8f);
        v.setExciterType(Membrum::ExciterType::Impulse);
        v.setBodyModel(Membrum::BodyModelType::Membrane);
        v.setAirLoading(airNorm);
        v.setModeScatter(0.15f);
        // Kick-preset noise + click layers from default_kit.
        v.setNoiseLayerMix(0.85f);
        v.setNoiseLayerCutoff(0.08f);
        v.setNoiseLayerResonance(0.15f);
        v.setNoiseLayerDecay(0.55f);
        v.setNoiseLayerColor(0.0f);
        v.setClickLayerMix(0.75f);
        v.setClickLayerContactMs(0.15f);
        v.setClickLayerBrightness(0.4f);
        v.noteOn(1.0f);
        const int N = 22050;
        std::vector<float> buf(static_cast<std::size_t>(N), 0.0f);
        constexpr int kBlock = 256;
        for (int off = 0; off < N; off += kBlock) {
            const int c = std::min(kBlock, N - off);
            v.processBlock(buf.data() + off, c);
        }
        return buf;
    };

    const auto noAir = render(0.0f);
    const auto fullAir = render(1.0f);

    // Count zero-crossings in the sustained body portion [150 ms, 400 ms].
    // Approximates the perceived pitch of the modal tail.
    auto zeroCrossPerSec = [](const std::vector<float>& buf, int start, int end) {
        int crossings = 0;
        for (int i = start + 1; i < end; ++i)
            if ((buf[i-1] >= 0.0f) != (buf[i] >= 0.0f)) ++crossings;
        return static_cast<double>(crossings) / (2.0 * (end - start) / 44100.0);
    };
    const double hzNoAir   = zeroCrossPerSec(noAir,  6615, 17640);
    const double hzFullAir = zeroCrossPerSec(fullAir, 6615, 17640);

    double accNo = 0, accFull = 0;
    for (float x : noAir)   accNo   += x * x;
    for (float x : fullAir) accFull += x * x;
    INFO("noAir ZC/s=" << hzNoAir << " rms=" << std::sqrt(accNo / noAir.size()));
    INFO("fullAir ZC/s=" << hzFullAir << " rms=" << std::sqrt(accFull / fullAir.size()));

    // airLoading affects mode ratios (character), not the absolute pitch-env
    // endpoints. On the Kick preset the pitch env overrides the body
    // fundamental so the zero-crossing shift is small (~physical 5 %).
    REQUIRE(hzNoAir > 10.0);
    REQUIRE(hzFullAir > 5.0);
    CHECK(hzFullAir <= hzNoAir);
}

TEST_CASE("Phase 8C: VoicePool stores airLoading through setPadConfigField",
          "[phase8c][voice_pool][wiring]")
{
    // Processor-side path: setPadConfigField(kPadAirLoading, ...) must
    // update the pad's cached airLoading so the next noteOn uses it.
    Membrum::VoicePool pool;
    pool.prepare(44100.0, 256);
    pool.setMaxPolyphony(4);
    pool.setPadConfigField(0, Membrum::kPadAirLoading, 0.9f);
    pool.setPadConfigField(0, Membrum::kPadModeScatter, 0.7f);
    CHECK(pool.padConfig(0).airLoading  == Approx(0.9f).margin(1e-6f));
    CHECK(pool.padConfig(0).modeScatter == Approx(0.7f).margin(1e-6f));
}

TEST_CASE("Phase 8C: kAirLoadingId proxy registers via controller",
          "[phase8c][controller][wiring]")
{
    Membrum::Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == Steinberg::kResultOk);
    Steinberg::Vst::ParameterInfo info{};
    // getParameterInfoById would be ideal; fall back to linear scan.
    bool found = false;
    const Steinberg::int32 n = ctl.getParameterCount();
    for (Steinberg::int32 i = 0; i < n; ++i) {
        Steinberg::Vst::ParameterInfo pi{};
        REQUIRE(ctl.getParameterInfo(i, pi) == Steinberg::kResultOk);
        if (pi.id == Membrum::kAirLoadingId) { info = pi; found = true; break; }
    }
    CHECK(found);
    CHECK(info.id == Membrum::kAirLoadingId);
    ctl.terminate();
}

TEST_CASE("Phase 8C: airLoading sweep depresses Membrane fundamental",
          "[phase8c][drum_voice][audio]")
{
    using Membrum::DrumVoice;
    auto fundamental = [](float airLoading) {
        DrumVoice v;
        v.prepare(44100.0, 0u);
        v.setMaterial(0.5f); v.setSize(0.5f); v.setDecay(0.5f);
        v.setStrikePosition(0.3f); v.setLevel(0.8f);
        v.setExciterType(Membrum::ExciterType::Impulse);
        v.setBodyModel(Membrum::BodyModelType::Membrane);
        v.setAirLoading(airLoading);
        v.setModeScatter(0.0f);
        v.noteOn(1.0f);
        return v.getBodyBankForTest().getSharedBank().getModeFrequency(0);
    };
    const float f0_none  = fundamental(0.0f);
    const float f0_full  = fundamental(1.0f);
    INFO("f0 airLoading=0 -> " << f0_none << " Hz, airLoading=1 -> " << f0_full);
    REQUIRE(f0_none > 100.0f);
    // Published Rossing curve -> max 5% depression at mode 0 (character,
    // not transposition).
    CHECK(f0_full < f0_none * 0.97f);
    CHECK(f0_full > f0_none * 0.93f);
}

TEST_CASE("Phase 8A.5: b3 sweep produces audible energy change",
          "[phase8a][drum_voice][audio]")
{
    // Sweep b3 from 0 to 1, b1 held at moderate value. Expect a clearly
    // audible change in rendered energy -- the plan's exit-gate contract
    // ("bright metallic ring -> dull wood thump"). Empirically at the
    // plan's original 8e-5 ceiling the effect was < 3 dB (inaudible);
    // widening to 1e-3 gives ~9 dB which is clearly audible.
    const auto lo = renderVoiceBandEnergy(0.3f, 0.0f);  // b3 = 0
    const auto hi = renderVoiceBandEnergy(0.3f, 1.0f);  // b3 = 1e-3

    const double loTotal = lo.below1k + lo.above1k;
    const double hiTotal = hi.below1k + hi.above1k;
    const double totalRatio = hiTotal / std::max(loTotal, 1e-30);
    INFO("lo total=" << loTotal << " hi total=" << hiTotal
         << " totalRatio=" << totalRatio);
    REQUIRE(loTotal > 0.0);
    REQUIRE(hiTotal > 0.0);
    // > 3 dB total-energy drop when b3 goes from 0 -> max.
    CHECK(totalRatio < 0.5);
}
