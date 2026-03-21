# Innexus — Physical Modelling Layer Roadmap

A phased plan for adding physical modelling synthesis to Innexus, building on the existing harmonic analysis/resynthesis architecture. Each phase is independently useful and evaluable — ship one, listen, decide whether to continue.

## Design Principle: Excitation → Resonance

All physical modelling in Innexus follows one pattern:

```
Excitation (what drives the sound)  →  Resonance (what shapes it)
```

The analyzed harmonic data provides **tuning** for resonators. Broadband signals (residual, impact, bow) provide **excitation**. This maps directly onto the existing analysis pipeline — no new analysis is needed.

## Architecture Overview

```
┌─ Per Voice ──────────────────────────────────────────────────────────┐
│                                                                      │
│  ┌──────────────────────────────────────────────────────┐            │
│  │  EXISTING PATH (unchanged)                           │            │
│  │  HarmonicFrame → OscillatorBank → harmonic signal ───┤            │
│  └──────────────────────────────────────────────────────┘            │
│                                                                      │
│  ┌──────────────────────────────────────────────────────┐            │
│  │  EXCITATION STAGE                                    │            │
│  │                                                      │            │
│  │  [Analyzed Residual] ──┐                             │            │
│  │  [Impact Model]     ───┼──→ Excitation Mix ────┐     │            │
│  │  [Bow Model]        ───┘                       │     │            │
│  └────────────────────────────────────────────────┼─────┘            │
│                                                   ▼                  │
│  ┌──────────────────────────────────────────────────────┐            │
│  │  RESONANCE STAGE                                     │            │
│  │                                                      │            │
│  │  HarmonicFrame ──→ tuning data                       │            │
│  │                                                      │            │
│  │  [Modal Resonator]  ───┐                             │            │
│  │  [Waveguide String] ───┼──→ Resonance Mix ───┐      │            │
│  │  [Body Resonance]   ───┘                      │      │            │
│  └───────────────────────────────────────────────┼──────┘            │
│                                                  ▼                   │
│  ┌──────────────────────────────────────────────────────┐            │
│  │  OUTPUT MIX                                          │            │
│  │                                                      │            │
│  │  harmonic signal ──┐                                 │            │
│  │  physical signal ──┼──→ [PhysModelMix] → voice out   │            │
│  │  residual signal ──┘    (dry/wet)                    │            │
│  └──────────────────────────────────────────────────────┘            │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘

┌─ Post-Voice (Global) ───────────────────────────────────────────────┐
│  All voice outputs ──→ [Sympathetic Resonance] ──→ master out       │
└─────────────────────────────────────────────────────────────────────┘
```

### Key Design Decisions

1. **Per-voice instances** (except sympathetic). Each voice gets its own excitation + resonance, tuned to that voice's analyzed partials.

2. **Selector + mix, not serial chain.** Each stage is a parallel selector with crossfade. Only active modules consume CPU.

3. **Physical model mix knob.** 0% = current pure additive behavior. 100% = fully physical. Backwards compatible.

4. **Tuning from analysis.** All resonance models read partial frequencies from the existing `HarmonicFrame`. Change the input sound → physical model adapts automatically.

---

## Phase 1: Modal Resonator Bank

**Goal:** Transform the residual signal from "noise filler" into a physically resonant texture by passing it through a bank of tuned resonators derived from the analyzed harmonic content.

**Sonic character:** Struck/rung metallic or wooden textures. The analyzed partials define *what* resonates; the residual provides broadband excitation. Think: hitting a surface whose resonant modes match the analyzed spectrum.

**Theoretical basis:** Modal synthesis decomposes the vibration of a physical structure as a superposition of its basic modes of vibration (Adrien, 1991). Each mode is defined by a frequency, a damping factor, and an amplitude determined by the excitation/pickup position. The sound is reconstructed by summing these independently decaying sinusoidal modes — implemented efficiently as a parallel bank of second-order resonant filters.

### Scientific Foundation

The approach draws on several established bodies of work:

- **Adrien (1991), "The Missing Link: Modal Synthesis"** — Established that any structural vibration decomposes into a superposition of eigenmodes, each with frequency ω_n, damping d_n, and spatial shape φ_n(x). This is the theoretical foundation for the resonator bank.

- **van den Doel & Pai (1996, 1998)** — First real-time interactive modal synthesis framework. Demonstrated position-dependent excitation gain vectors and 400+ modes in real-time. Their model: each mode = exponentially decaying sinusoid, decay rate proportional to frequency via internal friction parameter.

- **Cook (1996, 1997), PhISM/PhISAM** — Showed that modal synthesis does not need to be purely physics-driven: analysis data from recordings can parameterize the modal bank. This is exactly our use case (analyzed harmonic content → resonator tunings).

- **Smith (CCRMA), "Physical Audio Signal Processing"** — Canonical reference for the parallel second-order section implementation, impulse invariant transform, and constant-peak-gain resonator topology.

- **Aramaki, Kronland-Martinet et al. (2009–2012)** — Demonstrated that perceived material (wood/metal/glass) is primarily determined by the frequency-dependent damping law, with spectral content as secondary. Their perceptual evaluation showed synthesized impacts can be indistinguishable from recordings with proper dynamic filtering.

- **Mutable Instruments Elements/Rings (Gillet)** — Production-proven 64-mode resonator bank on embedded ARM, using SVF topology with lookup-table coefficient computation. Open-source reference design.

### New DSP Components

#### `ModalResonatorBank` — Layer 2 (processors)
Location: `dsp/include/krate/dsp/processors/modal_resonator_bank.h`

