# Research: Waveguide String Resonance

**Feature Branch**: `129-waveguide-string-resonance`
**Date**: 2026-03-22

---

## R-001: Dispersion Allpass Design Method

**Question**: Which dispersion allpass design method should be used for the 4-section biquad cascade modeling stiff string inharmonicity?

**Decision**: Use the Abel, Valimaki & Smith (2010) allpass group delay approximation method, implemented as direct coefficient computation from the inharmonicity coefficient B and fundamental frequency f0.

**Rationale**: The spec explicitly lists Abel, Valimaki & Smith (2010) as "state-of-the-art" among the three candidate methods. This method designs a cascade of 2nd-order allpass filters whose group delay approximates the frequency-dependent phase shift caused by string stiffness. The target group delay at harmonic n is derived from Fletcher's formula: `tau(f) = (f_stretched - f_harmonic) / f0` expressed as a phase-to-delay conversion. With 4 biquad sections, this provides adequate approximation for guitar and moderate-stiffness timbres per the spec's Phase 3 scope.

**Alternatives Considered**:
- Van Duyne & Smith (1994): Original method using nested allpass structures. More complex to implement, less numerically stable for high B values.
- Rauhala & Valimaki (2006): Improved approximation via warp-based design. Good accuracy but requires iterative optimization that complicates real-time coefficient updates.

**Implementation Approach**: Pre-compute allpass coefficients at note onset (stiffness is frozen per FR-010). The 4 biquad sections use the existing `Biquad` class from `dsp/include/krate/dsp/primitives/biquad.h` with `FilterType::Allpass` configuration. Coefficients are computed using the relationship between B, f0, and the target group delay at each section's center frequency.

---

## R-002: Thiran Allpass Fractional Delay

**Question**: How should fractional delay tuning be implemented within the feedback loop?

**Decision**: Use 1st-order Thiran allpass interpolation with coefficient `eta = (1 - Delta) / (1 + Delta)` where Delta is the fractional part of the total loop delay.

**Rationale**: Thiran allpass provides maximally flat group delay at DC (Thiran, 1971). The existing `DelayLine::readAllpass()` already implements a 1st-order allpass interpolation, but it uses a different coefficient formulation and stores state internally to the delay line. For the WaveguideString, we need an explicit external Thiran allpass filter to properly account for its group delay in the total loop delay calculation (FR-003, FR-004).

**Alternatives Considered**:
- Lagrange interpolation: MUST NOT be used per FR-004 -- non-unitary gain `|H| < 1` at high frequencies causes pitch-dependent damping inside the feedback loop.
- Higher-order Thiran (2nd, 3rd order): More accurate but adds more group delay to subtract from the loop, reducing the usable integer delay range at high frequencies. 1st-order is standard for string waveguides.
- Using `DelayLine::readAllpass()` directly: The internal allpass state of DelayLine is tied to a single read point. Since we have two delay segments, we need independent allpass state management.

**Implementation**: Implement as a simple inline struct `ThiranAllpass1` with `float state` and `float process(float input, float eta)` that implements `y = eta * (x - state) + x_prev; state = y`. The group delay at DC is `Delta / (1 + Delta)` samples.

---

## R-003: Loss Filter Design (Weighted One-Zero)

**Question**: How should the loss filter be designed to provide both frequency-independent and frequency-dependent damping?

**Decision**: Implement as `H(z) = rho * [(1 - S) + S * z^-1]` per FR-005, where rho is the frequency-independent loss per round trip and S is the brightness parameter controlling spectral tilt.

**Rationale**: This is the standard Extended Karplus-Strong (EKS) loss filter. At S=0, it's a pure gain (flat decay across spectrum). At S=0.5, it's the original KS averaging filter `(x + x_prev) / 2`. The existing `KarplusStrong` uses a separate `OnePoleLP` for damping; the WaveguideString spec requires the combined one-zero form for more physically accurate and controllable behavior.

