// ==============================================================================
// Factory Preset Generator for Ruinae
// ==============================================================================
// Generates .vstpreset files matching the Processor::getState() binary format.
// Run this tool once during development to create factory presets.
//
// Reference: plugins/ruinae/src/processor/processor.cpp getState() (lines 488-559)
// Reference: tools/disrumpo_preset_generator.cpp (established pattern)
// ==============================================================================

#include "ruinae_preset_format.h"

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

using namespace RuinaeFormat;

// ==============================================================================
// Constants (preset-generator-specific)
// ==============================================================================

// kProcessorUID(0xA3B7C1D5, 0x2E4F6A8B, 0x9C0D1E2F, 0x3A4B5C6D)
const char kClassIdAscii[33] = "A3B7C1D52E4F6A8B9C0D1E2F3A4B5C6D";

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
    RuinaePresetState state;
};

// ==============================================================================
// Arp Helper Functions (T020-T022)
// ==============================================================================

void setArpEnabled(RuinaePresetState& s, bool enabled) {
    s.arp.enabled = enabled ? 1 : 0;
}

void setArpMode(RuinaePresetState& s, int32_t mode) {
    s.arp.mode = mode;
}

void setArpRate(RuinaePresetState& s, int32_t noteValueIndex) {
    s.arp.noteValue = noteValueIndex;
}

void setArpGateLength(RuinaePresetState& s, float gateLength) {
    s.arp.gateLength = gateLength;
}

void setArpSwing(RuinaePresetState& s, float swing) {
    s.arp.swing = swing;
}

void setTempoSync(RuinaePresetState& s, bool tempoSync) {
    s.arp.tempoSync = tempoSync ? 1 : 0;
}

void setVelocityLane(RuinaePresetState& s, int32_t length,
                     const float* steps) {
    s.arp.velocityLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.velocityLaneSteps[i] = steps[i];
}

void setGateLane(RuinaePresetState& s, int32_t length,
                 const float* steps) {
    s.arp.gateLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.gateLaneSteps[i] = steps[i];
}

void setPitchLane(RuinaePresetState& s, int32_t length,
                  const int32_t* steps) {
    s.arp.pitchLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.pitchLaneSteps[i] = steps[i];
}

void setModifierLane(RuinaePresetState& s, int32_t length,
                     const int32_t* steps, int32_t accentVelocity,
                     float slideTime) {
    s.arp.modifierLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.modifierLaneSteps[i] = steps[i];
    s.arp.accentVelocity = accentVelocity;
    s.arp.slideTime = slideTime;
}

void setRatchetLane(RuinaePresetState& s, int32_t length,
                    const int32_t* steps) {
    s.arp.ratchetLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.ratchetLaneSteps[i] = steps[i];
}

void setConditionLane(RuinaePresetState& s, int32_t length,
                      const int32_t* steps, int32_t fillToggle) {
    s.arp.conditionLaneLength = std::clamp(length, 1, 32);
    for (int i = 0; i < length && i < 32; ++i)
        s.arp.conditionLaneSteps[i] = steps[i];
    s.arp.fillToggle = fillToggle;
}

void setEuclidean(RuinaePresetState& s, bool enabled, int32_t hits,
                  int32_t steps, int32_t rotation) {
    s.arp.euclideanEnabled = enabled ? 1 : 0;
    s.arp.euclideanHits = hits;
    s.arp.euclideanSteps = steps;
    s.arp.euclideanRotation = rotation;
}

// ==============================================================================
// Synth Patch Helpers (T023)
// ==============================================================================

// Warm pad: saw wave, low cutoff, slow attack/release, reverb on
void setSynthPad(RuinaePresetState& s) {
    s.oscA.waveform = 1;  // Sawtooth
    s.oscA.level = 0.7f;
    s.oscB.type = 0;
    s.oscB.waveform = 1;  // Sawtooth
    s.oscB.fineCents = 8.0f;  // Slight detune for warmth
    s.oscB.level = 0.5f;
    s.mixer.position = 0.45f;  // Slightly favor Osc A
    s.filter.type = 0;  // Ladder LP
    s.filter.cutoffHz = 3000.0f;
    s.filter.resonance = 0.2f;
    s.ampEnv.attackMs = 200.0f;
    s.ampEnv.decayMs = 500.0f;
    s.ampEnv.sustain = 0.7f;
    s.ampEnv.releaseMs = 800.0f;
    s.reverbEnabled = 1;
    s.reverb.size = 0.6f;
    s.reverb.mix = 0.3f;
    s.reverb.damping = 0.4f;
}

// Punchy bass: sub oscillator, fast attack, mid-high filter
void setSynthBass(RuinaePresetState& s) {
    s.oscA.waveform = 1;  // Sawtooth
    s.oscA.level = 0.8f;
    s.oscA.tuneSemitones = -12.0f;  // One octave down
    s.oscB.type = 0;
    s.oscB.waveform = 3;  // Square
    s.oscB.level = 0.4f;
    s.oscB.tuneSemitones = -12.0f;
    s.mixer.position = 0.6f;
    s.filter.type = 0;  // Ladder LP
    s.filter.cutoffHz = 5000.0f;
    s.filter.resonance = 0.15f;
    s.ampEnv.attackMs = 2.0f;
    s.ampEnv.decayMs = 200.0f;
    s.ampEnv.sustain = 0.6f;
    s.ampEnv.releaseMs = 150.0f;
}

// Bright lead: saw + slight detune, high cutoff
void setSynthLead(RuinaePresetState& s) {
    s.oscA.waveform = 1;  // Sawtooth
    s.oscA.level = 0.8f;
    s.oscB.type = 0;
    s.oscB.waveform = 1;  // Sawtooth
    s.oscB.fineCents = 10.0f;  // Detune for thickness
    s.oscB.level = 0.6f;
    s.mixer.position = 0.5f;
    s.filter.type = 0;  // Ladder LP
    s.filter.cutoffHz = 8000.0f;
    s.filter.resonance = 0.2f;
    s.ampEnv.attackMs = 5.0f;
    s.ampEnv.decayMs = 300.0f;
    s.ampEnv.sustain = 0.7f;
    s.ampEnv.releaseMs = 200.0f;
}

