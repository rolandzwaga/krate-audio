# Implementation Plan: Membrum Phase 2 — 5 Exciter Types + 5 Body Models (Swap-In Architecture)

**Branch**: `137-membrum-phase2-exciters-bodies` | **Date**: 2026-04-10 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/137-membrum-phase2-exciters-bodies/spec.md`

## Summary

Refactor Phase 1's hardcoded `ImpactExciter + ModalResonatorBank` `Membrum::DrumVoice` into a swap-in architecture supporting 6 exciter types × 6 body models (36 runtime combinations) with a Tone Shaper (SVF filter, Drive, Wavefolder, Pitch Envelope) and Unnatural Zone (Mode Stretch, Decay Skew, Mode Inject, Nonlinear Coupling, Material Morph). All new DSP is plugin-local — the shared `dsp/` library is not modified. Exciter and body dispatch is `std::variant` + `std::visit` (or index-based `switch`), never virtual. The Pitch Envelope is absolute-Hz (20–2000 Hz, default 160→50 Hz). Body-model switches while sounding are deferred to the next note-on. 1.25% single-voice CPU budget across all 144 parameter combinations (6×6×2×2). Scalar-only — SIMD is deferred to Phase 3.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang/Xcode 13+, GCC 10+)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (internal, Phase 2 uses ~20 existing components), KratePluginsShared (internal)
**Storage**: Binary state version 2 — version int32 + 5 Phase-1 float64 + 2 int32 selectors + ~25 Phase-2 float64 params (~220 bytes total)
**Testing**: Catch2 (via `test_helpers`), `allocation_detector`, `pluginval --strictness-level 5`, `auval -v aumu Mbrm KrAt`
**Target Platform**: Windows 10/11 (MSVC), macOS 11+ (Clang universal), Linux (GCC 10+)
**Project Type**: Plugin refactor in existing monorepo (`plugins/membrum/`)
**Performance Goals**: ≤ 1.25% single-voice CPU at 44.1 kHz for every exciter × body × toneShaper × unnatural combination (144 cases)
**Constraints**: 0 allocations on audio thread; real-time safe; cross-platform; no `dsp/` changes; virtual dispatch forbidden on hot path
**Scale/Scope**: Single voice (multi-voice deferred to Phase 3); ~29 new parameters; ~25 new source files; ~15 new unit test files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I — VST3 Architecture Separation:**
- [x] Processor and Controller remain separate (Phase 1 carryover). No cross-includes.
- [x] Parameter flow: Host → Processor via `IParameterChanges`; Controller receives state via `setComponentState()`.
- [x] Processor works without controller.

**Principle II — Real-Time Audio Thread Safety:**
- [x] All new DSP components pre-allocated in `DrumVoice::prepare()`. `ExciterBank` holds all 6 exciter variants inline via `std::variant`.
- [x] `setExciterType()` / `setBodyModel()` only set atomic pending values; no audio-thread allocation on swap.
- [x] Shared `ModalResonatorBank::setModes()` clears state in-place — no heap.
- [x] No locks, exceptions, I/O, virtual dispatch on the hot path.
- [x] Allocation-detector tests covering `DrumVoice::noteOn/noteOff/process` across all 36 combinations (SC-011).

**Principle III — Modern C++ Standards:**
- [x] C++20, RAII, `std::variant`, `std::visit`, no raw `new`/`delete`.
- [x] `constexpr` mode-ratio tables.

**Principle IV — SIMD & DSP Optimization:**
- [x] SIMD viability analysis completed (see section below). **Verdict: defer to Phase 3.** Scalar is sufficient for single-voice 1.25% budget.
- [x] Scalar-first workflow honored.

**Principle V — VSTGUI Development:**
- [x] N/A — no custom UI in Phase 2 (host-generic editor only, per FR-083). Custom UI is Phase 5.

**Principle VI — Cross-Platform Compatibility:**
- [x] No platform-specific code. All DSP uses `Krate::DSP` cross-platform primitives.
- [x] CI builds on Windows/macOS/Linux remain unchanged.
- [x] AU config files unchanged (no bus-config changes).

**Principle VII — Project Structure & Build System:**
- [x] All new code in `plugins/membrum/src/dsp/` (plugin-local).
- [x] `dsp/` library unchanged (FR-101).
- [x] CMakeLists.txt updates limited to adding source files.

**Principle VIII — Testing Discipline:**
- [x] Tests written BEFORE implementation per Phase N.1 ordering.
- [x] Unit tests for every new exciter/body/mapper/tone-shaper/unnatural-zone module.
- [x] Approval/golden-reference test for Phase 1 regression (SC-005).

**Principle IX — Layered DSP Architecture:**
- [x] No new `dsp/` layer components. Plugin-local code in `plugins/membrum/src/dsp/` does not count as a DSP layer.
- [x] All reused components respect their layers (Layer 1 primitives, Layer 2 processors, Layer 3 systems).

**Principle XII — Debugging Discipline:**
- [x] Framework commitment — using `std::variant` per C++ standard, `ModalResonatorBank::setModes` per Phase 1 proven pattern.

**Principle XIII — Test-First Development:**
- [x] Skills auto-load. Tests written before implementation.
- [x] Each task group ends with a commit step.

**Principle XIV — ODR Prevention:**
- [x] Codebase research complete — all new type names are in `Membrum::` or `Membrum::Bodies::` namespace.
- [x] Verified no collision with `Krate::DSP::FeedbackNetwork` / `NoiseGenerator` / existing classes.

**Principle XVI — Honest Completion:**
- [x] Compliance table will be filled with file paths, line numbers, test names, measured values — NOT from memory.

**Post-design re-check (after Phase 1 design complete):** PASS. No constitution violations introduced.

## Codebase Research (Principle XIV - ODR Prevention)

### Mandatory Searches Performed

**Classes/Structs to be created**: See `data-model.md` §11. New types:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `Membrum::ExciterType` (enum) | `grep -r "enum class ExciterType" dsp/ plugins/` | No | Create New |
| `Membrum::BodyModelType` (enum) | `grep -r "enum class BodyModelType" dsp/ plugins/` | No | Create New |
| `Membrum::ExciterBank` | `grep -r "class ExciterBank" dsp/ plugins/` | No | Create New |
| `Membrum::BodyBank` | `grep -r "class BodyBank" dsp/ plugins/` | No | Create New |
| `Membrum::ToneShaper` | `grep -r "class ToneShaper" dsp/ plugins/` | No | Create New |
| `Membrum::UnnaturalZone` | `grep -r "class UnnaturalZone" dsp/ plugins/` | No | Create New |
| `Membrum::MaterialMorph` | `grep -r "class MaterialMorph" dsp/ plugins/` | No | Create New |
| `Membrum::ModeInject` | `grep -r "class ModeInject" dsp/ plugins/` | No | Create New |
| `Membrum::NonlinearCoupling` | `grep -r "class NonlinearCoupling" dsp/ plugins/` | No | Create New |
| `Membrum::ImpulseExciter` | `grep -r "class ImpulseExciter" dsp/ plugins/` | No | Create New |
| `Membrum::MalletExciter` | `grep -r "class MalletExciter" dsp/ plugins/` | No | Create New |
| `Membrum::NoiseBurstExciter` | `grep -r "class NoiseBurstExciter" dsp/ plugins/` | No | Create New |
| `Membrum::FrictionExciter` | `grep -r "class FrictionExciter" dsp/ plugins/` | No | Create New |
| `Membrum::FMImpulseExciter` | `grep -r "class FMImpulseExciter" dsp/ plugins/` | No | Create New |
| `Membrum::FeedbackExciter` | `grep -r "class FeedbackExciter" dsp/ plugins/` | No | Create New (distinct from `Krate::DSP::FeedbackNetwork`) |
| `Membrum::MembraneBody` etc. | `grep -r "class MembraneBody" dsp/ plugins/` | No | Create New |
| `Membrum::NoiseBody` | `grep -r "class NoiseBody" dsp/ plugins/` | No | Create New (distinct from `Krate::DSP::NoiseGenerator`) |
| `Membrum::Bodies::PlateMapper` etc. | `grep -r "class PlateMapper" dsp/ plugins/` | No | Create New |
| `Membrum::VoiceCommonParams` | `grep -r "struct VoiceCommonParams" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: All per-body mode-ratio tables (`kPlateRatios`, `kShellRatios`, `kBellRatios`) and amplitude helpers are in `Membrum::Bodies::` namespace. No collisions.

