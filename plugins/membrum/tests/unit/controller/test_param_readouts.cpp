// ==============================================================================
// Parameter readouts use the DSP's laws (fix plan T2.18, MB-077, MB-078)
// ==============================================================================
// - Filter-envelope A/D/R readouts decode cubically (pad_config_apply.h).
// - Size reads the selected body's fundamental (natural_fundamental.h).
// - Pitch-envelope parameters read in Hz / ms, not as raw 0..1.
// ==============================================================================

#include "controller/controller.h"
#include "dsp/body_model_type.h"
#include "dsp/pad_config.h"
#include "plugin_ids.h"

#include "public.sdk/source/vst/utility/stringconvert.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace Membrum;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

std::string readout(Controller& c, ParamID tag, double norm)
{
    String128 s{};
    REQUIRE(c.getParamStringByValue(tag, norm, s) == kResultOk);
    return Steinberg::Vst::StringConvert::convert(s, 128);
}

// StringListParameter: idx -> idx / (count - 1).
double bodyNorm(BodyModelType body)
{
    return static_cast<double>(static_cast<int>(body))
         / static_cast<double>(static_cast<int>(BodyModelType::kCount) - 1);
}

} // namespace

TEST_CASE("Param readouts: filter-envelope times use the cubic law the voice decodes",
          "[controller][param_format]")
{
    Controller c;
    REQUIRE(c.initialize(nullptr) == kResultOk);

    CHECK(readout(c, kToneShaperFilterEnvDecayId, 0.5) == "250 ms");
    CHECK(readout(c, kToneShaperFilterEnvAttackId, 0.5) == "62.5 ms");
    CHECK(readout(c, kToneShaperFilterEnvReleaseId, 0.1) == "2.00 ms");

    CHECK(readout(c, padParamId(3, kPadTSFilterEnvDecay), 0.5) == "250 ms");
    CHECK(readout(c, padParamId(3, kPadTSFilterEnvAttack), 0.5) == "62.5 ms");
    CHECK(readout(c, padParamId(3, kPadTSFilterEnvRelease), 0.1) == "2.00 ms");

    REQUIRE(c.terminate() == kResultOk);
}

TEST_CASE("Param readouts: Size shows the selected body's fundamental",
          "[controller][param_format]")
{
    Controller c;
    REQUIRE(c.initialize(nullptr) == kResultOk);

    c.setParamNormalized(kBodyModelId, bodyNorm(BodyModelType::Shell));
    CHECK(readout(c, kSizeId, 0.5) == "50% (474 Hz)");

    c.setParamNormalized(padParamId(5, kPadBodyModel), bodyNorm(BodyModelType::Plate));
    CHECK(readout(c, padParamId(5, kPadSize), 0.5) == "50% (253 Hz)");

    // Another pad still on Membrane.
    c.setParamNormalized(padParamId(6, kPadBodyModel), bodyNorm(BodyModelType::Membrane));
    CHECK(readout(c, padParamId(6, kPadSize), 0.5) == "50% (158 Hz)");

    c.setParamNormalized(kBodyModelId, bodyNorm(BodyModelType::Membrane));
    CHECK(readout(c, kSizeId, 0.5) == "50% (158 Hz)");

    REQUIRE(c.terminate() == kResultOk);
}

TEST_CASE("Param readouts: pitch-envelope parameters show Hz and ms",
          "[controller][param_format]")
{
    Controller c;
    REQUIRE(c.initialize(nullptr) == kResultOk);

    CHECK(readout(c, kToneShaperPitchEnvStartId, 0.4515) == "160 Hz");
    CHECK(readout(c, kToneShaperPitchEnvEndId, 0.19897) == "50.0 Hz");
    CHECK(readout(c, kToneShaperPitchEnvTimeId, 0.1) == "50.0 ms");
    CHECK(readout(c, kPitchEnvMidPitchId, 1.0) == "2.00 kHz");

    CHECK(readout(c, padParamId(7, kPadTSPitchEnvStart), 0.4515) == "160 Hz");
    CHECK(readout(c, padParamId(7, kPadTSPitchEnvEnd), 0.19897) == "50.0 Hz");
    CHECK(readout(c, padParamId(7, kPadTSPitchEnvTime), 0.1) == "50.0 ms");
    CHECK(readout(c, padParamId(7, kPadTSPitchEnvMidPitch), 1.0) == "2.00 kHz");

    REQUIRE(c.terminate() == kResultOk);
}
