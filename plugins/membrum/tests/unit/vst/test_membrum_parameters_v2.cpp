// ==============================================================================
// Membrum VST Parameter Contract Tests (Phase 2 — T133)
// ==============================================================================
// Implements the test coverage requirements from
// `specs/137-membrum-phase2-exciters-bodies/contracts/vst_parameter_contract.md`:
//   1. Parameter count == 34 (5 Phase-1 + 29 Phase-2).
//   2. No duplicate parameter IDs across all 34 parameters.
//   3. Per-parameter random round-trip via setParamNormalized()/getParamNormalized()
//      is bit-identical (SC-006 parameter layer).
//   4. StringListParameter for Exciter Type and Body Model: toPlain(0..1) spans
//      6 integer values 0..5 correctly.
//   5. Real-time safety: setParamNormalized() makes zero heap allocations.
//      (allocation_detector global new/delete overrides live in
//       test_allocation_matrix.cpp.)
//   6. Phase-1 backward compatibility: load a version-1 state via
//      setComponentState(); assert controller exposes Phase-2 default values.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"

#include "public.sdk/source/common/memorystream.h"

#include <allocation_detector.h>

#include <array>
#include <cstdint>
#include <random>
#include <set>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

constexpr int kExpectedParameterCount = 1226;  // Phase 2 (34) + Phase 3 (3) + Phase 4 (1 + 1152) + Phase 5 (4) + Phase 6 US4 (32 per-pad coupling amounts)
constexpr int kExpectedExciterCount   = 6;
constexpr int kExpectedBodyCount      = 6;

// Centralised list of every parameter the controller is required to expose
// for Phase 2. Keep in lock-step with `plugin_ids.h` and `controller.cpp`.
struct ExpectedParam
{
    ParamID id;
    bool    isStringList;
    int     stepCountIfList;  // 0 for non-list parameters
};

// Global params to spot-check (first 37 Phase 1-3 params)
constexpr int kGlobalParamCount = 37;
const std::array<ExpectedParam, kGlobalParamCount> kGlobalExpectedParams = {{
    { Membrum::kMaterialId,       false, 0 },
    { Membrum::kSizeId,           false, 0 },
    { Membrum::kDecayId,          false, 0 },
    { Membrum::kStrikePositionId, false, 0 },
    { Membrum::kLevelId,          false, 0 },
    { Membrum::kExciterTypeId, true, kExpectedExciterCount - 1 },
    { Membrum::kBodyModelId,   true, kExpectedBodyCount    - 1 },
    { Membrum::kExciterFMRatioId,            false, 0 },
    { Membrum::kExciterFeedbackAmountId,     false, 0 },
    { Membrum::kExciterNoiseBurstDurationId, false, 0 },
    { Membrum::kExciterFrictionPressureId,   false, 0 },
    { Membrum::kToneShaperFilterTypeId,       false, 0 },
    { Membrum::kToneShaperFilterCutoffId,     false, 0 },
    { Membrum::kToneShaperFilterResonanceId,  false, 0 },
    { Membrum::kToneShaperFilterEnvAmountId,  false, 0 },
    { Membrum::kToneShaperDriveAmountId,      false, 0 },
    { Membrum::kToneShaperFoldAmountId,       false, 0 },
    { Membrum::kToneShaperPitchEnvStartId,    false, 0 },
    { Membrum::kToneShaperPitchEnvEndId,      false, 0 },
    { Membrum::kToneShaperPitchEnvTimeId,     false, 0 },
    { Membrum::kToneShaperPitchEnvCurveId,    false, 0 },
    { Membrum::kToneShaperFilterEnvAttackId,  false, 0 },
    { Membrum::kToneShaperFilterEnvDecayId,   false, 0 },
    { Membrum::kToneShaperFilterEnvSustainId, false, 0 },
    { Membrum::kToneShaperFilterEnvReleaseId, false, 0 },
    { Membrum::kUnnaturalModeStretchId,       false, 0 },
    { Membrum::kUnnaturalDecaySkewId,         false, 0 },
    { Membrum::kUnnaturalModeInjectAmountId,  false, 0 },
    { Membrum::kUnnaturalNonlinearCouplingId, false, 0 },
    { Membrum::kMorphEnabledId,    false, 0 },
    { Membrum::kMorphStartId,      false, 0 },
    { Membrum::kMorphEndId,        false, 0 },
    { Membrum::kMorphDurationMsId, false, 0 },
    { Membrum::kMorphCurveId,      false, 0 },
    { Membrum::kMaxPolyphonyId,    false, 0 },
    { Membrum::kVoiceStealingId,   true,  2 },
    { Membrum::kChokeGroupId,      false, 0 },
}};

} // namespace

