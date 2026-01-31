// ==============================================================================
// Factory Preset Generator for Disrumpo
// ==============================================================================
// Generates .vstpreset files matching the Processor::getState() v8 binary format.
// Run this tool once during development to create factory presets.
//
// Reference: plugins/disrumpo/src/processor/processor.cpp getState() (lines 441-712)
// Reference: tools/preset_generator.cpp (Iterum pattern)
// ==============================================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>

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

    void writeInt8(int8_t val) {
        data.push_back(static_cast<uint8_t>(val));
    }
};

// ==============================================================================
// Constants (must match plugin_ids.h and DSP headers)
// ==============================================================================

static constexpr int32_t kPresetVersion = 8;
static constexpr int kMaxBands = 4;
static constexpr int kMaxMorphNodes = 4;
static constexpr size_t kMaxMacros = 4;
static constexpr size_t kMaxModRoutings = 32;

// Distortion types (from distortion_types.h)
enum class DistortionType : uint8_t {
    SoftClip = 0, HardClip, Tube, Tape, Fuzz, AsymmetricFuzz,
    SineFold, TriangleFold, SergeFold,
    FullRectify, HalfRectify,
    Bitcrush, SampleReduce, Quantize, Aliasing, BitwiseMangler,
    Temporal,
    RingSaturation, FeedbackDist, AllpassResonant,
    Chaos, Formant, Granular, Spectral, Fractal, Stochastic
};

// Morph modes
enum class MorphMode : uint8_t { Linear1D = 0, Planar2D, Radial2D };

// Sweep falloff
enum class SweepFalloff : uint8_t { Sharp = 0, Smooth = 1 };

// Morph link modes
enum class MorphLinkMode : uint8_t {
    None = 0, SweepFreq, InverseSweep, EaseIn, EaseOut, HoldRise, Stepped, Custom
};

// Modulation sources
enum class ModSource : uint8_t {
    None = 0, LFO1, LFO2, EnvFollower, Random, Chaos,
    SampleHold, PitchFollower, Transient, Macro1, Macro2, Macro3, Macro4
};

// Modulation curves
enum class ModCurve : uint8_t { Linear = 0, Exponential, SCurve, Stepped };

// LFO waveforms
enum class Waveform : uint8_t { Sine = 0, Triangle, Saw, Square, SampleAndHold, SmoothRandom };

// Chaos models
enum class ChaosModel : uint8_t { Lorenz = 0, Rossler, Chua, Henon };

// Envelope follower source
enum class EnvFollowerSource : uint8_t { InputL = 0, InputR, Sum, Mid, Side };

// Sample & Hold source
enum class SHSource : uint8_t { Random = 0, LFO1, LFO2, External };

// ==============================================================================
// Preset State Structs
// ==============================================================================

struct BandState {
    float gainDb = 0.0f;    // [-24, +24] dB
    float pan = 0.0f;       // [-1, +1]
    bool solo = false;
    bool bypass = false;
    bool mute = false;
};

struct MorphNodeState {
    DistortionType type = DistortionType::SoftClip;
    float drive = 1.0f;     // [0, 10]
    float mix = 1.0f;       // [0, 1]
    float toneHz = 4000.0f; // [200, 8000]
    float bias = 0.0f;      // [-1, +1]
    float folds = 1.0f;     // [1, 12]
    float bitDepth = 16.0f; // [4, 24]
};

struct BandMorphState {
    float morphX = 0.5f;
    float morphY = 0.5f;
    MorphMode morphMode = MorphMode::Linear1D;
    int activeNodeCount = 2;
    float morphSmoothing = 0.0f;
    std::array<MorphNodeState, kMaxMorphNodes> nodes;
};

struct SweepState {
    bool enabled = false;
    float freqNorm = 0.566f;      // normalized [0,1] log frequency
    float widthNorm = 0.286f;     // normalized [0,1] -> [0.5, 4.0] octaves
    float intensityNorm = 0.25f;  // normalized [0,1] -> [0, 2.0]
    SweepFalloff falloff = SweepFalloff::Smooth;
    MorphLinkMode morphLink = MorphLinkMode::None;

    // LFO
    bool lfoEnabled = false;
    float lfoRateNorm = 0.606f;
    Waveform lfoWaveform = Waveform::Sine;
    float lfoDepth = 0.0f;
    bool lfoSync = false;
    int8_t lfoNoteIndex = 0;

    // Envelope
    bool envEnabled = false;
    float envAttackNorm = 0.091f;
    float envReleaseNorm = 0.184f;
    float envSensitivity = 0.5f;

    // Custom curve breakpoints
    int32_t curvePointCount = 2;
    float curvePoints[16] = {0.0f, 0.0f, 1.0f, 1.0f}; // pairs of (x,y)
};

struct ModSourceState {
    // LFO 1
    float lfo1RateNorm = 0.5f;
    Waveform lfo1Shape = Waveform::Sine;
    float lfo1PhaseNorm = 0.0f;
    bool lfo1Sync = false;
    int8_t lfo1NoteIndex = 0;
    bool lfo1Unipolar = false;
    bool lfo1Retrigger = true;

    // LFO 2
    float lfo2RateNorm = 0.5f;
    Waveform lfo2Shape = Waveform::Sine;
    float lfo2PhaseNorm = 0.0f;
    bool lfo2Sync = false;
    int8_t lfo2NoteIndex = 0;
    bool lfo2Unipolar = false;
    bool lfo2Retrigger = true;

    // Envelope Follower
    float envAttackNorm = 0.0f;
    float envReleaseNorm = 0.0f;
    float envSensitivity = 0.5f;
    EnvFollowerSource envSource = EnvFollowerSource::InputL;

    // Random
    float randomRateNorm = 0.0f;
    float randomSmoothness = 0.0f;
    bool randomSync = false;

    // Chaos
    ChaosModel chaosModel = ChaosModel::Lorenz;
    float chaosSpeedNorm = 0.0f;
    float chaosCoupling = 0.0f;

    // Sample & Hold
    SHSource shSource = SHSource::Random;
    float shRateNorm = 0.0f;
    float shSlewNorm = 0.0f;

    // Pitch Follower
    float pitchMinNorm = 0.0f;
    float pitchMaxNorm = 0.0f;
    float pitchConfidence = 0.5f;
    float pitchTrackNorm = 0.0f;

    // Transient
    float transSensitivity = 0.5f;
    float transAttackNorm = 0.0f;
    float transDecayNorm = 0.0f;
};

struct MacroState {
    float value = 0.0f;
    float minOutput = 0.0f;
    float maxOutput = 1.0f;
    ModCurve curve = ModCurve::Linear;
};

struct ModRouting {
    ModSource source = ModSource::None;
    int32_t destParamId = 0;
    float amount = 0.0f;
    ModCurve curve = ModCurve::Linear;
};

// ==============================================================================
// Complete Disrumpo Preset State (v8 format)
// ==============================================================================

