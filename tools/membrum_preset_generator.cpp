// ==============================================================================
// Factory Kit Preset Generator for Membrum
// ==============================================================================
// Generates .vstpreset files containing kit blobs in the v1 state codec
// format produced by Membrum::State::writeKitBlob (see state_codec.{h,cpp}).
//
// On-wire layout (little-endian):
//   [int32 version = 1]
//   [int32 maxPolyphony]
//   [int32 stealPolicy]
//   For each of 32 pads:
//     [int32 exciterType][int32 bodyModel]
//     [52 x float64 sound, with choke/bus mirrored at indices 28/29]
//     [uint8 chokeGroup][uint8 outputBus]
//   [int32 selectedPadIndex]
//   [4 x float64: globalCoupling, snareBuzz, tomResonance, couplingDelayMs]
//   [32 x float64 per-pad couplingAmount]
//   [160 x float64 pad-major macros]
//   [float64 masterGainNorm]
//   Optional session field [int32 uiMode] when emitted.
//
// Usage: membrum_preset_generator [output_dir]
//   Default output_dir: plugins/membrum/resources/presets/Kit Presets/
// ==============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// ==============================================================================
// Constants matching plugins/membrum/src/state/state_codec.h
// ==============================================================================

constexpr int kNumPads           = 32;
constexpr int kVersion           = 1;
constexpr int kSoundSlotsPerPad  = 52;

// kProcessorUID(0x4D656D62, 0x72756D50, 0x726F6331, 0x00000136)
const char kClassIdAscii[33] = "4D656D6272756D50726F633100000136";

enum ExciterType : int {
    Impulse    = 0,
    Mallet     = 1,
    NoiseBurst = 2,
    Friction   = 3,
    FMImpulse  = 4,
    Feedback   = 5,
};

enum BodyModelType : int {
    Membrane  = 0,
    Plate     = 1,
    Shell     = 2,
    String    = 3,
    Bell      = 4,
    NoiseBody = 5,
};

// Filter type normalised values used by a few kits.
namespace FilterType {
    constexpr double LP = 0.0;
    constexpr double HP = 0.5;
    constexpr double BP = 1.0;
}

// ==============================================================================
// Pad data -- mirrors the JS object shape one-for-one. Each field maps to a
// fixed slot in PadSnapshot::sound (see state_codec.h for the index map).
// ==============================================================================

struct Pad {
    int    exciterType = ExciterType::Impulse;
    int    bodyModel   = BodyModelType::Membrane;

    // Phase 1 core
    double material = 0.5, size = 0.5, decay = 0.3, strikePosition = 0.3, level = 0.8;
    // Phase 2 tone shaper
    double tsFilterType = 0.0, tsFilterCutoff = 1.0, tsFilterResonance = 0.0;
    double tsFilterEnvAmount = 0.5, tsDriveAmount = 0.0, tsFoldAmount = 0.0;
    double tsPitchEnvStart = 0.0, tsPitchEnvEnd = 0.0, tsPitchEnvTime = 0.0;
    double tsPitchEnvCurve = 0.0;
    double tsFilterEnvAttack = 0.0, tsFilterEnvDecay = 0.1;
    double tsFilterEnvSustain = 0.0, tsFilterEnvRelease = 0.1;
    // Unnatural zone
    double modeStretch = 0.333333, decaySkew = 0.5;
    double modeInjectAmount = 0.0, nonlinearCoupling = 0.0;
    // Material morph
    double morphEnabled = 0.0, morphStart = 1.0, morphEnd = 0.0;
    double morphDuration = 0.095477, morphCurve = 0.0;
    // Routing (uint8 below is authoritative; doubles mirrored on wire).
    int    chokeGroup = 0, outputBus = 0;
    // Exciter secondary
    double fmRatio = 0.5, feedbackAmount = 0.0;
    double noiseBurstDuration = 0.5, frictionPressure = 0.0;
    // Phase 5 per-pad coupling participation
    double couplingAmount = 0.5;
    // Phase 6 macros (neutral)
    double macroTightness = 0.5, macroBrightness = 0.5, macroBodySize = 0.5;
    double macroPunch = 0.5, macroComplexity = 0.5;

    // Phase 7 noise layer (always-on filtered noise running parallel to the
    // modal body; great for snare/hat realism, can be muted by mix=0).
    double noiseLayerMix       = 0.35;
    double noiseLayerCutoff    = 0.5;
    double noiseLayerResonance = 0.2;
    double noiseLayerDecay     = 0.3;
    double noiseLayerColor     = 0.5;
    // Phase 7 attack click transient (raised-cosine filtered-noise burst).
    double clickLayerMix        = 0.5;
    double clickLayerContactMs  = 0.3;
    double clickLayerBrightness = 0.6;
    // Phase 8A per-mode damping. The PadConfig sentinel (-1.0 = "let mapper
    // derive") cannot survive the controller's normalised parameter system,
    // so write explicit mid-range defaults. b1=0.40 -> ~0.35 s decay floor;
    // b3=0.40 -> moderate woody high-mode damping.
    double bodyDampingB1 = 0.40;
    double bodyDampingB3 = 0.40;
    // Phase 8C air-loading + modeScatter. 0.6 is the realistic membrane
    // default (Rossing 1982); modeScatter=0 keeps pure ratios.
    double airLoading  = 0.6;
    double modeScatter = 0.0;
    // Phase 8D head/shell coupling (off by default).
    double couplingStrength  = 0.0;
    double secondaryEnabled  = 0.0;
    double secondarySize     = 0.5;
    double secondaryMaterial = 0.4;
    // Phase 8E nonlinear tension modulation depth.
    double tensionModAmt = 0.0;
    // Phase 8F per-pad enable toggle (1.0 = on).
    double enabled = 1.0;
};

struct OverrideEntry {
    std::uint8_t src;
    std::uint8_t dst;
    float        coeff;
};

struct KitOpts {
    int    maxPolyphony     = 8;
    int    stealingPolicy   = 0;
    int    selectedPadIndex = 0;
    double globalCoupling   = 0.0;
    double snareBuzz        = 0.0;
    double tomResonance     = 0.0;
    double couplingDelayMs  = 1.0;
    double masterGainNorm   = 0.5;
    int    uiMode           = 0;
    bool   includeSession   = true;
    // v1 dropped the override matrix; left in the API as a no-op so the
    // kit-builder code can stay structurally identical to the JS source.
    std::vector<OverrideEntry> overrides;
};

struct Kit {
    std::string         name;
    std::string         subdir;
    std::vector<Pad>    pads;
    KitOpts             opts;
    std::vector<int>    crafted; ///< If non-empty, all other pads get enabled=0.
};

// ==============================================================================
// Helpers
// ==============================================================================

double toLogNorm(double hz) {
    return std::log(hz / 20.0) / std::log(100.0);
}

// Phase 8F: leave only genuinely-crafted pads sounding. A factory kit wired
// to bulk-loop "filler" pads (e.g. one perc patch repeated 19 times) wastes
// user attention; better to silence them so re-enabling a slot is an
// explicit "I want this pad" gesture.
void disableUncraftedPads(std::vector<Pad>& pads, const std::vector<int>& crafted) {
    std::vector<bool> keep(pads.size(), false);
    for (int i : crafted) {
        if (i >= 0 && static_cast<std::size_t>(i) < pads.size())
            keep[static_cast<std::size_t>(i)] = true;
    }
    for (std::size_t i = 0; i < pads.size(); ++i) {
        if (!keep[i])
            pads[i].enabled = 0.0;
    }
}

std::vector<Pad> defaultPads() {
    return std::vector<Pad>(kNumPads);
}

// ==============================================================================
// Binary blob writer
// ==============================================================================

inline void writeI8 (std::vector<std::uint8_t>& b, std::uint8_t v) {
    b.push_back(v);
}
inline void writeI16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    for (int i = 0; i < 2; ++i) b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}
inline void writeI32(std::vector<std::uint8_t>& b, std::int32_t v) {
    auto* p = reinterpret_cast<const std::uint8_t*>(&v);
    b.insert(b.end(), p, p + 4);
}
inline void writeF32(std::vector<std::uint8_t>& b, float v) {
    auto* p = reinterpret_cast<const std::uint8_t*>(&v);
    b.insert(b.end(), p, p + 4);
}
inline void writeF64(std::vector<std::uint8_t>& b, double v) {
    auto* p = reinterpret_cast<const std::uint8_t*>(&v);
    b.insert(b.end(), p, p + 8);
}

void writePadToBuffer(std::vector<std::uint8_t>& buf, const Pad& p) {
    writeI32(buf, p.exciterType);
    writeI32(buf, p.bodyModel);

    // 52 x float64 -- layout matches PadSnapshot::sound.
    const double sound[kSoundSlotsPerPad] = {
        // [0..27] Phase 1-6 contiguous block.
        p.material, p.size, p.decay, p.strikePosition, p.level,
        p.tsFilterType, p.tsFilterCutoff, p.tsFilterResonance,
        p.tsFilterEnvAmount, p.tsDriveAmount, p.tsFoldAmount,
        p.tsPitchEnvStart, p.tsPitchEnvEnd, p.tsPitchEnvTime,
        p.tsPitchEnvCurve,
        p.tsFilterEnvAttack, p.tsFilterEnvDecay,
        p.tsFilterEnvSustain, p.tsFilterEnvRelease,
        p.modeStretch, p.decaySkew, p.modeInjectAmount,
        p.nonlinearCoupling,
        p.morphEnabled, p.morphStart, p.morphEnd,
        p.morphDuration, p.morphCurve,
        // [28..29] choke/bus float64 mirrors.
        static_cast<double>(p.chokeGroup), static_cast<double>(p.outputBus),
        // [30..33] exciter secondary block.
        p.fmRatio, p.feedbackAmount,
        p.noiseBurstDuration, p.frictionPressure,
        // [34..38] Phase 7 noise layer.
        p.noiseLayerMix, p.noiseLayerCutoff,
        p.noiseLayerResonance, p.noiseLayerDecay, p.noiseLayerColor,
        // [39..41] Phase 7 click layer.
        p.clickLayerMix, p.clickLayerContactMs, p.clickLayerBrightness,
        // [42..43] Phase 8A body damping.
        p.bodyDampingB1, p.bodyDampingB3,
        // [44..45] Phase 8C air-loading + scatter.
        p.airLoading, p.modeScatter,
        // [46..49] Phase 8D head/shell coupling.
        p.couplingStrength, p.secondaryEnabled,
        p.secondarySize, p.secondaryMaterial,
        // [50] Phase 8E tension modulation.
        p.tensionModAmt,
        // [51] Phase 8F per-pad enable.
        p.enabled,
    };
    for (double v : sound)
        writeF64(buf, v);

    writeI8(buf, static_cast<std::uint8_t>(p.chokeGroup));
    writeI8(buf, static_cast<std::uint8_t>(p.outputBus));
}

std::vector<std::uint8_t> writeKitBlob(const std::vector<Pad>& pads, const KitOpts& opts) {
    std::vector<std::uint8_t> buf;
    buf.reserve(16384);

    writeI32(buf, kVersion);
    writeI32(buf, opts.maxPolyphony);
    writeI32(buf, opts.stealingPolicy);

    for (const auto& p : pads)
        writePadToBuffer(buf, p);

    writeI32(buf, opts.selectedPadIndex);
    writeF64(buf, opts.globalCoupling);
    writeF64(buf, opts.snareBuzz);
    writeF64(buf, opts.tomResonance);
    writeF64(buf, opts.couplingDelayMs);

    for (const auto& p : pads)
        writeF64(buf, p.couplingAmount);

    // Macros, pad-major.
    for (const auto& p : pads) {
        writeF64(buf, p.macroTightness);
        writeF64(buf, p.macroBrightness);
        writeF64(buf, p.macroBodySize);
        writeF64(buf, p.macroPunch);
        writeF64(buf, p.macroComplexity);
    }

    writeF64(buf, opts.masterGainNorm);

    if (opts.includeSession)
        writeI32(buf, opts.uiMode);

    return buf;
}

// ==============================================================================
// VST3 .vstpreset writer (Comp + Info chunks).
// ==============================================================================

inline void writeLE32(std::ofstream& f, std::uint32_t v) {
    f.write(reinterpret_cast<const char*>(&v), 4);
}
inline void writeLE64(std::ofstream& f, std::int64_t v) {
    f.write(reinterpret_cast<const char*>(&v), 8);
}

std::string buildInfoXml(const std::string& presetName, const std::string& subcat) {
    std::string xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<MetaInfo>\n";
    xml += "  <Attr id=\"MediaType\" value=\"VstPreset\" type=\"string\"/>\n";
    xml += "  <Attr id=\"PlugInName\" value=\"Membrum/Kits\" type=\"string\"/>\n";
    xml += "  <Attr id=\"PlugInCategory\" value=\"Kit Presets\" type=\"string\"/>\n";
    xml += "  <Attr id=\"Name\" value=\"" + presetName + "\" type=\"string\"/>\n";
    xml += "  <Attr id=\"MusicalCategory\" value=\"" + subcat + "\" type=\"string\"/>\n";
    xml += "  <Attr id=\"MusicalInstrument\" value=\"" + subcat + "\" type=\"string\"/>\n";
    xml += "</MetaInfo>\n";
    return xml;
}

bool writeVstPreset(const std::filesystem::path& path,
                    const std::vector<std::uint8_t>& componentState,
                    const std::string& presetName,
                    const std::string& subcat) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "Failed to create: " << path << std::endl;
        return false;
    }

    const std::string info = buildInfoXml(presetName, subcat);

    constexpr std::int64_t kHeaderSize = 48;
    const std::int64_t compOffset = kHeaderSize;
    const std::int64_t compSize   = static_cast<std::int64_t>(componentState.size());
    const std::int64_t infoOffset = compOffset + compSize;
    const std::int64_t infoSize   = static_cast<std::int64_t>(info.size());
    const std::int64_t listOffset = infoOffset + infoSize;

    // Header
    f.write("VST3", 4);
    writeLE32(f, 1);
    f.write(kClassIdAscii, 32);
    writeLE64(f, listOffset);

    // Comp data
    f.write(reinterpret_cast<const char*>(componentState.data()), compSize);

    // Info data (XML metadata, no terminator)
    f.write(info.data(), infoSize);

    // Chunk List: 2 entries.
    f.write("List", 4);
    writeLE32(f, 2);
    f.write("Comp", 4);
    writeLE64(f, compOffset);
    writeLE64(f, compSize);
    f.write("Info", 4);
    writeLE64(f, infoOffset);
    writeLE64(f, infoSize);

    return f.good();
}

// ==============================================================================
// Forward declarations: kit builders (one per factory preset).
// ==============================================================================
Kit acousticKit();
Kit electronicKit();
Kit experimentalKit();
Kit jazzBrushesKit();
Kit rockBigRoomKit();
Kit vintageWoodKit();
Kit orchestralKit();
Kit nineOhNineKit();
Kit linnDrumKit();
Kit modularWestCoastKit();
Kit trapModernKit();
Kit handDrumsKit();
Kit latinPercKit();
Kit tablaKit();
Kit worldMetalKit();
Kit cajonFramesKit();
Kit glassBellGardenKit();
Kit droneSustainKit();
Kit chaosEngineKit();
Kit ghostBonesKit();

// ==============================================================================
// Main
// ==============================================================================

int main(int argc, char* argv[]) {
    std::filesystem::path outputBase = "plugins/membrum/resources/presets/Kit Presets";
    if (argc > 1)
        outputBase = argv[1];

    // Kit subcategories MUST match the hardcoded list in
    // plugins/membrum/src/preset/membrum_preset_config.h
    // (Acoustic, Electronic, Percussive, Unnatural).
    const std::vector<Kit> kits = {
        // --- Acoustic (5) ---
        acousticKit(),
        jazzBrushesKit(),
        rockBigRoomKit(),
        vintageWoodKit(),
        orchestralKit(),

        // --- Electronic (5) ---
        electronicKit(),
        nineOhNineKit(),
        linnDrumKit(),
        modularWestCoastKit(),
        trapModernKit(),

        // --- Percussive (5) ---
        handDrumsKit(),
        latinPercKit(),
        tablaKit(),
        worldMetalKit(),
        cajonFramesKit(),

        // --- Unnatural (5) ---
        experimentalKit(),
        glassBellGardenKit(),
        droneSustainKit(),
        chaosEngineKit(),
        ghostBonesKit(),
    };

    int generated = 0;
    for (const auto& kit : kits) {
        const auto dir = outputBase / kit.subdir;
        std::filesystem::create_directories(dir);

        // Apply crafted-pad gating (Phase 8F).
        std::vector<Pad> pads = kit.pads;
        if (!kit.crafted.empty())
            disableUncraftedPads(pads, kit.crafted);

        const auto blob = writeKitBlob(pads, kit.opts);
        const auto path = dir / (kit.name + ".vstpreset");

        if (writeVstPreset(path, blob, kit.name, kit.subdir)) {
            std::cout << "  Wrote " << blob.size() << " bytes to " << path.string() << "\n";
            ++generated;
        }
    }

    std::cout << "\nGenerated " << generated << " of " << kits.size()
              << " factory kit presets." << std::endl;
    return generated == static_cast<int>(kits.size()) ? 0 : 1;
}

// ==============================================================================
// Kit definitions.
// ==============================================================================

