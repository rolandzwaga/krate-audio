// =============================================================================
// Layer 2: Processor Tests - SpectralSmear non-finite hygiene
//                            (specs/vorago-phase4-spectral-smear)
// =============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase4-spectral-smear/spec.md  (SC-016, SC-018 (b),
//                                                         FR-009, FR-062)
//            specs/vorago-phase4-spectral-smear/plan.md  (S8 - the poison
//                                                         clear, the re-prime
//                                                         and the silent-gap
//                                                         arithmetic)
//            specs/vorago-phase4-spectral-smear/tasks.md (T001 creates this TU,
//                                                         T011 lands these
//                                                         three cases)
//
// SCOPE OF THIS TU: SC-016, SC-018 arm (b), and FR-009's non-finite setter arm.
//
// THIS IS A SEPARATE TU BECAUSE OF ITS COMPILE FLAGS. It is the ONLY one of the
//   four Phase 4 TUs listed under "-fno-fast-math -fno-finite-math-only" in
//   dsp/tests/CMakeLists.txt. The other three must NOT be added to that block:
//   spectral_smear_test.cpp and spectral_smear_spectral_test.cpp stay out so the
//   FR-008/FR-009 guards are also exercised in the /fp:fast + -ffast-math mode
//   the header actually ships in (the
//   dsp/tests/unit/systems/resonance_drift_network_test.cpp:38-42 house rule),
//   and spectral_smear_perf_test.cpp stays out because -fno-fast-math would move
//   the figures its baselines are pinned to.
//
// NON-FINITE VALUES ARE BUILT FROM BIT PATTERNS THROUGH A VOLATILE SINK, never
//   from std::numeric_limits<float>::quiet_NaN() / infinity(): those fold to
//   FINITE GARBAGE on the macOS / Linux -ffast-math legs, and a test that
//   injects a finite number proves nothing. The idiom is transcribed from
//   dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp:143-165.
//   Finiteness is read with Krate::DSP::detail::isFinite (core/db_utils.h:118),
//   which inspects the IEEE-754 exponent field through an optimisation barrier -
//   never std::isnan / std::isinf / std::isfinite, which fold away in the same
//   place (tools/lint-nonfinite-symbols.js gates this).
//
// WHY THIS IS A REAL TRACE AND NOT A FORMALITY. A non-finite input sample walks
//   straight through the analysis: STFT::analyze windows it (NaN * 0.0f is NaN,
//   so even the tapered edges of the window do not kill it), the FFT spreads it
//   over every bin, computePolarBulk turns it into a non-finite magnitude AND a
//   non-finite phase (atan2(NaN, NaN) is NaN), and reconstructCartesianBulk then
//   computes mag * cos(phase) - so zeroing magnitudes alone would STILL
//   synthesise NaN. Worse, the magnitude integrator state[k] = m + p*(state - m)
//   would latch the non-finite value into the per-bin memory FOREVER, since
//   every later frame multiplies it by p and adds. FR-062's remedy is: clear the
//   memory, RE-ARM the priming flag, synthesise one silent frame, count it, and
//   CONTINUE - no latch (deliberately unlike AtmosphereEngine, which stops;
//   atmosphere_engine.h:2244-2260). This component sits on the GLOBAL bus, where
//   latching would convert one poisoned frame into a dead instrument.
//
// NO BIT-EXACT FLOAT GOLDENS anywhere in this TU (tools/lint-float-bit-goldens.js).
//   Every render comparison below is a MEASURED ratio against a stated dB gate,
//   and the only `==` comparisons are "this stored value was not written"
//   (setter substitution reads) and "these bytes were not touched".
// =============================================================================

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/spectral_smear.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using Krate::DSP::SpectralSmear;
using Krate::DSP::detail::isFinite;

