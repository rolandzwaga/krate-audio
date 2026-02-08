# Research: Ruinae Voice Architecture

**Date**: 2026-02-08 | **Spec**: 041-ruinae-voice-architecture

## Research Questions and Findings

### RQ-1: std::variant vs virtual dispatch for oscillator/filter/distortion selection

**Decision**: Use `std::variant` with `std::visit` (visitor-based dispatch).

**Rationale**:
- `std::variant` stores all types inline (no heap allocation per dispatch), which is critical for real-time safety.
- The DistortionRack already establishes this as the project-wide convention for selectable processors (see `dsp/include/krate/dsp/systems/distortion_rack.h`).
- Virtual dispatch requires heap allocation for polymorphic objects and prevents inlining. For audio processing where dispatch happens once per block (not per sample), the overhead difference is minimal, but variant provides better memory locality.
- All oscillator/filter/distortion types are known at compile time (closed set), which is the ideal use case for `std::variant`.

**Alternatives considered**:
1. **Virtual dispatch (inheritance)**: Rejected due to heap allocation requirement and project convention.
2. **CRTP (Curiously Recurring Template Pattern)**: Rejected because type selection is runtime, not compile-time.
3. **Switch-case with manual union**: Rejected as `std::variant` provides type-safe equivalent with visitor pattern.

**Key constraint discovered**: Many oscillator types (ChaosOscillator, ParticleOscillator, FormantOscillator, SpectralFreezeOscillator, LFO) are non-copyable but movable. `std::variant` supports move-only types, so this works. However, variant construction via `emplace` must be used for lazy initialization.

### RQ-2: SelectableOscillator storage strategy (lazy initialization)

**Decision**: Pre-allocate `std::variant` storage at prepare() time, but only call `prepare()` on the active type. On type switch, call `prepare()` on the newly activated type.

**Rationale**:
- The spec explicitly chose lazy initialization (Option B from clarifications).
- `std::variant` already reserves enough storage for the largest alternative. When a variant holds `std::monostate`, it uses no oscillator resources.
- On type switch: (1) destroy current active type in-place, (2) construct new type in-place via `emplace`, (3) call `prepare()` on the new type. All of this happens without heap allocation because the variant's internal storage is reused.
- Working set reduced from ~20KB/slot (all types prepared) to ~2-3KB/slot (only active type).

**Critical detail**: `std::variant::emplace<T>()` constructs in-place without allocation. However, some oscillator `prepare()` methods DO allocate (e.g., SpectralFreezeOscillator uses `std::vector`). This means type switching for FFT-based oscillators triggers allocation. The spec accepts this by noting "pre-allocate variant storage" -- the variant itself doesn't allocate, but the contained type may during its `prepare()`.

**Mitigation**: For the most common oscillator types (PolyBLEP, ChaosOscillator), `prepare()` does NOT allocate. FFT-based types (SpectralFreeze, Additive) allocate once when first activated, and the memory persists until type switch. This is acceptable because type switching is a user action, not a per-block operation.

### RQ-3: Mixer architecture (CrossfadeMix vs SpectralMorph)

**Decision**: Implement both modes as branches in the mixer section, not as separate variant types.

**Rationale**:
- CrossfadeMix is trivial arithmetic (`oscA * (1-mix) + oscB * mix`).
- SpectralMorph delegates to the existing `SpectralMorphFilter` (Layer 2) which has a dual-input `processBlock(const float* inputA, const float* inputB, float* output, size_t numSamples)` API.
- A simple `if (mixMode == MixMode::CrossfadeMix)` branch is cleaner than wrapping two strategies in a variant.
- The SpectralMorphFilter is non-copyable, move-only, and ~16KB -- it must be a direct member of RuinaeVoice, not inside a variant.

### RQ-4: Filter section selectable filter pattern

**Decision**: Use `std::variant<SVF, LadderFilter, FormantFilter, FeedbackComb>` following the DistortionRack pattern.

**Rationale**:
- All four filter types are relatively small (<1KB each).
- SVF and LadderFilter are Layer 1 (copyable); FormantFilter is Layer 2 (non-copyable, movable); FeedbackComb (CombFilter) is Layer 1 but contains a DelayLine (move-only).
- A variant of move-only types works correctly.
- Visitor-based dispatch with `process()` provides consistent API.

**Key API difference**: SVF has `process(float)` per-sample; FormantFilter has `process(float* buffer, size_t numSamples)` block-based; LadderFilter has `process(float)` per-sample; CombFilter has `process(float)` per-sample. The visitor must handle both patterns. For block processing, the voice will call per-sample in a loop, since filter cutoff is modulated per-sample.

### RQ-5: Per-voice modulation routing computation strategy

**Decision**: Per-block computation (compute modulation values once at start of `processBlock()`, apply to entire block).

