# Data Model: Membrum Phase 5 -- Cross-Pad Coupling

## 1. PadCategory Enum

```cpp
// plugins/membrum/src/dsp/pad_category.h
namespace Membrum {

enum class PadCategory : int {
    Kick,       // Membrane + pitch envelope active
    Snare,      // Membrane + noise exciter
    Tom,        // Membrane (no pitch env, no noise exciter)
    HatCymbal,  // NoiseBody
    Perc,       // Any other
    kCount
};

/// Derive pad category from its runtime configuration.
/// Priority-ordered rule chain (FR-033):
///   1. Membrane + pitch env active -> Kick
///   2. Membrane + NoiseBurst exciter -> Snare
///   3. Membrane -> Tom
///   4. NoiseBody -> HatCymbal
///   5. else -> Perc
[[nodiscard]] inline PadCategory classifyPad(const PadConfig& cfg) noexcept
{
    if (cfg.bodyModel == BodyModelType::Membrane) {
        if (cfg.tsPitchEnvTime > 0.0f)
            return PadCategory::Kick;
        if (cfg.exciterType == ExciterType::NoiseBurst)
            return PadCategory::Snare;
        return PadCategory::Tom;
    }
    if (cfg.bodyModel == BodyModelType::NoiseBody)
        return PadCategory::HatCymbal;
    return PadCategory::Perc;
}

} // namespace Membrum
```

## 2. CouplingMatrix

```cpp
// plugins/membrum/src/dsp/coupling_matrix.h
namespace Membrum {

/// Two-layer coupling coefficient resolver (FR-030).
/// Tier 1 (computed): derived from global knobs + pad categories.
/// Tier 2 (override): per-pair user overrides.
/// Resolved into a flat effectiveGain[32][32] for audio-thread use.
class CouplingMatrix {
public:
    static constexpr float kMaxCoefficient = 0.05f;  // FR-031
    static constexpr int kSize = kNumPads;            // 32

    CouplingMatrix() noexcept;

    // --- Tier 1: Recompute from global knobs + pad categories ---

    /// Recompute all computedGain values from current knob settings.
    /// Called when Snare Buzz, Tom Resonance, or pad config changes.
    void recomputeFromTier1(
        float snareBuzz,
        float tomResonance,
        const PadCategory categories[kSize]) noexcept;

    // --- Tier 2: Per-pair overrides ---

    /// Set a per-pair override (Phase 6 UI / programmatic).
    void setOverride(int src, int dst, float coeff) noexcept;

    /// Clear a per-pair override (revert to Tier 1 computed value).
    void clearOverride(int src, int dst) noexcept;

    /// Query whether a pair has an override.
    [[nodiscard]] bool hasOverrideAt(int src, int dst) const noexcept;

    /// Get the override value (only valid if hasOverrideAt returns true).
    [[nodiscard]] float getOverrideGain(int src, int dst) const noexcept;

    // --- Resolved access (audio thread) ---

    /// Get the effective coupling gain for a pair.
    [[nodiscard]] float getEffectiveGain(int src, int dst) const noexcept;

    /// Get pointer to the flat effective gain array (for batch iteration).
    [[nodiscard]] const float* effectiveGainArray() const noexcept;

    // --- Serialization ---

    /// Count of pairs with overrides (for state serialization).
    [[nodiscard]] int getOverrideCount() const noexcept;

    /// Iterate overrides: callback(src, dst, coeff) for each.
    template <typename Fn>
    void forEachOverride(Fn&& fn) const noexcept;

    // Note: State loading uses individual setOverride(src, dst, coeff) calls
    // per T052 -- no bulk loadOverrides method; caller iterates serialized pairs.

private:
    float computedGain_[kSize][kSize]{};
    float overrideGain_[kSize][kSize]{};
    bool  hasOverride_[kSize][kSize]{};
    float effectiveGain_[kSize][kSize]{};

    /// Resolve one pair: effectiveGain = hasOverride ? overrideGain : computedGain.
    void resolve(int src, int dst) noexcept;

    /// Resolve all pairs.
    void resolveAll() noexcept;
};

} // namespace Membrum
```

**Memory**: 4 * 32 * 32 * sizeof(float) + 32 * 32 * sizeof(bool) = 16384 + 1024 = ~17 KB.

## 3. Extended PadConfig

```cpp
// In pad_config.h -- add to PadParamOffset enum:
kPadCouplingAmount = 36,    // Phase 5: per-pad coupling participation [0.0, 1.0]
kPadActiveParamCountV5 = 37,  // offsets 0-36 are active in Phase 5
```

```cpp
// In PadConfig struct -- add after frictionPressure:
float couplingAmount = 0.5f;  // Phase 5: coupling participation [0.0, 1.0]
```