struct DisrumpoPresetState {
    // Global (v1+)
    float inputGain = 0.5f;   // normalized [0,1], 0.5 = 0dB
    float outputGain = 0.5f;
    float globalMix = 1.0f;

    // Bands (v2+)
    int32_t bandCount = 4;
    std::array<BandState, kMaxBands> bands{};
    std::array<float, kMaxBands - 1> crossoverFreqs = {
        200.0f, 1500.0f, 6000.0f
    };

    // Sweep (v4+)
    SweepState sweep;

    // Modulation (v5+)
    ModSourceState modSources;
    std::array<MacroState, kMaxMacros> macros{};
    std::array<ModRouting, kMaxModRoutings> routings{};

    // Morph (v6+)
    std::array<BandMorphState, kMaxBands> bandMorph{};

    // Serialize to binary matching getState() format exactly
    std::vector<uint8_t> serialize() const {
        BinaryWriter w;

        // Version
        w.writeInt32(kPresetVersion);

        // Global params (v1+)
        w.writeFloat(inputGain);
        w.writeFloat(outputGain);
        w.writeFloat(globalMix);

        // Band count (v2+)
        w.writeInt32(bandCount);

        // Per-band state (4 bands always)
        for (int b = 0; b < kMaxBands; ++b) {
            const auto& bs = bands[static_cast<size_t>(b)];
            w.writeFloat(bs.gainDb);
            w.writeFloat(bs.pan);
            w.writeInt8(static_cast<int8_t>(bs.solo ? 1 : 0));
            w.writeInt8(static_cast<int8_t>(bs.bypass ? 1 : 0));
            w.writeInt8(static_cast<int8_t>(bs.mute ? 1 : 0));
        }

        // Crossover frequencies (3 floats)
        for (int c = 0; c < kMaxBands - 1; ++c) {
            w.writeFloat(crossoverFreqs[static_cast<size_t>(c)]);
        }

        // === Sweep System (v4+) ===

        // Sweep Core (6 values)
        w.writeInt8(static_cast<int8_t>(sweep.enabled ? 1 : 0));
        w.writeFloat(sweep.freqNorm);
        w.writeFloat(sweep.widthNorm);
        w.writeFloat(sweep.intensityNorm);
        w.writeInt8(static_cast<int8_t>(sweep.falloff));
        w.writeInt8(static_cast<int8_t>(sweep.morphLink));

        // LFO (6 values)
        w.writeInt8(static_cast<int8_t>(sweep.lfoEnabled ? 1 : 0));
        w.writeFloat(sweep.lfoRateNorm);
        w.writeInt8(static_cast<int8_t>(sweep.lfoWaveform));
        w.writeFloat(sweep.lfoDepth);
        w.writeInt8(static_cast<int8_t>(sweep.lfoSync ? 1 : 0));
        w.writeInt8(sweep.lfoNoteIndex);

        // Envelope (4 values)
        w.writeInt8(static_cast<int8_t>(sweep.envEnabled ? 1 : 0));
        w.writeFloat(sweep.envAttackNorm);
        w.writeFloat(sweep.envReleaseNorm);
        w.writeFloat(sweep.envSensitivity);

        // Custom Curve
        w.writeInt32(sweep.curvePointCount);
        for (int32_t i = 0; i < sweep.curvePointCount; ++i) {
            w.writeFloat(sweep.curvePoints[static_cast<size_t>(i * 2)]);     // x
            w.writeFloat(sweep.curvePoints[static_cast<size_t>(i * 2 + 1)]); // y
        }

        // === Modulation System (v5+) ===

        // LFO 1 (7 values)
        w.writeFloat(modSources.lfo1RateNorm);
        w.writeInt8(static_cast<int8_t>(modSources.lfo1Shape));
        w.writeFloat(modSources.lfo1PhaseNorm);
        w.writeInt8(static_cast<int8_t>(modSources.lfo1Sync ? 1 : 0));
        w.writeInt8(modSources.lfo1NoteIndex);
        w.writeInt8(static_cast<int8_t>(modSources.lfo1Unipolar ? 1 : 0));
        w.writeInt8(static_cast<int8_t>(modSources.lfo1Retrigger ? 1 : 0));

        // LFO 2 (7 values)
        w.writeFloat(modSources.lfo2RateNorm);
        w.writeInt8(static_cast<int8_t>(modSources.lfo2Shape));
        w.writeFloat(modSources.lfo2PhaseNorm);
        w.writeInt8(static_cast<int8_t>(modSources.lfo2Sync ? 1 : 0));
        w.writeInt8(modSources.lfo2NoteIndex);
        w.writeInt8(static_cast<int8_t>(modSources.lfo2Unipolar ? 1 : 0));
        w.writeInt8(static_cast<int8_t>(modSources.lfo2Retrigger ? 1 : 0));

        // Envelope Follower (4 values)
        w.writeFloat(modSources.envAttackNorm);
        w.writeFloat(modSources.envReleaseNorm);
        w.writeFloat(modSources.envSensitivity);
        w.writeInt8(static_cast<int8_t>(modSources.envSource));

        // Random (3 values)
        w.writeFloat(modSources.randomRateNorm);
        w.writeFloat(modSources.randomSmoothness);
        w.writeInt8(static_cast<int8_t>(modSources.randomSync ? 1 : 0));

        // Chaos (3 values)
        w.writeInt8(static_cast<int8_t>(modSources.chaosModel));
        w.writeFloat(modSources.chaosSpeedNorm);
        w.writeFloat(modSources.chaosCoupling);

        // Sample & Hold (3 values)
        w.writeInt8(static_cast<int8_t>(modSources.shSource));
        w.writeFloat(modSources.shRateNorm);
        w.writeFloat(modSources.shSlewNorm);

        // Pitch Follower (4 values)
        w.writeFloat(modSources.pitchMinNorm);
        w.writeFloat(modSources.pitchMaxNorm);
        w.writeFloat(modSources.pitchConfidence);
        w.writeFloat(modSources.pitchTrackNorm);

        // Transient (3 values)
        w.writeFloat(modSources.transSensitivity);
        w.writeFloat(modSources.transAttackNorm);
        w.writeFloat(modSources.transDecayNorm);

        // Macros (4 x 4 = 16 values)
        for (size_t m = 0; m < kMaxMacros; ++m) {
            w.writeFloat(macros[m].value);
            w.writeFloat(macros[m].minOutput);
            w.writeFloat(macros[m].maxOutput);
            w.writeInt8(static_cast<int8_t>(macros[m].curve));
        }

        // Routings (32 x 4 values)
        for (size_t r = 0; r < kMaxModRoutings; ++r) {
            w.writeInt8(static_cast<int8_t>(routings[r].source));
            w.writeInt32(routings[r].destParamId);
            w.writeFloat(routings[r].amount);
            w.writeInt8(static_cast<int8_t>(routings[r].curve));
        }

        // === Morph Node State (v6+) ===
        for (int b = 0; b < kMaxBands; ++b) {
            const auto& bm = bandMorph[static_cast<size_t>(b)];

            w.writeFloat(bm.morphX);
            w.writeFloat(bm.morphY);
            w.writeInt8(static_cast<int8_t>(bm.morphMode));
            w.writeInt8(static_cast<int8_t>(bm.activeNodeCount));
            w.writeFloat(bm.morphSmoothing);

            for (int n = 0; n < kMaxMorphNodes; ++n) {
                const auto& mn = bm.nodes[static_cast<size_t>(n)];
                w.writeInt8(static_cast<int8_t>(mn.type));
                w.writeFloat(mn.drive);
                w.writeFloat(mn.mix);
                w.writeFloat(mn.toneHz);
                w.writeFloat(mn.bias);
                w.writeFloat(mn.folds);
                w.writeFloat(mn.bitDepth);
            }
        }

        return w.data;
    }
};

