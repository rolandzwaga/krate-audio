// ==============================================================================
// Layer 0: Core Utility - Transport Sync
// ==============================================================================
// Shared step-position calculation from host PPQ (projectTimeMusic).
// Used by TranceGate, ArpeggiatorCore, and any future tempo-synced
// step-sequenced components.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocation)
// - Principle III: Modern C++ (constexpr, header-only)
// - Principle IX: Layer 0 (depends only on note_value.h, Layer 0)
// ==============================================================================

#pragma once

#include "note_value.h"

#include <cmath>

namespace Krate {
namespace DSP {

// =============================================================================
// MusicalStepPosition
// =============================================================================

/// @brief Result of calculating step position from host PPQ.
struct MusicalStepPosition {
    int step = 0;              ///< Which step we're on [0, numSteps)
    double stepFraction = 0.0; ///< Progress within step [0.0, 1.0)
};

// =============================================================================
// calculateMusicalStepPosition
// =============================================================================

/// @brief Calculate step position from host PPQ (projectTimeMusic).
///
/// Given the host's musical position in quarter notes, determines which step
/// of a repeating pattern we're on and how far through that step we are.
/// Handles negative PPQ (pre-count) and wraps around the pattern length.
///
/// @param ppq           Host musical position in quarter notes
/// @param noteValue     Step rate note value
/// @param noteModifier  Step rate note modifier
/// @param numSteps      Total steps in pattern (must be >= 1)
/// @return Step index and fractional position within step
///
/// @example
/// @code
/// // At PPQ 2.0 with 1/4 note steps and 4 steps:
/// // beatsPerStep = 1.0, patternLength = 4.0 beats
/// // posInPattern = fmod(2.0, 4.0) = 2.0
/// // step = floor(2.0 / 1.0) % 4 = 2
/// // stepFraction = fmod(2.0, 1.0) / 1.0 = 0.0
/// auto pos = calculateMusicalStepPosition(2.0, NoteValue::Quarter, NoteModifier::None, 4);
/// // pos.step == 2, pos.stepFraction == 0.0
/// @endcode
[[nodiscard]] inline MusicalStepPosition calculateMusicalStepPosition(
    double ppq,
    NoteValue noteValue,
    NoteModifier noteModifier,
    int numSteps) noexcept
{
    const double beatsPerStep = static_cast<double>(
        getBeatsForNote(noteValue, noteModifier));
    if (beatsPerStep <= 0.0 || numSteps < 1) {
        return {};
    }

    const double patternLengthBeats = beatsPerStep * numSteps;
    double posInPattern = std::fmod(ppq, patternLengthBeats);
    if (posInPattern < 0.0) posInPattern += patternLengthBeats;

    const int step = static_cast<int>(posInPattern / beatsPerStep) % numSteps;
    const double stepFraction = std::fmod(posInPattern, beatsPerStep) / beatsPerStep;

    return {step, stepFraction};
}

} // namespace DSP
} // namespace Krate
