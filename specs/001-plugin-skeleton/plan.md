# Implementation Plan: Disrumpo Plugin Skeleton

**Branch**: `001-plugin-skeleton` | **Date**: 2026-01-27 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/001-plugin-skeleton/spec.md`

## Summary

Create the foundational VST3 plugin skeleton for Disrumpo, a multiband morphing distortion plugin. This implementation establishes:
- CMake project structure following the Krate Audio monorepo pattern
- Bit-encoded parameter ID system per [dsp-details.md](../Disrumpo/dsp-details.md) (NOT Iterum's sequential scheme)
- Processor skeleton with stereo I/O and audio passthrough
- Controller skeleton with parameter registration (no UI)
- State serialization with versioning for future migration
- Pluginval level 1 compliance

## Technical Context

**Language/Version**: C++20 (CMake 3.20+)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP library
**Storage**: Standard VST3 IBStream binary format for state serialization
**Testing**: Catch2 3.x (unit tests), pluginval (validation)
**Target Platform**: Windows primary (MSVC 2022), macOS/Linux validated later
**Project Type**: VST3 Plugin in monorepo (plugins/disrumpo/)
**Performance Goals**: Plugin instantiates < 2 seconds, state save/load < 10ms
**Constraints**: Stereo only, audio passthrough (no DSP processing in skeleton)
**Scale/Scope**: 3 parameters (InputGain, OutputGain, GlobalMix), single preset version

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller are separate classes
- [x] kDistributable flag will be set
- [x] Processor MUST function without Controller

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No allocations in process() - audio passthrough only
- [x] No locks/mutexes in audio thread
- [x] All buffers pre-allocated in setupProcessing()

**Required Check - Principle III (Modern C++):**
- [x] Target C++20
- [x] RAII for resource management
- [x] No raw new/delete

**Required Check - Principle VII (Project Structure):**
- [x] Modern CMake 3.20+ with target-based configuration
- [x] Monorepo structure: plugins/disrumpo/
- [x] Version from version.json

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] All FR-xxx requirements must be verified against implementation
- [x] All SC-xxx success criteria must be measured

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: Disrumpo::Processor, Disrumpo::Controller

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `Disrumpo::Processor` | `grep -r "class Processor" dsp/ plugins/` | No (Iterum::Processor exists in different namespace) | Create New in Disrumpo namespace |
| `Disrumpo::Controller` | `grep -r "class Controller" dsp/ plugins/` | No (Iterum::Controller exists in different namespace) | Create New in Disrumpo namespace |
| `namespace Disrumpo` | `grep -r "namespace Disrumpo" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None - skeleton uses existing SDK functions only

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| makeBandParamId | `grep -r "makeBandParamId" dsp/ plugins/` | No | N/A | Create New (Week 4 per roadmap) |
| makeNodeParamId | `grep -r "makeNodeParamId" dsp/ plugins/` | No | N/A | Create New (Week 4 per roadmap) |

