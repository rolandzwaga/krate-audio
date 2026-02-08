# Research: Dattorro Plate Reverb

**Feature Branch**: `040-reverb` | **Date**: 2026-02-08

## R1: Header-Only vs .cpp Split

**Decision**: Header-only implementation in `dsp/include/krate/dsp/effects/reverb.h`

**Rationale**: All existing Layer 4 effects in the codebase are header-only (digital_delay.h, tape_delay.h, bbd_delay.h, shimmer_delay.h, etc.). The reverb has no out-of-line implementation needs -- all processing is inline template-free code. Following the established pattern minimizes build system changes and keeps the API consistent.

**Alternatives considered**:
- .h + .cpp split: Would break the established convention where all effects are header-only. No benefit since there are no heavy compile-time costs (no templates, no large constexpr tables).

## R2: Reusing SchroederAllpass vs Custom Allpass

**Decision**: Reuse `SchroederAllpass` from `comb_filter.h` for the **4 input diffusion stages only**. For the **4 tank allpass stages** (2x decay diffusion 1, 2x decay diffusion 2), use **standalone `DelayLine` instances with inline allpass math**.

**Rationale**:
- SchroederAllpass uses `readLinear()` internally and provides exactly the Schroeder allpass difference equation needed for input diffusion stages.
- **Critical constraint**: FR-009 output taps must read from INSIDE the tank allpass delay lines at specific sample positions (e.g., tap Tank B decay diffusion 2 at position 1913). SchroederAllpass's internal `delay_` member is **private with no public accessor**, making it impossible to tap into the allpass delay line from outside.
- Therefore, the 4 tank allpass stages MUST use standalone `DelayLine` instances so that the output tap computation (FR-009) can call `delayLine.read(tapPosition)` directly.
- The allpass math is trivial (3 lines per stage): `delayed = delay.readLinear(delaySamples); output = -coeff * input + delayed; delay.write(input + coeff * output);`
- For modulated tank DD1 allpasses, the delay is LFO-modulated per-sample using `readLinear()` for linear interpolation per FR-019.

**Alternatives considered**:
- AllpassStage from diffusion_network.h: Uses allpass interpolation (`readAllpass()`), which FR-019 explicitly forbids for modulated feedback paths. NOT suitable for tank allpasses.
- SchroederAllpass for all stages: Would work functionally, but the private `delay_` member prevents output tap access required by FR-009. NOT suitable for tank allpasses.

## R3: LFO Implementation for Tank Modulation

**Decision**: Use a lightweight internal sine oscillator (manual phase accumulation + `std::sin()`) instead of the full `LFO` class.

**Rationale**:
- The LFO class (`lfo.h`) is a full-featured wavetable-based oscillator with 6 waveforms, tempo sync, crossfading, retrigger, S&H/smooth random state. It allocates 4 wavetables of 2048 floats each on `prepare()` (32KB of wavetable memory).
- The reverb only needs a single sine wave at a low rate (0-2 Hz) with quadrature output. This is trivially implemented with a phase accumulator and `std::sin()`.
- The DiffusionNetwork (Layer 2) already uses this exact pattern: `lfoPhase_` + `lfoPhaseIncrement_` + `std::sin(lfoPhase_ + offset)`. This is an established pattern in the codebase.
- Memory savings: Avoids 32KB of wavetable allocation per reverb instance.

**Alternatives considered**:
- LFO class: Overkill for a single sine LFO. Wastes memory. Non-copyable (uses `std::vector` internally).
- Gordon-Smith phasor: Could replace `std::sin()` for efficiency, but `std::sin()` is called only once per sample (not per-particle), so the cost is negligible at < 2 Hz rates.

## R4: Parameter Smoothing Strategy

**Decision**: Use `OnePoleSmoother` for all continuously changing parameters (decay, damping coefficient, mix, width, inputGain for freeze). Use a smoothing time of 10ms (matches `kDiffusionSmoothingMs` in diffusion_network.h).

**Rationale**:
- OnePoleSmoother is the standard parameter smoother used throughout the codebase.
- 10ms smoothing time provides click-free transitions without perceptible lag.
- Parameters that need smoothing: decay coefficient, damping LP coefficient, mix, width, input gain (for freeze on/off transition).
- Boolean freeze toggle can be smoothed by targeting input gain to 0.0 (freeze on) or 1.0 (freeze off), and decay to 1.0 (freeze on) or the roomSize-derived value (freeze off).

