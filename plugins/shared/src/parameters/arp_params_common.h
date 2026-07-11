// ==============================================================================
// arp_params_common.h — shared arpeggiator param serialization (Gradus + Ruinae)
// ==============================================================================
// Gradus and Ruinae share arp param IDs 3000-3372 and, historically, independent
// COPIES of the arp save/load code. The SAVE side is byte-identical between the two
// plugins for the shared 50-field prefix (verified: same fields, same order, same
// writeInt32/writeFloat types, no clamping or version branches) — so it is lifted
// here once, templated on the params struct, and called by both plugins. A change
// to the shared save format now lands in ONE place.
//
// The LOAD side is deliberately NOT unified: it has diverged on purpose (e.g.
// `mode` clamps to 0-9 in Ruinae but 0-11 in Gradus, and the two use different
// state-version gates). Merging load would change one plugin's clamping/versioning
// behavior and could corrupt existing presets — so each plugin keeps its own
// loadArpParams. See specs/_architecture_/gotchas.md.
//
// Requirements on the params struct P: it must expose the shared fields below as
// std::atomic members with these exact names (both plugins' ArpeggiatorParams do).
// ==============================================================================
#pragma once

#include "base/source/fstreamer.h"

#include <atomic>

namespace Krate::Shared {

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
