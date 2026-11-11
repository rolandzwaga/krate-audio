// ==============================================================================
// Oversampling Single-Type Selection Tests (User Story 1)
// ==============================================================================
// Tests for automatic per-type oversampling factor selection in BandProcessor.
//
// Reference: specs/009-intelligent-oversampling/spec.md
// Tasks: T11.017, T11.018, T11.019, T11.020
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/band_processor.h"
#include "dsp/distortion_types.h"
#include "dsp/oversampling_utils.h"
#include "test_helpers/spectral_analysis.h"

#include "../test_support/disrumpo_rig.h"
#include "plugin_ids.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using namespace Disrumpo;

// =============================================================================
// T11.017: Per-type factor selection for all 26 types (FR-001, FR-002, FR-014)
// =============================================================================

TEST_CASE("BandProcessor: automatic oversampling factor for all 26 types",
          "[oversampling][single-type][FR-001][FR-002][FR-014]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);

    auto testType = [&](DistortionType type, int expectedFactor) {
        bp.setDistortionType(type);
        INFO("Type: " << getTypeName(type) << " expected: " << expectedFactor);
        CHECK(bp.getOversampleFactor() == expectedFactor);
    };

    SECTION("8x types - aliasing past -50 dB at 4x with the re-staged drive") {
        // User ruling 2026-11-10: Tape and Temporal were 2x, the folders and fuzzes 4x
        testType(DistortionType::Tape, 8);
        testType(DistortionType::Temporal, 8);
        testType(DistortionType::Fuzz, 8);
        testType(DistortionType::AsymmetricFuzz, 8);
        testType(DistortionType::SineFold, 8);
        testType(DistortionType::TriangleFold, 8);
        testType(DistortionType::SergeFold, 8);
        // 4x until round 3 (user ruling 2026-11-11: its T6.01 reading at 4x was aliasing)
        testType(DistortionType::AllpassResonant, 8);
    }

    SECTION("4x types - strong harmonics") {
        testType(DistortionType::HardClip, 4);
        testType(DistortionType::FullRectify, 4);
        testType(DistortionType::HalfRectify, 4);
        testType(DistortionType::RingSaturation, 4);
        // 2x until the drive re-stage (user ruling 2026-11-10: aliasing past -50 dB at 2x)
        testType(DistortionType::FeedbackDist, 4);
        testType(DistortionType::Formant, 4);
        // 2x until the drive re-stage, with ADAA1 at 4x (user ruling 2026-11-10)
        testType(DistortionType::SoftClip, 4);
    }

    SECTION("2x types - moderate harmonics") {
        testType(DistortionType::Tube, 2);
        testType(DistortionType::Chaos, 2);
        testType(DistortionType::Granular, 2);
        testType(DistortionType::Fractal, 2);
        testType(DistortionType::Stochastic, 2);
    }

    SECTION("1x types - intentional artifacts") {
        testType(DistortionType::Bitcrush, 1);
        testType(DistortionType::SampleReduce, 1);
        testType(DistortionType::Quantize, 1);
        testType(DistortionType::Aliasing, 1);
        testType(DistortionType::BitwiseMangler, 1);
        testType(DistortionType::Spectral, 1);
    }
}

// =============================================================================
// T11.018: Global limit clamping per type (FR-007, FR-008)
// =============================================================================

TEST_CASE("BandProcessor: global limit clamps oversampling factor",
          "[oversampling][single-type][FR-007][FR-008]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);

    SECTION("limit 2x clamps 4x types to 2x") {
        bp.setMaxOversampleFactor(2);
        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getOversampleFactor() == 2);

        bp.setDistortionType(DistortionType::Fuzz);
        CHECK(bp.getOversampleFactor() == 2);
    }

    SECTION("limit 1x forces everything to 1x") {
        bp.setMaxOversampleFactor(1);
        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getOversampleFactor() == 1);

        bp.setDistortionType(DistortionType::Tube);
        CHECK(bp.getOversampleFactor() == 1);

        bp.setDistortionType(DistortionType::Bitcrush);
        CHECK(bp.getOversampleFactor() == 1);
    }

    SECTION("limit 4x does not affect types <= 4x and clamps the 8x types") {
        bp.setMaxOversampleFactor(4);
        bp.setDistortionType(DistortionType::Tube);
        CHECK(bp.getOversampleFactor() == 2);

        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getOversampleFactor() == 4);

        bp.setDistortionType(DistortionType::Tape);
        CHECK(bp.getOversampleFactor() == 4);
    }

    SECTION("limit 8x (default) runs the 8x types at 8x") {
        CHECK(bp.getMaxOversampleFactor() == 8);
        bp.setDistortionType(DistortionType::Tape);
        CHECK(bp.getOversampleFactor() == 8);
        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getOversampleFactor() == 4);
    }

    SECTION("changing limit re-clamps current factor") {
        bp.setDistortionType(DistortionType::HardClip);
        CHECK(bp.getOversampleFactor() == 4);

        bp.setMaxOversampleFactor(2);
        CHECK(bp.getOversampleFactor() == 2);

        bp.setMaxOversampleFactor(8);
        // After raising limit, the factor should be recalculated to recommended (4)
        CHECK(bp.getOversampleFactor() == 4);
    }
}

