# Implementation Plan: Sidechain Filter Processor

**Branch**: `090-sidechain-filter` | **Date**: 2026-01-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/090-sidechain-filter/spec.md`

## Summary

A Layer 2 DSP processor that dynamically controls a filter's cutoff frequency based on the amplitude envelope of a sidechain signal. Supports external sidechain (for ducking/pumping effects) and self-sidechain (auto-wah with lookahead) modes. Key behaviors: log-space envelope-to-cutoff mapping, hold phase that tracks but blocks release, threshold comparison in dB domain, and lookahead via audio delay (not sidechain delay).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: EnvelopeFollower (Layer 2), SVF (Layer 1), DelayLine (Layer 1), OnePoleSmoother (Layer 1), Biquad (Layer 1)
**Storage**: N/A (stateless configuration, in-memory state)
**Testing**: Catch2 via dsp_tests target
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: KrateDSP shared library at `dsp/include/krate/dsp/processors/`
**Performance Goals**: < 0.5% single core @ 48kHz stereo (Layer 2 processor budget)
**Constraints**: Zero allocations in process(), noexcept on all processing methods, real-time safe
**Scale/Scope**: Single processor class, ~500-700 LOC header

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in audio thread (DelayLine pre-allocated in prepare())
- [x] No locks/mutexes in processing path
- [x] No exceptions (noexcept on all process methods)
- [x] No I/O operations in audio callbacks

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 2 processor - may depend on Layers 0-1 and peer Layer 2 (EnvelopeFollower)
- [x] Will compose: EnvelopeFollower (L2), SVF (L1), DelayLine (L1), Biquad (L1), OnePoleSmoother (L1)

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: SidechainFilter, SidechainFilterState (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SidechainFilter | `grep -r "class SidechainFilter" dsp/ plugins/` | No | Create New |
| SidechainFilterState | `grep -r "SidechainFilterState" dsp/ plugins/` | No | Create New |
| Direction (enum) | `grep -r "enum.*Direction" dsp/` | Yes (EnvelopeFilter) | Declare locally (enum class allows same name) |
| FilterType (enum) | `grep -r "enum.*FilterType" dsp/` | Yes (EnvelopeFilter) | Declare locally (same pattern) |

**Utility Functions to be created**: None - all math utilities exist in Layer 0

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| gainToDb | `grep -r "gainToDb" dsp/` | Yes | db_utils.h | Reuse |
| dbToGain | `grep -r "dbToGain" dsp/` | Yes | db_utils.h | Reuse |
| constexprExp | `grep -r "constexprExp" dsp/` | Yes | db_utils.h | Reuse |
| constexprLn | `grep -r "constexprLn" dsp/` | Yes | db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| EnvelopeFollower | processors/envelope_follower.h | 2 | Sidechain amplitude detection with attack/release |
| SVF | primitives/svf.h | 1 | Main filter with cutoff modulation |
| DelayLine | primitives/delay_line.h | 1 | Lookahead buffer for main audio |
| OnePoleSmoother | primitives/smoother.h | 1 | Cutoff frequency smoothing (optional) |
| Biquad | primitives/biquad.h | 1 | Sidechain highpass filter |
| gainToDb | core/db_utils.h | 0 | Threshold comparison (envelope to dB) |
| dbToGain | core/db_utils.h | 0 | Sensitivity gain conversion |
| constexprExp | core/db_utils.h | 0 | Log-space interpolation |
| constexprLn | core/db_utils.h | 0 | Log-space interpolation (via std::log at runtime) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: SidechainFilter is a unique name not found in codebase. Direction and FilterType enums will be declared as nested enum classes within SidechainFilter (following EnvelopeFilter pattern), avoiding any ODR conflicts.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| EnvelopeFollower | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| EnvelopeFollower | reset | `void reset() noexcept` | Yes |
| EnvelopeFollower | processSample | `[[nodiscard]] float processSample(float input) noexcept` | Yes |
| EnvelopeFollower | setAttackTime | `void setAttackTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setReleaseTime | `void setReleaseTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setSidechainEnabled | `void setSidechainEnabled(bool enabled) noexcept` | Yes |
| EnvelopeFollower | setSidechainCutoff | `void setSidechainCutoff(float hz) noexcept` | Yes |
| EnvelopeFollower | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | getCutoff | `[[nodiscard]] float getCutoff() const noexcept` | Yes |
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | read | `[[nodiscard]] float read(size_t delaySamples) const noexcept` | Yes |
| DelayLine | maxDelaySamples | `[[nodiscard]] size_t maxDelaySamples() const noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| Biquad | configure | via FilterType, cutoff, Q, gain, sampleRate | Yes |
| Biquad | process | `float process(float input)` | Yes |
| Biquad | reset | `void reset()` | Yes |
| gainToDb | N/A | `[[nodiscard]] constexpr float gainToDb(float gain) noexcept` | Yes |
| dbToGain | N/A | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class
- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class
- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dB conversion utilities
- [x] `dsp/include/krate/dsp/processors/envelope_filter.h` - Reference for composition pattern
- [x] `dsp/include/krate/dsp/processors/ducking_processor.h` - Reference for hold state machine

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| EnvelopeFollower | Has own sidechain HP filter | Disable with `setSidechainEnabled(false)` - we handle filtering ourselves |
| EnvelopeFollower | prepare() takes maxBlockSize (unused) | Pass 0 or arbitrary value |
| DelayLine | prepare() takes maxDelaySeconds not ms | Convert: `maxLookaheadMs / 1000.0f` |
| DelayLine | read(0) returns most recent sample | Use `read(delaySamples)` for actual delay |
| gainToDb | Returns kSilenceFloorDb (-144) for gain <= 0 | Ensure envelope > 0 before conversion |
| SVFMode | Different enum from EnvelopeFilter::FilterType | Map between them |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | No new Layer 0 utilities needed | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateCutoff() | Uses class-specific parameters (min/max cutoff, direction) |
| updateStateMachine() | Class-specific hold/threshold logic |
| mapEnvelopeToCutoff() | Uses log-space interpolation with class parameters |