**Note**: The parameter ID helper functions are documented in dsp-details.md but are NOT implemented in this skeleton spec. They are planned for Week 4 (T4.6-T4.7). This skeleton only defines the global parameter IDs (0x0F00-0x0F02).

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| IBStreamer | vst3sdk/public.sdk/source/common/memorystream.h | SDK | State serialization (getState/setState) |
| AudioEffect | vst3sdk/public.sdk/source/vst/vstaudioeffect.h | SDK | Processor base class |
| EditControllerEx1 | vst3sdk/public.sdk/source/vst/vsteditcontroller.h | SDK | Controller base class |
| Iterum entry.cpp | plugins/iterum/src/entry.cpp | Plugin | Reference for plugin factory pattern |
| Iterum CMakeLists.txt | plugins/iterum/CMakeLists.txt | Plugin | Reference for CMake structure |
| Iterum version.h.in | plugins/iterum/src/version.h.in | Plugin | Template for version header |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `specs/_architecture_/` - Component inventory checked (no conflicts)
- [x] `plugins/iterum/src/` - Reference implementation (different namespace)
- [x] `plugins/disrumpo/src/` - Empty placeholder directories

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are in the `Disrumpo` namespace which does not exist in the codebase. The Iterum plugin uses `Iterum` namespace. No class name collisions are possible.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| AudioEffect | initialize | `tresult PLUGIN_API initialize(FUnknown* context) override` | x |
| AudioEffect | terminate | `tresult PLUGIN_API terminate() override` | x |
| AudioEffect | setBusArrangements | `tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs, int32 numOuts) override` | x |
| AudioEffect | setupProcessing | `tresult PLUGIN_API setupProcessing(ProcessSetup& newSetup) override` | x |
| AudioEffect | setActive | `tresult PLUGIN_API setActive(TBool state) override` | x |
| AudioEffect | process | `tresult PLUGIN_API process(ProcessData& data) override` | x |
| AudioEffect | getState | `tresult PLUGIN_API getState(IBStream* state) override` | x |
| AudioEffect | setState | `tresult PLUGIN_API setState(IBStream* state) override` | x |
| EditControllerEx1 | initialize | `tresult PLUGIN_API initialize(FUnknown* context) override` | x |
| EditControllerEx1 | terminate | `tresult PLUGIN_API terminate() override` | x |
| EditControllerEx1 | setComponentState | `tresult PLUGIN_API setComponentState(IBStream* state) override` | x |
| EditControllerEx1 | createView | `IPlugView* PLUGIN_API createView(FIDString name) override` | x |
| IBStreamer | writeInt32 | `bool writeInt32(int32 val)` | x |
| IBStreamer | readInt32 | `bool readInt32(int32& val)` | x |
| IBStreamer | writeFloat | `bool writeFloat(float val)` | x |
| IBStreamer | readFloat | `bool readFloat(float& val)` | x |

### Header Files Read

- [x] `extern/vst3sdk/public.sdk/source/vst/vstaudioeffect.h` - AudioEffect base class
- [x] `extern/vst3sdk/public.sdk/source/vst/vsteditcontroller.h` - EditControllerEx1 base class
- [x] `extern/vst3sdk/public.sdk/source/common/memorystream.h` - IBStreamer helper
- [x] `plugins/iterum/src/processor/processor.h` - Reference implementation
- [x] `plugins/iterum/src/controller/controller.h` - Reference implementation

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| IBStreamer | Must specify endianness | `IBStreamer streamer(state, kLittleEndian)` |
| State serialization | Version field MUST be first | Write int32 version before parameters |
| Parameter IDs | Disrumpo uses bit-encoding, NOT sequential | `0x0Fxx` for global, NOT `100-199` ranges |
| kDistributable | Must be set in DEF_CLASS2 | Enables processor/controller separation |
| createView | Return nullptr for no UI | Skeleton has no UI yet |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

Not applicable - this is a plugin skeleton with no DSP code. The skeleton does not introduce any new DSP utilities.

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| -- | -- |

**Decision**: No Layer 0 extraction needed. This is a plugin infrastructure spec, not a DSP spec.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Plugin Infrastructure (not a DSP layer)

**Related features at same layer** (from roadmap.md):
- 002-band-management (Week 2) - Will extend Processor with crossover network
- 003-distortion-integration (Week 3) - Will extend Processor with distortion adapter
- 004-vstgui-infrastructure (Week 4-5) - Will extend Controller with UI

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| plugin_ids.h parameter encoding | HIGH | All future Disrumpo specs | Keep in plugin, extend as needed |
| State serialization pattern | HIGH | All specs that add parameters | Established here, extended later |
| version.json pattern | HIGH | All Krate Audio plugins | Already established by Iterum |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Use bit-encoded parameter IDs | Per dsp-details.md specification, enables future band/node parameters |
| kPresetVersion = 1 | First release, provides migration path for future versions |
| No UI in skeleton | Spec explicitly states createView returns nullptr |

## Project Structure

### Documentation (this feature)

```text
specs/001-plugin-skeleton/
+-- plan.md              # This file
+-- research.md          # Phase 0 output (minimal - patterns from Iterum)
+-- data-model.md        # Phase 1 output (parameter definitions)
+-- quickstart.md        # Phase 1 output (build instructions)
+-- contracts/           # Phase 1 output (N/A for skeleton)
```

