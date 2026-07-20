#pragma once
// Base-ID-parameterized LFO parameter block shared by LFO1 and LFO2. The two
// LFOs are byte-identical except for their parameter ID block (kLFO1RateId..
// kLFO1QuantizeId at 1000-1010, kLFO2* at 1100-1110, a flat +100 offset) and
// their UI label prefix ("LFO 1 " / "LFO 2 "). This file is the single source
// of truth; lfo1_params.h / lfo2_params.h are thin shims that preserve the
// historical per-LFO API so existing processor/controller/preset/test call
// sites need no changes.
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/note_value_ui.h"
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
// Parameter block
// ============================================================================

struct LFOParams {
    std::atomic<float> rateHz{1.0f};       // 0.01-50 Hz
    std::atomic<int> shape{0};             // Waveform enum (0-5)
    std::atomic<float> depth{1.0f};        // 0-1
    std::atomic<bool> sync{true};          // on/off (default: sync to host)
    // Extended params (v12)
    std::atomic<float> phaseOffset{0.0f};  // 0-360 degrees
    std::atomic<bool> retrigger{true};     // on/off
    std::atomic<int> noteValue{10};        // dropdown index (default: 1/8 note)
    std::atomic<bool> unipolar{false};     // bipolar by default
    std::atomic<float> fadeInMs{0.0f};     // 0-5000 ms
    std::atomic<float> symmetry{0.5f};     // 0-1 (0.5 = centered)
    std::atomic<int> quantizeSteps{0};     // 0=off, 2-16
};

// ID offsets from the per-LFO block base (kLFO1RateId / kLFO2RateId).
enum LFOParamOffset {
    kLFORateOffset = 0,
    kLFOShapeOffset = 1,
    kLFODepthOffset = 2,
    kLFOSyncOffset = 3,
    kLFOPhaseOffsetOffset = 4,
    kLFORetriggerOffset = 5,
    kLFONoteValueOffset = 6,
    kLFOUnipolarOffset = 7,
    kLFOFadeInOffset = 8,
    kLFOSymmetryOffset = 9,
    kLFOQuantizeOffset = 10,
};

// ============================================================================
// Normalized <-> denormalized mappings (shared by both LFOs)
// ============================================================================

// Exponential rate: 0-1 -> 0.01-50 Hz.
// Now evaluated in double via the shared helper rather than in float; the two
// agree to well within a display digit, and the golden round-trip test pins the
// current values.
inline float lfoRateFromNormalized(double value) {
    return static_cast<float>(
        logMapFromNormalized(value, 0.01, 50.0));
}

inline double lfoRateToNormalized(float hz) {
    return logMapToNormalized(static_cast<double>(hz), 0.01, 50.0);
}

// Exponential fade-in: 0-1 -> 0-5000 ms (0 = off)
inline float lfoFadeInFromNormalized(double value) {
    if (value < 0.001) return 0.0f;
    return 1.0f * std::pow(5000.0f, static_cast<float>(value));
}

inline double lfoFadeInToNormalized(float ms) {
    if (ms <= 0.0f) return 0.0;
    return std::clamp(static_cast<double>(std::log(ms) / std::log(5000.0f)), 0.0, 1.0);
}

// Quantize: 0=off, 1->2 steps, ..., 14->16 steps (stepCount=15)
inline constexpr int kQuantizeStepCount = 15;  // 16 positions: 0=off, 1-15 -> 2-16 steps

inline int lfoQuantizeFromNormalized(double value) {
    int index = std::clamp(static_cast<int>(value * kQuantizeStepCount + 0.5), 0, kQuantizeStepCount);
    if (index == 0) return 0;  // off
    return index + 1;          // 2-16
}

inline double lfoQuantizeToNormalized(int steps) {
    if (steps < 2) return 0.0;
    int index = steps - 1;  // 2->1, 3->2, ..., 16->15
    return static_cast<double>(index) / kQuantizeStepCount;
}

// ============================================================================
// Generic operations (base-parameterized)
// ============================================================================

