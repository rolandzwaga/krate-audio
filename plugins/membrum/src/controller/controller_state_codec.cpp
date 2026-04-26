// ==============================================================================
// Controller-side KitSnapshot bridge -- implementation.
// ==============================================================================
// Single source of truth for translating between `Membrum::State::KitSnapshot`
// and the controller's VST3 parameter values. See `controller_state_codec.h`
// for the full design rationale; this file holds the offset tables and the
// global-decode/encode logic that previously lived inline (and divergently)
// inside three separate entry points in `controller.cpp`.
// ==============================================================================

#include "controller_state_codec.h"

#include "plugin_ids.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "dsp/pad_config.h"

#include <algorithm>
#include <array>

namespace Membrum::ControllerState {

using Steinberg::Vst::ParamID;
using Steinberg::Vst::EditControllerEx1;

namespace {

// PadParamOffsets whose snapshot indices are 0..27 (Phase 1..6 contiguous
// block: material, size, decay, ..., morphCurve). The order MUST match the
// `PadSnapshot::sound` documentation in `state_codec.h`.
constexpr std::array<int, 28> kContiguousOffsets = {
    kPadMaterial, kPadSize, kPadDecay, kPadStrikePosition, kPadLevel,
    kPadTSFilterType, kPadTSFilterCutoff, kPadTSFilterResonance,
    kPadTSFilterEnvAmount, kPadTSDriveAmount, kPadTSFoldAmount,
    kPadTSPitchEnvStart, kPadTSPitchEnvEnd, kPadTSPitchEnvTime,
    kPadTSPitchEnvCurve,
    kPadTSFilterEnvAttack, kPadTSFilterEnvDecay,
    kPadTSFilterEnvSustain, kPadTSFilterEnvRelease,
    kPadModeStretch, kPadDecaySkew, kPadModeInjectAmount,
    kPadNonlinearCoupling,
    kPadMorphEnabled, kPadMorphStart, kPadMorphEnd,
    kPadMorphDuration, kPadMorphCurve,
};

// Exciter secondary block at sound indices 30..33 -> offsets 32..35.
constexpr std::array<int, 4> kExciterSecOffsets = {
    kPadFMRatio, kPadFeedbackAmount,
    kPadNoiseBurstDuration, kPadFrictionPressure,
};

// Phase 7+ "late" slots: each entry pairs a per-pad offset with its index
// in `PadSnapshot::sound`. Adding a new phase = appending one row here +
// one slot count in `state_codec.h`.
struct LateSlot {
    int         offset;
    std::size_t snapIndex;
};
constexpr std::array<LateSlot, 18> kLateSlots = {{
    // Phase 7 noise layer (sound[34..38] -> offsets 42..46).
    {kPadNoiseLayerMix,        34}, {kPadNoiseLayerCutoff,     35},
    {kPadNoiseLayerResonance,  36}, {kPadNoiseLayerDecay,      37},
    {kPadNoiseLayerColor,      38},
    // Phase 7 click layer (sound[39..41] -> offsets 47..49).
    {kPadClickLayerMix,        39}, {kPadClickLayerContactMs,  40},
    {kPadClickLayerBrightness, 41},
    // Phase 8A body damping (sound[42..43] -> offsets 50..51).
    {kPadBodyDampingB1,        42}, {kPadBodyDampingB3,        43},
    // Phase 8C air-loading + scatter (sound[44..45] -> offsets 52..53).
    {kPadAirLoading,           44}, {kPadModeScatter,          45},
    // Phase 8D head/shell coupling (sound[46..49] -> offsets 54..57).
    {kPadCouplingStrength,     46}, {kPadSecondaryEnabled,     47},
    {kPadSecondarySize,        48}, {kPadSecondaryMaterial,    49},
    // Phase 8E tension modulation (sound[50] -> offset 58).
    {kPadTensionModAmt,        50},
    // Phase 8F per-pad enable toggle (sound[51] -> offset 59).
    {kPadEnabled,              51},
}};

// Macro block (Phase 6): 5 normalised values, one per macro.
constexpr std::array<int, 5> kMacroOffsets = {
    kPadMacroTightness, kPadMacroBrightness, kPadMacroBodySize,
    kPadMacroPunch,     kPadMacroComplexity,
};

// VST3 API does not declare `getParamNormalized` const, but it is logically
// a read. Wrap the const_cast in one place.
double readParam(const EditControllerEx1& ctrl, ParamID id) noexcept
{
    return const_cast<EditControllerEx1&>(ctrl).getParamNormalized(id);
}

} // namespace

// ==============================================================================
// Per-pad bridging
// ==============================================================================

Membrum::State::PadSnapshot
buildPadSnapshotFromParams(const EditControllerEx1& ctrl, int pad) noexcept
{
    Membrum::State::PadSnapshot snap;

    // Discrete selectors -- map normalised values to their integer enum.
    const double excNorm = readParam(ctrl,
        static_cast<ParamID>(padParamId(pad, kPadExciterType)));
    const int excI = std::clamp(
        static_cast<int>(excNorm * static_cast<double>(ExciterType::kCount)),
        0, static_cast<int>(ExciterType::kCount) - 1);
    snap.exciterType = static_cast<ExciterType>(excI);

    const double bodyNorm = readParam(ctrl,
        static_cast<ParamID>(padParamId(pad, kPadBodyModel)));
    const int bodyI = std::clamp(
        static_cast<int>(bodyNorm * static_cast<double>(BodyModelType::kCount)),
        0, static_cast<int>(BodyModelType::kCount) - 1);
    snap.bodyModel = static_cast<BodyModelType>(bodyI);

    // Phase 1..6 contiguous block.
    for (std::size_t j = 0; j < kContiguousOffsets.size(); ++j)
    {
        snap.sound[j] = readParam(ctrl,
            static_cast<ParamID>(padParamId(pad, kContiguousOffsets[j])));
    }

    // Sound[28..29]: chokeGroup / outputBus float64 mirror (denormalised).
    const double chokeNorm = readParam(ctrl,
        static_cast<ParamID>(padParamId(pad, kPadChokeGroup)));
    const double busNorm = readParam(ctrl,
        static_cast<ParamID>(padParamId(pad, kPadOutputBus)));
    snap.sound[28] = chokeNorm * 8.0;
    snap.sound[29] = busNorm   * 15.0;

    // Sound[30..33]: exciter secondary block.
    for (std::size_t j = 0; j < kExciterSecOffsets.size(); ++j)
    {
        snap.sound[30 + j] = readParam(ctrl,
            static_cast<ParamID>(padParamId(pad, kExciterSecOffsets[j])));
    }

    // Sound[34..51]: Phase 7+ late slots.
    for (const auto& slot : kLateSlots)
    {
        snap.sound[slot.snapIndex] = readParam(ctrl,
            static_cast<ParamID>(padParamId(pad, slot.offset)));
    }

    // Authoritative uint8 choke/bus (quantised from normalised).
    snap.chokeGroup = static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(chokeNorm * 8.0 + 0.5), 0, 8));
    snap.outputBus = static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(busNorm * 15.0 + 0.5), 0, 15));

    snap.couplingAmount = readParam(ctrl,
        static_cast<ParamID>(padParamId(pad, kPadCouplingAmount)));

    for (std::size_t j = 0; j < kMacroOffsets.size(); ++j)
    {
        snap.macros[j] = readParam(ctrl,
            static_cast<ParamID>(padParamId(pad, kMacroOffsets[j])));
    }

    return snap;
}

