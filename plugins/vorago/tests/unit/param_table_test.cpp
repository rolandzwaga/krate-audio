// ==============================================================================
// Vorago Phase 12 - parameter table and input hygiene (SC-010, SC-018)
// ==============================================================================
// Registered by T002 (specs/vorago-phase12-parameters/tasks.md) so no later task
// edits CMake. Filled by T014 (ID map), T015 (mapping), T031, T033, T036.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "engine/vorago_engine_config.h"
#include "param_table_expected.h"
#include "parameters/bloom_params.h"
#include "parameters/body_params.h"
#include "parameters/cloud_params.h"
#include "parameters/ecology_params.h"
#include "parameters/ecosystem_params.h"
#include "parameters/envelope_params.h"
#include "parameters/events_params.h"
#include "parameters/ghost_params.h"
#include "parameters/global_params.h"
#include "parameters/life_params.h"
#include "parameters/macro_params.h"
#include "parameters/noise_params.h"
#include "parameters/param_mapping.h"
#include "parameters/param_routes.h"
#include "parameters/resonance_params.h"
#include "parameters/smear_params.h"
#include "parameters/space_params.h"
#include "parameters/sub_params.h"
#include "plugin_ids.h"
#include "processor/processor.h"
#include "vorago_test_fixture.h"

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

using namespace Vorago;

// T014 / FR-010 / spec C-5: every Phase 12 ID value and every band range end.
TEST_CASE("Vorago_ParamIdMap", "[vorago][params]") {
    // --- Global (0-99); 0 and 1 shipped in Phase 11 and stay untouched ---
    STATIC_REQUIRE(kMasterGainId == 0);
    STATIC_REQUIRE(kPolyphonyId == 1);
    STATIC_REQUIRE(kSeedId == 2);
    STATIC_REQUIRE(kOutputSaturationId == 3);
    STATIC_REQUIRE(kSustainPedalId == 4);
    STATIC_REQUIRE(kChannelPressureId == 5);

    // --- Macros (100-199) shipped in Phase 11 ---
    STATIC_REQUIRE(kMacroDarknessId == 100);
    STATIC_REQUIRE(kMacroMassId == 111);

    // --- Cloud (200-299) ---
    STATIC_REQUIRE(kCloudRichnessId == 200);
    STATIC_REQUIRE(kCloudTiltId == 201);
    STATIC_REQUIRE(kCloudMutationId == 202);
    STATIC_REQUIRE(kCloudInharmonicityId == 203);
    STATIC_REQUIRE(kCloudDriftDepthId == 204);
    STATIC_REQUIRE(kCloudStereoSpreadId == 205);
    STATIC_REQUIRE(kCloudSpectralGravityId == 206);

    // --- Noise (300-399) ---
    STATIC_REQUIRE(kNoiseLevelId == 300);
    STATIC_REQUIRE(kNoiseWakeId == 301);
    STATIC_REQUIRE(kNoiseWanderRateId == 302);
    STATIC_REQUIRE(kNoiseSlot0ModelId == 310);
    STATIC_REQUIRE(kNoiseSlot1ModelId == 311);
    STATIC_REQUIRE(kNoiseSlot2ModelId == 312);
    STATIC_REQUIRE(kNoiseSlot3ModelId == 313);
    STATIC_REQUIRE(kNoiseSlot0TypeId == 320);
    STATIC_REQUIRE(kNoiseSlot1TypeId == 321);
    STATIC_REQUIRE(kNoiseSlot2TypeId == 322);
    STATIC_REQUIRE(kNoiseSlot3TypeId == 323);
    STATIC_REQUIRE(kNoiseSlot0CombFundamentalId == 330);
    STATIC_REQUIRE(kNoiseSlot1CombFundamentalId == 331);
    STATIC_REQUIRE(kNoiseSlot2CombFundamentalId == 332);
    STATIC_REQUIRE(kNoiseSlot3CombFundamentalId == 333);
    STATIC_REQUIRE(kNoiseSlot0CombSpreadId == 340);
    STATIC_REQUIRE(kNoiseSlot1CombSpreadId == 341);
    STATIC_REQUIRE(kNoiseSlot2CombSpreadId == 342);
    STATIC_REQUIRE(kNoiseSlot3CombSpreadId == 343);
    STATIC_REQUIRE(kNoiseSlot0CombFeedbackId == 350);
    STATIC_REQUIRE(kNoiseSlot1CombFeedbackId == 351);
    STATIC_REQUIRE(kNoiseSlot2CombFeedbackId == 352);
    STATIC_REQUIRE(kNoiseSlot3CombFeedbackId == 353);

    // --- Resonance (400-499) ---
    STATIC_REQUIRE(kResonanceGravityId == 400);
    STATIC_REQUIRE(kResonanceMixId == 401);
    STATIC_REQUIRE(kResonanceWanderRateId == 402);
    STATIC_REQUIRE(kResonanceAnchorModeId == 403);

    // --- Ecology (500-599) ---
    STATIC_REQUIRE(kEcologyMixId == 500);
    STATIC_REQUIRE(kEcologyLoopGainId == 501);
    STATIC_REQUIRE(kEcologyLoop0FilterModeId == 510);
    STATIC_REQUIRE(kEcologyLoop1FilterModeId == 511);
    STATIC_REQUIRE(kEcologyLoop2FilterModeId == 512);
    STATIC_REQUIRE(kEcologyLoop3FilterModeId == 513);
    STATIC_REQUIRE(kEcologyLoop4FilterModeId == 514);
    STATIC_REQUIRE(kEcologyLoop5FilterModeId == 515);

    // --- Sub (600-699) ---
    STATIC_REQUIRE(kSubLevelOffsetId == 600);
    STATIC_REQUIRE(kSubTrackingId == 601);
    STATIC_REQUIRE(kSubDiv2LevelId == 610);
    STATIC_REQUIRE(kSubDiv4LevelId == 611);
    STATIC_REQUIRE(kSubFifthBelowLevelId == 612);

    // --- Smear (700-799) ---
    STATIC_REQUIRE(kSmearAmountId == 700);
    STATIC_REQUIRE(kSmearDecoherenceId == 701);
    STATIC_REQUIRE(kSmearTiltId == 702);

    // --- Events (800-899) / Ecosystem (900-999) ---
    STATIC_REQUIRE(kEventsRateScaleId == 800);
    STATIC_REQUIRE(kEcosystemDepthId == 900);

    // --- Body (1000-1099) ---
    STATIC_REQUIRE(kBodyBlendId == 1000);
    STATIC_REQUIRE(kBodyDampingId == 1001);
    STATIC_REQUIRE(kBodyResonanceId == 1002);
    STATIC_REQUIRE(kBodyMixId == 1003);
    STATIC_REQUIRE(kBodyMaterialAId == 1004);
    STATIC_REQUIRE(kBodyMaterialBId == 1005);

    // --- Space (1100-1199) ---
    STATIC_REQUIRE(kSpaceSizeId == 1100);
    STATIC_REQUIRE(kSpaceDarknessId == 1101);
    STATIC_REQUIRE(kSpaceDecayId == 1102);
    STATIC_REQUIRE(kSpaceFogId == 1103);
    STATIC_REQUIRE(kSpaceDamperDepthId == 1104);
    STATIC_REQUIRE(kSpaceMixId == 1105);
    STATIC_REQUIRE(kSpaceWidthId == 1106);
    STATIC_REQUIRE(kSpaceDensityId == 1107);
    STATIC_REQUIRE(kSpaceDimensionalityId == 1108);
    STATIC_REQUIRE(kSpaceBreathId == 1109);
    STATIC_REQUIRE(kSpaceEarlySizeId == 1110);
    STATIC_REQUIRE(kSpaceEarlyLevelId == 1111);
    STATIC_REQUIRE(kSpaceEarlyAbsorptionId == 1112);
    STATIC_REQUIRE(kSpaceEarlySendId == 1113);
    STATIC_REQUIRE(kSpaceDamperRateId == 1114);
    STATIC_REQUIRE(kSpaceFreezeId == 1115);

    // --- Envelope (1200-1299, new band) ---
    STATIC_REQUIRE(kEnvelopeModeId == 1200);
    STATIC_REQUIRE(kEnvelopeStage0TimeId == 1201);
    STATIC_REQUIRE(kEnvelopeStage1TimeId == 1202);
    STATIC_REQUIRE(kEnvelopeStage2TimeId == 1203);
    STATIC_REQUIRE(kEnvelopeStage3TimeId == 1204);
    STATIC_REQUIRE(kEnvelopeReleaseId == 1205);
    STATIC_REQUIRE(kEnvelopeGrowthDurationId == 1206);

    // --- Bloom (1300-1399, new band) ---
    STATIC_REQUIRE(kBloomDepthId == 1300);
    STATIC_REQUIRE(kBloomSpawnRateId == 1301);

    // --- Ghost (1400-1499, new band) ---
    STATIC_REQUIRE(kGhostPeakLevelId == 1400);
    STATIC_REQUIRE(kGhostBlurId == 1401);
    STATIC_REQUIRE(kGhostReverseProbabilityId == 1402);
    STATIC_REQUIRE(kGhostEventTriggersId == 1403);

    // --- Life (1500-1599, new band) ---
    STATIC_REQUIRE(kLifeBreathingDepthId == 1500);
    STATIC_REQUIRE(kLifeBreathingIrregularityId == 1501);
    STATIC_REQUIRE(kLifeTidalDepthId == 1502);

    // --- Range ends (FR-043 range dispatch) ---
    STATIC_REQUIRE(kGlobalParamRangeEnd == 100);
    STATIC_REQUIRE(kMacroParamRangeEnd == 200);
    STATIC_REQUIRE(kCloudParamRangeEnd == 300);
    STATIC_REQUIRE(kNoiseParamRangeEnd == 400);
    STATIC_REQUIRE(kResonanceParamRangeEnd == 500);
    STATIC_REQUIRE(kEcologyParamRangeEnd == 600);
    STATIC_REQUIRE(kSubParamRangeEnd == 700);
    STATIC_REQUIRE(kSmearParamRangeEnd == 800);
    STATIC_REQUIRE(kEventsParamRangeEnd == 900);
    STATIC_REQUIRE(kEcosystemParamRangeEnd == 1000);
    STATIC_REQUIRE(kBodyParamRangeEnd == 1100);
    STATIC_REQUIRE(kSpaceParamRangeEnd == 1200);
    STATIC_REQUIRE(kEnvelopeParamRangeEnd == 1300);
    STATIC_REQUIRE(kBloomParamRangeEnd == 1400);
    STATIC_REQUIRE(kGhostParamRangeEnd == 1500);
    STATIC_REQUIRE(kLifeParamRangeEnd == 1600);

    // State version was bumped to 2 by T042 (plan 4.9).
    STATIC_REQUIRE(kCurrentStateVersion == 2);
}

