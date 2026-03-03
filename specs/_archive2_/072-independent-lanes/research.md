# Research: Independent Lane Architecture (072)

**Date**: 2026-02-21
**Phase 0 Output**: All NEEDS CLARIFICATION items resolved

---

## R1: ArpLane<T> Template Design — How to Handle Different Value Types

### Decision
Use a `template<typename T, size_t MaxSteps = 32>` class with internal `std::array<T, MaxSteps>` storage. The template parameter supports `float` (velocity, gate), `int8_t` (pitch), and future types (`uint8_t` for modifiers/ratchet/conditions in Phases 5-8).

### Rationale
- **std::array backing**: Zero heap allocation, fixed capacity 32, same pattern as HeldNoteBuffer (32 HeldNote entries) and SequencerCore (internal step arrays).
- **Template, not inheritance**: No virtual dispatch overhead in audio path. Each lane type is a concrete type with zero indirection.
- **MaxSteps = 32 default**: Matches trance gate (32 steps), held note buffer (32 notes). Sufficient for all planned lane types.
- **int8_t for pitch (not int)**: The DSP-side ArpLane uses int8_t natively since pitch offsets are -24 to +24. The *parameter storage* in ArpeggiatorParams uses `std::atomic<int>` per step to avoid lock-free uncertainty with `std::atomic<int8_t>` (per spec clarification). The conversion happens at the boundary between params and DSP.

### Alternatives Considered
1. **Non-templated with variant/union**: Rejected — adds runtime branching, violates "minimize branching in inner loops" (Constitution IV).
2. **Inheritance hierarchy (LaneBase/VelocityLane/etc.)**: Rejected — virtual function calls forbidden in tight processing loops (Constitution IV).
3. **Single concrete class per lane type**: Rejected — would triplicate identical advance/reset/setStep/getStep logic with only the value type differing.

---

## R2: Where ArpLane<T> Lives in the Layer Architecture

### Decision
`dsp/include/krate/dsp/primitives/arp_lane.h` — Layer 1 (primitives).

### Rationale
- ArpLane is a simple, stateful, fixed-capacity container with step advancement. It has no dependencies beyond `<array>`, `<cstddef>`, `<cstdint>`, and `<algorithm>` — all standard library.
- It follows the same pattern as HeldNoteBuffer (Layer 1): a fixed-capacity, zero-allocation container with position tracking.
- ArpeggiatorCore (Layer 2) composes ArpLane instances, which is valid since Layer 2 can depend on Layer 1.
- SequencerCore (Layer 1) is a conceptual peer — both are step containers. ArpLane is simpler (no timing, no gate state — just value storage with position).

### Alternatives Considered
1. **Layer 0 (core)**: Rejected — ArpLane has state (position, length), which is characteristic of Layer 1 primitives, not Layer 0 stateless utilities.
2. **Layer 2 (processors)**: Rejected — ArpLane does not process audio or compose other components. It is a pure data container.

---

## R3: How Lanes Interact with fireStep() in ArpeggiatorCore

### Decision
In `ArpeggiatorCore::fireStep()`, after `NoteSelector::advance()` returns the base note(s)/velocities, the three lanes are advanced and their values applied:

1. **Velocity**: `result.velocities[i] = clamp(round(result.velocities[i] * velocityLane_.advance()), 1, 127)`
2. **Gate**: `float gateScale = gateLane_.advance(); gateDuration = calculateGateDuration(gateScale)` — the lane value is passed INTO `calculateGateDuration()`, which applies the multiplication inside its cast chain. This preserves the exact IEEE 754 double-precision computation order required for SC-002 bit-identical output.
3. **Pitch**: `result.notes[i] = clamp(result.notes[i] + pitchLane_.advance(), 0, 127)`

Lane advancement happens once per step, simultaneously for all three lanes. For Chord mode, all chord notes on that step get the same lane values (per edge case spec).

