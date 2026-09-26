#pragma once

// ==============================================================================
// Layer 4: Effect - CavernVerb (Vorago "Cavern Space" engine)
// ==============================================================================
// Spec slug: vorago-phase9-cavern-space
// Spec:      specs/vorago-phase9-cavern-space/spec.md
// Plan:      specs/vorago-phase9-cavern-space/plan.md
// Roadmap:   specs/Vorago-roadmap.md, Part A, Phase 9 (lines 410-430)
// Layer:     4 (Effects) - may include Layers 0-3, and Layer 4 peers.
//
// Open Question 3 (specs/Vorago-roadmap.md, lines 569-570) - "cavern space:
// configuration layer over shared AetherReverb vs separate L4 effect" - is
// RULED: a separate Layer 4 class that owns an AetherReverb BY VALUE, plus one
// append-only damper hook on the shipped engine. Nothing in aether_reverb.h
// moves; CavernVerb adds the cavern early-reflection stage, stone absorption
// and the moving-damper bank around it.
//
// An enormous underground bunker: sparse stone-flavoured early reflections ->
// the owned AetherReverb (diffusion, FDN, spectral damping) -> moving dampers
// whose cutoffs wander under BrownianDrift -> dark late field.
//
// LOAD-BEARING FACTS, each verified against the cited line:
//
//  (1) The owned engine is prepared with shimmerEnabled = false and
//      bloomEnabled = false, so neither stage is CONSTRUCTED (FR-010). That is
//      also why CavernVerb has no 44.1 kHz floor: shimmer was the only stage
//      carrying one (aether_reverb.h:1394, :1641).
//
//  (2) The owned engine is permanently fully wet (setMix(1)) with zero
//      pre-delay (FR-019): the cavern's pre-delay IS the early-reflection
//      geometry, and CavernVerb owns the dry/wet mix itself.
//
//  (3) CavernVerb runs its OWN absolute sample counter and slices every call at
//      kControlChunkSamples boundaries exactly as the engine does
//      (aether_reverb.h:2211-2224), so the two control grids are phase-aligned
//      by construction (FR-007).
//
//  (4) The early-reflection stage uses NO RNG whatsoever (FR-035). Its pattern
//      is a fixed, pairwise-coprime integer series mapped through an affine
//      law. Do not add a random source to it.
//
//  (5) The FR-028 gain-sum assert needs exp(), which is not constexpr. BRANCH
//      TAKEN, of the two plan S2.2 offers: (i) a constexpr Maclaurin series,
//      detail::cavernExpSeries, evaluated at compile time; its agreement with
//      std::exp is additionally REQUIREd by the geometry test case. Branch (ii)
//      - carrying the sum only as a runtime REQUIRE - was NOT taken.
//
//  (6) Stone absorption is TWELVE INDEPENDENT one-pole states, one per tap
//      (EarlyTap::state), never one filter shared across taps. The cutoff set
//      is geometric in TAP INDEX, so a shared state would make the tap ordering
//      irrelevant - which is exactly what CavernVerb_EarlyAbsorption (ii) gates.
//
// tasks.md T004 implements the constants, the tap table, the lifecycle, the
// seventeen setters and the accessors. tasks.md T005 implements the control
// grid (processStereoBlock / runControlStep), the early-reflection audio path,
// the four alignment lines and the equal-power mix (renderSlice). tasks.md T006
// implements the per-tap absorption one-poles (updateAbsorption and the
// per-tap branch inside renderSlice). tasks.md T007 implements the ShapedRamp
// primitive, the FR-065 dormancy rules (both the ER-chain skip and the
// stated setMix == 0 exception) and FR-063's equal-power dry/wet law.
// tasks.md T008 implements the BrownianDrift damper bank: the two setters,
// the unconditional per-chunk advance and publishDamperOffsets().
// ==============================================================================

#include <krate/dsp/effects/aether_reverb.h>       // same-layer, permitted (tools/lint-layers.js:74)
#include <krate/dsp/primitives/delay_line.h>       // L1
#include <krate/dsp/primitives/smoother.h>         // L1  (OnePoleSmoother, LinearRamp)
#include <krate/dsp/processors/brownian_drift.h>   // L2
#include <krate/dsp/core/random.h>                 // L0  (deriveStreamSeed)

#include <algorithm>  // clamp, min, max, fill
#include <cmath>      // exp, pow, fabs
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

namespace detail {

// -----------------------------------------------------------------------------
// Compile-time helpers for the cavern early-reflection table (FR-020, FR-022,
// FR-028). Each takes the table by reference, so the shipped series lives in
// exactly one place - CavernVerb::kEarlyTapSeries - and nothing here can drift
// away from it.
// -----------------------------------------------------------------------------

/// @brief The affine tap law: d_i = firstMs + spanMs * (n_i - n_0)/(n_N-1 - n_0).
///
/// Evaluated in double so the compile-time asserts and the runtime table derive
/// from the same arithmetic rather than from two independent roundings.
template <std::size_t N>
[[nodiscard]] constexpr double cavernTapDelayMs(const std::size_t (&table)[N], std::size_t i,
                                                double firstMs, double spanMs) noexcept {
    const auto num = static_cast<double>(table[i] - table[0]);
    const auto den = static_cast<double>(table[N - 1u] - table[0]);
    return firstMs + ((spanMs * num) / den);
}

/// @brief constexpr exp(x) for the small negative arguments the gain law uses.
///
/// The gain law evaluates exp(-kEarlyGainAlphaPerMs * d) with d in [60, 600] ms,
/// i.e. x in [-6, -0.6]. A plain Maclaurin series with 40 terms truncates below
/// 1e-15 over that range (|x|^40 / 40! < 1e-25 at |x| = 6) - ten orders tighter
/// than the 1e-5 the static_assert needs. No range reduction is used because
/// none is required at this magnitude.
[[nodiscard]] constexpr double cavernExpSeries(double x) noexcept {
    double term = 1.0;
    double sum = 1.0;
    for (int n = 1; n <= 40; ++n) {
        term *= x / static_cast<double>(n);
        sum += term;
    }
    return sum;
}

/// @brief FR-020's strict-ascent invariant, delegated to the shipped fold.
template <std::size_t N>
[[nodiscard]] constexpr bool cavernTableStrictlyAscending(const std::size_t (&table)[N]) noexcept {
    return aetherTableStrictlyAscending(table);
}

/// @brief FR-020's pairwise-coprimality invariant, delegated to the shipped fold.
template <std::size_t N>
[[nodiscard]] constexpr bool cavernTablePairwiseCoprime(const std::size_t (&table)[N]) noexcept {
    return aetherTablePairwiseCoprime(table);
}

/// @brief FR-022's incommensurability metric over the tap delays, in ms:
///
///   min over i != j, p,q in 1..order of |p*d_i - q*d_j| / min(d_i, d_j)
///
/// "Not an integer multiple" is NOT the property. What matters is that no
/// low-order repetition of one tap lands near a low-order repetition of another,
/// which is what turns a sparse pattern into audible flutter. The metric is a
/// RATIO of delays, so it is invariant to the ER size and to the sample rate:
/// verifying it once at the default size verifies it at every size in
/// [kEarlySizeMinMs, kEarlySizeMaxMs] and every admissible rate.
template <std::size_t N>
[[nodiscard]] constexpr double cavernTableIncommensurabilityMin(const std::size_t (&table)[N],
                                                                double firstMs, double spanMs,
                                                                std::size_t order) noexcept {
    double worst = 1.0e300;
    for (std::size_t i = 0; i < N; ++i) {
        const double di = cavernTapDelayMs(table, i, firstMs, spanMs);
        for (std::size_t j = 0; j < N; ++j) {
            if (i == j) {
                continue;
            }
            const double dj = cavernTapDelayMs(table, j, firstMs, spanMs);
            const double smaller = (di < dj) ? di : dj;
            for (std::size_t p = 1; p <= order; ++p) {
                for (std::size_t q = 1; q <= order; ++q) {
                    double diff = (static_cast<double>(p) * di) - (static_cast<double>(q) * dj);
                    if (diff < 0.0) {
                        diff = -diff;
                    }
                    const double metric = diff / smaller;
                    if (metric < worst) {
                        worst = metric;
                    }
                }
            }
        }
    }
    return worst;
}

/// @brief FR-028's sum of |g_i| over the table, at compile time.
template <std::size_t N>
[[nodiscard]] constexpr double cavernTableGainSum(const std::size_t (&table)[N], double firstMs,
                                                  double spanMs, double g0,
                                                  double alphaPerMs) noexcept {
    double sum = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
        sum += g0 * cavernExpSeries(-alphaPerMs * cavernTapDelayMs(table, i, firstMs, spanMs));
    }
    return sum;
}

}  // namespace detail

/**
 * @brief Vorago's cavern reverberator (Layer 4).
 *
 * Owns an AetherReverb by value and surrounds it with a cavern early-reflection
 * stage, per-tap stone absorption and a bank of slowly wandering dampers.
 *
 * Real-time safe once prepared: prepare() is the ONLY allocating method; no
 * locks, no exceptions and no I/O anywhere.
 */
class CavernVerb {
public:
    // -------------------------------------------------------------------------
    // Public constants (plan S2.2). Tests name these instead of magic numbers.
    // -------------------------------------------------------------------------

