// ==============================================================================
// AP-003: Morph Transition Artifact Detection Test
// ==============================================================================
// Approval test that verifies morph transitions produce zero audible artifacts.
// Generates a sine sweep input while automating morph position, then checks
// for clicks/pops in the output.
//
// Reference: specs/005-morph-system/spec.md SC-003
// "Morph transitions produce zero audible artifacts (verified by approval test
//  AP-003: sine sweep during morph automation)"
//
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/band_processor.h"
#include "dsp/morph_engine.h"
#include "dsp/morph_node.h"
#include "dsp/distortion_types.h"

#include <test_helpers/test_signals.h>
#include <test_helpers/artifact_detection.h>

#include <cmath>
#include <array>
#include <memory>
#include <numbers>
#include <string>
#include <vector>
#include <algorithm>

using namespace Krate::DSP::TestUtils;

// =============================================================================
// Constants
// =============================================================================

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kTestDuration = 44100;  // 1 second
constexpr float kSweepStartFreq = 100.0f;
constexpr float kSweepEndFreq = 8000.0f;
constexpr float kInputAmplitude = 0.5f;

/// @brief Create a cross-family node setup (saturation vs digital).
/// This is the most challenging case for artifact-free morphing.
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> createCrossFamilySetup() {
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;

    // Node A: Saturation family (Soft Clip)
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[0].commonParams.drive = 3.0f;
    nodes[0].commonParams.mix = 1.0f;
    nodes[0].commonParams.toneHz = 4000.0f;

    // Node B: Digital family (Bitcrush) - different family for cross-family morph
    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Bitcrush);
    nodes[1].commonParams.drive = 2.0f;
    nodes[1].commonParams.mix = 1.0f;
    nodes[1].commonParams.toneHz = 4000.0f;
    nodes[1].params.bitDepth = 8.0f;
    nodes[1].params.sampleRateRatio = 1.0f;

    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::Fuzz);
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SineFold);

    return nodes;
}

/// @brief Create same-family node setup (saturation family only).
std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> createSameFamilySetup() {
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;

    // Both nodes in Saturation family
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[0].commonParams.drive = 2.0f;
    nodes[0].commonParams.mix = 1.0f;

    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::Tube);
    nodes[1].commonParams.drive = 4.0f;
    nodes[1].commonParams.mix = 1.0f;

    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::Fuzz);
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SineFold);

    return nodes;
}

/// @brief Compute maximum sample-to-sample derivative (for click detection).
float computeMaxDerivative(const std::vector<float>& signal) {
    if (signal.size() < 2) return 0.0f;

    float maxDeriv = 0.0f;
    for (size_t i = 1; i < signal.size(); ++i) {
        float deriv = std::abs(signal[i] - signal[i - 1]);
        maxDeriv = std::max(maxDeriv, deriv);
    }
    return maxDeriv;
}

/// @brief Count samples where derivative exceeds threshold.
size_t countClickSamples(const std::vector<float>& signal, float threshold) {
    if (signal.size() < 2) return 0;

    size_t count = 0;
    for (size_t i = 1; i < signal.size(); ++i) {
        float deriv = std::abs(signal[i] - signal[i - 1]);
        if (deriv > threshold) {
            ++count;
        }
    }
    return count;
}

} // anonymous namespace

// =============================================================================
// AP-003: Sine Sweep During Morph Automation
// =============================================================================

