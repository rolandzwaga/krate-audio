# Disrumpo - Complete Software Development Roadmap

**Version:** 1.0
**Date:** January 2026
**Status:** Planning

**Related Documents:**
- [spec.md](spec.md) - Core requirements specification
- [plan.md](plan.md) - System architecture overview
- [tasks.md](tasks.md) - Task breakdown summary
- [ui-mockups.md](ui-mockups.md) - UI specifications
- [dsp-details.md](dsp-details.md) - DSP implementation details
- [custom-controls.md](custom-controls.md) - Custom VSTGUI control specifications

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Project Scope](#2-project-scope)
3. [Architecture Overview](#3-architecture-overview)
4. [Phase Breakdown](#4-phase-breakdown)
5. [Detailed Task Schedule](#5-detailed-task-schedule)
6. [Dependency Graph](#6-dependency-graph)
7. [Testing Strategy](#7-testing-strategy)
8. [Risk Management](#8-risk-management)
9. [Quality Gates](#9-quality-gates)
10. [Resource Requirements](#10-resource-requirements)
11. [Milestone Criteria](#11-milestone-criteria)

---

## 1. Executive Summary

### Project Overview

Disrumpo is a multiband morphing distortion VST3 plugin featuring:
- **26 distortion algorithms** across 7 categories
- **1-8 configurable frequency bands** with Linkwitz-Riley crossovers
- **Multi-point morph spaces** (2-4 nodes per band)
- **Frequency sweep distortion** with morph linking
- **12 modulation sources** including chaos attractors and pitch tracking
- **Progressive disclosure UI** with custom VSTGUI controls

### Timeline Summary

| Phase | Duration | Weeks | Deliverable |
|-------|----------|-------|-------------|
| Phase 1: Foundation | 4 weeks | 1-4 | Basic multiband distortion, Level 1 UI |
| Phase 2: Core Features | 6 weeks | 5-10 | Morph system, sweep, modulation |
| Phase 3: Advanced | 4 weeks | 11-14 | Intelligent OS, presets, polish |
| Phase 4: Release | 2 weeks | 15-16 | Testing, docs, installers |
| **Total** | **16 weeks** | | |

### Key Assumptions

1. **KrateDSP Integration**: 24 of 26 distortion types exist in KrateDSP library
2. **Existing Infrastructure**: Follows Iterum plugin architecture patterns
3. **Single Developer**: Schedule assumes one full-time developer
4. **Platform Priority**: Windows development first, then macOS/Linux validation

---

## 2. Project Scope

### In Scope

| Category | Items |
|----------|-------|
| **DSP** | 26 distortion types, crossover network, morph engine, sweep processor, oversampling, modulation system |
| **UI** | SpectrumDisplay, MorphPad, BandStrip, modulation matrix, preset browser |
| **Plugin** | VST3 format, state serialization, automation, MIDI CC |
| **Presets** | 120 factory presets across 11 categories |
| **Documentation** | User manual, quick start guide |
| **Installers** | Windows, macOS, Linux |

### Out of Scope (v1.0)

| Item | Rationale |
|------|-----------|
| VST2/AU/AAX formats | VST3 only for initial release |
| Sidechain input | Complexity; can add in v1.1 |
| Custom curve editor | Use preset linking curves only |
| Preset preview/audition | Basic browser only |
| Undo/redo (50+ levels) | Deferred to v1.1 |

### Success Criteria (from spec.md)

| Metric | Target |
|--------|--------|
| CPU usage (4 bands, 4x OS) | < 15% single core @ 44.1kHz/512 samples |
| Latency | < 10ms at highest quality |
| Morph transition | Artifact-free at all speeds |
| UI responsiveness | < 16ms frame time (60fps) |
| Preset recall | < 50ms |

---

## 3. Architecture Overview

### System Layers

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Disrumpo Plugin                                   │
├─────────────────────────────────────────────────────────────────────────┤
│  PRESENTATION LAYER (Controller + VSTGUI)                                │
│  ┌──────────────┬──────────────┬──────────────┬──────────────────────┐  │
│  │ Spectrum     │ MorphPad     │ BandStrip    │ Modulation Matrix     │  │
│  │ Display      │              │              │                       │  │
│  └──────────────┴──────────────┴──────────────┴──────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ Controller (EditControllerEx1)                                    │   │
│  │ - Parameter registration & management                             │   │
│  │ - Preset browser integration                                      │   │
│  │ - MIDI CC mapping                                                 │   │
│  └──────────────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────────┤
│  PROCESSING LAYER (Processor)                                            │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ Processor (AudioEffect)                                           │   │
│  │ ┌────────────────┬────────────────┬────────────────────────────┐ │   │
│  │ │ Input Stage    │ Crossover      │ Band Processors (1-8)      │ │   │
│  │ │ - Gain         │ Network        │ - Oversampler              │ │   │
│  │ │ - Metering     │ (LR4)          │ - MorphEngine              │ │   │
│  │ │                │                │ - DistortionAdapter        │ │   │
│  │ └────────────────┴────────────────┴────────────────────────────┘ │   │
│  │ ┌────────────────┬────────────────┬────────────────────────────┐ │   │
│  │ │ Sweep          │ Modulation     │ Output Stage               │ │   │
│  │ │ Processor      │ Engine         │ - Mix                      │ │   │
│  │ │                │                │ - Gain                     │ │   │
│  │ │                │                │ - DC Block                 │ │   │
│  │ └────────────────┴────────────────┴────────────────────────────┘ │   │
│  └──────────────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────────┤
│  DSP LAYER (KrateDSP + Plugin-Specific)                                  │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ Plugin-Specific DSP (plugins/Disrumpo/src/dsp/)                   │   │
│  │ - DistortionAdapter (unified interface)                           │   │
│  │ - MorphEngine (weight computation, interpolation)                 │   │
│  │ - CrossoverNetwork (wrapper for CrossoverLR4)                     │   │
│  │ - SweepProcessor (Gaussian intensity)                             │   │
│  │ - ModulationEngine (routing, sources)                             │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ KrateDSP Library (dsp/include/krate/dsp/)                         │   │
│  │ - Processors: SaturationProcessor, TubeStage, TapeSaturator,      │   │
│  │   FuzzProcessor, WavefolderProcessor, BitcrusherProcessor,        │   │
│  │   TemporalDistortion, RingSaturation, FeedbackDistortion,         │   │
│  │   AliasingEffect, BitwiseMangler, ChaosWaveshaper,                │   │
│  │   FormantDistortion, GranularDistortion, SpectralDistortion,      │   │
│  │   FractalDistortion, StochasticShaper, AllpassSaturator           │   │
│  │ - Primitives: Oversampler, LFO, EnvelopeFollower, PitchDetector,  │   │
│  │   SampleRateReducer, DCBlocker, CrossoverLR4, FFT                 │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

### File Structure

```
krate-audio/
├── dsp/                                    # KrateDSP (plugin-agnostic)
│   ├── include/krate/dsp/
│   │   ├── primitives/                     # Layer 1
│   │   ├── processors/                     # Layer 2
│   │   └── systems/                        # Layer 3
│   └── tests/
│
├── plugins/
│   └── Disrumpo/                           # Disrumpo plugin
│       ├── CMakeLists.txt
│       ├── src/
│       │   ├── entry.cpp
│       │   ├── plugin_ids.h
│       │   ├── version.h
│       │   ├── dsp/                        # Plugin-specific DSP
│       │   │   ├── distortion_types.h
│       │   │   ├── distortion_adapter.h/.cpp
│       │   │   ├── morph_engine.h/.cpp
│       │   │   ├── band_processor.h/.cpp
│       │   │   ├── sweep_processor.h/.cpp
│       │   │   └── modulation_engine.h/.cpp
│       │   ├── processor/
│       │   │   └── processor.h/.cpp
│       │   └── controller/
│       │       ├── controller.h/.cpp
│       │       └── views/
│       │           ├── spectrum_display.h/.cpp
│       │           ├── morph_pad.h/.cpp
│       │           └── band_strip.h/.cpp
│       ├── tests/
│       │   ├── unit/
│       │   ├── integration/
│       │   └── approval/
│       └── resources/
│           ├── editor.uidesc
│           ├── presets/
│           └── installers/
│
└── specs/Disrumpo/                         # Specifications
```

---

## 4. Phase Breakdown

### Phase 1: Foundation (Weeks 1-4)

**Objective:** Establish plugin skeleton, band management, basic distortion processing, and Level 1 UI.

#### Week 1: Project Setup & Skeleton

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T1.1 | Create CMake project structure | P0 | 4h | - |
| T1.2 | Implement parameter ID encoding system | P0 | 8h | T1.1 |
| T1.3 | Create Processor skeleton with audio bus config | P0 | 8h | T1.1 |
| T1.4 | Create Controller skeleton with parameter registration | P0 | 8h | T1.2, T1.3 |
| T1.5 | Implement state serialization (getState/setState) | P0 | 8h | T1.4 |
| T1.6 | Verify plugin loads in DAW (Reaper, Ableton) | P0 | 4h | T1.5 |

**Milestone M1:** Plugin loads in DAW and passes pluginval level 1

#### Week 2: Band Management

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T2.1 | Create CrossoverNetwork wrapper class | P0 | 8h | M1 |
| T2.2 | Integrate CrossoverLR4 from KrateDSP | P0 | 4h | T2.1 |
| T2.3 | Implement dynamic band count (1-8) | P0 | 8h | T2.2 |
| T2.4 | Implement cascaded band splitting | P0 | 6h | T2.3 |
| T2.5 | Implement per-band state structure | P0 | 4h | T2.3 |
| T2.6 | Implement band gain/pan processing | P1 | 4h | T2.5 |
| T2.7 | Implement solo/bypass/mute logic | P1 | 4h | T2.5 |
| T2.8 | Implement band summation with phase coherence | P0 | 6h | T2.4 |
| T2.9 | Verify flat frequency response (±0.1 dB) | P0 | 4h | T2.8 |

**Deliverable:** Phase-coherent multiband crossover with adjustable band count

#### Week 3: Distortion Integration

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T3.1 | Create DistortionAdapter unified interface | P0 | 8h | T2.8 |
| T3.2 | Create DistortionType enum and category mapping | P0 | 4h | T3.1 |
| T3.3 | Integrate Saturation types (D01-D06) | P0 | 8h | T3.1 |
| T3.4 | Integrate Wavefold types (D07-D09) | P0 | 4h | T3.1 |
| T3.5 | Integrate Rectify types (D10-D11) | P0 | 2h | T3.1 |
| T3.6 | Integrate Digital types (D12-D14) | P0 | 4h | T3.1 |
| T3.7 | Integrate Digital types (D18-D19) | P0 | 4h | T3.1 |
| T3.8 | Integrate Dynamic type (D15) | P0 | 2h | T3.1 |
| T3.9 | Integrate Hybrid types (D16-D17, D26) | P0 | 6h | T3.1 |
| T3.10 | Integrate Experimental types (D20-D25) | P0 | 8h | T3.1 |
| T3.11 | Implement common parameter mapping (Drive, Mix, Tone) | P0 | 4h | T3.3-T3.10 |
| T3.12 | Integrate Oversampler per band | P0 | 6h | T3.11 |
| T3.13 | Wire distortion to bands (single type per band) | P0 | 4h | T3.12 |

**Milestone M2:** Working multiband distortion with all 26 types

#### Week 4: Basic UI (Level 1)

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T4.1 | Setup VSTGUI framework and editor.uidesc | P0 | 6h | M2 |
| T4.2 | Define color scheme per spec | P0 | 2h | T4.1 |
| T4.3 | Create SpectrumDisplay placeholder (band regions only) | P0 | 8h | T4.1 |
| T4.4 | Implement crossover divider interaction | P1 | 8h | T4.3 |
| T4.5 | Create BandStrip collapsed view | P0 | 8h | T4.1 |
| T4.6 | Implement distortion type dropdown | P0 | 4h | T4.5 |
| T4.7 | Implement Drive/Mix knobs per band | P0 | 4h | T4.5 |
| T4.8 | Implement Solo/Bypass/Mute toggles | P0 | 4h | T4.5 |
| T4.9 | Create global controls section | P0 | 6h | T4.1 |
| T4.10 | Implement Input/Output/Mix knobs | P0 | 4h | T4.9 |
| T4.11 | Implement band count selector | P0 | 4h | T4.9 |
| T4.12 | Create basic preset dropdown | P1 | 4h | T4.1 |

**Milestone M3:** Level 1 UI functional - basic multiband distortion usable

---

### Phase 2: Core Features (Weeks 5-10)

**Objective:** Implement morph system, sweep processor, and modulation engine.

#### Week 5-6: Morph System

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T5.1 | Implement MorphNode data structure | P0 | 4h | M3 |
| T5.2 | Implement MorphEngine class shell | P0 | 6h | T5.1 |
| T5.3 | Implement inverse distance weight computation | P0 | 8h | T5.2 |
| T5.4 | Implement 1D Linear morph mode (A↔B↔C↔D) | P0 | 8h | T5.3 |
| T5.5 | Implement 2D Planar morph mode | P0 | 12h | T5.3 |
| T5.6 | Implement 2D Radial morph mode | P1 | 8h | T5.3 |
| T5.7 | Implement same-family parameter interpolation | P0 | 8h | T5.4-T5.5 |
| T5.8 | Implement cross-family parallel processing | P0 | 12h | T5.4-T5.5 |
| T5.9 | Implement equal-power crossfade for cross-family | P0 | 6h | T5.8 |
| T5.10 | Implement transition zone fade-in (40%-60%) | P0 | 8h | T5.8, T5.9 |
| T5.11 | Implement morph smoothing (0-500ms) | P0 | 6h | T5.7, T5.10 |
| T5.12 | Add morph position parameters (X/Y per band) | P0 | 4h | T5.5 |
| T5.13 | Add morph mode parameter per band | P0 | 2h | T5.4-T5.6 |
| T5.14 | Integrate MorphEngine into BandProcessor | P0 | 8h | T5.11-T5.13 |
| T5.15 | Unit tests: weight computation | P0 | 4h | T5.3 |
| T5.16 | Unit tests: morph interpolation | P0 | 6h | T5.7-T5.10 |
| T5.17 | Approval tests: artifact-free transitions | P0 | 8h | T5.14 |

**Milestone M4:** 2D morph system working with artifact-free transitions

#### Week 7: Morph UI

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T7.1 | Create MorphPad custom control class | P0 | 8h | M4 |
| T7.2 | Implement node rendering (circles, colors) | P0 | 4h | T7.1 |
| T7.3 | Implement cursor rendering and drag interaction | P0 | 8h | T7.1 |
| T7.4 | Implement connection line rendering | P1 | 4h | T7.2, T7.3 |
| T7.5 | Implement Shift+drag fine adjustment | P1 | 2h | T7.3 |
| T7.6 | Implement Alt+drag node repositioning | P1 | 4h | T7.2 |
| T7.7 | Implement morph mode visual switching | P0 | 4h | T7.1 |
| T7.8 | Create BandStrip expanded view | P0 | 8h | T7.1 |
| T7.9 | Implement type-specific parameter zones | P0 | 12h | T7.8 |
| T7.10 | Implement view switcher for 26 type UIs | P0 | 16h | T7.9 |
| T7.11 | Create node editor panel | P1 | 8h | T7.8 |
| T7.12 | Add morph mode selector UI | P0 | 4h | T7.7 |

**Deliverable:** Full morph UI with XY pad and type-specific controls

#### Week 8: Sweep System

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T8.1 | Create SweepProcessor class | P0 | 6h | M4 |
| T8.2 | Implement center frequency parameter | P0 | 2h | T8.1 |
| T8.3 | Implement width parameter (octaves) | P0 | 2h | T8.1 |
| T8.4 | Implement intensity parameter | P0 | 2h | T8.1 |
| T8.5 | Implement Gaussian intensity distribution | P0 | 8h | T8.1-T8.4 |
| T8.6 | Calculate per-band intensity multiplier | P0 | 6h | T8.5 |
| T8.7 | Implement sweep enable/disable | P0 | 2h | T8.1 |
| T8.8 | Implement sweep-morph linking (None) | P0 | 2h | T8.1 |
| T8.9 | Implement sweep-morph linking (Linear) | P0 | 4h | T8.6 |
| T8.10 | Implement sweep-morph linking (Inverse) | P0 | 2h | T8.9 |
| T8.11 | Implement sweep-morph linking (EaseIn/Out) | P1 | 4h | T8.9 |
| T8.12 | Implement sweep-morph linking (HoldRise) | P1 | 2h | T8.9 |
| T8.13 | Implement sweep-morph linking (Stepped) | P1 | 2h | T8.9 |
| T8.14 | Add sweep UI controls | P0 | 6h | T8.7-T8.10 |
| T8.15 | Implement SweepIndicator on SpectrumDisplay | P0 | 8h | T8.5, T4.3 |
| T8.16 | Implement audio-sync for sweep visualization | P1 | 8h | T8.15 |
| T8.17 | Unit tests: Gaussian calculation | P0 | 4h | T8.5 |
| T8.18 | Unit tests: sweep-morph curves | P0 | 4h | T8.9-T8.13 |

**Milestone M5:** Sweep with morph linking functional

#### Week 9-10: Modulation System

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T9.1 | Create ModulationEngine class | P0 | 6h | M5 |
| T9.2 | Implement ModSource enum | P0 | 2h | T9.1 |
| T9.3 | Implement ModRouting structure | P0 | 2h | T9.1 |
| T9.4 | Integrate LFO sources (2x) from KrateDSP | P0 | 6h | T9.1 |
| T9.5 | Implement LFO rate (free mode) | P0 | 2h | T9.4 |
| T9.6 | Implement LFO rate (tempo sync) | P0 | 4h | T9.4 |
| T9.7 | Implement LFO shapes (6 waveforms) | P0 | 4h | T9.4 |
| T9.8 | Implement LFO phase offset | P1 | 2h | T9.4 |
| T9.9 | Implement LFO unipolar option | P1 | 2h | T9.4 |
| T9.10 | Integrate EnvelopeFollower from KrateDSP | P0 | 4h | T9.1 |
| T9.11 | Implement Random source | P0 | 4h | T9.1 |
| T9.12 | Implement Macro parameters (4x) | P0 | 6h | T9.1 |
| T9.13 | Implement Chaos modulation source | P1 | 12h | T9.1 |
| T9.14 | Implement Sample & Hold source | P1 | 8h | T9.1 |
| T9.15 | Implement Pitch Follower source | P1 | 12h | T9.1 |
| T9.16 | Implement Transient Detector source | P1 | 10h | T9.1 |
| T9.17 | Implement modulation routing matrix | P0 | 12h | T9.2-T9.16 |
| T9.18 | Implement routing amount (-100% to +100%) | P0 | 4h | T9.17 |
| T9.19 | Implement modulation curves (Linear, Exp, S, Stepped) | P0 | 8h | T9.17 |
| T9.20 | Implement bipolar modulation handling | P0 | 4h | T9.18, T9.19 |
| T9.21 | Implement multiple sources to same destination | P0 | 4h | T9.17 |
| T9.22 | Create modulation UI panel (sources) | P0 | 12h | T9.4-T9.16 |
| T9.23 | Create modulation UI panel (routing matrix) | P0 | 12h | T9.17-T9.21 |
| T9.24 | Create modulation UI panel (macros) | P0 | 6h | T9.12 |
| T9.25 | Unit tests: LFO waveforms | P0 | 4h | T9.7 |
| T9.26 | Unit tests: modulation routing | P0 | 6h | T9.17-T9.21 |
| T9.27 | Integration test: modulation → morph | P0 | 4h | T9.17, M4 |

**Milestone M6:** Full modulation system with 12 sources

---

### Phase 3: Advanced (Weeks 11-14)

**Objective:** Intelligent oversampling, preset system, and UI polish.

#### Week 11: Intelligent Oversampling

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T11.1 | Define per-type oversampling profiles | P0 | 4h | M6 |
| T11.2 | Implement getRecommendedOversample() | P0 | 4h | T11.1 |
| T11.3 | Implement weighted OS calculation for morph | P0 | 8h | T11.2 |
| T11.4 | Implement dynamic OS switching | P0 | 8h | T11.3 |
| T11.5 | Implement smooth transitions during OS change | P0 | 8h | T11.4 |
| T11.6 | Add global OS limit parameter | P0 | 2h | T11.3 |
| T11.7 | Profile CPU performance | P0 | 8h | T11.5 |
| T11.8 | Optimize hot paths (SIMD where applicable) | P1 | 16h | T11.7 |
| T11.9 | Unit tests: OS factor calculation | P0 | 4h | T11.3 |
| T11.10 | Performance tests: CPU targets | P0 | 8h | T11.8 |

**Deliverable:** Intelligent per-band oversampling meeting CPU targets

#### Week 12: Preset System

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T12.1 | Implement VST3 preset serialization version field | P0 | 2h | M6 |
| T12.2 | Serialize all global parameters | P0 | 4h | T12.1 |
| T12.3 | Serialize all per-band parameters | P0 | 8h | T12.1 |
| T12.4 | Serialize all per-node parameters | P0 | 8h | T12.1 |
| T12.5 | Serialize all modulation routings | P0 | 6h | T12.1 |
| T12.6 | Implement version migration (future-proofing) | P0 | 6h | T12.2-T12.5 |
| T12.7 | Test preset save/load round-trip | P0 | 4h | T12.6 |
| T12.8 | Create preset browser UI (category folders) | P0 | 12h | T12.6 |
| T12.9 | Implement preset search/filter | P1 | 8h | T12.8 |
| T12.10 | Implement save dialog (name, category, overwrite) | P0 | 8h | T12.8 |
| T12.11 | Create Init presets (5) | P0 | 4h | T12.6 |
| T12.12 | Create Sweep presets (15) | P0 | 8h | T12.6 |
| T12.13 | Create Morph presets (15) | P0 | 8h | T12.6 |
| T12.14 | Create Bass presets (10) | P0 | 6h | T12.6 |
| T12.15 | Create Leads presets (10) | P0 | 6h | T12.6 |
| T12.16 | Create Pads presets (10) | P0 | 6h | T12.6 |
| T12.17 | Create Drums presets (10) | P0 | 6h | T12.6 |
| T12.18 | Create Experimental presets (15) | P1 | 8h | T12.6 |
| T12.19 | Create Chaos presets (10) | P1 | 6h | T12.6 |
| T12.20 | Create Dynamic presets (10) | P1 | 6h | T12.6 |
| T12.21 | Create Lo-Fi presets (10) | P1 | 6h | T12.6 |

**Milestone M7:** Preset system complete with 120 factory presets

#### Week 13: UI Polish & Metering

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T13.1 | Implement real FFT spectrum analyzer | P0 | 12h | M7, T4.3 |
| T13.2 | Integrate KrateDSP FFT for spectrum | P0 | 4h | T13.1 |
| T13.3 | Implement audio→UI lock-free buffer | P0 | 6h | T13.1 |
| T13.4 | Implement spectrum smoothing/peak hold | P0 | 6h | T13.1 |
| T13.5 | Implement per-band spectrum coloring | P0 | 4h | T13.1 |
| T13.6 | Implement input/output overlay toggle | P1 | 4h | T13.1 |
| T13.7 | Implement input metering (peak+RMS) | P0 | 6h | M7 |
| T13.8 | Implement output metering (peak+RMS) | P0 | 4h | T13.7 |
| T13.9 | Implement per-band level indicators | P1 | 8h | T13.7 |
| T13.10 | Implement clipping detection | P0 | 2h | T13.7, T13.8 |
| T13.11 | Implement tooltips system | P1 | 8h | M7 |
| T13.12 | Add parameter descriptions | P1 | 8h | T13.11 |
| T13.13 | Add keyboard shortcut hints | P2 | 4h | T13.11 |

**Deliverable:** Production-quality spectrum and metering

#### Week 14: Progressive Disclosure & Accessibility

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T14.1 | Implement expand/collapse panels | P0 | 8h | M7 |
| T14.2 | Implement smooth collapse animation | P1 | 6h | T14.1 |
| T14.3 | Implement band detail expansion | P0 | 4h | T14.1 |
| T14.4 | Implement modulation panel toggle | P0 | 4h | T14.1 |
| T14.5 | Add keyboard shortcuts (Tab cycle) | P1 | 4h | M7 |
| T14.6 | Add keyboard shortcuts (Space bypass) | P1 | 2h | T14.5 |
| T14.7 | Add keyboard shortcuts (Arrow fine adjust) | P1 | 4h | T14.5 |
| T14.8 | Implement window resize (800x500 min) | P0 | 6h | M7 |
| T14.9 | Implement window resize (1400x900 max) | P0 | 4h | T14.8 |
| T14.10 | Maintain aspect ratio on resize | P1 | 4h | T14.8, T14.9 |
| T14.11 | Implement high contrast mode detection | P1 | 4h | M7 |
| T14.12 | Apply high contrast color scheme | P1 | 6h | T14.11 |
| T14.13 | Implement reduced motion detection | P1 | 4h | M7 |
| T14.14 | Disable animations when reduced motion | P1 | 4h | T14.13 |
| T14.15 | MIDI CC mapping implementation | P1 | 12h | M7 |
| T14.16 | MIDI Learn UI (right-click workflow) | P1 | 8h | T14.15 |
| T14.17 | 14-bit CC support | P2 | 6h | T14.15 |

**Milestone M8:** Complete UI with progressive disclosure

---

### Phase 4: Release (Weeks 15-16)

**Objective:** Testing, documentation, and release preparation.

#### Week 15: Testing & Validation

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T15.1 | Windows build verification | P0 | 4h | M8 |
| T15.2 | macOS build verification | P0 | 8h | M8 |
| T15.3 | Linux build verification | P1 | 8h | M8 |
| T15.4 | DAW testing: Ableton Live | P0 | 4h | T15.1 |
| T15.5 | DAW testing: Bitwig Studio | P0 | 4h | T15.1 |
| T15.6 | DAW testing: Cubase | P0 | 4h | T15.1 |
| T15.7 | DAW testing: FL Studio | P0 | 4h | T15.1 |
| T15.8 | DAW testing: Reaper | P0 | 4h | T15.1 |
| T15.9 | DAW testing: Studio One | P0 | 4h | T15.1 |
| T15.10 | DAW testing: Logic Pro | P0 | 4h | T15.2 |
| T15.11 | Run pluginval strictness 5 | P0 | 2h | T15.1 |
| T15.12 | Run pluginval strictness 10 | P0 | 2h | T15.11 |
| T15.13 | Performance profiling (CPU targets) | P0 | 8h | T15.1 |
| T15.14 | Memory usage analysis | P0 | 4h | T15.1 |
| T15.15 | Latency verification | P0 | 4h | T15.1 |
| T15.16 | Bug triage (P1/P2 issues) | P0 | 16h | T15.4-T15.12 |
| T15.17 | Regression testing after fixes | P0 | 8h | T15.16 |
| T15.18 | Approval tests: all presets load correctly | P0 | 4h | T15.1 |

**Milestone M9:** Beta - all tests passing, pluginval level 10

#### Week 16: Release Preparation

| Task ID | Task | Priority | Effort | Dependencies |
|---------|------|----------|--------|--------------|
| T16.1 | Write quick start guide | P0 | 8h | M9 |
| T16.2 | Write feature documentation | P0 | 12h | M9 |
| T16.3 | Write preset creation guide | P1 | 6h | M9 |
| T16.4 | Final factory preset quality review | P0 | 8h | M9 |
| T16.5 | Preset category organization | P0 | 4h | T16.4 |
| T16.6 | Preset naming convention pass | P0 | 4h | T16.4 |
| T16.7 | Create Windows installer (Inno Setup) | P0 | 8h | M9 |
| T16.8 | Create macOS installer (PKG) | P0 | 8h | M9 |
| T16.9 | Create Linux installer (DEB/RPM) | P1 | 8h | M9 |
| T16.10 | Windows code signing (EV certificate) | P0 | 4h | T16.7 |
| T16.11 | macOS code signing (Developer ID) | P0 | 4h | T16.8 |
| T16.12 | macOS notarization | P0 | 4h | T16.11 |
| T16.13 | Final build (release configuration) | P0 | 2h | T16.10-T16.12 |
| T16.14 | Installer testing (all platforms) | P0 | 6h | T16.13 |

**Milestone M10:** Release 1.0

---

## 5. Detailed Task Schedule

### Gantt Chart (Simplified)

```
Week:  1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   16
       ├────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┼────┤
Phase 1 ████████████████
Phase 2                  ████████████████████████████████
Phase 3                                                  ████████████████████
Phase 4                                                                      ████████

Milestones:
M1 (Plugin loads)      ●
M2 (Multiband dist)         ●
M3 (Level 1 UI)                  ●
M4 (Morph system)                          ●
M5 (Sweep)                                      ●
M6 (Modulation)                                           ●
M7 (Presets)                                                        ●
M8 (Complete UI)                                                              ●
M9 (Beta)                                                                          ●
M10 (Release)                                                                           ●
```

### Critical Path

The following tasks are on the critical path (delays here delay the entire project):

1. **T1.1-T1.6** → Project setup and plugin skeleton
2. **T2.1-T2.9** → Crossover network
3. **T3.1-T3.13** → Distortion integration
4. **T5.1-T5.14** → Morph engine
5. **T9.1-T9.21** → Modulation routing
6. **T12.1-T12.7** → Preset serialization
7. **T15.1-T15.17** → Platform testing

### Parallel Work Streams

| Stream | Tasks | Resources |
|--------|-------|-----------|
| **DSP Development** | T3.*, T5.*, T8.*, T9.*, T11.* | DSP engineer |
| **UI Development** | T4.*, T7.*, T13.*, T14.* | UI developer |
| **Preset Creation** | T12.11-T12.21 | Sound designer |
| **Documentation** | T16.1-T16.3 | Technical writer |

---

## 6. Dependency Graph

### Phase 1 Dependencies

```
T1.1 (CMake)
  ├── T1.2 (Param IDs)
  │     └── T1.4 (Controller)
  ├── T1.3 (Processor)
  │     └── T1.4 (Controller)
  │           └── T1.5 (Serialization)
  │                 └── T1.6 (DAW Test) → M1
  └── T2.1 (CrossoverNetwork)
        └── T2.2 → T2.3 → T2.4 → T2.8 → T2.9 → M2 (via T3.*)
```

### Phase 2 Dependencies

```
M3 (Level 1 UI)
  └── T5.1-T5.14 (Morph System) → M4
        ├── T7.1-T7.12 (Morph UI)
        └── T8.1-T8.18 (Sweep) → M5
              └── T9.1-T9.27 (Modulation) → M6
```

### Phase 3-4 Dependencies

```
M6 (Full Modulation)
  ├── T11.1-T11.10 (Intelligent OS)
  │     └── T11.7-T11.8 (Performance Opt)
  └── T12.1-T12.21 (Presets) → M7
        ├── T13.1-T13.13 (Metering/Polish)
        └── T14.1-T14.17 (Progressive Disclosure) → M8
              └── T15.1-T15.18 (Testing) → M9
                    └── T16.1-T16.14 (Release) → M10
```

---

## 7. Testing Strategy

### Test Categories

| Category | Scope | Tools | Frequency |
|----------|-------|-------|-----------|
| Unit Tests | Individual DSP components | Catch2 | Every commit |
| Integration Tests | Full signal path | Catch2 + custom | Every PR |
| Approval Tests | Regression prevention | ApprovalTests | Weekly |
| Performance Tests | CPU/memory targets | Custom profiling | Weekly |
| DAW Compatibility | Host integration | Manual + pluginval | Per milestone |
| UI Tests | Visual/interaction | Manual | Per milestone |

### Unit Test Coverage Requirements

| Component | Coverage Target | Key Tests |
|-----------|-----------------|-----------|
| MorphEngine | 95% | Weight computation, interpolation, transitions |
| CrossoverNetwork | 90% | Flat summation, phase coherence |
| SweepProcessor | 90% | Gaussian calculation, morph linking curves |
| ModulationEngine | 85% | Routing, curves, bipolar handling |
| DistortionAdapter | 80% | All 26 types process correctly |

### Performance Test Targets

| Configuration | CPU Target | Test Method |
|---------------|------------|-------------|
| 1 band, 1x OS | < 2% | 60s sustained load |
| 4 bands, 4x OS | < 15% | 60s sustained load |
| 8 bands, 8x OS | < 40% | 60s sustained load |
| Morph transition | No spikes | Continuous morph automation |
| Preset load | < 50ms | 120 preset load cycle |

### Approval Test Catalog

| Test ID | Description | Reference Output |
|---------|-------------|------------------|
| AP-001 | Flat response through bypass | Frequency sweep |
| AP-002 | Crossover summation flatness | Pink noise |
| AP-003 | Morph artifact detection | Sine sweep during morph |
| AP-004 | All preset load/save | Each preset round-trip |
| AP-005 | DC blocking verification | DC + tone input |

### DAW Compatibility Matrix

| DAW | Windows | macOS | Linux | Priority |
|-----|---------|-------|-------|----------|
| Reaper | Required | Required | Required | P0 |
| Ableton Live | Required | Required | - | P0 |
| Cubase | Required | Required | - | P0 |
| FL Studio | Required | - | - | P0 |
| Bitwig | Required | Required | Required | P0 |
| Studio One | Required | Required | - | P1 |
| Logic Pro | - | Required | - | P0 |

---

## 8. Risk Management

### Risk Register

| ID | Risk | Probability | Impact | Mitigation | Contingency |
|----|------|-------------|--------|------------|-------------|
| R1 | Morph artifacts at high speeds | Medium | High | Extensive smoothing, automated tests | Increase transition zone, add dedicated crossfade |
| R2 | CPU overload with 8 bands | Medium | High | Profile early, optimize hot paths | Reduce max bands to 6, add quality presets |
| R3 | UI complexity overwhelms users | High | Medium | Strict progressive disclosure | Add "Simple Mode" with reduced features |
| R4 | Cross-platform issues | Medium | Medium | CI/CD multi-platform builds | Prioritize Windows, defer Linux |
| R5 | KrateDSP component incompatibility | Low | High | Test integration early | Fork component to plugin-specific DSP |
| R6 | Preset format versioning issues | Low | Medium | Version field from day 1 | Add migration scripts |
| R7 | VSTGUI event API changes | Low | Medium | Target specific VSTGUI version | Document version requirements |
| R8 | FFT performance on UI thread | Medium | Medium | Use KrateDSP FFT, profile early | Reduce spectrum update rate |

### Risk Monitoring

- **Weekly:** Review R1 (morph artifacts), R2 (CPU), R3 (UI complexity)
- **Per Milestone:** Review all risks, update probabilities
- **Phase Gates:** Stop/go decision based on critical risks

---

## 9. Quality Gates

### Milestone Quality Gates

| Milestone | Quality Criteria |
|-----------|------------------|
| M1 | Plugin loads in 2+ DAWs, pluginval level 1 passes |
| M2 | All 26 distortion types process audio, < 5% CPU @ 4 bands |
| M3 | UI renders correctly, all controls responsive |
| M4 | Morph transitions artifact-free (approval test AP-003) |
| M5 | Sweep intensity affects correct bands, linking curves accurate |
| M6 | All 12 mod sources functional, routing matrix works |
| M7 | 120 presets load/save correctly, < 50ms load time |
| M8 | Window resize works, progressive disclosure functional |
| M9 | All tests pass, pluginval level 10, all DAWs tested |
| M10 | Installers work, documentation complete, signed binaries |

### Code Review Requirements

| Change Type | Reviewer Required | Tests Required |
|-------------|-------------------|----------------|
| DSP algorithm | Senior DSP engineer | Unit + approval |
| UI control | UI lead | Visual verification |
| State serialization | Any senior | Round-trip test |
| Performance-critical | Performance lead | Benchmark |

### Definition of Done

A task is **Done** when:
1. [ ] Code compiles with zero warnings
2. [ ] Unit tests pass
3. [ ] Code review approved
4. [ ] Documentation updated (if user-facing)
5. [ ] Pluginval passes (if plugin code changed)
6. [ ] Manual testing completed (if UI changed)

---

## 10. Resource Requirements

### Development Environment

| Resource | Specification |
|----------|---------------|
| Primary Dev Machine | Windows 11, 16GB RAM, i7/Ryzen 7 |
| macOS Dev Machine | macOS 13+, Apple Silicon or Intel |
| Linux Dev Machine | Ubuntu 22.04 LTS |
| IDE | Visual Studio 2022 (Windows), Xcode 15 (macOS) |
| Source Control | Git |
| CI/CD | GitHub Actions |
| Code Signing | Windows EV cert, Apple Developer ID |

### External Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| VST3 SDK | 3.7.8+ | Plugin framework |
| VSTGUI | 4.11+ | UI framework |
| KrateDSP | (internal) | DSP building blocks |
| Catch2 | 3.x | Unit testing |
| CMake | 3.20+ | Build system |

### Documentation Deliverables

| Document | Format | Audience |
|----------|--------|----------|
| User Manual | PDF/Web | End users |
| Quick Start Guide | PDF/Web | End users |
| API Documentation | Doxygen | Developers |
| Changelog | Markdown | All |

---

## 11. Milestone Criteria

### M1: Plugin Loads in DAW

**Success Criteria:**
- [ ] Plugin appears in DAW plugin scanner
- [ ] Plugin loads without error in Reaper
- [ ] Plugin loads without error in Ableton Live
- [ ] Audio passes through (bypass mode)
- [ ] State serialization works (save/load project)
- [ ] Pluginval level 1 passes

**Deliverables:**
- Processor skeleton
- Controller skeleton
- State serialization
- Parameter system

---

### M2: Working Multiband Distortion

**Success Criteria:**
- [ ] Crossover network splits audio into 1-8 bands
- [ ] Flat frequency response when bands sum (±0.1 dB)
- [ ] All 26 distortion types process audio correctly
- [ ] Common parameters (Drive, Mix, Tone) work
- [ ] Oversampling works (1x, 2x, 4x, 8x)
- [ ] CPU < 5% @ 4 bands, 4x OS, single type

**Deliverables:**
- CrossoverNetwork class
- DistortionAdapter class
- Per-band processing chain

---

### M3: Level 1 UI Functional

**Success Criteria:**
- [ ] Spectrum display shows band regions
- [ ] Crossover dividers are draggable
- [ ] Band strips show type selector, Drive, Mix
- [ ] Solo/Bypass/Mute toggles work
- [ ] Global controls (Input, Output, Mix, Band Count) work
- [ ] Window renders at correct size (1000x600)

**Deliverables:**
- editor.uidesc
- SpectrumDisplay (placeholder)
- BandStrip (collapsed)
- Global controls

---

### M4: 2D Morph System

**Success Criteria:**
- [ ] MorphEngine computes weights correctly (unit tests)
- [ ] 1D Linear mode works (A↔B↔C↔D)
- [ ] 2D Planar mode works (XY pad)
- [ ] Same-family morphs interpolate parameters
- [ ] Cross-family morphs use parallel processing
- [ ] Transitions are artifact-free (approval test AP-003)
- [ ] Smoothing configurable (0-500ms)

**Deliverables:**
- MorphEngine class
- MorphNode data structure
- MorphPad custom control
- BandStrip expanded view

---

### M5: Sweep with Morph Link

**Success Criteria:**
- [ ] Sweep enable/disable works
- [ ] Sweep frequency/width/intensity control works
- [ ] Gaussian intensity distribution correct (unit test)
- [ ] Sweep-morph linking: None, Linear, Inverse
- [ ] Sweep indicator on spectrum display
- [ ] Bands within sweep range show increased intensity

**Deliverables:**
- SweepProcessor class
- Sweep UI controls
- SweepIndicator on SpectrumDisplay

---

### M6: Full Modulation

**Success Criteria:**
- [ ] LFO 1 & 2 work (6 waveforms, sync, phase)
- [ ] Envelope Follower works
- [ ] Random source works
- [ ] Macros (4x) work
- [ ] Chaos source works
- [ ] Sample & Hold works
- [ ] Pitch Follower works
- [ ] Transient Detector works
- [ ] Routing matrix supports 32 routes
- [ ] Modulation curves work (Linear, Exp, S, Stepped)
- [ ] Multiple sources to same destination sum correctly

**Deliverables:**
- ModulationEngine class
- All 12 modulation sources
- Modulation UI panel

---

### M7: Preset System

**Success Criteria:**
- [ ] All parameters serialize/deserialize correctly
- [ ] Preset version migration works
- [ ] 120 factory presets included
- [ ] Preset browser UI functional
- [ ] Save dialog works (name, category)
- [ ] Preset load time < 50ms

**Deliverables:**
- Complete state serialization
- Preset browser UI
- 120 factory presets

---

### M8: Complete UI

**Success Criteria:**
- [ ] Real FFT spectrum analyzer works
- [ ] Input/Output metering works
- [ ] Per-band level indicators work
- [ ] Expand/collapse panels work
- [ ] Keyboard shortcuts work
- [ ] Window resize works (800x500 to 1400x900)
- [ ] High contrast mode supported
- [ ] Reduced motion mode supported
- [ ] MIDI CC mapping works

**Deliverables:**
- Production spectrum analyzer
- Metering system
- Progressive disclosure
- Accessibility support
- MIDI CC system

---

### M9: Beta

**Success Criteria:**
- [ ] All unit tests pass
- [ ] All approval tests pass
- [ ] Pluginval level 10 passes
- [ ] All DAWs in compatibility matrix tested
- [ ] CPU targets met (< 15% @ 4 bands, 4x OS)
- [ ] Memory usage < 100MB at max config
- [ ] All P1 bugs fixed
- [ ] Windows build verified
- [ ] macOS build verified
- [ ] Linux build verified

**Deliverables:**
- Complete test coverage
- Performance verification report
- Bug triage completed

---

### M10: Release 1.0

**Success Criteria:**
- [ ] User manual complete
- [ ] Quick start guide complete
- [ ] Factory presets finalized
- [ ] Windows installer works
- [ ] macOS installer works (notarized)
- [ ] Linux installer works
- [ ] Code signing complete (all platforms)
- [ ] All known issues documented
- [ ] Release notes prepared

**Deliverables:**
- Final release build
- Signed installers (all platforms)
- Documentation package
- Release notes

---

## Appendix A: Parameter Count Summary

| Category | Parameter Count |
|----------|-----------------|
| Global | 5 |
| Sweep | 5 |
| Per-Band (×8) | 9 + (per-node ×4) |
| Per-Node | 10+ (varies by type) |
| Modulation Sources | ~40 |
| Modulation Routings | 32 × 4 = 128 |
| **Estimated Total** | **~450 parameters** |

---

## Appendix B: Distortion Type Index

| ID | Type | Category | KrateDSP Component | OS Factor |
|----|------|----------|-------------------|-----------|
| D01 | Soft Clip | Saturation | `SaturationProcessor::Tape` | 2x |
| D02 | Hard Clip | Saturation | `SaturationProcessor::Digital` | 4x |
| D03 | Tube | Saturation | `TubeStage` | 2x |
| D04 | Tape | Saturation | `TapeSaturator` | 2x |
| D05 | Fuzz | Saturation | `FuzzProcessor::Germanium` | 4x |
| D06 | Asymmetric Fuzz | Saturation | `FuzzProcessor` + bias | 4x |
| D07 | Sine Fold | Wavefold | `WavefolderProcessor::Serge` | 4x |
| D08 | Triangle Fold | Wavefold | `WavefolderProcessor::Simple` | 4x |
| D09 | Serge Fold | Wavefold | `WavefolderProcessor::Lockhart` | 4x |
| D10 | Full Rectify | Rectify | `std::abs()` | 4x |
| D11 | Half Rectify | Rectify | `std::max(0, x)` | 4x |
| D12 | Bitcrush | Digital | `BitcrusherProcessor` | 1x |
| D13 | Sample Reduce | Digital | `SampleRateReducer` | 1x |
| D14 | Quantize | Digital | `BitCrusher` | 1x |
| D15 | Temporal | Dynamic | `TemporalDistortion` | 2x |
| D16 | Ring Saturation | Hybrid | `RingSaturation` | 4x |
| D17 | Feedback | Hybrid | `FeedbackDistortion` | 2x |
| D18 | Aliasing | Digital | `AliasingEffect` | 1x |
| D19 | Bitwise Mangler | Digital | `BitwiseMangler` | 1x |
| D20 | Chaos | Experimental | `ChaosWaveshaper` | 2x |
| D21 | Formant | Experimental | `FormantDistortion` | 2x |
| D22 | Granular | Experimental | `GranularDistortion` | 2x |
| D23 | Spectral | Experimental | `SpectralDistortion` | 1x |
| D24 | Fractal | Experimental | `FractalDistortion` | 2x |
| D25 | Stochastic | Experimental | `StochasticShaper` | 2x |
| D26 | Allpass Resonant | Hybrid | `AllpassSaturator` | 4x |

---

## Appendix C: UI Component Index

| Component | Type | Custom | Location |
|-----------|------|--------|----------|
| SpectrumDisplay | CView | Yes | spectrum_display.h |
| MorphPad | CControl | Yes | morph_pad.h |
| BandStrip | CViewContainer | Partial | band_strip.h |
| SweepIndicator | (inside SpectrumDisplay) | Yes | spectrum_display.h |
| ModMatrix | CViewContainer | Partial | uidesc |
| PresetBrowser | CViewContainer | Partial | uidesc |

---

## Appendix D: Factory Preset Categories

| Category | Count | Description |
|----------|-------|-------------|
| Init | 5 | Clean starting points (1-band to 8-band) |
| Sweep | 15 | Sweep-focused effects |
| Morph | 15 | Morph animation showcases |
| Bass | 10 | Optimized for bass frequencies |
| Leads | 10 | Lead and synth processing |
| Pads | 10 | Subtle ambient processing |
| Drums | 10 | Transient-friendly settings |
| Experimental | 15 | Chaos/Spectral/Granular showcases |
| Chaos | 10 | Attractor-driven presets |
| Dynamic | 10 | Envelope-reactive presets |
| Lo-Fi | 10 | Bitcrush/Sample reduce combinations |
| **Total** | **120** | |

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-27 | Claude | Initial roadmap creation |
