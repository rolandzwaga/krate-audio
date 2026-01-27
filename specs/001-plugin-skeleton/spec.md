# Feature Specification: Disrumpo Plugin Skeleton

**Feature Branch**: `001-plugin-skeleton`
**Created**: 2026-01-27
**Status**: Clarified (Ready for Planning)
**Input**: User description: "Foundational plugin skeleton for Disrumpo multiband morphing distortion plugin - Week 1 tasks T1.1-T1.6 from roadmap"

## Overview

This specification covers the foundational plugin skeleton for Disrumpo, a multiband morphing distortion VST3 plugin. This is the first implementation spec that establishes the core plugin infrastructure following the monorepo patterns established by Iterum.

**Scope**: Milestone M1 from roadmap.md - Plugin loads in DAW and passes pluginval level 1

**Related Documents**:
- [specs/Disrumpo/spec.md](../Disrumpo/spec.md) - Main requirements specification
- [specs/Disrumpo/roadmap.md](../Disrumpo/roadmap.md) - Full development roadmap
- [specs/Disrumpo/dsp-details.md](../Disrumpo/dsp-details.md) - DSP implementation details (parameter ID encoding)
- [specs/Disrumpo/vstgui-implementation.md](../Disrumpo/vstgui-implementation.md) - VSTGUI specifications
- [plugins/iterum/](../../plugins/iterum/) - Reference implementation

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Plugin Loads in DAW (Priority: P1)

A music producer wants to add Disrumpo to their project. They open their DAW (Reaper, Ableton, etc.), scan for plugins, and Disrumpo appears in the plugin list. They insert Disrumpo on a track and it loads without crashing.

**Why this priority**: Without the ability to load in a DAW, the plugin is unusable. This is the absolute minimum viable product.

**Independent Test**: Load the plugin in Reaper and Ableton Live. Plugin appears in scanner and instantiates without crash.

**Acceptance Scenarios**:

1. **Given** a fresh DAW installation with Disrumpo.vst3 in the VST3 folder, **When** the user triggers a plugin scan, **Then** Disrumpo appears in the plugin list under "Distortion" category.
2. **Given** the plugin is in the plugin list, **When** the user inserts Disrumpo on a stereo track, **Then** the plugin instantiates without error within 2 seconds.
3. **Given** the plugin is loaded, **When** the user closes and reopens the project, **Then** the plugin reloads without error.

---

### User Story 2 - Audio Passthrough (Priority: P2)

A music producer has Disrumpo loaded on a track. In its initial state (no processing applied), audio should pass through cleanly. This confirms the audio bus configuration is correct.

**Why this priority**: Audio passthrough validates the audio processing pipeline works correctly before any DSP is added.

**Independent Test**: Play audio through the plugin and verify output matches input (bit-transparent bypass).

**Acceptance Scenarios**:

1. **Given** Disrumpo is loaded on a stereo track with audio, **When** audio plays through, **Then** the output is identical to the input (bit-transparent).
2. **Given** Disrumpo is loaded, **When** processing 32-bit floating point audio at 44.1kHz, 48kHz, or 96kHz, **Then** audio passes through without artifacts.

---

### User Story 3 - Project State Persistence (Priority: P3)

A music producer has Disrumpo loaded with specific parameter settings. They save their project, close the DAW, reopen the project, and all Disrumpo settings are restored exactly as they were.

**Why this priority**: State persistence is essential for professional use but requires the parameter system to be functional first.

**Independent Test**: Save project with known parameter values, reload, and verify parameter values match.

**Acceptance Scenarios**:

1. **Given** Disrumpo is loaded with non-default parameter values, **When** the user saves and reloads the project, **Then** all parameter values are restored exactly.
2. **Given** Disrumpo is loaded, **When** the host requests the plugin state, **Then** the state is returned within 10ms.

---

### User Story 4 - Plugin Validation (Priority: P4)

A plugin developer wants to verify that Disrumpo meets basic VST3 compliance requirements. They run pluginval against the plugin and it passes level 1 validation.

**Why this priority**: Pluginval compliance ensures broad DAW compatibility and catches common implementation errors.

**Independent Test**: Run pluginval --strictness-level 1 --validate Disrumpo.vst3

**Acceptance Scenarios**:

1. **Given** a built Disrumpo.vst3 binary, **When** running pluginval at strictness level 1, **Then** all tests pass with no failures.

---

### Edge Cases

- What happens when the host requests an unsupported bus arrangement? Plugin rejects gracefully and offers stereo-only.
- What happens when setState is called with corrupted data? Plugin loads safe defaults without crashing.
- What happens when the plugin is loaded at extreme sample rates (8kHz or 384kHz)? Plugin handles gracefully without crash.

