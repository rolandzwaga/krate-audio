#pragma once

// ==============================================================================
// Vorago Phase 12 - route table kParamRoutes / kMbRoutes (C-2, plan section 4.1)
// ==============================================================================
// Every registered parameter ID (108, plan section 3.2) carries exactly one Route:
//   MB    - a VoragoMacroMatrix target base (setTargetBase), mapped by kMbRoutes
//   VP    - a per-voice parameter (VoragoVoiceParams -> applyVoiceParams)
//   ENG   - a direct VoragoEngine setter
//   CV    - a direct CavernVerb setter outside the matrix
//   MAC   - a macro value (the 12 macros + channel pressure)
//   Local - consumed by the processor itself (master gain, sustain pedal)
// The table is data; the static_asserts below turn the plan section 3.2 totals
// into compile-time facts. Header-only, constexpr, no allocation.
// ==============================================================================

#include "plugin_ids.h"

#include <krate/dsp/systems/vorago_macro_matrix.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Vorago {

enum class Route : std::uint8_t { MB, VP, ENG, CV, MAC, Local };

struct ParamRouteEntry {
    Steinberg::Vst::ParamID id;
    Route route;
};

/// Plan section 3.2 Route column, ascending ID.
inline constexpr std::array<ParamRouteEntry, 108> kParamRoutes = {{
    // --- Global (0-99) ---
    {kMasterGainId, Route::Local},
    {kPolyphonyId, Route::ENG},
    {kSeedId, Route::ENG},
    {kOutputSaturationId, Route::MB},
    {kSustainPedalId, Route::Local},
    {kChannelPressureId, Route::MAC},
    // --- Macros (100-111) ---
    {kMacroDarknessId, Route::MAC},
    {kMacroAgeId, Route::MAC},
    {kMacroDensityId, Route::MAC},
    {kMacroMovementId, Route::MAC},
    {kMacroGravityId, Route::MAC},
    {kMacroEntropyId, Route::MAC},
    {kMacroPressureId, Route::MAC},
    {kMacroWeightId, Route::MAC},
    {kMacroFogId, Route::MAC},
    {kMacroLifeId, Route::MAC},
    {kMacroDepthId, Route::MAC},
    {kMacroMassId, Route::MAC},
    // --- Cloud (200-206) ---
    {kCloudRichnessId, Route::MB},
    {kCloudTiltId, Route::MB},
    {kCloudMutationId, Route::MB},
    {kCloudInharmonicityId, Route::MB},
    {kCloudDriftDepthId, Route::MB},
    {kCloudStereoSpreadId, Route::VP},
    {kCloudSpectralGravityId, Route::VP},
    // --- Noise (300-353) ---
    {kNoiseLevelId, Route::MB},
    {kNoiseWakeId, Route::MB},
    {kNoiseWanderRateId, Route::MB},
    {kNoiseSlot0ModelId, Route::VP},
    {kNoiseSlot1ModelId, Route::VP},
    {kNoiseSlot2ModelId, Route::VP},
    {kNoiseSlot3ModelId, Route::VP},
    {kNoiseSlot0TypeId, Route::VP},
    {kNoiseSlot1TypeId, Route::VP},
    {kNoiseSlot2TypeId, Route::VP},
    {kNoiseSlot3TypeId, Route::VP},
    {kNoiseSlot0CombFundamentalId, Route::VP},
    {kNoiseSlot1CombFundamentalId, Route::VP},
    {kNoiseSlot2CombFundamentalId, Route::VP},
    {kNoiseSlot3CombFundamentalId, Route::VP},
    {kNoiseSlot0CombSpreadId, Route::VP},
    {kNoiseSlot1CombSpreadId, Route::VP},
    {kNoiseSlot2CombSpreadId, Route::VP},
    {kNoiseSlot3CombSpreadId, Route::VP},
    {kNoiseSlot0CombFeedbackId, Route::VP},
    {kNoiseSlot1CombFeedbackId, Route::VP},
    {kNoiseSlot2CombFeedbackId, Route::VP},
    {kNoiseSlot3CombFeedbackId, Route::VP},
    // --- Resonance (400-403) ---
    {kResonanceGravityId, Route::MB},
    {kResonanceMixId, Route::MB},
    {kResonanceWanderRateId, Route::MB},
    {kResonanceAnchorModeId, Route::VP},
    // --- Ecology (500-515) ---
    {kEcologyMixId, Route::MB},
    {kEcologyLoopGainId, Route::MB},
    {kEcologyLoop0FilterModeId, Route::VP},
    {kEcologyLoop1FilterModeId, Route::VP},
    {kEcologyLoop2FilterModeId, Route::VP},
    {kEcologyLoop3FilterModeId, Route::VP},
    {kEcologyLoop4FilterModeId, Route::VP},
    {kEcologyLoop5FilterModeId, Route::VP},
    // --- Sub (600-612) ---
    {kSubLevelOffsetId, Route::MB},
    {kSubTrackingId, Route::MB},
    {kSubDiv2LevelId, Route::ENG},
    {kSubDiv4LevelId, Route::ENG},
    {kSubFifthBelowLevelId, Route::ENG},
    // --- Smear (700-702) ---
    {kSmearAmountId, Route::MB},
    {kSmearDecoherenceId, Route::MB},
    {kSmearTiltId, Route::MB},
    // --- Events (800) / Ecosystem (900) ---
    {kEventsRateScaleId, Route::MB},
    {kEcosystemDepthId, Route::MB},
    // --- Body (1000-1005) ---
    {kBodyBlendId, Route::MB},
    {kBodyDampingId, Route::MB},
    {kBodyResonanceId, Route::MB},
    {kBodyMixId, Route::MB},
    {kBodyMaterialAId, Route::VP},
    {kBodyMaterialBId, Route::VP},
    // --- Space (1100-1115) ---
    {kSpaceSizeId, Route::MB},
    {kSpaceDarknessId, Route::MB},
    {kSpaceDecayId, Route::MB},
    {kSpaceFogId, Route::MB},
    {kSpaceDamperDepthId, Route::MB},
    {kSpaceMixId, Route::MB},
    {kSpaceWidthId, Route::MB},
    {kSpaceDensityId, Route::CV},
    {kSpaceDimensionalityId, Route::CV},
    {kSpaceBreathId, Route::CV},
    {kSpaceEarlySizeId, Route::CV},
    {kSpaceEarlyLevelId, Route::CV},
    {kSpaceEarlyAbsorptionId, Route::CV},
    {kSpaceEarlySendId, Route::CV},
    {kSpaceDamperRateId, Route::CV},
    {kSpaceFreezeId, Route::CV},
    // --- Envelope (1200-1206) ---
    {kEnvelopeModeId, Route::ENG},
    {kEnvelopeStage0TimeId, Route::ENG},
    {kEnvelopeStage1TimeId, Route::ENG},
    {kEnvelopeStage2TimeId, Route::ENG},
    {kEnvelopeStage3TimeId, Route::ENG},
    {kEnvelopeReleaseId, Route::ENG},
    {kEnvelopeGrowthDurationId, Route::ENG},
    // --- Bloom (1300-1301) ---
    {kBloomDepthId, Route::MB},
    {kBloomSpawnRateId, Route::MB},
    // --- Ghost (1400-1403) ---
    {kGhostPeakLevelId, Route::MB},
    {kGhostBlurId, Route::MB},
    {kGhostReverseProbabilityId, Route::ENG},
    {kGhostEventTriggersId, Route::ENG},
    // --- Life (1500-1502) ---
    {kLifeBreathingDepthId, Route::MB},
    {kLifeBreathingIrregularityId, Route::MB},
    {kLifeTidalDepthId, Route::MB},
}};

