# Research: Per-Step Modifiers (Slide, Accent, Tie, Rest)

**Date**: 2026-02-21
**Phase**: 0 (Outline & Research)

## R1: TB-303 Modifier Behavior Model

**Question**: How do TB-303 modifiers (Slide, Accent, Tie) interact, and what is the correct priority order?

**Decision**: Implement the following priority chain: Rest > Tie > Slide > Accent.

**Rationale**: The original TB-303 had three per-step flags: Accent, Slide, and Note/Rest. Tie was not a TB-303 concept per se (it was achieved via slide without pitch change), but in modern arpeggiator implementations, Tie is treated as a distinct "sustain previous note" modifier. The priority chain ensures:
- Rest (inactive step) always silences regardless of other flags
- Tie sustains the previous note (no new noteOn, so Slide and Accent are moot)
- Slide emits a legato noteOn (no preceding noteOff, envelope not retriggered)
- Accent boosts velocity on any step that actually emits a noteOn (Active or Slide)

**Alternatives considered**:
- Flat evaluation (all flags checked independently): Rejected because Tie+Slide creates an ambiguous state (sustain previous vs glide to new pitch). Priority chain resolves this.
- TB-303 exact emulation (only Accent+Slide, no Tie/Rest): Rejected because modern implementations benefit from all four flags and this system already extends beyond pure 303 emulation.

## R2: Bitmask vs. Enum for Step Modifiers

**Question**: Should per-step modifiers use a bitmask (multiple flags per step) or an enum (one state per step)?

**Decision**: Use a `uint8_t` bitmask with `ArpStepFlags` enum values.

**Rationale**: A bitmask allows combinations like Slide+Accent on a single step, which is musically useful (accented slide = loud glide, the signature acid sound). An enum would limit each step to a single modifier. The bitmask fits in `uint8_t` (4 flags = 4 bits out of 8) and directly stores in `ArpLane<uint8_t>` without any template changes.

**Alternatives considered**:
- `uint8_t` enum with compound values (e.g., SlideAccent = 0x0C): Rejected because it requires enumeration of all combinations, making the enum large and fragile.
- Separate boolean lanes per flag: Rejected because it would require 4 additional lanes (4 x 32 = 128 more parameters) instead of 1 lane with 32 uint8_t parameters. The bitmask is far more parameter-efficient.

## R3: Legato Flag in ArpEvent vs. Separate Event Type

**Question**: How should slide steps signal the engine to suppress envelope retrigger and apply portamento?

**Decision**: Add a `bool legato{false}` field to the existing `ArpEvent` struct.

**Rationale**: A boolean field is simpler and more composable than adding a new event type (e.g., `ArpEvent::Type::LegatoNoteOn`). The legato flag can be set on any NoteOn event regardless of its origin, making the system extensible. The engine already has legato handling in MonoHandler (the `retrigger` flag from `MonoEvent`), so the concept maps naturally. Adding a new Type would require changing all switch statements on ArpEvent::Type throughout the codebase.

**Alternatives considered**:
- New `ArpEvent::Type::LegatoNoteOn` enum value: Rejected because it forces all event consumers to add a new case, and the behavioral difference is small (just suppress retrigger + apply portamento).
- Encoding in velocity (e.g., velocity=0 means legato): Rejected because it destroys velocity information and is a fragile encoding.

## R4: Slide Implementation in Poly Mode

**Question**: How does slide/portamento work in Poly mode where MonoHandler is bypassed?

**Decision**: The slide time parameter is set on both MonoHandler (for Mono mode) AND directly on each RuinaeVoice (for Poly mode). The legato noteOn in Poly mode is routed to the same voice currently sounding the previous arp note.

**Rationale**: In Mono mode, the MonoHandler already handles portamento -- when it receives a noteOn with `legato_` enabled and `hadPreviousNote_` true, it calls `updatePortamentoTarget()` which triggers the portamento ramp. In Poly mode, the engine's `dispatchPolyNoteOn()` needs to be extended: when `legato=true`, instead of allocating a new voice via VoiceAllocator, the engine finds the voice currently playing the previous arp note and calls `setFrequency()` on it (same pattern as MonoHandler's legato path). The voice needs its own portamento mechanism for this to produce a glide rather than an instant jump.

**Key insight**: RuinaeVoice does NOT currently have a per-voice portamento ramp. It has `setFrequency()` which sets the target frequency instantly. For Phase 5, a simple approach is to route the slide noteOn through the voice's `setFrequency()` and accept an instant pitch change in Poly mode. Full per-voice portamento in Poly mode would require adding a portamento ramp to RuinaeVoice, which is a larger change. The spec says "The slide time MUST be forwarded to both MonoHandler AND each RuinaeVoice directly" -- this implies RuinaeVoice needs a portamento setter.

