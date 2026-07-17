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
constexpr int kVersion           = 5;  // wire-coupling: 59-slot sound array (added wireCoupling)
constexpr int kSoundSlotsPerPad  = 59;

// kProcessorUID(0x4D656D62, 0x72756D50, 0x726F6331, 0x00000136)
const char kClassIdAscii[33] = "4D656D6272756D50726F633100000136";

enum ExciterType : int {
    Impulse    = 0,
    Mallet     = 1,
    NoiseBurst = 2,
    Friction   = 3,
    FMImpulse  = 4,
    Feedback   = 5,
    Clap       = 6,  // multi-burst hand-clap flam (HAND-CLAP-PLAN)
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
    // Snare-body fix: per-pad wire-buzz gain multiplier (default 1.0 = the
    // -18 dBFS accent ceiling). Snares push this up so the wire reaches
    // near-body level. Serialized at sound[57].
    double noiseLayerGain      = 1.0;
    // Wire coupling: buzz-follows-body depth (0 = independent buzz, legacy).
    // Serialized at sound[58].
    double wireCoupling        = 0.0;
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
        // [57] snare-body fix: per-pad noise-layer gain.
        p.noiseLayerGain,
        // [58] wire coupling: buzz-follows-body depth.
        p.wireCoupling,
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

    // ---- Pad 2: Acoustic Snare (Membrane/Impulse) -- struck body + wires + shell ----
    // Snare-body fix (INVESTIGATION-snare-body): coherent Impulse strike (was
    // NoiseBurst, which under-drove the low modes so the snare read as a hi-hat).
    pads[2].exciterType = ExciterType::Impulse;
    pads[2].bodyModel   = BodyModelType::Membrane;
    pads[2].material = 0.50;
    pads[2].size = 0.42;            // ~190 Hz head
    pads[2].decay = 0.13;           // short "tat" -- wire buzz carries the tail
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
    pads[2].bodyDampingB1 = 0.60;  // ~30 s^-1: fast head decay (short tat)
    pads[2].bodyDampingB3 = 0.40;  // moderate HF damping (body keeps some crack)
    pads[2].airLoading = 0.42;
    // Snare-body fix: the WIRE buzz is the snare's identity. Bright (~5 kHz)
    // white/violet, decays past the body; noiseLayerGain lifts it to near-body
    // level (mix + the -18 dBFS accent ceiling alone leave it a hollow woodblock).
    pads[2].noiseLayerMix       = 0.65;
    pads[2].noiseLayerCutoff    = 0.80;
    pads[2].noiseLayerResonance = 0.10;
    pads[2].noiseLayerDecay     = 0.55;
    pads[2].noiseLayerColor     = 0.75;
    pads[2].noiseLayerGain      = 6.2;   // wire reaches snare level (calibrated)
    // Wire coupling: buzz partially tracks the head so it dies with the body
    // and chokes on note-off instead of running its full fixed ~600 ms tail.
    pads[2].wireCoupling        = 0.45;
    pads[2].clickLayerMix        = 0.55;  // stick crack
    pads[2].clickLayerContactMs  = 0.18;
    pads[2].clickLayerBrightness = 0.90;
    pads[2].couplingStrength  = 0.78;
    pads[2].secondaryEnabled  = 1.0;
    pads[2].secondarySize     = 0.78;
    pads[2].secondaryMaterial = 0.52;
    pads[2].tensionModAmt = 0.16;
    pads[2].pan = 0.50;

    // ---- Pad 3: Hand Clap (NoiseBody/Clap) -- multi-burst flam + room tail ----
    // HAND-CLAP-PLAN: ClapExciter fires the 4-burst flam; NoiseBody with heavy
    // scatter/stretch + damping stays pitch-free; NoiseLayer is the ~300 ms
    // roomy tail (acoustic studio flavour). Verified recipe from default kit.
    pads[3].exciterType = ExciterType::Clap;
    pads[3].bodyModel   = BodyModelType::NoiseBody;
    pads[3].material = 0.70;
    pads[3].size = 0.25;
    pads[3].decay = 0.10;
    pads[3].level = 0.80;
    pads[3].noiseLayerMix       = 0.70;
    pads[3].noiseLayerCutoff    = 0.60;
    pads[3].noiseLayerResonance = 0.20;
    pads[3].noiseLayerDecay     = 0.58;  // ~300 ms room
    pads[3].noiseLayerColor     = 0.60;  // warm white
    pads[3].noiseLayerGain      = 2.2;   // tail must be audible vs burst train
    pads[3].clickLayerMix        = 0.25;
    pads[3].clickLayerContactMs  = 0.15;
    pads[3].clickLayerBrightness = 0.50;
    pads[3].modeScatter = 0.35;
    pads[3].modeStretch = 0.60;
    pads[3].bodyDampingB1 = 0.65;
    pads[3].bodyDampingB3 = 0.30;
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
    // Tom-depth Fix E: END = each pad's natural f0 (round(500*0.1^size)); START
    // a few % above so the bank glides DOWN onto f0 (no up-tuning). Longer glide
    // (250..120 ms). tomB1 dropped to a low constant so the fundamental rings
    // long; the b3 f^2 term (0.15 below) supplies the T60 gradient across sizes.
    const double tomPitchStart[] = {  84,  106,  134,  149,  188,  211};
    const double tomPitchEnd[]   = {  79,  100,  126,  141,  177,  199};
    const double tomPitchTime[]  = {0.50, 0.44, 0.38, 0.32, 0.28, 0.24};  // 250..120 ms
    const double tomB1[]         = {0.055, 0.055, 0.055, 0.055, 0.055, 0.055};
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
        pads[p].bodyDampingB3 = 0.15;  // Tom-depth Fix E: freq-dependent damping
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
    // Crash redesign (CRASH-REDESIGN-PLAN.md): NO harmonic drone (modeInject 0),
    // long low ring + f^2 HF roll-off (b1 0.020 / b3 8e-5), dominant bloomed
    // wash (noiseLayerGain), dark stick click. Mirrors the default Cymbal
    // template; per-kit character kept in level/pan/outputBus/noise colour.
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel   = BodyModelType::NoiseBody;
    pads[13].material = 0.93;
    pads[13].size = 0.35;
    pads[13].decay = 0.80;
    pads[13].strikePosition = 0.32;
    pads[13].level = 0.72;
    pads[13].modeStretch = 0.60;
    pads[13].modeInjectAmount = 0.0;
    pads[13].nonlinearCoupling = 0.35;
    pads[13].modeScatter = 0.35;
    pads[13].decaySkew = 0.55;
    pads[13].bodyDampingB1 = 0.060;
    pads[13].bodyDampingB3 = 0.00008;
    pads[13].noiseLayerMix    = 0.90;
    pads[13].noiseLayerCutoff = 0.85;
    pads[13].noiseLayerColor  = 0.79;  // White-Violet
    pads[13].noiseLayerDecay  = 0.95;
    pads[13].noiseLayerGain   = 3.5;   // bloomed wash carries the tail
    pads[13].clickLayerMix        = 0.30;
    pads[13].clickLayerContactMs  = 0.15;
    pads[13].clickLayerBrightness = 0.45;
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
    // Splash = a short, bright crash: same crash-correct engine but a faster
    // low ring (b1 0.05) and quicker HF roll-off (b3 1e-4) than crash 1.
    pads[19].modeStretch = 0.55;
    pads[19].nonlinearCoupling = 0.28;
    pads[19].modeScatter = 0.35;
    pads[19].decaySkew = 0.55;
    pads[19].strikePosition = 0.32;
    pads[19].bodyDampingB1 = 0.050;
    pads[19].bodyDampingB3 = 0.0001;
    pads[19].noiseLayerMix       = 0.85;
    pads[19].noiseLayerCutoff    = 0.90;
    pads[19].noiseLayerResonance = 0.20;
    pads[19].noiseLayerDecay     = 0.60;
    pads[19].noiseLayerColor     = 0.90;  // Violet
    pads[19].noiseLayerGain      = 3.0;
    pads[19].clickLayerMix        = 0.22;
    pads[19].clickLayerBrightness = 0.45;
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
    pads[21].strikePosition = 0.32;
    pads[21].level = 0.72;
    // Crash 2 = darker/larger sister of crash 1 (lower cutoff, White noise).
    pads[21].modeStretch = 0.60;
    pads[21].modeInjectAmount = 0.0;
    pads[21].nonlinearCoupling = 0.35;
    pads[21].modeScatter = 0.35;
    pads[21].decaySkew = 0.56;
    pads[21].strikePosition = 0.32;
    pads[21].bodyDampingB1 = 0.060;
    pads[21].bodyDampingB3 = 0.00008;
    pads[21].noiseLayerMix    = 0.90;
    pads[21].noiseLayerCutoff = 0.80;
    pads[21].noiseLayerColor  = 0.70;  // White
    pads[21].noiseLayerDecay  = 0.90;
    pads[21].noiseLayerGain   = 3.5;
    pads[21].clickLayerMix        = 0.30;
    pads[21].clickLayerBrightness = 0.45;
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

    // TR-808: sub-sine kick, boom-glide toms, two-tone snares, atonal hats,
    // detuned-fifth cowbell, clap, inharmonic crashes/ride. 19 crafted pads
    // (was 13); the dead axes (stretch/skew/scatter/inject/NLC/pan) now
    // exercised. Corrections folded in: tom tensionMod 0 (fights the
    // descending boom-glide), cowbell modeInject 0 (fights the clang).

    // ---- Pad 0: 808 Kick (Membrane/Impulse) ----
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.15; pads[0].size = 0.90; pads[0].decay = 0.35;
    pads[0].level = 0.85;
    pads[0].tsPitchEnvStart = toLogNorm(200);
    pads[0].tsPitchEnvEnd   = toLogNorm(40);
    pads[0].tsPitchEnvTime = 0.06; pads[0].tsPitchEnvCurve = 0.15;
    pads[0].airLoading       = 0.0;
    pads[0].couplingStrength = 0.0;
    pads[0].secondaryEnabled = 0.0;
    pads[0].tensionModAmt = 0.30;   // attack snap (kick: archetype-endorsed)
    pads[0].clickLayerMix        = 0.35;
    pads[0].clickLayerContactMs  = 0.20;
    pads[0].clickLayerBrightness = 0.30;
    pads[0].noiseLayerMix        = 0.0;
    pads[0].pan = 0.50;

    // ---- Pad 1: Side Stick / Rim (Shell/Impulse), NEW ----
    pads[1].exciterType = ExciterType::Impulse;
    pads[1].bodyModel = BodyModelType::Shell;
    pads[1].material = 0.55; pads[1].size = 0.22; pads[1].decay = 0.14;
    pads[1].level = 0.78; pads[1].strikePosition = 0.70;
    pads[1].modeScatter = 0.30;
    pads[1].clickLayerMix = 0.85; pads[1].clickLayerContactMs = 0.08;
    pads[1].clickLayerBrightness = 0.85;
    pads[1].noiseLayerMix = 0.0;
    pads[1].airLoading = 0.0;
    pads[1].bodyDampingB1 = 0.50; pads[1].bodyDampingB3 = 0.06;
    pads[1].pan = 0.55;

    // ---- Pads 2 & 4: 808 snares (Membrane/NoiseBurst), two-tone + metal
    //      secondary shell. Common body recipe, then per-pad tuning. ----
    for (int p : {2, 4}) {
        pads[p].exciterType = ExciterType::NoiseBurst;
        pads[p].bodyModel = BodyModelType::Membrane;
        pads[p].level = 1.0;
        pads[p].strikePosition = 0.45;
        pads[p].airLoading       = 0.0;
        pads[p].modeScatter      = 0.0;
        pads[p].modeStretch = 0.42; pads[p].decaySkew = 0.62;
        pads[p].couplingStrength = 0.35;
        pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize    = 0.38;
        pads[p].secondaryMaterial = 0.85;
        pads[p].bodyDampingB1    = 0.18;
        pads[p].bodyDampingB3    = 0.05;
        pads[p].tsDriveAmount    = 0.48;
        pads[p].tsFilterType     = FilterType::LP;
        pads[p].tsFilterCutoff   = 0.80;
        pads[p].tsFilterResonance= 0.22;
        pads[p].tsFilterEnvAmount= 0.60;
        pads[p].tsFilterEnvDecay = 0.385;
        pads[p].tsFilterEnvRelease= 0.20;
        pads[p].tensionModAmt    = 0.35;
        pads[p].tsPitchEnvCurve  = 0.10;
        pads[p].noiseLayerCutoff   = 0.82;
        pads[p].noiseLayerColor    = 0.78;
        pads[p].noiseLayerResonance= 0.12;
        pads[p].clickLayerContactMs = 0.08;
        pads[p].noiseBurstDuration = (4.0 - 2.0) / 13.0;
        pads[p].pan = 0.50;
    }
    // Pad 2: classic deep 808 snare
    pads[2].material = 0.58; pads[2].size = 0.68; pads[2].decay = 0.85;
    pads[2].tsPitchEnvStart = toLogNorm(400);
    pads[2].tsPitchEnvEnd   = toLogNorm(110);
    pads[2].tsPitchEnvTime  = 0.13;
    pads[2].noiseLayerMix = 0.62; pads[2].noiseLayerDecay = 0.40;
    pads[2].clickLayerMix = 0.55; pads[2].clickLayerBrightness = 0.72;

    // Pad 4: brighter sister snare
    pads[4].material = 0.66; pads[4].size = 0.60; pads[4].decay = 0.72;
    pads[4].tsPitchEnvStart = toLogNorm(480);
    pads[4].tsPitchEnvEnd   = toLogNorm(160);
    pads[4].tsPitchEnvTime  = 0.10;
    pads[4].noiseLayerMix = 0.62; pads[4].noiseLayerCutoff = 0.86;
    pads[4].noiseLayerDecay = 0.32;
    pads[4].clickLayerMix = 0.55; pads[4].clickLayerBrightness = 0.74;

    // ---- Pad 3: 808 Clap (NoiseBody/Clap), multi-burst flam ----
    // HAND-CLAP-PLAN: true 4-burst ClapExciter flam; tighter machine tail.
    pads[3].exciterType = ExciterType::Clap;
    pads[3].bodyModel = BodyModelType::NoiseBody;
    pads[3].material = 0.70; pads[3].size = 0.25; pads[3].decay = 0.10;
    pads[3].level = 0.78;
    pads[3].modeScatter = 0.35; pads[3].modeStretch = 0.60;
    pads[3].noiseLayerMix = 0.70; pads[3].noiseLayerCutoff = 0.60;
    pads[3].noiseLayerResonance = 0.20;
    pads[3].noiseLayerDecay = 0.55; pads[3].noiseLayerColor = 0.65;
    pads[3].noiseLayerGain = 2.2;
    pads[3].clickLayerMix = 0.25; pads[3].clickLayerContactMs = 0.15;
    pads[3].clickLayerBrightness = 0.50;
    pads[3].airLoading = 0.0;
    pads[3].bodyDampingB1 = 0.65; pads[3].bodyDampingB3 = 0.30;
    pads[3].macroBrightness = 0.65; pads[3].macroComplexity = 0.55;
    pads[3].pan = 0.50;

    // ---- Hats: pads 6 (closed) / 8 (pedal) / 10 (open), choke group 1 ----
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::NoiseBody;
    pads[6].material = 0.92; pads[6].size = 0.10; pads[6].decay = 0.08;
    pads[6].level = 0.75;
    pads[6].chokeGroup = 1;
    pads[6].noiseBurstDuration = (3.0 - 2.0) / 13.0;
    pads[6].noiseLayerMix    = 0.85;
    pads[6].noiseLayerCutoff = 0.92;
    pads[6].noiseLayerColor  = 0.85;
    pads[6].noiseLayerDecay  = 0.10;
    pads[6].noiseLayerResonance = 0.20;
    pads[6].modeScatter = 0.35; pads[6].modeStretch = 0.45;
    pads[6].decaySkew = 0.55;
    pads[6].clickLayerMix    = 0.0;
    pads[6].bodyDampingB1 = 0.65; pads[6].bodyDampingB3 = 0.0;
    pads[6].airLoading       = 0.0;
    pads[6].pan = 0.55;

    pads[8] = pads[6];
    pads[8].material = 0.88; pads[8].size = 0.12; pads[8].decay = 0.06;
    pads[8].level = 0.70; pads[8].noiseLayerDecay = 0.07;
    pads[8].bodyDampingB1 = 0.72;
    pads[8].decaySkew = 0.5;   // pedal: neutral

    pads[10] = pads[6];
    pads[10].material = 0.90; pads[10].size = 0.20; pads[10].decay = 0.50;
    pads[10].level = 0.72; pads[10].strikePosition = 0.60;
    pads[10].tsFilterType = FilterType::HP; pads[10].tsFilterCutoff = 0.534;
    pads[10].noiseBurstDuration = 0.15;
    pads[10].noiseLayerMix = 0.80; pads[10].noiseLayerCutoff = 0.82;
    pads[10].noiseLayerDecay = 0.55; pads[10].noiseLayerColor = 0.90;
    pads[10].decaySkew = 0.60;
    pads[10].bodyDampingB1 = 0.40;   // sentinel-ish: longer open ring

    // ---- Pad 13: Crash 1 (NoiseBody/NoiseBurst), aux bus 1 ----
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel = BodyModelType::NoiseBody;
    pads[13].material = 0.95; pads[13].size = 0.35; pads[13].decay = 0.70;
    pads[13].level = 0.70; pads[13].strikePosition = 0.32;
    pads[13].modeStretch = 0.60; pads[13].modeInjectAmount = 0.0;
    pads[13].nonlinearCoupling = 0.35; pads[13].modeScatter = 0.60;
    pads[13].bodyDampingB1 = 0.060; pads[13].bodyDampingB3 = 0.00008;
    pads[13].noiseLayerMix    = 0.55;
    pads[13].noiseLayerCutoff = 0.92;
    pads[13].noiseLayerColor  = 0.82;
    pads[13].noiseLayerDecay  = 0.65;
    pads[13].clickLayerMix = 0.20; pads[13].clickLayerBrightness = 0.82;
    pads[13].airLoading       = 0.0;
    pads[13].outputBus = 1;
    pads[13].pan = 0.40;

    // ---- Pad 15: 808 Ride (NoiseBody/NoiseBurst), aux bus 1, NEW ----
    pads[15].exciterType = ExciterType::NoiseBurst;
    pads[15].bodyModel = BodyModelType::NoiseBody;
    pads[15].material = 0.93; pads[15].size = 0.30; pads[15].decay = 0.55;
    pads[15].level = 0.68; pads[15].strikePosition = 0.40;
    pads[15].modeStretch = 0.52; pads[15].modeScatter = 0.45;
    pads[15].decaySkew = 0.58; pads[15].nonlinearCoupling = 0.25;
    pads[15].bodyDampingB1 = 0.35; pads[15].bodyDampingB3 = 0.0;
    pads[15].noiseLayerMix = 0.40; pads[15].noiseLayerCutoff = 0.88;
    pads[15].noiseLayerColor = 0.82; pads[15].noiseLayerDecay = 0.40;
    pads[15].clickLayerMix = 0.30; pads[15].clickLayerBrightness = 0.85;
    pads[15].airLoading = 0.0;
    pads[15].outputBus = 1;
    pads[15].pan = 0.60;

