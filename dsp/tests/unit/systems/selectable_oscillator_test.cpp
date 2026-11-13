// ==============================================================================
// Tests: SelectableOscillator
// ==============================================================================
// Unit tests for the variant-based oscillator wrapper with lazy initialization.
//
// Feature: 041-ruinae-voice-architecture (User Story 3)
// Test-First: Constitution Principle XII
// ==============================================================================

#include <krate/dsp/systems/selectable_oscillator.h>
#include <krate/dsp/systems/oscillator_types.h>
#include <krate/dsp/systems/voice_mod_types.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/math_constants.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <atomic>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <utility>
#include <vector>

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

TEST_CASE("DSP types: enum sizes", "[dsp_types]") {
    STATIC_REQUIRE(static_cast<uint8_t>(OscType::NumTypes) == 10);
    STATIC_REQUIRE(static_cast<uint8_t>(VoiceModSource::NumSources) == 8);
    STATIC_REQUIRE(static_cast<uint8_t>(VoiceModDest::NumDestinations) == 10);
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
// RMS Level Measurement Across All Oscillator Types
// =============================================================================
// Measures the RMS output (in dBFS) of each oscillator type at 440 Hz over
// 1 second. Used to verify that gain compensation in OscillatorAdapter keeps
// all types within a reasonable loudness range of each other.

TEST_CASE("SelectableOscillator: RMS levels within +/-3 dB across all types",
          "[selectable_oscillator][rms_levels]") {
    constexpr size_t kOneSec = 44100;
    constexpr size_t kChunkSize = 512;
    constexpr float kFrequency = 440.0f;

    // Reference: PolyBLEP sawtooth (most common waveform, middle-of-the-road energy)
    // All other types should be within +/-3 dB of this reference.
    constexpr float kMaxDeviationDb = 3.0f;

    // Measure RMS for each type
    struct Measurement {
        OscType type;
        const char* name;
        float rmsDbfs;
    };

    // SpectralFreeze is excluded: it resynthesizes input, not a self-generating
    // oscillator in the same sense. Noise is also excluded since it has no
    // pitch and its RMS depends on color, not gain compensation.
    const std::array<std::pair<OscType, const char*>, 8> typesToMeasure = {{
        {OscType::PolyBLEP, "PolyBLEP"},
        {OscType::Wavetable, "Wavetable"},
        {OscType::PhaseDistortion, "PhaseDistortion"},
        {OscType::Sync, "Sync"},
        {OscType::Additive, "Additive"},
        {OscType::Chaos, "Chaos"},
        {OscType::Particle, "Particle"},
        {OscType::Formant, "Formant"},
    }};

    // First pass: measure all types and find the reference (PolyBLEP)
    std::array<float, 8> rmsValues{};
    float referenceDbfs = 0.0f;

    for (size_t t = 0; t < typesToMeasure.size(); ++t) {
        auto [type, name] = typesToMeasure[t];

        SelectableOscillator osc;
        osc.prepare(kSampleRate, kChunkSize);
        osc.setType(type);
        osc.setFrequency(kFrequency);

        double totalSumSq = 0.0;
        size_t totalSamples = 0;
        std::array<float, kChunkSize> buffer{};

        for (size_t processed = 0; processed < kOneSec; processed += kChunkSize) {
            size_t chunk = std::min(kChunkSize, kOneSec - processed);
            osc.processBlock(buffer.data(), chunk);
            for (size_t i = 0; i < chunk; ++i) {
                totalSumSq += static_cast<double>(buffer[i])
                            * static_cast<double>(buffer[i]);
            }
            totalSamples += chunk;
        }

        float rms = static_cast<float>(
            std::sqrt(totalSumSq / static_cast<double>(totalSamples)));
        rmsValues[t] = rmsToDbfs(rms);

        if (type == OscType::PolyBLEP) {
            referenceDbfs = rmsValues[t];
        }
    }

    // Print all measurements for diagnostics
    for (size_t t = 0; t < typesToMeasure.size(); ++t) {
        INFO(typesToMeasure[t].second << ": " << rmsValues[t]
             << " dBFS (delta: " << (rmsValues[t] - referenceDbfs) << " dB)");
    }

    // Second pass: verify each is within tolerance of reference
    for (size_t t = 0; t < typesToMeasure.size(); ++t) {
        auto [type, name] = typesToMeasure[t];
        float delta = rmsValues[t] - referenceDbfs;

        DYNAMIC_SECTION(name << " within +/-" << kMaxDeviationDb << " dB of PolyBLEP") {
            INFO(name << " RMS = " << rmsValues[t] << " dBFS, reference = "
                 << referenceDbfs << " dBFS, delta = " << delta << " dB");
            REQUIRE(delta >= -kMaxDeviationDb);
            REQUIRE(delta <= kMaxDeviationDb);
        }
    }
}

// =============================================================================
// Zero Heap Allocations During Type Switch (SC-004)
// =============================================================================
// Note: We test non-FFT types only because FFT-based oscillators
// (SpectralFreeze, Additive) may allocate during prepare.
// The allocation tracking uses a simple counter approach.

// Global allocation-operator replacements live in ONE shared header so the
// matched set and its visibility cannot drift per-TU (this TU previously
// defined new/delete/sized-delete only -- no array or nothrow forms, and no
// default visibility). Tracking now goes through AllocationDetector.
#include <allocation_operator_overrides.h>


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
            TestHelpers::AllocationDetector::instance().startTracking();

            osc.setType(type);

            const int allocs = static_cast<int>(TestHelpers::AllocationDetector::instance().stopTracking());

            INFO("OscType " << static_cast<int>(type) << " caused " << allocs << " allocations");
            REQUIRE(allocs == 0);
        }
    }
}