void applyPadSnapshotToParams(
    int pad,
    const Membrum::State::PadSnapshot& snap,
    const ParamSetter& setter) noexcept
{
    // Discrete selectors are encoded as (index + 0.5) / count so the
    // round-trip lands exactly on the integer when the controller decodes.
    setter(static_cast<ParamID>(padParamId(pad, kPadExciterType)),
           (static_cast<double>(snap.exciterType) + 0.5) /
               static_cast<double>(ExciterType::kCount));
    setter(static_cast<ParamID>(padParamId(pad, kPadBodyModel)),
           (static_cast<double>(snap.bodyModel) + 0.5) /
               static_cast<double>(BodyModelType::kCount));

    // Phase 8G ORDERING: macros FIRST. Each macro arriving at the processor
    // through processParameterChanges fires MacroMapper::apply, which under
    // the incremental contract layers (newDelta - oldDelta) onto the
    // per-pad cfg.material/size/decay/etc. By writing all five macros
    // before the per-pad fields, the macros run on default cfg values
    // (cache initialised at 0.5 -> apply layers preset_macro - 0.5 worth
    // of delta onto defaults), and the subsequent per-pad-field writes
    // OVERWRITE whatever the macros affected with the preset's saved
    // post-macro values. End state: cfg = preset values, cache = preset
    // macros. Subsequent macro-knob movements then layer the correct
    // delta on top of the loaded preset. Pre-fix order (per-pad fields
    // first, macros last) drifted cfg fields by an extra macro_delta
    // every save-load cycle for any preset with non-neutral macros.
    for (std::size_t j = 0; j < kMacroOffsets.size(); ++j)
    {
        setter(static_cast<ParamID>(padParamId(pad, kMacroOffsets[j])),
               snap.macros[j]);
    }

    // Phase 1..6 contiguous block.
    for (std::size_t j = 0; j < kContiguousOffsets.size(); ++j)
    {
        setter(static_cast<ParamID>(padParamId(pad, kContiguousOffsets[j])),
               snap.sound[j]);
    }

    // Choke group / output bus: write from the authoritative uint8 values
    // (the float64 mirrors at sound[28..29] are write-only and discarded
    // on load by design).
    setter(static_cast<ParamID>(padParamId(pad, kPadChokeGroup)),
           static_cast<double>(snap.chokeGroup) / 8.0);
    setter(static_cast<ParamID>(padParamId(pad, kPadOutputBus)),
           static_cast<double>(snap.outputBus) / 15.0);

    // Exciter secondary block (sound[30..33]). Includes
    // kPadNoiseBurstDuration which is a Punch-macro target, so it must
    // come AFTER the macros block above to overwrite the macro-computed
    // value with the preset's saved one.
    for (std::size_t j = 0; j < kExciterSecOffsets.size(); ++j)
    {
        setter(static_cast<ParamID>(padParamId(pad, kExciterSecOffsets[j])),
               snap.sound[30 + j]);
    }

    // kPadCouplingAmount is a Complexity-macro target -- this write must
    // come AFTER the macros block above for the same reason.
    setter(static_cast<ParamID>(padParamId(pad, kPadCouplingAmount)),
           snap.couplingAmount);

    // Phase 7+ late slots. Without this the controller-side mirror would
    // silently drop noiseLayer / clickLayer / bodyDamping / airLoading /
    // secondary coupling / tensionModAmt / enabled on every kit load --
    // the on-wire blob carries them, but the UI / global proxies would
    // stay at PadConfig defaults until the user touched a knob.
    //
    // EXCEPTION: bodyDampingB1/B3 use a sentinel value of -1.0 to mean
    // "let the audio mapper derive damping from decay/material" (Phase 8A
    // bit-identity preservation). VST3 parameters are normalised to
    // [0, 1], so the sentinel cannot survive a controller-side write:
    // the clamping setter would store 0.0 and a notifying setter would
    // performEdit(0.0) into the host, clobbering the processor's sentinel
    // with an explicit "0.2 s^-1 decay floor" (which produces ~10 s
    // sustains on every pad). Skip the write when the snapshot value is
    // the sentinel; the processor keeps whatever it already had (sentinel
    // by default on a fresh PadConfig, or the most recent explicit value
    // the user set via the global proxy).
    for (const auto& slot : kLateSlots)
    {
        const bool isSentinelSlot =
            (slot.offset == kPadBodyDampingB1 || slot.offset == kPadBodyDampingB3);
        if (isSentinelSlot && snap.sound[slot.snapIndex] < 0.0)
            continue;
        setter(static_cast<ParamID>(padParamId(pad, slot.offset)),
               snap.sound[slot.snapIndex]);
    }
}