### Source Code (repository root)

```text
plugins/disrumpo/
+-- CMakeLists.txt                    # Plugin build configuration
+-- version.json                      # Version metadata (single source of truth)
+-- src/
|   +-- entry.cpp                     # Plugin factory registration
|   +-- plugin_ids.h                  # FUIDs and parameter ID definitions
|   +-- version.h.in                  # Version header template
|   +-- version.h                     # Generated version header
|   +-- processor/
|   |   +-- processor.h               # Processor class declaration
|   |   +-- processor.cpp             # Processor implementation
|   +-- controller/
|       +-- controller.h              # Controller class declaration
|       +-- controller.cpp            # Controller implementation
+-- resources/
    +-- win32resource.rc.in           # Windows resource template
```

**Structure Decision**: Follows the established Iterum plugin pattern with processor/ and controller/ subdirectories. No dsp/ subdirectory yet (added in Week 3 per roadmap).

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

No violations. All Constitution principles are satisfied:
- Principle I: Processor/Controller separation maintained
- Principle II: Audio passthrough, no allocations in process()
- Principle VII: Modern CMake, monorepo structure
- Principle XIV: ODR prevention complete

---

## Implementation Tasks

### Phase 0 Artifacts

**research.md**: Minimal research needed - patterns established by Iterum reference implementation.

