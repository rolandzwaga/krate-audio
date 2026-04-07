# Implementation Plan: Innexus Milestone 5 -- Harmonic Memory (Snapshot Capture & Recall)

**Branch**: `119-harmonic-memory` | **Date**: 2026-03-05 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/119-harmonic-memory/spec.md`

## Summary

Add a Harmonic Memory system to the Innexus plugin: a `HarmonicSnapshot` data structure for normalized timbral storage, 8 pre-allocated memory slots with capture/recall triggers, integration with existing manual freeze infrastructure, state persistence v5 (extending v4 with memory slot data), and JSON export/import via `IMessage`. Three new VST3 parameters (Memory Slot, Memory Capture, Memory Recall) are registered. Capture extracts L2-normalized amplitudes, relative frequencies, inharmonic deviations, phases, and residual envelope from the current HarmonicFrame/ResidualFrame. Recall loads a stored snapshot into `manualFrozenFrame_` and engages manual freeze, reusing the existing morph, harmonic filter, and crossfade infrastructure unchanged.

## Technical Context

**Language/Version**: C++20, MSVC 2022 / Clang / GCC
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (shared library)
**Storage**: IBStream binary blob (VST3 state persistence, version 5)
**Testing**: Catch2 (dsp_tests, innexus_tests targets) *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows 10/11 (MSVC), macOS 11+ (Clang), Linux (GCC) -- cross-platform VST3 plugin
**Project Type**: Monorepo with shared DSP library + plugin
**Performance Goals**: Capture and recall complete in < 50 microseconds each (SC-006); < 0.05% single-core CPU combined
**Constraints**: Zero allocations on audio thread; all 8 memory slots pre-allocated as member variables (~6.8 KB total)
**Scale/Scope**: 3 new parameters, 1 new data structure (HarmonicSnapshot), snapshot<->frame conversion utilities, state v5 serialization, JSON serialization (P3), `notify()` handler; ~3 new test files, ~60 tasks across 6 phases

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller remain separate; no cross-includes
- [x] New parameters (kMemorySlotId, kMemoryCaptureId, kMemoryRecallId) registered in Controller, handled via atomics in Processor
- [x] State version bumped from 4 to 5; backward compatible with v4
- [x] JSON import uses `IMessage` from Controller to Processor (FR-029); no direct data sharing

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] All 8 memory slots are pre-allocated `HarmonicSnapshot` member variables (~856 bytes each, ~6.8 KB total)
- [x] Capture is a fixed-size struct copy (no allocation)
- [x] Recall is a fixed-size struct copy into `manualFrozenFrame_` (no allocation)
- [x] `notify()` handler for JSON import receives binary payload and performs fixed-size copy (no allocation)
- [x] No memory allocation, locks, exceptions, or I/O on audio thread

**Required Check - Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability analysis completed (see section below)
- [x] Scalar-first workflow planned (no SIMD needed)

**Required Check - Principle IX (Layered Architecture):**
- [x] `HarmonicSnapshot` placed at Layer 2 (processors) alongside `harmonic_types.h` -- future consumers (Evolution Engine M6, Multi-Source Blending) are DSP-level
- [x] Conversion utilities placed in same file at Layer 2
- [x] JSON serialization is a separate header (plugin-local) with no runtime dependency on JSON library in audio path
- [x] No circular dependencies introduced

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] Compliance table will be filled with actual file paths, line numbers, test names, measured values
- [x] No thresholds will be relaxed from spec

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `HarmonicSnapshot`, `MemorySlot`

| Planned Type | Search Pattern | Existing? | Action |
|--------------|----------------|-----------|--------|
| `HarmonicSnapshot` | `HarmonicSnapshot\|harmonicSnapshot\|harmonic_snapshot` | No | Create New in `dsp/include/krate/dsp/processors/harmonic_snapshot.h` |
| `MemorySlot` | `MemorySlot\|memorySlot\|memory_slot` | No | Inline struct inside `harmonic_snapshot.h` |

**Utility Functions to be created**:

| Planned Function | Search Pattern | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `captureSnapshot` | `captureSnapshot` | No | `harmonic_snapshot.h` | Create New |
| `recallSnapshotToFrame` | `recallSnapshot` | No | `harmonic_snapshot.h` | Create New |
| `snapshotToJson` | `snapshotToJson` | No | `plugins/innexus/src/dsp/harmonic_snapshot_json.h` | Create New (P3) |
| `jsonToSnapshot` | `jsonToSnapshot` | No | `plugins/innexus/src/dsp/harmonic_snapshot_json.h` | Create New (P3) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| HarmonicFrame | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Source data for capture; target for recall reconstruction |
| Partial | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Per-partial field extraction during capture and reconstruction during recall |
| kMaxPartials (=48) | `dsp/include/krate/dsp/processors/harmonic_types.h` | 2 | Fixed-size array sizing in HarmonicSnapshot |
| ResidualFrame | `dsp/include/krate/dsp/processors/residual_types.h` | 2 | Source for residual capture; target for recall |
| kResidualBands (=16) | `dsp/include/krate/dsp/processors/residual_types.h` | 2 | Residual band array sizing |
| lerpHarmonicFrame | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | 2 | Morph works unchanged with recalled snapshots (recalled = State A) |
| lerpResidualFrame | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | 2 | Same: morph residual works unchanged |
| computeHarmonicMask | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | 2 | Harmonic filter works unchanged on recalled data |
| applyHarmonicMask | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | 2 | Applied after morph, before oscillator bank |
| Manual Freeze Infrastructure | `plugins/innexus/src/processor/processor.h` (lines 396-410) | plugin | `manualFrozenFrame_`, `manualFrozenResidualFrame_`, `manualFreezeActive_`, crossfade members -- recall loads into these |
| Freeze engagement detection | `plugins/innexus/src/processor/processor.cpp` (lines 406-452) | plugin | Recall engages freeze by setting `manualFreezeActive_ = true` and loading frames |
| Morph interpolation | `plugins/innexus/src/processor/processor.cpp` (lines 487-554) | plugin | Works unchanged: morph 0.0 = recalled (State A), 1.0 = live (State B) |
| Harmonic filter | `plugins/innexus/src/processor/processor.cpp` (lines 456-479) | plugin | Works unchanged on recalled data |
| State Serialization v4 | `plugins/innexus/src/processor/processor.cpp` (lines 1115-1432) | plugin | Extended to v5 by appending memory slot data after v4 payload |
| Controller parameter registration | `plugins/innexus/src/controller/controller.cpp` | plugin | Add 3 new parameters (Slot, Capture, Recall) |
| IBStreamer | VST3 SDK | SDK | Binary serialization for state persistence |
| IMessage/notify() pattern | `plugins/ruinae/src/processor/processor.cpp` (lines 1877-1965) | reference | Pattern for `notify()` handler; Innexus currently has no notify() override |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no existing HarmonicSnapshot, no conflicts)
- [x] `plugins/innexus/src/` - Plugin source (no existing memory/snapshot code)
- [x] `plugins/innexus/src/plugin_ids.h` - Parameter IDs (304+ available for memory parameters)
- [x] `specs/_architecture_/` - Architecture docs (no snapshot-related components)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned new types are unique and not found in the codebase. `HarmonicSnapshot` is a new struct in the `Krate::DSP` namespace at Layer 2. Conversion functions are `inline` in header-only files. JSON utilities are plugin-local. No naming conflicts detected.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| HarmonicFrame | partials | `std::array<Partial, kMaxPartials> partials{}` | Yes |
| HarmonicFrame | numPartials | `int numPartials = 0` | Yes |
| HarmonicFrame | f0 | `float f0 = 0.0f` | Yes |
| HarmonicFrame | f0Confidence | `float f0Confidence = 0.0f` | Yes |
| HarmonicFrame | spectralCentroid | `float spectralCentroid = 0.0f` | Yes |
| HarmonicFrame | brightness | `float brightness = 0.0f` | Yes |
| HarmonicFrame | noisiness | `float noisiness = 0.0f` | Yes |
| HarmonicFrame | globalAmplitude | `float globalAmplitude = 0.0f` | Yes |
| Partial | amplitude | `float amplitude = 0.0f` | Yes |
| Partial | relativeFrequency | `float relativeFrequency = 0.0f` | Yes |
| Partial | harmonicIndex | `int harmonicIndex = 0` | Yes |
| Partial | phase | `float phase = 0.0f` | Yes |
| Partial | inharmonicDeviation | `float inharmonicDeviation = 0.0f` | Yes |
| Partial | frequency | `float frequency = 0.0f` | Yes |
| Partial | stability | `float stability = 0.0f` | Yes |
| Partial | age | `int age = 0` | Yes |
| ResidualFrame | bandEnergies | `std::array<float, kResidualBands> bandEnergies{}` | Yes |
| ResidualFrame | totalEnergy | `float totalEnergy = 0.0f` | Yes |
| ResidualFrame | transientFlag | `bool transientFlag = false` | Yes |
| HarmonicOscillatorBank | loadFrame | `void loadFrame(const HarmonicFrame& frame, float targetPitch) noexcept` | Yes |
| ResidualSynthesizer | loadFrame | `void loadFrame(const ResidualFrame& frame, float brightness, float transientEmphasis)` | Yes |
| IBStreamer | writeFloat | `bool writeFloat(float)` | Yes |
| IBStreamer | readFloat | `bool readFloat(float&)` | Yes |
| IBStreamer | writeInt32 | `bool writeInt32(Steinberg::int32)` | Yes |
| IBStreamer | readInt32 | `bool readInt32(Steinberg::int32&)` | Yes |
| IBStreamer | writeInt8 | `bool writeInt8(Steinberg::int8)` | Yes |
| IBStreamer | readInt8 | `bool readInt8(Steinberg::int8&)` | Yes |
| AudioEffect | allocateMessage | `IMessage* allocateMessage()` | Yes (from Ruinae pattern) |
| AudioEffect | sendMessage | `tresult sendMessage(IMessage* message)` | Yes (from Ruinae pattern) |
| IMessage | setMessageID | `void setMessageID(const char* id)` | Yes |
| IMessage | getAttributes | `IAttributeList* getAttributes()` | Yes |
| IAttributeList | setBinary | `tresult setBinary(const char* id, const void* data, uint32 size)` | Yes |
| IAttributeList | getBinary | `tresult getBinary(const char* id, const void*& data, uint32& size)` | Yes |
| IAttributeList | setInt | `tresult setInt(const char* id, int64 value)` | Yes |
| IAttributeList | getInt | `tresult getInt(const char* id, int64& value)` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/harmonic_types.h` - HarmonicFrame, Partial, kMaxPartials
- [x] `dsp/include/krate/dsp/processors/residual_types.h` - ResidualFrame, kResidualBands
- [x] `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` - lerpHarmonicFrame, lerpResidualFrame, computeHarmonicMask, applyHarmonicMask
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother (not needed for this feature)
- [x] `plugins/innexus/src/processor/processor.h` - Processor class members, manual freeze state
- [x] `plugins/innexus/src/processor/processor.cpp` - process() flow, state v4, freeze/morph/filter logic, parameter handling
- [x] `plugins/innexus/src/controller/controller.h` - Controller class (no notify() override needed in controller)
- [x] `plugins/innexus/src/controller/controller.cpp` - Parameter registration, setComponentState v4
- [x] `plugins/innexus/src/plugin_ids.h` - ParameterIds enum, 300-303 used by M4
- [x] `plugins/ruinae/src/processor/processor.cpp` - IMessage/notify() pattern reference (lines 1877-1965)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Processor state | Current version is 4 (M4); must write 5 for M5 | `streamer.writeInt32(5)` |
| Parameter atomics | Capture/Recall are momentary triggers -- must detect 0->1 transition | Track previous value; fire on `current > 0.5 && previous <= 0.5`. After firing, reset the atomic to 0.0f and call `setParamNormalized()` via IConnectionPoint so the host UI reflects the reset (FR-006, FR-011). |
| Memory slots | Pre-allocated as member array, NOT std::vector | `std::array<Krate::DSP::MemorySlot, 8>` with `bool occupied` flag |
| `manualFrozenFrame_` | Recall copies into this; subsequent capture to same slot does NOT update this | Recall is a copy, not a reference. Spec clarification confirms this. |
| `manualFreezeActive_` | Recall sets this to true; existing freeze toggle can disengage it | Recall integrates with existing freeze infrastructure seamlessly |
| `notify()` | Innexus has NO existing `notify()` override | Must add `notify()` declaration to processor.h and implementation to processor.cpp |
| Capture during morph | FR-008: capture post-morph blended state | Read from `morphedFrame_` / `morphedResidualFrame_` when morph is active |
| Capture with filter | FR-009: capture pre-filter data | Read from the frame BEFORE `applyHarmonicMask()`, not after |
| StringListParameter | 8 entries -> stepCount inferred, values 0.0, 1/7, 2/7, ..., 1.0 | Denormalize: `int slot = std::clamp(int(std::round(norm * 7.0f)), 0, 7)` |
| L2 normalization | Capture must L2-normalize amplitudes | `sum = sqrt(sum_of_squares); amp[i] /= sum` (avoid div by zero) |
| Capture source selection | Complex: depends on freeze state, morph state, source mode | See detailed capture source logic in design section |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep at Layer 2

