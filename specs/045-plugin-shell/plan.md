# Implementation Plan: Ruinae Plugin Shell

**Branch**: `045-plugin-shell` | **Date**: 2026-02-09 | **Spec**: `specs/045-plugin-shell/spec.md`
**Input**: Feature specification from `/specs/045-plugin-shell/spec.md`

## Summary

Create the VST3 plugin shell for the Ruinae chaos/spectral hybrid synthesizer. This integrates the RuinaeEngine (Phase 6) into the VST3 plugin framework by implementing: complete parameter packs for all 19 synthesizer sections (following the Iterum pattern), Processor-to-Engine MIDI dispatch and parameter routing, Controller parameter registration with proper display formatting, versioned state persistence with stepwise migration, host tempo/transport forwarding, and CMake build integration. The existing skeleton from Phase 6 provides the starting point; this spec fills in the parameter packs, wires up the engine, and completes state serialization.

## Technical Context

**Language/Version**: C++20 (MSVC on Windows, Clang on macOS, GCC on Linux)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (Layer 0-4), KratePluginsShared (PresetManager)
**Storage**: IBStreamer binary state serialization (VST3 host-managed)
**Testing**: Catch2 v3.4.0 (unit tests for parameter packs, integration tests for state round-trip, pluginval for compliance)
**Target Platform**: Windows 10/11 (MSVC), macOS 11+ (Clang), Linux (GCC) -- cross-platform
**Project Type**: VST3 plugin within monorepo
**Performance Goals**: < 5% single core @ 44.1kHz stereo, 0 allocations in audio thread, < 500ms plugin scan time
**Constraints**: Real-time audio thread safety (no allocations, locks, exceptions in process()), all parameters normalized 0.0-1.0 at VST boundary
**Scale/Scope**: ~80-100 parameters across 19 sections, 16-voice polyphony max

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller are separate classes (existing skeleton)
- [x] kDistributable flag set (existing entry.cpp)
- [x] State flows Host -> Processor -> Controller via setComponentState()
- [x] No cross-includes between processor/ and controller/

**Principle II (Real-Time Audio Thread Safety):**
- [x] All parameter storage uses std::atomic
- [x] No allocations in process() -- buffers pre-allocated in setupProcessing()
- [x] No locks, mutexes, or blocking primitives in audio path
- [x] No exceptions in audio path

**Principle III (Modern C++ Standards):**
- [x] C++20, RAII, smart pointers, constexpr
- [x] No raw new/delete outside factory createInstance()

**Principle V (VSTGUI Development):**
- [x] UI is placeholder (Phase 8 scope), only basic VST3Editor creation
- [x] All values normalized 0.0-1.0 at VST boundary

**Principle VI (Cross-Platform Compatibility):**
- [x] No platform-specific code in plugin shell
- [x] Uses VSTGUI cross-platform abstractions

**Principle VII (Project Structure & Build System):**
- [x] CMake 3.20+ with target-based configuration
- [x] Monorepo structure followed (plugins/ruinae/)

**Principle VIII (Testing Discipline):**
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI (Honest Completion):**
- [x] Compliance table will be verified against actual code/test output

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: Parameter pack structs (GlobalParams, OscAParams, OscBParams, MixerParams, FilterParams, DistortionParams, TranceGateParams_, AmpEnvParams, FilterEnvParams, ModEnvParams, LFO1Params, LFO2Params, ChaosModParams, ModMatrixParams, GlobalFilterParams, FreezeParams_, DelayParams, ReverbParams_, MonoModeParams)

Note: The trailing underscore on some names (e.g., TranceGateParams_) is to avoid ODR collision with DSP-layer structs of similar names.