/// Linear scan (108 entries); std::nullopt for any unregistered ID.
[[nodiscard]] constexpr std::optional<Route> routeOf(Steinberg::Vst::ParamID id) noexcept {
    for (const auto& e : kParamRoutes) {
        if (e.id == id)
            return e.route;
    }
    return std::nullopt;
}

struct MbRouteEntry {
    Steinberg::Vst::ParamID id;
    Krate::DSP::VoragoMacroTarget target;
};

/// Plan section 3.2 MB column, ascending ID: the matrix target each MB ID bases.
inline constexpr std::array<MbRouteEntry, 39> kMbRoutes = {{
    {kOutputSaturationId, Krate::DSP::VoragoMacroTarget::OutputSaturation},
    {kCloudRichnessId, Krate::DSP::VoragoMacroTarget::CloudRichness},
    {kCloudTiltId, Krate::DSP::VoragoMacroTarget::CloudSpectralTiltDb},
    {kCloudMutationId, Krate::DSP::VoragoMacroTarget::CloudMutation},
    {kCloudInharmonicityId, Krate::DSP::VoragoMacroTarget::CloudInharmonicity},
    {kCloudDriftDepthId, Krate::DSP::VoragoMacroTarget::CloudDriftDepthCents},
    {kNoiseLevelId, Krate::DSP::VoragoMacroTarget::NoiseLevelDb},
    {kNoiseWakeId, Krate::DSP::VoragoMacroTarget::NoiseWakeBase},
    {kNoiseWanderRateId, Krate::DSP::VoragoMacroTarget::NoiseWanderRate},
    {kResonanceGravityId, Krate::DSP::VoragoMacroTarget::ResonanceGravity},
    {kResonanceMixId, Krate::DSP::VoragoMacroTarget::ResonanceMix},
    {kResonanceWanderRateId, Krate::DSP::VoragoMacroTarget::ResonanceWanderRate},
    {kEcologyMixId, Krate::DSP::VoragoMacroTarget::EcologyMix},
    {kEcologyLoopGainId, Krate::DSP::VoragoMacroTarget::EcologyLoopGain},
    {kSubLevelOffsetId, Krate::DSP::VoragoMacroTarget::SubToneLevelOffsetDb},
    {kSubTrackingId, Krate::DSP::VoragoMacroTarget::SubTrackingAmount},
    {kSmearAmountId, Krate::DSP::VoragoMacroTarget::SmearAmount},
    {kSmearDecoherenceId, Krate::DSP::VoragoMacroTarget::SmearDecoherence},
    {kSmearTiltId, Krate::DSP::VoragoMacroTarget::SmearTilt},
    {kEventsRateScaleId, Krate::DSP::VoragoMacroTarget::EventRateScale},
    {kEcosystemDepthId, Krate::DSP::VoragoMacroTarget::EcosystemDepth},
    {kBodyBlendId, Krate::DSP::VoragoMacroTarget::BodyBlend},
    {kBodyDampingId, Krate::DSP::VoragoMacroTarget::BodyDamping},
    {kBodyResonanceId, Krate::DSP::VoragoMacroTarget::BodyResonance},
    {kBodyMixId, Krate::DSP::VoragoMacroTarget::BodyMix},
    {kSpaceSizeId, Krate::DSP::VoragoMacroTarget::CavernSize},
    {kSpaceDarknessId, Krate::DSP::VoragoMacroTarget::CavernDarkness},
    {kSpaceDecayId, Krate::DSP::VoragoMacroTarget::CavernDecaySeconds},
    {kSpaceFogId, Krate::DSP::VoragoMacroTarget::CavernFog},
    {kSpaceDamperDepthId, Krate::DSP::VoragoMacroTarget::CavernDamperDepth},
    {kSpaceMixId, Krate::DSP::VoragoMacroTarget::CavernMix},
    {kSpaceWidthId, Krate::DSP::VoragoMacroTarget::CavernWidth},
    {kBloomDepthId, Krate::DSP::VoragoMacroTarget::BloomDepth},
    {kBloomSpawnRateId, Krate::DSP::VoragoMacroTarget::BloomSpawnRateHz},
    {kGhostPeakLevelId, Krate::DSP::VoragoMacroTarget::GhostPeakLevel},
    {kGhostBlurId, Krate::DSP::VoragoMacroTarget::AtmosBlur},
    {kLifeBreathingDepthId, Krate::DSP::VoragoMacroTarget::BreathingDepth},
    {kLifeBreathingIrregularityId, Krate::DSP::VoragoMacroTarget::BreathingIrregularity},
    {kLifeTidalDepthId, Krate::DSP::VoragoMacroTarget::TidalDepth},
}};

