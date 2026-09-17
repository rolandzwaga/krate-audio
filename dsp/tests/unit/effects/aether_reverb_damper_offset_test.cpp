// ==============================================================================
// Layer 4: Effect Tests - AetherReverb, damper-offset hook (FR-040 .. FR-048)
//                                        (specs/vorago-phase9-cavern-space)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase9-cavern-space/spec.md
//            specs/vorago-phase9-cavern-space/plan.md
//            specs/vorago-phase9-cavern-space/tasks.md  (T001 creates this TU;
//                                                        T003 fills it)
//
// SCOPE OF THIS TU: the append-only damper-offset extension to the SHIPPED
//   AetherReverb - SC-012's inertness clause, hostile input, and the
//   frozen-at-init invariant. It is a separate TU because no file under
//   dsp/tests/unit/effects/aether_reverb_* (the Seraphis Phase 6 suite) may be
//   edited by this phase; the new coverage lands here instead.
//
// NEVER include <allocation_operator_overrides.h> here: the global operator
//   new/delete replacement for this image already lives in
//   dsp/tests/unit/effects/aether_reverb_test.cpp; a second include is a
//   duplicate-symbol link error. Use <allocation_detector.h> only.
//
// CONSTRUCTING NON-FINITE VALUES: never std::numeric_limits<float>::quiet_NaN()
//   or infinity(), and never std::isnan / std::isinf / std::isfinite. Build the
//   values from bit patterns through a VOLATILE sink.
//
// COMPILE FLAGS: this TU is deliberately NOT listed under
//   "-fno-fast-math -fno-finite-math-only" in dsp/tests/CMakeLists.txt, so it
//   builds in the FP mode aether_reverb.h actually ships in. Every finiteness
//   assertion below is therefore a raw exponent-field bit test, never a
//   <cmath> classifier.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/effects/aether_reverb.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using Krate::DSP::AetherReverb;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlockSamples = 512;

/// Mirror of AetherReverb's PRIVATE kDampingNyquistRatio (aether_reverb.h:2788).
/// A test cannot name a private constant, so the value is duplicated here and
/// the recomputation below is pinned to the same line the engine uses.
constexpr float kDampingNyquistRatioMirror = 0.05f;

// ------------------------------------------------------------------------------
// Bit-pattern helpers. The TU builds under /fp:fast + -ffast-math, so neither a
// std::numeric_limits non-finite literal nor a <cmath> classifier survives.
// ------------------------------------------------------------------------------

constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// The volatile READ is the sink: it is what stops the constant being folded
/// back at compile time, which is how a -ffast-math build turns an "infinity"
/// literal into a finite number. Idiom copied from
/// dsp/tests/unit/effects/aether_reverb_nonfinite_test.cpp:108-120.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t materialized = sink;
    float out = 0.0f;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