| Planned Type | Search Performed | Existing? | Action |
|---|---|---|---|
| GlobalParams | `grep -r "struct GlobalParams" dsp/ plugins/` | No | Create New in `Ruinae` namespace |
| OscAParams | `grep -r "struct OscAParams" dsp/ plugins/` | No | Create New |
| OscBParams | `grep -r "struct OscBParams" dsp/ plugins/` | No | Create New |
| MixerParams | `grep -r "struct MixerParams" dsp/ plugins/` | No | Create New |
| RuinaeFilterParams | `grep -r "struct RuinaeFilterParams" dsp/ plugins/` | No | Create New (uses Ruinae prefix to avoid conflict with any DSP FilterParams) |
| RuinaeDistortionParams | `grep -r "struct RuinaeDistortionParams" dsp/ plugins/` | No | Create New |
| RuinaeTranceGateParams | `grep -r "struct RuinaeTranceGateParams" dsp/ plugins/` | No | Create New (avoids collision with `Krate::DSP::TranceGateParams`) |
| AmpEnvParams | `grep -r "struct AmpEnvParams" dsp/ plugins/` | No | Create New |
| FilterEnvParams | `grep -r "struct FilterEnvParams" dsp/ plugins/` | No | Create New |
| ModEnvParams | `grep -r "struct ModEnvParams" dsp/ plugins/` | No | Create New |
| LFO1Params | `grep -r "struct LFO1Params" dsp/ plugins/` | No | Create New |
| LFO2Params | `grep -r "struct LFO2Params" dsp/ plugins/` | No | Create New |
| ChaosModParams | `grep -r "struct ChaosModParams" dsp/ plugins/` | No | Create New |
| ModMatrixParams | `grep -r "struct ModMatrixParams" dsp/ plugins/` | No | Create New |
| GlobalFilterParams | `grep -r "struct GlobalFilterParams" dsp/ plugins/` | No | Create New |
| RuinaeFreezeParams | `grep -r "struct RuinaeFreezeParams" dsp/ plugins/` | No | Create New |
| RuinaeDelayParams | `grep -r "struct RuinaeDelayParams" dsp/ plugins/` | No | Create New |
| RuinaeReverbParams | `grep -r "struct RuinaeReverbParams" dsp/ plugins/` | No | Create New (avoids collision with `Krate::DSP::ReverbParams`) |
| MonoModeParams | `grep -r "struct MonoModeParams" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Performed | Existing? | Location | Action |
|---|---|---|---|---|
| createDropdownParameter | `grep -r "createDropdownParameter" plugins/` | Yes | `plugins/iterum/src/controller/parameter_helpers.h` | Copy to Ruinae (different namespace) or move to shared |
| createDropdownParameterWithDefault | `grep -r "createDropdownParameterWithDefault" plugins/` | Yes | `plugins/iterum/src/controller/parameter_helpers.h` | Copy to Ruinae |
| createNoteValueDropdown | `grep -r "createNoteValueDropdown" plugins/` | Yes | `plugins/iterum/src/controller/parameter_helpers.h` | Copy to Ruinae |

**Decision**: Copy `parameter_helpers.h` to `plugins/ruinae/src/controller/parameter_helpers.h` in the `Ruinae` namespace. Also copy `note_value_ui.h` to `plugins/ruinae/src/parameters/note_value_ui.h` in the `Ruinae::Parameters` namespace. These files contain no Iterum-specific logic but are in the Iterum namespace. Moving to shared would be ideal but changes Iterum code; safer to copy for now and note for future refactoring.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|---|---|---|---|
| RuinaeEngine | `plugins/ruinae/src/engine/ruinae_engine.h` | 3 | Owned by Processor, all DSP goes through it |
| RuinaeVoice | `plugins/ruinae/src/engine/ruinae_voice.h` | 3 | Composed inside RuinaeEngine |
| RuinaeEffectsChain | `plugins/ruinae/src/engine/ruinae_effects_chain.h` | 3 | Composed inside RuinaeEngine |
| BlockContext | `dsp/include/krate/dsp/core/block_context.h` | 0 | Passed to engine.setBlockContext() |
| ReverbParams | `dsp/include/krate/dsp/effects/reverb.h` | 4 | Used to construct params for engine.setReverbParams() |
| TranceGateParams | `dsp/include/krate/dsp/processors/trance_gate.h` | 2 | Used to construct params for engine.setTranceGateParams() |
| parameter_helpers.h | `plugins/iterum/src/controller/parameter_helpers.h` | UI | Dropdown helper functions (copied to Ruinae namespace) |
| note_value_ui.h | `plugins/iterum/src/parameters/note_value_ui.h` | UI | Note value dropdown strings (copied to Ruinae namespace) |
| PresetManager | `plugins/shared/src/preset/preset_manager.h` | Shared | Already linked and initialized in controller |
| KratePluginsShared | `plugins/shared/` | Shared | Preset browser, MIDI CC manager |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (BlockContext, modulation_types)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (svf, lfo, envelope)
- [x] `dsp/include/krate/dsp/systems/ruinae_types.h` - Enum types for Ruinae voice
- [x] `dsp/include/krate/dsp/effects/reverb.h` - ReverbParams struct
- [x] `dsp/include/krate/dsp/processors/trance_gate.h` - TranceGateParams struct
- [x] `plugins/iterum/src/parameters/` - Iterum parameter pack pattern (reference)
- [x] `plugins/ruinae/src/` - Existing skeleton files
- [x] `plugins/shared/` - Shared infrastructure

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned parameter pack structs use unique names within the `Ruinae` namespace. Structs that could collide with DSP-layer types (TranceGateParams, ReverbParams, FilterParams) use the `Ruinae` prefix (e.g., RuinaeTranceGateParams, RuinaeReverbParams). The Ruinae namespace isolates all plugin-layer types from the Krate::DSP namespace.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|---|---|---|---|
| RuinaeEngine | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` (called by Processor::setupProcessing() internally) | Yes |
| RuinaeEngine | reset | `void reset() noexcept` | Yes |
| RuinaeEngine | processBlock | `void processBlock(float* left, float* right, size_t numSamples) noexcept` | Yes |
| RuinaeEngine | noteOn | `void noteOn(uint8_t note, uint8_t velocity) noexcept` | Yes |
| RuinaeEngine | noteOff | `void noteOff(uint8_t note) noexcept` | Yes |
| RuinaeEngine | setBlockContext | `void setBlockContext(const BlockContext& ctx) noexcept` | Yes |
| RuinaeEngine | setPolyphony | `void setPolyphony(size_t count) noexcept` | Yes |
| RuinaeEngine | setMode | `void setMode(VoiceMode mode) noexcept` | Yes |
| RuinaeEngine | setMasterGain | `void setMasterGain(float gain) noexcept` | Yes |
| RuinaeEngine | setSoftLimitEnabled | `void setSoftLimitEnabled(bool enabled) noexcept` | Yes |
| RuinaeEngine | setOscAType | `void setOscAType(OscType type) noexcept` | Yes |
| RuinaeEngine | setOscATuneSemitones | `void setOscATuneSemitones(float semitones) noexcept` | Yes (ADDED per A1) |
| RuinaeEngine | setOscAFineCents | `void setOscAFineCents(float cents) noexcept` | Yes (ADDED per A1) |
| RuinaeEngine | setOscALevel | `void setOscALevel(float level) noexcept` | Yes (ADDED per A1) |
| RuinaeEngine | setOscBType | `void setOscBType(OscType type) noexcept` | Yes |
| RuinaeEngine | setOscBTuneSemitones | `void setOscBTuneSemitones(float semitones) noexcept` | Yes (ADDED per A1) |
| RuinaeEngine | setOscBFineCents | `void setOscBFineCents(float cents) noexcept` | Yes (ADDED per A1) |
| RuinaeEngine | setOscBLevel | `void setOscBLevel(float level) noexcept` | Yes (ADDED per A1) |
| RuinaeEngine | setMixMode | `void setMixMode(MixMode mode) noexcept` | Yes |
| RuinaeEngine | setMixPosition | `void setMixPosition(float mix) noexcept` | Yes |
| RuinaeEngine | setFilterType | `void setFilterType(RuinaeFilterType type) noexcept` | Yes |
| RuinaeEngine | setFilterCutoff | `void setFilterCutoff(float hz) noexcept` | Yes |
| RuinaeEngine | setFilterResonance | `void setFilterResonance(float q) noexcept` | Yes |
| RuinaeEngine | setFilterEnvAmount | `void setFilterEnvAmount(float semitones) noexcept` | Yes |
| RuinaeEngine | setFilterKeyTrack | `void setFilterKeyTrack(float amount) noexcept` | Yes |
| RuinaeEngine | setDistortionType | `void setDistortionType(RuinaeDistortionType type) noexcept` | Yes |
| RuinaeEngine | setDistortionDrive | `void setDistortionDrive(float drive) noexcept` | Yes |
| RuinaeEngine | setDistortionCharacter | `void setDistortionCharacter(float character) noexcept` | Yes |
| RuinaeEngine | setDistortionMix | `void setDistortionMix(float mix) noexcept` | Yes (ADDED per A1) |
| RuinaeEngine | setTranceGateEnabled | `void setTranceGateEnabled(bool enabled) noexcept` | Yes |
| RuinaeEngine | setTranceGateParams | `void setTranceGateParams(const TranceGateParams& params) noexcept` | Yes |
| RuinaeEngine | setAmpAttack | `void setAmpAttack(float ms) noexcept` | Yes |
| RuinaeEngine | setAmpDecay | `void setAmpDecay(float ms) noexcept` | Yes |
| RuinaeEngine | setAmpSustain | `void setAmpSustain(float level) noexcept` | Yes |
| RuinaeEngine | setAmpRelease | `void setAmpRelease(float ms) noexcept` | Yes |
| RuinaeEngine | setFilterAttack | `void setFilterAttack(float ms) noexcept` | Yes |
| RuinaeEngine | setFilterDecay | `void setFilterDecay(float ms) noexcept` | Yes |
| RuinaeEngine | setFilterSustain | `void setFilterSustain(float level) noexcept` | Yes |
| RuinaeEngine | setFilterRelease | `void setFilterRelease(float ms) noexcept` | Yes |
| RuinaeEngine | setModAttack | `void setModAttack(float ms) noexcept` | Yes |
| RuinaeEngine | setModDecay | `void setModDecay(float ms) noexcept` | Yes |
| RuinaeEngine | setModSustain | `void setModSustain(float level) noexcept` | Yes |
| RuinaeEngine | setModRelease | `void setModRelease(float ms) noexcept` | Yes |
| RuinaeEngine | setGlobalLFO1Rate | `void setGlobalLFO1Rate(float hz) noexcept` | Yes |
| RuinaeEngine | setGlobalLFO1Waveform | `void setGlobalLFO1Waveform(Waveform shape) noexcept` | Yes |
| RuinaeEngine | setGlobalLFO2Rate | `void setGlobalLFO2Rate(float hz) noexcept` | Yes |
| RuinaeEngine | setGlobalLFO2Waveform | `void setGlobalLFO2Waveform(Waveform shape) noexcept` | Yes |
| RuinaeEngine | setChaosSpeed | `void setChaosSpeed(float speed) noexcept` | Yes |
| RuinaeEngine | setGlobalModRoute | `void setGlobalModRoute(int slot, ModSource source, RuinaeModDest dest, float amount) noexcept` | Yes |
| RuinaeEngine | setGlobalFilterEnabled | `void setGlobalFilterEnabled(bool enabled) noexcept` | Yes |
| RuinaeEngine | setGlobalFilterCutoff | `void setGlobalFilterCutoff(float hz) noexcept` | Yes |
| RuinaeEngine | setGlobalFilterResonance | `void setGlobalFilterResonance(float q) noexcept` | Yes |
| RuinaeEngine | setGlobalFilterType | `void setGlobalFilterType(SVFMode mode) noexcept` | Yes |
| RuinaeEngine | setFreezeEnabled | `void setFreezeEnabled(bool enabled) noexcept` | Yes |
| RuinaeEngine | setFreeze | `void setFreeze(bool frozen) noexcept` | Yes |
| RuinaeEngine | setDelayType | `void setDelayType(RuinaeDelayType type) noexcept` | Yes |
| RuinaeEngine | setDelayTime | `void setDelayTime(float ms) noexcept` | Yes |
| RuinaeEngine | setDelayFeedback | `void setDelayFeedback(float amount) noexcept` | Yes |
| RuinaeEngine | setDelayMix | `void setDelayMix(float mix) noexcept` | Yes |
| RuinaeEngine | setReverbParams | `void setReverbParams(const ReverbParams& params) noexcept` | Yes |
| RuinaeEngine | setTempo | `void setTempo(double bpm) noexcept` | Yes |
| RuinaeEngine | setPitchBend | `void setPitchBend(float bipolar) noexcept` | Yes |
| RuinaeEngine | setAftertouch | `void setAftertouch(float value) noexcept` | Yes |
| RuinaeEngine | setMonoPriority | `void setMonoPriority(MonoMode mode) noexcept` | Yes |
| RuinaeEngine | setLegato | `void setLegato(bool enabled) noexcept` | Yes |
| RuinaeEngine | setPortamentoTime | `void setPortamentoTime(float ms) noexcept` | Yes |
| RuinaeEngine | setPortamentoMode | `void setPortamentoMode(PortaMode mode) noexcept` | Yes |
| RuinaeEngine | setStereoSpread | `void setStereoSpread(float spread) noexcept` | Yes |
| RuinaeEngine | setStereoWidth | `void setStereoWidth(float width) noexcept` | Yes |
| RuinaeEngine | setVoiceModRoute | `void setVoiceModRoute(int index, VoiceModRoute route) noexcept` | Yes |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | Yes (from ProcessContext if kTempoValid, else default 120) |
| BlockContext | isPlaying | `bool isPlaying = false` | Yes (from ProcessContext::flags & kPlaying) |
| BlockContext | sampleRate | `double sampleRate = 44100.0` | Yes (from setupProcessing(), NOT ProcessContext) |
| BlockContext | blockSize | `size_t blockSize = 512` | Yes (from setupProcessing(), NOT ProcessContext) |
| BlockContext | timeSignatureNumerator | `uint8_t timeSignatureNumerator = 4` | Yes (from ProcessContext if kTimeSigValid, else default 4) |
| BlockContext | timeSignatureDenominator | `uint8_t timeSignatureDenominator = 4` | Yes (from ProcessContext if kTimeSigValid, else default 4) |
| BlockContext | transportPositionSamples | `int64_t transportPositionSamples = 0` | Yes (from ProcessContext::projectTimeMusic if available) |

