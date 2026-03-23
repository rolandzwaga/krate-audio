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

### Physical Background

Real mallet-object impacts produce a **deterministic, smooth force pulse** governed by the Hertzian contact force law:

```
F(t) = K * [compression(t)]^alpha    (during contact, F = 0 otherwise)
```

Where `K` is the stiffness coefficient (material-dependent) and `alpha` is the nonlinearity exponent:
- Elastic sphere (ideal Hertz): alpha = 1.5
- Soft felt mallet: alpha ~ 1.5–2.0
- Used piano hammer felt: alpha ~ 2.5–3.5
- Hard rubber/plastic: alpha ~ 3.0–5.0

The resulting force pulse is approximately a **half-sine** for soft mallets, becoming **narrower and more peaked** as hardness increases. It is NOT noise — noise enters the picture only through surface texture irregularities (a secondary effect) or through Smith's commuted synthesis, where body resonance is folded into the excitation signal and can be *perceptually approximated* by shaped noise.

**Key physical relationships:**
- **Contact duration scales with mass:** `T_contact ~ m^(2/5)` (Hertz). Heavier mallets → longer, broader pulses.
- **Contact duration decreases with velocity:** `T_contact ~ v^(-1/5)`. Harder strikes are shorter AND brighter — this coupling is automatic in physics but must be explicitly implemented.
- **Spectral centroid ≈ 1 / T_contact.** Perceived hardness correlates most strongly with the spectral centroid of the attack (Freed 1990).

