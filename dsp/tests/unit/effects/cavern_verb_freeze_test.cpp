// ==============================================================================
// Layer 4: Effect Tests - CavernVerb, freeze and silence contract
//                                        (specs/vorago-phase9-cavern-space)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase9-cavern-space/spec.md
//            specs/vorago-phase9-cavern-space/plan.md
//            specs/vorago-phase9-cavern-space/tasks.md  (T001 creates this TU;
//                                                        T014 fills it)
//
// SCOPE OF THIS TU: SC-001 (freeze energy conservation, three clauses), SC-008
//   (the pinned ten-cycle timeline) and CavernVerb_SilenceContract (FR-006).
//   Separate TU because its renders are long and independent of the main TU's
//   fixtures - which is also why the T005 fixtures are DUPLICATED below rather
//   than shared: the main TU is a different translation unit and nothing in
//   this phase creates a shared test header for them.
//
// NEVER include <allocation_operator_overrides.h> here: the global operator
//   new/delete replacement for this image already lives in
//   dsp/tests/unit/effects/aether_reverb_test.cpp; a second include is a
//   duplicate-symbol link error. Use <allocation_detector.h> only - and this TU
//   needs neither, because no case here measures allocation (SC-010 lives in
//   the main TU).
//
// CONSTRUCTING NON-FINITE VALUES: not done here at all. No std::isnan /
//   std::isinf / std::isfinite appears in this file (FR-071), so it is safe
//   under -ffast-math on every leg.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <artifact_detection.h>  // P-5's ClickDetector
#include <reverb_metrics.h>      // G-2 generator, octave bands

#include <krate/dsp/effects/cavern_verb.h>
#include <krate/dsp/primitives/smoother.h>         // calculateOnePolCoefficient
#include <krate/dsp/processors/brownian_drift.h>  // the damper generator's constants

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using Krate::DSP::AetherReverb;
using Krate::DSP::CavernVerb;
using Krate::DSP::deriveStreamSeed;

namespace TestUtils = Krate::DSP::TestUtils;

