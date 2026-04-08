# Research: Membrum Phase 1 -- Plugin Scaffold + Single Voice

**Date**: 2026-04-08

## R-001: Plugin Scaffold Pattern (Gradus Reference)

**Decision**: Follow Gradus plugin scaffold pattern exactly -- it is the newest instrument plugin (`aumu`) in the monorepo.

**Rationale**: Gradus was added most recently and represents the current standard for instrument plugins. Key patterns extracted:
- `CMakeLists.txt`: reads `version.json`, generates `version.h` via `configure_file`, uses `smtg_add_vst3plugin`, links `KrateDSP` + `KratePluginsShared`
- `entry.cpp`: uses `BEGIN_FACTORY_DEF` / `DEF_CLASS2` / `END_FACTORY` pattern with `kDistributable` flag
- `plugin_ids.h`: static `FUID` constants + `kSubCategories` constexpr + `enum ParameterIds`
- AU config: `au-info.plist` (type `aumu`, 0-in/2-out) + `audiounitconfig.h` (kSupportedNumChannels `0022`)
- Test CMakeLists: compiles processor/controller sources + SDK hosting helpers + Catch2

**Alternatives considered**: Could use Ruinae or Innexus as reference, but Gradus is simpler (no complex engine) and newest.

## R-002: DSP Component API Contracts

**Decision**: Wire existing KrateDSP components directly via their public APIs.

**Rationale**: All three components exist, are tested, and have clear APIs:

### ImpactExciter (Layer 2 processor)
- `prepare(double sampleRate, uint32_t voiceId)` -- must be called before use
- `trigger(float velocity, float hardness, float mass, float brightness, float position, float f0)` -- starts excitation
- `process(float feedbackVelocity) -> float` -- returns one sample; pass 0.0f when not using bow feedback
- `isActive() -> bool` -- true while pulse/bounce is still producing output
- Size: 280 bytes

### ModalResonatorBank (Layer 2 processor, implements IResonator)
- `prepare(double sampleRate)` -- initialize coefficients
- `setModes(const float* frequencies, const float* amplitudes, int numPartials, float decayTime, float brightness, float stretch, float scatter)` -- set up modes on note-on (clears filter states)
- `updateModes(...)` -- same signature, for parameter changes during sustain (does NOT clear states)
- `processSample(float excitation) -> float` -- process one exciter sample through modal bank
- `processSample(float excitation, float decayScale) -> float` -- with per-sample decay scaling
- `reset()` -- clear all states
- kMaxModes = 96, but we only use 16
- Size: 3872 bytes (32-byte aligned for SIMD)

### ADSREnvelope (Layer 1 primitive)
- `prepare(double sampleRate)` -- initialize
- `gate(bool on)` -- note-on/off; supports retrigger modes
- `setAttack/setDecay/setSustain/setRelease(float ms/level)` -- configure times
- `setVelocity(float velocity)` -- 0.0-1.0 for velocity scaling
- `setVelocityScaling(bool enabled)` -- enable velocity-to-peak mapping
- `process() -> float` -- returns envelope value 0.0-1.0
- `isActive() -> bool` -- false when envelope is idle
- `getStage() -> ADSRStage` -- current stage enum
- Size: 3172 bytes

### Damping Model (computeModeCoefficients internals)
The ModalResonatorBank uses Chaigne-Lambourg damping:
- `b1 = 1.0f / decayTime` -- base damping rate (higher = faster decay)
- `b3 = (1.0f - brightness) * kMaxB3` -- frequency-dependent damping (HF decay)
- Per-mode: `decayRate_k = b1 + b3 * f_w^2`, then `R_k = exp(-decayRate_k / sampleRate)`

For Membrum, the spec's Material/Decay mapping needs to translate to `decayTime` and `brightness`:
- **Material** controls b3 (frequency-dependent damping) + stiffness (stretch) + baseline b1
- **Decay** controls global decay time scaling
- These map to `setModes(freqs, amps, 16, decayTime, brightness, stretch, 0.0f)`

## R-003: DrumVoice Architecture

**Decision**: Create a `DrumVoice` class in `plugins/membrum/src/dsp/drum_voice.h` that composes the three DSP components.

**Rationale**: The voice is plugin-local (not shared DSP library material) because it encodes Membrum-specific parameter mapping logic. Phase 2 will swap exciter/body types, so DrumVoice should use direct composition (not templates or polymorphism in Phase 1) but keep the exciter/body as separate members for future extraction.

**Signal chain**: `ImpactExciter -> ModalResonatorBank (16 modes) -> ADSREnvelope -> stereo output`

**Parameter mapping in DrumVoice**:
- Material -> (b3 via brightness, stretch, baseline decay profile)
- Size -> fundamental frequency via `f0 = 500 * pow(0.1, size)`
- Decay -> decay time multiplier on material's baseline
- Strike Position -> per-mode amplitude weights via Bessel function values
- Level -> post-envelope gain

**Key design**: The 16 Bessel ratios are constexpr array. On note-on, frequencies = f0 * ratios[k], amplitudes = Bessel function evaluation at strike position.

