# Feature Specification: Chaos Attractor Waveshaper

**Feature Branch**: `104-chaos-waveshaper`
**Created**: 2026-01-26
**Status**: Draft
**Input**: User description: "Implement Chaos Attractor Waveshaper for organic, evolving distortion using chaotic mathematical systems (Lorenz, Rossler, Chua, Henon) as transfer functions - Layer 1 primitive"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Chaos Distortion (Priority: P1)

A sound designer wants to apply distortion that "breathes" and evolves over time, creating organic textures that are impossible with static waveshaping. The chaos attractor's state continuously evolves, causing the transfer function characteristics to change moment-to-moment, producing "living" distortion.

**Why this priority**: This is the core value proposition - distortion that changes over time without external modulation. The attractor's intrinsic dynamics create movement that sounds organic and unpredictable, distinguishing it from all static waveshapers.

**Independent Test**: Can be fully tested by feeding a constant-amplitude sine wave through the processor and verifying that output characteristics (harmonic content, RMS level) vary over time even with constant input and no parameter changes.

**Acceptance Scenarios**:

1. **Given** a 440Hz sine wave at constant amplitude, **When** processed with Lorenz model at chaosAmount=1.0 for 5 seconds, **Then** spectral analysis shows time-varying harmonic content (not a static spectrum).
2. **Given** any input signal, **When** chaosAmount is set to 0.0, **Then** output equals input (perfect bypass).
3. **Given** silence (zeros), **When** processed in any mode, **Then** output is silence (attractor evolves but produces no audio artifacts).

---

### User Story 2 - Input-Reactive Chaos (Priority: P2)

A producer wants the distortion character to respond to the input signal - louder sounds drive the attractor into wilder behavior, while quiet passages let it settle into gentler patterns. The input coupling parameter determines how much the input signal perturbs the attractor state.

**Why this priority**: Input coupling transforms passive "random distortion" into signal-reactive processing where the audio interacts with the chaos system, creating unique cause-and-effect relationships between dynamics and distortion.

**Independent Test**: Can be tested by comparing output with inputCoupling=0.0 vs inputCoupling=1.0 - the coupled version should show correlation between input envelope and distortion character.

**Acceptance Scenarios**:

1. **Given** inputCoupling=1.0 and a drum loop with clear transients, **When** processed, **Then** attractor state shows larger perturbations during transients than during quiet periods.
2. **Given** inputCoupling=0.0 and the same drum loop, **When** processed, **Then** attractor evolution is independent of input amplitude.
3. **Given** inputCoupling=0.5, **When** compared to inputCoupling=1.0 on same material, **Then** input-correlated variation is proportionally smaller.

---

### User Story 3 - Model Selection (Priority: P2)

A sound designer wants to choose between different chaos models to get different distortion flavors - Lorenz for swirling unpredictability, Rossler for smoother patterns, Chua for analog-circuit character, and Henon for discrete sharp transitions.

**Why this priority**: Different chaos models have genuinely different mathematical character that translates to audible differences, expanding the creative palette.

**Independent Test**: Can be tested by processing identical source material through each model with identical parameters and verifying that spectral/temporal characteristics differ measurably between models.

**Acceptance Scenarios**:

1. **Given** identical input and parameters, **When** processed through Lorenz vs Rossler, **Then** spectral analysis shows measurably different harmonic envelopes.
2. **Given** the Henon (2D map) model, **When** processing continuous audio, **Then** output exhibits more abrupt transitions than continuous-flow attractors (Lorenz, Rossler).
3. **Given** the Chua model, **When** compared to Lorenz, **Then** double-scroll attractor produces distinct bi-modal behavior.

---

### User Story 4 - Attractor Speed Control (Priority: P3)

A user wants to control how fast the attractor evolves - slow for subtle movement, fast for rapid textural changes. The attractorSpeed parameter scales the integration time step.

**Why this priority**: Speed control is essential for matching the chaos evolution rate to musical context (tempo, note length, texture density).

**Independent Test**: Can be tested by running the same input through two instances with different speed settings and verifying that higher speed produces more variation over the same time period.

**Acceptance Scenarios**:

