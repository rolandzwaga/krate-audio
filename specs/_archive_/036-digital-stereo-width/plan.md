# Implementation Plan: Digital Delay Stereo Width Control

**Branch**: `036-digital-stereo-width` | **Date**: 2025-12-29 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/036-digital-stereo-width/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/commands/plan.md` for the execution workflow.

## Summary

Add a stereo width parameter (0-200%) to the Digital Delay feature, matching the implementation already present in PingPong Delay. Includes full VST3 parameter system (ID, registration, handling), UI control (slider in editor.uidesc), DSP processing (Mid/Side width adjustment), and state persistence.

**Technical Approach**:

*VST3 Parameter System*:
- Add parameter ID `kDigitalWidthId = 612` to plugin_ids.h (Digital range: 600-699)
- Register parameter in Controller::initialize() with RangeParameter (0-200%, default 100%)
- Handle parameter changes in Processor::processParameterChanges() (denormalize to 0-200%)
- Add to state save/load in both Processor::getState/setState and Controller::getState/setComponentState

*UI Control*:
- Add horizontal slider control with tag="612" to DigitalPanel template in editor.uidesc
- Position in second row after DigitalOutputLevel control (approximately x=640, y=103)
- Control binding handled automatically via VSTGUI tag matching

*DSP Processing*:
- Add `std::atomic<float> width{100.0f}` to DigitalParams struct in digital_params.h
- Add OnePoleSmoother member to DigitalDelay class for 20ms smoothing
- Apply M/S processing in DigitalDelay::process() after delay line output
- Reuse M/S logic pattern from StereoField::processStereo()

*Reference Implementation*:
- Follow PingPongParams pattern exactly (same range, default, M/S approach)

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK, VSTGUI (existing dependencies)
**Storage**: Plugin state (IBStreamer for state persistence)
**Testing**: Catch2 (unit tests for parameter handling, M/S processing) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - VST3 plugin
**Project Type**: Single VST3 plugin project
**Performance Goals**: Real-time audio processing (<0.5% CPU per delay mode instance)
**Constraints**: No allocations in audio thread, parameter smoothing to prevent clicks, cross-platform compatibility
**Scale/Scope**: Single parameter addition to existing Digital Delay mode (1 of 9 delay modes)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] Width parameter will use atomic storage (std::atomic<float>)
- [x] Width smoother will be pre-allocated in prepare()
- [x] No allocations in process() path
- [x] M/S processing uses stack-local variables only

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created
- [x] Reusing existing MidSideProcessor or StereoField M/S logic

**Required Check - Principle VI (Cross-Platform Compatibility):**
- [x] Parameter handling follows cross-platform VST3 patterns
- [x] M/S math uses standard floating-point (no platform-specific intrinsics)
- [x] State persistence uses explicit byte order (little-endian)

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

This section prevents One Definition Rule (ODR) violations by documenting existing components that may be reused or would conflict with new implementations.

### Mandatory Searches Performed

**Classes/Structs to be created**: None (extending existing DigitalParams struct)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| DigitalParams | `grep -r "struct DigitalParams" src/` | Yes | **EXTEND** - Add width member to existing struct |

**Utility Functions to be created**: None (reusing existing M/S processing)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| M/S encoding | `grep -r "mid.*side" src/dsp/` | Yes | stereo_field.h, midside_processor.h | **REUSE** existing logic |
| Width application | `grep -r "setWidth" src/dsp/` | Yes | stereo_field.h line 142 | **REUSE** existing pattern |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Parameter smoothing (20ms) for width changes |
| MidSideProcessor | dsp/processors/midside_processor.h | 2 | M/S encoding/decoding (reference for algorithm) |
| StereoField::processStereo | dsp/systems/stereo_field.h lines 485-538 | 3 | Reference implementation for M/S width processing |
| PingPongParams::width | parameters/pingpong_params.h line 35 | - | Pattern for parameter range (0-200%), default (100%) |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No width-related utilities
- [x] `src/dsp/core/` - No stereo width utilities at Layer 0
- [x] `src/dsp/processors/midside_processor.h` - Provides M/S encoding/decoding
- [x] `src/dsp/systems/stereo_field.h` - Provides reference M/S width logic
- [x] `src/parameters/pingpong_params.h` - Reference for width parameter pattern
- [x] `src/parameters/digital_params.h` - Target struct to extend
- [x] `ARCHITECTURE.md` - Component inventory (if exists)

### ODR Risk Assessment

**Risk Level**: **LOW**

**Justification**:
- No new classes or global functions being created
- Only extending existing DigitalParams struct with one new member
- Reusing well-established M/S processing logic from StereoField
- Following exact pattern from PingPongParams (proven reference implementation)
- All utility functions (smoothing, M/S math) already exist in codebase

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins. Prevents compile-time API mismatch errors.*

This section documents the **exact API signatures** of all dependencies that will be called.

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| DigitalParams | width | `std::atomic<float> width{100.0f};` | ✓ (to be added) |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | ✓ |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | ✓ |
| OnePoleSmoother | process | `float process() noexcept` | ✓ |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | ✓ |

### Header Files Read

- [x] `src/dsp/primitives/smoother.h` - OnePoleSmoother class API
- [x] `src/dsp/systems/stereo_field.h` - M/S width processing reference (lines 485-538)
- [x] `src/dsp/processors/midside_processor.h` - M/S encoding/decoding reference
- [x] `src/parameters/pingpong_params.h` - Width parameter pattern reference
- [x] `src/parameters/digital_params.h` - Target struct to extend

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `widthSmoother_.snapTo(value)` |
| Width range | Stored as 0.0-200.0, not 0.0-2.0 | `std::atomic<float> width{100.0f};` (not 1.0f) |
| M/S formula | Width factor is `width / 100.0f` | `const float widthFactor = width / 100.0f;` |
| Parameter default | PingPong uses 0.5 normalized (100%) | `0.5` → default 100% width |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

See CLAUDE.md "Layer 0 Refactoring Analysis" for decision framework.

### Utilities to Extract to Layer 0

**None identified.** All M/S processing utilities already exist in Layer 2 (MidSideProcessor) and Layer 3 (StereoField).

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| M/S width application | Already exists in StereoField::processStereo - will inline in DigitalDelay::process() |
| Width conversion (%) | One-liner: `width / 100.0f` - not worth extracting |

**Decision**: No new Layer 0 utilities needed. All M/S processing logic already exists and will be reused inline in DigitalDelay::process().

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 4 - User Features (DigitalDelay)

**Related features at same layer** (from ROADMAP.md or known plans):
- Tape Delay (future candidate for width control)
- BBD Delay (future candidate for width control)
- Reverse Delay (future candidate for width control)
- Multi-Tap Delay (might want per-tap width control)
- Freeze Delay (future candidate for width control)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Width parameter pattern | **HIGH** | All delay modes (Tape, BBD, Reverse, etc.) | **Keep local** - extract after 2nd use |
| M/S width processing inline code | **MEDIUM** | Delay modes that add width | **Keep local** - pattern is simple (5 lines) |

### Detailed Analysis (for HIGH potential items)

**Width Parameter Pattern** provides:
- Parameter struct member: `std::atomic<float> width{100.0f};`
- Parameter registration: `STR16("Digital Width"), STR16("%"), 0, 0.5, kCanAutomate, kDigitalWidthId`
- Parameter handling: `params.width.store(static_cast<float>(normalizedValue * 200.0), std::memory_order_relaxed);`
- Formatting: `"%.0f%%"` display
- State persistence: `streamer.writeFloat(width)` / `streamer.readFloat(floatVal)`

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Tape Delay | **YES** | Same width range/behavior needed |
| BBD Delay | **YES** | Analog delays benefit from width control |
| Reverse Delay | **YES** | Width control applies to reversed output |
| Multi-Tap Delay | **MAYBE** | Might want per-tap width vs global width |

**Recommendation**: **Keep in digital_params.h for now.** After implementing width for a second delay mode (e.g., Tape), extract the common pattern to a shared parameter helper if the pattern remains identical.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared parameter utilities yet | First delay mode to add width (PingPong already has it) - wait for 2nd use |
| Keep M/S logic inline | Simple 5-line formula, not worth abstracting until proven need |

### Review Trigger

After implementing **Tape Delay width control**, review this section:
- [ ] Does Tape need identical width parameter pattern? → Extract to `delay_width_params.h` helper
- [ ] Does Tape use same M/S formula? → Keep inline (it's only 5 lines)
- [ ] Any duplicated parameter handling code? → Consider shared formatting/persistence utilities

## Project Structure

### Documentation (this feature)

```text
specs/036-digital-stereo-width/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output (NONE NEEDED - no research required)
├── data-model.md        # Phase 1 output (NONE NEEDED - simple parameter addition)
├── quickstart.md        # Phase 1 output (NONE NEEDED - no new API)
├── contracts/           # Phase 1 output (NONE NEEDED - parameter only)
├── checklists/          # Quality checklists
│   └── requirements.md  # Spec quality validation (already complete)
└── tasks.md             # Phase 2 output (/speckit.tasks command - NOT created by /speckit.plan)
```

**Note**: This is a simple parameter addition feature. No research, data modeling, or API contracts are needed. Implementation can proceed directly to tasks.md generation.

### Source Code (repository root)

```text
src/
├── plugin_ids.h                  # ADD: kDigitalWidthId = 612 parameter ID
├── parameters/
│   └── digital_params.h          # ADD: std::atomic<float> width{100.0f} member
├── controller/
│   └── controller.cpp            # MODIFY: Register width parameter in initialize()
│                                 # MODIFY: Add width to getState/setComponentState
├── processor/
│   └── processor.cpp             # MODIFY: Handle width in processParameterChanges()
│                                 # MODIFY: Add width to getState/setState
├── dsp/
│   └── features/
│       └── digital_delay.h       # MODIFY: Add OnePoleSmoother widthSmoother_ member
│       └── digital_delay.cpp     # MODIFY: Apply M/S width processing in process()
└── resources/
    └── editor.uidesc             # ADD: Width slider control in DigitalPanel template
                                  # ADD: Control tag entry for "DigitalWidth" (612)

