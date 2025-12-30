// ==============================================================================
// Benchmark: Mode Crossfade Performance (spec 041)
// ==============================================================================
// Verifies T045: Mode crossfade adds minimal CPU overhead
//
// Methodology:
//   - Process realistic audio buffers (512 samples, stereo)
//   - Compare processing time: single mode vs during crossfade
//   - Expected overhead: <2x (since we process two modes during crossfade)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety verification
// ==============================================================================

#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <vector>
#include <random>

#include "dsp/features/digital_delay.h"
#include "dsp/core/crossfade_utils.h"

using namespace Iterum::DSP;

int main() {
    constexpr double SAMPLE_RATE = 44100.0;
    constexpr size_t BLOCK_SIZE = 512;
    constexpr int NUM_ITERATIONS = 1000;
    constexpr float CROSSFADE_TIME_MS = 50.0f;  // Same as Processor

    // Calculate time budget for one block
    const double blockDurationMs = (static_cast<double>(BLOCK_SIZE) / SAMPLE_RATE) * 1000.0;
    const float crossfadeInc = crossfadeIncrement(CROSSFADE_TIME_MS, SAMPLE_RATE);

    std::cout << "Mode Crossfade Benchmark (spec 041-mode-switch-clicks)\n";
    std::cout << "=================================================================\n";
    std::cout << "Sample Rate: " << SAMPLE_RATE << " Hz\n";
    std::cout << "Block Size: " << BLOCK_SIZE << " samples\n";
    std::cout << "Block Duration: " << std::fixed << std::setprecision(3) << blockDurationMs << " ms\n";
    std::cout << "Crossfade Time: " << CROSSFADE_TIME_MS << " ms\n";
    std::cout << "=================================================================\n\n";

    // Create two delay engines (simulating mode A and mode B)
    DigitalDelay delayA;
    DigitalDelay delayB;

    delayA.prepare(SAMPLE_RATE, BLOCK_SIZE);
    delayB.prepare(SAMPLE_RATE, BLOCK_SIZE);

    // Configure delays slightly differently
    delayA.setDelayTime(300.0f);
    delayA.setFeedback(0.5f);
    delayA.setMix(0.5f);

    delayB.setDelayTime(400.0f);
    delayB.setFeedback(0.6f);
    delayB.setMix(0.5f);

    // Allocate buffers
    std::vector<float> inputL(BLOCK_SIZE);
    std::vector<float> inputR(BLOCK_SIZE);
    std::vector<float> outputL(BLOCK_SIZE);
    std::vector<float> outputR(BLOCK_SIZE);
    std::vector<float> crossfadeBufferL(BLOCK_SIZE);
    std::vector<float> crossfadeBufferR(BLOCK_SIZE);

    // Generate noise input
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        inputL[i] = dist(rng);
        inputR[i] = dist(rng);
    }

    // Warm up
    for (int i = 0; i < 10; ++i) {
        delayA.processStereo(inputL.data(), inputR.data(),
                             outputL.data(), outputR.data(), BLOCK_SIZE);
        delayB.processStereo(inputL.data(), inputR.data(),
                             outputL.data(), outputR.data(), BLOCK_SIZE);
    }

    // =========================================================================
    // Benchmark 1: Single Mode Processing (baseline)
    // =========================================================================
    auto start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        delayA.processStereo(inputL.data(), inputR.data(),
                             outputL.data(), outputR.data(), BLOCK_SIZE);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double singleModeTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    double singleModeAvgMs = singleModeTimeMs / NUM_ITERATIONS;
    double singleModeCpu = (singleModeAvgMs / blockDurationMs) * 100.0;

    std::cout << "SINGLE MODE (Digital Delay only):\n";
    std::cout << "  Avg per block: " << std::setprecision(4) << singleModeAvgMs << " ms\n";
    std::cout << "  CPU usage: " << std::setprecision(2) << singleModeCpu << "%\n\n";

    // =========================================================================
    // Benchmark 2: Dual Mode Processing (simulating crossfade)
    // =========================================================================
    start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // Process current mode
        delayA.processStereo(inputL.data(), inputR.data(),
                             outputL.data(), outputR.data(), BLOCK_SIZE);

        // Process previous mode (into crossfade buffer)
        delayB.processStereo(inputL.data(), inputR.data(),
                             crossfadeBufferL.data(), crossfadeBufferR.data(), BLOCK_SIZE);

        // Apply equal-power crossfade (simulated during-crossfade scenario)
        float position = 0.0f;
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            float fadeOut, fadeIn;
            equalPowerGains(position, fadeOut, fadeIn);

            outputL[i] = crossfadeBufferL[i] * fadeOut + outputL[i] * fadeIn;
            outputR[i] = crossfadeBufferR[i] * fadeOut + outputR[i] * fadeIn;

            position += crossfadeInc;
            if (position > 1.0f) position = 1.0f;
        }
    }
    end = std::chrono::high_resolution_clock::now();

    double dualModeTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    double dualModeAvgMs = dualModeTimeMs / NUM_ITERATIONS;
    double dualModeCpu = (dualModeAvgMs / blockDurationMs) * 100.0;

    std::cout << "DUAL MODE (Two delays + Crossfade):\n";
    std::cout << "  Avg per block: " << std::setprecision(4) << dualModeAvgMs << " ms\n";
    std::cout << "  CPU usage: " << std::setprecision(2) << dualModeCpu << "%\n\n";

    // =========================================================================
    // Benchmark 3: Crossfade Overhead Only (just the blending)
    // =========================================================================
    start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        float position = 0.0f;
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            float fadeOut, fadeIn;
            equalPowerGains(position, fadeOut, fadeIn);

            outputL[i] = crossfadeBufferL[i] * fadeOut + outputL[i] * fadeIn;
            outputR[i] = crossfadeBufferR[i] * fadeOut + outputR[i] * fadeIn;

            position += crossfadeInc;
            if (position > 1.0f) position = 1.0f;
        }
    }
    end = std::chrono::high_resolution_clock::now();

    double crossfadeOnlyTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    double crossfadeOnlyAvgMs = crossfadeOnlyTimeMs / NUM_ITERATIONS;
    double crossfadeOnlyCpu = (crossfadeOnlyAvgMs / blockDurationMs) * 100.0;

    std::cout << "CROSSFADE OVERHEAD ONLY (equal-power blending):\n";
    std::cout << "  Avg per block: " << std::setprecision(4) << crossfadeOnlyAvgMs << " ms\n";
    std::cout << "  CPU usage: " << std::setprecision(2) << crossfadeOnlyCpu << "%\n\n";

    // =========================================================================
    // Analysis
    // =========================================================================
    std::cout << "=================================================================\n";
    std::cout << "ANALYSIS:\n";

    double overhead = dualModeAvgMs / singleModeAvgMs;
    std::cout << "  Dual/Single ratio: " << std::setprecision(2) << overhead << "x\n";
    std::cout << "  Expected: ~2x (processing two modes)\n";

    double crossfadeOverheadPercent = (crossfadeOnlyAvgMs / singleModeAvgMs) * 100.0;
    std::cout << "  Crossfade blend overhead: " << std::setprecision(1) << crossfadeOverheadPercent << "% of single mode\n";

    std::cout << "=================================================================\n";

    // Verdict
    bool overheadAcceptable = overhead < 2.5;  // Allow some margin over 2x
    bool cpuAcceptable = dualModeCpu < 10.0;   // Total CPU during crossfade < 10%

    std::cout << "T045 (overhead < 2.5x): " << (overheadAcceptable ? "PASS" : "FAIL") << "\n";
    std::cout << "T045 (CPU < 10%): " << (cpuAcceptable ? "PASS" : "FAIL") << "\n";

    return (overheadAcceptable && cpuAcceptable) ? 0 : 1;
}