Note (A9): ProcessContext provides tempo, timeSignature, isPlaying, and transport position. sampleRate and blockSize come from setupProcessing(), not ProcessContext. If host doesn't provide ProcessContext or kTempoValid flag is not set, use defaults (120 BPM, 4/4, not playing).
| ReverbParams | (all fields) | `float roomSize=0.5f, damping=0.5f, width=1.0f, mix=0.3f, preDelayMs=0.0f, diffusion=0.7f, bool freeze=false, float modRate=0.5f, modDepth=0.0f` | Yes |
| TranceGateParams | (all fields) | `int numSteps=16, float rateHz=4.0f, depth=1.0f, attackMs=2.0f, releaseMs=10.0f, phaseOffset=0.0f, bool tempoSync=true, NoteValue noteValue=Sixteenth, NoteModifier noteModifier=None, bool perVoice=true` | Yes |

### Header Files Read

- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - Full RuinaeEngine class
- [x] `plugins/ruinae/src/engine/ruinae_voice.h` - RuinaeVoice class
- [x] `plugins/ruinae/src/engine/ruinae_effects_chain.h` - RuinaeEffectsChain class
- [x] `dsp/include/krate/dsp/core/block_context.h` - BlockContext struct
- [x] `dsp/include/krate/dsp/effects/reverb.h` - ReverbParams struct
- [x] `dsp/include/krate/dsp/processors/trance_gate.h` - TranceGateParams struct
- [x] `dsp/include/krate/dsp/systems/ruinae_types.h` - All Ruinae enum types
- [x] `dsp/include/krate/dsp/core/modulation_types.h` - ModSource enum
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - Waveform enum
- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVFMode enum
- [x] `dsp/include/krate/dsp/processors/mono_handler.h` - MonoMode, PortaMode enums
- [x] `dsp/include/krate/dsp/primitives/envelope_utils.h` - EnvCurve enum
- [x] `dsp/include/krate/dsp/systems/poly_synth_engine.h` - VoiceMode enum

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|---|---|---|
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM = 140.0` |
| BlockContext | Member is `blockSize` not `numSamples` | `ctx.blockSize = data.numSamples` |
| RuinaeEngine | `noteOn` takes `uint8_t note, uint8_t velocity` (0-127), NOT float | Convert VST3 float velocity with rounding: `static_cast<uint8_t>(event.noteOn.velocity * 127.0f + 0.5f)` to avoid truncation errors at boundary values (A21) |
| RuinaeEngine | `setMode` takes `VoiceMode` enum, not int | `engine.setMode(VoiceMode::Poly)` |
| RuinaeEngine | `processBlock` takes `float* left, float* right, size_t numSamples` | NOT `processBlock(data.outputs[0]...)` -- use output buffer pointers directly |
| TranceGateParams | DSP struct is `Krate::DSP::TranceGateParams`, plugin struct is `Ruinae::RuinaeTranceGateParams` | Different namespaces, different purposes |
| ReverbParams | DSP struct is `Krate::DSP::ReverbParams`, plugin struct is `Ruinae::RuinaeReverbParams` | Construct DSP struct from plugin atomics when applying to engine |
| VoiceMode | Defined in `poly_synth_engine.h`, not in `ruinae_types.h` | `#include <krate/dsp/systems/poly_synth_engine.h>` |
| SVFMode | Used for global filter type, has 8 modes but Ruinae only exposes 4 (LP, HP, BP, Notch) | Map dropdown index to SVFMode |
| Velocity-0 noteOn | Must be treated as noteOff per MIDI convention | Check `event.noteOn.velocity == 0` before dispatching |
| OscType enum | Has 10 values (0-9), NumTypes sentinel at 10 | stepCount = 9 for dropdown |
| RuinaeFilterType | Has 7 types + NumTypes sentinel | stepCount = 6 for dropdown |
| RuinaeDistortionType | Has 6 types + NumTypes sentinel | stepCount = 5 for dropdown |
| State version | First field in stream, int32, must be read before any parameters | `streamer.writeInt32(kCurrentStateVersion)` as first write |

