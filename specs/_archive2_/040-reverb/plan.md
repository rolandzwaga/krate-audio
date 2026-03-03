# Implementation Plan: Dattorro Plate Reverb

**Branch**: `040-reverb` | **Date**: 2026-02-08 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/040-reverb/spec.md`

## Summary

Implement the Dattorro plate reverb algorithm as a Layer 4 effect in the KrateDSP library. The reverb provides stereo spatial processing with configurable room size, damping, diffusion, width, pre-delay, tank modulation (LFO), and freeze mode. The implementation reuses existing Layer 0-1 primitives (DelayLine, OnePoleLP, DCBlocker, SchroederAllpass, OnePoleSmoother) and is delivered as a single header-only file at `dsp/include/krate/dsp/effects/reverb.h`.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP library (Layer 0-1 primitives)
**Storage**: N/A (in-memory DSP processing)
**Testing**: Catch2 (via `dsp_tests` target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo - shared DSP library component
**Performance Goals**: < 1% CPU per instance at 44.1 kHz stereo (SC-001)
**Constraints**: Real-time safe (zero allocations in process), header-only, noexcept
**Scale/Scope**: Single reverb class + params struct; ~600-800 lines of header code; ~400-600 lines of tests

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | N/A | DSP-only component, no processor/controller |
| II. Real-Time Audio Thread Safety | PASS | All process methods noexcept, zero allocations, no locks |
| III. Modern C++ Standards | PASS | C++20, RAII, smart pointers not needed (value types), constexpr constants |
| IV. SIMD & DSP Optimization | PASS | SIMD analysis below; scalar-first workflow |
| V. VSTGUI Development | N/A | No UI component |
| VI. Cross-Platform Compatibility | PASS | Header-only, standard C++, no platform-specific code |
| VII. Project Structure & Build System | PASS | effects/ layer, CMake integration documented |
| VIII. Testing Discipline | PASS | Test plan below, Catch2, CI/CD |
| IX. Layered DSP Architecture | PASS | Layer 4, depends only on Layer 0-1 |
| X. DSP Processing Constraints | PASS | Linear interpolation for modulated delays, DC blocking in feedback, parameter smoothing |
| XI. Performance Budgets | PASS | Target < 1% CPU (Layer 4 budget < 5%) |
| XII. Debugging Discipline | PASS | Framework-standard patterns |
| XIII. Test-First Development | PASS | Tests written before implementation |
| XIV. Living Architecture Documentation | PASS | Will update layer-4-features.md |
| XV. Pre-Implementation Research (ODR) | PASS | No conflicts found (see Codebase Research below) |
| XVI. Honest Completion | PASS | Compliance table in spec.md |
| XVII. Framework Knowledge Documentation | N/A | No VSTGUI/VST3 SDK work |
| XVIII. Spec Numbering | PASS | 040 is unique |

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `Reverb`, `ReverbParams`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| Reverb | `grep -r "class Reverb" dsp/ plugins/` | No | Create New |
| ReverbParams | `grep -r "struct ReverbParams" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all utilities exist in Layer 0)

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayLine | `dsp/include/krate/dsp/primitives/delay_line.h` | 1 | Pre-delay, 4 tank plain delays, output tap reads |
| OnePoleLP | `dsp/include/krate/dsp/primitives/one_pole.h` | 1 | Input bandwidth filter, 2 tank damping filters |
| DCBlocker | `dsp/include/krate/dsp/primitives/dc_blocker.h` | 1 | 2 tank DC blockers (one per tank loop) |
| SchroederAllpass | `dsp/include/krate/dsp/primitives/comb_filter.h` | 1 | 4 input diffusion stages, 4 tank allpass stages (2 modulated, 2 fixed) |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | 8 parameter smoothers (decay, damping, mix, width, inputGain, preDelay, diffusion1, diffusion2) |
| kPi, kTwoPi, kHalfPi | `dsp/include/krate/dsp/core/math_constants.h` | 0 | LFO phase calculations |
| detail::isNaN, detail::isInf, detail::flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Input validation, denormal flushing |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/effects/` - Layer 4 effects (no existing reverb)
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: Both planned types (Reverb, ReverbParams) do not exist in the codebase. No name collisions found. All reused primitives are from Layer 0-1, respecting the Layer 4 dependency rules.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | read | `[[nodiscard]] float read(size_t delaySamples) const noexcept` | Yes |
| DelayLine | readLinear | `[[nodiscard]] float readLinear(float delaySamples) const noexcept` | Yes |
| OnePoleLP | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| OnePoleLP | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| OnePoleLP | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| OnePoleLP | reset | `void reset() noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| SchroederAllpass | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| SchroederAllpass | reset | `void reset() noexcept` | Yes |
| SchroederAllpass | setCoefficient | `void setCoefficient(float g) noexcept` | Yes |
| SchroederAllpass | setDelaySamples | `void setDelaySamples(float samples) noexcept` | Yes |
| SchroederAllpass | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| detail::isNaN | isNaN | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| detail::flushDenormal | flushDenormal | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class
- [x] `dsp/include/krate/dsp/primitives/one_pole.h` - OnePoleLP class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/comb_filter.h` - SchroederAllpass class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi, kHalfPi
- [x] `dsp/include/krate/dsp/core/db_utils.h` - isNaN, isInf, flushDenormal

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| SchroederAllpass | Uses `readLinear()` internally (linear interpolation) -- correct for modulated delays | Call `setDelaySamples()` per-sample with modulated value |
| SchroederAllpass | Internal timing uses `delaySamples_ - 1.0f` compensation | Just set the desired delay; compensation is internal |
| OnePoleLP | `setCutoff()` derives coefficient from Hz, not direct coefficient | Compute equivalent Hz from Dattorro coefficient for bandwidth filter |
| OnePoleLP | Clamps cutoff to `[1.0, Nyquist * 0.495]` | Bandwidth filter cutoff ~3.5 Hz at 44.1kHz is within range |
| DCBlocker | `prepare()` takes `(sampleRate, cutoffHz)`, not just `(sampleRate)` | Use default 10.0 Hz cutoff |
| DelayLine | `read(size_t)` takes integer samples; `readLinear(float)` takes fractional | Use `read()` for fixed pre-delay, output taps |
| OnePoleSmoother | `snapTo()` sets BOTH current and target; `snapToTarget()` sets current=target only | Use `snapTo()` in prepare/reset for immediate initialization |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `scaleDelay()` | Helper to scale reference delays to actual sample rate; reverb-specific, only 1 consumer |
| `computeOutputTaps()` | Taps the tank delay lines for stereo output; algorithm-specific |

**Decision**: No new Layer 0 utilities needed. The Dattorro algorithm's delay scaling formula is trivial (`round(ref * fs / 29761.0)`) and reverb-specific.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | Two cross-coupled tank loops; each sample depends on previous output of the opposite tank |
| **Data parallelism width** | 2 channels (stereo) | Left/right are computed independently from the tank, but the tank itself is mono (summed input). Output tapping could theoretically be SIMD-ized (7 taps per channel) but the taps read from different delay lines at different positions |
| **Branch density in inner loop** | LOW | No branches in the per-sample processing (freeze mode changes are smoothed) |
| **Dominant operations** | Memory (delay reads) + arithmetic (multiply-add) | Most time spent reading from delay line buffers at various positions |
| **Current CPU budget vs expected usage** | < 1% target vs ~0.5% expected | Simple arithmetic, 13 delay line reads per sample, not CPU-bound |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The Dattorro reverb has tight feedback coupling between Tank A and Tank B -- each tank's output feeds the other's input, creating a sample-by-sample serial dependency that cannot be parallelized. The two tanks process sequentially within each sample. The output tapping reads from 14 different positions across 6 different delay lines, making SIMD gather operations impractical. The algorithm is already well within the 1% CPU budget as scalar code.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip modulation when modDepth=0 | ~10-15% in common case | LOW | YES |
| Batch output tap reads | Marginal (cache already warm) | LOW | DEFER |
| Skip diffusion when diffusion=0 | ~20% in edge case | LOW | YES |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer** (from ROADMAP.md or known plans):
- Future hall/room reverb algorithms for Ruinae
- Other Layer 4 effects (delay modes) -- different algorithm entirely

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| ReverbParams struct | LOW | Only this reverb | Keep local |
| Modulated allpass pattern | MEDIUM | Future reverb/chorus | Keep local (SchroederAllpass already reusable) |
| Output tap summing | LOW | Only Dattorro-style reverbs | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class for effects | First reverb in codebase -- no pattern to establish yet |
| Keep all code in single header | Consistent with existing Layer 4 effects pattern |
| No extraction of modulated allpass | SchroederAllpass already handles this -- just call setDelaySamples() per sample |

### Review Trigger

After implementing **a second reverb algorithm** (e.g., hall reverb), review:
- [ ] Does the second reverb need shared tank topology code? -> Extract to Layer 3
- [ ] Does it need shared output tapping? -> Extract utility
- [ ] Any duplicated parameter smoothing patterns? -> Already handled by OnePoleSmoother

## Project Structure

### Documentation (this feature)

```text
specs/040-reverb/
+-- plan.md              # This file
+-- research.md          # Phase 0 research output
+-- data-model.md        # Phase 1 data model
+-- quickstart.md        # Phase 1 quickstart guide
+-- contracts/           # Phase 1 API contracts
|   +-- reverb-api.h     # Public API contract
+-- spec.md              # Feature specification
+-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- effects/
|       +-- reverb.h           # NEW: Dattorro plate reverb implementation
+-- tests/
    +-- unit/effects/
        +-- reverb_test.cpp    # NEW: Reverb unit tests
