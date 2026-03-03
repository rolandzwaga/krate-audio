# Data Model: Mono Mode Conditional Panel

**Date**: 2026-02-15 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Entities

This spec creates no new data entities. All parameter data is already modeled in the existing codebase. This document captures the UI-layer entity model (view containers and their relationships).

### PolyGroup (CViewContainer)

| Property | Value | Source |
|----------|-------|--------|
| **Type** | VSTGUI::CViewContainer | editor.uidesc |
| **custom-view-name** | "PolyGroup" | For controller capture in verifyView() |
| **Origin (in panel)** | 8, 36 | Same position as current Polyphony dropdown |
| **Size** | 112, 18 | Width accommodates dropdown + margins, single row height |
| **Initial visibility** | true (visible) | Default Voice Mode = Polyphonic |
| **Contains** | 1 child: Polyphony COptionMenu | Existing dropdown, moved inside container |

**Visibility rule**: Visible when kVoiceModeId < 0.5 (Polyphonic). Hidden when kVoiceModeId >= 0.5 (Mono).

### MonoGroup (CViewContainer)

| Property | Value | Source |
|----------|-------|--------|
| **Type** | VSTGUI::CViewContainer | editor.uidesc |
| **custom-view-name** | "MonoGroup" | For controller capture in verifyView() |
| **Origin (in panel)** | 8, 36 | Same position as PolyGroup (overlapping) |
| **Size** | 112, 18 | Width matches PolyGroup, single row height |
| **Initial visibility** | false (hidden) | Default Voice Mode = Polyphonic |
| **Contains** | 4 children (see below) | 4 controls in a single row |

**Visibility rule**: Visible when kVoiceModeId >= 0.5 (Mono). Hidden when kVoiceModeId < 0.5 (Polyphonic).

### MonoGroup Children

| Control | Type | Origin (local) | Size | control-tag | default-value |
|---------|------|----------------|------|-------------|---------------|
| Legato | ToggleButton | 0, 0 | 22, 18 | MonoLegato (1801) | 0 (off) |
| Priority | COptionMenu | 24, 0 | 36, 18 | MonoPriority (1800) | 0 (Last Note) |
| Portamento Time | ArcKnob | 62, 0 | 18, 18 | MonoPortamentoTime (1802) | 0 (0ms) |
| Portamento Mode | COptionMenu | 82, 0 | 30, 18 | MonoPortaMode (1803) | 0 (Always) |

### Control-Tags (uidesc registration)

| Name | Tag | Parameter ID | Parameter Type | Auto-populated values |
|------|-----|--------------|----------------|----------------------|
| MonoPriority | 1800 | kMonoPriorityId | StringListParameter | "Last Note", "Low Note", "High Note" |
| MonoLegato | 1801 | kMonoLegatoId | Parameter (binary) | 0 (off) / 1 (on) |
| MonoPortamentoTime | 1802 | kMonoPortamentoTimeId | Parameter (continuous) | 0.0 to 1.0 (maps to 0-5000ms via cubic) |
| MonoPortaMode | 1803 | kMonoPortaModeId | StringListParameter | "Always", "Legato" |

### Controller View Pointers

| Field | Type | Captured via | Cleaned in |
|-------|------|--------------|------------|
| polyGroup_ | VSTGUI::CView* | verifyView(), custom-view-name="PolyGroup" | willClose() |
| monoGroup_ | VSTGUI::CView* | verifyView(), custom-view-name="MonoGroup" | willClose() |

### State Transitions

```
Voice Mode = Polyphonic (default):
  PolyGroup: visible
  MonoGroup: hidden

Voice Mode = Mono:
  PolyGroup: hidden
  MonoGroup: visible

Transition trigger: kVoiceModeId parameter change
  - In setParamNormalized(): value < 0.5 -> Poly, value >= 0.5 -> Mono
  - In verifyView(): initial state set from current parameter value
```

### Relationships

```
Voice & Output FieldsetContainer (120x160)
├── Mode label + VoiceMode dropdown + Gear icon  (y=14)
├── PolyGroup container (y=36, 112x18)           [visible when Poly]
│   └── Polyphony COptionMenu
├── MonoGroup container (y=36, 112x18)            [visible when Mono]
│   ├── Legato ToggleButton (x=0)
│   ├── Priority COptionMenu (x=24)
│   ├── Portamento Time ArcKnob (x=62)
│   └── Portamento Mode COptionMenu (x=82)
├── Output ArcKnob (y=58)
├── Output label (y=90)
├── Width ArcKnob + label (y=104)
├── Spread ArcKnob + label (y=104)
└── Soft Limit ToggleButton (y=146)
```