    // ---- Toms 5/7/9/11/12/14: 808 boom-glide (Membrane/Mallet) ----
    // tensionMod 0 (upward glide fights the descending boom-glide).
    const int    tomPads[]      = {5, 7, 9, 11, 12, 14};
    const double tomSizes[]     = {0.85, 0.75, 0.65, 0.55, 0.48, 0.40};
    const double tomPitchStart[] = {220,  260,  310,  370,  430,  500};
    const double tomPitchEnd[]  = { 80,   95,  115,  140,  175,  220};
    const double tomPitchTime[] = {0.50, 0.42, 0.36, 0.30, 0.24, 0.18};
    const double tomMaterial[]  = {0.18, 0.25, 0.32, 0.40, 0.50, 0.60};
    const double tomDecay[]     = {0.65, 0.58, 0.50, 0.43, 0.35, 0.28};
    const double tomBodyB1[]    = {0.10, 0.15, 0.20, 0.25, 0.32, 0.42};
    const double tomPan[]       = {0.30, 0.40, 0.48, 0.55, 0.62, 0.70};
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
        pads[p].tsPitchEnvCurve = 0.5;
        pads[p].airLoading       = 0.0;
        pads[p].couplingStrength = 0.0;
        pads[p].secondaryEnabled = 0.0;
        pads[p].tensionModAmt    = 0.0;   // FIXED: was 0.30
        pads[p].noiseLayerMix    = 0.05; pads[p].noiseLayerColor = 0.40;
        pads[p].noiseLayerDecay  = 0.55;
        pads[p].clickLayerMix    = 0.05;
        pads[p].bodyDampingB1    = tomBodyB1[i];
        pads[p].bodyDampingB3    = 0.10;
        pads[p].pan = tomPan[i];
    }

    // ---- Pad 20: Cowbell (Bell/FMImpulse), detuned-fifth clang, NEW ----
    pads[20].exciterType = ExciterType::FMImpulse;
    pads[20].bodyModel = BodyModelType::Bell;
    pads[20].material = 0.78; pads[20].size = 0.22; pads[20].decay = 0.30;
    pads[20].level = 0.75; pads[20].strikePosition = 0.30;
    pads[20].fmRatio = 0.50;   // -> 2.5 detuned fifth
    pads[20].modeStretch = 0.55; pads[20].modeScatter = 0.20;
    pads[20].decaySkew = 0.42; pads[20].modeInjectAmount = 0.0;  // FIXED
    pads[20].clickLayerMix = 0.55; pads[20].clickLayerContactMs = 0.10;
    pads[20].clickLayerBrightness = 0.72;
    pads[20].noiseLayerMix = 0.10; pads[20].noiseLayerColor = 0.40;
    pads[20].noiseLayerCutoff = 0.62; pads[20].noiseLayerDecay = 0.20;
    pads[20].airLoading = 0.0;
    pads[20].bodyDampingB1 = 0.32; pads[20].bodyDampingB3 = 0.0;
    pads[20].macroBrightness = 0.65;
    pads[20].pan = 0.42;

    // ---- Pad 21: Crash 2 bright (NoiseBody/NoiseBurst), aux bus 1, NEW ----
    pads[21].exciterType = ExciterType::NoiseBurst;
    pads[21].bodyModel = BodyModelType::NoiseBody;
    pads[21].material = 0.96; pads[21].size = 0.28; pads[21].decay = 0.55;
    pads[21].level = 0.68; pads[21].strikePosition = 0.60;
    pads[21].modeStretch = 0.65; pads[21].modeScatter = 0.70;
    pads[21].nonlinearCoupling = 0.35; pads[21].modeInjectAmount = 0.0;
    pads[21].bodyDampingB1 = 0.060; pads[21].bodyDampingB3 = 0.00008;
    pads[21].noiseLayerMix = 0.55; pads[21].noiseLayerCutoff = 0.94;
    pads[21].noiseLayerColor = 0.85; pads[21].noiseLayerDecay = 0.55;
    pads[21].clickLayerMix = 0.20;
    pads[21].airLoading = 0.0;
    pads[21].outputBus = 1;
    pads[21].pan = 0.62;

    // ---- Pad 23: FM-Bell Perc / tuned bell (Bell/FMImpulse) ----
    pads[23].exciterType = ExciterType::FMImpulse;
    pads[23].bodyModel = BodyModelType::Bell;
    pads[23].material = 0.70; pads[23].size = 0.25; pads[23].decay = 0.20;
    pads[23].level = 0.75; pads[23].strikePosition = 0.30;
    pads[23].fmRatio = 0.40;   // -> 2.2
    pads[23].clickLayerMix = 0.15; pads[23].clickLayerBrightness = 0.70;
    pads[23].noiseLayerMix = 0.0;
    pads[23].airLoading = 0.0;
    pads[23].bodyDampingB3 = 0.0;
    pads[23].pan = 0.58;

    // 19 sounding pads; 16-19, 22, 24-31 disabled (no canonical 808 voice).
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 20, 21, 23};
    return k;
}
Kit experimentalKit() {
    Kit k{"Experimental FX Kit", "Unnatural", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Synthetic sound-design flagship. GM anchors kept; FX region individually
    // crafted (was a 20-pad i-ramp). The only kit using modeInject (0.2 on the
    // chaos cycle + glass-bell pings). All 32 pads live. tensionMod/airLoading
    // are Membrane-only (kick + the one Membrane chaos pad).

    // ---- Pad 0: FM/Chaotic Kick (Membrane/FMImpulse) ----
    pads[0].exciterType = ExciterType::FMImpulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.20; pads[0].size = 0.85; pads[0].decay = 0.40;
    pads[0].strikePosition = 0.25; pads[0].level = 0.70; pads[0].fmRatio = 0.60;
    pads[0].tsPitchEnvStart = toLogNorm(300); pads[0].tsPitchEnvEnd = toLogNorm(30);
    pads[0].tsPitchEnvTime = 0.15; pads[0].tsPitchEnvCurve = 0.15;
    pads[0].tsDriveAmount = 0.30;
    pads[0].modeStretch = 0.333333; pads[0].decaySkew = 0.45; pads[0].modeScatter = 0.30;
    pads[0].nonlinearCoupling = 0.35;
    pads[0].tensionModAmt = 0.50; pads[0].airLoading = 0.40; // Membrane-only, live
    pads[0].couplingStrength = 0.50; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.60; pads[0].secondaryMaterial = 0.85;
    pads[0].clickLayerMix = 0.45; pads[0].clickLayerBrightness = 0.40;
    pads[0].noiseLayerMix = 0.25; pads[0].noiseLayerColor = 0.15; // brown air
    pads[0].bodyDampingB1 = 0.18; pads[0].bodyDampingB3 = 0.25;
    pads[0].macroPunch = 0.85; pads[0].pan = 0.50;

    // ---- Pad 2: Feedback-Shell Snare (Shell/Feedback) ----
    pads[2].exciterType = ExciterType::Feedback;
    pads[2].bodyModel = BodyModelType::Shell;
    pads[2].material = 0.55; pads[2].size = 0.55; pads[2].decay = 0.58;
    pads[2].level = 0.95; pads[2].feedbackAmount = 0.40;
    pads[2].modeScatter = 0.42; pads[2].nonlinearCoupling = 0.22;
    pads[2].couplingStrength = 0.65; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.65; pads[2].secondaryMaterial = 0.70;
    pads[2].tsDriveAmount = 0.30;
    pads[2].tsFilterType = FilterType::LP; pads[2].tsFilterCutoff = 0.80;
    pads[2].tsFilterResonance = 0.20; pads[2].tsFilterEnvAmount = 0.70;
    pads[2].tsFilterEnvAttack = 0.0; pads[2].tsFilterEnvDecay = 0.385;
    pads[2].tsFilterEnvSustain = 0.0; pads[2].tsFilterEnvRelease = 0.20;
    pads[2].tsPitchEnvStart = toLogNorm(230); pads[2].tsPitchEnvEnd = toLogNorm(160);
    pads[2].tsPitchEnvTime = 0.12; pads[2].tsPitchEnvCurve = 0.15;
    pads[2].noiseLayerMix = 0.70; pads[2].noiseLayerCutoff = 0.80;
    pads[2].noiseLayerColor = 0.70; pads[2].noiseLayerDecay = 0.32; // white
    pads[2].clickLayerMix = 0.85; pads[2].clickLayerContactMs = 0.08;
    pads[2].clickLayerBrightness = 0.85;
    pads[2].airLoading = 0.0; // no-op on Shell
    pads[2].bodyDampingB1 = 0.28; pads[2].bodyDampingB3 = 0.04;
    pads[2].pan = 0.45;

    // ---- Pad 4: FM-Plate Snare sister (Plate/FMImpulse) ----
    pads[4].exciterType = ExciterType::FMImpulse;
    pads[4].bodyModel = BodyModelType::Plate;
    pads[4].material = 0.45; pads[4].size = 0.55; pads[4].decay = 0.45;
    pads[4].strikePosition = 0.62; pads[4].level = 0.90; pads[4].fmRatio = 0.55;
    pads[4].modeStretch = 0.55; pads[4].decaySkew = 0.42; pads[4].modeScatter = 0.45;
    pads[4].nonlinearCoupling = 0.30;
    pads[4].tsPitchEnvStart = toLogNorm(230); pads[4].tsPitchEnvEnd = toLogNorm(160);
    pads[4].tsPitchEnvTime = 0.12; pads[4].tsPitchEnvCurve = 0.15;
    pads[4].tsFilterType = FilterType::LP; pads[4].tsFilterCutoff = 0.82;
    pads[4].tsFilterResonance = 0.20; pads[4].tsFilterEnvAmount = 0.60;
    pads[4].tsFilterEnvDecay = 0.385; pads[4].tsFilterEnvRelease = 0.20;
    pads[4].morphEnabled = 1.0; pads[4].morphStart = 0.55; pads[4].morphEnd = 0.80;
    pads[4].morphDuration = 0.30; pads[4].morphCurve = 0.5;
    pads[4].noiseLayerMix = 0.60; pads[4].noiseLayerColor = 0.70; // white
    pads[4].noiseLayerCutoff = 0.80; pads[4].noiseLayerDecay = 0.30;
    pads[4].clickLayerMix = 0.85; pads[4].clickLayerBrightness = 0.85;
    pads[4].bodyDampingB1 = 0.30; pads[4].bodyDampingB3 = 0.18;
    pads[4].macroBrightness = 0.85; pads[4].pan = 0.55;

    // ---- Metal hats (Bell/NoiseBurst, choke 1) ----
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::Bell;
    pads[6].material = 0.95; pads[6].size = 0.10; pads[6].decay = 0.08;
    pads[6].level = 0.70; pads[6].chokeGroup = 1;
    pads[6].morphEnabled = 1.0; pads[6].morphStart = 0.95; pads[6].morphEnd = 0.30;
    pads[6].morphDuration = 0.30; pads[6].morphCurve = 0.5;
    pads[6].modeScatter = 0.45; pads[6].bodyDampingB3 = 0.0;
    pads[6].noiseLayerMix = 0.55; pads[6].noiseLayerCutoff = 0.90;
    pads[6].noiseLayerColor = 0.85; // violet
    pads[6].clickLayerMix = 0.20; pads[6].clickLayerBrightness = 0.92;
    pads[6].pan = 0.58;

    pads[8] = pads[6];
    pads[8].decay = 0.04;

    pads[10] = pads[6];
    pads[10].decay = 0.60; pads[10].noiseLayerDecay = 0.55;

    // ---- Inharmonic Plate Toms (Plate/Mallet, pitch-env kerthump) ----
    const int    tomPads[]  = {5, 7, 9, 11, 12, 14};
    const double tomSizes[] = {0.85, 0.75, 0.65, 0.55, 0.45, 0.35};
    const double tomHi[]    = {240, 290, 360, 440, 520, 600};
    const double tomLo[]    = {80, 100, 130, 160, 190, 220};
    const double tomPan[]   = {0.30, 0.40, 0.46, 0.56, 0.62, 0.70};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel = BodyModelType::Plate;
        pads[p].material = 0.50; pads[p].size = tomSizes[i]; pads[p].decay = 0.55;
        pads[p].strikePosition = 0.62; pads[p].level = 0.78;
        pads[p].modeStretch = 0.50; pads[p].decaySkew = 0.42; pads[p].modeScatter = 0.50;
        pads[p].nonlinearCoupling = 0.30;
        pads[p].tsPitchEnvStart = toLogNorm(tomHi[i]); pads[p].tsPitchEnvEnd = toLogNorm(tomLo[i]);
        pads[p].tsPitchEnvTime = 0.09; pads[p].tsPitchEnvCurve = 0.15;
        pads[p].tsFoldAmount = 0.18; pads[p].tsDriveAmount = 0.12;
        pads[p].airLoading = 0.0; // no-op on Plate
        pads[p].couplingStrength = 0.30 + 0.04 * i; pads[p].secondaryEnabled = 1.0;
        pads[p].secondarySize = 0.30 + 0.05 * i; pads[p].secondaryMaterial = 0.65;
        pads[p].clickLayerMix = 0.40; pads[p].clickLayerBrightness = 0.65;
        pads[p].bodyDampingB1 = 0.30 + 0.04 * i; pads[p].bodyDampingB3 = 0.10;
        pads[p].macroComplexity = 0.75; pads[p].pan = tomPan[i];
    }

    // ---- Pad 13: FM-Bell Crash (Bell/FMImpulse, aux 1) ----
    pads[13].exciterType = ExciterType::FMImpulse;
    pads[13].bodyModel = BodyModelType::Bell;
    pads[13].material = 0.92; pads[13].size = 0.42; pads[13].decay = 0.78;
    pads[13].strikePosition = 0.32; pads[13].level = 0.72; pads[13].fmRatio = 0.45;
    pads[13].modeStretch = 0.45; pads[13].decaySkew = 0.65; pads[13].modeScatter = 0.55;
    pads[13].nonlinearCoupling = 0.45;
    pads[13].noiseLayerMix = 0.50; pads[13].noiseLayerCutoff = 0.92;
    pads[13].noiseLayerColor = 0.85; pads[13].noiseLayerDecay = 0.72;
    pads[13].noiseLayerResonance = 0.20;
    pads[13].clickLayerMix = 0.30; pads[13].clickLayerContactMs = 0.30;
    pads[13].clickLayerBrightness = 0.85;
    pads[13].bodyDampingB1 = 0.30; pads[13].bodyDampingB3 = 0.0;
    pads[13].outputBus = 1; pads[13].pan = 0.45;

    // ---- Chaos cycle (15-24, 26, 28) — body i%6 x exciter i%3, modeInject 0.2 ----
    {
        const int chaosPads[] = {15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 26, 28};
        const int bodyCyc[]   = {BodyModelType::Plate, BodyModelType::Shell,
                                 BodyModelType::String, BodyModelType::Bell,
                                 BodyModelType::Membrane, BodyModelType::NoiseBody};
        const int excCyc[]    = {ExciterType::Feedback, ExciterType::FMImpulse,
                                 ExciterType::Friction};
        const double filtCyc[]= {FilterType::LP, FilterType::HP, FilterType::BP};
        const int n = 12;
        for (int j = 0; j < n; ++j) {
            const int p = chaosPads[j];
            const int body = bodyCyc[j % 6];
            pads[p].exciterType = excCyc[j % 3];
            pads[p].bodyModel = body;
            pads[p].material = 0.40 + (j % 5) * 0.10;
            pads[p].size = 0.30 + (j % 6) * 0.06;
            pads[p].decay = 0.45 + (j % 4) * 0.10;
            pads[p].level = 0.72;
            pads[p].fmRatio = 0.40 + (j % 5) * 0.08;
            pads[p].feedbackAmount = 0.45 + (j % 3) * 0.05;
            pads[p].frictionPressure = 0.40 + (j % 3) * 0.08;
            pads[p].modeStretch = 0.60 + j * 0.025; // 0.60 -> 0.875
            pads[p].decaySkew = 0.55; pads[p].modeScatter = 0.60;
            pads[p].modeInjectAmount = 0.20; // kit signature
            pads[p].nonlinearCoupling = 0.75 + (j % 3) * 0.05;
            pads[p].tsDriveAmount = 0.45; pads[p].tsFoldAmount = 0.35 + (j % 4) * 0.05;
            pads[p].tsFilterType = filtCyc[j % 3];
            pads[p].tsFilterCutoff = 0.40 + (j % 5) * 0.08;
            pads[p].tsFilterResonance = 0.55;
            pads[p].tsFilterEnvAmount = 0.50;
            pads[p].noiseLayerMix = 0.25 + (j % 4) * 0.08;
            pads[p].noiseLayerColor = 0.40 + (j % 5) * 0.10;
            pads[p].clickLayerMix = 0.20 + (j % 3) * 0.10;
            pads[p].bodyDampingB1 = 0.30 + (j % 5) * 0.04;
            pads[p].bodyDampingB3 = (body == BodyModelType::Bell) ? 0.0 : 0.20;
            // Membrane-only axes: live on the Membrane pad, off elsewhere.
            if (body == BodyModelType::Membrane) {
                pads[p].tensionModAmt = 0.80; pads[p].airLoading = 0.0; // deliberate anti-whistle
            } else {
                pads[p].tensionModAmt = 0.0; pads[p].airLoading = 0.0;
            }
            pads[p].chokeGroup = 2;
            pads[p].outputBus = j % 4; // bus spread (0 main + aux 1/2/3)
            pads[p].macroComplexity = 0.85; pads[p].couplingAmount = 0.78;
            pads[p].pan = 0.15 + (j % 6) * 0.13;
        }
    }

    // ---- Pad 3: Friction String Drone (String/Friction, aux 1) ----
    pads[3].exciterType = ExciterType::Friction;
    pads[3].bodyModel = BodyModelType::String;
    pads[3].material = 0.55; pads[3].size = 0.60; pads[3].decay = 0.92;
    pads[3].level = 0.65; pads[3].frictionPressure = 0.50;
    pads[3].nonlinearCoupling = 0.40;
    pads[3].tsFilterType = FilterType::LP; pads[3].tsFilterCutoff = 0.45;
    pads[3].tsFilterResonance = 0.35; pads[3].tsFilterEnvAmount = 0.60;
    pads[3].tsFilterEnvAttack = 0.20; pads[3].tsFilterEnvDecay = 0.40;
    pads[3].tsFilterEnvSustain = 0.55; pads[3].tsFilterEnvRelease = 0.65;
    pads[3].tsDriveAmount = 0.26; pads[3].tsFoldAmount = 0.22;
    pads[3].morphEnabled = 1.0; pads[3].morphStart = 0.45; pads[3].morphEnd = 0.70;
    pads[3].morphDuration = 0.85; pads[3].morphCurve = 0.0; // exp
    pads[3].couplingStrength = 0.18; pads[3].secondaryEnabled = 1.0;
    pads[3].secondarySize = 0.50; pads[3].secondaryMaterial = 0.65;
    pads[3].noiseLayerMix = 0.16; pads[3].noiseLayerColor = 0.12; // brown
    pads[3].noiseLayerDecay = 0.85; pads[3].clickLayerMix = 0.0;
    pads[3].outputBus = 1; pads[3].macroComplexity = 0.85;
    pads[3].couplingAmount = 0.85; pads[3].pan = 0.30;

    // ---- Pad 25: Feedback Plate Drone (Plate/Feedback, aux 1, BP bloom) ----
    pads[25].exciterType = ExciterType::Feedback;
    pads[25].bodyModel = BodyModelType::Plate;
    pads[25].material = 0.62; pads[25].size = 0.60; pads[25].decay = 0.88;
    pads[25].strikePosition = 0.45; pads[25].level = 0.62; pads[25].feedbackAmount = 0.55;
    pads[25].tsFoldAmount = 0.25;
    pads[25].modeStretch = 0.50; pads[25].modeScatter = 0.35;
    pads[25].modeInjectAmount = 0.15; pads[25].nonlinearCoupling = 0.50;
    pads[25].decaySkew = 0.78;
    pads[25].tsFilterType = FilterType::BP; pads[25].tsFilterCutoff = 0.55;
    pads[25].tsFilterResonance = 0.45; pads[25].tsFilterEnvAmount = 0.65;
    pads[25].tsFilterEnvAttack = 0.45; pads[25].tsFilterEnvDecay = 0.60;
    pads[25].tsFilterEnvSustain = 0.55; pads[25].tsFilterEnvRelease = 0.65;
    pads[25].noiseLayerMix = 0.30; pads[25].noiseLayerColor = 0.70; // white
    pads[25].noiseLayerCutoff = 0.55; pads[25].noiseLayerDecay = 0.85;
    pads[25].clickLayerMix = 0.0;
    pads[25].bodyDampingB1 = 0.20; pads[25].bodyDampingB3 = 0.20;
    pads[25].outputBus = 1; pads[25].macroComplexity = 0.80;
    pads[25].couplingAmount = 0.85; pads[25].pan = 0.30;

    // ---- Pad 27: Feedback Shell Drone (Shell variant, aux 2) ----
    pads[27] = pads[25];
    pads[27].bodyModel = BodyModelType::Shell;
    pads[27].size = 0.55; pads[27].secondaryMaterial = 0.70;
    pads[27].outputBus = 2; pads[27].pan = 0.70;

    // ---- Pad 29: Ghost Tone (Bell/Mallet, high-stretch skeletal, aux 1) ----
    pads[29].exciterType = ExciterType::Mallet;
    pads[29].bodyModel = BodyModelType::Bell;
    pads[29].material = 0.50; pads[29].size = 0.40; pads[29].decay = 0.80;
    pads[29].strikePosition = 0.30; pads[29].level = 0.64;
    pads[29].modeStretch = 0.77; pads[29].decaySkew = 0.81; pads[29].modeScatter = 0.45;
    pads[29].nonlinearCoupling = 0.32; pads[29].modeInjectAmount = 0.0;
    pads[29].tsFilterType = FilterType::HP; pads[29].tsFilterCutoff = 0.24;
    pads[29].tsFilterResonance = 0.30; pads[29].tsFilterEnvAmount = 0.45;
    pads[29].morphEnabled = 1.0; pads[29].morphStart = 0.45; pads[29].morphEnd = 0.80;
    pads[29].morphDuration = 0.75; pads[29].morphCurve = 0.5;
    pads[29].noiseLayerMix = 0.12; pads[29].noiseLayerColor = 0.12; // brown
    pads[29].noiseLayerCutoff = 0.35; pads[29].noiseLayerDecay = 0.85;
    pads[29].clickLayerMix = 0.12;
    pads[29].bodyDampingB1 = 0.35; pads[29].bodyDampingB3 = 0.0;
    pads[29].outputBus = 1; pads[29].couplingAmount = 0.85; pads[29].pan = 0.60;

    // ---- Pad 30: Clap (NoiseBody/Clap), multi-burst flam ----
    pads[30].exciterType = ExciterType::Clap;
    pads[30].bodyModel = BodyModelType::NoiseBody;
    pads[30].material = 0.70; pads[30].size = 0.25; pads[30].decay = 0.10;
    pads[30].level = 0.80;
    pads[30].noiseLayerMix = 0.70; pads[30].noiseLayerCutoff = 0.60;
    pads[30].noiseLayerResonance = 0.20; pads[30].noiseLayerColor = 0.65;
    pads[30].noiseLayerDecay = 0.58;
    pads[30].noiseLayerGain = 2.2;
    pads[30].clickLayerMix = 0.25; pads[30].clickLayerContactMs = 0.15;
    pads[30].clickLayerBrightness = 0.50;
    pads[30].modeScatter = 0.35; pads[30].modeStretch = 0.60;
    pads[30].bodyDampingB1 = 0.65; pads[30].bodyDampingB3 = 0.30;
    pads[30].macroBrightness = 0.65; pads[30].macroComplexity = 0.55;
    pads[30].pan = 0.50;

    // ---- Glass-Bell pings 1 (FMImpulse) / 31 (Mallet), modeInject 0.2, aux 1 ----
    pads[1].exciterType = ExciterType::FMImpulse;
    pads[1].bodyModel = BodyModelType::Bell;
    pads[1].material = 0.70; pads[1].size = 0.30; pads[1].decay = 0.55;
    pads[1].level = 0.70; pads[1].fmRatio = 0.45;
    pads[1].modeStretch = 0.45; pads[1].decaySkew = 0.60; pads[1].modeScatter = 0.50;
    pads[1].modeInjectAmount = 0.20; pads[1].nonlinearCoupling = 0.20;
    pads[1].clickLayerMix = 0.15; pads[1].clickLayerBrightness = 0.70;
    pads[1].noiseLayerMix = 0.10;
    pads[1].bodyDampingB1 = 0.30; pads[1].bodyDampingB3 = 0.0;
    pads[1].outputBus = 1; pads[1].pan = 0.30;

    pads[31].exciterType = ExciterType::Mallet;
    pads[31].bodyModel = BodyModelType::Bell;
    pads[31].material = 0.85; pads[31].size = 0.20; pads[31].decay = 0.50;
    pads[31].level = 0.70;
    pads[31].modeStretch = 0.47; pads[31].decaySkew = 0.70; pads[31].modeScatter = 0.55;
    pads[31].modeInjectAmount = 0.20; pads[31].nonlinearCoupling = 0.20;
    pads[31].clickLayerMix = 0.42; pads[31].clickLayerBrightness = 0.85;
    pads[31].noiseLayerMix = 0.08;
    pads[31].bodyDampingB1 = 0.30; pads[31].bodyDampingB3 = 0.0;
    pads[31].outputBus = 1; pads[31].pan = 0.70;

    k.opts.maxPolyphony   = 16;
    k.opts.globalCoupling = 0.30;
    // crafted left empty: all 32 pads stay live (FX-kit design).
    return k;
}
Kit jazzBrushesKit() {
    Kit k{"Jazz Brushes", "Acoustic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Gentle sympathetic snare-buzz + tom-resonance for brush character. Kept
    // moderate: the infinite-ring accumulators were pad 3 (continuous Friction)
    // and the crash modeInject, both now fixed, so a light network no longer
    // builds into a sustained ring under dense repeated hits.
    k.opts.maxPolyphony   = 10;
    k.opts.globalCoupling = 0.18;
    k.opts.snareBuzz      = 0.18;
    k.opts.tomResonance   = 0.22;

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
    // Tom-depth Fix E: retune END to each pad's natural f0 (round(500*0.1^size)
    // for sizes 0.72..0.36 = 95,120,141,166,190,218 Hz), START ~6% above (glide
    // DOWN), longer graded glide time, low constant b1 for a long fundamental.
    const double tomB1[]        = {0.055, 0.055, 0.055, 0.055, 0.055, 0.055};
    const double tomPitchStart[] = {101, 127, 149, 176, 201, 231};
    const double tomPitchEnd[]   = { 95, 120, 141, 166, 190, 218};
    const double tomPitchTime[]  = {0.50, 0.44, 0.38, 0.32, 0.28, 0.24};  // 250..120 ms
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
        pads[p].tsPitchEnvTime  = tomPitchTime[i];
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
        pads[p].bodyDampingB1 = tomB1[i]; pads[p].bodyDampingB3 = 0.15;  // Fix E
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
    pads[15].modeStretch = 0.55; pads[15].modeInjectAmount = 0.0; // inject off: 1/k modes ring ~2s undamped (infinite-ring regression)
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
    // The kit's only Friction + modeInject voice. Kept as a SHORT brush swirl
    // NoiseBurst (NOT Friction): the kit is triggered with one-shot note-ons
    // (no note-off), so a continuous Friction drive would sustain as a flat
    // tone until the voice is stolen and bleed into the next hit (infinite-ring
    // regression). A long-burst NoiseBurst over a dark LP membrane gives the
    // same dark brush-swirl character but decays naturally. Held out of the
    // sympathetic network (couplingStrength 0, secondary off).
    pads[3].exciterType = ExciterType::NoiseBurst;
    pads[3].bodyModel   = BodyModelType::Membrane;
    pads[3].material = 0.40; pads[3].size = 0.58; pads[3].decay = 0.42;
    pads[3].level = 0.62;
    pads[3].noiseBurstDuration = 0.85;  // long brush-swirl smear
    pads[3].nonlinearCoupling = 0.0;
    pads[3].decaySkew = 0.55;
    pads[3].tensionModAmt = 0.0;
    pads[3].airLoading = 0.45; pads[3].modeScatter = 0.30;
    pads[3].tsFilterType   = FilterType::LP;
    pads[3].tsFilterCutoff = 0.55;      // dark bed
    pads[3].clickLayerMix = 0.0;
    pads[3].noiseLayerMix = 0.30; pads[3].noiseLayerColor = 0.40;  // pink
    pads[3].noiseLayerDecay = 0.50;
    pads[3].couplingStrength = 0.0; pads[3].secondaryEnabled = 0.0;
    pads[3].bodyDampingB1 = 0.40; pads[3].bodyDampingB3 = 0.10;
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
    // Snare-body fix (INVESTIGATION-snare-body): Impulse strike (was NoiseBurst)
    // so the big-room body rings instead of reading as filtered noise.
    pads[2].exciterType = ExciterType::Impulse;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.38; pads[2].size = 0.66; pads[2].decay = 0.20;  // short-ish tat (big-room body)
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
    // Snare-body fix: bright, loud wire buzz (the snare's identity) lifted to
    // near-body level via noiseLayerGain; carries the tail past the short body.
    pads[2].noiseLayerMix    = 0.65; pads[2].noiseLayerCutoff = 0.80;
    pads[2].noiseLayerResonance = 0.15;
    pads[2].noiseLayerColor  = 0.75; pads[2].noiseLayerDecay = 0.55;
    pads[2].noiseLayerGain   = 6.2;
    pads[2].wireCoupling     = 0.45;  // buzz tracks head: dies with body, chokes on note-off
    pads[2].clickLayerMix    = 0.55; pads[2].clickLayerContactMs = 0.06;
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
    pads[2].bodyDampingB1 = 0.50; pads[2].bodyDampingB3 = 0.40;  // snare-body fix: fast-ish head decay (short tat) + moderate HF damping
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
    // Tom-depth Fix E: END(Lo) = natural f0 round(500*0.1^size) for sizes
    // 0.92..0.48 = 60,71,89,112,141,166 Hz; START(Hi) ~6% above (glide DOWN);
    // low constant b1 for a long fundamental; graded glide time added below.
    const double tomPitchHi[]   = { 64,  75,  94, 119, 149, 176};
    const double tomPitchLo[]   = { 60,  71,  89, 112, 141, 166};
    const double tomB1[]        = {0.055, 0.055, 0.055, 0.055, 0.055, 0.055};
    const double tomPitchTime[] = {0.50, 0.44, 0.38, 0.32, 0.28, 0.24};  // 250..120 ms
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
        pads[p].tsPitchEnvTime  = tomPitchTime[i];
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
        pads[p].bodyDampingB1 = tomB1[i]; pads[p].bodyDampingB3 = 0.15;  // Fix E
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
    pads[13].strikePosition = 0.32;   // near-edge plate strike
    pads[13].modeStretch = 0.60; pads[13].modeScatter = 0.60;
    pads[13].modeInjectAmount = 0.0; pads[13].nonlinearCoupling = 0.35;
    pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.00008; pads[13].bodyDampingB1 = 0.060;
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
    pads[16].modeStretch = 0.66; pads[16].modeScatter = 0.35;
    pads[16].modeInjectAmount = 0.28; pads[16].nonlinearCoupling = 0.35;
    pads[16].morphEnabled = 1.0; pads[16].morphStart = 0.80;
    pads[16].morphEnd = 0.96; pads[16].morphDuration = 0.30;
    pads[16].strikePosition = 0.32;
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

    // Hand Clap (3, NEW) -- multi-burst ClapExciter flam, big-room tail.
    pads[3].exciterType = ExciterType::Clap;
    pads[3].bodyModel   = BodyModelType::NoiseBody;
    pads[3].material = 0.70; pads[3].size = 0.25; pads[3].decay = 0.10;
    pads[3].level = 0.80;
    pads[3].noiseLayerMix = 0.70; pads[3].noiseLayerCutoff = 0.60;
    pads[3].noiseLayerResonance = 0.20; pads[3].noiseLayerDecay = 0.62;
    pads[3].noiseLayerColor = 0.60;
    pads[3].noiseLayerGain = 2.2;
    pads[3].clickLayerMix = 0.25; pads[3].clickLayerContactMs = 0.15;
    pads[3].clickLayerBrightness = 0.50;
    pads[3].modeScatter = 0.35; pads[3].modeStretch = 0.60;
    pads[3].bodyDampingB1 = 0.65; pads[3].bodyDampingB3 = 0.30;
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
    // Snare-body fix (INVESTIGATION-snare-body): Impulse strike (was NoiseBurst)
    // so the warm wood body rings instead of reading as filtered noise.
    pads[2].exciterType = ExciterType::Impulse;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.32; pads[2].size = 0.62; pads[2].decay = 0.18;  // short tat (b1 override dominates)
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
    // Snare-body fix: bright, loud wire buzz lifted to near-body level via
    // noiseLayerGain; carries the tail past the short wood-body tat. Click kept
    // a touch darker (0.78) for the warm wood character.
    pads[2].noiseLayerMix    = 0.65; pads[2].noiseLayerCutoff = 0.78;
    pads[2].noiseLayerResonance = 0.12;
    pads[2].noiseLayerColor  = 0.70; pads[2].noiseLayerDecay = 0.55;
    pads[2].noiseLayerGain   = 6.2;
    pads[2].wireCoupling     = 0.45;  // buzz tracks head: dies with body, chokes on note-off
    pads[2].clickLayerMix    = 0.55; pads[2].clickLayerContactMs = 0.08;
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
    pads[2].bodyDampingB1 = 0.60; pads[2].bodyDampingB3 = 0.45;  // snare-body fix: ~30 s^-1 short tat + moderate HF damping (warm wood)
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
    // Tom-depth Fix E: END(Lo) = natural f0 round(500*0.1^size) for sizes
    // 0.72..0.35 = 95,120,141,166,190,223 Hz; START(Hi) ~6% above (glide DOWN);
    // low constant b1 for a long fundamental; graded glide time added below.
    const double tomB1[]        = {0.055, 0.055, 0.055, 0.055, 0.055, 0.055};
    const double tomPitchHi[]   = {101, 127, 149, 176, 201, 236};
    const double tomPitchLo[]   = { 95, 120, 141, 166, 190, 223};
    const double tomPitchTime[] = {0.50, 0.44, 0.38, 0.32, 0.28, 0.24};  // 250..120 ms
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
        pads[p].tsPitchEnvTime  = tomPitchTime[i];
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
        pads[p].bodyDampingB1 = tomB1[i]; pads[p].bodyDampingB3 = 0.15;  // Fix E
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
    pads[13].bodyDampingB3 = 0.00008; pads[13].bodyDampingB1 = 0.060;
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
    // secondary shell OFF (adds unscaled RMS -> clip). Levels restored
    // post-N-1 (old cut was to dodge the fixed clip).
    // strikePosition 0.83: mapper measures from CENTER (r/a = pos*0.9);
    // Rossing edge strike balances (1,1) pitch mode vs pitchless (0,1).
    // airLoading 1.00: only full loading lands the (m,1) series on the
    // 1.5/2/2.44/2.9 table (0.80 left it 37-74 cents flat).
    pads[0].exciterType = ExciterType::Mallet;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.32; pads[0].size = 0.90; pads[0].decay = 0.82;
    pads[0].strikePosition = 0.83;
    pads[0].level = 0.84;
    pads[0].tsPitchEnvStart = toLogNorm(180);
    pads[0].tsPitchEnvEnd   = toLogNorm(85);
    pads[0].tsPitchEnvTime  = 0.10;   // 50 ms
    pads[0].tsPitchEnvCurve = 0.35;
    pads[0].airLoading       = 1.00;
    pads[0].couplingStrength = 0.0;
    pads[0].secondaryEnabled = 0.0;
    pads[0].tensionModAmt    = 0.16;
    pads[0].modeScatter      = 0.06;
    pads[0].modeInjectAmount = 0.0;   // inject rings UNDAMPED (no envelope) --
                                      // the flat ~-20 dBFS plateau read as a
                                      // synth bass note; archetype mandates 0
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
    pads[2].strikePosition = 0.35;   // archetype: slightly off-center
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
    pads[2].noiseLayerGain   = 6.2;   // wire reaches snare level (calibrated,
                                      // matches acoustic/rock/vintage snares)
    pads[2].wireCoupling     = 0.45;  // buzz tracks head: dies with body,
                                      // chokes on note-off
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
    pads[4].strikePosition = 0.35;   // archetype: r/a ~0.31 off-center
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
    pads[4].bodyDampingB1 = 0.035; pads[4].bodyDampingB3 = 0.55;  // measured:
    // 0.05 rendered t60 1.83 s (b3 HF loss on top of b1); 0.035 -> ~2.4 s
    // concert-bass bloom (0.30 gave a 0.46 s kit-kick thud)
    pads[4].couplingAmount = 0.60;
    pads[4].pan = 0.50;

    // Timpani toms -- the SAME instrument as pad 0, 6 tunings. airLoading
    // 1.00 (matches pad 0; full loading is the (m,1) pitch-fusion trick),
    // strikePosition 0.83 edge strike (lifts the pitch-carrying (1,1) mode),
    // modeInject 0 (rings undamped -- the old 0.10-0.12 plateau read as a
    // synth bass note), level 0.82, per-mode decaySkew tilt + pan row sweep.
    // Pad 12 (MIDI 48, GM Hi-Mid Tom) is a 6th tuning inserted between 11
    // and 14 so GM tom fills run clean across 41/43/45/47/48/50 (the
    // triangle that used to sit on 48 moved to pad 18 / MIDI 54).
    const int    timpaniPads[]   = {5, 7, 9, 11, 12, 14};
    const double timpaniMat[]    = {0.32, 0.36, 0.40, 0.44, 0.46, 0.48};
    const double timpaniSize[]   = {0.88, 0.82, 0.75, 0.68, 0.64, 0.60};
    const double timpaniHi[]     = {180, 220, 280, 350, 395, 440};
    const double timpaniLo[]     = {80, 100, 130, 165, 190, 215};
    const double timpaniDecay[]  = {0.80, 0.72, 0.65, 0.58, 0.54, 0.50};
    const double timpaniB1[]     = {0.10, 0.12, 0.14, 0.17, 0.19, 0.21};
    const double timpaniSkew[]   = {0.45, 0.47, 0.50, 0.53, 0.54, 0.55};
    const double timpaniMacro[]  = {0.85, 0.80, 0.75, 0.70, 0.675, 0.65};
    const double timpaniPan[]    = {0.38, 0.43, 0.47, 0.52, 0.545, 0.57};
    for (int i = 0; i < 6; ++i) {
        const int p = timpaniPads[i];
        pads[p].exciterType = ExciterType::Mallet;
        pads[p].bodyModel = BodyModelType::Membrane;
        pads[p].material = timpaniMat[i];
        pads[p].size = timpaniSize[i];
        pads[p].decay = timpaniDecay[i];
        pads[p].level = 0.82;
        pads[p].strikePosition = 0.83;
        pads[p].tsPitchEnvStart = toLogNorm(timpaniHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(timpaniLo[i]);
        pads[p].tsPitchEnvTime  = 0.12;
        pads[p].tsPitchEnvCurve = 0.35;
        pads[p].airLoading       = 1.00;
        pads[p].modeScatter      = 0.08;
        pads[p].modeInjectAmount = 0.0;
        pads[p].decaySkew        = timpaniSkew[i];
        pads[p].couplingStrength = 0.0;
        pads[p].secondaryEnabled = 0.0;
        pads[p].tensionModAmt    = 0.15;
        pads[p].noiseLayerMix    = 0.12; pads[p].noiseLayerCutoff = 0.40;
        pads[p].noiseLayerColor  = 0.12; pads[p].noiseLayerDecay  = 0.20;  // Brown, matches pad 0
        pads[p].clickLayerMix    = 0.32; pads[p].clickLayerContactMs = 0.28;
        pads[p].clickLayerBrightness = 0.32;
        pads[p].bodyDampingB1 = timpaniB1[i]; pads[p].bodyDampingB3 = 0.10;
        pads[p].macroBodySize = timpaniMacro[i];
        pads[p].macroComplexity = 0.55;
        pads[p].couplingAmount = 0.55;
        pads[p].pan = timpaniPan[i];
    }

    // Triangle (18, MIDI 54; moved off GM Hi-Mid Tom 48 so tom fills don't
    // ding a triangle) -- Bell -> Shell (free-free bar 1:2.757:5.404).
    pads[18].exciterType = ExciterType::Impulse;
    pads[18].bodyModel = BodyModelType::Shell;
    pads[18].material = 0.95; pads[18].size = 0.085; pads[18].decay = 0.85;
    pads[18].level = 0.72;   // archetype value (0.65 was clip-dodging leftover)
    pads[18].strikePosition = 0.18;   // free-end antinode
    pads[18].modeStretch = 0.62; pads[18].decaySkew = 0.40;
    pads[18].modeScatter = 0.12;
    pads[18].clickLayerMix = 0.95; pads[18].clickLayerContactMs = 0.10;
    pads[18].clickLayerBrightness = 0.97;
    pads[18].noiseLayerMix = 0.0;
    pads[18].airLoading = 0.0;
    pads[18].bodyDampingB3 = 0.0;    // 0.02 killed the ~6.7 kHz shimmer in 7 ms
    pads[18].bodyDampingB1 = 0.02;   // T60 ~5.8 s ring (0.30 choked it at 0.46 s)
    pads[18].outputBus = 1;
    pads[18].macroBrightness = 0.95; pads[18].macroComplexity = 0.30;
    pads[18].pan = 0.62;

    // Suspended cymbal -- struck (6), choke group 1.
    // decay 0.85: washBlend = clamp((decay-0.5)*2) was 0 at the old 0.50 --
    // the internal wash never engaged, leaving a 57 ms sizzle behind a bare
    // 948 Hz modal ping ("a tone"). b1 0.05: the old 0.45 (22.6 1/s) choked
    // the ring to a 0.3 s tonk; a struck sus cymbal rings 3-8 s. HP at
    // ~1.45 kHz attenuates the fundamental ping.
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::NoiseBody;
    pads[6].material = 0.95; pads[6].size = 0.35; pads[6].decay = 0.85;
    pads[6].level = 0.62; pads[6].chokeGroup = 1;
    pads[6].strikePosition = 0.85;   // edge strike, m-rich plate families
    pads[6].modeStretch = 0.70; pads[6].decaySkew = 0.58;
    pads[6].nonlinearCoupling = 0.35;
    pads[6].tsFilterType = FilterType::HP;
    pads[6].tsFilterCutoff = 0.62;   // ~1.45 kHz
    pads[6].noiseLayerMix = 0.80; pads[6].noiseLayerCutoff = 0.88;
    pads[6].noiseLayerColor = 0.85; pads[6].noiseLayerDecay = 0.85;  // Violet, ~1.0 s
    // mix 0.80 / cutoff 0.88: measured flatnessHigh 0.26 at mix 0.65 --
    // modal lines still outweighed the wash in 2-10 kHz
    pads[6].clickLayerMix = 0.20;
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.60;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.05;
    pads[6].pan = 0.65;

    // Suspended cymbal -- pedal (8), short choke. Chick character comes
    // from the violet layer (body correctly stays below the wash gate).
    pads[8] = pads[6];
    pads[8].decay = 0.12; pads[8].noiseLayerDecay = 0.10;
    pads[8].noiseLayerMix = 0.75;
    pads[8].bodyDampingB1 = 0.30;

    // Suspended cymbal roll (10) -- morph swell, aux1, choke 1
    pads[10].exciterType = ExciterType::NoiseBurst;
    pads[10].bodyModel = BodyModelType::NoiseBody;
    pads[10].material = 0.95; pads[10].size = 0.42; pads[10].decay = 0.95;
    pads[10].level = 0.72; pads[10].chokeGroup = 1;
    pads[10].morphEnabled = 1.0;
    pads[10].morphStart = 0.55; pads[10].morphEnd = 0.95;
    pads[10].morphDuration = 0.85; pads[10].morphCurve = 0.4;  // ~1.7 s
    pads[10].strikePosition = 0.90;  // rolled edge contact
    pads[10].modeStretch = 0.70;     // archetype spread (B ~0.0049)
    pads[10].modeScatter = 0.65; pads[10].airLoading = 0.0;
    pads[10].nonlinearCoupling = 0.20;
    pads[10].bodyDampingB3 = 0.0; pads[10].bodyDampingB1 = 0.30;
    pads[10].noiseLayerMix = 0.85; pads[10].noiseLayerCutoff = 0.88;  // wash-forward
    // (measured flatnessHigh 0.30 at mix 0.65 -- a roll is mostly wash)
    pads[10].noiseLayerColor = 0.78; pads[10].noiseLayerDecay = 0.95;
    pads[10].clickLayerMix = 0.0;
    pads[10].decaySkew = 0.55;
    pads[10].outputBus = 1;
    pads[10].pan = 0.65;

    // Gong / Tam-Tam (13) -- Plate + stretch + bloom (secondary OFF by design).
    // Plate: 48 dense inharmonic Chladni modes vs Bell's 16 quasi-harmonic
    // lines (Bell topped out at ~1.36 kHz -> measured ZERO energy 2-8 kHz).
    // size 0.92 -> Plate f0 ~96 Hz (gong band; Plate has no sub-fundamental
    // hum partials, so size compensates). Bright violet wash at ~7 kHz per
    // the archetype; ~0.4 s morph bloom; inject 0 (rings undamped).
    pads[13].exciterType = ExciterType::Mallet;
    pads[13].bodyModel = BodyModelType::Plate;
    pads[13].material = 0.85; pads[13].size = 0.95; pads[13].decay = 0.95;
    // size 0.95: measured lowest partial 113.9 Hz at 0.92 (stretch warp on
    // top of the f0 law); 0.95 lands it in the 90-105 Hz gong band.
    pads[13].level = 0.82;
    pads[13].strikePosition = 0.60;
    pads[13].modeStretch = 0.62;
    pads[13].nonlinearCoupling = 0.80;   // bloom
    pads[13].modeInjectAmount = 0.0;
    pads[13].morphEnabled = 1.0;
    pads[13].morphStart = 0.85; pads[13].morphEnd = 0.55;
    pads[13].morphDuration = 0.20; pads[13].morphCurve = 0.5;  // ~0.4 s
    pads[13].modeScatter = 0.65; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0;
    pads[13].bodyDampingB1 = 0.02;  // measured: 0.30 (15.1 1/s) choked the
    // tam-tam to a 0.63 s thud; 0.02 -> ~5.8 s ring (real tam-tams ring 10+ s
    // but the coupled bloom keeps feeding the tail)
    pads[13].clickLayerMix = 0.30; pads[13].clickLayerContactMs = 0.22;
    pads[13].clickLayerBrightness = 0.30;
    pads[13].noiseLayerMix = 0.45; pads[13].noiseLayerCutoff = 0.85;
    // mix 0.45: measured 2-8 kHz fraction 0.0002 at 0.20 -- the violet wash
    // is the gong's only HF source (Plate modes top out low)
    pads[13].noiseLayerColor = 0.90; pads[13].noiseLayerDecay = 0.92;  // Violet
    pads[13].decaySkew = 0.65;
    pads[13].tensionModAmt = 0.0;   // Bell: tensionMod is a no-op
    pads[13].outputBus = 1;
    pads[13].macroBodySize = 0.95; pads[13].macroComplexity = 0.85;
    pads[13].couplingAmount = 0.95;
    pads[13].pan = 0.50;

    // Ride Cymbal (15, NEW role on the GM ride slot) -- Bell/NoiseBurst.
    // decay 0.62: the old 0.90 let the bare 16-line Bell ladder ring 8+ s
    // against a <1 s wash -- that inverted ping/shimmer balance was "a
    // tone". Wash pushed to the 2 s layer ceiling. DOCUMENTED DEVIATION
    // from the ride archetype's decay (kit doc records it).
    pads[15].exciterType = ExciterType::NoiseBurst;
    pads[15].bodyModel = BodyModelType::Bell;
    pads[15].material = 0.95; pads[15].size = 0.30; pads[15].decay = 0.62;
    pads[15].level = 0.72;
    pads[15].strikePosition = 0.18;
    pads[15].modeStretch = 0.45; pads[15].decaySkew = 0.70;
    pads[15].nonlinearCoupling = 0.18; pads[15].modeScatter = 0.55;
    pads[15].bodyDampingB1 = 0.16; pads[15].bodyDampingB3 = 0.0;
    pads[15].noiseLayerMix = 0.70; pads[15].noiseLayerCutoff = 0.90;
    pads[15].noiseLayerColor = 0.90; pads[15].noiseLayerDecay = 1.00;  // Violet, 2 s ceiling
    // mix 0.70: measured flatnessHigh 0.012 at 0.45 -- the Bell ladder still
    // buried the shimmer bed (interim voicing until DSP item D6)
    pads[15].clickLayerMix = 0.45; pads[15].clickLayerContactMs = 0.25;
    pads[15].clickLayerBrightness = 0.82;
    pads[15].airLoading = 0.0;
    pads[15].outputBus = 1;
    pads[15].macroBrightness = 0.85;
    pads[15].pan = 0.62;

    // Crotales hi (17) -- Bell/Mallet.
    // strikePosition 0.50: theta = pos*pi/2 -> hum AND prime hit the
    // cos(pi/2)=0 clamp (-26 dB) while the nominal (m=4) stays full; the
    // old 0.10 put the m=2 hum near max, so the "fundamental" was a
    // sub-octave hum. decaySkew 0.35 (phys -0.30) lifts the audible
    // octave/upper partials -- NEGATIVE skew brightens (canonical
    // semantics; several docs carried the inverted sign). HP at ~780 Hz
    // removes the residual hum after the body+layers sum. b1 0.04:
    // T60 ~3.2 s principal ring (0.30 choked it at 0.46 s).
    pads[17].exciterType = ExciterType::Mallet;
    pads[17].bodyModel = BodyModelType::Bell;
    pads[17].material = 0.92; pads[17].size = 0.12; pads[17].decay = 0.85;
    pads[17].level = 0.72;
    pads[17].strikePosition = 0.50;
    pads[17].decaySkew = 0.35;
    pads[17].tsFilterType = FilterType::HP;
    pads[17].tsFilterCutoff = 0.53;   // ~780 Hz
    pads[17].clickLayerMix = 0.42; pads[17].clickLayerContactMs = 0.20;
    pads[17].clickLayerBrightness = 0.85;
    pads[17].noiseLayerMix = 0.0;
    pads[17].airLoading = 0.0;
    pads[17].bodyDampingB3 = 0.0; pads[17].bodyDampingB1 = 0.04;
    pads[17].outputBus = 1;
    pads[17].macroBrightness = 0.85;
    pads[17].pan = 0.68;

    // Crotales lo (16, MIDI 52; moved off the GM Splash slot 55) -- larger
    // constituent bells, adjacent to crotales hi.
    pads[16] = pads[17];
    pads[16].size = 0.22; pads[16].material = 0.88;
    pads[16].pan = 0.32;

    // Splash-role cymbal (19, MIDI 55 = GM Splash) -- short bright copy of
    // the fixed suspended cymbal so GM content lands on a splash, not a
    // chime. Shares choke group 1 with the sus-cymbal set.
    pads[19] = pads[6];
    pads[19].decay = 0.25; pads[19].size = 0.15;
    pads[19].bodyDampingB1 = 0.18;  // measured: inherited 0.05 rang 2.5 s;
                                    // 0.18 (9.2 1/s) -> ~0.75 s splash
    pads[19].pan = 0.35;

    // Bell Tree (1, NEW) -- Bell/NoiseBurst cascade with morph dim.
    // decaySkew 0.22 (phys -0.56): NEGATIVE skew lifts the upper partials
    // (the bright top of the cascade). The old 0.78 had the sign INVERTED
    // -- it boosted the sub-octave hum x2.17 and cut the 12x partial to
    // x0.25, an 18.8 dB tilt the wrong way. Violet sizzle now passes a
    // ~13 kHz LP (unset cutoff defaulted to a 632 Hz LP that muted it).
    pads[1].exciterType = ExciterType::NoiseBurst;
    pads[1].bodyModel = BodyModelType::Bell;
    pads[1].material = 0.93; pads[1].size = 0.10; pads[1].decay = 0.60;
    pads[1].level = 0.70;   // archetype (generator previously left default)
    pads[1].strikePosition = 0.30;
    pads[1].modeStretch = 0.55; pads[1].decaySkew = 0.22;
    pads[1].modeScatter = 0.55;
    pads[1].bodyDampingB1 = 0.05; pads[1].bodyDampingB3 = 0.0;  // ~2.6 s shimmer bed
    pads[1].noiseLayerMix = 0.42; pads[1].noiseLayerColor = 0.90;  // Violet
    pads[1].noiseLayerCutoff = 0.92;  // ~13 kHz
    pads[1].noiseLayerDecay = 0.85;   // ~1.1 s sizzle tail
    pads[1].clickLayerMix = 0.55; pads[1].clickLayerBrightness = 0.92;
    pads[1].morphEnabled = 1.0;
    pads[1].morphStart = 0.85; pads[1].morphEnd = 0.55;
    pads[1].morphDuration = 0.50; pads[1].morphCurve = 0.0;  // ~1.1 s linear
    pads[1].airLoading = 0.0;
    pads[1].outputBus = 1;
    pads[1].pan = 0.55;

    // Tubular bell (23, MIDI 59; moved off the GM Clap slot 39 -- a chime
    // under a "Clap" label was the user-facing mismatch) -- String/Mallet
    // (modal axes inert no-ops on String).
    pads[23].exciterType = ExciterType::Mallet;
    pads[23].bodyModel = BodyModelType::String;
    pads[23].material = 0.85; pads[23].size = 0.55; pads[23].decay = 0.92;
    pads[23].level = 0.78;
    pads[23].strikePosition = 0.30;
    pads[23].modeStretch = 0.50;
    pads[23].clickLayerMix = 0.40; pads[23].clickLayerContactMs = 0.20;
    pads[23].clickLayerBrightness = 0.65;
    pads[23].noiseLayerMix = 0.10;
    pads[23].airLoading = 0.0;
    pads[23].bodyDampingB1 = 0.30; pads[23].bodyDampingB3 = 0.20;
    pads[23].decaySkew = 0.55;
    pads[23].outputBus = 1;
    pads[23].pan = 0.58;

    // Castanets (3, MIDI 39 = GM Clap slot) -- short dry Shell/Impulse
    // stand-in (castanets-on-39 is orchestral GS/XG tradition; a clap has
    // no orchestral role). Shell f0 ~1 kHz clack, hard-damped, bright tick.
    pads[3].exciterType = ExciterType::Impulse;
    pads[3].bodyModel = BodyModelType::Shell;
    pads[3].material = 0.30; pads[3].size = 0.18; pads[3].decay = 0.10;
    pads[3].level = 0.75;
    pads[3].strikePosition = 0.25;
    pads[3].modeScatter = 0.15;
    pads[3].clickLayerMix = 0.75; pads[3].clickLayerContactMs = 0.06;
    pads[3].clickLayerBrightness = 0.85;
    pads[3].noiseLayerMix = 0.0;
    pads[3].airLoading = 0.0;
    pads[3].bodyDampingB1 = 0.55; pads[3].bodyDampingB3 = 0.20;
    pads[3].pan = 0.45;

    // Kit globals: headroom recovered post-N-1, so coupling/polyphony
    // restored (long timpani/gong/chime tails overlap; sympathetic
    // timpani resonance back on). Capped at the plugin's [4,16] range.
    k.opts.maxPolyphony    = 16;
    k.opts.globalCoupling  = 0.28;
    k.opts.snareBuzz       = 0.20;
    k.opts.tomResonance    = 0.35;
    k.opts.couplingDelayMs = 1.6;
    // 21 sounding pads (GM-remapped: timpani row fills 41-50 incl. 48,
    // castanets on the Clap slot 39, splash-role on 55, triangle on 54,
    // crotales pair on 52/53, tubular bell on 59); pads 20-22 and 24-31
    // disabled.
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 23};
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
    pads[0].tsPitchEnvCurve = 0.15;
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
    pads[0].pan = 0.5;

    // 909 snare (redesigned: dual tonal layers + long noise tail. The
    // primary membrane is the boop body with moderate damping (decays in
    // ~150 ms, NOT a steady sine and NOT instant); secondary metallic body
    // adds the high "ping" the 909 is famous for; noise extends longest
    // for the snare-wire shhh tail.)
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.58; pads[2].size = 0.398; pads[2].decay = 0.55;
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
    pads[2].nonlinearCoupling = 0.0; pads[2].modeInjectAmount = 0.0;
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
    pads[2].pan = 0.5;

    // Rim Shot (4) — Shell/Impulse free-free bar (was Plate; post-audit Shell
    // is the correct inharmonic metal-bar body). Jagged inharmonic via
    // modeScatter+modeStretch; free-end antinode strike.
    pads[4].exciterType = ExciterType::Impulse;
    pads[4].bodyModel = BodyModelType::Shell;
    pads[4].material = 0.85; pads[4].size = 0.34; pads[4].decay = 0.18;
    pads[4].strikePosition = 0.15;
    pads[4].level = 0.9;
    pads[4].clickLayerMix = 0.94; pads[4].clickLayerContactMs = 0.20;
    pads[4].clickLayerBrightness = 0.94;
    pads[4].modeScatter = 0.40; pads[4].modeStretch = 0.45;
    pads[4].noiseLayerMix = 0.20; pads[4].noiseLayerColor = 0.85;
    pads[4].bodyDampingB1 = 0.50; pads[4].bodyDampingB3 = 0.30;
    pads[4].macroPunch = 0.92; pads[4].pan = 0.42;

    // Hi-hats (6 closed / 8 pedal / 10 open) — NoiseBody/NoiseBurst, choke 1.
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::NoiseBody;
    pads[6].material = 0.95; pads[6].size = 0.10; pads[6].decay = 0.07;
    pads[6].level = 0.72; pads[6].chokeGroup = 1;
    pads[6].noiseBurstDuration = 0.10; // ~3.3 ms sharp 909 tss attack
    pads[6].noiseLayerMix = 0.85; pads[6].noiseLayerCutoff = 0.95;
    pads[6].noiseLayerResonance = 0.12; // Q~0.86, flat broadband (no whistle)
    pads[6].noiseLayerColor = 0.95; pads[6].noiseLayerDecay = 0.07;
    pads[6].clickLayerMix = 0.0; // documented kit deviation: cleanest tss
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.0;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.65;
    pads[6].pan = 0.58;

    pads[8] = pads[6];
    pads[8].decay = 0.04; pads[8].noiseLayerDecay = 0.05;
    pads[8].bodyDampingB1 = 0.78;

    pads[10] = pads[6];
    pads[10].decay = 0.42; pads[10].noiseLayerDecay = 0.40;
    pads[10].bodyDampingB1 = 0.30; pads[10].pan = 0.60;

    // 909 toms collapsed 6 -> 3 real voices (Hi 5 / Mid 7 / Low 9). Tom pitch-env
    // norms corrected so settle pitches actually reach the stated low Hz.
    // Hi Tom (5)
    pads[5].exciterType = ExciterType::Impulse;
    pads[5].bodyModel = BodyModelType::Membrane;
    pads[5].material = 0.50; pads[5].size = 0.28; pads[5].decay = 0.18;
    pads[5].level = 0.78;
    pads[5].tsPitchEnvStart = toLogNorm(590);
    pads[5].tsPitchEnvEnd   = toLogNorm(210);
    pads[5].tsPitchEnvTime  = 0.10; pads[5].tsPitchEnvCurve = 0.15;
    pads[5].airLoading = 0.0; pads[5].couplingStrength = 0.0;
    pads[5].secondaryEnabled = 0.0; pads[5].tensionModAmt = 0.20;
    pads[5].noiseLayerMix = 0.05;
    pads[5].clickLayerMix = 0.32; pads[5].clickLayerContactMs = 0.15;
    pads[5].bodyDampingB1 = 0.42; pads[5].bodyDampingB3 = 0.18;
    pads[5].macroPunch = 0.65; pads[5].pan = 0.64;

    // Mid Tom (7)
    pads[7].exciterType = ExciterType::Impulse;
    pads[7].bodyModel = BodyModelType::Membrane;
    pads[7].material = 0.34; pads[7].size = 0.48; pads[7].decay = 0.25;
    pads[7].level = 0.78;
    pads[7].tsPitchEnvStart = toLogNorm(350);
    pads[7].tsPitchEnvEnd   = toLogNorm(120);
    pads[7].tsPitchEnvTime  = 0.10; pads[7].tsPitchEnvCurve = 0.15;
    pads[7].airLoading = 0.0; pads[7].couplingStrength = 0.0;
    pads[7].secondaryEnabled = 0.0; pads[7].tensionModAmt = 0.20;
    pads[7].noiseLayerMix = 0.05;
    pads[7].clickLayerMix = 0.32; pads[7].clickLayerContactMs = 0.15;
    pads[7].bodyDampingB1 = 0.36; pads[7].bodyDampingB3 = 0.18;
    pads[7].macroPunch = 0.65; pads[7].pan = 0.50;

    // Low Tom (9)
    pads[9].exciterType = ExciterType::Impulse;
    pads[9].bodyModel = BodyModelType::Membrane;
    pads[9].material = 0.20; pads[9].size = 0.65; pads[9].decay = 0.32;
    pads[9].level = 0.78;
    pads[9].tsPitchEnvStart = toLogNorm(240);
    pads[9].tsPitchEnvEnd   = toLogNorm(85);
    pads[9].tsPitchEnvTime  = 0.10; pads[9].tsPitchEnvCurve = 0.15;
    pads[9].airLoading = 0.0; pads[9].couplingStrength = 0.0;
    pads[9].secondaryEnabled = 0.0; pads[9].tensionModAmt = 0.20;
    pads[9].noiseLayerMix = 0.05;
    pads[9].clickLayerMix = 0.32; pads[9].clickLayerContactMs = 0.15;
    pads[9].bodyDampingB1 = 0.30; pads[9].bodyDampingB3 = 0.18;
    pads[9].macroPunch = 0.65; pads[9].pan = 0.36;

    // Clave / Woodblock (11) — Shell/Impulse, dry-wood HF damping override.
    pads[11].exciterType = ExciterType::Impulse;
    pads[11].bodyModel = BodyModelType::Shell;
    pads[11].material = 0.85; pads[11].size = 0.0; pads[11].decay = 0.12;
    pads[11].strikePosition = 0.12; pads[11].level = 0.80;
    pads[11].bodyDampingB3 = 0.70; // dry-wood high-mode damping override
    pads[11].clickLayerMix = 0.55; pads[11].clickLayerContactMs = 0.15;
    pads[11].clickLayerBrightness = 0.82;
    pads[11].noiseLayerMix = 0.0; pads[11].pan = 0.30;

    // Cowbell (12) — Bell/FMImpulse, detuned-fifth clang.
    pads[12].exciterType = ExciterType::FMImpulse;
    pads[12].bodyModel = BodyModelType::Bell;
    pads[12].material = 0.78; pads[12].size = 0.22; pads[12].decay = 0.30;
    pads[12].strikePosition = 0.30; pads[12].level = 0.75;
    pads[12].fmRatio = 0.50; // modulator ratio 2.5
    pads[12].clickLayerMix = 0.55; pads[12].clickLayerContactMs = 0.10;
    pads[12].clickLayerBrightness = 0.72;
    pads[12].noiseLayerMix = 0.10; pads[12].noiseLayerColor = 0.40;
    pads[12].noiseLayerCutoff = 0.62; pads[12].noiseLayerDecay = 0.20;
    pads[12].bodyDampingB1 = 0.32; pads[12].bodyDampingB3 = 0.0;
    pads[12].modeStretch = 0.55; pads[12].modeScatter = 0.20;
    pads[12].decaySkew = 0.42;
    pads[12].macroBrightness = 0.65; pads[12].pan = 0.66;

    // Crash (13) — NoiseBody/NoiseBurst, full Chladni bloom, aux bus 1.
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel = BodyModelType::NoiseBody;
    pads[13].material = 0.95; pads[13].size = 0.35; pads[13].decay = 0.70;
    pads[13].strikePosition = 0.32; pads[13].level = 0.72;
    pads[13].modeStretch = 0.60; pads[13].modeInjectAmount = 0.0;
    pads[13].nonlinearCoupling = 0.35; pads[13].modeScatter = 0.35;
    pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.00008; pads[13].bodyDampingB1 = 0.060;
    pads[13].noiseLayerMix = 0.50; pads[13].noiseLayerCutoff = 0.90;
    pads[13].noiseLayerColor = 0.92; pads[13].noiseLayerDecay = 0.60;
    pads[13].clickLayerMix = 0.20; pads[13].clickLayerBrightness = 0.82;
    pads[13].outputBus = 1; pads[13].pan = 0.40;

    // Ride (14) — Bell/NoiseBurst, long sustain + decay tilt, aux bus 1.
    pads[14].exciterType = ExciterType::NoiseBurst;
    pads[14].bodyModel = BodyModelType::Bell;
    pads[14].material = 0.95; pads[14].size = 0.30; pads[14].decay = 0.90;
    pads[14].strikePosition = 0.18; pads[14].level = 0.72;
    pads[14].modeStretch = 0.45; pads[14].decaySkew = 0.62;
    pads[14].nonlinearCoupling = 0.18; pads[14].modeScatter = 0.55;
    pads[14].bodyDampingB1 = 0.16; pads[14].bodyDampingB3 = 0.0;
    pads[14].noiseLayerMix = 0.45; pads[14].noiseLayerCutoff = 0.90;
    pads[14].noiseLayerColor = 0.85; pads[14].noiseLayerDecay = 0.78;
    pads[14].clickLayerMix = 0.45; pads[14].clickLayerBrightness = 0.82;
    pads[14].outputBus = 1; pads[14].pan = 0.62;

    // Hand Clap (15) — NoiseBody/Clap, true 909-style multi-burst flam.
    pads[15].exciterType = ExciterType::Clap;
    pads[15].bodyModel = BodyModelType::NoiseBody;
    pads[15].material = 0.70; pads[15].size = 0.25; pads[15].decay = 0.10;
    pads[15].level = 0.78;
    pads[15].noiseLayerMix = 0.70; pads[15].noiseLayerCutoff = 0.60;
    pads[15].noiseLayerResonance = 0.20;
    pads[15].noiseLayerColor = 0.65; pads[15].noiseLayerDecay = 0.55;
    pads[15].noiseLayerGain = 2.2;
    pads[15].clickLayerMix = 0.25; pads[15].clickLayerContactMs = 0.15;
    pads[15].clickLayerBrightness = 0.50;
    pads[15].airLoading = 0.0; pads[15].modeScatter = 0.35;
    pads[15].modeStretch = 0.60;
    pads[15].bodyDampingB1 = 0.65; pads[15].bodyDampingB3 = 0.30;
    pads[15].macroBrightness = 0.65; pads[15].macroComplexity = 0.55;
    pads[15].pan = 0.50;

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
    pads[0].airLoading = 0.0;
    // Woody secondary shell for sub-weight (documented delta; both Enabled
    // AND Coupling>0 required for the shell to sound).
    pads[0].couplingStrength = 0.20; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.50; pads[0].secondaryMaterial = 0.30;
    pads[0].modeInjectAmount = 0.12; // 1/k harmonic body fill
    pads[0].clickLayerMix = 0.40; pads[0].clickLayerContactMs = 0.15;
    pads[0].clickLayerBrightness = 0.45;
    pads[0].noiseLayerMix = 0.06;
    pads[0].bodyDampingB1 = 0.38; pads[0].bodyDampingB3 = 0.30;
    pads[0].macroPunch = 0.55; pads[0].macroBodySize = 0.45;
    pads[0].pan = 0.5;

    // LinnDrum snare (single-layer by design — the archetype's defining
    // identity is the thin/dry sample-machine snare; NO secondary metallic
    // shell. A light 1/k modeInject adds a touch of fundamental fill only.)
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
    pads[2].couplingStrength = 0.0; pads[2].secondaryEnabled = 0.0; // single-layer
    pads[2].modeInjectAmount = 0.10; // gentle 1/k fundamental fill only
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.10;
    pads[2].pan = 0.5;

    // CR-78 cowbell (4) — +stretch/scatter/skew, light two-tone modeInject.
    pads[4].exciterType = ExciterType::FMImpulse;
    pads[4].bodyModel = BodyModelType::Bell;
    pads[4].material = 0.78; pads[4].size = 0.22; pads[4].decay = 0.30;
    pads[4].strikePosition = 0.30; pads[4].level = 0.75;
    pads[4].fmRatio = 0.55;
    pads[4].clickLayerMix = 0.30; pads[4].clickLayerBrightness = 0.62;
    pads[4].noiseLayerMix = 0.08;
    pads[4].airLoading = 0.0;
    pads[4].modeStretch = 0.55; pads[4].modeScatter = 0.20; pads[4].decaySkew = 0.42;
    pads[4].modeInjectAmount = 0.10; // documented kit delta (two-tone reinforce)
    pads[4].bodyDampingB3 = 0.0; pads[4].bodyDampingB1 = 0.32;
    pads[4].macroBrightness = 0.65; pads[4].pan = 0.62;

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

    // Toms split into two distinct timbres across the pan arc (audit §4):
    // 5/7/9 stay Mallet (LinnDrum sampled-acoustic, softer attack, lower 3);
    // 11/12/14 become Impulse (CR-78 snappier pulse-triggered synth, hotter
    // click, upper 3). Physics axes stay neutral (clean pitched oscillators).
    const int    tomPads[]  = {5, 7, 9, 11, 12, 14};
    const double tomSizes[] = {0.62, 0.55, 0.48, 0.42, 0.36, 0.30};
    const double tomMat[]   = {0.25, 0.30, 0.36, 0.42, 0.50, 0.58};
    const double tomHi[]    = {220, 270, 330, 400, 480, 570};
    const double tomLo[]    = {90, 110, 135, 165, 200, 240};
    const double tomPan[]   = {0.30, 0.38, 0.46, 0.54, 0.62, 0.70};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        const bool synth = (i >= 3); // upper 3 = CR-78 Impulse synth toms
        pads[p].exciterType = synth ? ExciterType::Impulse : ExciterType::Mallet;
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
        pads[p].clickLayerMix = synth ? 0.42 : 0.32; // hotter click on synth toms
        pads[p].clickLayerBrightness = 0.55;
        pads[p].bodyDampingB1 = 0.32; pads[p].bodyDampingB3 = 0.20;
        pads[p].pan = tomPan[i];
    }

    // Rim Shot (1) — was Cabasa; Shell/Impulse free-free bar (cabasa moves to 16).
    pads[1].exciterType = ExciterType::Impulse;
    pads[1].bodyModel = BodyModelType::Shell;
    pads[1].material = 0.85; pads[1].size = 0.34; pads[1].decay = 0.18;
    pads[1].strikePosition = 0.15; pads[1].level = 0.9;
    pads[1].clickLayerMix = 0.94; pads[1].clickLayerContactMs = 0.20;
    pads[1].clickLayerBrightness = 0.94;
    pads[1].modeScatter = 0.40; pads[1].modeStretch = 0.45;
    pads[1].noiseLayerMix = 0.20; pads[1].noiseLayerColor = 0.85;
    pads[1].bodyDampingB1 = 0.50; pads[1].bodyDampingB3 = 0.30;
    pads[1].macroPunch = 0.92; pads[1].pan = 0.42;

    // Handclap (3) — was Clave; NoiseBody/Clap, tight vintage multi-burst flam.
    pads[3].exciterType = ExciterType::Clap;
    pads[3].bodyModel = BodyModelType::NoiseBody;
    pads[3].material = 0.70; pads[3].size = 0.25; pads[3].decay = 0.10;
    pads[3].strikePosition = 0.30; pads[3].level = 0.78;
    pads[3].noiseLayerMix = 0.70; pads[3].noiseLayerCutoff = 0.60;
    pads[3].noiseLayerResonance = 0.20; pads[3].noiseLayerColor = 0.65;
    pads[3].noiseLayerDecay = 0.52;
    pads[3].noiseLayerGain = 2.2;
    pads[3].clickLayerMix = 0.25; pads[3].clickLayerContactMs = 0.15;
    pads[3].clickLayerBrightness = 0.50;
    pads[3].airLoading = 0.0; pads[3].modeScatter = 0.35;
    pads[3].modeStretch = 0.60;
    pads[3].bodyDampingB1 = 0.65; pads[3].bodyDampingB3 = 0.30;
    pads[3].macroBrightness = 0.65; pads[3].macroComplexity = 0.55;
    pads[3].pan = 0.5;

    // Crash (13) — +stretch/inject/nonlinear bloom, aux bus 1. Keeps the dark
    // CR-78 noise colour.
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel = BodyModelType::NoiseBody;
    pads[13].material = 0.92; pads[13].size = 0.30; pads[13].decay = 0.55;
    pads[13].strikePosition = 0.32; pads[13].level = 0.70;
    pads[13].modeStretch = 0.60; pads[13].modeInjectAmount = 0.0;
    pads[13].nonlinearCoupling = 0.35; pads[13].modeScatter = 0.45;
    pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.00008; pads[13].bodyDampingB1 = 0.060;
    pads[13].noiseLayerMix = 0.55; pads[13].noiseLayerCutoff = 0.78;
    pads[13].noiseLayerColor = 0.62; pads[13].noiseLayerDecay = 0.55;
    pads[13].outputBus = 1; pads[13].pan = 0.42;

    // Ride (15) — NEW; Bell/FMImpulse, long wash + decay tilt, aux bus 1.
    pads[15].exciterType = ExciterType::FMImpulse;
    pads[15].bodyModel = BodyModelType::Bell;
    pads[15].material = 0.95; pads[15].size = 0.30; pads[15].decay = 0.90;
    pads[15].strikePosition = 0.18; pads[15].level = 0.72;
    pads[15].fmRatio = 0.30;
    pads[15].modeStretch = 0.45; pads[15].decaySkew = 0.62;
    pads[15].nonlinearCoupling = 0.18; pads[15].modeScatter = 0.55;
    pads[15].bodyDampingB1 = 0.16; pads[15].bodyDampingB3 = 0.0;
    pads[15].noiseLayerMix = 0.45; pads[15].noiseLayerCutoff = 0.90;
    pads[15].noiseLayerColor = 0.85; pads[15].noiseLayerDecay = 0.78;
    pads[15].clickLayerMix = 0.45; pads[15].clickLayerBrightness = 0.82;
    pads[15].outputBus = 1; pads[15].pan = 0.58;

    // Cabasa (16) — relocated from pad 1; +noise resonance.
    pads[16].exciterType = ExciterType::NoiseBurst;
    pads[16].bodyModel = BodyModelType::NoiseBody;
    pads[16].material = 0.88; pads[16].size = 0.08; pads[16].decay = 0.08;
    pads[16].level = 0.65;
    pads[16].noiseBurstDuration = 0.18;
    pads[16].noiseLayerMix = 0.85; pads[16].noiseLayerCutoff = 0.85;
    pads[16].noiseLayerResonance = 0.16;
    pads[16].noiseLayerColor = 0.75; pads[16].noiseLayerDecay = 0.10;
    pads[16].clickLayerMix = 0.0;
    pads[16].airLoading = 0.0; pads[16].modeScatter = 0.0;
    pads[16].bodyDampingB3 = 0.0; pads[16].bodyDampingB1 = 0.55;
    pads[16].pan = 0.65;

    // Agogo Hi (17) — NEW; Bell/FMImpulse bright Latin bell.
    pads[17].exciterType = ExciterType::FMImpulse;
    pads[17].bodyModel = BodyModelType::Bell;
    pads[17].material = 0.80; pads[17].size = 0.18; pads[17].decay = 0.35;
    pads[17].strikePosition = 0.30; pads[17].level = 0.72;
    pads[17].fmRatio = 0.72; // mod ratio ~3.16, bright hi
    pads[17].clickLayerMix = 0.45; pads[17].clickLayerContactMs = 0.30;
    pads[17].clickLayerBrightness = 0.78;
    pads[17].noiseLayerMix = 0.05;
    pads[17].modeStretch = 0.50; pads[17].modeScatter = 0.20; pads[17].decaySkew = 0.45;
    pads[17].bodyDampingB1 = 0.30; pads[17].bodyDampingB3 = 0.0;
    pads[17].macroBrightness = 0.70; pads[17].pan = 0.38;

    // Tambourine (18) — NEW; NoiseBody/NoiseBurst CR-78 signature jingles.
    pads[18].exciterType = ExciterType::NoiseBurst;
    pads[18].bodyModel = BodyModelType::NoiseBody;
    pads[18].material = 0.95; pads[18].size = 0.20; pads[18].decay = 0.30;
    pads[18].level = 0.68;
    pads[18].noiseBurstDuration = 0.30;
    pads[18].noiseLayerMix = 0.80; pads[18].noiseLayerCutoff = 0.92;
    pads[18].noiseLayerResonance = 0.20;
    pads[18].noiseLayerColor = 0.85; pads[18].noiseLayerDecay = 0.28;
    pads[18].clickLayerMix = 0.30; pads[18].clickLayerContactMs = 0.20;
    pads[18].clickLayerBrightness = 0.80;
    pads[18].airLoading = 0.0; pads[18].modeScatter = 0.40;
    pads[18].bodyDampingB1 = 0.40; pads[18].bodyDampingB3 = 0.0;
    pads[18].pan = 0.65;

    // Agogo Lo (19) — NEW; pair with 17, lower bell.
    pads[19].exciterType = ExciterType::FMImpulse;
    pads[19].bodyModel = BodyModelType::Bell;
    pads[19].material = 0.80; pads[19].size = 0.28; pads[19].decay = 0.38;
    pads[19].strikePosition = 0.30; pads[19].level = 0.72;
    pads[19].fmRatio = 0.55;
    pads[19].clickLayerMix = 0.45; pads[19].clickLayerContactMs = 0.30;
    pads[19].clickLayerBrightness = 0.72;
    pads[19].noiseLayerMix = 0.05;
    pads[19].modeStretch = 0.50; pads[19].modeScatter = 0.20; pads[19].decaySkew = 0.45;
    pads[19].bodyDampingB1 = 0.30; pads[19].bodyDampingB3 = 0.0;
    pads[19].macroBrightness = 0.62; pads[19].pan = 0.60;

    // Conga Hi (20) — NEW; Membrane/Impulse, woody barrel shell + tension.
    pads[20].exciterType = ExciterType::Impulse;
    pads[20].bodyModel = BodyModelType::Membrane;
    pads[20].material = 0.30; pads[20].size = 0.45; pads[20].decay = 0.28;
    pads[20].strikePosition = 0.25; pads[20].level = 0.78;
    pads[20].tsPitchEnvStart = toLogNorm(300);
    pads[20].tsPitchEnvEnd   = toLogNorm(220);
    pads[20].tsPitchEnvTime  = 0.04; pads[20].tsPitchEnvCurve = 0.15;
    pads[20].airLoading = 0.40; pads[20].tensionModAmt = 0.25;
    pads[20].couplingStrength = 0.20; pads[20].secondaryEnabled = 1.0;
    pads[20].secondarySize = 0.45; pads[20].secondaryMaterial = 0.30;
    pads[20].modeScatter = 0.10;
    pads[20].noiseLayerMix = 0.05;
    pads[20].clickLayerMix = 0.35; pads[20].clickLayerContactMs = 0.15;
    pads[20].bodyDampingB1 = 0.32; pads[20].bodyDampingB3 = 0.10;
    pads[20].macroPunch = 0.60; pads[20].pan = 0.40;

    // Claves (21) — relocated; Shell/Impulse, dry-wood HF damping override.
    pads[21].exciterType = ExciterType::Impulse;
    pads[21].bodyModel = BodyModelType::Shell;
    pads[21].material = 0.85; pads[21].size = 0.0; pads[21].decay = 0.12;
    pads[21].strikePosition = 0.12; pads[21].level = 0.80;
    pads[21].bodyDampingB3 = 0.70; // dry-wood high-mode damping
    pads[21].clickLayerMix = 0.55; pads[21].clickLayerContactMs = 0.15;
    pads[21].clickLayerBrightness = 0.82;
    pads[21].noiseLayerMix = 0.0; pads[21].pan = 0.40;

    // Maracas (22) — NEW; NoiseBody/NoiseBurst dark Pink shaker.
    pads[22].exciterType = ExciterType::NoiseBurst;
    pads[22].bodyModel = BodyModelType::NoiseBody;
    pads[22].material = 0.40; pads[22].size = 0.10; pads[22].decay = 0.08;
    pads[22].strikePosition = 0.50; pads[22].level = 0.66;
    pads[22].noiseBurstDuration = 0.20;
    pads[22].noiseLayerMix = 0.85; pads[22].noiseLayerCutoff = 0.70;
    pads[22].noiseLayerResonance = 0.16;
    pads[22].noiseLayerColor = 0.50; pads[22].noiseLayerDecay = 0.08; // Pink (dark)
    pads[22].clickLayerMix = 0.0;
    pads[22].airLoading = 0.0; pads[22].modeScatter = 0.15;
    pads[22].bodyDampingB3 = 0.0; pads[22].bodyDampingB1 = 0.55;
    pads[22].pan = 0.35;

    // Guiro (23) — NEW; NoiseBody/Friction (the kit's only Friction voice).
    pads[23].exciterType = ExciterType::Friction;
    pads[23].bodyModel = BodyModelType::NoiseBody;
    pads[23].material = 0.50; pads[23].size = 0.20; pads[23].decay = 0.35;
    pads[23].strikePosition = 0.30; pads[23].level = 0.70;
    pads[23].frictionPressure = 0.50;
    pads[23].decaySkew = 0.55;
    pads[23].noiseLayerMix = 0.75; pads[23].noiseLayerCutoff = 0.60;
    pads[23].noiseLayerResonance = 0.20;
    pads[23].noiseLayerColor = 0.50; pads[23].noiseLayerDecay = 0.30;
    pads[23].clickLayerMix = 0.10;
    pads[23].airLoading = 0.0; pads[23].modeScatter = 0.20;
    pads[23].bodyDampingB3 = 0.0; pads[23].bodyDampingB1 = 0.42;
    pads[23].pan = 0.55;

    // Conga Lo (24) — NEW; pair with 20, lower barrel.
    pads[24].exciterType = ExciterType::Impulse;
    pads[24].bodyModel = BodyModelType::Membrane;
    pads[24].material = 0.22; pads[24].size = 0.55; pads[24].decay = 0.32;
    pads[24].strikePosition = 0.25; pads[24].level = 0.78;
    pads[24].tsPitchEnvStart = toLogNorm(220);
    pads[24].tsPitchEnvEnd   = toLogNorm(160);
    pads[24].tsPitchEnvTime  = 0.04; pads[24].tsPitchEnvCurve = 0.15;
    pads[24].airLoading = 0.40; pads[24].tensionModAmt = 0.25;
    pads[24].couplingStrength = 0.20; pads[24].secondaryEnabled = 1.0;
    pads[24].secondarySize = 0.50; pads[24].secondaryMaterial = 0.30;
    pads[24].modeScatter = 0.10;
    pads[24].noiseLayerMix = 0.05;
    pads[24].clickLayerMix = 0.35; pads[24].clickLayerContactMs = 0.15;
    pads[24].bodyDampingB1 = 0.30; pads[24].bodyDampingB3 = 0.10;
    pads[24].macroPunch = 0.60; pads[24].pan = 0.58;

    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
    return k;
}

