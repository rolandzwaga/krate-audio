// =============================================================================
// Source-Mode Disable List
// =============================================================================
// Spec 142 (Gradus Piano-Roll Step Sequencer), Phase 6.
//
// Single source of truth for which controls must be visually disabled when
// Source = Sequencer (FR-036). The controller's syncViewsFromParams reads
// this list to apply setMouseEnabled(false) + setAlphaValue(0.4f) to each
// control's view, and the visibility tests read the same list to assert
// coverage of every FR-022 audio-thread-inert parameter.
//
// FR-021a controls (Transpose), FR-022a (Retrigger), and FR-022b (Spice /
// Dice / Humanize / per-lane speed-curve depth / swing / jitter) are NOT in
// this list — they remain enabled in both modes per spec.
// =============================================================================

#pragma once

#include "../plugin_ids.h"

#include <array>
#include <cstdint>

namespace Gradus {

// Total fixed-list IDs (top-level controls). Markov cells (49), Pin flags (32),
// and any future per-step Sequencer-disabled ranges are handled separately as
// contiguous ranges below to keep the static array compact.
inline constexpr std::array<uint32_t, 13> kSourceSequencerDisabledParamIds = {
    // FR-022 base mode/octave/scale/latch.
    kArpModeId,
    kArpOctaveRangeId,
    kArpOctaveModeId,
    kArpScaleQuantizeInputId,
    kArpLatchModeId,

    // FR-022 Euclidean (top-level controls).
    kArpEuclideanEnabledId,
    kArpEuclideanHitsId,
    kArpEuclideanStepsId,
    kArpEuclideanRotationId,

    // FR-022 Markov (preset dropdown — 49 cell IDs covered by the range below).
    kArpMarkovPresetId,

    // FR-022 Pin Note (PinFlag IDs 0..31 covered by the range below).
    kArpPinNoteId,

    // FR-022 Note Range Mapping.
    kArpRangeLowId,
    kArpRangeHighId,
    // kArpRangeModeId is right next to kArpRangeHighId; included separately
    // for clarity. (We keep the array literal size in sync — adjust if added.)
};

// Range helpers for contiguous per-step / per-cell ID blocks.
inline constexpr uint32_t kSourceSeqDisabledMarkovCellFirst = kArpMarkovCell00Id;
inline constexpr uint32_t kSourceSeqDisabledMarkovCellLast  = kArpMarkovCell66Id;

inline constexpr uint32_t kSourceSeqDisabledPinFlagFirst    = kArpPinFlagStep0Id;
inline constexpr uint32_t kSourceSeqDisabledPinFlagLast     = kArpPinFlagStep31Id;

// Range mode is a separate single ID.
inline constexpr uint32_t kSourceSeqDisabledRangeModeId     = kArpRangeModeId;

// Helper: returns true if the param ID belongs in the FR-036 disabled set.
inline constexpr bool isSourceSequencerDisabledParam(uint32_t id) noexcept {
    for (auto v : kSourceSequencerDisabledParamIds) {
        if (v == id) return true;
    }
    if (id >= kSourceSeqDisabledMarkovCellFirst &&
        id <= kSourceSeqDisabledMarkovCellLast) return true;
    if (id >= kSourceSeqDisabledPinFlagFirst &&
        id <= kSourceSeqDisabledPinFlagLast)    return true;
    if (id == kSourceSeqDisabledRangeModeId)    return true;
    return false;
}

} // namespace Gradus
