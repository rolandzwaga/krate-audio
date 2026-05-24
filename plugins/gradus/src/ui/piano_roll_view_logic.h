// =============================================================================
// PianoRollView — Pure Logic (Humble Object Pattern)
// =============================================================================
// Spec 142 (Gradus Piano-Roll Step Sequencer), Phase 6.
//
// VSTGUI-free logic for the Sequencer Note lane piano-roll view. Tests
// exercise these free functions and the StateMachine directly without
// instantiating any CView. The PianoRollView CView subclass (piano_roll_view.h)
// delegates to these helpers and only adds drawing, parameter binding, and
// VSTGUI mouse/lifecycle dispatch on top.
//
// Mirrors the humble-object pattern used by pin_flag_strip_logic.h.
// =============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Gradus { namespace PianoRollViewLogic {

// Grid constants — FR-028: exactly 4 octaves (C2..B5).
inline constexpr int kMidiLow      = 36;                                // C2
inline constexpr int kMidiHigh     = 83;                                // B5
inline constexpr int kPitchRows    = kMidiHigh - kMidiLow + 1;          // 48
inline constexpr int kMaxSteps     = 32;                                // FR-005 max length
inline constexpr int kPlayheadParamScale = 32;                          // FR-034a

static_assert(kPitchRows == 48,
              "FR-028: piano roll must show exactly 48 rows (C2..B5)");

// -----------------------------------------------------------------------------
// Per-step value cache. Mirrored from VST3 parameters via IDependent::update.
// -----------------------------------------------------------------------------
struct StepData {
    std::uint8_t pitch = 60;   // 0..127 (default C4 per FR-006)
    bool         isRest = true; // FR-007 default
};

using StepArray = std::array<StepData, kMaxSteps>;

// -----------------------------------------------------------------------------
// Geometry helpers (cellRect-style hit-tests). Local coordinates: (0,0) at
// view top-left; row 0 at the TOP corresponds to MIDI 83 (B5), row 47 at the
// BOTTOM corresponds to MIDI 36 (C2). Steps go left-to-right.
// -----------------------------------------------------------------------------

// Width of a single step column (active length determines column count).
[[nodiscard]] inline float colWidth(float viewWidth, int activeLength) noexcept {
    if (activeLength <= 0) return 0.0f;
    return viewWidth / static_cast<float>(activeLength);
}

// Height of a single pitch row.
[[nodiscard]] inline float rowHeight(float viewHeight) noexcept {
    return viewHeight / static_cast<float>(kPitchRows);
}

// Map an x in [0, viewWidth) to a step index in [0, activeLength-1] or -1.
[[nodiscard]] inline int stepFromX(float x, float viewWidth, int activeLength) noexcept {
    if (viewWidth <= 0.0f || activeLength <= 0) return -1;
    if (x < 0.0f || x >= viewWidth) return -1;
    const float cw = colWidth(viewWidth, activeLength);
    if (cw <= 0.0f) return -1;
    int idx = static_cast<int>(x / cw);
    if (idx < 0) return 0;
    if (idx >= activeLength) return activeLength - 1;
    return idx;
}

// Clamp x into the active step range and return the resulting step index, or
// the nearest in-range index for negative / out-of-bounds x. Always returns a
// valid step in [0, activeLength-1] (assumes activeLength > 0). Used during
// drag to keep painting within the visible columns.
[[nodiscard]] inline int stepFromXClamped(float x, float viewWidth, int activeLength) noexcept {
    if (activeLength <= 0) return -1;
    if (viewWidth <= 0.0f) return 0;
    if (x < 0.0f) return 0;
    if (x >= viewWidth) return activeLength - 1;
    const float cw = colWidth(viewWidth, activeLength);
    if (cw <= 0.0f) return 0;
    int idx = static_cast<int>(x / cw);
    if (idx < 0) return 0;
    if (idx >= activeLength) return activeLength - 1;
    return idx;
}

// Map a y in [0, viewHeight) to a MIDI pitch in [kMidiLow, kMidiHigh] or -1.
// y=0 corresponds to the top row (MIDI 83 / B5); y=viewHeight-1 corresponds to
// the bottom row (MIDI 36 / C2).
[[nodiscard]] inline int pitchFromY(float y, float viewHeight) noexcept {
    if (viewHeight <= 0.0f) return -1;
    if (y < 0.0f || y >= viewHeight) return -1;
    const float rh = rowHeight(viewHeight);
    if (rh <= 0.0f) return -1;
    int row = static_cast<int>(y / rh);
    if (row < 0) row = 0;
    if (row >= kPitchRows) row = kPitchRows - 1;
    return kMidiHigh - row;
}

// Convert a normalized playhead param value (0..1) into a step index in
// [0, activeLength-1] (FR-034a). Returns -1 if activeLength <= 0.
[[nodiscard]] inline int playheadStepFromParam(double normalizedValue, int activeLength) noexcept {
    if (activeLength <= 0) return -1;
    double clamped = std::clamp(normalizedValue, 0.0, 1.0);
    int step = static_cast<int>(clamped * static_cast<double>(kPlayheadParamScale));
    if (step < 0) return 0;
    if (step >= activeLength) return activeLength - 1;
    return step;
}

