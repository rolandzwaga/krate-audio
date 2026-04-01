// ==============================================================================
// Real Sample Noise Diagnostic
// ==============================================================================
// Loads the actual test sample through the analyzer and dumps what the
// oscillator bank receives. This is NOT a pass/fail test — it's a
// diagnostic that prints the data so we can see where noise lives.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analyzer.h"
#include "dsp/sample_analysis.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

using namespace Krate::DSP;

static const std::string kTestSamplePath =
    "C:/test/508325__owstu__female-vocal-single-note-a-x.wav";

TEST_CASE("Real sample diagnostic: analyze and dump partial data",
          "[innexus][diagnostic][.real]")
{
    // Check file exists
    FILE* f = fopen(kTestSamplePath.c_str(), "rb");
    if (!f) {
        WARN("Test sample not found at: " << kTestSamplePath << " — skipping");
        return;
    }
    fclose(f);

    // Run analysis
    Innexus::SampleAnalyzer analyzer;
    analyzer.startAnalysis(kTestSamplePath);

    // Wait for completion (max 30 seconds)
    for (int i = 0; i < 300; ++i) {
        if (analyzer.isComplete()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(analyzer.isComplete());

    auto analysisPtr = analyzer.takeResult();
    REQUIRE(analysisPtr != nullptr);
    Innexus::SampleAnalysis& a = *analysisPtr;
    REQUIRE(!a.frames.empty());

    WARN("Sample: " << kTestSamplePath);
    WARN("Total frames: " << a.frames.size());
    WARN("Sample rate: " << a.sampleRate);
    WARN("Hop time: " << a.hopTimeSec << " sec");
    WARN("Residual frames: " << a.residualFrames.size());

    // --- Dump frame 10 (early, voice should be strong) ---
    if (a.frames.size() > 10) {
        const auto& frame = a.frames[10];
        WARN("=== Frame 10 (early) ===");
        WARN("  F0: " << frame.f0 << " Hz, confidence: " << frame.f0Confidence);
        WARN("  numPartials: " << frame.numPartials);
        WARN("  globalAmplitude: " << frame.globalAmplitude);
        WARN("  noisiness: " << frame.noisiness);
        WARN("  brightness: " << frame.brightness);

        // Find peak amplitude
        float peakAmp = 0.0f;
        for (int i = 0; i < frame.numPartials; ++i)
            if (frame.partials[i].amplitude > peakAmp)
                peakAmp = frame.partials[i].amplitude;

        WARN("  Peak partial amplitude: " << peakAmp);

        // Dump all partials
        for (int i = 0; i < std::min(frame.numPartials, 48); ++i) {
            const auto& p = frame.partials[i];
            float dbBelowPeak = (p.amplitude > 0.0f && peakAmp > 0.0f)
                ? 20.0f * std::log10(p.amplitude / peakAmp) : -999.0f;
            WARN("  Partial " << i << ": freq=" << p.frequency
                 << " amp=" << p.amplitude
                 << " (" << dbBelowPeak << " dB below peak)"
                 << " age=" << p.age
                 << " stability=" << p.stability
                 << " bw=" << p.bandwidth
                 << " harmIdx=" << p.harmonicIndex);
        }
    }

    // --- Dump a frame near the end (voice fading) ---
    size_t lateIdx = a.frames.size() * 3 / 4;
    if (lateIdx < a.frames.size()) {
        const auto& frame = a.frames[lateIdx];
        WARN("=== Frame " << lateIdx << " (late, voice fading) ===");
        WARN("  F0: " << frame.f0 << " Hz, confidence: " << frame.f0Confidence);
        WARN("  numPartials: " << frame.numPartials);
        WARN("  globalAmplitude: " << frame.globalAmplitude);
        WARN("  noisiness: " << frame.noisiness);

        float peakAmp = 0.0f;
        for (int i = 0; i < frame.numPartials; ++i)
            if (frame.partials[i].amplitude > peakAmp)
                peakAmp = frame.partials[i].amplitude;

        WARN("  Peak partial amplitude: " << peakAmp);

        for (int i = 0; i < std::min(frame.numPartials, 48); ++i) {
            const auto& p = frame.partials[i];
            float dbBelowPeak = (p.amplitude > 0.0f && peakAmp > 0.0f)
                ? 20.0f * std::log10(p.amplitude / peakAmp) : -999.0f;
            WARN("  Partial " << i << ": freq=" << p.frequency
                 << " amp=" << p.amplitude
                 << " (" << dbBelowPeak << " dB below peak)"
                 << " age=" << p.age
                 << " stability=" << p.stability
                 << " bw=" << p.bandwidth
                 << " harmIdx=" << p.harmonicIndex);
        }
    }

    // --- Statistics across all frames ---
    float maxNoisiness = 0.0f;
    float avgNoisiness = 0.0f;
    int maxPartials = 0;
    float maxBandwidth = 0.0f;
    int framesWithHighNoisiness = 0;
    int framesWithManyPartials = 0;

    for (size_t fi = 0; fi < a.frames.size(); ++fi) {
        const auto& frame = a.frames[fi];
        if (frame.noisiness > maxNoisiness) maxNoisiness = frame.noisiness;
        avgNoisiness += frame.noisiness;
        if (frame.numPartials > maxPartials) maxPartials = frame.numPartials;
        if (frame.noisiness > 0.5f) ++framesWithHighNoisiness;
        if (frame.numPartials > 30) ++framesWithManyPartials;
        for (int i = 0; i < frame.numPartials; ++i)
            if (frame.partials[i].bandwidth > maxBandwidth)
                maxBandwidth = frame.partials[i].bandwidth;
    }
    avgNoisiness /= static_cast<float>(a.frames.size());

    WARN("=== Global Statistics ===");
    WARN("  Max noisiness: " << maxNoisiness);
    WARN("  Avg noisiness: " << avgNoisiness);
    WARN("  Max partials in a frame: " << maxPartials);
    WARN("  Max bandwidth: " << maxBandwidth);
    size_t totalFrameCount = a.frames.size();
    WARN("  Frames with noisiness > 0.5: " << framesWithHighNoisiness
         << " / " << totalFrameCount);
    WARN("  Frames with > 30 partials: " << framesWithManyPartials
         << " / " << totalFrameCount);

    // --- Residual energy ---
    if (!a.residualFrames.empty()) {
        float maxResEnergy = 0.0f;
        float avgResEnergy = 0.0f;
        size_t resCount = a.residualFrames.size();
        for (size_t ri = 0; ri < resCount; ++ri) {
            float e = a.residualFrames[ri].totalEnergy;
            if (e > maxResEnergy) maxResEnergy = e;
            avgResEnergy += e;
        }
        avgResEnergy /= static_cast<float>(resCount);

        WARN("  Max residual totalEnergy: " << maxResEnergy);
        WARN("  Avg residual totalEnergy: " << avgResEnergy);
    }

    // Force all INFO output by failing an assertion
    REQUIRE(a.frames.size() == 0); // intentional fail to dump data

}