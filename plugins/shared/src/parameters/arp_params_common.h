// ==============================================================================
// arp_params_common.h — shared arpeggiator param code (Gradus + Ruinae)
// ==============================================================================
// Gradus and Ruinae share arp param IDs 3000-3387 and, historically, independent
// COPIES of the arp parameter code. What is identical in both plugins lives here
// once, templated on the params struct, and is called by both:
//   - saveArpParamsShared: the 50-field shared SAVE prefix (same fields, same
//     order, same writeInt32/writeFloat types, no clamping or version branches);
//   - kLaneSpeedValues: the per-lane speed set (IDs 3380-3387);
//   - handleArpParamChangeShared / formatArpParamShared: denormalization and
//     value text for the shared IDs (R-098).
// A change to any of them now lands in ONE place.
//
// Deliberate per-plugin differences in the shared span stay explicit:
//   - Arp Mode (3001): Ruinae lists 10 modes, Gradus 12 (ArpSharedParamConfig).
//   - Ratchet Swing (3293): continuous 50-75 % in Ruinae, snapped to 3 positions
//     (Ratchet Shuffle) in Gradus (ArpSharedParamConfig).
//   - Value text of the list parameters: Ruinae formats them itself, Gradus lets
//     the StringListParameter do it, so formatArpParamShared leaves them out.
//   - Registration (names, flags) stays per plugin.
//
// The LOAD side is deliberately NOT unified: it has diverged on purpose (e.g.
// `mode` clamps to 0-9 in Ruinae but 0-11 in Gradus, and the two use different
// state-version gates). Merging load would change one plugin's clamping/versioning
// behavior and could corrupt existing presets — so each plugin keeps its own
// loadArpParams. See specs/_architecture_/gotchas.md.
//
// Requirements on the params struct P: it must expose the shared fields below as
// std::atomic members with these exact names (both plugins' ArpeggiatorParams do).
// Requirements on the scope Ids: a struct exposing the plugin's parameter-ID
// enumerators as members (`using enum ParameterIDs;`) and its Arp Scale Type
// display order as `kScaleDisplayOrder`, so these templates name the plugin's own
// constants, never a third copy (each plugin's ArpSharedScope).
// ==============================================================================
#pragma once

#include <krate/dsp/core/note_value.h>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace Krate::Shared {

// Per-lane speed multiplier values (fixed discrete set, IDs 3380-3387).
inline constexpr float kLaneSpeedValues[] = {
    0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 3.0f, 4.0f
};
inline constexpr int kLaneSpeedCount = 10;
inline constexpr int kLaneSpeedDefault = 3; // index 3 = 1.0x
static_assert(std::size(kLaneSpeedValues) == static_cast<std::size_t>(kLaneSpeedCount));

/// The deliberate per-plugin differences in handling the shared arp IDs.
struct ArpSharedParamConfig {
    /// Entries in the Arp Mode list (Ruinae 10, Gradus 12).
    int modeCount;
    /// Ratchet Swing positions in percent, or nullptr for the continuous
    /// 50-75 % control (Ruinae). Gradus passes its 3-entry Ratchet Shuffle set.
    const float* ratchetSwingPositions;
    int ratchetSwingPositionCount;
};