**Rationale**:
- Spec clarification explicitly chose this (Option A).
- Modulation sources (envelopes, LFO) change slowly relative to the block size.
- Computing once per block means 1 evaluation per source per block, not per sample.
- Maximum latency is one block (512 samples = ~11.6ms at 44.1kHz), which is imperceptible for modulation.
- This is consistent with how synthesizers typically handle modulation routing.

### RQ-6: Cache efficiency for voice pool

**Decision**: Pre-allocate all voices in a contiguous `std::array<RuinaeVoice, 16>`. Process voices sequentially. No SoA transformation.

**Rationale**:
- From project MEMORY.md: "When total working set fits in L1 cache, AoS->SoA layout change provides negligible speedup."
- Each RuinaeVoice is estimated at ~54KB (all oscillators pre-allocated) or ~8-12KB (lazy init, 2 active oscillators). For 16 voices with lazy init: ~128-192KB total, which fits in L2 cache.
- The PolySynthEngine already uses this exact pattern (`std::array<SynthVoice, 16>`) successfully.
- SoA would require splitting the voice into separate arrays per component, which massively complicates the code for negligible benefit.

### RQ-7: NaN/Inf safety strategy

**Decision**: Use `detail::flushDenormal()` after each processing stage; add output buffer scan at end of processBlock.

**Rationale**:
- The ChaosOscillator can diverge, producing NaN/Inf.
- `detail::isNaN()` and `detail::isInf()` from `db_utils.h` provide bit-manipulation-based checks that work with `-ffast-math`.
- Strategy: (1) After oscillator output, flush NaN/Inf to 0.0. (2) After distortion output, flush denormals. (3) Final output buffer scan as safety net.
- This follows the existing SynthVoice pattern where NaN/Inf inputs are silently ignored.

### RQ-8: Oscillator API uniformity

**Decision**: SelectableOscillator will normalize the API across all 10 types.

**Findings on API differences**:
- PolyBlepOscillator: `setFrequency(float)`, `process()` returns float, `processBlock(float*, size_t)`
- ChaosOscillator: `setFrequency(float)`, `processBlock(float*, size_t, const float* extInput = nullptr)` -- has optional external input
- ParticleOscillator: `setFrequency(float)` (actually `setFrequency(float centerHz)`), `process()`, `processBlock(float*, size_t)`
- FormantOscillator: Non-copyable, movable. Has `process()` and `processBlock()`.
- SpectralFreezeOscillator: Non-copyable, movable. Has `process()` and `processBlock()`. Uses vectors internally.
- WavetableOscillator: `setFrequency(float)`, `process()`, `processBlock()`.
- NoiseOscillator: No `setFrequency()` (noise has color, not pitch). `process()`, `processBlock()`.
- PhaseDistortionOscillator, SyncOscillator, AdditiveOscillator: `setFrequency(float)`, `process()`.

**Key unification decisions**:
1. All types support `processBlock()` -- if not natively, wrap per-sample `process()` in a loop.
2. `setFrequency()` is no-op for NoiseOscillator.
3. Phase reset on type switch: reset the new oscillator's phase to 0 (PhaseMode::Reset) or attempt to preserve (PhaseMode::Continuous, best-effort).

### RQ-9: SpectralMorphFilter per-voice feasibility

**Decision**: Feasible with 1024-point FFT, but CPU-expensive. User controls polyphony manually.

**Findings**:
- SpectralMorphFilter with 1024-point FFT at 44.1kHz: estimated 1-2% CPU per voice.
- With 8 voices using SpectralMorph: ~8-16% CPU, which exceeds the 5% total plugin budget.
- Spec explicitly chose "no automatic limit" (Option A from clarifications).
- The SpectralMorphFilter takes `fftSize` as a `prepare()` parameter. Using 1024 instead of default 2048 halves the cost.
- This is acceptable because SpectralMorph mode is a premium feature; users who enable it accept the CPU cost and reduce polyphony accordingly.

## Technology Decisions Summary

| Decision | Choice | Confidence |
|----------|--------|------------|
| Type selection mechanism | `std::variant` + `std::visit` | HIGH |
| Oscillator initialization | Lazy (prepare on first use) | HIGH |
| Mixer strategy | Branch-based (if/else on MixMode) | HIGH |
| Filter selection | `std::variant<SVF, LadderFilter, FormantFilter, FeedbackComb>` | HIGH |
| Distortion selection | `std::variant` following DistortionRack pattern | HIGH |
| Modulation update rate | Per-block | HIGH |
| Voice pool layout | Contiguous `std::array` (AoS) | HIGH |
| NaN safety | Bit-manipulation checks + flush per stage | HIGH |
| SpectralMorph FFT size | 1024-point per voice | HIGH |
