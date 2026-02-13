#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

// Dropdown item counts for type-specific parameters
inline constexpr int kChaosModelCount = 4;      // Lorenz/Rossler/Chua/Henon
inline constexpr int kSpectralModeCount = 4;     // PerBinSaturate/MagnitudeOnly/BinSelective/SpectralBitcrush
inline constexpr int kSpectralCurveCount = 9;    // 9 waveshape types
inline constexpr int kFoldTypeCount = 3;         // Triangle/Sine/Lockhart
inline constexpr int kTapeModelCount = 2;        // Simple/Hysteresis

struct RuinaeDistortionParams {
    // Core params (legacy, order preserved for state compat)
    std::atomic<int> type{0};          // RuinaeDistortionType (0-5)
    std::atomic<float> drive{0.0f};    // 0-1
    std::atomic<float> character{0.5f}; // 0-1 (DEAD CODE - kept for state compat)
    std::atomic<float> mix{1.0f};      // 0-1

    // Chaos Waveshaper type-specific
    std::atomic<int> chaosModel{0};       // 0-3
    std::atomic<float> chaosSpeed{0.5f};  // 0-1 (maps to 0.01-100 in voice)
    std::atomic<float> chaosCoupling{0.0f}; // 0-1

    // Spectral Distortion type-specific
    std::atomic<int> spectralMode{0};     // 0-3
    std::atomic<int> spectralCurve{0};    // 0-8
    std::atomic<float> spectralBits{1.0f}; // 0-1 (maps to 1-16 in voice)

    // Granular Distortion type-specific
    std::atomic<float> grainSize{0.47f};     // 0-1 (maps to 5-100ms in voice)
    std::atomic<float> grainDensity{0.43f};  // 0-1 (maps to 1-8 in voice)
    std::atomic<float> grainVariation{0.0f}; // 0-1
    std::atomic<float> grainJitter{0.0f};    // 0-1 (maps to 0-50ms in voice)

    // Wavefolder type-specific
    std::atomic<int> foldType{0};         // 0-2

    // Tape Saturator type-specific
    std::atomic<int> tapeModel{0};        // 0-1
    std::atomic<float> tapeSaturation{0.5f}; // 0-1
    std::atomic<float> tapeBias{0.5f};    // 0-1 (maps to -1..+1 in voice)
};

inline void handleDistortionParamChange(
    RuinaeDistortionParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        // Core params
        case kDistortionTypeId:
            params.type.store(std::clamp(static_cast<int>(value * (kDistortionTypeCount - 1) + 0.5), 0, kDistortionTypeCount - 1), std::memory_order_relaxed);
            break;
        case kDistortionDriveId:
            params.drive.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kDistortionCharacterId:
            params.character.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kDistortionMixId:
            params.mix.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;

        // Chaos Waveshaper
        case kDistortionChaosModelId:
            params.chaosModel.store(std::clamp(static_cast<int>(value * (kChaosModelCount - 1) + 0.5), 0, kChaosModelCount - 1), std::memory_order_relaxed);
            break;
        case kDistortionChaosSpeedId:
            params.chaosSpeed.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kDistortionChaosCouplingId:
            params.chaosCoupling.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;

        // Spectral Distortion
        case kDistortionSpectralModeId:
            params.spectralMode.store(std::clamp(static_cast<int>(value * (kSpectralModeCount - 1) + 0.5), 0, kSpectralModeCount - 1), std::memory_order_relaxed);
            break;
        case kDistortionSpectralCurveId:
            params.spectralCurve.store(std::clamp(static_cast<int>(value * (kSpectralCurveCount - 1) + 0.5), 0, kSpectralCurveCount - 1), std::memory_order_relaxed);
            break;
        case kDistortionSpectralBitsId:
            params.spectralBits.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;

        // Granular Distortion
        case kDistortionGrainSizeId:
            params.grainSize.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kDistortionGrainDensityId:
            params.grainDensity.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kDistortionGrainVariationId:
            params.grainVariation.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kDistortionGrainJitterId:
            params.grainJitter.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;

        // Wavefolder
        case kDistortionFoldTypeId:
            params.foldType.store(std::clamp(static_cast<int>(value * (kFoldTypeCount - 1) + 0.5), 0, kFoldTypeCount - 1), std::memory_order_relaxed);
            break;

        // Tape Saturator
        case kDistortionTapeModelId:
            params.tapeModel.store(std::clamp(static_cast<int>(value * (kTapeModelCount - 1) + 0.5), 0, kTapeModelCount - 1), std::memory_order_relaxed);
            break;
        case kDistortionTapeSaturationId:
            params.tapeSaturation.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kDistortionTapeBiasId:
            params.tapeBias.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;

        default: break;
    }
}

