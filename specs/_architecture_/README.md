# Krate Audio Architecture

Living inventory of components and APIs. Reference before writing specs to avoid duplication.

> **Constitution Principle XIII**: Every spec implementation MUST update this document.

**Last Updated**: 2026-02-27 | **Namespace**: `Krate::DSP` | **Include**: `<krate/dsp/...>`

## Repository Structure

```
dsp/                          # Shared KrateDSP library
├── include/krate/dsp/        # Public headers
│   ├── core/                 # Layer 0: Utilities
│   ├── primitives/           # Layer 1: DSP primitives
│   ├── processors/           # Layer 2: Processors
│   ├── systems/              # Layer 3: Systems
│   └── effects/              # Layer 4: Features
└── tests/                    # DSP tests

plugins/iterum/               # Iterum delay plugin
├── src/                      # Plugin source
├── tests/                    # Plugin tests
└── resources/                # UI, presets
```

## Layer Dependency Rules

```
Layer 4: User Features     ← composes layers 0-3
Layer 3: System Components ← composes layers 0-2
Layer 2: DSP Processors    ← uses layers 0-1
Layer 1: DSP Primitives    ← uses layer 0 only
Layer 0: Core Utilities    ← stdlib only, no DSP deps
```

---

## Architecture Sections

This architecture documentation is split into the following sections:

| Section | Description |
|---------|-------------|
| [Layer 0: Core Utilities](layer-0-core.md) | Mathematical utilities, constants, window functions, PRNG, tempo sync, fast math, interpolation |
| [Layer 1: DSP Primitives](layer-1-primitives.md) | DelayLine, LFO, Biquad, Oversampler, FFT, STFT, DCBlocker, Waveshaper, Wavefolder, LadderFilter |
| [Layer 2: DSP Processors](layer-2-processors.md) | EnvelopeFollower, Saturation, TubeStage, DiodeClipper, WavefolderProcessor, TapeSaturator, FuzzProcessor, BitcrusherProcessor, DynamicsProcessor, FormantPreserver, SpectralFreezeOscillator, ArpeggiatorCore |
| [Layer 3: System Components](layer-3-systems.md) | DelayEngine, FeedbackNetwork, ModulationMatrix, ModulationEngine, CharacterProcessor, TapManager, GrainCloud, AmpChannel, OscParam/setParam dispatch pattern |
| [Layer 4: User Features](layer-4-features.md) | Complete delay modes: Tape, BBD, Digital, PingPong, MultiTap, Reverse, Shimmer, Spectral, Freeze, Ducking, Granular |
| [Plugin Architecture](plugin-architecture.md) | VST3 components, parameter flow, state flow, UI components, preset browser integration registry |
| [Plugin Parameter System](plugin-parameter-system.md) | Parameter pack pattern, mod source parameter flows, denormalization mappings |
| [Plugin State Persistence](plugin-state-persistence.md) | State version history, stream format, ModSource enum migration, backward compatibility |
| [Plugin UI Patterns](plugin-ui-patterns.md) | Sync visibility switching, mod source dropdown view switching, IArpLane, ArpLaneHeader (+ transform buttons), ArpLaneEditor, ArpModifierLane, ArpConditionLane, ArpLaneContainer, EuclideanDotDisplay, PlayheadTrailState, template conventions |
| [Testing](testing.md) | Testing layers, test helpers infrastructure (artifact detection, signal metrics, golden reference) |
| [Quick Reference](quick-reference.md) | Layer inclusion rules, common include patterns, ODR prevention |

---

## Layer Inclusion Rules (Quick Reference)

| Your Code In | Can Include |
|--------------|-------------|
| Layer 0 | stdlib only |
| Layer 1 | Layer 0 |
| Layer 2 | Layers 0-1 |
| Layer 3 | Layers 0-2 |
| Layer 4 | Layers 0-3 |
| Plugin | All DSP layers |

---

## Shared UI Components

Reusable VSTGUI custom views in `plugins/shared/src/ui/`. Full API documentation in [Plugin Architecture](plugin-architecture.md) and [Plugin UI Patterns](plugin-ui-patterns.md).

