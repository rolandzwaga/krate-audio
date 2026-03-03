# Data Model: Ruinae Main UI Layout

**Spec**: 051-main-layout | **Date**: 2026-02-11

## Overview

This spec is primarily a layout/integration spec. No new DSP entities or data types are introduced. The "data model" for this feature is the editor.uidesc XML structure and the control-tag-to-parameter-ID binding map.

## Entities

### 1. Editor Layout Structure (editor.uidesc)

The editor.uidesc XML is the primary deliverable. It defines a hierarchical view tree:

```
editor (CViewContainer, 900x600)
  +-- HeaderBar (CViewContainer)
  |     +-- Title (CTextLabel "RUINAE")
  |     +-- PresetSelector (PresetBrowserView)
  |
  +-- Row1_SoundSource (CViewContainer)
  |     +-- OscA (FieldsetContainer "OSC A")
  |     |     +-- OscillatorTypeSelector (tag: kOscATypeId)
  |     |     +-- ArcKnob Tune / Detune / Level / Phase
  |     |     +-- UIViewSwitchContainer (10 type-specific templates)
  |     |
  |     +-- SpectralMorph (FieldsetContainer "SPECTRAL MORPH")
  |     |     +-- XYMorphPad (tags: kMixerPositionId, kMixerTiltId)
  |     |     +-- COptionMenu Mode (tag: kMixerModeId)
  |     |     +-- ArcKnob Shift
  |     |
  |     +-- OscB (FieldsetContainer "OSC B")
  |           +-- OscillatorTypeSelector (tag: kOscBTypeId)
  |           +-- ArcKnob Tune / Detune / Level / Phase
  |           +-- UIViewSwitchContainer (10 type-specific templates)
  |
  +-- Row2_TimbreDynamics (CViewContainer)
  |     +-- Filter (FieldsetContainer "FILTER")
  |     |     +-- COptionMenu Type + ArcKnob Cutoff/Res/EnvAmt/KeyTrack
  |     |     +-- ModRingIndicator overlays on Cutoff and Resonance
  |     |
  |     +-- Distortion (FieldsetContainer "DISTORTION")
  |     |     +-- COptionMenu Type + ArcKnob Drive/Character/Mix
  |     |
  |     +-- Envelopes (FieldsetContainer "ENVELOPES")
  |           +-- ENV1 (ADSRDisplay blue + 4 ArcKnobs A/D/S/R)
  |           +-- ENV2 (ADSRDisplay gold + 4 ArcKnobs A/D/S/R)
  |           +-- ENV3 (ADSRDisplay purple + 4 ArcKnobs A/D/S/R)
  |
  +-- Row3_Movement (CViewContainer)
  |     +-- TranceGate (FieldsetContainer "TRANCE GATE")
  |     |     +-- COnOffButton Enable + StepPatternEditor
  |     |     +-- Quick action buttons + Euclidean toolbar
  |     |     +-- ArcKnob Rate/Depth/Attack/Release/Phase
  |     |
  |     +-- Modulation (FieldsetContainer "MODULATION")
  |           +-- ArcKnob LFO1 Rate/Shape/Depth + LFO2 Rate/Shape/Depth + Chaos Rate
  |           +-- CategoryTabBar [Global | Voice]
  |           +-- ModMatrixGrid
  |           +-- ModHeatmap
  |
  +-- Row4_EffectsOutput (CViewContainer)
        +-- FxStrip (FieldsetContainer "EFFECTS")
        |     +-- Freeze: COnOffButton + ArcKnob Mix + Chevron
        |     +-- Delay: COnOffButton + ArcKnob Mix + Chevron
        |     +-- Reverb: COnOffButton + ArcKnob Mix + Chevron
        |     +-- Detail panels (only one visible at a time)
        |
        +-- Master (FieldsetContainer "MASTER")
              +-- ArcKnob Output + COptionMenu Polyphony + COnOffButton Soft Limit
```

### 2. Control-Tag-to-Parameter-ID Map

All control-tags in editor.uidesc must reference parameter IDs from `plugin_ids.h`.