void applyPadPresetSnapshotToParams(
    int pad,
    const Membrum::State::PadPresetSnapshot& preset,
    const ParamSetter& setter) noexcept
{
    // Selectors.
    setter(static_cast<ParamID>(padParamId(pad, kPadExciterType)),
           (static_cast<double>(preset.exciterType) + 0.5) /
               static_cast<double>(ExciterType::kCount));
    setter(static_cast<ParamID>(padParamId(pad, kPadBodyModel)),
           (static_cast<double>(preset.bodyModel) + 0.5) /
               static_cast<double>(BodyModelType::kCount));

    // Phase 1..6 contiguous block (sound[0..27]).
    for (std::size_t j = 0; j < kContiguousOffsets.size(); ++j)
    {
        setter(static_cast<ParamID>(padParamId(pad, kContiguousOffsets[j])),
               preset.sound[j]);
    }

    // sound[28..29] (chokeGroup/outputBus float64 mirrors) intentionally
    // skipped per FR-061 -- per-pad presets MUST NOT overwrite kit-level
    // routing.

    // Exciter secondary block (sound[30..33]).
    for (std::size_t j = 0; j < kExciterSecOffsets.size(); ++j)
    {
        setter(static_cast<ParamID>(padParamId(pad, kExciterSecOffsets[j])),
               preset.sound[30 + j]);
    }

    // Phase 7+ late slots that fit within the preset's sound array. The
    // per-pad preset deliberately stops one slot short of the kit blob (it
    // omits `kPadEnabled` at index 51), so we filter by snapIndex. Same
    // bodyDamping sentinel guard as `applyPadSnapshotToParams` -- see the
    // comment there.
    const std::size_t kPresetSoundSize = preset.sound.size();
    for (const auto& slot : kLateSlots)
    {
        if (slot.snapIndex >= kPresetSoundSize)
            continue; // kit-level fields outside the preset blob (e.g. enabled)
        const bool isSentinelSlot =
            (slot.offset == kPadBodyDampingB1 || slot.offset == kPadBodyDampingB3);
        if (isSentinelSlot && preset.sound[slot.snapIndex] < 0.0)
            continue;
        setter(static_cast<ParamID>(padParamId(pad, slot.offset)),
               preset.sound[slot.snapIndex]);
    }

    // FR-061 exemptions: kPadCouplingAmount, kPadMacro* are NOT written.
}