// Squelchy acid: saw, filter with env amount, fast decay, resonance up
void setSynthAcid(RuinaePresetState& s) {
    s.oscA.waveform = 1;  // Sawtooth
    s.oscA.level = 0.9f;
    s.oscB.level = 0.0f;  // Single osc acid sound
    s.mixer.position = 0.0f;  // Osc A only
    s.filter.type = 0;  // Ladder LP
    s.filter.cutoffHz = 800.0f;  // Low cutoff for squelch
    s.filter.resonance = 0.7f;  // High resonance
    s.filter.envAmount = 4000.0f;  // Strong filter envelope
    s.ampEnv.attackMs = 1.0f;
    s.ampEnv.decayMs = 150.0f;
    s.ampEnv.sustain = 0.5f;
    s.ampEnv.releaseMs = 100.0f;
    s.filterEnv.attackMs = 1.0f;
    s.filterEnv.decayMs = 200.0f;
    s.filterEnv.sustain = 0.1f;
    s.filterEnv.releaseMs = 150.0f;
}

// ==============================================================================
// Factory Preset Definitions (T024-T037, T038)
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

// Condition constants
static constexpr int32_t kCondAlways = 0;
static constexpr int32_t kCondProb10 = 1;
static constexpr int32_t kCondProb25 = 2;
static constexpr int32_t kCondProb50 = 3;
static constexpr int32_t kCondProb75 = 4;
static constexpr int32_t kCondProb90 = 5;
static constexpr int32_t kCondFill   = 16;

