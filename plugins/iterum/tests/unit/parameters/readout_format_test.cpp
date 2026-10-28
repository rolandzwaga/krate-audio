// ==============================================================================
// Readout formats: one standard across the ten modes (design-system.md 6.4, 9.3)
// ==============================================================================
// Every readout shows the controller's getParamStringByValue string verbatim (VST3Editor adds no unit), so the
// formatters are where the unit lives. These cases pin min / default / max of each formatter the 9.3 rows complete.
// Defaults are read from the registered ParameterInfo, never restated.

#include "parameters/bbd_params.h"
#include "parameters/freeze_params.h"
#include "parameters/granular_params.h"
#include "parameters/reverse_params.h"
#include "parameters/shimmer_params.h"
#include "parameters/spectral_params.h"
#include "parameters/tape_params.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>

using namespace Iterum;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

namespace {

using Formatter = std::function<Steinberg::tresult(ParamID, ParamValue, Steinberg::Vst::String128)>;

[[nodiscard]] std::string ascii(const Steinberg::Vst::String128 s) {
    std::string out;
    for (int i = 0; i < 128 && s[i] != 0; ++i)
        out += s[i] < 128 ? static_cast<char>(s[i]) : '?';
    return out;
}

[[nodiscard]] std::string format(const Formatter& fn, ParamID id, ParamValue n) {
    Steinberg::Vst::String128 s = {};
    REQUIRE(fn(id, n, s) == Steinberg::kResultOk);
    return ascii(s);
}

/// The registered default of @p id in a container filled by @p reg.
[[nodiscard]] ParamValue registeredDefault(const std::function<void(Steinberg::Vst::ParameterContainer&)>& reg,
                                           ParamID id) {
    Steinberg::Vst::ParameterContainer params;
    params.init();
    reg(params);
    auto* p = params.getParameter(id);
    REQUIRE(p != nullptr);
    return p->getInfo().defaultNormalizedValue;
}

[[nodiscard]] std::string percent(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f%%", v);
    return buf;
}

}  // namespace

TEST_CASE("Granular readouts print the 6.4 units", "[granular][format]") {
    const Formatter fn = formatGranularParam;
    const auto def = [](ParamID id) { return registeredDefault(registerGranularParams, id); };

    CHECK(format(fn, kGranularGrainSizeId, 0.0) == "10 ms");
    CHECK(format(fn, kGranularGrainSizeId, def(kGranularGrainSizeId)) == "100 ms");
    CHECK(format(fn, kGranularGrainSizeId, 1.0) == "500 ms");

    CHECK(format(fn, kGranularDensityId, 0.0) == "1.0 Hz");
    CHECK(format(fn, kGranularDensityId, def(kGranularDensityId)) == "20.0 Hz");
    CHECK(format(fn, kGranularDensityId, 1.0) == "100.0 Hz");

    CHECK(format(fn, kGranularDelayTimeId, 0.0) == "0.0 ms");
    CHECK(format(fn, kGranularDelayTimeId, def(kGranularDelayTimeId)) == "500.0 ms");
    CHECK(format(fn, kGranularDelayTimeId, 1.0) == "2.00 s");

    CHECK(format(fn, kGranularPitchId, 0.0) == "-24.0 st");
    CHECK(format(fn, kGranularPitchId, def(kGranularPitchId)) == "+0.0 st");
    CHECK(format(fn, kGranularPitchId, 1.0) == "+24.0 st");

    for (ParamID id : {kGranularPitchSprayId, kGranularPositionSprayId, kGranularPanSprayId, kGranularReverseProbId,
                       kGranularMixId, kGranularJitterId, kGranularTextureId, kGranularStereoWidthId}) {
        INFO("id " << id);
        CHECK(format(fn, id, 0.0) == "0%");
        CHECK(format(fn, id, def(id)) == percent(def(id) * 100.0));
        CHECK(format(fn, id, 1.0) == "100%");
    }

    CHECK(format(fn, kGranularFeedbackId, 0.0) == "0%");
    CHECK(format(fn, kGranularFeedbackId, def(kGranularFeedbackId)) == percent(def(kGranularFeedbackId) * 120.0));
    CHECK(format(fn, kGranularFeedbackId, 1.0) == "120%");
}

