// ==============================================================================
// Signal-path audit findings H-3 + M-1: the pitch envelope and Material Morph
// must drive ALL body types, not just Membrane.
// ==============================================================================
// Before the fix, updateBodyFundamental()/refreshBodyForMaterial() and the
// block-path refreshes early-returned for every non-Membrane body, so:
//   * H-3: an 808-style pitch drop on Plate/Shell/Bell/String/NoiseBody did
//     nothing -- the body's fundamental never moved (and not even the START
//     frequency was applied).
//   * M-1: a Material Morph sweep advanced its counter but never re-ran the
//     mapper for non-Membrane bodies, so only the static start value reached
//     the body.
//
// These tests reproduce both: they fail (sweep == 0, morph render == static
// render) on the buggy code and pass once the refresh paths are generalized.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/drum_voice.h"
#include "dsp/body_model_type.h"

#include <array>
#include <cmath>
#include <string>
#include <vector>

using Membrum::BodyModelType;
using Membrum::DrumVoice;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlock      = 256;

// Render `numSamples` of a prepared, note-on voice into a flat vector.
std::vector<float> render(DrumVoice& v, int numSamples)
{
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(numSamples));
    std::array<float, kBlock> buf{};
    int remaining = numSamples;
    while (remaining > 0)
    {
        const int n = remaining < kBlock ? remaining : kBlock;
        buf.fill(0.0f);
        v.processBlock(buf.data(), n);
        for (int i = 0; i < n; ++i)
            out.push_back(buf[i]);
        remaining -= n;
    }
    return out;
}

// Cheap zero-crossing count over a window -- a frequency proxy for the
// waveguide String body, which doesn't expose a modal-bank fundamental.
int zeroCrossings(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    int count = 0;
    for (std::size_t i = from + 1; i < to && i < x.size(); ++i)
        if ((x[i - 1] <= 0.0f) != (x[i] <= 0.0f))
            ++count;
    return count;
}

double energy(const std::vector<float>& x)
{
    double e = 0.0;
    for (float s : x)
        e += static_cast<double>(s) * s;
    return e;
}

const char* bodyName(BodyModelType t)
{
    switch (t)
    {
    case BodyModelType::Membrane:  return "Membrane";
    case BodyModelType::Plate:     return "Plate";
    case BodyModelType::Shell:     return "Shell";
    case BodyModelType::String:    return "String";
    case BodyModelType::Bell:      return "Bell";
    case BodyModelType::NoiseBody: return "NoiseBody";
    default:                       return "?";
    }
}

} // namespace

TEST_CASE("H-3: pitch envelope sweeps the modal fundamental on every modal body",
          "[membrum][audit][pitch_env]")
{
    // Plate/Shell/Bell/NoiseBody share the modal-bank fundamental that
    // getModeFrequency(0) reports; Membrane is the already-working control.
    const BodyModelType bodies[] = {
        BodyModelType::Membrane,
        BodyModelType::Plate,
        BodyModelType::Shell,
        BodyModelType::Bell,
        BodyModelType::NoiseBody,
    };

    for (BodyModelType body : bodies)
    {
        DYNAMIC_SECTION(bodyName(body))
        {
            DrumVoice v;
            v.prepare(kSampleRate);
            v.setBodyModel(body);
            v.setSize(0.5f);
            v.setMaterial(0.5f);

            // Steep downward 808-style sweep: 800 Hz -> 100 Hz over 60 ms.
            v.toneShaper().setPitchEnvStartHz(800.0f);
            v.toneShaper().setPitchEnvEndHz(100.0f);
            v.toneShaper().setPitchEnvTimeMs(60.0f);

            v.noteOn(1.0f);

            // Fundamental right after note-on -- the seed should already
            // reflect the START pitch, not the body's natural f0.
            const float startFreq = v.bodyBank().getSharedBank().getModeFrequency(0);

            // Render past the end of the sweep (60 ms ~= 2880 samples).
            (void)render(v, 4000);
            const float endFreq = v.bodyBank().getSharedBank().getModeFrequency(0);

            INFO("body=" << bodyName(body)
                 << " startFreq=" << startFreq << " endFreq=" << endFreq);

            // Both must be real, non-zero fundamentals.
            REQUIRE(startFreq > 0.0f);
            REQUIRE(endFreq > 0.0f);

            // The sweep must drop the fundamental substantially (8:1 pitch
            // ratio -> well under half). Buggy code leaves it static.
            CHECK(endFreq < 0.5f * startFreq);
        }
    }
}