| Component | Location | Purpose | Since |
|-----------|----------|---------|-------|
| StepPatternEditor | [`step_pattern_editor.h`](../../plugins/shared/src/ui/step_pattern_editor.h) | Visual step pattern editor for bar-chart sequences (Spec 046) | 0.1.0 |
| IArpLane | [`arp_lane.h`](../../plugins/shared/src/ui/arp_lane.h) | Pure virtual interface for polymorphic arpeggiator lane management (Spec 080) | 0.23.0 |
| ArpLaneHeader | [`arp_lane_header.h`](../../plugins/shared/src/ui/arp_lane_header.h) | Non-CView helper for collapsible lane headers with name/length dropdown (Spec 080) | 0.23.0 |
| ArpLaneEditor | [`arp_lane_editor.h`](../../plugins/shared/src/ui/arp_lane_editor.h) | StepPatternEditor + IArpLane subclass for arp lane editing: velocity, gate, pitch (bipolar), ratchet (discrete) (Specs 079 + 080) | 0.22.0 |
| ArpModifierLane | [`arp_modifier_lane.h`](../../plugins/shared/src/ui/arp_modifier_lane.h) | CControl + IArpLane for 4-row toggle dot grid (Rest/Tie/Slide/Accent bitmask) (Spec 080) | 0.23.0 |
| ArpConditionLane | [`arp_condition_lane.h`](../../plugins/shared/src/ui/arp_condition_lane.h) | CControl + IArpLane for per-step condition enum popup with 18 TrigCondition values (Spec 080) | 0.23.0 |
| ArpLaneContainer | [`arp_lane_container.h`](../../plugins/shared/src/ui/arp_lane_container.h) | CViewContainer with manual vertical scroll for stacked IArpLane* lanes (Specs 079 + 080) | 0.22.0 |
| ArcKnob | [`arc_knob.h`](../../plugins/shared/src/ui/arc_knob.h) | Minimal arc-style knob control | 0.1.0 |
| FieldsetContainer | [`fieldset_container.h`](../../plugins/shared/src/ui/fieldset_container.h) | Labeled container with rounded border and title | 0.1.0 |
| XYMorphPad | [`xy_morph_pad.h`](../../plugins/shared/src/ui/xy_morph_pad.h) | 2D XY pad for dual-parameter control (Spec 047) | 0.1.0 |
| ADSRDisplay | [`adsr_display.h`](../../plugins/shared/src/ui/adsr_display.h) | Interactive ADSR envelope editor with curve shaping (Spec 048) | 0.18.0 |
| ModMatrixGrid | [`mod_matrix_grid.h`](../../plugins/shared/src/ui/mod_matrix_grid.h) | Slot-based modulation route list with tabs (Spec 049) | 0.19.0 |
| ModRingIndicator | [`mod_ring_indicator.h`](../../plugins/shared/src/ui/mod_ring_indicator.h) | Colored arc overlay on destination knobs (Spec 049) | 0.19.0 |
| ModHeatmap | [`mod_heatmap.h`](../../plugins/shared/src/ui/mod_heatmap.h) | Source-by-destination routing heatmap (Spec 049) | 0.19.0 |
| BipolarSlider | [`bipolar_slider.h`](../../plugins/shared/src/ui/bipolar_slider.h) | Bipolar (-1 to +1) slider control (Spec 049) | 0.19.0 |
| OscillatorTypeSelector | [`oscillator_type_selector.h`](../../plugins/shared/src/ui/oscillator_type_selector.h) | Dropdown tile grid oscillator type chooser (Spec 050) | 0.19.0 |
| EuclideanDotDisplay | [`euclidean_dot_display.h`](../../plugins/shared/src/ui/euclidean_dot_display.h) | Circular Euclidean E(k,n) pattern visualization CView (Spec 081) | 0.24.0 |
| PlayheadTrailState | [`arp_lane.h`](../../plugins/shared/src/ui/arp_lane.h) | Helper struct for fading 4-step playhead trail in arp lanes (Spec 081) | 0.24.0 |

---

## Before Creating New Components

```bash
# Search for existing implementations (ODR prevention)
grep -r "class YourClassName" dsp/ plugins/
grep -r "struct YourStructName" dsp/ plugins/
```
