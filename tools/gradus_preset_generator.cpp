// ==============================================================================
// Factory Preset Generator for Gradus
// ==============================================================================
// Generates .vstpreset files matching the Processor::getState() binary format.
// Run this tool once during development to create factory presets.
//
// Arp pattern data is ported directly from the Ruinae preset generator,
// with all synth-specific code (oscillators, filters, effects, mod matrix,
// voice routes, LFOs) removed.
//
// Reference: tools/ruinae_preset_generator.cpp (source of arp patterns)
// Reference: tools/gradus_preset_format.h (Gradus binary format)
// ==============================================================================

#include "gradus_preset_format.h"

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

using namespace GradusFormat;

// ==============================================================================
// Constants (preset-generator-specific)
// ==============================================================================

// kProcessorUID(0x7A1B2C3D, 0x4E5F6A7B, 0x8C9D0E1F, 0x2A3B4C5D)
const char kClassIdAscii[33] = "7A1B2C3D4E5F6A7B8C9D0E1F2A3B4C5D";

// ==============================================================================
// VST3 Preset File Writer
// ==============================================================================

void writeLE32(std::ofstream& f, uint32_t val) {
    f.write(reinterpret_cast<const char*>(&val), 4);
}

void writeLE64(std::ofstream& f, int64_t val) {
    f.write(reinterpret_cast<const char*>(&val), 8);
}

bool writeVstPreset(const std::filesystem::path& path,
                    const std::vector<uint8_t>& componentState) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "Failed to create: " << path << std::endl;
        return false;
    }

    const int64_t kHeaderSize = 48;
    int64_t compDataOffset = kHeaderSize;
    int64_t compDataSize = static_cast<int64_t>(componentState.size());
    int64_t listOffset = compDataOffset + compDataSize;

    // Header
    f.write("VST3", 4);
    writeLE32(f, 1);
    f.write(kClassIdAscii, 32);
    writeLE64(f, listOffset);

    // Component State Data
    f.write(reinterpret_cast<const char*>(componentState.data()), compDataSize);

    // Chunk List
    f.write("List", 4);
    writeLE32(f, 1);
    f.write("Comp", 4);
    writeLE64(f, compDataOffset);
    writeLE64(f, compDataSize);

    f.close();
    return true;
}

// ==============================================================================
// Preset Definition
// ==============================================================================

struct PresetDef {
    std::string name;
    std::string category;
    GradusPresetState state;
};

// ==============================================================================
// Arp Helper Functions
// ==============================================================================

void setArpEnabled(GradusPresetState& s, bool enabled) {
    s.arp.operatingMode = enabled ? 1 : 0;
}

void setArpMode(GradusPresetState& s, int32_t mode) {
    s.arp.mode = mode;
}

void setArpRate(GradusPresetState& s, int32_t noteValueIndex) {
    s.arp.noteValue = noteValueIndex;
}

void setArpGateLength(GradusPresetState& s, float gateLength) {
    s.arp.gateLength = gateLength;
}

void setArpSwing(GradusPresetState& s, float swing) {
    s.arp.swing = swing;
}

void setTempoSync(GradusPresetState& s, bool tempoSync) {
    s.arp.tempoSync = tempoSync ? 1 : 0;
}

void setVelocityLane(GradusPresetState& s, int32_t length,
                     const float* steps) {
    s.arp.velocityLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.velocityLaneSteps[i] = steps[i];
}

void setGateLane(GradusPresetState& s, int32_t length,
                 const float* steps) {
    s.arp.gateLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.gateLaneSteps[i] = steps[i];
}

void setPitchLane(GradusPresetState& s, int32_t length,
                  const int32_t* steps) {
    s.arp.pitchLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.pitchLaneSteps[i] = steps[i];
}

void setModifierLane(GradusPresetState& s, int32_t length,
                     const int32_t* steps, int32_t accentVelocity,
                     float slideTime) {
    s.arp.modifierLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.modifierLaneSteps[i] = steps[i];
    s.arp.accentVelocity = accentVelocity;
    s.arp.slideTime = slideTime;
}

void setRatchetLane(GradusPresetState& s, int32_t length,
                    const int32_t* steps) {
    s.arp.ratchetLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.ratchetLaneSteps[i] = steps[i];
}

void setConditionLane(GradusPresetState& s, int32_t length,
                      const int32_t* steps, int32_t fillToggle) {
    s.arp.conditionLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.conditionLaneSteps[i] = steps[i];
    s.arp.fillToggle = fillToggle;
}

// Chord type constants
static constexpr int32_t kChordNone  = 0;
static constexpr int32_t kChordDyad  = 1;
static constexpr int32_t kChordTriad = 2;
static constexpr int32_t kChord7th   = 3;
static constexpr int32_t kChord9th   = 4;

// Inversion constants
static constexpr int32_t kInvRoot = 0;
static constexpr int32_t kInv1st  = 1;
static constexpr int32_t kInv2nd  = 2;
static constexpr int32_t kInv3rd  = 3;

void setChordLane(GradusPresetState& s, int32_t length,
                  const int32_t* steps) {
    s.arp.chordLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.chordLaneSteps[i] = steps[i];
}

void setInversionLane(GradusPresetState& s, int32_t length,
                      const int32_t* steps) {
    s.arp.inversionLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.inversionLaneSteps[i] = steps[i];
}

void setVoicingMode(GradusPresetState& s, int32_t mode) {
    s.arp.voicingMode = mode;
}

void setEuclidean(GradusPresetState& s, bool enabled, int32_t hits,
                  int32_t steps, int32_t rotation) {
    s.arp.euclideanEnabled = enabled ? 1 : 0;
    s.arp.euclideanHits = hits;
    s.arp.euclideanSteps = steps;
    s.arp.euclideanRotation = rotation;
}

// ==============================================================================
// v1.5 Helpers
// ==============================================================================

// Per-lane speed multipliers (0.25x-4x, 10 discrete values)
void setLaneSpeeds(GradusPresetState& s,
                   float vel, float gate, float pitch, float mod,
                   float ratchet, float cond, float chord, float inv) {
    s.arp.velocityLaneSpeed  = vel;
    s.arp.gateLaneSpeed      = gate;
    s.arp.pitchLaneSpeed     = pitch;
    s.arp.modifierLaneSpeed  = mod;
    s.arp.ratchetLaneSpeed   = ratchet;
    s.arp.conditionLaneSpeed = cond;
    s.arp.chordLaneSpeed     = chord;
    s.arp.inversionLaneSpeed = inv;
}

// Per-lane swing (0-75%)
void setLaneSwings(GradusPresetState& s,
                   float vel, float gate, float pitch, float mod,
                   float ratchet, float cond, float chord, float inv) {
    s.arp.velocityLaneSwing  = vel;
    s.arp.gateLaneSwing      = gate;
    s.arp.pitchLaneSwing     = pitch;
    s.arp.modifierLaneSwing  = mod;
    s.arp.ratchetLaneSwing   = ratchet;
    s.arp.conditionLaneSwing = cond;
    s.arp.chordLaneSwing     = chord;
    s.arp.inversionLaneSwing = inv;
}

// Per-lane length jitter (0-4 steps)
void setLaneJitters(GradusPresetState& s,
                    int vel, int gate, int pitch, int mod,
                    int ratchet, int cond, int chord, int inv) {
    s.arp.velocityLaneJitter  = vel;
    s.arp.gateLaneJitter      = gate;
    s.arp.pitchLaneJitter     = pitch;
    s.arp.modifierLaneJitter  = mod;
    s.arp.ratchetLaneJitter   = ratchet;
    s.arp.conditionLaneJitter = cond;
    s.arp.chordLaneJitter     = chord;
    s.arp.inversionLaneJitter = inv;
}

void setRatchetDecay(GradusPresetState& s, float percent) {
    s.arp.ratchetDecay = percent;
}

void setStrum(GradusPresetState& s, float timeMs, int direction) {
    s.arp.strumTime = timeMs;
    s.arp.strumDirection = direction;
}

void setRatchetShuffle(GradusPresetState& s, float percent) {
    // 50=Even, 66.67=Triplet, 75=Dotted
    s.arp.ratchetSwing = percent;
}

void setVelocityCurve(GradusPresetState& s, int type, float amount) {
    s.arp.velocityCurveType = type;
    s.arp.velocityCurveAmount = amount;
}

void setTranspose(GradusPresetState& s, int steps) {
    s.arp.transpose = steps;
}

void setNoteRange(GradusPresetState& s, int low, int high, int mode) {
    s.arp.rangeLow = low;
    s.arp.rangeHigh = high;
    s.arp.rangeMode = mode;
}

void setPinNote(GradusPresetState& s, int midiNote) {
    s.arp.pinNote = midiNote;
}

void setPinFlags(GradusPresetState& s, int length, const int* flags) {
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.pinFlags[i] = flags[i];
}

void setScale(GradusPresetState& s, int scaleType, int rootNote) {
    s.arp.scaleType = scaleType;
    s.arp.rootNote = rootNote;
}

// ==============================================================================
// Constants
// ==============================================================================

// v1.5 Strum directions
static constexpr int32_t kStrumUp        = 0;
static constexpr int32_t kStrumDown      = 1;
static constexpr int32_t kStrumRandom    = 2;
static constexpr int32_t kStrumAlternate = 3;

// v1.5 Velocity curve types
static constexpr int32_t kCurveLinear = 0;
static constexpr int32_t kCurveExp    = 1;
static constexpr int32_t kCurveLog    = 2;
static constexpr int32_t kCurveS      = 3;

// v1.5 Range modes
static constexpr int32_t kRangeWrap  = 0;
static constexpr int32_t kRangeClamp = 1;
static constexpr int32_t kRangeSkip  = 2;

// v1.5 Ratchet shuffle values
static constexpr float kShuffleEven    = 50.0f;
static constexpr float kShuffleTriplet = 66.6667f;
static constexpr float kShuffleDotted  = 75.0f;

// Scale type indices (from kArpScaleEnumToDisplay)
// Major=0, NatMinor=1, HarmMinor=2, MelMinor=3, Dorian=4, Phrygian=5,
// Lydian=6, Mixolydian=7, Chromatic=8, Locrian=9, MajorPenta=10,
// MinorPenta=11, Blues=12, WholeTone=13, DimWH=14, DimHW=15
static constexpr int32_t kScaleMajor      = 0;
static constexpr int32_t kScaleNatMinor   = 1;
static constexpr int32_t kScaleDorian     = 4;
static constexpr int32_t kScalePhrygian   = 5;
static constexpr int32_t kScaleMixolydian = 7;
static constexpr int32_t kScaleChromatic  = 8;
static constexpr int32_t kScaleMajorPenta = 10;
static constexpr int32_t kScaleMinorPenta = 11;
static constexpr int32_t kScaleBlues      = 12;

// Note names for root note
static constexpr int32_t kRootC = 0;
static constexpr int32_t kRootD = 2;
static constexpr int32_t kRootE = 4;
static constexpr int32_t kRootF = 5;
static constexpr int32_t kRootG = 7;
static constexpr int32_t kRootA = 9;

// ==============================================================================
// Constants
// ==============================================================================

// Arp mode constants
static constexpr int32_t kModeUp       = 0;
static constexpr int32_t kModeDown     = 1;
static constexpr int32_t kModeUpDown   = 2;
static constexpr int32_t kModeRandom   = 6;
static constexpr int32_t kModeAsPlayed = 8;

// Note value index constants
static constexpr int32_t kNote1_16  = 7;
static constexpr int32_t kNote1_8   = 10;
static constexpr int32_t kNote1_8T  = 9;
static constexpr int32_t kNote1_8D  = 11;
static constexpr int32_t kNote1_4   = 13;
static constexpr int32_t kNote1_4T  = 12;
static constexpr int32_t kNote1_16D = 8;

// Condition constants
static constexpr int32_t kCondAlways = 0;
static constexpr int32_t kCondProb10 = 1;
static constexpr int32_t kCondProb25 = 2;
static constexpr int32_t kCondProb50 = 3;
static constexpr int32_t kCondProb75 = 4;
static constexpr int32_t kCondProb90 = 5;
static constexpr int32_t kCondFill   = 16;

// ==============================================================================
// Factory Preset Definitions
// ==============================================================================