    // --- cadence, mirrored from the owned engine (FR-084) ---
    static constexpr std::size_t kControlChunkSamples = AetherReverb::kControlChunkSamples;

    // --- dark tuning (FR-012 .. FR-014) ---
    static constexpr float kCavernSizeFloor = 0.55f;     ///< FR-012, >= 0.55 (SC-007 (c))
    static constexpr float kCavernDampingFloor = 0.50f;  ///< FR-013
    /// Mirrors AetherReverb's PRIVATE kDecayMinSeconds (aether_reverb.h:2735).
    /// A private member is unreachable from here, so no static_assert can pin
    /// it; SC-016 (a) pins it at runtime instead.
    static constexpr float kCavernDecayMinSeconds = 0.5f;
    /// Mirrors AetherReverb's PRIVATE kDecayMaxSeconds (aether_reverb.h:2736).
    static constexpr float kCavernDecayMaxSeconds = 60.0f;
    /// Mirrors AetherReverb's PRIVATE kMaxChannels (aether_reverb.h:2725).
    /// SC-016 (b) pins it at runtime.
    static constexpr std::size_t kMaxChannels = 16;

    // --- early reflections (FR-020 .. FR-028) ---
    static constexpr std::size_t kEarlyTapCount = 12;
    /// Pairwise-coprime, strictly ascending integer series (FR-020). The affine
    /// law below maps it onto [kEarlyFirstArrivalFloorMs, kDefaultEarlySizeMs].
    static constexpr std::size_t kEarlyTapSeries[kEarlyTapCount] = {
        1u, 53u, 199u, 277u, 547u, 709u, 929u, 1049u, 1381u, 1583u, 1721u, 1997u};
    static constexpr float kEarlyFirstArrivalFloorMs = 60.0f;  ///< FR-021
    static constexpr float kDefaultEarlySizeMs = 220.0f;
    static constexpr float kEarlySizeMinMs = 80.0f;
    static constexpr float kEarlySizeMaxMs = 600.0f;
    /// FR-004's clamp range for PrepareConfig::maxEarlySeconds. The floor is
    /// BELOW kEarlySizeMinMs * 0.001 on purpose - see PrepareConfig.
    static constexpr float kMinMaxEarlySeconds = 0.05f;
    static constexpr float kMaxMaxEarlySeconds = 0.60f;
    static constexpr float kEarlyGainG0 = 0.5f;
    static constexpr float kEarlyGainAlphaPerMs = 0.01f;
    static constexpr float kEarlyGainSum = 1.865762f;  ///< sum|g_i|; asserted <= 2.0 below
    /// plan S0.2 B-1 ruling: order 8 is jointly infeasible with FR-028's gain
    /// cap over the admissible design space; order 6 leaves every other constant
    /// exactly as the spec pins it and clears the tolerance by 8.2 %.
    static constexpr std::size_t kIncommensurabilityOrder = 6;
    static constexpr float kEarlyIncommensurabilityTol = 0.05f;
    static constexpr float kEarlyAbsorptionFcMaxHz = 18000.0f;
    static constexpr float kEarlyAbsorptionFcMinHz = 1200.0f;
    /// The Nyquist guard is PART OF THE LAW, not an implementation detail
    /// (plan S5.3, S14 Q5), so it is named here and the tests read the same two
    /// constants the header does.
    static constexpr float kEarlyAbsorptionNyquistFraction = 0.45f;
    static constexpr float kEarlyAbsorptionSpanFraction = 0.40f;

    // --- dampers (FR-030 .. FR-034) ---
    static constexpr float kMaxDamperOctaves = 1.5f;
    static constexpr float kDefaultDamperDepth = 0.35f;
    static constexpr float kDefaultDamperRate = 0.15f;  ///< smoothness 0.85 -> tau ~= 25.5 s
    static constexpr std::size_t kCavernReverbSalt = 64;      ///< clear of {0..4} and [16, 24)
    static constexpr std::size_t kCavernDamperSaltBase = 96;  ///< 96 .. 111

    // --- FR-066's pinned default table ---
    static constexpr float kDefaultSize = 0.50f;
    static constexpr float kDefaultDarkness = 0.80f;
    static constexpr float kDefaultDecaySeconds = 20.0f;
    static constexpr float kDefaultDensity = 0.75f;
    static constexpr float kDefaultDimensionality = 0.50f;
    static constexpr float kDefaultBreath = 0.50f;
    static constexpr float kDefaultFog = 0.30f;
    static constexpr float kDefaultEarlyLevel = 0.80f;
    static constexpr float kDefaultEarlyAbsorption = 0.60f;
    static constexpr float kDefaultEarlySend = 0.70f;
    static constexpr float kDefaultWidth = 1.00f;
    static constexpr float kDefaultMix = 1.00f;
    static constexpr float kDefaultMaxEarlySeconds = 0.30f;

    // --- smoothing ---
    static constexpr float kEarlySizeSmoothingMs = 300.0f;  ///< matches the engine's Size smoother
    static constexpr float kAbsorptionSmoothingMs = 100.0f;
    static constexpr float kGateRampMs = 50.0f;  ///< FR-065 (iii)'s >= 50 ms re-entry

    // -------------------------------------------------------------------------
    // FR-084 static_asserts - PUBLIC facts only. The three mirrors of private
    // AetherReverb constants above cannot be asserted here (a private member is
    // unreachable from a non-friend); SC-016 pins those at runtime instead.
    // -------------------------------------------------------------------------
    static_assert(kControlChunkSamples == AetherReverb::kControlChunkSamples,
                  "CavernVerb's control grid must be the engine's control grid (FR-007)");
    static_assert((AetherReverb::kSizeScaleMin == 0.25f) && (AetherReverb::kSizeScaleMax == 4.0f),
                  "FR-012's size-floor arithmetic assumes S(v) = 0.25 * 2^(4v) over [0.25, 4]");
    static_assert((AetherReverb::kMinSampleRate == 8000.0f) &&
                      (AetherReverb::kMaxSampleRate == 192000.0f),
                  "FR-074's admissible rate range is the engine's");
    static_assert(AetherReverb::kDriftSaltBase == 16,
                  "the damper salt range is chosen to clear the engine's drift salts");
    static_assert(kCavernDamperSaltBase >= (AetherReverb::kDriftSaltBase + (kMaxChannels / 2u) + 8u),
                  "FR-034: the damper salts must not overlap the engine's drift salts");
    static_assert(AetherReverb::kRefDelays8[0] == 967u,
                  "SC-006's onset-gap arithmetic is derived from the shortest FDN line");
    static_assert(kEarlyGainSum <= 2.0f, "FR-028: the ER bus peak bound");
    static_assert(kMaxDamperOctaves <= AetherReverb::kMaxDamperOffsetOctaves,
                  "FR-040: a legitimate excursion must never be clipped by the engine's clamp");
    static_assert(kCavernSizeFloor >= 0.55f, "FR-012 / SC-007 (c)");

    // -------------------------------------------------------------------------
    // PrepareConfig (FR-004). Every field is clamped in place, never rejected
    // (FR-003). Designated-initialiser friendly with no narrowing (FR-072):
    // every default literal carries its member's own type.
    // -------------------------------------------------------------------------
    struct PrepareConfig {
        std::size_t numChannels = 8;         ///< 8 or 16, forwarded
        std::size_t maxBlockSamples = 2048;  ///< clamped to [64, 8192], forwarded
        /// Clamped to [0.05, 0.60] (FR-004); sizes the mono ER line.
        ///
        /// THE CLAMP RANGE AND THE ALLOCATED LENGTH ARE TWO DIFFERENT NUMBERS,
        /// deliberately. FR-004 fixes the clamp at [0.05, 0.60] and nothing
        /// below rejects a value inside it, but the ER geometry itself has a
        /// hard floor of kEarlySizeMinMs = 80 ms: setEarlySizeMs clamps into
        /// [kEarlySizeMinMs, max(kEarlySizeMinMs, min(kEarlySizeMaxMs,
        /// maxEarlySeconds * 1000))], so a prepared 0.05 s pins the ER size at
        /// exactly 80 ms. The line is therefore allocated for
        /// max(maxEarlySeconds_, kEarlySizeMinMs * 0.001) - see prepare() step 4
        /// - because a 50 ms line read at 80 ms would wrap into stale samples.
        /// Do NOT "simplify" that back into a 0.08 s clamp on the config field:
        /// FR-003 says every field is clamped in place, never rejected, and
        /// FR-004 states the range this one is clamped into.
        float maxEarlySeconds = 0.30f;
        float maxDelaySeconds = 0.50f;  ///< forwarded (the engine clamps to [0.05, 1.0])
        bool spectralDiffusionEnabled = true;
        std::size_t diffusionFftSize = 1024;  ///< forwarded; the engine clamps + bit_floors
        std::uint32_t seed = 1;
    };