---

## Requirements *(mandatory)*

### Functional Requirements

#### CMake Project Structure (T1.1)

- **FR-001**: Plugin MUST be buildable using CMake 3.20+ following the monorepo pattern (plugins/disrumpo/).
- **FR-002**: Plugin MUST link against the shared KrateDSP library for future DSP component access.
- **FR-003**: Plugin MUST link against VST3 SDK and VSTGUI support libraries.
- **FR-004**: Plugin version MUST be read from version.json (single source of truth).

#### Parameter ID Encoding System (T1.2)

- **FR-005**: Plugin MUST define unique FUIDs for Processor and Controller components (distinct from Iterum).
- **FR-006**: Plugin MUST use the bit-encoded parameter ID scheme defined in [dsp-details.md](../Disrumpo/dsp-details.md):
  - Global parameters: `0x0Fxx` (band = 0xF reserved for global)
  - Sweep parameters: `0x0Exx` (band = 0xE reserved for sweep)
  - Per-band parameters: `makeBandParamId(bandIndex, paramType)` → `(0xF << 12) | (band << 8) | param`
  - Per-node parameters: `makeNodeParamId(bandIndex, nodeIndex, paramType)` → `(node << 12) | (band << 8) | param`
  - This encoding supports 8 bands × 4 nodes × 16 param types = 512 unique per-band parameters plus 256 globals
- **FR-007**: Plugin MUST implement skeleton parameter IDs (see [data-model.md](data-model.md) for value interpretation):
  - `kInputGainId = 0x0F00` (GlobalInputGain) - normalized 0.0-1.0, default 0.5 (0 dB)
  - `kOutputGainId = 0x0F01` (GlobalOutputGain) - normalized 0.0-1.0, default 0.5 (0 dB)
  - `kGlobalMixId = 0x0F02` (GlobalMix) - normalized 0.0-1.0, default 1.0 (100% wet)
- **FR-008**: Plugin subcategory MUST be "Distortion" (VST3 standard: `Steinberg::Vst::PlugType::kFxDistortion`). This determines the plugin's category in DAW plugin browsers.

#### Processor Skeleton (T1.3)

- **FR-009**: Processor MUST implement IAudioProcessor interface with stereo input/output bus configuration.
- **FR-010**: Processor MUST implement setBusArrangements() accepting stereo (2 channels) only. Non-stereo arrangements MUST return kResultFalse (the host will then fall back to the default stereo arrangement).
- **FR-011**: Processor MUST implement setupProcessing() for sample rate and block size initialization.
- **FR-012**: Processor MUST implement process() with bit-transparent audio passthrough (output = input, no gain/mix applied in skeleton). Parameters are registered but have no effect on audio until DSP processing is added in future specs.
- **FR-013**: Processor MUST correctly handle 32-bit floating point audio at common sample rates (44.1kHz, 48kHz, 96kHz).

#### Controller Skeleton (T1.4)

- **FR-014**: Controller MUST implement IEditController interface.
- **FR-015**: Controller MUST register the skeleton parameters (GlobalInputGain, GlobalOutputGain, GlobalMix) in initialize().
- **FR-016**: Controller MUST implement setComponentState() to sync from processor state.
- **FR-017**: Controller MUST implement createView() returning nullptr (no UI in skeleton).

#### State Serialization (T1.5)

- **FR-018**: Processor MUST implement getState() to serialize all registered parameter values to IBStream using standard VST3 preset format (per [dsp-details.md](../Disrumpo/dsp-details.md) Section 2).
- **FR-019**: Processor MUST implement setState() to deserialize parameter values from IBStream.
- **FR-020**: State serialization MUST include a version field (int32) as first value for future migration (kPresetVersion = 1 for initial release).
- **FR-021**: State serialization MUST handle corrupted/invalid data by loading safe defaults without crash:
  - If version > kPresetVersion: load what we understand, skip unknown
  - If version < kPresetVersion: apply defaults for missing parameters
  - If read fails: return kResultFalse, plugin uses default values

#### DAW Verification (T1.6)

- **FR-022**: Plugin MUST be loadable in Reaper without crash.
- **FR-023**: Plugin MUST be loadable in Ableton Live without crash.
- **FR-024**: Plugin MUST pass pluginval at strictness level 1.

### Key Entities

