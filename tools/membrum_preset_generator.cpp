// ==============================================================================
// Factory Kit Preset Generator for Membrum
// ==============================================================================
// Generates .vstpreset files containing kit blobs in the v3 state codec
// format produced by Membrum::State::writeKitBlob (see state_codec.{h,cpp}).
//
// On-wire layout (little-endian):
//   [int32 version = 3]
//   [int32 maxPolyphony]
//   [int32 stealPolicy]
//   For each of 32 pads:
//     [int32 exciterType][int32 bodyModel]
//     [57 x float64 sound, with choke/bus mirrored at indices 28/29]
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
constexpr int kVersion           = 3;  // M-9: 57-slot sound array (added pan)
constexpr int kSoundSlotsPerPad  = 57;

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
    // Phase 10: tsPitchEnvCurve is now a continuous segment-1 curve amount.
    // Norm 0.5 -> curveAmount 0 (linear); 0.15 -> curveAmount -0.7 (matches the
    // old StringList "Exp" / EnvCurve::Logarithmic decay shape); 0.5 -> linear
    // (matches old "Lin"). Defaults to 0.15 to preserve the historical
    // exponential drop shape -- per-preset overrides explicit-set as needed.
    double tsPitchEnvCurve = 0.15;
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

    // Phase 10: three-point pitch envelope extension. Defaults: knee OFF,
    // mid pitch at the midpoint of the 20..2000 Hz log scale (norm 0.5),
    // mid fraction 0.5, segment-2 curve linear (norm 0.5 -> curveAmount 0).
    double tsPitchEnvKneeEnabled = 0.0;
    double tsPitchEnvMidPitch    = 0.5;
    double tsPitchEnvMidFraction = 0.5;
    double tsPitchEnvCurve2      = 0.5;
    // M-9: per-pad pan. 0.5 = center (equal-power, unity to both channels).
    double pan = 0.5;
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

    // 57 x float64 -- layout matches PadSnapshot::sound.
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
        // [52..55] Phase 10 three-point pitch envelope extension.
        p.tsPitchEnvKneeEnabled,
        p.tsPitchEnvMidPitch,
        p.tsPitchEnvMidFraction,
        p.tsPitchEnvCurve2,
        // [56] M-9 per-pad pan.
        p.pan,
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

    // ---- Pad 0: Acoustic Kick (Membrane/Mallet) ----
    pads[0].exciterType = ExciterType::Mallet;
    pads[0].bodyModel   = BodyModelType::Membrane;
    pads[0].material = 0.35;
    pads[0].size = 0.85;
    pads[0].decay = 0.25;
    pads[0].strikePosition = 0.30;
    pads[0].level = 0.85;
    pads[0].tsPitchEnvStart = toLogNorm(160);
    pads[0].tsPitchEnvEnd   = toLogNorm(50);
    pads[0].tsPitchEnvTime  = 0.08;   // 40 ms
    pads[0].tsPitchEnvCurve = 0.15;
    pads[0].bodyDampingB1 = 0.13;
    pads[0].bodyDampingB3 = 0.42;
    pads[0].airLoading = 0.78;
    pads[0].tensionModAmt = 0.18;
    pads[0].couplingStrength  = 0.35;
    pads[0].secondaryEnabled  = 1.0;
    pads[0].secondarySize     = 0.40;
    pads[0].secondaryMaterial = 0.60;
    pads[0].clickLayerMix        = 0.70;
    pads[0].clickLayerContactMs  = 0.20;
    pads[0].clickLayerBrightness = 0.45;
    pads[0].noiseLayerMix    = 0.10;
    pads[0].noiseLayerCutoff = 0.45;
    pads[0].noiseLayerColor  = 0.12;  // Brown
    pads[0].noiseLayerDecay  = 0.20;
    pads[0].pan = 0.50;

    // ---- Pad 1: Side Stick (Shell/Impulse) ----
    pads[1].exciterType = ExciterType::Impulse;
    pads[1].bodyModel   = BodyModelType::Shell;
    pads[1].material = 0.30;
    pads[1].size = 0.20;
    pads[1].decay = 0.16;
    pads[1].strikePosition = 0.30;
    pads[1].level = 0.78;
    pads[1].bodyDampingB1 = 0.42;
    pads[1].bodyDampingB3 = 0.10;
    pads[1].modeScatter = 0.50;
    pads[1].clickLayerMix        = 0.88;
    pads[1].clickLayerContactMs  = 0.10;
    pads[1].clickLayerBrightness = 0.62;
    pads[1].noiseLayerMix = 0.0;
    pads[1].airLoading = 0.0;   // Membrane-only no-op on Shell
    pads[1].pan = 0.58;

    // ---- Pad 2: Acoustic Snare (Membrane/NoiseBurst) -- crack + wires + shell ----
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel   = BodyModelType::Membrane;
    pads[2].material = 0.50;
    pads[2].size = 0.42;            // ~190 Hz head
    pads[2].decay = 0.30;
    pads[2].strikePosition = 0.35;
    pads[2].level = 0.92;
    pads[2].noiseBurstDuration = (4.0 - 2.0) / 13.0;  // ~4 ms
    pads[2].tsFilterType       = FilterType::LP;
    pads[2].tsFilterCutoff     = 0.92;
    pads[2].tsFilterResonance  = 0.12;
    pads[2].tsFilterEnvAmount  = 0.72;
    pads[2].tsFilterEnvDecay   = 0.32;
    pads[2].tsFilterEnvRelease = 0.15;
    pads[2].tsPitchEnvStart = toLogNorm(200);
    pads[2].tsPitchEnvEnd   = toLogNorm(130);
    pads[2].tsPitchEnvTime  = 0.07;   // 35 ms
    pads[2].tsPitchEnvCurve = 0.35;
    pads[2].tsDriveAmount = 0.08;
    pads[2].nonlinearCoupling = 0.22;
    pads[2].modeScatter = 0.28;
    pads[2].bodyDampingB1 = 0.28;
    pads[2].bodyDampingB3 = 0.03;
    pads[2].airLoading = 0.42;
    pads[2].noiseLayerMix       = 0.82;
    pads[2].noiseLayerCutoff    = 0.90;
    pads[2].noiseLayerResonance = 0.10;
    pads[2].noiseLayerDecay     = 0.48;
    pads[2].noiseLayerColor     = 0.90;  // Violet
    pads[2].clickLayerMix        = 0.92;
    pads[2].clickLayerContactMs  = 0.18;
    pads[2].clickLayerBrightness = 0.90;
    pads[2].couplingStrength  = 0.78;
    pads[2].secondaryEnabled  = 1.0;
    pads[2].secondarySize     = 0.78;
    pads[2].secondaryMaterial = 0.52;
    pads[2].tensionModAmt = 0.16;
    pads[2].pan = 0.50;

    // ---- Pad 3: Hand Clap (NoiseBody/NoiseBurst) -- cupped-hand formant ----
    pads[3].exciterType = ExciterType::NoiseBurst;
    pads[3].bodyModel   = BodyModelType::NoiseBody;
    pads[3].material = 0.85;
    pads[3].size = 0.18;
    pads[3].decay = 0.18;
    pads[3].level = 0.80;
    pads[3].noiseBurstDuration = 0.55;   // ~9 ms spread
    pads[3].noiseLayerMix       = 0.85;
    pads[3].noiseLayerCutoff    = 0.78;
    pads[3].noiseLayerResonance = 0.40;  // Q~2.18 clap formant
    pads[3].noiseLayerDecay     = 0.20;
    pads[3].noiseLayerColor     = 0.70;  // White
    pads[3].clickLayerMix        = 0.45;
    pads[3].clickLayerContactMs  = 0.22;
    pads[3].clickLayerBrightness = 0.62;
    pads[3].modeScatter = 0.40;
    pads[3].bodyDampingB1 = 0.50;
    pads[3].bodyDampingB3 = 0.0;
    pads[3].airLoading = 0.0;   // no-op on NoiseBody
    pads[3].pan = 0.44;

    // ---- Pad 4: Rim Shot (Shell/Impulse) -- 2nd snare articulation ----
    pads[4].exciterType = ExciterType::Impulse;
    pads[4].bodyModel   = BodyModelType::Shell;
    pads[4].material = 0.70;
    pads[4].size = 0.34;            // ~686 Hz, rim-crack band
    pads[4].decay = 0.18;
    pads[4].strikePosition = 0.15;  // near-edge
    pads[4].level = 0.88;
    pads[4].modeStretch = 0.45;
    pads[4].clickLayerMix        = 0.95;
    pads[4].clickLayerContactMs  = 0.08;
    pads[4].clickLayerBrightness = 0.92;
    pads[4].noiseLayerMix    = 0.20;
    pads[4].noiseLayerCutoff = 0.85;
    pads[4].noiseLayerColor  = 0.90;  // Violet
    pads[4].modeScatter = 0.40;
    pads[4].bodyDampingB1 = 0.50;
    pads[4].bodyDampingB3 = 0.08;
    pads[4].airLoading = 0.0;   // no-op on Shell
    pads[4].pan = 0.50;

    // ---- Toms (Membrane/Mallet) -- true per-pad graded gradient ----
    const int    tomPads[]       = {5, 7, 9, 11, 12, 14};
    const double tomSizes[]      = {0.80, 0.70, 0.60, 0.55, 0.45, 0.40};
    const double tomPitchStart[] = { 200,  250,  300,  290,  380,  470};
    const double tomPitchEnd[]   = { 110,  130,  150,  180,  230,  290};
    const double tomPitchTime[]  = {0.18, 0.16, 0.14, 0.13, 0.10, 0.08};  // 90..40 ms
    const double tomB1[]         = {0.30, 0.32, 0.34, 0.34, 0.37, 0.40};
    const double tomAir[]        = {0.65, 0.60, 0.55, 0.55, 0.48, 0.45};
    const double tomSecSize[]    = {0.32, 0.31, 0.30, 0.32, 0.30, 0.29};
    const double tomSkew[]       = {0.42, 0.44, 0.46, 0.48, 0.52, 0.55};
    const double tomPan[]        = {0.30, 0.38, 0.46, 0.54, 0.62, 0.70};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel   = BodyModelType::Membrane;
        pads[p].material = 0.40;
        pads[p].size = tomSizes[i];
        pads[p].decay = 0.50;
        pads[p].strikePosition = 0.35;
        pads[p].level = 0.80;
        pads[p].tsPitchEnvStart = toLogNorm(tomPitchStart[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomPitchEnd[i]);
        pads[p].tsPitchEnvTime  = tomPitchTime[i];
        pads[p].tsPitchEnvCurve = 0.15;
        pads[p].bodyDampingB1 = tomB1[i];
        pads[p].bodyDampingB3 = 0.10;
        pads[p].airLoading = tomAir[i];
        pads[p].tensionModAmt = 0.22;
        pads[p].couplingStrength  = 0.40;
        pads[p].secondaryEnabled  = 1.0;
        pads[p].secondarySize     = tomSecSize[i];
        pads[p].secondaryMaterial = 0.55;
        pads[p].decaySkew = tomSkew[i];
        pads[p].nonlinearCoupling = 0.12;
        pads[p].modeScatter = 0.08;
        pads[p].noiseLayerMix    = 0.16;
        pads[p].noiseLayerCutoff = 0.45;
        pads[p].noiseLayerColor  = 0.40;  // Pink
        pads[p].clickLayerMix        = 0.50;
        pads[p].clickLayerContactMs  = 0.32;
        pads[p].clickLayerBrightness = 0.55;
        pads[p].pan = tomPan[i];
    }

    // ---- Pad 6: Closed Hi-Hat (NoiseBody/NoiseBurst), choke 1 ----
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel   = BodyModelType::NoiseBody;
    pads[6].material = 0.88;
    pads[6].size = 0.10;
    pads[6].decay = 0.10;
    pads[6].strikePosition = 0.60;
    pads[6].level = 0.72;
    pads[6].chokeGroup = 1;
    pads[6].modeStretch = 0.50;
    pads[6].modeScatter = 0.30;
    pads[6].noiseBurstDuration = (3.0 - 2.0) / 13.0;  // 3 ms
    pads[6].noiseLayerMix       = 0.70;
    pads[6].noiseLayerCutoff    = 0.86;
    pads[6].noiseLayerResonance = 0.20;
    pads[6].noiseLayerDecay     = 0.10;
    pads[6].noiseLayerColor     = 0.90;  // Violet
    pads[6].clickLayerMix        = 0.18;
    pads[6].clickLayerBrightness = 0.85;
    pads[6].bodyDampingB1 = 0.55;
    pads[6].bodyDampingB3 = 0.0;
    pads[6].airLoading = 0.0;
    pads[6].pan = 0.62;

    // ---- Pad 8: Pedal Hi-Hat (NoiseBody/NoiseBurst), choke 1 ----
    pads[8].exciterType = ExciterType::NoiseBurst;
    pads[8].bodyModel   = BodyModelType::NoiseBody;
    pads[8].material = 0.88;
    pads[8].size = 0.12;
    pads[8].decay = 0.05;
    pads[8].level = 0.68;
    pads[8].chokeGroup = 1;
    pads[8].noiseLayerMix    = 0.70;
    pads[8].noiseLayerCutoff = 0.88;
    pads[8].noiseLayerDecay  = 0.07;
    pads[8].noiseLayerColor  = 0.90;  // Violet
    pads[8].clickLayerMix = 0.0;
    pads[8].bodyDampingB1 = 0.50;
    pads[8].bodyDampingB3 = 0.0;
    pads[8].airLoading = 0.0;
    pads[8].pan = 0.62;

    // ---- Pad 10: Open Hi-Hat (NoiseBody/NoiseBurst), choke 1, HP, long tail ----
    pads[10].exciterType = ExciterType::NoiseBurst;
    pads[10].bodyModel   = BodyModelType::NoiseBody;
    pads[10].material = 0.90;
    pads[10].size = 0.18;
    pads[10].decay = 0.55;
    pads[10].strikePosition = 0.45;
    pads[10].level = 0.72;
    pads[10].chokeGroup = 1;
    pads[10].tsFilterType      = FilterType::HP;
    pads[10].tsFilterCutoff    = 0.534;
    pads[10].tsFilterResonance = 0.20;
    pads[10].noiseLayerMix       = 0.70;
    pads[10].noiseLayerCutoff    = 0.867;
    pads[10].noiseLayerResonance = 0.25;
    pads[10].noiseLayerDecay     = 0.70;
    pads[10].noiseLayerColor     = 0.90;  // Violet
    pads[10].clickLayerMix        = 0.22;
    pads[10].clickLayerContactMs  = 0.20;
    pads[10].clickLayerBrightness = 0.85;
    pads[10].bodyDampingB1 = 0.30;
    pads[10].bodyDampingB3 = 0.0;
    pads[10].modeScatter = 0.45;
    pads[10].airLoading = 0.0;
    pads[10].pan = 0.62;  // shared hat image (override of archetype 0.42)

    // ---- Pad 13: Crash 1 (NoiseBody/NoiseBurst), bus 1, bloom ----
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel   = BodyModelType::NoiseBody;
    pads[13].material = 0.93;
    pads[13].size = 0.35;
    pads[13].decay = 0.70;
    pads[13].strikePosition = 0.55;
    pads[13].level = 0.72;
    pads[13].modeStretch = 0.60;
    pads[13].modeInjectAmount = 0.25;
    pads[13].nonlinearCoupling = 0.35;
    pads[13].modeScatter = 0.60;
    pads[13].decaySkew = 0.58;
    pads[13].bodyDampingB1 = 0.30;
    pads[13].bodyDampingB3 = 0.0;
    pads[13].noiseLayerMix    = 0.50;
    pads[13].noiseLayerCutoff = 0.85;
    pads[13].noiseLayerColor  = 0.79;  // White-Violet
    pads[13].noiseLayerDecay  = 0.60;
    pads[13].clickLayerMix        = 0.20;
    pads[13].clickLayerContactMs  = 0.30;
    pads[13].clickLayerBrightness = 0.82;
    pads[13].airLoading = 0.0;
    pads[13].outputBus = 1;
    pads[13].pan = 0.40;

    // ---- Pad 15: Ride 1 (Bell/NoiseBurst), bus 1 ----
    pads[15].exciterType = ExciterType::NoiseBurst;
    pads[15].bodyModel   = BodyModelType::Bell;
    pads[15].material = 0.95;
    pads[15].size = 0.30;            // ~400 Hz
    pads[15].decay = 0.90;
    pads[15].strikePosition = 0.18;
    pads[15].level = 0.72;
    pads[15].modeStretch = 0.45;
    pads[15].decaySkew = 0.62;
    pads[15].nonlinearCoupling = 0.18;
    pads[15].modeScatter = 0.55;
    pads[15].bodyDampingB1 = 0.16;
    pads[15].bodyDampingB3 = 0.0;
    pads[15].noiseLayerMix    = 0.45;
    pads[15].noiseLayerCutoff = 0.90;
    pads[15].noiseLayerDecay  = 0.78;
    pads[15].noiseLayerColor  = 0.90;  // Violet
    pads[15].clickLayerMix        = 0.45;
    pads[15].clickLayerContactMs  = 0.25;
    pads[15].clickLayerBrightness = 0.82;
    pads[15].airLoading = 0.0;
    pads[15].outputBus = 1;
    pads[15].pan = 0.62;

    // ---- Pad 17: Ride Bell / cup (Bell/NoiseBurst), bus 1 ----
    pads[17].exciterType = ExciterType::NoiseBurst;
    pads[17].bodyModel   = BodyModelType::Bell;
    pads[17].material = 0.95;
    pads[17].size = 0.26;
    pads[17].decay = 0.72;
    pads[17].strikePosition = 0.05;  // cup
    pads[17].level = 0.74;
    pads[17].modeStretch = 0.40;
    pads[17].decaySkew = 0.60;
    pads[17].nonlinearCoupling = 0.20;
    pads[17].modeScatter = 0.40;
    pads[17].bodyDampingB1 = 0.18;
    pads[17].bodyDampingB3 = 0.0;
    pads[17].noiseLayerMix    = 0.30;
    pads[17].noiseLayerCutoff = 0.88;
    pads[17].noiseLayerDecay  = 0.55;
    pads[17].noiseLayerColor  = 0.90;  // Violet
    pads[17].clickLayerMix        = 0.55;
    pads[17].clickLayerBrightness = 0.85;
    pads[17].airLoading = 0.0;
    pads[17].outputBus = 1;
    pads[17].pan = 0.64;

    // ---- Pad 18: Tambourine / Pandeiro (NoiseBody/NoiseBurst), choke 1 ----
    pads[18].exciterType = ExciterType::NoiseBurst;
    pads[18].bodyModel   = BodyModelType::NoiseBody;
    pads[18].material = 0.92;
    pads[18].size = 0.15;
    pads[18].decay = 0.25;
    pads[18].level = 0.74;
    pads[18].chokeGroup = 1;
    pads[18].noiseBurstDuration = 0.40;  // ~7 ms
    pads[18].noiseLayerMix       = 0.65;
    pads[18].noiseLayerCutoff    = 0.92;
    pads[18].noiseLayerResonance = 0.20;
    pads[18].noiseLayerDecay     = 0.30;
    pads[18].noiseLayerColor     = 0.90;  // Violet
    pads[18].modeScatter = 0.50;
    pads[18].modeStretch = 0.55;
    pads[18].bodyDampingB3 = 0.0;
    pads[18].clickLayerMix = 0.30;
    pads[18].airLoading = 0.0;
    pads[18].pan = 0.56;

    // ---- Pad 19: Splash (NoiseBody/NoiseBurst), bus 1 ----
    pads[19].exciterType = ExciterType::NoiseBurst;
    pads[19].bodyModel   = BodyModelType::NoiseBody;
    pads[19].material = 0.94;
    pads[19].size = 0.20;
    pads[19].decay = 0.30;
    pads[19].strikePosition = 0.55;
    pads[19].level = 0.70;
    pads[19].modeStretch = 0.55;
    pads[19].nonlinearCoupling = 0.28;
    pads[19].modeScatter = 0.60;
    pads[19].decaySkew = 0.55;
    pads[19].bodyDampingB1 = 0.40;
    pads[19].bodyDampingB3 = 0.0;
    pads[19].noiseLayerMix       = 0.50;
    pads[19].noiseLayerCutoff    = 0.90;
    pads[19].noiseLayerResonance = 0.20;
    pads[19].noiseLayerDecay     = 0.25;
    pads[19].noiseLayerColor     = 0.90;  // Violet
    pads[19].clickLayerMix = 0.22;
    pads[19].airLoading = 0.0;
    pads[19].outputBus = 1;
    pads[19].pan = 0.36;

    // ---- Pad 20: Cowbell (Bell/FMImpulse), live FM clang ----
    pads[20].exciterType = ExciterType::FMImpulse;
    pads[20].bodyModel   = BodyModelType::Bell;
    pads[20].material = 0.78;
    pads[20].size = 0.26;
    pads[20].decay = 0.30;
    pads[20].level = 0.76;
    pads[20].fmRatio = 0.50;   // mod ratio 2.5, detuned fifth
    pads[20].modeStretch = 0.55;
    pads[20].decaySkew = 0.42;
    pads[20].clickLayerMix        = 0.55;
    pads[20].clickLayerBrightness = 0.70;
    pads[20].noiseLayerMix    = 0.10;
    pads[20].noiseLayerCutoff = 0.62;
    pads[20].noiseLayerColor  = 0.40;  // Pink
    pads[20].noiseLayerDecay  = 0.20;
    pads[20].modeScatter = 0.20;
    pads[20].bodyDampingB1 = 0.40;
    pads[20].bodyDampingB3 = 0.0;
    pads[20].airLoading = 0.0;
    pads[20].pan = 0.46;

    // ---- Pad 21: Crash 2 (NoiseBody/NoiseBurst), dark, bus 1 ----
    pads[21].exciterType = ExciterType::NoiseBurst;
    pads[21].bodyModel   = BodyModelType::NoiseBody;
    pads[21].material = 0.90;
    pads[21].size = 0.40;
    pads[21].decay = 0.75;
    pads[21].strikePosition = 0.55;
    pads[21].level = 0.72;
    pads[21].modeStretch = 0.60;
    pads[21].modeInjectAmount = 0.25;
    pads[21].nonlinearCoupling = 0.35;
    pads[21].modeScatter = 0.65;
    pads[21].decaySkew = 0.56;
    pads[21].bodyDampingB1 = 0.28;
    pads[21].bodyDampingB3 = 0.0;
    pads[21].noiseLayerMix    = 0.50;
    pads[21].noiseLayerCutoff = 0.80;
    pads[21].noiseLayerColor  = 0.70;  // White
    pads[21].noiseLayerDecay  = 0.65;
    pads[21].clickLayerMix = 0.20;
    pads[21].airLoading = 0.0;
    pads[21].outputBus = 1;
    pads[21].pan = 0.58;

    // ---- Pad 22: Cabasa / Shaker (NoiseBody/NoiseBurst) ----
    pads[22].exciterType = ExciterType::NoiseBurst;
    pads[22].bodyModel   = BodyModelType::NoiseBody;
    pads[22].material = 0.80;
    pads[22].size = 0.20;
    pads[22].decay = 0.12;
    pads[22].level = 0.70;
    pads[22].noiseBurstDuration = 0.20;  // ~5 ms
    pads[22].noiseLayerMix       = 0.80;
    pads[22].noiseLayerCutoff    = 0.88;
    pads[22].noiseLayerResonance = 0.16;
    pads[22].noiseLayerDecay     = 0.10;
    pads[22].noiseLayerColor     = 0.90;  // Violet
    pads[22].clickLayerMix = 0.10;
    pads[22].bodyDampingB1 = 0.55;
    pads[22].bodyDampingB3 = 0.05;
    pads[22].airLoading = 0.0;
    pads[22].pan = 0.66;

    // ---- Pad 23: Ride 2 (Bell/NoiseBurst), dark, bus 1 ----
    pads[23].exciterType = ExciterType::NoiseBurst;
    pads[23].bodyModel   = BodyModelType::Bell;
    pads[23].material = 0.92;
    pads[23].size = 0.34;
    pads[23].decay = 0.92;
    pads[23].strikePosition = 0.18;
    pads[23].level = 0.72;
    pads[23].modeStretch = 0.45;
    pads[23].decaySkew = 0.60;
    pads[23].nonlinearCoupling = 0.18;
    pads[23].modeScatter = 0.58;
    pads[23].bodyDampingB1 = 0.15;
    pads[23].bodyDampingB3 = 0.0;
    pads[23].noiseLayerMix    = 0.42;
    pads[23].noiseLayerCutoff = 0.85;
    pads[23].noiseLayerDecay  = 0.78;
    pads[23].noiseLayerColor  = 0.79;  // White-Violet
    pads[23].clickLayerMix        = 0.45;
    pads[23].clickLayerBrightness = 0.80;
    pads[23].airLoading = 0.0;
    pads[23].outputBus = 1;
    pads[23].pan = 0.58;

    // ---- Pad 24: Bongo Hi / macho (Membrane/Impulse) ----
    pads[24].exciterType = ExciterType::Impulse;
    pads[24].bodyModel   = BodyModelType::Membrane;
    pads[24].material = 0.50;
    pads[24].size = 0.32;
    pads[24].decay = 0.22;
    pads[24].strikePosition = 0.40;
    pads[24].level = 0.78;
    pads[24].tsPitchEnvStart = toLogNorm(420);
    pads[24].tsPitchEnvEnd   = toLogNorm(350);
    pads[24].tsPitchEnvTime  = 0.04;   // 20 ms
    pads[24].tsPitchEnvCurve = 0.15;
    pads[24].airLoading = 0.40;
    pads[24].tensionModAmt = 0.25;
    pads[24].bodyDampingB1 = 0.40;
    pads[24].bodyDampingB3 = 0.08;
    pads[24].couplingStrength  = 0.25;
    pads[24].secondaryEnabled  = 1.0;
    pads[24].secondarySize     = 0.30;
    pads[24].secondaryMaterial = 0.40;
    pads[24].clickLayerMix        = 0.70;
    pads[24].clickLayerContactMs  = 0.15;
    pads[24].clickLayerBrightness = 0.75;
    pads[24].noiseLayerMix = 0.08;
    pads[24].modeScatter = 0.10;
    pads[24].pan = 0.40;

    // ---- Pad 25: Bongo Lo / hembra (Membrane/Impulse) ----
    pads[25].exciterType = ExciterType::Impulse;
    pads[25].bodyModel   = BodyModelType::Membrane;
    pads[25].material = 0.50;
    pads[25].size = 0.40;
    pads[25].decay = 0.24;
    pads[25].strikePosition = 0.40;
    pads[25].level = 0.78;
    pads[25].tsPitchEnvStart = toLogNorm(340);
    pads[25].tsPitchEnvEnd   = toLogNorm(280);
    pads[25].tsPitchEnvTime  = 0.04;   // 20 ms
    pads[25].tsPitchEnvCurve = 0.15;
    pads[25].airLoading = 0.45;
    pads[25].tensionModAmt = 0.25;
    pads[25].bodyDampingB1 = 0.38;
    pads[25].bodyDampingB3 = 0.08;
    pads[25].couplingStrength  = 0.25;
    pads[25].secondaryEnabled  = 1.0;
    pads[25].secondarySize     = 0.30;
    pads[25].secondaryMaterial = 0.40;
    pads[25].clickLayerMix        = 0.68;
    pads[25].clickLayerContactMs  = 0.15;
    pads[25].clickLayerBrightness = 0.72;
    pads[25].noiseLayerMix = 0.08;
    pads[25].modeScatter = 0.10;
    pads[25].pan = 0.46;

    // ---- Pad 26: Conga Hi (Membrane/Impulse), barrel shell ----
    pads[26].exciterType = ExciterType::Impulse;
    pads[26].bodyModel   = BodyModelType::Membrane;
    pads[26].material = 0.42;
    pads[26].size = 0.50;
    pads[26].decay = 0.30;
    pads[26].strikePosition = 0.42;
    pads[26].level = 0.80;
    pads[26].tsPitchEnvStart = toLogNorm(280);
    pads[26].tsPitchEnvEnd   = toLogNorm(210);
    pads[26].tsPitchEnvTime  = 0.04;   // 20 ms
    pads[26].tsPitchEnvCurve = 0.15;
    pads[26].airLoading = 0.55;
    pads[26].tensionModAmt = 0.22;
    pads[26].bodyDampingB1 = 0.34;
    pads[26].bodyDampingB3 = 0.10;
    pads[26].couplingStrength  = 0.30;
    pads[26].secondaryEnabled  = 1.0;
    pads[26].secondarySize     = 0.35;
    pads[26].secondaryMaterial = 0.45;
    pads[26].clickLayerMix        = 0.55;
    pads[26].clickLayerContactMs  = 0.20;
    pads[26].clickLayerBrightness = 0.60;
    pads[26].noiseLayerMix    = 0.10;
    pads[26].noiseLayerCutoff = 0.40;
    pads[26].noiseLayerColor  = 0.40;  // Pink
    pads[26].pan = 0.58;

    // ---- Pad 27: Conga Lo / tumba (Membrane/Impulse), barrel shell ----
    pads[27].exciterType = ExciterType::Impulse;
    pads[27].bodyModel   = BodyModelType::Membrane;
    pads[27].material = 0.42;
    pads[27].size = 0.62;
    pads[27].decay = 0.30;
    pads[27].strikePosition = 0.42;
    pads[27].level = 0.80;
    pads[27].tsPitchEnvStart = toLogNorm(200);
    pads[27].tsPitchEnvEnd   = toLogNorm(150);
    pads[27].tsPitchEnvTime  = 0.04;   // 20 ms
    pads[27].tsPitchEnvCurve = 0.15;
    pads[27].airLoading = 0.58;
    pads[27].tensionModAmt = 0.22;
    pads[27].bodyDampingB1 = 0.32;
    pads[27].bodyDampingB3 = 0.10;
    pads[27].couplingStrength  = 0.30;
    pads[27].secondaryEnabled  = 1.0;
    pads[27].secondarySize     = 0.38;
    pads[27].secondaryMaterial = 0.45;
    pads[27].clickLayerMix        = 0.55;
    pads[27].clickLayerContactMs  = 0.20;
    pads[27].clickLayerBrightness = 0.60;
    pads[27].noiseLayerMix    = 0.10;
    pads[27].noiseLayerCutoff = 0.40;
    pads[27].noiseLayerColor  = 0.40;  // Pink
    pads[27].pan = 0.64;

    // ---- Pad 28: Woodblock (Plate/Impulse), inharmonic slit-cavity ----
    pads[28].exciterType = ExciterType::Impulse;
    pads[28].bodyModel   = BodyModelType::Plate;
    pads[28].material = 0.32;
    pads[28].size = 0.18;            // ~529 Hz
    pads[28].decay = 0.18;
    pads[28].strikePosition = 0.30;
    pads[28].level = 0.76;
    pads[28].modeStretch = 0.50;
    pads[28].decaySkew = 0.30;
    pads[28].bodyDampingB1 = 0.50;
    pads[28].bodyDampingB3 = 0.10;
    pads[28].modeScatter = 0.20;
    pads[28].clickLayerMix        = 0.78;
    pads[28].clickLayerContactMs  = 0.12;
    pads[28].clickLayerBrightness = 0.78;
    pads[28].noiseLayerMix = 0.0;
    pads[28].airLoading = 0.0;   // no-op on Plate
    pads[28].pan = 0.52;

    // 28 sounding pads; pad 16 (GM China spare) + 29-31 stay default/disabled.
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28};
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

    // ---- Pads 2 & 4: 808 / electronic snares. The 808/909 snare uses TWO
    //                  tonal layers + noise: a main mid-low body (boop) and
    //                  a higher metallic shimmer body. We model that with
    //                  the primary Membrane (boop) + secondary metallic
    //                  modal bank (shimmer). Noise sits on top with a tail
    //                  longer than the body so the snare doesn't feel thin. ----
    for (int p : {2, 4}) {
        pads[p].exciterType = ExciterType::NoiseBurst;
        pads[p].bodyModel = BodyModelType::Membrane;
        pads[p].level = 1.0;
        pads[p].airLoading       = 0.0;
        pads[p].modeScatter      = 0.0;
        // Secondary metallic body adds the high-frequency "ring" that real
        // 808/909 snares get from their second oscillator. Tuned smaller
        // and metallic so it shimmers above the main boop without muddying
        // the low-mid weight.
        pads[p].couplingStrength = 0.35;
        pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize    = 0.38;
        pads[p].secondaryMaterial = 0.85;
        // Main body damping: low enough that the boop lasts ~150-250 ms
        // (not "steady sine" but not a hat either).
        pads[p].bodyDampingB1    = 0.18;
        pads[p].bodyDampingB3    = 0.05;
        // Drive fattens the body harmonics so it doesn't sound like a clean
        // sine; gives the 808 its boxy nonlinear bite.
        pads[p].tsDriveAmount    = 0.48;
        pads[p].tsFilterType     = FilterType::LP;
        pads[p].tsFilterCutoff   = 0.80;
        pads[p].tsFilterResonance= 0.22;
        pads[p].tsFilterEnvAmount= 0.60;
        pads[p].tsFilterEnvAttack= 0.0;
        pads[p].tsFilterEnvDecay = 0.385;
        pads[p].tsFilterEnvSustain= 0.0;
        pads[p].tsFilterEnvRelease= 0.20;
        pads[p].tensionModAmt    = 0.35;
        pads[p].tsPitchEnvCurve  = 0.10;
        // Noise: pushed back up so the snare has real "fffft" weight, with
        // a long tail extending past the body decay so the snare doesn't
        // feel short.
        pads[p].noiseLayerMix      = 0.62;
        pads[p].noiseLayerCutoff   = 0.82;
        pads[p].noiseLayerColor    = 0.78;
        pads[p].noiseLayerDecay    = 0.40;
        pads[p].noiseLayerResonance= 0.12;
        pads[p].clickLayerMix      = 0.55;
        pads[p].clickLayerContactMs = 0.08;
        pads[p].clickLayerBrightness= 0.72;
    }
    // Pad 2: classic 808 snare -- deeper boop with metallic high body
    pads[2].material = 0.58; pads[2].size = 0.68; pads[2].decay = 0.85;
    pads[2].noiseBurstDuration = (4.0 - 2.0) / 13.0;
    pads[2].tsPitchEnvStart = toLogNorm(400);
    pads[2].tsPitchEnvEnd   = toLogNorm(110);
    pads[2].tsPitchEnvTime  = 0.13;

    // Pad 4: brighter, slightly shorter sister snare (rim-shot ish, but tonal).
    pads[4].material = 0.66; pads[4].size = 0.60; pads[4].decay = 0.72;
    pads[4].noiseBurstDuration = (3.0 - 2.0) / 13.0;
    pads[4].tsPitchEnvStart = toLogNorm(480);
    pads[4].tsPitchEnvEnd   = toLogNorm(160);
    pads[4].tsPitchEnvTime  = 0.10;

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
        pads[p].tsPitchEnvCurve = 0.5;  // Phase 10: was "Lin" StringList -> norm 0.5 = linear (curveAmount 0)
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

    // ---- Pad 2: Feedback Snare (resonant shell + scatter, redesigned:
    //              keeps the FX-snare character but with a real housing
    //              underneath and a proper crack on top). ----
    pads[2].exciterType = ExciterType::Feedback;
    pads[2].bodyModel = BodyModelType::Shell;
    pads[2].material = 0.55;
    pads[2].size = 0.55;
    pads[2].decay = 0.58;
    pads[2].level = 0.95;
    pads[2].feedbackAmount = 0.4;
    pads[2].modeScatter      = 0.42;
    pads[2].nonlinearCoupling = 0.22;
    pads[2].couplingStrength  = 0.65;
    pads[2].secondaryEnabled  = 1.0;
    pads[2].secondarySize     = 0.65;
    pads[2].secondaryMaterial = 0.70;
    pads[2].tensionModAmt     = 0.40;
    pads[2].tsDriveAmount     = 0.30;
    pads[2].tsFilterType      = FilterType::LP;
    pads[2].tsFilterCutoff    = 0.80;
    pads[2].tsFilterResonance = 0.20;
    pads[2].tsFilterEnvAmount = 0.70;
    pads[2].tsFilterEnvAttack = 0.0;
    pads[2].tsFilterEnvDecay  = 0.385;
    pads[2].tsFilterEnvSustain= 0.0;
    pads[2].tsFilterEnvRelease= 0.20;
    pads[2].tsPitchEnvStart   = toLogNorm(230);
    pads[2].tsPitchEnvEnd     = toLogNorm(160);
    pads[2].tsPitchEnvTime    = 0.12;
    pads[2].tsPitchEnvCurve   = 0.15;
    pads[2].noiseLayerMix     = 0.70;
    pads[2].noiseLayerCutoff  = 0.80;
    pads[2].noiseLayerColor   = 0.70;
    pads[2].noiseLayerDecay   = 0.32;
    pads[2].clickLayerMix     = 0.85;
    pads[2].clickLayerContactMs = 0.08;
    pads[2].clickLayerBrightness = 0.85;
    pads[2].airLoading        = 0.42;
    pads[2].bodyDampingB1     = 0.28;
    pads[2].bodyDampingB3     = 0.04;

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

    // Kit globals: dial in the sympathetic snare-buzz + tom-resonance
    // network so the per-pad couplingAmount weights become audible.
    k.opts.maxPolyphony   = 10;
    k.opts.globalCoupling = 0.30;
    k.opts.snareBuzz      = 0.30;
    k.opts.tomResonance   = 0.40;

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

    // Brush snare (sweep) -- intentionally NO stick crack; the brushy
    // sweep is the character. Gains more body and woody shell so the
    // underlying drum body sits beneath the sweep.
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel   = BodyModelType::Membrane;
    pads[2].material = 0.42; pads[2].size = 0.60; pads[2].decay = 0.55;
    pads[2].level = 0.92;
    pads[2].noiseBurstDuration = 0.85;
    pads[2].frictionPressure   = 0.60;
    pads[2].tsDriveAmount = 0.20;
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
    pads[2].noiseLayerMix    = 0.75; pads[2].noiseLayerCutoff = 0.62;
    pads[2].noiseLayerColor  = 0.45; pads[2].noiseLayerDecay = 0.55;
    pads[2].noiseLayerResonance = 0.05;
    pads[2].clickLayerMix    = 0.0;
    pads[2].airLoading  = 0.45; pads[2].modeScatter = 0.38;
    pads[2].nonlinearCoupling = 0.18;
    pads[2].couplingStrength = 0.60; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.65; pads[2].secondaryMaterial = 0.45;
    pads[2].tensionModAmt = 0.20;
    pads[2].tsPitchEnvStart = toLogNorm(210);
    pads[2].tsPitchEnvEnd   = toLogNorm(150);
    pads[2].tsPitchEnvTime  = 0.13;
    pads[2].tsPitchEnvCurve = 0.15;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.04;
    pads[2].macroTightness = 0.30; pads[2].macroBrightness = 0.50;
    pads[2].macroBodySize = 0.55;  pads[2].macroPunch = 0.20;
    pads[2].macroComplexity = 0.65;
    pads[2].couplingAmount = 0.65;

    // Brush snare (tap) -- softer than studio crack but uses the same body
    // recipe so the housing is audible. Click stays soft (mallet feel).
    pads[4].exciterType = ExciterType::NoiseBurst;
    pads[4].bodyModel   = BodyModelType::Membrane;
    pads[4].material = 0.40; pads[4].size = 0.60; pads[4].decay = 0.58;
    pads[4].level = 0.95;
    pads[4].noiseBurstDuration = (5.0 - 2.0) / 13.0;
    pads[4].tsDriveAmount = 0.22;
    pads[4].tsFilterType    = FilterType::LP;
    pads[4].tsFilterCutoff  = 0.78;
    pads[4].tsFilterResonance = 0.15;
    pads[4].tsFilterEnvAmount = 0.65;
    pads[4].tsFilterEnvAttack = 0.0;
    pads[4].tsFilterEnvDecay = 0.385;
    pads[4].tsFilterEnvSustain = 0.0;
    pads[4].tsFilterEnvRelease = 0.20;
    pads[4].noiseLayerMix    = 0.65; pads[4].noiseLayerCutoff = 0.72;
    pads[4].noiseLayerColor  = 0.60; pads[4].noiseLayerDecay = 0.28;
    pads[4].clickLayerMix    = 0.62; pads[4].clickLayerContactMs = 0.14;
    pads[4].clickLayerBrightness = 0.60;
    pads[4].airLoading  = 0.45; pads[4].modeScatter = 0.38;
    pads[4].nonlinearCoupling = 0.18;
    pads[4].couplingStrength = 0.62; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.68; pads[4].secondaryMaterial = 0.48;
    pads[4].tensionModAmt = 0.25;
    pads[4].tsPitchEnvStart = toLogNorm(210);
    pads[4].tsPitchEnvEnd   = toLogNorm(150);
    pads[4].tsPitchEnvTime  = 0.12;
    pads[4].tsPitchEnvCurve = 0.15;
    pads[4].bodyDampingB1 = 0.28; pads[4].bodyDampingB3 = 0.04;
    pads[4].macroTightness = 0.55; pads[4].macroBrightness = 0.55;
    pads[4].macroComplexity = 0.40;
    pads[4].pan = 0.52;

    // Hi-hats (closed pan 0.40, inherited by pedal/open copies)
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
    pads[6].pan = 0.40;

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

    // Toms -- size-graded row with NEW graded decaySkew / modeScatter / pan
    // (airLoading 0.65->0.45 graded, skew 0.46->0.54, scatter 0.10->0.15,
    // pan 0.38->0.60 L->R).
    const int    tomPads[]      = {5, 7, 9, 11, 12, 14};
    const double tomSizes[]     = {0.72, 0.62, 0.55, 0.48, 0.42, 0.36};
    const double tomMaterials[] = {0.40, 0.43, 0.46, 0.50, 0.55, 0.60};
    const double tomDecays[]    = {0.45, 0.40, 0.36, 0.32, 0.28, 0.24};
    const double tomB1[]        = {0.30, 0.32, 0.34, 0.36, 0.38, 0.42};
    const double tomPitchStart[] = {200, 240, 290, 340, 400, 470};
    const double tomPitchEnd[]   = {110, 135, 165, 200, 240, 290};
    const double tomAir[]        = {0.65, 0.61, 0.57, 0.53, 0.49, 0.45};
    const double tomSkew[]       = {0.46, 0.476, 0.492, 0.508, 0.524, 0.54};
    const double tomScatter[]    = {0.10, 0.11, 0.12, 0.13, 0.14, 0.15};
    const double tomPan[]        = {0.38, 0.424, 0.468, 0.512, 0.556, 0.60};
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
        pads[p].tsPitchEnvCurve = 0.5;  // Phase 10: was "Lin" StringList -> norm 0.5 = linear (curveAmount 0)
        pads[p].airLoading      = tomAir[i];
        pads[p].modeScatter     = tomScatter[i];
        pads[p].decaySkew       = tomSkew[i];
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
        pads[p].pan = tomPan[i];
    }

    // Ride bow (13) -- GM Crash-1 slot (inherited ride<->crash swap, kept).
    // Fixes: single modeScatter 0.62 (was 0.28->0.85 double-assign),
    // metallic b1 0.16/b3 0.0 (was 0.40/0.30), + strikePos/stretch/skew/NLC.
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel   = BodyModelType::Bell;
    pads[13].material = 0.92; pads[13].size = 0.42; pads[13].decay = 0.85;
    pads[13].level = 0.74;
    pads[13].strikePosition = 0.18;   // near soundbow antinode -> full partials
    pads[13].fmRatio = 0.35; pads[13].feedbackAmount = 0.05;  // inert under NoiseBurst
    pads[13].modeStretch = 0.45; pads[13].decaySkew = 0.62;
    pads[13].nonlinearCoupling = 0.18;
    pads[13].modeScatter = 0.62; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.16;
    pads[13].noiseLayerMix = 0.30; pads[13].noiseLayerCutoff = 0.85;
    pads[13].noiseLayerResonance = 0.0;
    pads[13].noiseLayerColor = 0.75; pads[13].noiseLayerDecay = 0.75;
    pads[13].clickLayerMix = 0.45; pads[13].clickLayerContactMs = 0.10;
    pads[13].clickLayerBrightness = 0.85;
    pads[13].outputBus = 1;
    pads[13].macroTightness = 0.30; pads[13].macroBrightness = 0.75;
    pads[13].macroComplexity = 0.55;
    pads[13].pan = 0.60;

    // Crash (15) -- GM Ride-1 slot (inherited ride<->crash swap, kept).
    // Fixes: single modeScatter 0.60 (was 0.55->0.85 double-assign),
    // + modeStretch/modeInject/NLC bloom/decaySkew.
    pads[15].exciterType = ExciterType::NoiseBurst;
    pads[15].bodyModel   = BodyModelType::NoiseBody;
    pads[15].material = 0.92; pads[15].size = 0.32; pads[15].decay = 0.65;
    pads[15].level = 0.70;
    pads[15].modeStretch = 0.55; pads[15].modeInjectAmount = 0.20;
    pads[15].nonlinearCoupling = 0.30; pads[15].decaySkew = 0.60;
    pads[15].modeScatter = 0.60; pads[15].airLoading = 0.0;
    pads[15].bodyDampingB3 = 0.30; pads[15].bodyDampingB1 = 0.40;
    pads[15].noiseLayerMix = 0.55; pads[15].noiseLayerCutoff = 0.78;
    pads[15].noiseLayerColor = 0.62; pads[15].noiseLayerDecay = 0.60;
    pads[15].clickLayerMix = 0.20; pads[15].clickLayerBrightness = 0.70;
    pads[15].outputBus = 1;
    pads[15].pan = 0.58;

    // Wood block (1) -- material 0.60 per archetype (was 0.68);
    // airLoading 0 (no-op on Plate).
    pads[1].exciterType = ExciterType::Impulse;
    pads[1].bodyModel = BodyModelType::Plate;
    pads[1].material = 0.60; pads[1].size = 0.22; pads[1].decay = 0.20;
    pads[1].level = 0.72;
    pads[1].modeStretch = 0.50;
    pads[1].clickLayerMix = 0.70; pads[1].clickLayerContactMs = 0.12;
    pads[1].clickLayerBrightness = 0.75;
    pads[1].noiseLayerMix = 0.05;
    pads[1].airLoading = 0.0; pads[1].modeScatter = 0.20;
    pads[1].bodyDampingB1 = 0.50; pads[1].bodyDampingB3 = 0.10;
    pads[1].pan = 0.40;

    // ---- Pad 3: Brush Swirl / Buzz-Roll (Membrane/Friction) -- NEW ----
    // The kit's only Friction + modeInject voice. Filter ADSR left inert
    // (envAmt 0.5) so the LP is a static dark bed.
    pads[3].exciterType = ExciterType::Friction;
    pads[3].bodyModel   = BodyModelType::Membrane;
    pads[3].material = 0.40; pads[3].size = 0.58; pads[3].decay = 0.70;
    pads[3].frictionPressure  = 0.45;   // LIVE under Friction
    pads[3].modeInjectAmount  = 0.18;   // 1/k bowed series
    pads[3].nonlinearCoupling = 0.45;
    pads[3].decaySkew = 0.85;           // low-tilt
    pads[3].tensionModAmt = 0.30;
    pads[3].airLoading = 0.50;
    pads[3].tsFilterType   = FilterType::LP;
    pads[3].tsFilterCutoff = 0.55;      // static dark bed (env inert)
    pads[3].clickLayerMix = 0.0;
    pads[3].noiseLayerMix = 0.20; pads[3].noiseLayerColor = 0.40;  // pink, long
    pads[3].noiseLayerDecay = 0.70;
    pads[3].couplingStrength = 0.18; pads[3].secondaryEnabled = 1.0;
    pads[3].pan = 0.45;

    // ---- Pad 16: Ride Bell / Cup FM (Bell/FMImpulse) -- NEW (GM Chinese) ----
    // The kit's only audible FM voice. fmRatio 0.30 -> modRatio 1.9 (Chowning).
    pads[16].exciterType = ExciterType::FMImpulse;
    pads[16].bodyModel   = BodyModelType::Bell;
    pads[16].material = 0.90; pads[16].size = 0.34; pads[16].decay = 0.70;
    pads[16].fmRatio = 0.30;            // LIVE clang
    pads[16].modeStretch = 0.45; pads[16].decaySkew = 0.60;
    pads[16].nonlinearCoupling = 0.15; pads[16].modeScatter = 0.35;
    pads[16].bodyDampingB1 = 0.18; pads[16].bodyDampingB3 = 0.0;
    pads[16].clickLayerMix = 0.40; pads[16].clickLayerBrightness = 0.82;
    pads[16].noiseLayerMix = 0.12;     // sheen
    pads[16].airLoading = 0.0;
    pads[16].outputBus = 1;
    pads[16].pan = 0.62;

    // ---- Pad 17: Splash (NoiseBody/NoiseBurst) -- NEW (GM Ride-Bell) ----
    pads[17].exciterType = ExciterType::NoiseBurst;
    pads[17].bodyModel   = BodyModelType::NoiseBody;
    pads[17].material = 0.95; pads[17].size = 0.22; pads[17].decay = 0.28;
    pads[17].strikePosition = 0.35;
    pads[17].modeStretch = 0.55; pads[17].decaySkew = 0.58;
    pads[17].modeScatter = 0.50;
    pads[17].bodyDampingB1 = 0.30; pads[17].bodyDampingB3 = 0.0;
    pads[17].noiseLayerMix = 0.55; pads[17].noiseLayerCutoff = 0.92;
    pads[17].noiseLayerColor = 0.90; pads[17].noiseLayerDecay = 0.25;  // violet
    pads[17].clickLayerMix = 0.30; pads[17].clickLayerBrightness = 0.85;
    pads[17].noiseBurstDuration = 0.40;
    pads[17].macroBrightness = 0.70;
    pads[17].airLoading = 0.0;
    pads[17].outputBus = 1;
    pads[17].pan = 0.66;

    // ---- Pad 18: Cymbal Swell (NoiseBody/NoiseBurst, Morph) -- NEW (GM Tamb) ----
    // 2nd Morph user; nonlinearCoupling 0.45 is the energy-cascade swell lever.
    pads[18].exciterType = ExciterType::NoiseBurst;
    pads[18].bodyModel   = BodyModelType::NoiseBody;
    pads[18].material = 0.90; pads[18].size = 0.40; pads[18].decay = 0.85;
    pads[18].strikePosition = 0.90;     // edge strike, broad mode set
    pads[18].morphEnabled = 1.0; pads[18].morphStart = 0.55;
    pads[18].morphEnd = 0.95; pads[18].morphDuration = 0.85;  // slow bloom
    pads[18].modeStretch = 0.55; pads[18].decaySkew = 0.55;
    pads[18].nonlinearCoupling = 0.45; pads[18].modeScatter = 0.65;
    pads[18].clickLayerMix = 0.0;
    pads[18].noiseLayerMix = 0.65; pads[18].noiseLayerCutoff = 0.85;
    pads[18].noiseLayerDecay = 0.90;
    pads[18].bodyDampingB1 = 0.22; pads[18].bodyDampingB3 = 0.0;
    pads[18].airLoading = 0.0;
    pads[18].outputBus = 1;
    pads[18].pan = 0.55;

    // ---- Pad 19: Side Stick / Cross-Stick (Shell/Impulse) -- NEW (GM Splash) ----
    pads[19].exciterType = ExciterType::Impulse;
    pads[19].bodyModel   = BodyModelType::Shell;
    pads[19].material = 0.30; pads[19].size = 0.20; pads[19].decay = 0.16;
    pads[19].strikePosition = 0.30;
    pads[19].bodyDampingB1 = 0.42; pads[19].bodyDampingB3 = 0.10;
    pads[19].modeScatter = 0.50;
    pads[19].clickLayerMix = 0.88; pads[19].clickLayerBrightness = 0.62;  // woody
    pads[19].noiseLayerMix = 0.0;
    pads[19].airLoading = 0.0;
    pads[19].pan = 0.50;

    // ---- Pad 20: Shaker / Cabasa (NoiseBody/NoiseBurst) -- NEW (GM Cowbell) ----
    pads[20].exciterType = ExciterType::NoiseBurst;
    pads[20].bodyModel   = BodyModelType::NoiseBody;
    pads[20].material = 0.85; pads[20].size = 0.08; pads[20].decay = 0.08;
    pads[20].noiseLayerMix = 0.85; pads[20].noiseLayerCutoff = 0.73;
    pads[20].noiseLayerResonance = 0.16; pads[20].noiseLayerColor = 0.75;  // white
    pads[20].noiseLayerDecay = 0.12;
    pads[20].clickLayerMix = 0.0;
    pads[20].noiseBurstDuration = 0.20;
    pads[20].bodyDampingB1 = 0.55; pads[20].bodyDampingB3 = 0.0;
    pads[20].airLoading = 0.0;
    pads[20].pan = 0.64;

    // 21 sounding pads (0-20); pads 21-31 stay disabled (focused set).
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                 16, 17, 18, 19, 20};
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
    pads[0].tsPitchEnvStart = toLogNorm(160);  // impact-thump start (was 180)
    pads[0].tsPitchEnvEnd   = toLogNorm(46);
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
    pads[0].bodyDampingB1 = 0.32; pads[0].bodyDampingB3 = 0.42;  // woody HF roll-off
    pads[0].macroTightness = 0.55; pads[0].macroBrightness = 0.40;
    pads[0].macroBodySize = 0.85;  pads[0].macroPunch = 0.85;
    pads[0].macroComplexity = 0.40;
    pads[0].couplingAmount = 0.75;

    // Crack snare (redesigned: massive housing, big-room body)
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.38; pads[2].size = 0.66; pads[2].decay = 0.68;
    pads[2].strikePosition = 0.35;   // off-center crack pair
    pads[2].level = 1.0;
    pads[2].noiseBurstDuration = (4.0 - 2.0) / 13.0;
    pads[2].tsDriveAmount = 0.42;
    pads[2].tsFilterType = FilterType::LP;
    pads[2].tsFilterCutoff = 0.85;
    pads[2].tsFilterResonance = 0.20;
    pads[2].tsFilterEnvAmount = 0.78;
    pads[2].tsFilterEnvAttack = 0.0;
    pads[2].tsFilterEnvDecay = 0.385;
    pads[2].tsFilterEnvSustain = 0.0;
    pads[2].tsFilterEnvRelease = 0.20;
    pads[2].noiseLayerMix    = 0.85; pads[2].noiseLayerCutoff = 0.88;
    pads[2].noiseLayerResonance = 0.15;
    pads[2].noiseLayerColor  = 0.80; pads[2].noiseLayerDecay = 0.35;
    pads[2].clickLayerMix    = 0.95; pads[2].clickLayerContactMs = 0.06;
    pads[2].clickLayerBrightness = 0.92;
    pads[2].airLoading = 0.42; pads[2].modeScatter = 0.42;
    pads[2].nonlinearCoupling = 0.22;
    pads[2].couplingStrength = 0.78; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.80; pads[2].secondaryMaterial = 0.52;
    pads[2].tensionModAmt = 0.32;
    pads[2].tsPitchEnvStart = toLogNorm(220);
    pads[2].tsPitchEnvEnd   = toLogNorm(130);
    pads[2].tsPitchEnvTime  = 0.14;
    pads[2].tsPitchEnvCurve = 0.15;
    pads[2].bodyDampingB1 = 0.28; pads[2].bodyDampingB3 = 0.10;  // Mylar HF damping (big-room b1 0.28 kept)
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
    pads[6].strikePosition = 0.60;   // tight-chick plate weighting
    pads[6].pan = 0.40;              // L (inherited by pedal/open copies)

    pads[8] = pads[6];
    pads[8].decay = 0.06; pads[8].noiseLayerDecay = 0.07;
    pads[8].bodyDampingB1 = 0.65;

    pads[10] = pads[6];
    pads[10].decay = 0.65; pads[10].noiseLayerDecay = 0.60;
    pads[10].bodyDampingB1 = 0.30;
    pads[10].strikePosition = 0.45;

    // Toms -- NEW: explicit Strike 0.35 (recovers the pitched (1,1)/(2,1)
    // mode vs a dead-center thump) + graded decaySkew / nonlinearCoupling /
    // modeScatter / tensionMod / airLoading / pan. Pitch-env already encoded
    // via toLogNorm to the 180->70 .. 480->215 Hz row (no re-encode needed).
    const int    tomPads[]      = {5, 7, 9, 11, 12, 14};
    const double tomSizes[]     = {0.92, 0.85, 0.75, 0.65, 0.55, 0.48};
    const double tomMaterial[]  = {0.30, 0.34, 0.38, 0.43, 0.50, 0.58};
    const double tomDecay[]     = {0.65, 0.58, 0.50, 0.43, 0.36, 0.30};
    const double tomPitchHi[]   = {180, 220, 270, 330, 400, 480};
    const double tomPitchLo[]   = {70,  85, 105, 130, 165, 215};
    const double tomB1[]        = {0.26, 0.28, 0.30, 0.33, 0.36, 0.40};
    const double tomSkew[]      = {0.46, 0.448, 0.436, 0.424, 0.412, 0.40};
    const double tomScatter[]   = {0.14, 0.128, 0.116, 0.104, 0.092, 0.08};
    const double tomTension[]   = {0.34, 0.32, 0.30, 0.28, 0.26, 0.24};
    const double tomAir[]       = {0.78, 0.764, 0.748, 0.732, 0.716, 0.70};
    const double tomPan[]       = {0.66, 0.60, 0.55, 0.48, 0.41, 0.34};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel   = BodyModelType::Membrane;
        pads[p].material = tomMaterial[i];
        pads[p].size     = tomSizes[i];
        pads[p].decay    = tomDecay[i];
        pads[p].strikePosition = 0.35;
        pads[p].level    = 0.85;
        pads[p].tsPitchEnvStart = toLogNorm(tomPitchHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomPitchLo[i]);
        pads[p].tsPitchEnvTime  = 0.08;
        pads[p].tsPitchEnvCurve = 0.5;  // Phase 10: was "Lin" StringList -> norm 0.5 = linear (curveAmount 0)
        pads[p].tsDriveAmount   = 0.18;
        pads[p].airLoading      = tomAir[i];
        pads[p].modeScatter     = tomScatter[i];
        pads[p].decaySkew       = tomSkew[i];
        pads[p].nonlinearCoupling = 0.12;
        pads[p].couplingStrength = 0.55;
        pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize    = 0.40 + 0.02 * i;
        pads[p].secondaryMaterial = 0.55;
        pads[p].tensionModAmt    = tomTension[i];
        pads[p].noiseLayerMix    = 0.18; pads[p].noiseLayerCutoff = 0.45;
        pads[p].clickLayerMix    = 0.55; pads[p].clickLayerContactMs = 0.30;
        pads[p].clickLayerBrightness = 0.55;
        pads[p].bodyDampingB1 = tomB1[i]; pads[p].bodyDampingB3 = 0.10;
        pads[p].macroPunch = 0.85; pads[p].macroBodySize = 0.55 + 0.05 * i;
        pads[p].couplingAmount = 0.70;
        pads[p].pan = tomPan[i];
    }

    // Crash 1 (13) -- sustain crash, aux bus 1, pan L. NEW metallic axes:
    // modeStretch + modeScatter + modeInject 1/k + NLC bloom + Strike.
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel = BodyModelType::NoiseBody;
    pads[13].material = 0.95; pads[13].size = 0.45; pads[13].decay = 0.85;
    pads[13].level = 0.78;
    pads[13].strikePosition = 0.55;   // near-edge plate strike
    pads[13].modeStretch = 0.60; pads[13].modeScatter = 0.60;
    pads[13].modeInjectAmount = 0.25; pads[13].nonlinearCoupling = 0.35;
    pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.65; pads[13].noiseLayerCutoff = 0.92;
    pads[13].noiseLayerColor = 0.85; pads[13].noiseLayerDecay = 0.75;
    pads[13].clickLayerMix = 0.30; pads[13].clickLayerBrightness = 0.85;
    pads[13].outputBus = 1;
    pads[13].macroBrightness = 0.85;
    pads[13].pan = 0.34;

    // Ride (15) -- true Bell ping. FM ratio left default (no-op under
    // NoiseBurst). modeInject reset (1/k bloom is crash/china only).
    pads[15] = pads[13];
    pads[15].bodyModel = BodyModelType::Bell;
    pads[15].size = 0.55; pads[15].decay = 0.95;
    pads[15].strikePosition = 0.18;
    pads[15].modeStretch = 0.45; pads[15].modeScatter = 0.55;
    pads[15].decaySkew = 0.62;
    pads[15].modeInjectAmount = 0.0; pads[15].nonlinearCoupling = 0.18;
    pads[15].bodyDampingB1 = 0.16;
    pads[15].clickLayerMix = 0.55; pads[15].clickLayerBrightness = 0.92;
    pads[15].outputBus = 1;
    pads[15].pan = 0.62;

    // Crash 2 / China (16, NEW) -- trashy morph swell, aux bus 1, pan L.
    pads[16] = pads[13];
    pads[16].modeStretch = 0.66; pads[16].modeScatter = 0.70;
    pads[16].modeInjectAmount = 0.28; pads[16].nonlinearCoupling = 0.35;
    pads[16].morphEnabled = 1.0; pads[16].morphStart = 0.80;
    pads[16].morphEnd = 0.96; pads[16].morphDuration = 0.30;
    pads[16].strikePosition = 0.55;
    pads[16].outputBus = 1;
    pads[16].pan = 0.30;

    // Splash (17) -- short bright; modeInject/NLC reset (not in their lists).
    pads[17] = pads[13];
    pads[17].size = 0.22; pads[17].decay = 0.28;
    pads[17].strikePosition = 0.35;
    pads[17].modeStretch = 0.55; pads[17].modeScatter = 0.50;
    pads[17].modeInjectAmount = 0.0; pads[17].nonlinearCoupling = 0.0;
    pads[17].noiseLayerDecay = 0.25;
    pads[17].outputBus = 1;
    pads[17].pan = 0.66;

    // Cross-stick (1, NEW) -- dry, distinct from the loud rimshot (4).
    pads[1].exciterType = ExciterType::Impulse;
    pads[1].bodyModel   = BodyModelType::Shell;
    pads[1].material = 0.30; pads[1].size = 0.20; pads[1].decay = 0.16;
    pads[1].strikePosition = 0.30;
    pads[1].level = 0.72;
    pads[1].modeStretch = 0.40;
    pads[1].bodyDampingB1 = 0.42; pads[1].bodyDampingB3 = 0.10;
    pads[1].modeScatter = 0.50;
    pads[1].clickLayerMix = 0.85; pads[1].clickLayerContactMs = 0.10;
    pads[1].clickLayerBrightness = 0.62;
    pads[1].noiseLayerMix = 0.0;
    pads[1].airLoading = 0.0;
    pads[1].pan = 0.50;

    // Hand Clap (3, NEW) -- cupped-hand formant (noise reso 0.40).
    pads[3].exciterType = ExciterType::NoiseBurst;
    pads[3].bodyModel   = BodyModelType::NoiseBody;
    pads[3].material = 0.85; pads[3].size = 0.18; pads[3].decay = 0.18;
    pads[3].level = 0.80;
    pads[3].noiseBurstDuration = 0.55;
    pads[3].noiseLayerMix = 0.85; pads[3].noiseLayerCutoff = 0.78;
    pads[3].noiseLayerResonance = 0.40; pads[3].noiseLayerDecay = 0.20;
    pads[3].noiseLayerColor = 0.70;
    pads[3].clickLayerMix = 0.45; pads[3].clickLayerContactMs = 0.22;
    pads[3].clickLayerBrightness = 0.62;
    pads[3].modeScatter = 0.40;
    pads[3].bodyDampingB1 = 0.50; pads[3].bodyDampingB3 = 0.0;
    pads[3].airLoading = 0.0;
    pads[3].pan = 0.50;

    // Cowbell (18, NEW) -- the kit's only live FM voice (FMImpulse).
    pads[18].exciterType = ExciterType::FMImpulse;
    pads[18].bodyModel   = BodyModelType::Bell;
    pads[18].material = 0.78; pads[18].size = 0.26; pads[18].decay = 0.30;
    pads[18].level = 0.76;
    pads[18].fmRatio = 0.45;   // LIVE -> mod ratio 2.35 (detuned-fifth band)
    pads[18].modeStretch = 0.50;
    pads[18].clickLayerMix = 0.55; pads[18].clickLayerContactMs = 0.10;
    pads[18].clickLayerBrightness = 0.70;
    pads[18].noiseLayerMix = 0.10;
    pads[18].modeScatter = 0.20;
    pads[18].bodyDampingB3 = 0.0; pads[18].bodyDampingB1 = 0.40;
    pads[18].airLoading = 0.0;
    pads[18].macroBrightness = 0.65;
    pads[18].pan = 0.58;

    // Tambourine (20, NEW) -- secondary-shell jingle bank + macro complexity.
    pads[20].exciterType = ExciterType::NoiseBurst;
    pads[20].bodyModel   = BodyModelType::NoiseBody;
    pads[20].material = 0.92; pads[20].size = 0.15; pads[20].decay = 0.25;
    pads[20].level = 0.74;
    pads[20].noiseBurstDuration = 0.40;
    pads[20].noiseLayerMix = 0.65; pads[20].noiseLayerCutoff = 0.92;
    pads[20].noiseLayerResonance = 0.20; pads[20].noiseLayerDecay = 0.30;
    pads[20].noiseLayerColor = 0.90;
    pads[20].modeScatter = 0.50; pads[20].modeStretch = 0.55;
    pads[20].bodyDampingB3 = 0.0;
    pads[20].clickLayerMix = 0.30;
    pads[20].couplingStrength = 0.45; pads[20].secondaryEnabled = 1.0;
    pads[20].secondarySize = 0.30; pads[20].secondaryMaterial = 0.70;
    pads[20].macroComplexity = 0.65;
    pads[20].airLoading = 0.0;
    pads[20].pan = 0.55;

    k.opts.maxPolyphony    = 12;
    k.opts.globalCoupling  = 0.30;
    k.opts.snareBuzz       = 0.35;
    k.opts.tomResonance    = 0.45;
    k.opts.couplingDelayMs = 1.2;
    // 20 sounding pads; pads 19, 21-31 stay disabled (documented gap).
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20};
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
    pads[0].tsDriveAmount   = 0.20;   // M-2 unity makeup: Drive = flavour
    pads[0].tsFoldAmount    = 0.10;   // signature vintage saturator
    pads[0].decaySkew       = 0.55;   // per-mode tilt (was flat)
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

    // Wood-shell snare (redesigned: deeper wood housing, warmer body)
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.32; pads[2].size = 0.62; pads[2].decay = 0.66;
    pads[2].level = 1.0;
    pads[2].noiseBurstDuration = (5.0 - 2.0) / 13.0;
    pads[2].tsDriveAmount = 0.38;
    pads[2].tsFilterType = FilterType::LP;
    pads[2].tsFilterCutoff = 0.80;
    pads[2].tsFilterResonance = 0.18;
    pads[2].tsFilterEnvAmount = 0.72;
    pads[2].tsFilterEnvAttack = 0.0;
    pads[2].tsFilterEnvDecay = 0.385;
    pads[2].tsFilterEnvSustain = 0.0;
    pads[2].tsFilterEnvRelease = 0.20;
    pads[2].noiseLayerMix    = 0.78; pads[2].noiseLayerCutoff = 0.78;
    pads[2].noiseLayerResonance = 0.12;
    pads[2].noiseLayerColor  = 0.62; pads[2].noiseLayerDecay = 0.35;
    pads[2].clickLayerMix    = 0.88; pads[2].clickLayerContactMs = 0.08;
    pads[2].clickLayerBrightness = 0.78;
    pads[2].airLoading = 0.45; pads[2].modeScatter = 0.40;
    pads[2].nonlinearCoupling = 0.22;
    pads[2].couplingStrength = 0.80; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.75; pads[2].secondaryMaterial = 0.32;
    pads[2].tensionModAmt = 0.30;
    pads[2].tsPitchEnvStart = toLogNorm(200);
    pads[2].tsPitchEnvEnd   = toLogNorm(130);
    pads[2].tsPitchEnvTime  = 0.13;
    pads[2].tsPitchEnvCurve = 0.15;
    pads[2].bodyDampingB1 = 0.60; pads[2].bodyDampingB3 = 0.04;  // ~30 s^-1 short snares-on tat
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
    pads[4].pan = 0.40;   // L

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
    pads[6].pan = 0.62;   // R (inherited by pedal/open copies)

    pads[8] = pads[6];
    pads[8].decay = 0.05; pads[8].noiseLayerDecay = 0.06;

    pads[10] = pads[6];
    pads[10].decay = 0.45; pads[10].noiseLayerDecay = 0.42;
    pads[10].bodyDampingB1 = 0.30;

    // Toms -- NEW: exp glide (curve 0.15), graded decaySkew + pan spread
    // 0.36->0.64. secondaryMaterial 0.30 = deliberate dark-shell vintage
    // identity (documented departure from the archetype's "up" note).
    const int    tomPads[]      = {5, 7, 9, 11, 12, 14};
    const double tomSizes[]     = {0.72, 0.62, 0.55, 0.48, 0.42, 0.35};
    const double tomMaterial[]  = {0.30, 0.32, 0.36, 0.42, 0.48, 0.55};
    const double tomDecay[]     = {0.42, 0.38, 0.34, 0.30, 0.26, 0.22};
    const double tomB1[]        = {0.30, 0.32, 0.35, 0.38, 0.42, 0.46};
    const double tomPitchHi[]   = {200, 240, 290, 340, 400, 480};
    const double tomPitchLo[]   = {95, 115, 140, 170, 210, 260};
    const double tomSkew[]      = {0.44, 0.46, 0.48, 0.50, 0.52, 0.54};
    const double tomPan[]       = {0.36, 0.416, 0.472, 0.528, 0.584, 0.64};
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
        pads[p].tsPitchEnvCurve = 0.15;  // exp glide
        pads[p].tsDriveAmount   = 0.25;
        pads[p].airLoading      = 0.55;
        pads[p].modeScatter     = 0.18;
        pads[p].decaySkew       = tomSkew[i];
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
        pads[p].pan = tomPan[i];
    }

    // Crash 1 (13) -- NEW axes: modeStretch + modeInject 1/k + NLC bloom,
    // aux bus 1, pan L. (Amounts sourced from crash-cymbal.md, matching the
    // approved sibling-kit crashes.)
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel = BodyModelType::NoiseBody;
    pads[13].material = 0.92; pads[13].size = 0.30; pads[13].decay = 0.65;
    pads[13].level = 0.72;
    pads[13].modeStretch = 0.55; pads[13].modeInjectAmount = 0.20;
    pads[13].nonlinearCoupling = 0.30;
    pads[13].modeScatter = 0.55; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.55; pads[13].noiseLayerCutoff = 0.78;
    pads[13].noiseLayerColor = 0.65; pads[13].noiseLayerDecay = 0.60;
    pads[13].clickLayerMix = 0.18; pads[13].clickLayerBrightness = 0.65;
    pads[13].outputBus = 1;
    pads[13].pan = 0.40;   // L

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

    // Ride (15) -- NEW, replaces cowbell-on-GM-ride (MIDI 51) violation.
    // Bell body + NoiseBurst; fmRatio inert (the tuned ping is the Bell body).
    // Recipe sourced from ride-cymbal-bell-bow-cymbal.md (matches approved
    // sibling-kit rides). bus 1, pan R.
    pads[15].exciterType = ExciterType::NoiseBurst;
    pads[15].bodyModel   = BodyModelType::Bell;
    pads[15].material = 0.95; pads[15].size = 0.30; pads[15].decay = 0.90;
    pads[15].level = 0.72;
    pads[15].strikePosition = 0.18;
    pads[15].modeStretch = 0.45; pads[15].decaySkew = 0.62;
    pads[15].nonlinearCoupling = 0.18; pads[15].modeScatter = 0.55;
    pads[15].bodyDampingB1 = 0.16; pads[15].bodyDampingB3 = 0.0;
    pads[15].noiseLayerMix = 0.45; pads[15].noiseLayerCutoff = 0.90;
    pads[15].noiseLayerColor = 0.90; pads[15].noiseLayerDecay = 0.78;  // violet
    pads[15].clickLayerMix = 0.45; pads[15].clickLayerContactMs = 0.25;
    pads[15].clickLayerBrightness = 0.82;
    pads[15].airLoading = 0.0;
    pads[15].macroBrightness = 0.70;
    pads[15].outputBus = 1;
    pads[15].pan = 0.62;   // R

    // Crash 2 (16) -- NEW, darker/larger sister of crash 1 (lower f0).
    // bus 1, pan R. Inherits crash 1's metallic axes, then de-brightened.
    pads[16] = pads[13];
    pads[16].size = 0.34;            // larger -> lower register
    pads[16].material = 0.90;
    pads[16].noiseLayerColor = 0.58; // darker
    pads[16].pan = 0.62;             // R

    // Cowbell (17) -- relocated off the GM ride slot; vintage-wood Impulse
    // variant (Material 0.72, Decay 0.34, Click 0.62) + clang axes. pan L.
    pads[17].exciterType = ExciterType::Impulse;
    pads[17].bodyModel = BodyModelType::Bell;
    pads[17].material = 0.72; pads[17].size = 0.26; pads[17].decay = 0.34;
    pads[17].level = 0.75;
    pads[17].modeStretch = 0.55; pads[17].decaySkew = 0.42;
    pads[17].clickLayerMix = 0.62; pads[17].clickLayerContactMs = 0.10;
    pads[17].clickLayerBrightness = 0.70;
    pads[17].noiseLayerMix = 0.10;
    pads[17].airLoading = 0.0; pads[17].modeScatter = 0.20;
    pads[17].bodyDampingB3 = 0.0; pads[17].bodyDampingB1 = 0.40;
    pads[17].macroBrightness = 0.65;
    pads[17].pan = 0.40;   // L

    k.opts.maxPolyphony    = 10;
    k.opts.globalCoupling  = 0.20;
    k.opts.snareBuzz       = 0.30;
    k.opts.tomResonance    = 0.30;
    k.opts.couplingDelayMs = 1.0;
    // 18 sounding pads; pads 18-31 stay disabled (focused core).
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
    return k;
}
Kit orchestralKit() {
    Kit k{"Orchestral", "Acoustic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Timpani low (kick slot, Membrane/Mallet). Single-head kettle drum:
    // secondary shell OFF (adds unscaled RMS -> clip). Levels/airLoading
    // restored post-N-1 (old cut was to dodge the fixed clip).
    pads[0].exciterType = ExciterType::Mallet;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.32; pads[0].size = 0.90; pads[0].decay = 0.82;
    pads[0].strikePosition = 0.28;
    pads[0].level = 0.84;
    pads[0].tsPitchEnvStart = toLogNorm(180);
    pads[0].tsPitchEnvEnd   = toLogNorm(85);
    pads[0].tsPitchEnvTime  = 0.10;   // 50 ms
    pads[0].tsPitchEnvCurve = 0.35;
    pads[0].airLoading       = 0.80;
    pads[0].couplingStrength = 0.0;
    pads[0].secondaryEnabled = 0.0;
    pads[0].tensionModAmt    = 0.16;
    pads[0].modeScatter      = 0.06;
    pads[0].modeInjectAmount = 0.12;  // small post-audit 1/k activation
    pads[0].clickLayerMix       = 0.32; pads[0].clickLayerContactMs = 0.40;
    pads[0].clickLayerBrightness = 0.32;
    pads[0].noiseLayerMix = 0.10; pads[0].noiseLayerCutoff = 0.40;
    pads[0].noiseLayerColor = 0.12; pads[0].noiseLayerDecay = 0.20;  // Brown
    pads[0].bodyDampingB1 = 0.12; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroBodySize = 0.95; pads[0].macroComplexity = 0.55;
    pads[0].couplingAmount = 0.60;
    pads[0].pan = 0.42;

    // Orchestral Snare (2, moved off the bass slot). Fixed high-tension
    // head: NO pitch glide (tensionMod 0), tight 14x5 secondary shell.
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.60; pads[2].size = 0.33; pads[2].decay = 0.30;
    pads[2].level = 0.90;
    pads[2].noiseBurstDuration = (3.0 - 2.0) / 13.0;
    pads[2].tsDriveAmount = 0.06;
    pads[2].tsFilterType = FilterType::LP;
    pads[2].tsFilterCutoff = 0.92;
    pads[2].tsFilterResonance = 0.12;
    pads[2].tsFilterEnvAmount = 0.55;
    pads[2].tsFilterEnvAttack = 0.0;
    pads[2].tsFilterEnvDecay = 0.32;
    pads[2].tsFilterEnvSustain = 0.0;
    pads[2].tsFilterEnvRelease = 0.15;
    pads[2].noiseLayerMix    = 0.98; pads[2].noiseLayerCutoff = 0.90;
    pads[2].noiseLayerResonance = 0.10;
    pads[2].noiseLayerColor  = 0.90; pads[2].noiseLayerDecay = 0.55;  // Violet
    pads[2].clickLayerMix    = 0.95; pads[2].clickLayerContactMs = 0.18;
    pads[2].clickLayerBrightness = 0.93;
    pads[2].nonlinearCoupling = 0.12;
    pads[2].modeStretch = 0.40; pads[2].decaySkew = 0.58;
    pads[2].modeScatter = 0.18;
    pads[2].airLoading = 0.60;
    pads[2].couplingStrength = 0.28; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.32; pads[2].secondaryMaterial = 0.70;
    pads[2].tensionModAmt = 0.0;   // FIXED: concert snare has no pitch glide
    pads[2].tsPitchEnvStart = 0.0;
    pads[2].tsPitchEnvEnd   = 0.0;
    pads[2].tsPitchEnvTime  = 0.0;
    pads[2].bodyDampingB1 = 0.62; pads[2].bodyDampingB3 = 0.30;
    pads[2].macroBrightness = 0.65; pads[2].macroComplexity = 0.55;
    pads[2].pan = 0.50;

    // Concert Bass Drum (4, moved off the snare slot). Single-head:
    // secondary OFF. Deep slow pitch drop.
    pads[4].exciterType = ExciterType::Mallet;
    pads[4].bodyModel = BodyModelType::Membrane;
    pads[4].material = 0.30; pads[4].size = 0.90; pads[4].decay = 0.55;
    pads[4].level = 0.88;
    pads[4].tsPitchEnvStart = toLogNorm(110);
    pads[4].tsPitchEnvEnd   = toLogNorm(40);
    pads[4].tsPitchEnvTime  = 0.24;   // 120 ms
    pads[4].tsPitchEnvCurve = 0.15;   // exp
    pads[4].airLoading       = 0.80;
    pads[4].couplingStrength = 0.0;
    pads[4].secondaryEnabled = 0.0;
    pads[4].tensionModAmt    = 0.12;
    pads[4].clickLayerMix       = 0.35; pads[4].clickLayerContactMs = 0.42;
    pads[4].clickLayerBrightness = 0.20;
    pads[4].noiseLayerMix = 0.18; pads[4].noiseLayerCutoff = 0.30;
    pads[4].noiseLayerColor = 0.12; pads[4].noiseLayerDecay = 0.45;  // Brown
    pads[4].bodyDampingB1 = 0.30; pads[4].bodyDampingB3 = 0.55;
    pads[4].couplingAmount = 0.60;
    pads[4].pan = 0.50;

    // Timpani toms -- the SAME instrument as pad 0, 5 tunings. airLoading
    // 0.80 (matches pad 0, pitch-fusion), level restored 0.82, NEW per-mode
    // decaySkew tilt + small modeInject + pan row sweep.
    const int    timpaniPads[]   = {5, 7, 9, 11, 14};
    const double timpaniSize[]   = {0.88, 0.82, 0.75, 0.68, 0.60};
    const double timpaniHi[]     = {180, 220, 280, 350, 440};
    const double timpaniLo[]     = {80, 100, 130, 165, 215};
    const double timpaniDecay[]  = {0.80, 0.72, 0.65, 0.58, 0.50};
    const double timpaniB1[]     = {0.10, 0.12, 0.14, 0.17, 0.21};
    const double timpaniInject[] = {0.10, 0.105, 0.11, 0.115, 0.12};
    const double timpaniSkew[]   = {0.45, 0.47, 0.50, 0.53, 0.55};
    const double timpaniPan[]    = {0.38, 0.43, 0.47, 0.52, 0.57};
    for (int i = 0; i < 5; ++i) {
        const int p = timpaniPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel = BodyModelType::Membrane;
        pads[p].material = 0.32 + 0.04 * i;
        pads[p].size = timpaniSize[i];
        pads[p].decay = timpaniDecay[i];
        pads[p].level = 0.82;
        pads[p].tsPitchEnvStart = toLogNorm(timpaniHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(timpaniLo[i]);
        pads[p].tsPitchEnvTime  = 0.12;
        pads[p].tsPitchEnvCurve = 0.35;
        pads[p].airLoading       = 0.80;
        pads[p].modeScatter      = 0.08;
        pads[p].modeInjectAmount = timpaniInject[i];
        pads[p].decaySkew        = timpaniSkew[i];
        pads[p].couplingStrength = 0.0;
        pads[p].secondaryEnabled = 0.0;
        pads[p].tensionModAmt    = 0.15;
        pads[p].noiseLayerMix    = 0.12; pads[p].noiseLayerCutoff = 0.40;
        pads[p].clickLayerMix    = 0.32; pads[p].clickLayerContactMs = 0.28;
        pads[p].clickLayerBrightness = 0.32;
        pads[p].bodyDampingB1 = timpaniB1[i]; pads[p].bodyDampingB3 = 0.10;
        pads[p].macroBodySize = 0.85 - 0.05 * i;
        pads[p].macroComplexity = 0.55;
        pads[p].couplingAmount = 0.55;
        pads[p].pan = timpaniPan[i];
    }

    // Triangle (12) -- Bell -> Shell (free-free bar 1:2.757:5.404).
    pads[12].exciterType = ExciterType::Impulse;
    pads[12].bodyModel = BodyModelType::Shell;
    pads[12].material = 0.95; pads[12].size = 0.085; pads[12].decay = 0.85;
    pads[12].level = 0.65;
    pads[12].strikePosition = 0.18;   // free-end antinode
    pads[12].modeStretch = 0.62; pads[12].decaySkew = 0.40;
    pads[12].modeScatter = 0.12;
    pads[12].clickLayerMix = 0.95; pads[12].clickLayerContactMs = 0.10;
    pads[12].clickLayerBrightness = 0.97;
    pads[12].noiseLayerMix = 0.0;
    pads[12].airLoading = 0.0;
    pads[12].bodyDampingB3 = 0.02; pads[12].bodyDampingB1 = 0.30;
    pads[12].outputBus = 1;
    pads[12].macroBrightness = 0.95; pads[12].macroComplexity = 0.30;
    pads[12].pan = 0.62;

    // Suspended cymbal -- struck (6), choke group 1
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::NoiseBody;
    pads[6].material = 0.90; pads[6].size = 0.20; pads[6].decay = 0.50;
    pads[6].level = 0.70; pads[6].chokeGroup = 1;
    pads[6].modeStretch = 0.55; pads[6].decaySkew = 0.58;
    pads[6].nonlinearCoupling = 0.25;
    pads[6].noiseLayerMix = 0.65; pads[6].noiseLayerCutoff = 0.82;
    pads[6].noiseLayerColor = 0.70; pads[6].noiseLayerDecay = 0.12;
    pads[6].clickLayerMix = 0.20;
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.40;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.45;
    pads[6].pan = 0.65;

    // Suspended cymbal -- pedal (8), short choke
    pads[8] = pads[6];
    pads[8].decay = 0.18; pads[8].noiseLayerDecay = 0.07;
    pads[8].bodyDampingB1 = 0.65;

    // Suspended cymbal roll (10) -- morph swell, aux1, choke 1
    pads[10].exciterType = ExciterType::NoiseBurst;
    pads[10].bodyModel = BodyModelType::NoiseBody;
    pads[10].material = 0.95; pads[10].size = 0.42; pads[10].decay = 0.95;
    pads[10].level = 0.72; pads[10].chokeGroup = 1;
    pads[10].morphEnabled = 1.0;
    pads[10].morphStart = 0.55; pads[10].morphEnd = 0.95;
    pads[10].morphDuration = 0.85; pads[10].morphCurve = 0.4;  // ~1.7 s
    pads[10].modeScatter = 0.65; pads[10].airLoading = 0.0;
    pads[10].nonlinearCoupling = 0.20;
    pads[10].bodyDampingB3 = 0.0; pads[10].bodyDampingB1 = 0.30;
    pads[10].noiseLayerMix = 0.65; pads[10].noiseLayerCutoff = 0.82;
    pads[10].noiseLayerColor = 0.78; pads[10].noiseLayerDecay = 0.95;
    pads[10].clickLayerMix = 0.0;
    pads[10].decaySkew = 0.55;
    pads[10].outputBus = 1;
    pads[10].pan = 0.65;

    // Gong / Tam-Tam (13) -- Bell + stretch + bloom (secondary OFF by design).
    pads[13].exciterType = ExciterType::Mallet;
    pads[13].bodyModel = BodyModelType::Bell;
    pads[13].material = 0.85; pads[13].size = 0.85; pads[13].decay = 0.95;
    pads[13].level = 0.82;
    pads[13].strikePosition = 0.60;
    pads[13].modeStretch = 0.62;
    pads[13].nonlinearCoupling = 0.80;   // bloom
    pads[13].modeInjectAmount = 0.15;
    pads[13].morphEnabled = 1.0;
    pads[13].morphStart = 0.85; pads[13].morphEnd = 0.55;
    pads[13].morphDuration = 0.85; pads[13].morphCurve = 0.5;  // ~1.7 s
    pads[13].modeScatter = 0.65; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].clickLayerMix = 0.30; pads[13].clickLayerContactMs = 0.22;
    pads[13].clickLayerBrightness = 0.30;
    pads[13].noiseLayerMix = 0.20; pads[13].noiseLayerCutoff = 0.55;
    pads[13].noiseLayerColor = 0.45; pads[13].noiseLayerDecay = 0.92;
    pads[13].decaySkew = 0.65;
    pads[13].tensionModAmt = 0.0;   // Bell: tensionMod is a no-op
    pads[13].outputBus = 1;
    pads[13].macroBodySize = 0.95; pads[13].macroComplexity = 0.85;
    pads[13].couplingAmount = 0.95;
    pads[13].pan = 0.50;

    // Ride Cymbal (15, NEW role on the GM ride slot) -- Bell/NoiseBurst.
    pads[15].exciterType = ExciterType::NoiseBurst;
    pads[15].bodyModel = BodyModelType::Bell;
    pads[15].material = 0.95; pads[15].size = 0.30; pads[15].decay = 0.90;
    pads[15].level = 0.72;
    pads[15].strikePosition = 0.18;
    pads[15].modeStretch = 0.45; pads[15].decaySkew = 0.62;
    pads[15].nonlinearCoupling = 0.18; pads[15].modeScatter = 0.55;
    pads[15].bodyDampingB1 = 0.16; pads[15].bodyDampingB3 = 0.0;
    pads[15].noiseLayerMix = 0.45; pads[15].noiseLayerCutoff = 0.90;
    pads[15].noiseLayerColor = 0.90; pads[15].noiseLayerDecay = 0.78;  // Violet
    pads[15].clickLayerMix = 0.45; pads[15].clickLayerContactMs = 0.25;
    pads[15].clickLayerBrightness = 0.82;
    pads[15].airLoading = 0.0;
    pads[15].outputBus = 1;
    pads[15].macroBrightness = 0.85;
    pads[15].pan = 0.62;

    // Crotales hi (17) -- Bell/Mallet.
    pads[17].exciterType = ExciterType::Mallet;
    pads[17].bodyModel = BodyModelType::Bell;
    pads[17].material = 0.92; pads[17].size = 0.12; pads[17].decay = 0.85;
    pads[17].level = 0.72;
    pads[17].strikePosition = 0.10;
    pads[17].decaySkew = 0.58;
    pads[17].clickLayerMix = 0.42; pads[17].clickLayerContactMs = 0.20;
    pads[17].clickLayerBrightness = 0.85;
    pads[17].noiseLayerMix = 0.0;
    pads[17].airLoading = 0.0;
    pads[17].bodyDampingB3 = 0.0; pads[17].bodyDampingB1 = 0.30;
    pads[17].outputBus = 1;
    pads[17].macroBrightness = 0.85;
    pads[17].pan = 0.68;

    // Crotales lo (19, NEW) -- larger constituent bells.
    pads[19] = pads[17];
    pads[19].size = 0.22; pads[19].material = 0.88;
    pads[19].pan = 0.32;

    // Bell Tree (1, NEW) -- Bell/NoiseBurst cascade with morph dim.
    pads[1].exciterType = ExciterType::NoiseBurst;
    pads[1].bodyModel = BodyModelType::Bell;
    pads[1].material = 0.93; pads[1].size = 0.10; pads[1].decay = 0.60;
    pads[1].strikePosition = 0.15;
    pads[1].modeStretch = 0.45; pads[1].decaySkew = 0.78;
    pads[1].modeScatter = 0.35;
    pads[1].bodyDampingB1 = 0.35; pads[1].bodyDampingB3 = 0.0;
    pads[1].noiseLayerMix = 0.15; pads[1].noiseLayerColor = 0.90;  // Violet
    pads[1].noiseLayerDecay = 0.40;
    pads[1].clickLayerMix = 0.30; pads[1].clickLayerBrightness = 0.85;
    pads[1].morphEnabled = 1.0;
    pads[1].morphStart = 0.85; pads[1].morphEnd = 0.55;
    pads[1].morphDuration = 0.50; pads[1].morphCurve = 0.0;  // ~1.1 s linear
    pads[1].airLoading = 0.0;
    pads[1].outputBus = 1;
    pads[1].pan = 0.55;

    // Tubular bell (3) -- String/Mallet (modal axes inert no-ops on String).
    pads[3].exciterType = ExciterType::Mallet;
    pads[3].bodyModel = BodyModelType::String;
    pads[3].material = 0.85; pads[3].size = 0.55; pads[3].decay = 0.92;
    pads[3].level = 0.78;
    pads[3].strikePosition = 0.30;
    pads[3].modeStretch = 0.50;
    pads[3].clickLayerMix = 0.40; pads[3].clickLayerContactMs = 0.20;
    pads[3].clickLayerBrightness = 0.65;
    pads[3].noiseLayerMix = 0.10;
    pads[3].airLoading = 0.0;
    pads[3].bodyDampingB1 = 0.30; pads[3].bodyDampingB3 = 0.20;
    pads[3].decaySkew = 0.55;
    pads[3].outputBus = 1;
    pads[3].pan = 0.58;

    // Kit globals: headroom recovered post-N-1, so coupling/polyphony
    // restored (long timpani/gong/chime tails overlap; sympathetic
    // timpani resonance back on).
    k.opts.maxPolyphony    = 20;
    k.opts.globalCoupling  = 0.28;
    k.opts.snareBuzz       = 0.20;
    k.opts.tomResonance    = 0.35;
    k.opts.couplingDelayMs = 1.6;
    // 18 sounding pads; 16, 18, 20-31 disabled (no aux-perc role).
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 19};
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

    // 909 snare (redesigned: dual tonal layers + long noise tail. The
    // primary membrane is the boop body with moderate damping (decays in
    // ~150 ms, NOT a steady sine and NOT instant); secondary metallic body
    // adds the high "ping" the 909 is famous for; noise extends longest
    // for the snare-wire shhh tail.)
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.58; pads[2].size = 0.58; pads[2].decay = 0.55;
    pads[2].level = 1.0;
    pads[2].noiseBurstDuration = (3.0 - 2.0) / 13.0;
    pads[2].tsDriveAmount = 0.48;
    pads[2].tsFilterType = FilterType::LP;
    pads[2].tsFilterCutoff = 0.82;
    pads[2].tsFilterResonance = 0.22;
    pads[2].tsFilterEnvAmount = 0.60;
    pads[2].tsFilterEnvAttack = 0.0;
    pads[2].tsFilterEnvDecay = 0.385;
    pads[2].tsFilterEnvSustain = 0.0;
    pads[2].tsFilterEnvRelease = 0.20;
    pads[2].tensionModAmt = 0.32;
    pads[2].tsPitchEnvStart = toLogNorm(380);
    pads[2].tsPitchEnvEnd   = toLogNorm(140);
    pads[2].tsPitchEnvTime  = 0.09;
    pads[2].tsPitchEnvCurve = 0.10;
    // Metallic secondary body for the 909 "ping" shimmer.
    pads[2].couplingStrength = 0.38;
    pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize    = 0.32;
    pads[2].secondaryMaterial= 0.90;
    // Noise: long tail (decay > body damping) so the snare has the "shhh"
    // sustain that distinguishes a snare from a tom or hat.
    pads[2].noiseLayerMix    = 0.78; pads[2].noiseLayerCutoff = 0.86;
    pads[2].noiseLayerResonance = 0.15;
    pads[2].noiseLayerColor  = 0.86; pads[2].noiseLayerDecay = 0.48;
    pads[2].clickLayerMix    = 0.62; pads[2].clickLayerContactMs = 0.08;
    pads[2].clickLayerBrightness = 0.78;
    pads[2].airLoading = 0.0; pads[2].modeScatter = 0.0;
    // Moderate body damping: body sustains audibly (~120-150 ms) instead of
    // either ringing forever (steady sine) or decaying instantly (thin hat).
    pads[2].bodyDampingB1 = 0.38; pads[2].bodyDampingB3 = 0.20;
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
        pads[p].tsPitchEnvCurve = 0.15; // Phase 10: was "Exp" StringList -> norm 0.15 ~= curveAmount -0.7 (legacy log shape)
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

    // LinnDrum snare (redesigned: retro sampled-style, audible body + click)
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.45; pads[2].size = 0.52; pads[2].decay = 0.45;
    pads[2].level = 1.0;
    pads[2].noiseBurstDuration = (4.0 - 2.0) / 13.0;
    pads[2].tsDriveAmount = 0.28;
    pads[2].tsFilterType = FilterType::LP;
    pads[2].tsFilterCutoff = 0.82;
    pads[2].tsFilterResonance = 0.18;
    pads[2].tsFilterEnvAmount = 0.68;
    pads[2].tsFilterEnvAttack = 0.0;
    pads[2].tsFilterEnvDecay = 0.385;
    pads[2].tsFilterEnvSustain = 0.0;
    pads[2].tsFilterEnvRelease = 0.20;
    pads[2].tensionModAmt = 0.28;
    pads[2].tsPitchEnvStart = toLogNorm(240);
    pads[2].tsPitchEnvEnd   = toLogNorm(170);
    pads[2].tsPitchEnvTime  = 0.11;
    pads[2].tsPitchEnvCurve = 0.15;
    pads[2].noiseLayerMix = 0.82; pads[2].noiseLayerCutoff = 0.84;
    pads[2].noiseLayerResonance = 0.12;
    pads[2].noiseLayerColor = 0.78; pads[2].noiseLayerDecay = 0.28;
    pads[2].clickLayerMix = 0.88; pads[2].clickLayerContactMs = 0.07;
    pads[2].clickLayerBrightness = 0.85;
    pads[2].airLoading = 0.0; pads[2].modeScatter = 0.10;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.10;

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
        pads[p].tsPitchEnvCurve = 0.15; // Phase 10: was "Exp" StringList -> norm 0.15 ~= curveAmount -0.7 (legacy log shape)
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

    // Snare (FM Plate, redesigned: keeps FM character, gains body + click)
    pads[2].exciterType = ExciterType::FMImpulse;
    pads[2].bodyModel = BodyModelType::Plate;
    pads[2].material = 0.45; pads[2].size = 0.55; pads[2].decay = 0.55;
    pads[2].level = 0.95;
    pads[2].fmRatio = 0.55;
    pads[2].modeStretch = 0.55;
    pads[2].modeInjectAmount = 0.0;
    pads[2].nonlinearCoupling = 0.45;
    pads[2].tsDriveAmount = 0.28;
    pads[2].tsFilterType = FilterType::LP;
    pads[2].tsFilterCutoff = 0.85;
    pads[2].tsFilterResonance = 0.20;
    pads[2].tsFilterEnvAmount = 0.70;
    pads[2].tsFilterEnvAttack = 0.0;
    pads[2].tsFilterEnvDecay = 0.385;
    pads[2].tsFilterEnvSustain = 0.0;
    pads[2].tsFilterEnvRelease = 0.20;
    pads[2].tensionModAmt = 0.30;
    pads[2].tsPitchEnvStart = toLogNorm(230);
    pads[2].tsPitchEnvEnd   = toLogNorm(160);
    pads[2].tsPitchEnvTime  = 0.12;
    pads[2].tsPitchEnvCurve = 0.15;
    pads[2].morphEnabled = 1.0;
    pads[2].morphStart = 0.55; pads[2].morphEnd = 0.80;
    pads[2].morphDuration = 0.30; pads[2].morphCurve = 0.6;
    pads[2].noiseLayerMix = 0.60; pads[2].noiseLayerCutoff = 0.78;
    pads[2].noiseLayerResonance = 0.12;
    pads[2].noiseLayerColor = 0.70; pads[2].noiseLayerDecay = 0.30;
    pads[2].clickLayerMix = 0.85; pads[2].clickLayerContactMs = 0.07;
    pads[2].clickLayerBrightness = 0.85;
    pads[2].airLoading = 0.0; pads[2].modeScatter = 0.45;
    pads[2].bodyDampingB1 = 0.28; pads[2].bodyDampingB3 = 0.18;

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

    // Modern crispy snare (redesigned: trap brightness + audible housing)
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.48; pads[2].size = 0.56; pads[2].decay = 0.52;
    pads[2].level = 1.0;
    pads[2].noiseBurstDuration = (3.0 - 2.0) / 13.0;
    pads[2].tsFilterType = FilterType::LP;
    pads[2].tsFilterCutoff = 0.92;
    pads[2].tsFilterResonance = 0.22;
    pads[2].tsFilterEnvAmount = 0.78;
    pads[2].tsFilterEnvAttack = 0.0;
    pads[2].tsFilterEnvDecay = 0.385;
    pads[2].tsFilterEnvSustain = 0.0;
    pads[2].tsFilterEnvRelease = 0.20;
    pads[2].tsDriveAmount = 0.34;
    pads[2].tensionModAmt = 0.32;
    pads[2].tsPitchEnvStart = toLogNorm(250);
    pads[2].tsPitchEnvEnd   = toLogNorm(170);
    pads[2].tsPitchEnvTime  = 0.10;
    pads[2].tsPitchEnvCurve = 0.15;
    pads[2].noiseLayerMix = 0.90; pads[2].noiseLayerCutoff = 0.95;
    pads[2].noiseLayerResonance = 0.20;
    pads[2].noiseLayerColor = 0.96; pads[2].noiseLayerDecay = 0.30;
    pads[2].clickLayerMix = 0.95; pads[2].clickLayerContactMs = 0.06;
    pads[2].clickLayerBrightness = 0.97;
    pads[2].airLoading = 0.10; pads[2].modeScatter = 0.25;
    pads[2].nonlinearCoupling = 0.18;
    pads[2].couplingStrength = 0.55; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.62; pads[2].secondaryMaterial = 0.55;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.10;
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
        pads[p].tsPitchEnvCurve = 0.15; // Phase 10: was "Exp" StringList -> norm 0.15 ~= curveAmount -0.7 (legacy log shape)
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

    // 100% Membrane+Impulse pitched hand-drum ensemble. Cross-kit
    // duplicates (cajon/woodblock/frame/shaker) removed; pads 3/5/7/9/11
    // re-cast as hand-drum articulations + new voices on 1/13/14/15.
    // NEW axes per plan: pan spread, decaySkew tilt (0.38 dry -> 0.62 deep),
    // modeInject 1/k on deep voices, NLC bloom on open/tone/bass (0 on
    // slaps/tips), tensionMod 0 on chokes -> 0.45 on the udu hand-bend.

    // Conga lo / tumba open (0)
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.45; pads[0].size = 0.62; pads[0].decay = 0.40;
    pads[0].level = 0.80; pads[0].strikePosition = 0.45;
    pads[0].tsPitchEnvStart = toLogNorm(200);
    pads[0].tsPitchEnvEnd   = toLogNorm(150);
    pads[0].tsPitchEnvTime  = 0.04;
    pads[0].airLoading = 0.55; pads[0].modeScatter = 0.10;
    pads[0].modeInjectAmount = 0.10; pads[0].decaySkew = 0.60;
    pads[0].nonlinearCoupling = 0.18;
    pads[0].couplingStrength = 0.32; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.48; pads[0].secondaryMaterial = 0.40;
    pads[0].tensionModAmt = 0.20;
    pads[0].clickLayerMix = 0.50; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.55;
    pads[0].noiseLayerMix = 0.10; pads[0].noiseLayerColor = 0.40;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.10;
    pads[0].macroBodySize = 0.55;
    pads[0].pan = 0.40;

    // Conga lo MUTE (1, new) -- damped tumba, no glide
    pads[1].exciterType = ExciterType::Impulse;
    pads[1].bodyModel = BodyModelType::Membrane;
    pads[1].material = 0.45; pads[1].size = 0.62; pads[1].decay = 0.14;
    pads[1].level = 0.74; pads[1].strikePosition = 0.30;
    pads[1].airLoading = 0.50; pads[1].modeScatter = 0.15;
    pads[1].decaySkew = 0.42;
    pads[1].couplingStrength = 0.30; pads[1].secondaryEnabled = 1.0;
    pads[1].secondarySize = 0.48; pads[1].secondaryMaterial = 0.40;
    pads[1].tensionModAmt = 0.0;
    pads[1].clickLayerMix = 0.55; pads[1].clickLayerContactMs = 0.15;
    pads[1].clickLayerBrightness = 0.50;
    pads[1].noiseLayerMix = 0.08; pads[1].noiseLayerColor = 0.40;
    pads[1].bodyDampingB1 = 0.45; pads[1].bodyDampingB3 = 0.10;
    pads[1].pan = 0.40;

    // Conga hi open (2)
    pads[2].exciterType = ExciterType::Impulse;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.50; pads[2].size = 0.50; pads[2].decay = 0.32;
    pads[2].level = 0.80; pads[2].strikePosition = 0.30;
    pads[2].tsPitchEnvStart = toLogNorm(280);
    pads[2].tsPitchEnvEnd   = toLogNorm(210);
    pads[2].tsPitchEnvTime  = 0.04;
    pads[2].airLoading = 0.50; pads[2].modeScatter = 0.10;
    pads[2].decaySkew = 0.50; pads[2].nonlinearCoupling = 0.14;
    pads[2].couplingStrength = 0.28; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.40; pads[2].secondaryMaterial = 0.40;
    pads[2].tensionModAmt = 0.18;
    pads[2].clickLayerMix = 0.55; pads[2].clickLayerContactMs = 0.16;
    pads[2].clickLayerBrightness = 0.65;
    pads[2].noiseLayerMix = 0.12; pads[2].noiseLayerColor = 0.45;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.10;
    pads[2].pan = 0.46;

    // Quinto open (3, new -- highest conga, replaces Shaker)
    pads[3].exciterType = ExciterType::Impulse;
    pads[3].bodyModel = BodyModelType::Membrane;
    pads[3].material = 0.52; pads[3].size = 0.42; pads[3].decay = 0.28;
    pads[3].level = 0.80; pads[3].strikePosition = 0.30;
    pads[3].tsPitchEnvStart = toLogNorm(320);
    pads[3].tsPitchEnvEnd   = toLogNorm(250);
    pads[3].tsPitchEnvTime  = 0.04;
    pads[3].airLoading = 0.45; pads[3].modeScatter = 0.10;
    pads[3].decaySkew = 0.46; pads[3].nonlinearCoupling = 0.12;
    pads[3].couplingStrength = 0.26; pads[3].secondaryEnabled = 1.0;
    pads[3].secondarySize = 0.38; pads[3].secondaryMaterial = 0.40;
    pads[3].tensionModAmt = 0.20;
    pads[3].clickLayerMix = 0.55; pads[3].clickLayerContactMs = 0.16;
    pads[3].clickLayerBrightness = 0.68;
    pads[3].noiseLayerMix = 0.10; pads[3].noiseLayerColor = 0.40;
    pads[3].bodyDampingB1 = 0.30; pads[3].bodyDampingB3 = 0.10;
    pads[3].pan = 0.52;

    // Conga slap (4) -- choked, no glide/NLC
    pads[4].exciterType = ExciterType::Impulse;
    pads[4].bodyModel = BodyModelType::Membrane;
    pads[4].material = 0.55; pads[4].size = 0.50; pads[4].decay = 0.18;
    pads[4].level = 0.85; pads[4].strikePosition = 0.10;
    pads[4].airLoading = 0.40; pads[4].modeScatter = 0.30;
    pads[4].decaySkew = 0.40;
    pads[4].couplingStrength = 0.20; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.40; pads[4].secondaryMaterial = 0.40;
    pads[4].tensionModAmt = 0.0;
    pads[4].clickLayerMix = 0.85; pads[4].clickLayerContactMs = 0.10;
    pads[4].clickLayerBrightness = 0.85;
    pads[4].noiseLayerMix = 0.15; pads[4].noiseLayerColor = 0.40;
    pads[4].bodyDampingB1 = 0.45; pads[4].bodyDampingB3 = 0.10;
    pads[4].macroPunch = 0.85;
    pads[4].pan = 0.42;

    // Conga heel-tip (5, new -- driest stroke, replaces Cajon bass)
    pads[5].exciterType = ExciterType::Impulse;
    pads[5].bodyModel = BodyModelType::Membrane;
    pads[5].material = 0.50; pads[5].size = 0.50; pads[5].decay = 0.10;
    pads[5].level = 0.70; pads[5].strikePosition = 0.50;
    pads[5].airLoading = 0.45; pads[5].modeScatter = 0.20;
    pads[5].decaySkew = 0.38;
    pads[5].couplingStrength = 0.20; pads[5].secondaryEnabled = 1.0;
    pads[5].secondarySize = 0.40; pads[5].secondaryMaterial = 0.40;
    pads[5].tensionModAmt = 0.0;
    pads[5].clickLayerMix = 0.60; pads[5].clickLayerContactMs = 0.20;
    pads[5].clickLayerBrightness = 0.45;
    pads[5].noiseLayerMix = 0.08; pads[5].noiseLayerColor = 0.40;
    pads[5].bodyDampingB1 = 0.50; pads[5].bodyDampingB3 = 0.10;
    pads[5].pan = 0.38;

    // Bongo hi / macho open (6)
    pads[6].exciterType = ExciterType::Impulse;
    pads[6].bodyModel = BodyModelType::Membrane;
    pads[6].material = 0.55; pads[6].size = 0.32; pads[6].decay = 0.28;
    pads[6].level = 0.80; pads[6].strikePosition = 0.30;
    pads[6].tsPitchEnvStart = toLogNorm(420);
    pads[6].tsPitchEnvEnd   = toLogNorm(350);
    pads[6].tsPitchEnvTime  = 0.04;
    pads[6].airLoading = 0.42; pads[6].modeScatter = 0.10;
    pads[6].decaySkew = 0.46; pads[6].nonlinearCoupling = 0.12;
    pads[6].couplingStrength = 0.25; pads[6].secondaryEnabled = 1.0;
    pads[6].secondarySize = 0.30; pads[6].secondaryMaterial = 0.40;
    pads[6].tensionModAmt = 0.22;
    pads[6].clickLayerMix = 0.55; pads[6].clickLayerContactMs = 0.15;
    pads[6].clickLayerBrightness = 0.72;
    pads[6].noiseLayerMix = 0.10; pads[6].noiseLayerColor = 0.40;
    pads[6].bodyDampingB1 = 0.30; pads[6].bodyDampingB3 = 0.10;
    pads[6].pan = 0.60;

    // Bongo hi slap (7, new -- replaces Cajon slap)
    pads[7] = pads[6];
    pads[7].decay = 0.16; pads[7].strikePosition = 0.10;
    pads[7].tsPitchEnvTime = 0.0;   // slap: no glide
    pads[7].decaySkew = 0.40; pads[7].nonlinearCoupling = 0.0;
    pads[7].tensionModAmt = 0.0;
    pads[7].clickLayerMix = 0.85; pads[7].clickLayerContactMs = 0.10;
    pads[7].clickLayerBrightness = 0.82;
    pads[7].bodyDampingB1 = 0.45;
    pads[7].pan = 0.62;

    // Bongo lo / hembra open (8)
    pads[8] = pads[6];
    pads[8].size = 0.40; pads[8].decay = 0.32;
    pads[8].tsPitchEnvStart = toLogNorm(340);
    pads[8].tsPitchEnvEnd   = toLogNorm(280);
    pads[8].tsPitchEnvTime  = 0.04;
    pads[8].decaySkew = 0.50; pads[8].nonlinearCoupling = 0.14;
    pads[8].pan = 0.56;

    // Bongo lo slap (9, new -- replaces Wood Block)
    pads[9] = pads[8];
    pads[9].decay = 0.18; pads[9].strikePosition = 0.10;
    pads[9].tsPitchEnvTime = 0.0;   // slap: no glide
    pads[9].decaySkew = 0.42; pads[9].nonlinearCoupling = 0.0;
    pads[9].tensionModAmt = 0.0;
    pads[9].clickLayerMix = 0.85; pads[9].clickLayerContactMs = 0.10;
    pads[9].clickLayerBrightness = 0.80;
    pads[9].bodyDampingB1 = 0.45;
    pads[9].pan = 0.58;

    // Djembe bass (10)
    pads[10].exciterType = ExciterType::Impulse;
    pads[10].bodyModel = BodyModelType::Membrane;
    pads[10].material = 0.30; pads[10].size = 0.78; pads[10].decay = 0.45;
    pads[10].level = 0.85; pads[10].strikePosition = 0.50;
    pads[10].tsPitchEnvStart = toLogNorm(150);
    pads[10].tsPitchEnvEnd   = toLogNorm(85);
    pads[10].tsPitchEnvTime  = 0.05;
    pads[10].airLoading = 0.65; pads[10].modeScatter = 0.18;
    pads[10].modeInjectAmount = 0.12; pads[10].decaySkew = 0.60;
    pads[10].nonlinearCoupling = 0.20;
    pads[10].couplingStrength = 0.40; pads[10].secondaryEnabled = 1.0;
    pads[10].secondarySize = 0.50; pads[10].secondaryMaterial = 0.30;
    pads[10].tensionModAmt = 0.22;
    pads[10].clickLayerMix = 0.40; pads[10].clickLayerContactMs = 0.22;
    pads[10].clickLayerBrightness = 0.40;
    pads[10].noiseLayerMix = 0.18; pads[10].noiseLayerColor = 0.45;
    pads[10].bodyDampingB1 = 0.30; pads[10].bodyDampingB3 = 0.10;
    pads[10].macroBodySize = 0.85;
    pads[10].pan = 0.50;

    // Djembe tone (11, new -- mid tone, replaces Frame Drum)
    pads[11].exciterType = ExciterType::Impulse;
    pads[11].bodyModel = BodyModelType::Membrane;
    pads[11].material = 0.40; pads[11].size = 0.65; pads[11].decay = 0.30;
    pads[11].level = 0.80; pads[11].strikePosition = 0.35;
    pads[11].tsPitchEnvStart = toLogNorm(220);
    pads[11].tsPitchEnvEnd   = toLogNorm(160);
    pads[11].tsPitchEnvTime  = 0.04;
    pads[11].airLoading = 0.60; pads[11].modeScatter = 0.15;
    pads[11].decaySkew = 0.50; pads[11].nonlinearCoupling = 0.16;
    pads[11].couplingStrength = 0.35; pads[11].secondaryEnabled = 1.0;
    pads[11].secondarySize = 0.50; pads[11].secondaryMaterial = 0.30;
    pads[11].tensionModAmt = 0.18;
    pads[11].clickLayerMix = 0.55; pads[11].clickLayerContactMs = 0.18;
    pads[11].clickLayerBrightness = 0.60;
    pads[11].noiseLayerMix = 0.12; pads[11].noiseLayerColor = 0.40;
    pads[11].bodyDampingB1 = 0.32; pads[11].bodyDampingB3 = 0.10;
    pads[11].pan = 0.48;

    // Djembe slap (12) -- choked: reset glide/NLC/modeInject from bass copy
    pads[12] = pads[10];
    pads[12].decay = 0.20; pads[12].strikePosition = 0.10;
    pads[12].tsPitchEnvStart = toLogNorm(280);
    pads[12].tsPitchEnvEnd   = toLogNorm(220);
    pads[12].tsPitchEnvTime  = 0.0;   // slap: no glide
    pads[12].modeInjectAmount = 0.0; pads[12].nonlinearCoupling = 0.0;
    pads[12].decaySkew = 0.40; pads[12].tensionModAmt = 0.0;
    pads[12].clickLayerMix = 0.85; pads[12].clickLayerBrightness = 0.78;
    pads[12].clickLayerContactMs = 0.12;
    pads[12].bodyDampingB1 = 0.45;
    pads[12].pan = 0.46;

    // Udu / clay-pot bass (13, new) -- signature hand-over-hole bend
    pads[13].exciterType = ExciterType::Impulse;
    pads[13].bodyModel = BodyModelType::Membrane;
    pads[13].material = 0.30; pads[13].size = 0.78; pads[13].decay = 0.42;
    pads[13].level = 0.82; pads[13].strikePosition = 0.45;
    pads[13].tsPitchEnvStart = toLogNorm(149);
    pads[13].tsPitchEnvEnd   = toLogNorm(85);
    pads[13].tsPitchEnvTime  = 0.05;
    pads[13].airLoading = 0.70; pads[13].modeScatter = 0.18;
    pads[13].modeInjectAmount = 0.14; pads[13].decaySkew = 0.60;
    pads[13].nonlinearCoupling = 0.18;
    pads[13].couplingStrength = 0.40; pads[13].secondaryEnabled = 1.0;
    pads[13].secondarySize = 0.55; pads[13].secondaryMaterial = 0.30;
    pads[13].tensionModAmt = 0.45;   // hand-over-hole pitch bend (max in kit)
    pads[13].clickLayerMix = 0.40; pads[13].clickLayerContactMs = 0.22;
    pads[13].clickLayerBrightness = 0.40;
    pads[13].noiseLayerMix = 0.15; pads[13].noiseLayerColor = 0.40;
    pads[13].bodyDampingB1 = 0.30; pads[13].bodyDampingB3 = 0.10;
    pads[13].macroBodySize = 0.85;
    pads[13].pan = 0.50;

    // Tan-tan / repinique hi (14, new) -- bright nylon-head single drum
    pads[14].exciterType = ExciterType::Impulse;
    pads[14].bodyModel = BodyModelType::Membrane;
    pads[14].material = 0.45; pads[14].size = 0.36; pads[14].decay = 0.26;
    pads[14].level = 0.80; pads[14].strikePosition = 0.30;
    pads[14].tsPitchEnvStart = toLogNorm(348);
    pads[14].tsPitchEnvEnd   = toLogNorm(280);
    pads[14].tsPitchEnvTime  = 0.04;
    pads[14].airLoading = 0.45; pads[14].modeScatter = 0.12;
    pads[14].decaySkew = 0.46; pads[14].nonlinearCoupling = 0.12;
    pads[14].couplingStrength = 0.28; pads[14].secondaryEnabled = 1.0;
    pads[14].secondarySize = 0.36; pads[14].secondaryMaterial = 0.45;
    pads[14].tensionModAmt = 0.18;
    pads[14].clickLayerMix = 0.55; pads[14].clickLayerContactMs = 0.15;
    pads[14].clickLayerBrightness = 0.72;
    pads[14].noiseLayerMix = 0.10; pads[14].noiseLayerColor = 0.40;
    pads[14].bodyDampingB1 = 0.30; pads[14].bodyDampingB3 = 0.08;  // keep highs
    pads[14].pan = 0.64;

    // Surdo bass (15, new) -- deepest samba bass drum
    pads[15].exciterType = ExciterType::Impulse;
    pads[15].bodyModel = BodyModelType::Membrane;
    pads[15].material = 0.28; pads[15].size = 0.82; pads[15].decay = 0.48;
    pads[15].level = 0.86; pads[15].strikePosition = 0.45;
    pads[15].tsPitchEnvStart = toLogNorm(138);
    pads[15].tsPitchEnvEnd   = toLogNorm(80);
    pads[15].tsPitchEnvTime  = 0.05;
    pads[15].airLoading = 0.78; pads[15].modeScatter = 0.18;
    pads[15].modeInjectAmount = 0.10; pads[15].decaySkew = 0.62;
    pads[15].nonlinearCoupling = 0.20;
    pads[15].couplingStrength = 0.45; pads[15].secondaryEnabled = 1.0;
    pads[15].secondarySize = 0.55; pads[15].secondaryMaterial = 0.30;
    pads[15].tensionModAmt = 0.25;
    pads[15].clickLayerMix = 0.40; pads[15].clickLayerContactMs = 0.24;
    pads[15].clickLayerBrightness = 0.38;
    pads[15].noiseLayerMix = 0.15; pads[15].noiseLayerColor = 0.40;
    pads[15].bodyDampingB1 = 0.28; pads[15].bodyDampingB3 = 0.12;
    pads[15].macroBodySize = 0.90;
    pads[15].pan = 0.50;

    k.opts.maxPolyphony    = 12;
    k.opts.globalCoupling  = 0.30;
    k.opts.snareBuzz       = 0.15;
    k.opts.tomResonance    = 0.20;
    k.opts.couplingDelayMs = 0.9;
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    return k;
}
Kit latinPercKit() {
    Kit k{"Latin Perc", "Percussive", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Latin Perc -- 28 distinct voices, family-grouped. Key corrections:
    // clave/triangle Bell->Shell (free-free bar) with b3=0.70 wood override
    // on claves/castanets; timbales de-duplicated; cowbell feedback no-op
    // removed; pandeiro b1/b3 set explicit (metallic shimmer). Exact per-pad
    // values reconstructed from the cited archetypes + sibling kits within
    // the plan's documented semantics (no structured table in repo).

    // Bongo Macho hi (0) -- Membrane/Impulse
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.55; pads[0].size = 0.32; pads[0].decay = 0.28;
    pads[0].level = 0.80; pads[0].strikePosition = 0.30;
    pads[0].tsPitchEnvStart = toLogNorm(420);
    pads[0].tsPitchEnvEnd   = toLogNorm(350);
    pads[0].tsPitchEnvTime  = 0.04;
    pads[0].airLoading = 0.42; pads[0].modeScatter = 0.10;
    pads[0].decaySkew = 0.46; pads[0].nonlinearCoupling = 0.12;
    pads[0].couplingStrength = 0.25; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.30; pads[0].secondaryMaterial = 0.40;
    pads[0].tensionModAmt = 0.22;
    pads[0].clickLayerMix = 0.55; pads[0].clickLayerContactMs = 0.15;
    pads[0].clickLayerBrightness = 0.72;
    pads[0].noiseLayerMix = 0.10; pads[0].noiseLayerColor = 0.40;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.10;
    pads[0].pan = 0.60;

    // Bongo Hembra lo (1)
    pads[1] = pads[0];
    pads[1].size = 0.40; pads[1].decay = 0.32;
    pads[1].tsPitchEnvStart = toLogNorm(340);
    pads[1].tsPitchEnvEnd   = toLogNorm(280);
    pads[1].decaySkew = 0.50; pads[1].nonlinearCoupling = 0.14;
    pads[1].pan = 0.56;

    // Conga Hi open (2)
    pads[2].exciterType = ExciterType::Impulse;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.50; pads[2].size = 0.50; pads[2].decay = 0.32;
    pads[2].level = 0.80; pads[2].strikePosition = 0.30;
    pads[2].tsPitchEnvStart = toLogNorm(280);
    pads[2].tsPitchEnvEnd   = toLogNorm(210);
    pads[2].tsPitchEnvTime  = 0.04;
    pads[2].airLoading = 0.50; pads[2].modeScatter = 0.10;
    pads[2].decaySkew = 0.50; pads[2].nonlinearCoupling = 0.14;
    pads[2].couplingStrength = 0.28; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.40; pads[2].secondaryMaterial = 0.40;
    pads[2].tensionModAmt = 0.18;
    pads[2].clickLayerMix = 0.55; pads[2].clickLayerContactMs = 0.16;
    pads[2].clickLayerBrightness = 0.65;
    pads[2].noiseLayerMix = 0.12; pads[2].noiseLayerColor = 0.45;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.10;
    pads[2].pan = 0.44;

    // Conga Lo open (3)
    pads[3].exciterType = ExciterType::Impulse;
    pads[3].bodyModel = BodyModelType::Membrane;
    pads[3].material = 0.45; pads[3].size = 0.62; pads[3].decay = 0.40;
    pads[3].level = 0.80; pads[3].strikePosition = 0.45;
    pads[3].tsPitchEnvStart = toLogNorm(200);
    pads[3].tsPitchEnvEnd   = toLogNorm(150);
    pads[3].tsPitchEnvTime  = 0.04;
    pads[3].airLoading = 0.55; pads[3].modeScatter = 0.10;
    pads[3].modeInjectAmount = 0.10; pads[3].decaySkew = 0.60;
    pads[3].nonlinearCoupling = 0.18;
    pads[3].couplingStrength = 0.32; pads[3].secondaryEnabled = 1.0;
    pads[3].secondarySize = 0.48; pads[3].secondaryMaterial = 0.40;
    pads[3].tensionModAmt = 0.20;
    pads[3].clickLayerMix = 0.50; pads[3].clickLayerContactMs = 0.18;
    pads[3].clickLayerBrightness = 0.55;
    pads[3].noiseLayerMix = 0.10; pads[3].noiseLayerColor = 0.40;
    pads[3].bodyDampingB1 = 0.30; pads[3].bodyDampingB3 = 0.10;
    pads[3].macroBodySize = 0.55;
    pads[3].pan = 0.40;

    // Conga Slap (4)
    pads[4].exciterType = ExciterType::Impulse;
    pads[4].bodyModel = BodyModelType::Membrane;
    pads[4].material = 0.55; pads[4].size = 0.50; pads[4].decay = 0.18;
    pads[4].level = 0.85; pads[4].strikePosition = 0.10;
    pads[4].airLoading = 0.40; pads[4].modeScatter = 0.30;
    pads[4].decaySkew = 0.40;
    pads[4].couplingStrength = 0.20; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.40; pads[4].secondaryMaterial = 0.40;
    pads[4].tensionModAmt = 0.0;
    pads[4].clickLayerMix = 0.85; pads[4].clickLayerContactMs = 0.10;
    pads[4].clickLayerBrightness = 0.85;
    pads[4].noiseLayerMix = 0.15; pads[4].noiseLayerColor = 0.40;
    pads[4].bodyDampingB1 = 0.45; pads[4].bodyDampingB3 = 0.10;
    pads[4].macroPunch = 0.85;
    pads[4].pan = 0.42;

    // Timbale Hi macho (5) -- Membrane/NoiseBurst + metal shell
    pads[5].exciterType = ExciterType::NoiseBurst;
    pads[5].bodyModel = BodyModelType::Membrane;
    pads[5].material = 0.55; pads[5].size = 0.40; pads[5].decay = 0.30;
    pads[5].level = 0.82;
    pads[5].noiseBurstDuration = 0.25;
    pads[5].tsPitchEnvStart = toLogNorm(380);
    pads[5].tsPitchEnvEnd   = toLogNorm(280);
    pads[5].tsPitchEnvTime  = 0.04;
    pads[5].airLoading = 0.40; pads[5].modeScatter = 0.18;
    pads[5].decaySkew = 0.50;
    pads[5].couplingStrength = 0.50; pads[5].secondaryEnabled = 1.0;
    pads[5].secondarySize = 0.40; pads[5].secondaryMaterial = 0.65;
    pads[5].tensionModAmt = 0.22;
    pads[5].clickLayerMix = 0.65; pads[5].clickLayerContactMs = 0.12;
    pads[5].clickLayerBrightness = 0.78;
    pads[5].noiseLayerMix = 0.30; pads[5].noiseLayerColor = 0.65;
    pads[5].bodyDampingB1 = 0.32; pads[5].bodyDampingB3 = 0.10;
    pads[5].pan = 0.64;

    // Timbale Lo hembra (6)
    pads[6] = pads[5];
    pads[6].size = 0.55; pads[6].decay = 0.40; pads[6].material = 0.50;
    pads[6].tsPitchEnvStart = toLogNorm(280);
    pads[6].tsPitchEnvEnd   = toLogNorm(200);
    pads[6].secondarySize = 0.50;
    pads[6].pan = 0.60;

    // Cowbell Hi (7) -- Bell/FMImpulse (feedback no-op removed)
    pads[7].exciterType = ExciterType::FMImpulse;
    pads[7].bodyModel = BodyModelType::Bell;
    pads[7].material = 0.78; pads[7].size = 0.20; pads[7].decay = 0.30;
    pads[7].level = 0.78;
    pads[7].fmRatio = 0.55;
    pads[7].modeStretch = 0.50; pads[7].decaySkew = 0.50;
    pads[7].clickLayerMix = 0.55; pads[7].clickLayerContactMs = 0.10;
    pads[7].clickLayerBrightness = 0.78;
    pads[7].noiseLayerMix = 0.10; pads[7].noiseLayerColor = 0.65;
    pads[7].airLoading = 0.0; pads[7].modeScatter = 0.18;
    pads[7].bodyDampingB3 = 0.0; pads[7].bodyDampingB1 = 0.32;
    pads[7].macroBrightness = 0.75;
    pads[7].pan = 0.66;

    // Cowbell Lo (8)
    pads[8] = pads[7];
    pads[8].size = 0.28; pads[8].decay = 0.40; pads[8].fmRatio = 0.42;
    pads[8].pan = 0.62;

    // Agogo Hi (9) -- Bell/FMImpulse
    pads[9].exciterType = ExciterType::FMImpulse;
    pads[9].bodyModel = BodyModelType::Bell;
    pads[9].material = 0.85; pads[9].size = 0.14; pads[9].decay = 0.28;
    pads[9].level = 0.75;
    pads[9].fmRatio = 0.72;
    pads[9].decaySkew = 0.50; pads[9].modeScatter = 0.15;
    pads[9].clickLayerMix = 0.55; pads[9].clickLayerBrightness = 0.85;
    pads[9].noiseLayerMix = 0.0;
    pads[9].airLoading = 0.0;
    pads[9].bodyDampingB3 = 0.0; pads[9].bodyDampingB1 = 0.30;
    pads[9].pan = 0.36;

    // Agogo Lo (10)
    pads[10] = pads[9];
    pads[10].size = 0.22; pads[10].fmRatio = 0.55; pads[10].decay = 0.35;
    pads[10].pan = 0.40;

    // Clave Hi (11) -- Bell -> Shell (free-free bar), b3=0.70 dry-wood override
    pads[11].exciterType = ExciterType::Impulse;
    pads[11].bodyModel = BodyModelType::Shell;
    pads[11].material = 0.85; pads[11].size = 0.10; pads[11].decay = 0.12;
    pads[11].level = 0.80; pads[11].strikePosition = 0.85;  // free-end antinode
    pads[11].modeStretch = 0.50; pads[11].decaySkew = 0.45;
    pads[11].modeScatter = 0.10;
    pads[11].clickLayerMix = 0.92; pads[11].clickLayerContactMs = 0.06;
    pads[11].clickLayerBrightness = 0.92;
    pads[11].noiseLayerMix = 0.0;
    pads[11].airLoading = 0.0;
    pads[11].bodyDampingB3 = 0.70; pads[11].bodyDampingB1 = 0.40;
    pads[11].macroBrightness = 0.85;
    pads[11].pan = 0.34;

    // Clave Lo (12)
    pads[12] = pads[11];
    pads[12].size = 0.16; pads[12].decay = 0.16;
    pads[12].pan = 0.38;

    // Woodblock Hi (13) -- Plate/Impulse (free-plate Chladni)
    pads[13].exciterType = ExciterType::Impulse;
    pads[13].bodyModel = BodyModelType::Plate;
    pads[13].material = 0.32; pads[13].size = 0.18; pads[13].decay = 0.18;
    pads[13].level = 0.76; pads[13].strikePosition = 0.30;
    pads[13].modeStretch = 0.55; pads[13].decaySkew = 0.30;
    pads[13].modeScatter = 0.18;
    pads[13].clickLayerMix = 0.85; pads[13].clickLayerContactMs = 0.10;
    pads[13].clickLayerBrightness = 0.78;
    pads[13].noiseLayerMix = 0.0;
    pads[13].airLoading = 0.0;
    pads[13].bodyDampingB1 = 0.45; pads[13].bodyDampingB3 = 0.10;
    pads[13].pan = 0.66;

    // Woodblock Lo (14)
    pads[14] = pads[13];
    pads[14].size = 0.28; pads[14].decay = 0.22; pads[14].material = 0.30;
    pads[14].pan = 0.62;

    // Maracas (15) -- NoiseBody, PhISEM tight (reso 0.40, r~0.96)
    pads[15].exciterType = ExciterType::NoiseBurst;
    pads[15].bodyModel = BodyModelType::NoiseBody;
    pads[15].material = 0.78; pads[15].size = 0.14; pads[15].decay = 0.18;
    pads[15].level = 0.65;
    pads[15].noiseBurstDuration = 0.45;
    pads[15].noiseLayerMix = 0.85; pads[15].noiseLayerCutoff = 0.65;
    pads[15].noiseLayerResonance = 0.40;
    pads[15].noiseLayerColor = 0.70; pads[15].noiseLayerDecay = 0.18;
    pads[15].clickLayerMix = 0.0;
    pads[15].airLoading = 0.0; pads[15].modeScatter = 0.42;
    pads[15].bodyDampingB3 = 0.0; pads[15].bodyDampingB1 = 0.45;
    pads[15].pan = 0.44;

    // Cabasa (16) -- NoiseBody, PhISEM broad (reso 0.16, r=0.7)
    pads[16].exciterType = ExciterType::NoiseBurst;
    pads[16].bodyModel = BodyModelType::NoiseBody;
    pads[16].material = 0.85; pads[16].size = 0.10; pads[16].decay = 0.10;
    pads[16].level = 0.65;
    pads[16].noiseBurstDuration = 0.25;
    pads[16].noiseLayerMix = 0.85; pads[16].noiseLayerCutoff = 0.73;
    pads[16].noiseLayerResonance = 0.16;
    pads[16].noiseLayerColor = 0.75; pads[16].noiseLayerDecay = 0.10;
    pads[16].clickLayerMix = 0.0;
    pads[16].airLoading = 0.0; pads[16].modeScatter = 0.30;
    pads[16].bodyDampingB3 = 0.0; pads[16].bodyDampingB1 = 0.55;
    pads[16].pan = 0.56;

    // Hand Shaker (17) -- NoiseBody, PhISEM mid
    pads[17].exciterType = ExciterType::NoiseBurst;
    pads[17].bodyModel = BodyModelType::NoiseBody;
    pads[17].material = 0.82; pads[17].size = 0.12; pads[17].decay = 0.12;
    pads[17].level = 0.66;
    pads[17].noiseBurstDuration = 0.35;
    pads[17].noiseLayerMix = 0.85; pads[17].noiseLayerCutoff = 0.70;
    pads[17].noiseLayerResonance = 0.28;
    pads[17].noiseLayerColor = 0.72; pads[17].noiseLayerDecay = 0.12;
    pads[17].clickLayerMix = 0.0;
    pads[17].airLoading = 0.0; pads[17].modeScatter = 0.36;
    pads[17].bodyDampingB3 = 0.0; pads[17].bodyDampingB1 = 0.50;
    pads[17].pan = 0.50;

    // Guiro (18) -- NoiseBody/Friction scrape
    pads[18].exciterType = ExciterType::Friction;
    pads[18].bodyModel = BodyModelType::NoiseBody;
    pads[18].material = 0.65; pads[18].size = 0.18; pads[18].decay = 0.30;
    pads[18].level = 0.70;
    pads[18].frictionPressure = 0.55;
    pads[18].noiseLayerMix = 0.65; pads[18].noiseLayerCutoff = 0.62;
    pads[18].noiseLayerColor = 0.55; pads[18].noiseLayerDecay = 0.30;
    pads[18].clickLayerMix = 0.0;
    pads[18].airLoading = 0.0; pads[18].modeScatter = 0.30;
    pads[18].bodyDampingB3 = 0.0; pads[18].bodyDampingB1 = 0.42;
    pads[18].decaySkew = 0.55;
    pads[18].pan = 0.58;

    // Tambourine (19) -- NoiseBody jingle, choke group 1
    pads[19].exciterType = ExciterType::NoiseBurst;
    pads[19].bodyModel = BodyModelType::NoiseBody;
    pads[19].material = 0.85; pads[19].size = 0.20; pads[19].decay = 0.22;
    pads[19].level = 0.72;
    pads[19].chokeGroup = 1;
    pads[19].noiseBurstDuration = 0.30;
    pads[19].noiseLayerMix = 0.78; pads[19].noiseLayerCutoff = 0.92;
    pads[19].noiseLayerColor = 0.92; pads[19].noiseLayerDecay = 0.22;
    pads[19].clickLayerMix = 0.30; pads[19].clickLayerBrightness = 0.85;
    pads[19].airLoading = 0.0; pads[19].modeScatter = 0.55;
    pads[19].couplingStrength = 0.40; pads[19].secondaryEnabled = 1.0;
    pads[19].secondarySize = 0.20; pads[19].secondaryMaterial = 0.85;
    pads[19].bodyDampingB3 = 0.0; pads[19].bodyDampingB1 = 0.32;
    pads[19].macroComplexity = 0.65;
    pads[19].pan = 0.62;

    // Pandeiro (20) -- NoiseBody jingle + head, choke group 1.
    // b1/b3 explicit (struct b3=0.40 would kill the metallic shimmer).
    pads[20].exciterType = ExciterType::NoiseBurst;
    pads[20].bodyModel = BodyModelType::NoiseBody;
    pads[20].material = 0.85; pads[20].size = 0.22; pads[20].decay = 0.25;
    pads[20].level = 0.72;
    pads[20].chokeGroup = 1;
    pads[20].noiseBurstDuration = 0.35;
    pads[20].noiseLayerMix = 0.70; pads[20].noiseLayerCutoff = 0.90;
    pads[20].noiseLayerColor = 0.90; pads[20].noiseLayerDecay = 0.25;
    pads[20].clickLayerMix = 0.35; pads[20].clickLayerBrightness = 0.80;
    pads[20].airLoading = 0.0; pads[20].modeScatter = 0.50;
    pads[20].couplingStrength = 0.35; pads[20].secondaryEnabled = 1.0;
    pads[20].secondarySize = 0.25; pads[20].secondaryMaterial = 0.80;
    pads[20].bodyDampingB1 = 0.32; pads[20].bodyDampingB3 = 0.0;  // FIXED
    pads[20].macroComplexity = 0.60;
    pads[20].pan = 0.38;

    // Vibraslap (21) -- NoiseBody rattle
    pads[21].exciterType = ExciterType::NoiseBurst;
    pads[21].bodyModel = BodyModelType::NoiseBody;
    pads[21].material = 0.85; pads[21].size = 0.22; pads[21].decay = 0.32;
    pads[21].level = 0.70;
    pads[21].noiseBurstDuration = 0.55;
    pads[21].noiseLayerMix = 0.85; pads[21].noiseLayerCutoff = 0.78;
    pads[21].noiseLayerResonance = 0.30;
    pads[21].noiseLayerColor = 0.62; pads[21].noiseLayerDecay = 0.32;
    pads[21].clickLayerMix = 0.30; pads[21].clickLayerContactMs = 0.30;
    pads[21].airLoading = 0.0; pads[21].modeScatter = 0.62;
    pads[21].bodyDampingB3 = 0.0; pads[21].bodyDampingB1 = 0.32;
    pads[21].macroComplexity = 0.78;
    pads[21].pan = 0.34;

    // Triangle (22) -- Bell -> Shell (free-free steel rod)
    pads[22].exciterType = ExciterType::Impulse;
    pads[22].bodyModel = BodyModelType::Shell;
    pads[22].material = 0.95; pads[22].size = 0.085; pads[22].decay = 0.85;
    pads[22].level = 0.70; pads[22].strikePosition = 0.18;
    pads[22].modeStretch = 0.62; pads[22].decaySkew = 0.40;
    pads[22].modeScatter = 0.12;
    pads[22].clickLayerMix = 0.95; pads[22].clickLayerContactMs = 0.06;
    pads[22].clickLayerBrightness = 0.95;
    pads[22].noiseLayerMix = 0.0;
    pads[22].airLoading = 0.0;
    pads[22].bodyDampingB3 = 0.02; pads[22].bodyDampingB1 = 0.30;
    pads[22].macroBrightness = 0.85;
    pads[22].pan = 0.66;

    // Conga Bass center (23) -- deep open conga + 1/k weight
    pads[23].exciterType = ExciterType::Impulse;
    pads[23].bodyModel = BodyModelType::Membrane;
    pads[23].material = 0.42; pads[23].size = 0.60; pads[23].decay = 0.42;
    pads[23].level = 0.82; pads[23].strikePosition = 0.50;
    pads[23].tsPitchEnvStart = toLogNorm(200);
    pads[23].tsPitchEnvEnd   = toLogNorm(150);
    pads[23].tsPitchEnvTime  = 0.05;
    pads[23].airLoading = 0.58; pads[23].modeScatter = 0.12;
    pads[23].modeInjectAmount = 0.12; pads[23].decaySkew = 0.60;
    pads[23].nonlinearCoupling = 0.18;
    pads[23].couplingStrength = 0.35; pads[23].secondaryEnabled = 1.0;
    pads[23].secondarySize = 0.50; pads[23].secondaryMaterial = 0.35;
    pads[23].tensionModAmt = 0.20;
    pads[23].clickLayerMix = 0.45; pads[23].clickLayerContactMs = 0.20;
    pads[23].clickLayerBrightness = 0.45;
    pads[23].noiseLayerMix = 0.12; pads[23].noiseLayerColor = 0.40;
    pads[23].bodyDampingB1 = 0.30; pads[23].bodyDampingB3 = 0.10;
    pads[23].macroBodySize = 0.80;
    pads[23].pan = 0.50;

    // Surdo (24) -- Membrane/Mallet bass skin
    pads[24].exciterType = ExciterType::Mallet;
    pads[24].bodyModel = BodyModelType::Membrane;
    pads[24].material = 0.28; pads[24].size = 0.82; pads[24].decay = 0.48;
    pads[24].level = 0.86; pads[24].strikePosition = 0.45;
    pads[24].tsPitchEnvStart = toLogNorm(150);
    pads[24].tsPitchEnvEnd   = toLogNorm(85);
    pads[24].tsPitchEnvTime  = 0.05;
    pads[24].airLoading = 0.78; pads[24].modeScatter = 0.18;
    pads[24].modeInjectAmount = 0.10; pads[24].decaySkew = 0.62;
    pads[24].nonlinearCoupling = 0.20;
    pads[24].couplingStrength = 0.45; pads[24].secondaryEnabled = 1.0;
    pads[24].secondarySize = 0.55; pads[24].secondaryMaterial = 0.30;
    pads[24].tensionModAmt = 0.25;
    pads[24].clickLayerMix = 0.35; pads[24].clickLayerContactMs = 0.24;
    pads[24].clickLayerBrightness = 0.38;
    pads[24].noiseLayerMix = 0.15; pads[24].noiseLayerColor = 0.40;
    pads[24].bodyDampingB1 = 0.28; pads[24].bodyDampingB3 = 0.12;
    pads[24].macroBodySize = 0.90;
    pads[24].pan = 0.50;

    // Quinto Slap (25) -- smallest/highest conga, choked
    pads[25].exciterType = ExciterType::Impulse;
    pads[25].bodyModel = BodyModelType::Membrane;
    pads[25].material = 0.55; pads[25].size = 0.44; pads[25].decay = 0.16;
    pads[25].level = 0.82; pads[25].strikePosition = 0.10;
    pads[25].airLoading = 0.42; pads[25].modeScatter = 0.30;
    pads[25].decaySkew = 0.40;
    pads[25].couplingStrength = 0.24; pads[25].secondaryEnabled = 1.0;
    pads[25].secondarySize = 0.38; pads[25].secondaryMaterial = 0.40;
    pads[25].tensionModAmt = 0.0;
    pads[25].clickLayerMix = 0.85; pads[25].clickLayerContactMs = 0.10;
    pads[25].clickLayerBrightness = 0.85;
    pads[25].noiseLayerMix = 0.12; pads[25].noiseLayerColor = 0.40;
    pads[25].bodyDampingB1 = 0.45; pads[25].bodyDampingB3 = 0.10;
    pads[25].macroPunch = 0.85;
    pads[25].pan = 0.48;

    // Cuica (26) -- Membrane/Friction FX squeak (kit deltas: skew 0.55,
    // no secondary box; material morph + high tensionMod + NLC).
    pads[26].exciterType = ExciterType::Friction;
    pads[26].bodyModel = BodyModelType::Membrane;
    pads[26].material = 0.45; pads[26].size = 0.50; pads[26].decay = 0.50;
    pads[26].level = 0.76; pads[26].strikePosition = 0.40;
    pads[26].frictionPressure = 0.50;
    pads[26].modeInjectAmount = 0.16; pads[26].nonlinearCoupling = 0.40;
    pads[26].decaySkew = 0.55;
    pads[26].tensionModAmt = 0.40;
    pads[26].airLoading = 0.55; pads[26].modeScatter = 0.20;
    pads[26].morphEnabled = 1.0; pads[26].morphStart = 0.50;
    pads[26].morphEnd = 0.80; pads[26].morphDuration = 0.40;
    pads[26].clickLayerMix = 0.0;
    pads[26].noiseLayerMix = 0.15; pads[26].noiseLayerColor = 0.40;
    pads[26].bodyDampingB1 = 0.34; pads[26].bodyDampingB3 = 0.10;
    pads[26].pan = 0.50;

    // Castanets (27) -- Shell/Impulse dry-wood click pair, choke group 2
    pads[27].exciterType = ExciterType::Impulse;
    pads[27].bodyModel = BodyModelType::Shell;
    pads[27].material = 0.80; pads[27].size = 0.06; pads[27].decay = 0.08;
    pads[27].level = 0.76; pads[27].strikePosition = 0.85;
    pads[27].chokeGroup = 2;
    pads[27].modeStretch = 0.50; pads[27].decaySkew = 0.42;
    pads[27].modeScatter = 0.10;
    pads[27].clickLayerMix = 0.92; pads[27].clickLayerContactMs = 0.06;
    pads[27].clickLayerBrightness = 0.90;
    pads[27].noiseLayerMix = 0.0;
    pads[27].airLoading = 0.0;
    pads[27].bodyDampingB3 = 0.70; pads[27].bodyDampingB1 = 0.40;
    pads[27].pan = 0.56;

    k.opts.maxPolyphony    = 16;
    k.opts.globalCoupling  = 0.20;
    k.opts.snareBuzz       = 0.10;
    k.opts.tomResonance    = 0.25;
    k.opts.couplingDelayMs = 0.8;
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
                 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
    return k;
}
Kit tablaKit() {
    Kit k{"Tabla", "Percussive", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Full N/S-Indian classical ensemble: 30 distinct pads (was 10, 5 clones).
    // Tabla bol set (0-8), Dholak/Mridangam/Ghatam/Kanjira/Daf, a pitched
    // tarang melodic row, Manjira/Tingsha (Bell), Chimta (NoiseBody),
    // Khartal (Shell), and a 2-string Tanpura drone (String/Friction, aux1).
    // ModeInject 0.30 = the 1/k syahi stand-in on the harmonic dayan/
    // mridangam/tarang heads. tensionMod/airLoading Membrane-only.
    // NOTE: only ~7 voices have explicit plan values; the rest are
    // reconstructed from the dayan/bayan archetypes (no structured table).

    // Bayan 'Dha/Ge' open bass (0)
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.30; pads[0].size = 0.72; pads[0].decay = 0.78;
    pads[0].level = 0.82; pads[0].strikePosition = 0.50;
    pads[0].tsPitchEnvStart = toLogNorm(180);
    pads[0].tsPitchEnvEnd   = toLogNorm(70);
    pads[0].tsPitchEnvTime  = 0.20;   // 100 ms
    pads[0].airLoading = 0.62; pads[0].modeScatter = 0.10;
    pads[0].couplingStrength = 0.32; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.45; pads[0].secondaryMaterial = 0.40;
    pads[0].tensionModAmt = 0.78;
    pads[0].clickLayerMix = 0.45; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.45;
    pads[0].noiseLayerMix = 0.10; pads[0].noiseLayerColor = 0.42;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.05;
    pads[0].decaySkew = 0.65;
    pads[0].macroComplexity = 0.70;
    pads[0].pan = 0.42;

    // Bayan 'Ka' damped bass (1)
    pads[1] = pads[0];
    pads[1].decay = 0.18; pads[1].strikePosition = 0.20;
    pads[1].tensionModAmt = 0.0;   // damped: no gliss
    pads[1].decaySkew = 0.45;
    pads[1].clickLayerMix = 0.85; pads[1].clickLayerBrightness = 0.65;
    pads[1].tsPitchEnvTime = 0.0;
    pads[1].bodyDampingB1 = 0.45;

    // Dayan 'Na' open tone (2) -- syahi 1/k via ModeInject 0.30
    pads[2].exciterType = ExciterType::Impulse;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.50; pads[2].size = 0.42; pads[2].decay = 0.58;
    pads[2].level = 0.80; pads[2].strikePosition = 0.40;
    pads[2].tsPitchEnvStart = toLogNorm(420);
    pads[2].tsPitchEnvEnd   = toLogNorm(380);
    pads[2].tsPitchEnvTime  = 0.04;
    pads[2].airLoading = 0.45; pads[2].modeScatter = 0.05;
    pads[2].modeInjectAmount = 0.30; pads[2].decaySkew = 0.65;
    pads[2].couplingStrength = 0.35; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.35; pads[2].secondaryMaterial = 0.45;
    pads[2].tensionModAmt = 0.25;
    pads[2].clickLayerMix = 0.55; pads[2].clickLayerContactMs = 0.12;
    pads[2].clickLayerBrightness = 0.78;
    pads[2].noiseLayerMix = 0.08;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.05;
    pads[2].nonlinearCoupling = 0.20;
    pads[2].pan = 0.58;

    // Dayan 'Tin' edge (3)
    pads[3] = pads[2];
    pads[3].decay = 0.22; pads[3].strikePosition = 0.10;
    pads[3].modeInjectAmount = 0.20; pads[3].decaySkew = 0.40;
    pads[3].clickLayerMix = 0.85; pads[3].clickLayerBrightness = 0.85;
    pads[3].tensionModAmt = 0.15;
    pads[3].pan = 0.58;

    // Dayan 'Tha' palm (4) -- Mallet
    pads[4].exciterType = ExciterType::Mallet;
    pads[4].bodyModel = BodyModelType::Membrane;
    pads[4].material = 0.50; pads[4].size = 0.42; pads[4].decay = 0.45;
    pads[4].level = 0.80; pads[4].strikePosition = 0.50;
    pads[4].airLoading = 0.42; pads[4].modeScatter = 0.10;
    pads[4].decaySkew = 0.50;
    pads[4].couplingStrength = 0.30; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.35; pads[4].secondaryMaterial = 0.45;
    pads[4].tensionModAmt = 0.30;
    pads[4].clickLayerMix = 0.40; pads[4].clickLayerContactMs = 0.30;
    pads[4].clickLayerBrightness = 0.45;
    pads[4].noiseLayerMix = 0.15; pads[4].noiseLayerColor = 0.42;
    pads[4].bodyDampingB1 = 0.30; pads[4].bodyDampingB3 = 0.05;
    pads[4].pan = 0.58;

    // Dayan 'Tete' damped tap (5)
    pads[5].exciterType = ExciterType::Impulse;
    pads[5].bodyModel = BodyModelType::Membrane;
    pads[5].material = 0.50; pads[5].size = 0.42; pads[5].decay = 0.10;
    pads[5].level = 0.76; pads[5].strikePosition = 0.20;
    pads[5].airLoading = 0.42; pads[5].modeScatter = 0.10;
    pads[5].decaySkew = 0.30;
    pads[5].couplingStrength = 0.30; pads[5].secondaryEnabled = 1.0;
    pads[5].secondarySize = 0.35; pads[5].secondaryMaterial = 0.45;
    pads[5].tensionModAmt = 0.0;
    pads[5].clickLayerMix = 0.80; pads[5].clickLayerContactMs = 0.12;
    pads[5].clickLayerBrightness = 0.78;
    pads[5].noiseLayerMix = 0.05; pads[5].noiseLayerColor = 0.42;
    pads[5].bodyDampingB1 = 0.45; pads[5].bodyDampingB3 = 0.05;
    pads[5].pan = 0.58;

    // Bayan 'Ge' extreme gliss (6)
    pads[6] = pads[0];
    pads[6].decay = 0.85; pads[6].tensionModAmt = 0.85;
    pads[6].decaySkew = 0.65;

    // Dayan 'Ti' damped edge (7)
    pads[7] = pads[3];
    pads[7].decay = 0.14; pads[7].decaySkew = 0.35;
    pads[7].modeInjectAmount = 0.0; pads[7].tensionModAmt = 0.0;
    pads[7].bodyDampingB1 = 0.45;

    // Dayan 'Na' bloom (8) -- material morph
    pads[8].exciterType = ExciterType::Impulse;
    pads[8].bodyModel = BodyModelType::Membrane;
    pads[8].material = 0.55; pads[8].size = 0.42; pads[8].decay = 0.45;
    pads[8].level = 0.78; pads[8].strikePosition = 0.18;
    pads[8].morphEnabled = 1.0;
    pads[8].morphStart = 0.45; pads[8].morphEnd = 0.65;
    pads[8].morphDuration = 0.40; pads[8].morphCurve = 0.4;
    pads[8].airLoading = 0.38; pads[8].modeScatter = 0.45;
    pads[8].modeInjectAmount = 0.30; pads[8].decaySkew = 0.65;
    pads[8].couplingStrength = 0.38; pads[8].secondaryEnabled = 1.0;
    pads[8].secondarySize = 0.32; pads[8].secondaryMaterial = 0.50;
    pads[8].tensionModAmt = 0.18;
    pads[8].clickLayerMix = 0.55; pads[8].clickLayerContactMs = 0.14;
    pads[8].clickLayerBrightness = 0.78;
    pads[8].noiseLayerMix = 0.05; pads[8].noiseLayerColor = 0.42;
    pads[8].nonlinearCoupling = 0.32;
    pads[8].bodyDampingB1 = 0.30; pads[8].bodyDampingB3 = 0.05;
    pads[8].pan = 0.55;

    // Dholak HI (9) -- plain membrane, no ModeInject
    pads[9].exciterType = ExciterType::Impulse;
    pads[9].bodyModel = BodyModelType::Membrane;
    pads[9].material = 0.48; pads[9].size = 0.46; pads[9].decay = 0.34;
    pads[9].level = 0.80; pads[9].strikePosition = 0.35;
    pads[9].tsPitchEnvStart = toLogNorm(320);
    pads[9].tsPitchEnvEnd   = toLogNorm(220);
    pads[9].tsPitchEnvTime  = 0.05;   // 25 ms
    pads[9].airLoading = 0.50; pads[9].modeScatter = 0.10;
    pads[9].decaySkew = 0.50;
    pads[9].couplingStrength = 0.30; pads[9].secondaryEnabled = 1.0;
    pads[9].secondarySize = 0.42; pads[9].secondaryMaterial = 0.40;
    pads[9].tensionModAmt = 0.16;
    pads[9].clickLayerMix = 0.50; pads[9].clickLayerContactMs = 0.16;
    pads[9].clickLayerBrightness = 0.60;
    pads[9].noiseLayerMix = 0.10; pads[9].noiseLayerColor = 0.42;
    pads[9].bodyDampingB1 = 0.32; pads[9].bodyDampingB3 = 0.08;
    pads[9].pan = 0.58;

    // Dholak LO (10)
    pads[10] = pads[9];
    pads[10].material = 0.36; pads[10].size = 0.62; pads[10].decay = 0.42;
    pads[10].tsPitchEnvStart = toLogNorm(220);
    pads[10].tsPitchEnvEnd   = toLogNorm(140);
    pads[10].tsPitchEnvTime  = 0.16;   // 80 ms
    pads[10].airLoading = 0.58; pads[10].tensionModAmt = 0.34;
    pads[10].decaySkew = 0.58; pads[10].secondarySize = 0.50;
    pads[10].bodyDampingB3 = 0.12;
    pads[10].pan = 0.42;

    // Mridangam BASS / thoppi (11) -- modeInject syahi
    pads[11].exciterType = ExciterType::Impulse;
    pads[11].bodyModel = BodyModelType::Membrane;
    pads[11].material = 0.32; pads[11].size = 0.66; pads[11].decay = 0.50;
    pads[11].level = 0.82; pads[11].strikePosition = 0.45;
    pads[11].tsPitchEnvStart = toLogNorm(180);
    pads[11].tsPitchEnvEnd   = toLogNorm(110);
    pads[11].tsPitchEnvTime  = 0.06;
    pads[11].airLoading = 0.58; pads[11].modeScatter = 0.10;
    pads[11].modeInjectAmount = 0.18; pads[11].decaySkew = 0.60;
    pads[11].nonlinearCoupling = 0.16;
    pads[11].couplingStrength = 0.38; pads[11].secondaryEnabled = 1.0;
    pads[11].secondarySize = 0.50; pads[11].secondaryMaterial = 0.40;
    pads[11].tensionModAmt = 0.20;
    pads[11].clickLayerMix = 0.45; pads[11].clickLayerContactMs = 0.20;
    pads[11].clickLayerBrightness = 0.45;
    pads[11].noiseLayerMix = 0.10; pads[11].noiseLayerColor = 0.42;
    pads[11].bodyDampingB1 = 0.30; pads[11].bodyDampingB3 = 0.08;
    pads[11].macroBodySize = 0.80;
    pads[11].pan = 0.40;

    // Mridangam TREBLE / valanthalai (12)
    pads[12].exciterType = ExciterType::Impulse;
    pads[12].bodyModel = BodyModelType::Membrane;
    pads[12].material = 0.52; pads[12].size = 0.40; pads[12].decay = 0.40;
    pads[12].level = 0.80; pads[12].strikePosition = 0.30;
    pads[12].tsPitchEnvStart = toLogNorm(360);
    pads[12].tsPitchEnvEnd   = toLogNorm(300);
    pads[12].tsPitchEnvTime  = 0.04;
    pads[12].airLoading = 0.45; pads[12].modeScatter = 0.06;
    pads[12].modeInjectAmount = 0.30; pads[12].decaySkew = 0.60;
    pads[12].couplingStrength = 0.32; pads[12].secondaryEnabled = 1.0;
    pads[12].secondarySize = 0.35; pads[12].secondaryMaterial = 0.45;
    pads[12].tensionModAmt = 0.20;
    pads[12].clickLayerMix = 0.55; pads[12].clickLayerContactMs = 0.12;
    pads[12].clickLayerBrightness = 0.78;
    pads[12].noiseLayerMix = 0.08; pads[12].noiseLayerColor = 0.45;
    pads[12].bodyDampingB1 = 0.30; pads[12].bodyDampingB3 = 0.05;
    pads[12].pan = 0.60;

    // Ghatam clay pot (13) -- deep cavity membrane
    pads[13].exciterType = ExciterType::Impulse;
    pads[13].bodyModel = BodyModelType::Membrane;
    pads[13].material = 0.34; pads[13].size = 0.70; pads[13].decay = 0.38;
    pads[13].level = 0.80; pads[13].strikePosition = 0.50;
    pads[13].tsPitchEnvStart = toLogNorm(160);
    pads[13].tsPitchEnvEnd   = toLogNorm(110);
    pads[13].tsPitchEnvTime  = 0.05;
    pads[13].airLoading = 0.70; pads[13].modeScatter = 0.12;
    pads[13].modeInjectAmount = 0.12; pads[13].decaySkew = 0.55;
    pads[13].couplingStrength = 0.40; pads[13].secondaryEnabled = 1.0;
    pads[13].secondarySize = 0.55; pads[13].secondaryMaterial = 0.35;
    pads[13].tensionModAmt = 0.30;
    pads[13].clickLayerMix = 0.55; pads[13].clickLayerContactMs = 0.16;
    pads[13].clickLayerBrightness = 0.55;
    pads[13].noiseLayerMix = 0.10; pads[13].noiseLayerColor = 0.42;
    pads[13].bodyDampingB1 = 0.32; pads[13].bodyDampingB3 = 0.10;
    pads[13].pan = 0.50;

    // Kanjira frame drum (14)
    pads[14].exciterType = ExciterType::Impulse;
    pads[14].bodyModel = BodyModelType::Membrane;
    pads[14].material = 0.45; pads[14].size = 0.40; pads[14].decay = 0.30;
    pads[14].level = 0.78; pads[14].strikePosition = 0.35;
    pads[14].tsPitchEnvStart = toLogNorm(300);
    pads[14].tsPitchEnvEnd   = toLogNorm(220);
    pads[14].tsPitchEnvTime  = 0.04;
    pads[14].airLoading = 0.40; pads[14].modeScatter = 0.30;  // monkey-skin pitch bend
    pads[14].decaySkew = 0.50;
    pads[14].couplingStrength = 0.28; pads[14].secondaryEnabled = 1.0;
    pads[14].secondarySize = 0.36; pads[14].secondaryMaterial = 0.50;
    pads[14].tensionModAmt = 0.40;   // pronounced kanjira bend
    pads[14].clickLayerMix = 0.55; pads[14].clickLayerContactMs = 0.15;
    pads[14].clickLayerBrightness = 0.68;
    pads[14].noiseLayerMix = 0.12; pads[14].noiseLayerColor = 0.45;
    pads[14].bodyDampingB1 = 0.32; pads[14].bodyDampingB3 = 0.08;
    pads[14].pan = 0.62;

    // Daf / Duff frame drum (15) -- Mallet
    pads[15].exciterType = ExciterType::Mallet;
    pads[15].bodyModel = BodyModelType::Membrane;
    pads[15].material = 0.40; pads[15].size = 0.78; pads[15].decay = 0.42;
    pads[15].level = 0.78; pads[15].strikePosition = 0.45;
    pads[15].airLoading = 0.62; pads[15].modeScatter = 0.18;
    pads[15].decaySkew = 0.52;
    pads[15].couplingStrength = 0.25; pads[15].secondaryEnabled = 1.0;
    pads[15].secondarySize = 0.45; pads[15].secondaryMaterial = 0.40;
    pads[15].tensionModAmt = 0.20;
    pads[15].clickLayerMix = 0.35; pads[15].clickLayerContactMs = 0.28;
    pads[15].clickLayerBrightness = 0.40;
    pads[15].noiseLayerMix = 0.15; pads[15].noiseLayerColor = 0.42;
    pads[15].bodyDampingB1 = 0.30; pads[15].bodyDampingB3 = 0.10;
    pads[15].pan = 0.45;

    // Dayan-tarang melodic row (16/17/18) -- pitched harmonic heads
    const int    tarangPads[]  = {16, 17, 18};
    const double tarangSize[]  = {0.48, 0.42, 0.36};
    const double tarangHi[]    = {300, 360, 430};
    const double tarangPan[]   = {0.48, 0.52, 0.56};
    for (int i = 0; i < 3; ++i) {
        const int p = tarangPads[i];
        pads[p].exciterType = ExciterType::Impulse;
        pads[p].bodyModel = BodyModelType::Membrane;
        pads[p].material = 0.52; pads[p].size = tarangSize[i];
        pads[p].decay = 0.55; pads[p].level = 0.78;
        pads[p].strikePosition = 0.40;
        pads[p].tsPitchEnvStart = toLogNorm(tarangHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tarangHi[i] * 0.94);
        pads[p].tsPitchEnvTime  = 0.03;
        pads[p].airLoading = 0.45; pads[p].modeScatter = 0.05;
        pads[p].modeInjectAmount = 0.30; pads[p].decaySkew = 0.58;
        pads[p].couplingStrength = 0.32; pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize = 0.35; pads[p].secondaryMaterial = 0.45;
        pads[p].tensionModAmt = 0.12;
        pads[p].clickLayerMix = 0.45; pads[p].clickLayerContactMs = 0.14;
        pads[p].clickLayerBrightness = 0.72;
        pads[p].noiseLayerMix = 0.06; pads[p].noiseLayerColor = 0.45;
        pads[p].bodyDampingB1 = 0.28; pads[p].bodyDampingB3 = 0.05;
        pads[p].pan = tarangPan[i];
    }

    // Manjira hand cymbals (19) -- Bell
    pads[19].exciterType = ExciterType::Impulse;
    pads[19].bodyModel = BodyModelType::Bell;
    pads[19].material = 0.90; pads[19].size = 0.12; pads[19].decay = 0.70;
    pads[19].level = 0.72;
    pads[19].modeStretch = 0.55; pads[19].decaySkew = 0.40;
    pads[19].modeScatter = 0.18;
    pads[19].clickLayerMix = 0.55; pads[19].clickLayerContactMs = 0.06;
    pads[19].clickLayerBrightness = 0.90;
    pads[19].noiseLayerMix = 0.0;
    pads[19].airLoading = 0.0;
    pads[19].bodyDampingB3 = 0.0; pads[19].bodyDampingB1 = 0.30;
    pads[19].macroBrightness = 0.85;
    pads[19].pan = 0.30;

    // Tingsha / large Manjira (20) -- Bell, more inharmonic
    pads[20] = pads[19];
    pads[20].size = 0.18; pads[20].decay = 0.82;
    pads[20].modeStretch = 0.60; pads[20].decaySkew = 0.38;
    pads[20].pan = 0.70;

    // Chimta metallic shaker (21) -- NoiseBody + metallic Shell secondary
    pads[21].exciterType = ExciterType::NoiseBurst;
    pads[21].bodyModel = BodyModelType::NoiseBody;
    pads[21].material = 0.85; pads[21].size = 0.16; pads[21].decay = 0.20;
    pads[21].level = 0.68;
    pads[21].noiseBurstDuration = 0.30;
    pads[21].noiseLayerMix = 0.45; pads[21].noiseLayerCutoff = 0.92;
    pads[21].noiseLayerColor = 0.90; pads[21].noiseLayerDecay = 0.18;  // violet
    pads[21].clickLayerMix = 0.20;
    pads[21].airLoading = 0.0; pads[21].modeScatter = 0.30;
    pads[21].couplingStrength = 0.40; pads[21].secondaryEnabled = 1.0;
    pads[21].secondarySize = 0.20; pads[21].secondaryMaterial = 0.85;
    pads[21].bodyDampingB3 = 0.0; pads[21].bodyDampingB1 = 0.45;
    pads[21].pan = 0.65;

    // Khartal wood clapper (22) -- Shell free-free bar
    pads[22].exciterType = ExciterType::Impulse;
    pads[22].bodyModel = BodyModelType::Shell;
    pads[22].material = 0.80; pads[22].size = 0.12; pads[22].decay = 0.14;
    pads[22].level = 0.78; pads[22].strikePosition = 0.85;
    pads[22].modeStretch = 0.50; pads[22].decaySkew = 0.42;
    pads[22].modeScatter = 0.10;
    pads[22].clickLayerMix = 0.90; pads[22].clickLayerContactMs = 0.06;
    pads[22].clickLayerBrightness = 0.88;
    pads[22].noiseLayerMix = 0.0;
    pads[22].airLoading = 0.0;
    pads[22].bodyDampingB3 = 0.70; pads[22].bodyDampingB1 = 0.40;  // dry wood
    pads[22].pan = 0.38;

    // Tabla 'Dhin' composite (23) -- open dayan with ring
    pads[23] = pads[2];
    pads[23].decay = 0.62; pads[23].strikePosition = 0.42;
    pads[23].tensionModAmt = 0.20; pads[23].decaySkew = 0.62;
    pads[23].pan = 0.50;

    // Tanpura drone Sa (24) -- String/Friction, aux bus 1
    pads[24].exciterType = ExciterType::Friction;
    pads[24].bodyModel = BodyModelType::String;
    pads[24].material = 0.55; pads[24].size = 0.65; pads[24].decay = 0.95;
    pads[24].level = 0.55;
    pads[24].frictionPressure = 0.45;
    pads[24].modeStretch = 0.40;
    pads[24].nonlinearCoupling = 0.50;
    pads[24].tsDriveAmount = 0.20; pads[24].tsFoldAmount = 0.22;  // jivari buzz
    pads[24].morphEnabled = 1.0;
    pads[24].morphStart = 0.45; pads[24].morphEnd = 0.70;
    pads[24].morphDuration = 0.85; pads[24].morphCurve = 0.5;
    pads[24].decaySkew = 0.85;
    pads[24].noiseLayerMix = 0.18; pads[24].noiseLayerCutoff = 0.45;
    pads[24].clickLayerMix = 0.0;
    pads[24].bodyDampingB1 = 0.30; pads[24].bodyDampingB3 = 0.18;
    pads[24].outputBus = 1;
    pads[24].macroComplexity = 0.85;
    pads[24].pan = 0.50;

    // Tanpura drone Pa / 5th (25)
    pads[25] = pads[24];
    pads[25].size = 0.58;

    // Bayan-tarang low melodic (26)
    pads[26].exciterType = ExciterType::Impulse;
    pads[26].bodyModel = BodyModelType::Membrane;
    pads[26].material = 0.32; pads[26].size = 0.58; pads[26].decay = 0.60;
    pads[26].level = 0.80; pads[26].strikePosition = 0.45;
    pads[26].tsPitchEnvStart = toLogNorm(180);
    pads[26].tsPitchEnvEnd   = toLogNorm(165);
    pads[26].tsPitchEnvTime  = 0.04;
    pads[26].airLoading = 0.58; pads[26].modeScatter = 0.06;
    pads[26].modeInjectAmount = 0.30; pads[26].decaySkew = 0.60;
    pads[26].couplingStrength = 0.35; pads[26].secondaryEnabled = 1.0;
    pads[26].secondarySize = 0.48; pads[26].secondaryMaterial = 0.40;
    pads[26].tensionModAmt = 0.12;
    pads[26].clickLayerMix = 0.45; pads[26].clickLayerContactMs = 0.16;
    pads[26].clickLayerBrightness = 0.55;
    pads[26].noiseLayerMix = 0.08; pads[26].noiseLayerColor = 0.42;
    pads[26].bodyDampingB1 = 0.28; pads[26].bodyDampingB3 = 0.05;
    pads[26].pan = 0.40;

    // Dayan-tarang very-hi (27)
    pads[27] = pads[16];
    pads[27].size = 0.30;
    pads[27].tsPitchEnvStart = toLogNorm(500);
    pads[27].tsPitchEnvEnd   = toLogNorm(470);
    pads[27].pan = 0.58;

    // Mridangam 'chapu' slap (28)
    pads[28] = pads[12];
    pads[28].decay = 0.18; pads[28].strikePosition = 0.10;
    pads[28].modeInjectAmount = 0.0; pads[28].tensionModAmt = 0.0;
    pads[28].decaySkew = 0.38;
    pads[28].clickLayerMix = 0.85; pads[28].clickLayerBrightness = 0.85;
    pads[28].bodyDampingB1 = 0.45;
    pads[28].pan = 0.62;

    // Khol / Mridangam bass roll (29) -- Mallet
    pads[29] = pads[11];
    pads[29].exciterType = ExciterType::Mallet;
    pads[29].decay = 0.58; pads[29].clickLayerMix = 0.32;
    pads[29].clickLayerContactMs = 0.28; pads[29].clickLayerBrightness = 0.38;
    pads[29].pan = 0.40;

    // Dayan-tarang spare (30) + Bayan-tarang spare (31) -- optional
    pads[30] = pads[17];
    pads[30].pan = 0.50;
    pads[31] = pads[26];
    pads[31].size = 0.62;
    pads[31].pan = 0.44;

    k.opts.maxPolyphony    = 12;
    k.opts.globalCoupling  = 0.30;
    k.opts.tomResonance    = 0.45;
    k.opts.couplingDelayMs = 1.1;
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    return k;
}
Kit worldMetalKit() {
    Kit k{"World Metal", "Percussive", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Tuned-metal idiophone kit. Kalimba scale-graded (no x8 sameness),
    // mbira on String, full FM-bell family added (FMImpulse live post L-3).
    // modeInject 0 + airLoading 0 kit-wide (physically correct on
    // Bell/Plate/Shell/String); tensionMod inert (no Membrane voice).
    // 12 single voices have explicit plan values; the rest follow the
    // cited archetypes within the plan's documented semantics.

    // Kalimba C4-E5 (0-7) + F5/A5 octave extensions (30/31) -- one
    // instrument across a scale: pitch grade + center-out pan + decaySkew.
    const int    kalimbaPads[] = {0, 1, 2, 3, 4, 5, 6, 7, 30, 31};
    const double kalimbaMat[]  = {0.40, 0.46, 0.52, 0.58, 0.63, 0.69, 0.74, 0.80, 0.83, 0.86};
    const double kalimbaSize[] = {0.485, 0.435, 0.385, 0.310, 0.260, 0.185, 0.135, 0.085, 0.05, 0.02};
    const double kalimbaDec[]  = {0.60, 0.56, 0.52, 0.47, 0.43, 0.38, 0.34, 0.30, 0.28, 0.26};
    const double kalimbaPan[]  = {0.50, 0.38, 0.62, 0.30, 0.70, 0.25, 0.75, 0.50, 0.35, 0.65};
    for (int i = 0; i < 10; ++i) {
        const int p = kalimbaPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel = BodyModelType::Bell;
        pads[p].material = kalimbaMat[i]; pads[p].size = kalimbaSize[i];
        pads[p].decay = kalimbaDec[i]; pads[p].level = 0.72;
        pads[p].strikePosition = 0.15;
        pads[p].modeStretch = 0.40; pads[p].decaySkew = 0.62;
        pads[p].modeScatter = 0.08;
        pads[p].clickLayerMix = 0.42; pads[p].clickLayerContactMs = 0.25;
        pads[p].clickLayerBrightness = 0.78;
        pads[p].noiseLayerMix = 0.10; pads[p].noiseLayerCutoff = 0.60;
        pads[p].noiseLayerResonance = 0.30; pads[p].noiseLayerDecay = 0.20;
        pads[p].noiseLayerColor = 0.65;
        pads[p].airLoading = 0.0;
        pads[p].bodyDampingB1 = 0.18; pads[p].bodyDampingB3 = 0.10;
        pads[p].pan = kalimbaPan[i];
    }

    // Mbira tines (8-11) -- String/Mallet; modal axes are String no-ops
    // left neutral. material rises with pitch (darkens top sustain).
    const int    mbiraPads[] = {8, 9, 10, 11};
    const double mbiraMat[]  = {0.50, 0.62, 0.72, 0.82};
    const double mbiraSize[] = {0.32, 0.28, 0.24, 0.20};
    const double mbiraPan[]  = {0.40, 0.47, 0.53, 0.60};
    for (int i = 0; i < 4; ++i) {
        const int p = mbiraPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel = BodyModelType::String;
        pads[p].material = mbiraMat[i]; pads[p].size = mbiraSize[i];
        pads[p].decay = 0.78; pads[p].level = 0.70;
        pads[p].strikePosition = 0.85;   // pick near tip
        pads[p].nonlinearCoupling = 0.18;
        pads[p].clickLayerMix = 0.32; pads[p].clickLayerContactMs = 0.14;
        pads[p].clickLayerBrightness = 0.70;
        pads[p].noiseLayerMix = 0.12; pads[p].noiseLayerCutoff = 0.72;
        pads[p].noiseLayerResonance = 0.15; pads[p].noiseLayerDecay = 0.35;
        pads[p].noiseLayerColor = 0.78;
        pads[p].airLoading = 0.0;
        pads[p].pan = mbiraPan[i];
    }

    // Bell Tree (12) -- Bell/NoiseBurst inharmonic shimmer + morph dim
    pads[12].exciterType = ExciterType::NoiseBurst;
    pads[12].bodyModel = BodyModelType::Bell;
    pads[12].material = 0.95; pads[12].size = 0.30; pads[12].decay = 0.85;
    pads[12].level = 0.70;
    pads[12].modeStretch = 0.55; pads[12].modeScatter = 0.55;
    pads[12].decaySkew = 0.78;
    pads[12].morphEnabled = 1.0;
    pads[12].morphStart = 0.85; pads[12].morphEnd = 0.55;
    pads[12].morphDuration = 0.55; pads[12].morphCurve = 0.0;  // linear
    pads[12].clickLayerMix = 0.55; pads[12].clickLayerBrightness = 0.92;
    pads[12].noiseLayerMix = 0.42; pads[12].noiseLayerCutoff = 0.92;
    pads[12].noiseLayerColor = 0.90; pads[12].noiseLayerDecay = 0.85;  // violet
    pads[12].airLoading = 0.0;
    pads[12].bodyDampingB3 = 0.0; pads[12].bodyDampingB1 = 0.30;
    pads[12].pan = 0.50;

    // Crotales hi (13) -- Bell, harmonic octave (modeStretch physical)
    pads[13].exciterType = ExciterType::Mallet;
    pads[13].bodyModel = BodyModelType::Bell;
    pads[13].material = 0.92; pads[13].size = 0.12; pads[13].decay = 0.85;
    pads[13].level = 0.72;
    pads[13].decaySkew = 0.58;
    pads[13].clickLayerMix = 0.42; pads[13].clickLayerBrightness = 0.85;
    pads[13].noiseLayerMix = 0.0;
    pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].pan = 0.62;

    // Crotales lo (14)
    pads[14] = pads[13];
    pads[14].size = 0.22;
    pads[14].pan = 0.38;

    // Singing Bowl (15) -- Bell/Friction, long bowed ring, aux bus 1
    pads[15].exciterType = ExciterType::Friction;
    pads[15].bodyModel = BodyModelType::Bell;
    pads[15].material = 0.78; pads[15].size = 0.45; pads[15].decay = 0.95;
    pads[15].level = 0.65;
    pads[15].frictionPressure = 0.28;
    pads[15].modeStretch = 0.45; pads[15].decaySkew = 0.85;
    pads[15].modeScatter = 0.06;
    pads[15].nonlinearCoupling = 0.22;
    pads[15].morphEnabled = 1.0;
    pads[15].morphStart = 0.78; pads[15].morphEnd = 0.55;
    pads[15].morphDuration = 0.85; pads[15].morphCurve = 0.15;  // exp, ~1.7 s
    pads[15].noiseLayerMix = 0.10; pads[15].noiseLayerColor = 0.40;
    pads[15].clickLayerMix = 0.0;
    pads[15].airLoading = 0.0;
    pads[15].bodyDampingB3 = 0.0; pads[15].bodyDampingB1 = 0.30;
    pads[15].outputBus = 1;
    pads[15].macroComplexity = 0.85;
    pads[15].pan = 0.50;

    // Wood block hi (16) -- Plate free-plate Chladni
    pads[16].exciterType = ExciterType::Impulse;
    pads[16].bodyModel = BodyModelType::Plate;
    pads[16].material = 0.30; pads[16].size = 0.18; pads[16].decay = 0.16;
    pads[16].level = 0.72;
    pads[16].modeStretch = 0.50; pads[16].decaySkew = 0.50;
    pads[16].modeScatter = 0.20;
    pads[16].clickLayerMix = 0.78; pads[16].clickLayerContactMs = 0.10;
    pads[16].clickLayerBrightness = 0.78;
    pads[16].noiseLayerMix = 0.0;
    pads[16].airLoading = 0.0;
    pads[16].bodyDampingB1 = 0.50; pads[16].bodyDampingB3 = 0.10;
    pads[16].pan = 0.42;

    // Wood block lo (17)
    pads[17] = pads[16];
    pads[17].size = 0.28; pads[17].material = 0.28; pads[17].decay = 0.20;
    pads[17].pan = 0.58;

    // Tibetan Tingsha (18) -- Bell/Impulse, tens-of-sec ring
    pads[18].exciterType = ExciterType::Impulse;
    pads[18].bodyModel = BodyModelType::Bell;
    pads[18].material = 0.95; pads[18].size = 0.12; pads[18].decay = 0.92;
    pads[18].level = 0.65;
    pads[18].modeStretch = 0.62; pads[18].decaySkew = 0.38;
    pads[18].nonlinearCoupling = 0.22; pads[18].modeScatter = 0.12;
    pads[18].clickLayerMix = 0.40; pads[18].clickLayerBrightness = 0.85;
    pads[18].noiseLayerMix = 0.0;
    pads[18].airLoading = 0.0;
    pads[18].bodyDampingB3 = 0.04; pads[18].bodyDampingB1 = 0.08;
    pads[18].pan = 0.50;

    // Indian Temple Bell (19) -- Bell/Mallet, warm hum
    pads[19].exciterType = ExciterType::Mallet;
    pads[19].bodyModel = BodyModelType::Bell;
    pads[19].material = 0.82; pads[19].size = 0.50; pads[19].decay = 0.92;
    pads[19].level = 0.70;
    pads[19].modeStretch = 0.40; pads[19].decaySkew = 0.78;
    pads[19].modeScatter = 0.06;
    pads[19].clickLayerMix = 0.28; pads[19].clickLayerBrightness = 0.42;
    pads[19].noiseLayerMix = 0.04;
    pads[19].airLoading = 0.0;
    pads[19].bodyDampingB3 = 0.06; pads[19].bodyDampingB1 = 0.18;
    pads[19].pan = 0.50;

    // Gong / Tam-Tam (20, NEW) -- Bell/Mallet, bloom + head-shell secondary
    pads[20].exciterType = ExciterType::Mallet;
    pads[20].bodyModel = BodyModelType::Bell;
    pads[20].material = 0.85; pads[20].size = 0.85; pads[20].decay = 0.95;
    pads[20].level = 0.78; pads[20].strikePosition = 0.60;
    pads[20].modeStretch = 0.62; pads[20].modeScatter = 0.55;
    pads[20].decaySkew = 0.65; pads[20].nonlinearCoupling = 0.80;
    pads[20].morphEnabled = 1.0;
    pads[20].morphStart = 0.85; pads[20].morphEnd = 0.55;
    pads[20].morphDuration = 0.85; pads[20].morphCurve = 0.5;
    pads[20].couplingStrength = 0.85; pads[20].secondaryEnabled = 1.0;
    pads[20].secondarySize = 0.40; pads[20].secondaryMaterial = 0.70;
    pads[20].clickLayerMix = 0.30; pads[20].clickLayerContactMs = 0.22;
    pads[20].clickLayerBrightness = 0.40;
    pads[20].noiseLayerMix = 0.30; pads[20].noiseLayerColor = 0.90;  // violet
    pads[20].noiseLayerDecay = 0.90;
    pads[20].airLoading = 0.0;
    pads[20].bodyDampingB3 = 0.0; pads[20].bodyDampingB1 = 0.30;
    pads[20].outputBus = 1;
    pads[20].macroComplexity = 0.85;
    pads[20].pan = 0.50;

    // Triangle (21, NEW) -- Shell free-free bar (only Shell body)
    pads[21].exciterType = ExciterType::Impulse;
    pads[21].bodyModel = BodyModelType::Shell;
    pads[21].material = 0.95; pads[21].size = 0.085; pads[21].decay = 0.85;
    pads[21].level = 0.70; pads[21].strikePosition = 0.18;
    pads[21].modeStretch = 0.62; pads[21].decaySkew = 0.40;
    pads[21].modeScatter = 0.12;
    pads[21].clickLayerMix = 0.95; pads[21].clickLayerContactMs = 0.06;
    pads[21].clickLayerBrightness = 0.97;
    pads[21].noiseLayerMix = 0.0;
    pads[21].airLoading = 0.0;
    pads[21].bodyDampingB3 = 0.02; pads[21].bodyDampingB1 = 0.30;
    pads[21].pan = 0.60;

    // Tubular Bell (22, NEW) -- String/Mallet long tube
    pads[22].exciterType = ExciterType::Mallet;
    pads[22].bodyModel = BodyModelType::String;
    pads[22].material = 0.85; pads[22].size = 0.55; pads[22].decay = 0.92;
    pads[22].level = 0.72; pads[22].strikePosition = 0.30;
    pads[22].nonlinearCoupling = 0.12;
    pads[22].clickLayerMix = 0.40; pads[22].clickLayerContactMs = 0.20;
    pads[22].clickLayerBrightness = 0.65;
    pads[22].noiseLayerMix = 0.10;
    pads[22].airLoading = 0.0;
    pads[22].pan = 0.50;

    // Agogo hi (23, NEW) -- Bell/FMImpulse
    pads[23].exciterType = ExciterType::FMImpulse;
    pads[23].bodyModel = BodyModelType::Bell;
    pads[23].material = 0.85; pads[23].size = 0.14; pads[23].decay = 0.28;
    pads[23].level = 0.74;
    pads[23].fmRatio = 0.72;   // -> 3.16
    pads[23].modeStretch = 0.45; pads[23].decaySkew = 0.40;
    pads[23].clickLayerMix = 0.55; pads[23].clickLayerBrightness = 0.85;
    pads[23].noiseLayerMix = 0.0;
    pads[23].airLoading = 0.0;
    pads[23].bodyDampingB3 = 0.0; pads[23].bodyDampingB1 = 0.30;
    pads[23].macroBrightness = 0.65;
    pads[23].pan = 0.42;

    // Agogo lo (24, NEW)
    pads[24] = pads[23];
    pads[24].size = 0.22; pads[24].decay = 0.35; pads[24].fmRatio = 0.55;  // -> 2.65
    pads[24].pan = 0.58;

    // Cowbell (25, NEW) -- Bell/FMImpulse, detuned-fifth clang
    pads[25].exciterType = ExciterType::FMImpulse;
    pads[25].bodyModel = BodyModelType::Bell;
    pads[25].material = 0.78; pads[25].size = 0.22; pads[25].decay = 0.30;
    pads[25].level = 0.76;
    pads[25].fmRatio = 0.50;   // -> 2.5
    pads[25].modeStretch = 0.55; pads[25].modeScatter = 0.20;
    pads[25].clickLayerMix = 0.55; pads[25].clickLayerBrightness = 0.72;
    pads[25].noiseLayerMix = 0.10; pads[25].noiseLayerColor = 0.40;  // pink halo
    pads[25].airLoading = 0.0;
    pads[25].bodyDampingB3 = 0.0; pads[25].bodyDampingB1 = 0.32;
    pads[25].macroBrightness = 0.65;
    pads[25].pan = 0.50;

    // FM-Bell Perc (26, NEW) -- Bell/FMImpulse ping
    pads[26].exciterType = ExciterType::FMImpulse;
    pads[26].bodyModel = BodyModelType::Bell;
    pads[26].material = 0.80; pads[26].size = 0.25; pads[26].decay = 0.20;
    pads[26].level = 0.74;
    pads[26].fmRatio = 0.40;   // -> 2.2
    pads[26].clickLayerMix = 0.15;
    pads[26].noiseLayerMix = 0.0;
    pads[26].airLoading = 0.0;
    pads[26].bodyDampingB3 = 0.0; pads[26].bodyDampingB1 = 0.30;
    pads[26].pan = 0.50;

    // Sub-Bell Perc (27, NEW) -- Bell/FMImpulse, the only Drive user
    pads[27].exciterType = ExciterType::FMImpulse;
    pads[27].bodyModel = BodyModelType::Bell;
    pads[27].material = 0.70; pads[27].size = 0.35; pads[27].decay = 0.40;
    pads[27].level = 0.80;   // kit's loudest pad (recipe default)
    pads[27].fmRatio = 0.72;   // -> 3.16 grit
    pads[27].modeStretch = 0.45; pads[27].decaySkew = 0.42;
    pads[27].nonlinearCoupling = 0.40; pads[27].tsDriveAmount = 0.30;
    pads[27].clickLayerMix = 0.50; pads[27].clickLayerBrightness = 0.80;
    pads[27].noiseLayerMix = 0.12; pads[27].noiseLayerColor = 0.90;  // violet
    pads[27].airLoading = 0.0;
    pads[27].bodyDampingB3 = 0.0; pads[27].bodyDampingB1 = 0.30;
    pads[27].pan = 0.50;

    // Ride Bell ping (28, NEW) -- Bell/NoiseBurst, aux bus 1
    pads[28].exciterType = ExciterType::NoiseBurst;
    pads[28].bodyModel = BodyModelType::Bell;
    pads[28].material = 0.90; pads[28].size = 0.18; pads[28].decay = 0.70;
    pads[28].level = 0.72;
    pads[28].noiseBurstDuration = 0.20;
    pads[28].modeStretch = 0.45; pads[28].decaySkew = 0.55;
    pads[28].modeScatter = 0.15;
    pads[28].noiseLayerMix = 0.30; pads[28].noiseLayerCutoff = 0.90;
    pads[28].noiseLayerColor = 0.90; pads[28].noiseLayerDecay = 0.55;  // violet
    pads[28].clickLayerMix = 0.45; pads[28].clickLayerBrightness = 0.88;
    pads[28].airLoading = 0.0;
    pads[28].bodyDampingB3 = 0.0; pads[28].bodyDampingB1 = 0.25;
    pads[28].outputBus = 1;
    pads[28].pan = 0.40;

    // Glass Bell (29, NEW) -- Bell/FMImpulse, pure clean counterpart
    pads[29].exciterType = ExciterType::FMImpulse;
    pads[29].bodyModel = BodyModelType::Bell;
    pads[29].material = 0.85; pads[29].size = 0.20; pads[29].decay = 0.85;
    pads[29].level = 0.72;
    pads[29].fmRatio = 0.30;   // -> 1.9 pure
    pads[29].modeStretch = 0.40; pads[29].decaySkew = 0.45;
    pads[29].clickLayerMix = 0.30;
    pads[29].noiseLayerMix = 0.0;
    pads[29].airLoading = 0.0;
    pads[29].bodyDampingB3 = 0.0; pads[29].bodyDampingB1 = 0.20;
    pads[29].pan = 0.60;

    k.opts.maxPolyphony    = 20;
    k.opts.globalCoupling  = 0.40;
    k.opts.couplingDelayMs = 1.4;
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
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

    // Cajón snare side (4, redesigned: hand-percussion snare side, fuller
    // body but stays hand-driven rather than stick-driven, so click is
    // moderate not maximum).
    pads[4].exciterType = ExciterType::NoiseBurst;
    pads[4].bodyModel = BodyModelType::Plate;
    pads[4].material = 0.35; pads[4].size = 0.58; pads[4].decay = 0.55;
    pads[4].level = 0.95;
    pads[4].noiseBurstDuration = (5.0 - 2.0) / 13.0;
    pads[4].modeStretch = 0.42; pads[4].decaySkew = 0.40;
    pads[4].tsDriveAmount = 0.25;
    pads[4].tsFilterType = FilterType::LP;
    pads[4].tsFilterCutoff = 0.78;
    pads[4].tsFilterResonance = 0.15;
    pads[4].tsFilterEnvAmount = 0.65;
    pads[4].tsFilterEnvAttack = 0.0;
    pads[4].tsFilterEnvDecay = 0.385;
    pads[4].tsFilterEnvSustain = 0.0;
    pads[4].tsFilterEnvRelease = 0.20;
    pads[4].tensionModAmt = 0.25;
    pads[4].tsPitchEnvStart = toLogNorm(210);
    pads[4].tsPitchEnvEnd   = toLogNorm(140);
    pads[4].tsPitchEnvTime  = 0.13;
    pads[4].tsPitchEnvCurve = 0.15;
    pads[4].noiseLayerMix = 0.75; pads[4].noiseLayerCutoff = 0.75;
    pads[4].noiseLayerResonance = 0.10;
    pads[4].noiseLayerColor = 0.65; pads[4].noiseLayerDecay = 0.30;
    pads[4].clickLayerMix = 0.72; pads[4].clickLayerContactMs = 0.10;
    pads[4].clickLayerBrightness = 0.70;
    pads[4].airLoading = 0.35; pads[4].modeScatter = 0.38;
    pads[4].nonlinearCoupling = 0.18;
    pads[4].couplingStrength = 0.62; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.62; pads[4].secondaryMaterial = 0.30;
    pads[4].bodyDampingB1 = 0.28; pads[4].bodyDampingB3 = 0.05;

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
        pads[p].tsPitchEnvCurve = (i % 2) ? 0.5 : 0.15;  // Phase 10: was Lin/Exp toggle -> linear/log curveAmounts
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
