# Research: Ruinae Plugin Shell

**Feature**: 045-plugin-shell
**Date**: 2026-02-09

## R-001: Parameter Pack Pattern (Iterum Reference)

**Decision**: Follow the established Iterum parameter pack pattern exactly.

**Rationale**: The Iterum plugin (`plugins/iterum/src/parameters/`) established a well-tested pattern used across 11+ delay modes. Each parameter pack header file contains:
1. **Atomic struct** (e.g., `DigitalParams`) -- thread-safe parameter storage with default values
2. **Change handler** (`handleDigitalParamChange()`) -- inline function that denormalizes from 0.0-1.0 and stores in atomic
3. **Registration function** (`registerDigitalParams()`) -- registers all parameters in the Controller with proper types (StringListParameter for dropdowns, Parameter for continuous)
4. **Display formatter** (`formatDigitalParam()`) -- converts normalized values to human-readable strings with units
5. **Save/load functions** (`saveDigitalParams()` / `loadDigitalParams()`) -- IBStreamer serialization
6. **Controller sync template** (`loadDigitalParamsToController()`) -- template function for setComponentState() path that reads saved values and converts back to normalized

**Alternatives considered**:
- Disrumpo bit-encoded parameter IDs: More complex, uses segment+param bit fields. Ruinae uses flat ID ranges per the existing plugin_ids.h, matching Iterum's simpler approach.
- Centralized parameter registry class: Over-engineered for this use case. Inline functions in headers are simpler, compile-time checked, and zero-overhead.

## R-002: State Versioning Strategy

**Decision**: Implement versioned state with explicit stepwise migration (N -> N+1 only).

**Rationale**: The spec mandates monotonically increasing stateVersion (FR-011). Starting at version 1 for the initial release. The state format:
1. Write `int32 stateVersion` as first field
2. Write each parameter pack's save function in deterministic order (Global, OscA, OscB, Mixer, Filter, Distortion, TranceGate, AmpEnv, FilterEnv, ModEnv, LFO1, LFO2, ChaosMod, ModMatrix, GlobalFilter, Freeze, Delay, Reverb, MonoMode)
3. On load, read version first, then apply stepwise migration if needed
4. On unknown version (future), fail closed with safe defaults

**Migration pattern**:
```cpp
if (version == 1) {
    // Load v1 fields...
    // If migrating to v2, set new v2 fields to explicit defaults
    version = 2;
}
if (version == 2) {
    // Load v2 fields...
    version = 3;
}
// etc.
```

**Alternatives considered**:
- Versionless field-by-field reading: Fragile, can't detect truncation vs missing fields. Spec explicitly prohibits this.
- Jump migration (v1 -> v3): Spec requires N -> N+1 only. Each step is isolated and testable.

## R-003: Parameter Denormalization Ranges

**Decision**: Document all denormalization ranges for each parameter section, derived from the RuinaeEngine API and DSP component ranges.

### Global (0-99)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Master Gain | 0.0-1.0 | 0.0-2.0 | `setMasterGain(float)` |
| Voice Mode | 0/1 discrete | Poly(0)/Mono(1) | `setMode(VoiceMode)` |
| Polyphony | 0.0-1.0 | 1-16 int | `setPolyphony(size_t)` |
| Soft Limit | 0/1 toggle | off/on | `setSoftLimitEnabled(bool)` |

### OSC A / OSC B (100-199, 200-299)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Type | 0-9 discrete | OscType enum | `setOscAType(OscType)` / `setOscBType(OscType)` |
| Tune | 0.0-1.0 | -24 to +24 semitones | No direct setter -- handled via noteOn frequency calc |
| Fine | 0.0-1.0 | -100 to +100 cents | Same as Tune |
| Level | 0.0-1.0 | 0.0-1.0 | Stored only, applied as scaling in voice |
| Phase | 0.0-1.0 | 0.0-1.0 | `setOscAPhaseMode(PhaseMode)` |