namespace {

// =============================================================================
// LOCAL COPIES OF THE T005 FIXTURES (tasks.md T014: "duplicate them locally").
// Kept byte-for-byte in behaviour with dsp/tests/unit/effects/cavern_verb_test.cpp
// so a criterion measured here and a criterion measured there describe the same
// prepared instance.
// =============================================================================

constexpr double kSampleRate48 = 48000.0;

/// P-1 prepares N = 8 lines, so every per-line array here carries eight entries.
constexpr std::size_t kP1Channels = 8;

/// The control grid, as a local alias - every render in this TU steps by it so
/// that one processStereoBlock call is exactly one control step.
constexpr std::size_t kChunk = CavernVerb::kControlChunkSamples;

[[nodiscard]] std::size_t atSeconds48(double seconds) {
    return static_cast<std::size_t>(kSampleRate48 * seconds);
}

/// @brief `x` with its first `n` samples removed (the symmetric warm-up drop).
[[nodiscard]] std::vector<float> dropPrefix(const std::vector<float>& x, std::size_t n) {
    if (n >= x.size()) {
        return {};
    }
    return {x.begin() + static_cast<std::ptrdiff_t>(n), x.end()};
}

/// @brief P-1's prepared configuration: 48 kHz, N = 8, maxBlockSamples = 512,
///        seed 1, spectral stage ON at FFT 1024 (so alignSamples_ > 0).
[[nodiscard]] CavernVerb::PrepareConfig p1Config() {
    CavernVerb::PrepareConfig cfg{};
    cfg.numChannels = 8u;
    cfg.maxBlockSamples = 512u;
    cfg.maxEarlySeconds = CavernVerb::kDefaultMaxEarlySeconds;
    cfg.maxDelaySeconds = 0.50f;
    cfg.spectralDiffusionEnabled = true;
    cfg.diffusionFftSize = 1024u;
    cfg.seed = 1u;
    return cfg;
}

/// @brief Apply EVERY FR-066 default explicitly, so a future default change
///        breaks this fixture loudly instead of drifting a calibrated criterion.
void applyFr066Defaults(CavernVerb& cav) {
    cav.setSize(CavernVerb::kDefaultSize);
    cav.setDarkness(CavernVerb::kDefaultDarkness);
    cav.setDecaySeconds(CavernVerb::kDefaultDecaySeconds);
    cav.setDensity(CavernVerb::kDefaultDensity);
    cav.setDimensionality(CavernVerb::kDefaultDimensionality);
    cav.setBreath(CavernVerb::kDefaultBreath);
    cav.setFog(CavernVerb::kDefaultFog);
    cav.setEarlySizeMs(CavernVerb::kDefaultEarlySizeMs);
    cav.setEarlyLevel(CavernVerb::kDefaultEarlyLevel);
    cav.setEarlyAbsorption(CavernVerb::kDefaultEarlyAbsorption);
    cav.setEarlySend(CavernVerb::kDefaultEarlySend);
    cav.setDamperDepth(CavernVerb::kDefaultDamperDepth);
    cav.setDamperRate(CavernVerb::kDefaultDamperRate);
    cav.setWidth(CavernVerb::kDefaultWidth);
    cav.setMix(CavernVerb::kDefaultMix);
}

/// @brief P-1: prepare `cav` at `sr` with `cfg`, then apply every FR-066 default.
///
/// BY REFERENCE, never returned by value: a CavernVerb owns an AetherReverb by
/// value whose move constructor is `= default` WITH an explicit `noexcept`, so a
/// non-nothrow-movable member would silently DELETE the move rather than
/// diagnose it. Every render helper in this TU uses this shape.
void makeDefaultCavern(CavernVerb& cav, double sr, const CavernVerb::PrepareConfig& cfg) {
    cav.prepare(sr, cfg);
    applyFr066Defaults(cav);
}

void makeDefaultCavern(CavernVerb& cav, double sr) { makeDefaultCavern(cav, sr, p1Config()); }

/// @brief The AetherReverb configuration CavernVerb::prepare() builds for the
///        engine it owns, including the exact derived seed (plan S3.1 step 3).
///        A bare engine built from this is the like-for-like control.
[[nodiscard]] AetherReverb::PrepareConfig ownedEngineConfig(const CavernVerb::PrepareConfig& cfg) {
    AetherReverb::PrepareConfig ac{};
    ac.numChannels = (cfg.numChannels == 16u) ? std::size_t{16} : std::size_t{8};
    ac.maxBlockSamples = cfg.maxBlockSamples;
    ac.maxDelaySeconds = cfg.maxDelaySeconds;
    ac.shimmerEnabled = false;
    ac.bloomEnabled = false;
    ac.spectralDiffusionEnabled = cfg.spectralDiffusionEnabled;
    ac.diffusionFftSize = cfg.diffusionFftSize;
    ac.seed = deriveStreamSeed(cfg.seed, CavernVerb::kCavernReverbSalt);
    return ac;
}

/// @brief P-5's click-detector configuration, at 48 kHz.
///
/// The `.sampleRate` is corrected to 48000 DELIBERATELY: the struct default is
/// 44 100 (tests/test_helpers/artifact_detection.h:38), which would silently
/// mis-scale every reported detection time.
[[nodiscard]] TestUtils::ClickDetectorConfig clickConfig48() {
    return TestUtils::ClickDetectorConfig{.sampleRate = 48000.0f,
                                          .frameSize = std::size_t{512},
                                          .hopSize = std::size_t{256},
                                          .detectionThreshold = 5.0f,
                                          .energyThresholdDb = -60.0f,
                                          .mergeGap = std::size_t{5}};
}

[[nodiscard]] std::size_t countClicks(const TestUtils::ClickDetectorConfig& cfg,
                                      const std::vector<float>& x) {
    TestUtils::ClickDetector det(cfg);
    det.prepare();
    return det.detect(x.data(), x.size()).size();
}

/// @brief P-5's calibration ladder - the T005 form, duplicated verbatim in
///        behaviour (the deviation rationale lives in full at
///        dsp/tests/unit/effects/cavern_verb_test.cpp:260-345 and is NOT
///        restated here; the numbers below are the same numbers).
///
/// Ladder to the smallest zero-false-positive threshold, step 0.25, cap 8.0,
/// then + kCalibrationMarginSigma = 0.75 (the measured spread of that stopping
/// point across realisations - without it the gate is a coin flip on correct
/// code), still capped at 8.0. The reference must then STILL report exactly
/// zero, and the detector must still see a single-sample displacement at or
/// below 2 x the reference's own peak |dx| - so the margin cannot buy its zero
/// by going deaf.
///
/// THE ZERO-DETECTION REQUIREMENT ON THE JUDGED RENDER IS NEVER RELAXED (FR-082).
[[nodiscard]] TestUtils::ClickDetectorConfig calibratedClickConfig(
    const std::vector<float>& reference) {
    constexpr float kThresholdStep = 0.25f;
    constexpr float kThresholdCap = 8.0f;
    constexpr float kCalibrationMarginSigma = 0.75f;

    TestUtils::ClickDetectorConfig cfg = clickConfig48();
    std::size_t falsePositives = countClicks(cfg, reference);
    while ((falsePositives > 0u) && (cfg.detectionThreshold < kThresholdCap)) {
        cfg.detectionThreshold = std::min(kThresholdCap, cfg.detectionThreshold + kThresholdStep);
        falsePositives = countClicks(cfg, reference);
    }
    const float firstZero = cfg.detectionThreshold;

    cfg.detectionThreshold = std::min(kThresholdCap, firstZero + kCalibrationMarginSigma);
    falsePositives = countClicks(cfg, reference);

    float peakDx = 0.0f;
    for (std::size_t i = 1; i < reference.size(); ++i) {
        peakDx = std::max(peakDx, std::fabs(reference[i] - reference[i - 1]));
    }
    const float ceiling = std::max(0.1f, 2.0f * peakDx);

    constexpr float kProbeStart = 0.1f;
    constexpr float kProbeStep = 0.05f;
    const std::size_t mid = reference.size() / 2u;
    float probe = 0.0f;
    std::size_t controlCount = 0;
    for (float a = kProbeStart; a <= (ceiling + 1.0e-6f); a += kProbeStep) {
        std::vector<float> control = reference;
        control[mid] += a;  // ONE sample displaced, the aether precedent's shape
        controlCount = countClicks(cfg, control);
        probe = a;
        if (controlCount >= 1u) {
            break;
        }
    }

    WARN("P-5 click detector: threshold "
         << cfg.detectionThreshold << " sigma (smallest zero-false-positive threshold " << firstZero
         << " + margin " << kCalibrationMarginSigma << ", cap " << kThresholdCap
         << "), false positives " << falsePositives << ", control detections " << controlCount
         << " at probe amplitude " << probe << " (ceiling = 2 x reference peak |dx| = " << ceiling
         << ")");
    REQUIRE(falsePositives == 0u);
    REQUIRE(controlCount >= 1u);
    return cfg;
}

/// @brief SC-003 (a)'s per-control-chunk bound on ONE line's published offset,
///        written as the PARAMETERISED LAW and never as a literal - the T008
///        helper, duplicated (tasks.md T014 clause 3 names it as "T008's
///        parameterised bound").
///
///   alpha = 1 - exp(-5000/(kDriftOutputSmoothMs * sr))^kControlChunkSamples
///   bound = alpha * (kMaxDamperOctaves / kInternalStd + kMaxDamperOctaves)
///
/// At 48 kHz that is alpha = 0.0434712 and 0.19562 octaves/chunk. (The spec's
/// literal 0.02655 is wrong on two independent factors - plan S0.2 B-2 - and a
/// CORRECT implementation fails it. Do not write 0.02655.)
[[nodiscard]] float maxOffsetStepPerChunk(double sampleRate) {
    const float coeff = Krate::DSP::calculateOnePolCoefficient(
        Krate::DSP::BrownianDrift::kDriftOutputSmoothMs, static_cast<float>(sampleRate));
    const float alpha = 1.0f - std::pow(coeff, static_cast<float>(kChunk));
    return alpha * ((CavernVerb::kMaxDamperOctaves / Krate::DSP::BrownianDrift::kInternalStd) +
                    CavernVerb::kMaxDamperOctaves);
}

// =============================================================================
// dB helpers. No classifier from <cmath> is used anywhere (FR-071).
// =============================================================================

[[nodiscard]] double energyToDb(float energy) {
    return 10.0 * std::log10(std::max(static_cast<double>(energy), 1.0e-300));
}

[[nodiscard]] double meanSquareToDb(double meanSquare) {
    return 10.0 * std::log10(std::max(meanSquare, 1.0e-300));
}

/// @brief max |x[i] - x[0]| over a dB series - the deviation from the FIRST
///        (post-latch) sample, which is what SC-001 and SC-008 are written in.
[[nodiscard]] double maxDeviationDb(const std::vector<double>& series) {
    double worst = 0.0;
    for (const double v : series) {
        worst = std::max(worst, std::fabs(v - series.front()));
    }
    return worst;
}

// =============================================================================
// SC-001 clause 2's octave analysis
// =============================================================================

constexpr std::size_t kOctaveCount = 7;
constexpr double kOctaveCentres[kOctaveCount] = {125.0,  250.0,  500.0, 1000.0,
                                                 2000.0, 4000.0, 8000.0};

/// @brief Number of 1 s windows clause 2 integrates per reported octave level.
///
/// NOT a tolerance change - the +/-0.5 dB criterion is untouched. It is a
/// property of the MEASUREMENT, inherited from the source criterion's own
/// implementation (dsp/tests/unit/effects/aether_reverb_test.cpp:544-559): a
/// frozen lossless FDN is a deterministic sum of modes spaced well under 1 Hz
/// apart, so a 1 s window lets neighbouring modes beat WITHIN the window and
/// the estimator's own 1-sigma spread lands on the same order as the criterion
/// - which would make the assertion a coin toss on measurement noise rather
/// than a statement about the engine. At 10 s the window resolves 0.1 Hz, the
/// cross terms fall onto the sinc floor and the estimator becomes essentially
/// exact. Six 10 s points over 60 s still catch what the clause exists to
/// catch: a latched-but-wrong per-line coefficient drains ONE band
/// MONOTONICALLY, by tens of dB over 60 s, not by 0.5 dB of jitter.
constexpr std::size_t kOctaveGroupWindows = 10;

/// @brief Seven octave band-passes fed sample by sample, closing one energy
///        window per call to `closeWindow()`.
class OctaveAccum {
public:
    explicit OctaveAccum(double sampleRate) {
        for (std::size_t b = 0; b < kOctaveCount; ++b) {
            bands_[b] = TestUtils::makeOctaveBand(sampleRate, kOctaveCentres[b]);
        }
    }

