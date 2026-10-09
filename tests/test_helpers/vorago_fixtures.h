// ==============================================================================
// Test Helper: Vorago Fixtures
// ==============================================================================
// Shared measurement toolkit for Vorago Phase 10 (specs/vorago-phase10-voice-engine,
// plan S10.2). This is TEST INFRASTRUCTURE, not production DSP code: everything
// here is header-only and `inline`, and allocation is allowed because no audio
// thread ever runs this code.
//
// Location:  tests/test_helpers/vorago_fixtures.h
// Namespace: Krate::DSP::TestUtils::Vorago
//
// NO CMAKE EDIT IS NEEDED. `tests/test_helpers` is an INTERFACE target exposing
// the whole directory (tests/test_helpers/CMakeLists.txt:7-12), which is also
// what makes this header reachable from dsp_effects_tests, where the composed
// chain TU lives.
//
// WHY A NESTED `Vorago` NAMESPACE. `Krate::DSP::TestUtils` ALREADY declares
// `spectralCentroidHz(std::span<const float>, double)` - reverb_metrics.h:291,
// landed by Phase 9. Re-declaring that name at TestUtils scope would be a
// redefinition in any TU that includes both headers (the composed chain TU does).
// So the Phase 10 helpers live one namespace deeper, and the centroid is REUSED
// by a using-declaration rather than re-implemented (reuse-first: building
// something that already exists is a defect).
//
// WHAT THIS HEADER DELIBERATELY DOES NOT INCLUDE. Not
// <allocation_operator_overrides.h>. The single owner of the global
// operator new/delete replacements is unit/systems/selectable_oscillator_test.cpp:388
// in dsp_systems_tests and unit/effects/aether_reverb_test.cpp:38 in
// dsp_effects_tests; a second include anywhere in either image is a
// duplicate-symbol link error. Cases that need allocation detection include
// <allocation_detector.h> only.
//
// PORTABILITY. No std::isnan / std::isinf / std::isfinite anywhere in this
// header, so it is safe to include from TUs compiled with -ffast-math. All
// comparisons against a threshold are written as positive tests (`!(x > 0.0)`)
// so a NaN takes the guard branch instead of silently passing.
//
// SCOPE. Two halves. The ANALYSIS half (T005) is type-free - every helper takes
// a span and a sample rate, so it compiles against no Vorago type at all. The
// VORAGO-TYPED half (T016) at the bottom holds kFastAttackEnvelopeConfig,
// applyFastAttack, makeEngine and renderEngine, and is what makes this header
// depend on vorago_engine.h.
//
// Reference: specs/vorago-phase10-voice-engine/plan.md  (S10.2, S10.3)
//            specs/vorago-phase10-voice-engine/tasks.md (T005, T016)
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/fft.h>
// The Vorago-typed half (T016). Pulls in vorago_voice.h transitively, which is
// where kEnvelopeStages / kDefaultStageLevels and the envelope setters live.
#include <krate/dsp/systems/vorago_engine.h>