// -----------------------------------------------------------------------------
// Single-click resolution per FR-030.
// Inputs are the step's current state and the row the user clicked.
// Returns the new (pitch, isRest) to write.
// -----------------------------------------------------------------------------
struct EditResult {
    int  pitch  = 60;
    bool isRest = false;
};

[[nodiscard]] inline EditResult resolveSingleClick(StepData current, int clickedPitch) noexcept {
    // Click on a resting step → place note at clicked row (FR-030).
    if (current.isRest) {
        return EditResult{ clickedPitch, false };
    }
    // Same-pitch click → toggle to rest (FR-030).
    if (static_cast<int>(current.pitch) == clickedPitch) {
        return EditResult{ static_cast<int>(current.pitch), true };
    }
    // Different-pitch click → silent replace (FR-030).
    return EditResult{ clickedPitch, false };
}

// -----------------------------------------------------------------------------
// Mouse state machine per data-model.md Entity 5.
// Pure: holds drag state and decides what edits to emit. The view wraps this
// and applies the edits via editParamWithNotify.
// -----------------------------------------------------------------------------
class MouseStateMachine {
public:
    enum class State { kIdle, kDragging };

    struct PendingEdit {
        bool valid  = false;
        int  step   = -1;
        int  pitch  = 60;
        bool isRest = false;
    };

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] int   lastPaintedStep() const noexcept { return lastPaintedStep_; }
    [[nodiscard]] int   dragPitch() const noexcept { return dragPitch_; }
    [[nodiscard]] int   dragStartStep() const noexcept { return dragStartStep_; }

    // Reset to IDLE (used by removed()/dtor in defensive paths).
    void reset() noexcept {
        state_ = State::kIdle;
        dragPitch_ = 60;
        dragStartStep_ = -1;
        lastPaintedStep_ = -1;
    }

    // ---- LEFT button down ----
    // Captures the lock-to-start pitch and step (FR-031). Returns no edit yet;
    // edits happen on mouseMoved (drag paint) or on mouseUp (single click).
    void onLeftMouseDown(int clickedStep, int clickedPitch) noexcept {
        state_ = State::kDragging;
        dragStartStep_ = clickedStep;
        dragPitch_ = clickedPitch;
        lastPaintedStep_ = -1;
    }

    // ---- RIGHT button down ----
    // Per FR-032: set rest on the clicked step. Idempotent (right-click on
    // existing rest is a no-op edit but still returns a valid edit; the
    // controller path collapses it via Parameter::setNormalized noop).
    // Does NOT enter drag mode. Returns an edit when step is in range.
    [[nodiscard]] PendingEdit onRightMouseDown(int clickedStep, StepData current) const noexcept {
        if (clickedStep < 0) return PendingEdit{};
        // Preserve the step's current pitch; only force rest=true.
        return PendingEdit{
            true,
            clickedStep,
            static_cast<int>(current.pitch),
            true
        };
    }

    // ---- LEFT mouse moved while dragging ----
    // Per FR-031: paint locked pitch into the column under the cursor,
    // skipping the drag-start step (which is reserved for single-click
    // resolution on mouseUp) and any already-painted step. Drag NEVER toggles
    // to rest. Returns a valid edit if a new column should be painted.
    [[nodiscard]] PendingEdit onMouseMovedDragging(int stepUnderCursor) noexcept {
        if (state_ != State::kDragging) return PendingEdit{};
        if (stepUnderCursor < 0) return PendingEdit{};
        if (stepUnderCursor == dragStartStep_) return PendingEdit{};
        if (stepUnderCursor == lastPaintedStep_) return PendingEdit{};
        lastPaintedStep_ = stepUnderCursor;
        return PendingEdit{ true, stepUnderCursor, dragPitch_, /*isRest=*/false };
    }

    // ---- LEFT mouse up ----
    // If no painting occurred during the drag (lastPaintedStep == -1), this
    // was an isolated single click: resolve per FR-030 using the cached
    // start-step's current state. Returns the edit and transitions to IDLE.
    [[nodiscard]] PendingEdit onLeftMouseUp(StepData currentAtStart) noexcept {
        PendingEdit edit{};
        if (state_ == State::kDragging && lastPaintedStep_ == -1 && dragStartStep_ >= 0) {
            auto r = resolveSingleClick(currentAtStart, dragPitch_);
            edit = PendingEdit{ true, dragStartStep_, r.pitch, r.isRest };
        }
        state_ = State::kIdle;
        dragStartStep_ = -1;
        lastPaintedStep_ = -1;
        return edit;
    }

private:
    State state_           = State::kIdle;
    int   dragPitch_       = 60;
    int   dragStartStep_   = -1;
    int   lastPaintedStep_ = -1;
};

// -----------------------------------------------------------------------------
// Length clamping for the active-area helper used by the view.
// -----------------------------------------------------------------------------
[[nodiscard]] inline int clampActiveLength(int length) noexcept {
    if (length < 1) return 1;
    if (length > kMaxSteps) return kMaxSteps;
    return length;
}

}} // namespace Gradus::PianoRollViewLogic
