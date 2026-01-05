#pragma once

// ==============================================================================
// MultiTap Delay Parameters
// ==============================================================================
// Parameter pack for MultiTap Delay (spec 028)
// ID Range: 900-999
//
// SIMPLIFIED DESIGN:
// - No TimeMode toggle, no Base Time slider, no Internal Tempo slider
// - Rhythmic patterns (0-13): Use host tempo. Pattern name defines note value.
// - Mathematical patterns (14-19): Use Note Value + host tempo for baseTimeMs.
// ==============================================================================

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/note_value_ui.h"
#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"

#include <atomic>
#include <array>
#include <cmath>

namespace Iterum {

// ==============================================================================
// Custom Pattern Constants
// ==============================================================================

static constexpr size_t kCustomPatternMaxTaps = 16;

// ==============================================================================
// Parameter Storage
// ==============================================================================

struct MultiTapParams {
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};  // 0-19 (note values) - for mathematical patterns
    std::atomic<int> noteModifier{0};           // 0-2 (none, triplet, dotted) - for mathematical patterns
    std::atomic<int> timingPattern{2};          // 0-19 (pattern presets)
    std::atomic<int> spatialPattern{2};         // 0-6 (spatial presets)
    std::atomic<int> tapCount{4};               // 2-16 taps
    std::atomic<int> snapDivision{14};          // 0-21 (off + 21 note values) - spec 046, default: 1/4 (index 14)
    std::atomic<float> feedback{0.5f};          // 0-1.1 (110%)
    std::atomic<float> feedbackLPCutoff{20000.0f};  // 20-20000Hz
    std::atomic<float> feedbackHPCutoff{20.0f};     // 20-20000Hz
    std::atomic<float> morphTime{500.0f};       // 50-2000ms
    std::atomic<float> dryWet{0.5f};            // 0-1 (dry/wet mix)

    // Custom Pattern Data (spec 046) - 16 taps × 2 values
    // Time ratios: 0.0-1.0 (ratio of max delay time)
    // Levels: 0.0-1.0 (linear gain)
    std::array<std::atomic<float>, kCustomPatternMaxTaps> customTimeRatios{};
    std::array<std::atomic<float>, kCustomPatternMaxTaps> customLevels{};