```
Input: mono excitation signal (sample-by-sample)
Config: array of {frequency, amplitude, decayRate} per mode (from HarmonicFrame)
Output: mono resonant signal

Internal: Bank of N damped coupled-form resonators (Gordon-Smith topology)
  - One resonator per active partial (up to kMaxPartials = 96)
  - Coefficients via impulse invariant transform
  - SoA memory layout for SIMD processing
  - Sum of all resonator outputs = resonant signal
```

#### Filter Topology: Damped Coupled-Form Resonator

**Decision: Use the coupled-form (Gordon-Smith) resonator, NOT biquad.**

The research strongly favors the coupled-form for modal resonator banks:

| Property | Coupled-Form | DF2T Biquad | Cytomic SVF |
|----------|-------------|-------------|-------------|
| Muls/sample | 4 | 5 | ~5 |
| Adds/sample | 2 | 4 | ~5 |
| Amplitude stable | **Yes (det=R²)** | No guarantee | No guarantee |
| High-Q stable | Excellent | Problematic (coeff sensitivity) | Excellent |
| SIMD friendly | **Very** (uniform ops) | Moderate | Moderate |
| Freq accuracy | ~sin approx | Exact (bilinear) | Exact (tan warp) |
| Max stable freq | ~fs/6 | fs/2 | fs/2 |

The coupled-form's structural amplitude stability (determinant = R² for damped, exactly 1 for undamped) is the decisive advantage. With 96 high-Q resonators ringing simultaneously, accumulated floating-point rounding in biquads can cause slow energy drift. The coupled-form eliminates this by construction.

**Note on frequency limit:** The coupled-form is accurate up to ~fs/6 (~7.35 kHz at 44.1 kHz). For modes above this, we accept the slight frequency approximation — at high frequencies the ear is less sensitive to small pitch errors, and most musically important modes are well below this limit. Modes above 0.49 × fs are culled entirely (Nyquist guard).

**Post-Phase 1 evaluation:** Because our partials come from analysis (users expect identity preservation), frequency warping of upper partials could cause audible formant smearing or brightness changes. After Phase 1 listening tests, evaluate whether upper-partial frequency accuracy is sufficient. However, a hybrid topology (coupled-form below fs/6, SVF above) is **not recommended** — the phase discontinuity at the crossover frequency would likely be more audible and annoying than the slight frequency warping of a 12 kHz partial. Additionally, in modal synthesis, high-frequency inharmonicity is natural behavior of real objects, so the coupled-form's frequency approximation may actually *improve* realism. If upper-partial accuracy proves problematic, the better fix is tighter Nyquist culling (e.g. cull above fs/8 instead of 0.49*fs) rather than mixing filter topologies.

**Per-mode state and coefficients:**

```cpp
// Impulse invariant transform from modal parameters
// Given: f (Hz), decayRate (1/s), amplitude, sampleRate

float epsilon = 2.0f * sinf(kPi * f / sampleRate);  // frequency parameter
float R = expf(-decayRate / sampleRate);               // per-sample decay multiplier

// Per-sample coupled-form update (with excitation input):
float s_new = R * (s + epsilon * c) + amplitude * input;
float c_new = R * (c - epsilon * s_new);  // NOTE: uses updated s_new
output += s_new;  // accumulate across all modes
```

The two state variables (s, c) represent quadrature components of a damped sinusoidal oscillation. The key property: `s²+c²` contracts by exactly R² each sample, guaranteeing exponential decay at the configured rate regardless of floating-point rounding.

#### Frequency-Dependent Damping Model

**Decision: Use the Chaigne-Lambourg quadratic damping law.**

The research (Aramaki et al., Sterling & Lin 2016, Nathan Ho) converges on this model:

```
decayRate_k = b1 + b3 * f_k²
```

Where:
- `b1` (Hz) = baseline decay rate (frequency-independent). Controls overall sustain.
- `b3` (seconds) = high-frequency damping coefficient. Controls material character.
- `f_k` = frequency of mode k in Hz

**Physical basis:**
- `b1` captures air viscosity and radiation losses (dominant at low frequencies)
- `b3 * f_k²` captures thermoelastic and viscoelastic internal losses (dominant at high frequencies)

**Mapping to user parameters:**

| User Parameter | Damping Law Mapping | Perceptual Effect |
|----------------|-------------------|-------------------|
| Decay (0.01–5.0s, log-mapped) | `b1 = 1.0 / decayTime` | Overall ring time. Log mapping ensures perceptually uniform control: equal knob travel = equal perceived change in sustain. |
| Brightness (0–1) | `b3 = (1.0 - brightness) * kMaxB3` | 0=dark (wood, steep HF roll-off), 1=bright (metal, flat damping) |

**Why only two parameters:** An earlier design included a third "Damping" parameter that scaled both b1 and b3 simultaneously. This was removed because it overlaps conceptually with Decay (both affect overall ring time) and creates confusing interactions where three controls fight each other. Two parameters — one for *time*, one for *material character* — give clean, orthogonal control.

**Material character regions in parameter space:**
- **Metal** (long decay, high brightness): Low b1, low b3 → all modes ring equally → bright, shimmery
- **Wood** (moderate decay, low brightness): Moderate b1, high b3 → low modes sustain, high modes die fast → warm "thunk"
- **Glass** (short decay, high brightness): High b1, low b3 → bright ring with quick fundamental decay

The simpler alternative from Mutable Instruments Elements (`q *= q_loss` per mode, where q_loss < 1) is a reasonable approximation that avoids the `b3 * f²` computation. We may prototype with this simpler model first, then upgrade to the full quadratic law if the timbral range is insufficient.