// Sibling helper, quoted so it resolves relative to this file as well as through
// the INTERFACE include directory. reverb_metrics.h:291 owns spectralCentroidHz.
#include "reverb_metrics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace Krate {
namespace DSP {
namespace TestUtils {
namespace Vorago {

// =============================================================================
// Reused, not re-implemented
// =============================================================================

/// @brief Magnitude-weighted spectral centroid in Hz.
///
/// This is `Krate::DSP::TestUtils::spectralCentroidHz` (reverb_metrics.h:291),
/// pulled into the Vorago namespace so every Phase 10 metric is reachable
/// through one qualifier. It is NOT a second implementation - SC-008's Darkness
/// row and SC-005's evolution series measure exactly what Phase 9's cavern tests
/// measured.
using Krate::DSP::TestUtils::spectralCentroidHz;

// =============================================================================
// Internal spectral machinery
// =============================================================================

namespace detail {

/// pi in double precision. Named locally rather than reusing
/// `TestUtils::kPiDouble` so that a TU which pulls in both namespaces with
/// using-directives cannot make the name ambiguous.
inline constexpr double kVoragoPi = 3.14159265358979323846;

/// Absolute power floor. Guards log() of an exactly-zero bin without being
/// large enough to lift a real (numerically tiny) leakage floor.
inline constexpr double kPowerFloor = 1e-30;

/// @brief The transform size used by every helper here.
///
/// The repo FFT accepts powers of two in [kMinFFTSize, kMaxFFTSize] = [256, 8192]
/// (fft.h:44-47), so this is the largest such power of two that fits `n`, clamped
/// into that range. Matching reverb_metrics.h's rule keeps the centroid and the
/// band statistics on the same grid.
[[nodiscard]] inline std::size_t analysisFftSize(std::size_t n) noexcept {
    std::size_t size = kMinFFTSize;
    while (((size * 2u) <= n) && (size < kMaxFFTSize)) {
        size *= 2u;
    }
    return size;
}

/// @brief Periodic Hann window of length `n`.
[[nodiscard]] inline std::vector<double> hannWindow(std::size_t n) {
    std::vector<double> w(n, 0.0);
    if (n == 0u) {
        return w;
    }
    const double denom = static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
        w[i] = 0.5 - (0.5 * std::cos((2.0 * kVoragoPi * static_cast<double>(i)) / denom));
    }
    return w;
}

/// @brief Per-frame magnitude spectra: Hann-windowed, 50 % overlap.
///
/// A final partial frame is zero-padded rather than dropped, so a signal shorter
/// than one transform still yields exactly one frame.
///
/// @return One vector of (fftSize/2 + 1) magnitudes per frame; empty if the
///         input is empty or the transform could not be prepared.
[[nodiscard]] inline std::vector<std::vector<double>> frameMagnitudes(
        std::span<const float> x, std::size_t fftSize) {
    std::vector<std::vector<double>> frames;
    if (x.empty() || (fftSize == 0u)) {
        return frames;
    }

    FFT fft;
    fft.prepare(fftSize);
    if (fft.size() == 0u) {
        return frames;
    }

    const std::vector<double> window = hannWindow(fftSize);
    const std::size_t numBins = (fftSize / 2u) + 1u;
    const std::size_t hop = fftSize / 2u;

    std::vector<float> buffer(fftSize, 0.0f);
    std::vector<Complex> spectrum(numBins);

    std::size_t start = 0u;
    for (;;) {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        const std::size_t available = (start < x.size()) ? (x.size() - start) : 0u;
        const std::size_t count = std::min(available, fftSize);
        for (std::size_t i = 0; i < count; ++i) {
            buffer[i] = static_cast<float>(static_cast<double>(x[start + i]) * window[i]);
        }

        fft.forward(buffer.data(), spectrum.data());

        std::vector<double> magnitudes(numBins, 0.0);
        for (std::size_t k = 0; k < numBins; ++k) {
            const double re = static_cast<double>(spectrum[k].real);
            const double im = static_cast<double>(spectrum[k].imag);
            magnitudes[k] = std::sqrt((re * re) + (im * im));
        }
        frames.push_back(std::move(magnitudes));

        if ((start + fftSize) >= x.size()) {
            break;
        }
        start += hop;
    }

    return frames;
}

/// @brief Welch-averaged power spectrum (mean of |X_k|^2 over the frames).
///
/// Averaging is not cosmetic: a single-frame periodogram of white noise has
/// exponentially distributed per-bin power, whose geometric/arithmetic ratio is
/// exp(-gamma) ~= 0.56 - so a single-frame spectral flatness would sit a hair
/// above the 0.5 bar it is supposed to clear comfortably. Averaging K frames
/// pulls it to ~1 - 1/(2K).
[[nodiscard]] inline std::vector<double> welchPowerSpectrum(
        std::span<const float> x, std::size_t fftSize) {
    const std::vector<std::vector<double>> frames = frameMagnitudes(x, fftSize);
    std::vector<double> power((fftSize / 2u) + 1u, 0.0);
    if (frames.empty()) {
        return power;
    }
    for (const std::vector<double>& frame : frames) {
        for (std::size_t k = 0; (k < power.size()) && (k < frame.size()); ++k) {
            power[k] += frame[k] * frame[k];
        }
    }
    const double inv = 1.0 / static_cast<double>(frames.size());
    for (double& p : power) {
        p *= inv;
    }
    return power;
}

/// @brief Fractional average ranks of five samples, ties shared.
[[nodiscard]] inline std::array<double, 5> averageRanks(const std::array<double, 5>& v) {
    std::array<double, 5> ranks{};
    for (std::size_t i = 0; i < v.size(); ++i) {
        double numLess = 0.0;
        double numEqual = 0.0;
        for (std::size_t j = 0; j < v.size(); ++j) {
            if (v[j] < v[i]) {
                numLess += 1.0;
            } else if (!(v[j] > v[i])) {
                numEqual += 1.0;  // includes j == i
            }
        }
        // 1-based rank; a tied group of size m shares the mean of its m ranks.
        ranks[i] = numLess + ((numEqual + 1.0) * 0.5);
    }
    return ranks;
}

}  // namespace detail

// =============================================================================
// Band and spectrum statistics
// =============================================================================

/// @brief Energy inside [loHz, hiHz], in dB.
///
/// Bins are converted to sinusoid-equivalent amplitude (Hann coherent gain 0.5,
/// single-sided doubling) so the figure reads roughly as dBFS for a tone: a
/// full-scale sine inside the band returns about -3 dB. Only DIFFERENCES of this
/// statistic are ever gated (SC-008's Age row, its Weight row), so the exact
/// normalisation is documentation, not a threshold.
///
/// @return dB; -300.0 for an empty band, an empty span or a non-positive rate.
[[nodiscard]] inline double bandEnergyDb(std::span<const float> x, double sr,
                                         double loHz, double hiHz) {
    if (x.empty() || !(sr > 0.0) || !(hiHz > loHz)) {
        return -300.0;
    }

    const std::size_t fftSize = detail::analysisFftSize(x.size());
    const std::vector<double> power = detail::welchPowerSpectrum(x, fftSize);
    const double binHz = sr / static_cast<double>(fftSize);
    const double ampScale = 4.0 / static_cast<double>(fftSize);

    double totalPower = 0.0;
    for (std::size_t k = 0; k < power.size(); ++k) {
        const double freq = static_cast<double>(k) * binHz;
        if ((freq >= loHz) && (freq <= hiHz)) {
            const double amplitude = ampScale * std::sqrt(power[k]);
            totalPower += 0.5 * amplitude * amplitude;
        }
    }

    return 10.0 * std::log10(std::max(totalPower, detail::kPowerFloor));
}

/// @brief Spectral flatness: geometric mean over arithmetic mean of the
///        Welch-averaged power spectrum, DC excluded.
///
/// 1.0 is perfectly flat, 0.0 perfectly tonal. SC-008's Entropy row gates the
/// endpoint ratio of this number.
///
/// @return Flatness in [0, 1]; 0.0 for an empty span or a non-positive rate.
[[nodiscard]] inline double spectralFlatness(std::span<const float> x, double sr) {
    if (x.empty() || !(sr > 0.0)) {
        return 0.0;
    }

    const std::size_t fftSize = detail::analysisFftSize(x.size());
    const std::vector<double> power = detail::welchPowerSpectrum(x, fftSize);
    if (power.size() < 2u) {
        return 0.0;
    }

    double logSum = 0.0;
    double linearSum = 0.0;
    std::size_t count = 0u;
    for (std::size_t k = 1; k < power.size(); ++k) {  // skip DC
        const double p = std::max(power[k], detail::kPowerFloor);
        logSum += std::log(p);
        linearSum += p;
        ++count;
    }
    if (count == 0u) {
        return 0.0;
    }

    const double geometricMean = std::exp(logSum / static_cast<double>(count));
    const double arithmeticMean = linearSum / static_cast<double>(count);
    if (!(arithmeticMean > 0.0)) {
        return 0.0;
    }
    return geometricMean / arithmeticMean;
}

// =============================================================================
// Time-domain statistics
// =============================================================================

/// @brief Crest factor (peak over RMS) in dB. A full-scale sine returns 3.01 dB.
///
/// SC-008's Pressure row gates the endpoint difference of this number.
///
/// @return dB; 0.0 for an empty or all-zero span.
[[nodiscard]] inline double crestFactorDb(std::span<const float> x) {
    if (x.empty()) {
        return 0.0;
    }

    double peak = 0.0;
    double sumSquares = 0.0;
    for (const float sample : x) {
        const double v = static_cast<double>(sample);
        peak = std::max(peak, std::abs(v));
        sumSquares += v * v;
    }

    const double rms = std::sqrt(sumSquares / static_cast<double>(x.size()));
    if (!(rms > 0.0) || !(peak > 0.0)) {
        return 0.0;
    }
    return 20.0 * std::log10(peak / rms);
}

/// @brief Per-block RMS in dBFS, one entry per block.
///
/// A trailing partial block is measured over the samples it actually has. This
/// is SC-004b's settling trajectory and the input to its monotone-non-decreasing
/// and +/-6 dB stability clauses.
///
/// @return One dBFS value per block; empty for an empty span or a zero block
///         length. Silence floors at -240 dBFS rather than -inf.
[[nodiscard]] inline std::vector<double> blockRmsDb(std::span<const float> x,
                                                    std::size_t blockLen) {
    std::vector<double> out;
    if (x.empty() || (blockLen == 0u)) {
        return out;
    }
    out.reserve((x.size() / blockLen) + 1u);

    for (std::size_t start = 0; start < x.size(); start += blockLen) {
        const std::size_t count = std::min(blockLen, x.size() - start);
        double sumSquares = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            const double v = static_cast<double>(x[start + i]);
            sumSquares += v * v;
        }
        const double rms = std::sqrt(sumSquares / static_cast<double>(count));
        out.push_back(20.0 * std::log10(std::max(rms, 1e-12)));
    }
    return out;
}

/// @brief The click statistic: the largest sample-to-sample delta found inside
///        any window of `windowSamples` samples.
///
/// ONE implementation, shared by SC-010 (macro zipper), SC-011 (steal ramp),
/// SC-017a (body-blend zipper) and SC-018a (spectral-target edge) - four
/// criteria, one statistic, no per-case re-derivation. Each of them compares
/// this number on a transition against the same number measured clear of it,
/// with a 1.5x bound, exactly as seraphis_engine.h:232-244 states the shape.
///
/// Every adjacent pair in the span is examined exactly once (a pair straddling a
/// window boundary is attributed to the window holding its later sample), so a
/// click cannot hide in a seam. The window argument is therefore granularity,
/// not a filter: callers pass the sub-span they mean to measure.
///
/// @param windowSamples Window length; 0 or 1 means "the whole span".
/// @return Maximum |x[i] - x[i-1]|; 0.0 for a span shorter than two samples.
[[nodiscard]] inline double maxDeltaInWindow(std::span<const float> x,
                                             std::size_t windowSamples) {
    if (x.size() < 2u) {
        return 0.0;
    }
    const std::size_t window = (windowSamples < 2u) ? x.size() : windowSamples;

    double worst = 0.0;
    for (std::size_t start = 0; start < x.size(); start += window) {
        const std::size_t end = std::min(x.size(), start + window);
        const std::size_t first = std::max<std::size_t>(start, 1u);
        for (std::size_t i = first; i < end; ++i) {
            const double delta = static_cast<double>(x[i]) - static_cast<double>(x[i - 1u]);
            worst = std::max(worst, std::abs(delta));
        }
    }
    return worst;
}

/// @brief Bit-identity of two float buffers: same length and every sample's
/// IEEE-754 bit pattern equal.
///
/// This is the run-to-run EXACTNESS check the partition-independence and
/// sample-rate-recall cases make (FR-007, SC-007); it is deliberately NOT
/// `==` on floats (which would call -0.0f == 0.0f equal and NaN != NaN) and
/// NOT std::memcmp (whose object-representation semantics clang-tidy flags
/// for a type without a unique representation). Each sample is read as its
/// std::uint32_t bit pattern through std::memcpy, so the intent is explicit.
///
/// @return true when both spans have the same size and identical bits.
[[nodiscard]] inline bool bitIdentical(std::span<const float> a, std::span<const float> b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::uint32_t bitsA = 0;
        std::uint32_t bitsB = 0;
        std::memcpy(&bitsA, &a[i], sizeof(bitsA));
        std::memcpy(&bitsB, &b[i], sizeof(bitsB));
        if (bitsA != bitsB) {
            return false;
        }
    }
    return true;
}

