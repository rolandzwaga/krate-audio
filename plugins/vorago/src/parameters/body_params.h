#pragma once

// ==============================================================================
// Vorago Phase 12 - Body parameter pack (ID 1000-1099)   T024, FR-011/FR-012/FR-013
// ==============================================================================
// Six-function pack contract (shape of global_params.h). Plan section 3.2 rows
// 1000-1005: four linear [0, 1] amounts (clamp sources continuous_body.h:142-159
// via vorago_voice.h:1235-1282) and two 11-entry material lists whose index IS the
// ContinuousBody::BodyMaterial enum value (Q5 ruled (a); continuous_body.h:84-97),
// so there is no mapping table.
//
// Stream: float blend, damping, resonance, mix + int32 materialA, materialB
//         = 24 bytes, ascending ID order.
//
// ODR: Vorago::BodyParams is a near-name of Seraphis::BodyParams
// (plugins/seraphis/src/parameters/body_params.h:108) - different namespaces; no TU
// may `using namespace` both.
// ==============================================================================

#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/continuous_body.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Vorago {

inline constexpr double kBodyBlendDefault = 0.35;
inline constexpr double kBodyDampingDefault = 0.25;
inline constexpr double kBodyResonanceDefault = 0.70;
inline constexpr double kBodyMixDefault = 1.00;

inline constexpr int kNumBodyMaterialChoices = 11;
inline constexpr int kBodyMaterialADefault = 5;  // StoneChamber
inline constexpr int kBodyMaterialBDefault = 6;  // SteelTank

static_assert(Krate::DSP::ContinuousBody::kNumMaterials == 11,
              "Body material list must cover every ContinuousBody::BodyMaterial");
static_assert(static_cast<int>(Krate::DSP::ContinuousBody::BodyMaterial::StoneChamber) == 5,
              "Material A default index must be StoneChamber");
static_assert(static_cast<int>(Krate::DSP::ContinuousBody::BodyMaterial::SteelTank) == 6,
              "Material B default index must be SteelTank");

struct BodyParams {
    std::atomic<float> blend{static_cast<float>(kBodyBlendDefault)};          ///< ID 1000, [0, 1]
    std::atomic<float> damping{static_cast<float>(kBodyDampingDefault)};      ///< ID 1001, [0, 1]
    std::atomic<float> resonance{static_cast<float>(kBodyResonanceDefault)};  ///< ID 1002, [0, 1]
    std::atomic<float> mix{static_cast<float>(kBodyMixDefault)};              ///< ID 1003, [0, 1]
    std::atomic<int> materialA{kBodyMaterialADefault};    ///< ID 1004, BodyMaterial index
    std::atomic<int> materialB{kBodyMaterialBDefault};    ///< ID 1005, BodyMaterial index
};

// ==============================================================================
// Parameter Change Handler (caller has rejected non-finite and clamped to [0, 1])
// ==============================================================================

