// ==============================================================================
// Benchmark: ShimmerDelay Performance
// ==============================================================================
// Verifies SC-006: ShimmerDelay uses <1% CPU at 44.1kHz stereo
//
// Methodology:
//   - Process realistic audio buffers (512 samples, stereo)
//   - Measure processing time over multiple iterations
//   - Compare against available time budget (buffer duration)
//   - CPU% = (processing time / buffer duration) x 100
//
// Target: <1% CPU at 44.1kHz, 512 samples/block (Layer 4 budget)
// ==============================================================================
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <vector>
#include <random>

#include "dsp/features/shimmer_delay.h"
#include "dsp/core/block_context.h"

using namespace Iterum::DSP;

int main() {
    constexpr double SAMPLE_RATE = 44100.0;
    constexpr size_t BLOCK_SIZE = 512;
    constexpr float MAX_DELAY_MS = 5000.0f;  // 5 second max delay
    constexpr int NUM_ITERATIONS = 1000;

    // Calculate time budget for one block
    const double blockDurationMs = (static_cast<double>(BLOCK_SIZE) / SAMPLE_RATE) * 1000.0;

    std::cout << "ShimmerDelay Benchmark (SC-006): " << NUM_ITERATIONS << " iterations\n";
    std::cout << "Sample Rate: " << SAMPLE_RATE << " Hz\n";
    std::cout << "Block Size: " << BLOCK_SIZE << " samples\n";
    std::cout << "Block Duration: " << std::fixed << std::setprecision(3) << blockDurationMs << " ms\n";
    std::cout << "Target: <1% CPU usage\n";
    std::cout << "=================================================================\n";

    // Create and prepare ShimmerDelay
    ShimmerDelay shimmer;
    shimmer.prepare(SAMPLE_RATE, BLOCK_SIZE, MAX_DELAY_MS);

    // Configure for realistic usage (all features enabled)
    shimmer.setDelayTimeMs(500.0f);
    shimmer.setPitchSemitones(12.0f);       // Octave up
    shimmer.setShimmerMix(100.0f);          // Full shimmer
    shimmer.setFeedbackAmount(0.6f);        // 60% feedback
    shimmer.setDiffusionAmount(70.0f);      // High diffusion
    shimmer.setDiffusionSize(50.0f);
    shimmer.setFilterEnabled(true);
    shimmer.setFilterCutoff(4000.0f);
    shimmer.setDryWetMix(50.0f);
    shimmer.setPitchMode(PitchMode::Granular);  // Default quality mode
    shimmer.snapParameters();

    // Allocate buffers
    std::vector<float> leftBuffer(BLOCK_SIZE);
    std::vector<float> rightBuffer(BLOCK_SIZE);

    // Generate noise input
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        leftBuffer[i] = dist(rng);
        rightBuffer[i] = dist(rng);
    }

    // Create block context
    BlockContext ctx;
    ctx.sampleRate = SAMPLE_RATE;
    ctx.blockSize = BLOCK_SIZE;
    ctx.tempoBPM = 120.0;
    ctx.timeSignatureNumerator = 4;
    ctx.timeSignatureDenominator = 4;
    ctx.isPlaying = true;

    // Warm up
    for (int i = 0; i < 10; ++i) {
        shimmer.process(leftBuffer.data(), rightBuffer.data(), BLOCK_SIZE, ctx);
    }

    // Benchmark stereo processing
    auto start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // Reset buffers with fresh noise each iteration
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            leftBuffer[i] = dist(rng);
            rightBuffer[i] = dist(rng);
        }
        shimmer.process(leftBuffer.data(), rightBuffer.data(), BLOCK_SIZE, ctx);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double totalTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    double avgTimePerBlockMs = totalTimeMs / NUM_ITERATIONS;

    // Calculate CPU percentage
    double cpuPercent = (avgTimePerBlockMs / blockDurationMs) * 100.0;

    std::cout << "Stereo Processing Results (Granular mode):\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << totalTimeMs << " ms\n";
    std::cout << "  Avg per block: " << std::setprecision(4) << avgTimePerBlockMs << " ms\n";
    std::cout << "  Time budget: " << std::setprecision(3) << blockDurationMs << " ms\n";
    std::cout << "  CPU usage: " << std::setprecision(2) << cpuPercent << "%\n";
    std::cout << "=================================================================\n";

    // Also test with Simple mode (lowest CPU)
    shimmer.setPitchMode(PitchMode::Simple);
    shimmer.reset();

    start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            leftBuffer[i] = dist(rng);
            rightBuffer[i] = dist(rng);
        }
        shimmer.process(leftBuffer.data(), rightBuffer.data(), BLOCK_SIZE, ctx);
    }
    end = std::chrono::high_resolution_clock::now();

    totalTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    avgTimePerBlockMs = totalTimeMs / NUM_ITERATIONS;
    double simpleCpuPercent = (avgTimePerBlockMs / blockDurationMs) * 100.0;

    std::cout << "Simple Mode Results:\n";
    std::cout << "  Avg per block: " << std::setprecision(4) << avgTimePerBlockMs << " ms\n";
    std::cout << "  CPU usage: " << std::setprecision(2) << simpleCpuPercent << "%\n";
    std::cout << "=================================================================\n";

    // Also test with PhaseVocoder mode (highest quality/CPU)
    shimmer.setPitchMode(PitchMode::PhaseVocoder);
    shimmer.reset();

    start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            leftBuffer[i] = dist(rng);
            rightBuffer[i] = dist(rng);
        }
        shimmer.process(leftBuffer.data(), rightBuffer.data(), BLOCK_SIZE, ctx);
    }
    end = std::chrono::high_resolution_clock::now();

    totalTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    avgTimePerBlockMs = totalTimeMs / NUM_ITERATIONS;
    double phaseCpuPercent = (avgTimePerBlockMs / blockDurationMs) * 100.0;

    std::cout << "PhaseVocoder Mode Results:\n";
    std::cout << "  Avg per block: " << std::setprecision(4) << avgTimePerBlockMs << " ms\n";
    std::cout << "  CPU usage: " << std::setprecision(2) << phaseCpuPercent << "%\n";
    std::cout << "=================================================================\n";

    // Summary
    std::cout << "\nSummary (SC-006 target: <1% CPU):\n";
    std::cout << "  Simple mode:      " << std::setprecision(2) << simpleCpuPercent << "% ";
    std::cout << (simpleCpuPercent < 1.0 ? "[PASS]" : "[FAIL]") << "\n";
    std::cout << "  Granular mode:    " << cpuPercent << "% ";
    std::cout << (cpuPercent < 1.0 ? "[PASS]" : "[FAIL]") << "\n";
    std::cout << "  PhaseVocoder mode: " << phaseCpuPercent << "% ";
    std::cout << (phaseCpuPercent < 1.0 ? "[PASS]" : "[FAIL] (may exceed for high-quality mode)") << "\n";

    // Return non-zero if Granular mode fails (default mode)
    return (cpuPercent < 1.0) ? 0 : 1;
}