// =============================================================================
// Movement statistics
// =============================================================================

/// @brief Mean per-band energy total variation, in dB per analysis frame.
///
/// Eight logarithmically spaced bands span [40 Hz, min(16 kHz, Nyquist)]. Each
/// band's energy is tracked in dB across the Welch frames; the total variation
/// of that series, normalised by the number of transitions, is averaged over the
/// bands. A static spectrum returns ~0; a spectrum that keeps moving returns a
/// large number. Working in dB makes the statistic level-invariant, so SC-008's
/// Movement row measures motion rather than gain.
///
/// @return dB per frame; 0.0 when fewer than two frames exist.
[[nodiscard]] inline double perBandTotalVariation(std::span<const float> x, double sr) {
    if (x.empty() || !(sr > 0.0)) {
        return 0.0;
    }

    const std::size_t fftSize = detail::analysisFftSize(x.size());
    const std::vector<std::vector<double>> frames = detail::frameMagnitudes(x, fftSize);
    if (frames.size() < 2u) {
        return 0.0;
    }

    const double binHz = sr / static_cast<double>(fftSize);
    const double nyquist = sr * 0.5;
    const double loEdge = 40.0;
    const double hiEdge = std::min(16000.0, nyquist);
    if (!(hiEdge > loEdge)) {
        return 0.0;
    }

    constexpr std::size_t kNumBands = 8u;
    const double bandRatio = std::pow(hiEdge / loEdge, 1.0 / static_cast<double>(kNumBands));
    const double transitions = static_cast<double>(frames.size() - 1u);

    double totalVariation = 0.0;
    for (std::size_t b = 0; b < kNumBands; ++b) {
        const double lo = loEdge * std::pow(bandRatio, static_cast<double>(b));
        const double hi = lo * bandRatio;

        double previousDb = 0.0;
        bool havePrevious = false;
        double bandVariation = 0.0;
        for (const std::vector<double>& frame : frames) {
            double energy = 0.0;
            for (std::size_t k = 0; k < frame.size(); ++k) {
                const double freq = static_cast<double>(k) * binHz;
                if ((freq >= lo) && (freq < hi)) {
                    energy += frame[k] * frame[k];
                }
            }
            const double db = 10.0 * std::log10(std::max(energy, detail::kPowerFloor));
            if (havePrevious) {
                bandVariation += std::abs(db - previousDb);
            }
            previousDb = db;
            havePrevious = true;
        }
        totalVariation += bandVariation / transitions;
    }

    return totalVariation / static_cast<double>(kNumBands);
}