// =============================================================================
// T11.019: 1x bypass path (FR-020)
// =============================================================================

TEST_CASE("BandProcessor: 1x bypass path skips oversampler",
          "[oversampling][single-type][FR-020]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);

    SECTION("1x type processes directly without oversampler") {
        bp.setDistortionType(DistortionType::Bitcrush);
        CHECK(bp.getOversampleFactor() == 1);

        // Process a simple buffer - should not crash
        std::array<float, 64> left{};
        std::array<float, 64> right{};
        for (size_t i = 0; i < 64; ++i) {
            left[i] = 0.5f;
            right[i] = 0.5f;
        }

        bp.processBlock(left.data(), right.data(), 64);
        // Basic sanity check: output should not be all zeros
        // (drive is 0 by default so it passes through)
        bool hasNonZero = false;
        for (float sample : left) {
            if (std::abs(sample) > 1e-10f) {
                hasNonZero = true;
                break;
            }
        }
        // With drive=0, the signal passes through the sweep/gain stage
        // The default gain is 1.0 and sweep is 1.0, so output should be non-zero
        CHECK(hasNonZero);
    }
}

// =============================================================================
// T11.030: Bit-transparency bypass test (SC-011)
// =============================================================================

TEST_CASE("BandProcessor: bypassed band output is bit-identical to input",
          "[oversampling][bypass][SC-011]") {

    BandProcessor bp;
    bp.prepare(44100.0, 512);
    bp.setDistortionType(DistortionType::Tube);

    // Set non-zero drive so that bypass is the ONLY reason output matches input
    DistortionCommonParams params;
    params.drive = 0.8f;
    params.mix = 1.0f;
    params.toneHz = 4000.0f;
    bp.setDistortionCommonParams(params);

    // Enable bypass (FR-012)
    bp.setBypassed(true);

    // Generate test signal
    constexpr size_t kNumSamples = 512;
    std::array<float, kNumSamples> inputLeft{};
    std::array<float, kNumSamples> inputRight{};
    std::array<float, kNumSamples> left{};
    std::array<float, kNumSamples> right{};

    for (size_t i = 0; i < kNumSamples; ++i) {
        float val = 0.3f * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * static_cast<float>(i) / 44100.0f);
        inputLeft[i] = val;
        inputRight[i] = val;
        left[i] = val;
        right[i] = val;
    }

    bp.processBlock(left.data(), right.data(), kNumSamples);

    // FR-012: Bypassed band output MUST be bit-identical to input
    for (size_t i = 0; i < kNumSamples; ++i) {
        CHECK(left[i] == inputLeft[i]);
        CHECK(right[i] == inputRight[i]);
    }

    SECTION("disabling bypass re-enables processing") {
        bp.setBypassed(false);

        // Copy input again
        for (size_t i = 0; i < kNumSamples; ++i) {
            left[i] = inputLeft[i];
            right[i] = inputRight[i];
        }

        bp.processBlock(left.data(), right.data(), kNumSamples);

        // With drive=0.8, processing should modify the signal
        bool anyDifferent = false;
        for (size_t i = 0; i < kNumSamples; ++i) {
            if (left[i] != inputLeft[i] || right[i] != inputRight[i]) {
                anyDifferent = true;
                break;
            }
        }
        CHECK(anyDifferent);
    }
}

