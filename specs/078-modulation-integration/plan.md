# Implementation Plan: Arpeggiator Modulation Integration

**Branch**: `078-modulation-integration` | **Date**: 2026-02-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/078-modulation-integration/spec.md`

## Summary

Expose 5 arpeggiator parameters (rate, gate length, octave range, swing, spice) as modulation destinations in the existing ModulationEngine. This is a pure integration feature -- wiring existing systems together. No new DSP components, no new parameter IDs, and no state serialization changes are needed. The implementation adds 5 enum values to `RuinaeModDest`, extends the UI destination registry from 10 to 15 entries, extends the controller's param ID mapping array, and modifies `Processor::applyParamsToEngine()` to read modulation offsets and compute effective parameter values before calling ArpeggiatorCore setters.

## Technical Context

**Language/Version**: C++20 (MSVC 2022, Clang 15+, GCC 10+)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (in-tree shared library)
**Storage**: N/A (no new serialization; existing mod matrix serialization handles new destinations automatically)
**Testing**: Catch2 (unit tests via `ruinae_tests` target) *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows, macOS, Linux (cross-platform VST3)
**Project Type**: Monorepo VST3 plugin
**Performance Goals**: Zero additional allocations on audio thread; block-rate mod offset reads (5 float lookups per block)
**Constraints**: Real-time safety (Constitution Principle II); 1-block modulation latency accepted per spec
**Scale/Scope**: 4 files modified, ~80 lines of production code, ~300 lines of test code

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] No cross-include between processor and controller headers
- [x] kGlobalDestParamIds is controller-side only (indicator routing); DSP uses RuinaeModDest enum directly
- [x] Processor reads mod offsets from engine (audio thread); controller maps param IDs for UI display

**Principle II (Real-Time Audio Thread Safety):**
- [x] All mod offset reads are array lookups (getGlobalModOffset is a single array index)
- [x] All math is inline arithmetic (multiply, add, clamp, round) -- zero allocations
- [x] No locks, exceptions, or I/O in the modulation application path

**Principle VIII (Testing Discipline):**
- [x] Tests will be written BEFORE implementation code (test-first)
- [x] All existing tests must pass after changes
- [x] No pre-existing failures dismissed

**Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created
- [x] No new classes or structs introduced -- only enum extensions and inline code changes

**Principle XVI (Honest Completion):**
- [x] Compliance table will be filled from actual code and test output, not memory

**Post-Design Re-Check:**
- [x] No constitution violations in the design. All changes are to existing files.
- [x] The feature adds no new classes, no new parameter IDs, no new serialization fields.
- [x] All code paths are real-time safe (array lookups + arithmetic only).

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: NONE. This feature creates no new types.

**Utility Functions to be created**: NONE. All logic is inline within `applyParamsToEngine()`.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `RuinaeModDest` enum | `plugins/ruinae/src/engine/ruinae_engine.h:66-77` | Plugin | Extended with 5 new entries (ArpRate=74..ArpSpice=78) |
| `RuinaeEngine::getGlobalModOffset()` | `plugins/ruinae/src/engine/ruinae_engine.h:473-475` | Plugin | Read mod offsets for arp destinations in processor |
| `ModulationEngine::getModulationOffset()` | `dsp/include/krate/dsp/systems/modulation_engine.h:276-281` | Layer 3 | Backend for getGlobalModOffset(); returns float from modOffsets_ array |
| `kGlobalDestNames` | `plugins/shared/src/ui/mod_matrix_types.h:161-172` | Shared | Extended with 5 arp destination display names |
| `kNumGlobalDestinations` | `plugins/shared/src/ui/mod_matrix_types.h:64` | Shared | Updated from 10 to 15 |
| `kGlobalDestParamIds` | `plugins/ruinae/src/controller/controller.cpp:114-125` | Plugin | Extended with 5 arp param ID mappings |
| `kModDestCount` | `plugins/ruinae/src/parameters/dropdown_mappings.h:180` | Plugin | Already aliases kNumGlobalDestinations; updates automatically |
| `modDestFromIndex()` | `plugins/ruinae/src/parameters/dropdown_mappings.h:205-208` | Plugin | Already maps index to RuinaeModDest via GlobalFilterCutoff + index; works for indices 0-14 after enum extension |
| `ArpeggiatorCore` setters | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | Layer 2 | `setFreeRate()`, `setGateLength()`, `setOctaveRange()`, `setSwing()`, `setSpice()` -- no changes needed |
| `ArpeggiatorParams` | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Plugin | Base parameter values read from atomics -- no changes needed |
| `dropdownToDelayMs()` | `dsp/include/krate/dsp/core/note_value.h:240-246` | Layer 0 | Used for tempo-sync step duration computation |
| `getNoteValueFromDropdown()` | `dsp/include/krate/dsp/core/note_value.h` | Layer 0 | Used for tempo-sync NoteValue mapping |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - ArpeggiatorCore (unchanged)
- [x] `dsp/include/krate/dsp/systems/modulation_engine.h` - ModulationEngine (unchanged)
- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - RuinaeModDest enum (extended)
- [x] `plugins/shared/src/ui/mod_matrix_types.h` - Destination registry (extended)
- [x] `plugins/ruinae/src/controller/controller.cpp` - Param ID mapping (extended)
- [x] `plugins/ruinae/src/processor/processor.cpp` - Mod offset application (modified)
- [x] No `RuinaeModDest::Arp*` symbols found anywhere in codebase (grep verified)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new types are created. The only changes are extending an enum (adding values to the end), extending constexpr arrays (appending entries), and modifying inline code in processor.cpp. There is zero ODR risk.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `RuinaeEngine` | `getGlobalModOffset` | `[[nodiscard]] float getGlobalModOffset(RuinaeModDest dest) const noexcept` | Yes |
| `ModulationEngine` | `getModulationOffset` | `[[nodiscard]] float getModulationOffset(uint32_t destParamId) const noexcept` | Yes |
| `ArpeggiatorCore` | `setFreeRate` | `inline void setFreeRate(float hz) noexcept` | Yes |
| `ArpeggiatorCore` | `setGateLength` | `inline void setGateLength(float percent) noexcept` | Yes |
| `ArpeggiatorCore` | `setOctaveRange` | `inline void setOctaveRange(int octaves) noexcept` | Yes |
| `ArpeggiatorCore` | `setSwing` | `inline void setSwing(float percent) noexcept` | Yes |
| `ArpeggiatorCore` | `setSpice` | `void setSpice(float value) noexcept` | Yes |
| `ArpeggiatorCore` | constants | `kMinFreeRate=0.5f, kMaxFreeRate=50.0f, kMinGateLength=1.0f, kMaxGateLength=200.0f, kMinSwing=0.0f, kMaxSwing=75.0f` | Yes |
| `dropdownToDelayMs` | function | `[[nodiscard]] inline constexpr float dropdownToDelayMs(int dropdownIndex, double tempoBPM) noexcept` | Yes |
| `ArpeggiatorParams` | `freeRate` | `std::atomic<float> freeRate{4.0f}` | Yes |
| `ArpeggiatorParams` | `gateLength` | `std::atomic<float> gateLength{80.0f}` | Yes |
| `ArpeggiatorParams` | `octaveRange` | `std::atomic<int> octaveRange{1}` | Yes |
| `ArpeggiatorParams` | `swing` | `std::atomic<float> swing{0.0f}` | Yes |
| `ArpeggiatorParams` | `spice` | `std::atomic<float> spice{0.0f}` | Yes |
| `ArpeggiatorParams` | `tempoSync` | `std::atomic<bool> tempoSync{true}` | Yes |
| `ArpeggiatorParams` | `noteValue` | `std::atomic<int> noteValue{...}` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - ArpeggiatorCore class (setters, constants)
- [x] `dsp/include/krate/dsp/systems/modulation_engine.h` - ModulationEngine class (getModulationOffset, kMaxModDestinations=128)
- [x] `dsp/include/krate/dsp/core/note_value.h` - dropdownToDelayMs, getNoteValueFromDropdown
- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - RuinaeModDest, getGlobalModOffset
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - ArpeggiatorParams struct
- [x] `plugins/ruinae/src/processor/processor.h` - prevArpOctaveRange_ declaration
- [x] `plugins/shared/src/ui/mod_matrix_types.h` - kGlobalDestNames, kNumGlobalDestinations, static_asserts
- [x] `plugins/ruinae/src/controller/controller.cpp` - kGlobalDestParamIds array
- [x] `plugins/ruinae/src/parameters/dropdown_mappings.h` - modDestFromIndex, kModDestCount

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `ArpeggiatorCore::setSwing()` | Takes 0-75 percent as-is, NOT normalized 0-1 | `arpCore_.setSwing(percentValue)` |
| `ArpeggiatorCore::setOctaveRange()` | Triggers selector reset on value change; already guarded by `prevArpOctaveRange_` in processor | Must maintain the change-detection pattern |
| `ArpeggiatorCore::setSpice()` | Takes 0.0-1.0, NOT 0-100% | `arpCore_.setSpice(normalizedValue)` |
| `getGlobalModOffset()` | Returns previous block's offset (1-block latency) | Accepted per spec FR-007 |
| Tempo-sync rate | Step duration = `dropdownToDelayMs(noteIdx, tempoBPM) / 1000.0f` in seconds | The offset scales duration, not Hz |
| `kGlobalDestNames` array | Must be `constexpr` with exactly `kNumGlobalDestinations` entries | Enforced by static_assert at line 223 |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Mod offset application formulas | Inline in `applyParamsToEngine()`, single consumer, specific to arp-mod integration |

**Decision**: No Layer 0 extractions needed. All new code is inline arithmetic in processor.cpp.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Pure parameter mapping: read offset, compute effective value, call setter |
| **Data parallelism width** | 5 | 5 independent destinations, but each is a single scalar operation |
| **Branch density in inner loop** | LOW | One branch for tempo-sync vs free rate; rest is branchless |
| **Dominant operations** | Arithmetic | Multiply, add, clamp, round -- trivial cost |
| **Current CPU budget vs expected usage** | <<0.01% | 5 float lookups + 5 multiply-add-clamp per block (every ~1-10ms). Negligible. |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The modulation application is 5 independent scalar operations executed once per block (~100-1000Hz), not per sample. The total cost is approximately 30 floating-point operations per block. This is far below any threshold where SIMD optimization would be measurable, let alone beneficial.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip mod offset reads when arp disabled | Saves 5 array lookups per block when arp is off | LOW | YES (FR-015 permits this) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin integration layer (processor.cpp)

**Related features at same layer**:
- Phase 11: Arpeggiator UI (may need dynamic mode-aware indicator switching)
- Future: Effects chain parameter modulation destinations

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Mod offset application pattern | MEDIUM | Effects chain mod destinations | Keep inline; extract pattern to docs if 2nd consumer appears |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep mod application inline | Single consumer (arp section of applyParamsToEngine); pattern is straightforward |
| No helper function for scaling | Each destination has a unique formula; a generic helper would be over-abstracted |

## Project Structure

### Documentation (this feature)

```text
specs/078-modulation-integration/
├── spec.md              # Feature specification (input)
├── plan.md              # This file
├── research.md          # Phase 0 output (research findings)
├── data-model.md        # Phase 1 output (entity model)
├── quickstart.md        # Phase 1 output (implementation guide)
└── contracts/           # Phase 1 output (not applicable for this feature)
```

### Source Code (files modified)

```text
plugins/ruinae/src/engine/
└── ruinae_engine.h              # RuinaeModDest enum extension + static_assert