## Layer 0 Candidate Analysis

Not applicable -- this feature is a plugin-layer integration (not DSP). No new Layer 0 utilities needed.

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|---|---|---|---|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|---|---|
| All denormalization logic | Specific to Ruinae parameter ranges, inline in parameter pack handlers |

**Decision**: No Layer 0 extraction needed. All new code is plugin-layer.

## SIMD Optimization Analysis

*GATE: Not applicable for this feature.*

### Algorithm Characteristics

This feature is a plugin-layer integration, not a DSP algorithm. It bridges VST3 host APIs to the existing RuinaeEngine. There are no inner loops to optimize -- the hot path (parameter change handling, event dispatch) is I/O-bound and branch-driven.

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This is a plugin integration layer, not a DSP processing feature. The audio processing is entirely delegated to RuinaeEngine (already implemented). Parameter change handling and MIDI dispatch are inherently serial, branch-heavy operations with no data parallelism to exploit.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Plugin Layer (VST3 integration)

**Related features at same layer**:
- Phase 8 (UI Design) -- will consume all parameter registrations from this spec
- Future Krate Audio synthesizer plugins -- could follow same parameter pack pattern

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|---|---|---|---|
| parameter_helpers.h | HIGH | Any future Krate Audio plugin | Keep in Ruinae for now; consider moving to shared after 3rd plugin |
| note_value_ui.h | HIGH | Any tempo-synced plugin | Keep in Ruinae for now (same as Iterum copy) |
| Parameter pack pattern | HIGH | Future synth plugins | Document as template, no code extraction needed |
| State version migration pattern | MEDIUM | Future plugins with state versioning | Keep in Ruinae; extract pattern docs if reused |