### Rationale
- **Advance in fireStep()**: This is the single point where arp steps fire. Advancing lanes here ensures exact 1:1 correspondence between arp steps and lane steps.
- **Velocity multiply-then-clamp**: Matches MIDI semantics — velocity 0 is NoteOff, so we clamp to [1, 127] per FR-011.
- **Gate: pass into calculateGateDuration(), not multiply the return value**: The modified signature is `size_t calculateGateDuration(float gateLaneValue = 1.0f) const noexcept`. The full formula inside is `static_cast<size_t>(static_cast<double>(currentStepDuration_) * static_cast<double>(gateLengthPercent_) / 100.0 * static_cast<double>(gateLaneValue))`. This keeps the entire multiplication inside one double-precision expression, so when `gateLaneValue == 1.0f`, the result is bit-identical to the Phase 3 formula. If the gate lane value were multiplied AFTER `calculateGateDuration()` returned a `size_t`, precision would be lost (integer truncation before the lane multiplier). The internal-parameter approach in `plan.md`, `data-model.md`, and `tasks.md` is the authoritative design.
- **Pitch add-then-clamp**: Simple semitone offset, clamped to MIDI range [0, 127] per FR-018.

### Alternatives Considered
1. **Advance lanes in processBlock() main loop**: Rejected — would require tracking whether a step fired, introducing unnecessary complexity. fireStep() is the natural advancement point.
2. **Separate advanceAllLanes() method**: Considered but unnecessary — the three advance() calls are inline and trivial. A helper would add indirection without benefit.

---

## R4: Lane Reset Semantics

### Decision
Add a `resetLanes()` private method to ArpeggiatorCore that calls `reset()` on all three lanes. This method is called from the same places where the arp state currently resets:

1. **ArpeggiatorCore::reset()** (called from prepare() and transport stop/start)
2. **Retrigger Note mode** (in noteOn() when retriggerMode_ == Note)
3. **Retrigger Beat mode** (in processBlock() when bar boundary fires)
4. **setEnabled(false->true transition)** — this goes through reset() via prepare()

Lanes do NOT reset when heldNotes_ becomes empty in Latch Off mode — they pause at their current position (per FR-022 and edge case spec).

### Rationale
- Centralizing reset logic in `resetLanes()` ensures future lanes added in Phases 5-8 are automatically included by adding one line to the method.
- The three explicit reset triggers (retrigger, transport, disable/enable) are already identified in FR-022.
- Pause-on-empty is the natural behavior since `fireStep()` is not called when heldNotes_ is empty (the existing code already returns early).

### Alternatives Considered
1. **Reset inside NoteSelector::reset()**: Rejected — NoteSelector is Layer 1 and should not know about ArpLane. Layer 2 (ArpeggiatorCore) is the right place.
2. **No centralized resetLanes()**: Rejected — would require editing 3-4 call sites for each new lane type in Phases 5-8.

---

## R5: Parameter Storage Pattern (ArpeggiatorParams Extension)

### Decision
Extend `ArpeggiatorParams` struct with:
```cpp
// Velocity lane
std::atomic<int> velocityLaneLength{1};
std::array<std::atomic<float>, 32> velocityLaneSteps{};  // default 1.0

// Gate lane
std::atomic<int> gateLaneLength{1};
std::array<std::atomic<float>, 32> gateLaneSteps{};  // default 1.0

// Pitch lane
std::atomic<int> pitchLaneLength{1};
std::array<std::atomic<int>, 32> pitchLaneSteps{};  // default 0
```

Constructor initializes defaults. The `handleArpParamChange()` function uses range-based dispatch (same pattern as trance gate step levels).

### Rationale
- **std::atomic<int> for pitch steps**: Even though DSP uses int8_t, `std::atomic<int8_t>` is not guaranteed lock-free on all platforms. `std::atomic<int>` is universally lock-free (verified by the trance gate pattern already in use).
- **std::array<std::atomic<float>, 32>**: Exact same pattern as `RuinaeTranceGateParams::stepLevels`.
- **Constructor default initialization**: velocity/gate steps default to 1.0f, pitch steps to 0 — matching FR-012, FR-015, FR-019 (identical to Phase 3 behavior).

### Alternatives Considered
1. **Blob encoding**: Rejected per spec clarification — breaks per-step automation, deviates from established trance_gate_params.h pattern.
2. **std::atomic<int8_t> for pitch**: Rejected — `is_lock_free()` is not guaranteed, and int occupies same cache line space in this sparse-access pattern.

---

## R6: Parameter ID Allocation and kNumParameters Update

### Decision
Parameter IDs follow the roadmap spec exactly:
- Velocity lane: length at 3020, steps 0-31 at 3021-3052
- Gate lane: length at 3060, steps 0-31 at 3061-3092
- Pitch lane: length at 3100, steps 0-31 at 3101-3132