**Decision**: All new functions are class-specific calculations. Log-space interpolation uses existing `std::log`/`std::exp` (runtime) or `detail::constexprLn`/`detail::constexprExp` (compile-time).

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from ROADMAP.md or known plans):
- 091-reactive-filter (Phase 15.2) - May share threshold/dynamics detection patterns
- EnvelopeFilter (existing) - Self-envelope filter, different approach (no hold, no lookahead)
- DuckingProcessor (existing) - Shares hold state machine pattern

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Hold state machine pattern | MEDIUM | reactive-filter, other dynamics processors | Keep local - adapt from DuckingProcessor |
| Log-space cutoff mapping | MEDIUM | Other envelope-controlled filters | Keep local - simple inline formula |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | EnvelopeFilter and SidechainFilter have different enough APIs (external vs self sidechain) |
| Hold pattern not extracted | Only 2nd consumer; DuckingProcessor has gain-specific behavior |
| Local Direction enum | Avoids dependency on EnvelopeFilter header for simple enum |

## Project Structure

### Documentation (this feature)

```text
specs/090-sidechain-filter/
├── plan.md              # This file
├── research.md          # Phase 0 output (minimal - patterns well-established)
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (API header contract)
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── sidechain_filter.h    # Main header (Layer 2)
└── tests/
    └── processors/
        └── sidechain_filter_test.cpp  # Catch2 tests
```

**Structure Decision**: Standard KrateDSP processor structure. Header-only implementation in `processors/` folder (Layer 2). Tests in corresponding `tests/processors/` folder.

---

## Phase 0: Research Summary

### Research Questions Resolved

