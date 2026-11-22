#pragma once

// ==============================================================================
// ArpeggiatorParams: Atomic parameter storage for the arpeggiator (FR-004)
// ==============================================================================
// Follows trance_gate_params.h pattern exactly.
// Struct + 6 inline functions for parameter handling.
// ==============================================================================

#include "plugin_ids.h"
#include "parameters/note_value_ui.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/state_load_helpers.h"
#include "controller/parameter_helpers.h"

// Shared arp save serialization (Gradus + Ruinae), from plugins/shared/src.
#include "parameters/arp_params_common.h"

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/arp_lane.h>

#include <cmath>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace Ruinae {

/// Steps in an arpeggiator lane. The value lives in ArpLane's template default;
/// this is the one place Ruinae re-states it, so the loop bounds, lane lengths
/// and normalization divisors elsewhere cannot drift apart from it or from the
/// DSP layer.
inline constexpr int kMaxArpSteps =
    static_cast<int>(Krate::DSP::ArpLane<float>::kMaxSteps);

// =============================================================================
// ArpeggiatorParams: Atomic parameter storage (FR-004)
// =============================================================================
// Thread-safe bridge between UI/host thread (writes normalized values via
// processParameterChanges) and the audio thread (reads plain values in
// applyParamsToEngine).

// Arp operating mode: determines how arp interacts with MIDI and mod engine
// Unscoped on purpose: the modes are compared with and stored as plain ints (parameter values).
enum ArpOperatingMode : std::uint8_t {  // NOLINT(cppcoreguidelines-use-enum-class)
    kArpOff = 0,      // Arp disabled, MIDI goes direct to engine
    kArpMIDI = 1,     // Arp enabled, events dispatched to engine (classic behavior)
    kArpMod = 2,      // Arp runs as modulation source only, MIDI goes direct to engine
    kArpMIDIMod = 3   // Arp dispatches notes AND outputs pitch as mod source
};

// Per-lane speed multiplier values (fixed discrete set), shared with Gradus.
using Krate::Shared::kLaneSpeedValues;
using Krate::Shared::kLaneSpeedCount;
using Krate::Shared::kLaneSpeedDefault;

/// Ruinae's names for the shared arp templates in arp_params_common.h.
struct ArpSharedScope {
    using enum ParameterIDs;
    static constexpr const std::array<int, 16>& kScaleDisplayOrder = kArpScaleDisplayOrder;
};

/// Ruinae's side of the deliberate differences in the shared arp ID span:
/// 10 arp modes (Up..Chord) and a continuous 50-75 % Ratchet Swing.
inline constexpr Krate::Shared::ArpSharedParamConfig kArpSharedConfig{
    .modeCount = 10, .ratchetSwingPositions = nullptr, .ratchetSwingPositionCount = 0};

/// The kLaneSpeedValues entry nearest `speed` (a loaded lane speed lands on the
/// same discrete set the parameter produces); a non-finite speed loads as 1.0x.
inline float snapLaneSpeed(float speed) {
    if (!Krate::DSP::detail::isFinite(speed)) {
        return kLaneSpeedValues[kLaneSpeedDefault];
    }
    int bestIdx = kLaneSpeedDefault;
    float bestDist = std::abs(kLaneSpeedValues[kLaneSpeedDefault] - speed);
    for (int si = 0; si < kLaneSpeedCount; ++si) {
        const float dist = std::abs(kLaneSpeedValues[si] - speed);
        if (dist < bestDist) { bestDist = dist; bestIdx = si; }
    }
    return kLaneSpeedValues[bestIdx];
}

struct ArpeggiatorParams {
    // Base arp params (Phase 3)
    std::atomic<int>   operatingMode{kArpOff};  // ArpOperatingMode (0-3)
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
    std::atomic<int>   velocityLaneLength{16};  // 1-32
    std::array<std::atomic<float>, 32> velocityLaneSteps{};