namespace {

// =============================================================================
// Fixture constants
// =============================================================================

constexpr double      kFs48   = 48000.0;  ///< the reference rate
constexpr std::size_t kRefFft = 2048;     ///< the reference geometry (default fftSize)
constexpr std::size_t kRefHop = 512;      ///< kRefFft / kOverlapFactor

constexpr double kTestPi = 3.14159265358979323846;

// =============================================================================
// Non-finite construction (never std::numeric_limits)
// =============================================================================

struct NonFinitePattern {
    const char*   name;
    std::uint32_t bits;
};

/// The three binary32 patterns, named once rather than spelled at every
/// injection site (resonance_drift_network_nonfinite_test.cpp:130-134).
constexpr std::array<NonFinitePattern, 3> kPatterns{{
    {"quiet NaN", 0x7FC00000u},
    {"+Inf", 0x7F800000u},
    {"-Inf", 0xFF800000u},
}};

/// The binary64 twins, for prepare()'s `double sampleRate`. Same order.
constexpr std::array<std::uint64_t, 3> kPatterns64{{
    0x7FF8000000000000ULL,  // quiet NaN
    0x7FF0000000000000ULL,  // +Inf
    0xFFF0000000000000ULL,  // -Inf
}};

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// The volatile READ is the sink: it is what stops the constant from being
/// folded back into the memcpy at compile time, which is how a -ffast-math build
/// turns an "infinity" literal into a finite number.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink         = bits;
    const std::uint32_t    materialized = sink;
    float                  out          = 0.0f;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

/// @brief The binary64 twin of makeNonFinite, for prepare()'s sampleRate.
[[nodiscard]] double makeNonFiniteDouble(std::uint64_t bits) noexcept {
    volatile std::uint64_t sink         = bits;
    const std::uint64_t    materialized = sink;
    double                 out          = 0.0;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

// =============================================================================
// Render helpers (local to this TU - the fixture in spectral_smear_test.cpp is
// file-local there, and this TU may not share a header with it)
// =============================================================================

/// Copies the inputs into the output buffers and renders them IN PLACE in
/// `blockSize` chunks (the tail block is whatever is left).
void renderStereo(SpectralSmear& smear,
                  const std::vector<float>& inL, const std::vector<float>& inR,
                  std::vector<float>& outL, std::vector<float>& outR,
                  std::size_t blockSize) {
    outL = inL;
    outR = inR;
    const std::size_t total = std::min(outL.size(), outR.size());
    std::size_t       pos   = 0;
    while (pos < total) {
        const std::size_t block = std::min(blockSize, total - pos);
        smear.processBlock(outL.data() + pos, outR.data() + pos, block);
        pos += block;
    }
}

[[nodiscard]] double rmsOf(const float* data, std::size_t count) {
    if (count == 0u) return 0.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double s = static_cast<double>(data[i]);
        sum += s * s;
    }
    return std::sqrt(sum / static_cast<double>(count));
}

/// A steady 1 kHz sine. STATIONARY BY CHOICE: SC-016 (iii) compares the
/// recovered render's RMS against the un-injected reference's over the SAME
/// absolute window, and a stationary source is the only kind for which a
/// freshly PRIMED magnitude memory (state = this frame's analysed magnitude) and
/// a long-smeared one (state = the integrated magnitude) agree. On a
/// non-stationary source the 0.5 dB gate would be measuring the source, not the
/// recovery.
void fillTone(std::vector<float>& buffer, double sampleRate, double hz, float peak) {
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        buffer[i]      = static_cast<float>(static_cast<double>(peak) *
                                       std::sin(2.0 * kTestPi * hz * t));
    }
}

/// Longest run of consecutive samples whose magnitude is below `floorValue`,
/// scanning `[from, end)`. Returns the run length and, through `lastIndex`, the
/// index of that run's final sample (unchanged when the run length is zero).
[[nodiscard]] std::size_t longestSilentRun(const std::vector<float>& buffer, std::size_t from,
                                           double floorValue, std::size_t& lastIndex) {
    std::size_t best    = 0;
    std::size_t current = 0;
    for (std::size_t i = from; i < buffer.size(); ++i) {
        if (static_cast<double>(std::abs(buffer[i])) < floorValue) {
            ++current;
            if (current > best) {
                best      = current;
                lastIndex = i;
            }
        } else {
            current = 0;
        }
    }
    return best;
}

}  // namespace