Kit modularWestCoastKit() {
    Kit k{"Modular West Coast", "Electronic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // West Coast kick
    pads[0].exciterType = ExciterType::Feedback;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.25; pads[0].size = 0.85; pads[0].decay = 0.40;
    pads[0].strikePosition = 0.30; pads[0].level = 0.80;
    pads[0].feedbackAmount = 0.45;
    pads[0].tsPitchEnvStart = toLogNorm(220);
    pads[0].tsPitchEnvEnd   = toLogNorm(45);
    pads[0].tsPitchEnvTime  = 0.06;
    pads[0].tsPitchEnvCurve = 0.15;
    pads[0].tsFoldAmount    = 0.30;
    pads[0].airLoading = 0.0; pads[0].modeScatter = 0.0;
    pads[0].modeStretch = 0.45; pads[0].decaySkew = 0.50; // neutral skew per archetype
    pads[0].couplingStrength = 0.20; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.45; pads[0].secondaryMaterial = 0.85;
    pads[0].tensionModAmt = 0.45;
    pads[0].clickLayerMix = 0.30; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.55;
    pads[0].noiseLayerMix = 0.20; pads[0].noiseLayerCutoff = 0.50;
    pads[0].noiseLayerColor = 0.40; pads[0].noiseLayerDecay = 0.30; // Pink
    pads[0].nonlinearCoupling = 0.40;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.30;
    pads[0].macroComplexity = 0.85; pads[0].macroPunch = 0.65;
    pads[0].pan = 0.50;

    // Snare (FM Plate, redesigned: keeps FM character, gains body + click)
    pads[2].exciterType = ExciterType::FMImpulse;
    pads[2].bodyModel = BodyModelType::Plate;
    pads[2].material = 0.45; pads[2].size = 0.55; pads[2].decay = 0.55;
    pads[2].level = 0.95;
    pads[2].fmRatio = 0.55;
    pads[2].modeStretch = 0.55;
    pads[2].modeInjectAmount = 0.12; // FIX: was 0.0 despite plan claim (1/k fill)
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
    pads[2].pan = 0.50;

    // Sub-bell perc (4)
    pads[4].exciterType = ExciterType::FMImpulse;
    pads[4].bodyModel = BodyModelType::Bell;
    pads[4].material = 0.60; pads[4].size = 0.32; pads[4].decay = 0.55;
    pads[4].strikePosition = 0.20; pads[4].level = 0.75;
    pads[4].fmRatio = 0.72; pads[4].feedbackAmount = 0.30; // feedbackAmount inert under FMImpulse (doc only)
    pads[4].modeStretch = 0.50;
    pads[4].nonlinearCoupling = 0.30;
    pads[4].clickLayerMix = 0.45; pads[4].clickLayerBrightness = 0.85;
    pads[4].noiseLayerMix = 0.10;
    pads[4].airLoading = 0.0;
    pads[4].bodyDampingB3 = 0.0; pads[4].bodyDampingB1 = 0.30;
    pads[4].decaySkew = 0.45; pads[4].pan = 0.42;

    // Friction string drone (1) — aux bus 1. Note: modeStretch/decaySkew/b1/b3
    // are set but INERT on String (String bypasses the modal bank).
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
    pads[1].macroComplexity = 0.85; pads[1].pan = 0.30;

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
    pads[6].pan = 0.62;

    pads[8] = pads[6];
    pads[8].decay = 0.05; pads[8].fmRatio = 0.65;

    pads[10] = pads[6];
    pads[10].decay = 0.50; pads[10].fmRatio = 0.55;
    pads[10].noiseLayerDecay = 0.48;

    // Toms (inharmonic plates). decaySkew 0.40 (FIX: was neutral 0.50 — applies
    // the real M-5 per-mode tilt the plan claimed). tensionMod 0.45 is INERT on
    // Plate (Membrane-only); the kerthump comes from the pitch env. Pan sweeps
    // L->R across the row.
    const int    tomPads[] = {5, 7, 9, 11, 12, 14};
    const double tomPan[]  = {0.30, 0.38, 0.46, 0.54, 0.62, 0.70};
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
        pads[p].decaySkew = 0.40;
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
        pads[p].pan = tomPan[i];
    }

    // Crash (13)
    pads[13].exciterType = ExciterType::FMImpulse;
    pads[13].bodyModel = BodyModelType::Bell;
    pads[13].material = 0.92; pads[13].size = 0.45; pads[13].decay = 0.78;
    pads[13].strikePosition = 0.32; pads[13].level = 0.72;
    pads[13].fmRatio = 0.45;
    pads[13].modeStretch = 0.55;
    pads[13].modeScatter = 0.65;
    pads[13].modeInjectAmount = 0.10; // FIX: was unset despite plan claim
    pads[13].nonlinearCoupling = 0.45;
    pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].noiseLayerMix = 0.40; pads[13].noiseLayerCutoff = 0.92;
    pads[13].noiseLayerColor = 0.85; pads[13].noiseLayerDecay = 0.72;
    pads[13].clickLayerMix = 0.35; pads[13].clickLayerBrightness = 0.85;
    pads[13].pan = 0.58;

    // NEW pads: Clap/Rim (3), Ride (15), Splash (16), Hi Sub-Bell (17),
    // Feedback-Plate Drone (18, +in-loop bandpass), Friction-Membrane Drone (19).
    // Pad 3 — Inharmonic-Plate Clap/Rim (Plate/Impulse).
    pads[3].exciterType = ExciterType::Impulse;
    pads[3].bodyModel = BodyModelType::Plate;
    pads[3].material = 0.78; pads[3].size = 0.18; pads[3].decay = 0.12;
    pads[3].level = 0.85;
    pads[3].modeStretch = 0.55; pads[3].modeScatter = 0.45;
    pads[3].nonlinearCoupling = 0.30;
    pads[3].clickLayerMix = 0.90; pads[3].clickLayerContactMs = 0.06;
    pads[3].clickLayerBrightness = 0.92;
    pads[3].noiseLayerMix = 0.30; pads[3].noiseLayerDecay = 0.15;
    pads[3].airLoading = 0.0;
    pads[3].bodyDampingB1 = 0.50; pads[3].bodyDampingB3 = 0.10;
    pads[3].pan = 0.55;

    // Pad 15 — FM-Bell Ride (Bell/FMImpulse, aux bus 1).
    pads[15].exciterType = ExciterType::FMImpulse;
    pads[15].bodyModel = BodyModelType::Bell;
    pads[15].material = 0.90; pads[15].size = 0.38; pads[15].decay = 0.85;
    pads[15].strikePosition = 0.25; pads[15].level = 0.70;
    pads[15].fmRatio = 0.40;
    pads[15].modeStretch = 0.40; pads[15].modeScatter = 0.45; pads[15].decaySkew = 0.45;
    pads[15].nonlinearCoupling = 0.35;
    pads[15].noiseLayerMix = 0.30; pads[15].noiseLayerCutoff = 0.88;
    pads[15].noiseLayerColor = 0.85; pads[15].noiseLayerDecay = 0.70;
    pads[15].clickLayerMix = 0.50; pads[15].clickLayerBrightness = 0.90;
    pads[15].airLoading = 0.0;
    pads[15].bodyDampingB1 = 0.25; pads[15].bodyDampingB3 = 0.0;
    pads[15].outputBus = 1; pads[15].pan = 0.66;

    // Pad 16 — FM-Bell Splash (Bell/FMImpulse).
    pads[16].exciterType = ExciterType::FMImpulse;
    pads[16].bodyModel = BodyModelType::Bell;
    pads[16].material = 0.92; pads[16].size = 0.28; pads[16].decay = 0.35;
    pads[16].level = 0.70;
    pads[16].fmRatio = 0.50;
    pads[16].modeStretch = 0.55; pads[16].modeScatter = 0.65;
    pads[16].nonlinearCoupling = 0.40;
    pads[16].noiseLayerMix = 0.35; pads[16].noiseLayerCutoff = 0.92;
    pads[16].noiseLayerColor = 0.85; pads[16].noiseLayerDecay = 0.28;
    pads[16].clickLayerMix = 0.35; pads[16].clickLayerBrightness = 0.85;
    pads[16].airLoading = 0.0;
    pads[16].bodyDampingB1 = 0.35; pads[16].bodyDampingB3 = 0.0;
    pads[16].pan = 0.34;

    // Pad 17 — Hi Sub-Bell Perc (Bell/FMImpulse).
    pads[17].exciterType = ExciterType::FMImpulse;
    pads[17].bodyModel = BodyModelType::Bell;
    pads[17].material = 0.62; pads[17].size = 0.22; pads[17].decay = 0.40;
    pads[17].strikePosition = 0.20; pads[17].level = 0.72;
    pads[17].fmRatio = 0.65;
    pads[17].modeStretch = 0.45; pads[17].decaySkew = 0.45;
    pads[17].nonlinearCoupling = 0.30;
    pads[17].tsDriveAmount = 0.25;
    pads[17].clickLayerMix = 0.45; pads[17].clickLayerBrightness = 0.85;
    pads[17].noiseLayerMix = 0.10;
    pads[17].airLoading = 0.0;
    pads[17].bodyDampingB1 = 0.30; pads[17].bodyDampingB3 = 0.0;
    pads[17].pan = 0.58;

    // Pad 18 — Feedback-Plate Drone (Plate/Feedback, aux bus 1). The in-loop
    // BANDPASS ToneShaper is the regenerative band-selector that makes the
    // drone evolve rather than squeal (the plan's biggest correctness fix).
    pads[18].exciterType = ExciterType::Feedback;
    pads[18].bodyModel = BodyModelType::Plate;
    pads[18].material = 0.62; pads[18].size = 0.60; pads[18].decay = 0.88;
    pads[18].strikePosition = 0.45; pads[18].level = 0.60;
    pads[18].feedbackAmount = 0.55; pads[18].tsFoldAmount = 0.25;
    pads[18].modeStretch = 0.50; pads[18].modeScatter = 0.35;
    pads[18].modeInjectAmount = 0.15; pads[18].nonlinearCoupling = 0.50;
    pads[18].decaySkew = 0.78;
    pads[18].tsFilterType = FilterType::BP;
    pads[18].tsFilterCutoff = 0.55; pads[18].tsFilterResonance = 0.45;
    pads[18].tsFilterEnvAmount = 0.65; pads[18].tsFilterEnvAttack = 0.45;
    pads[18].tsFilterEnvDecay = 0.60; pads[18].tsFilterEnvSustain = 0.55;
    pads[18].tsFilterEnvRelease = 0.65;
    pads[18].noiseLayerMix = 0.30; pads[18].noiseLayerCutoff = 0.55;
    pads[18].noiseLayerColor = 0.70; pads[18].noiseLayerDecay = 0.85; // White
    pads[18].clickLayerMix = 0.0;
    pads[18].airLoading = 0.0;
    pads[18].bodyDampingB1 = 0.20; pads[18].bodyDampingB3 = 0.20;
    pads[18].outputBus = 1; pads[18].macroComplexity = 0.80; pads[18].pan = 0.72;

    // Pad 19 — Friction-Membrane Drone (Membrane/Friction, aux bus 1). Uses
    // tensionMod (cuica glide) and airLoading, both Membrane-consumed.
    pads[19].exciterType = ExciterType::Friction;
    pads[19].bodyModel = BodyModelType::Membrane;
    pads[19].material = 0.45; pads[19].size = 0.70; pads[19].decay = 0.80;
    pads[19].level = 0.60;
    pads[19].frictionPressure = 0.60;
    pads[19].nonlinearCoupling = 0.50;
    pads[19].tensionModAmt = 0.40; pads[19].airLoading = 0.70;
    pads[19].modeStretch = 0.40; pads[19].modeScatter = 0.18;
    pads[19].morphEnabled = 1.0;
    pads[19].morphStart = 0.40; pads[19].morphEnd = 0.75;
    pads[19].morphDuration = 0.55; pads[19].morphCurve = 0.5;
    pads[19].noiseLayerMix = 0.18; pads[19].noiseLayerColor = 0.12; // Brown
    pads[19].clickLayerMix = 0.0;
    pads[19].bodyDampingB1 = 0.25; pads[19].bodyDampingB3 = 0.20;
    pads[19].outputBus = 1; pads[19].pan = 0.28;

    k.opts.maxPolyphony    = 12;
    k.opts.stealingPolicy  = 1;
    k.opts.globalCoupling  = 0.65;
    k.opts.tomResonance    = 0.55;
    k.opts.couplingDelayMs = 1.4;
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                 15, 16, 17, 18, 19};
    return k;
}