**Key Formulas**:
- `rho = 10^(-3 / (T60 * f0))` -- per FR-005
- Phase delay at f0: `D_loss = (S * sin(2*pi*f0/fs)) / ((1-S) + S*cos(2*pi*f0/fs))` / (2*pi*f0/fs)
- Gain at f0: `|H(f0)| = rho * sqrt((1-S)^2 + 2*S*(1-S)*cos(w0) + S^2)` where w0 = 2*pi*f0/fs
- Passivity: `|H(e^{j*omega})| <= 1` for all omega when rho <= 1 and 0 <= S <= 0.5

**Implementation**: Implement as a simple 1-tap FIR `y = rho * ((1-S)*x + S*x_prev)`. Store `x_prev` for state. Compute rho and S at note onset or when decay/brightness change (with smoothing per FR-039).

---

## R-004: DC Blocker Configuration for In-Loop vs Out-of-Loop

**Question**: Should the DC blocker be inside or outside the feedback loop, and what cutoff frequency?

**Decision**: Place the DC blocker inside the feedback loop with R = 0.9995 at 44.1 kHz (fc ~ 3.5 Hz), as specified in FR-008.

**Rationale**: An in-loop DC blocker prevents DC accumulation at the source (each iteration removes a tiny amount of DC). The very low cutoff (3.5 Hz) minimizes pitch interaction -- phase contribution at the lowest playable F0 (~20 Hz) is < 0.01 samples per FR-008. The existing `DCBlocker` class supports configurable R values through its `prepare(sampleRate, cutoffHz)` method.

**Sample-Rate Adaptation**: R must be recalculated for each sample rate. The relationship is `R = exp(-2*pi*fc/fs)`. For fc = 3.5 Hz:
- 44.1 kHz: R = 0.99950
- 48 kHz: R = 0.99954
- 96 kHz: R = 0.99977
- 192 kHz: R = 0.99989

**Implementation**: Use the existing `DCBlocker` class from `dsp/include/krate/dsp/primitives/dc_blocker.h`. Configure with `prepare(sampleRate, 3.5f)` for in-loop operation.

---

## R-005: Pick Position Comb Filter

**Question**: How should the pick-position comb filter be implemented on the excitation signal?

**Decision**: Implement as a simple FIR comb filter `H(z) = 1 - z^{-M}` where `M = round(beta * N)`, applied to the excitation signal before injection into the delay line.

**Rationale**: This creates spectral nulls at harmonics that are integer multiples of `1/beta`. The KarplusStrong class already implements pick position (see `setPickPosition()` and the excitation filling logic), providing a reference. The comb filter is applied at note onset and frozen -- it modifies the excitation buffer, not the feedback loop.

**Implementation**: At note onset, generate the excitation buffer (noise burst), then apply the comb filter in-place: `excitation[i] -= excitation[i - M]` for i >= M. This is equivalent to the transfer function above. The buffer length is `N` (one period), so the comb creates nulls within one period of the excitation.

---

## R-006: Energy Normalisation Across F0

**Question**: How should energy be normalized across the pitch range to ensure consistent perceived loudness?

**Decision**: Two-stage normalisation per FR-026 and FR-027:
1. **F0-based scaling**: Multiply excitation amplitude by `sqrt(f0 / f_ref)` where f_ref = 261.6 Hz (middle C).
2. **Loop gain compensation**: At note onset, compute `G_total = |H_loss(f0)| * |H_dc(f0)| * |H_dispersion(f0)|` and multiply excitation by `1 / G_total`.

**Rationale**: Low-frequency notes have longer delay lines storing more energy per unit excitation. The sqrt(f0/f_ref) factor compensates for the energy density being proportional to loop length (number of samples). Loop gain compensation ensures that different filter configurations (varying stiffness, brightness) don't cause level variations.

**Implementation**: Compute both factors at note onset and combine into a single excitation gain scalar.

