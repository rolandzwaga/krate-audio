# 133 — Gradus Feature Ideas & Circular UI Design

Brainstormed enhancements for the Gradus standalone step arpeggiator, plus a radial UI redesign concept.

---

## Part 1: New Features

### High-Impact Additions

#### 1. Pattern Morphing / Scene System
Store 2+ snapshots of all lane data and morph between them with a single crossfader knob. Numeric values (velocity, gate, pitch, ratchet) interpolate smoothly; discrete values (conditions, modifiers, chords) switch at a probability threshold.

- Scene A / Scene B storage
- Morph amount (0–100%)
- Optional: morph automation via LFO or step lane

#### 2. Per-Step Probability per Lane
Independent per-lane probability overlay (0–100% per step), separate from the Condition lane. E.g., "this ratchet fires 50% of the time, but the pitch offset always applies."

#### 3. Per-Lane Swing
Independent swing per lane (0–75%). Pairs naturally with existing per-lane speed multipliers. Creates polyrhythmic groove interactions.

#### 4. Note Range / Register Mapping
Configurable floor and ceiling pitch. When arp + octave + pitch lane pushes a note outside bounds, it wraps or folds back.

- Range Low (MIDI note, default C1)
- Range High (MIDI note, default C6)
- Out-of-range mode: Wrap / Clamp / Skip

#### 5. Step Pinning (Absolute Note)
Pin specific steps to absolute MIDI notes instead of following the arp pattern. Creates pedal tones, drone notes, anchor points.

- One spare bit in Modifier bitmask (`kStepPinned = 0x10`)
- Plus a Pin Note lane or per-step note value

#### 6. Motion Recording
Record knob movements into a lane in real-time. Hold record, twist a knob, values written to steps as the arp plays. Standard on hardware sequencers, rare in software.

### Medium-Impact Tweaks

#### 7. Velocity Curve / Response Shaping
Global velocity curve applied after the velocity lane.

- Curve type: Linear / Exponential / Logarithmic / S-Curve
- Curve amount (0–100%)

#### 8. Pattern Length Randomization
"Length jitter" — pattern occasionally plays 1 step shorter or longer before cycling. Per-lane or global.

- Length jitter amount (0–4 steps)

#### 9. Ratchet Velocity Decay
Subdivisions decay in velocity like a bouncing ball. `velocity * decay^n` per subdivision.

- Ratchet decay (0–100%)

#### 10. Transpose Lock to Scale
Transpose amount quantized through the selected scale. +2 in C major = C→D→E, not C→D→D#.

#### 11. Step Skip / Mute Groups
Quick-toggle groups of steps on/off. Predefined groups (odds, evens, halves) plus custom.

### Creative / Experimental

#### 12. Gravity / Attraction Mode
New arp mode: notes sorted by proximity to the last played note. Creates smooth stepwise motion through held chords.

#### 13. Markov Chain Mode
Extend "Walk" with transition probability matrices between scale degrees. Ship with preset matrices (Jazz, Minimal, Ambient).

#### 14. Euclidean per Lane
Each lane gets its own Euclidean pattern overlay instead of one global generator. 4 params per lane (enable, hits, steps, rotation).

#### 15. Strum Mode for Chords
Spread chord notes slightly in time.

- Strum time (0–100ms)
- Strum direction: Up / Down / Random / Alternate

### Priority Ranking

| Priority | Feature | Effort | Impact |
|----------|---------|--------|--------|
| 1 | Ratchet Velocity Decay | Low | High |
| 2 | Strum Mode | Low | High |
| 3 | Per-Lane Swing | Low | High |
| 4 | Step Pinning | Medium | High |
| 5 | Velocity Curve | Low | Medium |
| 6 | Transpose Lock to Scale | Low | Medium |
| 7 | Gravity Mode | Medium | Medium |
| 8 | Note Range Mapping | Medium | Medium |
| 9 | Pattern Length Jitter | Low | Medium |
| 10 | Motion Recording | Medium | High |
| 11 | Per-Lane Probability | High | High |
| 12 | Per-Lane Euclidean | High | Medium |
| 13 | Pattern Morphing | High | Very High |
| 14 | Markov Chain Mode | Medium | Medium |
| 15 | Step Mute Groups | Medium | Medium |

---

## Part 2: Circular UI Redesign

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