TEST_CASE("AP-003: Sine sweep during morph automation - same family", "[approval][morph][SC-003][AP-003]") {
    // Generate sine sweep input
    std::vector<float> input(kTestDuration);
    TestHelpers::generateSweep(input.data(), input.size(),
                               kSweepStartFreq, kSweepEndFreq,
                               static_cast<float>(kSampleRate),
                               kInputAmplitude);

    // Setup band processor with morph engine
    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, 512);

    auto nodes = createSameFamilySetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphSmoothingTime(10.0f);  // 10ms smoothing

    // Start at position 0
    proc.setMorphPosition(0.0f, 0.0f);

    // Let smoothers settle
    for (int i = 0; i < 1000; ++i) {
        float l = 0.0f;
        float r = 0.0f;
        proc.process(l, r);
    }

    // Process while automating morph position (linear sweep 0 to 1)
    std::vector<float> output(kTestDuration);

    for (size_t i = 0; i < kTestDuration; ++i) {
        // Automate morph position: 0 to 1 over the test duration
        float morphPos = static_cast<float>(i) / static_cast<float>(kTestDuration);
        proc.setMorphPosition(morphPos, 0.0f);

        float left = input[i];
        float right = input[i];
        proc.process(left, right);

        output[i] = left;
    }

    // Analyze output for artifacts using click detector
    ClickDetectorConfig config;
    config.sampleRate = static_cast<float>(kSampleRate);
    config.frameSize = 512;
    config.hopSize = 256;
    config.detectionThreshold = 6.0f;  // 6 sigma for strict detection
    config.energyThresholdDb = -50.0f;

    ClickDetector detector(config);
    detector.prepare();

    auto detections = detector.detect(output.data(), output.size());

    // SC-003: Zero audible artifacts
    INFO("Click detections: " << detections.size());
    REQUIRE(detections.empty());

    // Derivative check: the automation adds no step beyond what the stationary nodes
    // produce. The same sweep is rendered with the cursor held on node 0 and on node 1
    // (no automation); the automated render's largest sample-to-sample step may not
    // exceed the larger held step by more than 5 %.
    // A fixed bound of 0.5 held only while the adapters were rated for 8x the host
    // rate and clocked at 1x (D-009, fixed in T1.06): the 4 kHz tone filter then acted
    // as ~500 Hz and the step read 0.10. Correctly tuned, the Tube node (drive 4) at the
    // 8 kHz end of the sweep steps 0.7202 with the cursor parked on it, and the
    // automated render's 0.7202 is that same sample (44096), at every smoothing time
    // from 0 to 50 ms (probe "Probe: AP-003 same-family max derivative sources").
    const auto heldRender = [&](float pos) {
        auto held = std::make_unique<Disrumpo::BandProcessor>();
        held->prepare(kSampleRate, 512);
        held->setMorphNodes(nodes, 2);
        held->setMorphMode(Disrumpo::MorphMode::Linear1D);
        held->setMorphSmoothingTime(10.0f);
        held->setMorphPosition(pos, 0.0f);
        for (int i = 0; i < 1000; ++i) {
            float l = 0.0f;
            float r = 0.0f;
            held->process(l, r);
        }
        std::vector<float> out(kTestDuration);
        for (size_t i = 0; i < kTestDuration; ++i) {
            float l = input[i];
            float r = input[i];
            held->process(l, r);
            out[i] = l;
        }
        return out;
    };
    const float heldDeriv0 = computeMaxDerivative(heldRender(0.0f));
    const float heldDeriv1 = computeMaxDerivative(heldRender(1.0f));
    const float maxDeriv = computeMaxDerivative(output);
    INFO("Max derivative: automated " << maxDeriv << ", held on node 0 " << heldDeriv0
         << ", held on node 1 " << heldDeriv1);
    REQUIRE(maxDeriv <= 1.05f * std::max(heldDeriv0, heldDeriv1));
}

TEST_CASE("AP-003: Sine sweep during morph automation - cross family", "[approval][morph][SC-003][AP-003]") {
    // This is the more challenging case: morphing between different families
    // (Saturation to Digital) which uses parallel processing with crossfade

    std::vector<float> input(kTestDuration);
    TestHelpers::generateSweep(input.data(), input.size(),
                               kSweepStartFreq, kSweepEndFreq,
                               static_cast<float>(kSampleRate),
                               kInputAmplitude);

    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, 512);

    auto nodes = createCrossFamilySetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphSmoothingTime(10.0f);

    proc.setMorphPosition(0.0f, 0.0f);

    // Let smoothers settle
    for (int i = 0; i < 1000; ++i) {
        float l = 0.0f;
        float r = 0.0f;
        proc.process(l, r);
    }

    std::vector<float> output(kTestDuration);

    for (size_t i = 0; i < kTestDuration; ++i) {
        float morphPos = static_cast<float>(i) / static_cast<float>(kTestDuration);
        proc.setMorphPosition(morphPos, 0.0f);

        float left = input[i];
        float right = input[i];
        proc.process(left, right);

        output[i] = left;
    }

    ClickDetectorConfig config;
    config.sampleRate = static_cast<float>(kSampleRate);
    config.frameSize = 512;
    config.hopSize = 256;
    config.detectionThreshold = 6.0f;
    config.energyThresholdDb = -50.0f;

    ClickDetector detector(config);
    detector.prepare();

    auto detections = detector.detect(output.data(), output.size());

    INFO("Cross-family click detections: " << detections.size());
    REQUIRE(detections.size() <= 10);  // Allow slightly more for cross-family

    float maxDeriv = computeMaxDerivative(output);
    INFO("Cross-family max derivative: " << maxDeriv);
    // Digital types (Bitcrush) have intentionally sharp staircase edges. The bound was 1.0
    // while centre pan cost every band -3.01 dB (D-021); the balance law (T1.08) raises
    // every output sample by exactly sqrt(2), so the bound moves by that factor and the
    // verdict for a given shaper output is unchanged.
    REQUIRE(maxDeriv < 1.0f * std::numbers::sqrt2_v<float>);
}