    // Initialize custom pattern to defaults (evenly spaced, full levels)
    MultiTapParams() {
        for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
            // Default: evenly spaced from 0 to 1
            customTimeRatios[i].store(static_cast<float>(i + 1) / (kCustomPatternMaxTaps + 1),
                                       std::memory_order_relaxed);
            // Default: full level (1.0)
            customLevels[i].store(1.0f, std::memory_order_relaxed);
        }
    }
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleMultiTapParamChange(
    MultiTapParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) {

    using namespace Steinberg;

    switch (id) {
        case kMultiTapNoteValueId:
            // 0-9 (note values) - for mathematical patterns
            params.noteValue.store(
                static_cast<int>(normalizedValue * 9.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kMultiTapNoteModifierId:
            // 0-2 (none, triplet, dotted) - for mathematical patterns
            params.noteModifier.store(
                static_cast<int>(normalizedValue * 2.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kMultiTapSnapDivisionId:
            // 0-21 (off + 21 note values) - spec 046
            params.snapDivision.store(
                static_cast<int>(normalizedValue * 21.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kMultiTapTimingPatternId:
            // 0-19
            params.timingPattern.store(
                static_cast<int>(normalizedValue * 19.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kMultiTapSpatialPatternId:
            // 0-6
            params.spatialPattern.store(
                static_cast<int>(normalizedValue * 6.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kMultiTapTapCountId:
            // 2-16
            params.tapCount.store(
                static_cast<int>(2.0 + normalizedValue * 14.0 + 0.5),
                std::memory_order_relaxed);
            break;
        case kMultiTapFeedbackId:
            // 0-1.1 (110%)
            params.feedback.store(
                static_cast<float>(normalizedValue * 1.1),
                std::memory_order_relaxed);
            break;
        case kMultiTapFeedbackLPCutoffId:
            // 20-20000Hz (logarithmic)
            params.feedbackLPCutoff.store(
                static_cast<float>(20.0 * std::pow(1000.0, normalizedValue)),
                std::memory_order_relaxed);
            break;
        case kMultiTapFeedbackHPCutoffId:
            // 20-20000Hz (logarithmic)
            params.feedbackHPCutoff.store(
                static_cast<float>(20.0 * std::pow(1000.0, normalizedValue)),
                std::memory_order_relaxed);
            break;
        case kMultiTapMorphTimeId:
            // 50-2000ms
            params.morphTime.store(
                static_cast<float>(50.0 + normalizedValue * 1950.0),
                std::memory_order_relaxed);
            break;
        case kMultiTapMixId:
            // 0-1 (passthrough)
            params.dryWet.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        // Custom Pattern Time Ratios (950-965)
        default:
            if (id >= kMultiTapCustomTime0Id && id <= kMultiTapCustomTime15Id) {
                size_t tapIndex = id - kMultiTapCustomTime0Id;
                params.customTimeRatios[tapIndex].store(
                    static_cast<float>(normalizedValue),
                    std::memory_order_relaxed);
            }
            // Custom Pattern Levels (966-981)
            else if (id >= kMultiTapCustomLevel0Id && id <= kMultiTapCustomLevel15Id) {
                size_t tapIndex = id - kMultiTapCustomLevel0Id;
                params.customLevels[tapIndex].store(
                    static_cast<float>(normalizedValue),
                    std::memory_order_relaxed);
            }
            break;
    }
}

// ==============================================================================
// Parameter Registration (for Controller)
// ==============================================================================

inline void registerMultiTapParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Note Value (for mathematical patterns) - 0-9 basic note values
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("MultiTap Note Value"), kMultiTapNoteValueId,
        2,  // default: Quarter (index 2)
        {STR16("Whole"), STR16("Half"), STR16("Quarter"), STR16("8th"), STR16("16th"),
         STR16("32nd"), STR16("64th"), STR16("128th"), STR16("1/2T"), STR16("1/4T")}
    ));

    // Note Modifier (for mathematical patterns) - None, Triplet, Dotted
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("MultiTap Note Modifier"), kMultiTapNoteModifierId,
        0,  // default: None (index 0)
        {STR16("None"), STR16("Triplet"), STR16("Dotted")}
    ));

    // Snap Division (spec 046 - grid snapping for custom pattern editor)
    // Uses same note values as delay time dropdown, with "Off" at index 0
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("MultiTap Snap"), kMultiTapSnapDivisionId,
        14,  // default: 1/4 (index 14)
        {STR16("Off"),
         // 1/64 variants
         STR16("1/64T"), STR16("1/64"), STR16("1/64D"),
         // 1/32 variants
         STR16("1/32T"), STR16("1/32"), STR16("1/32D"),
         // 1/16 variants
         STR16("1/16T"), STR16("1/16"), STR16("1/16D"),
         // 1/8 variants
         STR16("1/8T"), STR16("1/8"), STR16("1/8D"),
         // 1/4 variants
         STR16("1/4T"), STR16("1/4"), STR16("1/4D"),
         // 1/2 variants
         STR16("1/2T"), STR16("1/2"), STR16("1/2D"),
         // 1/1 variants
         STR16("1/1T"), STR16("1/1"), STR16("1/1D")}
    ));

    // Timing Pattern (20 patterns) - MUST use StringListParameter
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("MultiTap Timing Pattern"), kMultiTapTimingPatternId,
        2,  // default: Quarter (index 2)
        {STR16("Whole"), STR16("Half"), STR16("Quarter"), STR16("Eighth"), STR16("16th"), STR16("32nd"),
         STR16("Dotted Half"), STR16("Dotted Qtr"), STR16("Dotted 8th"), STR16("Dotted 16th"),
         STR16("Triplet Half"), STR16("Triplet Qtr"), STR16("Triplet 8th"), STR16("Triplet 16th"),
         STR16("Golden Ratio"), STR16("Fibonacci"), STR16("Exponential"), STR16("Primes"), STR16("Linear"), STR16("Custom")}
    ));

    // Spatial Pattern (7 patterns) - MUST use StringListParameter
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("MultiTap Spatial Pattern"), kMultiTapSpatialPatternId,
        2,  // default: Centered (index 2)
        {STR16("Cascade"), STR16("Alternating"), STR16("Centered"), STR16("Widening"), STR16("Decaying"), STR16("Flat"), STR16("Custom")}
    ));

    // Tap Count (2-16)
    parameters.addParameter(
        STR16("MultiTap Tap Count"),
        nullptr,
        14,  // 15 values (2-16)
        0.143,  // default: 4 taps normalized = (4-2)/14
        ParameterInfo::kCanAutomate,
        kMultiTapTapCountId);

    // Feedback (0-110%)
    parameters.addParameter(
        STR16("MultiTap Feedback"),
        STR16("%"),
        0,
        0.455,  // default: 50% normalized = 0.5/1.1
        ParameterInfo::kCanAutomate,
        kMultiTapFeedbackId);

    // Feedback LP Cutoff (20-20000Hz)
    parameters.addParameter(
        STR16("MultiTap Feedback LP"),
        STR16("Hz"),
        0,
        1.0,  // default: 20000Hz (max)
        ParameterInfo::kCanAutomate,
        kMultiTapFeedbackLPCutoffId);

    // Feedback HP Cutoff (20-20000Hz)
    parameters.addParameter(
        STR16("MultiTap Feedback HP"),
        STR16("Hz"),
        0,
        0.0,  // default: 20Hz (min)
        ParameterInfo::kCanAutomate,
        kMultiTapFeedbackHPCutoffId);

    // Morph Time (50-2000ms)
    parameters.addParameter(
        STR16("MultiTap Morph Time"),
        STR16("ms"),
        0,
        0.231,  // default: 500ms normalized = (500-50)/1950
        ParameterInfo::kCanAutomate,
        kMultiTapMorphTimeId);

    // Dry/Wet Mix (0-100%)
    parameters.addParameter(
        STR16("MultiTap Dry/Wet"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kMultiTapMixId);

    // Custom Pattern Time Ratios (950-965) - spec 046
    // 16 parameters for custom tap time ratios (0.0-1.0)
    static const Steinberg::Vst::TChar* customTimeNames[16] = {
        STR16("Custom Time 1"), STR16("Custom Time 2"), STR16("Custom Time 3"), STR16("Custom Time 4"),
        STR16("Custom Time 5"), STR16("Custom Time 6"), STR16("Custom Time 7"), STR16("Custom Time 8"),
        STR16("Custom Time 9"), STR16("Custom Time 10"), STR16("Custom Time 11"), STR16("Custom Time 12"),
        STR16("Custom Time 13"), STR16("Custom Time 14"), STR16("Custom Time 15"), STR16("Custom Time 16")
    };
    for (int i = 0; i < 16; ++i) {
        // Default: evenly spaced (i+1)/17
        float defaultTime = static_cast<float>(i + 1) / 17.0f;
        parameters.addParameter(
            customTimeNames[i],
            nullptr,
            0,
            defaultTime,
            ParameterInfo::kCanAutomate,
            kMultiTapCustomTime0Id + i);
    }

    // Custom Pattern Levels (966-981) - spec 046
    // 16 parameters for custom tap levels (0.0-1.0 linear gain)
    static const Steinberg::Vst::TChar* customLevelNames[16] = {
        STR16("Custom Level 1"), STR16("Custom Level 2"), STR16("Custom Level 3"), STR16("Custom Level 4"),
        STR16("Custom Level 5"), STR16("Custom Level 6"), STR16("Custom Level 7"), STR16("Custom Level 8"),
        STR16("Custom Level 9"), STR16("Custom Level 10"), STR16("Custom Level 11"), STR16("Custom Level 12"),
        STR16("Custom Level 13"), STR16("Custom Level 14"), STR16("Custom Level 15"), STR16("Custom Level 16")
    };
    for (int i = 0; i < 16; ++i) {
        parameters.addParameter(
            customLevelNames[i],
            nullptr,
            0,
            1.0,  // default: full level
            ParameterInfo::kCanAutomate,
            kMultiTapCustomLevel0Id + i);
    }
}

