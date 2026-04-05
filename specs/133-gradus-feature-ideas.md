# 133 — Gradus Feature Ideas & Circular UI Design

Brainstormed enhancements for the Gradus standalone step arpeggiator, plus a radial UI redesign concept.

## Implementation Status

**v1.5 (implemented):** 9 of 14 features below are implemented, plus the full circular ring UI from Part 2. See individual feature sections for status tags. Remaining features are deferred to v1.6 or later.

| Status Legend | Meaning |
|---|---|
| ✅ **Shipped in 1.5** | Feature is fully implemented, UI wired, and included in factory presets |
| ⏳ **Deferred** | Planned for v1.6 or later |

---

## Part 1: New Features

### High-Impact Additions

#### 1. Pattern Morphing / Scene System ⏳ **Deferred**
Store 2+ snapshots of all lane data and morph between them with a single crossfader knob. Numeric values (velocity, gate, pitch, ratchet) interpolate smoothly; discrete values (conditions, modifiers, chords) switch at a probability threshold.

- Scene A / Scene B storage
- Morph amount (0–100%)
- Optional: morph automation via LFO or step lane

#### 2. Per-Step Probability per Lane ⏳ **Deferred**
Independent per-lane probability overlay (0–100% per step), separate from the Condition lane. E.g., "this ratchet fires 50% of the time, but the pitch offset always applies."

#### 3. Per-Lane Swing ✅ **Shipped in 1.5**
Independent swing per lane (0–75%). Pairs naturally with existing per-lane speed multipliers. Creates polyrhythmic groove interactions.

**Implementation:** 8 per-lane swing parameters (kArpVelocity/Gate/Pitch/Modifier/Ratchet/Condition/Chord/InversionLaneSwingId, IDs 3391–3398). Each lane's advance threshold alternates between `1+swing` and `1-swing` ticks in `advanceLaneBySpeed()`. UI: contextual swing knob per-lane tab in the detail strip.

#### 4. Note Range / Register Mapping ✅ **Shipped in 1.5**
Configurable floor and ceiling pitch. When arp + octave + pitch lane pushes a note outside bounds, it wraps or folds back.

- Range Low (MIDI note, default C1)
- Range High (MIDI note, default C6)
- Out-of-range mode: Wrap / Clamp / Skip

**Implementation:** `kArpRangeLowId` (3410), `kArpRangeHighId` (3411), `kArpRangeModeId` (3412). Applied in `arpeggiator_core.h` fireStep after transpose (skipped for pinned steps). Wrap uses `((note - lo) % span + span) % span` for bidirectional folding. Skip mode compacts `result.count` and velocities. UI: contextual on the Pitch lane tab (Lo knob, Hi knob, Mode dropdown).

#### 5. Step Pinning (Absolute Note) ✅ **Shipped in 1.5 / completed in 1.6**
Pin specific steps to absolute MIDI notes instead of following the arp pattern. Creates pedal tones, drone notes, anchor points.

- One spare bit in Modifier bitmask (`kStepPinned = 0x10`)
- Plus a Pin Note lane or per-step note value

**Implementation:** Global `kArpPinNoteId` (3413) + 32 per-step `kArpPinFlagStep0..31Id` (3414–3445). When the pitch lane's current step has its flag set, the output note is overridden to `pinNote_`, chord expansion is collapsed to a single note, and pitch offset/transpose/range mapping are all bypassed. **v1.5 UI:** Pin Note knob contextual on the Pitch lane tab; per-step pin flags initially exposed as automation-only parameters. **v1.6 UI (completion):** inline 32-cell `PinFlagStrip` custom `CControl` placed inside `DetailStrip` (reserved per-lane row, y=[24,38]), visible only when the Pitch lane is active. Click a cell to toggle the corresponding `kArpPinFlagStepNId` via `beginEdit`/`performEdit`/`endEdit`; host-side changes are mirrored back to the strip via `Controller::setParamNormalized`. Per-step pin *notes* (vs. the single global pin note) remain deferred on YAGNI grounds — the cell layout has room for a future per-cell dropdown affordance if usage justifies it.

### Medium-Impact Tweaks