| Question | Resolution | Rationale |
|----------|------------|-----------|
| Log-space interpolation formula | `cutoff = exp(lerp(log(minCutoff), log(maxCutoff), envelope))` | Spec clarification, standard synthesizer approach |
| Hold phase envelope tracking | Continue tracking, block release only | Spec clarification - allows filter to follow rapid changes |
| Threshold comparison domain | dB: `20*log10(envelope) > threshold` | Spec clarification - standard dynamics processor approach |
| Self-sidechain lookahead routing | Sidechain undelayed, audio delayed | Spec clarification - allows anticipatory response |
| Resting positions | Up: minCutoff, Down: maxCutoff when silent | Spec clarification - matches semantic expectations |

### Reference Implementations Reviewed

- **EnvelopeFilter**: Composition pattern (EnvelopeFollower + SVF), exponential cutoff mapping
- **DuckingProcessor**: Hold state machine (Idle/Ducking/Holding), threshold comparison, re-trigger logic
- **SampleHoldFilter**: Complex Layer 2 processor with multiple state machines, good structure reference

### Best Practices Applied

1. **EnvelopeFollower composition**: Proven pattern from EnvelopeFilter and DuckingProcessor
2. **State machine for hold**: Idle/Active/Holding states from DuckingProcessor pattern
3. **Cutoff smoothing**: Optional OnePoleSmoother to prevent clicks during rapid changes
4. **DelayLine for lookahead**: Standard pattern - delay audio, not sidechain

---

## Phase 1: Design

### Architecture Overview

```
                     ┌─────────────────────────────────────────────────────┐
                     │                  SidechainFilter                    │
                     │                                                     │
 Sidechain Input ───>│  ┌───────────────┐                                 │
                     │  │ Sidechain HP  │ (optional, Biquad)              │
                     │  │    Filter     │                                 │
                     │  └───────┬───────┘                                 │
                     │          │                                          │
                     │          v                                          │
                     │  ┌───────────────┐                                 │
                     │  │ Sensitivity   │ (gain stage)                    │
                     │  │    Gain       │                                 │
                     │  └───────┬───────┘                                 │
                     │          │                                          │
                     │          v                                          │
                     │  ┌───────────────┐     ┌─────────────┐             │
                     │  │  Envelope     │────>│   State     │             │
                     │  │  Follower     │     │  Machine    │             │
                     │  └───────────────┘     │ (Hold/Trig) │             │
                     │          │             └──────┬──────┘             │
                     │          │                    │                     │
                     │          v                    v                     │
                     │  ┌───────────────────────────────────┐             │
                     │  │     Envelope-to-Cutoff Mapping    │             │
                     │  │  cutoff = exp(lerp(log(min),      │             │
                     │  │          log(max), envelope))     │             │
                     │  └───────────────┬───────────────────┘             │
                     │                  │                                  │
                     │                  v                                  │
                     │  ┌───────────────┐                                 │
                     │  │   Cutoff      │ (optional, OnePoleSmoother)     │
                     │  │   Smoother    │                                 │
                     │  └───────┬───────┘                                 │
                     │          │                                          │
                     │          v                                          │
   Main Input ──────>│  ┌───────────────┐     ┌───────────────┐          │
                     │  │   Lookahead   │────>│     SVF       │──────────>│──> Output
                     │  │   DelayLine   │     │   (LP/BP/HP)  │          │
                     │  └───────────────┘     └───────────────┘          │
                     │                                                     │
                     └─────────────────────────────────────────────────────┘
```

### State Machine Design

```
           ┌──────────────────────────────────────┐
           │                                      │
           v                                      │
    ┌─────────────┐                               │
    │             │   envelope_dB > threshold     │
    │    Idle     │──────────────────────────────>│
    │             │                               │
    └─────────────┘                               │
           ^                                      │
           │                                      v
           │ hold expired              ┌─────────────┐
           │                           │             │
           └───────────────────────────│   Active    │
                                       │             │
                                       └─────────────┘
                                              │
                          envelope_dB < threshold
                          (start hold timer)  │
                                              v
                                       ┌─────────────┐
                                       │             │
        envelope_dB > threshold ──────>│   Holding   │
        (reset hold timer)             │             │
                                       └─────────────┘
                                              │
                                       hold timer expires
                                              │
                                              v
                                       (back to Idle)
```