// =============================================================================
// OscillatorSlot::setParam() Base Behavior (FR-001)
// =============================================================================

TEST_CASE("OscillatorSlot: base setParam is silent no-op",
          "[selectable_oscillator][setParam]") {
    // SelectableOscillator with no active slot (before prepare) should not crash
    SelectableOscillator osc;
    // setParam before prepare -- no active_ pointer, should be a no-op
    osc.setParam(OscParam::Waveform, 1.0f);
    osc.setParam(OscParam::NoiseColor, 3.0f);
    osc.setParam(OscParam::ChaosAmount, 0.5f);
    // Reaching here without crash = pass
    REQUIRE(true);
}

// =============================================================================
// setParam() Dispatch for All 10 Adapter Types (FR-002)
// =============================================================================

TEST_CASE("SelectableOscillator: PolyBLEP setParam dispatches correctly",
          "[selectable_oscillator][setParam]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::PolyBLEP);
    osc.setFrequency(440.0f);

    // Set waveform to Sawtooth (1) -- should change output
    std::array<float, kBlockSize> bufSine{};
    osc.setParam(OscParam::Waveform, 0.0f);  // Sine
    osc.processBlock(bufSine.data(), kBlockSize);
    float rmsSine = computeRMS(bufSine.data(), kBlockSize);

    osc.setParam(OscParam::Waveform, 1.0f);  // Sawtooth
    std::array<float, kBlockSize> bufSaw{};
    osc.processBlock(bufSaw.data(), kBlockSize);
    float rmsSaw = computeRMS(bufSaw.data(), kBlockSize);

    // Both should produce output
    REQUIRE(rmsSine > 0.001f);
    REQUIRE(rmsSaw > 0.001f);

    // Set pulse width
    osc.setParam(OscParam::Waveform, 3.0f);  // Pulse
    osc.setParam(OscParam::PulseWidth, 0.1f);
    std::array<float, kBlockSize> bufPulse{};
    osc.processBlock(bufPulse.data(), kBlockSize);
    REQUIRE(computeRMS(bufPulse.data(), kBlockSize) > 0.001f);

    // Phase Modulation and Frequency Modulation should not crash
    osc.setParam(OscParam::PhaseModulation, 0.5f);
    osc.setParam(OscParam::FrequencyModulation, 0.3f);
    osc.processBlock(bufPulse.data(), kBlockSize);
    REQUIRE(computeRMS(bufPulse.data(), kBlockSize) > 0.001f);

    // Unrecognized params should be silently ignored
    osc.setParam(OscParam::NoiseColor, 2.0f);
    osc.setParam(OscParam::ChaosAmount, 0.5f);
}