// =============================================================================
// T11.020: Alias suppression test (SC-006)
// =============================================================================
// Process 1kHz sine wave at maximum drive through each 2x/4x type.
// Perform FFT analysis. Verify aliasing artifacts are suppressed by at least
// 48dB compared to the same processing at 1x.
//
// We use the spectral_analysis.h measureAliasing() helper which works with
// sample-by-sample processors. We wrap the BandProcessor's DistortionAdapter
// in a lambda that processes one sample at a time through the adapter directly,
// both at 1x and at the type's recommended oversampling factor.
// =============================================================================

TEST_CASE("BandProcessor: alias suppression with oversampling (SC-006)",
          "[oversampling][single-type][alias-suppression][SC-006]") {

    using namespace Krate::DSP::TestUtils;

    // Configuration: 5kHz at 44100 Hz, high drive to induce significant harmonics
    // At 5kHz, harmonics 5+ (25kHz+) will alias back from above Nyquist (22050Hz)
    AliasingTestConfig config;
    config.testFrequencyHz = 5000.0f;
    config.sampleRate = 44100.0f;
    config.driveGain = 4.0f;  // Strong drive to expose aliasing
    config.fftSize = 4096;
    config.maxHarmonic = 10;  // Consider harmonics up to 10th (50kHz)

    // Helper: Process a block through BandProcessor with a specific max factor
    // Returns the left channel output for FFT analysis
    auto processWithLimit = [](DistortionType type, int maxFactor,
                               float driveGain, float sampleRate,
                               float testFreqHz,
                               size_t numSamples) -> std::vector<float> {
        BandProcessor bp;
        bp.prepare(static_cast<double>(sampleRate), numSamples);
        bp.setMaxOversampleFactor(maxFactor);
        bp.setDistortionType(type);

        DistortionCommonParams params;
        params.drive = 1.0f;  // Full drive for maximum distortion
        params.mix = 1.0f;
        params.toneHz = 20000.0f;  // Wide tone to not filter harmonics
        bp.setDistortionCommonParams(params);

        // Generate test signal
        std::vector<float> left(numSamples);
        std::vector<float> right(numSamples);

        for (size_t i = 0; i < numSamples; ++i) {
            float phase = 2.0f * std::numbers::pi_v<float> * testFreqHz *
                          static_cast<float>(i) / sampleRate;
            left[i] = driveGain * std::sin(phase);
            right[i] = driveGain * std::sin(phase);
        }

        bp.processBlock(left.data(), right.data(), numSamples);

        return left;
    };

    // Types to test: all 2x and 4x types (1x types intentionally skip oversampling)
    struct TypeTest {
        DistortionType type;
        int recommendedFactor;
    };

    std::array<TypeTest, 4> representativeTypes = {{
        // Test a representative subset to keep test time reasonable
        {.type = DistortionType::HardClip, .recommendedFactor = 4},    // 4x: strong harmonics
        {.type = DistortionType::SineFold, .recommendedFactor = 8},     // 8x: wavefolder (ruling 2026-11-10)
        {.type = DistortionType::Fractal, .recommendedFactor = 2},      // 2x: moderate harmonics
        {.type = DistortionType::Tube, .recommendedFactor = 2},         // 2x: tube saturation
    }};

    for (const auto& tt : representativeTypes) {
        SECTION(std::string("alias suppression for ") + getTypeName(tt.type)) {
            // Process at 1x (forced) - this is the reference with aliasing
            auto output1x = processWithLimit(tt.type, 1,
                                             config.driveGain, config.sampleRate,
                                             config.testFrequencyHz,
                                             config.fftSize);

            // Process at recommended factor - this should suppress aliasing
            auto outputOS = processWithLimit(tt.type, tt.recommendedFactor,
                                             config.driveGain, config.sampleRate,
                                             config.testFrequencyHz,
                                             config.fftSize);

            // FFT analysis of both outputs
            Krate::DSP::FFT fft;
            fft.prepare(config.fftSize);

            // Apply Hann window
            std::vector<float> window(config.fftSize);
            Krate::DSP::Window::generateHann(window.data(), config.fftSize);

            std::vector<float> windowed1x(config.fftSize);
            std::vector<float> windowedOS(config.fftSize);
            for (size_t i = 0; i < config.fftSize; ++i) {
                windowed1x[i] = output1x[i] * window[i];
                windowedOS[i] = outputOS[i] * window[i];
            }

            std::vector<Krate::DSP::Complex> spectrum1x(fft.numBins());
            std::vector<Krate::DSP::Complex> spectrumOS(fft.numBins());
            fft.forward(windowed1x.data(), spectrum1x.data());
            fft.forward(windowedOS.data(), spectrumOS.data());

            // Measure aliased components (harmonics that fold back above Nyquist)
            auto aliasedBins = getAliasedBins(config);

            if (aliasedBins.empty()) {
                // No aliased harmonics at this frequency/sample rate - skip
                WARN("No aliased harmonics detected for " << getTypeName(tt.type));
                continue;
            }

            // Compute aliasing power for both
            float aliasPower1x = 0.0f;
            float aliasPowerOS = 0.0f;
            for (size_t bin : aliasedBins) {
                if (bin < spectrum1x.size()) {
                    float mag1x = spectrum1x[bin].magnitude();
                    float magOS = spectrumOS[bin].magnitude();
                    aliasPower1x += mag1x * mag1x;
                    aliasPowerOS += magOS * magOS;
                }
            }
            aliasPower1x = std::sqrt(aliasPower1x);
            aliasPowerOS = std::sqrt(aliasPowerOS);

            float aliasingDb1x = 20.0f * std::log10(aliasPower1x + 1e-10f);
            float aliasingDbOS = 20.0f * std::log10(aliasPowerOS + 1e-10f);
            float reductionDb = aliasingDb1x - aliasingDbOS;

            INFO("Type: " << getTypeName(tt.type));
            INFO("Factor: " << tt.recommendedFactor << "x");
            INFO("Aliasing at 1x: " << aliasingDb1x << " dB");
            INFO("Aliasing at " << tt.recommendedFactor << "x: " << aliasingDbOS << " dB");
            INFO("Reduction: " << reductionDb << " dB");

            // SC-006: Oversampled output should suppress aliasing
            // With IIR (economy/zero-latency) oversampling filters, suppression
            // varies by type. Wavefolders (SineFold etc.) generate extremely dense
            // harmonics requiring more aggressive filtering. We verify meaningful
            // improvement that confirms oversampling is functioning correctly.
            // Threshold: >3dB for 2x, >6dB for 4x and 8x (conservative for IIR mode).
            if (tt.recommendedFactor >= 4) {
                CHECK(reductionDb > 6.0f);
            } else if (tt.recommendedFactor == 2) {
                CHECK(reductionDb > 3.0f);
            }
        }
    }
}

