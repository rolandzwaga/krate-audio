// ==============================================================================
// Membrum state codec -- implementation.
// ==============================================================================

#include "state/state_codec.h"

#include "dsp/coupling_matrix.h"

#include <algorithm>
#include <cstring>

namespace Membrum::State {

using Steinberg::int32;
using Steinberg::tresult;
using Steinberg::kResultOk;
using Steinberg::kResultFalse;
using Steinberg::IBStream;

namespace {

// Small helpers that treat a short read as a failure. A partial read leaves
// the caller's destination bytes in an undefined state; wrap in std::memset
// upstream if the caller wants to fall back to a default on partial reads.
template <typename T>
bool readT(IBStream* s, T& dst) noexcept
{
    Steinberg::int32 got = 0;
    const auto size = static_cast<int32>(sizeof(T));
    if (s->read(&dst, size, &got) != kResultOk)
        return false;
    return got == size;
}

template <typename T>
void writeT(IBStream* s, const T& src) noexcept
{
    // VST3 IBStream::write takes a void*; cast away const to satisfy the
    // interface. Writes are treated as best-effort (failures are rare and
    // the caller reports kResultOk regardless, matching legacy behaviour).
    s->write(const_cast<T*>(&src), static_cast<int32>(sizeof(T)), nullptr);
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
    // Reuse the sound-block layout from PadSnapshot.
    const PadSnapshot full = toPadSnapshot(cfg);
    snap.sound = full.sound;
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
}

// ============================================================================
// Full kit / state codec
// ============================================================================

tresult writeKitBlob(IBStream* stream, const KitSnapshot& kit)
{
    if (!stream)
        return kResultFalse;

    const int32 version = kBlobVersion;
    writeT(stream, version);

    const int32 maxPoly = static_cast<int32>(kit.maxPolyphony);
    writeT(stream, maxPoly);

    const int32 stealPolicy = static_cast<int32>(kit.voiceStealingPolicy);
    writeT(stream, stealPolicy);

    for (const auto& pad : kit.pads)
    {
        const int32 excI = static_cast<int32>(pad.exciterType);
        const int32 bodyI = static_cast<int32>(pad.bodyModel);
        writeT(stream, excI);
        writeT(stream, bodyI);
        for (double v : pad.sound)
            writeT(stream, v);
        writeT(stream, pad.chokeGroup);
        writeT(stream, pad.outputBus);
    }

    const int32 selPad = static_cast<int32>(kit.selectedPadIndex);
    writeT(stream, selPad);

    writeT(stream, kit.globalCoupling);
    writeT(stream, kit.snareBuzz);
    writeT(stream, kit.tomResonance);
    writeT(stream, kit.couplingDelayMs);

    for (const auto& pad : kit.pads)
        writeT(stream, pad.couplingAmount);

    const auto overrideCount = static_cast<std::uint16_t>(kit.overrides.size());
    writeT(stream, overrideCount);
    for (const auto& ov : kit.overrides)
    {
        writeT(stream, ov.src);
        writeT(stream, ov.dst);
        writeT(stream, ov.coeff);
    }

    // Macros, pad-major (pad0.m0..pad0.m4, pad1.m0..pad1.m4, ...).
    for (const auto& pad : kit.pads)
        for (double m : pad.macros)
            writeT(stream, m);

    // Session field, only if flagged. uiMode persists in kit presets.
    if (kit.hasSession)
    {
        const int32 uiMode = static_cast<int32>(kit.uiMode);
        writeT(stream, uiMode);
    }

    return kResultOk;
}

tresult readKitBlob(IBStream* stream, KitSnapshot& kit)
{
    if (!stream)
        return kResultFalse;

    int32 version = 0;
    if (!readT(stream, version))
        return kResultFalse;
    if (version != kBlobVersion && version != kBlobVersionV8
        && version != kBlobVersionV7 && version != kBlobVersionV6)
        return kResultFalse;
    const bool isV6 = (version == kBlobVersionV6);
    const bool isV7 = (version == kBlobVersionV7);
    const bool isV8 = (version == kBlobVersionV8);
    constexpr std::size_t kV9Slots = std::tuple_size_v<decltype(PadSnapshot::sound)>;
    const std::size_t soundSlotsToRead =
        isV6 ? kV6SoundSlotCount
             : (isV7 ? kV7SoundSlotCount
                     : (isV8 ? kV8SoundSlotCount : kV9Slots));

    // Defaults for Phase 7 slots when reading a v6 blob. These mirror the
    // PadConfig defaults so legacy kits load as-authored + new-layer defaults.
    const PadConfig defaults{};
    const double defaultPhase7Sound[8] = {
        static_cast<double>(defaults.noiseLayerMix),
        static_cast<double>(defaults.noiseLayerCutoff),
        static_cast<double>(defaults.noiseLayerResonance),
        static_cast<double>(defaults.noiseLayerDecay),
        static_cast<double>(defaults.noiseLayerColor),
        static_cast<double>(defaults.clickLayerMix),
        static_cast<double>(defaults.clickLayerContactMs),
        static_cast<double>(defaults.clickLayerBrightness),
    };
    // Phase 8A defaults for legacy v6/v7 blobs: sentinel -1.0f preserves
    // Phase 1 bit-identity (mapper falls back to decay-derived damping).
    const double defaultPhase8ASound[2] = {
        static_cast<double>(defaults.bodyDampingB1),
        static_cast<double>(defaults.bodyDampingB3),
    };
    // Phase 8C defaults for legacy v6/v7/v8 blobs.
    const double defaultPhase8CSound[2] = {
        static_cast<double>(defaults.airLoading),
        static_cast<double>(defaults.modeScatter),
    };

    int32 maxPoly = 8;
    int32 stealPolicy = 0;
    if (!readT(stream, maxPoly))
        return kResultFalse;
    if (!readT(stream, stealPolicy))
        return kResultFalse;
    kit.maxPolyphony        = std::clamp(static_cast<int>(maxPoly), 4, 16);
    kit.voiceStealingPolicy = std::clamp(static_cast<int>(stealPolicy), 0, 2);

    for (auto& pad : kit.pads)
    {
        int32 excI = 0;
        int32 bodyI = 0;
        if (!readT(stream, excI))  return kResultFalse;
        if (!readT(stream, bodyI)) return kResultFalse;
        excI = std::clamp(excI, int32{0},
                          static_cast<int32>(ExciterType::kCount) - 1);
        bodyI = std::clamp(bodyI, int32{0},
                           static_cast<int32>(BodyModelType::kCount) - 1);
        pad.exciterType = static_cast<ExciterType>(excI);
        pad.bodyModel   = static_cast<BodyModelType>(bodyI);

        for (std::size_t i = 0; i < soundSlotsToRead; ++i)
        {
            if (!readT(stream, pad.sound[i]))
                pad.sound[i] = 0.0;
        }
        // v6 -> newer: Phase 7 slots (indices 34-41) are absent on disk. Fill
        // with PadConfig defaults so legacy kits still get the new layers.
        if (isV6)
        {
            for (std::size_t i = 0; i < 8; ++i)
                pad.sound[kV6SoundSlotCount + i] = defaultPhase7Sound[i];
        }
        // v6 or v7 -> v8+: Phase 8A slots (indices 42-43) are absent. Fill
        // with sentinel defaults so legacy kits keep Phase 1 bit-identity.
        if (isV6 || isV7)
        {
            for (std::size_t i = 0; i < 2; ++i)
                pad.sound[kV7SoundSlotCount + i] = defaultPhase8ASound[i];
        }
        // v6, v7 or v8 -> v9: Phase 8C slots (indices 44-45) are absent.
        if (isV6 || isV7 || isV8)
        {
            for (std::size_t i = 0; i < 2; ++i)
                pad.sound[kV8SoundSlotCount + i] = defaultPhase8CSound[i];
        }
        if (!readT(stream, pad.chokeGroup)) pad.chokeGroup = 0;
        if (!readT(stream, pad.outputBus))  pad.outputBus  = 0;
        if (pad.chokeGroup > 8U)  pad.chokeGroup = 0;
        if (pad.outputBus  > 15U) pad.outputBus  = 0;
    }

    int32 selPad = 0;
    if (!readT(stream, selPad))
        selPad = 0;
    kit.selectedPadIndex =
        std::clamp(static_cast<int>(selPad), 0, static_cast<int>(kit.pads.size()) - 1);

    double gc = 0.0, sb = 0.0, tr = 0.0, cd = 1.0;
    if (!readT(stream, gc)) gc = 0.0;
    if (!readT(stream, sb)) sb = 0.0;
    if (!readT(stream, tr)) tr = 0.0;
    if (!readT(stream, cd)) cd = 1.0;
    kit.globalCoupling  = std::clamp(gc, 0.0, 1.0);
    kit.snareBuzz       = std::clamp(sb, 0.0, 1.0);
    kit.tomResonance    = std::clamp(tr, 0.0, 1.0);
    kit.couplingDelayMs = std::clamp(cd, 0.5, 2.0);

    for (auto& pad : kit.pads)
    {
        double amt = 0.5;
        if (!readT(stream, amt))
            amt = 0.5;
        pad.couplingAmount = std::clamp(amt, 0.0, 1.0);
    }

    std::uint16_t overrideCount = 0;
    if (!readT(stream, overrideCount))
        overrideCount = 0;
    kit.overrides.clear();
    kit.overrides.reserve(overrideCount);
    for (std::uint16_t i = 0; i < overrideCount; ++i)
    {
        TierTwoOverride ov;
        if (!readT(stream, ov.src))   break;
        if (!readT(stream, ov.dst))   break;
        if (!readT(stream, ov.coeff)) break;
        if (ov.src < kit.pads.size() && ov.dst < kit.pads.size())
        {
            ov.coeff = std::clamp(ov.coeff, 0.0f,
                                  CouplingMatrix::kMaxCoefficient);
            kit.overrides.push_back(ov);
        }
    }

    for (auto& pad : kit.pads)
    {
        for (double& m : pad.macros)
        {
            if (!readT(stream, m))
                m = 0.5;
            m = std::clamp(m, 0.0, 1.0);
        }
    }

    // Optional session field: present only when the producer set hasSession.
    int32 uiMode = 0;
    if (readT(stream, uiMode))
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

    const int32 version = kPadBlobVersion;
    writeT(stream, version);

    const int32 excI  = static_cast<int32>(pad.exciterType);
    const int32 bodyI = static_cast<int32>(pad.bodyModel);
    writeT(stream, excI);
    writeT(stream, bodyI);

    for (double v : pad.sound)
        writeT(stream, v);

    return kResultOk;
}

tresult readPadPresetBlob(IBStream* stream, PadPresetSnapshot& pad)
{
    if (!stream)
        return kResultFalse;

    int32 version = 0;
    if (!readT(stream, version))
        return kResultFalse;
    if (version != kPadBlobVersion
        && version != kPadBlobVersionV3
        && version != kPadBlobVersionV2
        && version != kPadBlobVersionV1)
        return kResultFalse;
    const bool isV1 = (version == kPadBlobVersionV1);
    const bool isV2 = (version == kPadBlobVersionV2);
    const bool isV3 = (version == kPadBlobVersionV3);
    constexpr std::size_t kV4Slots = std::tuple_size_v<decltype(PadPresetSnapshot::sound)>;
    const std::size_t slotsToRead =
        isV1 ? kV6SoundSlotCount
             : (isV2 ? kV7SoundSlotCount
                     : (isV3 ? kV8SoundSlotCount : kV4Slots));

    int32 excI = 0;
    int32 bodyI = 0;
    if (!readT(stream, excI))  return kResultFalse;
    if (!readT(stream, bodyI)) return kResultFalse;
    excI = std::clamp(excI, int32{0},
                      static_cast<int32>(ExciterType::kCount) - 1);
    bodyI = std::clamp(bodyI, int32{0},
                       static_cast<int32>(BodyModelType::kCount) - 1);
    pad.exciterType = static_cast<ExciterType>(excI);
    pad.bodyModel   = static_cast<BodyModelType>(bodyI);

    for (std::size_t i = 0; i < slotsToRead; ++i)
    {
        if (!readT(stream, pad.sound[i]))
            return kResultFalse;
    }
    // v1 blobs lack Phase 7 sound slots (34-41). Fill with PadConfig defaults
    // so legacy per-pad presets load with sensible parallel-noise + click
    // parameters rather than silence.
    const PadConfig defaults{};
    if (isV1)
    {
        pad.sound[34] = static_cast<double>(defaults.noiseLayerMix);
        pad.sound[35] = static_cast<double>(defaults.noiseLayerCutoff);
        pad.sound[36] = static_cast<double>(defaults.noiseLayerResonance);
        pad.sound[37] = static_cast<double>(defaults.noiseLayerDecay);
        pad.sound[38] = static_cast<double>(defaults.noiseLayerColor);
        pad.sound[39] = static_cast<double>(defaults.clickLayerMix);
        pad.sound[40] = static_cast<double>(defaults.clickLayerContactMs);
        pad.sound[41] = static_cast<double>(defaults.clickLayerBrightness);
    }
    // v1 or v2 -> v3+: Phase 8A damping slots (42-43) are absent. Fill with
    // sentinel defaults so legacy pad presets keep Phase 1 bit-identity.
    if (isV1 || isV2)
    {
        pad.sound[42] = static_cast<double>(defaults.bodyDampingB1);
        pad.sound[43] = static_cast<double>(defaults.bodyDampingB3);
    }
    // v1, v2 or v3 -> v4: Phase 8C slots (44-45) are absent.
    if (isV1 || isV2 || isV3)
    {
        pad.sound[44] = static_cast<double>(defaults.airLoading);
        pad.sound[45] = static_cast<double>(defaults.modeScatter);
    }
    return kResultOk;
}

} // namespace Membrum::State
