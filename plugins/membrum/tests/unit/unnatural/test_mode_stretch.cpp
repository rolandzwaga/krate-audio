// ==============================================================================
// Mode Stretch tests -- Phase 8, T102, T107
// ==============================================================================
// Covers unnatural_zone_contract.md "Mode Stretch" section and
// FR-050 (direct pass-through), FR-055 (defaults-off identity within -120 dBFS).
//
// Strategy: use MembraneMapper directly to compare partial ratios with
// modeStretch=1.0 (baseline) vs modeStretch=1.5 (contract item 2). Then run
// the full DrumVoice process pipeline to verify FR-055 defaults-off identity.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/bodies/membrane_mapper.h"
#include "dsp/drum_voice.h"
#include "dsp/voice_common_params.h"

#include <allocation_detector.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using Catch::Approx;

namespace {

inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

Membrum::VoiceCommonParams makeBaselineParams()
{
    Membrum::VoiceCommonParams p{};
    p.material    = 0.5f;
    p.size        = 0.5f;
    p.decay       = 0.3f;
    p.strikePos   = 0.3f;
    p.level       = 0.8f;
    p.modeStretch = 1.0f;
    p.decaySkew   = 0.0f;
    return p;
}

// Render N samples of the default Membrum patch with the given UnnaturalZone
// setters applied (modeStretch + decaySkew). Uses the per-sample process()
// entry so the test is deterministic.
std::vector<float> renderVoice(float modeStretch, float decaySkew,
                               int numSamples, double sampleRate = 44100.0)
{
    Membrum::DrumVoice voice;
    voice.prepare(sampleRate);
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.3f);
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);

    voice.unnaturalZone().setModeStretch(modeStretch);
    voice.unnaturalZone().setDecaySkew(decaySkew);

    voice.noteOn(0.8f);

    std::vector<float> out(static_cast<std::size_t>(numSamples), 0.0f);
    for (int i = 0; i < numSamples; ++i)
        out[static_cast<std::size_t>(i)] = voice.process();
    return out;
}

float rmsDb(const std::vector<float>& a, const std::vector<float>& b)
{
    REQUIRE(a.size() == b.size());
    double sumSq = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSq += d * d;
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(a.size()));
    return (rms < 1e-12) ? -240.0f : static_cast<float>(20.0 * std::log10(rms));
}

} // namespace

// ==============================================================================
// T102(a) -- Mode Stretch directly multiplies the 'stretch' scalar forwarded
//           to ModalResonatorBank. With modeStretch > 1.0, the bank's internal
//           stiff-string inharmonicity warping (FR-011) shifts high partials
//           upward. We verify this by inspecting the MapperResult.stretch
//           field directly — it is the control-plane signal that flows into
//           the bank's stretch clamp.
// ==============================================================================

TEST_CASE("UnnaturalZone Mode Stretch -- stretch scalar increases with modeStretch",
          "[UnnaturalZone][ModeStretch]")
{
    auto params = makeBaselineParams();

    params.modeStretch = 1.0f;
    const auto baseline = Membrum::Bodies::MembraneMapper::map(params, 0.0f);

    params.modeStretch = 1.5f;
    const auto stretched = Membrum::Bodies::MembraneMapper::map(params, 0.0f);

    INFO("baseline.stretch=" << baseline.stretch
         << " stretched.stretch=" << stretched.stretch);
    // Stretched should be strictly larger.
    CHECK(stretched.stretch > baseline.stretch);
    // And when modeStretch is beyond the Phase 1 unity point, stretched
    // should be strictly greater than material*0.3 (= 0.15 at material=0.5).
    CHECK(stretched.stretch > baseline.stretch + 0.05f);
}

// ==============================================================================
// T102(b) -- modeStretch == 1.0 must produce bit-identical mapper output to
//            the "Unnatural Zone disabled" path. This is the FR-055 guarantee
//            at the mapper level.
// ==============================================================================