inline void registerDistortionParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // Core params
    parameters.addParameter(createDropdownParameter(
        STR16("Distortion Type"), kDistortionTypeId,
        {STR16("Clean"), STR16("Chaos Waveshaper"), STR16("Spectral"),
         STR16("Granular"), STR16("Wavefolder"), STR16("Tape Saturator")}
    ));
    parameters.addParameter(STR16("Distortion Drive"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDistortionDriveId);
    parameters.addParameter(STR16("Distortion Character"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kDistortionCharacterId);
    parameters.addParameter(STR16("Distortion Mix"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kDistortionMixId);

    // Chaos Waveshaper type-specific
    parameters.addParameter(createDropdownParameter(
        STR16("Chaos Model"), kDistortionChaosModelId,
        {STR16("Lorenz"), STR16("Rossler"), STR16("Chua"), STR16("Henon")}
    ));
    parameters.addParameter(STR16("Chaos Speed"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kDistortionChaosSpeedId);
    parameters.addParameter(STR16("Chaos Coupling"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kDistortionChaosCouplingId);

    // Spectral Distortion type-specific
    parameters.addParameter(createDropdownParameter(
        STR16("Spectral Mode"), kDistortionSpectralModeId,
        {STR16("Per-Bin Saturate"), STR16("Magnitude Only"),
         STR16("Bin Selective"), STR16("Spectral Bitcrush")}
    ));
    parameters.addParameter(createDropdownParameter(
        STR16("Spectral Curve"), kDistortionSpectralCurveId,
        {STR16("Tanh"), STR16("Atan"), STR16("Cubic"), STR16("Hard Clip"),
         STR16("Sine Fold"), STR16("Tube"), STR16("Diode"), STR16("Fuzz"),
         STR16("Bit Reduce")}
    ));
    parameters.addParameter(STR16("Spectral Bits"), STR16("bits"), 0, 1.0,
        ParameterInfo::kCanAutomate, kDistortionSpectralBitsId);

    // Granular Distortion type-specific
    parameters.addParameter(STR16("Grain Size"), STR16("ms"), 0, 0.47,
        ParameterInfo::kCanAutomate, kDistortionGrainSizeId);
    parameters.addParameter(STR16("Grain Density"), STR16(""), 0, 0.43,
        ParameterInfo::kCanAutomate, kDistortionGrainDensityId);
    parameters.addParameter(STR16("Grain Variation"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDistortionGrainVariationId);
    parameters.addParameter(STR16("Grain Jitter"), STR16("ms"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDistortionGrainJitterId);

    // Wavefolder type-specific
    parameters.addParameter(createDropdownParameter(
        STR16("Fold Type"), kDistortionFoldTypeId,
        {STR16("Triangle"), STR16("Sine"), STR16("Lockhart")}
    ));

    // Tape Saturator type-specific
    parameters.addParameter(createDropdownParameter(
        STR16("Tape Model"), kDistortionTapeModelId,
        {STR16("Simple"), STR16("Hysteresis")}
    ));
    parameters.addParameter(STR16("Tape Saturation"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kDistortionTapeSaturationId);
    parameters.addParameter(STR16("Tape Bias"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kDistortionTapeBiasId);

    // UI-only: Distortion view mode tab (General/Type), ephemeral, not persisted
    auto* viewModeParam = new StringListParameter(
        STR16("Distortion View"), kDistortionViewModeTag);
    viewModeParam->appendString(STR16("General"));
    viewModeParam->appendString(STR16("Type"));
    parameters.addParameter(viewModeParam);
}

inline Steinberg::tresult formatDistortionParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kDistortionDriveId:
        case kDistortionMixId:
        case kDistortionGrainVariationId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kDistortionCharacterId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kDistortionChaosSpeedId: {
            // Map 0-1 to 0.01-100 (exponential)
            float speed = 0.01f * std::pow(10000.0f, static_cast<float>(value));
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f", speed);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kDistortionChaosCouplingId:
        case kDistortionTapeSaturationId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kDistortionSpectralBitsId: {
            // Map 0-1 to 1-16
            float bits = 1.0f + static_cast<float>(value) * 15.0f;
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f", bits);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kDistortionGrainSizeId: {
            // Map 0-1 to 5-100ms
            float ms = 5.0f + static_cast<float>(value) * 95.0f;
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kDistortionGrainDensityId: {
            // Map 0-1 to 1-8
            float density = 1.0f + static_cast<float>(value) * 7.0f;
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f", density);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kDistortionGrainJitterId: {
            // Map 0-1 to 0-50ms
            float ms = static_cast<float>(value) * 50.0f;
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kDistortionTapeBiasId: {
            // Map 0-1 to -1..+1
            float bias = static_cast<float>(value) * 2.0f - 1.0f;
            char8 text[32];
            snprintf(text, sizeof(text), "%+.2f", bias);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveDistortionParams(const RuinaeDistortionParams& params, Steinberg::IBStreamer& streamer) {
    // Legacy fields (order preserved for backward compat)
    streamer.writeInt32(params.type.load(std::memory_order_relaxed));
    streamer.writeFloat(params.drive.load(std::memory_order_relaxed));
    streamer.writeFloat(params.character.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));

    // Type-specific fields (appended for new presets)
    streamer.writeInt32(params.chaosModel.load(std::memory_order_relaxed));
    streamer.writeFloat(params.chaosSpeed.load(std::memory_order_relaxed));
    streamer.writeFloat(params.chaosCoupling.load(std::memory_order_relaxed));

    streamer.writeInt32(params.spectralMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.spectralCurve.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spectralBits.load(std::memory_order_relaxed));

    streamer.writeFloat(params.grainSize.load(std::memory_order_relaxed));
    streamer.writeFloat(params.grainDensity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.grainVariation.load(std::memory_order_relaxed));
    streamer.writeFloat(params.grainJitter.load(std::memory_order_relaxed));

    streamer.writeInt32(params.foldType.load(std::memory_order_relaxed));

    streamer.writeInt32(params.tapeModel.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tapeSaturation.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tapeBias.load(std::memory_order_relaxed));
}

inline bool loadDistortionParams(RuinaeDistortionParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;

    // Legacy fields (must succeed)
    if (!streamer.readInt32(intVal)) return false;
    params.type.store(intVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.drive.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.character.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.mix.store(floatVal, std::memory_order_relaxed);

    // Type-specific fields (optional - old presets won't have them)
    if (streamer.readInt32(intVal))
        params.chaosModel.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.chaosSpeed.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.chaosCoupling.store(floatVal, std::memory_order_relaxed);

    if (streamer.readInt32(intVal))
        params.spectralMode.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.spectralCurve.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.spectralBits.store(floatVal, std::memory_order_relaxed);

    if (streamer.readFloat(floatVal))
        params.grainSize.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.grainDensity.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.grainVariation.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.grainJitter.store(floatVal, std::memory_order_relaxed);

    if (streamer.readInt32(intVal))
        params.foldType.store(intVal, std::memory_order_relaxed);

    if (streamer.readInt32(intVal))
        params.tapeModel.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.tapeSaturation.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.tapeBias.store(floatVal, std::memory_order_relaxed);

    return true;
}

template<typename SetParamFunc>
inline void loadDistortionParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;

    // Legacy fields
    if (streamer.readInt32(intVal))
        setParam(kDistortionTypeId, static_cast<double>(intVal) / (kDistortionTypeCount - 1));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionDriveId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionCharacterId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionMixId, static_cast<double>(floatVal));

    // Type-specific fields (optional)
    if (streamer.readInt32(intVal))
        setParam(kDistortionChaosModelId, static_cast<double>(intVal) / (kChaosModelCount - 1));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionChaosSpeedId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionChaosCouplingId, static_cast<double>(floatVal));

    if (streamer.readInt32(intVal))
        setParam(kDistortionSpectralModeId, static_cast<double>(intVal) / (kSpectralModeCount - 1));
    if (streamer.readInt32(intVal))
        setParam(kDistortionSpectralCurveId, static_cast<double>(intVal) / (kSpectralCurveCount - 1));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionSpectralBitsId, static_cast<double>(floatVal));

    if (streamer.readFloat(floatVal))
        setParam(kDistortionGrainSizeId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionGrainDensityId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionGrainVariationId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionGrainJitterId, static_cast<double>(floatVal));

    if (streamer.readInt32(intVal))
        setParam(kDistortionFoldTypeId, static_cast<double>(intVal) / (kFoldTypeCount - 1));

    if (streamer.readInt32(intVal))
        setParam(kDistortionTapeModelId, static_cast<double>(intVal) / (kTapeModelCount - 1));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionTapeSaturationId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kDistortionTapeBiasId, static_cast<double>(floatVal));
}

} // namespace Ruinae