**Decision refined**: Add a `setPortamentoTime(float ms)` method to RuinaeVoice that configures a simple portamento ramp (similar to MonoHandler's). When a legato noteOn arrives in Poly mode, call `setFrequency()` instead of `noteOn()` to avoid envelope retrigger, and the voice's internal portamento will handle the glide. The portamento ramp can use a simple linear or exponential approach matching MonoHandler's implementation.

**Alternatives considered**:
- Poly slide via MonoHandler only: Rejected because MonoHandler is only active in Mono mode. Poly mode bypasses it entirely.
- Instant pitch jump for Poly slide: Partially acceptable as a fallback but does not satisfy FR-018 which requires slide time on each RuinaeVoice.

## R5: Accent Velocity Computation Order

**Question**: In what order should velocity transformations be applied?

**Decision**: `finalVelocity = clamp(round(inputVelocity * velLaneScale) + accentBoost, 1, 127)`

**Rationale**: The velocity lane scales the input velocity (multiplicative), and accent adds a fixed boost (additive). Applying the lane scaling first, then accent, means accent always adds a consistent amount regardless of the velocity lane value. This matches the TB-303 behavior where accent is a fixed circuit activation, not a velocity-relative boost.

**Alternatives considered**:
- Accent before lane scaling: `clamp(round((inputVelocity + accentBoost) * velLaneScale), 1, 127)`. Rejected because a velocity lane value of 0.5 would halve the accent effect, making it unpredictable.
- Accent as multiplicative boost: `clamp(round(inputVelocity * velLaneScale * accentMultiplier), 1, 127)`. Rejected because a multiplier approach makes the accent amount depend on the base velocity, which is unintuitive.

## R6: Tie Chain Termination Rules

**Question**: What terminates a tie chain, and what happens at chain boundaries?

**Decision**: A tie chain is terminated by any of: Rest step, Active step (without Tie flag), Slide step, or end of pattern with no Tie at step 0.

**Rationale**:
- Rest explicitly silences -- it must break the chain and emit noteOff for the sustained note.
- Active (non-tie) starts a new note -- the previous sustained note(s) must receive noteOff before the new noteOn.
- Slide starts a new legato transition -- the previous sustained note transitions to the new pitch without noteOff (but the tie chain conceptually ends because a new note starts).
- A tie at step 0 after the pattern wraps continues the chain if a note was sounding from the end of the previous cycle.

**Key edge case**: Tie with no preceding note (first step, or after rest) produces silence. This is the correct behavior -- there is nothing to sustain.

## R7: Modifier Lane Default for Backward Compatibility

**Question**: What default values ensure the modifier lane has zero behavioral impact on existing Phase 4 presets?

**Decision**: Default length=1, step[0]=`kStepActive` (0x01). Accent velocity default=30. Slide time default=60ms.

**Rationale**: With length=1 and value=kStepActive, every arp step reads the same modifier: "active, no modifiers". This produces identical behavior to Phase 4 where no modifier evaluation existed. The accent velocity and slide time defaults are irrelevant when no steps have the Accent or Slide flags, but 30 and 60ms are reasonable starting points for when the user first enables these features.

## R8: Parameter ID Allocation

**Question**: Do the planned parameter IDs (3140-3181) fit within the existing reserved range?

**Decision**: Yes, for Phase 5. The range 3133-3199 was explicitly reserved for Phases 5-8 per the Phase 4 implementation comment: `// 3133-3199: reserved for future phases 5-8`. The modifier lane uses 3140-3172 (length + 32 steps), accent velocity uses 3180, and slide time uses 3181. This leaves 3173-3179 and 3182-3189 reserved for future use. kArpEndId=3199 and kNumParameters=3200 do NOT need updating for Phase 5.

**Phase 6 warning**: The roadmap allocates `kArpRatchetLaneLengthId = 3190` through `kArpRatchetLaneStep31Id = 3222` for Phase 6. ID 3222 exceeds the current `kArpEndId = 3199`. Phase 6 implementers MUST update `kArpEndId` to at least 3222 and `kNumParameters` to at least 3223. This is documented in `arpeggiator-roadmap.md` Phase 6 section. Phase 5 does not need this update.

## R9: SIMD Viability for Modifier Evaluation

**Question**: Is SIMD optimization beneficial for the modifier flag evaluation logic?

**Decision**: NOT BENEFICIAL.

**Rationale**: The modifier evaluation executes once per arp step (1-50 times per second), not per audio sample. It consists of a single uint8_t bitmask read followed by 3-4 conditional branches. The total computation is approximately 10-20 instructions per step. SIMD optimization would add complexity with zero measurable benefit. The entire arp logic including modifiers is well under 0.1% CPU.

## R10: Existing Step-Setting Pattern in Processor

**Question**: Which step-setting pattern does the existing Phase 4 code actually use, and should the modifier lane follow it?

**Decision**: Use the expand-write-shrink pattern: call `setLength(32)` first (expand), write all 32 steps, then call `setLength(actualLength)` (shrink). This is the authoritative pattern.

**Rationale**: The actual Phase 4 implementation in `processor.cpp` (lines 1271-1300) uses exactly this pattern. It is correct because `ArpLane::setStep()` clamps the index to `length_-1`: without expanding first, writing to index 5 when `length_=1` silently writes to step 0, corrupting all step data. Expanding to 32 first ensures all indices are writable, then shrinking sets the correct cycling length.

The spec Clarification Q4 originally stated "Do NOT call setLength(32) first" -- this was an error in the draft clarification that contradicted the actual Phase 4 implementation. It has been corrected: spec.md Clarification Q4, FR-031, plan.md Common Gotchas, and tasks.md T021 have all been updated to consistently mandate the expand-write-shrink pattern. The original Note about "if the spec clarification mandates a different pattern" is no longer applicable -- the clarification now matches the code.