#### 7. Velocity Curve / Response Shaping ✅ **Shipped in 1.5**
Global velocity curve applied after the velocity lane.

- Curve type: Linear / Exponential / Logarithmic / S-Curve
- Curve amount (0–100%)

**Implementation:** `kArpVelocityCurveTypeId` (3399) + `kArpVelocityCurveAmountId` (3400). Applied after velocity lane scaling, before accent capture, with blend formula `blended = normalized + (curved - normalized) * amount`. Exp = x², Log = √x, S-Curve = smoothstep `x²(3−2x)`. UI: contextual on the Velocity lane tab (Curve amount knob + Type dropdown).

#### 8. Pattern Length Randomization ✅ **Shipped in 1.5** (as per-lane)
"Length jitter" — pattern occasionally plays 1 step shorter or longer before cycling. Per-lane or global.

- Length jitter amount (0–4 steps)

**Implementation:** 8 per-lane jitter parameters (`kArpVelocity/Gate/Pitch/Modifier/Ratchet/Condition/Chord/InversionLaneJitterId`, IDs 3402–3409). On each lane wrap (detected via `newStep < prevStep`), re-rolls a random offset in `[-jitter, +jitter]` using a shared xorshift RNG. Positive values trigger extra advances immediately (shortens that cycle); negative values schedule pending skips (lengthens next cycle). UI: contextual jitter knob per-lane tab in the detail strip.

#### 9. Ratchet Velocity Decay ✅ **Shipped in 1.5**
Subdivisions decay in velocity like a bouncing ball. `velocity * decay^n` per subdivision.

- Ratchet decay (0–100%)

**Implementation:** `kArpRatchetDecayId` (3388). In `fireSubStep()`, velocity is multiplied by `(1 - decay)^subStepIndex` before emission. UI: contextual on the Ratchet lane tab (Decay knob).

#### 10. Transpose Lock to Scale ✅ **Shipped in 1.5**
Transpose amount quantized through the selected scale. +2 in C major = C→D→E, not C→D→D#.

**Implementation:** `kArpTransposeId` (3401). In chromatic mode the value is semitones; in scale mode it's fed to `scaleHarmonizer_.calculate()` as scale degrees so the result always stays in key. Range: −24 to +24 steps. UI: contextual on the Pitch lane tab (Transpose knob).

#### 11. Step Skip / Mute Groups ⏳ **Deferred**
Quick-toggle groups of steps on/off. Predefined groups (odds, evens, halves) plus custom.

### Creative / Experimental

#### 12. Gravity / Attraction Mode ✅ **Shipped in 1.5**
New arp mode: notes sorted by proximity to the last played note. Creates smooth stepwise motion through held chords.

**Implementation:** Added `ArpMode::Gravity = 10` (11th entry in the arp mode enum) in `held_note_buffer.h`. `NoteSelector::advanceGravity()` tracks `lastGravityNote_`, iterates held notes, picks the one with minimum pitch distance from the last. First advance picks the lowest held note. Reset clears `lastGravityNote_` to -1.

#### 13. Markov Chain Mode ✅ **Shipped in 1.7**
Extend "Walk" with transition probability matrices between scale degrees. Ship with preset matrices (Jazz, Minimal, Ambient).

