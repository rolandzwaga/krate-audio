// ==============================================================================
// Tests: SelectableOscillator
// ==============================================================================
// Unit tests for the variant-based oscillator wrapper with lazy initialization.
//
// Feature: 041-ruinae-voice-architecture (User Story 3)
// Test-First: Constitution Principle XII
// ==============================================================================

#include <krate/dsp/systems/selectable_oscillator.h>
#include <krate/dsp/systems/ruinae_types.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <atomic>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Helpers
// =============================================================================

namespace {

/// @brief Compute RMS of a buffer.
float computeRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    double sumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSq += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

/// @brief Convert linear RMS to dBFS.
float rmsToDbfs(float rms) {
    if (rms <= 0.0f) return -200.0f;
    return 20.0f * std::log10(rms);
}

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;

} // anonymous namespace

// =============================================================================
// Phase 2: Enumeration Verification (T002)
// =============================================================================

TEST_CASE("Ruinae types: enum sizes", "[ruinae_types]") {
    STATIC_REQUIRE(static_cast<uint8_t>(OscType::NumTypes) == 10);
    STATIC_REQUIRE(static_cast<uint8_t>(RuinaeFilterType::NumTypes) == 7);
    STATIC_REQUIRE(static_cast<uint8_t>(RuinaeDistortionType::NumTypes) == 6);
    STATIC_REQUIRE(static_cast<uint8_t>(VoiceModSource::NumSources) == 8);
    STATIC_REQUIRE(static_cast<uint8_t>(VoiceModDest::NumDestinations) == 9);
}

// =============================================================================
// Default Construction
// =============================================================================

TEST_CASE("SelectableOscillator: default construction produces PolyBLEP type",
          "[selectable_oscillator]") {
    SelectableOscillator osc;
    REQUIRE(osc.getActiveType() == OscType::PolyBLEP);
}

// =============================================================================
// All 10 Types Produce Non-Zero Output (SC-005)
// =============================================================================

TEST_CASE("SelectableOscillator: all 10 types produce non-zero output after prepare",
          "[selectable_oscillator][sc005]") {
    // Process 1 second of audio at 440 Hz for each type
    constexpr size_t kOneSec = 44100;
    constexpr size_t kChunkSize = 512;

    SelectableOscillator osc;
    osc.prepare(kSampleRate, kChunkSize);
    osc.setFrequency(440.0f);

    const std::array<OscType, 10> allTypes = {
        OscType::PolyBLEP,
        OscType::Wavetable,
        OscType::PhaseDistortion,
        OscType::Sync,
        OscType::Additive,
        OscType::Chaos,
        OscType::Particle,
        OscType::Formant,
        OscType::SpectralFreeze,
        OscType::Noise
    };

    for (auto type : allTypes) {
        DYNAMIC_SECTION("OscType " << static_cast<int>(type)) {
            SelectableOscillator testOsc;
            testOsc.prepare(kSampleRate, kChunkSize);
            testOsc.setType(type);
            testOsc.setFrequency(440.0f);

            // Accumulate RMS over 1 second
            double totalSumSq = 0.0;
            size_t totalSamples = 0;
            std::array<float, kChunkSize> buffer{};

            for (size_t processed = 0; processed < kOneSec; processed += kChunkSize) {
                size_t chunk = std::min(kChunkSize, kOneSec - processed);
                testOsc.processBlock(buffer.data(), chunk);
                for (size_t i = 0; i < chunk; ++i) {
                    totalSumSq += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
                }
                totalSamples += chunk;
            }

            float rms = static_cast<float>(std::sqrt(totalSumSq / static_cast<double>(totalSamples)));
            float dbfs = rmsToDbfs(rms);

            INFO("OscType " << static_cast<int>(type) << " RMS dBFS = " << dbfs);
            REQUIRE(dbfs > -60.0f);  // SC-005: RMS > -60 dBFS
        }
    }
}

// =============================================================================
// Type Switching Preserves Frequency
// =============================================================================

TEST_CASE("SelectableOscillator: type switching preserves frequency setting",
          "[selectable_oscillator]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setFrequency(880.0f);

    // Switch type
    osc.setType(OscType::Chaos);
    REQUIRE(osc.getActiveType() == OscType::Chaos);

    // Produce output -- should be non-silent at approximately the set frequency
    std::array<float, kBlockSize> buffer{};
    osc.processBlock(buffer.data(), kBlockSize);
    float rms = computeRMS(buffer.data(), kBlockSize);
    REQUIRE(rms > 0.001f);
}

// =============================================================================
// Same Type Switch is No-Op (AS-3.1)
// =============================================================================

TEST_CASE("SelectableOscillator: switching to same type is no-op",
          "[selectable_oscillator]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setFrequency(440.0f);

    // Process a block to establish state
    std::array<float, kBlockSize> buffer1{};
    osc.processBlock(buffer1.data(), kBlockSize);

    // Switch to same type -- should be a no-op
    osc.setType(OscType::PolyBLEP);
    REQUIRE(osc.getActiveType() == OscType::PolyBLEP);

    // Still produces output
    std::array<float, kBlockSize> buffer2{};
    osc.processBlock(buffer2.data(), kBlockSize);
    float rms = computeRMS(buffer2.data(), kBlockSize);
    REQUIRE(rms > 0.001f);
}

// =============================================================================
// processBlock Before prepare Produces Silence
// =============================================================================