| Control-Tag Name | Parameter ID | Tag Value | Section | Notes |
|-----------------|-------------|-----------|---------|-------|
| MasterGain | kMasterGainId | 0 | Master | |
| Polyphony | kPolyphonyId | 2 | Master |
| SoftLimit | kSoftLimitId | 3 | Master |
| OscAType | kOscATypeId | 100 | OSC A |
| OscATune | kOscATuneId | 101 | OSC A |
| OscAFine | kOscAFineId | 102 | OSC A | *(UI label: "Detune")* |
| OscALevel | kOscALevelId | 103 | OSC A |
| OscAPhase | kOscAPhaseId | 104 | OSC A |
| OscBType | kOscBTypeId | 200 | OSC B |
| OscBTune | kOscBTuneId | 201 | OSC B |
| OscBFine | kOscBFineId | 202 | OSC B | *(UI label: "Detune")* |
| OscBLevel | kOscBLevelId | 203 | OSC B |
| OscBPhase | kOscBPhaseId | 204 | OSC B |
| MixerMode | kMixerModeId | 300 | Spectral Morph |
| MixPosition | kMixerPositionId | 301 | Spectral Morph |
| MixerTilt | kMixerTiltId | 302 | Spectral Morph |
| FilterType | kFilterTypeId | 400 | Filter |
| FilterCutoff | kFilterCutoffId | 401 | Filter |
| FilterResonance | kFilterResonanceId | 402 | Filter |
| FilterEnvAmount | kFilterEnvAmountId | 403 | Filter |
| FilterKeyTrack | kFilterKeyTrackId | 404 | Filter |
| DistortionType | kDistortionTypeId | 500 | Distortion |
| DistortionDrive | kDistortionDriveId | 501 | Distortion |
| DistortionCharacter | kDistortionCharacterId | 502 | Distortion |
| DistortionMix | kDistortionMixId | 503 | Distortion |
| TranceGateEnabled | kTranceGateEnabledId | 600 | Trance Gate |
| TranceGateNumSteps | kTranceGateNumStepsId | 601 | Trance Gate |
| TranceGateRate | kTranceGateRateId | 602 | Trance Gate |
| TranceGateDepth | kTranceGateDepthId | 603 | Trance Gate |
| TranceGateAttack | kTranceGateAttackId | 604 | Trance Gate |
| TranceGateRelease | kTranceGateReleaseId | 605 | Trance Gate |
| TranceGateNoteValue | kTranceGateNoteValueId | 607 | Trance Gate |
| TranceGateEuclideanEnabled | kTranceGateEuclideanEnabledId | 608 | Trance Gate |
| TranceGateEuclideanHits | kTranceGateEuclideanHitsId | 609 | Trance Gate |
| TranceGateEuclideanRotation | kTranceGateEuclideanRotationId | 610 | Trance Gate |
| TranceGatePhaseOffset | kTranceGatePhaseOffsetId | 611 | Trance Gate |
| AmpEnvAttack | kAmpEnvAttackId | 700 | Envelopes |
| AmpEnvDecay | kAmpEnvDecayId | 701 | Envelopes |
| AmpEnvSustain | kAmpEnvSustainId | 702 | Envelopes |
| AmpEnvRelease | kAmpEnvReleaseId | 703 | Envelopes |
| FilterEnvAttack | kFilterEnvAttackId | 800 | Envelopes |
| FilterEnvDecay | kFilterEnvDecayId | 801 | Envelopes |
| FilterEnvSustain | kFilterEnvSustainId | 802 | Envelopes |
| FilterEnvRelease | kFilterEnvReleaseId | 803 | Envelopes |
| ModEnvAttack | kModEnvAttackId | 900 | Envelopes |
| ModEnvDecay | kModEnvDecayId | 901 | Envelopes |
| ModEnvSustain | kModEnvSustainId | 902 | Envelopes |
| ModEnvRelease | kModEnvReleaseId | 903 | Envelopes |
| LFO1Rate | kLFO1RateId | 1000 | Modulation |
| LFO1Shape | kLFO1ShapeId | 1001 | Modulation |
| LFO1Depth | kLFO1DepthId | 1002 | Modulation |
| LFO2Rate | kLFO2RateId | 1100 | Modulation |
| LFO2Shape | kLFO2ShapeId | 1101 | Modulation |
| LFO2Depth | kLFO2DepthId | 1102 | Modulation |
| ChaosRate | kChaosModRateId | 1200 | Modulation |
| FreezeEnabled | kFreezeEnabledId | 1500 | FX Strip |
| FreezeToggle | kFreezeToggleId | 1501 | FX Strip |
| DelayType | kDelayTypeId | 1600 | FX Strip |
| DelayTime | kDelayTimeId | 1601 | FX Strip |
| DelayFeedback | kDelayFeedbackId | 1602 | FX Strip |
| DelayMix | kDelayMixId | 1603 | FX Strip |
| DelaySync | kDelaySyncId | 1604 | FX Strip |
| ReverbSize | kReverbSizeId | 1700 | FX Strip |
| ReverbDamping | kReverbDampingId | 1701 | FX Strip |
| ReverbWidth | kReverbWidthId | 1702 | FX Strip |
| ReverbMix | kReverbMixId | 1703 | FX Strip |
| MixerShift | kMixerShiftId | 303 | Spectral Morph | *(reserved, registered in this spec)* |
| ReverbPreDelay | kReverbPreDelayId | 1704 | FX Strip |

> **Terminology note**: Control-tags in the 300 range use the "Mixer" prefix (MixerMode, MixPosition, MixerTilt, MixerShift) because those are the parameter ID names in `plugin_ids.h`. The UI section name is always "Spectral Morph". This is consistent with the project convention of `k{Section}{Parameter}Id` where the section in the parameter system is "Mixer".