| Function | Why Keep? |
|----------|-----------|
| `captureSnapshot()` | Operates on Layer 2 types (HarmonicFrame, ResidualFrame), belongs alongside them |
| `recallSnapshotToFrame()` | Same -- Layer 2 type conversion |

**Decision**: No new Layer 0 extractions. All new utilities operate on Layer 2 data types and belong at Layer 2.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Capture and recall are one-shot data copies |
| **Data parallelism width** | 48 partials + 16 bands | But operations are per-event, not per-sample |
| **Branch density in inner loop** | LOW | Simple array iteration with copy/normalize |
| **Dominant operations** | Array copy + L2 normalization | Trivially cheap arithmetic |
| **Current CPU budget vs expected usage** | 0.05% budget vs ~0.001% expected | Per-event operations (not per-sample); each is ~200 FLOPs |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: Capture and recall are per-event operations triggered by user action (button press), not per-sample or per-frame. Each operation involves copying 48 floats x 4 arrays + 16 floats + metadata -- approximately 200 FLOPs total. This happens at human interaction rate (maybe once per second at most). SIMD optimization would add code complexity with zero measurable benefit. The hot path (oscillator bank, morph, filter) is unchanged by this feature.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip capture when no analysis active (all-zero frame) | Avoids unnecessary normalization | LOW | YES |
| Skip recall when slot is empty | Early-out, no frame copy | LOW | YES (already in spec: FR-013) |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 (HarmonicSnapshot struct + conversion utilities) + Plugin-local (memory slots, serialization, JSON)

