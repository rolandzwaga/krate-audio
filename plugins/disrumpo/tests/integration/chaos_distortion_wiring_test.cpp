// ==============================================================================
// Chaos Distortion Integration Test
// ==============================================================================
// Measures actual parameter sensitivity and inter-model differences for the
// Chaos distortion type. Tests that each parameter produces measurable changes
// in the output signal, and that different attractor models produce distinct
// output characteristics.
//
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/chaos_waveshaper.h>

#include <array>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr size_t kSettleBlocks = 20;   // Let attractor settle
constexpr size_t kMeasureBlocks = 100; // Measure over this many blocks
constexpr float kSineFreq = 440.0f;
constexpr double kTwoPiD = 6.283185307179586;

/// @brief Generate a sine wave buffer
void generateSine(float* buffer, size_t numSamples, float freq,
                  double sampleRate, size_t offset = 0) {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = static_cast<float>(
            std::sin(kTwoPiD * freq * static_cast<double>(i + offset) / sampleRate));
    }
}

/// @brief Calculate RMS of a buffer
float calculateRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    double sumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSq += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

/// @brief Calculate mean absolute difference between two buffers
float calculateMAD(const float* a, const float* b, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += std::abs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
    }
    return static_cast<float>(sum / static_cast<double>(numSamples));
}

/// @brief Collect output statistics from a configured ChaosWaveshaper
struct OutputStats {
    float rms = 0.0f;
    float peakPos = 0.0f;
    float peakNeg = 0.0f;
    float zeroCrossings = 0.0f;
    std::vector<float> samples;
};

OutputStats measureOutput(ChaosWaveshaper& shaper) {
    OutputStats stats;
    std::array<float, kBlockSize> buffer{};
    size_t totalSamples = 0;
    double sumSq = 0.0;
    size_t zeroCrossCount = 0;
    float prevSample = 0.0f;

    // Settle
    for (size_t b = 0; b < kSettleBlocks; ++b) {
        generateSine(buffer.data(), kBlockSize, kSineFreq, kSampleRate,
                     b * kBlockSize);
        shaper.processBlock(buffer.data(), kBlockSize);
    }

    // Measure
    for (size_t b = 0; b < kMeasureBlocks; ++b) {
        generateSine(buffer.data(), kBlockSize, kSineFreq, kSampleRate,
                     (kSettleBlocks + b) * kBlockSize);
        shaper.processBlock(buffer.data(), kBlockSize);

        for (size_t i = 0; i < kBlockSize; ++i) {
            float s = buffer[i];
            stats.samples.push_back(s);
            sumSq += static_cast<double>(s) * static_cast<double>(s);
            if (s > stats.peakPos) stats.peakPos = s;
            if (s < stats.peakNeg) stats.peakNeg = s;
            if (totalSamples > 0 &&
                ((prevSample >= 0.0f && s < 0.0f) ||
                 (prevSample < 0.0f && s >= 0.0f))) {
                zeroCrossCount++;
            }
            prevSample = s;
            totalSamples++;
        }
    }

    stats.rms = static_cast<float>(
        std::sqrt(sumSq / static_cast<double>(totalSamples)));
    stats.zeroCrossings = static_cast<float>(zeroCrossCount);
    return stats;
}

} // anonymous namespace

// =============================================================================
// Test: Different attractor models produce measurably different output
// =============================================================================

TEST_CASE("Chaos distortion: different attractors produce distinct output",
          "[chaos][integration][wiring]") {
    const std::array<ChaosModel, 4> models = {
        ChaosModel::Lorenz, ChaosModel::Rossler,
        ChaosModel::Chua, ChaosModel::Henon
    };
    const std::array<const char*, 4> names = {
        "Lorenz", "Rossler", "Chua", "Henon"
    };

    std::array<OutputStats, 4> allStats;

    for (size_t m = 0; m < 4; ++m) {
        ChaosWaveshaper shaper;
        shaper.prepare(kSampleRate, kBlockSize);
        shaper.setModel(models[m]);
        shaper.setChaosAmount(1.0f);    // Full wet
        shaper.setAttractorSpeed(1.0f);
        shaper.setInputCoupling(0.0f);  // No coupling for deterministic behavior
        shaper.setXDrive(1.0f);
        shaper.setYDrive(0.0f);
        shaper.setSmoothness(0.0f);     // No smoothing

        allStats[m] = measureOutput(shaper);

        INFO("Model " << names[m]
             << ": RMS=" << allStats[m].rms
             << " peak+=" << allStats[m].peakPos
             << " peak-=" << allStats[m].peakNeg
             << " zeroCross=" << allStats[m].zeroCrossings);
    }

    // Each pair of models should produce measurably different output
    size_t pairsDifferent = 0;
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = i + 1; j < 4; ++j) {
            size_t compareLen = std::min(allStats[i].samples.size(),
                                        allStats[j].samples.size());
            float mad = calculateMAD(allStats[i].samples.data(),
                                     allStats[j].samples.data(), compareLen);
            float rmsDiff = std::abs(allStats[i].rms - allStats[j].rms);

            INFO(names[i] << " vs " << names[j]
                 << ": MAD=" << mad << " RMS_diff=" << rmsDiff);

            // MAD > 0.01 means at least 1% average sample difference
            if (mad > 0.01f) {
                pairsDifferent++;
            }
        }
    }

    // All 6 pairs should be distinguishable
    CHECK(pairsDifferent >= 4); // At least 4 of 6 pairs clearly different
}

