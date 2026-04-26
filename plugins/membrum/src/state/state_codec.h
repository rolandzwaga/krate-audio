#pragma once

// ==============================================================================
// Membrum state codec -- ONE on-wire format for kit/state save & load.
//
// This header is the single source of truth for the Membrum plugin's
// persisted state layout. Both Processor::getState/setState and the
// Controller kit/pad preset providers route through the same write/read
// helpers to eliminate the previous 4-6x duplication of the pad-sound-
// parameter serialisation block.
//
// Current version: v1. The plugin has not shipped, so there is no legacy
// state to support -- the codec emits and accepts a single version.
// PadSnapshot::sound carries 52 slots; PadPresetSnapshot carries 51.
// ==============================================================================

#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"
#include "dsp/pad_config.h"

#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/base/ibstream.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Membrum::State {

// ============================================================================
// Data-only snapshots. These POD types decouple the on-wire layout from the
// runtime Processor / Controller classes.
// ============================================================================

/// Per-pad snapshot matching the on-wire layout. 44 float64 sound params
/// cover offsets 2-35, 42-49 and 50-51 from PadConfig (chokeGroup and
/// outputBus at indices 28-29 are redundantly expressed as float64 -- the
/// authoritative uint8 values below are the ones applied on load).
struct PadSnapshot
{
    ExciterType   exciterType{};
    BodyModelType bodyModel{};

    // indices 0-27   -> offsets 2-29  (material..morphCurve)
    // indices 28-29  -> offsets 30-31 (chokeGroup, outputBus as float64)
    // indices 30-33  -> offsets 32-35 (fmRatio, feedbackAmount,
    //                                  noiseBurstDuration, frictionPressure)
    // indices 34-38  -> offsets 42-46 (noiseLayer{Mix,Cutoff,Resonance,
    //                                              Decay,Color})
    // indices 39-41  -> offsets 47-49 (clickLayer{Mix,ContactMs,Brightness})
    // indices 42-43  -> offsets 50-51 (bodyDamping{B1,B3})  [Phase 8A]
    // indices 44-45  -> offsets 52-53 (airLoading, modeScatter)  [Phase 8C]
    // indices 46-49  -> offsets 54-57 (coupling + secondary)  [Phase 8D]
    // index 50       -> offset 58     (tensionModAmt)         [Phase 8E]
    // index 51       -> offset 59     (enabled)               [Phase 8F]
    std::array<double, 52> sound{};

    std::uint8_t chokeGroup{0};       ///< Authoritative (uint8) on load.
    std::uint8_t outputBus{0};        ///< Authoritative (uint8) on load.
    double       couplingAmount{0.5}; ///< Phase 5 offset 36.
    std::array<double, 5> macros{0.5, 0.5, 0.5, 0.5, 0.5}; ///< Phase 6 offsets 37-41.
};

/// Full kit / state snapshot.
struct KitSnapshot
{
    int                              maxPolyphony{8};
    int                              voiceStealingPolicy{0};
    std::array<PadSnapshot, 32>      pads{};
    double                           globalCoupling{0.0};
    double                           snareBuzz{0.0};
    double                           tomResonance{0.0};
    double                           couplingDelayMs{1.0};
    int                              selectedPadIndex{0};

    // Global master output gain, normalised [0..1] mapping linearly to
    // [-24..+12] dB. Default 0.5 (= -6 dB).
    double                           masterGainNorm{0.5};

    // Session-scoped (kit-preset only; NOT written to processor IBStream
    // state). uiMode: 0=Acoustic, 1=Extended. Kit authors may design for a
    // specific UI mode, so uiMode persists in the kit preset.
    int                              uiMode{0};

    // When true on write: uiMode is emitted.
    // When true on read: uiMode was present in the blob.
    bool                             hasSession{false};
};

/// Per-pad preset snapshot. Narrower slice than PadSnapshot -- excludes
/// chokeGroup, outputBus, couplingAmount, and macros (FR-061).
struct PadPresetSnapshot
{
    ExciterType   exciterType{};
    BodyModelType bodyModel{};
    std::array<double, 51> sound{}; ///< Same layout as PadSnapshot::sound (indices 28-29 written but ignored on load).
};

// ============================================================================
// On-wire version constants. Single current format -- no legacy versions are
// accepted on read. Bumping these is a breaking change.
// ============================================================================

constexpr Steinberg::int32 kBlobVersion    = 1;
constexpr Steinberg::int32 kPadBlobVersion = 1;

// ============================================================================
// Blob codec -- one format for full kit/state.
// Layout (little-endian as produced by IBStream::write):
//   [int32 version == kBlobVersion]
//   [int32 maxPolyphony]
//   [int32 voiceStealingPolicy]
//   Per pad (32 times):
//     [int32 exciterType]
//     [int32 bodyModel]
//     [52 x float64 sound (see PadSnapshot)]
//     [uint8 chokeGroup]
//     [uint8 outputBus]
//   [int32 selectedPadIndex]
//   [4 x float64 global coupling (gc, sb, tr, cd)]
//   [32 x float64 per-pad couplingAmount]
//   [160 x float64 macros pad-major: pad0.m0..pad0.m4, pad1.m0..pad1.m4, ...]
//   [float64 masterGainNorm]
//   If hasSession (kit-preset only):
//     [int32 uiMode]
//
// writeKitBlob: always succeeds for a valid stream.
// readKitBlob:  accepts only kBlobVersion. Returns kResultFalse on any other
//               version or on a short read.
//               uiMode is OPTIONAL on read -- if the stream is exhausted
//               after the master-gain field, kit.hasSession=false and uiMode
//               stays at its default.
// ============================================================================

Steinberg::tresult writeKitBlob(Steinberg::IBStream* stream,
                                const KitSnapshot& kit);

Steinberg::tresult readKitBlob(Steinberg::IBStream* stream,
                               KitSnapshot& kit);

// ============================================================================
// Per-pad preset codec.
// Layout:
//   [int32 version == kPadBlobVersion]
//   [int32 exciterType]
//   [int32 bodyModel]
//   [51 x float64 sound]
// Total: 420 bytes.
// ============================================================================

Steinberg::tresult writePadPresetBlob(Steinberg::IBStream* stream,
                                      const PadPresetSnapshot& pad);

Steinberg::tresult readPadPresetBlob(Steinberg::IBStream* stream,
                                     PadPresetSnapshot& pad);

// ============================================================================
// PadConfig <-> PadSnapshot bridging.
// ============================================================================

[[nodiscard]] PadSnapshot toPadSnapshot(const PadConfig& cfg) noexcept;
void applyPadSnapshot(const PadSnapshot& snap, PadConfig& cfg) noexcept;

[[nodiscard]] PadPresetSnapshot toPadPresetSnapshot(const PadConfig& cfg) noexcept;
/// Applies sound-only fields; chokeGroup, outputBus, couplingAmount and
/// macros are left untouched per FR-061.
void applyPadPresetSnapshot(const PadPresetSnapshot& snap, PadConfig& cfg) noexcept;

} // namespace Membrum::State