#### Gain Normalization: Constant-Peak-Gain Resonator

Without normalization, resonator output varies by up to 80 dB across the tuning range at high Q (Smith, CCRMA). Two proven approaches:

**Option A — Zeros at z=+1 and z=-1** (used by Faust physmodels.lib, STK):
```
b0 = (1 - R²) / 2
b1 = 0
b2 = -(1 - R²) / 2
```
Cost: one extra subtraction per sample. Guarantees unity peak gain at any frequency.

**Option B — Explicit per-mode gain compensation** (used by Elements):
Apply `amplitude * (1 - R²)` as the input scaling per mode, where `(1-R²)` compensates for the resonator's gain at the pole frequency.

**Decision:** Use Option A (zeros at ±1). The cost is one extra subtraction per sample per mode — at 96 modes this is 96 subtractions, completely negligible. Option A guarantees unity peak gain at any frequency and behaves better under modulation (time-varying excitation, changing coefficients), which matters in our system where many overlapping modes are driven by irregular excitation signals. Option B's `(1-R²)` approximation assumes steady-state sinusoidal behavior that doesn't hold here.

**Implementation:** Apply the `(1 - R²) / 2` scaling as `b0`/`b2` in the coupled-form input injection rather than as a simple amplitude multiplier. This requires minimal restructuring — the excitation input term becomes `b0 * input` with the zero-pair topology baked in.

#### Output Safety Limiter

Option A normalizes each mode to unity peak gain, but the **sum** of 96 unity-gain resonators can still reach +20 dB or more when the excitation has broadband energy hitting many modes simultaneously. Without protection, this will clip the output.

**Decision: Soft clipper at ModalResonatorBank output.**

```cpp
// After summing all mode outputs, apply safety limiting
float output = reduceSum(modeOutputs);
output = softClip(output, kSafetyThreshold);  // e.g. threshold at -3 dBFS ≈ 0.707

// Simple soft clipper: tanh-based, no state, no latency
inline float softClip(float x, float threshold) {
    if (fabsf(x) < threshold) return x;  // pass-through below threshold
    return threshold * tanhf(x / threshold);  // soft saturation above
}
```

This is a last-resort safety measure, not a creative effect. The threshold should be set high enough that well-balanced presets never hit it (the per-mode gain normalization handles that), but it prevents catastrophic clipping when extreme parameter combinations or broadband excitation drive many modes simultaneously. Cost: one comparison + occasional `tanhf` per sample — negligible.

#### SIMD Implementation Strategy

The coupled-form is highly SIMD-friendly because all modes are independent and all perform identical operations. We already have Google Highway in KrateDSP.

**Memory layout — Structure of Arrays (SoA):**

```cpp
struct ModalResonatorBank {
    static constexpr int kMaxModes = 96;  // match HarmonicOscillatorBank

    // SoA layout, aligned for AVX (32-byte)
    alignas(32) float sinStates_[kMaxModes];   // quadrature state s
    alignas(32) float cosStates_[kMaxModes];   // quadrature state c
    alignas(32) float epsilons_[kMaxModes];    // 2*sin(π*f/fs) per mode
    alignas(32) float decays_[kMaxModes];      // exp(-decayRate/fs) per mode
    alignas(32) float amplitudes_[kMaxModes];  // per-mode gain (from HarmonicFrame)
    int numActiveModes_ = 0;

    // ... methods
};
```

**Processing kernel (Highway pseudo-code):**

```cpp
// Per sample: process all modes in SIMD batches
float processSample(float input) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);  // 4 (SSE), 8 (AVX), 16 (AVX-512)

    auto sum = hn::Zero(d);
    auto in_v = hn::Set(d, input);

    for (size_t m = 0; m < numActiveModes_; m += N) {
        auto s   = hn::Load(d, &sinStates_[m]);
        auto c   = hn::Load(d, &cosStates_[m]);
        auto eps = hn::Load(d, &epsilons_[m]);
        auto r   = hn::Load(d, &decays_[m]);
        auto amp = hn::Load(d, &amplitudes_[m]);

        // Coupled-form update: s_new = R * (s + eps * c) + amp * input
        auto s_new = hn::Mul(r, hn::MulAdd(eps, c, s));
        s_new = hn::MulAdd(amp, in_v, s_new);

        // c_new = R * (c - eps * s_new)
        auto c_new = hn::Mul(r, hn::NegMulAdd(eps, s_new, c));

        hn::Store(s_new, d, &sinStates_[m]);
        hn::Store(c_new, d, &cosStates_[m]);

        sum = hn::Add(sum, s_new);  // accumulate output
    }
    return hn::ReduceSum(d, sum);
}
```

**Performance estimate** (64 modes, 44.1 kHz, single voice):
- Scalar: 64 × (4 mul + 2 add) = 384 ops/sample → ~17M ops/sec → negligible on modern CPU
- AVX (8-wide): 8 iterations/sample → ~2M SIMD ops/sec → well under 1% single core
- 8 voices × 96 modes: still under 5% single core

#### Coefficient Update Strategy

Coefficients only need recomputation when:
1. **Note-on**: New HarmonicFrame loaded → set all mode frequencies, amplitudes, decays. Clear filter states.
2. **Frame advance** (sample playback): Frame changes per hop → recompute coefficients. Do NOT clear states (modes should ring through frame transitions).
3. **User parameter change** (damping, brightness): Recalculate decay rates using new b1/b3 values.