    void push(float x) noexcept {
        for (std::size_t b = 0; b < kOctaveCount; ++b) {
            const double y = static_cast<double>(bands_[b].process(x));
            acc_[b] += y * y;
        }
    }

    void closeWindow() {
        for (std::size_t b = 0; b < kOctaveCount; ++b) {
            windows_[b].push_back(acc_[b]);
            acc_[b] = 0.0;
        }
    }

    [[nodiscard]] const std::array<std::vector<double>, kOctaveCount>& windows() const noexcept {
        return windows_;
    }

private:
    std::array<TestUtils::OctaveBand, kOctaveCount> bands_{};
    std::array<double, kOctaveCount> acc_{};
    std::array<std::vector<double>, kOctaveCount> windows_{};
};

/// @brief Group `kOctaveGroupWindows` consecutive 1 s energy windows of one band
///        into a single mean-square level in dB.
[[nodiscard]] std::vector<double> groupedOctaveLevelsDb(const std::vector<double>& rawWindows,
                                                        std::size_t windowSamples) {
    std::vector<double> out;
    const std::size_t groups = rawWindows.size() / kOctaveGroupWindows;
    out.reserve(groups);
    for (std::size_t g = 0; g < groups; ++g) {
        double e = 0.0;
        for (std::size_t w = 0; w < kOctaveGroupWindows; ++w) {
            e += rawWindows[(g * kOctaveGroupWindows) + w];
        }
        const double samples =
            static_cast<double>(windowSamples) * static_cast<double>(kOctaveGroupWindows);
        out.push_back(meanSquareToDb(e / std::max(samples, 1.0)));
    }
    return out;
}

// =============================================================================
// The SC-001 freeze protocol
// =============================================================================

/// SC-001's frozen measurement span, in seconds.
constexpr std::size_t kFrozenSeconds = 60;

/// The post-thaw span clause 3's "resumes moving after thaw" is measured over.
constexpr std::size_t kThawSeconds = 2;

struct FreezeTrace {
    /// getStateEnergy() in dB: [0] is the first post-latch sample, then one per
    /// second over the 60 s frozen span.
    std::vector<double> energyDb;
    /// One row per control chunk of the frozen span (clause 3), when recorded.
    std::vector<std::array<float, kP1Channels>> frozenOffsets;
    /// One row per control chunk of the post-thaw span (clause 3), when recorded.
    std::vector<std::array<float, kP1Channels>> thawedOffsets;
    /// Per-band, per-1 s-window output energy over the frozen span, when measured.
    std::array<std::vector<double>, kOctaveCount> octaveEnergy;
    std::size_t windowSamples = 0;
};

/// @brief SC-001's protocol: 2 s G-2 -> setFreeze(true) -> 0.25 s -> isFrozen()
///        -> 60 s of G-3, sampling the forwarded getStateEnergy() once per
///        second. Optionally records the per-chunk damper offsets (clause 3)
///        and the per-octave output energy (clause 2).
///
/// Configuration: EVERY Phase-9 addition at maximum BEFORE the freeze -
/// damperDepth 1, damperRate 1, earlyLevel 1, earlySend 1, size 1, darkness 1,
/// dimensionality 1, and breath at the caller's value. Breath is the one knob
/// the two clauses disagree on: clause 1 runs it at 1 so the matrix is MORPHING
/// throughout (the inherited criterion mandates that, and a near-static matrix
/// is exactly the easy configuration it rejects), clause 2 runs it at 0 so the
/// matrix is a fixed orthogonal map and a per-BAND bound follows from
/// losslessness at all.
[[nodiscard]] FreezeTrace runFreezeProtocol(float breath, bool measureOctaves,
                                            bool recordOffsets) {
    const std::size_t chunksPerSecond = atSeconds48(1.0) / kChunk;  // 750, exact

    CavernVerb cav;
    makeDefaultCavern(cav, kSampleRate48);
    cav.setDamperDepth(1.0f);
    cav.setDamperRate(1.0f);
    cav.setEarlyLevel(1.0f);
    cav.setEarlySend(1.0f);
    cav.setSize(1.0f);
    cav.setDarkness(1.0f);
    cav.setBreath(breath);
    cav.setDimensionality(1.0f);

    std::vector<float> inL(kChunk, 0.0f);
    std::vector<float> inR(kChunk, 0.0f);
    std::vector<float> outL(kChunk, 0.0f);
    std::vector<float> outR(kChunk, 0.0f);
    TestUtils::NoiseState stateL;
    TestUtils::NoiseState stateR;

    // --- 2 s of G-2, generated block by block from one continuous stream -----
    const std::size_t exciteChunks = 2u * chunksPerSecond;
    for (std::size_t c = 0; c < exciteChunks; ++c) {
        const auto at = static_cast<std::uint64_t>(c) * static_cast<std::uint64_t>(kChunk);
        TestUtils::fillBandLimitedNoise(std::span<float>(inL), kSampleRate48, 7717u, at, stateL);
        TestUtils::fillBandLimitedNoise(std::span<float>(inR), kSampleRate48, 33427u, at, stateR);
        cav.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kChunk);
    }