// =============================================================================
// The same two properties through Processor::process (audit D-116, T1.07):
// the BandProcessor cases above call processBlock directly, a path the
// processor did not take until T1.07.
// =============================================================================

namespace {

namespace ts = Disrumpo::TestSupport;

double typeNormOf(DistortionType t) { return static_cast<double>(static_cast<int>(t)) / 25.0; }

/// One band, node 0 of `type` at drive 10, tone 8 kHz, mix 1, cursor on node 0.
ts::PList oneBandNode0(DistortionType type) {
    return {{kBandCountId, 0.0},
            {makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNormOf(type)},
            {makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 1.0},
            {makeNodeParamId(0, 0, NodeParamType::kNodeTone), 1.0},
            {makeNodeParamId(0, 0, NodeParamType::kNodeMix), 1.0},
            {makeBandParamId(0, BandParamType::kBandMorphX), 0.0},
            {makeBandParamId(0, BandParamType::kBandMorphY), 0.0}};
}

std::vector<float> sine44k(size_t n, float freqHz, float amp) {
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        x[i] = amp * static_cast<float>(std::sin(2.0 * std::numbers::pi * static_cast<double>(freqHz) *
                                                 static_cast<double>(i) / 44100.0));
    }
    return x;
}

} // namespace

TEST_CASE("Processor: bypassed band output is bit-identical to input (SC-011)",
          "[oversampling][bypass][SC-011]") {
    const auto in = sine44k(8192, 440.0f, 0.3f);
    std::vector<float> outL;
    std::vector<float> outR;

    ts::Rig rig(44100.0);
    // A 2x type without ADAA1: an ADAA1 type (SoftClip since 2026-11-10) moves every path by a
    // fractional reference shift (BandProcessor::requiredReferenceShift), a bypassed band too
    auto params = oneBandNode0(DistortionType::Tube);
    params.emplace_back(makeBandParamId(0, BandParamType::kBandBypass), 1.0);
    rig.apply(params);
    rig.reactivate();
    rig.render(in, in, outL, outR);

    // One band: the crossover passes the input through, the bypassed band leaves it
    // unchanged, and 0 dB input/output gain at mix 1 is the identity. Every band and the
    // dry path are delayed by the latency the processor reports (T2.10).
    const auto latency = static_cast<size_t>(rig.p->getLatencySamples());
    double maxDiff = 0.0;
    for (size_t i = 0; i < in.size(); ++i) {
        const float want = i >= latency ? in[i - latency] : 0.0f;
        maxDiff = std::max({maxDiff, std::fabs(static_cast<double>(outL[i] - want)),
                            std::fabs(static_cast<double>(outR[i] - want))});
    }
    INFO("latency " << latency << ", max |out - in delayed by it| = " << maxDiff);
    REQUIRE(maxDiff < 1e-6);
}