std::vector<PresetDef> createAllPresets() {
    std::vector<PresetDef> presets;

    // ==================== Classic Category (3 presets) ====================

    // T024: "Basic Up 1/16"
    {
        PresetDef p;
        p.name = "Basic Up 1/16";
        p.category = "Arp Classic";
        setSynthLead(p.state);
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

    // T025: "Down 1/8"
    {
        PresetDef p;
        p.name = "Down 1/8";
        p.category = "Arp Classic";
        setSynthPad(p.state);
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

    // T026: "UpDown 1/8T"
    {
        PresetDef p;
        p.name = "UpDown 1/8T";
        p.category = "Arp Classic";
        setSynthLead(p.state);
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

    // T027: "Acid Line 303"
    {
        PresetDef p;
        p.name = "Acid Line 303";
        p.category = "Arp Acid";
        setSynthAcid(p.state);
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
        // (1-indexed in task, 0-indexed here)
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

    // T028: "Acid Stab"
    {
        PresetDef p;
        p.name = "Acid Stab";
        p.category = "Arp Acid";
        setSynthAcid(p.state);
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

    // ==================== Euclidean World Category (3 presets) ====================

    // T029: "Tresillo E(3,8)"
    {
        PresetDef p;
        p.name = "Tresillo E(3,8)";
        p.category = "Arp Euclidean";
        setSynthPad(p.state);
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

    // T030: "Bossa E(5,16)"
    {
        PresetDef p;
        p.name = "Bossa E(5,16)";
        p.category = "Arp Euclidean";
        setSynthPad(p.state);
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

    // T031: "Samba E(7,16)"
    {
        PresetDef p;
        p.name = "Samba E(7,16)";
        p.category = "Arp Euclidean";
        setSynthLead(p.state);
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

    // T032: "3x5x7 Evolving"
    {
        PresetDef p;
        p.name = "3x5x7 Evolving";
        p.category = "Arp Polymetric";
        setSynthPad(p.state);
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

    // T033: "4x5 Shifting"
    {
        PresetDef p;
        p.name = "4x5 Shifting";
        p.category = "Arp Polymetric";
        setSynthBass(p.state);
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

    // T034: "Spice Evolver"
    {
        PresetDef p;
        p.name = "Spice Evolver";
        p.category = "Arp Generative";
        setSynthLead(p.state);
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

    // T035: "Chaos Garden"
    {
        PresetDef p;
        p.name = "Chaos Garden";
        p.category = "Arp Generative";
        setSynthPad(p.state);
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

    // T036: "Fill Cascade"
    {
        PresetDef p;
        p.name = "Fill Cascade";
        p.category = "Arp Performance";
        setSynthLead(p.state);
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

    // T037: "Probability Waves"
    {
        PresetDef p;
        p.name = "Probability Waves";
        p.category = "Arp Performance";
        setSynthBass(p.state);
        setArpEnabled(p.state, true);
        setArpMode(p.state, kModeUpDown);
        setTempoSync(p.state, true);
        setArpRate(p.state, kNote1_16);
        // Condition lane: Prob75 on even steps (1-indexed), Prob25 on odd
        // Even steps (2,4,6,8,...) are 0-indexed 1,3,5,7,...
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
        // Ratchet lane length=8 (values 1-4: 1,2,1,2,1,2,1,2)
        int32_t ratch8[] = {1, 2, 1, 2, 1, 2, 1, 2};
        setRatchetLane(p.state, 8, ratch8);
        presets.push_back(std::move(p));
    }

    // ==================== PAD Category (5 presets) ====================

    // "Warm Analog" - Classic warm detuned pad
    {
        PresetDef p;
        p.name = "Warm Analog";
        p.category = "Pads";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.level = 0.7f;
        s.oscB.type = 0;
        s.oscB.waveform = 1; // Saw
        s.oscB.fineCents = 8.0f; // Detune for warmth
        s.oscB.level = 0.6f;
        s.mixer.position = 0.45f;
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 2500.0f;
        s.filter.resonance = 0.3f;
        s.filter.ladderSlope = 4;
        s.ampEnv.attackMs = 300.0f;
        s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.75f;
        s.ampEnv.releaseMs = 1500.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.7f;
        s.reverb.mix = 0.35f;
        s.reverb.damping = 0.4f;
        s.reverb.diffusion = 0.8f;
        s.global.width = 1.4f;
        s.global.spread = 0.3f;
        presets.push_back(std::move(p));
    }

    // "Glass Shimmer" - Bright, airy additive pad
    {
        PresetDef p;
        p.name = "Glass Shimmer";
        p.category = "Pads";
        auto& s = p.state;
        s.oscA.type = 4; // Additive
        s.oscA.level = 0.7f;
        s.oscA.additivePartials = 32;
        s.oscA.additiveTilt = 3.0f; // Slight brightness
        s.oscB.type = 0; // PolyBLEP
        s.oscB.waveform = 4; // Triangle
        s.oscB.level = 0.4f;
        s.oscB.tuneSemitones = 12.0f; // Octave up
        s.mixer.position = 0.4f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 8000.0f;
        s.filter.resonance = 0.15f;
        s.ampEnv.attackMs = 500.0f;
        s.ampEnv.decayMs = 1000.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 2000.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.85f;
        s.reverb.mix = 0.4f;
        s.reverb.damping = 0.3f;
        s.reverb.modRateHz = 0.3f;
        s.reverb.modDepth = 0.2f;
        s.global.width = 1.6f;
        presets.push_back(std::move(p));
    }

    // "Spectral Drift" - Evolving spectral pad with LFO modulation
    {
        PresetDef p;
        p.name = "Spectral Drift";
        p.category = "Pads";
        auto& s = p.state;
        s.oscA.type = 8; // Spectral Freeze
        s.oscA.level = 0.8f;
        s.oscA.spectralTilt = 2.0f;
        s.oscB.type = 0; // PolyBLEP
        s.oscB.waveform = 0; // Sine
        s.oscB.level = 0.3f;
        s.oscB.tuneSemitones = 12.0f;
        s.mixer.mode = 1; // Spectral Morph
        s.mixer.position = 0.5f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 6000.0f;
        s.filter.resonance = 0.2f;
        s.ampEnv.attackMs = 600.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.8f;
        s.ampEnv.releaseMs = 2500.0f;
        // LFO1 slow morph
        s.lfo1.rateHz = 0.15f;
        s.lfo1.shape = 0; // Sine
        s.lfo1.depth = 0.6f;
        s.lfo1.sync = 0;
        // Mod matrix: LFO1 -> All Voice Morph Pos
        s.modMatrix.slots[0].source = 1; // LFO 1
        s.modMatrix.slots[0].dest = 5;   // All Voice Morph Pos
        s.modMatrix.slots[0].amount = 0.5f;
        s.delayEnabled = 1;
        s.delay.mix = 0.2f;
        s.delay.feedback = 0.3f;
        s.delay.timeMs = 500.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.8f;
        s.reverb.mix = 0.4f;
        presets.push_back(std::move(p));
    }

    // "Choir" - Voice-like formant pad
    {
        PresetDef p;
        p.name = "Choir";
        p.category = "Pads";
        auto& s = p.state;
        s.oscA.type = 7; // Formant
        s.oscA.level = 0.8f;
        s.oscA.formantVowel = 0; // A
        s.oscA.formantMorph = 0.0f;
        s.oscB.type = 9; // Noise
        s.oscB.noiseColor = 1; // Pink
        s.oscB.level = 0.08f; // Subtle breath
        s.mixer.position = 0.15f; // Mostly formant
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 5000.0f;
        s.filter.resonance = 0.15f;
        s.ampEnv.attackMs = 400.0f;
        s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.8f;
        s.ampEnv.releaseMs = 1200.0f;
        // LFO1 slowly morphs between vowels
        s.lfo1.rateHz = 0.08f;
        s.lfo1.shape = 1; // Triangle
        s.lfo1.depth = 0.8f;
        s.lfo1.sync = 0;
        s.modMatrix.slots[0].source = 1; // LFO 1
        s.modMatrix.slots[0].dest = 5;   // All Voice Morph Pos
        s.modMatrix.slots[0].amount = 0.4f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.75f;
        s.reverb.mix = 0.3f;
        s.reverb.damping = 0.5f;
        s.global.polyphony = 8;
        s.global.spread = 0.4f;
        presets.push_back(std::move(p));
    }

    // "Dark Matter" - Dark, evolving chaos pad
    {
        PresetDef p;
        p.name = "Dark Matter";
        p.category = "Pads";
        auto& s = p.state;
        s.oscA.type = 5; // Chaos
        s.oscA.chaosAttractor = 0; // Lorenz
        s.oscA.chaosAmount = 0.6f;
        s.oscA.chaosCoupling = 0.3f;
        s.oscA.level = 0.7f;
        s.oscB.type = 9; // Noise
        s.oscB.noiseColor = 2; // Brown
        s.oscB.level = 0.15f;
        s.mixer.position = 0.2f;
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 800.0f;
        s.filter.resonance = 0.4f;
        s.filter.ladderSlope = 4;
        s.distortion.type = 5; // Tape Saturator
        s.distortion.drive = 0.3f;
        s.distortion.tapeSaturation = 0.4f;
        s.ampEnv.attackMs = 800.0f;
        s.ampEnv.decayMs = 1000.0f;
        s.ampEnv.sustain = 0.6f;
        s.ampEnv.releaseMs = 2000.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.9f;
        s.reverb.mix = 0.45f;
        s.reverb.damping = 0.6f;
        s.reverb.diffusion = 0.85f;
        presets.push_back(std::move(p));
    }

    // ==================== LEAD Category (5 presets) ====================

    // "Supersaw" - Classic detuned supersaw lead
    {
        PresetDef p;
        p.name = "Supersaw";
        p.category = "Leads";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.level = 0.8f;
        s.oscB.type = 0;
        s.oscB.waveform = 1; // Saw
        s.oscB.fineCents = 12.0f; // Thicker detune
        s.oscB.level = 0.7f;
        s.mixer.position = 0.5f;
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 6000.0f;
        s.filter.resonance = 0.2f;
        s.filter.ladderSlope = 2; // 12dB for less aggressive roll-off
        s.ampEnv.attackMs = 5.0f;
        s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 300.0f;
        s.global.voiceMode = 1; // Mono
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 30.0f;
        s.global.width = 1.5f;
        s.global.spread = 0.2f;
        presets.push_back(std::move(p));
    }

    // "Sync Screamer" - Aggressive hard sync lead
    {
        PresetDef p;
        p.name = "Sync Screamer";
        p.category = "Leads";
        auto& s = p.state;
        s.oscA.type = 3; // Sync
        s.oscA.syncRatio = 3.0f;
        s.oscA.syncWaveform = 1; // Saw slave
        s.oscA.syncMode = 0; // Hard sync
        s.oscA.syncAmount = 1.0f;
        s.oscA.level = 0.9f;
        s.oscB.level = 0.0f; // Off
        s.mixer.position = 0.0f; // Osc A only
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 4000.0f;
        s.filter.resonance = 0.25f;
        s.filter.envAmount = 24.0f; // Strong filter env
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 250.0f;
        s.ampEnv.sustain = 0.6f;
        s.ampEnv.releaseMs = 200.0f;
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 300.0f;
        s.filterEnv.sustain = 0.2f;
        s.filterEnv.releaseMs = 200.0f;
        s.global.voiceMode = 1; // Mono
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 20.0f;
        presets.push_back(std::move(p));
    }

    // "Phase Lead" - Metallic phase distortion lead
    {
        PresetDef p;
        p.name = "Phase Lead";
        p.category = "Leads";
        auto& s = p.state;
        s.oscA.type = 2; // Phase Distortion
        s.oscA.pdWaveform = 5; // ResSaw
        s.oscA.pdDistortion = 0.6f;
        s.oscA.level = 0.8f;
        s.oscB.type = 2; // Phase Distortion
        s.oscB.pdWaveform = 6; // ResTri
        s.oscB.pdDistortion = 0.4f;
        s.oscB.level = 0.5f;
        s.oscB.tuneSemitones = 7.0f; // Fifth up
        s.mixer.position = 0.4f;
        s.filter.type = 2; // SVF BP
        s.filter.cutoffHz = 3000.0f;
        s.filter.resonance = 0.5f;
        s.ampEnv.attackMs = 3.0f;
        s.ampEnv.decayMs = 350.0f;
        s.ampEnv.sustain = 0.65f;
        s.ampEnv.releaseMs = 250.0f;
        s.global.voiceMode = 1; // Mono
        s.monoMode.portamentoTimeMs = 15.0f;
        presets.push_back(std::move(p));
    }

    // "Harmonic Bell" - Bell-like additive lead
    {
        PresetDef p;
        p.name = "Harmonic Bell";
        p.category = "Leads";
        auto& s = p.state;
        s.oscA.type = 4; // Additive
        s.oscA.additivePartials = 64;
        s.oscA.additiveInharm = 0.4f; // Inharmonicity for bell character
        s.oscA.additiveTilt = -3.0f; // Roll off highs slightly
        s.oscA.level = 0.7f;
        s.oscB.type = 0; // PolyBLEP
        s.oscB.waveform = 0; // Sine
        s.oscB.level = 0.4f;
        s.oscB.tuneSemitones = 12.0f; // Octave up
        s.mixer.position = 0.35f;
        s.filter.type = 1; // SVF HP
        s.filter.cutoffHz = 200.0f; // Remove rumble
        s.filter.resonance = 0.1f;
        s.ampEnv.attackMs = 1.0f;
        s.ampEnv.decayMs = 2000.0f;
        s.ampEnv.sustain = 0.15f; // Bell-like decay
        s.ampEnv.releaseMs = 1500.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.6f;
        s.reverb.mix = 0.35f;
        s.reverb.damping = 0.3f;
        presets.push_back(std::move(p));
    }

    // "Mono Scream" - Aggressive mono lead with wavefolder
    {
        PresetDef p;
        p.name = "Mono Scream";
        p.category = "Leads";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.level = 0.9f;
        s.oscB.type = 0;
        s.oscB.waveform = 2; // Square
        s.oscB.tuneSemitones = 7.0f; // Fifth up
        s.oscB.level = 0.5f;
        s.mixer.position = 0.4f;
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 2000.0f;
        s.filter.resonance = 0.6f;
        s.filter.envAmount = 36.0f; // Big filter sweep
        s.filter.ladderSlope = 4;
        s.filter.ladderDrive = 6.0f;
        s.distortion.type = 4; // Wavefolder
        s.distortion.drive = 0.4f;
        s.distortion.foldType = 1; // Sine fold
        s.ampEnv.attackMs = 1.0f;
        s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 150.0f;
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 300.0f;
        s.filterEnv.sustain = 0.15f;
        s.filterEnv.releaseMs = 200.0f;
        s.global.voiceMode = 1; // Mono
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 40.0f;
        presets.push_back(std::move(p));
    }

    // ==================== BASS Category (5 presets) ====================

    // "Sub Bass" - Deep, clean sub bass
    {
        PresetDef p;
        p.name = "Sub Bass";
        p.category = "Basses";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 0; // Sine
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.9f;
        s.oscB.type = 0;
        s.oscB.waveform = 4; // Triangle
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.2f; // Subtle harmonic content
        s.mixer.position = 0.15f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 500.0f;
        s.filter.resonance = 0.1f;
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 150.0f;
        s.ampEnv.sustain = 0.85f;
        s.ampEnv.releaseMs = 100.0f;
        s.global.voiceMode = 1; // Mono
        presets.push_back(std::move(p));
    }

    // "Reese" - Classic reese bass with phaser
    {
        PresetDef p;
        p.name = "Reese";
        p.category = "Basses";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.8f;
        s.oscB.type = 0;
        s.oscB.waveform = 1; // Saw
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.fineCents = 5.0f; // Slight detune
        s.oscB.level = 0.75f;
        s.mixer.position = 0.5f;
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 3000.0f;
        s.filter.resonance = 0.2f;
        s.filter.ladderSlope = 4;
        s.ampEnv.attackMs = 5.0f;
        s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.8f;
        s.ampEnv.releaseMs = 200.0f;
        s.phaserEnabled = 1;
        s.phaser.rateHz = 0.3f;
        s.phaser.depth = 0.4f;
        s.phaser.feedback = 0.5f;
        s.phaser.mix = 0.35f;
        s.phaser.stages = 2; // 6-stage phaser
        s.global.voiceMode = 1; // Mono
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 20.0f;
        presets.push_back(std::move(p));
    }

    // "Acid Bass" - 303-style acid bass
    {
        PresetDef p;
        p.name = "Acid Bass";
        p.category = "Basses";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.9f;
        s.oscB.level = 0.0f; // Single osc
        s.mixer.position = 0.0f;
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 600.0f;
        s.filter.resonance = 0.75f; // High resonance for squelch
        s.filter.envAmount = 36.0f; // Strong filter env
        s.filter.ladderSlope = 4;
        s.filter.ladderDrive = 3.0f;
        s.ampEnv.attackMs = 1.0f;
        s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.4f;
        s.ampEnv.releaseMs = 100.0f;
        s.filterEnv.attackMs = 1.0f;
        s.filterEnv.decayMs = 250.0f;
        s.filterEnv.sustain = 0.05f;
        s.filterEnv.releaseMs = 150.0f;
        s.global.voiceMode = 1; // Mono
        s.monoMode.legato = 1;
        s.monoMode.portamentoTimeMs = 30.0f;
        presets.push_back(std::move(p));
    }

    // "FM Bass" - Punchy digital FM-style bass
    {
        PresetDef p;
        p.name = "FM Bass";
        p.category = "Basses";
        auto& s = p.state;
        s.oscA.type = 2; // Phase Distortion
        s.oscA.pdWaveform = 3; // DoubleSine
        s.oscA.pdDistortion = 0.5f;
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.8f;
        s.oscB.type = 0; // PolyBLEP
        s.oscB.waveform = 2; // Square
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.4f;
        s.mixer.position = 0.3f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 4000.0f;
        s.filter.resonance = 0.15f;
        s.ampEnv.attackMs = 1.0f;
        s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.5f;
        s.ampEnv.releaseMs = 100.0f;
        s.global.voiceMode = 1; // Mono
        presets.push_back(std::move(p));
    }

    // "Wobble" - Dubstep-style wobble bass with LFO on filter
    {
        PresetDef p;
        p.name = "Wobble";
        p.category = "Basses";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.8f;
        s.oscB.type = 0;
        s.oscB.waveform = 2; // Square
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.6f;
        s.mixer.position = 0.45f;
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 2000.0f;
        s.filter.resonance = 0.5f;
        s.filter.ladderSlope = 4;
        s.filter.ladderDrive = 4.0f;
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 200.0f;
        s.ampEnv.sustain = 0.8f;
        s.ampEnv.releaseMs = 150.0f;
        // LFO1 modulating filter cutoff
        s.lfo1.rateHz = 4.0f;
        s.lfo1.shape = 0; // Sine
        s.lfo1.depth = 1.0f;
        s.lfo1.sync = 1; // Tempo sync
        s.lfo1Ext.noteValue = 13; // 1/2 note for half-bar wobble
        s.modMatrix.slots[0].source = 1; // LFO 1
        s.modMatrix.slots[0].dest = 4;   // All Voice Filter Cutoff
        s.modMatrix.slots[0].amount = 0.7f;
        s.global.voiceMode = 1; // Mono
        s.monoMode.legato = 1;
        presets.push_back(std::move(p));
    }

    // ==================== TEXTURE Category (5 presets) ====================

    // "Particle Cloud" - Granular particle texture
    {
        PresetDef p;
        p.name = "Particle Cloud";
        p.category = "Textures";
        auto& s = p.state;
        s.oscA.type = 6; // Particle
        s.oscA.particleScatter = 7.0f; // Wide scatter
        s.oscA.particleDensity = 32.0f;
        s.oscA.particleLifetime = 800.0f;
        s.oscA.particleSpawnMode = 1; // Random
        s.oscA.particleEnvType = 0; // Hann
        s.oscA.particleDrift = 0.3f;
        s.oscA.level = 0.7f;
        s.oscB.type = 9; // Noise
        s.oscB.noiseColor = 1; // Pink
        s.oscB.level = 0.1f;
        s.mixer.position = 0.15f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 4000.0f;
        s.filter.resonance = 0.2f;
        s.ampEnv.attackMs = 1000.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.85f;
        s.ampEnv.releaseMs = 3000.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.9f;
        s.reverb.mix = 0.5f;
        s.reverb.damping = 0.35f;
        s.reverb.diffusion = 0.9f;
        s.global.width = 1.8f;
        s.global.spread = 0.5f;
        presets.push_back(std::move(p));
    }

    // "Chaos Wind" - Evolving chaotic texture
    {
        PresetDef p;
        p.name = "Chaos Wind";
        p.category = "Textures";
        auto& s = p.state;
        s.oscA.type = 5; // Chaos
        s.oscA.chaosAttractor = 1; // Rossler
        s.oscA.chaosAmount = 0.7f;
        s.oscA.chaosCoupling = 0.4f;
        s.oscA.chaosOutput = 1; // Y axis
        s.oscA.level = 0.6f;
        s.oscB.type = 9; // Noise
        s.oscB.noiseColor = 2; // Brown
        s.oscB.level = 0.2f;
        s.mixer.position = 0.25f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 3000.0f;
        s.filter.resonance = 0.3f;
        // LFO1 slowly sweeps filter
        s.lfo1.rateHz = 0.1f;
        s.lfo1.shape = 5; // Smooth Random
        s.lfo1.depth = 0.7f;
        s.lfo1.sync = 0;
        s.modMatrix.slots[0].source = 1; // LFO 1
        s.modMatrix.slots[0].dest = 4;   // All Voice Filter Cutoff
        s.modMatrix.slots[0].amount = 0.5f;
        s.ampEnv.attackMs = 800.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.9f;
        s.ampEnv.releaseMs = 2500.0f;
        s.delayEnabled = 1;
        s.delay.mix = 0.25f;
        s.delay.feedback = 0.4f;
        s.delay.timeMs = 750.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.85f;
        s.reverb.mix = 0.4f;
        s.reverb.damping = 0.5f;
        presets.push_back(std::move(p));
    }

    // "Spectral Ghost" - Eerie spectral texture
    {
        PresetDef p;
        p.name = "Spectral Ghost";
        p.category = "Textures";
        auto& s = p.state;
        s.oscA.type = 8; // Spectral Freeze
        s.oscA.spectralPitch = 5.0f; // Shifted up
        s.oscA.spectralTilt = -4.0f; // Darker
        s.oscA.level = 0.7f;
        s.oscB.type = 4; // Additive
        s.oscB.additivePartials = 48;
        s.oscB.additiveTilt = -6.0f; // Very dark
        s.oscB.additiveInharm = 0.3f;
        s.oscB.level = 0.4f;
        s.mixer.mode = 1; // Spectral Morph
        s.mixer.position = 0.5f;
        s.filter.type = 1; // SVF HP
        s.filter.cutoffHz = 300.0f;
        s.filter.resonance = 0.2f;
        s.ampEnv.attackMs = 1500.0f;
        s.ampEnv.decayMs = 800.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 3000.0f;
        s.delayEnabled = 1;
        s.delay.mix = 0.3f;
        s.delay.feedback = 0.5f;
        s.delay.timeMs = 600.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.95f;
        s.reverb.mix = 0.5f;
        s.reverb.damping = 0.2f;
        s.reverb.modRateHz = 0.2f;
        s.reverb.modDepth = 0.15f;
        presets.push_back(std::move(p));
    }

    // "Granular Fog" - Dense granular fog
    {
        PresetDef p;
        p.name = "Granular Fog";
        p.category = "Textures";
        auto& s = p.state;
        s.oscA.type = 6; // Particle
        s.oscA.particleScatter = 5.0f;
        s.oscA.particleDensity = 48.0f;
        s.oscA.particleLifetime = 1500.0f; // Very long grains
        s.oscA.particleSpawnMode = 1; // Random
        s.oscA.particleEnvType = 2; // Blackman
        s.oscA.particleDrift = 0.5f;
        s.oscA.level = 0.7f;
        s.oscB.type = 6; // Particle (different settings)
        s.oscB.particleScatter = 10.0f; // Wider scatter
        s.oscB.particleDensity = 16.0f;
        s.oscB.particleLifetime = 500.0f;
        s.oscB.particleSpawnMode = 2; // Clustered
        s.oscB.particleEnvType = 0; // Hann
        s.oscB.particleDrift = 0.7f;
        s.oscB.level = 0.5f;
        s.mixer.position = 0.5f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 5000.0f;
        s.filter.resonance = 0.15f;
        s.ampEnv.attackMs = 1200.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.9f;
        s.ampEnv.releaseMs = 3500.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.95f;
        s.reverb.mix = 0.55f;
        s.reverb.damping = 0.3f;
        s.reverb.diffusion = 0.9f;
        s.global.width = 2.0f;
        s.global.spread = 0.6f;
        presets.push_back(std::move(p));
    }

    // "Metal Resonance" - Metallic resonant texture
    {
        PresetDef p;
        p.name = "Metal Resonance";
        p.category = "Textures";
        auto& s = p.state;
        s.oscA.type = 4; // Additive
        s.oscA.additivePartials = 128;
        s.oscA.additiveInharm = 0.6f; // High inharmonicity
        s.oscA.additiveTilt = -2.0f;
        s.oscA.level = 0.6f;
        s.oscB.type = 9; // Noise
        s.oscB.noiseColor = 0; // White
        s.oscB.level = 0.1f; // Subtle excitation
        s.mixer.position = 0.15f;
        s.filter.type = 6; // Comb filter
        s.filter.cutoffHz = 800.0f;
        s.filter.resonance = 0.7f;
        s.filter.combDamping = 0.3f;
        s.ampEnv.attackMs = 50.0f;
        s.ampEnv.decayMs = 3000.0f;
        s.ampEnv.sustain = 0.3f;
        s.ampEnv.releaseMs = 2000.0f;
        s.delayEnabled = 1;
        s.delay.mix = 0.2f;
        s.delay.feedback = 0.35f;
        s.delay.timeMs = 300.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.6f;
        s.reverb.mix = 0.3f;
        presets.push_back(std::move(p));
    }

    // ==================== RHYTHMIC Category (5 presets) ====================

    // "Trance Gate Pad" - Warm pad with trance gate rhythm
    {
        PresetDef p;
        p.name = "Trance Gate Pad";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.level = 0.7f;
        s.oscB.type = 0;
        s.oscB.waveform = 1; // Saw
        s.oscB.fineCents = 7.0f;
        s.oscB.level = 0.6f;
        s.mixer.position = 0.45f;
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 3500.0f;
        s.filter.resonance = 0.25f;
        s.ampEnv.attackMs = 100.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.8f;
        s.ampEnv.releaseMs = 600.0f;
        // Trance gate: 16-step pattern
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 16;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.9f;
        s.tranceGate.attackMs = 5.0f;
        s.tranceGate.releaseMs = 30.0f;
        // Classic trance gate pattern
        float tgSteps[32]{};
        tgSteps[0] = 1.0f; tgSteps[1] = 1.0f; tgSteps[2] = 0.0f; tgSteps[3] = 1.0f;
        tgSteps[4] = 1.0f; tgSteps[5] = 0.0f; tgSteps[6] = 1.0f; tgSteps[7] = 0.0f;
        tgSteps[8] = 1.0f; tgSteps[9] = 1.0f; tgSteps[10] = 0.0f; tgSteps[11] = 1.0f;
        tgSteps[12] = 1.0f; tgSteps[13] = 0.0f; tgSteps[14] = 0.0f; tgSteps[15] = 1.0f;
        for (int i = 0; i < 32; ++i) s.tranceGate.stepLevels[i] = tgSteps[i];
        s.reverbEnabled = 1;
        s.reverb.size = 0.5f;
        s.reverb.mix = 0.25f;
        s.global.width = 1.4f;
        presets.push_back(std::move(p));
    }

    // "Pumping Lead" - Lead with sidechain-style pumping gate
    {
        PresetDef p;
        p.name = "Pumping Lead";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.level = 0.85f;
        s.oscB.type = 0;
        s.oscB.waveform = 1; // Saw
        s.oscB.fineCents = 10.0f;
        s.oscB.level = 0.65f;
        s.mixer.position = 0.5f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 7000.0f;
        s.filter.resonance = 0.15f;
        s.ampEnv.attackMs = 3.0f;
        s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.75f;
        s.ampEnv.releaseMs = 200.0f;
        // Trance gate: 4-step pump pattern (sidechain simulation)
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 4;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 15.0f;
        s.tranceGate.releaseMs = 80.0f;
        s.tranceGate.stepLevels[0] = 0.0f; // Duck
        s.tranceGate.stepLevels[1] = 0.7f;
        s.tranceGate.stepLevels[2] = 1.0f;
        s.tranceGate.stepLevels[3] = 0.9f;
        presets.push_back(std::move(p));
    }

    // "Stutter Bass" - Bass with fast gate stutter
    {
        PresetDef p;
        p.name = "Stutter Bass";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.tuneSemitones = -12.0f;
        s.oscA.level = 0.85f;
        s.oscB.type = 0;
        s.oscB.waveform = 2; // Square
        s.oscB.tuneSemitones = -12.0f;
        s.oscB.level = 0.5f;
        s.mixer.position = 0.4f;
        s.filter.type = 4; // Ladder LP
        s.filter.cutoffHz = 3000.0f;
        s.filter.resonance = 0.3f;
        s.filter.ladderSlope = 4;
        s.ampEnv.attackMs = 2.0f;
        s.ampEnv.decayMs = 150.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 80.0f;
        // Trance gate: 8-step stutter pattern
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 1.0f;
        s.tranceGate.attackMs = 1.0f;
        s.tranceGate.releaseMs = 5.0f;
        s.tranceGate.stepLevels[0] = 1.0f; s.tranceGate.stepLevels[1] = 0.0f;
        s.tranceGate.stepLevels[2] = 1.0f; s.tranceGate.stepLevels[3] = 0.0f;
        s.tranceGate.stepLevels[4] = 1.0f; s.tranceGate.stepLevels[5] = 1.0f;
        s.tranceGate.stepLevels[6] = 0.0f; s.tranceGate.stepLevels[7] = 1.0f;
        s.global.voiceMode = 1; // Mono
        presets.push_back(std::move(p));
    }

    // "Gate Strings" - String-like pad with rhythmic gate and delay
    {
        PresetDef p;
        p.name = "Gate Strings";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.level = 0.65f;
        s.oscB.type = 0;
        s.oscB.waveform = 4; // Triangle
        s.oscB.fineCents = 6.0f;
        s.oscB.level = 0.5f;
        s.mixer.position = 0.45f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 4000.0f;
        s.filter.resonance = 0.15f;
        s.ampEnv.attackMs = 200.0f;
        s.ampEnv.decayMs = 600.0f;
        s.ampEnv.sustain = 0.75f;
        s.ampEnv.releaseMs = 800.0f;
        // Trance gate: 8-step alternating levels
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_8;
        s.tranceGate.depth = 0.8f;
        s.tranceGate.attackMs = 10.0f;
        s.tranceGate.releaseMs = 50.0f;
        s.tranceGate.stepLevels[0] = 1.0f; s.tranceGate.stepLevels[1] = 0.5f;
        s.tranceGate.stepLevels[2] = 0.8f; s.tranceGate.stepLevels[3] = 0.3f;
        s.tranceGate.stepLevels[4] = 1.0f; s.tranceGate.stepLevels[5] = 0.4f;
        s.tranceGate.stepLevels[6] = 0.9f; s.tranceGate.stepLevels[7] = 0.2f;
        s.delayEnabled = 1;
        s.delay.mix = 0.25f;
        s.delay.feedback = 0.3f;
        s.delay.sync = 1;
        s.delay.noteValue = kNote1_8;
        s.reverbEnabled = 1;
        s.reverb.size = 0.5f;
        s.reverb.mix = 0.2f;
        s.global.width = 1.3f;
        s.global.spread = 0.2f;
        presets.push_back(std::move(p));
    }

    // "Choppy Texture" - Textural sound with euclidean trance gate
    {
        PresetDef p;
        p.name = "Choppy Texture";
        p.category = "Rhythmic";
        auto& s = p.state;
        s.oscA.type = 6; // Particle
        s.oscA.particleScatter = 4.0f;
        s.oscA.particleDensity = 24.0f;
        s.oscA.particleLifetime = 300.0f;
        s.oscA.particleSpawnMode = 1; // Random
        s.oscA.level = 0.7f;
        s.oscB.type = 9; // Noise
        s.oscB.noiseColor = 1; // Pink
        s.oscB.level = 0.15f;
        s.mixer.position = 0.2f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 5000.0f;
        s.filter.resonance = 0.2f;
        s.ampEnv.attackMs = 30.0f;
        s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 500.0f;
        // Trance gate with euclidean pattern E(5,8)
        s.tranceGate.enabled = 1;
        s.tranceGate.numSteps = 8;
        s.tranceGate.tempoSync = 1;
        s.tranceGate.noteValue = kNote1_16;
        s.tranceGate.depth = 0.85f;
        s.tranceGate.attackMs = 3.0f;
        s.tranceGate.releaseMs = 20.0f;
        s.tranceGate.euclideanEnabled = 1;
        s.tranceGate.euclideanHits = 5;
        s.reverbEnabled = 1;
        s.reverb.size = 0.7f;
        s.reverb.mix = 0.4f;
        s.reverb.diffusion = 0.85f;
        presets.push_back(std::move(p));
    }

    // ==================== EXPERIMENTAL Category (5 presets) ====================

    // "Chaos Machine" - Double chaos oscillators with chaos distortion
    {
        PresetDef p;
        p.name = "Chaos Machine";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 5; // Chaos
        s.oscA.chaosAttractor = 0; // Lorenz
        s.oscA.chaosAmount = 0.8f;
        s.oscA.chaosCoupling = 0.6f;
        s.oscA.chaosOutput = 0; // X
        s.oscA.level = 0.7f;
        s.oscB.type = 5; // Chaos
        s.oscB.chaosAttractor = 3; // Henon
        s.oscB.chaosAmount = 0.5f;
        s.oscB.chaosCoupling = 0.3f;
        s.oscB.chaosOutput = 2; // Z
        s.oscB.level = 0.5f;
        s.mixer.position = 0.5f;
        s.filter.type = 2; // SVF BP
        s.filter.cutoffHz = 2000.0f;
        s.filter.resonance = 0.4f;
        s.distortion.type = 1; // Chaos Waveshaper
        s.distortion.drive = 0.5f;
        s.distortion.chaosModel = 0; // Lorenz
        s.distortion.chaosSpeed = 0.7f;
        s.distortion.chaosCoupling = 0.4f;
        s.ampEnv.attackMs = 10.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.6f;
        s.ampEnv.releaseMs = 800.0f;
        s.delayEnabled = 1;
        s.delay.mix = 0.3f;
        s.delay.feedback = 0.45f;
        s.delay.timeMs = 375.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.6f;
        s.reverb.mix = 0.3f;
        presets.push_back(std::move(p));
    }

    // "Formant Morph" - Morphing vowel sounds with formant filter
    {
        PresetDef p;
        p.name = "Formant Morph";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 7; // Formant
        s.oscA.formantVowel = 0; // A
        s.oscA.formantMorph = 0.0f;
        s.oscA.level = 0.8f;
        s.oscB.type = 7; // Formant
        s.oscB.formantVowel = 3; // O
        s.oscB.formantMorph = 2.0f; // Mid morph
        s.oscB.level = 0.6f;
        s.mixer.position = 0.5f;
        s.filter.type = 5; // Formant filter
        s.filter.formantMorph = 0.0f;
        s.filter.formantGender = 0.0f;
        s.filter.cutoffHz = 5000.0f;
        // LFO1 morphs formant osc
        s.lfo1.rateHz = 0.2f;
        s.lfo1.shape = 1; // Triangle
        s.lfo1.depth = 1.0f;
        s.lfo1.sync = 0;
        s.modMatrix.slots[0].source = 1; // LFO 1
        s.modMatrix.slots[0].dest = 5;   // All Voice Morph Pos
        s.modMatrix.slots[0].amount = 0.6f;
        // LFO2 modulates filter
        s.lfo2.rateHz = 0.08f;
        s.lfo2.shape = 0; // Sine
        s.lfo2.depth = 0.5f;
        s.lfo2.sync = 0;
        s.modMatrix.slots[1].source = 2; // LFO 2
        s.modMatrix.slots[1].dest = 4;   // All Voice Filter Cutoff
        s.modMatrix.slots[1].amount = 0.3f;
        s.ampEnv.attackMs = 50.0f;
        s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 500.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.5f;
        s.reverb.mix = 0.25f;
        presets.push_back(std::move(p));
    }

    // "Bit Crusher" - Digital destruction with spectral distortion
    {
        PresetDef p;
        p.name = "Bit Crusher";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 0; // PolyBLEP
        s.oscA.waveform = 1; // Saw
        s.oscA.level = 0.8f;
        s.oscB.type = 0;
        s.oscB.waveform = 2; // Square
        s.oscB.tuneSemitones = 7.0f; // Fifth
        s.oscB.level = 0.5f;
        s.mixer.position = 0.4f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 6000.0f;
        s.filter.resonance = 0.2f;
        s.distortion.type = 2; // Spectral Distortion
        s.distortion.drive = 0.6f;
        s.distortion.spectralMode = 3; // SpectralBitcrush
        s.distortion.spectralCurve = 8; // BitReduce
        s.distortion.spectralBits = 0.3f; // ~5 bits
        s.ampEnv.attackMs = 3.0f;
        s.ampEnv.decayMs = 300.0f;
        s.ampEnv.sustain = 0.65f;
        s.ampEnv.releaseMs = 200.0f;
        s.delayEnabled = 1;
        s.delay.mix = 0.2f;
        s.delay.feedback = 0.3f;
        s.delay.timeMs = 250.0f;
        presets.push_back(std::move(p));
    }

    // "Wavefold Madness" - Extreme wavefolding with sync oscillator
    {
        PresetDef p;
        p.name = "Wavefold Madness";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 3; // Sync
        s.oscA.syncRatio = 5.0f; // High ratio for harmonics
        s.oscA.syncWaveform = 1; // Saw slave
        s.oscA.syncMode = 0; // Hard
        s.oscA.syncAmount = 0.8f;
        s.oscA.level = 0.8f;
        s.oscB.type = 0; // PolyBLEP
        s.oscB.waveform = 4; // Triangle
        s.oscB.level = 0.4f;
        s.mixer.position = 0.35f;
        s.filter.type = 2; // SVF BP
        s.filter.cutoffHz = 2500.0f;
        s.filter.resonance = 0.35f;
        s.distortion.type = 4; // Wavefolder
        s.distortion.drive = 0.7f;
        s.distortion.foldType = 2; // Lockhart
        s.ampEnv.attackMs = 5.0f;
        s.ampEnv.decayMs = 400.0f;
        s.ampEnv.sustain = 0.6f;
        s.ampEnv.releaseMs = 300.0f;
        // LFO modulating distortion drive
        s.lfo1.rateHz = 0.5f;
        s.lfo1.shape = 1; // Triangle
        s.lfo1.depth = 0.7f;
        s.lfo1.sync = 0;
        s.modMatrix.slots[0].source = 1; // LFO 1
        s.modMatrix.slots[0].dest = 7;   // All Voice Spectral Tilt
        s.modMatrix.slots[0].amount = 0.4f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.4f;
        s.reverb.mix = 0.2f;
        presets.push_back(std::move(p));
    }

    // "Rungler Noise" - Experimental noise with rungler modulation
    {
        PresetDef p;
        p.name = "Rungler Noise";
        p.category = "Experimental";
        auto& s = p.state;
        s.oscA.type = 9; // Noise
        s.oscA.noiseColor = 3; // Blue
        s.oscA.level = 0.5f;
        s.oscB.type = 5; // Chaos
        s.oscB.chaosAttractor = 2; // Chua
        s.oscB.chaosAmount = 0.6f;
        s.oscB.chaosCoupling = 0.5f;
        s.oscB.level = 0.6f;
        s.mixer.position = 0.5f;
        s.filter.type = 0; // SVF LP
        s.filter.cutoffHz = 3000.0f;
        s.filter.resonance = 0.4f;
        // Rungler active
        s.rungler.depth = 0.6f;
        s.rungler.osc1FreqHz = 3.0f;
        s.rungler.osc2FreqHz = 5.0f;
        s.rungler.bits = 6;
        s.rungler.filter = 0.3f;
        // Mod matrix: Rungler -> filter cutoff
        s.modMatrix.slots[0].source = 10; // Rungler
        s.modMatrix.slots[0].dest = 4;    // All Voice Filter Cutoff
        s.modMatrix.slots[0].amount = 0.5f;
        s.ampEnv.attackMs = 20.0f;
        s.ampEnv.decayMs = 500.0f;
        s.ampEnv.sustain = 0.7f;
        s.ampEnv.releaseMs = 600.0f;
        s.delayEnabled = 1;
        s.delay.mix = 0.3f;
        s.delay.feedback = 0.5f;
        s.delay.timeMs = 333.0f;
        s.reverbEnabled = 1;
        s.reverb.size = 0.7f;
        s.reverb.mix = 0.35f;
        presets.push_back(std::move(p));
    }

    return presets;
}

// ==============================================================================
// Main
// ==============================================================================

int main(int argc, char* argv[]) {
    std::filesystem::path outputDir = "plugins/ruinae/resources/presets";

    if (argc > 1) {
        outputDir = argv[1];
    }

    std::filesystem::create_directories(outputDir);

    auto presets = createAllPresets();
    int successCount = 0;

    std::cout << "Generating " << presets.size()
              << " Ruinae factory presets..." << std::endl;

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