/// @brief Finiteness by exponent field, never std::isfinite.
///        (aether_reverb_perf_test.cpp:453 uses the same test.)
[[nodiscard]] bool isFiniteBits(float v) noexcept {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

// ------------------------------------------------------------------------------
// The shipped damping law, recomputed from PUBLIC data only.
//
// Transcribed line for line from AetherReverb::updateDecayAndDamping()
// (aether_reverb.h:3138-3149), including the gDC > 1e-10f guard at :3145 that
// the prose summary elides. `m` comes from getEffectiveDelayLengthSamples(i),
// which is the same effectiveDelay_[i] the engine reads at :3142.
// ------------------------------------------------------------------------------
[[nodiscard]] float shippedDampCoeff(float m, float decaySeconds, float damping,
                                     float sr) noexcept {
    const float t60dc = decaySeconds;
    const float t60nyq = t60dc * std::pow(kDampingNyquistRatioMirror, damping);
    const float gDC = std::pow(10.0f, -3.0f * m / (t60dc * sr));
    const float gNyq = std::pow(10.0f, -3.0f * m / (t60nyq * sr));
    float ratio = (gDC > 1e-10f) ? (gNyq / gDC) : 1.0f;
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    return std::clamp((2.0f * ratio) / (1.0f + ratio), 0.001f, 1.0f);
}

// ------------------------------------------------------------------------------
// WHY THIS COMPARISON IS NOT `==`, AND WHAT WAS MEASURED.
//
// SC-012 (a) worded the comparison as EXACT FLOAT EQUALITY between
// getEffectiveDampingCoefficient(i) and a recomputation of the shipped law until
// 2026-09-17, when it was amended to the relative bound below against this
// measurement. That equality is unattainable, and the reason is a property of the
// SHIPPED engine, not of the Phase 9 extension. Measured this session on the real build
// (MSVC 19.x, /fp:fast, the full 15 000-line sweep below):
//
//   * 70 / 5000 lines (N = 8) and 120 / 10 000 (N = 16) differ, always by 1-2 ULP
//     and always on the longest line(s) - the ones with the largest |exponent|.
//   * THE PROOF that no transcription can fix this is the damping == 0 row.
//     There T60_nyq = T60_dc * pow(0.05f, 0.0f), and pow(0.05f, 0.0f) was probed
//     to be EXACTLY 1.0f both constant-folded and at runtime (bits 0x3F800000),
//     so the engine's two std::pow(10.0f, ...) calls are handed BITWISE IDENTICAL
//     arguments and ratio is mathematically exactly 1, giving c = 1.0f. The
//     engine nevertheless returns 0x3F7FFFFF (0.99999994): inside
//     updateDecayAndDamping() the two calls are not emitted as the same
//     computation under /fp:fast. dampCoeff_ is therefore NOT the correctly-
//     rounded value of the documented law, and no independently compiled copy of
//     that law - however transcribed - can equal it bit for bit.
//   * Both alternatives were built and measured rather than assumed: laundering
//     every input of the recomputation through a volatile sink (rules out
//     constant folding in the TEST) leaves exactly the same 70 / 120 lines
//     differing, and mirroring the engine's loop shape by hand is WORSE
//     (630 / 1025 lines, worst 1.2e-6).
//
// The extension itself is exact where FR-044 says it must be: with no offsets
// published applyDamperOffsets() takes the `off == 0.0f` branch and does
// effectiveDampCoeff_[i] = dampCoeff_[i] by plain assignment, and SECTION 2's
// 10 s bit-identical render is the exactness clause that IS well-founded and
// that still passes verbatim.
//
// WORST MEASURED relative deviation over the sweep: 2.49e-7 (N = 8), 1.99e-7
// (N = 16). ANALYTIC bound for a 1-ULP argument perturbation, |dc/c| <=
// ln(10) * eps * (|a_dc| + |a_nyq|) + 2 * libm error, with the swept grid's
// reachable |a| <= ~6 before the 0.001 clamp saturates: ~3e-6. The bound below is
// 1e-5 - 40x the worst measurement, 3x the analytic bound (headroom for the
// Linux/macOS -ffast-math legs, which this TU also runs on), and still four
// orders of magnitude tighter than any defect the clause exists to catch: a
// coefficient taken from the wrong array, left stale, or carrying an unintended
// damper offset moves it by PERCENT, not by ULPs.
// ------------------------------------------------------------------------------
constexpr float kLawRelTolerance = 1.0e-5f;

/// @brief Does the accessor reproduce the shipped law to within kLawRelTolerance?
[[nodiscard]] bool matchesShippedLaw(float got, float expected) noexcept {
    return std::abs(got - expected) <= (kLawRelTolerance * std::abs(expected));
}

/// @brief Relative distance from the shipped law - used by the arm that requires
///        a published offset to have MOVED the coefficient.
[[nodiscard]] float relativeDistanceFromLaw(float got, float expected) noexcept {
    return std::abs(got - expected) / std::max(std::abs(expected), 1.0e-20f);
}

// ------------------------------------------------------------------------------
// A deterministic band-limited noise source. Self-contained on purpose: the
// measurement side of this TU must not depend on anything the engine owns.
// ------------------------------------------------------------------------------
class BandLimitedNoise {
public:
    explicit BandLimitedNoise(std::uint32_t seed) noexcept : state_(seed) {}

    [[nodiscard]] float next() noexcept {
        state_ = (state_ * 1664525u) + 1013904223u;
        const auto quantised = static_cast<float>((state_ >> 8u) & 0x00FFFFFFu);
        const float white = (quantised * (2.0f / 16777216.0f)) - 1.0f;
        lowpass_ += 0.25f * (white - lowpass_);  // ~2 kHz one-pole at 48 kHz
        return lowpass_;
    }

private:
    std::uint32_t state_;
    float lowpass_ = 0.0f;
};

/// @brief Fill one stereo block with decorrelated band-limited noise.
void fillNoiseBlock(BandLimitedNoise& left, BandLimitedNoise& right, std::vector<float>& outLeft,
                    std::vector<float>& outRight, float amplitude) noexcept {
    for (std::size_t k = 0; k < outLeft.size(); ++k) {
        outLeft[k] = amplitude * left.next();
        outRight[k] = amplitude * right.next();
    }
}

/// @brief Configure one engine identically across every case in this TU.
void prepareEngine(AetherReverb& engine, std::size_t numChannels, std::uint32_t seed) noexcept {
    AetherReverb::PrepareConfig config;
    config.numChannels = numChannels;
    config.maxBlockSamples = kBlockSamples;
    config.seed = seed;
    engine.prepare(kSampleRate, config);
}

/// @brief Park an engine on a KNOWN, fully settled control state with STATIC
///        geometry, then materialise it.
///
/// Why reset() and not "set then render": AetherReverb recomputes dampCoeff_
/// only inside runControlStep() -> refreshControlState() (aether_reverb.h:3610),
/// and its private control defaults are unreachable from a test. reset()
/// preserves every control target, snaps every smoother to it
/// (aether_reverb.h:2072-2085), forces the Jot/damping recompute by clearing the
/// lastJot* sentinels (:2095-2098) and calls refreshControlState() (:2099) - and
/// it leaves anySamplesProcessed_ false, so a later setter still snaps.
///
/// Why modDepth and sizeBreathDepth are zeroed: updateGeometry()
/// (aether_reverb.h:3036-3056) folds BrownianDrift and the breath into
/// effectiveDelay_ every chunk, while updateDecayAndDamping()'s epsilon gate
/// (:3128-3137) does NOT re-derive dampCoeff_ for that motion. With both depths
/// at zero the geometry is static, so the recomputation stays exact AFTER
/// samples have been rendered - which cases 2 and 3 need.
void parkSettled(AetherReverb& engine, float size, float decaySeconds, float density,
                 float damping) noexcept {
    engine.setSize(size);
    engine.setDecaySeconds(decaySeconds);
    engine.setDensity(density);
    engine.setDamping(damping);
    engine.setModDepth(0.0f);
    engine.setSizeBreathDepth(0.0f);
    engine.reset();
}

}  // namespace