Kit trapModernKit() {
    Kit k{"Trap Modern", "Electronic", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // Sub 808 kick (Membrane/Impulse) — tensionMod glide signature.
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.10; pads[0].size = 0.95; pads[0].decay = 0.65;
    pads[0].strikePosition = 0.30; pads[0].level = 0.92;
    pads[0].tsPitchEnvStart = toLogNorm(250);
    pads[0].tsPitchEnvEnd   = toLogNorm(35);
    pads[0].tsPitchEnvTime  = 0.06; pads[0].tsPitchEnvCurve = 0.15;
    pads[0].tsDriveAmount   = 0.18;
    pads[0].airLoading = 0.0; pads[0].couplingStrength = 0.0;
    pads[0].tensionModAmt = 0.85;
    pads[0].modeInjectAmount = 0.20; // 1/k body weight
    pads[0].clickLayerMix = 0.42; pads[0].clickLayerContactMs = 0.18;
    pads[0].clickLayerBrightness = 0.32;
    pads[0].noiseLayerMix = 0.0;
    pads[0].decaySkew = 0.45;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.30;
    pads[0].macroPunch = 0.95; pads[0].macroBodySize = 0.95;
    pads[0].pan = 0.5;

    // Crispy Snare (Membrane/NoiseBurst). Size 0.56 = f0 ~138 Hz (deliberately
    // deep boomy trap body under the dominant bright noise layer).
    pads[2].exciterType = ExciterType::NoiseBurst;
    pads[2].bodyModel = BodyModelType::Membrane;
    pads[2].material = 0.48; pads[2].size = 0.56; pads[2].decay = 0.52;
    pads[2].strikePosition = 0.45; pads[2].level = 1.0;
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
    pads[2].decaySkew = 0.42; pads[2].nonlinearCoupling = 0.18;
    pads[2].couplingStrength = 0.55; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.62; pads[2].secondaryMaterial = 0.55;
    pads[2].bodyDampingB1 = 0.30; pads[2].bodyDampingB3 = 0.10;
    pads[2].macroBrightness = 0.95; pads[2].macroPunch = 0.85;
    pads[2].macroTightness = 0.85;
    pads[2].pan = 0.5;

    // Rim Shot (4) — Shell/Impulse free-free bar (was Plate; physically-correct).
    pads[4].exciterType = ExciterType::Impulse;
    pads[4].bodyModel = BodyModelType::Shell;
    pads[4].material = 0.85; pads[4].size = 0.34; pads[4].decay = 0.18;
    pads[4].strikePosition = 0.15; pads[4].level = 0.90;
    pads[4].clickLayerMix = 0.94; pads[4].clickLayerContactMs = 0.20;
    pads[4].clickLayerBrightness = 0.94;
    pads[4].modeScatter = 0.40; pads[4].modeStretch = 0.45;
    pads[4].noiseLayerMix = 0.20; pads[4].noiseLayerColor = 0.92;
    pads[4].airLoading = 0.0;
    pads[4].bodyDampingB1 = 0.50; pads[4].bodyDampingB3 = 0.30;
    pads[4].macroPunch = 0.92; pads[4].pan = 0.5;

    // Closed Hat (6) — NoiseBody/NoiseBurst, choke 1.
    pads[6].exciterType = ExciterType::NoiseBurst;
    pads[6].bodyModel = BodyModelType::NoiseBody;
    pads[6].material = 0.92; pads[6].size = 0.10; pads[6].decay = 0.05;
    pads[6].level = 0.72; pads[6].chokeGroup = 1;
    pads[6].noiseLayerMix = 0.85; pads[6].noiseLayerCutoff = 0.95;
    pads[6].noiseLayerColor = 0.95; pads[6].noiseLayerDecay = 0.05;
    pads[6].noiseLayerResonance = 0.10;
    pads[6].clickLayerMix = 0.18; pads[6].clickLayerBrightness = 0.92;
    pads[6].airLoading = 0.0; pads[6].modeScatter = 0.25;
    pads[6].bodyDampingB3 = 0.0; pads[6].bodyDampingB1 = 0.78;
    pads[6].pan = 0.58;

    // Open Hat (10) — NoiseBody/NoiseBurst, choke 1.
    pads[10].exciterType = ExciterType::NoiseBurst;
    pads[10].bodyModel = BodyModelType::NoiseBody;
    pads[10].material = 0.92; pads[10].size = 0.18; pads[10].decay = 0.55;
    pads[10].level = 0.72; pads[10].chokeGroup = 1;
    pads[10].noiseLayerMix = 0.78; pads[10].noiseLayerCutoff = 0.92;
    pads[10].noiseLayerColor = 0.92; pads[10].noiseLayerDecay = 0.50;
    pads[10].modeScatter = 0.30;
    pads[10].bodyDampingB3 = 0.0; pads[10].bodyDampingB1 = 0.30;
    pads[10].pan = 0.55;

    // Tom row (Membrane/Impulse) — 6 graded toms; pitchEnv norms via toLogNorm.
    const int    tomPads[]  = {5, 7, 9, 11, 12, 14};
    const double tomSizes[] = {0.65, 0.55, 0.45, 0.38, 0.35, 0.32};
    const double tomMat[]   = {0.20, 0.28, 0.36, 0.45, 0.50, 0.55};
    const double tomDecay[] = {0.30, 0.27, 0.24, 0.21, 0.19, 0.18};
    const double tomHi[]    = {240, 290, 360, 440, 500, 540};
    const double tomLo[]    = {80, 100, 130, 165, 175, 210};
    const double tomB1[]    = {0.30, 0.33, 0.36, 0.39, 0.42, 0.42};
    const double tomPan[]   = {0.38, 0.44, 0.50, 0.56, 0.62, 0.38};
    for (int i = 0; i < 6; ++i) {
        const int p = tomPads[i];
        pads[p].exciterType = ExciterType::Impulse;
        pads[p].bodyModel = BodyModelType::Membrane;
        pads[p].material = tomMat[i]; pads[p].size = tomSizes[i];
        pads[p].decay = tomDecay[i]; pads[p].level = 0.78;
        pads[p].tsPitchEnvStart = toLogNorm(tomHi[i]);
        pads[p].tsPitchEnvEnd   = toLogNorm(tomLo[i]);
        pads[p].tsPitchEnvTime  = 0.06;
        pads[p].tsPitchEnvCurve = 0.15;
        pads[p].tsDriveAmount   = 0.18;
        pads[p].airLoading = 0.0;
        pads[p].tensionModAmt = 0.55;
        pads[p].modeInjectAmount = 0.15; // 1/k body weight
        pads[p].noiseLayerMix = 0.05;
        pads[p].clickLayerMix = 0.45; pads[p].clickLayerBrightness = 0.78;
        pads[p].bodyDampingB1 = tomB1[i]; pads[p].bodyDampingB3 = 0.30;
        pads[p].macroPunch = 0.78; pads[p].pan = tomPan[i];
    }

    // Trap Clave (1) — Shell/Impulse free-free bar (was Bell; corrected). b3
    // kept 0 as a flagged synthetic-trap-clave choice (brighter ring; the
    // acoustically-woody alternative is b3 0.70).
    pads[1].exciterType = ExciterType::Impulse;
    pads[1].bodyModel = BodyModelType::Shell;
    pads[1].material = 0.85; pads[1].size = 0.10; pads[1].decay = 0.15;
    pads[1].strikePosition = 0.12; pads[1].level = 0.78;
    pads[1].clickLayerMix = 0.85; pads[1].clickLayerBrightness = 0.95;
    pads[1].clickLayerContactMs = 0.06;
    pads[1].noiseLayerMix = 0.0;
    pads[1].airLoading = 0.0;
    pads[1].bodyDampingB3 = 0.0; pads[1].bodyDampingB1 = 0.42;
    pads[1].pan = 0.40;

    // Trap Cowbell (3) — NEW; Bell/FMImpulse detuned-fifth 808 clang.
    pads[3].exciterType = ExciterType::FMImpulse;
    pads[3].bodyModel = BodyModelType::Bell;
    pads[3].material = 0.78; pads[3].size = 0.22; pads[3].decay = 0.30;
    pads[3].strikePosition = 0.30; pads[3].level = 0.75;
    pads[3].fmRatio = 0.50; // mod ratio 2.5
    pads[3].modeStretch = 0.55; pads[3].decaySkew = 0.42; pads[3].modeScatter = 0.20;
    pads[3].clickLayerMix = 0.55; pads[3].clickLayerContactMs = 0.10;
    pads[3].clickLayerBrightness = 0.72;
    pads[3].noiseLayerMix = 0.10; pads[3].noiseLayerCutoff = 0.62;
    pads[3].noiseLayerColor = 0.40; pads[3].noiseLayerDecay = 0.20; // Pink halo
    pads[3].bodyDampingB1 = 0.32; pads[3].bodyDampingB3 = 0.0;
    pads[3].macroBrightness = 0.65; pads[3].pan = 0.60;

    // 909 Hand Clap (8) — NEW; NoiseBody/Clap, true multi-burst flam.
    pads[8].exciterType = ExciterType::Clap;
    pads[8].bodyModel = BodyModelType::NoiseBody;
    pads[8].material = 0.70; pads[8].size = 0.25; pads[8].decay = 0.10;
    pads[8].level = 0.80;
    pads[8].noiseLayerMix = 0.70; pads[8].noiseLayerCutoff = 0.60;
    pads[8].noiseLayerResonance = 0.20; pads[8].noiseLayerColor = 0.65;
    pads[8].noiseLayerDecay = 0.55;
    pads[8].noiseLayerGain = 2.2;
    pads[8].clickLayerMix = 0.25; pads[8].clickLayerContactMs = 0.15;
    pads[8].clickLayerBrightness = 0.50;
    pads[8].airLoading = 0.0; pads[8].modeScatter = 0.35;
    pads[8].modeStretch = 0.60;
    pads[8].bodyDampingB1 = 0.65; pads[8].bodyDampingB3 = 0.30;
    pads[8].macroBrightness = 0.65; pads[8].macroComplexity = 0.55;
    pads[8].pan = 0.5;

    // Crash 1 (13) — NoiseBody/NoiseBurst, full bloom, aux bus 1.
    pads[13].exciterType = ExciterType::NoiseBurst;
    pads[13].bodyModel = BodyModelType::NoiseBody;
    pads[13].material = 0.95; pads[13].size = 0.32; pads[13].decay = 0.72;
    pads[13].strikePosition = 0.32; pads[13].level = 0.72;
    pads[13].modeStretch = 0.60; pads[13].modeInjectAmount = 0.0;
    pads[13].nonlinearCoupling = 0.35; pads[13].decaySkew = 0.55;
    pads[13].modeScatter = 0.65; pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.00008; pads[13].bodyDampingB1 = 0.060;
    pads[13].noiseLayerMix = 0.78; pads[13].noiseLayerCutoff = 0.95;
    pads[13].noiseLayerColor = 0.92; pads[13].noiseLayerDecay = 0.70;
    pads[13].clickLayerMix = 0.20; pads[13].clickLayerBrightness = 0.82;
    pads[13].outputBus = 1; pads[13].pan = 0.45;

    // Crash 2 / Splash (15) — NEW; NoiseBody/NoiseBurst, aux bus 1.
    pads[15].exciterType = ExciterType::NoiseBurst;
    pads[15].bodyModel = BodyModelType::NoiseBody;
    pads[15].material = 0.95; pads[15].size = 0.22; pads[15].decay = 0.40;
    pads[15].strikePosition = 0.55; pads[15].level = 0.72;
    pads[15].modeStretch = 0.55; pads[15].modeInjectAmount = 0.20;
    pads[15].nonlinearCoupling = 0.30; pads[15].decaySkew = 0.55;
    pads[15].modeScatter = 0.70; pads[15].airLoading = 0.0;
    pads[15].bodyDampingB3 = 0.0; pads[15].bodyDampingB1 = 0.30;
    pads[15].noiseLayerMix = 0.72; pads[15].noiseLayerCutoff = 0.95;
    pads[15].noiseLayerColor = 0.92; pads[15].noiseLayerDecay = 0.40;
    pads[15].clickLayerMix = 0.22; pads[15].clickLayerBrightness = 0.82;
    pads[15].outputBus = 1; pads[15].pan = 0.55;

    // Trap Shaker (16) — NEW; NoiseBody/NoiseBurst cabasa-bright Violet sizzle.
    pads[16].exciterType = ExciterType::NoiseBurst;
    pads[16].bodyModel = BodyModelType::NoiseBody;
    pads[16].material = 0.85; pads[16].size = 0.12; pads[16].decay = 0.10;
    pads[16].level = 0.62;
    pads[16].noiseBurstDuration = 0.45;
    pads[16].noiseLayerMix = 0.85; pads[16].noiseLayerCutoff = 0.82;
    pads[16].noiseLayerColor = 0.92; pads[16].noiseLayerDecay = 0.10; // Violet
    pads[16].clickLayerMix = 0.0;
    pads[16].airLoading = 0.0; pads[16].modeScatter = 0.20;
    pads[16].bodyDampingB3 = 0.0; pads[16].bodyDampingB1 = 0.55;
    pads[16].pan = 0.66;

    k.opts.maxPolyphony   = 16;
    k.opts.stealingPolicy = 2;
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
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
    pads[21].bodyDampingB3 = 0.00008; pads[21].bodyDampingB1 = 0.060;
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

    k.opts.maxPolyphony    = 16; // plugin range is [4,16]
    k.opts.globalCoupling  = 0.40;
    k.opts.couplingDelayMs = 1.4;
    k.crafted = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    return k;
}
Kit cajonFramesKit() {
    Kit k{"Cajon and Frames", "Percussive", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // 18 crafted voices, 5 bodies. The 3 tabla-style clones (9/11/13)
    // become Conga/Bongo/Agogo-HI; adds Agogo-LO/Clave/Guiro/Cabasa/
    // Conga-Slap. Cajon-snare + riq corrected per archetype; pan spread;
    // choke groups 1 (pandeiro) + 2 (cabasa).

    // Cajon Bass (0) -- Plate/Impulse
    pads[0].exciterType = ExciterType::Impulse;
    pads[0].bodyModel = BodyModelType::Plate;
    pads[0].material = 0.30; pads[0].size = 0.80; pads[0].decay = 0.42;
    pads[0].level = 0.85; pads[0].strikePosition = 0.50;
    pads[0].modeStretch = 0.42; pads[0].decaySkew = 0.62;
    pads[0].tsPitchEnvStart = toLogNorm(165);
    pads[0].tsPitchEnvEnd   = toLogNorm(85);
    pads[0].tsPitchEnvTime  = 0.07; pads[0].tsPitchEnvCurve = 0.35;
    pads[0].modeScatter = 0.18;
    pads[0].couplingStrength = 0.45; pads[0].secondaryEnabled = 1.0;
    pads[0].secondarySize = 0.55; pads[0].secondaryMaterial = 0.30;
    pads[0].clickLayerMix = 0.35; pads[0].clickLayerContactMs = 0.20;
    pads[0].clickLayerBrightness = 0.45;
    pads[0].airLoading = 0.0;   // no-op on Plate
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.10;
    pads[0].pan = 0.50;

    // Cajon Slap (2) -- Plate/Impulse
    pads[2] = pads[0];
    pads[2].material = 0.42; pads[2].size = 0.65; pads[2].decay = 0.22;
    pads[2].strikePosition = 0.10; pads[2].decaySkew = 0.35;
    pads[2].tsPitchEnvStart = toLogNorm(280);
    pads[2].tsPitchEnvEnd   = toLogNorm(220);
    pads[2].tsPitchEnvTime  = 0.04;
    pads[2].clickLayerMix = 0.85; pads[2].clickLayerBrightness = 0.78;
    pads[2].clickLayerContactMs = 0.10;
    pads[2].macroPunch = 0.85;
    pads[2].pan = 0.58;

    // Cajon Snare-Side (4) -- Plate/NoiseBurst, bright slap tone (corrected)
    pads[4].exciterType = ExciterType::NoiseBurst;
    pads[4].bodyModel = BodyModelType::Plate;
    pads[4].material = 0.50; pads[4].size = 0.27; pads[4].decay = 0.34;
    pads[4].level = 0.85; pads[4].strikePosition = 0.70;  // edge
    pads[4].noiseBurstDuration = (5.0 - 2.0) / 13.0;
    pads[4].modeStretch = 0.42; pads[4].decaySkew = 0.40;
    pads[4].modeInjectAmount = 0.12; pads[4].nonlinearCoupling = 0.22;
    pads[4].tsDriveAmount = 0.25;
    pads[4].tsFilterType = FilterType::LP;
    pads[4].tsFilterCutoff = 0.884;       // ~9 kHz
    pads[4].tsFilterResonance = 0.15;
    pads[4].tsFilterEnvAmount = 0.5;      // no sweep
    pads[4].tsPitchEnvStart = toLogNorm(210);
    pads[4].tsPitchEnvEnd   = toLogNorm(140);
    pads[4].tsPitchEnvTime  = 0.14; pads[4].tsPitchEnvCurve = 0.15;
    pads[4].noiseLayerMix = 0.75; pads[4].noiseLayerCutoff = 0.82;
    pads[4].noiseLayerColor = 0.90; pads[4].noiseLayerDecay = 0.40;  // violet
    pads[4].clickLayerMix = 0.72; pads[4].clickLayerContactMs = 0.167;
    pads[4].clickLayerBrightness = 0.786;
    pads[4].airLoading = 0.0; pads[4].modeScatter = 0.30;
    pads[4].couplingStrength = 0.62; pads[4].secondaryEnabled = 1.0;
    pads[4].secondarySize = 0.55; pads[4].secondaryMaterial = 0.45;
    pads[4].couplingAmount = 0.62;
    pads[4].bodyDampingB1 = 0.55; pads[4].bodyDampingB3 = 0.18;
    pads[4].pan = 0.50;

    // Frame Drum tap (6) -- Membrane/Mallet (light frame-shell divergence)
    pads[6].exciterType = ExciterType::Mallet;
    pads[6].bodyModel = BodyModelType::Membrane;
    pads[6].material = 0.36; pads[6].size = 0.78; pads[6].decay = 0.50;
    pads[6].level = 0.80; pads[6].strikePosition = 0.35;
    pads[6].airLoading = 0.85; pads[6].modeScatter = 0.18;
    pads[6].decaySkew = 0.62;
    pads[6].couplingStrength = 0.30; pads[6].secondaryEnabled = 1.0;
    pads[6].secondarySize = 0.40; pads[6].secondaryMaterial = 0.32;
    pads[6].tensionModAmt = 0.20;
    pads[6].clickLayerMix = 0.40; pads[6].clickLayerContactMs = 0.30;
    pads[6].clickLayerBrightness = 0.40;
    pads[6].noiseLayerMix = 0.18; pads[6].noiseLayerColor = 0.40;
    pads[6].bodyDampingB1 = 0.30; pads[6].bodyDampingB3 = 0.55;
    pads[6].pan = 0.46;

    // Frame Drum slap (8)
    pads[8] = pads[6];
    pads[8].decay = 0.22; pads[8].strikePosition = 0.08;
    pads[8].decaySkew = 0.45; pads[8].bodyDampingB3 = 0.40;
    pads[8].clickLayerMix = 0.85; pads[8].clickLayerContactMs = 0.12;
    pads[8].clickLayerBrightness = 0.65;
    pads[8].pan = 0.54;

    // Bodhran (10) -- Membrane/Impulse
    pads[10].exciterType = ExciterType::Impulse;
    pads[10].bodyModel = BodyModelType::Membrane;
    pads[10].material = 0.45; pads[10].size = 0.72; pads[10].decay = 0.40;
    pads[10].level = 0.80; pads[10].strikePosition = 0.40;
    pads[10].airLoading = 0.78; pads[10].modeScatter = 0.20;
    pads[10].decaySkew = 0.45;
    pads[10].couplingStrength = 0.32; pads[10].secondaryEnabled = 1.0;
    pads[10].secondarySize = 0.42; pads[10].secondaryMaterial = 0.30;
    pads[10].tensionModAmt = 0.30;
    pads[10].clickLayerMix = 0.65; pads[10].clickLayerContactMs = 0.18;
    pads[10].clickLayerBrightness = 0.62;
    pads[10].noiseLayerMix = 0.18; pads[10].noiseLayerCutoff = 0.45;
    pads[10].noiseLayerColor = 0.40;
    pads[10].bodyDampingB1 = 0.30; pads[10].bodyDampingB3 = 0.10;
    pads[10].pan = 0.50;

    // Dholak HI (12) -- Membrane/Impulse
    pads[12].exciterType = ExciterType::Impulse;
    pads[12].bodyModel = BodyModelType::Membrane;
    pads[12].material = 0.42; pads[12].size = 0.55; pads[12].decay = 0.30;
    pads[12].level = 0.80; pads[12].strikePosition = 0.62;
    pads[12].tsPitchEnvStart = toLogNorm(320);
    pads[12].tsPitchEnvEnd   = toLogNorm(220);
    pads[12].tsPitchEnvTime  = 0.05; pads[12].tsPitchEnvCurve = 0.15;
    pads[12].airLoading = 0.45; pads[12].modeScatter = 0.10;
    pads[12].decaySkew = 0.58; pads[12].nonlinearCoupling = 0.12;
    pads[12].couplingStrength = 0.30; pads[12].secondaryEnabled = 1.0;
    pads[12].secondarySize = 0.45; pads[12].secondaryMaterial = 0.35;
    pads[12].tensionModAmt = 0.16;
    pads[12].clickLayerMix = 0.42; pads[12].clickLayerBrightness = 0.55;
    pads[12].noiseLayerMix = 0.14; pads[12].noiseLayerColor = 0.45;
    pads[12].bodyDampingB1 = 0.34; pads[12].bodyDampingB3 = 0.12;
    pads[12].pan = 0.58;

    // Dholak LO (14)
    pads[14].exciterType = ExciterType::Impulse;
    pads[14].bodyModel = BodyModelType::Membrane;
    pads[14].material = 0.33; pads[14].size = 0.65; pads[14].decay = 0.40;
    pads[14].level = 0.85; pads[14].strikePosition = 0.30;
    pads[14].tsPitchEnvStart = toLogNorm(220);
    pads[14].tsPitchEnvEnd   = toLogNorm(140);
    pads[14].tsPitchEnvTime  = 0.16; pads[14].tsPitchEnvCurve = 0.15;
    pads[14].airLoading = 0.60; pads[14].modeScatter = 0.10;
    pads[14].decaySkew = 0.66; pads[14].nonlinearCoupling = 0.18;
    pads[14].tsFilterType = FilterType::LP; pads[14].tsFilterCutoff = 0.82;
    pads[14].couplingStrength = 0.40; pads[14].secondaryEnabled = 1.0;
    pads[14].secondarySize = 0.55; pads[14].secondaryMaterial = 0.35;
    pads[14].tensionModAmt = 0.34;
    pads[14].clickLayerMix = 0.32; pads[14].clickLayerBrightness = 0.30;
    pads[14].noiseLayerMix = 0.10; pads[14].noiseLayerColor = 0.12;  // brown
    pads[14].bodyDampingB1 = 0.28; pads[14].bodyDampingB3 = 0.18;
    pads[14].pan = 0.42;

    // Riq (5) -- Membrane/Impulse, metallic jingle secondary (corrected)
    pads[5].exciterType = ExciterType::Impulse;
    pads[5].bodyModel = BodyModelType::Membrane;
    pads[5].material = 0.40; pads[5].size = 0.52; pads[5].decay = 0.34;
    pads[5].level = 0.78; pads[5].strikePosition = 0.60;
    pads[5].airLoading = 0.45; pads[5].modeScatter = 0.20;
    pads[5].decaySkew = 0.55;
    pads[5].couplingStrength = 0.40; pads[5].secondaryEnabled = 1.0;
    pads[5].secondarySize = 0.20; pads[5].secondaryMaterial = 0.85;
    pads[5].clickLayerMix = 0.65; pads[5].clickLayerContactMs = 0.30;
    pads[5].clickLayerBrightness = 0.85;
    pads[5].noiseLayerMix = 0.60; pads[5].noiseLayerCutoff = 0.82;
    pads[5].noiseLayerResonance = 0.30; pads[5].noiseLayerDecay = 0.50;
    pads[5].noiseLayerColor = 0.90;  // violet
    pads[5].bodyDampingB1 = 0.32; pads[5].bodyDampingB3 = 0.12;
    pads[5].macroComplexity = 0.62;
    pads[5].pan = 0.50;

    // Pandeiro shake (7) -- NoiseBody/NoiseBurst, choke group 1
    pads[7].exciterType = ExciterType::NoiseBurst;
    pads[7].bodyModel = BodyModelType::NoiseBody;
    pads[7].material = 0.90; pads[7].size = 0.12; pads[7].decay = 0.10;
    pads[7].level = 0.72; pads[7].strikePosition = 0.30;
    pads[7].chokeGroup = 1;
    pads[7].noiseBurstDuration = 0.40;
    pads[7].noiseLayerMix = 0.80; pads[7].noiseLayerCutoff = 0.92;
    pads[7].noiseLayerResonance = 0.20; pads[7].noiseLayerDecay = 0.18;
    pads[7].noiseLayerColor = 0.88;
    pads[7].clickLayerMix = 0.20; pads[7].clickLayerBrightness = 0.85;
    pads[7].airLoading = 0.0; pads[7].modeScatter = 0.45;
    pads[7].bodyDampingB3 = 0.0; pads[7].bodyDampingB1 = 0.32;
    pads[7].pan = 0.62;

    // Conga open (9) -- Membrane/Impulse (was tabla filler)
    pads[9].exciterType = ExciterType::Impulse;
    pads[9].bodyModel = BodyModelType::Membrane;
    pads[9].material = 0.45; pads[9].size = 0.62; pads[9].decay = 0.40;
    pads[9].level = 0.80; pads[9].strikePosition = 0.40;
    pads[9].tsPitchEnvStart = toLogNorm(200);
    pads[9].tsPitchEnvEnd   = toLogNorm(150);
    pads[9].tsPitchEnvTime  = 0.04; pads[9].tsPitchEnvCurve = 0.15;
    pads[9].airLoading = 0.50; pads[9].modeScatter = 0.10;
    pads[9].tensionModAmt = 0.20;
    pads[9].couplingStrength = 0.30; pads[9].secondaryEnabled = 1.0;
    pads[9].secondarySize = 0.45; pads[9].secondaryMaterial = 0.40;
    pads[9].clickLayerMix = 0.50; pads[9].clickLayerContactMs = 0.20;
    pads[9].clickLayerBrightness = 0.55;
    pads[9].noiseLayerMix = 0.10; pads[9].noiseLayerCutoff = 0.40;
    pads[9].noiseLayerColor = 0.40;
    pads[9].bodyDampingB1 = 0.30; pads[9].bodyDampingB3 = 0.10;
    pads[9].pan = 0.40;

    // Bongo macho (11) -- Membrane/Impulse (was tabla filler)
    pads[11].exciterType = ExciterType::Impulse;
    pads[11].bodyModel = BodyModelType::Membrane;
    pads[11].material = 0.55; pads[11].size = 0.32; pads[11].decay = 0.28;
    pads[11].level = 0.80; pads[11].strikePosition = 0.30;
    pads[11].tsPitchEnvStart = toLogNorm(420);
    pads[11].tsPitchEnvEnd   = toLogNorm(350);
    pads[11].tsPitchEnvTime  = 0.04; pads[11].tsPitchEnvCurve = 0.15;
    pads[11].airLoading = 0.42; pads[11].modeScatter = 0.10;
    pads[11].tensionModAmt = 0.22;
    pads[11].couplingStrength = 0.25; pads[11].secondaryEnabled = 1.0;
    pads[11].secondarySize = 0.30; pads[11].secondaryMaterial = 0.40;
    pads[11].clickLayerMix = 0.55; pads[11].clickLayerContactMs = 0.15;
    pads[11].clickLayerBrightness = 0.72;
    pads[11].noiseLayerMix = 0.10; pads[11].noiseLayerColor = 0.40;
    pads[11].bodyDampingB1 = 0.30; pads[11].bodyDampingB3 = 0.10;
    pads[11].pan = 0.60;

    // Agogo HI (13) -- Bell/FMImpulse (was tabla filler)
    pads[13].exciterType = ExciterType::FMImpulse;
    pads[13].bodyModel = BodyModelType::Bell;
    pads[13].material = 0.85; pads[13].size = 0.14; pads[13].decay = 0.28;
    pads[13].level = 0.75; pads[13].strikePosition = 0.30;
    pads[13].fmRatio = 0.72;   // -> 3.16
    pads[13].modeStretch = 0.45; pads[13].decaySkew = 0.40;
    pads[13].modeScatter = 0.12;
    pads[13].clickLayerMix = 0.55; pads[13].clickLayerContactMs = 0.30;
    pads[13].clickLayerBrightness = 0.85;
    pads[13].noiseLayerMix = 0.0;
    pads[13].airLoading = 0.0;
    pads[13].bodyDampingB3 = 0.0; pads[13].bodyDampingB1 = 0.30;
    pads[13].macroBrightness = 0.65;
    pads[13].pan = 0.42;

    // Agogo LO (15) -- Bell/FMImpulse
    pads[15] = pads[13];
    pads[15].size = 0.22; pads[15].decay = 0.35; pads[15].fmRatio = 0.55;  // -> 2.65
    pads[15].pan = 0.58;

    // Clave (16) -- Shell/Impulse, free-free bar, b3 0.70 dry-wood override
    pads[16].exciterType = ExciterType::Impulse;
    pads[16].bodyModel = BodyModelType::Shell;
    pads[16].material = 0.85; pads[16].size = 0.0; pads[16].decay = 0.12;
    pads[16].level = 0.80; pads[16].strikePosition = 0.12;
    pads[16].modeStretch = 0.333;
    pads[16].clickLayerMix = 0.55; pads[16].clickLayerContactMs = 0.15;
    pads[16].clickLayerBrightness = 0.82;
    pads[16].noiseLayerMix = 0.0;
    pads[16].airLoading = 0.0;
    pads[16].bodyDampingB3 = 0.70;
    pads[16].pan = 0.36;

    // Guiro (17) -- NoiseBody/Friction scrape (no beater click)
    pads[17].exciterType = ExciterType::Friction;
    pads[17].bodyModel = BodyModelType::NoiseBody;
    pads[17].material = 0.65; pads[17].size = 0.18; pads[17].decay = 0.30;
    pads[17].level = 0.70; pads[17].strikePosition = 0.30;
    pads[17].frictionPressure = 0.55;
    pads[17].decaySkew = 0.55; pads[17].modeScatter = 0.30;
    pads[17].noiseLayerMix = 0.65; pads[17].noiseLayerCutoff = 0.62;
    pads[17].noiseLayerResonance = 0.20; pads[17].noiseLayerDecay = 0.30;
    pads[17].noiseLayerColor = 0.55;
    pads[17].clickLayerMix = 0.0;
    pads[17].airLoading = 0.0;
    pads[17].bodyDampingB3 = 0.0; pads[17].bodyDampingB1 = 0.42;
    pads[17].pan = 0.64;

    // Cabasa (18) -- NoiseBody/NoiseBurst, choke group 2
    pads[18].exciterType = ExciterType::NoiseBurst;
    pads[18].bodyModel = BodyModelType::NoiseBody;
    pads[18].material = 0.85; pads[18].size = 0.08; pads[18].decay = 0.08;
    pads[18].level = 0.62;
    pads[18].chokeGroup = 2;
    pads[18].noiseBurstDuration = 0.20;
    pads[18].noiseLayerMix = 0.85; pads[18].noiseLayerCutoff = 0.73;
    pads[18].noiseLayerResonance = 0.16; pads[18].noiseLayerDecay = 0.12;
    pads[18].noiseLayerColor = 0.75;
    pads[18].clickLayerMix = 0.0;
    pads[18].airLoading = 0.0; pads[18].modeScatter = 0.30;
    pads[18].bodyDampingB3 = 0.0; pads[18].bodyDampingB1 = 0.55;
    pads[18].pan = 0.34;

    // Conga Slap (19) -- Membrane/Impulse, choked (no kerthump)
    pads[19].exciterType = ExciterType::Impulse;
    pads[19].bodyModel = BodyModelType::Membrane;
    pads[19].material = 0.55; pads[19].size = 0.50; pads[19].decay = 0.18;
    pads[19].level = 0.85; pads[19].strikePosition = 0.10;
    pads[19].airLoading = 0.40; pads[19].modeScatter = 0.30;
    pads[19].tensionModAmt = 0.0;
    pads[19].couplingStrength = 0.20; pads[19].secondaryEnabled = 1.0;
    pads[19].secondarySize = 0.40; pads[19].secondaryMaterial = 0.40;
    pads[19].clickLayerMix = 0.85; pads[19].clickLayerContactMs = 0.10;
    pads[19].clickLayerBrightness = 0.85;
    pads[19].noiseLayerMix = 0.15; pads[19].noiseLayerCutoff = 0.70;
    pads[19].noiseLayerColor = 0.78; pads[19].noiseLayerDecay = 0.18;
    pads[19].bodyDampingB1 = 0.45; pads[19].bodyDampingB3 = 0.10;
    pads[19].macroPunch = 0.85;
    pads[19].pan = 0.40;

    k.opts.maxPolyphony    = 12;
    k.opts.globalCoupling  = 0.18;
    k.opts.snareBuzz       = 0.20;
    k.opts.tomResonance    = 0.30;
    k.opts.couplingDelayMs = 1.0;
    // 18 sounding pads; 1, 3, 20-31 disabled.
    k.crafted = {0, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    return k;
}
Kit glassBellGardenKit() {
    Kit k{"Glass Bell Garden", "Unnatural", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // All 32 pads are Bell (the only tuned long-ring resonator), voiced as 7
    // distinct sub-roles rather than one i-ramp. airLoading/tensionMod are
    // Membrane-only no-ops on Bell (left 0, documented). b3=0 = pure metal.
    // Morph durations (seconds in the plan) are approximated to the normalized
    // morphDuration field: ~1.7 s->0.85, 1.4 s->0.75, 1.1 s->0.65, 0.4 s->0.30.
    for (int p = 0; p < kNumPads; ++p) {
        pads[p].bodyModel  = BodyModelType::Bell;
        pads[p].airLoading = 0.0;
        pads[p].tensionModAmt = 0.0;
        pads[p].bodyDampingB3 = 0.0;
    }

    // --- Struck glass bells 0-5 (Mallet) ---
    {
        const double mat[]    = {0.55, 0.62, 0.68, 0.75, 0.82, 0.90};
        const double size[]   = {0.62, 0.54, 0.46, 0.38, 0.30, 0.22};
        const double dec[]    = {0.86, 0.84, 0.82, 0.80, 0.78, 0.76};
        const double str[]    = {0.42, 0.43, 0.44, 0.45, 0.46, 0.47};
        const double skew[]   = {0.60, 0.62, 0.64, 0.66, 0.68, 0.70};
        const double scat[]   = {0.46, 0.48, 0.50, 0.52, 0.54, 0.56};
        const double clk[]    = {0.32, 0.34, 0.36, 0.38, 0.40, 0.42};
        const double nmix[]   = {0.13, 0.13, 0.13, 0.12, 0.12, 0.12};
        const double cpl[]    = {0.55, 0.60, 0.65, 0.70, 0.70, 0.75};
        const double pan[]    = {0.18, 0.30, 0.42, 0.58, 0.70, 0.82};
        const double spos[]   = {0.300, 0.284, 0.268, 0.252, 0.236, 0.220};
        const double lvl[]    = {0.700, 0.692, 0.684, 0.676, 0.668, 0.660};
        const double cbr[]    = {0.720, 0.752, 0.784, 0.816, 0.848, 0.880};
        for (int i = 0; i < 6; ++i) {
            const int p = i;
            pads[p].exciterType = ExciterType::Mallet;
            pads[p].material = mat[i]; pads[p].size = size[i]; pads[p].decay = dec[i];
            pads[p].strikePosition = spos[i]; pads[p].level = lvl[i];
            pads[p].modeStretch = str[i]; pads[p].decaySkew = skew[i];
            pads[p].modeScatter = scat[i];
            pads[p].bodyDampingB1 = 0.30;
            pads[p].clickLayerMix = clk[i]; pads[p].clickLayerBrightness = cbr[i];
            pads[p].noiseLayerMix = nmix[i];
            pads[p].couplingAmount = cpl[i];
            pads[p].pan = pan[i];
        }
        // Coupled shells on 2 and 5.
        pads[2].couplingStrength = 0.25; pads[2].secondaryEnabled = 1.0;
        pads[2].secondarySize = 0.38; pads[2].secondaryMaterial = 0.85;
        pads[5].couplingStrength = 0.25; pads[5].secondaryEnabled = 1.0;
        pads[5].secondarySize = 0.42; pads[5].secondaryMaterial = 0.85;
    }

    // --- FM-glass pings 6-11 (FMImpulse) ---
    {
        const double mat[]  = {0.70, 0.73, 0.76, 0.80, 0.85, 0.90};
        const double size[] = {0.42, 0.37, 0.32, 0.27, 0.22, 0.16};
        const double dec[]  = {0.55, 0.55, 0.52, 0.50, 0.48, 0.46};
        const double fmr[]  = {0.30, 0.38, 0.46, 0.54, 0.62, 0.60};
        const double str[]  = {0.42, 0.43, 0.44, 0.45, 0.46, 0.47};
        const double skew[] = {0.58, 0.60, 0.62, 0.64, 0.66, 0.68};
        const double minj[] = {0.12, 0.12, 0.10, 0.10, 0.08, 0.00};
        const double nlc[]  = {0.12, 0.16, 0.20, 0.24, 0.28, 0.30};
        const double cpl[]  = {0.55, 0.60, 0.65, 0.70, 0.70, 0.75};
        const double pan[]  = {0.26, 0.38, 0.50, 0.62, 0.74, 0.86};
        const double lvl[]  = {0.720, 0.712, 0.704, 0.696, 0.688, 0.680};
        for (int i = 0; i < 6; ++i) {
            const int p = 6 + i;
            pads[p].exciterType = ExciterType::FMImpulse;
            pads[p].material = mat[i]; pads[p].size = size[i]; pads[p].decay = dec[i];
            pads[p].strikePosition = 0.30; pads[p].level = lvl[i];
            pads[p].fmRatio = fmr[i];
            pads[p].modeStretch = str[i]; pads[p].decaySkew = skew[i];
            pads[p].modeInjectAmount = minj[i]; pads[p].nonlinearCoupling = nlc[i];
            pads[p].bodyDampingB1 = 0.30;
            pads[p].clickLayerMix = 0.15; pads[p].clickLayerBrightness = 0.70;
            pads[p].noiseLayerMix = 0.0;
            pads[p].couplingAmount = cpl[i];
            pads[p].pan = pan[i];
        }
        // Coupled shell on 11.
        pads[11].couplingStrength = 0.25; pads[11].secondaryEnabled = 1.0;
        pads[11].secondarySize = 0.46; pads[11].secondaryMaterial = 0.85;
    }

    // --- Bowed singing-glass 12/13 + glass-harmonica 26 (Friction, aux 1) ---
    {
        // {pad, mat, size, decay, stretch, skew, fric, morphStart, morphEnd,
        //  scatter, level, pan}
        const int    pd[]  = {12, 13, 26};
        const double mat[] = {0.78, 0.85, 0.80};
        const double sz[]  = {0.55, 0.42, 0.35};
        const double strc[]= {0.45, 0.47, 0.43};
        const double skw[] = {0.85, 0.82, 0.83};
        const double fri[] = {0.28, 0.32, 0.26};
        const double mS[]  = {0.78, 0.85, 0.80};
        const double mE[]  = {0.55, 0.60, 0.60};
        const double sct[] = {0.06, 0.10, 0.08};
        const double lvl[] = {0.60, 0.62, 0.60};
        const double pan[] = {0.34, 0.66, 0.50};
        for (int i = 0; i < 3; ++i) {
            const int p = pd[i];
            pads[p].exciterType = ExciterType::Friction;
            pads[p].material = mat[i]; pads[p].size = sz[i]; pads[p].decay = 0.95;
            pads[p].strikePosition = 0.20; pads[p].level = lvl[i];
            pads[p].frictionPressure = fri[i];
            pads[p].modeStretch = strc[i]; pads[p].decaySkew = skw[i];
            pads[p].nonlinearCoupling = 0.22;
            pads[p].modeScatter = sct[i];
            pads[p].morphEnabled = 1.0; pads[p].morphStart = mS[i];
            pads[p].morphEnd = mE[i]; pads[p].morphDuration = 0.85; pads[p].morphCurve = 0.0; // exp
            pads[p].clickLayerMix = 0.0;
            pads[p].noiseLayerMix = 0.10; pads[p].noiseLayerColor = 0.40; // pink
            pads[p].bodyDampingB1 = 0.30;
            pads[p].macroComplexity = 0.85;
            pads[p].outputBus = 1; pads[p].pan = pan[i];
        }
    }

    // --- Glass gong 14 (Mallet, bloom, heavy shell, aux 1) ---
    pads[14].exciterType = ExciterType::Mallet;
    pads[14].material = 0.60; pads[14].size = 0.85; pads[14].decay = 0.95;
    pads[14].strikePosition = 0.35; pads[14].level = 0.66;
    pads[14].modeStretch = 0.62; pads[14].decaySkew = 0.65;
    pads[14].nonlinearCoupling = 0.80; // bloom
    pads[14].modeScatter = 0.47;
    pads[14].couplingStrength = 0.95; pads[14].secondaryEnabled = 1.0;
    pads[14].secondarySize = 0.40; pads[14].secondaryMaterial = 0.85;
    pads[14].couplingAmount = 0.85;
    pads[14].morphEnabled = 1.0; pads[14].morphStart = 0.85; pads[14].morphEnd = 0.55;
    pads[14].morphDuration = 0.30; pads[14].morphCurve = 0.5; // lin
    pads[14].noiseLayerMix = 0.30; pads[14].noiseLayerColor = 0.92; // violet
    pads[14].noiseLayerCutoff = 0.85;
    pads[14].clickLayerMix = 0.20;
    pads[14].bodyDampingB1 = 0.28;
    pads[14].outputBus = 1; pads[14].pan = 0.50;

    // --- FM-glass shatter-crash 15 (FMImpulse, aux 1, static) ---
    pads[15].exciterType = ExciterType::FMImpulse;
    pads[15].material = 0.92; pads[15].size = 0.45; pads[15].decay = 0.78;
    pads[15].strikePosition = 0.32; pads[15].level = 0.66;
    pads[15].fmRatio = 0.45;
    pads[15].modeStretch = 0.55; pads[15].decaySkew = 0.60;
    pads[15].nonlinearCoupling = 0.45;
    pads[15].modeScatter = 0.65;
    pads[15].noiseLayerMix = 0.40; pads[15].noiseLayerColor = 0.92; // violet
    pads[15].noiseLayerCutoff = 0.92; pads[15].noiseLayerDecay = 0.72;
    pads[15].noiseLayerResonance = 0.20;
    pads[15].clickLayerMix = 0.35; pads[15].clickLayerBrightness = 0.85;
    pads[15].clickLayerContactMs = 0.30;
    pads[15].bodyDampingB1 = 0.30;
    pads[15].couplingAmount = 0.75;
    pads[15].outputBus = 1; pads[15].pan = 0.50;

    // --- Crotale-glass tines 16-20 (Mallet, NEUTRAL stretch, Mode Inject 1/k) ---
    {
        const double mat[]  = {0.92, 0.92, 0.92, 0.90, 0.95};
        const double size[] = {0.12, 0.16, 0.20, 0.22, 0.08};
        const double dec[]  = {0.85, 0.84, 0.83, 0.82, 0.80};
        const double minj[] = {0.16, 0.16, 0.14, 0.14, 0.12};
        const double cbr[]  = {0.85, 0.85, 0.85, 0.85, 0.88};
        const double pan[]  = {0.22, 0.36, 0.50, 0.64, 0.78};
        const double lvl[]  = {0.720, 0.715, 0.710, 0.705, 0.700};
        for (int i = 0; i < 5; ++i) {
            const int p = 16 + i;
            pads[p].exciterType = ExciterType::Mallet;
            pads[p].material = mat[i]; pads[p].size = size[i]; pads[p].decay = dec[i];
            pads[p].strikePosition = 0.10; pads[p].level = lvl[i];
            pads[p].modeStretch = 0.333333; pads[p].decaySkew = 0.58;
            pads[p].modeInjectAmount = minj[i];
            pads[p].modeScatter = 0.08;
            pads[p].bodyDampingB1 = 0.30;
            pads[p].clickLayerMix = 0.42; pads[p].clickLayerContactMs = 0.20;
            pads[p].clickLayerBrightness = cbr[i];
            pads[p].noiseLayerMix = 0.0;
            pads[p].pan = pan[i];
        }
    }

    // --- Temple-glass bell 21 (Mallet, warm low hum) ---
    pads[21].exciterType = ExciterType::Mallet;
    pads[21].material = 0.82; pads[21].size = 0.50; pads[21].decay = 0.92;
    pads[21].strikePosition = 0.18; pads[21].level = 0.78;
    pads[21].modeStretch = 0.40; pads[21].decaySkew = 0.78;
    pads[21].modeInjectAmount = 0.0; // would contradict the tuned hum
    pads[21].modeScatter = 0.06;
    pads[21].bodyDampingB1 = 0.18; pads[21].bodyDampingB3 = 0.06;
    pads[21].clickLayerMix = 0.28; pads[21].clickLayerContactMs = 0.45;
    pads[21].clickLayerBrightness = 0.42;
    pads[21].noiseLayerMix = 0.04;
    pads[21].pan = 0.50;

    // --- Ghost-glass sustains 22-25 (HP filter + Morph + aux 1) ---
    {
        const int    exc[] = {ExciterType::Mallet, ExciterType::FMImpulse,
                              ExciterType::Mallet, ExciterType::Friction};
        const double mat[] = {0.45, 0.50, 0.55, 0.50};
        const double sz[]  = {0.55, 0.45, 0.30, 0.50};
        const double dec[] = {0.80, 0.80, 0.80, 0.82};
        const double str[] = {0.66, 0.71, 0.77, 0.74};
        const double skw[] = {0.78, 0.81, 0.81, 0.81};
        const double hpc[] = {0.22, 0.24, 0.30, 0.26};
        const double pan[] = {0.14, 0.38, 0.62, 0.86};
        const double lvl[] = {0.65, 0.64, 0.64, 0.63};
        for (int i = 0; i < 4; ++i) {
            const int p = 22 + i;
            pads[p].exciterType = exc[i];
            pads[p].material = mat[i]; pads[p].size = sz[i]; pads[p].decay = dec[i];
            pads[p].strikePosition = 0.30; pads[p].level = lvl[i];
            pads[p].modeStretch = str[i]; pads[p].decaySkew = skw[i];
            pads[p].nonlinearCoupling = 0.32;
            pads[p].modeScatter = 0.45;
            pads[p].modeInjectAmount = 0.0;
            pads[p].tsFilterType = FilterType::HP;
            pads[p].tsFilterCutoff = hpc[i]; pads[p].tsFilterResonance = 0.30;
            pads[p].tsFilterEnvAmount = 0.45;
            pads[p].morphEnabled = 1.0; pads[p].morphStart = 0.45; pads[p].morphEnd = 0.80;
            pads[p].morphDuration = 0.75; pads[p].morphCurve = 0.5; // lin
            pads[p].noiseLayerMix = 0.12; pads[p].noiseLayerColor = 0.12; // brown
            pads[p].noiseLayerCutoff = 0.35; pads[p].noiseLayerDecay = 0.85;
            pads[p].clickLayerMix = (i == 3) ? 0.0 : 0.12; // pad 25 bowed, no onset
            pads[p].bodyDampingB1 = 0.35;
            pads[p].couplingAmount = 0.85;
            pads[p].outputBus = 1; pads[p].pan = pan[i];
        }
        pads[23].fmRatio = 0.40;          // c:m 2.2
        pads[25].frictionPressure = 0.30;
    }

    // --- Bell-tree glissando cascade 27-31 (NoiseBurst, choke 1, aux 1) ---
    {
        const double size[] = {0.34, 0.28, 0.22, 0.16, 0.10};
        const double dec[]  = {0.85, 0.84, 0.83, 0.82, 0.80};
        const double pan[]  = {0.10, 0.30, 0.50, 0.70, 0.90};
        const double lvl[]  = {0.70, 0.69, 0.68, 0.67, 0.66};
        for (int i = 0; i < 5; ++i) {
            const int p = 27 + i;
            pads[p].exciterType = ExciterType::NoiseBurst;
            pads[p].material = 0.95; pads[p].size = size[i]; pads[p].decay = dec[i];
            pads[p].strikePosition = 0.30; pads[p].level = lvl[i];
            pads[p].modeStretch = 0.55; pads[p].decaySkew = 0.78;
            pads[p].modeScatter = 0.55;
            pads[p].noiseBurstDuration = 0.45;
            pads[p].noiseLayerMix = 0.42; pads[p].noiseLayerColor = 0.92; // violet
            pads[p].noiseLayerCutoff = 0.92; pads[p].noiseLayerDecay = 0.85;
            pads[p].clickLayerMix = 0.55; pads[p].clickLayerBrightness = 0.92;
            pads[p].bodyDampingB1 = 0.30;
            pads[p].morphEnabled = 1.0; pads[p].morphStart = 0.85; pads[p].morphEnd = 0.55;
            pads[p].morphDuration = 0.65; pads[p].morphCurve = 0.5; // lin
            pads[p].chokeGroup = 1; pads[p].outputBus = 1; pads[p].pan = pan[i];
        }
    }

    k.opts.maxPolyphony    = 16; // plugin range is [4,16] (24 requested; capped)
    k.opts.globalCoupling  = 0.65;
    k.opts.couplingDelayMs = 1.8;
    for (int i = 0; i < kNumPads; ++i) k.crafted.push_back(i);
    return k;
}

Kit droneSustainKit() {
    Kit k{"Drone and Sustain", "Unnatural", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // 32-pad layer-and-hold sustain instrument: 7 drone families. Morph
    // durations (seconds in plan) approximated to normalized morphDuration:
    // ~1.7 s->0.85, ~1.4 s->0.75, ~0.4 s->0.30.

    // --- Friction String Drone 0,2,4,6 (String/Friction, west-coast Drive/Fold) ---
    {
        const int    pd[]  = {0, 2, 4, 6};
        const double mat[] = {0.45, 0.55, 0.64, 0.74};
        const double sz[]  = {0.40, 0.47, 0.55, 0.62};
        const double fri[] = {0.45, 0.49, 0.53, 0.57};
        const double nlc[] = {0.35, 0.37, 0.38, 0.40};
        const double cut[] = {0.45, 0.47, 0.50, 0.52};
        const double pan[] = {0.14, 0.32, 0.52, 0.72};
        for (int i = 0; i < 4; ++i) {
            const int p = pd[i];
            pads[p].exciterType = ExciterType::Friction;
            pads[p].bodyModel = BodyModelType::String;
            pads[p].material = mat[i]; pads[p].size = sz[i]; pads[p].decay = 0.92;
            pads[p].level = 0.62;
            pads[p].frictionPressure = fri[i];
            pads[p].modeStretch = 0.30 + i * 0.04; // inert on String, kept for consistency
            pads[p].nonlinearCoupling = nlc[i];
            pads[p].decaySkew = 0.85;
            pads[p].tsFilterType = FilterType::LP;
            pads[p].tsFilterCutoff = cut[i]; pads[p].tsFilterResonance = 0.35;
            pads[p].tsFilterEnvAmount = 0.62; pads[p].tsFilterEnvAttack = 0.20;
            pads[p].tsFilterEnvDecay = 0.40; pads[p].tsFilterEnvSustain = 0.55;
            pads[p].tsFilterEnvRelease = 0.65;
            pads[p].tsDriveAmount = 0.26; pads[p].tsFoldAmount = 0.22; // west-coast
            pads[p].modeScatter = 0.10 + i * 0.04;
            pads[p].airLoading = 0.0;
            pads[p].couplingStrength = 0.18; pads[p].secondaryEnabled = 1.0;
            pads[p].secondarySize = 0.50; pads[p].secondaryMaterial = 0.65;
            pads[p].noiseLayerMix = 0.16; pads[p].noiseLayerColor = 0.12; // Brown
            pads[p].noiseLayerCutoff = 0.50; pads[p].noiseLayerDecay = 0.85;
            pads[p].clickLayerMix = 0.0;
            pads[p].bodyDampingB1 = 0.30; pads[p].bodyDampingB3 = 0.20;
            pads[p].macroComplexity = 0.85; pads[p].couplingAmount = 0.85;
            pads[p].outputBus = 1; pads[p].pan = pan[i];
            if (i % 3 == 0) { // morph on a subset
                pads[p].morphEnabled = 1.0; pads[p].morphStart = 0.40;
                pads[p].morphEnd = 0.85; pads[p].morphDuration = 0.85;
                pads[p].morphCurve = 0.5;
            }
        }
    }

    // --- Friction Membrane Drone 1,3,5,7 (Membrane/Friction, cuica glide) ---
    {
        const int    pd[]  = {1, 3, 5, 7};
        const double mat[] = {0.40, 0.55, 0.70, 0.85};
        const double sz[]  = {0.50, 0.57, 0.63, 0.70};
        const double ten[] = {0.40, 0.45, 0.50, 0.55};
        const double pan[] = {0.22, 0.40, 0.60, 0.80};
        for (int i = 0; i < 4; ++i) {
            const int p = pd[i];
            pads[p].exciterType = ExciterType::Friction;
            pads[p].bodyModel = BodyModelType::Membrane;
            pads[p].material = mat[i]; pads[p].size = sz[i]; pads[p].decay = 0.92;
            pads[p].level = 0.62;
            pads[p].frictionPressure = 0.45;
            pads[p].decaySkew = 0.85;
            pads[p].modeInjectAmount = 0.18; // 1/k bowed (stick-slip) series
            pads[p].nonlinearCoupling = 0.38;
            pads[p].airLoading = 0.50;
            pads[p].tensionModAmt = ten[i]; // Membrane-only cuica glide
            pads[p].tsFilterType = FilterType::LP;
            pads[p].tsFilterCutoff = 0.48; pads[p].tsFilterResonance = 0.35;
            pads[p].tsFilterEnvAmount = 0.55; pads[p].tsFilterEnvAttack = 0.20;
            pads[p].tsFilterEnvDecay = 0.40; pads[p].tsFilterEnvSustain = 0.55;
            pads[p].tsFilterEnvRelease = 0.65;
            pads[p].modeScatter = 0.12;
            pads[p].couplingStrength = 0.18; pads[p].secondaryEnabled = 1.0;
            pads[p].secondarySize = 0.50; pads[p].secondaryMaterial = 0.65;
            pads[p].noiseLayerMix = 0.20; pads[p].noiseLayerColor = 0.40; // Pink
            pads[p].noiseLayerCutoff = 0.50; pads[p].noiseLayerDecay = 0.85;
            pads[p].clickLayerMix = 0.0;
            pads[p].bodyDampingB1 = 0.30; pads[p].bodyDampingB3 = 0.20;
            pads[p].macroComplexity = 0.85; pads[p].couplingAmount = 0.85;
            pads[p].outputBus = 1; pads[p].pan = pan[i];
        }
    }

    // --- Feedback Plate (8,10,12) / Shell (9,11,13) Drone (evolving BP bloom) ---
    {
        const double mat[]  = {0.62, 0.67, 0.72, 0.77, 0.81, 0.85};
        const double sz[]   = {0.45, 0.49, 0.53, 0.59, 0.63, 0.67};
        const double fb[]   = {0.45, 0.46, 0.47, 0.48, 0.49, 0.50};
        const double str[]  = {0.45, 0.47, 0.50, 0.53, 0.55, 0.57};
        const double cut[]  = {0.55, 0.56, 0.57, 0.56, 0.57, 0.58};
        const double pan[]  = {0.16, 0.30, 0.44, 0.58, 0.72, 0.86};
        for (int i = 0; i < 6; ++i) {
            const int p = 8 + i;
            pads[p].exciterType = ExciterType::Feedback;
            pads[p].bodyModel = (p % 2 == 0) ? BodyModelType::Plate : BodyModelType::Shell;
            pads[p].material = mat[i]; pads[p].size = sz[i]; pads[p].decay = 0.92;
            pads[p].level = 0.62;
            pads[p].feedbackAmount = fb[i];
            pads[p].modeStretch = str[i];
            pads[p].nonlinearCoupling = 0.45; // pulled down for sustained env
            pads[p].decaySkew = 0.78;
            pads[p].tsFilterType = FilterType::BP;
            pads[p].tsFilterCutoff = cut[i]; pads[p].tsFilterResonance = 0.45;
            pads[p].tsFilterEnvAmount = 0.65; pads[p].tsFilterEnvAttack = 0.45; // bloom
            pads[p].tsFilterEnvDecay = 0.60; pads[p].tsFilterEnvSustain = 0.55;
            pads[p].tsFilterEnvRelease = 0.65;
            pads[p].modeScatter = 0.40;
            pads[p].airLoading = 0.0;
            pads[p].tensionModAmt = 0.0; // no-op on Plate/Shell (removed)
            pads[p].couplingStrength = 0.30; pads[p].secondaryEnabled = 0.0;
            pads[p].noiseLayerMix = 0.30; pads[p].noiseLayerColor = 0.70; // White
            pads[p].noiseLayerCutoff = 0.55; pads[p].noiseLayerDecay = 0.85;
            pads[p].clickLayerMix = 0.0;
            pads[p].bodyDampingB1 = 0.30; pads[p].bodyDampingB3 = 0.20;
            pads[p].couplingAmount = 0.85;
            pads[p].outputBus = 1; pads[p].pan = pan[i];
        }
    }

    // --- Singing Bowls 14-17 + Bowed-Bell shimmer 18-19 (Bell/Friction, aux 1) ---
    {
        const int    pd[]  = {14, 15, 16, 17, 18, 19};
        const double mat[] = {0.74, 0.79, 0.85, 0.90, 0.88, 0.86};
        const double sz[]  = {0.22, 0.38, 0.54, 0.70, 0.20, 0.45};
        const double fri[] = {0.28, 0.30, 0.33, 0.35, 0.32, 0.32};
        const double nlc[] = {0.22, 0.25, 0.27, 0.30, 0.26, 0.26};
        const double skw[] = {0.85, 0.86, 0.87, 0.88, 0.85, 0.85};
        const double sct[] = {0.06, 0.06, 0.06, 0.06, 0.08, 0.08};
        const double lvl[] = {0.74, 0.73, 0.72, 0.70, 0.72, 0.72};
        const double pan[] = {0.20, 0.40, 0.60, 0.80, 0.30, 0.70};
        for (int i = 0; i < 6; ++i) {
            const int p = pd[i];
            pads[p].exciterType = ExciterType::Friction;
            pads[p].material = mat[i]; pads[p].size = sz[i]; pads[p].decay = 0.95;
            pads[p].strikePosition = 0.20; pads[p].level = lvl[i];
            pads[p].frictionPressure = fri[i];
            pads[p].modeStretch = 0.44; pads[p].decaySkew = skw[i];
            pads[p].nonlinearCoupling = nlc[i];
            pads[p].modeScatter = sct[i];
            pads[p].modeInjectAmount = 0.0; // keep inharmonic
            pads[p].morphEnabled = 1.0; pads[p].morphStart = 0.78; pads[p].morphEnd = 0.55;
            pads[p].morphDuration = 0.85; pads[p].morphCurve = 0.0; // exp
            pads[p].noiseLayerMix = 0.10; pads[p].noiseLayerColor = 0.40; // pink
            pads[p].clickLayerMix = 0.0;
            pads[p].bodyDampingB1 = 0.30;
            pads[p].macroComplexity = 0.85;
            pads[p].outputBus = 1; pads[p].pan = pan[i];
        }
    }

    // --- Ghost Tones 20,22 (Bell/Mallet) + 21 (String/FMImpulse) + 23 (String/Friction) ---
    {
        const int    pd[]  = {20, 22, 21, 23};
        const int    body[]= {BodyModelType::Bell, BodyModelType::Bell,
                              BodyModelType::String, BodyModelType::String};
        const int    exc[] = {ExciterType::Mallet, ExciterType::Mallet,
                              ExciterType::FMImpulse, ExciterType::Friction};
        const double mat[] = {0.50, 0.55, 0.50, 0.50};
        const double sz[]  = {0.50, 0.30, 0.45, 0.50};
        const double str[] = {0.71, 0.89, 0.71, 0.74};
        const double hpc[] = {0.24, 0.30, 0.24, 0.26};
        const double pan[] = {0.14, 0.62, 0.38, 0.86};
        for (int i = 0; i < 4; ++i) {
            const int p = pd[i];
            pads[p].exciterType = exc[i];
            pads[p].bodyModel = body[i];
            pads[p].material = mat[i]; pads[p].size = sz[i]; pads[p].decay = 0.80;
            pads[p].strikePosition = 0.30; pads[p].level = 0.64;
            pads[p].modeStretch = str[i]; pads[p].decaySkew = 0.81;
            pads[p].nonlinearCoupling = 0.32;
            pads[p].modeScatter = 0.45;
            pads[p].modeInjectAmount = 0.0; // keep inharmonic skeleton
            pads[p].tsFilterType = FilterType::HP;
            pads[p].tsFilterCutoff = hpc[i]; pads[p].tsFilterResonance = 0.30;
            pads[p].tsFilterEnvAmount = 0.45;
            pads[p].morphEnabled = 1.0; pads[p].morphStart = 0.45; pads[p].morphEnd = 0.80;
            pads[p].morphDuration = 0.75; pads[p].morphCurve = 0.5; // lin
            pads[p].noiseLayerMix = 0.12; pads[p].noiseLayerColor = 0.12; // brown
            pads[p].noiseLayerCutoff = 0.35; pads[p].noiseLayerDecay = 0.85;
            pads[p].clickLayerMix = 0.12;
            pads[p].bodyDampingB1 = 0.35;
            pads[p].couplingAmount = 0.85;
            pads[p].outputBus = 1; pads[p].pan = pan[i];
        }
        pads[21].fmRatio = 0.40;          // c:m 2.2
        pads[23].frictionPressure = 0.30;
    }

    // --- Tubular Bell / Chimes 24-27 (String/Mallet) ---
    {
        const double mat[] = {0.85, 0.86, 0.87, 0.88};
        const double sz[]  = {0.62, 0.54, 0.46, 0.38};
        const double dec[] = {0.90, 0.91, 0.91, 0.92};
        const double cbr[] = {0.65, 0.67, 0.70, 0.72};
        const double pan[] = {0.20, 0.40, 0.60, 0.80};
        for (int i = 0; i < 4; ++i) {
            const int p = 24 + i;
            pads[p].exciterType = ExciterType::Mallet;
            pads[p].bodyModel = BodyModelType::String;
            pads[p].material = mat[i]; pads[p].size = sz[i]; pads[p].decay = dec[i];
            pads[p].strikePosition = 0.30; pads[p].level = 0.70;
            pads[p].clickLayerMix = 0.40; pads[p].clickLayerContactMs = 0.20;
            pads[p].clickLayerBrightness = cbr[i];
            pads[p].noiseLayerMix = 0.10;
            pads[p].bodyDampingB1 = 0.30;
            pads[p].couplingAmount = 0.85;
            pads[p].pan = pan[i];
        }
    }

    // --- Gong / Tam-Tam 28-29 (Bell/Mallet, bloom + sub-octave shell) ---
    {
        const int    pd[]  = {28, 29};
        const double sz[]  = {0.85, 0.80};
        const double str[] = {0.62, 0.58};
        for (int i = 0; i < 2; ++i) {
            const int p = pd[i];
            pads[p].exciterType = ExciterType::Mallet;
            pads[p].material = 0.60; pads[p].size = sz[i]; pads[p].decay = 0.95;
            pads[p].strikePosition = 0.35; pads[p].level = 0.66;
            pads[p].modeStretch = str[i]; pads[p].decaySkew = 0.62;
            pads[p].nonlinearCoupling = 0.55; // bloom stand-in
            pads[p].modeScatter = 0.47;
            pads[p].couplingStrength = 0.95; pads[p].secondaryEnabled = 1.0;
            pads[p].secondarySize = 0.40; pads[p].secondaryMaterial = 0.80; // sub-octave metal
            pads[p].couplingAmount = 0.85;
            pads[p].morphEnabled = 1.0; pads[p].morphStart = 0.85; pads[p].morphEnd = 0.55;
            pads[p].morphDuration = 0.30; pads[p].morphCurve = 0.5; // lin
            pads[p].noiseLayerMix = 0.20; pads[p].noiseLayerColor = 0.92; // violet
            pads[p].noiseLayerCutoff = 0.85;
            pads[p].clickLayerMix = 0.15;
            pads[p].bodyDampingB1 = 0.28;
            pads[p].pan = 0.50;
        }
    }

    // --- Temple Bells 30-31 (Bell/Mallet, 1/k hum delta at nominal) ---
    {
        const int    pd[]  = {30, 31};
        const double mat[] = {0.82, 0.85};
        const double sz[]  = {0.50, 0.38};
        const double lvl[] = {0.78, 0.76};
        const double pan[] = {0.38, 0.62};
        for (int i = 0; i < 2; ++i) {
            const int p = pd[i];
            pads[p].exciterType = ExciterType::Mallet;
            pads[p].material = mat[i]; pads[p].size = sz[i]; pads[p].decay = 0.92;
            pads[p].strikePosition = 0.18; pads[p].level = lvl[i];
            pads[p].modeStretch = 0.40; pads[p].decaySkew = 0.78;
            pads[p].modeInjectAmount = 0.22; // kit-character hum delta (overrides recipe)
            pads[p].modeScatter = 0.06;
            pads[p].bodyDampingB1 = 0.18; pads[p].bodyDampingB3 = 0.06;
            pads[p].clickLayerMix = 0.28; pads[p].clickLayerContactMs = 0.45;
            pads[p].clickLayerBrightness = 0.42;
            pads[p].noiseLayerMix = 0.04;
            pads[p].pan = pan[i];
        }
    }

    k.opts.maxPolyphony    = 8;
    k.opts.stealingPolicy  = 1;
    k.opts.globalCoupling  = 0.85;
    k.opts.tomResonance    = 0.65;
    k.opts.couplingDelayMs = 1.9;
    for (int i = 0; i < kNumPads; ++i) k.crafted.push_back(i);
    return k;
}

Kit chaosEngineKit() {
    Kit k{"Chaos Engine", "Unnatural", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // 18 individually-crafted max-instability voices (was one fmod-ramped
    // loop). tensionMod/airLoading are Membrane-only (kick only). modeInject
    // finally used on the ghost/algorithmic pads (16/17). Per-pad pan + 4
    // output buses + choke groups place the field.

    // Pad 0 — Chaos Kick (Membrane/FMImpulse).
    pads[0].exciterType = ExciterType::FMImpulse;
    pads[0].bodyModel = BodyModelType::Membrane;
    pads[0].material = 0.20; pads[0].size = 0.85; pads[0].decay = 0.45;
    pads[0].level = 0.70; pads[0].fmRatio = 0.55;
    pads[0].tsPitchEnvStart = toLogNorm(220); pads[0].tsPitchEnvEnd = toLogNorm(40);
    pads[0].tsPitchEnvTime = 0.05; pads[0].tsPitchEnvCurve = 0.15;
    pads[0].tsDriveAmount = 0.25; pads[0].tsFoldAmount = 0.30;
    pads[0].tensionModAmt = 0.50; pads[0].airLoading = 0.40; // Membrane-only
    pads[0].modeStretch = 0.45; pads[0].decaySkew = 0.45;
    pads[0].nonlinearCoupling = 0.40;
    pads[0].clickLayerMix = 0.35; pads[0].clickLayerContactMs = 0.15;
    pads[0].noiseLayerMix = 0.10;
    pads[0].bodyDampingB1 = 0.30; pads[0].bodyDampingB3 = 0.30;
    pads[0].macroPunch = 0.85; pads[0].macroComplexity = 0.85;
    pads[0].couplingAmount = 0.78; pads[0].pan = 0.50;

    // Pad 1 — Metal snare-squeal (Shell/Feedback).
    pads[1].exciterType = ExciterType::Feedback;
    pads[1].bodyModel = BodyModelType::Shell;
    pads[1].material = 0.70; pads[1].size = 0.45; pads[1].decay = 0.40;
    pads[1].level = 0.66; pads[1].feedbackAmount = 0.55;
    pads[1].tsPitchEnvStart = toLogNorm(600); pads[1].tsPitchEnvEnd = toLogNorm(300);
    pads[1].tsPitchEnvTime = 0.04; pads[1].tsPitchEnvCurve = 0.15; // fast-exp chirp
    pads[1].modeStretch = 0.55; pads[1].decaySkew = 0.60;
    pads[1].nonlinearCoupling = 0.45; pads[1].modeScatter = 0.55;
    pads[1].couplingStrength = 0.30; pads[1].secondaryEnabled = 1.0;
    pads[1].secondarySize = 0.65; pads[1].secondaryMaterial = 0.85; // ~217 Hz
    pads[1].tsFilterType = FilterType::BP;
    pads[1].tsFilterCutoff = 0.60; pads[1].tsFilterResonance = 0.50;
    pads[1].tsFilterEnvAmount = 0.45;
    pads[1].noiseLayerMix = 0.40; pads[1].noiseLayerColor = 0.70; // white
    pads[1].noiseLayerCutoff = 0.85; pads[1].noiseLayerDecay = 0.40;
    pads[1].clickLayerMix = 0.30; pads[1].clickLayerBrightness = 0.85;
    pads[1].bodyDampingB1 = 0.30; pads[1].bodyDampingB3 = 0.20;
    pads[1].macroComplexity = 0.85; pads[1].couplingAmount = 0.78; pads[1].pan = 0.40;

    // Pad 2 — Plate snare-crack (Plate/FMImpulse, inharmonic-plate-tom).
    pads[2].exciterType = ExciterType::FMImpulse;
    pads[2].bodyModel = BodyModelType::Plate;
    pads[2].material = 0.55; pads[2].size = 0.50; pads[2].decay = 0.30;
    pads[2].strikePosition = 0.62; pads[2].level = 0.68; pads[2].fmRatio = 0.45;
    pads[2].tsPitchEnvStart = toLogNorm(300); pads[2].tsPitchEnvEnd = toLogNorm(120);
    pads[2].tsPitchEnvTime = 0.05; pads[2].tsPitchEnvCurve = 0.15;
    pads[2].modeStretch = 0.50; pads[2].decaySkew = 0.40; pads[2].modeScatter = 0.50;
    pads[2].nonlinearCoupling = 0.40;
    pads[2].couplingStrength = 0.30; pads[2].secondaryEnabled = 1.0;
    pads[2].secondarySize = 0.40; pads[2].secondaryMaterial = 0.75;
    pads[2].noiseLayerMix = 0.18; pads[2].noiseLayerColor = 0.70;
    pads[2].clickLayerMix = 0.40; pads[2].clickLayerBrightness = 0.85;
    pads[2].bodyDampingB1 = 0.40; pads[2].bodyDampingB3 = 0.30;
    pads[2].macroComplexity = 0.75; pads[2].couplingAmount = 0.65; pads[2].pan = 0.60;

    // Pad 3 — Chaos core A (Plate/Feedback, short clang).
    pads[3].exciterType = ExciterType::Feedback;
    pads[3].bodyModel = BodyModelType::Plate;
    pads[3].material = 0.50; pads[3].size = 0.45; pads[3].decay = 0.40;
    pads[3].strikePosition = 0.50; pads[3].level = 0.68; pads[3].feedbackAmount = 0.55;
    pads[3].modeStretch = 0.60; pads[3].decaySkew = 0.50; pads[3].modeScatter = 0.65;
    pads[3].nonlinearCoupling = 0.70;
    pads[3].tsDriveAmount = 0.35; pads[3].tsFoldAmount = 0.30;
    pads[3].tsFilterType = FilterType::BP;
    pads[3].tsFilterCutoff = 0.50; pads[3].tsFilterResonance = 0.55;
    pads[3].tsFilterEnvAmount = 0.55; pads[3].tsFilterEnvAttack = 0.05;
    pads[3].tsFilterEnvDecay = 0.20;
    pads[3].couplingStrength = 0.40; pads[3].secondaryEnabled = 1.0;
    pads[3].secondarySize = 0.40; pads[3].secondaryMaterial = 0.60;
    pads[3].noiseLayerMix = 0.25; pads[3].clickLayerMix = 0.30;
    pads[3].bodyDampingB1 = 0.35; pads[3].bodyDampingB3 = 0.20;
    pads[3].macroComplexity = 0.85; pads[3].couplingAmount = 0.78; pads[3].pan = 0.35;

    // Pad 4 — Chaos core B (Bell/FMImpulse).
    pads[4].exciterType = ExciterType::FMImpulse;
    pads[4].bodyModel = BodyModelType::Bell;
    pads[4].material = 0.60; pads[4].size = 0.35; pads[4].decay = 0.50;
    pads[4].level = 0.68; pads[4].fmRatio = 0.62;
    pads[4].modeStretch = 0.60; pads[4].decaySkew = 0.55; pads[4].modeScatter = 0.60;
    pads[4].nonlinearCoupling = 0.50;
    pads[4].tsDriveAmount = 0.30; pads[4].tsFoldAmount = 0.25;
    pads[4].noiseLayerMix = 0.20; pads[4].clickLayerMix = 0.30;
    pads[4].clickLayerBrightness = 0.80;
    pads[4].bodyDampingB1 = 0.30; pads[4].bodyDampingB3 = 0.0;
    pads[4].macroComplexity = 0.85; pads[4].couplingAmount = 0.78; pads[4].pan = 0.65;

    // Pad 5 — Chaos core C (NoiseBody/Friction).
    pads[5].exciterType = ExciterType::Friction;
    pads[5].bodyModel = BodyModelType::NoiseBody;
    pads[5].material = 0.55; pads[5].size = 0.40; pads[5].decay = 0.45;
    pads[5].strikePosition = 0.45; pads[5].level = 0.68; pads[5].frictionPressure = 0.45;
    pads[5].modeStretch = 0.60; pads[5].decaySkew = 0.55; pads[5].modeScatter = 0.65;
    pads[5].nonlinearCoupling = 0.65;
    pads[5].tsDriveAmount = 0.30; pads[5].tsFoldAmount = 0.30;
    pads[5].noiseLayerMix = 0.35; pads[5].noiseLayerCutoff = 0.50;
    pads[5].noiseLayerColor = 0.50; pads[5].noiseLayerDecay = 0.50;
    pads[5].clickLayerMix = 0.20;
    pads[5].bodyDampingB1 = 0.35; pads[5].bodyDampingB3 = 0.20;
    pads[5].macroComplexity = 0.85; pads[5].couplingAmount = 0.78; pads[5].pan = 0.45;

    // Pads 6/7/8 — Chaos hats (Bell/FMImpulse, choke 1).
    {
        const double sz[]  = {0.12, 0.10, 0.16};
        const double dec[] = {0.06, 0.04, 0.40};
        const double b1[]  = {0.65, 0.78, 0.30};
        const double ndc[] = {0.06, 0.04, 0.40};
        for (int i = 0; i < 3; ++i) {
            const int p = 6 + i;
            pads[p].exciterType = ExciterType::FMImpulse;
            pads[p].bodyModel = BodyModelType::Bell;
            pads[p].material = 0.92; pads[p].size = sz[i]; pads[p].decay = dec[i];
            pads[p].level = 0.66; pads[p].chokeGroup = 1; pads[p].fmRatio = 0.70;
            pads[p].modeStretch = 0.55; pads[p].decaySkew = 0.58; pads[p].modeScatter = 0.55;
            pads[p].noiseLayerMix = 0.40; pads[p].noiseLayerColor = 0.92; // violet
            pads[p].noiseLayerCutoff = 0.92; pads[p].noiseLayerDecay = ndc[i];
            pads[p].clickLayerMix = 0.20; pads[p].clickLayerBrightness = 0.92;
            pads[p].bodyDampingB1 = b1[i]; pads[p].bodyDampingB3 = 0.0;
            pads[p].pan = 0.60;
        }
    }

    // Pads 9/10/11 — Plate toms LO/MID/HI (Plate/FMImpulse, inharmonic-plate-tom).
    {
        const double sz[]  = {0.60, 0.48, 0.38};
        const double mat[] = {0.45, 0.55, 0.65};
        const double dec[] = {0.45, 0.40, 0.35};
        const double fmr[] = {0.40, 0.50, 0.60};
        const double hi[]  = {280, 360, 460};
        const double lo[]  = {90, 130, 165};
        const double b1[]  = {0.40, 0.40, 0.42};
        const double pan[] = {0.30, 0.50, 0.70};
        for (int i = 0; i < 3; ++i) {
            const int p = 9 + i;
            pads[p].exciterType = ExciterType::FMImpulse;
            pads[p].bodyModel = BodyModelType::Plate;
            pads[p].material = mat[i]; pads[p].size = sz[i]; pads[p].decay = dec[i];
            pads[p].strikePosition = 0.62; pads[p].level = 0.72; pads[p].fmRatio = fmr[i];
            pads[p].tsPitchEnvStart = toLogNorm(hi[i]); pads[p].tsPitchEnvEnd = toLogNorm(lo[i]);
            pads[p].tsPitchEnvTime = 0.06; pads[p].tsPitchEnvCurve = 0.15;
            pads[p].modeStretch = 0.50; pads[p].decaySkew = 0.40; pads[p].modeScatter = 0.45;
            pads[p].nonlinearCoupling = 0.40;
            pads[p].couplingStrength = 0.30; pads[p].secondaryEnabled = 1.0;
            pads[p].secondarySize = 0.40; pads[p].secondaryMaterial = 0.75;
            pads[p].noiseLayerMix = 0.18; pads[p].noiseLayerColor = 0.70;
            pads[p].clickLayerMix = 0.30; pads[p].clickLayerBrightness = 0.65;
            pads[p].bodyDampingB1 = b1[i]; pads[p].bodyDampingB3 = 0.30;
            pads[p].macroComplexity = 0.75; pads[p].couplingAmount = 0.65;
            pads[p].pan = pan[i];
        }
    }

    // Pad 12 — Feedback plate drone (Plate/Feedback, aux 1, choke 2, BP bloom).
    pads[12].exciterType = ExciterType::Feedback;
    pads[12].bodyModel = BodyModelType::Plate;
    pads[12].material = 0.62; pads[12].size = 0.60; pads[12].decay = 0.88;
    pads[12].strikePosition = 0.45; pads[12].level = 0.62; pads[12].feedbackAmount = 0.55;
    pads[12].tsFoldAmount = 0.25;
    pads[12].modeStretch = 0.50; pads[12].modeScatter = 0.35;
    pads[12].modeInjectAmount = 0.15; pads[12].nonlinearCoupling = 0.50;
    pads[12].decaySkew = 0.78;
    pads[12].tsFilterType = FilterType::BP;
    pads[12].tsFilterCutoff = 0.55; pads[12].tsFilterResonance = 0.45;
    pads[12].tsFilterEnvAmount = 0.65; pads[12].tsFilterEnvAttack = 0.45;
    pads[12].tsFilterEnvDecay = 0.60; pads[12].tsFilterEnvSustain = 0.55;
    pads[12].tsFilterEnvRelease = 0.65;
    pads[12].noiseLayerMix = 0.30; pads[12].noiseLayerColor = 0.70;
    pads[12].noiseLayerCutoff = 0.55; pads[12].noiseLayerDecay = 0.85;
    pads[12].clickLayerMix = 0.0;
    pads[12].bodyDampingB1 = 0.20; pads[12].bodyDampingB3 = 0.20;
    pads[12].chokeGroup = 2; pads[12].outputBus = 1;
    pads[12].macroComplexity = 0.80; pads[12].couplingAmount = 0.85; pads[12].pan = 0.30;

    // Pad 13 — Feedback shell drone (Shell/Feedback, aux 2, choke 2).
    pads[13] = pads[12];
    pads[13].bodyModel = BodyModelType::Shell;
    pads[13].size = 0.55; pads[13].outputBus = 2; pads[13].pan = 0.70;

    // Pad 14 — Singing-bowl drone (Bell/Friction, aux 3, choke 2).
    pads[14].exciterType = ExciterType::Friction;
    pads[14].bodyModel = BodyModelType::Bell;
    pads[14].material = 0.85; pads[14].size = 0.50; pads[14].decay = 0.95;
    pads[14].strikePosition = 0.20; pads[14].level = 0.72; pads[14].frictionPressure = 0.30;
    pads[14].modeStretch = 0.44; pads[14].decaySkew = 0.86;
    pads[14].nonlinearCoupling = 0.25; pads[14].modeScatter = 0.06;
    pads[14].modeInjectAmount = 0.0; // keep inharmonic
    pads[14].morphEnabled = 1.0; pads[14].morphStart = 0.78; pads[14].morphEnd = 0.55;
    pads[14].morphDuration = 0.85; pads[14].morphCurve = 0.0; // exp
    pads[14].noiseLayerMix = 0.10; pads[14].noiseLayerColor = 0.40; // pink
    pads[14].clickLayerMix = 0.0;
    pads[14].bodyDampingB1 = 0.30; pads[14].bodyDampingB3 = 0.0;
    pads[14].chokeGroup = 2; pads[14].outputBus = 3;
    pads[14].macroComplexity = 0.85; pads[14].couplingAmount = 0.85; pads[14].pan = 0.50;

    // Pad 15 — Friction string drone (String/Friction, aux 1). modeStretch/
    // decaySkew/scatter/airLoading/b1/b3/tension are inherent String no-ops.
    pads[15].exciterType = ExciterType::Friction;
    pads[15].bodyModel = BodyModelType::String;
    pads[15].material = 0.55; pads[15].size = 0.50; pads[15].decay = 0.92;
    pads[15].level = 0.62; pads[15].frictionPressure = 0.50;
    pads[15].nonlinearCoupling = 0.38;
    pads[15].tsFilterType = FilterType::LP;
    pads[15].tsFilterCutoff = 0.48; pads[15].tsFilterResonance = 0.35;
    pads[15].tsFilterEnvAmount = 0.62; pads[15].tsFilterEnvAttack = 0.20;
    pads[15].tsFilterEnvDecay = 0.40; pads[15].tsFilterEnvSustain = 0.55;
    pads[15].tsFilterEnvRelease = 0.65;
    pads[15].tsDriveAmount = 0.26; pads[15].tsFoldAmount = 0.22;
    pads[15].couplingStrength = 0.18; pads[15].secondaryEnabled = 1.0;
    pads[15].secondarySize = 0.50; pads[15].secondaryMaterial = 0.65;
    pads[15].noiseLayerMix = 0.16; pads[15].noiseLayerColor = 0.12; // brown
    pads[15].noiseLayerDecay = 0.85; pads[15].clickLayerMix = 0.0;
    pads[15].outputBus = 1; pads[15].macroComplexity = 0.85;
    pads[15].couplingAmount = 0.85; pads[15].pan = 0.40;

    // Pad 16 — Ghost bell (Bell/FMImpulse, aux 1) — modeInject 0.20 chaos delta.
    pads[16].exciterType = ExciterType::FMImpulse;
    pads[16].bodyModel = BodyModelType::Bell;
    pads[16].material = 0.50; pads[16].size = 0.40; pads[16].decay = 0.80;
    pads[16].strikePosition = 0.30; pads[16].level = 0.64; pads[16].fmRatio = 0.40;
    pads[16].modeStretch = 0.71; pads[16].decaySkew = 0.81; pads[16].modeScatter = 0.45;
    pads[16].modeInjectAmount = 0.20; pads[16].nonlinearCoupling = 0.32;
    pads[16].tsFilterType = FilterType::HP;
    pads[16].tsFilterCutoff = 0.24; pads[16].tsFilterResonance = 0.30;
    pads[16].tsFilterEnvAmount = 0.45;
    pads[16].morphEnabled = 1.0; pads[16].morphStart = 0.45; pads[16].morphEnd = 0.80;
    pads[16].morphDuration = 0.75; pads[16].morphCurve = 0.5; // lin
    pads[16].noiseLayerMix = 0.12; pads[16].noiseLayerColor = 0.12; // brown
    pads[16].noiseLayerCutoff = 0.35; pads[16].noiseLayerDecay = 0.85;
    pads[16].clickLayerMix = 0.12;
    pads[16].bodyDampingB1 = 0.35; pads[16].bodyDampingB3 = 0.0;
    pads[16].outputBus = 1; pads[16].couplingAmount = 0.85; pads[16].pan = 0.60;

    // Pad 17 — FM-bell modeInject hybrid (Bell/FMImpulse, aux 2, Algorithmic FX).
    pads[17].exciterType = ExciterType::FMImpulse;
    pads[17].bodyModel = BodyModelType::Bell;
    pads[17].material = 0.60; pads[17].size = 0.30; pads[17].decay = 0.60;
    pads[17].strikePosition = 0.30; pads[17].level = 0.66; pads[17].fmRatio = 0.65;
    pads[17].modeStretch = 0.60; pads[17].decaySkew = 0.55; pads[17].modeScatter = 0.55;
    pads[17].modeInjectAmount = 0.25; pads[17].nonlinearCoupling = 0.45;
    pads[17].tsDriveAmount = 0.30; pads[17].tsFoldAmount = 0.25;
    pads[17].noiseLayerMix = 0.20; pads[17].clickLayerMix = 0.25;
    pads[17].clickLayerBrightness = 0.80;
    pads[17].bodyDampingB1 = 0.30; pads[17].bodyDampingB3 = 0.0;
    pads[17].outputBus = 2; pads[17].macroComplexity = 0.85;
    pads[17].couplingAmount = 0.78; pads[17].pan = 0.50;

    k.opts.maxPolyphony    = 12;
    k.opts.stealingPolicy  = 2;
    k.opts.globalCoupling  = 0.80;
    k.opts.snareBuzz       = 0.55;
    k.opts.tomResonance    = 0.78;
    k.opts.couplingDelayMs = 1.7;
    for (int i = 0; i < 18; ++i) k.crafted.push_back(i);
    return k;
}

Kit ghostBonesKit() {
    Kit k{"Ghost Bones", "Unnatural", defaultPads(), {}, {}};
    auto& pads = k.pads;

    // 16-voice register-graded choir (low->high f0). Bell pads (even) run the
    // live modal bank (Stretch/Skew/Scatter/b1/b3); String pads (odd) bypass it
    // (those axes are inherent no-ops, left unset). modeStretch 0.66-0.88 is the
    // kit signature. Shared HP filter, Brown noise, long decays, Material morph.

    // --- Bell pads (modal) ---
    {
        const int    pd[]   = {0, 2, 4, 6, 8, 9, 10, 12, 14};
        const int    exc[]  = {ExciterType::Mallet, ExciterType::Impulse,
                              ExciterType::FMImpulse, ExciterType::Mallet,
                              ExciterType::FMImpulse, ExciterType::FMImpulse,
                              ExciterType::Mallet, ExciterType::Impulse,
                              ExciterType::Mallet};
        const double mat[]  = {0.42, 0.46, 0.50, 0.52, 0.55, 0.58, 0.60, 0.62, 0.64};
        const double sz[]   = {0.78, 0.66, 0.55, 0.46, 0.38, 0.34, 0.30, 0.24, 0.20};
        const double dec[]  = {0.85, 0.82, 0.82, 0.80, 0.78, 0.78, 0.76, 0.74, 0.72};
        const double stk[]  = {0.28, 0.32, 0.30, 0.34, 0.30, 0.36, 0.32, 0.34, 0.30};
        const double cut[]  = {0.16, 0.24, 0.30, 0.36, 0.42, 0.44, 0.46, 0.48, 0.48};
        const double str[]  = {0.66, 0.72, 0.78, 0.80, 0.84, 0.88, 0.82, 0.86, 0.84};
        const double skw[]  = {0.82, 0.80, 0.80, 0.78, 0.78, 0.80, 0.78, 0.78, 0.78};
        const double nlc[]  = {0.28, 0.30, 0.34, 0.32, 0.36, 0.40, 0.32, 0.34, 0.36};
        const double sct[]  = {0.40, 0.50, 0.55, 0.60, 0.65, 0.70, 0.55, 0.60, 0.68};
        const double fmr[]  = {-1,   -1,   0.40, -1,   0.50, 0.47, -1,   -1,   -1};
        const double mOn[]  = {0,    1,    1,    1,    1,    1,    1,    1,    1};
        const double mSt[]  = {0,    0.45, 0.45, 0.45, 0.45, 0.50, 0.45, 0.50, 0.50};
        const double mEn[]  = {0,    0.80, 0.80, 0.80, 0.82, 0.82, 0.80, 0.82, 0.85};
        const double nmx[]  = {0.10, 0.12, 0.12, 0.12, 0.11, 0.10, 0.11, 0.10, 0.10};
        const double nct[]  = {0.30, 0.35, 0.35, 0.40, 0.40, 0.40, 0.40, 0.40, 0.40};
        const double clk[]  = {0.10, 0.14, 0.08, 0.12, 0.08, 0.08, 0.10, 0.14, 0.10};
        const double cbr[]  = {0.55, 0.62, -1,   0.66, -1,   -1,   0.66, 0.70, 0.70};
        const double b1[]   = {0.33, 0.35, 0.35, 0.36, 0.37, 0.37, 0.36, 0.36, 0.36};
        const double pan[]  = {0.50, 0.65, 0.55, 0.70, 0.50, 0.80, 0.40, 0.60, 0.30};
        for (int i = 0; i < 9; ++i) {
            const int p = pd[i];
            pads[p].exciterType = exc[i];
            pads[p].bodyModel = BodyModelType::Bell;
            pads[p].material = mat[i]; pads[p].size = sz[i]; pads[p].decay = dec[i];
            pads[p].strikePosition = stk[i]; pads[p].level = 0.64;
            pads[p].modeStretch = str[i]; pads[p].decaySkew = skw[i];
            pads[p].nonlinearCoupling = nlc[i]; pads[p].modeScatter = sct[i];
            pads[p].modeInjectAmount = 0.0;
            if (fmr[i] >= 0.0) pads[p].fmRatio = fmr[i];
            pads[p].tsFilterType = FilterType::HP;
            pads[p].tsFilterCutoff = cut[i]; pads[p].tsFilterResonance = 0.30;
            pads[p].tsFilterEnvAmount = 0.45; pads[p].tsFilterEnvDecay = 0.30;
            if (mOn[i] != 0.0) {
                pads[p].morphEnabled = 1.0; pads[p].morphStart = mSt[i];
                pads[p].morphEnd = mEn[i]; pads[p].morphDuration = 0.70;
                pads[p].morphCurve = 0.4; // linear
            }
            pads[p].noiseLayerMix = nmx[i]; pads[p].noiseLayerColor = 0.20; // brown
            pads[p].noiseLayerCutoff = nct[i]; pads[p].noiseLayerDecay = 0.85;
            pads[p].clickLayerMix = clk[i];
            if (cbr[i] >= 0.0) pads[p].clickLayerBrightness = cbr[i];
            pads[p].bodyDampingB1 = b1[i]; pads[p].bodyDampingB3 = 0.0;
            pads[p].airLoading = 0.0;
            pads[p].macroComplexity = 0.85; pads[p].couplingAmount = 0.85;
            pads[p].pan = pan[i];
        }
    }

    // --- String pads (waveguide — modal-bank axes are inherent no-ops, unset) ---
    {
        const int    pd[]   = {1, 3, 5, 7, 11, 13, 15};
        const int    exc[]  = {ExciterType::Friction, ExciterType::FMImpulse,
                              ExciterType::Mallet, ExciterType::Impulse,
                              ExciterType::Friction, ExciterType::Impulse,
                              ExciterType::Mallet};
        const double mat[]  = {0.40, 0.42, 0.44, 0.46, 0.50, 0.52, 0.55};
        const double sz[]   = {0.72, 0.60, 0.50, 0.42, 0.26, 0.22, 0.20};
        const double dec[]  = {0.88, 0.84, 0.83, 0.82, 0.86, 0.80, 0.82};
        const double stk[]  = {0.35, 0.38, 0.40, 0.42, 0.40, 0.45, 0.50};
        const double cut[]  = {0.20, 0.28, 0.34, 0.40, 0.40, 0.44, 0.46};
        const double fea[]  = {0.60, 0.45, 0.50, 0.45, 0.60, 0.50, 0.50};
        const double fed[]  = {0.45, 0.30, 0.30, 0.30, 0.45, 0.30, 0.30};
        const double nlc[]  = {0.40, 0.38, 0.36, 0.40, 0.42, 0.38, 0.40};
        const double fmr[]  = {-1,   0.45, -1,   -1,   -1,   -1,   -1};
        const double fri[]  = {0.35, -1,   -1,   -1,   0.40, -1,   -1};
        const double mSt[]  = {0.45, 0.45, 0.45, 0.45, 0.45, 0.45, 0.50};
        const double mEn[]  = {0.75, 0.78, 0.78, 0.80, 0.78, 0.80, 0.82};
        const double mDu[]  = {0.65, 0.68, 0.66, 0.68, 0.66, 0.64, 0.64};
        const double nmx[]  = {0.16, 0.14, 0.13, 0.14, 0.16, 0.12, 0.12};
        const double nct[]  = {0.40, 0.40, 0.40, 0.45, 0.45, 0.45, 0.45};
        const double clk[]  = {0.0,  0.08, 0.12, 0.14, 0.0,  0.16, 0.14};
        const int    bus[]  = {0,    1,    0,    1,    1,    0,    1};
        const double pan[]  = {0.35, 0.25, 0.45, 0.30, 0.60, 0.50, 0.70};
        for (int i = 0; i < 7; ++i) {
            const int p = pd[i];
            pads[p].exciterType = exc[i];
            pads[p].bodyModel = BodyModelType::String;
            pads[p].material = mat[i]; pads[p].size = sz[i]; pads[p].decay = dec[i];
            pads[p].strikePosition = stk[i]; pads[p].level = (i == 4) ? 0.60 : 0.62;
            pads[p].nonlinearCoupling = nlc[i];
            if (fmr[i] >= 0.0) pads[p].fmRatio = fmr[i];
            if (fri[i] >= 0.0) pads[p].frictionPressure = fri[i];
            pads[p].tsFilterType = FilterType::HP;
            pads[p].tsFilterCutoff = cut[i]; pads[p].tsFilterResonance = 0.30;
            pads[p].tsFilterEnvAmount = fea[i]; pads[p].tsFilterEnvDecay = fed[i];
            pads[p].morphEnabled = 1.0; pads[p].morphStart = mSt[i];
            pads[p].morphEnd = mEn[i]; pads[p].morphDuration = mDu[i];
            pads[p].morphCurve = 0.4; // linear
            pads[p].noiseLayerMix = nmx[i]; pads[p].noiseLayerColor = 0.20; // brown
            pads[p].noiseLayerCutoff = nct[i]; pads[p].noiseLayerDecay = 0.85;
            pads[p].clickLayerMix = clk[i];
            pads[p].macroComplexity = 0.85; pads[p].couplingAmount = 0.85;
            pads[p].outputBus = bus[i]; pads[p].pan = pan[i];
            // Mode Stretch/Skew/Scatter/Inject, b1/b3, airLoading, tensionMod
            // left unset — inherent no-ops on the WaveguideString body.
        }
    }

    k.opts.maxPolyphony    = 12;
    k.opts.globalCoupling  = 0.78;
    k.opts.tomResonance    = 0.55;
    k.opts.couplingDelayMs = 2.0;
    for (int i = 0; i < 16; ++i) k.crafted.push_back(i);
    return k;
}