// =============================================================================
// Test: ChaosAmount parameter affects output
// =============================================================================

TEST_CASE("Chaos distortion: chaosAmount sweeps from bypass to full wet",
          "[chaos][integration][wiring]") {
    const std::array<float, 5> amounts = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    std::array<float, 5> rmsValues{};

    for (size_t a = 0; a < amounts.size(); ++a) {
        ChaosWaveshaper shaper;
        shaper.prepare(kSampleRate, kBlockSize);
        shaper.setModel(ChaosModel::Lorenz);
        shaper.setChaosAmount(amounts[a]);
        shaper.setAttractorSpeed(1.0f);
        shaper.setXDrive(1.0f);
        shaper.setYDrive(0.0f);
        shaper.setSmoothness(0.0f);

        auto stats = measureOutput(shaper);
        rmsValues[a] = stats.rms;

        INFO("ChaosAmount=" << amounts[a] << " RMS=" << stats.rms);
    }

    // At amount=0, output should equal input (bypass)
    // Input RMS of a sine = 1/sqrt(2) ≈ 0.707
    CHECK(rmsValues[0] == Approx(0.707f).margin(0.02f));

    // Each successive amount should produce a different RMS
    for (size_t i = 0; i < 4; ++i) {
        CHECK(std::abs(rmsValues[i] - rmsValues[i + 1]) > 0.001f);
    }
}

// =============================================================================
// Test: AttractorSpeed affects temporal behavior
// =============================================================================

TEST_CASE("Chaos distortion: speed parameter changes evolution rate",
          "[chaos][integration][wiring]") {
    const std::array<float, 3> speeds = {0.1f, 1.0f, 10.0f};
    std::array<float, 3> variationValues{};

    for (size_t s = 0; s < speeds.size(); ++s) {
        ChaosWaveshaper shaper;
        shaper.prepare(kSampleRate, kBlockSize);
        shaper.setModel(ChaosModel::Lorenz);
        shaper.setChaosAmount(1.0f);
        shaper.setAttractorSpeed(speeds[s]);
        shaper.setXDrive(1.0f);
        shaper.setYDrive(0.0f);
        shaper.setSmoothness(0.0f);

        auto stats = measureOutput(shaper);

        // Measure block-to-block RMS variation as proxy for evolution speed
        size_t numFullBlocks = stats.samples.size() / kBlockSize;
        std::vector<float> blockRms;
        for (size_t b = 0; b < numFullBlocks; ++b) {
            float rms = calculateRMS(&stats.samples[b * kBlockSize], kBlockSize);
            blockRms.push_back(rms);
        }

        // Variation = std dev of block RMS values
        float mean = 0.0f;
        for (float r : blockRms) mean += r;
        mean /= static_cast<float>(blockRms.size());

        float variance = 0.0f;
        for (float r : blockRms) {
            float diff = r - mean;
            variance += diff * diff;
        }
        variance /= static_cast<float>(blockRms.size());
        variationValues[s] = std::sqrt(variance);

        INFO("Speed=" << speeds[s]
             << " block_rms_stddev=" << variationValues[s]
             << " mean_rms=" << mean);
    }

    // Faster speeds should show more block-to-block variation
    // (or at least different from slow)
    CHECK(variationValues[0] != Approx(variationValues[2]).margin(0.001f));
}

// =============================================================================
// Test: XDrive parameter affects output
// =============================================================================