// ---------------------------------------------------------------------------
// FR-009: non-finite control input SUBSTITUTES the documented default
// ---------------------------------------------------------------------------

TEST_CASE("SpectralSmear_NonFiniteSetters", "[spectral_smear]") {
    // FR-009 SUBSTITUTES, it does not reject - the setters are
    // `std::clamp(sanitise(v, dflt), lo, hi)` with
    // `sanitise(v, dflt) = isFinite(v) ? v : dflt` (atmosphere_engine.h:911's
    // shape). THIS IS DELIBERATELY THE OPPOSITE OF ResonanceDriftNetwork's
    // FR-008, whose setters open with `if (!isFinite(v)) return;` and leave the
    // PREVIOUS value standing. A reviewer arriving from Phase 3 will expect
    // rejection, which is exactly why the "after a legal write" arm below
    // exists: it is the only arm the two rules disagree on.
    //
    // std::clamp alone would NOT be enough: with v = NaN both `v < lo` and
    // `hi < v` are false, so std::clamp returns NaN unchanged.
    for (const auto& pattern : kPatterns) {
        INFO("pattern = " << pattern.name);
        const float poison = makeNonFinite(pattern.bits);
        REQUIRE_FALSE(isFinite(poison));  // the sink really did survive the build

        // --- Arm 1: a non-finite write on a fresh instance lands on the default
        {
            SpectralSmear smear;
            smear.setSmearAmount(poison);
            smear.setDecoherence(poison);
            smear.setSmearTilt(poison);
            smear.setSmearTimeLow(poison);
            smear.setSmearTimeHigh(poison);

            REQUIRE(smear.getSmearAmount() == SpectralSmear::kDefaultSmearAmount);
            REQUIRE(smear.getDecoherence() == SpectralSmear::kDefaultDecoherence);
            REQUIRE(smear.getSmearTilt() == SpectralSmear::kDefaultSmearTilt);
            REQUIRE(smear.getSmearTimeLow() == SpectralSmear::kDefaultSmearTimeLow);
            REQUIRE(smear.getSmearTimeHigh() == SpectralSmear::kDefaultSmearTimeHigh);
        }

        // --- Arm 2: a non-finite write AFTER a legal write lands on the
        // default, NOT on the previous value. Substitution vs rejection.
        {
            SpectralSmear smear;
            smear.setSmearAmount(0.8f);
            smear.setDecoherence(0.6f);
            smear.setSmearTilt(-0.4f);
            smear.setSmearTimeLow(1.5f);
            smear.setSmearTimeHigh(0.75f);
            REQUIRE(smear.getSmearAmount() == 0.8f);
            REQUIRE(smear.getDecoherence() == 0.6f);
            REQUIRE(smear.getSmearTilt() == -0.4f);
            REQUIRE(smear.getSmearTimeLow() == 1.5f);
            REQUIRE(smear.getSmearTimeHigh() == 0.75f);

            smear.setSmearAmount(poison);
            smear.setDecoherence(poison);
            smear.setSmearTilt(poison);
            smear.setSmearTimeLow(poison);
            smear.setSmearTimeHigh(poison);

            REQUIRE(smear.getSmearAmount() == SpectralSmear::kDefaultSmearAmount);
            REQUIRE(smear.getDecoherence() == SpectralSmear::kDefaultDecoherence);
            REQUIRE(smear.getSmearTilt() == SpectralSmear::kDefaultSmearTilt);
            REQUIRE(smear.getSmearTimeLow() == SpectralSmear::kDefaultSmearTimeLow);
            REQUIRE(smear.getSmearTimeHigh() == SpectralSmear::kDefaultSmearTimeHigh);
        }

        // --- Arm 3: the same rule on a PREPARED instance, so the deferred
        // pole-table rebuild cannot be fed a non-finite tau.
        {
            SpectralSmear smear;
            smear.prepare(kFs48,
                          SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = true});
            smear.setSmearTimeLow(poison);
            smear.setSmearTimeHigh(poison);
            REQUIRE(smear.getSmearTimeLow() == SpectralSmear::kDefaultSmearTimeLow);
            REQUIRE(smear.getSmearTimeHigh() == SpectralSmear::kDefaultSmearTimeHigh);
        }
    }

    // --- prepare()'s `double sampleRate` (plan S2 step 1) --------------------
    for (std::size_t p = 0; p < kPatterns64.size(); ++p) {
        INFO("double pattern index = " << p);
        const double poisonRate = makeNonFiniteDouble(kPatterns64[p]);
        REQUIRE_FALSE(isFinite(poisonRate));

        SpectralSmear smear;
        smear.prepare(poisonRate, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = true});
        REQUIRE_FALSE(smear.isPrepared());
        REQUIRE(smear.getFftSize() == 0u);
        REQUIRE(smear.getHopSize() == 0u);
        REQUIRE(smear.getNumBins() == 0u);
        REQUIRE(smear.getSampleRate() == 0.0);
        REQUIRE(smear.getLatencySamples() == 0u);
        REQUIRE(smear.getAllocatedBytes() == 0u);
    }
}