inline void handleBodyParamChange(BodyParams& params, Steinberg::Vst::ParamID id,
                                  Steinberg::Vst::ParamValue value) noexcept {
    const auto lin01 = [value]() noexcept {
        return static_cast<float>(linearFromNormalized(value, 0.0, 1.0));
    };
    switch (id) {
        case kBodyBlendId:
            params.blend.store(lin01(), std::memory_order_relaxed);
            break;
        case kBodyDampingId:
            params.damping.store(lin01(), std::memory_order_relaxed);
            break;
        case kBodyResonanceId:
            params.resonance.store(lin01(), std::memory_order_relaxed);
            break;
        case kBodyMixId:
            params.mix.store(lin01(), std::memory_order_relaxed);
            break;
        case kBodyMaterialAId:
            params.materialA.store(indexFromNormalized(value, kNumBodyMaterialChoices),
                                   std::memory_order_relaxed);
            break;
        case kBodyMaterialBId:
            params.materialB.store(indexFromNormalized(value, kNumBodyMaterialChoices),
                                   std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerBodyParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Body Blend"), STR16("%"), 0,
                            kBodyBlendDefault, ParameterInfo::kCanAutomate,
                            kBodyBlendId);
    parameters.addParameter(STR16("Body Damping"), STR16("%"), 0,
                            kBodyDampingDefault, ParameterInfo::kCanAutomate,
                            kBodyDampingId);
    parameters.addParameter(STR16("Body Resonance"), STR16("%"), 0,
                            kBodyResonanceDefault,
                            ParameterInfo::kCanAutomate, kBodyResonanceId);
    parameters.addParameter(STR16("Body Mix"), STR16("%"), 0,
                            kBodyMixDefault, ParameterInfo::kCanAutomate,
                            kBodyMixId);

    // BodyMaterial declaration order (continuous_body.h:84-97); index == enum value.
    const auto addMaterialList = [&parameters](const TChar* title, ParamID id,
                                               int defaultIndex) {
        auto* list = Krate::Plugins::createDropdownParameterWithDefault(
            title, id, defaultIndex,
            {STR16("Glass"), STR16("Strings"), STR16("Metal Plate"), STR16("Chamber"),
             STR16("Ice"), STR16("Stone Chamber"), STR16("Steel Tank"), STR16("Wooden Hull"),
             STR16("Cathedral Column"), STR16("Cavern Wall"), STR16("Glass Sphere")});
        // P-1 (global_params.h:82-84): pin the REGISTERED default too.
        list->getInfo().defaultNormalizedValue =
            indexToNormalized(defaultIndex, kNumBodyMaterialChoices);
        parameters.addParameter(list);
    };
    addMaterialList(STR16("Body Material A"), kBodyMaterialAId, kBodyMaterialADefault);
    addMaterialList(STR16("Body Material B"), kBodyMaterialBId, kBodyMaterialBDefault);
}

// ==============================================================================
// Display Formatting (continuous IDs only; the lists format themselves)
// ==============================================================================

inline Steinberg::tresult formatBodyParam(Steinberg::Vst::ParamID id,
                                          Steinberg::Vst::ParamValue value,
                                          Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    switch (id) {
        case kBodyBlendId:
        case kBodyDampingId:
        case kBodyResonanceId:
        case kBodyMixId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", linearFromNormalized(value, 0.0, 1.0) * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default:
            return kResultFalse;
    }
}

// ==============================================================================
// State Persistence - 24 bytes (4 float + 2 int32), ascending ID order
// ==============================================================================

inline void saveBodyParams(const BodyParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.blend.load(std::memory_order_relaxed));
    streamer.writeFloat(params.damping.load(std::memory_order_relaxed));
    streamer.writeFloat(params.resonance.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.materialA.load(std::memory_order_relaxed)));
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.materialB.load(std::memory_order_relaxed)));
}

/// EOF-safe: the first failed read returns false and leaves every later field at
/// its CURRENT value. Non-finite floats are skipped (field unchanged); finite
/// floats clamp to [0, 1]; indices clamp to [0, 10].
inline bool loadBodyParams(BodyParams& params, Steinberg::IBStreamer& streamer) {
    const auto readAmount = [&streamer](std::atomic<float>& field) {
        float v = 0.0f;
        if (!streamer.readFloat(v))
            return false;
        if (Krate::DSP::detail::isFinite(v))  // fast-math-immune, db_utils.h:118
            field.store(std::clamp(v, 0.0f, 1.0f), std::memory_order_relaxed);
        return true;
    };
    const auto readMaterial = [&streamer](std::atomic<int>& field) {
        Steinberg::int32 i = 0;
        if (!streamer.readInt32(i))
            return false;
        field.store(std::clamp(static_cast<int>(i), 0, kNumBodyMaterialChoices - 1),
                    std::memory_order_relaxed);
        return true;
    };

    return readAmount(params.blend) && readAmount(params.damping) &&
           readAmount(params.resonance) && readAmount(params.mix) &&
           readMaterial(params.materialA) && readMaterial(params.materialB);
}

// ==============================================================================
// Controller State Sync (same read order and finite/clamp rules; inverse maps)
// ==============================================================================

template <typename SetParamFunc>
inline void loadBodyParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    const auto readAmount = [&streamer, &setParam](Steinberg::Vst::ParamID id) {
        float v = 0.0f;
        if (!streamer.readFloat(v))
            return false;
        if (Krate::DSP::detail::isFinite(v))
            setParam(id, linearToNormalized(static_cast<double>(v), 0.0, 1.0));
        return true;
    };
    const auto readMaterial = [&streamer, &setParam](Steinberg::Vst::ParamID id) {
        Steinberg::int32 i = 0;
        if (!streamer.readInt32(i))
            return false;
        setParam(id, indexToNormalized(static_cast<int>(i), kNumBodyMaterialChoices));
        return true;
    };

    [[maybe_unused]] const bool complete =
        readAmount(kBodyBlendId) && readAmount(kBodyDampingId) &&
        readAmount(kBodyResonanceId) && readAmount(kBodyMixId) &&
        readMaterial(kBodyMaterialAId) && readMaterial(kBodyMaterialBId);
}

}  // namespace Vorago
