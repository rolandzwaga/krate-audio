#pragma once
#include "plugin_ids.h"
#include "parameters/amp_env_params.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

struct ModEnvParams {
    std::atomic<float> attackMs{10.0f};
    std::atomic<float> decayMs{300.0f};
    std::atomic<float> sustain{0.5f};
    std::atomic<float> releaseMs{500.0f};

    // Curve amounts [-1, +1] (0=linear, -1=logarithmic, +1=exponential)
    std::atomic<float> attackCurve{0.0f};
    std::atomic<float> decayCurve{0.0f};
    std::atomic<float> releaseCurve{0.0f};

    // Bezier mode flag (0=Simple, 1=Bezier)
    std::atomic<float> bezierEnabled{0.0f};

    // Bezier control points [0, 1] (3 segments x 2 handles x 2 axes = 12)
    std::atomic<float> bezierAttackCp1X{0.33f};
    std::atomic<float> bezierAttackCp1Y{0.33f};
    std::atomic<float> bezierAttackCp2X{0.67f};
    std::atomic<float> bezierAttackCp2Y{0.67f};
    std::atomic<float> bezierDecayCp1X{0.33f};
    std::atomic<float> bezierDecayCp1Y{0.67f};
    std::atomic<float> bezierDecayCp2X{0.67f};
    std::atomic<float> bezierDecayCp2Y{0.33f};
    std::atomic<float> bezierReleaseCp1X{0.33f};
    std::atomic<float> bezierReleaseCp1Y{0.67f};
    std::atomic<float> bezierReleaseCp2X{0.67f};
    std::atomic<float> bezierReleaseCp2Y{0.33f};
};