- **Processor**: The audio processing component, handles real-time audio I/O and parameter state.
- **Controller**: The edit controller component, handles parameter registration and future UI.
- **Parameter ID**: Unique identifier for each automatable parameter, organized by range allocation.
- **State Stream**: Binary representation of all parameter values for project save/load.

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Plugin appears in DAW plugin scanner within 5 seconds of scan initiation.
- **SC-002**: Plugin instantiates without crash within 2 seconds of insertion.
- **SC-003**: Audio passes through with bit-transparent output (zero deviation from input).
- **SC-004**: State save/load completes within 10ms for all parameters.
- **SC-005**: Plugin passes pluginval strictness level 1 with zero failures.
- **SC-006**: Project reload restores 100% of saved parameter values correctly.

---

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- VST3 SDK and VSTGUI are available via the monorepo's extern/vst3sdk/ directory.
- KrateDSP library is built and linkable (already established for Iterum).
- Build system supports Windows (primary), with macOS/Linux to be validated in later specs.
- The plugin directory structure at plugins/disrumpo/ exists (confirmed present).

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| Disrumpo dsp-details.md | specs/Disrumpo/dsp-details.md | **Primary reference** for parameter ID encoding scheme (bit-encoded) and state serialization format |
| Iterum plugin_ids.h | plugins/iterum/src/plugin_ids.h | Reference implementation for FUID definition pattern (but NOT parameter ID scheme - Disrumpo uses different encoding) |
| Iterum entry.cpp | plugins/iterum/src/entry.cpp | Reference implementation for plugin factory registration |
| Iterum Processor | plugins/iterum/src/processor/processor.h | Reference for VST3 Processor implementation patterns |
| Iterum Controller | plugins/iterum/src/controller/controller.h | Reference for VST3 Controller implementation patterns |
| Iterum CMakeLists.txt | plugins/iterum/CMakeLists.txt | Reference for CMake target configuration |
| version.h.in template | plugins/iterum/src/version.h.in | Reference for version header generation |

**Search Results Summary**: The Iterum plugin provides reference implementations for skeleton structure, but **parameter ID encoding differs**: Disrumpo uses a bit-encoded scheme (band/node indices in upper bits) per dsp-details.md, not Iterum's sequential 100-gap scheme.

**Note**: For detailed ODR analysis and API contract verification, see [plan.md](plan.md) Section "Codebase Research (Principle XIV - ODR Prevention)".

### Forward Reusability Consideration

*Note for planning phase: This skeleton establishes patterns that all future Disrumpo specs will build upon.*

**Sibling features at same layer** (if known):
- 002-band-management (Week 2) - Will extend Processor with crossover network
- 003-distortion-integration (Week 3) - Will extend Processor with distortion adapter

**Potential shared components** (preliminary, refined in plan.md):
- Parameter ID encoding helpers (getBandParamId, getNodeParamId) - skeleton defines the scheme, Week 4 implements helpers
- State serialization versioning - established here, used by all future state updates

---

## File Structure

The following files will be created or modified:

```
plugins/disrumpo/
+-- CMakeLists.txt                    # New: Plugin build configuration
+-- version.json                      # New: Version metadata (single source of truth)
+-- src/
|   +-- entry.cpp                     # New: Plugin factory registration
|   +-- plugin_ids.h                  # New: FUIDs and parameter ID definitions
|   +-- version.h.in                  # New: Version header template
|   +-- version.h                     # Generated: Version header
|   +-- processor/
|   |   +-- processor.h               # New: Processor class declaration
|   |   +-- processor.cpp             # New: Processor implementation
|   +-- controller/
|       +-- controller.h              # New: Controller class declaration
|       +-- controller.cpp            # New: Controller implementation
+-- resources/
    +-- win32resource.rc.in           # New: Windows resource template
```

---

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 |   |   |
| FR-002 |   |   |
| FR-003 |   |   |
| FR-004 |   |   |
| FR-005 |   |   |
| FR-006 |   |   |
| FR-007 |   |   |
| FR-008 |   |   |
| FR-009 |   |   |
| FR-010 |   |   |
| FR-011 |   |   |
| FR-012 |   |   |
| FR-013 |   |   |
| FR-014 |   |   |
| FR-015 |   |   |
| FR-016 |   |   |
| FR-017 |   |   |
| FR-018 |   |   |
| FR-019 |   |   |
| FR-020 |   |   |
| FR-021 |   |   |
| FR-022 |   |   |
| FR-023 |   |   |
| FR-024 |   |   |
| SC-001 |   |   |
| SC-002 |   |   |
| SC-003 |   |   |
| SC-004 |   |   |
| SC-005 |   |   |
| SC-006 |   |   |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