    // --- freeze, then 0.25 s of G-3 so the 50 ms latch completes -------------
    cav.setFreeze(true);
    std::fill(inL.begin(), inL.end(), 0.0f);
    std::fill(inR.begin(), inR.end(), 0.0f);
    const std::size_t latchChunks = chunksPerSecond / 4u;
    for (std::size_t c = 0; c < latchChunks; ++c) {
        cav.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kChunk);
    }
    REQUIRE(cav.isFrozen());

    FreezeTrace tr;
    tr.windowSamples = atSeconds48(1.0);
    tr.energyDb.push_back(energyToDb(cav.getStateEnergy()));
    if (recordOffsets) {
        tr.frozenOffsets.reserve(kFrozenSeconds * chunksPerSecond);
    }

    OctaveAccum octaves(kSampleRate48);

    // --- 60 s of G-3 -----------------------------------------------------------
    for (std::size_t s = 0; s < kFrozenSeconds; ++s) {
        for (std::size_t c = 0; c < chunksPerSecond; ++c) {
            cav.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kChunk);
            if (recordOffsets) {
                std::array<float, kP1Channels> row{};
                for (std::size_t i = 0; i < kP1Channels; ++i) {
                    row[i] = cav.getDamperOffsetOctaves(i);
                }
                tr.frozenOffsets.push_back(row);
            }
            if (measureOctaves) {
                for (std::size_t k = 0; k < kChunk; ++k) {
                    octaves.push(outL[k]);
                }
            }
        }
        tr.energyDb.push_back(energyToDb(cav.getStateEnergy()));
        if (measureOctaves) {
            octaves.closeWindow();
        }
    }
    if (measureOctaves) {
        tr.octaveEnergy = octaves.windows();
    }

    // --- thaw, so clause 3 can show the coefficient resuming ------------------
    if (recordOffsets) {
        cav.setFreeze(false);
        const std::size_t thawChunks = kThawSeconds * chunksPerSecond;
        tr.thawedOffsets.reserve(thawChunks);
        for (std::size_t c = 0; c < thawChunks; ++c) {
            cav.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kChunk);
            std::array<float, kP1Channels> row{};
            for (std::size_t i = 0; i < kP1Channels; ++i) {
                row[i] = cav.getDamperOffsetOctaves(i);
            }
            tr.thawedOffsets.push_back(row);
        }
    }

    return tr;
}

/// @brief A bare AetherReverb standing in for the engine CavernVerb owns, at
///        the operating point SC-001 clause 1's configuration drives it to.
///
/// CavernVerb exposes no reference to the engine it owns, so the coefficient
/// the loop actually applies (FR-041) can only be read on an engine the test
/// owns itself. Every control is applied BEFORE the first sample, so each
/// smoother SNAPS (aether_reverb.h:3055-3063) and dampCoeff_ is static for the
/// whole replay - the only thing left able to move the effective coefficient is
/// the published offset, which is exactly the subject.
void configureBareControlEngine(AetherReverb& bare) {
    bare.prepare(kSampleRate48, ownedEngineConfig(p1Config()));
    bare.setMix(1.0f);
    bare.setPreDelayMs(0.0f);
    // setSize(1.0f) / setDarkness(1.0f) on the CavernVerb map onto the engine
    // through FR-012's and FR-013's floors - at the 1.0 endpoint both land on 1.
    bare.setSize(CavernVerb::kCavernSizeFloor + (1.0f - CavernVerb::kCavernSizeFloor));
    bare.setDamping(CavernVerb::kCavernDampingFloor + (1.0f - CavernVerb::kCavernDampingFloor));
    bare.setDecaySeconds(CavernVerb::kDefaultDecaySeconds);
    bare.setDensity(CavernVerb::kDefaultDensity);
    bare.setDimensionality(1.0f);
    // Breath OFF on the control engine: the tide moves effectiveDelay_, which is
    // a second, unrelated route into dampCoeff_. This replay measures the
    // damper's contribution alone.
    bare.setSizeBreathDepth(0.0f);
    bare.setDimensionalityTideDepth(0.0f);
}

// =============================================================================
// SC-008 clause (c): cross-correlation peak lag
// =============================================================================

struct XCorrPeak {
    std::size_t lag = 0;
    double raw = 0.0;        ///< |sum in[n-lag] * out[n]| at the peak
    double rms = 0.0;        ///< RMS of |r| over every searched lag (the floor)
    double normalised = 0.0;  ///< peak / sqrt(Ein * Eout), for the record
};

/// @brief Peak |cross-correlation| of `in` against `out` over lags in
///        [lagLo, lagHi], measured on `out[start, start + len)`.
///
/// `start` must be at least `lagHi`, so every lag reads real input history.
[[nodiscard]] XCorrPeak peakCrossCorrelation(const std::vector<float>& in,
                                             const std::vector<float>& out, std::size_t start,
                                             std::size_t len, std::size_t lagLo,
                                             std::size_t lagHi) {
    XCorrPeak best;
    if ((lagHi < lagLo) || (start < lagHi) || (len == 0u) || ((start + len) > out.size()) ||
        ((start + len) > in.size())) {
        return best;
    }

    double energyOut = 0.0;
    for (std::size_t n = 0; n < len; ++n) {
        const double v = static_cast<double>(out[start + n]);
        energyOut += v * v;
    }

    double sumSquares = 0.0;
    std::size_t lagCount = 0;
    for (std::size_t lag = lagLo; lag <= lagHi; ++lag) {
        double acc = 0.0;
        const std::size_t base = start - lag;
        for (std::size_t n = 0; n < len; ++n) {
            acc += static_cast<double>(in[base + n]) * static_cast<double>(out[start + n]);
        }
        const double mag = std::fabs(acc);
        sumSquares += mag * mag;
        ++lagCount;
        if (mag > best.raw) {
            best.raw = mag;
            best.lag = lag;
        }
    }

    best.rms = std::sqrt(sumSquares / static_cast<double>(std::max<std::size_t>(lagCount, 1u)));

    // The normalised figure is reported, not asserted, so it is computed over the
    // input window the PEAK lag actually reads - not over the whole searched
    // history, which would deflate it by the width of the lag range.
    double energyIn = 0.0;
    const std::size_t inBase = start - best.lag;
    for (std::size_t n = 0; n < len; ++n) {
        const double v = static_cast<double>(in[inBase + n]);
        energyIn += v * v;
    }
    best.normalised = best.raw / std::sqrt(std::max(energyIn * energyOut, 1.0e-300));
    return best;
}

}  // namespace

