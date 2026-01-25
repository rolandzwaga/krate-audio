# Research: GranularFilter

**Date**: 2026-01-25 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Phase 0 Research Summary

This document captures research findings and resolved clarifications for the GranularFilter implementation.

---

## 1. SVF Performance Analysis

### Research Question
Can 128 SVF filter instances (64 grains x 2 channels) meet the CPU budget of < 5% on Intel i5-8400 at 48kHz?

### Findings

**SVF Computational Cost** (from svf.h analysis):
- Per-sample operations: ~10 multiplications + ~8 additions
- State updates: 2 integrator updates with trapezoidal rule
- Denormal flushing: 2 checks per sample

**Budget Calculation**:
- Intel i5-8400: 2.8 GHz base, ~2.8 billion cycles/second
- 48kHz sample rate: 48,000 samples/second
- Cycles per sample: 2.8B / 48K = ~58,333 cycles available per sample
- 5% budget = ~2,917 cycles per sample for granular processing

**SVF Estimate**:
- ~25-30 cycles per SVF::process() call (conservative estimate for modern CPU)
- 128 filters x 30 cycles = 3,840 cycles per sample
- This is ~1.3% of total CPU budget

**Decision**: SVF is efficient enough. Even with 128 instances, we use only ~1.3% CPU for filtering, leaving margin for other granular processing.

**Rationale**: The SVF implementation uses the Cytomic TPT topology which is highly efficient. The main processing loop is a simple linear sequence without branches.

**Alternatives considered**:
- Biquad filters: Similar cost but less modulation-stable
- IIR one-pole: Cheaper but insufficient for bandpass/notch modes

---

## 2. Grain Slot Indexing Strategy

### Research Question
How to efficiently map grain pointers to their corresponding filter state indices?

### Findings

**GrainPool Architecture** (from grain_pool.h):
- Fixed array of 64 Grain structs: `std::array<Grain, kMaxGrains> grains_`
- activeGrains() returns span of pointers to active grains
- Grains are stored contiguously but active list is sparse

**Option A: Pointer Arithmetic**
```cpp
size_t getGrainIndex(const Grain* grain) const noexcept {
    return grain - &grains_[0];  // Pointer difference gives index
}
```
- Pro: O(1) constant time
- Con: Requires direct access to internal grains_ array

**Option B: Parallel Active Tracking**
- Maintain our own array tracking which slots have active grains
- Pro: Self-contained
- Con: Duplicates GrainPool logic

**Option C: Modify GrainPool to expose index**
- Add acquireGrainWithIndex() method
- Pro: Clean interface
- Con: Modifies Layer 1 primitive for Layer 3 need

**Decision**: Use pointer arithmetic (Option A) by accessing the grains via the pool's internal array position.

**Rationale**:
- The grains_ array is fixed-size and contiguous
- Pointer arithmetic is well-defined for arrays
- We can derive index from grain pointer address
- No modification to existing components needed

**Implementation**:
```cpp
// In GranularFilter, we'll need to track grain indices ourselves
// When acquiring a grain, search pool.grains_ to find the index
// Store index in a parallel array indexed by slot
```

---

## 3. Signal Flow Integration Point

### Research Question
How to inject filter processing between envelope and pan stages when GrainProcessor applies them together?

### Findings

**Current GrainProcessor::processGrain Flow** (from grain_processor.h):
```cpp
// 1. Get envelope value
const float envelope = GrainEnvelope::lookup(...);

// 2. Read from delay buffer
const float sampleL = delayBufferL.readLinear(delaySamples);
const float sampleR = delayBufferR.readLinear(delaySamples);

// 3. Apply envelope and amplitude
const float gainedL = sampleL * envelope * grain.amplitude;
const float gainedR = sampleR * envelope * grain.amplitude;

// 4. Apply panning (pan gains computed in initializeGrain)
const float outputL = gainedL * grain.panL;
const float outputR = gainedR * grain.panR;

// 5. Advance state
```

**Problem**: Filter needs to go between steps 3 and 4.

