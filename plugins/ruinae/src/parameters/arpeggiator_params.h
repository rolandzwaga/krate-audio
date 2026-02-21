#pragma once

// ==============================================================================
// ArpeggiatorParams: Atomic parameter storage for the arpeggiator (FR-004)
// ==============================================================================
// Follows trance_gate_params.h pattern exactly.
// Struct + 6 inline functions for parameter handling.
// ==============================================================================

#include "plugin_ids.h"
#include "parameters/note_value_ui.h"
#include "controller/parameter_helpers.h"

#include <krate/dsp/core/note_value.h>

#include <cmath>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>

namespace Ruinae {

// =============================================================================
// ArpeggiatorParams: Atomic parameter storage (FR-004)
// =============================================================================
// Thread-safe bridge between UI/host thread (writes normalized values via
// processParameterChanges) and the audio thread (reads plain values in
// applyParamsToEngine).

struct ArpeggiatorParams {
    // Base arp params (Phase 3)
    std::atomic<bool>  enabled{false};
    std::atomic<int>   mode{0};              // 0=Up..9=Chord
    std::atomic<int>   octaveRange{1};       // 1-4
    std::atomic<int>   octaveMode{0};        // 0=Sequential, 1=Interleaved
    std::atomic<bool>  tempoSync{true};
    std::atomic<int>   noteValue{Parameters::kNoteValueDefaultIndex}; // index 10 = 1/8 note
    std::atomic<float> freeRate{4.0f};       // 0.5-50 Hz
    std::atomic<float> gateLength{80.0f};    // 1-200%
    std::atomic<float> swing{0.0f};          // 0-75%
    std::atomic<int>   latchMode{0};         // 0=Off, 1=Hold, 2=Add
    std::atomic<int>   retrigger{0};         // 0=Off, 1=Note, 2=Beat

    // Velocity lane (072-independent-lanes, US1)
    std::atomic<int>   velocityLaneLength{1};   // 1-32
    std::array<std::atomic<float>, 32> velocityLaneSteps{};

    // Gate lane (072-independent-lanes, US2)
    std::atomic<int>   gateLaneLength{1};       // 1-32
    std::array<std::atomic<float>, 32> gateLaneSteps{};

    ArpeggiatorParams() {
        for (auto& step : velocityLaneSteps) {
            step.store(1.0f, std::memory_order_relaxed);
        }
        for (auto& step : gateLaneSteps) {
            step.store(1.0f, std::memory_order_relaxed);
        }
    }
};

// =============================================================================
// handleArpParamChange: Denormalize VST 0-1 -> plain values (FR-005)
// =============================================================================
// Called on audio thread from processParameterChanges().
// Must match ranges in registerArpParams() exactly.

inline void handleArpParamChange(
    ArpeggiatorParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value)
{
    switch (id) {
        case kArpEnabledId:
            params.enabled.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kArpModeId:
            // StringListParameter: 0-1 -> 0-9 (10 entries, stepCount=9)
            params.mode.store(
                std::clamp(static_cast<int>(value * 9.0 + 0.5), 0, 9),
                std::memory_order_relaxed);
            break;
        case kArpOctaveRangeId:
            // RangeParameter: 0-1 -> 1-4 (stepCount=3)
            params.octaveRange.store(
                std::clamp(static_cast<int>(1.0 + std::round(value * 3.0)), 1, 4),
                std::memory_order_relaxed);
            break;
        case kArpOctaveModeId:
            // StringListParameter: 0-1 -> 0-1 (2 entries, stepCount=1)
            params.octaveMode.store(
                std::clamp(static_cast<int>(value * 1.0 + 0.5), 0, 1),
                std::memory_order_relaxed);
            break;
        case kArpTempoSyncId:
            params.tempoSync.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kArpNoteValueId:
            // StringListParameter: 0-1 -> 0-20 (21 entries, stepCount=20)
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed);
            break;
        case kArpFreeRateId:
            // Continuous Parameter: 0-1 -> 0.5-50 Hz
            params.freeRate.store(
                std::clamp(static_cast<float>(0.5 + value * 49.5), 0.5f, 50.0f),
                std::memory_order_relaxed);
            break;
        case kArpGateLengthId:
            // Continuous Parameter: 0-1 -> 1-200%
            params.gateLength.store(
                std::clamp(static_cast<float>(1.0 + value * 199.0), 1.0f, 200.0f),
                std::memory_order_relaxed);
            break;
        case kArpSwingId:
            // Continuous Parameter: 0-1 -> 0-75%
            params.swing.store(
                std::clamp(static_cast<float>(value * 75.0), 0.0f, 75.0f),
                std::memory_order_relaxed);
            break;
        case kArpLatchModeId:
            // StringListParameter: 0-1 -> 0-2 (3 entries, stepCount=2)
            params.latchMode.store(
                std::clamp(static_cast<int>(value * 2.0 + 0.5), 0, 2),
                std::memory_order_relaxed);
            break;
        case kArpRetriggerId:
            // StringListParameter: 0-1 -> 0-2 (3 entries, stepCount=2)
            params.retrigger.store(
                std::clamp(static_cast<int>(value * 2.0 + 0.5), 0, 2),
                std::memory_order_relaxed);
            break;

        // --- Velocity Lane (072-independent-lanes, US1) ---
        case kArpVelocityLaneLengthId:
            // RangeParameter: 0-1 -> 1-32 (stepCount=31)
            params.velocityLaneLength.store(
                std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32),
                std::memory_order_relaxed);
            break;