// ==============================================================================
// VST3 Preset File Writer
// ==============================================================================
// FUID(0xA1B2C3D4, 0xE5F67890, 0x12345678, 0x9ABCDEF0)
// As 32 ASCII hex chars (each uint32 -> 8 hex chars):

const char kClassIdAscii[33] = "A1B2C3D4E5F6789012345678" "9ABCDEF0";

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
    DisrumpoPresetState state;
};

// ==============================================================================
// Helper: Create default state with specific band count
// ==============================================================================

DisrumpoPresetState makeInitState(int numBands) {
    DisrumpoPresetState s;
    s.bandCount = numBands;
    // All defaults: SoftClip, drive 1.0, mix 1.0, no sweep, no modulation
    // This is effectively bypass-equivalent (SoftClip at drive=1.0 is near-unity)
    return s;
}

// ==============================================================================
// Helper: Set band 0 node types for simple presets
// ==============================================================================

void setBand0NodeType(DisrumpoPresetState& s, DistortionType type, float drive = 1.0f) {
    for (int n = 0; n < kMaxMorphNodes; ++n) {
        s.bandMorph[0].nodes[static_cast<size_t>(n)].type = type;
        s.bandMorph[0].nodes[static_cast<size_t>(n)].drive = drive;
    }
}

void setAllBandsNodeType(DisrumpoPresetState& s, DistortionType type, float drive = 1.0f) {
    for (int b = 0; b < kMaxBands; ++b) {
        for (int n = 0; n < kMaxMorphNodes; ++n) {
            s.bandMorph[static_cast<size_t>(b)].nodes[static_cast<size_t>(n)].type = type;
            s.bandMorph[static_cast<size_t>(b)].nodes[static_cast<size_t>(n)].drive = drive;
        }
    }
}

// Helper: set morph node A and B to different types (for morphing presets)
void setMorphAB(BandMorphState& bm, DistortionType typeA, DistortionType typeB,
                float driveA = 1.0f, float driveB = 1.0f) {
    bm.nodes[0].type = typeA;
    bm.nodes[0].drive = driveA;
    bm.nodes[1].type = typeB;
    bm.nodes[1].drive = driveB;
    bm.activeNodeCount = 2;
    bm.morphMode = MorphMode::Linear1D;
}

// Helper: set 4-node morph configuration
void setMorph4Node(BandMorphState& bm,
                   DistortionType a, DistortionType b,
                   DistortionType c, DistortionType d,
                   MorphMode mode = MorphMode::Planar2D) {
    bm.nodes[0].type = a;
    bm.nodes[1].type = b;
    bm.nodes[2].type = c;
    bm.nodes[3].type = d;
    bm.activeNodeCount = 4;
    bm.morphMode = mode;
}

// Helper: enable sweep with given frequency and width
void enableSweep(DisrumpoPresetState& s, float freqNorm, float widthNorm,
                 float intensityNorm, MorphLinkMode link = MorphLinkMode::None) {
    s.sweep.enabled = true;
    s.sweep.freqNorm = freqNorm;
    s.sweep.widthNorm = widthNorm;
    s.sweep.intensityNorm = intensityNorm;
    s.sweep.morphLink = link;
}

// Helper: enable sweep LFO
void enableSweepLFO(DisrumpoPresetState& s, float rateNorm, float depth,
                    Waveform wave = Waveform::Sine) {
    s.sweep.lfoEnabled = true;
    s.sweep.lfoRateNorm = rateNorm;
    s.sweep.lfoDepth = depth;
    s.sweep.lfoWaveform = wave;
}

// Helper: add a modulation routing
void addRouting(DisrumpoPresetState& s, size_t slot, ModSource src,
                int32_t dest, float amount, ModCurve curve = ModCurve::Linear) {
    if (slot < kMaxModRoutings) {
        s.routings[slot].source = src;
        s.routings[slot].destParamId = dest;
        s.routings[slot].amount = amount;
        s.routings[slot].curve = curve;
    }
}

// Modulation destination indices (matching plugin_ids.h ModDest namespace)
namespace ModDest {
    constexpr int32_t kInputGain = 0;
    constexpr int32_t kOutputGain = 1;
    constexpr int32_t kGlobalMix = 2;
    constexpr int32_t kSweepFrequency = 3;
    constexpr int32_t kSweepWidth = 4;
    constexpr int32_t kSweepIntensity = 5;
    constexpr int32_t kBandBase = 6;
    constexpr int32_t kParamsPerBand = 6;
    // Per band: +0=MorphX, +1=MorphY, +2=Drive, +3=Mix, +4=BandGain, +5=BandPan
    constexpr int32_t bandMorphX(int band) { return kBandBase + band * kParamsPerBand + 0; }
    constexpr int32_t bandMorphY(int band) { return kBandBase + band * kParamsPerBand + 1; }
    constexpr int32_t bandDrive(int band)  { return kBandBase + band * kParamsPerBand + 2; }
    constexpr int32_t bandMix(int band)    { return kBandBase + band * kParamsPerBand + 3; }
    constexpr int32_t bandGain(int band)   { return kBandBase + band * kParamsPerBand + 4; }
    constexpr int32_t bandPan(int band)    { return kBandBase + band * kParamsPerBand + 5; }
}

// ==============================================================================
// Preset Definitions - 119 total across 11 categories
// ==============================================================================

