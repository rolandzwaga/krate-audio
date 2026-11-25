// ==============================================================================
// Membrum state codec -- implementation.
// ==============================================================================

#include "state/state_codec.h"

#include "dsp/bodies/body_damping_encoding.h"
#include "dsp/macro_offsets.h"

#include "base/source/fstreamer.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
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

/// The released version whose kit fields hold what a pad played.
constexpr int32 kBakedMacroBlobVersion = 5;

/// Sound slots per pad that a kit blob of `version` carries, or 0 for a version
/// no release wrote (rejected). Released builds wrote 1 (v0.9.0, v0.9.1), 2
/// (v0.10.1, v0.10.2) and 5 (v0.12.0 - v0.12.5); fix plan T2.8.
[[nodiscard]] std::size_t kitSoundSlots(int32 version) noexcept
{
    switch (version)
    {
    case 1:                      return 52;  // through the Phase 8F enabled flag
    case 2:                      return 56;  // + the Phase 10 pitch-envelope knee
    case kBakedMacroBlobVersion:
    case kBlobVersion:           return std::tuple_size_v<decltype(PadSnapshot::sound)>;
    default:                     return 0;
    }
}

/// Sound slots of a pad-preset blob of `version`, or 0 (rejected). Version 1
/// did not carry the kit-level enabled flag (slot 51).
[[nodiscard]] std::size_t padPresetSoundSlots(int32 version) noexcept
{
    switch (version)
    {
    case 1:                      return 51;
    case 2:                      return 56;
    case kBakedMacroBlobVersion:
    case kPadBlobVersion:        return std::tuple_size_v<decltype(PadPresetSnapshot::sound)>;
    default:                     return 0;
    }
}

/// Fix plan T2.8: maps a version-1 or version-2 pad's sound slots (`stored` of
/// them read) onto the version-5 meaning, so the version-5 migration below
/// then runs as for a v0.12 blob. The slots a version did not carry get the
/// value that makes this engine do what the old build did, and the slots whose
/// meaning changed since are converted to keep the plain value the old build
/// used. Engine changes that alter how an unchanged value sounds are not
/// migrated.
void legacySoundToV5(std::array<double, 59>& sound, std::size_t stored, int32 version) noexcept
{
    // Slots 51-58 by index - 51, PadConfig's neutral values of the fields
    // added after version 1.
    constexpr std::array<double, 8> kAddedSlots{
        1.0,  // 51 enabled (a version-1 pad preset did not carry it)
        0.0,  // 52 pitch-envelope knee off: one segment, as before Phase 10
        0.5,  // 53 mid pitch (unused with the knee off)
        0.5,  // 54 mid fraction (unused with the knee off)
        0.5,  // 55 segment-2 curve: linear (unused with the knee off)
        0.5,  // 56 pan centre: the old mono duplication
        1.0,  // 57 noiseLayerGain: no multiplier existed
        0.0,  // 58 wireCoupling 0: independent buzz (legacy)
    };
    for (std::size_t i = stored; i < sound.size(); ++i)
        sound[i] = kAddedSlots[i - 51];

    if (version == 1)
    {
        // v0.9.x read the pitch-envelope curve as Exponential (< 0.5) or
        // Linear; 0.10.0 made it a curve amount and remapped its factory kits
        // Exp -> 0.15 (amount -0.7), Lin -> 0.5 (linear).
        sound[14] = sound[14] < 0.5 ? 0.15 : 0.5;
        // v0.9.x decoded filter-envelope attack, decay and release linearly
        // (ms = n x max); since 0.10.0 cubically (ms = n^3 x max).
        for (const std::size_t i : {std::size_t{15}, std::size_t{16}, std::size_t{18}})
            sound[i] = std::cbrt(std::clamp(sound[i], 0.0, 1.0));
    }
    // v0.9.x and v0.10.x stored FM Ratio, Feedback Amount and Friction
    // Pressure but never forwarded them to the exciter: it played the 1.4
    // modulator ratio and velocity-only drive and pressure.
    constexpr float kLegacyFMRatioNorm = (1.4f - 1.0f) / 3.0f;  // ratio 1 + 3n
    sound[30] = static_cast<double>(kLegacyFMRatioNorm);
    sound[31] = 0.0;
    sound[33] = 0.0;
}