---

## R-007: IResonator Interface Design

**Question**: How should the IResonator interface be designed for maximum compatibility between ModalResonatorBank and WaveguideString?

**Decision**: Define IResonator as a pure virtual interface at Layer 2 with the methods specified in FR-020 through FR-022. Both ModalResonatorBank and WaveguideString will inherit from it.

**Rationale**: The interface must be minimal -- only methods that are semantically identical across resonator types. Type-specific setters (setStiffness for waveguide, setStretch/setScatter for modal) are called by the voice engine when it knows the active type, not through the interface.

**Key Design Decisions**:
- No `noteOn`/`noteOff` per FR-021 -- voice engine owns lifecycle
- No `setParameter(int, float)` per FR-022 -- named setters preserve type safety
- Energy followers (control and perceptual) computed at output tap per FR-024
- `silence()` clears all internal state including energy followers
- `getFeedbackVelocity()` returns 0.0f default for Phase 3

**ModalResonatorBank Adaptation**: The existing class needs adapter methods:
- `setFrequency(float f0)` -- currently implicit in `setModes()`; needs a stored F0 for loop gain queries
- `setDecay(float t60)` -- maps to the decay parameter in `computeModeCoefficients()`
- `setBrightness(float brightness)` -- maps to the brightness parameter
- `getControlEnergy()` / `getPerceptualEnergy()` -- new, computed from output
- `silence()` -- maps to `reset()`

---

## R-008: Scattering Junction Architecture

**Question**: How should the two-segment delay architecture with scattering junction be designed for Phase 4 compatibility?

**Decision**: Define `ScatteringJunction` as a simple abstract interface with `scatter(float vLeft, float vRight, float excitation) -> std::pair<float, float>`. `PluckJunction` passes waves through transparently (identity scatter + additive excitation).

**Rationale**: In Phase 3, the junction is a no-op -- waves pass through the interaction point without reflection. The only purpose is architectural: the string is divided into two segments at the pick/bow position, and Phase 4's bow model will replace `PluckJunction` with `BowJunction` implementing nonlinear friction.

**Implementation**: Since the junction is transparent in Phase 3, the primary architectural benefit is having the two delay segments (`nutSide_` and `bridgeSide_`) with lengths `beta*N` and `(1-beta)*N`. The `PluckJunction` simply adds excitation to the passing wave. For efficiency, the junction can be a function pointer or compile-time strategy rather than a virtual call in the inner loop.

---

## R-009: Crossfade Strategy for Resonance Type Switching

**Question**: How should the crossfade between modal and waveguide resonators work during the transition?

**Decision**: Equal-power cosine crossfade over 20-30 ms (use 1024 samples at 44.1 kHz = ~23 ms) with energy-aware gain matching per FR-029 through FR-031.

**Rationale**: Equal-power crossfade (`cos(t*pi/2)` for old, `sin(t*pi/2)` for new) maintains perceived loudness during the transition. Both models run in parallel during the crossfade, receiving the same excitation. The gain match `sqrt(eA/eB)` clamped to 0.25-4.0x prevents extreme corrections.

**Implementation**: The `PhysicalModelMixer` needs significant extension -- currently it's a stateless crossfader between harmonic/residual/physical paths. The resonance type crossfade is a different concept: it's between two resonator outputs (modal vs waveguide) before they enter the PhysicalModelMixer. This logic belongs in the voice engine or a new `ResonanceCrossfader` utility.

---

## R-010: Velocity Wave Convention

**Question**: Why velocity waves instead of displacement, and what are the implications?

**Decision**: Use velocity waves internally per FR-013. Phase 3 cost is zero (the waveguide output is already velocity). Phase 4 bow interaction is defined in terms of velocity.

**Rationale**: The bow-string interaction (Phase 4) requires velocity at the bow point: `v_d = v_bow - (v_in_left + v_in_right)`. Starting with displacement would require differentiation at the bow point and integration at the output, adding unnecessary computation and numerical issues.

