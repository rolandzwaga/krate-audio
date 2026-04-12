# Implementation Plan: Membrum Phase 4 -- 32-Pad Layout, Per-Pad Presets, Kit Presets, Separate Outputs

**Branch**: `139-membrum-phase4-pads` | **Date**: 2026-04-12 | **Spec**: `specs/139-membrum-phase4-pads/spec.md`
**Input**: Feature specification from `/specs/139-membrum-phase4-pads/spec.md`

## Summary

Phase 4 transforms Membrum from a single-template polyphonic drum synth into a full 32-pad drum machine. Each pad (MIDI 36-67, following GM Level 1) gets independent exciter type, body model, and 30 sound parameters stored in pre-allocated `PadConfig` structures. The plugin exposes 16 stereo output buses (1 main + 15 auxiliary) for per-pad mix routing, supports kit presets (all 32 pads) and per-pad presets (individual sounds), and introduces a "selected pad proxy" pattern so the host-generic editor remains usable. State format bumps to v4 with minimal v3 migration (no released version).

## Technical Context

**Language/Version**: C++20 (MSVC 2022, Clang 15+, GCC 12+)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (shared DSP lib), KratePluginsShared (preset manager, UI components)
**Storage**: Binary IBStream for state/presets (existing pattern); filesystem for preset files via PresetManager
**Testing**: Catch2 v3 (membrum_tests executable)
**Target Platform**: Windows 10/11, macOS 11+ (Intel + Apple Silicon), Linux (optional)
**Project Type**: Monorepo plugin -- `plugins/membrum/`
**Performance Goals**: Total plugin < 12% single core @ 44.1 kHz 8-voice worst case (SC-006). Zero audio-thread allocations (SC-007).
**Constraints**: All DSP real-time safe. No allocations in audio thread. Pre-allocate everything in initialize/setupProcessing.
**Scale/Scope**: 1024 per-pad parameters + ~35 global parameters. 8 KB state blob. 3+ factory kit presets.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Processor and Controller remain separate. Per-pad data in Processor; proxy logic in Controller. No cross-includes. |
| II. Real-Time Audio Thread Safety | PASS | PadConfig[32] pre-allocated. No allocation on noteOn/processBlock. Bus active flags are simple booleans updated outside process(). |
| III. Modern C++ Standards | PASS | Using std::array, constexpr, designated initializers. No raw new/delete for PadConfig. |
| IV. SIMD & DSP Optimization | N/A | Phase 4 is architecture/plumbing, not DSP algorithm work. Per-pad lookup adds negligible overhead. |
| V. VSTGUI Development | N/A | No custom UI in Phase 4 (FR-092). Host-generic editor only. |
| VI. Cross-Platform Compatibility | PASS | All code uses standard C++ and VST3 SDK abstractions. AU config updated for multi-output. |
| VII. Project Structure & Build System | PASS | New files follow existing directory structure. CMakeLists.txt updated. |
| VIII. Testing Discipline | PASS | Tests written before implementation per canonical todo. |
| IX. Layered DSP Architecture | PASS | PadConfig is plugin-local (plugins/membrum/src/dsp/). No DSP layer violations. |
| X. DSP Processing Constraints | N/A | No new DSP processing (existing exciter/body/tone shaper unchanged). |
| XI. Performance Budgets | PASS | Per-pad lookup is O(1) array index. Multi-bus output adds one extra buffer write per active aux bus. |
| XII. Debugging Discipline | PASS | N/A for planning phase. |
| XIII. Test-First Development | PASS | All task groups begin with failing tests. |
| XIV. Living Architecture | PASS | Will update specs/_architecture_/membrum-plugin.md. |
| XV. Pre-Implementation Research (ODR) | PASS | See Codebase Research section below. |
| XVI. Honest Completion | PASS | Compliance table in spec with per-FR evidence. |

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Existing? | Action |
|--------------|-----------|--------|
| PadConfig | No -- `grep -r "class PadConfig" plugins/` returns nothing | Create New in `plugins/membrum/src/dsp/pad_config.h` |
| DefaultKit (namespace/functions) | No | Create New in `plugins/membrum/src/dsp/default_kit.h` |
| MembrumPresetConfig (function) | No -- `grep -r "MembrumPreset" plugins/` returns nothing | Create New in `plugins/membrum/src/preset/membrum_preset_config.h` |
| PadPresetManager (typedef/alias) | No | Will be a second `PresetManager` instance, not a new class |