/// Version 5 stored a pad's macro-target fields with the macro deltas baked in
/// (what it played); version 6 stores base values that the macros offset. Map
/// the version-5 fields to bases, so the pad keeps its macro positions and
/// plays the same (fix plan T1.22, user ruling 2026-11-24). A field the
/// macros do not move keeps its stored double.
void baseFieldsFromBakedMacros(PadSnapshot& pad) noexcept
{
    PadConfig played;
    applyPadSnapshot(pad, played);
    const PadConfig base = baseFromEffective(played);
    const auto storeBase = [](double& slot, float playedValue, float baseValue) noexcept {
        if (baseValue != playedValue)
            slot = static_cast<double>(baseValue);
    };
    storeBase(pad.sound[0],  played.material,           base.material);
    storeBase(pad.sound[1],  played.size,               base.size);
    storeBase(pad.sound[2],  played.decay,              base.decay);
    storeBase(pad.sound[6],  played.tsFilterCutoff,     base.tsFilterCutoff);
    storeBase(pad.sound[11], played.tsPitchEnvStart,    base.tsPitchEnvStart);
    storeBase(pad.sound[13], played.tsPitchEnvTime,     base.tsPitchEnvTime);
    storeBase(pad.sound[19], played.modeStretch,        base.modeStretch);
    storeBase(pad.sound[20], played.decaySkew,          base.decaySkew);
    storeBase(pad.sound[21], played.modeInjectAmount,   base.modeInjectAmount);
    storeBase(pad.sound[22], played.nonlinearCoupling,  base.nonlinearCoupling);
    storeBase(pad.sound[32], played.noiseBurstDuration, base.noiseBurstDuration);
    storeBase(pad.couplingAmount, played.couplingAmount, base.couplingAmount);
}

/// Version 5 stored body damping b1 / b3 as ABSOLUTE values (b1 = 0.2 + n *
/// 49.8 s^-1, b3 = n * 1e-3 s, a negative value = derive the coefficient);
/// version 6 stores them relative to the pad's derived law (fix plan T1.23).
/// Re-encode slots 42 / 43 against `played`, the config the pad played, so it
/// keeps its sound.
void bodyDampingFromAbsolute(std::array<double, 59>& sound, const PadConfig& played) noexcept
{
    const auto norms = Bodies::encodeLegacyBodyDamping(played, sound[42], sound[43]);
    sound[42] = static_cast<double>(norms.b1);
    sound[43] = static_cast<double>(norms.b3);
}

/// Fix plan T2.2 (MB-010): a stored double that is NaN or +-Inf loads as
/// `fallback`. A blob is untrusted input; std::clamp passes a NaN through.
[[nodiscard]] double finiteOr(double value, double fallback) noexcept
{
    return Krate::DSP::detail::isFinite(value) ? value : fallback;
}

/// The defaults of the kit-level doubles (KitSnapshot's member initialisers).
constexpr KitSnapshot kKitDefaults{};

/// Replaces every non-finite sound slot with that slot's PadConfig default.
/// `absoluteBodyDamping` (a version-5 blob): slots 42 / 43 held absolute body
/// damping, where a negative value meant "derive", the default.
void sanitiseSoundSlots(std::array<double, 59>& sound, bool absoluteBodyDamping) noexcept
{
    constexpr double kLegacyDerive = -1.0;
    const PadSnapshot defaults = toPadSnapshot(PadConfig{});
    for (std::size_t i = 0; i < sound.size(); ++i)
    {
        const bool legacyDamping = absoluteBodyDamping && (i == 42 || i == 43);
        sound[i] = finiteOr(sound[i], legacyDamping ? kLegacyDerive : defaults.sound[i]);
    }
}