    // -------------------------------------------------------------------------
    // Special members (FR-002): non-copyable, movable.
    // -------------------------------------------------------------------------
    CavernVerb() noexcept = default;
    ~CavernVerb() noexcept = default;
    CavernVerb(const CavernVerb&) = delete;
    CavernVerb& operator=(const CavernVerb&) = delete;
    CavernVerb(CavernVerb&&) noexcept = default;
    CavernVerb& operator=(CavernVerb&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Compile-time tap geometry
    // -------------------------------------------------------------------------

    /// @brief d_i in milliseconds at the DEFAULT ER size (FR-020, FR-021).
    ///        Out of range returns 0.
    [[nodiscard]] static constexpr float earlyTapDelayMsAtDefaultSize(std::size_t i) noexcept {
        return (i < kEarlyTapCount)
                   ? static_cast<float>(detail::cavernTapDelayMs(
                         kEarlyTapSeries, i, static_cast<double>(kEarlyFirstArrivalFloorMs),
                         static_cast<double>(kDefaultEarlySizeMs - kEarlyFirstArrivalFloorMs)))
                   : 0.0f;
    }

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Size every buffer and materialise the control state. FR-003.
    ///
    /// THE ONLY ALLOCATING METHOD. Not real-time safe; call it from the message
    /// thread. Every control applied before this call is re-applied afterwards
    /// (FR-060), because AetherReverb::prepare() snaps its own smoothers back to
    /// ITS defaults (aether_reverb.h:1905-1938) and would otherwise discard them.
    void prepare(double sampleRate, const PrepareConfig& config) noexcept {
        // 1. the engine's own rate clamp, re-applied here so the ER geometry
        //    derives from the same number the engine uses (aether_reverb.h:1625).
        sampleRate_ = std::clamp(sampleRate, static_cast<double>(AetherReverb::kMinSampleRate),
                                 static_cast<double>(AetherReverb::kMaxSampleRate));
        msToSamples_ = static_cast<float>(sampleRate_) * 0.001f;

        // 2. prepared configuration.
        numChannels_ = (config.numChannels == 16u) ? std::size_t{16} : std::size_t{8};
        // FR-004's range, verbatim. isFinite first: std::clamp(NaN, lo, hi) is
        // NaN, and a NaN length would propagate into the allocation below.
        maxEarlySeconds_ =
            std::clamp(isFinite(config.maxEarlySeconds) ? config.maxEarlySeconds
                                                        : kDefaultMaxEarlySeconds,
                       kMinMaxEarlySeconds, kMaxMaxEarlySeconds);
        seed_ = config.seed;

        // 3. the owned engine. Shimmer and bloom are NOT CONSTRUCTED (FR-010),
        //    not merely zeroed. maxBlockSamples is forwarded even though the
        //    engine never sees more than one control chunk: it is part of the
        //    documented surface and sizes engine-internal scratch.
        AetherReverb::PrepareConfig ac{};
        ac.numChannels = numChannels_;
        ac.maxBlockSamples = std::clamp(config.maxBlockSamples, std::size_t{64}, std::size_t{8192});
        ac.maxDelaySeconds = config.maxDelaySeconds;
        ac.shimmerEnabled = false;
        ac.bloomEnabled = false;
        ac.spectralDiffusionEnabled = config.spectralDiffusionEnabled;
        ac.diffusionFftSize = config.diffusionFftSize;
        ac.seed = deriveStreamSeed(config.seed, kCavernReverbSalt);  // FR-034
        // FR-023's per-sample rule applied to the OWNED engine's own geometry.
        // The engine samples its delay lengths once per 64-sample control chunk
        // (aether_reverb.h updateGeometry()), and at this phase's operating
        // point - engine Size 0.75 from kCavernSizeFloor, sizeBreathDepth 0.5
        // from FR-016/FR-066 - that snapshot moves by up to 11.73 SAMPLES per
        // chunk. Read as a staircase it is a ~750 Hz buzz: MEASURED here, a
        // transition-free 32 s render scored 441 ClickDetector detections at
        // 5.0 sigma with 243 landing exactly on the 64-sample grid, and the
        // SC-003 (d) dormancy render could not be calibrated clean at any sigma
        // up to P-5's 8.0 cap. With the glide on: 192 detections, 2 on the grid,
        // and SC-003 (d) reports zero. This is the same defect FR-023 forbids
        // for the ER taps, one level down.
        ac.glideGeometryPerSample = true;
        engine_.prepare(sampleRate_, ac);

        // 4. the latency every bus is aligned to (FR-062).
        alignSamples_ = engine_.getLatencySamples();

        // 5. the mono ER line, with FOUR samples of headroom above the largest
        //    reachable tap: readLinear clamps at maxDelaySamples_ and takes
        //    index1 = min(index0 + 1, maxDelaySamples_) (delay_line.h:302-318),
        //    so the last tap would otherwise lose its interpolation partner.
        //    The extra HALF sample is a rounding guard, not slack: DelayLine
        //    takes SECONDS and recomputes maxDelaySamples_ as a truncating
        //    static_cast (delay_line.h:267-269), so a float quotient that lands
        //    a few ULP low would silently cost one of the four samples.
        //    THE LENGTH IS max(maxEarlySeconds_, kEarlySizeMinMs * 0.001), not
        //    maxEarlySeconds_: FR-004's clamp floor is 0.05 s while the ER
        //    geometry's own floor is kEarlySizeMinMs = 80 ms, so a prepared
        //    0.05 s still reaches taps out to 80 ms (setEarlySizeMs pins the
        //    size there) and a 50 ms line would wrap them into stale samples.
        const float erSeconds = std::max(maxEarlySeconds_, kEarlySizeMinMs * 0.001f);
        erLine_.prepare(sampleRate_, erSeconds + (4.5f / static_cast<float>(sampleRate_)));

        // 6. ALL FOUR alignment lines, UNCONDITIONALLY. There is no
        //    alignSamples_ == 0 bypass anywhere in this design: the render block
        //    writes and reads all four on every sample, and DelayLine::write is
        //    an unchecked buffer_[writeIndex_] store (delay_line.h:287-290), so
        //    an unprepared line would be an out-of-bounds heap write on the
        //    audio thread for the whole render whenever the spectral stage is
        //    off - a configuration SC-013 renders.
        //    The half-sample rounding guard is the same one erLine_ carries.
        const float alignSeconds =
            (static_cast<float>(alignSamples_ + 4u) + 0.5f) / static_cast<float>(sampleRate_);
        dryAlignL_.prepare(sampleRate_, alignSeconds);
        dryAlignR_.prepare(sampleRate_, alignSeconds);
        erAlignL_.prepare(sampleRate_, alignSeconds);
        erAlignR_.prepare(sampleRate_, alignSeconds);

        // 7. the tap table: the affine delay law, the exponential gain law and
        //    FR-020's fixed even -> L / odd -> R side rule.
        for (std::size_t i = 0; i < kEarlyTapCount; ++i) {
            const float delayMs = earlyTapDelayMsAtDefaultSize(i);
            taps_[i].delayMs = delayMs;
            taps_[i].gain = kEarlyGainG0 * std::exp(-kEarlyGainAlphaPerMs * delayMs);
            taps_[i].rightSide = ((i & 1u) != 0u);
            taps_[i].state = 0.0f;
            taps_[i].fcHz = 0.0f;
            taps_[i].a = 0.0f;
        }

        // 8. smoothers.
        const auto sr = static_cast<float>(sampleRate_);
        erSizeSm_.configure(kEarlySizeSmoothingMs, sr);
        absorptionSm_.configure(kAbsorptionSmoothingMs, sr);
        earlyLevel_.configure(sr);
        earlySend_.configure(sr);
        mix_.configure(sr);

        // 9. dampers, for EVERY slot and not only numChannels_, so a later
        //    prepare() at N = 16 cannot inherit a stale stream (the
        //    reseedStreams() reasoning at aether_reverb.h:2993-2995).
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            damper_[i].prepare(sampleRate_);
            damper_[i].setDepth(damperDepth_);
            damper_[i].setSmoothness(1.0f - damperRate_);
        }

        // 10. re-apply every shadowed control, re-seed, then reset.
        prepared_ = true;
        anySamplesProcessed_ = false;
        lastAbsorptionApplied_ = -1.0f;  // force the first refresh to compute
        applyAllControls();
        reseed();
        reset();
    }

    /// @brief Clear all audio state and re-seed every stochastic stream. FR-006.
    ///
    /// PRESERVES every control and re-applies the complete shadow set, so this
    /// path and prepare() cannot diverge. engine_.setFreeze(ctlFreeze_) is the
    /// one re-issue that is not merely tidy: AetherReverb::reset() clears
    /// freezeTarget_ unconditionally (aether_reverb.h:1979) and freeze is NOT
    /// one of the controls its doc comment promises to preserve, so without it a
    /// frozen instance silently thaws while ctlFreeze_ still reads true, the
    /// forwarded isFrozen() disagrees with the recorded state, and nothing ever
    /// restores it.
    ///
    /// @note Allocation-free, but NOT an audio-thread operation.
    void reset() noexcept {
        sampleCounter_ = 0u;
        anySamplesProcessed_ = false;

        engine_.reset();
        applyAllControls();

        erLine_.reset();
        dryAlignL_.reset();
        dryAlignR_.reset();
        erAlignL_.reset();
        erAlignR_.reset();
        for (std::size_t i = 0; i < kEarlyTapCount; ++i) {
            taps_[i].state = 0.0f;
        }
        clearScratch();

        erSizeSm_.snapToTarget();
        absorptionSm_.snapToTarget();
        erSizeMsCurrent_ = erSizeSm_.getCurrentValue();
        earlyLevel_.snapTo(ctlEarlyLevel_);
        earlySend_.snapTo(ctlEarlySend_);
        mix_.snapTo(ctlMix_);

        reseed();

        // The offsets are re-published from the rewound dampers, so
        // getDamperOffsetOctaves cannot report a value from before the reset.
        // reseed() has just snapped every drift's output smoother to
        // outputTarget() = depth * mean_ = 0 (brownian_drift.h:243-249), so
        // this writes exact zeros - the same state engine_.reset() leaves its
        // own copy in.
        publishDamperOffsets();

        // One control refresh, so a post-reset render - and every accessor -
        // starts fully materialised (the engine does the same at :2099).
        refreshControlState();
    }