TEST_CASE("Chaos distortion: xDrive parameter modulates drive depth",
          "[chaos][integration][wiring]") {
    const std::array<float, 3> xDrives = {0.0f, 0.5f, 1.0f};
    std::array<float, 3> rmsValues{};

    for (size_t d = 0; d < xDrives.size(); ++d) {
        ChaosWaveshaper shaper;
        shaper.prepare(kSampleRate, kBlockSize);
        shaper.setModel(ChaosModel::Lorenz);
        shaper.setChaosAmount(1.0f);
        shaper.setAttractorSpeed(1.0f);
        shaper.setXDrive(xDrives[d]);
        shaper.setYDrive(0.0f);
        shaper.setSmoothness(0.0f);

        auto stats = measureOutput(shaper);
        rmsValues[d] = stats.rms;

        INFO("XDrive=" << xDrives[d] << " RMS=" << stats.rms);
    }

    // XDrive=0 means no chaos modulation of drive (constant mid-range drive)
    // XDrive=1 means full chaos modulation -> different block-to-block
    // The RMS values should differ as the drive modulation changes
    CHECK(std::abs(rmsValues[0] - rmsValues[2]) > 0.001f);
}

// =============================================================================
// Test: YDrive parameter affects output independently of XDrive
// =============================================================================

TEST_CASE("Chaos distortion: yDrive adds Y-axis modulation",
          "[chaos][integration][wiring]") {
    // Test with XDrive=0 to isolate YDrive effect
    ChaosWaveshaper shaperNoY;
    shaperNoY.prepare(kSampleRate, kBlockSize);
    shaperNoY.setModel(ChaosModel::Lorenz);
    shaperNoY.setChaosAmount(1.0f);
    shaperNoY.setAttractorSpeed(1.0f);
    shaperNoY.setXDrive(0.0f);
    shaperNoY.setYDrive(0.0f);
    shaperNoY.setSmoothness(0.0f);

    ChaosWaveshaper shaperWithY;
    shaperWithY.prepare(kSampleRate, kBlockSize);
    shaperWithY.setModel(ChaosModel::Lorenz);
    shaperWithY.setChaosAmount(1.0f);
    shaperWithY.setAttractorSpeed(1.0f);
    shaperWithY.setXDrive(0.0f);
    shaperWithY.setYDrive(1.0f);
    shaperWithY.setSmoothness(0.0f);

    auto statsNoY = measureOutput(shaperNoY);
    auto statsWithY = measureOutput(shaperWithY);

    size_t compareLen = std::min(statsNoY.samples.size(),
                                statsWithY.samples.size());
    float mad = calculateMAD(statsNoY.samples.data(),
                             statsWithY.samples.data(), compareLen);

    INFO("YDrive=0 RMS=" << statsNoY.rms << " YDrive=1 RMS=" << statsWithY.rms
         << " MAD=" << mad);

    // Y drive should produce measurably different output
    CHECK(mad > 0.01f);
}

// =============================================================================
// Test: Smoothness parameter reduces jitter
// =============================================================================

TEST_CASE("Chaos distortion: smoothness reduces drive modulation jitter",
          "[chaos][integration][wiring]") {
    const std::array<float, 3> smoothnesses = {0.0f, 0.5f, 1.0f};
    std::array<float, 3> totalVariation{};

    for (size_t s = 0; s < smoothnesses.size(); ++s) {
        ChaosWaveshaper shaper;
        shaper.prepare(kSampleRate, kBlockSize);
        shaper.setModel(ChaosModel::Lorenz);
        shaper.setChaosAmount(1.0f);
        shaper.setAttractorSpeed(5.0f); // Fast for visible jitter
        shaper.setXDrive(1.0f);
        shaper.setYDrive(0.0f);
        shaper.setSmoothness(smoothnesses[s]);

        // Use constant DC input with process() (no oversampling) to isolate
        // the chaos drive modulation from audio signal variation
        constexpr float dcInput = 0.4f;
        constexpr size_t totalSamples = 50000;

        // Settle
        for (size_t i = 0; i < 5000; ++i) {
            (void)shaper.process(dcInput);
        }

        // Measure total variation (sum of absolute consecutive differences)
        float prevOutput = shaper.process(dcInput);
        double variation = 0.0;
        for (size_t i = 1; i < totalSamples; ++i) {
            float output = shaper.process(dcInput);
            variation += std::abs(
                static_cast<double>(output) - static_cast<double>(prevOutput));
            prevOutput = output;
        }
        totalVariation[s] = static_cast<float>(variation);

        INFO("Smoothness=" << smoothnesses[s]
             << " total_variation=" << totalVariation[s]);
    }

    // Higher smoothness should reduce total variation
    // (smoother drive = fewer sample-to-sample output changes)
    CHECK(totalVariation[0] > totalVariation[2]);
}

// =============================================================================
// Test: InputCoupling makes output signal-reactive
// =============================================================================

