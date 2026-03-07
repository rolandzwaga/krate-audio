// ==============================================================================
// Factory Preset Generator for Innexus
// ==============================================================================
// Generates .vstpreset files matching the Processor::getState() binary format.
// Run this tool once during development to create factory presets.
//
// Categories are organized by input signal type, since Innexus is a harmonic
// analysis/resynthesis instrument whose behavior depends on what you feed it.
//
// Reference: plugins/innexus/src/processor/processor_state.cpp getState()
// Reference: tools/ruinae_preset_generator.cpp (established pattern)
// ==============================================================================

#include "innexus_preset_format.h"

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <algorithm>

using namespace InnexusFormat;

// ==============================================================================
// Constants (preset-generator-specific)
// ==============================================================================

// kProcessorUID(0xE1F2A3B4, 0x5C6D7E8F, 0x9A0B1C2D, 0x3E4F5A6B)
const char kClassIdAscii[33] = "E1F2A3B45C6D7E8F9A0B1C2D3E4F5A6B";

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
    InnexusPresetState state;
};

// ==============================================================================
// Normalized value helpers
// ==============================================================================

// Evolution mode: 0=Cycle, 1=PingPong, 2=RandomWalk
// Stored as normalized: 0.0, 0.5, 1.0
static constexpr float kEvoCycle = 0.0f;
static constexpr float kEvoPingPong = 0.5f;
static constexpr float kEvoRandomWalk = 1.0f;

// Modulator waveform: 0=Sine, 1=Tri, 2=Sq, 3=Saw, 4=RSH
// 5 values mapped to 0.0-1.0
static constexpr float kWaveSine = 0.0f;
static constexpr float kWaveTri = 0.25f;
static constexpr float kWaveSquare = 0.5f;
static constexpr float kWaveSaw = 0.75f;
static constexpr float kWaveRSH = 1.0f;

// Modulator target: 0=Amplitude, 1=Frequency, 2=Pan
static constexpr float kTargetAmp = 0.0f;
static constexpr float kTargetFreq = 0.5f;
static constexpr float kTargetPan = 1.0f;

// Harmonic filter type: 0=AllPass, 1=OddOnly, 2=EvenOnly, 3=LowPartials, 4=HighPartials
static constexpr int32_t kFilterAllPass = 0;
static constexpr int32_t kFilterOddOnly = 1;
static constexpr int32_t kFilterEvenOnly = 2;
static constexpr int32_t kFilterLowPartials = 3;
static constexpr int32_t kFilterHighPartials = 4;

// Normalize a partial range value (1-48) to 0.0-1.0
float normalizeRange(int partial) {
    return static_cast<float>(std::clamp(partial, 1, 48) - 1) / 47.0f;
}

// Normalize an evolution speed (0.01-10.0 Hz log) to 0.0-1.0
// evolutionSpeed parameter is normalized 0-1
float normalizeEvoSpeed(float hz) {
    // log scale: 0.01 to 10.0
    // norm = log(hz/0.01) / log(10.0/0.01) = log(hz/0.01) / log(1000)
    if (hz <= 0.01f) return 0.0f;
    if (hz >= 10.0f) return 1.0f;
    return std::log(hz / 0.01f) / std::log(1000.0f);
}

// Normalize a mod rate (0.01-20.0 Hz log) to 0.0-1.0
float normalizeModRate(float hz) {
    if (hz <= 0.01f) return 0.0f;
    if (hz >= 20.0f) return 1.0f;
    return std::log(hz / 0.01f) / std::log(2000.0f);
}

// ==============================================================================
// Preset Definitions by Category
// ==============================================================================