Note: OSC Tune and Fine parameters are not directly set on the engine. They modify the frequency calculation in the Processor before dispatching noteOn. However, the RuinaeEngine's `setFrequency()` on individual voices is used for pitch offset. For the plugin shell, the tune/fine offsets are stored and applied when forwarding note frequencies. Since the engine handles voice frequency via `noteOn(note, velocity)` using MIDI note numbers, the tune/fine parameters need to be applied differently. Given the engine's note-based API, these parameters will be stored in the processor and the engine will need a `setOscATune(float semitones)` / `setOscAFine(float cents)` API, or the processor will compute the frequency offset and use a different mechanism.

**Research finding**: The RuinaeEngine's `noteOn` takes a MIDI note number, and internally calls `noteProcessor_.getFrequency(note)` to convert to Hz. The tune/fine parameters modify the perceived pitch. Since the engine does not expose per-oscillator tuning setters, we have two options:
1. Add tune/fine setters to the engine that apply pitch offsets per oscillator
2. Store the values in the processor and not apply them until UI work (Phase 8)

**Decision**: For the initial plugin shell, store OSC A/B Tune and Fine as parameters (registered, saved, loaded, display-formatted) but do NOT apply them to the engine. Mark with TODO comments. Adding setOscATune()/setOscAFine() to the engine is out of scope (spec says "Any methods required by the plugin shell but missing from the header must be added to the engine"). Since the spec says this must be done "before plugin shell completion", we will add these methods as part of this spec.

Actually, re-reading the engine more carefully: `setOscAPhaseMode` and `setOscBPhaseMode` exist. Looking at `SelectableOscillator`, it has `setFrequency()` which the engine calls per-voice. Oscillator tuning (semitone/cent offsets) would need to be applied per-voice at the voice level. The simplest approach is to add `setOscATuneSemitones(float)` and `setOscAFineCents(float)` to the engine, which forwards to all voices. But this requires modifying `ruinae_engine.h` -- which the spec explicitly allows.

**Decision (revised)**: Add minimal tune/fine/level setter methods to the RuinaeEngine header as needed. These are simple forwarders that store the offset and apply it during frequency calculation. If the engine already handles all needed parameters, just store and forward. For parameters where the engine lacks a setter, add the setter.

### Mixer (300-399)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Mode | 0/1 discrete | Crossfade(0)/SpectralMorph(1) | `setMixMode(MixMode)` |
| Position | 0.0-1.0 | 0.0-1.0 | `setMixPosition(float)` |

### Filter (400-499)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Type | 0-6 discrete | RuinaeFilterType enum | `setFilterType(RuinaeFilterType)` |
| Cutoff | 0.0-1.0 | 20-20000 Hz (exponential) | `setFilterCutoff(float)` |
| Resonance | 0.0-1.0 | 0.1-30.0 | `setFilterResonance(float)` |
| Env Amount | 0.0-1.0 | -48 to +48 semitones | `setFilterEnvAmount(float)` |
| Key Track | 0.0-1.0 | 0.0-1.0 | `setFilterKeyTrack(float)` |

Note: Filter cutoff uses exponential mapping for perceptually linear knob feel. The exact formula: `cutoffHz = 20.0f * pow(1000.0f, normalized)` maps 0→20Hz, 1→20000Hz. Verification (A5): 20 * 1000^0 = 20Hz, 20 * 1000^1 = 20000Hz ✓ CORRECT.

### Distortion (500-599)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Type | 0-5 discrete | RuinaeDistortionType enum | `setDistortionType(RuinaeDistortionType)` |
| Drive | 0.0-1.0 | 0.0-1.0 | `setDistortionDrive(float)` |
| Character | 0.0-1.0 | 0.0-1.0 | `setDistortionCharacter(float)` |
| Mix | 0.0-1.0 | 0.0-1.0 | No direct setter (distortion mix in engine is always 1.0, dry/wet control is at plugin level if needed) |

Note: The engine does not have a `setDistortionMix()`. Looking at the voice code, distortion is applied fully (no mix knob). The plugin registers the parameter for future use but won't apply it to the engine until a setter is added. Alternatively, since the spec lists "Distortion Mix" in FR-004, we need to handle this. Decision: Store the value, add a TODO for engine integration. The distortion block in the voice processes in-place; adding a dry/wet mix would require a code change in the voice.

