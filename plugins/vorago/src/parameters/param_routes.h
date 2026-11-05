#pragma once

// ==============================================================================
// Vorago Phase 12 - route table kParamRoutes / kMbRoutes (C-2, plan section 4.1)
// ==============================================================================
// Every registered parameter ID (110, plan 3.2 + Phase 14 FR-074) carries exactly one Route:
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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Vorago {

// `Macro`, not `MAC`: VSTGUI defines the macro MAC on macOS (vstgui/lib/vstguibase.h), and a TU that
// includes both the controller (VSTGUI) and this header would otherwise fail to parse the enumerator.
enum class Route : std::uint8_t { MB, VP, ENG, CV, Macro, Local };

struct ParamRouteEntry {
    Steinberg::Vst::ParamID id;
    Route route;
};

/// Plan section 3.2 Route column, ascending ID.
inline constexpr std::array<ParamRouteEntry, 110> kParamRoutes = {{
    // --- Global (0-99) ---
    {.id = kMasterGainId, .route = Route::Local},
    {.id = kPolyphonyId, .route = Route::ENG},
    {.id = kSeedId, .route = Route::ENG},
    {.id = kOutputSaturationId, .route = Route::MB},
    {.id = kSustainPedalId, .route = Route::Local},
    {.id = kChannelPressureId, .route = Route::Macro},
    // --- Macros (100-111) ---
    {.id = kMacroDarknessId, .route = Route::Macro},
    {.id = kMacroAgeId, .route = Route::Macro},
    {.id = kMacroDensityId, .route = Route::Macro},
    {.id = kMacroMovementId, .route = Route::Macro},
    {.id = kMacroGravityId, .route = Route::Macro},
    {.id = kMacroEntropyId, .route = Route::Macro},
    {.id = kMacroPressureId, .route = Route::Macro},
    {.id = kMacroWeightId, .route = Route::Macro},
    {.id = kMacroFogId, .route = Route::Macro},
    {.id = kMacroLifeId, .route = Route::Macro},
    {.id = kMacroDepthId, .route = Route::Macro},
    {.id = kMacroMassId, .route = Route::Macro},
    // --- Cloud (200-206) ---
    {.id = kCloudRichnessId, .route = Route::MB},
    {.id = kCloudTiltId, .route = Route::MB},
    {.id = kCloudMutationId, .route = Route::MB},
    {.id = kCloudInharmonicityId, .route = Route::MB},
    {.id = kCloudDriftDepthId, .route = Route::MB},
    {.id = kCloudStereoSpreadId, .route = Route::VP},
    {.id = kCloudSpectralGravityId, .route = Route::VP},
    // --- Noise (300-353) ---
    {.id = kNoiseLevelId, .route = Route::MB},
    {.id = kNoiseWakeId, .route = Route::MB},
    {.id = kNoiseWanderRateId, .route = Route::MB},
    {.id = kNoiseSlot0ModelId, .route = Route::VP},
    {.id = kNoiseSlot1ModelId, .route = Route::VP},
    {.id = kNoiseSlot2ModelId, .route = Route::VP},
    {.id = kNoiseSlot3ModelId, .route = Route::VP},
    {.id = kNoiseSlot0TypeId, .route = Route::VP},
    {.id = kNoiseSlot1TypeId, .route = Route::VP},
    {.id = kNoiseSlot2TypeId, .route = Route::VP},
    {.id = kNoiseSlot3TypeId, .route = Route::VP},
    {.id = kNoiseSlot0CombFundamentalId, .route = Route::VP},
    {.id = kNoiseSlot1CombFundamentalId, .route = Route::VP},
    {.id = kNoiseSlot2CombFundamentalId, .route = Route::VP},
    {.id = kNoiseSlot3CombFundamentalId, .route = Route::VP},
    {.id = kNoiseSlot0CombSpreadId, .route = Route::VP},
    {.id = kNoiseSlot1CombSpreadId, .route = Route::VP},
    {.id = kNoiseSlot2CombSpreadId, .route = Route::VP},
    {.id = kNoiseSlot3CombSpreadId, .route = Route::VP},
    {.id = kNoiseSlot0CombFeedbackId, .route = Route::VP},
    {.id = kNoiseSlot1CombFeedbackId, .route = Route::VP},
    {.id = kNoiseSlot2CombFeedbackId, .route = Route::VP},
    {.id = kNoiseSlot3CombFeedbackId, .route = Route::VP},
    // --- Resonance (400-403) ---
    {.id = kResonanceGravityId, .route = Route::MB},
    {.id = kResonanceMixId, .route = Route::MB},
    {.id = kResonanceWanderRateId, .route = Route::MB},
    {.id = kResonanceAnchorModeId, .route = Route::VP},
    // --- Ecology (500-515) ---
    {.id = kEcologyMixId, .route = Route::MB},
    {.id = kEcologyLoopGainId, .route = Route::MB},
    {.id = kEcologyLoop0FilterModeId, .route = Route::VP},
    {.id = kEcologyLoop1FilterModeId, .route = Route::VP},
    {.id = kEcologyLoop2FilterModeId, .route = Route::VP},
    {.id = kEcologyLoop3FilterModeId, .route = Route::VP},
    {.id = kEcologyLoop4FilterModeId, .route = Route::VP},
    {.id = kEcologyLoop5FilterModeId, .route = Route::VP},
    // --- Sub (600-612) ---
    {.id = kSubLevelOffsetId, .route = Route::MB},
    {.id = kSubTrackingId, .route = Route::MB},
    {.id = kSubDiv2LevelId, .route = Route::ENG},
    {.id = kSubDiv4LevelId, .route = Route::ENG},
    {.id = kSubFifthBelowLevelId, .route = Route::ENG},
    // --- Smear (700-702) ---
    {.id = kSmearAmountId, .route = Route::MB},
    {.id = kSmearDecoherenceId, .route = Route::MB},
    {.id = kSmearTiltId, .route = Route::MB},
    // --- Events (800) / Ecosystem (900-902) ---
    {.id = kEventsRateScaleId, .route = Route::MB},
    {.id = kEcosystemDepthId, .route = Route::MB},
    {.id = kEcosystemSyncRateId, .route = Route::VP},      // Phase 14 FR-074
    {.id = kEcosystemSelfAffinityId, .route = Route::VP},  // Phase 14 FR-074
    // --- Body (1000-1005) ---
    {.id = kBodyBlendId, .route = Route::MB},
    {.id = kBodyDampingId, .route = Route::MB},
    {.id = kBodyResonanceId, .route = Route::MB},
    {.id = kBodyMixId, .route = Route::MB},
    {.id = kBodyMaterialAId, .route = Route::VP},
    {.id = kBodyMaterialBId, .route = Route::VP},
    // --- Space (1100-1115) ---
    {.id = kSpaceSizeId, .route = Route::MB},
    {.id = kSpaceDarknessId, .route = Route::MB},
    {.id = kSpaceDecayId, .route = Route::MB},
    {.id = kSpaceFogId, .route = Route::MB},
    {.id = kSpaceDamperDepthId, .route = Route::MB},
    {.id = kSpaceMixId, .route = Route::MB},
    {.id = kSpaceWidthId, .route = Route::MB},
    {.id = kSpaceDensityId, .route = Route::CV},
    {.id = kSpaceDimensionalityId, .route = Route::CV},
    {.id = kSpaceBreathId, .route = Route::CV},
    {.id = kSpaceEarlySizeId, .route = Route::CV},
    {.id = kSpaceEarlyLevelId, .route = Route::CV},
    {.id = kSpaceEarlyAbsorptionId, .route = Route::CV},
    {.id = kSpaceEarlySendId, .route = Route::CV},
    {.id = kSpaceDamperRateId, .route = Route::CV},
    {.id = kSpaceFreezeId, .route = Route::CV},
    // --- Envelope (1200-1206) ---
    {.id = kEnvelopeModeId, .route = Route::ENG},
    {.id = kEnvelopeStage0TimeId, .route = Route::ENG},
    {.id = kEnvelopeStage1TimeId, .route = Route::ENG},
    {.id = kEnvelopeStage2TimeId, .route = Route::ENG},
    {.id = kEnvelopeStage3TimeId, .route = Route::ENG},
    {.id = kEnvelopeReleaseId, .route = Route::ENG},
    {.id = kEnvelopeGrowthDurationId, .route = Route::ENG},
    // --- Bloom (1300-1301) ---
    {.id = kBloomDepthId, .route = Route::MB},
    {.id = kBloomSpawnRateId, .route = Route::MB},
    // --- Ghost (1400-1403) ---
    {.id = kGhostPeakLevelId, .route = Route::MB},
    {.id = kGhostBlurId, .route = Route::MB},
    {.id = kGhostReverseProbabilityId, .route = Route::ENG},
    {.id = kGhostEventTriggersId, .route = Route::ENG},
    // --- Life (1500-1502) ---
    {.id = kLifeBreathingDepthId, .route = Route::MB},
    {.id = kLifeBreathingIrregularityId, .route = Route::MB},
    {.id = kLifeTidalDepthId, .route = Route::MB},
}};

/// Linear scan (110 entries); std::nullopt for any unregistered ID.
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
    {.id = kOutputSaturationId, .target = Krate::DSP::VoragoMacroTarget::OutputSaturation},
    {.id = kCloudRichnessId, .target = Krate::DSP::VoragoMacroTarget::CloudRichness},
    {.id = kCloudTiltId, .target = Krate::DSP::VoragoMacroTarget::CloudSpectralTiltDb},
    {.id = kCloudMutationId, .target = Krate::DSP::VoragoMacroTarget::CloudMutation},
    {.id = kCloudInharmonicityId, .target = Krate::DSP::VoragoMacroTarget::CloudInharmonicity},
    {.id = kCloudDriftDepthId, .target = Krate::DSP::VoragoMacroTarget::CloudDriftDepthCents},
    {.id = kNoiseLevelId, .target = Krate::DSP::VoragoMacroTarget::NoiseLevelDb},
    {.id = kNoiseWakeId, .target = Krate::DSP::VoragoMacroTarget::NoiseWakeBase},
    {.id = kNoiseWanderRateId, .target = Krate::DSP::VoragoMacroTarget::NoiseWanderRate},
    {.id = kResonanceGravityId, .target = Krate::DSP::VoragoMacroTarget::ResonanceGravity},
    {.id = kResonanceMixId, .target = Krate::DSP::VoragoMacroTarget::ResonanceMix},
    {.id = kResonanceWanderRateId, .target = Krate::DSP::VoragoMacroTarget::ResonanceWanderRate},
    {.id = kEcologyMixId, .target = Krate::DSP::VoragoMacroTarget::EcologyMix},
    {.id = kEcologyLoopGainId, .target = Krate::DSP::VoragoMacroTarget::EcologyLoopGain},
    {.id = kSubLevelOffsetId, .target = Krate::DSP::VoragoMacroTarget::SubToneLevelOffsetDb},
    {.id = kSubTrackingId, .target = Krate::DSP::VoragoMacroTarget::SubTrackingAmount},
    {.id = kSmearAmountId, .target = Krate::DSP::VoragoMacroTarget::SmearAmount},
    {.id = kSmearDecoherenceId, .target = Krate::DSP::VoragoMacroTarget::SmearDecoherence},
    {.id = kSmearTiltId, .target = Krate::DSP::VoragoMacroTarget::SmearTilt},
    {.id = kEventsRateScaleId, .target = Krate::DSP::VoragoMacroTarget::EventRateScale},
    {.id = kEcosystemDepthId, .target = Krate::DSP::VoragoMacroTarget::EcosystemDepth},
    {.id = kBodyBlendId, .target = Krate::DSP::VoragoMacroTarget::BodyBlend},
    {.id = kBodyDampingId, .target = Krate::DSP::VoragoMacroTarget::BodyDamping},
    {.id = kBodyResonanceId, .target = Krate::DSP::VoragoMacroTarget::BodyResonance},
    {.id = kBodyMixId, .target = Krate::DSP::VoragoMacroTarget::BodyMix},
    {.id = kSpaceSizeId, .target = Krate::DSP::VoragoMacroTarget::CavernSize},
    {.id = kSpaceDarknessId, .target = Krate::DSP::VoragoMacroTarget::CavernDarkness},
    {.id = kSpaceDecayId, .target = Krate::DSP::VoragoMacroTarget::CavernDecaySeconds},
    {.id = kSpaceFogId, .target = Krate::DSP::VoragoMacroTarget::CavernFog},
    {.id = kSpaceDamperDepthId, .target = Krate::DSP::VoragoMacroTarget::CavernDamperDepth},
    {.id = kSpaceMixId, .target = Krate::DSP::VoragoMacroTarget::CavernMix},
    {.id = kSpaceWidthId, .target = Krate::DSP::VoragoMacroTarget::CavernWidth},
    {.id = kBloomDepthId, .target = Krate::DSP::VoragoMacroTarget::BloomDepth},
    {.id = kBloomSpawnRateId, .target = Krate::DSP::VoragoMacroTarget::BloomSpawnRateHz},
    {.id = kGhostPeakLevelId, .target = Krate::DSP::VoragoMacroTarget::GhostPeakLevel},
    {.id = kGhostBlurId, .target = Krate::DSP::VoragoMacroTarget::AtmosBlur},
    {.id = kLifeBreathingDepthId, .target = Krate::DSP::VoragoMacroTarget::BreathingDepth},
    {.id = kLifeBreathingIrregularityId, .target = Krate::DSP::VoragoMacroTarget::BreathingIrregularity},
    {.id = kLifeTidalDepthId, .target = Krate::DSP::VoragoMacroTarget::TidalDepth},
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
    return std::ranges::any_of(kMacroOnlyTargets, [t](const auto m) { return m == t; });
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
    return std::ranges::all_of(kMbRoutes, [](const auto& e) {
        const auto r = routeOf(e.id);
        return r.has_value() && *r == Route::MB;
    });
}

}  // namespace RouteTableDetail

static_assert(RouteTableDetail::idsStrictlyAscending(), "kParamRoutes must be strictly ascending");
static_assert(RouteTableDetail::countRoute(Route::MB) == 39, "MB route count (plan 3.2)");
static_assert(RouteTableDetail::countRoute(Route::VP) == 33,
              "VP route count (plan 3.2 + Phase 14 FR-074: 901, 902)");
static_assert(RouteTableDetail::countRoute(Route::ENG) == 14, "ENG route count (plan 3.2)");
static_assert(RouteTableDetail::countRoute(Route::CV) == 9, "CV route count (plan 3.2)");
static_assert(RouteTableDetail::countRoute(Route::Macro) == 13, "MAC route count (plan 3.2)");
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