// ==============================================================================
// SC-001 - freeze conserves energy, with the dampers live.
//
// Two renders, both ~62 s (plan S10.5: "2 x ~65 s"): breath 1 carries clauses 1
// and 3, breath 0 carries clause 2. NO Catch2 SECTIONs - the body runs once, so
// each render is paid exactly once; a SECTION per clause would re-run the whole
// body and pay every render again.
// ==============================================================================
TEST_CASE("CavernVerb_FreezeEnergyConservation", "[effects][cavern]") {
    // =========================================================================
    // Clause 1 - the conserved quantity, WITH THE MATRIX MORPHING.
    //
    // breath = 1 is not decoration: FR-017 drives setDimensionalityTideDepth
    // from setBreath, and the inherited criterion mandates a morphing matrix on
    // the stated ground that a naive lerp cannot survive it. Leaving breath at
    // its default would freeze a near-static matrix - the easy configuration
    // the source criterion rejects.
    // =========================================================================
    const FreezeTrace morphing = runFreezeProtocol(1.0f, /*measureOctaves=*/false,
                                                   /*recordOffsets=*/true);

    REQUIRE(morphing.energyDb.size() == (kFrozenSeconds + 1u));
    // Teeth: a frozen network holding nothing conserves 0 trivially.
    REQUIRE(morphing.energyDb.front() > -60.0);

    {
        const double worst = maxDeviationDb(morphing.energyDb);
        WARN("SC-001 clause 1 (breath 1, matrix morphing): reference state energy "
             << morphing.energyDb.front() << " dB, worst deviation over 60 s " << worst << " dB");
        INFO("worst getStateEnergy() deviation over 60 s = " << worst << " dB");
        REQUIRE(worst <= 0.5);
    }

    // =========================================================================
    // Clause 3 - the FROZEN-DAMPER clause (FR-036 + FR-046), on clause 1's own
    // render.
    //
    // Two halves, and they pull in opposite directions on purpose:
    //   (i)  FR-036: the damper GENERATORS keep running while frozen. An
    //        implementation that gates damper_[i].processBlock() on !frozen or
    //        on input activity stops dead and fails ONLY here - clauses 1 and 2
    //        are made strictly EASIER by a damper bank that stopped moving.
    //   (ii) FR-046: what the generators publish must NOT reach the loop while
    //        frozen. Replayed into a bare engine, the effective damping
    //        coefficient must be BIT-IDENTICAL at every control chunk of the
    //        frozen span, and must resume moving after thaw. That is what gates
    //        E-5's placement inside refreshControlState()'s !freezeTarget_
    //        branch (aether_reverb.h:3775-3788).
    //
    // Clause 1 does NOT gate (ii): while frozen the damping one-pole contributes
    // nothing at all, because crossfade(filterState_[i], delRead[i], freezeRamp)
    // returns EXACTLY delRead[i] at freezeRamp == 1 (aether_reverb.h:2996-3001,
    // read site :4277-4284).
    // =========================================================================
    {
        REQUIRE(morphing.frozenOffsets.size() ==
                (kFrozenSeconds * (atSeconds48(1.0) / kChunk)));
        REQUIRE(morphing.thawedOffsets.size() == (kThawSeconds * (atSeconds48(1.0) / kChunk)));

        // --- (i) the generators kept running while frozen --------------------
        const float chunkBound = maxOffsetStepPerChunk(kSampleRate48);
        const float motionBound = 10.0f * chunkBound;
        const std::array<float, kP1Channels>& firstRow = morphing.frozenOffsets.front();
        const std::array<float, kP1Channels>& lastRow = morphing.frozenOffsets.back();
        float worstMotion = 0.0f;
        for (std::size_t i = 0; i < kP1Channels; ++i) {
            worstMotion = std::max(worstMotion, std::fabs(lastRow[i] - firstRow[i]));
        }
        WARN("SC-001 clause 3 (i): largest |offset(last chunk) - offset(first chunk)| over the "
             "frozen span = "
             << worstMotion << " octaves, bound 10 x " << chunkBound << " = " << motionBound);
        INFO("largest frozen-span damper excursion = " << worstMotion << " octaves");
        REQUIRE(worstMotion > motionBound);

        // --- (ii) the loop coefficient did NOT move while frozen -------------
        AetherReverb bare;
        configureBareControlEngine(bare);

        const std::vector<float> silence(kChunk, 0.0f);
        std::vector<float> oL(kChunk, 0.0f);
        std::vector<float> oR(kChunk, 0.0f);

        // One second of settle, unfrozen, with the FIRST frozen-span offset row
        // published, so dampCoeff_ and the geometry are fully materialised
        // before anything is latched.
        const std::size_t settleChunks = atSeconds48(1.0) / kChunk;
        for (std::size_t c = 0; c < settleChunks; ++c) {
            bare.setDamperOffsetsOctaves(firstRow.data(), kP1Channels);
            bare.processStereoBlock(silence.data(), silence.data(), oL.data(), oR.data(), kChunk);
        }

        std::array<float, kP1Channels> latched{};
        for (std::size_t i = 0; i < kP1Channels; ++i) {
            latched[i] = bare.getEffectiveDampingCoefficient(i);
        }

        bare.setFreeze(true);

        // Replay EVERY frozen-span row. The verdict is accumulated rather than
        // REQUIREd per chunk: 45 000 chunks x 8 lines is 360 000 assertions,
        // and one REQUIRE naming the first offending chunk says the same thing.
        bool identical = true;
        std::size_t firstBadChunk = 0;
        std::size_t firstBadLine = 0;
        float firstBadValue = 0.0f;
        for (std::size_t c = 0; c < morphing.frozenOffsets.size(); ++c) {
            bare.setDamperOffsetsOctaves(morphing.frozenOffsets[c].data(), kP1Channels);
            bare.processStereoBlock(silence.data(), silence.data(), oL.data(), oR.data(), kChunk);
            if (!identical) {
                continue;
            }
            for (std::size_t i = 0; i < kP1Channels; ++i) {
                const float now = bare.getEffectiveDampingCoefficient(i);
                if (now != latched[i]) {  // EXACT float equality: FR-046's latch
                    identical = false;
                    firstBadChunk = c;
                    firstBadLine = i;
                    firstBadValue = now;
                    break;
                }
            }
        }
        if (!identical) {
            WARN("SC-001 clause 3 (ii): the frozen loop coefficient MOVED - chunk "
                 << firstBadChunk << ", line " << firstBadLine << ", latched " << latched[firstBadLine]
                 << " -> " << firstBadValue);
        }
        INFO("frozen-span effective damping coefficients bit-identical = " << identical);
        REQUIRE(identical);

        // --- and resumes moving after thaw -----------------------------------
        bare.setFreeze(false);
        float worstThawDelta = 0.0f;
        for (std::size_t c = 0; c < morphing.thawedOffsets.size(); ++c) {
            bare.setDamperOffsetsOctaves(morphing.thawedOffsets[c].data(), kP1Channels);
            bare.processStereoBlock(silence.data(), silence.data(), oL.data(), oR.data(), kChunk);
            for (std::size_t i = 0; i < kP1Channels; ++i) {
                worstThawDelta = std::max(
                    worstThawDelta, std::fabs(bare.getEffectiveDampingCoefficient(i) - latched[i]));
            }
        }
        WARN("SC-001 clause 3 (ii): post-thaw largest |coefficient - latched| = "
             << worstThawDelta);
        INFO("post-thaw coefficient movement = " << worstThawDelta);
        REQUIRE(worstThawDelta > 0.0f);
    }

    // =========================================================================
    // Clause 2 - PER OCTAVE, with the matrix held still.
    //
    // breath = 0 so the tide is at 0 and the matrix is a FIXED orthogonal map.
    // A morphing mixer moves energy ACROSS frequency while conserving the
    // total, so a per-band bound does not follow from losslessness while it
    // morphs - which is why this clause needs its own render and cannot ride
    // clause 1's.
    // =========================================================================
    {
        const FreezeTrace still = runFreezeProtocol(0.0f, /*measureOctaves=*/true,
                                                    /*recordOffsets=*/false);
        REQUIRE(still.energyDb.size() == (kFrozenSeconds + 1u));
        REQUIRE(still.energyDb.front() > -60.0);

        // The broadband bound must hold here too - it is the same criterion with
        // one knob moved, and a per-band pass on a collapsing total is not a
        // pass.
        const double worstTotal = maxDeviationDb(still.energyDb);
        WARN("SC-001 clause 2 (breath 0, matrix static): worst getStateEnergy() deviation "
             << worstTotal << " dB");
        REQUIRE(worstTotal <= 0.5);

        std::size_t qualified = 0;
        double worstQualifying = 0.0;
        for (std::size_t b = 0; b < kOctaveCount; ++b) {
            REQUIRE(still.octaveEnergy[b].size() == kFrozenSeconds);
            const std::vector<double> levels =
                groupedOctaveLevelsDb(still.octaveEnergy[b], still.windowSamples);
            REQUIRE(levels.size() == (kFrozenSeconds / kOctaveGroupWindows));

            const double refDb = levels.front();
            if (refDb <= -80.0) {
                // -80 dBFS reference-window gate: an octave the excitation never
                // filled is measuring the numerical floor, not the tail.
                WARN("SC-001 clause 2: octave " << kOctaveCentres[b] << " Hz gated out, reference "
                                                << refDb << " dBFS");
                continue;
            }
            ++qualified;
            const double worstBand = maxDeviationDb(levels);
            worstQualifying = std::max(worstQualifying, worstBand);
            WARN("SC-001 clause 2: octave " << kOctaveCentres[b] << " Hz reference " << refDb
                                            << " dBFS, worst deviation " << worstBand << " dB");
            INFO("octave " << kOctaveCentres[b] << " Hz worst deviation = " << worstBand << " dB");
            REQUIRE(worstBand <= 0.5);
        }

        WARN("SC-001 clause 2: " << qualified << " of " << kOctaveCount
                                 << " octaves qualified, worst qualifying deviation "
                                 << worstQualifying << " dB");
        INFO("qualified octaves = " << qualified);
        REQUIRE(qualified >= 6u);
    }
}