### Trance Gate (600-699)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Enabled | 0/1 toggle | off/on | `setTranceGateEnabled(bool)` |
| NumSteps | discrete | 8/16/32 | Via `setTranceGateParams()` |
| Rate | 0.0-1.0 | 0.1-100 Hz | Via `setTranceGateParams()` |
| Depth | 0.0-1.0 | 0.0-1.0 | Via `setTranceGateParams()` |
| Attack | 0.0-1.0 | 1-20 ms | Via `setTranceGateParams()` |
| Release | 0.0-1.0 | 1-50 ms | Via `setTranceGateParams()` |
| Tempo Sync | 0/1 toggle | off/on | Via `setTranceGateParams()` |
| Note Value | discrete | NoteValue enum | Via `setTranceGateParams()` |

### Envelopes (700-999, three identical sections)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Attack | 0.0-1.0 | 0-10000 ms (exponential) | `setAmpAttack/setFilterAttack/setModAttack(float ms)` |
| Decay | 0.0-1.0 | 0-10000 ms (exponential) | `setAmpDecay/setFilterDecay/setModDecay(float ms)` |
| Sustain | 0.0-1.0 | 0.0-1.0 | `setAmpSustain/setFilterSustain/setModSustain(float)` |
| Release | 0.0-1.0 | 0-10000 ms (exponential) | `setAmpRelease/setFilterRelease/setModRelease(float ms)` |

Note: Envelope times use exponential mapping for perceptual linearity. Formula: `ms = 1.0f * pow(10000.0f, normalized)` maps 0->1ms, 1->10000ms. The spec says "0-10000ms" for envelope times.

### LFO 1/2 (1000-1199)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Rate | 0.0-1.0 | 0.01-50 Hz (exponential) | `setGlobalLFO1Rate/setGlobalLFO2Rate(float hz)` |
| Shape | 0-5 discrete | Waveform enum | `setGlobalLFO1Waveform/setGlobalLFO2Waveform(Waveform)` |
| Depth | 0.0-1.0 | 0.0-1.0 | Stored, applied via mod matrix amounts |
| Sync | 0/1 toggle | off/on | Stored for future sync implementation |

### Chaos Mod (1200-1299)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Rate | 0.0-1.0 | 0.01-10 Hz | `setChaosSpeed(float)` |
| Type | 0-1 discrete | Lorenz(0)/Rossler(1) | Stored (engine chaos type not separately settable) |
| Depth | 0.0-1.0 | 0.0-1.0 | Stored, applied via mod matrix amounts |

### Mod Matrix (1300-1399)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Slot N Source | discrete | ModSource enum (0-12) | `setGlobalModRoute(slot, source, dest, amount)` |
| Slot N Dest | discrete | RuinaeModDest enum | `setGlobalModRoute(slot, source, dest, amount)` |
| Slot N Amount | 0.0-1.0 | -1.0 to +1.0 (bipolar) | `setGlobalModRoute(slot, source, dest, amount)` |

### Global Filter (1400-1499)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Enabled | 0/1 toggle | off/on | `setGlobalFilterEnabled(bool)` |
| Type | 0-3 discrete | SVFMode (LP/HP/BP/Notch) | `setGlobalFilterType(SVFMode)` |
| Cutoff | 0.0-1.0 | 20-20000 Hz (exponential) | `setGlobalFilterCutoff(float)` |
| Resonance | 0.0-1.0 | 0.1-30.0 | `setGlobalFilterResonance(float)` |

### Freeze (1500-1599)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Enabled | 0/1 toggle | off/on | `setFreezeEnabled(bool)` |
| Freeze Toggle | 0/1 toggle | off/on | `setFreeze(bool)` |