std::vector<PresetDef> createAllPresets() {
    std::vector<PresetDef> presets;

    // =========================================================================
    // INIT (4 presets) - Clean starting points
    // =========================================================================
    {
        PresetDef p; p.name = "Init 1 Band"; p.category = "Init";
        p.state = makeInitState(1);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Init 2 Bands"; p.category = "Init";
        p.state = makeInitState(2);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Init 3 Bands"; p.category = "Init";
        p.state = makeInitState(3);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Init 4 Bands"; p.category = "Init";
        p.state = makeInitState(4);
        presets.push_back(p);
    }

    // =========================================================================
    // SWEEP (15 presets) - Sweep system showcases
    // =========================================================================
    {
        PresetDef p; p.name = "Frequency Hunter"; p.category = "Sweep";
        p.state = makeInitState(3);
        enableSweep(p.state, 0.5f, 0.4f, 0.5f);
        setAllBandsNodeType(p.state, DistortionType::SoftClip, 3.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Slow Scan"; p.category = "Sweep";
        p.state = makeInitState(4);
        enableSweep(p.state, 0.3f, 0.6f, 0.4f);
        enableSweepLFO(p.state, 0.2f, 0.7f, Waveform::Sine);
        setAllBandsNodeType(p.state, DistortionType::Tube, 2.5f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Wah Wah Drive"; p.category = "Sweep";
        p.state = makeInitState(2);
        enableSweep(p.state, 0.4f, 0.3f, 0.7f, MorphLinkMode::SweepFreq);
        enableSweepLFO(p.state, 0.5f, 0.8f, Waveform::Triangle);
        setMorphAB(p.state.bandMorph[0], DistortionType::Tube, DistortionType::Fuzz, 3.0f, 4.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Resonant Sweep"; p.category = "Sweep";
        p.state = makeInitState(3);
        enableSweep(p.state, 0.6f, 0.2f, 0.8f);
        setAllBandsNodeType(p.state, DistortionType::AllpassResonant, 2.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Envelope Follower"; p.category = "Sweep";
        p.state = makeInitState(3);
        enableSweep(p.state, 0.5f, 0.5f, 0.6f);
        p.state.sweep.envEnabled = true;
        p.state.sweep.envAttackNorm = 0.05f;
        p.state.sweep.envReleaseNorm = 0.3f;
        p.state.sweep.envSensitivity = 0.7f;
        setAllBandsNodeType(p.state, DistortionType::Tape, 2.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Triangle Wobble"; p.category = "Sweep";
        p.state = makeInitState(4);
        enableSweep(p.state, 0.45f, 0.35f, 0.5f);
        enableSweepLFO(p.state, 0.65f, 0.6f, Waveform::Triangle);
        setAllBandsNodeType(p.state, DistortionType::SoftClip, 2.5f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Sharp Focus"; p.category = "Sweep";
        p.state = makeInitState(3);
        enableSweep(p.state, 0.7f, 0.15f, 0.9f);
        p.state.sweep.falloff = SweepFalloff::Sharp;
        setAllBandsNodeType(p.state, DistortionType::HardClip, 3.5f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Wide Sweep"; p.category = "Sweep";
        p.state = makeInitState(4);
        enableSweep(p.state, 0.5f, 0.8f, 0.3f);
        enableSweepLFO(p.state, 0.3f, 0.5f, Waveform::Saw);
        setAllBandsNodeType(p.state, DistortionType::Tape, 1.5f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Inverse Link"; p.category = "Sweep";
        p.state = makeInitState(3);
        enableSweep(p.state, 0.5f, 0.4f, 0.6f, MorphLinkMode::InverseSweep);
        enableSweepLFO(p.state, 0.4f, 0.7f, Waveform::Sine);
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::HardClip, 2.0f, 4.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Step Scanner"; p.category = "Sweep";
        p.state = makeInitState(4);
        enableSweep(p.state, 0.4f, 0.3f, 0.7f, MorphLinkMode::Stepped);
        enableSweepLFO(p.state, 0.35f, 0.6f, Waveform::Saw);
        setAllBandsNodeType(p.state, DistortionType::SineFold, 2.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Low Rumble"; p.category = "Sweep";
        p.state = makeInitState(3);
        enableSweep(p.state, 0.2f, 0.5f, 0.6f);
        enableSweepLFO(p.state, 0.15f, 0.8f, Waveform::Sine);
        setAllBandsNodeType(p.state, DistortionType::Tube, 3.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "High Sweep"; p.category = "Sweep";
        p.state = makeInitState(3);
        enableSweep(p.state, 0.8f, 0.3f, 0.5f);
        enableSweepLFO(p.state, 0.7f, 0.5f, Waveform::Triangle);
        setAllBandsNodeType(p.state, DistortionType::Fuzz, 2.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Ease In Morph"; p.category = "Sweep";
        p.state = makeInitState(2);
        enableSweep(p.state, 0.5f, 0.4f, 0.5f, MorphLinkMode::EaseIn);
        enableSweepLFO(p.state, 0.3f, 0.6f, Waveform::Sine);
        setMorphAB(p.state.bandMorph[0], DistortionType::Tape, DistortionType::Fuzz, 2.0f, 3.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Random Scan"; p.category = "Sweep";
        p.state = makeInitState(4);
        enableSweep(p.state, 0.5f, 0.5f, 0.4f);
        enableSweepLFO(p.state, 0.5f, 0.7f, Waveform::SampleAndHold);
        setAllBandsNodeType(p.state, DistortionType::Bitcrush, 1.5f);
        p.state.bandMorph[0].nodes[0].bitDepth = 8.0f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Synced Pulse"; p.category = "Sweep";
        p.state = makeInitState(3);
        enableSweep(p.state, 0.5f, 0.3f, 0.6f);
        p.state.sweep.lfoEnabled = true;
        p.state.sweep.lfoSync = true;
        p.state.sweep.lfoNoteIndex = 6; // 1/8 note
        p.state.sweep.lfoDepth = 0.7f;
        p.state.sweep.lfoWaveform = Waveform::Square;
        setAllBandsNodeType(p.state, DistortionType::HardClip, 3.0f);
        presets.push_back(p);
    }

    // =========================================================================
    // MORPH (15 presets) - Morph system showcases
    // =========================================================================
    {
        PresetDef p; p.name = "Soft to Hard"; p.category = "Morph";
        p.state = makeInitState(2);
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::HardClip, 2.0f, 4.0f);
        p.state.bandMorph[0].morphX = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Tube to Fuzz"; p.category = "Morph";
        p.state = makeInitState(3);
        setMorphAB(p.state.bandMorph[0], DistortionType::Tube, DistortionType::Fuzz, 3.0f, 5.0f);
        p.state.bandMorph[0].morphX = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Four Corners"; p.category = "Morph";
        p.state = makeInitState(2);
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::SoftClip, DistortionType::HardClip,
            DistortionType::Tube, DistortionType::Fuzz);
        p.state.bandMorph[0].morphX = 0.5f;
        p.state.bandMorph[0].morphY = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Radial Blend"; p.category = "Morph";
        p.state = makeInitState(3);
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::Tape, DistortionType::SineFold,
            DistortionType::Bitcrush, DistortionType::Chaos, MorphMode::Radial2D);
        p.state.bandMorph[0].morphX = 0.5f;
        p.state.bandMorph[0].morphY = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Tape Fold Morph"; p.category = "Morph";
        p.state = makeInitState(2);
        setMorphAB(p.state.bandMorph[0], DistortionType::Tape, DistortionType::SineFold, 2.0f, 2.5f);
        p.state.bandMorph[0].morphSmoothing = 50.0f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Digital Organic"; p.category = "Morph";
        p.state = makeInitState(3);
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::Bitcrush, DistortionType::SoftClip,
            DistortionType::SampleReduce, DistortionType::Tube);
        p.state.bandMorph[0].nodes[0].bitDepth = 8.0f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Chaos to Order"; p.category = "Morph";
        p.state = makeInitState(2);
        setMorphAB(p.state.bandMorph[0], DistortionType::Chaos, DistortionType::SoftClip, 2.0f, 1.0f);
        p.state.bandMorph[0].morphX = 0.7f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Three Node Blend"; p.category = "Morph";
        p.state = makeInitState(3);
        p.state.bandMorph[0].nodes[0].type = DistortionType::Tube;
        p.state.bandMorph[0].nodes[0].drive = 2.0f;
        p.state.bandMorph[0].nodes[1].type = DistortionType::Fuzz;
        p.state.bandMorph[0].nodes[1].drive = 4.0f;
        p.state.bandMorph[0].nodes[2].type = DistortionType::SineFold;
        p.state.bandMorph[0].nodes[2].drive = 3.0f;
        p.state.bandMorph[0].activeNodeCount = 3;
        p.state.bandMorph[0].morphMode = MorphMode::Planar2D;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Rectify Blend"; p.category = "Morph";
        p.state = makeInitState(2);
        setMorphAB(p.state.bandMorph[0], DistortionType::FullRectify, DistortionType::HalfRectify, 1.0f, 1.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Feedback Explorer"; p.category = "Morph";
        p.state = makeInitState(3);
        setMorphAB(p.state.bandMorph[0], DistortionType::FeedbackDist, DistortionType::AllpassResonant, 2.0f, 2.5f);
        p.state.bandMorph[0].morphSmoothing = 100.0f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Multi Band Morph"; p.category = "Morph";
        p.state = makeInitState(4);
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::Tube, 2.0f, 3.0f);
        setMorphAB(p.state.bandMorph[1], DistortionType::Fuzz, DistortionType::SineFold, 3.0f, 2.0f);
        setMorphAB(p.state.bandMorph[2], DistortionType::Tape, DistortionType::HardClip, 1.5f, 4.0f);
        setMorphAB(p.state.bandMorph[3], DistortionType::Bitcrush, DistortionType::Chaos, 1.0f, 2.0f);
        p.state.bandMorph[3].nodes[0].bitDepth = 10.0f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Smooth Crossfade"; p.category = "Morph";
        p.state = makeInitState(2);
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::Tape, 1.5f, 2.0f);
        p.state.bandMorph[0].morphSmoothing = 200.0f;
        p.state.bandMorph[0].morphX = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Wavefolder Quad"; p.category = "Morph";
        p.state = makeInitState(2);
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::SineFold, DistortionType::TriangleFold,
            DistortionType::SergeFold, DistortionType::SoftClip);
        for (int n = 0; n < 3; ++n)
            p.state.bandMorph[0].nodes[static_cast<size_t>(n)].folds = 3.0f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Spectral Morph"; p.category = "Morph";
        p.state = makeInitState(3);
        setMorphAB(p.state.bandMorph[0], DistortionType::Spectral, DistortionType::Granular, 2.0f, 2.0f);
        p.state.bandMorph[0].morphMode = MorphMode::Linear1D;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Formant Shift"; p.category = "Morph";
        p.state = makeInitState(2);
        setMorphAB(p.state.bandMorph[0], DistortionType::Formant, DistortionType::AllpassResonant, 2.0f, 1.5f);
        presets.push_back(p);
    }

    // =========================================================================
    // BASS (10 presets) - Low-end optimized
    // =========================================================================
    {
        PresetDef p; p.name = "Warm Bass"; p.category = "Bass";
        p.state = makeInitState(2);
        p.state.crossoverFreqs[0] = 150.0f;
        setAllBandsNodeType(p.state, DistortionType::Tube, 2.5f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Subharmonic Growl"; p.category = "Bass";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 80.0f;
        p.state.crossoverFreqs[1] = 300.0f;
        setBand0NodeType(p.state, DistortionType::Fuzz, 4.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Tape Warmth"; p.category = "Bass";
        p.state = makeInitState(2);
        p.state.crossoverFreqs[0] = 200.0f;
        setAllBandsNodeType(p.state, DistortionType::Tape, 2.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Dirty Sub"; p.category = "Bass";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 60.0f;
        p.state.crossoverFreqs[1] = 250.0f;
        setBand0NodeType(p.state, DistortionType::AsymmetricFuzz, 3.5f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Wavefold Bass"; p.category = "Bass";
        p.state = makeInitState(2);
        p.state.crossoverFreqs[0] = 180.0f;
        setBand0NodeType(p.state, DistortionType::SineFold, 2.0f);
        p.state.bandMorph[0].nodes[0].folds = 2.0f;
        p.state.bandMorph[0].nodes[1].folds = 2.0f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Punchy Low End"; p.category = "Bass";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 100.0f;
        p.state.crossoverFreqs[1] = 400.0f;
        setBand0NodeType(p.state, DistortionType::HardClip, 3.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Soft Bass Drive"; p.category = "Bass";
        p.state = makeInitState(2);
        p.state.crossoverFreqs[0] = 200.0f;
        setAllBandsNodeType(p.state, DistortionType::SoftClip, 3.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Serge Bass"; p.category = "Bass";
        p.state = makeInitState(2);
        p.state.crossoverFreqs[0] = 150.0f;
        setBand0NodeType(p.state, DistortionType::SergeFold, 2.0f);
        p.state.bandMorph[0].nodes[0].folds = 2.0f;
        p.state.bandMorph[0].nodes[1].folds = 2.0f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Multi Band Bass"; p.category = "Bass";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 80.0f;
        p.state.crossoverFreqs[1] = 200.0f;
        p.state.crossoverFreqs[2] = 500.0f;
        for (int b = 0; b < kMaxBands; ++b) {
            for (int n = 0; n < kMaxMorphNodes; ++n) {
                p.state.bandMorph[static_cast<size_t>(b)].nodes[static_cast<size_t>(n)].type = DistortionType::Tube;
                p.state.bandMorph[static_cast<size_t>(b)].nodes[static_cast<size_t>(n)].drive = 1.5f + static_cast<float>(b) * 0.5f;
            }
        }
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Ring Bass"; p.category = "Bass";
        p.state = makeInitState(2);
        p.state.crossoverFreqs[0] = 200.0f;
        setBand0NodeType(p.state, DistortionType::RingSaturation, 2.0f);
        presets.push_back(p);
    }

    // =========================================================================
    // LEADS (10 presets) - Aggressive, cutting through
    // =========================================================================
    {
        PresetDef p; p.name = "Screaming Lead"; p.category = "Leads";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::HardClip, 5.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Fuzz Face"; p.category = "Leads";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::Fuzz, 6.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Feedback Scream"; p.category = "Leads";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::FeedbackDist, 4.0f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.3f);
        p.state.modSources.lfo1RateNorm = 0.6f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Chaos Lead"; p.category = "Leads";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::Chaos, 3.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Asymmetric Bite"; p.category = "Leads";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::AsymmetricFuzz, 5.0f);
        p.state.bandMorph[0].nodes[0].bias = 0.3f;
        p.state.bandMorph[0].nodes[1].bias = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Modulated Edge"; p.category = "Leads";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::HardClip, 4.0f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.5f);
        p.state.modSources.lfo1RateNorm = 0.5f;
        setMorphAB(p.state.bandMorph[0], DistortionType::HardClip, DistortionType::Fuzz, 4.0f, 5.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Ring Mod Lead"; p.category = "Leads";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::RingSaturation, 3.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Wavefold Lead"; p.category = "Leads";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::SineFold, 3.0f);
        for (int n = 0; n < kMaxMorphNodes; ++n) {
            p.state.bandMorph[0].nodes[static_cast<size_t>(n)].folds = 4.0f;
            p.state.bandMorph[1].nodes[static_cast<size_t>(n)].folds = 4.0f;
        }
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Temporal Edge"; p.category = "Leads";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::Temporal, 4.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Fractal Lead"; p.category = "Leads";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Fractal, 3.0f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.4f);
        presets.push_back(p);
    }

    // =========================================================================
    // PADS (10 presets) - Subtle, evolving
    // =========================================================================
    {
        PresetDef p; p.name = "Gentle Warmth"; p.category = "Pads";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::SoftClip, 1.5f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Evolving Texture"; p.category = "Pads";
        p.state = makeInitState(4);
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::Tape, 1.2f, 1.5f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.4f);
        p.state.modSources.lfo1RateNorm = 0.15f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Allpass Shimmer"; p.category = "Pads";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::AllpassResonant, 1.0f);
        enableSweep(p.state, 0.5f, 0.3f, 0.3f);
        enableSweepLFO(p.state, 0.1f, 0.4f, Waveform::Sine);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Tape Haze"; p.category = "Pads";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Tape, 1.2f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Slow Morph Pad"; p.category = "Pads";
        p.state = makeInitState(3);
        setMorphAB(p.state.bandMorph[0], DistortionType::Tube, DistortionType::AllpassResonant, 1.0f, 1.0f);
        p.state.bandMorph[0].morphSmoothing = 300.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.6f);
        p.state.modSources.lfo1RateNorm = 0.08f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Soft Sweep"; p.category = "Pads";
        p.state = makeInitState(4);
        setAllBandsNodeType(p.state, DistortionType::SoftClip, 1.0f);
        enableSweep(p.state, 0.4f, 0.5f, 0.2f);
        enableSweepLFO(p.state, 0.05f, 0.5f, Waveform::Sine);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Formant Pad"; p.category = "Pads";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Formant, 1.5f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Granular Wash"; p.category = "Pads";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Granular, 1.5f);
        enableSweep(p.state, 0.5f, 0.4f, 0.2f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Stochastic Drift"; p.category = "Pads";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Stochastic, 1.0f);
        addRouting(p.state, 0, ModSource::Random, ModDest::bandMorphX(0), 0.2f);
        p.state.modSources.randomRateNorm = 0.1f;
        p.state.modSources.randomSmoothness = 0.8f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Spectral Pad"; p.category = "Pads";
        p.state = makeInitState(4);
        setAllBandsNodeType(p.state, DistortionType::Spectral, 1.0f);
        presets.push_back(p);
    }

    // =========================================================================
    // DRUMS (10 presets) - Transient-friendly
    // =========================================================================
    {
        PresetDef p; p.name = "Punchy Clip"; p.category = "Drums";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::HardClip, 3.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Crushed Beats"; p.category = "Drums";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Bitcrush, 1.0f);
        for (int b = 0; b < kMaxBands; ++b)
            for (int n = 0; n < kMaxMorphNodes; ++n)
                p.state.bandMorph[static_cast<size_t>(b)].nodes[static_cast<size_t>(n)].bitDepth = 8.0f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Temporal Snap"; p.category = "Drums";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::Temporal, 3.0f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(0), 0.5f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Dirty Groove"; p.category = "Drums";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 100.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBand0NodeType(p.state, DistortionType::Fuzz, 3.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Snare Crack"; p.category = "Drums";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 5000.0f;
        setAllBandsNodeType(p.state, DistortionType::HardClip, 4.0f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(1), 0.6f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Kick Saturate"; p.category = "Drums";
        p.state = makeInitState(2);
        p.state.crossoverFreqs[0] = 150.0f;
        setBand0NodeType(p.state, DistortionType::Tape, 3.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Glitch Drum"; p.category = "Drums";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::SampleReduce, 1.0f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.4f);
        p.state.modSources.lfo1RateNorm = 0.8f;
        p.state.modSources.lfo1Shape = Waveform::SampleAndHold;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Transient Shape"; p.category = "Drums";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::SoftClip, 2.0f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandMix(0), 0.7f);
        p.state.modSources.transSensitivity = 0.7f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Rectified Drums"; p.category = "Drums";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::FullRectify, 1.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Aliased Drums"; p.category = "Drums";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::Aliasing, 2.0f);
        presets.push_back(p);
    }

    // =========================================================================
    // EXPERIMENTAL (15 presets) - Creative, unusual
    // =========================================================================
    {
        PresetDef p; p.name = "Spectral Scatter"; p.category = "Experimental";
        p.state = makeInitState(4);
        setAllBandsNodeType(p.state, DistortionType::Spectral, 2.5f);
        enableSweep(p.state, 0.5f, 0.6f, 0.5f);
        enableSweepLFO(p.state, 0.4f, 0.7f, Waveform::SampleAndHold);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Granular Crush"; p.category = "Experimental";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Granular, 3.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Fractal Noise"; p.category = "Experimental";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Fractal, 4.0f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(0), 0.5f);
        p.state.modSources.chaosSpeedNorm = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Stochastic Burst"; p.category = "Experimental";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Stochastic, 3.0f);
        addRouting(p.state, 0, ModSource::Random, ModDest::bandDrive(0), 0.6f);
        p.state.modSources.randomRateNorm = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Formant Shift"; p.category = "Experimental";
        p.state = makeInitState(4);
        setAllBandsNodeType(p.state, DistortionType::Formant, 2.5f);
        enableSweep(p.state, 0.4f, 0.3f, 0.6f, MorphLinkMode::SweepFreq);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Chaos Engine"; p.category = "Experimental";
        p.state = makeInitState(3);
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::Chaos, DistortionType::Fractal,
            DistortionType::Stochastic, DistortionType::FeedbackDist);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandMorphX(0), 0.7f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandMorphY(0), 0.7f);
        p.state.modSources.chaosSpeedNorm = 0.4f;
        p.state.modSources.chaosCoupling = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Bitwise Mangler"; p.category = "Experimental";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::BitwiseMangler, 2.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Ring Chaos"; p.category = "Experimental";
        p.state = makeInitState(3);
        setMorphAB(p.state.bandMorph[0], DistortionType::RingSaturation, DistortionType::Chaos, 3.0f, 2.0f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.6f);
        p.state.modSources.lfo1RateNorm = 0.4f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Quantize Glitch"; p.category = "Experimental";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Quantize, 2.0f);
        addRouting(p.state, 0, ModSource::SampleHold, ModDest::bandDrive(0), 0.5f);
        p.state.modSources.shRateNorm = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "All Types Morph"; p.category = "Experimental";
        p.state = makeInitState(4);
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::SoftClip, DistortionType::Fuzz,
            DistortionType::SineFold, DistortionType::Bitcrush);
        setMorph4Node(p.state.bandMorph[1],
            DistortionType::Chaos, DistortionType::Spectral,
            DistortionType::Formant, DistortionType::Granular);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Alien Voice"; p.category = "Experimental";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Formant, 3.0f);
        addRouting(p.state, 0, ModSource::PitchFollower, ModDest::bandDrive(0), 0.5f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Feedback Loop"; p.category = "Experimental";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::FeedbackDist, 5.0f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), -0.3f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Serge Madness"; p.category = "Experimental";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::SergeFold, 4.0f);
        for (int b = 0; b < kMaxBands; ++b)
            for (int n = 0; n < kMaxMorphNodes; ++n)
                p.state.bandMorph[static_cast<size_t>(b)].nodes[static_cast<size_t>(n)].folds = 6.0f;
        enableSweep(p.state, 0.5f, 0.5f, 0.7f);
        enableSweepLFO(p.state, 0.6f, 0.8f, Waveform::Saw);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Temporal Flux"; p.category = "Experimental";
        p.state = makeInitState(4);
        setAllBandsNodeType(p.state, DistortionType::Temporal, 3.0f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandDrive(1), 0.4f);
        p.state.modSources.lfo1RateNorm = 0.4f;
        p.state.modSources.lfo2RateNorm = 0.55f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Broken Radio"; p.category = "Experimental";
        p.state = makeInitState(3);
        setMorphAB(p.state.bandMorph[0], DistortionType::Aliasing, DistortionType::SampleReduce, 2.0f, 1.5f);
        addRouting(p.state, 0, ModSource::Random, ModDest::bandMorphX(0), 0.7f);
        p.state.modSources.randomRateNorm = 0.6f;
        presets.push_back(p);
    }

    // =========================================================================
    // CHAOS (10 presets) - Chaos model showcases
    // =========================================================================
    {
        PresetDef p; p.name = "Lorenz Drive"; p.category = "Chaos";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::SoftClip, 2.0f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(0), 0.5f);
        p.state.modSources.chaosModel = ChaosModel::Lorenz;
        p.state.modSources.chaosSpeedNorm = 0.3f;
        p.state.modSources.chaosCoupling = 0.4f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Rossler Morph"; p.category = "Chaos";
        p.state = makeInitState(3);
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::Fuzz, 2.0f, 4.0f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandMorphX(0), 0.6f);
        p.state.modSources.chaosModel = ChaosModel::Rossler;
        p.state.modSources.chaosSpeedNorm = 0.25f;
        p.state.modSources.chaosCoupling = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Chua Circuit"; p.category = "Chaos";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::Chaos, 3.0f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(0), 0.4f);
        p.state.modSources.chaosModel = ChaosModel::Chua;
        p.state.modSources.chaosSpeedNorm = 0.4f;
        p.state.modSources.chaosCoupling = 0.6f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Henon Map"; p.category = "Chaos";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::HardClip, 3.0f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandPan(0), 0.3f);
        p.state.modSources.chaosModel = ChaosModel::Henon;
        p.state.modSources.chaosSpeedNorm = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Chaotic Sweep"; p.category = "Chaos";
        p.state = makeInitState(4);
        setAllBandsNodeType(p.state, DistortionType::Tube, 2.0f);
        enableSweep(p.state, 0.5f, 0.4f, 0.5f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::kSweepFrequency, 0.4f);
        p.state.modSources.chaosModel = ChaosModel::Lorenz;
        p.state.modSources.chaosSpeedNorm = 0.2f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Coupled Chaos"; p.category = "Chaos";
        p.state = makeInitState(3);
        setMorphAB(p.state.bandMorph[0], DistortionType::Chaos, DistortionType::Fractal, 2.0f, 3.0f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandMorphX(0), 0.8f);
        p.state.modSources.chaosModel = ChaosModel::Lorenz;
        p.state.modSources.chaosSpeedNorm = 0.35f;
        p.state.modSources.chaosCoupling = 0.8f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Slow Chaos"; p.category = "Chaos";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::SoftClip, 1.5f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandMix(0), 0.3f);
        p.state.modSources.chaosModel = ChaosModel::Rossler;
        p.state.modSources.chaosSpeedNorm = 0.05f;
        p.state.modSources.chaosCoupling = 0.2f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Attractor Pan"; p.category = "Chaos";
        p.state = makeInitState(4);
        setAllBandsNodeType(p.state, DistortionType::Tape, 2.0f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandPan(0), 0.5f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandPan(1), -0.5f);
        p.state.modSources.chaosModel = ChaosModel::Lorenz;
        p.state.modSources.chaosSpeedNorm = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Chaos Morph 4"; p.category = "Chaos";
        p.state = makeInitState(2);
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::SoftClip, DistortionType::Fuzz,
            DistortionType::SineFold, DistortionType::Chaos);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandMorphX(0), 0.6f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandMorphY(0), 0.6f);
        p.state.modSources.chaosModel = ChaosModel::Chua;
        p.state.modSources.chaosSpeedNorm = 0.3f;
        p.state.modSources.chaosCoupling = 0.5f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Fast Chaos"; p.category = "Chaos";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::FeedbackDist, 3.0f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(0), 0.6f);
        p.state.modSources.chaosModel = ChaosModel::Henon;
        p.state.modSources.chaosSpeedNorm = 0.8f;
        p.state.modSources.chaosCoupling = 0.7f;
        presets.push_back(p);
    }

    // =========================================================================
    // DYNAMIC (10 presets) - Envelope/transient/pitch follower
    // =========================================================================
    {
        PresetDef p; p.name = "Touch Sensitive"; p.category = "Dynamic";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::SoftClip, 2.0f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.6f);
        p.state.modSources.envAttackNorm = 0.05f;
        p.state.modSources.envReleaseNorm = 0.2f;
        p.state.modSources.envSensitivity = 0.7f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Transient Punch"; p.category = "Dynamic";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::HardClip, 3.0f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(0), 0.7f);
        p.state.modSources.transSensitivity = 0.8f;
        p.state.modSources.transAttackNorm = 0.1f;
        p.state.modSources.transDecayNorm = 0.3f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Pitch Tracker"; p.category = "Dynamic";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Tube, 2.0f);
        addRouting(p.state, 0, ModSource::PitchFollower, ModDest::bandDrive(0), 0.4f);
        p.state.modSources.pitchMinNorm = 0.1f;
        p.state.modSources.pitchMaxNorm = 0.5f;
        p.state.modSources.pitchConfidence = 0.6f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Dynamic Mix"; p.category = "Dynamic";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Fuzz, 4.0f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandMix(0), -0.4f);
        p.state.modSources.envSensitivity = 0.8f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Envelope Morph"; p.category = "Dynamic";
        p.state = makeInitState(3);
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::HardClip, 1.5f, 4.0f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandMorphX(0), 0.7f);
        p.state.modSources.envAttackNorm = 0.02f;
        p.state.modSources.envReleaseNorm = 0.4f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Transient Gate"; p.category = "Dynamic";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::SoftClip, 2.5f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandMix(0), 0.8f);
        p.state.modSources.transSensitivity = 0.6f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Pitch Drive"; p.category = "Dynamic";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::Fuzz, 3.0f);
        addRouting(p.state, 0, ModSource::PitchFollower, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::PitchFollower, ModDest::kSweepFrequency, 0.3f);
        enableSweep(p.state, 0.5f, 0.3f, 0.4f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Velocity Response"; p.category = "Dynamic";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Tape, 2.0f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::kGlobalMix, 0.3f);
        p.state.modSources.envSensitivity = 0.6f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Multi Dynamic"; p.category = "Dynamic";
        p.state = makeInitState(4);
        setAllBandsNodeType(p.state, DistortionType::SoftClip, 2.0f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.4f);
        addRouting(p.state, 1, ModSource::Transient, ModDest::bandDrive(1), 0.5f);
        addRouting(p.state, 2, ModSource::PitchFollower, ModDest::bandDrive(2), 0.3f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Sidechain Pump"; p.category = "Dynamic";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::SoftClip, 3.0f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandMix(0), -0.6f);
        p.state.modSources.envAttackNorm = 0.01f;
        p.state.modSources.envReleaseNorm = 0.3f;
        p.state.modSources.envSensitivity = 0.9f;
        presets.push_back(p);
    }

    // =========================================================================
    // LO-FI (10 presets) - Digital degradation
    // =========================================================================
    {
        PresetDef p; p.name = "8-Bit Crunch"; p.category = "Lo-Fi";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::Bitcrush, 1.0f);
        for (int b = 0; b < kMaxBands; ++b)
            for (int n = 0; n < kMaxMorphNodes; ++n)
                p.state.bandMorph[static_cast<size_t>(b)].nodes[static_cast<size_t>(n)].bitDepth = 8.0f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Sample Rate Crush"; p.category = "Lo-Fi";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::SampleReduce, 1.5f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Quantize Dirt"; p.category = "Lo-Fi";
        p.state = makeInitState(3);
        setAllBandsNodeType(p.state, DistortionType::Quantize, 2.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Aliasing Harsh"; p.category = "Lo-Fi";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::Aliasing, 2.5f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Bit Mangler"; p.category = "Lo-Fi";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::BitwiseMangler, 2.0f);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "4-Bit Retro"; p.category = "Lo-Fi";
        p.state = makeInitState(2);
        setAllBandsNodeType(p.state, DistortionType::Bitcrush, 1.0f);
        for (int b = 0; b < kMaxBands; ++b)
            for (int n = 0; n < kMaxMorphNodes; ++n)
                p.state.bandMorph[static_cast<size_t>(b)].nodes[static_cast<size_t>(n)].bitDepth = 4.0f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Digital Decay"; p.category = "Lo-Fi";
        p.state = makeInitState(3);
        setMorphAB(p.state.bandMorph[0], DistortionType::Bitcrush, DistortionType::SampleReduce, 1.0f, 1.5f);
        p.state.bandMorph[0].nodes[0].bitDepth = 10.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.4f);
        p.state.modSources.lfo1RateNorm = 0.2f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Multi Band Lo-Fi"; p.category = "Lo-Fi";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 1000.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        // Different bit depths per band
        for (int n = 0; n < kMaxMorphNodes; ++n) {
            p.state.bandMorph[0].nodes[static_cast<size_t>(n)].type = DistortionType::Bitcrush;
            p.state.bandMorph[0].nodes[static_cast<size_t>(n)].bitDepth = 12.0f;
            p.state.bandMorph[1].nodes[static_cast<size_t>(n)].type = DistortionType::Bitcrush;
            p.state.bandMorph[1].nodes[static_cast<size_t>(n)].bitDepth = 8.0f;
            p.state.bandMorph[2].nodes[static_cast<size_t>(n)].type = DistortionType::Bitcrush;
            p.state.bandMorph[2].nodes[static_cast<size_t>(n)].bitDepth = 6.0f;
            p.state.bandMorph[3].nodes[static_cast<size_t>(n)].type = DistortionType::SampleReduce;
        }
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Glitch Box"; p.category = "Lo-Fi";
        p.state = makeInitState(2);
        setMorphAB(p.state.bandMorph[0], DistortionType::Aliasing, DistortionType::BitwiseMangler, 2.0f, 2.0f);
        addRouting(p.state, 0, ModSource::SampleHold, ModDest::bandMorphX(0), 0.7f);
        p.state.modSources.shRateNorm = 0.4f;
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Warm Lo-Fi"; p.category = "Lo-Fi";
        p.state = makeInitState(3);
        setMorphAB(p.state.bandMorph[0], DistortionType::Bitcrush, DistortionType::Tape, 1.0f, 1.5f);
        p.state.bandMorph[0].nodes[0].bitDepth = 12.0f;
        p.state.bandMorph[0].morphX = 0.4f;
        presets.push_back(p);
    }

    return presets;
}