### Existing Components to Reuse

See `research.md` §2 for the full verified API table. Summary:

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `Krate::DSP::ImpactExciter` | `dsp/include/krate/dsp/processors/impact_exciter.h:33` | 2 | Impulse + Mallet exciters (different parameter envelopes) |
| `Krate::DSP::BowExciter` | `dsp/include/krate/dsp/processors/bow_exciter.h:60` | 2 | Friction exciter (transient mode only) |
| `Krate::DSP::NoiseOscillator` | `dsp/include/krate/dsp/primitives/noise_oscillator.h:67` | 1 | Noise Burst + Noise Body noise layer |
| `Krate::DSP::FMOperator` | `dsp/include/krate/dsp/processors/fm_operator.h:67` | 2 | FM Impulse exciter (carrier + modulator) |
| `Krate::DSP::ModalResonatorBank` | `dsp/include/krate/dsp/processors/modal_resonator_bank.h:71` | 2 | Shared bank for Membrane/Plate/Shell/Bell/NoiseBody modal |
| `Krate::DSP::WaveguideString` | `dsp/include/krate/dsp/processors/waveguide_string.h:38` | 2 | String body (primary backend) |
| `Krate::DSP::HarmonicOscillatorBank` | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h:73` | 2 | Mode Inject partial source |
| `Krate::DSP::SVF` | `dsp/include/krate/dsp/primitives/svf.h:110` | 1 | Tone Shaper filter, Noise Burst color, Noise Body noise filter, Feedback exciter filter |
| `Krate::DSP::Waveshaper` | `dsp/include/krate/dsp/primitives/waveshaper.h:108` | 1 | Tone Shaper Drive |
| `Krate::DSP::Wavefolder` | `dsp/include/krate/dsp/primitives/wavefolder.h:101` | 1 | Tone Shaper Wavefolder |
| `Krate::DSP::DCBlocker` | `dsp/include/krate/dsp/primitives/dc_blocker.h:94` | 1 | Post-saturation DC removal |
| `Krate::DSP::TanhADAA` | `dsp/include/krate/dsp/primitives/tanh_adaa.h:66` | 1 | Feedback exciter soft-clip, Nonlinear Coupling energy limiter |
| `Krate::DSP::MultiStageEnvelope` | `dsp/include/krate/dsp/processors/multi_stage_envelope.h:61` | 2 | Pitch Envelope; FM Impulse amp + mod-index envelopes |
| `Krate::DSP::EnvelopeFollower` | `dsp/include/krate/dsp/processors/envelope_follower.h:82` | 2 | Nonlinear Coupling + Feedback exciter energy driver |
| `Krate::DSP::ADSREnvelope` | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | 1 | Amp envelope (unchanged); Tone Shaper filter envelope |
| `Krate::DSP::XorShift32` | `dsp/include/krate/dsp/core/xorshift32.h` | 0 | Mode Inject phase randomization |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` — no name collisions
- [x] `dsp/include/krate/dsp/primitives/` — no collisions
- [x] `dsp/include/krate/dsp/processors/` — `ImpactExciter`, `BowExciter`, `FMOperator`, etc. are the referenced existing components; no conflicts with new plugin-local names
- [x] `dsp/include/krate/dsp/systems/` — `FeedbackNetwork` exists in `Krate::DSP`, distinct namespace from `Membrum::FeedbackExciter`
- [x] `plugins/iterum/`, `plugins/disrumpo/`, `plugins/ruinae/`, `plugins/gradus/`, `plugins/innexus/`, `plugins/shared/` — no `Membrum::*` name collisions
- [x] `plugins/membrum/src/` — Phase 1 files to be modified, not duplicated

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All new types are in the `Membrum` or `Membrum::Bodies` namespace, which did not exist before Phase 1. The only class duplication hazards are `Membrum::FeedbackExciter` (vs `Krate::DSP::FeedbackNetwork`) and `Membrum::NoiseBody` (vs `Krate::DSP::NoiseGenerator`) — both resolved by the distinct namespaces. Search commands in the table above will be re-executed at the start of Phase 2 implementation to confirm no new collisions appeared.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