**Related features at same layer** (from INNEXUS-ROADMAP.md):
- **Priority 2: Harmonic Cross-Synthesis (Phase 17)** -- source switching uses memory slots as stored timbres
- **Priority 4: Evolution Engine (Phase 19)** -- slow drift between multiple stored snapshots; needs `HarmonicSnapshot` and `lerpHarmonicFrame` as core primitives
- **Priority 6: Multi-Source Blending (Phase 21)** -- blend multiple analysis streams and stored snapshots

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `HarmonicSnapshot` struct | HIGH | Evolution Engine, Multi-Source Blending, Cross-Synthesis | Place in shared DSP library (Layer 2) now |
| `captureSnapshot()` | HIGH | Any feature that stores harmonic state | Place in shared DSP library (Layer 2) now |
| `recallSnapshotToFrame()` | HIGH | Evolution Engine, Cross-Synthesis | Place in shared DSP library (Layer 2) now |
| Memory slot management | MEDIUM | Evolution Engine (needs slot access), Multi-Source (N slots) | Keep plugin-local for now; extract if M6 needs it |
| JSON serialization | LOW | Debugging tool, not performance-critical | Keep plugin-local |
| State v5 format | LOW | Only Innexus | Keep plugin-local |

### Detailed Analysis (for HIGH potential items)

**`HarmonicSnapshot` struct** provides:
- Self-contained normalized timbral representation
- Fixed-size, allocation-free storage
- Sample-rate-independent data format
- Forward-compatible phase storage

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Evolution Engine (P4) | YES | Core data type for stored spectra to drift between |
| Multi-Source Blending (P6) | YES | Stored snapshot side of blending |
| Cross-Synthesis (P2) | YES | Carrier/modulator snapshot storage |

**Recommendation**: Place `HarmonicSnapshot` in `dsp/include/krate/dsp/processors/harmonic_snapshot.h` as a shared DSP type. All three future consumers are KrateDSP-level features.

**`captureSnapshot()` + `recallSnapshotToFrame()`** provide:
- Bidirectional conversion between `HarmonicFrame`/`ResidualFrame` and `HarmonicSnapshot`
- L2 normalization during capture
- Denormalization during recall (amplitude scaling, frequency reconstruction)

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Evolution Engine (P4) | YES | Needs to read snapshots into frames for interpolation |
| Multi-Source Blending (P6) | YES | Same: snapshot -> frame for N-way blend |

**Recommendation**: Place in `harmonic_snapshot.h` alongside the struct definition.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| `HarmonicSnapshot` in shared DSP Layer 2 | 3+ future consumers identified; placing now avoids refactoring |
| Conversion utilities in same header | Tightly coupled to the struct; same consumers |
| Memory slot management plugin-local | Only Innexus uses slots currently; extract if M6 needs it |
| JSON serialization plugin-local | Debugging/sharing feature, not DSP-critical; no cross-plugin need |
| `notify()` handler for JSON import | Follows Ruinae pattern; keeps file I/O off audio thread |

### Review Trigger

After implementing **M6 (Creative Extensions, Phases 17-21)**, review this section:
- [ ] Does the Evolution Engine need direct access to memory slots? -> Consider shared slot manager
- [ ] Does Cross-Synthesis reuse `captureSnapshot()`? -> Confirm API sufficiency
- [ ] Does Multi-Source Blending need N-snapshot interpolation? -> Verify `recallSnapshotToFrame()` composability with `lerpHarmonicFrame()`

---

## Architecture Overview

### Data Flow: Capture

```
Current State (one of):
  - Live analysis frame (sidechain mode)
  - Sample playback frame (sample mode)
  - Manual frozen frame (freeze engaged)
  - Post-morph blended frame (morph active + freeze)
    |
    v
captureSnapshot(harmonicFrame, residualFrame)
    |
    v
HarmonicSnapshot (L2-normalized, relative frequencies)
    |
    v
memorySlots_[selectedSlot].snapshot = result
memorySlots_[selectedSlot].occupied = true
```

### Data Flow: Recall

```
memorySlots_[selectedSlot]
    |  (if occupied)
    v
recallSnapshotToFrame(snapshot)
    |
    v
{HarmonicFrame, ResidualFrame}
    |
    v
manualFrozenFrame_ = harmonicFrame
manualFrozenResidualFrame_ = residualFrame
manualFreezeActive_ = true
    |
    v
Existing M4 signal chain:
  [Morph] -> [Harmonic Filter] -> [Oscillator Bank]
```

### Signal Chain (Extended from M4)

```
Analysis Source (Sample/Sidechain)
    |
    v
Analysis Pipeline (existing)
    |
    v
HarmonicFrame + ResidualFrame (live, State B)
    |
    v
[Manual Freeze Gate]  -----> if freeze ON: capture State A (once)
    |                         if RECALL: load snapshot into State A
    |                         if freeze OFF + was frozen: 10ms crossfade
    v
[Confidence-Gated Auto-Freeze] (existing, lower priority than manual)
    |
    v
[Morph Interpolation]  <---- morphPosition (0=State A/recalled, 1=State B/live)
    |                         lerp amplitudes, relativeFreqs, residual bands
    v
[Harmonic Filter]       <---- filterType (All-Pass/Odd/Even/Low/High)
    |                         effectiveAmp_n = amp_n * mask(n)
    v
Oscillator Bank + Residual Synthesizer (existing)
    |
    v
Output
```

### Key Architecture Decisions

1. **Recall integrates with existing freeze infrastructure.** `manualFrozenFrame_` and `manualFrozenResidualFrame_` are the integration points. Recall loads snapshot data into these members and sets `manualFreezeActive_ = true`. This means morph, harmonic filter, freeze toggle, and crossfade all work automatically without any changes.

2. **Capture reads pre-filter data.** When a harmonic filter is active, capture reads from the morphed frame BEFORE `applyHarmonicMask()`. The filter is non-destructive and applied at read time.

