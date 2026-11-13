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
#include <exception>
#include <iterator>
#include <utility>

namespace {

// ==============================================================================
// Binary Writer (matches IBStreamer little-endian format)
// ==============================================================================

class BinaryWriter {
public:
    std::vector<uint8_t> data;

    void writeInt32(int32_t val) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), bytes, bytes + 4);
    }

    void writeFloat(float val) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), bytes, bytes + 4);
    }

    void writeInt8(int8_t val) {
        data.push_back(static_cast<uint8_t>(val));
    }
};

// ==============================================================================
// Constants (must match plugin_ids.h and DSP headers)
// ==============================================================================

constexpr int32_t kPresetVersion = 10;
constexpr int kMaxBands = 4;
constexpr int kMaxMorphNodes = 4;
constexpr size_t kMaxMacros = 4;
constexpr size_t kMaxModRoutings = 32;
constexpr int kShapeSlotCount = 10;
constexpr int kDistortionTypeCount = 26;

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

// Modulation sources (must match Krate::DSP::ModSource in modulation_types.h)
enum class ModSource : uint8_t {
    None = 0, LFO1, LFO2, EnvFollower, Random,
    Macro1, Macro2, Macro3, Macro4,
    Chaos, Rungler, SampleHold, PitchFollower, Transient
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

    // v9: Shape parameter slots (normalized [0,1], default 0.5)
    // Semantics depend on distortion type (see mapShapeSlotsToParams in processor.cpp)
    float shapeSlots[kShapeSlotCount] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
                                          0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
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
    int8_t lfoNoteIndex = 10; // 1/8 (standard default, kNoteValueDropdownMapping index)

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
    int8_t lfo1NoteIndex = 10; // 1/8 (standard default)
    bool lfo1Unipolar = false;
    bool lfo1Retrigger = true;

    // LFO 2
    float lfo2RateNorm = 0.5f;
    Waveform lfo2Shape = Waveform::Sine;
    float lfo2PhaseNorm = 0.0f;
    bool lfo2Sync = false;
    int8_t lfo2NoteIndex = 10; // 1/8 (standard default)
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

                // v9: Shape parameter slots (10 floats)
                for (float slot : mn.shapeSlots) {
                    w.writeFloat(slot);
                }

                // v9: Per-type shadow storage (26 types × 10 slots)
                // Write default 0.5 for all types, except the active type
                // gets the current shapeSlots values (so they're preserved on load)
                int activeType = static_cast<int>(mn.type);
                for (int t = 0; t < kDistortionTypeCount; ++t) {
                    for (float slot : mn.shapeSlots) {
                        w.writeFloat(t == activeType ? slot : 0.5f);
                    }
                }
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

// The five XML entities, as PresetManager writes and reads them back (preset_manager.cpp)
std::string xmlEscapeAttribute(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c; break;
        }
    }
    return out;
}

// The Info chunk: the metadata PresetManager::savePreset writes. The preset browser reads its
// Comment attribute back as the description shown under the preset list (FR-065).
std::string buildInfoXml(const std::string& name, const std::string& category,
                         const std::string& description) {
    std::string xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<MetaInfo>\n";
    xml += "  <Attr id=\"MediaType\" value=\"VstPreset\" type=\"string\"/>\n";
    xml += "  <Attr id=\"PlugInName\" value=\"Disrumpo\" type=\"string\"/>\n";
    xml += "  <Attr id=\"PlugInCategory\" value=\"Distortion\" type=\"string\"/>\n";
    xml += R"(  <Attr id="Name" value=")" + xmlEscapeAttribute(name) + "\" type=\"string\"/>\n";
    xml += R"(  <Attr id="MusicalCategory" value=")" + xmlEscapeAttribute(category) + "\" type=\"string\"/>\n";
    xml += R"(  <Attr id="MusicalInstrument" value=")" + xmlEscapeAttribute(category) + "\" type=\"string\"/>\n";
    xml += R"(  <Attr id="Comment" value=")" + xmlEscapeAttribute(description) + "\" type=\"string\"/>\n";
    xml += "</MetaInfo>\n";
    return xml;
}

// Layout: header (48 B), Comp payload, Info payload, then
// "List", 2, {"Comp", offset, size}, {"Info", offset, size}.
bool writeVstPreset(const std::filesystem::path& path,
                    const std::vector<uint8_t>& componentState,
                    const std::string& infoXml) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "Failed to create: " << path << '\n';
        return false;
    }

    const int64_t kHeaderSize = 48;
    int64_t compDataOffset = kHeaderSize;
    int64_t compDataSize = static_cast<int64_t>(componentState.size());
    int64_t infoDataOffset = compDataOffset + compDataSize;
    int64_t infoDataSize = static_cast<int64_t>(infoXml.size());
    int64_t listOffset = infoDataOffset + infoDataSize;

    // Header
    f.write("VST3", 4);
    writeLE32(f, 1);
    f.write(kClassIdAscii, 32);
    writeLE64(f, listOffset);

    // Component State Data
    f.write(reinterpret_cast<const char*>(componentState.data()), compDataSize);

    // Meta Info (XML)
    f.write(infoXml.data(), infoDataSize);

    // Chunk List
    f.write("List", 4);
    writeLE32(f, 2);
    f.write("Comp", 4);
    writeLE64(f, compDataOffset);
    writeLE64(f, compDataSize);
    f.write("Info", 4);
    writeLE64(f, infoDataOffset);
    writeLE64(f, infoDataSize);

    f.close();
    return f.good();
}

// ==============================================================================
// Preset Definition
// ==============================================================================

struct PresetDef {
    std::string name;
    std::string category;
    // One line shown under the preset browser's list: what the preset is built around,
    // read from its settings below (band types, morph, sweep, modulation).
    std::string description;
    DisrumpoPresetState state;
};

// ==============================================================================
// Helper: Create default state with specific band count
// ==============================================================================

DisrumpoPresetState makeInitState(int numBands) {
    DisrumpoPresetState s;
    s.bandCount = numBands;
    return s;
}

// ==============================================================================
// Helpers: Node configuration
// ==============================================================================

// Configure a single node on a specific band
void setNode(MorphNodeState& node, DistortionType type, float drive,
             float toneHz = 4000.0f, float mix = 1.0f) {
    node.type = type;
    node.drive = drive;
    node.toneHz = toneHz;
    node.mix = mix;
}

// Set all nodes on a band to the same type (for non-morphing bands)
void setBandType(BandMorphState& bm, DistortionType type, float drive,
                 float toneHz = 4000.0f) {
    for (int n = 0; n < kMaxMorphNodes; ++n)
        setNode(bm.nodes[static_cast<size_t>(n)], type, drive, toneHz);
}

// Set the mix of every node on a band: the base a positive routing into the band Mix opens from
void setBandMix(BandMorphState& bm, float mix) {
    for (auto& node : bm.nodes) node.mix = mix;
}

// Set morph A/B with full control
void setMorphAB(BandMorphState& bm, DistortionType typeA, DistortionType typeB,
                float driveA, float driveB, float toneA = 4000.0f, float toneB = 4000.0f) {
    setNode(bm.nodes[0], typeA, driveA, toneA);
    setNode(bm.nodes[1], typeB, driveB, toneB);
    bm.activeNodeCount = 2;
    bm.morphMode = MorphMode::Linear1D;
}

// Set 4-node morph configuration
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

// ==============================================================================
// Helpers: Shape slot setters (normalized [0,1] values)
// Each function sets the shape slots AND the shadow storage for the active type.
// Slot mapping matches processor.cpp mapShapeSlotsToParams().
// ==============================================================================

void setShapeSlots(MorphNodeState& node, std::initializer_list<float> values) {
    int i = 0;
    for (float v : values) {
        if (i < kShapeSlotCount) node.shapeSlots[i++] = v;
    }
}

// Per-type convenience helpers (take denormalized values, convert to normalized slots)

// SoftClip: curve [0,1], knee [0,1]
void shapeSoftClip(MorphNodeState& n, float curve, float knee) {
    setShapeSlots(n, {curve, knee});
}

// HardClip: threshold [0,1], ceiling [0,1]
void shapeHardClip(MorphNodeState& n, float threshold, float ceiling) {
    setShapeSlots(n, {threshold, ceiling});
}

// Tube: bias [-1,1], sag [0,1], stage [0-3]
void shapeTube(MorphNodeState& n, float bias, float sag, int stage = 0) {
    setShapeSlots(n, {(bias + 1.0f) * 0.5f, sag, static_cast<float>(stage) / 3.0f});
}

// Tape: bias [-1,1], sag [0,1], speed [0,1], model [0-1], hfRoll [0,1], flutter [0,1]
void shapeTape(MorphNodeState& n, float bias, float sag, float speed,
               int model = 0, float hfRoll = 0.5f, float flutter = 0.2f) {
    setShapeSlots(n, {(bias + 1.0f) * 0.5f, sag, speed,
                      static_cast<float>(model), hfRoll, flutter});
}

// Fuzz, in the plugin's slot order (shape_slot_descriptors.h): bias [-1,1], transistor [0 Germanium,
// 1 Silicon], octave [0,1], sustain [0,1]
void shapeFuzz(MorphNodeState& n, float bias, int transistor, float octave, float sustain) {
    setShapeSlots(n, {(bias + 1.0f) * 0.5f, static_cast<float>(transistor), octave, sustain});
}

// AsymmetricFuzz, in the plugin's slot order (shape_slot_descriptors.h): bias [-1,1], asymmetry [0,1],
// transistor [0 Germanium, 1 Silicon], sustain [0,1], body [0,1]
void shapeAsymFuzz(MorphNodeState& n, float bias, float asymmetry, int transistor, float sustain, float body) {
    setShapeSlots(n, {(bias + 1.0f) * 0.5f, asymmetry, static_cast<float>(transistor), sustain, body});
}

// SineFold: folds [1,12], symmetry [-1,1], shape [0,1], bias [-1,1], smooth [0,1]
void shapeSineFold(MorphNodeState& n, float folds, float symmetry = 0.0f,
                   float shape = 0.0f, float bias = 0.0f, float smooth = 0.0f) {
    setShapeSlots(n, {(folds - 1.0f) / 11.0f, (symmetry + 1.0f) * 0.5f,
                      shape, (bias + 1.0f) * 0.5f, smooth});
}

// TriangleFold: folds [1,12], symmetry [-1,1], angle [0,1], bias [-1,1], smooth [0,1]
void shapeTriFold(MorphNodeState& n, float folds, float symmetry = 0.0f,
                  float angle = 0.5f, float bias = 0.0f, float smooth = 0.0f) {
    setShapeSlots(n, {(folds - 1.0f) / 11.0f, (symmetry + 1.0f) * 0.5f,
                      angle, (bias + 1.0f) * 0.5f, smooth});
}

// SergeFold: folds [1,12], symmetry [-1,1], model [0-3], bias [-1,1],
//            shape [0,1], smooth [0,1]
void shapeSergeFold(MorphNodeState& n, float folds, float symmetry = 0.0f,
                    int model = 0, float bias = 0.0f, float shape = 0.0f,
                    float smooth = 0.0f) {
    setShapeSlots(n, {(folds - 1.0f) / 11.0f, (symmetry + 1.0f) * 0.5f,
                      static_cast<float>(model) / 3.0f, (bias + 1.0f) * 0.5f,
                      shape, smooth});
}

// FullRectify: smooth [0,1], dcBlock bool
void shapeFullRect(MorphNodeState& n, float smooth, bool dcBlock = true) {
    setShapeSlots(n, {smooth, dcBlock ? 1.0f : 0.0f});
}

// HalfRectify: threshold [0,1], smooth [0,1], dcBlock bool
void shapeHalfRect(MorphNodeState& n, float threshold, float smooth,
                   bool dcBlock = true) {
    setShapeSlots(n, {threshold, smooth, dcBlock ? 1.0f : 0.0f});
}

// Bitcrush: bits [4,16], dither [0,1], mode [0-1], jitter [0,1]
void shapeBitcrush(MorphNodeState& n, float bits, float dither = 0.0f,
                   int mode = 0, float jitter = 0.0f) {
    setShapeSlots(n, {(bits - 4.0f) / 12.0f, dither,
                      static_cast<float>(mode), jitter});
}

// SampleReduce: ratio [1,32], jitter [0,1], mode [0-1], smooth [0,1]
void shapeSampleReduce(MorphNodeState& n, float ratio, float jitter = 0.0f,
                       int mode = 0, float smooth = 0.0f) {
    setShapeSlots(n, {(ratio - 1.0f) / 31.0f, jitter,
                      static_cast<float>(mode), smooth});
}

// Quantize: levels [0,1], dither [0,1], smooth [0,1], offset [0,1]
void shapeQuantize(MorphNodeState& n, float levels, float dither = 0.0f,
                   float smooth = 0.0f, float offset = 0.0f) {
    setShapeSlots(n, {levels, dither, smooth, offset});
}

// Temporal: mode [0-3], sensitivity [0,1], curve [0,1], attackMs [1,500],
//           releaseMs [10,5000], depth [0,1], lookAhead [0-1], hold [0,1]
void shapeTemporal(MorphNodeState& n, int mode, float sensitivity, float curve,
                   float attackMs = 10.0f, float releaseMs = 100.0f,
                   float depth = 0.5f, int lookAhead = 0, float hold = 0.0f) {
    setShapeSlots(n, {static_cast<float>(mode) / 3.0f, sensitivity, curve,
                      (attackMs - 1.0f) / 499.0f, (releaseMs - 10.0f) / 4990.0f,
                      depth, static_cast<float>(lookAhead), hold});
}

// RingSaturation: mod [0,1], stages [1-4], curve [0,1], carrier [0-3],
//                 bias [-1,1], freqSelect [0-3]
void shapeRingSat(MorphNodeState& n, float mod, int stages = 1, float curve = 0.5f,
                  int carrier = 0, float bias = 0.0f, int freqSelect = 0) {
    setShapeSlots(n, {mod, static_cast<float>(stages - 1) / 3.0f, curve,
                      static_cast<float>(carrier) / 3.0f,
                      (bias + 1.0f) * 0.5f, static_cast<float>(freqSelect) / 3.0f});
}

// FeedbackDist: feedback [0,1.5], delayMs [1,100], curve [0,1], filterType [0-3],
//               filterFreq [0,1], stages [1-4], limiter bool, limThreshold [0,1]
void shapeFeedback(MorphNodeState& n, float feedback, float delayMs = 10.0f,
                   float curve = 0.5f, int filterType = 0, float filterFreq = 0.5f,
                   int stages = 1, bool limiter = true, float limThreshold = 0.8f) {
    setShapeSlots(n, {feedback / 1.5f, (delayMs - 1.0f) / 99.0f, curve,
                      static_cast<float>(filterType) / 3.0f, filterFreq,
                      static_cast<float>(stages - 1) / 3.0f,
                      limiter ? 1.0f : 0.0f, limThreshold});
}

// Aliasing: downsample [2,32], freqShift [-5000,5000], preFilter bool,
//           feedback [0,0.95], resonance [0,1]
void shapeAliasing(MorphNodeState& n, float downsample, float freqShift = 0.0f,
                   bool preFilter = false, float feedback = 0.0f,
                   float resonance = 0.0f) {
    setShapeSlots(n, {(downsample - 2.0f) / 30.0f,
                      (freqShift / 5000.0f + 1.0f) * 0.5f,
                      preFilter ? 1.0f : 0.0f, feedback / 0.95f, resonance});
}

// BitwiseMangler: op [0-5], intensity [0,1], pattern [0,1], bits [0,1], smooth [0,1]
void shapeBitwise(MorphNodeState& n, int op, float intensity = 0.5f,
                  float pattern = 0.0f, float bits = 0.5f, float smooth = 0.0f) {
    setShapeSlots(n, {static_cast<float>(op) / 5.0f, intensity, pattern, bits, smooth});
}

// Chaos: attractor [0-3], speed [0.01,100], amount [0,1], coupling [0,1],
//        xDrive [0,1], yDrive [0,1], smooth [0,1]
void shapeChaos(MorphNodeState& n, int attractor, float speed, float amount,
                float coupling = 0.5f, float xDrive = 0.5f, float yDrive = 0.5f,
                float smooth = 0.0f) {
    setShapeSlots(n, {static_cast<float>(attractor) / 3.0f,
                      (speed - 0.01f) / 99.99f, amount, coupling,
                      xDrive, yDrive, smooth});
}

// Formant: vowel [0-4], shift [-24,24], curve [0,1], reso [0,1], bw [0,1],
//          count [0-3], gender [0,1], blend [0,1]
void shapeFormant(MorphNodeState& n, int vowel, float shift = 0.0f, float curve = 0.5f,
                  float reso = 0.5f, float bw = 0.5f, int count = 0,
                  float gender = 0.5f, float blend = 0.5f) {
    setShapeSlots(n, {static_cast<float>(vowel) / 4.0f,
                      (shift / 24.0f + 1.0f) * 0.5f, curve, reso, bw,
                      static_cast<float>(count) / 3.0f, gender, blend});
}

// Granular: sizeMs [5,100], density [0,1], pitchVar [0,1], densVar [0,1],
//           pos [0,1], curve [0,1], envType [0-3], spread [0-3], freeze bool
void shapeGranular(MorphNodeState& n, float sizeMs, float density = 0.5f,
                   float pitchVar = 0.0f, float densVar = 0.0f, float pos = 0.0f,
                   float curve = 0.5f, int envType = 0, int spread = 0,
                   bool freeze = false) {
    setShapeSlots(n, {(sizeMs - 5.0f) / 95.0f, density, pitchVar, densVar, pos,
                      curve, static_cast<float>(envType) / 3.0f,
                      static_cast<float>(spread) / 3.0f,
                      freeze ? 1.0f : 0.0f});
}

// Spectral: mode [0-3], fftSize {512,1024,2048,4096} as [0-3], curve [0,1],
//           tilt [0,1], threshold [0,1], magMode [0-3], freq [0,1], phase [0-3]
void shapeSpectral(MorphNodeState& n, int mode, int fftIdx = 2, float curve = 0.5f,
                   float tilt = 0.5f, float threshold = 0.0f, int magMode = 0,
                   float freq = 0.5f, int phase = 0) {
    setShapeSlots(n, {static_cast<float>(mode) / 3.0f,
                      static_cast<float>(fftIdx) / 3.0f, curve, tilt, threshold,
                      static_cast<float>(magMode) / 3.0f, freq,
                      static_cast<float>(phase) / 3.0f});
}

// Fractal: mode [0-4], iterations [1-8], scale [0.3,0.9], curve [0,1],
//          decay [0,1], feedback [0,0.5], blend [0-3], depth [0,1]
void shapeFractal(MorphNodeState& n, int mode, int iterations = 4, float scale = 0.5f,
                  float curve = 0.5f, float decay = 0.5f, float feedback = 0.0f,
                  int blend = 0, float depth = 0.5f) {
    setShapeSlots(n, {static_cast<float>(mode) / 4.0f,
                      static_cast<float>(iterations - 1) / 7.0f,
                      (scale - 0.3f) / 0.6f, curve, decay,
                      feedback / 0.5f, static_cast<float>(blend) / 3.0f, depth});
}

// Stochastic: curve [0-5], jitter [0,1], rate [0.1,100], coefNoise [0,1],
//             drift [0,1], correlation [0-3], smooth [0,1]
void shapeStochastic(MorphNodeState& n, int curve, float jitter = 0.2f,
                     float rate = 10.0f, float coefNoise = 0.1f, float drift = 0.0f,
                     int correlation = 0, float smooth = 0.5f) {
    setShapeSlots(n, {static_cast<float>(curve) / 5.0f, jitter,
                      (rate - 0.1f) / 99.9f, coefNoise, drift,
                      static_cast<float>(correlation) / 3.0f, smooth});
}

// AllpassResonant: topology [0-3], freq [20,2000], feedback [0,0.99],
//                  decay [0.01,10], curve [0,1], stages [1-4], pitch bool, damp [0,1]
void shapeAllpass(MorphNodeState& n, int topology, float freq = 440.0f,
                  float feedback = 0.7f, float decay = 1.0f, float curve = 0.5f,
                  int stages = 1, bool pitch = false, float damp = 0.3f) {
    setShapeSlots(n, {static_cast<float>(topology) / 3.0f,
                      (freq - 20.0f) / 1980.0f, feedback / 0.99f,
                      (decay - 0.01f) / 9.99f, curve,
                      static_cast<float>(stages - 1) / 3.0f,
                      pitch ? 1.0f : 0.0f, damp});
}

// ==============================================================================
// Helpers: Sweep, modulation routing
// ==============================================================================

void enableSweep(DisrumpoPresetState& s, float freqNorm, float widthNorm,
                 float intensityNorm, MorphLinkMode link = MorphLinkMode::None) {
    s.sweep.enabled = true;
    s.sweep.freqNorm = freqNorm;
    s.sweep.widthNorm = widthNorm;
    s.sweep.intensityNorm = intensityNorm;
    s.sweep.morphLink = link;
}

void enableSweepLFO(DisrumpoPresetState& s, float rateNorm, float depth,
                    Waveform wave = Waveform::Sine) {
    s.sweep.lfoEnabled = true;
    s.sweep.lfoRateNorm = rateNorm;
    s.sweep.lfoDepth = depth;
    s.sweep.lfoWaveform = wave;
}

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
// Preset Definitions - 120 total across 11 categories
// ==============================================================================

