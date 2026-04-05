// =============================================================================
// PinFlagStrip — Pure Logic (Humble Object Pattern)
// =============================================================================
// Spec 133 (Gradus v1.6).
// VSTGUI-free logic for the 32-cell pin toggle row. Tests exercise these
// free functions directly without instantiating any CControl. The PinFlagStrip
// CControl subclass (pin_flag_strip.h) delegates to these functions and only
// adds drawing, mouse handling, and callback dispatch on top.
//
// Mirrors the humble-object pattern used by Iterum's
// tap_pattern_editor_logic.h.
// =============================================================================

#pragma once

#include <array>
#include <cstddef>

namespace Gradus::PinFlagStripLogic {

inline constexpr int kNumSteps = 32;

using State = std::array<float, kNumSteps>;

// A cell is considered pinned at exactly 0.5 to match
// handleArpParamChange() in arpeggiator_params.h (which uses value >= 0.5).
inline constexpr float kPinThreshold = 0.5f;

inline bool isPinned(float value) { return value >= kPinThreshold; }

// Returns the cell index [0..numSteps-1] for a local x in [0, width),
// or -1 for out-of-range x, non-positive width, or non-positive numSteps.
// numSteps tracks the pitch lane's active length so cells align 1:1 with
// the pitch bars underneath regardless of the user's lane length setting.
inline int cellIndexForX(float x, float width, int numSteps)
{
    if (width <= 0.0f) return -1;
    if (numSteps <= 0 || numSteps > kNumSteps) return -1;
    if (x < 0.0f || x >= width) return -1;
    const int idx = static_cast<int>((x * static_cast<float>(numSteps)) / width);
    if (idx < 0) return 0;
    if (idx >= numSteps) return numSteps - 1;
    return idx;
}

// Flips the cell's pinned state. Out-of-range steps are a no-op.
// Returns the new normalized value (0.0f or 1.0f).
inline float toggleStep(State& state, int step)
{
    if (step < 0 || step >= kNumSteps) return 0.0f;
    const size_t idx = static_cast<size_t>(step);
    const float newValue = isPinned(state[idx]) ? 0.0f : 1.0f;
    state[idx] = newValue;
    return newValue;
}

// Host-driven update (preset load, automation). Returns true if the state
// actually changed (callers use this to decide whether to mark dirty / repaint).
// Out-of-range steps are a no-op and return false.
inline bool setStepValue(State& state, int step, float value)
{
    if (step < 0 || step >= kNumSteps) return false;
    const size_t idx = static_cast<size_t>(step);
    const float quantized = isPinned(value) ? 1.0f : 0.0f;
    if (state[idx] == quantized) return false;
    state[idx] = quantized;
    return true;
}

inline float getStepValue(const State& state, int step)
{
    if (step < 0 || step >= kNumSteps) return 0.0f;
    return state[static_cast<size_t>(step)];
}

} // namespace Gradus::PinFlagStripLogic