TEST_CASE("SelectableOscillator: Wavetable setParam dispatches PM/FM",
          "[selectable_oscillator][setParam]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::Wavetable);
    osc.setFrequency(440.0f);

    // PM and FM shared with PolyBLEP
    osc.setParam(OscParam::PhaseModulation, 0.5f);
    osc.setParam(OscParam::FrequencyModulation, 0.3f);
    std::array<float, kBlockSize> buffer{};
    osc.processBlock(buffer.data(), kBlockSize);
    REQUIRE(computeRMS(buffer.data(), kBlockSize) > 0.001f);

    // Unrecognized params silently ignored
    osc.setParam(OscParam::Waveform, 2.0f);
}

TEST_CASE("SelectableOscillator: PhaseDistortion setParam dispatches correctly",
          "[selectable_oscillator][setParam]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::PhaseDistortion);
    osc.setFrequency(440.0f);

    osc.setParam(OscParam::PDWaveform, 0.0f);  // Saw
    osc.setParam(OscParam::PDDistortion, 0.8f);
    std::array<float, kBlockSize> buffer{};
    osc.processBlock(buffer.data(), kBlockSize);
    REQUIRE(computeRMS(buffer.data(), kBlockSize) > 0.001f);

    // Unrecognized
    osc.setParam(OscParam::Waveform, 1.0f);
}

TEST_CASE("SelectableOscillator: Sync setParam dispatches correctly",
          "[selectable_oscillator][setParam]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::Sync);
    osc.setFrequency(220.0f);

    osc.setParam(OscParam::SyncSlaveRatio, 3.0f);
    osc.setParam(OscParam::SyncSlaveWaveform, 1.0f);  // Sawtooth
    osc.setParam(OscParam::SyncMode, 0.0f);            // Hard
    osc.setParam(OscParam::SyncAmount, 1.0f);
    osc.setParam(OscParam::SyncSlavePulseWidth, 0.5f);
    std::array<float, kBlockSize> buffer{};
    osc.processBlock(buffer.data(), kBlockSize);
    REQUIRE(computeRMS(buffer.data(), kBlockSize) > 0.001f);

    // Unrecognized
    osc.setParam(OscParam::NoiseColor, 1.0f);
}

TEST_CASE("SelectableOscillator: Sync adapter uses stored slaveRatio",
          "[selectable_oscillator][setParam]") {
    // Verify that setting SyncSlaveRatio changes the slave frequency
    // relative to master, not just during setParam but also during setFrequency
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::Sync);
    osc.setFrequency(220.0f);

    // Set ratio to 4x
    osc.setParam(OscParam::SyncSlaveRatio, 4.0f);

    // Process block with ratio 4x
    std::array<float, kBlockSize> buf4x{};
    osc.processBlock(buf4x.data(), kBlockSize);

    // Now change frequency -- ratio should be preserved
    osc.setFrequency(330.0f);
    std::array<float, kBlockSize> buf4xNewFreq{};
    osc.processBlock(buf4xNewFreq.data(), kBlockSize);
    REQUIRE(computeRMS(buf4xNewFreq.data(), kBlockSize) > 0.001f);

    // Set ratio to 1.0 (no sync effect) and compare
    osc.setParam(OscParam::SyncSlaveRatio, 1.0f);
    std::array<float, kBlockSize> buf1x{};
    osc.processBlock(buf1x.data(), kBlockSize);
    REQUIRE(computeRMS(buf1x.data(), kBlockSize) > 0.001f);
}