3. **Capture source depends on current state:**
   - If manual freeze is active AND morph position != 0.0: capture from `morphedFrame_` / `morphedResidualFrame_` (post-morph blended state, FR-008)
   - If manual freeze is active AND morph position == 0.0: capture from `manualFrozenFrame_` (pure frozen state)
   - If no freeze, sidechain mode: capture from `currentLiveFrame_` / `currentLiveResidualFrame_`
   - If no freeze, sample mode: capture from `analysis->getFrame(currentFrameIndex_)` / `analysis->getResidualFrame(currentFrameIndex_)`
   - If no analysis active: capture empty/silent frame (valid but musically silent, per spec edge case)

4. **Capture and recall are triggered by parameter transitions.** The `kMemoryCaptureId` and `kMemoryRecallId` parameters are momentary triggers: the system detects 0->1 transitions in `process()` by comparing current value against a previous-value tracker. The parameter is a step-1 toggle (0 or 1).

5. **Slot-to-slot recall uses existing crossfade.** When recalling a different slot while freeze is already engaged, the freeze->freeze transition triggers the existing `manualFreezeRecoverySamplesRemaining_` crossfade mechanism. The current oscillator output is captured as the old level, and the new snapshot is loaded into the freeze frame. The ~10ms crossfade blends from old to new.

6. **JSON import via `IMessage`.** The controller handles file I/O (reading JSON, parsing), constructs a `HarmonicSnapshot` binary, packages it into an `IMessage` with `setBinary()`, and sends it to the processor. The processor's `notify()` handler receives it and performs a fixed-size copy into the target slot. No allocation on any thread except the controller's initial JSON parsing.

---

## Component-by-Component Design

### 1. New Shared DSP Type: `harmonic_snapshot.h`

**Location**: `dsp/include/krate/dsp/processors/harmonic_snapshot.h`
**Layer**: 2 (processors)
**Dependencies**: `harmonic_types.h`, `residual_types.h` (both Layer 2)

```cpp
namespace Krate::DSP {

/// A normalized, self-contained timbral snapshot for harmonic memory.
/// All frequencies relative to F0, amplitudes L2-normalized.
/// Fixed-size, allocation-free, suitable for real-time copy.
struct HarmonicSnapshot {
    float f0Reference = 0.0f;                              ///< Source F0 at capture (informational)
    int numPartials = 0;                                    ///< Active count [0, 48]
    std::array<float, kMaxPartials> relativeFreqs{};        ///< freq_n / F0
    std::array<float, kMaxPartials> normalizedAmps{};       ///< L2-normalized amplitudes
    std::array<float, kMaxPartials> phases{};               ///< Phase at capture (radians)
    std::array<float, kMaxPartials> inharmonicDeviation{};  ///< relativeFreq_n - harmonicIndex
    std::array<float, kResidualBands> residualBands{};      ///< Spectral envelope of residual
    float residualEnergy = 0.0f;                            ///< Overall residual level
    float globalAmplitude = 0.0f;                           ///< Source loudness (informational)
    float spectralCentroid = 0.0f;                          ///< Metadata for UI/sorting
    float brightness = 0.0f;                                ///< Metadata
};

/// A single memory slot: snapshot + occupancy flag.
struct MemorySlot {
    HarmonicSnapshot snapshot{};
    bool occupied = false;
};

/// Capture a HarmonicSnapshot from current analysis state.
/// L2-normalizes amplitudes. Extracts relative frequencies, phases,
/// inharmonic deviations, and metadata from the HarmonicFrame.
/// @param frame Source harmonic frame
/// @param residual Source residual frame
/// @return Captured snapshot in normalized harmonic domain
inline HarmonicSnapshot captureSnapshot(
    const HarmonicFrame& frame, const ResidualFrame& residual) noexcept
{
    HarmonicSnapshot snap{};
    snap.f0Reference = frame.f0;
    snap.numPartials = frame.numPartials;
    snap.globalAmplitude = frame.globalAmplitude;
    snap.spectralCentroid = frame.spectralCentroid;
    snap.brightness = frame.brightness;

    // Copy residual data
    snap.residualBands = residual.bandEnergies;
    snap.residualEnergy = residual.totalEnergy;

    // Extract per-partial data and accumulate for L2 normalization
    float sumSquares = 0.0f;
    for (int i = 0; i < frame.numPartials; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        const auto& p = frame.partials[idx];
        snap.relativeFreqs[idx] = p.relativeFrequency;
        snap.normalizedAmps[idx] = p.amplitude;
        snap.phases[idx] = p.phase;
        snap.inharmonicDeviation[idx] = p.inharmonicDeviation;
        sumSquares += p.amplitude * p.amplitude;
    }

    // L2-normalize amplitudes (FR-002)
    if (sumSquares > 0.0f)
    {
        const float norm = 1.0f / std::sqrt(sumSquares);
        for (int i = 0; i < frame.numPartials; ++i)
            snap.normalizedAmps[static_cast<size_t>(i)] *= norm;
    }

    return snap;
}

/// Reconstruct a HarmonicFrame and ResidualFrame from a stored snapshot.
/// @param snap Source snapshot
/// @param[out] frame Reconstructed harmonic frame
/// @param[out] residual Reconstructed residual frame
inline void recallSnapshotToFrame(
    const HarmonicSnapshot& snap,
    HarmonicFrame& frame,
    ResidualFrame& residual) noexcept
{
    frame = {}; // Clear all fields
    frame.f0 = snap.f0Reference;
    frame.numPartials = snap.numPartials;
    frame.globalAmplitude = snap.globalAmplitude;
    frame.spectralCentroid = snap.spectralCentroid;
    frame.brightness = snap.brightness;
    frame.f0Confidence = 1.0f; // Recalled data is always "confident"

    for (int i = 0; i < snap.numPartials; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        auto& p = frame.partials[idx];
        p.relativeFrequency = snap.relativeFreqs[idx];
        p.amplitude = snap.normalizedAmps[idx];
        p.phase = snap.phases[idx];
        p.inharmonicDeviation = snap.inharmonicDeviation[idx];
        // Derive harmonicIndex from relativeFrequency - inharmonicDeviation
        p.harmonicIndex = static_cast<int>(std::round(
            snap.relativeFreqs[idx] - snap.inharmonicDeviation[idx]));
        if (p.harmonicIndex <= 0) p.harmonicIndex = i + 1; // Fallback
        // frequency left at 0.0 -- oscillator bank uses relativeFrequency * targetPitch
        p.stability = 1.0f; // Recalled data is stable
        p.age = 1; // Non-zero age for validity
    }

    residual = {};
    residual.bandEnergies = snap.residualBands;
    residual.totalEnergy = snap.residualEnergy;
    residual.transientFlag = false; // No transient on recall
}

} // namespace Krate::DSP
```

### 2. Plugin ID Extensions: `plugin_ids.h`

Add to the existing `ParameterIds` enum in the 300-399 range:

```cpp
// Harmonic Memory (300-399) -- M5
kMemorySlotId = 304,           // StringListParameter: 8 entries (Slot 1-8)
kMemoryCaptureId = 305,        // Momentary trigger (step-1 toggle)
kMemoryRecallId = 306,         // Momentary trigger (step-1 toggle)
```