// ==============================================================================
// Kit-level bridging
// ==============================================================================

Membrum::State::KitSnapshot
buildSnapshot(const EditControllerEx1& ctrl, int selectedPadIndex) noexcept
{
    Membrum::State::KitSnapshot kit;

    // Polyphony: stored as normalised over [4, 16].
    const double maxPolyNorm = readParam(ctrl, kMaxPolyphonyId);
    kit.maxPolyphony = std::clamp(
        4 + static_cast<int>(maxPolyNorm * 12.0 + 0.5), 4, 16);

    const double stealNorm = readParam(ctrl, kVoiceStealingId);
    kit.voiceStealingPolicy = std::clamp(
        static_cast<int>(stealNorm * 3.0), 0, 2);

    // Phase 5 globals.
    kit.globalCoupling = readParam(ctrl, kGlobalCouplingId);
    kit.snareBuzz      = readParam(ctrl, kSnareBuzzId);
    kit.tomResonance   = readParam(ctrl, kTomResonanceId);
    // Coupling delay: param is normalised [0, 1] over [0.5, 2.0] ms range.
    {
        const double cdNorm = readParam(ctrl, kCouplingDelayId);
        kit.couplingDelayMs = std::clamp(0.5 + cdNorm * 1.5, 0.5, 2.0);
    }

    // Phase 9: master gain stored as raw normalized [0, 1] over [-24, +12] dB.
    kit.masterGainNorm = readParam(ctrl, kMasterGainId);

    kit.selectedPadIndex = selectedPadIndex;

    for (int pad = 0; pad < kNumPads; ++pad)
    {
        kit.pads[static_cast<std::size_t>(pad)] =
            buildPadSnapshotFromParams(ctrl, pad);
    }

    return kit;
}

void applySnapshot(const Membrum::State::KitSnapshot& kit,
                   const ParamSetter& setter,
                   const ApplyOptions& opts) noexcept
{
    // Polyphony / stealing.
    setter(kMaxPolyphonyId,
           static_cast<double>(kit.maxPolyphony - 4) / 12.0);
    setter(kVoiceStealingId,
           (static_cast<double>(kit.voiceStealingPolicy) + 0.5) / 3.0);

    // Phase 5 globals.
    setter(kGlobalCouplingId, kit.globalCoupling);
    setter(kSnareBuzzId,      kit.snareBuzz);
    setter(kTomResonanceId,   kit.tomResonance);
    setter(kCouplingDelayId,
           std::clamp((kit.couplingDelayMs - 0.5) / 1.5, 0.0, 1.0));

    // Phase 9: master gain.
    setter(kMasterGainId, std::clamp(kit.masterGainNorm, 0.0, 1.0));

    // Per-pad parameter blocks.
    for (int pad = 0; pad < kNumPads; ++pad)
    {
        applyPadSnapshotToParams(
            pad, kit.pads[static_cast<std::size_t>(pad)], setter);
    }

    if (opts.applySelectedPad)
    {
        setter(kSelectedPadId,
               std::clamp(static_cast<double>(kit.selectedPadIndex) / 31.0,
                          0.0, 1.0));
    }
}

} // namespace Membrum::ControllerState