// ==============================================================================
// SC-008 - infinite hold is usable. THE PINNED TIMELINE, not "ten cycles":
//
//   2 s G-2  ->  [ setFreeze(true), 5 s G-3, setFreeze(false), 3 s G-2 ] x 10
//
// at P-1 with setDamperDepth(1.0). With the engine thawed between cycles and a
// 20 s default decay, the state energy at each successive freeze ENTRY depends
// entirely on the thaw duration and input, so a +/-0.5 dB comparison ACROSS
// cycles has no defined referent - clause (a) is therefore measured WITHIN each
// span.
// ==============================================================================
TEST_CASE("CavernVerb_FreezeCycles", "[effects][cavern]") {
    constexpr std::size_t kCycles = 10;
    constexpr std::size_t kThirdSpan = 2;  // clause (c)'s named span, 0-based

    const std::size_t exciteSamples = atSeconds48(2.0);
    const std::size_t frozenSamples = atSeconds48(5.0);
    const std::size_t thawedSamples = atSeconds48(3.0);
    const std::size_t cycleSamples = frozenSamples + thawedSamples;
    const std::size_t totalSamples = exciteSamples + (kCycles * cycleSamples);

    REQUIRE((totalSamples % kChunk) == 0u);

    std::array<std::size_t, kCycles> freezeOnAt{};
    std::array<std::size_t, kCycles> freezeOffAt{};
    for (std::size_t k = 0; k < kCycles; ++k) {
        freezeOnAt[k] = exciteSamples + (k * cycleSamples);
        freezeOffAt[k] = freezeOnAt[k] + frozenSamples;
    }

    // --- the input, ONE continuous G-2 stream per channel --------------------
    // Generated once for the whole 82 s and then zeroed over the G-3 regions.
    // That is not a looped buffer: the generator ran once, start to finish.
    std::vector<float> inL(totalSamples, 0.0f);
    std::vector<float> inR(totalSamples, 0.0f);
    {
        TestUtils::NoiseState stateL;
        TestUtils::NoiseState stateR;
        TestUtils::fillBandLimitedNoise(std::span<float>(inL), kSampleRate48, 4273u, 0u, stateL);
        TestUtils::fillBandLimitedNoise(std::span<float>(inR), kSampleRate48, 61879u, 0u, stateR);
    }
    for (std::size_t k = 0; k < kCycles; ++k) {
        if (k == kThirdSpan) {
            continue;  // clause (c): the third frozen span is driven by G-2
        }
        std::fill(inL.begin() + static_cast<std::ptrdiff_t>(freezeOnAt[k]),
                  inL.begin() + static_cast<std::ptrdiff_t>(freezeOffAt[k]), 0.0f);
        std::fill(inR.begin() + static_cast<std::ptrdiff_t>(freezeOnAt[k]),
                  inR.begin() + static_cast<std::ptrdiff_t>(freezeOffAt[k]), 0.0f);
    }

    // --- the judged render ----------------------------------------------------
    std::vector<float> outL(totalSamples, 0.0f);
    std::vector<float> outR(totalSamples, 0.0f);
    std::array<double, kCycles> entryDb{};
    std::array<double, kCycles> exitDb{};
    std::array<bool, kCycles> latched{};
    latched.fill(false);
    std::array<float, CavernVerb::kEarlyTapCount> tapDelaySamples{};
    std::size_t latencySamples = 0;

    {
        CavernVerb cav;
        makeDefaultCavern(cav, kSampleRate48);
        cav.setDamperDepth(1.0f);
        latencySamples = cav.getLatencySamples();
        REQUIRE(latencySamples > 0u);

        std::size_t activeSpan = kCycles;  // == kCycles means "not frozen"
        for (std::size_t n = 0; n < totalSamples; n += kChunk) {
            for (std::size_t k = 0; k < kCycles; ++k) {
                if (n == freezeOnAt[k]) {
                    cav.setFreeze(true);
                    activeSpan = k;
                }
                if (n == freezeOffAt[k]) {
                    // "at the sample before setFreeze(false)" - read FIRST.
                    exitDb[k] = energyToDb(cav.getStateEnergy());
                    cav.setFreeze(false);
                    activeSpan = kCycles;
                }
            }
            cav.processStereoBlock(&inL[n], &inR[n], &outL[n], &outR[n], kChunk);
            if ((activeSpan < kCycles) && !latched[activeSpan] && cav.isFrozen()) {
                latched[activeSpan] = true;
                entryDb[activeSpan] = energyToDb(cav.getStateEnergy());
            }
        }

        for (std::size_t i = 0; i < CavernVerb::kEarlyTapCount; ++i) {
            tapDelaySamples[i] = cav.getEarlyTapDelaySamples(i);
        }
    }

    // --- the transition-free reference: the SAME input and the SAME block
    //     partition, differing ONLY in that setFreeze is never called ---------
    std::vector<float> refL(totalSamples, 0.0f);
    {
        CavernVerb cav;
        makeDefaultCavern(cav, kSampleRate48);
        cav.setDamperDepth(1.0f);
        std::vector<float> refR(kChunk, 0.0f);
        for (std::size_t n = 0; n < totalSamples; n += kChunk) {
            cav.processStereoBlock(&inL[n], &inR[n], &refL[n], refR.data(), kChunk);
        }
    }

    // =========================================================================
    // Clause (a) - conservation WITHIN each frozen span, never across cycles.
    // The third span is excluded: its input is G-2, so its state is being
    // driven, and a conservation reading there has no referent.
    // =========================================================================
    {
        double worst = 0.0;
        for (std::size_t k = 0; k < kCycles; ++k) {
            REQUIRE(latched[k]);
            const double delta = exitDb[k] - entryDb[k];
            WARN("SC-008 (a): span " << k << " entry " << entryDb[k] << " dB, exit " << exitDb[k]
                                     << " dB, delta " << delta << " dB"
                                     << ((k == kThirdSpan) ? "  [excluded - G-2 input]" : ""));
            if (k == kThirdSpan) {
                continue;
            }
            worst = std::max(worst, std::fabs(delta));
        }
        INFO("worst within-span |delta| over the nine silent spans = " << worst << " dB");
        REQUIRE(worst <= 0.5);
    }

    // =========================================================================
    // Clause (b) - zero click detections over the whole render, P-5's config.
    // The first second is dropped from BOTH renders symmetrically: it is the
    // prepare transient, not a freeze transition.
    // =========================================================================
    {
        const std::size_t warmup = atSeconds48(1.0);
        const TestUtils::ClickDetectorConfig cfg = calibratedClickConfig(dropPrefix(refL, warmup));
        const std::size_t clicks = countClicks(cfg, dropPrefix(outL, warmup));
        WARN("SC-008 (b): 10 freeze/thaw cycles over 82 s - " << clicks << " click detections");
        INFO("click detections over the pinned timeline = " << clicks);
        REQUIRE(clicks == 0u);
    }

    // =========================================================================
    // Clause (c) - the live paths (FR-029) on the third frozen span, whose
    // input is G-2. The output must be non-zero and correlated with the input
    // at an ER tap delay: the ER bus is feed-forward and freeze does not touch
    // it, while the frozen late field's injection is scaled by (1 - freezeRamp)
    // = 0 and therefore carries no trace of the current input at all.
    // =========================================================================
    {
        const std::size_t analysisLen = atSeconds48(1.0);
        const std::size_t analysisStart = freezeOffAt[kThirdSpan] - analysisLen;

        float peakOut = 0.0f;
        for (std::size_t n = analysisStart; n < (analysisStart + analysisLen); ++n) {
            peakOut = std::max(peakOut, std::fabs(outL[n]));
        }
        REQUIRE(peakOut > 0.0f);

        // The ER line is MONO (FR-020), so the correlated quantity is the mono
        // sum on both sides.
        std::vector<float> monoIn(totalSamples, 0.0f);
        std::vector<float> monoOut(totalSamples, 0.0f);
        for (std::size_t n = 0; n < totalSamples; ++n) {
            monoIn[n] = 0.5f * (inL[n] + inR[n]);
            monoOut[n] = 0.5f * (outL[n] + outR[n]);
        }

        float minTap = tapDelaySamples[0];
        float maxTap = tapDelaySamples[0];
        for (std::size_t i = 1; i < CavernVerb::kEarlyTapCount; ++i) {
            minTap = std::min(minTap, tapDelaySamples[i]);
            maxTap = std::max(maxTap, tapDelaySamples[i]);
        }
        const auto lagLo = static_cast<std::size_t>(
            std::max(0.0f, (static_cast<float>(latencySamples) + minTap) - 64.0f));
        const auto lagHi =
            static_cast<std::size_t>((static_cast<float>(latencySamples) + maxTap) + 64.0f);
        REQUIRE(analysisStart > lagHi);

        const XCorrPeak peak =
            peakCrossCorrelation(monoIn, monoOut, analysisStart, analysisLen, lagLo, lagHi);

        // Which tap, if any, the peak lands on: FR-062's alignment delay plus
        // the tap's own size-scaled delay, within +/- 1 sample.
        std::size_t matchedTap = CavernVerb::kEarlyTapCount;
        double bestMiss = 1.0e300;
        for (std::size_t i = 0; i < CavernVerb::kEarlyTapCount; ++i) {
            const double expected =
                static_cast<double>(latencySamples) + static_cast<double>(tapDelaySamples[i]);
            const double miss = std::fabs(static_cast<double>(peak.lag) - expected);
            if (miss < bestMiss) {
                bestMiss = miss;
                if (miss <= 1.0) {
                    matchedTap = i;
                }
            }
        }

        WARN("SC-008 (c): third frozen span, peak cross-correlation at lag "
             << peak.lag << " samples (latency " << latencySamples << " + tap), closest tap miss "
             << bestMiss << " samples, matched tap "
             << ((matchedTap < CavernVerb::kEarlyTapCount) ? static_cast<int>(matchedTap) : -1)
             << ", normalised |r| " << peak.normalised << ", peak/floor "
             << (peak.raw / std::max(peak.rms, 1.0e-300)));
        INFO("closest ER tap miss = " << bestMiss << " samples");
        REQUIRE(matchedTap < CavernVerb::kEarlyTapCount);
        // Teeth: the peak must be a real feature, not the largest value of a
        // flat, uncorrelated correlogram.
        REQUIRE(peak.raw > (4.0 * peak.rms));
    }
}

