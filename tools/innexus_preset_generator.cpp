// ==============================================================================
// Factory Preset Generator for Innexus (v2)
// ==============================================================================
// Generates .vstpreset files matching the v2 Processor::getState() binary format.
// Every preset is designed to showcase multiple Innexus features working together.
//
// Reference: plugins/innexus/src/processor/processor_state.cpp getState()
// ==============================================================================

#include "innexus_preset_format.h"

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

using namespace InnexusFormat;

// ==============================================================================
// Constants
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

    f.write("VST3", 4);
    writeLE32(f, 1);
    f.write(kClassIdAscii, 32);
    writeLE64(f, listOffset);

    f.write(reinterpret_cast<const char*>(componentState.data()), compDataSize);

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

// Evolution mode
static constexpr float kEvoCycle = 0.0f;
static constexpr float kEvoPingPong = 0.5f;
static constexpr float kEvoRandomWalk = 1.0f;

// Modulator waveform
static constexpr float kWaveSine = 0.0f;
static constexpr float kWaveTri = 0.25f;
static constexpr float kWaveSquare = 0.5f;
static constexpr float kWaveSaw = 0.75f;
static constexpr float kWaveRSH = 1.0f;

// Modulator target
static constexpr float kTargetAmp = 0.0f;
static constexpr float kTargetFreq = 0.5f;
static constexpr float kTargetPan = 1.0f;

// Harmonic filter type
static constexpr int32_t kFilterAllPass = 0;
static constexpr int32_t kFilterOddOnly = 1;
static constexpr int32_t kFilterEvenOnly = 2;
static constexpr int32_t kFilterLowPartials = 3;
static constexpr int32_t kFilterHighPartials = 4;

// Exciter type
static constexpr float kExciterResidual = 0.0f;
static constexpr float kExciterImpact = 0.5f;
static constexpr float kExciterBow = 1.0f;

// Resonance type
static constexpr float kResonanceModal = 0.0f;
static constexpr float kResonanceWaveguide = 1.0f;

// Voice mode
static constexpr float kVoiceMono = 0.0f;
static constexpr float kVoice4 = 0.5f;
static constexpr float kVoice8 = 1.0f;

float normalizeRange(int partial) {
    return static_cast<float>(std::clamp(partial, 1, 96) - 1) / 95.0f;
}

float normalizeEvoSpeed(float hz) {
    if (hz <= 0.01f) return 0.0f;
    if (hz >= 10.0f) return 1.0f;
    return std::log(hz / 0.01f) / std::log(1000.0f);
}

float normalizeModRate(float hz) {
    if (hz <= 0.01f) return 0.0f;
    if (hz >= 20.0f) return 1.0f;
    return std::log(hz / 0.01f) / std::log(2000.0f);
}

// Normalize resonance decay (log: plain = 0.01 * 500^norm)
float normalizeResDecay(float seconds) {
    if (seconds <= 0.01f) return 0.0f;
    if (seconds >= 5.0f) return 1.0f;
    return std::log(seconds / 0.01f) / std::log(500.0f);
}

// ==============================================================================
// Preset Definitions by Category
// ==============================================================================