inline void handleLFOParamChange(
    LFOParams& params, Steinberg::Vst::ParamID base,
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    switch (id - base) {
        case kLFORateOffset:
            params.rateHz.store(lfoRateFromNormalized(value), std::memory_order_relaxed); break;
        case kLFOShapeOffset:
            params.shape.store(std::clamp(static_cast<int>(value * (kWaveformCount - 1) + 0.5), 0, kWaveformCount - 1), std::memory_order_relaxed); break;
        case kLFODepthOffset:
            params.depth.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        case kLFOSyncOffset:
            params.sync.store(value >= 0.5, std::memory_order_relaxed); break;
        case kLFOPhaseOffsetOffset:
            params.phaseOffset.store(static_cast<float>(value * 360.0), std::memory_order_relaxed); break;
        case kLFORetriggerOffset:
            params.retrigger.store(value >= 0.5, std::memory_order_relaxed); break;
        case kLFONoteValueOffset:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed); break;
        case kLFOUnipolarOffset:
            params.unipolar.store(value >= 0.5, std::memory_order_relaxed); break;
        case kLFOFadeInOffset:
            params.fadeInMs.store(lfoFadeInFromNormalized(value), std::memory_order_relaxed); break;
        case kLFOSymmetryOffset:
            params.symmetry.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        case kLFOQuantizeOffset:
            params.quantizeSteps.store(lfoQuantizeFromNormalized(value), std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerLFOParams(Steinberg::Vst::ParameterContainer& parameters,
                              Steinberg::Vst::ParamID base,
                              const Steinberg::char16* labelPrefix) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    String128 buf;
    auto label = [&](const char16* suffix) -> const TChar* {
        UString u(buf, 128);
        u.assign(labelPrefix);
        u.append(suffix);
        return buf;
    };

    // Default 1 Hz -> log(1/0.01)/log(5000) ~ 0.540
    parameters.addParameter(label(STR16("Rate")), STR16("Hz"), 0, 0.540,
        ParameterInfo::kCanAutomate, base + kLFORateOffset);
    parameters.addParameter(createDropdownParameter(
        label(STR16("Shape")), base + kLFOShapeOffset,
        {STR16("Sine"), STR16("Triangle"), STR16("Sawtooth"),
         STR16("Square"), STR16("Sample & Hold"), STR16("Smooth Random")}
    ));
    parameters.addParameter(label(STR16("Depth")), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, base + kLFODepthOffset);
    parameters.addParameter(label(STR16("Sync")), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, base + kLFOSyncOffset);
    // Extended params
    parameters.addParameter(label(STR16("Phase")), STR16("deg"), 0, 0.0,
        ParameterInfo::kCanAutomate, base + kLFOPhaseOffsetOffset);
    parameters.addParameter(label(STR16("Retrigger")), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, base + kLFORetriggerOffset);
    parameters.addParameter(createNoteValueDropdown(
        label(STR16("Note Value")), base + kLFONoteValueOffset,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex));
    parameters.addParameter(label(STR16("Unipolar")), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, base + kLFOUnipolarOffset);
    parameters.addParameter(label(STR16("Fade In")), STR16("ms"), 0, 0.0,
        ParameterInfo::kCanAutomate, base + kLFOFadeInOffset);
    parameters.addParameter(label(STR16("Symmetry")), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, base + kLFOSymmetryOffset);
    parameters.addParameter(label(STR16("Quantize")), STR16(""), kQuantizeStepCount, 0.0,
        ParameterInfo::kCanAutomate, base + kLFOQuantizeOffset);
}

inline Steinberg::tresult formatLFOParam(
    Steinberg::Vst::ParamID base, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value, Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id - base) {
        case kLFORateOffset: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f Hz", lfoRateFromNormalized(value));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFODepthOffset: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFOPhaseOffsetOffset: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f deg", value * 360.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFOFadeInOffset: {
            char8 text[32];
            float ms = lfoFadeInFromNormalized(value);
            if (ms < 1.0f) {
                snprintf(text, sizeof(text), "Off");
            } else if (ms < 1000.0f) {
                snprintf(text, sizeof(text), "%.0f ms", ms);
            } else {
                snprintf(text, sizeof(text), "%.1f s", ms / 1000.0f);
            }
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFOSymmetryOffset: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFOQuantizeOffset: {
            char8 text[32];
            int steps = lfoQuantizeFromNormalized(value);
            if (steps < 2) {
                snprintf(text, sizeof(text), "Off");
            } else {
                snprintf(text, sizeof(text), "%d steps", steps);
            }
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveLFOParams(const LFOParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.shape.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
}

inline bool loadLFOParams(LFOParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) { return false; } params.rateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.shape.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.depth.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.sync.store(iv != 0, std::memory_order_relaxed);
    return true;
}

// Extended save/load for v12+
inline void saveLFOExtendedParams(const LFOParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.phaseOffset.load(std::memory_order_relaxed));
    streamer.writeInt32(params.retrigger.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    streamer.writeInt32(params.unipolar.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.fadeInMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.symmetry.load(std::memory_order_relaxed));
    streamer.writeInt32(params.quantizeSteps.load(std::memory_order_relaxed));
}

inline bool loadLFOExtendedParams(LFOParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) { return false; } params.phaseOffset.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.retrigger.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.noteValue.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.unipolar.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.fadeInMs.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.symmetry.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.quantizeSteps.store(iv, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadLFOParamsToController(
    Steinberg::Vst::ParamID base, Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(base + kLFORateOffset, lfoRateToNormalized(fv));
    if (streamer.readInt32(iv)) setParam(base + kLFOShapeOffset, static_cast<double>(iv) / (kWaveformCount - 1));
    if (streamer.readFloat(fv)) setParam(base + kLFODepthOffset, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(base + kLFOSyncOffset, iv != 0 ? 1.0 : 0.0);
}

template<typename SetParamFunc>
inline void loadLFOExtendedParamsToController(
    Steinberg::Vst::ParamID base, Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(base + kLFOPhaseOffsetOffset, static_cast<double>(fv) / 360.0);
    if (streamer.readInt32(iv)) setParam(base + kLFORetriggerOffset, iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv)) setParam(base + kLFONoteValueOffset, static_cast<double>(iv) / (Parameters::kNoteValueDropdownCount - 1));
    if (streamer.readInt32(iv)) setParam(base + kLFOUnipolarOffset, iv != 0 ? 1.0 : 0.0);
    if (streamer.readFloat(fv)) setParam(base + kLFOFadeInOffset, lfoFadeInToNormalized(fv));
    if (streamer.readFloat(fv)) setParam(base + kLFOSymmetryOffset, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(base + kLFOQuantizeOffset, lfoQuantizeToNormalized(iv));
}

} // namespace Ruinae