/// @brief Mean per-bin magnitude flux, normalised by the mean magnitude.
///
/// Sum of |M_t[k] - M_{t-1}[k]| over every bin and every frame transition,
/// divided by the number of terms and by the mean magnitude - so the result is
/// dimensionless and level-invariant, which is what makes SC-008's Fog row a
/// measure of shimmer rather than of loudness. A perfectly steady spectrum
/// returns ~0.
///
/// @return Dimensionless flux; 0.0 when fewer than two frames exist.
[[nodiscard]] inline double perBinMagnitudeFlux(std::span<const float> x, double sr) {
    if (x.empty() || !(sr > 0.0)) {
        return 0.0;
    }

    const std::size_t fftSize = detail::analysisFftSize(x.size());
    const std::vector<std::vector<double>> frames = detail::frameMagnitudes(x, fftSize);
    if (frames.size() < 2u) {
        return 0.0;
    }

    double fluxSum = 0.0;
    double magnitudeSum = 0.0;
    std::size_t fluxCount = 0u;
    std::size_t magnitudeCount = 0u;

    for (std::size_t t = 0; t < frames.size(); ++t) {
        const std::vector<double>& frame = frames[t];
        for (std::size_t k = 0; k < frame.size(); ++k) {
            magnitudeSum += frame[k];
            ++magnitudeCount;
            if (t > 0u) {
                const std::vector<double>& previous = frames[t - 1u];
                if (k < previous.size()) {
                    fluxSum += std::abs(frame[k] - previous[k]);
                    ++fluxCount;
                }
            }
        }
    }

    if ((fluxCount == 0u) || (magnitudeCount == 0u)) {
        return 0.0;
    }
    const double meanMagnitude = magnitudeSum / static_cast<double>(magnitudeCount);
    if (!(meanMagnitude > 0.0)) {
        return 0.0;
    }
    return (fluxSum / static_cast<double>(fluxCount)) / meanMagnitude;
}

