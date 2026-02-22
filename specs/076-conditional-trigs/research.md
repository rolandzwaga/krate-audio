# Research: Conditional Trig System

**Feature**: 076-conditional-trigs | **Date**: 2026-02-22

## Research Questions

### Q1: How should loop count be tracked relative to condition lane wrapping?

**Decision**: Loop count increments when the condition lane wraps from its last step back to step 0.

**Rationale**: In a polymetric system with multiple independent-length lanes, there is no single "pattern length." Tying the loop count to the condition lane's own cycle length is self-contained, predictable, and matches the Elektron model where loop count is tied to the pattern/track length. Alternative approaches (LCM of all lane lengths or a separate pattern length parameter) were rejected because they produce impractically long loop cycles or add unnecessary UI complexity.

**Alternatives considered**:
1. LCM of all lane lengths: E.g., LCM(3,5,7,8) = 840 steps, making A:B ratios useless for musical purposes.
2. Fixed pattern length parameter: Adds a separate control with unclear relationship to existing lane lengths.
3. Longest lane length: Changes loop definition unpredictably when any lane length changes.

### Q2: What PRNG should be used for probability evaluation?

**Decision**: Reuse the existing `Xorshift32` class from `dsp/include/krate/dsp/core/random.h` with a dedicated instance seeded at 7919.

**Rationale**: Xorshift32 is already used by NoteSelector (seed 42) for Random/Walk modes. It is fast (3 XOR operations per call), real-time safe (no allocation), constexpr, and noexcept. A dedicated instance with a different seed (7919, a prime) ensures the probability sequence is independent of the note selection sequence. The fixed seed ensures testability -- a test constructing a fresh ArpeggiatorCore gets a deterministic sequence.

**Alternatives considered**:
1. Shared PRNG with NoteSelector: Would create undesirable coupling between note selection and probability evaluation (changing modes would alter probability sequences).
2. Time-based seed: Prevents deterministic testing. Musical users don't need per-load variation -- the sequence is statistically well-distributed within a few steps regardless of starting point.
3. Linear congruential generator (LCG): Xorshift32 is already available and has better statistical properties than simple LCGs.

### Q3: Should the PRNG be consumed on Euclidean rest steps?

**Decision**: No. The PRNG is NOT consumed when a step is gated as rest by Euclidean timing.

**Rationale**: Since Euclidean rest short-circuits before condition evaluation (FR-012 step 3), the PRNG is naturally not consumed. This means the PRNG sequence is deterministic for a given pattern of Euclidean hits, which aids testing and reproducibility. If the PRNG were consumed even on rest steps, the probability sequence would shift when Euclidean patterns change, creating confusing behavior.

**Alternatives considered**:
1. Always consume PRNG (including rest steps): Would make the probability sequence dependent on Euclidean pattern configuration, which is counterintuitive.

### Q4: Should the PRNG be reset on pattern reset?

**Decision**: No. The condition PRNG is NOT reset on `reset()` or `resetLanes()`.

**Rationale**: If the PRNG were reset to its initial seed on every pattern restart, the same "random" pattern would play on every restart, defeating the purpose of probability conditions. By not resetting, each restart produces a different probability sequence, creating the intended musical variation. The fixed construction-time seed still ensures testability (a fresh ArpeggiatorCore always starts from the same point).

**Alternatives considered**:
1. Reset PRNG on pattern reset: Would produce identical "random" patterns on every restart -- musically undesirable.
2. Expose seed as parameter: Overkill for this use case. Can be added later if needed.

### Q5: What is the correct placement for condition evaluation in the fireStep() pipeline?

**Decision**: After Euclidean gating, before modifier evaluation.

**Rationale**: This creates a three-layer gating chain: Euclidean (structural rhythm) -> Conditions (evolutionary/probabilistic) -> Modifiers (per-step articulation). The roadmap explicitly states: "Phase 8 depends on Phase 7 because Euclidean determines which steps exist to apply conditions to."