**Alternatives considered**:
- LinearRamp: Better for delay time changes (predictable duration), but not needed here since no delay time parameters change during processing.
- No smoothing: Would cause clicks on parameter changes (FR-014 violation).

## R5: OnePoleLP Usage for Bandwidth and Damping Filters

**Decision**: Use `OnePoleLP` from `one_pole.h` for both the input bandwidth filter and the two tank damping filters.

**Rationale**:
- OnePoleLP implements exactly the one-pole lowpass formula needed: `y[n] = (1 - a) * x[n] + a * y[n-1]` where `a = exp(-2pi * fc / fs)`.
- For the bandwidth filter (FR-012): The coefficient 0.9995 corresponds to a cutoff frequency. We can set the cutoff using `setCutoff()`, or alternatively set the coefficient directly. Looking at OnePoleLP's implementation, it calculates `coefficient_ = exp(-kTwoPi * cutoffHz_ / sampleRate_)`. To get coefficient = 0.9995, we solve: `cutoffHz = -ln(0.9995) * sampleRate / (2*pi)`. At 44100 Hz: `cutoffHz = 0.0005 * 44100 / 6.2832 = 3.51 Hz`. This is a very gentle lowpass.
- **Important**: The bandwidth filter in the Dattorro paper uses the coefficient directly (0.9995) as the feedback coefficient in `y[n] = (1-a)*x[n] + a*y[n-1]`. This maps exactly to OnePoleLP's `coefficient_` member. However, OnePoleLP's `setCutoff()` derives the coefficient from a frequency, not directly. For the bandwidth filter, we need to either (a) calculate the equivalent cutoff frequency and use `setCutoff()`, or (b) bypass `setCutoff()` and just use a raw one-pole formula inline.
- **Decision update**: For the bandwidth filter, compute the equivalent cutoff Hz from the 0.9995 coefficient at the operating sample rate and use `setCutoff()`. The formula is: `cutoffHz = -ln(coeff) * sampleRate / (2*pi)`. This is computed once in `prepare()`.
- For the damping filters: The spec provides a direct mapping from the damping parameter to cutoff Hz (FR-013), which maps perfectly to `setCutoff()`.

**Alternatives considered**:
- Raw inline one-pole: Simpler for the bandwidth filter, but less consistent with the codebase style.

## R6: DCBlocker Usage

**Decision**: Use `DCBlocker` from `dc_blocker.h` for the two tank DC blockers.

**Rationale**:
- DCBlocker implements exactly the first-order DC blocking filter needed for the tank feedback paths (FR-029).
- Two instances needed (one per tank loop).
- Cutoff of 10 Hz (default) is within the spec range of 5-20 Hz.
- The DCBlocker1 (first-order) is the right choice per the dc_blocker.h selection guide: "Feedback loops -> DCBlocker" (not DCBlocker2).

**Alternatives considered**:
- DCBlocker2 (2nd-order Bessel): More CPU, faster settling, but the reverb tank doesn't need fast settling -- it operates continuously.

## R7: Pre-Delay Implementation

**Decision**: Use a single `DelayLine` for pre-delay (0-100ms).

**Rationale**:
- DelayLine already provides the exact functionality needed.
- Maximum pre-delay of 100ms requires `ceil(0.1 * 192000) = 19200` samples at 192kHz.
- Pre-delay uses integer reads (`read()`) since the delay value is constant (not modulated).

**Alternatives considered**:
- No pre-delay line (hardcode 0): Would lose the pre-delay feature (FR-011).

## R8: Output Scaling / Gain

**Decision**: The output tap sum needs to be scaled by a constant factor to prevent excessive amplitude. The Dattorro paper uses a gain of 0.6 applied to each output tap. This will be implemented as a compile-time constant.

**Rationale**:
- The raw output tap sum combines 7 taps (some added, some subtracted), which can produce values significantly above unity.
- A gain factor of 0.6 is the standard Dattorro output scaling.
- This factor is baked into the algorithm and does not need to be user-adjustable.

## R9: Denormal Flushing Strategy

**Decision**: Apply `detail::flushDenormal()` on the two state variables that persist across samples: the cross-coupled tank outputs (tankAOut_ and tankBOut_) which feed back into the opposite tank. Also flush inside the OnePoleLP and DCBlocker (which already flush internally).