// ==============================================================================
// Main
// ==============================================================================

int main(int argc, char* argv[]) {
    std::filesystem::path outputDir = "plugins/disrumpo/resources/presets";

    if (argc > 1) {
        outputDir = argv[1];
    }

    std::filesystem::create_directories(outputDir);

    auto presets = createAllPresets();
    int successCount = 0;

    std::cout << "Generating " << presets.size() << " Disrumpo factory presets..." << std::endl;

    // Verify expected count
    if (presets.size() != 119) {
        std::cerr << "WARNING: Expected 119 presets, got " << presets.size() << std::endl;
    }

    for (const auto& preset : presets) {
        auto state = preset.state.serialize();

        auto categoryDir = outputDir / preset.category;
        std::filesystem::create_directories(categoryDir);

        // Create filename: PresetName.vstpreset
        std::string filename;
        for (char c : preset.name) {
            if (c == ' ') filename += '_';
            else if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') filename += c;
        }
        filename += ".vstpreset";

        auto path = categoryDir / filename;

        if (writeVstPreset(path, state)) {
            std::cout << "  Created: " << preset.category << "/" << filename << std::endl;
            successCount++;
        }
    }

    std::cout << "\nGenerated " << successCount << " of " << presets.size() << " presets." << std::endl;
    std::cout << "Output directory: " << std::filesystem::absolute(outputDir) << std::endl;

    return (successCount == static_cast<int>(presets.size())) ? 0 : 1;
}