// ==============================================================================
// Parameter Display Formatting (for Controller)
// ==============================================================================

inline Steinberg::tresult formatMultiTapParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    switch (id) {
        // kMultiTapTimingPatternId: handled by StringListParameter::toString() automatically
        // kMultiTapSpatialPatternId: handled by StringListParameter::toString() automatically
        // kMultiTapNoteValueId: handled by StringListParameter::toString() automatically
        // kMultiTapNoteModifierId: handled by StringListParameter::toString() automatically

        case kMultiTapTapCountId: {
            int count = static_cast<int>(2.0 + normalizedValue * 14.0 + 0.5);
            char8 text[32];
            snprintf(text, sizeof(text), "%d", count);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kMultiTapFeedbackId: {
            float percent = static_cast<float>(normalizedValue * 110.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kMultiTapFeedbackLPCutoffId:
        case kMultiTapFeedbackHPCutoffId: {
            float hz = static_cast<float>(20.0 * std::pow(1000.0, normalizedValue));
            char8 text[32];
            if (hz >= 1000.0f) {
                snprintf(text, sizeof(text), "%.2f kHz", hz / 1000.0f);
            } else {
                snprintf(text, sizeof(text), "%.0f Hz", hz);
            }
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kMultiTapMorphTimeId: {
            float ms = static_cast<float>(50.0 + normalizedValue * 1950.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f ms", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kMultiTapMixId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // Custom Pattern Time Ratios (950-965)
        default:
            if (id >= kMultiTapCustomTime0Id && id <= kMultiTapCustomTime15Id) {
                float percent = static_cast<float>(normalizedValue * 100.0);
                char8 text[32];
                snprintf(text, sizeof(text), "%.0f%%", percent);
                Steinberg::UString(string, 128).fromAscii(text);
                return kResultOk;
            }
            // Custom Pattern Levels (966-981)
            else if (id >= kMultiTapCustomLevel0Id && id <= kMultiTapCustomLevel15Id) {
                float percent = static_cast<float>(normalizedValue * 100.0);
                char8 text[32];
                snprintf(text, sizeof(text), "%.0f%%", percent);
                Steinberg::UString(string, 128).fromAscii(text);
                return kResultOk;
            }
            break;
    }

    return Steinberg::kResultFalse;
}

// ==============================================================================
// State Persistence
// ==============================================================================

inline void saveMultiTapParams(const MultiTapParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    streamer.writeInt32(params.noteModifier.load(std::memory_order_relaxed));
    streamer.writeInt32(params.timingPattern.load(std::memory_order_relaxed));
    streamer.writeInt32(params.spatialPattern.load(std::memory_order_relaxed));
    streamer.writeInt32(params.tapCount.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedbackLPCutoff.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedbackHPCutoff.load(std::memory_order_relaxed));
    streamer.writeFloat(params.morphTime.load(std::memory_order_relaxed));
    streamer.writeFloat(params.dryWet.load(std::memory_order_relaxed));

    // Custom Pattern Data (spec 046)
    for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
        streamer.writeFloat(params.customTimeRatios[i].load(std::memory_order_relaxed));
    }
    for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
        streamer.writeFloat(params.customLevels[i].load(std::memory_order_relaxed));
    }

    // Snap Division (spec 046 - grid snapping)
    streamer.writeInt32(params.snapDivision.load(std::memory_order_relaxed));
}

inline void loadMultiTapParams(MultiTapParams& params, Steinberg::IBStreamer& streamer) {
    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    streamer.readInt32(intVal);
    params.noteValue.store(intVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.noteModifier.store(intVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.timingPattern.store(intVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.spatialPattern.store(intVal, std::memory_order_relaxed);

    streamer.readInt32(intVal);
    params.tapCount.store(intVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.feedback.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.feedbackLPCutoff.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.feedbackHPCutoff.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.morphTime.store(floatVal, std::memory_order_relaxed);

    streamer.readFloat(floatVal);
    params.dryWet.store(floatVal, std::memory_order_relaxed);

    // Custom Pattern Data (spec 046) - read if available for backward compatibility
    // Old presets without custom pattern data will retain defaults
    for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
        if (streamer.readFloat(floatVal)) {
            params.customTimeRatios[i].store(floatVal, std::memory_order_relaxed);
        }
    }
    for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
        if (streamer.readFloat(floatVal)) {
            params.customLevels[i].store(floatVal, std::memory_order_relaxed);
        }
    }

    // Snap Division (spec 046 - grid snapping) - read if available
    if (streamer.readInt32(intVal)) {
        params.snapDivision.store(intVal, std::memory_order_relaxed);
    }
}

// ==============================================================================
// Controller State Sync (from IBStreamer)
// ==============================================================================

// Template function that reads MultiTap params from streamer and calls setParam callback
// SetParamFunc signature: void(Steinberg::Vst::ParamID paramId, double normalizedValue)
template<typename SetParamFunc>
inline void loadMultiTapParamsToController(
    Steinberg::IBStreamer& streamer,
    SetParamFunc setParam)
{
    using namespace Steinberg;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // Note Value: 0-9 -> normalized = val/9
    if (streamer.readInt32(intVal)) {
        setParam(kMultiTapNoteValueId,
            static_cast<double>(intVal) / 9.0);
    }

    // Note Modifier: 0-2 -> normalized = val/2
    if (streamer.readInt32(intVal)) {
        setParam(kMultiTapNoteModifierId,
            static_cast<double>(intVal) / 2.0);
    }

    // Timing Pattern: 0-19 -> normalized = val/19
    if (streamer.readInt32(intVal)) {
        setParam(kMultiTapTimingPatternId,
            static_cast<double>(intVal) / 19.0);
    }

    // Spatial Pattern: 0-6 -> normalized = val/6
    if (streamer.readInt32(intVal)) {
        setParam(kMultiTapSpatialPatternId,
            static_cast<double>(intVal) / 6.0);
    }

    // Tap Count: 2-16 -> normalized = (val-2)/14
    if (streamer.readInt32(intVal)) {
        setParam(kMultiTapTapCountId,
            static_cast<double>(intVal - 2) / 14.0);
    }

    // Feedback: 0-1.1 -> normalized = val/1.1
    if (streamer.readFloat(floatVal)) {
        setParam(kMultiTapFeedbackId,
            static_cast<double>(floatVal / 1.1f));
    }

    // Feedback LP Cutoff: 20-20000Hz (log) -> normalized = log(val/20)/log(1000)
    if (streamer.readFloat(floatVal)) {
        setParam(kMultiTapFeedbackLPCutoffId,
            std::log(floatVal / 20.0f) / std::log(1000.0f));
    }

    // Feedback HP Cutoff: 20-20000Hz (log) -> normalized = log(val/20)/log(1000)
    if (streamer.readFloat(floatVal)) {
        setParam(kMultiTapFeedbackHPCutoffId,
            std::log(floatVal / 20.0f) / std::log(1000.0f));
    }

    // Morph Time: 50-2000ms -> normalized = (val-50)/1950
    if (streamer.readFloat(floatVal)) {
        setParam(kMultiTapMorphTimeId,
            static_cast<double>((floatVal - 50.0f) / 1950.0f));
    }

    // Dry/Wet: 0-1 (already normalized)
    if (streamer.readFloat(floatVal)) {
        setParam(kMultiTapMixId,
            static_cast<double>(floatVal));
    }

    // Custom Pattern Time Ratios (spec 046) - already normalized 0-1
    for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
        if (streamer.readFloat(floatVal)) {
            setParam(kMultiTapCustomTime0Id + static_cast<Steinberg::Vst::ParamID>(i),
                static_cast<double>(floatVal));
        }
    }

    // Custom Pattern Levels (spec 046) - already normalized 0-1
    for (size_t i = 0; i < kCustomPatternMaxTaps; ++i) {
        if (streamer.readFloat(floatVal)) {
            setParam(kMultiTapCustomLevel0Id + static_cast<Steinberg::Vst::ParamID>(i),
                static_cast<double>(floatVal));
        }
    }

    // Snap Division (spec 046): 0-21 -> normalized = val/21
    if (streamer.readInt32(intVal)) {
        setParam(kMultiTapSnapDivisionId,
            static_cast<double>(intVal) / 21.0);
    }
}

// Wrapper that uses the template with EditControllerEx1::setParamNormalized
inline void syncMultiTapParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    loadMultiTapParamsToController(streamer,
        [&controller](Steinberg::Vst::ParamID paramId, double normalizedValue) {
            controller.setParamNormalized(paramId, normalizedValue);
        });
}

} // namespace Iterum