### Delay (1600-1699)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Type | 0-4 discrete | RuinaeDelayType enum | `setDelayType(RuinaeDelayType)` |
| Time | 0.0-1.0 | 1-5000 ms | `setDelayTime(float)` |
| Feedback | 0.0-1.0 | 0.0-1.2 | `setDelayFeedback(float)` |
| Mix | 0.0-1.0 | 0.0-1.0 | `setDelayMix(float)` |
| Sync | 0/1 toggle | off/on | Stored, affects time calculation |
| Note Value | discrete | Note value index | Stored, used for tempo-synced time |

### Reverb (1700-1799)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Size | 0.0-1.0 | 0.0-1.0 | Via `setReverbParams(ReverbParams)` |
| Damping | 0.0-1.0 | 0.0-1.0 | Via `setReverbParams(ReverbParams)` |
| Width | 0.0-1.0 | 0.0-1.0 | Via `setReverbParams(ReverbParams)` |
| Mix | 0.0-1.0 | 0.0-1.0 | Via `setReverbParams(ReverbParams)` |
| Pre-Delay | 0.0-1.0 | 0-100 ms | Via `setReverbParams(ReverbParams)` |
| Diffusion | 0.0-1.0 | 0.0-1.0 | Via `setReverbParams(ReverbParams)` |
| Freeze | 0/1 toggle | off/on | Via `setReverbParams(ReverbParams)` |
| Mod Rate | 0.0-1.0 | 0.0-2.0 Hz | Via `setReverbParams(ReverbParams)` |
| Mod Depth | 0.0-1.0 | 0.0-1.0 | Via `setReverbParams(ReverbParams)` |

### Mono Mode (1800-1899)
| Parameter | Normalized Range | Real Range | Engine Setter |
|---|---|---|---|
| Priority | 0-2 discrete | MonoMode (Last/Low/High) | `setMonoPriority(MonoMode)` |
| Legato | 0/1 toggle | off/on | `setLegato(bool)` |
| Portamento Time | 0.0-1.0 | 0-5000 ms (exponential) | `setPortamentoTime(float ms)` |
| Porta Mode | 0/1 discrete | Always(0)/LegatoOnly(1) | `setPortamentoMode(PortaMode)` |

## R-004: Missing Engine API Methods

**Status: RESOLVED per code changes A1-A3**

All required engine setters have been added to RuinaeEngine:
1. `setOscATuneSemitones(float semitones)` ✓ ADDED
2. `setOscAFineCents(float cents)` ✓ ADDED
3. `setOscBTuneSemitones(float semitones)` ✓ ADDED
4. `setOscBFineCents(float cents)` ✓ ADDED
5. `setOscALevel(float level)` ✓ ADDED
6. `setOscBLevel(float level)` ✓ ADDED
7. `setDistortionMix(float mix)` ✓ ADDED

OSC tune/fine/level application strategy is fully resolved. SC-002 (all parameters produce audible changes) is now fully satisfiable with the complete engine API.

## R-005: Shared Parameter Helpers

**Decision**: Copy `parameter_helpers.h` and `note_value_ui.h` from Iterum to Ruinae with namespace change.

**Rationale**: These helpers contain generic VST3 parameter registration utilities (createDropdownParameter, createDropdownParameterWithDefault, createNoteValueDropdown) and note value UI strings. They have zero Iterum-specific logic but are in the `Iterum` namespace. Copying with namespace change is the safest approach that:
- Does not modify Iterum (avoids regression risk)
- Keeps plugins independently buildable
- Is exactly 2 files (~200 lines total)

**Alternatives considered**:
- Move to `plugins/shared/`: Ideal but requires modifying Iterum's include paths and testing. Deferred to a future cleanup spec.
- Template both under a shared namespace: Over-engineering for 3 small functions.

## R-006: VST3 Best Practices for Synth Plugins

**Decision**: Follow standard VST3 instrument conventions.

