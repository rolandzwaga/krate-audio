#pragma once

// ==============================================================================
// Vorago - factory-preset test support (Phase 14, plan 4.8)
// ==============================================================================
// Catch2-free: this header must never include a Catch2 header, because the
// probe TU (T002), the sweep harness and the preset generator all share it and
// the generator is not a Catch2 target.
//
// PART 0 (T001b, ruling R-8): the C-7.2 sound-space descriptor and the C-7.3
// distance (plan 5.4 / 5.5). ONE implementation, used by the audibility probe
// and by the sweep. T022 and T025 extend this header; nothing else lives here
// yet.
//
// Every metric is an EXISTING helper, reused, not re-implemented:
//   - bandEnergyDb, crestFactorDb, blockRmsDb, perBandTotalVariation,
//     perBinMagnitudeFlux: Krate::DSP::TestUtils::Vorago
//     (tests/test_helpers/vorago_fixtures.h)
//   - calculateCorrelation: Krate::DSP::TestUtils
//     (tests/test_helpers/low_frequency_metrics.h)
//
// NAMESPACE HAZARD (as vorago_test_fixture.h): Krate::DSP::TestUtils::Vorago
// exists, so the helpers are always fully qualified and this header never
// writes `using namespace Krate::DSP::TestUtils;`.
// ==============================================================================

#include <low_frequency_metrics.h>
#include <vorago_fixtures.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace VoragoTest {

/// Number of relative band-energy components (b0 ... b8).
inline constexpr std::size_t kDescriptorBands = 9u;

/// Total scaled components: 9 bands + motion, flux, corr, energySpread, crest.
inline constexpr std::size_t kDescriptorComponents = kDescriptorBands + 5u;

/// @brief The C-7.2 sound-space descriptor, every component already scaled so
///        that 1.0 is one audible step (plan 5.4).
struct PresetDescriptor {
    std::array<double, kDescriptorBands> band{};
    double motion = 0.0;
    double flux = 0.0;
    double corr = 0.0;
    double energySpread = 0.0;
    double crest = 0.0;
};

namespace detail {

/// Stereo band power: the power mean of the two per-channel bandEnergyDb values.
[[nodiscard]] inline double stereoBandDb(std::span<const float> L, std::span<const float> R,
                                         double sr, double lo, double hi) {
    const double eL = Krate::DSP::TestUtils::Vorago::bandEnergyDb(L, sr, lo, hi);
    const double eR = Krate::DSP::TestUtils::Vorago::bandEnergyDb(R, sr, lo, hi);
    const double p = (std::pow(10.0, eL / 10.0) + std::pow(10.0, eR / 10.0)) / 2.0;
    return 10.0 * std::log10(std::max(p, 1e-30));
}

/// Band edges (plan 5.4): k = 0 is [20, 80]; k = 1..7 is [80*2^(k-1), 80*2^k];
/// k = 8 is [10240, 20000].
[[nodiscard]] inline double bandLoHz(std::size_t k) {
    if (k == 0u) {
        return 20.0;
    }
    return 80.0 * std::pow(2.0, static_cast<double>(k - 1u));
}

[[nodiscard]] inline double bandHiHz(std::size_t k) {
    if (k == 0u) {
        return 80.0;
    }
    if (k >= kDescriptorBands - 1u) {
        return 20000.0;
    }
    return 80.0 * std::pow(2.0, static_cast<double>(k));
}

/// log2 with the plan's 1e-6 floor (a static render returns 0.0 from both
/// movement helpers). Written as a positive test so a NaN takes the floor.
[[nodiscard]] inline double log2Floored(double v) {
    const double floored = (v > 1e-6) ? v : 1e-6;
    return std::log2(floored);
}

}  // namespace detail