**For frame transitions:** Recompute target `epsilon` and `R` per mode. While the coupled-form is structurally more tolerant of coefficient changes than biquads (no risk of interpolated coefficients pushing poles outside the unit circle), instant changes to epsilon/R while state is non-zero can still cause phase discontinuities and subtle clicks — especially when harmonic frames change rapidly or partial tracking is unstable.

**Coefficient smoothing:** Apply one-pole smoothing on `epsilon` and `R` values with a ~2ms time constant:
```cpp
// Per-sample coefficient interpolation (one multiply + one add per coefficient per mode)
epsilon_current += kSmoothCoeff * (epsilon_target - epsilon_current);
R_current += kSmoothCoeff * (R_target - R_current);
// where kSmoothCoeff = 1.0 - exp(-1.0 / (smoothTimeMs * 0.001 * sampleRate))
```
This adds 4 ops per mode per sample (2 coefficients × (1 sub + 1 mul)) — negligible cost for click-free transitions. The smoothing targets are updated on frame advance or parameter change; the per-sample interpolation ensures no discontinuities.

**Clock-divided updates** (optimization from Elements): For modes above index 24, update coefficient *targets* only every other block. High-frequency modes are less perceptually sensitive to slight parameter lag. The per-sample smoothing still runs every sample for all modes.

#### Denormal Protection

With 96 resonators decaying independently, denormals are virtually guaranteed without protection.

**Strategy: FTZ/DAZ (primary) + activity tracking (optimization)**

FTZ/DAZ is already enabled in our audio thread setup per CLAUDE.md. Additionally:

```cpp
// Per-block: check which modes are still ringing
for (int m = 0; m < numActiveModes_; ++m) {
    float energy = sinStates_[m] * sinStates_[m] + cosStates_[m] * cosStates_[m];
    if (energy < kSilenceThreshold) {  // ~1e-12 (-120 dB)
        sinStates_[m] = 0.0f;
        cosStates_[m] = 0.0f;
    }
}
```

This provides both denormal safety and a CPU optimization: silent modes contribute zero to the sum and could be skipped in a future refinement (though the SIMD kernel processes all lanes uniformly, so the main benefit is preventing slow denormal math on non-SIMD fallback paths).

#### Inharmonic Mode Dispersion

**Decision: Add two parameters to warp partial frequencies away from strict harmonicity.**

Without this, the modal resonator is locked to perfectly harmonic structures — everything sounds like "harmonic resonant objects." Real physical structures have inharmonic modes (bells, plates, piano strings), and even a tiny deviation from harmonicity produces a massive perceptual upgrade. The key insight: deviations must be **coherent and deterministic** (a smooth function of partial index), never random noise — the ear tracks inter-partial relationships, and random detuning just produces chorus/mush.

**Two orthogonal warping axes:**

```
f_k' = f_k * sqrt(1 + B * k²) * (1 + C * sin(k * D))
```

**Axis 1 — Stretch (string-like, smooth):**
```
B = stretch² * 0.001
```
This is the physically correct stiff-string inharmonicity from the Euler-Bernoulli beam model. The `sqrt` form (vs. linear `1 + B*k²`) behaves correctly at larger values — upper partials grow gently rather than exploding. Cost: one `sqrtf` per mode per coefficient update (not per sample).

- `B = 0`: Perfectly harmonic (current behavior)
- `small B`: Subtle "tension," "realness" — piano-like
- `larger B`: Bell-like, metallic — upper partials spread significantly

**Axis 2 — Scatter (object-like, irregular):**
```
C = scatter * 0.02
D = kScatterPhase  // π × (√5 - 1) / 2 ≈ 1.942 (golden ratio × π)
```
The sinusoidal term adds structured irregularity that breaks harmonic rigidity while maintaining coherence. This simulates the irregular mode spacing of real objects (plates, bells, bars) where modes cluster and gap unpredictably.

- `C = 0`: No irregularity (pure stretch only)
- `small C`: Subtle beating between modes — "alive" quality
- `larger C`: Distinctly metallic/plate-like mode rearrangement

**Why two parameters, not one:** Stretch gives smooth, monotonic partial spreading (strings, piano). Scatter gives irregular, clustered deviations (plates, bells). These are perceptually distinct timbral axes — combining them into one knob forces one without the other. Two knobs, orthogonal control.

**Applied to resonator only** (Option A): The oscillator bank stays perfectly harmonic (preserving pitch stability and analyzed identity). The resonator gets warped frequencies. This mirrors real instruments: harmonic source → inharmonic body.

**Interaction with damping model:** The warped `f_k'` feeds directly into `decayRate_k = b1 + b3 * f_k'²`. This automatically produces more varied, realistic decay profiles — modes that are stretched higher decay faster. This is physically correct and should not be "fixed."

**Implementation (during coefficient computation, not per-sample):**
```cpp
float fk = partialFrequency;  // from HarmonicFrame

// Apply inharmonicity warping
float stretch = sqrtf(1.0f + B * k * k);
float warp = 1.0f + C * sinf(k * D);
fk *= stretch * warp;

// Then compute epsilon and R as usual
float epsilon = 2.0f * sinf(kPi * fk / sampleRate);
float R = expf(-decayRate / sampleRate);  // decayRate uses warped fk
```

Cost: one `sqrtf` + one `sinf` per mode per coefficient update. At 96 modes updated once per hop (~512 samples), this is negligible.

#### Mode Culling

Before loading modes from a HarmonicFrame (after applying stretch):
1. **Nyquist guard**: Skip modes where `f_k' >= 0.49 * sampleRate`
2. **Amplitude threshold**: Skip modes where the analyzed partial amplitude < -80 dB
3. **Partial count parameter**: Respect existing `kPartialCountId` (48/64/80/96) to limit mode count

