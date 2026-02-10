#pragma once
#include "plugin_ids.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace Ruinae {

struct AmpEnvParams {
    std::atomic<float> attackMs{10.0f};    // 0-10000 ms
    std::atomic<float> decayMs{100.0f};    // 0-10000 ms
    std::atomic<float> sustain{0.8f};      // 0-1
    std::atomic<float> releaseMs{200.0f};  // 0-10000 ms

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

// Exponential time mapping: normalized 0-1 -> 0-10000 ms
// Using x^3 * 10000 for perceptually linear feel
inline float envTimeFromNormalized(double value) {
    float v = static_cast<float>(value);
    return std::clamp(v * v * v * 10000.0f, 0.0f, 10000.0f);
}

inline double envTimeToNormalized(float ms) {
    return std::clamp(static_cast<double>(std::cbrt(ms / 10000.0f)), 0.0, 1.0);
}

// Curve amount mapping: normalized 0-1 -> [-1, +1]
inline float envCurveFromNormalized(double value) {
    return std::clamp(static_cast<float>(value) * 2.0f - 1.0f, -1.0f, 1.0f);
}

inline double envCurveToNormalized(float curve) {
    return std::clamp(static_cast<double>((curve + 1.0f) * 0.5f), 0.0, 1.0);
}

inline void handleAmpEnvParamChange(
    AmpEnvParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kAmpEnvAttackId:
            params.attackMs.store(envTimeFromNormalized(value), std::memory_order_relaxed);
            break;
        case kAmpEnvDecayId:
            params.decayMs.store(envTimeFromNormalized(value), std::memory_order_relaxed);
            break;
        case kAmpEnvSustainId:
            params.sustain.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kAmpEnvReleaseId:
            params.releaseMs.store(envTimeFromNormalized(value), std::memory_order_relaxed);
            break;
        case kAmpEnvAttackCurveId:
            params.attackCurve.store(envCurveFromNormalized(value), std::memory_order_relaxed);
            break;
        case kAmpEnvDecayCurveId:
            params.decayCurve.store(envCurveFromNormalized(value), std::memory_order_relaxed);
            break;
        case kAmpEnvReleaseCurveId:
            params.releaseCurve.store(envCurveFromNormalized(value), std::memory_order_relaxed);
            break;
        case kAmpEnvBezierEnabledId:
            params.bezierEnabled.store(value >= 0.5 ? 1.0f : 0.0f, std::memory_order_relaxed);
            break;
        case kAmpEnvBezierAttackCp1XId:
            params.bezierAttackCp1X.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kAmpEnvBezierAttackCp1YId:
            params.bezierAttackCp1Y.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kAmpEnvBezierAttackCp2XId:
            params.bezierAttackCp2X.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kAmpEnvBezierAttackCp2YId:
            params.bezierAttackCp2Y.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kAmpEnvBezierDecayCp1XId:
            params.bezierDecayCp1X.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kAmpEnvBezierDecayCp1YId:
            params.bezierDecayCp1Y.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kAmpEnvBezierDecayCp2XId:
            params.bezierDecayCp2X.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kAmpEnvBezierDecayCp2YId:
            params.bezierDecayCp2Y.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kAmpEnvBezierReleaseCp1XId:
            params.bezierReleaseCp1X.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kAmpEnvBezierReleaseCp1YId:
            params.bezierReleaseCp1Y.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kAmpEnvBezierReleaseCp2XId:
            params.bezierReleaseCp2X.store(static_cast<float>(value), std::memory_order_relaxed); break;
        case kAmpEnvBezierReleaseCp2YId:
            params.bezierReleaseCp2Y.store(static_cast<float>(value), std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerAmpEnvParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    // Default attack: 10ms -> cbrt(10/10000) ~ 0.1
    parameters.addParameter(STR16("Amp Attack"), STR16("ms"), 0, 0.1,
        ParameterInfo::kCanAutomate, kAmpEnvAttackId);
    // Default decay: 100ms -> cbrt(100/10000) ~ 0.215
    parameters.addParameter(STR16("Amp Decay"), STR16("ms"), 0, 0.215,
        ParameterInfo::kCanAutomate, kAmpEnvDecayId);
    parameters.addParameter(STR16("Amp Sustain"), STR16("%"), 0, 0.8,
        ParameterInfo::kCanAutomate, kAmpEnvSustainId);
    // Default release: 200ms -> cbrt(200/10000) ~ 0.271
    parameters.addParameter(STR16("Amp Release"), STR16("ms"), 0, 0.271,
        ParameterInfo::kCanAutomate, kAmpEnvReleaseId);

    // Curve amounts: default 0.0 -> normalized 0.5
    parameters.addParameter(STR16("Amp Attack Curve"), nullptr, 0, 0.5,
        ParameterInfo::kCanAutomate, kAmpEnvAttackCurveId);
    parameters.addParameter(STR16("Amp Decay Curve"), nullptr, 0, 0.5,
        ParameterInfo::kCanAutomate, kAmpEnvDecayCurveId);
    parameters.addParameter(STR16("Amp Release Curve"), nullptr, 0, 0.5,
        ParameterInfo::kCanAutomate, kAmpEnvReleaseCurveId);

    // Bezier mode flag: default off
    parameters.addParameter(STR16("Amp Env Bezier"), nullptr, 1, 0.0,
        ParameterInfo::kCanAutomate, kAmpEnvBezierEnabledId);

    // Bezier control points: default positions per data-model.md
    parameters.addParameter(STR16("Amp Atk Bez CP1 X"), nullptr, 0, 0.33,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kAmpEnvBezierAttackCp1XId);
    parameters.addParameter(STR16("Amp Atk Bez CP1 Y"), nullptr, 0, 0.33,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kAmpEnvBezierAttackCp1YId);
    parameters.addParameter(STR16("Amp Atk Bez CP2 X"), nullptr, 0, 0.67,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kAmpEnvBezierAttackCp2XId);
    parameters.addParameter(STR16("Amp Atk Bez CP2 Y"), nullptr, 0, 0.67,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kAmpEnvBezierAttackCp2YId);
    parameters.addParameter(STR16("Amp Dec Bez CP1 X"), nullptr, 0, 0.33,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kAmpEnvBezierDecayCp1XId);
    parameters.addParameter(STR16("Amp Dec Bez CP1 Y"), nullptr, 0, 0.67,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kAmpEnvBezierDecayCp1YId);
    parameters.addParameter(STR16("Amp Dec Bez CP2 X"), nullptr, 0, 0.67,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kAmpEnvBezierDecayCp2XId);
    parameters.addParameter(STR16("Amp Dec Bez CP2 Y"), nullptr, 0, 0.33,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kAmpEnvBezierDecayCp2YId);
    parameters.addParameter(STR16("Amp Rel Bez CP1 X"), nullptr, 0, 0.33,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kAmpEnvBezierReleaseCp1XId);
    parameters.addParameter(STR16("Amp Rel Bez CP1 Y"), nullptr, 0, 0.67,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kAmpEnvBezierReleaseCp1YId);
    parameters.addParameter(STR16("Amp Rel Bez CP2 X"), nullptr, 0, 0.67,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kAmpEnvBezierReleaseCp2XId);
    parameters.addParameter(STR16("Amp Rel Bez CP2 Y"), nullptr, 0, 0.33,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden, kAmpEnvBezierReleaseCp2YId);
}

inline Steinberg::tresult formatAmpEnvParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kAmpEnvAttackId: case kAmpEnvDecayId: case kAmpEnvReleaseId: {
            float ms = envTimeFromNormalized(value);
            char8 text[32];
            if (ms >= 1000.0f) snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
            else snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kAmpEnvSustainId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kAmpEnvAttackCurveId: case kAmpEnvDecayCurveId: case kAmpEnvReleaseCurveId: {
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

inline void saveAmpEnvParams(const AmpEnvParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.attackMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decayMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.sustain.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseMs.load(std::memory_order_relaxed));
    // Curve amounts
    streamer.writeFloat(params.attackCurve.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decayCurve.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseCurve.load(std::memory_order_relaxed));
    // Bezier mode
    streamer.writeFloat(params.bezierEnabled.load(std::memory_order_relaxed));
    // Bezier control points (12 values)
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

inline bool loadAmpEnvParams(AmpEnvParams& params, Steinberg::IBStreamer& streamer) {
    float v = 0.0f;
    if (!streamer.readFloat(v)) { return false; } params.attackMs.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) { return false; } params.decayMs.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) { return false; } params.sustain.store(v, std::memory_order_relaxed);
    if (!streamer.readFloat(v)) { return false; } params.releaseMs.store(v, std::memory_order_relaxed);
    // Curve amounts (optional for backward compatibility with older presets)
    if (streamer.readFloat(v)) { params.attackCurve.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.decayCurve.store(v, std::memory_order_relaxed); } else return true;
    if (streamer.readFloat(v)) { params.releaseCurve.store(v, std::memory_order_relaxed); } else return true;
    // Bezier mode
    if (streamer.readFloat(v)) { params.bezierEnabled.store(v, std::memory_order_relaxed); } else return true;
    // Bezier control points (12 values)
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
inline void loadAmpEnvParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float v = 0.0f;
    if (streamer.readFloat(v)) setParam(kAmpEnvAttackId, envTimeToNormalized(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvDecayId, envTimeToNormalized(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvSustainId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvReleaseId, envTimeToNormalized(v));
    else return;
    // Curve amounts (optional for backward compatibility)
    if (streamer.readFloat(v)) setParam(kAmpEnvAttackCurveId, envCurveToNormalized(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvDecayCurveId, envCurveToNormalized(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvReleaseCurveId, envCurveToNormalized(v));
    else return;
    // Bezier mode
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierEnabledId, static_cast<double>(v));
    else return;
    // Bezier control points (12 values, already normalized [0,1])
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierAttackCp1XId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierAttackCp1YId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierAttackCp2XId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierAttackCp2YId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierDecayCp1XId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierDecayCp1YId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierDecayCp2XId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierDecayCp2YId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierReleaseCp1XId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierReleaseCp1YId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierReleaseCp2XId, static_cast<double>(v));
    else return;
    if (streamer.readFloat(v)) setParam(kAmpEnvBezierReleaseCp2YId, static_cast<double>(v));
}

} // namespace Ruinae
