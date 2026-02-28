# Research: Arpeggiator Scale Mode

**Feature**: 084-arp-scale-mode
**Date**: 2026-02-28

## R-001: ScaleData struct design for variable-length scales

**Decision**: Use `struct ScaleData { std::array<int, 12> intervals; int degreeCount; };`

**Rationale**: The spec mandates this exact design (FR-016). The fixed-size `std::array<int, 12>` padded with zeros for unused slots avoids dynamic allocation and is `constexpr`-compatible across MSVC, Clang, and GCC at C++17. The maximum degree count is 12 (chromatic), so `std::array<int, 12>` covers all cases. Pentatonic scales use 5 entries, diatonic scales use 7, diminished scales use 8, whole tone uses 6, and chromatic uses 12. The `degreeCount` field replaces all hardcoded `7` values in `calculate()` and `buildReverseLookup()`.

**Alternatives considered**:
- `std::vector<int>`: Not `constexpr`-compatible, involves heap allocation. Rejected.
- `std::array<int, 7>` (current): Cannot represent pentatonic (5), blues (6), whole tone (6), diminished (8), or chromatic (12). Rejected -- this is the problem being solved.
- `std::span<const int>`: C++20 only, not `constexpr` constructible. Rejected.

## R-002: ScaleType enum extension strategy

**Decision**: Append 7 new values after `Chromatic = 8`: Locrian=9, MajorPentatonic=10, MinorPentatonic=11, Blues=12, WholeTone=13, DiminishedWH=14, DiminishedHW=15. Keep existing values stable.

**Rationale**: The existing enum values (Major=0 through Chromatic=8) are used in serialized presets via the Harmonizer's scale parameter. Reordering would silently corrupt existing presets. Adding new values at the end preserves backward compatibility. `kNumScaleTypes` changes from 9 to 16.

**Alternatives considered**:
- Reorder to put Chromatic at index 0: Breaks existing Harmonizer presets. Rejected.
- Separate enum for arp scales: Creates two divergent scale type systems. Rejected -- the spec explicitly requires both Arp and Harmonizer to share the same 16-scale vocabulary.

## R-003: Arp UI display order vs enum order

**Decision**: The Arp Scale Type dropdown shows Chromatic first (UI index 0), but maps to `ScaleType::Chromatic` (enum value 8) via display-layer index mapping. The Harmonizer dropdown shows scales in enum order (Major first, Chromatic at position 8).

**Rationale**: The spec's clarification session explicitly decided this. Chromatic is the default and most common mode for the arp, so showing it first is a UX optimization. The Harmonizer, where scales are chosen for musical purposes, keeps Major first. The implementation uses `createDropdownParameterWithDefault` with a custom string order and maps the UI index to the enum value in the parameter change handler.

**Alternatives considered**:
- Same order in both UIs: Chromatic buried at position 8 is poor UX for arp users who mostly want chromatic. Rejected.
- Reorder enum: Breaks Harmonizer presets. Rejected.

## R-004: kScaleIntervals table design

**Decision**: Change from `std::array<std::array<int, 7>, 8>` to `std::array<ScaleData, 16>` covering all 16 scale types (including Chromatic). Index by `ScaleType` enum value directly.

**Rationale**: Including Chromatic in the table (with 12 degrees and intervals {0,1,...,11}) simplifies `calculate()` -- the Chromatic early return can remain as an optimization, but the data structure is complete. Using 16 entries (one per ScaleType) indexed by enum value is simpler than a 15-entry table that requires offset calculation.

**Alternatives considered**:
- 15-entry table excluding Chromatic (as spec suggested): Adds complexity -- every index access needs `static_cast<int>(scale) - (scale > Chromatic ? 1 : 0)` or similar offset. Chromatic passthrough in `calculate()` is already handled. Using 16 entries with Chromatic included is cleaner.
- However, the spec says "15 non-Chromatic scales" for the table. We will use 16 entries indexed directly by enum value for simplicity, which is strictly a superset of the spec requirement. The Chromatic entry in the table is a valid optimization.

**Final decision**: Use `std::array<ScaleData, 16>` indexed by ScaleType enum value. This is simpler and avoids index translation. The Chromatic entry has `degreeCount = 12` and `intervals = {0,1,2,...,11}`.

## R-005: Parameter ID allocation

**Decision**: Use IDs 3300-3302 for the three new arp parameters.