        // --- Gate Lane (072-independent-lanes, US2) ---
        case kArpGateLaneLengthId:
            // RangeParameter: 0-1 -> 1-32 (stepCount=31)
            params.gateLaneLength.store(
                std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32),
                std::memory_order_relaxed);
            break;

        default:
            // Velocity lane steps: 3021-3052
            if (id >= kArpVelocityLaneStep0Id && id <= kArpVelocityLaneStep31Id) {
                float vel = std::clamp(static_cast<float>(value), 0.0f, 1.0f);
                params.velocityLaneSteps[id - kArpVelocityLaneStep0Id].store(
                    vel, std::memory_order_relaxed);
            }
            // Gate lane steps: 3061-3092
            else if (id >= kArpGateLaneStep0Id && id <= kArpGateLaneStep31Id) {
                float gate = std::clamp(static_cast<float>(0.01 + value * 1.99), 0.01f, 2.0f);
                params.gateLaneSteps[id - kArpGateLaneStep0Id].store(
                    gate, std::memory_order_relaxed);
            }
            break;
    }
}

// =============================================================================
// registerArpParams: Register 11 parameters with host (FR-002)
// =============================================================================
// Called on UI thread from Controller::initialize().
// All parameters have kCanAutomate flag.

inline void registerArpParams(
    Steinberg::Vst::ParameterContainer& parameters)
{
    using namespace Steinberg::Vst;

    // Arp Enabled: Toggle (0 or 1), default off
    parameters.addParameter(STR16("Arp Enabled"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kArpEnabledId);

    // Arp Mode: StringListParameter (10 entries), default 0 (Up)
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Mode"), kArpModeId,
        {STR16("Up"), STR16("Down"), STR16("UpDown"), STR16("DownUp"),
         STR16("Converge"), STR16("Diverge"), STR16("Random"),
         STR16("Walk"), STR16("AsPlayed"), STR16("Chord")}));

    // Arp Octave Range: RangeParameter 1-4, default 1, stepCount 3
    parameters.addParameter(
        new RangeParameter(STR16("Arp Octave Range"), kArpOctaveRangeId,
                          STR16(""), 1, 4, 1, 3,
                          ParameterInfo::kCanAutomate));

    // Arp Octave Mode: StringListParameter (2 entries), default 0 (Sequential)
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Octave Mode"), kArpOctaveModeId,
        {STR16("Sequential"), STR16("Interleaved")}));

    // Arp Tempo Sync: Toggle (0 or 1), default on
    parameters.addParameter(STR16("Arp Tempo Sync"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kArpTempoSyncId);

    // Arp Note Value: StringListParameter (21 entries), default index 10 (1/8)
    parameters.addParameter(createNoteValueDropdown(
        STR16("Arp Note Value"), kArpNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex));

    // Arp Free Rate: Continuous 0-1, default maps to 4.0 Hz
    // Normalized default: (4.0 - 0.5) / 49.5 = 3.5 / 49.5 ~= 0.0707
    parameters.addParameter(STR16("Arp Free Rate"), STR16("Hz"), 0,
        static_cast<double>((4.0f - 0.5f) / 49.5f),
        ParameterInfo::kCanAutomate, kArpFreeRateId);

    // Arp Gate Length: Continuous 0-1, default maps to 80%
    // Normalized default: (80.0 - 1.0) / 199.0 = 79.0 / 199.0 ~= 0.3970
    parameters.addParameter(STR16("Arp Gate Length"), STR16("%"), 0,
        static_cast<double>((80.0f - 1.0f) / 199.0f),
        ParameterInfo::kCanAutomate, kArpGateLengthId);

    // Arp Swing: Continuous 0-1, default maps to 0%
    // Normalized default: 0.0 / 75.0 = 0.0
    parameters.addParameter(STR16("Arp Swing"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kArpSwingId);

    // Arp Latch Mode: StringListParameter (3 entries), default 0 (Off)
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Latch Mode"), kArpLatchModeId,
        {STR16("Off"), STR16("Hold"), STR16("Add")}));

    // Arp Retrigger: StringListParameter (3 entries), default 0 (Off)
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Retrigger"), kArpRetriggerId,
        {STR16("Off"), STR16("Note"), STR16("Beat")}));

    // --- Velocity Lane (072-independent-lanes, US1) ---

    // Velocity lane length: RangeParameter 1-32, default 1, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Vel Lane Len"), kArpVelocityLaneLengthId,
                          STR16(""), 1, 32, 1, 31,
                          ParameterInfo::kCanAutomate));

    // Velocity lane steps: loop 0-31, RangeParameter 0.0-1.0, default 1.0
    for (int i = 0; i < 32; ++i) {
        char name[48];
        snprintf(name, sizeof(name), "Arp Vel Step %d", i + 1);
        Steinberg::Vst::String128 name16;
        Steinberg::UString(name16, 128).fromAscii(name);
        parameters.addParameter(
            new RangeParameter(name16,
                static_cast<ParamID>(kArpVelocityLaneStep0Id + i),
                STR16(""), 0.0, 1.0, 1.0, 0,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
    }

    // --- Gate Lane (072-independent-lanes, US2) ---

    // Gate lane length: RangeParameter 1-32, default 1, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Gate Lane Len"), kArpGateLaneLengthId,
                          STR16(""), 1, 32, 1, 31,
                          ParameterInfo::kCanAutomate));

    // Gate lane steps: loop 0-31, RangeParameter 0.01-2.0, default 1.0
    for (int i = 0; i < 32; ++i) {
        char name[48];
        snprintf(name, sizeof(name), "Arp Gate Step %d", i + 1);
        Steinberg::Vst::String128 name16;
        Steinberg::UString(name16, 128).fromAscii(name);
        parameters.addParameter(
            new RangeParameter(name16,
                static_cast<ParamID>(kArpGateLaneStep0Id + i),
                STR16(""), 0.01, 2.0, 1.0, 0,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
    }
}

// =============================================================================
// formatArpParam: Human-readable value display (FR-003)
// =============================================================================
// Called from Controller::getParamStringByValue().

inline Steinberg::tresult formatArpParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string)
{
    using namespace Steinberg;
    switch (id) {
        case kArpModeId: {
            static const char* const kModeNames[] = {
                "Up", "Down", "UpDown", "DownUp", "Converge",
                "Diverge", "Random", "Walk", "AsPlayed", "Chord"
            };
            int idx = std::clamp(static_cast<int>(value * 9.0 + 0.5), 0, 9);
            UString(string, 128).fromAscii(kModeNames[idx]);
            return kResultOk;
        }
        case kArpOctaveRangeId: {
            char8 text[32];
            int range = std::clamp(static_cast<int>(1.0 + std::round(value * 3.0)), 1, 4);
            snprintf(text, sizeof(text), "%d", range);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpOctaveModeId: {
            static const char* const kOctModeNames[] = {"Sequential", "Interleaved"};
            int idx = std::clamp(static_cast<int>(value * 1.0 + 0.5), 0, 1);
            UString(string, 128).fromAscii(kOctModeNames[idx]);
            return kResultOk;
        }
        case kArpNoteValueId: {
            // Map normalized -> dropdown index, then display same string as TG
            static const char* const kNoteNames[] = {
                "1/64T", "1/64", "1/64D",
                "1/32T", "1/32", "1/32D",
                "1/16T", "1/16", "1/16D",
                "1/8T",  "1/8",  "1/8D",
                "1/4T",  "1/4",  "1/4D",
                "1/2T",  "1/2",  "1/2D",
                "1/1T",  "1/1",  "1/1D",
            };
            int idx = std::clamp(
                static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                0, Parameters::kNoteValueDropdownCount - 1);
            UString(string, 128).fromAscii(kNoteNames[idx]);
            return kResultOk;
        }
        case kArpFreeRateId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f Hz", 0.5 + value * 49.5);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpGateLengthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", 1.0 + value * 199.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpSwingId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 75.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpLatchModeId: {
            static const char* const kLatchNames[] = {"Off", "Hold", "Add"};
            int idx = std::clamp(static_cast<int>(value * 2.0 + 0.5), 0, 2);
            UString(string, 128).fromAscii(kLatchNames[idx]);
            return kResultOk;
        }
        case kArpRetriggerId: {
            static const char* const kRetrigNames[] = {"Off", "Note", "Beat"};
            int idx = std::clamp(static_cast<int>(value * 2.0 + 0.5), 0, 2);
            UString(string, 128).fromAscii(kRetrigNames[idx]);
            return kResultOk;
        }

        // --- Velocity Lane (072-independent-lanes, US1) ---
        case kArpVelocityLaneLengthId: {
            char8 text[32];
            int len = std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            snprintf(text, sizeof(text), "%d steps", len);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // --- Gate Lane (072-independent-lanes, US2) ---
        case kArpGateLaneLengthId: {
            char8 text[32];
            int len = std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            snprintf(text, sizeof(text), "%d steps", len);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        default:
            // Velocity lane steps: display as percentage
            if (id >= kArpVelocityLaneStep0Id && id <= kArpVelocityLaneStep31Id) {
                char8 text[32];
                snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
                UString(string, 128).fromAscii(text);
                return kResultOk;
            }
            // Gate lane steps: display as multiplier
            if (id >= kArpGateLaneStep0Id && id <= kArpGateLaneStep31Id) {
                char8 text[32];
                double gateVal = 0.01 + value * 1.99;
                snprintf(text, sizeof(text), "%.2fx", gateVal);
                UString(string, 128).fromAscii(text);
                return kResultOk;
            }
            break;
    }
    return kResultFalse;
}

// =============================================================================
// saveArpParams: Serialize to stream (FR-012)
// =============================================================================

inline void saveArpParams(
    const ArpeggiatorParams& params,
    Steinberg::IBStreamer& streamer)
{
    // 11 fields in order: enabled, mode, octaveRange, octaveMode, tempoSync,
    // noteValue (all int32), freeRate, gateLength, swing (all float),
    // latchMode, retrigger (both int32)
    streamer.writeInt32(params.enabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.mode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.octaveRange.load(std::memory_order_relaxed));
    streamer.writeInt32(params.octaveMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.tempoSync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    streamer.writeFloat(params.freeRate.load(std::memory_order_relaxed));
    streamer.writeFloat(params.gateLength.load(std::memory_order_relaxed));
    streamer.writeFloat(params.swing.load(std::memory_order_relaxed));
    streamer.writeInt32(params.latchMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.retrigger.load(std::memory_order_relaxed));

    // --- Velocity Lane (072-independent-lanes, US1) ---
    streamer.writeInt32(params.velocityLaneLength.load(std::memory_order_relaxed));
    for (int i = 0; i < 32; ++i) {
        streamer.writeFloat(params.velocityLaneSteps[i].load(std::memory_order_relaxed));
    }

    // --- Gate Lane (072-independent-lanes, US2) ---
    streamer.writeInt32(params.gateLaneLength.load(std::memory_order_relaxed));
    for (int i = 0; i < 32; ++i) {
        streamer.writeFloat(params.gateLaneSteps[i].load(std::memory_order_relaxed));
    }
}

// =============================================================================
// loadArpParams: Deserialize from stream (FR-012)
// =============================================================================
// Returns false without corrupting state when stream ends early
// (backward compatibility with old presets).

inline bool loadArpParams(
    ArpeggiatorParams& params,
    Steinberg::IBStreamer& streamer)
{
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    if (!streamer.readInt32(intVal)) return false;
    params.enabled.store(intVal != 0, std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.mode.store(std::clamp(intVal, 0, 9), std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.octaveRange.store(std::clamp(intVal, 1, 4), std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.octaveMode.store(std::clamp(intVal, 0, 1), std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.tempoSync.store(intVal != 0, std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.noteValue.store(std::clamp(intVal, 0, Parameters::kNoteValueDropdownCount - 1),
        std::memory_order_relaxed);

    if (!streamer.readFloat(floatVal)) return false;
    params.freeRate.store(std::clamp(floatVal, 0.5f, 50.0f), std::memory_order_relaxed);

    if (!streamer.readFloat(floatVal)) return false;
    params.gateLength.store(std::clamp(floatVal, 1.0f, 200.0f), std::memory_order_relaxed);

    if (!streamer.readFloat(floatVal)) return false;
    params.swing.store(std::clamp(floatVal, 0.0f, 75.0f), std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.latchMode.store(std::clamp(intVal, 0, 2), std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.retrigger.store(std::clamp(intVal, 0, 2), std::memory_order_relaxed);

    // --- Velocity Lane (072-independent-lanes, US1) ---
    // EOF-safe: if lane data is missing (Phase 3 preset), keep defaults
    if (!streamer.readInt32(intVal)) return true;  // base params loaded OK
    params.velocityLaneLength.store(std::clamp(intVal, 1, 32), std::memory_order_relaxed);

    for (int i = 0; i < 32; ++i) {
        if (!streamer.readFloat(floatVal)) return false;
        params.velocityLaneSteps[i].store(
            std::clamp(floatVal, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    // --- Gate Lane (072-independent-lanes, US2) ---
    // EOF-safe: if gate lane data is missing (pre-US2 preset), keep defaults
    if (!streamer.readInt32(intVal)) return true;  // velocity lane loaded OK
    params.gateLaneLength.store(std::clamp(intVal, 1, 32), std::memory_order_relaxed);

    for (int i = 0; i < 32; ++i) {
        if (!streamer.readFloat(floatVal)) return false;
        params.gateLaneSteps[i].store(
            std::clamp(floatVal, 0.01f, 2.0f), std::memory_order_relaxed);
    }

    return true;
}

// =============================================================================
// loadArpParamsToController: Controller-side state restore (FR-012)
// =============================================================================

template<typename SetParamFunc>
inline void loadArpParamsToController(
    Steinberg::IBStreamer& streamer,
    SetParamFunc setParam)
{
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    // enabled (int32 -> bool -> normalized 0 or 1)
    if (streamer.readInt32(intVal))
        setParam(kArpEnabledId, intVal != 0 ? 1.0 : 0.0);
    else return;

    // mode (int32 -> normalized: index / 9)
    if (streamer.readInt32(intVal))
        setParam(kArpModeId, static_cast<double>(std::clamp(intVal, 0, 9)) / 9.0);
    else return;

    // octaveRange (int32 -> normalized: (range - 1) / 3)
    if (streamer.readInt32(intVal))
        setParam(kArpOctaveRangeId, static_cast<double>(std::clamp(intVal, 1, 4) - 1) / 3.0);
    else return;

    // octaveMode (int32 -> normalized: index / 1)
    if (streamer.readInt32(intVal))
        setParam(kArpOctaveModeId, static_cast<double>(std::clamp(intVal, 0, 1)));
    else return;

    // tempoSync (int32 -> bool -> normalized 0 or 1)
    if (streamer.readInt32(intVal))
        setParam(kArpTempoSyncId, intVal != 0 ? 1.0 : 0.0);
    else return;

    // noteValue (int32 -> normalized: index / 20)
    if (streamer.readInt32(intVal))
        setParam(kArpNoteValueId,
            static_cast<double>(std::clamp(intVal, 0, Parameters::kNoteValueDropdownCount - 1))
                / (Parameters::kNoteValueDropdownCount - 1));
    else return;

    // freeRate (float -> normalized: (rate - 0.5) / 49.5)
    if (streamer.readFloat(floatVal))
        setParam(kArpFreeRateId,
            static_cast<double>((std::clamp(floatVal, 0.5f, 50.0f) - 0.5f) / 49.5f));
    else return;

    // gateLength (float -> normalized: (len - 1) / 199)
    if (streamer.readFloat(floatVal))
        setParam(kArpGateLengthId,
            static_cast<double>((std::clamp(floatVal, 1.0f, 200.0f) - 1.0f) / 199.0f));
    else return;

    // swing (float -> normalized: swing / 75)
    if (streamer.readFloat(floatVal))
        setParam(kArpSwingId,
            static_cast<double>(std::clamp(floatVal, 0.0f, 75.0f) / 75.0f));
    else return;

    // latchMode (int32 -> normalized: index / 2)
    if (streamer.readInt32(intVal))
        setParam(kArpLatchModeId, static_cast<double>(std::clamp(intVal, 0, 2)) / 2.0);
    else return;

    // retrigger (int32 -> normalized: index / 2)
    if (streamer.readInt32(intVal))
        setParam(kArpRetriggerId, static_cast<double>(std::clamp(intVal, 0, 2)) / 2.0);
}

} // namespace Ruinae