### 3. Processor Extensions: `processor.h` / `processor.cpp`

**New member variables** (pre-allocated, real-time safe):

```cpp
// =========================================================================
// Harmonic Memory (M5: FR-005, FR-006, FR-010, FR-011)
// =========================================================================
std::atomic<float> memorySlot_{0.0f};            // normalized (0-7 mapped)
std::atomic<float> memoryCapture_{0.0f};          // momentary trigger
std::atomic<float> memoryRecall_{0.0f};           // momentary trigger

/// 8 pre-allocated memory slots (FR-010, FR-028)
std::array<Krate::DSP::MemorySlot, 8> memorySlots_{};

/// Tracks previous trigger values to detect 0->1 transitions
float previousCaptureTrigger_ = 0.0f;
float previousRecallTrigger_ = 0.0f;
```

**New public accessors** (TEST ONLY):

```cpp
/// @brief Get memory slot (TEST ONLY).
const Krate::DSP::MemorySlot& getMemorySlot(int index) const {
    return memorySlots_[static_cast<size_t>(std::clamp(index, 0, 7))];
}

/// @brief Get selected memory slot index (TEST ONLY).
int getSelectedSlotIndex() const {
    const float norm = memorySlot_.load(std::memory_order_relaxed);
    return std::clamp(static_cast<int>(std::round(norm * 7.0f)), 0, 7);
}
```

**New public method**:

```cpp
/// @brief Handle IMessage from controller (JSON import, etc.)
Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;
```

**Parameter handling** (in `processParameterChanges()`):

```cpp
case kMemorySlotId:
    memorySlot_.store(static_cast<float>(value));
    break;
case kMemoryCaptureId:
    memoryCapture_.store(static_cast<float>(value));
    break;
case kMemoryRecallId:
    memoryRecall_.store(static_cast<float>(value));
    break;
```

**Capture logic** (in `process()`, after morph interpolation but before filter application):

The capture trigger detection runs at block rate. When `memoryCapture_` transitions from <= 0.5 to > 0.5:

1. Read the selected slot index: `int slot = std::clamp(int(std::round(memorySlot_.load() * 7.0f)), 0, 7)`
2. Determine capture source (5 cases, see Architecture Decisions above)
3. Call `Krate::DSP::captureSnapshot(harmonicFrame, residualFrame)`
4. Store: `memorySlots_[slot].snapshot = result; memorySlots_[slot].occupied = true;`
5. Update trigger tracker: `previousCaptureTrigger_ = currentCaptureTrigger;`

**Recall logic** (in `process()`, at block start alongside freeze detection):

When `memoryRecall_` transitions from <= 0.5 to > 0.5:

1. Read the selected slot index
2. If `!memorySlots_[slot].occupied`: silently ignore (FR-013)
3. Reconstruct frame: `Krate::DSP::recallSnapshotToFrame(snap, tempFrame, tempResidual)`
4. If `manualFreezeActive_` already (slot-to-slot recall, FR-015):
   - `manualFreezeRecoveryOldLevel_ = noteActive_ ? oscillatorBank_.process() : 0.0f;`
   - `manualFreezeRecoverySamplesRemaining_ = manualFreezeRecoveryLengthSamples_;`
5. Load into freeze infrastructure: `manualFrozenFrame_ = tempFrame; manualFrozenResidualFrame_ = tempResidual;`
6. Engage freeze: `manualFreezeActive_ = true;`
7. Update trigger tracker: `previousRecallTrigger_ = currentRecallTrigger;`

**notify() handler** (for JSON import, FR-029):

```cpp
Steinberg::tresult PLUGIN_API Processor::notify(Steinberg::Vst::IMessage* message)
{
    if (!message)
        return Steinberg::kInvalidArgument;

    if (strcmp(message->getMessageID(), "HarmonicSnapshotImport") == 0)
    {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        Steinberg::int64 slotIndex = 0;
        if (attrs->getInt("slotIndex", slotIndex) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        if (slotIndex < 0 || slotIndex >= 8)
            return Steinberg::kResultFalse;

        const void* data = nullptr;
        Steinberg::uint32 size = 0;
        if (attrs->getBinary("snapshotData", data, size) != Steinberg::kResultOk)
            return Steinberg::kResultFalse;

        if (size != sizeof(Krate::DSP::HarmonicSnapshot))
            return Steinberg::kResultFalse;

        // Fixed-size copy -- real-time safe (no allocation)
        std::memcpy(&memorySlots_[static_cast<size_t>(slotIndex)].snapshot,
                     data, sizeof(Krate::DSP::HarmonicSnapshot));
        memorySlots_[static_cast<size_t>(slotIndex)].occupied = true;

        return Steinberg::kResultOk;
    }

    return AudioEffect::notify(message);
}
```

### 4. Controller Extensions: `controller.cpp`

Register 3 new parameters in `Controller::initialize()`:

```cpp
// M5 Harmonic Memory parameters (FR-005, FR-006, FR-011)
auto* slotParam = new Steinberg::Vst::StringListParameter(
    STR16("Memory Slot"), kMemorySlotId, nullptr,
    Steinberg::Vst::ParameterInfo::kCanAutomate |
    Steinberg::Vst::ParameterInfo::kIsList);
slotParam->appendString(STR16("Slot 1"));
slotParam->appendString(STR16("Slot 2"));
slotParam->appendString(STR16("Slot 3"));
slotParam->appendString(STR16("Slot 4"));
slotParam->appendString(STR16("Slot 5"));
slotParam->appendString(STR16("Slot 6"));
slotParam->appendString(STR16("Slot 7"));
slotParam->appendString(STR16("Slot 8"));
parameters.addParameter(slotParam);

parameters.addParameter(STR16("Memory Capture"), nullptr, 1, 0,
    Steinberg::Vst::ParameterInfo::kCanAutomate,
    kMemoryCaptureId);

parameters.addParameter(STR16("Memory Recall"), nullptr, 1, 0,
    Steinberg::Vst::ParameterInfo::kCanAutomate,
    kMemoryRecallId);
```

### 5. State Persistence: Version 5

**getState()** -- change version header to 5, append after M4 data:

