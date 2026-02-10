# Research: ADSRDisplay Custom Control

**Feature**: 048-adsr-display | **Date**: 2026-02-10

## R-001: Multi-Parameter Communication Pattern for CControl

**Context**: The ADSRDisplay must communicate with 7+ parameters per instance (4 ADSR time/level + 3 curve amounts minimum, plus up to 12 Bezier params). CControl's built-in `tag` system only supports a single parameter.

**Research Findings**:
- **XYMorphPad pattern**: Uses `CControl::tag` for X parameter and `controller_->performEdit(secondaryParamId_, value)` for the Y parameter. The controller pointer is set via `setController(this)` in `verifyView()`. This is a "dual-parameter" pattern.
- **StepPatternEditor pattern**: Uses `ParameterCallback` (`std::function<void(uint32_t paramId, float normalizedValue)>`) along with `EditCallback` for beginEdit/endEdit. The controller wires these callbacks in `verifyView()`. This supports N parameters with a single callback mechanism.

**Decision**: Use the StepPatternEditor callback pattern (ParameterCallback + EditCallback). This is the established project pattern for multi-parameter controls and scales cleanly to 7+ parameters without requiring a separate `performEdit` call per parameter.

**Rationale**: The ParameterCallback pattern is already proven in this codebase, supports arbitrary parameter IDs, and cleanly separates the control from the controller (the control never holds a direct controller pointer, only callbacks).

**Alternatives Considered**:
1. XYMorphPad's direct `controller_->performEdit()` approach -- rejected because it couples the control to a specific controller interface and becomes verbose with 7+ parameters
2. One CControl per parameter (compound control) -- rejected because it contradicts the spec's single custom CControl design

---

## R-002: Logarithmic Time Axis with Minimum Segment Width

**Context**: FR-009/FR-010 require logarithmic time scaling with 15% minimum segment width. Need to determine the exact algorithm.

**Research Findings**:
- Logarithmic scaling means segment width proportional to `log(timeMs)` rather than `timeMs`. This naturally compresses long segments and expands short ones.
- The 15% minimum constraint is applied after logarithmic scaling.
- With 4 segments (Attack, Decay, Sustain-Hold, Release), the sustain-hold is fixed at 25% of display width.
- The remaining 75% is distributed among Attack, Decay, and Release using logarithmic scaling, then clamped to 15% minimum.

**Decision**: Two-pass algorithm:
1. Compute logarithmic proportions: `w_i = log(1 + timeMs_i) / sum(log(1 + timeMs_j))` for the 75% available space.
2. Clamp: any segment below 15% gets raised to 15%, then renormalize the rest.

**Rationale**: This matches the spec clarification: "log scale first, then clamp." The `log(1 + t)` form avoids issues when timeMs approaches 0.

**Alternatives Considered**:
1. Linear scaling -- rejected because extreme ratios (0.1ms : 10000ms) make short segments invisible
2. Square root scaling -- considered but logarithmic was explicitly specified

---

## R-003: Curve Amount to Curve Shape Mapping

**Context**: FR-043/FR-044 require replacing the discrete EnvCurve enum with a continuous [-1, +1] curve parameter and 256-entry lookup tables.

**Research Findings**:
- Current system uses `EnvCurve` enum with 3 values: Exponential (one-pole), Linear, Logarithmic (linear-in-log-domain).
- The current ADSREnvelope uses a per-sample one-pole filter for exponential/linear curves and a linear-phase-in-log-domain approach for logarithmic curves.
- The spec proposes: `output = phase^(2^(curve * k))` where k controls curvature range.
- This is a power curve: when curve=0, exponent=1 (linear). When curve>0, exponent>1 (exponential: slow start, fast end). When curve<0, exponent<1 (logarithmic: fast start, slow end).
- k=3 gives a range from `phase^(1/8)` (very logarithmic) to `phase^8` (very exponential), which matches the visual expectation.
- Existing discrete values map: Logarithmic ~ -0.7, Linear = 0.0, Exponential ~ +0.7.

**Decision**:
- Use power curve formula: `output = phase^(2^(curve * k))` with k=3.
- Generate 256-entry lookup tables, one per segment.
- Tables regenerated on parameter change, not per sample.
- Audio thread does linear interpolation in the table for output.
- The continuous curve replaces the EnvCurve enum in `setAttackCurve()`, `setDecayCurve()`, `setReleaseCurve()`.

**Rationale**: Power curves are computationally simple, produce musically useful shapes, and the table-based approach means zero per-sample cost beyond a table lookup. The mapping `2^(curve*k)` provides symmetric logarithmic/exponential behavior around linear.

**Alternatives Considered**:
1. Keep one-pole filter approach with continuous target ratio -- rejected because the table approach is simpler and allows Bezier curves later
2. Cubic spline interpolation -- more complex, no audible benefit with 256 entries