/// @brief C-7.2 descriptor of one stereo minute (plan 5.4).
[[nodiscard]] inline PresetDescriptor describe(std::span<const float> L, std::span<const float> R,
                                               double sr) {
    namespace VM = Krate::DSP::TestUtils::Vorago;

    PresetDescriptor d;
    const std::size_t n = std::min(L.size(), R.size());
    const std::span<const float> l = L.first(n);
    const std::span<const float> r = R.first(n);

    // Bands, relative to the sub-free E_hi = E(80, 20000), clamped at -60 dB, /3.
    const double eHi = detail::stereoBandDb(l, r, sr, 80.0, 20000.0);
    for (std::size_t k = 0; k < kDescriptorBands; ++k) {
        const double rel =
            detail::stereoBandDb(l, r, sr, detail::bandLoHz(k), detail::bandHiHz(k)) - eHi;
        d.band[k] = std::max(rel, -60.0) / 3.0;
    }

    // Motion and flux: log2 of the channel mean, 1e-6 floor, unit 1.
    d.motion = detail::log2Floored(
        (VM::perBandTotalVariation(l, sr) + VM::perBandTotalVariation(r, sr)) / 2.0);
    d.flux = detail::log2Floored(
        (VM::perBinMagnitudeFlux(l, sr) + VM::perBinMagnitudeFlux(r, sr)) / 2.0);

    // Inter-channel correlation, unit 0.25.
    d.corr = static_cast<double>(Krate::DSP::TestUtils::calculateCorrelation(l.data(), r.data(), n))
             / 0.25;

    // Energy spread: population stddev of the one-second stereo dB values, unit 2 dB.
    const auto blockLen = static_cast<std::size_t>(sr);
    const std::vector<double> dbL = VM::blockRmsDb(l, blockLen);
    const std::vector<double> dbR = VM::blockRmsDb(r, blockLen);
    const std::size_t blocks = std::min(dbL.size(), dbR.size());
    if (blocks > 0u) {
        std::vector<double> stereoDb(blocks);
        double sum = 0.0;
        for (std::size_t b = 0; b < blocks; ++b) {
            const double p = (std::pow(10.0, dbL[b] / 10.0) + std::pow(10.0, dbR[b] / 10.0)) / 2.0;
            stereoDb[b] = 10.0 * std::log10(std::max(p, 1e-30));
            sum += stereoDb[b];
        }
        const double mean = sum / static_cast<double>(blocks);
        double var = 0.0;
        for (const double v : stereoDb) {
            var += (v - mean) * (v - mean);
        }
        d.energySpread = std::sqrt(var / static_cast<double>(blocks)) / 2.0;
    }

    // Crest factor, channel mean, unit 3 dB.
    d.crest = ((VM::crestFactorDb(l) + VM::crestFactorDb(r)) / 2.0) / 3.0;

    return d;
}

/// @brief C-7.3 distance: Euclidean over the 14 scaled components (plan 5.5).
[[nodiscard]] inline double descriptorDistance(const PresetDescriptor& a, const PresetDescriptor& b) {
    double sum = 0.0;
    for (std::size_t k = 0; k < kDescriptorBands; ++k) {
        const double diff = a.band[k] - b.band[k];
        sum += diff * diff;
    }
    const std::array<double, 5> da{a.motion - b.motion, a.flux - b.flux, a.corr - b.corr,
                                   a.energySpread - b.energySpread, a.crest - b.crest};
    for (const double diff : da) {
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

/// @brief Component-wise mean (D(P) is the mean of the three minutes, plan 5.4).
///        An empty span yields the all-zero descriptor.
[[nodiscard]] inline PresetDescriptor meanOf(std::span<const PresetDescriptor> ds) {
    PresetDescriptor m;
    if (ds.empty()) {
        return m;
    }
    for (const PresetDescriptor& d : ds) {
        for (std::size_t k = 0; k < kDescriptorBands; ++k) {
            m.band[k] += d.band[k];
        }
        m.motion += d.motion;
        m.flux += d.flux;
        m.corr += d.corr;
        m.energySpread += d.energySpread;
        m.crest += d.crest;
    }
    const double inv = 1.0 / static_cast<double>(ds.size());
    for (double& b : m.band) {
        b *= inv;
    }
    m.motion *= inv;
    m.flux *= inv;
    m.corr *= inv;
    m.energySpread *= inv;
    m.crest *= inv;
    return m;
}

}  // namespace VoragoTest