    /// @brief Fade out, clear the feed-forward state and fade back in. FR-006.
    ///
    /// Forwards to engine_.silence(), which owns the 20 ms gate and the
    /// amortized clear, and clears CavernVerb's own FEED-FORWARD state: the ER
    /// line and the twelve tap one-pole states.
    ///
    /// THE FOUR ALIGNMENT LINES ARE DELIBERATELY NOT CLEARED. The reasoning is
    /// the engine's own, recorded verbatim for its dry-alignment pair at
    /// aether_reverb.h:3625-3639: those lines carry INPUT HISTORY, nothing
    /// recirculates through them, and clearing them punches a latency-long hole
    /// that ends in a full-amplitude step after the gate has already returned to
    /// unity - a single-sample discontinuity where there was none. Do not
    /// "tidy" this into four more reset() calls.
    void silence() noexcept {
        engine_.silence();
        erLine_.reset();
        for (std::size_t i = 0; i < kEarlyTapCount; ++i) {
            taps_[i].state = 0.0f;
        }
    }

    /// @brief Render one stereo block. Real-time safe (FR-005, FR-007).
    ///
    /// The three contract cases, in the order they are checked:
    ///
    ///  (i)   ANY of the four pointers null -> return having written NOTHING.
    ///        The caller's output buffers are left exactly as they were.
    ///  (ii)  numSamples == 0 -> return, ADVANCING NOTHING. Not sampleCounter_,
    ///        not the control grid, not a smoother, not a damper. A counter
    ///        advanced here would rotate the FR-007 grid phase, so a render
    ///        would depend on how many times the host asked for nothing - the
    ///        engine's own rule at aether_reverb.h:2196-2198.
    ///  (iii) not prepared -> fill both outputs with silence, never leave stale
    ///        caller data behind.
    ///
    /// The caller's block is then sliced at ABSOLUTE control-chunk boundaries:
    /// the phase comes from sampleCounter_, which starts at prepare()/reset()
    /// and is never re-anchored to a caller block boundary. That - and
    /// runControlStep() always advancing by a FULL kControlChunkSamples - is
    /// what makes the render invariant to how the host partitions its blocks
    /// (FR-007, SC-011). A trailing partial sub-block runs NO control step; the
    /// next call resumes at the same phase.
    void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft,
                            float* outRight, std::size_t numSamples) noexcept {
        if ((inLeft == nullptr) || (inRight == nullptr) || (outLeft == nullptr) ||
            (outRight == nullptr)) {
            return;  // FR-005 (i)
        }
        if (numSamples == 0u) {
            return;  // FR-005 (ii): no state advances
        }
        if (!prepared_) {
            std::fill(outLeft, outLeft + numSamples, 0.0f);  // FR-005 (iii)
            std::fill(outRight, outRight + numSamples, 0.0f);
            return;
        }

        std::size_t done = 0;
        while (done < numSamples) {
            const auto phase = static_cast<std::size_t>(sampleCounter_ % kControlChunkSamples);
            if (phase == 0u) {
                runControlStep();
            }
            const std::size_t slice = std::min(numSamples - done, kControlChunkSamples - phase);
            renderSlice(inLeft + done, inRight + done, outLeft + done, outRight + done, slice,
                        phase);
            sampleCounter_ += slice;
            done += slice;
        }
        anySamplesProcessed_ = true;
    }

    // -------------------------------------------------------------------------
    // The seventeen controls (FR-061), in the spec's order. There is no
    // eighteenth: setPreDelayMs, setModDepth, setModSmoothness, every shimmer
    // and bloom control and bloomNoteOn/bloomNoteOff are DELIBERATELY ABSENT
    // (FR-011, FR-019).
    //
    // Every body is the house form: clamp(isFinite(x) ? x : default, lo, hi),
    // store the shadow copy, then forward or target a smoother. All noexcept,
    // all accepted before prepare(), all re-applied by prepare() and reset().
    // Smoothers SNAP rather than ramp while prepared_ && !anySamplesProcessed_
    // (the applyControl rule at aether_reverb.h:2950-2958), so "configure then
    // render" means what the caller expects.
    // -------------------------------------------------------------------------

    /// 0..1 -> engine Size over [kCavernSizeFloor, 1]: the cavern is never small.
    void setSize(float v) noexcept {
        ctlSize_ = std::clamp(isFinite(v) ? v : kDefaultSize, 0.0f, 1.0f);
        engine_.setSize(kCavernSizeFloor + (ctlSize_ * (1.0f - kCavernSizeFloor)));
    }

    /// 0..1 -> engine Damping over [kCavernDampingFloor, 1]: never bright.
    void setDarkness(float v) noexcept {
        ctlDarkness_ = std::clamp(isFinite(v) ? v : kDefaultDarkness, 0.0f, 1.0f);
        engine_.setDamping(kCavernDampingFloor + (ctlDarkness_ * (1.0f - kCavernDampingFloor)));
    }

    /// Seconds, clamped to [kCavernDecayMinSeconds, kCavernDecayMaxSeconds] -
    /// the mirrors of AetherReverb's private :2735-2736.
    void setDecaySeconds(float seconds) noexcept {
        ctlDecay_ = std::clamp(isFinite(seconds) ? seconds : kDefaultDecaySeconds,
                               kCavernDecayMinSeconds, kCavernDecayMaxSeconds);
        engine_.setDecaySeconds(ctlDecay_);
    }

    void setDensity(float v) noexcept {
        ctlDensity_ = std::clamp(isFinite(v) ? v : kDefaultDensity, 0.0f, 1.0f);
        engine_.setDensity(ctlDensity_);
    }

    void setDimensionality(float v) noexcept {
        ctlDim_ = std::clamp(isFinite(v) ? v : kDefaultDimensionality, 0.0f, 1.0f);
        engine_.setDimensionality(ctlDim_);
    }

    /// One control, TWO engine targets (FR-016). Dropping either one is
    /// invisible to every other criterion, which is why
    /// CavernVerb_BreathDualTarget exists.
    void setBreath(float v) noexcept {
        ctlBreath_ = std::clamp(isFinite(v) ? v : kDefaultBreath, 0.0f, 1.0f);
        engine_.setSizeBreathDepth(ctlBreath_);
        engine_.setDimensionalityTideDepth(ctlBreath_);
    }

    /// -> spectral diffusion, a per-bin PHASE smear, not damping.
    void setFog(float v) noexcept {
        ctlFog_ = std::clamp(isFinite(v) ? v : kDefaultFog, 0.0f, 1.0f);
        engine_.setSpectralDiffusion(ctlFog_);
    }

    /// Milliseconds, clamped to [kEarlySizeMinMs, min(kEarlySizeMaxMs,
    /// maxEarlySeconds * 1000)]. THE OUTER max() IS LOAD-BEARING, not hygiene:
    /// FR-004's clamp floor for maxEarlySeconds is 0.05 s, below
    /// kEarlySizeMinMs * 0.001, so hi < lo is reachable from a legal
    /// PrepareConfig - and std::clamp with hi < lo is UB by precondition (a hard
    /// abort under MSVC's debug iterator checks). At maxEarlySeconds = 0.05 the
    /// ER size is therefore pinned at exactly kEarlySizeMinMs, and prepare()
    /// allocates the line for that length rather than for 0.05 s.
    void setEarlySizeMs(float ms) noexcept {
        const float hi =
            std::max(kEarlySizeMinMs, std::min(kEarlySizeMaxMs, maxEarlySeconds_ * 1000.0f));
        ctlEarlySizeMs_ = std::clamp(isFinite(ms) ? ms : kDefaultEarlySizeMs, kEarlySizeMinMs, hi);
        if (prepared_ && !anySamplesProcessed_) {
            erSizeSm_.snapTo(ctlEarlySizeMs_);
            erSizeMsCurrent_ = ctlEarlySizeMs_;
        } else {
            erSizeSm_.setTarget(ctlEarlySizeMs_);
        }
    }

    /// 0..1, zero-gated (FR-065). erChainSkipped_ is cleared HERE, before the
    /// ramp starts moving, so the first ramped sample already has taps behind it.
    void setEarlyLevel(float v) noexcept {
        ctlEarlyLevel_ = std::clamp(isFinite(v) ? v : kDefaultEarlyLevel, 0.0f, 1.0f);
        if (ctlEarlyLevel_ > 0.0f) {
            erChainSkipped_ = false;
        }
        if (prepared_ && !anySamplesProcessed_) {
            earlyLevel_.snapTo(ctlEarlyLevel_);
        } else {
            earlyLevel_.setTarget(ctlEarlyLevel_);
        }
    }

    /// 0..1; v == 0 is an EXACT bypass by assignment, never a == 1 (FR-025).
    void setEarlyAbsorption(float v) noexcept {
        ctlAbsorption_ = std::clamp(isFinite(v) ? v : kDefaultEarlyAbsorption, 0.0f, 1.0f);
        if (prepared_ && !anySamplesProcessed_) {
            absorptionSm_.snapTo(ctlAbsorption_);
        } else {
            absorptionSm_.setTarget(ctlAbsorption_);
        }
    }

    /// 0..1, zero-gated (FR-065): at 0 the owned engine receives literal silence.
    void setEarlySend(float v) noexcept {
        ctlEarlySend_ = std::clamp(isFinite(v) ? v : kDefaultEarlySend, 0.0f, 1.0f);
        if (ctlEarlySend_ > 0.0f) {
            erChainSkipped_ = false;
        }
        if (prepared_ && !anySamplesProcessed_) {
            earlySend_.snapTo(ctlEarlySend_);
        } else {
            earlySend_.setTarget(ctlEarlySend_);
        }
    }

    /// 0..1 -> BrownianDrift::setDepth on every damper. DEPTH IS APPLIED
    /// EXACTLY ONCE, here (plan S0.2 B-3): getCurrentValue() already carries it
    /// (brownian_drift.h:249-251), so a second multiply would square it.
    void setDamperDepth(float v) noexcept {
        damperDepth_ = std::clamp(isFinite(v) ? v : kDefaultDamperDepth, 0.0f, 1.0f);
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            damper_[i].setDepth(damperDepth_);
        }
    }

    /// 0..1 -> setSmoothness(1 - v): 0 is the slowest wander (tau = 30 s), 1 the
    /// fastest (tau = 0.2 s). brownian_drift.h:97-99, :231-234.
    void setDamperRate(float v) noexcept {
        damperRate_ = std::clamp(isFinite(v) ? v : kDefaultDamperRate, 0.0f, 1.0f);
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            damper_[i].setSmoothness(1.0f - damperRate_);
        }
    }

    /// Idempotent at the engine (aether_reverb.h:2256-2262), so a per-block call
    /// cannot stall the 50 ms latch.
    void setFreeze(bool on) noexcept {
        ctlFreeze_ = on;
        engine_.setFreeze(on);
    }

    /// Late field only - the ER bus never sees it (FR-019).
    void setWidth(float v) noexcept {
        ctlWidth_ = std::clamp(isFinite(v) ? v : kDefaultWidth, 0.0f, 1.0f);
        engine_.setWidth(ctlWidth_);
    }

    /// 0..1, equal-power, zero-gated (FR-063, FR-065). CavernVerb owns the mix;
    /// the owned engine is permanently fully wet.
    void setMix(float v) noexcept {
        ctlMix_ = std::clamp(isFinite(v) ? v : kDefaultMix, 0.0f, 1.0f);
        if (prepared_ && !anySamplesProcessed_) {
            mix_.snapTo(ctlMix_);
        } else {
            mix_.setTarget(ctlMix_);
        }
    }

    /// Store and re-seed every stochastic stream (FR-034, FR-035).
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        reseed();
    }

    /// @brief Phase 12 (spec B-4 / FR-023). Rebuild the inner engine's
    ///        Dimensionality matrix endpoint from the seed setSeed() stored, so a
    ///        live seed change reproduces a fresh instance prepared at that seed.
    ///        Forwards to AetherReverb::rebuildMatrixFromSeed(); RT-safe, no-op
    ///        before prepare().
    void rebuildMatrixFromSeed() noexcept { engine_.rebuildMatrixFromSeed(); }

    // -------------------------------------------------------------------------
    // Introspection (FR-008, FR-009, FR-027, FR-037, FR-062).
    // Out-of-range indices return 0.0f everywhere (the
    // getEffectiveDelayLengthSamples idiom, aether_reverb.h:2582-2584).
    // -------------------------------------------------------------------------

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] bool isFrozen() const noexcept { return engine_.isFrozen(); }
    /// Always false: shimmer is never constructed (FR-010).
    [[nodiscard]] bool isShimmerActive() const noexcept { return engine_.isShimmerActive(); }
    [[nodiscard]] float getEffectiveDelayLengthSamples(std::size_t channel) const noexcept {
        return engine_.getEffectiveDelayLengthSamples(channel);
    }
    [[nodiscard]] float getModalDensityPerHz() const noexcept {
        return engine_.getModalDensityPerHz();
    }
    [[nodiscard]] float getMaxSizeScale() const noexcept { return engine_.getMaxSizeScale(); }
    [[nodiscard]] float getStateEnergy() const noexcept { return engine_.getStateEnergy(); }
    [[nodiscard]] std::size_t getNonFiniteRecoveryCount() const noexcept {
        return engine_.getNonFiniteRecoveryCount();
    }
    [[nodiscard]] bool isRecovering() const noexcept { return engine_.isRecovering(); }
    /// diffusionFftSize when the spectral stage was enabled at prepare, else 0.
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        return engine_.getLatencySamples();
    }

    /// @brief CavernVerb's OWN heap buffers, in bytes - the owned engine's are
    ///        NOT included (FR-075). Zero before prepare().
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept {
        if (!prepared_) {
            return 0u;
        }
        const std::size_t erFloats = nextPowerOf2(erLine_.maxDelaySamples() + 1u);
        const std::size_t alignFloats = nextPowerOf2(dryAlignL_.maxDelaySamples() + 1u);
        return (erFloats + (4u * alignFloats)) * sizeof(float);
    }

    [[nodiscard]] std::size_t getEarlyTapCount() const noexcept { return kEarlyTapCount; }

    /// @brief The CURRENT, size-scaled delay of one tap, in samples.
    [[nodiscard]] float getEarlyTapDelaySamples(std::size_t tap) const noexcept {
        return (tap < kEarlyTapCount) ? chunkTapDelayStart_[tap] : 0.0f;
    }

    /// @brief The STATIC tap gain g_i (FR-027): unaffected by ER size, by
    ///        absorption and by the early-level ramp.
    [[nodiscard]] float getEarlyTapGain(std::size_t tap) const noexcept {
        return (tap < kEarlyTapCount) ? taps_[tap].gain : 0.0f;
    }

    /// @brief The tap's current absorption cutoff, in Hz. While absorption is
    ///        bypassed (v == 0) every in-range tap reports the effective fcMax.
    [[nodiscard]] float getEarlyTapAbsorptionCutoffHz(std::size_t tap) const noexcept {
        return (tap < kEarlyTapCount) ? taps_[tap].fcHz : 0.0f;
    }

    /// @brief The damping offset PUBLISHED to the engine for one line, in
    ///        octaves - POST-clamp (plan S0.2 B-2 / S14 Q2).
    [[nodiscard]] float getDamperOffsetOctaves(std::size_t line) const noexcept {
        return (line < kMaxChannels) ? damperOffset_[line] : 0.0f;
    }