**Key references:**
- Chaigne & Doutaut, "Numerical simulations of xylophones I" JASA 101(1), 1997 — Hertzian mallet-bar contact model
- Chaigne & Askenfelt, "Numerical simulations of piano strings I/II" JASA 1994 — nonlinear hammer-string interaction
- Stulov, "Hysteretic model of grand piano hammer felt" JASA 97(4), 1995 — asymmetric pulse with memory
- Avanzini & Rocchesso, "Modeling Collision Sounds: Non-linear Contact Force" DAFx-01, 2001 — Hunt-Crossley model for synthesis
- Freed, "Auditory correlates of perceived mallet hardness" JASA 87(1), 1990 — spectral centroid = perceived hardness
- Bork, "Measuring the acoustical properties of mallets" Applied Acoustics 30(1), 1990 — mallet shock spectra
- Smith, *Physical Audio Signal Processing* §9.3 (https://ccrma.stanford.edu/~jos/pasp/) — commuted synthesis, force-pulse synthesis, noise substitution
- Cook, "PhISM: Physically Informed Sonic Modeling" CMJ 21(3), 1997 — stochastic event modelling for percussion
- Bilbao, *Numerical Sound Synthesis* (Wiley, 2009) — finite-difference collision framework

### New DSP Components

#### `ImpactExciter` — Layer 2 (processors)
Location: `dsp/include/krate/dsp/processors/impact_exciter.h`

```
Trigger: note-on event + MIDI velocity
Params: hardness (0-1), mass (0-1), brightness (0-1), position (0-1)
Output: short excitation burst (asymmetric pulse + shaped noise)

Signal model (hybrid pulse + noise):

  1. Asymmetric deterministic pulse (primary energy):
     - Shape: skewed raised half-sine with variable peakiness
       x = t / T
       skewedX = pow(x, 1.0 - skew)        // skew = 0.3 * hardness
       pulse(t) = pow(sin(pi * skewedX), gamma)
       where gamma = 1.0 (soft felt) to ~4.0 (hard metal)
     - The skew compresses the attack phase and stretches the release,
       matching Stulov's measured asymmetric force pulses (fast rise,
       slower fall from felt hysteresis). Harder mallets are more
       asymmetric because stiffer materials rebound faster.
     - Duration T: 0.5–15ms, mapped from mass parameter
       T = T_min + (T_max - T_min) * mass^0.4
       (the 0.4 exponent approximates the Hertzian m^(2/5) scaling)
       T_min = 0.5ms (hard tap), T_max = 15ms (heavy soft mallet)
       Real contact times: hard ~0.1-2ms, soft felt up to ~10ms.
       Beyond ~15ms the pulse overlaps the resonator decay and stops
       sounding like an impact. "Thuddy" character comes from
       resonator damping, not excitation length.
     - Amplitude: pow(velocity, 0.6) (nonlinear power curve;
       exponent ~0.5-0.7 matches natural loudness perception)

  2. Micro-bounce (hardness-dependent secondary pulse):
     - For hardness > 0.6, add a secondary pulse:
       bounce delay: 0.5–2ms after primary peak (shorter for harder)
       bounce amplitude: 10–20% of primary (less for harder)
     - Both delay and amplitude are randomized per trigger:
       bounceDelay *= (1.0 + rand(-0.15, +0.15))
       bounceAmp   *= (1.0 + rand(-0.10, +0.10))
       Fixed bounce delays cause comb-filtering artifacts on
       repeated same-note triggers. Randomization breaks this.
     - Models the physical rebound of rigid strikers, which produce
       2–3 micro-impulses on contact. Soft felt absorbs the bounce.
     - Only one bounce pulse needed — diminishing returns beyond that.

  3. Noise texture (surface realism):
     - Noise envelope: follows the same pulse envelope (NOT constant).
       noise(t) = whiteNoise * pulseEnvelope(t)
       This ensures noise dies with the pulse — no residual hiss.
     - Noise spectrum: tilted by hardness before SVF filtering.
       Soft (hardness < 0.3): pink-ish (one-pole pinking filter)
       Hard (hardness > 0.7): white (unfiltered)
       This models felt's inherent high-frequency absorption.
     - Noise level: lerp(0.25, 0.08, hardness)
       Soft felt has more surface texture noise; hard metal is cleaner.
     - IMPORTANT: noise generator must be per-voice (polyphonic).
       A shared global noise buffer causes phase cancellation and
       audible flanging when playing chords. Each voice instance
       maintains its own RNG state.
     - Filtered by the same SVF as the pulse (step 5).

  4. Per-trigger micro-variation:
     - On each note-on, randomize:
       gamma *= (1.0 + rand(-0.02, +0.02))
       T     *= (1.0 + rand(-0.05, +0.05))
     - Prevents the "machine gun" effect of identical repeated strikes.
     - Noise is inherently random per trigger (different RNG seed).

  5. Hardness-controlled lowpass (2-pole SVF):
     - Base cutoff mapped from hardness parameter:
       Soft (0.0): cutoff ~ 500 Hz, steep rolloff (12 dB/oct via SVF)
       Hard (1.0): cutoff ~ 12 kHz, near-transparent
     - Pulse peakiness also from hardness:
       gamma = 1.0 + 3.0 * hardness
     - Brightness trim (from kImpactBrightnessId) offsets the cutoff:
       effectiveCutoff = baseCutoff(hardness) * exp2(brightnessTrim)
       where brightnessTrim is bipolar: -1.0 to +1.0 maps to
       -12 to +12 semitones of cutoff shift.
       At default (0.0), the physical mapping is preserved.
       This allows "sharp transient but dark tone" and vice versa.

  6. Velocity coupling (nonlinear, multi-dimensional):
     - Effective hardness (velocity-hardness cross-modulation):
       effectiveHardness = clamp(hardness + velocity * 0.1, 0, 1)
       Real mallets compress more at higher velocity, making them
       effectively stiffer. This means ff strikes sound perceptually
       distinct from pp beyond just volume — they are also "harder."
       All hardness-derived values (gamma, cutoff, skew, noise tilt)
       use effectiveHardness rather than raw hardness.
     - Cutoff modulation (exponential, not linear):
       effectiveCutoff *= exp2(velocity * k)   // k ~ 1.5
       This models the rapid brightness increase with strike force —
       felt compresses nonlinearly, becoming stiffer at higher velocity.
     - Duration modulation:
       effectiveT *= pow(1.0 - velocity, 0.2)
       Subtle shortening at high velocity (physically: T ~ v^(-1/5)).
     - Both cutoff and duration mappings are perceptually motivated:
       linear velocity mapping sounds wrong because human
       loudness/brightness perception is logarithmic.

  7. Strike position comb filter:
     - H(z) = 1 - z^(-floor(position * N))
       where N = sampleRate / f0 (one period of the fundamental)
     - Softened by blending with dry signal:
       output = lerp(input, combFiltered, 0.7)
       This avoids the "too perfect" nulls of an ideal comb filter.
       Real strike position filtering is diffuse — the contact area
       is finite, not a mathematical point.
     - position = 0.0: near bridge/edge (all harmonics present)
     - position = 0.5: center (odd harmonics only, clarinet-like)
     - Default 0.13 matches the typical "sweet spot" of struck bars
     - Note: this comb assumes harmonic spacing. With inharmonic
       resonators (stretch > 0 from Phase 1), the comb nulls won't
       align perfectly with mode frequencies — this is acceptable
       and even desirable, as real inharmonic objects also have
       imperfect position-dependent filtering.
```

**Design rationale:** This hybrid model is informed by Mutable Instruments Elements (Émilie Gillet), which uses a similar architecture: deterministic pulse + noise component + SVF brightness filter. The asymmetric pulse shape is grounded in Stulov's hysteresis measurements (JASA 1995) — real felt produces faster rise than fall. The 2-pole SVF (rather than 1-pole) provides the steeper rolloff measured in real soft-felt mallets (Bork 1990). The strike position comb filter is from extended Karplus-Strong (Jaffe & Smith 1983) and is used in Elements/Rings for mode-selective excitation. The micro-bounce models the double/triple contact observed in rigid striker impacts.

**Why not a full nonlinear contact ODE?** The Chaigne/Bilbao approach of solving the coupled mallet-resonator ODE is more accurate but requires per-sample iteration of a stiff nonlinear system. The signal-model approach above captures the perceptually important features (velocity-brightness coupling, mass-duration scaling, hardness-spectral-centroid mapping, asymmetric pulse shape, micro-bounce) at a fraction of the CPU cost. Smith's commuted synthesis work (CCRMA) validates that signal-model approximations are perceptually indistinguishable from full physical models for most musical applications.

**Retrigger strategy:** Never reset resonator state on retrigger. The excitation signal is simply *added* to the resonator input — the resonator's own dynamics handle the mixing naturally. This matches the approach used by Elements, Rings, and Plaits (Mutable Instruments) and is click-free by construction. A short attack ramp (0.1–0.5ms) on the excitation envelope prevents sample-level discontinuities. Two complementary protections against energy explosion:
- **Energy capping (safety net):** Track cumulative excitation energy over a short window (~5ms). If energy exceeds a threshold (e.g., 4x single-strike energy), attenuate new excitation proportionally. This prevents runaway energy from rapid retrigger (drum rolls, trills) without audibly gating legitimate playing.
- **Mallet choke (physical simulation):** On rapid retrigger of the same note, momentarily increase the resonator's base decay rate for ~10ms before injecting the new excitation. This simulates the physical reality that a mallet re-striking an already-vibrating bar briefly damps the existing vibration through contact. The choke amount scales with velocity — a gentle re-tap barely damps; a hard re-strike significantly attenuates the previous ring. This produces the natural "cut-then-ring" quality of real re-struck percussion.

**Future integration note (per-mode excitation scaling):** The exciter outputs a broadband signal that feeds all modes equally. For more realism, the voice layer can weight per-mode excitation gain by the excitation spectrum at each mode frequency: `modeGain_k *= excitationSpectrum(f_k)`. This makes the strike position and hardness affect each mode differently, rather than applying a single spectral envelope to the summed output. This belongs in the voice/resonator integration, not in the exciter itself, because it requires knowledge of the mode frequencies.

### New Parameters

| ID | Name | Range | Default | Description |
|----|------|-------|---------|-------------|
| `kExciterTypeId` | Exciter Type | 0–2 | 0 | Residual / Impact / Bow (Phase 4) |
| `kImpactHardnessId` | Hardness | 0.0–1.0 | 0.5 | Felt → Metal striker character. Controls SVF cutoff (500 Hz–12 kHz), pulse peakiness (gamma 1.0–4.0), asymmetry (skew 0–0.3), and noise spectrum tilt |
| `kImpactMassId` | Mass | 0.0–1.0 | 0.3 | Light tap → Heavy thud. Controls pulse duration (0.5–15ms via m^0.4 scaling) and noise mix level |
| `kImpactBrightnessId` | Brightness | -1.0–1.0 | 0.0 | Brightness trim that offsets the hardness-derived SVF cutoff by +/-12 semitones. At 0.0, the physical mapping is preserved. Allows creative overrides like "sharp transient but dark tone" |
| `kImpactPositionId` | Strike Position | 0.0–1.0 | 0.13 | Edge → Center. Softened comb filter for mode-selective excitation (70% wet blend) |

**Note on brightness as trim, not independent axis:** The hardness parameter establishes the physically-motivated default (spectral centroid tracks contact stiffness, per Freed 1990). The brightness trim loosens this coupling for creative sound design without creating a fully independent axis where most combinations are physically unmotivated. At default (0.0), the instrument behaves physically; at extremes, users get the "sharp but dark" or "soft but bright" combinations they may want.

### Voice Integration

```cpp
// Unified exciter interface (all types share process(feedbackVelocity))
// MIDI velocity is set at trigger time via exciter->trigger(velocity), not per-sample.
float feedbackVelocity = resonator->getFeedbackVelocity();  // 0 when not bowed
float excitation = exciter->process(feedbackVelocity);
// Resonator state is never reset on retrigger — excitation adds to existing vibration.
// Energy capping prevents explosion from rapid retrigger.
float physicalSample = resonator->process(excitation);

// Future: per-mode excitation weighting
// for (int k = 0; k < numModes; ++k)
//     modeInput[k] = excitation * excitationSpectrum(modeFreq[k]);
```

### Success Criteria

- [ ] Impact + Modal produces convincing struck-object sounds across diverse analyzed timbres
- [ ] Hardness sweep is audibly smooth from felt mallet (dark, fundamental-heavy) to metallic click (bright, partial-rich)
- [ ] Pulse asymmetry is audible: hard strikes have snappier attack than soft strikes
- [ ] MIDI velocity affects loudness, brightness, AND effective hardness (exponential coupling, not linear)
- [ ] ff strikes sound perceptually "harder" than pp strikes at the same hardness setting
- [ ] Per-trigger micro-variation: repeated same-note strikes sound slightly different each time
- [ ] Brightness trim allows creative overrides without breaking the physical default
- [ ] Strike position creates audible harmonic filtering (center = hollow/odd-harmonics, edge = full spectrum)
- [ ] Note-on retrigger is click-free (additive injection, no resonator reset)
- [ ] Rapid retrigger (drum roll, trill) does not cause energy explosion (energy cap + mallet choke)
- [ ] Re-striking a ringing note produces natural "cut-then-ring" damping of previous vibration
- [ ] CPU cost negligible (pulse generator + SVF + comb filter per voice)

---

## Phase 3: Waveguide String Resonance

**Goal:** Add a digital waveguide string resonator as an alternative to the modal resonator bank. The digital waveguide models wave propagation on a string via a delay-line feedback loop, producing timbres characteristic of strings and tubes rather than struck rigid bodies.

**Sonic character:** Plucked strings, struck wires, metallic sustains. The waveguide naturally produces a complete harmonic series from a single delay loop, and the physical feedback topology imparts the characteristic "alive" quality of real vibrating strings — including frequency-dependent decay, dispersion, and nonlinear interactions.

**Scientific foundation:** The digital waveguide is the physical modelling interpretation of the Karplus-Strong algorithm (Karplus & Strong, 1983). Julius O. Smith III showed (Smith, 1992) that a delay-line feedback loop with a lowpass filter is equivalent to discretising the 1D wave equation for a lossy string, where the delay line models travelling-wave propagation and the loop filter models distributed energy losses. The Extended Karplus-Strong (EKS) algorithm (Jaffe & Smith, 1983) added pick-position filtering, stiffness allpass, dynamic-level control, and fractional-delay tuning.

### New DSP Components

#### `WaveguideString` — Layer 2 (processors)
Location: `dsp/include/krate/dsp/processors/waveguide_string.h`

```
Input: excitation signal (shaped noise burst or impulse)
Config: fundamental frequency (from HarmonicFrame F0),
        damping (T60), brightness, inharmonicity (stiffness coefficient B),
        pick position
Output: mono waveguide output

Signal flow (per sample):
  excitation ──►(+)──► [Soft Clip] ──► [Delay Line: N samples] ──► [Dispersion Allpass] ──►
                 ▲                                                                          │
                 │                                                                          ▼
                 │◄──── [DC Blocker] ◄── [Loss Filter] ◄────────── [Tuning Allpass] ◄───────┘
                 │
                 ▼
              output (tap)

Component details:

  1. Delay line — Integer part of loop delay:
     N = floor(fs / f0 - D_loss - D_dispersion - D_dc - D_tuning)
     where D_loss, D_dispersion, D_dc are the group delays (in samples) of the
     loss filter, dispersion allpass cascade, and DC blocker respectively,
     all evaluated at f0. This is critical: the dispersion filter's group delay
     at f0 changes with the stiffness coefficient B, so turning the Stiffness
     knob changes the effective loop length. If only D_loss is subtracted (as in
     simpler implementations), notes will go progressively flat as stiffness
     increases. D_tuning is the fractional delay handled by the tuning allpass.
     Reuses existing DelayLine from Layer 1.

  2. Tuning allpass (fractional delay) — Thiran 1st-order allpass interpolation.
     Compensates for fractional sample delay AND loop filter phase delay.
     H_eta(z) = (eta + z^-1) / (1 + eta * z^-1)
     where eta = (1 - Delta) / (1 + Delta), Delta = fractional part of total loop delay.
     Thiran allpass provides maximally flat group delay at DC (Thiran, 1971),
     which is optimal since pitch perception is most acute at low frequencies.
     Alternative: Lagrange 3rd-order FIR for non-recursive option (no state to
     manage on pitch changes), at the cost of slight high-frequency coloring.
     WARNING: Lagrange filters are non-unitary (|H| < 1 at high frequencies),
     which introduces pitch-dependent damping inside the feedback loop — higher
     notes lose more energy per round trip than lower notes. This "accidental
     damping" changes the timbral character unpredictably with pitch. Thiran
     allpass preserves energy (|H| = 1 at all frequencies) and should be
     preferred for in-loop use. Reserve Lagrange for output taps only.

  3. Loss filter (damping + brightness) — Weighted one-zero filter:
     H(z) = rho * [(1 - S) + S * z^-1]
     where:
       rho = frequency-independent loss per round trip = 10^(-3 / (T60 * f0))
       S = brightness parameter (0 = no freq-dependent loss, 0.5 = original KS averaging)
     At S = 0.5 this reduces to the original Karplus-Strong averaging filter
     H(z) = 0.5 * (1 + z^-1), but the parameterised form allows independent
     control of overall decay time (rho) and spectral tilt (S).

     The gain constraint |H(e^{j*omega})| <= 1 must hold at ALL frequencies to
     ensure passivity/stability (Smith, PASP). The filter models the combined
     effect of all distributed losses in a real string:
       - Air damping (frequency-independent, viscous drag)
       - Internal friction (frequency-dependent, dominant loss mechanism)
       - Bridge/nut radiation losses

     Design target per harmonic: |H(omega_n)| = 10^(-3 / (T60(n) * f0))
     For real strings, T60(n) ~ T60(1) / (1 + alpha * n^2) due to internal
     friction being proportional to frequency squared (Vallette, 1995).

     Tuning compensation: The loss filter's phase delay must be accounted for in
     the total loop delay. Note that the analytical value (S samples at DC for the
     one-zero filter) is only an approximation — it diverges at higher frequencies.
     In practice, use empirical tuning correction: measure the actual pitch output
     against the target F0 and apply a small correction LUT or polynomial fit
     indexed by f0 and S. This is especially important at high stiffness settings
     where dispersion interacts with loss filter phase to compound the tuning error.

  4. DC blocker — First-order highpass, placed OUTSIDE the primary feedback loop
     (between loop output and the final output tap) when possible, to avoid
     introducing phase lag into the resonant loop. If placed inside the loop
     (necessary when DC drift is severe), use a very low cutoff to minimise
     pitch interaction:
     H(z) = (1 - z^-1) / (1 - R * z^-1),  R = 0.9995 at 44.1 kHz (fc ~ 3.5 Hz)
     At this cutoff, phase contribution at the lowest playable F0 (~20 Hz) is
     negligible (<0.01 samples). If placed outside the loop, R = 0.995 (fc ~ 35 Hz)
     is fine since it doesn't affect loop tuning.
     Prevents DC offset accumulation from numerical round-off and asymmetric
     excitation. The zero at z=1 kills DC exactly; the nearby pole preserves
     bass content (Smith, PASP).

  5. Dispersion allpass (stiffness/inharmonicity) — Cascade of 2nd-order allpass
     sections (biquads) modelling frequency-dependent wave speed in stiff strings.
     Real string partials follow Fletcher's formula:
       f_n = n * f0 * sqrt(1 + B * n^2)
     where B is the inharmonicity coefficient:
       B = (pi^3 * E * a^4) / (16 * L^2 * K)
       E = Young's modulus, a = string radius, L = length, K = tension
     Typical B values: 0.00001 (piano bass) to 0.01+ (piano treble).
     Guitar strings have much lower B.

     The allpass filter provides the required frequency-dependent phase shift
     without affecting amplitude. Design methods (in order of sophistication):
       a) Van Duyne & Smith (1994): cascade of 1st-order allpass sections
       b) Rauhala & Välimäki (2006): Thiran-based closed-form 2nd-order sections
       c) Abel, Välimäki & Smith (2010): optimal biquad cascade from group delay —
          state-of-the-art, numerically robust, always stable
     For this implementation: 2-4 biquad sections sufficient for guitar/moderate
     stiffness. 6-8+ sections needed for realistic piano bass strings.
     User parameter "Stiffness" maps to B coefficient (0.0 = flexible string,
     1.0 = maximum inharmonicity).

     Stiffness modulation strategy: Changing B during a sounding note requires
     updating allpass biquad coefficients. Direct coefficient replacement causes
     state mismatch → audible "zing" or "chirp" transients. Two approaches:
       a) Freeze-at-onset (simple): compute dispersion coefficients at note-on,
          hold fixed for the note's lifetime. Stiffness knob only affects new notes.
          This is the recommended Phase 3 approach.
       b) Crossfaded interpolation (advanced): maintain two parallel dispersion
          filter chains, crossfade output over 5-10 ms when coefficients change.
          More expensive but allows real-time stiffness modulation. Consider for
          Phase 4+ if musically justified.

  6. In-loop soft clipper — Safety limiter placed before the delay line input:
     y = (|x| < threshold) ? x : threshold * tanhf(x / threshold)
     with threshold ≈ 1.0 (0 dBFS). Transparent at normal signal levels.
     Purpose: fast parameter sweeps can temporarily violate passivity before
     coefficient smoothing catches up, causing the loop to blow up. The soft
     clipper bounds the signal regardless of filter state, mimicking the
     physical limit of a string's maximum displacement. Cost: one comparison
     + occasional tanhf per sample — negligible. This parallels the output
     safety limiter already spec'd for ModalResonatorBank (Phase 1).
```

This reuses the existing `DelayLine` from Layer 1 but wraps it in the waveguide feedback topology.

**On filter ordering:** In the steady-state (fixed pitch, fixed parameters), all loop components are LTI and commute with the delay line (Smith, 1992), so their placement order does not affect the frequency response. However, **commutativity breaks under time-varying conditions** — pitch changes, stiffness modulation, and damping sweeps all violate the LTI assumption. In practice this means:
- The chosen filter ordering is **fixed at design time** and must be validated empirically under modulation
- Parameter changes must be smoothed (see Pitch Tracking and Stiffness Modulation sections) to keep the system "locally LTI" — i.e., parameters change slowly relative to the loop period
- Phase 4 (bow model) introduces a nonlinear element that fundamentally cannot commute; the bow junction's position in the loop is physically determined and non-negotiable

#### Excitation Design

The excitation signal determines the initial timbre before the loop filter shapes the decay:

- **Noise burst (default pluck):** Fill delay line with bandlimited noise. Lowpass-filtering the noise before injection provides dynamic level control — brighter noise = louder/harder pluck, matching how real instruments produce brighter timbres at higher dynamics (Karplus & Strong, 1983).
- **Pick-position comb filter (EKS):** `H_beta(z) = 1 - z^{-round(beta*N)}` where beta is normalised pick position (0–1). Creates spectral nulls at harmonics that are integer multiples of 1/beta. Plucking at 1/5 kills the 5th, 10th, 15th... harmonics. Very cheap (one subtraction) and musically powerful. **Important:** The comb delay `round(beta*N)` depends on the delay line length N. Under pitch changes, N changes, so the spectral nulls would drift unless handled. **Policy: evaluate pick position at note onset and freeze for the note's lifetime.** This matches real instruments (you pluck at a fixed point; you don't move the pluck during a note). The pick-position parameter only affects new excitations.
- **Shaped excitation from analysis:** Since Innexus has harmonic analysis data (F0, partial amplitudes), the excitation spectrum can be shaped to match the target spectral envelope. A filtered noise burst injected into the waveguide causes the loop filter to naturally evolve the spectrum over time.
- **Commuted synthesis (advanced):** Body impulse response convolved with excitation into a single "aggregate excitation table" (Smith, 1993). Eliminates body filter from the per-sample loop. Relevant when Phase 5 (Body Resonance) is integrated.