**Hold Phase Behavior (FR-016):**
- During Holding state, envelope tracking continues
- Filter cutoff follows envelope changes
- Release phase is blocked (cutoff doesn't return to resting)
- Re-trigger resets hold timer, transitions back to Active

### Class Design

```cpp
namespace Krate::DSP {

/// State machine states for hold behavior
enum class SidechainFilterState : uint8_t {
    Idle = 0,     ///< Below threshold, filter at resting position
    Active = 1,   ///< Above threshold, envelope controlling filter
    Holding = 2   ///< Below threshold but in hold period
};

/// Envelope-to-cutoff mapping direction
enum class Direction : uint8_t {
    Up = 0,    ///< Louder -> higher cutoff, rests at minCutoff
    Down = 1   ///< Louder -> lower cutoff, rests at maxCutoff
};

/// Filter response type
enum class FilterType : uint8_t {
    Lowpass = 0,
    Bandpass = 1,
    Highpass = 2
};

class SidechainFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 500.0f;
    static constexpr float kMinReleaseMs = 1.0f;
    static constexpr float kMaxReleaseMs = 5000.0f;
    static constexpr float kMinThresholdDb = -60.0f;
    static constexpr float kMaxThresholdDb = 0.0f;
    static constexpr float kMinSensitivityDb = -24.0f;
    static constexpr float kMaxSensitivityDb = 24.0f;
    static constexpr float kMinCutoffHz = 20.0f;
    static constexpr float kMinResonance = 0.5f;
    static constexpr float kMaxResonance = 20.0f;
    static constexpr float kMinLookaheadMs = 0.0f;
    static constexpr float kMaxLookaheadMs = 50.0f;
    static constexpr float kMinHoldMs = 0.0f;
    static constexpr float kMaxHoldMs = 1000.0f;
    static constexpr float kMinSidechainHpHz = 20.0f;
    static constexpr float kMaxSidechainHpHz = 500.0f;

    // =========================================================================
    // Lifecycle (FR-024, FR-025, FR-026)
    // =========================================================================

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;
    [[nodiscard]] size_t getLatency() const noexcept;  // FR-026

    // =========================================================================
    // Processing (FR-019, FR-020, FR-021)
    // =========================================================================

    /// Process with external sidechain (FR-001)
    [[nodiscard]] float processSample(float mainInput, float sidechainInput) noexcept;

    /// Process with self-sidechain (FR-002)
    [[nodiscard]] float processSample(float input) noexcept;

    /// Block processing with external sidechain (FR-020)
    void process(const float* mainInput, const float* sidechainInput,
                 float* output, size_t numSamples) noexcept;

    /// Block processing in-place with external sidechain (FR-021)
    void process(float* mainInOut, const float* sidechainInput,
                 size_t numSamples) noexcept;

    /// Block processing with self-sidechain
    void process(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Sidechain Detection Parameters (FR-003 to FR-006)
    // =========================================================================

    void setAttackTime(float ms) noexcept;
    void setReleaseTime(float ms) noexcept;
    void setThreshold(float dB) noexcept;
    void setSensitivity(float dB) noexcept;

    [[nodiscard]] float getAttackTime() const noexcept;
    [[nodiscard]] float getReleaseTime() const noexcept;
    [[nodiscard]] float getThreshold() const noexcept;
    [[nodiscard]] float getSensitivity() const noexcept;

    // =========================================================================
    // Filter Response Parameters (FR-007 to FR-012)
    // =========================================================================

    void setDirection(Direction dir) noexcept;
    void setMinCutoff(float hz) noexcept;
    void setMaxCutoff(float hz) noexcept;
    void setResonance(float q) noexcept;
    void setFilterType(FilterType type) noexcept;

    [[nodiscard]] Direction getDirection() const noexcept;
    [[nodiscard]] float getMinCutoff() const noexcept;
    [[nodiscard]] float getMaxCutoff() const noexcept;
    [[nodiscard]] float getResonance() const noexcept;
    [[nodiscard]] FilterType getFilterType() const noexcept;

    // =========================================================================
    // Timing Parameters (FR-013 to FR-016)
    // =========================================================================

    void setLookahead(float ms) noexcept;
    void setHoldTime(float ms) noexcept;

    [[nodiscard]] float getLookahead() const noexcept;
    [[nodiscard]] float getHoldTime() const noexcept;

    // =========================================================================
    // Sidechain Filter Parameters (FR-017, FR-018)
    // =========================================================================

    void setSidechainFilterEnabled(bool enabled) noexcept;
    void setSidechainFilterCutoff(float hz) noexcept;

    [[nodiscard]] bool isSidechainFilterEnabled() const noexcept;
    [[nodiscard]] float getSidechainFilterCutoff() const noexcept;

    // =========================================================================
    // Monitoring (FR-027, FR-028)
    // =========================================================================

    [[nodiscard]] float getCurrentCutoff() const noexcept;
    [[nodiscard]] float getCurrentEnvelope() const noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// Update state machine and return effective envelope for cutoff
    float updateStateMachine(float envelopeDb) noexcept;

    /// Map envelope [0,1] to cutoff using log-space interpolation
    [[nodiscard]] float mapEnvelopeToCutoff(float envelope) const noexcept;

    /// Get resting cutoff based on direction
    [[nodiscard]] float getRestingCutoff() const noexcept;

    /// Update lookahead delay samples from ms
    void updateLookaheadSamples() noexcept;

    /// Update hold time in samples
    void updateHoldSamples() noexcept;

    /// Map FilterType to SVFMode
    [[nodiscard]] SVFMode mapFilterType(FilterType type) const noexcept;

    // =========================================================================
    // Composed Components
    // =========================================================================

    EnvelopeFollower envFollower_;      ///< Sidechain envelope detection
    SVF filter_;                        ///< Main filter
    DelayLine lookaheadDelay_;          ///< Audio lookahead buffer
    Biquad sidechainHpFilter_;          ///< Sidechain highpass
    OnePoleSmoother cutoffSmoother_;    ///< Optional cutoff smoothing

    // =========================================================================
    // State Machine
    // =========================================================================

    SidechainFilterState state_ = SidechainFilterState::Idle;
    size_t holdSamplesRemaining_ = 0;
    size_t holdSamplesTotal_ = 0;
    float activeEnvelope_ = 0.0f;       ///< Envelope during active/hold

    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;
    float attackMs_ = 10.0f;
    float releaseMs_ = 100.0f;
    float thresholdDb_ = -30.0f;
    float sensitivityDb_ = 0.0f;
    float sensitivityGain_ = 1.0f;

    Direction direction_ = Direction::Down;
    FilterType filterType_ = FilterType::Lowpass;
    float minCutoffHz_ = 200.0f;
    float maxCutoffHz_ = 2000.0f;
    float resonance_ = 8.0f;

    float lookaheadMs_ = 0.0f;
    size_t lookaheadSamples_ = 0;
    float holdMs_ = 0.0f;

    bool sidechainHpEnabled_ = false;
    float sidechainHpCutoffHz_ = 80.0f;

    // =========================================================================
    // Monitoring State
    // =========================================================================

    float currentCutoff_ = 200.0f;
    float currentEnvelope_ = 0.0f;

    // =========================================================================
    // Lifecycle State
    // =========================================================================

    bool prepared_ = false;
    float maxCutoffLimit_ = 20000.0f;   ///< Nyquist-safe limit
};

} // namespace Krate::DSP
```

### Key Algorithm Details

#### 1. Log-Space Envelope-to-Cutoff Mapping (FR-012)

```cpp
float SidechainFilter::mapEnvelopeToCutoff(float envelope) const noexcept {
    // Clamp envelope to [0, 1]
    envelope = std::clamp(envelope, 0.0f, 1.0f);

    // Log-space interpolation: exp(lerp(log(min), log(max), t))
    const float logMin = std::log(minCutoffHz_);
    const float logMax = std::log(maxCutoffHz_);

    float t = envelope;
    if (direction_ == Direction::Down) {
        t = 1.0f - t;  // Invert for down direction
    }

    const float logCutoff = logMin + t * (logMax - logMin);
    return std::exp(logCutoff);
}
```

#### 2. Hold State Machine (FR-014, FR-015, FR-016)

```cpp
float SidechainFilter::updateStateMachine(float envelopeDb) noexcept {
    const bool aboveThreshold = envelopeDb > thresholdDb_;

    switch (state_) {
        case SidechainFilterState::Idle:
            if (aboveThreshold) {
                state_ = SidechainFilterState::Active;
                activeEnvelope_ = envFollower_.getCurrentValue();
            }
            // Return 0 to use resting cutoff
            return 0.0f;

        case SidechainFilterState::Active:
            activeEnvelope_ = envFollower_.getCurrentValue();
            if (!aboveThreshold) {
                if (holdSamplesTotal_ > 0) {
                    state_ = SidechainFilterState::Holding;
                    holdSamplesRemaining_ = holdSamplesTotal_;
                } else {
                    state_ = SidechainFilterState::Idle;
                    return 0.0f;  // Immediate release
                }
            }
            return activeEnvelope_;

        case SidechainFilterState::Holding:
            // Continue tracking envelope during hold (FR-016)
            activeEnvelope_ = envFollower_.getCurrentValue();

            if (aboveThreshold) {
                // Re-trigger: reset hold timer, go back to Active
                state_ = SidechainFilterState::Active;
            } else if (holdSamplesRemaining_ > 0) {
                --holdSamplesRemaining_;
            } else {
                // Hold expired: begin release
                state_ = SidechainFilterState::Idle;
                return 0.0f;
            }
            return activeEnvelope_;  // Keep tracking during hold
    }

    return 0.0f;
}
```

#### 3. Threshold Comparison (FR-005)

```cpp
// In processSample():
const float envelope = envFollower_.processSample(sidechainInput * sensitivityGain_);
currentEnvelope_ = envelope;

// Convert to dB for threshold comparison
const float envelopeDb = (envelope > 0.0f) ? gainToDb(envelope) : kSilenceFloorDb;

// Update state machine with dB comparison
const float effectiveEnvelope = updateStateMachine(envelopeDb);
```

#### 4. Self-Sidechain Lookahead Routing (FR-013)

```cpp
float SidechainFilter::processSample(float input) noexcept {
    // Self-sidechain mode: sidechain sees undelayed signal
    // Audio output is delayed by lookahead amount

    // 1. Process sidechain FIRST (undelayed input)
    float sidechainSignal = input;
    if (sidechainHpEnabled_) {
        sidechainSignal = sidechainHpFilter_.process(input);
    }
    const float envelope = envFollower_.processSample(sidechainSignal * sensitivityGain_);

    // 2. Write current sample to delay line
    lookaheadDelay_.write(input);

    // 3. Read delayed sample for audio processing
    const float delayedInput = (lookaheadSamples_ > 0)
        ? lookaheadDelay_.read(lookaheadSamples_)
        : input;

    // 4. Calculate cutoff from envelope
    // ... (state machine, mapping)

    // 5. Filter the DELAYED audio
    return filter_.process(delayedInput);
}
```

#### 5. Resting Position (Edge Case)

```cpp
float SidechainFilter::getRestingCutoff() const noexcept {
    // Direction::Up rests at minCutoff when silent (filter closed)
    // Direction::Down rests at maxCutoff when silent (filter open)
    return (direction_ == Direction::Up) ? minCutoffHz_ : maxCutoffHz_;
}
```

### File Structure

```
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── sidechain_filter.h    # Complete header-only implementation
└── tests/
    └── processors/
        └── sidechain_filter_test.cpp
```

### Test Strategy

#### Test Categories

1. **Unit Tests - Envelope to Cutoff Mapping**
   - Log-space interpolation correctness
   - Direction Up: envelope 0->1 maps minCutoff->maxCutoff
   - Direction Down: envelope 0->1 maps maxCutoff->minCutoff
   - Perceptual linearity (equal octave steps)

2. **Unit Tests - State Machine**
   - Idle -> Active on threshold crossing
   - Active -> Holding on signal drop
   - Holding -> Active on re-trigger (hold reset)
   - Holding -> Idle after hold expires
   - Envelope tracking during hold phase

3. **Unit Tests - Threshold Comparison**
   - dB domain comparison correctness
   - Sensitivity gain affects threshold effectively
   - Silent input stays in Idle

4. **Unit Tests - Lookahead Routing**
   - Self-sidechain: sidechain sees undelayed signal
   - Self-sidechain: audio output is delayed
   - Latency reported correctly
   - External sidechain: both paths work independently

5. **Unit Tests - Resting Positions**
   - Direction::Up rests at minCutoff when silent
   - Direction::Down rests at maxCutoff when silent
   - Smooth transition from active to resting

6. **Integration Tests**
   - Full signal chain with kick drum sidechain
   - Timing accuracy (attack/release within 5%)
   - Hold time accuracy (within 1ms)
   - Click-free operation during parameter changes

7. **Edge Case Tests**
   - NaN/Inf input handling
   - minCutoff == maxCutoff (static filter)
   - Zero hold time (direct release)
   - Zero lookahead (no latency)
   - Sidechain filter on/off

### Timing Verification Approach

```cpp
// Attack time test (SC-001)
TEST_CASE("Attack time within 5% tolerance") {
    SidechainFilter filter;
    filter.prepare(48000.0, 512);
    filter.setAttackTime(10.0f);  // 10ms

    // Send step input, measure time to reach 99% of target
    const float targetEnvelope = 1.0f;
    const float threshold99 = 0.99f * targetEnvelope;

    size_t samplesToReach = 0;
    for (size_t i = 0; i < 48000; ++i) {
        filter.processSample(0.0f, 1.0f);  // Step sidechain
        if (filter.getCurrentEnvelope() >= threshold99) {
            samplesToReach = i;
            break;
        }
    }

    const float actualMs = samplesToReach / 48.0f;
    REQUIRE(actualMs == Approx(10.0f).margin(0.5f));  // 5% of 10ms
}
```

## Complexity Tracking

No Constitution violations identified. Design follows established patterns from EnvelopeFilter and DuckingProcessor.

---

## Implementation Checklist

### Pre-Implementation
- [ ] Create `research.md` (minimal - patterns established)
- [ ] Create `data-model.md` with state and parameter documentation
- [ ] Create `quickstart.md` with usage examples
- [ ] Create `contracts/sidechain_filter.h` (API contract)

### Implementation (Phase 3 - via `/speckit.tasks`)
- [ ] Write failing tests for envelope-to-cutoff mapping
- [ ] Implement log-space mapping
- [ ] Write failing tests for state machine
- [ ] Implement hold state machine
- [ ] Write failing tests for lookahead routing
- [ ] Implement DelayLine integration
- [ ] Write failing tests for threshold comparison
- [ ] Implement dB domain comparison
- [ ] Write integration tests
- [ ] Complete full implementation
- [ ] Verify all tests pass
- [ ] Run pluginval (if integrated into plugin)
- [ ] Update architecture documentation

### Post-Implementation
- [ ] Fill compliance table in spec.md
- [ ] Update `specs/_architecture_/layer-2-processors.md`
