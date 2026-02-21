# Quickstart: Per-Step Modifiers Implementation

**Date**: 2026-02-21
**Branch**: `073-per-step-mods`

## Overview

This feature adds TB-303-inspired per-step modifier flags (Rest, Tie, Slide, Accent) to the ArpeggiatorCore as a bitmask lane, extends ArpEvent with a legato field, and wires the legato path through the engine for portamento behavior.

## Implementation Order

### 1. DSP Layer: ArpStepFlags + ArpEvent Extension
**Files**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`
**Tests**: `dsp/tests/unit/processors/arpeggiator_core_test.cpp`

- Add `ArpStepFlags` enum (uint8_t bitmask: kStepActive=0x01, kStepTie=0x02, kStepSlide=0x04, kStepAccent=0x08)
- Add `bool legato{false}` to ArpEvent struct
- Add `modifierLane_` (ArpLane<uint8_t>) member to ArpeggiatorCore
- Add `accentVelocity_`, `slideTimeMs_`, `tieActive_` members
- Add `modifierLane()` accessors, `setAccentVelocity()`, `setSlideTime()` methods
- Extend `resetLanes()` to include `modifierLane_.reset()` and `tieActive_ = false`
- Initialize `modifierLane_.setStep(0, kStepActive)` in constructor
- Verify backward compat: default modifier lane produces Phase 4-identical output

### 2. DSP Layer: fireStep() Modifier Evaluation
**Files**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`
**Tests**: `dsp/tests/unit/processors/arpeggiator_core_test.cpp`

Rewrite fireStep() to:
1. Advance all 4 lanes (velocity, gate, pitch, modifier) at the top
2. Evaluate modifier flags with priority: Rest > Tie > Slide > Active
3. Handle Rest: no noteOn, emit noteOff for any sounding notes
4. Handle Tie: suppress noteOff AND noteOn, sustain previous notes
5. Handle Slide: suppress previous noteOff, emit legato noteOn
6. Handle Accent: add accentVelocity_ to velocity after lane scaling
7. In `result.count == 0` branch, also advance modifier lane

### 3. Plugin Layer: Parameters + State
**Files**:
- `plugins/ruinae/src/plugin_ids.h` -- add 35 parameter IDs (3140-3181)
- `plugins/ruinae/src/parameters/arpeggiator_params.h` -- extend all 6 functions (handleArpParamChange, registerArpParams, formatArpParam, saveArpParams, loadArpParams, loadArpParamsToController)
- `plugins/ruinae/src/processor/processor.cpp` -- apply modifier params to arp core (expand-write-shrink)
- `plugins/ruinae/src/controller/controller.cpp` -- verify if this file has a separate formatting dispatch; if yes, extend it to cover IDs 3140-3181 (see task T020b)
**Tests**: `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`

### 4. Engine Integration: Legato noteOn
**Files**:
- `plugins/ruinae/src/engine/ruinae_engine.h` -- extend noteOn(), add dispatchPolyLegatoNoteOn()
- `plugins/ruinae/src/engine/ruinae_voice.h` -- add portamento support
- `plugins/ruinae/src/processor/processor.cpp` -- pass evt.legato to engine
**Tests**: `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`

## Key Design Decisions

1. **Bitmask, not enum**: Allows Slide+Accent on same step (acid bassline signature)
2. **Priority chain**: Rest > Tie > Slide > Accent (unambiguous evaluation)
3. **legato flag on ArpEvent**: Engine-agnostic design, works in both Poly and Mono modes
4. **Expand-write-shrink pattern** (mandatory): call setLength(32) first (expand), write all 32 steps via setStep(), then call setLength(actual) (shrink). Matches Phase 4's velocity/gate/pitch lane pattern exactly. Without the expand step, ArpLane::setStep() silently clamps all indices to 0 when length=1, corrupting all step data.
5. **Per-voice portamento**: RuinaeVoice gains setPortamentoTime() for Poly mode slide. Note: slideTimeMs_ stored on ArpeggiatorCore is for API symmetry only -- actual portamento routing is applyParamsToArp() -> engine_.setPortamentoTime(), not via fireStep().

## Build & Test

```bash
# Build
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests

# Test DSP
build/windows-x64-release/bin/Release/dsp_tests.exe "[arp]"

# Test Plugin
build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp]"

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```