TEST_CASE("Spectral readouts print the 6.4 units", "[spectral][format]") {
    const Formatter fn = formatSpectralParam;
    const auto def = [](ParamID id) { return registeredDefault(registerSpectralParams, id); };

    CHECK(format(fn, kSpectralBaseDelayId, 0.0) == "0.0 ms");
    CHECK(format(fn, kSpectralBaseDelayId, def(kSpectralBaseDelayId)) == "250.0 ms");
    CHECK(format(fn, kSpectralBaseDelayId, 1.0) == "2.00 s");

    // Spread is a time (0-2000 ms), never a percentage.
    CHECK(format(fn, kSpectralSpreadId, 0.0) == "0 ms");
    CHECK(format(fn, kSpectralSpreadId, def(kSpectralSpreadId)) == "0 ms");
    CHECK(format(fn, kSpectralSpreadId, 1.0) == "2000 ms");

    CHECK(format(fn, kSpectralFeedbackId, 0.0) == "0%");
    CHECK(format(fn, kSpectralFeedbackId, def(kSpectralFeedbackId)) == "40%");
    CHECK(format(fn, kSpectralFeedbackId, 1.0) == "120%");

    CHECK(format(fn, kSpectralFeedbackTiltId, 0.0) == "-100%");
    CHECK(format(fn, kSpectralFeedbackTiltId, def(kSpectralFeedbackTiltId)) == "+0%");
    CHECK(format(fn, kSpectralFeedbackTiltId, 1.0) == "+100%");

    for (ParamID id : {kSpectralDiffusionId, kSpectralMixId, kSpectralStereoWidthId}) {
        INFO("id " << id);
        CHECK(format(fn, id, 0.0) == "0%");
        CHECK(format(fn, id, def(id)) == percent(def(id) * 100.0));
        CHECK(format(fn, id, 1.0) == "100%");
    }
}

TEST_CASE("Shimmer readouts print the 6.4 units", "[shimmer][format]") {
    const Formatter fn = formatShimmerParam;
    const auto def = [](ParamID id) { return registeredDefault(registerShimmerParams, id); };

    CHECK(format(fn, kShimmerDelayTimeId, 0.0) == "10.0 ms");
    CHECK(format(fn, kShimmerDelayTimeId, def(kShimmerDelayTimeId)) == "499.0 ms");
    CHECK(format(fn, kShimmerDelayTimeId, 1.0) == "5.00 s");

    CHECK(format(fn, kShimmerPitchSemitonesId, 0.0) == "-24.0 st");
    CHECK(format(fn, kShimmerPitchSemitonesId, def(kShimmerPitchSemitonesId)) == "+12.0 st");
    CHECK(format(fn, kShimmerPitchSemitonesId, 1.0) == "+24.0 st");

    CHECK(format(fn, kShimmerPitchCentsId, 0.0) == "-100 ct");
    CHECK(format(fn, kShimmerPitchCentsId, def(kShimmerPitchCentsId)) == "+0 ct");
    CHECK(format(fn, kShimmerPitchCentsId, 1.0) == "+100 ct");

    for (ParamID id : {kShimmerPitchBlendId, kShimmerDiffusionSizeId, kShimmerMixId}) {
        INFO("id " << id);
        CHECK(format(fn, id, 0.0) == "0%");
        CHECK(format(fn, id, def(id)) == percent(def(id) * 100.0));
        CHECK(format(fn, id, 1.0) == "100%");
    }

    CHECK(format(fn, kShimmerFeedbackId, 0.0) == "0%");
    CHECK(format(fn, kShimmerFeedbackId, def(kShimmerFeedbackId)) == "40%");
    CHECK(format(fn, kShimmerFeedbackId, 1.0) == "120%");

    CHECK(format(fn, kShimmerFilterCutoffId, 0.0) == "20 Hz");
    CHECK(format(fn, kShimmerFilterCutoffId, def(kShimmerFilterCutoffId)) == "4.00 kHz");
    CHECK(format(fn, kShimmerFilterCutoffId, 1.0) == "20.00 kHz");
}

TEST_CASE("Tape core time switches to seconds at 1000 ms", "[tape][format]") {
    const Formatter fn = formatTapeParam;
    CHECK(format(fn, kTapeMotorSpeedId, 0.0) == "20.0 ms");
    CHECK(format(fn, kTapeMotorSpeedId, registeredDefault(registerTapeParams, kTapeMotorSpeedId)) == "499.2 ms");
    CHECK(format(fn, kTapeMotorSpeedId, 1.0) == "2.00 s");
}

