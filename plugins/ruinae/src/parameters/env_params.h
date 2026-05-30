#pragma once
// Config-driven ADSR envelope parameter block shared by the Amp, Filter, and
// Mod envelopes. The three envelopes are byte-identical except for their
// parameter ID block (kAmpEnvBaseId / kFilterEnvBaseId / kModEnvBaseId, laid
// out as contiguous +100-offset blocks in plugin_ids.h), their UI label
// prefixes, and a handful of default values. This file holds the single
// source of truth; amp_env_params.h / filter_env_params.h / mod_env_params.h
// are thin re-export shims that preserve the historical per-envelope API.
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

// ============================================================================
// Normalized <-> denormalized mappings (shared by all three envelopes)
// ============================================================================

// Exponential time mapping: normalized 0-1 -> 0-10000 ms.
// Using x^3 * 10000 for perceptually linear feel.
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

// ============================================================================
// Parameter block
// ============================================================================

// One ADSR envelope's runtime state. Concrete envelopes derive from this and
// only override the Attack/Decay/Sustain/Release defaults; the curve and Bezier
// defaults are identical across all three.
struct EnvParams {
    std::atomic<float> attackMs;
    std::atomic<float> decayMs;
    std::atomic<float> sustain;
    std::atomic<float> releaseMs;

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

    EnvParams(float attack, float decay, float sustainDef, float release)
        : attackMs(attack), decayMs(decay), sustain(sustainDef), releaseMs(release) {}
};

struct AmpEnvParams : EnvParams {
    AmpEnvParams() : EnvParams(10.0f, 100.0f, 0.8f, 200.0f) {}
};

struct FilterEnvParams : EnvParams {
    FilterEnvParams() : EnvParams(10.0f, 200.0f, 0.5f, 300.0f) {}
};

struct ModEnvParams : EnvParams {
    ModEnvParams() : EnvParams(10.0f, 300.0f, 0.5f, 500.0f) {}
};

// ============================================================================
// Field table — single source for stream order, ID offsets, and mappings
// ============================================================================

enum class EnvMap { Time, Sustain, Curve, BezierEnabled, BezierCp };

struct EnvField {
    std::atomic<float> EnvParams::* member;  // points into EnvParams
    Steinberg::Vst::ParamID offset;          // ID offset from the block base
    EnvMap map;
};

// The 20 persisted fields, in stream order. Offsets match the +0..+7 / +10..+21
// layout shared by the Amp/Filter/Mod blocks in plugin_ids.h.
inline const EnvField* envFields() {
    static const EnvField fields[] = {
        {&EnvParams::attackMs,          0, EnvMap::Time},
        {&EnvParams::decayMs,           1, EnvMap::Time},
        {&EnvParams::sustain,           2, EnvMap::Sustain},
        {&EnvParams::releaseMs,         3, EnvMap::Time},
        {&EnvParams::attackCurve,       4, EnvMap::Curve},
        {&EnvParams::decayCurve,        5, EnvMap::Curve},
        {&EnvParams::releaseCurve,      6, EnvMap::Curve},
        {&EnvParams::bezierEnabled,     7, EnvMap::BezierEnabled},
        {&EnvParams::bezierAttackCp1X, 10, EnvMap::BezierCp},
        {&EnvParams::bezierAttackCp1Y, 11, EnvMap::BezierCp},
        {&EnvParams::bezierAttackCp2X, 12, EnvMap::BezierCp},
        {&EnvParams::bezierAttackCp2Y, 13, EnvMap::BezierCp},
        {&EnvParams::bezierDecayCp1X,  14, EnvMap::BezierCp},
        {&EnvParams::bezierDecayCp1Y,  15, EnvMap::BezierCp},
        {&EnvParams::bezierDecayCp2X,  16, EnvMap::BezierCp},
        {&EnvParams::bezierDecayCp2Y,  17, EnvMap::BezierCp},
        {&EnvParams::bezierReleaseCp1X, 18, EnvMap::BezierCp},
        {&EnvParams::bezierReleaseCp1Y, 19, EnvMap::BezierCp},
        {&EnvParams::bezierReleaseCp2X, 20, EnvMap::BezierCp},
        {&EnvParams::bezierReleaseCp2Y, 21, EnvMap::BezierCp},
    };
    return fields;
}