// T015 / plan section 3.3, spec C-8: every mapping reference value, the seed table,
// the noise-type list and the cavern seed factory argument.
TEST_CASE("Vorago_ParamMapping", "[vorago][params]") {
    using Catch::Approx;

    SECTION("linear") {
        REQUIRE(linearFromNormalized(0.5, -12.0, 12.0) == 0.0);
        REQUIRE(linearToNormalized(-4.0, -12.0, 12.0) == Approx(1.0 / 3.0).epsilon(1e-12));
        REQUIRE(linearFromNormalized(-0.5, -12.0, 12.0) == -12.0);
        REQUIRE(linearFromNormalized(1.5, -12.0, 12.0) == 12.0);
        REQUIRE(linearToNormalized(-20.0, -12.0, 12.0) == 0.0);
        REQUIRE(linearToNormalized(20.0, -12.0, 12.0) == 1.0);
    }

    SECTION("offset-log (plan 3.3.1)") {
        // Envelope times: [0, 120000] ms, eps 10 ms.
        REQUIRE(offsetLogFromNormalized(0.5, 0.0, 120000.0, 10.0) ==
                Approx(1085.49).margin(0.01));
        REQUIRE(offsetLogFromNormalized(0.5, 0.0, 120000.0, 10.0) ==
                Approx(1085.4907576).epsilon(1e-6));
        REQUIRE(offsetLogFromNormalized(0.0, 0.0, 120000.0, 10.0) == 0.0);
        REQUIRE(offsetLogFromNormalized(1.0, 0.0, 120000.0, 10.0) == 120000.0);
        REQUIRE(offsetLogFromNormalized(-0.5, 0.0, 120000.0, 10.0) == 0.0);
        REQUIRE(offsetLogFromNormalized(1.5, 0.0, 120000.0, 10.0) == 120000.0);
        // Bloom spawn rate: [0, 0.05] Hz, eps 1e-4 Hz.
        REQUIRE(offsetLogFromNormalized(0.5, 0.0, 0.05, 1e-4) == Approx(0.002138).margin(1e-6));
        // Space damper rate: [0, 1], eps 0.01.
        REQUIRE(offsetLogFromNormalized(0.5, 0.0, 1.0, 0.01) == Approx(0.090499).margin(1e-6));

        // Inverses (1e-9).
        REQUIRE(offsetLogToNormalized(20000.0, 0.0, 120000.0, 10.0) ==
                Approx(0.809284413).margin(1e-9));
        REQUIRE(offsetLogToNormalized(30000.0, 0.0, 120000.0, 10.0) ==
                Approx(0.852434578).margin(1e-9));
        REQUIRE(offsetLogToNormalized(45000.0, 0.0, 120000.0, 10.0) ==
                Approx(0.895590654).margin(1e-9));
        REQUIRE(offsetLogToNormalized(60000.0, 0.0, 120000.0, 10.0) ==
                Approx(0.926212855).margin(1e-9));
        REQUIRE(offsetLogToNormalized(1.0 / 240.0, 0.0, 0.05, 1e-4) ==
                Approx(0.603772849).margin(1e-9));
        REQUIRE(offsetLogToNormalized(0.15, 0.0, 1.0, 0.01) == Approx(0.600761933).margin(1e-9));
        REQUIRE(offsetLogToNormalized(0.0, 0.0, 120000.0, 10.0) == 0.0);
        REQUIRE(offsetLogToNormalized(120000.0, 0.0, 120000.0, 10.0) == Approx(1.0).margin(1e-12));
    }

    SECTION("log (Krate::Plugins::logMapFromNormalized)") {
        using Krate::Plugins::logMapFromNormalized;
        using Krate::Plugins::logMapToNormalized;
        REQUIRE(logMapFromNormalized(0.5, 0.01, 100.0) == Approx(1.0).epsilon(1e-4));
        REQUIRE(logMapFromNormalized(0.5, 20.0, 19845.0) == Approx(630.0).margin(0.05));
        REQUIRE(logMapFromNormalized(0.5, 20.0, 19845.0) == Approx(630.0).epsilon(1e-4));
        REQUIRE(logMapFromNormalized(0.5, 0.002, 1.0) == Approx(0.044721).epsilon(1e-4));
        REQUIRE(logMapFromNormalized(0.5, 0.1, 10.0) == Approx(1.0).epsilon(1e-4));
        REQUIRE(logMapFromNormalized(0.5, 0.5, 60.0) == Approx(5.4772).epsilon(1e-4));
        REQUIRE(logMapFromNormalized(0.5, 80.0, 300.0) == Approx(154.919).epsilon(1e-4));
        REQUIRE(logMapFromNormalized(0.5, 1.0, 120.0) == Approx(10.9545).epsilon(1e-4));

        REQUIRE(logMapToNormalized(60.0, 20.0, 19845.0) == Approx(0.159219747).margin(1e-9));
        REQUIRE(logMapToNormalized(0.03, 0.01, 100.0) == Approx(0.119280314).margin(1e-9));
        REQUIRE(logMapToNormalized(0.03, 0.002, 1.0) == Approx(0.435755587).margin(1e-9));
        REQUIRE(logMapToNormalized(20.0, 0.5, 60.0) == Approx(0.770524453).margin(1e-9));
        REQUIRE(logMapToNormalized(220.0, 80.0, 300.0) == Approx(0.765346277).margin(1e-9));
    }

    SECTION("discrete") {
        REQUIRE(indexFromNormalized(5.0 / 11.0, 12) == 5);
        REQUIRE(indexToNormalized(6, 11) == Approx(0.6).margin(1e-12));
        REQUIRE(indexFromNormalized(-1.0, 12) == 0);
        REQUIRE(indexFromNormalized(2.0, 12) == 11);
        REQUIRE(indexFromNormalized(0.0, 12) == 0);
        REQUIRE(indexFromNormalized(1.0, 12) == 11);
        REQUIRE(indexToNormalized(-3, 11) == 0.0);
        REQUIRE(indexToNormalized(99, 11) == 1.0);
        REQUIRE(indexToNormalized(0, 1) == 0.0);
        // Round trip over every index of a 16-entry list.
        for (int i = 0; i < 16; ++i)
            REQUIRE(indexFromNormalized(indexToNormalized(i, 16), 16) == i);
    }

    SECTION("seed table (C-8)") {
        STATIC_REQUIRE(kVoragoSeedValues.size() == 16);
        STATIC_REQUIRE(kVoragoSeedValues[0] == 1u);
        STATIC_REQUIRE(kCavernSeedSalt == 0x43415645u);
        for (std::size_t i = 0; i < kVoragoSeedValues.size(); ++i) {
            for (std::size_t j = i + 1; j < kVoragoSeedValues.size(); ++j) {
                INFO("i = " << i << ", j = " << j);
                REQUIRE(kVoragoSeedValues[i] != kVoragoSeedValues[j]);
            }
        }
        STATIC_REQUIRE(cavernSeedFor(0) == 1u);
        for (int i = 1; i < 16; ++i) {
            INFO("i = " << i);
            REQUIRE(cavernSeedFor(i) ==
                    (kVoragoSeedValues[static_cast<std::size_t>(i)] ^ 0x43415645u));
        }
    }

    SECTION("noise-type list (plan 3.3.3)") {
        using Krate::DSP::NoiseType;
        STATIC_REQUIRE(kNoiseTypeByIndex.size() == 12);
        STATIC_REQUIRE(kNoiseTypeByIndex[5] == NoiseType::Brown);
        STATIC_REQUIRE(kNoiseTypeByIndex[11] == NoiseType::RadioStatic);
        for (std::size_t i = 0; i <= 10; ++i) {
            INFO("i = " << i);
            REQUIRE(static_cast<std::size_t>(kNoiseTypeByIndex[i]) == i);
        }
        for (std::size_t i = 0; i < kNoiseTypeByIndex.size(); ++i) {
            INFO("i = " << i);
            REQUIRE(kNoiseTypeByIndex[i] != NoiseType::ModulationNoise);
            REQUIRE(noiseTypeToIndex(kNoiseTypeByIndex[i]) == static_cast<int>(i));
        }
        STATIC_REQUIRE(noiseTypeToIndex(NoiseType::ModulationNoise) == 2);
    }

    SECTION("cavern config seed argument") {
        REQUIRE(makeVoragoCavernConfig(2048).seed == kCavernSeed);
        REQUIRE(makeVoragoCavernConfig(2048, 77u).seed == 77u);
    }
}