```

### Build Integration

**dsp/CMakeLists.txt** changes:
- Add `include/krate/dsp/effects/reverb.h` to `KRATE_DSP_EFFECTS_HEADERS` list

**dsp/tests/CMakeLists.txt** changes:
- Add `unit/effects/reverb_test.cpp` to `dsp_tests` source list (in "Layer 4: Effects" section)
- Add `unit/effects/reverb_test.cpp` to the `-fno-fast-math` properties list (for Clang/GCC)

## Implementation Architecture

### Signal Flow (per-sample)

```
1. Input: left, right (stereo)
2. NaN/Inf check: replace invalid with 0.0f
3. Store dry signal: dryL = left, dryR = right
4. Sum to mono: mono = (left + right) * 0.5f
5. Bandwidth filter: mono = bandwidthFilter_.process(mono)
6. Pre-delay: write mono, read at preDelaySamples
7. Input diffusion (4 cascaded allpasses):
   - Allpass 1: coeff = diffusion * 0.75, delay = scaled 142
   - Allpass 2: coeff = diffusion * 0.75, delay = scaled 107
   - Allpass 3: coeff = diffusion * 0.625, delay = scaled 379
   - Allpass 4: coeff = diffusion * 0.625, delay = scaled 277
   -> "diffusedInput"