TEST_CASE("AP-003: Rapid morph automation (20Hz LFO)", "[approval][morph][SC-003][SC-007]") {
    // SC-007: System handles rapid automation (20Hz morph modulation) without artifacts

    std::vector<float> input(kTestDuration);
    TestHelpers::generateSine(input.data(), input.size(),
                              440.0f,  // Use pure tone instead of sweep for cleaner analysis
                              static_cast<float>(kSampleRate),
                              kInputAmplitude);

    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, 512);

    auto nodes = createSameFamilySetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphSmoothingTime(5.0f);  // Fast smoothing for rapid automation

    proc.setMorphPosition(0.5f, 0.0f);

    // Let settle
    for (int i = 0; i < 500; ++i) {
        float l = 0.0f;
        float r = 0.0f;
        proc.process(l, r);
    }

    std::vector<float> output(kTestDuration);

    // 20Hz LFO modulation of morph position
    constexpr float kLfoFrequency = 20.0f;
    constexpr float kTwoPi = 6.283185307f;

    for (size_t i = 0; i < kTestDuration; ++i) {
        // Sine LFO at 20Hz, normalized to [0, 1]
        float lfoPhase = kTwoPi * kLfoFrequency * static_cast<float>(i) / static_cast<float>(kSampleRate);
        float morphPos = (std::sin(lfoPhase) + 1.0f) * 0.5f;
        proc.setMorphPosition(morphPos, 0.0f);

        float left = input[i];
        float right = input[i];
        proc.process(left, right);

        output[i] = left;
    }

    // Analyze for clicks
    ClickDetectorConfig config;
    config.sampleRate = static_cast<float>(kSampleRate);
    config.frameSize = 256;  // Smaller frame for faster automation
    config.hopSize = 128;
    config.detectionThreshold = 5.0f;
    config.energyThresholdDb = -50.0f;

    ClickDetector detector(config);
    detector.prepare();

    auto detections = detector.detect(output.data(), output.size());

    INFO("20Hz LFO click detections: " << detections.size());
    REQUIRE(detections.size() <= 5);

    float maxDeriv = computeMaxDerivative(output);
    INFO("20Hz LFO max derivative: " << maxDeriv);
    REQUIRE(maxDeriv < 0.4f);
}

TEST_CASE("AP-003: Morph position step change", "[approval][morph][SC-003]") {
    // Test response to instantaneous position changes (worst case)

    std::vector<float> input(kTestDuration);
    TestHelpers::generateSine(input.data(), input.size(),
                              440.0f,
                              static_cast<float>(kSampleRate),
                              kInputAmplitude);

    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, 512);

    auto nodes = createSameFamilySetup();
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphSmoothingTime(20.0f);  // 20ms smoothing

    proc.setMorphPosition(0.0f, 0.0f);

    // Let settle
    for (int i = 0; i < 1000; ++i) {
        float l = 0.0f;
        float r = 0.0f;
        proc.process(l, r);
    }

    std::vector<float> output(kTestDuration);

    for (size_t i = 0; i < kTestDuration; ++i) {
        // Step changes every ~5000 samples
        if (i == 5000 || i == 15000 || i == 25000 || i == 35000) {
            float newPos = (i == 5000 || i == 25000) ? 1.0f : 0.0f;
            proc.setMorphPosition(newPos, 0.0f);
        }

        float left = input[i];
        float right = input[i];
        proc.process(left, right);

        output[i] = left;
    }

    // Check around the step change points for clicks
    // With 20ms smoothing at 44.1kHz, ~882 samples for transition
    // Check samples around step change (avoiding the transition itself)

    ClickDetectorConfig config;
    config.sampleRate = static_cast<float>(kSampleRate);
    config.frameSize = 512;
    config.hopSize = 256;
    config.detectionThreshold = 5.0f;
    config.energyThresholdDb = -50.0f;

    ClickDetector detector(config);
    detector.prepare();

    auto detections = detector.detect(output.data(), output.size());

    INFO("Step change click detections: " << detections.size());
    REQUIRE(detections.size() <= 10);  // Some tolerance for step changes
}

