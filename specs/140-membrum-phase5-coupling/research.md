# Research: Membrum Phase 5 -- Cross-Pad Coupling

## R1: Partial Frequency Extraction from ModalResonatorBank

**Decision**: Add a `getModeFrequency(int k)` const accessor to `ModalResonatorBank` that recovers the frequency from the stored epsilon coefficient using `f = asin(epsilon / 2) * sampleRate / pi`.

**Rationale**: The bank already stores mode frequencies in encoded form (`epsilonTarget_[k]`), and the recovery formula is already used internally in `initBowedModeFilters()`. Adding a public accessor avoids duplicating the formula and provides a clean API for coupling integration. The accessor is O(1), noexcept, and read-only -- safe to call on the audio thread during noteOn.

**Alternatives considered**:
- Store frequencies separately in the bank: rejected (wastes 96 * 4 = 384 bytes of memory for a value derivable from existing state).
- Derive frequencies from PadConfig directly (recompute MembraneMapper::map): rejected (duplicates the mapper logic and wouldn't account for pitch envelope or mode stretch applied at noteOn time).
- Store partial info in DrumVoice: possible, but the bank already has the post-warp frequencies which are more accurate for coupling.

**Implementation**: Add `[[nodiscard]] float getModeFrequency(int k) const noexcept` and `[[nodiscard]] int getNumModes() const noexcept` to `ModalResonatorBank`. The `BodyBank::getSharedBank()` accessor already exists, so `DrumVoice` can expose partials via `bodyBank_.getSharedBank().getModeFrequency(k)`.

## R2: Pad Category Classification

**Decision**: Implement a free function `PadCategory classifyPad(const PadConfig& cfg)` using the priority-ordered rule chain from FR-033.

**Rationale**: The classification is a pure function of PadConfig fields (bodyModel, exciterType, pitch envelope active). Making it a free function keeps it stateless and testable. The DefaultKit archetypes inform the rules but the classification is derived from runtime config, not from the archetype enum -- users can modify pad configs after initialization.

**Rule chain**:
1. `bodyModel == Membrane && tsPitchEnvTime > 0` --> Kick (pitch envelope active = pitch sweep)
2. `bodyModel == Membrane && exciterType == NoiseBurst` --> Snare (noise exciter + membrane)
3. `bodyModel == Membrane` --> Tom (membrane without kick/snare indicators)
4. `bodyModel == NoiseBody` --> HatCymbal
5. else --> Perc

**Alternatives considered**:
- Store the category explicitly in PadConfig: rejected (becomes stale if user changes body/exciter).
- Use the DefaultKit archetype enum: rejected (not available at runtime after initial load).

## R3: Coupling Matrix Data Model

**Decision**: A `CouplingMatrix` class with a two-layer resolver: `computedGain[32][32]`, `overrideGain[32][32]`, `hasOverride[32][32]`, and a resolved `effectiveGain[32][32]`. The resolver runs at parameter-change time (not per-sample).

**Rationale**: The spec requires distinguishing Tier 1 computed values from Tier 2 per-pair overrides. A flat resolved array is cache-friendly for the per-sample audio loop. The resolver runs infrequently (parameter changes only) so its O(1024) cost is negligible.

**Storage**: `effectiveGain` is a flat `float[32][32]` = 4 KB. Total coupling matrix overhead: ~16 KB for all four arrays. Acceptable for a plugin with ~7 MB voice pool.

**Alternatives considered**:
- Sparse map for overrides: rejected (complicates resolver, small memory savings for max 1024 entries).
- Single flat array without resolver: rejected (can't distinguish computed vs override for the Tier 1 recomputation).

## R4: Signal Chain Integration Point

**Decision**: Insert coupling processing after `voicePool_.processBlock()` and before silence flag computation. The coupling engine processes the mono sum `(L+R)/2` through a `DelayLine`, then through `SympatheticResonance::process()`, and adds the output to both L and R channels.

**Rationale**: Per FR-002, coupling output is additive to the master output. Per FR-073, coupling routes to main bus only. Processing after voicePool but before silence flags ensures the coupling output is included in the silence detection.

**Signal chain**:
```
voicePool_.processBlock(outL, outR, ...) -->
for each sample s:
  mono = (outL[s] + outR[s]) * 0.5f
  delayed = couplingDelay_.readLinear(delaySamples)
  couplingDelay_.write(mono)
  coupling = sympatheticResonance_.process(delayed)
  coupling = energyLimiter(coupling)
  outL[s] += coupling
  outR[s] += coupling
```

**Alternatives considered**:
- Block-based processing with separate buffers: rejected (SympatheticResonance is per-sample due to feedback in the resonators).
- Processing inside VoicePool: rejected (coupling is a kit-level effect, not per-voice).

## R5: Energy Limiter Design

**Decision**: A simple peak-tracking limiter with soft-knee gain reduction. Track coupling RMS over a short window (~2ms), and when it exceeds -20 dBFS, apply gain reduction proportional to the excess.

**Rationale**: The coupling coefficients (0-0.05) inherently limit energy, but with 32 pads and feedback paths, a safety limiter is needed. A soft-knee limiter is transparent (no audible artifacts per FR-040) and computationally cheap.

**Implementation**: One-pole envelope follower on abs(coupling output), with a gain reduction curve:
```
envelope += alpha * (abs(output) - envelope)
if envelope > threshold:
  gain = threshold / envelope  // simple compressor ratio infinity:1
output *= gain
```

**Alternatives considered**:
- Hard clipper: rejected (audible artifacts).
- Lookahead limiter: rejected (adds latency, overkill for this use case).
- Per-resonator energy limiting: rejected (resonators are managed by SympatheticResonance's own eviction).

## R6: State Version 5 Serialization

**Decision**: Extend the v4 state format by appending Phase 5 data after the `selectedPadIndex` field. The v4 state is read first (using existing code), then Phase 5 data follows.

**Format (appended to v4)**:
```
[float64] globalCoupling     (default 0.0)
[float64] snareBuzz           (default 0.0)
[float64] tomResonance        (default 0.0)
[float64] couplingDelay       (default 1.0)
[32 x float64] perPadCouplingAmounts (default 0.5 each)
[uint16] overrideCount
[overrideCount x (uint8 src, uint8 dst, float32 coeff)] overrides
```

**Migration**: v4 -> v5: read v4 data normally, then set all Phase 5 params to defaults. The version number changes to 5. Loading v4 state in v5 processor: after reading v4 data, there's no more data in the stream -- all Phase 5 params take defaults. The `setState` code detects this by checking `version == 4`.

**Alternatives considered**:
- Inline per-pad coupling in the PadConfig float64 block (expanding 34 to 35 floats): rejected (breaks backward compatibility of the per-pad binary layout).
- Separate coupling state chunk: rejected (VST3 has one state stream per component).

## R7: VoicePool Integration for Coupling

**Decision**: Add coupling engine hooks to VoicePool's noteOn/noteOff paths. When a voice is allocated, extract the first 4 partial frequencies from the body's ModalResonatorBank and register with the coupling engine. When a voice is released, call noteOff on the coupling engine.

**Rationale**: The VoicePool already manages voice lifecycle. Adding coupling hooks here is natural. The partial extraction happens after `applyPadConfigToSlot` configures the body, so the frequencies reflect the actual pad configuration including size, mode stretch, etc.

**Challenge**: The SympatheticResonance engine is owned by Processor, not VoicePool. The VoicePool needs a reference/pointer to it.

**Decision**: Pass a `SympatheticResonance*` to VoicePool (set during `prepare()`). When null, coupling is disabled. This keeps the coupling engine as an optional dependency.

**Alternatives considered**:
- Callback/lambda: rejected (adds complexity, coupling is always SympatheticResonance).
- Move SympatheticResonance into VoicePool: rejected (coupling is a kit-level post-mix effect, not per-voice).

## R8: CPU Budget Analysis

**Decision**: The SympatheticResonance engine with 64 resonators adds approximately 0.5-1.0% CPU at 44.1 kHz (based on Innexus benchmarks from spec 132). This is within the 1.5% budget.

**Rationale**: The SIMD-accelerated path processes all 64 resonator slots in bulk. The anti-mud HPF adds negligible overhead. The energy limiter is a single one-pole follower + branch per sample. The DelayLine for propagation delay is a single read + write per sample.

**Early-out**: When globalCoupling == 0, skip all coupling processing (< 0.01% CPU). This is already handled by SympatheticResonance::isBypassed().

## R9: ModalResonatorBank Frequency Accessor -- NoiseBody Handling

**Decision**: For NoiseBody pads (Hat/Cymbal category), the ModalResonatorBank still holds mode frequencies from the noise body's configuration. These are valid for coupling. For bodies without modal content (pure noise), we use the NoiseBody's filter center frequencies as proxy partials.

**Rationale**: All body types in Membrum route through the shared ModalResonatorBank. Even NoiseBody sets up mode frequencies (the "noise" character comes from dense, irregularly-spaced modes). The coupling engine treats these the same as membrane modes.

## R10: Per-Pad Coupling Amount in PadConfig

**Decision**: Add a `float couplingAmount = 0.5f` field to `PadConfig` at the logical position after `frictionPressure`. Extend `PadParamOffset` with `kPadCouplingAmount = 36`. Update `padOffsetFromParamId` to accept offset 36.

**Rationale**: The reserved range 36-63 was explicitly allocated for Phase 5+. Using offset 36 is the first available slot. The PadConfig struct gains one float (4 bytes) -- negligible impact on the ~224 KB DrumVoice sizeof.

**Key change**: The `padOffsetFromParamId` function currently rejects offsets >= `kPadActiveParamCount` (36). This must be updated to accept offset 36 (or a new `kPadActiveParamCountV5 = 37`).