// T031 / spec C-2, plan sections 3.2 and 4.1: the 108-entry ID -> Route table and
// the 39-entry MB target mapping.
TEST_CASE("Vorago_RouteTable", "[vorago][params]") {
    using Krate::DSP::VoragoMacroTarget;

    SECTION("size, order and per-route counts") {
        REQUIRE(kParamRoutes.size() == 108);
        for (std::size_t i = 1; i < kParamRoutes.size(); ++i) {
            INFO("i = " << i);
            REQUIRE(kParamRoutes[i - 1].id < kParamRoutes[i].id);
        }
        std::array<int, 6> counts{};
        for (const auto& e : kParamRoutes)
            ++counts[static_cast<std::size_t>(e.route)];
        REQUIRE(counts[static_cast<std::size_t>(Route::MB)] == 39);
        REQUIRE(counts[static_cast<std::size_t>(Route::VP)] == 31);
        REQUIRE(counts[static_cast<std::size_t>(Route::ENG)] == 14);
        REQUIRE(counts[static_cast<std::size_t>(Route::CV)] == 9);
        REQUIRE(counts[static_cast<std::size_t>(Route::MAC)] == 13);
        REQUIRE(counts[static_cast<std::size_t>(Route::Local)] == 2);
    }

    SECTION("routeOf spot checks") {
        REQUIRE(routeOf(kMasterGainId) == Route::Local);
        REQUIRE(routeOf(kSustainPedalId) == Route::Local);
        REQUIRE(routeOf(kChannelPressureId) == Route::MAC);
        REQUIRE(routeOf(kOutputSaturationId) == Route::MB);
        REQUIRE(routeOf(kSeedId) == Route::ENG);
        REQUIRE(routeOf(kPolyphonyId) == Route::ENG);
        REQUIRE(routeOf(kCloudStereoSpreadId) == Route::VP);
        REQUIRE(routeOf(kSpaceFreezeId) == Route::CV);
        REQUIRE(routeOf(kGhostReverseProbabilityId) == Route::ENG);
        for (Steinberg::Vst::ParamID id = 100; id <= 111; ++id) {
            INFO("id = " << id);
            REQUIRE(routeOf(id) == Route::MAC);
        }
        REQUIRE_FALSE(routeOf(207).has_value());
        REQUIRE_FALSE(routeOf(1600).has_value());
    }

    SECTION("MB completeness") {
        REQUIRE(kMbRoutes.size() == 39);
        // Phase 12 B-1 / B-2: the two macro-only targets have no registered ID.
        REQUIRE(static_cast<std::size_t>(VoragoMacroTarget::Count) ==
                kMbRoutes.size() + kMacroOnlyTargets.size());
        for (std::size_t t = 0; t < static_cast<std::size_t>(VoragoMacroTarget::Count); ++t) {
            const auto target = static_cast<VoragoMacroTarget>(t);
            bool macroOnly = false;
            for (const auto m : kMacroOnlyTargets)
                macroOnly = macroOnly || (m == target);
            int hits = 0;
            for (const auto& e : kMbRoutes)
                hits += (e.target == target) ? 1 : 0;
            INFO("target = " << t);
            REQUIRE(hits == (macroOnly ? 0 : 1));
        }
        for (const auto& e : kMbRoutes) {
            INFO("id = " << e.id);
            REQUIRE(routeOf(e.id) == Route::MB);
        }
    }

    SECTION("MB mapping spot checks") {
        const auto targetOf = [](Steinberg::Vst::ParamID id) {
            for (const auto& e : kMbRoutes)
                if (e.id == id)
                    return static_cast<int>(e.target);
            return -1;
        };
        REQUIRE(targetOf(3) == static_cast<int>(VoragoMacroTarget::OutputSaturation));
        REQUIRE(targetOf(600) == static_cast<int>(VoragoMacroTarget::SubToneLevelOffsetDb));
        REQUIRE(targetOf(1102) == static_cast<int>(VoragoMacroTarget::CavernDecaySeconds));
        REQUIRE(targetOf(1301) == static_cast<int>(VoragoMacroTarget::BloomSpawnRateHz));
        REQUIRE(targetOf(1401) == static_cast<int>(VoragoMacroTarget::AtmosBlur));
    }
}