plugins/shared/src/ui/
└── mod_matrix_types.h           # kNumGlobalDestinations, kGlobalDestNames extension

plugins/ruinae/src/controller/
└── controller.cpp               # kGlobalDestParamIds extension

plugins/ruinae/src/processor/
└── processor.cpp                # Mod offset reading + application in applyParamsToEngine()

plugins/ruinae/tests/unit/processor/
└── arp_mod_integration_test.cpp # NEW: Unit tests for all 5 mod destinations
```

---

## Implementation Steps

### Step 0: Pre-Implementation Verification

1. Build the project to confirm a clean baseline:
   ```bash
   "$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
   ```
2. Run existing tests to confirm all pass:
   ```bash
   build/windows-x64-release/bin/Release/ruinae_tests.exe
   ```
3. Run existing arp integration tests specifically:
   ```bash
   build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp]"
   ```

### Step 1: Extend RuinaeModDest Enum (FR-001, FR-002, FR-020)

**File**: `plugins/ruinae/src/engine/ruinae_engine.h`
**Lines**: After `AllVoiceFilterEnvAmt = 73` (line 76)

Add 5 new enum values and the static_assert:

```cpp
enum class RuinaeModDest : uint32_t {
    GlobalFilterCutoff     = 64,
    GlobalFilterResonance  = 65,
    MasterVolume           = 66,
    EffectMix              = 67,
    AllVoiceFilterCutoff   = 68,
    AllVoiceMorphPosition  = 69,
    AllVoiceTranceGateRate = 70,
    AllVoiceSpectralTilt   = 71,
    AllVoiceResonance      = 72,
    AllVoiceFilterEnvAmt   = 73,
    // Arpeggiator modulation destinations (078-modulation-integration)
    ArpRate                = 74,  ///< Arp rate/speed modulation
    ArpGateLength          = 75,  ///< Arp gate length modulation
    ArpOctaveRange         = 76,  ///< Arp octave range modulation
    ArpSwing               = 77,  ///< Arp swing modulation
    ArpSpice               = 78   ///< Arp spice amount modulation
};