```cpp
// Version header: M5
streamer.writeInt32(5);

// ... existing M1-M4 data unchanged ...

// --- M5 parameters (harmonic memory) ---
// Selected slot index (int32, 0-7)
const float slotNorm = memorySlot_.load(std::memory_order_relaxed);
streamer.writeInt32(static_cast<Steinberg::int32>(
    std::clamp(static_cast<int>(std::round(slotNorm * 7.0f)), 0, 7)));

// 8 memory slots
for (size_t s = 0; s < 8; ++s)
{
    const auto& slot = memorySlots_[s];
    streamer.writeInt8(slot.occupied
        ? static_cast<Steinberg::int8>(1)
        : static_cast<Steinberg::int8>(0));

    if (slot.occupied)
    {
        const auto& snap = slot.snapshot;
        streamer.writeFloat(snap.f0Reference);
        streamer.writeInt32(static_cast<Steinberg::int32>(snap.numPartials));

        for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
            streamer.writeFloat(snap.relativeFreqs[i]);
        for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
            streamer.writeFloat(snap.normalizedAmps[i]);
        for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
            streamer.writeFloat(snap.phases[i]);
        for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
            streamer.writeFloat(snap.inharmonicDeviation[i]);
        for (size_t i = 0; i < Krate::DSP::kResidualBands; ++i)
            streamer.writeFloat(snap.residualBands[i]);

        streamer.writeFloat(snap.residualEnergy);
        streamer.writeFloat(snap.globalAmplitude);
        streamer.writeFloat(snap.spectralCentroid);
        streamer.writeFloat(snap.brightness);
    }
}
```

Per-occupied-slot binary size: 1 float (f0Reference) + 1 int32 (numPartials) + 48*4 floats (4 arrays) + 16 floats (residualBands) + 4 floats (metadata) = 213 values = 852 bytes. Total worst case (all 8 occupied): 1 int32 (selectedSlot) + 8 * (1 int8 + 852 bytes) = ~6.8 KB appended to state.

**setState()** -- read after M4 data (version >= 5):

```cpp
if (version >= 5)
{
    // Read selected slot
    Steinberg::int32 slotIndex = 0;
    if (streamer.readInt32(slotIndex))
        memorySlot_.store(
            std::clamp(static_cast<float>(slotIndex) / 7.0f, 0.0f, 1.0f));

    // Read 8 memory slots
    for (size_t s = 0; s < 8; ++s)
    {
        Steinberg::int8 occupied = 0;
        if (!streamer.readInt8(occupied))
            break;

        memorySlots_[s].occupied = (occupied != 0);

        if (memorySlots_[s].occupied)
        {
            auto& snap = memorySlots_[s].snapshot;
            bool ok = true;

            ok = ok && streamer.readFloat(snap.f0Reference);
            Steinberg::int32 numP = 0;
            ok = ok && streamer.readInt32(numP);
            snap.numPartials = std::clamp(static_cast<int>(numP), 0,
                static_cast<int>(Krate::DSP::kMaxPartials));

            for (size_t i = 0; i < Krate::DSP::kMaxPartials && ok; ++i)
                ok = streamer.readFloat(snap.relativeFreqs[i]);
            for (size_t i = 0; i < Krate::DSP::kMaxPartials && ok; ++i)
                ok = streamer.readFloat(snap.normalizedAmps[i]);
            for (size_t i = 0; i < Krate::DSP::kMaxPartials && ok; ++i)
                ok = streamer.readFloat(snap.phases[i]);
            for (size_t i = 0; i < Krate::DSP::kMaxPartials && ok; ++i)
                ok = streamer.readFloat(snap.inharmonicDeviation[i]);
            for (size_t i = 0; i < Krate::DSP::kResidualBands && ok; ++i)
                ok = streamer.readFloat(snap.residualBands[i]);

            ok = ok && streamer.readFloat(snap.residualEnergy);
            ok = ok && streamer.readFloat(snap.globalAmplitude);
            ok = ok && streamer.readFloat(snap.spectralCentroid);
            ok = ok && streamer.readFloat(snap.brightness);

            if (!ok)
            {
                memorySlots_[s].occupied = false;
                memorySlots_[s].snapshot = {};
                break;
            }
        }
        else
        {
            memorySlots_[s].snapshot = {};
        }
    }
}
else
{
    // Default: all slots empty, slot 0 selected
    for (auto& slot : memorySlots_)
    {
        slot.occupied = false;
        slot.snapshot = {};
    }
    memorySlot_.store(0.0f);
}
```

**setComponentState()** (Controller) -- version >= 5: read slot index, skip binary snapshot data:

```cpp
if (version >= 5)
{
    Steinberg::int32 slotIndex = 0;
    if (streamer.readInt32(slotIndex))
    {
        setParamNormalized(kMemorySlotId,
            static_cast<double>(std::clamp(
                static_cast<float>(slotIndex) / 7.0f, 0.0f, 1.0f)));
    }

    // Skip 8 memory slots (controller does not need snapshot data)
    for (size_t s = 0; s < 8; ++s)
    {
        Steinberg::int8 occupied = 0;
        if (!streamer.readInt8(occupied))
            break;

        if (occupied != 0)
        {
            // Skip snapshot binary: 1 float (f0Reference) + 1 int32 (numPartials)
            //   + 212 floats (48*4 relativeFreqs/normalizedAmps/phases/inharmonicDeviation
            //                 + 16 residualBands + 4 metadata) = 213 reads total
            float skipF = 0.0f;
            Steinberg::int32 skipI = 0;
            streamer.readFloat(skipF);  // f0Reference (1 float)
            streamer.readInt32(skipI);  // numPartials (1 int32)
            // 48*4 + 16 + 4 = 212 floats
            for (size_t i = 0; i < 48 * 4 + 16 + 4; ++i)
                streamer.readFloat(skipF);
        }
    }

    // Capture and Recall are momentary triggers -- default to 0
    setParamNormalized(kMemoryCaptureId, 0.0);
    setParamNormalized(kMemoryRecallId, 0.0);
}
else
{
    // Default M5 values for older states
    setParamNormalized(kMemorySlotId, 0.0);
    setParamNormalized(kMemoryCaptureId, 0.0);
    setParamNormalized(kMemoryRecallId, 0.0);
}
```

### 6. JSON Serialization (P3): `harmonic_snapshot_json.h`

**Location**: `plugins/innexus/src/dsp/harmonic_snapshot_json.h`

Plugin-local JSON utilities for human-readable export/import. Uses manual string formatting (no external JSON library dependency). This keeps the build simple and avoids adding a third-party JSON library.

**Export format**:

```json
{
    "version": 1,
    "f0Reference": 440.0,
    "numPartials": 12,
    "relativeFreqs": [1.0, 2.001, 3.003],
    "normalizedAmps": [0.707, 0.500, 0.354],
    "phases": [0.0, 1.57, 3.14],
    "inharmonicDeviation": [0.0, 0.001, 0.003],
    "residualBands": [0.01, 0.02, 0.015, 0.012, 0.01, 0.008, 0.006, 0.005, 0.004, 0.003, 0.002, 0.002, 0.001, 0.001, 0.001, 0.001],
    "residualEnergy": 0.05,
    "globalAmplitude": 0.3,
    "spectralCentroid": 2200.0,
    "brightness": 0.6
}
```

Export writes only `numPartials` entries for per-partial arrays (not all 48) for readability. Import validates: version field present (== 1), numPartials <= 48, all required arrays present with correct length, no negative amplitudes. On import, per-partial arrays are zero-padded to kMaxPartials.

**Functions**:
- `std::string snapshotToJson(const Krate::DSP::HarmonicSnapshot& snap)` -- export
- `bool jsonToSnapshot(const std::string& json, Krate::DSP::HarmonicSnapshot& out)` -- import with validation, returns false on failure

