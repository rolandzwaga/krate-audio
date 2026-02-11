# Research: Ruinae Main UI Layout

**Spec**: 051-main-layout | **Date**: 2026-02-11

## Research Tasks & Findings

### R1: UIViewSwitchContainer for Oscillator Type-Specific Parameters

**Question**: How to use UIViewSwitchContainer with template-switch-control for switching oscillator type-specific parameter sections?

**Decision**: Follow the existing Iterum pattern at `plugins/iterum/resources/editor.uidesc` line 706.

**Rationale**: The project already has a battle-tested UIViewSwitchContainer implementation in Iterum that switches 10 delay mode panels via a `template-switch-control="Mode"` attribute. The exact same pattern applies here: each oscillator type gets a template with its type-specific ArcKnobs, and the container's `template-switch-control` binds to `kOscATypeId` or `kOscBTypeId`.

**Key findings from Iterum codebase**:
- UIViewSwitchContainer DESTROYS and RECREATES controls when switching templates (documented in `plugins/iterum/src/controller/controller.cpp` line 141)
- Cached control pointers become dangling after template switch
- Must use dynamic tag-based lookup, never cache pointers to controls inside the switch container
- The `template-switch-control` attribute takes the name of a control-tag, which must reference a StringListParameter (stepCount = N-1 for N templates)
- Template names are comma-separated in `template-names` attribute
- Each template is defined as a standalone `<template>` element in the uidesc XML

**Impact on implementation**:
- OSC A type-specific params use UIViewSwitchContainer with `template-switch-control="OscAType"` where OscAType maps to `kOscATypeId` (tag 100)
- OSC B type-specific params use `template-switch-control="OscBType"` mapping to `kOscBTypeId` (tag 200)
- 10 templates per oscillator (PolyBLEP, Wavetable, PhaseDistortion, Sync, Additive, Chaos, Particle, Formant, SpectralFreeze, Noise)
- Type-specific parameter IDs start at 110-199 for OSC A and 210-299 for OSC B (already reserved in plugin_ids.h)
- Templates that have no type-specific params (e.g., Noise) get an empty template

**Alternatives considered**:
- Manual visibility via sub-controller: More complex, requires IDependent pattern, error-prone pointer caching. Rejected because UIViewSwitchContainer handles this automatically.
- Single CViewContainer with all params + show/hide: Wastes space, complex visibility logic. Rejected.

### R2: FX Strip Expand/Collapse Pattern

**Question**: How to implement the expand/collapse chevron button for the FX strip where only one detail panel is visible at a time?

**Decision**: Use a sub-controller with COnOffButton chevrons that mutually exclusive toggle CViewContainer visibility via the IDependent pattern.

**Rationale**: This is a UI-only state that does not need VST parameters (which effect detail panel is expanded). Using a sub-controller avoids polluting the parameter space with UI-only state. The IDependent pattern ensures thread-safe visibility changes.

**Implementation approach**:
- Three CViewContainer detail panels (Freeze, Delay, Reverb) stacked at the same position
- Three COnOffButton chevron buttons (one per effect)
- Controller::verifyView() wires the chevrons to a collapse callback
- When chevron N is clicked: show detail panel N, hide others, update chevron states
- No VST parameter needed -- this is editor-only state
- Detail panels appear below the compact FX strip row

**Alternatives considered**:
- UIViewSwitchContainer for FX details: Overkill since the switch is not driven by a VST parameter but by UI interaction. Also, UIViewSwitchContainer destroys/recreates views on switch, which is unnecessary overhead for a simple show/hide.
- COptionMenu to select active effect: Doesn't match the spec's chevron button requirement.

### R3: ModRingIndicator Frame Rate Tiers (60fps global / 30fps voice)

**Question**: How to achieve different update frame rates for global vs voice-level modulation sources on ModRingIndicator?

**Decision**: Use two CVSTGUITimer instances: one at ~16ms (60fps) for global sources, one at ~33ms (30fps) for voice-level sources.