// ---------------------------------------------------------------------------
// SC-016: a poisoned frame is cleared, counted and recovered from, with no
// reset() and no latch
// ---------------------------------------------------------------------------

TEST_CASE("SpectralSmear_NonFinite", "[spectral_smear]") {
    constexpr std::size_t kTotal     = 32768;  // > E + 3*fftSize with room to spare
    constexpr std::size_t kInjectAt  = 16384;  // hop-aligned ON PURPOSE, see arm (iv)
    constexpr std::size_t kInjectLen = 512;    // "one 512-sample block"
    constexpr std::size_t kLastInjected = kInjectAt + kInjectLen - 1;  // E
    constexpr float       kPeak         = 0.5f;

    std::vector<float> cleanL(kTotal, 0.0f);
    std::vector<float> cleanR(kTotal, 0.0f);
    fillTone(cleanL, kFs48, 1000.0, kPeak);
    cleanR = cleanL;

    const auto render = [](const std::vector<float>& inL, const std::vector<float>& inR,
                           std::vector<float>& outL, std::vector<float>& outR,
                           std::size_t& poisonCount) {
        SpectralSmear smear;
        smear.setSmearAmount(1.0f);  // before prepare(): the smoothers snap to it
        smear.setDecoherence(0.0f);
        smear.setSmearTilt(0.0f);
        smear.prepare(kFs48, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = true});
        REQUIRE(smear.getLatencySamples() == kRefFft);
        REQUIRE(smear.getHopSize() == kRefHop);
        renderStereo(smear, inL, inR, outL, outR, 512u);
        poisonCount = smear.getPoisonEngagements();
    };

    // The un-injected reference, rendered once: it is pattern-independent.
    std::vector<float> refL;
    std::vector<float> refR;
    std::size_t        refPoison = 0;
    render(cleanL, cleanR, refL, refR, refPoison);
    REQUIRE(refPoison == 0u);  // a clean render must never engage the poison path

    for (const auto& pattern : kPatterns) {
        INFO("pattern = " << pattern.name);
        const float poison = makeNonFinite(pattern.bits);
        REQUIRE_FALSE(isFinite(poison));

        // ONE CHANNEL ONLY (L): the two channels have independent magnitude
        // memories and independent RNG streams, so R is the control that shows
        // the clear did not reach across.
        std::vector<float> inL = cleanL;
        for (std::size_t i = 0; i < kInjectLen; ++i) {
            inL[kInjectAt + i] = poison;
        }

        std::vector<float> outL;
        std::vector<float> outR;
        std::size_t        poisonCount = 0;
        render(inL, cleanR, outL, outR, poisonCount);

        // --- (i) nothing non-finite reaches the output ----------------------
        // Asserted over the WHOLE buffer, which is stronger than the criterion's
        // "after the injecting block": popFifo writes every output sample
        // unconditionally, so there is no window in which a non-finite value
        // could legitimately survive.
        std::size_t firstBad = kTotal;
        for (std::size_t i = 0; i < kTotal; ++i) {
            if (!isFinite(outL[i]) || !isFinite(outR[i])) {
                firstBad = i;
                break;
            }
        }
        INFO("first non-finite output index = " << firstBad);
        REQUIRE(firstBad == kTotal);

        // --- (ii) the engagement was counted --------------------------------
        REQUIRE(poisonCount >= 1u);

        // --- (iii) it recovers on its own, with NO reset() ------------------
        // The window is [E + 2*fftSize, E + 3*fftSize), which plan S8 places
        // clear of the silent gap: the last poisoned analysis frame starts at or
        // before E, so its overlap-add contribution ends by E + 2*fftSize at the
        // latest. Any window overlapping the gap would fail however correct the
        // implementation is. Recovery inside ONE frame is only reachable because
        // FR-062 RE-ARMS the priming flag - a clear that merely zeroed the
        // memory would rebuild it through (1 - p) per frame and miss this gate
        // by 12-26 dB.
        const std::size_t recoverFrom = kLastInjected + 2u * kRefFft;
        REQUIRE(recoverFrom + kRefFft <= kTotal);
        const double recoveredRms = rmsOf(outL.data() + recoverFrom, kRefFft);
        const double referenceRms = rmsOf(refL.data() + recoverFrom, kRefFft);
        REQUIRE(referenceRms > 1.0e-6);
        REQUIRE(recoveredRms > 0.0);
        const double deltaDb = 20.0 * std::log10(recoveredRms / referenceRms);
        INFO("recovered RMS = " << recoveredRms << " reference RMS = " << referenceRms
                                << " delta = " << deltaDb << " dB");
        REQUIRE(std::abs(deltaDb) <= 0.5);

        // --- (iv) the silent gap is bounded in SHAPE, not just in length ----
        // The observable is NOT a frame count - no public API exposes per-frame
        // state - but the maximal run of consecutive near-silent output SAMPLES
        // after the first injected sample, with the floor stated relative to the
        // un-injected reference's local RMS (-80 dB of it).
        //
        // THE ARITHMETIC, AND WHY THE TWO CANDIDATE NUMBERS DIFFER BY 2.5x:
        // a L = 512 injection is overlapped by
        //   floor((L - 1 + fftSize - 1)/hopSize) + 1 = floor(2558/512) + 1 = 5
        // analysis frames (the exact maximum over alignments; window tapering
        // does not reduce it, because NaN * 0.0f is NaN), and each output sample
        // is covered by numOverlaps = 4 frames, so a silent RUN is
        //   (5 - 4 + 1) * hopSize = 2 * hopSize = 1024
        // samples - NOT 5 * hopSize = 2560. The frame count is the wrong
        // quantity to assert here and is the plausible transcription error.
        //
        // WHY THE FIXTURE INJECTS ON A HOP BOUNDARY, which is the one place this
        // TU departs from the criterion's worst case. The ideal figures above
        // ignore the Hann SYNTHESIS taper, and the measurement cannot: at each
        // edge of the gap exactly ONE clean frame contributes, under w^2/1.5,
        // and w^2/1.5 sits below this arm's own -80 dB floor for the last ~66
        // samples of that frame's span (w < 0.0103 => n > 2048 - 66). So every
        // measured run is ~132 samples LONGER than the ideal one, whatever the
        // implementation does. A hop-ALIGNED 512-sample injection is overlapped
        // by 4 frames, giving 1 * hopSize = 512 ideal + ~132 taper = ~644, which
        // sits inside the stated 2 * hopSize bound with room to spare. An
        // UNALIGNED one would measure 1024 + ~132 = ~1156 and red a CORRECT
        // build - that assertion would be measuring the Hann taper, not the
        // poison clear. The bound asserted is still the criterion's 2 * hopSize,
        // and it still has teeth: a clear that failed to re-arm, or one that
        // latched, produces a run of thousands of samples or one that never ends.
        const double localReferenceRms =
            rmsOf(refL.data() + kInjectAt, std::min<std::size_t>(4u * kRefFft, kTotal - kInjectAt));
        REQUIRE(localReferenceRms > 1.0e-6);
        const double silenceFloor = 1.0e-4 * localReferenceRms;  // -80 dB

        std::size_t runEnd = 0;
        const std::size_t runLength = longestSilentRun(outL, kInjectAt, silenceFloor, runEnd);
        INFO("silent run = " << runLength << " samples, ending at " << runEnd
                             << ", floor = " << silenceFloor);
        REQUIRE(runLength <= 2u * kRefHop);

        // R was never injected, so its own memory must never have been cleared:
        // its output tracks the reference render everywhere past the warm-up.
        const double rightRms          = rmsOf(outR.data() + recoverFrom, kRefFft);
        const double rightReferenceRms = rmsOf(refR.data() + recoverFrom, kRefFft);
        REQUIRE(rightReferenceRms > 1.0e-6);
        REQUIRE(std::abs(20.0 * std::log10(rightRms / rightReferenceRms)) <= 0.5);
    }
}