/// Denormalizes a shared arp ID (kArpBaseId..kArpInversionLaneSpeedId) into
/// `params`. Returns false for an ID it does not handle (the caller's own IDs,
/// or a read-only playhead in the span), leaving `params` untouched.
/// Called on the audio thread from processParameterChanges().
template <class Ids, class P>
inline bool handleArpParamChangeShared(P& params, Steinberg::Vst::ParamID id,
                                       Steinberg::Vst::ParamValue value,
                                       const ArpSharedParamConfig& config) {
    constexpr auto kRelaxed = std::memory_order_relaxed;
    // Value 0-1 -> list index 0..count-1.
    const auto listIndex = [value](int count) {
        return std::clamp(static_cast<int>(std::round(value * (count - 1))), 0, count - 1);
    };
    // Value 0-1 -> lane length 1-32 (stepCount 31).
    const auto laneLength = [value] {
        return std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
    };

    switch (id) {
        case Ids::kArpOperatingModeId: params.operatingMode.store(listIndex(4), kRelaxed); return true;
        case Ids::kArpModeId:          params.mode.store(listIndex(config.modeCount), kRelaxed); return true;
        case Ids::kArpOctaveRangeId:   params.octaveRange.store(1 + listIndex(4), kRelaxed); return true;
        case Ids::kArpOctaveModeId:    params.octaveMode.store(listIndex(2), kRelaxed); return true;
        case Ids::kArpTempoSyncId:     params.tempoSync.store(value >= 0.5, kRelaxed); return true;
        case Ids::kArpNoteValueId:
            params.noteValue.store(listIndex(Krate::DSP::kNoteValueDropdownCount), kRelaxed);
            return true;
        case Ids::kArpFreeRateId:
            // Continuous: 0-1 -> 0.5-50 Hz
            params.freeRate.store(std::clamp(static_cast<float>(0.5 + value * 49.5), 0.5f, 50.0f), kRelaxed);
            return true;
        case Ids::kArpGateLengthId:
            // Continuous: 0-1 -> 1-200 %
            params.gateLength.store(std::clamp(static_cast<float>(1.0 + value * 199.0), 1.0f, 200.0f), kRelaxed);
            return true;
        case Ids::kArpSwingId:
            // Continuous: 0-1 -> 0-75 %
            params.swing.store(std::clamp(static_cast<float>(value * 75.0), 0.0f, 75.0f), kRelaxed);
            return true;
        case Ids::kArpLatchModeId:     params.latchMode.store(listIndex(3), kRelaxed); return true;
        case Ids::kArpRetriggerId:     params.retrigger.store(listIndex(3), kRelaxed); return true;

        case Ids::kArpVelocityLaneLengthId:  params.velocityLaneLength.store(laneLength(), kRelaxed); return true;
        case Ids::kArpGateLaneLengthId:      params.gateLaneLength.store(laneLength(), kRelaxed); return true;
        case Ids::kArpPitchLaneLengthId:     params.pitchLaneLength.store(laneLength(), kRelaxed); return true;
        case Ids::kArpModifierLaneLengthId:  params.modifierLaneLength.store(laneLength(), kRelaxed); return true;
        case Ids::kArpRatchetLaneLengthId:   params.ratchetLaneLength.store(laneLength(), kRelaxed); return true;
        case Ids::kArpConditionLaneLengthId: params.conditionLaneLength.store(laneLength(), kRelaxed); return true;
        case Ids::kArpChordLaneLengthId:     params.chordLaneLength.store(laneLength(), kRelaxed); return true;
        case Ids::kArpInversionLaneLengthId: params.inversionLaneLength.store(laneLength(), kRelaxed); return true;

        case Ids::kArpAccentVelocityId:      params.accentVelocity.store(listIndex(128), kRelaxed); return true;
        case Ids::kArpSlideTimeId:
            // Continuous: 0-1 -> 0-500 ms
            params.slideTime.store(std::clamp(static_cast<float>(value * 500.0), 0.0f, 500.0f), kRelaxed);
            return true;

        case Ids::kArpEuclideanEnabledId:  params.euclideanEnabled.store(value >= 0.5, kRelaxed); return true;
        case Ids::kArpEuclideanHitsId:     params.euclideanHits.store(listIndex(33), kRelaxed); return true;
        case Ids::kArpEuclideanStepsId:    params.euclideanSteps.store(2 + listIndex(31), kRelaxed); return true;
        case Ids::kArpEuclideanRotationId: params.euclideanRotation.store(listIndex(32), kRelaxed); return true;

        case Ids::kArpFillToggleId: params.fillToggle.store(value >= 0.5, kRelaxed); return true;

        case Ids::kArpSpiceId:
            params.spice.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), kRelaxed);
            return true;
        case Ids::kArpDiceTriggerId:
            // Discrete 2-step: set on the rising edge (normalized >= 0.5)
            if (value >= 0.5) params.diceTrigger.store(true, kRelaxed);
            return true;
        case Ids::kArpHumanizeId:
            params.humanize.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), kRelaxed);
            return true;

        case Ids::kArpRatchetSwingId:
            if (config.ratchetSwingPositions != nullptr) {
                params.ratchetSwing.store(
                    config.ratchetSwingPositions[listIndex(config.ratchetSwingPositionCount)], kRelaxed);
            } else {
                // Continuous: 0-1 -> 50-75 %
                params.ratchetSwing.store(
                    std::clamp(static_cast<float>(50.0 + value * 25.0), 50.0f, 75.0f), kRelaxed);
            }
            return true;

        case Ids::kArpScaleTypeId: {
            // UI index -> ScaleType enum via the plugin's display-order mapping
            constexpr int kScaleCount = static_cast<int>(std::size(Ids::kScaleDisplayOrder));
            params.scaleType.store(
                Ids::kScaleDisplayOrder[static_cast<std::size_t>(listIndex(kScaleCount))], kRelaxed);
            return true;
        }
        case Ids::kArpRootNoteId:           params.rootNote.store(listIndex(12), kRelaxed); return true;  // C..B
        case Ids::kArpScaleQuantizeInputId: params.scaleQuantizeInput.store(value >= 0.5, kRelaxed); return true;
        case Ids::kArpMidiOutId:            params.midiOut.store(value >= 0.5, kRelaxed); return true;
        case Ids::kArpVoicingModeId:        params.voicingMode.store(listIndex(4), kRelaxed); return true;

        default:
            break;
    }

    // Per-step lanes: [first step ID, last step ID] -> atomic array.
    const auto stepIndex = [id](Steinberg::Vst::ParamID first) { return static_cast<std::size_t>(id - first); };
    if (id >= Ids::kArpVelocityLaneStep0Id && id <= Ids::kArpVelocityLaneStep31Id) {
        params.velocityLaneSteps[stepIndex(Ids::kArpVelocityLaneStep0Id)].store(
            std::clamp(static_cast<float>(value), 0.0f, 1.0f), kRelaxed);
        return true;
    }
    if (id >= Ids::kArpGateLaneStep0Id && id <= Ids::kArpGateLaneStep31Id) {
        params.gateLaneSteps[stepIndex(Ids::kArpGateLaneStep0Id)].store(
            std::clamp(static_cast<float>(0.01 + value * 1.99), 0.01f, 2.0f), kRelaxed);
        return true;
    }
    if (id >= Ids::kArpPitchLaneStep0Id && id <= Ids::kArpPitchLaneStep31Id) {
        params.pitchLaneSteps[stepIndex(Ids::kArpPitchLaneStep0Id)].store(-24 + listIndex(49), kRelaxed);
        return true;
    }
    if (id >= Ids::kArpModifierLaneStep0Id && id <= Ids::kArpModifierLaneStep31Id) {
        params.modifierLaneSteps[stepIndex(Ids::kArpModifierLaneStep0Id)].store(listIndex(256), kRelaxed);
        return true;
    }
    if (id >= Ids::kArpRatchetLaneStep0Id && id <= Ids::kArpRatchetLaneStep31Id) {
        params.ratchetLaneSteps[stepIndex(Ids::kArpRatchetLaneStep0Id)].store(1 + listIndex(4), kRelaxed);
        return true;
    }
    if (id >= Ids::kArpConditionLaneStep0Id && id <= Ids::kArpConditionLaneStep31Id) {
        params.conditionLaneSteps[stepIndex(Ids::kArpConditionLaneStep0Id)].store(listIndex(18), kRelaxed);
        return true;
    }
    if (id >= Ids::kArpChordLaneStep0Id && id <= Ids::kArpChordLaneStep31Id) {
        params.chordLaneSteps[stepIndex(Ids::kArpChordLaneStep0Id)].store(listIndex(5), kRelaxed);
        return true;
    }
    if (id >= Ids::kArpInversionLaneStep0Id && id <= Ids::kArpInversionLaneStep31Id) {
        params.inversionLaneSteps[stepIndex(Ids::kArpInversionLaneStep0Id)].store(listIndex(4), kRelaxed);
        return true;
    }
    if (id >= Ids::kArpVelocityLaneSpeedId && id <= Ids::kArpInversionLaneSpeedId) {
        const float speed = kLaneSpeedValues[listIndex(kLaneSpeedCount)];
        std::atomic<float>* const speeds[] = {
            &params.velocityLaneSpeed, &params.gateLaneSpeed, &params.pitchLaneSpeed,
            &params.modifierLaneSpeed, &params.ratchetLaneSpeed, &params.conditionLaneSpeed,
            &params.chordLaneSpeed, &params.inversionLaneSpeed};
        speeds[stepIndex(Ids::kArpVelocityLaneSpeedId)]->store(speed, kRelaxed);
        return true;
    }
    return false;
}

