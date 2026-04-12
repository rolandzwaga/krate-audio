// Phase 4 T011: Per-pad parameter registration and selected-pad proxy logic tests
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/funknown.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace Membrum;
using Catch::Approx;

// ==============================================================================
// Helper: initialize a Controller for testing
// ==============================================================================
namespace {

class TestController
{
public:
    TestController()
    {
        controller_.initialize(nullptr);
    }

    ~TestController()
    {
        controller_.terminate();
    }

    Controller& get() { return controller_; }

    Parameter* findParam(ParamID id)
    {
        return controller_.getParameterObject(id);
    }

    double getParamNormalized(ParamID id)
    {
        return controller_.getParamNormalized(id);
    }

    void setParamNormalized(ParamID id, double value)
    {
        controller_.setParamNormalized(id, value);
    }

private:
    Controller controller_;
};

} // namespace

// ==============================================================================
// kSelectedPadId registration
// ==============================================================================

TEST_CASE("Controller: kSelectedPadId is registered as stepped [0,31]", "[pad_params]")
{
    TestController tc;
    auto* param = tc.findParam(kSelectedPadId);
    REQUIRE(param != nullptr);

    // Should be a stepped parameter with 31 steps (0 through 31)
    auto& info = param->getInfo();
    CHECK(info.stepCount == 31);
}

// ==============================================================================
// Per-pad parameter registration (32 pads x 36 active offsets = 1152 params)
// ==============================================================================

TEST_CASE("Controller: all 1152 per-pad parameters are registered", "[pad_params]")
{
    TestController tc;

    int registeredCount = 0;
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        for (int offset = 0; offset < kPadActiveParamCount; ++offset)
        {
            const int paramId = padParamId(pad, offset);
            auto* param = tc.findParam(static_cast<ParamID>(paramId));
            if (param != nullptr)
                ++registeredCount;
        }
    }

    CHECK(registeredCount == kNumPads * kPadActiveParamCount);
}

TEST_CASE("Controller: per-pad param IDs span 1000-3047", "[pad_params]")
{
    TestController tc;

    // First pad, first param
    CHECK(tc.findParam(static_cast<ParamID>(padParamId(0, 0))) != nullptr);

    // First pad, last active param
    CHECK(tc.findParam(static_cast<ParamID>(padParamId(0, kPadActiveParamCount - 1))) != nullptr);

    // Last pad, first param
    CHECK(tc.findParam(static_cast<ParamID>(padParamId(31, 0))) != nullptr);

    // Last pad, last active param (3047)
    CHECK(tc.findParam(static_cast<ParamID>(padParamId(31, kPadActiveParamCount - 1))) != nullptr);
}

TEST_CASE("Controller: per-pad discrete params are StringListParameter", "[pad_params]")
{
    TestController tc;

    // ExciterType for pad 0 (offset 0) should be a list with 6 choices
    auto* excParam = tc.findParam(static_cast<ParamID>(padParamId(0, kPadExciterType)));
    REQUIRE(excParam != nullptr);
    CHECK(excParam->getInfo().stepCount == 5); // 6 values = 5 steps

    // BodyModel for pad 0 (offset 1) should be a list with 6 choices
    auto* bodyParam = tc.findParam(static_cast<ParamID>(padParamId(0, kPadBodyModel)));
    REQUIRE(bodyParam != nullptr);
    CHECK(bodyParam->getInfo().stepCount == 5); // 6 values = 5 steps
}

TEST_CASE("Controller: per-pad float params are RangeParameter [0,1]", "[pad_params]")
{
    TestController tc;

    // Material for pad 0 (offset 2)
    auto* matParam = tc.findParam(static_cast<ParamID>(padParamId(0, kPadMaterial)));
    REQUIRE(matParam != nullptr);
    CHECK(matParam->getInfo().stepCount == 0); // continuous
}

// ==============================================================================
// Selected pad proxy: global param changes forward to selected pad
// ==============================================================================

TEST_CASE("Controller: global proxy param forwards to selected pad's per-pad param", "[pad_params][proxy]")
{
    TestController tc;

    // Selected pad defaults to 0
    // Change the global material param
    tc.setParamNormalized(kMaterialId, 0.75);

    // The per-pad param for pad 0 material should reflect the same value
    double padValue = tc.getParamNormalized(static_cast<ParamID>(padParamId(0, kPadMaterial)));
    CHECK(padValue == Approx(0.75).margin(0.01));
}

TEST_CASE("Controller: changing kSelectedPadId updates global proxy params", "[pad_params][proxy]")
{
    TestController tc;

    // Set pad 1's material to a distinct value via per-pad param
    tc.setParamNormalized(static_cast<ParamID>(padParamId(1, kPadMaterial)), 0.3);

    // Select pad 1 (normalized: 1/31)
    tc.setParamNormalized(kSelectedPadId, 1.0 / 31.0);

    // Global material proxy should now reflect pad 1's value
    double globalMat = tc.getParamNormalized(kMaterialId);
    CHECK(globalMat == Approx(0.3).margin(0.01));
}

TEST_CASE("Controller: proxy does not touch non-selected pad params", "[pad_params][proxy]")
{
    TestController tc;

    // Set pad 0 material to 0.1
    tc.setParamNormalized(static_cast<ParamID>(padParamId(0, kPadMaterial)), 0.1);

    // Set pad 2 material to 0.9
    tc.setParamNormalized(static_cast<ParamID>(padParamId(2, kPadMaterial)), 0.9);

    // Select pad 0
    tc.setParamNormalized(kSelectedPadId, 0.0);

    // Change global material (should only affect pad 0)
    tc.setParamNormalized(kMaterialId, 0.5);

    // Pad 2 should be unaffected
    double pad2Mat = tc.getParamNormalized(static_cast<ParamID>(padParamId(2, kPadMaterial)));
    CHECK(pad2Mat == Approx(0.9).margin(0.01));
}

// ==============================================================================
// Proxy mapping covers all global parameter IDs
// ==============================================================================

TEST_CASE("Controller: proxy covers exciter secondary params (202-205)", "[pad_params][proxy]")
{
    TestController tc;

    // Set FM Ratio via global param
    tc.setParamNormalized(kExciterFMRatioId, 0.6);
    double padFM = tc.getParamNormalized(static_cast<ParamID>(padParamId(0, kPadFMRatio)));
    CHECK(padFM == Approx(0.6).margin(0.01));
}

TEST_CASE("Controller: proxy covers tone shaper params (210-223)", "[pad_params][proxy]")
{
    TestController tc;

    tc.setParamNormalized(kToneShaperFilterCutoffId, 0.7);
    double padCutoff = tc.getParamNormalized(
        static_cast<ParamID>(padParamId(0, kPadTSFilterCutoff)));
    CHECK(padCutoff == Approx(0.7).margin(0.01));
}

TEST_CASE("Controller: proxy covers unnatural zone params (230-233)", "[pad_params][proxy]")
{
    TestController tc;

    tc.setParamNormalized(kUnnaturalModeStretchId, 0.8);
    double padStretch = tc.getParamNormalized(
        static_cast<ParamID>(padParamId(0, kPadModeStretch)));
    CHECK(padStretch == Approx(0.8).margin(0.01));
}

TEST_CASE("Controller: proxy covers morph params (240-244)", "[pad_params][proxy]")
{
    TestController tc;

    tc.setParamNormalized(kMorphEnabledId, 1.0);
    double padMorph = tc.getParamNormalized(
        static_cast<ParamID>(padParamId(0, kPadMorphEnabled)));
    CHECK(padMorph == Approx(1.0).margin(0.01));
}