constexpr int kEnvFieldCount = 20;

// Number of mandatory (non-backward-compatible) leading fields: A/D/S/R.
constexpr int kEnvMandatoryFieldCount = 4;

// ============================================================================
// Generic operations (base-parameterized)
// ============================================================================

// Apply a normalized host value to one field.
inline void applyEnvNormalized(EnvParams& params, const EnvField& field,
                               Steinberg::Vst::ParamValue value) {
    switch (field.map) {
        case EnvMap::Time:
            (params.*field.member).store(envTimeFromNormalized(value), std::memory_order_relaxed);
            break;
        case EnvMap::Sustain:
            (params.*field.member).store(std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                                        std::memory_order_relaxed);
            break;
        case EnvMap::Curve:
            (params.*field.member).store(envCurveFromNormalized(value), std::memory_order_relaxed);
            break;
        case EnvMap::BezierEnabled:
            (params.*field.member).store(value >= 0.5 ? 1.0f : 0.0f, std::memory_order_relaxed);
            break;
        case EnvMap::BezierCp:
            (params.*field.member).store(static_cast<float>(value), std::memory_order_relaxed);
            break;
    }
}

inline void handleEnvParamChange(EnvParams& params, Steinberg::Vst::ParamID base,
                                 Steinberg::Vst::ParamID id,
                                 Steinberg::Vst::ParamValue value) {
    const int offset = static_cast<int>(id) - static_cast<int>(base);
    const EnvField* fields = envFields();
    for (int i = 0; i < kEnvFieldCount; ++i) {
        if (static_cast<int>(fields[i].offset) == offset) {
            applyEnvNormalized(params, fields[i], value);
            return;
        }
    }
}

inline void registerEnvParams(Steinberg::Vst::ParameterContainer& parameters,
                              Steinberg::Vst::ParamID base,
                              const Steinberg::char16* mainPrefix,
                              const Steinberg::char16* bezierTogglePrefix,
                              const Steinberg::char16* cpPrefix,
                              double decayDefault, double sustainDefault,
                              double releaseDefault) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    String128 buf;
    auto add = [&](const char16* prefix, const char16* suffix, const char16* units,
                   int32 stepCount, double defaultNorm, int32 flags, ParamID id) {
        UString u(buf, 128);
        u.assign(prefix);
        u.append(suffix);
        parameters.addParameter(buf, units, stepCount, defaultNorm, flags, id);
    };

    // Default attack: 10ms -> cbrt(10/10000) ~ 0.1 (same for all three envelopes)
    add(mainPrefix, STR16("Attack"), STR16("ms"), 0, 0.1,
        ParameterInfo::kCanAutomate, base + 0);
    add(mainPrefix, STR16("Decay"), STR16("ms"), 0, decayDefault,
        ParameterInfo::kCanAutomate, base + 1);
    add(mainPrefix, STR16("Sustain"), STR16("%"), 0, sustainDefault,
        ParameterInfo::kCanAutomate, base + 2);
    add(mainPrefix, STR16("Release"), STR16("ms"), 0, releaseDefault,
        ParameterInfo::kCanAutomate, base + 3);

    // Curve amounts: default 0.0 -> normalized 0.5
    add(mainPrefix, STR16("Attack Curve"), nullptr, 0, 0.5,
        ParameterInfo::kCanAutomate, base + 4);
    add(mainPrefix, STR16("Decay Curve"), nullptr, 0, 0.5,
        ParameterInfo::kCanAutomate, base + 5);
    add(mainPrefix, STR16("Release Curve"), nullptr, 0, 0.5,
        ParameterInfo::kCanAutomate, base + 6);

    // Bezier mode flag: default off
    add(bezierTogglePrefix, STR16("Bezier"), nullptr, 1, 0.0,
        ParameterInfo::kCanAutomate, base + 7);

    // Bezier control points: default positions per data-model.md
    static const char16* kCpSuffix[12] = {
        STR16("Atk Bez CP1 X"), STR16("Atk Bez CP1 Y"),
        STR16("Atk Bez CP2 X"), STR16("Atk Bez CP2 Y"),
        STR16("Dec Bez CP1 X"), STR16("Dec Bez CP1 Y"),
        STR16("Dec Bez CP2 X"), STR16("Dec Bez CP2 Y"),
        STR16("Rel Bez CP1 X"), STR16("Rel Bez CP1 Y"),
        STR16("Rel Bez CP2 X"), STR16("Rel Bez CP2 Y"),
    };
    static const double kCpDefault[12] = {
        0.33, 0.33, 0.67, 0.67,
        0.33, 0.67, 0.67, 0.33,
        0.33, 0.67, 0.67, 0.33,
    };
    for (int i = 0; i < 12; ++i) {
        add(cpPrefix, kCpSuffix[i], nullptr, 0, kCpDefault[i],
            ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden,
            base + 10 + static_cast<ParamID>(i));
    }
}