**Rationale**: The existing ModRingIndicator (spec 049) uses `setArcs()` which is called from `rebuildRingIndicators()` in the controller. The frame rate differentiation is achieved at the polling/update level, not inside the indicator itself.

**Implementation approach**:
- Global sources (LFO1, LFO2, Chaos) have their modulation output polled at 60fps
- Voice-level sources (envelopes, velocity, key track) are polled at 30fps
- The controller already has `playbackPollTimer_` (33ms for trance gate). A second timer for modulation ring animation would be added.
- On each timer tick, the controller reads modulation source output values from atomic pointers shared by the processor and calls `setArcs()` on each affected ModRingIndicator.
- For this spec (layout-only), the timer wiring is not yet implemented -- modulation animation is a future DSP integration task. The layout positions the ModRingIndicator overlays correctly.

**Alternatives considered**:
- Single timer at 60fps for all: Wastes CPU on voice-level updates that don't need 60fps.
- Timer inside ModRingIndicator: Against architecture -- the controller manages timers, views are passive.

### R4: Type-Specific Oscillator Parameters

**Question**: What type-specific parameters does each oscillator type need in the UIViewSwitchContainer templates?

**Decision**: Define placeholder type-specific ArcKnobs per oscillator type. Actual parameter IDs (110-199 for OSC A, 210-299 for OSC B) will be registered when those oscillator implementations are built.

**Rationale**: The spec says each oscillator section "MUST display type-specific parameters below the common parameters" (FR-008). Currently, only the 5 common parameters (Type, Tune, Fine, Level, Phase) are registered. Type-specific params are in the reserved ID ranges but not yet implemented. The layout templates should reserve space and show placeholder knobs that will be wired to real parameters in future specs.

**Type-specific parameter layout per oscillator type** (up to ~4 ArcKnobs):

| Oscillator Type | Type-Specific Parameters |
|----------------|--------------------------|
| PolyBLEP | PW (Pulse Width) |
| Wavetable | Position, Frame |
| Phase Distortion | PD Amount, Symmetry |
| Sync | Ratio, Shape |
| Additive | Harmonics, Rolloff |
| Chaos | Complexity, Feedback |
| Particle | Density, Spread |
| Formant | Vowel, Resonance |
| Spectral Freeze | Freeze Amt, Blur |
| Noise | Color, Bandwidth |

**Alternatives considered**:
- Defer type-specific templates entirely: Would violate FR-008. Even with placeholder knobs, the UIViewSwitchContainer mechanism must be wired now.
- All type-specific params in one container with visibility: More complex and loses the benefit of UIViewSwitchContainer's automatic template switching.

### R5: Layout Geometry Verification

**Question**: Do the specified section dimensions fit within the 900x600px editor window?

**Decision**: Verified -- all sections fit with the following row height allocation:

**Layout budget** (900 x 600 px total):
- Header bar: ~30px (y: 0-30)
- Row 1 (Sound Source): ~160px (y: 32-192)
- Row 2 (Timbre & Dynamics): ~140px (y: 194-334)
- Row 3 (Movement & Modulation): ~130px (y: 336-466)
- Row 4 (Effects & Output): ~80px (y: 468-548)
- Footer/padding: ~50px
- **Total used: ~548px** (within 600px)

**Width allocation for each row**:
- Row 1: OSC A (220px) + Spectral Morph (250px) + OSC B (220px) + margins = ~750px
- Row 2: Filter (170px) + Distortion (130px) + Envelopes (450px) + margins = ~790px
- Row 3: Trance Gate (380px) + Modulation (460px) + margins = ~880px
- Row 4: FX strip (700px) + Master (150px) + margins = ~890px
- All fit within 900px with 5-10px side margins

### R6: Existing Code Reuse Assessment

**Question**: Which existing components can be directly reused vs need modification?

**Decision**: All 17 shared UI view classes are ready for direct reuse. No modifications needed.