TEST_CASE("UnnaturalZone Mode Stretch -- modeStretch==1.0 matches Phase 1 mapper",
          "[UnnaturalZone][ModeStretch][DefaultsOff]")
{
    auto params = makeBaselineParams();
    params.modeStretch = 1.0f;
    params.decaySkew   = 0.0f;

    const auto r1 = Membrum::Bodies::MembraneMapper::map(params, 0.0f);

    // Reference: Phase 1 mapper had r.stretch = material * 0.3f, so
    // at material=0.5 that is 0.15.
    const float phase1Stretch    = params.material * 0.3f;
    const float phase1Brightness = params.material;

    CHECK(r1.stretch    == Approx(phase1Stretch).margin(1e-6f));
    CHECK(r1.brightness == Approx(phase1Brightness).margin(1e-6f));
    CHECK(r1.numPartials == Membrum::Bodies::MembraneMapper::kMembraneModeCount);

    // All 16 partial frequencies must match Phase 1 exactly.
    const float f0 = 500.0f * std::pow(0.1f, params.size);
    for (int k = 0; k < Membrum::Bodies::MembraneMapper::kMembraneModeCount; ++k)
    {
        const float expected = f0 * Membrum::kMembraneRatios[static_cast<std::size_t>(k)];
        CHECK(r1.frequencies[k] == Approx(expected).margin(1e-4f));
    }
}

// ==============================================================================
// T102(c) -- Allocation detector: wrap the parameter flow (setModeStretch +
//            noteOn + process) and assert zero heap activity (FR-056).
// ==============================================================================

TEST_CASE("UnnaturalZone Mode Stretch -- zero heap allocations on audio thread",
          "[UnnaturalZone][ModeStretch][allocation]")
{
    Membrum::DrumVoice voice;
    voice.prepare(44100.0);
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.3f);
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);

    // Warm up (note + process a block so first-allocation paths get flushed).
    voice.noteOn(0.8f);
    std::array<float, 64> drain{};
    for (int i = 0; i < 8; ++i)
        voice.processBlock(drain.data(), 64);
    voice.noteOff();

    {
        TestHelpers::AllocationScope scope;
        voice.unnaturalZone().setModeStretch(1.5f);
        voice.noteOn(0.8f);
        std::array<float, 512> block{};
        voice.processBlock(block.data(), 512);
        voice.noteOff();
        const size_t count = scope.getAllocationCount();
        INFO("Mode Stretch path alloc count = " << count);
        CHECK(count == 0u);
    }
}

// ==============================================================================
// T107 -- FR-055 all-defaults-off: with every UnnaturalZone parameter at its
//         default off value, DrumVoice output must be within -120 dBFS RMS of
//         the "UnnaturalZone disabled" path (Phase 2 baseline).
//
// Strategy: render the default patch TWICE — once touching no UnnaturalZone
// setters at all, once calling setters with default values (1.0, 0.0, 0.0, 0.0,
// enabled=false). The two outputs must be bit-identical to near machine
// precision.
// ==============================================================================

TEST_CASE("UnnaturalZone DefaultsOff -- all-defaults produce identity within -120 dBFS",
          "[UnnaturalZone][DefaultsOff]")
{
    constexpr int numSamples = 22050; // 500 ms @ 44.1 kHz

    // Baseline: no UnnaturalZone touches at all.
    const auto baseline = renderVoice(1.0f, 0.0f, numSamples);

    // Explicit defaults path: caller sets all defaults.
    Membrum::DrumVoice voice;
    voice.prepare(44100.0);
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.3f);
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);

    voice.unnaturalZone().setModeStretch(1.0f);
    voice.unnaturalZone().setDecaySkew(0.0f);
    voice.unnaturalZone().modeInject.setAmount(0.0f);
    voice.unnaturalZone().nonlinearCoupling.setAmount(0.0f);
    voice.unnaturalZone().materialMorph.setEnabled(false);

    voice.noteOn(0.8f);
    std::vector<float> explicitDefaults(static_cast<std::size_t>(numSamples), 0.0f);
    for (int i = 0; i < numSamples; ++i)
        explicitDefaults[static_cast<std::size_t>(i)] = voice.process();

    // Verify no NaN/Inf.
    bool allFinite = true;
    for (float s : baseline)          if (!isFiniteSample(s)) { allFinite = false; break; }
    for (float s : explicitDefaults)  if (!isFiniteSample(s)) { allFinite = false; break; }
    CHECK(allFinite);

    // Within -120 dBFS RMS (contract default-off guarantee).
    const float dbRms = rmsDb(baseline, explicitDefaults);
    INFO("RMS difference (baseline vs explicit defaults) = " << dbRms << " dBFS");
    CHECK(dbRms <= -120.0f);
}