TEST_CASE("SelectableOscillator: Additive setParam dispatches correctly",
          "[selectable_oscillator][setParam]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::Additive);
    osc.setFrequency(440.0f);

    osc.setParam(OscParam::AdditiveNumPartials, 32.0f);
    osc.setParam(OscParam::AdditiveSpectralTilt, -6.0f);
    osc.setParam(OscParam::AdditiveInharmonicity, 0.3f);
    std::array<float, kBlockSize> buffer{};
    osc.processBlock(buffer.data(), kBlockSize);
    REQUIRE(computeRMS(buffer.data(), kBlockSize) > 0.001f);

    // Unrecognized
    osc.setParam(OscParam::ChaosAttractor, 2.0f);
}

// AdditiveOscillator sums its partials at amplitude 1 each (times the tilt) and
// hard-clamps the sum at +-2, so in Ruinae every Additive patch above one partial
// clipped: 128 partials at +4 dB/oct spent 96 % of their samples on the clamp, a
// full-scale square wave (Arctic Frost). The adapter must scale the partial sum so
// that, for every partial count and tilt, the output never reaches the clamp and
// keeps about the loudness of a single partial.
//
// The adapter scales its output back up by 1 / kAdditiveLevel after the oscillator,
// so the oscillator clamp of +-2 shows at its output as +-2 / kAdditiveLevel.
TEST_CASE("SelectableOscillator: Additive output never reaches the clamp and keeps its level across partial counts",
          "[selectable_oscillator][additive]") {
    constexpr size_t kLen = 22050;   // 0.5 s
    constexpr size_t kSkip = 2048;   // IFFT warm-up (latency 1024) plus one frame
    constexpr float kClampAtOutput = 2.0f / OscillatorAdapter<AdditiveOscillator>::kAdditiveLevel;
    const auto render = [](float hz, size_t partials, float tilt, float inharm) {
        SelectableOscillator osc;
        osc.prepare(kSampleRate, kBlockSize);
        osc.setType(OscType::Additive);
        osc.setFrequency(hz);
        osc.setParam(OscParam::AdditiveNumPartials, static_cast<float>(partials));
        osc.setParam(OscParam::AdditiveSpectralTilt, tilt);
        osc.setParam(OscParam::AdditiveInharmonicity, inharm);
        osc.reset();
        std::vector<float> out(kLen, 0.0f);
        for (size_t pos = 0; pos < kLen; pos += kBlockSize) {
            osc.processBlock(out.data() + pos, std::min(kBlockSize, kLen - pos));
        }
        float peak = 0.0f;
        for (size_t i = kSkip; i < kLen; ++i) peak = std::max(peak, std::abs(out[i]));
        return std::pair<float, float>{peak, computeRMS(out.data() + kSkip, kLen - kSkip)};
    };

    for (float hz : {65.41f, 261.63f, 1046.5f}) {
        const float singleRms = render(hz, 1, 0.0f, 0.0f).second;
        REQUIRE(singleRms > 0.1f);  // not vacuous
        float worstPeak = 0.0f;
        float lowDb = 0.0f;
        float highDb = 0.0f;
        for (float inharm : {0.0f, 0.1f}) {
            for (float tilt : {-24.0f, -12.0f, -6.0f, 0.0f, 4.0f, 8.0f, 12.0f}) {
                for (size_t partials = 1; partials <= 128; ++partials) {
                    const auto [peak, rms] = render(hz, partials, tilt, inharm);
                    const float db = 20.0f * std::log10(rms / singleRms);
                    worstPeak = std::max(worstPeak, peak);
                    lowDb = std::min(lowDb, db);
                    highDb = std::max(highDb, db);
                    INFO(hz << " Hz, " << partials << " partials, tilt " << tilt << " dB/oct, inharmonicity "
                         << inharm << ": peak " << peak << ", RMS " << db << " dB re one partial");
                    // The clamp writes exactly 2.0, so any sample at 1.999 or above is clamped.
                    CHECK(peak < 0.9995f * kClampAtOutput);
                    // Partials at distinct frequencies add in power, so a sum normalised by
                    // the root of its summed squared amplitudes has the RMS of one partial;
                    // what is left is the IFFT frames' window ripple and partials that share
                    // a bin (summed coherently), held to +-1.5 dB.
                    CHECK(std::abs(db) <= 1.5f);
                }
            }
        }
        INFO(hz << " Hz: worst peak " << worstPeak << ", RMS band " << lowDb << " to " << highDb << " dB");
        CHECK(worstPeak < 0.9995f * kClampAtOutput);
    }
}