## R-004: Bessel Mode Amplitudes (Strike Position)

**Decision**: Use precomputed J_m(j_mn * r/a) values for the 16 membrane modes.

**Rationale**: The spec (FR-035) requires `A_mn ~ J_m(j_mn * r/a)` where r/a goes from 0.0 (center) to ~0.9 (edge). For Phase 1, we can use a fast approximation or lookup table for the Bessel function values at the 16 mode positions.

The 16 modes correspond to specific (m,n) pairs of the circular membrane. The frequency ratios (j_mn / j_01) are given in FR-031. Each mode's amplitude depends on its Bessel order m and the strike position r/a.

For center strike (r/a=0): only m=0 modes have non-zero J_0(0)=1. All m>0 modes are zero.
For edge strike (r/a~0.9): higher-order modes are excited.

**Implementation approach**: Store the Bessel orders m and the j_mn values for each mode. At each strike position, evaluate J_m(j_mn * r/a) using a polynomial approximation of the Bessel function. This gives the excitation amplitude per mode.

The (m,n) assignments for the 16 ratios (from standard circular membrane modes):
| Index | Ratio | (m,n) | j_mn |
|-------|-------|-------|------|
| 0 | 1.000 | (0,1) | 2.405 |
| 1 | 1.593 | (1,1) | 3.832 |
| 2 | 2.136 | (2,1) | 5.136 |
| 3 | 2.296 | (0,2) | 5.520 |
| 4 | 2.653 | (3,1) | 6.380 |
| 5 | 2.918 | (1,2) | 7.016 |
| 6 | 3.156 | (4,1) | 7.588 |
| 7 | 3.501 | (2,2) | 8.417 |
| 8 | 3.600 | (0,3) | 8.654 |
| 9 | 3.649 | (5,1) | 8.772 |
| 10 | 4.060 | (3,2) | 9.761 |
| 11 | 4.231 | (1,3) | 10.173 |
| 12 | 4.602 | (6,1) | 11.065 |
| 13 | 4.832 | (4,2) | 11.620 |
| 14 | 4.903 | (2,3) | 11.791 |
| 15 | 5.131 | (0,4) | 12.336 |

For J_m evaluation at arbitrary r/a, a reasonable Phase 1 approach is to use the identity that for center strike only m=0 modes are excited, and linearly interpolate mode amplitudes based on strike position. A more accurate approach uses polynomial Bessel approximations (3rd-5th order accurate).

## R-005: Material-to-Damping Mapping

**Decision**: Map Material parameter to b3 (brightness in ModalResonatorBank) and stretch, with a baseline decay profile.

**Rationale**: The ModalResonatorBank's `computeModeCoefficients` uses:
- `brightness` parameter: controls b3 = (1 - brightness) * kMaxB3. So brightness=1.0 means b3=0 (even decay = metallic), brightness=0.0 means b3=max (steep HF rolloff = woody).
- `stretch` parameter: controls inharmonicity B = stretch^2 * 0.001

Material mapping (0.0=woody, 1.0=metallic):
- `brightness = material` (direct mapping: woody=0 has max HF damping, metallic=1 has none)
- `stretch = material * 0.3` (metallic end has slight inharmonicity from stiffness)
- Base decay time profile varies by material: woody has shorter inherent decay, metallic longer

## R-006: CI Workflow Pattern

**Decision**: Add Membrum to CI following the exact Gradus pattern in all three platform jobs.

**Rationale**: The CI uses a `detect-changes` job with per-plugin path filters and conditional build/test/artifact steps. For Membrum, we need:
1. Add `membrum` output to `detect-changes` job
2. Add `plugins/membrum/**` filter
3. Add `membrum` to the `for p in ...` loop for workflow_call
4. Add build+test+artifact steps for each platform (Windows, macOS, Linux)
5. Add AU validation step on macOS: `auval -v aumu Mbrm KrAt`
6. Add AUv3 bundle verification on macOS
7. Add `plugins/membrum/CMakeLists.txt` to FetchContent cache keys

## R-007: State Versioning Pattern

**Decision**: Use versioned binary state format matching Gradus pattern.

**Rationale**: The spec requires forward-compatible state (FR-016). Gradus uses `kCurrentStateVersion` (currently 2) written as int32 before parameter data. Unknown parameters on load are skipped gracefully.

For Membrum Phase 1: version 1, save/load the 5 parameter values as float64 (matching VST3 normalized precision).

## R-008: FUID Generation

**Decision**: Generate unique FUIDs for Membrum processor and controller.

**Rationale**: FUIDs must be globally unique and never change after publication. Following the Gradus pattern of 4x 32-bit hex values. Will use pre-generated UUIDs to avoid collision.

Processor: `(0x4D656D62, 0x72756D50, 0x726F6331, 0x00000136)` -- "MembrumProc1" + spec number
Controller: `(0x4D656D62, 0x72756D43, 0x74726C31, 0x00000136)` -- "MembrumCtrl1" + spec number
