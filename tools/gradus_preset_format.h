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

    // Per-lane speed multipliers (8 floats, defaults 1.0x)
    float velocityLaneSpeed = 1.0f;
    float gateLaneSpeed = 1.0f;
    float pitchLaneSpeed = 1.0f;
    float modifierLaneSpeed = 1.0f;
    float ratchetLaneSpeed = 1.0f;
    float conditionLaneSpeed = 1.0f;
    float chordLaneSpeed = 1.0f;
    float inversionLaneSpeed = 1.0f;

    // --- v1.5: Ratchet Decay + Strum Mode + Per-Lane Swing ---
    float ratchetDecay = 0.0f;      // 0-100%
    float strumTime = 0.0f;         // 0-100 ms
    int32_t strumDirection = 0;     // 0=Up, 1=Down, 2=Random, 3=Alternate
    float velocityLaneSwing = 0.0f;
    float gateLaneSwing = 0.0f;
    float pitchLaneSwing = 0.0f;
    float modifierLaneSwing = 0.0f;
    float ratchetLaneSwing = 0.0f;
    float conditionLaneSwing = 0.0f;
    float chordLaneSwing = 0.0f;
    float inversionLaneSwing = 0.0f;

    // --- v1.5 Part 2: Velocity Curve + Transpose + Per-Lane Length Jitter ---
    int32_t velocityCurveType = 0;       // 0=Linear, 1=Exp, 2=Log, 3=S-Curve
    float velocityCurveAmount = 0.0f;    // 0-100%
    int32_t transpose = 0;               // -24 to +24 (semitones or scale degrees)
    int32_t velocityLaneJitter = 0;      // 0-4 steps
    int32_t gateLaneJitter = 0;
    int32_t pitchLaneJitter = 0;
    int32_t modifierLaneJitter = 0;
    int32_t ratchetLaneJitter = 0;
    int32_t conditionLaneJitter = 0;
    int32_t chordLaneJitter = 0;
    int32_t inversionLaneJitter = 0;

    // --- v1.5 Part 3: Note Range Mapping ---
    int32_t rangeLow = 0;        // MIDI floor (0-127)
    int32_t rangeHigh = 127;     // MIDI ceiling (0-127)
    int32_t rangeMode = 1;       // 0=Wrap, 1=Clamp, 2=Skip

    // --- v1.5 Part 3: Step Pinning ---
    int32_t pinNote = 60;                // MIDI note, default C4
    int32_t pinFlags[32]{};              // per-step 0/1

    // --- v1.6: Markov Chain ---
    int32_t markovPreset = 0;            // 0=Uniform..4=Classical, 5=Custom
    float markovMatrix[49]{};            // 7x7 row-major

    // --- v1.6: Per-Lane Speed Curve Depth ---
    float velocityLaneSpeedCurveDepth = 0.5f;
    float gateLaneSpeedCurveDepth = 0.5f;
    float pitchLaneSpeedCurveDepth = 0.5f;
    float modifierLaneSpeedCurveDepth = 0.5f;
    float ratchetLaneSpeedCurveDepth = 0.5f;
    float conditionLaneSpeedCurveDepth = 0.5f;
    float chordLaneSpeedCurveDepth = 0.5f;
    float inversionLaneSpeedCurveDepth = 0.5f;

    // --- v1.6: Per-Lane Speed Curve Point Data ---
    struct SpeedCurvePoint {
        float x = 0.0f, y = 0.5f;
        float cpLeftX = 0.0f, cpLeftY = 0.5f;
        float cpRightX = 0.0f, cpRightY = 0.5f;
    };
    struct SpeedCurve {
        int32_t enabled = 0;
        int32_t presetIndex = 0;    // 0=Flat, 1=Sine, 2=Triangle, 3=SawUp, 4=SawDown, 5=Square, 6=Exp
        std::vector<SpeedCurvePoint> points;
        SpeedCurve() {
            // Default flat: two endpoints at y=0.5
            points.push_back({0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.5f});
            points.push_back({1.0f, 0.5f, 1.0f, 0.5f, 1.0f, 0.5f});
        }
    };
    SpeedCurve speedCurves[8];

    // --- v1.7: MIDI Delay Lane ---
    int32_t midiDelayLaneLength = 16;
    int32_t midiDelayTimeModeSteps[32]{};       // 0=Free, 1=Synced (all default synced)
    float   midiDelayTimeSteps[32]{};           // normalized 0-1 (default ~1/8 note)
    int32_t midiDelayFeedbackSteps[32]{};       // 0-16 repeats
    float   midiDelayVelDecaySteps[32]{};       // 0-1
    int32_t midiDelayPitchShiftSteps[32]{};     // -24 to +24
    float   midiDelayGateScaleSteps[32]{};      // 0-1 normalized (maps to 0.1-2.0x)
    int32_t midiDelayActiveSteps[32]{};         // 0 or 1
    float   midiDelayLaneSpeed = 1.0f;
    float   midiDelayLaneSwing = 0.0f;
    int32_t midiDelayLaneJitter = 0;
    float   midiDelayLaneSpeedCurveDepth = 0.5f;

    ArpState() {
        for (auto& step : velocityLaneSteps) step = 1.0f;
        for (auto& step : gateLaneSteps) step = 1.0f;
        for (auto& step : modifierLaneSteps) step = kStepActive;
        for (auto& step : ratchetLaneSteps) step = 1;
        // Default Markov matrix: uniform 1/7 probabilities
        for (auto& cell : markovMatrix) cell = 1.0f / 7.0f;
        // Default MIDI delay: all synced, 1/8 note, inactive
        for (auto& v : midiDelayTimeModeSteps) v = 1;     // synced
        for (auto& v : midiDelayTimeSteps) v = 10.0f / 29.0f; // ~1/8 note
        for (auto& v : midiDelayFeedbackSteps) v = 3;     // 3 repeats
        for (auto& v : midiDelayVelDecaySteps) v = 0.5f;  // 50% decay
        for (auto& v : midiDelayPitchShiftSteps) v = 0;   // no shift
        for (auto& v : midiDelayGateScaleSteps) v = (1.0f - 0.1f) / 1.9f; // 1.0x
        for (auto& v : midiDelayActiveSteps) v = 0;       // inactive
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

        // Per-lane speed multipliers
        w.writeFloat(velocityLaneSpeed);
        w.writeFloat(gateLaneSpeed);
        w.writeFloat(pitchLaneSpeed);
        w.writeFloat(modifierLaneSpeed);
        w.writeFloat(ratchetLaneSpeed);
        w.writeFloat(conditionLaneSpeed);
        w.writeFloat(chordLaneSpeed);
        w.writeFloat(inversionLaneSpeed);

        // v1.5 Features: Ratchet Decay, Strum, Per-Lane Swing
        w.writeFloat(ratchetDecay);
        w.writeFloat(strumTime);
        w.writeInt32(strumDirection);
        w.writeFloat(velocityLaneSwing);
        w.writeFloat(gateLaneSwing);
        w.writeFloat(pitchLaneSwing);
        w.writeFloat(modifierLaneSwing);
        w.writeFloat(ratchetLaneSwing);
        w.writeFloat(conditionLaneSwing);
        w.writeFloat(chordLaneSwing);
        w.writeFloat(inversionLaneSwing);

        // v1.5 Part 2: Velocity Curve, Transpose, Per-Lane Length Jitter
        w.writeInt32(velocityCurveType);
        w.writeFloat(velocityCurveAmount);
        w.writeInt32(transpose);
        w.writeInt32(velocityLaneJitter);
        w.writeInt32(gateLaneJitter);
        w.writeInt32(pitchLaneJitter);
        w.writeInt32(modifierLaneJitter);
        w.writeInt32(ratchetLaneJitter);
        w.writeInt32(conditionLaneJitter);
        w.writeInt32(chordLaneJitter);
        w.writeInt32(inversionLaneJitter);

        // v1.5 Part 3: Note Range Mapping
        w.writeInt32(rangeLow);
        w.writeInt32(rangeHigh);
        w.writeInt32(rangeMode);

        // v1.5 Part 3: Step Pinning
        w.writeInt32(pinNote);
        for (int i = 0; i < 32; ++i) w.writeInt32(pinFlags[i]);

        // v1.6: Markov Chain
        w.writeInt32(markovPreset);
        for (int i = 0; i < 49; ++i) w.writeFloat(markovMatrix[i]);

        // v1.6: Per-Lane Speed Curve Depth
        w.writeFloat(velocityLaneSpeedCurveDepth);
        w.writeFloat(gateLaneSpeedCurveDepth);
        w.writeFloat(pitchLaneSpeedCurveDepth);
        w.writeFloat(modifierLaneSpeedCurveDepth);
        w.writeFloat(ratchetLaneSpeedCurveDepth);
        w.writeFloat(conditionLaneSpeedCurveDepth);
        w.writeFloat(chordLaneSpeedCurveDepth);
        w.writeFloat(inversionLaneSpeedCurveDepth);

        // v1.6: Per-Lane Speed Curve Point Data
        for (int lane = 0; lane < 8; ++lane) {
            const auto& curve = speedCurves[lane];
            w.writeInt32(curve.enabled);
            w.writeInt32(curve.presetIndex);
            w.writeInt32(static_cast<int32_t>(curve.points.size()));
            for (const auto& pt : curve.points) {
                w.writeFloat(pt.x);
                w.writeFloat(pt.y);
                w.writeFloat(pt.cpLeftX);
                w.writeFloat(pt.cpLeftY);
                w.writeFloat(pt.cpRightX);
                w.writeFloat(pt.cpRightY);
            }
        }

        // v1.7: MIDI Delay Lane
        w.writeInt32(midiDelayLaneLength);
        for (int i = 0; i < 32; ++i) w.writeInt32(midiDelayTimeModeSteps[i]);
        for (int i = 0; i < 32; ++i) w.writeFloat(midiDelayTimeSteps[i]);
        for (int i = 0; i < 32; ++i) w.writeInt32(midiDelayFeedbackSteps[i]);
        for (int i = 0; i < 32; ++i) w.writeFloat(midiDelayVelDecaySteps[i]);
        for (int i = 0; i < 32; ++i) w.writeInt32(midiDelayPitchShiftSteps[i]);
        for (int i = 0; i < 32; ++i) w.writeFloat(midiDelayGateScaleSteps[i]);
        for (int i = 0; i < 32; ++i) w.writeInt32(midiDelayActiveSteps[i]);
        w.writeFloat(midiDelayLaneSpeed);
        w.writeFloat(midiDelayLaneSwing);
        w.writeInt32(midiDelayLaneJitter);
        w.writeFloat(midiDelayLaneSpeedCurveDepth);
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
