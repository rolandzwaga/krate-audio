# Research: Arpeggiator Engine Integration (071)

**Date**: 2026-02-21
**Spec**: `specs/071-arp-engine-integration/spec.md`

## Research Summary

All unknowns from the Technical Context have been resolved through codebase analysis. No external research was needed -- this is a pure integration task following established patterns.

---

## R-001: Parameter Pattern -- How does Ruinae register, handle, serialize, and format parameters?

**Decision**: Follow the exact `trance_gate_params.h` pattern for all 6 functions in `arpeggiator_params.h`.

**Rationale**: Every parameter section in Ruinae (global, osc A/B, mixer, filter, distortion, trance gate, amp env, filter env, mod env, LFO1/2, chaos, mod matrix, delay, reverb, phaser, harmonizer, mono mode, macros, rungler, settings, env follower, sample hold, random, pitch follower, transient) follows the identical pattern:
1. Atomic struct (`XxxParams`) with `std::atomic` fields + defaults
2. `handleXxxParamChange()` -- denormalizes VST 0-1 values to plain values
3. `registerXxxParams()` -- registers parameters with readable names
4. `formatXxxParam()` -- formats values for host display
5. `saveXxxParams()` / `loadXxxParams()` -- state serialization
6. `loadXxxParamsToController()` -- controller-side state restore with normalized values

All functions are `inline` in the header file and use template `SetParamFunc` for the controller load function.

**Alternatives considered**: None -- deviation from the established pattern would be incorrect.

---

## R-002: Parameter ID Range -- Where do arp parameters fit in the ID space?

**Decision**: Use IDs 3000-3010 (Arpeggiator range) with `kArpBaseId = 3000` and `kArpEndId = 3099`.

**Rationale**: The current `plugin_ids.h` ends at `kNumParameters = 2900` (harmonizer ends at 2899). The range 2900-2999 is unallocated, and the spec explicitly assigns 3000-3099 for arpeggiator. `kNumParameters` must be bumped to 3100 (or higher) to include the new IDs.

**Alternatives considered**: Using 2900-2910 would work but the spec explicitly specifies 3000-3099 for forward compatibility with Phase 4 lane parameters (3020-3199).

---

## R-003: MIDI Routing -- How does processEvents() currently work and how to add arp branching?

**Decision**: Modify `processEvents()` to branch on `arpParams_.enabled`:
- When enabled: route note-on to `arpCore_.noteOn(pitch, velocity)`, note-off to `arpCore_.noteOff(pitch)`
- When disabled: route directly to `engine_.noteOn()`/`engine_.noteOff()` (current behavior)

**Rationale**: The current `processEvents()` (processor.cpp:1171-1208) is a simple event loop that maps VST events to `engine_.noteOn()`/`engine_.noteOff()`. Adding a conditional branch on `arpParams_.enabled` is the minimal change. The arp state is read from the atomic struct with `memory_order_relaxed` (same pattern as all other param reads on audio thread).

**Alternatives considered**:
- Routing through an intermediate event buffer: Unnecessary complexity -- ArpeggiatorCore manages its own internal state
- Making the engine aware of the arp: Violates layering -- the arp is a MIDI preprocessor, not part of the synth engine

---

## R-004: Block Processing Call Order -- Where does ArpeggiatorCore::processBlock() fit?

**Decision**: Insert arp processing between `processEvents()` and `engine_.processBlock()` in `Processor::process()`. The order is:
1. `processParameterChanges()` (existing)
2. `applyParamsToEngine()` (existing, extended for arp params)
3. Build BlockContext (existing)
4. `processEvents()` (modified to route to arp or engine)
5. **NEW**: Arp processBlock + route events to engine
6. `engine_.processBlock()` (existing)

**Rationale**: The spec (FR-007) requires: `applyParamsToEngine()` -> `processEvents()` -> `processBlock()` -> route ArpEvents. This matches the current processor flow. The BlockContext must be built before arp processBlock since it needs tempo/transport info. The BlockContext is currently passed to the engine via `engine_.setBlockContext(ctx)` -- the arp needs the same context passed to its `processBlock()`.

**Key detail**: The arp needs its own copy of the BlockContext (or a reference) since it uses `ctx.tempoBPM`, `ctx.sampleRate`, `ctx.isPlaying`, and `ctx.transportPositionSamples`. The current code builds the BlockContext and passes it to the engine. We need to store it in a local variable and pass to both arp and engine.

**Alternatives considered**: None -- the spec is explicit about call order.

---

## R-005: Transport Stop/Start Handling -- How to detect transport state changes?

**Decision**: Track previous `isPlaying` state. When transport transitions from playing to stopped, call `arpCore_.reset()`. The held-note buffer is preserved by reset (it only resets timing).

**Rationale**: ArpeggiatorCore::reset() (line 121-134 in arpeggiator_core.h) resets the step counter, sample counter, swing step counter, sets `wasPlaying_` to false, and `firstStepPending_` to true. It also sends note-offs for any currently-sounding arp notes via `needsDisableNoteOff_`. It does NOT clear the held-note buffer -- matching the spec requirement (FR-018).

The processor already tracks `isTransportPlaying_` as a `std::atomic<bool>` for the trance gate UI. We need a non-atomic local variable to detect transitions (atomic is for UI thread communication).