/// Writes the value text of a shared arp ID whose display is the same in both
/// plugins. Returns false for every other ID: the list parameters whose text
/// differs per plugin (Mode, Octave Mode, Note Value, Latch, Retrigger, Ratchet
/// Swing) and the ones the StringListParameter or host displays.
/// Called on the UI thread from Controller::getParamStringByValue().
template <class Ids>
inline bool formatArpParamShared(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
                                 Steinberg::Vst::String128 string) {
    char text[32];
    const auto write = [string](const char* s) { Steinberg::UString(string, 128).fromAscii(s); };
    const auto listIndex = [value](int count) {
        return std::clamp(static_cast<int>(std::round(value * (count - 1))), 0, count - 1);
    };
    const auto writeSteps = [&](int steps) {
        std::snprintf(text, sizeof(text), "%d steps", steps);
        write(text);
    };

    switch (id) {
        case Ids::kArpOctaveRangeId:
            std::snprintf(text, sizeof(text), "%d", 1 + listIndex(4));
            write(text);
            return true;
        case Ids::kArpFreeRateId:
            std::snprintf(text, sizeof(text), "%.1f Hz", 0.5 + value * 49.5);
            write(text);
            return true;
        case Ids::kArpGateLengthId:
            std::snprintf(text, sizeof(text), "%.0f%%", 1.0 + value * 199.0);
            write(text);
            return true;
        case Ids::kArpSwingId:
            std::snprintf(text, sizeof(text), "%.0f%%", value * 75.0);
            write(text);
            return true;
        case Ids::kArpVelocityLaneLengthId:
        case Ids::kArpGateLaneLengthId:
        case Ids::kArpPitchLaneLengthId:
        case Ids::kArpModifierLaneLengthId:
        case Ids::kArpRatchetLaneLengthId:
            writeSteps(1 + listIndex(32));
            return true;
        case Ids::kArpConditionLaneLengthId: {
            const int len = 1 + listIndex(32);
            std::snprintf(text, sizeof(text), len == 1 ? "%d step" : "%d steps", len);
            write(text);
            return true;
        }
        case Ids::kArpAccentVelocityId:
            std::snprintf(text, sizeof(text), "%d", listIndex(128));
            write(text);
            return true;
        case Ids::kArpSlideTimeId:
            std::snprintf(text, sizeof(text), "%.0f ms", value * 500.0);
            write(text);
            return true;
        case Ids::kArpEuclideanEnabledId:
        case Ids::kArpFillToggleId:
            write(value >= 0.5 ? "On" : "Off");
            return true;
        case Ids::kArpEuclideanHitsId:
            std::snprintf(text, sizeof(text), "%d hits", listIndex(33));
            write(text);
            return true;
        case Ids::kArpEuclideanStepsId:
            writeSteps(2 + listIndex(31));
            return true;
        case Ids::kArpEuclideanRotationId:
            std::snprintf(text, sizeof(text), "%d", listIndex(32));
            write(text);
            return true;
        case Ids::kArpSpiceId:
        case Ids::kArpHumanizeId:
            std::snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            write(text);
            return true;
        case Ids::kArpDiceTriggerId:
            write(value >= 0.5 ? "Roll" : "--");
            return true;
        default:
            break;
    }

    // Velocity lane steps: percentage
    if (id >= Ids::kArpVelocityLaneStep0Id && id <= Ids::kArpVelocityLaneStep31Id) {
        std::snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
        write(text);
        return true;
    }
    // Gate lane steps: multiplier
    if (id >= Ids::kArpGateLaneStep0Id && id <= Ids::kArpGateLaneStep31Id) {
        std::snprintf(text, sizeof(text), "%.2fx", 0.01 + value * 1.99);
        write(text);
        return true;
    }
    // Pitch lane steps: semitone offset
    if (id >= Ids::kArpPitchLaneStep0Id && id <= Ids::kArpPitchLaneStep31Id) {
        const int pitch = -24 + listIndex(49);
        std::snprintf(text, sizeof(text), pitch > 0 ? "+%d st" : "%d st", pitch);
        write(text);
        return true;
    }
    // Modifier lane steps: flag abbreviations (FR-022). ArpStepFlags:
    // kStepActive=0x01, kStepTie=0x02, kStepSlide=0x04, kStepAccent=0x08
    if (id >= Ids::kArpModifierLaneStep0Id && id <= Ids::kArpModifierLaneStep31Id) {
        const int step = listIndex(256);
        constexpr int kActive = 0x01;
        constexpr int kTie    = 0x02;
        constexpr int kSlide  = 0x04;
        constexpr int kAccent = 0x08;
        if ((step & kActive) == 0) {
            write("REST");
        } else if ((step & kTie) != 0) {
            write("TIE");
        } else {
            const bool hasSlide = (step & kSlide) != 0;
            const bool hasAccent = (step & kAccent) != 0;
            if (hasSlide && hasAccent) {
                write("SL AC");
            } else if (hasSlide) {
                write("SL");
            } else if (hasAccent) {
                write("AC");
            } else {
                write("--");
            }
        }
        return true;
    }
    // Ratchet lane steps: "Nx" (074-ratcheting)
    if (id >= Ids::kArpRatchetLaneStep0Id && id <= Ids::kArpRatchetLaneStep31Id) {
        std::snprintf(text, sizeof(text), "%dx", 1 + listIndex(4));
        write(text);
        return true;
    }
    // Condition lane steps: TrigCondition name (076-conditional-trigs)
    if (id >= Ids::kArpConditionLaneStep0Id && id <= Ids::kArpConditionLaneStep31Id) {
        static constexpr const char* kCondNames[] = {
            "Always", "10%", "25%", "50%", "75%", "90%",
            "1:2", "2:2", "1:3", "2:3", "3:3",
            "1:4", "2:4", "3:4", "4:4",
            "1st", "Fill", "!Fill"
        };
        write(kCondNames[listIndex(static_cast<int>(std::size(kCondNames)))]);
        return true;
    }
    // Chord lane steps: ChordType name (arp-chord-lane)
    if (id >= Ids::kArpChordLaneStep0Id && id <= Ids::kArpChordLaneStep31Id) {
        static constexpr const char* kChordNames[] = {"None", "Dyad", "Triad", "7th", "9th"};
        write(kChordNames[listIndex(static_cast<int>(std::size(kChordNames)))]);
        return true;
    }
    // Inversion lane steps: InversionType name (arp-chord-lane)
    if (id >= Ids::kArpInversionLaneStep0Id && id <= Ids::kArpInversionLaneStep31Id) {
        static constexpr const char* kInvNames[] = {"Root", "1st", "2nd", "3rd"};
        write(kInvNames[listIndex(static_cast<int>(std::size(kInvNames)))]);
        return true;
    }
    // Per-lane speed multipliers
    if (id >= Ids::kArpVelocityLaneSpeedId && id <= Ids::kArpInversionLaneSpeedId) {
        const float speed = kLaneSpeedValues[listIndex(kLaneSpeedCount)];
        if (speed == static_cast<float>(static_cast<int>(speed))) {
            std::snprintf(text, sizeof(text), "%dx", static_cast<int>(speed));
        } else {
            std::snprintf(text, sizeof(text), "%.2fx", static_cast<double>(speed));
        }
        write(text);
        return true;
    }
    return false;
}

// Writes the 50-field shared arp prefix in the canonical order. Gradus appends its
// plugin-specific fields AFTER calling this; Ruinae writes nothing else.
template <class P>
inline void saveArpParamsShared(const P& params, Steinberg::IBStreamer& streamer) {
    // 11 base fields: operatingMode, mode, octaveRange, octaveMode, tempoSync,
    // noteValue (int32), freeRate, gateLength, swing (float), latchMode, retrigger.
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
}

}  // namespace Krate::Shared