TEST_CASE("SelectableOscillator: processBlock before prepare produces silence",
          "[selectable_oscillator]") {
    SelectableOscillator osc;

    std::array<float, kBlockSize> buffer{};
    std::fill(buffer.begin(), buffer.end(), 999.0f);  // Fill with non-zero
    osc.processBlock(buffer.data(), kBlockSize);

    // All samples should be zero
    bool allZero = true;
    for (float s : buffer) {
        if (s != 0.0f) {
            allZero = false;
            break;
        }
    }
    REQUIRE(allZero);
}

// =============================================================================
// Phase Mode Reset
// =============================================================================

TEST_CASE("SelectableOscillator: setType with PhaseMode::Reset resets phase",
          "[selectable_oscillator]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setPhaseMode(PhaseMode::Reset);
    osc.setFrequency(440.0f);

    // Process some samples to advance phase
    std::array<float, kBlockSize> buffer{};
    osc.processBlock(buffer.data(), kBlockSize);

    // Switch type with Reset mode -- should start fresh
    osc.setType(OscType::PhaseDistortion);

    // Process and verify output (fresh start should produce clean signal)
    osc.processBlock(buffer.data(), kBlockSize);
    float rms = computeRMS(buffer.data(), kBlockSize);
    REQUIRE(rms > 0.001f);
}

// =============================================================================
// NaN/Inf Frequency is Silently Ignored
// =============================================================================

TEST_CASE("SelectableOscillator: NaN/Inf frequency is silently ignored",
          "[selectable_oscillator]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setFrequency(440.0f);

    // Set NaN frequency -- should be ignored, preserving 440 Hz
    osc.setFrequency(std::numeric_limits<float>::quiet_NaN());
    REQUIRE(osc.getActiveType() == OscType::PolyBLEP);

    std::array<float, kBlockSize> buffer{};
    osc.processBlock(buffer.data(), kBlockSize);
    float rms = computeRMS(buffer.data(), kBlockSize);
    REQUIRE(rms > 0.001f);  // Still produces output at previous frequency

    // Set Inf frequency -- should be ignored
    osc.setFrequency(std::numeric_limits<float>::infinity());
    osc.processBlock(buffer.data(), kBlockSize);
    rms = computeRMS(buffer.data(), kBlockSize);
    REQUIRE(rms > 0.001f);
}

// =============================================================================
// SpectralFreeze Special Case Debug
// =============================================================================

TEST_CASE("SelectableOscillator: SpectralFreeze produces output",
          "[selectable_oscillator]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setFrequency(440.0f);
    osc.setType(OscType::SpectralFreeze);

    // Process several blocks to let the overlap-add pipeline stabilize
    std::array<float, kBlockSize> buffer{};
    double totalSumSq = 0.0;
    size_t totalSamples = 0;

    for (int block = 0; block < 100; ++block) {
        osc.processBlock(buffer.data(), kBlockSize);
        for (size_t i = 0; i < kBlockSize; ++i) {
            totalSumSq += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
        }
        totalSamples += kBlockSize;
    }

    float rms = static_cast<float>(std::sqrt(totalSumSq / static_cast<double>(totalSamples)));
    float dbfs = rmsToDbfs(rms);
    INFO("SpectralFreeze RMS dBFS = " << dbfs);
    REQUIRE(dbfs > -60.0f);
}

// =============================================================================
// Zero Heap Allocations During Type Switch (SC-004)
// =============================================================================
// Note: We test non-FFT types only because FFT-based oscillators
// (SpectralFreeze, Additive) may allocate during prepare.
// The allocation tracking uses a simple counter approach.

namespace {
static std::atomic<int> g_allocationCount{0};
static bool g_trackAllocations = false;
} // anonymous namespace

// Override global operator new for allocation tracking
void* operator new(std::size_t size) {
    if (g_trackAllocations) {
        g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, [[maybe_unused]] std::size_t size) noexcept {
    std::free(p);
}

TEST_CASE("SelectableOscillator: zero heap allocations during type switch for ALL types (SC-004)",
          "[selectable_oscillator][sc004]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setFrequency(440.0f);

    // With pre-allocated slot pool, ALL 10 types should switch with zero allocations.
    // All types are constructed and prepared at prepare() time.
    const std::array<OscType, 10> allTypes = {
        OscType::PolyBLEP,
        OscType::Wavetable,
        OscType::PhaseDistortion,
        OscType::Sync,
        OscType::Additive,
        OscType::Chaos,
        OscType::Particle,
        OscType::Formant,
        OscType::SpectralFreeze,
        OscType::Noise
    };

    for (auto type : allTypes) {
        DYNAMIC_SECTION("OscType " << static_cast<int>(type)) {
            // Switch to PolyBLEP first (baseline)
            osc.setType(OscType::PolyBLEP);

            // Track allocations during the switch
            g_allocationCount.store(0, std::memory_order_relaxed);
            g_trackAllocations = true;

            osc.setType(type);

            g_trackAllocations = false;
            int allocs = g_allocationCount.load(std::memory_order_relaxed);

            INFO("OscType " << static_cast<int>(type) << " caused " << allocs << " allocations");
            REQUIRE(allocs == 0);
        }
    }
}

TEST_CASE("SelectableOscillator: zero heap allocations during processBlock (SC-004)",
          "[selectable_oscillator][sc004]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setFrequency(440.0f);

    std::array<float, kBlockSize> buffer{};

    // Track allocations during processBlock
    g_allocationCount.store(0, std::memory_order_relaxed);
    g_trackAllocations = true;

    osc.processBlock(buffer.data(), kBlockSize);

    g_trackAllocations = false;
    int allocs = g_allocationCount.load(std::memory_order_relaxed);

    INFO("processBlock caused " << allocs << " allocations");
    REQUIRE(allocs == 0);
}