inline void handleModEnvParamChange(
    ModEnvParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kModEnvAttackId:
            params.attackMs.store(envTimeFromNormalized(value), std::memory_order_relaxed); break;
        case kModEnvDecayId:
            params.decayMs.store(envTimeFromNormalized(value), std::memory_order_relaxed); break;
        case kModEnvSustainId:
            params.sustain.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        case kModEnvReleaseId:
            params.releaseMs.store(envTimeFromNormalized(value), std::memory_order_relaxed); break;
        case kModEnvAttackCurveId:
            params.attackCurve.store(envCurveFromNormalized(value), std::memory_order_relaxed); break;
        case kModEnvDecayCurveId:
            params.decayCurve.store(envCurveFromNormalized(value), std::memory_order_relaxed); break;
        case kModEnvReleaseCurveId:
            params.releaseCurve.store(envCurveFromNormalized(value), std::memory_order_relaxed); break;
        case kModEnvBezierEnabledId:
            params.bezierEnabled.store(value >= 0.5 ? 1.0f : 0.0f, std::memory_order_relaxed); break;
        case kModEnvBezierAttackCp1XId:
            params.bezierAttackCp1X.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kModEnvBezierAttackCp1YId:
            params.bezierAttackCp1Y.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kModEnvBezierAttackCp2XId:
            params.bezierAttackCp2X.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kModEnvBezierAttackCp2YId:
            params.bezierAttackCp2Y.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kModEnvBezierDecayCp1XId:
            params.bezierDecayCp1X.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kModEnvBezierDecayCp1YId:
            params.bezierDecayCp1Y.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kModEnvBezierDecayCp2XId:
            params.bezierDecayCp2X.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kModEnvBezierDecayCp2YId:
            params.bezierDecayCp2Y.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kModEnvBezierReleaseCp1XId:
            params.bezierReleaseCp1X.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kModEnvBezierReleaseCp1YId:
            params.bezierReleaseCp1Y.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kModEnvBezierReleaseCp2XId:
            params.bezierReleaseCp2X.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kModEnvBezierReleaseCp2YId:
            params.bezierReleaseCp2Y.store(static_cast<float>(value), std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerModEnvParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Mod Env Attack"), STR16("ms"), 0, 0.1,
        ParameterInfo::kCanAutomate, kModEnvAttackId);
    parameters.addParameter(STR16("Mod Env Decay"), STR16("ms"), 0, 0.310,
        ParameterInfo::kCanAutomate, kModEnvDecayId);
    parameters.addParameter(STR16("Mod Env Sustain"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kModEnvSustainId);
    parameters.addParameter(STR16("Mod Env Release"), STR16("ms"), 0, 0.368,
        ParameterInfo::kCanAutomate, kModEnvReleaseId);

    // Curve amounts: default 0.0 -> normalized 0.5
    parameters.addParameter(STR16("Mod Env Attack Curve"), nullptr, 0, 0.5,
        ParameterInfo::kCanAutomate, kModEnvAttackCurveId);
    parameters.addParameter(STR16("Mod Env Decay Curve"), nullptr, 0, 0.5,
        ParameterInfo::kCanAutomate, kModEnvDecayCurveId);
    parameters.addParameter(STR16("Mod Env Release Curve"), nullptr, 0, 0.5,
        ParameterInfo::kCanAutomate, kModEnvReleaseCurveId);

    // Bezier mode flag: default off
    parameters.addParameter(STR16("Mod Env Bezier"), nullptr, 1, 0.0,
        ParameterInfo::kCanAutomate, kModEnvBezierEnabledId);

    // Bezier control points
    parameters.addParameter(STR16("Mod Atk Bez CP1 X"), nullptr, 0, 0.33,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kModEnvBezierAttackCp1XId);
    parameters.addParameter(STR16("Mod Atk Bez CP1 Y"), nullptr, 0, 0.33,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kModEnvBezierAttackCp1YId);
    parameters.addParameter(STR16("Mod Atk Bez CP2 X"), nullptr, 0, 0.67,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kModEnvBezierAttackCp2XId);
    parameters.addParameter(STR16("Mod Atk Bez CP2 Y"), nullptr, 0, 0.67,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kModEnvBezierAttackCp2YId);
    parameters.addParameter(STR16("Mod Dec Bez CP1 X"), nullptr, 0, 0.33,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kModEnvBezierDecayCp1XId);
    parameters.addParameter(STR16("Mod Dec Bez CP1 Y"), nullptr, 0, 0.67,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kModEnvBezierDecayCp1YId);
    parameters.addParameter(STR16("Mod Dec Bez CP2 X"), nullptr, 0, 0.67,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kModEnvBezierDecayCp2XId);
    parameters.addParameter(STR16("Mod Dec Bez CP2 Y"), nullptr, 0, 0.33,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kModEnvBezierDecayCp2YId);
    parameters.addParameter(STR16("Mod Rel Bez CP1 X"), nullptr, 0, 0.33,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kModEnvBezierReleaseCp1XId);
    parameters.addParameter(STR16("Mod Rel Bez CP1 Y"), nullptr, 0, 0.67,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kModEnvBezierReleaseCp1YId);
    parameters.addParameter(STR16("Mod Rel Bez CP2 X"), nullptr, 0, 0.67,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kModEnvBezierReleaseCp2XId);
    parameters.addParameter(STR16("Mod Rel Bez CP2 Y"), nullptr, 0, 0.33,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kModEnvBezierReleaseCp2YId);
}

inline Steinberg::tresult formatModEnvParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kModEnvAttackId: case kModEnvDecayId: case kModEnvReleaseId: {
            float ms = envTimeFromNormalized(value);
            char8 text[32];
            if (ms >= 1000.0f) snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
            else snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kModEnvSustainId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kModEnvAttackCurveId: case kModEnvDecayCurveId: case kModEnvReleaseCurveId: {
            float curve = envCurveFromNormalized(value);
            char8 text[32];
            snprintf(text, sizeof(text), "%+.2f", curve);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveModEnvParams(const ModEnvParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.attackMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decayMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.sustain.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.attackCurve.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decayCurve.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseCurve.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierEnabled.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierAttackCp1X.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierAttackCp1Y.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierAttackCp2X.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierAttackCp2Y.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierDecayCp1X.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierDecayCp1Y.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierDecayCp2X.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierDecayCp2Y.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierReleaseCp1X.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierReleaseCp1Y.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierReleaseCp2X.load(std::memory_order_relaxed));
    streamer.writeFloat(params.bezierReleaseCp2Y.load(std::memory_order_relaxed));
}

inline bool loadModEnvParams(ModEnvParams& params, Steinberg::IBStreamer& streamer) {
    float v = 0.0f;
    if (!streamer.readFloat(v)) return false; params.attackMs.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) return false; params.decayMs.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) return false; params.sustain.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) return false; params.releaseMs.store(v, std::memory_order_relaxed);
    // Optional curve/Bezier fields (backward compatibility)
    if (streamer.readFloat(v)) { params.attackCurve.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.decayCurve.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.releaseCurve.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierEnabled.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierAttackCp1X.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierAttackCp1Y.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierAttackCp2X.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierAttackCp2Y.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierDecayCp1X.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierDecayCp1Y.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierDecayCp2X.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierDecayCp2Y.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierReleaseCp1X.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierReleaseCp1Y.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierReleaseCp2X.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.bezierReleaseCp2Y.store(v, std::memory_order_relaxed); } else return true;
    return true;
}

template<typename SetParamFunc>
inline void loadModEnvParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float v = 0.0f;
    if (streamer.readFloat(v)) setParam(kModEnvAttackId, envTimeToNormalized(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvDecayId, envTimeToNormalized(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvSustainId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvReleaseId, envTimeToNormalized(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvAttackCurveId, envCurveToNormalized(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvDecayCurveId, envCurveToNormalized(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvReleaseCurveId, envCurveToNormalized(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierEnabledId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierAttackCp1XId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierAttackCp1YId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierAttackCp2XId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierAttackCp2YId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierDecayCp1XId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierDecayCp1YId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierDecayCp2XId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierDecayCp2YId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierReleaseCp1XId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierReleaseCp1YId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierReleaseCp2XId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kModEnvBezierReleaseCp2YId, static_cast<double>(v));
}

} // namespace Ruinae
