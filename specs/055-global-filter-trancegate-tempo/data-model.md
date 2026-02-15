# Data Model: Global Filter Strip & Trance Gate Tempo Sync

**Date**: 2026-02-15
**Spec**: [spec.md](spec.md)

## Overview

This spec is purely UI-layer work. No new data entities, parameters, or state persistence changes are needed. All parameters referenced below are already fully implemented.

## Existing Parameters (No Changes)

### Global Filter Parameters (1400-1403)

| Parameter | ID | Type | Range | Default (Norm) | Registration |
|-----------|-----|------|-------|-----------------|--------------|
| GlobalFilterEnabled | 1400 | Boolean (stepCount=1) | 0/1 | 0.0 (off) | `global_filter_params.h:48` |
| GlobalFilterType | 1401 | StringListParameter | 0-3 (LP/HP/BP/Notch) | 0 (Lowpass) | `global_filter_params.h:50` |
| GlobalFilterCutoff | 1402 | Continuous | 0.0-1.0 (log: 20Hz-20kHz) | 0.574 (~1kHz) | `global_filter_params.h:54` |
| GlobalFilterResonance | 1403 | Continuous | 0.0-1.0 (linear: 0.1-30.0) | 0.020 (~0.707) | `global_filter_params.h:56` |

### Trance Gate Sync Parameter (606)

| Parameter | ID | Type | Range | Default (Norm) | Registration |
|-----------|-----|------|-------|-----------------|--------------|
| TranceGateTempoSync | 606 | Boolean (stepCount=1) | 0/1 | 1.0 (on) | `trance_gate_params.h:143` |

### Trance Gate Rate/NoteValue Parameters (602, 607)

| Parameter | ID | Type | Range | Default (Norm) | Registration |
|-----------|-----|------|-------|-----------------|--------------|
| TranceGateRate | 602 | Continuous | 0.0-1.0 (0.1-100 Hz) | 0.039 (~4Hz) | `trance_gate_params.h:135` |
| TranceGateNoteValue | 607 | StringListParameter | 0-20 (21 note values) | index 10 (1/8) | `trance_gate_params.h:145` |

## New UI Entities (uidesc Only)

### Control-Tags to Add

| Name | Tag | Parameter ID | Purpose |
|------|-----|-------------|---------|
| `GlobalFilterEnabled` | 1400 | kGlobalFilterEnabledId | On/Off toggle for global filter |
| `GlobalFilterType` | 1401 | kGlobalFilterTypeId | Filter type dropdown (LP/HP/BP/Notch) |
| `GlobalFilterCutoff` | 1402 | kGlobalFilterCutoffId | Cutoff frequency knob |
| `GlobalFilterResonance` | 1403 | kGlobalFilterResonanceId | Resonance knob |
| `TranceGateSync` | 606 | kTranceGateTempoSyncId | Tempo sync on/off toggle |

### Color to Add

| Name | RGBA | Purpose |
|------|------|---------|
| `global-filter` | `#C8649Cff` | Global Filter strip accent (rose/pink) |

### View Containers to Add (Controller Pointers)

| custom-view-name | Pointer Name | Sync Parameter | Show When |
|------------------|-------------|----------------|-----------|
| `TranceGateRateGroup` | `tranceGateRateGroup_` | kTranceGateTempoSyncId | sync < 0.5 (off) |
| `TranceGateNoteValueGroup` | `tranceGateNoteValueGroup_` | kTranceGateTempoSyncId | sync >= 0.5 (on) |

## State Persistence

No changes. All parameters are already saved/loaded:
- Global Filter: `saveGlobalFilterParams()` / `loadGlobalFilterParams()` in `global_filter_params.h:84-98`
- Trance Gate Sync: `saveTranceGateParams()` / `loadTranceGateParams()` in `trance_gate_params.h:250-338`