#### Excitation-Resonance Interaction

**Why the analyzed residual is an ideal excitation source:**

The source-filter paradigm (Smith, CCRMA; Aramaki et al.) factors sound into excitation × resonance. Our existing `ResidualSynthesizer` already generates a broadband signal shaped by the spectral envelope of the non-sinusoidal components. This carries:
- **Temporal character**: Transient attacks, noise bursts, friction textures from the original sound
- **Spectral shape**: Band-limited energy distribution matching the original

Feeding this through the modal resonator bank means: the *interaction type* (how it was excited) comes from the original sound's residual, while the *material/body character* (what resonates) comes from the resonator's damping law. This is the same paradigm used in Mutable Instruments Elements (external audio → resonator bank → output) and in commuted synthesis (Smith, CCRMA).

**Perceptual prediction:** The result should sound like the original sound's attack/noise character applied to a physical object whose resonant modes match the analyzed spectrum. Example: a vocal residual through a metallic-damped resonator → "singing bowl shaped by the voice's formants."

#### Excitation Conditioning

**Risk:** The raw residual is not guaranteed to behave like a physical excitation signal. Real excitations are typically sparse (impulses, impacts), high crest factor, and temporally structured. The residual can be continuous noise, smeared transients (from analysis windowing), or phasey/decorrelated — producing "filtered noise with ringing" rather than "struck object."

**Mitigation: Transient emphasis (applied before resonator input).**

A simple envelope-derivative transient emphasis boosts attack energy relative to sustained noise:

```cpp
// Lightweight transient emphasis on residual before feeding resonator
float envelope = envFollower.process(fabsf(residualSample));  // one-pole, ~5ms attack
float envelopeDerivative = envelope - prevEnvelope;           // positive = rising energy
prevEnvelope = envelope;

// Emphasize transients: boost when envelope is rising, pass through otherwise
float emphasis = 1.0f + kTransientGain * fmaxf(0.0f, envelopeDerivative);
float conditionedExcitation = residualSample * emphasis;
```

This is hardcoded (not a user parameter) with moderate emphasis (`kTransientGain ≈ 4.0`). It costs 5 ops/sample regardless of mode count — negligible. The effect: attacks hit the resonator harder, sustained noise passes through at normal level, making the resonance sound more "struck" and less "filtered."

Phase 2's `ImpactExciter` provides the full solution (purpose-built excitation signals), but this conditioning ensures Phase 1 sounds physically motivated even with just the residual path.

#### `PhysicalModelMixer` — Plugin-local
Location: `plugins/innexus/src/dsp/physical_model_mixer.h`

```
Inputs: harmonic signal (from OscillatorBank), physical signal, residual signal
Params: physModelMix (0-1)
Output: blended voice signal

Logic:
  dry = harmonicSignal + residualSignal  (current behavior)
  wet = harmonicSignal + physicalSignal  (physical replaces residual path)
  output = dry * (1 - mix) + wet * mix
```

### New Parameters

| ID | Name | Range | Default | Description |
|----|------|-------|---------|-------------|
| `kPhysModelMixId` | Physical Model Mix | 0.0–1.0 | 0.0 | Dry (additive) ↔ Wet (physical) |
| `kResonanceDecayId` | Decay | 0.01–5.0s (log-mapped) | 0.5s | Base ring-out time (maps to b1 in damping law). Log mapping: equal knob travel = equal perceived change. |
| `kResonanceBrightnessId` | Brightness | 0.0–1.0 | 0.5 | HF damping slope: 0=dark (wood, steep b3), 1=bright (metal, flat b3) |
| `kResonanceStretchId` | Stretch | 0.0–1.0 | 0.0 | Smooth inharmonic stretch (stiff-string model): 0=harmonic, higher=more bell-like partial spreading |
| `kResonanceScatterId` | Scatter | 0.0–1.0 | 0.0 | Irregular mode displacement: 0=smooth, higher=plate/bell-like mode clustering |

**Removed parameters:**
- `kResonanceQId` (Q control): In the coupled-form topology, resonance sharpness is a natural consequence of decay rate — `B = 1/(π·T60)` per Smith. A separate Q would fight the damping model.
- `kResonanceDampingId` (Damping): Scaled both b1 and b3, overlapping conceptually with Decay and creating confusing three-way interactions. Two orthogonal parameters (Decay for *time*, Brightness for *material character*) give cleaner, more intuitive control.

### Voice Integration

In the per-voice render loop (`processor.cpp`), after existing residual synthesis:

```cpp
// Existing: residual excitation signal
float residualSample = residualSynth.process();

// NEW: feed residual through modal resonator (tuned to current frame's partials)
float physicalSample = modalResonator.process(residualSample);

// NEW: mix
float resMixed = physModelMixer.process(residualSample, physicalSample, physModelMix);
```

On **note-on**: Initialize modal resonator with the current HarmonicFrame's partial frequencies and amplitudes. Clear all filter states. Compute initial decay rates from the damping model.

On **frame advance** (sample mode): Update mode frequencies/amplitudes from new frame. Do NOT clear states — modes ring through transitions for natural sound.

On **note-off**: Let the resonator ring naturally (exponential decay). The existing ADSR envelope handles voice-level gain reduction; the modal resonator's own decay adds physical character to the release.

### Test Strategy

