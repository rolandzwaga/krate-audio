# Research: Arpeggiator Modulation Integration

**Feature**: 078-modulation-integration
**Date**: 2026-02-24

## Research Questions & Findings

### R1: How does the ModulationEngine output become available to the processor?

**Decision**: Read offsets via `engine_.getGlobalModOffset(RuinaeModDest::Arp*)` in `applyParamsToEngine()`.

**Rationale**: The existing pattern (used by all 10 global destinations) reads mod offsets in `RuinaeEngine::processBlock()` at step 4 (line 701 of ruinae_engine.h). However, the arp modulation application must happen in `Processor::applyParamsToEngine()` because:
1. The arp parameters are set on `arpCore_` (a separate object from `engine_`), not on the engine.
2. The processor already calls `engine_.getGlobalModOffset()` after `engine_.processBlock()` for the morph pad UI animation (processor.cpp line 268).
3. The 1-block latency (reading previous block's offsets) is accepted per spec and matches all existing destinations.

**Alternatives considered**:
- Reading offsets inside `RuinaeEngine::processBlock()` and forwarding to arpCore_ via a new interface: Rejected because arpCore_ is owned by the Processor, not the RuinaeEngine.
- Adding arpCore_ to RuinaeEngine: Rejected because the arp is architecturally separate from the synth engine (it generates MIDI events that feed the engine).

### R2: How to handle tempo-sync rate modulation?

**Decision**: When rateOffset != 0 and tempoSync is on, compute the effective step duration from the note value + BPM, apply the scaling factor `1/(1 + 0.5 * offset)`, convert to Hz, and use `setFreeRate()` with `setTempoSync(false)` for that block.

**Rationale**: ArpeggiatorCore computes step duration internally when tempoSync=true using the noteValue and BPM from BlockContext. There is no public API to override the step duration directly while keeping tempoSync=true. The cleanest approach is to calculate what the step duration WOULD be, apply the mod scaling, convert to Hz (1/duration), and pass as a free rate. This gives exactly the modulated timing described in FR-014 and US1 scenario 5.

**Alternatives considered**:
- Adding a `setStepDurationOverride()` to ArpeggiatorCore: Rejected because it would modify a DSP library component for a plugin-specific feature, violating the layer boundary.
- Always computing free rate from noteValue+BPM (ignoring ArpeggiatorCore's internal sync): This IS the chosen approach, cleanly expressed.
- Scaling the BPM passed in BlockContext: Rejected because BlockContext is shared with the engine and all voices.

### R3: Should mod offset reads be inside or outside the "arp enabled" guard?

**Decision**: Read offsets only when arp is enabled (FR-015 optimization).

**Rationale**: FR-015 states the processor "MAY skip reading arp modulation offsets as an optimization." Since the offsets are only meaningful when the arp is active, and the mod engine continues computing them regardless, there is no state to "warm up." The first block after re-enable will read the most recent offset (max 1-block staleness), which is identical to normal operation.

**Alternatives considered**:
- Always reading offsets (even when arp disabled): Acceptable per spec but wastes 5 array lookups per block. The optimization is trivial to implement.

### R4: How does the change-detection for octaveRange interact with modulation?

**Decision**: Track the EFFECTIVE (modulated) octave range in `prevArpOctaveRange_`, not the base value.

**Rationale**: `setOctaveRange()` triggers a selector reset in ArpeggiatorCore, which can cause pattern discontinuity. The existing code (line 1223-1227) already tracks `prevArpOctaveRange_` to avoid calling setOctaveRange when the value hasn't changed. With modulation, the effective value can change even when the base value doesn't (due to the mod offset changing between blocks). By comparing the effective value (base + round(3*offset)) against prevArpOctaveRange_, we correctly call setOctaveRange only when the actual octave range changes. This is exactly what FR-010 prescribes.

**Alternatives considered**:
- Tracking base and offset separately: Unnecessary complexity; the effective value comparison is sufficient.

### R5: Do any UI components need changes beyond the array extensions?

**Decision**: No UI code changes needed beyond the array/constant extensions.

**Rationale**: Verified by reading:
- `ModMatrixGrid` iterates over `kNumGlobalDestinations` to populate dropdowns and grid cells.
- `ModHeatmap` uses `kNumGlobalDestinations` for destination count.
- `appendDestStrings()` in dropdown_mappings.h iterates over `kGlobalDestNames`.
- All UI components use the central registry arrays with size-driven iteration, so adding entries to those arrays automatically extends the UI.

### R6: Does state serialization need any changes?

**Decision**: No serialization changes needed.

**Rationale**: Verified by reading the mod matrix serialization code:
- Mod routing data (source, destination, amount, curve, smooth per slot) is serialized by the existing mod matrix parameter infrastructure.
- The destination field is stored as a raw integer index. Old presets with no arp destination routings will simply have no routings targeting indices 10-14.
- Arp parameter serialization (arpeggiator_params.h saveArpParams/loadArpParams) is unchanged because no new arp parameters are added.

### R7: What test infrastructure exists for testing mod destinations?

**Decision**: Use the existing mock parameter pipeline pattern from `mod_source_pipeline_test.cpp` and `arp_integration_test.cpp`.

**Rationale**: Both test files demonstrate the pattern needed:
- Mock IParamValueQueue, IParameterChanges for sending parameter values
- Mock IEventList for MIDI events
- Direct Processor instantiation with initialize/setupProcessing
- Processing blocks via `data.process()`
- Using Macro source (deterministic value control) for reliable modulation testing

The most reliable test approach uses Macro 1 as the modulation source because:
1. Macro value = direct output (no oscillation, no envelope following)
2. Setting Macro 1 to 0.5 with amount=1.0 gives offset=0.5 deterministically
3. This avoids LFO phase issues in tests