### 3. UIViewSwitchContainer Template Map

Two UIViewSwitchContainer instances, one per oscillator:

**OSC A Templates** (bound to `OscAType` control-tag):
```
template-names="OscA_PolyBLEP,OscA_Wavetable,OscA_PhaseDist,OscA_Sync,OscA_Additive,OscA_Chaos,OscA_Particle,OscA_Formant,OscA_SpectralFreeze,OscA_Noise"
```

**OSC B Templates** (bound to `OscBType` control-tag):
```
template-names="OscB_PolyBLEP,OscB_Wavetable,OscB_PhaseDist,OscB_Sync,OscB_Additive,OscB_Chaos,OscB_Particle,OscB_Formant,OscB_SpectralFreeze,OscB_Noise"
```

Each template contains 0-4 ArcKnobs for type-specific parameters. Templates without type-specific params contain a single transparent CViewContainer spacer.

### 4. Section Accent Colors

| Section | Accent Color | Hex |
|---------|-------------|-----|
| OSC A | Blue | `#64B4FF` (rgb(100,180,255)) |
| OSC B | Warm Orange | `#FF8C64` (rgb(255,140,100)) |
| Spectral Morph | Gold | `#DCA850` |
| Filter | Cyan | `#4ECDC4` |
| Distortion | Red/Orange | `#E8644C` |
| ENV 1 (Amp) | Blue | `#508CC8` (rgb(80,140,200)) |
| ENV 2 (Filter) | Gold | `#DCAA3C` (rgb(220,170,60)) |
| ENV 3 (Mod) | Purple | `#A05AC8` (rgb(160,90,200)) |
| Trance Gate | Gold Accent | `#DCA83C` |
| Modulation | Green | `#5AC882` |
| Effects | Cool Gray | `#6E7078` |
| Master | White/Silver | `#C8C8CC` |

### 5. FX Detail Panel State

UI-only state (not persisted in VST parameter state):

```
enum ExpandedEffect { None, Freeze, Delay, Reverb };
ExpandedEffect currentExpanded_ = None;  // All collapsed on initial editor load
```

Controller manages this via member variable + chevron button callbacks.

### 6. Reserved Parameter ID Ranges

| Range | Section | Used IDs | Reserved For |
|-------|---------|----------|-------------|
| 0–99 | Master/Global | 0, 2, 3 | Future global params |
| 100–199 | OSC A | 100–104 | 110–199: Type-specific oscillator params (future specs) |
| 200–299 | OSC B | 200–204 | 210–299: Type-specific oscillator params (future specs) |
| 300–399 | Spectral Morph (Mixer) | 300–303 | 304–399: Future morph params |
| 400–499 | Filter | 400–404 | 405–499: Future filter params |
| 500–599 | Distortion | 500–503 | 504–599: Future distortion params |
| 600–699 | Trance Gate | 600–611 | 612–699: Future trance gate params |
| 700–799 | ENV 1 (Amp) | 700–703 | 704–799: Future amp env params (bezier curves, etc.) |
| 800–899 | ENV 2 (Filter) | 800–803 | 804–899: Future filter env params |
| 900–999 | ENV 3 (Mod) | 900–903 | 904–999: Future mod env params |
| 1000–1099 | LFO 1 | 1000–1002 | 1003–1099: Future LFO1 params |
| 1100–1199 | LFO 2 | 1100–1102 | 1103–1199: Future LFO2 params |
| 1200–1299 | Chaos | 1200 | 1201–1299: Future chaos params |
| 1500–1599 | Freeze | 1500–1501 | 1502–1599: Future freeze params |
| 1600–1699 | Delay | 1600–1604 | 1605–1699: Future delay params |
| 1700–1799 | Reverb | 1700–1704 | 1705–1799: Future reverb params |
| 10010–10012 | Action Tags | 10010–10012 | FX chevron expand/collapse (not VST params) |

## Validation Rules

1. Every ArcKnob MUST have a `control-tag` referencing a registered parameter
2. Every FieldsetContainer MUST have a `fieldset-title` attribute
3. UIViewSwitchContainer `template-names` count MUST equal kOscTypeCount (10)
4. ModRingIndicator `dest-index` MUST be in range [0, kMaxRingIndicators-1] (0-10)
5. ADSRDisplay `control-tag` MUST be set to the envelope's attack parameter ID (identifying tag)
6. No CKnob or CAnimKnob allowed -- only ArcKnob (FR-042)

## State Transitions

### UIViewSwitchContainer
```
User selects type T -> host sets kOscXTypeId to T's normalized value
  -> UIViewSwitchContainer reads template-switch-control
  -> Current template destroyed
  -> Template T created and shown
  -> verifyView() called for all new controls (re-wiring)
```

### FX Detail Panel
```
User clicks Chevron[N]:
  if (currentExpanded_ == N) -> hide detail[N], set currentExpanded_ = None
  else -> hide detail[currentExpanded_], show detail[N], set currentExpanded_ = N
```