// ==============================================================================
// T033 / FR-041, SC-018: the controller registers, describes and formats all 108
// parameters exactly as the checked-in table (unit/param_table_expected.h) says.
// ==============================================================================

namespace {

std::string tableAscii(const Steinberg::Vst::String128 s) {
    char buf[128] = {};
    Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(s), 128)  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        .toAscii(buf, 128);
    return {buf};
}

/// |a - b| <= tol * max(|b|, 1): relative for large values, absolute near zero.
bool closeRel(double a, double b, double tol) {
    return std::abs(a - b) <= tol * std::max(std::abs(b), 1.0);
}

/// The pack's own DOUBLE-precision denormalisation for a continuous ID, built from
/// the constants and mapping functions each pack header exports (the same ones its
/// handle...ParamChange uses). std::nullopt for discrete or unknown IDs.
std::optional<double> packDenormalize(Steinberg::Vst::ParamID id, double n) {
    using Krate::Plugins::logMapFromNormalized;
    namespace V = ::Vorago;
    switch (id) {
        case V::kMasterGainId: return n * 2.0;  // handleGlobalParamChange: value * 2
        case V::kOutputSaturationId:
        case V::kSustainPedalId:
        case V::kChannelPressureId: return V::linearFromNormalized(n, 0.0, 1.0);
        default: break;
    }
    if (id >= V::kMacroDarknessId && id <= V::kMacroMassId) { return n; }  // identity
    for (const auto& s : V::kCloudParamSpecs) {
        if (s.id == id) { return V::linearFromNormalized(n, s.minPlain, s.maxPlain); }
    }
    if (id == V::kNoiseLevelId)
        return V::linearFromNormalized(n, V::kNoiseLevelMinDb, V::kNoiseLevelMaxDb);
    if (id == V::kNoiseWakeId) return V::linearFromNormalized(n, 0.0, 1.0);
    if (id == V::kNoiseWanderRateId)
        return logMapFromNormalized(n, V::kNoiseWanderMinHz, V::kNoiseWanderMaxHz);
    if (id >= V::kNoiseSlot0CombFundamentalId && id <= V::kNoiseSlot3CombFundamentalId)
        return logMapFromNormalized(n, V::kNoiseCombMinHz, V::kNoiseCombMaxHz);
    if (id >= V::kNoiseSlot0CombSpreadId && id <= V::kNoiseSlot3CombSpreadId)
        return V::linearFromNormalized(n, 0.0, 1.0);
    if (id >= V::kNoiseSlot0CombFeedbackId && id <= V::kNoiseSlot3CombFeedbackId)
        return V::linearFromNormalized(n, 0.0, V::kNoiseCombFeedbackMax);
    if (id == V::kResonanceGravityId) return V::resonanceGravityFromNormalized(n);
    if (id == V::kResonanceMixId) return V::resonanceMixFromNormalized(n);
    if (id == V::kResonanceWanderRateId) return V::resonanceWanderRateFromNormalized(n);
    if (id == V::kEcologyMixId) return V::linearFromNormalized(n, 0.0, 1.0);
    if (id == V::kEcologyLoopGainId)
        return V::linearFromNormalized(n, 0.0, V::kEcologyLoopGainMaxPlain);
    if (const int i = V::detail::subIndexOf(id); i >= 0) {
        const auto& r = V::detail::kSubRanges[static_cast<std::size_t>(i)];
        return V::linearFromNormalized(n, r.mn, r.mx);
    }
    if (id == V::kSmearAmountId)
        return V::linearFromNormalized(n, V::kSmearAmountMin, V::kSmearAmountMax);
    if (id == V::kSmearDecoherenceId)
        return V::linearFromNormalized(n, V::kSmearDecoherenceMin, V::kSmearDecoherenceMax);
    if (id == V::kSmearTiltId)
        return V::linearFromNormalized(n, V::kSmearTiltMin, V::kSmearTiltMax);
    if (id == V::kEventsRateScaleId)
        return logMapFromNormalized(n, V::kEventsRateScaleMin, V::kEventsRateScaleMax);
    if (id == V::kEcosystemDepthId)
        return V::linearFromNormalized(n, V::kEcosystemDepthMin, V::kEcosystemDepthMax);
    if (id >= V::kBodyBlendId && id <= V::kBodyMixId)
        return V::linearFromNormalized(n, 0.0, 1.0);  // handleBodyParamChange lin01
    for (const auto sid : V::kSpaceFloatIds) {
        if (sid == id) { return V::spaceFloatFromNormalized(id, n); }
    }
    if (id >= V::kEnvelopeStage0TimeId && id <= V::kEnvelopeReleaseId)
        return V::detail::envelopeTimeFromNormalized(n);
    if (id == V::kEnvelopeGrowthDurationId) return V::detail::growthDurationFromNormalized(n);
    if (id == V::kBloomDepthId)
        return V::linearFromNormalized(n, V::kBloomDepthMin, V::kBloomDepthMax);
    if (id == V::kBloomSpawnRateId)
        return V::offsetLogFromNormalized(n, V::kBloomSpawnRateMinHz, V::kBloomSpawnRateMaxHz,
                                          V::kBloomSpawnRateEpsHz);
    if (id >= V::kGhostPeakLevelId && id <= V::kGhostReverseProbabilityId)
        return V::linearFromNormalized(n, 0.0, 1.0);
    if (id >= V::kLifeBreathingDepthId && id <= V::kLifeTidalDepthId)
        return V::linearFromNormalized(n, V::kLifeMin, V::kLifeMax);
    return std::nullopt;
}