All signatures verified against headers on 2026-04-10. See `research.md` §2 for the full table.

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `ImpactExciter` | `prepare` | `void prepare(double sampleRate, uint32_t voiceId) noexcept` | ✓ |
| `ImpactExciter` | `trigger` | `void trigger(float velocity, float hardness, float mass, float brightness, float position, float f0) noexcept` | ✓ |
| `ImpactExciter` | `process` | `[[nodiscard]] float process(float feedbackVelocity) noexcept` | ✓ |
| `BowExciter` | `prepare` | `void prepare(double sampleRate) noexcept` | ✓ |
| `BowExciter` | `trigger` | `void trigger(float velocity) noexcept` | ✓ |
| `BowExciter` | `release` | `void release() noexcept` | ✓ |
| `BowExciter` | `process` | `[[nodiscard]] float process(float feedbackVelocity) noexcept` | ✓ |
| `BowExciter` | `setEnvelopeValue` | `void setEnvelopeValue(float) noexcept` | ✓ |
| `FMOperator` | `prepare` | `void prepare(double sampleRate) noexcept` | ✓ |
| `FMOperator` | `setFrequency` | `void setFrequency(float hz) noexcept` | ✓ |
| `FMOperator` | `setRatio` | `void setRatio(float ratio) noexcept` | ✓ |
| `FMOperator` | `setFeedback` | `void setFeedback(float amount) noexcept` | ✓ |
| `ModalResonatorBank` | `prepare` | `void prepare(double sampleRate) noexcept override` | ✓ |
| `ModalResonatorBank` | `setModes` | `void setModes(const float* frequencies, const float* amplitudes, int numPartials, float decayTime, float brightness, float stretch, float scatter) noexcept` | ✓ |
| `ModalResonatorBank` | `updateModes` | `void updateModes(...)` (same signature, does NOT clear state) | ✓ |
| `ModalResonatorBank` | `processSample` | `[[nodiscard]] float processSample(float excitation) noexcept` | ✓ |
| `ModalResonatorBank::kMaxModes` | constant | `static constexpr int kMaxModes = 96` | ✓ |
| `WaveguideString` | `prepare` | `void prepare(double sampleRate) noexcept override` | ✓ |
| `WaveguideString` | `prepareVoice` | `void prepareVoice(uint32_t voiceId) noexcept` | ✓ |
| `WaveguideString` | `setFrequency` | `void setFrequency(float f0) noexcept override` | ✓ |
| `WaveguideString` | `setDecay` | `void setDecay(float t60) noexcept override` | ✓ |
| `WaveguideString` | `setBrightness` | `void setBrightness(float brightness) noexcept override` | ✓ |
| `WaveguideString` | `process` | `[[nodiscard]] float process(float excitation) noexcept override` | ✓ |
| `SVF` | `prepare` | `void prepare(double sampleRate) noexcept` | ✓ |
| `SVF` | `setMode` | `void setMode(SVFMode mode) noexcept` | ✓ |
| `SVF` | `setCutoff` | `void setCutoff(float hz) noexcept` | ✓ |
| `SVF` | `setResonance` | `void setResonance(float q) noexcept` | ✓ |
| `SVF` | `process` | `[[nodiscard]] float process(float input) noexcept` | ✓ |
| `Wavefolder` | `setFoldAmount` | `void setFoldAmount(float amount) noexcept` | ✓ |
| `Wavefolder` | `process` | `[[nodiscard]] float process(float x) const noexcept` | ✓ |
| `MultiStageEnvelope` | `prepare` | `void prepare(float sampleRate) noexcept` | ✓ |
| `MultiStageEnvelope` | `setStage` | `void setStage(int stage, float level, float ms, EnvCurve curve) noexcept` | ✓ |
| `MultiStageEnvelope` | `gate` | `void gate(bool on) noexcept` | ✓ |
| `MultiStageEnvelope` | `process` | `float process() noexcept` | ✓ |
| `HarmonicOscillatorBank` | `prepare` | `void prepare(double sampleRate) noexcept` | ✓ |
| `HarmonicOscillatorBank` | `setTargetPitch` | `void setTargetPitch(float frequencyHz) noexcept` | ✓ |
| `HarmonicOscillatorBank` | `process` | `[[nodiscard]] float process() noexcept` | ✓ |
| `EnvelopeFollower` | `processSample` | `[[nodiscard]] float processSample(float input) noexcept` | ✓ |
| `TanhADAA` | `process` | `[[nodiscard]] float process(float x) noexcept` | ✓ |
| `DCBlocker` | `prepare` | `void prepare(double sampleRate, float cutoffHz) noexcept` | ✓ |
| `DCBlocker` | `process` | `[[nodiscard]] float process(float) noexcept` | ✓ |
| `NoiseOscillator` | `prepare` | `void prepare(double sampleRate) noexcept` | ✓ |
| `NoiseOscillator` | `process` | `[[nodiscard]] float process() noexcept` | ✓ |
| `XorShift32` | `next` | `[[nodiscard]] uint32_t next() noexcept` | ✓ |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/impact_exciter.h`
- [x] `dsp/include/krate/dsp/processors/bow_exciter.h`
- [x] `dsp/include/krate/dsp/processors/fm_operator.h`
- [x] `dsp/include/krate/dsp/systems/feedback_network.h` — confirmed it is block-oriented delay-feedback, NOT per-sample voice feedback. See research.md §3.
- [x] `dsp/include/krate/dsp/processors/modal_resonator_bank.h`
- [x] `dsp/include/krate/dsp/processors/waveguide_string.h`
- [x] `dsp/include/krate/dsp/processors/karplus_strong.h`
- [x] `dsp/include/krate/dsp/processors/noise_generator.h` — confirmed block-oriented, wrong shape. Use `NoiseOscillator` instead.
- [x] `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [x] `dsp/include/krate/dsp/primitives/svf.h`
- [x] `dsp/include/krate/dsp/primitives/wavefolder.h`
- [x] `dsp/include/krate/dsp/processors/multi_stage_envelope.h`
- [x] `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h`
- [x] `dsp/include/krate/dsp/processors/iresonator.h`
- [x] `plugins/membrum/src/dsp/drum_voice.h` — Phase 1 baseline

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `FeedbackNetwork` | Block-oriented stereo delay feedback — NOT per-sample voice feedback | Do NOT reuse. Build plugin-local `Membrum::FeedbackExciter` from primitives (SVF + TanhADAA + DCBlocker + EnvelopeFollower) |
| `NoiseGenerator` | Block-oriented multi-mode colored-noise source with internal envelope followers | Do NOT reuse for per-sample use. Use `NoiseOscillator` (Layer 1 primitive) instead |
| `WaveguideString::process` | Virtual override of `IResonator::process` | Called through concrete type in `std::variant`, compiler devirtualizes. Safe. |
| `ModalResonatorBank::setModes` vs `updateModes` | `setModes` clears filter state; `updateModes` preserves it | Use `setModes` on note-on; `updateModes` only during block-rate Pitch Envelope updates |
| `ModalResonatorBank::kMaxModes` | 96 | Noise Body uses up to 40; well under cap |
| `ImpactExciter::trigger` | position=0, f0=0 disables comb filter | Phase 1 convention; preserved |
| `BowExciter::trigger(velocity)` | No explicit mode selector; transient vs sustained is implicit in the envelope given to `setEnvelopeValue` | Phase 2 FrictionExciter wraps an internal ADSREnvelope that ramps down within ≤50 ms |
| `MultiStageEnvelope::kMinStages` | 4 minimum stages | Pitch Envelope uses 4 stages (start, attack, end, release) |
| `std::variant::index()` | Returns the 0-based alternative index | Use for index-based `switch` fallback dispatch on MSVC if `std::visit` benchmarks worse |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