#### Architecture for Phase 4 (Bow Model) Compatibility

The waveguide string must be designed to support bowing in Phase 4 without a fundamental redesign. Bowing requires a **nonlinear scattering junction** that cannot be added as an afterthought — it changes the core topology from a single delay loop to two coupled delay segments with a nonlinear two-port between them.

**Phase 3 design requirements to avoid Phase 4 retrofit pain:**

1. **Two delay segments, not one monolithic delay line.** The string is split at the interaction point into segment A (nut-side, length `beta*N` samples) and segment B (bridge-side, length `(1-beta)*N` samples). For Phase 3 pluck, excitation injects into the junction between them and both segments share the same loop filter. For Phase 4 bow, the junction becomes a nonlinear scattering element.

2. **Scattering junction interface.** Define an abstract junction that takes two incoming velocity waves (one from each segment) and produces two outgoing waves:
   ```
   struct ScatteringJunction {
       float characteristicImpedance = 1.0f;  // Z = sqrt(T * mu), normalised
       // Phase 3: unused (pluck is impedance-independent)
       // Phase 4: bow reflection coefficient depends on Z_bow / Z_string ratio

       virtual void scatter(float v_in_left, float v_in_right,
                           float& v_out_left, float& v_out_right) = 0;
   };
   ```
   - Phase 3 `PluckJunction`: passes waves through with additive excitation injection. Trivial — effectively transparent except at the moment of excitation.
   - Phase 4 `BowJunction`: implements velocity-dependent friction model. The bow-string interaction uses a memoryless nonlinear reflection function: `rho_t(v_d) = r(v_d) / (1 + r(v_d))` where `v_d = v_bow - (v_in_left + v_in_right)` is the differential velocity and `r()` encodes the static/dynamic friction characteristic (Smith, PASP Ch. 9).

3. **Velocity waves, not displacement.** The waveguide should internally use velocity waves (not displacement), since bow interaction is defined in terms of velocity. Displacement output can be obtained by integration if needed. This is a design choice that costs nothing in Phase 3 but saves a rewrite in Phase 4.

4. **Interaction point as runtime parameter.** The position dividing segments A and B must be changeable (pick position for pluck, bow position for bow). When the position changes, samples transfer between the two delay segments.

### New Parameters

| ID | Name | Range | Default | Description |
|----|------|-------|---------|-------------|
| `kResonanceTypeId` | Resonance Type | 0–2 | 0 | Modal / Waveguide / Body (Phase 5) |
| `kWaveguideStiffnessId` | Stiffness | 0.0–1.0 | 0.0 | String stiffness (inharmonicity coefficient B) |
| `kWaveguidePickPositionId` | Pick Position | 0.0–1.0 | 0.13 | Normalised pluck/interaction point (0.13 ≈ guitar bridge pickup) |

Reuses `kResonanceDecayId` and `kResonanceBrightnessId` from Phase 1 — same musical meaning, different physical implementation:
- **Decay** maps to `rho` (frequency-independent loss factor per round trip)
- **Brightness** maps to `S` (spectral tilt of the one-zero loss filter; 0 = flat decay, 1 = maximum high-frequency damping)

### Design Note: Modal vs. Waveguide