1. **Given** attractorSpeed=0.1 (slow), **When** processing 10 seconds of audio, **Then** attractor completes fewer "orbits" than with attractorSpeed=1.0.
2. **Given** attractorSpeed=10.0 (fast), **When** processing audio, **Then** distortion character changes more rapidly (higher modulation rate).
3. **Given** any speed setting, **When** attractor evolves, **Then** all state variables remain bounded (no runaway to infinity).

---

### Edge Cases

- What happens when input signal contains NaN/Inf values? (Input is sanitized; NaN treated as 0, Inf clamped)
- How does the system handle sample rate changes? (Integration step is sample-rate-compensated)
- What happens if attractor diverges to infinity? (Automatic reset to stable initial conditions with minimal audible artifact)
- How does the Henon map (discrete) integrate with continuous audio? (Interpolation between map iterations for smooth output)

## Clarifications

### Session 2026-01-26

- Q: The spec describes waveshaping where "attractor state modulates the transfer function" (FR-020, FR-022), but doesn't specify the exact mathematical relationship. → A: The attractor's normalized X component modulates the drive of a bounded soft-clipping transfer function. The drive parameter is mapped linearly from a defined minimum to maximum range, producing continuous, time-varying nonlinearity. A tanh-based waveshaper is a suitable reference implementation.
- Q: FR-018 requires automatic reset when state variables exceed "safe bounds", but doesn't specify what those bounds are. → A: Per-model bounds based on attractor geometry: Lorenz ±50, Rossler ±20, Chua ±10, Henon ±5 (matches expected dynamics)
- Q: FR-016 specifies Chua attractor equations with "piecewise-linear h(x)", but doesn't define the h(x) function or the alpha/beta parameter values. → A: Standard Chua: h(x) = m1*x + 0.5*(m0-m1)*(abs(x+1)-abs(x-1)), alpha=15.6, beta=28.0, m0=-1.143, m1=-0.714
- Q: FR-026 specifies the input perturbation formula but doesn't define the `perturbationScale` constant, which is critical for balancing input influence vs. attractor stability. → A: Per-model scaling (Lorenz: 0.1, Rossler: 0.1, Chua: 0.08, Henon: 0.05) to account for different attractor sensitivities
- Q: FR-019 requires sample-rate compensation for integration timestep, but doesn't specify the base timestep or how it scales. → A: Adaptive per-model base timesteps at 44100 Hz: Lorenz dt=0.005, Rossler dt=0.02, Chua dt=0.01, Henon uses per-iteration update (dt=1.0). Scale by (44100.0 / sampleRate) for other rates.

## Requirements *(mandatory)*

### Functional Requirements

**Core Interface:**
- **FR-001**: System MUST provide a `prepare(double sampleRate)` method that initializes attractor state and configures sample-rate-dependent integration step
- **FR-002**: System MUST provide a `reset()` method that reinitializes attractor state to stable starting conditions
- **FR-003**: System MUST provide a `process(float x) noexcept` method for sample-by-sample processing
- **FR-004**: System MUST provide a `processBlock(float* buffer, size_t n) noexcept` method for block processing

**Model Selection:**
- **FR-005**: System MUST support `ChaosModel::Lorenz` - classic 3D continuous attractor with sigma, rho, beta parameters
- **FR-006**: System MUST support `ChaosModel::Rossler` - smoother 3D continuous attractor with a, b, c parameters
- **FR-007**: System MUST support `ChaosModel::Chua` - double-scroll electronic circuit attractor with piecewise-linear nonlinearity
- **FR-008**: System MUST support `ChaosModel::Henon` - 2D discrete map with sharp transitions, interpolated for continuous output

**Parameters:**
- **FR-009**: System MUST provide `setModel(ChaosModel model)` to select the chaos algorithm
- **FR-010**: System MUST provide `setChaosAmount(float amount)` where amount is clamped to [0.0, 1.0]; amount=0.0 is bypass, amount=1.0 is full chaos processing
- **FR-011**: System MUST provide `setAttractorSpeed(float speed)` where speed is clamped to [0.01, 100.0]; speed=1.0 is nominal evolution rate
- **FR-012**: System MUST provide `setInputCoupling(float coupling)` where coupling is clamped to [0.0, 1.0]; coupling determines how much input amplitude perturbs the attractor state