**Decision**: No Layer 0 extractions in Phase 2. All new code is plugin-local per FR-101. If the per-body Mapper pattern proves valuable in Phase 3 (voice pool) or Phase 4 (bowed/sustained), it may be promoted to `plugins/shared/` at that time. Phase 2 keeps everything in `plugins/membrum/src/dsp/`.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Per-body strike-position amplitude helpers | Body-specific math; only one consumer per helper |
| Membrane → Plate → Shell → Bell mapping | Encapsulated per-body logic |

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES (per-voice) | Gordon-Smith modes in `ModalResonatorBank`; waveguide delay loops; FeedbackExciter path |
| **Data parallelism width** | N modes (16–40) per voice, but only 1 voice in Phase 2 | SIMD would parallelize modes; already present in `ModalResonatorBankSIMD` |
| **Branch density in inner loop** | LOW | Per-sample hot path is branchless once the variant is dispatched per block |
| **Dominant operations** | arithmetic (modal MACs) | multiply-accumulate in Gordon-Smith |
| **Current CPU budget vs expected usage** | 1.25% budget, expected ~0.5% at 40 modes scalar | Well within budget |

### SIMD Viability Verdict

**Verdict**: BENEFICIAL — **DEFER to Phase 3**

**Reasoning**: The scalar `ModalResonatorBank` at 16–40 modes single voice is expected to consume ~0.2–0.5% CPU on the reference machine (extrapolation from Phase 1's measured 0.2% at 16 modes). The 1.25% per-voice Phase 2 budget leaves ample headroom for the Tone Shaper and Unnatural Zone overhead. SIMD becomes critical in Phase 3 where 8 voices × 1.25% = 10% (at the total-plugin ceiling) — at that point, switching to `ModalResonatorBankSIMD` (already in the library) gains 2–4× on the mode-processing hot path.

### Implementation Workflow

**Phase 2 (this spec)**: Scalar only. Meet the 1.25% budget with scalar code. Establish correctness tests and CPU baselines for all 144 combinations.

**Phase 3 (future)**: Switch to `ModalResonatorBankSIMD` inside `BodyBank` as a drop-in. Same API (`processSample`, `setModes`, `updateModes`), same tests.

**Emergency fallback**: If Phase 2 CPU benchmark (FR-093) exceeds 1.25% for any combination on the scalar path — most likely Noise Body with 40 modes + full Unnatural Zone — the implementer switches `BodyBank::sharedBank_` to `ModalResonatorBankSIMD` within Phase 2 and documents the change. This is the only permitted scope creep.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out on idle voice (`isActive()` check) | ~100% saved when voice silent | LOW | YES — already in Phase 1; preserved |
| `ModalResonatorBank::flushSilentModes` | Marginal saving on decaying modes | LOW | YES — already built-in |
| `if (amount == 0) return;` on Unnatural Zone sub-modules | 100% saved when zeroed | LOW | YES — required by FR-055 |
| Early-out on Tone Shaper bypass | ~100% saved on bypassed stages | LOW | YES — required by FR-045 |
| Reduce Noise Body mode count from 40 → 30 | ~25% Noise Body saving | LOW | CONDITIONAL — only if 40 blows budget |

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin-local (`plugins/membrum/src/dsp/`); does not touch shared DSP library.

**Related features at same layer** (from Membrum roadmap):
- Phase 3: Voice pool + 32 pads (8-voice polyphony with per-pad parameters)
- Phase 4: Bowed / sustained excitation extension
- Phase 5: Custom UI with macro controls + Acoustic/Extended mode

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `ExciterBank` + `BodyBank` dispatch pattern | HIGH | Phase 3 (8-voice pool, each voice has an ExciterBank + BodyBank) | Keep plugin-local; duplicable per voice |
| Per-body Mapper helpers | MEDIUM | Phase 3 (per-pad parameters), Phase 4 (bowed variants may reuse Mappers), possible future tuned-percussion instrument | Keep local; consider promoting to `plugins/shared/` after 2+ consumers |
| `UnnaturalZone` (Mode Inject, Nonlinear Coupling, Material Morph) | MEDIUM | Possible reuse in a future Membrum-adjacent tuned-percussion instrument | Keep local for Phase 2; revisit after Phase 4 |
| `ToneShaper` (SVF + Drive + Wavefolder + Pitch Env chain) | HIGH | Potentially reusable across any percussion instrument | Keep local for now; promote in Phase 3 if a second consumer appears |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| All Phase 2 code plugin-local | FR-101 mandate; no `dsp/` changes |
| Per-body mappers as free functions, not virtual classes | No hot-path dispatch benefit; simpler to test as pure functions |
| `ExciterBank` holds all 6 variants inline | Fixed memory layout, no indirection, no heap |
| Pitch Envelope as control-plane, not audio-rate | Spec 135 mandate; matches physical-pitch-glide model |
| Shared `ModalResonatorBank` owned by `BodyBank` | Clarification 3 decision; avoids 5× memory overhead |

### Review Trigger

After implementing Phase 3 (voice pool), review:
- [ ] Does voice pool benefit from moving `ExciterBank` to `plugins/shared/`?
- [ ] Should `ToneShaper` be shared across Membrum + a future instrument?
- [ ] Are Mapper helpers stable enough to extract?

## Phase 2 Sub-phase Breakdown (for tasks.md input)

Phase 2 is divided into 7 sub-phases, each with its own test-first cycle and commit:

### Sub-phase 2.A — Architecture Refactor (Swap-In Skeleton)

**Goal**: Replace Phase 1's hardcoded `DrumVoice` composition with `ExciterBank` + `BodyBank` + `ToneShaper` + `UnnaturalZone` skeletons. All new components are stub-implemented; the only exciter is `ImpulseExciter`, the only body is `MembraneBody`. Phase 1 acceptance tests MUST continue to pass.

Deliverables:
- `plugin_ids.h` extended with Exciter Type + Body Model selectors, state version bumped to 2.
- `exciter_type.h`, `body_model_type.h`, `voice_common_params.h`, `exciter_bank.h`, `body_bank.h`, `tone_shaper.h` (all stub).
- `drum_voice.h` refactored to use ExciterBank + BodyBank + ToneShaper + UnnaturalZone (sub-modules stubbed).
- `MembraneMapper` extracted from Phase 1 inline code — produces bit-identical results.
- Phase 1 regression test added (golden-reference comparison, SC-005).
- Allocation-detector test for the refactored skeleton.
- State round-trip test (Phase 1 state file → Phase 2, assert defaults filled).
- All Phase 1 tests continue to pass unchanged.

Commit marker: "membrum: Phase 2.A skeleton — swap-in architecture".

### Sub-phase 2.B — Exciter Types (5 new)

**Goal**: Implement `MalletExciter`, `NoiseBurstExciter`, `FrictionExciter`, `FMImpulseExciter`, `FeedbackExciter`. Each is a separate subtask with tests.

Deliverables per exciter:
- Header in `plugins/membrum/src/dsp/exciters/`.
- Unit test for velocity → spectral-centroid response (FR-016, SC-004).
- Unit test for 500 ms non-silent, non-NaN, peak ≤ 0 dBFS.
- Allocation-detector test.
- Variant dispatch integrated into `ExciterBank`.

Commit markers (5 commits, one per exciter): "membrum: Phase 2.B.1 Mallet exciter", ..., "membrum: Phase 2.B.5 Feedback exciter".

### Sub-phase 2.C — Body Models (5 new)

**Goal**: Implement `PlateBody`, `ShellBody`, `StringBody`, `BellBody`, `NoiseBody`. Each with its own mode-ratio table (where modal) and mapping helper.

Deliverables per body:
- Mode-ratio header (`plate_modes.h`, `shell_modes.h`, `bell_modes.h`) where applicable.
- Body backend in `bodies/`.
- Per-body mapping helper (`plate_mapper.h`, etc.).
- Unit test for modal ratio accuracy (SC-002) within per-body tolerances.
- Unit test for Size sweep (≥ 1 octave range, US4-1).
- Unit test for Decay sweep (≥ 3× RT60 range, US4-2).
- Variant dispatch integrated into `BodyBank`.

Commit markers (5 commits): "membrum: Phase 2.C.1 Plate body", ..., "membrum: Phase 2.C.5 Noise Body".

### Sub-phase 2.D — Tone Shaper

**Goal**: Full Tone Shaper chain with filter envelope, drive, wavefolder, DC blocker, pitch envelope.

Deliverables:
- `tone_shaper.h` full implementation.
- Bypass-identity test (FR-045, ≤ −120 dBFS RMS difference).
- 808-kick pitch envelope test (SC-009).
- Filter envelope sweep test.
- Drive THD test.
- Wavefolder odd-harmonic test.
- Allocation-detector test.
- Integration into `DrumVoice::process()` signal chain.
- Parameter registration in Controller + handling in Processor.

Commit marker: "membrum: Phase 2.D Tone Shaper".

### Sub-phase 2.E — Unnatural Zone

**Goal**: Mode Stretch, Decay Skew, Mode Inject (with phase randomization), Nonlinear Coupling (with energy limiter), Material Morph.

Deliverables per sub-module (5 commits):
- `unnatural/mode_stretch.h` → direct parameter pass-through in Mapper.
- `unnatural/decay_skew.h` → scalar-bias approximation in Mapper.
- `unnatural/mode_inject.h` → full impl with XorShift32 phase randomization.
- `unnatural/nonlinear_coupling.h` → EnvelopeFollower + TanhADAA energy limiter.
- `unnatural/material_morph.h` → 2-point envelope with linear/exp curve.
- Default-off regression test (FR-055, all Unnatural at defaults = bit-identical to Phase 2 off).
- Per-sub-module behavior tests.
- Allocation-detector test.
- Parameter registration in Controller.

Commit marker (5 commits): "membrum: Phase 2.E.1 Mode Stretch", ..., "membrum: Phase 2.E.5 Material Morph".

### Sub-phase 2.F — 36-Combination Matrix & State Round-Trip

**Goal**: End-to-end validation across all 36 exciter × body combinations.

Deliverables:
- Parameterized 36-combination matrix test (FR-090): non-silent, non-NaN, peak ≤ 0 dBFS, allocation-free.
- Parameterized 144-combination matrix test: add toneShaper on/off × unnatural on/off.
- State round-trip test for all ~34 parameters (SC-006).
- Phase 1 backward compatibility test (FR-082).
- Sample rate sweep test (22050–192000 Hz, SC-007).
- Stability guard tests (Feedback exciter + max velocity + all bodies; Nonlinear Coupling = 1.0; SC-008).

Commit marker: "membrum: Phase 2.F end-to-end matrix + state round-trip".

### Sub-phase 2.G — CPU Benchmark + CI Gating + Release Prep

**Goal**: Meet the 1.25% single-voice CPU budget across all 144 combinations. Wire into CI.

Deliverables:
- `[.perf]`-tagged benchmark test iterating 144 combinations.
- CSV output to `build/.../membrum_benchmark_results.csv`.
- Hard-assert on budget exceeded.
- CI job config (nightly hard-assert, per-commit soft-warn).
- Final Noise Body mode count selection — start at 40, measure, reduce only if needed. Record final count in this plan's "Complexity Tracking" post-implementation.
- Pluginval strictness-5 validation (FR-096, SC-010).
- auval validation on macOS (FR-097, SC-010).
- Clang-tidy full pass on `membrum` target with zero warnings.
- Release-notes update in `plugins/membrum/CHANGELOG.md`.
- Version bump in `plugins/membrum/version.json`.
- Compliance table filled in spec.md per Principle XVI (file paths, line numbers, test names, measured values).

Commit marker: "membrum: Phase 2.G CPU validation + CI gating".

## Risk Register

| Risk | Severity | Mitigation | Trigger |
|------|----------|------------|---------|
| Noise Body 40 modes + full Unnatural blows 1.25% CPU budget | HIGH | Reduce to 30/25/20 modes iteratively; emergency fallback to `ModalResonatorBankSIMD` | Benchmark sub-phase 2.G |
| `std::visit` codegen on MSVC is slower than expected | MED | Fall back to index-based `switch` on `variant.index()` | Per-sub-phase CPU profile |
| `MembraneMapper` refactor drifts from Phase 1 inline code | MED | Golden-reference regression test (SC-005) at −90 dBFS tolerance; test added in sub-phase 2.A | Sub-phase 2.A commit |
| Decay Skew scalar-bias approximation fails US6-2 test | MED | Escalate to per-block `updateModes()` refresh with computed per-mode damping; documented in research.md §9 | Sub-phase 2.E.2 |
| Nonlinear Coupling instability at amount = 1.0 with high-Q body | HIGH | `TanhADAA` energy limiter mandatory; test at all 36 bodies × velocity 127 × amount 1.0 | Sub-phase 2.E.4 + 2.F |
| FeedbackExciter Larsen runaway on String or Bell body | HIGH | `EnvelopeFollower`-driven energy limiter; test at max feedback × all bodies | Sub-phase 2.B.5 + 2.F |
| Phase 1 regression test fails because refactor changes mapper output | MED | Extract Phase 1 code verbatim into `MembraneMapper::map`; bit-identical for same inputs | Sub-phase 2.A |
| 144-combination test runtime blows CI budget | LOW | Use `[.perf]` opt-in tag; per-commit runs subset (36 smoke), nightly runs full 144 | Sub-phase 2.G |
| State version 2 corrupts Phase 1 state files | HIGH | Backward-compat loader reads version first, fills Phase 2 defaults for version=1 | Sub-phase 2.A state test |
| Real-time allocation leak in one of the new components | HIGH | Allocation-detector test for every new component's `process()` and `trigger()` | Every sub-phase |
| Virtual dispatch accidentally reintroduced (WaveguideString inherits `IResonator`) | LOW | Concrete-type call through variant guarantees devirtualization; verified via asm inspection if needed | Sub-phase 2.C.3 |

## Future-Phase Preview

- **Phase 3** — Voice pool (8 voices), per-pad parameters, cross-pad coupling, `ModalResonatorBankSIMD`, choke groups, sympathetic resonance.
- **Phase 4** — Sustained bowed excitation (FrictionExciter extension with continuous bow pressure), snare wire modeling.
- **Phase 5** — Custom VSTGUI UI, macro controls (Tightness, Brightness, Body Size, Punch, Complexity), Acoustic/Extended mode gating.
- **Phase 6+** — Full von Karman nonlinear coupling (replace Phase 2's simplified stand-in), double membrane coupling, air coupling, tuned-bar body variant, 808-specific oscillator bank.

## Real-Time Safety Review

Every Phase 2 new component's audio-thread entry points enumerated:

| Component | Audio-thread entry points | Allocation? | Lock? | Exception? | Virtual dispatch? |
|-----------|---------------------------|-------------|-------|------------|-------------------|
| `DrumVoice::noteOn` | 1 call at note start | None (pre-alloc'd) | None | None | None (variant swap is inline) |
| `DrumVoice::noteOff` | 1 call at note end | None | None | None | None |
| `DrumVoice::process` | Per-sample | None | None | None | None (concrete type via variant) |
| `ExciterBank::trigger` | 1 call/note | None | None | None | None (variant emplace is in-place) |
| `ExciterBank::process` | Per-sample | None | None | None | None (via visit or switch) |
| `BodyBank::configureForNoteOn` | 1 call/note | None (`setModes` clears state in-place) | None | None | None |
| `BodyBank::processSample` | Per-sample | None | None | None | None |
| Every exciter backend `process()` | Per-sample | None (all state pre-alloc'd) | None | None | None (direct member calls inside alternative) |
| `ToneShaper::processSample` | Per-sample | None | None | None | None |
| `ToneShaper::processPitchEnvelope` | Per-sample (control plane) | None | None | None | None |
| `UnnaturalZone::*::process` | Per-sample | None | None | None | None |
| `ModeInject::trigger` | 1 call/note | None (XorShift32 is per-voice) | None | None | None |
| `MaterialMorph::process` | Per-sample | None | None | None | None |
| Parameter setters (`setExciterType`, etc.) | Multiple/block | None (atomic write + pending flag) | None | None | None |

**Verification**: allocation-detector test wraps every audio-thread entry point. Any leak fails the test.

## Test Strategy

| FR/SC | Test type | Test file | Measured value |
|-------|-----------|-----------|-----------------|
| FR-001–FR-007 | Unit + integration | `tests/unit/architecture/test_exciter_bank.cpp`, `test_body_bank.cpp` | Variant dispatch correctness |
| FR-010–FR-017 | Per-exciter unit | `tests/unit/exciters/test_<exciter>.cpp` (6 files) | Velocity response, spectral centroid, allocation-free |
| FR-020–FR-028 | Per-body unit | `tests/unit/bodies/test_<body>.cpp` (6 files) | Modal ratios (SC-002), fundamental sweep, decay sweep |
| FR-030–FR-035 | Per-mapper unit | `tests/unit/bodies/test_mapper_<body>.cpp` (6 files) | Mapper output matches reference, FR-031 Membrane bit-identity |
| FR-040–FR-047 | Tone Shaper unit | `tests/unit/tone_shaper/test_tone_shaper.cpp` | Bypass identity, 808 kick (SC-009), filter env, drive THD |
| FR-050–FR-056 | Unnatural Zone unit | `tests/unit/unnatural/test_<module>.cpp` (5 files) | Default-off identity (FR-055), phase randomization, energy limiter |
| FR-060–FR-061 | Velocity mapping | `tests/unit/exciters/test_velocity_mapping.cpp` | Per-exciter velocity → spectral centroid ratio ≥ 2.0 (SC-004) |
| FR-062 | Noise Body CPU | `tests/perf/test_noise_body_cpu.cpp` | Mode count vs CPU; final count recorded |
| FR-070–FR-074 | CPU budget + RT safety | `tests/perf/test_cpu_144_combinations.cpp`, `tests/unit/test_allocation_matrix.cpp` | CPU ≤ 1.25%, allocation count = 0 |
| FR-080–FR-083 | Parameter/controller | `tests/unit/vst/test_membrum_parameters_v2.cpp` | 34 parameters registered, StringList selectors correct |
| FR-090 | 36-combo matrix | `tests/unit/test_exciter_body_matrix.cpp` | Non-silent, non-NaN, peak ∈ (-30, 0) dBFS |
| FR-091 | Body spectral | `tests/unit/bodies/test_spectral_<body>.cpp` (6 files) | First-N partial ratios within tolerance |
| FR-092 | Exciter centroid | `tests/unit/exciters/test_centroid_<exciter>.cpp` (6 files) | vel 30/127 ratio ≥ 2.0 |
| FR-093 | CPU benchmark | `tests/perf/test_benchmark_144.cpp` (`[.perf]` tag) | CSV output + hard assert |
| FR-094 | State round-trip | `tests/unit/vst/test_state_roundtrip_v2.cpp` | Bit-identical normalized values |
| FR-095 | Phase 1 regression | `tests/approval/test_phase1_regression.cpp` | RMS diff ≤ −90 dBFS vs golden |
| FR-096 | Pluginval | CI job | strictness 5, 0 errors, 0 warnings |
| FR-097 | auval | CI job (macOS) | `auval -v aumu Mbrm KrAt` returns 0 |
| SC-008 | Stability guard | `tests/unit/test_stability_guard.cpp` | Feedback × max vel × all bodies → peak ≤ 0 dBFS |
| SC-011 | Allocation | `tests/unit/test_allocation_matrix.cpp` | All 36 combinations × all audio-thread entries |

## Project Structure

### Documentation (this feature)

```text
specs/137-membrum-phase2-exciters-bodies/
├── spec.md              # Feature specification (existing)
├── plan.md              # This file
├── research.md          # Phase 0 research findings
├── data-model.md        # Entity definitions
├── quickstart.md        # Build/test instructions
├── contracts/
│   ├── exciter_contract.md
│   ├── body_contract.md
│   ├── tone_shaper_contract.md
│   ├── unnatural_zone_contract.md
│   └── vst_parameter_contract.md
├── checklists/
│   └── requirements.md   # (existing, all items pass)
└── tasks.md              # To be produced by /speckit.tasks
```

### Source Code (repository root)

```text
plugins/membrum/
├── CMakeLists.txt                  # MODIFIED — add new source files
├── version.json                    # MODIFIED — minor version bump
├── CHANGELOG.md                    # MODIFIED — Phase 2 release notes
│
├── src/
│   ├── entry.cpp                   # unchanged
│   ├── plugin_ids.h                # MODIFIED — new Phase 2 parameter IDs + state version
│   ├── version.h.in                # unchanged
│   │
│   ├── processor/
│   │   ├── processor.h             # MODIFIED — new parameter atomics
│   │   └── processor.cpp           # MODIFIED — processParameterChanges for new params, state save/load v2
│   │
│   ├── controller/
│   │   ├── controller.h            # MODIFIED — new parameter registration
│   │   └── controller.cpp          # MODIFIED — StringListParameter for Exciter/Body selectors
│   │
│   └── dsp/
│       ├── drum_voice.h            # MODIFIED — refactored
│       ├── membrane_modes.h        # unchanged (Phase 1 carryover)
│       ├── exciter_type.h          # NEW
│       ├── body_model_type.h       # NEW
│       ├── voice_common_params.h   # NEW
│       ├── exciter_bank.h          # NEW
│       ├── body_bank.h             # NEW
│       ├── tone_shaper.h           # NEW
│       │
│       ├── exciters/
│       │   ├── impulse_exciter.h
│       │   ├── mallet_exciter.h
│       │   ├── noise_burst_exciter.h
│       │   ├── friction_exciter.h
│       │   ├── fm_impulse_exciter.h
│       │   └── feedback_exciter.h
│       │
│       ├── bodies/
│       │   ├── membrane_body.h
│       │   ├── plate_body.h
│       │   ├── shell_body.h
│       │   ├── string_body.h
│       │   ├── bell_body.h
│       │   ├── noise_body.h
│       │   ├── plate_modes.h
│       │   ├── shell_modes.h
│       │   ├── bell_modes.h
│       │   ├── membrane_mapper.h
│       │   ├── plate_mapper.h
│       │   ├── shell_mapper.h
│       │   ├── bell_mapper.h
│       │   ├── string_mapper.h
│       │   └── noise_body_mapper.h
│       │
│       └── unnatural/
│           ├── unnatural_zone.h
│           ├── material_morph.h
│           ├── mode_inject.h
│           └── nonlinear_coupling.h
│
├── tests/
│   ├── CMakeLists.txt                 # MODIFIED
│   ├── vstgui_test_stubs.cpp          # unchanged
│   ├── unit/
│   │   ├── test_main.cpp
│   │   ├── vst/
│   │   │   ├── membrum_vst_tests.cpp          # MODIFIED
│   │   │   ├── test_membrum_parameters_v2.cpp # NEW
│   │   │   └── test_state_roundtrip_v2.cpp    # NEW
│   │   ├── processor/
│   │   │   └── membrum_processor_tests.cpp    # unchanged
│   │   ├── architecture/
│   │   │   ├── test_exciter_bank.cpp
│   │   │   └── test_body_bank.cpp
│   │   ├── exciters/
│   │   │   ├── test_impulse_exciter.cpp
│   │   │   ├── test_mallet_exciter.cpp
│   │   │   ├── test_noise_burst_exciter.cpp
│   │   │   ├── test_friction_exciter.cpp
│   │   │   ├── test_fm_impulse_exciter.cpp
│   │   │   ├── test_feedback_exciter.cpp
│   │   │   └── test_velocity_mapping.cpp
│   │   ├── bodies/
│   │   │   ├── test_membrane_body.cpp
│   │   │   ├── test_plate_body.cpp
│   │   │   ├── test_shell_body.cpp
│   │   │   ├── test_string_body.cpp
│   │   │   ├── test_bell_body.cpp
│   │   │   └── test_noise_body.cpp
│   │   ├── tone_shaper/
│   │   │   ├── test_tone_shaper.cpp
│   │   │   ├── test_pitch_envelope_808.cpp
│   │   │   └── test_tone_shaper_bypass.cpp
│   │   ├── unnatural/
│   │   │   ├── test_mode_stretch.cpp
│   │   │   ├── test_decay_skew.cpp
│   │   │   ├── test_mode_inject.cpp
│   │   │   ├── test_nonlinear_coupling.cpp
│   │   │   └── test_material_morph.cpp
│   │   ├── test_exciter_body_matrix.cpp
│   │   ├── test_stability_guard.cpp
│   │   └── test_allocation_matrix.cpp
│   ├── approval/
│   │   └── test_phase1_regression.cpp
│   ├── perf/
│   │   ├── test_benchmark_144.cpp
│   │   └── test_noise_body_cpu.cpp
│   └── golden/
│       └── phase1_default.bin
│
└── resources/
    ├── au-info.plist                 # unchanged
    ├── auv3/audiounitconfig.h        # unchanged
    ├── win32resource.rc              # unchanged
    └── win32resource.rc.in           # unchanged
```

**Structure Decision**: Monorepo plugin refactor — preserves the Phase 1 layout and only adds files under `plugins/membrum/src/dsp/` (sub-directories for `exciters/`, `bodies/`, `unnatural/`) and parallel sub-directories under `plugins/membrum/tests/unit/`. No `dsp/` library changes (FR-101). No AU/VST3 resource changes. No CI config changes beyond adding Phase 2 test targets.

## Complexity Tracking

> Fill only if Constitution Check has violations that must be justified.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

**No constitution violations.** All new code is plugin-local, all dispatch is non-virtual (`std::variant`), all components are pre-allocated, all tests are written before implementation. The design explicitly honors every Phase 2 clarification and every applicable constitution principle.

## Post-Implementation Fields

These fields are **filled during Phase 2.G after measurement**:

- **Noise Body final mode count**: `___` (starts at 40, may reduce)
- **Worst-case CPU combination**: `___` (exciter × body × ts × uz)
- **Worst-case CPU percent**: `___%`
- **`std::visit` vs switch dispatch chosen**: `___`
- **Decay Skew implementation chosen**: scalar-bias / per-block updateModes
- **All 144 combinations under 1.25% budget?**: Yes / No — if No, list exceptions