std::vector<PresetDef> createAllPresets() {
    std::vector<PresetDef> presets;

    // ========================================================================
    // Voice — vocal analysis/resynthesis presets
    // ========================================================================
    {
        PresetDef p;
        p.name = "Choir Sustain";
        p.category = "Voice";
        auto& s = p.state;
        s.releaseTimeMs = 400.0f;
        s.harmonicLevelPlain = 1.2f;
        s.residualLevelPlain = 0.5f;
        s.brightnessPlain = 0.15f;
        s.warmth = 0.55f;
        s.coupling = 0.35f;
        s.stereoSpread = 0.5f;
        s.detuneSpread = 0.12f;
        s.voiceMode = kVoice4;
        s.sympatheticAmount = 0.2f;
        s.sympatheticDecay = 0.6f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveTri;
        s.mod1Rate = normalizeModRate(0.15f);
        s.mod1Depth = 0.12f;
        s.mod1RangeStart = normalizeRange(1);
        s.mod1RangeEnd = normalizeRange(24);
        s.mod1Target = kTargetPan;
        s.adsrAttackMs = 80.0f;
        s.adsrDecayMs = 200.0f;
        s.adsrSustainLevel = 0.85f;
        s.adsrReleaseMs = 500.0f;
        s.adsrAmount = 0.6f;
        s.adsrAttackCurve = 0.3f;
        s.partialCount = 2.0f / 3.0f; // 80 partials
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Ethereal Soprano";
        p.category = "Voice";
        auto& s = p.state;
        s.releaseTimeMs = 600.0f;
        s.harmonicLevelPlain = 1.1f;
        s.residualLevelPlain = 0.15f;
        s.brightnessPlain = 0.5f;
        s.warmth = 0.45f;
        s.stereoSpread = 0.7f;
        s.detuneSpread = 0.18f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f; // free
        s.mod1Rate = normalizeModRate(0.08f);
        s.mod1Depth = 0.2f;
        s.mod1Target = kTargetPan;
        s.mod2Enable = 1.0f;
        s.mod2Waveform = kWaveTri;
        s.mod2RateSync = 0.0f;
        s.mod2Rate = normalizeModRate(0.25f);
        s.mod2Depth = 0.08f;
        s.mod2RangeStart = normalizeRange(8);
        s.mod2RangeEnd = normalizeRange(48);
        s.mod2Target = kTargetFreq;
        s.sympatheticAmount = 0.15f;
        s.sympatheticDecay = 0.7f;
        s.bodySize = 0.6f;
        s.bodyMaterial = 0.7f;
        s.bodyMix = 0.15f;
        s.adsrAttackMs = 120.0f;
        s.adsrDecayMs = 300.0f;
        s.adsrSustainLevel = 0.9f;
        s.adsrReleaseMs = 800.0f;
        s.adsrAmount = 0.5f;
        s.adsrAttackCurve = 0.4f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Speech Texture";
        p.category = "Voice";
        auto& s = p.state;
        s.releaseTimeMs = 60.0f;
        s.harmonicLevelPlain = 0.8f;
        s.residualLevelPlain = 1.2f;
        s.transientEmphasisPlain = 0.9f;
        s.responsiveness = 0.85f;
        s.entropy = 0.2f;
        s.warmth = 0.2f;
        s.physModelMix = 0.3f;
        s.exciterType = kExciterResidual;
        s.resonanceDecay = normalizeResDecay(0.08f);
        s.resonanceBrightness = 0.65f;
        s.bodySize = 0.35f;
        s.bodyMaterial = 0.6f;
        s.bodyMix = 0.25f;
        s.adsrAttackMs = 5.0f;
        s.adsrDecayMs = 50.0f;
        s.adsrSustainLevel = 0.7f;
        s.adsrReleaseMs = 80.0f;
        s.adsrAmount = 0.8f;
        s.adsrAttackCurve = -0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Formant Shimmer";
        p.category = "Voice";
        auto& s = p.state;
        s.releaseTimeMs = 250.0f;
        s.harmonicLevelPlain = 1.3f;
        s.residualLevelPlain = 0.35f;
        s.brightnessPlain = 0.4f;
        s.harmonicFilterType = kFilterOddOnly;
        s.warmth = 0.35f;
        s.coupling = 0.2f;
        s.stereoSpread = 0.45f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(2.5f);
        s.mod1Depth = 0.2f;
        s.mod1RangeStart = normalizeRange(1);
        s.mod1RangeEnd = normalizeRange(16);
        s.mod1Target = kTargetAmp;
        s.mod2Enable = 1.0f;
        s.mod2Waveform = kWaveTri;
        s.mod2RateSync = 0.0f;
        s.mod2Rate = normalizeModRate(0.4f);
        s.mod2Depth = 0.15f;
        s.mod2RangeStart = normalizeRange(16);
        s.mod2RangeEnd = normalizeRange(64);
        s.mod2Target = kTargetPan;
        s.voiceMode = kVoice4;
        s.adsrAmount = 0.4f;
        s.adsrAttackMs = 40.0f;
        s.adsrReleaseMs = 350.0f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Vocal Drone";
        p.category = "Voice";
        auto& s = p.state;
        s.releaseTimeMs = 2000.0f;
        s.harmonicLevelPlain = 1.0f;
        s.residualLevelPlain = 0.2f;
        s.brightnessPlain = -0.15f;
        s.freeze = 1;
        s.warmth = 0.65f;
        s.coupling = 0.5f;
        s.stability = 0.6f;
        s.entropy = 0.05f;
        s.stereoSpread = 0.65f;
        s.detuneSpread = 0.22f;
        s.feedbackAmount = 0.25f;
        s.feedbackDecay = 0.4f;
        s.physModelMix = 0.2f;
        s.resonanceDecay = normalizeResDecay(2.0f);
        s.resonanceBrightness = 0.4f;
        s.resonanceStretch = 0.15f;
        s.sympatheticAmount = 0.3f;
        s.sympatheticDecay = 0.8f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(0.03f);
        s.mod1Depth = 0.15f;
        s.mod1Target = kTargetAmp;
        s.partialCount = 1.0f; // 96 partials
        presets.push_back(p);
    }

    // ========================================================================
    // Strings — bowed, plucked, and ensemble string textures
    // ========================================================================
    {
        PresetDef p;
        p.name = "Bowed Cello";
        p.category = "Strings";
        auto& s = p.state;
        s.releaseTimeMs = 350.0f;
        s.inharmonicityAmount = 0.85f;
        s.harmonicLevelPlain = 1.1f;
        s.residualLevelPlain = 0.4f;
        s.brightnessPlain = -0.2f;
        s.warmth = 0.7f;
        s.coupling = 0.45f;
        s.stability = 0.35f;
        s.exciterType = kExciterBow;
        s.physModelMix = 0.6f;
        s.resonanceType = kResonanceWaveguide;
        s.resonanceDecay = normalizeResDecay(1.5f);
        s.resonanceBrightness = 0.4f;
        s.waveguideStiffness = 0.15f;
        s.waveguidePickPosition = 0.2f;
        s.bowPressure = 0.45f;
        s.bowSpeed = 0.55f;
        s.bowPosition = 0.18f;
        s.bodySize = 0.75f;
        s.bodyMaterial = 0.55f;
        s.bodyMix = 0.4f;
        s.sympatheticAmount = 0.25f;
        s.sympatheticDecay = 0.65f;
        s.adsrAttackMs = 150.0f;
        s.adsrDecayMs = 200.0f;
        s.adsrSustainLevel = 0.9f;
        s.adsrReleaseMs = 400.0f;
        s.adsrAmount = 0.7f;
        s.adsrAttackCurve = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Plucked Guitar";
        p.category = "Strings";
        auto& s = p.state;
        s.releaseTimeMs = 120.0f;
        s.inharmonicityAmount = 0.9f;
        s.harmonicLevelPlain = 1.0f;
        s.residualLevelPlain = 0.6f;
        s.transientEmphasisPlain = 0.7f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.7f;
        s.resonanceType = kResonanceWaveguide;
        s.resonanceDecay = normalizeResDecay(0.8f);
        s.resonanceBrightness = 0.55f;
        s.waveguideStiffness = 0.1f;
        s.waveguidePickPosition = 0.13f;
        s.impactHardness = 0.4f;
        s.impactMass = 0.2f;
        s.impactBrightness = 0.55f;
        s.impactPosition = 0.15f;
        s.bodySize = 0.6f;
        s.bodyMaterial = 0.5f;
        s.bodyMix = 0.35f;
        s.sympatheticAmount = 0.3f;
        s.sympatheticDecay = 0.5f;
        s.voiceMode = kVoice4;
        s.adsrAttackMs = 3.0f;
        s.adsrDecayMs = 300.0f;
        s.adsrSustainLevel = 0.3f;
        s.adsrReleaseMs = 200.0f;
        s.adsrAmount = 0.85f;
        s.adsrDecayCurve = -0.4f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "String Ensemble";
        p.category = "Strings";
        auto& s = p.state;
        s.releaseTimeMs = 500.0f;
        s.harmonicLevelPlain = 1.2f;
        s.residualLevelPlain = 0.4f;
        s.warmth = 0.55f;
        s.coupling = 0.35f;
        s.stereoSpread = 0.75f;
        s.detuneSpread = 0.2f;
        s.voiceMode = kVoice8;
        s.exciterType = kExciterBow;
        s.physModelMix = 0.4f;
        s.resonanceType = kResonanceWaveguide;
        s.resonanceDecay = normalizeResDecay(1.2f);
        s.resonanceBrightness = 0.45f;
        s.waveguideStiffness = 0.08f;
        s.bowPressure = 0.35f;
        s.bowSpeed = 0.5f;
        s.bowPosition = 0.22f;
        s.bodySize = 0.7f;
        s.bodyMaterial = 0.5f;
        s.bodyMix = 0.3f;
        s.sympatheticAmount = 0.35f;
        s.sympatheticDecay = 0.7f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(0.12f);
        s.mod1Depth = 0.1f;
        s.mod1Target = kTargetPan;
        s.adsrAttackMs = 200.0f;
        s.adsrSustainLevel = 0.9f;
        s.adsrReleaseMs = 600.0f;
        s.adsrAmount = 0.5f;
        s.adsrAttackCurve = 0.6f;
        s.partialCount = 2.0f / 3.0f; // 80
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Sitar Drone";
        p.category = "Strings";
        auto& s = p.state;
        s.releaseTimeMs = 800.0f;
        s.inharmonicityAmount = 0.7f;
        s.harmonicLevelPlain = 1.3f;
        s.residualLevelPlain = 0.5f;
        s.brightnessPlain = 0.3f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.5f;
        s.resonanceType = kResonanceWaveguide;
        s.resonanceDecay = normalizeResDecay(2.5f);
        s.resonanceBrightness = 0.7f;
        s.resonanceStretch = 0.25f;
        s.waveguideStiffness = 0.3f;
        s.waveguidePickPosition = 0.08f;
        s.impactHardness = 0.55f;
        s.impactMass = 0.15f;
        s.impactPosition = 0.1f;
        s.bodySize = 0.55f;
        s.bodyMaterial = 0.6f;
        s.bodyMix = 0.4f;
        s.sympatheticAmount = 0.5f;
        s.sympatheticDecay = 0.8f;
        s.warmth = 0.4f;
        s.coupling = 0.6f;
        s.stability = 0.3f;
        s.stereoSpread = 0.4f;
        s.partialCount = 1.0f; // 96
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Piano Harmonics";
        p.category = "Strings";
        auto& s = p.state;
        s.releaseTimeMs = 200.0f;
        s.inharmonicityAmount = 0.8f;
        s.harmonicLevelPlain = 1.4f;
        s.residualLevelPlain = 0.3f;
        s.brightnessPlain = 0.35f;
        s.harmonicFilterType = kFilterHighPartials;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.5f;
        s.resonanceDecay = normalizeResDecay(1.8f);
        s.resonanceBrightness = 0.6f;
        s.resonanceStretch = 0.2f;
        s.impactHardness = 0.7f;
        s.impactMass = 0.4f;
        s.impactBrightness = 0.6f;
        s.impactPosition = 0.12f;
        s.bodySize = 0.85f;
        s.bodyMaterial = 0.45f;
        s.bodyMix = 0.35f;
        s.sympatheticAmount = 0.4f;
        s.sympatheticDecay = 0.75f;
        s.coupling = 0.55f;
        s.stability = 0.4f;
        s.voiceMode = kVoice8;
        s.stereoSpread = 0.35f;
        s.adsrAttackMs = 2.0f;
        s.adsrDecayMs = 800.0f;
        s.adsrSustainLevel = 0.15f;
        s.adsrReleaseMs = 500.0f;
        s.adsrAmount = 0.9f;
        s.adsrDecayCurve = -0.5f;
        presets.push_back(p);
    }

    // ========================================================================
    // Keys — keyboard/mallet instrument textures
    // ========================================================================
    {
        PresetDef p;
        p.name = "Struck Marimba";
        p.category = "Keys";
        auto& s = p.state;
        s.releaseTimeMs = 80.0f;
        s.inharmonicityAmount = 0.6f;
        s.harmonicLevelPlain = 1.0f;
        s.residualLevelPlain = 0.5f;
        s.transientEmphasisPlain = 0.8f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.75f;
        s.resonanceDecay = normalizeResDecay(0.6f);
        s.resonanceBrightness = 0.5f;
        s.resonanceScatter = 0.15f;
        s.impactHardness = 0.35f;
        s.impactMass = 0.45f;
        s.impactBrightness = 0.45f;
        s.impactPosition = 0.5f;
        s.bodySize = 0.5f;
        s.bodyMaterial = 0.55f;
        s.bodyMix = 0.5f;
        s.voiceMode = kVoice8;
        s.warmth = 0.3f;
        s.coupling = 0.4f;
        s.adsrAttackMs = 1.0f;
        s.adsrDecayMs = 400.0f;
        s.adsrSustainLevel = 0.1f;
        s.adsrReleaseMs = 150.0f;
        s.adsrAmount = 0.95f;
        s.adsrDecayCurve = -0.6f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Electric Piano";
        p.category = "Keys";
        auto& s = p.state;
        s.releaseTimeMs = 180.0f;
        s.harmonicLevelPlain = 1.2f;
        s.residualLevelPlain = 0.4f;
        s.brightnessPlain = 0.25f;
        s.warmth = 0.5f;
        s.stereoSpread = 0.35f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.45f;
        s.resonanceDecay = normalizeResDecay(0.9f);
        s.resonanceBrightness = 0.55f;
        s.impactHardness = 0.6f;
        s.impactMass = 0.35f;
        s.impactBrightness = 0.55f;
        s.bodySize = 0.4f;
        s.bodyMaterial = 0.35f;
        s.bodyMix = 0.2f;
        s.voiceMode = kVoice8;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(4.8f);
        s.mod1Depth = 0.08f;
        s.mod1RangeStart = normalizeRange(1);
        s.mod1RangeEnd = normalizeRange(8);
        s.mod1Target = kTargetAmp;
        s.adsrAttackMs = 2.0f;
        s.adsrDecayMs = 600.0f;
        s.adsrSustainLevel = 0.25f;
        s.adsrReleaseMs = 250.0f;
        s.adsrAmount = 0.85f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Music Box";
        p.category = "Keys";
        auto& s = p.state;
        s.releaseTimeMs = 250.0f;
        s.inharmonicityAmount = 0.5f;
        s.harmonicLevelPlain = 1.4f;
        s.residualLevelPlain = 0.2f;
        s.brightnessPlain = 0.7f;
        s.harmonicFilterType = kFilterHighPartials;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.6f;
        s.resonanceDecay = normalizeResDecay(1.5f);
        s.resonanceBrightness = 0.75f;
        s.resonanceStretch = 0.3f;
        s.impactHardness = 0.8f;
        s.impactMass = 0.1f;
        s.impactBrightness = 0.7f;
        s.impactPosition = 0.05f;
        s.bodySize = 0.25f;
        s.bodyMaterial = 0.3f;
        s.bodyMix = 0.3f;
        s.sympatheticAmount = 0.35f;
        s.sympatheticDecay = 0.6f;
        s.stereoSpread = 0.5f;
        s.detuneSpread = 0.06f;
        s.voiceMode = kVoice8;
        s.coupling = 0.3f;
        s.adsrAttackMs = 1.0f;
        s.adsrDecayMs = 1200.0f;
        s.adsrSustainLevel = 0.05f;
        s.adsrReleaseMs = 400.0f;
        s.adsrAmount = 0.9f;
        s.adsrDecayCurve = -0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Glass Bells";
        p.category = "Keys";
        auto& s = p.state;
        s.releaseTimeMs = 400.0f;
        s.inharmonicityAmount = 0.4f;
        s.harmonicLevelPlain = 1.5f;
        s.residualLevelPlain = 0.15f;
        s.brightnessPlain = 0.6f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.8f;
        s.resonanceDecay = normalizeResDecay(2.0f);
        s.resonanceBrightness = 0.7f;
        s.resonanceStretch = 0.4f;
        s.resonanceScatter = 0.2f;
        s.impactHardness = 0.9f;
        s.impactMass = 0.15f;
        s.impactBrightness = 0.65f;
        s.impactPosition = 0.3f;
        s.bodySize = 0.3f;
        s.bodyMaterial = 0.2f;
        s.bodyMix = 0.25f;
        s.stereoSpread = 0.6f;
        s.coupling = 0.7f;
        s.warmth = 0.15f;
        s.voiceMode = kVoice4;
        s.adsrAttackMs = 1.0f;
        s.adsrDecayMs = 2000.0f;
        s.adsrSustainLevel = 0.0f;
        s.adsrReleaseMs = 600.0f;
        s.adsrAmount = 1.0f;
        s.adsrDecayCurve = -0.4f;
        s.partialCount = 1.0f; // 96
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Hollow Organ";
        p.category = "Keys";
        auto& s = p.state;
        s.releaseTimeMs = 50.0f;
        s.harmonicLevelPlain = 1.4f;
        s.residualLevelPlain = 0.15f;
        s.harmonicFilterType = kFilterOddOnly;
        s.warmth = 0.35f;
        s.coupling = 0.2f;
        s.voiceMode = kVoice8;
        s.stereoSpread = 0.3f;
        s.detuneSpread = 0.04f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(6.0f);
        s.mod1Depth = 0.06f;
        s.mod1Target = kTargetFreq;
        s.adsrAttackMs = 20.0f;
        s.adsrDecayMs = 50.0f;
        s.adsrSustainLevel = 1.0f;
        s.adsrReleaseMs = 60.0f;
        s.adsrAmount = 0.6f;
        presets.push_back(p);
    }

    // ========================================================================
    // Brass & Winds — wind instrument textures
    // ========================================================================
    {
        PresetDef p;
        p.name = "Brass Section";
        p.category = "Brass and Winds";
        auto& s = p.state;
        s.releaseTimeMs = 120.0f;
        s.harmonicLevelPlain = 1.3f;
        s.residualLevelPlain = 0.5f;
        s.brightnessPlain = 0.35f;
        s.warmth = 0.55f;
        s.coupling = 0.4f;
        s.exciterType = kExciterBow;
        s.physModelMix = 0.35f;
        s.resonanceDecay = normalizeResDecay(0.3f);
        s.resonanceBrightness = 0.6f;
        s.bowPressure = 0.5f;
        s.bowSpeed = 0.65f;
        s.bowPosition = 0.25f;
        s.bodySize = 0.5f;
        s.bodyMaterial = 0.3f;
        s.bodyMix = 0.3f;
        s.voiceMode = kVoice4;
        s.stereoSpread = 0.4f;
        s.detuneSpread = 0.05f;
        s.adsrAttackMs = 30.0f;
        s.adsrDecayMs = 100.0f;
        s.adsrSustainLevel = 0.85f;
        s.adsrReleaseMs = 150.0f;
        s.adsrAmount = 0.7f;
        s.adsrAttackCurve = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Breathy Flute";
        p.category = "Brass and Winds";
        auto& s = p.state;
        s.releaseTimeMs = 100.0f;
        s.harmonicLevelPlain = 0.9f;
        s.residualLevelPlain = 1.1f;
        s.brightnessPlain = 0.25f;
        s.warmth = 0.3f;
        s.physModelMix = 0.25f;
        s.resonanceDecay = normalizeResDecay(0.15f);
        s.resonanceBrightness = 0.5f;
        s.bodySize = 0.3f;
        s.bodyMaterial = 0.6f;
        s.bodyMix = 0.2f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(5.5f);
        s.mod1Depth = 0.06f;
        s.mod1RangeStart = normalizeRange(1);
        s.mod1RangeEnd = normalizeRange(4);
        s.mod1Target = kTargetFreq;
        s.adsrAttackMs = 60.0f;
        s.adsrSustainLevel = 0.9f;
        s.adsrReleaseMs = 120.0f;
        s.adsrAmount = 0.6f;
        s.adsrAttackCurve = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Clarinet Reed";
        p.category = "Brass and Winds";
        auto& s = p.state;
        s.releaseTimeMs = 80.0f;
        s.harmonicLevelPlain = 1.2f;
        s.residualLevelPlain = 0.7f;
        s.harmonicFilterType = kFilterOddOnly;
        s.warmth = 0.4f;
        s.entropy = 0.1f;
        s.exciterType = kExciterBow;
        s.physModelMix = 0.3f;
        s.resonanceDecay = normalizeResDecay(0.2f);
        s.resonanceBrightness = 0.45f;
        s.bowPressure = 0.4f;
        s.bowSpeed = 0.6f;
        s.bowPosition = 0.3f;
        s.bodySize = 0.35f;
        s.bodyMaterial = 0.55f;
        s.bodyMix = 0.25f;
        s.voiceMode = kVoice4;
        s.adsrAttackMs = 25.0f;
        s.adsrSustainLevel = 0.95f;
        s.adsrReleaseMs = 100.0f;
        s.adsrAmount = 0.65f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Horn Swell";
        p.category = "Brass and Winds";
        auto& s = p.state;
        s.releaseTimeMs = 250.0f;
        s.harmonicLevelPlain = 1.2f;
        s.residualLevelPlain = 0.45f;
        s.brightnessPlain = 0.15f;
        s.warmth = 0.65f;
        s.coupling = 0.5f;
        s.stereoSpread = 0.35f;
        s.exciterType = kExciterBow;
        s.physModelMix = 0.3f;
        s.resonanceDecay = normalizeResDecay(0.4f);
        s.resonanceBrightness = 0.5f;
        s.bowPressure = 0.55f;
        s.bowSpeed = 0.5f;
        s.bowPosition = 0.22f;
        s.bodySize = 0.55f;
        s.bodyMaterial = 0.35f;
        s.bodyMix = 0.25f;
        s.sympatheticAmount = 0.15f;
        s.sympatheticDecay = 0.5f;
        s.voiceMode = kVoice4;
        s.evolutionEnable = 1.0f;
        s.evolutionSpeed = normalizeEvoSpeed(0.08f);
        s.evolutionDepth = 0.2f;
        s.evolutionMode = kEvoPingPong;
        s.adsrAttackMs = 200.0f;
        s.adsrDecayMs = 150.0f;
        s.adsrSustainLevel = 0.85f;
        s.adsrReleaseMs = 300.0f;
        s.adsrAmount = 0.75f;
        s.adsrAttackCurve = 0.7f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Didgeridoo";
        p.category = "Brass and Winds";
        auto& s = p.state;
        s.releaseTimeMs = 400.0f;
        s.harmonicLevelPlain = 1.0f;
        s.residualLevelPlain = 0.9f;
        s.brightnessPlain = -0.3f;
        s.harmonicFilterType = kFilterLowPartials;
        s.warmth = 0.7f;
        s.coupling = 0.6f;
        s.stability = 0.4f;
        s.exciterType = kExciterBow;
        s.physModelMix = 0.4f;
        s.resonanceDecay = normalizeResDecay(0.5f);
        s.resonanceBrightness = 0.3f;
        s.bowPressure = 0.6f;
        s.bowSpeed = 0.35f;
        s.bowPosition = 0.35f;
        s.bodySize = 0.9f;
        s.bodyMaterial = 0.55f;
        s.bodyMix = 0.45f;
        s.feedbackAmount = 0.2f;
        s.feedbackDecay = 0.35f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(0.3f);
        s.mod1Depth = 0.15f;
        s.mod1RangeStart = normalizeRange(1);
        s.mod1RangeEnd = normalizeRange(6);
        s.mod1Target = kTargetAmp;
        s.adsrAttackMs = 50.0f;
        s.adsrSustainLevel = 0.95f;
        s.adsrReleaseMs = 500.0f;
        s.adsrAmount = 0.5f;
        s.partialCount = 2.0f / 3.0f;
        presets.push_back(p);
    }

    // ========================================================================
    // Drums & Perc — percussive and transient-heavy textures
    // ========================================================================
    {
        PresetDef p;
        p.name = "Struck Metal";
        p.category = "Drums and Perc";
        auto& s = p.state;
        s.releaseTimeMs = 300.0f;
        s.inharmonicityAmount = 0.4f;
        s.harmonicLevelPlain = 1.3f;
        s.residualLevelPlain = 0.5f;
        s.brightnessPlain = 0.6f;
        s.transientEmphasisPlain = 0.5f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.85f;
        s.resonanceDecay = normalizeResDecay(1.5f);
        s.resonanceBrightness = 0.7f;
        s.resonanceStretch = 0.5f;
        s.resonanceScatter = 0.3f;
        s.impactHardness = 0.75f;
        s.impactMass = 0.5f;
        s.impactBrightness = 0.6f;
        s.impactPosition = 0.35f;
        s.bodySize = 0.4f;
        s.bodyMaterial = 0.15f;
        s.bodyMix = 0.3f;
        s.coupling = 0.65f;
        s.sympatheticAmount = 0.2f;
        s.sympatheticDecay = 0.5f;
        s.voiceMode = kVoice4;
        s.adsrAttackMs = 1.0f;
        s.adsrDecayMs = 1500.0f;
        s.adsrSustainLevel = 0.0f;
        s.adsrReleaseMs = 300.0f;
        s.adsrAmount = 1.0f;
        s.adsrDecayCurve = -0.5f;
        s.partialCount = 1.0f; // 96
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Tuned Drum";
        p.category = "Drums and Perc";
        auto& s = p.state;
        s.releaseTimeMs = 100.0f;
        s.inharmonicityAmount = 0.3f;
        s.harmonicLevelPlain = 1.1f;
        s.residualLevelPlain = 0.9f;
        s.transientEmphasisPlain = 1.0f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.7f;
        s.resonanceDecay = normalizeResDecay(0.25f);
        s.resonanceBrightness = 0.45f;
        s.impactHardness = 0.5f;
        s.impactMass = 0.6f;
        s.impactBrightness = 0.45f;
        s.impactPosition = 0.45f;
        s.bodySize = 0.55f;
        s.bodyMaterial = 0.7f;
        s.bodyMix = 0.45f;
        s.warmth = 0.35f;
        s.voiceMode = kVoice4;
        s.adsrAttackMs = 1.0f;
        s.adsrDecayMs = 250.0f;
        s.adsrSustainLevel = 0.0f;
        s.adsrReleaseMs = 100.0f;
        s.adsrAmount = 1.0f;
        s.adsrAttackCurve = -0.5f;
        s.adsrDecayCurve = -0.7f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Gamelan Gong";
        p.category = "Drums and Perc";
        auto& s = p.state;
        s.releaseTimeMs = 500.0f;
        s.inharmonicityAmount = 0.35f;
        s.harmonicLevelPlain = 1.5f;
        s.residualLevelPlain = 0.3f;
        s.brightnessPlain = 0.4f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.9f;
        s.resonanceDecay = normalizeResDecay(3.0f);
        s.resonanceBrightness = 0.55f;
        s.resonanceStretch = 0.35f;
        s.resonanceScatter = 0.25f;
        s.impactHardness = 0.6f;
        s.impactMass = 0.7f;
        s.impactBrightness = 0.5f;
        s.impactPosition = 0.4f;
        s.bodySize = 0.8f;
        s.bodyMaterial = 0.25f;
        s.bodyMix = 0.4f;
        s.coupling = 0.75f;
        s.warmth = 0.25f;
        s.sympatheticAmount = 0.4f;
        s.sympatheticDecay = 0.85f;
        s.stereoSpread = 0.45f;
        s.voiceMode = kVoice4;
        s.adsrAttackMs = 1.0f;
        s.adsrDecayMs = 3000.0f;
        s.adsrSustainLevel = 0.0f;
        s.adsrAmount = 1.0f;
        s.adsrDecayCurve = -0.3f;
        s.partialCount = 1.0f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Wood Block";
        p.category = "Drums and Perc";
        auto& s = p.state;
        s.releaseTimeMs = 40.0f;
        s.inharmonicityAmount = 0.5f;
        s.harmonicLevelPlain = 0.9f;
        s.residualLevelPlain = 1.0f;
        s.transientEmphasisPlain = 1.2f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.8f;
        s.resonanceDecay = normalizeResDecay(0.06f);
        s.resonanceBrightness = 0.5f;
        s.resonanceScatter = 0.1f;
        s.impactHardness = 0.65f;
        s.impactMass = 0.3f;
        s.impactBrightness = 0.5f;
        s.impactPosition = 0.4f;
        s.bodySize = 0.2f;
        s.bodyMaterial = 0.65f;
        s.bodyMix = 0.55f;
        s.warmth = 0.25f;
        s.voiceMode = kVoice4;
        s.adsrAttackMs = 1.0f;
        s.adsrDecayMs = 80.0f;
        s.adsrSustainLevel = 0.0f;
        s.adsrReleaseMs = 40.0f;
        s.adsrAmount = 1.0f;
        s.adsrDecayCurve = -0.8f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Cymbal Wash";
        p.category = "Drums and Perc";
        auto& s = p.state;
        s.releaseTimeMs = 800.0f;
        s.inharmonicityAmount = 0.2f;
        s.harmonicLevelPlain = 0.7f;
        s.residualLevelPlain = 1.4f;
        s.brightnessPlain = 0.45f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.6f;
        s.resonanceDecay = normalizeResDecay(2.0f);
        s.resonanceBrightness = 0.7f;
        s.resonanceStretch = 0.6f;
        s.resonanceScatter = 0.45f;
        s.impactHardness = 0.5f;
        s.impactMass = 0.25f;
        s.impactBrightness = 0.6f;
        s.impactPosition = 0.2f;
        s.bodySize = 0.5f;
        s.bodyMaterial = 0.2f;
        s.bodyMix = 0.25f;
        s.coupling = 0.5f;
        s.entropy = 0.15f;
        s.stereoSpread = 0.55f;
        s.sympatheticAmount = 0.15f;
        s.sympatheticDecay = 0.4f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(0.1f);
        s.mod1Depth = 0.1f;
        s.mod1Target = kTargetPan;
        s.adsrAttackMs = 1.0f;
        s.adsrDecayMs = 2500.0f;
        s.adsrSustainLevel = 0.0f;
        s.adsrReleaseMs = 500.0f;
        s.adsrAmount = 0.85f;
        s.adsrDecayCurve = -0.2f;
        s.partialCount = 1.0f;
        presets.push_back(p);
    }

    // ========================================================================
    // Pads & Drones — sustained, evolving textures
    // ========================================================================
    {
        PresetDef p;
        p.name = "Frozen Cathedral";
        p.category = "Pads and Drones";
        auto& s = p.state;
        s.releaseTimeMs = 2000.0f;
        s.harmonicLevelPlain = 1.1f;
        s.residualLevelPlain = 0.2f;
        s.brightnessPlain = 0.2f;
        s.freeze = 1;
        s.warmth = 0.6f;
        s.coupling = 0.5f;
        s.stability = 0.5f;
        s.entropy = 0.05f;
        s.stereoSpread = 0.85f;
        s.detuneSpread = 0.2f;
        s.physModelMix = 0.2f;
        s.resonanceDecay = normalizeResDecay(3.0f);
        s.resonanceBrightness = 0.4f;
        s.bodySize = 0.9f;
        s.bodyMaterial = 0.5f;
        s.bodyMix = 0.25f;
        s.sympatheticAmount = 0.35f;
        s.sympatheticDecay = 0.9f;
        s.voiceMode = kVoice4;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(0.04f);
        s.mod1Depth = 0.15f;
        s.mod1Target = kTargetAmp;
        s.mod2Enable = 1.0f;
        s.mod2Waveform = kWaveTri;
        s.mod2RateSync = 0.0f;
        s.mod2Rate = normalizeModRate(0.07f);
        s.mod2Depth = 0.12f;
        s.mod2RangeStart = normalizeRange(12);
        s.mod2RangeEnd = normalizeRange(64);
        s.mod2Target = kTargetPan;
        s.partialCount = 1.0f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Evolving Texture";
        p.category = "Pads and Drones";
        auto& s = p.state;
        s.releaseTimeMs = 1500.0f;
        s.harmonicLevelPlain = 1.15f;
        s.residualLevelPlain = 0.5f;
        s.warmth = 0.45f;
        s.entropy = 0.12f;
        s.stereoSpread = 0.65f;
        s.detuneSpread = 0.18f;
        s.evolutionEnable = 1.0f;
        s.evolutionSpeed = normalizeEvoSpeed(0.12f);
        s.evolutionDepth = 0.65f;
        s.evolutionMode = kEvoRandomWalk;
        s.physModelMix = 0.15f;
        s.resonanceDecay = normalizeResDecay(1.0f);
        s.bodySize = 0.6f;
        s.bodyMix = 0.15f;
        s.sympatheticAmount = 0.2f;
        s.sympatheticDecay = 0.7f;
        s.voiceMode = kVoice4;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveTri;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(0.18f);
        s.mod1Depth = 0.12f;
        s.mod1RangeStart = normalizeRange(6);
        s.mod1RangeEnd = normalizeRange(32);
        s.mod1Target = kTargetAmp;
        s.mod2Enable = 1.0f;
        s.mod2Waveform = kWaveSine;
        s.mod2RateSync = 0.0f;
        s.mod2Rate = normalizeModRate(0.3f);
        s.mod2Depth = 0.08f;
        s.mod2Target = kTargetPan;
        s.adsrAttackMs = 300.0f;
        s.adsrSustainLevel = 0.9f;
        s.adsrReleaseMs = 1000.0f;
        s.adsrAmount = 0.4f;
        s.adsrAttackCurve = 0.6f;
        s.partialCount = 2.0f / 3.0f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Feedback Drone";
        p.category = "Pads and Drones";
        auto& s = p.state;
        s.releaseTimeMs = 3000.0f;
        s.harmonicLevelPlain = 1.2f;
        s.residualLevelPlain = 0.4f;
        s.brightnessPlain = -0.15f;
        s.harmonicFilterType = kFilterLowPartials;
        s.warmth = 0.75f;
        s.coupling = 0.6f;
        s.stability = 0.55f;
        s.entropy = 0.08f;
        s.stereoSpread = 0.55f;
        s.feedbackAmount = 0.4f;
        s.feedbackDecay = 0.45f;
        s.physModelMix = 0.25f;
        s.resonanceDecay = normalizeResDecay(2.5f);
        s.resonanceBrightness = 0.35f;
        s.bodySize = 0.8f;
        s.bodyMaterial = 0.5f;
        s.bodyMix = 0.2f;
        s.sympatheticAmount = 0.3f;
        s.sympatheticDecay = 0.85f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(0.025f);
        s.mod1Depth = 0.18f;
        s.mod1Target = kTargetAmp;
        s.partialCount = 1.0f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Crystalline Bed";
        p.category = "Pads and Drones";
        auto& s = p.state;
        s.releaseTimeMs = 2500.0f;
        s.harmonicLevelPlain = 1.4f;
        s.residualLevelPlain = 0.1f;
        s.brightnessPlain = 0.6f;
        s.harmonicFilterType = kFilterHighPartials;
        s.stereoSpread = 0.9f;
        s.detuneSpread = 0.25f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.5f;
        s.resonanceDecay = normalizeResDecay(3.5f);
        s.resonanceBrightness = 0.75f;
        s.resonanceStretch = 0.3f;
        s.impactHardness = 0.85f;
        s.impactMass = 0.1f;
        s.bodySize = 0.35f;
        s.bodyMaterial = 0.25f;
        s.bodyMix = 0.2f;
        s.coupling = 0.5f;
        s.sympatheticAmount = 0.4f;
        s.sympatheticDecay = 0.9f;
        s.voiceMode = kVoice4;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(0.05f);
        s.mod1Depth = 0.1f;
        s.mod1Target = kTargetFreq;
        s.mod2Enable = 1.0f;
        s.mod2Waveform = kWaveTri;
        s.mod2RateSync = 0.0f;
        s.mod2Rate = normalizeModRate(0.08f);
        s.mod2Depth = 0.15f;
        s.mod2Target = kTargetPan;
        s.partialCount = 1.0f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Bowed Ambient";
        p.category = "Pads and Drones";
        auto& s = p.state;
        s.releaseTimeMs = 2000.0f;
        s.harmonicLevelPlain = 1.0f;
        s.residualLevelPlain = 0.3f;
        s.brightnessPlain = -0.1f;
        s.warmth = 0.5f;
        s.coupling = 0.4f;
        s.stability = 0.45f;
        s.stereoSpread = 0.7f;
        s.detuneSpread = 0.15f;
        s.exciterType = kExciterBow;
        s.physModelMix = 0.5f;
        s.resonanceType = kResonanceWaveguide;
        s.resonanceDecay = normalizeResDecay(2.5f);
        s.resonanceBrightness = 0.4f;
        s.waveguideStiffness = 0.1f;
        s.bowPressure = 0.2f;
        s.bowSpeed = 0.35f;
        s.bowPosition = 0.25f;
        s.bodySize = 0.75f;
        s.bodyMaterial = 0.55f;
        s.bodyMix = 0.3f;
        s.sympatheticAmount = 0.35f;
        s.sympatheticDecay = 0.85f;
        s.voiceMode = kVoice4;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(0.06f);
        s.mod1Depth = 0.12f;
        s.mod1Target = kTargetAmp;
        s.mod2Enable = 1.0f;
        s.mod2Waveform = kWaveTri;
        s.mod2RateSync = 0.0f;
        s.mod2Rate = normalizeModRate(0.1f);
        s.mod2Depth = 0.1f;
        s.mod2RangeStart = normalizeRange(8);
        s.mod2RangeEnd = normalizeRange(48);
        s.mod2Target = kTargetPan;
        s.adsrAttackMs = 500.0f;
        s.adsrSustainLevel = 0.95f;
        s.adsrReleaseMs = 1500.0f;
        s.adsrAmount = 0.45f;
        s.adsrAttackCurve = 0.7f;
        s.partialCount = 2.0f / 3.0f;
        presets.push_back(p);
    }

    // ========================================================================
    // Found Sound — experimental/textural presets
    // ========================================================================
    {
        PresetDef p;
        p.name = "Chaos Machine";
        p.category = "Found Sound";
        auto& s = p.state;
        s.releaseTimeMs = 150.0f;
        s.inharmonicityAmount = 0.15f;
        s.harmonicLevelPlain = 1.2f;
        s.residualLevelPlain = 0.8f;
        s.entropy = 0.45f;
        s.coupling = 0.7f;
        s.stability = 0.1f;
        s.warmth = 0.2f;
        s.feedbackAmount = 0.55f;
        s.feedbackDecay = 0.35f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.6f;
        s.resonanceDecay = normalizeResDecay(0.4f);
        s.resonanceBrightness = 0.65f;
        s.resonanceStretch = 0.6f;
        s.resonanceScatter = 0.5f;
        s.impactHardness = 0.7f;
        s.impactMass = 0.3f;
        s.impactBrightness = 0.6f;
        s.bodySize = 0.3f;
        s.bodyMaterial = 0.2f;
        s.bodyMix = 0.35f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveRSH;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(3.5f);
        s.mod1Depth = 0.3f;
        s.mod1Target = kTargetFreq;
        s.mod2Enable = 1.0f;
        s.mod2Waveform = kWaveSquare;
        s.mod2RateSync = 0.0f;
        s.mod2Rate = normalizeModRate(1.8f);
        s.mod2Depth = 0.25f;
        s.mod2RangeStart = normalizeRange(1);
        s.mod2RangeEnd = normalizeRange(32);
        s.mod2Target = kTargetAmp;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Industrial Reson";
        p.category = "Found Sound";
        auto& s = p.state;
        s.releaseTimeMs = 200.0f;
        s.inharmonicityAmount = 0.25f;
        s.harmonicLevelPlain = 1.3f;
        s.residualLevelPlain = 1.0f;
        s.transientEmphasisPlain = 0.7f;
        s.entropy = 0.3f;
        s.coupling = 0.55f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.7f;
        s.resonanceDecay = normalizeResDecay(0.6f);
        s.resonanceBrightness = 0.6f;
        s.resonanceStretch = 0.45f;
        s.resonanceScatter = 0.4f;
        s.impactHardness = 0.8f;
        s.impactMass = 0.55f;
        s.impactBrightness = 0.55f;
        s.impactPosition = 0.25f;
        s.bodySize = 0.45f;
        s.bodyMaterial = 0.15f;
        s.bodyMix = 0.35f;
        s.stereoSpread = 0.4f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSaw;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(0.8f);
        s.mod1Depth = 0.2f;
        s.mod1RangeStart = normalizeRange(4);
        s.mod1RangeEnd = normalizeRange(48);
        s.mod1Target = kTargetAmp;
        s.adsrAttackMs = 2.0f;
        s.adsrDecayMs = 500.0f;
        s.adsrSustainLevel = 0.2f;
        s.adsrReleaseMs = 200.0f;
        s.adsrAmount = 0.8f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Bowed Glass";
        p.category = "Found Sound";
        auto& s = p.state;
        s.releaseTimeMs = 600.0f;
        s.inharmonicityAmount = 0.45f;
        s.harmonicLevelPlain = 1.4f;
        s.residualLevelPlain = 0.25f;
        s.brightnessPlain = 0.45f;
        s.harmonicFilterType = kFilterEvenOnly;
        s.exciterType = kExciterBow;
        s.physModelMix = 0.7f;
        s.resonanceDecay = normalizeResDecay(2.0f);
        s.resonanceBrightness = 0.65f;
        s.resonanceStretch = 0.35f;
        s.bowPressure = 0.25f;
        s.bowSpeed = 0.4f;
        s.bowPosition = 0.1f;
        s.bodySize = 0.25f;
        s.bodyMaterial = 0.2f;
        s.bodyMix = 0.3f;
        s.coupling = 0.75f;
        s.warmth = 0.15f;
        s.stereoSpread = 0.5f;
        s.sympatheticAmount = 0.3f;
        s.sympatheticDecay = 0.7f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveSine;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(0.15f);
        s.mod1Depth = 0.1f;
        s.mod1Target = kTargetPan;
        s.partialCount = 1.0f;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Alien Transmission";
        p.category = "Found Sound";
        auto& s = p.state;
        s.releaseTimeMs = 400.0f;
        s.inharmonicityAmount = 0.1f;
        s.harmonicLevelPlain = 1.1f;
        s.residualLevelPlain = 0.6f;
        s.brightnessPlain = 0.3f;
        s.warmth = 0.3f;
        s.stability = 0.7f;
        s.entropy = 0.2f;
        s.feedbackAmount = 0.35f;
        s.feedbackDecay = 0.5f;
        s.exciterType = kExciterBow;
        s.physModelMix = 0.4f;
        s.resonanceType = kResonanceWaveguide;
        s.resonanceDecay = normalizeResDecay(1.0f);
        s.resonanceBrightness = 0.55f;
        s.waveguideStiffness = 0.5f;
        s.waveguidePickPosition = 0.35f;
        s.bowPressure = 0.6f;
        s.bowSpeed = 0.3f;
        s.bowPosition = 0.05f;
        s.stereoSpread = 0.7f;
        s.detuneSpread = 0.3f;
        s.mod1Enable = 1.0f;
        s.mod1Waveform = kWaveRSH;
        s.mod1RateSync = 0.0f;
        s.mod1Rate = normalizeModRate(0.6f);
        s.mod1Depth = 0.25f;
        s.mod1Target = kTargetFreq;
        s.mod2Enable = 1.0f;
        s.mod2Waveform = kWaveSaw;
        s.mod2RateSync = 0.0f;
        s.mod2Rate = normalizeModRate(0.2f);
        s.mod2Depth = 0.2f;
        s.mod2Target = kTargetAmp;
        s.evolutionEnable = 1.0f;
        s.evolutionSpeed = normalizeEvoSpeed(0.08f);
        s.evolutionDepth = 0.7f;
        s.evolutionMode = kEvoRandomWalk;
        presets.push_back(p);
    }
    {
        PresetDef p;
        p.name = "Object Study";
        p.category = "Found Sound";
        auto& s = p.state;
        s.releaseTimeMs = 180.0f;
        s.inharmonicityAmount = 0.35f;
        s.harmonicLevelPlain = 1.1f;
        s.residualLevelPlain = 1.1f;
        s.transientEmphasisPlain = 0.9f;
        s.brightnessPlain = 0.1f;
        s.warmth = 0.3f;
        s.stability = 0.25f;
        s.entropy = 0.15f;
        s.exciterType = kExciterImpact;
        s.physModelMix = 0.55f;
        s.resonanceDecay = normalizeResDecay(0.35f);
        s.resonanceBrightness = 0.55f;
        s.resonanceStretch = 0.2f;
        s.resonanceScatter = 0.2f;
        s.impactHardness = 0.55f;
        s.impactMass = 0.4f;
        s.impactBrightness = 0.5f;
        s.impactPosition = 0.3f;
        s.bodySize = 0.45f;
        s.bodyMaterial = 0.45f;
        s.bodyMix = 0.4f;
        s.sympatheticAmount = 0.2f;
        s.sympatheticDecay = 0.45f;
        s.stereoSpread = 0.35f;
        s.voiceMode = kVoice4;
        s.feedbackAmount = 0.15f;
        s.feedbackDecay = 0.3f;
        s.adsrAttackMs = 1.0f;
        s.adsrDecayMs = 400.0f;
        s.adsrSustainLevel = 0.1f;
        s.adsrReleaseMs = 200.0f;
        s.adsrAmount = 0.9f;
        s.adsrDecayCurve = -0.4f;
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
              << " Innexus factory presets (v2)..." << std::endl;

    for (const auto& preset : presets) {
        auto stateData = preset.state.serialize();

        auto categoryDir = outputDir / preset.category;
        std::filesystem::create_directories(categoryDir);

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