// FR-020: Validate linear mapping from UI index to enum value.
// modDestFromIndex() computes GlobalFilterCutoff + index, so ArpRate must be
// exactly GlobalFilterCutoff + 10 (since ArpRate is at UI index 10).
static_assert(static_cast<uint32_t>(RuinaeModDest::ArpRate) ==
              static_cast<uint32_t>(RuinaeModDest::GlobalFilterCutoff) + 10,
              "ArpRate enum value must equal GlobalFilterCutoff + 10 "
              "for modDestFromIndex() to work correctly");
```

**Verification**: FR-002 is satisfied because 74-78 < kMaxModDestinations (128).

### Step 2: Extend UI Destination Registry (FR-003, FR-004, FR-005)

**File**: `plugins/shared/src/ui/mod_matrix_types.h`

**Change 1** (line 64): Update constant:
```cpp
inline constexpr int kNumGlobalDestinations = 15;
```

**Change 2** (lines 161-172): Extend array from 10 to 15 entries:
```cpp
inline constexpr std::array<ModDestInfo, 15> kGlobalDestNames = {{
    {"Global Filter Cutoff",    "Global Flt Cutoff",     "GFCt"},
    {"Global Filter Resonance", "Global Flt Reso",       "GFRs"},
    {"Master Volume",           "Master Volume",         "Mstr"},
    {"Effect Mix",              "Effect Mix",            "FxMx"},
    {"All Voice Filter Cutoff", "All Voice Flt Cutoff",  "VFCt"},
    {"All Voice Morph Pos",     "All Voice Morph Pos",   "VMrp"},
    {"All Voice Gate Rate",     "All Voice Gate Rate",   "VGat"},
    {"All Voice Spectral Tilt", "All Voice Spectral Tilt", "VTlt"},
    {"All Voice Resonance",     "All Voice Resonance",   "VRso"},
    {"All Voice Flt Env Amt",   "All Voice Flt Env Amt", "VEnv"},
    // Arpeggiator destinations (078-modulation-integration)
    {"Arp Rate",                "Arp Rate",              "ARate"},
    {"Arp Gate Length",         "Arp Gate",              "AGat"},
    {"Arp Octave Range",        "Arp Octave",            "AOct"},
    {"Arp Swing",               "Arp Swing",             "ASwg"},
    {"Arp Spice",               "Arp Spice",             "ASpc"},
}};
```

**Verification**: The existing `static_assert` at line 223 (`kGlobalDestNames.size() == kNumGlobalDestinations`) will enforce that both the constant and array size match. FR-005 is automatically satisfied because `kModDestCount` in `dropdown_mappings.h:180` already references `kNumGlobalDestinations`.

### Step 3: Extend Controller Param ID Mapping (FR-006)

**File**: `plugins/ruinae/src/controller/controller.cpp`
**Lines**: 113-125

Update array from 10 to 15 entries:
```cpp
static constexpr std::array<Steinberg::Vst::ParamID,
    Krate::Plugins::kNumGlobalDestinations> kGlobalDestParamIds = {{
    kGlobalFilterCutoffId,    // 0: Global Filter Cutoff
    kGlobalFilterResonanceId, // 1: Global Filter Resonance
    kMasterGainId,            // 2: Master Volume
    kDelayMixId,              // 3: Effect Mix
    kFilterCutoffId,          // 4: All Voice Filter Cutoff
    kMixerPositionId,         // 5: All Voice Morph Position
    kTranceGateDepthId,       // 6: All Voice TranceGate Rate
    kMixerTiltId,             // 7: All Voice Spectral Tilt
    kFilterResonanceId,       // 8: All Voice Resonance
    kFilterEnvAmountId,       // 9: All Voice Filter Env Amount
    // Arpeggiator destinations (078-modulation-integration)
    kArpFreeRateId,           // 10: Arp Rate (always free-rate knob, see FR-006 limitation)
    kArpGateLengthId,         // 11: Arp Gate Length
    kArpOctaveRangeId,        // 12: Arp Octave Range
    kArpSwingId,              // 13: Arp Swing
    kArpSpiceId,              // 14: Arp Spice
}};
```

**Verification**: The existing `static_assert` at line 131 (`kGlobalDestParamIds.size() == kGlobalDestNames.size()`) will enforce size consistency.

### Step 4: Write Tests (Constitution Principle XIII -- Test-First)

**File**: `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp` (NEW)

This test file will use the same mock infrastructure pattern as `arp_integration_test.cpp` (mock IParameterChanges, mock IEventList). Tests will:

1. Create a Processor instance, initialize, setupProcessing
2. Set up mod matrix routing parameters (source, destination, amount) via mock IParameterChanges
3. Process multiple blocks to let the mod engine compute offsets
4. Verify the effective arp parameter values by observing ArpeggiatorCore behavior

**Test cases to implement (mapping to spec requirements)**:

| Test Case | FR/SC Coverage | What It Verifies |
|-----------|----------------|------------------|
| `ArpRateFreeMode_PositiveOffset` | FR-008, SC-006 | `effectiveRate = 4.0 * (1.0 + 0.5 * 1.0) = 6.0 Hz` (US1 scenario 1) |
| `ArpRateFreeMode_NegativeOffset` | FR-008, SC-006 | `effectiveRate = 4.0 * (1.0 - 0.5 * 1.0) = 2.0 Hz` (US1 scenario 2) |
| `ArpRateFreeMode_ZeroOffset` | FR-008, SC-005 | `effectiveRate = 4.0 Hz` exactly (US1 scenario 3) |
| `ArpRateFreeMode_Clamping` | FR-008, SC-006 | Combined offset clamped to [0.5, 50.0] Hz (US1 scenario 4) |
| `ArpRateTempoSync_PositiveOffset` | FR-008, FR-014, SC-006 | Step duration `0.125 / 1.5 = 83.3ms` at 120 BPM 1/16 (US1 scenario 5) |
| `ArpRateTempoSync_NegativeOffset` | FR-008, FR-014, SC-006 | Step duration `0.125 / 0.5 = 250ms` (US1 scenario 5) |
| `ArpGateLength_PositiveOffset` | FR-009, SC-006 | `effectiveGate = 50 + 100 * 1.0 = 150%` (US2 scenario 1) |
| `ArpGateLength_NegativeOffset` | FR-009, SC-006 | `effectiveGate = 80 - 100 * 1.0 = -20` clamped to 1% (US2 scenario 2) |
| `ArpOctaveRange_MaxExpansion` | FR-010, SC-006 | `1 + round(3 * 1.0) = 4` (US4 scenario 1) |
| `ArpOctaveRange_HalfAmount` | FR-010, SC-006 | `2 + round(3 * 0.5) = 4` clamped (US4 scenario 2) |
| `ArpOctaveRange_NegativeClamp` | FR-010, SC-006 | `3 + round(3 * -1.0) = 0` clamped to 1 (US4 scenario 3) |
| `ArpOctaveRange_ChangeDetection` | FR-010 | setOctaveRange only called when effective value changes |
| `ArpSwing_PositiveOffset` | FR-011, SC-006 | `effectiveSwing = 25 + 50 * 0.4 = 45%` where offset=0.4 = amount(0.5)*source(0.8) (US5 scenario 1) |
| `ArpSwing_ClampMax` | FR-011, SC-006 | `effectiveSwing = 60 + 50 * 1.0 = 110` clamped to 75% (US5 scenario 2) |
| `ArpSpice_BipolarPositive` | FR-012, SC-006 | `effectiveSpice = 0.2 + 0.5 = 0.7` (US3 scenario 1) |
| `ArpSpice_BipolarClampHigh` | FR-012, SC-006 | `effectiveSpice = 0.8 + 1.0 = 1.8` clamped to 1.0 (US3 scenario 2) |
| `ArpSpice_NegativeReduces` | FR-012, SC-006 | `effectiveSpice = 0.5 - 0.3 = 0.2` (negative offset reduces spice) |
| `ArpSpice_ZeroBase_ZeroMod` | FR-012, SC-006 | `effectiveSpice = 0.0 + 0.0 = 0.0` (US3 scenario 3) |
| `NoModRouting_IdenticalToPase9` | FR-017, SC-005, SC-008 | With no arp mod routings, behavior is bit-identical |
| `AllFiveDestinations_Simultaneous` | FR-013, SC-001 | All 5 destinations modulated concurrently |
| `ArpDisabled_SkipModReads` | FR-015 | Mod offsets not read when arp disabled (optimization) |
| `StaticAssert_Compilation` | FR-020, SC-010 | Build succeeds (static_asserts compile) |
| `ExistingDestinations_Unchanged` | FR-018, SC-008 | Existing 10 destinations still work after adding 5 |

**Testing strategy**: Since testing modulation requires the full processor pipeline (mod engine runs inside engine_.processBlock()), tests will:
1. Configure mod routing params via the parameter pipeline (source=LFO1, dest=ArpRate index, amount=1.0)
2. Set LFO1 to a fixed output via rate=0 and phase trick (or use Macro source for deterministic control)
3. Process blocks to let the mod engine produce offsets
4. Verify via observable behavior (step timing differences, or by direct inspection if accessible)

**Preferred approach**: Use Macro source (indices 4-7 in kGlobalSourceNames) for deterministic testing because Macro output = macro value directly (no LFO oscillation). Set Macro 1 to 1.0, route to Arp Rate with amount = 1.0, and the offset will be exactly 1.0.

### Step 5: Implement Mod Offset Application in Processor (FR-007 through FR-015)

**File**: `plugins/ruinae/src/processor/processor.cpp`
**Location**: Within `applyParamsToEngine()`, in the arpeggiator section (lines 1209-1373)

The modification replaces the raw parameter reads with modulated values. The new code will be inserted BEFORE the existing arp setter calls (line 1246).

**Pseudocode for the modification:**

```cpp
// --- Arpeggiator (FR-009) ---
// ... (existing mode/octaveMode/noteValue change-detection code stays the same)