8. Tank processing:
   a. Tank A input = inputGain * diffusedInput + decay * tankBOut_
   b. Tank A: DD1 allpass (modulated, coeff=-0.70) -> pre-damp delay -> damping LP -> * decay -> DD2 allpass (coeff=0.50) -> post-damp delay -> DC blocker -> tankAOut_
   c. Tank B input = inputGain * diffusedInput + decay * tankAOut_
   d. Tank B: DD1 allpass (modulated, coeff=-0.70) -> pre-damp delay -> damping LP -> * decay -> DD2 allpass (coeff=0.50) -> post-damp delay -> DC blocker -> tankBOut_
9. Output tapping: yL = sum of 7 taps, yR = sum of 7 taps (per Table 2)
10. Width processing: mid-side encoding with width parameter
11. Mix: outL = (1 - mix) * dryL + mix * wetL
12. Advance LFO phase
```

### Modulated Allpass Detail

The decay diffusion 1 allpass stages are modulated by the LFO. Per sample:
```
lfoA = sin(lfoPhase_) * modDepth * maxExcursion_
lfoB = cos(lfoPhase_) * modDepth * maxExcursion_  // 90-degree offset

tankADD1Delay = tankADD1Center_ + lfoA  // modulated delay time
tankBDD1Delay = tankBDD1Center_ + lfoB