Kit acousticKit() {
    Kit k{"Acoustic Studio Kit", "Acoustic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // ---- Pad 0: Acoustic Kick ----
    pads[0].exciterType = ExciterType::Mallet;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.35;
    pads[0].size = 0.85;
    pads[0].decay = 0.25;
    pads[0].level = 0.85;
    pads[0].tsPitchEnvStart = toLogNorm(160);
    pads[0].tsPitchEnvEnd   = toLogNorm(50);
    pads[0].tsPitchEnvTime = 0.04;
    pads[0].airLoading = 0.78;
    pads[0].couplingStrength  = 0.35;
    pads[0].secondaryEnabled  = 1.0;
    pads[0].secondarySize     = 0.40;
    pads[0].secondaryMaterial = 0.60;
    pads[0].tensionModAmt = 0.18;
    pads[0].clickLayerMix        = 0.70;
    pads[0].clickLayerContactMs  = 0.20;
    pads[0].clickLayerBrightness = 0.45;
    pads[0].noiseLayerMix        = 0.10;

    // ---- Pad 2: Acoustic Snare ----
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.5;
    pads[2].size = 0.5;
    pads[2].decay = 0.4;
    pads[2].level = 0.82;
    pads[2].noiseBurstDuration = (8 - 2) / 13.0;
    pads[2].noiseLayerMix        = 0.65;
    pads[2].noiseLayerCutoff     = 0.72;
    pads[2].noiseLayerResonance  = 0.15;
    pads[2].noiseLayerDecay      = 0.45;
    pads[2].noiseLayerColor      = 0.62;
    pads[2].clickLayerMix        = 0.55;
    pads[2].clickLayerContactMs  = 0.15;
    pads[2].clickLayerBrightness = 0.78;
    pads[2].airLoading  = 0.50;
    pads[2].modeScatter = 0.15;
    pads[2].couplingStrength  = 0.25;
    pads[2].secondaryEnabled  = 1.0;
    pads[2].secondarySize     = 0.55;
    pads[2].secondaryMaterial = 0.50;

    // ---- Pad 4: Side Stick ----
    pads[4].exciterType = ExciterType::Impulse;
    pads[4].bodyModel = BodyModelType::Shell;
    pads[4].material = 0.8;
    pads[4].size = 0.2;
    pads[4].decay = 0.15;
    pads[4].level = 0.78;
    pads[4].clickLayerMix        = 0.85;
    pads[4].clickLayerContactMs  = 0.10;
    pads[4].clickLayerBrightness = 0.85;
    pads[4].noiseLayerMix = 0.0;
    pads[4].airLoading    = 0.0;
    pads[4].modeScatter   = 0.40;

    // ---- Hi-hats ----
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::NoiseBody;
    pads[6].material = 0.88;
    pads[6].size = 0.15;
    pads[6].decay = 0.1;
    pads[6].level = 0.75;
    pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix       = 0.70;
    pads[6].noiseLayerCutoff    = 0.86;
    pads[6].noiseLayerColor     = 0.78;
    pads[6].noiseLayerDecay     = 0.10;
    pads[6].clickLayerMix       = 0.18;
    pads[6].airLoading          = 0.0;
    pads[6].modeScatter         = 0.35;
    pads[6].bodyDampingB3       = 0.0;
    pads[6].bodyDampingB1       = 0.55;

    pads[8] = pads[6];
    pads[8].decay           = 0.05;
    pads[8].noiseLayerDecay = 0.06;
    pads[8].bodyDampingB1   = 0.65;
    pads[8].chokeGroup      = 1;

    pads[10] = pads[6];
    pads[10].decay           = 0.6;
    pads[10].noiseLayerDecay = 0.55;
    pads[10].bodyDampingB1   = 0.30;
    pads[10].chokeGroup      = 1;

    // ---- Toms ----
    const int    tomPads[]  = {5, 7, 9, 11, 12, 14};
    const double tomSizes[] = {0.8, 0.7, 0.6, 0.5, 0.45, 0.4};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel = BodyModelType::Membrane;
        pads[p].material = 0.4;
        pads[p].size = tomSizes[i];
        pads[p].decay = 0.5;
        pads[p].level = 0.8;
        pads[p].airLoading  = 0.70;
        pads[p].modeScatter = 0.10;
        pads[p].bodyDampingB1 = 0.30 + 0.02 * i;
        pads[p].bodyDampingB3 = 0.10;
        pads[p].couplingStrength  = 0.40;
        pads[p].secondaryEnabled  = 1.0;
        pads[p].secondarySize     = 0.30 + 0.02 * i;
        pads[p].secondaryMaterial = 0.55;
        pads[p].tensionModAmt = 0.22;
        pads[p].noiseLayerMix        = 0.18;
        pads[p].noiseLayerCutoff     = 0.45;
        pads[p].clickLayerMix        = 0.50;
        pads[p].clickLayerContactMs  = 0.32;
        pads[p].clickLayerBrightness = 0.55;
    }

    // ---- Cymbals ----
    const int cymbalPads[] = {13, 15, 16, 17, 19, 21, 23};
    for (int p : cymbalPads) {
        pads[p].exciterType = ExciterType::NoiseBurst;
        pads[p].bodyModel = BodyModelType::NoiseBody;
        pads[p].material = 0.95;
        pads[p].size = 0.3;
        pads[p].decay = 0.8;
        pads[p].level = 0.72;
        pads[p].modeScatter   = 0.55;
        pads[p].airLoading    = 0.0;
        pads[p].bodyDampingB3 = 0.0;
        pads[p].bodyDampingB1 = 0.30;
        pads[p].noiseLayerMix       = 0.50;
        pads[p].noiseLayerCutoff    = 0.90;
        pads[p].noiseLayerColor     = 0.80;
        pads[p].noiseLayerDecay     = 0.70;
        pads[p].clickLayerMix       = 0.20;
        pads[p].clickLayerBrightness = 0.85;
    }

    // ---- Misc perc ----
    const int percPads[] = {1, 3, 18, 20, 22, 24, 25, 26, 27, 28, 29, 30, 31};
    for (int p : percPads) {
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel = BodyModelType::Plate;
        pads[p].material = 0.7;
        pads[p].size = 0.3;
        pads[p].decay = 0.3;
        pads[p].level = 0.78;
        pads[p].airLoading           = 0.30;
        pads[p].modeScatter          = 0.18;
        pads[p].clickLayerMix        = 0.40;
        pads[p].clickLayerContactMs  = 0.25;
        pads[p].clickLayerBrightness = 0.65;
        pads[p].noiseLayerMix        = 0.12;
    }

    k.crafted = {0, 2, 4, 5, 7, 9, 11, 12, 14, 6, 8, 10, 13};
    return k;
}
Kit electronicKit() {
    Kit k{"808 Electronic Kit", "Electronic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Synth-domain global tweaks: zero airLoading, no shell coupling, no
    // mode scatter. Electronic kits earn their character from clean modal
    // pitches and (for 808-style) a long tension-modulated boom on the kick.

    // ---- Pad 0: 808 Kick (clean sub + boom-glide) ----
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.15;
    pads[0].size = 0.9;
    pads[0].decay = 0.35;
    pads[0].level = 0.85;
    pads[0].tsPitchEnvStart = toLogNorm(200);
    pads[0].tsPitchEnvEnd   = toLogNorm(40);
    pads[0].tsPitchEnvTime = 0.06;
    pads[0].airLoading       = 0.0;
    pads[0].couplingStrength = 0.0;
    pads[0].secondaryEnabled = 0.0;
    pads[0].tensionModAmt = 0.30;
    pads[0].clickLayerMix        = 0.35;
    pads[0].clickLayerContactMs  = 0.20;
    pads[0].clickLayerBrightness = 0.30;
    pads[0].noiseLayerMix        = 0.0;

    // ---- Pads 2 & 4: 808 / electronic snares ----
    for (int p : {2, 4}) {
        pads[p].exciterType = ExciterType::NoiseBurst;
        pads[p].bodyModel = BodyModelType::Membrane;
        pads[p].level = 0.8;
        pads[p].airLoading       = 0.0;
        pads[p].couplingStrength = 0.0;
        pads[p].secondaryEnabled = 0.0;
        pads[p].modeScatter      = 0.0;
        pads[p].noiseLayerMix      = 0.55;
        pads[p].noiseLayerCutoff   = 0.85;
        pads[p].noiseLayerColor    = 0.82;
        pads[p].noiseLayerDecay    = 0.30;
        pads[p].clickLayerMix      = 0.32;
        pads[p].clickLayerContactMs = 0.18;
    }
    pads[2].material = 0.55; pads[2].size = 0.45; pads[2].decay = 0.35;
    pads[2].noiseBurstDuration = (6 - 2) / 13.0;
    pads[4].material = 0.6;  pads[4].size = 0.4;  pads[4].decay = 0.3;
    pads[4].noiseBurstDuration = (5 - 2) / 13.0;

    // ---- Hats: pads 6 (closed) / 8 (pedal) / 10 (open), choke group 1 ----
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::NoiseBody;
    pads[6].material = 0.92;
    pads[6].size = 0.1;
    pads[6].decay = 0.08;
    pads[6].level = 0.75;
    pads[6].chokeGroup = 1;
    pads[6].noiseBurstDuration = (3 - 2) / 13.0;
    pads[6].noiseLayerMix    = 0.85;
    pads[6].noiseLayerCutoff = 0.92;
    pads[6].noiseLayerColor  = 0.85;
    pads[6].noiseLayerDecay  = 0.10;
    pads[6].clickLayerMix    = 0.0;
    pads[6].airLoading       = 0.0;

    pads[8] = pads[6];
    pads[8].material = 0.88; pads[8].size = 0.12; pads[8].decay = 0.06;
    pads[8].level = 0.7;     pads[8].noiseLayerDecay = 0.07;

    pads[10] = pads[6];
    pads[10].material = 0.9; pads[10].size = 0.2; pads[10].decay = 0.5;
    pads[10].noiseLayerDecay = 0.55;

    // ---- Pad 13: Crash ----
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel = BodyModelType::NoiseBody;
    pads[13].material = 0.95;
    pads[13].size = 0.35;
    pads[13].decay = 0.7;
    pads[13].level = 0.7;
    pads[13].noiseLayerMix    = 0.55;
    pads[13].noiseLayerCutoff = 0.92;
    pads[13].noiseLayerColor  = 0.82;
    pads[13].noiseLayerDecay  = 0.65;
    pads[13].airLoading       = 0.0;

    // ---- Toms: 808-style Mallet+Membrane with the iconic boom-thud glide
    const int    tomPads[]      = {5, 7, 9, 11, 12, 14};
    const double tomSizes[]     = {0.85, 0.75, 0.65, 0.55, 0.48, 0.40};
    const double tomPitchStart[] = {220,  260,  310,  370,  430,  500};
    const double tomPitchEnd[]  = { 80,   95,  115,  140,  175,  220};
    const double tomPitchTime[] = {0.50, 0.42, 0.36, 0.30, 0.24, 0.18};
    const double tomMaterial[]  = {0.18, 0.25, 0.32, 0.40, 0.50, 0.60};
    const double tomDecay[]     = {0.65, 0.58, 0.50, 0.43, 0.35, 0.28};
    const double tomBodyB1[]    = {0.10, 0.15, 0.20, 0.25, 0.32, 0.42};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel   = BodyModelType::Membrane;
        pads[p].material    = tomMaterial[i];
        pads[p].size        = tomSizes[i];
        pads[p].decay       = tomDecay[i];
        pads[p].level       = 0.85;
        pads[p].tsPitchEnvStart = toLogNorm(tomPitchStart[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomPitchEnd[i]);
        pads[p].tsPitchEnvTime  = tomPitchTime[i];
        pads[p].tsPitchEnvCurve = 1.0;
        pads[p].airLoading       = 0.0;
        pads[p].couplingStrength = 0.0;
        pads[p].secondaryEnabled = 0.0;
        pads[p].tensionModAmt    = 0.30;
        pads[p].noiseLayerMix    = 0.05;
        pads[p].clickLayerMix    = 0.05;
        pads[p].bodyDampingB1    = tomBodyB1[i];
        pads[p].bodyDampingB3    = 0.10;
    }

    // ---- FM-bell perc: pad 1 only, rest disabled via crafted list ----
    const int percPads[] = {1, 3, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    for (int p : percPads) {
        pads[p].exciterType = ExciterType::FMImpulse;
        pads[p].bodyModel = BodyModelType::Bell;
        pads[p].material = 0.7;
        pads[p].size = 0.25;
        pads[p].decay = 0.2;
        pads[p].level = 0.75;
        pads[p].fmRatio = 0.4;
        pads[p].airLoading       = 0.0;
        pads[p].bodyDampingB3    = 0.0;
        pads[p].noiseLayerMix    = 0.0;
        pads[p].clickLayerMix    = 0.15;
        pads[p].clickLayerBrightness = 0.7;
    }

    k.crafted = {0, 2, 4, 5, 7, 9, 11, 12, 14, 6, 8, 10, 13};
    return k;
}
Kit experimentalKit() {
    Kit k{"Experimental FX Kit", "Unnatural", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // ---- Pad 0: FM Kick (chaotic glide + ringing shell coupling) ----
    pads[0].exciterType = ExciterType::FMImpulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.2;
    pads[0].size = 0.95;
    pads[0].decay = 0.4;
    pads[0].level = 0.85;
    pads[0].fmRatio = 0.6;
    pads[0].tsPitchEnvStart = toLogNorm(300);
    pads[0].tsPitchEnvEnd   = toLogNorm(30);
    pads[0].tsPitchEnvTime = 0.08;
    pads[0].airLoading       = 0.40;
    pads[0].modeScatter      = 0.20;
    pads[0].couplingStrength  = 0.50;
    pads[0].secondaryEnabled  = 1.0;
    pads[0].secondarySize     = 0.60;
    pads[0].secondaryMaterial = 0.85;
    pads[0].tensionModAmt = 0.50;
    pads[0].clickLayerMix       = 0.45;
    pads[0].clickLayerBrightness = 0.40;
    pads[0].noiseLayerMix       = 0.25;

    // ---- Pad 2: Feedback Snare (resonant shell + scatter) ----
    pads[2].exciterType = ExciterType::Feedback;
    pads[2].bodyModel = BodyModelType::Shell;
    pads[2].material = 0.6;
    pads[2].size = 0.4;
    pads[2].decay = 0.35;
    pads[2].level = 0.8;
    pads[2].feedbackAmount = 0.4;
    pads[2].modeScatter      = 0.30;
    pads[2].couplingStrength  = 0.40;
    pads[2].secondaryEnabled  = 1.0;
    pads[2].secondarySize     = 0.45;
    pads[2].secondaryMaterial = 0.70;
    pads[2].tensionModAmt     = 0.40;
    pads[2].noiseLayerMix     = 0.40;
    pads[2].noiseLayerCutoff  = 0.65;
    pads[2].clickLayerMix     = 0.35;

    // ---- Pad 4: Friction FX ----
    pads[4].exciterType = ExciterType::Friction;
    pads[4].bodyModel = BodyModelType::String;
    pads[4].material = 0.5;
    pads[4].size = 0.6;
    pads[4].decay = 0.7;
    pads[4].level = 0.75;
    pads[4].frictionPressure = 0.5;
    pads[4].modeScatter      = 0.50;
    pads[4].couplingStrength = 0.30;
    pads[4].tensionModAmt    = 0.25;
    pads[4].noiseLayerMix    = 0.30;

    // ---- Metal hats ----
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::Bell;
    pads[6].material = 0.95;
    pads[6].size = 0.1;
    pads[6].decay = 0.08;
    pads[6].level = 0.7;
    pads[6].chokeGroup = 1;
    pads[6].morphEnabled = 1.0;
    pads[6].morphStart = 0.95;
    pads[6].morphEnd = 0.3;
    pads[6].morphDuration = 0.2;
    pads[6].modeScatter   = 0.45;
    pads[6].bodyDampingB3 = 0.0;
    pads[6].noiseLayerMix = 0.55;
    pads[6].noiseLayerCutoff = 0.90;
    pads[6].noiseLayerColor  = 0.85;

    pads[8] = pads[6];
    pads[8].decay = 0.04;

    pads[10] = pads[6];
    pads[10].decay = 0.6;

    // ---- Toms: inharmonic plates with shell coupling and pitch glide ----
    const int    tomPads[]  = {5, 7, 9, 11, 12, 14};
    const double tomSizes[] = {0.85, 0.75, 0.65, 0.55, 0.45, 0.35};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel = BodyModelType::Plate;
        pads[p].material = 0.5;
        pads[p].size = tomSizes[i];
        pads[p].decay = 0.6;
        pads[p].level = 0.78;
        pads[p].modeStretch = 0.5;
        pads[p].nonlinearCoupling = 0.3;
        pads[p].modeScatter = 0.50;
        pads[p].airLoading  = 0.20;
        pads[p].couplingStrength  = 0.30 + 0.04 * i;
        pads[p].secondaryEnabled  = 1.0;
        pads[p].secondarySize     = 0.30 + 0.05 * i;
        pads[p].secondaryMaterial = 0.65;
        pads[p].tensionModAmt = 0.40;
        pads[p].clickLayerMix = 0.40;
        pads[p].bodyDampingB1 = 0.30 + 0.04 * i;
        pads[p].bodyDampingB3 = 0.10;
    }

    // ---- FX pads ----
    const int fxPads[] = {1, 3, 13, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    const int bodyCycle[] = {BodyModelType::Bell, BodyModelType::String, BodyModelType::Shell, BodyModelType::Plate};
    for (int i = 0; i < static_cast<int>(sizeof(fxPads) / sizeof(fxPads[0])); ++i) {
        const int p = fxPads[i];
        pads[p].exciterType = (i % 2 == 0) ? ExciterType::FMImpulse : ExciterType::Feedback;
        pads[p].bodyModel = bodyCycle[i % 4];
        pads[p].material = 0.3 + (i * 0.03);
        pads[p].size = 0.2 + (i * 0.02);
        pads[p].decay = 0.3 + (i * 0.02);
        pads[p].level = 0.75;
        pads[p].modeStretch = 0.2 + (i * 0.04);
        pads[p].modeInjectAmount = 0.2;
        pads[p].nonlinearCoupling = 0.15;
        pads[p].fmRatio = 0.3 + (i * 0.02);
        pads[p].feedbackAmount = 0.1 + (i * 0.02);
        pads[p].modeScatter      = 0.25 + (i % 7) * 0.06;
        pads[p].airLoading       = (i % 5) * 0.15;
        pads[p].couplingStrength = (i % 3 == 0) ? 0.40 : 0.0;
        pads[p].secondaryEnabled = (i % 3 == 0) ? 1.0 : 0.0;
        pads[p].secondarySize    = 0.35 + (i % 4) * 0.10;
        pads[p].secondaryMaterial = 0.50 + (i % 5) * 0.08;
        pads[p].tensionModAmt    = 0.20 + (i % 6) * 0.08;
        pads[p].noiseLayerMix    = 0.05 + (i % 5) * 0.10;
        pads[p].noiseLayerColor  = 0.30 + (i % 7) * 0.10;
        pads[p].clickLayerMix    = 0.10 + (i % 4) * 0.15;
        if (pads[p].bodyModel == BodyModelType::Bell) {
            pads[p].bodyDampingB3 = 0.0;
        }
    }

    return k;
}
Kit jazzBrushesKit() {
    Kit k{"Jazz Brushes", "Acoustic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Kick: soft mallet, deep airLoading, almost no click.
    pads[0].exciterType = ExciterType::Mallet;
    pads[0].bodyModel   = BodyModelType::Membrane;
    pads[0].material    = 0.45;  pads[0].size = 0.72; pads[0].decay = 0.32;
    pads[0].level = 0.78;
    pads[0].tsPitchEnvStart = toLogNorm(140);
    pads[0].tsPitchEnvEnd   = toLogNorm(60);
    pads[0].tsPitchEnvTime  = 0.05;
    pads[0].airLoading       = 0.78;
    pads[0].couplingStrength = 0.30;
    pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize    = 0.40; pads[0].secondaryMaterial = 0.55;
    pads[0].tensionModAmt    = 0.12;
    pads[0].clickLayerMix       = 0.28;
    pads[0].clickLayerContactMs = 0.40;
    pads[0].clickLayerBrightness = 0.22;
    pads[0].noiseLayerMix       = 0.06;
    pads[0].bodyDampingB1 = 0.40; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroTightness = 0.45; pads[0].macroBrightness = 0.30;
    pads[0].macroBodySize = 0.55;  pads[0].macroPunch     = 0.32;
    pads[0].macroComplexity = 0.40;

    // Brush snare (sweep)
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel   = BodyModelType::Membrane;
    pads[2].material = 0.55; pads[2].size = 0.45; pads[2].decay = 0.30;
    pads[2].level = 0.75;
    pads[2].noiseBurstDuration = 0.85;
    pads[2].frictionPressure   = 0.60;
    pads[2].tsFilterType    = FilterType::HP;
    pads[2].tsFilterCutoff  = 0.25;
    pads[2].tsFilterResonance = 0.10;
    pads[2].tsFilterEnvAmount = 0.30;
    pads[2].tsFilterEnvAttack  = 0.05;
    pads[2].tsFilterEnvDecay   = 0.20;
    pads[2].tsFilterEnvSustain = 0.30;
    pads[2].tsFilterEnvRelease = 0.40;
    pads[2].morphEnabled = 1.0;
    pads[2].morphStart = 0.55; pads[2].morphEnd = 0.30;
    pads[2].morphDuration = 0.35; pads[2].morphCurve = 0.6;
    pads[2].noiseLayerMix    = 0.70; pads[2].noiseLayerCutoff = 0.55;
    pads[2].noiseLayerColor  = 0.40; pads[2].noiseLayerDecay = 0.55;
    pads[2].noiseLayerResonance = 0.05;
    pads[2].clickLayerMix    = 0.0;
    pads[2].airLoading  = 0.45; pads[2].modeScatter = 0.35;
    pads[2].couplingStrength = 0.0;  pads[2].secondaryEnabled = 0.0;
    pads[2].bodyDampingB1 = 0.45; pads[2].bodyDampingB3 = 0.05;
    pads[2].macroTightness = 0.30; pads[2].macroBrightness = 0.50;
    pads[2].macroBodySize = 0.55;  pads[2].macroPunch = 0.20;
    pads[2].macroComplexity = 0.65;
    pads[2].couplingAmount = 0.65;

    // Brush snare (tap)
    pads[4].exciterType = ExciterType::NoiseBurst;
    pads[4].bodyModel   = BodyModelType::Membrane;
    pads[4].material = 0.55; pads[4].size = 0.45; pads[4].decay = 0.30;
    pads[4].level = 0.78;
    pads[4].noiseBurstDuration = 0.30;
    pads[4].noiseLayerMix    = 0.45; pads[4].noiseLayerCutoff = 0.65;
    pads[4].noiseLayerColor  = 0.55; pads[4].noiseLayerDecay = 0.20;
    pads[4].clickLayerMix    = 0.40; pads[4].clickLayerContactMs = 0.18;
    pads[4].clickLayerBrightness = 0.55;
    pads[4].airLoading  = 0.45; pads[4].modeScatter = 0.18;
    pads[4].couplingStrength = 0.22; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.50; pads[4].secondaryMaterial = 0.50;
    pads[4].bodyDampingB1 = 0.40; pads[4].bodyDampingB3 = 0.10;
    pads[4].macroTightness = 0.55; pads[4].macroBrightness = 0.55;
    pads[4].macroComplexity = 0.40;

    // Hi-hats
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel   = BodyModelType::NoiseBody;
    pads[6].material = 0.85; pads[6].size = 0.13; pads[6].decay = 0.07;
    pads[6].level = 0.68; pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix = 0.85; pads[6].noiseLayerCutoff = 0.78;
    pads[6].noiseLayerColor = 0.65; pads[6].noiseLayerDecay = 0.08;
    pads[6].clickLayerMix = 0.18; pads[6].clickLayerContactMs = 0.10;
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.70;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.60;
    pads[6].macroBrightness = 0.55; pads[6].macroTightness = 0.70;

    pads[8] = pads[6];
    pads[8].decay = 0.05; pads[8].noiseLayerDecay = 0.06;
    pads[8].bodyDampingB1 = 0.70;

    pads[10] = pads[6];
    pads[10].decay = 0.55; pads[10].noiseLayerDecay = 0.78;
    pads[10].noiseLayerCutoff = 0.92;
    pads[10].noiseLayerResonance = 0.0;
    pads[10].noiseLayerMix = 0.55;
    pads[10].bodyDampingB1 = 0.55;
    pads[10].bodyDampingB3 = 0.20;
    pads[10].modeScatter = 0.85;

    // Toms
    const int    tomPads[]      = {5, 7, 9, 11, 12, 14};
    const double tomSizes[]     = {0.72, 0.62, 0.55, 0.48, 0.42, 0.36};
    const double tomMaterials[] = {0.40, 0.43, 0.46, 0.50, 0.55, 0.60};
    const double tomDecays[]    = {0.45, 0.40, 0.36, 0.32, 0.28, 0.24};
    const double tomB1[]        = {0.30, 0.32, 0.34, 0.36, 0.38, 0.42};
    const double tomPitchStart[] = {200, 240, 290, 340, 400, 470};
    const double tomPitchEnd[]   = {110, 135, 165, 200, 240, 290};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel   = BodyModelType::Membrane;
        pads[p].material = tomMaterials[i];
        pads[p].size     = tomSizes[i];
        pads[p].decay    = tomDecays[i];
        pads[p].level    = 0.78;
        pads[p].tsPitchEnvStart = toLogNorm(tomPitchStart[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomPitchEnd[i]);
        pads[p].tsPitchEnvTime  = 0.10;
        pads[p].tsPitchEnvCurve = 1.0;
        pads[p].airLoading      = 0.65;
        pads[p].modeScatter     = 0.10;
        pads[p].couplingStrength  = 0.32;
        pads[p].secondaryEnabled  = 1.0;
        pads[p].secondarySize     = 0.32 + 0.02 * i;
        pads[p].secondaryMaterial = 0.55;
        pads[p].tensionModAmt = 0.18;
        pads[p].noiseLayerMix = 0.15; pads[p].noiseLayerCutoff = 0.40;
        pads[p].clickLayerMix = 0.40; pads[p].clickLayerContactMs = 0.38;
        pads[p].clickLayerBrightness = 0.45;
        pads[p].bodyDampingB1 = tomB1[i]; pads[p].bodyDampingB3 = 0.10;
        pads[p].macroBodySize = 0.45 + 0.03 * i;
        pads[p].macroPunch     = 0.35;
        pads[p].macroBrightness = 0.40;
        pads[p].macroComplexity = 0.45;
    }

    // Ride cymbal (13)
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel   = BodyModelType::Bell;
    pads[13].material = 0.92; pads[13].size = 0.42; pads[13].decay = 0.85;
    pads[13].level = 0.74;
    pads[13].fmRatio = 0.35; pads[13].feedbackAmount = 0.05;
    pads[13].modeScatter = 0.28; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.30; pads[13].bodyDampingB1 = 0.40;
    pads[13].modeScatter = 0.85;
    pads[13].noiseLayerMix = 0.30; pads[13].noiseLayerCutoff = 0.85;
    pads[13].noiseLayerResonance = 0.0;
    pads[13].noiseLayerColor = 0.75; pads[13].noiseLayerDecay = 0.75;
    pads[13].clickLayerMix = 0.45; pads[13].clickLayerContactMs = 0.10;
    pads[13].clickLayerBrightness = 0.85;
    pads[13].outputBus = 1;
    pads[13].macroTightness = 0.30; pads[13].macroBrightness = 0.75;
    pads[13].macroComplexity = 0.55;

    // Crash (15)
    pads[15].exciterType = ExciterType::NoiseBurst;
    pads[15].bodyModel   = BodyModelType::NoiseBody;
    pads[15].material = 0.92; pads[15].size = 0.32; pads[15].decay = 0.65;
    pads[15].level = 0.70;
    pads[15].modeScatter = 0.55; pads[15].airLoading = 0.0;
    pads[15].bodyDampingB3 = 0.30; pads[15].bodyDampingB1 = 0.40;
    pads[15].modeScatter = 0.85;
    pads[15].noiseLayerMix = 0.55; pads[15].noiseLayerCutoff = 0.78;
    pads[15].noiseLayerColor = 0.62; pads[15].noiseLayerDecay = 0.60;
    pads[15].clickLayerMix = 0.20; pads[15].clickLayerBrightness = 0.70;
    pads[15].outputBus = 1;

    // Wood block (1)
    pads[1].exciterType = ExciterType::Impulse;
    pads[1].bodyModel = BodyModelType::Plate;
    pads[1].material = 0.68; pads[1].size = 0.22; pads[1].decay = 0.20;
    pads[1].level = 0.72;
    pads[1].modeStretch = 0.50;
    pads[1].clickLayerMix = 0.70; pads[1].clickLayerContactMs = 0.12;
    pads[1].clickLayerBrightness = 0.75;
    pads[1].noiseLayerMix = 0.05;
    pads[1].airLoading = 0.10; pads[1].modeScatter = 0.20;
    pads[1].bodyDampingB1 = 0.50; pads[1].bodyDampingB3 = 0.10;

    k.crafted = {0, 1, 2, 4, 5, 7, 9, 11, 12, 14, 6, 8, 10, 13, 15};
    return k;
}
Kit rockBigRoomKit() {
    Kit k{"Rock Big Room", "Acoustic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Big-room kick
    pads[0].exciterType = ExciterType::Mallet;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.35; pads[0].size = 0.95; pads[0].decay = 0.30;
    pads[0].level = 0.90;
    pads[0].tsPitchEnvStart = toLogNorm(180);
    pads[0].tsPitchEnvEnd   = toLogNorm(45);
    pads[0].tsPitchEnvTime  = 0.05;
    pads[0].tsDriveAmount   = 0.30;
    pads[0].airLoading       = 0.85;
    pads[0].couplingStrength = 0.50;
    pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize    = 0.55; pads[0].secondaryMaterial = 0.50;
    pads[0].tensionModAmt    = 0.20;
    pads[0].clickLayerMix       = 0.85; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.50;
    pads[0].noiseLayerMix = 0.08;
    pads[0].bodyDampingB1 = 0.32; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroTightness = 0.55; pads[0].macroBrightness = 0.40;
    pads[0].macroBodySize = 0.85;  pads[0].macroPunch = 0.85;
    pads[0].macroComplexity = 0.40;
    pads[0].couplingAmount = 0.75;

    // Crack snare
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.55; pads[2].size = 0.50; pads[2].decay = 0.35;
    pads[2].level = 0.88;
    pads[2].noiseBurstDuration = 0.40;
    pads[2].tsDriveAmount = 0.22;
    pads[2].tsFilterType = FilterType::BP;
    pads[2].tsFilterCutoff = 0.55;
    pads[2].tsFilterResonance = 0.20;
    pads[2].tsFilterEnvAmount = 0.40;
    pads[2].tsFilterEnvDecay = 0.18;
    pads[2].noiseLayerMix    = 0.70; pads[2].noiseLayerCutoff = 0.78;
    pads[2].noiseLayerResonance = 0.20;
    pads[2].noiseLayerColor  = 0.70; pads[2].noiseLayerDecay = 0.40;
    pads[2].clickLayerMix    = 0.65; pads[2].clickLayerContactMs = 0.12;
    pads[2].clickLayerBrightness = 0.85;
    pads[2].airLoading = 0.55; pads[2].modeScatter = 0.20;
    pads[2].couplingStrength = 0.45; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.55; pads[2].secondaryMaterial = 0.50;
    pads[2].bodyDampingB1 = 0.42; pads[2].bodyDampingB3 = 0.10;
    pads[2].macroPunch = 0.85; pads[2].macroBrightness = 0.70;
    pads[2].macroComplexity = 0.50; pads[2].macroTightness = 0.65;
    pads[2].couplingAmount = 0.70;

    // Rim shot (4)
    pads[4].exciterType = ExciterType::Impulse;
    pads[4].bodyModel   = BodyModelType::Shell;
    pads[4].material = 0.70; pads[4].size = 0.30; pads[4].decay = 0.18;
    pads[4].level = 0.85;
    pads[4].modeStretch = 0.45;
    pads[4].clickLayerMix = 0.95; pads[4].clickLayerContactMs = 0.08;
    pads[4].clickLayerBrightness = 0.92;
    pads[4].noiseLayerMix = 0.20; pads[4].noiseLayerColor = 0.80;
    pads[4].noiseLayerDecay = 0.18;
    pads[4].airLoading = 0.10; pads[4].modeScatter = 0.40;
    pads[4].bodyDampingB1 = 0.50; pads[4].bodyDampingB3 = 0.30;
    pads[4].macroPunch = 0.95;

    // Hi-hats
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::NoiseBody;
    pads[6].material = 0.92; pads[6].size = 0.18; pads[6].decay = 0.12;
    pads[6].level = 0.78; pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix = 0.85; pads[6].noiseLayerCutoff = 0.90;
    pads[6].noiseLayerColor = 0.85; pads[6].noiseLayerDecay = 0.10;
    pads[6].clickLayerMix = 0.22;
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.70;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.50;

    pads[8] = pads[6];
    pads[8].decay = 0.06; pads[8].noiseLayerDecay = 0.07;
    pads[8].bodyDampingB1 = 0.65;

    pads[10] = pads[6];
    pads[10].decay = 0.65; pads[10].noiseLayerDecay = 0.60;
    pads[10].bodyDampingB1 = 0.30;

    // Toms
    const int    tomPads[]      = {5, 7, 9, 11, 12, 14};
    const double tomSizes[]     = {0.92, 0.85, 0.75, 0.65, 0.55, 0.48};
    const double tomMaterial[]  = {0.30, 0.34, 0.38, 0.43, 0.50, 0.58};
    const double tomDecay[]     = {0.65, 0.58, 0.50, 0.43, 0.36, 0.30};
    const double tomPitchHi[]   = {180, 220, 270, 330, 400, 480};
    const double tomPitchLo[]   = {70,  85, 105, 130, 165, 215};
    const double tomB1[]        = {0.26, 0.28, 0.30, 0.33, 0.36, 0.40};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel   = BodyModelType::Membrane;
        pads[p].material = tomMaterial[i];
        pads[p].size     = tomSizes[i];
        pads[p].decay    = tomDecay[i];
        pads[p].level    = 0.85;
        pads[p].tsPitchEnvStart = toLogNorm(tomPitchHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomPitchLo[i]);
        pads[p].tsPitchEnvTime  = 0.08;
        pads[p].tsPitchEnvCurve = 1.0;
        pads[p].tsDriveAmount   = 0.18;
        pads[p].airLoading      = 0.78;
        pads[p].modeScatter     = 0.12;
        pads[p].couplingStrength = 0.55;
        pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize    = 0.40 + 0.02 * i;
        pads[p].secondaryMaterial = 0.55;
        pads[p].tensionModAmt    = 0.30;
        pads[p].noiseLayerMix    = 0.18; pads[p].noiseLayerCutoff = 0.45;
        pads[p].clickLayerMix    = 0.55; pads[p].clickLayerContactMs = 0.30;
        pads[p].clickLayerBrightness = 0.55;
        pads[p].bodyDampingB1 = tomB1[i]; pads[p].bodyDampingB3 = 0.10;
        pads[p].macroPunch = 0.85; pads[p].macroBodySize = 0.55 + 0.05 * i;
        pads[p].couplingAmount = 0.70;
    }

    // Crash 1 (13)
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel = BodyModelType::NoiseBody;
    pads[13].material = 0.95; pads[13].size = 0.45; pads[13].decay = 0.85;
    pads[13].level = 0.78;
    pads[13].modeScatter = 0.55; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.65; pads[13].noiseLayerCutoff = 0.92;
    pads[13].noiseLayerColor = 0.85; pads[13].noiseLayerDecay = 0.75;
    pads[13].clickLayerMix = 0.30; pads[13].clickLayerBrightness = 0.85;
    pads[13].outputBus = 1;
    pads[13].macroBrightness = 0.85;

    // Ride (15)
    pads[15] = pads[13];
    pads[15].size = 0.55; pads[15].decay = 0.95;
    pads[15].bodyModel = BodyModelType::Bell;
    pads[15].fmRatio = 0.30; pads[15].feedbackAmount = 0.05;
    pads[15].clickLayerMix = 0.55; pads[15].clickLayerBrightness = 0.92;
    pads[15].outputBus = 1;

    // Splash (17)
    pads[17] = pads[13];
    pads[17].size = 0.22; pads[17].decay = 0.28;
    pads[17].noiseLayerDecay = 0.25;
    pads[17].outputBus = 1;

    k.opts.maxPolyphony    = 12;
    k.opts.globalCoupling  = 0.30;
    k.opts.snareBuzz       = 0.35;
    k.opts.tomResonance    = 0.45;
    k.opts.couplingDelayMs = 1.2;
    k.crafted = {0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17};
    return k;
}
Kit vintageWoodKit() {
    Kit k{"Vintage Wood", "Acoustic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Wood-shell kick
    pads[0].exciterType = ExciterType::Mallet;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.28; pads[0].size = 0.78; pads[0].decay = 0.27;
    pads[0].level = 0.82;
    pads[0].tsPitchEnvStart = toLogNorm(150);
    pads[0].tsPitchEnvEnd   = toLogNorm(55);
    pads[0].tsPitchEnvTime  = 0.045;
    pads[0].tsDriveAmount   = 0.45;
    pads[0].tsFoldAmount    = 0.10;
    pads[0].airLoading       = 0.70;
    pads[0].couplingStrength = 0.55;
    pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize    = 0.42; pads[0].secondaryMaterial = 0.32;
    pads[0].tensionModAmt    = 0.18;
    pads[0].clickLayerMix       = 0.62; pads[0].clickLayerContactMs = 0.25;
    pads[0].clickLayerBrightness = 0.40;
    pads[0].noiseLayerMix = 0.10;
    pads[0].bodyDampingB1 = 0.34; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroBodySize = 0.70; pads[0].macroPunch = 0.65;
    pads[0].macroBrightness = 0.30; pads[0].macroComplexity = 0.45;

    // Wood-shell snare
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.45; pads[2].size = 0.45; pads[2].decay = 0.32;
    pads[2].level = 0.82;
    pads[2].noiseBurstDuration = 0.35;
    pads[2].tsDriveAmount = 0.32;
    pads[2].tsFilterType = FilterType::HP;
    pads[2].tsFilterCutoff = 0.32;
    pads[2].tsFilterResonance = 0.18;
    pads[2].tsFilterEnvAmount = 0.45;
    pads[2].tsFilterEnvDecay = 0.15;
    pads[2].noiseLayerMix    = 0.55; pads[2].noiseLayerCutoff = 0.62;
    pads[2].noiseLayerColor  = 0.50; pads[2].noiseLayerDecay = 0.32;
    pads[2].clickLayerMix    = 0.70; pads[2].clickLayerContactMs = 0.16;
    pads[2].clickLayerBrightness = 0.70;
    pads[2].airLoading = 0.40; pads[2].modeScatter = 0.30;
    pads[2].couplingStrength = 0.55; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.50; pads[2].secondaryMaterial = 0.32;
    pads[2].bodyDampingB1 = 0.44; pads[2].bodyDampingB3 = 0.10;
    pads[2].macroTightness = 0.70; pads[2].macroBrightness = 0.55;
    pads[2].macroComplexity = 0.55;

    // Side stick (4)
    pads[4].exciterType = ExciterType::Impulse;
    pads[4].bodyModel = BodyModelType::Shell;
    pads[4].material = 0.30; pads[4].size = 0.20; pads[4].decay = 0.18;
    pads[4].level = 0.78;
    pads[4].clickLayerMix = 0.92; pads[4].clickLayerContactMs = 0.10;
    pads[4].clickLayerBrightness = 0.78;
    pads[4].noiseLayerMix = 0.05;
    pads[4].airLoading = 0.0; pads[4].modeScatter = 0.55;
    pads[4].bodyDampingB1 = 0.42; pads[4].bodyDampingB3 = 0.10;
    pads[4].macroComplexity = 0.30;

    // Hi-hats
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::NoiseBody;
    pads[6].material = 0.85; pads[6].size = 0.13; pads[6].decay = 0.08;
    pads[6].level = 0.72; pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix = 0.85; pads[6].noiseLayerCutoff = 0.72;
    pads[6].noiseLayerColor = 0.55; pads[6].noiseLayerDecay = 0.08;
    pads[6].clickLayerMix = 0.20;
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.70;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.55;
    pads[6].macroBrightness = 0.40;

    pads[8] = pads[6];
    pads[8].decay = 0.05; pads[8].noiseLayerDecay = 0.06;

    pads[10] = pads[6];
    pads[10].decay = 0.45; pads[10].noiseLayerDecay = 0.42;
    pads[10].bodyDampingB1 = 0.30;

    // Toms
    const int    tomPads[]      = {5, 7, 9, 11, 12, 14};
    const double tomSizes[]     = {0.72, 0.62, 0.55, 0.48, 0.42, 0.35};
    const double tomMaterial[]  = {0.30, 0.32, 0.36, 0.42, 0.48, 0.55};
    const double tomDecay[]     = {0.42, 0.38, 0.34, 0.30, 0.26, 0.22};
    const double tomB1[]        = {0.30, 0.32, 0.35, 0.38, 0.42, 0.46};
    const double tomPitchHi[]   = {200, 240, 290, 340, 400, 480};
    const double tomPitchLo[]   = {95, 115, 140, 170, 210, 260};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel   = BodyModelType::Membrane;
        pads[p].material = tomMaterial[i];
        pads[p].size     = tomSizes[i];
        pads[p].decay    = tomDecay[i];
        pads[p].level    = 0.78;
        pads[p].tsPitchEnvStart = toLogNorm(tomPitchHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomPitchLo[i]);
        pads[p].tsPitchEnvTime  = 0.10;
        pads[p].tsPitchEnvCurve = 1.0;
        pads[p].tsDriveAmount   = 0.25;
        pads[p].airLoading      = 0.55;
        pads[p].modeScatter     = 0.18;
        pads[p].couplingStrength = 0.50;
        pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize    = 0.32 + 0.02 * i;
        pads[p].secondaryMaterial = 0.30;
        pads[p].tensionModAmt    = 0.22;
        pads[p].noiseLayerMix    = 0.15; pads[p].noiseLayerCutoff = 0.42;
        pads[p].clickLayerMix    = 0.50; pads[p].clickLayerContactMs = 0.32;
        pads[p].clickLayerBrightness = 0.45;
        pads[p].bodyDampingB1 = tomB1[i]; pads[p].bodyDampingB3 = 0.10;
        pads[p].macroBodySize = 0.45 + 0.04 * i;
        pads[p].macroBrightness = 0.35;
    }

    // Crash 1 (13)
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel = BodyModelType::NoiseBody;
    pads[13].material = 0.92; pads[13].size = 0.30; pads[13].decay = 0.65;
    pads[13].level = 0.72;
    pads[13].modeScatter = 0.55; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.55; pads[13].noiseLayerCutoff = 0.78;
    pads[13].noiseLayerColor = 0.65; pads[13].noiseLayerDecay = 0.60;
    pads[13].clickLayerMix = 0.18; pads[13].clickLayerBrightness = 0.65;

    // Wood blocks (1, 3)
    pads[1].exciterType = ExciterType::Impulse;
    pads[1].bodyModel = BodyModelType::Plate;
    pads[1].material = 0.30; pads[1].size = 0.20; pads[1].decay = 0.18;
    pads[1].level = 0.75;
    pads[1].modeStretch = 0.55; pads[1].decaySkew = 0.30;
    pads[1].clickLayerMix = 0.78; pads[1].clickLayerContactMs = 0.10;
    pads[1].clickLayerBrightness = 0.80;
    pads[1].noiseLayerMix = 0.0;
    pads[1].airLoading = 0.0; pads[1].modeScatter = 0.18;
    pads[1].bodyDampingB1 = 0.45; pads[1].bodyDampingB3 = 0.10;

    pads[3] = pads[1];
    pads[3].size = 0.30; pads[3].decay = 0.22;
    pads[3].material = 0.28;
    pads[3].clickLayerBrightness = 0.65;

    // Cowbell (15)
    pads[15].exciterType = ExciterType::Impulse;
    pads[15].bodyModel = BodyModelType::Bell;
    pads[15].material = 0.78; pads[15].size = 0.26; pads[15].decay = 0.30;
    pads[15].level = 0.75;
    pads[15].fmRatio = 0.45;
    pads[15].clickLayerMix = 0.55; pads[15].clickLayerContactMs = 0.10;
    pads[15].clickLayerBrightness = 0.70;
    pads[15].noiseLayerMix = 0.10;
    pads[15].airLoading = 0.05; pads[15].modeScatter = 0.20;
    pads[15].bodyDampingB3 = 0.0; pads[15].bodyDampingB1 = 0.40;
    pads[15].macroBrightness = 0.65;

    k.opts.maxPolyphony    = 10;
    k.opts.globalCoupling  = 0.20;
    k.opts.snareBuzz       = 0.30;
    k.opts.tomResonance    = 0.30;
    k.opts.couplingDelayMs = 1.0;
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    return k;
}
Kit orchestralKit() {
    Kit k{"Orchestral", "Acoustic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Timpani (kick slot)
    pads[0].exciterType = ExciterType::Mallet;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.30; pads[0].size = 0.95; pads[0].decay = 0.85;
    pads[0].level = 0.82;
    pads[0].tsPitchEnvStart = toLogNorm(180);
    pads[0].tsPitchEnvEnd   = toLogNorm(85);
    pads[0].tsPitchEnvTime  = 0.10;
    pads[0].tsPitchEnvCurve = 1.0;
    pads[0].airLoading       = 0.92;
    pads[0].couplingStrength = 0.45;
    pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize    = 0.65; pads[0].secondaryMaterial = 0.50;
    pads[0].tensionModAmt    = 0.55;
    pads[0].clickLayerMix       = 0.32; pads[0].clickLayerContactMs = 0.28;
    pads[0].clickLayerBrightness = 0.30;
    pads[0].noiseLayerMix = 0.12;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroBodySize = 0.95; pads[0].macroComplexity = 0.55;
    pads[0].couplingAmount = 0.85;

    // Bass drum (2)
    pads[2].exciterType = ExciterType::Mallet;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.32; pads[2].size = 0.92; pads[2].decay = 0.55;
    pads[2].level = 0.85;
    pads[2].tsPitchEnvStart = toLogNorm(110);
    pads[2].tsPitchEnvEnd   = toLogNorm(40);
    pads[2].tsPitchEnvTime  = 0.06;
    pads[2].airLoading       = 0.90;
    pads[2].couplingStrength = 0.40;
    pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize    = 0.55; pads[2].secondaryMaterial = 0.45;
    pads[2].tensionModAmt    = 0.20;
    pads[2].clickLayerMix       = 0.30; pads[2].clickLayerContactMs = 0.42;
    pads[2].clickLayerBrightness = 0.20;
    pads[2].noiseLayerMix = 0.08;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.08;
    pads[2].couplingAmount = 0.85;

    // Snare drum (4)
    pads[4].exciterType = ExciterType::NoiseBurst;
    pads[4].bodyModel = BodyModelType::Membrane;
    pads[4].material = 0.55; pads[4].size = 0.42; pads[4].decay = 0.45;
    pads[4].level = 0.82;
    pads[4].noiseBurstDuration = 0.55;
    pads[4].noiseLayerMix    = 0.75; pads[4].noiseLayerCutoff = 0.78;
    pads[4].noiseLayerResonance = 0.20;
    pads[4].noiseLayerColor  = 0.70; pads[4].noiseLayerDecay = 0.50;
    pads[4].clickLayerMix    = 0.55; pads[4].clickLayerContactMs = 0.12;
    pads[4].clickLayerBrightness = 0.78;
    pads[4].airLoading = 0.50; pads[4].modeScatter = 0.25;
    pads[4].couplingStrength = 0.32; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.55; pads[4].secondaryMaterial = 0.50;
    pads[4].bodyDampingB1 = 0.30; pads[4].bodyDampingB3 = 0.10;
    pads[4].macroBrightness = 0.65; pads[4].macroComplexity = 0.55;

    // Timpani toms
    const int    timpaniPads[] = {5, 7, 9, 11, 14};
    const double timpaniSize[] = {0.92, 0.85, 0.78, 0.70, 0.62};
    const double timpaniHi[]   = {180, 220, 280, 350, 440};
    const double timpaniLo[]   = {80, 100, 130, 165, 215};
    const double timpaniDecay[] = {0.80, 0.72, 0.65, 0.58, 0.50};
    const double timpaniB1[]   = {0.10, 0.12, 0.14, 0.17, 0.21};
    for (int i = 0; i < 5; ++i) {
        const int p = timpaniPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel = BodyModelType::Membrane;
        pads[p].material = 0.32 + 0.04 * i;
        pads[p].size = timpaniSize[i];
        pads[p].decay = timpaniDecay[i];
        pads[p].level = 0.80;
        pads[p].tsPitchEnvStart = toLogNorm(timpaniHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(timpaniLo[i]);
        pads[p].tsPitchEnvTime  = 0.12;
        pads[p].tsPitchEnvCurve = 1.0;
        pads[p].airLoading       = 0.85;
        pads[p].modeScatter      = 0.08;
        pads[p].couplingStrength = 0.40;
        pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize    = 0.45 + 0.02 * i;
        pads[p].secondaryMaterial = 0.50;
        pads[p].tensionModAmt    = 0.40;
        pads[p].noiseLayerMix    = 0.12; pads[p].noiseLayerCutoff = 0.40;
        pads[p].clickLayerMix    = 0.32; pads[p].clickLayerContactMs = 0.28;
        pads[p].clickLayerBrightness = 0.32;
        pads[p].bodyDampingB1 = timpaniB1[i]; pads[p].bodyDampingB3 = 0.10;
        pads[p].macroBodySize = 0.85 - 0.05 * i;
        pads[p].macroComplexity = 0.55;
        pads[p].couplingAmount = 0.80;
    }

    // Triangle (12)
    pads[12].exciterType = ExciterType::Impulse;
    pads[12].bodyModel = BodyModelType::Bell;
    pads[12].material = 0.95; pads[12].size = 0.10; pads[12].decay = 0.85;
    pads[12].level = 0.65;
    pads[12].fmRatio = 0.45;
    pads[12].modeStretch = 0.55;
    pads[12].clickLayerMix = 0.55; pads[12].clickLayerContactMs = 0.08;
    pads[12].clickLayerBrightness = 0.95;
    pads[12].noiseLayerMix = 0.0;
    pads[12].airLoading = 0.0;
    pads[12].bodyDampingB3 = 0.0; pads[12].bodyDampingB1 = 0.30;
    pads[12].macroBrightness = 0.95; pads[12].macroComplexity = 0.30;

    // Hi-hats / suspended cymbal
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::NoiseBody;
    pads[6].material = 0.88; pads[6].size = 0.16; pads[6].decay = 0.15;
    pads[6].level = 0.70; pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix = 0.65; pads[6].noiseLayerCutoff = 0.82;
    pads[6].noiseLayerColor = 0.70; pads[6].noiseLayerDecay = 0.12;
    pads[6].clickLayerMix = 0.20;
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.30;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.45;

    pads[8] = pads[6];
    pads[8].decay = 0.06; pads[8].noiseLayerDecay = 0.07;
    pads[8].bodyDampingB1 = 0.65;

    // Suspended cymbal roll (10)
    pads[10].exciterType = ExciterType::NoiseBurst;
    pads[10].bodyModel = BodyModelType::NoiseBody;
    pads[10].material = 0.95; pads[10].size = 0.42; pads[10].decay = 0.95;
    pads[10].level = 0.72;
    pads[10].morphEnabled = 1.0;
    pads[10].morphStart = 0.55; pads[10].morphEnd = 0.95;
    pads[10].morphDuration = 0.85; pads[10].morphCurve = 0.4;
    pads[10].modeScatter = 0.65; pads[10].airLoading = 0.0;
    pads[10].bodyDampingB3 = 0.0; pads[10].bodyDampingB1 = 0.30;
    pads[10].noiseLayerMix = 0.65; pads[10].noiseLayerCutoff = 0.82;
    pads[10].noiseLayerColor = 0.78; pads[10].noiseLayerDecay = 0.95;
    pads[10].clickLayerMix = 0.0;
    pads[10].decaySkew = 0.55;
    pads[10].outputBus = 1;

    // Gong (13)
    pads[13].exciterType = ExciterType::Mallet;
    pads[13].bodyModel = BodyModelType::Bell;
    pads[13].material = 0.85; pads[13].size = 0.85; pads[13].decay = 0.95;
    pads[13].level = 0.78;
    pads[13].fmRatio = 0.30; pads[13].feedbackAmount = 0.10;
    pads[13].modeStretch = 0.55;
    pads[13].morphEnabled = 1.0;
    pads[13].morphStart = 0.85; pads[13].morphEnd = 0.55;
    pads[13].morphDuration = 0.85; pads[13].morphCurve = 0.5;
    pads[13].modeScatter = 0.65; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].clickLayerMix = 0.30; pads[13].clickLayerContactMs = 0.22;
    pads[13].clickLayerBrightness = 0.30;
    pads[13].noiseLayerMix = 0.20; pads[13].noiseLayerCutoff = 0.55;
    pads[13].noiseLayerColor = 0.45; pads[13].noiseLayerDecay = 0.92;
    pads[13].decaySkew = 0.65;
    pads[13].tensionModAmt = 0.22;
    pads[13].outputBus = 1;
    pads[13].macroBodySize = 0.95; pads[13].macroComplexity = 0.85;
    pads[13].couplingAmount = 0.95;

    // Crotales hi (15)
    pads[15].exciterType = ExciterType::Mallet;
    pads[15].bodyModel = BodyModelType::Bell;
    pads[15].material = 0.92; pads[15].size = 0.18; pads[15].decay = 0.85;
    pads[15].level = 0.72;
    pads[15].fmRatio = 0.55;
    pads[15].clickLayerMix = 0.40; pads[15].clickLayerBrightness = 0.85;
    pads[15].noiseLayerMix = 0.0;
    pads[15].airLoading = 0.0;
    pads[15].bodyDampingB3 = 0.0; pads[15].bodyDampingB1 = 0.30;
    pads[15].outputBus = 1;
    pads[15].macroBrightness = 0.85;

    // Crotales lo (17)
    pads[17] = pads[15];
    pads[17].size = 0.25; pads[17].fmRatio = 0.40;
    pads[17].material = 0.88;

    // Tubular bell (3)
    pads[3].exciterType = ExciterType::Mallet;
    pads[3].bodyModel = BodyModelType::String;
    pads[3].material = 0.85; pads[3].size = 0.55; pads[3].decay = 0.92;
    pads[3].level = 0.72;
    pads[3].modeStretch = 0.50;
    pads[3].clickLayerMix = 0.40; pads[3].clickLayerContactMs = 0.20;
    pads[3].clickLayerBrightness = 0.65;
    pads[3].noiseLayerMix = 0.10;
    pads[3].airLoading = 0.0;
    pads[3].bodyDampingB1 = 0.30; pads[3].bodyDampingB3 = 0.20;
    pads[3].decaySkew = 0.55;

    k.opts.maxPolyphony    = 16;
    k.opts.globalCoupling  = 0.45;
    k.opts.snareBuzz       = 0.20;
    k.opts.tomResonance    = 0.55;
    k.opts.couplingDelayMs = 1.6;
    k.crafted = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17};
    return k;
}
Kit nineOhNineKit() {
    Kit k{"909 Drum Machine", "Electronic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // 909 kick
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.18; pads[0].size = 0.78; pads[0].decay = 0.22;
    pads[0].level = 0.85;
    pads[0].tsPitchEnvStart = toLogNorm(180);
    pads[0].tsPitchEnvEnd   = toLogNorm(55);
    pads[0].tsPitchEnvTime  = 0.04;
    pads[0].tsDriveAmount   = 0.20;
    pads[0].airLoading       = 0.0;
    pads[0].couplingStrength = 0.0;
    pads[0].secondaryEnabled = 0.0;
    pads[0].tensionModAmt    = 0.10;
    pads[0].clickLayerMix       = 0.55; pads[0].clickLayerContactMs = 0.12;
    pads[0].clickLayerBrightness = 0.55;
    pads[0].noiseLayerMix = 0.05;
    pads[0].bodyDampingB1 = 0.42; pads[0].bodyDampingB3 = 0.30;
    pads[0].macroPunch = 0.85; pads[0].macroBodySize = 0.45;

    // 909 snare
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.62; pads[2].size = 0.38; pads[2].decay = 0.20;
    pads[2].level = 0.82;
    pads[2].noiseBurstDuration = 0.30;
    pads[2].noiseLayerMix    = 0.75; pads[2].noiseLayerCutoff = 0.92;
    pads[2].noiseLayerColor  = 0.92; pads[2].noiseLayerDecay = 0.22;
    pads[2].clickLayerMix    = 0.40; pads[2].clickLayerContactMs = 0.10;
    pads[2].clickLayerBrightness = 0.92;
    pads[2].airLoading = 0.0; pads[2].modeScatter = 0.0;
    pads[2].bodyDampingB1 = 0.45; pads[2].bodyDampingB3 = 0.30;
    pads[2].macroBrightness = 0.85; pads[2].macroPunch = 0.65;

    // Rim shot (4)
    pads[4].exciterType = ExciterType::Impulse;
    pads[4].bodyModel = BodyModelType::Plate;
    pads[4].material = 0.75; pads[4].size = 0.18; pads[4].decay = 0.10;
    pads[4].level = 0.78;
    pads[4].clickLayerMix = 0.92; pads[4].clickLayerBrightness = 0.95;
    pads[4].clickLayerContactMs = 0.08;
    pads[4].noiseLayerMix = 0.0;
    pads[4].bodyDampingB1 = 0.55; pads[4].bodyDampingB3 = 0.0;

    // Hi-hats
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::NoiseBody;
    pads[6].material = 0.95; pads[6].size = 0.10; pads[6].decay = 0.07;
    pads[6].level = 0.72; pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix = 0.85; pads[6].noiseLayerCutoff = 0.95;
    pads[6].noiseLayerColor = 0.95; pads[6].noiseLayerDecay = 0.07;
    pads[6].clickLayerMix = 0.0;
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.0;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.65;

    pads[8] = pads[6];
    pads[8].decay = 0.04; pads[8].noiseLayerDecay = 0.05;
    pads[8].bodyDampingB1 = 0.78;

    pads[10] = pads[6];
    pads[10].decay = 0.42; pads[10].noiseLayerDecay = 0.40;
    pads[10].bodyDampingB1 = 0.30;

    // Toms
    const int    tomPads[]      = {5, 7, 9, 11, 12, 14};
    const double tomSizes[]     = {0.65, 0.55, 0.48, 0.40, 0.34, 0.28};
    const double tomMaterial[]  = {0.20, 0.25, 0.30, 0.36, 0.42, 0.50};
    const double tomDecay[]     = {0.32, 0.28, 0.25, 0.22, 0.20, 0.18};
    const double tomPitchHi[]   = {240, 290, 350, 420, 500, 590};
    const double tomPitchLo[]   = {85, 100, 120, 145, 175, 210};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Impulse;
        pads[p].bodyModel = BodyModelType::Membrane;
        pads[p].material = tomMaterial[i];
        pads[p].size = tomSizes[i];
        pads[p].decay = tomDecay[i];
        pads[p].level = 0.78;
        pads[p].tsPitchEnvStart = toLogNorm(tomPitchHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomPitchLo[i]);
        pads[p].tsPitchEnvTime  = 0.10;
        pads[p].tsPitchEnvCurve = 0.0;
        pads[p].airLoading = 0.0;
        pads[p].couplingStrength = 0.0;
        pads[p].secondaryEnabled = 0.0;
        pads[p].tensionModAmt = 0.20;
        pads[p].noiseLayerMix = 0.05;
        pads[p].clickLayerMix = 0.32; pads[p].clickLayerContactMs = 0.15;
        pads[p].clickLayerBrightness = 0.55;
        pads[p].bodyDampingB1 = 0.30; pads[p].bodyDampingB3 = 0.18;
        pads[p].macroPunch = 0.65;
    }

    // Crash (13)
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel = BodyModelType::NoiseBody;
    pads[13].material = 0.95; pads[13].size = 0.32; pads[13].decay = 0.55;
    pads[13].level = 0.72;
    pads[13].modeScatter = 0.50; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.65; pads[13].noiseLayerCutoff = 0.95;
    pads[13].noiseLayerColor = 0.92; pads[13].noiseLayerDecay = 0.55;

    // Clap (15)
    pads[15].exciterType = ExciterType::NoiseBurst;
    pads[15].bodyModel = BodyModelType::NoiseBody;
    pads[15].material = 0.85; pads[15].size = 0.18; pads[15].decay = 0.18;
    pads[15].level = 0.78;
    pads[15].noiseBurstDuration = 0.55;
    pads[15].noiseLayerMix = 0.85; pads[15].noiseLayerCutoff = 0.78;
    pads[15].noiseLayerResonance = 0.40;
    pads[15].noiseLayerColor = 0.65; pads[15].noiseLayerDecay = 0.20;
    pads[15].clickLayerMix = 0.45; pads[15].clickLayerContactMs = 0.22;
    pads[15].clickLayerBrightness = 0.62;
    pads[15].airLoading = 0.0; pads[15].modeScatter = 0.40;
    pads[15].bodyDampingB1 = 0.50; pads[15].bodyDampingB3 = 0.0;
    pads[15].macroBrightness = 0.65; pads[15].macroComplexity = 0.55;

    k.crafted = {0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    return k;
}

Kit linnDrumKit() {
    Kit k{"LinnDrum CR-78", "Electronic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // LinnDrum kick
    pads[0].exciterType = ExciterType::FMImpulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.20; pads[0].size = 0.72; pads[0].decay = 0.28;
    pads[0].level = 0.85;
    pads[0].fmRatio = 0.30;
    pads[0].tsPitchEnvStart = toLogNorm(160);
    pads[0].tsPitchEnvEnd   = toLogNorm(50);
    pads[0].tsPitchEnvTime  = 0.04;
    pads[0].tsFoldAmount = 0.18;
    pads[0].airLoading = 0.0; pads[0].couplingStrength = 0.0;
    pads[0].clickLayerMix = 0.40; pads[0].clickLayerContactMs = 0.15;
    pads[0].clickLayerBrightness = 0.45;
    pads[0].noiseLayerMix = 0.06;
    pads[0].bodyDampingB1 = 0.38; pads[0].bodyDampingB3 = 0.30;
    pads[0].macroPunch = 0.55; pads[0].macroBodySize = 0.45;

    // LinnDrum snare
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.58; pads[2].size = 0.38; pads[2].decay = 0.22;
    pads[2].level = 0.80;
    pads[2].noiseBurstDuration = 0.32;
    pads[2].noiseLayerMix = 0.62; pads[2].noiseLayerCutoff = 0.78;
    pads[2].noiseLayerColor = 0.72; pads[2].noiseLayerDecay = 0.22;
    pads[2].clickLayerMix = 0.42; pads[2].clickLayerContactMs = 0.12;
    pads[2].clickLayerBrightness = 0.78;
    pads[2].airLoading = 0.0; pads[2].modeScatter = 0.05;
    pads[2].bodyDampingB1 = 0.40; pads[2].bodyDampingB3 = 0.30;

    // CR-78 cowbell (4)
    pads[4].exciterType = ExciterType::FMImpulse;
    pads[4].bodyModel = BodyModelType::Bell;
    pads[4].material = 0.78; pads[4].size = 0.22; pads[4].decay = 0.30;
    pads[4].level = 0.75;
    pads[4].fmRatio = 0.55;
    pads[4].clickLayerMix = 0.30; pads[4].clickLayerBrightness = 0.62;
    pads[4].noiseLayerMix = 0.08;
    pads[4].airLoading = 0.0;
    pads[4].bodyDampingB3 = 0.0; pads[4].bodyDampingB1 = 0.32;
    pads[4].macroBrightness = 0.65;

    // Hats
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::NoiseBody;
    pads[6].material = 0.78; pads[6].size = 0.12; pads[6].decay = 0.10;
    pads[6].level = 0.70; pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix = 0.78; pads[6].noiseLayerCutoff = 0.65;
    pads[6].noiseLayerColor = 0.50; pads[6].noiseLayerDecay = 0.10;
    pads[6].clickLayerMix = 0.18;
    pads[6].airLoading = 0.0;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.55;

    pads[8] = pads[6];
    pads[8].decay = 0.06; pads[8].noiseLayerDecay = 0.07;

    pads[10] = pads[6];
    pads[10].decay = 0.40; pads[10].noiseLayerDecay = 0.38;
    pads[10].bodyDampingB1 = 0.30;

    // Toms
    const int    tomPads[]  = {5, 7, 9, 11, 12, 14};
    const double tomSizes[] = {0.62, 0.55, 0.48, 0.42, 0.36, 0.30};
    const double tomMat[]   = {0.25, 0.30, 0.36, 0.42, 0.50, 0.58};
    const double tomHi[]    = {220, 270, 330, 400, 480, 570};
    const double tomLo[]    = {90, 110, 135, 165, 200, 240};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel = BodyModelType::Membrane;
        pads[p].material = tomMat[i]; pads[p].size = tomSizes[i];
        pads[p].decay = 0.30 - 0.02 * i; pads[p].level = 0.75;
        pads[p].tsPitchEnvStart = toLogNorm(tomHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomLo[i]);
        pads[p].tsPitchEnvTime  = 0.08;
        pads[p].tsPitchEnvCurve = 0.0;
        pads[p].airLoading = 0.0; pads[p].modeScatter = 0.0;
        pads[p].tensionModAmt = 0.20;
        pads[p].noiseLayerMix = 0.06;
        pads[p].clickLayerMix = 0.32; pads[p].clickLayerBrightness = 0.55;
        pads[p].bodyDampingB1 = 0.32; pads[p].bodyDampingB3 = 0.20;
    }

    // Cabasa (1)
    pads[1].exciterType = ExciterType::NoiseBurst;
    pads[1].bodyModel = BodyModelType::NoiseBody;
    pads[1].material = 0.88; pads[1].size = 0.08; pads[1].decay = 0.08;
    pads[1].level = 0.65;
    pads[1].noiseBurstDuration = 0.18;
    pads[1].noiseLayerMix = 0.85; pads[1].noiseLayerCutoff = 0.85;
    pads[1].noiseLayerColor = 0.75; pads[1].noiseLayerDecay = 0.10;
    pads[1].clickLayerMix = 0.0;
    pads[1].airLoading = 0.0; pads[1].modeScatter = 0.0;
    pads[1].bodyDampingB3 = 0.0; pads[1].bodyDampingB1 = 0.55;

    // Clave (3)
    pads[3].exciterType = ExciterType::FMImpulse;
    pads[3].bodyModel = BodyModelType::Bell;
    pads[3].material = 0.85; pads[3].size = 0.12; pads[3].decay = 0.18;
    pads[3].level = 0.75;
    pads[3].fmRatio = 0.62;
    pads[3].clickLayerMix = 0.65; pads[3].clickLayerBrightness = 0.85;
    pads[3].noiseLayerMix = 0.0;
    pads[3].airLoading = 0.0;
    pads[3].bodyDampingB3 = 0.0; pads[3].bodyDampingB1 = 0.40;

    // Crash (13)
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel = BodyModelType::NoiseBody;
    pads[13].material = 0.92; pads[13].size = 0.30; pads[13].decay = 0.55;
    pads[13].level = 0.70;
    pads[13].modeScatter = 0.45; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.55; pads[13].noiseLayerCutoff = 0.78;
    pads[13].noiseLayerColor = 0.62; pads[13].noiseLayerDecay = 0.55;

    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    return k;
}

Kit modularWestCoastKit() {
    Kit k{"Modular West Coast", "Electronic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // West Coast kick
    pads[0].exciterType = ExciterType::Feedback;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.25; pads[0].size = 0.85; pads[0].decay = 0.40;
    pads[0].level = 0.80;
    pads[0].feedbackAmount = 0.45;
    pads[0].tsPitchEnvStart = toLogNorm(220);
    pads[0].tsPitchEnvEnd   = toLogNorm(45);
    pads[0].tsPitchEnvTime  = 0.06;
    pads[0].tsFoldAmount    = 0.30;
    pads[0].airLoading = 0.0;
    pads[0].couplingStrength = 0.20; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.45; pads[0].secondaryMaterial = 0.85;
    pads[0].tensionModAmt = 0.45;
    pads[0].clickLayerMix = 0.30; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.55;
    pads[0].noiseLayerMix = 0.20;
    pads[0].nonlinearCoupling = 0.40;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.30;
    pads[0].macroComplexity = 0.85; pads[0].macroPunch = 0.65;

    // Snare (FM Plate)
    pads[2].exciterType = ExciterType::FMImpulse;
    pads[2].bodyModel = BodyModelType::Plate;
    pads[2].material = 0.55; pads[2].size = 0.40; pads[2].decay = 0.35;
    pads[2].level = 0.78;
    pads[2].fmRatio = 0.55;
    pads[2].modeStretch = 0.55;
    pads[2].modeInjectAmount = 0.0;
    pads[2].nonlinearCoupling = 0.45;
    pads[2].morphEnabled = 1.0;
    pads[2].morphStart = 0.55; pads[2].morphEnd = 0.80;
    pads[2].morphDuration = 0.30; pads[2].morphCurve = 0.6;
    pads[2].noiseLayerMix = 0.30; pads[2].noiseLayerCutoff = 0.65;
    pads[2].noiseLayerColor = 0.62; pads[2].noiseLayerDecay = 0.30;
    pads[2].clickLayerMix = 0.45; pads[2].clickLayerBrightness = 0.78;
    pads[2].airLoading = 0.0; pads[2].modeScatter = 0.45;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.30;

    // Sub-bell perc (4)
    pads[4].exciterType = ExciterType::FMImpulse;
    pads[4].bodyModel = BodyModelType::Bell;
    pads[4].material = 0.55; pads[4].size = 0.32; pads[4].decay = 0.55;
    pads[4].level = 0.75;
    pads[4].fmRatio = 0.72; pads[4].feedbackAmount = 0.30;
    pads[4].modeStretch = 0.50;
    pads[4].nonlinearCoupling = 0.30;
    pads[4].clickLayerMix = 0.45; pads[4].clickLayerBrightness = 0.85;
    pads[4].noiseLayerMix = 0.10;
    pads[4].airLoading = 0.0;
    pads[4].bodyDampingB3 = 0.0; pads[4].bodyDampingB1 = 0.30;
    pads[4].decaySkew = 0.45;

    // Friction string drone (1)
    pads[1].exciterType = ExciterType::Friction;
    pads[1].bodyModel = BodyModelType::String;
    pads[1].material = 0.50; pads[1].size = 0.55; pads[1].decay = 0.85;
    pads[1].level = 0.65;
    pads[1].frictionPressure = 0.55;
    pads[1].modeStretch = 0.40;
    pads[1].nonlinearCoupling = 0.55;
    pads[1].morphEnabled = 1.0;
    pads[1].morphStart = 0.40; pads[1].morphEnd = 0.85;
    pads[1].morphDuration = 0.55; pads[1].morphCurve = 0.5;
    pads[1].decaySkew = 0.65;
    pads[1].noiseLayerMix = 0.20; pads[1].noiseLayerCutoff = 0.45;
    pads[1].clickLayerMix = 0.0;
    pads[1].bodyDampingB1 = 0.30; pads[1].bodyDampingB3 = 0.20;
    pads[1].outputBus = 1;
    pads[1].macroComplexity = 0.85;

    // Hats: FM bell + scatter
    pads[6].exciterType = ExciterType::FMImpulse;
    pads[6].bodyModel = BodyModelType::Bell;
    pads[6].material = 0.92; pads[6].size = 0.10; pads[6].decay = 0.08;
    pads[6].level = 0.68; pads[6].chokeGroup = 1;
    pads[6].fmRatio = 0.78;
    pads[6].modeScatter = 0.55;
    pads[6].noiseLayerMix = 0.40; pads[6].noiseLayerCutoff = 0.92;
    pads[6].noiseLayerColor = 0.85; pads[6].noiseLayerDecay = 0.08;
    pads[6].clickLayerMix = 0.25; pads[6].clickLayerBrightness = 0.92;
    pads[6].airLoading = 0.0;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.55;

    pads[8] = pads[6];
    pads[8].decay = 0.05; pads[8].fmRatio = 0.65;

    pads[10] = pads[6];
    pads[10].decay = 0.50; pads[10].fmRatio = 0.55;
    pads[10].noiseLayerDecay = 0.48;

    // Toms (inharmonic plates)
    const int tomPads[] = {5, 7, 9, 11, 12, 14};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = (i % 2 == 0) ? ExciterType::FMImpulse : ExciterType::Feedback;
        pads[p].bodyModel = BodyModelType::Plate;
        pads[p].material = 0.40 + i * 0.06;
        pads[p].size = 0.85 - i * 0.10;
        pads[p].decay = 0.65 - i * 0.05;
        pads[p].level = 0.75;
        pads[p].fmRatio = 0.30 + i * 0.08;
        pads[p].feedbackAmount = 0.20 + (i % 3) * 0.10;
        pads[p].modeStretch = 0.30 + i * 0.05;
        pads[p].modeInjectAmount = 0.0;
        pads[p].nonlinearCoupling = 0.40;
        pads[p].decaySkew = 0.50;
        pads[p].tsPitchEnvStart = toLogNorm(280 + i * 60);
        pads[p].tsPitchEnvEnd   = toLogNorm(70 + i * 25);
        pads[p].tsPitchEnvTime  = 0.10;
        pads[p].airLoading = 0.0;
        pads[p].modeScatter = 0.30 + (i % 4) * 0.10;
        pads[p].couplingStrength = 0.30; pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize = 0.40; pads[p].secondaryMaterial = 0.75;
        pads[p].tensionModAmt = 0.45;
        pads[p].noiseLayerMix = 0.18; pads[p].noiseLayerColor = 0.70;
        pads[p].clickLayerMix = 0.30; pads[p].clickLayerBrightness = 0.65;
        pads[p].bodyDampingB1 = 0.30 + 0.03 * i; pads[p].bodyDampingB3 = 0.30;
        pads[p].macroComplexity = 0.75;
        pads[p].couplingAmount = 0.65;
    }

    // Crash (13)
    pads[13].exciterType = ExciterType::FMImpulse;
    pads[13].bodyModel = BodyModelType::Bell;
    pads[13].material = 0.92; pads[13].size = 0.45; pads[13].decay = 0.78;
    pads[13].level = 0.72;
    pads[13].fmRatio = 0.45;
    pads[13].modeStretch = 0.55;
    pads[13].modeScatter = 0.65;
    pads[13].nonlinearCoupling = 0.45;
    pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.40; pads[13].noiseLayerCutoff = 0.92;
    pads[13].noiseLayerColor = 0.85; pads[13].noiseLayerDecay = 0.72;
    pads[13].clickLayerMix = 0.35; pads[13].clickLayerBrightness = 0.85;

    k.opts.maxPolyphony    = 12;
    k.opts.stealingPolicy  = 1;
    k.opts.globalCoupling  = 0.65;
    k.opts.tomResonance    = 0.55;
    k.opts.couplingDelayMs = 1.4;
    k.crafted = {0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    return k;
}

Kit trapModernKit() {
    Kit k{"Trap Modern", "Electronic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Sub 808 kick
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.10; pads[0].size = 0.95; pads[0].decay = 0.65;
    pads[0].level = 0.92;
    pads[0].tsPitchEnvStart = toLogNorm(250);
    pads[0].tsPitchEnvEnd   = toLogNorm(35);
    pads[0].tsPitchEnvTime  = 0.06;
    pads[0].tsDriveAmount   = 0.18;
    pads[0].airLoading = 0.0; pads[0].couplingStrength = 0.0;
    pads[0].tensionModAmt = 0.85;
    pads[0].clickLayerMix = 0.42; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.32;
    pads[0].noiseLayerMix = 0.0;
    pads[0].decaySkew = 0.45;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.30;
    pads[0].macroPunch = 0.95; pads[0].macroBodySize = 0.95;

    // Modern crispy snare
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.65; pads[2].size = 0.42; pads[2].decay = 0.30;
    pads[2].level = 0.88;
    pads[2].noiseBurstDuration = 0.40;
    pads[2].tsFilterType = FilterType::HP;
    pads[2].tsFilterCutoff = 0.45;
    pads[2].tsFilterResonance = 0.30;
    pads[2].tsFilterEnvAmount = 0.55;
    pads[2].tsFilterEnvDecay = 0.18;
    pads[2].tsDriveAmount = 0.20;
    pads[2].noiseLayerMix = 0.78; pads[2].noiseLayerCutoff = 0.92;
    pads[2].noiseLayerResonance = 0.30;
    pads[2].noiseLayerColor = 0.95; pads[2].noiseLayerDecay = 0.32;
    pads[2].clickLayerMix = 0.85; pads[2].clickLayerContactMs = 0.10;
    pads[2].clickLayerBrightness = 0.95;
    pads[2].airLoading = 0.0; pads[2].modeScatter = 0.20;
    pads[2].bodyDampingB1 = 0.32; pads[2].bodyDampingB3 = 0.30;
    pads[2].macroBrightness = 0.95; pads[2].macroPunch = 0.85;
    pads[2].macroTightness = 0.85;

    // Snare layer rim shot (4)
    pads[4].exciterType = ExciterType::Impulse;
    pads[4].bodyModel = BodyModelType::Plate;
    pads[4].material = 0.78; pads[4].size = 0.16; pads[4].decay = 0.10;
    pads[4].level = 0.85;
    pads[4].clickLayerMix = 0.95; pads[4].clickLayerContactMs = 0.06;
    pads[4].clickLayerBrightness = 0.95;
    pads[4].noiseLayerMix = 0.18; pads[4].noiseLayerColor = 0.92;
    pads[4].airLoading = 0.0; pads[4].modeScatter = 0.20;
    pads[4].bodyDampingB1 = 0.50; pads[4].bodyDampingB3 = 0.0;

    // Closed hat variants (6, 7, 8)
    const int hatPads[] = {6, 7, 8};
    const double hatDecays[] = {0.05, 0.08, 0.12};
    for (int i = 0; i < 3; ++i) {
        const int p = hatPads[i];
        pads[p].exciterType = ExciterType::NoiseBurst;
        pads[p].bodyModel = BodyModelType::NoiseBody;
        pads[p].material = 0.92; pads[p].size = 0.10;
        pads[p].decay = hatDecays[i]; pads[p].level = 0.72;
        pads[p].chokeGroup = 1;
        pads[p].noiseLayerMix = 0.85; pads[p].noiseLayerCutoff = 0.95;
        pads[p].noiseLayerColor = 0.95;
        pads[p].noiseLayerDecay = hatDecays[i];
        pads[p].noiseLayerResonance = 0.10;
        pads[p].clickLayerMix = 0.18; pads[p].clickLayerBrightness = 0.92;
        pads[p].airLoading = 0.0; pads[p].modeScatter = 0.25;
        pads[p].bodyDampingB3 = 0.0;
        pads[p].bodyDampingB1 = 0.78 - 0.10 * i;
    }

    // Open hat (10)
    pads[10].exciterType = ExciterType::NoiseBurst;
    pads[10].bodyModel = BodyModelType::NoiseBody;
    pads[10].material = 0.92; pads[10].size = 0.18; pads[10].decay = 0.55;
    pads[10].level = 0.72; pads[10].chokeGroup = 1;
    pads[10].noiseLayerMix = 0.78; pads[10].noiseLayerCutoff = 0.92;
    pads[10].noiseLayerColor = 0.92; pads[10].noiseLayerDecay = 0.50;
    pads[10].modeScatter = 0.30;
    pads[10].bodyDampingB3 = 0.0; pads[10].bodyDampingB1 = 0.30;

    // Toms
    const int    tomPads[]  = {5, 9, 11, 12, 14};
    const double tomSizes[] = {0.65, 0.55, 0.45, 0.38, 0.32};
    const double tomMat[]   = {0.20, 0.28, 0.36, 0.45, 0.55};
    const double tomHi[]    = {240, 290, 360, 440, 540};
    const double tomLo[]    = {80, 100, 130, 165, 210};
    for (int i = 0; i < 5; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Impulse;
        pads[p].bodyModel = BodyModelType::Membrane;
        pads[p].material = tomMat[i]; pads[p].size = tomSizes[i];
        pads[p].decay = 0.30 - 0.03 * i; pads[p].level = 0.78;
        pads[p].tsPitchEnvStart = toLogNorm(tomHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomLo[i]);
        pads[p].tsPitchEnvTime  = 0.06;
        pads[p].tsPitchEnvCurve = 0.0;
        pads[p].tsDriveAmount   = 0.18;
        pads[p].airLoading = 0.0;
        pads[p].tensionModAmt = 0.55;
        pads[p].noiseLayerMix = 0.05;
        pads[p].clickLayerMix = 0.45; pads[p].clickLayerBrightness = 0.78;
        pads[p].bodyDampingB1 = 0.30 + 0.03 * i; pads[p].bodyDampingB3 = 0.30;
        pads[p].macroPunch = 0.78;
    }

    // Trap clave (1)
    pads[1].exciterType = ExciterType::Impulse;
    pads[1].bodyModel = BodyModelType::Bell;
    pads[1].material = 0.92; pads[1].size = 0.10; pads[1].decay = 0.15;
    pads[1].level = 0.78;
    pads[1].fmRatio = 0.65;
    pads[1].clickLayerMix = 0.85; pads[1].clickLayerBrightness = 0.95;
    pads[1].clickLayerContactMs = 0.06;
    pads[1].noiseLayerMix = 0.0;
    pads[1].airLoading = 0.0;
    pads[1].bodyDampingB3 = 0.0; pads[1].bodyDampingB1 = 0.42;

    // Crash (13)
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel = BodyModelType::NoiseBody;
    pads[13].material = 0.95; pads[13].size = 0.32; pads[13].decay = 0.72;
    pads[13].level = 0.72;
    pads[13].modeScatter = 0.65; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.78; pads[13].noiseLayerCutoff = 0.95;
    pads[13].noiseLayerColor = 0.92; pads[13].noiseLayerDecay = 0.70;

    k.opts.maxPolyphony   = 16;
    k.opts.stealingPolicy = 2;
    k.crafted = {0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    return k;
}
Kit handDrumsKit() {
    Kit k{"Hand Drums", "Percussive", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Conga lo (0)
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.45; pads[0].size = 0.62; pads[0].decay = 0.40;
    pads[0].level = 0.80; pads[0].strikePosition = 0.45;
    pads[0].tsPitchEnvStart = toLogNorm(200);
    pads[0].tsPitchEnvEnd   = toLogNorm(150);
    pads[0].tsPitchEnvTime  = 0.04;
    pads[0].airLoading = 0.55; pads[0].modeScatter = 0.10;
    pads[0].couplingStrength = 0.32; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.48; pads[0].secondaryMaterial = 0.40;
    pads[0].tensionModAmt = 0.20;
    pads[0].clickLayerMix = 0.50; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.55;
    pads[0].noiseLayerMix = 0.10; pads[0].noiseLayerColor = 0.40;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroBodySize = 0.55;

    // Conga hi (2)
    pads[2].exciterType = ExciterType::Impulse;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.50; pads[2].size = 0.50; pads[2].decay = 0.32;
    pads[2].level = 0.80; pads[2].strikePosition = 0.30;
    pads[2].tsPitchEnvStart = toLogNorm(280);
    pads[2].tsPitchEnvEnd   = toLogNorm(210);
    pads[2].tsPitchEnvTime  = 0.04;
    pads[2].airLoading = 0.50; pads[2].modeScatter = 0.10;
    pads[2].couplingStrength = 0.28; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.40; pads[2].secondaryMaterial = 0.40;
    pads[2].tensionModAmt = 0.18;
    pads[2].clickLayerMix = 0.55; pads[2].clickLayerContactMs = 0.16;
    pads[2].clickLayerBrightness = 0.65;
    pads[2].noiseLayerMix = 0.12; pads[2].noiseLayerColor = 0.45;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.10;

    // Conga slap (4)
    pads[4].exciterType = ExciterType::Impulse;
    pads[4].bodyModel = BodyModelType::Membrane;
    pads[4].material = 0.55; pads[4].size = 0.50; pads[4].decay = 0.18;
    pads[4].level = 0.85; pads[4].strikePosition = 0.10;
    pads[4].airLoading = 0.40; pads[4].modeScatter = 0.30;
    pads[4].couplingStrength = 0.20; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.40; pads[4].secondaryMaterial = 0.40;
    pads[4].clickLayerMix = 0.85; pads[4].clickLayerContactMs = 0.10;
    pads[4].clickLayerBrightness = 0.85;
    pads[4].noiseLayerMix = 0.15;
    pads[4].bodyDampingB1 = 0.45; pads[4].bodyDampingB3 = 0.10;
    pads[4].macroPunch = 0.85;

    // Bongo hi (6) / lo (8)
    pads[6].exciterType = ExciterType::Impulse;
    pads[6].bodyModel = BodyModelType::Membrane;
    pads[6].material = 0.55; pads[6].size = 0.32; pads[6].decay = 0.28;
    pads[6].level = 0.80; pads[6].strikePosition = 0.30;
    pads[6].tsPitchEnvStart = toLogNorm(420);
    pads[6].tsPitchEnvEnd   = toLogNorm(350);
    pads[6].tsPitchEnvTime  = 0.04;
    pads[6].airLoading = 0.42; pads[6].modeScatter = 0.10;
    pads[6].couplingStrength = 0.25; pads[6].secondaryEnabled = 1.0;
    pads[6].secondarySize = 0.30; pads[6].secondaryMaterial = 0.40;
    pads[6].tensionModAmt = 0.22;
    pads[6].clickLayerMix = 0.55; pads[6].clickLayerContactMs = 0.15;
    pads[6].clickLayerBrightness = 0.72;
    pads[6].noiseLayerMix = 0.10;
    pads[6].bodyDampingB1 = 0.30; pads[6].bodyDampingB3 = 0.10;

    pads[8] = pads[6];
    pads[8].size = 0.40; pads[8].decay = 0.32;
    pads[8].tsPitchEnvStart = toLogNorm(340);
    pads[8].tsPitchEnvEnd   = toLogNorm(280);

    // Djembe bass (10)
    pads[10].exciterType = ExciterType::Impulse;
    pads[10].bodyModel = BodyModelType::Membrane;
    pads[10].material = 0.30; pads[10].size = 0.78; pads[10].decay = 0.45;
    pads[10].level = 0.85; pads[10].strikePosition = 0.50;
    pads[10].tsPitchEnvStart = toLogNorm(150);
    pads[10].tsPitchEnvEnd   = toLogNorm(85);
    pads[10].tsPitchEnvTime  = 0.05;
    pads[10].airLoading = 0.65; pads[10].modeScatter = 0.18;
    pads[10].couplingStrength = 0.40; pads[10].secondaryEnabled = 1.0;
    pads[10].secondarySize = 0.50; pads[10].secondaryMaterial = 0.30;
    pads[10].tensionModAmt = 0.22;
    pads[10].clickLayerMix = 0.40; pads[10].clickLayerContactMs = 0.22;
    pads[10].clickLayerBrightness = 0.40;
    pads[10].noiseLayerMix = 0.18; pads[10].noiseLayerColor = 0.45;
    pads[10].bodyDampingB1 = 0.30; pads[10].bodyDampingB3 = 0.10;
    pads[10].macroBodySize = 0.85;

    // Djembe slap (12)
    pads[12] = pads[10];
    pads[12].decay = 0.20; pads[12].strikePosition = 0.10;
    pads[12].clickLayerMix = 0.85; pads[12].clickLayerBrightness = 0.78;
    pads[12].clickLayerContactMs = 0.12;
    pads[12].tsPitchEnvStart = toLogNorm(280);
    pads[12].tsPitchEnvEnd   = toLogNorm(220);

    // Cajón bass (5)
    pads[5].exciterType = ExciterType::Impulse;
    pads[5].bodyModel = BodyModelType::Plate;
    pads[5].material = 0.32; pads[5].size = 0.62; pads[5].decay = 0.35;
    pads[5].level = 0.82; pads[5].strikePosition = 0.50;
    pads[5].modeStretch = 0.42;
    pads[5].airLoading = 0.30; pads[5].modeScatter = 0.15;
    pads[5].couplingStrength = 0.30; pads[5].secondaryEnabled = 1.0;
    pads[5].secondarySize = 0.50; pads[5].secondaryMaterial = 0.30;
    pads[5].clickLayerMix = 0.55; pads[5].clickLayerContactMs = 0.20;
    pads[5].clickLayerBrightness = 0.40;
    pads[5].noiseLayerMix = 0.12; pads[5].noiseLayerColor = 0.40;
    pads[5].bodyDampingB1 = 0.30; pads[5].bodyDampingB3 = 0.10;
    pads[5].tsPitchEnvStart = toLogNorm(180);
    pads[5].tsPitchEnvEnd   = toLogNorm(95);
    pads[5].tsPitchEnvTime  = 0.05;

    // Cajón slap (7)
    pads[7] = pads[5];
    pads[7].decay = 0.20; pads[7].strikePosition = 0.15;
    pads[7].clickLayerMix = 0.85; pads[7].clickLayerBrightness = 0.78;
    pads[7].material = 0.50;

    // Frame drum (11)
    pads[11].exciterType = ExciterType::Mallet;
    pads[11].bodyModel = BodyModelType::Membrane;
    pads[11].material = 0.40; pads[11].size = 0.85; pads[11].decay = 0.55;
    pads[11].level = 0.78;
    pads[11].airLoading = 0.78; pads[11].modeScatter = 0.20;
    pads[11].couplingStrength = 0.22;
    pads[11].clickLayerMix = 0.32; pads[11].clickLayerContactMs = 0.30;
    pads[11].clickLayerBrightness = 0.30;
    pads[11].noiseLayerMix = 0.20; pads[11].noiseLayerColor = 0.40;
    pads[11].bodyDampingB1 = 0.30; pads[11].bodyDampingB3 = 0.10;
    pads[11].decaySkew = 0.45;

    // Hand shaker (3)
    pads[3].exciterType = ExciterType::NoiseBurst;
    pads[3].bodyModel = BodyModelType::NoiseBody;
    pads[3].material = 0.85; pads[3].size = 0.12; pads[3].decay = 0.10;
    pads[3].level = 0.65;
    pads[3].noiseBurstDuration = 0.25;
    pads[3].noiseLayerMix = 0.85; pads[3].noiseLayerCutoff = 0.78;
    pads[3].noiseLayerColor = 0.65; pads[3].noiseLayerDecay = 0.10;
    pads[3].clickLayerMix = 0.0;
    pads[3].airLoading = 0.0; pads[3].modeScatter = 0.20;
    pads[3].bodyDampingB3 = 0.0; pads[3].bodyDampingB1 = 0.55;

    // Wood block (9)
    pads[9].exciterType = ExciterType::Impulse;
    pads[9].bodyModel = BodyModelType::Plate;
    pads[9].material = 0.32; pads[9].size = 0.18; pads[9].decay = 0.18;
    pads[9].level = 0.75;
    pads[9].modeStretch = 0.55;
    pads[9].clickLayerMix = 0.85; pads[9].clickLayerBrightness = 0.78;
    pads[9].clickLayerContactMs = 0.10;
    pads[9].noiseLayerMix = 0.0;
    pads[9].airLoading = 0.0;
    pads[9].bodyDampingB1 = 0.45; pads[9].bodyDampingB3 = 0.10;

    k.opts.maxPolyphony    = 12;
    k.opts.globalCoupling  = 0.30;
    k.opts.snareBuzz       = 0.15;
    k.opts.tomResonance    = 0.20;
    k.opts.couplingDelayMs = 0.9;
    k.crafted = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    return k;
}
Kit latinPercKit() {
    Kit k{"Latin Perc", "Percussive", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Clave hi (0)
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Bell;
    pads[0].material = 0.85; pads[0].size = 0.10; pads[0].decay = 0.18;
    pads[0].level = 0.80;
    pads[0].fmRatio = 0.62;
    pads[0].modeStretch = 0.50;
    pads[0].clickLayerMix = 0.92; pads[0].clickLayerContactMs = 0.06;
    pads[0].clickLayerBrightness = 0.92;
    pads[0].noiseLayerMix = 0.0;
    pads[0].airLoading = 0.0;
    pads[0].bodyDampingB3 = 0.0; pads[0].bodyDampingB1 = 0.40;
    pads[0].macroBrightness = 0.85;

    // Clave lo (2)
    pads[2] = pads[0];
    pads[2].size = 0.16; pads[2].decay = 0.22;
    pads[2].fmRatio = 0.50;

    // Cowbell hi (4)
    pads[4].exciterType = ExciterType::FMImpulse;
    pads[4].bodyModel = BodyModelType::Bell;
    pads[4].material = 0.78; pads[4].size = 0.20; pads[4].decay = 0.30;
    pads[4].level = 0.78;
    pads[4].fmRatio = 0.55; pads[4].feedbackAmount = 0.10;
    pads[4].clickLayerMix = 0.55; pads[4].clickLayerContactMs = 0.10;
    pads[4].clickLayerBrightness = 0.78;
    pads[4].noiseLayerMix = 0.10; pads[4].noiseLayerColor = 0.65;
    pads[4].airLoading = 0.0; pads[4].modeScatter = 0.18;
    pads[4].bodyDampingB3 = 0.0; pads[4].bodyDampingB1 = 0.32;
    pads[4].macroBrightness = 0.75;

    // Cowbell lo (6)
    pads[6] = pads[4];
    pads[6].size = 0.28; pads[6].decay = 0.40;
    pads[6].fmRatio = 0.42;
    pads[6].chokeGroup = 0;

    // Agogo hi (8)
    pads[8].exciterType = ExciterType::FMImpulse;
    pads[8].bodyModel = BodyModelType::Bell;
    pads[8].material = 0.85; pads[8].size = 0.14; pads[8].decay = 0.28;
    pads[8].level = 0.75;
    pads[8].fmRatio = 0.72;
    pads[8].clickLayerMix = 0.55; pads[8].clickLayerBrightness = 0.85;
    pads[8].noiseLayerMix = 0.0;
    pads[8].airLoading = 0.0;
    pads[8].bodyDampingB3 = 0.0; pads[8].bodyDampingB1 = 0.30;

    // Agogo lo (10)
    pads[10] = pads[8];
    pads[10].size = 0.22; pads[10].fmRatio = 0.55;
    pads[10].decay = 0.35;

    // Timbale hi (12)
    pads[12].exciterType = ExciterType::NoiseBurst;
    pads[12].bodyModel = BodyModelType::Membrane;
    pads[12].material = 0.55; pads[12].size = 0.40; pads[12].decay = 0.30;
    pads[12].level = 0.82;
    pads[12].noiseBurstDuration = 0.25;
    pads[12].tsPitchEnvStart = toLogNorm(380);
    pads[12].tsPitchEnvEnd   = toLogNorm(280);
    pads[12].tsPitchEnvTime  = 0.04;
    pads[12].airLoading = 0.40; pads[12].modeScatter = 0.18;
    pads[12].couplingStrength = 0.50; pads[12].secondaryEnabled = 1.0;
    pads[12].secondarySize = 0.40; pads[12].secondaryMaterial = 0.65;
    pads[12].tensionModAmt = 0.22;
    pads[12].clickLayerMix = 0.65; pads[12].clickLayerContactMs = 0.12;
    pads[12].clickLayerBrightness = 0.78;
    pads[12].noiseLayerMix = 0.30; pads[12].noiseLayerColor = 0.65;
    pads[12].bodyDampingB1 = 0.32; pads[12].bodyDampingB3 = 0.10;

    // Timbale lo (14)
    pads[14] = pads[12];
    pads[14].size = 0.55; pads[14].decay = 0.40;
    pads[14].material = 0.50;
    pads[14].tsPitchEnvStart = toLogNorm(280);
    pads[14].tsPitchEnvEnd   = toLogNorm(200);
    pads[14].secondarySize = 0.50;

    // Cabasa (1)
    pads[1].exciterType = ExciterType::NoiseBurst;
    pads[1].bodyModel = BodyModelType::NoiseBody;
    pads[1].material = 0.85; pads[1].size = 0.10; pads[1].decay = 0.10;
    pads[1].level = 0.65;
    pads[1].noiseBurstDuration = 0.25;
    pads[1].noiseLayerMix = 0.85; pads[1].noiseLayerCutoff = 0.85;
    pads[1].noiseLayerColor = 0.78; pads[1].noiseLayerDecay = 0.10;
    pads[1].clickLayerMix = 0.0;
    pads[1].airLoading = 0.0; pads[1].modeScatter = 0.30;
    pads[1].bodyDampingB3 = 0.0; pads[1].bodyDampingB1 = 0.55;

    // Maracas (3)
    pads[3].exciterType = ExciterType::NoiseBurst;
    pads[3].bodyModel = BodyModelType::NoiseBody;
    pads[3].material = 0.78; pads[3].size = 0.14; pads[3].decay = 0.18;
    pads[3].level = 0.65;
    pads[3].noiseBurstDuration = 0.45;
    pads[3].noiseLayerMix = 0.85; pads[3].noiseLayerCutoff = 0.55;
    pads[3].noiseLayerColor = 0.50; pads[3].noiseLayerDecay = 0.18;
    pads[3].clickLayerMix = 0.0;
    pads[3].airLoading = 0.0; pads[3].modeScatter = 0.42;
    pads[3].bodyDampingB3 = 0.0; pads[3].bodyDampingB1 = 0.45;

    // Tambourine (5)
    pads[5].exciterType = ExciterType::NoiseBurst;
    pads[5].bodyModel = BodyModelType::NoiseBody;
    pads[5].material = 0.85; pads[5].size = 0.20; pads[5].decay = 0.22;
    pads[5].level = 0.72;
    pads[5].noiseBurstDuration = 0.30;
    pads[5].noiseLayerMix = 0.78; pads[5].noiseLayerCutoff = 0.92;
    pads[5].noiseLayerColor = 0.92; pads[5].noiseLayerDecay = 0.22;
    pads[5].clickLayerMix = 0.30; pads[5].clickLayerBrightness = 0.85;
    pads[5].airLoading = 0.0; pads[5].modeScatter = 0.55;
    pads[5].couplingStrength = 0.40; pads[5].secondaryEnabled = 1.0;
    pads[5].secondarySize = 0.20; pads[5].secondaryMaterial = 0.85;
    pads[5].bodyDampingB3 = 0.0; pads[5].bodyDampingB1 = 0.32;
    pads[5].macroComplexity = 0.65;

    // Triangle (7)
    pads[7].exciterType = ExciterType::Impulse;
    pads[7].bodyModel = BodyModelType::Bell;
    pads[7].material = 0.95; pads[7].size = 0.08; pads[7].decay = 0.85;
    pads[7].level = 0.65;
    pads[7].fmRatio = 0.55; pads[7].modeStretch = 0.55;
    pads[7].clickLayerMix = 0.55; pads[7].clickLayerBrightness = 0.95;
    pads[7].clickLayerContactMs = 0.06;
    pads[7].noiseLayerMix = 0.0;
    pads[7].airLoading = 0.0;
    pads[7].bodyDampingB3 = 0.0; pads[7].bodyDampingB1 = 0.30;

    // Guiro (9)
    pads[9].exciterType = ExciterType::Friction;
    pads[9].bodyModel = BodyModelType::NoiseBody;
    pads[9].material = 0.65; pads[9].size = 0.18; pads[9].decay = 0.30;
    pads[9].level = 0.70;
    pads[9].frictionPressure = 0.55;
    pads[9].noiseLayerMix = 0.65; pads[9].noiseLayerCutoff = 0.62;
    pads[9].noiseLayerColor = 0.55; pads[9].noiseLayerDecay = 0.30;
    pads[9].clickLayerMix = 0.0;
    pads[9].airLoading = 0.0; pads[9].modeScatter = 0.30;
    pads[9].bodyDampingB3 = 0.0; pads[9].bodyDampingB1 = 0.42;
    pads[9].decaySkew = 0.55;

    // Vibraslap (11)
    pads[11].exciterType = ExciterType::NoiseBurst;
    pads[11].bodyModel = BodyModelType::NoiseBody;
    pads[11].material = 0.85; pads[11].size = 0.22; pads[11].decay = 0.32;
    pads[11].level = 0.70;
    pads[11].noiseBurstDuration = 0.55;
    pads[11].noiseLayerMix = 0.85; pads[11].noiseLayerCutoff = 0.78;
    pads[11].noiseLayerResonance = 0.30;
    pads[11].noiseLayerColor = 0.62; pads[11].noiseLayerDecay = 0.32;
    pads[11].clickLayerMix = 0.30; pads[11].clickLayerContactMs = 0.30;
    pads[11].airLoading = 0.0; pads[11].modeScatter = 0.62;
    pads[11].bodyDampingB3 = 0.0; pads[11].bodyDampingB1 = 0.32;
    pads[11].macroComplexity = 0.78;

    // Bongo perc filler (13)
    pads[13].exciterType = ExciterType::Impulse;
    pads[13].bodyModel = BodyModelType::Membrane;
    pads[13].material = 0.55; pads[13].size = 0.25; pads[13].decay = 0.18;
    pads[13].level = 0.75; pads[13].strikePosition = 0.20;
    pads[13].airLoading = 0.30;
    pads[13].clickLayerMix = 0.65; pads[13].clickLayerBrightness = 0.65;
    pads[13].noiseLayerMix = 0.10;
    pads[13].bodyDampingB1 = 0.32; pads[13].bodyDampingB3 = 0.10;

    k.opts.maxPolyphony    = 12;
    k.opts.globalCoupling  = 0.10;
    k.opts.couplingDelayMs = 0.8;
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    return k;
}
Kit tablaKit() {
    Kit k{"Tabla", "Percussive", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Bayan (left, bass) (0)
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.30; pads[0].size = 0.72; pads[0].decay = 0.55;
    pads[0].level = 0.82; pads[0].strikePosition = 0.50;
    pads[0].tsPitchEnvStart = toLogNorm(180);
    pads[0].tsPitchEnvEnd   = toLogNorm(70);
    pads[0].tsPitchEnvTime  = 0.20;
    pads[0].tsPitchEnvCurve = 1.0;
    pads[0].airLoading = 0.62; pads[0].modeScatter = 0.10;
    pads[0].couplingStrength = 0.32; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.45; pads[0].secondaryMaterial = 0.40;
    pads[0].tensionModAmt = 0.65;
    pads[0].clickLayerMix = 0.45; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.45;
    pads[0].noiseLayerMix = 0.10; pads[0].noiseLayerColor = 0.42;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.05;
    pads[0].decaySkew = 0.55;
    pads[0].macroComplexity = 0.70;

    // Bayan slap (1)
    pads[1] = pads[0];
    pads[1].decay = 0.20; pads[1].strikePosition = 0.20;
    pads[1].clickLayerMix = 0.85; pads[1].clickLayerBrightness = 0.65;
    pads[1].tensionModAmt = 0.20;

    // Dayan (2)
    pads[2].exciterType = ExciterType::Impulse;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.50; pads[2].size = 0.42; pads[2].decay = 0.58;
    pads[2].level = 0.80; pads[2].strikePosition = 0.40;
    pads[2].tsPitchEnvStart = toLogNorm(420);
    pads[2].tsPitchEnvEnd   = toLogNorm(380);
    pads[2].tsPitchEnvTime  = 0.04;
    pads[2].airLoading = 0.45; pads[2].modeScatter = 0.05;
    pads[2].couplingStrength = 0.35; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.35; pads[2].secondaryMaterial = 0.45;
    pads[2].tensionModAmt = 0.25;
    pads[2].clickLayerMix = 0.55; pads[2].clickLayerContactMs = 0.12;
    pads[2].clickLayerBrightness = 0.75;
    pads[2].noiseLayerMix = 0.08;
    pads[2].decaySkew = 0.60;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.05;
    pads[2].nonlinearCoupling = 0.20;

    // Tin (3)
    pads[3] = pads[2];
    pads[3].decay = 0.22; pads[3].strikePosition = 0.10;
    pads[3].clickLayerMix = 0.85; pads[3].clickLayerBrightness = 0.85;
    pads[3].tensionModAmt = 0.15; pads[3].decaySkew = 0.40;

    // Tha (4)
    pads[4].exciterType = ExciterType::Mallet;
    pads[4].bodyModel = BodyModelType::Membrane;
    pads[4].material = 0.50; pads[4].size = 0.42; pads[4].decay = 0.45;
    pads[4].level = 0.80; pads[4].strikePosition = 0.50;
    pads[4].airLoading = 0.42; pads[4].modeScatter = 0.10;
    pads[4].couplingStrength = 0.30; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.35; pads[4].secondaryMaterial = 0.45;
    pads[4].tensionModAmt = 0.30;
    pads[4].clickLayerMix = 0.40; pads[4].clickLayerContactMs = 0.30;
    pads[4].clickLayerBrightness = 0.45;
    pads[4].noiseLayerMix = 0.15;
    pads[4].bodyDampingB1 = 0.30; pads[4].bodyDampingB3 = 0.05;
    pads[4].decaySkew = 0.50;

    // Na (5)
    pads[5].exciterType = ExciterType::Impulse;
    pads[5].bodyModel = BodyModelType::Membrane;
    pads[5].material = 0.55; pads[5].size = 0.42; pads[5].decay = 0.45;
    pads[5].level = 0.78; pads[5].strikePosition = 0.18;
    pads[5].morphEnabled = 1.0;
    pads[5].morphStart = 0.45; pads[5].morphEnd = 0.65;
    pads[5].morphDuration = 0.40; pads[5].morphCurve = 0.4;
    pads[5].airLoading = 0.38; pads[5].modeScatter = 0.45;
    pads[5].decaySkew = 0.50;
    pads[5].couplingStrength = 0.38; pads[5].secondaryEnabled = 1.0;
    pads[5].secondarySize = 0.32; pads[5].secondaryMaterial = 0.50;
    pads[5].tensionModAmt = 0.18;
    pads[5].clickLayerMix = 0.55; pads[5].clickLayerBrightness = 0.78;
    pads[5].clickLayerContactMs = 0.14;
    pads[5].noiseLayerMix = 0.05;
    pads[5].decaySkew = 0.65;
    pads[5].nonlinearCoupling = 0.32;
    pads[5].bodyDampingB1 = 0.30; pads[5].bodyDampingB3 = 0.05;

    // Ge (6)
    pads[6] = pads[0];
    pads[6].decay = 0.78; pads[6].tensionModAmt = 0.85;
    pads[6].decaySkew = 0.65;

    // Ka (7)
    pads[7] = pads[1];
    pads[7].material = 0.30; pads[7].size = 0.65;

    // Tete (8)
    pads[8] = pads[3];
    pads[8].decay = 0.10; pads[8].decaySkew = 0.30;

    // Tanpura drone (9)
    pads[9].exciterType = ExciterType::Friction;
    pads[9].bodyModel = BodyModelType::String;
    pads[9].material = 0.55; pads[9].size = 0.65; pads[9].decay = 0.95;
    pads[9].level = 0.55;
    pads[9].frictionPressure = 0.32;
    pads[9].modeStretch = 0.40;
    pads[9].nonlinearCoupling = 0.45;
    pads[9].morphEnabled = 1.0;
    pads[9].morphStart = 0.55; pads[9].morphEnd = 0.65;
    pads[9].morphDuration = 0.85; pads[9].morphCurve = 0.5;
    pads[9].decaySkew = 0.85;
    pads[9].noiseLayerMix = 0.18; pads[9].noiseLayerCutoff = 0.45;
    pads[9].clickLayerMix = 0.0;
    pads[9].bodyDampingB1 = 0.30; pads[9].bodyDampingB3 = 0.18;
    pads[9].outputBus = 1;
    pads[9].macroComplexity = 0.85;

    k.opts.maxPolyphony    = 12;
    k.opts.globalCoupling  = 0.30;
    k.opts.tomResonance    = 0.45;
    k.opts.couplingDelayMs = 1.1;
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    return k;
}
Kit worldMetalKit() {
    Kit k{"World Metal", "Percussive", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Kalimba pads (0-7)
    const int    kalimbaPads[] = {0, 1, 2, 3, 4, 5, 6, 7};
    const double kalimbaMat[]  = {0.42, 0.48, 0.55, 0.62, 0.68, 0.75, 0.82, 0.88};
    const double kalimbaSize[] = {0.40, 0.36, 0.32, 0.28, 0.25, 0.22, 0.20, 0.18};
    for (int i = 0; i < 8; ++i) {
        const int p = kalimbaPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel = BodyModelType::Bell;
        pads[p].material = kalimbaMat[i]; pads[p].size = kalimbaSize[i];
        pads[p].decay = 0.65; pads[p].level = 0.72;
        pads[p].fmRatio = 0.42 + i * 0.02;
        pads[p].modeStretch = 0.40;
        pads[p].clickLayerMix = 0.45; pads[p].clickLayerContactMs = 0.18;
        pads[p].clickLayerBrightness = 0.65;
        pads[p].noiseLayerMix = 0.08;
        pads[p].airLoading = 0.0;
        pads[p].bodyDampingB3 = 0.0; pads[p].bodyDampingB1 = 0.30;
        pads[p].decaySkew = 0.55;
        pads[p].macroBrightness = 0.60 + i * 0.03;
    }

    // Mbira (8-11)
    const int    mbiraPads[] = {8, 9, 10, 11};
    const double mbiraMat[]  = {0.50, 0.62, 0.72, 0.82};
    const double mbiraSize[] = {0.32, 0.28, 0.24, 0.20};
    for (int i = 0; i < 4; ++i) {
        const int p = mbiraPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel = BodyModelType::String;
        pads[p].material = mbiraMat[i]; pads[p].size = mbiraSize[i];
        pads[p].decay = 0.78; pads[p].level = 0.70;
        pads[p].modeStretch = 0.42;
        pads[p].nonlinearCoupling = 0.18;
        pads[p].clickLayerMix = 0.32; pads[p].clickLayerContactMs = 0.14;
        pads[p].clickLayerBrightness = 0.70;
        pads[p].noiseLayerMix = 0.12;
        pads[p].airLoading = 0.0;
        pads[p].bodyDampingB1 = 0.30; pads[p].bodyDampingB3 = 0.22;
        pads[p].decaySkew = 0.60;
    }

    // Bell tree (12)
    pads[12].exciterType = ExciterType::NoiseBurst;
    pads[12].bodyModel = BodyModelType::Bell;
    pads[12].material = 0.95; pads[12].size = 0.30; pads[12].decay = 0.85;
    pads[12].level = 0.70;
    pads[12].fmRatio = 0.55;
    pads[12].modeStretch = 0.55;
    pads[12].modeScatter = 0.55;
    pads[12].morphEnabled = 1.0;
    pads[12].morphStart = 0.85; pads[12].morphEnd = 0.55;
    pads[12].morphDuration = 0.55; pads[12].morphCurve = 0.3;
    pads[12].clickLayerMix = 0.55; pads[12].clickLayerBrightness = 0.92;
    pads[12].noiseLayerMix = 0.42; pads[12].noiseLayerCutoff = 0.92;
    pads[12].noiseLayerColor = 0.85; pads[12].noiseLayerDecay = 0.85;
    pads[12].airLoading = 0.0;
    pads[12].bodyDampingB3 = 0.0; pads[12].bodyDampingB1 = 0.30;
    pads[12].decaySkew = 0.78;

    // Crotales hi (13)
    pads[13].exciterType = ExciterType::Mallet;
    pads[13].bodyModel = BodyModelType::Bell;
    pads[13].material = 0.92; pads[13].size = 0.16; pads[13].decay = 0.85;
    pads[13].level = 0.72;
    pads[13].fmRatio = 0.62;
    pads[13].clickLayerMix = 0.45; pads[13].clickLayerBrightness = 0.92;
    pads[13].noiseLayerMix = 0.0;
    pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].decaySkew = 0.55;

    // Crotales lo (14)
    pads[14] = pads[13];
    pads[14].size = 0.22; pads[14].fmRatio = 0.45;

    // Singing bowl (15)
    pads[15].exciterType = ExciterType::Friction;
    pads[15].bodyModel = BodyModelType::Bell;
    pads[15].material = 0.78; pads[15].size = 0.45; pads[15].decay = 0.95;
    pads[15].level = 0.65;
    pads[15].frictionPressure = 0.28;
    pads[15].modeStretch = 0.45;
    pads[15].nonlinearCoupling = 0.22;
    pads[15].morphEnabled = 1.0;
    pads[15].morphStart = 0.78; pads[15].morphEnd = 0.55;
    pads[15].morphDuration = 0.85; pads[15].morphCurve = 0.5;
    pads[15].tensionModAmt = 0.40;
    pads[15].decaySkew = 0.85;
    pads[15].noiseLayerMix = 0.10; pads[15].noiseLayerColor = 0.45;
    pads[15].clickLayerMix = 0.0;
    pads[15].airLoading = 0.0;
    pads[15].bodyDampingB3 = 0.0; pads[15].bodyDampingB1 = 0.30;
    pads[15].outputBus = 1;
    pads[15].macroComplexity = 0.85;

    // Wood blocks (16, 17)
    pads[16].exciterType = ExciterType::Impulse;
    pads[16].bodyModel = BodyModelType::Plate;
    pads[16].material = 0.30; pads[16].size = 0.18; pads[16].decay = 0.18;
    pads[16].level = 0.72;
    pads[16].modeStretch = 0.55;
    pads[16].clickLayerMix = 0.85; pads[16].clickLayerBrightness = 0.78;
    pads[16].clickLayerContactMs = 0.10;
    pads[16].noiseLayerMix = 0.0;
    pads[16].airLoading = 0.0;
    pads[16].bodyDampingB1 = 0.42; pads[16].bodyDampingB3 = 0.10;

    pads[17] = pads[16];
    pads[17].size = 0.28; pads[17].material = 0.28; pads[17].decay = 0.22;

    // Tibetan tingsha (18)
    pads[18].exciterType = ExciterType::Impulse;
    pads[18].bodyModel = BodyModelType::Bell;
    pads[18].material = 0.95; pads[18].size = 0.12; pads[18].decay = 0.92;
    pads[18].level = 0.65;
    pads[18].fmRatio = 0.78; pads[18].modeStretch = 0.55;
    pads[18].clickLayerMix = 0.50; pads[18].clickLayerBrightness = 0.95;
    pads[18].noiseLayerMix = 0.0;
    pads[18].airLoading = 0.0;
    pads[18].bodyDampingB3 = 0.0; pads[18].bodyDampingB1 = 0.30;
    pads[18].decaySkew = 0.65;

    // Indian temple bell (19)
    pads[19].exciterType = ExciterType::Mallet;
    pads[19].bodyModel = BodyModelType::Bell;
    pads[19].material = 0.85; pads[19].size = 0.50; pads[19].decay = 0.92;
    pads[19].level = 0.70;
    pads[19].fmRatio = 0.30;
    pads[19].modeStretch = 0.50;
    pads[19].clickLayerMix = 0.30; pads[19].clickLayerBrightness = 0.65;
    pads[19].noiseLayerMix = 0.05;
    pads[19].airLoading = 0.0;
    pads[19].bodyDampingB3 = 0.0; pads[19].bodyDampingB1 = 0.30;
    pads[19].decaySkew = 0.78;
    pads[19].tensionModAmt = 0.18;

    k.opts.maxPolyphony    = 16;
    k.opts.globalCoupling  = 0.40;
    k.opts.couplingDelayMs = 1.4;
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    return k;
}
Kit cajonFramesKit() {
    Kit k{"Cajon and Frames", "Percussive", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Cajón bass (0)
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Plate;
    pads[0].material = 0.30; pads[0].size = 0.65; pads[0].decay = 0.40;
    pads[0].level = 0.85; pads[0].strikePosition = 0.50;
    pads[0].modeStretch = 0.42;
    pads[0].tsPitchEnvStart = toLogNorm(150);
    pads[0].tsPitchEnvEnd   = toLogNorm(78);
    pads[0].tsPitchEnvTime  = 0.04;
    pads[0].airLoading = 0.45; pads[0].modeScatter = 0.18;
    pads[0].couplingStrength = 0.45; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.55; pads[0].secondaryMaterial = 0.30;
    pads[0].tensionModAmt = 0.18;
    pads[0].clickLayerMix = 0.55; pads[0].clickLayerContactMs = 0.20;
    pads[0].clickLayerBrightness = 0.45;
    pads[0].noiseLayerMix = 0.12; pads[0].noiseLayerColor = 0.45;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroBodySize = 0.78;

    // Cajón slap (2)
    pads[2] = pads[0];
    pads[2].decay = 0.22; pads[2].strikePosition = 0.10;
    pads[2].material = 0.42;
    pads[2].clickLayerMix = 0.85; pads[2].clickLayerBrightness = 0.78;
    pads[2].clickLayerContactMs = 0.10;
    pads[2].tsPitchEnvStart = toLogNorm(280);
    pads[2].tsPitchEnvEnd   = toLogNorm(220);
    pads[2].macroPunch = 0.85;

    // Cajón snare side (4)
    pads[4].exciterType = ExciterType::NoiseBurst;
    pads[4].bodyModel = BodyModelType::Plate;
    pads[4].material = 0.40; pads[4].size = 0.45; pads[4].decay = 0.32;
    pads[4].level = 0.78;
    pads[4].noiseBurstDuration = 0.30;
    pads[4].modeStretch = 0.42; pads[4].decaySkew = 0.40;
    pads[4].noiseLayerMix = 0.65; pads[4].noiseLayerCutoff = 0.62;
    pads[4].noiseLayerColor = 0.55; pads[4].noiseLayerDecay = 0.30;
    pads[4].clickLayerMix = 0.45; pads[4].clickLayerBrightness = 0.62;
    pads[4].airLoading = 0.30; pads[4].modeScatter = 0.30;
    pads[4].couplingStrength = 0.40; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.40; pads[4].secondaryMaterial = 0.30;
    pads[4].bodyDampingB1 = 0.30; pads[4].bodyDampingB3 = 0.10;

    // Frame drum tap (6)
    pads[6].exciterType = ExciterType::Mallet;
    pads[6].bodyModel = BodyModelType::Membrane;
    pads[6].material = 0.40; pads[6].size = 0.78; pads[6].decay = 0.50;
    pads[6].level = 0.78; pads[6].strikePosition = 0.35;
    pads[6].airLoading = 0.85; pads[6].modeScatter = 0.18;
    pads[6].couplingStrength = 0.30; pads[6].secondaryEnabled = 1.0;
    pads[6].secondarySize = 0.40; pads[6].secondaryMaterial = 0.32;
    pads[6].tensionModAmt = 0.20;
    pads[6].clickLayerMix = 0.40; pads[6].clickLayerContactMs = 0.30;
    pads[6].clickLayerBrightness = 0.40;
    pads[6].noiseLayerMix = 0.18; pads[6].noiseLayerColor = 0.40;
    pads[6].bodyDampingB1 = 0.30; pads[6].bodyDampingB3 = 0.10;

    // Frame drum slap (8)
    pads[8] = pads[6];
    pads[8].decay = 0.22; pads[8].strikePosition = 0.08;
    pads[8].clickLayerMix = 0.85; pads[8].clickLayerBrightness = 0.65;

    // Bodhran (10)
    pads[10].exciterType = ExciterType::Impulse;
    pads[10].bodyModel = BodyModelType::Membrane;
    pads[10].material = 0.45; pads[10].size = 0.72; pads[10].decay = 0.40;
    pads[10].level = 0.80; pads[10].strikePosition = 0.40;
    pads[10].airLoading = 0.78; pads[10].modeScatter = 0.20;
    pads[10].couplingStrength = 0.32; pads[10].secondaryEnabled = 1.0;
    pads[10].secondarySize = 0.42; pads[10].secondaryMaterial = 0.30;
    pads[10].tensionModAmt = 0.30;
    pads[10].clickLayerMix = 0.65; pads[10].clickLayerContactMs = 0.18;
    pads[10].clickLayerBrightness = 0.62;
    pads[10].noiseLayerMix = 0.18;
    pads[10].bodyDampingB1 = 0.30; pads[10].bodyDampingB3 = 0.10;

    // Dholak hi (12)
    pads[12].exciterType = ExciterType::Impulse;
    pads[12].bodyModel = BodyModelType::Membrane;
    pads[12].material = 0.48; pads[12].size = 0.55; pads[12].decay = 0.45;
    pads[12].level = 0.80; pads[12].strikePosition = 0.32;
    pads[12].tsPitchEnvStart = toLogNorm(320);
    pads[12].tsPitchEnvEnd   = toLogNorm(220);
    pads[12].tsPitchEnvTime  = 0.08;
    pads[12].airLoading = 0.55; pads[12].modeScatter = 0.10;
    pads[12].couplingStrength = 0.50; pads[12].secondaryEnabled = 1.0;
    pads[12].secondarySize = 0.45; pads[12].secondaryMaterial = 0.32;
    pads[12].tensionModAmt = 0.30;
    pads[12].clickLayerMix = 0.55; pads[12].clickLayerBrightness = 0.65;
    pads[12].noiseLayerMix = 0.15; pads[12].noiseLayerColor = 0.50;
    pads[12].bodyDampingB1 = 0.30; pads[12].bodyDampingB3 = 0.10;
    pads[12].decaySkew = 0.50;

    // Dholak lo (14)
    pads[14] = pads[12];
    pads[14].size = 0.65; pads[14].material = 0.40;
    pads[14].tsPitchEnvStart = toLogNorm(220);
    pads[14].tsPitchEnvEnd   = toLogNorm(140);
    pads[14].decay = 0.55;

    // Riq (5)
    pads[5].exciterType = ExciterType::Impulse;
    pads[5].bodyModel = BodyModelType::Membrane;
    pads[5].material = 0.50; pads[5].size = 0.42; pads[5].decay = 0.32;
    pads[5].level = 0.78; pads[5].strikePosition = 0.30;
    pads[5].airLoading = 0.45; pads[5].modeScatter = 0.42;
    pads[5].couplingStrength = 0.45; pads[5].secondaryEnabled = 1.0;
    pads[5].secondarySize = 0.20; pads[5].secondaryMaterial = 0.85;
    pads[5].clickLayerMix = 0.65; pads[5].clickLayerBrightness = 0.78;
    pads[5].clickLayerContactMs = 0.12;
    pads[5].noiseLayerMix = 0.45; pads[5].noiseLayerCutoff = 0.85;
    pads[5].noiseLayerColor = 0.85; pads[5].noiseLayerDecay = 0.20;
    pads[5].bodyDampingB1 = 0.32; pads[5].bodyDampingB3 = 0.10;
    pads[5].macroComplexity = 0.78;

    // Pandeiro shake (7)
    pads[7].exciterType = ExciterType::NoiseBurst;
    pads[7].bodyModel = BodyModelType::NoiseBody;
    pads[7].material = 0.85; pads[7].size = 0.18; pads[7].decay = 0.20;
    pads[7].level = 0.72;
    pads[7].noiseBurstDuration = 0.40;
    pads[7].noiseLayerMix = 0.85; pads[7].noiseLayerCutoff = 0.92;
    pads[7].noiseLayerColor = 0.88; pads[7].noiseLayerDecay = 0.20;
    pads[7].clickLayerMix = 0.20; pads[7].clickLayerBrightness = 0.85;
    pads[7].airLoading = 0.0; pads[7].modeScatter = 0.55;
    pads[7].bodyDampingB3 = 0.0; pads[7].bodyDampingB1 = 0.32;

    // Tabla-style fillers (9, 11, 13)
    pads[9].exciterType = ExciterType::Impulse;
    pads[9].bodyModel = BodyModelType::Membrane;
    pads[9].material = 0.50; pads[9].size = 0.32; pads[9].decay = 0.30;
    pads[9].level = 0.75; pads[9].strikePosition = 0.30;
    pads[9].airLoading = 0.40;
    pads[9].clickLayerMix = 0.55; pads[9].clickLayerBrightness = 0.62;
    pads[9].noiseLayerMix = 0.10;
    pads[9].bodyDampingB1 = 0.32; pads[9].bodyDampingB3 = 0.10;

    pads[11] = pads[9];
    pads[11].size = 0.42; pads[11].decay = 0.38; pads[11].material = 0.45;

    pads[13] = pads[9];
    pads[13].size = 0.22; pads[13].decay = 0.20; pads[13].material = 0.55;
    pads[13].clickLayerBrightness = 0.78;

    k.opts.maxPolyphony    = 12;
    k.opts.globalCoupling  = 0.18;
    k.opts.snareBuzz       = 0.20;
    k.opts.tomResonance    = 0.30;
    k.opts.couplingDelayMs = 1.0;
    k.crafted = {0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    return k;
}
Kit glassBellGardenKit() {
    Kit k{"Glass Bell Garden", "Unnatural", defaultPads(), {}, {}};
    auto& pads = k.pads;

    for (int i = 0; i <= 15; ++i) {
        const int p = i;
        const bool useFriction = (i % 4 == 3);
        pads[p].exciterType = useFriction ? ExciterType::Friction
            : ((i % 3 == 0) ? ExciterType::Mallet : ExciterType::FMImpulse);
        pads[p].bodyModel = BodyModelType::Bell;
        pads[p].material = 0.40 + (i / 15.0) * 0.55;
        pads[p].size     = 0.10 + ((15 - i) / 15.0) * 0.60;
        pads[p].decay    = 0.55 + (i % 6) * 0.07;
        pads[p].level    = 0.68;
        pads[p].fmRatio  = 0.30 + std::fmod(i * 0.04, 0.65);
        pads[p].feedbackAmount = useFriction ? 0.0 : (i % 5) * 0.08;
        pads[p].frictionPressure = useFriction ? (0.30 + (i % 4) * 0.10) : 0.0;
        pads[p].modeStretch = 0.30 + (i % 7) * 0.06;
        pads[p].modeInjectAmount = 0.0;
        pads[p].nonlinearCoupling = (i % 4) * 0.10;
        pads[p].decaySkew = 0.55 + (i % 4) * 0.08;
        pads[p].morphEnabled = (i % 5 == 0) ? 1.0 : 0.0;
        if (pads[p].morphEnabled != 0.0) {
            pads[p].morphStart = 0.40 + (i % 3) * 0.15;
            pads[p].morphEnd   = 0.85;
            pads[p].morphDuration = 0.45;
            pads[p].morphCurve = 0.5;
        }
        pads[p].modeScatter = 0.30 + (i % 5) * 0.08;
        pads[p].airLoading  = 0.0;
        pads[p].couplingStrength = (i % 3 == 0) ? 0.25 : 0.0;
        pads[p].secondaryEnabled = (i % 3 == 0) ? 1.0 : 0.0;
        pads[p].secondarySize    = 0.30 + (i % 4) * 0.08;
        pads[p].secondaryMaterial = 0.85;
        pads[p].tensionModAmt = (i % 4) * 0.08;
        pads[p].noiseLayerMix = 0.05 + (i % 5) * 0.08;
        pads[p].noiseLayerCutoff = 0.85;
        pads[p].noiseLayerColor  = 0.78 - (i % 4) * 0.08;
        pads[p].noiseLayerDecay  = 0.65 + (i % 4) * 0.08;
        pads[p].clickLayerMix    = 0.32 + (i % 3) * 0.12;
        pads[p].clickLayerContactMs = 0.10 + (i % 3) * 0.06;
        pads[p].clickLayerBrightness = 0.65 + (i % 4) * 0.08;
        pads[p].bodyDampingB3 = 0.0;
        pads[p].bodyDampingB1 = 0.30 + (i % 7) * 0.04;
        pads[p].outputBus = (i % 8 == 7) ? 1 : 0;
        pads[p].macroBrightness = 0.55 + (i / 15.0) * 0.40;
        pads[p].macroComplexity = 0.55 + (i % 5) * 0.06;
        pads[p].couplingAmount = 0.55 + (i % 4) * 0.10;
    }

    k.opts.maxPolyphony    = 16;
    k.opts.globalCoupling  = 0.65;
    k.opts.couplingDelayMs = 1.8;
    for (int i = 0; i < 16; ++i) k.crafted.push_back(i);
    return k;
}

Kit droneSustainKit() {
    Kit k{"Drone and Sustain", "Unnatural", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Friction strings (0-7)
    for (int i = 0; i <= 7; ++i) {
        const int p = i;
        pads[p].exciterType = ExciterType::Friction;
        pads[p].bodyModel = (i % 2 == 0) ? BodyModelType::String : BodyModelType::Membrane;
        pads[p].material = 0.35 + (i / 7.0) * 0.50;
        pads[p].size     = 0.40 + (i % 4) * 0.10;
        pads[p].decay    = 0.92;
        pads[p].level    = 0.62;
        pads[p].frictionPressure = 0.35 + (i % 3) * 0.12;
        pads[p].modeStretch = 0.30 + (i % 6) * 0.06;
        pads[p].modeInjectAmount = 0.0;
        pads[p].nonlinearCoupling = 0.40 + (i % 4) * 0.10;
        pads[p].decaySkew = 0.85;
        pads[p].morphEnabled = (i % 3 == 0) ? 1.0 : 0.0;
        if (pads[p].morphEnabled != 0.0) {
            pads[p].morphStart = 0.40; pads[p].morphEnd = 0.85;
            pads[p].morphDuration = 0.85; pads[p].morphCurve = 0.5;
        }
        pads[p].tsFilterType = FilterType::LP;
        pads[p].tsFilterCutoff = 0.45;
        pads[p].tsFilterResonance = 0.35;
        pads[p].tsFilterEnvAmount = 0.55;
        pads[p].tsFilterEnvAttack = 0.20;
        pads[p].tsFilterEnvDecay  = 0.40;
        pads[p].tsFilterEnvSustain = 0.55;
        pads[p].tsFilterEnvRelease = 0.65;
        pads[p].modeScatter = 0.10 + (i % 4) * 0.08;
        pads[p].airLoading  = 0.0;
        pads[p].couplingStrength = 0.18; pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize = 0.50; pads[p].secondaryMaterial = 0.65;
        pads[p].tensionModAmt = 0.25 + (i % 4) * 0.10;
        pads[p].noiseLayerMix = 0.18 + (i % 4) * 0.08;
        pads[p].noiseLayerCutoff = 0.50;
        pads[p].noiseLayerColor  = 0.40;
        pads[p].noiseLayerDecay  = 0.85;
        pads[p].clickLayerMix    = 0.0;
        pads[p].bodyDampingB1 = 0.30 + (i % 6) * 0.02;
        pads[p].bodyDampingB3 = 0.18 + (i % 4) * 0.05;
        pads[p].outputBus = i % 2;
        pads[p].macroComplexity = 0.78;
        pads[p].couplingAmount = 0.85;
    }

    // Feedback drones (8-13)
    for (int i = 8; i <= 13; ++i) {
        const int p = i;
        pads[p].exciterType = ExciterType::Feedback;
        pads[p].bodyModel = (i % 2 == 0) ? BodyModelType::Plate : BodyModelType::Shell;
        pads[p].material = 0.40 + ((i - 8) / 6.0) * 0.45;
        pads[p].size     = 0.45 + (i % 3) * 0.12;
        pads[p].decay    = 0.92;
        pads[p].level    = 0.62;
        pads[p].feedbackAmount = 0.40 + (i % 4) * 0.10;
        pads[p].modeStretch = 0.25 + (i % 5) * 0.08;
        pads[p].modeInjectAmount = 0.0;
        pads[p].nonlinearCoupling = 0.55;
        pads[p].decaySkew = 0.78;
        pads[p].tsFilterType = FilterType::BP;
        pads[p].tsFilterCutoff = 0.55;
        pads[p].tsFilterResonance = 0.45;
        pads[p].tsFilterEnvAmount = 0.30;
        pads[p].modeScatter = 0.40;
        pads[p].airLoading  = 0.20;
        pads[p].couplingStrength = 0.30;
        pads[p].tensionModAmt = 0.45;
        pads[p].noiseLayerMix = 0.30; pads[p].noiseLayerCutoff = 0.55;
        pads[p].noiseLayerColor = 0.55; pads[p].noiseLayerDecay = 0.85;
        pads[p].clickLayerMix = 0.0;
        pads[p].bodyDampingB1 = 0.30; pads[p].bodyDampingB3 = 0.20;
        pads[p].outputBus = 1;
        pads[p].couplingAmount = 0.85;
    }

    k.opts.maxPolyphony    = 8;
    k.opts.stealingPolicy  = 1;
    k.opts.globalCoupling  = 0.85;
    k.opts.tomResonance    = 0.65;
    k.opts.couplingDelayMs = 1.9;
    for (int i = 0; i < 14; ++i) k.crafted.push_back(i);
    return k;
}

Kit chaosEngineKit() {
    Kit k{"Chaos Engine", "Unnatural", defaultPads(), {}, {}};
    auto& pads = k.pads;
    const int bodyCycle[] = {BodyModelType::Plate, BodyModelType::Shell,
                             BodyModelType::String, BodyModelType::Bell,
                             BodyModelType::Membrane, BodyModelType::NoiseBody};
    const double filterCycle[] = {FilterType::LP, FilterType::HP, FilterType::BP};

    for (int i = 0; i <= 13; ++i) {
        const int p = i;
        pads[p].exciterType = (i % 3 == 0) ? ExciterType::Feedback
            : (i % 3 == 1) ? ExciterType::FMImpulse : ExciterType::Friction;
        pads[p].bodyModel = bodyCycle[i % 6];
        pads[p].material = 0.20 + std::fmod(i * 0.06, 0.75);
        pads[p].size     = 0.25 + std::fmod(i * 0.05, 0.65);
        pads[p].decay    = 0.40 + (i % 7) * 0.07;
        pads[p].level    = 0.72;
        pads[p].fmRatio  = 0.20 + std::fmod(i * 0.07, 0.70);
        pads[p].feedbackAmount   = 0.40 + (i % 4) * 0.12;
        pads[p].frictionPressure = 0.30 + (i % 3) * 0.15;
        pads[p].modeStretch      = 0.40 + (i % 5) * 0.12;
        pads[p].modeInjectAmount = 0.0;
        pads[p].nonlinearCoupling = 0.65 + (i % 3) * 0.10;
        pads[p].decaySkew        = 0.40 + (i % 5) * 0.12;
        pads[p].morphEnabled = (i % 2 == 0) ? 1.0 : 0.0;
        if (pads[p].morphEnabled != 0.0) {
            pads[p].morphStart = (i % 4) * 0.20;
            pads[p].morphEnd   = 0.85 - (i % 3) * 0.20;
            pads[p].morphDuration = 0.20 + (i % 5) * 0.15;
            pads[p].morphCurve = (i % 7) * 0.14;
        }
        pads[p].tsFilterType = filterCycle[i % 3];
        pads[p].tsFilterCutoff    = 0.30 + std::fmod(i * 0.07, 0.60);
        pads[p].tsFilterResonance = 0.55 + (i % 4) * 0.10;
        pads[p].tsFilterEnvAmount = 0.55;
        pads[p].tsFilterEnvAttack = (i % 5) * 0.04;
        pads[p].tsFilterEnvDecay  = 0.10 + (i % 4) * 0.10;
        pads[p].tsDriveAmount     = 0.30 + (i % 4) * 0.12;
        pads[p].tsFoldAmount      = 0.20 + (i % 3) * 0.18;
        pads[p].modeScatter       = 0.55 + (i % 5) * 0.08;
        pads[p].airLoading        = (i % 4) * 0.20;
        pads[p].couplingStrength  = 0.40 + (i % 3) * 0.15;
        pads[p].secondaryEnabled  = 1.0;
        pads[p].secondarySize     = 0.30 + std::fmod(i * 0.05, 0.50);
        pads[p].secondaryMaterial = std::fmod(i * 0.07, 1.0);
        pads[p].tensionModAmt     = 0.78 + (i % 4) * 0.05;
        pads[p].noiseLayerMix     = 0.20 + (i % 5) * 0.10;
        pads[p].noiseLayerCutoff  = 0.30 + (i % 6) * 0.10;
        pads[p].noiseLayerResonance = 0.30 + (i % 4) * 0.12;
        pads[p].noiseLayerColor   = 0.30 + (i % 5) * 0.12;
        pads[p].noiseLayerDecay   = 0.40 + (i % 7) * 0.08;
        pads[p].clickLayerMix     = 0.20 + (i % 5) * 0.10;
        pads[p].clickLayerContactMs = 0.10 + (i % 4) * 0.10;
        pads[p].clickLayerBrightness = 0.40 + (i % 5) * 0.12;
        pads[p].bodyDampingB1 = 0.30 + (i % 6) * 0.05;
        pads[p].bodyDampingB3 = (pads[p].bodyModel == BodyModelType::Bell) ? 0.0
                              : (0.20 + (i % 4) * 0.10);
        pads[p].chokeGroup    = (i < 4) ? 0 : ((i % 2) + 1);
        pads[p].outputBus     = i % 4;
        pads[p].tsPitchEnvStart = toLogNorm(180 + std::fmod(i * 30.0, 350.0));
        pads[p].tsPitchEnvEnd   = toLogNorm(40 + std::fmod(i * 12.0, 200.0));
        pads[p].tsPitchEnvTime  = 0.04 + (i % 5) * 0.04;
        pads[p].tsPitchEnvCurve = (i % 2) ? 1.0 : 0.0;
        pads[p].macroTightness  = 0.20 + (i % 5) * 0.15;
        pads[p].macroBrightness = 0.30 + (i % 4) * 0.18;
        pads[p].macroBodySize   = 0.30 + (i % 5) * 0.14;
        pads[p].macroPunch      = 0.40 + (i % 4) * 0.15;
        pads[p].macroComplexity = 0.85;
        pads[p].couplingAmount  = 0.78;
    }

    k.opts.maxPolyphony    = 12;
    k.opts.stealingPolicy  = 2;
    k.opts.globalCoupling  = 0.92;
    k.opts.snareBuzz       = 0.55;
    k.opts.tomResonance    = 0.78;
    k.opts.couplingDelayMs = 1.7;
    for (int i = 0; i < 14; ++i) k.crafted.push_back(i);
    return k;
}

Kit ghostBonesKit() {
    Kit k{"Ghost Bones", "Unnatural", defaultPads(), {}, {}};
    auto& pads = k.pads;

    for (int i = 0; i <= 13; ++i) {
        const int p = i;
        const bool useFriction = (i % 5 == 4);
        pads[p].exciterType = useFriction ? ExciterType::Friction
            : (i % 3 == 0) ? ExciterType::Mallet
            : (i % 3 == 1) ? ExciterType::FMImpulse : ExciterType::Impulse;
        pads[p].bodyModel = (i % 2 == 0) ? BodyModelType::Bell : BodyModelType::String;
        pads[p].material = 0.30 + std::fmod(i * 0.04, 0.55);
        pads[p].size     = 0.20 + ((13 - i) / 13.0) * 0.55;
        pads[p].decay    = 0.85 - (i % 5) * 0.05;
        pads[p].level    = 0.65;
        pads[p].fmRatio  = 0.30 + std::fmod(i * 0.05, 0.50);
        pads[p].frictionPressure = useFriction ? 0.30 : 0.0;
        pads[p].modeStretch      = 0.65 + (i % 4) * 0.08;
        pads[p].modeInjectAmount = 0.0;
        pads[p].nonlinearCoupling = 0.20 + (i % 4) * 0.08;
        pads[p].decaySkew        = 0.78 + (i % 3) * 0.06;
        pads[p].morphEnabled = (i % 3 != 0) ? 1.0 : 0.0;
        if (pads[p].morphEnabled != 0.0) {
            pads[p].morphStart = 0.40 + (i % 4) * 0.10;
            pads[p].morphEnd   = 0.80;
            pads[p].morphDuration = 0.65 + (i % 3) * 0.10;
            pads[p].morphCurve = 0.35 + (i % 4) * 0.10;
        }
        pads[p].tsFilterType = FilterType::HP;
        pads[p].tsFilterCutoff = 0.18 + (i % 4) * 0.10;
        pads[p].tsFilterResonance = 0.30;
        pads[p].tsFilterEnvAmount = 0.45;
        pads[p].tsFilterEnvDecay  = 0.30;
        pads[p].modeScatter = 0.40 + (i % 4) * 0.10;
        pads[p].airLoading  = 0.0;
        pads[p].couplingStrength = (i % 2) * 0.30;
        pads[p].secondaryEnabled = (i % 2) ? 1.0 : 0.0;
        pads[p].secondarySize    = 0.25 + (i % 3) * 0.08;
        pads[p].secondaryMaterial = 0.85;
        pads[p].tensionModAmt = 0.30 + (i % 4) * 0.12;
        pads[p].noiseLayerMix = 0.10 + (i % 5) * 0.06;
        pads[p].noiseLayerCutoff = 0.30 + (i % 6) * 0.10;
        pads[p].noiseLayerColor  = 0.20 + (i % 4) * 0.10;
        pads[p].noiseLayerDecay  = 0.85;
        pads[p].clickLayerMix    = 0.10 + (i % 4) * 0.08;
        pads[p].clickLayerBrightness = 0.55 + (i % 4) * 0.08;
        pads[p].bodyDampingB1 = 0.30 + (i % 5) * 0.02;
        pads[p].bodyDampingB3 = (pads[p].bodyModel == BodyModelType::Bell) ? 0.0 : 0.18;
        pads[p].outputBus = (i % 4 == 3) ? 1 : 0;
        pads[p].macroBrightness = 0.30 + (i % 4) * 0.12;
        pads[p].macroComplexity = 0.85;
        pads[p].macroBodySize   = 0.55;
        pads[p].couplingAmount  = 0.85;
    }

    k.opts.maxPolyphony    = 12;
    k.opts.globalCoupling  = 0.78;
    k.opts.tomResonance    = 0.55;
    k.opts.couplingDelayMs = 2.0;
    for (int i = 0; i < 14; ++i) k.crafted.push_back(i);
    return k;
}