**Implementation:** Added `ArpMode::Markov = 11` (12th entry in the arp mode enum) in `held_note_buffer.h`. `NoteSelector::advanceMarkov()` uses a 7×7 row-major transition matrix indexed by scale degree: at advance time it maps each held note to its degree via `ScaleHarmonizer::noteToScaleDegree` (falling back to held-note indexing in Chromatic mode), samples the next degree from the matrix row (normalized on-the-fly so user-edited cells don't need manual balancing), then picks the held note matching that degree — or the nearest degree with a held note if the target isn't held. Edge cases: single-note and empty buffers, degenerate all-zero rows (falls back to uniform), and state reset on `setMode` all covered by unit tests. 5 hardcoded preset matrices in a new `dsp/include/krate/dsp/processors/markov_matrices.h` (Uniform / Jazz / Minimal / Ambient / Classical) plus a Custom sentinel. Plugin exposes `kArpMarkovPresetId` (3446) + 49 `kArpMarkovCell{row}{col}Id` params (3447–3495). Controller batch-writes cells when a preset is picked (guarded against state-recall echo) and flips the preset dropdown to "Custom" when any cell is edited. UI: 160×160 `MarkovMatrixEditor` custom CView (humble-object pattern: pure logic in `markov_matrix_editor_logic.h`) with 7×7 click-drag grid and I/ii/iii/IV/V/vi/vii° labels, visible only when Markov mode is selected.

#### 14. Euclidean per Lane ⏳ **Deferred**
Each lane gets its own Euclidean pattern overlay instead of one global generator. 4 params per lane (enable, hits, steps, rotation).

#### 15. Strum Mode for Chords ✅ **Shipped in 1.5**
Spread chord notes slightly in time.

- Strum time (0–100ms)
- Strum direction: Up / Down / Random / Alternate

**Implementation:** `kArpStrumTimeId` (3389) + `kArpStrumDirectionId` (3390). `prepareStrumOffsets(noteCount)` precomputes per-note sample offsets into a `strumOffsets_[32]` array once per chord, ensuring consistent direction for Random/Alternate across the chord's notes. Applied at all 4 chord emission sites (main fireStep, slide, chord-mode, ratchet sub-step chords) plus matching note-off scheduling so each strummed note gets the same effective gate length. UI: contextual on the Chord/Inversion lane tabs (Strum Time knob + Direction dropdown).

### Priority Ranking

| Priority | Feature | Effort | Impact | Status |
|----------|---------|--------|--------|--------|
| 1 | Ratchet Velocity Decay | Low | High | ✅ Shipped in 1.5 |
| 2 | Strum Mode | Low | High | ✅ Shipped in 1.5 |
| 3 | Per-Lane Swing | Low | High | ✅ Shipped in 1.5 |
| 4 | Step Pinning | Medium | High | ✅ Shipped in 1.5 / pin-grid UI completed in 1.6 |
| 5 | Velocity Curve | Low | Medium | ✅ Shipped in 1.5 |
| 6 | Transpose Lock to Scale | Low | Medium | ✅ Shipped in 1.5 |
| 7 | Gravity Mode | Medium | Medium | ✅ Shipped in 1.5 |
| 8 | Note Range Mapping | Medium | Medium | ✅ Shipped in 1.5 |
| 9 | Pattern Length Jitter | Low | Medium | ✅ Shipped in 1.5 (per-lane, not global) |
| 10 | Per-Lane Probability | High | High | ⏳ Deferred |
| 11 | Per-Lane Euclidean | High | Medium | ⏳ Deferred |
| 12 | Pattern Morphing | High | Very High | ⏳ Deferred |
| 13 | Markov Chain Mode | Medium | Medium | ✅ Shipped in 1.7 |
| 14 | Step Mute Groups | Medium | Medium | ⏳ Deferred |

**9 of 14 features shipped in v1.5**, v1.6 completed Step Pinning with the inline 32-cell `PinFlagStrip`, and v1.7 adds the Markov Chain arp mode with 5 preset matrices and an editable 7×7 matrix UI. Remaining 4 features are deferred to v1.8 or later.

---

## Part 2: Circular UI Redesign ✅ **Shipped in 1.5**

The full concentric ring display is implemented and is now the default (and only) UI for Gradus. Ruinae continues to use the original horizontal lane stack UI.

### Concept

Replace the horizontal stacked-lane layout with a **concentric ring display**. Steps are arranged radially around circles, with each ring representing one or more lanes. A detail strip below provides precise linear editing.

### Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Ring editing | Direct drag on ring segments | More immediate; detail strip available for precision |
| Combined ring interaction | Inner/outer click zones per segment | With hover indicator showing which lane will open |
| Window size | Larger than 900x860 | Modern monitors have room; circle needs space |
| Center content | Euclidean visualizer | Natural fit — Euclidean rhythms originate from circular math |
| Scope | Gradus-only, new UI | Old horizontal UI stays in Ruinae; may upgrade later separately |

### Ring Layout (4 rings, 8 lanes)

| Ring | Position | Lanes | Visual Encoding |
|------|----------|-------|-----------------|
| Ring 1 | Outermost | Velocity + Gate | Bar height = velocity, bar width = gate. Inner zone = gate, outer zone = velocity |
| Ring 2 | Middle-outer | Pitch | Bipolar bars (-24 to +24), centered on ring midline |
| Ring 3 | Middle-inner | Modifier + Condition | Modifier as color icons (tie/slide/accent), condition as background tint/symbol |
| Ring 4 | Innermost | Ratchet + Chord + Inversion | Ratchet as subdivisions, chord as glyph, inversion as dot position |

### Playhead System: Per-Step Colored Highlights

No separate playhead line. Instead, the **active step segment on each ring gets a colored outline/highlight**. Each lane has its own color, and the highlight races around the ring at that lane's speed.

| Lane | Highlight Color | Ring |
|------|----------------|------|
| Velocity | Cyan | Ring 1 |
| Gate | Amber/Gold | Ring 1 |
| Pitch | Green | Ring 2 |
| Modifier | Red/Coral | Ring 3 |
| Condition | Purple | Ring 3 |
| Ratchet | Orange | Ring 4 |
| Chord | Blue | Ring 4 |
| Inversion | Teal | Ring 4 |

**Behavior:**
- All lanes at 1x: unified glow sweeps all rings in sync
- Mixed speeds: colored highlights chase each other at different rates
- Step trigger: brief flash/pulse on segment when it fires
- Same-step overlap: outlines stack (inner/outer borders) or blend
- Speed divergence creates visible polymetric phasing

### Combined Ring Hover Indicator

When hovering over a combined ring (e.g., Ring 1 = Velocity + Gate), a visual indicator shows which lane the click will target:

- **Outer half of segment**: velocity color highlight appears on hover
- **Inner half of segment**: gate color highlight appears on hover
- Cursor or segment border changes to the target lane's color
- Clicking selects that lane in the detail strip below

### Overall Layout

```
┌──────────────────────────────────────────────────────┐
│  [Mode][Octave][Oct Mode][Latch][Retrig]             │
│  [Sync][Note][Rate][Gate][Swing]                     │
│  [Scale][Root][Quantize][Voicing]                    │
├──────────────────────────────────────────────────────┤
│                                                      │
│                  ╭──────────────╮                    │
│               ╭──┤   Ring 4     ├──╮                │
│            ╭──┤  ╰──────────────╯  ├──╮             │
│         ╭──┤  ╰────────────────────╯  ├──╮          │
│         │  ╰──────────Ring 1──────────╯  │          │
│         ╰────────────────────────────────╯          │
│                                                      │
│              Center: Euclidean Visualizer             │
│              (hits/steps/rotation as dot ring)        │
│                                                      │
├──[Vel][Gate][Pitch][Mod][Cond][Rat][Chd][Inv]────────┤
│  Steps: [16]   Speed: [1x]   Swing: [25%]           │
│  ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐ │
│  │▇ │▅ │▇ │▃ │▇ │▅ │▇ │▁ │▇ │▅ │▇ │▃ │▇ │▅ │▇ │▁ │ │
│  └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘ │
├──────────────────────────────────────────────────────┤
│  [Audition: On/Off] [Vol] [Wave] [Decay]   [Preset] │
└──────────────────────────────────────────────────────┘
```

### Implementation Notes

- Built as a custom `CView` subclass in VSTGUI (like Iterum's tap pattern editor)
- All drawing via `CDrawContext` vector primitives (arcs, fills, strokes)
- Hit testing via polar coordinate conversion (x,y → angle,radius → ring,step,lane)
- Ring segment geometry: `arc(centerX, centerY, ringRadius, startAngle, endAngle)`
- Highlight animation: timer-driven, update active step index per lane per clock tick
- Gradus-only; Ruinae keeps the existing horizontal lane UI
- VSTGUI cross-platform — no native APIs

### VSTGUI Feasibility

All required drawing operations are supported:
- `CDrawContext::drawArc()` / `drawEllipse()` for rings
- `CGraphicsPath` for complex arc segments with variable heights
- `CColor` with alpha for highlight blending
- `CViewContainer` for layout composition
- Timer via `CVSTGUITimer` for highlight animation
- Mouse hit testing with `CView::onMouseDown()` + polar math