**Rationale**:
- FR-028 requires denormal flushing in feedback paths.
- The tank feedback values are the primary feedback state. Everything else is processed by primitives that already flush denormals internally (OnePoleLP, DCBlocker, SchroederAllpass).
- The SchroederAllpass (comb_filter.h) already calls `detail::flushDenormal()` before writing to its delay line.

## R10: Memory Budget Analysis

**Decision**: Total memory for all delay lines at 192kHz:

| Delay Line | Ref Samples | @192kHz (scaled) | Buffer (power-of-2) |
|-----------|-------------|-------------------|---------------------|
| Input Diffusion 1 (142) | 142 | 917 | 1024 |
| Input Diffusion 2 (107) | 107 | 691 | 1024 |
| Input Diffusion 3 (379) | 379 | 2448 | 4096 |
| Input Diffusion 4 (277) | 277 | 1789 | 2048 |
| Tank A: DD1 allpass (672) | 672 | 4341 | 8192 |
| Tank A: Pre-damp delay (4453) | 4453 | 28762 | 32768 |
| Tank A: DD2 allpass (1800) | 1800 | 11624 | 16384 |
| Tank A: Post-damp delay (3720) | 3720 | 24028 | 32768 |
| Tank B: DD1 allpass (908) | 908 | 5865 | 8192 |
| Tank B: Pre-damp delay (4217) | 4217 | 27237 | 32768 |
| Tank B: DD2 allpass (2656) | 2656 | 17152 | 32768 |
| Tank B: Post-damp delay (3163) | 3163 | 20430 | 32768 |
| Pre-delay (100ms) | - | 19200 | 32768 |

Total buffer floats: ~237,568 floats = ~950 KB at 192kHz. Well within the 10s @ 192kHz maximum (1.92M samples) budget.

**Rationale**: Power-of-2 sizes are required by DelayLine for bitwise wrapping. At 44.1kHz, the total drops to ~60KB -- very reasonable.

## R11: `std::isfinite()` Usage for NaN/Infinity Detection

**Decision**: Use `std::isfinite()` for input validation as specified in FR-027. However, due to the VST3 SDK's `-ffast-math` flag, the reverb header must be compiled with `-fno-fast-math` on Clang/GCC. Alternatively, use the project's `detail::isNaN()` and `detail::isInf()` bit-manipulation functions which work regardless of `-ffast-math`.

**Rationale**: The constitution Cross-Platform Compatibility section states: "-ffast-math breaks std::isnan(). Use bit manipulation." The existing primitives (OnePoleLP, SchroederAllpass, etc.) all use `detail::isNaN()` and `detail::isInf()`. Following this pattern ensures correctness across all compilers.

**Updated Decision**: Use `detail::isNaN(x) || detail::isInf(x)` pattern consistent with all other DSP code. Do NOT use `std::isfinite()`.

## R12: Diffusion Parameter Scaling

**Decision**: The diffusion parameter (0.0-1.0) scales the input diffusion coefficients:
- Input Diffusion 1 coefficient = diffusion * 0.75 (FR-002 default)
- Input Diffusion 2 coefficient = diffusion * 0.625 (FR-002 default)

At diffusion=1.0, the coefficients are at their default values (0.75, 0.625).
At diffusion=0.0, the coefficients are 0.0 (allpass becomes pass-through).

**Rationale**: This matches the spec statement "Scales the input diffusion coefficients" (FR-011). The Dattorro paper defines these as separate parameters, but the spec consolidates them into a single diffusion knob.

## R13: Decay Diffusion 2 Tracking

**Decision**: Per the Dattorro paper (Table 1, row "Decay Diffusion 2"), the decay diffusion 2 coefficient tracks the decay: `decayDiffusion2 = decay + 0.15`, clamped to [0.25, 0.50]. The spec (FR-007) states the default is 0.50, and the spec's "Assumptions" section notes it is "fixed at 0.50."

**Rationale**: The spec explicitly states in the Assumptions section: "The decay diffusion 2 coefficient is fixed at 0.50 (per FR-007), following the Dattorro paper default. It is not modulated by the roomSize/decay parameter." This overrides the Dattorro paper's tracking behavior. Implementation will use a fixed coefficient of 0.50.

**Alternatives considered**:
- Tracking decay: Would follow the paper more faithfully but contradicts the spec. Could be added as a future enhancement.
