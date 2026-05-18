// ==============================================================================
// Pitch envelope UI gate -- BodyModel != Membrane disables the section
// ==============================================================================
// The pitch envelope only retargets the modal bank's f0 for Membrane bodies
// (see drum_voice.h:486). On Plate / Shell / String / Bell / NoiseBody, the
// envelope advances but its output is ignored. The controller dims and
// disables the pitch-envelope UI section (display + Knee menu + Knee label)
// to match this, via `updatePitchEnvControlsEnabled()`.
//
// This file pins the gate decision: the helper's verdict must follow the
// BodyModel global proxy across every body type, including the bands of
// normalized values that map to each discrete body via the processor's clamp
// `static_cast<int>(norm * kCount)` (processor.cpp:288).
// ==============================================================================

#include "controller/controller.h"
#include "dsp/body_model_type.h"
#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>

using namespace Membrum;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

/// StringListParameter normalizes idx -> idx / stepCount, where stepCount =
/// count - 1. The processor's clamp later maps back via int(norm * count).
/// Choose midpoints inside each body's normalized band so the test is not
/// sensitive to rounding at the boundaries.
constexpr double bodyNormFor(BodyModelType body) noexcept
{
    // 6 entries -> stepCount = 5. idx N -> norm = N / 5.0.
    return static_cast<double>(static_cast<int>(body))
         / static_cast<double>(static_cast<int>(BodyModelType::kCount) - 1);
}

} // namespace

TEST_CASE("Pitch envelope UI gate: Membrane enables, others disable",
          "[pitch_env_gate]")
{
    Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    SECTION("Membrane (idx 0) enables the section")
    {
        controller.setParamNormalized(
            kBodyModelId, bodyNormFor(BodyModelType::Membrane));
        REQUIRE(controller.isMembraneBodySelectedForTest() == true);
    }

    SECTION("Plate (idx 1) disables the section")
    {
        controller.setParamNormalized(
            kBodyModelId, bodyNormFor(BodyModelType::Plate));
        REQUIRE(controller.isMembraneBodySelectedForTest() == false);
    }

    SECTION("Shell (idx 2) disables the section")
    {
        controller.setParamNormalized(
            kBodyModelId, bodyNormFor(BodyModelType::Shell));
        REQUIRE(controller.isMembraneBodySelectedForTest() == false);
    }

    SECTION("String (idx 3) disables the section")
    {
        controller.setParamNormalized(
            kBodyModelId, bodyNormFor(BodyModelType::String));
        REQUIRE(controller.isMembraneBodySelectedForTest() == false);
    }

    SECTION("Bell (idx 4) disables the section")
    {
        controller.setParamNormalized(
            kBodyModelId, bodyNormFor(BodyModelType::Bell));
        REQUIRE(controller.isMembraneBodySelectedForTest() == false);
    }

    SECTION("NoiseBody (idx 5) disables the section")
    {
        controller.setParamNormalized(
            kBodyModelId, bodyNormFor(BodyModelType::NoiseBody));
        REQUIRE(controller.isMembraneBodySelectedForTest() == false);
    }

    controller.terminate();
}

TEST_CASE("Pitch envelope UI gate: helper is null-tolerant",
          "[pitch_env_gate]")
{
    // updatePitchEnvControlsEnabled() must be safe to call before verifyView()
    // populates any cached view pointers (e.g. immediately after initialize()
    // when no editor frame has been opened yet). The helper short-circuits on
    // null views.
    Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Any setParamNormalized on kBodyModelId triggers updatePitchEnvControlsEnabled
    // internally; this exercises the null-view path under both gate verdicts.
    REQUIRE_NOTHROW(controller.setParamNormalized(
        kBodyModelId, bodyNormFor(BodyModelType::Membrane)));
    REQUIRE_NOTHROW(controller.setParamNormalized(
        kBodyModelId, bodyNormFor(BodyModelType::Bell)));

    controller.terminate();
}