/// Spec B-1 / B-2 (FR-060 contingent clause): macro-only targets with NO
/// registered parameter ID - the only VoragoMacroTargets absent from kMbRoutes.
inline constexpr std::array<Krate::DSP::VoragoMacroTarget, 2> kMacroOnlyTargets = {
    Krate::DSP::VoragoMacroTarget::ResonanceOctaveLock,
    Krate::DSP::VoragoMacroTarget::OutputDriveDb,
};

// ------------------------------------------------------------------------------
// Compile-time completeness (plan section 4.1)
// ------------------------------------------------------------------------------
namespace RouteTableDetail {

[[nodiscard]] constexpr bool idsStrictlyAscending() noexcept {
    for (std::size_t i = 1; i < kParamRoutes.size(); ++i) {
        if (!(kParamRoutes[i - 1].id < kParamRoutes[i].id))
            return false;
    }
    return true;
}

[[nodiscard]] constexpr std::size_t countRoute(Route r) noexcept {
    std::size_t n = 0;
    for (const auto& e : kParamRoutes)
        n += (e.route == r) ? 1u : 0u;
    return n;
}

[[nodiscard]] constexpr bool isMacroOnly(Krate::DSP::VoragoMacroTarget t) noexcept {
    for (const auto m : kMacroOnlyTargets) {
        if (m == t)
            return true;
    }
    return false;
}

/// Every non-macro-only target appears exactly once; macro-only targets never.
[[nodiscard]] constexpr bool mbTargetsComplete() noexcept {
    constexpr auto kCount = static_cast<std::size_t>(Krate::DSP::VoragoMacroTarget::Count);
    for (std::size_t t = 0; t < kCount; ++t) {
        const auto target = static_cast<Krate::DSP::VoragoMacroTarget>(t);
        std::size_t hits = 0;
        for (const auto& e : kMbRoutes)
            hits += (e.target == target) ? 1u : 0u;
        if (hits != (isMacroOnly(target) ? 0u : 1u))
            return false;
    }
    return true;
}

[[nodiscard]] constexpr bool mbIdsAreMbRouted() noexcept {
    for (const auto& e : kMbRoutes) {
        const auto r = routeOf(e.id);
        if (!r.has_value() || *r != Route::MB)
            return false;
    }
    return true;
}

}  // namespace RouteTableDetail