Key findings:
1. **Plugin Factory Pattern**: Use `DEF_CLASS2` with `kDistributable` flag
2. **State Serialization**: Version field first, then parameters in order
3. **Parameter IDs**: Bit-encoded per dsp-details.md (NOT Iterum's sequential scheme)
4. **CMake Pattern**: Read version from version.json, configure version.h.in

### Phase 1 Artifacts

**data-model.md**: Parameter definitions

| Parameter | ID | Range | Default | Unit |
|-----------|----|----|---------|------|
| kInputGainId | 0x0F00 | 0.0-1.0 (normalized) | 0.5 (-6dB) | dB |
| kOutputGainId | 0x0F01 | 0.0-1.0 (normalized) | 0.5 (-6dB) | dB |
| kGlobalMixId | 0x0F02 | 0.0-1.0 (normalized) | 1.0 (100%) | % |

**State Format (kPresetVersion = 1)**:
```
int32  version        // Always 1 for initial release
float  inputGain      // Normalized 0.0-1.0
float  outputGain     // Normalized 0.0-1.0
float  globalMix      // Normalized 0.0-1.0
```

**contracts/**: N/A for plugin skeleton (no API contracts beyond VST3 SDK interfaces)

**quickstart.md**: Build instructions

```bash
# Configure
cmake --preset windows-x64-release

# Build
cmake --build build/windows-x64-release --config Release

# Plugin location
# build/windows-x64-release/VST3/Release/Disrumpo.vst3/

# Validate
tools/pluginval.exe --strictness-level 1 --validate "build/VST3/Release/Disrumpo.vst3"
```

---

## Task Breakdown (T1.1-T1.6)

### T1.1: CMake Project Structure (4h)

**Files to create:**
- `plugins/disrumpo/CMakeLists.txt`
- `plugins/disrumpo/version.json`
- `plugins/disrumpo/src/version.h.in`
- `plugins/disrumpo/resources/win32resource.rc.in`

**Acceptance criteria:**
- CMake configures without error
- version.h generated from version.json
- Links to sdk, vstgui_support, KrateDSP

### T1.2: Parameter ID Encoding System (8h)

**File to create:**
- `plugins/disrumpo/src/plugin_ids.h`

**Contents:**
- Unique FUIDs (Processor and Controller)
- Parameter ID enum with bit-encoding scheme
- Subcategory = "Distortion" (VST3 standard subcategory)

**FUID Generation (Authoritative Source):**
FUIDs will be generated using Visual Studio's `uuidgen` or online UUID v4 generator.
The exact values are defined ONLY in `plugin_ids.h` (single source of truth).
Example pattern (actual values generated at implementation time):
```cpp
static const Steinberg::FUID kProcessorUID(0x????????, 0x????????, 0x????????, 0x????????);
static const Steinberg::FUID kControllerUID(0x????????, 0x????????, 0x????????, 0x????????);
```

**Acceptance criteria:**
- FUIDs are unique (not same as Iterum) - verified by comparing to `plugins/iterum/src/plugin_ids.h`
- Parameter IDs use 0x0Fxx encoding per dsp-details.md
- kInputGainId = 0x0F00, kOutputGainId = 0x0F01, kGlobalMixId = 0x0F02

### T1.3: Processor Skeleton (8h)

**Files to create:**
- `plugins/disrumpo/src/processor/processor.h`
- `plugins/disrumpo/src/processor/processor.cpp`

**Implementation:**
- Inherit from `Steinberg::Vst::AudioEffect`
- Stereo input/output bus configuration
- setBusArrangements: accept stereo only
- setupProcessing: store sample rate and block size
- process: audio passthrough (output = input)
- Atomic parameters for InputGain, OutputGain, GlobalMix

**Acceptance criteria:**
- Compiles without warnings
- Audio passes through unchanged (bit-transparent)

### T1.4: Controller Skeleton (8h)

**Files to create:**
- `plugins/disrumpo/src/controller/controller.h`
- `plugins/disrumpo/src/controller/controller.cpp`

**Implementation:**
- Inherit from `Steinberg::Vst::EditControllerEx1`
- Register 3 parameters in initialize()
- setComponentState: sync from processor state
- createView: return nullptr (no UI)

**Acceptance criteria:**
- Parameters appear in DAW automation
- No UI opens (createView returns nullptr)

### T1.5: State Serialization (8h)

**Implementation in processor.cpp:**
- getState: write version + 3 float parameters
- setState: read version, handle migration, read parameters

**Version handling:**
- version > kPresetVersion: load what we understand
- version < kPresetVersion: apply defaults for missing
- Read fails: return kResultFalse, use defaults

**Acceptance criteria:**
- Save/load project preserves parameter values
- Corrupted state loads defaults without crash

### T1.6: Entry Point & DAW Verification (4h)

**File to create:**
- `plugins/disrumpo/src/entry.cpp`

**Verification:**
- Plugin loads in Reaper
- Plugin loads in Ableton Live (if available)
- pluginval level 1 passes

**Acceptance criteria:**
- Plugin appears in DAW scanner
- Plugin instantiates without crash
- pluginval --strictness-level 1 passes

---

## Files Created by This Plan

| File | Purpose |
|------|---------|
| `plugins/disrumpo/CMakeLists.txt` | Build configuration |
| `plugins/disrumpo/version.json` | Version metadata |
| `plugins/disrumpo/src/entry.cpp` | Plugin factory |
| `plugins/disrumpo/src/plugin_ids.h` | FUIDs and parameter IDs |
| `plugins/disrumpo/src/version.h.in` | Version header template |
| `plugins/disrumpo/src/processor/processor.h` | Processor declaration |
| `plugins/disrumpo/src/processor/processor.cpp` | Processor implementation |
| `plugins/disrumpo/src/controller/controller.h` | Controller declaration |
| `plugins/disrumpo/src/controller/controller.cpp` | Controller implementation |
| `plugins/disrumpo/resources/win32resource.rc.in` | Windows resources |

## Root CMakeLists.txt Modification

Add to root CMakeLists.txt after the Iterum plugin:

```cmake
# ==============================================================================
# Plugins
# ==============================================================================
add_subdirectory(plugins/iterum)
add_subdirectory(plugins/disrumpo)  # ADD THIS LINE
```

---

## Verification Checklist (Milestone M1)

- [ ] Plugin appears in DAW plugin scanner within 5 seconds
- [ ] Plugin instantiates without crash within 2 seconds
- [ ] Audio passes through (bit-transparent)
- [ ] State save/load completes within 10ms
- [ ] Plugin passes pluginval strictness level 1
- [ ] Parameter values restored on project reload