// ==============================================================================
// Case 1 - SC-012 (a). THE CLAUSE WITH TEETH IS THE RECOMPUTATION.
//
// A bare engine and one handed an all-zero vector hold identical state and
// execute identical instructions once FR-043/FR-044 route both arms through the
// same effectiveDampCoeff_ read site, so their bit-identity is true by
// construction and proves nothing. What has teeth is recomputing the SHIPPED law
// from public data and requiring the new accessor to reproduce it over the whole
// control sweep.
//
// PER SC-012 (a) AS AMENDED 2026-09-17 (and as spec.md now records, with this
// measurement): the comparison is kLawRelTolerance-bounded, not `==`. The earlier
// wording, and plan.md / tasks.md's copies of it, asked for `==`. The shipped engine's own
// dampCoeff_ is not the correctly-rounded value of the law it documents - see
// the measurement and the damping == 0 proof above matchesShippedLaw(). Nothing
// else about the clause is relaxed: the full cross product still runs, every
// line is still checked, and SECTION 2 keeps its bit-exact render.
// ==============================================================================
TEST_CASE("AetherReverb_DamperOffsetInert", "[effects][cavern]") {
    const auto sr = static_cast<float>(kSampleRate);

    SECTION("recomputation of the shipped law over the control sweep, SC-012 (a)") {
        const std::array<float, 5> sizes{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        const std::array<float, 5> decays{0.5f, 2.0f, 4.0f, 12.0f, 60.0f};
        const std::array<float, 5> densities{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        const std::array<float, 5> dampings{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

        const std::array<std::size_t, 2> orders{std::size_t{8}, std::size_t{16}};
        for (const std::size_t numChannels : orders) {
            AetherReverb engine;
            prepareEngine(engine, numChannels, 20260916u);
            REQUIRE(engine.isPrepared());

            std::size_t linesChecked = 0;
            for (const float size : sizes) {
                for (const float decay : decays) {
                    for (const float density : densities) {
                        for (const float damping : dampings) {
                            // Offsets are NEVER published on this engine.
                            engine.setSize(size);
                            engine.setDecaySeconds(decay);
                            engine.setDensity(density);
                            engine.setDamping(damping);
                            engine.reset();

                            for (std::size_t i = 0; i < numChannels; ++i) {
                                const float m = engine.getEffectiveDelayLengthSamples(i);
                                const float expected = shippedDampCoeff(m, decay, damping, sr);
                                const float got = engine.getEffectiveDampingCoefficient(i);
                                if (!matchesShippedLaw(got, expected)) {
                                    UNSCOPED_INFO("N=" << numChannels << " size=" << size
                                                       << " decay=" << decay
                                                       << " density=" << density
                                                       << " damping=" << damping << " line=" << i
                                                       << " m=" << m << " got=" << got
                                                       << " expected=" << expected << " relDev="
                                                       << relativeDistanceFromLaw(got, expected));
                                }
                                REQUIRE(matchesShippedLaw(got, expected));
                                ++linesChecked;
                            }
                        }
                    }
                }
            }
            REQUIRE(linesChecked == (5u * 5u * 5u * 5u) * numChannels);
        }
    }

    SECTION("labelled SMOKE CHECK (cheap, not the clause with teeth): bare vs all-zero vector") {
        // Two runs of the SAME build compared against each other. Nothing is
        // committed, so tools/lint-float-bit-goldens.js and the no-bit-exact-
        // goldens rule are not engaged: this is not a golden, it is an equality
        // between two live instances.
        AetherReverb bare;
        AetherReverb zeroed;
        prepareEngine(bare, 8u, 7u);
        prepareEngine(zeroed, 8u, 7u);
        parkSettled(bare, 0.6f, 3.0f, 0.7f, 0.5f);
        parkSettled(zeroed, 0.6f, 3.0f, 0.7f, 0.5f);

        const std::array<float, 8> allZero{};
        zeroed.setDamperOffsetsOctaves(allZero.data(), allZero.size());

        std::vector<float> inL(kBlockSamples, 0.0f);
        std::vector<float> inR(kBlockSamples, 0.0f);
        std::vector<float> bareL(kBlockSamples, 0.0f);
        std::vector<float> bareR(kBlockSamples, 0.0f);
        std::vector<float> zeroL(kBlockSamples, 0.0f);
        std::vector<float> zeroR(kBlockSamples, 0.0f);

        BandLimitedNoise noiseL(11u);
        BandLimitedNoise noiseR(29u);

        const std::size_t blocks =
            static_cast<std::size_t>((10.0 * kSampleRate) / static_cast<double>(kBlockSamples));
        std::size_t mismatches = 0;
        std::size_t firstMismatchSample = 0;
        for (std::size_t b = 0; b < blocks; ++b) {
            fillNoiseBlock(noiseL, noiseR, inL, inR, 0.25f);
            bare.processStereoBlock(inL.data(), inR.data(), bareL.data(), bareR.data(),
                                    kBlockSamples);
            zeroed.processStereoBlock(inL.data(), inR.data(), zeroL.data(), zeroR.data(),
                                      kBlockSamples);
            for (std::size_t k = 0; k < kBlockSamples; ++k) {
                if ((bareL[k] != zeroL[k]) || (bareR[k] != zeroR[k])) {
                    if (mismatches == 0) {
                        firstMismatchSample = (b * kBlockSamples) + k;
                    }
                    ++mismatches;
                }
            }
        }
        INFO("first differing sample index: " << firstMismatchSample);
        REQUIRE(mismatches == 0u);
    }
}

// ==============================================================================
// Case 2 - FR-040, FR-044, FR-045. Hostile input.
// ==============================================================================
TEST_CASE("AetherReverb_DamperOffsetHostileInput", "[effects][cavern]") {
    constexpr std::size_t kNumChannels = 8;
    constexpr float kSize = 0.6f;
    constexpr float kDecay = 3.0f;
    constexpr float kDensity = 0.7f;
    constexpr float kDamping = 0.5f;
    const auto sr = static_cast<float>(kSampleRate);

    AetherReverb engine;
    prepareEngine(engine, kNumChannels, 4242u);
    REQUIRE(engine.isPrepared());
    parkSettled(engine, kSize, kDecay, kDensity, kDamping);

    // Baseline: with no offsets published the accessor already reproduces the
    // shipped law (to kLawRelTolerance - see the banner above matchesShippedLaw()
    // for why this is not `==`). Everything below is measured against this.
    for (std::size_t i = 0; i < kNumChannels; ++i) {
        const float m = engine.getEffectiveDelayLengthSamples(i);
        INFO("baseline, channel " << i);
        REQUIRE(matchesShippedLaw(engine.getEffectiveDampingCoefficient(i),
                                  shippedDampCoeff(m, kDecay, kDamping, sr)));
    }

    // FR-040: count (32) deliberately EXCEEDS numChannels_ (8), and the array
    // holds exactly eight floats. An implementation that copies `count` values
    // instead of min(count, numChannels_) reads out of bounds here, which is
    // what the ASan lane is for.
    const std::array<float, kNumChannels> hostile{
        makeNonFinite(kQuietNaNBits), makeNonFinite(kPosInfBits), makeNonFinite(kNegInfBits),
        1.0e30f,                      -1.0e30f,                   0.0f,
        7.5f,                         -7.5f};
    // The clause that fails FIRST if the volatile sink is ever removed.
    REQUIRE_FALSE(isFiniteBits(hostile[0]));
    REQUIRE_FALSE(isFiniteBits(hostile[1]));
    REQUIRE_FALSE(isFiniteBits(hostile[2]));

    engine.setDamperOffsetsOctaves(hostile.data(), 32u);

    std::vector<float> inL(kBlockSamples, 0.0f);
    std::vector<float> inR(kBlockSamples, 0.0f);
    std::vector<float> outL(kBlockSamples, 0.0f);
    std::vector<float> outR(kBlockSamples, 0.0f);
    BandLimitedNoise noiseL(101u);
    BandLimitedNoise noiseR(211u);

    const std::size_t blocks =
        static_cast<std::size_t>((10.0 * kSampleRate) / static_cast<double>(kBlockSamples));
    float peak = 0.0f;
    std::size_t nonFiniteSamples = 0;
    for (std::size_t b = 0; b < blocks; ++b) {
        fillNoiseBlock(noiseL, noiseR, inL, inR, 0.25f);
        engine.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockSamples);
        for (std::size_t k = 0; k < kBlockSamples; ++k) {
            if (!isFiniteBits(outL[k]) || !isFiniteBits(outR[k])) {
                ++nonFiniteSamples;
                continue;
            }
            peak = std::max(peak, std::max(std::abs(outL[k]), std::abs(outR[k])));
        }
    }
    INFO("peak |out| under the hostile offset vector: " << peak);
    REQUIRE(nonFiniteSamples == 0u);
    REQUIRE(peak < 10.0f);

    // FR-045: the applied coefficient stays inside the shipped [0.001, 1] range
    // for every admissible AND every hostile offset.
    for (std::size_t i = 0; i < kNumChannels; ++i) {
        const float c = engine.getEffectiveDampingCoefficient(i);
        INFO("channel " << i << " effective damping coefficient " << c);
        REQUIRE(isFiniteBits(c));
        REQUIRE(c >= 0.001f);
        REQUIRE(c <= 1.0f);
    }

    // --- FR-040's two clearing arms. One control chunk (kControlChunkSamples =
    //     64) is enough for refreshControlState() to re-apply; 128 is rendered
    //     so the assertion does not depend on the chunk phase. Written as plain
    //     statements rather than SECTIONs on purpose: Catch2 re-runs the whole
    //     case body per section, and the 10 s render above is not worth paying
    //     for twice.
    constexpr std::size_t kClearingBlock = 128;
    std::vector<float> clearIn(kClearingBlock, 0.0f);
    std::vector<float> clearOutL(kClearingBlock, 0.0f);
    std::vector<float> clearOutR(kClearingBlock, 0.0f);
    for (std::size_t k = 0; k < kClearingBlock; ++k) {
        clearIn[k] = 0.25f * noiseL.next();
    }

    // Arm 1: a null pointer clears the whole array, whatever the count says.
    engine.setDamperOffsetsOctaves(nullptr, 8u);
    engine.processStereoBlock(clearIn.data(), clearIn.data(), clearOutL.data(), clearOutR.data(),
                              kClearingBlock);
    for (std::size_t i = 0; i < kNumChannels; ++i) {
        const float m = engine.getEffectiveDelayLengthSamples(i);
        INFO("nullptr arm, channel " << i);
        REQUIRE(matchesShippedLaw(engine.getEffectiveDampingCoefficient(i),
                                  shippedDampCoeff(m, kDecay, kDamping, sr)));
    }

    // Arm 2: re-arm the hostile vector, prove it took effect on at least one
    // line, then clear it again with a VALID pointer and count == 0.
    engine.setDamperOffsetsOctaves(hostile.data(), 32u);
    engine.processStereoBlock(clearIn.data(), clearIn.data(), clearOutL.data(), clearOutR.data(),
                              kClearingBlock);
    {
        // Channel 4 carries -1e30f, which FR-040 clamps to -kMaxDamperOffsetOctaves:
        // a brighter line, so the coefficient must have MOVED off the shipped one.
        // The threshold is 1 % - three orders of magnitude above the ULP-scale
        // noise quantified above matchesShippedLaw(), and far below the tens of
        // percent a clamped 4-octave offset actually produces.
        const float m = engine.getEffectiveDelayLengthSamples(4);
        const float moved = relativeDistanceFromLaw(engine.getEffectiveDampingCoefficient(4),
                                                    shippedDampCoeff(m, kDecay, kDamping, sr));
        INFO("channel 4 relative movement off the shipped law: " << moved);
        REQUIRE(moved > 0.01f);
    }
    const std::array<float, kNumChannels> ignored{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    engine.setDamperOffsetsOctaves(ignored.data(), 0u);
    engine.processStereoBlock(clearIn.data(), clearIn.data(), clearOutL.data(), clearOutR.data(),
                              kClearingBlock);
    for (std::size_t i = 0; i < kNumChannels; ++i) {
        const float m = engine.getEffectiveDelayLengthSamples(i);
        INFO("count == 0 arm, channel " << i);
        REQUIRE(matchesShippedLaw(engine.getEffectiveDampingCoefficient(i),
                                  shippedDampCoeff(m, kDecay, kDamping, sr)));
    }
}

// ==============================================================================
// Case 3 - FR-047's invariant.
//
// An engine that is prepared (or reset) and frozen BEFORE its first thawed
// control chunk would, without FR-047's second half, run the :4283 one-pole on
// an array that was never written. refreshControlState() refreshes
// effectiveDampCoeff_ only on the !freezeTarget_ branch (:3611-3614), so nothing
// after setFreeze(true) can repair it.
//
// parkSettled() is used for the same reason as in case 2: the engine's control
// defaults are private, so the only way to recompute the shipped coefficient is
// to set the controls to KNOWN values and let reset() materialise them. reset()
// is not a render - no processStereoBlock() runs before setFreeze(true), which
// is the condition the invariant is about.
// ==============================================================================
TEST_CASE("AetherReverb_DamperOffsetFrozenInit", "[effects][cavern]") {
    constexpr std::size_t kNumChannels = 8;
    constexpr float kSize = 0.5f;
    constexpr float kDecay = 6.0f;
    constexpr float kDensity = 0.6f;
    constexpr float kDamping = 0.35f;
    const auto sr = static_cast<float>(kSampleRate);

    AetherReverb engine;
    prepareEngine(engine, kNumChannels, 909u);
    REQUIRE(engine.isPrepared());
    parkSettled(engine, kSize, kDecay, kDensity, kDamping);

    engine.setFreeze(true);  // BEFORE any processStereoBlock

    std::vector<float> inL(kBlockSamples, 0.0f);
    std::vector<float> inR(kBlockSamples, 0.0f);
    std::vector<float> outL(kBlockSamples, 0.0f);
    std::vector<float> outR(kBlockSamples, 0.0f);
    BandLimitedNoise noiseL(5u);
    BandLimitedNoise noiseR(13u);

    const std::size_t blocks =
        static_cast<std::size_t>(kSampleRate / static_cast<double>(kBlockSamples));
    float peak = 0.0f;
    std::size_t nonFiniteSamples = 0;
    for (std::size_t b = 0; b < blocks; ++b) {
        fillNoiseBlock(noiseL, noiseR, inL, inR, 0.25f);
        engine.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockSamples);
        for (std::size_t k = 0; k < kBlockSamples; ++k) {
            if (!isFiniteBits(outL[k]) || !isFiniteBits(outR[k])) {
                ++nonFiniteSamples;
                continue;
            }
            peak = std::max(peak, std::max(std::abs(outL[k]), std::abs(outR[k])));
        }
    }
    INFO("frozen-at-init peak |out|: " << peak);
    REQUIRE(nonFiniteSamples == 0u);
    REQUIRE(peak > 0.0f);

    for (std::size_t i = 0; i < kNumChannels; ++i) {
        const float m = engine.getEffectiveDelayLengthSamples(i);
        INFO("frozen-at-init, channel " << i);
        REQUIRE(matchesShippedLaw(engine.getEffectiveDampingCoefficient(i),
                                  shippedDampCoeff(m, kDecay, kDamping, sr)));
    }
}
