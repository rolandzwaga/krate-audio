#pragma once

// ==============================================================================
// Vorago - Noise Parameters (ID 300-399)   T017, FR-011/FR-012/FR-013
// ==============================================================================
// Six-function pack contract (plan section 3.4; shape of global_params.h).
// 23 IDs: level / wake / wander rate, plus 4 slots x model / type / comb
// fundamental / comb spread / comb feedback.
//
// Stream (92 bytes, ascending ID order): 3F (level, wake, wander),
// 4I model, 4I type, 4F comb fundamental, 4F comb spread, 4F comb feedback.
//
// Continuous fields hold PLAIN units; discrete fields hold list indices.
// Plain-range clamps are the destination setter clamps (plan section 3.2).
// ==============================================================================

#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/noise_generator.h>
#include <krate/dsp/processors/resonator_bank.h>
#include <krate/dsp/processors/stochastic_filter.h>
#include <krate/dsp/systems/noise_organism.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>

namespace Vorago {

// ------------------------------------------------------------------------------
// Ranges and defaults (plan section 3.2, Noise rows)
// ------------------------------------------------------------------------------
inline constexpr int kNumNoiseSlots = 4;
inline constexpr int kNumNoiseModelChoices = 4;

// Double literals (not the float constants widened) so the registered defaults
// hit the table n0 values to 1e-9; each is pinned to its owner below.
inline constexpr double kNoiseLevelMinDb = -96.0;       // noise_generator.h:104
inline constexpr double kNoiseLevelMaxDb = 12.0;        // noise_generator.h:105
inline constexpr double kNoiseWanderMinHz = 0.01;       // StochasticFilter::kMinChangeRate
inline constexpr double kNoiseWanderMaxHz = 100.0;      // StochasticFilter::kMaxChangeRate
inline constexpr double kNoiseCombMinHz = 20.0;         // kMinResonatorFrequency
/// 0.45 (kMaxResonatorFrequencyRatio) x 44.1 kHz: the lowest-rate component ceiling.
inline constexpr double kNoiseCombMaxHz = 19845.0;
inline constexpr double kNoiseCombFeedbackMax = 0.9;    // NoiseOrganism::kCombFeedbackCap
inline constexpr double kNoiseCombFeedbackDefault = 0.55;          // kDefaultCombFeedback
inline constexpr double kNoiseCombFeedbackMetallicDefault = 0.75;  // kMetallicCombFeedback

static_assert(static_cast<float>(kNoiseLevelMinDb) == Krate::DSP::NoiseGenerator::kMinLevelDb &&
              static_cast<float>(kNoiseLevelMaxDb) == Krate::DSP::NoiseGenerator::kMaxLevelDb);
// Inexact decimals are pinned float-literal to float-constant (the resonance_params.h
// form): MSVC's constant evaluator does not round static_cast<float>(double) to
// float before comparing, so the cast form fails there for 0.01 / 0.9 / 0.55 / 0.75.
static_assert(Krate::DSP::StochasticFilter::kMinChangeRate == 0.01f &&
              static_cast<float>(kNoiseWanderMaxHz) == Krate::DSP::StochasticFilter::kMaxChangeRate);
static_assert(static_cast<float>(kNoiseCombMinHz) == Krate::DSP::kMinResonatorFrequency);
static_assert(Krate::DSP::NoiseOrganism::kCombFeedbackCap == 0.9f &&
              Krate::DSP::NoiseOrganism::kDefaultCombFeedback == 0.55f &&
              Krate::DSP::NoiseOrganism::kMetallicCombFeedback == 0.75f);
static_assert(Krate::DSP::NoiseOrganism::kMaxSources == static_cast<std::size_t>(kNumNoiseSlots));

// List index == NoiseOrganismModel value (noise_organism.h:126-131).
static_assert(static_cast<int>(Krate::DSP::NoiseOrganismModel::Direct) == 0 &&
              static_cast<int>(Krate::DSP::NoiseOrganismModel::FilteredWind) == 1 &&
              static_cast<int>(Krate::DSP::NoiseOrganismModel::GranularDust) == 2 &&
              static_cast<int>(Krate::DSP::NoiseOrganismModel::MetallicHiss) == 3);

struct NoiseParams {
    std::atomic<float> levelDb{-18.0f};      ///< dB [-96, 12]
    std::atomic<float> wake{0.35f};          ///< [0, 1]
    std::atomic<float> wanderRateHz{0.03f};  ///< Hz [0.01, 100], log
    /// NoiseOrganismModel index per slot: FilteredWind, GranularDust, Direct, MetallicHiss.
    std::array<std::atomic<int>, kNumNoiseSlots> model{{{1}, {2}, {0}, {3}}};
    /// kNoiseTypeByIndex index per slot (Brown == 5).
    std::array<std::atomic<int>, kNumNoiseSlots> type{
        {{kDefaultNoiseTypeIndex}, {kDefaultNoiseTypeIndex}, {kDefaultNoiseTypeIndex},
         {kDefaultNoiseTypeIndex}}};
    std::array<std::atomic<float>, kNumNoiseSlots> combFundamentalHz{
        {{60.0f}, {60.0f}, {60.0f}, {60.0f}}};  ///< Hz [20, 19845], log
    std::array<std::atomic<float>, kNumNoiseSlots> combSpread{
        {{0.35f}, {0.35f}, {0.35f}, {0.35f}}};  ///< [0, 1]
    std::array<std::atomic<float>, kNumNoiseSlots> combFeedback{
        {{Krate::DSP::NoiseOrganism::kDefaultCombFeedback},
         {Krate::DSP::NoiseOrganism::kDefaultCombFeedback},
         {Krate::DSP::NoiseOrganism::kDefaultCombFeedback},
         {Krate::DSP::NoiseOrganism::kMetallicCombFeedback}}};  ///< [0, 0.9]
};

namespace detail_noise {

inline constexpr std::array<int, kNumNoiseSlots> kDefaultModel{1, 2, 0, 3};

[[nodiscard]] inline std::size_t slotOf(Steinberg::Vst::ParamID id,
                                        Steinberg::Vst::ParamID base) noexcept {
    return static_cast<std::size_t>(id - base);
}

[[nodiscard]] inline float clampFinite(float v, double mn, double mx) noexcept {
    return std::clamp(v, static_cast<float>(mn), static_cast<float>(mx));
}

[[nodiscard]] inline int clampIndex(Steinberg::int32 i, int count) noexcept {
    return static_cast<int>(std::clamp(i, Steinberg::int32{0}, static_cast<Steinberg::int32>(count - 1)));
}

}  // namespace detail_noise

// ==============================================================================
// Parameter Change Handler
// ==============================================================================
// The caller has already rejected non-finite values and clamped to [0, 1].

inline void handleNoiseParamChange(NoiseParams& params, Steinberg::Vst::ParamID id,
                                   Steinberg::Vst::ParamValue value) noexcept {
    using detail_noise::slotOf;
    constexpr auto kRelaxed = std::memory_order_relaxed;
    switch (id) {
        case kNoiseLevelId:
            params.levelDb.store(
                static_cast<float>(linearFromNormalized(value, kNoiseLevelMinDb, kNoiseLevelMaxDb)),
                kRelaxed);
            break;
        case kNoiseWakeId:
            params.wake.store(static_cast<float>(linearFromNormalized(value, 0.0, 1.0)), kRelaxed);
            break;
        case kNoiseWanderRateId:
            params.wanderRateHz.store(static_cast<float>(Krate::Plugins::logMapFromNormalized(
                                          value, kNoiseWanderMinHz, kNoiseWanderMaxHz)),
                                      kRelaxed);
            break;
        case kNoiseSlot0ModelId:
        case kNoiseSlot1ModelId:
        case kNoiseSlot2ModelId:
        case kNoiseSlot3ModelId:
            params.model[slotOf(id, kNoiseSlot0ModelId)].store(
                indexFromNormalized(value, kNumNoiseModelChoices), kRelaxed);
            break;
        case kNoiseSlot0TypeId:
        case kNoiseSlot1TypeId:
        case kNoiseSlot2TypeId:
        case kNoiseSlot3TypeId:
            params.type[slotOf(id, kNoiseSlot0TypeId)].store(
                indexFromNormalized(value, kNumNoiseTypeChoices), kRelaxed);
            break;
        case kNoiseSlot0CombFundamentalId:
        case kNoiseSlot1CombFundamentalId:
        case kNoiseSlot2CombFundamentalId:
        case kNoiseSlot3CombFundamentalId:
            params.combFundamentalHz[slotOf(id, kNoiseSlot0CombFundamentalId)].store(
                static_cast<float>(
                    Krate::Plugins::logMapFromNormalized(value, kNoiseCombMinHz, kNoiseCombMaxHz)),
                kRelaxed);
            break;
        case kNoiseSlot0CombSpreadId:
        case kNoiseSlot1CombSpreadId:
        case kNoiseSlot2CombSpreadId:
        case kNoiseSlot3CombSpreadId:
            params.combSpread[slotOf(id, kNoiseSlot0CombSpreadId)].store(
                static_cast<float>(linearFromNormalized(value, 0.0, 1.0)), kRelaxed);
            break;
        case kNoiseSlot0CombFeedbackId:
        case kNoiseSlot1CombFeedbackId:
        case kNoiseSlot2CombFeedbackId:
        case kNoiseSlot3CombFeedbackId:
            params.combFeedback[slotOf(id, kNoiseSlot0CombFeedbackId)].store(
                static_cast<float>(linearFromNormalized(value, 0.0, kNoiseCombFeedbackMax)),
                kRelaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerNoiseParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    constexpr auto kAuto = ParameterInfo::kCanAutomate;

    parameters.addParameter(STR16("Noise Level"), STR16("dB"), 0,
                            linearToNormalized(-18.0, kNoiseLevelMinDb, kNoiseLevelMaxDb), kAuto,
                            kNoiseLevelId);
    parameters.addParameter(STR16("Noise Wake"), STR16("%"), 0, linearToNormalized(0.35, 0.0, 1.0),
                            kAuto, kNoiseWakeId);
    parameters.addParameter(
        STR16("Noise Wander Rate"), STR16("Hz"), 0,
        Krate::Plugins::logMapToNormalized(0.03, kNoiseWanderMinHz, kNoiseWanderMaxHz), kAuto,
        kNoiseWanderRateId);

    const std::array<const TChar*, kNumNoiseSlots> kModelTitles{
        STR16("Noise Slot 1 Model"), STR16("Noise Slot 2 Model"), STR16("Noise Slot 3 Model"),
        STR16("Noise Slot 4 Model")};
    const std::array<const TChar*, kNumNoiseSlots> kTypeTitles{
        STR16("Noise Slot 1 Type"), STR16("Noise Slot 2 Type"), STR16("Noise Slot 3 Type"),
        STR16("Noise Slot 4 Type")};
    const std::array<const TChar*, kNumNoiseSlots> kFundTitles{
        STR16("Noise Slot 1 Comb Freq"), STR16("Noise Slot 2 Comb Freq"),
        STR16("Noise Slot 3 Comb Freq"), STR16("Noise Slot 4 Comb Freq")};
    const std::array<const TChar*, kNumNoiseSlots> kSpreadTitles{
        STR16("Noise Slot 1 Comb Spread"), STR16("Noise Slot 2 Comb Spread"),
        STR16("Noise Slot 3 Comb Spread"), STR16("Noise Slot 4 Comb Spread")};
    const std::array<const TChar*, kNumNoiseSlots> kFeedbackTitles{
        STR16("Noise Slot 1 Comb Feedback"), STR16("Noise Slot 2 Comb Feedback"),
        STR16("Noise Slot 3 Comb Feedback"), STR16("Noise Slot 4 Comb Feedback")};

    // Index order == NoiseOrganismModel (noise_organism.h:126-131).
    const std::array<const TChar*, kNumNoiseModelChoices> kModelNames{
        STR16("Direct"), STR16("Filtered Wind"), STR16("Granular Dust"),
        STR16("Metallic Hiss")};
    // Index order == kNoiseTypeByIndex (param_mapping.h).
    const std::array<const TChar*, kNumNoiseTypeChoices> kTypeNames{
        STR16("White"), STR16("Pink"),  STR16("Tape Hiss"), STR16("Vinyl Crackle"),
        STR16("Asperity"), STR16("Brown"), STR16("Blue"), STR16("Violet"),
        STR16("Grey"), STR16("Velvet"), STR16("Vinyl Rumble"), STR16("Radio Static")};

    for (int s = 0; s < kNumNoiseSlots; ++s) {
        const auto us = static_cast<std::size_t>(s);
        const auto off = static_cast<ParamID>(s);
        const int defModel = detail_noise::kDefaultModel[us];
        auto* model = Krate::Plugins::createDropdownParameterWithDefault(
            kModelTitles[us], kNoiseSlot0ModelId + off, defModel, kModelNames.data(),
            kNumNoiseModelChoices);
        // P-1 (global_params.h:82-84): the helper sets only the CURRENT value.
        model->getInfo().defaultNormalizedValue =
            indexToNormalized(defModel, kNumNoiseModelChoices);
        parameters.addParameter(model);
    }
    for (int s = 0; s < kNumNoiseSlots; ++s) {
        const auto us = static_cast<std::size_t>(s);
        auto* type = Krate::Plugins::createDropdownParameterWithDefault(
            kTypeTitles[us], kNoiseSlot0TypeId + static_cast<ParamID>(s), kDefaultNoiseTypeIndex,
            kTypeNames.data(), kNumNoiseTypeChoices);
        type->getInfo().defaultNormalizedValue =
            indexToNormalized(kDefaultNoiseTypeIndex, kNumNoiseTypeChoices);
        parameters.addParameter(type);
    }
    for (int s = 0; s < kNumNoiseSlots; ++s) {
        parameters.addParameter(
            kFundTitles[static_cast<std::size_t>(s)], STR16("Hz"), 0,
            Krate::Plugins::logMapToNormalized(60.0, kNoiseCombMinHz, kNoiseCombMaxHz), kAuto,
            kNoiseSlot0CombFundamentalId + static_cast<ParamID>(s));
    }
    for (int s = 0; s < kNumNoiseSlots; ++s) {
        parameters.addParameter(kSpreadTitles[static_cast<std::size_t>(s)], STR16("%"), 0,
                                linearToNormalized(0.35, 0.0, 1.0), kAuto,
                                kNoiseSlot0CombSpreadId + static_cast<ParamID>(s));
    }
    for (int s = 0; s < kNumNoiseSlots; ++s) {
        const double fb = (s == kNumNoiseSlots - 1) ? kNoiseCombFeedbackMetallicDefault
                                                    : kNoiseCombFeedbackDefault;
        parameters.addParameter(kFeedbackTitles[static_cast<std::size_t>(s)], STR16("%"), 0,
                                linearToNormalized(fb, 0.0, kNoiseCombFeedbackMax), kAuto,
                                kNoiseSlot0CombFeedbackId + static_cast<ParamID>(s));
    }
}

// ==============================================================================
// Display Formatting (continuous IDs only; the lists format themselves)
// ==============================================================================

inline Steinberg::tresult formatNoiseParam(Steinberg::Vst::ParamID id,
                                           Steinberg::Vst::ParamValue value,
                                           Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];

    if (id == kNoiseLevelId) {
        snprintf(text, sizeof(text), "%.1f dB",
                 linearFromNormalized(value, kNoiseLevelMinDb, kNoiseLevelMaxDb));
    } else if (id == kNoiseWakeId ||
               (id >= kNoiseSlot0CombSpreadId && id <= kNoiseSlot3CombSpreadId)) {
        snprintf(text, sizeof(text), "%.0f%%", linearFromNormalized(value, 0.0, 1.0) * 100.0);
    } else if (id == kNoiseWanderRateId) {
        snprintf(text, sizeof(text), "%.3f Hz",
                 Krate::Plugins::logMapFromNormalized(value, kNoiseWanderMinHz, kNoiseWanderMaxHz));
    } else if (id >= kNoiseSlot0CombFundamentalId && id <= kNoiseSlot3CombFundamentalId) {
        snprintf(text, sizeof(text), "%.1f Hz",
                 Krate::Plugins::logMapFromNormalized(value, kNoiseCombMinHz, kNoiseCombMaxHz));
    } else if (id >= kNoiseSlot0CombFeedbackId && id <= kNoiseSlot3CombFeedbackId) {
        snprintf(text, sizeof(text), "%.0f%%",
                 linearFromNormalized(value, 0.0, kNoiseCombFeedbackMax) * 100.0);
    } else {
        return kResultFalse;
    }
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 92 bytes, ascending ID order
// ==============================================================================

inline void saveNoiseParams(const NoiseParams& params, Steinberg::IBStreamer& streamer) {
    constexpr auto kRelaxed = std::memory_order_relaxed;
    streamer.writeFloat(params.levelDb.load(kRelaxed));
    streamer.writeFloat(params.wake.load(kRelaxed));
    streamer.writeFloat(params.wanderRateHz.load(kRelaxed));
    for (const auto& m : params.model)
        streamer.writeInt32(static_cast<Steinberg::int32>(m.load(kRelaxed)));
    for (const auto& t : params.type)
        streamer.writeInt32(static_cast<Steinberg::int32>(t.load(kRelaxed)));
    for (const auto& f : params.combFundamentalHz) streamer.writeFloat(f.load(kRelaxed));
    for (const auto& s : params.combSpread) streamer.writeFloat(s.load(kRelaxed));
    for (const auto& f : params.combFeedback) streamer.writeFloat(f.load(kRelaxed));
}

/// EOF-safe: a failed read returns false and leaves every later field at its
/// CURRENT value. Non-finite floats leave their field unchanged; finite floats
/// clamp to the plain range; indices clamp to [0, count - 1].
inline bool loadNoiseParams(NoiseParams& params, Steinberg::IBStreamer& streamer) {
    constexpr auto kRelaxed = std::memory_order_relaxed;
    using detail_noise::clampFinite;
    using detail_noise::clampIndex;

    const auto readF = [&](std::atomic<float>& dst, double mn, double mx) {
        float v = 0.0f;
        if (!streamer.readFloat(v))
            return false;
        if (Krate::DSP::detail::isFinite(v))  // fast-math-immune, db_utils.h:118
            dst.store(clampFinite(v, mn, mx), kRelaxed);
        return true;
    };
    const auto readI = [&](std::atomic<int>& dst, int count) {
        Steinberg::int32 i = 0;
        if (!streamer.readInt32(i))
            return false;
        dst.store(clampIndex(i, count), kRelaxed);
        return true;
    };

    if (!readF(params.levelDb, kNoiseLevelMinDb, kNoiseLevelMaxDb)) return false;
    if (!readF(params.wake, 0.0, 1.0)) return false;
    if (!readF(params.wanderRateHz, kNoiseWanderMinHz, kNoiseWanderMaxHz)) return false;
    for (auto& m : params.model)
        if (!readI(m, kNumNoiseModelChoices)) return false;
    for (auto& t : params.type)
        if (!readI(t, kNumNoiseTypeChoices)) return false;
    for (auto& f : params.combFundamentalHz)
        if (!readF(f, kNoiseCombMinHz, kNoiseCombMaxHz)) return false;
    for (auto& s : params.combSpread)
        if (!readF(s, 0.0, 1.0)) return false;
    for (auto& f : params.combFeedback)
        if (!readF(f, 0.0, kNoiseCombFeedbackMax)) return false;
    return true;
}

// ==============================================================================
// Controller State Sync (same read order; inverse maps)
// ==============================================================================

template <typename SetParamFunc>
inline void loadNoiseParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    using Steinberg::Vst::ParamID;
    using detail_noise::clampFinite;
    using detail_noise::clampIndex;

    // Returns false on a failed read; a non-finite value is skipped (no setParam).
    const auto readF = [&](ParamID id, double mn, double mx, bool isLog) {
        float v = 0.0f;
        if (!streamer.readFloat(v))
            return false;
        if (Krate::DSP::detail::isFinite(v)) {
            const double u = static_cast<double>(clampFinite(v, mn, mx));
            setParam(id, isLog ? Krate::Plugins::logMapToNormalized(u, mn, mx)
                               : linearToNormalized(u, mn, mx));
        }
        return true;
    };
    const auto readI = [&](ParamID id, int count) {
        Steinberg::int32 i = 0;
        if (!streamer.readInt32(i))
            return false;
        setParam(id, indexToNormalized(clampIndex(i, count), count));
        return true;
    };

    if (!readF(kNoiseLevelId, kNoiseLevelMinDb, kNoiseLevelMaxDb, false)) return;
    if (!readF(kNoiseWakeId, 0.0, 1.0, false)) return;
    if (!readF(kNoiseWanderRateId, kNoiseWanderMinHz, kNoiseWanderMaxHz, true)) return;
    for (int s = 0; s < kNumNoiseSlots; ++s)
        if (!readI(kNoiseSlot0ModelId + static_cast<ParamID>(s), kNumNoiseModelChoices)) return;
    for (int s = 0; s < kNumNoiseSlots; ++s)
        if (!readI(kNoiseSlot0TypeId + static_cast<ParamID>(s), kNumNoiseTypeChoices)) return;
    for (int s = 0; s < kNumNoiseSlots; ++s)
        if (!readF(kNoiseSlot0CombFundamentalId + static_cast<ParamID>(s), kNoiseCombMinHz,
                   kNoiseCombMaxHz, true))
            return;
    for (int s = 0; s < kNumNoiseSlots; ++s)
        if (!readF(kNoiseSlot0CombSpreadId + static_cast<ParamID>(s), 0.0, 1.0, false)) return;
    for (int s = 0; s < kNumNoiseSlots; ++s)
        if (!readF(kNoiseSlot0CombFeedbackId + static_cast<ParamID>(s), 0.0,
                   kNoiseCombFeedbackMax, false))
            return;
}

}  // namespace Vorago
