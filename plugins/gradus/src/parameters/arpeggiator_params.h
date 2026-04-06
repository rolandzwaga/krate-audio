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
#include "controller/parameter_helpers.h"

#include <krate/dsp/core/note_value.h>

#include <cmath>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include "../ui/speed_curve_data.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>

namespace Gradus {

// =============================================================================
// ArpeggiatorParams: Atomic parameter storage (FR-004)
// =============================================================================
// Thread-safe bridge between UI/host thread (writes normalized values via
// processParameterChanges) and the audio thread (reads plain values in
// applyParamsToEngine).

// Arp operating mode: determines how arp interacts with MIDI and mod engine
enum ArpOperatingMode {
    kArpOff = 0,      // Arp disabled, MIDI goes direct to engine
    kArpMIDI = 1,     // Arp enabled, events dispatched to engine (classic behavior)
    kArpMod = 2,      // Arp runs as modulation source only, MIDI goes direct to engine
    kArpMIDIMod = 3   // Arp dispatches notes AND outputs pitch as mod source
};

// Per-lane speed multiplier values (fixed discrete set)
static constexpr float kLaneSpeedValues[] = {
    0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 3.0f, 4.0f
};
static constexpr int kLaneSpeedCount = 10;
static constexpr int kLaneSpeedDefault = 3; // index 3 = 1.0x

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

    // --- v1.5 Features ---
    // Ratchet velocity decay (0-100%, 0 = flat, 100 = rapid exponential falloff)
    std::atomic<float> ratchetDecay{0.0f};

    // Strum mode for chords
    std::atomic<float> strumTime{0.0f};       // 0-100ms
    std::atomic<int>   strumDirection{0};     // 0=Up, 1=Down, 2=Random, 3=Alternate

    // Per-lane swing (0-75%, mirrors per-lane speed)
    std::atomic<float> velocityLaneSwing{0.0f};
    std::atomic<float> gateLaneSwing{0.0f};
    std::atomic<float> pitchLaneSwing{0.0f};
    std::atomic<float> modifierLaneSwing{0.0f};
    std::atomic<float> ratchetLaneSwing{0.0f};
    std::atomic<float> conditionLaneSwing{0.0f};
    std::atomic<float> chordLaneSwing{0.0f};
    std::atomic<float> inversionLaneSwing{0.0f};

    // --- v1.5 Part 2 ---
    std::atomic<int>   velocityCurveType{0};     // 0=Linear, 1=Exp, 2=Log, 3=S
    std::atomic<float> velocityCurveAmount{0.0f}; // 0-100%
    std::atomic<int>   transpose{0};             // -24 to +24 semitones (scale-quantized)

    // Per-lane length jitter (0-4 steps)
    std::atomic<int>   velocityLaneJitter{0};
    std::atomic<int>   gateLaneJitter{0};
    std::atomic<int>   pitchLaneJitter{0};
    std::atomic<int>   modifierLaneJitter{0};
    std::atomic<int>   ratchetLaneJitter{0};
    std::atomic<int>   conditionLaneJitter{0};
    std::atomic<int>   chordLaneJitter{0};
    std::atomic<int>   inversionLaneJitter{0};

    // --- v1.5 Part 3: Note Range Mapping (global) ---
    std::atomic<int>   rangeLow{0};     // MIDI 0-127, default 0 (no low clamp)
    std::atomic<int>   rangeHigh{127};  // MIDI 0-127, default 127 (no high clamp)
    std::atomic<int>   rangeMode{1};    // 0=Wrap, 1=Clamp, 2=Skip (default Clamp)

    // --- v1.5 Part 3: Step Pinning ---
    std::atomic<int>   pinNote{60};   // MIDI 0-127, default C4
    std::array<std::atomic<int>, 32> pinFlags{};  // 0/1 per step

    // --- Markov Chain Mode ---
    std::atomic<int>   markovPreset{0};  // 0=Uniform..4=Classical, 5=Custom
    // 7x7 row-major matrix. Each cell is 0.0-1.0. Rows are auto-normalized at
    // sample time in NoteSelector::advanceMarkov, so users can edit freely.
    std::array<std::atomic<float>, 49> markovMatrix{};

    // --- Per-Lane Speed Curve Depth ---
    std::atomic<float> velocityLaneSpeedCurveDepth{0.0f};
    std::atomic<float> gateLaneSpeedCurveDepth{0.0f};
    std::atomic<float> pitchLaneSpeedCurveDepth{0.0f};
    std::atomic<float> modifierLaneSpeedCurveDepth{0.0f};
    std::atomic<float> ratchetLaneSpeedCurveDepth{0.0f};
    std::atomic<float> conditionLaneSpeedCurveDepth{0.0f};
    std::atomic<float> chordLaneSpeedCurveDepth{0.0f};
    std::atomic<float> inversionLaneSpeedCurveDepth{0.0f};

    // Per-lane speed curve data (serialized in state, not automatable).
    // Protected by speedCurveMutex_ for thread-safe access during
    // save/load (audio thread) and IMessage handling (message thread).
    mutable std::mutex speedCurveMutex_;
    std::array<SpeedCurveData, 8> speedCurves;

    // Baked curve tables + enabled flags. Written by IMessage notify() on the
    // message thread, consumed by applyParamsToEngine on the audio thread.
    // Uses ArpeggiatorCore's staging buffer pattern (atomic dirty flags).
    // Also updated directly by the processor from speedCurves[] on state load.
    std::array<std::atomic<bool>, 8> speedCurveEnabledFlags{};

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