tests/
└── unit/
    └── vst/
        ├── digital_width_param_test.cpp      # ADD: Parameter registration/handling tests
        └── digital_width_processing_test.cpp # ADD: M/S processing tests
```

**Structure Decision**: Single project (VST3 plugin). All changes are additions/modifications to existing files in the established structure. No new directories needed.

**File Count**:
- Modified: 6 files (plugin_ids.h, digital_params.h, controller.cpp, processor.cpp, digital_delay.h/.cpp, editor.uidesc)
- Created: 2 test files (digital_width_param_test.cpp, digital_width_processing_test.cpp)

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

**No violations** - all constitutional requirements are met.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

---

## Phase 0: Research (SKIPPED)

**Rationale for Skipping**: This is a straightforward parameter addition following an established pattern (PingPongParams.width). All technical questions are answered by the existing reference implementation:

- **How to implement M/S width processing?** → Reference: StereoField::processStereo (lines 529-537)
- **What range/default for width parameter?** → Reference: PingPongParams (0-200%, default 100%)
- **How to smooth the parameter?** → Reference: OnePoleSmoother with 20ms smoothing time
- **How to persist the parameter?** → Reference: PingPongParams state save/load pattern

No research.md needed - proceeding directly to implementation tasks.

---

## Phase 1: Data Model & Contracts (SKIPPED)

**Rationale for Skipping**: This feature adds a single parameter to an existing delay mode. No new data entities, no API contracts, no external integrations.

### Data Model Analysis

**Existing Entity**: `DigitalParams` struct (src/parameters/digital_params.h line 28)

**Modification**: Add one member:
```cpp
std::atomic<float> width{100.0f};  // 0-200%
```

No data-model.md needed - the "data model" is a single atomic float member.

### API Contracts Analysis

**No external API contracts** - parameter handling is internal to the plugin. The VST3 host interface remains unchanged.

No contracts/ directory needed - this is an internal parameter addition.

### Quickstart Analysis

**No user-facing API changes** - users interact via the existing VST3 parameter automation interface.

No quickstart.md needed - parameter usage is standard VST automation.

---

## Phase 2: Task Generation (Next Step)

Use `/speckit.tasks` command to generate tasks.md with implementation steps.

Expected task structure:
1. **Pre-Implementation**: Verify TESTING-GUIDE.md in context
2. **VST3 Parameter System**:
   - Add kDigitalWidthId = 612 to plugin_ids.h
   - Add control tag entry "DigitalWidth" (tag="612") to editor.uidesc
   - Add std::atomic<float> width{100.0f} to DigitalParams struct
   - Register parameter in Controller::initialize() (RangeParameter, 0-200%, default 100%)
   - Handle parameter in Processor::processParameterChanges() (denormalize to 0-200%)
   - Add width to Controller::getState/setComponentState
   - Add width to Processor::getState/setState
3. **UI Control**:
   - Add horizontal slider control to DigitalPanel template in editor.uidesc
   - Position control in second row after OutputLevel (x≈640, y=103)
   - Verify VSTGUI tag binding (automatic via tag="612")
4. **DSP Processing**:
   - Add OnePoleSmoother widthSmoother_ member to DigitalDelay class
   - Configure smoother in prepare() (20ms smoothing time)
   - Apply M/S width processing in DigitalDelay::process()
   - Snap smoother in reset()
5. **Testing**:
   - Write parameter registration tests (vst_tests)
   - Write parameter handling tests (vst_tests)
   - Write M/S processing tests (dsp_tests)
   - Write state persistence tests (vst_tests)
6. **Manual Verification**: Build plugin, test UI control, verify automation

---

## Summary

This implementation plan identifies a **low-complexity, low-risk** feature addition:

✅ **No ODR risks** - extending existing struct, reusing existing utilities
✅ **Reference implementation exists** - PingPongParams provides exact pattern to follow
✅ **No research needed** - M/S processing already well-understood in codebase
✅ **No new abstractions** - wait for 2nd delay mode to extract shared utilities
✅ **Constitutional compliance** - real-time safety, test-first, cross-platform

**Ready for**: `/speckit.tasks` to generate implementation task list.