/// A sound slot that applyPadSnapshot / applyPadPresetSnapshot load as a
/// normalised [0, 1] field (T2.2: slots 0-27 and 30-33 were copied unclamped).
[[nodiscard]] float unitSlot(double value) noexcept
{
    return std::clamp(static_cast<float>(value), 0.0f, 1.0f);
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

    cfg.material          = unitSlot(snap.sound[0]);
    cfg.size              = unitSlot(snap.sound[1]);
    cfg.decay             = unitSlot(snap.sound[2]);
    cfg.strikePosition    = unitSlot(snap.sound[3]);
    cfg.level             = unitSlot(snap.sound[4]);
    cfg.tsFilterType      = unitSlot(snap.sound[5]);
    cfg.tsFilterCutoff    = unitSlot(snap.sound[6]);
    cfg.tsFilterResonance = unitSlot(snap.sound[7]);
    cfg.tsFilterEnvAmount = unitSlot(snap.sound[8]);
    cfg.tsDriveAmount     = unitSlot(snap.sound[9]);
    cfg.tsFoldAmount      = unitSlot(snap.sound[10]);
    cfg.tsPitchEnvStart   = unitSlot(snap.sound[11]);
    cfg.tsPitchEnvEnd     = unitSlot(snap.sound[12]);
    cfg.tsPitchEnvTime    = unitSlot(snap.sound[13]);
    cfg.tsPitchEnvCurve   = unitSlot(snap.sound[14]);
    cfg.tsFilterEnvAttack = unitSlot(snap.sound[15]);
    cfg.tsFilterEnvDecay  = unitSlot(snap.sound[16]);
    cfg.tsFilterEnvSustain = unitSlot(snap.sound[17]);
    cfg.tsFilterEnvRelease = unitSlot(snap.sound[18]);
    cfg.modeStretch       = unitSlot(snap.sound[19]);
    cfg.decaySkew         = unitSlot(snap.sound[20]);
    cfg.modeInjectAmount  = unitSlot(snap.sound[21]);
    cfg.nonlinearCoupling = unitSlot(snap.sound[22]);
    cfg.morphEnabled      = unitSlot(snap.sound[23]);
    cfg.morphStart        = unitSlot(snap.sound[24]);
    cfg.morphEnd          = unitSlot(snap.sound[25]);
    cfg.morphDuration     = unitSlot(snap.sound[26]);
    cfg.morphCurve        = unitSlot(snap.sound[27]);
    // Indices 28-29 are the float64 expression of chokeGroup/outputBus; the
    // uint8 fields below are authoritative, so we skip the float64 values.
    cfg.fmRatio            = unitSlot(snap.sound[30]);
    cfg.feedbackAmount     = unitSlot(snap.sound[31]);
    cfg.noiseBurstDuration = unitSlot(snap.sound[32]);
    cfg.frictionPressure   = unitSlot(snap.sound[33]);
    // Phase 7: parallel noise layer + click transient.
    cfg.noiseLayerMix        = std::clamp(static_cast<float>(snap.sound[34]), 0.0f, 1.0f);
    cfg.noiseLayerCutoff     = std::clamp(static_cast<float>(snap.sound[35]), 0.0f, 1.0f);
    cfg.noiseLayerResonance  = std::clamp(static_cast<float>(snap.sound[36]), 0.0f, 1.0f);
    cfg.noiseLayerDecay      = std::clamp(static_cast<float>(snap.sound[37]), 0.0f, 1.0f);
    cfg.noiseLayerColor      = std::clamp(static_cast<float>(snap.sound[38]), 0.0f, 1.0f);
    cfg.clickLayerMix        = std::clamp(static_cast<float>(snap.sound[39]), 0.0f, 1.0f);
    cfg.clickLayerContactMs  = std::clamp(static_cast<float>(snap.sound[40]), 0.0f, 1.0f);
    cfg.clickLayerBrightness = std::clamp(static_cast<float>(snap.sound[41]), 0.0f, 1.0f);
    // Phase 8A: body damping b1 / b3, relative to the derived law (fix plan
    // T1.23; a version-5 blob's absolute values were mapped by readKitBlob).
    cfg.bodyDampingB1 = std::clamp(static_cast<float>(snap.sound[42]), 0.0f, 1.0f);
    cfg.bodyDampingB3 = std::clamp(static_cast<float>(snap.sound[43]), 0.0f, 1.0f);
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
    cfg.material           = unitSlot(snap.sound[0]);
    cfg.size               = unitSlot(snap.sound[1]);
    cfg.decay              = unitSlot(snap.sound[2]);
    cfg.strikePosition     = unitSlot(snap.sound[3]);
    cfg.level              = unitSlot(snap.sound[4]);
    cfg.tsFilterType       = unitSlot(snap.sound[5]);
    cfg.tsFilterCutoff     = unitSlot(snap.sound[6]);
    cfg.tsFilterResonance  = unitSlot(snap.sound[7]);
    cfg.tsFilterEnvAmount  = unitSlot(snap.sound[8]);
    cfg.tsDriveAmount      = unitSlot(snap.sound[9]);
    cfg.tsFoldAmount       = unitSlot(snap.sound[10]);
    cfg.tsPitchEnvStart    = unitSlot(snap.sound[11]);
    cfg.tsPitchEnvEnd      = unitSlot(snap.sound[12]);
    cfg.tsPitchEnvTime     = unitSlot(snap.sound[13]);
    cfg.tsPitchEnvCurve    = unitSlot(snap.sound[14]);
    cfg.tsFilterEnvAttack  = unitSlot(snap.sound[15]);
    cfg.tsFilterEnvDecay   = unitSlot(snap.sound[16]);
    cfg.tsFilterEnvSustain = unitSlot(snap.sound[17]);
    cfg.tsFilterEnvRelease = unitSlot(snap.sound[18]);
    cfg.modeStretch        = unitSlot(snap.sound[19]);
    cfg.decaySkew          = unitSlot(snap.sound[20]);
    cfg.modeInjectAmount   = unitSlot(snap.sound[21]);
    cfg.nonlinearCoupling  = unitSlot(snap.sound[22]);
    cfg.morphEnabled       = unitSlot(snap.sound[23]);
    cfg.morphStart         = unitSlot(snap.sound[24]);
    cfg.morphEnd           = unitSlot(snap.sound[25]);
    cfg.morphDuration      = unitSlot(snap.sound[26]);
    cfg.morphCurve         = unitSlot(snap.sound[27]);
    cfg.fmRatio            = unitSlot(snap.sound[30]);
    cfg.feedbackAmount     = unitSlot(snap.sound[31]);
    cfg.noiseBurstDuration = unitSlot(snap.sound[32]);
    cfg.frictionPressure   = unitSlot(snap.sound[33]);
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
    // Phase 8A: body damping b1 / b3, relative to the derived law (fix plan
    // T1.23; a version-5 preset's absolute values were mapped by
    // readPadPresetBlob).
    cfg.bodyDampingB1 = std::clamp(static_cast<float>(snap.sound[42]), 0.0f, 1.0f);
    cfg.bodyDampingB3 = std::clamp(static_cast<float>(snap.sound[43]), 0.0f, 1.0f);
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

namespace {

/// readKitBlob's body: writes `kit` as it reads, so the caller passes a copy.
tresult readKitBlobInto(IBStream* stream, KitSnapshot& kit)
{
    IBStreamer streamer(stream, kLittleEndian);

    int32 version = 0;
    if (!readT(streamer, version))
        return kResultFalse;
    const std::size_t storedSlots = kitSoundSlots(version);
    if (storedSlots == 0)
        return kResultFalse;
    // Every released version before the current one: absolute body damping
    // and macro deltas baked into the fields (the version-5 meaning, which
    // versions 1 and 2 are mapped onto).
    const bool released = version != kBlobVersion;

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

        for (std::size_t i = 0; i < storedSlots; ++i)
        {
            if (!readT(streamer, pad.sound[i]))
                return kResultFalse;
        }
        if (storedSlots < pad.sound.size())
            legacySoundToV5(pad.sound, storedSlots, version);
        sanitiseSoundSlots(pad.sound, released);
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
    // T2.2 (MB-010): a non-finite double loads as its default, then clamps.
    kit.globalCoupling  = std::clamp(finiteOr(gc, kKitDefaults.globalCoupling), 0.0, 1.0);
    kit.snareBuzz       = std::clamp(finiteOr(sb, kKitDefaults.snareBuzz), 0.0, 1.0);
    kit.tomResonance    = std::clamp(finiteOr(tr, kKitDefaults.tomResonance), 0.0, 1.0);
    kit.couplingDelayMs = std::clamp(finiteOr(cd, kKitDefaults.couplingDelayMs), 0.5, 2.0);

    const PadSnapshot padDefaults = toPadSnapshot(PadConfig{});
    for (auto& pad : kit.pads)
    {
        double amt = 0.5;
        if (!readT(streamer, amt))
            return kResultFalse;
        pad.couplingAmount = std::clamp(finiteOr(amt, padDefaults.couplingAmount), 0.0, 1.0);
    }

    for (auto& pad : kit.pads)
    {
        for (std::size_t j = 0; j < pad.macros.size(); ++j)
        {
            double& m = pad.macros[j];
            if (!readT(streamer, m))
                return kResultFalse;
            m = std::clamp(finiteOr(m, padDefaults.macros[j]), 0.0, 1.0);
        }
    }

    double mg = 0.5;
    if (!readT(streamer, mg))
        return kResultFalse;
    kit.masterGainNorm = std::clamp(finiteOr(mg, kKitDefaults.masterGainNorm), 0.0, 1.0);

    if (released)
    {
        for (auto& pad : kit.pads)
        {
            // Version-5 kit fields are what the pad played (macros baked in).
            PadConfig played;
            applyPadSnapshot(pad, played);
            bodyDampingFromAbsolute(pad.sound, played);
            baseFieldsFromBakedMacros(pad);
        }
    }

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

} // namespace

tresult readKitBlob(IBStream* stream, KitSnapshot& kit)
{
    if (!stream)
        return kResultFalse;

    // A rejected or short blob leaves the caller's snapshot as it was.
    KitSnapshot read = kit;
    if (readKitBlobInto(stream, read) != kResultOk)
        return kResultFalse;
    kit = read;
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
    // A pad preset carries no macros; a released one (versions 1, 2, 5) only
    // has its body damping slots re-encoded (below), after versions 1 and 2
    // are mapped onto the version-5 meaning.
    const std::size_t storedSlots = padPresetSoundSlots(version);
    if (storedSlots == 0)
        return kResultFalse;
    const bool released = version != kPadBlobVersion;

    // Read into a copy: a short blob leaves the caller's snapshot as it was.
    PadPresetSnapshot read = pad;
    int32 excI = 0;
    int32 bodyI = 0;
    if (!readT(streamer, excI))  return kResultFalse;
    if (!readT(streamer, bodyI)) return kResultFalse;
    excI = std::clamp(excI, int32{0},
                      static_cast<int32>(ExciterType::kCount) - 1);
    bodyI = std::clamp(bodyI, int32{0},
                       static_cast<int32>(BodyModelType::kCount) - 1);
    read.exciterType = static_cast<ExciterType>(excI);
    read.bodyModel   = static_cast<BodyModelType>(bodyI);

    for (std::size_t i = 0; i < storedSlots; ++i)
    {
        if (!readT(streamer, read.sound[i]))
            return kResultFalse;
    }
    if (storedSlots < read.sound.size())
        legacySoundToV5(read.sound, storedSlots, version);
    sanitiseSoundSlots(read.sound, released);

    // Released versions held absolute body damping values. A pad preset
    // carries no macros, so its fields are what it played.
    if (released)
    {
        PadConfig played;
        applyPadPresetSnapshot(read, played);
        bodyDampingFromAbsolute(read.sound, played);
    }
    pad = read;
    return kResultOk;
}

} // namespace Membrum::State