1. **Unit test: impulse response** — Feed a single impulse into a bank configured with known frequencies (e.g., 220, 440, 660, 880 Hz). FFT the output and verify spectral peaks at those frequencies within ±1 Hz.
2. **Unit test: exponential decay** — Configure a single mode at 440 Hz with known decay rate. Measure peak amplitude at t=0, t=T60/2, t=T60. Verify exponential envelope within 0.5 dB.
3. **Unit test: frequency-dependent damping** — Configure 8 modes spanning 100–8000 Hz with brightness=0 (maximum HF damping). Verify that high-frequency modes decay faster by measuring per-mode T60.
4. **Unit test: amplitude stability** — Configure a mode with very low damping (R ≈ 0.99999). Run 10 seconds of silence (no excitation after initial impulse). Verify output amplitude tracks the expected exponential decay without drift (coupled-form structural guarantee).
5. **Unit test: SIMD consistency** — Compare scalar and SIMD processing paths for bit-exact output (or within 1 ULP for FMA differences).
6. **Unit test: Nyquist guard** — Configure modes above fs/2. Verify they are culled and produce no output.
7. **Unit test: coefficient update** — Change mode frequencies mid-stream (simulating frame advance). Verify no clicks or instability.
8. **Integration test: PhysicalModelMixer** — Verify mix=0 produces identical output to current behavior (bit-exact regression).
9. **Listening test: material presets** — Load a vocal sample, configure resonator with metal-like damping (bright, long decay) vs wood-like damping (dark, short high-freq decay). Verify audibly distinct and physically motivated material characters.

### Success Criteria

- [ ] Mix at 0% is bit-exact with current output (no regression)
- [ ] Modal resonator CPU cost < 5% single core at 96 modes × 8 voices, 44.1 kHz
- [ ] No denormals or numerical instability after 30s of sustained resonance
- [ ] Amplitude stability test passes: no drift after 10s with R ≈ 0.99999
- [ ] Brightness sweep produces audibly distinct material characters (metal → wood → muffled)
- [ ] Decay parameter directly controls perceived ring time
- [ ] Sounds *physically motivated* — not just "filtered noise"

### References

| Citation | Relevance |
|----------|-----------|
| Adrien, J.-M. (1991). "The missing link: Modal synthesis." In *Representations of Musical Signals*, MIT Press. | Theoretical foundation: modal decomposition of vibrating structures |
| van den Doel, K. & Pai, D.K. (1998). "The sounds of physical shapes." *Presence*, 7(4), 382-395. | Real-time modal synthesis framework, position-dependent excitation |
| Cook, P.R. (1997). "Physically Informed Sonic Modeling (PhISM)." *CMJ*, 21(3), 38-49. | Analysis-driven parameterization of modal banks |
| Smith, J.O. (2010). *Physical Audio Signal Processing*. CCRMA Stanford. | Parallel second-order sections, impulse invariant transform, constant-peak-gain resonator |
| Aramaki, M. & Kronland-Martinet, R. (2006). "Analysis-Synthesis of Impact Sounds by Real-Time Dynamic Filtering." *IEEE Trans. Audio Speech Lang. Processing*. | Material-dependent damping laws, perceptual evaluation |
| Sterling, A. & Lin, M. (2016). "Interactive Modal Sound Synthesis Using Generalized Proportional Damping." *I3D*. | GPD model for arbitrary frequency-dependent damping |
| Chaigne, A. & Lambourg, C. (2001). "Time-domain simulation of damped impacted plates." *JASA*. | Quadratic damping law: R_k = b1 + b3·f_k² |
| Gillet, E. Mutable Instruments Elements/Rings firmware. MIT License. | Production reference: 64-mode SVF bank, clock-divided updates, LUT coefficients |
| Ho, N. "Exploring Modal Synthesis." Blog post. | Practical implementation guide, damping models, frequency models |
| Faust physmodels.lib. GRAME. | Constant-peak-gain biquad implementation, mesh2faust pipeline |
| Smith, J.O. "Digital Sinusoid Generators." CCRMA. | Coupled-form/Gordon-Smith oscillator: amplitude stability proof |

---

## Phase 2: Impact Exciter

**Goal:** Replace the analyzed residual as excitation source with a physical impact model, giving mallet/pluck attacks to any analyzed timbre.

**Sonic character:** The attack of a marimba, kalimba, or plucked string, but the resonance rings with the analyzed spectrum. Decouples "how it's hit" from "what rings."

### New DSP Components

#### `ImpactExciter` — Layer 2 (processors)
Location: `dsp/include/krate/dsp/processors/impact_exciter.h`

```
Trigger: note-on event
Params: hardness (0-1), mass (0-1), brightness (0-1)
Output: short burst of shaped noise (excitation signal)

Model:
  - Generates a short noise burst (1-50ms depending on mass)
  - Shaped by hardness: soft = low-pass filtered, hard = full bandwidth
  - Envelope: single-shot exponential decay, duration from mass
  - Brightness: spectral tilt of the noise
```

This is intentionally simple — a noise burst with spectral shaping. The *musical complexity* comes from the resonance stage it feeds into.

### New Parameters

| ID | Name | Range | Default | Description |
|----|------|-------|---------|-------------|
| `kExciterTypeId` | Exciter Type | 0–2 | 0 | Residual / Impact / Bow (Phase 4) |
| `kImpactHardnessId` | Hardness | 0.0–1.0 | 0.5 | Felt → Metal striker character |
| `kImpactMassId` | Mass | 0.0–1.0 | 0.3 | Light tap → Heavy thud |

### Voice Integration