// --- Arp Modulation Integration (078-modulation-integration) ---
// Read mod offsets from previous block's modulation engine output (FR-007).
// Applied BEFORE calling arpCore_ setters per FR-013.
// NOTE: verify the exact namespace of RuinaeModDest in ruinae_engine.h before
// writing the using-declaration; it is a plugin-layer enum, not in Krate::DSP.
{
    const float rateOffset = engine_.getGlobalModOffset(RuinaeModDest::ArpRate);
    const float gateOffset = engine_.getGlobalModOffset(RuinaeModDest::ArpGateLength);
    const float octaveOffset = engine_.getGlobalModOffset(RuinaeModDest::ArpOctaveRange);
    const float swingOffset = engine_.getGlobalModOffset(RuinaeModDest::ArpSwing);
    const float spiceOffset = engine_.getGlobalModOffset(RuinaeModDest::ArpSpice);

    // FR-008: Rate modulation -- multiplicative scaling +/-50%.
    // For tempo-sync mode: compute the modulated step duration, convert to Hz,
    // and temporarily override tempoSync=false so ArpeggiatorCore uses the
    // calculated free rate for this block. ArpeggiatorCore has no public API to
    // override step duration while keeping tempoSync=true, so the cleanest
    // correct approach is the free-rate override. See research.md R2 for
    // rationale and rejected alternatives.
    if (arpParams_.tempoSync.load(std::memory_order_relaxed)) {
        // Tempo-sync mode: scale step duration (FR-014)
        const auto noteValue = arpParams_.noteValue.load(std::memory_order_relaxed);
        auto mapping = getNoteValueFromDropdown(noteValue);
        float baseDurationSec = Krate::DSP::noteToDelayMs(
            mapping.note, mapping.modifier, tempoBPM_) / 1000.0f;
        float modDuration = baseDurationSec / (1.0f + 0.5f * rateOffset);
        if (rateOffset != 0.0f) {
            float effectiveHz = (modDuration > 0.0f) ? (1.0f / modDuration) : 50.0f;
            effectiveHz = std::clamp(effectiveHz, 0.5f, 50.0f);
            arpCore_.setTempoSync(false);
            arpCore_.setFreeRate(effectiveHz);
        } else {
            arpCore_.setTempoSync(true);
            // Normal tempo-sync path (setNoteValue already handled above)
            arpCore_.setFreeRate(arpParams_.freeRate.load(std::memory_order_relaxed));
        }
    } else {
        // Free rate mode: multiply base rate by (1 + 0.5 * offset)
        const float baseRate = arpParams_.freeRate.load(std::memory_order_relaxed);
        const float effectiveRate = std::clamp(
            baseRate * (1.0f + 0.5f * rateOffset),
            0.5f, 50.0f);
        arpCore_.setFreeRate(effectiveRate);
    }

    // FR-009: Gate length modulation -- additive +/-100%
    const float baseGate = arpParams_.gateLength.load(std::memory_order_relaxed);
    const float effectiveGate = std::clamp(
        baseGate + 100.0f * gateOffset,
        1.0f, 200.0f);
    arpCore_.setGateLength(effectiveGate);

    // FR-010: Octave range modulation -- additive, rounded to int, +/-3
    const int baseOctave = arpParams_.octaveRange.load(std::memory_order_relaxed);
    const int effectiveOctave = std::clamp(
        baseOctave + static_cast<int>(std::round(3.0f * octaveOffset)),
        1, 4);
    if (effectiveOctave != prevArpOctaveRange_) {
        arpCore_.setOctaveRange(effectiveOctave);
        prevArpOctaveRange_ = effectiveOctave;
    }

    // FR-011: Swing modulation -- additive +/-50%
    const float baseSwing = arpParams_.swing.load(std::memory_order_relaxed);
    const float effectiveSwing = std::clamp(
        baseSwing + 50.0f * swingOffset,
        0.0f, 75.0f);
    arpCore_.setSwing(effectiveSwing);

    // FR-012: Spice modulation -- bipolar additive in [0,1]
    const float baseSpice = arpParams_.spice.load(std::memory_order_relaxed);
    const float effectiveSpice = std::clamp(
        baseSpice + spiceOffset,
        0.0f, 1.0f);
    arpCore_.setSpice(effectiveSpice);
}
```

**IMPORTANT**: This code REPLACES the existing raw calls at lines 1246-1249 and 1357. The existing lines:
```cpp
arpCore_.setFreeRate(arpParams_.freeRate.load(...));    // line 1246
arpCore_.setGateLength(arpParams_.gateLength.load(...)); // line 1247
arpCore_.setSwing(arpParams_.swing.load(...));           // line 1249
arpCore_.setSpice(arpParams_.spice.load(...));           // line 1357
```
...are replaced by the modulated versions above. The `setOctaveRange` call at line 1224-1227 is also replaced (with the effective octave including the change-detection).

The `arpCore_.setTempoSync()` call at line 1237 needs to be moved inside the rate modulation block so it can be conditionally overridden when there's a non-zero rate offset in tempo-sync mode.

### Step 6: Handle FR-015 Optimization (Skip reads when arp disabled)

In `applyParamsToEngine()`, wrap the mod offset reads in a check:

```cpp
if (arpParams_.enabled.load(std::memory_order_relaxed)) {
    // Read and apply mod offsets (the block from Step 5)
} else {
    // Use raw param values (existing code, no mod offset reads)
    arpCore_.setFreeRate(arpParams_.freeRate.load(std::memory_order_relaxed));
    arpCore_.setGateLength(arpParams_.gateLength.load(std::memory_order_relaxed));
    // ... etc
}
```

Note: This optimization is optional per FR-015 ("MAY skip"). But implementing it is simple and consistent with the spec's note about not wasting CPU on unused destinations.

### Step 7: Build and Verify

1. Build: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests`
2. Verify zero compiler warnings from changed files
3. Run all tests: `build/windows-x64-release/bin/Release/ruinae_tests.exe`
4. Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`
5. Run clang-tidy: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja`