private:
    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    /// @brief Finiteness without <cmath>'s classifiers (FR-071).
    ///
    /// ITERUM_NOINLINE (primitives/smoother.h:39-45) is LOAD-BEARING, not style:
    /// without it the guard is inlined and folded away under -ffast-math on the
    /// macOS leg. Composes the Layer 0 helpers detail::isNaN and detail::isInf
    /// rather than reimplementing the bit test a fourth time.
    [[nodiscard]] ITERUM_NOINLINE static bool isFinite(float v) noexcept {
        return !detail::isNaN(v) && !detail::isInf(v);
    }

    struct EarlyTap {
        float gain = 0.0f;       ///< g_i, static (FR-027 reports THIS)
        float delayMs = 0.0f;    ///< d_i at the DEFAULT ER size
        float fcHz = 0.0f;       ///< current absorption cutoff (FR-025)
        float a = 0.0f;          ///< one-pole coefficient for fcHz
        float state = 0.0f;      ///< one-pole state - ONE PER TAP, never shared
        bool rightSide = false;  ///< FR-020's even -> L / odd -> R rule
    };

    /// @brief Shaped, per-sample, >= 50 ms control ramp (FR-065 (iii), FR-023).
    ///
    ///   value = from + smoothstep(r) * (to - from)
    ///
    /// Exact at BOTH endpoints (so the control law is undistorted when settled)
    /// and zero-derivative at both - the aether_reverb.h:4237-4241 shaping, for
    /// the measured C0-corner failure at :4270-4281. A re-target to the SAME
    /// value is dropped, so a per-block setter cannot stall the ramp.
    struct ShapedRamp {
        LinearRamp r;
        float from = 0.0f;
        float to = 0.0f;

        void configure(float sampleRate) noexcept {
            r.configure(kGateRampMs, sampleRate);
            r.snapTo(1.0f);
        }

        void snapTo(float v) noexcept {
            from = v;
            to = v;
            r.snapTo(1.0f);
        }

        void setTarget(float v) noexcept {
            if (v == to) {
                return;  // a re-target to the same value must not restart the ramp
            }
            from = shaped(r.getCurrentValue());
            to = v;
            r.snapTo(0.0f);
            r.setTarget(1.0f);
        }

        /// Advances the ramp by EXACTLY ONE sample and returns the shaped value.
        [[nodiscard]] float process() noexcept { return shaped(r.process()); }

        [[nodiscard]] float current() const noexcept { return shaped(r.getCurrentValue()); }

        [[nodiscard]] bool settledAtZero() const noexcept {
            return (to == 0.0f) && r.isComplete();
        }

    private:
        [[nodiscard]] float shaped(float t) const noexcept {
            const float s = t * t * (3.0f - (2.0f * t));  // smoothstep
            return from + (s * (to - from));
        }
    };

    /// @brief Zero the seven control-chunk scratch arrays (FR-006).
    ///
    /// They are written before they are read on every slice, so this is about
    /// leaving no stale audio behind a reset() - the engine clears its own
    /// scratch for the same reason (aether_reverb.h:2004-2012).
    void clearScratch() noexcept {
        std::fill(erScratchL_, erScratchL_ + kControlChunkSamples, 0.0f);
        std::fill(erScratchR_, erScratchR_ + kControlChunkSamples, 0.0f);
        std::fill(sendScratch_, sendScratch_ + kControlChunkSamples, 0.0f);
        std::fill(engineOutL_, engineOutL_ + kControlChunkSamples, 0.0f);
        std::fill(engineOutR_, engineOutR_ + kControlChunkSamples, 0.0f);
        std::fill(dryScratchL_, dryScratchL_ + kControlChunkSamples, 0.0f);
        std::fill(dryScratchR_, dryScratchR_ + kControlChunkSamples, 0.0f);
    }

    /// @brief Re-apply every shadowed control (FR-060), plus the two engine
    ///        invariants CavernVerb pins: permanently fully wet (D-2) and zero
    ///        pre-delay (FR-019). prepare() and reset() both go through here so
    ///        the two lifecycle paths cannot diverge.
    void applyAllControls() noexcept {
        engine_.setMix(1.0f);
        engine_.setPreDelayMs(0.0f);

        setSize(ctlSize_);
        setDarkness(ctlDarkness_);
        setDecaySeconds(ctlDecay_);
        setDensity(ctlDensity_);
        setDimensionality(ctlDim_);
        setBreath(ctlBreath_);
        setFog(ctlFog_);
        setEarlySizeMs(ctlEarlySizeMs_);
        setEarlyLevel(ctlEarlyLevel_);
        setEarlyAbsorption(ctlAbsorption_);
        setEarlySend(ctlEarlySend_);
        setDamperDepth(damperDepth_);
        setDamperRate(damperRate_);
        setFreeze(ctlFreeze_);
        setWidth(ctlWidth_);
        setMix(ctlMix_);
    }

    /// @brief Re-seed every stochastic stream from seed_ (FR-034, FR-035).
    ///
    /// The engine's own delay-jitter streams are derived as
    /// deriveStreamSeed(engineSeed, kDriftSaltBase + j) from
    /// engineSeed = deriveStreamSeed(seed_, kCavernReverbSalt) - a different
    /// base AND a disjoint salt range from the dampers' kCavernDamperSaltBase.
    ///
    /// setSeed stores the value and re-seeds the RNG (brownian_drift.h:145-148);
    /// reset() -> initState() is what rewinds the walk (:133, :243-249), so BOTH
    /// are needed, in that order.
    ///
    /// THE EARLY-REFLECTION STAGE USES NO RNG AT ALL (FR-035). Do not add one.
    void reseed() noexcept {
        engine_.setSeed(deriveStreamSeed(seed_, kCavernReverbSalt));
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            damper_[i].setSeed(deriveStreamSeed(seed_, kCavernDamperSaltBase + i));
            damper_[i].reset();
        }
    }

    /// @brief Per-tap stone absorption cutoffs (FR-025, plan S5.3):
    ///
    ///   fcMax = min(kEarlyAbsorptionFcMaxHz, kEarlyAbsorptionNyquistFraction * sr)
    ///   fcMin = min(kEarlyAbsorptionFcMinHz, kEarlyAbsorptionSpanFraction * fcMax)
    ///   fc_i  = fcMax * (fcMin / fcMax) ^ (v * i / (n - 1))
    ///   a_i   = 1 - exp(-2*pi*fc_i / sr)
    ///
    /// The Nyquist guard is part of the LAW, not an implementation detail, and
    /// both fractions are public constants so the tests read what the header
    /// reads. Tap 0 stays at fcMax for every v.
    ///
    /// Recomputed only when v actually moved (the kJotRecomputeEpsilon idiom,
    /// aether_reverb.h:2785), so a settled absorption costs nothing per chunk.
    void updateAbsorption() noexcept {
        const float v = absorptionSm_.getCurrentValue();
        if (std::fabs(v - lastAbsorptionApplied_) <= kAbsorptionEpsilon) {
            return;
        }
        lastAbsorptionApplied_ = v;

        // Exact bypass by assignment at v == 0 (FR-025), NOT a == 1: a path that
        // may carry a stale value is replaced, never scaled by a coefficient
        // that happens to be identity.
        const bool bypass = (v <= 0.0f);
        if (!bypass && absorptionBypass_) {
            // Leaving bypass: the states hold history the filter was not running
            // on, so they are zeroed before the one-pole is re-engaged.
            for (std::size_t i = 0; i < kEarlyTapCount; ++i) {
                taps_[i].state = 0.0f;
            }
        }
        absorptionBypass_ = bypass;

        const auto sr = static_cast<float>(sampleRate_);
        const float fcMax = std::min(kEarlyAbsorptionFcMaxHz, kEarlyAbsorptionNyquistFraction * sr);
        const float fcMin = std::min(kEarlyAbsorptionFcMinHz, kEarlyAbsorptionSpanFraction * fcMax);

        if (bypass) {
            // The bypass computes NOTHING: no pow, no exp, no coefficient.
            // fcHz is the LITERAL fcMax rather than fcMax * pow(fcMin/fcMax, 0),
            // which is only exactly fcMax by grace of pow(x, 0) == 1 - so
            // FR-027's "reports fcMax while bypassed" holds by construction
            // instead of by a rounding that a future libm could give up.
            for (std::size_t i = 0; i < kEarlyTapCount; ++i) {
                taps_[i].fcHz = fcMax;
                taps_[i].a = 0.0f;  // never read: renderSlice branches on the flag
            }
            return;
        }

        // At most 12 pow + 12 exp, and ONLY on a chunk where v actually moved -
        // a settled absorption costs nothing at all.
        //
        // state += a * (x - state) is NON-EXPANSIVE for every a in (0, 1] (the
        // argument recorded at aether_reverb.h:3085-3086), so FR-028's sum|g_i|
        // peak bound on the ER bus survives absorption unchanged.
        const auto span = static_cast<float>(kEarlyTapCount - 1u);
        for (std::size_t i = 0; i < kEarlyTapCount; ++i) {
            const float t = (v * static_cast<float>(i)) / span;
            const float fc = fcMax * std::pow(fcMin / fcMax, t);
            taps_[i].fcHz = fc;
            taps_[i].a = 1.0f - std::exp((-2.0f * kPi * fc) / sr);
        }
    }

    /// @brief The once-per-control-chunk refresh (plan S3.3 step 4).
    ///
    /// erSizeSm_ IS ADVANCED HERE AND NOWHERE ELSE. Advancing it in a second
    /// place would step the 300 ms smoother by 128 samples per 64-sample chunk
    /// (OnePoleSmoother::advanceSamples is a real closed-form advance,
    /// smoother.h:243-254): the effective smoothing time would halve, AND
    /// chunkTapDelayStart_[i] would no longer equal the delay actually used at
    /// the previous chunk's last sample, putting a step at every chunk boundary.
    void refreshControlState() noexcept {
        const float prevMs = erSizeMsCurrent_;
        erSizeSm_.advanceSamples(kControlChunkSamples);
        const float nextMs = erSizeSm_.getCurrentValue();
        const float sPrev = prevMs / kDefaultEarlySizeMs;
        const float sNext = nextMs / kDefaultEarlySizeMs;
        for (std::size_t i = 0; i < kEarlyTapCount; ++i) {
            chunkTapDelayStart_[i] = taps_[i].delayMs * sPrev * msToSamples_;
            chunkTapDelayEnd_[i] = taps_[i].delayMs * sNext * msToSamples_;
        }
        erSizeMsCurrent_ = nextMs;

        updateAbsorption();

        // FR-065: the tap loop is dormant only when BOTH gains are settled at
        // exactly zero. The ER line keeps being written either way, so a
        // returning gain does not fade in from a hole.
        erChainSkipped_ = earlyLevel_.settledAtZero() && earlySend_.settledAtZero();

        // FR-073: the tap one-poles are first-order IIRs, so their states ARE a
        // recirculating quantity. The per-chunk flush bounds the exposure to at
        // most one control chunk of denormal arithmetic on twelve scalars.
        for (std::size_t i = 0; i < kEarlyTapCount; ++i) {
            if (std::fabs(taps_[i].state) < 1.0e-20f) {
                taps_[i].state = 0.0f;
            }
        }
    }

    /// @brief Generate, clamp and publish this chunk's damper offsets (FR-031,
    ///        FR-033, FR-037; plan S6.2).
    ///
    ///   offset_i = (kMaxDamperOctaves / kInternalStd) * drift_i.getCurrentValue()
    ///
    /// THE DEPTH IS NOT APPLIED HERE. It was applied exactly once, in
    /// setDamperDepth, through BrownianDrift::setDepth: getCurrentValue()
    /// already carries it, because outputTarget() is
    /// clamp(depth_ * x_, -1, +1) (brownian_drift.h:249-251) and the returned
    /// value is itself clamped to [-1, +1] (:212-214). A second multiply by the
    /// depth here would SQUARE it (plan S0.2 B-3) - do not add one.
    ///
    /// Dividing by kInternalStd = 0.5 (brownian_drift.h:101) is what makes
    /// kMaxDamperOctaves the TYPICAL peak excursion at depth 1 rather than a
    /// cap the walk reaches only on rare tail excursions; the walk is zero-mean,
    /// so a line wanders both darker and brighter - it breathes rather than only
    /// darkening.
    ///
    /// No second smoother is added: BrownianDrift's internal 150 ms output
    /// smoother (kDriftOutputSmoothMs, :103) IS FR-033's slew limit, and
    /// SC-003 (a) measures the per-chunk step it produces.
    ///
    /// The PUBLISHED value is the POST-CLAMP one (plan S0.2 B-2), which is also
    /// what getDamperOffsetOctaves reports. At depth 0 every drift's
    /// outputTarget() is exactly 0.0f, so every published offset is exactly
    /// 0.0f BY VALUE and FR-044's inertness precondition holds without a
    /// tolerance.
    void publishDamperOffsets() noexcept {
        constexpr float kOctavesPerUnit = kMaxDamperOctaves / BrownianDrift::kInternalStd;
        for (std::size_t i = 0; i < numChannels_; ++i) {
            float o = kOctavesPerUnit * damper_[i].getCurrentValue();
            o = std::clamp(o, -kMaxDamperOctaves, kMaxDamperOctaves);
            if (std::fabs(o) < 1.0e-20f) {
                o = 0.0f;  // FR-073
            }
            damperOffset_[i] = o;
        }
        for (std::size_t i = numChannels_; i < kMaxChannels; ++i) {
            damperOffset_[i] = 0.0f;
        }
        engine_.setDamperOffsetsOctaves(damperOffset_, numChannels_);
    }

    /// @brief One control step, at an ABSOLUTE kControlChunkSamples boundary.
    ///
    /// THE ORDER IS NORMATIVE - it mirrors the engine's own control step
    /// (aether_reverb.h:3871-3913):
    ///
    ///  1. the dampers advance UNCONDITIONALLY, each by a FULL
    ///     kControlChunkSamples (FR-036) - never by a slice length, because
    ///     processBlock(36) + processBlock(28) is not the same state as
    ///     processBlock(64) (brownian_drift.h:194-206). That is what makes
    ///     SC-011's partition invariance structural rather than incidental.
    ///  2. absorptionSm_ advances by a full chunk. erSizeSm_ IS DELIBERATELY
    ///     NOT ADVANCED HERE: it is advanced in exactly one place, inside
    ///     refreshControlState(), where prevMs/nextMs are captured around it.
    ///     Advancing it twice would halve the effective 300 ms smoothing time
    ///     AND put a step at every chunk boundary (plan S3.3 step 2).
    ///     earlyLevel_, earlySend_ and mix_ are PER-SAMPLE ramps and are
    ///     likewise not advanced here.
    ///  3. the damper offsets are computed and published to the engine, BEFORE
    ///     the engine's own control step (which happens inside renderSlice's
    ///     engine_.processStereoBlock call at this same absolute boundary).
    ///  4. refreshControlState(): the chunk tap-delay endpoints, the absorption
    ///     coefficients, the ER-chain skip decision and the denormal flush.
    ///
    /// @note Steps 1 and 3 are the damper bank. Step 3 must stay AHEAD of the
    ///       engine's own control step (which runs inside renderSlice's
    ///       engine_.processStereoBlock call at this same absolute boundary),
    ///       so the offsets this chunk publishes are the ones the engine reads
    ///       this chunk.
    void runControlStep() noexcept {
        // 1. UNCONDITIONALLY, and by a FULL chunk. There is deliberately no
        //    `if (!ctlFreeze_)` and no input-activity test here (FR-036): what
        //    freeze suspends is the APPLICATION of the offsets, inside the
        //    engine's own !freezeTarget_ branch (aether_reverb.h:3775-3787),
        //    never their generation.
        for (std::size_t i = 0; i < numChannels_; ++i) {
            damper_[i].processBlock(kControlChunkSamples);
        }
        // 2.
        absorptionSm_.advanceSamples(kControlChunkSamples);
        // 3.
        publishDamperOffsets();
        // 4.
        refreshControlState();
    }

    /// @brief Render one sub-block that lies entirely inside one control chunk.
    ///
    /// @param inL   Left input, `slice` samples.
    /// @param inR   Right input, `slice` samples.
    /// @param outL  Left output, `slice` samples.
    /// @param outR  Right output, `slice` samples.
    /// @param slice Sample count; never exceeds kControlChunkSamples, which is
    ///              why the seven scratch arrays are exactly that size.
    /// @param phase Absolute grid phase of `inL[0]` - the interpolation of the
    ///              tap delays is derived from it, so the delay used at a given
    ///              ABSOLUTE sample index does not depend on where the caller
    ///              happened to split its block (FR-007, SC-011).
    void renderSlice(const float* inL, const float* inR, float* outL, float* outR,
                     std::size_t slice, std::size_t phase) noexcept {
        constexpr float kInvChunk = 1.0f / static_cast<float>(kControlChunkSamples);
        constexpr float kHalfPi = 1.57079633f;

        for (std::size_t k = 0; k < slice; ++k) {
            // FR-064: sanitise ONCE, before ANY consumer - the ER line
            // included. A REPLACEMENT, never a counter increment
            // (aether_reverb.h:4163-4172). Sanitising only the dry bus would be
            // a defect, not an asymmetry: the per-tap one-pole below is a
            // first-order IIR, so one non-finite input sample would latch all
            // twelve tap states permanently (the FR-073 flush tests
            // |state| < 1e-20, which is false for NaN, and nothing else in this
            // design ever clears them).
            const float xl = isFinite(inL[k]) ? inL[k] : 0.0f;
            const float xr = isFinite(inR[k]) ? inR[k] : 0.0f;

            // ONE MONO ER LINE (FR-020): the reflection pattern is therefore
            // invariant to input panning and its stereo width is purely
            // geometric, so setWidth cannot collapse it.
            float mono = 0.5f * (xl + xr);
            if (std::fabs(mono) < 1.0e-20f) {
                mono = 0.0f;  // FR-073: the ER line's denormal guard
            }
            erLine_.write(mono);

            // FR-023: the tap delay is interpolated PER SAMPLE across the
            // chunk. DelayLine::makeLinearTap is deliberately NOT used - it
            // pins the delay for a whole chunk, which turns an 80 -> 600 ms
            // sweep into a ~548-sample staircase every 1.33 ms at 48 kHz (the
            // measured precedent is aether_reverb.h:4270-4281).
            const float t = static_cast<float>(phase + k) * kInvChunk;

            float erL = 0.0f;
            float erR = 0.0f;
            float erMono = 0.0f;
            if (!erChainSkipped_) {
                for (std::size_t i = 0; i < kEarlyTapCount; ++i) {
                    const float d = chunkTapDelayStart_[i] +
                                    (t * (chunkTapDelayEnd_[i] - chunkTapDelayStart_[i]));
                    float x = erLine_.readLinear(d);
                    if (!absorptionBypass_) {
                        taps_[i].state += taps_[i].a * (x - taps_[i].state);
                        x = taps_[i].state;
                    }
                    const float y = x * taps_[i].gain;
                    if (taps_[i].rightSide) {
                        erR += y;
                    } else {
                        erL += y;
                    }
                    erMono += y;  // FR-026 (ii): the SAME reads feed the send
                }
            }

            // The send ramp is advanced EVERY sample, skipped or not, so the
            // dormant and active paths share one timeline. While skipped the
            // engine receives literal digital silence BY ASSIGNMENT (FR-024's
            // rule: a path that may carry a stale value is replaced, never
            // scaled by a coefficient that happens to be zero).
            const float sendG = earlySend_.process();
            sendScratch_[k] = erChainSkipped_ ? 0.0f : (erMono * sendG);

            erScratchL_[k] = erL;  // UNGAINED: the level is applied POST-alignment
            erScratchR_[k] = erR;
            dryScratchL_[k] = xl;  // FR-064: the SAME sanitised value the ER line saw
            dryScratchR_[k] = xr;
        }

        // FR-026 (i) + (iii): the owned engine is excited by the post-absorption
        // ER sum and by NOTHING else - no direct and no unabsorbed path reaches
        // it by any route - and by the SAME mono buffer on both inputs. The
        // engine's own matrix and per-line drift are what decorrelate the late
        // field's two channels.
        engine_.processStereoBlock(sendScratch_, sendScratch_, engineOutL_, engineOutR_, slice);

        for (std::size_t k = 0; k < slice; ++k) {
            // EACH ShapedRamp IS ADVANCED EXACTLY ONCE PER OUTPUT SAMPLE, never
            // once per channel - which is why the values are hoisted above BOTH
            // channel assignments and both assignments are written out in full.
            // A second process() call for R would halve kGateRampMs (50 -> 25 ms)
            // and break FR-065 (iii)'s ">= 50 ms re-entry" guarantee.
            const float mx = mix_.process();
            const float dryG = std::cos(mx * kHalfPi);  // FR-063: equal power
            const float wetG = std::sin(mx * kHalfPi);
            const float lvl = earlyLevel_.process();
            const bool mixOff = mix_.settledAtZero();
            const bool erOff = earlyLevel_.settledAtZero();

            // FR-062: four alignment lines, always prepared, written and read on
            // EVERY sample. The dry and ER busses carry DIFFERENT gains
            // (dryG vs wetG * lvl), so one shared aligned pair cannot serve
            // both - and applying a gain before the line would delay every gain
            // change by the full latency, which is why the gains come after.
            dryAlignL_.write(dryScratchL_[k]);
            const float dL = dryAlignL_.read(alignSamples_);
            dryAlignR_.write(dryScratchR_[k]);
            const float dR = dryAlignR_.read(alignSamples_);
            erAlignL_.write(erScratchL_[k]);
            const float eL = erAlignL_.read(alignSamples_);
            erAlignR_.write(erScratchR_[k]);
            const float eR = erAlignR_.read(alignSamples_);

            // FR-065's STATED EXCEPTION, reproduced here because the roadmap
            // obliges a spec that keeps a silent slot burning its chain to say
            // what the listener would hear that justifies it:
            //
            //   setMix == 0 SKIPS NOTHING. The wet path - the owned engine AND
            //   the ER tap loop - keeps running. The FDN is a 60-second-decay
            //   recirculating state that a drone player holds, freezes and
            //   returns to; a mix automated to 0 and back is a DUCK, not a
            //   STOP. Skipping the chain would drain that state, and what the
            //   listener would hear on the way back is a cavern rebuilding from
            //   silence over tens of seconds instead of the tail they left -
            //   plus a setFreeze() that latches nothing. The reverse case is
            //   the precedent: Phase 5 cleared a feedback loop at the sleep
            //   edge BECAUSE it has no generator behind it and would replay a
            //   stale burst. Here there is a generator (the input, the dampers,
            //   the matrix morph) and the state IS the instrument.
            //
            // Only the OUTPUT is gated, and by assignment - never by an x * 0
            // product, because a buffer that may hold a non-finite value must
            // be replaced (NaN * 0 is NaN), FR-024 / FR-063.
            if (mixOff) {
                outL[k] = dL;  // FR-063: an ASSIGNMENT, not a x * 0 product
                outR[k] = dR;
            } else {
                const float erLg = erOff ? 0.0f : (lvl * eL);  // FR-024, same rule
                const float erRg = erOff ? 0.0f : (lvl * eR);
                outL[k] = (dryG * dL) + (wetG * (engineOutL_[k] + erLg));
                outR[k] = (dryG * dR) + (wetG * (engineOutR_[k] + erRg));
            }
        }
    }

    // -------------------------------------------------------------------------
    // Private state (plan S2.5)
    // -------------------------------------------------------------------------

    /// Recompute gate for the absorption coefficients (the :2785 idiom).
    static constexpr float kAbsorptionEpsilon = 1.0e-7f;
    /// pi, so the absorption coefficient reads as 1 - exp(-2*pi*fc/sr).
    static constexpr float kPi = 3.14159265358979f;

    // --- prepared configuration ---
    bool prepared_ = false;
    bool anySamplesProcessed_ = false;
    double sampleRate_ = 48000.0;
    std::size_t numChannels_ = 8;
    std::size_t alignSamples_ = 0;  ///< == engine_.getLatencySamples()
    float maxEarlySeconds_ = kDefaultMaxEarlySeconds;
    float msToSamples_ = 48.0f;  ///< sampleRate_ / 1000
    std::uint32_t seed_ = 1;
    std::uint64_t sampleCounter_ = 0;

    AetherReverb engine_;  ///< owned BY VALUE (FR-002)

    // --- ER stage ---
    DelayLine erLine_;  ///< MONO (FR-020)
    EarlyTap taps_[kEarlyTapCount]{};
    OnePoleSmoother erSizeSm_;    ///< ms, kEarlySizeSmoothingMs
    OnePoleSmoother absorptionSm_;  ///< [0,1], kAbsorptionSmoothingMs
    float erSizeMsCurrent_ = kDefaultEarlySizeMs;
    float chunkTapDelayStart_[kEarlyTapCount]{};  ///< samples, at the chunk's first sample
    float chunkTapDelayEnd_[kEarlyTapCount]{};    ///< samples, one past the chunk's last
    float lastAbsorptionApplied_ = -1.0f;         ///< forces the first refresh to compute
    bool absorptionBypass_ = false;               ///< exact bypass at v == 0 (FR-025)
    bool erChainSkipped_ = false;                 ///< FR-065 dormancy of the tap loop

    ShapedRamp earlyLevel_;
    ShapedRamp earlySend_;
    ShapedRamp mix_;

    // --- alignment (FR-062): FOUR mono lines, dry L/R and ER L/R ---
    DelayLine dryAlignL_;
    DelayLine dryAlignR_;
    DelayLine erAlignL_;
    DelayLine erAlignR_;

    // --- dampers ---
    BrownianDrift damper_[kMaxChannels];
    float damperOffset_[kMaxChannels]{};  ///< published octaves (post-clamp)
    float damperDepth_ = kDefaultDamperDepth;
    float damperRate_ = kDefaultDamperRate;

    // --- scratch: exactly one control chunk, NEVER sized by maxBlockSamples.
    //     7 * 64 * 4 B = 1 792 B of member storage; no heap, no VLA.
    float erScratchL_[kControlChunkSamples]{};
    float erScratchR_[kControlChunkSamples]{};
    float sendScratch_[kControlChunkSamples]{};
    float engineOutL_[kControlChunkSamples]{};
    float engineOutR_[kControlChunkSamples]{};
    float dryScratchL_[kControlChunkSamples]{};
    float dryScratchR_[kControlChunkSamples]{};

    // --- shadow copies of every control, so prepare()/reset() can re-apply them ---
    float ctlSize_ = kDefaultSize;
    float ctlDarkness_ = kDefaultDarkness;
    float ctlDecay_ = kDefaultDecaySeconds;
    float ctlDensity_ = kDefaultDensity;
    float ctlDim_ = kDefaultDimensionality;
    float ctlBreath_ = kDefaultBreath;
    float ctlFog_ = kDefaultFog;
    float ctlEarlySizeMs_ = kDefaultEarlySizeMs;
    float ctlEarlyLevel_ = kDefaultEarlyLevel;
    float ctlAbsorption_ = kDefaultEarlyAbsorption;
    float ctlEarlySend_ = kDefaultEarlySend;
    float ctlWidth_ = kDefaultWidth;
    float ctlMix_ = kDefaultMix;
    bool ctlFreeze_ = false;
};