/// The value a pack's handle...ParamChange actually STORES (float atomic) for a
/// continuous ID at normalized n, read back as double. std::nullopt for others.
std::optional<double> packHandledPlain(Steinberg::Vst::ParamID id, double n) {
    namespace V = ::Vorago;
    const auto rd = [](const std::atomic<float>& a) { return static_cast<double>(a.load()); };
    if (id == V::kMasterGainId || id == V::kOutputSaturationId || id == V::kSustainPedalId ||
        id == V::kChannelPressureId) {
        V::GlobalParams p;
        V::handleGlobalParamChange(p, id, n);
        if (id == V::kMasterGainId) return rd(p.masterGain);
        if (id == V::kOutputSaturationId) return rd(p.outputSaturation);
        if (id == V::kSustainPedalId) return rd(p.sustainPedal);
        return rd(p.channelPressure);
    }
    if (id >= V::kMacroDarknessId && id <= V::kMacroMassId) {
        V::MacroParams p;
        V::handleMacroParamChange(p, id, n);
        return rd(V::macroField(p, static_cast<int>(id - V::kMacroDarknessId)));
    }
    if (id >= V::kCloudRichnessId && id <= V::kCloudSpectralGravityId) {
        V::CloudParams p;
        V::handleCloudParamChange(p, id, n);
        return rd(V::cloudField(p, static_cast<int>(id - V::kCloudRichnessId)));
    }
    if (id >= V::kNoiseLevelId && id < V::kNoiseParamRangeEnd) {
        V::NoiseParams p;
        V::handleNoiseParamChange(p, id, n);
        if (id == V::kNoiseLevelId) return rd(p.levelDb);
        if (id == V::kNoiseWakeId) return rd(p.wake);
        if (id == V::kNoiseWanderRateId) return rd(p.wanderRateHz);
        if (id >= V::kNoiseSlot0CombFundamentalId && id <= V::kNoiseSlot3CombFundamentalId)
            return rd(p.combFundamentalHz[static_cast<std::size_t>(
                id - V::kNoiseSlot0CombFundamentalId)]);
        if (id >= V::kNoiseSlot0CombSpreadId && id <= V::kNoiseSlot3CombSpreadId)
            return rd(p.combSpread[static_cast<std::size_t>(id - V::kNoiseSlot0CombSpreadId)]);
        if (id >= V::kNoiseSlot0CombFeedbackId && id <= V::kNoiseSlot3CombFeedbackId)
            return rd(
                p.combFeedback[static_cast<std::size_t>(id - V::kNoiseSlot0CombFeedbackId)]);
        return std::nullopt;
    }
    if (id >= V::kResonanceGravityId && id <= V::kResonanceWanderRateId) {
        V::ResonanceParams p;
        V::handleResonanceParamChange(p, id, n);
        if (id == V::kResonanceGravityId) return rd(p.gravity);
        if (id == V::kResonanceMixId) return rd(p.mix);
        return rd(p.wanderRateHz);
    }
    if (id == V::kEcologyMixId || id == V::kEcologyLoopGainId) {
        V::EcologyParams p;
        V::handleEcologyParamChange(p, id, n);
        return id == V::kEcologyMixId ? rd(p.mix) : rd(p.loopGain);
    }
    if (const int i = V::detail::subIndexOf(id); i >= 0) {
        V::SubParams p;
        V::handleSubParamChange(p, id, n);
        return rd(V::detail::subField(p, i));
    }
    if (id >= V::kSmearAmountId && id <= V::kSmearTiltId) {
        V::SmearParams p;
        V::handleSmearParamChange(p, id, n);
        if (id == V::kSmearAmountId) return rd(p.amount);
        if (id == V::kSmearDecoherenceId) return rd(p.decoherence);
        return rd(p.tilt);
    }
    if (id == V::kEventsRateScaleId) {
        V::EventsParams p;
        V::handleEventsParamChange(p, id, n);
        return rd(p.eventRateScale);
    }
    if (id == V::kEcosystemDepthId) {
        V::EcosystemParams p;
        V::handleEcosystemParamChange(p, id, n);
        return rd(p.depth);
    }
    if (id >= V::kBodyBlendId && id <= V::kBodyMixId) {
        V::BodyParams p;
        V::handleBodyParamChange(p, id, n);
        if (id == V::kBodyBlendId) return rd(p.blend);
        if (id == V::kBodyDampingId) return rd(p.damping);
        if (id == V::kBodyResonanceId) return rd(p.resonance);
        return rd(p.mix);
    }
    if (id >= V::kSpaceSizeId && id <= V::kSpaceDamperRateId) {
        V::SpaceParams p;
        V::handleSpaceParamChange(p, id, n);
        const std::atomic<float>* f = V::spaceFloatField(p, id);
        if (f == nullptr) return std::nullopt;
        return rd(*f);
    }
    if (id >= V::kEnvelopeStage0TimeId && id <= V::kEnvelopeGrowthDurationId) {
        V::EnvelopeParams p;
        V::handleEnvelopeParamChange(p, id, n);
        if (id == V::kEnvelopeGrowthDurationId) return rd(p.growthDurationSeconds);
        return rd(*V::detail::envelopeTimeField(p, id));
    }
    if (id == V::kBloomDepthId || id == V::kBloomSpawnRateId) {
        V::BloomParams p;
        V::handleBloomParamChange(p, id, n);
        return id == V::kBloomDepthId ? rd(p.depth) : rd(p.spawnRateHz);
    }
    if (id >= V::kGhostPeakLevelId && id <= V::kGhostReverseProbabilityId) {
        V::GhostParams p;
        V::handleGhostParamChange(p, id, n);
        if (id == V::kGhostPeakLevelId) return rd(p.peakLevel);
        if (id == V::kGhostBlurId) return rd(p.blur);
        return rd(p.reverseProbability);
    }
    if (id >= V::kLifeBreathingDepthId && id <= V::kLifeTidalDepthId) {
        V::LifeParams p;
        V::handleLifeParamChange(p, id, n);
        if (id == V::kLifeBreathingDepthId) return rd(p.breathingDepth);
        if (id == V::kLifeBreathingIrregularityId) return rd(p.breathingIrregularity);
        return rd(p.tidalDepth);
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("Vorago_ParameterInfoTable", "[vorago][params]") {
    using VoragoTest::kExpectedParams;

    auto controller = Steinberg::owned(new ::Vorago::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);

    SECTION("count and band order") {
        REQUIRE(controller->getParameterCount() == 108);
        REQUIRE(kExpectedParams.size() == 108);
        // Registration order == band order == ascending ID == the table's row order.
        for (Steinberg::int32 i = 0; i < 108; ++i) {
            Steinberg::Vst::ParameterInfo info{};
            REQUIRE(controller->getParameterInfo(i, info) == Steinberg::kResultOk);
            INFO("index " << i);
            REQUIRE(info.id == kExpectedParams[static_cast<std::size_t>(i)].id);
        }
    }

    SECTION("title, units, stepCount, flags, default") {
        for (const auto& row : kExpectedParams) {
            INFO("id " << row.id << " (" << row.title << ")");
            auto* p = controller->getParameterObject(row.id);
            REQUIRE(p != nullptr);
            const Steinberg::Vst::ParameterInfo& info = p->getInfo();
            CHECK(tableAscii(info.title) == row.title);
            CHECK(tableAscii(info.units) == row.units);
            CHECK(info.stepCount == row.stepCount);
            CHECK(info.flags == row.flags);
            // Strict 1e-9: no Catch::Approx, whose default relative epsilon (~1.2e-5)
            // would admit a float-widened default.
            INFO("registered " << info.defaultNormalizedValue << " expected "
                               << row.defaultNormalized);
            CHECK(std::abs(info.defaultNormalizedValue - row.defaultNormalized) <= 1e-9);
        }
    }

    SECTION("every ID formats non-empty at 0, 0.5, 1") {
        for (const auto& row : kExpectedParams) {
            for (const double v : {0.0, 0.5, 1.0}) {
                INFO("id " << row.id << " v " << v);
                Steinberg::Vst::String128 s{};
                REQUIRE(controller->getParamStringByValue(row.id, v, s) == Steinberg::kResultOk);
                CHECK_FALSE(tableAscii(s).empty());
            }
        }
    }

    SECTION("taper: pack denormalisation at n = 0.5 equals the table") {
        int continuousRows = 0;
        for (const auto& row : kExpectedParams) {
            if (row.taper == ::Vorago::Taper::Discrete) { continue; }
            ++continuousRows;
            INFO("id " << row.id << " (" << row.title << ") expected " << row.mid);

            // The table's own (taper, range, eps) reproduces its plan 3.3.1 midpoint.
            double fromTable = 0.0;
            switch (row.taper) {
                case ::Vorago::Taper::Log:
                    fromTable =
                        Krate::Plugins::logMapFromNormalized(0.5, row.minPlain, row.maxPlain);
                    break;
                case ::Vorago::Taper::OffsetLog:
                    fromTable = ::Vorago::offsetLogFromNormalized(0.5, row.minPlain,
                                                                  row.maxPlain, row.eps);
                    break;
                default:
                    fromTable = ::Vorago::linearFromNormalized(0.5, row.minPlain, row.maxPlain);
                    break;
            }
            CHECK(closeRel(fromTable, row.mid, 1e-9));

            // The pack's double mapping at 0.5 (relative 1e-9) ...
            const auto packMid = packDenormalize(row.id, 0.5);
            REQUIRE(packMid.has_value());
            CHECK(closeRel(*packMid, row.mid, 1e-9));
            // ... whose endpoints are the table's plain range ...
            const auto packLo = packDenormalize(row.id, 0.0);
            const auto packHi = packDenormalize(row.id, 1.0);
            REQUIRE(packLo.has_value());
            REQUIRE(packHi.has_value());
            CHECK(closeRel(*packLo, row.minPlain, 1e-9));
            CHECK(closeRel(*packHi, row.maxPlain, 1e-9));

            // ... and the handler really stores that value (float atomic precision).
            const auto stored = packHandledPlain(row.id, 0.5);
            REQUIRE(stored.has_value());
            CHECK(closeRel(*stored, row.mid, 1e-6));
        }
        // 85 group-A rows + Sustain Pedal (4, group C but a continuous Vst::Parameter).
        REQUIRE(continuousRows == 86);
    }

    SECTION("body materials: list index n == BodyMaterial n") {
        using BM = Krate::DSP::ContinuousBody::BodyMaterial;
        constexpr std::array<BM, 11> kOrder = {
            BM::Glass,     BM::Strings,     BM::MetalPlate, BM::Chamber,
            BM::Ice,       BM::StoneChamber, BM::SteelTank, BM::WoodenHull,
            BM::CathedralColumn, BM::CavernWall, BM::GlassSphere};
        constexpr std::array<const char*, 11> kLabels = {
            "Glass",      "Strings",    "Metal Plate",      "Chamber",
            "Ice",        "Stone Chamber", "Steel Tank",    "Wooden Hull",
            "Cathedral Column", "Cavern Wall", "Glass Sphere"};
        for (const Steinberg::Vst::ParamID id :
             {static_cast<Steinberg::Vst::ParamID>(::Vorago::kBodyMaterialAId),
              static_cast<Steinberg::Vst::ParamID>(::Vorago::kBodyMaterialBId)}) {
            for (int n = 0; n < 11; ++n) {
                INFO("id " << id << " index " << n);
                const double v = static_cast<double>(n) / 10.0;
                ::Vorago::BodyParams p;
                ::Vorago::handleBodyParamChange(p, id, v);
                const int stored = (id == ::Vorago::kBodyMaterialAId) ? p.materialA.load()
                                                                      : p.materialB.load();
                REQUIRE(stored == n);
                REQUIRE(static_cast<BM>(stored) == kOrder[static_cast<std::size_t>(n)]);
                REQUIRE(controller->getParameterObject(id)->toPlain(v) ==
                        static_cast<double>(n));
                Steinberg::Vst::String128 s{};
                REQUIRE(controller->getParamStringByValue(id, v, s) == Steinberg::kResultOk);
                REQUIRE(tableAscii(s) == kLabels[static_cast<std::size_t>(n)]);
            }
        }
    }

    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}

// ==============================================================================
// T036 / FR-013, SC-010: processParameterChanges input hygiene. Unregistered IDs
// change nothing; NaN / +-Inf change nothing; out-of-range values clamp to the
// plain range end; one valid change per pack reaches that pack's atomic.
// ==============================================================================

namespace {

using PacksView = ::Vorago::Processor::PacksForTest;

/// The plain value an ID's atomic holds, read through the processor's const seam,
/// in the units of kExpectedParams (discrete rows in INDEX units, so Polyphony's
/// stored voice count [1, 6] is read back as index [0, 5]). The 108 registered IDs
/// cover every atomic of every pack exactly once (6 + 12 + 7 + 23 + 4 + 8 + 5 + 3 +
/// 1 + 1 + 6 + 16 + 7 + 2 + 4 + 3 == 108), so a snapshot over them is a snapshot
/// of every atomic. std::nullopt for an unknown ID.
std::optional<double> packsReadPlain(const PacksView& p, Steinberg::Vst::ParamID id) {
    namespace V = ::Vorago;
    const auto rf = [](const std::atomic<float>& a) { return static_cast<double>(a.load()); };
    const auto ri = [](const std::atomic<int>& a) { return static_cast<double>(a.load()); };
    const auto slot = [id](Steinberg::Vst::ParamID first) {
        return static_cast<std::size_t>(id - first);
    };
    switch (id) {
        case V::kMasterGainId: return rf(p.global.masterGain);
        case V::kPolyphonyId: return ri(p.global.polyphony) - 1.0;
        case V::kSeedId: return ri(p.global.seedIndex);
        case V::kOutputSaturationId: return rf(p.global.outputSaturation);
        case V::kSustainPedalId: return rf(p.global.sustainPedal);
        case V::kChannelPressureId: return rf(p.global.channelPressure);
        default: break;
    }
    if (id >= V::kMacroDarknessId && id <= V::kMacroMassId)
        return rf(V::macroField(p.macro, static_cast<int>(id - V::kMacroDarknessId)));
    if (id >= V::kCloudRichnessId && id <= V::kCloudSpectralGravityId)
        return rf(V::cloudField(p.cloud, static_cast<int>(id - V::kCloudRichnessId)));
    if (id == V::kNoiseLevelId) return rf(p.noise.levelDb);
    if (id == V::kNoiseWakeId) return rf(p.noise.wake);
    if (id == V::kNoiseWanderRateId) return rf(p.noise.wanderRateHz);
    if (id >= V::kNoiseSlot0ModelId && id <= V::kNoiseSlot3ModelId)
        return ri(p.noise.model[slot(V::kNoiseSlot0ModelId)]);
    if (id >= V::kNoiseSlot0TypeId && id <= V::kNoiseSlot3TypeId)
        return ri(p.noise.type[slot(V::kNoiseSlot0TypeId)]);
    if (id >= V::kNoiseSlot0CombFundamentalId && id <= V::kNoiseSlot3CombFundamentalId)
        return rf(p.noise.combFundamentalHz[slot(V::kNoiseSlot0CombFundamentalId)]);
    if (id >= V::kNoiseSlot0CombSpreadId && id <= V::kNoiseSlot3CombSpreadId)
        return rf(p.noise.combSpread[slot(V::kNoiseSlot0CombSpreadId)]);
    if (id >= V::kNoiseSlot0CombFeedbackId && id <= V::kNoiseSlot3CombFeedbackId)
        return rf(p.noise.combFeedback[slot(V::kNoiseSlot0CombFeedbackId)]);
    if (id == V::kResonanceGravityId) return rf(p.resonance.gravity);
    if (id == V::kResonanceMixId) return rf(p.resonance.mix);
    if (id == V::kResonanceWanderRateId) return rf(p.resonance.wanderRateHz);
    if (id == V::kResonanceAnchorModeId) return ri(p.resonance.anchorMode);
    if (id == V::kEcologyMixId) return rf(p.ecology.mix);
    if (id == V::kEcologyLoopGainId) return rf(p.ecology.loopGain);
    if (id >= V::kEcologyLoop0FilterModeId && id <= V::kEcologyLoop5FilterModeId)
        return ri(p.ecology.loopFilterMode[slot(V::kEcologyLoop0FilterModeId)]);
    if (const int i = V::detail::subIndexOf(id); i >= 0) return rf(V::detail::subField(p.sub, i));
    if (id == V::kSmearAmountId) return rf(p.smear.amount);
    if (id == V::kSmearDecoherenceId) return rf(p.smear.decoherence);
    if (id == V::kSmearTiltId) return rf(p.smear.tilt);
    if (id == V::kEventsRateScaleId) return rf(p.events.eventRateScale);
    if (id == V::kEcosystemDepthId) return rf(p.ecosystem.depth);
    if (id == V::kBodyBlendId) return rf(p.body.blend);
    if (id == V::kBodyDampingId) return rf(p.body.damping);
    if (id == V::kBodyResonanceId) return rf(p.body.resonance);
    if (id == V::kBodyMixId) return rf(p.body.mix);
    if (id == V::kBodyMaterialAId) return ri(p.body.materialA);
    if (id == V::kBodyMaterialBId) return ri(p.body.materialB);
    if (id == V::kSpaceFreezeId) return ri(p.space.freeze);
    if (id >= V::kSpaceSizeId && id <= V::kSpaceDamperRateId) {
        const std::atomic<float>* f = V::spaceFloatField(p.space, id);
        if (f == nullptr) return std::nullopt;
        return rf(*f);
    }
    if (id == V::kEnvelopeModeId) return ri(p.envelope.mode);
    if (id == V::kEnvelopeStage0TimeId) return rf(p.envelope.stage0TimeMs);
    if (id == V::kEnvelopeStage1TimeId) return rf(p.envelope.stage1TimeMs);
    if (id == V::kEnvelopeStage2TimeId) return rf(p.envelope.stage2TimeMs);
    if (id == V::kEnvelopeStage3TimeId) return rf(p.envelope.stage3TimeMs);
    if (id == V::kEnvelopeReleaseId) return rf(p.envelope.releaseMs);
    if (id == V::kEnvelopeGrowthDurationId) return rf(p.envelope.growthDurationSeconds);
    if (id == V::kBloomDepthId) return rf(p.bloom.depth);
    if (id == V::kBloomSpawnRateId) return rf(p.bloom.spawnRateHz);
    if (id == V::kGhostPeakLevelId) return rf(p.ghost.peakLevel);
    if (id == V::kGhostBlurId) return rf(p.ghost.blur);
    if (id == V::kGhostReverseProbabilityId) return rf(p.ghost.reverseProbability);
    if (id == V::kGhostEventTriggersId) return ri(p.ghost.eventTriggers);
    if (id == V::kLifeBreathingDepthId) return rf(p.life.breathingDepth);
    if (id == V::kLifeBreathingIrregularityId) return rf(p.life.breathingIrregularity);
    if (id == V::kLifeTidalDepthId) return rf(p.life.tidalDepth);
    return std::nullopt;
}

using PackSnapshot = std::array<std::uint64_t, VoragoTest::kNumExpectedParams>;

/// Bit patterns of every atomic (float and int widen to double exactly), table order.
PackSnapshot snapshotPacks(const ::Vorago::Processor& proc) {
    const PacksView packs = proc.packsForTest();
    PackSnapshot s{};
    for (std::size_t i = 0; i < VoragoTest::kNumExpectedParams; ++i) {
        const auto v = packsReadPlain(packs, VoragoTest::kExpectedParams[i].id);
        REQUIRE(v.has_value());
        s[i] = std::bit_cast<std::uint64_t>(*v);
    }
    return s;
}

/// One parameter-only process() call carrying a single one-point queue.
void sendOneChange(VoragoTest::ProcessorFixture& fx, VoragoTest::MultiParamChanges& changes,
                   Steinberg::Vst::ParamID id, double value) {
    changes.clear();
    changes.addQueue(id).addTestPoint(0, value);
    REQUIRE(fx.processNoOutputs(&changes) == Steinberg::kResultOk);
    changes.clear();
}

/// Plain-range test. The float atomic of a log / offset-log endpoint may round a
/// hair past the double endpoint, so the ends carry relative 1e-6 (absolute near 0).
bool insideRange(double v, double mn, double mx) {
    return v >= mn - 1e-6 * std::max(std::abs(mn), 1.0) &&
           v <= mx + 1e-6 * std::max(std::abs(mx), 1.0);
}

}  // namespace

TEST_CASE("Vorago_ParamInputHygiene", "[vorago][params]") {
    using VoragoTest::kExpectedParams;
    VoragoTest::ProcessorFixture fx;
    VoragoTest::MultiParamChanges changes;
    changes.reserve(4);

    SECTION("unregistered IDs change nothing") {
        constexpr std::array<Steinberg::Vst::ParamID, 18> kUnregistered = {
            6, 99, 112, 207, 399, 516, 613, 703, 801, 901, 1006, 1116, 1207, 1302, 1404, 1503,
            1599, 1600};
        for (const auto id : kUnregistered) {
            INFO("unregistered id " << id);
            const PackSnapshot before = snapshotPacks(*fx.proc);
            sendOneChange(fx, changes, id, 0.9);
            const PackSnapshot after = snapshotPacks(*fx.proc);
            for (std::size_t i = 0; i < kExpectedParams.size(); ++i) {
                INFO("atomic of id " << kExpectedParams[i].id);
                CHECK(after[i] == before[i]);
            }
        }
    }

    SECTION("non-finite values change nothing; out-of-range values clamp") {
        // Built from bit patterns: std::numeric_limits / std::isnan are unreliable
        // under -ffast-math (constitution, FR-062).
        const double kNaN = std::bit_cast<double>(std::uint64_t{0x7FF8000000000000ULL});
        const double kPosInf = std::bit_cast<double>(std::uint64_t{0x7FF0000000000000ULL});
        const double kNegInf = std::bit_cast<double>(std::uint64_t{0xFFF0000000000000ULL});
        REQUIRE_FALSE(Krate::DSP::detail::isFinite(kNaN));
        REQUIRE_FALSE(Krate::DSP::detail::isFinite(kPosInf));
        REQUIRE_FALSE(Krate::DSP::detail::isFinite(kNegInf));

        struct BadValue {
            const char* name;
            double value;
            bool finite;
        };
        const std::array<BadValue, 5> kBad = {{{.name="NaN", .value=kNaN, .finite=false},
                                               {.name="+Inf", .value=kPosInf, .finite=false},
                                               {.name="-Inf", .value=kNegInf, .finite=false},
                                               {.name="-0.5", .value=-0.5, .finite=true},
                                               {.name="1.5", .value=1.5, .finite=true}}};

        for (std::size_t target = 0; target < kExpectedParams.size(); ++target) {
            const auto& row = kExpectedParams[target];
            for (const BadValue& bad : kBad) {
                INFO("id " << row.id << " (" << row.title << ") value " << bad.name);
                const PackSnapshot before = snapshotPacks(*fx.proc);
                sendOneChange(fx, changes, row.id, bad.value);
                const PackSnapshot after = snapshotPacks(*fx.proc);
                const PacksView packs = fx.proc->packsForTest();

                // Every atomic finite and inside its plain range.
                for (const auto& r : kExpectedParams) {
                    const auto v = packsReadPlain(packs, r.id);
                    REQUIRE(v.has_value());
                    INFO("atomic of id " << r.id << " = " << *v);
                    CHECK(Krate::DSP::detail::isFinite(*v));
                    CHECK(insideRange(*v, r.minPlain, r.maxPlain));
                }

                // Every OTHER atomic bit-identical to the pre-snapshot.
                for (std::size_t i = 0; i < kExpectedParams.size(); ++i) {
                    if (i == target) { continue; }
                    INFO("other atomic of id " << kExpectedParams[i].id);
                    CHECK(after[i] == before[i]);
                }

                // The target: unchanged for NaN / Inf, the range end for -0.5 / 1.5.
                if (!bad.finite) {
                    CHECK(after[target] == before[target]);
                } else {
                    const auto v = packsReadPlain(packs, row.id);
                    REQUIRE(v.has_value());
                    const double expected = (bad.value < 0.0) ? row.minPlain : row.maxPlain;
                    INFO("stored " << *v << " expected " << expected);
                    CHECK(closeRel(*v, expected, 1e-6));
                }
            }
        }
    }

    SECTION("one valid change per pack reaches that pack's atomic") {
        // The FR-013 example: Cloud Tilt [-12, 12] at 0.25 -> -6 dB/oct.
        sendOneChange(fx, changes, ::Vorago::kCloudTiltId, 0.25);
        REQUIRE(fx.proc->packsForTest().cloud.tiltDb.load() == -6.0f);

        // One continuous ID per band (global ... life); expected = the pack's own
        // double denormalisation at 0.25 (packDenormalize above).
        constexpr std::array<Steinberg::Vst::ParamID, 16> kOnePerPack = {
            ::Vorago::kOutputSaturationId,       ::Vorago::kMacroDensityId,
            ::Vorago::kCloudTiltId,              ::Vorago::kNoiseWakeId,
            ::Vorago::kResonanceMixId,           ::Vorago::kEcologyMixId,
            ::Vorago::kSubTrackingId,            ::Vorago::kSmearAmountId,
            ::Vorago::kEventsRateScaleId,        ::Vorago::kEcosystemDepthId,
            ::Vorago::kBodyDampingId,            ::Vorago::kSpaceDecayId,
            ::Vorago::kEnvelopeGrowthDurationId, ::Vorago::kBloomDepthId,
            ::Vorago::kGhostBlurId,              ::Vorago::kLifeBreathingIrregularityId};
        for (const auto id : kOnePerPack) {
            INFO("id " << id);
            sendOneChange(fx, changes, id, 0.25);
            const auto expected = packDenormalize(id, 0.25);
            REQUIRE(expected.has_value());
            const auto stored = packsReadPlain(fx.proc->packsForTest(), id);
            REQUIRE(stored.has_value());
            INFO("stored " << *stored << " expected " << *expected);
            CHECK(closeRel(*stored, *expected, 1e-6));
        }
    }
}