// =============================================================================
// Rank correlation
// =============================================================================

/// @brief Spearman rank correlation of two five-point series.
///
/// Five points because SC-008 sweeps every macro over {0, 0.25, 0.5, 0.75, 1}.
/// Ties share their average rank, so a metric that saturates over part of the
/// sweep degrades the coefficient instead of aborting it. The result is the
/// Pearson correlation of the two rank vectors.
///
/// @return rho in [-1, 1]; 0.0 when either series is constant.
[[nodiscard]] inline double spearmanRho(const std::array<double, 5>& x,
                                        const std::array<double, 5>& y) {
    const std::array<double, 5> rx = detail::averageRanks(x);
    const std::array<double, 5> ry = detail::averageRanks(y);

    double meanX = 0.0;
    double meanY = 0.0;
    for (std::size_t i = 0; i < rx.size(); ++i) {
        meanX += rx[i];
        meanY += ry[i];
    }
    meanX /= static_cast<double>(rx.size());
    meanY /= static_cast<double>(ry.size());

    double covariance = 0.0;
    double varianceX = 0.0;
    double varianceY = 0.0;
    for (std::size_t i = 0; i < rx.size(); ++i) {
        const double dx = rx[i] - meanX;
        const double dy = ry[i] - meanY;
        covariance += dx * dy;
        varianceX += dx * dx;
        varianceY += dy * dy;
    }

    const double denominator = std::sqrt(varianceX * varianceY);
    if (!(denominator > 0.0)) {
        return 0.0;
    }
    return covariance / denominator;
}