**Alternatives considered**: Using a separate `wasArpPlaying_` member -- the existing `isTransportPlaying_` atomic serves UI, so we need a separate processor-side bool anyway.

---

## R-006: State Serialization Position -- Where to append arp state in the stream?

**Decision**: Append arp state after the last existing state data (harmonizer enable flag) in both `getState()` and `setState()`.

**Rationale**: The state format is sequential with no random access. New data must be appended at the end. The existing pattern (phaser, extended LFO, macros, rungler, settings, mod sources, harmonizer) all follow this append-at-end pattern. The controller's `setComponentState()` must mirror this order exactly.

---

## R-007: ArpEvent Buffer -- Pre-allocation strategy

**Decision**: Declare `std::array<Krate::DSP::ArpEvent, 128> arpEvents_{}` as a processor member. Pass pointer and count to the arp processBlock routing loop.

**Rationale**: The spec requires exactly 128 entries (worst case: 16 notes * 4 octaves chord mode + 64 overlapping note-offs). `std::array` is stack-allocated (no heap), fixed-size, and zero-initialized. ArpeggiatorCore::processBlock() takes `std::span<ArpEvent>` -- a `std::array<ArpEvent, 128>` implicitly converts to span. The method returns the number of events written.

**Alternatives considered**: Using a `std::vector` -- forbidden on audio thread (Constitution Principle II).

---

## R-008: UI Layout -- Where does the arp section fit in the SEQ tab?

**Decision**: The SEQ tab (Tab_Seq) is 1400x620 pixels. The Trance Gate section currently occupies a FieldsetContainer at `origin="8, 4" size="1384, 612"`. This needs to be split: Trance Gate takes the top portion, a horizontal divider separates them, and the Arpeggiator section takes the bottom portion.

**Rationale**: The spec (FR-014) explicitly states "below the existing TRANCE GATE section, separated by a horizontal divider." The arp controls are basic: 1 toggle, 4 dropdowns, 1 visibility-toggle pair (note value / free rate), 3 knobs. This fits comfortably in approximately 200px of vertical space. The trance gate section can be reduced from 612px to ~390px (the step pattern editor and knobs don't need the full height), leaving ~220px for the arp section.

**Alternatives considered**: Placing arp in a separate tab -- rejected by spec clarification.

---

## R-009: Sync/Rate/NoteValue Visibility Toggle -- Pattern to follow

**Decision**: Follow the exact same pattern as LFO1, LFO2, Chaos, S&H, Random, Delay, Phaser, and Trance Gate sync toggles in the controller:
1. In `editor.uidesc`: Two `CViewContainer` groups with `custom-view-name` attributes (e.g., `ArpRateGroup` and `ArpNoteValueGroup`), one visible and one hidden by default
2. In `controller.cpp` `setParamNormalized()`: Toggle visibility based on `kArpTempoSyncId`
3. In `controller.h`: Add pointer members `arpRateGroup_` and `arpNoteValueGroup_`
4. In `controller.cpp` `didOpen()`/`willClose()`: Wire up custom view pointers

**Rationale**: This pattern is used 7+ times already in the controller. Consistency is critical for maintainability.

**Alternatives considered**: Using UIViewSwitchContainer -- would work but is not the pattern used by existing sync toggles.

---

## R-010: ArpeggiatorCore API Verification

**Decision**: All ArpeggiatorCore setter methods verified from header (arpeggiator_core.h):

| Method | Signature | Parameter Source |
|--------|-----------|-----------------|
| `prepare()` | `void prepare(double sampleRate, size_t maxBlockSize)` | setupProcessing |
| `reset()` | `void reset()` | Transport stop |
| `noteOn()` | `void noteOn(uint8_t note, uint8_t velocity)` | processEvents |
| `noteOff()` | `void noteOff(uint8_t note)` | processEvents |
| `setEnabled()` | `void setEnabled(bool enabled)` | applyParamsToEngine |
| `setMode()` | `void setMode(ArpMode mode)` | applyParamsToEngine |
| `setOctaveRange()` | `void setOctaveRange(int range)` | applyParamsToEngine |
| `setOctaveMode()` | `void setOctaveMode(OctaveMode mode)` | applyParamsToEngine |
| `setTempoSync()` | `void setTempoSync(bool sync)` | applyParamsToEngine |
| `setNoteValue()` | `void setNoteValue(NoteValue note, NoteModifier mod)` | applyParamsToEngine |
| `setFreeRate()` | `void setFreeRate(float hz)` | applyParamsToEngine |
| `setGateLength()` | `void setGateLength(float percent)` | applyParamsToEngine |
| `setSwing()` | `void setSwing(float percent)` | applyParamsToEngine |
| `setLatchMode()` | `void setLatchMode(LatchMode mode)` | applyParamsToEngine |
| `setRetrigger()` | `void setRetrigger(ArpRetriggerMode mode)` | applyParamsToEngine |
| `processBlock()` | `size_t processBlock(const BlockContext& ctx, std::span<ArpEvent> outputEvents) noexcept` | process() |

**Key constants**: `kMaxEvents = 128`, `kMinFreeRate = 0.5f`, `kMaxFreeRate = 50.0f`, `kMinGateLength = 1.0f`, `kMaxGateLength = 200.0f`, `kMinSwing = 0.0f`, `kMaxSwing = 75.0f`.

**Rationale**: Reading the actual header prevents API mismatch errors.