// Set modulated delay on SchroederAllpass
tankADecayDiff1_.setDelaySamples(tankADD1Delay);
tankBDecayDiff1_.setDelaySamples(tankBDD1Delay);
```

### Freeze Mode Detail

When freeze is active:
- `inputGain` smoothed to 0.0 (no new input enters tank)
- `decay` smoothed to 1.0 (infinite sustain)
- Damping filters bypassed: set cutoff to Nyquist (no HF attenuation)

When freeze is deactivated:
- `inputGain` smoothed back to 1.0
- `decay` smoothed back to roomSize-derived value
- Damping filters restored to damping-derived cutoff

### Output Tap Positions (Reference Rate)

All positions at 29761 Hz, scaled by `round(pos * sampleRate / 29761.0)`.

**Left output (yL)**:
```
+tankBPreDampDelay.read(266)
+tankBPreDampDelay.read(2974)
-tankBDecayDiff2 (internal delay at pos 1913) -- read from its delay line
+tankBPostDampDelay.read(1996)
-tankAPreDampDelay.read(1990)
-tankADecayDiff2 (internal delay at pos 187)
+tankAPostDampDelay.read(1066)
```

**Note on tapping allpass internal delays**: The SchroederAllpass class uses a single internal DelayLine. To read output taps from inside the allpass, we need access to that delay line. Since SchroederAllpass's delay line is private, we will use standalone `DelayLine` instances for the allpasses instead of SchroederAllpass for the 4 tank allpass stages, implementing the allpass math inline. This gives us tap access to the allpass delay buffers.

**REVISED DECISION**: For the 4 tank allpass stages (decay diffusion 1 and decay diffusion 2 in both tanks), implement the allpass algorithm inline using raw `DelayLine` instances. This is necessary because output taps FR-009 read from INSIDE the allpass delay lines at specific positions. SchroederAllpass encapsulates its delay line privately, making tap reads impossible.

For the 4 input diffusion stages, continue using SchroederAllpass since no output taps read from those delay lines.

### Memory Layout Summary

**Total DelayLine instances**: 13
- 1 pre-delay
- 4 input diffusion (via SchroederAllpass, internal)
- 4 tank allpass delay lines (standalone, for tap access)
- 4 tank plain delay lines (pre-damp and post-damp)

**Total OnePoleLP instances**: 3
- 1 bandwidth filter
- 2 damping filters

**Total DCBlocker instances**: 2

**Total OnePoleSmoother instances**: 8

## Test Plan

### Test Categories

#### 1. Construction & Lifecycle Tests

| Test | Validates |
|------|-----------|
| Default construction succeeds | Reverb() creates unprepared instance |
| prepare() marks as prepared | isPrepared() returns true after prepare() |
| prepare() at various sample rates | 44100, 48000, 96000, 192000 |
| reset() clears state | After processing, reset produces silence |
| reset() preserves prepared state | isPrepared() still true after reset() |

#### 2. Parameter Tests

| Test | Validates |
|------|-----------|
| setParams() with defaults | No crash, no NaN output |
| roomSize range [0, 1] | Decay coefficient maps correctly |
| damping range [0, 1] | Damping cutoff maps correctly |
| mix=0 produces dry only | Output equals input |
| mix=1 produces wet only | Output differs from input (has reverb tail) |
| width=0 produces mono output | L == R for wet signal |
| width=1 produces full stereo | L != R for wet signal |
| preDelay creates offset | Wet signal onset delayed |
| diffusion=0 reduces smearing | Output less diffused than diffusion=1 |

#### 3. Core Processing Tests

| Test | Validates |
|------|-----------|
| Impulse produces decaying tail | FR-001: Reverb tail from impulse input |
| Tail duration scales with roomSize | Larger roomSize = longer tail |
| Damping reduces HF in tail | High damping = darker tail (measure spectral content) |
| Stereo output has decorrelation | Cross-correlation < 0.5 for width=1.0 (SC-007) |
| Continuous input produces blended output | Dry + wet mix correct |

#### 4. Freeze Mode Tests

| Test | Validates |
|------|-----------|
| Freeze sustains tail indefinitely | SC-003: Level stable +/-0.5 dB for 60s |
| Freeze blocks new input | No new signal enters tank |
| Unfreeze resumes decay | Tail fades after unfreeze |
| Freeze transition is click-free | No discontinuity on freeze toggle |

#### 5. Modulation Tests

| Test | Validates |
|------|-----------|
| modDepth=0 has no modulation effect | Output matches unmodulated reverb |
| modDepth>0 smears spectral peaks | FR-017: Reduced resonant peaks |
| Quadrature LFO phase | Tank A and B modulation are 90 degrees apart |

#### 6. Stability Tests

| Test | Validates |
|------|-----------|
| NaN input produces valid output | FR-027: NaN replaced with 0 |
| Infinity input produces valid output | FR-027: Inf replaced with 0 |
| Max roomSize + min damping is stable | SC-008: No unbounded growth over 10s |
| White noise input stays bounded | Output < +6 dBFS |
| Long processing does not accumulate DC | DC blocker prevents offset |

#### 7. Sample Rate Tests

| Test | Validates |
|------|-----------|
| 44100 Hz produces valid output | FR-030: Correct at standard rates |
| 48000 Hz produces valid output | FR-030 |
| 96000 Hz produces valid output | FR-030 |
| 192000 Hz produces valid output | FR-030 |
| 8000 Hz produces valid output | FR-030: Edge case low rate |
| Reverb character consistent across rates | SC-005: Perceptually similar decay |

#### 8. Performance Tests

| Test | Validates |
|------|-----------|
| Single instance < 1% CPU at 44.1kHz | SC-001 |
| processBlock equivalent to N * process | Bit-identical output |

## Complexity Tracking

No constitution violations. All design decisions comply with the project constitution.