**Controller-side import dispatch** (FR-025, FR-029):

The controller is responsible for the full import pipeline: reading the file from disk, parsing via `jsonToSnapshot()`, and dispatching to the processor via `IMessage`. Add the following method to `Controller` in `controller.cpp`:

```cpp
/// Triggered off the audio thread (e.g., by a test helper or future UI button).
/// Reads a JSON file, parses it, and dispatches the snapshot to the processor
/// via IMessage so the audio thread never touches file I/O.
bool Controller::importSnapshotFromJson(const std::string& filePath, int slotIndex)
{
    // Read file
    std::ifstream f(filePath);
    if (!f.is_open()) return false;
    const std::string json((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());

    // Parse
    Krate::DSP::HarmonicSnapshot snap{};
    if (!Innexus::jsonToSnapshot(json, snap)) return false;

    // Dispatch to processor via IMessage (FR-029)
    auto* msg = allocateMessage();
    if (!msg) return false;
    msg->setMessageID("HarmonicSnapshotImport");
    auto* attrs = msg->getAttributes();
    attrs->setInt("slotIndex", static_cast<Steinberg::int64>(slotIndex));
    attrs->setBinary("snapshotData", &snap, sizeof(snap));
    sendMessage(msg);
    msg->release();
    return true;
}
```

For M5, `importSnapshotFromJson()` is called from tests and can be exposed as a controller method. The file dialog is deferred to Milestone 7.

---

## Implementation Phases

> **Phase numbering note**: This plan uses 6 phases (Phases 1-6). The corresponding `tasks.md` uses 7 task phases for finer granularity. The mapping is: plan Phase 1 (HarmonicSnapshot struct + utilities) → task Phases 2+3; plan Phase 2 (parameter IDs + registration) → task Phase 2 (Foundational, shared with plan Phase 1); plan Phase 3 (capture logic) → task Phase 3 (US1); plan Phase 4 (recall logic) → task Phase 4 (US2); plan Phase 5 (state persistence) → task Phase 5 (US3); plan Phase 6 (JSON + integration) → task Phases 6+7. When cross-referencing, use the task ID (T001-T113) as the canonical identifier.

### Phase 1: HarmonicSnapshot Data Structure & Conversion Utilities

1. Create `dsp/include/krate/dsp/processors/harmonic_snapshot.h`
2. Implement `HarmonicSnapshot` struct, `MemorySlot` struct
3. Implement `captureSnapshot()` and `recallSnapshotToFrame()`
4. Write unit tests: `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`
   - Test `captureSnapshot()` field extraction (relativeFreqs, phases, inharmonicDeviation match source)
   - Test L2 normalization of amplitudes (SC-001: within 1e-6)
   - Test `captureSnapshot()` with empty frame (0 partials)
   - Test `captureSnapshot()` residual data extraction
   - Test `recallSnapshotToFrame()` reconstructs valid HarmonicFrame
   - Test `recallSnapshotToFrame()` sets harmonicIndex correctly from relativeFreq - deviation
   - Test `recallSnapshotToFrame()` sets f0Confidence = 1.0 and stability = 1.0
   - Test round-trip: capture -> recall -> compare fields (SC-001)
   - Test `captureSnapshot()` metadata (f0Reference, globalAmplitude, spectralCentroid, brightness)
5. Add test file to `dsp/tests/CMakeLists.txt`
6. Build and verify all tests pass

### Phase 2: Parameter IDs, Registration, and Processor Memory Slots

1. Add `kMemorySlotId = 304`, `kMemoryCaptureId = 305`, `kMemoryRecallId = 306` to `plugin_ids.h`
2. Add `#include <krate/dsp/processors/harmonic_snapshot.h>` to `processor.h`
3. Add memory slot member variables to `processor.h`:
   - `std::array<Krate::DSP::MemorySlot, 8> memorySlots_{}`
   - `std::atomic<float> memorySlot_{0.0f}`, `memoryCapture_{0.0f}`, `memoryRecall_{0.0f}`
   - `float previousCaptureTrigger_ = 0.0f`, `previousRecallTrigger_ = 0.0f`
   - Test accessors: `getMemorySlot(int)`, `getSelectedSlotIndex()`
4. Add parameter handling in `processParameterChanges()` for 3 new parameters
5. Register parameters in `controller.cpp` (StringListParameter for slot, step-1 toggles for capture/recall)
6. Write VST parameter tests: `plugins/innexus/tests/unit/vst/harmonic_memory_vst_tests.cpp`
   - Test parameter registration (3 new params exist)
   - Test Memory Slot has 8 entries
   - Test Memory Capture/Recall are step-1 toggles
7. Build and verify pluginval passes

### Phase 3: Capture Logic (Processor)

1. Implement capture trigger detection in `process()`:
   - Detect 0->1 transition of `memoryCapture_`
   - Determine capture source based on current state (freeze/morph/sidechain/sample)
   - Call `captureSnapshot()` and store in selected slot