TEST_CASE("SelectableOscillator: Chaos setParam dispatches correctly",
          "[selectable_oscillator][setParam]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::Chaos);
    osc.setFrequency(440.0f);

    osc.setParam(OscParam::ChaosAttractor, 1.0f);  // Rossler
    osc.setParam(OscParam::ChaosAmount, 0.7f);
    osc.setParam(OscParam::ChaosCoupling, 0.3f);
    osc.setParam(OscParam::ChaosOutput, 1.0f);     // Y axis
    std::array<float, kBlockSize> buffer{};
    osc.processBlock(buffer.data(), kBlockSize);
    REQUIRE(computeRMS(buffer.data(), kBlockSize) > 0.001f);

    // Unrecognized
    osc.setParam(OscParam::Waveform, 0.0f);
}

TEST_CASE("SelectableOscillator: Particle setParam dispatches correctly",
          "[selectable_oscillator][setParam]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::Particle);
    osc.setFrequency(440.0f);

    osc.setParam(OscParam::ParticleScatter, 6.0f);
    osc.setParam(OscParam::ParticleDensity, 32.0f);
    osc.setParam(OscParam::ParticleLifetime, 500.0f);
    osc.setParam(OscParam::ParticleSpawnMode, 0.0f);   // Regular
    osc.setParam(OscParam::ParticleEnvType, 0.0f);     // Hann
    osc.setParam(OscParam::ParticleDrift, 0.5f);

    // Process multiple blocks to let particles spawn and produce output
    std::array<float, kBlockSize> buffer{};
    double totalSumSq = 0.0;
    for (int i = 0; i < 20; ++i) {
        osc.processBlock(buffer.data(), kBlockSize);
        for (size_t s = 0; s < kBlockSize; ++s) {
            totalSumSq += static_cast<double>(buffer[s]) * static_cast<double>(buffer[s]);
        }
    }
    float rms = static_cast<float>(std::sqrt(totalSumSq / (20.0 * kBlockSize)));
    REQUIRE(rms > 0.001f);

    // Unrecognized
    osc.setParam(OscParam::PDDistortion, 0.5f);
}

TEST_CASE("SelectableOscillator: Formant setParam dispatches correctly",
          "[selectable_oscillator][setParam]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::Formant);
    osc.setFrequency(220.0f);

    osc.setParam(OscParam::FormantVowel, 2.0f);  // I
    osc.setParam(OscParam::FormantMorph, 1.5f);
    std::array<float, kBlockSize> buffer{};
    osc.processBlock(buffer.data(), kBlockSize);
    REQUIRE(computeRMS(buffer.data(), kBlockSize) > 0.001f);

    // Unrecognized
    osc.setParam(OscParam::SyncAmount, 0.5f);
}