| Parameter | ID | Type |
|---|---|---|
| kArpScaleTypeId | 3300 | StringListParameter (16 entries, custom display order) |
| kArpRootNoteId | 3301 | StringListParameter (12 entries: C through B) |
| kArpScaleQuantizeInputId | 3302 | Toggle (0/1, default 0) |

**Rationale**: The spec explicitly states "Parameter IDs in the 3300-3399 range are available (current arp IDs end at 3299)." Current `kNumParameters = 3300` must be bumped to 3303 (or higher for future headroom). The existing `kArpEndId = 3299` must also be updated.

## R-006: Scale quantize input integration point

**Decision**: Quantize incoming MIDI notes in `ArpeggiatorCore::noteOn()` before they enter the held notes buffer, using `ScaleHarmonizer::quantizeToScale()`.

**Rationale**: The spec says "incoming MIDI notes MUST be snapped to the nearest note in the selected scale before entering the arp note pool." The `noteOn()` method is where notes enter the pool (`heldNotes_.noteOn(note, velocity)`). Quantizing `note` before this call achieves the requirement. The `ScaleHarmonizer` instance will be owned by `ArpeggiatorCore` (or configured via setters, parallel to other arp settings).

**Alternatives considered**:
- Quantize in the processor before calling `arpCore_.noteOn()`: Would work but violates encapsulation -- the arp core should own its scale logic since it also needs scale info for pitch lane interpretation.
- Quantize in `fireStep()`: Wrong -- FR-009 says "before entering the arp note pool", not "at step output time."

## R-007: Pitch lane scale-degree-to-semitone conversion

**Decision**: In `ArpeggiatorCore::fireStep()`, replace the direct pitch offset addition with a call to `ScaleHarmonizer::calculate()` when a non-Chromatic scale is active.

**Rationale**: The current code at lines 1567-1573 does:
```cpp
int offsetNote = static_cast<int>(result.notes[i]) + static_cast<int>(pitchOffset);
```
With scale mode active, `pitchOffset` should be interpreted as scale degrees, not semitones. The `ScaleHarmonizer::calculate(inputMidiNote, pitchOffset)` method returns a `DiatonicInterval` with the correct semitone shift, handling octave wrapping and degree-count-aware modulo. The result is then added to the input note.

## R-008: UI dimming strategy for Chromatic mode

**Decision**: Use the existing VSTGUI `CView::setAlphaValue()` approach to visually dim the Root Note dropdown and Scale Quantize Input toggle when Scale Type is Chromatic.

**Rationale**: VSTGUI does not natively support "disabled but visible" controls. The common pattern in this codebase is to set alpha to ~0.4 and optionally ignore mouse events. The ArpLaneEditor already uses similar conditional rendering. The controller will monitor the Scale Type parameter via the `IDependent` pattern (as documented in the vst-guide skill) and update control visibility/alpha accordingly.

**Alternatives considered**:
- Native platform dimming: Forbidden by constitution (cross-platform rule).
- Hide controls entirely: Spec says "visually dimmed/disabled", not hidden.
- Sub-controller with visibility: Overly complex for 2 controls.

## R-009: Popup suffix adaptation (FR-018)

**Decision**: Modify `ArpLaneEditor::formatValueText()` to accept the current scale type as context and return "st" or "deg" suffix accordingly.

**Rationale**: Currently `formatValueText()` for pitch mode returns just the number (e.g., "+2"). The spec requires "+2 st" for Chromatic and "+2 deg" for non-Chromatic. The ArpLaneEditor needs access to the current Scale Type parameter value. This can be achieved by:
1. Adding a `setScaleType(int)` method to ArpLaneEditor.
2. Having the controller update this value when the Scale Type parameter changes.
3. `formatValueText()` reads the cached scale type to determine suffix.

This is cleaner than having the editor directly read a VST parameter, maintaining the separation between UI and parameter system.

## R-010: State save/load backward compatibility

**Decision**: Append the 3 new parameters at the end of `saveArpParams()` / `loadArpParams()`. In `loadArpParams()`, if the stream ends before reaching the new fields (old preset), use defaults: scaleType=8 (Chromatic), rootNote=0 (C), quantizeInput=false.

**Rationale**: The existing `loadArpParams()` already uses early-return-on-read-failure for backward compatibility (it returns `false` without corrupting state when the stream ends early). The new parameters are appended at the end, so old presets simply stop reading before they reach the new fields, and the defaults remain.