---

## R-004: Bezier Curve Evaluation and Table Generation

**Context**: FR-027/FR-030 require cubic Bezier curves with 2 control points per segment, generating the same 256-entry lookup table.

**Research Findings**:
- A cubic Bezier curve B(t) = (1-t)^3 * P0 + 3(1-t)^2*t * P1 + 3(1-t)*t^2 * P2 + t^3 * P3.
- For envelope segments: P0 = (0, startLevel), P3 = (1, endLevel), P1 and P2 are the user-draggable control points normalized within the segment bounding box.
- The x-coordinate of the Bezier is NOT necessarily monotonic (handles can cross), but the table is indexed by phase (uniform x). Need to solve for t given x, then evaluate y.
- For table generation: iterate t from 0 to 1 in small steps, compute (x,y), then resample to uniform x spacing.
- Alternatively, use adaptive subdivision or Newton-Raphson to find t for each uniform x value.

**Decision**: For table generation, use the sampling approach: evaluate the Bezier at 1024 uniform t values, store (x,y) pairs, then resample to 256 uniform x values using linear interpolation. This handles non-monotonic x gracefully (S-curves with overshoots).

**Rationale**: Sampling at 4x resolution then resampling is simple, handles all curve shapes including S-curves, and the generation cost is negligible since it only runs on parameter change.

**Alternatives Considered**:
1. Newton-Raphson t-for-x solver -- more accurate but unnecessary complexity for 256-entry tables
2. De Casteljau subdivision -- elegant but more complex to implement correctly

---

## R-005: VSTGUI Custom Drawing APIs

**Context**: The ADSRDisplay needs to render filled curves, dashed lines, text labels, and control points using VSTGUI's drawing API.

**Research Findings**:
- `CDrawContext` provides: `drawLine()`, `drawRect()`, `fillRect()`, `drawEllipse()`, `fillEllipse()`, `drawPolygon()`, `fillPolygon()`.
- `CGraphicsPath` (via `context->createGraphicsPath()`) provides: `addLine()`, `addCurve()` (cubic Bezier), `beginSubpath()`, `closeSubpath()`.
- For filled envelope curves: create a `CGraphicsPath`, trace the envelope from start to end, then close the path down to the baseline. Use `context->drawGraphicsPath()` with `kPathFilled` for the fill and then trace again with `kPathStroked` for the stroke.
- Dashed lines: `CLineStyle` with `setDashLengths()` for the sustain hold and gate marker lines.
- Text: `context->drawString()` with `CFontRef` for time labels.
- Diamond shapes (Bezier handles): Draw rotated square path with 4 points.

**Decision**: Use `CGraphicsPath` for the envelope curve (filled + stroked), `CLineStyle` with dash pattern for sustain/gate lines, and `drawEllipse()` for control points. Use `drawString()` for labels.

**Rationale**: CGraphicsPath is the standard VSTGUI approach for complex curves. The other controls in this project (ArcKnob, XYMorphPad) already use similar drawing APIs.

---

## R-006: Playback Dot Communication via IMessage

**Context**: FR-034-037 require a playback dot showing current envelope position. The processor must communicate voice activity and envelope stage/level to the controller.

**Research Findings**:
- Existing pattern in Ruinae: Processor sends atomic pointer addresses via IMessage to the controller. The controller reads the atomic values on a timer.
- For the TranceGate, the processor sends `&tranceGatePlaybackStep_` and `&isTransportPlaying_` as int64 pointers in an IMessage.
- The controller stores these pointers and reads them on a CVSTGUITimer tick.
- For envelope playback, we need: envelope stage (EnvStage enum), envelope output level (float), and voice activity flag.
- Per-voice data: the spec says "most recently triggered voice." The processor can maintain a single "display voice" atomic struct with {stage, output, noteOnTimestamp}.

**Decision**: Follow the same IMessage pointer pattern. The processor will maintain per-envelope atomic display state:
- `std::atomic<float> envDisplayOutput_[3]` (one per envelope)
- `std::atomic<int> envDisplayStage_[3]` (one per envelope, cast from EnvStage)
- `std::atomic<bool> envVoiceActive_`

The processor sends these atomic addresses to the controller via IMessage. The controller reads them on a ~30fps timer and updates the ADSRDisplay.

**Rationale**: Consistent with the existing TranceGate playback pattern. Using atomics for lock-free communication between audio and UI threads.

**Alternatives Considered**:
1. Sending envelope values per process() call via IMessage -- rejected due to overhead (IMessage is not real-time safe at high rates)
2. Ring buffer -- rejected as overkill for a display-only feature at 30fps

---

## R-007: Simple-to-Bezier and Bezier-to-Simple Mode Conversion