```cpp
// Exciter selection
float excitation;
switch (exciterType) {
    case kResidual: excitation = residualSynth.process(); break;
    case kImpact:   excitation = impactExciter.process(); break;
    case kBow:      excitation = bowModel.process(); break;  // Phase 4
}
float physicalSample = modalResonator.process(excitation);
```

### Success Criteria

- [ ] Impact + Modal produces convincing struck-object sounds
- [ ] Hardness sweep is audibly smooth from mallet to metallic click
- [ ] Note-on retrigger is click-free
- [ ] CPU cost negligible (single noise generator + filter)

---

## Phase 3: Waveguide String Resonance

**Goal:** Add a Karplus-Strong style waveguide as an alternative resonance model. Where modal resonator sounds like struck rigid objects, waveguides sound like strings and tubes.

**Sonic character:** Plucked strings, struck wires, metallic sustains. The waveguide's natural harmonic series aligns with the analyzed partials, but the physical feedback loop adds the characteristic "alive" quality.

### New DSP Components

#### `WaveguideString` — Layer 2 (processors)
Location: `dsp/include/krate/dsp/processors/waveguide_string.h`

```
Input: excitation signal
Config: fundamental frequency (from HarmonicFrame F0),
        damping, brightness, inharmonicity
Output: mono waveguide output

Internal:
  - Delay line tuned to F0 (length = sampleRate / f0)
  - Fractional delay via allpass interpolation (Thiran 1st order)
  - Feedback loop with:
    - One-pole lowpass filter (brightness → cutoff)
    - Loss factor (damping → feedback gain)
    - Optional allpass for inharmonicity (stiffness)
```

This reuses the existing `DelayLine` from Layer 1 but wraps it in the waveguide feedback topology.

### New Parameters

| ID | Name | Range | Default | Description |
|----|------|-------|---------|-------------|
| `kResonanceTypeId` | Resonance Type | 0–2 | 0 | Modal / Waveguide / Body (Phase 5) |
| `kWaveguideStiffnessId` | Stiffness | 0.0–1.0 | 0.0 | String stiffness (inharmonicity) |

Reuses `kResonanceDecayId` and `kResonanceBrightnessId` from Phase 1 — same musical meaning, different implementation.

### Design Note: Modal vs. Waveguide

| Property | Modal Resonator | Waveguide String |
|----------|----------------|-----------------|
| Sound character | Struck rigid body (bells, bars) | Strings, tubes, wires |
| Partial control | Independent per-partial | Coupled (harmonic series from F0) |
| Inharmonicity | From analysis (each partial free) | From stiffness model (physically coupled) |
| CPU cost | O(N) biquads, N = partial count | O(1) delay line + filters |
| Best for | Complex spectral shapes | Harmonic/near-harmonic timbres |

### Success Criteria

- [ ] Waveguide produces self-sustaining oscillation when excited
- [ ] Pitch tracks the analyzed F0 accurately (< 1 cent error)
- [ ] Brightness and damping controls produce distinct string materials
- [ ] Stiffness adds audible inharmonicity (piano-like)
- [ ] Can switch between Modal and Waveguide without clicks

---

## Phase 4: Bow Model Exciter

**Goal:** Continuous excitation for sustained physical modelling tones, replacing the one-shot nature of impacts and the static nature of residual.

**Sonic character:** Bowed strings, blown tubes, sustained friction textures. The micro-variations of stick-slip friction make sustained notes *alive* in a way that static additive frames can't achieve.

### New DSP Components

#### `BowExciter` — Layer 2 (processors)
Location: `dsp/include/krate/dsp/processors/bow_exciter.h`

```
Input: continuous (runs every sample while note is held)
Params: pressure (0-1), speed (0-1), position (0-1)
Output: continuous excitation signal

Model (simplified Friedlander friction curve):
  - Velocity difference = bowSpeed - stringVelocity (from resonator feedback)
  - Friction force = pressure * frictionCurve(velocityDifference)
  - frictionCurve: hyperbolic shape with stick/slip transition
  - Position: controls which harmonics are emphasized (node placement)
```

### New Parameters

| ID | Name | Range | Default | Description |
|----|------|-------|---------|-------------|
| `kBowPressureId` | Bow Pressure | 0.0–1.0 | 0.5 | Light (flautando) → Heavy (ponticello) |
| `kBowSpeedId` | Bow Speed | 0.0–1.0 | 0.5 | Slow (quiet) → Fast (loud, brighter) |
| `kBowPositionId` | Bow Position | 0.0–1.0 | 0.13 | Bridge → Fingerboard (harmonic emphasis) |

### Design Note: Bow ↔ Resonance Coupling

The bow model is unique because it needs **feedback from the resonator** — the friction force depends on the string's current velocity. This creates a coupling:

```
BowExciter.process(resonatorFeedback) → excitation → Resonator.process(excitation) → output
                                                                    └──→ feedback to bow
```

This is a one-sample feedback loop, same pattern as any feedback delay. The waveguide resonator naturally provides this; for the modal resonator, we sum the filter bank output as the feedback signal.

### Success Criteria

- [ ] Sustained notes have organic micro-variation (not static)
- [ ] Pressure sweep transitions smoothly from airy to gritty
- [ ] Bow + Waveguide produces convincing cello/violin-like sustain
- [ ] Bow + Modal produces interesting "bowed vibraphone" textures
- [ ] CPU cost reasonable — friction model is per-sample but simple arithmetic

---

## Phase 5: Body Resonance

**Goal:** Add the resonant character of an instrument body — guitar, violin, marimba resonator tube — as a post-resonance coloring stage.

**Sonic character:** Adds warmth, depth, and the "in a box" quality that distinguishes a raw string from a guitar. Short reverberant coloring, not room reverb.