// ==============================================================================
// FR-006 - the silence() contract. THE ONLY CASE IN THIS PHASE THAT CALLS
// silence(): no spec criterion does, which is why R-14's mitigation is this
// case rather than SC-008 (b).
//
// THE TIMELINE, and why it is the literal one: the render is 4 s, G-2 occupies
// its first 2 s and silence() is called at the 2 s mark - i.e. mid-render - and
// the render then continues to 4 s. The input is digital silence after the
// call, and that is forced, not chosen: the engine's own gate REOPENS once the
// amortized clear finishes (aether_reverb.h:2164-2166, "NO reset() is
// required"), so an input still running at the recovery point refills the
// network and no correct implementation could report getStateEnergy() at or
// below -80 dBFS there.
// ==============================================================================
TEST_CASE("CavernVerb_SilenceContract", "[effects][cavern]") {
    const std::size_t totalSamples = atSeconds48(4.0);
    const std::size_t callAt = atSeconds48(2.0);

    std::vector<float> inL(totalSamples, 0.0f);
    std::vector<float> inR(totalSamples, 0.0f);
    {
        TestUtils::NoiseState stateL;
        TestUtils::NoiseState stateR;
        TestUtils::fillBandLimitedNoise(std::span<float>(inL).first(callAt), kSampleRate48, 8941u,
                                        0u, stateL);
        TestUtils::fillBandLimitedNoise(std::span<float>(inR).first(callAt), kSampleRate48, 20399u,
                                        0u, stateR);
    }

    // --- the judged render ----------------------------------------------------
    std::vector<float> outL(totalSamples, 0.0f);
    std::vector<float> outR(totalSamples, 0.0f);
    std::size_t recoveredAt = totalSamples;
    double energyAtRecoveryDb = 0.0;
    double energyAtEndDb = 0.0;

    {
        CavernVerb cav;
        makeDefaultCavern(cav, kSampleRate48);
        // The precondition R-14 is about: with the spectral stage on, the four
        // alignment lines are a full FFT long, so "tidying" them into the clear
        // would punch a 1024-sample hole.
        REQUIRE(cav.getLatencySamples() > 0u);

        bool called = false;
        for (std::size_t n = 0; n < totalSamples; n += kChunk) {
            if (n == callAt) {
                cav.silence();
                called = true;
            }
            cav.processStereoBlock(&inL[n], &inR[n], &outL[n], &outR[n], kChunk);
            if (called && (recoveredAt == totalSamples) && !cav.isRecovering()) {
                recoveredAt = n + kChunk;
                energyAtRecoveryDb = energyToDb(cav.getStateEnergy());
            }
        }
        energyAtEndDb = energyToDb(cav.getStateEnergy());
    }

    // --- the transition-free reference: identical in every way except that
    //     silence() is never called ------------------------------------------
    std::vector<float> refL(totalSamples, 0.0f);
    {
        CavernVerb cav;
        makeDefaultCavern(cav, kSampleRate48);
        std::vector<float> refR(kChunk, 0.0f);
        for (std::size_t n = 0; n < totalSamples; n += kChunk) {
            cav.processStereoBlock(&inL[n], &inR[n], &refL[n], refR.data(), kChunk);
        }
    }

    // --- the gate and the amortized clear both completed ---------------------
    WARN("FR-006 silence(): recovery (gate open AND clear finished) at sample "
         << recoveredAt << " (" << (static_cast<double>(recoveredAt - callAt) * 1000.0 / kSampleRate48)
         << " ms after the call)");
    REQUIRE(recoveredAt < totalSamples);

    // --- zero P-5 detections over the second FOLLOWING the call --------------
    {
        const std::size_t windowLen = atSeconds48(1.0);
        std::vector<float> judged(outL.begin() + static_cast<std::ptrdiff_t>(callAt),
                                  outL.begin() + static_cast<std::ptrdiff_t>(callAt + windowLen));
        std::vector<float> reference(
            refL.begin() + static_cast<std::ptrdiff_t>(callAt),
            refL.begin() + static_cast<std::ptrdiff_t>(callAt + windowLen));

        const TestUtils::ClickDetectorConfig cfg = calibratedClickConfig(reference);
        const std::size_t clicks = countClicks(cfg, judged);
        WARN("FR-006 silence(): " << clicks << " click detections over the second after the call");
        INFO("click detections after silence() = " << clicks);
        REQUIRE(clicks == 0u);
    }

    // --- the state is actually gone ------------------------------------------
    WARN("FR-006 silence(): getStateEnergy() " << energyAtRecoveryDb << " dB at recovery, "
                                               << energyAtEndDb << " dB at the end of the render");
    INFO("state energy at recovery = " << energyAtRecoveryDb << " dB");
    REQUIRE(energyAtRecoveryDb <= -80.0);
    REQUIRE(energyAtEndDb <= -80.0);
}