    // Gate lane (072-independent-lanes, US2)
    std::atomic<int>   gateLaneLength{16};      // 1-32
    std::array<std::atomic<float>, 32> gateLaneSteps{};

    // Pitch lane (072-independent-lanes, US3)
    std::atomic<int>   pitchLaneLength{16};     // 1-32
    std::array<std::atomic<int>, 32> pitchLaneSteps{};  // -24 to +24 (int for lock-free guarantee)

    // --- Modifier Lane (073-per-step-mods) ---
    std::atomic<int>   modifierLaneLength{16};     // 1-32
    std::array<std::atomic<int>, 32> modifierLaneSteps{};  // uint8_t bitmask stored as int (lock-free)

    // Modifier configuration
    std::atomic<int>   accentVelocity{30};         // 0-127
    std::atomic<float> slideTime{60.0f};           // 0-500 ms

    // --- Ratchet Lane (074-ratcheting) ---
    std::atomic<int>   ratchetLaneLength{16};      // 1-32
    std::array<std::atomic<int>, 32> ratchetLaneSteps{};  // 1-4 (int for lock-free guarantee)

    // --- Euclidean Timing (075-euclidean-timing) ---
    std::atomic<bool> euclideanEnabled{false};    // default off
    std::atomic<int>  euclideanHits{4};           // default 4
    std::atomic<int>  euclideanSteps{8};          // default 8
    std::atomic<int>  euclideanRotation{0};       // default 0

    // --- Condition Lane (076-conditional-trigs) ---
    std::atomic<int>   conditionLaneLength{16};      // 1-32
    std::array<std::atomic<int>, 32> conditionLaneSteps{};  // 0-17 (TrigCondition, int for lock-free)
    std::atomic<bool>  fillToggle{false};            // Fill mode toggle

    // --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
    std::atomic<float> spice{0.0f};
    std::atomic<bool>  diceTrigger{false};
    std::atomic<float> humanize{0.0f};

    // --- Ratchet Swing (078-ratchet-swing) ---
    std::atomic<float> ratchetSwing{50.0f};    // 50-75%

    // --- Scale Mode (084-arp-scale-mode) ---
    std::atomic<int>  scaleType{8};               // ScaleType enum value (0-15, default 8 = Chromatic)
    std::atomic<int>  rootNote{0};                // 0=C, 1=C#, ..., 11=B (default 0 = C)
    std::atomic<bool> scaleQuantizeInput{false};  // Snap incoming notes to scale (default OFF)

    // --- MIDI Output ---
    std::atomic<bool> midiOut{false};              // Output arp notes as MIDI (default OFF)

    // --- Chord Lane (arp-chord-lane) ---
    std::atomic<int>   chordLaneLength{1};         // 1-32 (default 1)
    std::array<std::atomic<int>, 32> chordLaneSteps{};  // 0-4 (ChordType, int for lock-free)

    // --- Inversion Lane (arp-chord-lane) ---
    std::atomic<int>   inversionLaneLength{1};     // 1-32 (default 1)
    std::array<std::atomic<int>, 32> inversionLaneSteps{};  // 0-3 (InversionType, int for lock-free)

    // --- Voicing Mode (arp-chord-lane) ---
    std::atomic<int>   voicingMode{0};             // 0-3 (VoicingMode, default 0=Close)

    // --- Per-Lane Speed Multipliers ---
    std::atomic<float> velocityLaneSpeed{1.0f};
    std::atomic<float> gateLaneSpeed{1.0f};
    std::atomic<float> pitchLaneSpeed{1.0f};
    std::atomic<float> modifierLaneSpeed{1.0f};
    std::atomic<float> ratchetLaneSpeed{1.0f};
    std::atomic<float> conditionLaneSpeed{1.0f};
    std::atomic<float> chordLaneSpeed{1.0f};
    std::atomic<float> inversionLaneSpeed{1.0f};