// -----------------------------------------------------------------------------
// FR-020 / FR-022 / FR-028 table asserts, at namespace scope so they read the
// completed class - the house placement (aether_reverb.h:4820-4826).
// -----------------------------------------------------------------------------
namespace detail {

/// The affine law's span at the default ER size: 220 - 60 = 160 ms.
inline constexpr double kCavernTapSpanMs =
    static_cast<double>(CavernVerb::kDefaultEarlySizeMs - CavernVerb::kEarlyFirstArrivalFloorMs);
inline constexpr double kCavernTapFirstMs =
    static_cast<double>(CavernVerb::kEarlyFirstArrivalFloorMs);
inline constexpr double kCavernMeasuredGainSum =
    cavernTableGainSum(CavernVerb::kEarlyTapSeries, kCavernTapFirstMs, kCavernTapSpanMs,
                       static_cast<double>(CavernVerb::kEarlyGainG0),
                       static_cast<double>(CavernVerb::kEarlyGainAlphaPerMs));

}  // namespace detail

static_assert(detail::cavernTableStrictlyAscending(CavernVerb::kEarlyTapSeries),
              "FR-020: the early-reflection series must be strictly ascending");
static_assert(detail::cavernTablePairwiseCoprime(CavernVerb::kEarlyTapSeries),
              "FR-020: the early-reflection series must be pairwise coprime");
static_assert(detail::cavernTableIncommensurabilityMin(
                  CavernVerb::kEarlyTapSeries, detail::kCavernTapFirstMs, detail::kCavernTapSpanMs,
                  CavernVerb::kIncommensurabilityOrder) >=
                  static_cast<double>(CavernVerb::kEarlyIncommensurabilityTol),
              "FR-022: no low-order repetition of one tap may land near another's");
static_assert(detail::kCavernMeasuredGainSum <= 2.0, "FR-028: sum|g_i| must not exceed 2.0");
static_assert(detail::kCavernMeasuredGainSum -
                      static_cast<double>(CavernVerb::kEarlyGainSum) <
                  1.0e-5,
              "kEarlyGainSum must equal the table's actual gain sum (upper side)");
static_assert(static_cast<double>(CavernVerb::kEarlyGainSum) - detail::kCavernMeasuredGainSum <
                  1.0e-5,
              "kEarlyGainSum must equal the table's actual gain sum (lower side)");

}  // namespace DSP
}  // namespace Krate