inline Steinberg::tresult formatEnvParam(Steinberg::Vst::ParamID base,
                                         Steinberg::Vst::ParamID id,
                                         Steinberg::Vst::ParamValue value,
                                         Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    const int offset = static_cast<int>(id) - static_cast<int>(base);
    // Attack (0) / Decay (1) / Release (3): time in ms or s
    if (offset == 0 || offset == 1 || offset == 3) {
        float ms = envTimeFromNormalized(value);
        char8 text[32];
        if (ms >= 1000.0f) snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
        else snprintf(text, sizeof(text), "%.1f ms", ms);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    // Sustain (2): percent
    if (offset == 2) {
        char8 text[32];
        snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    // Curve amounts (4/5/6): signed float
    if (offset == 4 || offset == 5 || offset == 6) {
        float curve = envCurveFromNormalized(value);
        char8 text[32];
        snprintf(text, sizeof(text), "%+.2f", curve);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse;
}

inline void saveEnvParams(const EnvParams& params, Steinberg::IBStreamer& streamer) {
    const EnvField* fields = envFields();
    for (int i = 0; i < kEnvFieldCount; ++i) {
        streamer.writeFloat((params.*fields[i].member).load(std::memory_order_relaxed));
    }
}

inline bool loadEnvParams(EnvParams& params, Steinberg::IBStreamer& streamer) {
    const EnvField* fields = envFields();
    float v = 0.0f;
    // A/D/S/R are mandatory.
    for (int i = 0; i < kEnvMandatoryFieldCount; ++i) {
        if (!streamer.readFloat(v)) { return false; }
        (params.*fields[i].member).store(v, std::memory_order_relaxed);
    }
    // Curve/Bezier fields are optional for backward compatibility with older presets.
    for (int i = kEnvMandatoryFieldCount; i < kEnvFieldCount; ++i) {
        if (streamer.readFloat(v)) {
            (params.*fields[i].member).store(v, std::memory_order_relaxed);
        } else {
            return true;
        }
    }
    return true;
}

inline double envFieldToNormalized(const EnvField& field, float v) {
    switch (field.map) {
        case EnvMap::Time:  return envTimeToNormalized(v);
        case EnvMap::Curve: return envCurveToNormalized(v);
        case EnvMap::Sustain:
        case EnvMap::BezierEnabled:
        case EnvMap::BezierCp:
        default:            return static_cast<double>(v);
    }
}

template <typename SetParamFunc>
inline void loadEnvParamsToController(Steinberg::Vst::ParamID base,
                                      Steinberg::IBStreamer& streamer,
                                      SetParamFunc setParam) {
    const EnvField* fields = envFields();
    float v = 0.0f;
    for (int i = 0; i < kEnvFieldCount; ++i) {
        if (streamer.readFloat(v)) {
            setParam(base + fields[i].offset, envFieldToNormalized(fields[i], v));
        } else {
            return;
        }
    }
}

// ============================================================================
// Per-envelope wrappers — preserve the historical named API so existing
// processor/controller/preset call sites need no changes.
// ============================================================================

// ---- Amp envelope ----
inline void handleAmpEnvParamChange(AmpEnvParams& params, Steinberg::Vst::ParamID id,
                                    Steinberg::Vst::ParamValue value) {
    handleEnvParamChange(params, kAmpEnvBaseId, id, value);
}
inline void registerAmpEnvParams(Steinberg::Vst::ParameterContainer& parameters) {
    registerEnvParams(parameters, kAmpEnvBaseId, STR16("Amp "), STR16("Amp Env "),
                      STR16("Amp "), 0.215, 0.8, 0.271);
}
inline Steinberg::tresult formatAmpEnvParam(Steinberg::Vst::ParamID id,
                                            Steinberg::Vst::ParamValue value,
                                            Steinberg::Vst::String128 string) {
    return formatEnvParam(kAmpEnvBaseId, id, value, string);
}
inline void saveAmpEnvParams(const AmpEnvParams& params, Steinberg::IBStreamer& streamer) {
    saveEnvParams(params, streamer);
}
inline bool loadAmpEnvParams(AmpEnvParams& params, Steinberg::IBStreamer& streamer) {
    return loadEnvParams(params, streamer);
}
template <typename SetParamFunc>
inline void loadAmpEnvParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    loadEnvParamsToController(kAmpEnvBaseId, streamer, setParam);
}

// ---- Filter envelope ----
inline void handleFilterEnvParamChange(FilterEnvParams& params, Steinberg::Vst::ParamID id,
                                       Steinberg::Vst::ParamValue value) {
    handleEnvParamChange(params, kFilterEnvBaseId, id, value);
}
inline void registerFilterEnvParams(Steinberg::Vst::ParameterContainer& parameters) {
    registerEnvParams(parameters, kFilterEnvBaseId, STR16("Filter Env "), STR16("Filter Env "),
                      STR16("Flt "), 0.271, 0.5, 0.310);
}
inline Steinberg::tresult formatFilterEnvParam(Steinberg::Vst::ParamID id,
                                               Steinberg::Vst::ParamValue value,
                                               Steinberg::Vst::String128 string) {
    return formatEnvParam(kFilterEnvBaseId, id, value, string);
}
inline void saveFilterEnvParams(const FilterEnvParams& params, Steinberg::IBStreamer& streamer) {
    saveEnvParams(params, streamer);
}
inline bool loadFilterEnvParams(FilterEnvParams& params, Steinberg::IBStreamer& streamer) {
    return loadEnvParams(params, streamer);
}
template <typename SetParamFunc>
inline void loadFilterEnvParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    loadEnvParamsToController(kFilterEnvBaseId, streamer, setParam);
}

// ---- Mod envelope ----
inline void handleModEnvParamChange(ModEnvParams& params, Steinberg::Vst::ParamID id,
                                    Steinberg::Vst::ParamValue value) {
    handleEnvParamChange(params, kModEnvBaseId, id, value);
}
inline void registerModEnvParams(Steinberg::Vst::ParameterContainer& parameters) {
    registerEnvParams(parameters, kModEnvBaseId, STR16("Mod Env "), STR16("Mod Env "),
                      STR16("Mod "), 0.310, 0.5, 0.368);
}
inline Steinberg::tresult formatModEnvParam(Steinberg::Vst::ParamID id,
                                            Steinberg::Vst::ParamValue value,
                                            Steinberg::Vst::String128 string) {
    return formatEnvParam(kModEnvBaseId, id, value, string);
}
inline void saveModEnvParams(const ModEnvParams& params, Steinberg::IBStreamer& streamer) {
    saveEnvParams(params, streamer);
}
inline bool loadModEnvParams(ModEnvParams& params, Steinberg::IBStreamer& streamer) {
    return loadEnvParams(params, streamer);
}
template <typename SetParamFunc>
inline void loadModEnvParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    loadEnvParamsToController(kModEnvBaseId, streamer, setParam);
}

} // namespace Ruinae