static_assert(RouteTableDetail::idsStrictlyAscending(), "kParamRoutes must be strictly ascending");
static_assert(RouteTableDetail::countRoute(Route::MB) == 39, "MB route count (plan 3.2)");
static_assert(RouteTableDetail::countRoute(Route::VP) == 31, "VP route count (plan 3.2)");
static_assert(RouteTableDetail::countRoute(Route::ENG) == 14, "ENG route count (plan 3.2)");
static_assert(RouteTableDetail::countRoute(Route::CV) == 9, "CV route count (plan 3.2)");
static_assert(RouteTableDetail::countRoute(Route::MAC) == 13, "MAC route count (plan 3.2)");
static_assert(RouteTableDetail::countRoute(Route::Local) == 2, "Local route count (plan 3.2)");
static_assert(RouteTableDetail::countRoute(Route::MB) == kMbRoutes.size(),
              "kMbRoutes must cover every MB-routed ID");
static_assert(static_cast<std::size_t>(Krate::DSP::VoragoMacroTarget::Count) ==
                  kMbRoutes.size() + kMacroOnlyTargets.size(),
              "every VoragoMacroTarget is either MB-routed or macro-only");
static_assert(RouteTableDetail::mbTargetsComplete(),
              "every non-macro-only VoragoMacroTarget appears in kMbRoutes exactly once");
static_assert(RouteTableDetail::mbIdsAreMbRouted(), "every kMbRoutes ID must have Route::MB");

}  // namespace Vorago
