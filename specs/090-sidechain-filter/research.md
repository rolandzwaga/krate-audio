# Research: Sidechain Filter Processor

**Feature**: 090-sidechain-filter
**Date**: 2026-01-23

## Summary

Research for SidechainFilter is minimal as all required patterns are well-established in the existing codebase. The spec clarifications resolved all technical questions.

## Research Questions

### Q1: Log-Space Envelope-to-Cutoff Mapping

**Decision**: Use `cutoff = exp(lerp(log(minCutoff), log(maxCutoff), envelope))`

**Rationale**: This is the standard synthesizer approach for perceptually linear frequency sweeps. The formula interpolates linearly in log-frequency space, meaning equal changes in envelope produce equal perceived pitch changes (octaves).

**Alternatives Considered**:
- Linear interpolation: Rejected - produces perceptually non-linear sweeps (faster at high frequencies)
- Exponential mapping with arbitrary base: Rejected - log-space is more intuitive and standard

**Implementation**:
```cpp
float mapEnvelopeToCutoff(float envelope) const noexcept {
    const float logMin = std::log(minCutoffHz_);
    const float logMax = std::log(maxCutoffHz_);
    float t = (direction_ == Direction::Down) ? (1.0f - envelope) : envelope;
    return std::exp(logMin + t * (logMax - logMin));
}
```

---

### Q2: Hold Phase Envelope Tracking

**Decision**: Continue tracking envelope during hold phase, but block release transition.

**Rationale**: This allows the filter to respond to rapid sidechain changes during hold (e.g., multiple kick hits) while still preventing chattering release. The filter cutoff follows envelope variations; only the state machine transition to Idle is blocked.

**Alternatives Considered**:
- Freeze cutoff at hold start value: Rejected - less musical, doesn't respond to new transients
- Ignore envelope entirely during hold: Rejected - same issue

**Implementation**: State machine tracks `activeEnvelope_` in Holding state and uses it for cutoff calculation.

---

### Q3: Threshold Comparison Domain

**Decision**: Compare in dB domain: `envelopeDb = 20*log10(envelope) > threshold`

**Rationale**: Standard dynamics processor approach. Users think in dB for threshold settings. Matches DuckingProcessor behavior.

**Alternatives Considered**:
- Linear comparison with converted threshold: Rejected - less intuitive threshold values
- Percentage-based: Rejected - non-standard

**Implementation**: Use `gainToDb()` from `db_utils.h` which handles edge cases (returns -144dB for zero/negative).

---

### Q4: Self-Sidechain Lookahead Routing

**Decision**: Sidechain analyzes undelayed signal; audio path is delayed by lookahead amount.

**Rationale**: This creates anticipatory filter response - the filter begins changing before the transient appears in the audio output, resulting in smoother ducking without clipping attack transients.

**Alternatives Considered**:
- Delay both paths equally: Rejected - defeats purpose of lookahead
- No lookahead in self-sidechain: Rejected - removes useful feature

**Implementation**: Write input to DelayLine, read delayed output for SVF, process sidechain with undelayed input.

---

### Q5: Resting Positions

**Decision**:
- Direction::Up rests at minCutoff when silent (filter closed)
- Direction::Down rests at maxCutoff when silent (filter open)

**Rationale**: Matches semantic expectations. "Up" means louder signal opens the filter, so silent = closed. "Down" means louder signal closes the filter, so silent = open.

**Alternatives Considered**:
- Opposite mapping: Rejected - counterintuitive naming
- Configurable resting position: Rejected - adds complexity without clear benefit

---

## Reference Implementations

### EnvelopeFilter (Layer 2)
- **Pattern**: Composition of EnvelopeFollower + SVF
- **Cutoff Mapping**: Exponential (`minFreq * pow(freqRatio, modAmount)`)
- **Key Difference**: Self-envelope only, no external sidechain, no hold, no lookahead

### DuckingProcessor (Layer 2)
- **Pattern**: Idle/Ducking/Holding state machine
- **Threshold**: dB domain comparison
- **Hold Behavior**: Maintains gain reduction, re-trigger resets timer
- **Key Difference**: Controls gain not filter cutoff

### SampleHoldFilter (Layer 2)
- **Pattern**: Complex state machine with multiple sources
- **Structure**: Good reference for Layer 2 processor organization
- **Key Difference**: Sample & hold vs continuous envelope

---

## API Design Decisions

### Processing Method Overloads

Three processing variants to match spec requirements:

1. `processSample(float mainInput, float sidechainInput)` - External sidechain (FR-001, FR-019)
2. `processSample(float input)` - Self-sidechain (FR-002)
3. Block variants for efficiency (FR-020, FR-021)

### Parameter Grouping

Parameters organized by function following existing processor conventions:
- Sidechain detection: attack, release, threshold, sensitivity
- Filter response: direction, min/max cutoff, resonance, type
- Timing: lookahead, hold
- Sidechain filtering: enabled, cutoff

### Monitoring

Two monitoring outputs for UI metering (FR-027, FR-028):
- `getCurrentCutoff()` - Real-time filter frequency
- `getCurrentEnvelope()` - Current envelope value

---

## Dependencies Verified

All dependencies exist and APIs are documented in plan.md:

| Component | Location | Verified API |
|-----------|----------|--------------|
| EnvelopeFollower | processors/envelope_follower.h | Yes |
| SVF | primitives/svf.h | Yes |
| DelayLine | primitives/delay_line.h | Yes |
| OnePoleSmoother | primitives/smoother.h | Yes |
| Biquad | primitives/biquad.h | Yes |
| gainToDb/dbToGain | core/db_utils.h | Yes |

---

## Risk Assessment

**Low Risk**: All patterns well-established, no new algorithms, composition of proven components.

**Potential Issues**:
1. Cutoff smoothing: May need optional smoother to prevent clicks - included in design
2. Lookahead latency reporting: Must match DelayLine samples - verified API
3. Hold timer precision: Uses size_t samples, adequate for 1ms accuracy at 192kHz