std::vector<PresetDef> createAllPresets() {
    std::vector<PresetDef> presets;

    // Additional mode constants not in the header constants above
    static constexpr int32_t kModeDownUp   = 3;
    static constexpr int32_t kModeConverge = 4;
    static constexpr int32_t kModeDiverge  = 5;
    static constexpr int32_t kModeWalk     = 7;
    static constexpr int32_t kModeChord    = 9;
    static constexpr int32_t kModeGravity  = 10;  // v1.5

    // ========================================================================
    // ARP CLASSIC (5 presets)
    // Traditional patterns with velocity accents, gate variation, humanize,
    // modifier ties/slides.
    // ========================================================================

    // 1. "Rising Accents" — Up 1/16 with strong downbeat accents, gate variation
    {
        PresetDef p;
        p.name = "Rising Accents";
        p.category = "Arp Classic";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 75.0f);
        setArpSwing(p.state, 10.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.12f;
        // Velocity: accented downbeats, ghost notes between
        float vel16[] = {
            1.0f, 0.45f, 0.6f, 0.5f,  0.9f, 0.4f, 0.55f, 0.5f,
            0.85f, 0.42f, 0.58f, 0.48f, 0.95f, 0.4f, 0.62f, 0.52f
        };
        setVelocityLane(p.state, 16, vel16);
        // Gate: staccato on ghosts, legato on accents
        float gate16[] = {
            1.3f, 0.4f, 0.7f, 0.5f,  1.2f, 0.35f, 0.65f, 0.5f,
            1.1f, 0.4f, 0.6f, 0.45f, 1.25f, 0.38f, 0.7f, 0.48f
        };
        setGateLane(p.state, 16, gate16);
        // Modifier: tie across beats 3-4 and 7-8
        int32_t mod16[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive | kStepTie, kStepActive,
            kStepActive | kStepAccent, kStepActive, kStepActive, kStepActive,
            kStepActive | kStepAccent, kStepActive, kStepActive | kStepTie, kStepActive,
            kStepActive | kStepAccent, kStepActive, kStepActive, kStepActive
        };
        setModifierLane(p.state, 16, mod16, 40, 50.0f);
        presets.push_back(std::move(p));
    }

    // 2. "Falling Legato" — Down 1/8 with ties, smooth velocity curves
    {
        PresetDef p;
        p.name = "Falling Legato";
        p.category = "Arp Classic";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 110.0f);
        setArpSwing(p.state, 5.0f);
        p.state.arp.octaveRange = 3;
        p.state.arp.humanize = 0.08f;
        // Velocity: descending swell then reset
        float vel8[] = {0.95f, 0.85f, 0.75f, 0.65f, 1.0f, 0.88f, 0.7f, 0.55f};
        setVelocityLane(p.state, 8, vel8);
        // Gate: long legato with occasional staccato
        float gate8[] = {1.4f, 1.3f, 1.2f, 0.5f, 1.5f, 1.3f, 1.1f, 0.4f};
        setGateLane(p.state, 8, gate8);
        // Modifier: ties on consecutive steps, accent on reset points
        int32_t mod8[] = {
            kStepActive | kStepAccent | kStepTie, kStepActive | kStepTie,
            kStepActive | kStepTie, kStepActive,
            kStepActive | kStepAccent | kStepTie, kStepActive | kStepTie,
            kStepActive | kStepTie, kStepActive
        };
        setModifierLane(p.state, 8, mod8, 30, 70.0f);
        presets.push_back(std::move(p));
    }

    // 3. "UpDown Bounce" — UpDown 1/8T with swing, ratchets on peaks
    {
        PresetDef p;
        p.name = "UpDown Bounce";
        p.category = "Arp Classic";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8T);
        setArpGateLength(p.state, 70.0f);
        setArpSwing(p.state, 25.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.15f;
        // Velocity: crescendo to peak, decrescendo back
        float vel8[] = {0.55f, 0.65f, 0.8f, 1.0f, 0.95f, 0.75f, 0.6f, 0.5f};
        setVelocityLane(p.state, 8, vel8);
        // Gate: tighter at ends, legato in middle
        float gate8[] = {0.5f, 0.7f, 1.0f, 1.3f, 1.2f, 0.9f, 0.6f, 0.4f};
        setGateLane(p.state, 8, gate8);
        // Ratchet: doubles on peak notes
        int32_t ratch8[] = {1, 1, 1, 2, 2, 1, 1, 1};
        setRatchetLane(p.state, 8, ratch8);
        // Modifier: slides on ascending run
        int32_t mod8[] = {
            kStepActive | kStepSlide, kStepActive | kStepSlide,
            kStepActive | kStepSlide, kStepActive | kStepAccent,
            kStepActive, kStepActive, kStepActive, kStepActive
        };
        setModifierLane(p.state, 8, mod8, 35, 60.0f);
        presets.push_back(std::move(p));
    }

    // 4. "Converge Pulse" — Converge with staccato/legato alternation
    {
        PresetDef p;
        p.name = "Converge Pulse";
        p.category = "Arp Classic";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeConverge);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 65.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.octaveMode = 1; // Interleaved
        p.state.arp.humanize = 0.1f;
        // Velocity: pulsing on/off pattern
        float vel8[] = {1.0f, 0.35f, 0.9f, 0.3f, 0.85f, 0.32f, 0.95f, 0.28f};
        setVelocityLane(p.state, 8, vel8);
        // Gate: staccato-legato alternation
        float gate8[] = {1.1f, 0.3f, 1.0f, 0.25f, 0.95f, 0.3f, 1.2f, 0.2f};
        setGateLane(p.state, 8, gate8);
        // Pitch: subtle chromatic motion
        int32_t pitch8[] = {0, 0, 1, 0, 0, -1, 0, 0};
        setPitchLane(p.state, 8, pitch8);
        // Modifier: accents on strong notes
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepAccent, kStepActive,
            kStepActive, kStepActive,
            kStepActive | kStepAccent, kStepActive
        };
        setModifierLane(p.state, 8, mod8, 30, 40.0f);
        presets.push_back(std::move(p));
    }

    // 5. "Dotted Eighth Drive" — Dotted 1/8 against bar, classic U2/delay feel
    {
        PresetDef p;
        p.name = "Dotted Eighth Drive";
        p.category = "Arp Classic";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8D);
        setArpGateLength(p.state, 85.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.05f;
        // Velocity: strong-weak-medium pattern (6 steps for polyrhythmic feel)
        float vel6[] = {1.0f, 0.5f, 0.7f, 0.9f, 0.45f, 0.65f};
        setVelocityLane(p.state, 6, vel6);
        // Gate: mostly legato with one staccato break
        float gate6[] = {1.2f, 1.0f, 1.1f, 0.4f, 1.0f, 1.15f};
        setGateLane(p.state, 6, gate6);
        // Ratchet: double on the staccato step for rhythmic interest
        int32_t ratch6[] = {1, 1, 1, 2, 1, 1};
        setRatchetLane(p.state, 6, ratch6);
        // Modifier: slides for smooth motion, accent on 1
        int32_t mod6[] = {
            kStepActive | kStepAccent, kStepActive | kStepSlide,
            kStepActive | kStepSlide, kStepActive,
            kStepActive | kStepSlide, kStepActive
        };
        setModifierLane(p.state, 6, mod6, 25, 65.0f);
        presets.push_back(std::move(p));
    }

    // ========================================================================
    // ARP ACID (5 presets)
    // 303-style with slides, accents, pitch bends, short gates, ratchets.
    // Scale quantization to minor pentatonic or blues.
    // ========================================================================

    // 1. "Acid Bass 303" — Classic squelchy bassline
    {
        PresetDef p;
        p.name = "Acid Bass 303";
        p.category = "Arp Acid";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 55.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 1;
        p.state.arp.scaleType = 11; // MinPent
        p.state.arp.rootNote = 0;   // C
        p.state.arp.scaleQuantizeInput = 1;
        // Velocity: resonance-friendly, accented notes drive filter
        float vel16[] = {
            0.9f, 0.4f, 0.5f, 0.35f,  1.0f, 0.38f, 0.45f, 0.6f,
            0.85f, 0.35f, 0.5f, 0.4f,  1.0f, 0.5f, 0.42f, 0.55f
        };
        setVelocityLane(p.state, 16, vel16);
        // Gate: short staccato with occasional long notes
        float gate16[] = {
            0.8f, 0.3f, 0.4f, 0.25f,  1.4f, 0.3f, 0.35f, 0.5f,
            0.7f, 0.25f, 0.4f, 0.3f,  1.3f, 0.4f, 0.3f, 0.45f
        };
        setGateLane(p.state, 16, gate16);
        // Pitch: bass movement with octave jumps
        int32_t pitch16[] = {
            0, 0, 3, 0,  -12, 0, 5, 0,
            0, 3, 0, 7,  -12, 0, 0, 5
        };
        setPitchLane(p.state, 16, pitch16);
        // Modifier: classic 303 slide+accent pattern
        int32_t mod16[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive | kStepSlide, kStepActive,
            kStepActive | kStepAccent | kStepSlide, kStepActive, kStepActive, kStepActive | kStepSlide,
            kStepActive | kStepAccent, kStepActive | kStepSlide, kStepActive, kStepActive,
            kStepActive | kStepAccent | kStepSlide, kStepActive, kStepActive, kStepActive | kStepSlide
        };
        setModifierLane(p.state, 16, mod16, 110, 45.0f);
        // Ratchet: doubles on accent notes
        int32_t ratch16[] = {
            2, 1, 1, 1,  2, 1, 1, 1,
            2, 1, 1, 1,  2, 1, 1, 1
        };
        setRatchetLane(p.state, 16, ratch16);
        presets.push_back(std::move(p));
    }

    // 2. "Acid Stab" — Aggressive accented stabs
    {
        PresetDef p;
        p.name = "Acid Stab";
        p.category = "Arp Acid";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 35.0f);
        setArpSwing(p.state, 8.0f);
        p.state.arp.octaveRange = 1;
        p.state.arp.scaleType = 12; // Blues
        p.state.arp.rootNote = 5;   // F
        p.state.arp.scaleQuantizeInput = 1;
        // Velocity: all-out accented, with some ghost notes
        float vel8[] = {1.0f, 0.3f, 1.0f, 0.25f, 1.0f, 0.3f, 0.9f, 1.0f};
        setVelocityLane(p.state, 8, vel8);
        // Gate: very short stabs with one sustained
        float gate8[] = {0.3f, 0.2f, 0.35f, 0.15f, 0.3f, 0.2f, 1.2f, 0.25f};
        setGateLane(p.state, 8, gate8);
        // Pitch: octave jumps for aggression
        int32_t pitch8[] = {0, 12, 0, -12, 0, 12, 0, 0};
        setPitchLane(p.state, 8, pitch8);
        // Modifier: accent on stabs, slide into sustained note
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepAccent, kStepActive, kStepActive | kStepSlide, kStepActive | kStepAccent
        };
        setModifierLane(p.state, 8, mod8, 120, 40.0f);
        // Ratchet: triplet on final accent
        int32_t ratch8[] = {1, 1, 1, 1, 1, 1, 1, 3};
        setRatchetLane(p.state, 8, ratch8);
        presets.push_back(std::move(p));
    }

    // 3. "Acid Slide Machine" — Slide-heavy with long portamento
    {
        PresetDef p;
        p.name = "Acid Slide Machine";
        p.category = "Arp Acid";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 95.0f);
        setArpSwing(p.state, 12.0f);
        p.state.arp.octaveRange = 1;
        p.state.arp.scaleType = 11; // MinPent
        p.state.arp.rootNote = 9;   // A
        p.state.arp.scaleQuantizeInput = 1;
        p.state.arp.humanize = 0.05f;
        // Velocity: smooth contour, not flat
        float vel8[] = {0.7f, 0.8f, 0.6f, 0.9f, 0.75f, 0.85f, 0.65f, 1.0f};
        setVelocityLane(p.state, 8, vel8);
        // Gate: all long for slide effect
        float gate8[] = {1.4f, 1.3f, 1.5f, 1.2f, 1.4f, 1.3f, 1.5f, 0.6f};
        setGateLane(p.state, 8, gate8);
        // Pitch: minor pentatonic walk
        int32_t pitch8[] = {0, 3, 5, 7, 10, 7, 5, 3};
        setPitchLane(p.state, 8, pitch8);
        // Modifier: nearly all slides, accent on last (break) note
        int32_t mod8[] = {
            kStepActive | kStepSlide, kStepActive | kStepSlide,
            kStepActive | kStepSlide, kStepActive | kStepSlide,
            kStepActive | kStepSlide, kStepActive | kStepSlide,
            kStepActive | kStepSlide, kStepActive | kStepAccent
        };
        setModifierLane(p.state, 8, mod8, 100, 90.0f);
        presets.push_back(std::move(p));
    }

    // 4. "Acid Bubbles" — Fast 1/16 with random-ish pitch, probability gate
    {
        PresetDef p;
        p.name = "Acid Bubbles";
        p.category = "Arp Acid";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeRandom);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 45.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.scaleType = 12; // Blues
        p.state.arp.rootNote = 2;   // D
        p.state.arp.scaleQuantizeInput = 1;
        p.state.arp.spice = 0.3f;
        p.state.arp.humanize = 0.1f;
        // Velocity: random-ish highs and lows for filter movement
        float vel16[] = {
            0.9f, 0.3f, 0.7f, 1.0f,  0.4f, 0.8f, 0.35f, 0.95f,
            0.5f, 1.0f, 0.3f, 0.75f, 0.9f, 0.4f, 0.85f, 0.3f
        };
        setVelocityLane(p.state, 16, vel16);
        // Gate: mostly short with occasional long
        float gate16[] = {
            0.4f, 0.25f, 0.35f, 1.0f,  0.3f, 0.4f, 0.2f, 0.8f,
            0.35f, 0.9f, 0.25f, 0.4f,  0.3f, 0.3f, 0.7f, 0.2f
        };
        setGateLane(p.state, 16, gate16);
        // Pitch: bouncy intervals
        int32_t pitch16[] = {
            0, 7, -5, 3,  12, -3, 5, 0,
            7, -7, 3, 10, -5, 0, 12, -12
        };
        setPitchLane(p.state, 16, pitch16);
        // Modifier: scattered slides and accents
        int32_t mod16[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive | kStepSlide, kStepActive | kStepAccent,
            kStepActive, kStepActive | kStepSlide, kStepActive, kStepActive | kStepAccent,
            kStepActive | kStepSlide, kStepActive, kStepActive, kStepActive | kStepAccent | kStepSlide,
            kStepActive, kStepActive | kStepSlide, kStepActive | kStepAccent, kStepActive
        };
        setModifierLane(p.state, 16, mod16, 100, 50.0f);
        // Condition: some probability for unpredictable gate
        int32_t cond16[] = {
            kCondAlways, kCondProb75, kCondAlways, kCondAlways,
            kCondProb50, kCondAlways, kCondProb75, kCondAlways,
            kCondAlways, kCondAlways, kCondProb50, kCondAlways,
            kCondAlways, kCondProb75, kCondAlways, kCondProb50
        };
        setConditionLane(p.state, 16, cond16, 0);
        presets.push_back(std::move(p));
    }

    // 5. "Acid Hypnotic" — Repeating hypnotic pattern with ties
    {
        PresetDef p;
        p.name = "Acid Hypnotic";
        p.category = "Arp Acid";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 70.0f);
        setArpSwing(p.state, 18.0f);
        p.state.arp.octaveRange = 1;
        p.state.arp.scaleType = 11; // MinPent
        p.state.arp.rootNote = 7;   // G
        p.state.arp.scaleQuantizeInput = 1;
        p.state.arp.ratchetSwing = 65.0f;
        // Velocity: hypnotic pulsing
        float vel8[] = {1.0f, 0.5f, 0.7f, 0.45f, 0.9f, 0.5f, 0.65f, 0.4f};
        setVelocityLane(p.state, 8, vel8);
        // Gate: mostly short, long on tied notes
        float gate8[] = {0.6f, 0.3f, 1.4f, 0.3f, 0.5f, 0.3f, 1.3f, 0.25f};
        setGateLane(p.state, 8, gate8);
        // Pitch: repetitive octave root movement
        int32_t pitch8[] = {0, 0, 0, -12, 0, 0, 0, 12};
        setPitchLane(p.state, 8, pitch8);
        // Modifier: ties create connected phrases, accents on 1 and 5
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepTie | kStepSlide, kStepActive,
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepTie | kStepSlide, kStepActive
        };
        setModifierLane(p.state, 8, mod8, 105, 55.0f);
        // Ratchet: triples on accented beat
        int32_t ratch8[] = {3, 1, 1, 1, 2, 1, 1, 1};
        setRatchetLane(p.state, 8, ratch8);
        presets.push_back(std::move(p));
    }

    // ========================================================================
    // ARP EUCLIDEAN (5 presets)
    // Different E(k,n,r) patterns with velocity accents, ratchets, conditions.
    // Polymetric lane lengths.
    // ========================================================================

    // 1. "Tresillo E(3,8)" — Classic Afro-Cuban, accented velocity, ratchets
    {
        PresetDef p;
        p.name = "Tresillo E(3,8)";
        p.category = "Arp Euclidean";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 75.0f);
        setArpSwing(p.state, 15.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.1f;
        setEuclidean(p.state, true, 3, 8, 0);
        // Velocity: strong on euclidean hits, ghost between (5-step polymetric)
        float vel5[] = {1.0f, 0.4f, 0.85f, 0.35f, 0.9f};
        setVelocityLane(p.state, 5, vel5);
        // Gate: varied for rhythmic interest
        float gate8[] = {1.1f, 0.4f, 0.9f, 0.35f, 1.0f, 0.4f, 0.85f, 0.3f};
        setGateLane(p.state, 8, gate8);
        // Ratchet: double on strong beats (3-step polymetric)
        int32_t ratch3[] = {2, 1, 1};
        setRatchetLane(p.state, 3, ratch3);
        // Modifier: accents on euclidean downbeats
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive,
            kStepActive | kStepAccent, kStepActive, kStepActive,
            kStepActive | kStepAccent, kStepActive
        };
        setModifierLane(p.state, 8, mod8, 35, 50.0f);
        presets.push_back(std::move(p));
    }

    // 2. "Bossa Nova E(5,16)" — Brazilian rhythm, pitch melody, conditions
    {
        PresetDef p;
        p.name = "Bossa Nova E(5,16)";
        p.category = "Arp Euclidean";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 80.0f);
        setArpSwing(p.state, 10.0f);
        p.state.arp.octaveRange = 1;
        p.state.arp.humanize = 0.12f;
        p.state.arp.scaleType = 4; // Dorian
        p.state.arp.rootNote = 2;  // D
        setEuclidean(p.state, true, 5, 16, 0);
        // Velocity: melodic contour (7-step polymetric)
        float vel7[] = {0.85f, 0.6f, 0.75f, 0.9f, 0.55f, 0.7f, 1.0f};
        setVelocityLane(p.state, 7, vel7);
        // Gate: legato for bossa feel
        float gate5[] = {1.2f, 0.9f, 1.1f, 0.8f, 1.0f};
        setGateLane(p.state, 5, gate5);
        // Pitch: melodic intervals
        int32_t pitch5[] = {0, 2, 5, 3, 7};
        setPitchLane(p.state, 5, pitch5);
        // Condition: light probability for variation
        int32_t cond8[] = {
            kCondAlways, kCondAlways, kCondProb75, kCondAlways,
            kCondAlways, kCondProb90, kCondAlways, kCondAlways
        };
        setConditionLane(p.state, 8, cond8, 0);
        // Modifier: slides for smooth motion
        int32_t mod5[] = {
            kStepActive | kStepAccent, kStepActive | kStepSlide,
            kStepActive, kStepActive | kStepSlide, kStepActive
        };
        setModifierLane(p.state, 5, mod5, 25, 70.0f);
        presets.push_back(std::move(p));
    }

    // 3. "Samba Fiesta E(7,16,2)" — Dense, rotated, with ratchet fills
    {
        PresetDef p;
        p.name = "Samba Fiesta E(7,16,2)";
        p.category = "Arp Euclidean";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 65.0f);
        setArpSwing(p.state, 20.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.15f;
        p.state.arp.ratchetSwing = 60.0f;
        setEuclidean(p.state, true, 7, 16, 2);
        // Velocity: accented pattern (11-step polymetric for long evolution)
        float vel11[] = {
            1.0f, 0.5f, 0.7f, 0.85f, 0.4f, 0.9f,
            0.45f, 0.8f, 0.5f, 0.95f, 0.4f
        };
        setVelocityLane(p.state, 11, vel11);
        // Gate: rhythmic variation
        float gate7[] = {0.9f, 0.5f, 0.7f, 1.1f, 0.4f, 0.8f, 0.6f};
        setGateLane(p.state, 7, gate7);
        // Ratchet: fills on select beats
        int32_t ratch8[] = {1, 2, 1, 1, 3, 1, 2, 1};
        setRatchetLane(p.state, 8, ratch8);
        // Pitch: rhythmic bass movement
        int32_t pitch4[] = {0, 5, 0, 7};
        setPitchLane(p.state, 4, pitch4);
        // Modifier: accents follow euclidean density
        int32_t mod7[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive | kStepSlide,
            kStepActive | kStepAccent, kStepActive, kStepActive,
            kStepActive | kStepAccent
        };
        setModifierLane(p.state, 7, mod7, 40, 55.0f);
        presets.push_back(std::move(p));
    }

    // 4. "Sparse Pulse E(3,16,5)" — Minimal, wide spacing, ambient
    {
        PresetDef p;
        p.name = "Sparse Pulse E(3,16,5)";
        p.category = "Arp Euclidean";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 120.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 3;
        p.state.arp.humanize = 0.08f;
        p.state.arp.scaleType = 7; // Lydian
        p.state.arp.rootNote = 5;  // F
        setEuclidean(p.state, true, 3, 16, 5);
        // Velocity: gentle swell
        float vel4[] = {0.6f, 0.75f, 0.9f, 1.0f};
        setVelocityLane(p.state, 4, vel4);
        // Gate: very long, ambient
        float gate3[] = {1.8f, 1.5f, 1.6f};
        setGateLane(p.state, 3, gate3);
        // Pitch: wide intervals for spaciousness
        int32_t pitch3[] = {0, 7, -5};
        setPitchLane(p.state, 3, pitch3);
        // Chord: triads on hits
        int32_t chords3[] = {kChordTriad, kChordNone, kChordTriad};
        setChordLane(p.state, 3, chords3);
        setVoicingMode(p.state, 2); // Spread
        // Condition: occasional drops
        int32_t cond4[] = {kCondAlways, kCondProb90, kCondAlways, kCondProb75};
        setConditionLane(p.state, 4, cond4, 0);
        presets.push_back(std::move(p));
    }

    // 5. "Dense Machine E(11,16,3)" — Near-fill density, industrial
    {
        PresetDef p;
        p.name = "Dense Machine E(11,16,3)";
        p.category = "Arp Euclidean";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 40.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 1;
        p.state.arp.scaleType = 8; // Chromatic
        p.state.arp.ratchetSwing = 40.0f;
        setEuclidean(p.state, true, 11, 16, 3);
        // Velocity: mechanical alternating pattern
        float vel4[] = {1.0f, 0.6f, 0.8f, 0.5f};
        setVelocityLane(p.state, 4, vel4);
        // Gate: very short, percussive
        float gate8[] = {0.3f, 0.25f, 0.35f, 0.2f, 0.3f, 0.28f, 0.32f, 0.22f};
        setGateLane(p.state, 8, gate8);
        // Ratchet: mechanical doubles and triples
        int32_t ratch6[] = {1, 2, 1, 3, 1, 2};
        setRatchetLane(p.state, 6, ratch6);
        // Pitch: chromatic cluster
        int32_t pitch6[] = {0, 1, -1, 2, -2, 0};
        setPitchLane(p.state, 6, pitch6);
        // Modifier: all accented, mechanical feel
        int32_t mod4[] = {
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepAccent, kStepActive
        };
        setModifierLane(p.state, 4, mod4, 50, 30.0f);
        presets.push_back(std::move(p));
    }

    // ========================================================================
    // ARP POLYMETRIC (5 presets)
    // Different lengths per lane (3, 5, 7, 11, etc.), creative pitch intervals,
    // condition lanes, ratchet patterns shifting against main rhythm.
    // ========================================================================

    // 1. "3x5x7 Evolving" — Classic polymetric, three coprime lane lengths
    {
        PresetDef p;
        p.name = "3x5x7 Evolving";
        p.category = "Arp Polymetric";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 70.0f);
        setArpSwing(p.state, 10.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.08f;
        p.state.arp.scaleType = 1; // NatMinor
        p.state.arp.rootNote = 9;  // A
        // Velocity lane length=3
        float vel3[] = {1.0f, 0.55f, 0.75f};
        setVelocityLane(p.state, 3, vel3);
        // Gate lane length=5
        float gate5[] = {1.1f, 0.5f, 0.8f, 1.3f, 0.4f};
        setGateLane(p.state, 5, gate5);
        // Pitch lane length=7
        int32_t pitch7[] = {0, 3, 0, 7, -2, 5, 0};
        setPitchLane(p.state, 7, pitch7);
        // Ratchet lane length=4
        int32_t ratch4[] = {1, 1, 2, 1};
        setRatchetLane(p.state, 4, ratch4);
        // Modifier: ties and accents (length=3, shifts against others)
        int32_t mod3[] = {
            kStepActive | kStepAccent, kStepActive | kStepSlide, kStepActive
        };
        setModifierLane(p.state, 3, mod3, 30, 60.0f);
        // Condition: probability layer (length=5)
        int32_t cond5[] = {kCondAlways, kCondAlways, kCondProb75, kCondAlways, kCondProb90};
        setConditionLane(p.state, 5, cond5, 0);
        presets.push_back(std::move(p));
    }

    // 2. "5x11 Hypnosis" — Very long cycle, trance-inducing
    {
        PresetDef p;
        p.name = "5x11 Hypnosis";
        p.category = "Arp Polymetric";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 80.0f);
        setArpSwing(p.state, 5.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.1f;
        p.state.arp.scaleType = 4; // Dorian
        p.state.arp.rootNote = 2;  // D
        // Velocity lane length=5
        float vel5[] = {0.9f, 0.5f, 0.7f, 0.6f, 1.0f};
        setVelocityLane(p.state, 5, vel5);
        // Pitch lane length=11
        int32_t pitch11[] = {0, 2, 5, 7, 3, -2, 0, 5, 7, 10, 12};
        setPitchLane(p.state, 11, pitch11);
        // Gate lane length=7
        float gate7[] = {1.0f, 0.6f, 0.8f, 1.2f, 0.5f, 0.9f, 0.7f};
        setGateLane(p.state, 7, gate7);
        // Ratchet lane length=3
        int32_t ratch3[] = {1, 2, 1};
        setRatchetLane(p.state, 3, ratch3);
        // Modifier: slides for legato feeling (length=5)
        int32_t mod5[] = {
            kStepActive | kStepSlide, kStepActive, kStepActive | kStepAccent,
            kStepActive | kStepSlide, kStepActive
        };
        setModifierLane(p.state, 5, mod5, 25, 75.0f);
        presets.push_back(std::move(p));
    }

    // 3. "7x13 Phase Drift" — Maximally coprime, evolves over many bars
    {
        PresetDef p;
        p.name = "7x13 Phase Drift";
        p.category = "Arp Polymetric";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 60.0f);
        setArpSwing(p.state, 15.0f);
        p.state.arp.octaveRange = 3;
        p.state.arp.octaveMode = 1; // Interleaved
        p.state.arp.scaleType = 6;  // Phrygian
        p.state.arp.rootNote = 4;   // E
        // Velocity lane length=7
        float vel7[] = {1.0f, 0.4f, 0.65f, 0.85f, 0.35f, 0.7f, 0.9f};
        setVelocityLane(p.state, 7, vel7);
        // Pitch lane length=13
        int32_t pitch13[] = {0, 1, 5, 0, -1, 3, 7, 0, -3, 5, 1, 8, 0};
        setPitchLane(p.state, 13, pitch13);
        // Gate lane length=5
        float gate5[] = {0.8f, 0.4f, 1.1f, 0.3f, 0.7f};
        setGateLane(p.state, 5, gate5);
        // Ratchet lane length=7
        int32_t ratch7[] = {1, 1, 2, 1, 1, 1, 3};
        setRatchetLane(p.state, 7, ratch7);
        // Condition lane length=11
        int32_t cond11[] = {
            kCondAlways, kCondAlways, kCondProb75, kCondAlways, kCondAlways,
            kCondProb50, kCondAlways, kCondAlways, kCondAlways, kCondProb90,
            kCondAlways
        };
        setConditionLane(p.state, 11, cond11, 0);
        // Modifier: slides at phrase boundaries
        int32_t mod7[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive | kStepSlide,
            kStepActive, kStepActive, kStepActive | kStepSlide,
            kStepActive | kStepAccent
        };
        setModifierLane(p.state, 7, mod7, 30, 65.0f);
        presets.push_back(std::move(p));
    }

    // 4. "4x5x6 Groove" — Moderate polymetry, funk-oriented
    {
        PresetDef p;
        p.name = "4x5x6 Groove";
        p.category = "Arp Polymetric";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 55.0f);
        setArpSwing(p.state, 30.0f);
        p.state.arp.octaveRange = 1;
        p.state.arp.scaleType = 5; // Mixolydian
        p.state.arp.rootNote = 7;  // G
        p.state.arp.humanize = 0.15f;
        // Velocity lane length=4: funky accent pattern
        float vel4[] = {1.0f, 0.4f, 0.7f, 0.45f};
        setVelocityLane(p.state, 4, vel4);
        // Gate lane length=5: staccato-legato mix
        float gate5[] = {0.5f, 1.3f, 0.4f, 0.8f, 0.6f};
        setGateLane(p.state, 5, gate5);
        // Pitch lane length=6
        int32_t pitch6[] = {0, 0, 5, 0, -5, 7};
        setPitchLane(p.state, 6, pitch6);
        // Ratchet lane length=5: rhythmic fills
        int32_t ratch5[] = {1, 2, 1, 1, 2};
        setRatchetLane(p.state, 5, ratch5);
        // Modifier: accents and slides (length=4)
        int32_t mod4[] = {
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepSlide, kStepActive | kStepAccent
        };
        setModifierLane(p.state, 4, mod4, 40, 45.0f);
        // Chord: occasional dyads (length=6)
        int32_t chords6[] = {kChordNone, kChordNone, kChordDyad, kChordNone, kChordNone, kChordDyad};
        setChordLane(p.state, 6, chords6);
        presets.push_back(std::move(p));
    }

    // 5. "Prime Cascade 3x7x11" — Triple prime, maximum evolution length
    {
        PresetDef p;
        p.name = "Prime Cascade 3x7x11";
        p.category = "Arp Polymetric";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeConverge);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 75.0f);
        setArpSwing(p.state, 8.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.scaleType = 2; // HarmMinor
        p.state.arp.rootNote = 0;  // C
        p.state.arp.humanize = 0.06f;
        // Velocity lane length=3
        float vel3[] = {1.0f, 0.6f, 0.8f};
        setVelocityLane(p.state, 3, vel3);
        // Gate lane length=7
        float gate7[] = {0.9f, 0.5f, 1.2f, 0.4f, 0.8f, 1.1f, 0.6f};
        setGateLane(p.state, 7, gate7);
        // Pitch lane length=11: harmonic minor exploration
        int32_t pitch11[] = {0, 3, 5, 7, 8, 11, 12, 8, 7, 5, 3};
        setPitchLane(p.state, 11, pitch11);
        // Ratchet lane length=5: asymmetric
        int32_t ratch5[] = {1, 1, 2, 1, 3};
        setRatchetLane(p.state, 5, ratch5);
        // Modifier length=7
        int32_t mod7[] = {
            kStepActive | kStepAccent, kStepActive | kStepSlide, kStepActive,
            kStepActive | kStepTie, kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepSlide
        };
        setModifierLane(p.state, 7, mod7, 35, 70.0f);
        // Condition length=3
        int32_t cond3[] = {kCondAlways, kCondAlways, kCondProb75};
        setConditionLane(p.state, 3, cond3, 0);
        presets.push_back(std::move(p));
    }

    // ========================================================================
    // ARP GENERATIVE (5 presets)
    // High spice, humanize, probability conditions, random/walk modes,
    // euclidean timing — ever-evolving patterns.
    // ========================================================================

    // 1. "Spice Evolver" — Moderate randomization with structured backbone
    {
        PresetDef p;
        p.name = "Spice Evolver";
        p.category = "Arp Generative";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 70.0f);
        setArpSwing(p.state, 12.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.spice = 0.6f;
        p.state.arp.humanize = 0.25f;
        p.state.arp.scaleType = 10; // MajPent
        p.state.arp.rootNote = 0;   // C
        p.state.arp.scaleQuantizeInput = 1;
        // Condition lane: mixed probabilities
        int32_t cond8[] = {
            kCondAlways, kCondProb50, kCondAlways, kCondProb75,
            kCondAlways, kCondProb25, kCondProb50, kCondAlways
        };
        setConditionLane(p.state, 8, cond8, 0);
        // Velocity: organic wave pattern
        float vel8[] = {0.7f, 0.9f, 0.5f, 1.0f, 0.6f, 0.8f, 0.4f, 0.95f};
        setVelocityLane(p.state, 8, vel8);
        // Gate: variation
        float gate8[] = {0.8f, 0.5f, 1.1f, 0.6f, 0.9f, 0.4f, 1.0f, 0.7f};
        setGateLane(p.state, 8, gate8);
        // Pitch: pentatonic intervals
        int32_t pitch5[] = {0, 2, 4, 7, 9};
        setPitchLane(p.state, 5, pitch5);
        // Ratchet: occasional bursts
        int32_t ratch4[] = {1, 1, 2, 1};
        setRatchetLane(p.state, 4, ratch4);
        // Modifier: accents mark structural beats
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive,
            kStepActive | kStepSlide, kStepActive | kStepAccent,
            kStepActive, kStepActive, kStepActive | kStepSlide
        };
        setModifierLane(p.state, 8, mod8, 30, 60.0f);
        presets.push_back(std::move(p));
    }

    // 2. "Chaos Garden" — High randomization, many probability layers
    {
        PresetDef p;
        p.name = "Chaos Garden";
        p.category = "Arp Generative";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeRandom);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 60.0f);
        setArpSwing(p.state, 18.0f);
        p.state.arp.octaveRange = 3;
        p.state.arp.spice = 0.85f;
        p.state.arp.humanize = 0.4f;
        p.state.arp.scaleType = 13; // WholeTone
        p.state.arp.rootNote = 6;   // F#
        p.state.arp.scaleQuantizeInput = 1;
        // Condition lane: heavy probability
        int32_t cond16[] = {
            kCondProb25, kCondProb50, kCondProb75, kCondAlways,
            kCondProb10, kCondProb50, kCondAlways, kCondProb75,
            kCondProb90, kCondProb25, kCondProb50, kCondAlways,
            kCondProb75, kCondProb10, kCondAlways, kCondProb50
        };
        setConditionLane(p.state, 16, cond16, 0);
        // Velocity: chaotic contour
        float vel11[] = {
            0.9f, 0.3f, 0.7f, 1.0f, 0.2f, 0.8f,
            0.4f, 0.95f, 0.35f, 0.6f, 0.85f
        };
        setVelocityLane(p.state, 11, vel11);
        // Gate: wide range
        float gate7[] = {0.3f, 1.5f, 0.4f, 0.8f, 1.2f, 0.25f, 0.9f};
        setGateLane(p.state, 7, gate7);
        // Pitch: wide intervals
        int32_t pitch8[] = {0, 6, -6, 12, -12, 4, 8, -4};
        setPitchLane(p.state, 8, pitch8);
        // Ratchet: chaotic subdivisions
        int32_t ratch5[] = {1, 3, 1, 2, 4};
        setRatchetLane(p.state, 5, ratch5);
        presets.push_back(std::move(p));
    }

    // 3. "Random Walk Drift" — Walk mode, gentle randomization, ambient
    {
        PresetDef p;
        p.name = "Random Walk Drift";
        p.category = "Arp Generative";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeWalk);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 110.0f);
        setArpSwing(p.state, 8.0f);
        p.state.arp.octaveRange = 3;
        p.state.arp.spice = 0.45f;
        p.state.arp.humanize = 0.3f;
        p.state.arp.scaleType = 7; // Lydian
        p.state.arp.rootNote = 5;  // F
        p.state.arp.scaleQuantizeInput = 1;
        setEuclidean(p.state, true, 5, 8, 1);
        // Velocity: gentle undulation
        float vel6[] = {0.6f, 0.7f, 0.8f, 0.75f, 0.65f, 0.55f};
        setVelocityLane(p.state, 6, vel6);
        // Gate: long and flowing
        float gate5[] = {1.3f, 1.1f, 1.5f, 0.9f, 1.2f};
        setGateLane(p.state, 5, gate5);
        // Pitch: lydian intervals
        int32_t pitch7[] = {0, 4, 7, 11, 7, 4, 2};
        setPitchLane(p.state, 7, pitch7);
        // Condition: very light probability
        int32_t cond4[] = {kCondAlways, kCondAlways, kCondProb90, kCondAlways};
        setConditionLane(p.state, 4, cond4, 0);
        // Chord: occasional triads for richness
        int32_t chords5[] = {kChordNone, kChordTriad, kChordNone, kChordNone, kChordDyad};
        setChordLane(p.state, 5, chords5);
        setVoicingMode(p.state, 2); // Spread
        // Modifier: slides for drift feeling
        int32_t mod4[] = {
            kStepActive | kStepSlide, kStepActive | kStepSlide,
            kStepActive, kStepActive | kStepSlide
        };
        setModifierLane(p.state, 4, mod4, 20, 85.0f);
        presets.push_back(std::move(p));
    }

    // 4. "Probability Machine" — Conditions on everything, structured chaos
    {
        PresetDef p;
        p.name = "Probability Machine";
        p.category = "Arp Generative";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 65.0f);
        setArpSwing(p.state, 20.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.spice = 0.5f;
        p.state.arp.humanize = 0.2f;
        p.state.arp.scaleType = 3; // MelMinor
        p.state.arp.rootNote = 11;  // B
        p.state.arp.ratchetSwing = 40.0f;
        setEuclidean(p.state, true, 7, 12, 2);
        // Condition: every step has probability
        int32_t cond12[] = {
            kCondProb90, kCondProb50, kCondProb75, kCondAlways,
            kCondProb25, kCondProb90, kCondAlways, kCondProb50,
            kCondProb75, kCondProb90, kCondProb50, kCondAlways
        };
        setConditionLane(p.state, 12, cond12, 0);
        // Velocity: dramatic swings (7-step polymetric)
        float vel7[] = {1.0f, 0.3f, 0.8f, 0.2f, 0.9f, 0.4f, 0.7f};
        setVelocityLane(p.state, 7, vel7);
        // Gate: mixed lengths
        float gate5[] = {0.5f, 1.2f, 0.3f, 0.8f, 1.5f};
        setGateLane(p.state, 5, gate5);
        // Pitch: melodic minor intervals
        int32_t pitch8[] = {0, 2, 3, 7, 9, 11, 7, 3};
        setPitchLane(p.state, 8, pitch8);
        // Ratchet: chaotic
        int32_t ratch3[] = {1, 2, 3};
        setRatchetLane(p.state, 3, ratch3);
        // Modifier: mixed flags
        int32_t mod6[] = {
            kStepActive | kStepAccent, kStepActive | kStepSlide,
            kStepActive, kStepActive | kStepTie,
            kStepActive | kStepAccent | kStepSlide, kStepActive
        };
        setModifierLane(p.state, 6, mod6, 35, 55.0f);
        presets.push_back(std::move(p));
    }

    // 5. "Cosmic Entropy" — Maximum generative chaos
    {
        PresetDef p;
        p.name = "Cosmic Entropy";
        p.category = "Arp Generative";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeRandom);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8T);
        setArpGateLength(p.state, 50.0f);
        setArpSwing(p.state, 35.0f);
        p.state.arp.octaveRange = 4;
        p.state.arp.octaveMode = 1; // Interleaved
        p.state.arp.spice = 1.0f;
        p.state.arp.humanize = 0.5f;
        p.state.arp.scaleType = 14; // DimWH
        p.state.arp.rootNote = 3;   // Eb
        p.state.arp.scaleQuantizeInput = 1;
        p.state.arp.ratchetSwing = 30.0f;
        setEuclidean(p.state, true, 5, 13, 3);
        // Condition: maximum probability variation
        int32_t cond13[] = {
            kCondProb10, kCondProb75, kCondAlways, kCondProb50, kCondProb25,
            kCondAlways, kCondProb10, kCondProb90, kCondProb50, kCondAlways,
            kCondProb75, kCondProb25, kCondProb90
        };
        setConditionLane(p.state, 13, cond13, 0);
        // Velocity: extreme dynamics
        float vel9[] = {1.0f, 0.15f, 0.6f, 0.95f, 0.1f, 0.7f, 0.2f, 0.85f, 0.3f};
        setVelocityLane(p.state, 9, vel9);
        // Gate: extremes
        float gate7[] = {0.2f, 1.8f, 0.15f, 0.5f, 1.5f, 0.25f, 1.0f};
        setGateLane(p.state, 7, gate7);
        // Pitch: wide leaps
        int32_t pitch11[] = {0, -12, 7, 15, -7, 3, 12, -5, 10, -10, 24};
        setPitchLane(p.state, 11, pitch11);
        // Ratchet: wild
        int32_t ratch7[] = {1, 4, 1, 2, 3, 1, 4};
        setRatchetLane(p.state, 7, ratch7);
        // Chord: scattered chord types
        int32_t chords5[] = {kChordNone, kChord7th, kChordNone, kChordTriad, kChordNone};
        setChordLane(p.state, 5, chords5);
        setVoicingMode(p.state, 3); // Random
        presets.push_back(std::move(p));
    }

    // ========================================================================
    // ARP PERFORMANCE (5 presets)
    // Fill conditions, latch modes, retrigger, ratchet fills, designed for
    // live performance transitions.
    // ========================================================================

    // 1. "Fill Cascade" — Fill conditions double density on toggle
    {
        PresetDef p;
        p.name = "Fill Cascade";
        p.category = "Arp Performance";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 70.0f);
        setArpSwing(p.state, 10.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.latchMode = 1;   // Hold
        p.state.arp.retrigger = 2;   // Beat
        p.state.arp.humanize = 0.08f;
        // Condition: fills add extra density between main beats
        int32_t cond16[] = {
            kCondAlways, kCondFill,   kCondAlways, kCondFill,
            kCondAlways, kCondFill,   kCondFill,   kCondAlways,
            kCondAlways, kCondFill,   kCondAlways, kCondFill,
            kCondFill,   kCondAlways, kCondFill,   kCondFill
        };
        setConditionLane(p.state, 16, cond16, 0);
        // Velocity: accented main beats, lighter fills
        float vel16[] = {
            1.0f, 0.5f, 0.85f, 0.45f, 0.9f, 0.4f, 0.35f, 0.8f,
            0.95f, 0.42f, 0.88f, 0.4f, 0.38f, 0.85f, 0.45f, 0.35f
        };
        setVelocityLane(p.state, 16, vel16);
        // Gate: short on fills, longer on main
        float gate16[] = {
            1.0f, 0.3f, 0.9f, 0.3f, 0.95f, 0.25f, 0.3f, 0.85f,
            1.0f, 0.3f, 0.9f, 0.25f, 0.3f, 0.85f, 0.3f, 0.25f
        };
        setGateLane(p.state, 16, gate16);
        // Ratchet: doubles on fill-activated beats for buzz rolls
        int32_t ratch8[] = {1, 2, 1, 2, 1, 2, 1, 1};
        setRatchetLane(p.state, 8, ratch8);
        // Modifier: accents on downbeats
        int32_t mod4[] = {
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepAccent, kStepActive
        };
        setModifierLane(p.state, 4, mod4, 35, 40.0f);
        presets.push_back(std::move(p));
    }

    // 2. "Latch Pad Builder" — Hold mode, slow rate, builds up chords
    {
        PresetDef p;
        p.name = "Latch Pad Builder";
        p.category = "Arp Performance";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_4);
        setArpGateLength(p.state, 130.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.latchMode = 2;   // Add
        p.state.arp.retrigger = 0;   // Off
        p.state.arp.humanize = 0.12f;
        p.state.arp.scaleType = 0;   // Major
        p.state.arp.rootNote = 0;    // C
        // Velocity: crescendo pattern
        float vel8[] = {0.45f, 0.55f, 0.65f, 0.75f, 0.8f, 0.85f, 0.9f, 1.0f};
        setVelocityLane(p.state, 8, vel8);
        // Gate: long, overlapping
        float gate4[] = {1.5f, 1.4f, 1.6f, 1.3f};
        setGateLane(p.state, 4, gate4);
        // Chord: builds from none to triads to 7ths
        int32_t chords8[] = {
            kChordNone, kChordNone, kChordDyad, kChordDyad,
            kChordTriad, kChordTriad, kChord7th, kChord7th
        };
        setChordLane(p.state, 8, chords8);
        // Inversion: smooth voice leading
        int32_t inv8[] = {
            kInvRoot, kInvRoot, kInvRoot, kInv1st,
            kInvRoot, kInv1st, kInv2nd, kInvRoot
        };
        setInversionLane(p.state, 8, inv8);
        setVoicingMode(p.state, 1); // Drop2
        // Modifier: ties for sustain
        int32_t mod4[] = {
            kStepActive | kStepTie, kStepActive | kStepTie,
            kStepActive | kStepTie, kStepActive
        };
        setModifierLane(p.state, 4, mod4, 20, 80.0f);
        presets.push_back(std::move(p));
    }

    // 3. "Retrigger Stutter" — Note retrigger, ratchet fills, stutter effect
    {
        PresetDef p;
        p.name = "Retrigger Stutter";
        p.category = "Arp Performance";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 45.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 1;
        p.state.arp.retrigger = 1;   // Note
        p.state.arp.ratchetSwing = 35.0f;
        // Velocity: strong stutter hits, quiet between
        float vel8[] = {1.0f, 0.3f, 0.5f, 0.25f, 0.9f, 0.3f, 0.4f, 0.2f};
        setVelocityLane(p.state, 8, vel8);
        // Gate: very short for stutter
        float gate8[] = {0.4f, 0.2f, 0.3f, 0.15f, 0.35f, 0.2f, 0.25f, 0.15f};
        setGateLane(p.state, 8, gate8);
        // Ratchet: heavy subdivisions for stutter
        int32_t ratch8[] = {4, 1, 2, 1, 3, 1, 2, 1};
        setRatchetLane(p.state, 8, ratch8);
        // Pitch: repeating root with octave pops
        int32_t pitch8[] = {0, 0, 0, 12, 0, 0, -12, 0};
        setPitchLane(p.state, 8, pitch8);
        // Condition: fill-activated extra ratchets
        int32_t cond8[] = {
            kCondAlways, kCondAlways, kCondFill, kCondAlways,
            kCondAlways, kCondFill, kCondAlways, kCondFill
        };
        setConditionLane(p.state, 8, cond8, 0);
        // Modifier: accents on main hits
        int32_t mod4[] = {
            kStepActive | kStepAccent, kStepActive,
            kStepActive, kStepActive | kStepAccent
        };
        setModifierLane(p.state, 4, mod4, 50, 30.0f);
        presets.push_back(std::move(p));
    }

    // 4. "Probability Waves" — UpDown with probability-driven dynamics
    {
        PresetDef p;
        p.name = "Probability Waves";
        p.category = "Arp Performance";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 65.0f);
        setArpSwing(p.state, 15.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.latchMode = 1;   // Hold
        p.state.arp.humanize = 0.1f;
        p.state.arp.scaleType = 1;   // NatMinor
        p.state.arp.rootNote = 2;    // D
        // Condition: wave of probability
        int32_t cond16[] = {
            kCondAlways, kCondProb90, kCondProb75, kCondProb50,
            kCondProb25, kCondProb50, kCondProb75, kCondProb90,
            kCondAlways, kCondProb90, kCondProb75, kCondProb50,
            kCondProb25, kCondProb50, kCondProb75, kCondProb90
        };
        setConditionLane(p.state, 16, cond16, 0);
        // Velocity: mirrors probability wave
        float vel16[] = {
            1.0f, 0.9f, 0.75f, 0.55f, 0.35f, 0.55f, 0.75f, 0.9f,
            1.0f, 0.9f, 0.75f, 0.55f, 0.35f, 0.55f, 0.75f, 0.9f
        };
        setVelocityLane(p.state, 16, vel16);
        // Gate: matches intensity
        float gate8[] = {1.1f, 0.9f, 0.7f, 0.4f, 0.4f, 0.7f, 0.9f, 1.1f};
        setGateLane(p.state, 8, gate8);
        // Ratchet: doubles at peaks
        int32_t ratch8[] = {2, 1, 1, 1, 1, 1, 1, 2};
        setRatchetLane(p.state, 8, ratch8);
        // Modifier: accents at wave peaks
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive, kStepActive,
            kStepActive, kStepActive, kStepActive, kStepActive | kStepAccent
        };
        setModifierLane(p.state, 8, mod8, 30, 60.0f);
        presets.push_back(std::move(p));
    }

    // 5. "Build and Drop" — Fill builds intensity, main is sparse
    {
        PresetDef p;
        p.name = "Build and Drop";
        p.category = "Arp Performance";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 55.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 3;
        p.state.arp.latchMode = 1;  // Hold
        p.state.arp.retrigger = 2;  // Beat
        p.state.arp.ratchetSwing = 55.0f;
        p.state.arp.scaleType = 11; // MinPent
        p.state.arp.rootNote = 9;   // A
        // Condition: sparse main, fills add the build
        int32_t cond16[] = {
            kCondAlways, kCondFill, kCondFill, kCondFill,
            kCondAlways, kCondFill, kCondFill, kCondFill,
            kCondAlways, kCondFill, kCondFill, kCondFill,
            kCondAlways, kCondFill, kCondFill, kCondFill
        };
        setConditionLane(p.state, 16, cond16, 0);
        // Velocity: fills crescendo into main beats
        float vel16[] = {
            1.0f, 0.3f, 0.45f, 0.6f,  0.9f, 0.35f, 0.5f, 0.65f,
            0.85f, 0.3f, 0.4f, 0.55f, 0.95f, 0.4f, 0.55f, 0.75f
        };
        setVelocityLane(p.state, 16, vel16);
        // Gate: main beats longer, fills short
        float gate16[] = {
            1.2f, 0.2f, 0.25f, 0.3f,  1.1f, 0.2f, 0.25f, 0.3f,
            1.0f, 0.2f, 0.25f, 0.3f,  1.15f, 0.2f, 0.3f, 0.35f
        };
        setGateLane(p.state, 16, gate16);
        // Ratchet: triplets on main beat for impact
        int32_t ratch4[] = {3, 1, 1, 1};
        setRatchetLane(p.state, 4, ratch4);
        // Pitch: drops with fills ascending
        int32_t pitch8[] = {0, 3, 5, 7, 0, 5, 7, 12};
        setPitchLane(p.state, 8, pitch8);
        // Modifier: accents on main, slides on fills
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive | kStepSlide,
            kStepActive | kStepSlide, kStepActive | kStepSlide,
            kStepActive | kStepAccent, kStepActive | kStepSlide,
            kStepActive | kStepSlide, kStepActive | kStepSlide
        };
        setModifierLane(p.state, 8, mod8, 40, 50.0f);
        presets.push_back(std::move(p));
    }

    // ========================================================================
    // ARP CHORDS (5 presets)
    // Rich chord progressions with inversions, voicing modes, scale
    // quantization, velocity dynamics.
    // ========================================================================

    // 1. "Diatonic Triads" — Rising triads in C Major, classic arpeggiated
    {
        PresetDef p;
        p.name = "Diatonic Triads";
        p.category = "Arp Chords";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 90.0f);
        setArpSwing(p.state, 5.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.06f;
        p.state.arp.scaleType = 0;  // Major
        p.state.arp.rootNote = 0;   // C
        // Chord lane: triads with passing singles
        int32_t chords8[] = {
            kChordTriad, kChordNone, kChordTriad, kChordNone,
            kChordTriad, kChordNone, kChordTriad, kChordNone
        };
        setChordLane(p.state, 8, chords8);
        // Inversion: smooth voice leading
        int32_t inv8[] = {
            kInvRoot, kInvRoot, kInv1st, kInvRoot,
            kInv2nd, kInvRoot, kInvRoot, kInv1st
        };
        setInversionLane(p.state, 8, inv8);
        // Velocity: strong on chords, soft on singles
        float vel8[] = {1.0f, 0.5f, 0.9f, 0.45f, 0.85f, 0.5f, 0.95f, 0.4f};
        setVelocityLane(p.state, 8, vel8);
        // Gate: legato chords, staccato singles
        float gate8[] = {1.2f, 0.5f, 1.1f, 0.45f, 1.15f, 0.5f, 1.2f, 0.4f};
        setGateLane(p.state, 8, gate8);
        // Modifier: accents on chord hits
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepAccent, kStepActive
        };
        setModifierLane(p.state, 8, mod8, 25, 50.0f);
        presets.push_back(std::move(p));
    }

    // 2. "Minor 7th Pulse" — Dark minor 7th chords, rhythmic gate, Drop2
    {
        PresetDef p;
        p.name = "Minor 7th Pulse";
        p.category = "Arp Chords";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 55.0f);
        setArpSwing(p.state, 12.0f);
        p.state.arp.octaveRange = 1;
        p.state.arp.humanize = 0.1f;
        p.state.arp.scaleType = 1;  // NaturalMinor
        p.state.arp.rootNote = 9;   // A
        setVoicingMode(p.state, 1); // Drop2
        // Chord lane: 7th on downbeats, dyads fill, rests breathe
        int32_t chords16[] = {
            kChord7th, kChordDyad, kChordNone, kChordDyad,
            kChord7th, kChordNone, kChordDyad, kChordNone,
            kChord7th, kChordDyad, kChordNone, kChordNone,
            kChord7th, kChordNone, kChordDyad, kChordDyad
        };
        setChordLane(p.state, 16, chords16);
        // Inversion: voice leading motion
        int32_t inv16[] = {
            kInvRoot, kInv1st, kInvRoot, kInv2nd,
            kInv1st, kInvRoot, kInv2nd, kInvRoot,
            kInvRoot, kInv1st, kInvRoot, kInvRoot,
            kInv2nd, kInvRoot, kInv1st, kInvRoot
        };
        setInversionLane(p.state, 16, inv16);
        // Gate: staccato pulse with occasional sustain
        float gate8[] = {0.7f, 0.3f, 0.25f, 0.35f, 0.8f, 0.25f, 0.4f, 0.3f};
        setGateLane(p.state, 8, gate8);
        // Velocity: pulsing
        float vel8[] = {0.95f, 0.55f, 0.35f, 0.5f, 0.9f, 0.3f, 0.6f, 0.4f};
        setVelocityLane(p.state, 8, vel8);
        // Ratchet: doubles on chord hits
        int32_t ratch8[] = {2, 1, 1, 1, 2, 1, 1, 1};
        setRatchetLane(p.state, 8, ratch8);
        // Modifier: accents on 7ths
        int32_t mod4[] = {
            kStepActive | kStepAccent, kStepActive,
            kStepActive, kStepActive | kStepAccent
        };
        setModifierLane(p.state, 4, mod4, 30, 45.0f);
        presets.push_back(std::move(p));
    }

    // 3. "Chord Cascade" — Polymetric chord/inversion, evolving harmonies
    {
        PresetDef p;
        p.name = "Chord Cascade";
        p.category = "Arp Chords";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 85.0f);
        setArpSwing(p.state, 8.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.08f;
        p.state.arp.scaleType = 4;  // Dorian
        p.state.arp.rootNote = 2;   // D
        // Chord lane length=5 (polymetric against 7-step inversion)
        int32_t chords5[] = {
            kChordTriad, kChordDyad, kChord7th, kChordTriad, kChordNone
        };
        setChordLane(p.state, 5, chords5);
        // Inversion lane length=7 (polymetric)
        int32_t inv7[] = {
            kInvRoot, kInv1st, kInv2nd, kInvRoot, kInv1st, kInv2nd, kInv3rd
        };
        setInversionLane(p.state, 7, inv7);
        setVoicingMode(p.state, 1); // Drop2
        // Pitch lane length=3 for extra polymetry
        int32_t pitch3[] = {0, 5, -3};
        setPitchLane(p.state, 3, pitch3);
        // Velocity: dynamic contour (length=4)
        float vel4[] = {0.9f, 0.55f, 0.8f, 0.45f};
        setVelocityLane(p.state, 4, vel4);
        // Gate: varied
        float gate5[] = {1.1f, 0.6f, 0.9f, 1.2f, 0.5f};
        setGateLane(p.state, 5, gate5);
        // Modifier: slides for smooth chord changes
        int32_t mod5[] = {
            kStepActive | kStepAccent | kStepSlide, kStepActive,
            kStepActive | kStepSlide, kStepActive | kStepAccent,
            kStepActive
        };
        setModifierLane(p.state, 5, mod5, 25, 70.0f);
        presets.push_back(std::move(p));
    }

    // 4. "Spread Ninths" — Wide voicing 9th chords, ambient lydian
    {
        PresetDef p;
        p.name = "Spread Ninths";
        p.category = "Arp Chords";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_4);
        setArpGateLength(p.state, 120.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.1f;
        p.state.arp.scaleType = 7;  // Lydian
        p.state.arp.rootNote = 5;   // F
        setVoicingMode(p.state, 2); // Spread
        // Chord lane: lush 9th and 7th chords
        int32_t chords6[] = {kChord9th, kChord7th, kChord9th, kChordTriad, kChord9th, kChord7th};
        setChordLane(p.state, 6, chords6);
        // Inversion: smooth cycling
        int32_t inv6[] = {kInvRoot, kInv1st, kInv2nd, kInvRoot, kInv1st, kInv3rd};
        setInversionLane(p.state, 6, inv6);
        // Velocity: gentle crescendo-decrescendo
        float vel6[] = {0.5f, 0.65f, 0.85f, 1.0f, 0.8f, 0.6f};
        setVelocityLane(p.state, 6, vel6);
        // Gate: very long, overlapping
        float gate4[] = {1.6f, 1.4f, 1.7f, 1.5f};
        setGateLane(p.state, 4, gate4);
        // Modifier: ties for sustained pads
        int32_t mod6[] = {
            kStepActive | kStepTie, kStepActive | kStepTie | kStepSlide,
            kStepActive | kStepTie, kStepActive | kStepAccent,
            kStepActive | kStepTie | kStepSlide, kStepActive
        };
        setModifierLane(p.state, 6, mod6, 20, 90.0f);
        // Condition: very occasional drops for breathing room
        int32_t cond6[] = {
            kCondAlways, kCondAlways, kCondAlways,
            kCondProb90, kCondAlways, kCondAlways
        };
        setConditionLane(p.state, 6, cond6, 0);
        presets.push_back(std::move(p));
    }

    // 5. "Stab Machine" — Rhythmic chord stabs, funky with ratchets
    {
        PresetDef p;
        p.name = "Stab Machine";
        p.category = "Arp Chords";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 45.0f);
        setArpSwing(p.state, 22.0f);
        p.state.arp.octaveRange = 1;
        p.state.arp.humanize = 0.12f;
        p.state.arp.scaleType = 5;  // Mixolydian
        p.state.arp.rootNote = 7;   // G
        p.state.arp.ratchetSwing = 55.0f;
        // Chord lane: stabs on strong beats, rests between
        int32_t chords16[] = {
            kChordTriad, kChordNone, kChordNone, kChordDyad,
            kChordNone, kChordTriad, kChordNone, kChordNone,
            kChordDyad, kChordNone, kChordNone, kChordTriad,
            kChordNone, kChordDyad, kChordNone, kChordNone
        };
        setChordLane(p.state, 16, chords16);
        // Inversion: varies per chord type
        int32_t inv16[] = {
            kInvRoot, kInvRoot, kInvRoot, kInv1st,
            kInvRoot, kInv1st, kInvRoot, kInvRoot,
            kInv2nd, kInvRoot, kInvRoot, kInvRoot,
            kInvRoot, kInv1st, kInvRoot, kInvRoot
        };
        setInversionLane(p.state, 16, inv16);
        // Ratchet: syncopated doubles
        int32_t ratch8[] = {1, 1, 2, 1, 1, 1, 2, 1};
        setRatchetLane(p.state, 8, ratch8);
        // Velocity: accented stabs, dead between
        float vel16[] = {
            1.0f, 0.25f, 0.2f, 0.8f,  0.2f, 0.95f, 0.2f, 0.2f,
            0.75f, 0.2f, 0.2f, 0.9f,  0.2f, 0.7f, 0.2f, 0.2f
        };
        setVelocityLane(p.state, 16, vel16);
        // Gate: very short stabs, longer on chord hits
        float gate16[] = {
            0.6f, 0.15f, 0.15f, 0.5f,  0.15f, 0.55f, 0.15f, 0.15f,
            0.45f, 0.15f, 0.15f, 0.6f,  0.15f, 0.4f, 0.15f, 0.15f
        };
        setGateLane(p.state, 16, gate16);
        // Modifier: accents on chord stabs
        int32_t mod16[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive, kStepActive | kStepAccent,
            kStepActive, kStepActive | kStepAccent, kStepActive, kStepActive,
            kStepActive | kStepAccent, kStepActive, kStepActive, kStepActive | kStepAccent,
            kStepActive, kStepActive | kStepAccent, kStepActive, kStepActive
        };
        setModifierLane(p.state, 16, mod16, 45, 35.0f);
        presets.push_back(std::move(p));
    }

    // ========================================================================
    // ARP ADVANCED (5 presets)
    // Complex combinations: polymetric + euclidean + conditions + chords +
    // ratchets. Push every parameter.
    // ========================================================================

    // 1. "Polyrhythmic Cathedral" — Euclidean + chords + polymetric lanes
    {
        PresetDef p;
        p.name = "Polyrhythmic Cathedral";
        p.category = "Arp Advanced";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeConverge);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 80.0f);
        setArpSwing(p.state, 10.0f);
        p.state.arp.octaveRange = 3;
        p.state.arp.octaveMode = 1; // Interleaved
        p.state.arp.humanize = 0.1f;
        p.state.arp.spice = 0.2f;
        p.state.arp.scaleType = 2;  // HarmMinor
        p.state.arp.rootNote = 0;   // C
        p.state.arp.ratchetSwing = 45.0f;
        setEuclidean(p.state, true, 7, 12, 1);
        // Chord lane length=5
        int32_t chords5[] = {kChordTriad, kChordNone, kChord7th, kChordDyad, kChordTriad};
        setChordLane(p.state, 5, chords5);
        // Inversion lane length=7
        int32_t inv7[] = {kInvRoot, kInv2nd, kInv1st, kInvRoot, kInv3rd, kInv1st, kInv2nd};
        setInversionLane(p.state, 7, inv7);
        setVoicingMode(p.state, 2); // Spread
        // Velocity length=11
        float vel11[] = {
            1.0f, 0.45f, 0.7f, 0.85f, 0.3f, 0.6f,
            0.9f, 0.4f, 0.75f, 0.5f, 0.95f
        };
        setVelocityLane(p.state, 11, vel11);
        // Gate length=7
        float gate7[] = {1.1f, 0.5f, 0.8f, 1.3f, 0.4f, 0.9f, 0.6f};
        setGateLane(p.state, 7, gate7);
        // Pitch length=13
        int32_t pitch13[] = {0, 3, 7, 8, 0, -4, 3, 7, 11, 12, 8, 3, 0};
        setPitchLane(p.state, 13, pitch13);
        // Ratchet length=5
        int32_t ratch5[] = {1, 2, 1, 1, 3};
        setRatchetLane(p.state, 5, ratch5);
        // Condition length=9
        int32_t cond9[] = {
            kCondAlways, kCondAlways, kCondProb75, kCondAlways, kCondProb50,
            kCondAlways, kCondAlways, kCondProb90, kCondAlways
        };
        setConditionLane(p.state, 9, cond9, 0);
        // Modifier length=5
        int32_t mod5[] = {
            kStepActive | kStepAccent, kStepActive | kStepSlide,
            kStepActive | kStepTie, kStepActive | kStepAccent | kStepSlide,
            kStepActive
        };
        setModifierLane(p.state, 5, mod5, 30, 65.0f);
        presets.push_back(std::move(p));
    }

    // 2. "Chaos Matrix" — Walk mode + euclidean + conditions + ratchets
    {
        PresetDef p;
        p.name = "Chaos Matrix";
        p.category = "Arp Advanced";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeWalk);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 60.0f);
        setArpSwing(p.state, 22.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.spice = 0.5f;
        p.state.arp.humanize = 0.25f;
        p.state.arp.scaleType = 15; // DimHW
        p.state.arp.rootNote = 1;   // Db
        p.state.arp.scaleQuantizeInput = 1;
        p.state.arp.ratchetSwing = 35.0f;
        setEuclidean(p.state, true, 5, 11, 2);
        // Velocity: dramatic swings (length=7)
        float vel7[] = {1.0f, 0.2f, 0.7f, 0.95f, 0.15f, 0.8f, 0.3f};
        setVelocityLane(p.state, 7, vel7);
        // Gate: extremes (length=5)
        float gate5[] = {0.3f, 1.4f, 0.2f, 0.8f, 1.6f};
        setGateLane(p.state, 5, gate5);
        // Pitch: angular intervals (length=11)
        int32_t pitch11[] = {0, -6, 3, 9, -3, 6, -9, 12, -12, 1, 5};
        setPitchLane(p.state, 11, pitch11);
        // Ratchet: wild (length=7)
        int32_t ratch7[] = {1, 3, 1, 2, 4, 1, 2};
        setRatchetLane(p.state, 7, ratch7);
        // Condition: heavy probability (length=13)
        int32_t cond13[] = {
            kCondProb50, kCondAlways, kCondProb25, kCondProb75, kCondAlways,
            kCondProb10, kCondAlways, kCondProb50, kCondProb90, kCondAlways,
            kCondProb75, kCondProb25, kCondAlways
        };
        setConditionLane(p.state, 13, cond13, 0);
        // Modifier: all the flags (length=9)
        int32_t mod9[] = {
            kStepActive | kStepAccent, kStepActive | kStepSlide,
            kStepActive, kStepActive | kStepTie,
            kStepActive | kStepAccent | kStepSlide, kStepActive,
            kStepActive | kStepSlide, kStepActive | kStepAccent,
            kStepActive | kStepTie | kStepSlide
        };
        setModifierLane(p.state, 9, mod9, 40, 55.0f);
        // Chord: scattered (length=5)
        int32_t chords5[] = {kChordNone, kChord7th, kChordNone, kChordTriad, kChordNone};
        setChordLane(p.state, 5, chords5);
        setVoicingMode(p.state, 3); // Random
        presets.push_back(std::move(p));
    }

    // 3. "Metamorphosis" — Every lane different length, maximum polymetry
    {
        PresetDef p;
        p.name = "Metamorphosis";
        p.category = "Arp Advanced";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeDiverge);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 72.0f);
        setArpSwing(p.state, 14.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.12f;
        p.state.arp.spice = 0.3f;
        p.state.arp.scaleType = 3;  // MelMinor
        p.state.arp.rootNote = 7;   // G
        p.state.arp.latchMode = 1;  // Hold
        // Every lane has a different prime-ish length
        // Velocity length=3
        float vel3[] = {1.0f, 0.5f, 0.75f};
        setVelocityLane(p.state, 3, vel3);
        // Gate length=4
        float gate4[] = {0.8f, 1.3f, 0.5f, 1.0f};
        setGateLane(p.state, 4, gate4);
        // Pitch length=5
        int32_t pitch5[] = {0, 3, 7, 11, -2};
        setPitchLane(p.state, 5, pitch5);
        // Modifier length=7
        int32_t mod7[] = {
            kStepActive | kStepAccent, kStepActive | kStepSlide,
            kStepActive, kStepActive | kStepTie,
            kStepActive | kStepAccent, kStepActive,
            kStepActive | kStepSlide
        };
        setModifierLane(p.state, 7, mod7, 35, 60.0f);
        // Ratchet length=6
        int32_t ratch6[] = {1, 1, 2, 1, 3, 1};
        setRatchetLane(p.state, 6, ratch6);
        // Condition length=8
        int32_t cond8[] = {
            kCondAlways, kCondProb75, kCondAlways, kCondAlways,
            kCondProb50, kCondAlways, kCondProb90, kCondAlways
        };
        setConditionLane(p.state, 8, cond8, 0);
        // Chord length=9
        int32_t chords9[] = {
            kChordTriad, kChordNone, kChordDyad, kChordNone, kChord7th,
            kChordNone, kChordNone, kChordTriad, kChordNone
        };
        setChordLane(p.state, 9, chords9);
        // Inversion length=11
        int32_t inv11[] = {
            kInvRoot, kInv1st, kInvRoot, kInv2nd, kInvRoot,
            kInv1st, kInv3rd, kInvRoot, kInv2nd, kInv1st, kInvRoot
        };
        setInversionLane(p.state, 11, inv11);
        setVoicingMode(p.state, 1); // Drop2
        presets.push_back(std::move(p));
    }

    // 4. "Euclidean Chord Maze" — Euclidean gating + chord progression + fills
    {
        PresetDef p;
        p.name = "Euclidean Chord Maze";
        p.category = "Arp Advanced";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeChord);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 90.0f);
        setArpSwing(p.state, 10.0f);
        p.state.arp.octaveRange = 1;
        p.state.arp.humanize = 0.08f;
        p.state.arp.scaleType = 0;   // Major
        p.state.arp.rootNote = 5;    // F
        p.state.arp.retrigger = 2;   // Beat
        p.state.arp.ratchetSwing = 60.0f;
        setEuclidean(p.state, true, 5, 8, 0);
        // Chord lane: rich progression
        int32_t chords8[] = {
            kChord7th, kChordTriad, kChord9th, kChordTriad,
            kChord7th, kChordDyad, kChord9th, kChordTriad
        };
        setChordLane(p.state, 8, chords8);
        // Inversion: smooth voice leading
        int32_t inv8[] = {
            kInvRoot, kInv1st, kInv2nd, kInvRoot,
            kInv1st, kInvRoot, kInv3rd, kInv2nd
        };
        setInversionLane(p.state, 8, inv8);
        setVoicingMode(p.state, 2); // Spread
        // Velocity: accent on euclidean hits
        float vel8[] = {1.0f, 0.6f, 0.9f, 0.55f, 0.85f, 0.5f, 0.95f, 0.6f};
        setVelocityLane(p.state, 8, vel8);
        // Gate: legato chords
        float gate8[] = {1.3f, 0.8f, 1.2f, 0.7f, 1.1f, 0.6f, 1.4f, 0.75f};
        setGateLane(p.state, 8, gate8);
        // Ratchet: gentle
        int32_t ratch4[] = {1, 1, 2, 1};
        setRatchetLane(p.state, 4, ratch4);
        // Condition: fill activates extra chords
        int32_t cond8[] = {
            kCondAlways, kCondFill, kCondAlways, kCondAlways,
            kCondFill, kCondAlways, kCondAlways, kCondFill
        };
        setConditionLane(p.state, 8, cond8, 0);
        // Modifier: ties and slides for smooth transitions
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive | kStepSlide,
            kStepActive | kStepTie, kStepActive,
            kStepActive | kStepAccent | kStepSlide, kStepActive,
            kStepActive | kStepTie, kStepActive | kStepAccent
        };
        setModifierLane(p.state, 8, mod8, 25, 75.0f);
        presets.push_back(std::move(p));
    }

    // 5. "Kitchen Sink" — Absolutely everything at once
    {
        PresetDef p;
        p.name = "Kitchen Sink";
        p.category = "Arp Advanced";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeDownUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 68.0f);
        setArpSwing(p.state, 25.0f);
        p.state.arp.octaveRange = 3;
        p.state.arp.octaveMode = 1;  // Interleaved
        p.state.arp.latchMode = 2;   // Add
        p.state.arp.retrigger = 1;   // Note
        p.state.arp.spice = 0.4f;
        p.state.arp.humanize = 0.2f;
        p.state.arp.scaleType = 6;   // Phrygian
        p.state.arp.rootNote = 4;    // E
        p.state.arp.scaleQuantizeInput = 1;
        p.state.arp.ratchetSwing = 38.0f;
        setEuclidean(p.state, true, 9, 16, 3);
        // Velocity length=7
        float vel7[] = {1.0f, 0.3f, 0.7f, 0.85f, 0.2f, 0.6f, 0.9f};
        setVelocityLane(p.state, 7, vel7);
        // Gate length=5
        float gate5[] = {0.5f, 1.3f, 0.3f, 0.9f, 1.5f};
        setGateLane(p.state, 5, gate5);
        // Pitch length=11: Phrygian intervals
        int32_t pitch11[] = {0, 1, 3, 5, 7, 8, 10, 12, 8, 5, 1};
        setPitchLane(p.state, 11, pitch11);
        // Modifier length=9: every flag combination
        int32_t mod9[] = {
            kStepActive | kStepAccent, kStepActive | kStepSlide,
            kStepActive | kStepTie, kStepActive,
            kStepActive | kStepAccent | kStepSlide | kStepTie,
            kStepActive, kStepActive | kStepAccent,
            kStepActive | kStepSlide | kStepTie, kStepActive
        };
        setModifierLane(p.state, 9, mod9, 45, 60.0f);
        // Ratchet length=6: varied subdivisions
        int32_t ratch6[] = {1, 2, 1, 3, 1, 4};
        setRatchetLane(p.state, 6, ratch6);
        // Condition length=13: mixed probability + fills
        int32_t cond13[] = {
            kCondAlways, kCondProb75, kCondFill, kCondAlways, kCondProb50,
            kCondAlways, kCondFill, kCondProb90, kCondAlways, kCondProb25,
            kCondFill, kCondAlways, kCondProb75
        };
        setConditionLane(p.state, 13, cond13, 0);
        // Chord length=8: full progression
        int32_t chords8[] = {
            kChordTriad, kChordNone, kChord7th, kChordDyad,
            kChord9th, kChordNone, kChordTriad, kChord7th
        };
        setChordLane(p.state, 8, chords8);
        // Inversion length=5
        int32_t inv5[] = {kInvRoot, kInv2nd, kInv1st, kInv3rd, kInvRoot};
        setInversionLane(p.state, 5, inv5);
        setVoicingMode(p.state, 3); // Random
        presets.push_back(std::move(p));
    }

    // ========================================================================
    // ARP V1.5 SHOWCASE (10 presets)
    // Each preset highlights one or more v1.5 features: ratchet decay, strum
    // mode, per-lane swing, per-lane length jitter, velocity curve, transpose,
    // note range mapping, Gravity arp mode, step pinning.
    // ========================================================================

    // 1. "Bouncing Ball Ratchets" — showcases Ratchet Decay
    {
        PresetDef p;
        p.name = "Bouncing Ball Ratchets";
        p.category = "Arp v1.5";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 85.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.06f;

        // Varying ratchet counts — every step ratchets differently
        int32_t rat8[] = {1, 2, 3, 4, 2, 3, 4, 2};
        setRatchetLane(p.state, 8, rat8);
        setRatchetDecay(p.state, 55.0f);         // 55% velocity decay per sub-step
        setRatchetShuffle(p.state, kShuffleTriplet); // triplet feel

        // Velocity pattern with strong downbeats
        float vel8[] = {1.0f, 0.7f, 0.85f, 0.65f, 0.95f, 0.7f, 0.8f, 0.65f};
        setVelocityLane(p.state, 8, vel8);

        // Gate shortened so each ratchet pop is tight
        float gate8[] = {0.6f, 0.55f, 0.5f, 0.45f, 0.6f, 0.55f, 0.5f, 0.45f};
        setGateLane(p.state, 8, gate8);

        setScale(p.state, kScaleMinorPenta, kRootA);
        presets.push_back(std::move(p));
    }

    // 2. "Guitar Strum Groove" — showcases Strum Mode
    {
        PresetDef p;
        p.name = "Guitar Strum Groove";
        p.category = "Arp v1.5";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeChord);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_4);
        setArpGateLength(p.state, 90.0f);
        p.state.arp.octaveRange = 1;
        p.state.arp.humanize = 0.08f;
        p.state.arp.swing = 15.0f;

        // Chord lane: triad → 7th → triad → 9th
        int32_t chords4[] = {kChordTriad, kChord7th, kChordTriad, kChord9th};
        setChordLane(p.state, 4, chords4);

        // Inversions cycle for voice leading
        int32_t inv4[] = {kInvRoot, kInv1st, kInv2nd, kInv1st};
        setInversionLane(p.state, 4, inv4);
        setVoicingMode(p.state, 2); // Spread

        // Strum: 60ms with Alternate direction for guitar up/down feel
        setStrum(p.state, 60.0f, kStrumAlternate);

        // Velocity accent on 1 and 3
        float vel4[] = {1.0f, 0.75f, 0.9f, 0.7f};
        setVelocityLane(p.state, 4, vel4);

        setScale(p.state, kScaleMajor, kRootG);
        presets.push_back(std::move(p));
    }

    // 3. "Polymetric Pulse" — showcases Per-Lane Swing + Per-Lane Speed
    {
        PresetDef p;
        p.name = "Polymetric Pulse";
        p.category = "Arp v1.5";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 65.0f);
        p.state.arp.octaveRange = 2;

        // Each lane swings independently — Velocity hard swing, Gate straight,
        // Pitch light swing, Ratchet dotted feel
        setLaneSwings(p.state,
            60.0f,  // velocity
            0.0f,   // gate
            25.0f,  // pitch
            0.0f,   // modifier
            50.0f,  // ratchet (dotted-ish)
            0.0f,   // condition
            0.0f,   // chord
            0.0f);  // inversion

        // Different speeds too — velocity half time, pitch 2x
        setLaneSpeeds(p.state,
            0.5f,   // velocity lane at half speed
            1.0f,   // gate normal
            2.0f,   // pitch 2x
            1.0f,   // modifier
            1.0f,   // ratchet
            1.0f,   // condition
            1.0f, 1.0f);

        float vel16[] = {
            1.0f, 0.6f, 0.7f, 0.55f, 0.9f, 0.6f, 0.7f, 0.55f,
            0.95f, 0.6f, 0.7f, 0.55f, 0.85f, 0.6f, 0.7f, 0.55f
        };
        setVelocityLane(p.state, 16, vel16);

        int32_t pitch16[] = {
            0, 3, 7, 5, 0, 3, 7, 5, 0, 3, 7, 5, 0, 3, 7, 5
        };
        setPitchLane(p.state, 16, pitch16);

        setScale(p.state, kScaleDorian, kRootE);
        presets.push_back(std::move(p));
    }

    // 4. "Evolving Haze" — showcases Per-Lane Length Jitter
    {
        PresetDef p;
        p.name = "Evolving Haze";
        p.category = "Arp v1.5";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeRandom);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 130.0f);
        p.state.arp.octaveRange = 3;
        p.state.arp.humanize = 0.15f;
        p.state.arp.spice = 0.3f;

        // Every lane has a different jitter amount — pattern never repeats
        setLaneJitters(p.state,
            2,  // velocity ± 2 steps
            3,  // gate ± 3
            4,  // pitch ± 4
            1,  // modifier ± 1
            2,  // ratchet
            1,  // condition
            2,  // chord
            1); // inversion

        float vel16[] = {
            0.9f, 0.6f, 0.8f, 0.7f, 0.85f, 0.65f, 0.75f, 0.55f,
            0.9f, 0.6f, 0.8f, 0.7f, 0.85f, 0.65f, 0.75f, 0.55f
        };
        setVelocityLane(p.state, 16, vel16);

        float gate16[] = {
            1.5f, 1.2f, 1.4f, 1.1f, 1.6f, 1.0f, 1.3f, 1.2f,
            1.5f, 1.2f, 1.4f, 1.1f, 1.6f, 1.0f, 1.3f, 1.2f
        };
        setGateLane(p.state, 16, gate16);

        int32_t pitch12[] = {0, 0, 5, 7, 12, 7, 5, 0, -5, 0, 5, 0};
        setPitchLane(p.state, 12, pitch12);

        setScale(p.state, kScaleMixolydian, kRootD);
        presets.push_back(std::move(p));
    }

    // 5. "Expressive Curve" — showcases Velocity Curve
    {
        PresetDef p;
        p.name = "Expressive Curve";
        p.category = "Arp v1.5";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 70.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.1f;

        // Ramp velocity from soft to loud across the pattern — curve amplifies
        float vel16[] = {
            0.2f, 0.3f, 0.35f, 0.45f, 0.5f, 0.6f, 0.65f, 0.75f,
            0.8f, 0.85f, 0.9f, 0.92f, 0.95f, 0.97f, 1.0f, 1.0f
        };
        setVelocityLane(p.state, 16, vel16);

        // S-Curve with 80% amount = smooth ease in/out of dynamics
        setVelocityCurve(p.state, kCurveS, 80.0f);

        float gate16[] = {
            0.5f, 0.6f, 0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f,
            0.95f, 1.0f, 1.05f, 1.1f, 1.15f, 1.2f, 1.25f, 1.3f
        };
        setGateLane(p.state, 16, gate16);

        setScale(p.state, kScaleMajor, kRootC);
        presets.push_back(std::move(p));
    }

    // 6. "Key-Locked Wander" — showcases Scale-aware Transpose + Gravity mode
    {
        PresetDef p;
        p.name = "Key-Locked Wander";
        p.category = "Arp v1.5";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeGravity);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8T);
        setArpGateLength(p.state, 100.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.08f;

        // Transpose +5 scale degrees — shifts entire pattern up a 6th in scale
        setTranspose(p.state, 5);

        setScale(p.state, kScaleBlues, kRootE);

        // Gentle velocity variation
        float vel16[] = {
            0.85f, 0.7f, 0.8f, 0.75f, 0.9f, 0.7f, 0.8f, 0.75f,
            0.85f, 0.7f, 0.8f, 0.75f, 0.9f, 0.7f, 0.8f, 0.75f
        };
        setVelocityLane(p.state, 16, vel16);

        // Gravity mode picks nearest note — smooth stepwise feel
        presets.push_back(std::move(p));
    }

    // 7. "Bass Register Lock" — showcases Note Range Mapping
    {
        PresetDef p;
        p.name = "Bass Register Lock";
        p.category = "Arp v1.5";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 80.0f);
        p.state.arp.octaveRange = 4; // wide range, but...

        // ...clamp to bass register: C1-C3 (MIDI 24-48)
        setNoteRange(p.state, 24, 48, kRangeWrap);

        // Pitch lane adds variation that would escape the register without wrap
        int32_t pitch16[] = {
            0, 12, 0, 7, 0, 12, -12, 7, 0, 12, 0, 7, -5, 12, 0, 7
        };
        setPitchLane(p.state, 16, pitch16);

        // Velocity accents every 4
        float vel16[] = {
            1.0f, 0.65f, 0.8f, 0.7f, 0.95f, 0.65f, 0.8f, 0.7f,
            0.9f, 0.65f, 0.8f, 0.7f, 0.95f, 0.65f, 0.8f, 0.7f
        };
        setVelocityLane(p.state, 16, vel16);

        float gate16[] = {
            0.8f, 0.5f, 0.65f, 0.55f, 0.85f, 0.5f, 0.65f, 0.55f,
            0.8f, 0.5f, 0.65f, 0.55f, 0.85f, 0.5f, 0.65f, 0.55f
        };
        setGateLane(p.state, 16, gate16);

        setScale(p.state, kScalePhrygian, kRootE);
        presets.push_back(std::move(p));
    }

    // 8. "Pedal Tone Melody" — showcases Step Pinning
    {
        PresetDef p;
        p.name = "Pedal Tone Melody";
        p.category = "Arp v1.5";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 90.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.humanize = 0.05f;

        // Pin every other step to a low drone note (MIDI 36 = C2)
        setPinNote(p.state, 36);
        int pinFlags16[] = {
            1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0
        };
        setPinFlags(p.state, 16, pinFlags16);

        // The arpeggio fills in the unpinned steps
        p.state.arp.pitchLaneLength = 16;

        // Strong velocity on pinned drone steps
        float vel16[] = {
            1.0f, 0.6f, 0.7f, 0.55f, 1.0f, 0.6f, 0.7f, 0.55f,
            1.0f, 0.6f, 0.7f, 0.55f, 1.0f, 0.6f, 0.7f, 0.55f
        };
        setVelocityLane(p.state, 16, vel16);

        setScale(p.state, kScaleNatMinor, kRootC);
        presets.push_back(std::move(p));
    }

    // 9. "Full Kitchen Sink" — uses ALL new features at once
    {
        PresetDef p;
        p.name = "Full Kitchen Sink";
        p.category = "Arp v1.5";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeGravity);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 85.0f);
        p.state.arp.octaveRange = 3;
        p.state.arp.humanize = 0.1f;
        p.state.arp.spice = 0.2f;

        // Ratchet with decay + triplet shuffle
        int32_t rat16[] = {
            1, 1, 2, 1, 1, 3, 1, 2,
            1, 1, 4, 1, 1, 2, 3, 1
        };
        setRatchetLane(p.state, 16, rat16);
        setRatchetDecay(p.state, 40.0f);
        setRatchetShuffle(p.state, kShuffleTriplet);

        // Chord lane with strum
        int32_t chords8[] = {
            kChordNone, kChordNone, kChordTriad, kChordNone,
            kChordNone, kChord7th, kChordNone, kChord9th
        };
        setChordLane(p.state, 8, chords8);
        setStrum(p.state, 35.0f, kStrumAlternate);
        setVoicingMode(p.state, 2); // Spread

        // Per-lane swing
        setLaneSwings(p.state, 40.0f, 20.0f, 0.0f, 0.0f, 30.0f, 0.0f, 15.0f, 0.0f);

        // Per-lane jitter
        setLaneJitters(p.state, 1, 2, 1, 0, 2, 0, 1, 0);

        // Per-lane speeds (polymetric)
        setLaneSpeeds(p.state, 1.0f, 1.0f, 2.0f, 1.0f, 0.5f, 1.0f, 0.5f, 0.5f);

        // Velocity curve
        float vel16[] = {
            0.9f, 0.55f, 0.75f, 0.6f, 0.95f, 0.55f, 0.7f, 0.6f,
            0.85f, 0.55f, 0.75f, 0.6f, 1.0f, 0.55f, 0.7f, 0.6f
        };
        setVelocityLane(p.state, 16, vel16);
        setVelocityCurve(p.state, kCurveExp, 50.0f);

        // Note range (clamp to playable register)
        setNoteRange(p.state, 36, 84, kRangeClamp);

        // Transpose +2 scale degrees
        setTranspose(p.state, 2);

        // Pin first step of every bar to D2 (38)
        setPinNote(p.state, 38);
        int pinFlags16[] = {
            1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0
        };
        setPinFlags(p.state, 16, pinFlags16);

        // Euclidean rhythm underneath it all
        setEuclidean(p.state, true, 7, 16, 2);

        setScale(p.state, kScaleDorian, kRootD);
        presets.push_back(std::move(p));
    }

    // 10. "Cascading Harp" — Gravity mode + Strum + velocity curve
    {
        PresetDef p;
        p.name = "Cascading Harp";
        p.category = "Arp v1.5";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeGravity);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8T);
        setArpGateLength(p.state, 140.0f);  // legato
        p.state.arp.octaveRange = 3;
        p.state.arp.humanize = 0.12f;

        // Chord lane all triads → smooth voice-led chord sequence via Gravity
        int32_t chords4[] = {kChordTriad, kChordTriad, kChordTriad, kChord7th};
        setChordLane(p.state, 4, chords4);

        // Strum 45ms Up direction — classic harp arpeggio feel
        setStrum(p.state, 45.0f, kStrumUp);
        setVoicingMode(p.state, 0); // Close voicing for clean harp

        // Logarithmic velocity curve — fast rise, soft tail
        setVelocityCurve(p.state, kCurveLog, 60.0f);

        // Gentle velocity wave
        float vel12[] = {
            0.95f, 0.7f, 0.8f, 0.85f, 0.7f, 0.9f,
            0.95f, 0.7f, 0.8f, 0.85f, 0.7f, 0.9f
        };
        setVelocityLane(p.state, 12, vel12);

        // Light per-lane swing on velocity for breathing
        setLaneSwings(p.state, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 10.0f, 0.0f);

        setScale(p.state, kScaleMajor, kRootF);
        presets.push_back(std::move(p));
    }

    return presets;
}