2. Write capture integration tests: `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
   - Test capture stores snapshot in selected slot (SC-001)
   - Test capture from live analysis (sidechain simulation)
   - Test capture from sample playback frame
   - Test capture from frozen frame (manual freeze active)
   - Test capture during morph blend (FR-008: post-morph state)
   - Test capture with filter active stores pre-filter data (FR-009)
   - Test capture with no analysis produces empty snapshot
   - Test capture overwrites existing slot
   - Test slot independence (SC-009: capture into N does not affect others)
   - Test rapid captures (last one wins)
3. Build and verify all tests pass

### Phase 4: Recall Logic & Freeze Integration (Processor)

1. Implement recall trigger detection in `process()`:
   - Detect 0->1 transition of `memoryRecall_`
   - If slot empty: silently ignore (FR-013)
   - If slot occupied: `recallSnapshotToFrame()`, load into freeze infrastructure
   - If already frozen (slot-to-slot): initiate crossfade (FR-015)
2. Write recall integration tests (add to `harmonic_memory_tests.cpp`):
   - Test recall loads snapshot into manualFrozenFrame_ (FR-012)
   - Test recall engages manual freeze (FR-012d)
   - Test recall on empty slot is silently ignored (FR-013)
   - Test recall integrates with morph (FR-016: morph 0.0 = recalled, 1.0 = live)
   - Test recall integrates with harmonic filter (FR-017)
   - Test recall disengage via freeze toggle returns to live (FR-018)
   - Test slot-to-slot recall crossfade (SC-003: < 10ms, no click)
   - Test capture into recalled slot does NOT update manualFrozenFrame_ (edge case)
   - Test selecting different slot without recall does not change playback (edge case)
   - Test CPU timing (SC-006: < 50 microseconds each)
3. Build and verify all tests pass

### Phase 5: State Persistence v5

1. Update `getState()`: change version to 5, append memory slot data after M4 payload
2. Update `setState()`: read memory slots when version >= 5, defaults for version < 5
3. Update `setComponentState()` in Controller: read/skip M5 data
4. Write state persistence tests (add to `harmonic_memory_tests.cpp`):
   - Test v5 state round-trip: populate slots, save, reload, compare all fields (SC-004)
   - Test v4 backward compatibility: load v4 state, verify all slots empty and M4 params restored (SC-005)
   - Test selected slot index persists
   - Test freeze/recall state does NOT persist (FR-023)
   - Test partial slot occupancy round-trips (some occupied, some empty)
5. Build, test, pluginval

### Phase 6: JSON Export/Import (P3) & Integration Testing

1. Create `plugins/innexus/src/dsp/harmonic_snapshot_json.h`:
   - `std::string snapshotToJson(const HarmonicSnapshot&)` -- export
   - `bool jsonToSnapshot(const std::string&, HarmonicSnapshot&)` -- import with validation
2. Add `notify()` override to processor (FR-029):
   - Handle "HarmonicSnapshotImport" message with binary payload
   - Fixed-size copy into target slot
3. Write JSON and IMessage tests (add to `harmonic_memory_tests.cpp` or separate file):
   - Test JSON export produces valid JSON with all fields (FR-024)
   - Test JSON import round-trip (SC-010: within 1e-6 per field)
   - Test JSON import of malformed data fails gracefully (FR-026)
   - Test JSON import with out-of-range values fails gracefully (numPartials > 48, negative amps)
   - Test `notify()` handler writes snapshot into correct slot
   - Test `notify()` handler rejects wrong binary size
4. Full integration test: capture -> recall -> morph -> filter -> playback pipeline
5. CPU measurement test (SC-006: < 50 microseconds each)
6. Real-time safety verification (SC-007: code review, no allocations on audio thread)
7. Pluginval at strictness level 5 (SC-008)
8. Update architecture documentation at `specs/_architecture_/` (Constitution Principle XIV)

---

## Test Strategy

### Unit Tests (DSP Layer)

**File**: `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`
- `captureSnapshot` field extraction accuracy (relativeFreqs, phases, inharmonicDeviation)
- `captureSnapshot` L2 normalization correctness
- `captureSnapshot` with empty frame (0 partials)
- `captureSnapshot` metadata extraction
- `captureSnapshot` residual data
- `recallSnapshotToFrame` field reconstruction
- `recallSnapshotToFrame` harmonicIndex derivation
- `recallSnapshotToFrame` default values (confidence, stability, age)
- Round-trip: capture -> recall -> compare (SC-001: within 1e-6)

### Integration Tests (Plugin Layer)

**File**: `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- Capture from various sources (live, sample, frozen, morphed)
- Capture pre-filter data (FR-009)
- Capture slot independence (SC-009)
- Recall loads into freeze infrastructure (FR-012)
- Recall on empty slot ignored (FR-013)
- Recall engages freeze (FR-012d)
- Slot-to-slot crossfade (SC-003: no click, < 10ms)
- Morph with recalled snapshot (FR-016)
- Harmonic filter on recalled data (FR-017)
- Freeze disengage after recall (FR-018)
- State v5 round-trip (SC-004)
- State v4 backward compatibility (SC-005)
- Recall state not persisted (FR-023)
- CPU timing (SC-006)
- JSON export/import round-trip (SC-010)
- JSON malformed input rejection (FR-026)
- IMessage notify() handler

### VST Tests

**File**: `plugins/innexus/tests/unit/vst/harmonic_memory_vst_tests.cpp`
- Parameter registration for all 3 new params
- Memory Slot has 8 entries with correct names
- Memory Capture/Recall are step-1 (momentary) parameters
- Parameter normalization ranges correct

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Capture source selection is complex (5 cases) | MEDIUM | MEDIUM | Clearly document source priority in code comments; unit test all 5 paths |
| Slot-to-slot recall crossfade clicks | LOW | HIGH | Reuse existing manualFreezeRecovery mechanism; SC-003 test verifies |
| State v5 breaks v4 backward compatibility | LOW | HIGH | Explicit version check; v4 loads with empty slots |
| JSON parser vulnerability (malformed input) | LOW | MEDIUM | Validate all fields before accepting; no allocation on failure |
| notify() / process() thread safety | MEDIUM | MEDIUM | Write occupied flag AFTER snapshot copy; see Complexity Tracking |
| L2 normalization div-by-zero | LOW | HIGH | Guard with `if (sumSquares > 0.0f)` check |

---

## Project Structure

### Documentation (this feature)

```text
specs/119-harmonic-memory/
+-- plan.md              # This file
+-- spec.md              # Feature specification
```

### Source Code (repository root)

```text
dsp/include/krate/dsp/processors/
+-- harmonic_snapshot.h         # NEW: HarmonicSnapshot, MemorySlot, captureSnapshot(), recallSnapshotToFrame()

dsp/tests/unit/processors/
+-- harmonic_snapshot_tests.cpp # NEW: Unit tests for snapshot capture/recall

plugins/innexus/src/
+-- plugin_ids.h                # MODIFIED: Add kMemorySlotId(304), kMemoryCaptureId(305), kMemoryRecallId(306)
+-- dsp/
|   +-- harmonic_snapshot_json.h  # NEW (P3): JSON export/import utilities
+-- processor/
|   +-- processor.h             # MODIFIED: Add memory slot array, parameter atomics, notify() override, test accessors
|   +-- processor.cpp           # MODIFIED: Capture/recall logic in process(), state v5, parameter handling, notify() handler
+-- controller/
    +-- controller.cpp          # MODIFIED: Register 3 new parameters, setComponentState v5

plugins/innexus/tests/
+-- CMakeLists.txt              # MODIFIED: Add new test files
+-- unit/processor/
|   +-- harmonic_memory_tests.cpp    # NEW: Capture/recall integration tests, state persistence, JSON, CPU timing
+-- unit/vst/
    +-- harmonic_memory_vst_tests.cpp # NEW: VST parameter registration tests
```

**Structure Decision**: Follows existing monorepo pattern. `HarmonicSnapshot` struct and conversion utilities in shared DSP library at Layer 2 (3+ future consumers identified). JSON serialization and memory slot management are plugin-local. Tests split between DSP unit tests and plugin integration tests.

## Complexity Tracking

No constitution violations. All design decisions align with established patterns.

**Thread safety note for `notify()` / `process()` interaction**: The `notify()` method is called from the UI thread, while `process()` runs on the audio thread. Both access `memorySlots_[]`. The risk of torn reads is mitigated by:
1. Writing the `occupied` flag AFTER the snapshot copy completes in `notify()`
2. The snapshot copy is a fixed-size `memcpy` that completes in microseconds
3. The worst case (process() reads a partially-written snapshot during a concurrent JSON import) would produce one frame of garbled harmonics, which is an extremely rare user action and the next frame would be correct
4. This matches the Ruinae pattern for `VoiceModRouteUpdate` which uses the same approach (non-atomic struct writes in `notify()`, reads in `process()`)

If stricter guarantees are needed in the future, a double-buffer or atomic flag can be added.