TEST_CASE("SelectableOscillator: SpectralFreeze setParam dispatches correctly",
          "[selectable_oscillator][setParam]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setFrequency(440.0f);
    osc.setType(OscType::SpectralFreeze);

    osc.setParam(OscParam::SpectralPitchShift, 12.0f);
    osc.setParam(OscParam::SpectralTilt, -3.0f);
    osc.setParam(OscParam::SpectralFormantShift, 6.0f);

    // Process several blocks to let overlap-add stabilize
    std::array<float, kBlockSize> buffer{};
    double totalSumSq = 0.0;
    for (int i = 0; i < 50; ++i) {
        osc.processBlock(buffer.data(), kBlockSize);
        for (size_t s = 0; s < kBlockSize; ++s) {
            totalSumSq += static_cast<double>(buffer[s]) * static_cast<double>(buffer[s]);
        }
    }
    float rms = static_cast<float>(std::sqrt(totalSumSq / (50.0 * kBlockSize)));
    REQUIRE(rms > 0.001f);

    // Unrecognized
    osc.setParam(OscParam::Waveform, 1.0f);
}

TEST_CASE("SelectableOscillator: Noise setParam dispatches correctly",
          "[selectable_oscillator][setParam]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::Noise);

    osc.setParam(OscParam::NoiseColor, 2.0f);  // Brown
    std::array<float, kBlockSize> buffer{};
    osc.processBlock(buffer.data(), kBlockSize);
    REQUIRE(computeRMS(buffer.data(), kBlockSize) > 0.001f);

    osc.setParam(OscParam::NoiseColor, 0.0f);  // White
    osc.processBlock(buffer.data(), kBlockSize);
    REQUIRE(computeRMS(buffer.data(), kBlockSize) > 0.001f);

    // Unrecognized
    osc.setParam(OscParam::FormantVowel, 1.0f);
}

// Ruinae re-sends every oscillator parameter at the top of each process() call,
// so NoiseColor reaches the Noise adapter once per host block with the colour
// the oscillator already has. NoiseOscillator::setColor() resets the colouring
// filters on every call (Membrum relies on that per hit), so forwarding the
// unchanged colour restarted brown noise's integrator from zero each block,
// draining its low end: Ruinae's Gut String, Degauss Drift, Tidal Lock and Ping
// Garden played 1.5-3.2 dB apart at 64- and 512-sample buffers. Re-sending the
// current colour must leave the noise alone, so the output cannot depend on the
// block size.
TEST_CASE("SelectableOscillator: Noise output does not depend on the block size when the colour is re-sent per block",
          "[selectable_oscillator][setParam][block_size]") {
    constexpr size_t kNumSamples = 44100;
    const auto render = [](float color, size_t blockSize) {
        SelectableOscillator osc;
        osc.prepare(kSampleRate, 4096);
        osc.setType(OscType::Noise);
        osc.setParam(OscParam::NoiseColor, color);
        std::vector<float> out(kNumSamples, 0.0f);
        for (size_t pos = 0; pos < kNumSamples; pos += blockSize) {
            osc.setParam(OscParam::NoiseColor, color);  // the per-block refresh
            const size_t n = std::min(blockSize, kNumSamples - pos);
            osc.processBlock(out.data() + pos, n);
        }
        return out;
    };

    // Pink, Brown, Blue, Violet, Grey, RadioStatic: every colour with filter state.
    for (float color : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f}) {
        const auto reference = render(color, 4096);
        for (size_t blockSize : {32u, 64u, 512u, 1000u}) {
            const auto out = render(color, blockSize);
            float maxDiff = 0.0f;
            for (size_t i = 0; i < kNumSamples; ++i) {
                maxDiff = std::max(maxDiff, std::abs(out[i] - reference[i]));
            }
            // One seeded generator feeds the same per-sample filter arithmetic,
            // so a colour that survives the re-send renders the same samples;
            // 1e-6 (-120 dB) only absorbs float noise. A filter reset per block
            // changes the waveform by the order of the signal.
            INFO("color " << color << ", block size " << blockSize << ": max |diff| "
                 << maxDiff << ", RMS "
                 << 20.0f * std::log10(computeRMS(out.data(), kNumSamples)) << " dB vs "
                 << 20.0f * std::log10(computeRMS(reference.data(), kNumSamples)) << " dB at 4096");
            CHECK(maxDiff <= 1e-6f);
        }
    }
}