**Option A: Modify GrainProcessor to accept callback**
```cpp
std::pair<float, float> processGrain(Grain& grain, DelayLine& L, DelayLine& R,
                                      std::function<void(float&, float&)> postEnvelope);
```
- Con: std::function has virtual call overhead, not real-time safe

**Option B: Create FilteredGrainProcessor subclass**
- Con: Virtual inheritance overhead

**Option C: Duplicate processGrain logic in GranularFilter**
- Pro: Complete control over signal flow
- Pro: No changes to existing components
- Con: Code duplication

**Decision**: Duplicate the grain processing logic in GranularFilter (Option C).

**Rationale**:
- The processGrain function is relatively simple (~30 lines)
- Complete control over signal flow without modifying existing code
- No virtual dispatch overhead
- Real-time safe guaranteed

---

## 4. Cutoff Randomization Mathematics

### Research Question
How to implement octave-based cutoff randomization with uniform distribution?

### Findings

**Specification** (from spec.md):
- Base cutoff + randomization in octaves (0-4 octaves)
- Example: 1kHz base, 2 octaves = 250Hz to 4kHz range

**Mathematics**:
```
Octave offset = randomValue * randomizationOctaves  // randomValue in [-1, 1]
Actual cutoff = baseCutoff * 2^(octaveOffset)
```

For 2 octaves randomization around 1kHz:
- randomValue = -1: cutoff = 1000 * 2^(-2) = 250 Hz
- randomValue = 0:  cutoff = 1000 * 2^(0) = 1000 Hz
- randomValue = +1: cutoff = 1000 * 2^(2) = 4000 Hz

**Implementation**:
```cpp
float calculateRandomizedCutoff(float baseCutoff, float randomizationOctaves) noexcept {
    // Get bipolar random value [-1, 1]
    const float randomOffset = rng_.nextFloat();  // Xorshift32::nextFloat() returns [-1, 1]

    // Calculate octave offset
    const float octaveOffset = randomOffset * randomizationOctaves;

    // Calculate actual cutoff (2^x using std::exp2)
    const float cutoff = baseCutoff * std::exp2(octaveOffset);

    // Clamp to valid range
    const float minCutoff = 20.0f;
    const float maxCutoff = static_cast<float>(sampleRate_) * SVF::kMaxCutoffRatio;
    return std::clamp(cutoff, minCutoff, maxCutoff);
}
```

**Decision**: Use bipolar random value with exp2() for octave scaling.

**Rationale**: This produces uniform distribution in log-frequency space (octaves), which matches how humans perceive pitch/frequency.

---

## 5. Filter State Reset Strategy

### Research Question
When and how should filter state be reset to prevent artifacts?

### Findings

**SVF State** (from svf.h):
- Two integrator states: `ic1eq_`, `ic2eq_`
- `reset()` method zeroes both states

**Grain Lifecycle**:
1. `acquireGrain()`: Grain slot allocated (new or stolen)
2. Processing loop: Grain actively producing audio
3. `releaseGrain()`: Grain envelope complete, slot freed

**Reset Timing Options**:

**Option A: Reset on acquire**
- Reset filter when grain is acquired
- Pro: Clean slate for each grain
- Con: None significant

**Option B: Reset on release**
- Reset filter when grain is released
- Con: State from old grain could affect next grain briefly

**Option C: Continuous reset when inactive**
- Reset filter on every process() call for inactive grains
- Con: Unnecessary work

**Decision**: Reset filter state when grain is acquired (Option A).

**Rationale**:
- Ensures clean filter state at start of each grain
- Prevents any DC offset or ringing from previous grain
- Matches existing GranularEngine pattern of initializing state on acquire