std::vector<PresetDef> createAllPresets() {
    std::vector<PresetDef> presets;

    // =========================================================================
    // INIT (4 presets) - Clean starting points
    // =========================================================================
    {
        PresetDef p; p.name = "Init 1 Band"; p.category = "Init";
        p.description = "One full-range Soft Clip band at drive 1, with no sweep and no modulation: the blank "
                        "starting point for a new sound.";
        p.state = makeInitState(1);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Init 2 Bands"; p.category = "Init";
        p.description = "Two Soft Clip bands at drive 1, split at 200 Hz, with no sweep and no modulation: a blank "
                        "start for treating lows and highs apart.";
        p.state = makeInitState(2);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Init 3 Bands"; p.category = "Init";
        p.description = "Three Soft Clip bands at drive 1, split at 200 Hz and 1.5 kHz, with no sweep and no "
                        "modulation: a blank three-way starting point.";
        p.state = makeInitState(3);
        presets.push_back(p);
    }
    {
        PresetDef p; p.name = "Init 4 Bands"; p.category = "Init";
        p.description = "Four Soft Clip bands at drive 1, split at 200 Hz, 1.5 kHz and 6 kHz, with no sweep and no "
                        "modulation: a blank start using every band.";
        p.state = makeInitState(4);
        presets.push_back(p);
    }

    // =========================================================================
    // SWEEP (15 presets) - Sweep system showcases
    // =========================================================================
    { // Frequency Hunter: Tube lows, wavefold mids, tape-saturated highs, sweep scanning
        PresetDef p; p.name = "Frequency Hunter"; p.category = "Sweep";
        p.description = "A triangle-LFO sweep hunts across three bands and morphs each as it rises: Tube lows "
                        "into Asymmetric Fuzz, Sine Fold mids into Triangle Fold, Tape highs into Hard Clip; LFO 1 "
                        "varies its intensity.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3000.0f;
        enableSweep(p.state, 0.5f, 0.4f, 0.6f, MorphLinkMode::SweepFreq);
        enableSweepLFO(p.state, 0.35f, 0.6f, Waveform::Triangle);
        // Low: warm tube, rising into a silicon asymmetric fuzz
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 3.0f, 2500.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.2f, 0.4f, 1);
        // bias +0.15, asymmetry 0.3, Silicon, sustain 0.8, body 0.4
        setNode(p.state.bandMorph[0].nodes[1], DistortionType::AsymmetricFuzz, 3.0f, 4000.0f);
        shapeAsymFuzz(p.state.bandMorph[0].nodes[1], 0.15f, 0.3f, 1, 0.8f, 0.4f);
        // Mid: sine fold, rising into a triangle fold with more folds
        setBandType(p.state.bandMorph[1], DistortionType::SineFold, 2.5f, 5000.0f);
        shapeSineFold(p.state.bandMorph[1].nodes[0], 3.0f, 0.2f, 0.3f);
        setNode(p.state.bandMorph[1].nodes[1], DistortionType::TriangleFold, 2.5f, 5500.0f);
        shapeTriFold(p.state.bandMorph[1].nodes[1], 4.5f, -0.2f, 0.7f);
        // High: bright tape, rising into hard clip
        setBandType(p.state.bandMorph[2], DistortionType::Tape, 2.0f, 6000.0f);
        shapeTape(p.state.bandMorph[2].nodes[0], 0.0f, 0.3f, 0.7f, 1, 0.3f, 0.15f);
        setNode(p.state.bandMorph[2].nodes[1], DistortionType::HardClip, 2.5f, 7000.0f);
        shapeHardClip(p.state.bandMorph[2].nodes[1], 0.5f, 0.9f);
        // Mod: LFO1 moves sweep intensity
        addRouting(p.state, 0, ModSource::LFO1, ModDest::kSweepIntensity, 0.3f);
        p.state.modSources.lfo1RateNorm = 0.2f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.bands[1].pan = -0.2f;
        p.state.bands[2].pan = 0.2f;
        presets.push_back(p);
    }
    { // Slow Scan: 4-band with different textures, slow sine sweep LFO
        PresetDef p; p.name = "Slow Scan"; p.category = "Sweep";
        p.description = "A wide sweep that a sine LFO moves slowly up from the low end, about one pass every 40 "
                        "seconds, across Tube, Tape, Soft Clip and Allpass Resonant bands. For pads and long synth "
                        "parts.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 150.0f;
        p.state.crossoverFreqs[1] = 1200.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        enableSweep(p.state, 0.3f, 0.6f, 0.4f);
        enableSweepLFO(p.state, 0.12f, 0.7f, Waveform::Sine);
        // Low: tube with sag
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 2.5f, 2000.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.1f, 0.6f, 2);
        // Lo-mid: tape warmth
        setBandType(p.state.bandMorph[1], DistortionType::Tape, 2.0f, 3500.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.0f, 0.4f, 0.6f, 1, 0.6f, 0.3f);
        // Hi-mid: soft saturation
        setBandType(p.state.bandMorph[2], DistortionType::SoftClip, 2.0f, 5500.0f);
        shapeSoftClip(p.state.bandMorph[2].nodes[0], 0.7f, 0.3f);
        // High: allpass shimmer
        setBandType(p.state.bandMorph[3], DistortionType::AllpassResonant, 1.5f, 7000.0f);
        shapeAllpass(p.state.bandMorph[3].nodes[0], 1, 800.0f, 0.6f, 2.0f, 0.4f, 2);
        p.state.bands[0].pan = 0.0f;
        p.state.bands[1].pan = -0.15f;
        p.state.bands[2].pan = 0.15f;
        addRouting(p.state, 0, ModSource::LFO2, ModDest::bandDrive(3), 0.2f);
        p.state.modSources.lfo2RateNorm = 0.08f;
        p.state.modSources.lfo2Shape = Waveform::SmoothRandom;
        presets.push_back(p);
    }
    { // Wah Wah Drive: sweep-linked morph, tube→fuzz crossfade
        PresetDef p; p.name = "Wah Wah Drive"; p.category = "Sweep";
        p.description = "Two bands split at 800 Hz: a triangle-LFO sweep moves the low band from Tube to Fuzz like "
                        "a slow wah and clips the Hard Clip highs harder and brighter as it rises; the input level "
                        "adds drive. Try guitar.";
        p.state = makeInitState(2);
        p.state.crossoverFreqs[0] = 800.0f;
        enableSweep(p.state, 0.4f, 0.3f, 0.7f, MorphLinkMode::SweepFreq);
        enableSweepLFO(p.state, 0.5f, 0.8f, Waveform::Triangle);
        // Low: tube→fuzz morph
        setMorphAB(p.state.bandMorph[0], DistortionType::Tube, DistortionType::Fuzz,
                   3.0f, 4.5f, 2500.0f, 5000.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.15f, 0.5f, 1);
        shapeFuzz(p.state.bandMorph[0].nodes[1], 0.2f, 0, 1.0f, 0.3f);
        // High: hard clip for edge
        setBandType(p.state.bandMorph[1], DistortionType::HardClip, 3.0f, 6000.0f);
        shapeHardClip(p.state.bandMorph[1].nodes[0], 0.6f, 0.9f);
        // the sweep clips the highs harder and brighter as it rises
        p.state.bandMorph[1].nodes[1].drive = 6.0f;
        p.state.bandMorph[1].nodes[1].toneHz = 7500.0f;
        shapeHardClip(p.state.bandMorph[1].nodes[1], 0.25f, 0.95f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.3f);
        p.state.modSources.envAttackNorm = 0.03f;
        p.state.modSources.envReleaseNorm = 0.15f;
        p.state.modSources.envSensitivity = 0.7f;
        presets.push_back(p);
    }
    { // Resonant Sweep: allpass on mids, tape on lows, formant on highs
        PresetDef p; p.name = "Resonant Sweep"; p.category = "Sweep";
        p.description = "A narrow, strong sweep moving through an Allpass Resonant mid band, between Tape lows and "
                        "Formant highs, with LFO 1 swelling the mid drive. Ringing, vocal tones for synths and "
                        "guitars.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        enableSweep(p.state, 0.6f, 0.2f, 0.8f);
        enableSweepLFO(p.state, 0.4f, 0.6f, Waveform::Sine);
        // Low: tape for weight
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 2.0f, 1500.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.1f, 0.5f, 0.4f, 0, 0.7f, 0.1f);
        // Mid: allpass resonant — the star
        setBandType(p.state.bandMorph[1], DistortionType::AllpassResonant, 2.5f, 5000.0f);
        shapeAllpass(p.state.bandMorph[1].nodes[0], 2, 600.0f, 0.8f, 1.5f, 0.6f, 3, false, 0.2f);
        // High: formant for vocal character
        setBandType(p.state.bandMorph[2], DistortionType::Formant, 1.5f, 7000.0f);
        shapeFormant(p.state.bandMorph[2].nodes[0], 2, 3.0f, 0.6f, 0.7f, 0.4f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(1), 0.25f);
        p.state.modSources.lfo1RateNorm = 0.3f;
        p.state.bands[1].pan = -0.1f;
        p.state.bands[2].pan = 0.1f;
        presets.push_back(p);
    }
    { // Envelope Follower: input-reactive sweep, different saturation per band
        PresetDef p; p.name = "Envelope Follower"; p.category = "Sweep";
        p.description = "The sweep follows your playing level instead of an LFO, rising as you play harder over "
                        "Tape, Tube and Soft Clip bands; the input level also drives the Tube mids and Soft Clip "
                        "top. For guitar, keys and drums.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        enableSweep(p.state, 0.5f, 0.5f, 0.6f);
        p.state.sweep.envEnabled = true;
        p.state.sweep.envAttackNorm = 0.04f;
        p.state.sweep.envReleaseNorm = 0.25f;
        p.state.sweep.envSensitivity = 0.8f;
        // Low: warm tape
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 2.0f, 2000.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.0f, 0.5f, 0.5f, 1, 0.6f, 0.25f);
        // Mid: tube with sag that reacts to dynamics
        setBandType(p.state.bandMorph[1], DistortionType::Tube, 3.0f, 4000.0f);
        shapeTube(p.state.bandMorph[1].nodes[0], 0.1f, 0.7f, 2);
        // High: soft clip
        setBandType(p.state.bandMorph[2], DistortionType::SoftClip, 1.8f, 6500.0f);
        shapeSoftClip(p.state.bandMorph[2].nodes[0], 0.6f, 0.4f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(1), 0.4f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandDrive(2), 0.3f);
        p.state.modSources.envAttackNorm = 0.02f;
        p.state.modSources.envReleaseNorm = 0.2f;
        p.state.modSources.envSensitivity = 0.75f;
        presets.push_back(p);
    }
    { // Triangle Wobble: wavefold lows, fuzz mids, ring sat highs, triangle LFO
        PresetDef p; p.name = "Triangle Wobble"; p.category = "Sweep";
        p.description = "A quick triangle-LFO sweep wobbles across Triangle Fold, Asymmetric Fuzz, Ring Saturation "
                        "and Soft Clip bands, while LFO 2 swings the two middle bands in opposite pan directions.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 180.0f;
        p.state.crossoverFreqs[1] = 1500.0f;
        p.state.crossoverFreqs[2] = 6000.0f;
        enableSweep(p.state, 0.45f, 0.35f, 0.55f);
        enableSweepLFO(p.state, 0.65f, 0.6f, Waveform::Triangle);
        // Low: triangle fold
        setBandType(p.state.bandMorph[0], DistortionType::TriangleFold, 2.5f, 2000.0f);
        shapeTriFold(p.state.bandMorph[0].nodes[0], 2.5f, 0.1f, 0.6f);
        // Lo-mid: asymmetric fuzz
        setBandType(p.state.bandMorph[1], DistortionType::AsymmetricFuzz, 3.5f, 4000.0f);
        shapeAsymFuzz(p.state.bandMorph[1].nodes[0], 0.3f, 0.6f, 1, 0.1f, 0.6f);
        // Hi-mid: ring saturation
        setBandType(p.state.bandMorph[2], DistortionType::RingSaturation, 2.0f, 5500.0f);
        shapeRingSat(p.state.bandMorph[2].nodes[0], 0.6f, 2, 0.4f, 1);
        // High: soft clip to keep it tame
        setBandType(p.state.bandMorph[3], DistortionType::SoftClip, 1.5f, 7000.0f);
        shapeSoftClip(p.state.bandMorph[3].nodes[0], 0.8f, 0.2f);
        p.state.bands[1].pan = -0.25f;
        p.state.bands[2].pan = 0.25f;
        addRouting(p.state, 0, ModSource::LFO2, ModDest::bandPan(1), 0.2f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandPan(2), -0.2f);
        p.state.modSources.lfo2RateNorm = 0.4f;
        p.state.modSources.lfo2Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // Sharp Focus: narrow sharp sweep on feedback dist, allpass resonance underneath
        PresetDef p; p.name = "Sharp Focus"; p.category = "Sweep";
        p.description = "A narrow sweep with a sharp edge, moved by a saw LFO, over Feedback Distortion lows, Hard "
                        "Clip mids and Aliasing highs; transients kick the mid drive. For drums and synth leads.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 400.0f;
        p.state.crossoverFreqs[1] = 5000.0f;
        enableSweep(p.state, 0.7f, 0.12f, 0.9f);
        p.state.sweep.falloff = SweepFalloff::Sharp;
        enableSweepLFO(p.state, 0.25f, 0.5f, Waveform::Saw);
        // Low: feedback dist with short delay
        setBandType(p.state.bandMorph[0], DistortionType::FeedbackDist, 3.5f, 3000.0f);
        shapeFeedback(p.state.bandMorph[0].nodes[0], 0.7f, 8.0f, 0.6f, 1, 0.4f, 2);
        // Mid: hard clip with tight threshold
        setBandType(p.state.bandMorph[1], DistortionType::HardClip, 4.0f, 5000.0f);
        shapeHardClip(p.state.bandMorph[1].nodes[0], 0.5f, 0.85f);
        // High: aliasing for digital edge
        setBandType(p.state.bandMorph[2], DistortionType::Aliasing, 2.0f, 7500.0f);
        shapeAliasing(p.state.bandMorph[2].nodes[0], 4.0f, 200.0f, true, 0.2f, 0.4f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(1), 0.4f);
        p.state.modSources.transSensitivity = 0.7f;
        p.state.modSources.transAttackNorm = 0.05f;
        presets.push_back(p);
    }
    { // Wide Sweep: broad sweep with morphing tape→tube per band
        PresetDef p; p.name = "Wide Sweep"; p.category = "Sweep";
        p.description = "A wide saw-LFO sweep eases four bands toward their second node: Tape lows into Tube (LFO "
                        "1 nudges it), Serge Fold mids into more folds, a harder Soft Clip, and a Formant top shifting "
                        "its formants up.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 120.0f;
        p.state.crossoverFreqs[1] = 1000.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        enableSweep(p.state, 0.5f, 0.8f, 0.35f, MorphLinkMode::EaseOut);
        enableSweepLFO(p.state, 0.2f, 0.5f, Waveform::Saw);
        // Low: tape with flutter
        setMorphAB(p.state.bandMorph[0], DistortionType::Tape, DistortionType::Tube,
                   2.0f, 2.5f, 1500.0f, 2000.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.0f, 0.6f, 0.4f, 1, 0.7f, 0.35f);
        shapeTube(p.state.bandMorph[0].nodes[1], 0.15f, 0.5f, 2);
        // Lo-mid: serge fold
        setBandType(p.state.bandMorph[1], DistortionType::SergeFold, 2.0f, 3500.0f);
        shapeSergeFold(p.state.bandMorph[1].nodes[0], 2.0f, 0.15f, 1, 0.0f, 0.3f);
        // the sweep eases it into more folds (Buchla model, which the sweep's range always runs)
        p.state.bandMorph[1].nodes[1].drive = 2.5f;
        shapeSergeFold(p.state.bandMorph[1].nodes[1], 5.0f, -0.2f, 2, 0.0f, 0.6f, 0.3f);
        // Hi-mid: soft clip, eased into a harder curve
        setBandType(p.state.bandMorph[2], DistortionType::SoftClip, 2.0f, 5500.0f);
        shapeSoftClip(p.state.bandMorph[2].nodes[0], 0.65f, 0.4f);
        p.state.bandMorph[2].nodes[1].drive = 3.0f;
        p.state.bandMorph[2].nodes[1].toneHz = 6500.0f;
        shapeSoftClip(p.state.bandMorph[2].nodes[1], 0.9f, 0.1f);
        // High: light formant, eased into higher, more resonant formants
        setBandType(p.state.bandMorph[3], DistortionType::Formant, 1.5f, 7000.0f);
        shapeFormant(p.state.bandMorph[3].nodes[0], 1, 2.0f, 0.4f, 0.5f, 0.6f);
        shapeFormant(p.state.bandMorph[3].nodes[1], 1, 12.0f, 0.6f, 0.85f, 0.3f, 2);
        p.state.bands[1].pan = -0.2f;
        p.state.bands[2].pan = 0.2f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.3f);
        p.state.modSources.lfo1RateNorm = 0.15f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // Inverse Link: morph sweeps inversely — gets brighter as sweep descends
        PresetDef p; p.name = "Inverse Link"; p.category = "Sweep";
        p.description = "As the sweep falls (an inverse link) the lows morph from Soft Clip to Hard Clip, the "
                        "octave Fuzz mids gain sustain and brightness and the Bitcrush highs drop toward 6 bits; a "
                        "stepped random LFO jumps the mid drive.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        enableSweep(p.state, 0.5f, 0.4f, 0.6f, MorphLinkMode::InverseSweep);
        enableSweepLFO(p.state, 0.4f, 0.7f, Waveform::Sine);
        // Low: soft→hard morph
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::HardClip,
                   2.0f, 4.5f, 2000.0f, 3000.0f);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.8f, 0.3f);
        shapeHardClip(p.state.bandMorph[0].nodes[1], 0.55f, 0.9f);
        // Mid: fuzz with octave
        setBandType(p.state.bandMorph[1], DistortionType::Fuzz, 4.0f, 4500.0f);
        shapeFuzz(p.state.bandMorph[1].nodes[0], 0.1f, 0, 1.0f, 0.4f);
        // as the sweep falls: more sustain, the bias flipped, brighter (node A: Germanium, octave
        // 1.0, sustain 0.4): bias -0.15, Germanium, octave 1.0, sustain 0.7
        p.state.bandMorph[1].nodes[1].toneHz = 7000.0f;
        shapeFuzz(p.state.bandMorph[1].nodes[1], -0.15f, 0, 1.0f, 0.7f);
        // High: bitcrush for digital edge
        setBandType(p.state.bandMorph[2], DistortionType::Bitcrush, 1.5f, 6000.0f);
        shapeBitcrush(p.state.bandMorph[2].nodes[0], 10.0f, 0.2f, 0, 0.1f);
        // as the sweep falls: down to 6 bits, little dither, more jitter
        shapeBitcrush(p.state.bandMorph[2].nodes[1], 6.0f, 0.05f, 0, 0.35f);
        addRouting(p.state, 0, ModSource::LFO2, ModDest::bandDrive(1), 0.2f);
        p.state.modSources.lfo2RateNorm = 0.55f;
        p.state.modSources.lfo2Shape = Waveform::SampleAndHold;
        p.state.bands[2].gainDb = -2.0f;
        presets.push_back(p);
    }
    { // Step Scanner: stepped morph link, wavefold on each band with different folds
        PresetDef p; p.name = "Step Scanner"; p.category = "Sweep";
        p.description = "Three wavefolders (Sine, Triangle, Serge) and a Chaos top; a saw-LFO sweep on a stepped "
                        "link steps each band to more folds or a faster attractor, and a Rossler chaos source varies "
                        "the top drive.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 1500.0f;
        p.state.crossoverFreqs[2] = 6000.0f;
        enableSweep(p.state, 0.4f, 0.3f, 0.7f, MorphLinkMode::Stepped);
        enableSweepLFO(p.state, 0.35f, 0.6f, Waveform::Saw);
        // Low: sine fold, low count
        setBandType(p.state.bandMorph[0], DistortionType::SineFold, 2.5f, 2000.0f);
        shapeSineFold(p.state.bandMorph[0].nodes[0], 2.0f, 0.0f, 0.2f);
        p.state.bandMorph[0].nodes[1].drive = 3.0f;  // each step: more folds
        shapeSineFold(p.state.bandMorph[0].nodes[1], 8.0f, 0.3f, 0.5f, 0.0f, 0.5f);
        // Lo-mid: triangle fold, medium
        setBandType(p.state.bandMorph[1], DistortionType::TriangleFold, 2.5f, 4000.0f);
        shapeTriFold(p.state.bandMorph[1].nodes[0], 3.5f, 0.2f, 0.7f);
        p.state.bandMorph[1].nodes[1].drive = 3.5f;
        shapeTriFold(p.state.bandMorph[1].nodes[1], 7.0f, -0.2f, 0.4f, 0.0f, 0.5f);
        // Hi-mid: serge fold, high count
        setBandType(p.state.bandMorph[2], DistortionType::SergeFold, 3.0f, 5500.0f);
        shapeSergeFold(p.state.bandMorph[2].nodes[0], 5.0f, -0.3f, 2, 0.1f, 0.4f);
        p.state.bandMorph[2].nodes[1].drive = 3.5f;
        shapeSergeFold(p.state.bandMorph[2].nodes[1], 9.0f, 0.2f, 2, -0.1f, 0.7f, 0.5f);
        // High: chaos for unpredictable texture; each step a faster, harder-driven attractor
        setBandType(p.state.bandMorph[3], DistortionType::Chaos, 2.0f, 7000.0f);
        shapeChaos(p.state.bandMorph[3].nodes[0], 1, 5.0f, 0.6f, 0.4f, 0.6f, 0.3f);
        p.state.bandMorph[3].nodes[1].drive = 2.5f;
        shapeChaos(p.state.bandMorph[3].nodes[1], 1, 15.0f, 0.85f, 0.6f, 0.3f, 0.7f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(3), 0.3f);
        p.state.modSources.chaosModel = ChaosModel::Rossler;
        p.state.modSources.chaosSpeedNorm = 0.2f;
        p.state.bands[0].pan = -0.15f;
        p.state.bands[3].pan = 0.15f;
        presets.push_back(p);
    }
    { // Low Rumble: sub-focused sweep, feedback dist lows, tape mids
        PresetDef p; p.name = "Low Rumble"; p.category = "Sweep";
        p.description = "Built for the bottom octave: Feedback Distortion below 100 Hz, heavy-sag Tube to 800 Hz "
                        "and light Tape above, with a slow sweep around 55 Hz and the sub drive swelling. For bass "
                        "and kicks.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 100.0f;
        p.state.crossoverFreqs[1] = 800.0f;
        enableSweep(p.state, 0.15f, 0.5f, 0.65f);
        enableSweepLFO(p.state, 0.1f, 0.8f, Waveform::Sine);
        // Sub: feedback dist for resonant sub rumble
        setBandType(p.state.bandMorph[0], DistortionType::FeedbackDist, 3.5f, 1000.0f);
        shapeFeedback(p.state.bandMorph[0].nodes[0], 0.8f, 15.0f, 0.7f, 0, 0.3f, 1);
        // Low-mid: tube with heavy sag
        setBandType(p.state.bandMorph[1], DistortionType::Tube, 3.5f, 2500.0f);
        shapeTube(p.state.bandMorph[1].nodes[0], 0.2f, 0.8f, 3);
        // Mid+high: light tape to glue
        setBandType(p.state.bandMorph[2], DistortionType::Tape, 1.5f, 4000.0f);
        shapeTape(p.state.bandMorph[2].nodes[0], 0.0f, 0.3f, 0.3f, 0, 0.8f, 0.1f);
        p.state.bands[0].gainDb = 3.0f;
        p.state.bands[2].gainDb = -2.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.35f, ModCurve::SCurve);
        p.state.modSources.lfo1RateNorm = 0.08f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // High Sweep: focused high sweep, fuzz highs, temporal mids, tube lows
        PresetDef p; p.name = "High Sweep"; p.category = "Sweep";
        p.description = "A focused sweep around 5 kHz on a fast triangle LFO, over Tube lows, Temporal mids and "
                        "octave Fuzz highs that the input level drives harder. Moving bite for synths, guitars and "
                        "hats.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 500.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        enableSweep(p.state, 0.8f, 0.25f, 0.6f);
        enableSweepLFO(p.state, 0.7f, 0.5f, Waveform::Triangle);
        // Low: tube warmth
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 2.0f, 2000.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.0f, 0.4f, 1);
        // Mid: temporal distortion for dynamic response
        setBandType(p.state.bandMorph[1], DistortionType::Temporal, 3.0f, 5000.0f);
        shapeTemporal(p.state.bandMorph[1].nodes[0], 0, 0.6f, 0.5f, 15.0f, 150.0f, 0.6f);
        // High: fuzz with octave
        setBandType(p.state.bandMorph[2], DistortionType::Fuzz, 3.5f, 7000.0f);
        shapeFuzz(p.state.bandMorph[2].nodes[0], -0.1f, 0, 0.0f, 0.5f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(2), 0.3f);
        p.state.modSources.envSensitivity = 0.7f;
        p.state.bands[2].gainDb = -1.5f;
        presets.push_back(p);
    }
    { // Ease In Morph: slow ease-in morph link, tape→chaos
        PresetDef p; p.name = "Ease In Morph"; p.category = "Sweep";
        p.description = "The sweep eases the lows from Tape into Fuzz (an ease-in morph link) and, on the same "
                        "curve, drives the Chaos mids faster and wilder and tilts the Spectral highs; a chaos source "
                        "varies the mid drive, LFO 2 pans the top.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3000.0f;
        enableSweep(p.state, 0.5f, 0.4f, 0.5f, MorphLinkMode::EaseIn);
        enableSweepLFO(p.state, 0.2f, 0.6f, Waveform::Sine);
        // Low: tape→fuzz morph
        setMorphAB(p.state.bandMorph[0], DistortionType::Tape, DistortionType::Fuzz,
                   2.0f, 4.0f, 2000.0f, 3500.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.1f, 0.4f, 0.5f, 1, 0.5f, 0.2f);
        shapeFuzz(p.state.bandMorph[0].nodes[1], 0.2f, 0, 0.0f, 0.2f);
        // Mid: chaos with slow attractor
        setBandType(p.state.bandMorph[1], DistortionType::Chaos, 2.5f, 4500.0f);
        shapeChaos(p.state.bandMorph[1].nodes[0], 0, 2.0f, 0.6f, 0.5f, 0.4f, 0.6f, 0.2f);
        p.state.bandMorph[1].nodes[1].drive = 3.5f;  // eased into a faster, wilder attractor
        shapeChaos(p.state.bandMorph[1].nodes[1], 0, 8.0f, 0.9f, 0.7f, 0.7f, 0.3f, 0.05f);
        // High: spectral, eased into a harder curve tilted darker
        setBandType(p.state.bandMorph[2], DistortionType::Spectral, 2.0f, 6500.0f);
        shapeSpectral(p.state.bandMorph[2].nodes[0], 1, 2, 0.6f, 0.7f, 0.1f);
        shapeSpectral(p.state.bandMorph[2].nodes[1], 1, 2, 0.9f, 0.3f, 0.3f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(1), 0.25f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandPan(2), 0.3f);
        p.state.modSources.chaosSpeedNorm = 0.15f;
        p.state.modSources.lfo2RateNorm = 0.3f;
        p.state.modSources.lfo2Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // Random Scan: S&H sweep with varied digital degradation per band
        PresetDef p; p.name = "Random Scan"; p.category = "Sweep";
        p.description = "A sample-and-hold sweep jumps between four digital bands: 7-bit Bitcrush, Sample Reduce, "
                        "Quantize and Aliasing, with a sample-and-hold source also stepping two drives. For loops "
                        "and drums.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 1500.0f;
        p.state.crossoverFreqs[2] = 6000.0f;
        enableSweep(p.state, 0.5f, 0.5f, 0.45f);
        enableSweepLFO(p.state, 0.5f, 0.7f, Waveform::SampleAndHold);
        // Low: bitcrush 10-bit with dither
        setBandType(p.state.bandMorph[0], DistortionType::Bitcrush, 1.5f, 2500.0f);
        shapeBitcrush(p.state.bandMorph[0].nodes[0], 7.0f, 0.3f, 0, 0.1f);
        // Lo-mid: sample reduce
        setBandType(p.state.bandMorph[1], DistortionType::SampleReduce, 1.5f, 4000.0f);
        shapeSampleReduce(p.state.bandMorph[1].nodes[0], 8.0f, 0.2f, 1, 0.15f);
        // Hi-mid: quantize
        setBandType(p.state.bandMorph[2], DistortionType::Quantize, 2.0f, 5500.0f);
        shapeQuantize(p.state.bandMorph[2].nodes[0], 0.4f, 0.15f, 0.1f, 0.2f);
        // High: aliasing with freq shift
        setBandType(p.state.bandMorph[3], DistortionType::Aliasing, 1.5f, 7000.0f);
        shapeAliasing(p.state.bandMorph[3].nodes[0], 6.0f, 500.0f, true, 0.15f, 0.3f);
        addRouting(p.state, 0, ModSource::SampleHold, ModDest::bandDrive(0), 0.3f);
        addRouting(p.state, 1, ModSource::SampleHold, ModDest::bandDrive(2), 0.25f);
        p.state.modSources.shRateNorm = 0.4f;
        p.state.modSources.shSlewNorm = 0.3f;
        p.state.bands[1].pan = -0.2f;
        p.state.bands[3].pan = 0.2f;
        presets.push_back(p);
    }
    { // Synced Pulse: tempo-synced square sweep, hard clip + temporal
        PresetDef p; p.name = "Synced Pulse"; p.category = "Sweep";
        p.description = "A tempo-synced square-wave sweep pulses in eighth notes over Hard Clip lows, Temporal "
                        "mids and Ring Saturation highs; transients punch the low drive. For drum loops and "
                        "rhythmic synths.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        enableSweep(p.state, 0.5f, 0.3f, 0.65f);
        p.state.sweep.lfoEnabled = true;
        p.state.sweep.lfoSync = true;
        p.state.sweep.lfoNoteIndex = 10; // 1/8 note (dropdown index)
        p.state.sweep.lfoDepth = 0.7f;
        p.state.sweep.lfoWaveform = Waveform::Square;
        // Low: hard clip with low threshold
        setBandType(p.state.bandMorph[0], DistortionType::HardClip, 3.5f, 2500.0f);
        shapeHardClip(p.state.bandMorph[0].nodes[0], 0.45f, 0.8f);
        // Mid: temporal with envelope mode
        setBandType(p.state.bandMorph[1], DistortionType::Temporal, 3.0f, 5000.0f);
        shapeTemporal(p.state.bandMorph[1].nodes[0], 1, 0.7f, 0.6f, 5.0f, 80.0f, 0.7f, 0, 0.3f);
        // High: ring sat for harmonic content
        setBandType(p.state.bandMorph[2], DistortionType::RingSaturation, 2.0f, 6500.0f);
        shapeRingSat(p.state.bandMorph[2].nodes[0], 0.5f, 1, 0.6f, 2, 0.0f, 1);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandMix(2), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.5f;
        p.state.modSources.lfo1Sync = true;
        p.state.modSources.lfo1NoteIndex = 13; // 1/4 note (dropdown index)
        p.state.modSources.transSensitivity = 0.8f;
        presets.push_back(p);
    }

    // =========================================================================
    // MORPH (15 presets) - Morph system showcases
    // =========================================================================
    { // Soft to Hard: per-band morphing with shape params, LFO-animated
        PresetDef p; p.name = "Soft to Hard"; p.category = "Morph";
        p.description = "LFO 1 slowly moves two morphs together, Soft Clip to Hard Clip on the lows and Tube to "
                        "Fuzz on the mids, over steady Tape highs. Warmth turns to grit and back on any sustained "
                        "part.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        // Low: soft→hard morph with shaped curve/threshold
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::HardClip,
                   2.5f, 4.0f, 2000.0f, 2500.0f);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.8f, 0.2f);
        shapeHardClip(p.state.bandMorph[0].nodes[1], 0.5f, 0.85f);
        // Mid: tube→fuzz morph
        setMorphAB(p.state.bandMorph[1], DistortionType::Tube, DistortionType::Fuzz,
                   2.0f, 4.0f, 4000.0f, 5000.0f);
        shapeTube(p.state.bandMorph[1].nodes[0], 0.1f, 0.5f, 1);
        shapeFuzz(p.state.bandMorph[1].nodes[1], 0.15f, 0, 1.0f, 0.3f);
        // High: tape stays as anchor
        setBandType(p.state.bandMorph[2], DistortionType::Tape, 1.5f, 6500.0f);
        shapeTape(p.state.bandMorph[2].nodes[0], 0.0f, 0.2f, 0.7f, 1, 0.4f, 0.1f);
        p.state.bandMorph[0].morphX = 0.3f;
        p.state.bandMorph[1].morphX = 0.3f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.4f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandMorphX(1), 0.4f);
        p.state.modSources.lfo1RateNorm = 0.25f;
        p.state.modSources.lfo1Shape = Waveform::Triangle;
        presets.push_back(p);
    }
    { // Tube to Fuzz: 3-band, each band morphs between different pairs
        PresetDef p; p.name = "Tube to Fuzz"; p.category = "Morph";
        p.description = "Play harder and the morphs move: the lows go from Tube to Asymmetric Fuzz and the mids "
                        "from Fuzz to Hard Clip as the input level rises, over Soft Clip highs. For guitar and "
                        "bass.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 3000.0f;
        // Low: tube→asym fuzz
        setMorphAB(p.state.bandMorph[0], DistortionType::Tube, DistortionType::AsymmetricFuzz,
                   3.0f, 5.0f, 1800.0f, 2500.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.2f, 0.7f, 2);
        shapeAsymFuzz(p.state.bandMorph[0].nodes[1], 0.4f, 0.7f, 1, 0.1f, 0.7f);
        // Mid: fuzz→hard clip
        setMorphAB(p.state.bandMorph[1], DistortionType::Fuzz, DistortionType::HardClip,
                   4.0f, 4.5f, 4000.0f, 5000.0f);
        shapeFuzz(p.state.bandMorph[1].nodes[0], 0.1f, 0, 0.0f, 0.4f);
        shapeHardClip(p.state.bandMorph[1].nodes[1], 0.55f, 0.9f);
        // High: soft clip stays warm
        setBandType(p.state.bandMorph[2], DistortionType::SoftClip, 2.0f, 6000.0f);
        shapeSoftClip(p.state.bandMorph[2].nodes[0], 0.6f, 0.35f);
        p.state.bandMorph[0].morphX = 0.5f;
        p.state.bandMorph[1].morphX = 0.5f;
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandMorphX(0), 0.5f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandMorphX(1), 0.4f);
        p.state.modSources.envAttackNorm = 0.03f;
        p.state.modSources.envReleaseNorm = 0.25f;
        p.state.modSources.envSensitivity = 0.8f;
        presets.push_back(p);
    }
    { // Four Corners: 2D morph pad across 4 distortion families
        PresetDef p; p.name = "Four Corners"; p.category = "Morph";
        p.description = "A four-node XY morph on the low band between Soft Clip, Hard Clip, Tube and Fuzz, steered "
                        "by two slow LFOs, under Tape mids and Formant highs.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        // Low: 4-node 2D morph — analog flavors
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::SoftClip, DistortionType::HardClip,
            DistortionType::Tube, DistortionType::Fuzz);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.7f, 0.3f);
        shapeHardClip(p.state.bandMorph[0].nodes[1], 0.5f, 0.9f);
        shapeTube(p.state.bandMorph[0].nodes[2], 0.1f, 0.6f, 2);
        shapeFuzz(p.state.bandMorph[0].nodes[3], 0.2f, 0, 1.0f, 0.3f);
        p.state.bandMorph[0].nodes[0].toneHz = 2500.0f;
        p.state.bandMorph[0].nodes[1].toneHz = 3500.0f;
        p.state.bandMorph[0].nodes[2].toneHz = 2000.0f;
        p.state.bandMorph[0].nodes[3].toneHz = 4000.0f;
        p.state.bandMorph[0].morphX = 0.5f;
        p.state.bandMorph[0].morphY = 0.5f;
        // Mid: tape warmth
        setBandType(p.state.bandMorph[1], DistortionType::Tape, 2.0f, 4500.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.0f, 0.4f, 0.6f, 1, 0.5f, 0.15f);
        // High: formant
        setBandType(p.state.bandMorph[2], DistortionType::Formant, 1.5f, 7000.0f);
        shapeFormant(p.state.bandMorph[2].nodes[0], 3, 1.0f, 0.5f, 0.6f, 0.5f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.3f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandMorphY(0), 0.3f);
        p.state.modSources.lfo1RateNorm = 0.12f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.lfo2RateNorm = 0.18f;
        p.state.modSources.lfo2Shape = Waveform::Triangle;
        presets.push_back(p);
    }
    { // Radial Blend: radial 2D morph, tape/fold/crush/chaos
        PresetDef p; p.name = "Radial Blend"; p.category = "Morph";
        p.description = "A radial four-node morph on the lows (Tape, Sine Fold, Bitcrush, Chaos) wandered by a "
                        "Lorenz chaos source, with Allpass Resonant mids and Stochastic highs.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        // Low: 4-node radial
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::Tape, DistortionType::SineFold,
            DistortionType::Bitcrush, DistortionType::Chaos, MorphMode::Radial2D);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.1f, 0.5f, 0.4f, 1, 0.6f, 0.3f);
        shapeSineFold(p.state.bandMorph[0].nodes[1], 4.0f, 0.2f, 0.4f);
        shapeBitcrush(p.state.bandMorph[0].nodes[2], 8.0f, 0.2f, 0, 0.15f);
        shapeChaos(p.state.bandMorph[0].nodes[3], 0, 3.0f, 0.5f, 0.4f, 0.5f, 0.5f);
        p.state.bandMorph[0].nodes[0].toneHz = 2000.0f;
        p.state.bandMorph[0].nodes[1].toneHz = 5000.0f;
        p.state.bandMorph[0].nodes[2].toneHz = 3000.0f;
        p.state.bandMorph[0].nodes[3].toneHz = 4500.0f;
        p.state.bandMorph[0].morphX = 0.5f;
        p.state.bandMorph[0].morphY = 0.5f;
        // Mid: allpass resonant
        setBandType(p.state.bandMorph[1], DistortionType::AllpassResonant, 2.0f, 5000.0f);
        shapeAllpass(p.state.bandMorph[1].nodes[0], 1, 500.0f, 0.7f, 1.5f, 0.5f, 2);
        // High: stochastic shimmer
        setBandType(p.state.bandMorph[2], DistortionType::Stochastic, 1.5f, 6500.0f);
        shapeStochastic(p.state.bandMorph[2].nodes[0], 1, 0.15f, 15.0f, 0.08f, 0.1f, 1, 0.6f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandMorphX(0), 0.4f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandMorphY(0), 0.4f);
        p.state.modSources.chaosSpeedNorm = 0.15f;
        p.state.modSources.chaosModel = ChaosModel::Lorenz;
        p.state.bands[1].pan = -0.15f;
        p.state.bands[2].pan = 0.15f;
        presets.push_back(p);
    }
    { // Tape Fold Morph: smooth morphing between tape and wavefold per band
        PresetDef p; p.name = "Tape Fold Morph"; p.category = "Morph";
        p.description = "Every band morphs from Tape into a wavefolder (Serge, Sine, Triangle from low to high), "
                        "all moved by one slow sine LFO with smoothed morphing. Warm to metallic, for synths and "
                        "pads.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 3000.0f;
        // Low: tape→serge fold
        setMorphAB(p.state.bandMorph[0], DistortionType::Tape, DistortionType::SergeFold,
                   2.0f, 2.5f, 1800.0f, 3000.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.0f, 0.5f, 0.5f, 1, 0.6f, 0.3f);
        shapeSergeFold(p.state.bandMorph[0].nodes[1], 3.0f, 0.1f, 1, 0.0f, 0.3f, 0.1f);
        // Mid: tape→sine fold
        setMorphAB(p.state.bandMorph[1], DistortionType::Tape, DistortionType::SineFold,
                   2.5f, 3.0f, 3500.0f, 5000.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.05f, 0.4f, 0.6f, 0, 0.5f, 0.2f);
        shapeSineFold(p.state.bandMorph[1].nodes[1], 3.5f, -0.2f, 0.5f, 0.0f, 0.15f);
        // High: tape→triangle fold
        setMorphAB(p.state.bandMorph[2], DistortionType::Tape, DistortionType::TriangleFold,
                   1.5f, 2.0f, 5000.0f, 6500.0f);
        shapeTape(p.state.bandMorph[2].nodes[0], 0.0f, 0.3f, 0.8f, 1, 0.3f, 0.1f);
        shapeTriFold(p.state.bandMorph[2].nodes[1], 2.5f, 0.0f, 0.6f, 0.0f, 0.2f);
        p.state.bandMorph[0].morphSmoothing = 80.0f;
        p.state.bandMorph[1].morphSmoothing = 80.0f;
        p.state.bandMorph[2].morphSmoothing = 80.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.5f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandMorphX(1), 0.4f);
        addRouting(p.state, 2, ModSource::LFO1, ModDest::bandMorphX(2), 0.3f);
        p.state.modSources.lfo1RateNorm = 0.15f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // Digital Organic: 4-node morphing between digital + analog
        PresetDef p; p.name = "Digital Organic"; p.category = "Morph";
        p.description = "The lows roam a four-node XY morph of Bitcrush, Soft Clip, Sample Reduce and Tube on two "
                        "slow LFOs, under a Granular mid band and Spectral highs. Digital grit against analog "
                        "warmth.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        // Low: 4-node crush/soft/reduce/tube
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::Bitcrush, DistortionType::SoftClip,
            DistortionType::SampleReduce, DistortionType::Tube);
        shapeBitcrush(p.state.bandMorph[0].nodes[0], 8.0f, 0.15f, 1, 0.1f);
        shapeSoftClip(p.state.bandMorph[0].nodes[1], 0.7f, 0.3f);
        shapeSampleReduce(p.state.bandMorph[0].nodes[2], 10.0f, 0.1f, 0, 0.2f);
        shapeTube(p.state.bandMorph[0].nodes[3], 0.1f, 0.5f, 1);
        p.state.bandMorph[0].nodes[0].toneHz = 3000.0f;
        p.state.bandMorph[0].nodes[1].toneHz = 2500.0f;
        p.state.bandMorph[0].nodes[2].toneHz = 2000.0f;
        p.state.bandMorph[0].nodes[3].toneHz = 2000.0f;
        // Mid: granular texture
        setBandType(p.state.bandMorph[1], DistortionType::Granular, 2.0f, 5000.0f);
        shapeGranular(p.state.bandMorph[1].nodes[0], 30.0f, 0.6f, 0.2f, 0.15f, 0.3f, 0.5f, 1, 1);
        // High: spectral
        setBandType(p.state.bandMorph[2], DistortionType::Spectral, 1.5f, 7000.0f);
        shapeSpectral(p.state.bandMorph[2].nodes[0], 1, 1, 0.6f, 0.4f, 0.1f, 1);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.35f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandMorphY(0), 0.35f);
        addRouting(p.state, 2, ModSource::Random, ModDest::bandDrive(1), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.1f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        p.state.modSources.lfo2RateNorm = 0.14f;
        p.state.modSources.lfo2Shape = Waveform::Triangle;
        p.state.modSources.randomRateNorm = 0.2f;
        p.state.modSources.randomSmoothness = 0.7f;
        presets.push_back(p);
    }
    { // Chaos to Order: chaos→soft clip morph, reactive to dynamics
        PresetDef p; p.name = "Chaos to Order"; p.category = "Morph";
        p.description = "Each band morphs from a wild type (Chaos, Fractal, Stochastic) to a tame one (Tube, Soft "
                        "Clip, Tape). It rests on the tame side and louder input pushes it further toward order.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        // Low: chaos→tube morph
        setMorphAB(p.state.bandMorph[0], DistortionType::Chaos, DistortionType::Tube,
                   2.5f, 6.0f, 2000.0f, 1800.0f);
        shapeChaos(p.state.bandMorph[0].nodes[0], 0, 3.0f, 0.6f, 0.5f, 0.5f, 0.4f, 0.15f);
        shapeTube(p.state.bandMorph[0].nodes[1], 0.0f, 0.4f, 1);
        // Mid: fractal→soft clip
        setMorphAB(p.state.bandMorph[1], DistortionType::Fractal, DistortionType::SoftClip,
                   2.0f, 1.5f, 4000.0f, 4500.0f);
        shapeFractal(p.state.bandMorph[1].nodes[0], 1, 5, 0.5f, 0.6f, 0.5f, 0.1f);
        shapeSoftClip(p.state.bandMorph[1].nodes[1], 0.7f, 0.3f);
        // High: stochastic→tape
        setMorphAB(p.state.bandMorph[2], DistortionType::Stochastic, DistortionType::Tape,
                   1.5f, 1.5f, 5500.0f, 6000.0f);
        shapeStochastic(p.state.bandMorph[2].nodes[0], 2, 0.3f, 20.0f, 0.15f, 0.2f, 0, 0.5f);
        shapeTape(p.state.bandMorph[2].nodes[1], 0.0f, 0.2f, 0.7f, 1, 0.4f, 0.1f);
        p.state.bandMorph[0].morphX = 0.7f;
        p.state.bandMorph[1].morphX = 0.7f;
        p.state.bandMorph[2].morphX = 0.7f;
        // Envelope follower pushes toward order when loud
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandMorphX(0), 0.4f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandMorphX(1), 0.4f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandMorphX(2), 0.3f);
        p.state.modSources.envSensitivity = 0.8f;
        p.state.modSources.envAttackNorm = 0.02f;
        p.state.modSources.envReleaseNorm = 0.3f;
        presets.push_back(p);
    }
    { // Three Node Blend: 3-node morph on each band with different flavors
        PresetDef p; p.name = "Three Node Blend"; p.category = "Morph";
        p.description = "Three-node morphs on two bands, Tube, Fuzz and Tape on the lows and Sine Fold, Chaos and "
                        "Allpass Resonant on the mids, drifting on slow LFOs over Soft Clip highs.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3000.0f;
        // Low: tube/fuzz/tape 3-node
        auto& bm0 = p.state.bandMorph[0];
        setNode(bm0.nodes[0], DistortionType::Tube, 2.5f, 2000.0f);
        setNode(bm0.nodes[1], DistortionType::Fuzz, 4.0f, 3500.0f);
        setNode(bm0.nodes[2], DistortionType::Tape, 2.0f, 2200.0f);
        shapeTube(bm0.nodes[0], 0.15f, 0.6f, 2);
        shapeFuzz(bm0.nodes[1], 0.1f, 0, 1.0f, 0.3f);
        shapeTape(bm0.nodes[2], 0.0f, 0.5f, 0.5f, 1, 0.6f, 0.25f);
        bm0.activeNodeCount = 3;
        bm0.morphMode = MorphMode::Planar2D;
        // Mid: sinefold/chaos/allpass 3-node
        auto& bm1 = p.state.bandMorph[1];
        setNode(bm1.nodes[0], DistortionType::SineFold, 3.0f, 5000.0f);
        setNode(bm1.nodes[1], DistortionType::Chaos, 2.0f, 4500.0f);
        setNode(bm1.nodes[2], DistortionType::AllpassResonant, 2.0f, 5500.0f);
        shapeSineFold(bm1.nodes[0], 3.5f, 0.2f, 0.3f);
        shapeChaos(bm1.nodes[1], 1, 4.0f, 0.5f, 0.4f);
        shapeAllpass(bm1.nodes[2], 1, 700.0f, 0.6f, 2.0f);
        bm1.activeNodeCount = 3;
        bm1.morphMode = MorphMode::Planar2D;
        // High: soft clip anchor
        setBandType(p.state.bandMorph[2], DistortionType::SoftClip, 1.5f, 7000.0f);
        shapeSoftClip(p.state.bandMorph[2].nodes[0], 0.6f, 0.3f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.3f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandMorphY(0), 0.25f);
        addRouting(p.state, 2, ModSource::LFO1, ModDest::bandMorphX(1), 0.25f);
        p.state.modSources.lfo1RateNorm = 0.08f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        p.state.modSources.lfo2RateNorm = 0.12f;
        p.state.modSources.lfo2Shape = Waveform::Triangle;
        presets.push_back(p);
    }
    { // Rectify Blend: full→half rectify morph + ring sat on mid band
        PresetDef p; p.name = "Rectify Blend"; p.category = "Morph";
        p.description = "A slow square LFO flips the lows between octave-up Full Rectify and Half Rectify every "
                        "few seconds, under a Ring Saturation mid band whose drive LFO 2 swells, with Soft Clip on "
                        "top.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        // Low: full→half rectify
        setMorphAB(p.state.bandMorph[0], DistortionType::FullRectify, DistortionType::HalfRectify,
                   1.5f, 1.5f, 1800.0f, 2200.0f);
        shapeFullRect(p.state.bandMorph[0].nodes[0], 0.15f);
        shapeHalfRect(p.state.bandMorph[0].nodes[1], 0.4f, 0.2f);
        // Mid: ring sat with modulation
        setBandType(p.state.bandMorph[1], DistortionType::RingSaturation, 2.5f, 5000.0f);
        shapeRingSat(p.state.bandMorph[1].nodes[0], 0.7f, 2, 0.5f, 1, 0.1f, 2);
        // High: soft clip to tame
        setBandType(p.state.bandMorph[2], DistortionType::SoftClip, 1.5f, 6500.0f);
        shapeSoftClip(p.state.bandMorph[2].nodes[0], 0.65f, 0.4f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.5f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandDrive(1), 0.3f);
        p.state.modSources.lfo1RateNorm = 0.3f;
        p.state.modSources.lfo1Shape = Waveform::Square;
        p.state.modSources.lfo2RateNorm = 0.45f;
        p.state.modSources.lfo2Shape = Waveform::Sine;
        p.state.bands[1].pan = -0.2f;
        p.state.bands[2].pan = 0.2f;
        presets.push_back(p);
    }
    { // Feedback Explorer: feedback dist vs allpass, different topologies per band
        PresetDef p; p.name = "Feedback Explorer"; p.category = "Morph";
        p.description = "Lows and mids both morph between Feedback Distortion and Allpass Resonant, moved by one "
                        "slow LFO in opposite directions, so one band rings while the other feeds back. Tape on "
                        "top.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        // Low: feedback→allpass morph with long delay
        setMorphAB(p.state.bandMorph[0], DistortionType::FeedbackDist, DistortionType::AllpassResonant,
                   2.5f, 2.5f, 2000.0f, 2500.0f);
        shapeFeedback(p.state.bandMorph[0].nodes[0], 0.8f, 20.0f, 0.6f, 1, 0.3f, 2, true, 0.7f);
        shapeAllpass(p.state.bandMorph[0].nodes[1], 0, 300.0f, 0.75f, 2.5f, 0.5f, 3, false, 0.4f);
        // Mid: feedback→allpass morph with short delay
        setMorphAB(p.state.bandMorph[1], DistortionType::FeedbackDist, DistortionType::AllpassResonant,
                   2.0f, 2.0f, 4500.0f, 5000.0f);
        shapeFeedback(p.state.bandMorph[1].nodes[0], 0.6f, 5.0f, 0.4f, 2, 0.6f, 1);
        shapeAllpass(p.state.bandMorph[1].nodes[1], 2, 800.0f, 0.6f, 1.0f, 0.4f, 2, true, 0.3f);
        // High: tape glue
        setBandType(p.state.bandMorph[2], DistortionType::Tape, 1.5f, 7000.0f);
        shapeTape(p.state.bandMorph[2].nodes[0], 0.0f, 0.2f, 0.8f, 1, 0.3f, 0.1f);
        p.state.bandMorph[0].morphSmoothing = 120.0f;
        p.state.bandMorph[1].morphSmoothing = 120.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.4f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandMorphX(1), -0.4f); // inverse
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandDrive(0), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.1f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.chaosSpeedNorm = 0.1f;
        presets.push_back(p);
    }
    { // Multi Band Morph: 4 bands, each morphing between completely different pairs
        PresetDef p; p.name = "Multi Band Morph"; p.category = "Morph";
        p.description = "Four bands, each morphing between its own pair (Tube and Feedback, Fuzz and Sine Fold, "
                        "Tape and Chaos, Bitcrush and Spectral) on its own source: LFO 1, LFO 2, chaos and random.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 150.0f;
        p.state.crossoverFreqs[1] = 1200.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        // Sub: tube→feedback dist
        setMorphAB(p.state.bandMorph[0], DistortionType::Tube, DistortionType::FeedbackDist,
                   2.5f, 3.0f, 1500.0f, 2000.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.15f, 0.6f, 2);
        shapeFeedback(p.state.bandMorph[0].nodes[1], 0.6f, 12.0f, 0.5f, 0, 0.3f, 1);
        // Lo-mid: fuzz→sine fold
        setMorphAB(p.state.bandMorph[1], DistortionType::Fuzz, DistortionType::SineFold,
                   3.5f, 2.5f, 3500.0f, 4500.0f);
        shapeFuzz(p.state.bandMorph[1].nodes[0], 0.2f, 0, 1.0f, 0.3f);
        shapeSineFold(p.state.bandMorph[1].nodes[1], 3.0f, 0.15f, 0.4f);
        // Hi-mid: tape→chaos
        setMorphAB(p.state.bandMorph[2], DistortionType::Tape, DistortionType::Chaos,
                   2.0f, 2.0f, 5000.0f, 5500.0f);
        shapeTape(p.state.bandMorph[2].nodes[0], 0.0f, 0.4f, 0.6f, 1, 0.5f, 0.2f);
        shapeChaos(p.state.bandMorph[2].nodes[1], 2, 4.0f, 0.5f, 0.4f, 0.5f, 0.4f, 0.2f);
        // High: bitcrush→spectral
        setMorphAB(p.state.bandMorph[3], DistortionType::Bitcrush, DistortionType::Spectral,
                   1.5f, 2.0f, 6500.0f, 7000.0f);
        shapeBitcrush(p.state.bandMorph[3].nodes[0], 10.0f, 0.2f, 0, 0.1f);
        shapeSpectral(p.state.bandMorph[3].nodes[1], 2, 1, 0.5f, 0.6f, 0.1f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.3f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandMorphX(1), 0.35f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandMorphX(2), 0.25f);
        addRouting(p.state, 3, ModSource::Random, ModDest::bandMorphX(3), 0.3f);
        p.state.modSources.lfo1RateNorm = 0.12f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.lfo2RateNorm = 0.18f;
        p.state.modSources.lfo2Shape = Waveform::Triangle;
        p.state.modSources.chaosSpeedNorm = 0.1f;
        p.state.modSources.randomRateNorm = 0.15f;
        p.state.modSources.randomSmoothness = 0.6f;
        p.state.bands[0].pan = 0.0f;
        p.state.bands[1].pan = -0.2f;
        p.state.bands[2].pan = 0.2f;
        p.state.bands[3].pan = -0.1f;
        presets.push_back(p);
    }
    { // Smooth Crossfade: very smooth morph, soft clip→granular→tape
        PresetDef p; p.name = "Smooth Crossfade"; p.category = "Morph";
        p.description = "Two bands split at 500 Hz, Soft Clip to Tape below and Granular to Formant above, "
                        "crossfaded in opposite directions by one very slow LFO with heavy smoothing. Gentle; for "
                        "pads.";
        p.state = makeInitState(2);
        p.state.crossoverFreqs[0] = 500.0f;
        // Low: soft→tape morph with high smoothing
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::Tape,
                   1.5f, 2.0f, 2500.0f, 2000.0f);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.75f, 0.25f);
        shapeTape(p.state.bandMorph[0].nodes[1], 0.05f, 0.5f, 0.5f, 1, 0.6f, 0.25f);
        p.state.bandMorph[0].morphSmoothing = 250.0f;
        p.state.bandMorph[0].morphX = 0.5f;
        // High: granular→formant
        setMorphAB(p.state.bandMorph[1], DistortionType::Granular, DistortionType::Formant,
                   1.5f, 1.5f, 5000.0f, 6000.0f);
        shapeGranular(p.state.bandMorph[1].nodes[0], 40.0f, 0.5f, 0.1f, 0.1f, 0.2f, 0.6f, 2, 1);
        shapeFormant(p.state.bandMorph[1].nodes[1], 1, 2.0f, 0.5f, 0.6f, 0.5f, 1, 0.4f, 0.6f);
        p.state.bandMorph[1].morphSmoothing = 250.0f;
        p.state.bandMorph[1].morphX = 0.5f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.3f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandMorphX(1), -0.3f);
        p.state.modSources.lfo1RateNorm = 0.05f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // Wavefolder Quad: 4-node morph across all 3 wavefolder types + chaos
        PresetDef p; p.name = "Wavefolder Quad"; p.category = "Morph";
        p.description = "An XY morph on the lows between all three wavefolders (Sine, Triangle, Serge) and Chaos "
                        "on two slow LFOs, with the input level adding drive; Tube mids and Ring Saturation highs.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        // Low: 4-node wavefolder morph
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::SineFold, DistortionType::TriangleFold,
            DistortionType::SergeFold, DistortionType::Chaos);
        shapeSineFold(p.state.bandMorph[0].nodes[0], 4.0f, 0.3f, 0.5f, 0.1f, 0.1f);
        shapeTriFold(p.state.bandMorph[0].nodes[1], 3.5f, -0.2f, 0.7f, 0.0f, 0.15f);
        shapeSergeFold(p.state.bandMorph[0].nodes[2], 5.0f, 0.0f, 2, 0.0f, 0.4f, 0.1f);
        shapeChaos(p.state.bandMorph[0].nodes[3], 2, 5.0f, 0.5f, 0.4f);
        p.state.bandMorph[0].nodes[0].drive = 2.5f;
        p.state.bandMorph[0].nodes[1].drive = 2.5f;
        p.state.bandMorph[0].nodes[2].drive = 3.0f;
        p.state.bandMorph[0].nodes[3].drive = 2.0f;
        p.state.bandMorph[0].nodes[0].toneHz = 2500.0f;
        p.state.bandMorph[0].nodes[1].toneHz = 3000.0f;
        p.state.bandMorph[0].nodes[2].toneHz = 2800.0f;
        p.state.bandMorph[0].nodes[3].toneHz = 3500.0f;
        // Mid: tube anchor
        setBandType(p.state.bandMorph[1], DistortionType::Tube, 2.0f, 4500.0f);
        shapeTube(p.state.bandMorph[1].nodes[0], 0.1f, 0.5f, 1);
        // High: ring saturation for shimmer
        setBandType(p.state.bandMorph[2], DistortionType::RingSaturation, 1.5f, 6500.0f);
        shapeRingSat(p.state.bandMorph[2].nodes[0], 0.5f, 1, 0.4f, 2, 0.0f, 1);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.35f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandMorphY(0), 0.35f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandDrive(0), 0.25f);
        p.state.modSources.lfo1RateNorm = 0.1f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        p.state.modSources.lfo2RateNorm = 0.15f;
        p.state.modSources.lfo2Shape = Waveform::Sine;
        p.state.modSources.envSensitivity = 0.6f;
        presets.push_back(p);
    }
    { // Spectral Morph: spectral→granular morph on mids, fractal on lows
        PresetDef p; p.name = "Spectral Morph"; p.category = "Morph";
        p.description = "A slow LFO morphs the mids from Spectral to Granular, between a Fractal low band that a "
                        "random source drives and Stochastic highs that LFO 2 pans. Textural; for pads and drones.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        // Low: fractal distortion
        setBandType(p.state.bandMorph[0], DistortionType::Fractal, 2.5f, 2500.0f);
        shapeFractal(p.state.bandMorph[0].nodes[0], 2, 5, 0.5f, 0.6f, 0.5f, 0.15f, 1, 0.6f);
        // Mid: spectral→granular morph
        setMorphAB(p.state.bandMorph[1], DistortionType::Spectral, DistortionType::Granular,
                   2.0f, 2.5f, 5000.0f, 4500.0f);
        shapeSpectral(p.state.bandMorph[1].nodes[0], 2, 2, 0.6f, 0.4f, 0.15f, 1, 0.6f);
        shapeGranular(p.state.bandMorph[1].nodes[1], 25.0f, 0.7f, 0.3f, 0.2f, 0.4f, 0.5f, 1, 2);
        p.state.bandMorph[1].morphSmoothing = 60.0f;
        // High: stochastic shimmer
        setBandType(p.state.bandMorph[2], DistortionType::Stochastic, 1.5f, 7000.0f);
        shapeStochastic(p.state.bandMorph[2].nodes[0], 1, 0.15f, 12.0f, 0.08f, 0.1f, 1, 0.7f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(1), 0.5f);
        addRouting(p.state, 1, ModSource::Random, ModDest::bandDrive(0), 0.2f);
        addRouting(p.state, 2, ModSource::LFO2, ModDest::bandPan(2), 0.25f);
        p.state.modSources.lfo1RateNorm = 0.08f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.lfo2RateNorm = 0.2f;
        p.state.modSources.lfo2Shape = Waveform::Triangle;
        p.state.modSources.randomRateNorm = 0.1f;
        p.state.modSources.randomSmoothness = 0.8f;
        p.state.bands[2].pan = 0.15f;
        presets.push_back(p);
    }
    { // Formant Shift: formant→allpass morph with vowel shifting
        PresetDef p; p.name = "Formant Shift"; p.category = "Morph";
        p.description = "The mids morph from a Formant vowel to Allpass Resonant ringing on a slow LFO, with the "
                        "input pitch adding drive, between Tube lows and a second Formant on top. For vocals and "
                        "leads.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        // Low: tube anchor
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 2.0f, 2000.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.1f, 0.5f, 1);
        // Mid: formant→allpass morph
        setMorphAB(p.state.bandMorph[1], DistortionType::Formant, DistortionType::AllpassResonant,
                   2.5f, 2.0f, 5000.0f, 5500.0f);
        shapeFormant(p.state.bandMorph[1].nodes[0], 0, 5.0f, 0.6f, 0.7f, 0.4f, 2, 0.3f, 0.6f);
        shapeAllpass(p.state.bandMorph[1].nodes[1], 1, 600.0f, 0.7f, 2.0f, 0.5f, 2, true, 0.3f);
        p.state.bandMorph[1].morphSmoothing = 100.0f;
        // High: formant with different vowel
        setBandType(p.state.bandMorph[2], DistortionType::Formant, 1.5f, 7000.0f);
        shapeFormant(p.state.bandMorph[2].nodes[0], 3, -3.0f, 0.5f, 0.5f, 0.6f, 1, 0.7f, 0.4f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(1), 0.5f);
        addRouting(p.state, 1, ModSource::PitchFollower, ModDest::bandDrive(1), 0.3f);
        addRouting(p.state, 2, ModSource::LFO2, ModDest::bandPan(1), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.12f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.lfo2RateNorm = 0.25f;
        p.state.modSources.lfo2Shape = Waveform::Triangle;
        p.state.modSources.pitchConfidence = 0.7f;
        p.state.bands[1].pan = -0.1f;
        p.state.bands[2].pan = 0.1f;
        presets.push_back(p);
    }

    // =========================================================================
    // BASS (10 presets) - Low-end optimized
    // =========================================================================
    { // Warm Bass: tube subs, tape low-mids, soft clip highs
        PresetDef p; p.name = "Warm Bass"; p.category = "Bass";
        p.description = "Tube with heavy sag below 100 Hz, Tape up to 400 Hz and gentle Soft Clip above, turned "
                        "down 3 dB; the sub drive rises with the input level. Round, warm bass.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 100.0f;
        p.state.crossoverFreqs[1] = 400.0f;
        // Sub: tube with heavy sag
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 3.0f, 1500.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.1f, 0.7f, 2);
        // Low-mid: tape for body
        setBandType(p.state.bandMorph[1], DistortionType::Tape, 2.5f, 2500.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.05f, 0.5f, 0.4f, 1, 0.7f, 0.2f);
        // Upper: gentle soft clip to define
        setBandType(p.state.bandMorph[2], DistortionType::SoftClip, 1.5f, 5000.0f);
        shapeSoftClip(p.state.bandMorph[2].nodes[0], 0.7f, 0.25f);
        p.state.bands[0].gainDb = 2.0f;
        p.state.bands[2].gainDb = -3.0f;
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.25f);
        p.state.modSources.envSensitivity = 0.6f;
        presets.push_back(p);
    }
    { // Subharmonic Growl: fuzz subs, feedback dist mids, hard clip presence
        PresetDef p; p.name = "Subharmonic Growl"; p.category = "Bass";
        p.description = "Asymmetric Fuzz on everything below 80 Hz, Feedback Distortion to 350 Hz for growl and "
                        "Hard Clip above for attack; the input level pushes the sub harder. For synth and electric "
                        "bass.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 80.0f;
        p.state.crossoverFreqs[1] = 350.0f;
        // Sub: asymmetric fuzz for subharmonic generation
        setBandType(p.state.bandMorph[0], DistortionType::AsymmetricFuzz, 4.5f, 1000.0f);
        shapeAsymFuzz(p.state.bandMorph[0].nodes[0], 0.4f, 0.8f, 1, 0.0f, 0.7f);
        // Low-mid: feedback dist for growl
        setBandType(p.state.bandMorph[1], DistortionType::FeedbackDist, 3.0f, 2500.0f);
        shapeFeedback(p.state.bandMorph[1].nodes[0], 0.6f, 10.0f, 0.7f, 0, 0.3f, 1);
        // Upper: hard clip for punch
        setBandType(p.state.bandMorph[2], DistortionType::HardClip, 2.5f, 4000.0f);
        shapeHardClip(p.state.bandMorph[2].nodes[0], 0.5f, 0.85f);
        p.state.bands[0].gainDb = 3.0f;
        p.state.bands[2].gainDb = -2.0f;
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.35f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandDrive(1), 0.15f);
        p.state.modSources.envSensitivity = 0.7f;
        p.state.modSources.lfo1RateNorm = 0.3f;
        p.state.modSources.lfo1Shape = Waveform::Triangle;
        presets.push_back(p);
    }
    { // Tape Warmth: tape on subs with flutter, tube mids, allpass sparkle
        PresetDef p; p.name = "Tape Warmth"; p.category = "Bass";
        p.description = "Slow, fluttering Tape on the lows, Tube from 150 to 600 Hz and a quiet Allpass Resonant "
                        "top band, with a very slow LFO breathing the low drive. Soft, saturated bass.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 150.0f;
        p.state.crossoverFreqs[1] = 600.0f;
        // Sub: tape with slow speed and flutter
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 2.5f, 1200.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.05f, 0.6f, 0.3f, 1, 0.8f, 0.35f);
        // Low-mid: tube
        setBandType(p.state.bandMorph[1], DistortionType::Tube, 2.0f, 3000.0f);
        shapeTube(p.state.bandMorph[1].nodes[0], 0.0f, 0.5f, 1);
        // Upper: allpass for sparkle
        setBandType(p.state.bandMorph[2], DistortionType::AllpassResonant, 1.0f, 5500.0f);
        shapeAllpass(p.state.bandMorph[2].nodes[0], 0, 400.0f, 0.5f, 1.5f, 0.4f, 1);
        p.state.bands[0].gainDb = 1.5f;
        p.state.bands[2].gainDb = -4.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.1f, ModCurve::SCurve);
        p.state.modSources.lfo1RateNorm = 0.06f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // Dirty Sub: extreme sub processing, asym fuzz + bitcrush
        PresetDef p; p.name = "Dirty Sub"; p.category = "Bass";
        p.description = "Asymmetric Fuzz below 60 Hz, 7-bit Bitcrush from 60 to 250 Hz and Tape above; the input "
                        "level drives the sub harder and a slow square LFO pulses the crushed band mix. Gritty sub "
                        "bass.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 60.0f;
        p.state.crossoverFreqs[1] = 250.0f;
        // Sub: asym fuzz for gnarly harmonics
        setBandType(p.state.bandMorph[0], DistortionType::AsymmetricFuzz, 4.0f, 800.0f);
        shapeAsymFuzz(p.state.bandMorph[0].nodes[0], 0.5f, 0.7f, 0, 0.05f, 0.8f);
        // Low: bitcrush for grit
        setBandType(p.state.bandMorph[1], DistortionType::Bitcrush, 2.0f, 2000.0f);
        shapeBitcrush(p.state.bandMorph[1].nodes[0], 7.0f, 0.1f, 1, 0.05f);
        // Mid: tape to glue
        setBandType(p.state.bandMorph[2], DistortionType::Tape, 1.5f, 4000.0f);
        shapeTape(p.state.bandMorph[2].nodes[0], 0.0f, 0.3f, 0.5f, 0, 0.6f, 0.1f);
        p.state.bands[0].gainDb = 4.0f;
        p.state.bands[2].gainDb = -3.0f;
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.3f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandMix(1), 0.15f);
        p.state.modSources.envSensitivity = 0.75f;
        p.state.modSources.lfo1RateNorm = 0.4f;
        p.state.modSources.lfo1Shape = Waveform::Square;
        presets.push_back(p);
    }
    { // Wavefold Bass: sine fold on subs, serge mids, soft clip highs
        PresetDef p; p.name = "Wavefold Bass"; p.category = "Bass";
        p.description = "Gentle Sine Fold below 120 Hz and Serge Fold to 500 Hz add upper harmonics to the low "
                        "end, with Soft Clip on top; LFO 1 and the input level move the two fold drives.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 120.0f;
        p.state.crossoverFreqs[1] = 500.0f;
        // Sub: controlled sine fold
        setBandType(p.state.bandMorph[0], DistortionType::SineFold, 2.5f, 1500.0f);
        shapeSineFold(p.state.bandMorph[0].nodes[0], 2.0f, 0.0f, 0.2f, 0.0f, 0.15f);
        // Low-mid: serge fold
        setBandType(p.state.bandMorph[1], DistortionType::SergeFold, 2.0f, 3000.0f);
        shapeSergeFold(p.state.bandMorph[1].nodes[0], 2.5f, 0.1f, 1, 0.0f, 0.3f, 0.1f);
        // Upper: soft clip for clarity
        setBandType(p.state.bandMorph[2], DistortionType::SoftClip, 1.5f, 5000.0f);
        shapeSoftClip(p.state.bandMorph[2].nodes[0], 0.6f, 0.35f);
        p.state.bands[0].gainDb = 2.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.15f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandDrive(1), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.2f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.envSensitivity = 0.6f;
        presets.push_back(p);
    }
    { // Punchy Low End: hard clip subs, temporal mids, tube upper
        PresetDef p; p.name = "Punchy Low End"; p.category = "Bass";
        p.description = "Hard Clip under 100 Hz that the transient detector pushes on every attack, Temporal "
                        "distortion to 400 Hz at 70 % mix that each attack opens fully, and Tube on top. For kick "
                        "and bass.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 100.0f;
        p.state.crossoverFreqs[1] = 400.0f;
        // Sub: hard clip for punch
        setBandType(p.state.bandMorph[0], DistortionType::HardClip, 3.5f, 1200.0f);
        shapeHardClip(p.state.bandMorph[0].nodes[0], 0.45f, 0.8f);
        // Low-mid: temporal for dynamics; drive 4 (was 2.5 at 100 % mix) keeps the bite between attacks
        setBandType(p.state.bandMorph[1], DistortionType::Temporal, 4.0f, 2500.0f);
        shapeTemporal(p.state.bandMorph[1].nodes[0], 0, 0.6f, 0.5f, 5.0f, 80.0f, 0.6f);
        setBandMix(p.state.bandMorph[1], 0.7f);  // each attack opens it (routing 1)
        // Upper: tube for presence
        setBandType(p.state.bandMorph[2], DistortionType::Tube, 2.0f, 4000.0f);
        shapeTube(p.state.bandMorph[2].nodes[0], 0.05f, 0.4f, 1);
        p.state.bands[0].gainDb = 2.5f;
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(0), 0.4f);
        addRouting(p.state, 1, ModSource::Transient, ModDest::bandMix(1), 0.3f);
        p.state.modSources.transSensitivity = 0.8f;
        p.state.modSources.transAttackNorm = 0.05f;
        p.state.modSources.transDecayNorm = 0.2f;
        presets.push_back(p);
    }
    { // Soft Bass Drive: morph soft→tube on subs, tape shimmer up top
        PresetDef p; p.name = "Soft Bass Drive"; p.category = "Bass";
        p.description = "Two bands split at 200 Hz: the lows sit between Soft Clip and Tube and lean toward Tube "
                        "as you play harder, with light Tape above. A gentle drive for bass guitar and synth bass.";
        p.state = makeInitState(2);
        p.state.crossoverFreqs[0] = 200.0f;
        // Low: soft clip→tube morph
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::Tube,
                   3.0f, 3.5f, 1500.0f, 2000.0f);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.8f, 0.2f);
        shapeTube(p.state.bandMorph[0].nodes[1], 0.15f, 0.6f, 2);
        p.state.bandMorph[0].morphX = 0.4f;
        // High: tape
        setBandType(p.state.bandMorph[1], DistortionType::Tape, 1.5f, 5000.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.0f, 0.3f, 0.7f, 1, 0.4f, 0.15f);
        p.state.bands[0].gainDb = 1.5f;
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandMorphX(0), 0.3f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandDrive(0), 0.1f);
        p.state.modSources.envSensitivity = 0.65f;
        p.state.modSources.lfo1RateNorm = 0.15f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        presets.push_back(p);
    }
    { // Serge Bass: serge fold subs, ring sat mids, formant highs
        PresetDef p; p.name = "Serge Bass"; p.category = "Bass";
        p.description = "Serge Fold below 120 Hz, Ring Saturation to 500 Hz and a quiet Formant band on top for a "
                        "vowel colour; LFO 1 and the input level move the lower drives. For synth bass.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 120.0f;
        p.state.crossoverFreqs[1] = 500.0f;
        // Sub: serge fold with controlled depth
        setBandType(p.state.bandMorph[0], DistortionType::SergeFold, 2.5f, 1500.0f);
        shapeSergeFold(p.state.bandMorph[0].nodes[0], 2.0f, 0.0f, 0, 0.1f, 0.3f, 0.15f);
        // Low-mid: ring saturation for harmonics
        setBandType(p.state.bandMorph[1], DistortionType::RingSaturation, 2.0f, 3000.0f);
        shapeRingSat(p.state.bandMorph[1].nodes[0], 0.4f, 1, 0.5f, 0, 0.0f, 0);
        // Upper: formant for vocal quality
        setBandType(p.state.bandMorph[2], DistortionType::Formant, 1.0f, 5000.0f);
        shapeFormant(p.state.bandMorph[2].nodes[0], 4, -2.0f, 0.4f, 0.5f, 0.6f, 0, 0.6f, 0.4f);
        p.state.bands[0].gainDb = 2.0f;
        p.state.bands[2].gainDb = -3.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.15f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandDrive(1), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.25f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.envSensitivity = 0.65f;
        presets.push_back(p);
    }
    { // Multi Band Bass: 4-band with escalating character
        PresetDef p; p.name = "Multi Band Bass"; p.category = "Bass";
        p.description = "Four bands rising in grit: Tube under 80 Hz, Tape to 200 Hz, Fuzz to 500 Hz and Soft Clip "
                        "above; the input level drives the Tube and Fuzz bands harder.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 80.0f;
        p.state.crossoverFreqs[1] = 200.0f;
        p.state.crossoverFreqs[2] = 500.0f;
        // Sub: tube with sag, low tone
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 2.0f, 800.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.0f, 0.8f, 3);
        // Low: tape warmth
        setBandType(p.state.bandMorph[1], DistortionType::Tape, 2.5f, 1500.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.1f, 0.5f, 0.4f, 1, 0.7f, 0.25f);
        // Low-mid: fuzz for growl
        setBandType(p.state.bandMorph[2], DistortionType::Fuzz, 3.0f, 3000.0f);
        shapeFuzz(p.state.bandMorph[2].nodes[0], 0.15f, 0, 0.0f, 0.2f);
        // Mid: soft clip for definition
        setBandType(p.state.bandMorph[3], DistortionType::SoftClip, 2.0f, 4500.0f);
        shapeSoftClip(p.state.bandMorph[3].nodes[0], 0.65f, 0.3f);
        p.state.bands[0].gainDb = 3.0f;
        p.state.bands[1].gainDb = 1.0f;
        p.state.bands[3].gainDb = -2.0f;
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.2f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandDrive(2), 0.3f);
        addRouting(p.state, 2, ModSource::LFO1, ModDest::bandPan(2), 0.1f);
        p.state.modSources.envSensitivity = 0.7f;
        p.state.modSources.lfo1RateNorm = 0.2f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // Ring Bass: ring sat subs, chaos mids, spectral shimmer
        PresetDef p; p.name = "Ring Bass"; p.category = "Bass";
        p.description = "Ring Saturation under 150 Hz gives the low end a metallic edge, with a Chaos band to 500 "
                        "Hz that a chaos source drives and a quiet Spectral top. Clangy synth bass.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 150.0f;
        p.state.crossoverFreqs[1] = 500.0f;
        // Sub: ring saturation with strong mod
        setBandType(p.state.bandMorph[0], DistortionType::RingSaturation, 2.5f, 1200.0f);
        shapeRingSat(p.state.bandMorph[0].nodes[0], 0.7f, 2, 0.6f, 0, 0.1f, 0);
        // Low-mid: chaos waveshaper
        setBandType(p.state.bandMorph[1], DistortionType::Chaos, 2.0f, 3000.0f);
        shapeChaos(p.state.bandMorph[1].nodes[0], 1, 2.0f, 0.5f, 0.4f, 0.5f, 0.4f, 0.2f);
        // Upper: spectral for shimmer
        setBandType(p.state.bandMorph[2], DistortionType::Spectral, 1.5f, 5500.0f);
        shapeSpectral(p.state.bandMorph[2].nodes[0], 0, 1, 0.5f, 0.6f, 0.1f, 0, 0.5f);
        p.state.bands[0].gainDb = 2.0f;
        p.state.bands[2].gainDb = -4.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.2f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(1), 0.15f);
        p.state.modSources.lfo1RateNorm = 0.3f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.chaosSpeedNorm = 0.1f;
        presets.push_back(p);
    }

    // =========================================================================
    // LEADS (10 presets) - Aggressive, cutting through
    // =========================================================================
    { // Screaming Lead: hard clip mids, fuzz lows, aliased highs
        PresetDef p; p.name = "Screaming Lead"; p.category = "Leads";
        p.description = "Hard Clip at drive 5.5 on the 400 Hz to 5 kHz mids, octave Fuzz below and Aliasing sizzle "
                        "on top; the input level drives the mids harder. For lead guitar and mono synth leads.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 400.0f;
        p.state.crossoverFreqs[1] = 5000.0f;
        // Low: fuzz with octave for thickness
        setBandType(p.state.bandMorph[0], DistortionType::Fuzz, 5.0f, 3000.0f);
        shapeFuzz(p.state.bandMorph[0].nodes[0], 0.1f, 0, 1.0f, 0.4f);
        // Mid: hard clip with low threshold
        setBandType(p.state.bandMorph[1], DistortionType::HardClip, 5.5f, 5500.0f);
        shapeHardClip(p.state.bandMorph[1].nodes[0], 0.35f, 0.8f);
        // High: aliasing for sizzle
        setBandType(p.state.bandMorph[2], DistortionType::Aliasing, 2.5f, 7500.0f);
        shapeAliasing(p.state.bandMorph[2].nodes[0], 4.0f, 300.0f, false, 0.2f, 0.3f);
        p.state.bands[1].gainDb = 2.0f;
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(1), 0.4f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandPan(2), 0.3f);
        p.state.modSources.envSensitivity = 0.8f;
        p.state.modSources.lfo1RateNorm = 0.5f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // Fuzz Face: classic fuzz on mids, tape subs, ring sat highs
        PresetDef p; p.name = "Fuzz Face"; p.category = "Leads";
        p.description = "A heavy Fuzz at drive 6.5 owns the mids, with Tape under 300 Hz and Ring Saturation "
                        "shimmer above 5 kHz; the input level pushes the fuzz further. A fuzz-pedal lead for "
                        "guitar.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 5000.0f;
        // Low: tape for warmth under fuzz
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 2.0f, 2000.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.1f, 0.5f, 0.4f, 1, 0.7f, 0.2f);
        // Mid: heavy fuzz — the star
        setBandType(p.state.bandMorph[1], DistortionType::Fuzz, 6.5f, 5000.0f);
        shapeFuzz(p.state.bandMorph[1].nodes[0], 0.15f, 0, 1.0f, 0.5f);
        // High: ring saturation for shimmer
        setBandType(p.state.bandMorph[2], DistortionType::RingSaturation, 2.0f, 7000.0f);
        shapeRingSat(p.state.bandMorph[2].nodes[0], 0.4f, 1, 0.5f, 2, 0.0f, 1);
        p.state.bands[1].gainDb = 1.5f;
        p.state.bands[2].gainDb = -2.0f;
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(1), 0.3f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandDrive(2), 0.2f);
        p.state.modSources.envSensitivity = 0.7f;
        p.state.modSources.lfo1RateNorm = 0.6f;
        p.state.modSources.lfo1Shape = Waveform::Triangle;
        presets.push_back(p);
    }
    { // Feedback Scream: feedback dist morphing, chaos-modulated
        PresetDef p; p.name = "Feedback Scream"; p.category = "Leads";
        p.description = "Feedback Distortion on the lows and a Feedback to Hard Clip morph on the mids, both "
                        "pushed around by a Lorenz chaos source, with a Chaos top band. Unstable, howling leads.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 350.0f;
        p.state.crossoverFreqs[1] = 4500.0f;
        // Low: feedback dist with long delay
        setBandType(p.state.bandMorph[0], DistortionType::FeedbackDist, 4.0f, 2500.0f);
        shapeFeedback(p.state.bandMorph[0].nodes[0], 0.9f, 15.0f, 0.7f, 1, 0.4f, 2, true, 0.6f);
        // Mid: feedback→hard clip morph
        setMorphAB(p.state.bandMorph[1], DistortionType::FeedbackDist, DistortionType::HardClip,
                   4.5f, 5.0f, 4500.0f, 5500.0f);
        shapeFeedback(p.state.bandMorph[1].nodes[0], 0.7f, 5.0f, 0.6f, 2, 0.6f, 1);
        shapeHardClip(p.state.bandMorph[1].nodes[1], 0.4f, 0.85f);
        // High: chaos for screaming overtones
        setBandType(p.state.bandMorph[2], DistortionType::Chaos, 3.0f, 7000.0f);
        shapeChaos(p.state.bandMorph[2].nodes[0], 0, 8.0f, 0.6f, 0.5f, 0.6f, 0.5f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(0), 0.3f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandMorphX(1), 0.4f);
        addRouting(p.state, 2, ModSource::LFO1, ModDest::bandPan(0), 0.25f);
        p.state.modSources.chaosModel = ChaosModel::Lorenz;
        p.state.modSources.chaosSpeedNorm = 0.3f;
        p.state.modSources.chaosCoupling = 0.5f;
        p.state.modSources.lfo1RateNorm = 0.4f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // Chaos Lead: chaos-driven multi-band with fractal undertones
        PresetDef p; p.name = "Chaos Lead"; p.category = "Leads";
        p.description = "Fractal lows, a Chaos mid band and Stochastic highs, with a Chua chaos source moving both "
                        "lower drives and the top band pan. A restless lead that never repeats.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 400.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        // Low: fractal for deep harmonics
        setBandType(p.state.bandMorph[0], DistortionType::Fractal, 3.0f, 2500.0f);
        shapeFractal(p.state.bandMorph[0].nodes[0], 1, 6, 0.6f, 0.5f, 0.6f, 0.2f, 1, 0.7f);
        // Mid: chaos waveshaper
        setBandType(p.state.bandMorph[1], DistortionType::Chaos, 3.5f, 5000.0f);
        shapeChaos(p.state.bandMorph[1].nodes[0], 2, 6.0f, 0.7f, 0.6f, 0.6f, 0.5f);
        // High: stochastic for noisy texture
        setBandType(p.state.bandMorph[2], DistortionType::Stochastic, 2.5f, 7000.0f);
        shapeStochastic(p.state.bandMorph[2].nodes[0], 3, 0.4f, 30.0f, 0.2f, 0.3f, 2, 0.3f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(0), 0.3f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(1), 0.4f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandPan(2), 0.35f);
        p.state.modSources.chaosModel = ChaosModel::Chua;
        p.state.modSources.chaosSpeedNorm = 0.4f;
        p.state.modSources.chaosCoupling = 0.6f;
        presets.push_back(p);
    }
    { // Asymmetric Bite: asym fuzz with body, bitwise mids, formant highs
        PresetDef p; p.name = "Asymmetric Bite"; p.category = "Leads";
        p.description = "Asymmetric Fuzz with heavy body under 350 Hz, Bitwise Mangler mids stepped by a "
                        "sample-and-hold LFO and a Formant top shifted up 8 semitones. A biting, vocal lead.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 350.0f;
        p.state.crossoverFreqs[1] = 4500.0f;
        // Low: asym fuzz, heavy body
        setBandType(p.state.bandMorph[0], DistortionType::AsymmetricFuzz, 5.0f, 3000.0f);
        shapeAsymFuzz(p.state.bandMorph[0].nodes[0], 0.4f, 0.8f, 1, 0.8f, 0.8f);
        // Mid: bitwise mangler for digital aggression
        setBandType(p.state.bandMorph[1], DistortionType::BitwiseMangler, 3.0f, 5500.0f);
        shapeBitwise(p.state.bandMorph[1].nodes[0], 2, 0.7f, 0.4f, 0.6f, 0.1f);
        // High: formant for vocal screech
        setBandType(p.state.bandMorph[2], DistortionType::Formant, 2.5f, 7000.0f);
        shapeFormant(p.state.bandMorph[2].nodes[0], 0, 8.0f, 0.7f, 0.8f, 0.3f, 2, 0.2f, 0.5f);
        p.state.bands[1].pan = -0.15f;
        p.state.bands[2].pan = 0.15f;
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.35f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandDrive(1), 0.25f);
        p.state.modSources.envSensitivity = 0.8f;
        p.state.modSources.lfo1RateNorm = 0.6f;
        p.state.modSources.lfo1Shape = Waveform::SampleAndHold;
        presets.push_back(p);
    }
    { // Modulated Edge: morph + LFO animated hard clip→fuzz
        PresetDef p; p.name = "Modulated Edge"; p.category = "Leads";
        p.description = "A saw LFO sweeps the mids from Hard Clip to Fuzz about every two seconds, over Tube lows "
                        "and a Sine Fold top band that LFO 2 drives. A lead with a built-in rhythm.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4500.0f;
        // Low: tube warmth for foundation
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 3.0f, 2500.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.2f, 0.6f, 2);
        // Mid: hard clip→fuzz morph animated by LFO
        setMorphAB(p.state.bandMorph[1], DistortionType::HardClip, DistortionType::Fuzz,
                   4.5f, 5.5f, 5000.0f, 5500.0f);
        shapeHardClip(p.state.bandMorph[1].nodes[0], 0.4f, 0.85f);
        shapeFuzz(p.state.bandMorph[1].nodes[1], 0.2f, 0, 1.0f, 0.4f);
        // High: wavefold for overtones
        setBandType(p.state.bandMorph[2], DistortionType::SineFold, 3.0f, 7000.0f);
        shapeSineFold(p.state.bandMorph[2].nodes[0], 4.0f, 0.3f, 0.4f, 0.0f, 0.1f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(1), 0.6f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandDrive(2), 0.3f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandDrive(0), 0.25f);
        p.state.modSources.lfo1RateNorm = 0.5f;
        p.state.modSources.lfo1Shape = Waveform::Saw;
        p.state.modSources.lfo2RateNorm = 0.7f;
        p.state.modSources.lfo2Shape = Waveform::Triangle;
        p.state.modSources.envSensitivity = 0.7f;
        presets.push_back(p);
    }
    { // Ring Mod Lead: ring sat with chaos-modulated carrier, allpass underneath
        PresetDef p; p.name = "Ring Mod Lead"; p.category = "Leads";
        p.description = "Ring Saturation at drive 3.5 carries the mids, with Allpass Resonant body below and a "
                        "Chaos top; a chaos source varies the ring drive and LFO 1 pans it. Bell-like, metallic "
                        "leads.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        // Low: allpass for body
        setBandType(p.state.bandMorph[0], DistortionType::AllpassResonant, 2.5f, 2500.0f);
        shapeAllpass(p.state.bandMorph[0].nodes[0], 2, 250.0f, 0.7f, 1.5f, 0.5f, 2, true, 0.3f);
        // Mid: ring saturation — the star
        setBandType(p.state.bandMorph[1], DistortionType::RingSaturation, 3.5f, 5500.0f);
        shapeRingSat(p.state.bandMorph[1].nodes[0], 0.8f, 3, 0.6f, 2, 0.15f, 2);
        // High: chaos for wild overtones
        setBandType(p.state.bandMorph[2], DistortionType::Chaos, 2.0f, 7000.0f);
        shapeChaos(p.state.bandMorph[2].nodes[0], 1, 7.0f, 0.5f, 0.4f, 0.5f, 0.6f);
        p.state.bands[0].pan = -0.1f;
        p.state.bands[2].pan = 0.1f;
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(1), 0.25f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandPan(1), 0.3f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandDrive(0), 0.2f);
        p.state.modSources.chaosSpeedNorm = 0.25f;
        p.state.modSources.lfo1RateNorm = 0.45f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.envSensitivity = 0.6f;
        presets.push_back(p);
    }
    { // Wavefold Lead: sine fold→tri fold morph, serge underneath
        PresetDef p; p.name = "Wavefold Lead"; p.category = "Leads";
        p.description = "The mids morph between Sine Fold and Triangle Fold on a triangle LFO, over Serge Fold "
                        "lows and a Hard Clip top; the input level drives the lows. Bright, hollow synth leads.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 5000.0f;
        // Low: serge fold with model switching
        setBandType(p.state.bandMorph[0], DistortionType::SergeFold, 3.0f, 3000.0f);
        shapeSergeFold(p.state.bandMorph[0].nodes[0], 4.0f, -0.2f, 2, 0.1f, 0.5f, 0.1f);
        // Mid: sine fold→triangle fold morph
        setMorphAB(p.state.bandMorph[1], DistortionType::SineFold, DistortionType::TriangleFold,
                   3.5f, 3.5f, 5500.0f, 5500.0f);
        shapeSineFold(p.state.bandMorph[1].nodes[0], 5.0f, 0.3f, 0.5f, 0.1f, 0.1f);
        shapeTriFold(p.state.bandMorph[1].nodes[1], 4.0f, -0.1f, 0.8f, 0.0f, 0.1f);
        // High: hard clip for edge
        setBandType(p.state.bandMorph[2], DistortionType::HardClip, 3.0f, 7500.0f);
        shapeHardClip(p.state.bandMorph[2].nodes[0], 0.45f, 0.85f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(1), 0.5f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandDrive(0), 0.3f);
        addRouting(p.state, 2, ModSource::LFO2, ModDest::bandDrive(1), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.4f;
        p.state.modSources.lfo1Shape = Waveform::Triangle;
        p.state.modSources.lfo2RateNorm = 0.6f;
        p.state.modSources.lfo2Shape = Waveform::Saw;
        p.state.modSources.envSensitivity = 0.7f;
        presets.push_back(p);
    }
    { // Temporal Edge: temporal dist with dynamic response, tube warmth
        PresetDef p; p.name = "Temporal Edge"; p.category = "Leads";
        p.description = "Temporal distortion at drive 4.5 on the mids, pushed harder by every transient, between "
                        "Tube lows the input level drives and a Bitwise Mangler top stepped by a sample-and-hold "
                        "LFO. Follows picking dynamics.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 350.0f;
        p.state.crossoverFreqs[1] = 4500.0f;
        // Low: tube for warmth
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 3.0f, 2500.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.15f, 0.5f, 2);
        // Mid: temporal distortion — envelope mode
        setBandType(p.state.bandMorph[1], DistortionType::Temporal, 4.5f, 5000.0f);
        shapeTemporal(p.state.bandMorph[1].nodes[0], 1, 0.8f, 0.6f, 8.0f, 120.0f, 0.7f, 1, 0.2f);
        // High: bitwise for digital edge
        setBandType(p.state.bandMorph[2], DistortionType::BitwiseMangler, 2.5f, 7000.0f);
        shapeBitwise(p.state.bandMorph[2].nodes[0], 3, 0.6f, 0.3f, 0.5f, 0.1f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(1), 0.5f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandDrive(2), 0.2f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandDrive(0), 0.2f);
        p.state.modSources.transSensitivity = 0.8f;
        p.state.modSources.lfo1RateNorm = 0.55f;
        p.state.modSources.lfo1Shape = Waveform::SampleAndHold;
        p.state.modSources.envSensitivity = 0.7f;
        presets.push_back(p);
    }
    { // Fractal Lead: fractal dist with feedback, spectral highs
        PresetDef p; p.name = "Fractal Lead"; p.category = "Leads";
        p.description = "Fractal distortion at drive 4 on the mids, with Tape lows and a Spectral top; the input "
                        "level drives the fractal harder and a chaos source drifts its pan. Dense, glassy leads.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        // Low: tape warmth
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 2.5f, 2000.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.1f, 0.5f, 0.4f, 1, 0.6f, 0.2f);
        // Mid: fractal — the star
        setBandType(p.state.bandMorph[1], DistortionType::Fractal, 4.0f, 5000.0f);
        shapeFractal(p.state.bandMorph[1].nodes[0], 2, 6, 0.6f, 0.5f, 0.6f, 0.25f, 2, 0.7f);
        // High: spectral for crystalline quality
        setBandType(p.state.bandMorph[2], DistortionType::Spectral, 2.0f, 7000.0f);
        shapeSpectral(p.state.bandMorph[2].nodes[0], 1, 1, 0.5f, 0.7f, 0.15f, 2, 0.6f, 1);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(1), 0.4f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandDrive(2), 0.2f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandPan(1), 0.2f);
        p.state.modSources.envSensitivity = 0.75f;
        p.state.modSources.lfo1RateNorm = 0.3f;
        p.state.modSources.lfo1Shape = Waveform::Triangle;
        p.state.modSources.chaosSpeedNorm = 0.15f;
        presets.push_back(p);
    }

    // =========================================================================
    // PADS (10 presets) - Subtle, evolving
    // =========================================================================
    { // Gentle Warmth: soft clip lows, tape mids, allpass shimmer highs
        PresetDef p; p.name = "Gentle Warmth"; p.category = "Pads";
        p.description = "Light Soft Clip lows, Tape mids at drive 1.2 and a quiet Allpass Resonant top, with two "
                        "very slow LFOs breathing the upper drives. Subtle warmth for pads, keys and mix buses.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        // Low: soft clip with gentle curve
        setBandType(p.state.bandMorph[0], DistortionType::SoftClip, 1.5f, 2500.0f);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.8f, 0.15f);
        // Mid: tape with gentle flutter
        setBandType(p.state.bandMorph[1], DistortionType::Tape, 1.2f, 4500.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.0f, 0.3f, 0.5f, 1, 0.5f, 0.2f);
        // High: allpass for shimmer
        setBandType(p.state.bandMorph[2], DistortionType::AllpassResonant, 0.8f, 6500.0f);
        shapeAllpass(p.state.bandMorph[2].nodes[0], 0, 500.0f, 0.5f, 2.5f, 0.4f, 1, false, 0.4f);
        p.state.bands[2].gainDb = -3.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(1), 0.1f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandDrive(2), 0.15f);
        p.state.modSources.lfo1RateNorm = 0.06f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.lfo2RateNorm = 0.04f;
        p.state.modSources.lfo2Shape = Waveform::SmoothRandom;
        presets.push_back(p);
    }
    { // Evolving Texture: slow morphs on all bands, random-driven
        PresetDef p; p.name = "Evolving Texture"; p.category = "Pads";
        p.description = "Four bands that keep changing: Soft Clip to Tape and Tube to Allpass Resonant morphs on "
                        "two slow LFOs, and Stochastic and Formant upper bands whose drives a smooth random source "
                        "moves.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 1500.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        // Low: soft clip→tape morph
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::Tape,
                   1.2f, 1.5f, 2000.0f, 1800.0f);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.75f, 0.2f);
        shapeTape(p.state.bandMorph[0].nodes[1], 0.0f, 0.4f, 0.5f, 1, 0.6f, 0.3f);
        // Lo-mid: tube→allpass morph
        setMorphAB(p.state.bandMorph[1], DistortionType::Tube, DistortionType::AllpassResonant,
                   1.0f, 1.0f, 3500.0f, 4000.0f);
        shapeTube(p.state.bandMorph[1].nodes[0], 0.0f, 0.3f, 0);
        shapeAllpass(p.state.bandMorph[1].nodes[1], 1, 400.0f, 0.5f, 2.0f, 0.4f, 1);
        // Hi-mid: stochastic drift
        setBandType(p.state.bandMorph[2], DistortionType::Stochastic, 0.8f, 5500.0f);
        shapeStochastic(p.state.bandMorph[2].nodes[0], 1, 0.1f, 5.0f, 0.05f, 0.15f, 1, 0.7f);
        // High: formant with slow vowel shift
        setBandType(p.state.bandMorph[3], DistortionType::Formant, 1.0f, 7000.0f);
        shapeFormant(p.state.bandMorph[3].nodes[0], 1, 0.0f, 0.4f, 0.4f, 0.6f, 0, 0.5f, 0.6f);
        p.state.bandMorph[0].morphSmoothing = 200.0f;
        p.state.bandMorph[1].morphSmoothing = 200.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.4f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandMorphX(1), 0.4f);
        addRouting(p.state, 2, ModSource::Random, ModDest::bandDrive(2), 0.15f);
        addRouting(p.state, 3, ModSource::Random, ModDest::bandDrive(3), 0.1f);
        p.state.modSources.lfo1RateNorm = 0.05f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.lfo2RateNorm = 0.07f;
        p.state.modSources.lfo2Shape = Waveform::SmoothRandom;
        p.state.modSources.randomRateNorm = 0.08f;
        p.state.modSources.randomSmoothness = 0.9f;
        p.state.bands[1].pan = -0.1f;
        p.state.bands[2].pan = 0.1f;
        presets.push_back(p);
    }
    { // Allpass Shimmer: allpass highs, tube warmth lows, sweep-animated
        PresetDef p; p.name = "Allpass Shimmer"; p.category = "Pads";
        p.description = "A pitch-tracking Allpass Resonant top band rings over light Tube and Soft Clip, with a "
                        "gentle sweep on a very slow LFO and LFO 2 moving the shimmer across the stereo field.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        enableSweep(p.state, 0.6f, 0.3f, 0.25f);
        enableSweepLFO(p.state, 0.08f, 0.4f, Waveform::Sine);
        // Low: tube for warmth
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 1.0f, 2000.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.0f, 0.3f, 0);
        // Mid: soft clip
        setBandType(p.state.bandMorph[1], DistortionType::SoftClip, 1.0f, 4500.0f);
        shapeSoftClip(p.state.bandMorph[1].nodes[0], 0.7f, 0.2f);
        // High: allpass resonant with pitch tracking
        setBandType(p.state.bandMorph[2], DistortionType::AllpassResonant, 1.0f, 7000.0f);
        shapeAllpass(p.state.bandMorph[2].nodes[0], 1, 700.0f, 0.6f, 3.0f, 0.4f, 2, true, 0.25f);
        p.state.bands[2].pan = 0.15f;
        p.state.bands[2].gainDb = -2.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(2), 0.15f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandPan(2), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.05f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        p.state.modSources.lfo2RateNorm = 0.1f;
        p.state.modSources.lfo2Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // Tape Haze: tape everywhere but with different character per band
        PresetDef p; p.name = "Tape Haze"; p.category = "Pads";
        p.description = "Two Tape bands, slow and saggy below 250 Hz and fluttering to 3.5 kHz, under a quiet "
                        "Stochastic top; very slow LFOs and a smooth random pan keep it drifting. Worn, hazy pads.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        // Low: slow tape with heavy sag
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 1.5f, 1800.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.0f, 0.7f, 0.3f, 1, 0.8f, 0.4f);
        // Mid: hysteresis tape with flutter
        setBandType(p.state.bandMorph[1], DistortionType::Tape, 1.2f, 4000.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.1f, 0.4f, 0.6f, 1, 0.5f, 0.35f);
        // High: stochastic for subtle movement
        setBandType(p.state.bandMorph[2], DistortionType::Stochastic, 0.8f, 6000.0f);
        shapeStochastic(p.state.bandMorph[2].nodes[0], 0, 0.08f, 3.0f, 0.04f, 0.1f, 1, 0.8f);
        p.state.bands[2].gainDb = -2.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.1f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandDrive(1), 0.08f);
        addRouting(p.state, 2, ModSource::Random, ModDest::bandPan(1), 0.1f);
        p.state.modSources.lfo1RateNorm = 0.04f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.lfo2RateNorm = 0.06f;
        p.state.modSources.lfo2Shape = Waveform::SmoothRandom;
        p.state.modSources.randomRateNorm = 0.05f;
        p.state.modSources.randomSmoothness = 0.9f;
        presets.push_back(p);
    }
    { // Slow Morph Pad: ultra-slow morphing across bands
        PresetDef p; p.name = "Slow Morph Pad"; p.category = "Pads";
        p.description = "Tube to Allpass Resonant on the lows and Tape to Granular on the mids, each LFO cycle "
                        "taking over a minute, over a soft Formant top. Barely moving; for long pads.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        // Low: tube→allpass morph
        setMorphAB(p.state.bandMorph[0], DistortionType::Tube, DistortionType::AllpassResonant,
                   1.0f, 1.0f, 2000.0f, 2500.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.0f, 0.4f, 1);
        shapeAllpass(p.state.bandMorph[0].nodes[1], 0, 300.0f, 0.5f, 3.0f, 0.4f, 1, false, 0.4f);
        p.state.bandMorph[0].morphSmoothing = 350.0f;
        // Mid: tape→granular morph
        setMorphAB(p.state.bandMorph[1], DistortionType::Tape, DistortionType::Granular,
                   1.0f, 1.5f, 4000.0f, 4500.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.0f, 0.3f, 0.5f, 1, 0.5f, 0.2f);
        shapeGranular(p.state.bandMorph[1].nodes[1], 50.0f, 0.4f, 0.1f, 0.1f, 0.5f, 0.6f, 2, 1);
        p.state.bandMorph[1].morphSmoothing = 350.0f;
        // High: formant gently
        setBandType(p.state.bandMorph[2], DistortionType::Formant, 0.8f, 6500.0f);
        shapeFormant(p.state.bandMorph[2].nodes[0], 2, 0.0f, 0.4f, 0.4f, 0.6f, 0, 0.5f, 0.5f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.6f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandMorphX(1), 0.5f);
        addRouting(p.state, 2, ModSource::Random, ModDest::bandDrive(2), 0.1f);
        p.state.modSources.lfo1RateNorm = 0.03f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.lfo2RateNorm = 0.04f;
        p.state.modSources.lfo2Shape = Waveform::Triangle;
        p.state.modSources.randomRateNorm = 0.05f;
        p.state.modSources.randomSmoothness = 0.9f;
        presets.push_back(p);
    }
    { // Soft Sweep: gentle sweep over tube + formant + spectral
        PresetDef p; p.name = "Soft Sweep"; p.category = "Pads";
        p.description = "A gentle, very slow sweep over four light bands, Tube, Soft Clip, Formant and Spectral, "
                        "with the upper two turned down and panned apart. Soft movement for pads and ambient beds.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 1500.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        enableSweep(p.state, 0.4f, 0.5f, 0.2f);
        enableSweepLFO(p.state, 0.04f, 0.5f, Waveform::Sine);
        // Sub: tube warmth
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 1.0f, 1500.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.0f, 0.3f, 0);
        // Lo-mid: soft clip
        setBandType(p.state.bandMorph[1], DistortionType::SoftClip, 1.0f, 3500.0f);
        shapeSoftClip(p.state.bandMorph[1].nodes[0], 0.8f, 0.15f);
        // Hi-mid: formant
        setBandType(p.state.bandMorph[2], DistortionType::Formant, 0.8f, 5500.0f);
        shapeFormant(p.state.bandMorph[2].nodes[0], 2, 1.0f, 0.3f, 0.4f, 0.6f, 0, 0.5f, 0.6f);
        // High: spectral
        setBandType(p.state.bandMorph[3], DistortionType::Spectral, 0.8f, 7000.0f);
        shapeSpectral(p.state.bandMorph[3].nodes[0], 0, 2, 0.4f, 0.6f, 0.05f);
        p.state.bands[2].gainDb = -2.0f;
        p.state.bands[3].gainDb = -3.0f;
        p.state.bands[2].pan = -0.1f;
        p.state.bands[3].pan = 0.1f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(2), 0.1f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandPan(3), 0.15f);
        p.state.modSources.lfo1RateNorm = 0.05f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        p.state.modSources.lfo2RateNorm = 0.08f;
        p.state.modSources.lfo2Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // Formant Pad: formant mids, granular lows, stochastic highs
        PresetDef p; p.name = "Formant Pad"; p.category = "Pads";
        p.description = "Formant mids on a slow drive LFO give pads a vowel colour, over long Granular grains on "
                        "the lows and a quiet Stochastic top; a smooth random source and LFO 2 add drift.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        // Low: granular texture
        setBandType(p.state.bandMorph[0], DistortionType::Granular, 1.0f, 2500.0f);
        shapeGranular(p.state.bandMorph[0].nodes[0], 60.0f, 0.3f, 0.1f, 0.1f, 0.4f, 0.6f, 2, 1);
        // Mid: formant with slow vowel morph
        setBandType(p.state.bandMorph[1], DistortionType::Formant, 1.5f, 5000.0f);
        shapeFormant(p.state.bandMorph[1].nodes[0], 0, 0.0f, 0.5f, 0.6f, 0.5f, 1, 0.5f, 0.5f);
        // High: stochastic shimmer
        setBandType(p.state.bandMorph[2], DistortionType::Stochastic, 0.8f, 6500.0f);
        shapeStochastic(p.state.bandMorph[2].nodes[0], 1, 0.1f, 8.0f, 0.06f, 0.1f, 1, 0.75f);
        p.state.bands[2].gainDb = -2.0f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(1), 0.15f);
        addRouting(p.state, 1, ModSource::Random, ModDest::bandDrive(0), 0.1f);
        addRouting(p.state, 2, ModSource::LFO2, ModDest::bandPan(0), 0.15f);
        p.state.modSources.lfo1RateNorm = 0.04f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.lfo2RateNorm = 0.06f;
        p.state.modSources.lfo2Shape = Waveform::SmoothRandom;
        p.state.modSources.randomRateNorm = 0.06f;
        p.state.modSources.randomSmoothness = 0.85f;
        presets.push_back(p);
    }
    { // Granular Wash: granular main with sweep, tape warmth, spectral highs
        PresetDef p; p.name = "Granular Wash"; p.category = "Pads";
        p.description = "Granular mids that a smooth random source pans around, between Tape lows and a quiet "
                        "Spectral top, with a gentle sweep on a very slow LFO. Turns sustained sounds into a soft "
                        "wash.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        enableSweep(p.state, 0.5f, 0.4f, 0.2f);
        enableSweepLFO(p.state, 0.06f, 0.4f, Waveform::Sine);
        // Low: tape warmth
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 1.0f, 2000.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.0f, 0.4f, 0.4f, 1, 0.7f, 0.3f);
        // Mid: granular — the star
        setBandType(p.state.bandMorph[1], DistortionType::Granular, 1.5f, 4500.0f);
        shapeGranular(p.state.bandMorph[1].nodes[0], 45.0f, 0.5f, 0.15f, 0.15f, 0.3f, 0.5f, 1, 2);
        // High: spectral tilt
        setBandType(p.state.bandMorph[2], DistortionType::Spectral, 1.0f, 7000.0f);
        shapeSpectral(p.state.bandMorph[2].nodes[0], 0, 2, 0.4f, 0.7f, 0.05f, 0, 0.5f);
        p.state.bands[2].gainDb = -3.0f;
        p.state.bands[1].pan = -0.1f;
        p.state.bands[2].pan = 0.1f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(1), 0.15f);
        addRouting(p.state, 1, ModSource::Random, ModDest::bandPan(1), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.05f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        p.state.modSources.randomRateNorm = 0.1f;
        p.state.modSources.randomSmoothness = 0.8f;
        presets.push_back(p);
    }
    { // Stochastic Drift: stochastic on mids, chaos on lows, random-modulated
        PresetDef p; p.name = "Stochastic Drift"; p.category = "Pads";
        p.description = "Chaos, Stochastic and Fractal bands at low drive, with smooth random drives and a slow "
                        "Rossler chaos source and LFO 1 drifting the pans. Unsettled, slowly moving pads.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        // Low: chaos with very slow attractor
        setBandType(p.state.bandMorph[0], DistortionType::Chaos, 1.0f, 2000.0f);
        shapeChaos(p.state.bandMorph[0].nodes[0], 1, 0.5f, 0.3f, 0.3f, 0.4f, 0.4f, 0.3f);
        // Mid: stochastic drift
        setBandType(p.state.bandMorph[1], DistortionType::Stochastic, 1.0f, 4500.0f);
        shapeStochastic(p.state.bandMorph[1].nodes[0], 2, 0.15f, 8.0f, 0.1f, 0.2f, 2, 0.6f);
        // High: fractal with low depth
        setBandType(p.state.bandMorph[2], DistortionType::Fractal, 0.8f, 6500.0f);
        shapeFractal(p.state.bandMorph[2].nodes[0], 0, 3, 0.4f, 0.5f, 0.4f, 0.05f, 0, 0.4f);
        p.state.bands[2].gainDb = -2.0f;
        addRouting(p.state, 0, ModSource::Random, ModDest::bandDrive(0), 0.15f);
        addRouting(p.state, 1, ModSource::Random, ModDest::bandDrive(1), 0.2f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandPan(0), 0.2f);
        addRouting(p.state, 3, ModSource::LFO1, ModDest::bandPan(2), 0.15f);
        p.state.modSources.randomRateNorm = 0.06f;
        p.state.modSources.randomSmoothness = 0.9f;
        p.state.modSources.chaosSpeedNorm = 0.05f;
        p.state.modSources.chaosModel = ChaosModel::Rossler;
        p.state.modSources.lfo1RateNorm = 0.07f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        presets.push_back(p);
    }
    { // Spectral Pad: spectral mids with tilt, allpass lows, formant highs
        PresetDef p; p.name = "Spectral Pad"; p.category = "Pads";
        p.description = "Spectral processing on the 1.5 to 5 kHz band leads, over soft Tube and Allpass Resonant "
                        "bands and a quiet Formant top; slow LFOs and a random pan keep it moving.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 1500.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        // Sub: tube warmth
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 0.8f, 1500.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.0f, 0.3f, 0);
        // Lo-mid: allpass resonant
        setBandType(p.state.bandMorph[1], DistortionType::AllpassResonant, 1.0f, 3500.0f);
        shapeAllpass(p.state.bandMorph[1].nodes[0], 1, 350.0f, 0.5f, 2.5f, 0.4f, 1, false, 0.35f);
        // Hi-mid: spectral — the star
        setBandType(p.state.bandMorph[2], DistortionType::Spectral, 1.0f, 5500.0f);
        shapeSpectral(p.state.bandMorph[2].nodes[0], 1, 2, 0.5f, 0.6f, 0.08f, 1, 0.5f, 1);
        // High: formant for vocal color
        setBandType(p.state.bandMorph[3], DistortionType::Formant, 0.8f, 7000.0f);
        shapeFormant(p.state.bandMorph[3].nodes[0], 3, 1.0f, 0.3f, 0.4f, 0.6f, 0, 0.6f, 0.5f);
        p.state.bands[2].gainDb = -1.0f;
        p.state.bands[3].gainDb = -3.0f;
        p.state.bands[1].pan = -0.1f;
        p.state.bands[3].pan = 0.1f;
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(2), 0.1f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandDrive(3), 0.1f);
        addRouting(p.state, 2, ModSource::Random, ModDest::bandPan(1), 0.1f);
        p.state.modSources.lfo1RateNorm = 0.04f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.lfo2RateNorm = 0.06f;
        p.state.modSources.lfo2Shape = Waveform::SmoothRandom;
        p.state.modSources.randomRateNorm = 0.07f;
        p.state.modSources.randomSmoothness = 0.85f;
        presets.push_back(p);
    }

    // =========================================================================
    // DRUMS (10 presets) - Transient-friendly
    // =========================================================================
    { // Punchy Clip: hard clip kick, tape snare body, fuzz cymbals
        PresetDef p; p.name = "Punchy Clip"; p.category = "Drums";
        p.description = "Hard Clip under 150 Hz for the kick, Tape across the snare and body range and light Fuzz "
                        "on the cymbals; the transient detector drives the lower two bands on every hit. For drum "
                        "buses.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 150.0f;
        p.state.crossoverFreqs[1] = 5000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::HardClip, 3.5f, 1500.0f);
        shapeHardClip(p.state.bandMorph[0].nodes[0], 0.45f, 0.85f);
        setBandType(p.state.bandMorph[1], DistortionType::Tape, 2.5f, 4500.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.1f, 0.5f, 0.5f, 1, 0.5f, 0.15f);
        setBandType(p.state.bandMorph[2], DistortionType::Fuzz, 2.0f, 7000.0f);
        shapeFuzz(p.state.bandMorph[2].nodes[0], 0.0f, 0, 0.0f, 0.2f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::Transient, ModDest::bandDrive(1), 0.3f);
        p.state.modSources.transSensitivity = 0.8f;
        p.state.modSources.transAttackNorm = 0.05f;
        p.state.modSources.transDecayNorm = 0.15f;
        presets.push_back(p);
    }
    { // Crushed Beats: bitcrush lows, sample reduce mids, quantize highs
        PresetDef p; p.name = "Crushed Beats"; p.category = "Drums";
        p.description = "8-bit Bitcrush on the lows, Sample Reduce on the mids and Quantize on top; transients "
                        "push the mid band and a sample-and-hold LFO steps the top drive. Lo-fi drum loops.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Bitcrush, 1.5f, 2000.0f);
        shapeBitcrush(p.state.bandMorph[0].nodes[0], 8.0f, 0.15f, 1, 0.1f);
        setBandType(p.state.bandMorph[1], DistortionType::SampleReduce, 1.5f, 5000.0f);
        shapeSampleReduce(p.state.bandMorph[1].nodes[0], 6.0f, 0.15f, 0, 0.1f);
        setBandType(p.state.bandMorph[2], DistortionType::Quantize, 2.0f, 7000.0f);
        shapeQuantize(p.state.bandMorph[2].nodes[0], 0.35f, 0.1f, 0.1f, 0.15f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(1), 0.4f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandDrive(2), 0.2f);
        p.state.modSources.transSensitivity = 0.7f;
        p.state.modSources.lfo1RateNorm = 0.6f;
        p.state.modSources.lfo1Shape = Waveform::SampleAndHold;
        presets.push_back(p);
    }
    { // Temporal Snap: temporal on mids, hard clip kicks, aliasing cymbals
        PresetDef p; p.name = "Temporal Snap"; p.category = "Drums";
        p.description = "Hard Clip lows and Temporal mids that the transient detector drives hard on each hit, "
                        "with Aliasing on the cymbals. Snappy, aggressive drums.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 180.0f;
        p.state.crossoverFreqs[1] = 5000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::HardClip, 3.0f, 1200.0f);
        shapeHardClip(p.state.bandMorph[0].nodes[0], 0.5f, 0.8f);
        setBandType(p.state.bandMorph[1], DistortionType::Temporal, 3.0f, 5000.0f);
        shapeTemporal(p.state.bandMorph[1].nodes[0], 2, 0.7f, 0.6f, 3.0f, 60.0f, 0.7f, 1, 0.1f);
        setBandType(p.state.bandMorph[2], DistortionType::Aliasing, 1.5f, 7500.0f);
        shapeAliasing(p.state.bandMorph[2].nodes[0], 3.0f, 150.0f, true, 0.1f, 0.2f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::Transient, ModDest::bandDrive(1), 0.6f);
        p.state.modSources.transSensitivity = 0.85f;
        p.state.modSources.transAttackNorm = 0.03f;
        p.state.modSources.transDecayNorm = 0.12f;
        presets.push_back(p);
    }
    { // Dirty Groove: fuzz kick, tube snare, bitwise hats
        PresetDef p; p.name = "Dirty Groove"; p.category = "Drums";
        p.description = "Fuzz on the kick below 120 Hz, Tube on the snare range and Bitwise Mangler on the hats; "
                        "the input level drives the kick band and transients drive the hats.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 120.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Fuzz, 3.5f, 1500.0f);
        shapeFuzz(p.state.bandMorph[0].nodes[0], 0.2f, 0, 1.0f, 0.3f);
        setBandType(p.state.bandMorph[1], DistortionType::Tube, 3.0f, 4500.0f);
        shapeTube(p.state.bandMorph[1].nodes[0], 0.15f, 0.6f, 2);
        setBandType(p.state.bandMorph[2], DistortionType::BitwiseMangler, 2.0f, 7000.0f);
        shapeBitwise(p.state.bandMorph[2].nodes[0], 1, 0.5f, 0.3f, 0.6f, 0.1f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.3f);
        addRouting(p.state, 1, ModSource::Transient, ModDest::bandDrive(2), 0.35f);
        p.state.modSources.envSensitivity = 0.7f;
        p.state.modSources.transSensitivity = 0.75f;
        presets.push_back(p);
    }
    { // Snare Crack: transient-reactive per band, different saturation
        PresetDef p; p.name = "Snare Crack"; p.category = "Drums";
        p.description = "Hard Clip at drive 4 on the 200 Hz to 5 kHz band, pushed further by every transient, "
                        "between Tape lows and a Ring Saturation top at 60 % mix that each hit opens fully. For "
                        "snares and claps.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 5000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 2.0f, 1500.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.0f, 0.5f, 0.4f, 1, 0.7f, 0.15f);
        setBandType(p.state.bandMorph[1], DistortionType::HardClip, 4.0f, 5500.0f);
        shapeHardClip(p.state.bandMorph[1].nodes[0], 0.4f, 0.85f);
        setBandType(p.state.bandMorph[2], DistortionType::RingSaturation, 1.5f, 7500.0f);
        shapeRingSat(p.state.bandMorph[2].nodes[0], 0.4f, 1, 0.5f, 2, 0.0f, 1);
        setBandMix(p.state.bandMorph[2], 0.6f);  // each hit opens it (routing 1)
        p.state.bands[1].gainDb = 2.0f;
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(1), 0.6f);
        addRouting(p.state, 1, ModSource::Transient, ModDest::bandMix(2), 0.4f);
        p.state.modSources.transSensitivity = 0.8f;
        p.state.modSources.transAttackNorm = 0.02f;
        p.state.modSources.transDecayNorm = 0.1f;
        presets.push_back(p);
    }
    { // Kick Saturate: tape subs, tube body, soft clip top
        PresetDef p; p.name = "Kick Saturate"; p.category = "Drums";
        p.description = "Tape on the sub below 80 Hz, raised 3 dB, Tube on the 80 to 300 Hz body and Soft Clip "
                        "above; the input level and each transient drive the sub. Thick, saturated kicks.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 80.0f;
        p.state.crossoverFreqs[1] = 300.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 3.0f, 1000.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.05f, 0.6f, 0.3f, 1, 0.8f, 0.2f);
        setBandType(p.state.bandMorph[1], DistortionType::Tube, 2.5f, 2500.0f);
        shapeTube(p.state.bandMorph[1].nodes[0], 0.1f, 0.5f, 2);
        setBandType(p.state.bandMorph[2], DistortionType::SoftClip, 1.5f, 5000.0f);
        shapeSoftClip(p.state.bandMorph[2].nodes[0], 0.7f, 0.25f);
        p.state.bands[0].gainDb = 3.0f;
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.3f);
        addRouting(p.state, 1, ModSource::Transient, ModDest::bandDrive(0), 0.4f);
        p.state.modSources.envSensitivity = 0.7f;
        p.state.modSources.transSensitivity = 0.75f;
        presets.push_back(p);
    }
    { // Glitch Drum: sample reduce + bitwise morph, S&H modulated
        PresetDef p; p.name = "Glitch Drum"; p.category = "Drums";
        p.description = "A sample-and-hold source jumps the mids between Sample Reduce and Bitwise Mangler and "
                        "steps the Aliasing top band drive, over Hard Clip lows. Glitchy, stuttering drum loops.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::HardClip, 2.5f, 1500.0f);
        shapeHardClip(p.state.bandMorph[0].nodes[0], 0.5f, 0.8f);
        setMorphAB(p.state.bandMorph[1], DistortionType::SampleReduce, DistortionType::BitwiseMangler,
                   2.0f, 2.0f, 4000.0f, 5000.0f);
        shapeSampleReduce(p.state.bandMorph[1].nodes[0], 12.0f, 0.3f, 1, 0.1f);
        shapeBitwise(p.state.bandMorph[1].nodes[1], 3, 0.6f, 0.5f, 0.4f, 0.1f);
        setBandType(p.state.bandMorph[2], DistortionType::Aliasing, 1.5f, 7000.0f);
        shapeAliasing(p.state.bandMorph[2].nodes[0], 5.0f, 300.0f, false, 0.15f, 0.2f);
        addRouting(p.state, 0, ModSource::SampleHold, ModDest::bandMorphX(1), 0.6f);
        addRouting(p.state, 1, ModSource::SampleHold, ModDest::bandDrive(2), 0.3f);
        addRouting(p.state, 2, ModSource::LFO1, ModDest::bandPan(1), 0.25f);
        p.state.modSources.shRateNorm = 0.5f;
        p.state.modSources.shSlewNorm = 0.2f;
        p.state.modSources.lfo1RateNorm = 0.7f;
        p.state.modSources.lfo1Shape = Waveform::SampleAndHold;
        presets.push_back(p);
    }
    { // Transient Shape: transient controls mix/drive differently per band
        PresetDef p; p.name = "Transient Shape"; p.category = "Drums";
        p.description = "Soft Clip under 200 Hz at half mix that each transient opens fully, Temporal mids that "
                        "each transient drives harder, and a light Tube top the input level pushes. Adds crunch to "
                        "the attack of each drum hit.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::SoftClip, 2.0f, 2000.0f);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.7f, 0.3f);
        setBandMix(p.state.bandMorph[0], 0.5f);  // each transient opens it (routing 0)
        setBandType(p.state.bandMorph[1], DistortionType::Temporal, 2.5f, 4500.0f);
        shapeTemporal(p.state.bandMorph[1].nodes[0], 0, 0.6f, 0.5f, 5.0f, 80.0f, 0.6f);
        setBandType(p.state.bandMorph[2], DistortionType::Tube, 1.5f, 6500.0f);
        shapeTube(p.state.bandMorph[2].nodes[0], 0.0f, 0.3f, 1);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandMix(0), 0.5f);
        addRouting(p.state, 1, ModSource::Transient, ModDest::bandDrive(1), 0.6f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandDrive(2), 0.3f);
        p.state.modSources.transSensitivity = 0.75f;
        p.state.modSources.transAttackNorm = 0.04f;
        p.state.modSources.transDecayNorm = 0.2f;
        p.state.modSources.envSensitivity = 0.6f;
        presets.push_back(p);
    }
    { // Rectified Drums: full rectify kick, half rectify snare, ring sat hats
        PresetDef p; p.name = "Rectified Drums"; p.category = "Drums";
        p.description = "Full Rectify on the kick band below 150 Hz, Half Rectify across the snare range and Ring "
                        "Saturation on the cymbals; transients drive the kick band. Hollow, buzzing drums.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 150.0f;
        p.state.crossoverFreqs[1] = 4500.0f;
        setBandType(p.state.bandMorph[0], DistortionType::FullRectify, 1.5f, 1500.0f);
        shapeFullRect(p.state.bandMorph[0].nodes[0], 0.1f, true);
        setBandType(p.state.bandMorph[1], DistortionType::HalfRectify, 2.0f, 4500.0f);
        shapeHalfRect(p.state.bandMorph[1].nodes[0], 0.3f, 0.15f, true);
        setBandType(p.state.bandMorph[2], DistortionType::RingSaturation, 2.0f, 7000.0f);
        shapeRingSat(p.state.bandMorph[2].nodes[0], 0.5f, 2, 0.5f, 1, 0.0f, 2);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(0), 0.4f);
        addRouting(p.state, 1, ModSource::LFO1, ModDest::bandDrive(2), 0.2f);
        p.state.modSources.transSensitivity = 0.7f;
        p.state.modSources.lfo1RateNorm = 0.5f;
        p.state.modSources.lfo1Shape = Waveform::Triangle;
        presets.push_back(p);
    }
    { // Aliased Drums: aliasing mids, feedback lows, chaos highs
        PresetDef p; p.name = "Aliased Drums"; p.category = "Drums";
        p.description = "Aliasing across the 200 Hz to 5 kHz band, driven harder by each transient, over Feedback "
                        "Distortion lows and a Chaos top that a chaos source drives. Metallic, broken drums.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 5000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::FeedbackDist, 2.5f, 1500.0f);
        shapeFeedback(p.state.bandMorph[0].nodes[0], 0.5f, 8.0f, 0.5f, 0, 0.3f, 1);
        setBandType(p.state.bandMorph[1], DistortionType::Aliasing, 2.5f, 5500.0f);
        shapeAliasing(p.state.bandMorph[1].nodes[0], 5.0f, 200.0f, true, 0.2f, 0.3f);
        setBandType(p.state.bandMorph[2], DistortionType::Chaos, 2.0f, 7000.0f);
        shapeChaos(p.state.bandMorph[2].nodes[0], 3, 8.0f, 0.5f, 0.3f, 0.5f, 0.4f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(1), 0.5f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(2), 0.25f);
        p.state.modSources.transSensitivity = 0.8f;
        p.state.modSources.chaosSpeedNorm = 0.35f;
        p.state.bands[1].pan = -0.15f;
        p.state.bands[2].pan = 0.15f;
        presets.push_back(p);
    }

    // =========================================================================
    // EXPERIMENTAL (16 presets) - Creative, unusual
    // =========================================================================
    { // Spectral Scatter: spectral mids, granular lows, S&H sweep
        PresetDef p; p.name = "Spectral Scatter"; p.category = "Experimental";
        p.description = "Four textural bands (Granular, Spectral, Fractal, Stochastic) under a sweep that a "
                        "sample-and-hold LFO throws around; a sample-and-hold source also steps the Spectral "
                        "drive.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 1500.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        enableSweep(p.state, 0.5f, 0.6f, 0.5f);
        enableSweepLFO(p.state, 0.4f, 0.7f, Waveform::SampleAndHold);
        setBandType(p.state.bandMorph[0], DistortionType::Granular, 2.5f, 2000.0f);
        shapeGranular(p.state.bandMorph[0].nodes[0], 20.0f, 0.7f, 0.3f, 0.2f, 0.5f, 0.4f, 1, 2);
        setBandType(p.state.bandMorph[1], DistortionType::Spectral, 2.5f, 4500.0f);
        shapeSpectral(p.state.bandMorph[1].nodes[0], 2, 2, 0.7f, 0.5f, 0.2f, 2, 0.6f, 1);
        setBandType(p.state.bandMorph[2], DistortionType::Fractal, 2.0f, 6000.0f);
        shapeFractal(p.state.bandMorph[2].nodes[0], 3, 5, 0.6f, 0.5f, 0.5f, 0.15f, 1);
        setBandType(p.state.bandMorph[3], DistortionType::Stochastic, 1.5f, 7000.0f);
        shapeStochastic(p.state.bandMorph[3].nodes[0], 3, 0.3f, 25.0f, 0.15f, 0.2f, 2, 0.4f);
        addRouting(p.state, 0, ModSource::SampleHold, ModDest::bandDrive(1), 0.3f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandPan(2), 0.25f);
        p.state.modSources.shRateNorm = 0.4f;
        p.state.modSources.chaosSpeedNorm = 0.2f;
        p.state.bands[1].pan = -0.2f;
        p.state.bands[3].pan = 0.2f;
        presets.push_back(p);
    }
    { // Granular Crush: granular mids + bitcrush lows + chaos highs
        PresetDef p; p.name = "Granular Crush"; p.category = "Experimental";
        p.description = "6-bit Bitcrush on the lows, dense 15 ms Granular grains on the mids and a Chaos top, with "
                        "LFO 1, a chaos source and a random source each moving one band. Broken, grainy textures.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Bitcrush, 2.0f, 2000.0f);
        shapeBitcrush(p.state.bandMorph[0].nodes[0], 6.0f, 0.2f, 1, 0.2f);
        setBandType(p.state.bandMorph[1], DistortionType::Granular, 3.0f, 5000.0f);
        shapeGranular(p.state.bandMorph[1].nodes[0], 15.0f, 0.8f, 0.4f, 0.3f, 0.2f, 0.3f, 0, 3);
        setBandType(p.state.bandMorph[2], DistortionType::Chaos, 2.5f, 7000.0f);
        shapeChaos(p.state.bandMorph[2].nodes[0], 2, 6.0f, 0.6f, 0.5f, 0.6f, 0.4f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(1), 0.3f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(2), 0.25f);
        addRouting(p.state, 2, ModSource::Random, ModDest::bandPan(0), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.3f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        p.state.modSources.chaosSpeedNorm = 0.25f;
        p.state.modSources.randomRateNorm = 0.2f;
        p.state.modSources.randomSmoothness = 0.5f;
        presets.push_back(p);
    }
    { // Fractal Noise: fractal + stochastic + feedback per band
        PresetDef p; p.name = "Fractal Noise"; p.category = "Experimental";
        p.description = "Feedback Distortion, Fractal at drive 4 and Stochastic at drive 3, with a Chua chaos "
                        "source driving the lower two and a random pan on top. Noisy, unstable textures for "
                        "drones.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::FeedbackDist, 3.5f, 2000.0f);
        shapeFeedback(p.state.bandMorph[0].nodes[0], 0.85f, 18.0f, 0.7f, 1, 0.3f, 2, true, 0.6f);
        setBandType(p.state.bandMorph[1], DistortionType::Fractal, 4.0f, 5000.0f);
        shapeFractal(p.state.bandMorph[1].nodes[0], 3, 7, 0.7f, 0.6f, 0.6f, 0.3f, 2, 0.8f);
        setBandType(p.state.bandMorph[2], DistortionType::Stochastic, 3.0f, 7000.0f);
        shapeStochastic(p.state.bandMorph[2].nodes[0], 4, 0.5f, 40.0f, 0.3f, 0.35f, 3, 0.3f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(0), 0.4f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(1), 0.5f);
        addRouting(p.state, 2, ModSource::Random, ModDest::bandPan(2), 0.3f);
        p.state.modSources.chaosModel = ChaosModel::Chua;
        p.state.modSources.chaosSpeedNorm = 0.3f;
        p.state.modSources.chaosCoupling = 0.6f;
        p.state.modSources.randomRateNorm = 0.3f;
        presets.push_back(p);
    }
    { // Stochastic Burst: stochastic + granular + bitwise per band
        PresetDef p; p.name = "Stochastic Burst"; p.category = "Experimental";
        p.description = "Tiny 10 ms Granular grains, Stochastic mids and a Bitwise Mangler top, with a random "
                        "source and a sample-and-hold source throwing all three drives around. Bursts of noise and "
                        "grit.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Granular, 2.5f, 2000.0f);
        shapeGranular(p.state.bandMorph[0].nodes[0], 10.0f, 0.9f, 0.5f, 0.4f, 0.3f, 0.3f, 0, 3);
        setBandType(p.state.bandMorph[1], DistortionType::Stochastic, 3.5f, 4500.0f);
        shapeStochastic(p.state.bandMorph[1].nodes[0], 5, 0.5f, 50.0f, 0.25f, 0.4f, 2, 0.25f);
        setBandType(p.state.bandMorph[2], DistortionType::BitwiseMangler, 2.5f, 6500.0f);
        shapeBitwise(p.state.bandMorph[2].nodes[0], 4, 0.7f, 0.6f, 0.7f, 0.05f);
        addRouting(p.state, 0, ModSource::Random, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::Random, ModDest::bandDrive(1), 0.6f);
        addRouting(p.state, 2, ModSource::SampleHold, ModDest::bandDrive(2), 0.4f);
        p.state.modSources.randomRateNorm = 0.5f;
        p.state.modSources.randomSmoothness = 0.3f;
        p.state.modSources.shRateNorm = 0.4f;
        p.state.modSources.shSlewNorm = 0.15f;
        presets.push_back(p);
    }
    { // Formant Vowels: formant morph across vowels, sweep-linked
        PresetDef p; p.name = "Formant Vowels"; p.category = "Experimental";
        p.description = "The mids morph between two Formant vowels, the second shifted up, moved by the sweep and a "
                        "slow LFO; the same sweep sags the Tube lows darker and tunes the Allpass Resonant top "
                        "higher. Talking synths and vocals.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        enableSweep(p.state, 0.4f, 0.3f, 0.6f, MorphLinkMode::SweepFreq);
        enableSweepLFO(p.state, 0.15f, 0.5f, Waveform::Sine);
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 2.0f, 2000.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.1f, 0.5f, 1);
        // the sweep pushes the lows into a darker, saggier tube
        p.state.bandMorph[0].nodes[1].drive = 4.0f;
        p.state.bandMorph[0].nodes[1].toneHz = 1200.0f;
        shapeTube(p.state.bandMorph[0].nodes[1], 0.35f, 0.85f, 1);
        // Mid: formant morph A→E vowels
        setMorphAB(p.state.bandMorph[1], DistortionType::Formant, DistortionType::Formant,
                   2.5f, 2.5f, 5000.0f, 5500.0f);
        shapeFormant(p.state.bandMorph[1].nodes[0], 0, 0.0f, 0.6f, 0.7f, 0.4f, 2, 0.3f, 0.5f);
        shapeFormant(p.state.bandMorph[1].nodes[1], 3, 6.0f, 0.5f, 0.6f, 0.5f, 1, 0.7f, 0.6f);
        setBandType(p.state.bandMorph[2], DistortionType::AllpassResonant, 1.5f, 7000.0f);
        shapeAllpass(p.state.bandMorph[2].nodes[0], 1, 600.0f, 0.6f, 1.5f, 0.4f, 2, true);
        // the sweep tunes the ring higher, shorter and brighter
        shapeAllpass(p.state.bandMorph[2].nodes[1], 1, 1400.0f, 0.75f, 0.8f, 0.55f, 2, true, 0.2f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(1), 0.5f);
        addRouting(p.state, 1, ModSource::PitchFollower, ModDest::bandDrive(1), 0.3f);
        p.state.modSources.lfo1RateNorm = 0.1f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.pitchConfidence = 0.7f;
        presets.push_back(p);
    }
    { // Chaos Engine: 4-node chaos/fractal/stochastic/feedback morph
        PresetDef p; p.name = "Chaos Engine"; p.category = "Experimental";
        p.description = "A Lorenz chaos source throws the lows around a four-node XY morph of Chaos, Fractal, "
                        "Stochastic and Feedback Distortion, with Serge Fold mids on a saw LFO and a Spectral top.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        // Low: 4-node chaos morph
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::Chaos, DistortionType::Fractal,
            DistortionType::Stochastic, DistortionType::FeedbackDist);
        shapeChaos(p.state.bandMorph[0].nodes[0], 0, 5.0f, 0.7f, 0.6f, 0.6f, 0.5f);
        shapeFractal(p.state.bandMorph[0].nodes[1], 2, 6, 0.6f, 0.5f, 0.6f, 0.2f, 1);
        shapeStochastic(p.state.bandMorph[0].nodes[2], 3, 0.4f, 30.0f, 0.2f, 0.3f, 2, 0.3f);
        shapeFeedback(p.state.bandMorph[0].nodes[3], 0.7f, 12.0f, 0.6f, 1, 0.4f, 2);
        p.state.bandMorph[0].nodes[0].drive = 2.5f;
        p.state.bandMorph[0].nodes[1].drive = 3.0f;
        p.state.bandMorph[0].nodes[2].drive = 2.0f;
        p.state.bandMorph[0].nodes[3].drive = 3.0f;
        // Mid: serge fold for texture
        setBandType(p.state.bandMorph[1], DistortionType::SergeFold, 3.0f, 4500.0f);
        shapeSergeFold(p.state.bandMorph[1].nodes[0], 4.0f, -0.2f, 2, 0.1f, 0.4f);
        // High: spectral
        setBandType(p.state.bandMorph[2], DistortionType::Spectral, 2.0f, 6500.0f);
        shapeSpectral(p.state.bandMorph[2].nodes[0], 3, 1, 0.6f, 0.5f, 0.2f, 2, 0.5f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandMorphX(0), 0.7f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandMorphY(0), 0.7f);
        addRouting(p.state, 2, ModSource::LFO1, ModDest::bandDrive(1), 0.3f);
        p.state.modSources.chaosModel = ChaosModel::Lorenz;
        p.state.modSources.chaosSpeedNorm = 0.35f;
        p.state.modSources.chaosCoupling = 0.6f;
        p.state.modSources.lfo1RateNorm = 0.25f;
        p.state.modSources.lfo1Shape = Waveform::Saw;
        presets.push_back(p);
    }
    { // Bitwise Mangler: bitwise morph + aliasing + quantize per band
        PresetDef p; p.name = "Bitwise Mangler"; p.category = "Experimental";
        p.description = "A saw LFO morphs the mids from Bitwise Mangler to Aliasing, over Quantize lows stepped by "
                        "a sample-and-hold source and a Sample Reduce top that LFO 2 pans. Digital wreckage.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Quantize, 2.0f, 2000.0f);
        shapeQuantize(p.state.bandMorph[0].nodes[0], 0.3f, 0.15f, 0.1f, 0.25f);
        setMorphAB(p.state.bandMorph[1], DistortionType::BitwiseMangler, DistortionType::Aliasing,
                   2.5f, 2.0f, 5000.0f, 5500.0f);
        shapeBitwise(p.state.bandMorph[1].nodes[0], 4, 0.8f, 0.5f, 0.6f, 0.05f);
        shapeAliasing(p.state.bandMorph[1].nodes[1], 6.0f, 400.0f, true, 0.2f, 0.4f);
        setBandType(p.state.bandMorph[2], DistortionType::SampleReduce, 2.0f, 7000.0f);
        shapeSampleReduce(p.state.bandMorph[2].nodes[0], 10.0f, 0.25f, 1, 0.1f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(1), 0.5f);
        addRouting(p.state, 1, ModSource::SampleHold, ModDest::bandDrive(0), 0.4f);
        addRouting(p.state, 2, ModSource::LFO2, ModDest::bandPan(2), 0.3f);
        p.state.modSources.lfo1RateNorm = 0.4f;
        p.state.modSources.lfo1Shape = Waveform::Saw;
        p.state.modSources.lfo2RateNorm = 0.6f;
        p.state.modSources.lfo2Shape = Waveform::Sine;
        p.state.modSources.shRateNorm = 0.35f;
        presets.push_back(p);
    }
    { // Ring Chaos: ring sat morph to chaos, allpass underneath
        PresetDef p; p.name = "Ring Chaos"; p.category = "Experimental";
        p.description = "The mids morph between Ring Saturation and Chaos on a smooth random LFO, with Allpass "
                        "Resonant lows that a chaos source drives and a Fractal top. Metallic one moment, unstable "
                        "the next.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::AllpassResonant, 2.0f, 2500.0f);
        shapeAllpass(p.state.bandMorph[0].nodes[0], 2, 250.0f, 0.75f, 2.0f, 0.5f, 3, false, 0.35f);
        setMorphAB(p.state.bandMorph[1], DistortionType::RingSaturation, DistortionType::Chaos,
                   3.0f, 2.5f, 5000.0f, 5000.0f);
        shapeRingSat(p.state.bandMorph[1].nodes[0], 0.8f, 3, 0.6f, 2, 0.15f, 2);
        shapeChaos(p.state.bandMorph[1].nodes[1], 1, 7.0f, 0.6f, 0.5f, 0.5f, 0.6f);
        setBandType(p.state.bandMorph[2], DistortionType::Fractal, 2.0f, 7000.0f);
        shapeFractal(p.state.bandMorph[2].nodes[0], 1, 5, 0.5f, 0.5f, 0.5f, 0.15f, 2);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(1), 0.6f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(0), 0.2f);
        addRouting(p.state, 2, ModSource::LFO2, ModDest::bandPan(1), 0.25f);
        p.state.modSources.lfo1RateNorm = 0.35f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        p.state.modSources.lfo2RateNorm = 0.4f;
        p.state.modSources.lfo2Shape = Waveform::Sine;
        p.state.modSources.chaosSpeedNorm = 0.2f;
        presets.push_back(p);
    }
    { // Quantize Glitch: quantize + S&H modulated across bands
        PresetDef p; p.name = "Quantize Glitch"; p.category = "Experimental";
        p.description = "Quantize, 6-bit Bitcrush and Bitwise Mangler bands whose drives jump in steps on "
                        "sample-and-hold sources, with the outer bands panned apart. Stepped, glitchy digital "
                        "grit.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Quantize, 2.5f, 2000.0f);
        shapeQuantize(p.state.bandMorph[0].nodes[0], 0.25f, 0.2f, 0.05f, 0.3f);
        setBandType(p.state.bandMorph[1], DistortionType::Bitcrush, 2.0f, 4500.0f);
        shapeBitcrush(p.state.bandMorph[1].nodes[0], 6.0f, 0.3f, 1, 0.2f);
        setBandType(p.state.bandMorph[2], DistortionType::BitwiseMangler, 2.0f, 7000.0f);
        shapeBitwise(p.state.bandMorph[2].nodes[0], 2, 0.6f, 0.4f, 0.5f, 0.1f);
        addRouting(p.state, 0, ModSource::SampleHold, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::SampleHold, ModDest::bandDrive(1), 0.4f);
        addRouting(p.state, 2, ModSource::LFO1, ModDest::bandDrive(2), 0.3f);
        p.state.modSources.shRateNorm = 0.35f;
        p.state.modSources.shSlewNorm = 0.2f;
        p.state.modSources.lfo1RateNorm = 0.6f;
        p.state.modSources.lfo1Shape = Waveform::SampleAndHold;
        p.state.bands[0].pan = -0.15f;
        p.state.bands[2].pan = 0.15f;
        presets.push_back(p);
    }
    { // All Types Morph: 4-band with 4-node morphs on each
        PresetDef p; p.name = "All Types Morph"; p.category = "Experimental";
        p.description = "Sixteen types at once: each of four bands runs a four-node morph (analog, experimental, "
                        "digital and resonant types from low to high), its X moved by LFO 1, LFO 2, chaos or "
                        "random.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 1500.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        // Band 0: analog flavors
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::SoftClip, DistortionType::Fuzz,
            DistortionType::SineFold, DistortionType::Tape);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.7f, 0.3f);
        shapeFuzz(p.state.bandMorph[0].nodes[1], 0.2f, 0, 1.0f, 0.3f);
        shapeSineFold(p.state.bandMorph[0].nodes[2], 3.0f, 0.2f, 0.4f);
        shapeTape(p.state.bandMorph[0].nodes[3], 0.0f, 0.5f, 0.5f, 1, 0.5f, 0.2f);
        // Band 1: experimental flavors
        setMorph4Node(p.state.bandMorph[1],
            DistortionType::Chaos, DistortionType::Spectral,
            DistortionType::Formant, DistortionType::Granular);
        shapeChaos(p.state.bandMorph[1].nodes[0], 0, 4.0f, 0.5f, 0.4f);
        shapeSpectral(p.state.bandMorph[1].nodes[1], 1, 2, 0.5f, 0.5f, 0.1f);
        shapeFormant(p.state.bandMorph[1].nodes[2], 1, 3.0f, 0.5f, 0.6f, 0.5f, 1);
        shapeGranular(p.state.bandMorph[1].nodes[3], 30.0f, 0.6f, 0.2f, 0.15f, 0.3f);
        // Band 2: digital flavors
        setMorph4Node(p.state.bandMorph[2],
            DistortionType::Bitcrush, DistortionType::Aliasing,
            DistortionType::BitwiseMangler, DistortionType::SampleReduce);
        shapeBitcrush(p.state.bandMorph[2].nodes[0], 8.0f, 0.2f, 0, 0.1f);
        shapeAliasing(p.state.bandMorph[2].nodes[1], 5.0f, 200.0f, true, 0.15f, 0.3f);
        shapeBitwise(p.state.bandMorph[2].nodes[2], 3, 0.6f, 0.4f, 0.5f);
        shapeSampleReduce(p.state.bandMorph[2].nodes[3], 8.0f, 0.15f, 0, 0.1f);
        // Band 3: resonant flavors
        setMorph4Node(p.state.bandMorph[3],
            DistortionType::AllpassResonant, DistortionType::FeedbackDist,
            DistortionType::RingSaturation, DistortionType::Fractal);
        shapeAllpass(p.state.bandMorph[3].nodes[0], 1, 500.0f, 0.6f, 1.5f);
        shapeFeedback(p.state.bandMorph[3].nodes[1], 0.6f, 8.0f, 0.5f, 1, 0.5f, 1);
        shapeRingSat(p.state.bandMorph[3].nodes[2], 0.6f, 2, 0.5f, 1);
        shapeFractal(p.state.bandMorph[3].nodes[3], 1, 4, 0.5f, 0.5f, 0.5f, 0.1f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(0), 0.3f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandMorphX(1), 0.3f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandMorphX(2), 0.3f);
        addRouting(p.state, 3, ModSource::Random, ModDest::bandMorphX(3), 0.3f);
        p.state.modSources.lfo1RateNorm = 0.08f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        p.state.modSources.lfo2RateNorm = 0.1f;
        p.state.modSources.lfo2Shape = Waveform::Triangle;
        p.state.modSources.chaosSpeedNorm = 0.12f;
        p.state.modSources.randomRateNorm = 0.15f;
        p.state.modSources.randomSmoothness = 0.6f;
        presets.push_back(p);
    }
    { // Alien Voice: formant + ring sat + spectral, pitch-tracked
        PresetDef p; p.name = "Alien Voice"; p.category = "Experimental";
        p.description = "Ring Saturation lows, a Formant mid band shifted up 8 semitones and a Spectral top, with "
                        "the input pitch driving the Formant and ring bands. Robotic, alien voices; try vocals.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::RingSaturation, 2.5f, 2500.0f);
        shapeRingSat(p.state.bandMorph[0].nodes[0], 0.7f, 3, 0.5f, 3, 0.2f, 2);
        setBandType(p.state.bandMorph[1], DistortionType::Formant, 3.0f, 5000.0f);
        shapeFormant(p.state.bandMorph[1].nodes[0], 0, 8.0f, 0.7f, 0.8f, 0.3f, 3, 0.2f, 0.5f);
        setBandType(p.state.bandMorph[2], DistortionType::Spectral, 2.0f, 7000.0f);
        shapeSpectral(p.state.bandMorph[2].nodes[0], 2, 1, 0.6f, 0.4f, 0.15f, 2, 0.6f, 2);
        addRouting(p.state, 0, ModSource::PitchFollower, ModDest::bandDrive(1), 0.4f);
        addRouting(p.state, 1, ModSource::PitchFollower, ModDest::bandDrive(0), 0.3f);
        addRouting(p.state, 2, ModSource::LFO1, ModDest::bandPan(0), 0.25f);
        p.state.modSources.pitchConfidence = 0.7f;
        p.state.modSources.pitchMinNorm = 0.1f;
        p.state.modSources.pitchMaxNorm = 0.6f;
        p.state.modSources.lfo1RateNorm = 0.35f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.bands[0].pan = -0.15f;
        p.state.bands[2].pan = 0.15f;
        presets.push_back(p);
    }
    { // Feedback Loop: feedback dist on 2 bands, env-ducked
        PresetDef p; p.name = "Feedback Loop"; p.category = "Experimental";
        p.description = "Feedback Distortion at drive 4 and 5 on the lows and mids, pulled back by the input level "
                        "while you play so it grows as notes decay, with a Chaos top that a chaos source pans.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::FeedbackDist, 4.0f, 2000.0f);
        shapeFeedback(p.state.bandMorph[0].nodes[0], 0.9f, 25.0f, 0.7f, 0, 0.3f, 3, true, 0.5f);
        setBandType(p.state.bandMorph[1], DistortionType::FeedbackDist, 5.0f, 5000.0f);
        shapeFeedback(p.state.bandMorph[1].nodes[0], 0.8f, 5.0f, 0.5f, 2, 0.6f, 2, true, 0.6f);
        setBandType(p.state.bandMorph[2], DistortionType::Chaos, 2.5f, 7000.0f);
        shapeChaos(p.state.bandMorph[2].nodes[0], 0, 6.0f, 0.5f, 0.4f, 0.5f, 0.5f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), -0.3f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandDrive(1), -0.25f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandPan(2), 0.3f);
        p.state.modSources.envSensitivity = 0.8f;
        p.state.modSources.envAttackNorm = 0.02f;
        p.state.modSources.envReleaseNorm = 0.25f;
        p.state.modSources.chaosSpeedNorm = 0.2f;
        presets.push_back(p);
    }
    { // Serge Madness: serge fold + sine fold + tri fold, sweep-animated
        PresetDef p; p.name = "Serge Madness"; p.category = "Experimental";
        p.description = "All three wavefolders, Serge, Sine and Triangle from low to high at high fold counts, "
                        "under a saw-LFO sweep, with LFO 1, LFO 2 and a chaos source each moving one band drive.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        enableSweep(p.state, 0.5f, 0.5f, 0.7f);
        enableSweepLFO(p.state, 0.5f, 0.8f, Waveform::Saw);
        setBandType(p.state.bandMorph[0], DistortionType::SergeFold, 4.0f, 2500.0f);
        shapeSergeFold(p.state.bandMorph[0].nodes[0], 6.0f, -0.3f, 3, 0.15f, 0.5f, 0.05f);
        setBandType(p.state.bandMorph[1], DistortionType::SineFold, 3.5f, 5000.0f);
        shapeSineFold(p.state.bandMorph[1].nodes[0], 5.0f, 0.4f, 0.6f, 0.1f, 0.05f);
        setBandType(p.state.bandMorph[2], DistortionType::TriangleFold, 3.0f, 7000.0f);
        shapeTriFold(p.state.bandMorph[2].nodes[0], 4.0f, -0.2f, 0.8f, 0.0f, 0.1f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.4f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandDrive(1), 0.35f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandDrive(2), 0.3f);
        p.state.modSources.lfo1RateNorm = 0.3f;
        p.state.modSources.lfo1Shape = Waveform::Triangle;
        p.state.modSources.lfo2RateNorm = 0.45f;
        p.state.modSources.lfo2Shape = Waveform::Saw;
        p.state.modSources.chaosSpeedNorm = 0.25f;
        p.state.bands[0].pan = -0.2f;
        p.state.bands[2].pan = 0.2f;
        presets.push_back(p);
    }
    { // Temporal Flux: temporal on mids, feedback lows, chaos highs, dual-LFO
        PresetDef p; p.name = "Temporal Flux"; p.category = "Experimental";
        p.description = "Two Temporal bands in different modes between Feedback Distortion lows and a Chaos top; a "
                        "sine LFO, a sample-and-hold LFO and the transient detector each drive one band.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 1500.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::FeedbackDist, 3.0f, 1800.0f);
        shapeFeedback(p.state.bandMorph[0].nodes[0], 0.6f, 15.0f, 0.6f, 0, 0.3f, 2);
        setBandType(p.state.bandMorph[1], DistortionType::Temporal, 3.5f, 4000.0f);
        shapeTemporal(p.state.bandMorph[1].nodes[0], 1, 0.7f, 0.6f, 8.0f, 120.0f, 0.7f, 1, 0.2f);
        setBandType(p.state.bandMorph[2], DistortionType::Temporal, 3.0f, 5500.0f);
        shapeTemporal(p.state.bandMorph[2].nodes[0], 2, 0.6f, 0.4f, 3.0f, 60.0f, 0.6f, 0, 0.1f);
        setBandType(p.state.bandMorph[3], DistortionType::Chaos, 2.5f, 7000.0f);
        shapeChaos(p.state.bandMorph[3].nodes[0], 2, 7.0f, 0.5f, 0.4f, 0.6f, 0.4f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.4f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandDrive(1), 0.35f);
        addRouting(p.state, 2, ModSource::Transient, ModDest::bandDrive(2), 0.5f);
        addRouting(p.state, 3, ModSource::Chaos, ModDest::bandPan(3), 0.3f);
        p.state.modSources.lfo1RateNorm = 0.35f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.lfo2RateNorm = 0.5f;
        p.state.modSources.lfo2Shape = Waveform::SampleAndHold;
        p.state.modSources.transSensitivity = 0.7f;
        p.state.modSources.chaosSpeedNorm = 0.3f;
        presets.push_back(p);
    }
    { // Broken Radio: aliasing + sample reduce + noise per band
        PresetDef p; p.name = "Broken Radio"; p.category = "Experimental";
        p.description = "A random source flips the lows between Aliasing and Sample Reduce, over noisy Stochastic "
                        "mids and a Bitwise Mangler top stepped by a sample-and-hold source. A failing radio.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setMorphAB(p.state.bandMorph[0], DistortionType::Aliasing, DistortionType::SampleReduce,
                   2.0f, 2.0f, 2000.0f, 2500.0f);
        shapeAliasing(p.state.bandMorph[0].nodes[0], 8.0f, 500.0f, false, 0.3f, 0.4f);
        shapeSampleReduce(p.state.bandMorph[0].nodes[1], 15.0f, 0.3f, 1, 0.05f);
        setBandType(p.state.bandMorph[1], DistortionType::Stochastic, 2.5f, 5000.0f);
        shapeStochastic(p.state.bandMorph[1].nodes[0], 4, 0.4f, 35.0f, 0.2f, 0.25f, 1, 0.3f);
        setBandType(p.state.bandMorph[2], DistortionType::BitwiseMangler, 2.5f, 7000.0f);
        shapeBitwise(p.state.bandMorph[2].nodes[0], 5, 0.7f, 0.6f, 0.7f, 0.05f);
        addRouting(p.state, 0, ModSource::Random, ModDest::bandMorphX(0), 0.7f);
        addRouting(p.state, 1, ModSource::Random, ModDest::bandDrive(1), 0.4f);
        addRouting(p.state, 2, ModSource::SampleHold, ModDest::bandDrive(2), 0.5f);
        p.state.modSources.randomRateNorm = 0.5f;
        p.state.modSources.randomSmoothness = 0.2f;
        p.state.modSources.shRateNorm = 0.45f;
        p.state.modSources.shSlewNorm = 0.1f;
        p.state.bands[0].pan = -0.2f;
        p.state.bands[2].pan = 0.2f;
        presets.push_back(p);
    }
    { // Formant Shift: a Formant node on every band, the sweep driving morph X
        // (the recipe of the v8 Experimental/Formant_Shift file the v9 overhaul dropped)
        PresetDef p; p.name = "Formant Shift"; p.category = "Experimental";
        p.description = "Formant on all four bands, vowels U, I, A and O shifted from -12 to +12 semitones low to "
                        "high; a slow sine-LFO sweep slides every band's formants about 4 semitones up and down as "
                        "it moves.";
        p.state = makeInitState(4);
        for (auto& bm : p.state.bandMorph)
            setBandType(bm, DistortionType::Formant, 2.5f);
        // Per band one vowel (Vowel Select, Blend 0.5 = exactly that vowel); node A sits 12 st
        // below the band's shift, node B 12 st above.
        constexpr std::array<int, 4> kVowels = {4, 2, 0, 3};               // U, I, A, O
        constexpr std::array<float, 4> kShifts = {-12.0f, -4.0f, 4.0f, 12.0f};
        for (size_t b = 0; b < 4; ++b) {
            shapeFormant(p.state.bandMorph[b].nodes[0], kVowels[b], kShifts[b] - 12.0f, 0.5f, 0.5f, 0.5f, 2, 0.5f,
                         0.5f);
            shapeFormant(p.state.bandMorph[b].nodes[1], kVowels[b], kShifts[b] + 12.0f, 0.5f, 0.5f, 0.5f, 2, 0.5f,
                         0.5f);
        }
        enableSweep(p.state, 0.5f, 0.3f, 0.6f, MorphLinkMode::SweepFreq);
        enableSweepLFO(p.state, 0.4f, 0.8f, Waveform::Sine);
        presets.push_back(p);
    }

    // =========================================================================
    // CHAOS (10 presets) - Chaos model showcases
    // =========================================================================
    { // Lorenz Drive: Lorenz modulates drive across tube/fuzz/tape bands
        PresetDef p; p.name = "Lorenz Drive"; p.category = "Chaos";
        p.description = "A Lorenz chaos source pushes the drive of Tube lows and Fuzz mids up and down and wanders "
                        "the Tape top across the stereo field. Familiar tones that never sit still.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 2.5f, 2000.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.1f, 0.6f, 2);
        setBandType(p.state.bandMorph[1], DistortionType::Fuzz, 3.0f, 4500.0f);
        shapeFuzz(p.state.bandMorph[1].nodes[0], 0.15f, 0, 1.0f, 0.2f);
        setBandType(p.state.bandMorph[2], DistortionType::Tape, 1.5f, 6500.0f);
        shapeTape(p.state.bandMorph[2].nodes[0], 0.0f, 0.3f, 0.6f, 1, 0.4f, 0.15f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(1), 0.4f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandPan(2), 0.25f);
        p.state.modSources.chaosModel = ChaosModel::Lorenz;
        p.state.modSources.chaosSpeedNorm = 0.25f;
        p.state.modSources.chaosCoupling = 0.4f;
        presets.push_back(p);
    }
    { // Rossler Morph: Rossler drives morph on multiple bands
        PresetDef p; p.name = "Rossler Morph"; p.category = "Chaos";
        p.description = "A Rossler chaos source moves two morphs, Soft Clip to Fuzz on the lows and Tape to Sine "
                        "Fold on the mids, and varies the drive of an Allpass Resonant top. Smooth, "
                        "never-repeating motion.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::Fuzz,
                   2.0f, 4.0f, 2000.0f, 3000.0f);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.7f, 0.3f);
        shapeFuzz(p.state.bandMorph[0].nodes[1], 0.2f, 0, 1.0f, 0.3f);
        setMorphAB(p.state.bandMorph[1], DistortionType::Tape, DistortionType::SineFold,
                   2.0f, 2.5f, 4500.0f, 5000.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.0f, 0.4f, 0.5f, 1, 0.5f, 0.2f);
        shapeSineFold(p.state.bandMorph[1].nodes[1], 3.0f, 0.2f, 0.3f);
        setBandType(p.state.bandMorph[2], DistortionType::AllpassResonant, 1.5f, 6500.0f);
        shapeAllpass(p.state.bandMorph[2].nodes[0], 1, 500.0f, 0.6f, 2.0f, 0.4f, 2);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandMorphX(0), 0.6f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandMorphX(1), 0.5f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandDrive(2), 0.2f);
        p.state.modSources.chaosModel = ChaosModel::Rossler;
        p.state.modSources.chaosSpeedNorm = 0.2f;
        p.state.modSources.chaosCoupling = 0.35f;
        presets.push_back(p);
    }
    { // Chua Circuit: Chua chaos drives chaos distortion + feedback
        PresetDef p; p.name = "Chua Circuit"; p.category = "Chaos";
        p.description = "A Chua chaos source drives Feedback Distortion lows and a Chaos mid band and swings the "
                        "low band pan, with Stochastic highs. Jumpy, circuit-like instability.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::FeedbackDist, 3.0f, 2000.0f);
        shapeFeedback(p.state.bandMorph[0].nodes[0], 0.7f, 12.0f, 0.6f, 1, 0.3f, 2);
        setBandType(p.state.bandMorph[1], DistortionType::Chaos, 3.0f, 5000.0f);
        shapeChaos(p.state.bandMorph[1].nodes[0], 2, 5.0f, 0.7f, 0.6f, 0.5f, 0.5f);
        setBandType(p.state.bandMorph[2], DistortionType::Stochastic, 2.0f, 7000.0f);
        shapeStochastic(p.state.bandMorph[2].nodes[0], 2, 0.3f, 20.0f, 0.15f, 0.2f, 1, 0.4f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(0), 0.4f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(1), 0.5f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandPan(0), 0.3f);
        p.state.modSources.chaosModel = ChaosModel::Chua;
        p.state.modSources.chaosSpeedNorm = 0.35f;
        p.state.modSources.chaosCoupling = 0.6f;
        presets.push_back(p);
    }
    { // Henon Map: fast Henon chaos drives hard clip + bitwise
        PresetDef p; p.name = "Henon Map"; p.category = "Chaos";
        p.description = "A Henon chaos source jumps the drive and pan of Hard Clip lows and the drive of Bitwise "
                        "Mangler mids, with Aliasing on top. Erratic, digital-sounding motion.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::HardClip, 3.5f, 2500.0f);
        shapeHardClip(p.state.bandMorph[0].nodes[0], 0.45f, 0.85f);
        setBandType(p.state.bandMorph[1], DistortionType::BitwiseMangler, 2.5f, 5000.0f);
        shapeBitwise(p.state.bandMorph[1].nodes[0], 3, 0.6f, 0.4f, 0.5f, 0.1f);
        setBandType(p.state.bandMorph[2], DistortionType::Aliasing, 2.0f, 7000.0f);
        shapeAliasing(p.state.bandMorph[2].nodes[0], 4.0f, 200.0f, true, 0.15f, 0.3f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandPan(0), 0.35f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandDrive(1), 0.3f);
        p.state.modSources.chaosModel = ChaosModel::Henon;
        p.state.modSources.chaosSpeedNorm = 0.5f;
        p.state.modSources.chaosCoupling = 0.5f;
        presets.push_back(p);
    }
    { // Chaotic Sweep: chaos drives sweep frequency + band distortions
        PresetDef p; p.name = "Chaotic Sweep"; p.category = "Chaos";
        p.description = "A Lorenz chaos source steers the sweep across four bands of Tube, Tape, Sine Fold and "
                        "Formant, and also drives the Sine Fold band. A sweep that wanders instead of cycling.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 150.0f;
        p.state.crossoverFreqs[1] = 1200.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        enableSweep(p.state, 0.5f, 0.4f, 0.5f);
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 2.0f, 1500.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.1f, 0.5f, 1);
        setBandType(p.state.bandMorph[1], DistortionType::Tape, 2.0f, 3500.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.0f, 0.4f, 0.5f, 1, 0.5f, 0.2f);
        setBandType(p.state.bandMorph[2], DistortionType::SineFold, 2.0f, 5500.0f);
        shapeSineFold(p.state.bandMorph[2].nodes[0], 2.5f, 0.1f, 0.3f);
        setBandType(p.state.bandMorph[3], DistortionType::Formant, 1.5f, 7000.0f);
        shapeFormant(p.state.bandMorph[3].nodes[0], 1, 2.0f, 0.5f, 0.5f, 0.5f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::kSweepFrequency, 0.4f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(2), 0.3f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandPan(1), 0.2f);
        p.state.modSources.chaosModel = ChaosModel::Lorenz;
        p.state.modSources.chaosSpeedNorm = 0.18f;
        p.state.modSources.chaosCoupling = 0.4f;
        presets.push_back(p);
    }
    { // Coupled Chaos: high coupling Lorenz on chaos→fractal morph
        PresetDef p; p.name = "Coupled Chaos"; p.category = "Chaos";
        p.description = "A strongly coupled Lorenz source swings the lows hard between Chaos and Fractal, and also "
                        "moves the Stochastic mid drive and the Allpass Resonant top pan.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        setMorphAB(p.state.bandMorph[0], DistortionType::Chaos, DistortionType::Fractal,
                   2.5f, 3.0f, 2500.0f, 3000.0f);
        shapeChaos(p.state.bandMorph[0].nodes[0], 0, 4.0f, 0.7f, 0.7f, 0.6f, 0.5f);
        shapeFractal(p.state.bandMorph[0].nodes[1], 2, 6, 0.6f, 0.5f, 0.6f, 0.2f, 1);
        setBandType(p.state.bandMorph[1], DistortionType::Stochastic, 2.0f, 4500.0f);
        shapeStochastic(p.state.bandMorph[1].nodes[0], 3, 0.3f, 25.0f, 0.2f, 0.25f, 2, 0.35f);
        setBandType(p.state.bandMorph[2], DistortionType::AllpassResonant, 1.5f, 6500.0f);
        shapeAllpass(p.state.bandMorph[2].nodes[0], 2, 400.0f, 0.7f, 2.0f, 0.4f, 2);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandMorphX(0), 0.8f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(1), 0.3f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandPan(2), 0.3f);
        p.state.modSources.chaosModel = ChaosModel::Lorenz;
        p.state.modSources.chaosSpeedNorm = 0.3f;
        p.state.modSources.chaosCoupling = 0.8f;
        presets.push_back(p);
    }
    { // Slow Chaos: glacial Rossler modulates subtle textures
        PresetDef p; p.name = "Slow Chaos"; p.category = "Chaos";
        p.description = "A very slow Rossler chaos source gently varies light Soft Clip, Tape and Granular bands. "
                        "Quiet, slowly shifting colour for pads and ambient material.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::SoftClip, 1.5f, 2500.0f);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.75f, 0.2f);
        setBandType(p.state.bandMorph[1], DistortionType::Tape, 1.2f, 4500.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.0f, 0.4f, 0.5f, 1, 0.5f, 0.25f);
        setBandType(p.state.bandMorph[2], DistortionType::Granular, 1.0f, 6500.0f);
        shapeGranular(p.state.bandMorph[2].nodes[0], 50.0f, 0.4f, 0.1f, 0.1f, 0.4f, 0.6f, 2, 1);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandMix(0), 0.25f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(1), 0.15f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandDrive(2), 0.2f);
        p.state.modSources.chaosModel = ChaosModel::Rossler;
        p.state.modSources.chaosSpeedNorm = 0.04f;
        p.state.modSources.chaosCoupling = 0.2f;
        presets.push_back(p);
    }
    { // Attractor Pan: chaos pans 4 different bands around stereo field
        PresetDef p; p.name = "Attractor Pan"; p.category = "Chaos";
        p.description = "A Lorenz chaos source pans four bands (Tape, Tube, Sine Fold, Formant) in alternating "
                        "directions, so the spectrum spreads and folds across the stereo field.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 1500.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 2.0f, 1500.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.0f, 0.5f, 0.4f, 1, 0.7f, 0.2f);
        setBandType(p.state.bandMorph[1], DistortionType::Tube, 2.0f, 3500.0f);
        shapeTube(p.state.bandMorph[1].nodes[0], 0.1f, 0.4f, 1);
        setBandType(p.state.bandMorph[2], DistortionType::SineFold, 2.0f, 5500.0f);
        shapeSineFold(p.state.bandMorph[2].nodes[0], 2.0f, 0.1f, 0.2f);
        setBandType(p.state.bandMorph[3], DistortionType::Formant, 1.5f, 7000.0f);
        shapeFormant(p.state.bandMorph[3].nodes[0], 2, 1.0f, 0.4f, 0.5f, 0.5f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandPan(0), 0.5f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandPan(1), -0.5f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandPan(2), 0.4f);
        addRouting(p.state, 3, ModSource::Chaos, ModDest::bandPan(3), -0.4f);
        p.state.modSources.chaosModel = ChaosModel::Lorenz;
        p.state.modSources.chaosSpeedNorm = 0.25f;
        p.state.modSources.chaosCoupling = 0.5f;
        presets.push_back(p);
    }
    { // Chaos Morph 4: Chua-driven 4-node morph with shaped params
        PresetDef p; p.name = "Chaos Morph 4"; p.category = "Chaos";
        p.description = "A Chua chaos source steers the lows around a four-node XY morph of Soft Clip, Fuzz, Sine "
                        "Fold and Chaos, over Tube mids and a Spectral top.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setMorph4Node(p.state.bandMorph[0],
            DistortionType::SoftClip, DistortionType::Fuzz,
            DistortionType::SineFold, DistortionType::Chaos);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.7f, 0.3f);
        shapeFuzz(p.state.bandMorph[0].nodes[1], 0.2f, 0, 1.0f, 0.3f);
        shapeSineFold(p.state.bandMorph[0].nodes[2], 4.0f, 0.2f, 0.4f);
        shapeChaos(p.state.bandMorph[0].nodes[3], 2, 5.0f, 0.6f, 0.5f, 0.5f, 0.5f);
        p.state.bandMorph[0].nodes[0].drive = 2.0f;
        p.state.bandMorph[0].nodes[1].drive = 4.0f;
        p.state.bandMorph[0].nodes[2].drive = 3.0f;
        p.state.bandMorph[0].nodes[3].drive = 2.5f;
        setBandType(p.state.bandMorph[1], DistortionType::Tube, 2.0f, 4500.0f);
        shapeTube(p.state.bandMorph[1].nodes[0], 0.1f, 0.5f, 1);
        setBandType(p.state.bandMorph[2], DistortionType::Spectral, 1.5f, 7000.0f);
        shapeSpectral(p.state.bandMorph[2].nodes[0], 1, 2, 0.5f, 0.5f, 0.1f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandMorphX(0), 0.6f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandMorphY(0), 0.6f);
        addRouting(p.state, 2, ModSource::LFO1, ModDest::bandDrive(1), 0.15f);
        p.state.modSources.chaosModel = ChaosModel::Chua;
        p.state.modSources.chaosSpeedNorm = 0.3f;
        p.state.modSources.chaosCoupling = 0.5f;
        p.state.modSources.lfo1RateNorm = 0.2f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        presets.push_back(p);
    }
    { // Fast Chaos: fast Henon on feedback + bitwise + aliasing
        PresetDef p; p.name = "Fast Chaos"; p.category = "Chaos";
        p.description = "A fast Henon chaos source rattles the drives of Feedback Distortion lows and Bitwise "
                        "Mangler mids and pans the outer bands in opposite directions, with Aliasing on top. "
                        "Harsh, jittery.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::FeedbackDist, 3.5f, 2500.0f);
        shapeFeedback(p.state.bandMorph[0].nodes[0], 0.7f, 6.0f, 0.6f, 1, 0.4f, 2);
        setBandType(p.state.bandMorph[1], DistortionType::BitwiseMangler, 3.0f, 5000.0f);
        shapeBitwise(p.state.bandMorph[1].nodes[0], 4, 0.7f, 0.5f, 0.6f, 0.05f);
        setBandType(p.state.bandMorph[2], DistortionType::Aliasing, 2.5f, 7000.0f);
        shapeAliasing(p.state.bandMorph[2].nodes[0], 5.0f, 300.0f, false, 0.25f, 0.35f);
        addRouting(p.state, 0, ModSource::Chaos, ModDest::bandDrive(0), 0.6f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(1), 0.5f);
        addRouting(p.state, 2, ModSource::Chaos, ModDest::bandPan(0), 0.4f);
        addRouting(p.state, 3, ModSource::Chaos, ModDest::bandPan(2), -0.4f);
        p.state.modSources.chaosModel = ChaosModel::Henon;
        p.state.modSources.chaosSpeedNorm = 0.75f;
        p.state.modSources.chaosCoupling = 0.7f;
        presets.push_back(p);
    }

    // =========================================================================
    // DYNAMIC (10 presets) - Envelope/transient/pitch follower
    // =========================================================================
    { // Touch Sensitive: env controls drive on tube/tape/soft clip per band
        PresetDef p; p.name = "Touch Sensitive"; p.category = "Dynamic";
        p.description = "Tube, Tape and Soft Clip bands whose drive follows your playing: the envelope follower "
                        "pushes all three harder as the input gets louder, the Tube lows most. For guitar, keys and "
                        "bass.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 2.0f, 2000.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.1f, 0.5f, 1);
        setBandType(p.state.bandMorph[1], DistortionType::Tape, 2.0f, 4500.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.0f, 0.4f, 0.5f, 1, 0.5f, 0.2f);
        setBandType(p.state.bandMorph[2], DistortionType::SoftClip, 1.5f, 6500.0f);
        shapeSoftClip(p.state.bandMorph[2].nodes[0], 0.7f, 0.3f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.6f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandDrive(1), 0.4f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandDrive(2), 0.3f);
        p.state.modSources.envAttackNorm = 0.04f;
        p.state.modSources.envReleaseNorm = 0.2f;
        p.state.modSources.envSensitivity = 0.75f;
        presets.push_back(p);
    }
    { // Transient Punch: transient detector drives hard clip + temporal
        PresetDef p; p.name = "Transient Punch"; p.category = "Dynamic";
        p.description = "The transient detector slams the drive of Hard Clip lows and Temporal mids on every "
                        "attack, and the input level pushes the Fuzz top. Adds punch to drums, plucked bass and "
                        "percussive synths.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::HardClip, 3.0f, 1500.0f);
        shapeHardClip(p.state.bandMorph[0].nodes[0], 0.45f, 0.85f);
        setBandType(p.state.bandMorph[1], DistortionType::Temporal, 3.0f, 5000.0f);
        shapeTemporal(p.state.bandMorph[1].nodes[0], 2, 0.7f, 0.6f, 3.0f, 60.0f, 0.7f, 1, 0.15f);
        setBandType(p.state.bandMorph[2], DistortionType::Fuzz, 2.5f, 7000.0f);
        shapeFuzz(p.state.bandMorph[2].nodes[0], 0.1f, 0, 0.0f, 0.3f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandDrive(0), 0.7f);
        addRouting(p.state, 1, ModSource::Transient, ModDest::bandDrive(1), 0.5f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandDrive(2), 0.3f);
        p.state.modSources.transSensitivity = 0.85f;
        p.state.modSources.transAttackNorm = 0.08f;
        p.state.modSources.transDecayNorm = 0.25f;
        p.state.modSources.envSensitivity = 0.6f;
        presets.push_back(p);
    }
    { // Pitch Tracker: pitch controls drive + sweep across bands
        PresetDef p; p.name = "Pitch Tracker"; p.category = "Dynamic";
        p.description = "The input pitch drives the Tube lows and Allpass Resonant mids and moves the sweep, with "
                        "Formant on top, so higher notes distort more. For monophonic leads, bass and vocals.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        enableSweep(p.state, 0.5f, 0.3f, 0.4f);
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 2.0f, 2500.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.1f, 0.5f, 1);
        setBandType(p.state.bandMorph[1], DistortionType::AllpassResonant, 2.0f, 5000.0f);
        shapeAllpass(p.state.bandMorph[1].nodes[0], 1, 400.0f, 0.6f, 1.5f, 0.5f, 2, true, 0.3f);
        setBandType(p.state.bandMorph[2], DistortionType::Formant, 1.5f, 6500.0f);
        shapeFormant(p.state.bandMorph[2].nodes[0], 1, 0.0f, 0.5f, 0.6f, 0.5f, 1, 0.5f, 0.5f);
        addRouting(p.state, 0, ModSource::PitchFollower, ModDest::bandDrive(0), 0.4f);
        addRouting(p.state, 1, ModSource::PitchFollower, ModDest::kSweepFrequency, 0.3f);
        addRouting(p.state, 2, ModSource::PitchFollower, ModDest::bandDrive(1), 0.3f);
        p.state.modSources.pitchMinNorm = 0.1f;
        p.state.modSources.pitchMaxNorm = 0.5f;
        p.state.modSources.pitchConfidence = 0.65f;
        presets.push_back(p);
    }
    { // Dynamic Mix: env ducks fuzz, transient boosts hard clip
        PresetDef p; p.name = "Dynamic Mix"; p.category = "Dynamic";
        p.description = "The Fuzz lows back off as you play louder (the envelope lowers their mix), transients "
                        "push the Hard Clip mids, and the input level drives the Ring Saturation top.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Fuzz, 4.0f, 2500.0f);
        shapeFuzz(p.state.bandMorph[0].nodes[0], 0.2f, 0, 1.0f, 0.3f);
        setBandType(p.state.bandMorph[1], DistortionType::HardClip, 3.0f, 5000.0f);
        shapeHardClip(p.state.bandMorph[1].nodes[0], 0.5f, 0.85f);
        setBandType(p.state.bandMorph[2], DistortionType::RingSaturation, 2.0f, 7000.0f);
        shapeRingSat(p.state.bandMorph[2].nodes[0], 0.5f, 2, 0.5f, 1, 0.0f, 1);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandMix(0), -0.4f);
        addRouting(p.state, 1, ModSource::Transient, ModDest::bandDrive(1), 0.5f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandDrive(2), 0.3f);
        p.state.modSources.envSensitivity = 0.8f;
        p.state.modSources.transSensitivity = 0.7f;
        presets.push_back(p);
    }
    { // Envelope Morph: env morphs soft→hard per band differently
        PresetDef p; p.name = "Envelope Morph"; p.category = "Dynamic";
        p.description = "Louder input moves the morphs toward their harder side, Soft Clip to Hard Clip on the "
                        "lows and Tape to Fuzz on the mids, with Tube on top; quiet passages stay in between.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        setMorphAB(p.state.bandMorph[0], DistortionType::SoftClip, DistortionType::HardClip,
                   1.5f, 4.0f, 2000.0f, 2500.0f);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.8f, 0.2f);
        shapeHardClip(p.state.bandMorph[0].nodes[1], 0.45f, 0.85f);
        setMorphAB(p.state.bandMorph[1], DistortionType::Tape, DistortionType::Fuzz,
                   2.0f, 4.0f, 4000.0f, 5000.0f);
        shapeTape(p.state.bandMorph[1].nodes[0], 0.0f, 0.4f, 0.5f, 1, 0.5f, 0.2f);
        shapeFuzz(p.state.bandMorph[1].nodes[1], 0.15f, 0, 1.0f, 0.3f);
        setBandType(p.state.bandMorph[2], DistortionType::Tube, 1.5f, 6500.0f);
        shapeTube(p.state.bandMorph[2].nodes[0], 0.0f, 0.3f, 1);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandMorphX(0), 0.7f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandMorphX(1), 0.5f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandDrive(2), 0.3f);
        p.state.modSources.envAttackNorm = 0.02f;
        p.state.modSources.envReleaseNorm = 0.35f;
        p.state.modSources.envSensitivity = 0.8f;
        presets.push_back(p);
    }
    { // Transient Gate: transient controls mix on different distortion types
        PresetDef p; p.name = "Transient Gate"; p.category = "Dynamic";
        p.description = "Transients gate the distortion in: the Soft Clip lows sit at 30 % mix and the Chaos mids "
                        "at 50 %, and each hit opens both and pushes the Spectral drive on top, easing back over "
                        "about 130 ms. For drums and percussive parts.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        // Drive 4.5 (was 2.5 at 100 % mix) keeps the lows' bite between hits
        setBandType(p.state.bandMorph[0], DistortionType::SoftClip, 4.5f, 2000.0f);
        shapeSoftClip(p.state.bandMorph[0].nodes[0], 0.7f, 0.3f);
        setBandMix(p.state.bandMorph[0], 0.3f);  // each hit opens it (routing 0)
        setBandType(p.state.bandMorph[1], DistortionType::Chaos, 2.0f, 5000.0f);
        shapeChaos(p.state.bandMorph[1].nodes[0], 1, 3.0f, 0.5f, 0.4f, 0.5f, 0.4f, 0.2f);
        setBandMix(p.state.bandMorph[1], 0.5f);  // each hit opens it (routing 1)
        setBandType(p.state.bandMorph[2], DistortionType::Spectral, 1.5f, 7000.0f);
        shapeSpectral(p.state.bandMorph[2].nodes[0], 1, 2, 0.5f, 0.5f, 0.1f);
        addRouting(p.state, 0, ModSource::Transient, ModDest::bandMix(0), 0.7f);
        addRouting(p.state, 1, ModSource::Transient, ModDest::bandMix(1), 0.5f);
        addRouting(p.state, 2, ModSource::Transient, ModDest::bandDrive(2), 0.3f);
        p.state.modSources.transSensitivity = 0.65f;
        p.state.modSources.transAttackNorm = 0.03f;
        p.state.modSources.transDecayNorm = 0.6f;  // 128 ms: opens on every kick and snare at 512-sample blocks
        presets.push_back(p);
    }
    { // Pitch Drive: pitch tracks drive + sweep frequency
        PresetDef p; p.name = "Pitch Drive"; p.category = "Dynamic";
        p.description = "The input pitch drives the octave Fuzz lows and Sine Fold mids and moves the sweep, with "
                        "a Formant top, so higher notes bite harder. For monophonic leads and bass.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        enableSweep(p.state, 0.5f, 0.3f, 0.4f);
        setBandType(p.state.bandMorph[0], DistortionType::Fuzz, 3.0f, 2500.0f);
        shapeFuzz(p.state.bandMorph[0].nodes[0], 0.15f, 0, 1.0f, 0.4f);
        setBandType(p.state.bandMorph[1], DistortionType::SineFold, 2.5f, 5000.0f);
        shapeSineFold(p.state.bandMorph[1].nodes[0], 3.0f, 0.2f, 0.3f);
        setBandType(p.state.bandMorph[2], DistortionType::Formant, 2.0f, 7000.0f);
        shapeFormant(p.state.bandMorph[2].nodes[0], 0, 0.0f, 0.6f, 0.7f, 0.4f, 1, 0.5f, 0.5f);
        addRouting(p.state, 0, ModSource::PitchFollower, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::PitchFollower, ModDest::kSweepFrequency, 0.3f);
        addRouting(p.state, 2, ModSource::PitchFollower, ModDest::bandDrive(1), 0.3f);
        p.state.modSources.pitchConfidence = 0.7f;
        p.state.modSources.pitchMinNorm = 0.1f;
        p.state.modSources.pitchMaxNorm = 0.5f;
        presets.push_back(p);
    }
    { // Velocity Response: envelope controls drive across tape/tube/soft clip
        PresetDef p; p.name = "Velocity Response"; p.category = "Dynamic";
        p.description = "The envelope follower drives the Tape lows, Tube mids and Soft Clip top harder as the "
                        "input gets louder. Soft notes stay lighter, hard notes break up.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 2.0f, 2000.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.05f, 0.5f, 0.5f, 1, 0.6f, 0.25f);
        setBandType(p.state.bandMorph[1], DistortionType::Tube, 2.0f, 4500.0f);
        shapeTube(p.state.bandMorph[1].nodes[0], 0.1f, 0.5f, 2);
        setBandType(p.state.bandMorph[2], DistortionType::SoftClip, 1.5f, 6500.0f);
        shapeSoftClip(p.state.bandMorph[2].nodes[0], 0.7f, 0.3f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.5f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandDrive(1), 0.4f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandDrive(2), 0.3f);
        p.state.modSources.envSensitivity = 0.65f;
        p.state.modSources.envAttackNorm = 0.03f;
        p.state.modSources.envReleaseNorm = 0.2f;
        presets.push_back(p);
    }
    { // Multi Dynamic: all dynamic sources controlling different bands
        PresetDef p; p.name = "Multi Dynamic"; p.category = "Dynamic";
        p.description = "Each band follows a detector: the envelope drives the Tube lows and the Formant top, "
                        "transients drive the octave Fuzz, and the input pitch drives the Sine Fold band.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 150.0f;
        p.state.crossoverFreqs[1] = 1200.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 2.0f, 1500.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.1f, 0.5f, 1);
        setBandType(p.state.bandMorph[1], DistortionType::Fuzz, 3.0f, 3500.0f);
        shapeFuzz(p.state.bandMorph[1].nodes[0], 0.15f, 0, 0.0f, 0.9f);
        setBandType(p.state.bandMorph[2], DistortionType::SineFold, 2.5f, 5500.0f);
        shapeSineFold(p.state.bandMorph[2].nodes[0], 3.0f, 0.1f, 0.3f);
        setBandType(p.state.bandMorph[3], DistortionType::Formant, 1.5f, 7000.0f);
        shapeFormant(p.state.bandMorph[3].nodes[0], 2, 2.0f, 0.5f, 0.5f, 0.5f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandDrive(0), 0.4f);
        addRouting(p.state, 1, ModSource::Transient, ModDest::bandDrive(1), 0.5f);
        addRouting(p.state, 2, ModSource::PitchFollower, ModDest::bandDrive(2), 0.3f);
        addRouting(p.state, 3, ModSource::EnvFollower, ModDest::bandDrive(3), 0.3f);
        p.state.modSources.envSensitivity = 0.7f;
        p.state.modSources.transSensitivity = 0.75f;
        p.state.modSources.pitchConfidence = 0.6f;
        presets.push_back(p);
    }
    { // Sidechain Pump: env ducks mix on different types per band
        PresetDef p; p.name = "Sidechain Pump"; p.category = "Dynamic";
        p.description = "Louder input pulls back the Tape lows and Fuzz mids mix and the Hard Clip top drive, so "
                        "the distortion pumps against the dynamics. Try it on drum loops or a full mix.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 3.0f, 2000.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.0f, 0.5f, 0.4f, 1, 0.6f, 0.2f);
        setBandType(p.state.bandMorph[1], DistortionType::Fuzz, 3.5f, 5000.0f);
        shapeFuzz(p.state.bandMorph[1].nodes[0], 0.15f, 0, 1.0f, 0.3f);
        setBandType(p.state.bandMorph[2], DistortionType::HardClip, 2.5f, 6500.0f);
        shapeHardClip(p.state.bandMorph[2].nodes[0], 0.5f, 0.85f);
        addRouting(p.state, 0, ModSource::EnvFollower, ModDest::bandMix(0), -0.6f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandMix(1), -0.5f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandDrive(2), -0.3f);
        p.state.modSources.envAttackNorm = 0.01f;
        p.state.modSources.envReleaseNorm = 0.25f;
        p.state.modSources.envSensitivity = 0.9f;
        presets.push_back(p);
    }

    // =========================================================================
    // LO-FI (10 presets) - Digital degradation
    // =========================================================================
    { // 8-Bit Crunch: bitcrush lows, quantize mids, aliasing highs
        PresetDef p; p.name = "8-Bit Crunch"; p.category = "Lo-Fi";
        p.description = "8-bit Bitcrush on the lows, Quantize on the mids and light Aliasing on top, with slow "
                        "random drift on the crush drive. Retro console grit for drums, synths and loops.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Bitcrush, 1.5f, 2000.0f);
        shapeBitcrush(p.state.bandMorph[0].nodes[0], 8.0f, 0.1f, 0, 0.05f);
        setBandType(p.state.bandMorph[1], DistortionType::Quantize, 1.5f, 5000.0f);
        shapeQuantize(p.state.bandMorph[1].nodes[0], 0.35f, 0.15f, 0.1f, 0.1f);
        setBandType(p.state.bandMorph[2], DistortionType::Aliasing, 1.5f, 7000.0f);
        shapeAliasing(p.state.bandMorph[2].nodes[0], 3.0f, 100.0f, true, 0.1f, 0.2f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.15f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandDrive(1), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.3f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        p.state.modSources.envSensitivity = 0.6f;
        presets.push_back(p);
    }
    { // Sample Rate Crush: sample reduce + tape warmth + stochastic shimmer
        PresetDef p; p.name = "Sample Rate Crush"; p.category = "Lo-Fi";
        p.description = "Sample Reduce at 8x on the 250 Hz to 4 kHz band, with Tape lows and a Stochastic top "
                        "drifting in the stereo field. Sample-rate crunch that leaves the low end intact.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 1.5f, 1800.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.0f, 0.5f, 0.3f, 1, 0.7f, 0.3f);
        setBandType(p.state.bandMorph[1], DistortionType::SampleReduce, 1.5f, 4500.0f);
        shapeSampleReduce(p.state.bandMorph[1].nodes[0], 8.0f, 0.2f, 1, 0.15f);
        setBandType(p.state.bandMorph[2], DistortionType::Stochastic, 1.0f, 6500.0f);
        shapeStochastic(p.state.bandMorph[2].nodes[0], 1, 0.12f, 8.0f, 0.06f, 0.1f, 1, 0.6f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(1), 0.2f);
        addRouting(p.state, 1, ModSource::Random, ModDest::bandPan(2), 0.15f);
        p.state.modSources.lfo1RateNorm = 0.2f;
        p.state.modSources.lfo1Shape = Waveform::Triangle;
        p.state.modSources.randomRateNorm = 0.1f;
        p.state.modSources.randomSmoothness = 0.7f;
        presets.push_back(p);
    }
    { // Quantize Dirt: quantize + bitcrush morph, tube warmth underneath
        PresetDef p; p.name = "Quantize Dirt"; p.category = "Lo-Fi";
        p.description = "A slow LFO morphs the mids between Quantize and 10-bit Bitcrush, over warm Tube lows and "
                        "a Sample Reduce top stepped by a sample-and-hold source.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 1.5f, 1800.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.0f, 0.4f, 1);
        setMorphAB(p.state.bandMorph[1], DistortionType::Quantize, DistortionType::Bitcrush,
                   2.0f, 1.5f, 4000.0f, 4500.0f);
        shapeQuantize(p.state.bandMorph[1].nodes[0], 0.3f, 0.2f, 0.1f, 0.2f);
        shapeBitcrush(p.state.bandMorph[1].nodes[1], 10.0f, 0.15f, 1, 0.1f);
        setBandType(p.state.bandMorph[2], DistortionType::SampleReduce, 1.5f, 6500.0f);
        shapeSampleReduce(p.state.bandMorph[2].nodes[0], 6.0f, 0.15f, 0, 0.1f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(1), 0.4f);
        addRouting(p.state, 1, ModSource::SampleHold, ModDest::bandDrive(2), 0.25f);
        p.state.modSources.lfo1RateNorm = 0.15f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.shRateNorm = 0.3f;
        presets.push_back(p);
    }
    { // Aliasing Harsh: aliasing mids, bitwise lows, chaos highs
        PresetDef p; p.name = "Aliasing Harsh"; p.category = "Lo-Fi";
        p.description = "Aliasing on the 300 Hz to 4 kHz band, stepped by a sample-and-hold LFO, with Bitwise "
                        "Mangler lows and a Chaos top. Harsh, inharmonic digital tones.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::BitwiseMangler, 2.0f, 2000.0f);
        shapeBitwise(p.state.bandMorph[0].nodes[0], 1, 0.5f, 0.3f, 0.5f, 0.1f);
        setBandType(p.state.bandMorph[1], DistortionType::Aliasing, 2.5f, 5500.0f);
        shapeAliasing(p.state.bandMorph[1].nodes[0], 6.0f, 300.0f, false, 0.2f, 0.35f);
        setBandType(p.state.bandMorph[2], DistortionType::Chaos, 2.0f, 7000.0f);
        shapeChaos(p.state.bandMorph[2].nodes[0], 3, 8.0f, 0.5f, 0.3f, 0.5f, 0.4f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(1), 0.3f);
        addRouting(p.state, 1, ModSource::Chaos, ModDest::bandDrive(2), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.5f;
        p.state.modSources.lfo1Shape = Waveform::SampleAndHold;
        p.state.modSources.chaosSpeedNorm = 0.3f;
        p.state.bands[0].pan = -0.15f;
        p.state.bands[2].pan = 0.15f;
        presets.push_back(p);
    }
    { // Bit Mangler: bitwise on mids with morph, tape warmth, fractal shimmer
        PresetDef p; p.name = "Bit Mangler"; p.category = "Lo-Fi";
        p.description = "A saw LFO morphs the mids from Bitwise Mangler to Quantize and a random source varies "
                        "their drive, over Tape lows and a light Fractal top.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 1.5f, 2000.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.0f, 0.4f, 0.4f, 1, 0.7f, 0.25f);
        setMorphAB(p.state.bandMorph[1], DistortionType::BitwiseMangler, DistortionType::Quantize,
                   2.5f, 2.0f, 5000.0f, 4500.0f);
        shapeBitwise(p.state.bandMorph[1].nodes[0], 3, 0.7f, 0.5f, 0.6f, 0.05f);
        shapeQuantize(p.state.bandMorph[1].nodes[1], 0.3f, 0.1f, 0.1f, 0.15f);
        setBandType(p.state.bandMorph[2], DistortionType::Fractal, 1.0f, 6500.0f);
        shapeFractal(p.state.bandMorph[2].nodes[0], 0, 3, 0.4f, 0.5f, 0.4f, 0.05f, 0, 0.4f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(1), 0.5f);
        addRouting(p.state, 1, ModSource::Random, ModDest::bandDrive(1), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.35f;
        p.state.modSources.lfo1Shape = Waveform::Saw;
        p.state.modSources.randomRateNorm = 0.25f;
        p.state.modSources.randomSmoothness = 0.4f;
        presets.push_back(p);
    }
    { // 4-Bit Retro: extreme bitcrush + sample reduce + tape saturation
        PresetDef p; p.name = "4-Bit Retro"; p.category = "Lo-Fi";
        p.description = "4-bit Bitcrush (its lowest setting) on the mids and Sample Reduce at 12x on the highs, "
                        "with fluttering Tape lows to keep it warm. Try drums and chiptune leads.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tape, 2.0f, 1500.0f);
        shapeTape(p.state.bandMorph[0].nodes[0], 0.1f, 0.6f, 0.3f, 1, 0.8f, 0.35f);
        setBandType(p.state.bandMorph[1], DistortionType::Bitcrush, 1.5f, 4000.0f);
        shapeBitcrush(p.state.bandMorph[1].nodes[0], 4.0f, 0.2f, 1, 0.15f);
        setBandType(p.state.bandMorph[2], DistortionType::SampleReduce, 1.5f, 6000.0f);
        shapeSampleReduce(p.state.bandMorph[2].nodes[0], 12.0f, 0.2f, 1, 0.1f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(0), 0.1f);
        addRouting(p.state, 1, ModSource::EnvFollower, ModDest::bandDrive(1), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.08f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        p.state.modSources.envSensitivity = 0.5f;
        presets.push_back(p);
    }
    { // Digital Decay: bitcrush→sample reduce morph + stochastic drift
        PresetDef p; p.name = "Digital Decay"; p.category = "Lo-Fi";
        p.description = "A slow LFO morphs the mids between 7-bit Bitcrush and Sample Reduce, over light Tube lows "
                        "and a drifting Stochastic top. Degraded, slowly shifting digital texture.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 1.0f, 2000.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.0f, 0.3f, 0);
        setMorphAB(p.state.bandMorph[1], DistortionType::Bitcrush, DistortionType::SampleReduce,
                   1.5f, 1.5f, 4000.0f, 4500.0f);
        shapeBitcrush(p.state.bandMorph[1].nodes[0], 7.0f, 0.15f, 0, 0.1f);
        shapeSampleReduce(p.state.bandMorph[1].nodes[1], 10.0f, 0.2f, 1, 0.15f);
        setBandType(p.state.bandMorph[2], DistortionType::Stochastic, 1.0f, 6500.0f);
        shapeStochastic(p.state.bandMorph[2].nodes[0], 1, 0.1f, 5.0f, 0.05f, 0.1f, 1, 0.7f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(1), 0.4f);
        addRouting(p.state, 1, ModSource::Random, ModDest::bandDrive(2), 0.15f);
        p.state.modSources.lfo1RateNorm = 0.12f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.randomRateNorm = 0.08f;
        p.state.modSources.randomSmoothness = 0.8f;
        presets.push_back(p);
    }
    { // Multi Band Lo-Fi: different lo-fi type per band with varied params
        PresetDef p; p.name = "Multi Band Lo-Fi"; p.category = "Lo-Fi";
        p.description = "A different lo-fi type per band: 12-bit and 8-bit Bitcrush, Quantize and Sample Reduce "
                        "from low to high, with slow LFOs and the input level moving the drives.";
        p.state = makeInitState(4);
        p.state.crossoverFreqs[0] = 200.0f;
        p.state.crossoverFreqs[1] = 1000.0f;
        p.state.crossoverFreqs[2] = 5000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Bitcrush, 1.0f, 1500.0f);
        shapeBitcrush(p.state.bandMorph[0].nodes[0], 12.0f, 0.1f, 0, 0.05f);
        setBandType(p.state.bandMorph[1], DistortionType::Bitcrush, 1.5f, 3000.0f);
        shapeBitcrush(p.state.bandMorph[1].nodes[0], 8.0f, 0.2f, 1, 0.1f);
        setBandType(p.state.bandMorph[2], DistortionType::Quantize, 1.5f, 5500.0f);
        shapeQuantize(p.state.bandMorph[2].nodes[0], 0.3f, 0.15f, 0.1f, 0.15f);
        setBandType(p.state.bandMorph[3], DistortionType::SampleReduce, 1.5f, 7000.0f);
        shapeSampleReduce(p.state.bandMorph[3].nodes[0], 8.0f, 0.15f, 0, 0.1f);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandDrive(1), 0.2f);
        addRouting(p.state, 1, ModSource::LFO2, ModDest::bandDrive(3), 0.15f);
        addRouting(p.state, 2, ModSource::EnvFollower, ModDest::bandDrive(2), 0.2f);
        p.state.modSources.lfo1RateNorm = 0.2f;
        p.state.modSources.lfo1Shape = Waveform::SmoothRandom;
        p.state.modSources.lfo2RateNorm = 0.15f;
        p.state.modSources.lfo2Shape = Waveform::Triangle;
        p.state.modSources.envSensitivity = 0.55f;
        p.state.bands[1].pan = -0.1f;
        p.state.bands[3].pan = 0.1f;
        presets.push_back(p);
    }
    { // Glitch Box: aliasing→bitwise morph + S&H modulation
        PresetDef p; p.name = "Glitch Box"; p.category = "Lo-Fi";
        p.description = "A sample-and-hold source jumps the mids between Aliasing and Bitwise Mangler and steps "
                        "the Sample Reduce top drive, over Feedback Distortion lows. Stuttering glitch for beats.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 300.0f;
        p.state.crossoverFreqs[1] = 4000.0f;
        setBandType(p.state.bandMorph[0], DistortionType::FeedbackDist, 2.0f, 2000.0f);
        shapeFeedback(p.state.bandMorph[0].nodes[0], 0.5f, 8.0f, 0.5f, 0, 0.3f, 1);
        setMorphAB(p.state.bandMorph[1], DistortionType::Aliasing, DistortionType::BitwiseMangler,
                   2.5f, 2.5f, 5000.0f, 5000.0f);
        shapeAliasing(p.state.bandMorph[1].nodes[0], 6.0f, 400.0f, false, 0.2f, 0.3f);
        shapeBitwise(p.state.bandMorph[1].nodes[1], 4, 0.7f, 0.5f, 0.6f, 0.05f);
        setBandType(p.state.bandMorph[2], DistortionType::SampleReduce, 2.0f, 7000.0f);
        shapeSampleReduce(p.state.bandMorph[2].nodes[0], 10.0f, 0.3f, 1, 0.05f);
        addRouting(p.state, 0, ModSource::SampleHold, ModDest::bandMorphX(1), 0.7f);
        addRouting(p.state, 1, ModSource::SampleHold, ModDest::bandDrive(2), 0.4f);
        addRouting(p.state, 2, ModSource::LFO1, ModDest::bandPan(1), 0.25f);
        p.state.modSources.shRateNorm = 0.4f;
        p.state.modSources.shSlewNorm = 0.15f;
        p.state.modSources.lfo1RateNorm = 0.6f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        presets.push_back(p);
    }
    { // Warm Lo-Fi: bitcrush→tape morph + granular texture
        PresetDef p; p.name = "Warm Lo-Fi"; p.category = "Lo-Fi";
        p.description = "The mids sit between 12-bit Bitcrush and Tape, swaying on a slow LFO, over Tube lows and "
                        "a quiet Granular top. Soft, dusty lo-fi for keys, pads and beats.";
        p.state = makeInitState(3);
        p.state.crossoverFreqs[0] = 250.0f;
        p.state.crossoverFreqs[1] = 3500.0f;
        setBandType(p.state.bandMorph[0], DistortionType::Tube, 1.5f, 1800.0f);
        shapeTube(p.state.bandMorph[0].nodes[0], 0.0f, 0.5f, 1);
        setMorphAB(p.state.bandMorph[1], DistortionType::Bitcrush, DistortionType::Tape,
                   1.0f, 1.5f, 3500.0f, 4000.0f);
        shapeBitcrush(p.state.bandMorph[1].nodes[0], 12.0f, 0.1f, 0, 0.05f);
        shapeTape(p.state.bandMorph[1].nodes[1], 0.0f, 0.4f, 0.5f, 1, 0.5f, 0.25f);
        p.state.bandMorph[1].morphX = 0.4f;
        setBandType(p.state.bandMorph[2], DistortionType::Granular, 1.0f, 6000.0f);
        shapeGranular(p.state.bandMorph[2].nodes[0], 40.0f, 0.4f, 0.1f, 0.1f, 0.3f, 0.6f, 2, 1);
        addRouting(p.state, 0, ModSource::LFO1, ModDest::bandMorphX(1), 0.25f);
        addRouting(p.state, 1, ModSource::Random, ModDest::bandDrive(2), 0.1f);
        p.state.modSources.lfo1RateNorm = 0.08f;
        p.state.modSources.lfo1Shape = Waveform::Sine;
        p.state.modSources.randomRateNorm = 0.06f;
        p.state.modSources.randomSmoothness = 0.85f;
        p.state.bands[2].gainDb = -2.0f;
        presets.push_back(p);
    }

    return presets;
}

// ==============================================================================
// Output trims (D-144, fix plan T3.01)
// ==============================================================================
// Re-derived for the drive re-stage (user ruling 2026-11-10: the level-dependent types run
// 18 dB hotter into their shapers, so drive 1 distorts a -18 dBFS input). On 2 s of pink noise
// peaking at -12 dBFS, 44.1 kHz (plugins/disrumpo/tests/integration/preset/
// factory_preset_validation_test.cpp, [D-144]), each trim brings the preset's RMS back to its
// RMS before the re-stage (18c5b0d2) as far as a peak of -0.3 dBFS (after the output safety
// clipper) and a DC of at most 0.009 allow, rounded down to 0.1 dB; trims may raise or lower.
// The Init presets keep 0 dB.

struct OutputTrim {
    const char* preset;  // "Category/File_Stem"
    float db;
};

constexpr OutputTrim kOutputTrims[] = {
    {.preset = "Bass/Dirty_Sub", .db = 6.9f},
    {.preset = "Bass/Multi_Band_Bass", .db = 6.9f},
    {.preset = "Bass/Punchy_Low_End", .db = 7.4f},
    {.preset = "Bass/Ring_Bass", .db = -2.1f},
    {.preset = "Bass/Serge_Bass", .db = 9.6f},
    {.preset = "Bass/Soft_Bass_Drive", .db = 6.0f},
    {.preset = "Bass/Subharmonic_Growl", .db = 10.0f},
    {.preset = "Bass/Tape_Warmth", .db = 8.7f},
    {.preset = "Bass/Warm_Bass", .db = 7.6f},
    {.preset = "Bass/Wavefold_Bass", .db = 7.2f},
    {.preset = "Chaos/Attractor_Pan", .db = 8.5f},
    {.preset = "Chaos/Chaos_Morph_4", .db = 4.7f},
    {.preset = "Chaos/Chaotic_Sweep", .db = 9.7f},
    {.preset = "Chaos/Chua_Circuit", .db = 6.8f},
    {.preset = "Chaos/Coupled_Chaos", .db = 2.8f},
    {.preset = "Chaos/Fast_Chaos", .db = 8.0f},
    {.preset = "Chaos/Henon_Map", .db = 10.3f},
    {.preset = "Chaos/Lorenz_Drive", .db = 5.9f},
    {.preset = "Chaos/Rossler_Morph", .db = 4.0f},
    {.preset = "Chaos/Slow_Chaos", .db = 4.6f},
    {.preset = "Drums/Aliased_Drums", .db = 6.3f},
    {.preset = "Drums/Crushed_Beats", .db = -0.1f},
    {.preset = "Drums/Dirty_Groove", .db = 8.5f},
    {.preset = "Drums/Glitch_Drum", .db = 6.8f},
    {.preset = "Drums/Kick_Saturate", .db = 7.0f},
    {.preset = "Drums/Punchy_Clip", .db = 9.5f},
    {.preset = "Drums/Rectified_Drums", .db = -3.6f},
    {.preset = "Drums/Snare_Crack", .db = 8.5f},
    {.preset = "Drums/Temporal_Snap", .db = 7.7f},
    {.preset = "Drums/Transient_Shape", .db = 0.6f},
    {.preset = "Dynamic/Dynamic_Mix", .db = 8.5f},
    {.preset = "Dynamic/Envelope_Morph", .db = 8.8f},
    {.preset = "Dynamic/Multi_Dynamic", .db = 12.2f},
    {.preset = "Dynamic/Pitch_Drive", .db = 0.5f},
    {.preset = "Dynamic/Pitch_Tracker", .db = 8.2f},
    {.preset = "Dynamic/Sidechain_Pump", .db = 11.7f},
    {.preset = "Dynamic/Touch_Sensitive", .db = 7.7f},
    {.preset = "Dynamic/Transient_Gate", .db = 3.6f},
    {.preset = "Dynamic/Transient_Punch", .db = 8.6f},
    {.preset = "Dynamic/Velocity_Response", .db = 8.5f},
    {.preset = "Experimental/Alien_Voice", .db = 10.2f},
    {.preset = "Experimental/All_Types_Morph", .db = 9.9f},
    {.preset = "Experimental/Broken_Radio", .db = 0.1f},
    {.preset = "Experimental/Chaos_Engine", .db = 9.5f},
    {.preset = "Experimental/Feedback_Loop", .db = 14.0f},
    {.preset = "Experimental/Formant_Shift", .db = 3.8f},
    {.preset = "Experimental/Formant_Vowels", .db = 6.8f},
    {.preset = "Experimental/Fractal_Noise", .db = 1.6f},
    {.preset = "Experimental/Granular_Crush", .db = -2.4f},
    {.preset = "Experimental/Quantize_Glitch", .db = -0.1f},
    {.preset = "Experimental/Ring_Chaos", .db = -4.1f},
    {.preset = "Experimental/Serge_Madness", .db = 13.1f},
    {.preset = "Experimental/Spectral_Scatter", .db = -5.8f},
    {.preset = "Experimental/Stochastic_Burst", .db = -9.1f},
    {.preset = "Experimental/Temporal_Flux", .db = 10.4f},
    {.preset = "Leads/Asymmetric_Bite", .db = 4.3f},
    {.preset = "Leads/Chaos_Lead", .db = 0.3f},
    {.preset = "Leads/Feedback_Scream", .db = 9.3f},
    {.preset = "Leads/Fractal_Lead", .db = 14.9f},
    {.preset = "Leads/Fuzz_Face", .db = 0.6f},
    {.preset = "Leads/Modulated_Edge", .db = 7.8f},
    {.preset = "Leads/Ring_Mod_Lead", .db = -3.8f},
    {.preset = "Leads/Screaming_Lead", .db = -0.1f},
    {.preset = "Leads/Temporal_Edge", .db = 8.8f},
    {.preset = "Leads/Wavefold_Lead", .db = 12.0f},
    {.preset = "Lo-Fi/4-Bit_Retro", .db = 7.5f},
    {.preset = "Lo-Fi/8-Bit_Crunch", .db = -0.1f},
    {.preset = "Lo-Fi/Aliasing_Harsh", .db = 1.1f},
    {.preset = "Lo-Fi/Bit_Mangler", .db = 10.4f},
    {.preset = "Lo-Fi/Digital_Decay", .db = 0.3f},
    {.preset = "Lo-Fi/Glitch_Box", .db = 6.1f},
    {.preset = "Lo-Fi/Multi_Band_Lo-Fi", .db = -0.1f},
    {.preset = "Lo-Fi/Quantize_Dirt", .db = 0.8f},
    {.preset = "Lo-Fi/Sample_Rate_Crush", .db = 12.6f},
    {.preset = "Lo-Fi/Warm_Lo-Fi", .db = 6.0f},
    {.preset = "Morph/Chaos_to_Order", .db = -2.5f},
    {.preset = "Morph/Digital_Organic", .db = -9.8f},
    {.preset = "Morph/Feedback_Explorer", .db = 4.0f},
    {.preset = "Morph/Formant_Shift", .db = 6.5f},
    {.preset = "Morph/Four_Corners", .db = 10.3f},
    {.preset = "Morph/Multi_Band_Morph", .db = 8.3f},
    {.preset = "Morph/Radial_Blend", .db = -1.0f},
    {.preset = "Morph/Rectify_Blend", .db = -4.8f},
    {.preset = "Morph/Smooth_Crossfade", .db = 11.0f},
    {.preset = "Morph/Soft_to_Hard", .db = 4.6f},
    {.preset = "Morph/Tape_Fold_Morph", .db = 9.0f},
    {.preset = "Morph/Three_Node_Blend", .db = 9.4f},
    {.preset = "Morph/Tube_to_Fuzz", .db = 8.0f},
    {.preset = "Morph/Wavefolder_Quad", .db = 13.2f},
    {.preset = "Pads/Allpass_Shimmer", .db = 12.5f},
    {.preset = "Pads/Evolving_Texture", .db = 7.1f},
    {.preset = "Pads/Formant_Pad", .db = -2.5f},
    {.preset = "Pads/Gentle_Warmth", .db = 8.3f},
    {.preset = "Pads/Granular_Wash", .db = -2.5f},
    {.preset = "Pads/Slow_Morph_Pad", .db = 7.9f},
    {.preset = "Pads/Soft_Sweep", .db = -0.8f},
    {.preset = "Pads/Spectral_Pad", .db = -3.6f},
    {.preset = "Pads/Stochastic_Drift", .db = 2.4f},
    {.preset = "Pads/Tape_Haze", .db = 13.5f},
    {.preset = "Sweep/Ease_In_Morph", .db = 8.0f},
    {.preset = "Sweep/Envelope_Follower", .db = 8.1f},
    {.preset = "Sweep/Frequency_Hunter", .db = 8.8f},
    {.preset = "Sweep/High_Sweep", .db = 2.8f},
    {.preset = "Sweep/Inverse_Link", .db = 6.1f},
    {.preset = "Sweep/Low_Rumble", .db = 7.7f},
    {.preset = "Sweep/Random_Scan", .db = -0.1f},
    {.preset = "Sweep/Resonant_Sweep", .db = -2.3f},
    {.preset = "Sweep/Sharp_Focus", .db = 9.6f},
    {.preset = "Sweep/Slow_Scan", .db = 8.0f},
    {.preset = "Sweep/Step_Scanner", .db = 8.6f},
    {.preset = "Sweep/Synced_Pulse", .db = 9.4f},
    {.preset = "Sweep/Triangle_Wobble", .db = 10.0f},
    {.preset = "Sweep/Wah_Wah_Drive", .db = 8.1f},
    {.preset = "Sweep/Wide_Sweep", .db = 8.2f},
};

/// Output Gain dB [-24, +24] -> normalized [0, 1] (the processor's kGainMinDb / kGainRangeDb)
float outputGainNorm(float db) {
    return (db + 24.0f) / 48.0f;
}

// ==============================================================================
// Main
// ==============================================================================

int run(int argc, char* argv[]) {
    std::filesystem::path outputDir = "plugins/disrumpo/resources/presets";

    if (argc > 1) {
        outputDir = argv[1];
    }

    std::filesystem::create_directories(outputDir);

    auto presets = createAllPresets();
    int successCount = 0;

    std::cout << "Generating " << presets.size() << " Disrumpo factory presets..." << '\n';

    // Verify expected count
    if (presets.size() != 120) {
        std::cerr << "WARNING: Expected 120 presets, got " << presets.size() << '\n';
    }

    size_t trimsApplied = 0;
    for (const auto& preset : presets) {
        auto categoryDir = outputDir / preset.category;
        std::filesystem::create_directories(categoryDir);

        // Create filename: PresetName.vstpreset
        std::string stem;
        for (char c : preset.name) {
            if (c == ' ') stem += '_';
            else if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') stem += c;
        }
        const std::string filename = stem + ".vstpreset";

        DisrumpoPresetState state = preset.state;
        const std::string key = preset.category + "/" + stem;
        for (const auto& trim : kOutputTrims) {
            if (key == trim.preset) {
                state.outputGain = outputGainNorm(trim.db);
                ++trimsApplied;
            }
        }
        auto stateBytes = state.serialize();

        auto path = categoryDir / filename;

        if (preset.description.empty()) {
            std::cerr << "ERROR: " << key << " has no description" << '\n';
            continue;
        }
        if (writeVstPreset(path, stateBytes, buildInfoXml(preset.name, preset.category, preset.description))) {
            std::cout << "  Created: " << preset.category << "/" << filename << '\n';
            successCount++;
        }
    }

    std::cout << "\nGenerated " << successCount << " of " << presets.size() << " presets." << '\n';
    std::cout << "Output directory: " << std::filesystem::absolute(outputDir) << '\n';

    if (trimsApplied != std::size(kOutputTrims)) {
        std::cerr << "ERROR: " << std::size(kOutputTrims) - trimsApplied << " output trims name no preset" << '\n';
        return 1;
    }
    return std::cmp_equal(successCount, presets.size()) ? 0 : 1;
}

}  // namespace

// Every statement is inside try / catch (...); bugprone-exception-escape still reports the
// stream writes in the handlers as escaping (as tools/profundum_preset_generator.cpp).
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
    } catch (...) {
        std::cerr << "ERROR: unknown exception" << '\n';
    }
    return 1;
}