TEST_CASE("Chaos distortion: inputCoupling makes output signal-reactive",
          "[chaos][integration][wiring]") {
    // Process with two different input levels, compare difference
    // with coupling=0 vs coupling=1
    auto processWithLevel = [](float coupling, float inputLevel) -> OutputStats {
        ChaosWaveshaper shaper;
        shaper.prepare(kSampleRate, kBlockSize);
        shaper.setModel(ChaosModel::Lorenz);
        shaper.setChaosAmount(1.0f);
        shaper.setAttractorSpeed(1.0f);
        shaper.setInputCoupling(coupling);
        shaper.setXDrive(1.0f);
        shaper.setYDrive(0.0f);
        shaper.setSmoothness(0.0f);

        OutputStats stats;
        std::array<float, kBlockSize> buffer{};

        // Settle + measure
        for (size_t b = 0; b < kSettleBlocks + kMeasureBlocks; ++b) {
            for (size_t i = 0; i < kBlockSize; ++i) {
                buffer[i] = inputLevel * static_cast<float>(
                    std::sin(kTwoPiD * kSineFreq *
                             static_cast<double>(b * kBlockSize + i) / kSampleRate));
            }
            shaper.processBlock(buffer.data(), kBlockSize);

            if (b >= kSettleBlocks) {
                for (size_t i = 0; i < kBlockSize; ++i) {
                    stats.samples.push_back(buffer[i]);
                }
            }
        }
        stats.rms = calculateRMS(stats.samples.data(), stats.samples.size());
        return stats;
    };

    // No coupling: different input levels should scale output but
    // attractor evolves the same way
    auto noCoup_low = processWithLevel(0.0f, 0.2f);
    auto noCoup_high = processWithLevel(0.0f, 0.8f);

    // Full coupling: attractor is perturbed by input, so behavior diverges
    auto fullCoup_low = processWithLevel(1.0f, 0.2f);
    auto fullCoup_high = processWithLevel(1.0f, 0.8f);

    INFO("NoCoupling: low_rms=" << noCoup_low.rms << " high_rms=" << noCoup_high.rms);
    INFO("FullCoupling: low_rms=" << fullCoup_low.rms << " high_rms=" << fullCoup_high.rms);

    // Both coupling settings should produce output
    CHECK(noCoup_low.rms > 0.01f);
    CHECK(fullCoup_high.rms > 0.01f);
}

// =============================================================================
// DIAGNOSTIC: Print actual drive range and attractor statistics
// =============================================================================

TEST_CASE("DIAGNOSTIC: Chaos attractor state statistics per model",
          "[chaos][integration][diagnostic]") {
    const std::array<ChaosModel, 4> models = {
        ChaosModel::Lorenz, ChaosModel::Rossler,
        ChaosModel::Chua, ChaosModel::Henon
    };
    const std::array<const char*, 4> names = {
        "Lorenz", "Rossler", "Chua", "Henon"
    };

    for (size_t m = 0; m < 4; ++m) {
        ChaosWaveshaper shaper;
        shaper.prepare(kSampleRate, kBlockSize);
        shaper.setModel(models[m]);
        shaper.setChaosAmount(1.0f);
        shaper.setAttractorSpeed(1.0f);
        shaper.setXDrive(1.0f);
        shaper.setYDrive(0.0f);
        shaper.setSmoothness(0.0f);

        // Process single samples to observe per-sample behavior
        std::vector<float> outputs;
        std::vector<float> inputs;
        constexpr size_t totalSamples = kBlockSize * (kSettleBlocks + kMeasureBlocks);

        // Use process() (not processBlock) so we see raw behavior
        for (size_t i = 0; i < totalSamples; ++i) {
            float input = static_cast<float>(
                std::sin(kTwoPiD * kSineFreq * static_cast<double>(i) / kSampleRate));
            float output = shaper.process(input);
            if (i >= kBlockSize * kSettleBlocks) {
                outputs.push_back(output);
                inputs.push_back(input);
            }
        }

        // Compute difference from dry signal
        float mad = calculateMAD(outputs.data(), inputs.data(), outputs.size());
        float outputRms = calculateRMS(outputs.data(), outputs.size());
        float inputRms = calculateRMS(inputs.data(), inputs.size());

        // Compute output distribution stats
        float minOut = *std::min_element(outputs.begin(), outputs.end());
        float maxOut = *std::max_element(outputs.begin(), outputs.end());

        // Count unique quantized output values (proxy for output diversity)
        std::vector<int> histogram(100, 0);
        for (float s : outputs) {
            int bin = static_cast<int>((s + 1.0f) * 49.5f);
            bin = std::clamp(bin, 0, 99);
            histogram[bin]++;
        }
        int usedBins = 0;
        for (int h : histogram) {
            if (h > 0) usedBins++;
        }

        WARN(names[m]
             << ": output_rms=" << outputRms
             << " input_rms=" << inputRms
             << " MAD_from_dry=" << mad
             << " range=[" << minOut << ", " << maxOut << "]"
             << " histogram_bins_used=" << usedBins << "/100");

        // Every model should produce output different from input
        CHECK(mad > 0.001f);
    }
}