**Implementation**: The delay lines store velocity waves. Output is tapped as velocity (sum of left-going and right-going waves at the observation point). If displacement is ever needed (unlikely in Phase 3), it would be obtained by accumulating (integrating) the velocity output.

---

## R-011: Soft Clipper Design

**Question**: What soft clipper design should be used for the in-loop safety limiter?

**Decision**: Per FR-012: `y = (|x| < threshold) ? x : threshold * tanhf(x / threshold)` with threshold = 1.0f.

**Rationale**: This is a standard soft clipper that is transparent below threshold and smoothly limits above. The conditional avoids the cost of tanhf when the signal is below threshold (which is the common case in a well-behaved waveguide). Only needed during fast parameter sweeps that temporarily violate passivity.

**Implementation**: Inline function, approximately 1 comparison per sample + occasional tanhf. Similar to the `softClip()` already used in `ModalResonatorBank`.

---

## R-012: Pitch Accuracy Testing via YIN/Autocorrelation

**Question**: How should automated pitch accuracy tests be implemented?

**Decision**: Implement a simple autocorrelation-based F0 estimator in the test harness. Render 4096+ samples of waveguide output, compute autocorrelation, find the peak corresponding to the fundamental period, convert to frequency, and compare against target within 1 cent.

**Rationale**: Full YIN is more robust but more complex to implement in a test. For a clean waveguide signal (strong fundamental, known characteristics), simple autocorrelation peak detection is sufficient. The test renders enough samples for the signal to settle (skip initial transient), then analyses a window of steady-state output.

**Implementation**:
1. Render N samples (e.g., 8192) through WaveguideString at target frequency
2. Skip initial transient (first 2048 samples)
3. Apply autocorrelation on remaining samples
4. Find lag of first peak after zero crossing
5. f0_detected = sampleRate / lag
6. Assert: `abs(1200 * log2(f0_detected / f0_target)) < 1.0` (< 1 cent)

---

## R-013: Existing Component Reuse Analysis

**Question**: Which existing codebase components can be directly reused vs need adaptation?

**Decision Summary**:

| Component | Reuse Strategy |
|-----------|---------------|
| `DelayLine` | Direct reuse -- two instances for nut/bridge segments |
| `DCBlocker` | Direct reuse -- configure with 3.5 Hz cutoff |
| `Biquad` | Direct reuse -- 4 instances configured as allpass for dispersion |
| `OnePoleSmoother` | Direct reuse -- for parameter smoothing |
| `XorShift32` | Direct reuse -- for noise burst excitation |
| `BiquadCascade<4>` | Potential reuse -- but may need individual stage control beyond what `setButterworth` provides. Using 4 individual `Biquad` instances gives more control. |
| `ModalResonatorBank` | Needs adaptation -- add IResonator interface methods + energy followers |
| `PhysicalModelMixer` | Needs extension -- resonance type switching + crossfade |
| `WaveguideResonator` | Reference only -- must NOT be modified per spec |
| `KarplusStrong` | Reference only -- pick position and excitation logic patterns |

---

## R-014: Two-Segment Delay Transfer on Position Change

**Question**: How should samples transfer between delay segments when pick position changes?

**Decision**: For Phase 3, pick position is frozen at note onset (FR-015, FR-019). Therefore, no real-time sample transfer is needed. The delay segment lengths are computed once at note onset and remain fixed for the note's lifetime.

**Rationale**: Real-time position changes would require moving samples between the two delay buffers to maintain wave continuity. This is complex and error-prone. Since pick position is frozen at note onset (matching real instruments), we avoid this entirely in Phase 3.

**Phase 4 Note**: When bow position becomes a continuous parameter, sample transfer will be needed. The two-segment architecture supports this by having the junction point as a conceptual boundary between two delay lines.