        // Markov: initialize matrix to Uniform (1/7 per cell)
        constexpr float kUniformCell = 1.0f / 7.0f;
        for (auto& cell : markovMatrix) {
            cell.store(kUniformCell, std::memory_order_relaxed);
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
        case kArpOperatingModeId:
            // StringListParameter: 0-1 -> 0-3 (4 entries, stepCount=3)
            params.operatingMode.store(
                std::clamp(static_cast<int>(value * 3.0 + 0.5), 0, 3),
                std::memory_order_relaxed);
            break;
        case kArpModeId:
            // StringListParameter: 0-1 -> 0-11 (12 entries, stepCount=11)
            params.mode.store(
                std::clamp(static_cast<int>(value * 11.0 + 0.5), 0, 11),
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

        // --- Ratchet Shuffle (078-ratchet-swing, snapped to 3 positions) ---
        case kArpRatchetSwingId: {
            // 3 discrete values: 0=Even(50%), 1=Triplet(~66.67%), 2=Dotted(75%)
            const int idx = std::clamp(
                static_cast<int>(std::round(value * 2.0)), 0, 2);
            static constexpr float kShuffleValues[3] = {50.0f, 66.6667f, 75.0f};
            params.ratchetSwing.store(kShuffleValues[idx],
                std::memory_order_relaxed);
            break;
        }

        // --- Scale Mode (084-arp-scale-mode) ---
        case kArpScaleTypeId: {
            // UI index -> ScaleType enum via display order mapping
            int uiIndex = std::clamp(
                static_cast<int>(value * (kArpScaleTypeCount - 1) + 0.5),
                0, kArpScaleTypeCount - 1);
            params.scaleType.store(kArpScaleDisplayOrder[static_cast<size_t>(uiIndex)],
                std::memory_order_relaxed);
            return;
        }
        case kArpRootNoteId:
            params.rootNote.store(
                std::clamp(static_cast<int>(value * (kArpRootNoteCount - 1) + 0.5),
                           0, kArpRootNoteCount - 1),
                std::memory_order_relaxed);
            return;
        case kArpScaleQuantizeInputId:
            params.scaleQuantizeInput.store(value >= 0.5, std::memory_order_relaxed);
            return;
        case kArpMidiOutId:
            params.midiOut.store(value >= 0.5, std::memory_order_relaxed);
            return;

        // --- Chord Lane (arp-chord-lane) ---
        case kArpChordLaneLengthId:
            params.chordLaneLength.store(
                std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32),
                std::memory_order_relaxed);
            return;
        case kArpInversionLaneLengthId:
            params.inversionLaneLength.store(
                std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32),
                std::memory_order_relaxed);
            return;
        case kArpVoicingModeId:
            params.voicingMode.store(
                std::clamp(static_cast<int>(value * 3.0 + 0.5), 0, 3),
                std::memory_order_relaxed);
            return;

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
            // Chord lane steps: 3305-3336 (arp-chord-lane)
            else if (id >= kArpChordLaneStep0Id && id <= kArpChordLaneStep31Id) {
                int step = std::clamp(
                    static_cast<int>(std::round(value * 4.0)), 0, 4);
                params.chordLaneSteps[id - kArpChordLaneStep0Id].store(
                    step, std::memory_order_relaxed);
            }
            // Inversion lane steps: 3338-3369 (arp-chord-lane)
            else if (id >= kArpInversionLaneStep0Id && id <= kArpInversionLaneStep31Id) {
                int step = std::clamp(
                    static_cast<int>(std::round(value * 3.0)), 0, 3);
                params.inversionLaneSteps[id - kArpInversionLaneStep0Id].store(
                    step, std::memory_order_relaxed);
            }
            // Per-lane speed multipliers (3380-3387)
            else if (id >= kArpVelocityLaneSpeedId && id <= kArpInversionLaneSpeedId) {
                int idx = std::clamp(static_cast<int>(std::round(value * (kLaneSpeedCount - 1))), 0, kLaneSpeedCount - 1);
                float speed = kLaneSpeedValues[idx];
                switch (id) {
                    case kArpVelocityLaneSpeedId:  params.velocityLaneSpeed.store(speed, std::memory_order_relaxed); break;
                    case kArpGateLaneSpeedId:      params.gateLaneSpeed.store(speed, std::memory_order_relaxed); break;
                    case kArpPitchLaneSpeedId:     params.pitchLaneSpeed.store(speed, std::memory_order_relaxed); break;
                    case kArpModifierLaneSpeedId:  params.modifierLaneSpeed.store(speed, std::memory_order_relaxed); break;
                    case kArpRatchetLaneSpeedId:   params.ratchetLaneSpeed.store(speed, std::memory_order_relaxed); break;
                    case kArpConditionLaneSpeedId: params.conditionLaneSpeed.store(speed, std::memory_order_relaxed); break;
                    case kArpChordLaneSpeedId:     params.chordLaneSpeed.store(speed, std::memory_order_relaxed); break;
                    case kArpInversionLaneSpeedId: params.inversionLaneSpeed.store(speed, std::memory_order_relaxed); break;
                    default: break;
                }
            }
            // v1.5: Ratchet Decay (0-100%)
            else if (id == kArpRatchetDecayId) {
                params.ratchetDecay.store(
                    std::clamp(static_cast<float>(value * 100.0), 0.0f, 100.0f),
                    std::memory_order_relaxed);
            }
            // v1.5: Strum Time (0-100ms)
            else if (id == kArpStrumTimeId) {
                params.strumTime.store(
                    std::clamp(static_cast<float>(value * 100.0), 0.0f, 100.0f),
                    std::memory_order_relaxed);
            }
            // v1.5: Strum Direction (0-3)
            else if (id == kArpStrumDirectionId) {
                params.strumDirection.store(
                    std::clamp(static_cast<int>(value * 3.0 + 0.5), 0, 3),
                    std::memory_order_relaxed);
            }
            // v1.5: Per-lane swing (3391-3398)
            else if (id >= kArpVelocityLaneSwingId && id <= kArpInversionLaneSwingId) {
                float swing = std::clamp(static_cast<float>(value * 75.0), 0.0f, 75.0f);
                switch (id) {
                    case kArpVelocityLaneSwingId:  params.velocityLaneSwing.store(swing, std::memory_order_relaxed); break;
                    case kArpGateLaneSwingId:      params.gateLaneSwing.store(swing, std::memory_order_relaxed); break;
                    case kArpPitchLaneSwingId:     params.pitchLaneSwing.store(swing, std::memory_order_relaxed); break;
                    case kArpModifierLaneSwingId:  params.modifierLaneSwing.store(swing, std::memory_order_relaxed); break;
                    case kArpRatchetLaneSwingId:   params.ratchetLaneSwing.store(swing, std::memory_order_relaxed); break;
                    case kArpConditionLaneSwingId: params.conditionLaneSwing.store(swing, std::memory_order_relaxed); break;
                    case kArpChordLaneSwingId:     params.chordLaneSwing.store(swing, std::memory_order_relaxed); break;
                    case kArpInversionLaneSwingId: params.inversionLaneSwing.store(swing, std::memory_order_relaxed); break;
                    default: break;
                }
            }
            // v1.5 Part 2: Velocity Curve Type (0-3)
            else if (id == kArpVelocityCurveTypeId) {
                params.velocityCurveType.store(
                    std::clamp(static_cast<int>(value * 3.0 + 0.5), 0, 3),
                    std::memory_order_relaxed);
            }
            // v1.5 Part 2: Velocity Curve Amount (0-100%)
            else if (id == kArpVelocityCurveAmountId) {
                params.velocityCurveAmount.store(
                    std::clamp(static_cast<float>(value * 100.0), 0.0f, 100.0f),
                    std::memory_order_relaxed);
            }
            // v1.5 Part 2: Transpose (-24 to +24)
            else if (id == kArpTransposeId) {
                params.transpose.store(
                    std::clamp(static_cast<int>(std::round(value * 48.0 - 24.0)), -24, 24),
                    std::memory_order_relaxed);
            }
            // v1.5 Part 3: Note Range Mapping (3410-3412)
            else if (id == kArpRangeLowId) {
                params.rangeLow.store(
                    std::clamp(static_cast<int>(std::round(value * 127.0)), 0, 127),
                    std::memory_order_relaxed);
            }
            else if (id == kArpRangeHighId) {
                params.rangeHigh.store(
                    std::clamp(static_cast<int>(std::round(value * 127.0)), 0, 127),
                    std::memory_order_relaxed);
            }
            else if (id == kArpRangeModeId) {
                params.rangeMode.store(
                    std::clamp(static_cast<int>(value * 2.0 + 0.5), 0, 2),
                    std::memory_order_relaxed);
            }
            // v1.5 Part 3: Step Pinning
            else if (id == kArpPinNoteId) {
                params.pinNote.store(
                    std::clamp(static_cast<int>(std::round(value * 127.0)), 0, 127),
                    std::memory_order_relaxed);
            }
            else if (id >= kArpPinFlagStep0Id && id <= kArpPinFlagStep31Id) {
                int stepIdx = static_cast<int>(id - kArpPinFlagStep0Id);
                params.pinFlags[stepIdx].store(
                    value >= 0.5 ? 1 : 0, std::memory_order_relaxed);
            }
            // Markov Chain mode
            else if (id == kArpMarkovPresetId) {
                // StringListParameter: 0-1 -> 0-5 (6 entries, stepCount=5)
                params.markovPreset.store(
                    std::clamp(static_cast<int>(value * 5.0 + 0.5), 0, 5),
                    std::memory_order_relaxed);
            }
            else if (id >= kArpMarkovCell00Id && id <= kArpMarkovCell66Id) {
                int cellIdx = static_cast<int>(id - kArpMarkovCell00Id);
                params.markovMatrix[static_cast<size_t>(cellIdx)].store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                    std::memory_order_relaxed);
            }
            // v1.5 Part 2: Per-lane Length Jitter (3402-3409)
            else if (id >= kArpVelocityLaneJitterId && id <= kArpInversionLaneJitterId) {
                int jitter = std::clamp(static_cast<int>(std::round(value * 4.0)), 0, 4);
                switch (id) {
                    case kArpVelocityLaneJitterId:  params.velocityLaneJitter.store(jitter, std::memory_order_relaxed); break;
                    case kArpGateLaneJitterId:      params.gateLaneJitter.store(jitter, std::memory_order_relaxed); break;
                    case kArpPitchLaneJitterId:     params.pitchLaneJitter.store(jitter, std::memory_order_relaxed); break;
                    case kArpModifierLaneJitterId:  params.modifierLaneJitter.store(jitter, std::memory_order_relaxed); break;
                    case kArpRatchetLaneJitterId:   params.ratchetLaneJitter.store(jitter, std::memory_order_relaxed); break;
                    case kArpConditionLaneJitterId: params.conditionLaneJitter.store(jitter, std::memory_order_relaxed); break;
                    case kArpChordLaneJitterId:     params.chordLaneJitter.store(jitter, std::memory_order_relaxed); break;
                    case kArpInversionLaneJitterId: params.inversionLaneJitter.store(jitter, std::memory_order_relaxed); break;
                    default: break;
                }
            }
            // v1.6: Per-lane Speed Curve Depth (3500-3507)
            else if (id >= kArpVelocityLaneSpeedCurveDepthId &&
                     id <= kArpInversionLaneSpeedCurveDepthId) {
                float depth = std::clamp(static_cast<float>(value), 0.0f, 1.0f);
                switch (id) {
                    case kArpVelocityLaneSpeedCurveDepthId:  params.velocityLaneSpeedCurveDepth.store(depth, std::memory_order_relaxed); break;
                    case kArpGateLaneSpeedCurveDepthId:      params.gateLaneSpeedCurveDepth.store(depth, std::memory_order_relaxed); break;
                    case kArpPitchLaneSpeedCurveDepthId:     params.pitchLaneSpeedCurveDepth.store(depth, std::memory_order_relaxed); break;
                    case kArpModifierLaneSpeedCurveDepthId:  params.modifierLaneSpeedCurveDepth.store(depth, std::memory_order_relaxed); break;
                    case kArpRatchetLaneSpeedCurveDepthId:   params.ratchetLaneSpeedCurveDepth.store(depth, std::memory_order_relaxed); break;
                    case kArpConditionLaneSpeedCurveDepthId: params.conditionLaneSpeedCurveDepth.store(depth, std::memory_order_relaxed); break;
                    case kArpChordLaneSpeedCurveDepthId:     params.chordLaneSpeedCurveDepth.store(depth, std::memory_order_relaxed); break;
                    case kArpInversionLaneSpeedCurveDepthId: params.inversionLaneSpeedCurveDepth.store(depth, std::memory_order_relaxed); break;
                    default: break;
                }
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

    // Arp Operating Mode: StringListParameter (4 entries), default 0 (Off)
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Operating Mode"), kArpOperatingModeId,
        {STR16("Off"), STR16("MIDI"), STR16("Mod"), STR16("MIDI+Mod")}));

    // Arp Mode: StringListParameter (12 entries), default 0 (Up)
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Mode"), kArpModeId,
        {STR16("Up"), STR16("Down"), STR16("UpDown"), STR16("DownUp"),
         STR16("Converge"), STR16("Diverge"), STR16("Random"),
         STR16("Walk"), STR16("AsPlayed"), STR16("Chord"),
         STR16("Gravity"), STR16("Markov Chain")}));

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

    // --- Ratchet Shuffle (078-ratchet-swing, snapped to 3 positions) ---
    // 3 discrete values: Even (50%), Triplet (~66.67%), Dotted (75%)
    // Default = 0 (Even). StringListParameter gives automatic snapping.
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Ratchet Shuffle"), kArpRatchetSwingId,
        {STR16("Even"), STR16("Triplet"), STR16("Dotted")}));

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
                    STR16("x"), 0, 1, static_cast<double>(kLaneSpeedDefault) / (kLaneSpeedCount - 1),
                    kLaneSpeedCount - 1,
                    ParameterInfo::kCanAutomate));
        }
    }

    // --- v1.5 Features: Ratchet Decay, Strum, Per-Lane Swing ---

    // Ratchet Velocity Decay: 0-100%, default 0
    parameters.addParameter(
        new RangeParameter(STR16("Arp Ratchet Decay"), kArpRatchetDecayId,
            STR16("%"), 0.0, 100.0, 0.0,
            0, ParameterInfo::kCanAutomate));

    // Strum Time: 0-100ms, default 0
    parameters.addParameter(
        new RangeParameter(STR16("Arp Strum Time"), kArpStrumTimeId,
            STR16("ms"), 0.0, 100.0, 0.0,
            0, ParameterInfo::kCanAutomate));

    // Strum Direction: StringListParameter (4 entries), default 0 (Up)
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Strum Direction"), kArpStrumDirectionId,
        {STR16("Up"), STR16("Down"), STR16("Random"), STR16("Alternate")}));

    // Per-lane swing (8 parameters, 0-75%)
    {
        static const char* const kSwingNames[] = {
            "Arp Velocity Lane Swing", "Arp Gate Lane Swing",
            "Arp Pitch Lane Swing", "Arp Modifier Lane Swing",
            "Arp Ratchet Lane Swing", "Arp Condition Lane Swing",
            "Arp Chord Lane Swing", "Arp Inversion Lane Swing"
        };
        static constexpr ParamID kSwingIds[] = {
            kArpVelocityLaneSwingId, kArpGateLaneSwingId,
            kArpPitchLaneSwingId, kArpModifierLaneSwingId,
            kArpRatchetLaneSwingId, kArpConditionLaneSwingId,
            kArpChordLaneSwingId, kArpInversionLaneSwingId
        };
        for (int i = 0; i < 8; ++i) {
            Steinberg::Vst::String128 name16;
            Steinberg::UString(name16, 128).fromAscii(kSwingNames[i]);
            parameters.addParameter(
                new RangeParameter(name16, kSwingIds[i],
                    STR16("%"), 0.0, 75.0, 0.0,
                    0, ParameterInfo::kCanAutomate));
        }
    }

    // --- v1.5 Part 2: Velocity Curve, Transpose, Length Jitter ---

    // Velocity Curve Type: StringListParameter (4 entries), default 0 (Linear)
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Velocity Curve Type"), kArpVelocityCurveTypeId,
        {STR16("Linear"), STR16("Exponential"), STR16("Logarithmic"), STR16("S-Curve")}));

    // Velocity Curve Amount: 0-100%, default 0
    parameters.addParameter(
        new RangeParameter(STR16("Arp Velocity Curve Amount"), kArpVelocityCurveAmountId,
            STR16("%"), 0.0, 100.0, 0.0,
            0, ParameterInfo::kCanAutomate));

    // Transpose: -24 to +24 semitones (scale-quantized), default 0
    // Normalized default: (0 - (-24)) / 48 = 0.5
    parameters.addParameter(
        new RangeParameter(STR16("Arp Transpose"), kArpTransposeId,
            STR16("st"), -24.0, 24.0, 0.0,
            48, ParameterInfo::kCanAutomate));

    // Per-lane Length Jitter: 0-4 steps per lane, default 0
    {
        static const char* const kJitterNames[] = {
            "Arp Velocity Lane Jitter", "Arp Gate Lane Jitter",
            "Arp Pitch Lane Jitter", "Arp Modifier Lane Jitter",
            "Arp Ratchet Lane Jitter", "Arp Condition Lane Jitter",
            "Arp Chord Lane Jitter", "Arp Inversion Lane Jitter"
        };
        static constexpr ParamID kJitterIds[] = {
            kArpVelocityLaneJitterId, kArpGateLaneJitterId,
            kArpPitchLaneJitterId, kArpModifierLaneJitterId,
            kArpRatchetLaneJitterId, kArpConditionLaneJitterId,
            kArpChordLaneJitterId, kArpInversionLaneJitterId
        };
        for (int i = 0; i < 8; ++i) {
            Steinberg::Vst::String128 name16;
            Steinberg::UString(name16, 128).fromAscii(kJitterNames[i]);
            parameters.addParameter(
                new RangeParameter(name16, kJitterIds[i],
                    STR16("steps"), 0.0, 4.0, 0.0,
                    4, ParameterInfo::kCanAutomate));
        }
    }

    // --- v1.5 Part 3: Note Range Mapping ---

    // Range Low: 0-127, default 0
    parameters.addParameter(
        new RangeParameter(STR16("Arp Range Low"), kArpRangeLowId,
            STR16(""), 0.0, 127.0, 0.0,
            127, ParameterInfo::kCanAutomate));

    // Range High: 0-127, default 127
    parameters.addParameter(
        new RangeParameter(STR16("Arp Range High"), kArpRangeHighId,
            STR16(""), 0.0, 127.0, 127.0,
            127, ParameterInfo::kCanAutomate));

    // Range Mode: StringListParameter (3 entries), default 1 (Clamp)
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Range Mode"), kArpRangeModeId,
        {STR16("Wrap"), STR16("Clamp"), STR16("Skip")}));

    // --- v1.5 Part 3: Step Pinning ---

    // Pin Note (global, MIDI 0-127, default 60 = C4)
    parameters.addParameter(
        new RangeParameter(STR16("Arp Pin Note"), kArpPinNoteId,
            STR16(""), 0.0, 127.0, 60.0,
            127, ParameterInfo::kCanAutomate));

    // Pin Flags (32 steps, binary 0/1)
    for (int i = 0; i < 32; ++i) {
        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "Arp Pin Step %d", i + 1);
        Steinberg::Vst::String128 name16;
        Steinberg::UString(name16, 128).fromAscii(nameBuf);
        parameters.addParameter(
            new RangeParameter(name16,
                static_cast<ParamID>(kArpPinFlagStep0Id + i),
                STR16(""), 0.0, 1.0, 0.0,
                1, ParameterInfo::kCanAutomate));
    }

    // --- Markov Chain Mode ---

    // Markov Preset: StringListParameter (6 entries), default 0 (Uniform)
    parameters.addParameter(createDropdownParameter(
        STR16("Arp Markov Preset"), kArpMarkovPresetId,
        {STR16("Uniform"), STR16("Jazz"), STR16("Minimal"),
         STR16("Ambient"), STR16("Classical"), STR16("Custom")}));

    // Markov Cells: 49 RangeParams (0.0-1.0). Default to Uniform (1/7) so that
    // the matrix is usable out-of-the-box before any preset is loaded.
    constexpr double kUniformCell = 1.0 / 7.0;
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 7; ++col) {
            const int idx = row * 7 + col;
            char nameBuf[48];
            snprintf(nameBuf, sizeof(nameBuf),
                     "Arp Markov Cell %d-%d", row, col);
            Steinberg::Vst::String128 name16;
            Steinberg::UString(name16, 128).fromAscii(nameBuf);
            parameters.addParameter(
                new RangeParameter(name16,
                    static_cast<ParamID>(kArpMarkovCell00Id + idx),
                    STR16(""), 0.0, 1.0, kUniformCell,
                    0, ParameterInfo::kCanAutomate));
        }
    }

    // v1.6: Per-lane Speed Curve Depth (0.0-1.0, default 0.0 = off)
    static constexpr struct {
        const Steinberg::char16* name;
        ParamID id;
    } kSpeedCurveDepthParams[] = {
        {STR16("Velocity Speed Curve Depth"), kArpVelocityLaneSpeedCurveDepthId},
        {STR16("Gate Speed Curve Depth"),     kArpGateLaneSpeedCurveDepthId},
        {STR16("Pitch Speed Curve Depth"),    kArpPitchLaneSpeedCurveDepthId},
        {STR16("Modifier Speed Curve Depth"), kArpModifierLaneSpeedCurveDepthId},
        {STR16("Ratchet Speed Curve Depth"),  kArpRatchetLaneSpeedCurveDepthId},
        {STR16("Condition Speed Curve Depth"),kArpConditionLaneSpeedCurveDepthId},
        {STR16("Chord Speed Curve Depth"),    kArpChordLaneSpeedCurveDepthId},
        {STR16("Inversion Speed Curve Depth"),kArpInversionLaneSpeedCurveDepthId},
    };
    for (const auto& p : kSpeedCurveDepthParams) {
        parameters.addParameter(
            new RangeParameter(p.name, p.id,
                STR16("%"), 0.0, 1.0, 0.0,
                0, ParameterInfo::kCanAutomate));
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
        case kArpOctaveRangeId: {
            char8 text[32];
            int range = std::clamp(static_cast<int>(1.0 + std::round(value * 3.0)), 1, 4);
            snprintf(text, sizeof(text), "%d", range);
            UString(string, 128).fromAscii(text);
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

        // --- Ratchet Shuffle — StringListParameter handles display ---
        case kArpRatchetSwingId:
            return kResultFalse;

        // --- v1.5 Features ---
        case kArpRatchetDecayId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpStrumTimeId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f ms", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpStrumDirectionId:
            return kResultFalse; // StringListParameter handles

        // --- v1.5 Part 2 ---
        case kArpVelocityCurveTypeId:
            return kResultFalse; // StringListParameter handles
        case kArpVelocityCurveAmountId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpTransposeId: {
            char8 text[32];
            int semi = static_cast<int>(std::round(value * 48.0 - 24.0));
            snprintf(text, sizeof(text), "%+d st", semi);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpVelocityLaneJitterId:
        case kArpGateLaneJitterId:
        case kArpPitchLaneJitterId:
        case kArpModifierLaneJitterId:
        case kArpRatchetLaneJitterId:
        case kArpConditionLaneJitterId:
        case kArpChordLaneJitterId:
        case kArpInversionLaneJitterId: {
            char8 text[32];
            int steps = static_cast<int>(std::round(value * 4.0));
            snprintf(text, sizeof(text), "%d step%s", steps, steps == 1 ? "" : "s");
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // --- v1.5 Part 3: Note Range ---
        case kArpRangeLowId:
        case kArpRangeHighId: {
            char8 text[32];
            int midi = static_cast<int>(std::round(value * 127.0));
            // Format as note name: C-1, C0, ..., G9
            static const char* const kNoteNames[] = {
                "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
            };
            int octave = (midi / 12) - 1;
            snprintf(text, sizeof(text), "%s%d", kNoteNames[midi % 12], octave);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kArpRangeModeId:
            return kResultFalse; // StringListParameter handles

        // --- v1.5 Part 3: Step Pinning ---
        case kArpPinNoteId: {
            char8 text[32];
            int midi = static_cast<int>(std::round(value * 127.0));
            static const char* const kNoteNames[] = {
                "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
            };
            int octave = (midi / 12) - 1;
            snprintf(text, sizeof(text), "%s%d", kNoteNames[midi % 12], octave);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        // --- Scale Mode (084-arp-scale-mode) ---
        // Let StringListParameter handle display for these.
        // StringListParameter::toString reads from the list registered via
        // createDropdownParameter() in registerArpParams(), so adding a new
        // StringList param only requires updating ONE source of truth.
        case kArpOperatingModeId:
        case kArpModeId:
        case kArpOctaveModeId:
        case kArpNoteValueId:
        case kArpLatchModeId:
        case kArpRetriggerId:
        case kArpScaleTypeId:
        case kArpRootNoteId:
        case kArpScaleQuantizeInputId:
        case kArpMidiOutId:
        case kArpVoicingModeId:
        case kArpMarkovPresetId:
            return kResultFalse;

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
            // Modifier lane steps: display as human-readable flag abbreviations (FR-022)
            if (id >= kArpModifierLaneStep0Id && id <= kArpModifierLaneStep31Id) {
                int step = std::clamp(
                    static_cast<int>(std::round(value * 255.0)), 0, 255);
                // ArpStepFlags: kStepActive=0x01, kStepTie=0x02, kStepSlide=0x04, kStepAccent=0x08
                constexpr int kActive = 0x01;
                constexpr int kTie    = 0x02;
                constexpr int kSlide  = 0x04;
                constexpr int kAccent = 0x08;
                if (!(step & kActive)) {
                    UString(string, 128).fromAscii("REST");
                    return kResultOk;
                }
                // Active step -- build abbreviation string from flags
                char8 text[32];
                text[0] = '\0';
                if (step & kTie) {
                    snprintf(text, sizeof(text), "TIE");
                } else {
                    bool hasSlide = (step & kSlide) != 0;
                    bool hasAccent = (step & kAccent) != 0;
                    if (hasSlide && hasAccent)
                        snprintf(text, sizeof(text), "SL AC");
                    else if (hasSlide)
                        snprintf(text, sizeof(text), "SL");
                    else if (hasAccent)
                        snprintf(text, sizeof(text), "AC");
                    else
                        snprintf(text, sizeof(text), "--");
                }
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
            // Chord lane steps: display ChordType name (arp-chord-lane)
            if (id >= kArpChordLaneStep0Id && id <= kArpChordLaneStep31Id) {
                static const char* const kChordNames[] = {
                    "None", "Dyad", "Triad", "7th", "9th"
                };
                int idx = std::clamp(
                    static_cast<int>(std::round(value * 4.0)), 0, 4);
                UString(string, 128).fromAscii(kChordNames[idx]);
                return kResultOk;
            }
            // Inversion lane steps: display InversionType name (arp-chord-lane)
            if (id >= kArpInversionLaneStep0Id && id <= kArpInversionLaneStep31Id) {
                static const char* const kInvNames[] = {
                    "Root", "1st", "2nd", "3rd"
                };
                int idx = std::clamp(
                    static_cast<int>(std::round(value * 3.0)), 0, 3);
                UString(string, 128).fromAscii(kInvNames[idx]);
                return kResultOk;
            }
            // Per-lane speed multipliers
            if (id >= kArpVelocityLaneSpeedId && id <= kArpInversionLaneSpeedId) {
                int idx = std::clamp(static_cast<int>(std::round(value * (kLaneSpeedCount - 1))), 0, kLaneSpeedCount - 1);
                float speed = kLaneSpeedValues[idx];
                char buf[16];
                if (speed == static_cast<int>(speed))
                    snprintf(buf, sizeof(buf), "%dx", static_cast<int>(speed));
                else
                    snprintf(buf, sizeof(buf), "%.2fx", speed);
                UString(string, 128).fromAscii(buf);
                return kResultTrue;
            }
            break;
        // Speed Curve Depth: display as 0-100%
        case kArpVelocityLaneSpeedCurveDepthId:
        case kArpGateLaneSpeedCurveDepthId:
        case kArpPitchLaneSpeedCurveDepthId:
        case kArpModifierLaneSpeedCurveDepthId:
        case kArpRatchetLaneSpeedCurveDepthId:
        case kArpConditionLaneSpeedCurveDepthId:
        case kArpChordLaneSpeedCurveDepthId:
        case kArpInversionLaneSpeedCurveDepthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%d%%", static_cast<int>(std::round(value * 100.0)));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
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
    // 11 fields in order: operatingMode, mode, octaveRange, octaveMode, tempoSync,
    // noteValue (all int32), freeRate, gateLength, swing (all float),
    // latchMode, retrigger (both int32)
    streamer.writeInt32(params.operatingMode.load(std::memory_order_relaxed));
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

    // --- Ratchet Swing (078-ratchet-swing) ---
    streamer.writeFloat(params.ratchetSwing.load(std::memory_order_relaxed));

    // --- Scale Mode (084-arp-scale-mode) ---
    streamer.writeInt32(params.scaleType.load(std::memory_order_relaxed));
    streamer.writeInt32(params.rootNote.load(std::memory_order_relaxed));
    streamer.writeInt32(params.scaleQuantizeInput.load(std::memory_order_relaxed) ? 1 : 0);

    // --- MIDI Output ---
    streamer.writeInt32(params.midiOut.load(std::memory_order_relaxed) ? 1 : 0);

    // --- Chord Lane (arp-chord-lane, version 4+) ---
    streamer.writeInt32(params.chordLaneLength.load(std::memory_order_relaxed));
    for (int i = 0; i < 32; ++i) {
        streamer.writeInt32(params.chordLaneSteps[i].load(std::memory_order_relaxed));
    }

    // --- Inversion Lane (arp-chord-lane, version 4+) ---
    streamer.writeInt32(params.inversionLaneLength.load(std::memory_order_relaxed));
    for (int i = 0; i < 32; ++i) {
        streamer.writeInt32(params.inversionLaneSteps[i].load(std::memory_order_relaxed));
    }

    // --- Voicing Mode (arp-chord-lane, version 4+) ---
    streamer.writeInt32(params.voicingMode.load(std::memory_order_relaxed));

    // Per-lane speed multipliers
    streamer.writeFloat(params.velocityLaneSpeed.load(std::memory_order_relaxed));
    streamer.writeFloat(params.gateLaneSpeed.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pitchLaneSpeed.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modifierLaneSpeed.load(std::memory_order_relaxed));
    streamer.writeFloat(params.ratchetLaneSpeed.load(std::memory_order_relaxed));
    streamer.writeFloat(params.conditionLaneSpeed.load(std::memory_order_relaxed));
    streamer.writeFloat(params.chordLaneSpeed.load(std::memory_order_relaxed));
    streamer.writeFloat(params.inversionLaneSpeed.load(std::memory_order_relaxed));

    // --- v1.5 Features: Ratchet Decay, Strum, Per-Lane Swing ---
    streamer.writeFloat(params.ratchetDecay.load(std::memory_order_relaxed));
    streamer.writeFloat(params.strumTime.load(std::memory_order_relaxed));
    streamer.writeInt32(params.strumDirection.load(std::memory_order_relaxed));
    streamer.writeFloat(params.velocityLaneSwing.load(std::memory_order_relaxed));
    streamer.writeFloat(params.gateLaneSwing.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pitchLaneSwing.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modifierLaneSwing.load(std::memory_order_relaxed));
    streamer.writeFloat(params.ratchetLaneSwing.load(std::memory_order_relaxed));
    streamer.writeFloat(params.conditionLaneSwing.load(std::memory_order_relaxed));
    streamer.writeFloat(params.chordLaneSwing.load(std::memory_order_relaxed));
    streamer.writeFloat(params.inversionLaneSwing.load(std::memory_order_relaxed));

    // --- v1.5 Part 2: Velocity Curve, Transpose, Per-Lane Length Jitter ---
    streamer.writeInt32(params.velocityCurveType.load(std::memory_order_relaxed));
    streamer.writeFloat(params.velocityCurveAmount.load(std::memory_order_relaxed));
    streamer.writeInt32(params.transpose.load(std::memory_order_relaxed));
    streamer.writeInt32(params.velocityLaneJitter.load(std::memory_order_relaxed));
    streamer.writeInt32(params.gateLaneJitter.load(std::memory_order_relaxed));
    streamer.writeInt32(params.pitchLaneJitter.load(std::memory_order_relaxed));
    streamer.writeInt32(params.modifierLaneJitter.load(std::memory_order_relaxed));
    streamer.writeInt32(params.ratchetLaneJitter.load(std::memory_order_relaxed));
    streamer.writeInt32(params.conditionLaneJitter.load(std::memory_order_relaxed));
    streamer.writeInt32(params.chordLaneJitter.load(std::memory_order_relaxed));
    streamer.writeInt32(params.inversionLaneJitter.load(std::memory_order_relaxed));

    // --- v1.5 Part 3: Note Range Mapping ---
    streamer.writeInt32(params.rangeLow.load(std::memory_order_relaxed));
    streamer.writeInt32(params.rangeHigh.load(std::memory_order_relaxed));
    streamer.writeInt32(params.rangeMode.load(std::memory_order_relaxed));

    // --- v1.5 Part 3: Step Pinning ---
    streamer.writeInt32(params.pinNote.load(std::memory_order_relaxed));
    for (int i = 0; i < 32; ++i) {
        streamer.writeInt32(params.pinFlags[i].load(std::memory_order_relaxed));
    }

    // --- Markov Chain Mode ---
    // Preset selector (0..5) + 49 matrix cells (float 0-1, row-major).
    streamer.writeInt32(params.markovPreset.load(std::memory_order_relaxed));
    for (size_t i = 0; i < 49; ++i) {
        streamer.writeFloat(params.markovMatrix[i].load(std::memory_order_relaxed));
    }

    // --- Per-Lane Speed Curve Depth + Curve Data ---
    streamer.writeFloat(params.velocityLaneSpeedCurveDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.gateLaneSpeedCurveDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.pitchLaneSpeedCurveDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modifierLaneSpeedCurveDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.ratchetLaneSpeedCurveDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.conditionLaneSpeedCurveDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.chordLaneSpeedCurveDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.inversionLaneSpeedCurveDepth.load(std::memory_order_relaxed));

    // Per-lane speed curve point data (free-form bezier curves)
    {
        std::lock_guard<std::mutex> lock(params.speedCurveMutex_);
        for (int lane = 0; lane < 8; ++lane) {
            const auto& curve = params.speedCurves[static_cast<size_t>(lane)];
            streamer.writeInt32(curve.enabled ? 1 : 0);
            streamer.writeInt32(curve.presetIndex);
            streamer.writeInt32(static_cast<Steinberg::int32>(curve.points.size()));
            for (const auto& pt : curve.points) {
                streamer.writeFloat(pt.x);
                streamer.writeFloat(pt.y);
                streamer.writeFloat(pt.cpLeftX);
                streamer.writeFloat(pt.cpLeftY);
                streamer.writeFloat(pt.cpRightX);
                streamer.writeFloat(pt.cpRightY);
            }
        }
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
    params.operatingMode.store(std::clamp(intVal, 0, 3),
        std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    // 12 entries (0=Up..11=Markov)
    params.mode.store(std::clamp(intVal, 0, 11), std::memory_order_relaxed);

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

    // --- Ratchet Swing (078-ratchet-swing) ---
    // EOF-safe: if ratchet swing data is missing (Phase 9 preset), keep default (50%)
    if (!streamer.readFloat(floatVal)) return true;
    params.ratchetSwing.store(std::clamp(floatVal, 50.0f, 75.0f), std::memory_order_relaxed);

    // --- Scale Mode (084-arp-scale-mode) ---
    // EOF-safe: if scale data is missing (pre-scale-mode preset), keep defaults
    if (!streamer.readInt32(intVal)) return true;  // Old preset, keep defaults
    params.scaleType.store(std::clamp(intVal, 0, 15), std::memory_order_relaxed);
    if (!streamer.readInt32(intVal)) return true;
    params.rootNote.store(std::clamp(intVal, 0, 11), std::memory_order_relaxed);
    if (!streamer.readInt32(intVal)) return true;
    params.scaleQuantizeInput.store(intVal != 0, std::memory_order_relaxed);

    // --- MIDI Output ---
    if (!streamer.readInt32(intVal)) return true;
    params.midiOut.store(intVal != 0, std::memory_order_relaxed);

    // --- Chord Lane (arp-chord-lane) ---
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

    // Per-lane speed multipliers
    {
        float speed = 1.0f;
        auto loadSpeed = [&](std::atomic<float>& target) {
            if (streamer.readFloat(speed))
                target.store(speed, std::memory_order_relaxed);
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

    // --- v1.5 Features (optional, for backward compatibility) ---
    {
        float f = 0.0f;
        auto loadFloat = [&](std::atomic<float>& target) -> bool {
            if (!streamer.readFloat(f)) return false;
            target.store(f, std::memory_order_relaxed);
            return true;
        };

        if (!loadFloat(params.ratchetDecay)) return true;
        if (!loadFloat(params.strumTime)) return true;
        if (!streamer.readInt32(intVal)) return true;
        params.strumDirection.store(std::clamp(intVal, 0, 3), std::memory_order_relaxed);

        if (!loadFloat(params.velocityLaneSwing)) return true;
        if (!loadFloat(params.gateLaneSwing)) return true;
        if (!loadFloat(params.pitchLaneSwing)) return true;
        if (!loadFloat(params.modifierLaneSwing)) return true;
        if (!loadFloat(params.ratchetLaneSwing)) return true;
        if (!loadFloat(params.conditionLaneSwing)) return true;
        if (!loadFloat(params.chordLaneSwing)) return true;
        if (!loadFloat(params.inversionLaneSwing)) return true;
    }

    // --- v1.5 Part 2: Velocity Curve, Transpose, Per-Lane Length Jitter ---
    {
        if (!streamer.readInt32(intVal)) return true;
        params.velocityCurveType.store(std::clamp(intVal, 0, 3), std::memory_order_relaxed);
        if (!streamer.readFloat(floatVal)) return true;
        params.velocityCurveAmount.store(
            std::clamp(floatVal, 0.0f, 100.0f), std::memory_order_relaxed);
        if (!streamer.readInt32(intVal)) return true;
        params.transpose.store(std::clamp(intVal, -24, 24), std::memory_order_relaxed);

        auto loadJitter = [&](std::atomic<int>& target) -> bool {
            if (!streamer.readInt32(intVal)) return false;
            target.store(std::clamp(intVal, 0, 4), std::memory_order_relaxed);
            return true;
        };
        if (!loadJitter(params.velocityLaneJitter)) return true;
        if (!loadJitter(params.gateLaneJitter)) return true;
        if (!loadJitter(params.pitchLaneJitter)) return true;
        if (!loadJitter(params.modifierLaneJitter)) return true;
        if (!loadJitter(params.ratchetLaneJitter)) return true;
        if (!loadJitter(params.conditionLaneJitter)) return true;
        if (!loadJitter(params.chordLaneJitter)) return true;
        if (!loadJitter(params.inversionLaneJitter)) return true;
    }

    // --- v1.5 Part 3: Note Range Mapping ---
    if (!streamer.readInt32(intVal)) return true;
    params.rangeLow.store(std::clamp(intVal, 0, 127), std::memory_order_relaxed);
    if (!streamer.readInt32(intVal)) return true;
    params.rangeHigh.store(std::clamp(intVal, 0, 127), std::memory_order_relaxed);
    if (!streamer.readInt32(intVal)) return true;
    params.rangeMode.store(std::clamp(intVal, 0, 2), std::memory_order_relaxed);

    // --- v1.5 Part 3: Step Pinning ---
    if (!streamer.readInt32(intVal)) return true;
    params.pinNote.store(std::clamp(intVal, 0, 127), std::memory_order_relaxed);
    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(intVal)) return true;
        params.pinFlags[i].store(intVal ? 1 : 0, std::memory_order_relaxed);
    }

    // --- Markov Chain Mode ---
    // EOF-safe: if Markov data is missing (pre-v1.6 preset), keep defaults.
    // From here, EOF signals a corrupt stream (preset was present but cells are not).
    if (!streamer.readInt32(intVal)) return true;
    params.markovPreset.store(std::clamp(intVal, 0, 5), std::memory_order_relaxed);
    for (size_t i = 0; i < 49; ++i) {
        if (!streamer.readFloat(floatVal)) return false;
        params.markovMatrix[i].store(
            std::clamp(floatVal, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    // --- Per-Lane Speed Curve Depth ---
    // EOF-safe: pre-v1.6 presets don't have this data — keep defaults.
    {
        auto loadDepth = [&](std::atomic<float>& target) -> bool {
            if (!streamer.readFloat(floatVal)) return false;
            target.store(std::clamp(floatVal, 0.0f, 1.0f), std::memory_order_relaxed);
            return true;
        };
        if (!loadDepth(params.velocityLaneSpeedCurveDepth)) return true;
        if (!loadDepth(params.gateLaneSpeedCurveDepth)) return false;
        if (!loadDepth(params.pitchLaneSpeedCurveDepth)) return false;
        if (!loadDepth(params.modifierLaneSpeedCurveDepth)) return false;
        if (!loadDepth(params.ratchetLaneSpeedCurveDepth)) return false;
        if (!loadDepth(params.conditionLaneSpeedCurveDepth)) return false;
        if (!loadDepth(params.chordLaneSpeedCurveDepth)) return false;
        if (!loadDepth(params.inversionLaneSpeedCurveDepth)) return false;
    }

    // Per-lane speed curve point data
    {
        std::lock_guard<std::mutex> lock(params.speedCurveMutex_);
        for (int lane = 0; lane < 8; ++lane) {
            auto& curve = params.speedCurves[static_cast<size_t>(lane)];
            Steinberg::int32 enabledInt = 0;
            if (!streamer.readInt32(enabledInt)) return true;  // EOF = pre-curve preset
            curve.enabled = (enabledInt != 0);

            Steinberg::int32 presetIdx = 0;
            if (!streamer.readInt32(presetIdx)) return false;
            curve.presetIndex = presetIdx;

            Steinberg::int32 numPoints = 0;
            if (!streamer.readInt32(numPoints)) return false;
            numPoints = std::clamp(numPoints, Steinberg::int32{0}, Steinberg::int32{64});

            curve.points.clear();
            curve.points.reserve(static_cast<size_t>(numPoints));
            for (Steinberg::int32 p = 0; p < numPoints; ++p) {
                SpeedCurvePoint pt;
                if (!streamer.readFloat(pt.x)) return false;
                if (!streamer.readFloat(pt.y)) return false;
                if (!streamer.readFloat(pt.cpLeftX)) return false;
                if (!streamer.readFloat(pt.cpLeftY)) return false;
                if (!streamer.readFloat(pt.cpRightX)) return false;
                if (!streamer.readFloat(pt.cpRightY)) return false;
                curve.points.push_back(pt);
            }
        }
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

    // operatingMode (int32 -> normalized: index / 3)
    if (streamer.readInt32(intVal)) {
        int mode = std::clamp(intVal, 0, 3);
        setParam(kArpOperatingModeId, static_cast<double>(mode) / 3.0);
    }
    else return;

    // mode (int32 -> normalized: index / 11 for 12 entries)
    if (streamer.readInt32(intVal))
        setParam(kArpModeId, static_cast<double>(std::clamp(intVal, 0, 11)) / 11.0);
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

    // --- Ratchet Shuffle (was continuous 50-75%, now snapped to 3 positions) ---
    // Backward-compat: old presets store 50-75% float; map to nearest of
    // {Even=50, Triplet=66.67, Dotted=75} → index 0/1/2 → normalized 0.0/0.5/1.0.
    if (!streamer.readFloat(floatVal)) return;
    {
        const float clamped = std::clamp(floatVal, 50.0f, 75.0f);
        int idx;
        if (clamped < 58.33f)       idx = 0;  // < midpoint between 50 and 66.67
        else if (clamped < 70.83f)  idx = 1;  // < midpoint between 66.67 and 75
        else                        idx = 2;
        setParam(kArpRatchetSwingId, static_cast<double>(idx) / 2.0);
    }

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

    // --- MIDI Output ---
    if (streamer.readInt32(iv))
        setParam(kArpMidiOutId, iv != 0 ? 1.0 : 0.0);

    // --- Chord Lane (arp-chord-lane) ---
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

    // Per-lane speed multipliers
    {
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

    // --- v1.5 Features (optional, backward compatible) ---
    if (!streamer.readFloat(floatVal)) return;
    setParam(kArpRatchetDecayId, static_cast<double>(std::clamp(floatVal, 0.0f, 100.0f)) / 100.0);

    if (!streamer.readFloat(floatVal)) return;
    setParam(kArpStrumTimeId, static_cast<double>(std::clamp(floatVal, 0.0f, 100.0f)) / 100.0);

    if (!streamer.readInt32(iv)) return;
    setParam(kArpStrumDirectionId, static_cast<double>(std::clamp(static_cast<int>(iv), 0, 3)) / 3.0);

    {
        static constexpr Steinberg::Vst::ParamID kSwingIds[] = {
            kArpVelocityLaneSwingId, kArpGateLaneSwingId,
            kArpPitchLaneSwingId, kArpModifierLaneSwingId,
            kArpRatchetLaneSwingId, kArpConditionLaneSwingId,
            kArpChordLaneSwingId, kArpInversionLaneSwingId
        };
        for (auto pid : kSwingIds) {
            if (streamer.readFloat(floatVal)) {
                setParam(pid, static_cast<double>(std::clamp(floatVal, 0.0f, 75.0f)) / 75.0);
            }
        }
    }

    // --- v1.5 Part 2: Velocity Curve, Transpose, Per-Lane Length Jitter ---
    if (!streamer.readInt32(iv)) return;
    setParam(kArpVelocityCurveTypeId,
        static_cast<double>(std::clamp(static_cast<int>(iv), 0, 3)) / 3.0);
    if (!streamer.readFloat(floatVal)) return;
    setParam(kArpVelocityCurveAmountId,
        static_cast<double>(std::clamp(floatVal, 0.0f, 100.0f)) / 100.0);
    if (!streamer.readInt32(iv)) return;
    setParam(kArpTransposeId,
        static_cast<double>(std::clamp(static_cast<int>(iv), -24, 24) + 24) / 48.0);

    {
        static constexpr Steinberg::Vst::ParamID kJitterIds[] = {
            kArpVelocityLaneJitterId, kArpGateLaneJitterId,
            kArpPitchLaneJitterId, kArpModifierLaneJitterId,
            kArpRatchetLaneJitterId, kArpConditionLaneJitterId,
            kArpChordLaneJitterId, kArpInversionLaneJitterId
        };
        for (auto pid : kJitterIds) {
            if (streamer.readInt32(iv)) {
                setParam(pid,
                    static_cast<double>(std::clamp(static_cast<int>(iv), 0, 4)) / 4.0);
            }
        }
    }

    // --- v1.5 Part 3: Note Range Mapping ---
    if (!streamer.readInt32(iv)) return;
    setParam(kArpRangeLowId,
        static_cast<double>(std::clamp(static_cast<int>(iv), 0, 127)) / 127.0);
    if (!streamer.readInt32(iv)) return;
    setParam(kArpRangeHighId,
        static_cast<double>(std::clamp(static_cast<int>(iv), 0, 127)) / 127.0);
    if (!streamer.readInt32(iv)) return;
    setParam(kArpRangeModeId,
        static_cast<double>(std::clamp(static_cast<int>(iv), 0, 2)) / 2.0);

    // --- v1.5 Part 3: Step Pinning ---
    if (!streamer.readInt32(iv)) return;
    setParam(kArpPinNoteId,
        static_cast<double>(std::clamp(static_cast<int>(iv), 0, 127)) / 127.0);
    for (int i = 0; i < 32; ++i) {
        if (!streamer.readInt32(iv)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpPinFlagStep0Id + i),
            iv ? 1.0 : 0.0);
    }

    // --- Markov Chain Mode ---
    // Preset selector normalized over 5 steps (6 entries); 49 cell floats.
    // Note: the Controller's setParamNormalized override has
    // suppressMarkovPresetLoad_ set during state recall, so writing the
    // preset value here will NOT clobber the cell values we load after it.
    if (!streamer.readInt32(iv)) return;
    setParam(kArpMarkovPresetId,
        static_cast<double>(std::clamp(static_cast<int>(iv), 0, 5)) / 5.0);
    for (int i = 0; i < 49; ++i) {
        float f = 0.0f;
        if (!streamer.readFloat(f)) return;
        setParam(static_cast<Steinberg::Vst::ParamID>(kArpMarkovCell00Id + i),
            static_cast<double>(std::clamp(f, 0.0f, 1.0f)));
    }

    // --- Per-Lane Speed Curve Depth ---
    {
        static constexpr Steinberg::Vst::ParamID kDepthIds[] = {
            kArpVelocityLaneSpeedCurveDepthId,
            kArpGateLaneSpeedCurveDepthId,
            kArpPitchLaneSpeedCurveDepthId,
            kArpModifierLaneSpeedCurveDepthId,
            kArpRatchetLaneSpeedCurveDepthId,
            kArpConditionLaneSpeedCurveDepthId,
            kArpChordLaneSpeedCurveDepthId,
            kArpInversionLaneSpeedCurveDepthId,
        };
        for (auto depthId : kDepthIds) {
            float f = 0.0f;
            if (!streamer.readFloat(f)) return;
            setParam(depthId, static_cast<double>(std::clamp(f, 0.0f, 1.0f)));
        }
    }
}

} // namespace Gradus