| Property | Modal Resonator | Waveguide String |
|----------|----------------|-----------------|
| Sound character | Struck rigid body (bells, bars) | Strings, tubes, wires |
| Partial control | Independent per-partial | Coupled (harmonic series from F0) |
| Inharmonicity | From analysis (each partial free) | From stiffness model (physically coupled via Fletcher's formula) |
| CPU cost | O(M) biquads, M = partial count (~9M ops/sample) | O(1) delay line + filters (~10-12 ops/sample) |
| Harmonic content | Only explicitly modelled partials | All harmonics up to Nyquist (free) |
| Memory | 4 state vars per mode | N samples for delay buffer (N = fs/f0) |
| Best for | Complex spectral shapes, inharmonic timbres | Harmonic/near-harmonic timbres, string/tube sounds |
| Pitch changes | Retune each biquad independently | Retune single delay length (+ allpass state update) |

**CPU comparison:** For 32 modes at 44.1 kHz, the unvectorised modal bank costs ~288 ops/sample. A single waveguide string costs ~10-12 ops/sample. However, the modal bank is embarrassingly parallel (each biquad is independent) and benefits greatly from SIMD — with AVX2 processing 8 modes per vector op, effective cost drops to ~36-72 ops/sample equivalent. The waveguide loop is inherently sequential (feedback dependency) with limited SIMD opportunity within a single string. **Realistic advantage: ~5-10x cheaper per string** after SIMD optimisation of the modal path. Still a significant win, especially for polyphonic use and sympathetic resonance (Phase 6) where multiple waveguide strings run in parallel (and *can* be SIMD-vectorised across strings).

### Energy Normalisation Across F0

Without compensation, waveguide output level varies with pitch:
- **Lower notes** have longer delay lines → more energy stored in the loop → louder output
- **Higher notes** have shorter delay lines → less stored energy → quieter output
- Additionally, the loop filter and fractional delay filter introduce pitch-dependent gain variations

**Required compensation:**

1. **Delay-length normalisation:** Scale excitation amplitude or output gain by `sqrt(f0 / f_ref)` where `f_ref` is a reference frequency (e.g., middle C, 261.6 Hz). This compensates for the energy density difference. The square root comes from the energy being proportional to the number of samples in the loop.

2. **Loop gain verification per note:** At each note-on, compute the total loop gain `G_total = |H_loss| * |H_dc| * |H_dispersion|` at the fundamental frequency. Apply a correction factor `1 / G_total` to the excitation level to ensure consistent perceived loudness.

3. **Velocity curve calibration:** After energy normalisation, the velocity-to-excitation-amplitude mapping must produce perceptually consistent dynamics across the pitch range. This requires empirical calibration — a linear mapping will not sound even.

Without this, low notes will be noticeably louder than high notes, and velocity response will feel inconsistent across the keyboard.

### Click-Free Model Switching

Switching between Modal and Waveguide resonance types must be artifact-free:

1. **Output-domain equal-power crossfade** over 20–30 ms (882–1323 samples at 44.1 kHz)
2. During the crossfade, **both models run in parallel** receiving the same excitation
3. Apply cosine crossfade: `out = old * cos(t*pi/2) + new * sin(t*pi/2)` for equal power
4. Optionally: initialise the waveguide delay buffer from the modal bank's current output for phase continuity at the switch point

The cost of running both models for 20 ms is negligible (~0.1% of a second of audio).

### Resonator Interface (Shared Abstraction)

Modal and Waveguide resonators must conform to a common interface. This is what turns "multiple synthesis engines" into "one instrument with interchangeable physics."

**Scope:** This interface covers **per-voice resonators only** — components that consume excitation and produce resonated audio. It does NOT cover:
- **Body Resonance (Phase 5):** A post-processing coloring stage that takes resonator output, not raw excitation. Different topology — it wraps a resonator, it isn't one.
- **Sympathetic Resonance (Phase 6):** Global (post-voice-accumulation), not per-voice. Operates on summed output of all voices.
- **Exciters (Impact, Bow):** These produce excitation signals. They are the resonator's input, not its peer.

```cpp
class IResonator {
public:
    virtual ~IResonator() = default;

    // --- Configuration (called at note-on or on frame advance) ---
    virtual void setFrequency(float f0) = 0;
    virtual void setDecay(float t60) = 0;
    virtual void setBrightness(float brightness) = 0;

    // --- Per-sample processing ---
    virtual float process(float excitation) = 0;  // returns audio sample

    // --- Energy observation ---
    virtual float getControlEnergy() const = 0;     // fast EMA (τ ≈ 5 ms)
    virtual float getPerceptualEnergy() const = 0;  // slow EMA (τ ≈ 30 ms)

    // --- State management ---
    virtual void silence() = 0;            // clear all internal state (including energy followers)

    // --- Bow coupling (Phase 4 forward-compatibility) ---
    // Returns the resonator's current output velocity at the interaction point.
    // For Phase 3: returns 0 (no feedback needed for pluck excitation).
    // For Phase 4: waveguide returns velocity wave sum at bow position;
    //              modal returns sum of mode outputs (approximation).
    virtual float getFeedbackVelocity() const { return 0.0f; }
};
```

**Design decisions:**
- **No `noteOn`/`noteOff`:** The voice engine owns note lifecycle. Resonators are stateless w.r.t. note events — they respond to excitation and parameter changes. The voice calls `silence()` on voice steal and `setFrequency()` on new notes.
- **No `setParameter(int, float)`:** Named setters preserve type safety. Each resonator type may have additional type-specific setters (e.g., `setStiffness()` on WaveguideString, `setStretch()`/`setScatter()` on ModalResonatorBank) called by the voice engine when it knows the active type.
- **Per-sample `process(excitation)`:** Returns `float` (audio sample), not a struct. Energy is queried separately via `getControlEnergy()`/`getPerceptualEnergy()`, which the voice engine reads when needed (crossfade decisions, choking logic) — not every sample. This avoids per-sample struct overhead.
- **`getFeedbackVelocity()`:** Forward-compatible hook for Phase 4 bow coupling. Default returns 0, so Phase 3 pluck doesn't need to implement it.

#### Energy Model

Energy is a **first-class architectural observable**, not a diagnostic. It serves as the shared perceptual coordinate system that makes all resonator types interchangeable.

**Two energy layers with different time constants:**

| Layer | Time constant | Purpose | Consumers |
|-------|--------------|---------|-----------|
| Control energy (fast) | τ ≈ 5 ms | Retrigger choking, excitation interaction limits, impact detection | Voice engine retrigger logic |
| Perceptual energy (slow) | τ ≈ 30 ms | Crossfade normalisation, gain matching, voice balancing, UI metering | Voice engine crossfade, mixer |

Using one time constant for both **will** cause audible problems: a fast τ makes crossfades pump; a slow τ makes choking laggy. These are fundamentally different use cases.

**Implementation (identical for all resonator types):**

Both energy followers are one-pole EMA filters applied to the squared output signal, computed inside `process()`:

```cpp
// Inside process(), after computing output sample:
float x2 = output * output;
controlEnergy_  = kControlAlpha  * controlEnergy_  + (1.0f - kControlAlpha)  * x2;
perceptualEnergy_ = kPerceptualAlpha * perceptualEnergy_ + (1.0f - kPerceptualAlpha) * x2;

// Constants (computed once at setSampleRate):
// kControlAlpha    = expf(-1.0f / (0.005f * sampleRate));   // τ = 5 ms
// kPerceptualAlpha = expf(-1.0f / (0.030f * sampleRate));   // τ = 30 ms
```

**Why measure at the output tap, not from internal state:**
- Modal internal state (sum of mode energies) double-counts spectrally overlapping modes
- Waveguide delay buffer energy is spatial, not perceptual (phase distribution matters)
- By measuring the same thing (output power) with the same EMA, all resonator types are **automatically on the same perceptual scale** without per-model calibration factors
- The output tap is the scattering junction for waveguides — exactly where energy should be measured

**Cross-model energy contract:**

> Equal perceived loudness → approximately equal `getPerceptualEnergy()` values, regardless of resonator type.

This is achieved automatically because all types use the same EMA on the same signal (their output). No per-model normalisation factors needed — that complexity only arises when you try to compute energy from internal state.

**Energy-aware crossfade (voice engine, during resonator type switch):**

```cpp
float a = oldResonator->process(excitation);
float b = newResonator->process(excitation);
float eA = oldResonator->getPerceptualEnergy();
float eB = newResonator->getPerceptualEnergy();
float gainMatch = (eB > 1e-20f) ? sqrtf(eA / eB) : 1.0f;
float t = crossfadeProgress;  // 0→1 over 20-30 ms
output = a * cosf(t * kHalfPi) + b * clampf(gainMatch, 0.25f, 4.0f) * sinf(t * kHalfPi);
```

The `gainMatch` clamp (0.25–4x = ±12 dB) prevents extreme corrections when one model is near-silent during startup transients.

**Semantic guarantees (the real contract):**
1. **Passive energy:** For zero excitation, both energy values must monotonically decay. No self-oscillation unless explicitly driven (bow model drives through excitation, not internally).
2. **Causal:** `process(excitation)` must not depend on future excitation samples. No lookahead, no block-level buffering dependency.
3. **Parameter smoothing:** All `set*()` calls must be internally smoothed to sample rate OR documented as note-onset-only (e.g., stiffness). Unsmoothed parameter changes cause clicks regardless of crossfade quality.
4. **Deterministic:** Given identical excitation and parameter sequences, output and energy must be reproducible (no random internal state after `silence()`).

### Pitch Tracking and Dynamic Retuning

When the analysed F0 changes (new note, pitch bend, vibrato):

- **Smooth delay length interpolation** over 5–20 ms prevents clicks
- For perceptually linear portamento, interpolate in log-frequency space: `delay = fs / exp(lerp(log(f_start), log(f_end), t))`
- **Thiran allpass state update:** When the fractional delay coefficient changes, the allpass filter state must be updated to avoid transient clicks (Välimäki, Laakso & Mackenzie, 1995). The simplest approach: reset the allpass state to zero (acceptable for slow changes); for fast vibrato, use the state-variable update method from the cited paper.

### Stability Constraints

The waveguide feedback loop MUST satisfy these conditions:

1. **Passivity:** `|H_loop(e^{j*omega})| <= 1` at ALL frequencies — the loop filter, DC blocker, and dispersion allpass combined must never amplify any frequency component
2. **DC gain:** Set `H_loop(1) <= 1.0` (typically exactly 1.0 for maximum sustain, or slightly below for finite decay)
3. **Verification:** After computing filter coefficients, sweep the frequency response and renormalise if any frequency exceeds unity gain (Smith, PASP)
4. **Nonlinear extensions (future):** For bowed strings (Phase 4), use `tanh()` saturation to bound the feedback — `|tanh(a*x)| < |x|` for all `|x| > 0`, guaranteeing passivity

**Numerical drift (practical concern):** Even with mathematically passive filters, floating-point round-off accumulates over long decays (>10 s). This manifests as:
- Inconsistent decay tails across CPU architectures (x87 vs SSE vs ARM)
- Residual low-level noise or DC offset that never fully decays

Mitigations:
- **Energy floor clamp:** After each loop iteration, if `|sample| < epsilon` (e.g., 1e-20), force to zero. This prevents denormal accumulation and ensures clean silence.
- **Periodic renormalisation (optional):** Every N loop iterations, measure RMS energy in the delay buffer. If it exceeds expected decay envelope by more than a threshold, scale the buffer down. This is a safety net, not a primary mechanism — if it triggers frequently, the filter design has a bug.
- **FTZ/DAZ:** Enable flush-to-zero and denormals-are-zero on x86 (already project policy). This eliminates the most common source of numerical drift in long tails.

### Success Criteria

**Core function:**
- [ ] Waveguide produces self-sustaining oscillation when excited with noise burst
- [ ] Pitch tracks the analysed F0 accurately (< 1 cent error at fundamental, empirically verified)
- [ ] Tuning compensation accounts for loop filter phase delay (empirical correction, not analytical-only)
- [ ] Brightness and damping controls produce distinct string materials (nylon → steel → piano wire)
- [ ] Stiffness adds audible, physically-correct inharmonicity following Fletcher's formula
- [ ] Pick position creates audible spectral nulls at expected harmonics (frozen at note onset)

**Stability and robustness:**
- [ ] DC blocker prevents offset accumulation over sustained notes (> 30 s)
- [ ] Loop filter gain verified <= 1.0 at all frequencies (passivity/stability)
- [ ] Energy floor clamp prevents denormal accumulation in long decay tails
- [ ] Consistent decay behaviour across x86 SSE and ARM (FTZ/DAZ enabled)

**Energy and loudness:**
- [ ] Output level consistent across F0 range (energy normalisation applied)
- [ ] Velocity response perceptually even across pitch range

**Integration:**
- [ ] Can switch between Modal and Waveguide without clicks (equal-power crossfade via IResonator interface)
- [ ] Stiffness parameter frozen at note onset (no modulation artifacts in Phase 3)
- [ ] Two-segment delay line architecture ready for Phase 4 scattering junction
- [ ] IResonator interface implemented by both ModalResonatorBank and WaveguideString

### References

| Ref | Citation | Relevance |
|-----|----------|-----------|
| [KS83] | Karplus & Strong, "Digital Synthesis of Plucked-String and Drum Timbres", CMJ 7(2), 1983 | Original algorithm |
| [JS83] | Jaffe & Smith, "Extensions of the Karplus-Strong Plucked-String Algorithm", CMJ 7(2), 1983 | EKS: pick position, stiffness, tuning allpass, decay stretching |
| [S92] | Smith, "Physical Modeling Using Digital Waveguides", CMJ 16(4), 1992 | Waveguide theory: delay line = wave equation discretisation |
| [PASP] | Smith, "Physical Audio Signal Processing", CCRMA online book, 2010 | Definitive reference: loop filters, DC blocking, stability, commuted synthesis |
| [T71] | Thiran, "Recursive Digital Filters with Maximally Flat Group Delay", IEEE Trans. Circuit Theory, 1971 | Thiran allpass fractional delay design |
| [VL00] | Välimäki & Laakso, "Principles of Fractional Delay Filters", IEEE ICASSP, 2000 | Survey of fractional delay methods for waveguides |
| [VLM95] | Välimäki, Laakso & Mackenzie, "Elimination of Transients in Time-Varying Allpass Fractional Delay Filters", ICMC, 1995 | Click-free pitch changes with allpass interpolation |
| [BV03] | Bank & Välimäki, "Robust Loss Filter Design for Digital Waveguide Synthesis of String Tones", IEEE SPL 10(1), 2003 | Higher-order IIR loss filter design |
| [VDS94] | Van Duyne & Smith, "A Simplified Approach to Modeling Dispersion Caused by Stiffness", ICMC, 1994 | First practical allpass dispersion filter |
| [RV06] | Rauhala & Välimäki, "Tunable Dispersion Filter Design for Piano Synthesis", IEEE SPL 13, 2006 | Thiran-based closed-form dispersion biquads |
| [AVS10] | Abel, Välimäki & Smith, "Robust, Efficient Design of Allpass Filters for Dispersive String Sound Synthesis", IEEE SPL 17(4), 2010 | State-of-art dispersion filter design |
| [FR98] | Fletcher & Rossing, "The Physics of Musical Instruments", Springer, 1998 | Fletcher's inharmonicity formula: f_n = n*f0*sqrt(1+B*n^2) |
| [V95] | Vallette, "The Mechanics of Vibrating Strings", Springer, 1995 | Physical loss mechanisms in strings |

---

## Phase 4: Bow Model Exciter

**Goal:** Continuous excitation for sustained physical modelling tones, replacing the one-shot nature of impacts and the static nature of residual.

**Sonic character:** Bowed strings, blown tubes, sustained friction textures. The micro-variations of stick-slip friction make sustained notes *alive* in a way that static additive frames can't achieve.

### Physical Background

#### Helmholtz Motion and Stick-Slip Dynamics

A bowed string vibrates in **Helmholtz motion** [H1863]: the string forms two straight-line segments connected by a sharp corner (the "Helmholtz corner") that circulates around the string at the wave speed. At the bowing point, the string alternates between:

- **Stick phase:** The corner travels away from the bow point. String moves with the bow (friction holds).
- **Slip phase:** The corner passes the bow point. String suddenly slides back against the bow direction.

The velocity at the bow point approximates a **sawtooth wave** — constant during stick, sharp jump during slip — which gives bowed strings their rich harmonic content. The ratio of stick time to slip time is determined by the bow position β (distance from bridge as a fraction of string length).

#### The Schelleng Diagram (Playability Regions)

Schelleng [S73] established that bow force (F) and bow position (β) define three vibration regimes on a log-log plot:

1. **Below F_min — Surface sound / sul tasto:** Bow slides without establishing Helmholtz motion. Thin, glassy, airy tone.
2. **Between F_min and F_max — Helmholtz region:** Stable stick-slip oscillation. Clean, musical bowing.
3. **Above F_max — Raucous / scratchy:** Multiple slip events per period, no clean Helmholtz corner. Harsh, crunchy texture.

The boundary equations (approximate):
```
F_min ∝ v_bow / β²     (minimum force for Helmholtz motion)
F_max ∝ v_bow / β      (maximum force before raucous regime)
```

The playable range **narrows as β decreases** (bow closer to bridge). This is why sul ponticello requires careful pressure control. All three regimes are musically useful in a synthesizer context.

#### Attack Transients: The Guettler Diagram

Guettler [G02] mapped the parameter space of **bow acceleration** vs **bow force** for attack quality:

- A **triangular "playable" region** (vertex at origin) produces clean attacks where Helmholtz motion establishes within ~10-20 nominal periods.
- Outside this region: scratchy transients with multiple flyback, or failed attacks.
- The triangle narrows as bow position moves closer to the bridge.

Pre-Helmholtz transients involve chaotic stick-slip interaction producing broadband noise — the characteristic "crunch" at bow onset. The **rate of bow velocity increase** and **initial bow force** determine whether the attack is clean or scratchy.

### Friction Curve Models

The core of bow modelling is the **friction function** mapping relative velocity between bow and string to friction force. Five models exist in the literature, with increasing realism and cost:

#### Model A: STK Power-Law Bow Table (Recommended Primary)

The STK (Synthesis Toolkit) [CS99] uses a memoryless nonlinearity proven in real-time synthesis since the 1990s:

```
bowTable(v_delta) = clamp( (|v_delta * slope + offset| + 0.75)^(-4), 0.01, 0.98 )

where: slope = 5.0 - 4.0 * bowPressure    (maps pressure 0→1 to slope 5→1)
       offset = 0.0                        (friction asymmetry, typically zero)
```

The force injected into the resonator is `v_delta * bowTable(v_delta)`, i.e. the product of velocity difference and reflection coefficient.

**Optimization:** Replace `pow(x, -4)` with `1.0f / (x * x * x * x)` — four multiplies instead of an expensive `pow()` call. Can also be pre-computed as a 256-entry lookup table for further savings.

**Characteristics:**
- Sharp peak at zero differential velocity (stick phase) that drops rapidly (slip phase)
- Higher pressure → lower slope → wider sticking zone → more "grabbing"
- No hysteresis (memoryless) — simplifies implementation, sacrifices some transient realism
- CPU cost: ~4 multiplies + 1 fabs + 1 clamp per sample

This is also exactly what Faust's `physmodels.lib` uses:
```faust
bowTable(offset, slope) = pow(abs(sample) + 0.75, -4) : min(1)
with { sample = +(offset) * slope; };
violinBowTable(bowPressure) = bowTable(0, 5 - 4 * bowPressure);
```

#### Model B: Friedlander/Keller Piecewise Friction

The original physical model [F53] uses a piecewise friction curve:

```
f(v) = μ_s * F_N                                           for |v| ≤ v_capture (stick)
f(v) = μ_d * F_N + (μ_s - μ_d) * F_N * v_capture / |v|    for |v| > v_capture (slip)
```

Where μ_s = static friction coefficient, μ_d = dynamic friction coefficient, F_N = normal (bow) force, v_capture = capture velocity.

**Hysteresis:** The Friedlander model can have up to 3 intersection points with the string's impedance load line, creating hysteresis in stick-slip transitions. The physical system follows the branch closest to its current state. The STK bow table ignores this hysteresis entirely.

#### Model C: Thermal Friction (Future Enhancement)

Smith & Woodhouse [SW00] showed that rosin friction depends on **temperature**, not just velocity. Frictional heating during stick phase softens rosin, reducing viscosity; cooling during slip re-hardens it. This creates naturally hysteretic friction without explicit multi-intersection tracking:

```
dT/dt = (friction_power - heat_loss) / thermal_mass
μ(v, T) = f(v, T)    // friction depends on both velocity AND temperature
```

**Benefit:** More realistic transient behavior — Helmholtz motion establishes more reliably and quickly than with the classic memoryless model. Significantly better attack quality.

**Cost:** ~2-3x the simple bow table (one additional state variable per sample).

**Recommendation:** Consider as a future "quality" option for users who want more expressive transients. Not needed for initial implementation.

#### Model D: Elasto-Plastic Bristle (Research Reference Only)

Dupont et al. [D02], applied to bowed strings by Serafin [S04] at IRCAM/CCRMA. Models bow-string contact as a deformable "bristle" with elastic pre-sliding:

```
dz/dt = v * (1 - α(v,z) * z / z_ss(v))
f = σ₀ * z + σ₁ * dz/dt + σ₂ * v
```

Produces the most physically realistic pre-slip behavior (the "give" before the bow grabs) but at ~4x the cost of the simple bow table. Overkill for a synthesizer context.

#### Model E: Bilbao Exponential (Finite Difference)

Bilbao [B09] uses a smooth bell curve: `φ(v) = √(2a) * v * exp(-a*v² + 0.5)` with `a ≈ 100`. Requires Newton-Raphson iteration (up to 100 iterations/sample) — far too expensive for real-time plugin use but useful as a reference for accuracy comparison.

#### Friction Model Comparison

| Model | Hysteresis | State Vars | CPU Cost | Realism | Recommended Use |
|-------|-----------|------------|----------|---------|-----------------|
| STK Bow Table (A) | None | 0 | Very Low | Musical | **Primary model** |
| Friedlander (B) | Possible | 0 | Low | Good | Reference |
| Thermal (C) | Natural | 1 | Medium | Very Good | Future enhancement |
| Elasto-Plastic (D) | Natural | 1 | High | Excellent | Research only |
| Bilbao Exponential (E) | N/A | N/A | Very High | Reference-grade | Offline only |

### New DSP Components

#### `BowExciter` — Layer 2 (processors)
Location: `dsp/include/krate/dsp/processors/bow_exciter.h`

**Interface:** `float process(float feedbackVelocity)` — unified with `ImpactExciter` (which ignores the feedback parameter). This ensures the voice engine can switch exciter types without branching on interface shape. See "Unified Exciter Interface" below.

```
Input: continuous (runs every sample while note is held)
Params: pressure (0-1), speed (0-1), position (0-1)
Output: continuous excitation signal (force injected into resonator)

Per-sample algorithm (STK power-law bow table):
  1. bowAcceleration = attackAccel * envelope   // envelope controls acceleration, not velocity directly
  2. bowVelocity += bowAcceleration * dt         // integrate to get velocity (Guettler-aware)
  3. bowVelocity = clamp(bowVelocity, 0, maxVelocity * speed)  // speed param sets ceiling
  4. deltaV = bowVelocity - feedbackVelocity     // from resonator feedback
  5. slope = clamp(5.0 - 4.0 * pressure, 1.0, 10.0)  // cap to prevent float overflow in x^4
  6. offset = frictionJitter                     // slow LFO + noise (see "Rosin Character" below)
  7. x = |deltaV * slope + offset| + 0.75
  8. reflectionCoeff = clamp(1/(x*x*x*x), 0.01, 0.98)
  9. rawForce = deltaV * reflectionCoeff
  10. rawForce = bowHairLPF.tick(rawForce)        // one-pole LPF ~8 kHz (see "Bow Hair Width" below)
  11. rawForce *= positionImpedance               // position-dependent impedance scaling (see below)
  12. excitationForce = rawForce * energyGain     // energy-aware scaling (see below)
  13. Return excitationForce

Acceleration-based bow envelope (Guettler compliance):
  The Guettler diagram [G02] shows that attack quality depends on bow
  ACCELERATION, not just velocity. Using velocity = speed * envelope
  produces a fixed ramp shape regardless of attack time. Instead:
  - The ADSR envelope drives acceleration (its derivative controls jerk)
  - Velocity is the integral of acceleration
  - Short ADSR attack = high acceleration = snappy, potentially scratchy onset
  - Long ADSR attack = low acceleration = smooth, clean onset
  - This naturally produces the clean vs scratchy attack spectrum without
    separate transient modelling

Position-dependent impedance scaling:
  The impedance seen by the bow varies with position β. Near the bridge
  (small β) or near the nut (β near 1), impedance is high — the string
  is harder to excite. Near the centre, impedance is low — easier to excite.

  positionImpedance = 1.0 / max(β * (1 - β) * 4.0, 0.1)

  The factor of 4 normalises so that β=0.5 gives impedance=1.0.
  The max(..., 0.1) prevents singularities at β=0 and β=1.

  This gives:
  - β = 0.13 (normal): impedance ≈ 2.2 (moderate effort to excite)
  - β = 0.5 (centre): impedance = 1.0 (easiest to excite)
  - β = 0.05 (ponticello): impedance ≈ 5.3 (hard to excite, needs more pressure)

  Combined with the Schelleng boundaries, this makes position affect
  playability (not just spectrum), matching real bowed string behaviour.

Rosin character (friction jitter):
  Real rosin isn't perfectly uniform — surface irregularities and thermal
  micro-variations break the mathematical perfection of the stick-slip cycle.
  Without jitter, the model produces a "perfect" static loop that sounds
  distinctly digital. A small modulation of the bow table's offset parameter
  prevents this:

  frictionJitter = lfo(0.7 Hz, depth=0.003) + noise(highpassed @ 200 Hz, depth=0.001)

  The LFO provides slow drift (simulating gradual rosin redistribution).
  The noise provides per-sample variation (simulating rosin granularity).
  Both are internal — not user-exposed. Depths are small enough to be
  inaudible as pitch/timbre modulation but sufficient to break periodicity.

Bow hair width (excitation LPF):
  A real bow is a ribbon of hair (~10mm wide), not a mathematical point.
  A point-source excitation erroneously excites arbitrarily high frequencies
  that the finite bow width would physically damp. A one-pole LPF at ~8 kHz
  on the excitation force simulates this:

  bowHairLPF: one-pole lowpass, cutoff ≈ 8 kHz, applied to rawForce (step 10)

  This is always-on and internal (not a parameter). It tames the "line-y"
  quality of point-source excitation without audibly dulling the tone, since
  the string loss filter already handles high-frequency decay per round-trip.

Position effect (harmonic suppression):
  Bowing at position β = 1/n suppresses the nth harmonic and its multiples.
  Spectral envelope follows sin(n*π*β) / (n*π*β)  (sinc-like).
  - β ≈ 0.08 (near bridge): few harmonics suppressed, bright metallic tone
  - β ≈ 0.13 (normal position): modest suppression of high harmonics
  - β ≈ 0.5 (over fingerboard): strong suppression of even harmonics, flute-like
```

### New Parameters

| ID | Name | Range | Default | Description |
|----|------|-------|---------|-------------|
| `kBowPressureId` | Bow Pressure | 0.0–1.0 | 0.3 | Light (flautando/surface sound) → Heavy (ponticello/raucous). Maps to friction curve slope: 5.0 - 4.0*pressure |
| `kBowSpeedId` | Bow Speed | 0.0–1.0 | 0.5 | Slow (quiet, gentle attack) → Fast (loud, brighter, harder attack). Scales bow velocity: maxVelocity * speed |
| `kBowPositionId` | Bow Position | 0.0–1.0 | 0.13 | Bridge (0.0) → Fingerboard (1.0). Controls harmonic emphasis via node placement. Default 0.13 ≈ 1/8 string length — classical violin bow point |

**Playability mapping for pressure parameter:**
- 0.0–0.1: Surface sound territory (below Schelleng F_min — airy, glassy, musical effect)
- 0.1–0.8: Helmholtz region (clean, musical bowing — the sweet spot)
- 0.8–1.0: Near-raucous territory (above Schelleng F_max — gritty, intense, distortion-like)

**MIDI/MPE mapping recommendations:**
- Velocity → bow speed (maxVelocity) + ADSR attack time (faster velocity = harder attack)
- Aftertouch/MPE pressure → bow pressure (real-time timbral control)
- MPE slide → bow position (real-time harmonic emphasis control)

### Design Note: Bow ↔ Resonance Coupling

The bow is a **nonlinear two-port scattering junction** [MSW83, S86] — it must sit *inside* the resonator feedback loop, not outside it. The friction force depends on the string's current velocity, creating a one-sample feedback coupling.

#### With Waveguide Resonator

The bow junction **splits the waveguide delay line** into two segments (neck-side and bridge-side). Per-sample signal flow:

```
┌────────────────────┐                           ┌─────────────────────┐
│ neckDelay           │                           │ bridgeDelay          │
│ (nut side)          │←── incomingRight + force ──│ (bridge side)       │
│ length: L*(1-β)     │                           │ length: L*β          │
│ incomingLeft →      │──→ BOW JUNCTION ←─────────│ ← incomingRight     │
│                     │                           │ (via bridge filter)  │
└────────────────────┘                           └─────────────────────┘

Per sample:
  1. incomingLeft  = -neckDelay.lastOut()                    // from nut (inverted reflection)
  2. incomingRight = -bridgeFilter.tick(bridgeDelay.lastOut()) // from bridge (filtered + inverted)
  3. stringVelocity = incomingLeft + incomingRight            // sum = velocity at bow point
  4. force = bowExciter.process(stringVelocity)               // includes friction, impedance, energy scaling
  5. neckDelay.write(incomingRight + force)                   // propagate toward nut
  6. bridgeDelay.write(incomingLeft + force)                  // propagate toward bridge
  7. output = bridgeDelay.lastOut()                           // read from bridge end
```

Delay lengths: `bridgeDelay = totalDelay * β`, `neckDelay = totalDelay * (1 - β)`, where `β = bowPosition` and `totalDelay = sampleRate / frequency - filterDelayCompensation`.

#### With Modal Resonator (Elements-Style Bowed Mode Subset)

The modal resonator uses a **dedicated subset of 8 "bowed modes"** [MI] for the feedback loop — this is the only supported implementation. Summing all 96 modes per sample for feedback is both expensive and numerically problematic (the modal outputs are abstract amplitudes, not physical velocities — unit mismatch with the waveguide path). The bowed-mode subset operates in its own self-consistent domain, sidestepping this issue.

```
Architecture:
  - 8 bowed modes (bandpass filters with short delay lines), tuned to the
    first 8 mode frequencies, separate from the main 96-mode bank
  - These provide the feedback velocity to the bow table
  - The bow's excitation force feeds into ALL modes (up to 96),
    weighted by sin((n+1) * π * bowPosition) for harmonic selectivity

Per sample:
  1. feedbackVelocity = 0
     for n in bowedModes[0..7]:
       feedbackVelocity += bowedModeOutput[n]          // small fixed-cost sum
  2. excitation = bowExciter.process(feedbackVelocity)  // includes friction + energy scaling
  3. for n in allModes[0..numModes]:
       modeInput[n] += excitation * sin((n+1) * π * bowPosition)
  4. Update bowed modes with excitation (same input as main modes)
```

**Why 8 bowed modes (not fewer, not more):**
- Fewer than 4: coupling feels thin, hard to establish Helmholtz-like motion
- 8: covers the first 8 partials, sufficient for stable self-oscillation and timbral richness
- More than 8: diminishing returns — higher modes contribute little to the feedback and add CPU cost
- Elements uses exactly this number and it ships in production hardware

**CPU cost:** 8 bandpass filter evaluations + 8 delay reads per sample — comparable to a single biquad cascade, negligible next to the main 96-mode bank.

**Key difference from waveguide:** Bowed bars/plates **slip** during most of the motion cycle (unlike bowed strings which **stick** for most of the cycle). Self-excited modal vibrations are dominated by the first few modes, with limiting values for normal force and bow velocity defining a "playability space" [ESC00].

#### DC Blocking

A **DC blocker** is essential in the bow-resonator feedback loop. The friction nonlinearity can inject DC offset that accumulates in the feedback path, causing waveform drift and eventual clipping.

**Placement:** Strictly **after** the friction junction output but **before** the signal re-enters the delay lines. In the waveguide signal flow, this means filtering `force` between steps 9 and 10 (after bow table, before delay write). Placing it elsewhere (e.g., at the bridge termination) would not catch DC injected by the bow.

**Cutoff:** 20 Hz first-order high-pass (not 30 Hz). The lower cutoff protects the fundamental of low-tuned strings — cello C2 is 65 Hz, and an aggressive 30 Hz cutoff with its -3 dB rolloff would audibly thin notes in the 50-80 Hz range. At 20 Hz, the attenuation at 65 Hz is negligible (~0.1 dB for first-order).

#### Energy Control (Non-Negotiable)

The bow is an **active exciter** — unlike the impact exciter (which injects a finite energy pulse), the bow continuously injects energy into the resonator. Without energy-aware gain control, high pressure + high speed will cause runaway amplitude, especially with low-loss waveguide resonators.

**Integration with the existing energy model (Phase 3, "Energy as First-Class Observable"):**

The resonator already computes `controlEnergy_` (τ ≈ 5ms) and `perceptualEnergy_` (τ ≈ 30ms). The bow exciter uses the control energy to modulate its output gain:

```cpp
// Inside BowExciter::process(), after computing rawForce:
float currentEnergy = resonator->getControlEnergy();
float energyRatio = currentEnergy / targetEnergy_;    // targetEnergy_ set by velocity/speed
float energyGain = 1.0f;
if (energyRatio > 1.0f) {
    // Above target: attenuate force to prevent runaway
    // Soft knee: gradual reduction, not hard clamp
    energyGain = 1.0f / (1.0f + (energyRatio - 1.0f) * 2.0f);
}
excitationForce = rawForce * energyGain;
```

**Why this works:**
- `targetEnergy_` is set from MIDI velocity / bow speed at note-on — louder notes have a higher energy ceiling
- When the resonator is below target energy, the bow drives it up naturally (energyGain ≈ 1.0)
- When the resonator exceeds target, the bow backs off smoothly (no hard clip, no pumping)
- The 5ms time constant is fast enough to catch runaway but slow enough to not interfere with normal stick-slip dynamics
- This is the same control energy follower already used for retrigger choking in the impact exciter — no new infrastructure needed

**Without this:** Exploding amplitudes, inconsistent loudness across pitch range, unusable presets at high pressure. This is non-negotiable.

### Extended Techniques as Parameter Configurations

The three bow parameters naturally produce classic string techniques at their extremes:

| Technique | Position | Pressure | Speed | Sonic Character |
|-----------|----------|----------|-------|-----------------|
| **Normal (arco)** | 0.10–0.15 | 0.2–0.5 | 0.3–0.7 | Warm, full bowed string tone |
| **Sul ponticello** | 0.02–0.08 | 0.7–1.0 | 0.3–0.5 | Metallic, shimmery, prominent high harmonics |
| **Sul tasto** | 0.30–0.50 | 0.1–0.3 | 0.3–0.5 | Soft, flute-like, fundamental-heavy |
| **Flautando** | 0.35–0.50 | 0.05–0.15 | 0.2–0.4 | Pure, harmonic-sparse, ethereal |
| **Crunchy attack** | 0.10–0.15 | 0.8–1.0 | 0.8–1.0 | Aggressive onset, raucous bite |
| **Harmonics** | 0.50 (or 0.33, 0.25) | 0.1–0.2 | 0.3–0.5 | Even harmonics suppressed (at 0.5), fundamental suppressed |
| **Tremolo** | any | any | LFO on speed | Rapid amplitude variation |

### Unified Exciter Interface

All exciter types (Residual, Impact, Bow) must share a single `process` signature so the voice engine can switch between them without interface branching:

```cpp
// Shared interface for all exciters
float process(float feedbackVelocity);
```

- **Residual:** Ignores `feedbackVelocity`, returns next sample from residual buffer
- **Impact:** Ignores `feedbackVelocity`, returns current excitation pulse sample
- **Bow:** Uses `feedbackVelocity` for friction computation, returns excitation force

This unifies the voice integration code (currently divergent in Phase 2):

```cpp
// Voice engine (unified — no exciter-type branching)
float feedbackVelocity = resonator->getFeedbackVelocity();  // 0 for modal if not bowed
float excitation = exciter->process(feedbackVelocity);
float output = resonator->process(excitation);
```

**Retroactive change to Phase 2:** `ImpactExciter::process(float velocity)` (where `velocity` was MIDI velocity) should be refactored. MIDI velocity is a trigger-time parameter set via `trigger(float velocity)`, not a per-sample input. The per-sample `process(float feedbackVelocity)` then ignores the feedback and returns the current pulse sample. This is a minor change but must happen before Phase 4 implementation.

### Anti-Aliasing: Switchable Oversampling

The friction nonlinearity expands signal bandwidth. In a feedback loop, this bandwidth expansion compounds over time, causing aliasing [S10] — especially at high pressure, high stiffness, and on high-pitched notes. This is a **known problem** in nonlinear waveguide models, not a theoretical concern.

**Built-in mitigations (always active):**
- The power-law bow table is **smooth** (no discontinuities), which limits bandwidth expansion compared to piecewise-linear friction curves
- The string loss filter in the feedback loop naturally removes high-frequency energy each round-trip
- The DC blocker prevents sub-audible accumulation

**Switchable oversampling (design it in from the start):**

The bow-resonator loop must support a **2x oversampling path** toggled by a quality parameter:

```
1x mode (default): Normal sample rate. Sufficient for most settings.
2x mode (quality): Upsample input to bow junction, run friction + immediate
                   feedback at 2x rate, downsample output. Only the nonlinear
                   junction and its immediate neighbors need oversampling —
                   the full delay lines stay at 1x (adjust delay lengths by 2x).
```

**Why design it in now:** Retrofitting oversampling into a feedback loop is architecturally painful — the delay line lengths, filter coefficients, and energy scaling all change. If the interface supports it from day one, enabling it later is a parameter flip.

**When to enable 2x:** Metallic aliasing on notes above ~1 kHz at pressure > 0.7. Evaluate empirically during implementation.

### Success Criteria

- [ ] Sustained notes have organic micro-variation (not static)
- [ ] Pressure sweep transitions smoothly from airy (surface sound) through clean (Helmholtz) to gritty (raucous)
- [ ] Bow + Waveguide produces convincing cello/violin-like sustain
- [ ] Bow + Modal (8 bowed modes) produces interesting "bowed vibraphone" textures
- [ ] Bow position audibly changes harmonic emphasis (bright near bridge, dark near fingerboard)
- [ ] Bow position affects playability (near-bridge requires higher pressure to sustain, matching Schelleng)
- [ ] Attack transients range from clean to scratchy depending on speed/pressure/acceleration
- [ ] No DC drift or instability at any parameter combination
- [ ] No runaway amplitude at any pressure/speed combination (energy control verified)
- [ ] Consistent loudness across pitch range (energy-aware scaling)
- [ ] Unified exciter interface: voice engine uses `process(feedbackVelocity)` for all exciter types
- [ ] 2x oversampling path functional (switchable, not necessarily default)
- [ ] CPU cost reasonable — friction model is per-sample but simple arithmetic (~4 muls + 1 fabs + 1 clamp + energy check)

### References

| Tag | Citation | Relevance |
|-----|----------|-----------|
| [H1863] | Helmholtz, "On the Sensations of Tone", 1863 | Helmholtz corner motion pattern in bowed strings |
| [F53] | Friedlander, "On the oscillations of a bowed string", Cambridge Phil. Soc. Proc. 49, 1953 | Original graphical solution for bow-string intersection problem |
| [S73] | Schelleng, "The bowed string and the player", JASA 53(1):26-41, 1973 | Playability diagram: bow force vs position boundaries |
| [MSW83] | McIntyre, Schumacher & Woodhouse, "On the oscillations of musical instruments", JASA 74(5):1325-1345, 1983 | Foundational paper for reflection function + nonlinear excitation framework |
| [S86] | Smith, "Efficient simulation of the reed-bore and bow-string mechanisms", Proc. ICMC, 1986 | Digital waveguide implementation of bowed strings; introduced bow table concept |
| [CS99] | Cook & Scavone, "The Synthesis ToolKit (STK)", Proc. ICMC, 1999. Code: https://github.com/thestk/stk | Reference real-time implementation (BowTable.h, Bowed.cpp) |
| [SW00] | Smith & Woodhouse, "The tribology of rosin", J. Mechanics & Physics of Solids 48(8):1633-1681, 2000 | Thermal friction model — friction depends on rosin temperature, not just velocity |
| [ESC00] | Essl, Serafin & Cook, "Measurements and efficient simulations of bowed bars", JASA, 2000 | Bowed bar/metal modal synthesis — playability space, limiting force/velocity values |
| [D02] | Dupont, Hayward, Armstrong & Altpeter, "Single state elasto-plastic friction models", IEEE Trans. AC 47(5):787-792, 2002 | Bristle/elasto-plastic friction for pre-sliding deformation |
| [G02] | Guettler, "On the creation of the Helmholtz motion in bowed strings", Acustica 88, 2002 | Guettler diagram: bow acceleration vs force for attack transient quality |
| [W03] | Woodhouse, "Bowed string simulation using a thermal friction model", Acta Acustica united with Acustica 89(2), 2003 | Applied thermal friction to digital simulation; improved transient realism |
| [S04] | Serafin, "The Sound of Friction: Real-time Models, Playability, and Musical Applications", PhD thesis, Stanford/CCRMA, 2004 | Extended bowed string models with thermodynamic friction and bow-hair compliance |
| [B09] | Bilbao, "Numerical Sound Synthesis", Wiley, 2009 | Finite difference bowed string; exponential friction model |
| [S10] | Smith, "Physical Audio Signal Processing", W3K Publishing, 2010. Online: https://ccrma.stanford.edu/~jos/pasp/ | Canonical textbook for waveguide synthesis including bowed strings (Ch. 9) |
| [DB16] | Desvages & Bilbao, "Two-polarisation physical model of bowed strings", Applied Sciences 6(5):135, 2016. https://www.mdpi.com/2076-3417/6/5/135 | State-of-art FD bowed string with two polarizations |
| [MI] | Gillet (Mutable Instruments), Elements resonator. https://github.com/pichenettes/eurorack/blob/master/elements/dsp/resonator.cc | Reference implementation of bow exciter coupled to modal resonator bank |
| [FL] | GRAME, Faust physmodels.lib. https://faustlibraries.grame.fr/libs/physmodels/ | Functional implementation of violin bow model (same math as STK) |
| [FR98] | Fletcher & Rossing, "The Physics of Musical Instruments", Springer, 1998 | Physical acoustics of bowed strings, Helmholtz motion |
| [C02] | Cook, "Real Sound Synthesis for Interactive Applications", A.K. Peters, 2002 | Practical guide to implementing physical models including bowed strings |

---

## Phase 5: Body Resonance

**Goal:** Add the resonant character of an instrument body — guitar, violin, marimba resonator tube — as a post-resonance coloring stage.

**Sonic character:** Adds warmth, depth, and the "in a box" quality that distinguishes a raw string from a guitar. Short reverberant coloring (T60 ≈ 0.02–0.3 s for wood, 0.5–10 s for metal), not room reverb. The body acts as a multi-resonant bandpass filter whose transfer function can be decomposed as a sum of parallel second-order resonant modes.

### Physical Background

Instrument bodies exhibit discrete **resonant modes** at low frequencies that transition into a dense, statistically diffuse response at higher frequencies. The perceptually dominant features are:

**Guitar body** (Elejabarrieta et al., "Coupled Modes of the Resonance Box"; Dan Russell, Penn State modal analysis):
- **Helmholtz (A0) air mode**: ~90–110 Hz (dreadnought), ~100–120 Hz (classical). Q ≈ 10–30. Caused by air oscillating through the soundhole: f_H = (c / 2π) √(S / (V · L_eff)).
- **First coupled top-plate mode T(1,1)**: ~180–220 Hz. Q ≈ 15–50. Strongest radiator.
- **Higher plate modes**: 220–800+ Hz, progressively shorter T60 (~0.02–0.08 s). 11 significant modes below 750 Hz identified in folk guitar measurements.
- Overall body IR energy concentrated in first ~50–200 ms.

**Violin body** (Gough, "Violin Acoustics," Acoustics Today 2016; Woodhouse, Euphonics):
- **A0 (Helmholtz)**: ~275 Hz. Q ≈ 20–30.
- **B1− (first corpus bending)**: ~450–480 Hz. Q ≈ 30–50. Strong radiator.
- **B1+ (second corpus bending)**: ~530–570 Hz. Q ≈ 30–50. Strongest radiator.
- **Bridge hill**: broad peak at ~2–3 kHz, bandwidth ~1 kHz (effectively Q ≈ 5–15). Caused by coupling of in-plane bridge resonance with body. Critical for violin "brilliance."
- Body modes have Q ≈ 20–60; lowest modes can ring up to ~0.2 s.

**Marimba resonator tube** (Yamaha musical instrument guide; LaFavre resonator data):
- Quarter-wavelength closed-end cylindrical resonator: L = c/(4f) − 0.61r (end correction).
- Reinforces bar fundamental only (tube harmonics at 3f, 5f don't align with bar partials tuned to 4:1 ratio).
- T60 ≈ 0.2–0.5 s. Q ≈ 10–30 for the tube resonance.

**Material-dependent damping** — the key physical distinction between wood and metal:
- **Wood** (spruce, maple, rosewood): internal friction (loss tangent tan δ ≈ 0.005–0.02). Higher frequencies decay 3–10× faster than low frequencies. T60 ratio (low:high) ≈ 3:1 to 10:1.
- **Metal** (steel, brass, aluminum): internal friction tan δ ≈ 0.0001–0.001 (10–100× lower than wood). Nearly frequency-independent damping. T60 ratio ≈ 1:1 to 2:1. Much longer overall T60.

### Architecture Choice: Hybrid Modal + FDN

**Why not commuted synthesis?** Smith & Välimäki's commuted waveguide synthesis (ICMC 1995) moves the body IR into the excitation signal, replacing the body filter with a wavetable lookup — dramatically cheaper for polyphonic playback of static body types. However, commuted synthesis requires pre-recorded/pre-computed body IRs and cannot morph body character in real-time. Since Innexus needs real-time parametric control of body size and material, commuted synthesis is not suitable here.

**Why not a waveguide mesh?** 2D/3D waveguide meshes model physical geometry directly but suffer from frequency- and direction-dependent dispersion (Smith, PASP), require O(N²) or O(N³) computation for N nodes per side, and are difficult to parameterize. They are primarily research/analysis tools.

**Chosen approach — hybrid modal bank + small FDN:**

```
input
  ↓
[coupling filter] (1-2 biquad EQ, material-dependent)
  ↓
 ┌────────────────────┬─────────────────────┐
 │ modal bank         │ 4-line FDN          │
 │ (signature modes)  │ (dense tail)        │
 │ dominates low freq │ dominates mid/high  │
 └────────┬───────────┴──────────┬──────────┘
          ↓                      ↓
       frequency-weighted sum
          ↓
    radiation HPF (12 dB/oct, ~0.7× lowest mode freq)
          ↓
    energy normalization (passive: ||out|| ≤ ||in||)
          ↓
       dry/wet mix
```

**Stage 0: Coupling filter** (1–2 biquad EQ before the modal+FDN split). Real bodies don't respond flat — the bridge/soundpost acts as a frequency-dependent coupler. This filter shapes the input spectrum before it enters the resonant stages:
- Wood bodies: slight low-mid emphasis (~100–400 Hz) modelling bridge admittance
- Metal bodies: broader, flatter response
- Violin-type: bridge hill pre-emphasis (~2–3 kHz bump)
- Controlled by material parameter; cheap (1–2 biquads, ~15 FLOPS/sample).

**Stage 1: Parametric modal resonator bank** (6–12 second-order biquad filters) for the low-frequency **signature modes** that define the instrument's tonal character (Helmholtz, plate modes, bridge hill). These provide precise frequency control and are the most perceptually important features. Design via impulse-invariant transform (preferred over bilinear for modal synthesis — avoids frequency warping; Smith, CCRMA):
   ```
   θ = 2π·freq / sampleRate
   R = exp(−π · freq / (Q · sampleRate))
   a1 = −2·R·cos(θ),  a2 = R²
   b0 = 1 − R,  b1 = 0,  b2 = −(1 − R)
   ```
   Each biquad costs ~7–11 FLOPS/sample. At 12 modes: ~130 FLOPS/sample (~0.01% CPU at 44.1 kHz).

   **Mode frequency control via interpolated reference presets** (not physics scaling):
   - Store 3 reference modal sets: small (violin-scale), medium (guitar-scale), large (cello-scale)
   - Each set: array of {frequency, gain, Q} tuples for 6–12 modes
   - Interpolate log-linearly between adjacent sets: `f = exp(lerp(log(f_small), log(f_large), size))`
   - Log-linear interpolation preserves musical interval spacing and avoids the detuning artifacts that physics-based scaling (1/size²) would produce — real instrument families don't scale uniformly across dimensions
   - Gains and Q factors interpolated linearly between reference sets

   **Interpolation method — pole/zero domain, not coefficient domain:**
   - Interpolate (R, θ) directly, then recompute a1 = −2R·cos(θ), a2 = R². Since R < 1 always holds for decaying modes, any interpolated (R, θ) produces stable coefficients. This avoids the instability and "chirp" artifacts that arise from interpolating raw biquad coefficients (a1, a2) across large frequency jumps.
   - Do NOT cross-fade parallel banks (doubles CPU cost for marginal benefit).

   **Reference preset design guidelines:**
   - **A0/T1 coupling (guitar preset):** The Helmholtz (A0) and first top-plate (T1) modes are coupled oscillators exhibiting frequency repulsion. Encode this as an anti-phase gain relationship between adjacent modes with a characteristic dip at ~110 Hz. This creates the "hollow woodiness" of real acoustic guitars rather than an unnatural volume spike at the overlap frequency.
   - **Bridge hill (violin preset):** Include a broad, low-Q resonance (Q ≈ 5–15) at ~2–3 kHz. This is what gives violins their projection and brilliance (Jansson).
   - **Sub-Helmholtz rolloff:** Each preset's lowest mode gain should taper to zero below the Helmholtz frequency — real bodies cannot radiate at those frequencies.

**Stage 2: Small FDN** (4 delay lines) for the dense mid/high-frequency response that fills in between the discrete low modes. This is NOT room reverb — delay lengths are very short (body-scale), producing colored resonance rather than spaciousness.

   **FDN design (from Smith, PASP; Jot & Chaigne, AES 1991; Välimäki et al., IEEE TASLP 2012):**
   - **4 delay lines** with mutually coprime lengths, biased short to stay firmly in "body" territory and avoid early-reflection/room character. Range: **8–80 samples at 44.1 kHz** (~0.2–1.8 ms). Example prime cluster: [11, 17, 23, 31] samples. Scaled by size parameter (larger body = longer delays within this range).
   - **Hadamard mixing matrix** (4×4): H₄ = (1/2)·[[1,1,1,1],[1,−1,1,−1],[1,1,−1,−1],[1,−1,−1,1]]. Optimal mixing (maximum determinant), requires no multiplies when N is a power of 4 (Smith, PASP). Guarantees lossless mixing (orthogonal, spectral norm = 1).
   - **First-order absorption filters** per delay line for frequency-dependent decay (Smith, PASP, "First-Order Delay Filter Design"):
     ```
     H_i(z) = g_i / (1 − p_i · z⁻¹)
     p_i = (R₀^Mᵢ − Rπ^Mᵢ) / (R₀^Mᵢ + Rπ^Mᵢ)
     g_i = 2·R₀^Mᵢ·Rπ^Mᵢ / (R₀^Mᵢ + Rπ^Mᵢ)
     ```
     where R₀, Rπ are per-sample decay rates at DC and Nyquist, derived from T60(DC) and T60(Nyquist). Material parameter controls the Rπ/R₀ ratio: wood = low Rπ relative to R₀ ("fuzzy," strong HF damping), metal = Rπ close to R₀ ("glassy," preserved HF). This is the primary mechanism for material character in the FDN — no feedback gain boost is needed (which would violate passivity).
   - **Hard RT60 cap on FDN:** max T60 = 300 ms (wood) / 2 s (metal). This structurally prevents reverb-like behavior regardless of parameter settings.
   - **No allpass diffusion needed** — the 4-line FDN with Hadamard mixing provides sufficient density for body-scale cavities. Allpass stages would add latency without benefit at these short delay lengths.

**Stage 3: Frequency-weighted sum.** Modal bank and FDN outputs are combined with frequency-dependent weighting:
- Modal bank dominates below ~500 Hz (where discrete body modes are perceptually distinct)
- FDN dominates above ~500 Hz (where real body response becomes dense and statistical)
- Implementation: simple first-order crossover (6 dB/oct) or fixed gain split. The crossover frequency scales with body size (smaller body → higher crossover).

**Stage 4: Energy normalization.** The body resonator must be **passive** — output energy ≤ input energy — to maintain consistency with the energy models in earlier phases (exciter energy tracking, resonator passivity). Implementation:
- Normalize modal bank gains so that sum of peak gains ≤ 1.0
- FDN absorption filters guarantee gain ≤ 1 at all frequencies (structural passivity from orthogonal mixing + absorptive feedback)
- Optional auto-gain compensation: measure short-term RMS of wet output and scale to match dry input level, with a ceiling of 1.0

This hybrid approach follows the "body-model factoring" principle (Karjalainen & Smith, ICMC 1996): extract the least-damped (high-Q) resonances as parametric filters for real-time control; let the FDN handle the remaining dense, heavily-damped response.

**CPU estimate:** coupling filter (~15 FLOPS/sample) + 12 biquads (~130 FLOPS/sample) + 4-line FDN (~100 FLOPS/sample) + crossover (~10 FLOPS/sample) + radiation HPF (~10 FLOPS/sample) ≈ 265 FLOPS/sample total. At 44.1 kHz: ~12M FLOPS/s, well under 0.5% single-core CPU.

### New DSP Components

#### `BodyResonance` — Layer 2 (processors)
Location: `dsp/include/krate/dsp/processors/body_resonance.h`

```
Input: mono signal (from modal/waveguide output)
Params: size (0-1), material (0-1), mix (0-1)
Output: mono colored signal

Signal flow:
  input → coupling filter → ┬─ modal bank ─┬→ freq-weighted sum → radiation HPF → energy norm → mix
                             └─ FDN ────────┘

Internal architecture:

  0. Coupling filter (1-2 biquads):
     - Pre-shapes input spectrum before resonant stages
     - Wood: low-mid emphasis (~100-400 Hz, models bridge admittance)
     - Metal: broader, flatter response
     - Parameterized by material; coefficients updated at control rate

  1. Parametric modal bank (6-12 parallel biquad resonators):
     - 3 reference modal sets stored: small (violin), medium (guitar), large (cello)
     - Each set: array of {freq, gain, Q} tuples
     - Size parameter interpolates log-linearly between sets:
       f = exp(lerp(log(f_small), log(f_large), size))
     - Size 0.0: small body (violin-scale, modes at 275-570+ Hz)
     - Size 0.5: medium body (guitar-scale, modes at 90-400+ Hz)
     - Size 1.0: large body (cello-scale, modes at 60-250+ Hz)
     - Modal gains normalized: sum of peak gains ≤ 1.0 (passivity)

  2. Small FDN (4 delay lines):
     - Hadamard 4x4 mixing matrix (lossless, multiply-free)
     - Delay lengths: 8-80 samples at 44.1 kHz (body-scale, NOT room-scale)
     - Example prime cluster: [11, 17, 23, 31] samples
     - Scaled by size parameter (larger body = longer delays within range)
     - Per-line first-order absorption filter parameterized by T60(DC), T60(Nyquist)
     - Hard RT60 cap: 300 ms (wood) / 2 s (metal)

  3. Frequency-weighted sum:
     - Modal bank dominates below crossover (~500 Hz, scales with size)
     - FDN dominates above crossover
     - Simple first-order crossover (6 dB/oct) or fixed gain split

  4. Radiation high-pass filter (12 dB/oct, 1 biquad):
     - Real instrument bodies are open systems that cannot radiate below
       their Helmholtz resonance. This HPF prevents physically impossible
       sub-rumble, especially audible when small body models process low notes.
     - Cutoff: ~0.7× the frequency of the lowest active mode (A0/Helmholtz)
     - Scales automatically with size parameter (smaller body = higher cutoff)
     - Cheap: 1 biquad, ~10 FLOPS/sample

  5. Material control (applied to coupling filter, modal bank, AND FDN):
     - Wood (material=0): steep high-frequency rolloff
       T60 ratio 5:1+ (low:high), overall T60 ~0.02-0.3 s
       Higher Q modes (Q ≈ 15-50), tan δ ≈ 0.005-0.02
     - Metal (material=1): gentle rolloff
       T60 ratio ~1.5:1, overall T60 ~0.5-2 s
       Higher Q modes (Q ≈ 100-1000), tan δ ≈ 0.0001-0.001
     - Intermediate values crossfade between wood and metal decay profiles

  6. Energy normalization:
     - Body resonator is PASSIVE: ||output|| ≤ ||input||
     - Modal gains normalized (sum of peaks ≤ 1.0)
     - FDN structurally passive (orthogonal mixing + absorptive filters)
     - Optional auto-gain: match wet RMS to dry RMS, ceiling 1.0

  Output: mix * body_signal + (1-mix) * dry_input
```

### New Parameters

| ID | Name | Range | Default | Description |
|----|------|-------|---------|-------------|
| `kBodySizeId` | Body Size | 0.0–1.0 | 0.5 | Interpolates between reference body modal sets. 0 = small (violin-scale, modes ~275+ Hz), 0.5 = medium (guitar-scale, modes ~90+ Hz), 1.0 = large (cello/bass-scale, modes ~60+ Hz). Log-linear interpolation preserves musical interval spacing. Also scales FDN delay lengths (8–80 samples) and crossover frequency. |
| `kBodyMaterialId` | Material | 0.0–1.0 | 0.5 | Controls frequency-dependent damping profile and coupling filter shape. 0 = wood character (steep HF rolloff, T60 ratio 5:1+, short overall decay 0.02–0.3 s, low-mid coupling emphasis), 1.0 = metal character (flat damping, T60 ratio ~1.5:1, longer decay 0.5–2 s, flat coupling). Affects absorption filters in coupling filter, modal bank, and FDN. |
| `kBodyMixId` | Body Mix | 0.0–1.0 | 0.0 | Dry/wet blend of body resonance. 0 = bypass (no body coloring), 1.0 = fully colored. |

### Design Notes

**Why post-resonance and not commuted?** In Innexus, body parameters are user-controllable and can change in real-time. Commuted synthesis (Smith & Välimäki, 1995) would require pre-baked body IRs convolved into the excitation, preventing real-time morphing of size/material. The post-resonance FDN+modal approach trades some efficiency for full parametric control. Since this is a per-voice processor (not polyphonic convolution), the CPU cost is acceptable.

**Coupling model:** One-way coupling only (string/plate → body). Two-way coupling (body → string) is needed primarily for wolf-note simulation in bowed strings (Schleske) where the bridge impedance approaches the string impedance. Since Innexus targets general-purpose physical modelling color rather than specific bowed-instrument pathologies, one-way coupling is sufficient and dramatically simpler.

**Parameter smoothing (critical for artifact-free operation):**
- **Modal bank frequencies:** Interpolate in the pole/zero domain (R, θ), not the coefficient domain (a1, a2). Compute target (R, θ) at control rate, exponentially interpolate toward them each block, then derive coefficients. This is always stable (any R < 1 produces valid coefficients) and avoids the chirps/instability that raw coefficient interpolation causes across large frequency jumps.
- **FDN delay lengths:** Use fractional delay interpolation (same approach as waveguide string component) when size changes. Without this, delay length jumps cause audible pitch artifacts in the FDN resonances.
- **Material/coupling filter:** Absorption filter coefficients and coupling EQ interpolated smoothly at control rate. Less critical than modal frequencies (damping changes are perceptually forgiving).
- **Mix:** Simple linear ramp per block (standard parameter smoothing).

**Energy model consistency:** The body resonator is passive by construction — it cannot add energy to the signal. This is enforced structurally:
- Modal bank: parallel bandpass filters with normalized gains (sum of peak gains ≤ 1.0). No feedback path exists.
- FDN: orthogonal mixing matrix (Hadamard) preserves energy; absorption filters strictly attenuate (|H(e^jω)| ≤ 1 at all frequencies). Combined: energy decays monotonically.
- Coupling filter: unity-gain EQ (reshapes spectrum, doesn't add energy).
- This maintains consistency with the passivity constraints in earlier phases (exciter energy tracking, resonator decay models).

### Success Criteria

- [ ] Adds audible "body" coloring without sounding like room reverb (FDN RT60 hard-capped at 300 ms for wood, 2 s for metal)
- [ ] Size parameter changes perceived instrument scale: small sounds "violin-like" (modes above ~250 Hz), large sounds "cello-like" (modes below ~100 Hz)
- [ ] Material parameter spans wood → metal convincingly: wood has warm, quickly-damped HF; metal rings longer with preserved brightness
- [ ] No feedback instability at any parameter combination (FDN mixing matrix is orthogonal; all absorption filter gains ≤ 1; modal bank is purely parallel with no feedback)
- [ ] Energy passive: ||body_output|| ≤ ||input|| at all parameter settings (no artificial energy boost)
- [ ] CPU cost < 0.5% single core per voice at 44.1 kHz (target ~265 FLOPS/sample)
- [ ] Body mix at 0% produces bit-identical output to input (true bypass)
- [ ] No zipper noise or pitch artifacts when size/material parameters change during sustained notes
- [ ] No metallic ringing in wood mode (material=0): FDN delay line fundamentals must sit above the modal/FDN crossover frequency, preventing pitched FDN resonances from leaking into the modal range
- [ ] No sub-rumble on small body models: radiation HPF prevents energy below ~0.7× lowest mode frequency

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
822  kImpactPositionId       // Phase 2
823  kImpactBrightnessId     // Phase 2 (trim: offsets hardness-derived cutoff)

830  kWaveguideStiffnessId   // Phase 3
831  kWaveguidePickPositionId // Phase 3

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