**Attractor Dynamics:**
- **FR-013**: Lorenz attractor MUST use Euler integration with equations: dx/dt = sigma*(y-x), dy/dt = x*(rho-z)-y, dz/dt = x*y - beta*z
- **FR-014**: Lorenz attractor MUST use standard parameters by default: sigma=10.0, rho=28.0, beta=8.0/3.0
- **FR-015**: Rossler attractor MUST use equations: dx/dt = -y-z, dy/dt = x+a*y, dz/dt = b+z*(x-c) with default a=0.2, b=0.2, c=5.7
- **FR-016**: Chua attractor MUST use equations: dx/dt = alpha*(y-x-h(x)), dy/dt = x-y+z, dz/dt = -beta*y with piecewise-linear h(x) = m1*x + 0.5*(m0-m1)*(abs(x+1)-abs(x-1)) and default parameters alpha=15.6, beta=28.0, m0=-1.143, m1=-0.714
- **FR-017**: Henon map MUST use equations: x[n+1] = 1 - a*x[n]^2 + y[n], y[n+1] = b*x[n] with default a=1.4, b=0.3
- **FR-018**: All attractors MUST have bounded state - if any state variable exceeds per-model safe bounds (Lorenz: ±50, Rossler: ±20, Chua: ±10, Henon: ±5), attractor MUST reset to stable initial conditions
- **FR-019**: Integration time step MUST use per-model base timesteps at 44100 Hz (Lorenz: 0.005, Rossler: 0.02, Chua: 0.01, Henon: 1.0 per iteration) and scale by (44100.0 / sampleRate) to maintain consistent attractor speed across sample rates

**Transfer Function:**
- **FR-020**: The attractor output (normalized to [-1, 1]) SHALL modulate the waveshaping transfer function
- **FR-021**: Transfer function MUST be: output = lerp(input, waveshape(input, attractorState), chaosAmount)
- **FR-022**: Waveshape function MUST use attractor's normalized X component to modulate a drive parameter for bounded soft-clipping (e.g., tanh-based transfer function with time-varying drive mapped from minimum to maximum range)
- **FR-023**: When chaosAmount=0.0, output MUST equal input exactly (no processing)
- **FR-024**: When chaosAmount=1.0, output MUST be the full chaos-modulated waveshape

**Input Coupling:**
- **FR-025**: When inputCoupling > 0.0, input signal amplitude SHALL perturb attractor state variables
- **FR-026**: Perturbation formula MUST be: state += inputCoupling * inputAmplitude * perturbationScale, where perturbationScale is per-model (Lorenz: 0.1, Rossler: 0.1, Chua: 0.08, Henon: 0.05)
- **FR-027**: Perturbation MUST NOT cause attractor to diverge (safe bounds enforcement remains active)

**Real-Time Safety:**
- **FR-028**: The `process()` and `processBlock()` methods MUST be noexcept and perform no heap allocations
- **FR-029**: All internal buffers and state MUST be allocated during `prepare()`, not during processing
- **FR-030**: Denormal flushing MUST be applied to attractor state variables to prevent CPU spikes

**Numerical Stability:**
- **FR-031**: NaN input values MUST be treated as 0.0
- **FR-032**: Infinity input values MUST be clamped to [-1.0, 1.0]
- **FR-033**: Attractor state variables MUST be checked for NaN/Inf after each integration step; if detected, reset to initial conditions

**Oversampling (Constitution Principle X):**
- **FR-034**: Waveshaping MUST use internal 2x oversampling to prevent aliasing artifacts from the nonlinear transfer function
- **FR-035**: Oversampling MUST use the existing `Oversampler<2, 1>` primitive from `primitives/oversampler.h` (mono, 2x factor)
- **FR-036**: The `setModel()` method MUST handle invalid enum values by defaulting to `ChaosModel::Lorenz` without crashing

### Key Entities

