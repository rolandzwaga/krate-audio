# Contract: editor.uidesc XML Structure

**Spec**: 051-main-layout | **Date**: 2026-02-11

## Overview

This contract defines the required XML structure for `plugins/ruinae/resources/editor.uidesc`. The existing demo/prototype layout will be replaced with the structured 4-row production layout.

## Top-Level Structure

```xml
<?xml version="1.0" encoding="UTF-8"?>
<vstgui-ui-description version="1">
    <control-tags>
        <!-- All parameter bindings (see data-model.md for full list) -->
    </control-tags>

    <!-- Oscillator type-specific templates (10 per oscillator, 20 total) -->
    <template name="OscA_PolyBLEP" .../>
    <template name="OscA_Wavetable" .../>
    <!-- ... 8 more OscA templates ... -->
    <template name="OscB_PolyBLEP" .../>
    <!-- ... 9 more OscB templates ... -->

    <!-- Main editor template -->
    <template name="editor" minSize="900, 600" maxSize="900, 600" size="900, 600"
              background-color="#1A1A1E" class="CViewContainer">

        <!-- HEADER BAR -->
        <!-- ROW 1: SOUND SOURCE -->
        <!-- ROW 2: TIMBRE & DYNAMICS -->
        <!-- ROW 3: MOVEMENT & MODULATION -->
        <!-- ROW 4: EFFECTS & OUTPUT -->

    </template>
</vstgui-ui-description>
```

## Control-Tags Block

All control-tags follow the pattern: `<control-tag name="TagName" tag="N"/>` where N is the ParameterID from `plugin_ids.h`.

Required control-tags (minimum for full layout):
- Global: MasterGain(0), Polyphony(2), SoftLimit(3)
- OSC A: OscAType(100), OscATune(101), OscAFine(102), OscALevel(103), OscAPhase(104)
- OSC B: OscBType(200), OscBTune(201), OscBFine(202), OscBLevel(203), OscBPhase(204)
- Mixer: MixerMode(300), MixPosition(301), MixerTilt(302)
- Filter: FilterType(400), FilterCutoff(401), FilterResonance(402), FilterEnvAmount(403), FilterKeyTrack(404)
- Distortion: DistortionType(500), DistortionDrive(501), DistortionCharacter(502), DistortionMix(503)
- Trance Gate: 12 tags (600-611)
- Envelopes: 12 ADSR tags (700-703, 800-803, 900-903)
- LFOs: LFO1Rate(1000), LFO1Shape(1001), LFO1Depth(1002), LFO2Rate(1100), LFO2Shape(1101), LFO2Depth(1102)
- Chaos: ChaosRate(1200)
- FX: FreezeEnabled(1500), DelayType(1600), DelayTime(1601), DelayFeedback(1602), DelayMix(1603), DelaySync(1604), ReverbSize(1700), ReverbDamping(1701), ReverbWidth(1702), ReverbMix(1703), ReverbPreDelay(1704)
- UI Actions: 10 tags (10000-10009)

## Row Layout Geometry

```
Y=0:    Header bar (30px tall)
Y=32:   Row 1 - Sound Source (160px tall)
Y=194:  Row 2 - Timbre & Dynamics (138px tall)
Y=334:  Row 3 - Movement & Modulation (130px tall)
Y=466:  Row 4 - Effects & Output (80px tall)
Y=548:  Footer padding
```

All rows span x: 8 to 892 (880px usable, 10px margin each side).

## Section Geometry (within each row)

### Row 1 (y=32, height=160)

| Section | Origin (in row) | Size | Container |
|---------|-----------------|------|-----------|
| OSC A | 8, 32 | 220 x 160 | FieldsetContainer |
| Spectral Morph | 236, 32 | 250 x 160 | FieldsetContainer |
| OSC B | 494, 32 | 220 x 160 | FieldsetContainer |

### Row 2 (y=194, height=138)

| Section | Origin | Size | Container |
|---------|--------|------|-----------|
| Filter | 8, 194 | 170 x 138 | FieldsetContainer |
| Distortion | 186, 194 | 130 x 138 | FieldsetContainer |
| Envelopes | 324, 194 | 568 x 138 | FieldsetContainer |

### Row 3 (y=334, height=130)

| Section | Origin | Size | Container |
|---------|--------|------|-----------|
| Trance Gate | 8, 334 | 380 x 130 | FieldsetContainer |
| Modulation | 396, 334 | 496 x 130 | FieldsetContainer |

### Row 4 (y=466, height=80)

| Section | Origin | Size | Container |
|---------|--------|------|-----------|
| Effects | 8, 466 | 730 x 80 | FieldsetContainer |
| Master | 746, 466 | 146 x 80 | FieldsetContainer |

## UIViewSwitchContainer Contract

```xml
<!-- Inside FieldsetContainer "OSC A", below common knobs -->
<view class="UIViewSwitchContainer"
      origin="4, 90"
      size="212, 60"
      template-names="OscA_PolyBLEP,OscA_Wavetable,OscA_PhaseDist,OscA_Sync,OscA_Additive,OscA_Chaos,OscA_Particle,OscA_Formant,OscA_SpectralFreeze,OscA_Noise"
      template-switch-control="OscAType"
      transparent="true"
/>
```

Each template is a CViewContainer with type-specific ArcKnobs:
```xml
<template name="OscA_PolyBLEP" size="212, 60" class="CViewContainer" transparent="true">
    <view class="ArcKnob" origin="10, 10" size="36, 36" control-tag="OscAPW" arc-color="#64B4FF"/>
    <view class="CTextLabel" origin="2, 46" size="52, 12" title="PW" .../>
</template>
```

## ArcKnob Standard Configuration

Every ArcKnob instance uses these common attributes:
```xml
<view class="ArcKnob"
      size="36, 36"
      default-value="0.5"
      arc-color="{section accent color}"
      guide-color="#FFFFFF28"
/>
```

With a CTextLabel below:
```xml
<view class="CTextLabel"
      size="52, 12"
      font="~ NormalFontSmaller"
      font-color="#808080"
      text-alignment="center"
      transparent="true"
/>
```

## FieldsetContainer Standard Configuration

```xml
<view class="FieldsetContainer"
      fieldset-title="SECTION NAME"
      fieldset-color="{section accent color}"
      fieldset-radius="4"
      fieldset-line-width="1"
      fieldset-font-size="10"
      transparent="true"
/>
```

## FX Strip Expand/Collapse

Three CViewContainer detail panels positioned at the same origin, one per effect. Initially all hidden (background-color="#00000000", transparent). Controller shows one at a time based on chevron clicks.

```xml
<!-- Compact FX strip (always visible) -->
<view class="CViewContainer" origin="4, 14" size="722, 28">
    <!-- Freeze: toggle + mix knob + chevron -->
    <!-- Delay: toggle + mix knob + chevron -->
    <!-- Reverb: toggle + mix knob + chevron -->
</view>

<!-- Detail panels (only one shown at a time) -->
<view class="CViewContainer" origin="4, 44" size="722, 32"
      custom-view-name="FreezeDetail" transparent="true"/>
<view class="CViewContainer" origin="4, 44" size="722, 32"
      custom-view-name="DelayDetail" transparent="true"/>
<view class="CViewContainer" origin="4, 44" size="722, 32"
      custom-view-name="ReverbDetail" transparent="true"/>
```