// ==============================================================================
// (1) Parameter count
// ==============================================================================
TEST_CASE("Phase 2 contract: controller exposes exactly 34 parameters",
          "[membrum][vst][params][contract]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    CHECK(controller.getParameterCount() == kExpectedParameterCount);

    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// (2) Parameter ID uniqueness — no duplicate IDs across all 34 parameters
// ==============================================================================
TEST_CASE("Phase 2 contract: all 34 parameter IDs are unique and registered",
          "[membrum][vst][params][contract]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    const int32 paramCount = controller.getParameterCount();
    REQUIRE(paramCount == kExpectedParameterCount);

    std::set<ParamID> registeredIds;
    for (int32 i = 0; i < paramCount; ++i)
    {
        ParameterInfo info{};
        REQUIRE(controller.getParameterInfo(i, info) == kResultOk);
        const auto inserted = registeredIds.insert(info.id).second;
        INFO("Duplicate parameter ID at index " << i << ": " << info.id);
        CHECK(inserted);
    }

    // Every expected ID must be present.
    for (const auto& exp : kGlobalExpectedParams)
    {
        INFO("Missing expected parameter ID " << exp.id);
        CHECK(registeredIds.count(exp.id) == 1);
    }

    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// (3) Round-trip random normalized values via setParamNormalized() /
//     getParamNormalized() — bit-identical (SC-006 parameter layer).
// ==============================================================================
TEST_CASE("Phase 2 contract: setParamNormalized round-trips bit-exact for all 34 params",
          "[membrum][vst][params][contract]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Deterministic RNG so failures are reproducible.
    std::mt19937 rng(0xC0FFEEu);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    for (const auto& exp : kGlobalExpectedParams)
    {
        for (int trial = 0; trial < 5; ++trial)
        {
            const double rawValue = dist(rng);
            // EditController::setParamNormalized returns kResultTrue on success
            // (not kResultOk).
            const tresult setRes = controller.setParamNormalized(exp.id, rawValue);
            REQUIRE(setRes == kResultTrue);

            const ParamValue readBack = controller.getParamNormalized(exp.id);

            // Both base Parameter and RangeParameter store the raw normalized
            // value (clamped to [0,1]). StringListParameter does NOT quantise
            // in setNormalized() — quantisation happens only in toPlain(). So
            // the round-trip is bit-exact for every parameter type when the
            // input lies in [0,1].
            INFO("Param ID " << exp.id << " trial " << trial
                               << " raw=" << rawValue
                               << " readBack=" << readBack);
            CHECK(readBack == rawValue);
        }
    }

    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// (4) StringListParameter range coverage:
//     Exciter Type and Body Model each map normalized 0..1 to 6 distinct
//     plain integer values 0..5.
// ==============================================================================
TEST_CASE("Phase 2 contract: Exciter Type StringListParameter spans 6 integer values",
          "[membrum][vst][params][contract]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto* param = controller.getParameterObject(Membrum::kExciterTypeId);
    REQUIRE(param != nullptr);
    CHECK(param->getInfo().stepCount == kExpectedExciterCount - 1);

    std::set<int> seen;
    for (int idx = 0; idx < kExpectedExciterCount; ++idx)
    {
        const double norm = static_cast<double>(idx) /
                            static_cast<double>(kExpectedExciterCount - 1);
        const auto plain = static_cast<int>(param->toPlain(norm));
        INFO("Exciter Type idx=" << idx << " norm=" << norm << " plain=" << plain);
        CHECK(plain == idx);
        seen.insert(plain);
    }
    CHECK(seen.size() == static_cast<size_t>(kExpectedExciterCount));
    CHECK(*seen.begin() == 0);
    CHECK(*seen.rbegin() == kExpectedExciterCount - 1);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Phase 2 contract: Body Model StringListParameter spans 6 integer values",
          "[membrum][vst][params][contract]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto* param = controller.getParameterObject(Membrum::kBodyModelId);
    REQUIRE(param != nullptr);
    CHECK(param->getInfo().stepCount == kExpectedBodyCount - 1);

    std::set<int> seen;
    for (int idx = 0; idx < kExpectedBodyCount; ++idx)
    {
        const double norm = static_cast<double>(idx) /
                            static_cast<double>(kExpectedBodyCount - 1);
        const auto plain = static_cast<int>(param->toPlain(norm));
        INFO("Body Model idx=" << idx << " norm=" << norm << " plain=" << plain);
        CHECK(plain == idx);
        seen.insert(plain);
    }
    CHECK(seen.size() == static_cast<size_t>(kExpectedBodyCount));
    CHECK(*seen.begin() == 0);
    CHECK(*seen.rbegin() == kExpectedBodyCount - 1);

    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// (5) Real-time safety: setParamNormalized() must not allocate.
// AllocationDetector global new/delete overrides live in
// test_allocation_matrix.cpp and are part of this membrum_tests TU set.
// ==============================================================================
TEST_CASE("Phase 2 contract: setParamNormalized makes zero heap allocations",
          "[membrum][vst][params][contract][rt-safety]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Pre-touch every parameter once outside the tracking scope so any first-
    // call lazy host-side allocation (none expected, but defensive) is excluded.
    for (const auto& exp : kGlobalExpectedParams)
        REQUIRE(controller.setParamNormalized(exp.id, 0.5) == kResultOk);

    {
        TestHelpers::AllocationScope scope;
        for (const auto& exp : kGlobalExpectedParams)
        {
            for (int trial = 0; trial < 4; ++trial)
            {
                const double v = 0.1 + 0.2 * trial;
                (void)controller.setParamNormalized(exp.id, v);
                (void)controller.getParamNormalized(exp.id);
            }
        }
    }

    const auto allocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
    INFO("Heap allocations during 34 * 4 setParamNormalized calls: " << allocs);
    CHECK(allocs == 0u);

    REQUIRE(controller.terminate() == kResultOk);
}

// ==============================================================================
// (6) Phase 1 backward compatibility:
//     A version-1 state blob loaded via setComponentState() must produce the
//     Phase 2 defaults for all new parameters.
// ==============================================================================
TEST_CASE("Phase 2 contract: version-1 state loads Phase-2 defaults via setComponentState",
          "[membrum][vst][params][contract][backcompat]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Build a Phase-1 state blob: int32 version=1 + 5 x float64 in
    // Material/Size/Decay/StrikePos/Level order.
    auto* stream = new MemoryStream();
    int32 version = 1;
    stream->write(&version, sizeof(version), nullptr);
    const double phase1[] = { 0.21, 0.72, 0.13, 0.84, 0.55 };
    for (double p : phase1)
        stream->write(&p, sizeof(p), nullptr);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    REQUIRE(controller.setComponentState(stream) == kResultOk);
    stream->release();

    // Phase-1 parameters should reflect the loaded values.
    CHECK(controller.getParamNormalized(Membrum::kMaterialId)       == Approx(0.21));
    CHECK(controller.getParamNormalized(Membrum::kSizeId)           == Approx(0.72));
    CHECK(controller.getParamNormalized(Membrum::kDecayId)          == Approx(0.13));
    CHECK(controller.getParamNormalized(Membrum::kStrikePositionId) == Approx(0.84));
    CHECK(controller.getParamNormalized(Membrum::kLevelId)          == Approx(0.55));

    // Selectors must default to Impulse / Membrane (index 0). For a
    // StringListParameter, the controller stores normalized = (idx+0.5)/count
    // before quantisation, which the host then quantises back to the nearest
    // step. The plain integer value is what we ultimately care about — the UI
    // will display "Impulse" / "Membrane".
    {
        auto* excParam = controller.getParameterObject(Membrum::kExciterTypeId);
        REQUIRE(excParam != nullptr);
        const auto excPlain =
            static_cast<int>(excParam->toPlain(controller.getParamNormalized(Membrum::kExciterTypeId)));
        CHECK(excPlain == 0);  // Impulse

        auto* bodyParam = controller.getParameterObject(Membrum::kBodyModelId);
        REQUIRE(bodyParam != nullptr);
        const auto bodyPlain =
            static_cast<int>(bodyParam->toPlain(controller.getParamNormalized(Membrum::kBodyModelId)));
        CHECK(bodyPlain == 0);  // Membrane
    }

    // The 27 continuous Phase-2 parameters should reflect their compiled-in
    // defaults from controller.cpp's kPhase2Specs table.
    struct DefaultEntry { ParamID id; double value; };
    const DefaultEntry kDefaults[] = {
        { Membrum::kExciterFMRatioId,            0.133333 },
        { Membrum::kExciterFeedbackAmountId,     0.0      },
        { Membrum::kExciterNoiseBurstDurationId, 0.230769 },
        { Membrum::kExciterFrictionPressureId,   0.3      },

        { Membrum::kToneShaperFilterTypeId,      0.0      },
        { Membrum::kToneShaperFilterCutoffId,    1.0      },
        { Membrum::kToneShaperFilterResonanceId, 0.0      },
        { Membrum::kToneShaperFilterEnvAmountId, 0.5      },
        { Membrum::kToneShaperDriveAmountId,     0.0      },
        { Membrum::kToneShaperFoldAmountId,      0.0      },
        { Membrum::kToneShaperPitchEnvStartId,   0.070721 },
        { Membrum::kToneShaperPitchEnvEndId,     0.0      },
        { Membrum::kToneShaperPitchEnvTimeId,    0.0      },
        { Membrum::kToneShaperPitchEnvCurveId,   0.0      },

        { Membrum::kToneShaperFilterEnvAttackId,  0.0 },
        { Membrum::kToneShaperFilterEnvDecayId,   0.1 },
        { Membrum::kToneShaperFilterEnvSustainId, 0.0 },
        { Membrum::kToneShaperFilterEnvReleaseId, 0.1 },

        { Membrum::kUnnaturalModeStretchId,       0.333333 },
        { Membrum::kUnnaturalDecaySkewId,         0.5      },
        { Membrum::kUnnaturalModeInjectAmountId,  0.0      },
        { Membrum::kUnnaturalNonlinearCouplingId, 0.0      },

        { Membrum::kMorphEnabledId,    0.0      },
        { Membrum::kMorphStartId,      1.0      },
        { Membrum::kMorphEndId,        0.0      },
        { Membrum::kMorphDurationMsId, 0.095477 },
        { Membrum::kMorphCurveId,      0.0      },
    };
    static_assert(sizeof(kDefaults) / sizeof(kDefaults[0]) == 27,
                  "Phase 2 defaults table must list all 27 continuous params");

    for (const auto& def : kDefaults)
    {
        const ParamValue actual = controller.getParamNormalized(def.id);
        INFO("Default for ParamID " << def.id
             << " expected=" << def.value
             << " actual=" << actual);
        CHECK(actual == Approx(def.value).margin(1e-6));
    }

    REQUIRE(controller.terminate() == kResultOk);
}