// =============================================================================
// The Vorago-typed half (T016; plan S10.2)
// =============================================================================
// Everything below names a Vorago type, which is why it could only land once
// vorago_voice.h and vorago_engine.h existed. The analysis half above stays
// type-free on purpose.

// -----------------------------------------------------------------------------
// kFastAttackEnvelopeConfig (FR-014a)
// -----------------------------------------------------------------------------

/// @brief One stage of `kFastAttackEnvelopeConfig`.
///
/// `level` is FR-014's SHIPPED stage level, reproduced verbatim rather than
/// invented: `VoragoVoice` publishes no stage-LEVEL setter at all
/// (`setEnvelopeStageTimeMs`, vorago_voice.h:1006, is the only public stage
/// write path and takes a time only), so the fixture substitutes TIMES and
/// leaves the shape of the walk exactly as the instrument ships it.
/// `fastAttackKeepsShippedLevels` pins that identity at COMPILE time, so a
/// future change to `VoragoVoice::kDefaultStageLevels` cannot leave this POD
/// quietly describing an envelope nobody installs.
struct FastAttackStage {
    float level = 0.0f;
    float ms = 0.0f;
};

/// @brief FR-014a's fast-attack envelope: six `{level, ms}` pairs and a release.
struct FastAttackEnvelope {
    std::array<FastAttackStage, static_cast<std::size_t>(VoragoVoice::kEnvelopeStages)> stages{};
    float releaseMs = 0.0f;
};