TEST_CASE("SelectableOscillator: setParam with null active is silent no-op",
          "[selectable_oscillator][setParam]") {
    SelectableOscillator osc;
    // Not prepared yet -- active_ is null
    // Should not crash
    osc.setParam(OscParam::Waveform, 1.0f);
    osc.setParam(OscParam::ChaosAmount, 0.5f);
    osc.setParam(OscParam::NoiseColor, 3.0f);
    REQUIRE(true);
}

TEST_CASE("SelectableOscillator: cross-type setParam is silently ignored",
          "[selectable_oscillator][setParam]") {
    // When active type is PolyBLEP, sending Chaos/Noise/Particle params
    // should be silently ignored without crash or side effects
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::PolyBLEP);
    osc.setFrequency(440.0f);

    // Send params for other types -- should all be no-ops
    osc.setParam(OscParam::ChaosAttractor, 2.0f);
    osc.setParam(OscParam::NoiseColor, 3.0f);
    osc.setParam(OscParam::ParticleDensity, 32.0f);
    osc.setParam(OscParam::FormantVowel, 1.0f);
    osc.setParam(OscParam::SpectralPitchShift, 12.0f);
    osc.setParam(OscParam::PDDistortion, 0.5f);
    osc.setParam(OscParam::SyncSlaveRatio, 3.0f);
    osc.setParam(OscParam::AdditiveNumPartials, 64.0f);

    // PolyBLEP should still produce normal output
    std::array<float, kBlockSize> buffer{};
    osc.processBlock(buffer.data(), kBlockSize);
    REQUIRE(computeRMS(buffer.data(), kBlockSize) > 0.001f);
}

TEST_CASE("SelectableOscillator: zero heap allocations during processBlock (SC-004)",
          "[selectable_oscillator][sc004]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setFrequency(440.0f);

    std::array<float, kBlockSize> buffer{};

    // Track allocations during processBlock
    TestHelpers::AllocationDetector::instance().startTracking();

    osc.processBlock(buffer.data(), kBlockSize);

    const int allocs = static_cast<int>(TestHelpers::AllocationDetector::instance().stopTracking());

    INFO("processBlock caused " << allocs << " allocations");
    REQUIRE(allocs == 0);
}

// =============================================================================
// SpectralFreeze note tracking
// =============================================================================
// The SpectralFreeze slot freezes a fixed 440 Hz reference sine at prepare time
// and has no setFrequency() of its own. Its pitch must therefore follow the
// played note through the oscillator's pitch-shift control, or every key on the
// keyboard sounds the same concert A.

namespace {

/// @brief Estimate dominant frequency via FFT peak with parabolic interpolation.
float estimateDominantFrequency(const float* buffer, size_t numSamples,
                                float sampleRate) {
    constexpr size_t kFftSize = 8192;
    if (numSamples < kFftSize) return 0.0f;

    FFT fft;
    fft.prepare(kFftSize);

    std::vector<float> windowed(kFftSize);
    for (size_t i = 0; i < kFftSize; ++i) {
        const float w = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i)
                                               / static_cast<float>(kFftSize));
        windowed[i] = buffer[i] * w;
    }

    std::vector<Complex> spectrum(kFftSize / 2 + 1);
    fft.forward(windowed.data(), spectrum.data());

    // Skip DC and the lowest bins, which collect window leakage.
    size_t bestBin = 0;
    float bestMag = 0.0f;
    for (size_t k = 2; k < kFftSize / 2; ++k) {
        const float mag = spectrum[k].magnitude();
        if (mag > bestMag) {
            bestMag = mag;
            bestBin = k;
        }
    }
    if (bestBin == 0) return 0.0f;

    // Parabolic interpolation around the peak for sub-bin accuracy.
    float refined = static_cast<float>(bestBin);
    if (bestBin > 0 && bestBin + 1 < kFftSize / 2) {
        const float m0 = spectrum[bestBin - 1].magnitude();
        const float m1 = spectrum[bestBin].magnitude();
        const float m2 = spectrum[bestBin + 1].magnitude();
        const float denom = m0 - 2.0f * m1 + m2;
        if (std::abs(denom) > 1e-12f) {
            refined += 0.5f * (m0 - m2) / denom;
        }
    }
    return refined * sampleRate / static_cast<float>(kFftSize);
}

