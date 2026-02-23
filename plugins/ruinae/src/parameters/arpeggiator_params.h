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

    // Pitch lane (072-independent-lanes, US3)
    std::atomic<int>   pitchLaneLength{1};      // 1-32
    std::array<std::atomic<int>, 32> pitchLaneSteps{};  // -24 to +24 (int for lock-free guarantee)

    // --- Modifier Lane (073-per-step-mods) ---
    std::atomic<int>   modifierLaneLength{1};      // 1-32
    std::array<std::atomic<int>, 32> modifierLaneSteps{};  // uint8_t bitmask stored as int (lock-free)

    // Modifier configuration
    std::atomic<int>   accentVelocity{30};         // 0-127
    std::atomic<float> slideTime{60.0f};           // 0-500 ms

    // --- Ratchet Lane (074-ratcheting) ---
    std::atomic<int>   ratchetLaneLength{1};       // 1-32
    std::array<std::atomic<int>, 32> ratchetLaneSteps{};  // 1-4 (int for lock-free guarantee)

    // --- Euclidean Timing (075-euclidean-timing) ---
    std::atomic<bool> euclideanEnabled{false};    // default off
    std::atomic<int>  euclideanHits{4};           // default 4
    std::atomic<int>  euclideanSteps{8};          // default 8
    std::atomic<int>  euclideanRotation{0};       // default 0

    // --- Condition Lane (076-conditional-trigs) ---
    std::atomic<int>   conditionLaneLength{1};       // 1-32
    std::array<std::atomic<int>, 32> conditionLaneSteps{};  // 0-17 (TrigCondition, int for lock-free)
    std::atomic<bool>  fillToggle{false};            // Fill mode toggle

    // --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
    std::atomic<float> spice{0.0f};
    std::atomic<bool>  diceTrigger{false};
    std::atomic<float> humanize{0.0f};

    ArpeggiatorParams() {
        for (auto& step : velocityLaneSteps) {
            step.store(1.0f, std::memory_order_relaxed);
        }
        for (auto& step : gateLaneSteps) {
            step.store(1.0f, std::memory_order_relaxed);
        }
        // pitchLaneSteps default to 0 via value-initialization -- correct identity for pitch
        // modifierLaneSteps default to 1 (kStepActive) -- active, no modifiers
        for (auto& step : modifierLaneSteps) {
            step.store(1, std::memory_order_relaxed);  // kStepActive = 0x01
        }
        // ratchetLaneSteps default to 1 (no ratcheting) -- 074-ratcheting (FR-031)
        for (auto& step : ratchetLaneSteps) {
            step.store(1, std::memory_order_relaxed);
        }
        // conditionLaneSteps default to 0 (TrigCondition::Always) via value-initialization -- correct
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

        // --- Pitch Lane (072-independent-lanes, US3) ---
        case kArpPitchLaneLengthId:
            // RangeParameter: 0-1 -> 1-32 (stepCount=31)
            params.pitchLaneLength.store(
                std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32),
                std::memory_order_relaxed);
            break;

        // --- Modifier Lane (073-per-step-mods) ---
        case kArpModifierLaneLengthId:
            // RangeParameter: 0-1 -> 1-32 (stepCount=31)
            params.modifierLaneLength.store(
                std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32),
                std::memory_order_relaxed);
            break;

        case kArpAccentVelocityId:
            // RangeParameter: 0-1 -> 0-127 (stepCount=127)
            params.accentVelocity.store(
                std::clamp(static_cast<int>(std::round(value * 127.0)), 0, 127),
                std::memory_order_relaxed);
            break;

        case kArpSlideTimeId:
            // Continuous Parameter: 0-1 -> 0-500ms
            params.slideTime.store(
                std::clamp(static_cast<float>(value * 500.0), 0.0f, 500.0f),
                std::memory_order_relaxed);
            break;

        // --- Ratchet Lane (074-ratcheting) ---
        case kArpRatchetLaneLengthId:
            // RangeParameter: 0-1 -> 1-32 (stepCount=31)
            params.ratchetLaneLength.store(
                std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32),
                std::memory_order_relaxed);
            break;

        // --- Euclidean Timing (075-euclidean-timing) ---
        case kArpEuclideanEnabledId:
            params.euclideanEnabled.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kArpEuclideanHitsId:
            // RangeParameter: 0-1 -> 0-32 (stepCount=32)
            params.euclideanHits.store(
                std::clamp(static_cast<int>(std::round(value * 32.0)), 0, 32),
                std::memory_order_relaxed);
            break;
        case kArpEuclideanStepsId:
            // RangeParameter: 0-1 -> 2-32 (stepCount=30)
            params.euclideanSteps.store(
                std::clamp(static_cast<int>(2.0 + std::round(value * 30.0)), 2, 32),
                std::memory_order_relaxed);
            break;
        case kArpEuclideanRotationId:
            // RangeParameter: 0-1 -> 0-31 (stepCount=31)
            params.euclideanRotation.store(
                std::clamp(static_cast<int>(std::round(value * 31.0)), 0, 31),
                std::memory_order_relaxed);
            break;

        // --- Condition Lane (076-conditional-trigs) ---
        case kArpConditionLaneLengthId:
            // RangeParameter: 0-1 -> 1-32 (stepCount=31)
            params.conditionLaneLength.store(
                std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32),
                std::memory_order_relaxed);
            break;
        case kArpFillToggleId:
            params.fillToggle.store(value >= 0.5, std::memory_order_relaxed);
            break;

        // --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
        case kArpSpiceId:
            params.spice.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kArpDiceTriggerId:
            // Discrete 2-step: set to true on rising edge (normalized >= 0.5)
            if (value >= 0.5) {
                params.diceTrigger.store(true, std::memory_order_relaxed);
            }
            break;
        case kArpHumanizeId:
            params.humanize.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
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
            // Pitch lane steps: 3101-3132
            else if (id >= kArpPitchLaneStep0Id && id <= kArpPitchLaneStep31Id) {
                int pitch = std::clamp(
                    static_cast<int>(-24.0 + std::round(value * 48.0)), -24, 24);
                params.pitchLaneSteps[id - kArpPitchLaneStep0Id].store(
                    pitch, std::memory_order_relaxed);
            }
            // Modifier lane steps: 3141-3172
            else if (id >= kArpModifierLaneStep0Id && id <= kArpModifierLaneStep31Id) {
                int step = std::clamp(
                    static_cast<int>(std::round(value * 255.0)), 0, 255);
                params.modifierLaneSteps[id - kArpModifierLaneStep0Id].store(
                    step, std::memory_order_relaxed);
            }
            // Ratchet lane steps: 3191-3222 (074-ratcheting)
            else if (id >= kArpRatchetLaneStep0Id && id <= kArpRatchetLaneStep31Id) {
                int ratchet = std::clamp(
                    static_cast<int>(1.0 + std::round(value * 3.0)), 1, 4);
                params.ratchetLaneSteps[id - kArpRatchetLaneStep0Id].store(
                    ratchet, std::memory_order_relaxed);
            }
            // Condition lane steps: 3241-3272 (076-conditional-trigs)
            else if (id >= kArpConditionLaneStep0Id && id <= kArpConditionLaneStep31Id) {
                int step = std::clamp(
                    static_cast<int>(std::round(value * 17.0)), 0, 17);
                params.conditionLaneSteps[id - kArpConditionLaneStep0Id].store(
                    step, std::memory_order_relaxed);
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

    // --- Pitch Lane (072-independent-lanes, US3) ---

    // Pitch lane length: RangeParameter 1-32, default 1, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Pitch Lane Len"), kArpPitchLaneLengthId,
                          STR16(""), 1, 32, 1, 31,
                          ParameterInfo::kCanAutomate));

    // Pitch lane steps: loop 0-31, RangeParameter -24 to +24, default 0, stepCount 48
    for (int i = 0; i < 32; ++i) {
        char name[48];
        snprintf(name, sizeof(name), "Arp Pitch Step %d", i + 1);
        Steinberg::Vst::String128 name16;
        Steinberg::UString(name16, 128).fromAscii(name);
        parameters.addParameter(
            new RangeParameter(name16,
                static_cast<ParamID>(kArpPitchLaneStep0Id + i),
                STR16("st"), -24, 24, 0, 48,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
    }

    // --- Modifier Lane (073-per-step-mods) ---

    // Modifier lane length: RangeParameter 1-32, default 1, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Mod Lane Len"), kArpModifierLaneLengthId,
                          STR16(""), 1, 32, 1, 31,
                          ParameterInfo::kCanAutomate));

    // Modifier lane steps: loop 0-31, RangeParameter 0-255, default 1 (kStepActive), stepCount 255
    for (int i = 0; i < 32; ++i) {
        char name[48];
        snprintf(name, sizeof(name), "Arp Mod Step %d", i + 1);
        Steinberg::Vst::String128 name16;
        Steinberg::UString(name16, 128).fromAscii(name);
        parameters.addParameter(
            new RangeParameter(name16,
                static_cast<ParamID>(kArpModifierLaneStep0Id + i),
                STR16(""), 0, 255, 1, 255,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
    }

    // Accent velocity: RangeParameter 0-127, default 30, stepCount 127
    parameters.addParameter(
        new RangeParameter(STR16("Arp Accent Vel"), kArpAccentVelocityId,
                          STR16(""), 0, 127, 30, 127,
                          ParameterInfo::kCanAutomate));

    // Slide time: Continuous Parameter 0-1, default 0.12 (maps to 60ms)
    parameters.addParameter(STR16("Arp Slide Time"), STR16("ms"), 0,
        0.12,
        ParameterInfo::kCanAutomate, kArpSlideTimeId);

    // --- Ratchet Lane (074-ratcheting) ---

    // Ratchet lane length: RangeParameter 1-32, default 1, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Ratchet Lane Len"), kArpRatchetLaneLengthId,
                          STR16(""), 1, 32, 1, 31,
                          ParameterInfo::kCanAutomate));

    // Ratchet lane steps: loop 0-31, RangeParameter 1-4, default 1, stepCount 3
    for (int i = 0; i < 32; ++i) {
        char name[48];
        snprintf(name, sizeof(name), "Arp Ratchet Step %d", i + 1);
        Steinberg::Vst::String128 name16;
        Steinberg::UString(name16, 128).fromAscii(name);
        parameters.addParameter(
            new RangeParameter(name16,
                static_cast<ParamID>(kArpRatchetLaneStep0Id + i),
                STR16(""), 1, 4, 1, 3,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
    }

    // --- Euclidean Timing (075-euclidean-timing) ---

    // Euclidean enabled: Toggle (0 or 1), default off
    parameters.addParameter(STR16("Arp Euclidean"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kArpEuclideanEnabledId);

    // Euclidean hits: RangeParameter 0-32, default 4, stepCount 32
    parameters.addParameter(
        new RangeParameter(STR16("Arp Euclidean Hits"), kArpEuclideanHitsId,
                          STR16(""), 0, 32, 4, 32,
                          ParameterInfo::kCanAutomate));

    // Euclidean steps: RangeParameter 2-32, default 8, stepCount 30
    parameters.addParameter(
        new RangeParameter(STR16("Arp Euclidean Steps"), kArpEuclideanStepsId,
                          STR16(""), 2, 32, 8, 30,
                          ParameterInfo::kCanAutomate));

    // Euclidean rotation: RangeParameter 0-31, default 0, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Euclidean Rotation"), kArpEuclideanRotationId,
                          STR16(""), 0, 31, 0, 31,
                          ParameterInfo::kCanAutomate));

    // --- Condition Lane (076-conditional-trigs) ---

    // Condition lane length: RangeParameter 1-32, default 1, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Cond Lane Len"), kArpConditionLaneLengthId,
                          STR16(""), 1, 32, 1, 31,
                          ParameterInfo::kCanAutomate));

    // Condition lane steps: loop 0-31, RangeParameter 0-17, default 0 (Always), stepCount 17
    for (int i = 0; i < 32; ++i) {
        char name[48];
        snprintf(name, sizeof(name), "Arp Cond Step %d", i + 1);
        Steinberg::Vst::String128 name16;
        Steinberg::UString(name16, 128).fromAscii(name);
        parameters.addParameter(
            new RangeParameter(name16,
                static_cast<ParamID>(kArpConditionLaneStep0Id + i),
                STR16(""), 0, 17, 0, 17,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
    }

    // Fill toggle: Toggle (0 or 1), default off
    parameters.addParameter(STR16("Arp Fill"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kArpFillToggleId);

    // --- Spice/Dice & Humanize (077-spice-dice-humanize) ---

    // Spice amount: Continuous 0-1, default 0.0 (0%)
    parameters.addParameter(STR16("Arp Spice"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kArpSpiceId);

    // Dice trigger: Discrete 2-step (0 = idle, 1 = trigger), default 0
    parameters.addParameter(STR16("Arp Dice"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kArpDiceTriggerId);

    // Humanize amount: Continuous 0-1, default 0.0 (0%)
    parameters.addParameter(STR16("Arp Humanize"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kArpHumanizeId);
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

        // --- Pitch Lane (072-independent-lanes, US3) ---
        case kArpPitchLaneLengthId: {
            char8 text[32];
            int len = std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            snprintf(text, sizeof(text), "%d steps", len);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // --- Modifier Lane (073-per-step-mods) ---
        case kArpModifierLaneLengthId: {
            char8 text[32];
            int len = std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            snprintf(text, sizeof(text), "%d steps", len);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpAccentVelocityId: {
            char8 text[32];
            int vel = std::clamp(static_cast<int>(std::round(value * 127.0)), 0, 127);
            snprintf(text, sizeof(text), "%d", vel);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpSlideTimeId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f ms", value * 500.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // --- Ratchet Lane (074-ratcheting) ---
        case kArpRatchetLaneLengthId: {
            char8 text[32];
            int len = std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            snprintf(text, sizeof(text), "%d steps", len);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // --- Euclidean Timing (075-euclidean-timing) ---
        case kArpEuclideanEnabledId: {
            UString(string, 128).fromAscii(value >= 0.5 ? "On" : "Off");
            return kResultOk;
        }
        case kArpEuclideanHitsId: {
            char8 text[32];
            int hits = std::clamp(static_cast<int>(std::round(value * 32.0)), 0, 32);
            snprintf(text, sizeof(text), "%d hits", hits);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpEuclideanStepsId: {
            char8 text[32];
            int steps = std::clamp(static_cast<int>(2.0 + std::round(value * 30.0)), 2, 32);
            snprintf(text, sizeof(text), "%d steps", steps);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpEuclideanRotationId: {
            char8 text[32];
            int rot = std::clamp(static_cast<int>(std::round(value * 31.0)), 0, 31);
            snprintf(text, sizeof(text), "%d", rot);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // --- Condition Lane (076-conditional-trigs) ---
        case kArpConditionLaneLengthId: {
            char8 text[32];
            int len = std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            snprintf(text, sizeof(text), len == 1 ? "%d step" : "%d steps", len);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpFillToggleId: {
            UString(string, 128).fromAscii(value >= 0.5 ? "On" : "Off");
            return kResultOk;
        }

        // --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
        case kArpSpiceId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpDiceTriggerId: {
            UString(string, 128).fromAscii(value >= 0.5 ? "Roll" : "--");
            return kResultOk;
        }
        case kArpHumanizeId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
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
            // Pitch lane steps: display as semitone offset
            if (id >= kArpPitchLaneStep0Id && id <= kArpPitchLaneStep31Id) {
                char8 text[32];
                int pitch = std::clamp(
                    static_cast<int>(-24.0 + std::round(value * 48.0)), -24, 24);
                if (pitch > 0) {
                    snprintf(text, sizeof(text), "+%d st", pitch);
                } else {
                    snprintf(text, sizeof(text), "%d st", pitch);
                }
                UString(string, 128).fromAscii(text);
                return kResultOk;
            }
            // Modifier lane steps: display as hex
            if (id >= kArpModifierLaneStep0Id && id <= kArpModifierLaneStep31Id) {
                char8 text[32];
                int step = std::clamp(
                    static_cast<int>(std::round(value * 255.0)), 0, 255);
                snprintf(text, sizeof(text), "0x%02X", step);
                UString(string, 128).fromAscii(text);
                return kResultOk;
            }
            // Ratchet lane steps: display as "Nx" (074-ratcheting)
            if (id >= kArpRatchetLaneStep0Id && id <= kArpRatchetLaneStep31Id) {
                char8 text[32];
                int ratchet = std::clamp(
                    static_cast<int>(1.0 + std::round(value * 3.0)), 1, 4);
                snprintf(text, sizeof(text), "%dx", ratchet);
                UString(string, 128).fromAscii(text);
                return kResultOk;
            }
            // Condition lane steps: display TrigCondition name (076-conditional-trigs)
            if (id >= kArpConditionLaneStep0Id && id <= kArpConditionLaneStep31Id) {
                static const char* const kCondNames[] = {
                    "Always", "10%", "25%", "50%", "75%", "90%",
                    "1:2", "2:2", "1:3", "2:3", "3:3",
                    "1:4", "2:4", "3:4", "4:4",
                    "1st", "Fill", "!Fill"
                };
                int idx = std::clamp(
                    static_cast<int>(std::round(value * 17.0)), 0, 17);
                UString(string, 128).fromAscii(kCondNames[idx]);
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

    // --- Pitch Lane (072-independent-lanes, US3) ---
    streamer.writeInt32(params.pitchLaneLength.load(std::memory_order_relaxed));
    for (int i = 0; i < 32; ++i) {
        streamer.writeInt32(params.pitchLaneSteps[i].load(std::memory_order_relaxed));
    }

    // --- Modifier Lane (073-per-step-mods) ---
    streamer.writeInt32(params.modifierLaneLength.load(std::memory_order_relaxed));
    for (int i = 0; i < 32; ++i) {
        streamer.writeInt32(params.modifierLaneSteps[i].load(std::memory_order_relaxed));
    }
    streamer.writeInt32(params.accentVelocity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.slideTime.load(std::memory_order_relaxed));

    // --- Ratchet Lane (074-ratcheting) ---
    streamer.writeInt32(params.ratchetLaneLength.load(std::memory_order_relaxed));
    for (int i = 0; i < 32; ++i) {
        streamer.writeInt32(params.ratchetLaneSteps[i].load(std::memory_order_relaxed));
    }

    // --- Euclidean Timing (075-euclidean-timing) ---
    streamer.writeInt32(params.euclideanEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.euclideanHits.load(std::memory_order_relaxed));
    streamer.writeInt32(params.euclideanSteps.load(std::memory_order_relaxed));
    streamer.writeInt32(params.euclideanRotation.load(std::memory_order_relaxed));

    // --- Condition Lane (076-conditional-trigs) ---
    streamer.writeInt32(params.conditionLaneLength.load(std::memory_order_relaxed));
    for (int i = 0; i < 32; ++i) {
        streamer.writeInt32(params.conditionLaneSteps[i].load(std::memory_order_relaxed));
    }
    streamer.writeInt32(params.fillToggle.load(std::memory_order_relaxed) ? 1 : 0);

    // --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
    streamer.writeFloat(params.spice.load(std::memory_order_relaxed));
    streamer.writeFloat(params.humanize.load(std::memory_order_relaxed));
    // diceTrigger and overlay arrays NOT serialized (ephemeral, FR-030, FR-037)
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

    // --- Pitch Lane (072-independent-lanes, US3) ---
    // EOF-safe: if pitch lane data is missing (pre-US3 preset), keep defaults
    if (!streamer.readInt32(intVal)) return true;  // gate lane loaded OK
    params.pitchLaneLength.store(std::clamp(intVal, 1, 32), std::memory_order_relaxed);

    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(intVal)) return false;
        params.pitchLaneSteps[i].store(
            std::clamp(intVal, -24, 24), std::memory_order_relaxed);
    }

    // --- Modifier Lane (073-per-step-mods) ---
    // EOF-safe: if modifier data is missing entirely (Phase 4 preset), keep defaults.
    // If modifier data is partially present (truncated after length), return false (corrupt).
    if (!streamer.readInt32(intVal)) return true;  // EOF at first modifier field = Phase 4 compat
    params.modifierLaneLength.store(std::clamp(intVal, 1, 32), std::memory_order_relaxed);

    // From here, EOF signals a corrupt stream (length was present but steps are not)
    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(intVal)) return false;  // Corrupt: length present but no step data
        params.modifierLaneSteps[i].store(
            std::clamp(intVal, 0, 255), std::memory_order_relaxed);
    }

    // Accent velocity
    if (!streamer.readInt32(intVal)) return false;  // Corrupt: steps present but no accentVelocity
    params.accentVelocity.store(std::clamp(intVal, 0, 127), std::memory_order_relaxed);

    // Slide time
    if (!streamer.readFloat(floatVal)) return false;  // Corrupt: accentVelocity present but no slideTime
    params.slideTime.store(std::clamp(floatVal, 0.0f, 500.0f), std::memory_order_relaxed);

    // --- Ratchet Lane (074-ratcheting) ---
    // EOF-safe: if ratchet data is missing entirely (Phase 5 preset), keep defaults.
    if (!streamer.readInt32(intVal)) return true;  // EOF at first ratchet field = Phase 5 compat
    params.ratchetLaneLength.store(std::clamp(intVal, 1, 32), std::memory_order_relaxed);

    // From here, EOF signals a corrupt stream (length was present but steps are not)
    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(intVal)) return false;  // Corrupt: length present but no step data
        params.ratchetLaneSteps[i].store(
            std::clamp(intVal, 1, 4), std::memory_order_relaxed);
    }

    // --- Euclidean Timing (075-euclidean-timing) ---
    // EOF-safe: if Euclidean data is missing entirely (Phase 6 preset), keep defaults.
    if (!streamer.readInt32(intVal)) return true;  // EOF at first Euclidean field = Phase 6 compat
    params.euclideanEnabled.store(intVal != 0, std::memory_order_relaxed);

    // From here, EOF signals a corrupt stream (enabled was present but remaining fields are not)
    if (!streamer.readInt32(intVal)) return false;
    params.euclideanHits.store(std::clamp(intVal, 0, 32), std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.euclideanSteps.store(std::clamp(intVal, 2, 32), std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.euclideanRotation.store(std::clamp(intVal, 0, 31), std::memory_order_relaxed);

    // --- Condition Lane (076-conditional-trigs) ---
    // EOF-safe: if condition data is missing entirely (Phase 7 preset), keep defaults.
    if (!streamer.readInt32(intVal)) return true;  // EOF at first condition field = Phase 7 compat
    params.conditionLaneLength.store(std::clamp(intVal, 1, 32), std::memory_order_relaxed);

    // From here, EOF signals a corrupt stream (length was present but steps are not)
    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(intVal)) return false;  // Corrupt: length present but no step data
        params.conditionLaneSteps[i].store(
            std::clamp(intVal, 0, 17), std::memory_order_relaxed);
    }

    // Fill toggle
    if (!streamer.readInt32(intVal)) return false;  // Corrupt: steps present but no fill toggle
    params.fillToggle.store(intVal != 0, std::memory_order_relaxed);

    // --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
    // EOF-safe: if Spice/Humanize data is missing (Phase 8 preset), keep defaults (FR-038)
    if (!streamer.readFloat(floatVal)) return true;  // EOF at first Spice field = Phase 8 compat
    params.spice.store(std::clamp(floatVal, 0.0f, 1.0f), std::memory_order_relaxed);

    if (!streamer.readFloat(floatVal)) return false;  // Corrupt: spice present but no humanize
    params.humanize.store(std::clamp(floatVal, 0.0f, 1.0f), std::memory_order_relaxed);

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
    else return;

    // --- Velocity Lane (072-independent-lanes, US1) ---
    // EOF-safe: if lane data is missing (Phase 3 preset), keep controller defaults
    if (!streamer.readInt32(intVal)) return;
    setParam(kArpVelocityLaneLengthId,
        static_cast<double>(std::clamp(intVal, 1, 32) - 1) / 31.0);

    for (int i = 0; i < 32; ++i) {
        if (!streamer.readFloat(floatVal)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpVelocityLaneStep0Id + i),
            static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
    }

    // --- Gate Lane (072-independent-lanes, US2) ---
    if (!streamer.readInt32(intVal)) return;
    setParam(kArpGateLaneLengthId,
        static_cast<double>(std::clamp(intVal, 1, 32) - 1) / 31.0);

    for (int i = 0; i < 32; ++i) {
        if (!streamer.readFloat(floatVal)) return;
        // Gate lane: [0.01, 2.0] -> normalized: (val - 0.01) / 1.99
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpGateLaneStep0Id + i),
            static_cast<double>((std::clamp(floatVal, 0.01f, 2.0f) - 0.01f) / 1.99f));
    }

    // --- Pitch Lane (072-independent-lanes, US3) ---
    if (!streamer.readInt32(intVal)) return;
    setParam(kArpPitchLaneLengthId,
        static_cast<double>(std::clamp(intVal, 1, 32) - 1) / 31.0);

    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(intVal)) return;
        // Pitch lane: [-24, +24] -> normalized: (val + 24) / 48
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpPitchLaneStep0Id + i),
            static_cast<double>(std::clamp(intVal, -24, 24) + 24) / 48.0);
    }

    // --- Modifier Lane (073-per-step-mods) ---
    // EOF-safe: if modifier data is missing (Phase 4 preset), keep controller defaults
    if (!streamer.readInt32(intVal)) return;
    setParam(kArpModifierLaneLengthId,
        static_cast<double>(std::clamp(intVal, 1, 32) - 1) / 31.0);

    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(intVal)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpModifierLaneStep0Id + i),
            static_cast<double>(std::clamp(intVal, 0, 255)) / 255.0);
    }

    // Accent velocity: int32 -> normalized: value / 127
    if (!streamer.readInt32(intVal)) return;
    setParam(kArpAccentVelocityId,
        static_cast<double>(std::clamp(intVal, 0, 127)) / 127.0);

    // Slide time: float -> normalized: value / 500
    if (!streamer.readFloat(floatVal)) return;
    setParam(kArpSlideTimeId,
        static_cast<double>(std::clamp(floatVal, 0.0f, 500.0f)) / 500.0);

    // --- Ratchet Lane (074-ratcheting) ---
    // EOF-safe: if ratchet data is missing (Phase 5 preset), keep controller defaults
    if (!streamer.readInt32(intVal)) return;
    setParam(kArpRatchetLaneLengthId,
        static_cast<double>(std::clamp(intVal, 1, 32) - 1) / 31.0);

    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(intVal)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpRatchetLaneStep0Id + i),
            static_cast<double>(std::clamp(intVal, 1, 4) - 1) / 3.0);
    }

    // --- Euclidean Timing (075-euclidean-timing) ---
    // EOF-safe: if Euclidean data is missing (Phase 6 preset), keep controller defaults
    if (!streamer.readInt32(intVal)) return;
    setParam(kArpEuclideanEnabledId, intVal != 0 ? 1.0 : 0.0);

    if (!streamer.readInt32(intVal)) return;
    setParam(kArpEuclideanHitsId,
        static_cast<double>(std::clamp(intVal, 0, 32)) / 32.0);

    if (!streamer.readInt32(intVal)) return;
    setParam(kArpEuclideanStepsId,
        static_cast<double>(std::clamp(intVal, 2, 32) - 2) / 30.0);

    if (!streamer.readInt32(intVal)) return;
    setParam(kArpEuclideanRotationId,
        static_cast<double>(std::clamp(intVal, 0, 31)) / 31.0);

    // --- Condition Lane (076-conditional-trigs) ---
    // EOF-safe: if condition data is missing (Phase 7 preset), keep controller defaults
    if (!streamer.readInt32(intVal)) return;
    setParam(kArpConditionLaneLengthId,
        static_cast<double>(std::clamp(intVal, 1, 32) - 1) / 31.0);

    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(intVal)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpConditionLaneStep0Id + i),
            static_cast<double>(std::clamp(intVal, 0, 17)) / 17.0);
    }

    // Fill toggle
    if (!streamer.readInt32(intVal)) return;
    setParam(kArpFillToggleId, intVal != 0 ? 1.0 : 0.0);

    // --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
    // EOF-safe: if Spice/Humanize data is missing (Phase 8 preset), keep controller defaults
    if (!streamer.readFloat(floatVal)) return;
    setParam(kArpSpiceId, static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));

    if (!streamer.readFloat(floatVal)) return;
    setParam(kArpHumanizeId, static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
    // diceTrigger is NOT synced (transient action)
}

} // namespace Ruinae