### Step 8: Update Architecture Documentation

**File**: `specs/_architecture_/` -- Update relevant section to document the arp modulation destination pattern.

---

## Requirement-to-Implementation Mapping

| Requirement | Implementation Location | Notes |
|-------------|------------------------|-------|
| FR-001 | `ruinae_engine.h` enum extension | Add ArpRate=74..ArpSpice=78 |
| FR-002 | No code needed | 74-78 < 128 (kMaxModDestinations) |
| FR-003 | `mod_matrix_types.h` constant | kNumGlobalDestinations = 15 |
| FR-004 | `mod_matrix_types.h` array | 5 new ModDestInfo entries at indices 10-14 |
| FR-005 | No code needed | kModDestCount already references kNumGlobalDestinations |
| FR-006 | `controller.cpp` array | 5 new ParamID entries |
| FR-007 | `processor.cpp` applyParamsToEngine() | engine_.getGlobalModOffset(RuinaeModDest::Arp*) calls |
| FR-008 | `processor.cpp` applyParamsToEngine() | Rate: baseRate * (1 + 0.5 * offset), clamped [0.5, 50] |
| FR-009 | `processor.cpp` applyParamsToEngine() | Gate: baseGate + 100 * offset, clamped [1, 200] |
| FR-010 | `processor.cpp` applyParamsToEngine() | Octave: base + round(3 * offset), clamped [1, 4] |
| FR-011 | `processor.cpp` applyParamsToEngine() | Swing: base + 50 * offset, clamped [0, 75] |
| FR-012 | `processor.cpp` applyParamsToEngine() | Spice: base + offset, clamped [0, 1] |
| FR-013 | `processor.cpp` applyParamsToEngine() | All 5 offsets read before setters called |
| FR-014 | `processor.cpp` applyParamsToEngine() | tempoSync branch: scale duration by 1/(1+0.5*offset) |
| FR-015 | `processor.cpp` applyParamsToEngine() | Skip mod reads when arp disabled |
| FR-016 | By construction | All operations are array lookups + arithmetic |
| FR-017 | By construction | Old presets have no arp routings; offset = 0 |
| FR-018 | By construction | Existing enum values unchanged; new values appended |
| FR-019 | Verified | No new entries in plugin_ids.h |
| FR-020 | `ruinae_engine.h` static_assert | GlobalFilterCutoff + 10 == ArpRate |