/// @brief Drive a prepared SpectralFreeze oscillator at `hz` and measure its f0.
float renderSpectralFreezeF0(SelectableOscillator& osc, float hz) {
    osc.setFrequency(hz);

    std::array<float, kBlockSize> block{};

    // Let the overlap-add pipeline flush the previous pitch setting.
    for (size_t i = 0; i < 16; ++i) {
        osc.processBlock(block.data(), kBlockSize);
    }

    constexpr size_t kAnalysisLen = 8192;
    std::vector<float> analysis(kAnalysisLen);
    for (size_t i = 0; i < kAnalysisLen; i += kBlockSize) {
        const size_t n = std::min(kBlockSize, kAnalysisLen - i);
        osc.processBlock(block.data(), n);
        std::copy_n(block.data(), n, analysis.data() + i);
    }

    return estimateDominantFrequency(analysis.data(), kAnalysisLen,
                                     static_cast<float>(kSampleRate));
}

} // namespace

TEST_CASE("SelectableOscillator: SpectralFreeze pitch follows the played note",
          "[selectable_oscillator][spectral_freeze][pitch]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::SpectralFreeze);

    // Bin resolution of the per-voice 1024-point FFT is ~43 Hz, and pitch
    // shifting is bin remapping, so the tolerance is proportional rather than
    // tight. 6% is roughly one semitone.
    struct Case { float requested; };
    const std::array<Case, 4> cases{{{220.0f}, {440.0f}, {880.0f}, {1760.0f}}};

    for (const auto& c : cases) {
        const float measured = renderSpectralFreezeF0(osc, c.requested);
        INFO("requested " << c.requested << " Hz, measured " << measured << " Hz");
        REQUIRE(measured == Approx(c.requested).epsilon(0.06));
    }
}

TEST_CASE("SelectableOscillator: SpectralFreeze tracks notes above the old "
          "+24 semitone shift limit",
          "[selectable_oscillator][spectral_freeze][pitch]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::SpectralFreeze);

    // A7 is +36 semitones above the 440 Hz reference: beyond the pitch-shift
    // range the oscillator originally clamped to.
    const float measured = renderSpectralFreezeF0(osc, 3520.0f);
    INFO("requested 3520 Hz, measured " << measured << " Hz");
    REQUIRE(measured == Approx(3520.0f).epsilon(0.06));
}

TEST_CASE("SelectableOscillator: SpectralFreeze pitch-shift parameter offsets "
          "the played note",
          "[selectable_oscillator][spectral_freeze][pitch]") {
    SelectableOscillator osc;
    osc.prepare(kSampleRate, kBlockSize);
    osc.setType(OscType::SpectralFreeze);

    // The preset-facing SpectralPitchShift parameter is an offset on top of note
    // tracking, not a replacement for it: +12 st on a 220 Hz note gives 440 Hz.
    // The note is deliberately not 440 Hz, so an implementation that applied the
    // parameter but ignored the note would land on 880 Hz and fail.
    osc.setParam(OscParam::SpectralPitchShift, 12.0f);

    const float measured = renderSpectralFreezeF0(osc, 220.0f);
    INFO("requested 220 Hz + 12 st, measured " << measured << " Hz");
    REQUIRE(measured == Approx(440.0f).epsilon(0.06));
}
