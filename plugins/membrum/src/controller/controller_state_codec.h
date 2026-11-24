#pragma once

// ==============================================================================
// Controller-side KitSnapshot bridge.
// ==============================================================================
// This module is the single source of truth for translating between
// `Membrum::State::KitSnapshot` (the on-wire data POD) and the controller's
// VST3 parameter values. It is shared by all three kit-level entry points
// on the controller side:
//
//   * `Controller::setComponentState`    — host-driven DAW project load
//   * `Controller::kitPresetLoadProvider` — controller-driven kit preset load
//   * `Controller::kitPresetStateProvider` — controller-driven kit preset save
//
// Side effects that depend on cached `Controller` state (PadGridView mirror,
// `syncGlobalProxyFromPad`, `selectedPadIndex_` field, session-scoped uiMode)
// stay at the call sites because they cannot be expressed without access to
// `Controller`. The kit-level helpers below cover everything that is purely
// a function of (KitSnapshot, ParamSetter).
// ==============================================================================

#include "state/state_codec.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <array>
#include <functional>

namespace Membrum::ControllerState {

/// The per-pad sound fields that no controller parameter can hold, mirrored
/// from the last loaded state so a preset SAVE from the controller writes
/// what the processor plays (MB-087, MB-162): `noiseLayerGain` (sound[57],
/// 0.1..16) has no ParamID. (Body damping b1 / b3 are ordinary parameter
/// values since fix plan T1.23: 0.5 is the derived law, no sentinel.)
/// The default is `PadConfig`'s (noiseLayerGain 1.0).
struct PadParamlessFields {
    double noiseLayerGain = 1.0;
};

/// Read the mirror fields of one pad from a snapshot's sound array (the
/// layout of `PadSnapshot::sound` and `PadPresetSnapshot::sound`).
[[nodiscard]] PadParamlessFields paramlessFieldsFromSnapshot(
    const std::array<double, 59>& sound) noexcept;

/// Type-erased parameter writer. Every caller (setComponentState and the kit
/// and pad preset load providers) passes a plain
/// `EditControllerEx1::setParamNormalized`: the processor already holds the
/// values (host setState, or the "LoadPresetState" / "LoadPadPreset"
/// message), so a load performs no `beginEdit / performEdit / endEdit`
/// (MB-152). The preset load providers tell the host through
/// `restartComponent(kParamValuesChanged)` and `setDirty` instead.
using ParamSetter =
    std::function<void(Steinberg::Vst::ParamID, double)>;

/// Encode a single pad's controller parameters into a `PadSnapshot`. The
/// snapshot's slot layout matches the on-wire format defined in
/// `state_codec.h`; see the comments on `Membrum::State::PadSnapshot::sound`.
/// noiseLayerGain (sound[57]) comes from `paramless`, the controller's mirror
/// of the loaded pad.
[[nodiscard]] Membrum::State::PadSnapshot
buildPadSnapshotFromParams(
    const Steinberg::Vst::EditControllerEx1& ctrl, int pad,
    const PadParamlessFields& paramless) noexcept;

/// Apply a `PadSnapshot` to a single pad's controller parameters. Writes
/// every active offset (Phase 1..8F) -- adding a new offset only requires
/// adding one row to the offset tables in this function's implementation.
void applyPadSnapshotToParams(
    int pad,
    const Membrum::State::PadSnapshot& snap,
    const ParamSetter& setter) noexcept;

/// Apply a `PadPresetSnapshot` to a single pad's controller parameters.
/// The controller mirror of `State::applyPadPresetSnapshot` (R-203): writes
/// the sound-domain offsets that function applies, and SKIPS what it skips:
///   * `kPadChokeGroup`, `kPadOutputBus` (FR-061: kit-level routing)
///   * `kPadCouplingAmount`             (FR-061: kit-level coupling)
///   * Phase 6 macros                   (FR-061: kit-level macros)
///   * `kPadEnabled`, `kPadPan`         (FR-061: the target pad keeps its
///                                       on/off state and position)
/// so that loading a per-pad preset onto a pad changes its sound but not
/// its kit-level role, its on/off state or its pan.
void applyPadPresetSnapshotToParams(
    int pad,
    const Membrum::State::PadPresetSnapshot& preset,
    const ParamSetter& setter) noexcept;

/// Encode the entire controller state into a `KitSnapshot`:
///   * Polyphony / voice stealing
///   * Phase 5 globals (globalCoupling / snareBuzz / tomResonance /
///     couplingDelayMs)
///   * `selectedPadIndex` (taken as a parameter because it lives outside
///     the parameter system)
///   * 32 per-pad snapshots, each with its `paramless` mirror entry
///
/// The snapshot's `hasSession`, `uiMode`, and `overrides` fields are NOT
/// touched here -- callers fill them based on their context (kit-preset
/// save sets `hasSession=true`; processor `getState` does not).
[[nodiscard]] Membrum::State::KitSnapshot
buildSnapshot(const Steinberg::Vst::EditControllerEx1& ctrl,
              int selectedPadIndex,
              const std::array<PadParamlessFields, kNumPads>& paramless) noexcept;

/// Behavioural switches for `applySnapshot`. Both default to false; each
/// call site enables only the parts it needs.
struct ApplyOptions {
    /// When true, the helper writes `kSelectedPadId` from
    /// `kit.selectedPadIndex`. setComponentState=true; kit-preset
    /// load=false (per FR-052: a kit-preset load preserves the user's
    /// current pad selection).
    bool applySelectedPad = false;
};

/// Decode a `KitSnapshot` into the controller's parameters via `setter`.
/// Writes:
///   * Polyphony / voice stealing
///   * Phase 5 globals (4 coupling params)
///   * 32 per-pad parameter blocks via `applyPadSnapshotToParams`
///   * `kSelectedPadId` if `opts.applySelectedPad` is set
///
/// Does NOT touch `kUiModeId` (session-scoped, handled at call sites with
/// path-specific semantics) and does NOT call `syncGlobalProxyFromPad` or
/// `PadGridView::setPadEnabled` (side effects outside the parameter system,
/// handled at call sites).
void applySnapshot(const Membrum::State::KitSnapshot& kit,
                   const ParamSetter& setter,
                   const ApplyOptions& opts) noexcept;

} // namespace Membrum::ControllerState