// ---------------------------------------------------------------------------
// SC-018 arm (b): the poison clear RE-ARMS the priming flag
// ---------------------------------------------------------------------------

TEST_CASE("SpectralSmear_MagnitudePrimingAfterPoison", "[spectral_smear]") {
    // SC-018 ARM (b), relocated to this TU by spec correction C-17: arm (a)
    // (SpectralSmear_MagnitudePriming, spectral_smear_test.cpp) covers the
    // prepare()/reset() prime, and this arm covers the prime FR-062 re-arms -
    // which needs a non-finite injection and therefore cannot live in a TU that
    // may not name one.
    //
    // THE MEASUREMENT, and why it is sample-domain (plan S13.2, spec correction
    // C-5): the priming frame's EXCLUSIVE output contribution is the hop that
    // immediately follows the maximal silent run, because every earlier output
    // sample there is still covered by at least one poisoned (silent) frame. A
    // smearAmount = 1 render and a smearAmount = 0 render are fed the SAME input
    // with the SAME injection - FR-062's accumulator is independent of `amount`,
    // so both poison on exactly the same frames and both synthesise the same
    // silence.
    //
    // WHY THIS DISCRIMINATES. If the clear RE-ARMS the prime, the amount = 1
    // render writes NO magnitude on that frame (state = mag, so the blend is the
    // identity) and reproduces the amount = 0 identity path to FFT round-off. If
    // the clear merely ZEROED the memory, the integrator would write
    // (1 - p) * mag[k] instead - p is ~0.99 across the band at the shipped
    // endpoints - leaving a residual of ~99 % of the reference, about -0.1 dB
    // against a -60 dB gate. The margin is more than 30 dB in each direction, so
    // the arm can neither pass by accident nor fail on round-off.
    constexpr std::size_t kTotal    = 32768;
    constexpr std::size_t kInjectAt = 16384;  // ONE sample; any index poisons 4 frames
    constexpr float       kPeak     = 0.5f;

    std::vector<float> cleanL(kTotal, 0.0f);
    fillTone(cleanL, kFs48, 1000.0, kPeak);  // the 1 kHz burst both instances see
    const std::vector<float> cleanR = cleanL;

    const auto renderAt = [&](float amount, const std::vector<float>& inL,
                              std::vector<float>& outL, std::vector<float>& outR,
                              std::size_t& poisonCount) {
        SpectralSmear smear;
        smear.setSmearAmount(amount);
        smear.setDecoherence(0.0f);
        smear.setSmearTilt(0.0f);
        smear.prepare(kFs48, SpectralSmear::PrepareConfig{.fftSize = kRefFft, .enabled = true});
        renderStereo(smear, inL, cleanR, outL, outR, 512u);
        poisonCount = smear.getPoisonEngagements();
    };

    for (const auto& pattern : kPatterns) {
        INFO("pattern = " << pattern.name);
        const float poison = makeNonFinite(pattern.bits);
        REQUIRE_FALSE(isFinite(poison));

        std::vector<float> inL = cleanL;
        inL[kInjectAt]         = poison;

        std::vector<float> actualL;
        std::vector<float> actualR;
        std::size_t        actualPoison = 0;
        renderAt(1.0f, inL, actualL, actualR, actualPoison);

        std::vector<float> referenceL;
        std::vector<float> referenceR;
        std::size_t        referencePoison = 0;
        renderAt(0.0f, inL, referenceL, referenceR, referencePoison);

        // Both must have poisoned, and identically: FR-062's accumulator sums
        // the ANALYSED magnitudes, which `amount` never touches.
        REQUIRE(actualPoison >= 1u);
        REQUIRE(actualPoison == referencePoison);

        // Locate G - the last index of the maximal silent run after the
        // injection - in the REFERENCE render. A SINGLE non-finite sample at
        // index P is overlapped by the 4 analysis frames whose starts lie in
        // (P - fftSize, P], at EVERY alignment, so the ideal silent run is
        // (4 - 4 + 1) * hopSize = 512 samples; the measured one is ~130 longer
        // because the one clean frame contributing at each edge of the gap does
        // so under the Hann synthesis taper (see SpectralSmear_NonFinite arm
        // (iv) for the w^2/1.5 arithmetic). Both figures sit inside SC-016
        // (iv)'s 2 * hopSize bound, which is re-asserted here so this case
        // cannot silently start measuring a different gap from the one the
        // criterion describes.
        const double localReferenceRms = rmsOf(
            referenceL.data() + kInjectAt, std::min<std::size_t>(4u * kRefFft, kTotal - kInjectAt));
        REQUIRE(localReferenceRms > 1.0e-6);
        const double silenceFloor = 1.0e-4 * localReferenceRms;  // -80 dB, as SC-016 (iv)

        std::size_t       gapEnd = 0;
        const std::size_t runLength =
            longestSilentRun(referenceL, kInjectAt, silenceFloor, gapEnd);
        INFO("silent run = " << runLength << " samples, G = " << gapEnd);
        REQUIRE(runLength >= 1u);              // the injection really did silence something
        REQUIRE(runLength <= 2u * kRefHop);    // SC-016 (iv)'s bound still holds
        REQUIRE(gapEnd + 1u + kRefHop <= kTotal);

        // [G + 1, G + 1 + hopSize) - the priming frame's exclusive contribution.
        const std::size_t from = gapEnd + 1u;
        std::vector<float> residual(kRefHop, 0.0f);
        for (std::size_t i = 0; i < kRefHop; ++i) {
            residual[i] = actualL[from + i] - referenceL[from + i];
        }

        const double referenceRms = rmsOf(referenceL.data() + from, kRefHop);
        const double residualRms  = rmsOf(residual.data(), kRefHop);
        REQUIRE(referenceRms > 1.0e-6);  // the priming window is not degenerate

        const double relative = residualRms / referenceRms;
        INFO("reference RMS = " << referenceRms << " residual RMS = " << residualRms
                                << " ratio = " << relative);
        REQUIRE(relative <= 1.0e-3);  // -60 dB
    }
}