TEST_CASE("H-3: pitch envelope retunes the String waveguide body",
          "[membrum][audit][pitch_env]")
{
    auto makeVoice = [](bool pitchEnv) {
        auto v = std::make_unique<DrumVoice>();
        v->prepare(kSampleRate);
        v->setBodyModel(BodyModelType::String);
        v->setSize(0.5f);
        v->setMaterial(0.5f);
        // Isolate the body fundamental: silence the broadband layers.
        v->setNoiseLayerMix(0.0f);
        v->setClickLayerMix(0.0f);
        if (pitchEnv)
        {
            v->toneShaper().setPitchEnvStartHz(800.0f);
            v->toneShaper().setPitchEnvEndHz(100.0f);
            v->toneShaper().setPitchEnvTimeMs(120.0f);
        }
        v->noteOn(1.0f);
        return v;
    };

    auto flat  = makeVoice(false);
    auto swept = makeVoice(true);

    const auto flatAudio  = render(*flat, 12000);
    const auto sweptAudio = render(*swept, 12000);

    // 1. The pitch envelope must actually change the String output. On the
    //    buggy code the StringMapper ignores pitchHz, so the two renders are
    //    bit-identical (the envelope is fully dead for String).
    REQUIRE(flatAudio.size() == sweptAudio.size());
    double maxDiff = 0.0;
    for (std::size_t i = 0; i < flatAudio.size(); ++i)
        maxDiff = std::max(maxDiff,
            std::abs(static_cast<double>(flatAudio[i] - sweptAudio[i])));
    INFO("String flat-vs-swept maxDiff=" << maxDiff
         << " sweptEnergy=" << energy(sweptAudio));
    CHECK(maxDiff > 1e-4);

    // 2. Direction: a downward sweep lowers the zero-crossing rate from the
    //    early (high-pitch) window to the late (settled-low) window.
    const int zcEarly = zeroCrossings(sweptAudio, 600, 3000);
    const int zcLate  = zeroCrossings(sweptAudio, 8000, 11000);
    INFO("String zcEarly=" << zcEarly << " zcLate=" << zcLate);
    CHECK(zcLate < zcEarly);
}

TEST_CASE("M-1: Material Morph re-runs the mapper on a non-Membrane body",
          "[membrum][audit][material_morph]")
{
    // A Plate whose material morphs 0 -> 1 over the note must render
    // differently from a Plate held statically at material 0. On the buggy
    // code the morph counter advances but the Plate mapper is never
    // re-applied, so only the static start value (0) reaches the body and the
    // two renders are identical.
    auto makePlate = [](bool morph) {
        auto v = std::make_unique<DrumVoice>();
        v->prepare(kSampleRate);
        v->setBodyModel(BodyModelType::Plate);
        v->setSize(0.5f);
        v->setStrikePosition(0.3f);
        v->setMaterial(0.0f);  // static baseline == morph start value
        if (morph)
        {
            auto& mm = v->unnaturalZone().materialMorph;
            mm.setEnabled(true);
            mm.setStart(0.0f);
            mm.setEnd(1.0f);
            mm.setDurationMs(120.0f);
        }
        v->noteOn(1.0f);
        return v;
    };

    auto staticPlate = makePlate(false);
    auto morphPlate  = makePlate(true);

    const auto staticAudio = render(*staticPlate, 12000);
    const auto morphAudio  = render(*morphPlate, 12000);

    REQUIRE(staticAudio.size() == morphAudio.size());
    double maxDiff = 0.0;
    for (std::size_t i = 0; i < staticAudio.size(); ++i)
        maxDiff = std::max(maxDiff,
            std::abs(static_cast<double>(staticAudio[i] - morphAudio[i])));
    INFO("Plate static-vs-morph maxDiff=" << maxDiff);
    CHECK(maxDiff > 1e-4);
}

TEST_CASE("Default-off: no pitch env leaves the modal fundamental static",
          "[membrum][audit][pitch_env]")
{
    // Guards against spurious per-block retuning when the envelope is disabled.
    DrumVoice v;
    v.prepare(kSampleRate);
    v.setBodyModel(BodyModelType::Plate);
    v.setSize(0.5f);
    v.noteOn(1.0f);  // pitchEnvTimeMs == 0 -> inactive

    const float f0 = v.bodyBank().getSharedBank().getModeFrequency(0);
    (void)render(v, 4000);
    const float f1 = v.bodyBank().getSharedBank().getModeFrequency(0);

    using Catch::Matchers::WithinRel;
    CHECK_THAT(f1, WithinRel(f0, 1e-3f));
}