**Implementation**:
```cpp
void triggerNewGrain(...) {
    Grain* grain = pool_.acquireGrain(currentSample_);
    if (grain == nullptr) return;

    // Find the grain's slot index
    size_t slotIndex = /* ... */;

    // Reset filter state for this slot
    filterStates_[slotIndex].filterL.reset();
    filterStates_[slotIndex].filterR.reset();

    // Configure filter for this grain
    float cutoff = calculateRandomizedCutoff(baseCutoffHz_, cutoffRandomizationOctaves_);
    filterStates_[slotIndex].filterL.setCutoff(cutoff);
    filterStates_[slotIndex].filterR.setCutoff(cutoff);
    filterStates_[slotIndex].cutoffHz = cutoff;

    // ... rest of grain initialization
}
```

---

## 6. Filter Bypass Mode Equivalence

### Research Question
How to ensure filter bypass mode produces bit-identical output to GranularEngine?

### Findings

**Requirement** (SC-007): Filter bypass mode produces output identical to GranularEngine (bit-identical when seeded).

**Challenge**: GranularFilter uses its own processing loop, which must exactly match GranularEngine's behavior.

**Approach**:
1. When `filterEnabled_ = false`:
   - Skip filter processing entirely (no call to SVF::process())
   - All other processing must match GranularEngine exactly

2. Implementation match requirements:
   - Same parameter smoothing (OnePoleSmoother with same time constants)
   - Same gain scaling (1/sqrt(n) for overlapping grains)
   - Same RNG usage pattern (scheduler seed + main RNG)
   - Same envelope lookup
   - Same delay buffer interpolation (readLinear)

**Decision**: Verify bypass equivalence through comparison tests.

**Rationale**: The test suite will verify bit-identical output by running GranularEngine and GranularFilter (with filter disabled) side-by-side with identical seeds.

---

## 7. Memory Layout Optimization

### Research Question
What is the optimal memory layout for filter state storage?

### Findings

**SVF Memory Size** (estimated from svf.h members):
```cpp
// Configuration (constant during grain lifetime)
double sampleRate_ = 44100.0;    // 8 bytes
float cutoffHz_ = 1000.0f;       // 4 bytes
float q_ = kButterworthQ;         // 4 bytes
float gainDb_ = 0.0f;            // 4 bytes
SVFMode mode_ = Lowpass;          // 1 byte
bool prepared_ = false;           // 1 byte

// Coefficients (derived from config)
float g_, k_, a1_, a2_, a3_, A_;  // 6 * 4 = 24 bytes
float m0_, m1_, m2_;              // 3 * 4 = 12 bytes

// State (modified per-sample)
float ic1eq_ = 0.0f;             // 4 bytes
float ic2eq_ = 0.0f;             // 4 bytes

// Total: ~66 bytes per SVF, ~80 with padding
```

**FilteredGrainState Memory**:
```cpp
struct FilteredGrainState {
    SVF filterL;           // ~80 bytes
    SVF filterR;           // ~80 bytes
    float cutoffHz;        // 4 bytes
    bool filterEnabled;    // 1 byte + 3 padding
};
// Total: ~168 bytes per grain slot
```

**Total Memory**: 64 slots * 168 bytes = ~10.75 KB

**Decision**: Acceptable memory usage, no optimization needed.

**Rationale**: 10.75 KB is negligible compared to delay buffer memory (2 seconds at 192kHz = 3.07 MB per channel).

---

## Summary of Resolved Clarifications

| Question | Resolution | Rationale |
|----------|------------|-----------|
| Can 128 SVFs meet CPU budget? | Yes (~1.3% CPU) | SVF is highly efficient at ~30 cycles/sample |
| How to index grain slots? | Pointer arithmetic | O(1) lookup, no modification to existing code |
| Where to inject filter in signal chain? | Duplicate processGrain logic | Complete control, no virtual dispatch |
| How to randomize cutoff? | exp2(bipolar * octaves) | Uniform distribution in perceptual space |
| When to reset filter state? | On grain acquire | Clean slate, prevents artifacts |
| How to ensure bypass equivalence? | Comparison tests | Verify bit-identical with same seed |
| Memory impact? | ~10.75 KB acceptable | Negligible vs delay buffer |

All research questions resolved. Ready for Phase 1 design.