/// FR-014a's ceiling. Every time in the fixture is at or below it, and the two
/// predicates below make that a build error rather than a review item.
inline constexpr float kFastAttackCeilingMs = 100.0f;

/// @brief True when every stage time and the release sit at or below FR-014a's
///        100 ms ceiling.
[[nodiscard]] constexpr bool fastAttackIsInsideCeiling(const FastAttackEnvelope& e) noexcept {
    for (const FastAttackStage& stage : e.stages) {
        if (!(stage.ms <= kFastAttackCeilingMs)) {
            return false;
        }
    }
    return e.releaseMs <= kFastAttackCeilingMs;
}

/// @brief True when the POD's six levels are EXACTLY FR-014's shipped levels.
///
/// The fixture is allowed to shorten the envelope; it is not allowed to reshape
/// it. Comparing floats for equality is deliberate here: both sides are the same
/// literals, copied, and a drifted copy is precisely what this rules out.
[[nodiscard]] constexpr bool fastAttackKeepsShippedLevels(const FastAttackEnvelope& e) noexcept {
    for (std::size_t i = 0; i < e.stages.size(); ++i) {
        if (!(e.stages[i].level == VoragoVoice::kDefaultStageLevels[i])) {
            return false;
        }
    }
    return true;
}

/// @brief THE named fast-attack fixture (FR-014a).
///
/// WHY IT EXISTS. The shipped envelope is a 20 s attack followed by 30 / 45 /
/// 60 s body stages (vorago_voice.h:321-323), so a 1 s or 60 s measurement
/// window sees the onset and nothing else. SC-021a, SC-008 and SC-022 clause 2
/// cite this fixture BY NAME so that their renders reach a steady state inside
/// their windows.
///
/// SC-004b MAY NOT USE IT (FR-014a's second sentence). The 8 h soak renders the
/// shipped slow envelope precisely because that envelope is the property under
/// test; substituting a fast one there is exactly the forbidden "bend the
/// shipped character to fit a test window".
///
/// WHY ALL SIX STAGES CARRY 50 ms, INCLUDING THE TWO THAT SHIP 0 ms. Stages 0..3
/// are the pre-sustain walk (vorago_voice.h:315). Stage 4 is the sustain point,
/// and `advanceToNextStage()` enters `Sustaining` only AFTER the stage at
/// `sustainPoint_` has run (multi_stage_envelope.h:400-415) - so its 50 ms are
/// really consumed, but its level (0.85) equals stage 3's, which makes those
/// 50 ms a flat hold and leaves the rendered signal unchanged. Stage 5 sits
/// above the sustain point and is never entered while the gate is held. The
/// uniform 50 ms therefore costs nothing and keeps the POD honest: every pair it
/// states is a pair `applyFastAttack` actually writes.
inline constexpr FastAttackEnvelope kFastAttackEnvelopeConfig{
    {{{1.00f, 50.0f},
      {0.80f, 50.0f},
      {0.92f, 50.0f},
      {0.85f, 50.0f},
      {0.85f, 50.0f},
      {0.00f, 50.0f}}},
    100.0f};

static_assert(kFastAttackEnvelopeConfig.stages.size()
                  == static_cast<std::size_t>(VoragoVoice::kEnvelopeStages),
              "FR-014a: the fixture must describe every stage the voice walks");
static_assert(fastAttackIsInsideCeiling(kFastAttackEnvelopeConfig),
              "FR-014a: every fast-attack time must be <= 100 ms");
static_assert(fastAttackKeepsShippedLevels(kFastAttackEnvelopeConfig),
              "FR-014a: the fixture shortens the envelope, it does not reshape it - these levels "
              "must stay identical to VoragoVoice::kDefaultStageLevels");