// ==============================================================================
// Main
// ==============================================================================

int main(int argc, char* argv[]) {
    std::filesystem::path outputDir = "plugins/gradus/resources/presets";

    if (argc > 1) {
        outputDir = argv[1];
    }

    std::filesystem::create_directories(outputDir);

    auto presets = createAllPresets();
    int successCount = 0;

    std::cout << "Generating " << presets.size()
              << " Gradus factory presets..." << std::endl;

    for (const auto& preset : presets) {
        auto stateData = preset.state.serialize();

        auto categoryDir = outputDir / preset.category;
        std::filesystem::create_directories(categoryDir);

        // Create filename: spaces -> underscores, keep alphanumeric and hyphens
        std::string filename;
        for (char c : preset.name) {
            if (c == ' ')
                filename += '_';
            else if (c == '/')
                filename += '-';
            else if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
                     c == '(' || c == ')')
                filename += c;
        }
        filename += ".vstpreset";

        auto path = categoryDir / filename;

        if (writeVstPreset(path, stateData)) {
            std::cout << "  Created: " << preset.category << "/"
                      << filename << " (" << stateData.size() << " bytes)"
                      << std::endl;
            successCount++;
        }
    }

    std::cout << "\nGenerated " << successCount << " of " << presets.size()
              << " presets." << std::endl;
    std::cout << "Output directory: " << std::filesystem::absolute(outputDir)
              << std::endl;

    return (successCount == static_cast<int>(presets.size())) ? 0 : 1;
}
