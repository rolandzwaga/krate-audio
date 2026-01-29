#pragma once

// ==============================================================================
// Morph Link Mode Functions
// ==============================================================================
// T158: Link mode mapping functions for morph-sweep integration (US8, FR-032-034)
//
// These functions map sweep frequency position to morph axis position
// using various curve shapes.
//
// Reference: specs/006-morph-ui/plan.md "Morph Link Mode Equations"
// ==============================================================================

#include "plugin_ids.h"

namespace Disrumpo {

/// @brief Apply a morph link mode to convert sweep position to morph position.
/// @param mode The link mode to apply
/// @param sweepNorm Normalized sweep frequency position [0, 1] where 0 = 20Hz, 1 = 20kHz (log scale)
/// @param manualPosition The manual position to return when mode is None
/// @return Morph position [0, 1]
float applyMorphLinkMode(MorphLinkMode mode, float sweepNorm, float manualPosition);

/// @brief Convert sweep frequency in Hz to normalized position [0, 1].
/// @param frequencyHz Sweep frequency in Hz (20 to 20000)
/// @return Normalized position [0, 1] on log scale
float sweepFrequencyToNormalized(float frequencyHz);

/// @brief Convert normalized position to sweep frequency in Hz.
/// @param normalized Normalized position [0, 1]
/// @return Frequency in Hz (20 to 20000)
float normalizedToSweepFrequency(float normalized);

} // namespace Disrumpo