    // Gradus's v1.5 fields (ratchet decay, strum, per-lane swing and jitter,
    // velocity curve, transpose, note range, step pinning; IDs 3388-3445) are
    // absent: Ruinae's arp engine consumes none of them, so they were never
    // registered, applied, saved or loaded here, and they are not part of the
    // arp SAVE prefix shared with Gradus.

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
    // Every arp ID Ruinae handles is in the span shared with Gradus.
    Krate::Shared::handleArpParamChangeShared<ArpSharedScope>(params, id, value, kArpSharedConfig);
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

    // Arp Operating Mode: StringListParameter (4 entries), default 0 (Off)
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Operating Mode"), kArpOperatingModeId,
        {STR16("Off"), STR16("MIDI"), STR16("Mod"), STR16("MIDI+Mod")}));

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

    // Velocity lane length: RangeParameter 1-32, default 16, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Vel Lane Len"), kArpVelocityLaneLengthId,
                          STR16(""), 1, 32, 16, 31,
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

    // Gate lane length: RangeParameter 1-32, default 16, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Gate Lane Len"), kArpGateLaneLengthId,
                          STR16(""), 1, 32, 16, 31,
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

    // Pitch lane length: RangeParameter 1-32, default 16, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Pitch Lane Len"), kArpPitchLaneLengthId,
                          STR16(""), 1, 32, 16, 31,
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

    // Modifier lane length: RangeParameter 1-32, default 16, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Mod Lane Len"), kArpModifierLaneLengthId,
                          STR16(""), 1, 32, 16, 31,
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

    // Ratchet lane length: RangeParameter 1-32, default 16, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Ratchet Lane Len"), kArpRatchetLaneLengthId,
                          STR16(""), 1, 32, 16, 31,
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

    // Condition lane length: RangeParameter 1-32, default 16, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Cond Lane Len"), kArpConditionLaneLengthId,
                          STR16(""), 1, 32, 16, 31,
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

    // --- Ratchet Swing (078-ratchet-swing) ---
    // Ratchet swing: Continuous 0-1, default 0.0 (= 50%)
    // Normalized default: (50 - 50) / 25 = 0.0
    parameters.addParameter(STR16("Arp Ratchet Swing"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kArpRatchetSwingId);

    // --- Scale Mode (084-arp-scale-mode) ---

    // Scale Type: StringListParameter (16 entries), default index 0 = Chromatic
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Scale Type"), kArpScaleTypeId,
        {STR16("Chromatic"), STR16("Major"), STR16("Natural Minor"),
         STR16("Harmonic Minor"), STR16("Melodic Minor"),
         STR16("Dorian"), STR16("Phrygian"), STR16("Lydian"),
         STR16("Mixolydian"), STR16("Locrian"),
         STR16("Major Pentatonic"), STR16("Minor Pentatonic"),
         STR16("Blues"), STR16("Whole Tone"),
         STR16("Diminished (W-H)"), STR16("Diminished (H-W)")}));

    // Root Note: StringListParameter (12 entries), default index 0 = C
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Root Note"), kArpRootNoteId,
        {STR16("C"), STR16("C#"), STR16("D"), STR16("D#"),
         STR16("E"), STR16("F"), STR16("F#"), STR16("G"),
         STR16("G#"), STR16("A"), STR16("A#"), STR16("B")}));

    // Scale Quantize Input: Toggle (0 or 1), default off
    parameters.addParameter(STR16("Arp Scale Quantize"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kArpScaleQuantizeInputId);

    // --- MIDI Output ---
    // Toggle (0 or 1), default off
    parameters.addParameter(STR16("Arp MIDI Out"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kArpMidiOutId);

    // --- Playhead Parameters (079-layout-framework + 080-specialized-lane-types) ---
    // Hidden, non-automatable. Written by processor, polled by controller.
    // NOT saved to preset state (transient playback position only).
    parameters.addParameter(STR16("Arp Vel Playhead"), STR16(""), 0, 1.0,
        ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly, kArpVelocityPlayheadId);
    parameters.addParameter(STR16("Arp Gate Playhead"), STR16(""), 0, 1.0,
        ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly, kArpGatePlayheadId);
    parameters.addParameter(STR16("Arp Pitch Playhead"), STR16(""), 0, 1.0,
        ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly, kArpPitchPlayheadId);
    parameters.addParameter(STR16("Arp Ratchet Playhead"), STR16(""), 0, 1.0,
        ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly, kArpRatchetPlayheadId);
    parameters.addParameter(STR16("Arp Modifier Playhead"), STR16(""), 0, 1.0,
        ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly, kArpModifierPlayheadId);
    parameters.addParameter(STR16("Arp Condition Playhead"), STR16(""), 0, 1.0,
        ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly, kArpConditionPlayheadId);

    // --- Chord Lane (arp-chord-lane) ---

    // Chord lane length: RangeParameter 1-32, default 1, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Chord Lane Len"), kArpChordLaneLengthId,
                          STR16(""), 1, 32, 1, 31,
                          ParameterInfo::kCanAutomate));

    // Chord lane steps: RangeParameter 0-4, default 0 (None)
    for (int i = 0; i < 32; ++i) {
        char name[48];
        snprintf(name, sizeof(name), "Arp Chord Step %d", i + 1);
        Steinberg::Vst::String128 name16;
        Steinberg::UString(name16, 128).fromAscii(name);
        parameters.addParameter(
            new RangeParameter(name16,
                static_cast<ParamID>(kArpChordLaneStep0Id + i),
                STR16(""), 0, 4, 0, 4,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
    }

    // --- Inversion Lane (arp-chord-lane) ---

    // Inversion lane length: RangeParameter 1-32, default 1, stepCount 31
    parameters.addParameter(
        new RangeParameter(STR16("Arp Inv Lane Len"), kArpInversionLaneLengthId,
                          STR16(""), 1, 32, 1, 31,
                          ParameterInfo::kCanAutomate));

    // Inversion lane steps: RangeParameter 0-3, default 0 (Root)
    for (int i = 0; i < 32; ++i) {
        char name[48];
        snprintf(name, sizeof(name), "Arp Inv Step %d", i + 1);
        Steinberg::Vst::String128 name16;
        Steinberg::UString(name16, 128).fromAscii(name);
        parameters.addParameter(
            new RangeParameter(name16,
                static_cast<ParamID>(kArpInversionLaneStep0Id + i),
                STR16(""), 0, 3, 0, 3,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
    }

    // Voicing Mode: StringListParameter (4 entries), default 0 (Close)
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Voicing"), kArpVoicingModeId,
        {STR16("Close"), STR16("Drop-2"), STR16("Spread"), STR16("Random")}));

    // Chord/Inversion playhead parameters (hidden, non-automatable)
    parameters.addParameter(STR16("Arp Chord Playhead"), STR16(""), 0, 1.0,
        ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly, kArpChordPlayheadId);
    parameters.addParameter(STR16("Arp Inversion Playhead"), STR16(""), 0, 1.0,
        ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly, kArpInversionPlayheadId);

    // --- Per-Lane Speed Multipliers ---
    {
        static const char* const kLaneNames[] = {
            "Arp Velocity Lane Speed", "Arp Gate Lane Speed",
            "Arp Pitch Lane Speed", "Arp Modifier Lane Speed",
            "Arp Ratchet Lane Speed", "Arp Condition Lane Speed",
            "Arp Chord Lane Speed", "Arp Inversion Lane Speed"
        };
        static constexpr ParamID kSpeedIds[] = {
            kArpVelocityLaneSpeedId, kArpGateLaneSpeedId,
            kArpPitchLaneSpeedId, kArpModifierLaneSpeedId,
            kArpRatchetLaneSpeedId, kArpConditionLaneSpeedId,
            kArpChordLaneSpeedId, kArpInversionLaneSpeedId
        };
        for (int i = 0; i < 8; ++i) {
            Steinberg::Vst::String128 name16;
            Steinberg::UString(name16, 128).fromAscii(kLaneNames[i]);
            parameters.addParameter(
                new RangeParameter(name16, kSpeedIds[i],
                    // A stepped RangeParameter takes its default as a step index, not as a normalized value.
                    STR16("x"), 0, 1, static_cast<double>(kLaneSpeedDefault),
                    kLaneSpeedCount - 1,
                    ParameterInfo::kCanAutomate));
        }
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
    if (Krate::Shared::formatArpParamShared<ArpSharedScope>(id, value, string)) {
        return kResultOk;
    }

    // Ruinae formats its list parameters itself (Gradus leaves them to the
    // StringListParameter) and shows Ratchet Swing as a continuous percentage.
    switch (id) {
        case kArpModeId: {
            static const char* const kModeNames[] = {
                "Up", "Down", "UpDown", "DownUp", "Converge",
                "Diverge", "Random", "Walk", "AsPlayed", "Chord"
            };
            static_assert(std::size(kModeNames) == static_cast<std::size_t>(kArpSharedConfig.modeCount));
            int idx = std::clamp(static_cast<int>(value * 9.0 + 0.5), 0, 9);
            UString(string, 128).fromAscii(kModeNames[idx]);
            return kResultOk;
        }
        case kArpOctaveModeId: {
            static const char* const kOctModeNames[] = {"Sequential", "Interleaved"};
            int idx = std::clamp(static_cast<int>(value * 1.0 + 0.5), 0, 1);
            UString(string, 128).fromAscii(kOctModeNames[idx]);
            return kResultOk;
        }
        case kArpNoteValueId: {
            // Map normalized -> dropdown index, then show the registered dropdown's string
            static_assert(std::size(Parameters::kNoteValueDropdownStrings) ==
                          static_cast<std::size_t>(Parameters::kNoteValueDropdownCount));
            int idx = std::clamp(
                static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                0, Parameters::kNoteValueDropdownCount - 1);
            UString(string, 128).assign(Parameters::kNoteValueDropdownStrings[idx]);
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

        // --- Ratchet Swing (078-ratchet-swing) ---
        case kArpRatchetSwingId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", 50.0 + value * 25.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        default:
            // Scale Type, Root Note, Scale Quantize, MIDI Out, Voicing: the
            // StringListParameter / host shows these.
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
    Krate::Shared::saveArpParamsShared(params, streamer);
}

// =============================================================================
// loadArpParams: Deserialize from stream (FR-012)
// =============================================================================
// A state that carries the arp pack defines every serialized field: a field the
// pack does not carry (gated by the state version, or after an EOF early
// return) takes its default, never the value the previous preset left behind.
// Both load paths apply the same rule (loadArpParams for the processor,
// loadArpParamsToController for the controller), so a legacy preset loads the
// same values through the host and through the preset browser. A state that
// ends before the pack leaves the arp untouched, like every other tail pack.

namespace detail {

/// Writes a default instance's arp pack into `stream` and rewinds it. The save
/// format is the one list of serialized fields and their defaults, so the
/// legacy resets cannot drift from it.
inline void writeDefaultArpPack(Steinberg::MemoryStream& stream) {
    const ArpeggiatorParams defaults;
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    saveArpParams(defaults, streamer);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
}

/// True when the stream still holds the arp pack's first field. The read
/// position is left where it was.
inline bool arpPackFollows(Steinberg::IBStreamer& streamer) {
    const Steinberg::int64 position = streamer.tell();
    Steinberg::int32 probe = 0;
    const bool present = streamer.readInt32(probe);
    streamer.seek(position, Steinberg::kSeekSet);
    return present;
}

/// Reads one arp pack into `params`. Fields the stream does not carry are left
/// untouched; loadArpParams resets them first. Returns false when the stream
/// ends inside a field group (corrupt) or before the pack.
inline bool readArpParams(
    ArpeggiatorParams& params,
    Steinberg::IBStreamer& streamer,
    Steinberg::int32 stateVersion)
{
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    if (!streamer.readInt32(intVal)) return false;
    if (stateVersion < 3) {
        // v2 migration: old boolean enabled (0/1) -> operating mode (Off/MIDI)
        params.operatingMode.store(intVal != 0 ? kArpMIDI : kArpOff,
            std::memory_order_relaxed);
    } else {
        params.operatingMode.store(std::clamp(intVal, 0, 3),
            std::memory_order_relaxed);
    }

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
    params.freeRate.store(finiteClamp(floatVal, 0.5f, 50.0f, 4.0f), std::memory_order_relaxed);

    if (!streamer.readFloat(floatVal)) return false;
    params.gateLength.store(finiteClamp(floatVal, 1.0f, 200.0f, 80.0f), std::memory_order_relaxed);

    if (!streamer.readFloat(floatVal)) return false;
    params.swing.store(finiteClamp(floatVal, 0.0f, 75.0f, 0.0f), std::memory_order_relaxed);

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
            finiteClamp(floatVal, 0.0f, 1.0f, 1.0f), std::memory_order_relaxed);
    }

    // --- Gate Lane (072-independent-lanes, US2) ---
    // EOF-safe: if gate lane data is missing (pre-US2 preset), keep defaults
    if (!streamer.readInt32(intVal)) return true;  // velocity lane loaded OK
    params.gateLaneLength.store(std::clamp(intVal, 1, 32), std::memory_order_relaxed);

    for (int i = 0; i < 32; ++i) {
        if (!streamer.readFloat(floatVal)) return false;
        params.gateLaneSteps[i].store(
            finiteClamp(floatVal, 0.01f, 2.0f, 1.0f), std::memory_order_relaxed);
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
    params.slideTime.store(finiteClamp(floatVal, 0.0f, 500.0f, 60.0f), std::memory_order_relaxed);

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
    params.spice.store(finiteClamp(floatVal, 0.0f, 1.0f, 0.0f), std::memory_order_relaxed);

    if (!streamer.readFloat(floatVal)) return false;  // Corrupt: spice present but no humanize
    params.humanize.store(finiteClamp(floatVal, 0.0f, 1.0f, 0.0f), std::memory_order_relaxed);

    // --- Ratchet Swing (078-ratchet-swing) ---
    // EOF-safe: if ratchet swing data is missing (Phase 9 preset), keep default (50%)
    if (!streamer.readFloat(floatVal)) return true;
    params.ratchetSwing.store(finiteClamp(floatVal, 50.0f, 75.0f, 50.0f), std::memory_order_relaxed);

    // --- Scale Mode (084-arp-scale-mode) ---
    // EOF-safe: if scale data is missing (pre-scale-mode preset), keep defaults
    if (!streamer.readInt32(intVal)) return true;  // Old preset, keep defaults
    params.scaleType.store(std::clamp(intVal, 0, 15), std::memory_order_relaxed);
    if (!streamer.readInt32(intVal)) return true;
    params.rootNote.store(std::clamp(intVal, 0, 11), std::memory_order_relaxed);
    if (!streamer.readInt32(intVal)) return true;
    params.scaleQuantizeInput.store(intVal != 0, std::memory_order_relaxed);

    // --- MIDI Output (version 2+) ---
    if (stateVersion >= 2) {
        if (!streamer.readInt32(intVal)) return true;
        params.midiOut.store(intVal != 0, std::memory_order_relaxed);
    }

    // --- Chord Lane (arp-chord-lane, version 4+) ---
    if (stateVersion >= 4) {
        if (!streamer.readInt32(intVal)) return true;
        params.chordLaneLength.store(std::clamp(intVal, 1, 32), std::memory_order_relaxed);
        for (int i = 0; i < 32; ++i) {
            if (!streamer.readInt32(intVal)) return false;
            params.chordLaneSteps[i].store(std::clamp(intVal, 0, 4), std::memory_order_relaxed);
        }

        if (!streamer.readInt32(intVal)) return true;
        params.inversionLaneLength.store(std::clamp(intVal, 1, 32), std::memory_order_relaxed);
        for (int i = 0; i < 32; ++i) {
            if (!streamer.readInt32(intVal)) return false;
            params.inversionLaneSteps[i].store(std::clamp(intVal, 0, 3), std::memory_order_relaxed);
        }

        if (!streamer.readInt32(intVal)) return true;
        params.voicingMode.store(std::clamp(intVal, 0, 3), std::memory_order_relaxed);
    }

    // Per-lane speed multipliers (version 8+)
    if (stateVersion >= 8) {
        float speed = 1.0f;
        auto loadSpeed = [&](std::atomic<float>& target) {
            if (streamer.readFloat(speed))
                target.store(snapLaneSpeed(speed), std::memory_order_relaxed);
        };
        loadSpeed(params.velocityLaneSpeed);
        loadSpeed(params.gateLaneSpeed);
        loadSpeed(params.pitchLaneSpeed);
        loadSpeed(params.modifierLaneSpeed);
        loadSpeed(params.ratchetLaneSpeed);
        loadSpeed(params.conditionLaneSpeed);
        loadSpeed(params.chordLaneSpeed);
        loadSpeed(params.inversionLaneSpeed);
    }

    return true;
}

} // namespace detail

/// Puts every serialized arp field at its default.
inline void resetArpParamsToDefaults(ArpeggiatorParams& params) {
    Steinberg::MemoryStream stream;
    detail::writeDefaultArpPack(stream);
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);
    detail::readArpParams(params, streamer, kCurrentStateVersion);
}

/// Returns false when the stream ends before the pack or inside a field group.
inline bool loadArpParams(
    ArpeggiatorParams& params,
    Steinberg::IBStreamer& streamer,
    Steinberg::int32 stateVersion = kCurrentStateVersion)
{
    if (!detail::arpPackFollows(streamer))
        return false;
    resetArpParamsToDefaults(params);
    return detail::readArpParams(params, streamer, stateVersion);
}

// =============================================================================
// loadArpParamsToController: Controller-side state restore (FR-012)
// =============================================================================

namespace detail {

/// Controller mirror of readArpParams: calls setParam for each field the
/// stream carries, and for no other.
template<typename SetParamFunc>
inline void readArpParamsToController(
    Steinberg::IBStreamer& streamer,
    const SetParamFunc& setParam,
    Steinberg::int32 stateVersion)
{
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    // operatingMode (int32 -> normalized: index / 3)
    if (streamer.readInt32(intVal)) {
        int mode;
        if (stateVersion < 3) {
            // v2 migration: old boolean enabled (0/1) -> operating mode (Off/MIDI)
            mode = intVal != 0 ? kArpMIDI : kArpOff;
        } else {
            mode = std::clamp(intVal, 0, 3);
        }
        setParam(kArpOperatingModeId, static_cast<double>(mode) / 3.0);
    }
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

    // --- Ratchet Swing (078-ratchet-swing) ---
    // EOF-safe: if ratchet swing data is missing (Phase 9 preset), keep controller default
    if (!streamer.readFloat(floatVal)) return;
    // float [50, 75] -> normalized: (val - 50) / 25
    setParam(kArpRatchetSwingId,
        static_cast<double>((std::clamp(floatVal, 50.0f, 75.0f) - 50.0f) / 25.0f));

    // --- Scale Mode (084-arp-scale-mode) ---
    // EOF-safe: if scale data is missing (pre-scale-mode preset), keep controller defaults
    Steinberg::int32 iv = 0;
    if (streamer.readInt32(iv)) {
        int enumVal = std::clamp(static_cast<int>(iv), 0, 15);
        int uiIndex = kArpScaleEnumToDisplay[static_cast<size_t>(enumVal)];
        setParam(kArpScaleTypeId, static_cast<double>(uiIndex) / (kArpScaleTypeCount - 1));
    }
    if (streamer.readInt32(iv))
        setParam(kArpRootNoteId, static_cast<double>(std::clamp(static_cast<int>(iv), 0, 11)) / (kArpRootNoteCount - 1));
    if (streamer.readInt32(iv))
        setParam(kArpScaleQuantizeInputId, iv != 0 ? 1.0 : 0.0);

    // --- MIDI Output (version 2+) ---
    if (stateVersion >= 2) {
        if (streamer.readInt32(iv))
            setParam(kArpMidiOutId, iv != 0 ? 1.0 : 0.0);
    }

    // --- Chord Lane (arp-chord-lane, version 4+) ---
    if (stateVersion >= 4) {
        if (!streamer.readInt32(iv)) return;
        setParam(kArpChordLaneLengthId,
            static_cast<double>(std::clamp(static_cast<int>(iv), 1, 32) - 1) / 31.0);
        for (int i = 0; i < 32; ++i) {
            if (!streamer.readInt32(iv)) return;
            setParam(static_cast<Steinberg::Vst::ParamID>(kArpChordLaneStep0Id + i),
                static_cast<double>(std::clamp(static_cast<int>(iv), 0, 4)) / 4.0);
        }

        if (!streamer.readInt32(iv)) return;
        setParam(kArpInversionLaneLengthId,
            static_cast<double>(std::clamp(static_cast<int>(iv), 1, 32) - 1) / 31.0);
        for (int i = 0; i < 32; ++i) {
            if (!streamer.readInt32(iv)) return;
            setParam(static_cast<Steinberg::Vst::ParamID>(kArpInversionLaneStep0Id + i),
                static_cast<double>(std::clamp(static_cast<int>(iv), 0, 3)) / 3.0);
        }

        if (!streamer.readInt32(iv)) return;
        setParam(kArpVoicingModeId,
            static_cast<double>(std::clamp(static_cast<int>(iv), 0, 3)) / 3.0);
    }

    // Per-lane speed multipliers (version 8+)
    if (stateVersion >= 8) {
        static constexpr Steinberg::Vst::ParamID kSpeedIds[] = {
            kArpVelocityLaneSpeedId, kArpGateLaneSpeedId,
            kArpPitchLaneSpeedId, kArpModifierLaneSpeedId,
            kArpRatchetLaneSpeedId, kArpConditionLaneSpeedId,
            kArpChordLaneSpeedId, kArpInversionLaneSpeedId
        };
        for (auto pid : kSpeedIds) {
            if (streamer.readFloat(floatVal)) {
                // Find the closest speed index for normalization
                int bestIdx = kLaneSpeedDefault;
                float bestDist = 999.0f;
                for (int si = 0; si < kLaneSpeedCount; ++si) {
                    float dist = std::abs(kLaneSpeedValues[si] - floatVal);
                    if (dist < bestDist) { bestDist = dist; bestIdx = si; }
                }
                setParam(pid, static_cast<double>(bestIdx) / (kLaneSpeedCount - 1));
            }
        }
    }
}

} // namespace detail

/// The controller side of loadArpParams: the fields the stream carries, then
/// every other serialized field at the default the processor resets it to (one
/// edit per parameter, so the browser path sends each value once).
template<typename SetParamFunc>
inline void loadArpParamsToController(
    Steinberg::IBStreamer& streamer,
    const SetParamFunc& setParam,
    Steinberg::int32 stateVersion = kCurrentStateVersion)
{
    if (!detail::arpPackFollows(streamer))
        return;

    std::vector<Steinberg::Vst::ParamID> loaded;
    detail::readArpParamsToController(streamer,
        [&](Steinberg::Vst::ParamID id, double value) {
            loaded.push_back(id);
            setParam(id, value);
        },
        stateVersion);

    Steinberg::MemoryStream defaultsStream;
    detail::writeDefaultArpPack(defaultsStream);
    Steinberg::IBStreamer defaults(&defaultsStream, kLittleEndian);
    detail::readArpParamsToController(defaults,
        [&](Steinberg::Vst::ParamID id, double value) {
            if (std::find(loaded.begin(), loaded.end(), id) == loaded.end())
                setParam(id, value);
        },
        kCurrentStateVersion);
}

} // namespace Ruinae