Key patterns from the VST3 SDK documentation and Iterum reference:
1. **Bus configuration**: 0 audio inputs + 1 stereo audio output + 1 event input (MIDI). Reject any other arrangement in `setBusArrangements()`.
2. **Event handling**: Iterate `IEventList`, handle `kNoteOnEvent` and `kNoteOffEvent`. Velocity-0 noteOn = noteOff per MIDI convention.
3. **Parameter changes**: Read `IParameterChanges` in process(), get last point value for each changed parameter, route by ID range.
4. **Host tempo**: Read from `data.processContext` if available, use defaults (120 BPM, 4/4) if null.
5. **State format**: Version-prefixed, little-endian IBStreamer, deterministic field order.

## R-007: Dropdown Mappings for Ruinae

**Decision**: Create `plugins/ruinae/src/parameters/dropdown_mappings.h` with Ruinae-specific enum mapping functions.

Required dropdown mappings:
- OscType (10 items): PolyBLEP, Wavetable, PhaseDistortion, Sync, Additive, Chaos, Particle, Formant, SpectralFreeze, Noise
- RuinaeFilterType (7 items): SVF LP, SVF HP, SVF BP, SVF Notch, Ladder, Formant, Comb
- RuinaeDistortionType (6 items): Clean, ChaosWaveshaper, Spectral, Granular, Wavefolder, Tape
- MixMode (2 items): Crossfade, SpectralMorph
- RuinaeDelayType (5 items): Digital, Tape, PingPong, Granular, Spectral
- Waveform/LFO Shape (6 items): Sine, Triangle, Sawtooth, Square, S&H, SmoothRandom
- MonoMode Priority (3 items): Last, Low, High
- PortaMode (2 items): Always, Legato Only
- SVFMode for global filter (4 items): Lowpass, Highpass, Bandpass, Notch
- ModSource (13 items): None through Transient
- RuinaeModDest (7 items): GlobalFilterCutoff through AllVoiceTranceGateRate

Each mapping function: `inline constexpr EnumType getEnumFromDropdown(int index)` with lookup table pattern matching Iterum's `dropdown_mappings.h`.

## R-008: CMake Build Integration

**Decision**: Update `plugins/ruinae/CMakeLists.txt` to include all new source files.

The existing CMakeLists.txt already links against `sdk`, `vstgui_support`, `KrateDSP`, and `KratePluginsShared`. Changes needed:
1. Add all new parameter pack headers to the source list
2. Add `src/controller/parameter_helpers.h`
3. Add `src/parameters/note_value_ui.h`
4. Add `src/parameters/dropdown_mappings.h`
5. No new library dependencies needed

The root CMakeLists.txt already includes `add_subdirectory(plugins/ruinae)` -- no changes needed there.

## R-009: Test Strategy

**Decision**: Create two test categories organized per user story (US1-US7) as per tasks.md structure.

### Unit Tests (`tests/unit/plugin_shell_test.cpp`)
1. **Parameter pack tests**: For each parameter pack, verify:
   - Default values are sensible (e.g., default amp envelope produces audible sound)
   - Denormalization produces correct ranges (e.g., normalized 0.5 -> 500ms for envelope)
   - Boundary values are clamped correctly
   - Save/load round-trip is lossless (< 1e-6 precision loss)

2. **State version tests**:
   - Version 1 state saves and loads correctly
   - Future version (999) fails gracefully with safe defaults
   - Truncated state fails gracefully with safe defaults
   - State round-trip preserves all parameters

### Integration Tests (organized per user story - see tasks.md)
1. **Processor lifecycle (US1)**: initialize() -> setupProcessing() -> setActive(true) -> process() with MIDI -> verify audio output
2. **Parameter flow (US3)**: Set parameter via IParameterChanges -> verify engine state changes
3. **Bus configuration (US1)**: Verify correct bus setup (no audio in, stereo out)
4. **Event handling (US5)**: Verify noteOn/noteOff dispatch, velocity-0 handling, pitch bend, aftertouch
5. **Tempo forwarding (US6)**: Verify ProcessContext tempo reaches engine BlockContext

**Note (A13)**: Existing integration tests provide coverage for edge cases as documented in spec.md Edge Cases section. No new tasks needed, just documented coverage mapping in tasks.md or spec.md.

### Pluginval
- Run at strictness level 5 after all other tests pass
- Validates factory, parameters, state, processing, and bus configuration