TEST_CASE("AP-003: Output level consistency during morph", "[approval][morph][SC-002]") {
    // SC-002: Cross-family morph maintains output level within 1dB of single-type output

    Disrumpo::BandProcessor proc;
    proc.prepare(kSampleRate, 512);

    // Two cross-family nodes at drive 0.5 (both near-linear): SoftClip (Saturation) and
    // SineFold (Wavefold), so the parallel cross-family blend is what is measured.
    std::array<Disrumpo::MorphNode, Disrumpo::kMaxMorphNodes> nodes;
    nodes[0] = Disrumpo::MorphNode(0, 0.0f, 0.0f, Disrumpo::DistortionType::SoftClip);
    nodes[0].commonParams.drive = 0.5f;
    nodes[0].commonParams.mix = 1.0f;
    nodes[1] = Disrumpo::MorphNode(1, 1.0f, 0.0f, Disrumpo::DistortionType::SineFold);
    nodes[1].commonParams.drive = 0.5f;
    nodes[1].commonParams.mix = 1.0f;
    nodes[2] = Disrumpo::MorphNode(2, 0.0f, 1.0f, Disrumpo::DistortionType::Fuzz);
    nodes[3] = Disrumpo::MorphNode(3, 1.0f, 1.0f, Disrumpo::DistortionType::SineFold);
    proc.setMorphNodes(nodes, 2);
    proc.setMorphMode(Disrumpo::MorphMode::Linear1D);
    proc.setMorphSmoothingTime(0.1f);  // Very fast for level measurement

    // Measure level at position 0
    proc.setMorphPosition(0.0f, 0.0f);
    for (int i = 0; i < 2000; ++i) {
        float l = 0.0f;
        float r = 0.0f;
        proc.process(l, r);
    }

    float sumSqA = 0.0f;
    constexpr int kMeasureSamples = 4410;  // 100ms
    for (int i = 0; i < kMeasureSamples; ++i) {
        float left = kInputAmplitude * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * static_cast<float>(i) / static_cast<float>(kSampleRate));
        float right = left;
        proc.process(left, right);
        sumSqA += left * left;
    }
    float rmsA = std::sqrt(sumSqA / kMeasureSamples);

    // Measure level at position 0.5 (middle of morph)
    proc.setMorphPosition(0.5f, 0.0f);
    for (int i = 0; i < 2000; ++i) {
        float l = 0.0f;
        float r = 0.0f;
        proc.process(l, r);
    }

    float sumSqMid = 0.0f;
    for (int i = 0; i < kMeasureSamples; ++i) {
        float left = kInputAmplitude * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * static_cast<float>(i) / static_cast<float>(kSampleRate));
        float right = left;
        proc.process(left, right);
        sumSqMid += left * left;
    }
    float rmsMid = std::sqrt(sumSqMid / kMeasureSamples);

    // Measure level at position 1
    proc.setMorphPosition(1.0f, 0.0f);
    for (int i = 0; i < 2000; ++i) {
        float l = 0.0f;
        float r = 0.0f;
        proc.process(l, r);
    }

    float sumSqB = 0.0f;
    for (int i = 0; i < kMeasureSamples; ++i) {
        float left = kInputAmplitude * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * static_cast<float>(i) / static_cast<float>(kSampleRate));
        float right = left;
        proc.process(left, right);
        sumSqB += left * left;
    }
    float rmsB = std::sqrt(sumSqB / kMeasureSamples);

    // Calculate dB differences
    // Avoid log of zero
    rmsA = std::max(rmsA, 1e-10f);
    rmsMid = std::max(rmsMid, 1e-10f);
    rmsB = std::max(rmsB, 1e-10f);

    float levelA_dB = 20.0f * std::log10(rmsA);
    float levelMid_dB = 20.0f * std::log10(rmsMid);
    float levelB_dB = 20.0f * std::log10(rmsB);

    // Average reference level
    float avgRef_dB = (levelA_dB + levelB_dB) / 2.0f;

    INFO("Level at position 0: " << levelA_dB << " dB");
    INFO("Level at position 0.5: " << levelMid_dB << " dB");
    INFO("Level at position 1: " << levelB_dB << " dB");
    INFO("Average reference: " << avgRef_dB << " dB");

    // SC-002: Within 1dB of single-type output at the middle point
    float diffMid = std::abs(levelMid_dB - avgRef_dB);
    REQUIRE(diffMid < 1.0f);
}

