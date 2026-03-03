// Contract: arpeggiator_params.h
// Location: plugins/ruinae/src/parameters/arpeggiator_params.h
// Pattern: Follows trance_gate_params.h exactly
//
// This file documents the API contract for the ArpeggiatorParams module.
// It is NOT compiled -- it serves as a specification for implementation.

#pragma once

#include "plugin_ids.h"
#include "parameters/note_value_ui.h"
#include "controller/parameter_helpers.h"

#include <krate/dsp/core/note_value.h>

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <atomic>
#include <algorithm>
#include <cstdio>

namespace Ruinae {

// =============================================================================
// ArpeggiatorParams: Atomic parameter storage (FR-004)
// =============================================================================

struct ArpeggiatorParams {
    std::atomic<bool>  enabled{false};
    std::atomic<int>   mode{0};              // 0=Up..9=Chord
    std::atomic<int>   octaveRange{1};       // 1-4
    std::atomic<int>   octaveMode{0};        // 0=Sequential, 1=Interleaved
    std::atomic<bool>  tempoSync{true};
    std::atomic<int>   noteValue{Parameters::kNoteValueDefaultIndex}; // 0-20
    std::atomic<float> freeRate{4.0f};       // 0.5-50 Hz
    std::atomic<float> gateLength{80.0f};    // 1-200%
    std::atomic<float> swing{0.0f};          // 0-75%
    std::atomic<int>   latchMode{0};         // 0=Off, 1=Hold, 2=Add
    std::atomic<int>   retrigger{0};         // 0=Off, 1=Note, 2=Beat
};

// =============================================================================
// handleArpParamChange: Denormalize VST 0-1 -> plain values (FR-005)
// =============================================================================
// Called on audio thread from processParameterChanges()
// Must match ranges in registerArpParams() exactly

inline void handleArpParamChange(
    ArpeggiatorParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value);

// =============================================================================
// registerArpParams: Register 11 parameters with host (FR-002)
// =============================================================================
// Called on UI thread from Controller::initialize()
// All parameters have kCanAutomate flag

inline void registerArpParams(
    Steinberg::Vst::ParameterContainer& parameters);

// =============================================================================
// formatArpParam: Human-readable value display (FR-003)
// =============================================================================
// Called from Controller::getParamStringByValue()

inline Steinberg::tresult formatArpParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string);

// =============================================================================
// saveArpParams: Serialize to stream (FR-012)
// =============================================================================

inline void saveArpParams(
    const ArpeggiatorParams& params,
    Steinberg::IBStreamer& streamer);

// =============================================================================
// loadArpParams: Deserialize from stream (FR-012)
// =============================================================================

inline bool loadArpParams(
    ArpeggiatorParams& params,
    Steinberg::IBStreamer& streamer);

// =============================================================================
// loadArpParamsToController: Controller-side state restore (FR-012)
// =============================================================================

template<typename SetParamFunc>
inline void loadArpParamsToController(
    Steinberg::IBStreamer& streamer,
    SetParamFunc setParam);

} // namespace Ruinae