/// @brief Install `kFastAttackEnvelopeConfig` on one voice.
///
/// `Standard` mode is set FIRST and explicitly: in `Growth` mode every
/// pre-sustain stage time is forced to 0 ms and the composite is multiplied by a
/// 120 s logistic swell instead (vorago_voice.h:990-1001), which no measurement
/// window this fixture serves could wait out.
inline void applyFastAttack(VoragoVoice& voice) noexcept {
    voice.setEnvelopeMode(VoragoVoice::EnvelopeMode::Standard);
    for (int stage = 0; stage < VoragoVoice::kEnvelopeStages; ++stage) {
        voice.setEnvelopeStageTimeMs(
            stage, kFastAttackEnvelopeConfig.stages[static_cast<std::size_t>(stage)].ms);
    }
    voice.setEnvelopeReleaseMs(kFastAttackEnvelopeConfig.releaseMs);
}

/// @brief Install `kFastAttackEnvelopeConfig` on EVERY slot of one engine.
///
/// This overload is implementable without a `const_cast` only because the engine
/// exposes the four envelope fan-out forwarders (vorago_engine.h:668-690):
/// `getVoice(i)` is const and the Voice-owned macro rows travel through
/// `friend class VoragoMacroMatrix`, so those forwarders are the engine's sole
/// mutable route to its voices. Each fans out over all `kMaxVoices`, which is
/// why one call here also covers slots a later `setPolyphony()` growth admits.
inline void applyFastAttack(VoragoEngine& engine) noexcept {
    engine.setEnvelopeMode(VoragoVoice::EnvelopeMode::Standard);
    for (int stage = 0; stage < VoragoVoice::kEnvelopeStages; ++stage) {
        engine.setEnvelopeStageTimeMs(
            stage, kFastAttackEnvelopeConfig.stages[static_cast<std::size_t>(stage)].ms);
    }
    engine.setEnvelopeReleaseMs(kFastAttackEnvelopeConfig.releaseMs);
}

// -----------------------------------------------------------------------------
// Construction and rendering
// -----------------------------------------------------------------------------

/// @brief A prepared `VoragoEngine` on the HEAP - the only construction path.
///
/// `std::array<VoragoVoice, kMaxVoices>` is hundreds of kilobytes and MSVC's
/// default main-thread stack is 1 MiB (vorago_engine.h:146-151,
/// seraphis_engine.h:201-204), so a stack-local engine is a defect, not a style
/// preference. Every case obtains its engine through this function.
///
/// @param sampleRate Forwarded verbatim; the engine CLAMPS rather than rejects.
/// @param cfg        Forwarded verbatim; every field is clamped by its owner.
[[nodiscard]] inline std::unique_ptr<VoragoEngine> makeEngine(double sampleRate,
                                                              const VoragoEngineConfig& cfg) {
    auto engine = std::make_unique<VoragoEngine>();
    engine->prepare(sampleRate, cfg);
    return engine;
}

/// @brief Render exactly @p samples frames in blocks of @p blockSize.
///
/// THE standard render loop, so that no case rolls its own partition by
/// accident - a case that means to vary the partition (SC-007's invariance arm)
/// says so with its own loop, and the difference then reads as a decision.
/// Both vectors are resized and zero-filled first, so a short engine write shows
/// up as silence rather than as stale data.
///
/// @param blockSize 0 means "one block"; a final short block is rendered as-is.
inline void renderEngine(VoragoEngine& engine, std::vector<float>& l, std::vector<float>& r,
                         std::size_t samples, std::size_t blockSize) {
    l.assign(samples, 0.0f);
    r.assign(samples, 0.0f);
    if (samples == 0u) {
        return;
    }
    const std::size_t step = (blockSize == 0u) ? samples : blockSize;
    std::size_t done = 0u;
    while (done < samples) {
        const std::size_t n = std::min(step, samples - done);
        engine.processStereoBlock(l.data() + done, r.data() + done, n);
        done += n;
    }
}
}  // namespace Vorago
}  // namespace TestUtils
}  // namespace DSP
}  // namespace Krate
