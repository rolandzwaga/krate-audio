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

    // ==================== Classic Category (3 presets) ====================

    // "Basic Up 1/16"
    {
        PresetDef p;
        p.name = "Basic Up 1/16";
        p.category = "Arp Classic";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 80.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        float vel8[] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
        setVelocityLane(p.state, 8, vel8);
        float gate8[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        setGateLane(p.state, 8, gate8);
        presets.push_back(std::move(p));
    }

    // "Down 1/8"
    {
        PresetDef p;
        p.name = "Down 1/8";
        p.category = "Arp Classic";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 90.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        float vel8[] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
        setVelocityLane(p.state, 8, vel8);
        float gate8[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        setGateLane(p.state, 8, gate8);
        presets.push_back(std::move(p));
    }

    // "UpDown 1/8T"
    {
        PresetDef p;
        p.name = "UpDown 1/8T";
        p.category = "Arp Classic";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8T);
        setArpGateLength(p.state, 75.0f);
        setArpSwing(p.state, 0.0f);
        p.state.arp.octaveRange = 2;
        // Accent pattern: alternating 0.6/1.0
        float velAccent8[] = {0.6f, 1.0f, 0.6f, 1.0f, 0.6f, 1.0f, 0.6f, 1.0f};
        setVelocityLane(p.state, 8, velAccent8);
        float gate8[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        setGateLane(p.state, 8, gate8);
        presets.push_back(std::move(p));
    }

    // ==================== Acid Category (2 presets) ====================

    // "Acid Line 303"
    {
        PresetDef p;
        p.name = "Acid Line 303";
        p.category = "Arp Acid";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 60.0f);
        float vel8[] = {0.75f, 0.75f, 0.75f, 0.75f,
                        0.75f, 0.75f, 0.75f, 0.75f};
        setVelocityLane(p.state, 8, vel8);
        int32_t pitch8[] = {0, 0, 3, 0, 5, 0, 3, 7};
        setPitchLane(p.state, 8, pitch8);
        // Modifier lane: slide on steps 3,7; accent on steps 5; both on step 7
        int32_t mod8[] = {
            kStepActive,                            // step 1
            kStepActive,                            // step 2
            kStepActive | kStepSlide,               // step 3 (slide)
            kStepActive,                            // step 4
            kStepActive | kStepAccent,              // step 5 (accent)
            kStepActive,                            // step 6
            kStepActive | kStepSlide | kStepAccent, // step 7 (slide+accent)
            kStepActive                             // step 8
        };
        setModifierLane(p.state, 8, mod8, 100, 50.0f);
        presets.push_back(std::move(p));
    }

    // "Acid Stab"
    {
        PresetDef p;
        p.name = "Acid Stab";
        p.category = "Arp Acid";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 40.0f);
        float vel8[] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
        setVelocityLane(p.state, 8, vel8);
        int32_t pitch8[] = {0, 0, 0, 0, 0, 0, 0, 0};
        setPitchLane(p.state, 8, pitch8);
        // All steps: active + accent
        int32_t mod8[] = {
            kStepActive | kStepAccent, kStepActive | kStepAccent,
            kStepActive | kStepAccent, kStepActive | kStepAccent,
            kStepActive | kStepAccent, kStepActive | kStepAccent,
            kStepActive | kStepAccent, kStepActive | kStepAccent
        };
        setModifierLane(p.state, 8, mod8, 110, 50.0f);
        presets.push_back(std::move(p));
    }

    // ==================== Euclidean Category (3 presets) ====================

    // "Tresillo E(3,8)"
    {
        PresetDef p;
        p.name = "Tresillo E(3,8)";
        p.category = "Arp Euclidean";
        setArpEnabled(p.state, true);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 80.0f);
        p.state.arp.octaveRange = 1;
        setEuclidean(p.state, true, 3, 8, 0);
        float vel8[] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
        setVelocityLane(p.state, 8, vel8);
        presets.push_back(std::move(p));
    }

    // "Bossa E(5,16)"
    {
        PresetDef p;
        p.name = "Bossa E(5,16)";
        p.category = "Arp Euclidean";
        setArpEnabled(p.state, true);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 75.0f);
        p.state.arp.octaveRange = 1;
        setEuclidean(p.state, true, 5, 16, 0);
        float vel16[16];
        for (int i = 0; i < 16; ++i) vel16[i] = 0.75f;
        setVelocityLane(p.state, 16, vel16);
        presets.push_back(std::move(p));
    }

    // "Samba E(7,16)"
    {
        PresetDef p;
        p.name = "Samba E(7,16)";
        p.category = "Arp Euclidean";
        setArpEnabled(p.state, true);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 70.0f);
        p.state.arp.octaveRange = 2;
        setEuclidean(p.state, true, 7, 16, 0);
        float vel16[16];
        for (int i = 0; i < 16; ++i) vel16[i] = 0.8f;
        setVelocityLane(p.state, 16, vel16);
        presets.push_back(std::move(p));
    }

    // ==================== Polymetric Category (2 presets) ====================

    // "3x5x7 Evolving"
    {
        PresetDef p;
        p.name = "3x5x7 Evolving";
        p.category = "Arp Polymetric";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        // Velocity lane length=3
        float vel3[] = {0.5f, 0.8f, 1.0f};
        setVelocityLane(p.state, 3, vel3);
        // Gate lane length=5
        float gate5[] = {0.8f, 1.2f, 0.6f, 1.0f, 0.4f};
        setGateLane(p.state, 5, gate5);
        // Pitch lane length=7
        int32_t pitch7[] = {0, 3, 0, 7, 0, -2, 5};
        setPitchLane(p.state, 7, pitch7);
        // Ratchet lane length=4
        int32_t ratch4[] = {1, 1, 2, 1};
        setRatchetLane(p.state, 4, ratch4);
        presets.push_back(std::move(p));
    }

    // "4x5 Shifting"
    {
        PresetDef p;
        p.name = "4x5 Shifting";
        p.category = "Arp Polymetric";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        // Ratchet lane length=4
        int32_t ratch4[] = {1, 2, 1, 2};
        setRatchetLane(p.state, 4, ratch4);
        // Velocity lane length=5
        float vel5[] = {0.6f, 1.0f, 0.7f, 0.9f, 0.5f};
        setVelocityLane(p.state, 5, vel5);
        // Gate lane length=6
        float gate6[] = {0.8f, 1.1f, 0.7f, 1.0f, 0.6f, 0.9f};
        setGateLane(p.state, 6, gate6);
        presets.push_back(std::move(p));
    }

    // ==================== Generative Category (2 presets) ====================

    // "Spice Evolver"
    {
        PresetDef p;
        p.name = "Spice Evolver";
        p.category = "Arp Generative";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        p.state.arp.octaveRange = 2;
        p.state.arp.spice = 0.7f;
        p.state.arp.humanize = 0.3f;
        // Condition lane length=8 with mixed conditions
        int32_t cond8[] = {
            kCondAlways, kCondProb50, kCondAlways, kCondProb75,
            kCondAlways, kCondProb25, kCondProb50, kCondAlways
        };
        setConditionLane(p.state, 8, cond8, 0);
        // Varied velocity lane
        float vel8[] = {0.7f, 0.9f, 0.5f, 1.0f, 0.6f, 0.8f, 0.4f, 0.95f};
        setVelocityLane(p.state, 8, vel8);
        presets.push_back(std::move(p));
    }

    // "Chaos Garden"
    {
        PresetDef p;
        p.name = "Chaos Garden";
        p.category = "Arp Generative";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeRandom);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        p.state.arp.spice = 0.9f;
        p.state.arp.humanize = 0.5f;
        // Condition lane length=16 with cycling probabilities
        int32_t cond16[] = {
            kCondProb10, kCondProb25, kCondProb50, kCondProb75,
            kCondProb90, kCondProb10, kCondProb25, kCondProb50,
            kCondProb75, kCondProb90, kCondProb10, kCondProb25,
            kCondProb50, kCondProb75, kCondProb90, kCondProb10
        };
        setConditionLane(p.state, 16, cond16, 0);
        // Velocity lane 16 steps uniform
        float vel16[16];
        for (int i = 0; i < 16; ++i) vel16[i] = 0.8f;
        setVelocityLane(p.state, 16, vel16);
        // Pitch lane length=8
        int32_t pitch8[] = {0, 2, 4, 7, 9, 12, -5, 0};
        setPitchLane(p.state, 8, pitch8);
        presets.push_back(std::move(p));
    }

    // ==================== Performance Category (2 presets) ====================

    // "Fill Cascade"
    {
        PresetDef p;
        p.name = "Fill Cascade";
        p.category = "Arp Performance";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        p.state.arp.octaveRange = 2;
        // Condition lane: Fill on steps 5-8 and 13-16 (0-indexed: 4-7, 12-15)
        int32_t cond16[] = {
            kCondAlways, kCondAlways, kCondAlways, kCondAlways,
            kCondFill,   kCondFill,   kCondFill,   kCondFill,
            kCondAlways, kCondAlways, kCondAlways, kCondAlways,
            kCondFill,   kCondFill,   kCondFill,   kCondFill
        };
        setConditionLane(p.state, 16, cond16, 0);
        // Velocity lane 16 steps uniform
        float vel16[16];
        for (int i = 0; i < 16; ++i) vel16[i] = 0.8f;
        setVelocityLane(p.state, 16, vel16);
        // Gate lane 16 steps uniform
        float gate16[16];
        for (int i = 0; i < 16; ++i) gate16[i] = 0.9f;
        setGateLane(p.state, 16, gate16);
        presets.push_back(std::move(p));
    }

    // "Probability Waves"
    {
        PresetDef p;
        p.name = "Probability Waves";
        p.category = "Arp Performance";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        // Condition lane: Prob75 on even steps (1-indexed), Prob25 on odd
        int32_t cond16[] = {
            kCondProb25, kCondProb75, kCondProb25, kCondProb75,
            kCondProb25, kCondProb75, kCondProb25, kCondProb75,
            kCondProb25, kCondProb75, kCondProb25, kCondProb75,
            kCondProb25, kCondProb75, kCondProb25, kCondProb75
        };
        setConditionLane(p.state, 16, cond16, 0);
        // Velocity lane 8 steps alternating
        float vel8[] = {0.6f, 1.0f, 0.6f, 1.0f, 0.6f, 1.0f, 0.6f, 1.0f};
        setVelocityLane(p.state, 8, vel8);
        // Modifier lane: accent on even steps (0-indexed 1,3,5,7)
        int32_t mod8[] = {
            kStepActive,                // step 1 (odd)
            kStepActive | kStepAccent,  // step 2 (even)
            kStepActive,                // step 3 (odd)
            kStepActive | kStepAccent,  // step 4 (even)
            kStepActive,                // step 5 (odd)
            kStepActive | kStepAccent,  // step 6 (even)
            kStepActive,                // step 7 (odd)
            kStepActive | kStepAccent   // step 8 (even)
        };
        setModifierLane(p.state, 8, mod8, 30, 60.0f);
        // Ratchet lane length=8
        int32_t ratch8[] = {1, 2, 1, 2, 1, 2, 1, 2};
        setRatchetLane(p.state, 8, ratch8);
        presets.push_back(std::move(p));
    }

    // ==================== Arp Chords Category (5 presets) ====================

    // "Diatonic Triads" - Rising triads in C Major, classic arpeggiated chords
    {
        PresetDef p;
        p.name = "Diatonic Triads";
        p.category = "Arp Chords";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 90.0f);
        p.state.arp.octaveRange = 2;
        // Scale: C Major for proper diatonic chord stacking
        p.state.arp.scaleType = 0;  // Major
        p.state.arp.rootNote = 0;   // C
        // Chord lane: alternating triads and single notes
        int32_t chords8[] = {
            kChordTriad, kChordNone, kChordTriad, kChordNone,
            kChordTriad, kChordNone, kChordTriad, kChordNone
        };
        setChordLane(p.state, 8, chords8);
        // Velocity: strong on chord steps, softer on single notes
        float vel8[] = {1.0f, 0.6f, 0.9f, 0.5f, 0.85f, 0.55f, 0.95f, 0.5f};
        setVelocityLane(p.state, 8, vel8);
        presets.push_back(std::move(p));
    }

    // "Minor 7th Pulse" - Dark minor 7th chords with rhythmic gate
    {
        PresetDef p;
        p.name = "Minor 7th Pulse";
        p.category = "Arp Chords";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 60.0f);
        // Scale: A Natural Minor
        p.state.arp.scaleType = 1;  // NaturalMinor
        p.state.arp.rootNote = 9;   // A
        // Chord lane: 7th chords on downbeats, dyads on offbeats
        int32_t chords8[] = {
            kChord7th, kChordDyad, kChordNone, kChordDyad,
            kChord7th, kChordNone, kChordDyad, kChordNone
        };
        setChordLane(p.state, 8, chords8);
        // Inversion lane: vary voicings across steps
        int32_t inv8[] = {
            kInvRoot, kInv1st, kInvRoot, kInv2nd,
            kInv1st, kInvRoot, kInv2nd, kInvRoot
        };
        setInversionLane(p.state, 8, inv8);
        // Short staccato gate for pulse feel
        float gate8[] = {0.8f, 0.4f, 0.3f, 0.4f, 0.8f, 0.3f, 0.5f, 0.3f};
        setGateLane(p.state, 8, gate8);
        presets.push_back(std::move(p));
    }

    // "Chord Cascade" - Polymetric chord/inversion evolving pattern
    {
        PresetDef p;
        p.name = "Chord Cascade";
        p.category = "Arp Chords";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 85.0f);
        p.state.arp.octaveRange = 2;
        // Scale: D Dorian
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
        // Pitch lane length=3 for extra polymetry
        int32_t pitch3[] = {0, 5, -3};
        setPitchLane(p.state, 3, pitch3);
        // Velocity 4 steps
        float vel4[] = {0.9f, 0.6f, 0.8f, 0.5f};
        setVelocityLane(p.state, 4, vel4);
        presets.push_back(std::move(p));
    }

    // "Spread Ninths" - Wide voicing 9th chords, ambient
    {
        PresetDef p;
        p.name = "Spread Ninths";
        p.category = "Arp Chords";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_4);
        setArpGateLength(p.state, 100.0f);
        // Scale: F Lydian for bright 9th chords
        p.state.arp.scaleType = 7;  // Lydian
        p.state.arp.rootNote = 5;   // F
        // Spread voicing for wide register
        setVoicingMode(p.state, 2);  // Spread
        // Chord lane: all 9th chords
        int32_t chords4[] = {kChord9th, kChord9th, kChord7th, kChord9th};
        setChordLane(p.state, 4, chords4);
        // Inversion: cycle through
        int32_t inv4[] = {kInvRoot, kInv1st, kInv2nd, kInvRoot};
        setInversionLane(p.state, 4, inv4);
        // Slow velocity swell
        float vel4[] = {0.5f, 0.7f, 0.85f, 1.0f};
        setVelocityLane(p.state, 4, vel4);
        presets.push_back(std::move(p));
    }

    // "Stab Machine" - Rhythmic chord stabs with ratchets
    {
        PresetDef p;
        p.name = "Stab Machine";
        p.category = "Arp Chords";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 50.0f);
        setArpSwing(p.state, 15.0f);
        // Scale: G Mixolydian for funky dominant feel
        p.state.arp.scaleType = 5;  // Mixolydian
        p.state.arp.rootNote = 7;   // G
        // Chord lane: stabs on strong beats, rests between
        int32_t chords16[] = {
            kChordTriad, kChordNone, kChordNone, kChordDyad,
            kChordNone, kChordTriad, kChordNone, kChordNone,
            kChordDyad, kChordNone, kChordNone, kChordTriad,
            kChordNone, kChordDyad, kChordNone, kChordNone
        };
        setChordLane(p.state, 16, chords16);
        // Inversion: root position on triads, 1st on dyads for tighter voicing
        int32_t inv16[] = {
            kInvRoot, kInvRoot, kInvRoot, kInv1st,
            kInvRoot, kInvRoot, kInvRoot, kInvRoot,
            kInv1st, kInvRoot, kInvRoot, kInv2nd,
            kInvRoot, kInv1st, kInvRoot, kInvRoot
        };
        setInversionLane(p.state, 16, inv16);
        // Ratchet for rhythmic interest
        int32_t ratch8[] = {1, 1, 2, 1, 1, 1, 2, 1};
        setRatchetLane(p.state, 8, ratch8);
        // Velocity: accented stabs
        float vel16[] = {
            1.0f, 0.4f, 0.3f, 0.8f,
            0.3f, 0.95f, 0.3f, 0.3f,
            0.75f, 0.3f, 0.3f, 0.9f,
            0.3f, 0.7f, 0.3f, 0.3f
        };
        setVelocityLane(p.state, 16, vel16);
        presets.push_back(std::move(p));
    }

    // ==================== Arp Advanced Category (6 presets) ====================
    // Ported from Ruinae "Arp Modulation" — arp pattern data only

    // "Arp Filter Sweep" - Pitch lane with chromatic walk
    {
        PresetDef p;
        p.name = "Arp Filter Sweep";
        p.category = "Arp Advanced";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUp);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 70.0f);
        p.state.arp.octaveRange = 2;
        // Pitch lane: chromatic walk up and down
        int32_t pitch8[] = {0, 2, 4, 7, 12, 7, 4, 2};
        setPitchLane(p.state, 8, pitch8);
        presets.push_back(std::move(p));
    }

    // "Arp Morph Sequence" - UpDown with pitch lane for melodic variation
    {
        PresetDef p;
        p.name = "Arp Morph Sequence";
        p.category = "Arp Advanced";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 80.0f);
        setArpSwing(p.state, 15.0f);
        p.state.arp.octaveRange = 2;
        int32_t pitchM[] = {0, 3, 7, 12, 7, 3, 0, -5};
        setPitchLane(p.state, 8, pitchM);
        presets.push_back(std::move(p));
    }

    // "Arp Tilt Cascade" - Converge pattern with wider intervals
    {
        PresetDef p;
        p.name = "Arp Tilt Cascade";
        p.category = "Arp Advanced";
        setArpEnabled(p.state, true);
        setArpMode(p.state, 4); // Converge
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 60.0f);
        p.state.arp.octaveRange = 3;
        // Pitch lane with wider intervals
        int32_t pitchC[] = {0, 5, -3, 7, -7, 12, 0, -12};
        setPitchLane(p.state, 8, pitchC);
        // Velocity lane for dynamic accent
        float velC[] = {1.0f, 0.6f, 0.8f, 0.5f, 1.0f, 0.7f, 0.9f, 0.4f};
        setVelocityLane(p.state, 8, velC);
        presets.push_back(std::move(p));
    }

    // "Arp Chaos Matrix" - Random walk with complex lanes
    {
        PresetDef p;
        p.name = "Arp Chaos Matrix";
        p.category = "Arp Advanced";
        setArpEnabled(p.state, true);
        setArpMode(p.state, 7); // Walk (random walk)
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 65.0f);
        setArpSwing(p.state, 20.0f);
        p.state.arp.octaveRange = 2;
        p.state.arp.spice = 0.4f;
        p.state.arp.humanize = 0.2f;
        int32_t pitchX[] = {0, 3, -2, 5, 7, -4, 12, -7};
        setPitchLane(p.state, 8, pitchX);
        // Ratchet for rhythmic interest
        int32_t ratchX[] = {1, 1, 2, 1, 1, 3, 1, 2};
        setRatchetLane(p.state, 8, ratchX);
        presets.push_back(std::move(p));
    }

    // "Arp FX Depth" - As-played with euclidean and swing
    {
        PresetDef p;
        p.name = "Arp FX Depth";
        p.category = "Arp Advanced";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeAsPlayed);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_8);
        setArpGateLength(p.state, 90.0f);
        setArpSwing(p.state, 30.0f);
        p.state.arp.octaveRange = 1;
        setEuclidean(p.state, true, 5, 8, 1);
        int32_t pitchF[] = {0, 4, 7, 12, 0, -3, 5, 10};
        setPitchLane(p.state, 8, pitchF);
        // Gate lane with dynamic lengths
        float gateF[] = {1.0f, 0.5f, 1.5f, 0.3f, 1.0f, 0.7f, 1.2f, 0.4f};
        setGateLane(p.state, 8, gateF);
        presets.push_back(std::move(p));
    }

    // "Arp Self Modulator" - UpDown with slides and accents
    {
        PresetDef p;
        p.name = "Arp Self Modulator";
        p.category = "Arp Advanced";
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        setArpGateLength(p.state, 75.0f);
        p.state.arp.octaveRange = 2;
        int32_t pitchS[] = {0, 2, 4, 5, 7, 9, 11, 12};
        setPitchLane(p.state, 8, pitchS);
        // Modifiers: slides on steps 4 and 8, accents on 1 and 5
        int32_t modS[] = {
            kStepActive | kStepAccent, kStepActive, kStepActive,
            kStepActive | kStepSlide, kStepActive | kStepAccent,
            kStepActive, kStepActive, kStepActive | kStepSlide
        };
        setModifierLane(p.state, 8, modS, 35, 80.0f);
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