- **ChaosModel**: Enumeration defining the four chaos attractor types (Lorenz, Rossler, Chua, Henon)
- **ChaosWaveshaper**: The main Layer 1 primitive class containing attractor state and processing methods
- **AttractorState**: Internal struct holding x, y, z (or x, y for Henon) state variables for each model

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Processing a 440Hz sine wave with chaosAmount=1.0 MUST produce time-varying output spectrum (measured as spectral flux > 0.1 over 5 seconds using 2048-point FFT with 512-sample hop, or verified by qualitative visual inspection of spectrogram)
- **SC-002**: chaosAmount=0.0 MUST produce output identical to input (bit-exact or < -120dB difference)
- **SC-003**: Silence in MUST produce silence out (noise floor < -120dB)
- **SC-004**: CPU usage MUST be < 0.1% per instance at 44.1kHz (Layer 1 primitive budget)
- **SC-005**: Attractor state MUST remain bounded for 10+ minutes of continuous processing without manual reset
- **SC-006**: All four models (Lorenz, Rossler, Chua, Henon) MUST produce audibly distinct results on identical input
- **SC-007**: Speed parameter MUST scale evolution rate proportionally (2x speed = ~2x state change rate over time)
- **SC-008**: Input coupling MUST produce measurable correlation between input envelope and attractor perturbation when coupling > 0

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Input signals are normalized to [-1.0, 1.0] range
- Sample rates from 44100Hz to 192000Hz are supported
- Mono processing only; stereo handled by instantiating two processors
- Attractor integration uses Euler method (sufficient for audio-rate chaos generation)
- Default attractor parameters produce chaotic behavior (not periodic or divergent)

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `Oversampler<2, 1>` | `dsp/include/krate/dsp/primitives/oversampler.h` | **REQUIRED** - 2x mono oversampling for anti-aliased waveshaping (FR-034, FR-035) |
| `Xorshift32` | `dsp/include/krate/dsp/core/random.h` | Can seed attractor initial conditions for reproducible chaos |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | May smooth parameter changes (speed, coupling) |
| `Waveshaper` | `dsp/include/krate/dsp/primitives/waveshaper.h` | Reference for transfer function patterns; could compose for base saturation curve |
| `StochasticFilter` (Lorenz mode) | `dsp/include/krate/dsp/processors/stochastic_filter.h` | Contains existing Lorenz attractor implementation - reference for integration approach |
| `detail::flushDenormal()` | `dsp/include/krate/dsp/core/db_utils.h` | Denormal flushing utility |
| `detail::isNaN()`, `detail::isInf()` | `dsp/include/krate/dsp/core/db_utils.h` | NaN/Inf detection utilities |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "ChaosWaveshaper" dsp/ plugins/  # No existing implementation found
grep -r "class.*Lorenz" dsp/ plugins/    # No standalone Lorenz class (embedded in StochasticFilter)
grep -r "Rossler\|Chua\|Henon" dsp/ plugins/  # No existing implementations found
```

**Search Results Summary**: No existing ChaosWaveshaper implementation. The Lorenz attractor is implemented inline in StochasticFilter but is tightly coupled to filter modulation. Rossler, Chua, and Henon attractors are not implemented. The Lorenz implementation in StochasticFilter can serve as reference for integration approach and numerical stability patterns.

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (Layer 1 primitives):

- Future chaos-based LFO could reuse attractor implementations
- Stochastic delay modulation could use chaos sources
- Generative sequencer components could use chaos state

**Potential shared components** (preliminary, refined in plan.md):

- Attractor integration kernels (Lorenz, Rossler, Chua, Henon) could be extracted as standalone functions for reuse
- Bounded-state management pattern (reset on divergence) is reusable
- Input coupling pattern (signal perturbing internal state) is a useful primitive pattern

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-007 | | |
| FR-008 | | |
| FR-009 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| FR-016 | | |
| FR-017 | | |
| FR-018 | | |
| FR-019 | | |
| FR-020 | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| FR-024 | | |
| FR-025 | | |
| FR-026 | | |
| FR-027 | | |
| FR-028 | | |
| FR-029 | | |
| FR-030 | | |
| FR-031 | | |
| FR-032 | | |
| FR-033 | | |
| FR-034 | | |
| FR-035 | | |
| FR-036 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