**Utility Functions to be created**:

| Planned Function | Existing? | Location | Action |
|------------------|-----------|----------|--------|
| padParamId() | No | N/A | Create in pad_config.h |
| padIndexFromParamId() | No | N/A | Create in pad_config.h |
| padOffsetFromParamId() | No | N/A | Create in pad_config.h |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| VoicePool | `plugins/membrum/src/voice_pool/voice_pool.h` | Plugin | Extend with PadConfig[32], per-pad dispatch, multi-bus output |
| ChokeGroupTable | `plugins/membrum/src/voice_pool/choke_group_table.h` | Plugin | Per-pad choke groups (already supports 32 entries) |
| VoiceMeta | `plugins/membrum/src/voice_pool/voice_meta.h` | Plugin | Already has `originatingNote` -- pad index = note - 36 |
| DrumVoice | `plugins/membrum/src/dsp/drum_voice.h` | Plugin | Unchanged -- configured at noteOn via PadConfig |
| VoiceCommonParams | `plugins/membrum/src/dsp/voice_common_params.h` | Plugin | May be superseded by PadConfig or kept as a subset |
| ExciterBank | `plugins/membrum/src/dsp/exciter_bank.h` | Plugin | Used per-voice as before |
| BodyBank | `plugins/membrum/src/dsp/body_bank.h` | Plugin | Used per-voice as before |
| ToneShaper | `plugins/membrum/src/dsp/tone_shaper.h` | Plugin | Configured per-pad at noteOn |
| PresetManager | `plugins/shared/src/preset/preset_manager.h` | Shared | Kit preset save/load |
| PresetManagerConfig | `plugins/shared/src/preset/preset_manager_config.h` | Shared | Config for both kit and pad preset managers |

### Files Checked for Conflicts

- [x] `plugins/membrum/src/dsp/` - No PadConfig, no DefaultKit
- [x] `plugins/membrum/src/voice_pool/` - SharedParams exists (will be replaced)
- [x] `plugins/membrum/src/preset/` - Directory does not exist yet (will create)
- [x] `plugins/shared/src/preset/` - PresetManager exists (will reuse, not duplicate)
- [x] `specs/_architecture_/membrum-plugin.md` - Will update after implementation

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (PadConfig, DefaultKit functions) are unique to the Membrum namespace and not found anywhere in the codebase. The existing SharedParams struct in VoicePool will be replaced, not duplicated. No name conflicts.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| VoicePool | processBlock | `void processBlock(float* outL, float* outR, int numSamples) noexcept` | Yes |
| VoicePool | noteOn | `void noteOn(std::uint8_t midiNote, float velocity) noexcept` | Yes |
| VoicePool | setMaxPolyphony | `void setMaxPolyphony(int n) noexcept` | Yes |
| VoicePool | setChokeGroup | `void setChokeGroup(std::uint8_t group) noexcept` | Yes |
| VoicePool | setSharedVoiceParams | `void setSharedVoiceParams(float mat, float sz, float dec, float sp, float lv)` | Yes (to be removed) |
| VoicePool::SharedParams | material, size, etc. | struct with float fields + ExciterType + BodyModelType | Yes (to be removed) |
| ChokeGroupTable | lookup | `uint8_t lookup(uint8_t midiNote) const noexcept` | Yes |
| ChokeGroupTable | loadFromRaw | `void loadFromRaw(const std::array<uint8_t, 32>& in) noexcept` | Yes |
| VoiceMeta | originatingNote | `uint8_t originatingNote = 0` | Yes |
| DrumVoice | setExciterType | `void setExciterType(ExciterType type)` | Yes |
| DrumVoice | setBodyModel | `void setBodyModel(BodyModelType model)` | Yes |
| DrumVoice | setMaterial | `void setMaterial(float v)` | Yes |
| DrumVoice | toneShaper() | `ToneShaper& toneShaper()` | Yes |
| DrumVoice | unnaturalZone() | `UnnaturalZone& unnaturalZone()` | Yes |
| PresetManager | setStateProvider | `void setStateProvider(StateProvider provider)` | Yes |
| PresetManager | setLoadProvider | `void setLoadProvider(LoadProvider provider)` | Yes |
| PresetManagerConfig | subcategoryNames | `std::vector<std::string> subcategoryNames` | Yes |
| AudioEffect | addAudioOutput | inherited from Component, adds bus to output bus list | Yes |
| AudioEffect | activateBus | `tresult activateBus(MediaType, BusDirection, int32, TBool)` -- base impl calls Bus::setActive | Yes |