### Decision Log

| Decision | Rationale |
|---|---|
| Copy parameter_helpers.h to Ruinae | Avoids modifying Iterum, keeps plugins independent. Only 3 functions. |
| Copy note_value_ui.h to Ruinae | Same rationale. Different namespace (Ruinae::Parameters). |
| Keep all parameter packs in Ruinae namespace | No shared synth parameter infrastructure exists yet. |

### Review Trigger

After implementing Phase 8 (UI):
- [ ] Does the UI need to share parameter helpers? -> Consider shared location
- [ ] Are dropdown patterns identical across plugins? -> Extract common helpers

## Project Structure

### Documentation (this feature)

```text
specs/045-plugin-shell/
 plan.md              # This file
 research.md          # Phase 0 output
 data-model.md        # Phase 1 output
 quickstart.md        # Phase 1 output
 contracts/           # Phase 1 output
 tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
plugins/ruinae/
 src/
   entry.cpp                        # Already exists (complete)
   plugin_ids.h                     # Already exists (complete ID allocation)
   version.h.in                     # Already exists (complete)
   processor/
     processor.h                    # UPDATE: Add RuinaeEngine, all param pack structs
     processor.cpp                  # UPDATE: Wire engine, param routing, state, events
   controller/
     controller.h                   # UPDATE: Minor additions (no new members needed)
     controller.cpp                 # UPDATE: Complete param registration, display, state sync
     parameter_helpers.h            # NEW: Copied from Iterum, Ruinae namespace
   parameters/
     global_params.h                # NEW: Global param pack (ID 0-99)
     osc_a_params.h                 # NEW: OSC A param pack (ID 100-199)
     osc_b_params.h                 # NEW: OSC B param pack (ID 200-299)
     mixer_params.h                 # NEW: Mixer param pack (ID 300-399)
     filter_params.h                # NEW: Filter param pack (ID 400-499)
     distortion_params.h            # NEW: Distortion param pack (ID 500-599)
     trance_gate_params.h           # NEW: Trance Gate param pack (ID 600-699)
     amp_env_params.h               # NEW: Amp Envelope param pack (ID 700-799)
     filter_env_params.h            # NEW: Filter Envelope param pack (ID 800-899)
     mod_env_params.h               # NEW: Mod Envelope param pack (ID 900-999)
     lfo1_params.h                  # NEW: LFO 1 param pack (ID 1000-1099)
     lfo2_params.h                  # NEW: LFO 2 param pack (ID 1100-1199)
     chaos_mod_params.h             # NEW: Chaos Mod param pack (ID 1200-1299)
     mod_matrix_params.h            # NEW: Mod Matrix param pack (ID 1300-1399)
     global_filter_params.h         # NEW: Global Filter param pack (ID 1400-1499)
     freeze_params.h                # NEW: Freeze param pack (ID 1500-1599)
     delay_params.h                 # NEW: Delay param pack (ID 1600-1699)
     reverb_params.h                # NEW: Reverb param pack (ID 1700-1799)
     mono_mode_params.h             # NEW: Mono Mode param pack (ID 1800-1899)
     note_value_ui.h                # NEW: Copied from Iterum, Ruinae namespace
     dropdown_mappings.h            # NEW: Ruinae-specific dropdown enum mappings
   engine/
     ruinae_engine.h                # Already exists (no changes)
     ruinae_voice.h                 # Already exists (no changes)
     ruinae_effects_chain.h         # Already exists (no changes)
   preset/
     ruinae_preset_config.h         # Already exists (no changes)
 tests/
   unit/
     plugin_shell_test.cpp          # NEW: Parameter pack tests, state round-trip
   integration/
     plugin_integration_test.cpp    # NEW: Processor+Controller integration tests
   CMakeLists.txt                   # UPDATE: Add new test sources
 CMakeLists.txt                     # UPDATE: Add new source files
```

**Structure Decision**: Follows the established Iterum plugin pattern (parameters/ subdirectory with one header per section). Each parameter pack header is self-contained with: atomic struct, change handler, registration function, display formatter, save/load functions, and controller sync template function.

## Complexity Tracking

No constitution violations. The design follows all principles and established patterns.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| -- | -- | -- |