| Component | Status | Notes |
|-----------|--------|-------|
| ArcKnob | Direct reuse | Registered, works with arc-color per section |
| FieldsetContainer | Direct reuse | Registered, has title gap and color |
| BipolarSlider | Direct reuse | For mod matrix route amounts |
| ModRingIndicator | Direct reuse | Overlay on modulatable knobs |
| StepPatternEditor | Direct reuse | Already wired in controller |
| XYMorphPad | Direct reuse | Already wired in controller |
| ADSRDisplay | Direct reuse | Already wired for all 3 envelopes |
| OscillatorTypeSelector | Direct reuse | Registered with osc-identity attribute |
| ModMatrixGrid | Direct reuse | Already wired in controller |
| ModHeatmap | Direct reuse | Already wired in controller |
| CategoryTabBar | Direct reuse | For Global/Voice tabs in modulation |
| PresetBrowserView | Direct reuse | For header preset selector |
| mod_source_colors.h | Direct reuse | Canonical color map |
| mod_matrix_types.h | Direct reuse | Shared type definitions |
| color_utils.h | Direct reuse | Color manipulation utilities |

**Controller modifications needed**:
- Wire OscillatorTypeSelector instances (2x) in `verifyView()`
- Wire FX strip chevron expand/collapse logic
- Wire CategoryTabBar for mod Global/Voice tabs
- Add control-tags to editor.uidesc for all parameter bindings
- No changes to processor or DSP code

### R7: Missing Control-Tags Analysis

**Question**: Which control-tags need to be added to editor.uidesc for the full layout?

**Decision**: The current editor.uidesc has only a subset of control-tags (demo layout). The full layout needs tags for ALL parameters that bind to UI controls.

**Control-tags to add** (grouped by section):

**Header**: (none needed -- preset browser uses custom view, not control-tag)

**Row 1 (OSC A/B, Morph)**:
- OscAType (100), OscATune (101), OscAFine (102), OscALevel (103), OscAPhase (104)
- OscBType (200), OscBTune (201), OscBFine (202), OscBLevel (203), OscBPhase (204)
- MixerMode (300), MixPosition (301), MixerTilt (302) -- MixPosition and MixerTilt already exist

**Row 2 (Filter, Distortion, Envelopes)**:
- FilterType (400), FilterCutoff (401), FilterResonance (402), FilterEnvAmount (403), FilterKeyTrack (404) -- FilterCutoff already exists
- DistortionType (500), DistortionDrive (501), DistortionCharacter (502), DistortionMix (503)
- AmpEnvAttack (700), AmpEnvDecay (701), AmpEnvSustain (702), AmpEnvRelease (703) -- AmpEnvAttack already exists
- FilterEnvAttack (800), FilterEnvDecay (801), FilterEnvSustain (802), FilterEnvRelease (803) -- FilterEnvAttack already exists
- ModEnvAttack (900), ModEnvDecay (901), ModEnvSustain (902), ModEnvRelease (903) -- ModEnvAttack already exists

**Row 3 (Trance Gate, Modulation)**:
- TranceGateEnabled (600), TranceGateNumSteps (601), TranceGateRate (602), TranceGateDepth (603)
- TranceGateAttack (604), TranceGateRelease (605), TranceGateTempoSync (606), TranceGateNoteValue (607)
- TranceGateEuclideanEnabled (608), etc. -- several already exist
- LFO1Rate (1000), LFO1Shape (1001), LFO1Depth (1002)
- LFO2Rate (1100), LFO2Shape (1101), LFO2Depth (1102)
- ChaosRate (1200)

**Row 4 (FX, Master)**:
- FreezeEnabled (1500), FreezeToggle (1501)
- DelayType (1600), DelayTime (1601), DelayFeedback (1602), DelayMix (1603), DelaySync (1604)
- ReverbSize (1700), ReverbDamping (1701), ReverbWidth (1702), ReverbMix (1703), ReverbPreDelay (1704)
- MasterGain (0), Polyphony (2), SoftLimit (3) -- MasterGain already exists