### Header Files Read

- [x] `plugins/membrum/src/voice_pool/voice_pool.h` - VoicePool, SharedParams, processBlock, noteOn
- [x] `plugins/membrum/src/voice_pool/voice_meta.h` - VoiceMeta struct
- [x] `plugins/membrum/src/voice_pool/choke_group_table.h` - ChokeGroupTable
- [x] `plugins/membrum/src/dsp/drum_voice.h` - DrumVoice API
- [x] `plugins/membrum/src/dsp/voice_common_params.h` - VoiceCommonParams
- [x] `plugins/membrum/src/processor/processor.h` - Processor fields
- [x] `plugins/membrum/src/controller/controller.h` - Controller API
- [x] `plugins/shared/src/preset/preset_manager.h` - PresetManager, StateProvider, LoadProvider
- [x] `plugins/shared/src/preset/preset_manager_config.h` - PresetManagerConfig
- [x] `extern/vst3sdk/public.sdk/source/vst/vstcomponent.cpp` - activateBus base impl

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| VoicePool::processBlock | Currently writes mono to both L/R channels | Phase 4 extends to multi-bus but keeps mono-to-stereo behavior |
| activateBus | Called from component thread, not audio thread | Safe to update busActive_ array without atomic -- VST3 guarantees no overlap with process() |
| PresetManager::StateProvider | Returns IBStream* -- caller owns the stream | Must create MemoryStream and return it; PresetManager writes it to file |
| ChokeGroupTable | setGlobal() sets ALL 32 entries | Phase 4 needs per-pad setChokeGroup -- use direct array write or new method |
| Per-pad params | StringListParameter needs discrete values | For per-pad selectors (ExciterType, BodyModel), use StringListParameter with padded name |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | No new Layer 0 candidates | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| padParamId() | Plugin-specific parameter ID computation, only used by Membrum |
| padIndexFromParamId() | Plugin-specific, constexpr helper |

**Decision**: No Layer 0 extractions. All new code is plugin-local.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | N/A | Phase 4 is architecture, not DSP algorithm work |
| **Data parallelism width** | N/A | No new inner loops added |
| **Branch density in inner loop** | N/A | Multi-bus output routing is O(1) per voice, not inner-loop |
| **Dominant operations** | Memory/routing | Buffer copies and accumulations |
| **Current CPU budget vs expected usage** | 12% budget, expected minimal overhead | Per-pad lookup is array index; aux bus write is one extra memcpy |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: Phase 4 adds architectural plumbing (per-pad config lookup, multi-bus buffer routing), not compute-intensive DSP. The hot path change is one additional buffer accumulation per active auxiliary bus per voice. This is memory-bound, not compute-bound, and does not benefit from SIMD.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip inactive aux bus writes | Saves one buffer accumulation per inactive bus per voice | LOW | YES (already in spec: FR-045) |
| Skip silent voices early | Already implemented in Phase 3 | N/A | Already done |

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin-local architecture