The `kArpEndId` changes from 3099 to 3199. `kNumParameters` changes from 3100 to 3200.

### Rationale
- 40-ID gaps between lane blocks (3020-3052, 3060-3092, 3100-3132) leave room for future per-lane metadata parameters (e.g., lane direction, interpolation mode).
- The existing 11 arp params (3000-3010) are untouched, preserving backward compatibility.
- kNumParameters is a sentinel used for array sizing. Expanding to 3200 adds ~400 bytes to any parameter arrays — negligible.

### Alternatives Considered
1. **Contiguous allocation (3011-3109)**: Rejected — no gaps for future lane expansion; harder to debug ID math.
2. **Separate ID range (4000-4199)**: Rejected — arp parameters should stay in the arp block; adding a new range complicates dispatch logic.

---

## R7: State Serialization Backward Compatibility

### Decision
Lane data is appended after existing arp params in the state stream. The existing `loadArpParams()` already returns `false` on EOF (truncated stream), which triggers default initialization. The new loading code:

1. First reads the 11 base arp params (existing code, unchanged).
2. Then attempts to read lane data (3 lengths + 3x32 step values).
3. If reading lane data fails at any point, all lanes default to length 1 with default values.

No version bump is needed (same pattern as spec 071's arp params — EOF-safe loading).

### Rationale
- Phase 3 presets have no lane data after the 11 base params. The stream will EOF when lane reads are attempted, and the `readInt32()/readFloat()` calls will return false. Lanes stay at their defaults (length 1, velocity 1.0, gate 1.0, pitch 0), producing behavior identical to Phase 3.
- This matches the exact pattern already used by `loadArpParams()` in spec 071 — no version marker needed because loading is EOF-tolerant.

### Alternatives Considered
1. **Version marker (like kTranceGateStateVersion)**: Considered but unnecessary — the arp params were designed from Phase 3 to be EOF-safe. A version marker adds complexity with no benefit since the lane data has a known fixed size.
2. **Separate serialization function for lanes**: Rejected — keeps save/load logic split across two functions. Better to extend the existing `saveArpParams()`/`loadArpParams()`.

---

## R8: Bit-Identical Backward Compatibility (SC-002)

### Decision
When all lanes have length 1 and default values, the computation is mathematically identical to Phase 3:

- **Velocity**: `velocity * 1.0f == velocity` (IEEE 754 guarantees this).
- **Gate**: `calculateGateDuration() * 1.0f == calculateGateDuration()` (integer * 1.0f preserves the exact value; we cast to size_t after).
- **Pitch**: `note + 0 == note` (integer addition with 0 is exact).

The gate calculation requires care: the existing Phase 3 formula is `static_cast<size_t>(static_cast<double>(currentStepDuration_) * static_cast<double>(gateLengthPercent_) / 100.0)`. With lanes, this becomes `static_cast<size_t>(static_cast<double>(currentStepDuration_) * static_cast<double>(gateLengthPercent_) / 100.0 * static_cast<double>(gateLaneValue))`. When `gateLaneValue == 1.0f`, `* 1.0` in double is a no-op for IEEE 754.

### Rationale
- The spec demands strict bit-identical output (zero tolerance) when lanes are at defaults. IEEE 754 arithmetic guarantees that multiplying by 1.0 and adding 0 are identity operations. No special-casing or branching is needed.

### Verification Plan
- A dedicated test will capture Phase 3 output (all lanes at defaults) for 1000+ steps across multiple tempos, then verify that the Phase 4 output matches bit-for-bit (same note, same velocity, same noteOff sample offset).

---

## R9: SIMD Viability for Lane Operations

### Decision
SIMD is NOT BENEFICIAL for the lane system.

### Rationale
- Lane advance() is called once per arp step (not per sample). At typical arp rates (1-50 Hz), this means 1-50 calls per second — far too infrequent to benefit from SIMD.
- Each advance() is a single array read + index increment + modulo wrap. This is 3-4 instructions.
- The dominant cost in the arp is the main loop in processBlock() (timing, NoteOff tracking), not lane operations.
- There is no data parallelism to exploit: each lane has a different type and different range.

### Alternative Optimizations
None needed. The entire arp (including lanes) is well under 0.1% CPU at any reasonable tempo.