std::vector<PresetDef> createAllPresets() {
    std::vector<PresetDef> presets;

    // ========================================================================
    // Voice
    // ========================================================================
    {
        PresetDef p;
        p.name = "Choir Sustain";
        p.category = "Voice";
        p.state.releaseTimeMs = 300.0f;
        p.state.harmonicLevelPlain = 1.4f;
        p.state.residualLevelPlain = 0.6f;
        p.state.brightnessPlain = 0.2f;
        p.state.responsiveness = 0.4f;
        p.state.warmth = 0.6f;
        p.state.coupling = 0.3f;
        p.state.stereoSpread = 0.4f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Vocal Freeze";
        p.category = "Voice";
        p.state.releaseTimeMs = 500.0f;
        p.state.harmonicLevelPlain = 1.2f;
        p.state.residualLevelPlain = 0.3f;
        p.state.freeze = 1;
        p.state.responsiveness = 0.3f;
        p.state.warmth = 0.4f;
        p.state.stereoSpread = 0.5f;
        p.state.evolutionEnable = 1.0f;
        p.state.evolutionSpeed = normalizeEvoSpeed(0.08f);
        p.state.evolutionDepth = 0.3f;
        p.state.evolutionMode = kEvoPingPong;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Formant Shimmer";
        p.category = "Voice";
        p.state.releaseTimeMs = 200.0f;
        p.state.harmonicLevelPlain = 1.6f;
        p.state.residualLevelPlain = 0.4f;
        p.state.brightnessPlain = 0.5f;
        p.state.harmonicFilterType = kFilterOddOnly;
        p.state.responsiveness = 0.6f;
        p.state.warmth = 0.3f;
        p.state.mod1Enable = 1.0f;
        p.state.mod1Waveform = kWaveSine;
        p.state.mod1Rate = normalizeModRate(2.0f);
        p.state.mod1Depth = 0.25f;
        p.state.mod1RangeStart = normalizeRange(1);
        p.state.mod1RangeEnd = normalizeRange(16);
        p.state.mod1Target = kTargetAmp;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Speech Texture";
        p.category = "Voice";
        p.state.releaseTimeMs = 80.0f;
        p.state.harmonicLevelPlain = 0.8f;
        p.state.residualLevelPlain = 1.4f;
        p.state.transientEmphasisPlain = 0.8f;
        p.state.responsiveness = 0.8f;
        p.state.entropy = 0.15f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Ethereal Soprano";
        p.category = "Voice";
        p.state.releaseTimeMs = 400.0f;
        p.state.harmonicLevelPlain = 1.8f;
        p.state.residualLevelPlain = 0.2f;
        p.state.brightnessPlain = 0.6f;
        p.state.responsiveness = 0.3f;
        p.state.warmth = 0.5f;
        p.state.stereoSpread = 0.6f;
        p.state.detuneSpread = 0.15f;
        p.state.mod1Enable = 1.0f;
        p.state.mod1Waveform = kWaveTri;
        p.state.mod1Rate = normalizeModRate(0.3f);
        p.state.mod1Depth = 0.15f;
        p.state.mod1Target = kTargetPan;
        presets.push_back(p);
    }

    // ========================================================================
    // Strings
    // ========================================================================
    {
        PresetDef p;
        p.name = "Bowed Sustain";
        p.category = "Strings";
        p.state.releaseTimeMs = 250.0f;
        p.state.inharmonicityAmount = 0.8f;
        p.state.harmonicLevelPlain = 1.4f;
        p.state.residualLevelPlain = 0.8f;
        p.state.brightnessPlain = 0.1f;
        p.state.responsiveness = 0.5f;
        p.state.warmth = 0.7f;
        p.state.coupling = 0.4f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Plucked Decay";
        p.category = "Strings";
        p.state.releaseTimeMs = 150.0f;
        p.state.inharmonicityAmount = 0.9f;
        p.state.harmonicLevelPlain = 1.2f;
        p.state.residualLevelPlain = 1.0f;
        p.state.transientEmphasisPlain = 0.6f;
        p.state.responsiveness = 0.7f;
        p.state.coupling = 0.5f;
        p.state.stability = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "String Ensemble";
        p.category = "Strings";
        p.state.releaseTimeMs = 350.0f;
        p.state.harmonicLevelPlain = 1.6f;
        p.state.residualLevelPlain = 0.5f;
        p.state.warmth = 0.6f;
        p.state.coupling = 0.3f;
        p.state.stereoSpread = 0.7f;
        p.state.detuneSpread = 0.2f;
        p.state.evolutionEnable = 1.0f;
        p.state.evolutionSpeed = normalizeEvoSpeed(0.05f);
        p.state.evolutionDepth = 0.2f;
        p.state.evolutionMode = kEvoCycle;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Guitar Harmonics";
        p.category = "Strings";
        p.state.releaseTimeMs = 120.0f;
        p.state.inharmonicityAmount = 0.7f;
        p.state.harmonicLevelPlain = 1.8f;
        p.state.residualLevelPlain = 0.3f;
        p.state.brightnessPlain = 0.4f;
        p.state.harmonicFilterType = kFilterHighPartials;
        p.state.responsiveness = 0.6f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Cello Warmth";
        p.category = "Strings";
        p.state.releaseTimeMs = 300.0f;
        p.state.harmonicLevelPlain = 1.3f;
        p.state.residualLevelPlain = 0.9f;
        p.state.brightnessPlain = -0.3f;
        p.state.responsiveness = 0.4f;
        p.state.warmth = 0.8f;
        p.state.coupling = 0.5f;
        p.state.stability = 0.4f;
        presets.push_back(p);
    }

    // ========================================================================
    // Keys
    // ========================================================================
    {
        PresetDef p;
        p.name = "Piano Attack";
        p.category = "Keys";
        p.state.releaseTimeMs = 80.0f;
        p.state.inharmonicityAmount = 0.85f;
        p.state.harmonicLevelPlain = 1.2f;
        p.state.residualLevelPlain = 1.2f;
        p.state.transientEmphasisPlain = 1.0f;
        p.state.responsiveness = 0.8f;
        p.state.coupling = 0.6f;
        p.state.stability = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Electric Piano";
        p.category = "Keys";
        p.state.releaseTimeMs = 150.0f;
        p.state.harmonicLevelPlain = 1.5f;
        p.state.residualLevelPlain = 0.6f;
        p.state.brightnessPlain = 0.3f;
        p.state.responsiveness = 0.6f;
        p.state.warmth = 0.5f;
        p.state.stereoSpread = 0.3f;
        p.state.mod1Enable = 1.0f;
        p.state.mod1Waveform = kWaveSine;
        p.state.mod1Rate = normalizeModRate(4.5f);
        p.state.mod1Depth = 0.1f;
        p.state.mod1Target = kTargetAmp;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Organ Sustain";
        p.category = "Keys";
        p.state.releaseTimeMs = 60.0f;
        p.state.harmonicLevelPlain = 1.8f;
        p.state.residualLevelPlain = 0.2f;
        p.state.harmonicFilterType = kFilterOddOnly;
        p.state.responsiveness = 0.7f;
        p.state.warmth = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Mallet Bright";
        p.category = "Keys";
        p.state.releaseTimeMs = 100.0f;
        p.state.inharmonicityAmount = 0.6f;
        p.state.harmonicLevelPlain = 1.4f;
        p.state.residualLevelPlain = 0.8f;
        p.state.brightnessPlain = 0.6f;
        p.state.transientEmphasisPlain = 0.7f;
        p.state.responsiveness = 0.7f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Music Box";
        p.category = "Keys";
        p.state.releaseTimeMs = 200.0f;
        p.state.harmonicLevelPlain = 1.6f;
        p.state.residualLevelPlain = 0.4f;
        p.state.brightnessPlain = 0.8f;
        p.state.harmonicFilterType = kFilterHighPartials;
        p.state.responsiveness = 0.5f;
        p.state.stereoSpread = 0.4f;
        p.state.detuneSpread = 0.08f;
        presets.push_back(p);
    }

    // ========================================================================
    // Brass & Winds
    // ========================================================================
    {
        PresetDef p;
        p.name = "Brass Section";
        p.category = "Brass and Winds";
        p.state.releaseTimeMs = 120.0f;
        p.state.harmonicLevelPlain = 1.6f;
        p.state.residualLevelPlain = 0.6f;
        p.state.brightnessPlain = 0.4f;
        p.state.responsiveness = 0.7f;
        p.state.warmth = 0.5f;
        p.state.coupling = 0.4f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Flute Breath";
        p.category = "Brass and Winds";
        p.state.releaseTimeMs = 100.0f;
        p.state.harmonicLevelPlain = 1.0f;
        p.state.residualLevelPlain = 1.4f;
        p.state.brightnessPlain = 0.3f;
        p.state.responsiveness = 0.6f;
        p.state.warmth = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Reed Buzz";
        p.category = "Brass and Winds";
        p.state.releaseTimeMs = 80.0f;
        p.state.harmonicLevelPlain = 1.4f;
        p.state.residualLevelPlain = 1.0f;
        p.state.brightnessPlain = 0.2f;
        p.state.harmonicFilterType = kFilterOddOnly;
        p.state.responsiveness = 0.7f;
        p.state.warmth = 0.4f;
        p.state.entropy = 0.1f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Horn Swell";
        p.category = "Brass and Winds";
        p.state.releaseTimeMs = 200.0f;
        p.state.harmonicLevelPlain = 1.5f;
        p.state.residualLevelPlain = 0.5f;
        p.state.responsiveness = 0.4f;
        p.state.warmth = 0.7f;
        p.state.coupling = 0.5f;
        p.state.stereoSpread = 0.3f;
        p.state.evolutionEnable = 1.0f;
        p.state.evolutionSpeed = normalizeEvoSpeed(0.1f);
        p.state.evolutionDepth = 0.15f;
        p.state.evolutionMode = kEvoPingPong;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Woodwind Choir";
        p.category = "Brass and Winds";
        p.state.releaseTimeMs = 250.0f;
        p.state.harmonicLevelPlain = 1.3f;
        p.state.residualLevelPlain = 0.8f;
        p.state.brightnessPlain = 0.1f;
        p.state.responsiveness = 0.5f;
        p.state.warmth = 0.5f;
        p.state.stereoSpread = 0.5f;
        p.state.detuneSpread = 0.12f;
        presets.push_back(p);
    }

    // ========================================================================
    // Drums & Perc
    // ========================================================================
    {
        PresetDef p;
        p.name = "Tonal Percussion";
        p.category = "Drums and Perc";
        p.state.releaseTimeMs = 60.0f;
        p.state.harmonicLevelPlain = 1.0f;
        p.state.residualLevelPlain = 1.6f;
        p.state.transientEmphasisPlain = 1.2f;
        p.state.responsiveness = 0.9f;
        p.state.stability = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Metallic Ring";
        p.category = "Drums and Perc";
        p.state.releaseTimeMs = 300.0f;
        p.state.inharmonicityAmount = 0.5f;
        p.state.harmonicLevelPlain = 1.8f;
        p.state.residualLevelPlain = 0.4f;
        p.state.brightnessPlain = 0.7f;
        p.state.responsiveness = 0.6f;
        p.state.coupling = 0.6f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Noise Burst";
        p.category = "Drums and Perc";
        p.state.releaseTimeMs = 40.0f;
        p.state.harmonicLevelPlain = 0.3f;
        p.state.residualLevelPlain = 2.0f;
        p.state.transientEmphasisPlain = 1.5f;
        p.state.responsiveness = 1.0f;
        p.state.entropy = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Tuned Drum";
        p.category = "Drums and Perc";
        p.state.releaseTimeMs = 100.0f;
        p.state.inharmonicityAmount = 0.3f;
        p.state.harmonicLevelPlain = 1.4f;
        p.state.residualLevelPlain = 1.2f;
        p.state.transientEmphasisPlain = 0.8f;
        p.state.responsiveness = 0.8f;
        p.state.warmth = 0.4f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Cymbal Wash";
        p.category = "Drums and Perc";
        p.state.releaseTimeMs = 500.0f;
        p.state.inharmonicityAmount = 0.2f;
        p.state.harmonicLevelPlain = 0.6f;
        p.state.residualLevelPlain = 1.8f;
        p.state.brightnessPlain = 0.5f;
        p.state.responsiveness = 0.3f;
        p.state.entropy = 0.2f;
        p.state.stereoSpread = 0.5f;
        presets.push_back(p);
    }

    // ========================================================================
    // Pads & Drones
    // ========================================================================
    {
        PresetDef p;
        p.name = "Frozen Pad";
        p.category = "Pads and Drones";
        p.state.releaseTimeMs = 1000.0f;
        p.state.harmonicLevelPlain = 1.4f;
        p.state.residualLevelPlain = 0.3f;
        p.state.freeze = 1;
        p.state.responsiveness = 0.2f;
        p.state.warmth = 0.5f;
        p.state.stereoSpread = 0.7f;
        p.state.detuneSpread = 0.15f;
        p.state.evolutionEnable = 1.0f;
        p.state.evolutionSpeed = normalizeEvoSpeed(0.03f);
        p.state.evolutionDepth = 0.4f;
        p.state.evolutionMode = kEvoCycle;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Ambient Wash";
        p.category = "Pads and Drones";
        p.state.releaseTimeMs = 2000.0f;
        p.state.harmonicLevelPlain = 1.2f;
        p.state.residualLevelPlain = 0.8f;
        p.state.brightnessPlain = -0.2f;
        p.state.responsiveness = 0.2f;
        p.state.warmth = 0.7f;
        p.state.stereoSpread = 0.8f;
        p.state.mod1Enable = 1.0f;
        p.state.mod1Waveform = kWaveSine;
        p.state.mod1Rate = normalizeModRate(0.1f);
        p.state.mod1Depth = 0.2f;
        p.state.mod1Target = kTargetPan;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Drone Engine";
        p.category = "Pads and Drones";
        p.state.releaseTimeMs = 3000.0f;
        p.state.harmonicLevelPlain = 1.6f;
        p.state.residualLevelPlain = 0.6f;
        p.state.freeze = 1;
        p.state.harmonicFilterType = kFilterLowPartials;
        p.state.responsiveness = 0.1f;
        p.state.warmth = 0.8f;
        p.state.coupling = 0.6f;
        p.state.stability = 0.5f;
        p.state.stereoSpread = 0.5f;
        p.state.feedbackAmount = 0.4f;
        p.state.feedbackDecay = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Evolving Texture";
        p.category = "Pads and Drones";
        p.state.releaseTimeMs = 1500.0f;
        p.state.harmonicLevelPlain = 1.3f;
        p.state.residualLevelPlain = 0.7f;
        p.state.responsiveness = 0.3f;
        p.state.warmth = 0.4f;
        p.state.entropy = 0.15f;
        p.state.stereoSpread = 0.6f;
        p.state.detuneSpread = 0.2f;
        p.state.evolutionEnable = 1.0f;
        p.state.evolutionSpeed = normalizeEvoSpeed(0.15f);
        p.state.evolutionDepth = 0.6f;
        p.state.evolutionMode = kEvoRandomWalk;
        p.state.mod1Enable = 1.0f;
        p.state.mod1Waveform = kWaveTri;
        p.state.mod1Rate = normalizeModRate(0.2f);
        p.state.mod1Depth = 0.15f;
        p.state.mod1RangeStart = normalizeRange(8);
        p.state.mod1RangeEnd = normalizeRange(32);
        p.state.mod1Target = kTargetAmp;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Crystalline Bed";
        p.category = "Pads and Drones";
        p.state.releaseTimeMs = 2500.0f;
        p.state.harmonicLevelPlain = 1.8f;
        p.state.residualLevelPlain = 0.2f;
        p.state.brightnessPlain = 0.7f;
        p.state.harmonicFilterType = kFilterHighPartials;
        p.state.responsiveness = 0.2f;
        p.state.stereoSpread = 0.9f;
        p.state.detuneSpread = 0.25f;
        p.state.mod2Enable = 1.0f;
        p.state.mod2Waveform = kWaveSine;
        p.state.mod2Rate = normalizeModRate(0.05f);
        p.state.mod2Depth = 0.1f;
        p.state.mod2Target = kTargetFreq;
        presets.push_back(p);
    }

    // ========================================================================
    // Found Sound
    // ========================================================================
    {
        PresetDef p;
        p.name = "Industrial Reson";
        p.category = "Found Sound";
        p.state.releaseTimeMs = 200.0f;
        p.state.inharmonicityAmount = 0.3f;
        p.state.harmonicLevelPlain = 1.6f;
        p.state.residualLevelPlain = 1.2f;
        p.state.transientEmphasisPlain = 0.6f;
        p.state.responsiveness = 0.7f;
        p.state.entropy = 0.3f;
        p.state.coupling = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Object Study";
        p.category = "Found Sound";
        p.state.releaseTimeMs = 150.0f;
        p.state.inharmonicityAmount = 0.4f;
        p.state.harmonicLevelPlain = 1.2f;
        p.state.residualLevelPlain = 1.4f;
        p.state.transientEmphasisPlain = 1.0f;
        p.state.responsiveness = 0.8f;
        p.state.stability = 0.2f;
        p.state.feedbackAmount = 0.3f;
        p.state.feedbackDecay = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Field Recording";
        p.category = "Found Sound";
        p.state.releaseTimeMs = 500.0f;
        p.state.inharmonicityAmount = 0.2f;
        p.state.harmonicLevelPlain = 0.8f;
        p.state.residualLevelPlain = 1.8f;
        p.state.brightnessPlain = -0.1f;
        p.state.responsiveness = 0.5f;
        p.state.entropy = 0.2f;
        p.state.stereoSpread = 0.6f;
        p.state.mod1Enable = 1.0f;
        p.state.mod1Waveform = kWaveRSH;
        p.state.mod1Rate = normalizeModRate(0.5f);
        p.state.mod1Depth = 0.2f;
        p.state.mod1Target = kTargetAmp;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Chaos Machine";
        p.category = "Found Sound";
        p.state.releaseTimeMs = 100.0f;
        p.state.inharmonicityAmount = 0.1f;
        p.state.harmonicLevelPlain = 1.4f;
        p.state.residualLevelPlain = 1.0f;
        p.state.responsiveness = 0.9f;
        p.state.entropy = 0.5f;
        p.state.coupling = 0.7f;
        p.state.stability = 0.1f;
        p.state.feedbackAmount = 0.6f;
        p.state.feedbackDecay = 0.4f;
        p.state.mod1Enable = 1.0f;
        p.state.mod1Waveform = kWaveRSH;
        p.state.mod1Rate = normalizeModRate(3.0f);
        p.state.mod1Depth = 0.3f;
        p.state.mod1Target = kTargetFreq;
        p.state.mod2Enable = 1.0f;
        p.state.mod2Waveform = kWaveSquare;
        p.state.mod2Rate = normalizeModRate(1.5f);
        p.state.mod2Depth = 0.25f;
        p.state.mod2Target = kTargetAmp;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Glass Resonance";
        p.category = "Found Sound";
        p.state.releaseTimeMs = 400.0f;
        p.state.inharmonicityAmount = 0.5f;
        p.state.harmonicLevelPlain = 1.8f;
        p.state.residualLevelPlain = 0.4f;
        p.state.brightnessPlain = 0.5f;
        p.state.harmonicFilterType = kFilterEvenOnly;
        p.state.responsiveness = 0.4f;
        p.state.coupling = 0.8f;
        p.state.warmth = 0.2f;
        p.state.stereoSpread = 0.4f;
        presets.push_back(p);
    }

    return presets;
}

// ==============================================================================
// Main
// ==============================================================================

int main(int argc, char* argv[]) {
    std::filesystem::path outputDir = "plugins/innexus/resources/presets";

    if (argc > 1) {
        outputDir = argv[1];
    }

    std::filesystem::create_directories(outputDir);

    auto presets = createAllPresets();
    int successCount = 0;

    std::cout << "Generating " << presets.size()
              << " Innexus factory presets..." << std::endl;

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