**Related features at same layer:**
- Phase 5: Cross-pad coupling (reads PadConfig modal frequencies)
- Phase 6: Custom VSTGUI editor (reads PadConfig for display, uses kSelectedPadId proxy)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| PadConfig struct | HIGH | Phase 5 (coupling reads pad frequencies), Phase 6 (UI displays pad params) | Keep in membrum/src/dsp/, expose via public header |
| padParamId() helpers | HIGH | Phase 6 (UI needs pad-to-param mapping) | Keep in pad_config.h |
| DefaultKit templates | MEDIUM | Phase 6 (kit reset button), any future "initialize to defaults" | Keep in default_kit.h |
| Multi-bus output routing | LOW | Other plugins unlikely to need this exact pattern | Keep in VoicePool |
| Selected pad proxy pattern | MEDIUM | Could be generalized but wait for 2nd consumer | Keep in Controller |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| PadConfig in membrum/src/dsp/ not shared/ | Only Membrum uses per-pad configs; no other plugin needs it |
| No shared multi-bus base class | First and likely only multi-bus plugin in this project |
| Two PresetManager instances (kit + pad) | Cleaner than one manager with type discrimination |

## Project Structure

### Documentation (this feature)

```text
specs/139-membrum-phase4-pads/
    plan.md              # This file
    research.md          # Phase 0 research findings
    data-model.md        # Entity definitions and binary formats
    quickstart.md        # Build/test quick reference
    contracts/           # API contracts
        pad-config.h
        voice-pool-v4.h
        processor-v4.h
        controller-v4.h
```

### Source Code (changes)

```text
plugins/membrum/
    src/
        plugin_ids.h                          # MODIFIED: add kSelectedPadId, kPadBaseId, state v4
        dsp/
            pad_config.h                      # NEW: PadConfig struct, constants, helpers
            default_kit.h                     # NEW: GM-inspired default templates
            voice_common_params.h             # MODIFIED: may be simplified (PadConfig supersedes)
        voice_pool/
            voice_pool.h                      # MODIFIED: PadConfig[32], multi-bus processBlock
            voice_pool.cpp                    # MODIFIED: per-pad dispatch, multi-bus routing
            choke_group_table.h               # MODIFIED: add per-pad setter if needed
        processor/
            processor.h                       # MODIFIED: busActive_, remove per-pad atomics
            processor.cpp                     # MODIFIED: per-pad param dispatch, state v4, multi-bus
        controller/
            controller.h                      # MODIFIED: add proxy state tracking
            controller.cpp                    # MODIFIED: 1024 per-pad params, proxy logic
        preset/                               # NEW directory
            membrum_preset_config.h           # NEW: kit + pad preset configs
    CMakeLists.txt                            # MODIFIED: add new source files
    resources/
        au-info.plist                         # MODIFIED: multi-output config
        auv3/audiounitconfig.h                # MODIFIED: multi-output config
    tests/
        unit/
            vst/
                test_pad_config.cpp           # NEW: PadConfig struct tests
                test_state_v4.cpp             # NEW: v4 round-trip
                test_state_migration_v3_to_v4.cpp  # NEW: v3->v4 migration
                test_pad_parameters.cpp       # NEW: per-pad parameter registration
            voice_pool/
                test_per_pad_dispatch.cpp     # NEW: voice pool per-pad config
                test_multi_bus_output.cpp     # NEW: multi-bus routing
            processor/
                test_default_kit.cpp          # NEW: GM default templates
            preset/                           # NEW directory
                test_kit_preset.cpp           # NEW: kit preset save/load
                test_pad_preset.cpp           # NEW: per-pad preset save/load
```

**Structure Decision**: Plugin-local structure following existing Membrum conventions. New `preset/` directory mirrors the pattern from Iterum/Disrumpo. New test directories mirror source structure.

## Complexity Tracking

No constitution violations. All decisions follow established patterns.

| Decision | Justification |
|----------|---------------|
| 1024 per-pad parameters | Required by spec. VST3 supports up to 2^31 parameter IDs. Host-generic editor handles large parameter counts. |
| Two PresetManager instances | Clean separation between kit and pad presets with different subcategories |
| Binary state format (not JSON) | Consistency with v1-v3 state format and existing PresetManager IBStream pattern |