| Success Criterion | Verification Method |
|-------------------|---------------------|
| SC-001 | Manual: open plugin, check mod matrix dropdown shows 5 new destinations |
| SC-002 | Unit test: verify mod offset applied per block |
| SC-003 | Unit test: 10000+ blocks with varying offsets, check no NaN/Inf |
| SC-004 | Unit test: save/load cycle with arp mod routings |
| SC-005 | Unit test: compare output with/without mod code changes when offset=0 |
| SC-006 | Unit tests: all formula-based tests with known inputs and expected outputs |
| SC-007 | Code inspection: no new/delete/malloc/free in mod application path |
| SC-008 | Run existing mod matrix tests: all pass without modification |
| SC-009 | Unit test: load Phase 9 preset, verify arp behavior identical |
| SC-010 | Build succeeds (static_asserts compile) |
| SC-011 | pluginval --strictness-level 5 passes |
| SC-012 | Zero compiler warnings + zero clang-tidy findings from modified files |

---

## Risk Assessment

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Tempo-sync rate override approach may cause audible glitches | Medium | Low | Test with tempo-sync + modulation extensively; the approach of temporarily switching to free rate is clean because ArpeggiatorCore's setTempoSync/setFreeRate are called every block anyway |
| Change-detection for octaveRange may interact with modulation | Low | Low | The effective octave (including mod offset) is tracked in prevArpOctaveRange_, not the base value |
| Existing tests break due to kNumGlobalDestinations change | Medium | Very Low | The change only adds entries to arrays and increases a count; existing indices are unchanged |

## Complexity Tracking

No constitution violations to justify. All changes are minimal and comply with all principles.