// Measurement probe (hidden, fix plan T1.06): where the same-family sweep's max
// derivative comes from. "old" replays the pre-T1.06 band path (engine prepared at
// 8x the host rate, clocked at 1x, centre pan); "auto" is the AP-003 render; "static"
// holds the cursor at 0 or 1 for the whole sweep (no morph automation at all).
TEST_CASE("Probe: AP-003 same-family max derivative sources", "[.][t106-ap003-probe]") {
    std::vector<float> input(kTestDuration);
    TestHelpers::generateSweep(input.data(), input.size(), kSweepStartFreq, kSweepEndFreq,
                               static_cast<float>(kSampleRate), kInputAmplitude);
    auto nodes = createSameFamilySetup();
    const auto report =[&](const char* label, const std::vector<float>& out) {
        float maxD = 0.0f;
        size_t at = 0;
        for (size_t i = 1; i < out.size(); ++i) {
            const float d = std::abs(out[i] - out[i - 1]);
            if (d > maxD) { maxD = d; at = i; }
        }
        const float freq = kSweepStartFreq + (kSweepEndFreq - kSweepStartFreq) *
                                                 static_cast<float>(at) / static_cast<float>(kTestDuration);
        float peak = 0.0f;
        for (const float v : out) peak = std::max(peak, std::abs(v));
        WARN("PROBE ap003 " << label << ": max derivative " << maxD << " at sample " << at
             << " (sweep " << freq << " Hz), peak " << peak);
    };
    const auto bandRender = [&](bool automate, float pos, float smoothingMs) {
        auto proc = std::make_unique<Disrumpo::BandProcessor>();
        proc->prepare(kSampleRate, 512);
        proc->setMorphNodes(nodes, 2);
        proc->setMorphMode(Disrumpo::MorphMode::Linear1D);
        proc->setMorphSmoothingTime(smoothingMs);
        proc->setMorphPosition(pos, 0.0f);
        for (int i = 0; i < 1000; ++i) {
            float l = 0.0f;
            float r = 0.0f;
            proc->process(l, r);
        }
        std::vector<float> out(kTestDuration);
        for (size_t i = 0; i < kTestDuration; ++i) {
            if (automate) {
                proc->setMorphPosition(static_cast<float>(i) / static_cast<float>(kTestDuration), 0.0f);
            }
            float l = input[i];
            float r = input[i];
            proc->process(l, r);
            out[i] = l;
        }
        return out;
    };
    {
        auto engine = std::make_unique<Disrumpo::MorphEngine>();
        engine->prepare(kSampleRate * 8.0, 512 * 8);
        engine->setMode(Disrumpo::MorphMode::Linear1D);
        engine->setSmoothingTime(10.0f);
        engine->setNodes(nodes, 2);
        engine->setMorphPosition(0.0f, 0.0f);
        for (int i = 0; i < 1000; ++i) (void)engine->process(0.0f);
        const float pan = std::cos(std::numbers::pi_v<float> / 4.0f);
        std::vector<float> out(kTestDuration);
        for (size_t i = 0; i < kTestDuration; ++i) {
            engine->setMorphPosition(static_cast<float>(i) / static_cast<float>(kTestDuration), 0.0f);
            out[i] = engine->process(input[i]) * pan;
        }
        report("old (8x-rated engine) auto", out);
    }
    for (const float ms : {0.0f, 5.0f, 10.0f, 20.0f, 50.0f}) {
        const std::string label = "new auto, smoothing " + std::to_string(ms) + " ms";
        report(label.c_str(), bandRender(true, 0.0f, ms));
    }
    report("new static pos 0", bandRender(false, 0.0f, 10.0f));
    report("new static pos 1", bandRender(false, 1.0f, 10.0f));
    // The same static Tube render with the input stopped at 90 % of the sweep (7210 Hz)
    {
        auto out = bandRender(false, 1.0f, 10.0f);
        out.resize(kTestDuration * 9 / 10);
        report("new static pos 1, first 90 % (to 7210 Hz)", out);
    }
    // T1.13: the cross-family sibling's setup (SoftClip drive 3 -> Bitcrush drive 2)
    nodes = createCrossFamilySetup();
    report("cross-family auto, smoothing 10 ms", bandRender(true, 0.0f, 10.0f));
    report("cross-family static pos 0", bandRender(false, 0.0f, 10.0f));
    report("cross-family static pos 1", bandRender(false, 1.0f, 10.0f));
}