## 4. Extended ParameterIds

```cpp
// In plugin_ids.h -- Phase 5 range (270-279):
kGlobalCouplingId  = 270,   // RangeParameter [0.0, 1.0], default 0.0
kSnareBuzzId       = 271,   // RangeParameter [0.0, 1.0], default 0.0
kTomResonanceId    = 272,   // RangeParameter [0.0, 1.0], default 0.0
kCouplingDelayId   = 273,   // RangeParameter [0.5, 2.0] ms, default 1.0
```

## 5. State Version 5 Binary Layout

### v5 format (extends v4):

```
Offset  Size    Description
------  ------  -----------
0       4       int32 version = 5
4       4       int32 maxPolyphony [4, 16]
8       4       int32 voiceStealingPolicy [0, 2]
12      ...     32 x PadConfig (same as v4: 2 int32 + 34 float64 + 2 uint8)
...     4       int32 selectedPadIndex [0, 31]
--- Phase 5 appended data ---
...     8       float64 globalCoupling [0.0, 1.0], default 0.0
...     8       float64 snareBuzz [0.0, 1.0], default 0.0
...     8       float64 tomResonance [0.0, 1.0], default 0.0
...     8       float64 couplingDelay [0.5, 2.0] ms, default 1.0
...     256     32 x float64 perPadCouplingAmounts, default 0.5 each
...     2       uint16 overrideCount
...     N*6     overrideCount x { uint8 src, uint8 dst, float32 coeff }
```

### Migration chain:
- v1 -> v2 -> v3 -> v4: existing, unchanged
- v4 -> v5: read v4 data, fill Phase 5 params with defaults
- v5: read everything including Phase 5 data

## 6. Processor Extensions

```cpp
// In processor.h -- new members:
Krate::DSP::SympatheticResonance  couplingEngine_;
Krate::DSP::DelayLine             couplingDelay_;
CouplingMatrix                    couplingMatrix_;

// Global coupling parameters (atomics for thread-safe parameter updates)
std::atomic<float> globalCoupling_{0.0f};
std::atomic<float> snareBuzz_{0.0f};
std::atomic<float> tomResonance_{0.0f};
std::atomic<float> couplingDelayMs_{1.0f};

// Cached pad categories (recomputed on pad config changes)
std::array<PadCategory, kNumPads> padCategories_{};

// Energy limiter state
float energyEnvelope_ = 0.0f;
```

## 7. ModalResonatorBank Extension

```cpp
// In modal_resonator_bank.h -- new public methods:

/// Get the frequency of mode k (Hz). Returns 0 if k is out of range or inactive.
[[nodiscard]] float getModeFrequency(int k) const noexcept
{
    if (k < 0 || k >= numModes_) return 0.0f;
    float eps = epsilonTarget_[k];
    if (eps <= 0.0f) return 0.0f;
    return std::asin(eps * 0.5f) * sampleRate_ / std::numbers::pi_v<float>;
}

/// Get the number of configured modes.
[[nodiscard]] int getNumModes() const noexcept { return numModes_; }
```

## 8. Effective Gain Formula

Per FR-014, the per-sample coupling gain applied for a pair (src, dst) is:

```
globalCoupling * effectiveGain[src][dst] * padCouplingAmount[src] * padCouplingAmount[dst]
```

Where `effectiveGain[src][dst]` is resolved by `CouplingMatrix` and already encodes the
Tier 1 knob value times `kMaxCoefficient`, or the Tier 2 override value. The Tier 1 knob
is NOT applied separately at audio time -- doing so would double-count it.

The `CouplingMatrix::recomputeFromTier1()` computes `computedGain` as:
- Kick src, Snare dst: `snareBuzz * kMaxCoefficient`
- Tom src, Tom dst (src != dst): `tomResonance * kMaxCoefficient`
- All other pairs: 0.0 (Tier 1 only enables specific category pairs)
- Self-pairs (src == dst): always 0.0

Then `effectiveGain[src][dst] = hasOverrideAt(src, dst) ? overrideGain : computedGain`.

## 9. Entity Relationships

```
Processor
  |-- VoicePool (32 pads, 16 voices)
  |     |-- PadConfig[32] (includes couplingAmount at offset 36)
  |     |-- DrumVoice[16] -> BodyBank -> ModalResonatorBank (partial frequencies)
  |
  |-- SympatheticResonance (coupling engine, 64 resonator pool)
  |-- DelayLine (propagation delay, 0.5-2 ms)
  |-- CouplingMatrix (32x32, two-layer resolver)
  |-- PadCategory[32] (cached, derived from PadConfig)
```
