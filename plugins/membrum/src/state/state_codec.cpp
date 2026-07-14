// ==============================================================================
// Membrum state codec -- implementation.
// ==============================================================================

#include "state/state_codec.h"

#include "base/source/fstreamer.h"

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace Membrum::State {

using Steinberg::int32;
using Steinberg::tresult;
using Steinberg::kResultOk;
using Steinberg::kResultFalse;
using Steinberg::IBStream;
using Steinberg::IBStreamer;

namespace {

// Audit finding 13: serialise every multibyte value through IBStreamer with an
// explicit little-endian byte order, matching all five sibling plugins. The
// previous raw IBStream::read/write of sizeof(T) bytes was non-portable across
// host endianness. On a little-endian host the on-wire bytes are identical, so
// existing states/presets remain loadable.
//
// readT treats a short/failed read as a failure; writeT is best-effort (the
// callers report kResultOk regardless, matching the legacy behaviour).
template <typename T>
bool readT(IBStreamer& s, T& dst) noexcept
{
    if constexpr (std::is_same_v<T, double>)
        return s.readDouble(dst);
    else if constexpr (std::is_same_v<T, Steinberg::int32>)
        return s.readInt32(dst);
    else if constexpr (std::is_same_v<T, std::uint8_t>)
        return s.readInt8u(dst);
    else
        static_assert(sizeof(T) == 0, "readT: unsupported codec type");
}

template <typename T>
void writeT(IBStreamer& s, const T& src) noexcept
{
    if constexpr (std::is_same_v<T, double>)
        s.writeDouble(src);
    else if constexpr (std::is_same_v<T, Steinberg::int32>)
        s.writeInt32(src);
    else if constexpr (std::is_same_v<T, std::uint8_t>)
        s.writeInt8u(src);
    else
        static_assert(sizeof(T) == 0, "writeT: unsupported codec type");
}

} // namespace

// ============================================================================
// Pad <-> Snapshot bridging
// ============================================================================

PadSnapshot toPadSnapshot(const PadConfig& cfg) noexcept
{
    PadSnapshot snap;
    snap.exciterType = cfg.exciterType;
    snap.bodyModel   = cfg.bodyModel;

    // Offsets 2-29 (28 values, indices 0-27 in the `sound` array).
    snap.sound[0]  = static_cast<double>(cfg.material);
    snap.sound[1]  = static_cast<double>(cfg.size);
    snap.sound[2]  = static_cast<double>(cfg.decay);
    snap.sound[3]  = static_cast<double>(cfg.strikePosition);
    snap.sound[4]  = static_cast<double>(cfg.level);
    snap.sound[5]  = static_cast<double>(cfg.tsFilterType);
    snap.sound[6]  = static_cast<double>(cfg.tsFilterCutoff);
    snap.sound[7]  = static_cast<double>(cfg.tsFilterResonance);
    snap.sound[8]  = static_cast<double>(cfg.tsFilterEnvAmount);
    snap.sound[9]  = static_cast<double>(cfg.tsDriveAmount);
    snap.sound[10] = static_cast<double>(cfg.tsFoldAmount);
    snap.sound[11] = static_cast<double>(cfg.tsPitchEnvStart);
    snap.sound[12] = static_cast<double>(cfg.tsPitchEnvEnd);
    snap.sound[13] = static_cast<double>(cfg.tsPitchEnvTime);
    snap.sound[14] = static_cast<double>(cfg.tsPitchEnvCurve);
    snap.sound[15] = static_cast<double>(cfg.tsFilterEnvAttack);
    snap.sound[16] = static_cast<double>(cfg.tsFilterEnvDecay);
    snap.sound[17] = static_cast<double>(cfg.tsFilterEnvSustain);
    snap.sound[18] = static_cast<double>(cfg.tsFilterEnvRelease);
    snap.sound[19] = static_cast<double>(cfg.modeStretch);
    snap.sound[20] = static_cast<double>(cfg.decaySkew);
    snap.sound[21] = static_cast<double>(cfg.modeInjectAmount);
    snap.sound[22] = static_cast<double>(cfg.nonlinearCoupling);
    snap.sound[23] = static_cast<double>(cfg.morphEnabled);
    snap.sound[24] = static_cast<double>(cfg.morphStart);
    snap.sound[25] = static_cast<double>(cfg.morphEnd);
    snap.sound[26] = static_cast<double>(cfg.morphDuration);
    snap.sound[27] = static_cast<double>(cfg.morphCurve);

    // Offsets 30-31 (chokeGroup, outputBus) as float64 -- these are also
    // emitted as the authoritative uint8 fields below.
    snap.sound[28] = static_cast<double>(cfg.chokeGroup);
    snap.sound[29] = static_cast<double>(cfg.outputBus);

    // Offsets 32-35.
    snap.sound[30] = static_cast<double>(cfg.fmRatio);
    snap.sound[31] = static_cast<double>(cfg.feedbackAmount);
    snap.sound[32] = static_cast<double>(cfg.noiseBurstDuration);
    snap.sound[33] = static_cast<double>(cfg.frictionPressure);

    // Phase 7: parallel noise layer (offsets 42-46).
    snap.sound[34] = static_cast<double>(cfg.noiseLayerMix);
    snap.sound[35] = static_cast<double>(cfg.noiseLayerCutoff);
    snap.sound[36] = static_cast<double>(cfg.noiseLayerResonance);
    snap.sound[37] = static_cast<double>(cfg.noiseLayerDecay);
    snap.sound[38] = static_cast<double>(cfg.noiseLayerColor);
    // Phase 7: always-on click transient (offsets 47-49).
    snap.sound[39] = static_cast<double>(cfg.clickLayerMix);
    snap.sound[40] = static_cast<double>(cfg.clickLayerContactMs);
    snap.sound[41] = static_cast<double>(cfg.clickLayerBrightness);
    // Phase 8A: per-mode damping law overrides (offsets 50-51). Sentinel
    // -1.0f persists verbatim so round-trip with untouched pads stays
    // bit-identical to the Phase 7 behaviour.
    snap.sound[42] = static_cast<double>(cfg.bodyDampingB1);
    snap.sound[43] = static_cast<double>(cfg.bodyDampingB3);
    // Phase 8C: air-loading + per-mode scatter (offsets 52-53).
    snap.sound[44] = static_cast<double>(cfg.airLoading);
    snap.sound[45] = static_cast<double>(cfg.modeScatter);
    // Phase 8D: head <-> shell coupling (offsets 54-57).
    snap.sound[46] = static_cast<double>(cfg.couplingStrength);
    snap.sound[47] = static_cast<double>(cfg.secondaryEnabled);
    snap.sound[48] = static_cast<double>(cfg.secondarySize);
    snap.sound[49] = static_cast<double>(cfg.secondaryMaterial);
    // Phase 8E: nonlinear tension modulation (offset 58).
    snap.sound[50] = static_cast<double>(cfg.tensionModAmt);
    // Phase 8F: per-pad enable toggle (offset 59).
    snap.sound[51] = static_cast<double>(cfg.enabled);
    // Phase 10: three-point pitch envelope extension (offsets 60-63).
    snap.sound[52] = static_cast<double>(cfg.tsPitchEnvKneeEnabled);
    snap.sound[53] = static_cast<double>(cfg.tsPitchEnvMidPitch);
    snap.sound[54] = static_cast<double>(cfg.tsPitchEnvMidFraction);
    snap.sound[55] = static_cast<double>(cfg.tsPitchEnvCurve2);

    // M-9: per-pad pan (offset 64).
    snap.sound[56] = static_cast<double>(cfg.pan);
    // Snare-body fix: per-pad noise-layer gain multiplier.
    snap.sound[57] = static_cast<double>(cfg.noiseLayerGain);
    // Wire coupling: buzz-follows-body depth.
    snap.sound[58] = static_cast<double>(cfg.wireCoupling);

    snap.chokeGroup     = cfg.chokeGroup;
    snap.outputBus      = cfg.outputBus;
    snap.couplingAmount = static_cast<double>(cfg.couplingAmount);
    snap.macros[0]      = static_cast<double>(cfg.macroTightness);
    snap.macros[1]      = static_cast<double>(cfg.macroBrightness);
    snap.macros[2]      = static_cast<double>(cfg.macroBodySize);
    snap.macros[3]      = static_cast<double>(cfg.macroPunch);
    snap.macros[4]      = static_cast<double>(cfg.macroComplexity);
    return snap;
}

void applyPadSnapshot(const PadSnapshot& snap, PadConfig& cfg) noexcept
{
    cfg.exciterType = snap.exciterType;
    cfg.bodyModel   = snap.bodyModel;

    cfg.material          = static_cast<float>(snap.sound[0]);
    cfg.size              = static_cast<float>(snap.sound[1]);
    cfg.decay             = static_cast<float>(snap.sound[2]);
    cfg.strikePosition    = static_cast<float>(snap.sound[3]);
    cfg.level             = static_cast<float>(snap.sound[4]);
    cfg.tsFilterType      = static_cast<float>(snap.sound[5]);
    cfg.tsFilterCutoff    = static_cast<float>(snap.sound[6]);
    cfg.tsFilterResonance = static_cast<float>(snap.sound[7]);
    cfg.tsFilterEnvAmount = static_cast<float>(snap.sound[8]);
    cfg.tsDriveAmount     = static_cast<float>(snap.sound[9]);
    cfg.tsFoldAmount      = static_cast<float>(snap.sound[10]);
    cfg.tsPitchEnvStart   = static_cast<float>(snap.sound[11]);
    cfg.tsPitchEnvEnd     = static_cast<float>(snap.sound[12]);
    cfg.tsPitchEnvTime    = static_cast<float>(snap.sound[13]);
    cfg.tsPitchEnvCurve   = static_cast<float>(snap.sound[14]);
    cfg.tsFilterEnvAttack = static_cast<float>(snap.sound[15]);
    cfg.tsFilterEnvDecay  = static_cast<float>(snap.sound[16]);
    cfg.tsFilterEnvSustain = static_cast<float>(snap.sound[17]);
    cfg.tsFilterEnvRelease = static_cast<float>(snap.sound[18]);
    cfg.modeStretch       = static_cast<float>(snap.sound[19]);
    cfg.decaySkew         = static_cast<float>(snap.sound[20]);
    cfg.modeInjectAmount  = static_cast<float>(snap.sound[21]);
    cfg.nonlinearCoupling = static_cast<float>(snap.sound[22]);
    cfg.morphEnabled      = static_cast<float>(snap.sound[23]);
    cfg.morphStart        = static_cast<float>(snap.sound[24]);
    cfg.morphEnd          = static_cast<float>(snap.sound[25]);
    cfg.morphDuration     = static_cast<float>(snap.sound[26]);
    cfg.morphCurve        = static_cast<float>(snap.sound[27]);
    // Indices 28-29 are the float64 expression of chokeGroup/outputBus; the
    // uint8 fields below are authoritative, so we skip the float64 values.
    cfg.fmRatio            = static_cast<float>(snap.sound[30]);
    cfg.feedbackAmount     = static_cast<float>(snap.sound[31]);
    cfg.noiseBurstDuration = static_cast<float>(snap.sound[32]);
    cfg.frictionPressure   = static_cast<float>(snap.sound[33]);
    // Phase 7: parallel noise layer + click transient.
    cfg.noiseLayerMix        = std::clamp(static_cast<float>(snap.sound[34]), 0.0f, 1.0f);
    cfg.noiseLayerCutoff     = std::clamp(static_cast<float>(snap.sound[35]), 0.0f, 1.0f);
    cfg.noiseLayerResonance  = std::clamp(static_cast<float>(snap.sound[36]), 0.0f, 1.0f);
    cfg.noiseLayerDecay      = std::clamp(static_cast<float>(snap.sound[37]), 0.0f, 1.0f);
    cfg.noiseLayerColor      = std::clamp(static_cast<float>(snap.sound[38]), 0.0f, 1.0f);
    cfg.clickLayerMix        = std::clamp(static_cast<float>(snap.sound[39]), 0.0f, 1.0f);
    cfg.clickLayerContactMs  = std::clamp(static_cast<float>(snap.sound[40]), 0.0f, 1.0f);
    cfg.clickLayerBrightness = std::clamp(static_cast<float>(snap.sound[41]), 0.0f, 1.0f);
    // Phase 8A: per-mode damping law overrides. Sentinel -1.0f is preserved
    // verbatim (mapper falls back to legacy derivation); other values are
    // clamped to [0, 1] before denormalisation in the mapper.
    {
        const double b1Raw = snap.sound[42];
        const double b3Raw = snap.sound[43];
        cfg.bodyDampingB1 = (b1Raw < 0.0)
            ? -1.0f
            : std::clamp(static_cast<float>(b1Raw), 0.0f, 1.0f);
        cfg.bodyDampingB3 = (b3Raw < 0.0)
            ? -1.0f
            : std::clamp(static_cast<float>(b3Raw), 0.0f, 1.0f);
    }
    // Phase 8C: air-loading + per-mode scatter.
    cfg.airLoading  = std::clamp(static_cast<float>(snap.sound[44]), 0.0f, 1.0f);
    cfg.modeScatter = std::clamp(static_cast<float>(snap.sound[45]), 0.0f, 1.0f);
    // Phase 8D: head <-> shell coupling.
    cfg.couplingStrength  = std::clamp(static_cast<float>(snap.sound[46]), 0.0f, 1.0f);
    cfg.secondaryEnabled  = std::clamp(static_cast<float>(snap.sound[47]), 0.0f, 1.0f);
    cfg.secondarySize     = std::clamp(static_cast<float>(snap.sound[48]), 0.0f, 1.0f);
    cfg.secondaryMaterial = std::clamp(static_cast<float>(snap.sound[49]), 0.0f, 1.0f);
    // Phase 8E: nonlinear tension modulation.
    cfg.tensionModAmt     = std::clamp(static_cast<float>(snap.sound[50]), 0.0f, 1.0f);
    // Phase 8F: per-pad enable toggle. Clamp [0, 1] then float-as-bool.
    cfg.enabled           = std::clamp(static_cast<float>(snap.sound[51]), 0.0f, 1.0f);
    // Phase 10: three-point pitch envelope extension.
    cfg.tsPitchEnvKneeEnabled = std::clamp(static_cast<float>(snap.sound[52]), 0.0f, 1.0f);
    cfg.tsPitchEnvMidPitch    = std::clamp(static_cast<float>(snap.sound[53]), 0.0f, 1.0f);
    cfg.tsPitchEnvMidFraction = std::clamp(static_cast<float>(snap.sound[54]), 0.0f, 1.0f);
    cfg.tsPitchEnvCurve2      = std::clamp(static_cast<float>(snap.sound[55]), 0.0f, 1.0f);
    // M-9: per-pad pan (offset 64).
    cfg.pan                   = std::clamp(static_cast<float>(snap.sound[56]), 0.0f, 1.0f);
    // Snare-body fix: per-pad noise-layer gain multiplier (sanity-clamped).
    cfg.noiseLayerGain        = std::clamp(static_cast<float>(snap.sound[57]), 0.1f, 16.0f);
    // Wire coupling: buzz-follows-body depth.
    cfg.wireCoupling          = std::clamp(static_cast<float>(snap.sound[58]), 0.0f, 1.0f);

    cfg.chokeGroup      = (snap.chokeGroup > 8U) ? std::uint8_t{0} : snap.chokeGroup;
    cfg.outputBus       = (snap.outputBus > 15U) ? std::uint8_t{0} : snap.outputBus;
    cfg.couplingAmount  = std::clamp(static_cast<float>(snap.couplingAmount), 0.0f, 1.0f);
    cfg.macroTightness  = std::clamp(static_cast<float>(snap.macros[0]), 0.0f, 1.0f);
    cfg.macroBrightness = std::clamp(static_cast<float>(snap.macros[1]), 0.0f, 1.0f);
    cfg.macroBodySize   = std::clamp(static_cast<float>(snap.macros[2]), 0.0f, 1.0f);
    cfg.macroPunch      = std::clamp(static_cast<float>(snap.macros[3]), 0.0f, 1.0f);
    cfg.macroComplexity = std::clamp(static_cast<float>(snap.macros[4]), 0.0f, 1.0f);
}

PadPresetSnapshot toPadPresetSnapshot(const PadConfig& cfg) noexcept
{
    PadPresetSnapshot snap;
    snap.exciterType = cfg.exciterType;
    snap.bodyModel   = cfg.bodyModel;
    // Reuse the first 51 sound slots from PadSnapshot. The Phase 8F
    // enable-toggle slot at index 51 is intentionally excluded -- per-pad
    // presets carry sound character only, never the kit-level enabled
    // flag, so loading a preset onto a pad never silently flips its
    // enable state.
    const PadSnapshot full = toPadSnapshot(cfg);
    // PadPresetSnapshot::sound mirrors PadSnapshot::sound exactly (both 59
    // slots); indices 28-29 (choke/bus float64 mirrors), 51 (kit-level
    // enabled flag) and 56 (pan -- positioning, not sound character) are
    // written but ignored on load -- see applyPadPresetSnapshot below.
    std::copy_n(full.sound.begin(), snap.sound.size(), snap.sound.begin());
    return snap;
}

void applyPadPresetSnapshot(const PadPresetSnapshot& snap, PadConfig& cfg) noexcept
{
    cfg.exciterType = snap.exciterType;
    cfg.bodyModel   = snap.bodyModel;

    // Apply only the sound fields (indices 0-27, 30-33). Indices 28-29
    // (chokeGroup / outputBus as float64) are intentionally skipped per
    // FR-061: per-pad presets MUST NOT carry routing or choke data.
    cfg.material           = static_cast<float>(snap.sound[0]);
    cfg.size               = static_cast<float>(snap.sound[1]);
    cfg.decay              = static_cast<float>(snap.sound[2]);
    cfg.strikePosition     = static_cast<float>(snap.sound[3]);
    cfg.level              = static_cast<float>(snap.sound[4]);
    cfg.tsFilterType       = static_cast<float>(snap.sound[5]);
    cfg.tsFilterCutoff     = static_cast<float>(snap.sound[6]);
    cfg.tsFilterResonance  = static_cast<float>(snap.sound[7]);
    cfg.tsFilterEnvAmount  = static_cast<float>(snap.sound[8]);
    cfg.tsDriveAmount      = static_cast<float>(snap.sound[9]);
    cfg.tsFoldAmount       = static_cast<float>(snap.sound[10]);
    cfg.tsPitchEnvStart    = static_cast<float>(snap.sound[11]);
    cfg.tsPitchEnvEnd      = static_cast<float>(snap.sound[12]);
    cfg.tsPitchEnvTime     = static_cast<float>(snap.sound[13]);
    cfg.tsPitchEnvCurve    = static_cast<float>(snap.sound[14]);
    cfg.tsFilterEnvAttack  = static_cast<float>(snap.sound[15]);
    cfg.tsFilterEnvDecay   = static_cast<float>(snap.sound[16]);
    cfg.tsFilterEnvSustain = static_cast<float>(snap.sound[17]);
    cfg.tsFilterEnvRelease = static_cast<float>(snap.sound[18]);
    cfg.modeStretch        = static_cast<float>(snap.sound[19]);
    cfg.decaySkew          = static_cast<float>(snap.sound[20]);
    cfg.modeInjectAmount   = static_cast<float>(snap.sound[21]);
    cfg.nonlinearCoupling  = static_cast<float>(snap.sound[22]);
    cfg.morphEnabled       = static_cast<float>(snap.sound[23]);
    cfg.morphStart         = static_cast<float>(snap.sound[24]);
    cfg.morphEnd           = static_cast<float>(snap.sound[25]);
    cfg.morphDuration      = static_cast<float>(snap.sound[26]);
    cfg.morphCurve         = static_cast<float>(snap.sound[27]);
    cfg.fmRatio            = static_cast<float>(snap.sound[30]);
    cfg.feedbackAmount     = static_cast<float>(snap.sound[31]);
    cfg.noiseBurstDuration = static_cast<float>(snap.sound[32]);
    cfg.frictionPressure   = static_cast<float>(snap.sound[33]);
    // Phase 7: parallel noise layer + click transient (offsets 42-49).
    cfg.noiseLayerMix        = std::clamp(static_cast<float>(snap.sound[34]), 0.0f, 1.0f);
    cfg.noiseLayerCutoff     = std::clamp(static_cast<float>(snap.sound[35]), 0.0f, 1.0f);
    cfg.noiseLayerResonance  = std::clamp(static_cast<float>(snap.sound[36]), 0.0f, 1.0f);
    cfg.noiseLayerDecay      = std::clamp(static_cast<float>(snap.sound[37]), 0.0f, 1.0f);
    cfg.noiseLayerColor      = std::clamp(static_cast<float>(snap.sound[38]), 0.0f, 1.0f);
    cfg.clickLayerMix        = std::clamp(static_cast<float>(snap.sound[39]), 0.0f, 1.0f);
    cfg.clickLayerContactMs  = std::clamp(static_cast<float>(snap.sound[40]), 0.0f, 1.0f);
    cfg.clickLayerBrightness = std::clamp(static_cast<float>(snap.sound[41]), 0.0f, 1.0f);
    // Snare-body fix: per-pad noise-layer gain (preset carries wire level).
    cfg.noiseLayerGain       = std::clamp(static_cast<float>(snap.sound[57]), 0.1f, 16.0f);
    // Wire coupling: preset carries buzz-follows-body depth (sound character).
    cfg.wireCoupling         = std::clamp(static_cast<float>(snap.sound[58]), 0.0f, 1.0f);
    // Phase 8A: per-mode damping law overrides (sentinel -1.0f kept verbatim).
    {
        const double b1Raw = snap.sound[42];
        const double b3Raw = snap.sound[43];
        cfg.bodyDampingB1 = (b1Raw < 0.0)
            ? -1.0f
            : std::clamp(static_cast<float>(b1Raw), 0.0f, 1.0f);
        cfg.bodyDampingB3 = (b3Raw < 0.0)
            ? -1.0f
            : std::clamp(static_cast<float>(b3Raw), 0.0f, 1.0f);
    }
    // Phase 8C: air-loading + per-mode scatter.
    cfg.airLoading  = std::clamp(static_cast<float>(snap.sound[44]), 0.0f, 1.0f);
    cfg.modeScatter = std::clamp(static_cast<float>(snap.sound[45]), 0.0f, 1.0f);
    // Phase 8D: head <-> shell coupling.
    cfg.couplingStrength  = std::clamp(static_cast<float>(snap.sound[46]), 0.0f, 1.0f);
    cfg.secondaryEnabled  = std::clamp(static_cast<float>(snap.sound[47]), 0.0f, 1.0f);
    cfg.secondarySize     = std::clamp(static_cast<float>(snap.sound[48]), 0.0f, 1.0f);
    cfg.secondaryMaterial = std::clamp(static_cast<float>(snap.sound[49]), 0.0f, 1.0f);
    // Phase 8E: nonlinear tension modulation.
    cfg.tensionModAmt     = std::clamp(static_cast<float>(snap.sound[50]), 0.0f, 1.0f);
    // Note: snap.sound[51] (enabled) is intentionally skipped per FR-061 --
    // per-pad presets must not carry per-pad-mute state.
    // Phase 10: three-point pitch envelope extension. These are sound character
    // and MUST round-trip through pad presets.
    cfg.tsPitchEnvKneeEnabled = std::clamp(static_cast<float>(snap.sound[52]), 0.0f, 1.0f);
    cfg.tsPitchEnvMidPitch    = std::clamp(static_cast<float>(snap.sound[53]), 0.0f, 1.0f);
    cfg.tsPitchEnvMidFraction = std::clamp(static_cast<float>(snap.sound[54]), 0.0f, 1.0f);
    cfg.tsPitchEnvCurve2      = std::clamp(static_cast<float>(snap.sound[55]), 0.0f, 1.0f);
}

// ============================================================================
// Full kit / state codec
// ============================================================================

tresult writeKitBlob(IBStream* stream, const KitSnapshot& kit)
{
    if (!stream)
        return kResultFalse;

    IBStreamer streamer(stream, kLittleEndian);

    const int32 version = kBlobVersion;
    writeT(streamer, version);

    const int32 maxPoly = static_cast<int32>(kit.maxPolyphony);
    writeT(streamer, maxPoly);

    const int32 stealPolicy = static_cast<int32>(kit.voiceStealingPolicy);
    writeT(streamer, stealPolicy);

    for (const auto& pad : kit.pads)
    {
        const int32 excI = static_cast<int32>(pad.exciterType);
        const int32 bodyI = static_cast<int32>(pad.bodyModel);
        writeT(streamer, excI);
        writeT(streamer, bodyI);
        for (double v : pad.sound)
            writeT(streamer, v);
        writeT(streamer, pad.chokeGroup);
        writeT(streamer, pad.outputBus);
    }

    const int32 selPad = static_cast<int32>(kit.selectedPadIndex);
    writeT(streamer, selPad);

    writeT(streamer, kit.globalCoupling);
    writeT(streamer, kit.snareBuzz);
    writeT(streamer, kit.tomResonance);
    writeT(streamer, kit.couplingDelayMs);

    for (const auto& pad : kit.pads)
        writeT(streamer, pad.couplingAmount);

    // Macros, pad-major (pad0.m0..pad0.m4, pad1.m0..pad1.m4, ...).
    for (const auto& pad : kit.pads)
        for (double m : pad.macros)
            writeT(streamer, m);

    // Master gain norm, after macros, before optional uiMode.
    writeT(streamer, kit.masterGainNorm);

    // Session field, only if flagged. uiMode persists in kit presets.
    if (kit.hasSession)
    {
        const int32 uiMode = static_cast<int32>(kit.uiMode);
        writeT(streamer, uiMode);
    }

    return kResultOk;
}

tresult readKitBlob(IBStream* stream, KitSnapshot& kit)
{
    if (!stream)
        return kResultFalse;

    IBStreamer streamer(stream, kLittleEndian);

    int32 version = 0;
    if (!readT(streamer, version))
        return kResultFalse;
    if (version != kBlobVersion)
        return kResultFalse;

    int32 maxPoly = 8;
    int32 stealPolicy = 0;
    if (!readT(streamer, maxPoly))
        return kResultFalse;
    if (!readT(streamer, stealPolicy))
        return kResultFalse;
    kit.maxPolyphony        = std::clamp(static_cast<int>(maxPoly), 4, 16);
    kit.voiceStealingPolicy = std::clamp(static_cast<int>(stealPolicy), 0, 2);

    for (auto& pad : kit.pads)
    {
        int32 excI = 0;
        int32 bodyI = 0;
        if (!readT(streamer, excI))  return kResultFalse;
        if (!readT(streamer, bodyI)) return kResultFalse;
        excI = std::clamp(excI, int32{0},
                          static_cast<int32>(ExciterType::kCount) - 1);
        bodyI = std::clamp(bodyI, int32{0},
                           static_cast<int32>(BodyModelType::kCount) - 1);
        pad.exciterType = static_cast<ExciterType>(excI);
        pad.bodyModel   = static_cast<BodyModelType>(bodyI);

        for (double& slot : pad.sound)
        {
            if (!readT(streamer, slot))
                return kResultFalse;
        }
        if (!readT(streamer, pad.chokeGroup)) return kResultFalse;
        if (!readT(streamer, pad.outputBus))  return kResultFalse;
        if (pad.chokeGroup > 8U)  pad.chokeGroup = 0;
        if (pad.outputBus  > 15U) pad.outputBus  = 0;
    }

    int32 selPad = 0;
    if (!readT(streamer, selPad))
        return kResultFalse;
    kit.selectedPadIndex =
        std::clamp(static_cast<int>(selPad), 0, static_cast<int>(kit.pads.size()) - 1);

    double gc = 0.0;
    double sb = 0.0;
    double tr = 0.0;
    double cd = 1.0;
    if (!readT(streamer, gc)) return kResultFalse;
    if (!readT(streamer, sb)) return kResultFalse;
    if (!readT(streamer, tr)) return kResultFalse;
    if (!readT(streamer, cd)) return kResultFalse;
    kit.globalCoupling  = std::clamp(gc, 0.0, 1.0);
    kit.snareBuzz       = std::clamp(sb, 0.0, 1.0);
    kit.tomResonance    = std::clamp(tr, 0.0, 1.0);
    kit.couplingDelayMs = std::clamp(cd, 0.5, 2.0);

    for (auto& pad : kit.pads)
    {
        double amt = 0.5;
        if (!readT(streamer, amt))
            return kResultFalse;
        pad.couplingAmount = std::clamp(amt, 0.0, 1.0);
    }

    for (auto& pad : kit.pads)
    {
        for (double& m : pad.macros)
        {
            if (!readT(streamer, m))
                return kResultFalse;
            m = std::clamp(m, 0.0, 1.0);
        }
    }

    double mg = 0.5;
    if (!readT(streamer, mg))
        return kResultFalse;
    kit.masterGainNorm = std::clamp(mg, 0.0, 1.0);

    // Optional session field: present only when the producer set hasSession.
    int32 uiMode = 0;
    if (readT(streamer, uiMode))
    {
        kit.uiMode     = std::clamp(static_cast<int>(uiMode), 0, 1);
        kit.hasSession = true;
    }
    else
    {
        kit.hasSession = false;
    }

    return kResultOk;
}

// ============================================================================
// Per-pad preset codec
// ============================================================================

tresult writePadPresetBlob(IBStream* stream, const PadPresetSnapshot& pad)
{
    if (!stream)
        return kResultFalse;

    IBStreamer streamer(stream, kLittleEndian);

    const int32 version = kPadBlobVersion;
    writeT(streamer, version);

    const int32 excI  = static_cast<int32>(pad.exciterType);
    const int32 bodyI = static_cast<int32>(pad.bodyModel);
    writeT(streamer, excI);
    writeT(streamer, bodyI);

    for (double v : pad.sound)
        writeT(streamer, v);

    return kResultOk;
}

tresult readPadPresetBlob(IBStream* stream, PadPresetSnapshot& pad)
{
    if (!stream)
        return kResultFalse;

    IBStreamer streamer(stream, kLittleEndian);

    int32 version = 0;
    if (!readT(streamer, version))
        return kResultFalse;
    if (version != kPadBlobVersion)
        return kResultFalse;

    int32 excI = 0;
    int32 bodyI = 0;
    if (!readT(streamer, excI))  return kResultFalse;
    if (!readT(streamer, bodyI)) return kResultFalse;
    excI = std::clamp(excI, int32{0},
                      static_cast<int32>(ExciterType::kCount) - 1);
    bodyI = std::clamp(bodyI, int32{0},
                       static_cast<int32>(BodyModelType::kCount) - 1);
    pad.exciterType = static_cast<ExciterType>(excI);
    pad.bodyModel   = static_cast<BodyModelType>(bodyI);

    for (double& slot : pad.sound)
    {
        if (!readT(streamer, slot))
            return kResultFalse;
    }
    return kResultOk;
}

} // namespace Membrum::State