TEST_CASE("BBD core time switches to seconds at 1000 ms", "[bbd][format]") {
    const Formatter fn = formatBBDParam;
    CHECK(format(fn, kBBDDelayTimeId, 0.0) == "20.0 ms");
    CHECK(format(fn, kBBDDelayTimeId, registeredDefault(registerBBDParams, kBBDDelayTimeId)) == "300.3 ms");
    CHECK(format(fn, kBBDDelayTimeId, 1.0) == "1.00 s");
}

TEST_CASE("Reverse core time switches to seconds at 1000 ms", "[reverse][format]") {
    const Formatter fn = formatReverseParam;
    CHECK(format(fn, kReverseChunkSizeId, 0.0) == "10.0 ms");
    CHECK(format(fn, kReverseChunkSizeId, registeredDefault(registerReverseParams, kReverseChunkSizeId)) ==
          "499.5 ms");
    CHECK(format(fn, kReverseChunkSizeId, 1.0) == "2.00 s");
}

TEST_CASE("Freeze readouts print the 6.4 units from the processor's mappings", "[freeze][format]") {
    const auto def = [](ParamID id) { return registeredDefault(registerFreezeParams, id); };
    const ParamValue steps8 = def(kFreezeEuclideanStepsId);  // registered default: 8 steps
    const auto fz = [&](ParamID id, ParamValue n, ParamValue steps) {
        Steinberg::Vst::String128 s = {};
        REQUIRE(formatFreezeParam(id, n, s, steps) == Steinberg::kResultOk);
        return ascii(s);
    };
    const auto f = [&](ParamID id, ParamValue n) { return fz(id, n, steps8); };

    CHECK(f(kFreezeMixId, def(kFreezeMixId)) == "50%");

    CHECK(f(kFreezeSliceLengthId, 0.0) == "10.0 ms");
    CHECK(f(kFreezeSliceLengthId, def(kFreezeSliceLengthId)) == "200.0 ms");
    CHECK(f(kFreezeSliceLengthId, 1.0) == "2.00 s");

    CHECK(f(kFreezeEuclideanStepsId, 0.0) == "2");
    CHECK(f(kFreezeEuclideanStepsId, steps8) == "8");
    CHECK(f(kFreezeEuclideanStepsId, 1.0) == "32");

    SECTION("Hits and Rotation follow the current Steps") {
        CHECK(fz(kFreezeEuclideanHitsId, 0.0, steps8) == "1");
        CHECK(fz(kFreezeEuclideanHitsId, def(kFreezeEuclideanHitsId), steps8) == "3");
        CHECK(fz(kFreezeEuclideanHitsId, 1.0, steps8) == "8");
        CHECK(fz(kFreezeEuclideanRotationId, 0.0, steps8) == "0");
        CHECK(fz(kFreezeEuclideanRotationId, 1.0, steps8) == "7");

        const ParamValue steps16 = 14.0 / 30.0;
        CHECK(fz(kFreezeEuclideanStepsId, steps16, steps16) == "16");
        CHECK(fz(kFreezeEuclideanHitsId, 0.0, steps16) == "1");
        CHECK(fz(kFreezeEuclideanHitsId, 1.0, steps16) == "16");
        CHECK(fz(kFreezeEuclideanRotationId, 1.0, steps16) == "15");
    }

    CHECK(f(kFreezeGranularDensityId, 0.0) == "1.0 Hz");
    CHECK(f(kFreezeGranularDensityId, def(kFreezeGranularDensityId)) == "10.0 Hz");
    CHECK(f(kFreezeGranularDensityId, 1.0) == "50.0 Hz");

    CHECK(f(kFreezeGranularPositionJitterId, 0.0) == "0%");
    CHECK(f(kFreezeGranularPositionJitterId, def(kFreezeGranularPositionJitterId)) == "50%");
    CHECK(f(kFreezeGranularPositionJitterId, 1.0) == "100%");
    CHECK(f(kFreezeGranularSizeJitterId, def(kFreezeGranularSizeJitterId)) == "25%");
    CHECK(f(kFreezeDroneDriftId, def(kFreezeDroneDriftId)) == "30%");
    CHECK(f(kFreezeNoiseFilterSweepId, 0.0) == "0%");
    CHECK(f(kFreezeNoiseFilterSweepId, def(kFreezeNoiseFilterSweepId)) == "50%");
    CHECK(f(kFreezeNoiseFilterSweepId, 1.0) == "100%");

    CHECK(f(kFreezeGranularGrainSizeId, 0.0) == "10 ms");
    CHECK(f(kFreezeGranularGrainSizeId, def(kFreezeGranularGrainSizeId)) == "100 ms");
    CHECK(f(kFreezeGranularGrainSizeId, 1.0) == "500 ms");

    CHECK(f(kFreezeDroneVoiceCountId, 0.0) == "1");
    CHECK(f(kFreezeDroneVoiceCountId, def(kFreezeDroneVoiceCountId)) == "2");
    CHECK(f(kFreezeDroneVoiceCountId, 1.0) == "4");

    CHECK(f(kFreezeDroneDriftRateId, 0.0) == "0.10 Hz");
    CHECK(f(kFreezeDroneDriftRateId, def(kFreezeDroneDriftRateId)) == "0.50 Hz");
    CHECK(f(kFreezeDroneDriftRateId, 1.0) == "2.00 Hz");

    CHECK(f(kFreezeNoiseFilterCutoffId, 0.0) == "20 Hz");
    CHECK(f(kFreezeNoiseFilterCutoffId, def(kFreezeNoiseFilterCutoffId)) == "2.00 kHz");
    CHECK(f(kFreezeNoiseFilterCutoffId, 1.0) == "20.00 kHz");

    CHECK(f(kFreezeEnvelopeAttackId, 0.0) == "0 ms");
    CHECK(f(kFreezeEnvelopeAttackId, def(kFreezeEnvelopeAttackId)) == "10 ms");
    CHECK(f(kFreezeEnvelopeAttackId, 1.0) == "500 ms");

    CHECK(f(kFreezeEnvelopeReleaseId, 0.0) == "0 ms");
    CHECK(f(kFreezeEnvelopeReleaseId, def(kFreezeEnvelopeReleaseId)) == "100 ms");
    CHECK(f(kFreezeEnvelopeReleaseId, 1.0) == "2000 ms");

    // Lists stay with their StringListParameter.
    Steinberg::Vst::String128 s = {};
    CHECK(formatFreezeParam(kFreezePatternTypeId, 0.0, s, steps8) == Steinberg::kResultFalse);
}