### New DSP Components

#### `BodyResonance` — Layer 2 (processors)
Location: `dsp/include/krate/dsp/processors/body_resonance.h`

```
Input: mono signal (from modal/waveguide output)
Params: size (0-1), material (0-1), mix (0-1)
Output: mono colored signal

Internal: Small waveguide mesh or FDN (2-4 delay lines):
  - Delay lengths set by body size (5ms–50ms range)
  - Allpass diffusion for density
  - Frequency-dependent damping for material character
  - NOT a reverb — very short, colored, resonant
```

### New Parameters

| ID | Name | Range | Default | Description |
|----|------|-------|---------|-------------|
| `kBodySizeId` | Body Size | 0.0–1.0 | 0.5 | Small (violin) → Large (cello/bass) |
| `kBodyMaterialId` | Material | 0.0–1.0 | 0.5 | Warm (wood) → Bright (metal) |
| `kBodyMixId` | Body Mix | 0.0–1.0 | 0.0 | How much body coloring to apply |

### Success Criteria

- [ ] Adds audible "body" without sounding like reverb
- [ ] Size parameter changes perceived instrument scale
- [ ] Material parameter spans wood → metal convincingly
- [ ] No feedback instability at any parameter combination

---

## Phase 6: Sympathetic Resonance

**Goal:** Cross-voice harmonic bleed — when multiple notes play, their harmonics excite each other's resonators, like piano strings with sustain pedal held.

**Sonic character:** Shimmering, halo-like reinforcement of shared harmonics across voices. Chords become richer than the sum of their parts.

### New DSP Components

#### `SympatheticResonance` — Layer 3 (systems)
Location: `dsp/include/krate/dsp/systems/sympathetic_resonance.h`

```
Input: summed voice output (post per-voice processing)
Config: union of all active voices' partial frequencies
Output: sympathetic signal added to master output

Internal: Shared modal resonator bank tuned to the union of all
  active partials. Fed by the summed voice output at low gain.

Key: only partials that are common across multiple voices
  resonate strongly (reinforcement of shared harmonics).
```

### New Parameters

| ID | Name | Range | Default | Description |
|----|------|-------|---------|-------------|
| `kSympatheticAmountId` | Sympathetic | 0.0–1.0 | 0.0 | Cross-voice resonance amount |
| `kSympatheticDampingId` | Sympathetic Decay | 0.0–1.0 | 0.5 | Ring time of sympathetic resonance |

### Design Note

This is the only global (non-per-voice) physical modelling component. It lives post-voice-accumulation, pre-master-gain. The resonator bank is rebuilt whenever the set of active notes changes (voice on/off events).

### Success Criteria

- [ ] Single note: no audible effect (nothing to sympathize with)
- [ ] Octave: strong reinforcement (all harmonics shared)
- [ ] Fifth: characteristic shimmer (alternating partial reinforcement)
- [ ] Dissonant interval: minimal effect (few shared harmonics)
- [ ] Voice steal: sympathetic ring-out persists briefly after voice dies
- [ ] CPU cost scales with total active partials, not voice count × partials

---

## Parameter ID Allocation

All physical modelling parameters in the 800–899 range:

```
800  kPhysModelMixId
801  kExciterTypeId          // Phase 2
802  kResonanceTypeId        // Phase 3

810  kResonanceDecayId       // Phase 1
811  kResonanceBrightnessId  // Phase 1
812  kResonanceStretchId     // Phase 1
813  kResonanceScatterId     // Phase 1

820  kImpactHardnessId       // Phase 2
821  kImpactMassId           // Phase 2

830  kWaveguideStiffnessId   // Phase 3

840  kBowPressureId          // Phase 4
841  kBowSpeedId             // Phase 4
842  kBowPositionId          // Phase 4

850  kBodySizeId             // Phase 5
851  kBodyMaterialId         // Phase 5
852  kBodyMixId              // Phase 5

860  kSympatheticAmountId    // Phase 6
861  kSympatheticDampingId   // Phase 6
```

---

## DSP Layer Placement Summary

| Component | Layer | Location | Rationale |
|-----------|-------|----------|-----------|
| ModalResonator | 2 (processors) | `dsp/include/krate/dsp/processors/` | Self-contained filter bank, reusable |
| ImpactExciter | 2 (processors) | `dsp/include/krate/dsp/processors/` | Self-contained noise generator |
| WaveguideString | 2 (processors) | `dsp/include/krate/dsp/processors/` | Uses Layer 1 DelayLine |
| BowExciter | 2 (processors) | `dsp/include/krate/dsp/processors/` | Self-contained friction model |
| BodyResonance | 2 (processors) | `dsp/include/krate/dsp/processors/` | Small FDN, self-contained |
| SympatheticResonance | 3 (systems) | `dsp/include/krate/dsp/systems/` | Coordinates across voices |
| PhysicalModelMixer | plugin-local | `plugins/innexus/src/dsp/` | Innexus-specific routing |

---

## Evaluation Strategy

After each phase, evaluate before proceeding:

1. **Does it sound good?** Load diverse source material (voice, strings, percussion, synth). Does the physical modelling add something the pure additive path doesn't?
2. **CPU budget?** Measure per-voice cost. Target: full physical chain < 10% single core at 8 voices, 44.1kHz.
3. **Parameter intuitiveness?** Can you dial in distinct textures without reading the manual?
4. **Regression?** Mix at 0% must be identical to pre-physical-modelling output.

If a phase doesn't earn its keep sonically, skip the next phase and move to a different one. The architecture supports any subset.
