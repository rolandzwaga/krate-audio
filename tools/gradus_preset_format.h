#pragma once

// ==============================================================================
// Gradus Preset Format Header
// ==============================================================================
// Shared format definitions for the Gradus preset generator.
// The arp state layout matches Ruinae's saveArpParams() binary order exactly,
// enabling preset sharing between the two plugins.
//
// When adding new fields to the processor state, update the corresponding
// struct here AND its serialize() method.
// ==============================================================================

#include <vector>
#include <cstdint>
#include <array>

namespace GradusFormat {

// ==============================================================================
// Binary Writer (matches IBStreamer little-endian format)
// ==============================================================================

class BinaryWriter {
public:
    std::vector<uint8_t> data;

    void writeInt32(int32_t val) {
        auto bytes = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), bytes, bytes + 4);
    }

    void writeFloat(float val) {
        auto bytes = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), bytes, bytes + 4);
    }
};

// ==============================================================================
// Constants
// ==============================================================================

static constexpr int32_t kStateVersion = 1;

// Arp step flags (must match ArpStepFlags in arpeggiator_core.h)
static constexpr int kStepActive = 0x01;
static constexpr int kStepTie    = 0x02;
static constexpr int kStepSlide  = 0x04;
static constexpr int kStepAccent = 0x08;

// Note value default index (1/8 note = index 10)
static constexpr int kNoteValueDefaultIndex = 10;

// ==============================================================================
// ArpState (binary layout matches Ruinae's saveArpParams exactly)
// ==============================================================================

struct ArpState {
    // Base params (11 values)
    int32_t operatingMode = 1; // Always MIDI in Gradus (kArpMIDI)
    int32_t mode = 0; // Up
    int32_t octaveRange = 1;
    int32_t octaveMode = 0; // Sequential
    int32_t tempoSync = 1; // true
    int32_t noteValue = kNoteValueDefaultIndex; // 1/8 note
    float freeRate = 4.0f;
    float gateLength = 80.0f;
    float swing = 0.0f;
    int32_t latchMode = 0; // Off
    int32_t retrigger = 0; // Off

    // Velocity lane
    int32_t velocityLaneLength = 16;
    float velocityLaneSteps[32]{};

    // Gate lane
    int32_t gateLaneLength = 16;
    float gateLaneSteps[32]{};

    // Pitch lane
    int32_t pitchLaneLength = 16;
    int32_t pitchLaneSteps[32]{}; // all 0

    // Modifier lane
    int32_t modifierLaneLength = 16;
    int32_t modifierLaneSteps[32]{};
    int32_t accentVelocity = 30;
    float slideTime = 60.0f;

    // Ratchet lane
    int32_t ratchetLaneLength = 16;
    int32_t ratchetLaneSteps[32]{};

    // Euclidean
    int32_t euclideanEnabled = 0;
    int32_t euclideanHits = 4;
    int32_t euclideanSteps = 8;
    int32_t euclideanRotation = 0;

    // Condition lane
    int32_t conditionLaneLength = 16;
    int32_t conditionLaneSteps[32]{}; // all 0 = Always
    int32_t fillToggle = 0;

    // Spice/Humanize
    float spice = 0.0f;
    float humanize = 0.0f;

    // Ratchet swing
    float ratchetSwing = 50.0f;

    // Scale mode
    int32_t scaleType = 8;          // Chromatic
    int32_t rootNote = 0;           // C
    int32_t scaleQuantizeInput = 0; // off

    // MIDI output
    int32_t midiOut = 0;            // irrelevant for Gradus but kept for binary compat

    // Chord lane
    int32_t chordLaneLength = 1;
    int32_t chordLaneSteps[32]{};   // all 0 = None

    // Inversion lane
    int32_t inversionLaneLength = 1;
    int32_t inversionLaneSteps[32]{}; // all 0 = Root

    // Voicing mode
    int32_t voicingMode = 0;        // Close

    ArpState() {
        for (auto& step : velocityLaneSteps) step = 1.0f;
        for (auto& step : gateLaneSteps) step = 1.0f;
        for (auto& step : modifierLaneSteps) step = kStepActive;
        for (auto& step : ratchetLaneSteps) step = 1;
    }

    void serialize(BinaryWriter& w) const {
        w.writeInt32(operatingMode);
        w.writeInt32(mode);
        w.writeInt32(octaveRange);
        w.writeInt32(octaveMode);
        w.writeInt32(tempoSync);
        w.writeInt32(noteValue);
        w.writeFloat(freeRate);
        w.writeFloat(gateLength);
        w.writeFloat(swing);
        w.writeInt32(latchMode);
        w.writeInt32(retrigger);

        w.writeInt32(velocityLaneLength);
        for (int i = 0; i < 32; ++i) w.writeFloat(velocityLaneSteps[i]);

        w.writeInt32(gateLaneLength);
        for (int i = 0; i < 32; ++i) w.writeFloat(gateLaneSteps[i]);

        w.writeInt32(pitchLaneLength);
        for (int i = 0; i < 32; ++i) w.writeInt32(pitchLaneSteps[i]);

        w.writeInt32(modifierLaneLength);
        for (int i = 0; i < 32; ++i) w.writeInt32(modifierLaneSteps[i]);
        w.writeInt32(accentVelocity);
        w.writeFloat(slideTime);

        w.writeInt32(ratchetLaneLength);
        for (int i = 0; i < 32; ++i) w.writeInt32(ratchetLaneSteps[i]);

        w.writeInt32(euclideanEnabled);
        w.writeInt32(euclideanHits);
        w.writeInt32(euclideanSteps);
        w.writeInt32(euclideanRotation);

        w.writeInt32(conditionLaneLength);
        for (int i = 0; i < 32; ++i) w.writeInt32(conditionLaneSteps[i]);
        w.writeInt32(fillToggle);

        w.writeFloat(spice);
        w.writeFloat(humanize);

        w.writeFloat(ratchetSwing);

        w.writeInt32(scaleType);
        w.writeInt32(rootNote);
        w.writeInt32(scaleQuantizeInput);

        w.writeInt32(midiOut);

        w.writeInt32(chordLaneLength);
        for (int i = 0; i < 32; ++i) w.writeInt32(chordLaneSteps[i]);

        w.writeInt32(inversionLaneLength);
        for (int i = 0; i < 32; ++i) w.writeInt32(inversionLaneSteps[i]);

        w.writeInt32(voicingMode);
    }
};

// ==============================================================================
// AuditionState (Gradus-specific)
// ==============================================================================

struct AuditionState {
    int32_t enabled = 1;    // on
    float volume = 0.7f;
    int32_t waveform = 0;   // Sine
    float decay = 200.0f;

    void serialize(BinaryWriter& w) const {
        w.writeInt32(enabled);
        w.writeFloat(volume);
        w.writeInt32(waveform);
        w.writeFloat(decay);
    }
};

// ==============================================================================
// Complete Gradus Preset State
// ==============================================================================

struct GradusPresetState {
    ArpState arp;

    std::vector<uint8_t> serialize() const {
        BinaryWriter w;

        // 1. State version
        w.writeInt32(kStateVersion);

        // 2. Arp state (audition params are session-only, not in presets)
        arp.serialize(w);

        return w.data;
    }
};

} // namespace GradusFormat