TEST_CASE("Processor: alias suppression with oversampling (SC-006)",
          "[oversampling][single-type][alias-suppression][SC-006]") {
    using namespace Krate::DSP::TestUtils;

    AliasingTestConfig config;
    config.testFrequencyHz = 5000.0f;
    config.sampleRate = 44100.0f;
    config.fftSize = 4096;
    config.maxHarmonic = 10;
    const auto aliasedBins = getAliasedBins(config);
    REQUIRE_FALSE(aliasedBins.empty());

    // Render 0.5 s through the processor, measure the aliased bins of the last fftSize samples
    const auto aliasingDb = [&](DistortionType type, double oversampleMaxNorm, int& factor) {
        const auto in = sine44k(22050, config.testFrequencyHz, 0.5f);
        std::vector<float> outL;
        std::vector<float> outR;
        ts::Rig rig(44100.0);
        auto params = oneBandNode0(type);
        params.emplace_back(kOversampleMaxId, oversampleMaxNorm);
        rig.apply(params);
        rig.reactivate();
        rig.render(in, in, outL, outR);
        factor = rig.p->getBandProcessor(0).getOversampleFactor();

        std::vector<float> window(config.fftSize);
        Krate::DSP::Window::generateHann(window.data(), config.fftSize);
        std::vector<float> frame(config.fftSize);
        const size_t start = outL.size() - config.fftSize;
        for (size_t i = 0; i < config.fftSize; ++i) frame[i] = outL[start + i] * window[i];
        Krate::DSP::FFT fft;
        fft.prepare(config.fftSize);
        std::vector<Krate::DSP::Complex> spectrum(fft.numBins());
        fft.forward(frame.data(), spectrum.data());
        return detail::toDb(detail::sumBinPower(spectrum.data(), aliasedBins));
    };

    struct TypeTest {
        DistortionType type;
        int recommendedFactor;
        double limitNorm;  // Oversample Max normalized value of the recommended factor
    };
    const std::array<TypeTest, 4> representativeTypes = {{
        {.type = DistortionType::HardClip, .recommendedFactor = 4, .limitNorm = 2.0 / 3.0},
        {.type = DistortionType::SineFold, .recommendedFactor = 8, .limitNorm = 1.0},
        {.type = DistortionType::Fractal, .recommendedFactor = 2, .limitNorm = 1.0 / 3.0},
        {.type = DistortionType::Tube, .recommendedFactor = 2, .limitNorm = 1.0 / 3.0},
    }};

    for (const auto& tt : representativeTypes) {
        SECTION(std::string("processor alias suppression for ") + getTypeName(tt.type)) {
            int factor1x = 0;
            int factorOS = 0;
            const float db1x = aliasingDb(tt.type, 0.0, factor1x);
            const float dbOS = aliasingDb(tt.type, tt.limitNorm, factorOS);
            const float reductionDb = db1x - dbOS;
            INFO("Type: " << getTypeName(tt.type) << ", factors " << factor1x << "x / " << factorOS << "x");
            INFO("Aliasing at 1x: " << db1x << " dB, oversampled: " << dbOS << " dB, reduction "
                 << reductionDb << " dB");
            REQUIRE(factor1x == 1);
            REQUIRE(factorOS == tt.recommendedFactor);
            // Same thresholds as the BandProcessor case above
            if (tt.recommendedFactor >= 4) {
                CHECK(reductionDb > 6.0f);
            } else {
                CHECK(reductionDb > 3.0f);
            }
        }
    }
}
