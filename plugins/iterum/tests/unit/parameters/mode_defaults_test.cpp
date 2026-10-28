// ==============================================================================
// Fresh-instance defaults per delay mode
// ==============================================================================
// What a mode sounds like before anyone touches it. A delay should start
// audible: tempo-synced, with an echo tail (40 % feedback, like Tape, BBD and
// Digital). The processor runs on each params struct's initial values until the
// host sends parameters, so those must match the registered defaults.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "parameters/granular_params.h"
#include "parameters/shimmer_params.h"
#include "parameters/spectral_params.h"
#include "parameters/tape_params.h"
#include "plugin_ids.h"

#include "public.sdk/source/vst/vstparameters.h"

#include <functional>

using namespace Iterum;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

/// Registered default of @p id in a container filled by @p reg.
double registeredDefault(const std::function<void(ParameterContainer&)>& reg, ParamID id) {
    ParameterContainer parameters;
    parameters.init();
    reg(parameters);
    auto* p = parameters.getParameter(id);
    REQUIRE(p != nullptr);
    return p->getInfo().defaultNormalizedValue;
}

} // namespace

TEST_CASE("Granular starts synced, with feedback and overlapping grains", "[granular][defaults]") {
    const auto def = [](ParamID id) { return registeredDefault(registerGranularParams, id); };
    const GranularParams fresh;

    SECTION("Time Mode defaults to Synced") {
        REQUIRE(def(kGranularTimeModeId) == Approx(1.0));
        REQUIRE(fresh.timeMode.load() == 1);
    }

    SECTION("Feedback defaults to 40%") {
        REQUIRE(def(kGranularFeedbackId) * 1.2 == Approx(0.4).margin(0.001));
        REQUIRE(fresh.feedback.load() == Approx(0.4f));
    }

    SECTION("Density defaults to 20 grains/s, so 100 ms grains overlap") {
        REQUIRE(1.0 + def(kGranularDensityId) * 99.0 == Approx(20.0).margin(0.01));
        REQUIRE(fresh.density.load() == Approx(20.0f));
        REQUIRE(fresh.density.load() * fresh.grainSize.load() / 1000.0f >= 2.0f);
    }
}

TEST_CASE("Spectral starts synced, with feedback", "[spectral][defaults]") {
    const auto def = [](ParamID id) { return registeredDefault(registerSpectralParams, id); };
    const SpectralParams fresh;

    SECTION("Time Mode defaults to Synced") {
        REQUIRE(def(kSpectralTimeModeId) == Approx(1.0));
        REQUIRE(fresh.timeMode.load() == 1);
    }

    SECTION("Feedback defaults to 40%") {
        REQUIRE(def(kSpectralFeedbackId) * 1.2 == Approx(0.4).margin(0.001));
        REQUIRE(fresh.feedback.load() == Approx(0.4f));
    }
}

TEST_CASE("Tape starts synced, with feedback", "[tape][defaults]") {
    const auto def = [](ParamID id) { return registeredDefault(registerTapeParams, id); };
    const TapeParams fresh;

    SECTION("Time Mode defaults to Synced") {
        REQUIRE(def(kTapeTimeModeId) == Approx(1.0));
        REQUIRE(fresh.timeMode.load() == 1);
    }

    SECTION("Note Value defaults to 1/8, like the other synced modes") {
        REQUIRE(def(kTapeNoteValueId)
                == Approx(registeredDefault(registerShimmerParams, kShimmerNoteValueId)));
        REQUIRE(fresh.noteValue.load() == Krate::DSP::kNoteValueDefaultIndex);
    }

    SECTION("Feedback defaults to 40%") {
        REQUIRE(def(kTapeFeedbackId) * 1.2 == Approx(0.4).margin(0.001));
        REQUIRE(fresh.feedback.load() == Approx(0.4f));
    }
}

TEST_CASE("Shimmer starts synced, with feedback", "[shimmer][defaults]") {
    const auto def = [](ParamID id) { return registeredDefault(registerShimmerParams, id); };
    const ShimmerParams fresh;

    SECTION("Time Mode defaults to Synced") {
        REQUIRE(def(kShimmerTimeModeId) == Approx(1.0));
        REQUIRE(fresh.timeMode.load() == 1);
    }

    SECTION("Feedback defaults to 40%") {
        REQUIRE(def(kShimmerFeedbackId) * 1.2 == Approx(0.4).margin(0.001));
        REQUIRE(fresh.feedback.load() == Approx(0.4f));
    }
}