**Alternatives considered**:
1. Before Euclidean: Would evaluate conditions on Euclidean rest steps, wasting PRNG values and creating confusion about what "loop count" means for structurally inactive steps.
2. After modifier evaluation: Would allow conditions to gate based on modifier flags, which is semantically backwards (conditions determine IF a step fires; modifiers determine HOW).

### Q6: How should the condition lane interact with the expand-write-shrink parameter application pattern?

**Decision**: Follow the same pattern as ratchet/modifier lanes: `setLength(32)`, write all 32 steps, then `setLength(actualLength)`. The final `setLength()` call does NOT affect `loopCount_`.

**Rationale**: `ArpLane::setLength()` only clamps the position if it exceeds the new length -- it does not reset or touch any external state. The `loopCount_` is a separate member of ArpeggiatorCore, not part of ArpLane. This matches the spec requirement (FR-018) that loopCount_ continues uninterrupted across length changes.

**Alternatives considered**: None. The expand-write-shrink pattern is well-established across all 5 existing lane types.

### Q7: What is the correct behavior for fillActive_ across resets?

**Decision**: `fillActive_` is NOT reset by `reset()` or `resetLanes()`.

**Rationale**: Fill mode is a performance control (like a sustain pedal), not a pattern state. The user expects Fill to remain active when the arp resets via transport restart, retrigger, or disable/enable transitions. The host parameter system manages the Fill Toggle parameter's lifecycle. The only way to change `fillActive_` is through explicit `setFillActive()` calls from the parameter system.

**Alternatives considered**:
1. Reset fillActive_ on resetLanes(): Would surprise the performer who has Fill held down during a retrigger.

### Q8: What is the correct condition lane advance timing relative to loopCount_ detection?

**Decision**: The condition lane advances along with all other lanes in a single batch. After all lanes advance, check if the condition lane wrapped (currentStep() == 0 after advance) and increment loopCount_ if so.

**Rationale**: ArpLane::advance() returns the current value and then moves the position forward. After advance, if currentStep() is 0, the lane just wrapped from its last step to the beginning. For length 1, this happens on every step (correct per FR-018). For length > 1, this happens every `length` steps. The wrap detection is a simple comparison with no special cases needed.

**Alternatives considered**:
1. Track pre-advance position and compare: More complex and equivalent to the currentStep() == 0 check after advance.
2. Add a `wrapped()` method to ArpLane: Would require ArpLane changes. The currentStep() == 0 check is sufficient and requires no ArpLane modifications.

### Q9: TrigCondition display strings for parameter formatting

**Decision**: Use concise musical notation that matches Elektron conventions.

| TrigCondition | Display String |
|---------------|----------------|
| Always | "Always" |
| Prob10 | "10%" |
| Prob25 | "25%" |
| Prob50 | "50%" |
| Prob75 | "75%" |
| Prob90 | "90%" |
| Ratio_1_2 | "1:2" |
| Ratio_2_2 | "2:2" |
| Ratio_1_3 | "1:3" |
| Ratio_2_3 | "2:3" |
| Ratio_3_3 | "3:3" |
| Ratio_1_4 | "1:4" |
| Ratio_2_4 | "2:4" |
| Ratio_3_4 | "3:4" |
| Ratio_4_4 | "4:4" |
| First | "1st" |
| Fill | "Fill" |
| NotFill | "!Fill" |

**Rationale**: This matches the compact display style used on Elektron hardware parameter screens. The probability values use percentage notation. A:B ratios use colon notation. "1st" is the standard Elektron abbreviation for First.

### Q10: Backward compatibility with Phase 7 presets

**Decision**: If `loadArpParams()` encounters EOF at the first condition field (`conditionLaneLength`), it returns `true` (success) and all condition fields retain defaults (length 1, all steps Always = 0, fill off). If EOF occurs after `conditionLaneLength` is read but before all step values are read, it returns `false` (corrupt stream).

**Rationale**: This follows the exact same EOF-safe backward compatibility pattern used by every previous phase: velocity lane (Phase 4), modifier lane (Phase 5), ratchet lane (Phase 6), and Euclidean timing (Phase 7). The first field of a new section serves as the presence sentinel.

**Alternatives considered**: None. The pattern is well-established and proven across 4 prior phases.