**Context**: FR-032/FR-033 require lossless conversion from Simple to Bezier and lossy conversion from Bezier to Simple.

**Research Findings**:
- **Simple to Bezier (FR-033)**: For a power curve `y = x^p`, the equivalent cubic Bezier control points that approximate the shape are:
  - For curve amount c, compute power p = 2^(c*k).
  - Place cp1 and cp2 to match the power curve at t=1/3 and t=2/3.
  - Simpler approach: for a convex/concave curve, place cp1.x = cp2.x = 0.5, cp1.y and cp2.y chosen to match the power curve at phase 0.5.
  - Even simpler: default Bezier handles at (1/3, y(1/3)) and (2/3, y(2/3)) where y is the power curve.

- **Bezier to Simple (FR-032)**: Sample the Bezier at 50% phase and find the curve amount that produces the same value. Given output_50 = 0.5^(2^(c*k)), solve for c: c = log2(log(output_50) / log(0.5)) / k. This is the spec's specified algorithm.

**Decision**: Implement both conversions as specified:
- Simple-to-Bezier: Generate Bezier handles that reproduce the power curve shape by placing cp1 at (1/3, powerCurve(1/3)) and cp2 at (2/3, powerCurve(2/3)).
- Bezier-to-Simple: Sample Bezier at phase 0.5, derive curve amount using the inverse power formula.

**Rationale**: The spec explicitly defines the Bezier-to-Simple algorithm. The Simple-to-Bezier approach using 1/3 and 2/3 sample points provides a good cubic approximation of the power curve.

---

## R-008: DSP Refactoring Scope -- ADSREnvelope Modifications

**Context**: The ADSREnvelope currently uses EnvCurve enum (3 discrete values) and per-sample one-pole processing. This needs to change to support continuous curves via lookup tables.

**Research Findings**:
- Current `ADSREnvelope` (Layer 1) uses:
  - `EnvCurve attackCurve_`, `decayCurve_`, `releaseCurve_` fields
  - `setAttackCurve(EnvCurve)`, `setDecayCurve(EnvCurve)`, `setReleaseCurve(EnvCurve)` methods
  - `calcAttackCoefficients()` etc. which use `getAttackTargetRatio(EnvCurve)` to select coefficients
  - Per-sample processing: `processAttack()` uses `output_ = output_ * attackCoef_ + attackBase_` (one-pole) or linear-in-log for Logarithmic
- The refactoring needs to:
  1. Replace `EnvCurve` enum fields with `float curveAmount_` fields [-1, +1]
  2. Add `std::array<float, 256> attackTable_`, `decayTable_`, `releaseTable_`
  3. Change `setAttackCurve(EnvCurve)` to `setAttackCurve(float curveAmount)` (overload or replace)
  4. Regenerate lookup table on curve change
  5. Change per-sample processing to use table lookup instead of one-pole coefficients
- Impact on existing code: `EnvCurve` enum is used in `RuinaeVoice` and other places. Need to check all references.

**Decision**:
- Add new `setAttackCurve(float)` overloads alongside keeping the enum versions for backward compatibility initially.
- Add 256-entry lookup tables per segment.
- The table-based processing replaces one-pole coefficients for shaped segments.
- The `EnvCurve` enum overloads internally convert to curve amounts: Exp->+0.7, Linear->0.0, Log->-0.7.

**Rationale**: Maintaining backward compatibility with enum API allows incremental migration. The table approach is simpler and more flexible than one-pole coefficients.

---

## R-009: Coordinate Conversion System

**Context**: Need pixel-to-parameter and parameter-to-pixel conversion with logarithmic time axis, sustain hold segment, and Y-axis level mapping.

**Research Findings**:
- The display has a fixed layout: Attack | Decay | Sustain-Hold | Release
- Sustain-hold is fixed 25% of width
- The remaining 75% is allocated to Attack, Decay, Release via log scaling with 15% min
- Y axis: 0.0 at bottom, 1.0 at top (inverted from pixel coordinates)
- Time-to-pixel requires: compute segment widths, then position within segment is linear

**Decision**: Implement a `SegmentLayout` struct computed once per parameter change:
```
struct SegmentLayout {
    float attackStartX, attackEndX;    // pixel X bounds
    float decayStartX, decayEndX;
    float sustainStartX, sustainEndX;  // hold segment
    float releaseStartX, releaseEndX;
};
```
Conversion functions:
- `timeMsToPixelX(segment, timeMs)` -- for positioning within a segment
- `pixelXToTimeMs(pixelX)` -- identifies segment and converts back
- `levelToPixelY(level)` -- simple linear (inverted)
- `pixelYToLevel(pixelY)` -- inverse

**Rationale**: Pre-computing layout bounds avoids repeated logarithmic calculations during drawing and hit testing.