// The processor maps a head Level as -30..+6 dB (handleTapeParamChange). The readout and the state load used
// -96..+6, so the default 0.941 read "0.0 dB" while the heads played at +3.9 dB, and a reloaded level landed at the
// wrong knob position (user ruling 2026-10-28: the display and the load follow the DSP; the DSP is unchanged).
TEST_CASE("Tape head Level readout and state load follow the processor's dB mapping", "[tape][format]") {
    const Formatter fn = formatTapeParam;
    const ParamValue def = registeredDefault(registerTapeParams, kTapeHead1LevelId);

    SECTION("the readout prints the dB the processor applies") {
        for (const ParamValue n : {0.0, 0.25, 30.0 / 36.0, def, 1.0}) {
            TapeParams p;
            handleTapeParamChange(p, kTapeHead1LevelId, n);
            double dB = 20.0 * std::log10(static_cast<double>(p.head1Level.load()));
            if (dB > -0.05 && dB < 0.05) dB = 0.0;
            char want[32];
            std::snprintf(want, sizeof(want), "%.1f dB", dB);
            INFO("normalized " << n);
            CHECK(format(fn, kTapeHead1LevelId, n) == want);
        }
        CHECK(format(fn, kTapeHead1LevelId, 0.0) == "-30.0 dB");
        CHECK(format(fn, kTapeHead2LevelId, 30.0 / 36.0) == "0.0 dB");
        CHECK(format(fn, kTapeHead3LevelId, 1.0) == "6.0 dB");
    }

    SECTION("a level saved by the processor loads back at the same normalized value") {
        const std::array<ParamID, 3> ids{kTapeHead1LevelId, kTapeHead2LevelId, kTapeHead3LevelId};
        const std::array<ParamValue, 3> values{0.25, 0.5, def};
        TapeParams p;
        for (std::size_t i = 0; i < ids.size(); ++i) handleTapeParamChange(p, ids[i], values[i]);

        Steinberg::MemoryStream stream;
        Steinberg::IBStreamer writer(&stream, kLittleEndian);
        saveTapeParams(p, writer);
        stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        Steinberg::IBStreamer reader(&stream, kLittleEndian);
        std::map<ParamID, ParamValue> loaded;
        loadTapeParamsToController(reader, [&](ParamID id, ParamValue v) { loaded[id] = v; });

        for (std::size_t i = 0; i < ids.size(); ++i) {
            INFO("head " << i + 1);
            REQUIRE(loaded.count(ids[i]) == 1);
            CHECK(std::abs(loaded[ids[i]] - values[i]) < 1e-5);
        }
    }
}
